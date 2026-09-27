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
 * ROM loader tests: iNES / NES 2.0 header parsing, trainer handling, SRAM
 * allocation, mapper dispatch through the image and the CRC based romdb.
 */
#include "test.h"

int test_rom_layout(void) {
    test_rom_spec_t spec;
    memset(&spec, 0, sizeof(spec));
    spec.mapper = 37;
    spec.prg_units = 2;
    spec.chr_units = 3;
    spec.trainer = 1;
    spec.save = 1;
    spec.fill = TEST_ROM_FILL_RANDOM;

    size_t size = 0;
    uint8_t* rom = test_make_ines_ex(&size, &spec);
    TEST_CHECK(rom != NULL);

    nes_test_rom_layout_t layout;
    TEST_EQ_I32(NES_TEST_ROM_OK, nes_test_parse_rom(rom, size, &layout));
    TEST_EQ_U32(37, layout.mapper_number);
    TEST_EQ_U32(2, layout.prg_rom_size);
    TEST_EQ_U32(3, layout.chr_rom_size);
    TEST_CHECK(layout.trainer);
    TEST_CHECK(layout.save_ram);
    TEST_EQ_U32(16u + TRAINER_SIZE, layout.prg_offset);
    TEST_EQ_U32(layout.chr_offset, layout.prg_offset + PRG_ROM_UNIT_SIZE * 2u);
    TEST_EQ_U32(layout.total_size, size);
    TEST_CHECK(layout.crc32 != 0);

    test_fixture_t f;
    TEST_CHECK(test_fixture_make(&f, &spec));
    /* The trainer is copied into SRAM at $7000 ($1000 into the 8KB window). */
    TEST_CHECK(f.nes->nes_rom.sram != NULL);
    TEST_EQ_U32(0x5A, f.nes->nes_rom.sram[0x1000]);
    TEST_EQ_U32(37, f.nes->nes_rom.mapper_number);
    /* No romdb entry matches this synthetic CRC, so the header mapper stands. */
    TEST_CHECK(f.nes->nes_rom.rom_crc != 0);
    TEST_CHECK(nes_test_bank_check(f.nes) == NES_OK);
    test_fixture_free(&f);
    test_free_rom(rom);
    return TEST_PASS;
}

int test_rom_errors(void) {
    uint8_t header[16];
    nes_test_rom_layout_t layout;
    memset(header, 0, sizeof(header));

    TEST_EQ_I32(NES_TEST_ROM_TRUNCATED_HEADER, nes_test_parse_rom(header, 4, &layout));
    memcpy(header, "NES\x1a", 4);
    TEST_EQ_I32(NES_TEST_ROM_TRUNCATED_PRG, nes_test_parse_rom(header, sizeof(header), &layout));
    memcpy(header, "BAD\x1a", 4);
    TEST_EQ_I32(NES_TEST_ROM_INVALID_MAGIC, nes_test_parse_rom(header, sizeof(header), &layout));
    TEST_EQ_I32(NES_TEST_ROM_INVALID_ARGUMENT, nes_test_parse_rom(NULL, sizeof(header), &layout));

    /* A CHR-ROM image whose data is missing must be rejected. */
    test_rom_spec_t spec;
    memset(&spec, 0, sizeof(spec));
    spec.mapper = 0;
    spec.prg_units = 2;
    spec.chr_units = 2;
    size_t size = 0;
    uint8_t* rom = test_make_ines_ex(&size, &spec);
    TEST_CHECK(rom != NULL);
    TEST_EQ_I32(NES_TEST_ROM_TRUNCATED_CHR, nes_test_parse_rom(rom, size - 1u, &layout));

    /* nes_load_rom() itself must refuse a bad magic. */
    nes_t* nes = nes_init();
    TEST_CHECK(nes != NULL);
    rom[0] = 'X';
    TEST_EQ_I32(NES_ERROR, nes_load_rom(nes, rom));
    nes_deinit(nes);
    test_free_rom(rom);
    return TEST_PASS;
}

int test_rom_header_variants(void) {
    /* NES 2.0 header: 12 bit sizes and mapper field. */
    test_rom_spec_t spec;
    memset(&spec, 0, sizeof(spec));
    spec.mapper = 37;
    spec.prg_units = 2;
    spec.chr_units = 3;
    spec.mirroring = 1;
    spec.save = 1;
    spec.nes2 = 1;
    spec.fill = TEST_ROM_FILL_RANDOM;

    size_t size = 0;
    uint8_t* rom = test_make_ines_ex(&size, &spec);
    TEST_CHECK(rom != NULL);

    nes_test_rom_layout_t layout;
    TEST_EQ_I32(NES_TEST_ROM_OK, nes_test_parse_rom(rom, size, &layout));
    TEST_CHECK(layout.nes2);
    TEST_EQ_U32(37, layout.mapper_number);
    TEST_EQ_U32(2, layout.prg_rom_size);
    TEST_EQ_U32(3, layout.chr_rom_size);

    test_fixture_t f;
    TEST_CHECK(test_fixture_make(&f, &spec));
    TEST_EQ_U32(37, f.nes->nes_rom.mapper_number);
    TEST_EQ_U32(1, f.nes->nes_rom.mirroring_type);
    TEST_EQ_U32(1, f.nes->nes_rom.save_ram);
    TEST_CHECK(nes_test_bank_check(f.nes) == NES_OK);
    /* Header says vertical, so the name tables are mirrored accordingly. */
    TEST_EQ_PTR(f.nes->nes_ppu.name_table[0], f.nes->nes_ppu.name_table[2]);
    test_fixture_free(&f);
    test_free_rom(rom);

    /* Dirty iNES header: bytes 12-15 non zero means only the low nibble counts. */
    memset(&spec, 0, sizeof(spec));
    spec.mapper = 37;
    spec.prg_units = 2;
    spec.chr_units = 1;
    spec.fill = TEST_ROM_FILL_RANDOM;
    rom = test_make_ines_ex(&size, &spec);
    TEST_CHECK(rom != NULL);
    rom[12] = 0x01;
    TEST_EQ_I32(NES_TEST_ROM_OK, nes_test_parse_rom(rom, size, &layout));
    TEST_EQ_U32(5, layout.mapper_number);
    test_free_rom(rom);
    return TEST_PASS;
}

int test_stream_consistency(void) {
#if (NES_ROM_STREAM == 1)
    test_rom_spec_t spec;
    memset(&spec, 0, sizeof(spec));
    spec.mapper = 0;
    spec.prg_units = 2;
    spec.chr_units = 4;
    spec.fill = TEST_ROM_FILL_STUB;

    size_t size = 0;
    uint8_t* rom = test_make_ines_ex(&size, &spec);
    TEST_CHECK(rom != NULL);

    char path[600];
    test_path(path, sizeof(path), "test/out/stream.nes");
    FILE* out = fopen(path, "wb");
    TEST_CHECK(out != NULL);
    TEST_CHECK(fwrite(rom, 1, size, out) == size);
    fclose(out);

    nes_t* nes = nes_init();
    TEST_CHECK(nes != NULL);
    TEST_EQ_I32(NES_OK, nes_load_file(nes, path));
    TEST_CHECK(nes_test_bank_check(nes) == NES_OK);
    /* The ROM stays on disk and the cache serves the banks: the image loads in
     * both streaming and non-streaming builds with identical PRG/CHR content. */
    TEST_CHECK(nes->nes_rom.rom_file != NULL);
    const uint32_t crc = nes_test_bank_crc(nes);
    TEST_CHECK(crc != 0);
    /* MMC5 extended background may address CHR beyond the resident cache.
     * Read tile rows across all pages without changing pattern mappings. */
    uint8_t* mapped[8];
    nes_memcpy(mapped, nes->nes_ppu.pattern_table, sizeof(mapped));
    const uint8_t* chr = rom + 16u + 2u * PRG_ROM_UNIT_SIZE;
    for (uint32_t address = 0; address < 4u * CHR_ROM_UNIT_SIZE; address += 1008u) {
        const uint8_t* tile = nes_chrrom_tile(nes, address);
        TEST_CHECK(nes_memcmp(tile, chr + address, 16) == 0);
        TEST_CHECK(nes_memcmp(mapped, nes->nes_ppu.pattern_table, sizeof(mapped)) == 0);
        for (int i = 0; i < 8; ++i) {
            TEST_CHECK(nes_memcmp(mapped[i], chr + (size_t)i * 1024u, 1024u) == 0);
        }
    }
    test_free_rom(rom);
    nes_unload_file(nes);
    nes_deinit(nes);
    return TEST_PASS;
#else
    printf("    (needs the nes-tests-stream target)\n");
    TEST_SKIP_MSG("built without NES_ROM_STREAM=1");
#endif
}

int test_stream_short_read(void) {
#if (NES_ROM_STREAM == 1)
    test_rom_spec_t spec;
    size_t size = 0;
    uint8_t* rom;
    char full_path[600];
    char short_path[600];
    FILE* out;
    nes_t* nes;
    const size_t prg_before_missing = 16u + (size_t)3u * PRG_ROM_UNIT_SIZE;

    memset(&spec, 0, sizeof(spec));
    spec.mapper = 0;
    spec.prg_units = 4;
    spec.chr_units = 1;
    spec.fill = TEST_ROM_FILL_STUB;
    rom = test_make_ines_ex(&size, &spec);
    TEST_CHECK(rom != NULL);

    test_path(full_path, sizeof(full_path), "test/out/stream_short_read_full.nes");
    test_path(short_path, sizeof(short_path), "test/out/stream_short_read_truncated.nes");
    out = fopen(full_path, "wb");
    TEST_CHECK(out != NULL);
    TEST_CHECK(fwrite(rom, 1, size, out) == size);
    fclose(out);
    /* Keep the header and only the first three 16KB PRG units.  The header
     * still advertises four units, so streaming discovers the short read when
     * mapper 0 maps its fixed final bank. */
    out = fopen(short_path, "wb");
    TEST_CHECK(out != NULL);
    TEST_CHECK(fwrite(rom, 1, prg_before_missing, out) == prg_before_missing);
    fclose(out);
    test_free_rom(rom);

    nes = nes_init();
    TEST_CHECK(nes != NULL);
    TEST_EQ_I32(NES_OK, nes_load_file(nes, short_path));
    TEST_EQ_I32(NES_STREAM_ERR_READ, nes_rom_stream_error(nes));
    for (int i = 0; i < NES_PRG_CACHE_SLOTS; ++i) {
        TEST_CHECK(nes->nes_rom.prg_cache[i].tag != 6u);
        TEST_CHECK(nes->nes_rom.prg_cache[i].tag != 7u);
    }
    TEST_CHECK(nes->nes_cpu.prg_banks[3] != NULL);
    TEST_EQ_U32(0u, nes_test_cpu_read(nes, 0xE000u));
    nes_unload_file(nes);
    nes_deinit(nes);

    nes = nes_init();
    TEST_CHECK(nes != NULL);
    TEST_EQ_I32(NES_OK, nes_load_file(nes, full_path));
    TEST_EQ_I32(NES_STREAM_OK, nes_rom_stream_error(nes));
    TEST_CHECK(nes_test_bank_check(nes) == NES_OK);
    const uint32_t crc_before = nes_test_bank_crc(nes);
    TEST_CHECK(crc_before != 0u);
    TEST_EQ_I32(NES_STREAM_OK, nes_load_prgrom_8k(nes, 0, 6));
    TEST_EQ_I32(NES_STREAM_OK, nes_load_prgrom_8k(nes, 1, 7));
    TEST_CHECK(nes_test_bank_check(nes) == NES_OK);
    TEST_EQ_U32(crc_before, nes_test_bank_crc(nes));
    nes_unload_file(nes);
    nes_deinit(nes);
    remove(full_path);
    remove(short_path);
    return TEST_PASS;
#else
    TEST_SKIP_MSG("built without NES_ROM_STREAM=1");
#endif
}
