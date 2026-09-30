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
 * Mapper 147 - Sachen SA-72008 with the TXC "JV001" protection chip (少林武者 ...).
 * Authority: Mesen2 Core/NES/Mappers/Sachen/Sachen_147.h plus
 *            Core/NES/Mappers/Txc/TxcChip.h (constructed as TxcChip(true), the JV001 variant).
 *
 * The board is a two-page mapper (PRG 32KB, CHR 8KB) whose page numbers come out of a 4-bit
 * scrambler instead of plain registers:
 *
 *   TxcChip state: accumulator / inverter / staging / output / increase / invert (mask 0x0F)
 *     $4100 write : increase ? accumulator++ : accumulator = ((acc & 0xF0) | (staging & 0x0F))
 *                                                             ^ (invert ? 0xFF : 0)
 *     $4101 write : invert   = bit 0
 *     $4102 write : staging  = value & 0x0F ; inverter = value & 0xF0
 *     $4103 write : increase = bit 0
 *     $8000+ write: output   = (accumulator & 0x0F) | (inverter & 0xF0)      [JV001]
 *     read        : (accumulator & 0x0F) | ((inverter ^ (invert ? 0xFF : 0)) & 0xF0)
 *   All of those decode through `addr & 0xE103`, so the window answers at $4100-$4103 and at
 *   $C100-$C103 alike - which is why both the APU hook and the $8000+ hook must feed it.
 *
 * The mapper scrambles the value before handing it to the chip and derives the pages as:
 *   write  : txc_write(addr, ((value & 0xFC) >> 2) | ((value & 0x03) << 6))
 *   update : out = output ; PRG page = ((out & 0x20) >> 4) | (out & 0x01) ;
 *            CHR page = (out & 0x1E) >> 1
 *   read   : (addr & 0x103) == 0x100 -> ((v & 0x3F) << 2) | ((v & 0xC0) >> 6) from txc.Read()
 *   Power-on: PRG page 0, CHR page 0; PRG/CHR are refreshed on writes at $8000+ and on reads.
 *
 * The old implementation was a bespoke register file wired only to the $4020-$5FFF window,
 * so every write the game made above $8000 was silently dropped.
 */

typedef struct {
    uint8_t accumulator;
    uint8_t inverter;
    uint8_t staging;
    uint8_t output;
    uint8_t increase;
    uint8_t invert;
} mapper147_t;

static void nes_mapper_deinit(nes_t* nes) {
    nes_free(nes->nes_mapper.mapper_register);
    nes->nes_mapper.mapper_register = NULL;
}

static void mapper147_update_state(nes_t* nes) {
    mapper147_t* m = (mapper147_t*)nes->nes_mapper.mapper_register;
    const uint8_t out = m->output;
    nes_load_prgrom_32k(nes, 0, (uint16_t)(((out & 0x20u) >> 4) | (out & 0x01u)));
    nes_load_chrrom_8k(nes, 0, (uint8_t)((out & 0x1Eu) >> 1));
}

/* The TXC chip's own register file ($4100-$4103, mirrored at $C100-$C103). */
static void mapper147_txc_write(mapper147_t* m, uint16_t address, uint8_t value) {
    if (address < 0x8000u) {
        switch (address & 0xE103u) {
        case 0x4100u:
            if (m->increase) {
                m->accumulator++;
            } else {
                m->accumulator = (uint8_t)(((m->accumulator & 0xF0u) | (m->staging & 0x0Fu))
                                           ^ (m->invert ? 0xFFu : 0x00u));
            }
            break;
        case 0x4101u:
            m->invert = (uint8_t)((value & 0x01u) != 0u);
            break;
        case 0x4102u:
            m->staging = (uint8_t)(value & 0x0Fu);
            m->inverter = (uint8_t)(value & 0xF0u);
            break;
        case 0x4103u:
            m->increase = (uint8_t)((value & 0x01u) != 0u);
            break;
        default:
            break;
        }
    } else {
        m->output = (uint8_t)((m->accumulator & 0x0Fu) | (m->inverter & 0xF0u));
    }
}

static void mapper147_register_write(nes_t* nes, uint16_t address, uint8_t data) {
    mapper147_t* m = (mapper147_t*)nes->nes_mapper.mapper_register;
    mapper147_txc_write(m, address, (uint8_t)(((data & 0xFCu) >> 2) | ((data & 0x03u) << 6)));
    if (address >= 0x8000u) {
        mapper147_update_state(nes);
    }
}

static void nes_mapper_write(nes_t* nes, uint16_t address, uint8_t data) {
    mapper147_register_write(nes, address, data);
}

static void nes_mapper_apu(nes_t* nes, uint16_t address, uint8_t data) {
    mapper147_register_write(nes, address, data);
}

static uint8_t nes_mapper_read_apu(nes_t* nes, uint16_t address) {
    mapper147_t* m = (mapper147_t*)nes->nes_mapper.mapper_register;
    uint8_t value = 0;

    if ((address & 0x0103u) == 0x0100u) {
        const uint8_t v = (uint8_t)((m->accumulator & 0x0Fu) |
                                    ((m->inverter ^ (m->invert ? 0xFFu : 0x00u)) & 0xF0u));
        value = (uint8_t)(((v & 0x3Fu) << 2) | ((v & 0xC0u) >> 6));
    }
    mapper147_update_state(nes);
    return value;
}

static void nes_mapper_init(nes_t* nes) {
    if (nes->nes_mapper.mapper_register == NULL) {
        nes->nes_mapper.mapper_register = nes_mapper_register_alloc(nes, (uint16_t)sizeof(mapper147_t));
        if (nes->nes_mapper.mapper_register == NULL) return;
    }
    mapper147_t* m = (mapper147_t*)nes->nes_mapper.mapper_register;
    nes_memset(m, 0, sizeof(mapper147_t));

    m->invert = 1u;                      /* TxcChip(true) powers on inverted */
    mapper147_update_state(nes);
}

static void nes_mapper_state_reapply(nes_t* nes) {
    mapper147_update_state(nes);
}

int nes_mapper147_init(nes_t* nes) {
    nes->nes_mapper.mapper_init          = nes_mapper_init;
    nes->nes_mapper.mapper_deinit        = nes_mapper_deinit;
    nes->nes_mapper.mapper_write         = nes_mapper_write;
    nes->nes_mapper.mapper_apu           = nes_mapper_apu;
    nes->nes_mapper.mapper_read_apu      = nes_mapper_read_apu;
    nes->nes_mapper.mapper_state_reapply = nes_mapper_state_reapply;
    return NES_OK;
}
