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
 * Mapper 99 — Nintendo VS. UniSystem (VS Battle City, VS Gumshoe, ...).
 *
 * The board register is not in cartridge space: it is latched by a *write to $4016*, the same port
 * the CPU uses to strobe the controllers, so that write must still reach the joypad as well (this
 * is why the core gained the mapper_io_write hook).
 *
 *   latch bit 2 -> 8KB CHR bank (the board carries 16KB of CHR, i.e. bank 0 or 1)
 *   latch bit 2 -> also drives the *first 8KB* PRG page (FCEUX: "Special for VS Gumshoe");
 *                  every other VS UniSystem game runs 32KB PRG fixed at $8000-$FFFF.
 *
 * $6000-$7FFF holds 8KB of work RAM and the board has four-screen VRAM, so there is no mirroring
 * register (iNES flags6 bit3 carries that and the core applies it).
 *
 * Reference: FCEUX src/boards/99.cpp (M99Power / M99Write / Sync).
 */

typedef struct {
    uint8_t latch;
    uint8_t prg_bank_count;   /* in 8KB units */
    uint8_t chr_bank_count;   /* in 8KB units */
} mapper99_t;

static void mapper99_update(nes_t* nes) {
    mapper99_t* m = (mapper99_t*)nes->nes_mapper.mapper_register;

    /* 32KB of PRG fixed at $8000-$FFFF (16KB VS boards wrap, which the core's loader handles). */
    (void)nes_load_prgrom_32k(nes, 0, 0);
    if (m->prg_bank_count > 0u) {
        (void)nes_load_prgrom_8k(nes, 0, (uint16_t)((m->latch & 0x04u) % m->prg_bank_count));
    }
    if (m->chr_bank_count > 0u) {
        (void)nes_load_chrrom_8k(nes, 0, (uint16_t)(((m->latch >> 2) & 0x01u) % m->chr_bank_count));
    }
}

static void nes_mapper_deinit(nes_t* nes) {
    nes_free(nes->nes_mapper.mapper_register);
    nes->nes_mapper.mapper_register = NULL;
}

static void nes_mapper_init(nes_t* nes) {
    if (nes->nes_mapper.mapper_register == NULL) {
        nes->nes_mapper.mapper_register = nes_mapper_register_alloc(nes, (uint16_t)sizeof(mapper99_t));
        if (nes->nes_mapper.mapper_register == NULL) return;
    }
    mapper99_t* m = (mapper99_t*)nes->nes_mapper.mapper_register;
    nes_memset(m, 0, sizeof(mapper99_t));
    m->prg_bank_count = (uint8_t)(nes->nes_rom.prg_rom_size * 2u);
    m->chr_bank_count = (uint8_t)(nes->nes_rom.chr_rom_size);

    /* The VS board always carries 8KB of work RAM at $6000-$7FFF.  Ports built with
     * NES_USE_SRAM=0 leave nes_rom.sram unset, and the games store their variables there. */
    if (nes->nes_rom.sram == NULL) {
        nes->nes_rom.sram = (uint8_t*)nes_malloc(SRAM_SIZE);
        if (nes->nes_rom.sram) {
            nes_memset(nes->nes_rom.sram, 0x00, SRAM_SIZE);
        }
    }

    if (m->chr_bank_count == 0u) {
        (void)nes_load_chrrom_8k(nes, 0, 0);
    }
    mapper99_update(nes);
}

/* $4016 write: latch the board register, then let the core do the usual controller strobe. */
static void nes_mapper_io_write(nes_t* nes, uint16_t address, uint8_t data) {
    if (address != 0x4016u) return;
    mapper99_t* m = (mapper99_t*)nes->nes_mapper.mapper_register;
    if (m->latch == data) return;          /* the strobe runs every frame; only reload on change */
    m->latch = data;
    mapper99_update(nes);
}

static void nes_mapper_state_reapply(nes_t* nes) {
    mapper99_update(nes);
}

int nes_mapper99_init(nes_t* nes) {
    nes->nes_mapper.mapper_init           = nes_mapper_init;
    nes->nes_mapper.mapper_deinit         = nes_mapper_deinit;
    nes->nes_mapper.mapper_io_write       = nes_mapper_io_write;
    nes->nes_mapper.mapper_state_reapply  = nes_mapper_state_reapply;
    return NES_OK;
}
