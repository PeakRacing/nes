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

/* https://www.nesdev.org/wiki/INES_Mapper_242
 * Mapper 242 (Waixing, "1200-in-1") - Mesen's Core/NES/Mappers/Waixing/Mapper242.h:
 *
 *   prg 32KB page = (address >> 3) & 0x0F      address bits 6:3, 4 bits -> 16 x 32KB = 512KB
 *   mirroring     = address & 0x02             (0 = vertical, 1 = horizontal)
 *   Reset         = 32KB page 0 + vertical mirroring
 *
 * The write *address* is the register here - the data byte is ignored - and there is no fixed bank
 * at $C000: the whole $8000-$FFFF window is one 32KB page.  The 4-bit field matches this image
 * exactly (512KB = 16 x 32KB).  The previous implementation took the bank from (data >> 1) & 7 as
 * a 16KB page, kept the last 16KB fixed and read the mirroring from data bit 0, so the 1200-in-1
 * only ever produced a blank screen.
 */

typedef struct {
    uint8_t page;      /* the selected 32KB page */
} mapper242_t;

static void nes_mapper_deinit(nes_t* nes) {
    nes_free(nes->nes_mapper.mapper_register);
    nes->nes_mapper.mapper_register = NULL;
}

static void mapper242_apply(nes_t* nes) {
    mapper242_t* m = (mapper242_t*)nes->nes_mapper.mapper_register;
    if (m == NULL) return;
    /* 32KB page = the 16KB pair (page*2, page*2+1); the 16k loader is the idiom the boards that
       are known to work here use (mapper 62 and friends), and nes_load_prgrom_32k turned out not
       to map this window at power-on. */
    nes_load_prgrom_16k(nes, 0, (uint16_t)(m->page << 1));
    nes_load_prgrom_16k(nes, 1, (uint16_t)((m->page << 1) + 1u));
}

static void nes_mapper_init(nes_t* nes) {
    if (nes->nes_mapper.mapper_register == NULL) {
        nes->nes_mapper.mapper_register = nes_mapper_register_alloc(nes, (uint16_t)sizeof(mapper242_t));
        if (nes->nes_mapper.mapper_register == NULL) return;
    }
    mapper242_t* m = (mapper242_t*)nes->nes_mapper.mapper_register;
    m->page = 0;

    /* CHR-RAM boards (1200合1 is one) need the 8KB pattern window attached unconditionally -
       the same pitfall that mapper 93/97/226/231/245 hit. */
    nes_load_chrrom_8k(nes, 0, 0);
    mapper242_apply(nes);
    if (nes->nes_rom.four_screen == 0) {
        nes_ppu_screen_mirrors(nes, NES_MIRROR_VERTICAL);
    }
}

static void nes_mapper_write(nes_t* nes, uint16_t address, uint8_t data) {
    (void)data;                                   /* the address is the register */
    mapper242_t* m = (mapper242_t*)nes->nes_mapper.mapper_register;
    if (m == NULL) return;
    m->page = (uint8_t)((address >> 3) & 0x0Fu);
    mapper242_apply(nes);
    if (nes->nes_rom.four_screen == 0) {
        nes_ppu_screen_mirrors(nes, (address & 0x02u) ? NES_MIRROR_HORIZONTAL : NES_MIRROR_VERTICAL);
    }
}

static void nes_mapper_state_reapply(nes_t* nes) {
    mapper242_apply(nes);
}

int nes_mapper242_init(nes_t* nes) {
    nes->nes_mapper.mapper_init          = nes_mapper_init;
    nes->nes_mapper.mapper_deinit        = nes_mapper_deinit;
    nes->nes_mapper.mapper_write         = nes_mapper_write;
    nes->nes_mapper.mapper_state_reapply = nes_mapper_state_reapply;
    return NES_OK;
}
