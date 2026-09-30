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
 * Mapper 115 - Waixing MMC3 with extra bank extension + protection latch
 * (包青天 / 雷电2 / 幽游白书 ...).
 * Authority: Mesen2 Core/NES/Mappers/Mmc3Variants/MMC3_115.h.
 *
 * Everything MMC3 does (R0-R7, the two bank modes, mirroring, the scanline IRQ) plus a
 * second register window living at $4100-$7FFF:
 *
 *   write $5080            : protection latch = value; reads at $5000-$5FFF return it
 *   write $4100-$7FFF, odd : CHR extension bit = value & 1
 *   write $4100-$7FFF, even: PRG extension register = value
 *
 * The PRG extension is only active while its bit 7 is set:
 *   bit 5 set : one 32KB block at $8000-$FFFF = (prgReg & 0x0F) >> 1 (32KB units)
 *   bit 5 clear: both 16KB halves show the same bank (prgReg & 0x0F)
 *
 * The CHR extension bit becomes bit 8 of *every* MMC3 CHR page (=> up to 256KB of CHR).
 *
 * The previous implementation was a different board entirely: it kept a private 2KB
 * CHR-RAM and redirected MMC3 CHR pages 66/67 into it.  Those pages are ordinary CHR
 * banks here, and the games stayed on the power-on mapping (blank / hung).
 */

typedef struct {
    uint8_t bank_select;
    uint8_t bank_values[8];     /* R0-R7 */
    uint8_t mirroring;
    uint8_t prg_ram_protect;
    uint8_t irq_latch;
    uint8_t irq_counter;
    uint8_t irq_reload;
    uint8_t irq_enabled;
    uint8_t prg_reg;            /* $4100-$7FFF even writes */
    uint8_t chr_reg;            /* $4100-$7FFF odd writes (bit 0 = CHR page bit 8) */
    uint8_t protection_reg;     /* $5080 write, read back from $5000-$5FFF */
    uint16_t prg_bank_count;    /* number of 8KB PRG banks */
    uint16_t chr_bank_count;    /* number of 1KB CHR banks */
} nes_mapper115_t;

static void nes_mapper_deinit(nes_t* nes) {
    nes_free(nes->nes_mapper.mapper_register);
    nes->nes_mapper.mapper_register = NULL;
}

/* Every CHR page carries the extension bit as its bit 8. */
static void mapper115_load_chr1k(nes_t* nes, nes_mapper115_t* m, uint8_t slot, uint8_t bank) {
    const uint16_t page = (uint16_t)((uint16_t)bank | ((uint16_t)m->chr_reg << 8));
    if (m->chr_bank_count > 0u) {
        nes_load_chrrom_1k(nes, slot, page % m->chr_bank_count);
    }
}

static void mapper115_update_banks(nes_t* nes) {
    nes_mapper115_t* m = (nes_mapper115_t*)nes->nes_mapper.mapper_register;
    const uint8_t prg_mode = (uint8_t)((m->bank_select >> 6) & 1u);
    const uint8_t chr_mode = (uint8_t)((m->bank_select >> 7) & 1u);
    const uint16_t prg_count = (m->prg_bank_count != 0u) ? m->prg_bank_count : 1u;
    const uint16_t last = (uint16_t)(prg_count - 1u);
    const uint16_t slast = (uint16_t)((prg_count >= 2u) ? (prg_count - 2u) : 0u);

    /* Standard MMC3 PRG banking. */
    if (prg_mode == 0u) {
        nes_load_prgrom_8k(nes, 0, (uint16_t)(m->bank_values[6] % prg_count));
        nes_load_prgrom_8k(nes, 1, (uint16_t)(m->bank_values[7] % prg_count));
        nes_load_prgrom_8k(nes, 2, slast);
        nes_load_prgrom_8k(nes, 3, last);
    } else {
        nes_load_prgrom_8k(nes, 0, slast);
        nes_load_prgrom_8k(nes, 1, (uint16_t)(m->bank_values[7] % prg_count));
        nes_load_prgrom_8k(nes, 2, (uint16_t)(m->bank_values[6] % prg_count));
        nes_load_prgrom_8k(nes, 3, last);
    }

    /* Waixing PRG extension: only while bit 7 is set. */
    if (m->prg_reg & 0x80u) {
        if (m->prg_reg & 0x20u) {
            /* One 32KB block: 8KB start page ((prgReg & 0x0F) >> 1) << 2 => 32KB unit (prgReg & 0x0F) >> 1. */
            nes_load_prgrom_32k(nes, 0, (uint16_t)((m->prg_reg & 0x0Fu) >> 1));
        } else {
            /* Both 16KB halves take 8KB page (prgReg & 0x0F) << 1 => 16KB unit (prgReg & 0x0F). */
            nes_load_prgrom_16k(nes, 0, (uint16_t)(m->prg_reg & 0x0Fu));
            nes_load_prgrom_16k(nes, 1, (uint16_t)(m->prg_reg & 0x0Fu));
        }
    }

    /* CHR banking (MMC3 layout) with the extension bit applied to every slot. */
    if (chr_mode == 0u) {
        mapper115_load_chr1k(nes, m, 0, (uint8_t)(m->bank_values[0] & 0xFEu));
        mapper115_load_chr1k(nes, m, 1, (uint8_t)(m->bank_values[0] | 0x01u));
        mapper115_load_chr1k(nes, m, 2, (uint8_t)(m->bank_values[1] & 0xFEu));
        mapper115_load_chr1k(nes, m, 3, (uint8_t)(m->bank_values[1] | 0x01u));
        mapper115_load_chr1k(nes, m, 4, m->bank_values[2]);
        mapper115_load_chr1k(nes, m, 5, m->bank_values[3]);
        mapper115_load_chr1k(nes, m, 6, m->bank_values[4]);
        mapper115_load_chr1k(nes, m, 7, m->bank_values[5]);
    } else {
        mapper115_load_chr1k(nes, m, 0, m->bank_values[2]);
        mapper115_load_chr1k(nes, m, 1, m->bank_values[3]);
        mapper115_load_chr1k(nes, m, 2, m->bank_values[4]);
        mapper115_load_chr1k(nes, m, 3, m->bank_values[5]);
        mapper115_load_chr1k(nes, m, 4, (uint8_t)(m->bank_values[0] & 0xFEu));
        mapper115_load_chr1k(nes, m, 5, (uint8_t)(m->bank_values[0] | 0x01u));
        mapper115_load_chr1k(nes, m, 6, (uint8_t)(m->bank_values[1] & 0xFEu));
        mapper115_load_chr1k(nes, m, 7, (uint8_t)(m->bank_values[1] | 0x01u));
    }
}

static void nes_mapper_init(nes_t* nes) {
    if (nes->nes_mapper.mapper_register == NULL) {
        nes->nes_mapper.mapper_register = nes_mapper_register_alloc(nes, (uint16_t)sizeof(nes_mapper115_t));
        if (nes->nes_mapper.mapper_register == NULL) return;
    }
    nes_mapper115_t* m = (nes_mapper115_t*)nes->nes_mapper.mapper_register;
    nes_memset(m, 0, sizeof(nes_mapper115_t));

    m->prg_bank_count = (uint16_t)(nes->nes_rom.prg_rom_size * 2u);
    m->chr_bank_count = (uint16_t)(nes->nes_rom.chr_rom_size * 8u);

    /* Power-on MMC3 registers: R6 = 0, R7 = 1 (the reset vector lives there). */
    m->bank_values[6] = 0;
    m->bank_values[7] = 1;

    mapper115_update_banks(nes);
}

/*
 * $8000-$FFFF: standard MMC3 register set.
 *   $8000 (even): bank select  $8001 (odd): bank data
 *   $A000 (even): mirroring    $A001 (odd): PRG RAM protect (ignored)
 *   $C000 (even): IRQ latch    $C001 (odd): IRQ reload
 *   $E000 (even): IRQ disable  $E001 (odd): IRQ enable
 */
static void nes_mapper_write(nes_t* nes, uint16_t address, uint8_t data) {
    nes_mapper115_t* m = (nes_mapper115_t*)nes->nes_mapper.mapper_register;
    switch (address & 0xE001u) {
    case 0x8000u:
        m->bank_select = data;
        mapper115_update_banks(nes);
        break;
    case 0x8001u: {
        const uint8_t reg = (uint8_t)(m->bank_select & 0x07u);
        m->bank_values[reg] = data;
        mapper115_update_banks(nes);
        break;
    }
    case 0xA000u:
        m->mirroring = (uint8_t)(data & 0x01u);
        if (nes->nes_rom.four_screen == 0) {
            nes_ppu_screen_mirrors(nes, m->mirroring ? NES_MIRROR_HORIZONTAL : NES_MIRROR_VERTICAL);
        }
        break;
    case 0xA001u:
        m->prg_ram_protect = data;
        break;
    case 0xC000u:
        m->irq_latch = data;
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

/* $4100-$7FFF (writes) - the Waixing extension window.  The core routes $4020-$5FFF
   writes to mapper_apu and $6000-$7FFF writes to mapper_sram, so both are installed. */
static void mapper115_write_extension(nes_t* nes, uint16_t address, uint8_t data) {
    nes_mapper115_t* m = (nes_mapper115_t*)nes->nes_mapper.mapper_register;
    if (address < 0x4100u) return;

    if (address == 0x5080u) {
        m->protection_reg = data;
        return;
    }
    if (address & 0x01u) {
        m->chr_reg = (uint8_t)(data & 0x01u);
    } else {
        m->prg_reg = data;
    }
    mapper115_update_banks(nes);
}

static void nes_mapper_apu(nes_t* nes, uint16_t address, uint8_t data) {
    mapper115_write_extension(nes, address, data);
}

static void nes_mapper_sram(nes_t* nes, uint16_t address, uint8_t data) {
    mapper115_write_extension(nes, address, data);
}

/* $5000-$5FFF reads hand back the protection latch. */
static uint8_t nes_mapper_read_apu(nes_t* nes, uint16_t address) {
    nes_mapper115_t* m = (nes_mapper115_t*)nes->nes_mapper.mapper_register;
    if (address >= 0x5000u && address <= 0x5FFFu) {
        return m->protection_reg;
    }
    return 0;
}

static uint8_t nes_mapper_read_sram(nes_t* nes, uint16_t address) {
    return nes_mapper_read_apu(nes, address);
}

/* Scanline IRQ - identical to MMC3. */
static void nes_mapper_hsync(nes_t* nes) {
    nes_mapper115_t* m = (nes_mapper115_t*)nes->nes_mapper.mapper_register;
    if (nes->nes_ppu.MASK_b == 0 && nes->nes_ppu.MASK_s == 0) return;

    if (m->irq_counter == 0 || m->irq_reload) {
        m->irq_counter = m->irq_latch;
    } else {
        m->irq_counter--;
    }

    if (m->irq_counter == 0 && m->irq_enabled) {
        nes_cpu_irq(nes);
    }

    m->irq_reload = 0;
}

static void mapper115_state_reapply(nes_t* nes) {
    mapper115_update_banks(nes);
}

int nes_mapper115_init(nes_t* nes) {
    nes->nes_mapper.mapper_init   = nes_mapper_init;
    nes->nes_mapper.mapper_deinit = nes_mapper_deinit;
    nes->nes_mapper.mapper_write  = nes_mapper_write;
    nes->nes_mapper.mapper_hsync  = nes_mapper_hsync;
    nes->nes_mapper.mapper_apu    = nes_mapper_apu;
    nes->nes_mapper.mapper_sram   = nes_mapper_sram;
    nes->nes_mapper.mapper_read_apu  = nes_mapper_read_apu;
    nes->nes_mapper.mapper_read_sram = nes_mapper_read_sram;
    nes->nes_mapper.mapper_state_reapply = mapper115_state_reapply;
    return NES_OK;
}
