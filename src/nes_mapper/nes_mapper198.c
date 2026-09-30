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
 * Mapper 198 - MMC3 variant (吞食天地2 / Cheng Ji Si Han).
 * Authority: Mesen2 Core/NES/Mappers/Mmc3Variants/MMC3_198.h.
 *
 * Mesen's own comment calls this board "most likely incorrect/incomplete, but works with the
 * two games marked as mapper 198", so this port follows it exactly instead of guessing:
 *
 *   - The four PRG slots ignore the MMC3 mapping and come from exRegs[] instead:
 *       power-on exRegs[] = { 0, 1, last-1, last }
 *       a write to MMC3 register 6/7 lands in exRegs[0]/exRegs[1], masked with 0x3F, or with
 *       0x4F when the value is >= 0x40
 *     (so the MMC3 PRG mode bit has no visible effect on these slots)
 *   - 4KB of work RAM is mirrored over $5000-$7FFF (Mesen's ForceWorkRamSize).
 *   - When all six MMC3 CHR registers are zero, the whole 8KB CHR-RAM is selected as a single
 *     page; otherwise the ordinary MMC3 CHR mapping applies.
 *
 * The old implementation was plain MMC3 with a CHR-RAM window, so the game's bank writes
 * through registers 6/7 landed in slots this board does not map that way.
 */

#define MAPPER198_WRAM_SIZE (0x1000u)

typedef struct {
    uint8_t bank_select;
    uint8_t bank_values[8];
    uint8_t ex_regs[4];
    uint8_t mirroring;
    uint8_t irq_latch;
    uint8_t irq_counter;
    uint8_t irq_reload;
    uint8_t irq_enabled;
    uint16_t prg_bank_count;    /* 8KB units */
    uint16_t chr_bank_count;    /* 1KB units */
    uint8_t* wram;              /* 4KB, mirrored over $5000-$7FFF */
} mapper198_t;

static void nes_mapper_deinit(nes_t* nes) {
    mapper198_t* m = (mapper198_t*)nes->nes_mapper.mapper_register;
    if (m != NULL && m->wram != NULL) {
        nes_free(m->wram);
        m->wram = NULL;
    }
    nes_free(nes->nes_mapper.mapper_register);
    nes->nes_mapper.mapper_register = NULL;
}

static void mapper198_update_banks(nes_t* nes) {
    mapper198_t* m = (mapper198_t*)nes->nes_mapper.mapper_register;
    const uint8_t chr_mode = (uint8_t)((m->bank_select >> 7) & 1u);

    /* PRG: the four slots are exRegs[0..3] whichever mode the MMC3 register file asks for. */
    nes_load_prgrom_8k(nes, 0, m->ex_regs[0]);
    nes_load_prgrom_8k(nes, 1, m->ex_regs[1]);
    nes_load_prgrom_8k(nes, 2, m->ex_regs[2]);
    nes_load_prgrom_8k(nes, 3, m->ex_regs[3]);

    if (m->chr_bank_count == 0u) return;

    /* All-zero CHR registers mean "the whole 8KB CHR-RAM as one page". */
    if (m->bank_values[0] == 0u && m->bank_values[1] == 0u && m->bank_values[2] == 0u &&
        m->bank_values[3] == 0u && m->bank_values[4] == 0u && m->bank_values[5] == 0u) {
        nes_load_chrrom_8k(nes, 0, 0);
        return;
    }

    if (chr_mode == 0u) {
        nes_load_chrrom_1k(nes, 0, (uint16_t)((m->bank_values[0] & 0xFEu) % m->chr_bank_count));
        nes_load_chrrom_1k(nes, 1, (uint16_t)((m->bank_values[0] | 0x01u) % m->chr_bank_count));
        nes_load_chrrom_1k(nes, 2, (uint16_t)((m->bank_values[1] & 0xFEu) % m->chr_bank_count));
        nes_load_chrrom_1k(nes, 3, (uint16_t)((m->bank_values[1] | 0x01u) % m->chr_bank_count));
        nes_load_chrrom_1k(nes, 4, (uint16_t)(m->bank_values[2] % m->chr_bank_count));
        nes_load_chrrom_1k(nes, 5, (uint16_t)(m->bank_values[3] % m->chr_bank_count));
        nes_load_chrrom_1k(nes, 6, (uint16_t)(m->bank_values[4] % m->chr_bank_count));
        nes_load_chrrom_1k(nes, 7, (uint16_t)(m->bank_values[5] % m->chr_bank_count));
    } else {
        nes_load_chrrom_1k(nes, 0, (uint16_t)(m->bank_values[2] % m->chr_bank_count));
        nes_load_chrrom_1k(nes, 1, (uint16_t)(m->bank_values[3] % m->chr_bank_count));
        nes_load_chrrom_1k(nes, 2, (uint16_t)(m->bank_values[4] % m->chr_bank_count));
        nes_load_chrrom_1k(nes, 3, (uint16_t)(m->bank_values[5] % m->chr_bank_count));
        nes_load_chrrom_1k(nes, 4, (uint16_t)((m->bank_values[0] & 0xFEu) % m->chr_bank_count));
        nes_load_chrrom_1k(nes, 5, (uint16_t)((m->bank_values[0] | 0x01u) % m->chr_bank_count));
        nes_load_chrrom_1k(nes, 6, (uint16_t)((m->bank_values[1] & 0xFEu) % m->chr_bank_count));
        nes_load_chrrom_1k(nes, 7, (uint16_t)((m->bank_values[1] | 0x01u) % m->chr_bank_count));
    }
}

static void nes_mapper_init(nes_t* nes) {
    if (nes->nes_mapper.mapper_register == NULL) {
        nes->nes_mapper.mapper_register = nes_mapper_register_alloc(nes, (uint16_t)sizeof(mapper198_t));
        if (nes->nes_mapper.mapper_register == NULL) return;
    }
    mapper198_t* m = (mapper198_t*)nes->nes_mapper.mapper_register;
    nes_memset(m, 0, sizeof(mapper198_t));

    m->prg_bank_count = (uint16_t)(nes->nes_rom.prg_rom_size * 2u);
    m->chr_bank_count = (uint16_t)(nes->nes_rom.chr_rom_size * 8u);
    m->bank_values[6] = 0;
    m->bank_values[7] = 1;
    m->ex_regs[0] = 0;
    m->ex_regs[1] = 1;
    m->ex_regs[2] = (uint8_t)((m->prg_bank_count >= 2u) ? (m->prg_bank_count - 2u) : 0u);
    m->ex_regs[3] = (uint8_t)((m->prg_bank_count >= 1u) ? (m->prg_bank_count - 1u) : 0u);

    m->wram = (uint8_t*)nes_malloc(MAPPER198_WRAM_SIZE);
    if (m->wram != NULL) {
        nes_memset(m->wram, 0, MAPPER198_WRAM_SIZE);
    }

    if (nes->nes_rom.chr_rom_size == 0u) nes_load_chrrom_8k(nes, 0, 0);
    mapper198_update_banks(nes);
}

static void nes_mapper_write(nes_t* nes, uint16_t address, uint8_t data) {
    mapper198_t* m = (mapper198_t*)nes->nes_mapper.mapper_register;
    switch (address & 0xE001u) {
    case 0x8000:
        m->bank_select = data;
        mapper198_update_banks(nes);
        break;
    case 0x8001: {
        const uint8_t reg = (uint8_t)(m->bank_select & 0x07u);
        if (reg >= 6u) {
            m->ex_regs[reg - 6u] = (uint8_t)(data & ((data >= 0x40u) ? 0x4Fu : 0x3Fu));
        } else {
            m->bank_values[reg] = data;
        }
        mapper198_update_banks(nes);
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

/* The 4KB work RAM answers over $5000-$7FFF, mirrored every 4KB. */
static uint8_t mapper198_wram_read(nes_t* nes, uint16_t address) {
    mapper198_t* m = (mapper198_t*)nes->nes_mapper.mapper_register;
    if (m->wram == NULL) return 0;
    return m->wram[address & 0x0FFFu];
}

static void mapper198_wram_write(nes_t* nes, uint16_t address, uint8_t data) {
    mapper198_t* m = (mapper198_t*)nes->nes_mapper.mapper_register;
    if (m->wram == NULL) return;
    m->wram[address & 0x0FFFu] = data;
}

static void nes_mapper_apu(nes_t* nes, uint16_t address, uint8_t data) {
    if (address >= 0x5000u && address <= 0x5FFFu) {
        mapper198_wram_write(nes, address, data);
    }
}

static uint8_t nes_mapper_read_apu(nes_t* nes, uint16_t address) {
    if (address >= 0x5000u && address <= 0x5FFFu) {
        return mapper198_wram_read(nes, address);
    }
    return 0;
}

static void nes_mapper_sram(nes_t* nes, uint16_t address, uint8_t data) {
    mapper198_wram_write(nes, address, data);
}

static uint8_t nes_mapper_read_sram(nes_t* nes, uint16_t address) {
    return mapper198_wram_read(nes, address);
}

static void nes_mapper_hsync(nes_t* nes) {
    mapper198_t* m = (mapper198_t*)nes->nes_mapper.mapper_register;
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
    mapper198_update_banks(nes);
}

int nes_mapper198_init(nes_t* nes) {
    nes->nes_mapper.mapper_init          = nes_mapper_init;
    nes->nes_mapper.mapper_deinit        = nes_mapper_deinit;
    nes->nes_mapper.mapper_write         = nes_mapper_write;
    nes->nes_mapper.mapper_apu           = nes_mapper_apu;
    nes->nes_mapper.mapper_read_apu      = nes_mapper_read_apu;
    nes->nes_mapper.mapper_sram          = nes_mapper_sram;
    nes->nes_mapper.mapper_read_sram     = nes_mapper_read_sram;
    nes->nes_mapper.mapper_hsync         = nes_mapper_hsync;
    nes->nes_mapper.mapper_state_reapply = nes_mapper_state_reapply;
    return NES_OK;
}
