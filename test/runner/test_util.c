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
        {"ppu", "registers and VRAM", test_ppu_registers},
        {"ppu", "scroll and address", test_ppu_scroll},
        {"ppu", "palette mirroring", test_ppu_palette},
        {"ppu", "chr write protection", test_ppu_chr_protection},
        {"ppu", "mirroring table", test_ppu_mirroring},
        {"ppu", "sprite 0 hit and priority", test_ppu_sprite0},
        {"ppu", "rendering", test_ppu_render},
        {"apu", "length counters and 4015", test_apu_length_counters},
        {"apu", "frame counter irq", test_apu_frame_irq},
        {"apu", "samples", test_apu_samples},
        {"rom", "layout and CRC", test_rom_layout},
        {"rom", "invalid images", test_rom_errors},
        {"rom", "header variants", test_rom_header_variants},
        {"mapper", "dispatch and banks", test_mapper_dispatch},
        {"mapper", "bank helpers", test_mapper_bank_helpers},
        {"mapper", "waixing chr-ram window", test_mapper4_waixing_window},
        {"mapper", "synthetic smoke", test_mapper_synthetic_smoke},
        {"mapper", "write storm", test_mapper_write_storm},
        {"mapper", "bank stress", test_mapper_bank_stress},
        {"rom", "stream consistency", test_stream_consistency},
        {"stress", "CPU million instructions", test_cpu_stress},
        {"corpus", "rom corpus", test_corpus_case},
    };
    if (count) *count = sizeof(cases) / sizeof(cases[0]);
    return cases;
}
