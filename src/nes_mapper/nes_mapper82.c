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

/* Taito X1-017 (iNES mapper 82) — 8KB PRG pages, 1KB CHR pages, registers at $7EF0-$7EFF
 * (reached through the $6000-$7FFF window, hence the mapper_sram hook).
 * Register map follows Mesen's TaitoX1017:
 *   $7EF0-$7EF5 : CHR registers 0-5
 *   $7EF6       : bit0 = mirroring (1 = vertical), bit1 = CHR mode
 *   $7EF7-$7EF9 : RAM permission ($CA / $69 / $84) for $6000-$73FF
 *   $7EFA       : PRG 8KB page 0 ($8000-$9FFF) = value >> 2
 *   $7EFB       : PRG 8KB page 1 ($A000-$BFFF) = value >> 2
 *   $7EFC       : PRG 8KB page 2 ($C000-$DFFF) = value >> 2
 *   $7EFD-$7EFF: unused (the board only decodes $7EFA-$7EFC)
 * CHR layout:
 *   mode 0: $0000-$07FF = reg0 (2KB, LSB ignored), $0800-$0FFF = reg1 (2KB),
 *           $1000-$1FFF = regs 2-5 (four 1KB pages)
 *   mode 1: $0000-$0FFF = regs 2-5 (four 1KB pages),
 *           $1000-$17FF = reg0 (2KB), $1800-$1FFF = reg1 (2KB)
 * The last 8KB PRG page is fixed at $E000-$FFFF.
 */

typedef struct mapper82_register {
    uint8_t chr[6];
    uint8_t ram_permission[3];
    uint8_t chr_mode;
} mapper82_register_t;

static void mapper82_update_chr(nes_t* nes) {
    mapper82_register_t* r = (mapper82_register_t*)nes->nes_mapper.mapper_register;
    if (nes->nes_rom.chr_rom_size == 0) {
        return;
    }
    if (r->chr_mode == 0) {
        nes_load_chrrom_1k(nes, 0, (uint8_t)(r->chr[0] & 0xFEu));
        nes_load_chrrom_1k(nes, 1, (uint8_t)((r->chr[0] & 0xFEu) + 1u));
        nes_load_chrrom_1k(nes, 2, (uint8_t)(r->chr[1] & 0xFEu));
        nes_load_chrrom_1k(nes, 3, (uint8_t)((r->chr[1] & 0xFEu) + 1u));
        nes_load_chrrom_1k(nes, 4, r->chr[2]);
        nes_load_chrrom_1k(nes, 5, r->chr[3]);
        nes_load_chrrom_1k(nes, 6, r->chr[4]);
        nes_load_chrrom_1k(nes, 7, r->chr[5]);
    } else {
        nes_load_chrrom_1k(nes, 0, r->chr[2]);
        nes_load_chrrom_1k(nes, 1, r->chr[3]);
        nes_load_chrrom_1k(nes, 2, r->chr[4]);
        nes_load_chrrom_1k(nes, 3, r->chr[5]);
        nes_load_chrrom_1k(nes, 4, (uint8_t)(r->chr[0] & 0xFEu));
        nes_load_chrrom_1k(nes, 5, (uint8_t)((r->chr[0] & 0xFEu) + 1u));
        nes_load_chrrom_1k(nes, 6, (uint8_t)(r->chr[1] & 0xFEu));
        nes_load_chrrom_1k(nes, 7, (uint8_t)((r->chr[1] & 0xFEu) + 1u));
    }
}

static void nes_mapper_init(nes_t* nes) {
    if (nes->nes_mapper.mapper_register == NULL) {
        nes->nes_mapper.mapper_register = nes_mapper_register_alloc(nes, (uint16_t)sizeof(mapper82_register_t));
        if (nes->nes_mapper.mapper_register == NULL) return;
    }
    mapper82_register_t* r = (mapper82_register_t*)nes->nes_mapper.mapper_register;
    nes_memset(r, 0, sizeof(mapper82_register_t));

    uint8_t prg_banks = (uint8_t)(nes->nes_rom.prg_rom_size * 2u);
    /* Power-on: $E000-$FFFF is the fixed last 8KB page, everything else starts at page 0. */
    nes_load_prgrom_8k(nes, 0, 0);
    nes_load_prgrom_8k(nes, 1, 0);
    nes_load_prgrom_8k(nes, 2, 0);
    nes_load_prgrom_8k(nes, 3, (uint8_t)(prg_banks - 1u));
    mapper82_update_chr(nes);
}

static void nes_mapper_deinit(nes_t* nes) {
    if (nes->nes_mapper.mapper_register != NULL) {
        nes_free(nes->nes_mapper.mapper_register);
        nes->nes_mapper.mapper_register = NULL;
    }
}

static void nes_mapper_sram(nes_t* nes, uint16_t address, uint8_t data) {
    mapper82_register_t* r = (mapper82_register_t*)nes->nes_mapper.mapper_register;
    if (r == NULL) {
        return;
    }
    switch (address) {
    case 0x7EF0u: case 0x7EF1u: case 0x7EF2u:
    case 0x7EF3u: case 0x7EF4u: case 0x7EF5u:
        r->chr[address & 0x07u] = data;
        mapper82_update_chr(nes);
        break;
    case 0x7EF6u:
        r->chr_mode = (uint8_t)((data >> 1) & 0x01u);
        if (nes->nes_rom.four_screen == 0) {
            nes_ppu_screen_mirrors(nes, (data & 0x01u) ? NES_MIRROR_VERTICAL : NES_MIRROR_HORIZONTAL);
        }
        mapper82_update_chr(nes);
        break;
    case 0x7EF7u:
    case 0x7EF8u:
    case 0x7EF9u:
        /* RAM permission for $6000-$73FF ($CA / $69 / $84). The core hands the whole 8KB
         * window to the mapper-allocated RAM, so the value is recorded but not enforced. */
        r->ram_permission[(address - 0x7EF7u) & 0x03u] = data;
        break;
    case 0x7EFAu:
        nes_load_prgrom_8k(nes, 0, (uint16_t)(data >> 2));
        break;
    case 0x7EFBu:
        nes_load_prgrom_8k(nes, 1, (uint16_t)(data >> 2));
        break;
    case 0x7EFCu:
        nes_load_prgrom_8k(nes, 2, (uint16_t)(data >> 2));
        break;
    default:
        break;
    }
}

int nes_mapper82_init(nes_t* nes) {
    nes->nes_mapper.mapper_init   = nes_mapper_init;
    nes->nes_mapper.mapper_deinit = nes_mapper_deinit;
    nes->nes_mapper.mapper_sram   = nes_mapper_sram;
    return NES_OK;
}
