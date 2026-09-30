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

#include "nes.h"

/* https://www.nesdev.org/wiki/INES_Mapper_086
 * Jaleco JF-13 — PRG 32KB + CHR 8KB switchable via $6000-$7FFF.
 * Write $6000-$7FFF:
 *   bits[1:0] = PRG 32KB bank
 *   bits[6:4] = CHR 8KB bank
 */

static void nes_mapper_init(nes_t* nes) {
    nes_load_prgrom_32k(nes, 0, 0);
    if (nes->nes_rom.chr_rom_size > 0) {
        nes_load_chrrom_8k(nes, 0, 0);
    }
}

/* Jaleco JF-13 register (Mesen2 Jaleco/JalecoJf13.h):
     only $6000-$6FFF is the register ($7000-$7FFF is the unsupported audio port);
     PRG 32KB page = bits 4-5 of the value;
     CHR  8KB page = bits 0-1, plus bit 6 as the third bank bit.
   The old implementation read the PRG page from bits 0-1 and the CHR page from bits 4-6,
   i.e. every field was in the wrong place. */
static void nes_mapper_sram(nes_t* nes, uint16_t address, uint8_t data) {
    if ((address & 0x7000u) != 0x6000u) return;

    nes_load_prgrom_32k(nes, 0, (uint16_t)((data & 0x30u) >> 4));
    if (nes->nes_rom.chr_rom_size > 0) {
        nes_load_chrrom_8k(nes, 0, (uint8_t)((data & 0x03u) | ((data >> 4) & 0x04u)));
    }
}

int nes_mapper86_init(nes_t* nes) {
    nes->nes_mapper.mapper_init = nes_mapper_init;
    nes->nes_mapper.mapper_sram = nes_mapper_sram;
    return NES_OK;
}
