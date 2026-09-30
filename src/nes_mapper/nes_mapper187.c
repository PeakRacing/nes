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
 * Mapper 187 - Waixing MMC3 with outer bank latch and a security read port
 * (少年街霸2 / 拳王'96 等).
 * Authority: Mesen2 Core/NES/Mappers/Mmc3Variants/MMC3_187.h.
 *
 * MMC3 plus three extras:
 *
 *   $5000 / $6000 write : outer PRG register = value (bit 7 enables the override)
 *   $5000-$5FFF read    : { 0x83, 0x83, 0x42, 0x00 }[latch & 3] - a copy protection check
 *   $8000 write         : arms the latch (latch = 1) *and* is the normal MMC3 bank select
 *   $8001 write         : only reaches MMC3 while the latch is armed
 *
 * While the outer register's bit 7 is clear every MMC3 PRG page is masked to 6 bits
 * (64 x 8KB = 512KB).  With it set, bits 0-4 select an outer page and bit 5 chooses
 * between one 32KB block (bit 6 set) and a doubled 16KB pair.
 *
 * Every MMC3 CHR page also carries an extra bit 8, applied to whichever half the CHR
 * mode does *not* bank in 1KB units (=> up to 512KB of CHR).
 *
 * The previous implementation was "standard MMC3 with the security bypassed" - it had
 * neither the outer PRG register nor the extra CHR bit nor the protection read, so the
 * games that check all three stayed blank.
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
    uint8_t ex_regs[2];         /* [0] = $5000/$6000 outer register, [1] = $8000 latch */
    uint16_t prg_bank_count;    /* number of 8KB PRG banks */
    uint16_t chr_bank_count;    /* number of 1KB CHR banks */
} mapper187_t;

static void nes_mapper_deinit(nes_t* nes) {
    nes_free(nes->nes_mapper.mapper_register);
    nes->nes_mapper.mapper_register = NULL;
}

static void mapper187_load_prg8k(nes_t* nes, mapper187_t* m, uint8_t slot, uint16_t page) {
    if (m->prg_bank_count == 0u) return;
    nes_load_prgrom_8k(nes, slot, (uint16_t)(page % m->prg_bank_count));
}

/* The extra CHR bit goes to the half that the CHR mode does not bank in 1KB units. */
static void mapper187_load_chr1k(nes_t* nes, mapper187_t* m, uint8_t slot, uint16_t bank) {
    const uint8_t chr_mode = (uint8_t)((m->bank_select >> 7) & 1u);
    uint16_t page = bank;
    if ((chr_mode && slot >= 4u) || (!chr_mode && slot < 4u)) {
        page |= 0x100u;
    }
    if (m->chr_bank_count > 0u) {
        nes_load_chrrom_1k(nes, slot, (uint16_t)(page % m->chr_bank_count));
    }
}

static void mapper187_update_banks(nes_t* nes) {
    mapper187_t* m = (mapper187_t*)nes->nes_mapper.mapper_register;
    const uint8_t prg_mode = (uint8_t)((m->bank_select >> 6) & 1u);
    const uint8_t chr_mode = (uint8_t)((m->bank_select >> 7) & 1u);

    if (m->ex_regs[0] & 0x80u) {
        /* Outer register override. */
        uint16_t ex_page = (uint16_t)(m->ex_regs[0] & 0x1Fu);
        if (m->ex_regs[0] & 0x20u) {
            if (m->ex_regs[0] & 0x40u) {
                ex_page = (uint16_t)(ex_page & 0xFCu);
                for (uint8_t slot = 0; slot < 4u; slot++) {
                    mapper187_load_prg8k(nes, m, slot, (uint16_t)(ex_page + slot));
                }
            } else {
                ex_page = (uint16_t)(ex_page & 0xFEu);
                for (uint8_t slot = 0; slot < 4u; slot++) {
                    mapper187_load_prg8k(nes, m, slot, (uint16_t)((ex_page << 1) + slot));
                }
            }
        } else {
            const uint16_t half = (uint16_t)(ex_page << 1);
            mapper187_load_prg8k(nes, m, 0, half);
            mapper187_load_prg8k(nes, m, 1, (uint16_t)(half + 1u));
            mapper187_load_prg8k(nes, m, 2, half);
            mapper187_load_prg8k(nes, m, 3, (uint16_t)(half + 1u));
        }
    } else {
        /* Standard MMC3 layout, but a 6-bit page mask. */
        const uint16_t count = (m->prg_bank_count != 0u) ? m->prg_bank_count : 1u;
        const uint16_t last = (uint16_t)(count - 1u);
        const uint16_t slast = (uint16_t)((count >= 2u) ? (count - 2u) : 0u);
        if (prg_mode == 0u) {
            mapper187_load_prg8k(nes, m, 0, (uint16_t)(m->bank_values[6] & 0x3Fu));
            mapper187_load_prg8k(nes, m, 1, (uint16_t)(m->bank_values[7] & 0x3Fu));
            mapper187_load_prg8k(nes, m, 2, slast);
            mapper187_load_prg8k(nes, m, 3, last);
        } else {
            mapper187_load_prg8k(nes, m, 0, slast);
            mapper187_load_prg8k(nes, m, 1, (uint16_t)(m->bank_values[7] & 0x3Fu));
            mapper187_load_prg8k(nes, m, 2, (uint16_t)(m->bank_values[6] & 0x3Fu));
            mapper187_load_prg8k(nes, m, 3, last);
        }
    }

    if (chr_mode == 0u) {
        mapper187_load_chr1k(nes, m, 0, (uint16_t)(m->bank_values[0] & 0xFEu));
        mapper187_load_chr1k(nes, m, 1, (uint16_t)(m->bank_values[0] | 0x01u));
        mapper187_load_chr1k(nes, m, 2, (uint16_t)(m->bank_values[1] & 0xFEu));
        mapper187_load_chr1k(nes, m, 3, (uint16_t)(m->bank_values[1] | 0x01u));
        mapper187_load_chr1k(nes, m, 4, m->bank_values[2]);
        mapper187_load_chr1k(nes, m, 5, m->bank_values[3]);
        mapper187_load_chr1k(nes, m, 6, m->bank_values[4]);
        mapper187_load_chr1k(nes, m, 7, m->bank_values[5]);
    } else {
        mapper187_load_chr1k(nes, m, 0, m->bank_values[2]);
        mapper187_load_chr1k(nes, m, 1, m->bank_values[3]);
        mapper187_load_chr1k(nes, m, 2, m->bank_values[4]);
        mapper187_load_chr1k(nes, m, 3, m->bank_values[5]);
        mapper187_load_chr1k(nes, m, 4, (uint16_t)(m->bank_values[0] & 0xFEu));
        mapper187_load_chr1k(nes, m, 5, (uint16_t)(m->bank_values[0] | 0x01u));
        mapper187_load_chr1k(nes, m, 6, (uint16_t)(m->bank_values[1] & 0xFEu));
        mapper187_load_chr1k(nes, m, 7, (uint16_t)(m->bank_values[1] | 0x01u));
    }
}

static void nes_mapper_init(nes_t* nes) {
    if (nes->nes_mapper.mapper_register == NULL) {
        nes->nes_mapper.mapper_register = nes_mapper_register_alloc(nes, (uint16_t)sizeof(mapper187_t));
        if (nes->nes_mapper.mapper_register == NULL) return;
    }
    mapper187_t* m = (mapper187_t*)nes->nes_mapper.mapper_register;
    nes_memset(m, 0, sizeof(mapper187_t));

    m->prg_bank_count = (uint16_t)(nes->nes_rom.prg_rom_size * 2u);
    m->chr_bank_count = (uint16_t)(nes->nes_rom.chr_rom_size * 8u);

    m->bank_values[6] = 0;
    m->bank_values[7] = 1;

    mapper187_update_banks(nes);
}

static void nes_mapper_write(nes_t* nes, uint16_t address, uint8_t data) {
    mapper187_t* m = (mapper187_t*)nes->nes_mapper.mapper_register;

    if (address == 0x8000u) {
        m->ex_regs[1] = 1;                      /* arm the latch */
    } else if (address == 0x8001u) {
        if (m->ex_regs[1] != 1u) return;        /* bank data ignored until $8000 arms it */
    }

    switch (address & 0xE001u) {
    case 0x8000u:
        m->bank_select = data;
        mapper187_update_banks(nes);
        break;
    case 0x8001u: {
        const uint8_t reg = (uint8_t)(m->bank_select & 0x07u);
        m->bank_values[reg] = data;
        mapper187_update_banks(nes);
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

/* Only $5000 and $6000 exactly hold the outer PRG register - the 少年街霸2 probe shows the
 * game writing unrelated data to $6031/$60F0/$686F, which must not touch the mapping. */
static void mapper187_write_outer(nes_t* nes, uint16_t address, uint8_t data) {
    mapper187_t* m = (mapper187_t*)nes->nes_mapper.mapper_register;
    if (address != 0x5000u && address != 0x6000u) return;
    m->ex_regs[0] = data;
    mapper187_update_banks(nes);
}

static void nes_mapper_apu(nes_t* nes, uint16_t address, uint8_t data) {
    mapper187_write_outer(nes, address, data);
}

static void nes_mapper_sram(nes_t* nes, uint16_t address, uint8_t data) {
    mapper187_write_outer(nes, address, data);
}

/* $5000-$5FFF reads return the copy protection table indexed by the $8000 latch. */
static uint8_t nes_mapper_read_apu(nes_t* nes, uint16_t address) {
    static const uint8_t security[4] = { 0x83u, 0x83u, 0x42u, 0x00u };
    mapper187_t* m = (mapper187_t*)nes->nes_mapper.mapper_register;
    if (address >= 0x5000u && address <= 0x5FFFu) {
        return security[m->ex_regs[1] & 0x03u];
    }
    return 0;
}

static uint8_t nes_mapper_read_sram(nes_t* nes, uint16_t address) {
    return nes_mapper_read_apu(nes, address);
}

/* Scanline IRQ - identical to MMC3. */
static void nes_mapper_hsync(nes_t* nes) {
    mapper187_t* m = (mapper187_t*)nes->nes_mapper.mapper_register;
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

static void mapper187_state_reapply(nes_t* nes) {
    mapper187_update_banks(nes);
}

int nes_mapper187_init(nes_t* nes) {
    nes->nes_mapper.mapper_init   = nes_mapper_init;
    nes->nes_mapper.mapper_deinit = nes_mapper_deinit;
    nes->nes_mapper.mapper_write  = nes_mapper_write;
    nes->nes_mapper.mapper_hsync  = nes_mapper_hsync;
    nes->nes_mapper.mapper_apu    = nes_mapper_apu;
    nes->nes_mapper.mapper_sram   = nes_mapper_sram;
    nes->nes_mapper.mapper_read_apu  = nes_mapper_read_apu;
    nes->nes_mapper.mapper_read_sram = nes_mapper_read_sram;
    nes->nes_mapper.mapper_state_reapply = mapper187_state_reapply;
    return NES_OK;
}
