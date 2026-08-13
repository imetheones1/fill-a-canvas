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

    WindowState window_state;

    TTF_TextEngine* text_engine;
    TTF_Font* font;

    TTF_Text* intro_text;
} AppState;

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

    if (!SDL_CreateWindowAndRenderer("fill a canvas with a 1px brush", 400, 300, SDL_WINDOW_RESIZABLE, &state->window, &state->renderer)) {
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

    state->intro_text = TTF_CreateText(state->text_engine, state->font, "Hello World", 0);
    if (!state->intro_text) {
        SDL_Log("Failed to create text object: %s",SDL_GetError());
        return SDL_APP_FAILURE;
    }

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
    if (event->type == SDL_EVENT_QUIT) {
        return SDL_APP_SUCCESS;
    }
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate) {
    AppState *state = (AppState *)appstate;

    int screen_width, screen_height;
    SDL_GetWindowSize(state->window, &screen_width, &screen_height);

    SDL_SetRenderDrawColor(state->renderer, 0, 0, 0, 255);
    SDL_RenderClear(state->renderer);

    SDL_SetRenderDrawColor(state->renderer, 255, 255, 255, 255);
    
    TTF_DrawRendererText(state->intro_text,screen_width/2,screen_height/2);

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
