#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <SDL3_ttf/SDL_ttf.h>

typedef enum WindowState {
    SELECTING,
    MAIN,
    FINISH
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
    uint32_t* color_timestamps_colors;
    uint32_t* color_pixels;
    SDL_Texture* color_texture;
    uint64_t canvas_starttime;

    double canvas_zoom;
    double canvas_x, canvas_y;
    double canvas_rotation;

    bool mouse_down;
    double last_mouse_x;
    double last_mouse_y;

    TTF_Text* ready_button_text;
    SDL_FRect ready_button_rect;

    TTF_Text* finish_button_text;
    SDL_FRect finish_button_rect;

    TTF_Text* final_screen_text;

    TTF_Text* rotate_buttons_text;
    SDL_FRect rotate_button_right;
    SDL_FRect rotate_button_left;
    SDL_FRect rotate_button_reset;
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
    SDL_SetTextureScaleMode(state->color_texture,SDL_SCALEMODE_PIXELART);

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

#define is_inside_rect(x,y,rx,ry,rw,rh) ((x)>=(rx)&&(x)<((rx)+(rw))&&(y)>=(ry)&&(y)<((ry)+(rh)))
#define is_inside_rect_rect(cx,cy,rect) is_inside_rect((cx),(cy),(rect).x,(rect).y,(rect).w,(rect).h)

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

    state->ready_button_text = TTF_CreateText(state->text_engine,state->font, "yeah i'm ready",0);
    if (!state->ready_button_text) {
        SDL_Log("Failed to create text object: %s",SDL_GetError());
        return SDL_APP_FAILURE;
    }

    int rbrw, rbrh;
    if (!TTF_GetTextSize(state->ready_button_text,&rbrw,&rbrh)) {
        SDL_Log("Failed to measure text: %s",SDL_GetError());
        return SDL_APP_FAILURE;
    }
    state->ready_button_rect.w = rbrw+20;
    state->ready_button_rect.h = rbrh+10;

    state->finish_button_text = TTF_CreateText(state->text_engine,state->font, "i finished i think",0);
    if (!state->finish_button_text) {
        SDL_Log("Failed to create text object: %s",SDL_GetError());
        return SDL_APP_FAILURE;
    }

    int fbrw,fbrh;
    if (!TTF_GetTextSize(state->finish_button_text,&fbrw,&fbrh)) {
        SDL_Log("Failed to measure text: %s",SDL_GetError());
        return SDL_APP_FAILURE;
    }

    state->finish_button_rect.w = fbrw+20;
    state->finish_button_rect.h = fbrh+10;

    state->final_screen_text = TTF_CreateText(state->text_engine,state->font,"You are did it I am so of proud of you!",0);
    if (!state->final_screen_text) {
        SDL_Log("Failed to create text object: %s",SDL_GetError());
        return SDL_APP_FAILURE;
    }

    state->rotate_buttons_text = TTF_CreateText(state->text_engine,state->font,"<  Reset rotation  >",0);
    if (!state->rotate_buttons_text) {
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
            switch (state->window_state){
                case SELECTING: {
                    // if (is_inside_rect(event->button.x,event->button.y,state->ready_button_rect.x,state->ready_button_rect.y,state->ready_button_rect.w,state->ready_button_rect.h)) {
                    if (is_inside_rect_rect(event->button.x,event->button.y,state->ready_button_rect)) {
                        state->window_state = MAIN;
                        initialize_canvas(state);
                    }
                    break;
                }
                case MAIN: {
                    // if (is_inside_rect(event->button.x,event->button.y,state->finish_button_rect.x,state->finish_button_rect.y,state->finish_button_rect.w,state->finish_button_rect.h)) {
                    if (is_inside_rect_rect(event->button.x,event->button.y,state->finish_button_rect)) {
                        bool finished = true;
                        for (size_t i = 0; i < (state->canvas_width*state->canvas_height); ++i) {
                            if (state->color_timestamps[i] == 0) {
                                finished = false;
                                break;
                            }
                        }
                        if (finished) {
                            state->window_state = FINISH;

                            uint64_t max_color = 0;
                            uint64_t min_color = state->color_timestamps[0];
                            for (size_t i = 0; i < (state->canvas_width*state->canvas_height); ++i) {
                                if (state->color_timestamps[i] > max_color) max_color = state->color_timestamps[i];
                                if (state->color_timestamps[i] < min_color) min_color = state->color_timestamps[i];
                            }

                            SDL_free(state->color_timestamps_colors);
                            state->color_timestamps_colors = SDL_calloc(state->canvas_width*state->canvas_height, sizeof(uint32_t));

                            for (size_t i = 0; i < (state->canvas_width*state->canvas_height); ++i) {
                                const double val = (double)(state->color_timestamps[i] - min_color)/(double)max_color;
                                const uint8_t cur_color = val * 255;
                                state->color_timestamps_colors[i] = (cur_color << 24)|(cur_color << 16)|(cur_color << 8)|(255);
                            }

                        } else {
                            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION,"you didnt finish","you did not finish please",state->window);
                        }
                        break;
                    }

                    else if (is_inside_rect_rect(event->button.x,event->button.y,state->rotate_button_left)){
                        state->canvas_rotation -= 10;
                    }
                    else if (is_inside_rect_rect(event->button.x,event->button.y,state->rotate_button_right)){
                        state->canvas_rotation += 10;
                    }
                    else if (is_inside_rect_rect(event->button.x,event->button.y,state->rotate_button_reset)){
                        state->canvas_rotation = 0;
                    }

                    state->mouse_down = event->button.button == SDL_BUTTON_LEFT;
                    screen_to_canvas(state, event->button.x, event->button.y, &state->last_mouse_x, &state->last_mouse_y);
                    int lx = state->last_mouse_x, ly=state->last_mouse_y;
                    if (is_inside_rect(lx,ly,0,0,state->canvas_width,state->canvas_height)) {
                        size_t index = ly * state->canvas_width + lx;
                        if (state->color_timestamps && state->color_timestamps[index] == 0) state->color_timestamps[index] = event->motion.timestamp-state->canvas_starttime + 1;
                        if (state->color_pixels) state->color_pixels[index] = 0xFF000000;
                    }
                    break;
                }
                case FINISH: {
                    state->mouse_down = event->button.button == SDL_BUTTON_LEFT;
                }
            }
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            state->mouse_down = false;
            break;
        }
        case SDL_EVENT_MOUSE_MOTION: {
            if (!state->mouse_down) break;

            const bool *key_state = SDL_GetKeyboardState(NULL);
            if (key_state[SDL_SCANCODE_SPACE]) {
                state->canvas_x += event->motion.xrel;
                state->canvas_y += event->motion.yrel;

                break;
            }

            double mx = 0, my = 0;
            screen_to_canvas(state, event->motion.x,event->motion.y, &mx, &my);

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
                    if (state->color_timestamps && state->color_timestamps[index] == 0) state->color_timestamps[index] = event->motion.timestamp-state->canvas_starttime + 1;
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
        case SDL_EVENT_MOUSE_WHEEL: {
            float mx, my;
            SDL_GetMouseState(&mx, &my);

            double cx, cy;
            screen_to_canvas(state, mx, my, &cx, &cy);

            state->canvas_zoom += event->wheel.y * 0.1;

            double scale = SDL_pow(2, state->canvas_zoom);
            double rad = state->canvas_rotation * (SDL_PI_F / 180.0);
            double cos_theta = SDL_cos(rad);
            double sin_theta = SDL_sin(rad);

            double sx = cx - (state->canvas_width / 2.0);
            double sy = cy - (state->canvas_height / 2.0);

            double rx = sx * scale;
            double ry = sy * scale;

            double dx = (rx * cos_theta) - (ry * sin_theta);
            double dy = (rx * sin_theta) + (ry * cos_theta);

            state->canvas_x = mx - dx - (state->window_width / 2.0);
            state->canvas_y = my - dy - (state->window_height / 2.0);

            break;
        }
    }
    return SDL_APP_CONTINUE;
}

#define deg2rad(deg) ((deg * SDL_PI_D)/180.0);

SDL_AppResult SDL_AppIterate(void *appstate) {
    AppState *state = (AppState *)appstate;

    SDL_GetWindowSize(state->window, &state->window_width, &state->window_height);

    SDL_SetRenderDrawColor(state->renderer, 0, 0, 0, 255);
    SDL_RenderClear(state->renderer);

    switch (state->window_state) {
        case SELECTING: {
            SDL_SetRenderDrawColor(state->renderer, 255, 255, 255, 255);
    
            int intro_text_width = 0;
            int intro_text_height = 0;
            TTF_GetTextSize(state->intro_text,&intro_text_width,&intro_text_height);
            TTF_DrawRendererText(state->intro_text,state->window_width/2 - intro_text_width/2,state->window_height/2 - intro_text_height/2);

            state->ready_button_rect.x = state->window_width/2 - state->ready_button_rect.w/2;
            state->ready_button_rect.y = state->window_height/2 - state->ready_button_rect.h/2 + 50;

            SDL_RenderRect(state->renderer, &state->ready_button_rect);

            TTF_DrawRendererText(state->ready_button_text,state->window_width/2 - (state->ready_button_rect.w-20)/2,state->window_height/2 - (state->ready_button_rect.h-10)/2 + 50);

            break;
        }
        case MAIN: {
            SDL_UpdateTexture(state->color_texture,NULL,state->color_pixels,state->canvas_width * sizeof(uint32_t));

            double zoom_factor = SDL_pow(2, state->canvas_zoom);

            SDL_FRect canvas_rect = {
                .w = state->canvas_width  * zoom_factor, 
                .h = state->canvas_height * zoom_factor, 
            };
            canvas_rect.x = state->canvas_x + state->window_width/2  - canvas_rect.w/2;
            canvas_rect.y = state->canvas_y + state->window_height/2 - canvas_rect.h/2;

            SDL_RenderTextureRotated(state->renderer, state->color_texture, NULL, &canvas_rect, state->canvas_rotation,NULL,SDL_FLIP_NONE);

            const float canvas_rotation_rad = deg2rad(state->canvas_rotation);

            const float cosA = SDL_cosf(canvas_rotation_rad);
            const float sinA = SDL_sinf(canvas_rotation_rad);

            const float hx = canvas_rect.w / 2.0f;
            const float hy = canvas_rect.h / 2.0f;

            const float cx = canvas_rect.x + hx;
            const float cy = canvas_rect.y + hy;

            const int o = 2;
            const float cornersX[4] = { -hx - o,  hx + o, hx + o, -hx - o };
            const float cornersY[4] = { -hy - o, -hy - o, hy + o,  hy + o };

            SDL_FPoint border[5];
            for (int i = 0; i < 4; i++) {

                border[i].x = (int)(cx + (cornersX[i] * cosA - cornersY[i] * sinA));
                border[i].y = (int)(cy + (cornersX[i] * sinA + cornersY[i] * cosA));
            }
            border[4] = border[0];

            SDL_SetRenderDrawColor(state->renderer, 255, 255, 255, 255);
            SDL_RenderLines(state->renderer,border,5);

            state->finish_button_rect.x = state->window_width - state->ready_button_rect.w - 20;
            state->finish_button_rect.y = state->window_height - state->ready_button_rect.h;

            SDL_SetRenderDrawColor(state->renderer, 0, 0, 0, 255);

            SDL_RenderFillRect(state->renderer, &state->finish_button_rect);
            SDL_SetRenderDrawColor(state->renderer, 255, 255, 255, 255);
            SDL_RenderRect(state->renderer, &state->finish_button_rect);
            TTF_DrawRendererText(state->finish_button_text,state->window_width - state->finish_button_rect.w,state->window_height - (state->finish_button_rect.h-5));


            state->rotate_button_left = (SDL_FRect){
                .h = 15, .w = 15,
                .x = 7, .y = 7
            };
            SDL_SetRenderDrawColor(state->renderer, 0, 0, 0, 255);
            SDL_RenderFillRect(state->renderer,&state->rotate_button_left);
            SDL_SetRenderDrawColor(state->renderer, 255, 255, 255, 255);
            SDL_RenderRect(state->renderer,&state->rotate_button_left);

            state->rotate_button_right = (SDL_FRect){
                .h = 15, .w = 15,
                .x = 128, .y = 7
            };
            SDL_SetRenderDrawColor(state->renderer, 0, 0, 0, 255);
            SDL_RenderFillRect(state->renderer,&state->rotate_button_right);
            SDL_SetRenderDrawColor(state->renderer, 255, 255, 255, 255);
            SDL_RenderRect(state->renderer,&state->rotate_button_right);

            state->rotate_button_reset = (SDL_FRect){
                .h = 21, .w = 100,
                .x = 24, .y = 5
            };
            SDL_SetRenderDrawColor(state->renderer, 0, 0, 0, 255);
            SDL_RenderFillRect(state->renderer,&state->rotate_button_reset);
            SDL_SetRenderDrawColor(state->renderer, 255, 255, 255, 255);
            SDL_RenderRect(state->renderer,&state->rotate_button_reset);

            SDL_SetRenderDrawColor(state->renderer, 255, 255, 255, 255);
            TTF_DrawRendererText(state->rotate_buttons_text, 10, 5);
            break;
        }
        case FINISH: {
            SDL_SetRenderDrawColor(state->renderer, 255, 255, 255, 255);

            SDL_UpdateTexture(state->color_texture,NULL,state->color_timestamps_colors,state->canvas_width * sizeof(uint32_t));

            double zoom_factor = SDL_pow(2, state->canvas_zoom);

            SDL_FRect canvas_rect = {
                .w = state->canvas_width  * zoom_factor, 
                .h = state->canvas_height * zoom_factor, 
            };
            canvas_rect.x = state->canvas_x + state->window_width/2  - canvas_rect.w/2;
            canvas_rect.y = state->canvas_y + state->window_height/2 - canvas_rect.h/2;

            SDL_RenderTextureRotated(state->renderer, state->color_texture, NULL, &canvas_rect, state->canvas_rotation,NULL,SDL_FLIP_NONE);

            canvas_rect.x -= 2;
            canvas_rect.y -= 2;
            canvas_rect.w += 4;
            canvas_rect.h += 4;

            SDL_SetRenderDrawColor(state->renderer, 255, 255, 255, 255);

            SDL_RenderRect(state->renderer,&canvas_rect);

            int final_text_width = 0;
            int final_text_height = 0;
            TTF_GetTextSize(state->final_screen_text,&final_text_width,&final_text_height);
            TTF_DrawRendererText(state->final_screen_text,state->window_width/2 - final_text_width/2,state->window_height - final_text_height - 5);

            break;
        }
    }

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
