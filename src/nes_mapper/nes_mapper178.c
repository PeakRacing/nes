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
 * Mapper 178 - Waixing (三国忠烈传 / 宠物大家族 / 大航海7 ...).
 * Authority: Mesen2 Core/NES/Mappers/Waixing/Waixing178.h.
 *
 * Four registers live at $4800-$4FFF (selected by addr & 3), the PRG page size is 16KB and
 * CHR is one fixed 8KB page:
 *
 *   regs[0] bit 0 : mirroring (1 = horizontal, 0 = vertical)
 *           bit 1 : 32KB mode - slot 0 takes the small bank, slot 1 the fixed one
 *           bit 2 : in that mode slot 1 becomes (bbank << 3) | 6 | regs[1] bit 0,
 *                   otherwise both halves show the same bank
 *   regs[1] bits 0-2 : small PRG bank (sbank; bit 0 is also reused by the 32KB mode)
 *   regs[2]          : big PRG bank (bbank, shifted left by 3)
 *   regs[3] bits 0-1 : WRAM bank at $6000-$7FFF - the board has 32KB of work RAM
 *
 * Power-on runs the same path with every register zero: 32KB starting at page 0.
 *
 * The old implementation was a different board (four independent 8KB bank registers written
 * at $6000-$6003), so the Waixing games never left their power-on mapping.
 */

typedef struct {
    uint8_t  regs[4];
    uint16_t prg_bank_count;    /* 16KB units */
    uint8_t* wram;              /* 32KB board RAM, 4 x 8KB banks */
} mapper178_t;

#define MAPPER178_WRAM_SIZE (0x8000u)

static void nes_mapper_deinit(nes_t* nes) {
    mapper178_t* m = (mapper178_t*)nes->nes_mapper.mapper_register;
    if (m != NULL && m->wram != NULL) {
        nes_free(m->wram);
        m->wram = NULL;
    }
    nes_free(nes->nes_mapper.mapper_register);
    nes->nes_mapper.mapper_register = NULL;
}

static void mapper178_update(nes_t* nes) {
    mapper178_t* m = (mapper178_t*)nes->nes_mapper.mapper_register;
    const uint16_t sbank = (uint16_t)(m->regs[1] & 0x07u);
    const uint16_t bbank = m->regs[2];

    if (m->regs[0] & 0x02u) {
        nes_load_prgrom_16k(nes, 0, (uint16_t)((bbank << 3) | sbank));
        if (m->regs[0] & 0x04u) {
            nes_load_prgrom_16k(nes, 1, (uint16_t)((bbank << 3) | 0x06u | (m->regs[1] & 0x01u)));
        } else {
            nes_load_prgrom_16k(nes, 1, (uint16_t)((bbank << 3) | 0x07u));
        }
    } else {
        const uint16_t bank = (uint16_t)((bbank << 3) | sbank);
        if (m->regs[0] & 0x04u) {
            nes_load_prgrom_16k(nes, 0, bank);
            nes_load_prgrom_16k(nes, 1, bank);
        } else {
            /* SelectPrgPage2x(0, bank): two consecutive 16KB pages. */
            nes_load_prgrom_16k(nes, 0, bank);
            nes_load_prgrom_16k(nes, 1, (uint16_t)(bank + 1u));
        }
    }

    if (nes->nes_rom.four_screen == 0) {
        nes_ppu_screen_mirrors(nes, (m->regs[0] & 0x01u) ? NES_MIRROR_HORIZONTAL : NES_MIRROR_VERTICAL);
    }
}

static void nes_mapper_init(nes_t* nes) {
    if (nes->nes_mapper.mapper_register == NULL) {
        nes->nes_mapper.mapper_register = nes_mapper_register_alloc(nes, (uint16_t)sizeof(mapper178_t));
        if (nes->nes_mapper.mapper_register == NULL) return;
    }
    mapper178_t* m = (mapper178_t*)nes->nes_mapper.mapper_register;
    nes_memset(m, 0, sizeof(mapper178_t));

    m->prg_bank_count = (uint16_t)(nes->nes_rom.prg_rom_size);

    m->wram = (uint8_t*)nes_malloc(MAPPER178_WRAM_SIZE);
    if (m->wram != NULL) {
        nes_memset(m->wram, 0, MAPPER178_WRAM_SIZE);
    }

    nes_load_chrrom_8k(nes, 0, 0);
    mapper178_update(nes);
}

/* $4800-$4FFF: regs[(addr & 3)] = value (the core routes $4020-$5FFF here). */
static void nes_mapper_apu(nes_t* nes, uint16_t address, uint8_t data) {
    mapper178_t* m = (mapper178_t*)nes->nes_mapper.mapper_register;
    if (address < 0x4800u || address > 0x4FFFu) return;
    m->regs[address & 0x03u] = data;
    mapper178_update(nes);
}

static uint8_t mapper178_wram_bank(mapper178_t* m) {
    return (uint8_t)(m->regs[3] & 0x03u);
}

static uint8_t nes_mapper_read_sram(nes_t* nes, uint16_t address) {
    mapper178_t* m = (mapper178_t*)nes->nes_mapper.mapper_register;
    if (m->wram == NULL) return 0;
    return m->wram[(uint32_t)mapper178_wram_bank(m) * 8192u + (address & 0x1FFFu)];
}

static void nes_mapper_sram(nes_t* nes, uint16_t address, uint8_t data) {
    mapper178_t* m = (mapper178_t*)nes->nes_mapper.mapper_register;
    if (m->wram == NULL) return;
    m->wram[(uint32_t)mapper178_wram_bank(m) * 8192u + (address & 0x1FFFu)] = data;
}

static void nes_mapper_state_reapply(nes_t* nes) {
    mapper178_update(nes);
}

int nes_mapper178_init(nes_t* nes) {
    nes->nes_mapper.mapper_init          = nes_mapper_init;
    nes->nes_mapper.mapper_deinit        = nes_mapper_deinit;
    nes->nes_mapper.mapper_apu           = nes_mapper_apu;
    nes->nes_mapper.mapper_sram          = nes_mapper_sram;
    nes->nes_mapper.mapper_read_sram     = nes_mapper_read_sram;
    nes->nes_mapper.mapper_state_reapply = nes_mapper_state_reapply;
    return NES_OK;
}
