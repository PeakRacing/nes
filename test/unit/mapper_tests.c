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
 * Mapper tests.
 *
 * Two layers:
 *   1. bank helper semantics (wrap around, CHR-RAM identity mapping);
 *   2. a synthetic smoke pass over every mapper number in the iNES plane
 *      (0-255) that needs no ROM files at all: each supported mapper is loaded,
 *      stormed with writes and run for a couple of frames.
 */
#include "test.h"

#define MAPPER_ADDR_COUNT 10
static const uint16_t mapper_storm_addrs[MAPPER_ADDR_COUNT] = {
    0x8000, 0x8001, 0xA000, 0xA001, 0xC000, 0xC001, 0xE000, 0xE001, 0x6000, 0x6001
};

static int mapper_supported_cache[256];
static int mapper_supported_ready;

static int mapper_is_supported(int mapper) {
    if (!mapper_supported_ready) {
        for (int i = 0; i < 256; ++i) mapper_supported_cache[i] = test_mapper_supported((uint16_t)i);
        mapper_supported_ready = 1;
    }
    return mapper_supported_cache[mapper];
}

static void mapper_fill_spec(test_rom_spec_t* spec, uint16_t mapper, uint16_t prg_units, uint16_t chr_units) {
    memset(spec, 0, sizeof(*spec));
    spec->mapper = mapper;
    spec->prg_units = prg_units;
    spec->chr_units = chr_units;
    spec->save = 1;
    spec->fill = TEST_ROM_FILL_STUB;
}

static int mapper_report(const char* what, uint16_t mapper, const char* expected, const char* actual) {
    char label[64];
    snprintf(label, sizeof(label), "mapper %u: %s", (unsigned)mapper, what);
    test_record_failure(__FILE__, __LINE__, label, expected, actual);
    return TEST_FAIL;
}

int test_mapper_dispatch(void) {
    test_rom_spec_t spec;
    mapper_fill_spec(&spec, 0, 2, 1);
    test_fixture_t f;
    TEST_CHECK(test_fixture_make(&f, &spec));
    TEST_CHECK(f.nes->nes_mapper.mapper_init != NULL);
    TEST_CHECK(f.nes->nes_mapper.mapper_write != NULL);
    TEST_EQ_U32(0, f.nes->nes_rom.mapper_number);
    TEST_CHECK(nes_test_bank_check(f.nes) == NES_OK);
    test_fixture_free(&f);
    return TEST_PASS;
}

int test_mapper_bank_helpers(void) {
    test_rom_spec_t spec;
    mapper_fill_spec(&spec, 0, 2, 1);
    test_fixture_t f;
    TEST_CHECK(test_fixture_make(&f, &spec));
    nes_t* nes = f.nes;
    uint8_t* const prg = nes->nes_rom.prg_rom;
    uint8_t* const chr = nes->nes_rom.chr_rom;

    /* PRG banks are 8KB; 2 x 16KB units means 4 banks. */
    TEST_EQ_U32(4, nes->nes_rom.prg_rom_size * 2u);
    nes_load_prgrom_8k(nes, 0, 3);
    TEST_EQ_PTR(prg + 3 * 8192, nes->nes_cpu.prg_banks[0]);
    /* Out of range values wrap instead of running off the buffer. */
    nes_load_prgrom_8k(nes, 1, 4);
    TEST_EQ_PTR(prg, nes->nes_cpu.prg_banks[1]);
    nes_load_prgrom_8k(nes, 2, 100);
    TEST_EQ_PTR(prg, nes->nes_cpu.prg_banks[2]);

    nes_load_prgrom_16k(nes, 0, 1);
    TEST_EQ_PTR(prg + 2 * 8192, nes->nes_cpu.prg_banks[0]);
    TEST_EQ_PTR(prg + 3 * 8192, nes->nes_cpu.prg_banks[1]);

    nes_load_prgrom_32k(nes, 0, 0);
    for (int i = 0; i < 4; ++i) {
        if (nes->nes_cpu.prg_banks[i] != prg + (size_t)i * 8192) {
            test_fixture_free(&f);
            return mapper_report("32KB PRG load", 0, "sequential banks", "mismatch");
        }
    }

    /* CHR: 1 x 8KB unit means 8 x 1KB banks. */
    nes_load_chrrom_1k(nes, 0, 5);
    TEST_EQ_PTR(chr + 5 * 1024, nes->nes_ppu.pattern_table[0]);
    nes_load_chrrom_1k(nes, 1, 9);          /* wraps to bank 1 */
    TEST_EQ_PTR(chr + 1 * 1024, nes->nes_ppu.pattern_table[1]);
    nes_load_chrrom_4k(nes, 1, 0);
    for (int i = 0; i < 4; ++i) {
        if (nes->nes_ppu.pattern_table[4 + i] != chr + (size_t)i * 1024) {
            test_fixture_free(&f);
            return mapper_report("4KB CHR load", 0, "sequential banks", "mismatch");
        }
    }
    nes_load_chrrom_8k(nes, 0, 3);          /* wraps to 0 */
    for (int i = 0; i < 8; ++i) {
        if (nes->nes_ppu.pattern_table[i] != chr + (size_t)i * 1024) {
            test_fixture_free(&f);
            return mapper_report("8KB CHR load", 0, "sequential banks", "mismatch");
        }
    }
    test_fixture_free(&f);

    /* CHR-RAM boards ignore the requested bank and map slot -> same slot. */
    test_rom_spec_t ram_spec;
    mapper_fill_spec(&ram_spec, 0, 2, 0);
    test_fixture_t ram;
    TEST_CHECK(test_fixture_make(&ram, &ram_spec));
    TEST_CHECK(ram.nes->nes_rom.chr_rom != NULL);   /* backing store allocated */
    uint8_t* const ram_chr = ram.nes->nes_rom.chr_rom;
    nes_load_chrrom_1k(ram.nes, 3, 5);
    TEST_EQ_PTR(ram_chr + 3 * 1024, ram.nes->nes_ppu.pattern_table[3]);
    nes_load_chrrom_4k(ram.nes, 1, 7);
    for (int i = 0; i < 4; ++i) {
        if (ram.nes->nes_ppu.pattern_table[4 + i] != ram_chr + (size_t)(4 + i) * 1024) {
            test_fixture_free(&ram);
            return mapper_report("CHR-RAM 4KB identity", 0, "slot == bank", "mismatch");
        }
    }
    test_fixture_free(&ram);
    return TEST_PASS;
}

/*
 * Mapper 4 + Waixing protection board (风云, 外星電腦科技 titles).
 *
 * The board answers an $5010 = $8C handshake; from then on it serves its own 2KB
 * CHR-RAM at $0800-$0FFF while R1 selects bank $00, and the game copies its
 * Chinese dialog font and message frame tiles into that window through $2007.
 * Before the board is modelled those writes were dropped as CHR-ROM writes, so
 * every dialog glyph came out of the wrong pattern page.
 */
static void mapper4_ppu_write(nes_t* nes, uint16_t address, uint8_t data) {
    nes_write_ppu_register(nes, 0x2006, (uint8_t)(address >> 8));
    nes_write_ppu_register(nes, 0x2006, (uint8_t)(address & 0xFF));
    nes_write_ppu_register(nes, 0x2007, data);
}

/* Reads one byte of pattern space; $2007 is buffered, hence the double read. */
static uint8_t mapper4_ppu_read(nes_t* nes, uint16_t address) {
    nes_write_ppu_register(nes, 0x2006, (uint8_t)(address >> 8));
    nes_write_ppu_register(nes, 0x2006, (uint8_t)(address & 0xFF));
    (void)nes_read_ppu_register(nes, 0x2007);   /* returns the stale buffer */
    return nes_read_ppu_register(nes, 0x2007);
}

int test_mapper4_waixing_window(void) {
    test_rom_spec_t spec;
    mapper_fill_spec(&spec, 4, 4, 1);       /* mapper 4 with 8KB of CHR-ROM */
    test_fixture_t f;
    TEST_CHECK(test_fixture_make(&f, &spec));
    nes_t* nes = f.nes;
    uint8_t* const chr = nes->nes_rom.chr_rom;
    const uint16_t window = 0x0FC0;         /* upper half of the $0800 window */
    const uint8_t rom_byte = chr[1 * 1024 + (window & 0x3FF)];
    const uint8_t rom_byte_low = chr[0 * 1024 + 0x100];

    /* R1 = $00 selects the window the protection board owns. */
    nes->nes_mapper.mapper_write(nes, 0x8000, 0x01);
    nes->nes_mapper.mapper_write(nes, 0x8001, 0x00);
    TEST_EQ_PTR(chr + 0 * 1024, nes->nes_ppu.pattern_table[2]);
    TEST_EQ_PTR(chr + 1 * 1024, nes->nes_ppu.pattern_table[3]);

    /* Without the handshake the window is CHR-ROM, so the write must be dropped. */
    mapper4_ppu_write(nes, window, 0x5A);
    TEST_EQ_U32(rom_byte, chr[1 * 1024 + (window & 0x3FF)]);
    TEST_EQ_U32(rom_byte, mapper4_ppu_read(nes, window));

    /* The handshake is what hands the window to the board. */
    nes->nes_mapper.mapper_apu(nes, 0x5010, 0x8Cu);
    mapper4_ppu_write(nes, window, 0x5A);
    TEST_CHECK(nes->nes_ppu.pattern_table[3] != chr + 1 * 1024);
    TEST_EQ_U32(0x5A, mapper4_ppu_read(nes, window));
    /* Both 1KB halves of the window are the board's RAM. */
    mapper4_ppu_write(nes, 0x0900, 0xA5);
    TEST_EQ_U32(0xA5, mapper4_ppu_read(nes, 0x0900));
    /* Writes to a CHR-ROM bank elsewhere in pattern space stay blocked. */
    mapper4_ppu_write(nes, 0x0100, 0x3C);
    TEST_EQ_U32(rom_byte_low, chr[0 * 1024 + 0x100]);

    /* Any other R1 value pages CHR-ROM back into the window. */
    nes->nes_mapper.mapper_write(nes, 0x8001, 0x02);
    TEST_EQ_PTR(chr + 2 * 1024, nes->nes_ppu.pattern_table[2]);
    TEST_EQ_PTR(chr + 3 * 1024, nes->nes_ppu.pattern_table[3]);

    test_fixture_free(&f);
    return TEST_PASS;
}

/*
 * VRC4 boards wire 8KB of work RAM at $6000-$7FFF whether or not the cart has a battery
 * (Mesen's VRC2_4 sets _prgRamSize = 0x2000 unconditionally).  Gradius II (沙罗曼蛇2) depends
 * on it: it composes the intro's tile upload data at $6360 and reads it back through the
 * pointer pair $08/$09, so an unmapped window uploads an all-zero buffer and the whole
 * opening animation renders as a black screen.
 */
int test_mapper25_wram(void) {
    test_rom_spec_t spec;
    mapper_fill_spec(&spec, 25, 16, 16);    /* 128KB PRG + 128KB CHR, like 沙罗曼蛇2.nes */
    spec.save = 0;                          /* no battery bit in the iNES header */
    test_fixture_t f;
    TEST_CHECK(test_fixture_make(&f, &spec));
    nes_t* nes = f.nes;

    /* Both the core (NES_USE_SRAM=1) and the board may have allocated it; drop the core's
     * buffer and re-run the board init to model a NES_USE_SRAM=0 build. */
    if (nes->nes_rom.sram != NULL) {
        nes_free(nes->nes_rom.sram);
        nes->nes_rom.sram = NULL;
    }
    (void)nes->nes_mapper.mapper_init(nes);
    TEST_CHECK(nes->nes_rom.sram != NULL);

    /* The window the intro uploads from must behave like work RAM through the CPU bus. */
    nes_test_cpu_write(nes, 0x6360, 0x35);
    nes_test_cpu_write(nes, 0x6361, 0x36);
    nes_test_cpu_write(nes, 0x7FFF, 0x4D);
    TEST_EQ_U32(0x35, nes_test_cpu_read(nes, 0x6360));
    TEST_EQ_U32(0x36, nes_test_cpu_read(nes, 0x6361));
    TEST_EQ_U32(0x4D, nes_test_cpu_read(nes, 0x7FFF));
    /* Work RAM, not a battery save: the header bit stays clear. */
    TEST_EQ_U32(0, nes->nes_rom.save_ram);

    test_fixture_free(&f);
    return TEST_PASS;
}

/*
 * Mapper 164 (Waixing, 太空战士5).  Mesen2's Waixing/Waixing164.h: 32KB PRG pages, registers at
 * $5000 (low nibble) and $5100 (high nibble), and power-on on the LAST page (0x0F) because that
 * is where the reset vector lives.  The old implementation started on page 0, packed six bits
 * into $5000 and ignored $5100, so the board fetched its reset vector from the wrong bank.
 */
int test_mapper164_waixing(void) {
    test_rom_spec_t spec;
    mapper_fill_spec(&spec, 164, 128, 0);   /* 2MB PRG = 64 x 32KB, so the high nibble is observable */
    test_fixture_t f;
    TEST_CHECK(test_fixture_make(&f, &spec));
    nes_t* nes = f.nes;
    uint8_t* const prg = nes->nes_rom.prg_rom;

    /* Power-on page 0x0F -> 16KB banks 30 / 31 (NOT the last page: the board powers up on a
       fixed page number 0x0F, not on the highest one). */
    TEST_EQ_U32(30u, (uint32_t)(nes->nes_cpu.prg_banks[0] - prg) / 16384u);
    TEST_EQ_U32(31u, (uint32_t)(nes->nes_cpu.prg_banks[2] - prg) / 16384u);

    TEST_CHECK(nes->nes_mapper.mapper_apu != NULL);
    /* $5000 sets the low nibble: 0x0F & 0xF0 | 3 = 3 -> 16KB banks 6 / 7. */
    nes->nes_mapper.mapper_apu(nes, 0x5000, 0x03);
    TEST_EQ_U32(6u, (uint32_t)(nes->nes_cpu.prg_banks[0] - prg) / 16384u);
    /* $5100 sets the high nibble: 3 & 0x0F | (1 << 4) = 0x13 -> 16KB banks 38 / 39. */
    nes->nes_mapper.mapper_apu(nes, 0x5100, 0x01);
    TEST_EQ_U32(38u, (uint32_t)(nes->nes_cpu.prg_banks[0] - prg) / 16384u);
    TEST_EQ_U32(39u, (uint32_t)(nes->nes_cpu.prg_banks[2] - prg) / 16384u);

    test_fixture_free(&f);
    return TEST_PASS;
}

/*
 * Mapper 165 (the Chinese Fire Emblem board).  Mesen2's Mmc3Variants/MMC3_165.h is the
 * authority: standard MMC3 PRG banking plus MMC2-style CHR latches.  CHR slot 0 takes
 * register[latch0 ? 1 : 0] and slot 1 takes register[latch1 ? 4 : 2]; page 0 means the board's
 * own 4KB CHR-RAM, anything else a CHR-ROM 4KB page.  The latches flip when the PPU fetches
 * from tile $FD/$FE, which the core reports through mapper_ppu.  The old implementation was a
 * simplified MMC2 (one PRG register at $A000, CHR registers at $B000-$E000), so 圣火徽章外传
 * never reached its title screen.
 */
int test_mapper165_fire_emblem(void) {
    test_rom_spec_t spec;
    mapper_fill_spec(&spec, 165, 32, 16);   /* 512KB PRG (32 x 16KB) + 128KB CHR (16 x 8KB) */
    test_fixture_t f;
    TEST_CHECK(test_fixture_make(&f, &spec));
    nes_t* nes = f.nes;
    uint8_t* const prg = nes->nes_rom.prg_rom;

    /* Power-on: MMC3 mode 0 with bank_values 6/7 = 0/1 -> $8000 = 8KB bank 0,
       $A000 = bank 1, and the last two banks fixed. */
    const uint16_t pg8 = (uint16_t)(nes->nes_rom.prg_rom_size * 2u);
    TEST_EQ_U32(0u, (uint32_t)(nes->nes_cpu.prg_banks[0] - prg) / 8192u);
    TEST_EQ_U32(1u, (uint32_t)(nes->nes_cpu.prg_banks[1] - prg) / 8192u);
    TEST_EQ_U32((uint32_t)(pg8 - 2u), (uint32_t)(nes->nes_cpu.prg_banks[2] - prg) / 8192u);
    TEST_EQ_U32((uint32_t)(pg8 - 1u), (uint32_t)(nes->nes_cpu.prg_banks[3] - prg) / 8192u);

    /* The board owns a 4KB CHR-RAM even though the cart has CHR-ROM. */
    TEST_CHECK(nes->nes_mapper.mapper_ppu != NULL);
    /* CHR register R0 = 0 -> slot 0 reads the CHR-RAM, i.e. four 1KB pointers inside one buffer. */
    nes->nes_mapper.mapper_write(nes, 0x8000, 0x00);   /* select R0 */
    nes->nes_mapper.mapper_write(nes, 0x8001, 0x00);   /* R0 = 0 -> CHR-RAM */
    {
        const uint8_t* p0 = nes->nes_ppu.pattern_table[0];
        const uint8_t* p1 = nes->nes_ppu.pattern_table[1];
        /* Must be the board's OWN 4KB buffer: four consecutive 1KB pointers that live outside
           the CHR-ROM image.  Consecutiveness alone also holds for CHR-ROM pages, so without the
           "outside CHR-ROM" half this assertion could not flip when the allocation is removed -
           the reverse-verification trap this project has hit before. */
        const uint8_t* rom_lo = nes->nes_rom.chr_rom;
        const uint8_t* rom_hi = rom_lo + (size_t)nes->nes_rom.chr_rom_size * 8192u;
        const int inside_rom = (p0 >= rom_lo && p0 < rom_hi);
        if (p0 == NULL || p1 == NULL || (p0 - p1) != -1024 || inside_rom) {
            test_fixture_free(&f);
            return mapper_report("mapper 165: CHR register 0 selects the 4KB CHR-RAM", 165,
                                 "four consecutive 1KB pointers outside CHR-ROM",
                                 "not the board's CHR-RAM buffer");
        }
    }
    /* R0 = 8 -> CHR-ROM 4KB page 8 >> 2 = 2, i.e. 1KB pages 8..11 of the ROM. */
    nes->nes_mapper.mapper_write(nes, 0x8000, 0x00);
    nes->nes_mapper.mapper_write(nes, 0x8001, 0x08);
    {
        uint8_t* const chr = nes->nes_rom.chr_rom;
        const uint32_t off = (uint32_t)(nes->nes_ppu.pattern_table[0] - chr);
        if (off != 8u * 1024u) {
            test_fixture_free(&f);
            return mapper_report("mapper 165: CHR register 8 selects CHR-ROM page 2", 165,
                                 "pattern_table[0] at CHR offset 8192", "wrong CHR offset");
        }
    }

    test_fixture_free(&f);
    return TEST_PASS;
}

/*
 * Mapper 162 (Waixing).  Mesen2's Waixing162.h is the authority: 32KB PRG pages, four registers
 * at $5000-$5FFF selected by address bits 9-8, power-on regs 3/0/0/7, and a bank formula that
 * switches on bits 0 and 2 of regs[3].  The old implementation used one register at $8000, a
 * 16KB page and a single formula, so 西游记后传 booted into the wrong 32KB page and stayed blank.
 * Note the registers live in $5000-$5FFF, which the core routes to mapper_apu - the board must
 * install that hook or every bank switch is silently dropped.
 */
int test_mapper162_waixing(void) {
    test_rom_spec_t spec;
    mapper_fill_spec(&spec, 162, 64, 0);    /* 1MB PRG = 64 x 16KB, CHR-RAM */
    test_fixture_t f;
    TEST_CHECK(test_fixture_make(&f, &spec));
    nes_t* nes = f.nes;
    uint8_t* const prg = nes->nes_rom.prg_rom;

    /* Power-on: regs = 3/0/0/7 -> regs[3]&5 == 5 -> page = (3 & 0x0F) | ((0 & 0x0F) << 4) = 3.
       So $8000 maps 16KB bank 6 and $C000 maps bank 7. */
    TEST_EQ_U32(6u, (uint32_t)(nes->nes_cpu.prg_banks[0] - prg) / 16384u);
    TEST_EQ_U32(7u, (uint32_t)(nes->nes_cpu.prg_banks[2] - prg) / 16384u);

    /* The registers are reached through the $5000-$5FFF hook, not $8000-$FFFF. */
    TEST_CHECK(nes->nes_mapper.mapper_apu != NULL);
    /* regs[3] stays 7 (bits 0x5 == 5), regs[2] = 1 -> page = 3 | (1 << 4) = 19
       -> 16KB banks 38 and 39. */
    nes->nes_mapper.mapper_apu(nes, 0x5200, 0x01);
    TEST_EQ_U32(38u, (uint32_t)(nes->nes_cpu.prg_banks[0] - prg) / 16384u);
    TEST_EQ_U32(39u, (uint32_t)(nes->nes_cpu.prg_banks[2] - prg) / 16384u);

    /* regs[3] = 4 flips to the "case 4" formula: page = (regs[0] & 0x0E)|((regs[1]>>1)&1)|((regs[2]&0x0F)<<4). */
    nes->nes_mapper.mapper_apu(nes, 0x5000, 0x05);   /* regs[0] = 5 -> 0x04 */
    nes->nes_mapper.mapper_apu(nes, 0x5100, 0x02);   /* regs[1] = 2 -> (2>>1)&1 = 1 */
    nes->nes_mapper.mapper_apu(nes, 0x5300, 0x04);   /* regs[3] = 4 -> case 4 */
    /* page = 0x04 | 1 | (0x01 << 4) = 0x15 = 21 -> 16KB banks 42 / 43 */
    TEST_EQ_U32(42u, (uint32_t)(nes->nes_cpu.prg_banks[0] - prg) / 16384u);

    test_fixture_free(&f);
    return TEST_PASS;
}

/*
 * Same class of bug as mapper 25: the board owns work RAM at $6000-$7FFF that the iNES header
 * does not advertise with a battery bit.  沙罗曼蛇3 is the Famicom Disk System Salamander moved
 * onto a VRC3 cartridge, and the FDS gives the game WRAM from $6000 up - it decompresses the
 * level source there and reads it back while building the screen, so an unmapped window makes
 * the level render as structured garbage over a blank background ("sprites but no background").
 */
int test_mapper73_wram(void) {
    test_rom_spec_t spec;
    mapper_fill_spec(&spec, 73, 8, 1);      /* 128KB PRG, no CHR-ROM (8KB CHR-RAM) */
    spec.save = 0;
    test_fixture_t f;
    TEST_CHECK(test_fixture_make(&f, &spec));
    nes_t* nes = f.nes;

    if (nes->nes_rom.sram != NULL) {
        nes_free(nes->nes_rom.sram);
        nes->nes_rom.sram = NULL;
    }
    (void)nes->nes_mapper.mapper_init(nes);
    TEST_CHECK(nes->nes_rom.sram != NULL);

    /* The window the level decompressor fills must read back what it wrote. */
    nes_test_cpu_write(nes, 0x6000, 0xA5);
    nes_test_cpu_write(nes, 0x6C3F, 0x3C);
    nes_test_cpu_write(nes, 0x7FFF, 0x5A);
    TEST_EQ_U32(0xA5, nes_test_cpu_read(nes, 0x6000));
    TEST_EQ_U32(0x3C, nes_test_cpu_read(nes, 0x6C3F));
    TEST_EQ_U32(0x5A, nes_test_cpu_read(nes, 0x7FFF));
    TEST_EQ_U32(0, nes->nes_rom.save_ram);

    test_fixture_free(&f);
    return TEST_PASS;
}

/*
 * MMC3 boards carry 8KB of PRG-RAM at $6000-$7FFF even when the iNES header has no
 * battery bit (TSROM: Super Mario Bros. 2/USA and friends use it as plain work RAM).
 * Builds with NES_USE_SRAM=0 leave nes_rom.sram NULL, so the board itself has to
 * provide it - 超级马里奥2 otherwise stops on the yellow level-load screen right
 * after the character select.
 */
int test_mapper4_wram(void) {
    test_rom_spec_t spec;
    mapper_fill_spec(&spec, 4, 4, 1);
    spec.save = 0;                          /* TSROM: WRAM but no battery bit */
    test_fixture_t f;
    TEST_CHECK(test_fixture_make(&f, &spec));
    nes_t* nes = f.nes;

    /* Both the core (NES_USE_SRAM=1) and the board may have allocated it; drop the
     * core's buffer and re-run the board init to model a NES_USE_SRAM=0 build. */
    if (nes->nes_rom.sram != NULL) {
        nes_free(nes->nes_rom.sram);
        nes->nes_rom.sram = NULL;
    }
    (void)nes->nes_mapper.mapper_init(nes);
    TEST_CHECK(nes->nes_rom.sram != NULL);

    /* $6000-$7FFF now behaves like work RAM through the CPU bus. */
    nes_test_cpu_write(nes, 0x61C2, 0x4D);
    nes_test_cpu_write(nes, 0x7802, 0xFB);
    TEST_EQ_U32(0x4D, nes_test_cpu_read(nes, 0x61C2));
    TEST_EQ_U32(0xFB, nes_test_cpu_read(nes, 0x7802));
    /* SRAM is not battery backed on those boards: the header bit stays clear. */
    TEST_EQ_U32(0, nes->nes_rom.save_ram);

    test_fixture_free(&f);
    return TEST_PASS;
}

static int mapper_fail(const char* what, uint16_t mapper, int variant, const char* detail) {
    char label[96];
    snprintf(label, sizeof(label), "mapper %u%s: %s", (unsigned)mapper,
             (variant == 0) ? " (CHR-ROM)" : (variant == 1) ? " (CHR-RAM)" : "", what);
    test_record_failure(__FILE__, __LINE__, label, "ok", detail);
    return 1;
}

int test_mapper_synthetic_smoke(void) {
    int supported = 0;
    for (int mapper = 0; mapper < 256; ++mapper) {
        if (mapper_is_supported(mapper)) ++supported;
    }
    /* The dispatch table exposes well over 150 boards on this build. */
    TEST_CHECK(supported >= 150);

    int failures = 0;
    for (int mapper = 0; mapper < 256; ++mapper) {
        if (!mapper_supported_cache[mapper]) continue;
        /* Two board styles: 8KB CHR-ROM and CHR-RAM. */
        for (int variant = 0; variant < 2; ++variant) {
            test_rom_spec_t spec;
            mapper_fill_spec(&spec, (uint16_t)mapper, 4, (variant == 0) ? 1 : 0);
            test_fixture_t f;
            if (!test_fixture_make(&f, &spec)) {
                failures += mapper_fail("synthetic load", (uint16_t)mapper, variant, "failed");
                continue;
            }
            if (f.nes->nes_mapper.mapper_init == NULL) {
                failures += mapper_fail("mapper_init", (uint16_t)mapper, variant, "NULL");
            } else if (f.nes->nes_mapper.mapper_write == NULL) {
                failures += mapper_fail("mapper_write", (uint16_t)mapper, variant, "NULL");
            } else if (nes_test_bank_check(f.nes) != NES_OK) {
                failures += mapper_fail("bank setup", (uint16_t)mapper, variant, "NULL bank");
            } else if (nes_test_run_frames(f.nes, 2) != NES_OK) {
                failures += mapper_fail("frame run", (uint16_t)mapper, variant, "error");
            }
            test_fixture_free(&f);
            TEST_ABORT_OR_FAIL();
        }
    }
    printf("    %d mapper(s) exercisable without ROM files, %d failure(s)\n", supported, failures);
    return failures == 0 ? TEST_PASS : TEST_FAIL;
}

/*
 * Mapper 1 serial port: a register is loaded on the fifth write.
 *
 * romdb can select a real write counter for MMC1 (nes_rom.mmc1_strict) for ROMs verified to send
 * exactly five bits per register.  The counter must load on that fifth write: the sentinel-bit test
 * is useless once the shifter starts cleared, and then the load fires one write late — into the
 * next register, carrying four stale bits.  AD&D (Hillsfar, romdb 0x2C33161D) is exactly that case:
 * with the delayed load its control register never loaded at all and every PRG select landed one
 * write late, so the boot code looped forever re-writing the same nonsense to the mapper.
 */
int test_mapper1_serial_counter(void) {
    test_rom_spec_t spec;
    mapper_fill_spec(&spec, 1, 4, 0);       /* 4 x 16KB PRG banks, CHR-RAM board */
    test_fixture_t f;
    TEST_CHECK(test_fixture_make(&f, &spec));
    nes_t* nes = f.nes;
    uint8_t* const prg = nes->nes_rom.prg_rom;

    /* Power-on control is $0C (P=3): $8000 is switchable, $C000 holds the last bank. */
    TEST_EQ_PTR(prg, nes->nes_cpu.prg_banks[0]);
    TEST_EQ_PTR(prg + 3 * 16384, nes->nes_cpu.prg_banks[2]);

    for (int model = 0; model < 2; ++model) {
        nes->nes_rom.mmc1_strict = (uint8_t)model;   /* 0 = sentinel bit, 1 = write counter */
        /* Start from a known bank: five zero bits select bank 0. */
        for (int i = 0; i < 5; ++i) {
            nes_test_cpu_write(nes, 0xE000, 0x00);
        }
        /* Five writes of "0 1 0 0 0" select bank 2, and only the fifth one may load it. */
        const uint8_t bits[5] = { 0, 1, 0, 0, 0 };
        for (int i = 0; i < 4; ++i) {
            nes_test_cpu_write(nes, 0xE000, bits[i]);
        }
        if (nes->nes_cpu.prg_banks[0] != prg) {
            test_fixture_free(&f);
            return mapper_report("serial port: four bits must not load", 1, "bank 0",
                                 "loaded early");
        }
        nes_test_cpu_write(nes, 0xE000, bits[4]);
        if (nes->nes_cpu.prg_banks[0] != prg + 2 * 16384) {
            test_fixture_free(&f);
            return mapper_report("serial port: fifth write loads", 1, "bank 2", "not bank 2");
        }

        /* A write with bit 7 set resets the shifter and its counter: what follows is a fresh
           five-bit sequence, not the tail of the previous one. */
        nes_test_cpu_write(nes, 0xE000, 0x00);
        nes_test_cpu_write(nes, 0xE000, 0x00);
        nes_test_cpu_write(nes, 0xE000, 0x80);
        nes_test_cpu_write(nes, 0xE000, 0x01);
        for (int i = 0; i < 4; ++i) {
            nes_test_cpu_write(nes, 0xE000, 0x00);
        }
        if (nes->nes_cpu.prg_banks[0] != prg + 1 * 16384) {
            test_fixture_free(&f);
            return mapper_report("serial port: $80 restarts the sequence", 1, "bank 1",
                                 "stale bits leaked across the reset");
        }
    }
    test_fixture_free(&f);
    return TEST_PASS;
}

/*
 * 6502 read-modify-write on an MMC1 register.
 *
 * INC/DEC/ASL/... write the original value back before the modified one, and MMC1 drops a bit
 * write that arrives in the cycle right after another one ($80 reset writes are never dropped).
 * AD&D (Hillsfar)'s reset stub is `SEI / INC $FFD7 / JMP $C000` with $FF stored at $FFD7: the
 * dummy write carries the $FF and resets the shift register, and the $00 the instruction computes
 * is dropped.  A core that emits only the modified value writes a stray 0 bit instead, which
 * shifts every following five-bit sequence by one — that game then loops forever re-writing the
 * same nonsense to the mapper (control never loads, PRG stays on the wrong bank).
 */
int test_mapper1_rmw_reset_write(void) {
    test_rom_spec_t spec;
    mapper_fill_spec(&spec, 1, 4, 0);       /* 4 x 16KB PRG banks, CHR-RAM board */
    test_fixture_t f;
    TEST_CHECK(test_fixture_make(&f, &spec));
    nes_t* nes = f.nes;
    uint8_t* const prg = nes->nes_rom.prg_rom;
    nes->nes_rom.mmc1_strict = 1;

    /* The byte the stub reads must have bit 7 set, as the real ROM has ($FF at $FFD7). */
    prg[(size_t)(nes->nes_rom.prg_rom_size - 1) * 16384 + (0xFFD7 - 0xC000)] = 0xFF;

    /* Three bits into the PRG register first: both halves of the fix matter, because a stray bit
       either way completes this sequence early and lands on the wrong bank below. */
    nes_test_cpu_write(nes, 0xE000, 0x01);
    nes_test_cpu_write(nes, 0xE000, 0x01);
    nes_test_cpu_write(nes, 0xE000, 0x01);

    /* Execute `INC $FFD7` for real (the test API writes data, it does not run RMW instructions). */
    nes_test_cpu_write(nes, 0x0010, 0xEE);   /* INC abs */
    nes_test_cpu_write(nes, 0x0011, 0xD7);
    nes_test_cpu_write(nes, 0x0012, 0xFF);
    nes_test_cpu_prepare(nes, 0x0010);
    uint16_t cycles = 0;
    if (nes_test_cpu_step(nes, &cycles) != NES_OK) {
        test_fixture_free(&f);
        return mapper_report("RMW on $FFD7", 1, "instruction runs", "error");
    }

    /* The reset cleared the shifter, so these five bits select bank 2 on their own. */
    for (int i = 0; i < 5; ++i) {
        nes_test_cpu_write(nes, 0xE000, (uint8_t)((0x02u >> i) & 1u));
    }
    if (nes->nes_cpu.prg_banks[0] != prg + 2 * 16384) {
        test_fixture_free(&f);
        return mapper_report("RMW reset write: five bits after INC", 1, "bank 2",
                             "stray bit shifted the sequence");
    }
    test_fixture_free(&f);
    return TEST_PASS;
}

/*
 * Family BASIC's NROM board carries work RAM at $6000-$7FFF, and its interpreter stops with
 * "バックアップ スイッチ ヲ OFF ニ シテクダサイ" when the RAM write test fails.  Port builds run
 * with NES_USE_SRAM=0, so nothing provides that window unless the romdb flag lets mapper 0 do it —
 * while every other NROM game must keep running without the extra 8KB.
 */
int test_mapper0_prg_ram(void) {
    test_rom_spec_t spec;
    mapper_fill_spec(&spec, 0, 2, 1);
    test_fixture_t f;
    TEST_CHECK(test_fixture_make(&f, &spec));
    nes_t* nes = f.nes;

    /* Re-run mapper_init() the way a NES_USE_SRAM=0 build would: no PRG RAM to start with. */
    if (nes->nes_rom.sram != NULL) {
        nes_free(nes->nes_rom.sram);
        nes->nes_rom.sram = NULL;
    }
    nes->nes_rom.prg_ram = 0;
    nes->nes_mapper.mapper_init(nes);
    if (nes->nes_rom.sram != NULL) {
        test_fixture_free(&f);
        return mapper_report("NROM without the romdb flag", 0, "no PRG RAM", "allocated");
    }

    nes->nes_rom.prg_ram = 1;
    nes->nes_mapper.mapper_init(nes);
    if (nes->nes_rom.sram == NULL) {
        test_fixture_free(&f);
        return mapper_report("NROM with the romdb flag", 0, "PRG RAM", "none");
    }
    nes_test_cpu_write(nes, 0x6005, 0x5A);          /* the interpreter's write test */
    TEST_EQ_U32(0x5A, nes_test_cpu_read(nes, 0x6005));
    TEST_EQ_U32(0x00, nes_test_cpu_read(nes, 0x6006));   /* zero-initialised */
    test_fixture_free(&f);
    return TEST_PASS;
}

/*
 * Mapper 245 on a board without CHR-ROM (FC塞尔达传说汉化版).
 *
 * The 2KB CHR-RAM window that CHR bank values 0/1 point at only makes sense when the cartridge
 * also carries CHR-ROM to page in beside it.  Without CHR-ROM there are no banks to page: the game
 * fills the 8KB CHR-RAM itself through $2007 — this hack writes all 8192 pattern bytes — so each
 * 1KB slot has to show its own page.  Folding them onto the two-page window garbles every tile
 * (the title frame and logo came out as noise).  The game never writes R0-R5, so the CHR registers
 * stay 0 and the override used to catch every slot.
 */
int test_mapper245_chr_ram_board(void) {
    test_rom_spec_t spec;
    mapper_fill_spec(&spec, 245, 8, 0);          /* 128KB PRG, CHR-RAM board */
    test_fixture_t f;
    TEST_CHECK(test_fixture_make(&f, &spec));
    nes_t* nes = f.nes;
    uint8_t* const chr = nes->nes_rom.chr_rom;

    /* Only the PRG registers are ever written; R0-R5 keep their power-on value 0. */
    nes_test_cpu_write(nes, 0x8000, 0x06);       /* select R6 */
    nes_test_cpu_write(nes, 0x8001, 0x0E);
    nes_test_cpu_write(nes, 0x8000, 0x00);       /* back to R0 */

    for (int i = 0; i < 8; ++i) {
        if (nes->nes_ppu.pattern_table[i] != chr + (size_t)i * 1024) {
            test_fixture_free(&f);
            return mapper_report("mapper 245 CHR-RAM board: slot identity", 245,
                                 "CHR-RAM page i", "private 2KB window");
        }
    }

    /* An upload through $2007 must land in the CHR-RAM the PPU reads back. */
    mapper4_ppu_write(nes, 0x1C00, 0xAB);
    if (chr[7 * 1024] != 0xAB) {
        test_fixture_free(&f);
        return mapper_report("mapper 245 CHR-RAM board: $2007 upload", 245,
                             "byte stored in CHR-RAM", "not visible to the PPU");
    }
    test_fixture_free(&f);
    return TEST_PASS;
}

/*
 * Color Dreams (mapper 11): a write to $8000-$FFFF is [CCCC PPPP] — the high nibble selects the
 * 8KB CHR bank, the low nibble the 32KB PRG bank.
 *
 * Raid 2020's header claims mapper 7 (AxROM), which has no CHR banking at all; a romdb entry
 * reroutes the ROM here.  Its own bank table (written through $FFD8,X) is
 * "0C 0D 1C 1D 2C 2D 3C 3D 4C 4D 5C 5D", i.e. PRG banks 0/1 crossed with CHR banks 0-5: the title
 * screen runs on CHR bank 0, and the play field switches to 1-5.  Drop the CHR half and the game
 * still boots but every in-game screen is drawn with the title's tiles ("画面乱").
 */
int test_mapper11_chr_bank_select(void) {
    test_rom_spec_t spec;
    mapper_fill_spec(&spec, 11, 4, 8);           /* 64KB PRG + 64KB CHR, like Raid 2020 */
    spec.fill = TEST_ROM_FILL_RANDOM;            /* distinct banks: the slots are compared byte-wise */
    test_fixture_t f;
    TEST_CHECK(test_fixture_make(&f, &spec));
    nes_t* nes = f.nes;
    uint8_t* const prg = nes->nes_rom.prg_rom;
    uint8_t* const chr = nes->nes_rom.chr_rom;

    static const struct { uint8_t value, prg_bank, chr_bank; } cases[] = {
        { 0x0Cu, 0u, 0u }, { 0x0Du, 1u, 0u }, { 0x1Cu, 0u, 1u }, { 0x1Du, 1u, 1u },
        { 0x2Cu, 0u, 2u }, { 0x3Du, 1u, 3u }, { 0x4Cu, 0u, 4u }, { 0x5Du, 1u, 5u },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        nes_test_cpu_write(nes, 0x8000, cases[i].value);
        if (nes->nes_ppu.pattern_table[0] != chr + (size_t)cases[i].chr_bank * 8192) {
            test_fixture_free(&f);
            return mapper_report("mapper 11 CHR bank from the high nibble", 11,
                                 "8KB CHR bank selected by the write", "CHR bank ignored");
        }
        if (nes_test_cpu_read(nes, 0x8000) != prg[(size_t)cases[i].prg_bank * 32768]) {
            test_fixture_free(&f);
            return mapper_report("mapper 11 PRG bank from the low nibble", 11,
                                 "32KB PRG bank selected by the write", "wrong PRG bank");
        }
    }
    test_fixture_free(&f);
    return TEST_PASS;
}

#if (NES_VS_SYSTEM == 1)
/*
 * VS. System arcade PPUs (RP2C03 / RP2C04-xxxx) paint the same 6-bit colour index differently from
 * the consumer 2C02: RP2C04-0001 shows index $0A as orange where the NES palette has green, and
 * index $1A as black where the NES palette is green.  VS Battle City draws its whole title screen
 * with those two indexes, which is why it looked "green" until the arcade palette was selected
 * through nes_rom.vs_ppu (romdb).  The table itself is Mesen2's (_ppuPaletteArgb).
 */
int test_vs_ppu_palette(void) {
    test_rom_spec_t spec;
    mapper_fill_spec(&spec, 99, 2, 2);
    spec.fill = TEST_ROM_FILL_RANDOM;
    test_fixture_t f;
    TEST_CHECK(test_fixture_make(&f, &spec));
    nes_t* nes = f.nes;

    /* Consumer 2C02: index $1A is green (never black). */
    nes->nes_rom.vs_ppu = 0;
    nes->nes_ppu.palette_indexes[0] = 0x1Au;
    nes_palette_generate(nes);
    const nes_color_t consumer = nes->nes_ppu.palette[0];
    if ((uint32_t)(consumer & 0xFFFFFFu) == 0x000000u) {
        test_fixture_free(&f);
        return mapper_report("consumer PPU keeps the NES palette", 0,
                             "a non-black colour for index $1A", "black");
    }

    /* RP2C04-0001 (Mesen PpuModel 2): index $1A is black and index $0A is orange. */
    nes->nes_rom.vs_ppu = 2u;
    nes_palette_generate(nes);
    if ((uint32_t)(nes->nes_ppu.palette[0] & 0xFFFFFFu) != 0x000000u) {
        test_fixture_free(&f);
        return mapper_report("VS PPU palette is selected through nes_rom.vs_ppu", 0,
                             "index $1A black on RP2C04-0001", "consumer colour");
    }
    nes->nes_ppu.palette_indexes[0] = 0x0Au;
    nes_palette_generate(nes);
#if (NES_COLOR_DEPTH == 32)
    TEST_EQ_U32(0x00FFB600u, (uint32_t)(nes->nes_ppu.palette[0] & 0xFFFFFFu));   /* orange */
#else
    if ((uint32_t)(nes->nes_ppu.palette[0] & 0xFFFFFFu) == (uint32_t)(consumer & 0xFFFFFFu)) {
        test_fixture_free(&f);
        return mapper_report("VS PPU palette is selected through nes_rom.vs_ppu", 0,
                             "index $0A differs from the consumer table", "same colour");
    }
#endif

    /* An out-of-range model falls back to the consumer palette instead of reading out of bounds. */
    nes->nes_rom.vs_ppu = 200u;
    nes->nes_ppu.palette_indexes[0] = 0x1Au;
    nes_palette_generate(nes);
    if ((uint32_t)(nes->nes_ppu.palette[0] & 0xFFFFFFu) == 0x000000u) {
        test_fixture_free(&f);
        return mapper_report("unknown VS PPU model falls back", 0,
                             "the consumer palette", "black (out of range table)");
    }
    test_fixture_free(&f);
    return TEST_PASS;
}
#endif /* NES_VS_SYSTEM */

#if (NES_VS_SYSTEM == 1)
/*
 * VS. System cabinets have no second gamepad: the coin/credit/service switches share the upper bits
 * of the controller ports, and the games read them *outside* the 8-bit shift sequence.  VS Battle
 * City latches the coin from $4016 bit 4 ("LDA $4016 / AND #$10 ... LDA #$05 / STA $51") and never
 * leaves its attract mode without it — which is exactly why the pad alone looked unresponsive
 * ("presses do nothing") even though the standard controller read worked fine.
 */
int test_vs_system_switches(void) {
    test_rom_spec_t spec;
    mapper_fill_spec(&spec, 99, 2, 2);
    spec.fill = TEST_ROM_FILL_RANDOM;
    test_fixture_t f;
    TEST_CHECK(test_fixture_make(&f, &spec));
    nes_t* nes = f.nes;

    /* A plain NES keeps the ports free of arcade bits, even if a switch is somehow held. */
    nes->nes_rom.vs_system = 0;
    nes->nes_cpu.joypad.vs_coin = 1;
    nes->nes_cpu.joypad.vs_service = 1;
    if ((nes_test_cpu_read(nes, 0x4016) & 0x18u) != 0u ||
        (nes_test_cpu_read(nes, 0x4017) & 0x04u) != 0u) {
        test_fixture_free(&f);
        return mapper_report("VS switches stay off on a plain NES", 0,
                             "$4016 bits 4/3 and $4017 bit 2 clear", "arcade bits leaked");
    }

    /* VS board: coin = $4016 bit 4, credit/start = $4016 bit 3, service = $4017 bit 2. */
    nes->nes_rom.vs_system = 1;
    if ((nes_test_cpu_read(nes, 0x4016) & 0x24u) != 0x24u) {
        test_fixture_free(&f);
        return mapper_report("VS coin drives $4016 bits 5 and 2", 0, "bits set", "bits clear");
    }
    nes->nes_cpu.joypad.vs_start = 1;
    if ((nes_test_cpu_read(nes, 0x4016) & 0x08u) != 0x08u) {
        test_fixture_free(&f);
        return mapper_report("VS credit/start is $4016 bit 3", 0, "bit set", "bit clear");
    }
    if ((nes_test_cpu_read(nes, 0x4017) & 0x04u) != 0x04u) {
        test_fixture_free(&f);
        return mapper_report("VS service is $4017 bit 2", 0, "bit set", "bit clear");
    }

    /* ...and the standard 8-bit shift still lives in bit 0, so ordinary controller reads work.
       (Clear the credit bit first so this part only observes the coin bit.) */
    nes->nes_cpu.joypad.vs_start = 0;
    nes->nes_cpu.joypad.A1 = 1;
    nes_test_cpu_write(nes, 0x4016, 0x01);       /* strobe */
    nes_test_cpu_write(nes, 0x4016, 0x00);
    TEST_EQ_U32(0x25u, nes_test_cpu_read(nes, 0x4016));   /* bit 0 = A, bits 5/2 = coin */
    TEST_EQ_U32(0x24u, nes_test_cpu_read(nes, 0x4016));   /* A released, coin still held */
    test_fixture_free(&f);
    return TEST_PASS;
}

/*
 * VS. System cabinet protection hardware ($4020-$5FFF).  Mesen implements it in its VS control
 * manager, not in the mapper: reading $5E00 resets a counter and each $5E01 read returns the next
 * byte of a fixed 32-entry table.  VS TKO Boxing keeps its own copy of that sequence at $DD11 and
 * jumps back to the reset entry ($C0E7) on the first mismatch, so without this hardware the game
 * restarted its boot every two frames and never reached the title screen (VS Super Xevious uses the
 * third kind, which answers every other $4020-$5FFF read).
 */
int test_vs_system_protection(void) {
    test_rom_spec_t spec;
    mapper_fill_spec(&spec, 206, 4, 4);
    spec.fill = TEST_ROM_FILL_RANDOM;
    test_fixture_t f;
    TEST_CHECK(test_fixture_make(&f, &spec));
    nes_t* nes = f.nes;

    /* A board without the protection hardware must not answer for it. */
    nes->nes_rom.vs_protection = 0;
    TEST_EQ_U32(0u, nes_test_cpu_read(nes, 0x5E01));

    /* TKO Boxing: $5E00 resets the counter, $5E01 rotates the table (Mesen's first kind). */
    nes->nes_rom.vs_protection = 1;
    TEST_EQ_U32(0u, nes_test_cpu_read(nes, 0x5E00));
    TEST_EQ_U32(0xFFu, nes_test_cpu_read(nes, 0x5E01));
    TEST_EQ_U32(0xBFu, nes_test_cpu_read(nes, 0x5E01));
    TEST_EQ_U32(0xB7u, nes_test_cpu_read(nes, 0x5E01));
    /* Every other $4020-$5FFF address still belongs to the mapper. */
    TEST_EQ_U32(0u, nes_test_cpu_read(nes, 0x4800));
    /* Reading $5E00 restarts the sequence... */
    TEST_EQ_U32(0u, nes_test_cpu_read(nes, 0x5E00));
    TEST_EQ_U32(0xFFu, nes_test_cpu_read(nes, 0x5E01));
    /* ...and it wraps after 32 entries. */
    for (int i = 0; i < 31; i++) { (void)nes_test_cpu_read(nes, 0x5E01); }
    TEST_EQ_U32(0xFFu, nes_test_cpu_read(nes, 0x5E01));

    /* Super Xevious (third kind) answers every other $4020-$5FFF read from its own table. */
    nes->nes_rom.vs_protection = 3;
    TEST_EQ_U32(0u, nes_test_cpu_read(nes, 0x5E00));
    TEST_EQ_U32(0x05u, nes_test_cpu_read(nes, 0x4800));
    TEST_EQ_U32(0x01u, nes_test_cpu_read(nes, 0x4801));
    TEST_EQ_U32(0x89u, nes_test_cpu_read(nes, 0x4802));

    test_fixture_free(&f);
    return TEST_PASS;
}
#endif /* NES_VS_SYSTEM */

/*
 * Mapper 99 (Nintendo VS. UniSystem, VS Battle City): the board's bank latch is not in cartridge
 * space at all - it is written through $4016, the same port the CPU uses to strobe the
 * controllers, so that write has to reach both the board and the joypad.  The latch picks the 8KB
 * CHR bank (bit 2) and, on VS Gumshoe, also drives the first 8KB PRG page.
 */
int test_mapper99_vs_latch(void) {
    test_rom_spec_t spec;
    mapper_fill_spec(&spec, 99, 4, 2);           /* 64KB PRG + 16KB CHR so bank 4 is visible */
    spec.four_screen = 1;                        /* the VS board has four-screen VRAM */
    spec.fill = TEST_ROM_FILL_RANDOM;
    test_fixture_t f;
    TEST_CHECK(test_fixture_make(&f, &spec));
    nes_t* nes = f.nes;
    uint8_t* const prg = nes->nes_rom.prg_rom;
    uint8_t* const chr = nes->nes_rom.chr_rom;

    /* The VS board always carries 8KB of work RAM at $6000-$7FFF. */
    TEST_CHECK(nes->nes_rom.sram != NULL);

    /* Power-on: 32KB PRG fixed at $8000, CHR bank 0. */
    TEST_EQ_U32(prg[0], nes->nes_cpu.prg_banks[0][0]);
    TEST_EQ_U32(prg[2 * 8192], nes->nes_cpu.prg_banks[2][0]);
    TEST_EQ_U32(chr[0], nes->nes_ppu.pattern_table[0][0]);

    /* A write to $4016 latches the board register - and still strobes the controller (mask 0). */
    nes_test_cpu_write(nes, 0x4016, 0x05);
    if (nes->nes_cpu.joypad.mask != 0x00) {
        test_fixture_free(&f);
        return mapper_report("mapper 99 $4016 write also strobes the joypad", 99,
                             "controller strobe still runs", "board latch swallowed the write");
    }
    TEST_EQ_U32(chr[8192], nes->nes_ppu.pattern_table[0][0]);          /* CHR 8KB bank 1 */

    /* bit 2 also drives the first 8KB PRG page (VS Gumshoe): 8KB bank 4 of this 64KB image. */
    TEST_EQ_U32(prg[4 * 8192], nes->nes_cpu.prg_banks[0][0]);

    /* Clearing bit 2 goes back to the 32KB-fixed layout and CHR bank 0. */
    nes_test_cpu_write(nes, 0x4016, 0x00);
    TEST_EQ_U32(prg[0], nes->nes_cpu.prg_banks[0][0]);
    TEST_EQ_U32(chr[0], nes->nes_ppu.pattern_table[0][0]);
    test_fixture_free(&f);
    return TEST_PASS;
}

/*
 * Mapper 78 (Jaleco JF-16) has two wirings and iNES 1.0 cannot tell them apart: Holy Diver
 * (submapper 3) drives the register's bit 3 as H/V mirroring, while every other board of the
 * family - Cosmo Carrier is submapper 1 - uses one-screen A/B.  Both games are 128KB PRG +
 * 128KB CHR with flags6 bit3 set, so the old shape heuristic classified Cosmo Carrier as Holy
 * Diver and displayed the nametable the game had not filled (blank blue title screen).
 */
int test_mapper78_jf16_mirroring(void) {
    test_rom_spec_t spec;
    mapper_fill_spec(&spec, 78, 8, 16);          /* 128KB PRG + 128KB CHR, like Cosmo Carrier */
    spec.four_screen = 1;                        /* flags6 bit3: both JF-16 games set it */
    spec.fill = TEST_ROM_FILL_RANDOM;
    test_fixture_t f;
    TEST_CHECK(test_fixture_make(&f, &spec));
    nes_t* nes = f.nes;

    /* Not a Holy Diver CRC (the fixture's CRC is synthetic) -> one-screen A/B. */
    nes_test_cpu_write(nes, 0xD548, 0x00);       /* bit3 clear -> screen A */
    uint8_t* const screen_a = nes->nes_ppu.name_table[0];
    if (screen_a != nes->nes_ppu.name_table[1] ||
        screen_a != nes->nes_ppu.name_table[2] ||
        screen_a != nes->nes_ppu.name_table[3]) {
        test_fixture_free(&f);
        return mapper_report("mapper 78 JF-16 uses one-screen mirroring", 78,
                             "all four nametables on one screen", "H/V wiring");
    }

    nes_test_cpu_write(nes, 0xD550, 0x08);       /* bit3 set -> screen B */
    if (nes->nes_ppu.name_table[0] == screen_a ||
        nes->nes_ppu.name_table[0] != nes->nes_ppu.name_table[1] ||
        nes->nes_ppu.name_table[0] != nes->nes_ppu.name_table[3]) {
        test_fixture_free(&f);
        return mapper_report("mapper 78 JF-16 selects screen B on bit 3", 78,
                             "the other single screen", "screen A");
    }

    /* Bank bits 0-2 (PRG) and 4-7 (CHR) still decode; the board has bus conflicts, so the value
       the register sees is the written byte ANDed with the ROM byte it lands on. */
    const uint8_t rom_at_register = nes->nes_cpu.prg_banks[2][0x1553u];   /* $D553 -> slot 2 */
    const uint8_t latched = (uint8_t)(0x53u & rom_at_register);
    nes_test_cpu_write(nes, 0xD553, 0x53);
    if (nes->nes_cpu.prg_banks[0] != nes->nes_rom.prg_rom + (size_t)(latched & 0x07u) * 16384 ||
        nes->nes_ppu.pattern_table[0] != nes->nes_rom.chr_rom + (size_t)((latched >> 4) & 0x0Fu) * 8192) {
        test_fixture_free(&f);
        return mapper_report("mapper 78 JF-16 bank bits see the bus conflict", 78,
                             "banks from (written AND ROM)", "raw written value");
    }
    test_fixture_free(&f);
    return TEST_PASS;
}

/*
 * Mapper 226 (BMC 42-in-1, Super_42-in-1.nes): two write-only registers decoded by A0, and the
 * 16KB bank number is assembled from both of them:
 *
 *   bank = (reg0 & 0x1F) | ((reg0 & 0x80) >> 2) | ((reg1 & 0x01) << 6)
 *
 * reg0 bit 5 selects 32KB mode (both slots take the bank), otherwise the slots take the aligned
 * pair (bank & 0xFE, +1), and reg0 bit 6 is the mirroring bit (set = vertical).
 *
 * The old code shifted reg0 left instead, used reg1 bit 0 as the low bit, read the mode from
 * bit 6 / mirroring from bit 7, and mapped a single bank into both slots at power-on — which
 * hides the reset vector (Super_42-in-1's bank 0 vector is $04FE, i.e. RAM), so the game spun
 * forever at $8307 writing the mapper and never enabled rendering (gray screen).
 */
int test_mapper226_bank_formula(void) {
    test_rom_spec_t spec;
    mapper_fill_spec(&spec, 226, 64, 0);         /* 1MB PRG + CHR-RAM, like Super_42-in-1 */
    spec.fill = TEST_ROM_FILL_RANDOM;
    test_fixture_t f;
    TEST_CHECK(test_fixture_make(&f, &spec));
    nes_t* nes = f.nes;
    uint8_t* const prg = nes->nes_rom.prg_rom;

    /* Power-on: 16KB pages 0 and 1, so the last page's reset vector is reachable. */
    TEST_EQ_U32(prg[0 * 16384], nes_test_cpu_read(nes, 0x8000));
    TEST_EQ_U32(prg[1 * 16384], nes_test_cpu_read(nes, 0xC000));

    /* 16KB mode: $8000 <- $12 leaves bank bits 0-4 = 0x12 -> page 18 -> the aligned pair 18/19. */
    nes_test_cpu_write(nes, 0x8000, 0x12);
    nes_test_cpu_write(nes, 0x8001, 0x00);
    TEST_EQ_U32(prg[18 * 16384], nes_test_cpu_read(nes, 0x8000));
    TEST_EQ_U32(prg[19 * 16384], nes_test_cpu_read(nes, 0xC000));

    /* reg0 bit 5 = 32KB mode: $8000 <- $35 -> bank bits 0-4 = 0x15 -> page 21 in both slots. */
    nes_test_cpu_write(nes, 0x8000, 0x35);
    TEST_EQ_U32(prg[21 * 16384], nes_test_cpu_read(nes, 0x8000));
    TEST_EQ_U32(prg[21 * 16384], nes_test_cpu_read(nes, 0xC000));

    /* reg0 bit 7 is bank bit 5 (not the mirroring bit): page 0x20 -> slots 32 and 33. */
    nes_test_cpu_write(nes, 0x8000, 0x80);
    TEST_EQ_U32(prg[32 * 16384], nes_test_cpu_read(nes, 0x8000));
    TEST_EQ_U32(prg[33 * 16384], nes_test_cpu_read(nes, 0xC000));

    /* reg1 bit 0 is bank bit 6: with reg0 = 0 that is page 64 -> wraps to slots 0 and 1. */
    nes_test_cpu_write(nes, 0x8000, 0x00);
    nes_test_cpu_write(nes, 0x8001, 0x01);
    TEST_EQ_U32(prg[0 * 16384], nes_test_cpu_read(nes, 0x8000));
    TEST_EQ_U32(prg[1 * 16384], nes_test_cpu_read(nes, 0xC000));

    /* Mirroring: reg0 bit 6 set = vertical, clear = horizontal (header says vertical here). */
    nes_test_cpu_write(nes, 0x8000, 0x40);
    if (nes->nes_ppu.name_table[0] == nes->nes_ppu.name_table[1]) {
        test_fixture_free(&f);
        return mapper_report("mapper 226 mirroring bit is reg0 bit 6", 226,
                             "vertical nametable arrangement", "horizontal");
    }
    nes_test_cpu_write(nes, 0x8000, 0x00);
    if (nes->nes_ppu.name_table[0] != nes->nes_ppu.name_table[1]) {
        test_fixture_free(&f);
        return mapper_report("mapper 226 mirroring bit is reg0 bit 6", 226,
                             "horizontal nametable arrangement", "vertical");
    }
    test_fixture_free(&f);
    return TEST_PASS;
}

/*
 * Mapper 121 (Panda Prince / MK4 / A9713 pirate board, sf97.nes): an MMC3 with a protection
 * latch.  The program seeds the latch by writing $8003 and reads its answer back from
 * $5000-$5FFF; while the latch holds an accepted value the board also owns the three upper 8KB
 * PRG slots.  Under a plain MMC3 (mapper 187, what the header claims) the protection read
 * returns garbage, the game never gets past its check and stays gray.
 */
/*
 * CNROM (mapper 3) takes the 8KB CHR bank straight from the value written to $8000-$FFFF, modulo
 * the board's CHR size.  That is the board the pirate hack "Aladdin 3" really uses: its iNES header
 * claims mapper 41 (Caltron 6-in-1), which banks PRG/CHR somewhere else entirely, so the game drew
 * every tile from the wrong CHR bank ("游戏中乱码").  A romdb entry reroutes the ROM here.
 * Mesen's database: C9EE15A7,Famicorn,,,,3,32,32,0,0,0,0,v  (mapper 3, 32KB PRG, 32KB CHR).
 */
/*
 * Irem G-101 (mapper 32) banks PRG in 8KB units: $8000 and $A000 select banks, $C000/$E000 stay
 * fixed to the last two - and bit 1 of $9000 swaps that around ("PRG mode 1": $8000 becomes the
 * second-to-last bank while $C000 follows the $8000 register).  That 8KB granularity is exactly
 * what the Waixing game 爱先生的占卜之星 needs: its iNES header claims mapper 65 (Irem H-3001, a
 * different Irem board), and under that board the reset code jumps into the wrong bank and the
 * screen stays grey.  Mesen's database has 283AD224,Famicorn,,,,32,256,128,0,0,0,0,h.
 */
/*
 * Mapper 57 (the Dendy multicart board, "6 in 1"): two registers mirrored over the whole
 * $8000-$FFFF range in 2KB steps - $8000-$87FF (and $9000-$97FF, ...) is register 0,
 * $8800-$8FFF (and $9800-$9FFF, ...) is register 1.  Register 1 bits 5-6 pick the 16KB PRG
 * bank, bit 4 switches between "same bank in both halves" and an even 32KB pair, bit 3 is the
 * nametable mirroring; the 8KB CHR bank is register 0 bit 6 plus (register 0 | register 1) & 7.
 * A previous implementation read a single write value as "outer game select + PRG mode + inner
 * bank", which left the 6-in-1 image stuck on a blank screen.
 */
/*
 * Mapper 231 (BMC 20-in-1): the *address* of the write is the register and the data byte is
 * ignored.  The bank is ((address >> 5) & 1) | (address & 0x1E) - five bits, with address bit 5
 * acting as bank bit 0 - $8000-$BFFF takes the 32KB-aligned even bank (bank & 0x1E) while
 * $C000-$FFFF takes the bank itself, and the mirroring comes from address bit 7.  The previous
 * implementation read address bit 4 as a "32KB mode" flag, used only four bank bits and took the
 * mirroring from bit 5, which left the 20-in-1 image on a blank screen.
 */
/*
 * Mapper 230 (BMC reset-selected multicart): the board boots into "Contra mode", where only
 * $8000-$BFFF is switchable (value & 0x07) and $C000-$FFFF stays pinned to the kernel at bank 7 -
 * *not* to the last bank of the image.  Mesen's Mapper230.h reaches the same state by calling
 * Reset(true) from InitMapper.  The 22-in-1 (640KB, 40 banks) previously powered up with the
 * kernel slot on bank 39, so the reset vector came from the wrong bank and the screen stayed
 * black (verdict ok but every pixel the backdrop colour).
 */
/*
 * Mapper 229 (BMC 31-in-1): the write *address* is the register (the data byte is ignored).
 * Mesen's Mapper229.h decodes it as: CHR 8KB bank = address & 0xFF; if (address & 0x1E) == 0 the
 * whole 32KB page 0 fills $8000-$FFFF, otherwise address & 0x1F goes into *both* 16KB halves;
 * mirroring is address bit 5.  The previous implementation used a five-bit CHR bank, treated
 * address bit 0 as a "32KB mode" flag and put bank+1 into the second half, so the 31-in-1 never
 * left its boot screen.
 */
/*
 * Mapper 249 (Waixing MMC3 board): a $5000 register turns on a bank-number permutation, and the
 * board is permuted from power-on.  Chinese Waixing games such as `三十六计 [外星科技]` verify the
 * mapping by setting R6/R7 and comparing the byte at $BFFF with the low byte of their return
 * address; without the permutation only the value 0 can ever match, so the game spun forever in
 * that check loop ($F08F) and never enabled rendering.  The same ROM also showed the classic
 * uint8_t overflow: 256KB of CHR is 256 1KB pages, which truncated to 0 and skipped the CHR
 * mapping entirely.
 */
/*
 * Mapper 58 (Dendy multicart board): the *address* of the write is the register.  Address bit 6
 * selects "same 16KB bank in both halves" vs the aligned 32KB pair (bank & 0x06), bits 0-2 are the
 * bank, bits 3-5 are the 8KB CHR bank and bit 7 is the mirroring (set = horizontal).  The board
 * decodes it both in the $6000-$7FFF window and in cartridge space; `68合1` only ever writes the
 * latter (a single `STA $D728`), so a mapper that installed just the SRAM hook left the initial
 * mapping in place and the menu came out as a screenful of repeating tiles.
 */
/*
 * Mapper 255 (BMC PCB-018, the discrete 110-in-1 / 115-in-1 board; mapper 225 is the same board
 * in FCEUX).  The write address is the register: bits 6-11 are the PRG field, bits 0-5 the CHR
 * field, bit 12 picks 32KB (prg >> 1) vs "prg in both 16KB halves", bit 13 is the mirroring and
 * bit 14 is an extra bank bit that lands on bit 6 of both fields (7 bits total, matching the
 * 128 x 16KB PRG and 128 x 8KB CHR of these images).  $5000-$5FFF also carries four nibbles of
 * extra RAM behind address bit 11, which 115-in-1 needs.  The previous implementation read bits
 * 14/13 as mirroring/mode, dropped the extra bank bit and used another PRG formula, so both
 * multicarts mapped onto the same wrong 32KB page and showed one identical screen.
 */
/*
 * Mapper 235 ("Golden Game" 150-in-1 / 260合1): 2MB of PRG in 32KB pages plus 8KB of CHR-RAM.
 * Follows FCEUX's src/boards/235.cpp - the write address is the register, bank = ((A & 0x300) >> 3)
 * | (A & 0x1F), bit 11 selects 16KB mode (16KB page (bank << 1) | (A >> 12 bit 0) in both halves)
 * versus the 32KB page `bank`, bit 10 forces one-screen mirroring and otherwise bit 13 picks
 * horizontal.  The previous implementation used A & 0xFF as the bank, bit 8 as the mode and bit 9
 * as the mirroring, so 260合1 only drew its title over a blank list.
 */
/*
 * Mapper 235 open bus: FCEUX's M235Read answers with the data bus latch while the selected bank is
 * past the end of PRG, and `260合1(150合1)` uses exactly that read to decide which of its two menus
 * to build - without it the game uploaded a different, incomplete tile set and ~6% of the screen
 * came out as black blocks (and it showed the 260-in-1 title instead of the 150-in-1 one).
 */
/*
 * Mapper 62 ("Super 700-in-1", 2MB PRG + 1MB CHR): the encoding mixes address and data bits, and
 * Mesen's Mapper62.h and FCEUX's boards/62.cpp agree on it:
 *   prg page  = ((A & 0x3F00) >> 8) | (A & 0x40)          (7 bits)
 *   chr page  = ((A & 0x1F) << 2) | (data & 0x03)          (7 bits)
 *   A bit 5 set  -> 16KB mode (that page in both halves)
 *   A bit 5 clear -> 32KB mode (the aligned pair page & 0xFE and +1)
 *   mirroring = A bit 7 (0 = vertical, 1 = horizontal)
 * The previous implementation read `address >> 6` as the PRG page, data bit 7 as the mode and a
 * different CHR formula, and never set the mirroring - the 700-in-1 stayed blank.
 */
/*
 * Mapper 242 (Waixing, "1200-in-1"): Mesen's Core/NES/Mappers/Waixing/Mapper242.h - the write
 * address is the register (data ignored), the PRG window is ONE 32KB page selected by address
 * bits 6:3 (4 bits = 16 pages = 512KB, exactly this image) and the mirroring comes from address
 * bit 1.  There is no fixed bank at $C000.  The previous implementation used (data >> 1) & 7 as a
 * 16KB bank, kept the last 16KB fixed and read the mirroring from data bit 0.
 *
 * NOTE: 1200合1 itself still renders nothing in this core *and* in Mesen (plain green screen), so
 * this decoder is verified against Mesen's source rather than against a running title.
 */
int test_mapper242_waixing_1200in1(void) {
    test_rom_spec_t spec;
    mapper_fill_spec(&spec, 242, 32, 0);         /* 512KB PRG (32 x 16KB) + 8KB CHR-RAM */
    spec.fill = TEST_ROM_FILL_RANDOM;
    test_fixture_t f;
    TEST_CHECK(test_fixture_make(&f, &spec));
    nes_t* nes = f.nes;
    uint8_t* const prg = nes->nes_rom.prg_rom;

    /* Power-on: 32KB page 0 => 16KB banks 0 and 1 (8KB slots 0 and 2). */
    TEST_EQ_U32(0u, (uint32_t)(nes->nes_cpu.prg_banks[0] - prg) / 16384u);
    TEST_EQ_U32(1u, (uint32_t)(nes->nes_cpu.prg_banks[2] - prg) / 16384u);

    /* $8008: page = (0x8008 >> 3) & 0xF = 1 => 16KB banks 2 and 3; bit 1 clear -> vertical. */
    nes_test_cpu_write(nes, 0x8008, 0x00u);
    TEST_EQ_U32(2u, (uint32_t)(nes->nes_cpu.prg_banks[0] - prg) / 16384u);
    TEST_EQ_U32(3u, (uint32_t)(nes->nes_cpu.prg_banks[2] - prg) / 16384u);

    /* $807A: page = (0x807A >> 3) & 0xF = 15 => the last 32KB; bit 1 set -> horizontal. */
    nes_test_cpu_write(nes, 0x807A, 0x00u);
    TEST_EQ_U32(30u, (uint32_t)(nes->nes_cpu.prg_banks[0] - prg) / 16384u);
    TEST_EQ_U32(31u, (uint32_t)(nes->nes_cpu.prg_banks[2] - prg) / 16384u);
    if (nes->nes_ppu.name_table[0] != nes->nes_ppu.name_table[1] ||
        nes->nes_ppu.name_table[2] != nes->nes_ppu.name_table[3] ||
        nes->nes_ppu.name_table[0] == nes->nes_ppu.name_table[2]) {
        test_fixture_free(&f);
        return mapper_report("mapper 242 mirroring comes from address bit 1", 242,
                             "horizontal wiring when bit 1 is set", "wrong nametable wiring");
    }

    /* The data byte is ignored: a different value must not change the mapping. */
    nes_test_cpu_write(nes, 0x8008, 0xFFu);
    TEST_EQ_U32(2u, (uint32_t)(nes->nes_cpu.prg_banks[0] - prg) / 16384u);

    test_fixture_free(&f);
    return TEST_PASS;
}

int test_mapper62_super_700in1(void) {
    test_rom_spec_t spec;
    mapper_fill_spec(&spec, 62, 128, 128);       /* 2MB PRG (128 x 16KB) + 1MB CHR (128 x 8KB) */
    spec.fill = TEST_ROM_FILL_RANDOM;            /* distinct banks so the slots can be compared */
    test_fixture_t f;
    TEST_CHECK(test_fixture_make(&f, &spec));
    nes_t* nes = f.nes;
    uint8_t* const prg = nes->nes_rom.prg_rom;
    uint8_t* const chr = nes->nes_rom.chr_rom;

    /* Power-on: 16KB bank 0 at $8000 and bank 1 at $A000 (8KB slots 0 and 2). */
    TEST_EQ_U32(0u, (uint32_t)(nes->nes_cpu.prg_banks[0] - prg) / 16384u);
    TEST_EQ_U32(1u, (uint32_t)(nes->nes_cpu.prg_banks[2] - prg) / 16384u);

    /* $8045 with data 2: prg page = ((0x8045 & 0x3F00) >> 8) | 0x40 = 64, bit 5 clear -> 32KB mode
       on the aligned pair 64/65; chr page = ((0x8045 & 0x1F) << 2) | 2 = 22. */
    nes_test_cpu_write(nes, 0x8045, 0x02u);
    TEST_EQ_U32(64u, (uint32_t)(nes->nes_cpu.prg_banks[0] - prg) / 16384u);
    TEST_EQ_U32(65u, (uint32_t)(nes->nes_cpu.prg_banks[2] - prg) / 16384u);
    TEST_EQ_U32(22u, (uint32_t)(nes->nes_ppu.pattern_table[0] - chr) / 8192u);

    /* $A120: prg page = ((0xA120 & 0x3F00) >> 8) | (0xA120 & 0x40) = 0x21 | 0 = 33, bit 5 set ->
       16KB mode, so page 33 fills both halves; the CHR page follows the low address bits again. */
    nes_test_cpu_write(nes, 0xA120, 0x00u);
    TEST_EQ_U32(33u, (uint32_t)(nes->nes_cpu.prg_banks[0] - prg) / 16384u);
    TEST_EQ_U32(33u, (uint32_t)(nes->nes_cpu.prg_banks[2] - prg) / 16384u);
    TEST_EQ_U32(0u, (uint32_t)(nes->nes_ppu.pattern_table[0] - chr) / 8192u);

    /* The data bits 1-0 are the inner CHR bits: $801F gives ((0x1F) << 2) | 3 = 127. */
    nes_test_cpu_write(nes, 0x801F, 0x03u);
    TEST_EQ_U32(127u, (uint32_t)(nes->nes_ppu.pattern_table[0] - chr) / 8192u);

    /* Address bit 7 is the mirroring. */
    nes_test_cpu_write(nes, 0x8080, 0x00u);
    if (nes->nes_ppu.name_table[0] != nes->nes_ppu.name_table[1] ||
        nes->nes_ppu.name_table[2] != nes->nes_ppu.name_table[3] ||
        nes->nes_ppu.name_table[0] == nes->nes_ppu.name_table[2]) {
        test_fixture_free(&f);
        return mapper_report("mapper 62 mirroring comes from address bit 7", 62,
                             "horizontal wiring when bit 7 is set", "wrong nametable wiring");
    }

    test_fixture_free(&f);
    return TEST_PASS;
}

int test_mapper235_open_bus(void) {
    test_rom_spec_t spec;
    mapper_fill_spec(&spec, 235, 128, 0);        /* 2MB PRG = 64 x 32KB pages, so bank >= 64 is out */
    spec.fill = TEST_ROM_FILL_RANDOM;
    test_fixture_t f;
    TEST_CHECK(test_fixture_make(&f, &spec));
    nes_t* nes = f.nes;
    uint8_t* const prg = nes->nes_rom.prg_rom;

    /* An ordinary, in-range read must still come from the mapped bank (regression guard for the
       hand-written read_prg that this board installs). */
    TEST_EQ_U32((uint32_t)nes->nes_cpu.prg_banks[0][0x1234], (uint32_t)nes_test_cpu_read(nes, 0x9234));

    /* $831F: bank = ((0x300 >> 3) | 0x1F) = 127, past the 64 pages of this image -> open bus.
       The write stores the data byte as the bus latch... */
    nes_test_cpu_write(nes, 0x831F, 0x5Au);
    /* ...so the next PRG read answers with it (and clears the flag)... */
    TEST_EQ_U32(0x5Au, (uint32_t)nes_test_cpu_read(nes, 0x8000));
    /* ...and the one after that goes back to the normal mapping. */
    TEST_EQ_U32((uint32_t)nes->nes_cpu.prg_banks[0][0x0000], (uint32_t)nes_test_cpu_read(nes, 0x8000));

    /* The mapping itself is untouched by the out-of-range write, so the slots still hold the page
       that was selected before it. */
    TEST_EQ_U32(0u, (uint32_t)(nes->nes_cpu.prg_banks[0] - prg) / 8192u);

    test_fixture_free(&f);
    return TEST_PASS;
}

int test_mapper235_golden_game(void) {
    test_rom_spec_t spec;
    mapper_fill_spec(&spec, 235, 128, 0);        /* 2MB PRG (128 x 16KB) + 8KB CHR-RAM */
    spec.fill = TEST_ROM_FILL_RANDOM;            /* distinct banks so the slots can be compared */
    test_fixture_t f;
    TEST_CHECK(test_fixture_make(&f, &spec));
    nes_t* nes = f.nes;
    uint8_t* const prg = nes->nes_rom.prg_rom;

    /* Power-on: the 32KB page 0 (8KB banks 0-3) and a mapped CHR-RAM window. */
    for (uint8_t slot = 0; slot < 4; slot++) {
        TEST_EQ_U32((uint32_t)slot, (uint32_t)(nes->nes_cpu.prg_banks[slot] - prg) / 8192u);
    }
    if (nes->nes_ppu.pattern_table[0] == NULL) {
        test_fixture_free(&f);
        return mapper_report("mapper 235 maps its CHR-RAM window", 235,
                             "pattern table mapped", "CHR window lost");
    }

    /* $8805: bit 11 set -> 16KB mode, bank = ((0x800 >> 3) | 5) = 5, so page (5 << 1) | 0 = 10
       fills both halves. */
    nes_test_cpu_write(nes, 0x8805, 0x00u);
    TEST_EQ_U32(10u, (uint32_t)(nes->nes_cpu.prg_banks[0] - prg) / 16384u);
    TEST_EQ_U32(10u, (uint32_t)(nes->nes_cpu.prg_banks[2] - prg) / 16384u);

    /* $8105: bit 11 clear -> 32KB page ((0x100 >> 3) | 5) = 37, i.e. the 16KB pair 74/75. */
    nes_test_cpu_write(nes, 0x8105, 0x00u);
    TEST_EQ_U32(74u, (uint32_t)(nes->nes_cpu.prg_banks[0] - prg) / 16384u);
    TEST_EQ_U32(75u, (uint32_t)(nes->nes_cpu.prg_banks[2] - prg) / 16384u);

    /* Bit 10 forces one-screen mirroring: all four nametables land on the same page. */
    nes_test_cpu_write(nes, 0x8505, 0x00u);
    if (nes->nes_ppu.name_table[0] != nes->nes_ppu.name_table[1] ||
        nes->nes_ppu.name_table[0] != nes->nes_ppu.name_table[2] ||
        nes->nes_ppu.name_table[0] != nes->nes_ppu.name_table[3]) {
        test_fixture_free(&f);
        return mapper_report("mapper 235 one-screen mode comes from address bit 10", 235,
                             "all four nametables on one screen", "H/V wiring");
    }

    /* Clearing bit 10 with bit 13 set gives horizontal wiring ($A005: bit 13 = 0x2000 only). */
    nes_test_cpu_write(nes, 0xA005, 0x00u);
    if (nes->nes_ppu.name_table[0] != nes->nes_ppu.name_table[1] ||
        nes->nes_ppu.name_table[2] != nes->nes_ppu.name_table[3] ||
        nes->nes_ppu.name_table[0] == nes->nes_ppu.name_table[2]) {
        test_fixture_free(&f);
        return mapper_report("mapper 235 mirroring comes from address bit 13", 235,
                             "horizontal wiring when bit 13 is set", "wrong nametable wiring");
    }

    test_fixture_free(&f);
    return TEST_PASS;
}

int test_mapper255_bmc_pcb018(void) {
    test_rom_spec_t spec;
    mapper_fill_spec(&spec, 255, 128, 128);      /* 2MB PRG (128 x 16KB) + 1MB CHR (128 x 8KB) */
    spec.fill = TEST_ROM_FILL_RANDOM;            /* distinct banks so the slots can be compared */
    test_fixture_t f;
    TEST_CHECK(test_fixture_make(&f, &spec));
    nes_t* nes = f.nes;
    uint8_t* const prg = nes->nes_rom.prg_rom;
    uint8_t* const chr = nes->nes_rom.chr_rom;

    /* Power-on: prg = 0, mode = 0 -> 32KB page 0 (8KB banks 0-3), CHR bank 0. */
    for (uint8_t slot = 0; slot < 4; slot++) {
        TEST_EQ_U32((uint32_t)slot, (uint32_t)(nes->nes_cpu.prg_banks[slot] - prg) / 8192u);
    }
    TEST_EQ_U32(0u, (uint32_t)(nes->nes_ppu.pattern_table[0] - chr) / 8192u);

    /* $9140: bit 12 set -> 16KB mode with prg = 5, so both halves take bank 5. */
    nes_test_cpu_write(nes, 0x9140, 0x00u);
    TEST_EQ_U32(5u, (uint32_t)(nes->nes_cpu.prg_banks[0] - prg) / 16384u);
    TEST_EQ_U32(5u, (uint32_t)(nes->nes_cpu.prg_banks[2] - prg) / 16384u);

    /* $C083: bit 14 set (bank bit -> bit 6), prg field 2, chr field 3 -> prg 66 / chr 67, and
       bit 12 clear means 32KB page 66 >> 1 = 33, i.e. the pair 66/67. */
    nes_test_cpu_write(nes, 0xC083, 0x00u);
    TEST_EQ_U32(66u, (uint32_t)(nes->nes_cpu.prg_banks[0] - prg) / 16384u);
    TEST_EQ_U32(67u, (uint32_t)(nes->nes_cpu.prg_banks[2] - prg) / 16384u);
    TEST_EQ_U32(67u, (uint32_t)(nes->nes_ppu.pattern_table[0] - chr) / 8192u);

    /* Bit 13 is the mirroring. */
    nes_test_cpu_write(nes, 0xA000, 0x00u);
    if (nes->nes_ppu.name_table[0] != nes->nes_ppu.name_table[1] ||
        nes->nes_ppu.name_table[2] != nes->nes_ppu.name_table[3] ||
        nes->nes_ppu.name_table[0] == nes->nes_ppu.name_table[2]) {
        test_fixture_free(&f);
        return mapper_report("mapper 255 mirroring comes from address bit 13", 255,
                             "horizontal wiring when bit 13 is set", "wrong nametable wiring");
    }

    /* The $5000-$5FFF extra RAM (address bit 11 picks it, the low two bits pick the nibble). */
    nes_test_cpu_write(nes, 0x5802, 0xABu);
    TEST_EQ_U32(0x0Bu, (uint32_t)nes_test_cpu_read(nes, 0x5802));
    nes_test_cpu_write(nes, 0x5803, 0x07u);
    TEST_EQ_U32(0x07u, (uint32_t)nes_test_cpu_read(nes, 0x5803));
    TEST_EQ_U32(0x0Bu, (uint32_t)nes_test_cpu_read(nes, 0x5802));

    test_fixture_free(&f);
    return TEST_PASS;
}

int test_mapper58_dendy_address_register(void) {
    test_rom_spec_t spec;
    mapper_fill_spec(&spec, 58, 8, 8);           /* 128KB PRG (8 x 16KB) + 64KB CHR (8 x 8KB) */
    spec.fill = TEST_ROM_FILL_RANDOM;            /* distinct banks so the slots can be compared */
    test_fixture_t f;
    TEST_CHECK(test_fixture_make(&f, &spec));
    nes_t* nes = f.nes;
    uint8_t* const prg = nes->nes_rom.prg_rom;
    uint8_t* const chr = nes->nes_rom.chr_rom;

    /* Power-on: $8000 -> bank 0, $A000 -> bank 1 (8KB slots 0 and 2). */
    TEST_EQ_U32(0u, (uint32_t)(nes->nes_cpu.prg_banks[0] - prg) / 16384u);
    TEST_EQ_U32(1u, (uint32_t)(nes->nes_cpu.prg_banks[2] - prg) / 16384u);

    /* The SRAM window works: $6062 -> bit 6 set (16KB mode), the same bank 2 in both halves,
       CHR bank (0x6062 >> 3) & 7 = 4. */
    nes_test_cpu_write(nes, 0x6062, 0x00u);
    TEST_EQ_U32(2u, (uint32_t)(nes->nes_cpu.prg_banks[0] - prg) / 16384u);
    TEST_EQ_U32(2u, (uint32_t)(nes->nes_cpu.prg_banks[2] - prg) / 16384u);
    TEST_EQ_U32(4u, (uint32_t)(nes->nes_ppu.pattern_table[0] - chr) / 8192u);

    /* ...and so does cartridge space - this is the write `68合1` actually performs.  $8005 has
       bit 6 clear, so the aligned 32KB pair (5 & 6) / 2 = page 2 = banks 4/5 appears. */
    nes_test_cpu_write(nes, 0x8005, 0x00u);
    TEST_EQ_U32(4u, (uint32_t)(nes->nes_cpu.prg_banks[0] - prg) / 16384u);
    TEST_EQ_U32(5u, (uint32_t)(nes->nes_cpu.prg_banks[2] - prg) / 16384u);
    TEST_EQ_U32(0u, (uint32_t)(nes->nes_ppu.pattern_table[0] - chr) / 8192u);

    /* Bit 7 switches the mirroring to horizontal. */
    nes_test_cpu_write(nes, 0x8085, 0x00u);
    if (nes->nes_ppu.name_table[0] != nes->nes_ppu.name_table[1] ||
        nes->nes_ppu.name_table[2] != nes->nes_ppu.name_table[3] ||
        nes->nes_ppu.name_table[0] == nes->nes_ppu.name_table[2]) {
        test_fixture_free(&f);
        return mapper_report("mapper 58 mirroring comes from address bit 7", 58,
                             "horizontal wiring when bit 7 is set", "wrong nametable wiring");
    }

    test_fixture_free(&f);
    return TEST_PASS;
}

int test_mapper249_waixing_permute(void) {
    test_rom_spec_t spec;
    mapper_fill_spec(&spec, 249, 16, 32);        /* 256KB PRG (32 x 8KB) + 256KB CHR (256 x 1KB) */
    spec.fill = TEST_ROM_FILL_RANDOM;            /* distinct banks so the slots can be compared */
    test_fixture_t f;
    TEST_CHECK(test_fixture_make(&f, &spec));
    nes_t* nes = f.nes;
    uint8_t* const chr = nes->nes_rom.chr_rom;

    /* 256KB of CHR is 256 1KB pages; a uint8_t page count wrapped that to 0 and skipped the whole
       CHR mapping, which is what broke `三十六计 [外星科技]` (and, on the PRG side, left its
       bank-switch self check spinning at $F08F forever).  The window must be mapped... */
    if (nes->nes_ppu.pattern_table[0] == NULL) {
        test_fixture_free(&f);
        return mapper_report("mapper 249 maps its 256 CHR pages", 249,
                             "pattern table mapped", "CHR window lost (uint8_t overflow?)");
    }

    /* ...and R0 must actually move it: with the permutation the board applies from power-on,
       R0 = 0 -> page 0 while R0 = 2 -> page 27, and both are inside the 256-page ROM. */
    nes_test_cpu_write(nes, 0x8000, 0x00u);      /* select R0 */
    nes_test_cpu_write(nes, 0x8001, 0x00u);
    const uint8_t* const page0 = nes->nes_ppu.pattern_table[0];
    nes_test_cpu_write(nes, 0x8001, 0x02u);
    const uint8_t* const page2 = nes->nes_ppu.pattern_table[0];
    if (page0 == page2) {
        test_fixture_free(&f);
        return mapper_report("mapper 249 rebanks its 256 CHR pages", 249,
                             "R0 moves the 1KB window", "CHR window never changes");
    }
    /* The board's permutation is what makes R0 = 2 land back on page 2 (its formula maps 2 -> 2
       while sending e.g. 1 -> 5); both pages must come out of the 256-page CHR ROM. */
    if (page0 != chr || page2 != chr + 2 * 1024) {
        test_fixture_free(&f);
        return mapper_report("mapper 249 applies the Waixing page permutation", 249,
                             "R0 = 0 -> page 0, R0 = 2 -> page 2",
                             "wrong CHR page");
    }

    test_fixture_free(&f);
    return TEST_PASS;
}

int test_mapper229_bmc_31in1(void) {
    test_rom_spec_t spec;
    mapper_fill_spec(&spec, 229, 32, 32);        /* 512KB PRG (32 x 16KB) + 256KB CHR (32 x 8KB) */
    spec.fill = TEST_ROM_FILL_RANDOM;            /* distinct banks so the slots can be compared */
    test_fixture_t f;
    TEST_CHECK(test_fixture_make(&f, &spec));
    nes_t* nes = f.nes;
    uint8_t* const prg = nes->nes_rom.prg_rom;
    uint8_t* const chr = nes->nes_rom.chr_rom;

    /* Power-on goes through the same register write with $8000: CHR bank 0, and because
       $8000 & 0x1E == 0 the whole 32KB page 0 fills the address space (8KB banks 0-3). */
    TEST_EQ_U32(0u, (uint32_t)(nes->nes_ppu.pattern_table[0] - chr) / 8192u);
    for (uint8_t slot = 0; slot < 4; slot++) {
        TEST_EQ_U32((uint32_t)slot, (uint32_t)(nes->nes_cpu.prg_banks[slot] - prg) / 8192u);
    }
    /* ...with vertical wiring (address bit 5 clear). */
    if (nes->nes_ppu.name_table[0] != nes->nes_ppu.name_table[2] ||
        nes->nes_ppu.name_table[1] != nes->nes_ppu.name_table[3] ||
        nes->nes_ppu.name_table[0] == nes->nes_ppu.name_table[1]) {
        test_fixture_free(&f);
        return mapper_report("mapper 229 powers up on the 32KB page 0", 229,
                             "vertical wiring, 8KB banks 0-3", "wrong power-on state");
    }

    /* $8002: bits 1-4 non-zero -> 16KB mode, and *both* halves take bank 2 (the same bank twice).
       The CHR bank comes from the low 8 address bits, so it is 2 as well. */
    nes_test_cpu_write(nes, 0x8002, 0x00u);
    TEST_EQ_U32(2u, (uint32_t)(nes->nes_cpu.prg_banks[0] - prg) / 16384u);
    TEST_EQ_U32(2u, (uint32_t)(nes->nes_cpu.prg_banks[2] - prg) / 16384u);
    TEST_EQ_U32(2u, (uint32_t)(nes->nes_ppu.pattern_table[0] - chr) / 8192u);

    /* $8020: bit 5 sets only the mirroring - PRG stays on the 32KB page 0. */
    nes_test_cpu_write(nes, 0x8020, 0x00u);
    TEST_EQ_U32(0u, (uint32_t)(nes->nes_cpu.prg_banks[0] - prg) / 8192u);
    if (nes->nes_ppu.name_table[0] != nes->nes_ppu.name_table[1] ||
        nes->nes_ppu.name_table[2] != nes->nes_ppu.name_table[3] ||
        nes->nes_ppu.name_table[0] == nes->nes_ppu.name_table[2]) {
        test_fixture_free(&f);
        return mapper_report("mapper 229 mirroring comes from address bit 5", 229,
                             "horizontal wiring when bit 5 is set", "wrong nametable wiring");
    }

    test_fixture_free(&f);
    return TEST_PASS;
}

int test_mapper230_contra_mode(void) {
    test_rom_spec_t spec;
    mapper_fill_spec(&spec, 230, 40, 0);         /* 640KB PRG (40 x 16KB) + CHR-RAM, like the ROM */
    spec.fill = TEST_ROM_FILL_RANDOM;            /* distinct banks so the slots can be compared */
    test_fixture_t f;
    TEST_CHECK(test_fixture_make(&f, &spec));
    nes_t* nes = f.nes;
    uint8_t* const prg = nes->nes_rom.prg_rom;

    /* prg_banks[] are 8KB windows: 0 = $8000, 2 = $C000 - the two 16KB halves. */
    TEST_EQ_U32(0u, (uint32_t)(nes->nes_cpu.prg_banks[0] - prg) / 16384u);
    TEST_EQ_U32(7u, (uint32_t)(nes->nes_cpu.prg_banks[2] - prg) / 16384u);

    /* Contra mode boots with vertical wiring. */
    if (nes->nes_ppu.name_table[0] != nes->nes_ppu.name_table[2] ||
        nes->nes_ppu.name_table[1] != nes->nes_ppu.name_table[3] ||
        nes->nes_ppu.name_table[0] == nes->nes_ppu.name_table[1]) {
        test_fixture_free(&f);
        return mapper_report("mapper 230 boots with vertical wiring", 230,
                             "vertical nametable wiring", "wrong nametable wiring");
    }

    /* A write moves only the switchable half; the kernel stays pinned at bank 7. */
    nes_test_cpu_write(nes, 0x8000, 0x03u);
    TEST_EQ_U32(3u, (uint32_t)(nes->nes_cpu.prg_banks[0] - prg) / 16384u);
    TEST_EQ_U32(7u, (uint32_t)(nes->nes_cpu.prg_banks[2] - prg) / 16384u);

    /* Contra mode masks the bank to three bits (0x3F -> bank 7, still not bank 39). */
    nes_test_cpu_write(nes, 0x8000, 0x3Fu);
    TEST_EQ_U32(7u, (uint32_t)(nes->nes_cpu.prg_banks[0] - prg) / 16384u);
    TEST_EQ_U32(7u, (uint32_t)(nes->nes_cpu.prg_banks[2] - prg) / 16384u);

    /* This is a CHR-RAM board, so the pattern window must be mapped even with no CHR ROM. */
    if (nes->nes_ppu.pattern_table[0] == NULL) {
        test_fixture_free(&f);
        return mapper_report("mapper 230 maps its CHR-RAM window", 230,
                             "pattern table mapped", "CHR window lost");
    }

    test_fixture_free(&f);
    return TEST_PASS;
}

int test_mapper231_bmc_20in1(void) {
    test_rom_spec_t spec;
    mapper_fill_spec(&spec, 231, 32, 0);         /* 512KB PRG (32 x 16KB) + CHR-RAM, like the ROM */
    spec.fill = TEST_ROM_FILL_RANDOM;            /* distinct banks so the slots can be compared */
    test_fixture_t f;
    TEST_CHECK(test_fixture_make(&f, &spec));
    nes_t* nes = f.nes;
    uint8_t* const prg = nes->nes_rom.prg_rom;

    /* prg_banks[] are 8KB windows: 0 = $8000, 2 = $C000 - the two 16KB halves. */
    /* Power-on: both halves on bank 0, vertical wiring. */
    TEST_EQ_U32(0u, (uint32_t)(nes->nes_cpu.prg_banks[0] - prg) / 16384u);
    TEST_EQ_U32(0u, (uint32_t)(nes->nes_cpu.prg_banks[2] - prg) / 16384u);

    /* Address bit 5 is bank bit 0: $8020 selects bank 1, so $8000-$BFFF stays on the even bank 0. */
    nes_test_cpu_write(nes, 0x8020, 0x00u);
    TEST_EQ_U32(0u, (uint32_t)(nes->nes_cpu.prg_banks[0] - prg) / 16384u);
    TEST_EQ_U32(1u, (uint32_t)(nes->nes_cpu.prg_banks[2] - prg) / 16384u);

    /* Address bits 1-4 are the upper bank bits: $8002 selects bank 2 in both halves. */
    nes_test_cpu_write(nes, 0x8002, 0x00u);
    TEST_EQ_U32(2u, (uint32_t)(nes->nes_cpu.prg_banks[0] - prg) / 16384u);
    TEST_EQ_U32(2u, (uint32_t)(nes->nes_cpu.prg_banks[2] - prg) / 16384u);

    /* The top of the 512KB ROM: bit 5 + bits 1-4 set -> bank 31, so $8000-$BFFF holds bank 30
       (this is the pair the reset vector lives in). */
    nes_test_cpu_write(nes, 0x803E, 0x00u);
    TEST_EQ_U32(30u, (uint32_t)(nes->nes_cpu.prg_banks[0] - prg) / 16384u);
    TEST_EQ_U32(31u, (uint32_t)(nes->nes_cpu.prg_banks[2] - prg) / 16384u);

    /* Mirroring comes from address bit 7, not bit 5. */
    nes_test_cpu_write(nes, 0x8082, 0x00u);      /* bit 7 set -> horizontal */
    if (nes->nes_ppu.name_table[0] != nes->nes_ppu.name_table[1] ||
        nes->nes_ppu.name_table[2] != nes->nes_ppu.name_table[3] ||
        nes->nes_ppu.name_table[0] == nes->nes_ppu.name_table[2]) {
        test_fixture_free(&f);
        return mapper_report("mapper 231 mirroring comes from address bit 7", 231,
                             "horizontal wiring when bit 7 is set", "wrong nametable wiring");
    }
    nes_test_cpu_write(nes, 0x8002, 0x00u);      /* bit 7 clear -> vertical */
    if (nes->nes_ppu.name_table[0] != nes->nes_ppu.name_table[2] ||
        nes->nes_ppu.name_table[1] != nes->nes_ppu.name_table[3] ||
        nes->nes_ppu.name_table[0] == nes->nes_ppu.name_table[1]) {
        test_fixture_free(&f);
        return mapper_report("mapper 231 mirroring returns to vertical", 231,
                             "vertical wiring when bit 7 is clear", "wrong nametable wiring");
    }

    /* This is a CHR-RAM board, so the pattern window must be mapped even with no CHR ROM. */
    if (nes->nes_ppu.pattern_table[0] == NULL) {
        test_fixture_free(&f);
        return mapper_report("mapper 231 maps its CHR-RAM window", 231,
                             "pattern table mapped", "CHR window lost");
    }

    test_fixture_free(&f);
    return TEST_PASS;
}

int test_mapper57_dendy_registers(void) {
    test_rom_spec_t spec;
    mapper_fill_spec(&spec, 57, 8, 16);          /* 128KB PRG (8 x 16KB) + 128KB CHR (16 x 8KB) */
    spec.fill = TEST_ROM_FILL_RANDOM;            /* distinct banks so the slots can be compared */
    test_fixture_t f;
    TEST_CHECK(test_fixture_make(&f, &spec));
    nes_t* nes = f.nes;
    uint8_t* const prg = nes->nes_rom.prg_rom;
    uint8_t* const chr = nes->nes_rom.chr_rom;

    /* Power-on: both registers clear -> vertical mirroring, bank 0 in both 16KB halves.
       Note prg_banks[] holds 8KB windows (0 = $8000, 2 = $C000), so the halves are slots 0 and 2. */
    TEST_EQ_U32(0u, (uint32_t)(nes->nes_ppu.pattern_table[0] - chr) / 8192u);
    TEST_EQ_U32(0u, (uint32_t)(nes->nes_cpu.prg_banks[0] - prg) / 16384u);
    TEST_EQ_U32(0u, (uint32_t)(nes->nes_cpu.prg_banks[2] - prg) / 16384u);

    /* Register 0 ($8000) bit 6 and bit 0 both feed the CHR bank: 8 | 1 = 9. */
    nes_test_cpu_write(nes, 0x8000, 0x41u);
    TEST_EQ_U32(9u, (uint32_t)(nes->nes_ppu.pattern_table[0] - chr) / 8192u);

    /* Register 1 ($8800) bit 3 set -> horizontal wiring: NT0/NT1 share a page, NT2/NT3 the other. */
    nes_test_cpu_write(nes, 0x8800, 0x08u);
    if (nes->nes_ppu.name_table[0] != nes->nes_ppu.name_table[1] ||
        nes->nes_ppu.name_table[2] != nes->nes_ppu.name_table[3] ||
        nes->nes_ppu.name_table[0] == nes->nes_ppu.name_table[2]) {
        test_fixture_free(&f);
        return mapper_report("mapper 57 mirroring comes from register 1 bit 3", 57,
                             "horizontal wiring when bit 3 is set", "wrong nametable wiring");
    }

    /* ...and register 1 is mirrored at $9800 as well: clearing bit 3 gives vertical wiring. */
    nes_test_cpu_write(nes, 0x9800, 0x00u);
    if (nes->nes_ppu.name_table[0] != nes->nes_ppu.name_table[2] ||
        nes->nes_ppu.name_table[1] != nes->nes_ppu.name_table[3] ||
        nes->nes_ppu.name_table[0] == nes->nes_ppu.name_table[1]) {
        test_fixture_free(&f);
        return mapper_report("mapper 57 register 1 is mirrored at $9800", 57,
                             "vertical wiring after clearing bit 3", "wrong nametable wiring");
    }

    /* 32KB mode (bit 4, bank bits 5-6 = 10 -> even bank 2) loads banks 2 and 3. */
    nes_test_cpu_write(nes, 0x8800, 0x50u);
    TEST_EQ_U32(2u, (uint32_t)(nes->nes_cpu.prg_banks[0] - prg) / 16384u);
    TEST_EQ_U32(3u, (uint32_t)(nes->nes_cpu.prg_banks[2] - prg) / 16384u);

    /* 16KB mode (bit 4 clear, bank 3) mirrors bank 3 into both halves. */
    nes_test_cpu_write(nes, 0x8800, 0x60u);
    TEST_EQ_U32(3u, (uint32_t)(nes->nes_cpu.prg_banks[0] - prg) / 16384u);
    TEST_EQ_U32(3u, (uint32_t)(nes->nes_cpu.prg_banks[2] - prg) / 16384u);

    /* Register 1 bits 0-2 also take part in the CHR bank: (0x41 | 0x60) & 7 = 1, plus 8 = 9. */
    TEST_EQ_U32(9u, (uint32_t)(nes->nes_ppu.pattern_table[0] - chr) / 8192u);

    test_fixture_free(&f);
    return TEST_PASS;
}

int test_mapper32_irem_g101_prg_mode(void) {
    test_rom_spec_t spec;
    mapper_fill_spec(&spec, 32, 16, 4);          /* 256KB PRG = 32 x 8KB banks, plus some CHR */
    spec.fill = TEST_ROM_FILL_RANDOM;            /* distinct banks so the offsets can be compared */
    test_fixture_t f;
    TEST_CHECK(test_fixture_make(&f, &spec));
    nes_t* nes = f.nes;
    uint8_t* const prg = nes->nes_rom.prg_rom;

    /* Power-on: $8000/$A000 on bank 0, the last two slots fixed. */
    static const struct { uint8_t slot, bank; } boot[] = { { 0u, 0u }, { 1u, 0u }, { 2u, 30u }, { 3u, 31u } };
    for (size_t i = 0; i < sizeof(boot) / sizeof(boot[0]); ++i) {
        if (nes->nes_cpu.prg_banks[boot[i].slot] != prg + (size_t)boot[i].bank * 8192u) {
            test_fixture_free(&f);
            return mapper_report("mapper 32 power-on PRG layout", 32,
                                 "$8000/$A000 free, $C000/$E000 on the last two banks", "wrong banks");
        }
    }

    /* Mode 0: $8000 and $A000 are switchable, $C000/$E000 fixed. */
    nes_test_cpu_write(nes, 0x8000, 5u);
    nes_test_cpu_write(nes, 0xA000, 7u);
    if (nes->nes_cpu.prg_banks[0] != prg + 5u * 8192u || nes->nes_cpu.prg_banks[1] != prg + 7u * 8192u ||
        nes->nes_cpu.prg_banks[2] != prg + 30u * 8192u || nes->nes_cpu.prg_banks[3] != prg + 31u * 8192u) {
        test_fixture_free(&f);
        return mapper_report("mapper 32 mode 0 PRG banks", 32,
                             "$8000/$A000 switchable, $C000/$E000 fixed", "wrong banks");
    }

    /* Mode 1 ($9000 bit 1): $8000 goes to the second-to-last bank, $C000 follows the register. */
    nes_test_cpu_write(nes, 0x9000, 0x02u);
    if (nes->nes_cpu.prg_banks[0] != prg + 30u * 8192u || nes->nes_cpu.prg_banks[2] != prg + 5u * 8192u ||
        nes->nes_cpu.prg_banks[1] != prg + 7u * 8192u || nes->nes_cpu.prg_banks[3] != prg + 31u * 8192u) {
        test_fixture_free(&f);
        return mapper_report("mapper 32 mode 1 PRG banks", 32,
                             "$8000 fixed to second-to-last, $C000 switchable", "wrong banks");
    }

    /* Back to mode 0 and the register must drive $8000 again. */
    nes_test_cpu_write(nes, 0x9000, 0x00u);
    if (nes->nes_cpu.prg_banks[0] != prg + 5u * 8192u || nes->nes_cpu.prg_banks[2] != prg + 30u * 8192u) {
        test_fixture_free(&f);
        return mapper_report("mapper 32 returns to mode 0", 32,
                             "$8000 switchable again", "mode bit stayed set");
    }

    test_fixture_free(&f);
    return TEST_PASS;
}

int test_mapper3_cnrom_chr_bank(void) {
    test_rom_spec_t spec;
    mapper_fill_spec(&spec, 3, 2, 4);            /* 32KB PRG + 32KB CHR = four 8KB banks */
    spec.fill = TEST_ROM_FILL_RANDOM;            /* distinct banks so the slots can be compared */
    test_fixture_t f;
    TEST_CHECK(test_fixture_make(&f, &spec));
    nes_t* nes = f.nes;
    uint8_t* const chr = nes->nes_rom.chr_rom;

    /* Power-on selects CHR bank 0. */
    if (nes->nes_ppu.pattern_table[0] != chr) {
        test_fixture_free(&f);
        return mapper_report("mapper 3 powers up on CHR bank 0", 3,
                             "pattern table points at CHR bank 0", "wrong CHR bank at power-on");
    }

    /* CNROM only implements the bits the board's CHR can hold, so the value wraps at four banks. */
    static const struct { uint8_t value, bank; } cases[] = {
        { 0x00u, 0u }, { 0x01u, 1u }, { 0x02u, 2u }, { 0x03u, 3u }, { 0xFFu, 3u },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        nes_test_cpu_write(nes, 0x8000, cases[i].value);
        if (nes->nes_ppu.pattern_table[0] != chr + (size_t)cases[i].bank * 8192u) {
            test_fixture_free(&f);
            return mapper_report("mapper 3 CHR bank from the written value", 3,
                                 "8KB CHR bank selected by the write (wrapping)", "CHR bank ignored");
        }
    }
    test_fixture_free(&f);

    /* A CHR-RAM board (no CHR ROM at all) must ignore the register instead of banking nothing. */
    mapper_fill_spec(&spec, 3, 2, 0);
    spec.fill = TEST_ROM_FILL_RANDOM;
    TEST_CHECK(test_fixture_make(&f, &spec));
    nes = f.nes;
    nes_test_cpu_write(nes, 0x8000, 0x03u);
    if (nes->nes_ppu.pattern_table[0] == NULL) {
        test_fixture_free(&f);
        return mapper_report("mapper 3 keeps CHR-RAM mapped", 3,
                             "pattern table still mapped", "CHR window lost");
    }
    test_fixture_free(&f);
    return TEST_PASS;
}

int test_mapper121_protection(void) {
    test_rom_spec_t spec;
    mapper_fill_spec(&spec, 121, 16, 64);        /* 256KB PRG + 512KB CHR, like sf97.nes */
    spec.fill = TEST_ROM_FILL_RANDOM;
    test_fixture_t f;
    TEST_CHECK(test_fixture_make(&f, &spec));
    nes_t* nes = f.nes;
    uint8_t* const chr = nes->nes_rom.chr_rom;

    /* $5000-$5FFF answers with the {83 83 42 00} table entry the program last selected. */
    nes_test_cpu_write(nes, 0x5000, 0x00);
    TEST_EQ_U32(0x83u, nes_test_cpu_read(nes, 0x5000));
    nes_test_cpu_write(nes, 0x5FFF, 0x02);
    TEST_EQ_U32(0x42u, nes_test_cpu_read(nes, 0x5000));
    /* Other addresses in that window stay open bus. */
    TEST_EQ_U32(0x00u, nes_test_cpu_read(nes, 0x4800));

    /* Bank select $86 = PRG register 6, then a latch value $28 stores the reversed bank. */
    nes_test_cpu_write(nes, 0x8000, 0x86);       /* MMC3: select R6 */
    nes_test_cpu_write(nes, 0x8001, 0x0A);       /* reversed(0x0A) = 0x28 -> accepted */
    nes_test_cpu_write(nes, 0x8003, 0x28);       /* arm the latch */
    /* With the latch armed $E000 gets the derived bank; before that it was the last 8KB. */
    if (nes_test_cpu_read(nes, 0xE000) == nes->nes_rom.prg_rom[(size_t)(16u * 2u - 1u) * 8192u]) {
        test_fixture_free(&f);
        return mapper_report("mapper 121 protection overrides the upper PRG slot", 121,
                             "derived bank at $E000", "still the boot bank");
    }

    /* CHR: a 512KB CHR ROM needs 512 1KB banks (a uint8_t counter would truncate to 0), and the
       half the MMC3 CHR mode selects is taken from the far 256KB. */
    nes_test_cpu_write(nes, 0x8000, 0x00);       /* CHR mode 0 (bit7 clear), select R0 */
    nes_test_cpu_write(nes, 0x8001, 0x40);       /* R0 = 2KB bank 0x40 -> 1KB banks 0x40/0x41 */
    if (nes->nes_ppu.pattern_table[0] != chr + (size_t)(0x40u | 0x100u) * 1024u) {
        test_fixture_free(&f);
        return mapper_report("mapper 121 CHR from the far 256KB half", 121,
                             "bank 0x140 of the CHR ROM", "first half");
    }
    test_fixture_free(&f);
    return TEST_PASS;
}

/*
 * Sunsoft FME-7 (mapper 69): $C000-$FFFF belongs to the 5B audio chip (register select at $C000,
 * data at $E000) and the mapper must not decode those writes.  Mr Gimmick writes $E000 while
 * register 9 is selected, so treating it as a parameter write paged bank $38 into $8000-$9FFF;
 * the game then ran into garbage (gray screen, PC stuck at $64ED, rendering never enabled).
 * $A000-$BFFF stays the parameter register, mirrors included ($A917 is used by the game).
 */
int test_mapper69_5b_audio_write(void) {
    test_rom_spec_t spec;
    mapper_fill_spec(&spec, 69, 16, 16);         /* 256KB PRG + 128KB CHR, like Mr Gimmick */
    test_fixture_t f;
    TEST_CHECK(test_fixture_make(&f, &spec));
    nes_t* nes = f.nes;
    uint8_t* const prg = nes->nes_rom.prg_rom;

    nes_test_cpu_write(nes, 0x8000, 0x09);       /* select register 9: PRG bank at $8000 */
    nes_test_cpu_write(nes, 0xA000, 0x03);       /* bank 3 */
    TEST_EQ_U32(prg[3 * 8192], nes_test_cpu_read(nes, 0x8000));

    /* 5B audio writes must leave the mapper alone. */
    nes_test_cpu_write(nes, 0xE000, 0x38);
    TEST_EQ_U32(prg[3 * 8192], nes_test_cpu_read(nes, 0x8000));
    nes_test_cpu_write(nes, 0xC000, 0x07);
    TEST_EQ_U32(prg[3 * 8192], nes_test_cpu_read(nes, 0x8000));

    /* The parameter register itself still answers, mirrors included. */
    nes_test_cpu_write(nes, 0xA917, 0x05);
    TEST_EQ_U32(prg[5 * 8192], nes_test_cpu_read(nes, 0x8000));

    test_fixture_free(&f);
    return TEST_PASS;
}

/*
 * FFE F8xxx (mapper 17) power-on layout.  The board comes up Mapper-6 compatible: the last 16KB is
 * fixed at $C000-$FFFF (slot2 = bank 14, slot3 = bank 15) and $8000-$BFFF holds banks 0/1.
 * Q版沙罗曼蛇 (Chinese trainer hack of Parodius) calls $C542 without ever writing $4506 and expects
 * the bank-14 library there; with slot2 = bank 0 that address holds "03 00 00", so the call ran
 * into a BRK storm and the game never enabled rendering (gray screen).  Batman sets $4506 itself,
 * which is why it never saw the difference.
 */
int test_mapper17_power_on_slots(void) {
    test_rom_spec_t spec;
    mapper_fill_spec(&spec, 17, 8, 16);          /* 128KB PRG (16 x 8KB) + 128KB CHR, like both games */
    spec.fill = TEST_ROM_FILL_RANDOM;            /* distinct banks: the slots are compared byte-wise */
    test_fixture_t f;
    TEST_CHECK(test_fixture_make(&f, &spec));
    nes_t* nes = f.nes;
    uint8_t* const prg = nes->nes_rom.prg_rom;
    uint16_t banks = (uint16_t)(nes->nes_rom.prg_rom_size * 2u);   /* 8KB banks */

    TEST_EQ_U32(prg[0 * 8192], nes_test_cpu_read(nes, 0x8000));                    /* slot0 = 0 */
    TEST_EQ_U32(prg[1 * 8192], nes_test_cpu_read(nes, 0xA000));                    /* slot1 = 1 */
    TEST_EQ_U32(prg[(banks - 2u) * 8192], nes_test_cpu_read(nes, 0xC000));         /* slot2 = last-1 */
    TEST_EQ_U32(prg[(banks - 1u) * 8192], nes_test_cpu_read(nes, 0xE000));         /* slot3 = last  */

    /* $4506 still switches slot2, and $4507 slot3 (the register map itself is unchanged). */
    nes_test_cpu_write(nes, 0x4506, 0x05);
    TEST_EQ_U32(prg[5 * 8192], nes_test_cpu_read(nes, 0xC000));
    nes_test_cpu_write(nes, 0x4507, 0x03);
    TEST_EQ_U32(prg[3 * 8192], nes_test_cpu_read(nes, 0xE000));

    test_fixture_free(&f);
    return TEST_PASS;
}

int test_mapper_write_storm(void) {
    int failures = 0;
    for (int mapper = 0; mapper < 256; ++mapper) {
        if (!mapper_is_supported(mapper)) continue;
        test_rom_spec_t spec;
        mapper_fill_spec(&spec, (uint16_t)mapper, 4, 1);
        test_fixture_t f;
        if (!test_fixture_make(&f, &spec)) {
            failures += mapper_fail("storm fixture", (uint16_t)mapper, 0, "failed");
            continue;
        }
        nes_t* nes = f.nes;
        int broken = 0;
        for (unsigned data = 0; data < 256 && !broken; ++data) {
            for (int a = 0; a < MAPPER_ADDR_COUNT; ++a) {
                nes_test_cpu_write(nes, mapper_storm_addrs[a], (uint8_t)data);
            }
            if (nes_test_bank_check(nes) != NES_OK) {
                char detail[32];
                snprintf(detail, sizeof(detail), "data=0x%02X", data);
                failures += mapper_fail("write storm left a NULL bank", (uint16_t)mapper, 0, detail);
                broken = 1;
            }
        }
        /* The board must still be able to run after the storm. */
        if (!broken && nes_test_run_frames(nes, 2) != NES_OK) {
            failures += mapper_fail("post storm frames", (uint16_t)mapper, 0, "error");
        }
        test_fixture_free(&f);
        TEST_ABORT_OR_FAIL();
    }
    printf("    %d write-storm failure(s)\n", failures);
    return failures == 0 ? TEST_PASS : TEST_FAIL;
}

int test_mapper_bank_stress(void) {
    /* MMC3 with 32 x 8KB CHR = 256 x 1KB banks: the classic uint8_t overflow. */
    test_rom_spec_t spec;
    mapper_fill_spec(&spec, 4, 8, 32);
    spec.fill = TEST_ROM_FILL_RANDOM;
    test_fixture_t f;
    TEST_CHECK(test_fixture_make(&f, &spec));
    TEST_EQ_U32(32, f.nes->nes_rom.chr_rom_size);
    TEST_CHECK(nes_test_bank_check(f.nes) == NES_OK);
    TEST_CHECK(nes_test_bank_crc(f.nes) != 0);

    /* Selecting the highest 1KB CHR bank must land inside the ROM. */
    nes_test_cpu_write(f.nes, 0x8000, 0x00);        /* bank select: R0 */
    nes_test_cpu_write(f.nes, 0x8001, 0xFF);
    TEST_CHECK(f.nes->nes_ppu.pattern_table[0] >= f.nes->nes_rom.chr_rom);
    TEST_CHECK(f.nes->nes_ppu.pattern_table[0] < f.nes->nes_rom.chr_rom + 32u * 8192u);
    TEST_CHECK(nes_test_bank_check(f.nes) == NES_OK);
    test_fixture_free(&f);
    return TEST_PASS;
}

/* Mapper 51 (BMC 11-in-1): mode/bank come from the *address*, and $6000-$7FFF is a
   PRG-ROM window.  Authority: Mesen2 Core/NES/Mappers/Unlicensed/Bmc51.h */
int test_mapper51_bmc(void) {
    test_rom_spec_t spec;
    mapper_fill_spec(&spec, 51, 32, 0);          /* 512KB PRG (64 x 8KB) + CHR-RAM */
    spec.fill = TEST_ROM_FILL_RANDOM;
    test_fixture_t f;
    TEST_CHECK(test_fixture_make(&f, &spec));
    nes_t* nes = f.nes;
    uint8_t* const prg = nes->nes_rom.prg_rom;

    /* Power-on: bank 0, mode 1 -> 32KB of bank 0 (8KB pages 0-3), vertical wiring. */
    for (uint8_t slot = 0; slot < 4; slot++) {
        TEST_EQ_U32((uint32_t)slot, (uint32_t)(nes->nes_cpu.prg_banks[slot] - prg) / 8192u);
    }
    if (nes->nes_ppu.name_table[0] != nes->nes_ppu.name_table[2] ||
        nes->nes_ppu.name_table[1] != nes->nes_ppu.name_table[3] ||
        nes->nes_ppu.name_table[0] == nes->nes_ppu.name_table[1]) {
        test_fixture_free(&f);
        return mapper_report("mapper 51 powers up on 32KB bank 0", 51,
                             "vertical wiring, 8KB banks 0-3", "wrong power-on state");
    }

    /* $6000 write clears mode bit 0: 16KB at $8000 = 8KB pages 0/1, $C000 = pages 14/15. */
    nes_test_cpu_write(nes, 0x6000, 0x00u);
    TEST_EQ_U32(0u, (uint32_t)(nes->nes_cpu.prg_banks[0] - prg) / 8192u);
    TEST_EQ_U32(14u, (uint32_t)(nes->nes_cpu.prg_banks[2] - prg) / 8192u);

    /* $C000-$DFFF: bank = value & 0x0F, keeping mode bit 0 (still 0) -> bank 5 = 8KB pages 20/21
       and the fixed second half (5 << 1) | 7 = 15 (16KB units) -> 8KB pages 30/31. */
    nes_test_cpu_write(nes, 0xC005, 0x05u);
    TEST_EQ_U32(20u, (uint32_t)(nes->nes_cpu.prg_banks[0] - prg) / 8192u);
    TEST_EQ_U32(30u, (uint32_t)(nes->nes_cpu.prg_banks[2] - prg) / 8192u);
    /* In 16KB mode the $6000 window is PRG page 0x2F | (bank << 2) = 0x3F. */
    TEST_EQ_U32((uint32_t)prg[0x3Fu * 8192u], nes_test_cpu_read(nes, 0x6000u));

    /* $8000-$BFFF: bank only (mode keeps bit 0). */
    nes_test_cpu_write(nes, 0x8000, 0x08u);
    TEST_EQ_U32(32u, (uint32_t)(nes->nes_cpu.prg_banks[0] - prg) / 8192u);

    /* $6000 write with bit 1 set -> mode = 1 (32KB), bank stays 8 -> pages 32-35. */
    nes_test_cpu_write(nes, 0x6000, 0x02u);
    TEST_EQ_U32(32u, (uint32_t)(nes->nes_cpu.prg_banks[0] - prg) / 8192u);
    TEST_EQ_U32(35u, (uint32_t)(nes->nes_cpu.prg_banks[3] - prg) / 8192u);

    /* The $6000-$7FFF window shows PRG page 0x23 | (bank << 2) = 0x23 outright. */
    TEST_EQ_U32((uint32_t)prg[0x23u * 8192u], nes_test_cpu_read(nes, 0x6000u));
    TEST_EQ_U32((uint32_t)prg[0x23u * 8192u + 0x1FFFu], nes_test_cpu_read(nes, 0x7FFFu));

    test_fixture_free(&f);
    return TEST_PASS;
}

/* Mapper 142 (Kaiser 202): 8KB PRG pages, fixed last page, CPU-clocked IRQ and a
   $6000-$7FFF window that is work RAM unless the use-ROM bit is set.
   Authority: Mesen2 Core/NES/Mappers/Kaiser/Kaiser202.h */
int test_mapper142_kaiser(void) {
    test_rom_spec_t spec;
    mapper_fill_spec(&spec, 142, 8, 0);          /* 128KB PRG (16 x 8KB) + CHR-RAM */
    spec.save = 0;                               /* the board RAM is not a battery */
    spec.fill = TEST_ROM_FILL_RANDOM;
    test_fixture_t f;
    TEST_CHECK(test_fixture_make(&f, &spec));
    nes_t* nes = f.nes;
    uint8_t* const prg = nes->nes_rom.prg_rom;

    /* Every board with a RAM window must own that RAM even without a battery bit. */
    TEST_CHECK(nes->nes_rom.sram != NULL);
    if (nes->nes_rom.sram != NULL) {
        /* The window is a full 8KB, so $6000 and $7FFF are different cells. */
        nes_test_cpu_write(nes, 0x6000u, 0x5Au);
        nes_test_cpu_write(nes, 0x7FFFu, 0xA5u);
        TEST_EQ_U32(0x5Au, nes_test_cpu_read(nes, 0x6000u));
        TEST_EQ_U32(0xA5u, nes_test_cpu_read(nes, 0x7FFFu));
    }

    /* Power-on: slots 0-2 on page 0, slot 3 hardwired to the last 8KB page (15). */
    TEST_EQ_U32(0u, (uint32_t)(nes->nes_cpu.prg_banks[0] - prg) / 8192u);
    TEST_EQ_U32(15u, (uint32_t)(nes->nes_cpu.prg_banks[3] - prg) / 8192u);

    /* $E000 = 1 selects PRG bank 0, $E000 = 2 selects bank 1 (register index is value-1). */
    nes_test_cpu_write(nes, 0xE000u, 0x01u);
    nes_test_cpu_write(nes, 0xF000u, 0x05u);
    TEST_EQ_U32(5u, (uint32_t)(nes->nes_cpu.prg_banks[0] - prg) / 8192u);
    nes_test_cpu_write(nes, 0xE000u, 0x02u);
    nes_test_cpu_write(nes, 0xF000u, 0x07u);
    TEST_EQ_U32(7u, (uint32_t)(nes->nes_cpu.prg_banks[1] - prg) / 8192u);

    /* IRQ: reload = 4 ($8000 low nibble), $C000 bit 1 enables and reloads the counter.
       The counter fires when it reaches $FFFF, i.e. after 65531 clocks. */
    nes_test_cpu_write(nes, 0x8000u, 0x04u);
    nes->nes_cpu.irq_pending = 0;
    nes_test_cpu_write(nes, 0xC000u, 0x02u);
    nes->nes_mapper.mapper_cpu_clock(nes, 0xFFFAu);
    TEST_EQ_U32(0u, nes->nes_cpu.irq_pending);
    nes->nes_mapper.mapper_cpu_clock(nes, 2u);
    TEST_EQ_U32(1u, nes->nes_cpu.irq_pending);
    /* Firing reloads the counter and clears its enable bit (one-shot). */
    nes->nes_cpu.irq_pending = 0;
    nes->nes_mapper.mapper_cpu_clock(nes, 0xFFFAu);
    TEST_EQ_U32(0u, nes->nes_cpu.irq_pending);

    test_fixture_free(&f);
    return TEST_PASS;
}

/* Mapper 149 (Sachen SA-0036): the 8KB CHR bank is bit 7 of the written byte.
   Authority: Mesen2 Core/NES/Mappers/Sachen/Sachen_149.h */
int test_mapper149_sachen_chr(void) {
    test_rom_spec_t spec;
    mapper_fill_spec(&spec, 149, 2, 2);          /* 32KB PRG + 16KB CHR (2 x 8KB) */
    spec.fill = TEST_ROM_FILL_RANDOM;
    test_fixture_t f;
    TEST_CHECK(test_fixture_make(&f, &spec));
    nes_t* nes = f.nes;
    uint8_t* const chr = nes->nes_rom.chr_rom;

    TEST_EQ_U32(0u, (uint32_t)(nes->nes_ppu.pattern_table[0] - chr) / 8192u);
    /* 0x80: bit 7 -> CHR bank 1.  The old implementation read the low bits and stayed on 0. */
    nes_test_cpu_write(nes, 0x8000u, 0x80u);
    TEST_EQ_U32(1u, (uint32_t)(nes->nes_ppu.pattern_table[0] - chr) / 8192u);
    nes_test_cpu_write(nes, 0x8000u, 0x00u);
    TEST_EQ_U32(0u, (uint32_t)(nes->nes_ppu.pattern_table[0] - chr) / 8192u);

    test_fixture_free(&f);
    return TEST_PASS;
}

/* Mapper 171 (Kaiser 7058): 32KB PRG fixed, two 4KB CHR pages picked by the address.
   Authority: Mesen2 Core/NES/Mappers/Kaiser/Kaiser7058.h */
int test_mapper171_kaiser_chr(void) {
    test_rom_spec_t spec;
    mapper_fill_spec(&spec, 171, 2, 4);          /* 32KB PRG + 32KB CHR (8 x 4KB) */
    spec.fill = TEST_ROM_FILL_RANDOM;
    test_fixture_t f;
    TEST_CHECK(test_fixture_make(&f, &spec));
    nes_t* nes = f.nes;
    uint8_t* const chr = nes->nes_rom.chr_rom;

    /* The board leaves CHR on 4KB page 0 for both slots at power-on (Mesen's Kaiser7058 only
       selects the PRG page), so the second 4KB slot starts on 1KB page 0 as well. */
    TEST_EQ_U32(0u, (uint32_t)(nes->nes_ppu.pattern_table[0] - chr) / 1024u);
    TEST_EQ_U32(0u, (uint32_t)(nes->nes_ppu.pattern_table[4] - chr) / 1024u);

    nes_test_cpu_write(nes, 0xF000u, 0x03u);     /* $F000-$F07F -> CHR page 0 */
    TEST_EQ_U32(12u, (uint32_t)(nes->nes_ppu.pattern_table[0] - chr) / 1024u);
    nes_test_cpu_write(nes, 0xF07Fu, 0x02u);     /* still the even half */
    TEST_EQ_U32(8u, (uint32_t)(nes->nes_ppu.pattern_table[0] - chr) / 1024u);
    nes_test_cpu_write(nes, 0xF080u, 0x05u);     /* $F080-$F0FF -> CHR page 1 */
    TEST_EQ_U32(20u, (uint32_t)(nes->nes_ppu.pattern_table[4] - chr) / 1024u);

    test_fixture_free(&f);
    return TEST_PASS;
}

/* Mapper 207 (Taito X1-005, alternate mirroring): nametable slots come from bit 7 of the
   two CHR bank registers, and the 128-byte $7F00 window needs the $A3 permission.
   Authority: Mesen2 Core/NES/Mappers/Taito/TaitoX1005.h (TaitoX1005(true)) */
int test_mapper207_taito_mirroring(void) {
    test_rom_spec_t spec;
    mapper_fill_spec(&spec, 207, 16, 16);        /* 256KB PRG (32 x 8KB) + 128KB CHR (128 x 1KB) */
    spec.save = 0;
    spec.fill = TEST_ROM_FILL_RANDOM;
    test_fixture_t f;
    TEST_CHECK(test_fixture_make(&f, &spec));
    nes_t* nes = f.nes;
    uint8_t* const prg = nes->nes_rom.prg_rom;
    uint8_t* const chr = nes->nes_rom.chr_rom;

    TEST_CHECK(nes->nes_rom.sram != NULL);
    /* Power-on: three switchable pages on 0 and the last 8KB fixed in slot 3. */
    TEST_EQ_U32(0u, (uint32_t)(nes->nes_cpu.prg_banks[0] - prg) / 8192u);
    TEST_EQ_U32(31u, (uint32_t)(nes->nes_cpu.prg_banks[3] - prg) / 8192u);

    /* $7EF0 = 0x8A: CHR pages 0/1 = 10/11 and NT slots 0/1 -> VRAM screen B (bit 7 set). */
    nes_test_cpu_write(nes, 0x7EF0u, 0x8Au);
    TEST_EQ_U32(10u, (uint32_t)(nes->nes_ppu.pattern_table[0] - chr) / 1024u);
    TEST_EQ_U32(11u, (uint32_t)(nes->nes_ppu.pattern_table[1] - chr) / 1024u);
    if (nes->nes_ppu.name_table[0] != nes->nes_ppu.ppu_vram[1] ||
        nes->nes_ppu.name_table[1] != nes->nes_ppu.ppu_vram[1]) {
        test_fixture_free(&f);
        return mapper_report("mapper 207 alternate mirroring (slots 0/1)", 207,
                             "both slots on VRAM screen B", "wrong nametable");
    }
    /* $7EF1 = 0x0C: CHR pages 2/3 = 12/13, NT slots 2/3 -> screen A. */
    nes_test_cpu_write(nes, 0x7EF1u, 0x0Cu);
    TEST_EQ_U32(12u, (uint32_t)(nes->nes_ppu.pattern_table[2] - chr) / 1024u);
    TEST_EQ_U32(13u, (uint32_t)(nes->nes_ppu.pattern_table[3] - chr) / 1024u);
    if (nes->nes_ppu.name_table[2] != nes->nes_ppu.ppu_vram[0] ||
        nes->nes_ppu.name_table[3] != nes->nes_ppu.ppu_vram[0]) {
        test_fixture_free(&f);
        return mapper_report("mapper 207 alternate mirroring (slots 2/3)", 207,
                             "both slots on VRAM screen A", "wrong nametable");
    }

    /* $7EF8 is closed until it holds $A3; the 128 bytes then mirror across both halves of
       $7F00-$7FFF (so $7F00 aliases $7F80, while $7F7F is a different byte). */
    nes_test_cpu_write(nes, 0x7EF8u, 0x00u);
    nes_test_cpu_write(nes, 0x7F00u, 0x5Au);
    TEST_EQ_U32(0u, nes_test_cpu_read(nes, 0x7F00u));
    nes_test_cpu_write(nes, 0x7EF8u, 0xA3u);
    nes_test_cpu_write(nes, 0x7F00u, 0x5Au);
    nes_test_cpu_write(nes, 0x7F7Fu, 0x3Cu);
    TEST_EQ_U32(0x5Au, nes_test_cpu_read(nes, 0x7F00u));
    TEST_EQ_U32(0x5Au, nes_test_cpu_read(nes, 0x7F80u));
    TEST_EQ_U32(0x3Cu, nes_test_cpu_read(nes, 0x7F7Fu));
    TEST_EQ_U32(0x3Cu, nes_test_cpu_read(nes, 0x7FFFu));

    /* $7EFE switches the third 8KB PRG page. */
    nes_test_cpu_write(nes, 0x7EFEu, 0x11u);
    TEST_EQ_U32(17u, (uint32_t)(nes->nes_cpu.prg_banks[2] - prg) / 8192u);

    test_fixture_free(&f);
    return TEST_PASS;
}

/* Mapper 244 (C&E Decathlon): the bank comes from the *data byte* through permutation
   tables.  Authority: Mesen2 Core/NES/Mappers/Unlicensed/Mapper244.h */
int test_mapper244_decathlon(void) {
    test_rom_spec_t spec;
    mapper_fill_spec(&spec, 244, 8, 8);          /* 128KB PRG (4 x 32KB) + 64KB CHR (8 x 8KB) */
    spec.fill = TEST_ROM_FILL_RANDOM;
    test_fixture_t f;
    TEST_CHECK(test_fixture_make(&f, &spec));
    nes_t* nes = f.nes;
    uint8_t* const prg = nes->nes_rom.prg_rom;
    uint8_t* const chr = nes->nes_rom.chr_rom;

    TEST_EQ_U32(0u, (uint32_t)(nes->nes_cpu.prg_banks[0] - prg) / 8192u);
    TEST_EQ_U32(0u, (uint32_t)(nes->nes_ppu.pattern_table[0] - chr) / 8192u);

    /* value 0x09: bit 3 set -> CHR, lutChr[0][1] = 1.  An address-based decode stays on 0. */
    nes_test_cpu_write(nes, 0x8000u, 0x09u);
    TEST_EQ_U32(1u, (uint32_t)(nes->nes_ppu.pattern_table[0] - chr) / 8192u);

    /* value 0x23: bit 3 clear -> PRG, lutPrg[2][3] = 3 -> 32KB page 3 = 8KB pages 12-15. */
    nes_test_cpu_write(nes, 0x8000u, 0x23u);
    TEST_EQ_U32(12u, (uint32_t)(nes->nes_cpu.prg_banks[0] - prg) / 8192u);

    /* value 0x38: bit 3 set -> CHR, lutChr[3][0] = 0. */
    nes_test_cpu_write(nes, 0x8000u, 0x38u);
    TEST_EQ_U32(0u, (uint32_t)(nes->nes_ppu.pattern_table[0] - chr) / 8192u);

    test_fixture_free(&f);
    return TEST_PASS;
}

/* Mapper 115 (Waixing MMC3 + bank extension): $4100-$7FFF extension registers and the
   $5080 protection latch read back from $5000-$5FFF.
   Authority: Mesen2 Core/NES/Mappers/Mmc3Variants/MMC3_115.h */
int test_mapper115_waixing_extension(void) {
    test_rom_spec_t spec;
    mapper_fill_spec(&spec, 115, 16, 64);        /* 256KB PRG (16 x 16KB = 32 x 8KB) + 512KB CHR */
    spec.fill = TEST_ROM_FILL_RANDOM;
    test_fixture_t f;
    TEST_CHECK(test_fixture_make(&f, &spec));
    nes_t* nes = f.nes;
    uint8_t* const prg = nes->nes_rom.prg_rom;
    uint8_t* const chr = nes->nes_rom.chr_rom;

    /* Power-on MMC3 layout: R6 = 0, R7 = 1, the last two banks fixed. */
    TEST_EQ_U32(0u, (uint32_t)(nes->nes_cpu.prg_banks[0] - prg) / 8192u);
    TEST_EQ_U32(1u, (uint32_t)(nes->nes_cpu.prg_banks[1] - prg) / 8192u);
    TEST_EQ_U32(30u, (uint32_t)(nes->nes_cpu.prg_banks[2] - prg) / 8192u);
    TEST_EQ_U32(31u, (uint32_t)(nes->nes_cpu.prg_banks[3] - prg) / 8192u);
    TEST_EQ_U32(0u, (uint32_t)(nes->nes_ppu.pattern_table[0] - chr) / 1024u);

    /* Odd extension write sets CHR page bit 8: R0 = 0 -> 1KB page 0x100. */
    nes_test_cpu_write(nes, 0x4101u, 0x01u);
    TEST_EQ_U32(256u, (uint32_t)(nes->nes_ppu.pattern_table[0] - chr) / 1024u);
    nes_test_cpu_write(nes, 0x4101u, 0x00u);
    TEST_EQ_U32(0u, (uint32_t)(nes->nes_ppu.pattern_table[0] - chr) / 1024u);

    /* Even extension write with bit 7 clear does nothing. */
    nes_test_cpu_write(nes, 0x4100u, 0x0Fu);
    TEST_EQ_U32(0u, (uint32_t)(nes->nes_cpu.prg_banks[0] - prg) / 8192u);

    /* bit 7 set, bit 5 clear: both 16KB halves take bank (value & 0x0F) = 15. */
    nes_test_cpu_write(nes, 0x4100u, 0x8Fu);
    TEST_EQ_U32(30u, (uint32_t)(nes->nes_cpu.prg_banks[0] - prg) / 8192u);
    TEST_EQ_U32(30u, (uint32_t)(nes->nes_cpu.prg_banks[2] - prg) / 8192u);

    /* bit 7 + bit 5: one 32KB block = (value & 0x0F) >> 1 = 2 -> 8KB banks 8-11. */
    nes_test_cpu_write(nes, 0x4100u, 0xA4u);
    TEST_EQ_U32(8u, (uint32_t)(nes->nes_cpu.prg_banks[0] - prg) / 8192u);
    TEST_EQ_U32(11u, (uint32_t)(nes->nes_cpu.prg_banks[3] - prg) / 8192u);

    /* The protection register is written at $5080 and read back anywhere in $5000-$5FFF. */
    nes_test_cpu_write(nes, 0x5080u, 0x5Au);
    TEST_EQ_U32(0x5Au, nes_test_cpu_read(nes, 0x5080u));
    TEST_EQ_U32(0x5Au, nes_test_cpu_read(nes, 0x5FFFu));
    /* ...and it did not disturb the PRG mapping. */
    TEST_EQ_U32(8u, (uint32_t)(nes->nes_cpu.prg_banks[0] - prg) / 8192u);

    test_fixture_free(&f);
    return TEST_PASS;
}

/* Mapper 187 (Waixing MMC3 + outer register + security latch).
   Authority: Mesen2 Core/NES/Mappers/Mmc3Variants/MMC3_187.h */
int test_mapper187_waixing_outer(void) {
    test_rom_spec_t spec;
    mapper_fill_spec(&spec, 187, 16, 64);        /* 256KB PRG (32 x 8KB) + 512KB CHR (512 x 1KB) */
    spec.fill = TEST_ROM_FILL_RANDOM;
    test_fixture_t f;
    TEST_CHECK(test_fixture_make(&f, &spec));
    nes_t* nes = f.nes;
    uint8_t* const prg = nes->nes_rom.prg_rom;
    uint8_t* const chr = nes->nes_rom.chr_rom;

    /* The extra CHR bit 8 goes to the half the CHR mode does not bank in 1KB units: with
       CHR mode 0 (bank select bit 7 clear) that is slots 0-3. */
    TEST_EQ_U32(256u, (uint32_t)(nes->nes_ppu.pattern_table[0] - chr) / 1024u);
    TEST_EQ_U32(0u, (uint32_t)(nes->nes_ppu.pattern_table[4] - chr) / 1024u);

    /* $8001 is ignored until an $8000 write arms the latch. */
    nes_test_cpu_write(nes, 0x8001u, 0x05u);
    TEST_EQ_U32(0u, (uint32_t)(nes->nes_cpu.prg_banks[0] - prg) / 8192u);
    nes_test_cpu_write(nes, 0x8000u, 0x06u);     /* R6, mode 0 */
    nes_test_cpu_write(nes, 0x8001u, 0x05u);
    TEST_EQ_U32(5u, (uint32_t)(nes->nes_cpu.prg_banks[0] - prg) / 8192u);

    /* $5000-$5FFF reads return the security table indexed by the latch. */
    TEST_EQ_U32(0x83u, nes_test_cpu_read(nes, 0x5000u));

    /* $6000 holds the outer PRG register: bit 7 set, bits 0-4 = 1, bit 5 clear ->
       both 16KB halves take 8KB page (exPage << 1) = 2. */
    nes_test_cpu_write(nes, 0x6000u, 0x81u);
    TEST_EQ_U32(2u, (uint32_t)(nes->nes_cpu.prg_banks[0] - prg) / 8192u);
    TEST_EQ_U32(2u, (uint32_t)(nes->nes_cpu.prg_banks[2] - prg) / 8192u);

    /* Any other $6000-$7FFF address must NOT touch the outer register (the 少年街霸2 probe
       shows the game storing unrelated data at $6031/$60F0/$686F). */
    nes_test_cpu_write(nes, 0x6031u, 0x00u);
    nes_test_cpu_write(nes, 0x686Fu, 0x00u);
    TEST_EQ_U32(2u, (uint32_t)(nes->nes_cpu.prg_banks[0] - prg) / 8192u);
    TEST_EQ_U32(2u, (uint32_t)(nes->nes_cpu.prg_banks[2] - prg) / 8192u);

    test_fixture_free(&f);
    return TEST_PASS;
}

/* Mapper 245 (Waixing MMC3 + 0x40 PRG block latch; 勇者斗恶龙6/7 are CHR-RAM carts with a
   fixed 4KB CHR layout).  Authority: Mesen2 Core/NES/Mappers/Mmc3Variants/MMC3_245.h */
int test_mapper245_prg_block(void) {
    test_rom_spec_t spec;
    mapper_fill_spec(&spec, 245, 64, 0);         /* 1MB PRG (128 x 8KB) + 8KB CHR-RAM */
    spec.save = 0;
    spec.fill = TEST_ROM_FILL_RANDOM;
    test_fixture_t f;
    TEST_CHECK(test_fixture_make(&f, &spec));
    nes_t* nes = f.nes;
    uint8_t* const prg = nes->nes_rom.prg_rom;
    uint8_t* const ram = nes->nes_rom.chr_rom;

    /* Power-on: R0 = 0 (no 0x40 block), the fixed slots are the last page of the first
       64-page block (0x3F = 63), not the last page of the 1MB image. */
    TEST_EQ_U32(0u, (uint32_t)(nes->nes_cpu.prg_banks[0] - prg) / 8192u);
    TEST_EQ_U32(1u, (uint32_t)(nes->nes_cpu.prg_banks[1] - prg) / 8192u);
    TEST_EQ_U32(62u, (uint32_t)(nes->nes_cpu.prg_banks[2] - prg) / 8192u);
    TEST_EQ_U32(63u, (uint32_t)(nes->nes_cpu.prg_banks[3] - prg) / 8192u);

    /* CHR-RAM cart in CHR mode 0: slots 0-3 are the first 4KB, slots 4-7 the second. */
    TEST_CHECK(ram != NULL);
    if (ram != NULL) {
        TEST_EQ_PTR(ram, nes->nes_ppu.pattern_table[0]);
        TEST_EQ_PTR(ram + 4096, nes->nes_ppu.pattern_table[4]);
    }

    /* R0 = 2 -> orValue = 0x40: R6/R7 get the 512KB block and the fixed slots follow it. */
    nes_test_cpu_write(nes, 0x8000u, 0x00u);     /* select R0, mode 0 */
    nes_test_cpu_write(nes, 0x8001u, 0x02u);
    TEST_EQ_U32(64u, (uint32_t)(nes->nes_cpu.prg_banks[0] - prg) / 8192u);
    TEST_EQ_U32(126u, (uint32_t)(nes->nes_cpu.prg_banks[2] - prg) / 8192u);
    TEST_EQ_U32(127u, (uint32_t)(nes->nes_cpu.prg_banks[3] - prg) / 8192u);

    /* CHR mode 1 swaps the two 4KB halves of the CHR-RAM. */
    nes_test_cpu_write(nes, 0x8000u, 0x80u);
    if (ram != NULL) {
        TEST_EQ_PTR(ram + 4096, nes->nes_ppu.pattern_table[0]);
        TEST_EQ_PTR(ram, nes->nes_ppu.pattern_table[4]);
    }

    test_fixture_free(&f);
    return TEST_PASS;
}
int test_mapper240_register_window(void) {
    test_rom_spec_t spec;
    mapper_fill_spec(&spec, 240, 16, 32);        /* 256KB PRG (8 x 32KB) + 256KB CHR (32 x 8KB) */
    spec.fill = TEST_ROM_FILL_RANDOM;
    test_fixture_t f;
    TEST_CHECK(test_fixture_make(&f, &spec));
    nes_t* nes = f.nes;
    uint8_t* const prg = nes->nes_rom.prg_rom;
    uint8_t* const chr = nes->nes_rom.chr_rom;

    TEST_EQ_U32(0u, (uint32_t)(nes->nes_cpu.prg_banks[0] - prg) / 8192u);
    TEST_EQ_U32(0u, (uint32_t)(nes->nes_ppu.pattern_table[0] - chr) / 8192u);

    /* $5000 (well outside the old $40FF window): PRG = 2, CHR = 10. */
    nes_test_cpu_write(nes, 0x5000u, 0x2Au);
    TEST_EQ_U32(8u, (uint32_t)(nes->nes_cpu.prg_banks[0] - prg) / 8192u);
    TEST_EQ_U32(10u, (uint32_t)(nes->nes_ppu.pattern_table[0] - chr) / 8192u);
    /* $4020 and $5FFF are the same window: 0x31 -> PRG 32KB page 3 = 8KB banks 12-15. */
    nes_test_cpu_write(nes, 0x4020u, 0x31u);
    TEST_EQ_U32(12u, (uint32_t)(nes->nes_cpu.prg_banks[0] - prg) / 8192u);
    nes_test_cpu_write(nes, 0x5FFFu, 0x12u);     /* PRG 32KB page 1 = 8KB banks 4-7, CHR page 2 */
    TEST_EQ_U32(4u, (uint32_t)(nes->nes_cpu.prg_banks[0] - prg) / 8192u);
    TEST_EQ_U32(2u, (uint32_t)(nes->nes_ppu.pattern_table[0] - chr) / 8192u);

    test_fixture_free(&f);
    return TEST_PASS;
}

/* Mapper 150 (Sachen 74LS374N): $4100 selects a register, $4101 writes it (the decode
   masks the address with $C101, so the same pair is mirrored through $7FFF).
   Authority: Mesen2 Core/NES/Mappers/Sachen/Sachen74LS374N.h */
int test_mapper150_sachen_374(void) {
    test_rom_spec_t spec;
    mapper_fill_spec(&spec, 150, 8, 8);          /* 128KB PRG (4 x 32KB) + 64KB CHR (8 x 8KB) */
    spec.fill = TEST_ROM_FILL_RANDOM;
    test_fixture_t f;
    TEST_CHECK(test_fixture_make(&f, &spec));
    nes_t* nes = f.nes;
    uint8_t* const prg = nes->nes_rom.prg_rom;
    uint8_t* const chr = nes->nes_rom.chr_rom;

    /* Power-on: 32KB page 0 in the whole window, CHR page 0. */
    TEST_EQ_U32(0u, (uint32_t)(nes->nes_cpu.prg_banks[0] - prg) / 8192u);
    TEST_EQ_U32(0u, (uint32_t)(nes->nes_ppu.pattern_table[0] - chr) / 8192u);

    /* $6000-$7FFF is NOT a data window on this board (the old implementation treated it
       as one): a stray write there must leave the mapping alone. */
    nes_test_cpu_write(nes, 0x6000u, 0x05u);
    TEST_EQ_U32(0u, (uint32_t)(nes->nes_cpu.prg_banks[0] - prg) / 8192u);

    /* regs[5] = 2 -> 32KB PRG page 2 = 8KB pages 8-11. */
    nes_test_cpu_write(nes, 0x4100u, 0x05u);
    nes_test_cpu_write(nes, 0x4101u, 0x02u);
    TEST_EQ_U32(8u, (uint32_t)(nes->nes_cpu.prg_banks[0] - prg) / 8192u);

    /* regs[6] = 1 -> CHR 8KB page ((regs[4] & 1) << 2) | 1 = 1. */
    nes_test_cpu_write(nes, 0x4100u, 0x06u);
    nes_test_cpu_write(nes, 0x4101u, 0x01u);
    TEST_EQ_U32(1u, (uint32_t)(nes->nes_ppu.pattern_table[0] - chr) / 8192u);

    /* regs[4] = 1 now -> CHR page 4 | 1 = 5, and the register write is mirrored at $4901. */
    nes_test_cpu_write(nes, 0x4900u, 0x04u);
    nes_test_cpu_write(nes, 0x4901u, 0x01u);
    TEST_EQ_U32(5u, (uint32_t)(nes->nes_ppu.pattern_table[0] - chr) / 8192u);

    /* regs[7] bits 2:1 = 1 -> horizontal wiring. */
    nes_test_cpu_write(nes, 0x4100u, 0x07u);
    nes_test_cpu_write(nes, 0x4101u, 0x02u);
    if (nes->nes_ppu.name_table[0] != nes->nes_ppu.name_table[1] ||
        nes->nes_ppu.name_table[2] != nes->nes_ppu.name_table[3] ||
        nes->nes_ppu.name_table[0] == nes->nes_ppu.name_table[2]) {
        test_fixture_free(&f);
        return mapper_report("mapper 150 mirroring from regs[7]", 150,
                             "horizontal wiring", "wrong nametable");
    }

    /* Reading $4101 hands back the selected register in the low three bits. */
    nes_test_cpu_write(nes, 0x4100u, 0x06u);
    TEST_EQ_U32(1u, nes_test_cpu_read(nes, 0x4101u) & 0x07u);
    /* ...and the idle address bus is not a register write. */
    nes_test_cpu_write(nes, 0x4200u, 0x07u);

    test_fixture_free(&f);
    return TEST_PASS;
}

/* Mapper 227 (BMC 1200-in-1 / 南晶 大富翁): the bank comes from the *address* of the
   write, CHR is not banked at all.  Authority: Mesen2 Mapper227.h */
int test_mapper227_bmc_1200in1(void) {
    test_rom_spec_t spec;
    mapper_fill_spec(&spec, 227, 64, 2);         /* 1MB PRG (64 x 16KB) + 16KB CHR */
    spec.fill = TEST_ROM_FILL_RANDOM;
    test_fixture_t f;
    TEST_CHECK(test_fixture_make(&f, &spec));
    nes_t* nes = f.nes;
    uint8_t* const prg = nes->nes_rom.prg_rom;
    uint8_t* const chr = nes->nes_rom.chr_rom;

    /* Power-on ($8000): both 16KB slots on bank 0, vertical. */
    TEST_EQ_U32(0u, (uint32_t)(nes->nes_cpu.prg_banks[0] - prg) / 8192u);
    TEST_EQ_U32(0u, (uint32_t)(nes->nes_cpu.prg_banks[2] - prg) / 8192u);
    if (nes->nes_ppu.name_table[0] != nes->nes_ppu.name_table[2] ||
        nes->nes_ppu.name_table[1] != nes->nes_ppu.name_table[3] ||
        nes->nes_ppu.name_table[0] == nes->nes_ppu.name_table[1]) {
        test_fixture_free(&f);
        return mapper_report("mapper 227 power-on", 227,
                             "vertical wiring", "wrong nametable");
    }

    /* $800C: prgBank = 3, sFlag/lFlag clear -> slot 0 = bank 3, slot 1 = bank 3 & 0x38 = 0.
       CHR must not move at all (the old implementation banked it here). */
    nes_test_cpu_write(nes, 0x800Cu, 0x00u);
    TEST_EQ_U32(6u, (uint32_t)(nes->nes_cpu.prg_banks[0] - prg) / 8192u);
    TEST_EQ_U32(0u, (uint32_t)(nes->nes_cpu.prg_banks[2] - prg) / 8192u);
    TEST_EQ_U32(0u, (uint32_t)(nes->nes_ppu.pattern_table[0] - chr) / 8192u);

    /* $8300: address bit 8 sets prgBank bit 5 and bit 9 is lFlag without sFlag
       -> slot 0 = 0x20 (8KB 64), slot 1 = 0x20 | 7 = 0x27 (8KB 78), vertical. */
    nes_test_cpu_write(nes, 0x8300u, 0x00u);
    TEST_EQ_U32(64u, (uint32_t)(nes->nes_cpu.prg_banks[0] - prg) / 8192u);
    TEST_EQ_U32(78u, (uint32_t)(nes->nes_cpu.prg_banks[2] - prg) / 8192u);

    /* $8301: the same with sFlag -> slot 0 = 0x20 & 0x3E = 0x20, slot 1 = 0x27. */
    nes_test_cpu_write(nes, 0x8301u, 0x00u);
    TEST_EQ_U32(64u, (uint32_t)(nes->nes_cpu.prg_banks[0] - prg) / 8192u);
    TEST_EQ_U32(78u, (uint32_t)(nes->nes_cpu.prg_banks[2] - prg) / 8192u);

    /* $8187: prgMode (bit 7) with sFlag -> prgBank = 33, the aligned 32KB pair is 32/33
       (8KB pages 64-67), and address bit 1 gives horizontal wiring. */
    nes_test_cpu_write(nes, 0x8187u, 0x00u);
    TEST_EQ_U32(64u, (uint32_t)(nes->nes_cpu.prg_banks[0] - prg) / 8192u);
    TEST_EQ_U32(66u, (uint32_t)(nes->nes_cpu.prg_banks[2] - prg) / 8192u);
    if (nes->nes_ppu.name_table[0] != nes->nes_ppu.name_table[1] ||
        nes->nes_ppu.name_table[2] != nes->nes_ppu.name_table[3] ||
        nes->nes_ppu.name_table[0] == nes->nes_ppu.name_table[2]) {
        test_fixture_free(&f);
        return mapper_report("mapper 227 mirroring from address bit 1", 227,
                             "horizontal wiring", "wrong nametable");
    }

    test_fixture_free(&f);
    return TEST_PASS;
}
