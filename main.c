#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <SDL3_ttf/SDL_ttf.h>

typedef enum WindowState {
    SELECTING,
    MAIN
} WindowState;

typedef struct AppState {
    SDL_Window *window;
    SDL_Renderer *renderer;

    int window_width;
    int window_height;

    WindowState window_state;

    TTF_TextEngine* text_engine;
    TTF_Font* font;

    TTF_Text* intro_text;

    size_t canvas_width;
    size_t canvas_height;
    uint64_t* color_timestamps;
    uint32_t* color_pixels;
    SDL_Texture* color_texture;
    uint64_t canvas_starttime;

    double canvas_zoom;
    double canvas_x, canvas_y;
    double canvas_rotation;

    bool mouse_down;
    double last_mouse_x;
    double last_mouse_y;
} AppState;

void initialize_canvas(AppState* state){
    SDL_free(state->color_timestamps);
    state->color_timestamps = NULL;
    SDL_free(state->color_pixels);
    state->color_pixels = NULL;
    SDL_DestroyTexture(state->color_texture);
    state->color_texture = NULL;

    state->color_timestamps = SDL_calloc(state->canvas_width*state->canvas_height, sizeof(uint64_t));
    state->color_pixels = SDL_malloc(state->canvas_width*state->canvas_height * sizeof(uint64_t));
    SDL_memset4(state->color_pixels,0xFFFFFFFF,state->canvas_width*state->canvas_height);
    state->color_texture = SDL_CreateTexture(
        state->renderer, 
        SDL_PIXELFORMAT_RGBA8888, 
        SDL_TEXTUREACCESS_STREAMING, 
        state->canvas_width, 
        state->canvas_height
    );

    state->canvas_starttime = SDL_GetTicksNS();
}

void screen_to_canvas(AppState *state ,double screen_x, double screen_y, double* out_canvas_x, double* out_canvas_y) {
    double center_x = (state->window_width / 2.0) + state->canvas_x;
    double center_y = (state->window_height / 2.0) + state->canvas_y;

    double dx = screen_x - center_x;
    double dy = screen_y - center_y;

    double rad = state->canvas_rotation * (SDL_PI_F / 180.0);
    double cos_theta = SDL_cos(rad);
    double sin_theta = SDL_sin(rad);

    double rx = (dx * cos_theta) + (dy * sin_theta);
    double ry = (-dx * sin_theta) + (dy * cos_theta);

    double scale = SDL_pow(2, state->canvas_zoom);
    double sx = rx / scale;
    double sy = ry / scale;

    *out_canvas_x = sx + (state->canvas_width / 2.0);
    *out_canvas_y = sy + (state->canvas_height / 2.0);
}

#define is_inside_rect(x,y,rx,ry,rw,rh) (x>=rx&&x<rx+rw&&y>=ry&&y<ry+rh)


SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[]) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("Failed to initialize SDL: %s",SDL_GetError());
        return SDL_APP_FAILURE;
    }

    if (!TTF_Init()) {
        SDL_Log("Failed to initialize SDL_ttf: %s",SDL_GetError());
        return SDL_APP_FAILURE;
    }

    AppState *state = SDL_calloc(1, sizeof(AppState));
    if (!state) {
        return SDL_APP_FAILURE;
    }
    *appstate = state;

    state->window_width = 400;
    state->window_height = 300;

    if (!SDL_CreateWindowAndRenderer("fill a canvas with a 1px brush", state->window_width, state->window_height, SDL_WINDOW_RESIZABLE, &state->window, &state->renderer)) {
        SDL_Log("Failed to create window and renderer: %s",SDL_GetError());
        return SDL_APP_FAILURE;
    }

    state->window_state = SELECTING;

    state->text_engine = TTF_CreateRendererTextEngine(state->renderer);
    if (!state->text_engine) {
        SDL_Log("Failed to create text engine: %s",SDL_GetError());
        return SDL_APP_FAILURE;
    }

    char font_path[500];
    SDL_snprintf(font_path, 500, "%s/%s", SDL_GetBasePath(), "Roboto-Regular.ttf");
    state->font = TTF_OpenFont(font_path, 16);
    if (!state->font) {
        SDL_Log("Failed to load font: %s",SDL_GetError());
        return SDL_APP_FAILURE;
    }

    TTF_SetFontWrapAlignment(state->font, TTF_HORIZONTAL_ALIGN_CENTER);

    state->intro_text = TTF_CreateText(state->text_engine, state->font, "are you ready to fill in \na canvas using a 1px brush", 0);
    if (!state->intro_text) {
        SDL_Log("Failed to create text object: %s",SDL_GetError());
        return SDL_APP_FAILURE;
    }

    state->canvas_width  = 100;
    state->canvas_height = 100;
    initialize_canvas(state);

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
    AppState *state = (AppState *)appstate;
    switch (event->type) {
        case SDL_EVENT_QUIT: {
            return SDL_APP_SUCCESS;
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN: {
            state->mouse_down = true;
            screen_to_canvas(state, event->button.x,event->button.y, &state->last_mouse_x, &state->last_mouse_y);
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            state->mouse_down = false;
            break;
        }
        case SDL_EVENT_MOUSE_MOTION: {
            if (!state->mouse_down) break;
            double mx = 0, my = 0;
            screen_to_canvas(state, event->motion.x,event->motion.y, &mx, &my);
            // if (!is_inside_rect(mx,my,0,0,state->canvas_width,state->canvas_height)) break;

            int x0 = mx;
            int y0 = my;
            int x1 = state->last_mouse_x;
            int y1 = state->last_mouse_y;

            int dx = SDL_abs(x1 - x0);
            int dy = -SDL_abs(y1 - y0);
            
            int sx = (x0 < x1) ? 1 : -1;
            int sy = (y0 < y1) ? 1 : -1;
            
            int err = dx + dy; 
            int e2;

            while (1) {
                if (is_inside_rect(x0,y0,0,0,state->canvas_width,state->canvas_height)) {
                    size_t index = y0 * state->canvas_width + x0;
                    if (state->color_timestamps && state->color_timestamps[index] == 0) state->color_timestamps[index] = event->motion.timestamp-state->canvas_starttime;
                    if (state->color_pixels) state->color_pixels[index] = 0xFF000000;
                }
                
                if (x0 == x1 && y0 == y1) break;
                
                e2 = 2 * err;
                
                if (e2 >= dy) { 
                    err += dy; 
                    x0 += sx; 
                }
                
                if (e2 <= dx) { 
                    err += dx; 
                    y0 += sy; 
                }
            }

            state->last_mouse_x = mx;
            state->last_mouse_y = my;
            break;
        }
    }
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate) {
    AppState *state = (AppState *)appstate;

    SDL_GetWindowSize(state->window, &state->window_width, &state->window_height);

    SDL_SetRenderDrawColor(state->renderer, 0, 0, 0, 255);
    SDL_RenderClear(state->renderer);

    SDL_UpdateTexture(state->color_texture,NULL,state->color_pixels,state->canvas_width * sizeof(uint32_t));

    double zoom_factor = SDL_pow(2, state->canvas_zoom);

    SDL_FRect canvas_rect = {
        .w = state->canvas_width  * zoom_factor, 
        .h = state->canvas_height * zoom_factor, 
    };
    canvas_rect.x = state->canvas_x + state->window_width/2  - canvas_rect.w/2;
    canvas_rect.y = state->canvas_y + state->window_height/2 - canvas_rect.h/2;

    SDL_RenderTextureRotated(state->renderer, state->color_texture, NULL, &canvas_rect, state->canvas_rotation,NULL,SDL_FLIP_NONE);

    SDL_SetRenderDrawColor(state->renderer, 255, 255, 255, 255);
    
    int intro_text_width = 0;
    int intro_text_height = 0;
    TTF_GetTextSize(state->intro_text,&intro_text_width,&intro_text_height);
    TTF_DrawRendererText(state->intro_text,state->window_width/2 - intro_text_width/2,state->window_height/2 - intro_text_height/2);

    SDL_RenderPresent(state->renderer);

    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result) {
    if (appstate) {
        AppState *state = (AppState *)appstate;
        SDL_DestroyRenderer(state->renderer);
        SDL_DestroyWindow(state->window);
        TTF_DestroyRendererTextEngine(state->text_engine);
        TTF_CloseFont(state->font);
        SDL_free(state);
    }
    SDL_Quit();
}
