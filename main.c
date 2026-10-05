#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <SDL3_ttf/SDL_ttf.h>

static const int canvas_size_presets[] = {8, 16, 32, 50, 64, 100, 128, 200, 256, 512};
#define CANVAS_SIZE_PRESET_COUNT (int)SDL_arraysize(canvas_size_presets)
// 4096 is the largest texture size WebGL reliably supports
#define CANVAS_SIZE_MAX 4096

typedef enum SizeField {
    SIZE_FIELD_NONE,
    SIZE_FIELD_WIDTH,
    SIZE_FIELD_HEIGHT
} SizeField;

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

    int chosen_width;
    int chosen_height;
    SizeField editing_field;
    char edit_buffer[8];
    SDL_FRect width_field_rect;
    SDL_FRect height_field_rect;
    TTF_Text* width_text;
    TTF_Text* height_text;
    TTF_Text* left_arrow_text;
    TTF_Text* right_arrow_text;
    SDL_FRect width_left_button;
    SDL_FRect width_right_button;
    SDL_FRect height_left_button;
    SDL_FRect height_right_button;
} AppState;

void update_size_text(AppState* state) {
    char buf[32];
    if (state->editing_field == SIZE_FIELD_WIDTH) {
        SDL_snprintf(buf, sizeof(buf), "width: %s_", state->edit_buffer);
    } else {
        SDL_snprintf(buf, sizeof(buf), "width: %d", state->chosen_width);
    }
    TTF_SetTextString(state->width_text, buf, 0);

    if (state->editing_field == SIZE_FIELD_HEIGHT) {
        SDL_snprintf(buf, sizeof(buf), "height: %s_", state->edit_buffer);
    } else {
        SDL_snprintf(buf, sizeof(buf), "height: %d", state->chosen_height);
    }
    TTF_SetTextString(state->height_text, buf, 0);
}

void start_size_edit(AppState* state, SizeField field) {
    state->editing_field = field;
    state->edit_buffer[0] = '\0';
    SDL_StartTextInput(state->window);
    update_size_text(state);
}

void finish_size_edit(AppState* state) {
    if (state->editing_field == SIZE_FIELD_NONE) return;

    if (state->edit_buffer[0] != '\0') {
        int value = SDL_clamp(SDL_atoi(state->edit_buffer), 1, CANVAS_SIZE_MAX);
        if (state->editing_field == SIZE_FIELD_WIDTH) state->chosen_width = value;
        else state->chosen_height = value;
    }

    state->editing_field = SIZE_FIELD_NONE;
    SDL_StopTextInput(state->window);
    update_size_text(state);
}

int next_preset_up(int value) {
    for (int i = 0; i < CANVAS_SIZE_PRESET_COUNT; ++i) {
        if (canvas_size_presets[i] > value) return canvas_size_presets[i];
    }
    return value;
}

int next_preset_down(int value) {
    for (int i = CANVAS_SIZE_PRESET_COUNT-1; i >= 0; --i) {
        if (canvas_size_presets[i] < value) return canvas_size_presets[i];
    }
    return value;
}

void initialize_canvas(AppState* state){
    SDL_free(state->color_timestamps);
    state->color_timestamps = NULL;
    SDL_free(state->color_pixels);
    state->color_pixels = NULL;
    SDL_DestroyTexture(state->color_texture);
    state->color_texture = NULL;

    state->color_timestamps = SDL_calloc(state->canvas_width*state->canvas_height, sizeof(uint64_t));
    state->color_pixels = SDL_malloc(state->canvas_width*state->canvas_height * sizeof(uint32_t));
    SDL_memset4(state->color_pixels,0xFFFFFFFF,state->canvas_width*state->canvas_height);
    state->color_texture = SDL_CreateTexture(
        state->renderer, 
        SDL_PIXELFORMAT_RGBA8888, 
        SDL_TEXTUREACCESS_STREAMING, 
        state->canvas_width, 
        state->canvas_height
    );
    SDL_SetTextureScaleMode(state->color_texture,SDL_SCALEMODE_PIXELART);

    const double fit_w = state->window_width  * 0.8 / state->canvas_width;
    const double fit_h = state->window_height * 0.8 / state->canvas_height;
    state->canvas_zoom = SDL_log(SDL_min(fit_w, fit_h)) / SDL_log(2.0);
    state->canvas_x = 0;
    state->canvas_y = 0;
    state->canvas_rotation = 0;

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

    SDL_WindowFlags window_flags = SDL_WINDOW_RESIZABLE;
#ifdef SDL_PLATFORM_EMSCRIPTEN
    // Without OPENGL the GLES2 renderer recreates the window, which breaks
    // SDL's fill-document cleanup and throws in the browser
    window_flags |= SDL_WINDOW_FILL_DOCUMENT | SDL_WINDOW_OPENGL;
#endif

    if (!SDL_CreateWindowAndRenderer("fill a canvas with a 1px brush", state->window_width, state->window_height, window_flags, &state->window, &state->renderer)) {
        SDL_Log("Failed to create window and renderer: %s",SDL_GetError());
        return SDL_APP_FAILURE;
    }
    SDL_SetRenderVSync(state->renderer,1);

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

    state->width_text = TTF_CreateText(state->text_engine,state->font,"",0);
    state->height_text = TTF_CreateText(state->text_engine,state->font,"",0);
    state->left_arrow_text = TTF_CreateText(state->text_engine,state->font,"<",0);
    state->right_arrow_text = TTF_CreateText(state->text_engine,state->font,">",0);
    if (!state->width_text || !state->height_text || !state->left_arrow_text || !state->right_arrow_text) {
        SDL_Log("Failed to create text object: %s",SDL_GetError());
        return SDL_APP_FAILURE;
    }

    state->chosen_width = 100;
    state->chosen_height = 100;
    update_size_text(state);

    state->canvas_width  = state->chosen_width;
    state->canvas_height = state->chosen_height;
    initialize_canvas(state);

    return SDL_APP_CONTINUE;
}

typedef struct Vector3 {
    double x,y,z;
} Vector3;

Vector3 cosine_palette(double t, Vector3 a, Vector3 b, Vector3 c, Vector3 d) {
    return (Vector3){
        .x = a.x + b.x * SDL_cos(SDL_PI_D*2 * (c.x * t + d.x)),
        .y = a.y + b.y * SDL_cos(SDL_PI_D*2 * (c.y * t + d.y)),
        .z = a.z + b.z * SDL_cos(SDL_PI_D*2 * (c.z * t + d.z))
    };
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
                    const float bx = event->button.x, by = event->button.y;

                    finish_size_edit(state);

                    if (is_inside_rect_rect(bx,by,state->ready_button_rect)) {
                        state->canvas_width  = state->chosen_width;
                        state->canvas_height = state->chosen_height;
                        state->window_state = MAIN;
                        initialize_canvas(state);
                    }
                    else if (is_inside_rect_rect(bx,by,state->width_left_button))   state->chosen_width  = next_preset_down(state->chosen_width);
                    else if (is_inside_rect_rect(bx,by,state->width_right_button))  state->chosen_width  = next_preset_up(state->chosen_width);
                    else if (is_inside_rect_rect(bx,by,state->height_left_button))  state->chosen_height = next_preset_down(state->chosen_height);
                    else if (is_inside_rect_rect(bx,by,state->height_right_button)) state->chosen_height = next_preset_up(state->chosen_height);
                    else if (is_inside_rect_rect(bx,by,state->width_field_rect))    start_size_edit(state, SIZE_FIELD_WIDTH);
                    else if (is_inside_rect_rect(bx,by,state->height_field_rect))   start_size_edit(state, SIZE_FIELD_HEIGHT);

                    update_size_text(state);
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

                            Vector3 a = {0.5, 0.5, 0.5};
                            Vector3 b = {0.5, 0.5, 0.5};
                            Vector3 c = {0.5, 0.5, 0.3};
                            Vector3 d = {0.0, 0.33, 0.66};

                            const double range = (max_color > min_color) ? (double)(max_color - min_color) : 1.0;

                            for (size_t i = 0; i < (state->canvas_width*state->canvas_height); ++i) {
                                const double val = (double)(state->color_timestamps[i] - min_color)/range;
                                Vector3 color = cosine_palette(SDL_clamp(val,0,1),a,b,c,d);
                                uint32_t r_v = color.x * 255.0;
                                uint32_t g_v = color.y * 255.0;
                                uint32_t b_v = color.z * 255.0;
                                state->color_timestamps_colors[i] = (r_v << 24)|(g_v << 16)|(b_v << 8)|(255);
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
                    if (state->mouse_down) {
                        int lx = (int)SDL_floor(state->last_mouse_x), ly = (int)SDL_floor(state->last_mouse_y);
                        if (is_inside_rect(lx,ly,0,0,state->canvas_width,state->canvas_height)) {
                            size_t index = ly * state->canvas_width + lx;
                            if (state->color_timestamps && state->color_timestamps[index] == 0) state->color_timestamps[index] = event->button.timestamp-state->canvas_starttime + 1;
                            if (state->color_pixels) state->color_pixels[index] = 0xFF000000;
                        }
                    }
                    break;
                }
                case FINISH: {
                    state->mouse_down = event->button.button == SDL_BUTTON_LEFT;
                }
            }
            break;
        }
        case SDL_EVENT_TEXT_INPUT: {
            if (state->editing_field == SIZE_FIELD_NONE) break;
            size_t len = SDL_strlen(state->edit_buffer);
            for (const char *c = event->text.text; *c; ++c) {
                if (*c >= '0' && *c <= '9' && len < 4) {
                    state->edit_buffer[len++] = *c;
                    state->edit_buffer[len] = '\0';
                }
            }
            update_size_text(state);
            break;
        }
        case SDL_EVENT_KEY_DOWN: {
            if (state->editing_field == SIZE_FIELD_NONE) break;
            size_t len = SDL_strlen(state->edit_buffer);
            switch (event->key.key) {
                case SDLK_BACKSPACE:
                    if (len > 0) state->edit_buffer[len-1] = '\0';
                    update_size_text(state);
                    break;
                case SDLK_RETURN:
                case SDLK_KP_ENTER:
                    finish_size_edit(state);
                    break;
                case SDLK_TAB: {
                    SizeField next = state->editing_field == SIZE_FIELD_WIDTH ? SIZE_FIELD_HEIGHT : SIZE_FIELD_WIDTH;
                    finish_size_edit(state);
                    start_size_edit(state, next);
                    break;
                }
                case SDLK_ESCAPE:
                    // finish_size_edit keeps the old value when the buffer is empty
                    state->edit_buffer[0] = '\0';
                    finish_size_edit(state);
                    break;
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

            int x0 = (int)SDL_floor(mx);
            int y0 = (int)SDL_floor(my);
            int x1 = (int)SDL_floor(state->last_mouse_x);
            int y1 = (int)SDL_floor(state->last_mouse_y);

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

#define deg2rad(deg) (((deg) * SDL_PI_D)/180.0)

void draw_canvas(AppState *state, const uint32_t *pixels) {
    SDL_UpdateTexture(state->color_texture,NULL,pixels,state->canvas_width * sizeof(uint32_t));

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
}

void draw_button(AppState *state, SDL_FRect rect, TTF_Text *text) {
    SDL_SetRenderDrawColor(state->renderer, 0, 0, 0, 255);
    SDL_RenderFillRect(state->renderer, &rect);
    SDL_SetRenderDrawColor(state->renderer, 255, 255, 255, 255);
    SDL_RenderRect(state->renderer, &rect);

    int tw = 0, th = 0;
    TTF_GetTextSize(text, &tw, &th);
    TTF_DrawRendererText(text, (int)(rect.x + rect.w/2 - tw/2.0f), (int)(rect.y + rect.h/2 - th/2.0f));
}

void draw_size_row(AppState *state, float y, TTF_Text *label, bool editing, SDL_FRect *field, SDL_FRect *left, SDL_FRect *right) {
    const float button_size = 21;
    const float half_gap = 70;
    const float cx = state->window_width / 2.0f;

    *left  = (SDL_FRect){ .x = cx - half_gap - button_size, .y = y, .w = button_size, .h = button_size };
    *right = (SDL_FRect){ .x = cx + half_gap,               .y = y, .w = button_size, .h = button_size };

    *field = (SDL_FRect){ .x = left->x + left->w + 4, .y = y, .w = right->x - (left->x + left->w) - 8, .h = button_size };
    if (editing) {
        SDL_SetRenderDrawColor(state->renderer, 255, 255, 255, 255);
        SDL_RenderRect(state->renderer, field);
    }

    draw_button(state, *left, state->left_arrow_text);
    draw_button(state, *right, state->right_arrow_text);

    int tw = 0, th = 0;
    TTF_GetTextSize(label, &tw, &th);
    TTF_DrawRendererText(label, (int)(cx - tw/2.0f), (int)(y + button_size/2 - th/2.0f));
}

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

            const float size_rows_y = state->ready_button_rect.y + state->ready_button_rect.h + 20;
            draw_size_row(state, size_rows_y,      state->width_text,  state->editing_field == SIZE_FIELD_WIDTH,  &state->width_field_rect,  &state->width_left_button,  &state->width_right_button);
            draw_size_row(state, size_rows_y + 30, state->height_text, state->editing_field == SIZE_FIELD_HEIGHT, &state->height_field_rect, &state->height_left_button, &state->height_right_button);

            break;
        }
        case MAIN: {
            draw_canvas(state, state->color_pixels);

            state->finish_button_rect.x = state->window_width - state->finish_button_rect.w - 10;
            state->finish_button_rect.y = state->window_height - state->finish_button_rect.h - 10;

            SDL_SetRenderDrawColor(state->renderer, 0, 0, 0, 255);

            SDL_RenderFillRect(state->renderer, &state->finish_button_rect);
            SDL_SetRenderDrawColor(state->renderer, 255, 255, 255, 255);
            SDL_RenderRect(state->renderer, &state->finish_button_rect);
            TTF_DrawRendererText(state->finish_button_text,state->finish_button_rect.x + 10,state->finish_button_rect.y + 5);


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
            draw_canvas(state, state->color_timestamps_colors);

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
        TTF_DestroyText(state->intro_text);
        TTF_DestroyText(state->ready_button_text);
        TTF_DestroyText(state->finish_button_text);
        TTF_DestroyText(state->final_screen_text);
        TTF_DestroyText(state->rotate_buttons_text);
        TTF_DestroyText(state->width_text);
        TTF_DestroyText(state->height_text);
        TTF_DestroyText(state->left_arrow_text);
        TTF_DestroyText(state->right_arrow_text);
        TTF_DestroyRendererTextEngine(state->text_engine);
        TTF_CloseFont(state->font);
        SDL_DestroyTexture(state->color_texture);
        SDL_DestroyRenderer(state->renderer);
        SDL_DestroyWindow(state->window);
        SDL_free(state->color_timestamps);
        SDL_free(state->color_timestamps_colors);
        SDL_free(state->color_pixels);
        SDL_free(state);
    }
    TTF_Quit();
    SDL_Quit();
}
