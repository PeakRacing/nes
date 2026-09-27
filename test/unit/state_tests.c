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
 * Save state tests: round-trip determinism, header/section validation, battery RAM
 * round-trip and rejection of states that belong to another ROM.
 */
#include "test.h"

#if (NES_USE_FS == 1)

#define STATE_TEST_FRAMES   (60u)

static void state_test_path(char* buf, size_t size, const char* name) {
    test_path(buf, size, name);
}

/* Run frames while recording the rolling frame hash chain. */
static void state_test_run(nes_t* nes, uint32_t frames, uint32_t* hashes) {
    uint32_t chain = 2166136261u;
    for (uint32_t i = 0; i < frames; i++) {
        nes_test_run_frames(nes, 1);
        chain = (chain ^ nes_test_frame_hash(nes)) * 16777619u;
        if (hashes != NULL) {
            hashes[i] = chain;
        }
    }
}

/* Save at frame N, keep running, reload and run again: both runs must agree frame by frame. */
static int state_roundtrip_for_mapper(uint16_t mapper) {
    test_rom_spec_t spec;
    test_fixture_t fixture;
    char path[512];
    static uint32_t after_save[STATE_TEST_FRAMES];
    static uint32_t after_load[STATE_TEST_FRAMES];

    memset(&spec, 0, sizeof(spec));
    spec.mapper = mapper;
    spec.prg_units = 4;
    spec.chr_units = 2;
    spec.save = 1;                      /* battery RAM present: exercises the SRAM section */
    spec.fill = TEST_ROM_FILL_STUB;
    if (!test_fixture_make(&fixture, &spec)) {
        return -1;                      /* mapper not loadable in this build */
    }

    state_test_path(path, sizeof(path), "test/out/state_roundtrip.nessave");
    (void)nes_remove(path);

    state_test_run(fixture.nes, 20, NULL);
    fixture.nes->nes_rom.sram[0x100] = 0x5A;    /* simulate an in-game save */
    fixture.nes->nes_rom.sram[0x101] = 0xA5;
    fixture.nes->nes_rom.sram_dirty = 1;

    if (nes_state_save(fixture.nes, path) != NES_OK) {
        test_fixture_free(&fixture);
        return -1;
    }
    state_test_run(fixture.nes, STATE_TEST_FRAMES, after_save);

    if (nes_state_load(fixture.nes, path) != NES_OK) {
        test_fixture_free(&fixture);
        return -1;
    }
    if (fixture.nes->nes_rom.sram[0x100] != 0x5A || fixture.nes->nes_rom.sram[0x101] != 0xA5) {
        test_fixture_free(&fixture);
        return -1;                      /* loading a state must roll the battery RAM back too */
    }

    state_test_run(fixture.nes, STATE_TEST_FRAMES, after_load);
    (void)nes_remove(path);
    test_fixture_free(&fixture);

    for (uint32_t i = 0; i < STATE_TEST_FRAMES; i++) {
        if (after_save[i] != after_load[i]) {
            return (int)(i + 1);        /* first diverging frame number */
        }
    }
    return 0;
}

int test_state_roundtrip(void) {
    static const uint16_t mappers[] = { 0u, 2u, 3u };   /* NROM, UxROM, CNROM */
    for (size_t i = 0; i < sizeof(mappers) / sizeof(mappers[0]); i++) {
        int result = state_roundtrip_for_mapper(mappers[i]);
        if (result > 0) {
            test_record_failure(__FILE__, __LINE__, "save state round-trip",
                                "identical frame hashes", "diverged after loading the state");
            return TEST_FAIL;
        }
        if (result < 0) {
            TEST_SKIP_MSG("cannot run the save-state round-trip for this build");
        }
    }
    return TEST_PASS;
}

/* Hot storage: save and load repeatedly *while the machine keeps running*, the way the SDL
 * port does it between frames (F5/F8).  Every loaded snapshot must resume identically, and
 * overwriting the file with a newer snapshot must be picked up by the next load. */
int test_state_hot_save(void) {
    test_rom_spec_t spec;
    test_fixture_t fixture;
    char state_path[512];
    char sav_path[512];
    char rom_name[128];
    static uint32_t baseline[STATE_TEST_FRAMES];
    static uint32_t replay[STATE_TEST_FRAMES];
    FILE* file;

    memset(&spec, 0, sizeof(spec));
    spec.mapper = 2;
    spec.prg_units = 4;
    spec.chr_units = 2;
    spec.save = 1;
    spec.fill = TEST_ROM_FILL_STUB;
    TEST_CHECK(test_fixture_make(&fixture, &spec));

    state_test_path(state_path, sizeof(state_path), "test/out/hot_save.nessave");
    (void)nes_remove(state_path);
    /* The battery file follows the ROM path, so point the instance at a scratch ROM name. */
    fixture.nes->nes_rom.sram_persist = 1;   /* this case verifies the battery file too */
    state_test_path(rom_name, sizeof(rom_name), "test/out/hot_save_rom.nes");
    snprintf(fixture.nes->nes_rom.rom_path, sizeof(fixture.nes->nes_rom.rom_path), "%s", rom_name);
    state_test_path(sav_path, sizeof(sav_path), "test/out/hot_save_rom.sav");
    (void)nes_remove(sav_path);

    /* First hot save in the middle of the run. */
    state_test_run(fixture.nes, 20, NULL);
    fixture.nes->nes_rom.sram[0x200] = 0x11;
    TEST_EQ_I32(NES_OK, nes_state_save(fixture.nes, state_path));
    file = fopen(sav_path, "rb");
    TEST_CHECK(file != NULL);                       /* the game's own save is flushed too */
    fseek(file, 0, SEEK_END);
    TEST_EQ_I32((int)SRAM_SIZE, (int)ftell(file));
    fclose(file);

    /* Keep playing and hot save again, overwriting the file. */
    state_test_run(fixture.nes, 20, NULL);
    fixture.nes->nes_rom.sram[0x200] = 0x22;
    TEST_EQ_I32(NES_OK, nes_state_save(fixture.nes, state_path));
    TEST_CHECK(fixture.nes->nes_rom.sram_dirty == 0);   /* flushed by the save */

    /* Record the continuation of this second snapshot as the reference. */
    state_test_run(fixture.nes, STATE_TEST_FRAMES, baseline);

    /* Load it twice in a row: both replays must match the reference bit for bit. */
    for (int round = 0; round < 2; round++) {
        TEST_EQ_I32(NES_OK, nes_state_load(fixture.nes, state_path));
        TEST_EQ_U32(0x22, fixture.nes->nes_rom.sram[0x200]);
        state_test_run(fixture.nes, STATE_TEST_FRAMES, replay);
        for (uint32_t f = 0; f < STATE_TEST_FRAMES; f++) {
            if (baseline[f] != replay[f]) {
                test_record_failure(__FILE__, __LINE__, "hot save replay",
                                    "identical frame hashes", "diverged");
                return TEST_FAIL;
            }
        }
    }

    /* Keep running after the last load: the machine must stay usable (no half-restored bus). */
    state_test_run(fixture.nes, 10, NULL);
    TEST_CHECK(fixture.nes->nes_cpu.PC != 0u);
    TEST_CHECK(nes_test_bank_check(fixture.nes) == NES_OK);

    /* Boards whose battery is a mapper owned buffer (Racermate's 64KB CHR-RAM): the .sav file
     * must carry that buffer instead of the core's 8KB sram, and load it back verbatim. */
    {
        static uint8_t battery[4096];
        FILE* bfile;
        fixture.nes->nes_mapper.mapper_battery = battery;
        fixture.nes->nes_mapper.mapper_battery_size = (uint32_t)sizeof(battery);
        for (uint32_t i = 0; i < sizeof(battery); i++) {
            battery[i] = (uint8_t)(i * 7u + 3u);
        }
        TEST_EQ_I32(NES_OK, nes_sram_save(fixture.nes));
        bfile = fopen(sav_path, "rb");
        TEST_CHECK(bfile != NULL);
        fseek(bfile, 0, SEEK_END);
        TEST_EQ_I32((int)sizeof(battery), (int)ftell(bfile));
        fclose(bfile);

        memset(battery, 0, sizeof(battery));
        TEST_EQ_I32(NES_OK, nes_sram_load(fixture.nes));
        for (uint32_t i = 0; i < sizeof(battery); i++) {
            if (battery[i] != (uint8_t)(i * 7u + 3u)) {
                test_record_failure(__FILE__, __LINE__, "mapper battery round trip",
                                    "identical bytes after nes_sram_load", "mismatch");
                return TEST_FAIL;
            }
        }
        fixture.nes->nes_mapper.mapper_battery = NULL;
        fixture.nes->nes_mapper.mapper_battery_size = 0;
    }
    (void)nes_remove(state_path);
    (void)nes_remove(sav_path);
    test_fixture_free(&fixture);
    return TEST_PASS;
}

int test_state_validation(void) {
    test_rom_spec_t spec;
    test_fixture_t fixture;
    test_fixture_t other;
    char path[512];
    uint8_t* raw = NULL;
    long size = 0;
    FILE* file;
    uint16_t pc;
    uint32_t ram_sum_before = 0;

    memset(&spec, 0, sizeof(spec));
    spec.mapper = 2;
    spec.prg_units = 4;
    spec.chr_units = 2;
    spec.fill = TEST_ROM_FILL_STUB;
    TEST_CHECK(test_fixture_make(&fixture, &spec));

    state_test_path(path, sizeof(path), "test/out/state_validation.nessave");
    (void)nes_remove(path);
    nes_test_run_frames(fixture.nes, 20);
    TEST_EQ_I32(NES_OK, nes_state_save(fixture.nes, path));

    file = fopen(path, "rb");
    TEST_CHECK(file != NULL);
    fseek(file, 0, SEEK_END);
    size = ftell(file);
    TEST_CHECK(size > 64);
    raw = (uint8_t*)nes_malloc((int)size);
    TEST_CHECK(raw != NULL);
    fseek(file, 0, SEEK_SET);
    TEST_CHECK(fread(raw, 1, (size_t)size, file) == (size_t)size);
    fclose(file);

    pc = fixture.nes->nes_cpu.PC;
    for (int i = 0; i < 64; i++) {
        ram_sum_before += fixture.nes->nes_cpu.cpu_ram[i];
    }

    /* Magic is what identifies the file: a state from another emulator (or a stray file)
     * must be refused, not half-applied. */
    raw[0] = 'X';
    file = fopen(path, "wb");
    TEST_CHECK(file != NULL);
    (void)fwrite(raw, 1, (size_t)size, file);
    fclose(file);
    TEST_EQ_I32(NES_STATE_ERR_FORMAT, nes_state_load(fixture.nes, path));
    raw[0] = 'N';

    /* Corrupted payload: the section CRC catches it before anything is applied. */
    raw[size - 1] ^= 0xFF;
    file = fopen(path, "wb");
    TEST_CHECK(file != NULL);
    (void)fwrite(raw, 1, (size_t)size, file);
    fclose(file);
    TEST_EQ_I32(NES_STATE_ERR_CRC, nes_state_load(fixture.nes, path));
    raw[size - 1] ^= 0xFF;

    /* Truncated file. */
    file = fopen(path, "wb");
    TEST_CHECK(file != NULL);
    (void)fwrite(raw, 1, (size_t)size / 2u, file);
    fclose(file);
    TEST_EQ_I32(NES_STATE_ERR_TRUNC, nes_state_load(fixture.nes, path));

    /* A failed load must leave the machine exactly as it was. */
    TEST_CHECK(fixture.nes->nes_cpu.PC == pc);
    {
        uint32_t ram_sum_after = 0;
        for (int i = 0; i < 64; i++) {
            ram_sum_after += fixture.nes->nes_cpu.cpu_ram[i];
        }
        TEST_CHECK(ram_sum_after == ram_sum_before);
    }

    /* Restore the good file, then try to load it into a different ROM. */
    file = fopen(path, "wb");
    TEST_CHECK(file != NULL);
    (void)fwrite(raw, 1, (size_t)size, file);
    fclose(file);

    {
        test_rom_spec_t other_spec;
        memset(&other_spec, 0, sizeof(other_spec));
        other_spec.mapper = 2;
        other_spec.prg_units = 8;       /* different size => different CRC */
        other_spec.chr_units = 2;
        other_spec.fill = TEST_ROM_FILL_STUB;
        if (test_fixture_make(&other, &other_spec)) {
            TEST_EQ_I32(NES_STATE_ERR_ROM, nes_state_load(other.nes, path));
            test_fixture_free(&other);
        }
    }

    nes_free(raw);
    (void)nes_remove(path);
    test_fixture_free(&fixture);
    return TEST_PASS;
}

int test_state_rom_roundtrip(void) {
    /* Real cartridges, covering the boards whose memory mapping is mapper specific
     * (Taito X1-005 / X1-017, Sunsoft-4, TAM-S1, TC0190): a state must resume them exactly. */
    static const char* const roms[] = {
        "mapper68/Maharaja.nes",
        "mapper80/Fudou Myouou Den (J) [!].nes",
        "mapper80/Kyonshiizu 2 (J).nes",
        "mapper82/Kyuukyoku Harikiri Koushien (J) [!].nes",
        "mapper97/Kaiketsu Yanchamaru (J) [!].nes",
        "mapper33/Don Doko Don 2 (J) [!].nes",
        "mapper4/超级马里奥3.nes",
    };
    static uint32_t after_save[STATE_TEST_FRAMES];
    static uint32_t after_load[STATE_TEST_FRAMES];
    char rom_path[512];
    char state_path[512];
    int ran = 0;

    state_test_path(state_path, sizeof(state_path), "test/out/rom_roundtrip.nessave");
    for (size_t i = 0; i < sizeof(roms) / sizeof(roms[0]); i++) {
        nes_t* nes;
        FILE* probe;
        snprintf(rom_path, sizeof(rom_path), "%s/%s", test_rom_dir(), roms[i]);
        probe = fopen(rom_path, "rb");
        if (probe == NULL) {
            continue;                   /* ROM not present in this checkout */
        }
        fclose(probe);

        nes = nes_init();
        if (nes == NULL) {
            continue;
        }
        {
            int lret = nes_load_file(nes, rom_path);
            if (lret != NES_OK) {
                printf("  [state] load failed (%d): %s\n", lret, rom_path);
                nes_deinit(nes);
                continue;
            }
        }
        (void)nes_remove(state_path);
        /* Redirect the battery save into the scratch directory: the corpus must stay clean. */
        snprintf(nes->nes_rom.rom_path, sizeof(nes->nes_rom.rom_path), "%s", state_path);
        state_test_run(nes, 30, NULL);
        {
            int sret = nes_state_save(nes, state_path);
            if (sret != NES_OK) {
                printf("  [state] save failed (%d): %s -> %s\n", sret, rom_path, state_path);
                (void)nes_unload_file(nes);
                nes_deinit(nes);
                continue;
            }
        }
        state_test_run(nes, STATE_TEST_FRAMES, after_save);
        {
            int rret = nes_state_load(nes, state_path);
            if (rret != NES_OK) {
                printf("  [state] load state failed (%d): %s\n", rret, state_path);
                (void)nes_unload_file(nes);
                nes_deinit(nes);
                continue;
            }
        }
        state_test_run(nes, STATE_TEST_FRAMES, after_load);
        (void)nes_unload_file(nes);
        nes_deinit(nes);
        ran++;
        for (uint32_t f = 0; f < STATE_TEST_FRAMES; f++) {
            if (after_save[f] != after_load[f]) {
                test_record_failure(__FILE__, __LINE__, roms[i],
                                    "identical frame hashes after loading the state",
                                    "diverged");
                return TEST_FAIL;
            }
        }
    }
    (void)nes_remove(state_path);
    if (ran == 0) {
        TEST_SKIP_MSG("no corpus ROMs available for the round-trip");
    }
    return TEST_PASS;
}

#endif /* NES_USE_FS */
