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

#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "nes.h"

#include <SDL.h>

/* memory */
void *nes_malloc(int num){
    return SDL_malloc(num);
}

void nes_free(void *address){
    SDL_free(address);
}

void *nes_memcpy(void *str1, const void *str2, size_t n){
    return SDL_memcpy(str1, str2, n);
}

void *nes_memset(void *str, int c, size_t n){
    return SDL_memset(str,c,n);
}

int nes_memcmp(const void *str1, const void *str2, size_t n){
    return SDL_memcmp(str1,str2,n);
}

#if (NES_USE_FS == 1)
/* io */
FILE *nes_fopen(const char * filename, const char * mode ){
    return fopen(filename,mode);
}

size_t nes_fread(void *ptr, size_t size, size_t nmemb, FILE *stream){
    return fread(ptr, size, nmemb,stream);
}

size_t nes_fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream){
    return fwrite(ptr, size, nmemb,stream);
}

int nes_fseek(FILE *stream, long int offset, int whence){
    return fseek(stream,offset,whence);
}

int nes_fclose(FILE *stream ){
    return fclose(stream);
}

long nes_ftell(FILE *stream){
    return ftell(stream);
}

int nes_remove(const char * filename){
    return remove(filename);
}
#endif

static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;
static SDL_Texture *framebuffer = NULL;
static uint64_t nes_next_frame_tick = 0;

#if (NES_USE_FS == 1)
static uint32_t sdl_title_until = 0;

/* Show a short lived status message in the window title (there is no OSD yet). */
static void sdl_status(const char* message) {
    if (window != NULL) {
        SDL_SetWindowTitle(window, message);
        sdl_title_until = SDL_GetTicks() + 2000u;
    }
    NES_LOG_INFO("%s\n", message);
}

static void sdl_save_state(nes_t* nes) {
    char path[NES_PATH_MAX];
    if (nes_state_default_path(nes, path, sizeof(path)) != NES_OK) {
        sdl_status("NES - save failed: no ROM path");
        return;
    }
    sdl_status(nes_state_save(nes, path) == NES_OK ? "NES - state saved (F5)" : "NES - save FAILED");
}

static void sdl_load_state(nes_t* nes) {
    char path[NES_PATH_MAX];
    if (nes_state_default_path(nes, path, sizeof(path)) != NES_OK) {
        sdl_status("NES - load failed: no ROM path");
        return;
    }
    sdl_status(nes_state_load(nes, path) == NES_OK ? "NES - state loaded (F8)" : "NES - load FAILED");
}
#endif

static void sdl_event(nes_t *nes) {
    SDL_Event event;
    while (SDL_PollEvent(&event)){
        switch (event.type) {
            case SDL_KEYDOWN:
                switch (event.key.keysym.scancode){
#if (NES_USE_FS == 1)
                    case 62://F5 save state
                        sdl_save_state(nes);
                        break;
                    case 65://F8 load state
                        sdl_load_state(nes);
                        break;
#endif
                    case 26://W
                        nes->nes_cpu.joypad.U1 = 1;
                        break;
                    case 22://S
                        nes->nes_cpu.joypad.D1 = 1;
                        break;
                    case 4://A
                        nes->nes_cpu.joypad.L1 = 1;
                        break;
                    case 7://D
                        nes->nes_cpu.joypad.R1 = 1;
                        break;
                    case 13://J
                        nes->nes_cpu.joypad.A1 = 1;
                        break;
                    case 14://K
                        nes->nes_cpu.joypad.B1 = 1;
                        break;
                    case 25://V
                        nes->nes_cpu.joypad.SE1 = 1;
                        break;
                    case 5://B
                        nes->nes_cpu.joypad.ST1 = 1;
                        break;
                    case 82://↑
                        nes->nes_cpu.joypad.U2 = 1;
                        break;
                    case 81://↓
                        nes->nes_cpu.joypad.D2 = 1;
                        break;
                    case 80://←
                        nes->nes_cpu.joypad.L2 = 1;
                        break;
                    case 79://→
                        nes->nes_cpu.joypad.R2 = 1;
                        break;
                    case 93://5
                        nes->nes_cpu.joypad.A2 = 1;
                        break;
                    case 94://6
                        nes->nes_cpu.joypad.B2 = 1;
                        break;
                    case 89://1
                        nes->nes_cpu.joypad.SE2 = 1;
                        break;
                    case 90://2
                        nes->nes_cpu.joypad.ST2 = 1;
                        break;
                    default:
                        break;
                    }
                break;
            case SDL_KEYUP:
                switch (event.key.keysym.scancode){
                    case 26://W
                        nes->nes_cpu.joypad.U1 = 0;
                        break;
                    case 22://S
                        nes->nes_cpu.joypad.D1 = 0;
                        break;
                    case 4://A
                        nes->nes_cpu.joypad.L1 = 0;
                        break;
                    case 7://D
                        nes->nes_cpu.joypad.R1 = 0;
                        break;
                    case 13://J
                        nes->nes_cpu.joypad.A1 = 0;
                        break;
                    case 14://K
                        nes->nes_cpu.joypad.B1 = 0;
                        break;
                    case 25://V
                        nes->nes_cpu.joypad.SE1 = 0;
                        break;
                    case 5://B
                        nes->nes_cpu.joypad.ST1 = 0;
                        break;
                    case 82://↑
                        nes->nes_cpu.joypad.U2 = 0;
                        break;
                    case 81://↓
                        nes->nes_cpu.joypad.D2 = 0;
                        break;
                    case 80://←
                        nes->nes_cpu.joypad.L2 = 0;
                        break;
                    case 79://→
                        nes->nes_cpu.joypad.R2 = 0;
                        break;
                    case 93://5
                        nes->nes_cpu.joypad.A2 = 0;
                        break;
                    case 94://6
                        nes->nes_cpu.joypad.B2 = 0;
                        break;
                    case 89://1
                        nes->nes_cpu.joypad.SE2 = 0;
                        break;
                    case 90://2
                        nes->nes_cpu.joypad.ST2 = 0;
                        break;
                    default:
                        break;
                    }
                break;
            case SDL_QUIT:
                nes->nes_quit = 1;
                return;
        }
    }
}

#if (NES_ENABLE_SOUND == 1)

static SDL_AudioDeviceID nes_audio_device;
#define SDL_AUDIO_NUM_CHANNELS          (1)

int nes_sound_output(uint8_t *buffer, size_t len){
    const uint32_t max_queue_bytes = NES_APU_SAMPLE_PER_SYNC * 4;
    if (SDL_GetQueuedAudioSize(nes_audio_device) > max_queue_bytes){
        SDL_ClearQueuedAudio(nes_audio_device);
    }
    SDL_QueueAudio(nes_audio_device, buffer, (uint32_t)len);
    return 0;
}
#endif

int nes_initex(nes_t *nes){
#if (NES_USE_FS == 1)
    /* Desktop/board frontend: keep the game's battery save on disk. */
    nes->nes_rom.sram_persist = 1;
#endif
    if (SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO|SDL_INIT_JOYSTICK| SDL_INIT_TIMER)) {
        SDL_Log("Can not init video, %s", SDL_GetError());
        return -1;
    }
    window = SDL_CreateWindow(
            NES_NAME,
            SDL_WINDOWPOS_UNDEFINED,
            SDL_WINDOWPOS_UNDEFINED,
            NES_WIDTH * 2, NES_HEIGHT * 2,      // 二倍分辨率
            SDL_WINDOW_SHOWN|SDL_WINDOW_ALLOW_HIGHDPI
    );
    if (window == NULL) {
        SDL_Log("Can not create window, %s", SDL_GetError());
        return -1;
    }
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE);
    framebuffer = SDL_CreateTexture(renderer,
                                    SDL_PIXELFORMAT_ARGB8888,
                                    SDL_TEXTUREACCESS_STREAMING,
                                    NES_WIDTH,
                                    NES_HEIGHT);
#if (NES_ENABLE_SOUND == 1)
    SDL_AudioSpec desired = {
        .freq = NES_APU_SAMPLE_RATE,
        .format = AUDIO_U8,
        .channels = SDL_AUDIO_NUM_CHANNELS,
        .samples = NES_APU_SAMPLE_PER_SYNC,
        .callback = NULL,
        .userdata = nes
    };
    nes_audio_device = SDL_OpenAudioDevice(NULL, SDL_FALSE, &desired, NULL, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (!nes_audio_device) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Couldn't open audio: %s\n", SDL_GetError());
    }
    SDL_PauseAudioDevice(nes_audio_device, SDL_FALSE);
#endif
    return 0;
}

int nes_deinitex(nes_t *nes){
    (void)nes;
    SDL_DestroyTexture(framebuffer);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

int nes_draw(int x1, int y1, int x2, int y2, nes_color_t* color_data){
    if (!framebuffer){
        return -1;
    }
    SDL_Rect rect;
    rect.x = x1;
    rect.y = y1;
    rect.w = x2 - x1 + 1;
    rect.h = y2 - y1 + 1;
    SDL_UpdateTexture(framebuffer, &rect, color_data, rect.w * 4);
    return 0;
}


#if defined(NES_TEST_MODE) && (NES_TEST_MODE == 1) && (NES_USE_FS == 1)
/*
 * Macro isolated test hooks (only compiled into the "nes-test" target, see xmake.lua):
 * drive the emulator from the environment so automated runs can exercise the real frontend
 * without synthetic keystrokes.
 *   NES_TEST_SAVE_AT=<frame>   save a state (same code path as F5)
 *   NES_TEST_LOAD_AT=<frame>   load the state written by a previous run (F8)
 *   NES_TEST_EXIT_AT=<frame>   quit once that frame has been rendered
 *   NES_TEST_HASHLOG=<file>    append "frame hash" per frame (framebuffer FNV-1a)
 *   NES_TEST_KEYS="60:START,90:RIGHT+A,150:"   input script: each entry sets the held buttons
 *                              from that frame on (empty button list = release), so gameplay
 *                              can be reached automatically (e.g. to reproduce in-game bugs).
 */
#include <stdio.h>
#include <stdlib.h>

#define SDL_TEST_KEY_ENTRIES  (64)

/* Button bits of the script (independent of the joypad bitfield layout). */
#define SDL_TEST_BTN_A       (0x01u)
#define SDL_TEST_BTN_B       (0x02u)
#define SDL_TEST_BTN_SELECT  (0x04u)
#define SDL_TEST_BTN_START   (0x08u)
#define SDL_TEST_BTN_UP      (0x10u)
#define SDL_TEST_BTN_DOWN    (0x20u)
#define SDL_TEST_BTN_LEFT    (0x40u)
#define SDL_TEST_BTN_RIGHT   (0x80u)

typedef struct {
    uint32_t frame;
    uint8_t  buttons;
} sdl_test_key_t;

typedef struct {
    int      active;
    uint32_t frame;
    uint32_t save_at;
    uint32_t load_at;
    uint32_t exit_at;
    FILE*    log;
    sdl_test_key_t keys[SDL_TEST_KEY_ENTRIES];
    uint16_t key_count;
} sdl_test_driver_t;

static sdl_test_driver_t sdl_test;

/* "START", "RIGHT+A", "SELECT", ... -> button bitmask */
static uint8_t sdl_test_parse_buttons(const char* text) {
    uint8_t mask = 0;
    while (*text != '\0') {
        if (*text == '+' || *text == ' ') {
            text++;
            continue;
        }
        if ((text[0] == 'A' || text[0] == 'a') && (text[1] == '+' || text[1] == '\0' || text[1] == ' ')) {
            mask |= SDL_TEST_BTN_A;
            text++;
        } else if ((text[0] == 'B' || text[0] == 'b') && (text[1] == '+' || text[1] == '\0' || text[1] == ' ')) {
            mask |= SDL_TEST_BTN_B;
            text++;
        } else if (strncmp(text, "SELECT", 6) == 0) {
            mask |= SDL_TEST_BTN_SELECT;
            text += 6;
        } else if (strncmp(text, "START", 5) == 0) {
            mask |= SDL_TEST_BTN_START;
            text += 5;
        } else if (strncmp(text, "UP", 2) == 0) {
            mask |= SDL_TEST_BTN_UP;
            text += 2;
        } else if (strncmp(text, "DOWN", 4) == 0) {
            mask |= SDL_TEST_BTN_DOWN;
            text += 4;
        } else if (strncmp(text, "LEFT", 4) == 0) {
            mask |= SDL_TEST_BTN_LEFT;
            text += 4;
        } else if (strncmp(text, "RIGHT", 5) == 0) {
            mask |= SDL_TEST_BTN_RIGHT;
            text += 5;
        } else {
            text++;     /* unknown token: skip one character */
        }
    }
    return mask;
}

/* "60:START,90:RIGHT+A,150:" -> one entry per comma separated item */
static void sdl_test_parse_keys(const char* script) {
    const char* cursor = script;
    while (*cursor != '\0' && sdl_test.key_count < SDL_TEST_KEY_ENTRIES) {
        const char* colon = cursor;
        sdl_test_key_t* entry = &sdl_test.keys[sdl_test.key_count];
        while (*colon != '\0' && *colon != ':' && *colon != ',') {
            colon++;
        }
        entry->frame = (*colon == ':') ? (uint32_t)strtoul(cursor, NULL, 10) : 0u;
        entry->buttons = (*colon == ':') ? sdl_test_parse_buttons(colon + 1) : 0u;
        sdl_test.key_count++;
        while (*colon != '\0' && *colon != ',') {
            colon++;
        }
        cursor = (*colon == ',') ? (colon + 1) : colon;
    }
}

static void sdl_test_init(void) {
    const char* value;
    if (sdl_test.active) {
        return;
    }
    sdl_test.active = 1;
    sdl_test.frame = 0;
    if ((value = getenv("NES_TEST_SAVE_AT")) != NULL)  sdl_test.save_at = (uint32_t)atoi(value);
    if ((value = getenv("NES_TEST_LOAD_AT")) != NULL)  sdl_test.load_at = (uint32_t)atoi(value);
    if ((value = getenv("NES_TEST_EXIT_AT")) != NULL)  sdl_test.exit_at = (uint32_t)atoi(value);
    if ((value = getenv("NES_TEST_HASHLOG")) != NULL)  sdl_test.log = fopen(value, "wb");
    if ((value = getenv("NES_TEST_KEYS")) != NULL)     sdl_test_parse_keys(value);
}

/* Held buttons for the current frame: the last entry at or before it wins. */
static void sdl_test_apply_keys(nes_t* nes) {
    uint8_t mask = 0;
    for (uint16_t i = 0; i < sdl_test.key_count; i++) {
        if (sdl_test.keys[i].frame > sdl_test.frame) {
            break;
        }
        mask = sdl_test.keys[i].buttons;
    }
    nes->nes_cpu.joypad.A1  = (mask & SDL_TEST_BTN_A) ? 1u : 0u;
    nes->nes_cpu.joypad.B1  = (mask & SDL_TEST_BTN_B) ? 1u : 0u;
    nes->nes_cpu.joypad.SE1 = (mask & SDL_TEST_BTN_SELECT) ? 1u : 0u;
    nes->nes_cpu.joypad.ST1 = (mask & SDL_TEST_BTN_START) ? 1u : 0u;
    nes->nes_cpu.joypad.U1  = (mask & SDL_TEST_BTN_UP) ? 1u : 0u;
    nes->nes_cpu.joypad.D1  = (mask & SDL_TEST_BTN_DOWN) ? 1u : 0u;
    nes->nes_cpu.joypad.L1  = (mask & SDL_TEST_BTN_LEFT) ? 1u : 0u;
    nes->nes_cpu.joypad.R1  = (mask & SDL_TEST_BTN_RIGHT) ? 1u : 0u;
}

static void sdl_test_tick(nes_t* nes) {
    sdl_test_init();
    sdl_test.frame++;
    if (sdl_test.save_at != 0u && sdl_test.frame == sdl_test.save_at) {
        sdl_save_state(nes);
    }
    if (sdl_test.load_at != 0u && sdl_test.frame == sdl_test.load_at) {
        sdl_load_state(nes);
    }
    if (sdl_test.key_count != 0u) {
        sdl_test_apply_keys(nes);
    }
    if (sdl_test.log != NULL) {
        const uint8_t* data = (const uint8_t*)nes->nes_draw_data;
        uint32_t hash = 2166136261u;
        for (size_t i = 0; i < sizeof(nes->nes_draw_data); i++) {
            hash = (hash ^ data[i]) * 16777619u;
        }
        fprintf(sdl_test.log, "%u %u\n", (unsigned)sdl_test.frame, (unsigned)hash);
        fflush(sdl_test.log);
    }
    if (sdl_test.exit_at != 0u && sdl_test.frame >= sdl_test.exit_at) {
        nes->nes_quit = 1;
    }
}
#endif
#if defined(NES_TEST_MODE) && (NES_TEST_MODE == 1)
uint64_t nes_test_time_us(void) {
    const uint64_t frequency = SDL_GetPerformanceFrequency();
    const uint64_t counter = SDL_GetPerformanceCounter();
    return (counter / frequency) * 1000000u +
           (counter % frequency) * 1000000u / frequency;
}
static void sdl_test_pacing_tick(void) {
    static int initialized;
    static FILE* log;
    static uint64_t previous;
    if (!initialized) {
        const char* path = getenv("NES_TEST_PACINGLOG");
        if (path && *path) log = fopen(path, "w");
        initialized = 1;
    }
    if (log) {
        const uint64_t now = nes_test_time_us();
        if (previous) fprintf(log, "%llu\n", (unsigned long long)(now - previous));
        previous = now;
        fflush(log);
    }
}
#endif
void nes_frame(nes_t* nes){
    const uint64_t freq = SDL_GetPerformanceFrequency();
#if defined(NES_TEST_MODE) && (NES_TEST_MODE == 1) && (NES_USE_FS == 1)
    sdl_test_tick(nes);
#endif
#if (NES_USE_FS == 1)
    if (sdl_title_until != 0u && SDL_GetTicks() > sdl_title_until) {
        sdl_title_until = 0;
        if (window != NULL) {
            SDL_SetWindowTitle(window, NES_NAME);
        }
    }
#endif
    const uint64_t frame_ticks = freq / (uint64_t)nes->timing.frame_rate;

    if (nes_next_frame_tick == 0){
        nes_next_frame_tick = SDL_GetPerformanceCounter();
    }

    SDL_RenderCopy(renderer, framebuffer, NULL, NULL);
    SDL_RenderPresent(renderer);
#if defined(NES_TEST_MODE) && (NES_TEST_MODE == 1)
    sdl_test_pacing_tick();
#endif
    sdl_event(nes);

    nes_next_frame_tick += frame_ticks;
    uint64_t now = SDL_GetPerformanceCounter();
    while (now < nes_next_frame_tick){
        uint32_t delay_ms = (uint32_t)((nes_next_frame_tick - now) * 1000 / freq);
        if (delay_ms > 1){
            SDL_Delay(delay_ms - 1);
        } else {
            /* Yield the final short interval, then check the actual deadline. */
            SDL_Delay(0);
        }
        now = SDL_GetPerformanceCounter();
    }
    if ((now - nes_next_frame_tick) > (frame_ticks * 2)){
        // If we are far behind, resync to avoid long-term drift.
        nes_next_frame_tick = now;
    }
}
