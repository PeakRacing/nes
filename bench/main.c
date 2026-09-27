/*
 * Headless benchmark frontend.  This is intentionally a platform port rather
 * than a test: all instrumentation lives behind NES_TEST_MODE in the core.
 */
#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include "nes.h"
#include "nes_test.h"

#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#include <windows.h>
#endif

#define BENCH_FNV_OFFSET 2166136261u
#define BENCH_FNV_PRIME  16777619u

static uint64_t bench_draw_checksum = BENCH_FNV_OFFSET;
static uint64_t bench_sound_checksum = BENCH_FNV_OFFSET;
static int bench_full_hash;

static void bench_hash_sample(uint64_t* checksum, const uint8_t* data, size_t size) {
    if (data == NULL || size == 0u) return;
    if (bench_full_hash) {
        for (size_t i = 0; i < size; ++i) {
            *checksum ^= data[i];
            *checksum *= BENCH_FNV_PRIME;
        }
        return;
    }
    *checksum ^= data[0];
    *checksum *= BENCH_FNV_PRIME;
    *checksum ^= data[size - 1u];
    *checksum *= BENCH_FNV_PRIME;
    *checksum ^= (uint64_t)size;
    *checksum *= BENCH_FNV_PRIME;
}

/* ------------------------------------------------------------------------- */
/* Platform hooks                                                            */
/* ------------------------------------------------------------------------- */

#if defined(_MSC_VER)
typedef __declspec(align(16)) union { size_t size; char padding[16]; } bench_alloc_header;
#else
typedef union { max_align_t alignment; size_t size; } bench_alloc_header;
#endif
static size_t bench_heap_live, bench_heap_peak;
void* nes_malloc(int num) {
    if (num < 0) return NULL;
    bench_alloc_header* block = malloc(sizeof(*block) + (size_t)num);
    if (!block) return NULL;
    block->size = (size_t)num;
    bench_heap_live += block->size;
    if (bench_heap_live > bench_heap_peak) bench_heap_peak = bench_heap_live;
    return block + 1;
}
void nes_free(void* address) {
    if (!address) return;
    bench_alloc_header* block = (bench_alloc_header*)address - 1;
    bench_heap_live -= block->size;
    free(block);
}
void* nes_memcpy(void* dst, const void* src, size_t n) { return memcpy(dst, src, n); }
void* nes_memset(void* dst, int c, size_t n) { return memset(dst, c, n); }
int nes_memcmp(const void* a, const void* b, size_t n) { return memcmp(a, b, n); }

FILE* nes_fopen(const char* name, const char* mode) { return fopen(name, mode); }
size_t nes_fread(void* ptr, size_t size, size_t count, FILE* file) {
    return fread(ptr, size, count, file);
}
size_t nes_fwrite(const void* ptr, size_t size, size_t count, FILE* file) {
    return fwrite(ptr, size, count, file);
}
int nes_fseek(FILE* file, long offset, int origin) { return fseek(file, offset, origin); }
long nes_ftell(FILE* file) { return ftell(file); }
int nes_fclose(FILE* file) { return fclose(file); }
int nes_remove(const char* name) { return remove(name); }

int nes_draw(int x1, int y1, int x2, int y2, nes_color_t* color_data) {
    const size_t width = (x2 >= x1) ? (size_t)(x2 - x1 + 1) : 0u;
    const size_t height = (y2 >= y1) ? (size_t)(y2 - y1 + 1) : 0u;
    bench_hash_sample(&bench_draw_checksum, (const uint8_t*)color_data,
                      width * height * sizeof(*color_data));
    return 0;
}

int nes_sound_output(uint8_t* buffer, size_t len) {
    bench_hash_sample(&bench_sound_checksum, buffer, len);
    return 0;
}

int nes_initex(nes_t* nes) { (void)nes; return NES_OK; }
int nes_deinitex(nes_t* nes) { (void)nes; return NES_OK; }

void nes_frame(nes_t* nes) { nes_test_frame_tick(nes); }

int nes_log_printf(const char* format, ...) {
    (void)format;
    return 0;
}

uint64_t nes_test_time_us(void) {
#if defined(_WIN32)
    static uint64_t frequency;
    LARGE_INTEGER counter;
    if (frequency == 0u) {
        LARGE_INTEGER queried_frequency;
        if (!QueryPerformanceFrequency(&queried_frequency) || queried_frequency.QuadPart <= 0) {
            return 1u;
        }
        frequency = (uint64_t)queried_frequency.QuadPart;
    }
    if (!QueryPerformanceCounter(&counter)) return 1u;
    return ((uint64_t)counter.QuadPart / frequency) * 1000000u +
           (((uint64_t)counter.QuadPart % frequency) * 1000000u / frequency);
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 1u;
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)(ts.tv_nsec / 1000u);
#endif
}

/* ------------------------------------------------------------------------- */
/* Reporting                                                                 */
/* ------------------------------------------------------------------------- */

static void bench_usage(const char* program) {
    fprintf(stderr, "usage,%s <rom.nes> [--frames N] [--regions 0|1] [--hash 0|1]\n", program);
}

static int bench_parse_u32(const char* text, uint32_t* value) {
    char* end = NULL;
    unsigned long parsed;
    if (text == NULL || *text == '\0' || value == NULL) return 0;
    parsed = strtoul(text, &end, 10);
    if (*end != '\0' || parsed > UINT32_MAX) return 0;
    *value = (uint32_t)parsed;
    return 1;
}

static const char* bench_region_name(nes_test_profile_region_t region) {
    static const char* const names[NES_PROF_COUNT] = {
        "frame", "cpu", "bg", "sprite", "apu", "draw"
    };
    return (region >= 0 && region < NES_PROF_COUNT) ? names[region] : "unknown";
}

static void bench_report(const nes_t* nes, int hash_enabled) {
    const nes_test_profile_t* profile = nes_test_profile_get();
    const uint64_t draw_bytes = (uint64_t)NES_DRAW_SIZE * sizeof(nes_color_t);
    uint64_t prg_bytes;
    uint64_t chr_bytes;
    uint64_t total_bytes;
    const uint64_t total_us = profile->frame_us;
    const uint64_t frames = profile->frames;
    const double avg_us = frames ? (double)total_us / (double)frames : 0.0;
    const double fps = total_us ? ((double)frames * 1000000.0) / (double)total_us : 0.0;
    const uint32_t frame_hash = hash_enabled ? nes_test_frame_hash(nes) : 0u;

#if (NES_ROM_STREAM == 1)
    prg_bytes = (uint64_t)NES_PRG_CACHE_SLOTS * 8192u;
    chr_bytes = (uint64_t)NES_CHR_CACHE_SLOTS * 1024u;
#else
    prg_bytes = (uint64_t)nes->nes_rom.prg_rom_size * PRG_ROM_UNIT_SIZE;
    chr_bytes = (uint64_t)nes->nes_rom.chr_rom_size * CHR_ROM_UNIT_SIZE;
#endif
    /* nes_draw_data is embedded in nes_t; do not count it twice. */
    total_bytes = (uint64_t)sizeof(nes_t) + prg_bytes + chr_bytes;

    printf("build,color_depth=%d,ram_lack=%d,stream=%d,frameskip=%d,sram=%d,sound=%d,regions_enabled=%d\n",
           NES_COLOR_DEPTH, NES_RAM_LACK, NES_ROM_STREAM, NES_FRAME_SKIP,
           NES_USE_SRAM, NES_ENABLE_SOUND, nes_test_profile_regions_enabled());
    printf("ram,nes_t_bytes=%zu,draw_bytes=%" PRIu64 ",prg_bytes=%" PRIu64
           ",chr_bytes=%" PRIu64 ",total_bytes=%" PRIu64 "\n",
           sizeof(nes_t), draw_bytes, prg_bytes, chr_bytes, total_bytes);
    printf("frame,frames=%" PRIu64 ",total_us=%" PRIu64 ",avg_us=%.3f,max_us=%" PRIu64
           ",fps=%.3f\n",
           frames, total_us, avg_us, profile->frame_max_us, fps);
    printf("heap,live_bytes=%zu,peak_bytes=%zu\n", bench_heap_live, bench_heap_peak);
#if (NES_ROM_STREAM == 1)
    printf("cache,prg_slots=%d,chr_slots=%d\n", NES_PRG_CACHE_SLOTS, NES_CHR_CACHE_SLOTS);
#endif
    for (int region = NES_PROF_CPU; region < NES_PROF_COUNT; ++region) {
        const double percent = total_us ?
            ((double)profile->region_us[region] * 100.0) / (double)total_us : 0.0;
        printf("region,name=%s,us=%" PRIu64 ",calls=%" PRIu32 ",pct_of_frame_total=%.3f\n",
               bench_region_name((nes_test_profile_region_t)region),
               profile->region_us[region], profile->region_calls[region], percent);
    }
    printf("stream,prg_hit=%" PRIu32 ",prg_miss=%" PRIu32
           ",chr_hit=%" PRIu32 ",chr_miss=%" PRIu32 "\n",
           profile->stream_prg_hit, profile->stream_prg_miss,
           profile->stream_chr_hit, profile->stream_chr_miss);
    printf("verify,frame_hash=%" PRIu32 ",draw_checksum=%" PRIu64
           ",sound_checksum=%" PRIu64 "\n",
           frame_hash, bench_draw_checksum, bench_sound_checksum);
}

int main(int argc, char** argv) {
    const char* rom_path;
    uint32_t frames = 300u;
    uint32_t regions = 1u;
    uint32_t hash_enabled = 0u;
    nes_t* nes;

    if (argc < 2) {
        bench_usage(argv[0]);
        return 2;
    }
    rom_path = argv[1];
    for (int i = 2; i < argc; ++i) {
        uint32_t value;
        if ((strcmp(argv[i], "--frames") == 0 || strcmp(argv[i], "--regions") == 0 ||
             strcmp(argv[i], "--hash") == 0) && i + 1 < argc) {
            const char* option = argv[i++];
            if (!bench_parse_u32(argv[i], &value)) {
                bench_usage(argv[0]);
                return 2;
            }
            if (strcmp(option, "--frames") == 0) {
                if (value == 0u) {
                    bench_usage(argv[0]);
                    return 2;
                }
                frames = value;
            } else if (strcmp(option, "--regions") == 0) {
                if (value > 1u) {
                    bench_usage(argv[0]);
                    return 2;
                }
                regions = value;
            } else {
                if (value > 1u) {
                    bench_usage(argv[0]);
                    return 2;
                }
                hash_enabled = value;
            }
        } else {
            bench_usage(argv[0]);
            return 2;
        }
    }

    bench_draw_checksum = BENCH_FNV_OFFSET;
    bench_sound_checksum = BENCH_FNV_OFFSET;
    nes = nes_init();
    if (nes == NULL) {
        fprintf(stderr, "error,stage=init,reason=allocation_failed\n");
        return 1;
    }
    if (nes_load_file(nes, rom_path) != NES_OK) {
        fprintf(stderr, "error,stage=load_file,path=%s,reason=invalid_or_unsupported_rom\n", rom_path);
        nes_deinit(nes);
        return 1;
    }

    bench_full_hash = (int)hash_enabled;
    nes_test_profile_regions((int)regions);
    nes_test_profile_begin(nes);
    if (nes_test_run_frames(nes, frames) != NES_OK) {
        fprintf(stderr, "error,stage=run_frames,reason=core_failed\n");
        nes_unload_file(nes);
        nes_deinit(nes);
        return 1;
    }
    bench_report(nes, (int)hash_enabled);
    nes_unload_file(nes);
    nes_deinit(nes);
    return 0;
}
