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
/*
 * PPU tests: registers, scroll latch, palette mirroring, CHR write protection,
 * nametable mirroring and a deterministic sprite-0 scene.
 */
#include "test.h"

static int ppu_fixture(test_fixture_t* f, uint16_t chr_units, uint8_t mirroring, uint8_t four_screen) {
    test_rom_spec_t spec;
    memset(&spec, 0, sizeof(spec));
    spec.mapper = 0;
    spec.prg_units = 2;
    spec.chr_units = chr_units;
    spec.mirroring = mirroring;
    spec.four_screen = four_screen;
    spec.fill = TEST_ROM_FILL_STUB;
    return test_fixture_make(f, &spec);
}

static void ppu_set_vram_address(nes_t* nes, uint16_t address) {
    nes_write_ppu_register(nes, 0x2006, (uint8_t)(address >> 8));
    nes_write_ppu_register(nes, 0x2006, (uint8_t)(address & 0xFF));
}

/* ---------------------------------------------------------------- */

int test_ppu_registers(void) {
    test_fixture_t f;
    TEST_CHECK(ppu_fixture(&f, 1, 0, 0));
    nes_t* nes = f.nes;

    nes_write_ppu_register(nes, 0x2000, 0x80);
    TEST_EQ_U32(0x80, nes->nes_ppu.ppu_ctrl);
    nes_write_ppu_register(nes, 0x2000, 0x83);
    TEST_EQ_U32(3, nes->nes_ppu.t.nametable);       /* D0-D1 -> t.nametable */

    nes_write_ppu_register(nes, 0x2001, 0x1E);
    TEST_EQ_U32(0x1E, nes->nes_ppu.ppu_mask);

    /* $2005 first write: coarse X and fine X; second: fine Y and coarse Y. */
    nes_write_ppu_register(nes, 0x2005, 0x5A);      /* 0101 1010 -> coarse_x=11, x=2 */
    TEST_EQ_U32(11, nes->nes_ppu.t.coarse_x);
    TEST_EQ_U32(2, nes->nes_ppu.x);
    TEST_EQ_U32(1, nes->nes_ppu.w);
    nes_write_ppu_register(nes, 0x2005, 0x2D);      /* 0010 1101 -> coarse_y=5, fine_y=5 */
    TEST_EQ_U32(5, nes->nes_ppu.t.coarse_y);
    TEST_EQ_U32(5, nes->nes_ppu.t.fine_y);
    TEST_EQ_U32(0, nes->nes_ppu.w);

    /* $2006 copies t into v on the second write. */
    ppu_set_vram_address(nes, 0x23C0);
    TEST_EQ_U32(0x23C0, nes->nes_ppu.t_reg);
    TEST_EQ_U32(0x23C0, nes->nes_ppu.v_reg);
    TEST_EQ_U32(0, nes->nes_ppu.w);

    /* $2007 writes into VRAM (nametable 0 lives in ppu_vram[0]). */
    nes_write_ppu_register(nes, 0x2007, 0x5A);
    TEST_EQ_U32(0x5A, nes->nes_ppu.ppu_vram[0][0x3C0]);
    TEST_EQ_U32(0x23C1, nes->nes_ppu.v_reg);

    /* $2007 reads are buffered: the first read returns the previous buffer. */
    ppu_set_vram_address(nes, 0x2001);
    nes->nes_ppu.ppu_vram[0][1] = 0x77;
    TEST_EQ_U32(0x00, nes_read_ppu_register(nes, 0x2007));
    TEST_EQ_U32(0x77, nes_read_ppu_register(nes, 0x2007));

    /* CTRL_I selects a 32 byte step. */
    nes_write_ppu_register(nes, 0x2000, 0x04);
    ppu_set_vram_address(nes, 0x2000);
    nes_write_ppu_register(nes, 0x2007, 0x11);
    TEST_EQ_U32(0x2020, nes->nes_ppu.v_reg);

    /* OAM access */
    nes_write_ppu_register(nes, 0x2003, 0x12);
    TEST_EQ_U32(0x12, nes->nes_ppu.oam_addr);
    nes_write_ppu_register(nes, 0x2004, 0xAB);
    TEST_EQ_U32(0xAB, nes->nes_ppu.oam_data[0x12]);
    TEST_EQ_U32(0x13, nes->nes_ppu.oam_addr);
    TEST_EQ_U32(nes->nes_ppu.oam_data[0x13], nes_read_ppu_register(nes, 0x2004));
    TEST_EQ_U32(0x13, nes->nes_ppu.oam_addr);       /* reads do not advance */

    /* Reading $2002 clears VBlank and the write toggle. */
    nes->nes_ppu.STATUS_V = 1;
    nes->nes_ppu.w = 1;
    TEST_CHECK((nes_read_ppu_register(nes, 0x2002) & 0x80) != 0);
    TEST_EQ_U32(0, nes->nes_ppu.STATUS_V);
    TEST_EQ_U32(0, nes->nes_ppu.w);

    test_fixture_free(&f);
    return TEST_PASS;
}

int test_ppu_scroll(void) {
    test_fixture_t f;
    TEST_CHECK(ppu_fixture(&f, 1, 0, 0));
    nes_t* nes = f.nes;

    /* $2005 and $2006 share the w latch. */
    nes_write_ppu_register(nes, 0x2005, 0x00);      /* w: 0 -> 1 */
    TEST_EQ_U32(1, nes->nes_ppu.w);
    nes_write_ppu_register(nes, 0x2006, 0x24);      /* w=1: low byte of t and v */
    TEST_EQ_U32(0x0024, nes->nes_ppu.t_reg);
    TEST_EQ_U32(0x0024, nes->nes_ppu.v_reg);
    TEST_EQ_U32(0, nes->nes_ppu.w);

    /* High byte only keeps the low 6 bits and clears bit 14. */
    ppu_set_vram_address(nes, 0x2FFF);
    TEST_EQ_U32(0x2FFF, nes->nes_ppu.t_reg);

    /* v is untouched by $2005 writes. */
    const uint16_t v_before = nes->nes_ppu.v_reg;
    nes_write_ppu_register(nes, 0x2005, 0x40);
    nes_write_ppu_register(nes, 0x2005, 0x80);
    TEST_EQ_U32(v_before, nes->nes_ppu.v_reg);

    /* Nametable selection follows PPUCTRL D0-D1. */
    nes_write_ppu_register(nes, 0x2000, 0x02);
    TEST_EQ_U32(2, nes->nes_ppu.t.nametable);

    /* Rendering enabled: the pre-render line copies t into v. */
    nes_write_ppu_register(nes, 0x2001, 0x1E);
    ppu_set_vram_address(nes, 0x2000);
    TEST_CHECK(nes_test_run_frames(nes, 1) == NES_OK);
    TEST_EQ_U32(nes->nes_ppu.t_reg, nes->nes_ppu.v_reg);

    test_fixture_free(&f);
    return TEST_PASS;
}

int test_ppu_palette(void) {
    test_fixture_t f;
    TEST_CHECK(ppu_fixture(&f, 1, 0, 0));
    nes_t* nes = f.nes;

    /* Writes are masked to 6 bits. */
    ppu_set_vram_address(nes, 0x3F00);
    nes_write_ppu_register(nes, 0x2007, 0xFF);
    TEST_EQ_U32(0x3F, nes->nes_ppu.palette_indexes[0x00]);

    /* Only every fourth (backdrop) entry mirrors between the halves. */
    ppu_set_vram_address(nes, 0x3F10);
    nes_write_ppu_register(nes, 0x2007, 0x21);
    TEST_EQ_U32(0x21, nes->nes_ppu.palette_indexes[0x10]);
    TEST_EQ_U32(0x21, nes->nes_ppu.palette_indexes[0x00]);

    ppu_set_vram_address(nes, 0x3F04);
    nes_write_ppu_register(nes, 0x2007, 0x15);
    TEST_EQ_U32(0x15, nes->nes_ppu.palette_indexes[0x04]);
    TEST_EQ_U32(0x15, nes->nes_ppu.palette_indexes[0x14]);

    ppu_set_vram_address(nes, 0x3F0C);
    nes_write_ppu_register(nes, 0x2007, 0x2A);
    TEST_EQ_U32(0x2A, nes->nes_ppu.palette_indexes[0x0C]);
    TEST_EQ_U32(0x2A, nes->nes_ppu.palette_indexes[0x1C]);

    /* A non-backdrop sprite entry stays independent. */
    ppu_set_vram_address(nes, 0x3F11);
    nes_write_ppu_register(nes, 0x2007, 0x30);
    TEST_EQ_U32(0x30, nes->nes_ppu.palette_indexes[0x11]);
    TEST_CHECK(nes->nes_ppu.palette_indexes[0x01] != 0x30);

    /* Reads mirror the same way (no buffering for palette addresses). */
    ppu_set_vram_address(nes, 0x3F00);
    nes_write_ppu_register(nes, 0x2007, 0x0F);
    ppu_set_vram_address(nes, 0x3F10);
    TEST_EQ_U32(0x0F, nes_read_ppu_register(nes, 0x2007));
    ppu_set_vram_address(nes, 0x3F11);
    TEST_EQ_U32(0x30, nes_read_ppu_register(nes, 0x2007));

    test_fixture_free(&f);
    return TEST_PASS;
}

int test_ppu_chr_protection(void) {
    /* CHR-ROM board: pattern writes must be dropped. */
    test_fixture_t f;
    TEST_CHECK(ppu_fixture(&f, 1, 0, 0));
    nes_t* nes = f.nes;
    const uint8_t original = nes->nes_rom.chr_rom[0];
    ppu_set_vram_address(nes, 0x0000);
    nes_write_ppu_register(nes, 0x2007, 0xAB);
    TEST_EQ_U32(original, nes->nes_rom.chr_rom[0]);
    TEST_EQ_U32(original, nes->nes_ppu.pattern_table[0][0]);

    /* A nametable slot redirected at CHR-ROM (Namco 163) is protected too. */
    nes->nes_ppu.pattern_table[4] = nes->nes_rom.chr_rom;
    ppu_set_vram_address(nes, 0x1000);
    nes_write_ppu_register(nes, 0x2007, 0xCD);
    TEST_EQ_U32(original, nes->nes_rom.chr_rom[0]);
    test_fixture_free(&f);

    /* CHR-RAM board: the same write lands in the RAM backing store. */
    test_fixture_t ram;
    TEST_CHECK(ppu_fixture(&ram, 0, 0, 0));
    TEST_CHECK(ram.nes->nes_rom.chr_rom_size == 0);
    ppu_set_vram_address(ram.nes, 0x0000);
    nes_write_ppu_register(ram.nes, 0x2007, 0xAB);
    TEST_EQ_U32(0xAB, ram.nes->nes_ppu.chr_banks[0][0]);
    ppu_set_vram_address(ram.nes, 0x1C00);
    nes_write_ppu_register(ram.nes, 0x2007, 0x5C);
    TEST_EQ_U32(0x5C, ram.nes->nes_ppu.chr_banks[7][0x000]);
    test_fixture_free(&ram);
    return TEST_PASS;
}

int test_ppu_mirroring(void) {
    test_fixture_t f;
    TEST_CHECK(ppu_fixture(&f, 1, 0, 0));
    nes_t* nes = f.nes;

    nes_ppu_screen_mirrors(nes, NES_MIRROR_HORIZONTAL);
    TEST_EQ_PTR(nes->nes_ppu.name_table[0], nes->nes_ppu.name_table[1]);
    TEST_EQ_PTR(nes->nes_ppu.name_table[2], nes->nes_ppu.name_table[3]);
    TEST_CHECK(nes->nes_ppu.name_table[0] != nes->nes_ppu.name_table[2]);

    nes_ppu_screen_mirrors(nes, NES_MIRROR_VERTICAL);
    TEST_EQ_PTR(nes->nes_ppu.name_table[0], nes->nes_ppu.name_table[2]);
    TEST_EQ_PTR(nes->nes_ppu.name_table[1], nes->nes_ppu.name_table[3]);
    TEST_CHECK(nes->nes_ppu.name_table[0] != nes->nes_ppu.name_table[1]);

    nes_ppu_screen_mirrors(nes, NES_MIRROR_ONE_SCREEN0);
    TEST_EQ_PTR(nes->nes_ppu.ppu_vram[0], nes->nes_ppu.name_table[0]);
    TEST_EQ_PTR(nes->nes_ppu.name_table[0], nes->nes_ppu.name_table[3]);

    nes_ppu_screen_mirrors(nes, NES_MIRROR_ONE_SCREEN1);
    TEST_EQ_PTR(nes->nes_ppu.ppu_vram[1], nes->nes_ppu.name_table[0]);
    TEST_EQ_PTR(nes->nes_ppu.name_table[0], nes->nes_ppu.name_table[2]);

    nes_ppu_screen_mirrors(nes, NES_MIRROR_FOUR_SCREEN);
    TEST_CHECK(nes->nes_ppu.name_table[0] != nes->nes_ppu.name_table[1]);
    TEST_CHECK(nes->nes_ppu.name_table[1] != nes->nes_ppu.name_table[2]);
    TEST_CHECK(nes->nes_ppu.name_table[2] != nes->nes_ppu.name_table[3]);

    /* Writing through a mirrored name table lands in the shared VRAM page. */
    nes_ppu_screen_mirrors(nes, NES_MIRROR_HORIZONTAL);
    nes->nes_ppu.name_table[1][5] = 0x99;
    TEST_EQ_U32(0x99, nes->nes_ppu.ppu_vram[0][5]);
    test_fixture_free(&f);

    /* NES_MIRROR_AUTO follows the iNES header, including four-screen. */
    test_fixture_t vertical;
    TEST_CHECK(ppu_fixture(&vertical, 1, 1, 0));
    TEST_EQ_PTR(vertical.nes->nes_ppu.name_table[0], vertical.nes->nes_ppu.name_table[2]);
    test_fixture_free(&vertical);

    test_fixture_t four;
    TEST_CHECK(ppu_fixture(&four, 1, 0, 1));
    TEST_CHECK(four.nes->nes_ppu.name_table[0] != four.nes->nes_ppu.name_table[1]);
    test_fixture_free(&four);
    return TEST_PASS;
}

/* ---------------------------------------------------------------- */
/* Deterministic sprite-0 scene                                      */
/* ---------------------------------------------------------------- */

/*
 * Program installed at $8000 (PRG offset 0):
 *   - set background palette ($3F00-$3F03) and sprite palette ($3F10-$3F13)
 *   - point t at $0000 so the pre-render line reloads v to the top-left scroll
 *     position (v bits 12-14 are fine Y, not the nametable offset)
 *   - enable background + sprites ($2001 = $1E)
 *   - poll $2002 bit 6 (sprite 0 hit); store 1 at $0010 when seen,
 *     otherwise spin in a timeout loop with $0010 left at 0.
 * $3F02 = $22 and $3F12 = $17 make background and sprite pixels distinguishable.
 */
static const uint8_t ppu_scene_program[] = {
    0xA9, 0x3F, 0x8D, 0x06, 0x20,       /* LDA #$3F; STA $2006 */
    0xA9, 0x00, 0x8D, 0x06, 0x20,       /* LDA #$00; STA $2006 -> v=$3F00 */
    0xA9, 0x0F, 0x8D, 0x07, 0x20,       /* $3F00 = $0F */
    0xA9, 0x21, 0x8D, 0x07, 0x20,       /* $3F01 = $21 */
    0xA9, 0x22, 0x8D, 0x07, 0x20,       /* $3F02 = $22 */
    0xA9, 0x27, 0x8D, 0x07, 0x20,       /* $3F03 = $27 */
    0xA9, 0x3F, 0x8D, 0x06, 0x20,       /* LDA #$3F; STA $2006 */
    0xA9, 0x10, 0x8D, 0x06, 0x20,       /* LDA #$10; STA $2006 -> v=$3F10 */
    0xA9, 0x0F, 0x8D, 0x07, 0x20,       /* $3F10 = $0F (mirrors $3F00) */
    0xA9, 0x16, 0x8D, 0x07, 0x20,       /* $3F11 = $16 */
    0xA9, 0x17, 0x8D, 0x07, 0x20,       /* $3F12 = $17 */
    0xA9, 0x30, 0x8D, 0x07, 0x20,       /* $3F13 = $30 */
    0xA9, 0x00, 0x8D, 0x06, 0x20,       /* LDA #$00; STA $2006 */
    0xA9, 0x00, 0x8D, 0x06, 0x20,       /* LDA #$00; STA $2006 -> v=$0000 (top-left scroll) */
    0xA9, 0x1E, 0x8D, 0x01, 0x20,       /* LDA #$1E; STA $2001 */
    0xA2, 0x00,                         /* LDX #$00 */
    0xAD, 0x02, 0x20,                   /* wait: LDA $2002 */
    0x29, 0x40,                         /* AND #$40 */
    0xD0, 0x08,                         /* BNE hit */
    0xE8,                               /* INX */
    0xE0, 0x00,                         /* CPX #$00 */
    0xD0, 0xF4,                         /* BNE wait */
    0x4C, 0x59, 0x80,                   /* JMP $8059 (timeout spin) */
    0xA9, 0x01,                         /* hit: LDA #$01 */
    0x8D, 0x10, 0x00,                   /* STA $0010 */
    0x4C, 0x61, 0x80                    /* JMP $8061 (spin) */
};

static int ppu_scene_fixture(test_fixture_t* f) {
    if (!ppu_fixture(f, 1, 0, 0)) return 0;
    memcpy(f->rom + 16, ppu_scene_program, sizeof(ppu_scene_program));
    /* OAM sprite 0: y=0 (scanline 1), tile 0, attributes, x=8. */
    nes_t* nes = f->nes;
    nes_memset(nes->nes_ppu.oam_data, 0xF8, NES_PPU_OAM_SIZE);   /* park every sprite off-screen */
    nes->nes_ppu.oam_data[0] = 0x00;
    nes->nes_ppu.oam_data[1] = 0x00;
    nes->nes_ppu.oam_data[2] = 0x00;
    nes->nes_ppu.oam_data[3] = 0x08;
    return 1;
}

int test_ppu_sprite0(void) {
    /* Sprite 0 sits at screen x=8, scanline 1 (dy=0 -> opaque at x=11,12).
     * The background uses tile 1 there, also opaque, so the hit fires. */
    const size_t probe = 256u + 12u;

    /* Reference frame: sprite 0 parked off-screen, background only. */
    test_fixture_t bg;
    TEST_CHECK(ppu_scene_fixture(&bg));
    bg.nes->nes_ppu.oam_data[0] = 0xF8;
    TEST_CHECK(nes_test_run_frames(bg.nes, 2) == NES_OK);
    TEST_EQ_U32(0, bg.nes->nes_cpu.cpu_ram[0x10]);          /* no hit */
    const nes_color_t bg_pixel = bg.nes->nes_draw_data[probe];
    const nes_color_t bg_backdrop = bg.nes->nes_draw_data[0];
    TEST_CHECK(bg_pixel != bg_backdrop);                    /* the scene really draws */
    TEST_EQ_U32(bg.nes->nes_ppu.background_palette[2], bg_pixel);
    test_fixture_free(&bg);

    /* Sprite 0 in front: hit is signalled and the sprite colour wins. */
    test_fixture_t front;
    TEST_CHECK(ppu_scene_fixture(&front));
    TEST_CHECK(nes_test_run_frames(front.nes, 2) == NES_OK);
    TEST_EQ_U32(1, front.nes->nes_cpu.cpu_ram[0x10]);
    const nes_color_t front_pixel = front.nes->nes_draw_data[probe];
    TEST_CHECK(front_pixel != bg_pixel);
    TEST_EQ_U32(front.nes->nes_ppu.sprite_palette[2], front_pixel);
    test_fixture_free(&front);

    /* Sprite 0 behind an opaque background pixel: hit still set, background kept. */
    test_fixture_t behind;
    TEST_CHECK(ppu_scene_fixture(&behind));
    behind.nes->nes_ppu.oam_data[2] = 0x20;                 /* priority = behind */
    TEST_CHECK(nes_test_run_frames(behind.nes, 2) == NES_OK);
    TEST_EQ_U32(1, behind.nes->nes_cpu.cpu_ram[0x10]);
    TEST_EQ_U32(bg_pixel, behind.nes->nes_draw_data[probe]);
    test_fixture_free(&behind);

    return TEST_PASS;
}

int test_ppu_render(void) {
    test_fixture_t f;
    TEST_CHECK(ppu_scene_fixture(&f));
    TEST_CHECK(nes_test_run_frames(f.nes, 2) == NES_OK);
    TEST_CHECK(nes_test_frame_hash(f.nes) != 0);
    TEST_CHECK(f.nes->nes_ppu.pattern_table[0] != NULL);
    TEST_CHECK(f.nes->nes_ppu.pattern_table[7] != NULL);

    /* Row 1 must contain more than one colour: the pattern table gives it a
     * non-backdrop pixel at x=12 while x=0 stays backdrop. */
    const nes_color_t row_first = f.nes->nes_draw_data[256];
    int distinct = 0;
    for (size_t x = 1; x < 256; ++x) {
        if (f.nes->nes_draw_data[256 + x] != row_first) { distinct = 1; break; }
    }
    TEST_CHECK(distinct);

    /* Two identical scenes must render identically (deterministic core).
     * Note: nes_test_run_frames() resets the CPU on entry, so a "continue the
     * previous frame" comparison is not possible - both sides start fresh with
     * the same frame budget instead. */
    const uint32_t first_hash = nes_test_frame_hash(f.nes);
    test_fixture_free(&f);

    test_fixture_t g;
    TEST_CHECK(ppu_scene_fixture(&g));
    TEST_CHECK(nes_test_run_frames(g.nes, 2) == NES_OK);
    TEST_EQ_U32(first_hash, nes_test_frame_hash(g.nes));
    test_fixture_free(&g);
    return TEST_PASS;
}
