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
 * Mapper 121 — "Panda Prince" / MK4 / MK6 / A9711-A9713 pirate board.
 *
 * An MMC3 with a protection latch: the program seeds the latch by writing $8003, and the board
 * answers reads of $5000-$5FFF with the value it was last given there (a {83 83 42 00} table
 * indexed by the low two bits of the data).  While the latch holds one of its accepted values
 * the board also takes over the upper three 8KB PRG slots ($A000/$C000/$E000) with three banks
 * it derived from the bit-reversed bank number — that is how these boards hide their bank
 * layout from copiers, and why a plain MMC3 leaves the game stuck with the wrong banks.
 * Board registers decode in $8000-$9FFF only (A0-A1 plus A13-A15), so the standard MMC3
 * registers at $A000/$C000/$E000 keep their usual meaning.
 *
 * sf97.nes (Street Fighter 97 multicart menu, 256KB PRG + 512KB CHR) declares mapper 187 in its
 * header and stays gray under any plain MMC3 variant, because it never gets past the protection
 * sequence.  Mesen's database (derived from NewRisingSun's NES 2.0 header database) lists this
 * ROM's CRC as mapper 121; FCEUX's src/boards/121.cpp notes that iNES mapper 187 uses the same
 * A98402 board.
 *
 * Reference: FCEUX src/boards/121.cpp (M121Write / M121LoWrite / M121Read / Sync / M121PW / M121CW).
 */

typedef struct {
    /* MMC3 */
    uint8_t bank_select;        /* $8000 */
    uint8_t bank_values[8];     /* $8001 */
    uint8_t mirroring;
    uint8_t irq_latch;
    uint8_t irq_counter;
    uint8_t irq_reload;
    uint8_t irq_enabled;
    uint16_t prg_bank_count;
    uint16_t chr_bank_count;    /* 1KB units: a 512KB CHR ROM needs 512, which does not fit in 8 bits */
    /* Protection latch — same layout as FCEUX's EXPREGS[0..7]. */
    uint8_t exp[8];             /* [0]=$E000 bank, [1]=$C000 bank, [2]=$A000 bank, [3]=outer bank,
                                   [4]=value read back from $5000-$5FFF, [5]=$8003 latch,
                                   [6]=bit-reversed $8001 value, [7]=latch update flag */
} mapper121_t;

/* Value handed to the program when it reads $5000-$5FFF (FCEUX prot_array). */
static const uint8_t mapper121_prot_array[4] = { 0x83u, 0x83u, 0x42u, 0x00u };

static void nes_mapper_deinit(nes_t* nes) {
    nes_free(nes->nes_mapper.mapper_register);
    nes->nes_mapper.mapper_register = NULL;
}

/* FCEUX reverses the low five bits of the $8001 value before the protection sees it. */
static uint8_t mapper121_reverse5(uint8_t v) {
    return (uint8_t)(((v & 0x01u) << 5) | ((v & 0x02u) << 3) | ((v & 0x04u) << 1) |
                     ((v & 0x08u) >> 1) | ((v & 0x10u) >> 3) | ((v & 0x20u) >> 5));
}

/* Protection state machine: only a few $8003 values are accepted, anything else disarms it. */
static void mapper121_sync(mapper121_t* m) {
    switch (m->exp[5] & 0x3Fu) {
    case 0x20u:
    case 0x29u:
    case 0x2Bu:
    case 0x3Cu:
    case 0x3Fu:
        m->exp[7] = 1u;
        m->exp[0] = m->exp[6];
        break;
    case 0x26u:
        m->exp[7] = 0u;
        m->exp[0] = m->exp[6];
        break;
    case 0x2Cu:
        m->exp[7] = 1u;
        if (m->exp[6] != 0u) m->exp[0] = m->exp[6];
        break;
    case 0x28u:
        m->exp[7] = 0u;
        m->exp[1] = m->exp[6];
        break;
    case 0x2Au:
        m->exp[7] = 0u;
        m->exp[2] = m->exp[6];
        break;
    case 0x2Fu:
        break;
    default:
        m->exp[5] = 0u;
        break;
    }
}

static void mapper121_update_banks(nes_t* nes) {
    mapper121_t* m = (mapper121_t*)nes->nes_mapper.mapper_register;
    const uint16_t outer = (uint16_t)((m->exp[3] & 0x80u) >> 2);   /* FCEUX: | 0x20 (outer 128KB) */
    uint8_t prg_mode = (uint8_t)((m->bank_select >> 6) & 1u);
    uint8_t chr_mode = (uint8_t)((m->bank_select >> 7) & 1u);
    uint16_t last  = (uint16_t)(m->prg_bank_count - 1u);
    uint16_t slast = (uint16_t)(m->prg_bank_count - 2u);
    uint16_t slot[4];

    /* MMC3 mapping, but the board only latches five bank bits (256KB of PRG). */
    if (prg_mode == 0u) {
        slot[0] = (uint16_t)(m->bank_values[6] & 0x1Fu);
        slot[1] = (uint16_t)(m->bank_values[7] & 0x1Fu);
        slot[2] = slast;
        slot[3] = last;
    } else {
        slot[0] = slast;
        slot[1] = (uint16_t)(m->bank_values[7] & 0x1Fu);
        slot[2] = (uint16_t)(m->bank_values[6] & 0x1Fu);
        slot[3] = last;
    }

    /* While the protection latch is armed it owns $A000/$C000/$E000. */
    if ((m->exp[5] & 0x3Fu) != 0u) {
        slot[1] = m->exp[2];
        slot[2] = m->exp[1];
        slot[3] = m->exp[0];
    }

    for (uint8_t i = 0; i < 4u; ++i) {
        (void)nes_load_prgrom_8k(nes, i, (uint16_t)(slot[i] | outer));
    }

    if (m->chr_bank_count == 0u) return;

    /* 1KB CHR pages: the half selected by the MMC3 CHR mode comes from the far 256KB. */
    const uint16_t chr_hi = (uint16_t)(((uint16_t)(m->bank_select & 0x80u)) << 5);  /* 0 or 0x1000 */
    uint8_t v[8];
    if (chr_mode == 0u) {
        v[0] = (uint8_t)(m->bank_values[0] & 0xFEu);
        v[1] = (uint8_t)(m->bank_values[0] | 0x01u);
        v[2] = (uint8_t)(m->bank_values[1] & 0xFEu);
        v[3] = (uint8_t)(m->bank_values[1] | 0x01u);
        v[4] = m->bank_values[2];
        v[5] = m->bank_values[3];
        v[6] = m->bank_values[4];
        v[7] = m->bank_values[5];
    } else {
        v[0] = m->bank_values[2];
        v[1] = m->bank_values[3];
        v[2] = m->bank_values[4];
        v[3] = m->bank_values[5];
        v[4] = (uint8_t)(m->bank_values[0] & 0xFEu);
        v[5] = (uint8_t)(m->bank_values[0] | 0x01u);
        v[6] = (uint8_t)(m->bank_values[1] & 0xFEu);
        v[7] = (uint8_t)(m->bank_values[1] | 0x01u);
    }
    for (uint8_t i = 0; i < 8u; ++i) {
        uint16_t bank = v[i];
        if (((uint16_t)((uint16_t)i * 0x400u) & 0x1000u) == chr_hi) {
            bank |= 0x100u;                     /* second 256KB of CHR (this dump has 512KB) */
        }
        (void)nes_load_chrrom_1k(nes, i, bank);
    }
}

static void nes_mapper_init(nes_t* nes) {
    if (nes->nes_mapper.mapper_register == NULL) {
        nes->nes_mapper.mapper_register = nes_mapper_register_alloc(nes, (uint16_t)sizeof(mapper121_t));
        if (nes->nes_mapper.mapper_register == NULL) return;
    }
    mapper121_t* m = (mapper121_t*)nes->nes_mapper.mapper_register;
    nes_memset(m, 0, sizeof(mapper121_t));

    m->prg_bank_count = (uint16_t)(nes->nes_rom.prg_rom_size * 2u);   /* 8KB units */
    m->chr_bank_count = (uint16_t)(nes->nes_rom.chr_rom_size * 8u);   /* 1KB units */
    m->exp[3] = 0x80u;                                               /* outer bank bit on at power-on */
    m->bank_values[6] = 0u;
    m->bank_values[7] = 1u;

    if (nes->nes_rom.chr_rom_size == 0u) nes_load_chrrom_8k(nes, 0, 0);
    mapper121_update_banks(nes);
}

static void nes_mapper_write(nes_t* nes, uint16_t address, uint8_t data) {
    mapper121_t* m = (mapper121_t*)nes->nes_mapper.mapper_register;

    /* Board registers decode in $8000-$9FFF only (A0-A1 plus A13-A15). */
    if ((address & 0xE000u) == 0x8000u) {
        switch (address & 0xE003u) {
        case 0x8000u:
            m->bank_select = data;
            mapper121_update_banks(nes);
            return;
        case 0x8003u:
            /* Doubles as an MMC3 bank-select write. */
            m->exp[5] = data;
            mapper121_sync(m);
            m->bank_select = data;
            mapper121_update_banks(nes);
            return;
        case 0x8001u:
            m->exp[6] = mapper121_reverse5(data);
            if (m->exp[7] == 0u) mapper121_sync(m);
            m->bank_values[m->bank_select & 0x07u] = data;
            mapper121_update_banks(nes);
            return;
        default:
            return;                                     /* $8002-$9FFF mirrors: ignored */
        }
    }

    switch (address & 0xE001u) {
    case 0xA000:
        m->mirroring = data & 1u;
        if (nes->nes_rom.four_screen == 0)
            nes_ppu_screen_mirrors(nes, m->mirroring ? NES_MIRROR_HORIZONTAL : NES_MIRROR_VERTICAL);
        break;
    case 0xC000: m->irq_latch   = data; break;
    case 0xC001: m->irq_reload  = 1; break;
    case 0xE000: m->irq_enabled = 0; nes->nes_cpu.irq_pending = 0; break;
    case 0xE001: m->irq_enabled = 1; break;
    default: break;
    }
}

/* $5000-$5FFF write: arm the value the program will read back.
 * The rest of $4020-$5FFF has to stay open bus: the core routes that whole range here, and a
 * board-detection read of, say, $4800 must not see the protection byte. */
static void nes_mapper_apu(nes_t* nes, uint16_t address, uint8_t data) {
    mapper121_t* m = (mapper121_t*)nes->nes_mapper.mapper_register;
    if (address < 0x5000u || address > 0x5FFFu) return;
    m->exp[4] = mapper121_prot_array[data & 0x03u];
}

/* $5000-$5FFF read: the protection answer. */
static uint8_t nes_mapper_read_apu(nes_t* nes, uint16_t address) {
    mapper121_t* m = (mapper121_t*)nes->nes_mapper.mapper_register;
    if (address < 0x5000u || address > 0x5FFFu) return 0u;
    return m->exp[4];
}

static void nes_mapper_hsync(nes_t* nes) {
    mapper121_t* m = (mapper121_t*)nes->nes_mapper.mapper_register;
    if (nes->nes_ppu.MASK_b == 0 && nes->nes_ppu.MASK_s == 0) return;
    if (m->irq_counter == 0u || m->irq_reload) {
        m->irq_counter = m->irq_latch;
    } else {
        m->irq_counter--;
    }
    if (m->irq_counter == 0u && m->irq_enabled) nes_cpu_irq(nes);
    m->irq_reload = 0;
}

int nes_mapper121_init(nes_t* nes) {
    nes->nes_mapper.mapper_init     = nes_mapper_init;
    nes->nes_mapper.mapper_deinit   = nes_mapper_deinit;
    nes->nes_mapper.mapper_write    = nes_mapper_write;
    nes->nes_mapper.mapper_apu      = nes_mapper_apu;
    nes->nes_mapper.mapper_read_apu = nes_mapper_read_apu;
    nes->nes_mapper.mapper_hsync    = nes_mapper_hsync;
    return NES_OK;
}
