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

#if defined(NES_TEST_PROFILE) && (NES_TEST_PROFILE == 1)
/* Profiling is a separate switch on purpose: it needs a platform clock
 * (nes_test_time_us) and must not be pulled in by the plain NES_TEST_MODE builds
 * (the SDL nes-test target only defines NES_TEST_MODE). */
#include "nes_test.h"
#define NES_PROF_BEGIN(nes_, region_) nes_test_profile_region_begin((nes_), (region_))
#define NES_PROF_END(nes_, region_)   nes_test_profile_region_end((nes_), (region_))
#else
/* Production builds keep the hooks as no-ops: no call, no enum, no branch. */
#define NES_PROF_BEGIN(nes_, region_) ((void)0)
#define NES_PROF_END(nes_, region_)   ((void)0)
#endif

//https://www.nesdev.org/pal.txt

static nes_color_t nes_palette[]={
#if (NES_COLOR_DEPTH == 32) // ARGB8888
    0xFF757575, 0xFF271B8F, 0xFF0000AB, 0xFF47009F, 0xFF8F0077, 0xFFAB0013, 0xFFA70000, 0xFF7F0B00, 0xFF432F00, 0xFF004700, 0xFF005100, 0xFF003F17, 0xFF1B3F5F, 0xFF000000, 0xFF000000, 0xFF000000,
    0xFFBCBCBC, 0xFF0073EF, 0xFF233BEF, 0xFF8300F3, 0xFFBF00BF, 0xFFE7005B, 0xFFDB2B00, 0xFFCB4F0F, 0xFF8B7300, 0xFF009700, 0xFF00AB00, 0xFF00933B, 0xFF00838B, 0xFF000000, 0xFF000000, 0xFF000000,
    0xFFFFFFFF, 0xFF3FBFFF, 0xFF5F97FF, 0xFFA78BFD, 0xFFF77BFF, 0xFFFF77B7, 0xFFFF7763, 0xFFFF9B3B, 0xFFF3BF3F, 0xFF83D313, 0xFF4FDF4B, 0xFF58F898, 0xFF00EBDB, 0xFF000000, 0xFF000000, 0xFF000000,
    0xFFFFFFFF, 0xFFABE7FF, 0xFFC7D7FF, 0xFFD7CBFF, 0xFFFFC7FF, 0xFFFFC7DB, 0xFFFFBFB3, 0xFFFFDBAB, 0xFFFFE7A3, 0xFFE3FFA3, 0xFFABF3BF, 0xFFB3FFCF, 0xFF9FFFF3, 0xFF000000, 0xFF000000, 0xFF000000,
#elif (NES_COLOR_DEPTH == 16)
#if (NES_COLOR_SWAP == 0) // RGB565
    0x73AE, 0x20D1, 0x0015, 0x4013, 0x880E, 0x0802, 0xA000, 0x7840, 0x4160, 0x0220, 0x0280, 0x01E2, 0x19EB, 0x0000, 0x0000, 0x0000,
    0xBDF7, 0x039D, 0x21DD, 0x801E, 0xB817, 0x000B, 0xD940, 0xCA61, 0x8B80, 0x04A0, 0x0540, 0x0487, 0x0411, 0x0000, 0x0000, 0x0000,
    0xFFFF, 0x3DFF, 0x5CBF, 0xA45F, 0xF3DF, 0x0BB6, 0xFBAC, 0xFCC7, 0xF5E7, 0x8682, 0x4EE9, 0x5FD3, 0x075B, 0x0000, 0x0000, 0x0000,
    0xFFFF, 0xAF3F, 0xC6BF, 0xD65F, 0xFE3F, 0x0E3B, 0xFDF6, 0xFED5, 0xFF34, 0xE7F4, 0xAF97, 0xB7F9, 0x9FFE, 0x0000, 0x0000, 0x0000,
#else // RGB565_SWAP
    0xAE73, 0xD120, 0x1500, 0x1340, 0x0E88, 0x0208, 0x00A0, 0x4078, 0x6041, 0x2002, 0x8002, 0xE201, 0xEB19, 0x0000, 0x0000, 0x0000,
    0xF7BD, 0x9D03, 0xDD21, 0x1E80, 0x17B8, 0x0B00, 0x40D9, 0x61CA, 0x808B, 0xA004, 0x4005, 0x8704, 0x1104, 0x0000, 0x0000, 0x0000,
    0xFFFF, 0xFF3D, 0xBF5C, 0x5FA4, 0xDFF3, 0xB60B, 0xACFB, 0xC7FC, 0xE7F5, 0x8286, 0xE94E, 0xD35F, 0x5B07, 0x0000, 0x0000, 0x0000,
    0xFFFF, 0x3FAF, 0xBFC6, 0x5FD6, 0x3FFE, 0x3B0E, 0xF6FD, 0xD5FE, 0x34FF, 0xF4E7, 0x97AF, 0xF9B7, 0xFE9F, 0x0000, 0x0000, 0x0000,
#endif /* NES_COLOR_SWAP */
#endif /* NES_COLOR_DEPTH */
};

#if (NES_VS_SYSTEM == 1)
/* VS. System arcade PPUs (RP2C03 / RP2C04-xxxx) drive RGB outputs from their own fixed palettes,
 * so the same 6-bit colour index looks different from a consumer 2C02 - VS Battle City shows the
 * game's green backdrop as black on the arcade PPU, for instance.  The tables below are the RGB888
 * part of the identical tables in Mesen2 (Core/NES/NesDefaultVideoFilter.cpp, _ppuPaletteArgb),
 * kept in one build-independent form and converted to the build's pixel format in
 * nes_palette_generate(); the index matches Mesen's PpuModel enum and comes from the romdb. */
static const uint32_t nes_vs_palette_rgb[][64] = {
    { /* 1: RP2C03 (PlayChoice-10) */
        0x6D6D6D, 0x002491, 0x0000DA, 0x6D48DA, 0x91006D, 0xB6006D, 0xB62400, 0x914800, 0x6D4800, 0x244800, 0x006D24, 0x009100, 0x004848, 0x000000, 0x000000, 0x000000,
        0xB6B6B6, 0x006DDA, 0x0048FF, 0x9100FF, 0xB600FF, 0xFF0091, 0xFF0000, 0xDA6D00, 0x916D00, 0x249100, 0x009100, 0x00B66D, 0x009191, 0x000000, 0x000000, 0x000000,
        0xFFFFFF, 0x6DB6FF, 0x9191FF, 0xDA6DFF, 0xFF00FF, 0xFF6DFF, 0xFF9100, 0xFFB600, 0xDADA00, 0x6DDA00, 0x00FF00, 0x48FFDA, 0x00FFFF, 0x000000, 0x000000, 0x000000,
        0xFFFFFF, 0xB6DAFF, 0xDAB6FF, 0xFFB6FF, 0xFF91FF, 0xFFB6B6, 0xFFDA91, 0xFFFF48, 0xFFFF6D, 0xB6FF48, 0x91FF6D, 0x48FFDA, 0x91DAFF, 0x000000, 0x000000, 0x000000,
    },
    { /* 2: RP2C04-0001 */
        0xFFB6B6, 0xDA6DFF, 0xFF0000, 0x9191FF, 0x009191, 0x244800, 0x484848, 0xFF0091, 0xFFFFFF, 0x6D6D6D, 0xFFB600, 0xB6006D, 0x91006D, 0xDADA00, 0x6D4800, 0xFFFFFF,
        0x6DB6FF, 0xDAB66D, 0x6D2400, 0x6DDA00, 0x91DAFF, 0xDAB6FF, 0xFFDA91, 0x0048FF, 0xFFDA00, 0x48FFDA, 0x000000, 0x480000, 0xDADADA, 0x919191, 0xFF00FF, 0x002491,
        0x00006D, 0xB6DAFF, 0xFFB6FF, 0x00FF00, 0x00FFFF, 0x004848, 0x00B66D, 0xB600FF, 0x000000, 0x914800, 0xFF91FF, 0xB62400, 0x9100FF, 0x0000DA, 0xFF9100, 0x000000,
        0x000000, 0x249100, 0xB6B6B6, 0x006D24, 0xB6FF48, 0x6D48DA, 0xFFFF00, 0xDA6D00, 0x004800, 0x006DDA, 0x009100, 0x242424, 0xFFFF6D, 0xFF6DFF, 0x916D00, 0x91FF6D,
    },
    { /* 3: RP2C04-0002 */
        0x000000, 0xFFB600, 0x916D00, 0xB6FF48, 0x91FF6D, 0xFF6DFF, 0x009191, 0xB6DAFF, 0xFF0000, 0x9100FF, 0xFFFF6D, 0xFF91FF, 0xFFFFFF, 0xDA6DFF, 0x91DAFF, 0x009100,
        0x004800, 0x6DB6FF, 0xB62400, 0xDADADA, 0x00B66D, 0x6DDA00, 0x480000, 0x9191FF, 0x484848, 0xFF00FF, 0x00006D, 0x48FFDA, 0xDAB6FF, 0x6D4800, 0x000000, 0x6D48DA,
        0x91006D, 0xFFDA91, 0xFF9100, 0xFFB6FF, 0x006DDA, 0x6D2400, 0xB6B6B6, 0x0000DA, 0xB600FF, 0xFFDA00, 0x6D6D6D, 0x244800, 0x0048FF, 0x000000, 0xDADA00, 0xFFFFFF,
        0xDAB66D, 0x242424, 0x00FF00, 0xDA6D00, 0x004848, 0x002491, 0xFF0091, 0x249100, 0x000000, 0x00FFFF, 0x914800, 0xFFFF00, 0xFFB6B6, 0xB6006D, 0x006D24, 0x919191,
    },
    { /* 4: RP2C04-0003 */
        0xB600FF, 0xFF6DFF, 0x91FF6D, 0xB6B6B6, 0x009100, 0xFFFFFF, 0xB6DAFF, 0x244800, 0x002491, 0x000000, 0xFFDA91, 0x6D4800, 0xFF0091, 0xDADADA, 0xDAB66D, 0x91DAFF,
        0x9191FF, 0x009191, 0xB6006D, 0x0048FF, 0x249100, 0x916D00, 0xDA6D00, 0x00B66D, 0x6D6D6D, 0x6D48DA, 0x000000, 0x0000DA, 0xFF0000, 0xB62400, 0xFF91FF, 0xFFB6B6,
        0xDA6DFF, 0x004800, 0x00006D, 0xFFFF00, 0x242424, 0xFFB600, 0xFF9100, 0xFFFFFF, 0x6DDA00, 0x91006D, 0x6DB6FF, 0xFF00FF, 0x006DDA, 0x919191, 0x000000, 0x6D2400,
        0x00FFFF, 0x480000, 0xB6FF48, 0xFFB6FF, 0x914800, 0x00FF00, 0xDADA00, 0x484848, 0x006D24, 0x000000, 0xDAB6FF, 0xFFFF6D, 0x9100FF, 0x48FFDA, 0xFFDA00, 0x004848,
    },
    { /* 5: RP2C04-0004 */
        0x916D00, 0x6D48DA, 0x009191, 0xDADA00, 0x000000, 0xFFB6B6, 0x002491, 0xDA6D00, 0xB6B6B6, 0x6D2400, 0x00FF00, 0x00006D, 0xFFDA91, 0xFFFF00, 0x009100, 0xB6FF48,
        0xFF6DFF, 0x480000, 0x0048FF, 0xFF91FF, 0x000000, 0x484848, 0xB62400, 0xFF9100, 0xDAB66D, 0x00B66D, 0x9191FF, 0x249100, 0x91006D, 0x000000, 0x91FF6D, 0x6DB6FF,
        0xB6006D, 0x006D24, 0x914800, 0x0000DA, 0x9100FF, 0xB600FF, 0x6D6D6D, 0xFF0091, 0x004848, 0xDADADA, 0x006DDA, 0x004800, 0x242424, 0xFFFF6D, 0x919191, 0xFF00FF,
        0xFFB6FF, 0xFFFFFF, 0x6D4800, 0xFF0000, 0xFFDA00, 0x48FFDA, 0xFFFFFF, 0x91DAFF, 0x000000, 0xFFB600, 0xDA6DFF, 0xB6DAFF, 0x6DDA00, 0xDAB6FF, 0x00FFFF, 0x244800,
    },
};

/* RGB888 -> this build's pixel format (the consumer table above is hand tuned per depth, the
 * arcade tables are not, so they share one representation). */
static inline nes_color_t nes_palette_from_rgb(uint32_t rgb) {
#if (NES_COLOR_DEPTH == 32)
    return (nes_color_t)(0xFF000000u | rgb);
#elif (NES_COLOR_DEPTH == 16)
    const uint16_t v = (uint16_t)(((rgb >> 8) & 0xF800u) | ((rgb >> 5) & 0x07E0u) | ((rgb >> 3) & 0x001Fu));
#if (NES_COLOR_SWAP == 0)
    return (nes_color_t)v;
#else
    return (nes_color_t)((uint16_t)((v >> 8) | (v << 8)));
#endif
#else
    (void)rgb;
    return (nes_color_t)0;
#endif
}
#endif /* NES_VS_SYSTEM */

/* PAL: 312 lines/frame at 50Hz, CPU:PPU = 3.2 dots per CPU cycle => 341/3.2 = 106.5625
 * CPU cycles per line (106 + 9/16), 80 cycles in the first chunk (256 dots). */
void nes_timing_set_pal(nes_t* nes) {
    nes->timing.line_clocks       = 106u;
    nes->timing.line_split        = 80u;
    nes->timing.remainder_add     = 9u;
    nes->timing.remainder_mod     = 16u;
    nes->timing.vblank_lines      = 70u;
    nes->timing.frame_rate        = 50u;
    nes->timing.apu_frame_divisor = 78u;
    nes->timing.cpu_clock         = 1662607u;   /* PAL PPU 5.320342MHz / 3.2 */
    nes->timing.samples_per_frame = (uint16_t)(NES_APU_SAMPLE_RATE / 50u);
}

nes_t* nes_init(void){
    nes_t* nes = nes_malloc(sizeof(nes_t));
    if (nes == NULL) { return NULL; }
    nes_memset(nes, 0, sizeof(nes_t));
    /* NTSC timing is the default; a PAL ROM only swaps these numbers (see nes_timing_t). */
    nes->timing.line_clocks       = 113u;
    nes->timing.line_split        = 85u;
    nes->timing.remainder_add     = 2u;
    nes->timing.remainder_mod     = 3u;
    nes->timing.vblank_lines      = 20u;
    nes->timing.frame_rate        = 60u;
    nes->timing.apu_frame_divisor = 66u;
    nes->timing.cpu_clock         = 1789773u;   /* NTSC 21.47727MHz / 12 */
    nes->timing.samples_per_frame = (uint16_t)(NES_APU_SAMPLE_RATE / 60u);
    nes_initex(nes);
    return nes;
}

int nes_deinit(nes_t *nes){
    nes->nes_quit = 1;
    nes_deinitex(nes);
    if (nes){
        nes_free(nes);
        nes = NULL;
    }
    return NES_OK;
}

/* Rebuild the RGB palette cache from palette_indexes[].
 * Called once per frame and, importantly, on every palette RAM write:
 * games change palette RAM mid-frame (raster splits) and the change must be
 * visible from the next rendered scanline on. */
void nes_palette_generate(nes_t* nes){
    /* VS. System boards carry an arcade PPU whose palette differs from the consumer 2C02; the
     * romdb picks it (nes_rom.vs_ppu, index 0 = consumer PPU).  The table choice is made once per
     * call, and this runs once per frame plus on palette RAM writes, so it stays off the pixel
     * path. */
#if (NES_VS_SYSTEM == 1)
    const uint32_t* vs = NULL;
    const uint8_t vs_ppu = nes->nes_rom.vs_ppu;
    if (vs_ppu >= 1u && vs_ppu <= (uint8_t)(sizeof(nes_vs_palette_rgb) / sizeof(nes_vs_palette_rgb[0]))) {
        vs = nes_vs_palette_rgb[vs_ppu - 1u];
    }
#endif
    for (uint8_t i = 0; i < 32; i++) {
        const uint8_t index = nes->nes_ppu.palette_indexes[i];
#if (NES_VS_SYSTEM == 1)
        nes->nes_ppu.palette[i] = (vs != NULL) ? nes_palette_from_rgb(vs[index]) : nes_palette[index];
#else
        nes->nes_ppu.palette[i] = nes_palette[index];
#endif
    }
    for (uint8_t i = 1; i < 8; i++){
        nes->nes_ppu.palette[4 * i] = nes->nes_ppu.palette[0];
    }
}

static inline void nes_mapper_ppu_tile_fetch(nes_t* nes, uint8_t tile, uint16_t address, uint8_t*** pattern_table){
    if (nes->nes_mapper.mapper_ppu &&
        tile >= nes->nes_mapper.mapper_ppu_tile_min &&
        tile <= nes->nes_mapper.mapper_ppu_tile_max) {
        nes->nes_mapper.mapper_ppu(nes, address);
        *pattern_table = nes->nes_ppu.pattern_table;
    }
}

static inline void nes_draw_background_pixel(nes_t* nes, nes_color_t* draw_data, uint8_t x, uint8_t palette_index){
    if (x < 8u && !nes->nes_ppu.MASK_m) {
        nes->nes_ppu.bg_opaque[x] = 0;
        draw_data[x] = nes->nes_ppu.background_palette[0];
    } else {
        nes->nes_ppu.bg_opaque[x] = (palette_index & 0x03u) != 0u;
        draw_data[x] = nes->nes_ppu.background_palette[palette_index];
    }
}

/* MMC5 extended background may address CHR beyond the resident cache.  Both the renderer
 * and its opacity-only twin go through here so the two can never disagree. */
static inline const uint8_t* nes_extended_bg_tile(nes_t* nes, uint16_t ex_bank, uint8_t pattern_id) {
    const uint32_t address = (uint32_t)ex_bank * 4096u + ((uint16_t)pattern_id << 4u);
#if (NES_ROM_STREAM == 1)
    return nes_chrrom_tile(nes, address);
#else
    return nes->nes_rom.chr_rom + address;
#endif
}
/* Full tiles use constant shifts and one clipping decision for eight pixels. */
static inline void nes_draw_background_tile(nes_t* nes, nes_color_t* out,
                                            uint8_t x, uint8_t lo, uint8_t hi,
                                            uint8_t palette) {
    const nes_color_t* colors = nes->nes_ppu.background_palette + palette;
    uint8_t* opaque = nes->nes_ppu.bg_opaque + x;
    out += x;
#define NES_BG_PIXEL(n, shift) do { \
    const uint8_t value = ((lo >> (shift)) & 1u) | (((hi >> (shift)) & 1u) << 1); \
    opaque[n] = value != 0; out[n] = colors[value]; \
} while (0)
    NES_BG_PIXEL(0, 7); NES_BG_PIXEL(1, 6);
    NES_BG_PIXEL(2, 5); NES_BG_PIXEL(3, 4);
    NES_BG_PIXEL(4, 3); NES_BG_PIXEL(5, 2);
    NES_BG_PIXEL(6, 1); NES_BG_PIXEL(7, 0);
#undef NES_BG_PIXEL
    if (x < 8u && !nes->nes_ppu.MASK_m) {
        for (uint8_t i = 0; i < 8u - x; ++i) {
            opaque[i] = 0;
            out[i] = nes->nes_ppu.background_palette[0];
        }
    }
}

static void nes_render_background_line(nes_t* nes,uint16_t scanline,nes_color_t* draw_data){
    (void)scanline;
    uint8_t p = 0;
    int8_t m = 7 - nes->nes_ppu.x;
    const uint8_t dx = (const uint8_t)nes->nes_ppu.v.coarse_x;
    const uint8_t dy = (const uint8_t)nes->nes_ppu.v.fine_y;
    const uint8_t tile_y = (const uint8_t)nes->nes_ppu.v.coarse_y;
    uint8_t nametable_id = (uint8_t)nes->nes_ppu.v.nametable;
    const uint8_t bg_base = nes->nes_ppu.CTRL_B ? 4 : 0;
    // 预计算tile_y相关的偏移量,避免每个tile重复计算
    const uint16_t tile_y_offset = (uint16_t)(tile_y << 5);
    const uint16_t attr_y_offset = (uint16_t)(960 + ((tile_y >> 2) << 3));
    const uint8_t attr_y_shift = (tile_y & 2) << 1;
    uint8_t** pattern_table = nes->nes_ppu.pattern_table;
    const uint8_t* name_table = nes->nes_ppu.name_table[nametable_id];
    /* ExRAM mode 1 (MMC5): per-tile 4KB CHR bank + palette override */
    const uint8_t* exram = nes->nes_mapper.mapper_exram;
    const uint16_t ex_4k_banks = (exram != NULL && nes->nes_rom.chr_rom != NULL && nes->nes_rom.chr_rom_size > 0u) ?
        (uint16_t)(nes->nes_rom.chr_rom_size * 2u) : 0u;
    for (uint8_t tile_x = dx; tile_x < 32; tile_x++){
        const uint8_t pattern_id = name_table[tile_x + tile_y_offset];
        const uint8_t* bit0_p;
        uint8_t high_bit;
        if (ex_4k_banks > 0u) {
            const uint8_t ex_byte = exram[(uint16_t)tile_y * 32u + tile_x];
            /* NESdev MMC5: bits[5:0] = 4KB CHR bank, bits[7:6] = palette */
            const uint16_t ex_bank = (uint16_t)((ex_byte & 0x3Fu) | ((uint16_t)nes->nes_mapper.mapper_chr_hi << 6u)) % ex_4k_banks;
bit0_p = nes_extended_bg_tile(nes, ex_bank, pattern_id);
            high_bit = (uint8_t)(((ex_byte >> 6u) & 0x03u) << 2u);
        } else {
            const uint16_t pattern_address = (uint16_t)((uint16_t)bg_base * 0x400u + (uint16_t)pattern_id * 16u + dy);
            const uint8_t* tile_p = pattern_table[bg_base + (pattern_id >> 6)] + ((pattern_id & 0x3F) << 4);
            const uint8_t attribute = name_table[attr_y_offset + (tile_x >> 2)];
            nes_mapper_ppu_tile_fetch(nes, pattern_id, (uint16_t)(pattern_address + 8u), &pattern_table);
            if (nes->nes_mapper.mapper_ppu) {
                name_table = nes->nes_ppu.name_table[nametable_id];
            }
            bit0_p = tile_p;
            high_bit = ((attribute >> (attr_y_shift | (tile_x & 2))) & 3) << 2;
        }
        const uint8_t bit0 = bit0_p[dy];
        const uint8_t bit1 = bit0_p[dy + 8];
        if (m == 7) {
            nes_draw_background_tile(nes, draw_data, p, bit0, bit1, high_bit);
            p += 8;
        } else for (; m >= 0; m--){
            uint8_t low_bit = ((bit0 >> m) & 0x01) | ((bit1 >> m)<<1 & 0x02);
            nes_draw_background_pixel(nes, draw_data, p, high_bit | low_bit);
            p++;
        }
        m = 7;
    }
    nametable_id ^= 1;
    name_table = nes->nes_ppu.name_table[nametable_id];
    for (uint8_t tile_x = 0; tile_x <= dx; tile_x++){
        const uint8_t pattern_id = name_table[tile_x + tile_y_offset];
        const uint8_t* bit0_p;
        uint8_t high_bit;
        if (ex_4k_banks > 0u) {
            const uint8_t ex_byte = exram[(uint16_t)tile_y * 32u + tile_x];
            /* NESdev MMC5: bits[5:0] = 4KB CHR bank, bits[7:6] = palette */
            const uint16_t ex_bank = (uint16_t)((ex_byte & 0x3Fu) | ((uint16_t)nes->nes_mapper.mapper_chr_hi << 6u)) % ex_4k_banks;
bit0_p = nes_extended_bg_tile(nes, ex_bank, pattern_id);
            high_bit = (uint8_t)(((ex_byte >> 6u) & 0x03u) << 2u);
        } else {
            const uint16_t pattern_address = (uint16_t)((uint16_t)bg_base * 0x400u + (uint16_t)pattern_id * 16u + dy);
            const uint8_t* tile_p = pattern_table[bg_base + (pattern_id >> 6)] + ((pattern_id & 0x3F) << 4);
            const uint8_t attribute = name_table[attr_y_offset + (tile_x >> 2)];
            nes_mapper_ppu_tile_fetch(nes, pattern_id, (uint16_t)(pattern_address + 8u), &pattern_table);
            if (nes->nes_mapper.mapper_ppu) {
                name_table = nes->nes_ppu.name_table[nametable_id];
            }
            bit0_p = tile_p;
            high_bit = ((attribute >> (attr_y_shift | (tile_x & 2))) & 3) << 2;
        }
        const uint8_t bit0 = bit0_p[dy];
        const uint8_t bit1 = bit0_p[dy + 8];
        uint8_t skew = 0;
        if (tile_x == dx){
            if (nes->nes_ppu.x){
                skew = 8 - nes->nes_ppu.x;
            }else
                break;
        }
        if (m == 7 && skew == 0) {
            nes_draw_background_tile(nes, draw_data, p, bit0, bit1, high_bit);
            p += 8;
        } else for (; m >= skew; m--){
            const uint8_t low_bit = ((bit0 >> m) & 0x01) | ((bit1 >> m)<<1 & 0x02);
            nes_draw_background_pixel(nes, draw_data, p, high_bit | low_bit);
            p++;
        }
        m = 7;
    }
}

#if (NES_FRAME_SKIP != 0)
/* Opacity-only twin of nes_render_background_line().
 *
 * A skipped frame does not draw pixels, but sprite 0 hit is decided from the
 * background/sprite overlap, so the per-pixel opacity map still has to describe
 * the frame being emulated.  Carrying the previous frame's map over makes raster
 * effects fire (or miss) one frame late, so the tile walk below repeats the same
 * fetches as the renderer while leaving palette lookups and pixel stores out.
 * Keep it in sync with nes_render_background_line().
 * https://www.nesdev.org/wiki/PPU_OAM#Sprite_zero_hits */
static void nes_render_background_opacity_line(nes_t* nes){
    uint8_t p = 0;
    int8_t m = 7 - nes->nes_ppu.x;
    const uint8_t dx = (const uint8_t)nes->nes_ppu.v.coarse_x;
    const uint8_t dy = (const uint8_t)nes->nes_ppu.v.fine_y;
    const uint8_t tile_y = (const uint8_t)nes->nes_ppu.v.coarse_y;
    uint8_t nametable_id = (uint8_t)nes->nes_ppu.v.nametable;
    const uint8_t bg_base = nes->nes_ppu.CTRL_B ? 4 : 0;
    const uint16_t tile_y_offset = (uint16_t)(tile_y << 5);
    const uint16_t attr_y_offset = (uint16_t)(960 + ((tile_y >> 2) << 3));
    const uint8_t attr_y_shift = (tile_y & 2) << 1;
    uint8_t** pattern_table = nes->nes_ppu.pattern_table;
    const uint8_t* name_table = nes->nes_ppu.name_table[nametable_id];
    const uint8_t* exram = nes->nes_mapper.mapper_exram;
    const uint16_t ex_4k_banks = (exram != NULL && nes->nes_rom.chr_rom != NULL && nes->nes_rom.chr_rom_size > 0u) ?
        (uint16_t)(nes->nes_rom.chr_rom_size * 2u) : 0u;
    for (uint8_t tile_x = dx; tile_x < 32; tile_x++){
        const uint8_t pattern_id = name_table[tile_x + tile_y_offset];
        const uint8_t* bit0_p;
        uint8_t high_bit;
        if (ex_4k_banks > 0u) {
            const uint8_t ex_byte = exram[(uint16_t)tile_y * 32u + tile_x];
            const uint16_t ex_bank = (uint16_t)((ex_byte & 0x3Fu) | ((uint16_t)nes->nes_mapper.mapper_chr_hi << 6u)) % ex_4k_banks;
bit0_p = nes_extended_bg_tile(nes, ex_bank, pattern_id);
            high_bit = (uint8_t)(((ex_byte >> 6u) & 0x03u) << 2u);
        } else {
            const uint16_t pattern_address = (uint16_t)((uint16_t)bg_base * 0x400u + (uint16_t)pattern_id * 16u + dy);
            const uint8_t* tile_p = pattern_table[bg_base + (pattern_id >> 6)] + ((pattern_id & 0x3F) << 4);
            const uint8_t attribute = name_table[attr_y_offset + (tile_x >> 2)];
            nes_mapper_ppu_tile_fetch(nes, pattern_id, (uint16_t)(pattern_address + 8u), &pattern_table);
            if (nes->nes_mapper.mapper_ppu) {
                name_table = nes->nes_ppu.name_table[nametable_id];
            }
            bit0_p = tile_p;
            high_bit = ((attribute >> (attr_y_shift | (tile_x & 2))) & 3) << 2;
        }
        const uint8_t bit0 = bit0_p[dy];
        const uint8_t bit1 = bit0_p[dy + 8];
        for (; m >= 0; m--){
            const uint8_t low_bit = ((bit0 >> m) & 0x01) | ((bit1 >> m)<<1 & 0x02);
            nes->nes_ppu.bg_opaque[p] = (p < 8u && !nes->nes_ppu.MASK_m) ? 0u : (uint8_t)((high_bit | low_bit) & 0x03u);
            p++;
        }
        m = 7;
    }
    nametable_id ^= 1;
    name_table = nes->nes_ppu.name_table[nametable_id];
    for (uint8_t tile_x = 0; tile_x <= dx; tile_x++){
        const uint8_t pattern_id = name_table[tile_x + tile_y_offset];
        const uint8_t* bit0_p;
        uint8_t high_bit;
        if (ex_4k_banks > 0u) {
            const uint8_t ex_byte = exram[(uint16_t)tile_y * 32u + tile_x];
            const uint16_t ex_bank = (uint16_t)((ex_byte & 0x3Fu) | ((uint16_t)nes->nes_mapper.mapper_chr_hi << 6u)) % ex_4k_banks;
bit0_p = nes_extended_bg_tile(nes, ex_bank, pattern_id);
            high_bit = (uint8_t)(((ex_byte >> 6u) & 0x03u) << 2u);
        } else {
            const uint16_t pattern_address = (uint16_t)((uint16_t)bg_base * 0x400u + (uint16_t)pattern_id * 16u + dy);
            const uint8_t* tile_p = pattern_table[bg_base + (pattern_id >> 6)] + ((pattern_id & 0x3F) << 4);
            const uint8_t attribute = name_table[attr_y_offset + (tile_x >> 2)];
            nes_mapper_ppu_tile_fetch(nes, pattern_id, (uint16_t)(pattern_address + 8u), &pattern_table);
            if (nes->nes_mapper.mapper_ppu) {
                name_table = nes->nes_ppu.name_table[nametable_id];
            }
            bit0_p = tile_p;
            high_bit = ((attribute >> (attr_y_shift | (tile_x & 2))) & 3) << 2;
        }
        const uint8_t bit0 = bit0_p[dy];
        const uint8_t bit1 = bit0_p[dy + 8];
        uint8_t skew = 0;
        if (tile_x == dx){
            if (nes->nes_ppu.x){
                skew = 8 - nes->nes_ppu.x;
            }else
                break;
        }
        for (; m >= skew; m--){
            const uint8_t low_bit = ((bit0 >> m) & 0x01) | ((bit1 >> m)<<1 & 0x02);
            nes->nes_ppu.bg_opaque[p] = (p < 8u && !nes->nes_ppu.MASK_m) ? 0u : (uint8_t)((high_bit | low_bit) & 0x03u);
            p++;
        }
        m = 7;
    }
}
#endif /* NES_FRAME_SKIP */

typedef struct {
    uint8_t sprite_id;
    sprite_info_t sprite_info;
    uint8_t bit0;
    uint8_t bit1;
} sprite_line_entry_t;

typedef struct {
    uint8_t sprite_numbers;
    sprite_line_entry_t sprite[8];
} sprite_line_t;

static void nes_prepare_sprite_line(nes_t* nes,uint16_t scanline,sprite_line_t* sprite_line){
    const sprite_info_t* sprite_info_arr = nes->nes_ppu.sprite_info;
    uint8_t** pattern_table = nes->nes_ppu.pattern_table;
    const uint8_t sprite_size = nes->nes_ppu.CTRL_H?16:8;
    sprite_line->sprite_numbers = 0;

    for (uint8_t i = 0; i < 64; i++){
        if (sprite_info_arr[i].y >= 0xEF){
            continue;
        }
        const sprite_info_t sprite_info = sprite_info_arr[i];
        const uint8_t sprite_y = (uint8_t)(sprite_info.y + 1);
        if (scanline < sprite_y || scanline >= sprite_y + sprite_size){
            continue;
        }
        if (sprite_line->sprite_numbers == 8){
            nes->nes_ppu.STATUS_O = 1;
            break;
        }
        const uint8_t spr_base = nes->nes_ppu.CTRL_H ? ((sprite_info.pattern_8x16) ? 4 : 0) : (nes->nes_ppu.CTRL_S ? 4 : 0);
        const uint8_t spr_tile = nes->nes_ppu.CTRL_H ? (uint8_t)(sprite_info.tile_index_8x16 << 1) : sprite_info.tile_index_number;
        uint8_t spr_pattern = spr_tile;
        uint8_t dy = (uint8_t)(scanline - sprite_y);

        if (nes->nes_ppu.CTRL_H){
            if (sprite_info.flip_v){
                if (dy < 8){
                    spr_pattern++;
                    dy = sprite_size - dy - 1 -8;
                }else{
                    dy = sprite_size - dy - 1;
                }
            }else{
                if (dy > 7){
                    spr_pattern++;
                    dy-=8;
                }
            }
        }else{
            if (sprite_info.flip_v){
                dy = sprite_size - dy - 1;
            }
        }

        const uint16_t pattern_address = (uint16_t)((uint16_t)spr_base * 0x400u + (uint16_t)spr_pattern * 16u + dy);
        const uint8_t* sprite_bit0_p = pattern_table[spr_base + (spr_pattern >> 6)] + ((spr_pattern & 0x3F) << 4);
        sprite_line_entry_t* entry = &sprite_line->sprite[sprite_line->sprite_numbers++];
        entry->sprite_id = i;
        entry->sprite_info = sprite_info;
        entry->bit0 = sprite_bit0_p[dy];
        entry->bit1 = sprite_bit0_p[dy + 8];
        nes_mapper_ppu_tile_fetch(nes, spr_pattern, (uint16_t)(pattern_address + 8u), &pattern_table);
    }
}

static void nes_render_sprite_line(nes_t* nes,const sprite_line_t* sprite_line,nes_color_t* draw_data){
    const nes_color_t* spr_pal = nes->nes_ppu.sprite_palette;
    const sprite_info_t* sprite_info_arr = nes->nes_ppu.sprite_info;
    // 显示精灵
    for (uint8_t sprite_number = sprite_line->sprite_numbers; sprite_number > 0; sprite_number--){
        const sprite_line_entry_t* entry = &sprite_line->sprite[sprite_number - 1];
        const uint8_t sprite_id = entry->sprite_id;
        const sprite_info_t sprite_info = entry->sprite_info;
        const uint8_t sprite_bit0 = entry->bit0;
        const uint8_t sprite_bit1 = entry->bit1;
        // 完全透明的精灵行,跳过绘制
        const uint8_t sprite_date = sprite_bit0 | sprite_bit1;
        if (sprite_date == 0) {
            // 精灵0命中不需要检测(无可见像素)
            continue;
        }
#if (NES_FRAME_SKIP != 0)
        if(nes->nes_frame_skip_count == 0)
#endif
        {
            uint8_t p = sprite_info.x;
            const uint8_t spr_pal_base = sprite_info.sprite_palette << 2;
            if (sprite_info.flip_h){
                for (int8_t m = 0; m <= 7; m++){
                    const uint8_t low_bit = ((sprite_bit0 >> m) & 0x01) | ((sprite_bit1 >> m)<<1 & 0x02);
                    if (low_bit && (p >= 8u || nes->nes_ppu.MASK_M)){
                        const uint8_t palette_index = spr_pal_base | low_bit;
                        if (sprite_info.priority){
                            if (!nes->nes_ppu.bg_opaque[p]){
                                draw_data[p] = spr_pal[palette_index];
                            }
                        }else{
                            draw_data[p] = spr_pal[palette_index];
                        }
                    }
                    if (p == 255)
                        break;
                    p++;
                }
            }else{
                for (int8_t m = 7; m >= 0; m--){
                    const uint8_t low_bit = ((sprite_bit0 >> m) & 0x01) | ((sprite_bit1 >> m)<<1 & 0x02);
                    if (low_bit && (p >= 8u || nes->nes_ppu.MASK_M)){
                        const uint8_t palette_index = spr_pal_base | low_bit;
                        if (sprite_info.priority){
                            if (!nes->nes_ppu.bg_opaque[p]){
                                draw_data[p] = spr_pal[palette_index];
                            }
                        }else{
                            draw_data[p] = spr_pal[palette_index];
                        }
                    }
                    if (p == 255)
                        break;
                    p++;
                }
            }
        }
        // 检测精灵0命中：硬件按背景/精灵不透明像素判断，而不是按最终颜色判断。
        if (sprite_id == 0){
            if (nes->nes_ppu.MASK_b && nes->nes_ppu.STATUS_S == 0){
                uint8_t px = sprite_info_arr[0].x;
                if (sprite_info.flip_h){
                    for (int8_t m = 0; m <= 7; m++){
                        if (px == 255) break;
                        if ((sprite_date >> m) & 1){
                            if ((px >= 8u || (nes->nes_ppu.MASK_m && nes->nes_ppu.MASK_M)) && nes->nes_ppu.bg_opaque[px]){
                                nes->nes_ppu.STATUS_S = 1;
                                break;
                            }
                        }
                        px++;
                    }
                } else {
                    for (int8_t m = 7; m >= 0; m--){
                        if (px == 255) break;
                        if ((sprite_date >> m) & 1){
                            if ((px >= 8u || (nes->nes_ppu.MASK_m && nes->nes_ppu.MASK_M)) && nes->nes_ppu.bg_opaque[px]){
                                nes->nes_ppu.STATUS_S = 1;
                                break;
                            }
                        }
                        px++;
                    }
                }
            }
        }
        
    }
}

// https://www.nesdev.org/wiki/PPU_rendering


// static void nes_background_pattern_test(nes_t* nes){
//     nes_palette_generate(nes);
//     nes_memset(nes->nes_draw_data, nes->nes_ppu.background_palette[0], sizeof(nes_color_t) * NES_DRAW_SIZE);

//     uint8_t nametable_id = 0;
//     for (uint8_t j = 0; j < 16 * 8; j++){
//         uint16_t p = j*NES_WIDTH;
//         uint8_t tile_y = j/8;
//         uint8_t dy = j%8;
//         int8_t m = 7;
//         for (uint8_t i = 0; i < 16; i++){
//             uint8_t tile_x = i;
//             const uint8_t pattern_id = tile_y*16 + tile_x;
//             const uint8_t* bit0_p = nes->nes_ppu.pattern_table[1 ? 4 : 0] + pattern_id * 16;
//             const uint8_t* bit1_p = bit0_p + 8;
//             const uint8_t bit0 = bit0_p[dy];
//             const uint8_t bit1 = bit1_p[dy];
//             const uint8_t attribute = nes->nes_ppu.name_table[nametable_id][960 + ((tile_y >> 2) << 3) + (tile_x >> 2)];
//             const uint8_t high_bit = ((attribute >> (((tile_y & 2) << 1) | (tile_x & 2))) & 3) << 2;
//             for (; m >= 0; m--){
//                 uint8_t low_bit = ((bit0 >> m) & 0x01) | ((bit1 >> m)<<1 & 0x02);
//                 uint8_t palette_index = (high_bit & 0x0c) | low_bit;
//                 nes->nes_draw_data[p++] = nes->nes_ppu.background_palette[palette_index];
//             }
//             m = 7;
//         }
//     }
//     nes_draw(0, 0, NES_WIDTH-1, NES_HEIGHT-1, nes->nes_draw_data);
//     nes_frame(nes);
// }

void nes_run(nes_t* nes){
    NES_LOG_DEBUG("mapper:%03d\n",nes->nes_rom.mapper_number);
    NES_LOG_DEBUG("prg_rom_size:%d*16kB\n",nes->nes_rom.prg_rom_size);
    NES_LOG_DEBUG("chr_rom_size:%d*8kB\n",nes->nes_rom.chr_rom_size);
    NES_LOG_DEBUG("mirroring_type:%d\n",nes->nes_rom.mirroring_type);
    NES_LOG_DEBUG("four_screen:%d\n",nes->nes_rom.four_screen);
    // NES_LOG_DEBUG("save_ram:%d\n",nes->nes_rom.save_ram);

    nes_cpu_reset(nes);
    /* One PPU scanline is 341 dots, which is not a whole number of CPU cycles:
     * NTSC 341/3 = 113 + 2/3, PAL 341/3.2 = 106 + 9/16.  The fractional part is
     * accumulated so every remainder_mod-th line emits one extra cycle.  All the
     * numbers come from nes->timing, so PAL visible lines use the PAL period too
     * (before this they were hard coded to the NTSC 113/114 pattern). */
    uint8_t dot_remainder = 0;

    while(!nes->nes_quit){
        NES_PROF_BEGIN(nes, NES_PROF_FRAME);
#if (NES_FRAME_SKIP != 0)
        if(nes->nes_frame_skip_count == 0)
#endif
        {
            nes_palette_generate(nes);
        }
        if (nes->nes_ppu.MASK_b == 0){
#if (NES_FRAME_SKIP != 0)
            if(nes->nes_frame_skip_count == 0)
#endif
            {
                nes_memset(nes->nes_draw_data, nes->nes_ppu.background_palette[0], sizeof(nes_color_t) * NES_DRAW_SIZE);
            }
        }
#if (NES_ENABLE_SOUND==1)
        NES_PROF_BEGIN(nes, NES_PROF_APU);
        nes_apu_frame(nes);
        NES_PROF_END(nes, NES_PROF_APU);
#endif
        /* The PPU clears the sprite 0 hit and sprite overflow flags at dot 1 of the pre-render
           scanline, so every frame starts with both flags clear.  Games use them as raster timing
           markers: VS Platoon's title screen polls $2002 bit 6 ("LDA $2002 / AND #$40 / BEQ ...")
           and a flag that is set once and never cleared makes that wait return immediately, so the
           PPU writes land at the wrong time and the screen never finishes drawing. */
        nes->nes_ppu.STATUS_S = 0;
        nes->nes_ppu.STATUS_O = 0;
        // https://www.nesdev.org/wiki/PPU_rendering#Visible_scanlines_(0-239)
        for(nes->scanline = 0; nes->scanline < NES_HEIGHT; nes->scanline++) { // 0-239 Visible frame
            uint16_t scanline_ticks = nes->timing.line_clocks;
            sprite_line_t sprite_line = {0};
            dot_remainder += nes->timing.remainder_add;
            if (dot_remainder >= nes->timing.remainder_mod) { dot_remainder -= nes->timing.remainder_mod; scanline_ticks++; }
            if (nes->nes_ppu.MASK_s){
                NES_PROF_BEGIN(nes, NES_PROF_SPRITE);
                nes_prepare_sprite_line(nes, nes->scanline, &sprite_line);
                NES_PROF_END(nes, NES_PROF_SPRITE);
            }
            if (nes->nes_ppu.MASK_b){
                if (nes->nes_mapper.mapper_render_screen)
                    nes->nes_mapper.mapper_render_screen(nes, 1);
                NES_PROF_BEGIN(nes, NES_PROF_BG);
#if (NES_FRAME_SKIP != 0)
                if (nes->nes_frame_skip_count == 0)
#endif
                {
#if (NES_RAM_LACK == 1)
                nes_render_background_line(nes, nes->scanline, nes->nes_draw_data + nes->scanline%(NES_HEIGHT/2) * NES_WIDTH);
#else
                nes_render_background_line(nes, nes->scanline, nes->nes_draw_data + nes->scanline * NES_WIDTH);
#endif
                }
#if (NES_FRAME_SKIP != 0)
                else {
                    /* Sprite 0 hit has to see this frame's background, so a skipped
                     * frame still refreshes the opacity map (no pixels, no palettes). */
                    nes_render_background_opacity_line(nes);
                }
#endif
                NES_PROF_END(nes, NES_PROF_BG);
            } else {
#if (NES_FRAME_SKIP != 0)
                if (nes->nes_frame_skip_count == 0)
#endif
                {
#if (NES_RAM_LACK == 1)
                nes_color_t* line_data = nes->nes_draw_data + nes->scanline%(NES_HEIGHT/2) * NES_WIDTH;
#else
                nes_color_t* line_data = nes->nes_draw_data + nes->scanline * NES_WIDTH;
#endif
                for (uint16_t x = 0; x < NES_WIDTH; x++) {
                    line_data[x] = nes->nes_ppu.background_palette[0];
                }
                nes_memset(nes->nes_ppu.bg_opaque, 0, sizeof(nes->nes_ppu.bg_opaque));
                }
            }
            if (nes->nes_ppu.MASK_s){
                if (nes->nes_mapper.mapper_render_screen)
                    nes->nes_mapper.mapper_render_screen(nes, 0);
                NES_PROF_BEGIN(nes, NES_PROF_SPRITE);
#if (NES_RAM_LACK == 1)
                nes_render_sprite_line(nes, &sprite_line,nes->nes_draw_data + nes->scanline%(NES_HEIGHT/2) * NES_WIDTH);
#else
                nes_render_sprite_line(nes, &sprite_line,nes->nes_draw_data + nes->scanline * NES_WIDTH);
#endif
                NES_PROF_END(nes, NES_PROF_SPRITE);
            }
            nes_opcode(nes,nes->timing.line_split); // ppu cycles: 85*3=255 (NTSC)
            // https://www.nesdev.org/wiki/PPU_scrolling#Wrapping_around
            if (nes->nes_ppu.MASK_b || nes->nes_ppu.MASK_s){
                // Rendering resets OAMADDR during sprite evaluation; at line granularity,
                // keep it with the existing dot-256/257 scroll update.
                nes->nes_ppu.oam_addr = 0;
                // https://www.nesdev.org/wiki/PPU_scrolling#At_dot_256_of_each_scanline
                if ((nes->nes_ppu.v.fine_y) < 7) {
                    nes->nes_ppu.v.fine_y++;
                }else {
                    nes->nes_ppu.v.fine_y = 0;
                    uint8_t y = (uint8_t)(nes->nes_ppu.v.coarse_y);
                    if (y == 29) {
                        y = 0;
                        nes->nes_ppu.v_reg ^= 0x0800;
                    }else if (y == 31) {
                        y = 0;
                    }else {
                        y++;
                    }
                    nes->nes_ppu.v.coarse_y = y;
                }
                // https://www.nesdev.org/wiki/PPU_scrolling#At_dot_257_of_each_scanline
                // v: ....A.. ...BCDEF <- t: ....A.. ...BCDEF
                nes->nes_ppu.v_reg = (nes->nes_ppu.v_reg & (uint16_t)0xFBE0) | (nes->nes_ppu.t_reg & (uint16_t)0x041F);
            }
            if (nes->nes_mapper.mapper_hsync) {
                nes->nes_mapper.mapper_hsync(nes);
            }
            nes_opcode(nes,scanline_ticks-nes->timing.line_split);
#if (NES_ENABLE_SOUND==1)
            if ((uint16_t)nes->scanline % nes->timing.apu_frame_divisor == (uint16_t)(nes->timing.apu_frame_divisor - 1u)) {
                NES_PROF_BEGIN(nes, NES_PROF_APU);
                nes_apu_frame(nes);
                NES_PROF_END(nes, NES_PROF_APU);
            }
#endif
#if (NES_RAM_LACK == 1)
#if (NES_FRAME_SKIP != 0)
            if(nes->nes_frame_skip_count == 0)
#endif
            {
                if (nes->scanline == NES_HEIGHT/2-1){
                    NES_PROF_BEGIN(nes, NES_PROF_DRAW);
                    nes_draw(0, 0, NES_WIDTH-1, NES_HEIGHT/2-1, nes->nes_draw_data);
                    NES_PROF_END(nes, NES_PROF_DRAW);
                }else if(nes->scanline == NES_HEIGHT-1){
                    NES_PROF_BEGIN(nes, NES_PROF_DRAW);
                    nes_draw(0, NES_HEIGHT/2, NES_WIDTH-1, NES_HEIGHT-1, nes->nes_draw_data);
                    NES_PROF_END(nes, NES_PROF_DRAW);
                }
            }
#endif
        }
#if (NES_RAM_LACK == 0)
#if (NES_FRAME_SKIP != 0)
        if(nes->nes_frame_skip_count == 0)
#endif
        {
            NES_PROF_BEGIN(nes, NES_PROF_DRAW);
            nes_draw(0, 0, NES_WIDTH-1, NES_HEIGHT-1, nes->nes_draw_data);
            NES_PROF_END(nes, NES_PROF_DRAW);
        }
#endif
        {
            uint16_t scanline_ticks = nes->timing.line_clocks;
            dot_remainder += nes->timing.remainder_add;
            if (dot_remainder >= nes->timing.remainder_mod) { dot_remainder -= nes->timing.remainder_mod; scanline_ticks++; }
            nes_opcode(nes,scanline_ticks); //240 Post-render line
        }
        
        nes->nes_ppu.STATUS_V = 1;// Set VBlank flag (241 line)
        if (nes->nes_mapper.mapper_vsync) {
            nes->nes_mapper.mapper_vsync(nes);
        }
        if (nes->nes_ppu.CTRL_V) {
            nes->nes_cpu.irq_nmi=1;
        }

        for(uint8_t i = 0; i < nes->timing.vblank_lines; i++){ // 241-260行 垂直空白行 x20 (NTSC)
            uint16_t scanline_ticks = nes->timing.line_clocks;
            dot_remainder += nes->timing.remainder_add;
            if (dot_remainder >= nes->timing.remainder_mod) { dot_remainder -= nes->timing.remainder_mod; scanline_ticks++; }
            nes_opcode(nes,scanline_ticks);
        }
        nes->nes_ppu.ppu_status = 0;    // Clear:VBlank,Sprite 0,Overflow
        {
            uint16_t scanline_ticks = nes->timing.line_clocks;
            dot_remainder += nes->timing.remainder_add;
            if (dot_remainder >= nes->timing.remainder_mod) { dot_remainder -= nes->timing.remainder_mod; scanline_ticks++; }
            nes_opcode(nes,scanline_ticks); // Pre-render scanline (-1 or 261)
        }

        if (nes->nes_ppu.MASK_b || nes->nes_ppu.MASK_s){
            // Dot 257 copies horizontal scroll bits, then dots 280-304 copy vertical bits.
            // At line granularity this is equivalent to syncing the full VRAM address.
            // https://www.nesdev.org/wiki/PPU_scrolling#During_dots_280_to_304_of_the_pre-render_scanline_(end_of_vblank)
            nes->nes_ppu.v_reg = nes->nes_ppu.t_reg;
        }
        NES_PROF_END(nes, NES_PROF_FRAME);
        nes_frame(nes);
#if (NES_FRAME_SKIP != 0)
        if ( ++nes->nes_frame_skip_count > NES_FRAME_SKIP){
            nes->nes_frame_skip_count = 0;
        }
#endif
    }
}

