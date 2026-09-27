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
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nes.h"
#include "nes_test.h"

typedef int (*nes_test_fn)(void);
typedef struct { const char* group; const char* name; nes_test_fn fn; } nes_test_case_t;

#define TEST_PASS (0)
#define TEST_FAIL (1)
#define TEST_SKIP (2)

/* Corpus verdicts (see runner/corpus.c) */
typedef enum {
    CORPUS_OK = 0,          /* rendering enabled and a non-uniform frame was drawn */
    CORPUS_SUSPECT,         /* rendering enabled but every frame was a single colour */
    CORPUS_BLANK,           /* rendering never enabled during the whole run */
    CORPUS_LOAD_FAIL,       /* nes_load_file() rejected the image */
    CORPUS_VERDICT_COUNT
} corpus_verdict_t;

typedef struct {
    const char* assert_file;
    int assert_line;
    const char* expression;
    const char* expected;
    const char* actual;
    const char* skip_reason;
    int skipped;
    int aborted;
} test_record_t;

typedef struct {
    const char* root;           /* resolved repository root */
    const char* report;         /* --report csv path, may be NULL */
    const char* rom_dir;        /* corpus directory, default <root>/rom */
    const char* single_rom;     /* --rom <file>: run exactly one corpus image */
    const char* analyze;        /* --analyze <results.csv>: rebuild reports only */
    uint32_t frames;            /* frames per corpus image */
    int mapper_filter;          /* -1 = every mapper */
    uint32_t max_roms;          /* 0 = unlimited */
    int update_baseline;        /* write test/baseline/corpus.csv */
    int strict;                 /* frame-hash mismatch is a failure */
    unsigned timeout_ms;        /* per case */
} test_options_t;

void test_record_failure(const char* file, int line, const char* expression,
                         const char* expected, const char* actual);
void test_record_skip(const char* reason);
int test_should_abort(void);
void test_begin_case(unsigned timeout_ms);
int test_was_skipped(void);
const test_record_t* test_get_record(void);
void test_set_options(const test_options_t* options);
const test_options_t* test_get_options(void);

#define TEST_CHECK(cond) do { if (!(cond)) { \
    test_record_failure(__FILE__, __LINE__, #cond, "true", "false"); return TEST_FAIL; \
} } while (0)
#define TEST_EQ_U32(expected, actual) do { \
    unsigned long long e_=(unsigned long long)(expected), a_=(unsigned long long)(actual); \
    char es_[32], as_[32]; snprintf(es_, sizeof(es_), "%llu", e_); snprintf(as_, sizeof(as_), "%llu", a_); \
    if (e_ != a_) { test_record_failure(__FILE__, __LINE__, #actual, es_, as_); return TEST_FAIL; } \
} while (0)
#define TEST_EQ_I32(expected, actual) do { \
    long long e_=(long long)(expected), a_=(long long)(actual); \
    char es_[32], as_[32]; snprintf(es_, sizeof(es_), "%lld", e_); snprintf(as_, sizeof(as_), "%lld", a_); \
    if (e_ != a_) { test_record_failure(__FILE__, __LINE__, #actual, es_, as_); return TEST_FAIL; } \
} while (0)
#define TEST_EQ_PTR(expected, actual) do { \
    const void* e_=(const void*)(expected); const void* a_=(const void*)(actual); \
    char es_[32], as_[32]; snprintf(es_, sizeof(es_), "%p", e_); snprintf(as_, sizeof(as_), "%p", a_); \
    if (e_ != a_) { test_record_failure(__FILE__, __LINE__, #actual, es_, as_); return TEST_FAIL; } \
} while (0)
#define TEST_ABORT_OR_FAIL() do { if (test_should_abort()) return TEST_FAIL; } while (0)
#define TEST_SKIP_MSG(msg) do { test_record_skip(msg); return TEST_SKIP; } while (0)

/* ---- runner/platform helpers (runner/test_platform.c) ---- */
int test_platform_install(void);
void test_platform_uninstall(void);

/* Repository root / root-relative paths (works from repo root or from test/). */
const char* test_root(void);
const char* test_rom_dir(void);
void test_path(char* buf, size_t size, const char* relative);

/* Deterministic synthetic iNES factory. */
typedef enum {
    TEST_ROM_FILL_RANDOM = 0,   /* pseudo-random PRG, good for load/CRC tests */
    TEST_ROM_FILL_STUB          /* tiny 6502 program: enable rendering, then loop */
} test_rom_fill_t;

typedef struct {
    uint16_t mapper;
    uint16_t prg_units;         /* 16KB units */
    uint16_t chr_units;         /* 8KB units, 0 = CHR-RAM board */
    uint8_t  trainer;
    uint8_t  save;
    uint8_t  four_screen;
    uint8_t  mirroring;         /* iNES flags6 D0 */
    uint8_t  fill;              /* test_rom_fill_t */
    uint8_t  nes2;              /* write a NES 2.0 header instead of iNES */
} test_rom_spec_t;

uint8_t* test_make_ines_ex(size_t* size, const test_rom_spec_t* spec);
uint8_t* test_make_ines(size_t* size, uint16_t mapper, uint16_t prg_units, uint16_t chr_units,
                        uint8_t trainer, uint8_t save);
void test_free_rom(uint8_t* rom);

/* Loaded fixture: a nes_t plus the ROM buffer it points into. */
typedef struct {
    nes_t* nes;
    uint8_t* rom;
    size_t size;
} test_fixture_t;

int test_fixture_make(test_fixture_t* fixture, const test_rom_spec_t* spec);
void test_fixture_free(test_fixture_t* fixture);

/* 1 when the current build can load a synthetic ROM for this mapper number. */
int test_mapper_supported(uint16_t mapper);

/* Frame probe: fed from nes_frame() while a corpus run is active. */
typedef struct {
    int active;
    uint32_t frames;
    uint32_t first_render_frame;
    int render_seen;
    int nonuniform_seen;
    uint32_t hash_chain;
} test_frame_probe_t;

void test_probe_begin(void);
const test_frame_probe_t* test_probe_end(void);

/* ---- corpus (runner/corpus.c) ---- */
int test_corpus_case(void);          /* aggregate run over --rom-dir */
int test_corpus_single(const char* rom_path);   /* --rom <file> */
int test_corpus_analyze(const char* results_csv);/* --analyze <file> */
