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
 * Mapper 208 - MMC3 with a protection latch (快打传说 / Street Fighter V pirate).
 * Authority: Mesen2 Core/NES/Mappers/Mmc3Variants/MMC3_208.h.
 *
 * The board is an MMC3 for $8000-$FFFF, but the whole 32KB PRG window is one block taken
 * from an extension register instead of the MMC3 PRG registers, and the game has to talk
 * through a scrambler before the extension registers accept values:
 *
 *   $5000-$57FF write : latch the "seed"           -> exRegs[4]
 *   $5800-$5FFF write : exRegs[addr & 3] = value ^ protectionLut[exRegs[4]]
 *   $5800-$5FFF read  : return exRegs[addr & 3]    (the game checks what it wrote)
 *   $4800-$4FFF write : exRegs[5] = (value & 1) | ((value >> 3) & 2)   -> 32KB PRG block
 *   $6800-$6FFF write : same as $4800-$4FFF
 *   $8000-$FFFF       : ordinary MMC3 (bank select/data, mirroring, scanline IRQ)
 *
 * exRegs[5] powers on at 3, so the reset vector comes from the fourth 32KB block.
 *
 * The previous implementation was a different board again (an "FK23C + MMC3 hybrid" with an
 * outer bank register at $5000 and MMC3 PRG registers still in play), so the game never left
 * its power-on mapping.
 */

typedef struct {
    uint8_t bank_select;
    uint8_t bank_values[8];
    uint8_t mirroring;
    uint8_t irq_latch;
    uint8_t irq_counter;
    uint8_t irq_reload;
    uint8_t irq_enabled;
    uint16_t prg_bank_count;    /* 8KB units */
    uint16_t chr_bank_count;    /* 1KB units */
    uint8_t ex_regs[6];
} mapper208_t;

static const uint8_t mapper208_protection_lut[256] = {
    0x59, 0x59, 0x59, 0x59, 0x59, 0x59, 0x59, 0x59, 0x59, 0x49, 0x19, 0x09, 0x59, 0x49, 0x19, 0x09,
    0x59, 0x59, 0x59, 0x59, 0x59, 0x59, 0x59, 0x59, 0x51, 0x41, 0x11, 0x01, 0x51, 0x41, 0x11, 0x01,
    0x59, 0x59, 0x59, 0x59, 0x59, 0x59, 0x59, 0x59, 0x59, 0x49, 0x19, 0x09, 0x59, 0x49, 0x19, 0x09,
    0x59, 0x59, 0x59, 0x59, 0x59, 0x59, 0x59, 0x59, 0x51, 0x41, 0x11, 0x01, 0x51, 0x41, 0x11, 0x01,
    0x00, 0x10, 0x40, 0x50, 0x00, 0x10, 0x40, 0x50, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x08, 0x18, 0x48, 0x58, 0x08, 0x18, 0x48, 0x58, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x10, 0x40, 0x50, 0x00, 0x10, 0x40, 0x50, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x08, 0x18, 0x48, 0x58, 0x08, 0x18, 0x48, 0x58, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x59, 0x59, 0x59, 0x59, 0x59, 0x59, 0x59, 0x59, 0x58, 0x48, 0x18, 0x08, 0x58, 0x48, 0x18, 0x08,
    0x59, 0x59, 0x59, 0x59, 0x59, 0x59, 0x59, 0x59, 0x50, 0x40, 0x10, 0x00, 0x50, 0x40, 0x10, 0x00,
    0x59, 0x59, 0x59, 0x59, 0x59, 0x59, 0x59, 0x59, 0x58, 0x48, 0x18, 0x08, 0x58, 0x48, 0x18, 0x08,
    0x59, 0x59, 0x59, 0x59, 0x59, 0x59, 0x59, 0x59, 0x50, 0x40, 0x10, 0x00, 0x50, 0x40, 0x10, 0x00,
    0x01, 0x11, 0x41, 0x51, 0x01, 0x11, 0x41, 0x51, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x09, 0x19, 0x49, 0x59, 0x09, 0x19, 0x49, 0x59, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x11, 0x41, 0x51, 0x01, 0x11, 0x41, 0x51, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x09, 0x19, 0x49, 0x59, 0x09, 0x19, 0x49, 0x59, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static void nes_mapper_deinit(nes_t* nes) {
    nes_free(nes->nes_mapper.mapper_register);
    nes->nes_mapper.mapper_register = NULL;
}

static void mapper208_load_chr1k(nes_t* nes, mapper208_t* m, uint8_t slot, uint16_t page) {
    if (m->chr_bank_count > 0u) {
        nes_load_chrrom_1k(nes, slot, (uint16_t)(page % m->chr_bank_count));
    }
}

/* PRG is one 32KB block; CHR and mirroring follow the MMC3 registers. */
static void mapper208_update_banks(nes_t* nes) {
    mapper208_t* m = (mapper208_t*)nes->nes_mapper.mapper_register;
    const uint8_t chr_mode = (uint8_t)((m->bank_select >> 7) & 1u);

    nes_load_prgrom_32k(nes, 0, m->ex_regs[5]);

    if (m->chr_bank_count == 0u) return;

    if (chr_mode == 0u) {
        mapper208_load_chr1k(nes, m, 0, (uint16_t)(m->bank_values[0] & 0xFEu));
        mapper208_load_chr1k(nes, m, 1, (uint16_t)(m->bank_values[0] | 0x01u));
        mapper208_load_chr1k(nes, m, 2, (uint16_t)(m->bank_values[1] & 0xFEu));
        mapper208_load_chr1k(nes, m, 3, (uint16_t)(m->bank_values[1] | 0x01u));
        mapper208_load_chr1k(nes, m, 4, m->bank_values[2]);
        mapper208_load_chr1k(nes, m, 5, m->bank_values[3]);
        mapper208_load_chr1k(nes, m, 6, m->bank_values[4]);
        mapper208_load_chr1k(nes, m, 7, m->bank_values[5]);
    } else {
        mapper208_load_chr1k(nes, m, 0, m->bank_values[2]);
        mapper208_load_chr1k(nes, m, 1, m->bank_values[3]);
        mapper208_load_chr1k(nes, m, 2, m->bank_values[4]);
        mapper208_load_chr1k(nes, m, 3, m->bank_values[5]);
        mapper208_load_chr1k(nes, m, 4, (uint16_t)(m->bank_values[0] & 0xFEu));
        mapper208_load_chr1k(nes, m, 5, (uint16_t)(m->bank_values[0] | 0x01u));
        mapper208_load_chr1k(nes, m, 6, (uint16_t)(m->bank_values[1] & 0xFEu));
        mapper208_load_chr1k(nes, m, 7, (uint16_t)(m->bank_values[1] | 0x01u));
    }
}

static void mapper208_set_prg_block(nes_t* nes, uint8_t value) {
    mapper208_t* m = (mapper208_t*)nes->nes_mapper.mapper_register;
    m->ex_regs[5] = (uint8_t)((value & 0x01u) | ((value >> 3) & 0x02u));
    mapper208_update_banks(nes);
}

static void nes_mapper_init(nes_t* nes) {
    if (nes->nes_mapper.mapper_register == NULL) {
        nes->nes_mapper.mapper_register = nes_mapper_register_alloc(nes, (uint16_t)sizeof(mapper208_t));
        if (nes->nes_mapper.mapper_register == NULL) return;
    }
    mapper208_t* m = (mapper208_t*)nes->nes_mapper.mapper_register;
    nes_memset(m, 0, sizeof(mapper208_t));

    m->prg_bank_count = (uint16_t)(nes->nes_rom.prg_rom_size * 2u);
    m->chr_bank_count = (uint16_t)(nes->nes_rom.chr_rom_size * 8u);
    m->bank_values[6] = 0;
    m->bank_values[7] = 1;
    m->ex_regs[5] = 3;                       /* power-on 32KB block */

    if (nes->nes_rom.chr_rom_size == 0u) nes_load_chrrom_8k(nes, 0, 0);
    mapper208_update_banks(nes);
}

/* $8000-$FFFF: ordinary MMC3. */
static void nes_mapper_write(nes_t* nes, uint16_t address, uint8_t data) {
    mapper208_t* m = (mapper208_t*)nes->nes_mapper.mapper_register;
    switch (address & 0xE001u) {
    case 0x8000:
        m->bank_select = data;
        mapper208_update_banks(nes);
        break;
    case 0x8001: {
        const uint8_t reg = (uint8_t)(m->bank_select & 0x07u);
        m->bank_values[reg] = data;
        mapper208_update_banks(nes);
        break;
    }
    case 0xA000:
        m->mirroring = (uint8_t)(data & 1u);
        if (nes->nes_rom.four_screen == 0) {
            nes_ppu_screen_mirrors(nes, m->mirroring ? NES_MIRROR_HORIZONTAL : NES_MIRROR_VERTICAL);
        }
        break;
    case 0xC000: m->irq_latch = data; break;
    case 0xC001: m->irq_reload = 1; break;
    case 0xE000: m->irq_enabled = 0; nes->nes_cpu.irq_pending = 0; break;
    case 0xE001: m->irq_enabled = 1; break;
    default: break;
    }
}

/* $4020-$5FFF: the protection latch ($5000-$57FF seed, $5800-$5FFF scrambled write) and
 * the $4800-$4FFF 32KB PRG block register. */
static void nes_mapper_apu(nes_t* nes, uint16_t address, uint8_t data) {
    mapper208_t* m = (mapper208_t*)nes->nes_mapper.mapper_register;

    if (address >= 0x5000u && address <= 0x5FFFu) {
        if (address <= 0x57FFu) {
            m->ex_regs[4] = data;
        } else {
            m->ex_regs[address & 0x03u] = (uint8_t)(data ^ mapper208_protection_lut[m->ex_regs[4]]);
        }
        return;
    }
    if (address >= 0x4800u && address <= 0x4FFFu) {
        mapper208_set_prg_block(nes, data);
    }
}

static uint8_t nes_mapper_read_apu(nes_t* nes, uint16_t address) {
    mapper208_t* m = (mapper208_t*)nes->nes_mapper.mapper_register;
    if (address >= 0x5800u && address <= 0x5FFFu) {
        return m->ex_regs[address & 0x03u];
    }
    return 0;
}

/* $6000-$7FFF: $6800-$6FFF mirrors the PRG block register. */
static void nes_mapper_sram(nes_t* nes, uint16_t address, uint8_t data) {
    if (address >= 0x6800u && address <= 0x6FFFu) {
        mapper208_set_prg_block(nes, data);
    }
}

static void nes_mapper_hsync(nes_t* nes) {
    mapper208_t* m = (mapper208_t*)nes->nes_mapper.mapper_register;
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
    mapper208_update_banks(nes);
}

int nes_mapper208_init(nes_t* nes) {
    nes->nes_mapper.mapper_init          = nes_mapper_init;
    nes->nes_mapper.mapper_deinit        = nes_mapper_deinit;
    nes->nes_mapper.mapper_write         = nes_mapper_write;
    nes->nes_mapper.mapper_apu           = nes_mapper_apu;
    nes->nes_mapper.mapper_read_apu      = nes_mapper_read_apu;
    nes->nes_mapper.mapper_sram          = nes_mapper_sram;
    nes->nes_mapper.mapper_hsync         = nes_mapper_hsync;
    nes->nes_mapper.mapper_state_reapply = nes_mapper_state_reapply;
    return NES_OK;
}
