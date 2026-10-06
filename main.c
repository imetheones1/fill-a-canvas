#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <SDL3_ttf/SDL_ttf.h>

#ifdef SDL_PLATFORM_EMSCRIPTEN
#include <emscripten.h>
#endif

// Generated from Roboto-Regular.ttf by CMake, so the font is built into the program
extern const unsigned char roboto_regular_ttf[];
extern const size_t roboto_regular_ttf_size;

#define GITHUB_URL "https://github.com/imetheones1/fill-a-canvas"
#define WEB_URL "https://fill-a-canvas.imetheones1.com"
#define LICENSE_URL WEB_URL "/OFL.txt"
// Relative to the page, since the .exe is hosted alongside the web build
#define EXE_DOWNLOAD_PATH "fill-a-canvas.exe"
#define INFO_TEXT "so you fill in a canvas with a one pixel wide brush. how big is the canvas? you choose. how big is the brush? one pixel. you do that and you have fun and such. built in c with SDL3"

static const int canvas_size_presets[] = {8, 16, 32, 50, 64, 100, 128, 200, 256, 512};
#define CANVAS_SIZE_PRESET_COUNT (int)SDL_arraysize(canvas_size_presets)
// 4096 is the largest texture size WebGL reliably supports
#define CANVAS_SIZE_MAX 4096

#define BLANK_COLOR   0xFFFFFFFF
#define PAINTED_COLOR 0x000000FF

#define deg2rad(deg) (((deg) * SDL_PI_D)/180.0)
#define log2d(x) (SDL_log(x) / SDL_log(2.0))

typedef enum Screen {
    SCREEN_INTRO,
    SCREEN_INFO,
    SCREEN_DRAWING,
    SCREEN_FINISHED
} Screen;

typedef enum SizeField {
    SIZE_FIELD_NONE,
    SIZE_FIELD_WIDTH,
    SIZE_FIELD_HEIGHT
} SizeField;

typedef struct Button {
    TTF_Text *text;
    SDL_FRect rect;
} Button;

typedef struct Vector3 {
    double x,y,z;
} Vector3;

typedef struct AppState {
    SDL_Window *window;
    SDL_Renderer *renderer;
    int window_width;
    int window_height;

    Screen screen;

    TTF_TextEngine *text_engine;
    TTF_Font *font;
    TTF_Text *texts[32];
    int text_count;

    size_t canvas_width;
    size_t canvas_height;
    uint64_t *paint_times;
    uint32_t *drawing_pixels;
    uint32_t *timestamp_pixels;
    SDL_Texture *canvas_texture;
    uint64_t canvas_start_time;

    double canvas_zoom;
    double canvas_x, canvas_y;
    double canvas_rotation;

    bool mouse_down;
    bool panning;
    double last_mouse_x;
    double last_mouse_y;

    SDL_FingerID gesture_fingers[2];
    SDL_FPoint gesture_points[2];
    int gesture_finger_count;
    bool has_native_pinch;

    SDL_Cursor *move_cursor;
    bool showing_move_cursor;

    TTF_Text *intro_text;
    Button ready_button;
    int chosen_width;
    int chosen_height;
    SizeField editing_field;
    char edit_buffer[8];
    TTF_Text *width_text;
    TTF_Text *height_text;
    SDL_FRect width_field;
    SDL_FRect height_field;
    Button width_down_button;
    Button width_up_button;
    Button height_down_button;
    Button height_up_button;
    Button info_button;

    TTF_Text *info_text;
    float info_text_y;
    Button github_button;
    Button other_version_button;
    // Desktop only; the web build already serves OFL.txt next to the page
    Button license_button;
    Button back_button;

    Button finish_button;
    Button rotate_left_button;
    Button rotate_reset_button;
    Button rotate_right_button;
    Button reset_view_button;
    TTF_Text *zoom_text;
    int zoom_text_percent;

    TTF_Text *final_screen_text;
    Button save_drawing_button;
    Button save_timestamps_button;
} AppState;

bool showing_canvas(AppState *state) {
    return state->screen == SCREEN_DRAWING || state->screen == SCREEN_FINISHED;
}

void screen_to_canvas(AppState *state, double screen_x, double screen_y, double *out_canvas_x, double *out_canvas_y) {
    double dx = screen_x - (state->window_width / 2.0 + state->canvas_x);
    double dy = screen_y - (state->window_height / 2.0 + state->canvas_y);

    double rad = deg2rad(state->canvas_rotation);
    double cos_theta = SDL_cos(rad);
    double sin_theta = SDL_sin(rad);

    double rx = (dx * cos_theta) + (dy * sin_theta);
    double ry = (-dx * sin_theta) + (dy * cos_theta);

    double scale = SDL_pow(2, state->canvas_zoom);
    *out_canvas_x = rx / scale + state->canvas_width / 2.0;
    *out_canvas_y = ry / scale + state->canvas_height / 2.0;
}

void canvas_to_screen(AppState *state, double canvas_x, double canvas_y, double *out_screen_x, double *out_screen_y) {
    double scale = SDL_pow(2, state->canvas_zoom);
    double sx = (canvas_x - state->canvas_width / 2.0) * scale;
    double sy = (canvas_y - state->canvas_height / 2.0) * scale;

    double rad = deg2rad(state->canvas_rotation);
    double cos_theta = SDL_cos(rad);
    double sin_theta = SDL_sin(rad);

    *out_screen_x = state->window_width / 2.0 + state->canvas_x + (sx * cos_theta) - (sy * sin_theta);
    *out_screen_y = state->window_height / 2.0 + state->canvas_y + (sx * sin_theta) + (sy * cos_theta);
}

void reset_view(AppState *state) {
    const double fit_w = state->window_width  * 0.8 / state->canvas_width;
    const double fit_h = state->window_height * 0.8 / state->canvas_height;
    state->canvas_zoom = log2d(SDL_min(fit_w, fit_h));
    state->canvas_x = 0;
    state->canvas_y = 0;
}

// Changes zoom (in powers of two) while keeping the canvas point under (screen_x, screen_y) in place
void zoom_at(AppState *state, double screen_x, double screen_y, double zoom_change) {
    double cx, cy;
    screen_to_canvas(state, screen_x, screen_y, &cx, &cy);

    // Stop at 16 screen pixels for the whole canvas, or 128 screen pixels per canvas pixel,
    // so the canvas can't be zoomed out of sight or into a single pixel filling the screen
    const double min_zoom = log2d(16.0 / SDL_max(state->canvas_width, state->canvas_height));
    state->canvas_zoom = SDL_clamp(state->canvas_zoom + zoom_change, SDL_min(min_zoom, state->canvas_zoom), SDL_max(7.0, state->canvas_zoom));

    double new_x, new_y;
    canvas_to_screen(state, cx, cy, &new_x, &new_y);
    state->canvas_x += screen_x - new_x;
    state->canvas_y += screen_y - new_y;
}

#ifdef SDL_PLATFORM_EMSCRIPTEN
// Browsers report trackpad pinches as ctrl+wheel events with tiny deltas. The ctrl isn't a real
// key press, so SDL can't tell them apart from scrolling; catch them first and collect the zoom here.
EM_JS(void, install_pinch_listener, (), {
    Module.pinchZoom = 0;
    window.addEventListener('wheel', (e) => {
        if (!e.ctrlKey) return;
        e.preventDefault();
        e.stopPropagation();
        let dy = e.deltaY;
        if (e.deltaMode === 1) dy *= 33;
        else if (e.deltaMode === 2) dy *= 800;
        // Pinch steps are a few pixels; a ctrl+mouse-wheel notch is ~100 and would jump too far
        dy = Math.max(-30, Math.min(30, dy));
        Module.pinchZoom -= dy / 100 / Math.LN2;
    }, { capture: true, passive: false });
});

EM_JS(double, take_pinch_zoom, (), {
    const zoom = Module.pinchZoom;
    Module.pinchZoom = 0;
    return zoom;
});
#endif

bool initialize_canvas(AppState *state) {
    SDL_free(state->paint_times);
    SDL_free(state->drawing_pixels);
    SDL_free(state->timestamp_pixels);
    SDL_DestroyTexture(state->canvas_texture);
    state->timestamp_pixels = NULL;

    const size_t count = state->canvas_width * state->canvas_height;
    state->paint_times = SDL_calloc(count, sizeof(uint64_t));
    state->drawing_pixels = SDL_malloc(count * sizeof(uint32_t));
    state->canvas_texture = SDL_CreateTexture(state->renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, (int)state->canvas_width, (int)state->canvas_height);
    if (!state->paint_times || !state->drawing_pixels || !state->canvas_texture) {
        return false;
    }

    SDL_memset4(state->drawing_pixels, BLANK_COLOR, count);
    SDL_SetTextureScaleMode(state->canvas_texture, SDL_SCALEMODE_PIXELART);

    reset_view(state);
    state->canvas_rotation = 0;
    state->canvas_start_time = SDL_GetTicksNS();
    return true;
}

void paint_pixel(AppState *state, int x, int y, uint64_t timestamp) {
    if (x < 0 || y < 0 || x >= (int)state->canvas_width || y >= (int)state->canvas_height) return;

    const size_t index = (size_t)y * state->canvas_width + x;
    // +1 so a pixel painted the instant the canvas started isn't mistaken for unpainted (0)
    if (state->paint_times[index] == 0) state->paint_times[index] = timestamp - state->canvas_start_time + 1;
    state->drawing_pixels[index] = PAINTED_COLOR;
}

void paint_line(AppState *state, int x0, int y0, int x1, int y1, uint64_t timestamp) {
    int dx = SDL_abs(x1 - x0);
    int dy = -SDL_abs(y1 - y0);

    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;

    int err = dx + dy;

    while (1) {
        paint_pixel(state, x0, y0, timestamp);

        if (x0 == x1 && y0 == y1) break;

        int e2 = 2 * err;

        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }

        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

Vector3 cosine_palette(double t, Vector3 a, Vector3 b, Vector3 c, Vector3 d) {
    return (Vector3){
        .x = a.x + b.x * SDL_cos(SDL_PI_D*2 * (c.x * t + d.x)),
        .y = a.y + b.y * SDL_cos(SDL_PI_D*2 * (c.y * t + d.y)),
        .z = a.z + b.z * SDL_cos(SDL_PI_D*2 * (c.z * t + d.z))
    };
}

void finish_canvas(AppState *state) {
    const size_t count = state->canvas_width * state->canvas_height;

    uint64_t min_time = SDL_MAX_UINT64;
    uint64_t max_time = 0;
    for (size_t i = 0; i < count; ++i) {
        if (state->paint_times[i] == 0) {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION,"you didnt finish","you did not finish please",state->window);
            return;
        }
        min_time = SDL_min(min_time, state->paint_times[i]);
        max_time = SDL_max(max_time, state->paint_times[i]);
    }

    SDL_free(state->timestamp_pixels);
    state->timestamp_pixels = SDL_malloc(count * sizeof(uint32_t));
    if (!state->timestamp_pixels) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "out of memory", SDL_GetError(), state->window);
        return;
    }

    Vector3 a = {0.5, 0.5, 0.5};
    Vector3 b = {0.5, 0.5, 0.5};
    Vector3 c = {0.5, 0.5, 0.3};
    Vector3 d = {0.0, 0.33, 0.66};

    const double range = (max_time > min_time) ? (double)(max_time - min_time) : 1.0;

    for (size_t i = 0; i < count; ++i) {
        const double t = (double)(state->paint_times[i] - min_time) / range;
        Vector3 color = cosine_palette(t, a, b, c, d);
        uint32_t red = color.x * 255.0;
        uint32_t green = color.y * 255.0;
        uint32_t blue = color.z * 255.0;
        state->timestamp_pixels[i] = (red << 24) | (green << 16) | (blue << 8) | 255;
    }

    state->mouse_down = false;
    state->screen = SCREEN_FINISHED;
}

void update_size_text(AppState *state) {
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

void start_size_edit(AppState *state, SizeField field) {
    state->editing_field = field;
    state->edit_buffer[0] = '\0';
    SDL_StartTextInput(state->window);
    update_size_text(state);
}

void finish_size_edit(AppState *state) {
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

SDL_Surface *surface_from_pixels(size_t width, size_t height, const uint32_t *pixels) {
    return SDL_CreateSurfaceFrom((int)width, (int)height, SDL_PIXELFORMAT_RGBA8888, (void *)pixels, (int)(width * sizeof(uint32_t)));
}

#ifndef SDL_PLATFORM_EMSCRIPTEN
typedef struct SaveRequest {
    size_t width, height;
    const uint32_t *pixels;
} SaveRequest;

void SDLCALL save_dialog_callback(void *userdata, const char * const *filelist, int filter) {
    SaveRequest *request = userdata;

    if (!filelist) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "couldn't save", SDL_GetError(), NULL);
    } else if (filelist[0]) {
        char *path = NULL;
        const size_t len = SDL_strlen(filelist[0]);
        if (len >= 4 && SDL_strcasecmp(filelist[0] + len - 4, ".png") == 0) {
            path = SDL_strdup(filelist[0]);
        } else {
            SDL_asprintf(&path, "%s.png", filelist[0]);
        }

        SDL_Surface *surface = surface_from_pixels(request->width, request->height, request->pixels);
        if (!surface || !SDL_SavePNG(surface, path)) {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "couldn't save", SDL_GetError(), NULL);
        }
        SDL_DestroySurface(surface);
        SDL_free(path);
    }

    SDL_free(request);
}
#endif

void save_image(AppState *state, const uint32_t *pixels, const char *filename) {
#ifdef SDL_PLATFORM_EMSCRIPTEN
    // Browsers have no save dialog, so encode the PNG in memory and hand it over as a download
    SDL_Surface *surface = surface_from_pixels(state->canvas_width, state->canvas_height, pixels);
    SDL_IOStream *io = SDL_IOFromDynamicMem();
    if (surface && io && SDL_SavePNG_IO(surface, io, false)) {
        const void *data = SDL_GetPointerProperty(SDL_GetIOProperties(io), SDL_PROP_IOSTREAM_DYNAMIC_MEMORY_POINTER, NULL);
        const int size = (int)SDL_TellIO(io);
        EM_ASM({
            const blob = new Blob([HEAPU8.slice($0, $0 + $1)], { type: 'image/png' });
            const link = document.createElement('a');
            link.href = URL.createObjectURL(blob);
            link.download = UTF8ToString($2);
            link.click();
            setTimeout(() => URL.revokeObjectURL(link.href), 1000);
        }, data, size, filename);
    } else {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "couldn't save", SDL_GetError(), state->window);
    }
    SDL_CloseIO(io);
    SDL_DestroySurface(surface);
#else
    SaveRequest *request = SDL_malloc(sizeof(SaveRequest));
    if (!request) return;
    *request = (SaveRequest){ .width = state->canvas_width, .height = state->canvas_height, .pixels = pixels };

    static const SDL_DialogFileFilter filters[] = { { "PNG image", "png" } };
    SDL_ShowSaveFileDialog(save_dialog_callback, request, state->window, filters, SDL_arraysize(filters), filename);
#endif
}

// Every text object is tracked here so SDL_AppQuit can destroy them all
TTF_Text *create_text(AppState *state, const char *string) {
    SDL_assert(state->text_count < (int)SDL_arraysize(state->texts));

    TTF_Text *text = TTF_CreateText(state->text_engine, state->font, string, 0);
    if (!text) {
        SDL_Log("Failed to create text object: %s",SDL_GetError());
        return NULL;
    }
    state->texts[state->text_count++] = text;
    return text;
}

bool create_button(AppState *state, Button *button, const char *label) {
    button->text = create_text(state, label);
    if (!button->text) return false;

    int w, h;
    TTF_GetTextSize(button->text, &w, &h);
    button->rect.w = w + 20;
    button->rect.h = h + 10;
    return true;
}

bool create_arrow_button(AppState *state, Button *button, const char *label) {
    if (!create_button(state, button, label)) return false;
    button->rect.w = button->rect.h;
    return true;
}

bool button_clicked(const Button *button, float x, float y) {
    if (!button->text) return false;

    const SDL_FPoint point = { x, y };
    return SDL_PointInRectFloat(&point, &button->rect);
}

void place_size_row(AppState *state, float y, SDL_FRect *field, Button *down, Button *up) {
    const float half_gap = 70;
    const float cx = state->window_width / 2.0f;

    down->rect.x = cx - half_gap - down->rect.w;
    down->rect.y = y;
    up->rect.x = cx + half_gap;
    up->rect.y = y;

    *field = (SDL_FRect){
        .x = down->rect.x + down->rect.w + 4,
        .y = y,
        .w = up->rect.x - (down->rect.x + down->rect.w) - 8,
        .h = down->rect.h
    };
}

void layout(AppState *state) {
    const float w = state->window_width, h = state->window_height;

    state->ready_button.rect.x = w/2 - state->ready_button.rect.w/2;
    state->ready_button.rect.y = h/2 - state->ready_button.rect.h/2 + 50;

    const float size_rows_y = state->ready_button.rect.y + state->ready_button.rect.h + 20;
    place_size_row(state, size_rows_y, &state->width_field, &state->width_down_button, &state->width_up_button);
    place_size_row(state, size_rows_y + state->width_down_button.rect.h + 6, &state->height_field, &state->height_down_button, &state->height_up_button);

    state->info_button.rect.x = w - state->info_button.rect.w - 10;
    state->info_button.rect.y = 10;

    state->back_button.rect.x = 10;
    state->back_button.rect.y = 10;

    // Wrap to the window on phones, but keep lines readable on wide screens
    TTF_SetTextWrapWidth(state->info_text, (int)SDL_min(w - 40, 500));
    int info_w = 0, info_h = 0;
    TTF_GetTextSize(state->info_text, &info_w, &info_h);
    float info_block_h = info_h + 20 + state->github_button.rect.h + 10 + state->other_version_button.rect.h;
    if (state->license_button.text) info_block_h += 10 + state->license_button.rect.h;
    state->info_text_y = h/2 - info_block_h/2;
    state->github_button.rect.x = w/2 - state->github_button.rect.w/2;
    state->github_button.rect.y = state->info_text_y + info_h + 20;
    state->other_version_button.rect.x = w/2 - state->other_version_button.rect.w/2;
    state->other_version_button.rect.y = state->github_button.rect.y + state->github_button.rect.h + 10;
    state->license_button.rect.x = w/2 - state->license_button.rect.w/2;
    state->license_button.rect.y = state->other_version_button.rect.y + state->other_version_button.rect.h + 10;

    state->rotate_left_button.rect.x = 10;
    state->rotate_left_button.rect.y = 10;
    state->rotate_reset_button.rect.x = state->rotate_left_button.rect.x + state->rotate_left_button.rect.w + 5;
    state->rotate_reset_button.rect.y = 10;
    state->rotate_right_button.rect.x = state->rotate_reset_button.rect.x + state->rotate_reset_button.rect.w + 5;
    state->rotate_right_button.rect.y = 10;

    state->reset_view_button.rect.x = w - state->reset_view_button.rect.w - 10;
    state->reset_view_button.rect.y = 10;

    state->finish_button.rect.x = w - state->finish_button.rect.w - 10;
    state->finish_button.rect.y = h - state->finish_button.rect.h - 10;

    state->save_drawing_button.rect.x = 10;
    state->save_drawing_button.rect.y = 10;
    state->save_timestamps_button.rect.x = 10;
    state->save_timestamps_button.rect.y = state->save_drawing_button.rect.y + state->save_drawing_button.rect.h + 10;
}

void update_cursor(AppState *state) {
    const bool *key_state = SDL_GetKeyboardState(NULL);
    const bool want_move_cursor = showing_canvas(state) && (state->panning || key_state[SDL_SCANCODE_SPACE]);
    if (want_move_cursor != state->showing_move_cursor && state->move_cursor) {
        SDL_SetCursor(want_move_cursor ? state->move_cursor : SDL_GetDefaultCursor());
        state->showing_move_cursor = want_move_cursor;
    }
}

void update_zoom_text(AppState *state) {
    const int zoom_percent = (int)SDL_round(SDL_pow(2, state->canvas_zoom) * 100);
    if (zoom_percent == state->zoom_text_percent) return;

    char buf[32];
    SDL_snprintf(buf, sizeof(buf), "%d%%", zoom_percent);
    TTF_SetTextString(state->zoom_text, buf, 0);
    state->zoom_text_percent = zoom_percent;
}

void draw_text_centered(TTF_Text *text, float cx, float y) {
    int tw = 0, th = 0;
    TTF_GetTextSize(text, &tw, &th);
    TTF_DrawRendererText(text, (int)(cx - tw/2.0f), (int)y);
}

void draw_button(AppState *state, const Button *button) {
    SDL_SetRenderDrawColor(state->renderer, 0, 0, 0, 255);
    SDL_RenderFillRect(state->renderer, &button->rect);
    SDL_SetRenderDrawColor(state->renderer, 255, 255, 255, 255);
    SDL_RenderRect(state->renderer, &button->rect);

    int tw = 0, th = 0;
    TTF_GetTextSize(button->text, &tw, &th);
    TTF_DrawRendererText(button->text, (int)(button->rect.x + button->rect.w/2 - tw/2.0f), (int)(button->rect.y + button->rect.h/2 - th/2.0f));
}

void draw_size_row(AppState *state, TTF_Text *label, const SDL_FRect *field, bool editing, const Button *down, const Button *up) {
    if (editing) {
        SDL_SetRenderDrawColor(state->renderer, 255, 255, 255, 255);
        SDL_RenderRect(state->renderer, field);
    }

    draw_button(state, down);
    draw_button(state, up);

    int tw = 0, th = 0;
    TTF_GetTextSize(label, &tw, &th);
    TTF_DrawRendererText(label, (int)(field->x + field->w/2 - tw/2.0f), (int)(field->y + field->h/2 - th/2.0f));
}

void draw_canvas(AppState *state, const uint32_t *pixels) {
    SDL_UpdateTexture(state->canvas_texture, NULL, pixels, state->canvas_width * sizeof(uint32_t));

    double zoom_factor = SDL_pow(2, state->canvas_zoom);

    SDL_FRect canvas_rect = {
        .w = state->canvas_width  * zoom_factor,
        .h = state->canvas_height * zoom_factor,
    };
    canvas_rect.x = state->canvas_x + state->window_width/2.0  - canvas_rect.w/2;
    canvas_rect.y = state->canvas_y + state->window_height/2.0 - canvas_rect.h/2;

    SDL_RenderTextureRotated(state->renderer, state->canvas_texture, NULL, &canvas_rect, state->canvas_rotation, NULL, SDL_FLIP_NONE);

    // 2 screen pixels outside the canvas edge
    const double o = 2 / zoom_factor;
    const double w = state->canvas_width, h = state->canvas_height;
    const double corners[4][2] = { { -o, -o }, { w + o, -o }, { w + o, h + o }, { -o, h + o } };

    SDL_FPoint border[5];
    for (int i = 0; i < 4; i++) {
        double x, y;
        canvas_to_screen(state, corners[i][0], corners[i][1], &x, &y);
        border[i] = (SDL_FPoint){ (float)SDL_floor(x), (float)SDL_floor(y) };
    }
    border[4] = border[0];

    SDL_SetRenderDrawColor(state->renderer, 255, 255, 255, 255);
    SDL_RenderLines(state->renderer, border, 5);
}

void draw_zoom_text(AppState *state) {
    int tw = 0, th = 0;
    TTF_GetTextSize(state->zoom_text, &tw, &th);
    TTF_DrawRendererText(state->zoom_text, 10, state->window_height - th - 10);
}

void draw_intro_screen(AppState *state) {
    int tw = 0, th = 0;
    TTF_GetTextSize(state->intro_text, &tw, &th);
    draw_text_centered(state->intro_text, state->window_width/2.0f, state->window_height/2.0f - th/2.0f);

    draw_button(state, &state->ready_button);
    draw_button(state, &state->info_button);
    draw_size_row(state, state->width_text,  &state->width_field,  state->editing_field == SIZE_FIELD_WIDTH,  &state->width_down_button,  &state->width_up_button);
    draw_size_row(state, state->height_text, &state->height_field, state->editing_field == SIZE_FIELD_HEIGHT, &state->height_down_button, &state->height_up_button);
}

void draw_info_screen(AppState *state) {
    draw_text_centered(state->info_text, state->window_width/2.0f, state->info_text_y);
    draw_button(state, &state->github_button);
    draw_button(state, &state->other_version_button);
    if (state->license_button.text) draw_button(state, &state->license_button);
    draw_button(state, &state->back_button);
}

void draw_drawing_screen(AppState *state) {
    draw_canvas(state, state->drawing_pixels);

    draw_button(state, &state->rotate_left_button);
    draw_button(state, &state->rotate_reset_button);
    draw_button(state, &state->rotate_right_button);
    draw_button(state, &state->reset_view_button);
    draw_button(state, &state->finish_button);
    draw_zoom_text(state);
}

void draw_finished_screen(AppState *state) {
    draw_canvas(state, state->timestamp_pixels);

    int tw = 0, th = 0;
    TTF_GetTextSize(state->final_screen_text, &tw, &th);
    draw_text_centered(state->final_screen_text, state->window_width/2.0f, state->window_height - th - 5);

    draw_button(state, &state->save_drawing_button);
    draw_button(state, &state->save_timestamps_button);
    draw_button(state, &state->reset_view_button);
    draw_zoom_text(state);
}

void on_intro_click(AppState *state, const SDL_MouseButtonEvent *button) {
    const float x = button->x, y = button->y;

    finish_size_edit(state);

    if (button_clicked(&state->ready_button, x, y)) {
        state->canvas_width  = state->chosen_width;
        state->canvas_height = state->chosen_height;
        if (initialize_canvas(state)) {
            state->screen = SCREEN_DRAWING;
        } else {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "couldn't make the canvas", SDL_GetError(), state->window);
        }
    }
    else if (button_clicked(&state->info_button, x, y))        state->screen = SCREEN_INFO;
    else if (button_clicked(&state->width_down_button, x, y))  state->chosen_width  = next_preset_down(state->chosen_width);
    else if (button_clicked(&state->width_up_button, x, y))    state->chosen_width  = next_preset_up(state->chosen_width);
    else if (button_clicked(&state->height_down_button, x, y)) state->chosen_height = next_preset_down(state->chosen_height);
    else if (button_clicked(&state->height_up_button, x, y))   state->chosen_height = next_preset_up(state->chosen_height);
    else if (SDL_PointInRectFloat(&(SDL_FPoint){ x, y }, &state->width_field))  start_size_edit(state, SIZE_FIELD_WIDTH);
    else if (SDL_PointInRectFloat(&(SDL_FPoint){ x, y }, &state->height_field)) start_size_edit(state, SIZE_FIELD_HEIGHT);

    update_size_text(state);
}

void open_other_version(void) {
#ifdef SDL_PLATFORM_EMSCRIPTEN
    EM_ASM({
        const link = document.createElement('a');
        link.href = UTF8ToString($0);
        link.download = UTF8ToString($0);
        link.click();
    }, EXE_DOWNLOAD_PATH);
#else
    SDL_OpenURL(WEB_URL);
#endif
}

void on_info_click(AppState *state, const SDL_MouseButtonEvent *button) {
    const float x = button->x, y = button->y;

    if (button_clicked(&state->github_button, x, y))             SDL_OpenURL(GITHUB_URL);
    else if (button_clicked(&state->other_version_button, x, y)) open_other_version();
    else if (button_clicked(&state->license_button, x, y))       SDL_OpenURL(LICENSE_URL);
    else if (button_clicked(&state->back_button, x, y))          state->screen = SCREEN_INTRO;
}

void on_drawing_click(AppState *state, const SDL_MouseButtonEvent *button) {
    const float x = button->x, y = button->y;

    if (button_clicked(&state->finish_button, x, y))            finish_canvas(state);
    else if (button_clicked(&state->rotate_left_button, x, y))  state->canvas_rotation -= 10;
    else if (button_clicked(&state->rotate_right_button, x, y)) state->canvas_rotation += 10;
    else if (button_clicked(&state->rotate_reset_button, x, y)) state->canvas_rotation = 0;
    else if (button_clicked(&state->reset_view_button, x, y))   reset_view(state);
    else {
        state->mouse_down = button->button == SDL_BUTTON_LEFT && state->gesture_finger_count < 2;
        screen_to_canvas(state, x, y, &state->last_mouse_x, &state->last_mouse_y);
        if (state->mouse_down) {
            paint_pixel(state, (int)SDL_floor(state->last_mouse_x), (int)SDL_floor(state->last_mouse_y), button->timestamp);
        }
    }
}

void on_finished_click(AppState *state, const SDL_MouseButtonEvent *button) {
    const float x = button->x, y = button->y;

    if (button_clicked(&state->save_drawing_button, x, y))         save_image(state, state->drawing_pixels, "fill-a-canvas.png");
    else if (button_clicked(&state->save_timestamps_button, x, y)) save_image(state, state->timestamp_pixels, "fill-a-canvas-timestamps.png");
    else if (button_clicked(&state->reset_view_button, x, y))      reset_view(state);
    else state->mouse_down = button->button == SDL_BUTTON_LEFT;
}

void on_size_edit_key(AppState *state, SDL_Keycode key) {
    size_t len = SDL_strlen(state->edit_buffer);
    switch (key) {
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
}

void on_view_key(AppState *state, const SDL_KeyboardEvent *key) {
    if (!(key->mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI))) return;

    const double cx = state->window_width / 2.0, cy = state->window_height / 2.0;
    switch (key->key) {
        case SDLK_EQUALS:
        case SDLK_PLUS:
        case SDLK_KP_PLUS:
            zoom_at(state, cx, cy, 0.5);
            break;
        case SDLK_MINUS:
        case SDLK_KP_MINUS:
            zoom_at(state, cx, cy, -0.5);
            break;
        case SDLK_0:
        case SDLK_KP_0:
            reset_view(state);
            break;
    }
}

int find_gesture_finger(AppState *state, SDL_FingerID id) {
    for (int i = 0; i < state->gesture_finger_count; ++i) {
        if (state->gesture_fingers[i] == id) return i;
    }
    return -1;
}

void on_finger_motion(AppState *state, const SDL_TouchFingerEvent *finger) {
    const int i = find_gesture_finger(state, finger->fingerID);
    if (i < 0) return;

    const SDL_FPoint old_a = state->gesture_points[0], old_b = state->gesture_points[1];
    state->gesture_points[i] = (SDL_FPoint){ finger->x * state->window_width, finger->y * state->window_height };
    if (state->gesture_finger_count < 2) return;

    const SDL_FPoint a = state->gesture_points[0], b = state->gesture_points[1];
    const double mid_x = (a.x + b.x) / 2.0, mid_y = (a.y + b.y) / 2.0;

    state->canvas_x += mid_x - (old_a.x + old_b.x) / 2.0;
    state->canvas_y += mid_y - (old_a.y + old_b.y) / 2.0;

    // Platforms with native pinch events already zoom through SDL_EVENT_PINCH_UPDATE
    const double old_dist = SDL_sqrt(SDL_pow(old_a.x - old_b.x, 2) + SDL_pow(old_a.y - old_b.y, 2));
    const double dist = SDL_sqrt(SDL_pow(a.x - b.x, 2) + SDL_pow(a.y - b.y, 2));
    if (!state->has_native_pinch && old_dist > 0 && dist > 0) {
        zoom_at(state, mid_x, mid_y, log2d(dist / old_dist));
    }
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

    state->text_engine = TTF_CreateRendererTextEngine(state->renderer);
    if (!state->text_engine) {
        SDL_Log("Failed to create text engine: %s",SDL_GetError());
        return SDL_APP_FAILURE;
    }

    state->font = TTF_OpenFontIO(SDL_IOFromConstMem(roboto_regular_ttf, roboto_regular_ttf_size), true, 16);
    if (!state->font) {
        SDL_Log("Failed to load font: %s",SDL_GetError());
        return SDL_APP_FAILURE;
    }

    TTF_SetFontWrapAlignment(state->font, TTF_HORIZONTAL_ALIGN_CENTER);

    state->intro_text = create_text(state, "are you ready to fill in \na canvas using a 1px brush");
    state->width_text = create_text(state, "");
    state->height_text = create_text(state, "");
    state->zoom_text = create_text(state, "");
    state->final_screen_text = create_text(state, "You are did it I am so of proud of you!");
    state->info_text = create_text(state, INFO_TEXT);

    const bool created =
        state->intro_text && state->width_text && state->height_text && state->zoom_text && state->final_screen_text && state->info_text &&
        create_button(state, &state->ready_button, "yeah i'm ready") &&
        create_button(state, &state->info_button, "info") &&
        create_button(state, &state->github_button, "view on github") &&
#ifdef SDL_PLATFORM_EMSCRIPTEN
        create_button(state, &state->other_version_button, "download for windows") &&
#else
        create_button(state, &state->other_version_button, "play in browser") &&
        create_button(state, &state->license_button, "font license (OFL)") &&
#endif
        create_button(state, &state->back_button, "back") &&
        create_arrow_button(state, &state->width_down_button, "<") &&
        create_arrow_button(state, &state->width_up_button, ">") &&
        create_arrow_button(state, &state->height_down_button, "<") &&
        create_arrow_button(state, &state->height_up_button, ">") &&
        create_button(state, &state->finish_button, "i finished i think") &&
        create_arrow_button(state, &state->rotate_left_button, "<") &&
        create_button(state, &state->rotate_reset_button, "reset rotation") &&
        create_arrow_button(state, &state->rotate_right_button, ">") &&
        create_button(state, &state->reset_view_button, "reset view") &&
        create_button(state, &state->save_drawing_button, "save drawing") &&
        create_button(state, &state->save_timestamps_button, "save timestamps");
    if (!created) {
        return SDL_APP_FAILURE;
    }

    state->chosen_width = 100;
    state->chosen_height = 100;
    update_size_text(state);

    state->move_cursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_MOVE);

#ifdef SDL_PLATFORM_EMSCRIPTEN
    install_pinch_listener();
#endif

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
    AppState *state = (AppState *)appstate;
    const bool viewing_canvas = showing_canvas(state);

    switch (event->type) {
        case SDL_EVENT_QUIT: {
            return SDL_APP_SUCCESS;
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN: {
            if (viewing_canvas && (event->button.button == SDL_BUTTON_MIDDLE || event->button.button == SDL_BUTTON_RIGHT)) {
                state->panning = true;
                break;
            }

            switch (state->screen) {
                case SCREEN_INTRO:    on_intro_click(state, &event->button);    break;
                case SCREEN_INFO:     on_info_click(state, &event->button);     break;
                case SCREEN_DRAWING:  on_drawing_click(state, &event->button);  break;
                case SCREEN_FINISHED: on_finished_click(state, &event->button); break;
            }
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            if (event->button.button == SDL_BUTTON_LEFT) state->mouse_down = false;
            else if (event->button.button == SDL_BUTTON_MIDDLE || event->button.button == SDL_BUTTON_RIGHT) state->panning = false;
            break;
        }
        case SDL_EVENT_MOUSE_MOTION: {
            const bool *key_state = SDL_GetKeyboardState(NULL);
            if (state->panning || (state->mouse_down && key_state[SDL_SCANCODE_SPACE])) {
                state->canvas_x += event->motion.xrel;
                state->canvas_y += event->motion.yrel;

                // Otherwise drawing after the pan would join up with where the stroke was before it
                screen_to_canvas(state, event->motion.x, event->motion.y, &state->last_mouse_x, &state->last_mouse_y);
                break;
            }

            if (!state->mouse_down || state->screen != SCREEN_DRAWING) break;

            double mx, my;
            screen_to_canvas(state, event->motion.x, event->motion.y, &mx, &my);
            paint_line(state,
                (int)SDL_floor(mx), (int)SDL_floor(my),
                (int)SDL_floor(state->last_mouse_x), (int)SDL_floor(state->last_mouse_y),
                event->motion.timestamp);

            state->last_mouse_x = mx;
            state->last_mouse_y = my;
            break;
        }
        case SDL_EVENT_MOUSE_WHEEL: {
            if (viewing_canvas) zoom_at(state, event->wheel.mouse_x, event->wheel.mouse_y, event->wheel.y * 0.1);
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
            if (state->editing_field != SIZE_FIELD_NONE) on_size_edit_key(state, event->key.key);
            else if (state->screen == SCREEN_INFO && event->key.key == SDLK_ESCAPE) state->screen = SCREEN_INTRO;
            else if (viewing_canvas) on_view_key(state, &event->key);
            break;
        }
        case SDL_EVENT_FINGER_DOWN: {
            if (!viewing_canvas || state->gesture_finger_count == 2) break;

            const int i = state->gesture_finger_count++;
            state->gesture_fingers[i] = event->tfinger.fingerID;
            state->gesture_points[i] = (SDL_FPoint){ event->tfinger.x * state->window_width, event->tfinger.y * state->window_height };

            // The first finger has already started a stroke through SDL's touch-to-mouse emulation
            if (state->gesture_finger_count == 2) state->mouse_down = false;
            break;
        }
        case SDL_EVENT_FINGER_MOTION: {
            on_finger_motion(state, &event->tfinger);
            break;
        }
        case SDL_EVENT_FINGER_UP:
        case SDL_EVENT_FINGER_CANCELED: {
            const int i = find_gesture_finger(state, event->tfinger.fingerID);
            if (i < 0) break;

            state->gesture_fingers[i] = state->gesture_fingers[state->gesture_finger_count-1];
            state->gesture_points[i] = state->gesture_points[state->gesture_finger_count-1];
            state->gesture_finger_count--;
            break;
        }
        case SDL_EVENT_PINCH_BEGIN: {
            state->has_native_pinch = true;
            break;
        }
        case SDL_EVENT_PINCH_UPDATE: {
            if (!viewing_canvas) break;
            float mx, my;
            SDL_GetMouseState(&mx, &my);
            zoom_at(state, mx, my, log2d(event->pinch.scale));
            break;
        }
    }
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate) {
    AppState *state = (AppState *)appstate;

    SDL_GetWindowSize(state->window, &state->window_width, &state->window_height);

#ifdef SDL_PLATFORM_EMSCRIPTEN
    const double pinch_zoom = take_pinch_zoom();
    if (pinch_zoom != 0 && showing_canvas(state)) {
        float mx, my;
        SDL_GetMouseState(&mx, &my);
        zoom_at(state, mx, my, pinch_zoom);
    }
#endif

    layout(state);
    update_cursor(state);
    update_zoom_text(state);

    SDL_SetRenderDrawColor(state->renderer, 0, 0, 0, 255);
    SDL_RenderClear(state->renderer);

    switch (state->screen) {
        case SCREEN_INTRO:    draw_intro_screen(state);    break;
        case SCREEN_INFO:     draw_info_screen(state);     break;
        case SCREEN_DRAWING:  draw_drawing_screen(state);  break;
        case SCREEN_FINISHED: draw_finished_screen(state); break;
    }

    SDL_RenderPresent(state->renderer);

    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result) {
    if (appstate) {
        AppState *state = (AppState *)appstate;
        for (int i = 0; i < state->text_count; ++i) {
            TTF_DestroyText(state->texts[i]);
        }
        TTF_DestroyRendererTextEngine(state->text_engine);
        TTF_CloseFont(state->font);
        SDL_DestroyTexture(state->canvas_texture);
        SDL_DestroyCursor(state->move_cursor);
        SDL_DestroyRenderer(state->renderer);
        SDL_DestroyWindow(state->window);
        SDL_free(state->paint_times);
        SDL_free(state->drawing_pixels);
        SDL_free(state->timestamp_pixels);
        SDL_free(state);
    }
    TTF_Quit();
    SDL_Quit();
}
