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

/* https://www.nesdev.org/wiki/INES_Mapper_045
 * Mapper 45 — Star Prolog/Yanchong (MMC3 + 4-register outer bank).
 * Sequential writes to $6000-$7FFF advance a 4-register FIFO:
 *   reg[0]: CHR outer base
 *   reg[1]: CHR mask
 *   reg[2]: PRG outer base
 *   reg[3]: PRG mask / mode
 * Actual CHR bank = outer_base | (inner & ~mask)
 * Actual PRG bank = outer_base | (inner & ~mask)
 */

typedef struct {
    uint8_t bank_select;
    uint8_t bank_values[8];
    uint8_t mirroring;
    uint8_t irq_latch;
    uint8_t irq_counter;
    uint8_t irq_reload;
    uint8_t irq_enabled;
    uint8_t prg_bank_count;
    uint8_t chr_bank_count;
    uint8_t regs[4];
    uint8_t latch_pos;
} mapper45_t;

static void nes_mapper_deinit(nes_t* nes) {
    nes_free(nes->nes_mapper.mapper_register);
    nes->nes_mapper.mapper_register = NULL;
}

static void mapper45_update_banks(nes_t* nes) {
    mapper45_t* m = (mapper45_t*)nes->nes_mapper.mapper_register;
    uint8_t prg_mode = (m->bank_select >> 6) & 1u;
    uint8_t chr_mode = (m->bank_select >> 7) & 1u;

    /* PRG outer: regs[2] = base, regs[3] = mask */
    uint8_t prg_base = (uint8_t)((m->regs[2] & 0x3Fu) << 1u);  /* in 8KB units */
    uint8_t prg_mask = (uint8_t)(~(m->regs[3] & 0x3Fu) & 0x3Fu);
    uint8_t prg_seg  = (uint8_t)((~prg_mask & 0x3Fu) + 1u);
    if (prg_seg == 0u) prg_seg = 1u;
    uint8_t last  = (uint8_t)(prg_base | prg_mask);
    uint8_t slast = (uint8_t)(prg_base | (prg_mask - 1u & prg_mask));

    if (prg_mode == 0u) {
        nes_load_prgrom_8k(nes, 0, (uint8_t)(prg_base | (m->bank_values[6] & prg_mask)));
        nes_load_prgrom_8k(nes, 1, (uint8_t)(prg_base | (m->bank_values[7] & prg_mask)));
        nes_load_prgrom_8k(nes, 2, slast);
        nes_load_prgrom_8k(nes, 3, last);
    } else {
        nes_load_prgrom_8k(nes, 0, slast);
        nes_load_prgrom_8k(nes, 1, (uint8_t)(prg_base | (m->bank_values[7] & prg_mask)));
        nes_load_prgrom_8k(nes, 2, (uint8_t)(prg_base | (m->bank_values[6] & prg_mask)));
        nes_load_prgrom_8k(nes, 3, last);
    }

    if (m->chr_bank_count == 0u) return;

    /* CHR outer: regs[0] = base, regs[1] = mask */
    uint8_t chr_base = (uint8_t)((m->regs[0] & 0x7Fu) << 1u);  /* in 1KB units */
    uint8_t chr_mask = (uint8_t)(m->regs[1] & 0x7Fu);

#define CHR_BANK(b) ((uint8_t)(chr_base | ((b) & chr_mask)))

    if (chr_mode == 0u) {
        nes_load_chrrom_1k(nes, 0, CHR_BANK(m->bank_values[0] & 0xFEu));
        nes_load_chrrom_1k(nes, 1, CHR_BANK(m->bank_values[0] | 0x01u));
        nes_load_chrrom_1k(nes, 2, CHR_BANK(m->bank_values[1] & 0xFEu));
        nes_load_chrrom_1k(nes, 3, CHR_BANK(m->bank_values[1] | 0x01u));
        nes_load_chrrom_1k(nes, 4, CHR_BANK(m->bank_values[2]));
        nes_load_chrrom_1k(nes, 5, CHR_BANK(m->bank_values[3]));
        nes_load_chrrom_1k(nes, 6, CHR_BANK(m->bank_values[4]));
        nes_load_chrrom_1k(nes, 7, CHR_BANK(m->bank_values[5]));
    } else {
        nes_load_chrrom_1k(nes, 0, CHR_BANK(m->bank_values[2]));
        nes_load_chrrom_1k(nes, 1, CHR_BANK(m->bank_values[3]));
        nes_load_chrrom_1k(nes, 2, CHR_BANK(m->bank_values[4]));
        nes_load_chrrom_1k(nes, 3, CHR_BANK(m->bank_values[5]));
        nes_load_chrrom_1k(nes, 4, CHR_BANK(m->bank_values[0] & 0xFEu));
        nes_load_chrrom_1k(nes, 5, CHR_BANK(m->bank_values[0] | 0x01u));
        nes_load_chrrom_1k(nes, 6, CHR_BANK(m->bank_values[1] & 0xFEu));
        nes_load_chrrom_1k(nes, 7, CHR_BANK(m->bank_values[1] | 0x01u));
    }
#undef CHR_BANK
}

static void nes_mapper_init(nes_t* nes) {
    if (nes->nes_mapper.mapper_register == NULL) {
        nes->nes_mapper.mapper_register = nes_malloc(sizeof(mapper45_t));
        if (nes->nes_mapper.mapper_register == NULL) return;
    }
    mapper45_t* m = (mapper45_t*)nes->nes_mapper.mapper_register;
    nes_memset(m, 0, sizeof(mapper45_t));

    m->prg_bank_count = (uint8_t)(nes->nes_rom.prg_rom_size * 2u);
    m->chr_bank_count = (uint8_t)(nes->nes_rom.chr_rom_size * 8u);
    m->regs[1] = 0x0Fu;   /* allow bits[3:0] of inner bank by default */
    m->regs[3] = 0x0Fu;
    m->bank_values[6] = 0;
    m->bank_values[7] = 1;

    if (nes->nes_rom.chr_rom_size == 0u) nes_load_chrrom_8k(nes, 0, 0);
    mapper45_update_banks(nes);
}

static void nes_mapper_write(nes_t* nes, uint16_t address, uint8_t data) {
    mapper45_t* m = (mapper45_t*)nes->nes_mapper.mapper_register;
    switch (address & 0xE001u) {
    case 0x8000: m->bank_select = data; mapper45_update_banks(nes); break;
    case 0x8001: {
        uint8_t reg = m->bank_select & 0x07u;
        m->bank_values[reg] = data;
        mapper45_update_banks(nes);
        break;
    }
    case 0xA000:
        m->mirroring = data & 1u;
        if (nes->nes_rom.four_screen == 0)
            nes_ppu_screen_mirrors(nes, m->mirroring ? NES_MIRROR_HORIZONTAL : NES_MIRROR_VERTICAL);
        break;
    case 0xA001: break;
    case 0xC000: m->irq_latch   = data; break;
    case 0xC001: m->irq_reload  = 1; break;
    case 0xE000: m->irq_enabled = 0; nes->nes_cpu.irq_pending = 0; break;
    case 0xE001: m->irq_enabled = 1; break;
    default: break;
    }
}

static void nes_mapper_sram(nes_t* nes, uint16_t address, uint8_t data) {
    mapper45_t* m = (mapper45_t*)nes->nes_mapper.mapper_register;
    (void)address;
    m->regs[m->latch_pos & 3u] = data;
    m->latch_pos = (m->latch_pos + 1u) & 3u;
    mapper45_update_banks(nes);
}

static void nes_mapper_hsync(nes_t* nes) {
    mapper45_t* m = (mapper45_t*)nes->nes_mapper.mapper_register;
    if (nes->nes_ppu.MASK_b == 0 && nes->nes_ppu.MASK_s == 0) return;
    if (m->irq_counter == 0u || m->irq_reload) {
        m->irq_counter = m->irq_latch;
    } else {
        m->irq_counter--;
    }
    if (m->irq_counter == 0u && m->irq_enabled) nes_cpu_irq(nes);
    m->irq_reload = 0;
}

int nes_mapper45_init(nes_t* nes) {
    nes->nes_mapper.mapper_init   = nes_mapper_init;
    nes->nes_mapper.mapper_deinit = nes_mapper_deinit;
    nes->nes_mapper.mapper_write  = nes_mapper_write;
    nes->nes_mapper.mapper_sram   = nes_mapper_sram;
    nes->nes_mapper.mapper_hsync  = nes_mapper_hsync;
    return NES_OK;
}
