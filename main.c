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

    bool mouse_down;
    float last_mouse_x;
    float last_mouse_y;
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
}

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
            state->last_mouse_x = event->button.x;
            state->last_mouse_y = event->button.y;
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            state->mouse_down = false;
            break;
        }
        case SDL_EVENT_MOUSE_MOTION: {
            if (!state->mouse_down) break;

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

    SDL_FRect canvas_rect = {.w = state->canvas_width, .h = state->canvas_height, .x = state->window_width/2 - state->canvas_width/2, .y = state->window_height/2 - state->canvas_height/2};
    SDL_RenderTexture(state->renderer, state->color_texture, NULL, &canvas_rect);

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
