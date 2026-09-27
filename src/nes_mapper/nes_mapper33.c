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

/* https://www.nesdev.org/wiki/INES_Mapper_033
 * Mapper 33 - Taito TC0190.
 * No IRQ, no SRAM. PRG: two 8KB switchable banks at $8000/$A000; last two 8KB banks fixed.
 * CHR: two 2KB banks at $0000/$0800, four 1KB banks at $1000-$1C00.
 *
 * Some cartridges of this family (iNES mapper 48, Taito TC0690 — e.g. Don Doko Don 2)
 * replace the $8000 mirroring bit with a dedicated register at $E000 and keep bit 6 of
 * $8000 as part of the PRG bank.  A TC0190 game never writes $E000 (its board has no
 * such register), so the first write there switches this mapper to the TC0690 rules.
 */
typedef struct {
    uint8_t tc0690;
} mapper33_t;

static void nes_mapper_init(nes_t* nes) {
    if (nes->nes_mapper.mapper_register == NULL) {
        nes->nes_mapper.mapper_register = nes_malloc(sizeof(mapper33_t));
        if (nes->nes_mapper.mapper_register == NULL) return;
    }
    nes_memset(nes->nes_mapper.mapper_register, 0, sizeof(mapper33_t));
    uint16_t prg_banks = (uint16_t)(nes->nes_rom.prg_rom_size * 2);
    nes_load_prgrom_8k(nes, 0, 0);
    nes_load_prgrom_8k(nes, 1, 1);
    nes_load_prgrom_8k(nes, 2, (uint8_t)(prg_banks - 2));
    nes_load_prgrom_8k(nes, 3, (uint8_t)(prg_banks - 1));
    nes_load_chrrom_8k(nes, 0, 0);
}

static void nes_mapper_deinit(nes_t* nes) {
    nes_free(nes->nes_mapper.mapper_register);
    nes->nes_mapper.mapper_register = NULL;
}

/*
 * $8000: bits[5:0]=8KB PRG bank for $8000-$9FFF; bit[6]=mirror (0=V,1=H) on TC0190
 * $8001: bits[5:0]=8KB PRG bank for $A000-$BFFF
 * $8002: 2KB CHR bank (PPU $0000-$07FF)
 * $8003: 2KB CHR bank (PPU $0800-$0FFF)
 * $A000: 1KB CHR bank (PPU $1000-$13FF)
 * $A001: 1KB CHR bank (PPU $1400-$17FF)
 * $A002: 1KB CHR bank (PPU $1800-$1BFF)
 * $A003: 1KB CHR bank (PPU $1C00-$1FFF)
 * $E000: TC0690 mirroring register, bit[6] (0=V,1=H) — see FCEUX boards/33.cpp
 */
static void nes_mapper_write(nes_t* nes, uint16_t address, uint8_t data) {
    mapper33_t* m = (mapper33_t*)nes->nes_mapper.mapper_register;
    switch (address & 0xE003u) {
    case 0x8000u:
        nes_load_prgrom_8k(nes, 0, data & 0x3Fu);
        if (m != NULL && m->tc0690 == 0u && nes->nes_rom.four_screen == 0) {
            nes_mirror_type_t mt = (data & 0x40u) ? NES_MIRROR_HORIZONTAL : NES_MIRROR_VERTICAL;
            nes_ppu_screen_mirrors(nes, mt);
        }
        break;
    case 0x8001u:
        nes_load_prgrom_8k(nes, 1, data & 0x3Fu);
        break;
    case 0x8002u: /* 2KB CHR at PPU $0000 */
        nes_load_chrrom_1k(nes, 0, (uint16_t)data * 2u);
        nes_load_chrrom_1k(nes, 1, (uint16_t)data * 2u + 1u);
        break;
    case 0x8003u: /* 2KB CHR at PPU $0800 */
        nes_load_chrrom_1k(nes, 2, (uint16_t)data * 2u);
        nes_load_chrrom_1k(nes, 3, (uint16_t)data * 2u + 1u);
        break;
    case 0xA000u:
        nes_load_chrrom_1k(nes, 4, data);
        break;
    case 0xA001u:
        nes_load_chrrom_1k(nes, 5, data);
        break;
    case 0xA002u:
        nes_load_chrrom_1k(nes, 6, data);
        break;
    case 0xA003u:
        nes_load_chrrom_1k(nes, 7, data);
        break;
    case 0xE000u:
        if (m != NULL) m->tc0690 = 1u;
        if (nes->nes_rom.four_screen == 0) {
            nes_mirror_type_t mt = (data & 0x40u) ? NES_MIRROR_HORIZONTAL : NES_MIRROR_VERTICAL;
            nes_ppu_screen_mirrors(nes, mt);
        }
        break;
    default:
        break;
    }
}

int nes_mapper33_init(nes_t* nes) {
    nes->nes_mapper.mapper_init   = nes_mapper_init;
    nes->nes_mapper.mapper_deinit = nes_mapper_deinit;
    nes->nes_mapper.mapper_write  = nes_mapper_write;
    return NES_OK;
}
