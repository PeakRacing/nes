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

/* https://www.nesdev.org/wiki/INES_Mapper_057
 * Mapper 57 - the Dendy multicart board (Globe / GK-47 style).  Two registers, both mirrored
 * across the whole $8000-$FFFF range in 2KB steps ($8000-$87FF -> register 0, $8800-$8FFF ->
 * register 1), exactly as Mesen's Core/NES/Mappers/Unlicensed/Mapper57.h decodes them:
 *
 *   register 1 bit 3     nametable mirroring: 0 = vertical, 1 = horizontal
 *   register 1 bit 4     PRG mode: 0 = the selected 16KB bank into both halves,
 *                                  1 = the selected 32KB pair (even bank plus the next one)
 *   register 1 bits 5-6  PRG bank number (16KB units)
 *   register 0 bit 6 and (register 0 | register 1) bits 0-2   8KB CHR bank
 *
 * The previous implementation here treated a single write value as "outer game select + PRG mode
 * + inner bank".  With that decode the 6-in-1 image never left its boot screen (verdict blank,
 * first_render=0) because the PRG window pointed at the wrong 16KB bank.
 */

typedef struct {
    uint8_t reg[2];
} mapper57_register_t;

static void nes_mapper_deinit(nes_t* nes) {
    nes_free(nes->nes_mapper.mapper_register);
    nes->nes_mapper.mapper_register = NULL;
}

static void mapper57_update(nes_t* nes) {
    mapper57_register_t* r = (mapper57_register_t*)nes->nes_mapper.mapper_register;
    const uint8_t reg0 = r->reg[0];
    const uint8_t reg1 = r->reg[1];

    if (nes->nes_rom.four_screen == 0) {
        nes_ppu_screen_mirrors(nes, (reg1 & 0x08u) ? NES_MIRROR_HORIZONTAL : NES_MIRROR_VERTICAL);
    }

    if (nes->nes_rom.chr_rom_size > 0) {
        nes_load_chrrom_8k(nes, 0, (uint16_t)(((reg0 & 0x40u) >> 3u) | ((reg0 | reg1) & 0x07u)));
    }

    if (reg1 & 0x10u) {
        /* 32KB mode: an even 16KB bank plus the following one. */
        const uint16_t bank = (uint16_t)((reg1 >> 5u) & 0x06u);
        nes_load_prgrom_16k(nes, 0, bank);
        nes_load_prgrom_16k(nes, 1, (uint16_t)(bank + 1u));
    } else {
        /* 16KB mode: the same bank mirrored into both halves. */
        const uint16_t bank = (uint16_t)((reg1 >> 5u) & 0x07u);
        nes_load_prgrom_16k(nes, 0, bank);
        nes_load_prgrom_16k(nes, 1, bank);
    }
}

static void nes_mapper_init(nes_t* nes) {
    if (nes->nes_mapper.mapper_register == NULL) {
        nes->nes_mapper.mapper_register = nes_mapper_register_alloc(nes, (uint16_t)sizeof(mapper57_register_t));
        if (nes->nes_mapper.mapper_register == NULL) return;
    }
    mapper57_register_t* r = (mapper57_register_t*)nes->nes_mapper.mapper_register;
    nes_memset(r, 0, sizeof(mapper57_register_t));
    mapper57_update(nes);
}

static void nes_mapper_write(nes_t* nes, uint16_t address, uint8_t data) {
    mapper57_register_t* r = (mapper57_register_t*)nes->nes_mapper.mapper_register;
    if (r == NULL) return;
    switch (address & 0x8800u) {
    case 0x8000u: r->reg[0] = data; break;
    case 0x8800u: r->reg[1] = data; break;
    default: return;
    }
    mapper57_update(nes);
}

static void nes_mapper_state_reapply(nes_t* nes) {
    mapper57_update(nes);
}

int nes_mapper57_init(nes_t* nes) {
    nes->nes_mapper.mapper_init          = nes_mapper_init;
    nes->nes_mapper.mapper_deinit        = nes_mapper_deinit;
    nes->nes_mapper.mapper_write         = nes_mapper_write;
    nes->nes_mapper.mapper_state_reapply = nes_mapper_state_reapply;
    return NES_OK;
}
