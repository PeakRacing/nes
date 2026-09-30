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

/* https://www.nesdev.org/wiki/INES_Mapper_162
 * Mapper 162 — Waixing.  Verified against Mesen2 Core/NES/Mappers/Waixing/Waixing162.h:
 *   - PRG page size is 0x8000 (32KB), CHR is fixed to 8KB page 0
 *   - the four registers live at $5000-$5FFF, selected by address bits 9-8
 *   - power-on values are regs[0]=3, regs[1]=0, regs[2]=0, regs[3]=7
 *   - the bank formula depends on bits 0 and 2 of regs[3], not on a single expression
 * The old implementation put a single register at $8000 with a 16KB page and one fixed
 * formula, so 西游记后传 booted into the wrong 32KB page and never turned the screen on.
 */

typedef struct {
    uint8_t regs[4];
    uint16_t prg_page_count;   /* 32KB pages */
    uint16_t chr_page_count;   /* 8KB pages */
} mapper162_t;

static void nes_mapper_deinit(nes_t* nes) {
    nes_free(nes->nes_mapper.mapper_register);
    nes->nes_mapper.mapper_register = NULL;
}

static void mapper162_update_banks(nes_t* nes) {
    mapper162_t* m = (mapper162_t*)nes->nes_mapper.mapper_register;
    uint16_t page;
    switch (m->regs[3] & 0x05u) {
    case 0x00u:
        page = (uint16_t)((m->regs[0] & 0x0Cu) | (m->regs[1] & 0x02u) | ((m->regs[2] & 0x0Fu) << 4));
        break;
    case 0x01u:
        page = (uint16_t)((m->regs[0] & 0x0Cu) | ((m->regs[2] & 0x0Fu) << 4));
        break;
    case 0x04u:
        page = (uint16_t)((m->regs[0] & 0x0Eu) | ((m->regs[1] >> 1) & 0x01u) | ((m->regs[2] & 0x0Fu) << 4));
        break;
    default: /* 0x05 */
        page = (uint16_t)((m->regs[0] & 0x0Fu) | ((m->regs[2] & 0x0Fu) << 4));
        break;
    }
    if (m->prg_page_count != 0u) page = (uint16_t)(page % m->prg_page_count);
    /* 32KB page -> the two 16KB slots of the core's PRG windows. */
    nes_load_prgrom_16k(nes, 0, (uint16_t)(page * 2u));
    nes_load_prgrom_16k(nes, 1, (uint16_t)(page * 2u + 1u));
    nes_load_chrrom_8k(nes, 0, 0);   /* CHR is not banked */
}

static void nes_mapper_init(nes_t* nes) {
    if (nes->nes_mapper.mapper_register == NULL) {
        nes->nes_mapper.mapper_register = nes_mapper_register_alloc(nes, (uint16_t)sizeof(mapper162_t));
        if (nes->nes_mapper.mapper_register == NULL) return;
    }
    mapper162_t* m = (mapper162_t*)nes->nes_mapper.mapper_register;
    nes_memset(m, 0, sizeof(mapper162_t));
    m->prg_page_count = (uint16_t)(nes->nes_rom.prg_rom_size / 2u);
    m->chr_page_count = (uint16_t)nes->nes_rom.chr_rom_size;
    m->regs[0] = 3u;   /* power-on values from Mesen */
    m->regs[1] = 0u;
    m->regs[2] = 0u;
    m->regs[3] = 7u;
    mapper162_update_banks(nes);

    /* 8KB work RAM at $6000-$7FFF (the Mesen database lists workRAM=8 for mapper 162 and the
     * iNES header has no battery bit).  The board's registers sit at $5000-$5FFF, one page
     * below, so the two windows do not collide. */
    if (nes->nes_rom.sram == NULL) {
        nes->nes_rom.sram = (uint8_t*)nes_malloc(SRAM_SIZE);
        if (nes->nes_rom.sram != NULL) {
            nes_memset(nes->nes_rom.sram, 0, SRAM_SIZE);
        } else {
            NES_LOG_ERROR("mapper162: failed to allocate work RAM\n");
        }
    }
}

/* The registers are in $5000-$5FFF, which the core routes to mapper_apu (not mapper_write),
 * so the write hook has to be installed there or every bank switch is dropped. */
static void nes_mapper_apu(nes_t* nes, uint16_t address, uint8_t data) {
    mapper162_t* m = (mapper162_t*)nes->nes_mapper.mapper_register;
    m->regs[(address >> 8) & 0x03u] = data;
    mapper162_update_banks(nes);
}

int nes_mapper162_init(nes_t* nes) {
    nes->nes_mapper.mapper_init   = nes_mapper_init;
    nes->nes_mapper.mapper_deinit = nes_mapper_deinit;
    nes->nes_mapper.mapper_apu    = nes_mapper_apu;
    return NES_OK;
}
