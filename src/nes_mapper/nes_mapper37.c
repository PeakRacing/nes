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

/* https://www.nesdev.org/wiki/INES_Mapper_037
 * Mapper 37 — Nintendo SMB/Tetris/World Cup multicart (MMC3 + outer bank).
 * Outer register at $6000-$7FFF (any SRAM write):
 *   bits[2:0] = outer bank value
 *   bit[2]=1: SMB mode — force last 32KB PRG; bits[1:0] select 8KB CHR chunk
 *   bit[2]=0: normal MMC3 mode with outer PRG and CHR offset
 * Inner MMC3 PRG slots are offset by outer_prg*16 (in 8KB units).
 * Inner MMC3 CHR slots are offset by outer_chr*8 (in 1KB units).
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
    uint8_t outer;
} mapper37_t;

static void nes_mapper_deinit(nes_t* nes) {
    nes_free(nes->nes_mapper.mapper_register);
    nes->nes_mapper.mapper_register = NULL;
}

static void mapper37_update_banks(nes_t* nes) {
    mapper37_t* m = (mapper37_t*)nes->nes_mapper.mapper_register;
    uint8_t outer = m->outer;
    uint8_t prg_mode = (m->bank_select >> 6) & 1u;
    uint8_t chr_mode = (m->bank_select >> 7) & 1u;

    if (outer & 0x04u) {
        /* SMB mode: PRG fixed to last 32KB */
        uint8_t base = (uint8_t)(m->prg_bank_count - 4u);
        nes_load_prgrom_8k(nes, 0, base);
        nes_load_prgrom_8k(nes, 1, (uint8_t)(base + 1u));
        nes_load_prgrom_8k(nes, 2, (uint8_t)(base + 2u));
        nes_load_prgrom_8k(nes, 3, (uint8_t)(base + 3u));
    } else {
        uint8_t prg_off = (uint8_t)((outer & 0x03u) << 4u);  /* 128KB chunks */
        uint8_t seg_sz  = (uint8_t)(m->prg_bank_count / 4u);
        if (seg_sz == 0u) seg_sz = 1u;
        uint8_t last  = (uint8_t)(prg_off + seg_sz - 1u);
        uint8_t slast = (uint8_t)(prg_off + seg_sz - 2u);

        if (prg_mode == 0u) {
            nes_load_prgrom_8k(nes, 0, (uint8_t)(prg_off + m->bank_values[6] % seg_sz));
            nes_load_prgrom_8k(nes, 1, (uint8_t)(prg_off + m->bank_values[7] % seg_sz));
            nes_load_prgrom_8k(nes, 2, slast);
            nes_load_prgrom_8k(nes, 3, last);
        } else {
            nes_load_prgrom_8k(nes, 0, slast);
            nes_load_prgrom_8k(nes, 1, (uint8_t)(prg_off + m->bank_values[7] % seg_sz));
            nes_load_prgrom_8k(nes, 2, (uint8_t)(prg_off + m->bank_values[6] % seg_sz));
            nes_load_prgrom_8k(nes, 3, last);
        }
    }

    if (m->chr_bank_count == 0u) return;

    uint8_t chr_off = (uint8_t)((outer & 0x07u) << 3u);  /* 8KB CHR chunks */
    uint8_t seg_chr = (uint8_t)(m->chr_bank_count / 8u);
    if (seg_chr == 0u) seg_chr = 1u;

    if (chr_mode == 0u) {
        nes_load_chrrom_1k(nes, 0, (uint8_t)(chr_off + (m->bank_values[0] & 0xFEu) % (seg_chr * 8u)));
        nes_load_chrrom_1k(nes, 1, (uint8_t)(chr_off + (m->bank_values[0] | 0x01u) % (seg_chr * 8u)));
        nes_load_chrrom_1k(nes, 2, (uint8_t)(chr_off + (m->bank_values[1] & 0xFEu) % (seg_chr * 8u)));
        nes_load_chrrom_1k(nes, 3, (uint8_t)(chr_off + (m->bank_values[1] | 0x01u) % (seg_chr * 8u)));
        nes_load_chrrom_1k(nes, 4, (uint8_t)(chr_off + m->bank_values[2] % (seg_chr * 8u)));
        nes_load_chrrom_1k(nes, 5, (uint8_t)(chr_off + m->bank_values[3] % (seg_chr * 8u)));
        nes_load_chrrom_1k(nes, 6, (uint8_t)(chr_off + m->bank_values[4] % (seg_chr * 8u)));
        nes_load_chrrom_1k(nes, 7, (uint8_t)(chr_off + m->bank_values[5] % (seg_chr * 8u)));
    } else {
        nes_load_chrrom_1k(nes, 0, (uint8_t)(chr_off + m->bank_values[2] % (seg_chr * 8u)));
        nes_load_chrrom_1k(nes, 1, (uint8_t)(chr_off + m->bank_values[3] % (seg_chr * 8u)));
        nes_load_chrrom_1k(nes, 2, (uint8_t)(chr_off + m->bank_values[4] % (seg_chr * 8u)));
        nes_load_chrrom_1k(nes, 3, (uint8_t)(chr_off + m->bank_values[5] % (seg_chr * 8u)));
        nes_load_chrrom_1k(nes, 4, (uint8_t)(chr_off + (m->bank_values[0] & 0xFEu) % (seg_chr * 8u)));
        nes_load_chrrom_1k(nes, 5, (uint8_t)(chr_off + (m->bank_values[0] | 0x01u) % (seg_chr * 8u)));
        nes_load_chrrom_1k(nes, 6, (uint8_t)(chr_off + (m->bank_values[1] & 0xFEu) % (seg_chr * 8u)));
        nes_load_chrrom_1k(nes, 7, (uint8_t)(chr_off + (m->bank_values[1] | 0x01u) % (seg_chr * 8u)));
    }
}

static void nes_mapper_init(nes_t* nes) {
    if (nes->nes_mapper.mapper_register == NULL) {
        nes->nes_mapper.mapper_register = nes_malloc(sizeof(mapper37_t));
        if (nes->nes_mapper.mapper_register == NULL) return;
    }
    mapper37_t* m = (mapper37_t*)nes->nes_mapper.mapper_register;
    nes_memset(m, 0, sizeof(mapper37_t));

    m->prg_bank_count = (uint8_t)(nes->nes_rom.prg_rom_size * 2u);
    m->chr_bank_count = (uint8_t)(nes->nes_rom.chr_rom_size * 8u);
    m->bank_values[6] = 0;
    m->bank_values[7] = 1;

    if (nes->nes_rom.chr_rom_size == 0u) nes_load_chrrom_8k(nes, 0, 0);
    mapper37_update_banks(nes);
}

static void nes_mapper_write(nes_t* nes, uint16_t address, uint8_t data) {
    mapper37_t* m = (mapper37_t*)nes->nes_mapper.mapper_register;
    switch (address & 0xE001u) {
    case 0x8000: m->bank_select = data; mapper37_update_banks(nes); break;
    case 0x8001: {
        uint8_t reg = m->bank_select & 0x07u;
        m->bank_values[reg] = data;
        mapper37_update_banks(nes);
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
    mapper37_t* m = (mapper37_t*)nes->nes_mapper.mapper_register;
    (void)address;
    m->outer = data & 0x07u;
    mapper37_update_banks(nes);
}

static void nes_mapper_hsync(nes_t* nes) {
    mapper37_t* m = (mapper37_t*)nes->nes_mapper.mapper_register;
    if (nes->nes_ppu.MASK_b == 0 && nes->nes_ppu.MASK_s == 0) return;
    if (m->irq_counter == 0u || m->irq_reload) {
        m->irq_counter = m->irq_latch;
    } else {
        m->irq_counter--;
    }
    if (m->irq_counter == 0u && m->irq_enabled) nes_cpu_irq(nes);
    m->irq_reload = 0;
}

int nes_mapper37_init(nes_t* nes) {
    nes->nes_mapper.mapper_init   = nes_mapper_init;
    nes->nes_mapper.mapper_deinit = nes_mapper_deinit;
    nes->nes_mapper.mapper_write  = nes_mapper_write;
    nes->nes_mapper.mapper_sram   = nes_mapper_sram;
    nes->nes_mapper.mapper_hsync  = nes_mapper_hsync;
    return NES_OK;
}
