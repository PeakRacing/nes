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

/* https://www.nesdev.org/wiki/INES_Mapper_064 — Tengen RAMBO-1 */

typedef struct {
    uint8_t cmd;            /* $8000 bank select register */
    uint8_t regs[16];       /* R0-R9 CHR/PRG windows, R15 = PRG mode bank */
    uint8_t irq_mode;       /* 0: scanline mode, 1: CPU-cycle mode */
    uint8_t irq_enable;
    uint8_t irq_counter;
    uint8_t irq_latch;
    uint8_t need_reload;     /* $C001 asked for a reload on the next tick */
    uint8_t irq_delay;       /* IRQ assertion delay counter (Mesen CpuIrqDelay/PpuIrqDelay) */
    uint8_t force_clock;     /* $C001 switch back to scanline mode needs one extra tick */
    uint16_t irq_cycle_accum;
} nes_mapper64_t;

static void nes_mapper_deinit(nes_t* nes) {
    nes_free(nes->nes_mapper.mapper_register);
    nes->nes_mapper.mapper_register = NULL;
}

static void mapper64_update_banks(nes_t* nes) {
    nes_mapper64_t* m = (nes_mapper64_t*)nes->nes_mapper.mapper_register;
    const uint8_t prg_count = (uint8_t)(nes->nes_rom.prg_rom_size * 2u);

    /* PRG layout selected by cmd bit 6 (Mesen Rambo1::UpdateState):
     *   set   : $8000 = R15, $A000 = R6, $C000 = R7
     *   clear : $8000 = R6,  $A000 = R7, $C000 = R15     ($E000 always the last bank) */
    if (m->cmd & 0x40u) {
        nes_load_prgrom_8k(nes, 0, m->regs[15] % prg_count);
        nes_load_prgrom_8k(nes, 1, m->regs[6] % prg_count);
        nes_load_prgrom_8k(nes, 2, m->regs[7] % prg_count);
    } else {
        nes_load_prgrom_8k(nes, 0, m->regs[6] % prg_count);
        nes_load_prgrom_8k(nes, 1, m->regs[7] % prg_count);
        nes_load_prgrom_8k(nes, 2, m->regs[15] % prg_count);
    }
    nes_load_prgrom_8k(nes, 3, (uint16_t)(prg_count - 1));

    if (nes->nes_rom.chr_rom_size == 0) return;
    const uint8_t chr_count = (uint8_t)(nes->nes_rom.chr_rom_size * 8u);

    /*
     * RAMBO-1 CHR layout (Mesen Rambo1::UpdateState):
     *   cmd bit 7 = A12 inversion: swaps the $0000 and $1000 halves (XOR 4 on the 1KB slot)
     *   R0/R1/R2/R3/R4/R5 are always 1KB pages at slots 0/2/4/5/6/7
     *   cmd bit 5 = 1: slots 1/3 take R8/R9 (1KB)
     *   cmd bit 5 = 0: slots 1/3 take R0+1/R1+1 (the second half of the 2KB pair)
     */
    {
        const uint8_t inv = (m->cmd & 0x80u) ? 4u : 0u;
        nes_load_chrrom_1k(nes, (uint8_t)(0u ^ inv), m->regs[0] % chr_count);
        nes_load_chrrom_1k(nes, (uint8_t)(2u ^ inv), m->regs[1] % chr_count);
        nes_load_chrrom_1k(nes, (uint8_t)(4u ^ inv), m->regs[2] % chr_count);
        nes_load_chrrom_1k(nes, (uint8_t)(5u ^ inv), m->regs[3] % chr_count);
        nes_load_chrrom_1k(nes, (uint8_t)(6u ^ inv), m->regs[4] % chr_count);
        nes_load_chrrom_1k(nes, (uint8_t)(7u ^ inv), m->regs[5] % chr_count);
        if (m->cmd & 0x20u) {
            nes_load_chrrom_1k(nes, (uint8_t)(1u ^ inv), m->regs[8] % chr_count);
            nes_load_chrrom_1k(nes, (uint8_t)(3u ^ inv), m->regs[9] % chr_count);
        } else {
            nes_load_chrrom_1k(nes, (uint8_t)(1u ^ inv), (uint8_t)((m->regs[0] + 1u) % chr_count));
            nes_load_chrrom_1k(nes, (uint8_t)(3u ^ inv), (uint8_t)((m->regs[1] + 1u) % chr_count));
        }
    }
}
static void nes_mapper_init(nes_t* nes) {
    if (nes->nes_mapper.mapper_register == NULL) {
        nes->nes_mapper.mapper_register = nes_mapper_register_alloc(nes, (uint16_t)sizeof(nes_mapper64_t));
        if (nes->nes_mapper.mapper_register == NULL) return;
    }
    nes_mapper64_t* m = (nes_mapper64_t*)nes->nes_mapper.mapper_register;
    nes_memset(m, 0, sizeof(nes_mapper64_t));
    nes_memset(m->regs, 0xFF, sizeof(m->regs));

    mapper64_update_banks(nes);

    if (nes->nes_rom.mirroring_type) {
        nes_ppu_screen_mirrors(nes, NES_MIRROR_VERTICAL);
    } else {
        nes_ppu_screen_mirrors(nes, NES_MIRROR_HORIZONTAL);
    }
}

/*
 * $8000 (even): Bank select
 *   bits[3:0]: register index (0-9, F used)
 *   bit[5]:    CHR mode (0: two 2KB banks at $0000/$0800, 1: four 1KB banks)
 * $8001 (odd):  Bank data written to register indexed by last $8000 write
 * $A000 (even): Mirroring — bit[0]: 0=V, 1=H
 * $C000 (even): IRQ latch (8-bit)
 * $C001 (odd):  IRQ reload and mode select (bit 0: CPU-cycle mode)
 * $E000 (even): IRQ disable + acknowledge
 * $E001 (odd):  IRQ enable
 */
static void nes_mapper_write(nes_t* nes, uint16_t address, uint8_t data) {
    nes_mapper64_t* m = (nes_mapper64_t*)nes->nes_mapper.mapper_register;
    switch (address & 0xF001u) {
    case 0x8000:
        m->cmd = data;
        break;
    case 0x8001: {
        const uint8_t reg = m->cmd & 0x0Fu;   /* Mesen: _registers[cmd & 0x0F] = value */
        m->regs[reg] = data;
        mapper64_update_banks(nes);
        break;
    }
    case 0xA000:
        nes_ppu_screen_mirrors(nes, (data & 0x1u) ? NES_MIRROR_HORIZONTAL : NES_MIRROR_VERTICAL);
        break;
    case 0xC000:
        m->irq_latch = data;                  /* only the reload value; no counter reload */
        break;
    case 0xC001:
        if (m->irq_mode && (data & 0x01u) == 0u) {
            m->force_clock = 1;               /* Mesen: fixes Skull & Crossbones */
        }
        m->irq_mode = data & 0x01u;
        m->need_reload = 1;
        break;
    case 0xE000:
        m->irq_enable = 0;
        nes->nes_cpu.irq_pending = 0;
        break;
    case 0xE001:
        m->irq_enable = 1;
        break;
    default:
        break;
    }
}

/* Counter semantics copied from Mesen Rambo1::ClockIrqCounter (including the reload quirk
 * that fixes Hard Drivin': reload <= 1 loads reload+1, otherwise reload+2). */
static void mapper64_clock_irq(nes_t* nes, uint8_t delay) {
    nes_mapper64_t* m = (nes_mapper64_t*)nes->nes_mapper.mapper_register;
    if (m->need_reload) {
        m->irq_counter = (uint8_t)((m->irq_latch <= 1u) ? (m->irq_latch + 1u) : (m->irq_latch + 2u));
        m->need_reload = 0;
    } else if (m->irq_counter == 0u) {
        m->irq_counter = (uint8_t)(m->irq_latch + 1u);
    }
    m->irq_counter--;
    if (m->irq_counter == 0u && m->irq_enable) {
        m->irq_delay = delay;
    }
}
static void nes_mapper_cpu_clock(nes_t* nes, uint16_t cycles) {
    nes_mapper64_t* m = (nes_mapper64_t*)nes->nes_mapper.mapper_register;
    if (m->irq_delay != 0u) {
        if (--m->irq_delay == 0u) {
            nes_cpu_irq(nes);
        }
    }
    if (m->irq_mode || m->force_clock) {
        m->irq_cycle_accum = (uint16_t)(m->irq_cycle_accum + cycles);
        while (m->irq_cycle_accum >= 4u) {
            m->irq_cycle_accum = (uint16_t)(m->irq_cycle_accum - 4u);
            mapper64_clock_irq(nes, 1u);      /* Mesen CpuIrqDelay */
            m->force_clock = 0;
        }
    }
}
static void nes_mapper_hsync(nes_t* nes) {
    nes_mapper64_t* m = (nes_mapper64_t*)nes->nes_mapper.mapper_register;
    if (m->irq_delay != 0u) {
        if (--m->irq_delay == 0u) {
            nes_cpu_irq(nes);
        }
    }
    if (m->irq_mode) return;
    mapper64_clock_irq(nes, 2u);              /* Mesen PpuIrqDelay, A12 rises approximated by hsync */
}
int nes_mapper64_init(nes_t* nes) {
    nes->nes_mapper.mapper_init   = nes_mapper_init;
    nes->nes_mapper.mapper_deinit = nes_mapper_deinit;
    nes->nes_mapper.mapper_write  = nes_mapper_write;
    nes->nes_mapper.mapper_hsync  = nes_mapper_hsync;
    nes->nes_mapper.mapper_cpu_clock = nes_mapper_cpu_clock;
    return NES_OK;
}
