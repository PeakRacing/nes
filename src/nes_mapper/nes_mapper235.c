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

/* https://www.nesdev.org/wiki/INES_Mapper_235
 * Mapper 235 - "Golden Game 150-in-1" (the 260合1(150合1) image).  The write address is the
 * register; this follows FCEUX's src/boards/235.cpp:
 *
 *   bank   = ((address & 0x300) >> 3) | (address & 0x1F)      (7 bits: address bits 8-9 and 0-4)
 *   address bit 11 set: 16KB mode, $8000-$FFFF takes 16KB bank (bank << 1) | (address bit 12)
 *                       (the same bank in both halves)
 *   address bit 11 clear: 32KB mode, the whole 32KB page `bank`
 *   address bit 10 set: one-screen mirroring, else horizontal when address bit 13 is set
 *   a bank past the end of PRG answers with the data bus latch (open bus)
 *
 * The board also has a second, reset-selected "UNROM" personality for 128KB-multiple images
 * (FCEUX flips its `unrom` flag in M235Reset, i.e. on the reset button, not on power-up), which
 * is the other half of the 260-in-1 menu.
 *
 * The previous implementation here used `address & 0xFF` as the bank, bit 8 as the mode and bit 9
 * as the mirroring, so `260合1` only ever drew its title and the green border - the game list came
 * out blank.
 */

typedef struct {
    uint16_t cmd;
    uint8_t  unrom_data;
    uint8_t  unrom;
    uint8_t  open_bus;
    uint8_t  last_data;
} mapper235_t;

static void nes_mapper_deinit(nes_t* nes) {
    nes_free(nes->nes_mapper.mapper_register);
    nes->nes_mapper.mapper_register = NULL;
}

static void mapper235_sync(nes_t* nes) {
    mapper235_t* m = (mapper235_t*)nes->nes_mapper.mapper_register;
    if (m == NULL) return;

    const uint16_t prg_16k_pages = (uint16_t)(nes->nes_rom.prg_rom_size);   /* 16KB units */

    if (m->unrom) {
        /* Reset-selected UNROM personality: fixed last 8KB-page group above, 16KB window below. */
        nes_load_prgrom_16k(nes, 0, (uint16_t)(((prg_16k_pages - 1u) & 0xF8u) | (m->unrom_data & 0x07u)));
        nes_load_prgrom_16k(nes, 1, (uint16_t)((prg_16k_pages - 1u) & 0xF8u | 0x07u));
        if (nes->nes_rom.four_screen == 0) {
            nes_ppu_screen_mirrors(nes, NES_MIRROR_VERTICAL);
        }
        m->open_bus = 0;
        return;
    }

    const uint16_t bank = (uint16_t)(((m->cmd & 0x300u) >> 3) | (m->cmd & 0x1Fu));
    if (bank >= (uint16_t)(nes->nes_rom.prg_rom_size >> 1)) {   /* 32KB pages */
        m->open_bus = 1;
        return;
    }
    m->open_bus = 0;

    if (m->cmd & 0x800u) {
        const uint16_t page = (uint16_t)((bank << 1) | ((m->cmd >> 12) & 0x01u));
        nes_load_prgrom_16k(nes, 0, page);
        nes_load_prgrom_16k(nes, 1, page);
    } else {
        nes_load_prgrom_32k(nes, 0, bank);
    }

    if (nes->nes_rom.four_screen == 0) {
        if (m->cmd & 0x400u) {
            nes_ppu_screen_mirrors(nes, NES_MIRROR_ONE_SCREEN0);
        } else {
            nes_ppu_screen_mirrors(nes, ((m->cmd >> 13) & 0x01u) ? NES_MIRROR_HORIZONTAL : NES_MIRROR_VERTICAL);
        }
    }
}

/* FCEUX's M235Read answers with the data bus latch while the selected bank is past the end of
 * PRG.  This core's mapper_read_prg hook *replaces* the normal PRG read outright (nes_cpu.c:163),
 * so the in-range case below replicates that default read (prg_banks[(addr >> 13) - 4]) exactly -
 * normal fetches are byte-for-byte unchanged - and only the out-of-range case returns the latch.
 * Installed in init (it needs the register block to exist first). */
static uint8_t nes_mapper_read_prg(nes_t* nes, uint16_t address) {
    mapper235_t* m = (mapper235_t*)nes->nes_mapper.mapper_register;
    if (m != NULL && m->open_bus) {
        m->open_bus = 0;
        return m->last_data;
    }
    return nes->nes_cpu.prg_banks[(uint8_t)((address >> 13) - 4u)][address & 0x1FFFu];
}

static void nes_mapper_init(nes_t* nes) {
    if (nes->nes_mapper.mapper_register == NULL) {
        nes->nes_mapper.mapper_register = nes_mapper_register_alloc(nes, (uint16_t)sizeof(mapper235_t));
        if (nes->nes_mapper.mapper_register == NULL) return;
    }
    mapper235_t* m = (mapper235_t*)nes->nes_mapper.mapper_register;
    nes_memset(m, 0, sizeof(mapper235_t));

    /* CHR-RAM board: attach the 8KB pattern window unconditionally. */
    nes_load_chrrom_8k(nes, 0, 0);
    mapper235_sync(nes);
    nes->nes_mapper.mapper_read_prg = nes_mapper_read_prg;
}

static void nes_mapper_write(nes_t* nes, uint16_t address, uint8_t data) {
    mapper235_t* m = (mapper235_t*)nes->nes_mapper.mapper_register;
    if (m == NULL) return;
    m->cmd = address;                            /* the address is the register */
    m->unrom_data = data;
    m->last_data = data;
    mapper235_sync(nes);
}


static void nes_mapper_state_reapply(nes_t* nes) {
    mapper235_sync(nes);
}

int nes_mapper235_init(nes_t* nes) {
    nes->nes_mapper.mapper_init          = nes_mapper_init;
    nes->nes_mapper.mapper_deinit        = nes_mapper_deinit;
    nes->nes_mapper.mapper_write         = nes_mapper_write;
    nes->nes_mapper.mapper_state_reapply = nes_mapper_state_reapply;
    return NES_OK;
}
