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

#if defined(NES_TEST_MODE) && (NES_TEST_MODE == 1)

#include "nes_default.h"

#ifdef __cplusplus
extern "C" {
#endif

struct nes;
typedef struct nes nes_t;

typedef enum {
    NES_TEST_ROM_OK = 0,
    NES_TEST_ROM_INVALID_ARGUMENT = -1,
    NES_TEST_ROM_TRUNCATED_HEADER = -2,
    NES_TEST_ROM_INVALID_MAGIC = -3,
    NES_TEST_ROM_UNSUPPORTED_LAYOUT = -4,
    NES_TEST_ROM_TRUNCATED_TRAINER = -5,
    NES_TEST_ROM_TRUNCATED_PRG = -6,
    NES_TEST_ROM_TRUNCATED_CHR = -7,
    NES_TEST_ROM_LOAD_FAILED = -8
} nes_test_rom_result_t;

typedef struct {
    uint16_t prg_rom_size;
    uint16_t chr_rom_size;
    uint16_t mapper_number;
    uint8_t submapper;
    uint8_t trainer;
    uint8_t nes2;
    uint8_t mirroring_type;
    uint8_t four_screen;
    uint8_t save_ram;
    size_t trainer_offset;
    size_t prg_offset;
    size_t chr_offset;
    size_t total_size;
    uint32_t crc32;
} nes_test_rom_layout_t;

typedef struct {
    uint32_t frames;
    uint32_t samples;
    uint32_t min_sample;
    uint32_t max_sample;
    uint64_t sum_sample;
    uint64_t sum_sample_sq;
    uint32_t checksum;
} nes_test_audio_stats_t;
void nes_test_frame_tick(nes_t* nes);

int nes_test_cpu_prepare(nes_t* nes, uint16_t pc);
int nes_test_cpu_step(nes_t* nes, uint16_t* cycles);
uint8_t nes_test_cpu_read(nes_t* nes, uint16_t address);
void nes_test_cpu_write(nes_t* nes, uint16_t address, uint8_t data);
int nes_test_ppu_tick(nes_t* nes, uint32_t ppu_cycles);
int nes_test_apu_tick(nes_t* nes, uint32_t apu_frames);
int nes_test_reset(nes_t* nes);
int nes_test_run_frames(nes_t* nes, uint32_t frames);
uint32_t nes_test_frame_hash(const nes_t* nes);
nes_test_audio_stats_t nes_test_audio_stats(const nes_t* nes);
int nes_test_bank_check(nes_t* nes);
uint32_t nes_test_bank_crc(nes_t* nes);
uint32_t nes_test_bank_hash(nes_t* nes, uint8_t cpu_slot, uint8_t ppu_slot);
int nes_test_reset_deterministic(nes_t* nes);
nes_test_rom_result_t nes_test_parse_rom(const uint8_t* data, size_t size,
                                        nes_test_rom_layout_t* layout);
nes_test_rom_result_t nes_test_load_rom_checked(nes_t* nes, const uint8_t* data,
                                               size_t size,
                                               nes_test_rom_layout_t* layout);

#ifdef __cplusplus
}
#endif

#endif
