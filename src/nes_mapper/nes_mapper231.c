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

/* https://www.nesdev.org/wiki/INES_Mapper_231
 * BMC 20-in-1 - the *address* of the write is the register, the data byte is ignored.  Matches
 * Mesen's Core/NES/Mappers/Unlicensed/Mapper231.h:
 *
 *   prg bank = ((address >> 5) & 0x01) | (address & 0x1E)   (5 bits: address bit 5 is bank bit 0,
 *                                                            address bits 1-4 are bank bits 1-4)
 *   $8000-$BFFF <- prg bank & 0x1E                           (the 32KB-aligned even bank)
 *   $C000-$FFFF <- prg bank                                  (that bank plus one)
 *   mirroring   = address bit 7 ? horizontal : vertical
 *
 * A previous implementation here treated address bit 4 as a "32KB mode" flag, used only four bank
 * bits and read the mirroring from bit 5, so the 20-in-1 image never left its boot screen
 * (verdict blank, first_render=0).  CHR is RAM on this board, so the CHR window is mapped once in
 * init and never rebanked.
 */

typedef struct {
    uint16_t last_addr;
} mapper231_register_t;

static void nes_mapper_deinit(nes_t* nes) {
    nes_free(nes->nes_mapper.mapper_register);
    nes->nes_mapper.mapper_register = NULL;
}

static void mapper231_update(nes_t* nes) {
    mapper231_register_t* r = (mapper231_register_t*)nes->nes_mapper.mapper_register;
    const uint16_t address = r->last_addr;
    const uint8_t prg_bank = (uint8_t)(((address >> 5) & 0x01u) | (address & 0x1Eu));

    nes_load_prgrom_16k(nes, 0, (uint16_t)(prg_bank & 0x1Eu));
    nes_load_prgrom_16k(nes, 1, (uint16_t)prg_bank);

    if (nes->nes_rom.four_screen == 0) {
        nes_ppu_screen_mirrors(nes, (address & 0x80u) ? NES_MIRROR_HORIZONTAL : NES_MIRROR_VERTICAL);
    }
}

static void nes_mapper_init(nes_t* nes) {
    if (nes->nes_mapper.mapper_register == NULL) {
        nes->nes_mapper.mapper_register = nes_mapper_register_alloc(nes, (uint16_t)sizeof(mapper231_register_t));
        if (nes->nes_mapper.mapper_register == NULL) return;
    }
    mapper231_register_t* r = (mapper231_register_t*)nes->nes_mapper.mapper_register;
    nes_memset(r, 0, sizeof(mapper231_register_t));

    /* This is a CHR-RAM board: the 8KB pattern window has to be attached unconditionally. */
    nes_load_chrrom_8k(nes, 0, 0);
    mapper231_update(nes);
}

static void nes_mapper_write(nes_t* nes, uint16_t address, uint8_t data) {
    (void)data;                                  /* the address is the register */
    mapper231_register_t* r = (mapper231_register_t*)nes->nes_mapper.mapper_register;
    if (r == NULL) return;
    r->last_addr = address;
    mapper231_update(nes);
}

static void nes_mapper_state_reapply(nes_t* nes) {
    mapper231_update(nes);
}

int nes_mapper231_init(nes_t* nes) {
    nes->nes_mapper.mapper_init          = nes_mapper_init;
    nes->nes_mapper.mapper_deinit        = nes_mapper_deinit;
    nes->nes_mapper.mapper_write         = nes_mapper_write;
    nes->nes_mapper.mapper_state_reapply = nes_mapper_state_reapply;
    return NES_OK;
}
