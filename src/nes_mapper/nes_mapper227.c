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

/*
 * Mapper 227 - BMC 1200-in-1 / 南晶科技 大富翁 (超级大富翁, 香帅传奇 ...).
 * Authority: Mesen2 Core/NES/Mappers/Unlicensed/Mapper227.h.
 *
 * The register is the *address* of any $8000-$FFFF write (the data byte is ignored):
 *
 *   prgBank = ((addr >> 2) & 0x1F) | ((addr & 0x100) >> 3)     (6 bits, 16KB pages)
 *   sFlag   = addr bit 0
 *   lFlag   = addr bit 9
 *   prgMode = addr bit 7
 *
 *   prgMode = 1 : sFlag -> the aligned 32KB pair (prgBank & 0xFE), else prgBank in both
 *                 16KB slots
 *   prgMode = 0 : slot 0 = sFlag ? prgBank & 0x3E : prgBank
 *                 slot 1 = lFlag ? prgBank | 0x07 : prgBank & 0x38
 *
 *   mirroring   : addr bit 1 -> horizontal, otherwise vertical
 *   CHR         : not banked at all - one fixed 8KB page
 *
 * Power-on runs the same path with addr = $8000: both 16KB slots on bank 0, vertical.
 * The old implementation banked CHR and read the PRG/mirroring bits from different
 * address lines, so the games came up blank.
 */

static void nes_mapper_apply(nes_t* nes, uint16_t addr) {
    const uint16_t prg_bank = (uint16_t)(((addr >> 2) & 0x1Fu) | ((addr & 0x100u) >> 3));
    const uint8_t  s_flag = (uint8_t)(addr & 0x01u);
    const uint8_t  l_flag = (uint8_t)((addr >> 9) & 0x01u);
    const uint8_t  prg_mode = (uint8_t)((addr >> 7) & 0x01u);

    if (prg_mode) {
        if (s_flag) {
            nes_load_prgrom_32k(nes, 0, (uint16_t)((prg_bank & 0xFEu) >> 1));
        } else {
            nes_load_prgrom_16k(nes, 0, prg_bank);
            nes_load_prgrom_16k(nes, 1, prg_bank);
        }
    } else {
        nes_load_prgrom_16k(nes, 0, (uint16_t)(s_flag ? (prg_bank & 0x3Eu) : prg_bank));
        nes_load_prgrom_16k(nes, 1, (uint16_t)(l_flag ? (prg_bank | 0x07u) : (prg_bank & 0x38u)));
    }

    if (nes->nes_rom.four_screen == 0) {
        nes_ppu_screen_mirrors(nes, (addr & 0x02u) ? NES_MIRROR_HORIZONTAL : NES_MIRROR_VERTICAL);
    }
}

static void nes_mapper_init(nes_t* nes) {
    /* CHR is a single fixed 8KB page (CHR-RAM on the 南晶 boards). */
    nes_load_chrrom_8k(nes, 0, 0);
    nes_mapper_apply(nes, 0x8000u);
}

static void nes_mapper_write(nes_t* nes, uint16_t address, uint8_t data) {
    (void)data;
    nes_mapper_apply(nes, address);
}

int nes_mapper227_init(nes_t* nes) {
    nes->nes_mapper.mapper_init  = nes_mapper_init;
    nes->nes_mapper.mapper_write = nes_mapper_write;
    return NES_OK;
}
