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
#include "test.h"
#include "test_cases.h"

const nes_test_case_t* test_cases(size_t* count) {
    static const nes_test_case_t cases[] = {
        {"cpu", "opcode coverage", test_cpu_opcode_coverage},
        {"cpu", "opcode cycles", test_cpu_opcode_cycles},
        {"cpu", "flags", test_cpu_flags},
        {"cpu", "stack and control flow", test_cpu_stack},
        {"cpu", "undocumented opcodes", test_cpu_undocumented},
        {"cpu", "interrupt timing", test_cpu_interrupts},
        {"cpu", "oam dma", test_cpu_oam_dma},
        {"cpu", "oam dma mapper read", test_cpu_oam_dma_mapper_read},
        {"cpu", "expansion keyboard", test_cpu_exp_keyboard},
        {"ppu", "registers and VRAM", test_ppu_registers},
        {"ppu", "scroll and address", test_ppu_scroll},
        {"ppu", "palette mirroring", test_ppu_palette},
        {"ppu", "chr write protection", test_ppu_chr_protection},
        {"ppu", "mirroring table", test_ppu_mirroring},
        {"ppu", "sprite 0 hit and priority", test_ppu_sprite0},
        {"ppu", "sprite 0 hit while skipping", test_ppu_sprite0_frameskip},
        {"ppu", "rendering", test_ppu_render},
        {"ppu", "ntsc/pal scanline timing", test_ppu_timing_regions},
        {"apu", "length counters and 4015", test_apu_length_counters},
        {"apu", "frame counter irq", test_apu_frame_irq},
        {"apu", "samples", test_apu_samples},
        {"rom", "layout and CRC", test_rom_layout},
        {"rom", "invalid images", test_rom_errors},
        {"rom", "header variants", test_rom_header_variants},
        {"mapper", "dispatch and banks", test_mapper_dispatch},
        {"mapper", "bank helpers", test_mapper_bank_helpers},
        {"mapper", "mmc1 serial counter", test_mapper1_serial_counter},
        {"mapper", "mmc1 rmw reset write", test_mapper1_rmw_reset_write},
        {"mapper", "nrom prg ram", test_mapper0_prg_ram},
        {"mapper", "245 chr-ram board", test_mapper245_chr_ram_board},
        {"mapper", "69 5b audio write", test_mapper69_5b_audio_write},
        {"mapper", "17 power-on slots", test_mapper17_power_on_slots},
        {"mapper", "11 chr bank select", test_mapper11_chr_bank_select},
        {"mapper", "230 contra mode", test_mapper230_contra_mode},
        {"mapper", "231 bmc 20in1", test_mapper231_bmc_20in1},
        {"mapper", "57 dendy registers", test_mapper57_dendy_registers},
        {"mapper", "32 irem g101 prg mode", test_mapper32_irem_g101_prg_mode},
        {"mapper", "3 cnrom chr bank", test_mapper3_cnrom_chr_bank},
        {"mapper", "121 protection", test_mapper121_protection},
        {"mapper", "226 bank formula", test_mapper226_bank_formula},
        {"mapper", "78 jf16 mirroring", test_mapper78_jf16_mirroring},
                {"mapper", "99 vs unisystem latch", test_mapper99_vs_latch},
        #if (NES_VS_SYSTEM == 1)
        {"ppu", "vs system palette", test_vs_ppu_palette},
        {"cpu", "vs system switches", test_vs_system_switches},
        {"cpu", "vs system protection", test_vs_system_protection},
#endif
        {"mapper", "waixing chr-ram window", test_mapper4_waixing_window},
        {"mapper", "mmc3 prg-ram", test_mapper4_wram},
        {"mapper", "synthetic smoke", test_mapper_synthetic_smoke},
        {"mapper", "write storm", test_mapper_write_storm},
        {"mapper", "bank stress", test_mapper_bank_stress},
        {"rom", "stream consistency", test_stream_consistency},
        {"rom", "stream short read fallback", test_stream_short_read},
        {"state", "round-trip determinism", test_state_roundtrip},
        {"state", "header and ROM validation", test_state_validation},
        {"state", "hot save and reload cycles", test_state_hot_save},
        {"state", "corpus ROM round-trip", test_state_rom_roundtrip},
        {"stress", "CPU million instructions", test_cpu_stress},
        {"corpus", "rom corpus", test_corpus_case},
    };
    if (count) *count = sizeof(cases) / sizeof(cases[0]);
    return cases;
}
