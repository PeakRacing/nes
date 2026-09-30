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
 * Mapper 176 - Waixing FK23C ASIC (上海大亨 / 东周列国志 / 封神榜 / 宠物翡翠 ...).
 * Authority: Mesen2 Core/NES/Mappers/Waixing/Fk23C.h (mapper 176 is the only user).
 *
 * FK23C is "MMC3 plus a lot": the MMC3 register file grows to 12 entries and the board
 * adds its own bank-extension registers in the $5000-$5FFF window.
 *
 *   $5010-$501F (only when `(addr & 0x5010) == 0x5010`), selected by addr & 3:
 *     +0 : prgBankingMode = value & 7 ; outerChrBankSize = value bit 4 ;
 *          selectChrRam = value bit 5 ; mmc3ChrMode = bit 6 clear ;
 *          prgBaseBits bits 7/8 = value bits 7/3
 *     +1 : prgBaseBits bits 0-6 = value & 0x7F
 *     +2 : prgBaseBits bit 9 = value bit 6 ; chrBaseBits = value ; cnromChrReg = 0
 *     +3 : extendedMmc3Mode = value bit 1 ; cnromChrMode = value bits 2/6
 *   These are ignored while the board is in "WRAM config" mode, where $5000-$5FFF instead
 *   aliases the second 4KB half of WRAM bank (wramBankSelect + 1) & 3.
 *
 *   $8000-$FFFF (MMC3 shape, $8001 only reaches the file while currentRegister < 12):
 *     $8000 : invertPrgA14 = bit 6 ; invertChrA12 = bit 7 ; currentRegister = value & 0x0F
 *     $8001 : mmc3Regs[current & (extended ? 0x0F : 0x07)] = value
 *     $A000 : mirroringReg = value & 3
 *     $A001 : WRAM config - bank bits 0-1, bit 2 = CHR pages 0-7 read WRAM,
 *             bit 3 = allow single-screen mirroring, bit 5 = WRAM config mode,
 *             bit 6 = FK23C registers enabled + WRAM write protect, bit 7 = WRAM enabled
 *     $C000/$C001/$E000/$E001 : MMC3 scanline IRQ (fired 2 CPU clocks after the hit)
 *
 * PRG page size is 8KB and CHR page size is 1KB.  Power-on runs the MMC3 default register
 * file { 0, 2, 4, 5, 6, 7, 0, 1, $FE, $FF, $FF, $FF } with CHR in MMC3 mode; a 1MB PRG +
 * 1MB CHR image boots in the second 512KB (prgBaseBits = 0x20).
 *
 * The old implementation was a totally different board (a two-register "FC-026" multicart
 * at $5FF1/$5FF3 with 32KB/8KB pages), so the Waixing games never left their power-on banks.
 *
 * Not modelled: `selectChrRam` (a CHR-RAM buffer on top of CHR-ROM - Mesen gives such carts
 * up to 256KB of CHR-RAM).  Carts without CHR-ROM use the core's CHR-RAM normally.
 */

typedef struct {
    uint8_t  mmc3_regs[12];
    uint8_t  prg_banking_mode;
    uint8_t  outer_chr_bank_size;
    uint8_t  select_chr_ram;
    uint8_t  mmc3_chr_mode;
    uint8_t  cnrom_chr_mode;
    uint16_t prg_base_bits;
    uint8_t  chr_base_bits;
    uint8_t  extended_mmc3_mode;
    uint8_t  wram_bank_select;
    uint8_t  ram_in_first_chr_bank;
    uint8_t  allow_single_screen_mirroring;
    uint8_t  fk23_registers_enabled;
    uint8_t  wram_config_enabled;
    uint8_t  wram_enabled;
    uint8_t  wram_write_protected;
    uint8_t  invert_prg_a14;
    uint8_t  invert_chr_a12;
    uint8_t  current_register;
    uint8_t  irq_reload_value;
    uint8_t  irq_counter;
    uint8_t  irq_reload;
    uint8_t  irq_enabled;
    uint16_t irq_delay_cycles;      /* Mesen fires 2 CPU clocks after the counter hits 0 */
    uint8_t  mirroring_reg;
    uint8_t  cnrom_chr_reg;
    uint16_t prg_bank_count;        /* 8KB units */
    uint16_t chr_bank_count;        /* 1KB units */
    uint8_t* wram;                  /* 32KB board RAM ($6000-$7FFF, 4 x 8KB banks) */
} mapper176_t;

#define MAPPER176_WRAM_SIZE (0x8000u)

static void nes_mapper_deinit(nes_t* nes) {
    mapper176_t* m = (mapper176_t*)nes->nes_mapper.mapper_register;
    if (m != NULL && m->wram != NULL) {
        nes_free(m->wram);
        m->wram = NULL;
    }
    nes_free(nes->nes_mapper.mapper_register);
    nes->nes_mapper.mapper_register = NULL;
}

/* CHR page selection, including Mesen's "CHR pages 0-7 come from WRAM" case. */
static void mapper176_load_chr1k(nes_t* nes, mapper176_t* m, uint8_t slot, uint16_t page) {
    if (m->wram_config_enabled && m->ram_in_first_chr_bank && page <= 7u && m->wram != NULL) {
        nes->nes_ppu.pattern_table[slot] = m->wram + (uint32_t)page * 1024u;
        return;
    }
    if (m->chr_bank_count > 0u) {
        nes_load_chrrom_1k(nes, slot, (uint16_t)(page % m->chr_bank_count));
    }
}

static void mapper176_update_prg(nes_t* nes) {
    mapper176_t* m = (mapper176_t*)nes->nes_mapper.mapper_register;
    const uint8_t swap = (uint8_t)(m->invert_prg_a14 ? 2u : 0u);
    const uint16_t outer = (uint16_t)(m->prg_base_bits << 1);

    switch (m->prg_banking_mode) {
    case 0: case 1: case 2:
        if (m->extended_mmc3_mode) {
            nes_load_prgrom_8k(nes, (uint8_t)(0u ^ swap), (uint16_t)(m->mmc3_regs[6] | outer));
            nes_load_prgrom_8k(nes, 1u, (uint16_t)(m->mmc3_regs[7] | outer));
            nes_load_prgrom_8k(nes, (uint8_t)(2u ^ swap), (uint16_t)(m->mmc3_regs[8] | outer));
            nes_load_prgrom_8k(nes, 3u, (uint16_t)(m->mmc3_regs[9] | outer));
        } else {
            const uint16_t inner_mask = (uint16_t)(0x3Fu >> m->prg_banking_mode);
            const uint16_t masked_outer = (uint16_t)(outer & (uint16_t)(0xFFFFu ^ inner_mask));
            nes_load_prgrom_8k(nes, (uint8_t)(0u ^ swap), (uint16_t)((m->mmc3_regs[6] & inner_mask) | masked_outer));
            nes_load_prgrom_8k(nes, 1u, (uint16_t)((m->mmc3_regs[7] & inner_mask) | masked_outer));
            nes_load_prgrom_8k(nes, (uint8_t)(2u ^ swap), (uint16_t)((0xFEu & inner_mask) | masked_outer));
            nes_load_prgrom_8k(nes, 3u, (uint16_t)((0xFFu & inner_mask) | masked_outer));
        }
        break;

    case 3:
        /* SelectPrgPage2x(0, prgBaseBits << 1) and the same page again in both halves. */
        nes_load_prgrom_16k(nes, 0, m->prg_base_bits);
        nes_load_prgrom_16k(nes, 1, m->prg_base_bits);
        break;

    case 4: {
        /* SelectPrgPage4x(0, (prgBaseBits & 0xFFE) << 1) -> one 32KB block. */
        const uint16_t page32 = (uint16_t)((m->prg_base_bits & 0xFFEu) >> 1);
        nes_load_prgrom_32k(nes, 0, page32);
        break;
    }

    default:
        break;
    }
}

static void mapper176_update_chr(nes_t* nes) {
    mapper176_t* m = (mapper176_t*)nes->nes_mapper.mapper_register;

    if (m->mmc3_chr_mode == 0u) {
        const uint16_t inner_mask = (uint16_t)(m->cnrom_chr_mode ? (m->outer_chr_bank_size ? 1u : 3u) : 0u);
        const uint16_t base = (uint16_t)(((m->cnrom_chr_reg & inner_mask) | m->chr_base_bits) << 3);
        for (uint8_t i = 0; i < 8u; i++) {
            mapper176_load_chr1k(nes, m, i, (uint16_t)(base + i));
        }
        return;
    }

    {
        const uint8_t swap = (uint8_t)(m->invert_chr_a12 ? 4u : 0u);
        if (m->extended_mmc3_mode) {
            const uint16_t outer = (uint16_t)(m->chr_base_bits << 3);
            mapper176_load_chr1k(nes, m, (uint8_t)(0u ^ swap), (uint16_t)(m->mmc3_regs[0] | outer));
            mapper176_load_chr1k(nes, m, (uint8_t)(1u ^ swap), (uint16_t)(m->mmc3_regs[10] | outer));
            mapper176_load_chr1k(nes, m, (uint8_t)(2u ^ swap), (uint16_t)(m->mmc3_regs[1] | outer));
            mapper176_load_chr1k(nes, m, (uint8_t)(3u ^ swap), (uint16_t)(m->mmc3_regs[11] | outer));
            mapper176_load_chr1k(nes, m, (uint8_t)(4u ^ swap), (uint16_t)(m->mmc3_regs[2] | outer));
            mapper176_load_chr1k(nes, m, (uint8_t)(5u ^ swap), (uint16_t)(m->mmc3_regs[3] | outer));
            mapper176_load_chr1k(nes, m, (uint8_t)(6u ^ swap), (uint16_t)(m->mmc3_regs[4] | outer));
            mapper176_load_chr1k(nes, m, (uint8_t)(7u ^ swap), (uint16_t)(m->mmc3_regs[5] | outer));
        } else {
            const uint16_t inner_mask = (uint16_t)(m->outer_chr_bank_size ? 0x7Fu : 0xFFu);
            const uint16_t outer = (uint16_t)((m->chr_base_bits << 3) & (uint16_t)(0xFFFFu ^ inner_mask));
            mapper176_load_chr1k(nes, m, (uint8_t)(0u ^ swap), (uint16_t)(((m->mmc3_regs[0] & 0xFEu) & inner_mask) | outer));
            mapper176_load_chr1k(nes, m, (uint8_t)(1u ^ swap), (uint16_t)(((m->mmc3_regs[0] | 0x01u) & inner_mask) | outer));
            mapper176_load_chr1k(nes, m, (uint8_t)(2u ^ swap), (uint16_t)(((m->mmc3_regs[1] & 0xFEu) & inner_mask) | outer));
            mapper176_load_chr1k(nes, m, (uint8_t)(3u ^ swap), (uint16_t)(((m->mmc3_regs[1] | 0x01u) & inner_mask) | outer));
            mapper176_load_chr1k(nes, m, (uint8_t)(4u ^ swap), (uint16_t)((m->mmc3_regs[2] & inner_mask) | outer));
            mapper176_load_chr1k(nes, m, (uint8_t)(5u ^ swap), (uint16_t)((m->mmc3_regs[3] & inner_mask) | outer));
            mapper176_load_chr1k(nes, m, (uint8_t)(6u ^ swap), (uint16_t)((m->mmc3_regs[4] & inner_mask) | outer));
            mapper176_load_chr1k(nes, m, (uint8_t)(7u ^ swap), (uint16_t)((m->mmc3_regs[5] & inner_mask) | outer));
        }
    }
}

static void mapper176_update_mirroring(nes_t* nes) {
    mapper176_t* m = (mapper176_t*)nes->nes_mapper.mapper_register;
    if (nes->nes_rom.four_screen) return;
    switch (m->mirroring_reg & (uint8_t)(m->allow_single_screen_mirroring ? 0x03u : 0x01u)) {
    case 0: nes_ppu_screen_mirrors(nes, NES_MIRROR_VERTICAL);     break;
    case 1: nes_ppu_screen_mirrors(nes, NES_MIRROR_HORIZONTAL);   break;
    case 2: nes_ppu_screen_mirrors(nes, NES_MIRROR_ONE_SCREEN0);  break;
    default: nes_ppu_screen_mirrors(nes, NES_MIRROR_ONE_SCREEN1); break;
    }
}

static void mapper176_update_state(nes_t* nes) {
    mapper176_update_mirroring(nes);
    mapper176_update_prg(nes);
    mapper176_update_chr(nes);
}

static void nes_mapper_init(nes_t* nes) {
    static const uint8_t init_values[12] = { 0, 2, 4, 5, 6, 7, 0, 1, 0xFEu, 0xFFu, 0xFFu, 0xFFu };
    if (nes->nes_mapper.mapper_register == NULL) {
        nes->nes_mapper.mapper_register = nes_mapper_register_alloc(nes, (uint16_t)sizeof(mapper176_t));
        if (nes->nes_mapper.mapper_register == NULL) return;
    }
    mapper176_t* m = (mapper176_t*)nes->nes_mapper.mapper_register;
    nes_memset(m, 0, sizeof(mapper176_t));

    for (uint8_t i = 0; i < 12u; i++) {
        m->mmc3_regs[i] = init_values[i];
    }
    m->mmc3_chr_mode = 1u;

    /* 1MB PRG + 1MB CHR images boot in the second 512KB. */
    if (nes->nes_rom.prg_rom_size == 64u && nes->nes_rom.chr_rom_size == 128u) {
        m->prg_base_bits = 0x20u;
    }

    m->prg_bank_count = (uint16_t)(nes->nes_rom.prg_rom_size * 2u);
    m->chr_bank_count = (uint16_t)(nes->nes_rom.chr_rom_size * 8u);

    m->wram = (uint8_t*)nes_malloc(MAPPER176_WRAM_SIZE);
    if (m->wram != NULL) {
        nes_memset(m->wram, 0, MAPPER176_WRAM_SIZE);
    }

    if (nes->nes_rom.chr_rom_size == 0u) nes_load_chrrom_8k(nes, 0, 0);
    mapper176_update_state(nes);
}

/* $5000-$5FFF: FK23C registers, or the upper WRAM half while the WRAM config is active. */
static void nes_mapper_apu(nes_t* nes, uint16_t address, uint8_t data) {
    mapper176_t* m = (mapper176_t*)nes->nes_mapper.mapper_register;

    if (m->wram_config_enabled && !m->fk23_registers_enabled) {
        if (m->wram != NULL) {
            const uint8_t bank = (uint8_t)((m->wram_bank_select + 1u) & 0x03u);
            m->wram[(uint32_t)bank * 8192u + (address & 0x1FFFu)] = data;
        }
        return;
    }

    if ((address & 0x5010u) != 0x5010u) return;

    switch (address & 0x03u) {
    case 0:
        m->prg_banking_mode = (uint8_t)(data & 0x07u);
        m->outer_chr_bank_size = (uint8_t)((data & 0x10u) >> 4);
        m->select_chr_ram = (uint8_t)((data & 0x20u) != 0u);
        m->mmc3_chr_mode = (uint8_t)((data & 0x40u) == 0u);
        m->prg_base_bits = (uint16_t)((m->prg_base_bits & 0xFE7Fu)
                                      | ((uint16_t)(data & 0x80u) << 1)
                                      | ((uint16_t)(data & 0x08u) << 4));
        break;
    case 1:
        m->prg_base_bits = (uint16_t)((m->prg_base_bits & 0xFF80u) | (data & 0x7Fu));
        break;
    case 2:
        m->prg_base_bits = (uint16_t)((m->prg_base_bits & 0xFDFFu) | ((uint16_t)(data & 0x40u) << 3));
        m->chr_base_bits = data;
        m->cnrom_chr_reg = 0;
        break;
    default:
        m->extended_mmc3_mode = (uint8_t)((data & 0x02u) != 0u);
        m->cnrom_chr_mode = (uint8_t)((data & 0x44u) != 0u);
        break;
    }
    mapper176_update_state(nes);
}

static uint8_t nes_mapper_read_apu(nes_t* nes, uint16_t address) {
    mapper176_t* m = (mapper176_t*)nes->nes_mapper.mapper_register;
    if (m->wram_config_enabled) {
        if (m->wram != NULL) {
            const uint8_t bank = (uint8_t)((m->wram_bank_select + 1u) & 0x03u);
            return m->wram[(uint32_t)bank * 8192u + (address & 0x1FFFu)];
        }
    }
    return 0;
}

/* $6000-$7FFF: WRAM bank wramBankSelect (or bank 0 when the WRAM config is off). */
static uint8_t mapper176_wram_bank(mapper176_t* m) {
    return (uint8_t)(m->wram_config_enabled ? (m->wram_bank_select & 0x03u) : 0u);
}

static uint8_t nes_mapper_read_sram(nes_t* nes, uint16_t address) {
    mapper176_t* m = (mapper176_t*)nes->nes_mapper.mapper_register;
    if (!m->wram_enabled || m->wram == NULL) return 0;
    return m->wram[(uint32_t)mapper176_wram_bank(m) * 8192u + (address & 0x1FFFu)];
}

static void nes_mapper_sram(nes_t* nes, uint16_t address, uint8_t data) {
    mapper176_t* m = (mapper176_t*)nes->nes_mapper.mapper_register;
    if (!m->wram_enabled || m->wram_write_protected || m->wram == NULL) return;
    m->wram[(uint32_t)mapper176_wram_bank(m) * 8192u + (address & 0x1FFFu)] = data;
}

static void nes_mapper_write(nes_t* nes, uint16_t address, uint8_t data) {
    mapper176_t* m = (mapper176_t*)nes->nes_mapper.mapper_register;

    /* CNROM-style CHR latch: every write outside $A000-$BFFF while cnrom mode is on. */
    if (m->cnrom_chr_mode && (address <= 0x9FFFu || address >= 0xC000u)) {
        m->cnrom_chr_reg = (uint8_t)(data & 0x03u);
        mapper176_update_chr(nes);
    }

    switch (address & 0xE001u) {
    case 0x8000u:
        if (nes->nes_rom.prg_rom_size == 1024u && (data == 0x46u || data == 0x47u)) {
            data ^= 0x01u;      /* 16MB subtype: $46/$47 swapped */
        }
        m->invert_prg_a14 = (uint8_t)((data & 0x40u) != 0u);
        m->invert_chr_a12 = (uint8_t)((data & 0x80u) != 0u);
        m->current_register = (uint8_t)(data & 0x0Fu);
        mapper176_update_state(nes);
        break;

    case 0x8001u: {
        const uint8_t reg = (uint8_t)(m->current_register & (m->extended_mmc3_mode ? 0x0Fu : 0x07u));
        if (reg < 12u) {
            m->mmc3_regs[reg] = data;
            mapper176_update_state(nes);
        }
        break;
    }

    case 0xA000u:
        m->mirroring_reg = (uint8_t)(data & 0x03u);
        mapper176_update_state(nes);
        break;

    case 0xA001u: {
        uint8_t value = data;
        if ((value & 0x20u) == 0u) {
            value = (uint8_t)(value & 0xC0u);   /* extra bits need bit 5 */
        }
        m->wram_bank_select = (uint8_t)(value & 0x03u);
        m->ram_in_first_chr_bank = (uint8_t)((value & 0x04u) != 0u);
        m->allow_single_screen_mirroring = (uint8_t)((value & 0x08u) != 0u);
        m->wram_config_enabled = (uint8_t)((value & 0x20u) != 0u);
        m->fk23_registers_enabled = (uint8_t)((value & 0x40u) != 0u);
        m->wram_write_protected = (uint8_t)((value & 0x40u) != 0u);
        m->wram_enabled = (uint8_t)((value & 0x80u) != 0u);
        mapper176_update_state(nes);
        break;
    }

    case 0xC000u:
        m->irq_reload_value = data;
        break;
    case 0xC001u:
        m->irq_counter = 0;
        m->irq_reload = 1;
        break;
    case 0xE000u:
        m->irq_enabled = 0;
        nes->nes_cpu.irq_pending = 0;
        break;
    case 0xE001u:
        m->irq_enabled = 1;
        break;
    default:
        break;
    }
}

/* Scanline hook stands in for Mesen's A12 watcher; the IRQ itself is delayed 2 CPU clocks. */
static void nes_mapper_hsync(nes_t* nes) {
    mapper176_t* m = (mapper176_t*)nes->nes_mapper.mapper_register;
    if (nes->nes_ppu.MASK_b == 0 && nes->nes_ppu.MASK_s == 0) return;

    if (m->irq_counter == 0u || m->irq_reload) {
        m->irq_counter = m->irq_reload_value;
    } else {
        m->irq_counter--;
    }

    if (m->irq_counter == 0u && m->irq_enabled) {
        m->irq_delay_cycles = 2;
    }
    m->irq_reload = 0;
}

static void nes_mapper_cpu_clock(nes_t* nes, uint16_t cycles) {
    mapper176_t* m = (mapper176_t*)nes->nes_mapper.mapper_register;
    if (m->irq_delay_cycles == 0u) return;
    if (m->irq_delay_cycles > cycles) {
        m->irq_delay_cycles = (uint16_t)(m->irq_delay_cycles - cycles);
        return;
    }
    m->irq_delay_cycles = 0;
    nes_cpu_irq(nes);
}

static void nes_mapper_state_reapply(nes_t* nes) {
    mapper176_update_state(nes);
}

int nes_mapper176_init(nes_t* nes) {
    nes->nes_mapper.mapper_init          = nes_mapper_init;
    nes->nes_mapper.mapper_deinit        = nes_mapper_deinit;
    nes->nes_mapper.mapper_write         = nes_mapper_write;
    nes->nes_mapper.mapper_apu           = nes_mapper_apu;
    nes->nes_mapper.mapper_read_apu      = nes_mapper_read_apu;
    nes->nes_mapper.mapper_sram          = nes_mapper_sram;
    nes->nes_mapper.mapper_read_sram     = nes_mapper_read_sram;
    nes->nes_mapper.mapper_hsync         = nes_mapper_hsync;
    nes->nes_mapper.mapper_cpu_clock     = nes_mapper_cpu_clock;
    nes->nes_mapper.mapper_state_reapply = nes_mapper_state_reapply;
    return NES_OK;
}
