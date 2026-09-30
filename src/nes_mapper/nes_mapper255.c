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

/* https://www.nesdev.org/wiki/INES_Mapper_255
 * Mapper 255 - BMC PCB-018, the discrete 110-in-1 / 115-in-1 board.  Mapper 225 is the same board
 * (FCEUX keeps both in one file: "PCB-018 board, discrete multigame cart 110-in-1, Mapper 225 /
 * Mapper 255") and decodes the *address* of the write, the data byte being ignored:
 *
 *   bank = (address >> 14) & 1          (added on top of the 6-bit fields, as bit 6)
 *   mirr = (address >> 13) & 1          (0 = vertical, 1 = horizontal)
 *   mode = (address >> 12) & 1          (0 = 32KB page prg >> 1, 1 = prg in both 16KB halves)
 *   chr  = (address & 0x3F) | (bank << 6)          -> 7 bits, matches the 128 x 8KB CHR ROM
 *   prg  = ((address >> 6) & 0x3F) | (bank << 6)   -> 7 bits, matches the 128 x 16KB PRG ROM
 *
 * The board also exposes four nibbles of "extra RAM" in the $5000-$5FFF window (address bit 11
 * selects it, the low two address bits pick one of the four): 115-in-1 (CRC B39D30B4, the very
 * image FCEUX names in that comment) relies on it, so both the read and the write side of that
 * window are wired to mapper_apu / mapper_read_apu here.
 *
 * The previous implementation here used address bits 14/13 for mirroring/mode (they are 13/12),
 * ignored the extra bank bit entirely and used a different PRG formula, which mapped both
 * multicarts onto the same wrong 32KB page - hence two different ROMs producing one identical
 * (and wrong) screen.
 */

typedef struct {
    uint8_t prg;
    uint8_t chr;
    uint8_t mode;
    uint8_t mirr;
    uint8_t extra_ram[4];
} mapper255_t;

static void nes_mapper_deinit(nes_t* nes) {
    nes_free(nes->nes_mapper.mapper_register);
    nes->nes_mapper.mapper_register = NULL;
}

static void mapper255_sync(nes_t* nes) {
    mapper255_t* m = (mapper255_t*)nes->nes_mapper.mapper_register;
    if (m == NULL) return;

    if (m->mode) {
        /* 16KB mode: the same bank fills both halves. */
        nes_load_prgrom_16k(nes, 0, m->prg);
        nes_load_prgrom_16k(nes, 1, m->prg);
    } else {
        /* 32KB mode: prg >> 1 selects the aligned pair. */
        nes_load_prgrom_32k(nes, 0, (uint16_t)(m->prg >> 1));
    }

    if (nes->nes_rom.chr_rom_size > 0) {
        nes_load_chrrom_8k(nes, 0, m->chr);
    }

    if (nes->nes_rom.four_screen == 0) {
        nes_ppu_screen_mirrors(nes, m->mirr ? NES_MIRROR_HORIZONTAL : NES_MIRROR_VERTICAL);
    }
}

static void nes_mapper_init(nes_t* nes) {
    if (nes->nes_mapper.mapper_register == NULL) {
        nes->nes_mapper.mapper_register = nes_mapper_register_alloc(nes, (uint16_t)sizeof(mapper255_t));
        if (nes->nes_mapper.mapper_register == NULL) return;
    }
    mapper255_t* m = (mapper255_t*)nes->nes_mapper.mapper_register;
    nes_memset(m, 0, sizeof(mapper255_t));
    mapper255_sync(nes);
}

static void nes_mapper_write(nes_t* nes, uint16_t address, uint8_t data) {
    (void)data;                                  /* the address is the register */
    mapper255_t* m = (mapper255_t*)nes->nes_mapper.mapper_register;
    if (m == NULL) return;

    const uint8_t bank = (uint8_t)((address >> 14) & 0x01u);
    m->mirr = (uint8_t)((address >> 13) & 0x01u);
    m->mode = (uint8_t)((address >> 12) & 0x01u);
    m->chr  = (uint8_t)((address & 0x3Fu) | (uint8_t)(bank << 6));
    m->prg  = (uint8_t)(((address >> 6) & 0x3Fu) | (uint8_t)(bank << 6));
    mapper255_sync(nes);
}

/* $5000-$5FFF: four nibbles of extra RAM behind address bit 11. */
static void nes_mapper_apu(nes_t* nes, uint16_t address, uint8_t data) {
    mapper255_t* m = (mapper255_t*)nes->nes_mapper.mapper_register;
    if (m == NULL) return;
    if (address & 0x0800u) {
        m->extra_ram[address & 0x03u] = (uint8_t)(data & 0x0Fu);
    }
}

static uint8_t nes_mapper_read_apu(nes_t* nes, uint16_t address) {
    mapper255_t* m = (mapper255_t*)nes->nes_mapper.mapper_register;
    if (m == NULL) return 0;
    if (address & 0x0800u) {
        return m->extra_ram[address & 0x03u];
    }
    return 0;
}

static void nes_mapper_state_reapply(nes_t* nes) {
    mapper255_sync(nes);
}

int nes_mapper255_init(nes_t* nes) {
    nes->nes_mapper.mapper_init          = nes_mapper_init;
    nes->nes_mapper.mapper_deinit        = nes_mapper_deinit;
    nes->nes_mapper.mapper_write         = nes_mapper_write;
    nes->nes_mapper.mapper_apu           = nes_mapper_apu;
    nes->nes_mapper.mapper_read_apu      = nes_mapper_read_apu;
    nes->nes_mapper.mapper_state_reapply = nes_mapper_state_reapply;
    return NES_OK;
}
