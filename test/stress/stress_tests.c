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
 * Long-running stress cases: they only assert that the emulator keeps making
 * progress and stays inside its bounds.
 */
#include "test.h"

int test_cpu_stress(void) {
    test_rom_spec_t spec;
    test_fixture_t f;
    memset(&spec, 0, sizeof(spec));
    spec.mapper = 0;
    spec.prg_units = 2;
    spec.chr_units = 1;
    spec.fill = TEST_ROM_FILL_STUB;
    TEST_CHECK(test_fixture_make(&f, &spec));

    nes_test_cpu_prepare(f.nes, 0x200);
    f.nes->nes_cpu.cpu_ram[0x200] = 0xEA;
    f.nes->nes_cpu.cpu_ram[0x201] = 0xEA;
    f.nes->nes_cpu.cpu_ram[0x202] = 0xEA;
    for (unsigned i = 0; i < 1000000u; ++i) {
        uint16_t cycles = 0;
        if (nes_test_cpu_step(f.nes, &cycles) != NES_OK || cycles == 0) {
            test_fixture_free(&f);
            return TEST_FAIL;
        }
        if ((f.nes->nes_cpu.PC & 0xFF00) != 0x0200) nes_test_cpu_prepare(f.nes, 0x200);
        TEST_ABORT_OR_FAIL();
    }
    test_fixture_free(&f);
    return TEST_PASS;
}
