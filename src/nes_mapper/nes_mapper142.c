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
 * Mapper 142 - Kaiser 202 (金牌马里奥2 等; the same chip is mapper 56).
 * Authority: Mesen2 Core/NES/Mappers/Kaiser/Kaiser202.h.
 *
 * PRG pages are 8KB (page 3 is hardwired to the last page), CHR pages are 1KB.
 * Registers are selected by the *address*:
 *   $8000 / $9000 / $A000 / $B000 : IRQ reload value, one nibble each (bits 0-3 / 4-7 / 8-11 / 12-15)
 *   $C000                         : IRQ control - bit1 enables the counter and reloads it,
 *                                   any write acknowledges the pending IRQ
 *   $D000                         : acknowledge the pending IRQ
 *   $E000                         : register select = (value & 7) - 1 (0 means "none")
 *   $F000                         : write the selected register
 *                                     select 0..3 -> PRG bank 0..2 low nibble (bits 0-3)
 *                                     select 4    -> bit2 switches $6000-$7FFF to PRG-ROM
 *
 * The IRQ counter counts CPU clocks and, on reaching $FFFF, reloads itself, clears its
 * enable bit and asserts the CPU IRQ line (mirroring Mesen's ProcessCpuClock).
 *
 * $6000-$7FFF is 8KB of work RAM unless "use ROM" is set, in which case it shows
 * the PRG page held in bank register 3.  Like every other board with an on-cartridge
 * RAM window, the RAM is allocated unconditionally - not only for battery carts.
 */

typedef struct {
    uint16_t irq_reload;
    uint16_t irq_counter;
    uint8_t  irq_control;
    uint8_t  selected;         /* register select - 1 (0xFF = none) */
    uint8_t  prg[4];
    uint8_t  use_rom;
} mapper142_t;

static void mapper142_apply(nes_t* nes) {
    mapper142_t* m = (mapper142_t*)nes->nes_mapper.mapper_register;
    const uint16_t total_8k = (uint16_t)(nes->nes_rom.prg_rom_size * 2u);
    const uint16_t last_8k = (total_8k != 0u) ? (uint16_t)(total_8k - 1u) : 0u;

    nes_load_prgrom_8k(nes, 0, m->prg[0]);
    nes_load_prgrom_8k(nes, 1, m->prg[1]);
    nes_load_prgrom_8k(nes, 2, m->prg[2]);
    nes_load_prgrom_8k(nes, 3, last_8k);
}

static void nes_mapper_init(nes_t* nes) {
    if (nes->nes_mapper.mapper_register == NULL) {
        nes->nes_mapper.mapper_register = nes_mapper_register_alloc(nes, (uint16_t)sizeof(mapper142_t));
        if (nes->nes_mapper.mapper_register == NULL) return;
    }
    mapper142_t* m = (mapper142_t*)nes->nes_mapper.mapper_register;
    nes_memset(m, 0, sizeof(mapper142_t));
    m->selected = 0xFFu;

    if (nes->nes_rom.sram == NULL) {
        nes->nes_rom.sram = (uint8_t*)nes_malloc(SRAM_SIZE);
        if (nes->nes_rom.sram != NULL) {
            nes_memset(nes->nes_rom.sram, 0, SRAM_SIZE);
        }
    }
    nes_load_chrrom_8k(nes, 0, 0);
    mapper142_apply(nes);
}

static void nes_mapper_write(nes_t* nes, uint16_t address, uint8_t data) {
    mapper142_t* m = (mapper142_t*)nes->nes_mapper.mapper_register;

    switch (address & 0xF000u) {
    case 0x8000:
        m->irq_reload = (uint16_t)((m->irq_reload & 0xFFF0u) | (data & 0x0Fu));
        break;
    case 0x9000:
        m->irq_reload = (uint16_t)((m->irq_reload & 0xFF0Fu) | ((uint16_t)(data & 0x0Fu) << 4));
        break;
    case 0xA000:
        m->irq_reload = (uint16_t)((m->irq_reload & 0xF0FFu) | ((uint16_t)(data & 0x0Fu) << 8));
        break;
    case 0xB000:
        m->irq_reload = (uint16_t)((m->irq_reload & 0x0FFFu) | ((uint16_t)(data & 0x0Fu) << 12));
        break;
    case 0xC000:
        m->irq_control = data;
        if (m->irq_control & 0x02u) {
            m->irq_counter = m->irq_reload;
        }
        nes->nes_cpu.irq_pending = 0;
        break;
    case 0xD000:
        nes->nes_cpu.irq_pending = 0;
        break;
    case 0xE000:
        m->selected = (uint8_t)((data & 0x07u) - 1u);   /* 0 -> 0xFF ("no register") */
        break;
    case 0xF000:
        switch (m->selected) {
        case 0: case 1: case 2: case 3:
            m->prg[m->selected] = (uint8_t)((m->prg[m->selected] & 0x10u) | (data & 0x0Fu));
            break;
        case 4:
            m->use_rom = (uint8_t)(data & 0x04u);
            break;
        default:
            break;
        }
        mapper142_apply(nes);      /* bank 3 also feeds the $6000 window through read_sram */
        break;
    default:
        break;
    }
}

static uint8_t nes_mapper_read_sram(nes_t* nes, uint16_t address) {
    mapper142_t* m = (mapper142_t*)nes->nes_mapper.mapper_register;
    if (!m->use_rom) {
        if (nes->nes_rom.sram != NULL) {
            return nes->nes_rom.sram[address & 0x1FFFu];
        }
        return 0;
    }
#if (NES_ROM_STREAM == 1)
    return 0;                                          /* see nes_mapper51.c's note */
#else
    if (nes->nes_rom.prg_rom == NULL) return 0;
    {
        const uint16_t total_8k = (uint16_t)(nes->nes_rom.prg_rom_size * 2u);
        uint16_t page = (uint16_t)(m->prg[3] & 0x1Fu);
        if (total_8k != 0u && page >= total_8k) page = (uint16_t)(page % total_8k);
        return nes->nes_rom.prg_rom[(uint32_t)page * 8192u + (uint16_t)(address & 0x1FFFu)];
    }
#endif
}

/* 16-bit counter advanced by CPU clocks; on $FFFF it reloads, disables itself and fires. */
static void nes_mapper_cpu_clock(nes_t* nes, uint16_t cycles) {
    mapper142_t* m = (mapper142_t*)nes->nes_mapper.mapper_register;
    uint32_t next;
    if ((m->irq_control & 0x02u) == 0u || cycles == 0u) return;
    next = (uint32_t)m->irq_counter + (uint32_t)cycles;
    if (next >= 0xFFFFu) {
        m->irq_counter = m->irq_reload;
        m->irq_control = (uint8_t)(m->irq_control & 0xFDu);
        nes_cpu_irq(nes);
    } else {
        m->irq_counter = (uint16_t)next;
    }
}

static void nes_mapper_state_reapply(nes_t* nes) {
    mapper142_apply(nes);
}

int nes_mapper142_init(nes_t* nes) {
    nes->nes_mapper.mapper_init          = nes_mapper_init;
    nes->nes_mapper.mapper_write         = nes_mapper_write;
    nes->nes_mapper.mapper_read_sram     = nes_mapper_read_sram;
    nes->nes_mapper.mapper_cpu_clock     = nes_mapper_cpu_clock;
    nes->nes_mapper.mapper_state_reapply = nes_mapper_state_reapply;
    return NES_OK;
}
