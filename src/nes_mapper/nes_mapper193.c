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

/* https://www.nesdev.org/wiki/INES_Mapper_193
 * NTDEC TC-112 — PRG 32KB fixed; 4x2KB CHR bank switching via $6000-$6003.
 * $6000: 2KB CHR bank for PPU $0000-$07FF
 * $6001: 2KB CHR bank for PPU $0800-$0FFF
 * $6002: 2KB CHR bank for PPU $1000-$17FF
 * $6003: 2KB CHR bank for PPU $1800-$1FFF
 */

static void nes_mapper_init(nes_t* nes) {
    nes_load_prgrom_32k(nes, 0, 0);
    if (nes->nes_rom.chr_rom_size > 0) {
        for (uint8_t i = 0; i < 8u; i++) {
            nes_load_chrrom_1k(nes, i, i);
        }
    }
}

static void nes_mapper_sram(nes_t* nes, uint16_t address, uint8_t data) {
    if (nes->nes_rom.chr_rom_size == 0) return;
    uint8_t slot = (uint8_t)(address & 0x03u);
    /* Each register selects a 2KB CHR bank for the corresponding 2KB window */
    uint8_t bank1k_base = (uint8_t)((data & 0x1Fu) << 1u);
    nes_load_chrrom_1k(nes, (uint8_t)(slot * 2u),      bank1k_base);
    nes_load_chrrom_1k(nes, (uint8_t)(slot * 2u + 1u), (uint8_t)(bank1k_base + 1u));
}

int nes_mapper193_init(nes_t* nes) {
    nes->nes_mapper.mapper_init = nes_mapper_init;
    nes->nes_mapper.mapper_sram = nes_mapper_sram;
    return NES_OK;
}
