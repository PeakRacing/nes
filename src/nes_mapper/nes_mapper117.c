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
 * Mapper 117 - Waixing "one register per address" board (三国志4 等).
 * Authority: Mesen2 Core/NES/Mappers/Unlicensed/Mapper117.h.
 *
 * Four 8KB PRG slots and eight 1KB CHR slots are written DIRECTLY, one address per slot:
 *
 *   $8000-$8003 : PRG 8KB slot 0-3 = value
 *   $A000-$A007 : CHR 1KB slot 0-7 = value
 *   $C001       : IRQ reload value
 *   $C002       : acknowledge the IRQ
 *   $C003       : counter = reload value, and arm the "alt" enable flag
 *   $D000       : mirroring (bit 0 set -> horizontal, clear -> vertical)
 *   $E000       : IRQ enable = bit 0 (also acknowledges)
 *
 * The IRQ counter only counts down while BOTH enable flags are set: $E000 arms the main
 * one, $C003 the alt one; reaching zero fires the IRQ and clears the alt flag.
 *
 * Power-on shows the LAST 32KB in $8000-$FFFF (SelectPrgPage4x(0, -4)).
 *
 * The previous implementation was a completely different board (an MMC3 with outer bank
 * registers at $5000-$5003), so games that never see the expected free banks stayed blank.
 */

typedef struct {
    uint8_t prg[4];
    uint8_t chr[8];
    uint8_t irq_reload_value;
    uint8_t irq_counter;
    uint8_t irq_enabled;
    uint8_t irq_enabled_alt;
    uint16_t prg_bank_count;   /* number of 8KB PRG banks */
    uint16_t chr_bank_count;   /* number of 1KB CHR banks */
} mapper117_t;

static void nes_mapper_deinit(nes_t* nes) {
    nes_free(nes->nes_mapper.mapper_register);
    nes->nes_mapper.mapper_register = NULL;
}

static void mapper117_update_prg(nes_t* nes) {
    mapper117_t* m = (mapper117_t*)nes->nes_mapper.mapper_register;
    uint8_t slot;
    for (slot = 0; slot < 4u; slot++) {
        nes_load_prgrom_8k(nes, slot, m->prg[slot]);
    }
}

static void mapper117_update_chr(nes_t* nes) {
    mapper117_t* m = (mapper117_t*)nes->nes_mapper.mapper_register;
    uint8_t slot;
    if (m->chr_bank_count == 0u) return;
    for (slot = 0; slot < 8u; slot++) {
        nes_load_chrrom_1k(nes, slot, (uint16_t)(m->chr[slot] % m->chr_bank_count));
    }
}

static void nes_mapper_init(nes_t* nes) {
    if (nes->nes_mapper.mapper_register == NULL) {
        nes->nes_mapper.mapper_register = nes_mapper_register_alloc(nes, (uint16_t)sizeof(mapper117_t));
        if (nes->nes_mapper.mapper_register == NULL) return;
    }
    mapper117_t* m = (mapper117_t*)nes->nes_mapper.mapper_register;
    nes_memset(m, 0, sizeof(mapper117_t));

    m->prg_bank_count = (uint16_t)(nes->nes_rom.prg_rom_size * 2u);
    m->chr_bank_count = (uint16_t)(nes->nes_rom.chr_rom_size * 8u);

    /* Power-on: the last 32KB sits in $8000-$FFFF (SelectPrgPage4x(0, -4)). */
    if (m->prg_bank_count >= 4u) {
        const uint16_t first = (uint16_t)(m->prg_bank_count - 4u);
        for (uint8_t slot = 0; slot < 4u; slot++) {
            m->prg[slot] = (uint8_t)(first + slot);
        }
    }

    if (nes->nes_rom.chr_rom_size == 0u) nes_load_chrrom_8k(nes, 0, 0);
    mapper117_update_prg(nes);
    mapper117_update_chr(nes);
}

static void nes_mapper_write(nes_t* nes, uint16_t address, uint8_t data) {
    mapper117_t* m = (mapper117_t*)nes->nes_mapper.mapper_register;

    if (address >= 0x8000u && address <= 0x8003u) {
        m->prg[address & 0x03u] = data;
        mapper117_update_prg(nes);
        return;
    }
    if (address >= 0xA000u && address <= 0xA007u) {
        m->chr[address & 0x07u] = data;
        mapper117_update_chr(nes);
        return;
    }
    if (address >= 0xC000u && address <= 0xE000u) {
        switch (address & 0xE003u) {
        case 0xC001u: m->irq_reload_value = data; break;
        case 0xC002u: nes->nes_cpu.irq_pending = 0; break;
        case 0xC003u:
            m->irq_counter = m->irq_reload_value;
            m->irq_enabled_alt = 1u;
            break;
        case 0xE000u:
            m->irq_enabled = (uint8_t)(data & 0x01u);
            nes->nes_cpu.irq_pending = 0;
            break;
        default:
            break;
        }
        return;
    }
    if (address == 0xD000u) {
        if (nes->nes_rom.four_screen == 0) {
            nes_ppu_screen_mirrors(nes, (data & 0x01u) ? NES_MIRROR_HORIZONTAL : NES_MIRROR_VERTICAL);
        }
    }
    (void)m;
}

/* The scanline hook stands in for Mesen's A12 watcher: the counter only moves while both
   enable flags are set, and hitting zero fires and disarms the alt flag. */
static void nes_mapper_hsync(nes_t* nes) {
    mapper117_t* m = (mapper117_t*)nes->nes_mapper.mapper_register;
    if (nes->nes_ppu.MASK_b == 0 && nes->nes_ppu.MASK_s == 0) return;
    if (m->irq_enabled == 0u || m->irq_enabled_alt == 0u || m->irq_counter == 0u) return;

    m->irq_counter--;
    if (m->irq_counter == 0u) {
        m->irq_enabled_alt = 0u;
        nes_cpu_irq(nes);
    }
}

static void nes_mapper_state_reapply(nes_t* nes) {
    mapper117_update_prg(nes);
    mapper117_update_chr(nes);
}

int nes_mapper117_init(nes_t* nes) {
    nes->nes_mapper.mapper_init   = nes_mapper_init;
    nes->nes_mapper.mapper_deinit = nes_mapper_deinit;
    nes->nes_mapper.mapper_write  = nes_mapper_write;
    nes->nes_mapper.mapper_hsync  = nes_mapper_hsync;
    nes->nes_mapper.mapper_state_reapply = nes_mapper_state_reapply;
    return NES_OK;
}
