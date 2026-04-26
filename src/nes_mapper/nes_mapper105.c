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

/* https://www.nesdev.org/wiki/INES_Mapper_105
 * Mapper 105 — NES-EVENT (Nintendo World Championships 1990).
 * Based on MMC1 with an additional DIP-switch counter IRQ.
 * Timer counts down CPU cycles; fires IRQ when reaching 0.
 * For MCU: implement full MMC1 + cycle-based IRQ.
 */

typedef struct {
    uint8_t shift;
    uint8_t shift_count;
    uint8_t regs[4];
    uint32_t irq_counter;
    uint8_t irq_enabled;
    uint8_t prg_bank_count;
} mapper105_t;

#define MAPPER105_IRQ_PERIOD (0x20000000u)

static void nes_mapper_deinit(nes_t* nes) {
    nes_free(nes->nes_mapper.mapper_register);
    nes->nes_mapper.mapper_register = NULL;
}

static void mapper105_update_banks(nes_t* nes) {
    mapper105_t* m = (mapper105_t*)nes->nes_mapper.mapper_register;
    uint8_t prg16 = (uint8_t)(m->prg_bank_count / 2u);
    if (prg16 == 0u) prg16 = 1u;

    uint8_t prg_mode = (m->regs[0] >> 2u) & 0x03u;
    uint8_t bank = m->regs[3] & 0x07u;
    m->irq_enabled = (m->regs[1] & 0x10u) ? 0u : 1u;

    switch (prg_mode) {
    case 0u: case 1u:
        nes_load_prgrom_32k(nes, 0, (uint16_t)((bank >> 1u) % (prg16 / 2u > 0u ? prg16 / 2u : 1u)));
        break;
    case 2u:
        nes_load_prgrom_16k(nes, 0, 0);
        nes_load_prgrom_16k(nes, 2, (uint16_t)(bank % prg16));
        break;
    case 3u:
        nes_load_prgrom_16k(nes, 0, (uint16_t)(bank % prg16));
        nes_load_prgrom_16k(nes, 2, (uint16_t)(prg16 - 1u));
        break;
    }

    uint8_t mirror = m->regs[0] & 0x03u;
    if (nes->nes_rom.four_screen == 0) {
        switch (mirror) {
        case 0u: nes_ppu_screen_mirrors(nes, NES_MIRROR_ONE_SCREEN0); break;
        case 1u: nes_ppu_screen_mirrors(nes, NES_MIRROR_ONE_SCREEN1); break;
        case 2u: nes_ppu_screen_mirrors(nes, NES_MIRROR_VERTICAL);    break;
        default: nes_ppu_screen_mirrors(nes, NES_MIRROR_HORIZONTAL);  break;
        }
    }
    nes_load_chrrom_8k(nes, 0, 0);
}

static void nes_mapper_init(nes_t* nes) {
    if (nes->nes_mapper.mapper_register == NULL) {
        nes->nes_mapper.mapper_register = nes_malloc(sizeof(mapper105_t));
        if (nes->nes_mapper.mapper_register == NULL) return;
    }
    mapper105_t* m = (mapper105_t*)nes->nes_mapper.mapper_register;
    nes_memset(m, 0, sizeof(mapper105_t));
    m->prg_bank_count = (uint8_t)(nes->nes_rom.prg_rom_size * 2u);
    m->regs[0] = 0x0Cu;
    m->irq_counter = MAPPER105_IRQ_PERIOD;
    mapper105_update_banks(nes);
}

static void nes_mapper_write(nes_t* nes, uint16_t address, uint8_t data) {
    mapper105_t* m = (mapper105_t*)nes->nes_mapper.mapper_register;
    if (data & 0x80u) {
        m->shift = 0u;
        m->shift_count = 0u;
        m->regs[0] |= 0x0Cu;
        mapper105_update_banks(nes);
        return;
    }
    m->shift = (uint8_t)((m->shift >> 1u) | ((data & 0x01u) << 4u));
    m->shift_count++;
    if (m->shift_count == 5u) {
        uint8_t reg = (uint8_t)((address >> 13u) & 0x03u);
        m->regs[reg] = m->shift & 0x1Fu;
        m->shift      = 0u;
        m->shift_count = 0u;
        mapper105_update_banks(nes);
    }
}

static void nes_mapper_cpu_clock(nes_t* nes, uint16_t cycles) {
    mapper105_t* m = (mapper105_t*)nes->nes_mapper.mapper_register;
    if (!m->irq_enabled) return;
    if (m->irq_counter <= cycles) {
        m->irq_counter = 0u;
        nes_cpu_irq(nes);
    } else {
        m->irq_counter -= cycles;
    }
}

int nes_mapper105_init(nes_t* nes) {
    nes->nes_mapper.mapper_init      = nes_mapper_init;
    nes->nes_mapper.mapper_deinit    = nes_mapper_deinit;
    nes->nes_mapper.mapper_write     = nes_mapper_write;
    nes->nes_mapper.mapper_cpu_clock = nes_mapper_cpu_clock;
    return NES_OK;
}
