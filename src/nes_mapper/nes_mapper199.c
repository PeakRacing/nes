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
 * Mapper 199 - MMC3 plus four extension registers and a private 8KB CHR-RAM.
 * Authority: Mesen2 Core/NES/Mappers/Mmc3Variants/MMC3_199.h.
 *
 *   $8000      : MMC3 bank select.  While bit 3 is SET, $8001 no longer writes the MMC3
 *                register file but exRegs[value & 3] instead.
 *   $8001      : bank data, or an extension register (see above).
 *   $A000      : MMC3 mirroring - 0 = vertical, 1 = horizontal, 2 = one-screen A,
 *                3 = one-screen B (this board uses the single-screen modes).
 *   $C000/$C001/$E000/$E001 : standard MMC3 scanline IRQ.
 *
 * The PRG window is a normal MMC3 layout whose slots 2/3 are fixed to exRegs[0]/exRegs[1]
 * (0xFE/0xFF at power-on).  CHR keeps the MMC3 layout but slots 1 and 3 come from
 * exRegs[2]/exRegs[3] (1 and 3 at power-on), and every CHR page below 8 reads the board's
 * own 8KB CHR-RAM instead of CHR-ROM - that is where these games put their Chinese fonts.
 *
 * The previous implementation was a different Waixing board (a bespoke register layout
 * reverse-engineered from a single game), which is why 汤姆历险记 stayed blank.
 */

#define MAPPER199_CHR_RAM_SIZE (0x2000u)

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
    uint8_t ex_regs[4];
    uint8_t* chr_ram;           /* 8KB private CHR-RAM for pages 0-7 */
} mapper199_t;

static void nes_mapper_deinit(nes_t* nes) {
    mapper199_t* m = (mapper199_t*)nes->nes_mapper.mapper_register;
    if (m != NULL && m->chr_ram != NULL) {
        nes_free(m->chr_ram);
        m->chr_ram = NULL;
    }
    nes_free(nes->nes_mapper.mapper_register);
    nes->nes_mapper.mapper_register = NULL;
}

/* Pages 0-7 come from the private CHR-RAM, everything else from CHR-ROM. */
static void mapper199_load_chr1k(nes_t* nes, mapper199_t* m, uint8_t slot, uint16_t page) {
    if (page < 8u && m->chr_ram != NULL) {
        nes->nes_ppu.pattern_table[slot] = m->chr_ram + (uint32_t)page * 1024u;
        return;
    }
    if (m->chr_bank_count > 0u) {
        nes_load_chrrom_1k(nes, slot, (uint16_t)(page % m->chr_bank_count));
    }
}

static void mapper199_update_banks(nes_t* nes) {
    mapper199_t* m = (mapper199_t*)nes->nes_mapper.mapper_register;
    const uint8_t prg_mode = (uint8_t)((m->bank_select >> 6) & 1u);
    const uint8_t chr_mode = (uint8_t)((m->bank_select >> 7) & 1u);
    const uint16_t count = (m->prg_bank_count != 0u) ? m->prg_bank_count : 1u;
    const uint16_t slast = (uint16_t)((count >= 2u) ? (count - 2u) : 0u);

    /* Standard MMC3 layout, with the two fixed slots overridden afterwards. */
    if (prg_mode == 0u) {
        nes_load_prgrom_8k(nes, 0, (uint16_t)(m->bank_values[6] % count));
        nes_load_prgrom_8k(nes, 1, (uint16_t)(m->bank_values[7] % count));
    } else {
        nes_load_prgrom_8k(nes, 1, (uint16_t)(m->bank_values[7] % count));
        nes_load_prgrom_8k(nes, 0, slast);
    }
    nes_load_prgrom_8k(nes, 2, m->ex_regs[0]);
    nes_load_prgrom_8k(nes, 3, m->ex_regs[1]);

    if (chr_mode == 0u) {
        mapper199_load_chr1k(nes, m, 0, (uint16_t)(m->bank_values[0] & 0xFEu));
        mapper199_load_chr1k(nes, m, 1, m->ex_regs[2]);
        mapper199_load_chr1k(nes, m, 2, (uint16_t)(m->bank_values[1] & 0xFEu));
        mapper199_load_chr1k(nes, m, 3, m->ex_regs[3]);
        mapper199_load_chr1k(nes, m, 4, m->bank_values[2]);
        mapper199_load_chr1k(nes, m, 5, m->bank_values[3]);
        mapper199_load_chr1k(nes, m, 6, m->bank_values[4]);
        mapper199_load_chr1k(nes, m, 7, m->bank_values[5]);
    } else {
        mapper199_load_chr1k(nes, m, 0, m->bank_values[2]);
        mapper199_load_chr1k(nes, m, 1, m->ex_regs[2]);
        mapper199_load_chr1k(nes, m, 2, m->bank_values[3]);
        mapper199_load_chr1k(nes, m, 3, m->ex_regs[3]);
        mapper199_load_chr1k(nes, m, 4, (uint16_t)(m->bank_values[0] & 0xFEu));
        mapper199_load_chr1k(nes, m, 5, (uint16_t)(m->bank_values[0] | 0x01u));
        mapper199_load_chr1k(nes, m, 6, (uint16_t)(m->bank_values[1] & 0xFEu));
        mapper199_load_chr1k(nes, m, 7, (uint16_t)(m->bank_values[1] | 0x01u));
    }
}

static void nes_mapper_init(nes_t* nes) {
    if (nes->nes_mapper.mapper_register == NULL) {
        nes->nes_mapper.mapper_register = nes_mapper_register_alloc(nes, (uint16_t)sizeof(mapper199_t));
        if (nes->nes_mapper.mapper_register == NULL) return;
    }
    mapper199_t* m = (mapper199_t*)nes->nes_mapper.mapper_register;
    nes_memset(m, 0, sizeof(mapper199_t));

    m->prg_bank_count = (uint16_t)(nes->nes_rom.prg_rom_size * 2u);
    m->chr_bank_count = (uint16_t)(nes->nes_rom.chr_rom_size * 8u);
    m->bank_values[6] = 0;
    m->bank_values[7] = 1;
    m->ex_regs[0] = 0xFEu;
    m->ex_regs[1] = 0xFFu;
    m->ex_regs[2] = 1u;
    m->ex_regs[3] = 3u;

    m->chr_ram = (uint8_t*)nes_malloc(MAPPER199_CHR_RAM_SIZE);
    if (m->chr_ram != NULL) {
        nes_memset(m->chr_ram, 0, MAPPER199_CHR_RAM_SIZE);
    }

    mapper199_update_banks(nes);
}

static void nes_mapper_write(nes_t* nes, uint16_t address, uint8_t data) {
    mapper199_t* m = (mapper199_t*)nes->nes_mapper.mapper_register;

    switch (address & 0xE001u) {
    case 0x8000:
        m->bank_select = data;
        mapper199_update_banks(nes);
        break;
    case 0x8001:
        if (m->bank_select & 0x08u) {
            m->ex_regs[m->bank_select & 0x03u] = data;   /* extension register */
        } else {
            m->bank_values[m->bank_select & 0x07u] = data;
        }
        mapper199_update_banks(nes);
        break;
    case 0xA000:
        m->mirroring = (uint8_t)(data & 0x03u);
        if (nes->nes_rom.four_screen == 0) {
            static const nes_mirror_type_t table[4] = {
                NES_MIRROR_VERTICAL, NES_MIRROR_HORIZONTAL,
                NES_MIRROR_ONE_SCREEN0, NES_MIRROR_ONE_SCREEN1,
            };
            nes_ppu_screen_mirrors(nes, table[m->mirroring]);
        }
        break;
    case 0xC000: m->irq_latch = data; break;
    case 0xC001: m->irq_reload = 1; break;
    case 0xE000: m->irq_enabled = 0; nes->nes_cpu.irq_pending = 0; break;
    case 0xE001: m->irq_enabled = 1; break;
    default: break;
    }
}

static void nes_mapper_hsync(nes_t* nes) {
    mapper199_t* m = (mapper199_t*)nes->nes_mapper.mapper_register;
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
    mapper199_update_banks(nes);
}

int nes_mapper199_init(nes_t* nes) {
    nes->nes_mapper.mapper_init          = nes_mapper_init;
    nes->nes_mapper.mapper_deinit        = nes_mapper_deinit;
    nes->nes_mapper.mapper_write         = nes_mapper_write;
    nes->nes_mapper.mapper_hsync         = nes_mapper_hsync;
    nes->nes_mapper.mapper_state_reapply = nes_mapper_state_reapply;
    return NES_OK;
}
