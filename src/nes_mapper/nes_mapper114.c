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
 * Mapper 114 - MMC3 with a scrambled register interface (风中奇缘 / 狮子王 pirates).
 * Authority: Mesen2 Core/NES/Mappers/Mmc3Variants/MMC3_114.h.
 *
 * The board is an ordinary MMC3 underneath, but the game talks to it through a wire-swap
 * that defeats naive MMC3 clones.  Every address keeps its MMC3 meaning except these:
 *
 *   $5000-$7FFF write : extra PRG register; bit 7 set -> BOTH 16KB halves show
 *                       (value & 0x0F), otherwise the standard MMC3 mapping applies
 *   $8001             : goes to MMC3's $A000 (mirroring!)
 *   $A000             : register SELECT, passed through the scramble table
 *                       `{0,3,1,5,6,7,2,4}` with the two mode bits preserved:
 *                       MMC3 $8000 = (value & 0xC0) | security[value & 0x07]
 *   $A001             : IRQ reload value (MMC3's $C000)
 *   $C000             : MMC3 bank DATA ($8001) - accepted only once after the $A000 select
 *   $C001             : IRQ reload flag
 *   $E000 / $E001     : IRQ disable + acknowledge / enable
 *
 * Mesen also forces the "MMC3 Rev A" IRQ variant for this board; the scanline counter below
 * is the same one the other MMC3 variants in this core use.
 *
 * The old implementation scrambled the register *bits* with a different table and had no
 * separate select/data protocol at all, so the games only ever saw the power-on banks.
 */

typedef struct {
    uint8_t bank_select;        /* MMC3 $8000 view, written through the scramble table */
    uint8_t bank_values[8];     /* R0-R7 */
    uint8_t mirroring;
    uint8_t irq_latch;
    uint8_t irq_counter;
    uint8_t irq_reload;
    uint8_t irq_enabled;
    uint8_t ex_regs[2];         /* [0] = $5000-$7FFF register, [1] = data latch */
    uint16_t prg_bank_count;    /* number of 8KB PRG banks */
    uint16_t chr_bank_count;    /* number of 1KB CHR banks */
} mapper114_t;

static const uint8_t mapper114_security[8] = { 0u, 3u, 1u, 5u, 6u, 7u, 2u, 4u };

static void nes_mapper_deinit(nes_t* nes) {
    nes_free(nes->nes_mapper.mapper_register);
    nes->nes_mapper.mapper_register = NULL;
}

static void mapper114_load_chr1k(nes_t* nes, mapper114_t* m, uint8_t slot, uint16_t bank) {
    if (m->chr_bank_count > 0u) {
        nes_load_chrrom_1k(nes, slot, (uint16_t)(bank % m->chr_bank_count));
    }
}

static void mapper114_update_banks(nes_t* nes) {
    mapper114_t* m = (mapper114_t*)nes->nes_mapper.mapper_register;
    const uint8_t prg_mode = (uint8_t)((m->bank_select >> 6) & 1u);
    const uint8_t chr_mode = (uint8_t)((m->bank_select >> 7) & 1u);
    const uint16_t count = (m->prg_bank_count != 0u) ? m->prg_bank_count : 1u;
    const uint16_t last = (uint16_t)(count - 1u);
    const uint16_t slast = (uint16_t)((count >= 2u) ? (count - 2u) : 0u);

    /* Extra PRG register: one 16KB bank in both halves when bit 7 is set. */
    if (m->ex_regs[0] & 0x80u) {
        const uint16_t half = (uint16_t)(m->ex_regs[0] & 0x0Fu);
        nes_load_prgrom_16k(nes, 0, half);
        nes_load_prgrom_16k(nes, 1, half);
    } else if (prg_mode == 0u) {
        nes_load_prgrom_8k(nes, 0, (uint16_t)(m->bank_values[6] % count));
        nes_load_prgrom_8k(nes, 1, (uint16_t)(m->bank_values[7] % count));
        nes_load_prgrom_8k(nes, 2, slast);
        nes_load_prgrom_8k(nes, 3, last);
    } else {
        nes_load_prgrom_8k(nes, 0, slast);
        nes_load_prgrom_8k(nes, 1, (uint16_t)(m->bank_values[7] % count));
        nes_load_prgrom_8k(nes, 2, (uint16_t)(m->bank_values[6] % count));
        nes_load_prgrom_8k(nes, 3, last);
    }

    if (chr_mode == 0u) {
        mapper114_load_chr1k(nes, m, 0, (uint16_t)(m->bank_values[0] & 0xFEu));
        mapper114_load_chr1k(nes, m, 1, (uint16_t)(m->bank_values[0] | 0x01u));
        mapper114_load_chr1k(nes, m, 2, (uint16_t)(m->bank_values[1] & 0xFEu));
        mapper114_load_chr1k(nes, m, 3, (uint16_t)(m->bank_values[1] | 0x01u));
        mapper114_load_chr1k(nes, m, 4, m->bank_values[2]);
        mapper114_load_chr1k(nes, m, 5, m->bank_values[3]);
        mapper114_load_chr1k(nes, m, 6, m->bank_values[4]);
        mapper114_load_chr1k(nes, m, 7, m->bank_values[5]);
    } else {
        mapper114_load_chr1k(nes, m, 0, m->bank_values[2]);
        mapper114_load_chr1k(nes, m, 1, m->bank_values[3]);
        mapper114_load_chr1k(nes, m, 2, m->bank_values[4]);
        mapper114_load_chr1k(nes, m, 3, m->bank_values[5]);
        mapper114_load_chr1k(nes, m, 4, (uint16_t)(m->bank_values[0] & 0xFEu));
        mapper114_load_chr1k(nes, m, 5, (uint16_t)(m->bank_values[0] | 0x01u));
        mapper114_load_chr1k(nes, m, 6, (uint16_t)(m->bank_values[1] & 0xFEu));
        mapper114_load_chr1k(nes, m, 7, (uint16_t)(m->bank_values[1] | 0x01u));
    }
}

static void nes_mapper_init(nes_t* nes) {
    if (nes->nes_mapper.mapper_register == NULL) {
        nes->nes_mapper.mapper_register = nes_mapper_register_alloc(nes, (uint16_t)sizeof(mapper114_t));
        if (nes->nes_mapper.mapper_register == NULL) return;
    }
    mapper114_t* m = (mapper114_t*)nes->nes_mapper.mapper_register;
    nes_memset(m, 0, sizeof(mapper114_t));

    m->prg_bank_count = (uint16_t)(nes->nes_rom.prg_rom_size * 2u);
    m->chr_bank_count = (uint16_t)(nes->nes_rom.chr_rom_size * 8u);
    m->bank_values[6] = 0;
    m->bank_values[7] = 1;

    mapper114_update_banks(nes);
}

static void nes_mapper_write(nes_t* nes, uint16_t address, uint8_t data) {
    mapper114_t* m = (mapper114_t*)nes->nes_mapper.mapper_register;

    if (address < 0x8000u) {
        m->ex_regs[0] = data;
        mapper114_update_banks(nes);
        return;
    }

    switch (address & 0xE001u) {
    case 0x8001u:
        /* Mirrors MMC3's $A000. */
        m->mirroring = (uint8_t)(data & 0x01u);
        if (nes->nes_rom.four_screen == 0) {
            nes_ppu_screen_mirrors(nes, m->mirroring ? NES_MIRROR_HORIZONTAL : NES_MIRROR_VERTICAL);
        }
        break;

    case 0xA000u:
        /* Register select through the scramble table, mode bits preserved. */
        m->bank_select = (uint8_t)((data & 0xC0u) | mapper114_security[data & 0x07u]);
        m->ex_regs[1] = 1u;
        mapper114_update_banks(nes);
        break;

    case 0xA001u:
        m->irq_latch = data;            /* IRQ reload value */
        break;

    case 0xC000u:
        if (m->ex_regs[1] != 0u) {      /* bank data, accepted once per $A000 select */
            m->ex_regs[1] = 0u;
            m->bank_values[m->bank_select & 0x07u] = data;
            mapper114_update_banks(nes);
        }
        break;

    case 0xC001u:
        m->irq_reload = 1u;
        break;

    case 0xE000u:
        m->irq_enabled = 0u;
        nes->nes_cpu.irq_pending = 0;
        break;

    case 0xE001u:
        m->irq_enabled = 1u;
        break;

    default:
        break;
    }
}

/* $5000-$7FFF also carries the extra PRG register (core routes it to mapper_sram). */
static void nes_mapper_sram(nes_t* nes, uint16_t address, uint8_t data) {
    mapper114_t* m = (mapper114_t*)nes->nes_mapper.mapper_register;
    m->ex_regs[0] = data;
    mapper114_update_banks(nes);
    (void)address;
}

static void nes_mapper_apu(nes_t* nes, uint16_t address, uint8_t data) {
    mapper114_t* m = (mapper114_t*)nes->nes_mapper.mapper_register;
    m->ex_regs[0] = data;
    mapper114_update_banks(nes);
    (void)address;
}

/* Scanline IRQ - the same shape as the other MMC3 variants in this core. */
static void nes_mapper_hsync(nes_t* nes) {
    mapper114_t* m = (mapper114_t*)nes->nes_mapper.mapper_register;
    if (nes->nes_ppu.MASK_b == 0 && nes->nes_ppu.MASK_s == 0) return;

    if (m->irq_counter == 0u || m->irq_reload) {
        m->irq_counter = m->irq_latch;
    } else {
        m->irq_counter--;
    }

    if (m->irq_counter == 0u && m->irq_enabled) {
        nes_cpu_irq(nes);
    }
    m->irq_reload = 0;
}

static void nes_mapper_state_reapply(nes_t* nes) {
    mapper114_update_banks(nes);
}

int nes_mapper114_init(nes_t* nes) {
    nes->nes_mapper.mapper_init   = nes_mapper_init;
    nes->nes_mapper.mapper_deinit = nes_mapper_deinit;
    nes->nes_mapper.mapper_write  = nes_mapper_write;
    nes->nes_mapper.mapper_sram   = nes_mapper_sram;
    nes->nes_mapper.mapper_apu    = nes_mapper_apu;
    nes->nes_mapper.mapper_hsync  = nes_mapper_hsync;
    nes->nes_mapper.mapper_state_reapply = nes_mapper_state_reapply;
    return NES_OK;
}
