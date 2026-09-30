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
int test_cpu_oam_dma_mapper_read(void);
int test_cpu_exp_keyboard(void);
int test_cpu_stress(void);

/* ppu */
int test_ppu_registers(void);
int test_ppu_scroll(void);
int test_ppu_palette(void);
int test_ppu_chr_protection(void);
int test_ppu_mirroring(void);
int test_ppu_sprite0(void);
int test_ppu_sprite0_frameskip(void);
int test_ppu_render(void);
int test_ppu_timing_regions(void);

/* apu */
int test_apu_length_counters(void);
int test_apu_frame_irq(void);
int test_apu_samples(void);

/* rom */
int test_rom_layout(void);
int test_rom_errors(void);
int test_rom_header_variants(void);
int test_stream_consistency(void);
int test_stream_short_read(void);

/* mapper */
int test_mapper_dispatch(void);
int test_mapper_bank_helpers(void);
int test_mapper1_serial_counter(void);
int test_mapper1_rmw_reset_write(void);
int test_mapper0_prg_ram(void);
int test_mapper245_chr_ram_board(void);
int test_mapper69_5b_audio_write(void);
int test_mapper17_power_on_slots(void);
int test_mapper11_chr_bank_select(void);
int test_mapper121_protection(void);
int test_mapper3_cnrom_chr_bank(void);
int test_mapper32_irem_g101_prg_mode(void);
int test_mapper57_dendy_registers(void);
int test_mapper231_bmc_20in1(void);
int test_mapper230_contra_mode(void);
int test_mapper229_bmc_31in1(void);
int test_mapper249_waixing_permute(void);
int test_mapper58_dendy_address_register(void);
int test_mapper255_bmc_pcb018(void);
int test_mapper235_golden_game(void);
int test_mapper235_open_bus(void);
int test_mapper62_super_700in1(void);
int test_mapper242_waixing_1200in1(void);
int test_mapper226_bank_formula(void);
int test_mapper78_jf16_mirroring(void);
int test_mapper99_vs_latch(void);
#if (NES_VS_SYSTEM == 1)
int test_vs_ppu_palette(void);
int test_vs_system_switches(void);
int test_vs_system_protection(void);
#endif
int test_mapper4_waixing_window(void);
int test_mapper4_wram(void);
int test_mapper25_wram(void);
int test_mapper73_wram(void);
int test_mapper162_waixing(void);
int test_mapper165_fire_emblem(void);
int test_mapper164_waixing(void);
int test_mapper_synthetic_smoke(void);
int test_mapper_write_storm(void);
int test_mapper_bank_stress(void);

/* state */
int test_state_roundtrip(void);
int test_state_validation(void);
int test_state_hot_save(void);
int test_state_rom_roundtrip(void);

const nes_test_case_t* test_cases(size_t* count);
