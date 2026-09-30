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
 * Mapper 245 - Waixing MMC3 with a 512KB PRG block latch (勇者斗恶龙6/7 等).
 * Authority: Mesen2 Core/NES/Mappers/Mmc3Variants/MMC3_245.h.
 *
 * MMC3 plus one extra bit of PRG banking and a fixed CHR layout for CHR-RAM carts:
 *
 *   PRG : orValue = (R0 & 0x02) ? 0x40 : 0x00
 *         R6/R7 are masked with 0x3F and OR'd with orValue, and the fixed slots take the
 *         LAST PAGE OF THE SELECTED 64-PAGE BLOCK (0x3F | orValue) - not the last page of
 *         the image - whenever the ROM has at least 64 8KB pages.
 *           mode 0: slot0 = R6, slot1 = R7, slot2 = last-1, slot3 = last
 *           mode 1: slot0 = last-1, slot1 = R7, slot2 = R6, slot3 = last
 *   CHR : boards without CHR-ROM (勇者斗恶龙6/7 are CHR-RAM carts) get the two 4KB halves
 *         forced to the 1KB pages 0-3 / 4-7 according to the MMC3 CHR mode - the MMC3 CHR
 *         registers do not apply at all.
 *
 * The previous implementation invented a private 2KB CHR-RAM for MMC3 bank values 0/1
 * (the same wrong model that mapper 115 had), and it ignored the 0x40 PRG block bit, so
 * multi-block images came up with the wrong banks.
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
    uint16_t prg_bank_count;    /* number of 8KB PRG banks */
    uint16_t chr_bank_count;    /* number of 1KB CHR banks */
} mapper245_t;

static void nes_mapper_deinit(nes_t* nes) {
    nes_free(nes->nes_mapper.mapper_register);
    nes->nes_mapper.mapper_register = NULL;
}

static void mapper245_load_prg8k(nes_t* nes, uint16_t count, uint8_t slot, uint16_t page) {
    if (count == 0u) return;
    nes_load_prgrom_8k(nes, slot, (uint16_t)(page % count));
}

static void mapper245_load_chr1k(nes_t* nes, mapper245_t* m, uint8_t slot, uint16_t bank) {
    if (m->chr_bank_count > 0u) {
        nes_load_chrrom_1k(nes, slot, (uint16_t)(bank % m->chr_bank_count));
    }
}

static void mapper245_update_banks(nes_t* nes) {
    mapper245_t* m = (mapper245_t*)nes->nes_mapper.mapper_register;
    const uint8_t prg_mode = (uint8_t)((m->bank_select >> 6) & 1u);
    const uint8_t chr_mode = (uint8_t)((m->bank_select >> 7) & 1u);
    const uint8_t or_value = (uint8_t)((m->bank_values[0] & 0x02u) ? 0x40u : 0x00u);
    const uint16_t r6 = (uint16_t)((m->bank_values[6] & 0x3Fu) | or_value);
    const uint16_t r7 = (uint16_t)((m->bank_values[7] & 0x3Fu) | or_value);
    const uint16_t count = (m->prg_bank_count != 0u) ? m->prg_bank_count : 1u;
    const uint16_t last_block_page = (count >= 0x40u) ? (uint16_t)(0x3Fu | or_value)
                                                       : (uint16_t)(count - 1u);

    if (prg_mode == 0u) {
        mapper245_load_prg8k(nes, count, 0, r6);
        mapper245_load_prg8k(nes, count, 1, r7);
        mapper245_load_prg8k(nes, count, 2, (uint16_t)(last_block_page - 1u));
        mapper245_load_prg8k(nes, count, 3, last_block_page);
    } else {
        mapper245_load_prg8k(nes, count, 0, (uint16_t)(last_block_page - 1u));
        mapper245_load_prg8k(nes, count, 1, r7);
        mapper245_load_prg8k(nes, count, 2, r6);
        mapper245_load_prg8k(nes, count, 3, last_block_page);
    }

    if (nes->nes_rom.chr_rom_size == 0u) {
        /* CHR-RAM board: the two 4KB halves are fixed and the MMC3 CHR registers are unused.
         * The whole 8KB CHR-RAM is resident, so the halves are swapped by pointing the
         * pattern table straight into it (nes_load_chrrom_4k ignores the page for CHR-RAM). */
        uint8_t* const ram = nes->nes_rom.chr_rom;
        uint8_t i;
        if (ram != NULL) {
            for (i = 0; i < 4u; i++) {
                if (chr_mode != 0u) {
                    nes->nes_ppu.pattern_table[i] = ram + 4096u + (uint32_t)i * 1024u;
                    nes->nes_ppu.pattern_table[i + 4u] = ram + (uint32_t)i * 1024u;
                } else {
                    nes->nes_ppu.pattern_table[i] = ram + (uint32_t)i * 1024u;
                    nes->nes_ppu.pattern_table[i + 4u] = ram + 4096u + (uint32_t)i * 1024u;
                }
            }
        }
        return;
    }

    if (chr_mode == 0u) {
        mapper245_load_chr1k(nes, m, 0, (uint16_t)(m->bank_values[0] & 0xFEu));
        mapper245_load_chr1k(nes, m, 1, (uint16_t)(m->bank_values[0] | 0x01u));
        mapper245_load_chr1k(nes, m, 2, (uint16_t)(m->bank_values[1] & 0xFEu));
        mapper245_load_chr1k(nes, m, 3, (uint16_t)(m->bank_values[1] | 0x01u));
        mapper245_load_chr1k(nes, m, 4, m->bank_values[2]);
        mapper245_load_chr1k(nes, m, 5, m->bank_values[3]);
        mapper245_load_chr1k(nes, m, 6, m->bank_values[4]);
        mapper245_load_chr1k(nes, m, 7, m->bank_values[5]);
    } else {
        mapper245_load_chr1k(nes, m, 0, m->bank_values[2]);
        mapper245_load_chr1k(nes, m, 1, m->bank_values[3]);
        mapper245_load_chr1k(nes, m, 2, m->bank_values[4]);
        mapper245_load_chr1k(nes, m, 3, m->bank_values[5]);
        mapper245_load_chr1k(nes, m, 4, (uint16_t)(m->bank_values[0] & 0xFEu));
        mapper245_load_chr1k(nes, m, 5, (uint16_t)(m->bank_values[0] | 0x01u));
        mapper245_load_chr1k(nes, m, 6, (uint16_t)(m->bank_values[1] & 0xFEu));
        mapper245_load_chr1k(nes, m, 7, (uint16_t)(m->bank_values[1] | 0x01u));
    }
}

static void nes_mapper_init(nes_t* nes) {
    if (nes->nes_mapper.mapper_register == NULL) {
        nes->nes_mapper.mapper_register = nes_mapper_register_alloc(nes, (uint16_t)sizeof(mapper245_t));
        if (nes->nes_mapper.mapper_register == NULL) return;
    }
    mapper245_t* m = (mapper245_t*)nes->nes_mapper.mapper_register;
    nes_memset(m, 0, sizeof(mapper245_t));

    m->prg_bank_count = (uint16_t)(nes->nes_rom.prg_rom_size * 2u);
    m->chr_bank_count = (uint16_t)(nes->nes_rom.chr_rom_size * 8u);

    m->bank_values[6] = 0;
    m->bank_values[7] = 1;

    if (nes->nes_rom.chr_rom_size == 0u) nes_load_chrrom_8k(nes, 0, 0);
    mapper245_update_banks(nes);
}

/*
 * Register map identical to MMC3:
 *   $8000 (even): bank select  $8001 (odd): bank data
 *   $A000 (even): mirroring    $A001 (odd): PRG RAM protect (ignored)
 *   $C000 (even): IRQ latch    $C001 (odd): IRQ reload
 *   $E000 (even): IRQ disable  $E001 (odd): IRQ enable
 */
static void nes_mapper_write(nes_t* nes, uint16_t address, uint8_t data) {
    mapper245_t* m = (mapper245_t*)nes->nes_mapper.mapper_register;
    switch (address & 0xE001u) {
    case 0x8000u:
        m->bank_select = data;
        mapper245_update_banks(nes);
        break;
    case 0x8001u: {
        const uint8_t reg = (uint8_t)(m->bank_select & 0x07u);
        m->bank_values[reg] = data;
        mapper245_update_banks(nes);
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

/* Scanline IRQ - identical to MMC3. */
static void nes_mapper_hsync(nes_t* nes) {
    mapper245_t* m = (mapper245_t*)nes->nes_mapper.mapper_register;
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

static void mapper245_state_reapply(nes_t* nes) {
    mapper245_update_banks(nes);
}

int nes_mapper245_init(nes_t* nes) {
    nes->nes_mapper.mapper_init   = nes_mapper_init;
    nes->nes_mapper.mapper_deinit = nes_mapper_deinit;
    nes->nes_mapper.mapper_write  = nes_mapper_write;
    nes->nes_mapper.mapper_hsync  = nes_mapper_hsync;
    nes->nes_mapper.mapper_state_reapply = mapper245_state_reapply;
    return NES_OK;
}
