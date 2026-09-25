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
#include "test.h"

/* cpu */
int test_cpu_opcode_coverage(void);
int test_cpu_opcode_cycles(void);
int test_cpu_flags(void);
int test_cpu_stack(void);
int test_cpu_undocumented(void);
int test_cpu_interrupts(void);
int test_cpu_oam_dma(void);
int test_cpu_stress(void);

/* ppu */
int test_ppu_registers(void);
int test_ppu_scroll(void);
int test_ppu_palette(void);
int test_ppu_chr_protection(void);
int test_ppu_mirroring(void);
int test_ppu_sprite0(void);
int test_ppu_render(void);

/* apu */
int test_apu_length_counters(void);
int test_apu_frame_irq(void);
int test_apu_samples(void);

/* rom */
int test_rom_layout(void);
int test_rom_errors(void);
int test_rom_header_variants(void);
int test_stream_consistency(void);

/* mapper */
int test_mapper_dispatch(void);
int test_mapper_bank_helpers(void);
int test_mapper4_waixing_window(void);
int test_mapper_synthetic_smoke(void);
int test_mapper_write_storm(void);
int test_mapper_bank_stress(void);

const nes_test_case_t* test_cases(size_t* count);
