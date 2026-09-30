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

/* https://www.nesdev.org/wiki/INES_Mapper_153
 * Mapper 153 — Bandai FCG (LZ93D50) with 8KB of work RAM at $6000-$7FFF instead of a serial
 * EEPROM.  Mirrors Mesen2's Core/NES/Mappers/Bandai/BandaiFcg.h with MapperID == 153:
 *
 *   $8000-$FFFF, register = address & 0x0F
 *     $0-$7  CHR 1KB pages 0-7.  On this board each register's bit 0 also feeds the PRG page's
 *            high bits (bit 4 for register 0 ... bit 11 for register 7? no - see below):
 *            _prgBankSelect |= (_chrRegs[i] & 0x01) << 4, i.e. bit 4 of the 16KB PRG page.
 *     $8     _prgPage = value & 0x0F (the low 4 bits of the PRG page)
 *     $9     mirroring: 0 vertical, 1 horizontal, 2 one-screen A, 3 one-screen B
 *     $A     IRQ enable (bit 0); writing also copies the reload latch into the counter
 *     $B/$C  IRQ reload latch, low / high byte
 *     $D     $6000-$7FFF access switch (bit 5 = read/write, otherwise disabled)
 *
 *   PRG is 16KB pages: slot 0 = _prgPage | _prgBankSelect, slot 1 = 0x0F | _prgBankSelect
 *   (the last page of the current 16-page block), i.e. $C000-$FFFF is NOT always the very last
 *   16KB of the ROM.
 *
 * The old implementation modelled $8 as "bit0 = outer bank, bits 1-3 = page", used $9/$A as the
 * IRQ latch and ignored mirroring entirely, so 英雄列传2 (Famicom Jump II) never started.
 *
 * Registers are only reachable at $8000-$FFFF on this board: $6000-$7FFF is the work RAM, and
 * Mesen removes the register range there.
 */

typedef struct {
    uint8_t  chr[8];
    uint8_t  prg_page;         /* low 4 bits of the 16KB PRG page */
    uint8_t  prg_bank_select;  /* bit 4 of the 16KB PRG page, rebuilt from the CHR registers */
    uint8_t  irq_enabled;
    uint8_t  irq_reload_low;
    uint8_t  irq_reload_high;
    uint8_t  irq_reload_pending;   /* set when $A copies the latch into the counter */
    uint16_t irq_counter;
    uint16_t irq_reload;
    uint16_t prg_page_count;   /* 16KB pages */
    uint16_t chr_bank_count;   /* 1KB pages */
} mapper153_t;

static void nes_mapper_deinit(nes_t* nes) {
    nes_free(nes->nes_mapper.mapper_register);
    nes->nes_mapper.mapper_register = NULL;
}

static void mapper153_update_banks(nes_t* nes) {
    mapper153_t* m = (mapper153_t*)nes->nes_mapper.mapper_register;
    if (m->prg_page_count == 0u) return;
    uint16_t first = (uint16_t)((m->prg_page | m->prg_bank_select) % m->prg_page_count);
    uint16_t fixed = (uint16_t)((0x0Fu | m->prg_bank_select) % m->prg_page_count);
    nes_load_prgrom_16k(nes, 0, first);
    nes_load_prgrom_16k(nes, 1, fixed);
    if (m->chr_bank_count > 0u) {
        for (uint8_t i = 0u; i < 8u; i++) {
            nes_load_chrrom_1k(nes, i, (uint16_t)(m->chr[i] % m->chr_bank_count));
        }
    }
}

/* Each CHR register's bit 0 is also PRG page bit 4 on this board. */
static void mapper153_rebuild_prg_bank_select(mapper153_t* m) {
    uint8_t sel = 0u;
    for (uint8_t i = 0u; i < 8u; i++) {
        sel = (uint8_t)(sel | ((m->chr[i] & 0x01u) << 4));
    }
    m->prg_bank_select = (uint8_t)(sel & 0x10u);
}

static void nes_mapper_init(nes_t* nes) {
    if (nes->nes_mapper.mapper_register == NULL) {
        nes->nes_mapper.mapper_register = nes_mapper_register_alloc(nes, (uint16_t)sizeof(mapper153_t));
        if (nes->nes_mapper.mapper_register == NULL) return;
    }
    mapper153_t* m = (mapper153_t*)nes->nes_mapper.mapper_register;
    nes_memset(m, 0, sizeof(mapper153_t));
    m->prg_page_count = (uint16_t)(nes->nes_rom.prg_rom_size);      /* 16KB units */
    m->chr_bank_count = (uint16_t)(nes->nes_rom.chr_rom_size * 8u); /* 8KB units -> 1KB pages */

    nes_ppu_screen_mirrors(nes, NES_MIRROR_VERTICAL);
    if (nes->nes_rom.chr_rom_size == 0u) nes_load_chrrom_8k(nes, 0, 0);
    mapper153_rebuild_prg_bank_select(m);
    mapper153_update_banks(nes);

    /* 8KB work RAM at $6000-$7FFF (Mesen: "Mapper 153 has regular save ram from $6000-$7FFF";
     * the board switches it in through register $D but the core maps nes_rom.sram there as soon
     * as the buffer exists).  The iNES header has no battery bit for these carts. */
    if (nes->nes_rom.sram == NULL) {
        nes->nes_rom.sram = (uint8_t*)nes_malloc(SRAM_SIZE);
        if (nes->nes_rom.sram != NULL) {
            nes_memset(nes->nes_rom.sram, 0, SRAM_SIZE);
        } else {
            NES_LOG_ERROR("mapper153: failed to allocate work RAM\n");
        }
    }
}

static void nes_mapper_write(nes_t* nes, uint16_t address, uint8_t data) {
    mapper153_t* m = (mapper153_t*)nes->nes_mapper.mapper_register;
    if (m == NULL) return;
    if (address < 0x8000u) return;   /* $6000-$7FFF is work RAM on this board */
    switch (address & 0x000Fu) {
    case 0x00u: case 0x01u: case 0x02u: case 0x03u:
    case 0x04u: case 0x05u: case 0x06u: case 0x07u:
        m->chr[address & 0x07u] = data;
        mapper153_rebuild_prg_bank_select(m);
        mapper153_update_banks(nes);
        break;
    case 0x08u:
        m->prg_page = (uint8_t)(data & 0x0Fu);
        mapper153_update_banks(nes);
        break;
    case 0x09u:
        switch (data & 0x03u) {
        case 0u: nes_ppu_screen_mirrors(nes, NES_MIRROR_VERTICAL); break;
        case 1u: nes_ppu_screen_mirrors(nes, NES_MIRROR_HORIZONTAL); break;
        case 2u: nes_ppu_screen_mirrors(nes, NES_MIRROR_ONE_SCREEN0); break;
        default: nes_ppu_screen_mirrors(nes, NES_MIRROR_ONE_SCREEN1); break;
        }
        break;
    case 0x0Au:
        m->irq_enabled = (uint8_t)(data & 0x01u);
        m->irq_counter = m->irq_reload;    /* Famicom Jump II needs this copy */
        nes->nes_cpu.irq_pending = 0;
        break;
    case 0x0Bu:
        m->irq_reload_low = data;
        m->irq_reload = (uint16_t)((m->irq_reload & 0xFF00u) | data);
        break;
    case 0x0Cu:
        m->irq_reload_high = data;
        m->irq_reload = (uint16_t)((m->irq_reload & 0x00FFu) | ((uint16_t)data << 8));
        break;
    case 0x0Du:
        /* $6000-$7FFF access switch (bit 5).  The core keeps nes_rom.sram mapped once it
         * exists, so this bit is recorded but does not gate the window. */
        m->irq_reload_pending = (uint8_t)((data & 0x20u) ? 1u : 0u);
        break;
    default:
        break;
    }
}

static void nes_mapper_cpu_clock(nes_t* nes, uint16_t cycles) {
    mapper153_t* m = (mapper153_t*)nes->nes_mapper.mapper_register;
    if (m == NULL || !m->irq_enabled) return;
    if (m->irq_counter <= (uint16_t)cycles) {
        m->irq_counter = 0u;
        m->irq_enabled = 0u;
        nes_cpu_irq(nes);
    } else {
        m->irq_counter = (uint16_t)(m->irq_counter - cycles);
    }
}

static void nes_mapper_state_reapply(nes_t* nes) {
    mapper153_rebuild_prg_bank_select((mapper153_t*)nes->nes_mapper.mapper_register);
    mapper153_update_banks(nes);
}

int nes_mapper153_init(nes_t* nes) {
    nes->nes_mapper.mapper_init          = nes_mapper_init;
    nes->nes_mapper.mapper_deinit        = nes_mapper_deinit;
    nes->nes_mapper.mapper_write         = nes_mapper_write;
    nes->nes_mapper.mapper_cpu_clock     = nes_mapper_cpu_clock;
    nes->nes_mapper.mapper_state_reapply = nes_mapper_state_reapply;
    return NES_OK;
}
