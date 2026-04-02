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
#endif

static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;
static SDL_Texture *framebuffer = NULL;
static uint64_t nes_next_frame_tick = 0;

static void sdl_event(nes_t *nes) {
    SDL_Event event;
    while (SDL_PollEvent(&event)){
        switch (event.type) {
            case SDL_KEYDOWN:
                switch (event.key.keysym.scancode){
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
                nes_deinit(nes);
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

void nes_frame(nes_t* nes){
    const uint64_t freq = SDL_GetPerformanceFrequency();
    const uint64_t frame_ticks = freq / 60;

    if (nes_next_frame_tick == 0){
        nes_next_frame_tick = SDL_GetPerformanceCounter();
    }

    SDL_RenderCopy(renderer, framebuffer, NULL, NULL);
    SDL_RenderPresent(renderer);
    sdl_event(nes);

    nes_next_frame_tick += frame_ticks;
    uint64_t now = SDL_GetPerformanceCounter();
    if (now < nes_next_frame_tick){
        uint32_t delay_ms = (uint32_t)((nes_next_frame_tick - now) * 1000 / freq);
        if (delay_ms > 0){
            SDL_Delay(delay_ms);
        }
    }else if ((now - nes_next_frame_tick) > (frame_ticks * 2)){
        // If we are far behind, resync to avoid long-term drift.
        nes_next_frame_tick = now;
    }
}
