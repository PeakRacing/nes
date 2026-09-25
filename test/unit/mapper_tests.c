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
