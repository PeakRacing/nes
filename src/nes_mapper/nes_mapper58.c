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

/* https://www.nesdev.org/wiki/INES_Mapper_058
 * Mapper 58 - the Dendy multicart board ("68 in 1" and friends).  Matches Mesen's
 * Core/NES/Mappers/Unlicensed/Mapper58.h: the *address* of the write is the register and the data
 * byte is ignored (the board simply decodes the address lines):
 *
 *   prg bank   = address & 0x07
 *   address bit 6 set: both 16KB halves take that same bank
 *   address bit 6 clear: $8000-$FFFF takes the 32KB-aligned pair (bank & 0x06)
 *   CHR 8KB bank = (address >> 3) & 0x07
 *   mirroring    = address bit 7 (set = horizontal)
 *
 * The previous implementation here read the *data* byte instead of the address, put bank|7 in the
 * second 16KB half, did not align the 32KB page, and never touched the mirroring.  The board also
 * reaches those registers through the $6000-$7FFF window, so the handler stays installed as the
 * board's SRAM hook.
 */

static void nes_mapper_init(nes_t* nes) {
    nes_load_prgrom_16k(nes, 0, 0);
    nes_load_prgrom_16k(nes, 1, 1);
    if (nes->nes_rom.chr_rom_size > 0) {
        nes_load_chrrom_8k(nes, 0, 0);
    }
}

static void nes_mapper_apply(nes_t* nes, uint16_t address) {
    const uint8_t prg_bank = (uint8_t)(address & 0x07u);

    if (address & 0x40u) {
        /* 16KB mode: the same bank in both halves. */
        nes_load_prgrom_16k(nes, 0, prg_bank);
        nes_load_prgrom_16k(nes, 1, prg_bank);
    } else {
        /* 32KB mode: the aligned pair (bank & 0x06, +1). */
        nes_load_prgrom_32k(nes, 0, (uint16_t)((prg_bank & 0x06u) >> 1));
    }

    if (nes->nes_rom.chr_rom_size > 0) {
        nes_load_chrrom_8k(nes, 0, (uint16_t)((address >> 3) & 0x07u));
    }

    if (nes->nes_rom.four_screen == 0) {
        nes_ppu_screen_mirrors(nes, (address & 0x80u) ? NES_MIRROR_HORIZONTAL : NES_MIRROR_VERTICAL);
    }
}

static void nes_mapper_sram(nes_t* nes, uint16_t address, uint8_t data) {
    (void)data;                                  /* the address is the register */
    nes_mapper_apply(nes, address);
}

static void nes_mapper_write(nes_t* nes, uint16_t address, uint8_t data) {
    (void)data;
    nes_mapper_apply(nes, address);
}

int nes_mapper58_init(nes_t* nes) {
    nes->nes_mapper.mapper_init  = nes_mapper_init;
    nes->nes_mapper.mapper_sram  = nes_mapper_sram;
    nes->nes_mapper.mapper_write = nes_mapper_write;
    return NES_OK;
}
