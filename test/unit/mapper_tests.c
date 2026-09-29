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
