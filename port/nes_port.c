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

/* memory */
void *nes_malloc(int num){
    return malloc(num);
}

void nes_free(void *address){
    free(address);
}

void *nes_memcpy(void *str1, const void *str2, size_t n){
    return memcpy(str1, str2, n);
}

void *nes_memset(void *str, int c, size_t n){
    return memset(str,c,n);
}

int nes_memcmp(const void *str1, const void *str2, size_t n){
    return memcmp(str1,str2,n);
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

int nes_log_printf(const char *format, ...){
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);

    size_t len;
    va_list args;
    if (luat_log_level_cur > level) return;
    char log_printf_buff[LOGLOG_SIZE]  = {0};
    va_start(args, format);
    len = vsnprintf_(log_printf_buff, LOGLOG_SIZE - 2, format, args);
    va_end(args);
    if (len > 0) {
        log_printf_buff[len] = '\n';
        luat_log_write(log_printf_buff, len + 1);
    }

}

#if (NES_ENABLE_SOUND == 1)

int nes_sound_output(uint8_t *buffer, size_t len){
    return 0;
}
#endif

int nes_initex(nes_t *nes){
#if (NES_USE_FS == 1)
    /* Desktop/board frontend: keep the game's battery save on disk. */
    nes->nes_rom.sram_persist = 1;
#endif
    return 0;
}

int nes_deinitex(nes_t *nes){
    return 0;
}

int nes_draw(int x1, int y1, int x2, int y2, nes_color_t* color_data){
    return 0;
}

#define FRAMES_PER_SECOND   1000/60

void nes_frame(nes_t* nes){
}


