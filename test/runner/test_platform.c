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
 * Test harness runtime, fake platform port and deterministic ROM factory.
 *
 * The fake port lets the whole emulator run head-less: nes_draw() is a no-op and
 * nes_frame() only feeds the frame probe and the frame budget brake.
 */
#include "test.h"
#include <stdarg.h>
#include <time.h>
#if defined(_WIN32)
#include <windows.h>
#endif

typedef struct {
    const char* assert_file;
    int assert_line;
    char expression[160];
    char expected[64];
    char actual[64];
    char skip_reason[160];
    uint64_t started;
    uint64_t deadline;
    int skipped;
    int aborted;
} test_runtime_t;

static test_runtime_t test_runtime;
static test_options_t test_options = { NULL, NULL, NULL, NULL, NULL, 0, -1, 0, 0, 0, 0 };

static uint64_t test_now_ms(void) {
    struct timespec ts;
    if (timespec_get(&ts, TIME_UTC) != TIME_UTC) return (uint64_t)clock() * 1000u / CLOCKS_PER_SEC;
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000);
}

/* Monotonic microsecond clock for the NES_TEST_MODE hot path profiler.  A wall
 * clock must not be used here: a clock adjustment in the middle of a run turns
 * into a huge (or negative) delta and poisons every per module total. */
uint64_t nes_test_time_us(void) {
#if defined(_WIN32)
    LARGE_INTEGER freq, count;
    if (!QueryPerformanceFrequency(&freq) || freq.QuadPart == 0) return 0;
    QueryPerformanceCounter(&count);
    return (uint64_t)(count.QuadPart / freq.QuadPart) * 1000000u +
           (uint64_t)((count.QuadPart % freq.QuadPart) * 1000000u / freq.QuadPart);
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)(ts.tv_nsec / 1000);
#endif
}

void test_begin_case(unsigned timeout_ms) {
    memset(&test_runtime, 0, sizeof(test_runtime));
    test_runtime.started = test_now_ms();
    test_runtime.deadline = test_runtime.started + timeout_ms;
}

int test_should_abort(void) {
    if (test_runtime.aborted) return 1;
    if (test_runtime.deadline != 0 && test_now_ms() >= test_runtime.deadline) {
        test_runtime.aborted = 1;
        return 1;
    }
    return 0;
}

int test_was_skipped(void) { return test_runtime.skipped; }

void test_record_failure(const char* file, int line, const char* expression,
                         const char* expected, const char* actual) {
    test_runtime.assert_file = file;
    test_runtime.assert_line = line;
    snprintf(test_runtime.expression, sizeof(test_runtime.expression), "%s", expression);
    snprintf(test_runtime.expected, sizeof(test_runtime.expected), "%s", expected);
    snprintf(test_runtime.actual, sizeof(test_runtime.actual), "%s", actual);
    printf("    assert %s:%d: %s expected=%s actual=%s\n", file, line, expression, expected, actual);
}

void test_record_skip(const char* reason) {
    test_runtime.skipped = 1;
    snprintf(test_runtime.skip_reason, sizeof(test_runtime.skip_reason), "%s", reason ? reason : "skip");
    printf("    SKIP: %s\n", test_runtime.skip_reason);
}

const test_record_t* test_get_record(void) {
    static test_record_t record;
    record.assert_file = test_runtime.assert_file;
    record.assert_line = test_runtime.assert_line;
    record.expression = test_runtime.expression;
    record.expected = test_runtime.expected;
    record.actual = test_runtime.actual;
    record.skip_reason = test_runtime.skip_reason;
    record.skipped = test_runtime.skipped;
    record.aborted = test_runtime.aborted;
    return &record;
}

void test_set_options(const test_options_t* options) {
    if (options != NULL) test_options = *options;
}
const test_options_t* test_get_options(void) { return &test_options; }

/* ------------------------------------------------------------------ */
/* Repository root resolution                                          */
/* ------------------------------------------------------------------ */

static char test_root_buf[512];

static int test_is_root(const char* dir) {
    char probe[600];
    FILE* f;
    snprintf(probe, sizeof(probe), "%s/src/nes_mapper.c", dir);
    f = fopen(probe, "rb");
    if (f == NULL) return 0;
    fclose(f);
    snprintf(probe, sizeof(probe), "%s/test/runner/test.h", dir);
    f = fopen(probe, "rb");
    if (f == NULL) return 0;
    fclose(f);
    return 1;
}

const char* test_root(void) {
    static const char* candidates[] = { ".", "..", "../..", "../../.." };
    if (test_root_buf[0] != 0) return test_root_buf;
    if (test_options.root != NULL && test_is_root(test_options.root)) {
        snprintf(test_root_buf, sizeof(test_root_buf), "%s", test_options.root);
        return test_root_buf;
    }
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
        if (test_is_root(candidates[i])) {
            snprintf(test_root_buf, sizeof(test_root_buf), "%s", candidates[i]);
            return test_root_buf;
        }
    }
    snprintf(test_root_buf, sizeof(test_root_buf), ".");
    return test_root_buf;
}

void test_path(char* buf, size_t size, const char* relative) {
    snprintf(buf, size, "%s/%s", test_root(), relative);
}

const char* test_rom_dir(void) {
    static char rom_dir_buf[600];
    if (rom_dir_buf[0] != 0) return rom_dir_buf;
    if (test_options.rom_dir != NULL) {
        snprintf(rom_dir_buf, sizeof(rom_dir_buf), "%s", test_options.rom_dir);
    } else {
        test_path(rom_dir_buf, sizeof(rom_dir_buf), "rom");
    }
    return rom_dir_buf;
}

/* ------------------------------------------------------------------ */
/* Fake platform port                                                  */
/* ------------------------------------------------------------------ */

int nes_log_printf(const char* format, ...) { (void)format; return 0; }

void* nes_malloc(int num) { return malloc((size_t)num); }
void nes_free(void* address) { free(address); }
void* nes_memcpy(void* dst, const void* src, size_t n) { return memcpy(dst, src, n); }
void* nes_memset(void* dst, int c, size_t n) { return memset(dst, c, n); }
int nes_memcmp(const void* a, const void* b, size_t n) { return memcmp(a, b, n); }
FILE* nes_fopen(const char* name, const char* mode) { return fopen(name, mode); }
size_t nes_fread(void* p, size_t s, size_t n, FILE* f) { return fread(p, s, n, f); }
size_t nes_fwrite(const void* p, size_t s, size_t n, FILE* f) { return fwrite(p, s, n, f); }
int nes_fseek(FILE* f, long o, int w) { return fseek(f, o, w); }
long nes_ftell(FILE* f) { return ftell(f); }
int nes_remove(const char* name) { return remove(name); }
int nes_fclose(FILE* f) { return fclose(f); }

int nes_draw(int x1, int y1, int x2, int y2, nes_color_t* data) {    (void)x1; (void)y1; (void)x2; (void)y2; (void)data; return 0;
}

int nes_sound_output(uint8_t* data, size_t len) { (void)data; (void)len; return 0; }
int nes_initex(nes_t* nes) { (void)nes; return NES_OK; }
int nes_deinitex(nes_t* nes) { (void)nes; return NES_OK; }

/* ------------------------------------------------------------------ */
/* Frame probe                                                         */
/* ------------------------------------------------------------------ */

static test_frame_probe_t test_probe;

void test_probe_begin(void) {
    memset(&test_probe, 0, sizeof(test_probe));
    test_probe.active = 1;
    test_probe.hash_chain = 2166136261u;
}

const test_frame_probe_t* test_probe_end(void) {
    test_probe.active = 0;
    return &test_probe;
}

static uint32_t test_probe_hash_bytes(uint32_t hash, const uint8_t* data, size_t size) {
    for (size_t i = 0; i < size; ++i) {
        hash ^= data[i];
        hash *= 16777619u;
    }
    return hash;
}

/* Sampled uniformity test: a frame counts as "drawn" when not every probed
 * pixel equals the first one. Probes every 4th pixel: cheap enough for a
 * 180 frame corpus run while still catching a mostly blank screen. */
static int test_frame_is_uniform(const nes_t* nes) {
    const nes_color_t first = nes->nes_draw_data[0];
    for (size_t i = 4; i < (size_t)NES_DRAW_SIZE; i += 4) {
        if (nes->nes_draw_data[i] != first) return 0;
    }
    return 1;
}

void nes_frame(nes_t* nes) {
    if (test_probe.active) {
        const uint32_t frame_hash = nes_test_frame_hash(nes);
        test_probe.frames++;
        if ((nes->nes_ppu.ppu_mask & 0x18u) != 0u) {   /* MASK_b | MASK_s */
            if (!test_probe.render_seen) {
                test_probe.render_seen = 1;
                test_probe.first_render_frame = test_probe.frames;
            }
            if (!test_frame_is_uniform(nes)) test_probe.nonuniform_seen = 1;
        }
        test_probe.hash_chain = test_probe_hash_bytes(test_probe.hash_chain,
                                                      (const uint8_t*)&frame_hash,
                                                      sizeof(frame_hash));
    }
    nes_test_frame_tick(nes);
}

int test_platform_install(void) { return 0; }
void test_platform_uninstall(void) { }

/* ------------------------------------------------------------------ */
/* Deterministic ROM factory                                           */
/* ------------------------------------------------------------------ */

/* Tiny 6502 program placed at the start of every 16KB PRG bank:
 *   SEI; LDA #$1E; STA $2001; LDX #$00; STX $2006; STX $2006;
 *   LDY #$10; STY $2007; INY; CPY #$20; BNE -8; JMP self
 * It enables background+sprites and writes 16 different tile ids into the
 * first nametable row, so a correct renderer produces a non-uniform frame. */
static const uint8_t test_stub_program[] = {
    0x78,                   /* SEI                */
    0xA9, 0x1E,             /* LDA #$1E           */
    0x8D, 0x01, 0x20,       /* STA $2001          */
    0xA2, 0x00,             /* LDX #$00           */
    0x8E, 0x06, 0x20,       /* STX $2006          */
    0x8E, 0x06, 0x20,       /* STX $2006          */
    0xA0, 0x10,             /* LDY #$10           */
    0x8C, 0x07, 0x20,       /* STY $2007   <-- loop */
    0xC8,                   /* INY                */
    0xC0, 0x20,             /* CPY #$20           */
    0xD0, 0xF8,             /* BNE loop           */
    0x4C, 0x18, 0x80        /* JMP $8018 (self)   */
};

static void test_fill_prg(uint8_t* prg, size_t prg_size, uint8_t fill) {
    for (size_t bank = 0; bank + PRG_ROM_UNIT_SIZE <= prg_size; bank += PRG_ROM_UNIT_SIZE) {
        uint8_t* base = prg + bank;
        if (fill == TEST_ROM_FILL_STUB) {
            memset(base, 0xEA, PRG_ROM_UNIT_SIZE);
            memcpy(base, test_stub_program, sizeof(test_stub_program));
        } else {
            for (size_t i = 0; i < PRG_ROM_UNIT_SIZE; ++i) {
                base[i] = (uint8_t)((i ^ 0xa5u) + (bank / PRG_ROM_UNIT_SIZE) * 17u);
            }
        }
        /* Vectors live at the end of every 16KB bank so that banking cannot
         * change where reset/NMI/IRQ land. */
        base[PRG_ROM_UNIT_SIZE - 6] = 0x00; base[PRG_ROM_UNIT_SIZE - 5] = 0x80;
        base[PRG_ROM_UNIT_SIZE - 4] = 0x00; base[PRG_ROM_UNIT_SIZE - 3] = 0x80;
        base[PRG_ROM_UNIT_SIZE - 2] = 0x00; base[PRG_ROM_UNIT_SIZE - 1] = 0x80;
    }
}

uint8_t* test_make_ines_ex(size_t* size, const test_rom_spec_t* spec) {
    const size_t prg_size = (size_t)spec->prg_units * PRG_ROM_UNIT_SIZE;
    const size_t chr_size = (size_t)spec->chr_units * CHR_ROM_UNIT_SIZE;
    const size_t total = 16u + (spec->trainer ? TRAINER_SIZE : 0u) + prg_size + chr_size;
    uint8_t* rom = (uint8_t*)calloc(1, total);
    if (rom == NULL) return NULL;
    memcpy(rom, "NES\x1a", 4);
    if (spec->nes2) {
        rom[4] = (uint8_t)(spec->prg_units & 0xFF);
        rom[5] = (uint8_t)(spec->chr_units & 0xFF);
        rom[6] = (uint8_t)((spec->mirroring ? 1u : 0u) | (spec->save ? 2u : 0u) |
                           (spec->trainer ? 4u : 0u) | (spec->four_screen ? 8u : 0u) |
                           ((spec->mapper & 0x0Fu) << 4));
        rom[7] = (uint8_t)((((spec->mapper >> 4) & 0x0Fu) << 4) | 0x08u);   /* mapper D4-D7 | identifier 2 */
        rom[8] = (uint8_t)((spec->mapper >> 8) & 0x0Fu);
        rom[9] = (uint8_t)(((spec->prg_units >> 8) & 0x0Fu) | (((spec->chr_units >> 8) & 0x0Fu) << 4));
        rom[10] = 0x07;                     /* PRG-RAM: 64 << 7 = 8KB */
        rom[11] = 0x00;
    } else {
        rom[4] = (uint8_t)(spec->prg_units & 0xFF);
        rom[5] = (uint8_t)(spec->chr_units & 0xFF);
        rom[6] = (uint8_t)((spec->mirroring ? 1u : 0u) | (spec->save ? 2u : 0u) |
                           (spec->trainer ? 4u : 0u) | (spec->four_screen ? 8u : 0u) |
                           ((spec->mapper & 0x0Fu) << 4));
        rom[7] = (uint8_t)(spec->mapper & 0xF0u);
    }
    if (spec->trainer) memset(rom + 16, 0x5a, TRAINER_SIZE);
    size_t off = 16u + (spec->trainer ? TRAINER_SIZE : 0u);
    test_fill_prg(rom + off, prg_size, spec->fill);
    off += prg_size;
    for (size_t i = 0; i < chr_size; ++i) {
        rom[off + i] = (uint8_t)((i * 3u) + (i / 1024u) * 17u);
    }
    if (size) *size = total;
    return rom;
}

uint8_t* test_make_ines(size_t* size, uint16_t mapper, uint16_t prg_units, uint16_t chr_units,
                        uint8_t trainer, uint8_t save) {
    test_rom_spec_t spec;
    memset(&spec, 0, sizeof(spec));
    spec.mapper = mapper;
    spec.prg_units = prg_units;
    spec.chr_units = chr_units;
    spec.trainer = trainer;
    spec.save = save;
    spec.fill = TEST_ROM_FILL_RANDOM;
    return test_make_ines_ex(size, &spec);
}

void test_free_rom(uint8_t* rom) { free(rom); }

int test_fixture_make(test_fixture_t* fixture, const test_rom_spec_t* spec) {
    memset(fixture, 0, sizeof(*fixture));
    fixture->rom = test_make_ines_ex(&fixture->size, spec);
    if (fixture->rom == NULL) return 0;
    fixture->nes = nes_init();
    if (fixture->nes == NULL) return 0;
    if (nes_test_load_rom_checked(fixture->nes, fixture->rom, fixture->size, NULL) != NES_TEST_ROM_OK) {
        return 0;
    }
    return 1;
}

void test_fixture_free(test_fixture_t* fixture) {
    if (fixture->nes != NULL) {
        nes_unload_rom(fixture->nes);
        nes_deinit(fixture->nes);
    }
    test_free_rom(fixture->rom);
    memset(fixture, 0, sizeof(*fixture));
}

int test_mapper_supported(uint16_t mapper) {
    test_rom_spec_t spec;
    size_t size = 0;
    memset(&spec, 0, sizeof(spec));
    spec.mapper = mapper;
    spec.prg_units = 2;
    spec.chr_units = 1;
    spec.fill = TEST_ROM_FILL_RANDOM;
    uint8_t* rom = test_make_ines_ex(&size, &spec);
    if (rom == NULL) return 0;
    nes_t* nes = nes_init();
    if (nes == NULL) { test_free_rom(rom); return 0; }
    int rc = nes_load_rom(nes, rom);
    int ok = (rc == NES_OK) && (nes->nes_mapper.mapper_init != NULL);
    nes_unload_rom(nes);
    nes_deinit(nes);
    test_free_rom(rom);
    return ok;
}




























