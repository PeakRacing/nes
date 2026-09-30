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

/* https://www.nesdev.org/wiki/INES_Mapper_249
 * Mapper 249 - Waixing MMC3 board with an extra $5000 register.  Mirrors Mesen's
 * Core/NES/Mappers/Mmc3Variants/MMC3_249.h: everything is standard MMC3 except that, once the game
 * has written $5000 with bit 1 set, every PRG/CHR page number passes through a bit permutation
 * before it reaches the ROM (the board stores its banks in a scrambled order):
 *
 *   CHR page: (p & 0x03) | ((p >> 1) & 0x04) | ((p >> 4) & 0x08) | ((p >> 2) & 0x10)
 *             | ((p << 3) & 0x20) | ((p << 2) & 0xC0)
 *   PRG page < 0x20: (p & 0x01) | ((p >> 3) & 0x02) | ((p >> 1) & 0x04) | ((p << 2) & 0x18)
 *   PRG page >= 0x20: the same formula as the CHR one, applied to p - 0x20.
 *
 * Two bugs made Chinese Waixing games like `三十六计 [外星科技]` hang on a black screen:
 *   1. `chr_bank_count` was a uint8_t, and 256KB of CHR is 256 1KB pages - truncated to 0, so the
 *      CHR mapping was skipped altogether (the same trap as mappers 121/165/...).
 *   2. The $5000 register and its permutation were not implemented at all, so the game's
 *      bank-switching self check (it sets R6/R7 and compares the byte at $BFFF) never matched and
 *      it spun forever at $F08F, never reaching the code that enables rendering.
 */

typedef struct {
    uint8_t bank_select;
    uint8_t bank_values[8];
    uint8_t mirroring;
    uint8_t irq_latch;
    uint8_t irq_counter;
    uint8_t irq_reload;
    uint8_t irq_enabled;
    uint8_t ex_reg;
    uint16_t prg_bank_count;
    uint16_t chr_bank_count;
} mapper249_t;

static void nes_mapper_deinit(nes_t* nes) {
    nes_free(nes->nes_mapper.mapper_register);
    nes->nes_mapper.mapper_register = NULL;
}

/* The board's bank-number permutation, enabled by $5000 bit 1. */
static uint16_t mapper249_permute_chr(uint16_t page) {
    return (uint16_t)((page & 0x03u) | ((page >> 1) & 0x04u) | ((page >> 4) & 0x08u) |
                      ((page >> 2) & 0x10u) | ((page << 3) & 0x20u) | ((page << 2) & 0xC0u));
}

static uint16_t mapper249_permute_prg(uint16_t page) {
    if (page < 0x20u) {
        return (uint16_t)((page & 0x01u) | ((page >> 3) & 0x02u) | ((page >> 1) & 0x04u) |
                          ((page << 2) & 0x18u));
    }
    return mapper249_permute_chr((uint16_t)(page - 0x20u));
}

static void mapper249_update_banks(nes_t* nes) {
    mapper249_t* m = (mapper249_t*)nes->nes_mapper.mapper_register;
    const uint8_t prg_mode = (m->bank_select >> 6) & 1u;
    const uint8_t chr_mode = (m->bank_select >> 7) & 1u;
    const uint16_t last  = (uint16_t)(m->prg_bank_count - 1u);
    const uint16_t slast = (uint16_t)(m->prg_bank_count - 2u);
    const uint8_t permute = (m->ex_reg & 0x02u) ? 1u : 0u;

    uint16_t prg[4];
    if (prg_mode == 0u) {
        prg[0] = (uint16_t)(m->bank_values[6] % m->prg_bank_count);
        prg[1] = (uint16_t)(m->bank_values[7] % m->prg_bank_count);
        prg[2] = slast;
        prg[3] = last;
    } else {
        prg[0] = slast;
        prg[1] = (uint16_t)(m->bank_values[7] % m->prg_bank_count);
        prg[2] = (uint16_t)(m->bank_values[6] % m->prg_bank_count);
        prg[3] = last;
    }
    for (uint8_t i = 0; i < 4u; i++) {
        const uint16_t page = permute ? mapper249_permute_prg(prg[i]) : prg[i];
        nes_load_prgrom_8k(nes, i, (uint16_t)(page % m->prg_bank_count));
    }

    if (m->chr_bank_count == 0u) return;

    uint16_t chr[8];
    if (chr_mode == 0u) {
        chr[0] = (uint16_t)(m->bank_values[0] & 0xFEu);
        chr[1] = (uint16_t)(m->bank_values[0] | 0x01u);
        chr[2] = (uint16_t)(m->bank_values[1] & 0xFEu);
        chr[3] = (uint16_t)(m->bank_values[1] | 0x01u);
        chr[4] = m->bank_values[2];
        chr[5] = m->bank_values[3];
        chr[6] = m->bank_values[4];
        chr[7] = m->bank_values[5];
    } else {
        chr[0] = m->bank_values[2];
        chr[1] = m->bank_values[3];
        chr[2] = m->bank_values[4];
        chr[3] = m->bank_values[5];
        chr[4] = (uint16_t)(m->bank_values[0] & 0xFEu);
        chr[5] = (uint16_t)(m->bank_values[0] | 0x01u);
        chr[6] = (uint16_t)(m->bank_values[1] & 0xFEu);
        chr[7] = (uint16_t)(m->bank_values[1] | 0x01u);
    }
    for (uint8_t i = 0; i < 8u; i++) {
        const uint16_t page = permute ? mapper249_permute_chr(chr[i]) : chr[i];
        nes_load_chrrom_1k(nes, i, (uint16_t)(page % m->chr_bank_count));
    }
}

static void nes_mapper_init(nes_t* nes) {
    if (nes->nes_mapper.mapper_register == NULL) {
        nes->nes_mapper.mapper_register = nes_mapper_register_alloc(nes, (uint16_t)sizeof(mapper249_t));
        if (nes->nes_mapper.mapper_register == NULL) return;
    }
    mapper249_t* m = (mapper249_t*)nes->nes_mapper.mapper_register;
    nes_memset(m, 0, sizeof(mapper249_t));
    m->prg_bank_count = (uint16_t)(nes->nes_rom.prg_rom_size * 2u);   /* 16KB units -> 8KB pages */
    m->chr_bank_count = (uint16_t)(nes->nes_rom.chr_rom_size * 8u);   /* 8KB units -> 1KB pages  */
    m->bank_values[6] = 0;
    m->bank_values[7] = 1;
    /* The board applies the permutation from power-on: the games verify the mapping (they set
       R6/R7 and compare the byte at $BFFF with their return address) *before* they ever get to a
       $5000 write, so starting unpermuted leaves them spinning in that check forever. */
    m->ex_reg = 0x02u;
    if (nes->nes_rom.chr_rom_size == 0u) nes_load_chrrom_8k(nes, 0, 0);
    mapper249_update_banks(nes);
}

static void nes_mapper_write(nes_t* nes, uint16_t address, uint8_t data) {
    mapper249_t* m = (mapper249_t*)nes->nes_mapper.mapper_register;
    if (m == NULL) return;

    if (address == 0x5000u) {
        /* Waixing bank-permutation latch: re-map everything with the new setting. */
        m->ex_reg = data;
        mapper249_update_banks(nes);
        return;
    }

    switch (address & 0xE001u) {
    case 0x8000: m->bank_select = data; mapper249_update_banks(nes); break;
    case 0x8001: {
        uint8_t reg = m->bank_select & 0x07u;
        m->bank_values[reg] = data;
        mapper249_update_banks(nes);
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

static void nes_mapper_hsync(nes_t* nes) {
    mapper249_t* m = (mapper249_t*)nes->nes_mapper.mapper_register;
    if (nes->nes_ppu.MASK_b == 0 && nes->nes_ppu.MASK_s == 0) return;
    if (m->irq_counter == 0u || m->irq_reload) {
        m->irq_counter = m->irq_latch;
    } else {
        m->irq_counter--;
    }
    if (m->irq_counter == 0u && m->irq_enabled) nes_cpu_irq(nes);
    m->irq_reload = 0;
}

static void nes_mapper_state_reapply(nes_t* nes) {
    mapper249_update_banks(nes);
}

int nes_mapper249_init(nes_t* nes) {
    nes->nes_mapper.mapper_init          = nes_mapper_init;
    nes->nes_mapper.mapper_deinit        = nes_mapper_deinit;
    nes->nes_mapper.mapper_write         = nes_mapper_write;
    nes->nes_mapper.mapper_hsync         = nes_mapper_hsync;
    nes->nes_mapper.mapper_state_reapply = nes_mapper_state_reapply;
    return NES_OK;
}
