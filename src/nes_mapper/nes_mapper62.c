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

/* https://www.nesdev.org/wiki/INES_Mapper_062
 * Mapper 62 - "Super 700-in-1" (2MB PRG + 1MB CHR).  Mesen's Mappers/Unlicensed/Mapper62.h and
 * FCEUX's src/boards/62.cpp agree on the encoding, which mixes address and data bits:
 *
 *   prg page  = ((address & 0x3F00) >> 8) | (address & 0x40)    7 bits: address[13:8] + address[6]
 *   chr page  = ((address & 0x1F) << 2) | (value & 0x03)        7 bits: address[4:0] << 2 + data[1:0]
 *   address bit 5 set: 16KB mode - that same page fills both halves
 *   address bit 5 clear: 32KB mode - the aligned pair (page & 0xFE) and +1
 *   mirroring = address bit 7 (0 = vertical, 1 = horizontal)
 *
 * Both 7-bit fields match the image exactly (128 x 16KB PRG and 128 x 8KB CHR).  The previous
 * implementation here used `address >> 6` as the PRG page, data bit 7 as the 16KB/32KB flag, a
 * different CHR formula and never touched the mirroring, so the 700-in-1 stayed on a blank screen.
 */

typedef struct {
    uint8_t bank;      /* data[1:0] - the inner CHR/PRG bits */
    uint16_t mode;     /* address & 0x3FFF - the outer page, mode and mirroring bits */
} mapper62_t;

static void nes_mapper_deinit(nes_t* nes) {
    nes_free(nes->nes_mapper.mapper_register);
    nes->nes_mapper.mapper_register = NULL;
}

static void mapper62_sync(nes_t* nes) {
    mapper62_t* m = (mapper62_t*)nes->nes_mapper.mapper_register;
    if (m == NULL) return;

    const uint8_t prg_page = (uint8_t)(((m->mode & 0x3F00u) >> 8) | (m->mode & 0x40u));
    const uint8_t chr_page = (uint8_t)(((m->mode & 0x1Fu) << 2) | (m->bank & 0x03u));

    if (m->mode & 0x20u) {
        /* 16KB mode: the same page in both halves. */
        nes_load_prgrom_16k(nes, 0, prg_page);
        nes_load_prgrom_16k(nes, 1, prg_page);
    } else {
        /* 32KB mode: the aligned pair. */
        nes_load_prgrom_16k(nes, 0, (uint16_t)(prg_page & 0xFEu));
        nes_load_prgrom_16k(nes, 1, (uint16_t)((prg_page & 0xFEu) + 1u));
    }

    if (nes->nes_rom.chr_rom_size > 0) {
        nes_load_chrrom_8k(nes, 0, chr_page);
    }

    if (nes->nes_rom.four_screen == 0) {
        nes_ppu_screen_mirrors(nes, (m->mode & 0x80u) ? NES_MIRROR_HORIZONTAL : NES_MIRROR_VERTICAL);
    }
}

static void nes_mapper_init(nes_t* nes) {
    if (nes->nes_mapper.mapper_register == NULL) {
        nes->nes_mapper.mapper_register = nes_mapper_register_alloc(nes, (uint16_t)sizeof(mapper62_t));
        if (nes->nes_mapper.mapper_register == NULL) return;
    }
    mapper62_t* m = (mapper62_t*)nes->nes_mapper.mapper_register;
    nes_memset(m, 0, sizeof(mapper62_t));

    /* Power-on: page 0 in $8000 and page 1 in $A000 (both boards agree). */
    nes_load_prgrom_16k(nes, 0, 0);
    nes_load_prgrom_16k(nes, 1, 1);
    if (nes->nes_rom.chr_rom_size > 0) {
        nes_load_chrrom_8k(nes, 0, 0);
    }
    if (nes->nes_rom.four_screen == 0) {
        nes_ppu_screen_mirrors(nes, NES_MIRROR_VERTICAL);
    }
}

static void nes_mapper_write(nes_t* nes, uint16_t address, uint8_t data) {
    mapper62_t* m = (mapper62_t*)nes->nes_mapper.mapper_register;
    if (m == NULL) return;
    m->mode = (uint16_t)(address & 0x3FFFu);
    m->bank = (uint8_t)(data & 0x03u);
    mapper62_sync(nes);
}

static void nes_mapper_state_reapply(nes_t* nes) {
    mapper62_sync(nes);
}

int nes_mapper62_init(nes_t* nes) {
    nes->nes_mapper.mapper_init          = nes_mapper_init;
    nes->nes_mapper.mapper_deinit        = nes_mapper_deinit;
    nes->nes_mapper.mapper_write         = nes_mapper_write;
    nes->nes_mapper.mapper_state_reapply = nes_mapper_state_reapply;
    return NES_OK;
}
