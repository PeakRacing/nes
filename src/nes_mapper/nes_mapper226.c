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
 * Mapper 226 — BMC 42-in-1 / "Super 42-in-1" pirate multicart (1MB PRG, 8KB CHR-RAM).
 *
 * Two write-only registers, decoded by A0 only ($8000 selects register 0, $8001 register 1):
 *
 *   bank number = (reg0 & 0x1F) | ((reg0 & 0x80) >> 2) | ((reg1 & 0x01) << 6)
 *                 \_________/    \_____________/       \_____________/
 *                   bits 0-4         bit 5                 bit 6
 *   reg0 & 0x20 : 32KB mode, both 16KB slots take that bank
 *                 otherwise the slots take the consecutive pair (bank & 0xFE, bank | 1)
 *   reg0 & 0x40 : mirroring (set -> vertical, clear -> horizontal)
 *
 * Power-on maps PRG page 0 at $8000 and page 1 at $C000, i.e. a valid reset vector is only
 * visible when both slots hold the consecutive pair — booting with a single bank in both slots
 * points the CPU at garbage and the game spins forever writing the mapper from $8307 with
 * rendering never enabled (gray screen).
 *
 * The cartridge has no CHR-ROM: the 8KB CHR-RAM window still has to be mapped to the pattern
 * tables, otherwise the game's $2007 uploads go nowhere (same trap as mappers 93/97/245).
 *
 * Reference: Mesen2 Core/NES/Mappers/Unlicensed/Mapper226.h.
 */

typedef struct {
    uint8_t reg0;
    uint8_t reg1;
} mapper226_reg_t;

/* 7-bit bank number assembled from both registers (Mesen: Mapper226::GetPrgPage). */
static uint16_t mapper226_prg_page(const mapper226_reg_t* r) {
    return (uint16_t)((uint16_t)(r->reg0 & 0x1Fu) |
                      (uint16_t)((r->reg0 & 0x80u) >> 2) |
                      (uint16_t)((uint16_t)(r->reg1 & 0x01u) << 6));
}

static void mapper226_update_prg(nes_t* nes) {
    mapper226_reg_t* r = (mapper226_reg_t*)nes->nes_mapper.mapper_register;
    const uint16_t total = (uint16_t)(nes->nes_rom.prg_rom_size * 2u);   /* 16KB units */
    const uint16_t page = mapper226_prg_page(r);

    if (r->reg0 & 0x20u) {
        /* 32KB mode: both slots take the same 16KB bank. */
        const uint16_t bank = (uint16_t)(page % total);
        (void)nes_load_prgrom_16k(nes, 0, bank);
        (void)nes_load_prgrom_16k(nes, 1, bank);
    } else {
        /* 16KB mode: an aligned consecutive pair. */
        const uint16_t bank = (uint16_t)((page & 0xFEu) % total);
        (void)nes_load_prgrom_16k(nes, 0, bank);
        (void)nes_load_prgrom_16k(nes, 1, (uint16_t)((bank + 1u) % total));
    }
}

static void nes_mapper_deinit(nes_t* nes) {
    nes_free(nes->nes_mapper.mapper_register);
    nes->nes_mapper.mapper_register = NULL;
}

static void nes_mapper_init(nes_t* nes) {
    if (nes->nes_mapper.mapper_register == NULL) {
        nes->nes_mapper.mapper_register = nes_mapper_register_alloc(nes, (uint16_t)sizeof(mapper226_reg_t));
        if (nes->nes_mapper.mapper_register == NULL) return;
    }
    mapper226_reg_t* r = (mapper226_reg_t*)nes->nes_mapper.mapper_register;
    nes_memset(r, 0, sizeof(mapper226_reg_t));

    /* Power-on: pages 0 and 1 (Mesen's InitMapper).  Mirroring keeps the header/database value
     * until the program writes register 0. */
    (void)nes_load_prgrom_16k(nes, 0, 0);
    (void)nes_load_prgrom_16k(nes, 1, 1);

    /* CHR-RAM board: map the window unconditionally (chr_rom_size is 0 here). */
    (void)nes_load_chrrom_8k(nes, 0, 0);
}

static void nes_mapper_write(nes_t* nes, uint16_t address, uint8_t data) {
    mapper226_reg_t* r = (mapper226_reg_t*)nes->nes_mapper.mapper_register;
    if (address & 0x01u) {
        r->reg1 = data;
    } else {
        r->reg0 = data;
    }
    mapper226_update_prg(nes);
    if (nes->nes_rom.four_screen == 0) {
        nes_ppu_screen_mirrors(nes, (r->reg0 & 0x40u) ? NES_MIRROR_VERTICAL : NES_MIRROR_HORIZONTAL);
    }
}

int nes_mapper226_init(nes_t* nes) {
    nes->nes_mapper.mapper_init   = nes_mapper_init;
    nes->nes_mapper.mapper_deinit = nes_mapper_deinit;
    nes->nes_mapper.mapper_write  = nes_mapper_write;
    return NES_OK;
}
