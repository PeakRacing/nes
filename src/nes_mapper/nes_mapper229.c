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

/* https://www.nesdev.org/wiki/INES_Mapper_229
 * BMC 31-in-1 - the *address* of the write is the register (the data byte is ignored), decoded
 * exactly as Mesen's Core/NES/Mappers/Unlicensed/Mapper229.h does:
 *
 *   CHR 8KB bank = address & 0xFF
 *   if (address & 0x1E) == 0   -> 32KB page 0 fills $8000-$FFFF
 *   else                       -> address & 0x1F fills *both* 16KB halves (the same bank twice)
 *   mirroring = address bit 5 (0 = vertical, 1 = horizontal)
 *
 * Mesen's InitMapper() calls the same register write with $8000, which lands on the 32KB page 0
 * with vertical wiring.  The previous implementation here decoded a five-bit CHR bank, used
 * address bit 0 as a "32KB mode" flag and put bank+1 in the second half, so the 31-in-1 never left
 * its boot screen (verdict blank, first_render=0).
 */

typedef struct {
    uint16_t last_addr;
} mapper229_register_t;

static void nes_mapper_deinit(nes_t* nes) {
    nes_free(nes->nes_mapper.mapper_register);
    nes->nes_mapper.mapper_register = NULL;
}

static void mapper229_update(nes_t* nes) {
    mapper229_register_t* r = (mapper229_register_t*)nes->nes_mapper.mapper_register;
    const uint16_t address = r->last_addr;

    if (nes->nes_rom.chr_rom_size > 0) {
        nes_load_chrrom_8k(nes, 0, (uint16_t)(address & 0xFFu));
    }

    if ((address & 0x1Eu) == 0u) {
        /* 32KB mode: the whole 32KB page 0. */
        nes_load_prgrom_32k(nes, 0, 0);
    } else {
        /* 16KB mode: the same bank in both halves. */
        const uint16_t bank = (uint16_t)(address & 0x1Fu);
        nes_load_prgrom_16k(nes, 0, bank);
        nes_load_prgrom_16k(nes, 1, bank);
    }

    if (nes->nes_rom.four_screen == 0) {
        nes_ppu_screen_mirrors(nes, (address & 0x20u) ? NES_MIRROR_HORIZONTAL : NES_MIRROR_VERTICAL);
    }
}

static void nes_mapper_init(nes_t* nes) {
    if (nes->nes_mapper.mapper_register == NULL) {
        nes->nes_mapper.mapper_register = nes_mapper_register_alloc(nes, (uint16_t)sizeof(mapper229_register_t));
        if (nes->nes_mapper.mapper_register == NULL) return;
    }
    mapper229_register_t* r = (mapper229_register_t*)nes->nes_mapper.mapper_register;
    nes_memset(r, 0, sizeof(mapper229_register_t));

    /* Mesen powers up through WriteRegister(0x8000, 0); on a CHR-RAM board the pattern window has
       to be attached unconditionally, which that write already does (CHR bank 0). */
    if (nes->nes_rom.chr_rom_size == 0) {
        nes_load_chrrom_8k(nes, 0, 0);
    }
    r->last_addr = 0x8000u;
    mapper229_update(nes);
}

static void nes_mapper_write(nes_t* nes, uint16_t address, uint8_t data) {
    (void)data;                                  /* the address is the register */
    mapper229_register_t* r = (mapper229_register_t*)nes->nes_mapper.mapper_register;
    if (r == NULL) return;
    r->last_addr = address;
    mapper229_update(nes);
}

static void nes_mapper_state_reapply(nes_t* nes) {
    mapper229_update(nes);
}

int nes_mapper229_init(nes_t* nes) {
    nes->nes_mapper.mapper_init          = nes_mapper_init;
    nes->nes_mapper.mapper_deinit        = nes_mapper_deinit;
    nes->nes_mapper.mapper_write         = nes_mapper_write;
    nes->nes_mapper.mapper_state_reapply = nes_mapper_state_reapply;
    return NES_OK;
}
