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

/* https://www.nesdev.org/wiki/INES_Mapper_228
 * Action 52 / Cheetahmen II — PRG+CHR bank selection via address bits.
 * Write $8000-$FFFF:
 *   address[13:11] = CHR bank high bits [3:1]; data[3] = CHR bank bit 0
 *   address[9:8]   = PRG chip (0-3); address[8:3] within chip = 16KB PRG bank
 *   data[0]        = mirroring (0=V, 1=H)
 */

static void nes_mapper_init(nes_t* nes) {
    nes_load_prgrom_16k(nes, 0, 0);
    nes_load_prgrom_16k(nes, 2, (uint16_t)(nes->nes_rom.prg_rom_size - 1));
    if (nes->nes_rom.chr_rom_size > 0) {
        nes_load_chrrom_8k(nes, 0, 0);
    }
}

static void nes_mapper_write(nes_t* nes, uint16_t address, uint8_t data) {
    /* PRG: chip[1:0] from address[9:8], bank[5:1] from address[8:3] */
    uint16_t prg_bank = (uint16_t)(((address >> 8) & 0x03u) * 8u + ((address >> 3) & 0x07u));
    /* 16KB mirrored mode: both halves same bank */
    nes_load_prgrom_16k(nes, 0, prg_bank);
    nes_load_prgrom_16k(nes, 2, prg_bank);
    /* CHR: address[13:11] high bits, data[3] low bit */
    uint8_t chr = (uint8_t)(((address >> 11) & 0x07u) << 1u | ((data >> 3) & 0x01u));
    if (nes->nes_rom.chr_rom_size > 0) {
        nes_load_chrrom_8k(nes, 0, chr);
    }
    if (nes->nes_rom.four_screen == 0) {
        nes_ppu_screen_mirrors(nes, (data & 0x01u) ? NES_MIRROR_HORIZONTAL : NES_MIRROR_VERTICAL);
    }
}

int nes_mapper228_init(nes_t* nes) {
    nes->nes_mapper.mapper_init  = nes_mapper_init;
    nes->nes_mapper.mapper_write = nes_mapper_write;
    return NES_OK;
}
