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
 * Mapper 133 - Sachen SA-72007 (迷魂车 / 存储王 ...).
 * Authority: Mesen2 Core/NES/Mappers/Sachen/Sachen_133.h.
 *
 *   PRG page size 32KB, CHR page size 8KB, one register selected by the ADDRESS:
 *     (addr & 0x6100) == 0x4100  ->  PRG 32KB page = (value >> 2) & 1
 *                                    CHR  8KB page = value & 3
 *   The same window is reachable both in $4100-$7FFF (the core's mapper_apu hook) and in
 *   $C100-$FFFF (mapper_write), so both hooks must decode it.
 *   Power-on: PRG page 0, CHR page 0.
 *
 * The old implementation only listened on the APU window, decoded PRG from bit 7 and CHR
 * from bits 0-2, so games that write the register above $8000 never changed page.
 */

typedef struct {
    uint8_t reg;
    uint8_t prg_bank_count;
    uint8_t chr_bank_count;
} mapper133_t;

static void nes_mapper_deinit(nes_t* nes) {
    nes_free(nes->nes_mapper.mapper_register);
    nes->nes_mapper.mapper_register = NULL;
}

static void mapper133_update_banks(nes_t* nes) {
    mapper133_t* m = (mapper133_t*)nes->nes_mapper.mapper_register;
    nes_load_prgrom_32k(nes, 0, (uint16_t)((m->reg >> 2u) & 0x01u));
    nes_load_chrrom_8k(nes, 0, (uint8_t)(m->reg & 0x03u));
}

static void nes_mapper_init(nes_t* nes) {
    if (nes->nes_mapper.mapper_register == NULL) {
        nes->nes_mapper.mapper_register = nes_mapper_register_alloc(nes, (uint16_t)sizeof(mapper133_t));
        if (nes->nes_mapper.mapper_register == NULL) return;
    }
    mapper133_t* m = (mapper133_t*)nes->nes_mapper.mapper_register;
    nes_memset(m, 0, sizeof(mapper133_t));
    m->prg_bank_count = (uint8_t)(nes->nes_rom.prg_rom_size * 2u);
    m->chr_bank_count = (uint8_t)(nes->nes_rom.chr_rom_size * 8u);
    mapper133_update_banks(nes);
}

/* One decode shared by both windows: only (addr & 0x6100) == 0x4100 is the register. */
static void mapper133_apply(nes_t* nes, uint16_t address, uint8_t data) {
    mapper133_t* m = (mapper133_t*)nes->nes_mapper.mapper_register;
    if ((address & 0x6100u) != 0x4100u) return;
    m->reg = data;
    mapper133_update_banks(nes);
}

static void nes_mapper_write(nes_t* nes, uint16_t address, uint8_t data) {
    mapper133_apply(nes, address, data);
}

static void nes_mapper_apu(nes_t* nes, uint16_t address, uint8_t data) {
    mapper133_apply(nes, address, data);
}

int nes_mapper133_init(nes_t* nes) {
    nes->nes_mapper.mapper_init   = nes_mapper_init;
    nes->nes_mapper.mapper_deinit = nes_mapper_deinit;
    nes->nes_mapper.mapper_write  = nes_mapper_write;
    nes->nes_mapper.mapper_apu    = nes_mapper_apu;
    return NES_OK;
}
