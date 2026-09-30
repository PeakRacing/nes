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
 * Mapper 150 - Sachen 74LS374N (台湾花牌麻雀 / 美女拳 / 数学学园 ...).
 * Authority: Mesen2 Core/NES/Mappers/Sachen/Sachen74LS374N.h.
 *
 * The board has **no** data registers in the usual cartridge space: every address whose
 * bits 15/14/8/0 match $4100 (i.e. `addr & 0xC101 == 0x4100`) selects a register, and the
 * matching +1 address writes it:
 *
 *   write (addr & 0xC101) == 0x4100 : current register = value & 7
 *   write (addr & 0xC101) == 0x4101 : regs[current] = value & 7, then re-map
 *   read  (addr & 0xC101) == 0x4101 : open bus in the upper bits + regs[current] & 7
 *
 * Mapping (the 74LS374N wiring, i.e. Mesen's `MapperID == 150` case):
 *   PRG : one 32KB page = regs[5] & 0x03
 *   CHR : one 8KB page  = ((regs[4] & 0x01) << 2) | (regs[6] & 0x03)
 *   NT  : (regs[7] >> 1) & 0x03 -> 0 = screen A for NT0-2 + screen B for NT3,
 *                                 1 = horizontal, 2 = vertical, 3 = screen A only
 *
 * Mesen additionally models a 1-bit DIP switch that ORs $04 into every register write
 * and frees D2 on reads.  There is no DIP switch plumbing in this core, so the
 * documented power-on setting (bit clear) is what gets implemented here.
 *
 * The old implementation decoded a completely different board (registers written to
 * $6000-$7FFF, 8KB PRG pages, 1KB CHR pairs), so games that program the board at
 * $4100/$4101 kept running on the power-on banks and stayed blank.
 */

typedef struct {
    uint8_t regs[8];
    uint8_t current;
} mapper150_t;

static void mapper150_update(nes_t* nes) {
    mapper150_t* m = (mapper150_t*)nes->nes_mapper.mapper_register;
    const uint8_t chr_page = (uint8_t)(((m->regs[4] & 0x01u) << 2) | (m->regs[6] & 0x03u));

    /* PRG is a single 32KB page; the board only sees 2 bits of regs[5]. */
    nes_load_prgrom_32k(nes, 0, (uint16_t)(m->regs[5] & 0x03u));

    if (nes->nes_rom.chr_rom_size > 0) {
        nes_load_chrrom_8k(nes, 0, chr_page);
    }

    if (nes->nes_rom.four_screen == 0) {
        switch ((m->regs[7] >> 1) & 0x03u) {
        case 0:
            /* Sachen 8259 arrangement: three slots on screen A, the fourth on screen B. */
            nes->nes_ppu.name_table[0] = nes->nes_ppu.ppu_vram[0];
            nes->nes_ppu.name_table[1] = nes->nes_ppu.ppu_vram[0];
            nes->nes_ppu.name_table[2] = nes->nes_ppu.ppu_vram[0];
            nes->nes_ppu.name_table[3] = nes->nes_ppu.ppu_vram[1];
            nes->nes_ppu.name_table_mirrors[0] = nes->nes_ppu.name_table[0];
            nes->nes_ppu.name_table_mirrors[1] = nes->nes_ppu.name_table[1];
            nes->nes_ppu.name_table_mirrors[2] = nes->nes_ppu.name_table[2];
            nes->nes_ppu.name_table_mirrors[3] = nes->nes_ppu.name_table[3];
            break;
        case 1: nes_ppu_screen_mirrors(nes, NES_MIRROR_HORIZONTAL);   break;
        case 2: nes_ppu_screen_mirrors(nes, NES_MIRROR_VERTICAL);     break;
        default: nes_ppu_screen_mirrors(nes, NES_MIRROR_ONE_SCREEN0); break;
        }
    }
}

static void nes_mapper_init(nes_t* nes) {
    if (nes->nes_mapper.mapper_register == NULL) {
        nes->nes_mapper.mapper_register = nes_mapper_register_alloc(nes, (uint16_t)sizeof(mapper150_t));
        if (nes->nes_mapper.mapper_register == NULL) return;
    }
    mapper150_t* m = (mapper150_t*)nes->nes_mapper.mapper_register;
    nes_memset(m, 0, sizeof(mapper150_t));

    if (nes->nes_rom.chr_rom_size == 0) nes_load_chrrom_8k(nes, 0, 0);
    mapper150_update(nes);
}

static void mapper150_write(nes_t* nes, uint16_t address, uint8_t data) {
    mapper150_t* m = (mapper150_t*)nes->nes_mapper.mapper_register;
    switch (address & 0xC101u) {
    case 0x4100u:
        m->current = (uint8_t)(data & 0x07u);
        break;
    case 0x4101u:
        m->regs[m->current] = (uint8_t)(data & 0x07u);
        mapper150_update(nes);
        break;
    default:
        break;
    }
}

/* $4020-$5FFF writes reach mapper_apu and $6000-$7FFF writes reach mapper_sram: the board
   decodes both windows, so both get the same handler. */
static void nes_mapper_apu(nes_t* nes, uint16_t address, uint8_t data) {
    mapper150_write(nes, address, data);
}

static void nes_mapper_sram(nes_t* nes, uint16_t address, uint8_t data) {
    mapper150_write(nes, address, data);
}

static uint8_t ns_mapper150_read(nes_t* nes, uint16_t address) {
    mapper150_t* m = (mapper150_t*)nes->nes_mapper.mapper_register;
    if ((address & 0xC101u) == 0x4101u) {
        /* Upper bits are open bus on the real board; keep them high like the reference. */
        return (uint8_t)(0xF8u | (m->regs[m->current] & 0x07u));
    }
    return 0;
}

static uint8_t nes_mapper_read_apu(nes_t* nes, uint16_t address) {
    return ns_mapper150_read(nes, address);
}

static uint8_t nes_mapper_read_sram(nes_t* nes, uint16_t address) {
    return ns_mapper150_read(nes, address);
}

static void nes_mapper_state_reapply(nes_t* nes) {
    mapper150_update(nes);
}

int nes_mapper150_init(nes_t* nes) {
    nes->nes_mapper.mapper_init          = nes_mapper_init;
    nes->nes_mapper.mapper_apu           = nes_mapper_apu;
    nes->nes_mapper.mapper_sram          = nes_mapper_sram;
    nes->nes_mapper.mapper_read_apu      = nes_mapper_read_apu;
    nes->nes_mapper.mapper_read_sram     = nes_mapper_read_sram;
    nes->nes_mapper.mapper_state_reapply = nes_mapper_state_reapply;
    return NES_OK;
}
