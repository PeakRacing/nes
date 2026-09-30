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

/* https://www.nesdev.org/wiki/INES_Mapper_165
 * Mapper 165 — the Chinese "Fire Emblem" board (圣火徽章).  Mirrors Mesen2's
 * Core/NES/Mappers/Mmc3Variants/MMC3_165.h: standard MMC3 PRG banking, but CHR is
 * selected MMC2-style — two 4KB slots whose source page is picked by a latch that flips
 * when the PPU fetches from tile $FD/$FE:
 *
 *   slot 0 <- register[latch0 ? 1 : 0]      slot 1 <- register[latch1 ? 4 : 2]
 *   page == 0 -> 4KB CHR-RAM, otherwise the CHR-ROM 4KB page (register page >> 2)
 *
 * The old implementation was MMC2-flavoured instead: one PRG register, CHR bank registers at
 * $B000-$E000, and the MMC2 trigger addresses.  Fire Emblem Gaiden never reaches its title
 * screen with that mapping (verdict `blank`, first_render 0), because its PRG banking and its
 * CHR page numbers are simply not what the board expects.
 */

typedef struct {
    uint8_t  bank_select;
    uint8_t  bank_values[8];
    uint8_t  mirroring;
    uint8_t  irq_latch;
    uint8_t  irq_counter;
    uint8_t  irq_reload;
    uint8_t  irq_enabled;
    uint8_t  chr_latch[2];
    uint16_t prg_bank_count;   /* 8KB pages */
    uint16_t chr_bank_count;   /* 1KB pages */
    uint8_t* chr_ram;          /* 4KB, used when a CHR register selects page 0 */
} mapper165_t;

static void nes_mapper_deinit(nes_t* nes) {
    mapper165_t* m = (mapper165_t*)nes->nes_mapper.mapper_register;
    if (m != NULL && m->chr_ram != NULL) {
        nes_free(m->chr_ram);
        m->chr_ram = NULL;
    }
    nes_free(nes->nes_mapper.mapper_register);
    nes->nes_mapper.mapper_register = NULL;
}

/* Point one 4KB CHR slot at either the board's 4KB CHR-RAM or a CHR-ROM page. */
static void mapper165_set_chr_slot(nes_t* nes, mapper165_t* m, uint8_t slot, uint8_t page) {
    uint8_t* base = m->chr_ram;
    if (page == 0u && base != NULL) {
        nes->nes_ppu.pattern_table[slot * 4u + 0u] = base + 0u * 1024u;
        nes->nes_ppu.pattern_table[slot * 4u + 1u] = base + 1u * 1024u;
        nes->nes_ppu.pattern_table[slot * 4u + 2u] = base + 2u * 1024u;
        nes->nes_ppu.pattern_table[slot * 4u + 3u] = base + 3u * 1024u;
        return;
    }
    /* CHR-ROM page numbers are in 1KB units in the MMC3 registers; a 4KB slot is page >> 2. */
    {
        uint16_t p4 = (uint16_t)(page >> 2);
        if (m->chr_bank_count != 0u) p4 = (uint16_t)(p4 % (m->chr_bank_count / 4u ? m->chr_bank_count / 4u : 1u));
        nes_load_chrrom_4k(nes, slot, p4);
    }
}

static void mapper165_update_banks(nes_t* nes) {
    mapper165_t* m = (mapper165_t*)nes->nes_mapper.mapper_register;
    const uint8_t prg_mode = (m->bank_select >> 6) & 1u;
    const uint16_t last  = (uint16_t)(m->prg_bank_count - 1u);
    const uint16_t slast = (uint16_t)(m->prg_bank_count - 2u);

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
        nes_load_prgrom_8k(nes, i, prg[i]);
    }

    /* CHR: two 4KB slots, source page chosen by the MMC2-style latches. */
    mapper165_set_chr_slot(nes, m, 0u, m->bank_values[m->chr_latch[0] ? 1u : 0u]);
    mapper165_set_chr_slot(nes, m, 1u, m->bank_values[m->chr_latch[1] ? 4u : 2u]);
}

static void nes_mapper_init(nes_t* nes) {
    if (nes->nes_mapper.mapper_register == NULL) {
        nes->nes_mapper.mapper_register = nes_mapper_register_alloc(nes, (uint16_t)sizeof(mapper165_t));
        if (nes->nes_mapper.mapper_register == NULL) return;
    }
    mapper165_t* m = (mapper165_t*)nes->nes_mapper.mapper_register;
    nes_memset(m, 0, sizeof(mapper165_t));
    m->prg_bank_count = (uint16_t)(nes->nes_rom.prg_rom_size * 2u);   /* 16KB units -> 8KB pages */
    m->chr_bank_count = (uint16_t)(nes->nes_rom.chr_rom_size * 8u);   /* 8KB units -> 1KB pages  */

    /* The board carries 4KB of CHR-RAM next to the CHR-ROM (Mesen: GetChrRamSize 0x1000).
     * The core only allocates a CHR buffer when the header has no CHR-ROM at all, so when the
     * cart does have CHR-ROM the board has to own this one and hand the PPU the pointers. */
    if (nes->nes_rom.chr_rom_size != 0u && m->chr_ram == NULL) {
        m->chr_ram = (uint8_t*)nes_malloc(4096u);
        if (m->chr_ram != NULL) {
            nes_memset(m->chr_ram, 0, 4096u);
        } else {
            NES_LOG_ERROR("mapper165: failed to allocate 4KB CHR-RAM\n");
        }
    }
    if (nes->nes_rom.chr_rom_size == 0u) {
        nes_load_chrrom_8k(nes, 0, 0);
    }
    /* Standard MMC3 power-on register values: R6 = 0 and R7 = 1, so $8000 maps 8KB bank 0 and
     * $A000 maps bank 1 before the game writes anything. */
    m->bank_values[6] = 0u;
    m->bank_values[7] = 1u;
    mapper165_update_banks(nes);

    /* 8KB work RAM at $6000-$7FFF (Mesen lists workRAM=8 for these boards and the iNES header
     * has no battery bit). */
    if (nes->nes_rom.sram == NULL) {
        nes->nes_rom.sram = (uint8_t*)nes_malloc(SRAM_SIZE);
        if (nes->nes_rom.sram != NULL) {
            nes_memset(nes->nes_rom.sram, 0, SRAM_SIZE);
        } else {
            NES_LOG_ERROR("mapper165: failed to allocate work RAM\n");
        }
    }
}

static void nes_mapper_write(nes_t* nes, uint16_t address, uint8_t data) {
    mapper165_t* m = (mapper165_t*)nes->nes_mapper.mapper_register;
    if (m == NULL) return;
    switch (address & 0xE001u) {
    case 0x8000: m->bank_select = data; mapper165_update_banks(nes); break;
    case 0x8001:
        m->bank_values[m->bank_select & 0x07u] = data;
        mapper165_update_banks(nes);
        break;
    case 0xA000:
        m->mirroring = data & 1u;
        if (nes->nes_rom.four_screen == 0)
            nes_ppu_screen_mirrors(nes, m->mirroring ? NES_MIRROR_HORIZONTAL : NES_MIRROR_VERTICAL);
        break;
    case 0xA001: break;
    case 0xC000: m->irq_latch  = data; break;
    case 0xC001: m->irq_reload = 1; break;
    case 0xE000: m->irq_enabled = 0; nes->nes_cpu.irq_pending = 0; break;
    case 0xE001: m->irq_enabled = 1; break;
    default: break;
    }
}

static void nes_mapper_hsync(nes_t* nes) {
    mapper165_t* m = (mapper165_t*)nes->nes_mapper.mapper_register;
    if (m == NULL) return;
    if (nes->nes_ppu.MASK_b == 0 && nes->nes_ppu.MASK_s == 0) return;
    if (m->irq_counter == 0u || m->irq_reload) {
        m->irq_counter = m->irq_latch;
    } else {
        m->irq_counter--;
    }
    if (m->irq_counter == 0u && m->irq_enabled) nes_cpu_irq(nes);
    m->irq_reload = 0;
}

/*
 * MMC2-style CHR latch, per Mesen's NotifyVramAddressChange: the latches flip when the PPU
 * reaches $FD0-$FD7 (latch 0) or $FE8-$FEF (latch 1) in either pattern table, where address
 * bit 12 picks the slot.  The core's mapper_ppu hook receives the pattern address, so the
 * test is (address & 0x1FF8) against the two triggers.
 */
static void nes_mapper_ppu(nes_t* nes, uint16_t address) {
    mapper165_t* m = (mapper165_t*)nes->nes_mapper.mapper_register;
    if (m == NULL) return;
    switch (address & 0x1FF8u) {
    case 0x0FD0u:
    case 0x0FE8u:
        m->chr_latch[(address >> 12) & 0x01u] = ((address & 0x08u) == 0x08u) ? 1u : 0u;
        mapper165_update_banks(nes);
        break;
    default:
        break;
    }
}

static void nes_mapper_state_reapply(nes_t* nes) {
    mapper165_update_banks(nes);
}

int nes_mapper165_init(nes_t* nes) {
    nes->nes_mapper.mapper_init          = nes_mapper_init;
    nes->nes_mapper.mapper_deinit        = nes_mapper_deinit;
    nes->nes_mapper.mapper_write         = nes_mapper_write;
    nes->nes_mapper.mapper_hsync         = nes_mapper_hsync;
    nes->nes_mapper.mapper_ppu           = nes_mapper_ppu;
    nes->nes_mapper.mapper_ppu_tile_min  = 0xFD;
    nes->nes_mapper.mapper_ppu_tile_max  = 0xFE;
    nes->nes_mapper.mapper_state_reapply = nes_mapper_state_reapply;
    return NES_OK;
}
