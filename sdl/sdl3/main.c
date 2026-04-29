/*
 * Copyright PeakRacing
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "nes.h"

#include <SDL3/SDL.h>

enum {
    LAUNCHER_RESULT_ERROR = -1,
    LAUNCHER_RESULT_QUIT = 0,
    LAUNCHER_RESULT_SELECTED = 1
};

#define LAUNCHER_WIDTH     720
#define LAUNCHER_HEIGHT    420

typedef enum {
    LAUNCHER_MESSAGE_INFO,
    LAUNCHER_MESSAGE_ERROR,
    LAUNCHER_MESSAGE_LOADING
} launcher_message_type_t;

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
} launcher_color_t;

typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Mutex *mutex;
    char *selected_rom;
    char message[256];
    launcher_message_type_t message_type;
    bool dialog_open;
    bool button_hovered;
    bool quit;
} launcher_t;

static const launcher_color_t color_bg_top = { 13, 18, 30, 255 };
static const launcher_color_t color_bg_bottom = { 28, 35, 52, 255 };
static const launcher_color_t color_card = { 32, 41, 60, 255 };
static const launcher_color_t color_card_border = { 72, 84, 110, 255 };
static const launcher_color_t color_text = { 241, 245, 249, 255 };
static const launcher_color_t color_text_muted = { 148, 163, 184, 255 };
static const launcher_color_t color_button = { 59, 130, 246, 255 };
static const launcher_color_t color_button_hover = { 96, 165, 250, 255 };
static const launcher_color_t color_button_disabled = { 71, 85, 105, 255 };
static const launcher_color_t color_status_info = { 125, 211, 252, 255 };
static const launcher_color_t color_status_error = { 248, 113, 113, 255 };
static const launcher_color_t color_status_loading = { 134, 239, 172, 255 };

static const SDL_DialogFileFilter launcher_rom_filters[] = {
    { "NES ROM", "nes;NES" },
    { "All files", "*" }
};

static bool is_nes_file_path(const char *path){
    if (!path){
        return false;
    }

    size_t len = SDL_strlen(path);
    return (len >= 4) && (SDL_strcasecmp(path + len - 4, ".nes") == 0);
}

static void launcher_set_message(launcher_t *launcher, const char *message, launcher_message_type_t type){
    SDL_strlcpy(launcher->message, message ? message : "", sizeof(launcher->message));
    launcher->message_type = type;
}

static void SDLCALL launcher_file_dialog_callback(void *userdata, const char * const *filelist, int filter){
    (void)filter;
    launcher_t *launcher = (launcher_t *)userdata;

    SDL_LockMutex(launcher->mutex);
    SDL_free(launcher->selected_rom);
    launcher->selected_rom = NULL;
    launcher->dialog_open = false;

    if (!filelist){
        const char *error = SDL_GetError();
        launcher_set_message(launcher,
                             error && error[0] ? error : "Failed to open file dialog.",
                             LAUNCHER_MESSAGE_ERROR);
    }else if (!filelist[0]){
        launcher_set_message(launcher, "Ready when you are.", LAUNCHER_MESSAGE_INFO);
    }else if (!is_nes_file_path(filelist[0])){
        launcher_set_message(launcher, "Please select a .nes ROM file.", LAUNCHER_MESSAGE_ERROR);
    }else{
        launcher->selected_rom = SDL_strdup(filelist[0]);
        if (launcher->selected_rom){
            launcher_set_message(launcher, "Loading selected ROM...", LAUNCHER_MESSAGE_LOADING);
        }else{
            launcher_set_message(launcher,
                                 "Out of memory while saving ROM path.",
                                 LAUNCHER_MESSAGE_ERROR);
        }
    }
    SDL_UnlockMutex(launcher->mutex);
}

static bool launcher_dialog_is_open(launcher_t *launcher){
    bool dialog_open;

    SDL_LockMutex(launcher->mutex);
    dialog_open = launcher->dialog_open;
    SDL_UnlockMutex(launcher->mutex);

    return dialog_open;
}

static void launcher_open_rom_dialog(launcher_t *launcher){
    SDL_LockMutex(launcher->mutex);
    if (launcher->dialog_open){
        SDL_UnlockMutex(launcher->mutex);
        return;
    }
    launcher->dialog_open = true;
    launcher->button_hovered = false;
    launcher_set_message(launcher, "Opening ROM file dialog...", LAUNCHER_MESSAGE_LOADING);
    SDL_UnlockMutex(launcher->mutex);

    SDL_ShowOpenFileDialog(launcher_file_dialog_callback,
                           launcher,
                           launcher->window,
                           launcher_rom_filters,
                           (int)(sizeof(launcher_rom_filters) / sizeof(launcher_rom_filters[0])),
                           NULL,
                           false);
}

static void launcher_request_quit(launcher_t *launcher){
    SDL_LockMutex(launcher->mutex);
    if (launcher->dialog_open){
        launcher_set_message(launcher,
                             "Close the file dialog before exiting.",
                             LAUNCHER_MESSAGE_INFO);
    }else{
        launcher->quit = true;
    }
    SDL_UnlockMutex(launcher->mutex);
}

static char *launcher_take_selected_rom(launcher_t *launcher){
    char *selected_rom;

    SDL_LockMutex(launcher->mutex);
    selected_rom = launcher->selected_rom;
    launcher->selected_rom = NULL;
    SDL_UnlockMutex(launcher->mutex);

    return selected_rom;
}

static bool launcher_should_quit(launcher_t *launcher){
    bool quit;

    SDL_LockMutex(launcher->mutex);
    quit = launcher->quit;
    SDL_UnlockMutex(launcher->mutex);

    return quit;
}

static bool launcher_point_in_rect(float x, float y, const SDL_FRect *rect){
    return x >= rect->x && x < (rect->x + rect->w) && y >= rect->y && y < (rect->y + rect->h);
}

static void launcher_set_color(SDL_Renderer *renderer, launcher_color_t color){
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
}

static float launcher_text_width(const char *text){
    return text ? ((float)SDL_strlen(text) * SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE) : 0.0f;
}

static void launcher_draw_text_centered(SDL_Renderer *renderer,
                                        const char *text,
                                        float center_x,
                                        float y){
    SDL_RenderDebugText(renderer, center_x - (launcher_text_width(text) / 2.0f), y, text);
}

static void launcher_draw_text_centered_scaled(SDL_Renderer *renderer,
                                               const char *text,
                                               float center_x,
                                               float y,
                                               float scale){
    if (SDL_SetRenderScale(renderer, scale, scale)){
        SDL_RenderDebugText(renderer,
                            (center_x - (launcher_text_width(text) * scale / 2.0f)) / scale,
                            y / scale,
                            text);
        SDL_SetRenderScale(renderer, 1.0f, 1.0f);
    }else{
        launcher_draw_text_centered(renderer, text, center_x, y);
    }
}

static void launcher_draw_rect(SDL_Renderer *renderer, const SDL_FRect *rect, launcher_color_t color){
    launcher_set_color(renderer, color);
    SDL_RenderFillRect(renderer, rect);
}

static void launcher_draw_background(SDL_Renderer *renderer){
    SDL_FRect top = { .x = 0, .y = 0, .w = LAUNCHER_WIDTH, .h = LAUNCHER_HEIGHT / 2.0f };
    SDL_FRect bottom = { .x = 0, .y = LAUNCHER_HEIGHT / 2.0f, .w = LAUNCHER_WIDTH, .h = LAUNCHER_HEIGHT / 2.0f };
    SDL_FRect glow = { .x = 96, .y = 40, .w = 528, .h = 120 };

    launcher_draw_rect(renderer, &top, color_bg_top);
    launcher_draw_rect(renderer, &bottom, color_bg_bottom);
    launcher_draw_rect(renderer, &glow, (launcher_color_t){ 29, 78, 216, 32 });
}

static void launcher_draw_card(SDL_Renderer *renderer, const SDL_FRect *card){
    SDL_FRect shadow = { .x = card->x + 8, .y = card->y + 10, .w = card->w, .h = card->h };
    SDL_FRect accent = { .x = card->x, .y = card->y, .w = card->w, .h = 4 };

    launcher_draw_rect(renderer, &shadow, (launcher_color_t){ 2, 6, 23, 92 });
    launcher_draw_rect(renderer, card, color_card);
    launcher_set_color(renderer, color_card_border);
    SDL_RenderRect(renderer, card);
    launcher_draw_rect(renderer, &accent, color_button);
}

static launcher_color_t launcher_status_color(launcher_message_type_t type){
    switch (type){
        case LAUNCHER_MESSAGE_ERROR:
            return color_status_error;
        case LAUNCHER_MESSAGE_LOADING:
            return color_status_loading;
        case LAUNCHER_MESSAGE_INFO:
        default:
            return color_status_info;
    }
}

static void launcher_handle_events(launcher_t *launcher, const SDL_FRect *open_button){
    SDL_Event event;

    while (SDL_PollEvent(&event)){
        switch (event.type){
            case SDL_EVENT_QUIT:
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                launcher_request_quit(launcher);
                break;
            case SDL_EVENT_KEY_DOWN:
                if (event.key.scancode == SDL_SCANCODE_ESCAPE){
                    launcher_request_quit(launcher);
                }else if (event.key.scancode == SDL_SCANCODE_RETURN || event.key.scancode == SDL_SCANCODE_KP_ENTER){
                    launcher_open_rom_dialog(launcher);
                }
                break;
            case SDL_EVENT_MOUSE_MOTION:
                SDL_LockMutex(launcher->mutex);
                launcher->button_hovered = !launcher->dialog_open &&
                                           launcher_point_in_rect(event.motion.x, event.motion.y, open_button);
                SDL_UnlockMutex(launcher->mutex);
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (event.button.button == SDL_BUTTON_LEFT &&
                    launcher_point_in_rect(event.button.x, event.button.y, open_button)){
                    launcher_open_rom_dialog(launcher);
                }
                break;
            default:
                break;
        }
    }
}

static void launcher_draw(launcher_t *launcher, const SDL_FRect *open_button){
    char message[256];
    bool dialog_open;
    bool button_hovered;
    launcher_message_type_t message_type;
    SDL_FRect card = { .x = 110, .y = 58, .w = 500, .h = 304 };
    SDL_FRect button_shadow = { .x = open_button->x, .y = open_button->y + 6, .w = open_button->w, .h = open_button->h };
    SDL_FRect button_highlight = { .x = open_button->x, .y = open_button->y, .w = open_button->w, .h = 3 };
    SDL_FRect status_dot = { .x = 192, .y = 285, .w = 8, .h = 8 };

    SDL_LockMutex(launcher->mutex);
    SDL_strlcpy(message, launcher->message, sizeof(message));
    dialog_open = launcher->dialog_open;
    button_hovered = launcher->button_hovered;
    message_type = launcher->message_type;
    SDL_UnlockMutex(launcher->mutex);

    launcher_draw_background(launcher->renderer);
    launcher_draw_card(launcher->renderer, &card);

    launcher_set_color(launcher->renderer, color_text);
    launcher_draw_text_centered_scaled(launcher->renderer, "NES Emulator", LAUNCHER_WIDTH / 2.0f, 86, 2.0f);
    launcher_set_color(launcher->renderer, color_text_muted);
    launcher_draw_text_centered(launcher->renderer,
                                "Select a .nes ROM to start playing",
                                LAUNCHER_WIDTH / 2.0f,
                                126);
    launcher_draw_text_centered(launcher->renderer,
                                "Terminal mode: nes.exe xxx.nes",
                                LAUNCHER_WIDTH / 2.0f,
                                146);

    launcher_draw_rect(launcher->renderer, &button_shadow, (launcher_color_t){ 2, 6, 23, 120 });
    launcher_draw_rect(launcher->renderer,
                       open_button,
                       dialog_open ? color_button_disabled :
                                     (button_hovered ? color_button_hover : color_button));
    launcher_draw_rect(launcher->renderer,
                       &button_highlight,
                       dialog_open ? (launcher_color_t){ 100, 116, 139, 255 } :
                                     (launcher_color_t){ 147, 197, 253, 255 });
    launcher_set_color(launcher->renderer, color_text);
    SDL_RenderRect(launcher->renderer, open_button);
    launcher_draw_text_centered_scaled(launcher->renderer,
                                       dialog_open ? "Opening..." : "Open ROM",
                                       open_button->x + (open_button->w / 2.0f),
                                       open_button->y + 21,
                                       1.5f);

    launcher_draw_rect(launcher->renderer, &status_dot, launcher_status_color(message_type));
    launcher_set_color(launcher->renderer, launcher_status_color(message_type));
    SDL_RenderDebugText(launcher->renderer, 210, 281, message);

    launcher_set_color(launcher->renderer, color_text_muted);
    launcher_draw_text_centered(launcher->renderer,
                                "Enter: Open ROM    Esc: Quit",
                                LAUNCHER_WIDTH / 2.0f,
                                326);

    SDL_RenderPresent(launcher->renderer);
}

static void launcher_cleanup(launcher_t *launcher){
    SDL_free(launcher->selected_rom);
    launcher->selected_rom = NULL;
    if (launcher->mutex){
        SDL_DestroyMutex(launcher->mutex);
        launcher->mutex = NULL;
    }
    if (launcher->renderer){
        SDL_DestroyRenderer(launcher->renderer);
    }
    if (launcher->window){
        SDL_DestroyWindow(launcher->window);
    }
    launcher->renderer = NULL;
    launcher->window = NULL;
    SDL_Quit();
}

static int launcher_select_rom(char **selected_rom){
    launcher_t launcher = {0};
    SDL_FRect open_button = {
        .x = 250,
        .y = 188,
        .w = 220,
        .h = 56
    };
    int result = LAUNCHER_RESULT_QUIT;

    *selected_rom = NULL;

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)){
        SDL_Log("Can not init launcher, %s", SDL_GetError());
        return LAUNCHER_RESULT_ERROR;
    }
    if (!SDL_CreateWindowAndRenderer(NES_NAME " Launcher",
                                     LAUNCHER_WIDTH,
                                     LAUNCHER_HEIGHT,
                                     0,
                                     &launcher.window,
                                     &launcher.renderer)){
        SDL_Log("Can not create launcher window, %s", SDL_GetError());
        launcher_cleanup(&launcher);
        return LAUNCHER_RESULT_ERROR;
    }
    if (!SDL_SetRenderLogicalPresentation(launcher.renderer,
                                          LAUNCHER_WIDTH,
                                          LAUNCHER_HEIGHT,
                                          SDL_LOGICAL_PRESENTATION_LETTERBOX)){
        SDL_Log("Can not set launcher presentation, %s", SDL_GetError());
        launcher_cleanup(&launcher);
        return LAUNCHER_RESULT_ERROR;
    }
    if (!SDL_SetRenderDrawBlendMode(launcher.renderer, SDL_BLENDMODE_BLEND)){
        SDL_Log("Can not set launcher blend mode, %s", SDL_GetError());
        launcher_cleanup(&launcher);
        return LAUNCHER_RESULT_ERROR;
    }
    launcher.mutex = SDL_CreateMutex();
    if (!launcher.mutex){
        SDL_Log("Can not create launcher mutex, %s", SDL_GetError());
        launcher_cleanup(&launcher);
        return LAUNCHER_RESULT_ERROR;
    }

    launcher_set_message(&launcher, "Ready when you are.", LAUNCHER_MESSAGE_INFO);
    while (result == LAUNCHER_RESULT_QUIT){
        launcher_handle_events(&launcher, &open_button);
        *selected_rom = launcher_take_selected_rom(&launcher);
        if (*selected_rom){
            result = LAUNCHER_RESULT_SELECTED;
            break;
        }
        if (launcher_should_quit(&launcher)){
            break;
        }
        launcher_draw(&launcher, &open_button);
        SDL_Delay(16);
    }

    while (launcher_dialog_is_open(&launcher)){
        SDL_PumpEvents();
        SDL_Delay(16);
    }
    launcher_cleanup(&launcher);
    return result;
}

static int run_rom_file(const char *nes_file_path){
    if (!is_nes_file_path(nes_file_path)){
        NES_LOG_ERROR("Please enter xxx.nes\n");
        return -1;
    }

    nes_t* nes = nes_init();
    if (!nes){
        NES_LOG_ERROR("nes init fail\n");
        return -1;
    }

    NES_LOG_INFO("nes_file_path:%s\n",nes_file_path);
    int ret = nes_load_file(nes, nes_file_path);
    if (ret){
        NES_LOG_ERROR("nes load file fail\n");
        nes_deinit(nes);
        return -1;
    }

    nes_run(nes);
    nes_unload_file(nes);
    nes_deinit(nes);
    return 0;
}

int main(int argc, char** argv){
    if (argc == 2){
        return run_rom_file(argv[1]);
    }

    if (argc == 1){
        char *selected_rom = NULL;
        int launcher_result = launcher_select_rom(&selected_rom);
        if (launcher_result == LAUNCHER_RESULT_SELECTED){
            int ret = run_rom_file(selected_rom);
            SDL_free(selected_rom);
            return ret;
        }
        return launcher_result == LAUNCHER_RESULT_ERROR ? -1 : 0;
    }

    NES_LOG_ERROR("Please enter one nes file path\n");
    return -1;
}

