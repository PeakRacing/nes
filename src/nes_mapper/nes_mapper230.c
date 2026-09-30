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

/* https://www.nesdev.org/wiki/INES_Mapper_230
 * BMC multicart with a *reset-selected* game: the board boots into "Contra mode" and toggles the
 * mode on every soft reset.  This mirrors Mesen's Core/NES/Mappers/Unlicensed/Mapper230.h, whose
 * InitMapper() calls Reset(true), so the first boot is Contra mode:
 *
 *   Contra mode: $8000-$BFFF <- value & 0x07 (16KB bank switching), $C000-$FFFF <- 7 (kernel),
 *                vertical mirroring
 *   Mappy mode:  value bit 5 set -> both halves on (value & 0x1F) + 8,
 *                else the pair (value & 0x1E) + 8 / + 9;
 *                mirroring from value bit 6 (set = vertical)
 *
 * The previous implementation here powered up with $C000-$FFFF on the *last* bank of the image
 * (bank 39 of a 640KB ROM) instead of the kernel at bank 7, so the reset vector was fetched from
 * the wrong bank: the 22-in-1 stayed on a black screen (verdict ok, first_render=3 - the console
 * did enable rendering, but every pixel stayed the backdrop colour).
 */

typedef struct {
    uint8_t contra_mode;
} mapper230_register_t;

static void nes_mapper_deinit(nes_t* nes) {
    nes_free(nes->nes_mapper.mapper_register);
    nes->nes_mapper.mapper_register = NULL;
}

static void nes_mapper_init(nes_t* nes) {
    if (nes->nes_mapper.mapper_register == NULL) {
        nes->nes_mapper.mapper_register = nes_mapper_register_alloc(nes, (uint16_t)sizeof(mapper230_register_t));
        if (nes->nes_mapper.mapper_register == NULL) return;
    }
    mapper230_register_t* r = (mapper230_register_t*)nes->nes_mapper.mapper_register;
    nes_memset(r, 0, sizeof(mapper230_register_t));

    /* This is a CHR-RAM board: attach the 8KB pattern window unconditionally. */
    nes_load_chrrom_8k(nes, 0, 0);

    /* Mesen calls Reset(softReset = true) from InitMapper, which flips the flag to Contra mode. */
    r->contra_mode = 1u;
    nes_load_prgrom_16k(nes, 0, 0);
    nes_load_prgrom_16k(nes, 1, 7);
    if (nes->nes_rom.four_screen == 0) {
        nes_ppu_screen_mirrors(nes, NES_MIRROR_VERTICAL);
    }
}

static void nes_mapper_write(nes_t* nes, uint16_t address, uint8_t data) {
    (void)address;
    mapper230_register_t* r = (mapper230_register_t*)nes->nes_mapper.mapper_register;
    if (r == NULL) return;

    if (r->contra_mode) {
        nes_load_prgrom_16k(nes, 0, (uint16_t)(data & 0x07u));
        return;
    }

    if (data & 0x20u) {
        const uint16_t bank = (uint16_t)((data & 0x1Fu) + 8u);
        nes_load_prgrom_16k(nes, 0, bank);
        nes_load_prgrom_16k(nes, 1, bank);
    } else {
        const uint16_t bank = (uint16_t)((data & 0x1Eu) + 8u);
        nes_load_prgrom_16k(nes, 0, bank);
        nes_load_prgrom_16k(nes, 1, (uint16_t)(bank + 1u));
    }
    if (nes->nes_rom.four_screen == 0) {
        nes_ppu_screen_mirrors(nes, (data & 0x40u) ? NES_MIRROR_VERTICAL : NES_MIRROR_HORIZONTAL);
    }
}

static void nes_mapper_state_reapply(nes_t* nes) {
    /* The PRG slots are not derivable from the last register write alone (Contra mode keeps
       $C000-$FFFF on the kernel bank), so nothing to replay here. */
    (void)nes;
}

int nes_mapper230_init(nes_t* nes) {
    nes->nes_mapper.mapper_init          = nes_mapper_init;
    nes->nes_mapper.mapper_deinit        = nes_mapper_deinit;
    nes->nes_mapper.mapper_write         = nes_mapper_write;
    nes->nes_mapper.mapper_state_reapply = nes_mapper_state_reapply;
    return NES_OK;
}
