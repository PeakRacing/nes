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

/* https://www.nesdev.org/wiki/INES_Mapper_244
 * C&E Decathlon: 32KB PRG page + 8KB CHR bank, both selected by the **data byte**
 * through permutation tables (the write address carries no information).
 * Authority: FCEUX src/boards/244.cpp (M244Write / M244Power).
 */

static const uint8_t mapper244_prg_perm[4][4] = {
    { 0, 1, 2, 3 },
    { 3, 2, 1, 0 },
    { 0, 2, 1, 3 },
    { 3, 1, 2, 0 },
};

static const uint8_t mapper244_chr_perm[8][8] = {
    { 0, 1, 2, 3, 4, 5, 6, 7 },
    { 0, 2, 1, 3, 4, 6, 5, 7 },
    { 0, 1, 4, 5, 2, 3, 6, 7 },
    { 0, 4, 1, 5, 2, 6, 3, 7 },
    { 0, 4, 2, 6, 1, 5, 3, 7 },
    { 0, 2, 4, 6, 1, 3, 5, 7 },
    { 7, 6, 5, 4, 3, 2, 1, 0 },
    { 7, 6, 5, 4, 3, 2, 1, 0 },
};

static void nes_mapper_apply(nes_t* nes) {
    uint8_t* reg = (uint8_t*)nes->nes_mapper.mapper_register;
    nes_load_prgrom_32k(nes, 0, reg[0]);
    if (nes->nes_rom.chr_rom_size > 0) {
        nes_load_chrrom_8k(nes, 0, reg[1]);
    }
}

static void nes_mapper_init(nes_t* nes) {
    if (nes->nes_mapper.mapper_register == NULL) {
        nes->nes_mapper.mapper_register = nes_mapper_register_alloc(nes, 2);
        if (nes->nes_mapper.mapper_register == NULL) return;
    }
    uint8_t* reg = (uint8_t*)nes->nes_mapper.mapper_register;
    reg[0] = 0;                     /* PRG 32KB page */
    reg[1] = 0;                     /* CHR 8KB bank */
    nes_mapper_apply(nes);
}

static void nes_mapper_write(nes_t* nes, uint16_t address, uint8_t data) {
    uint8_t* reg = (uint8_t*)nes->nes_mapper.mapper_register;
    (void)address;
    if (data & 0x08u) {
        reg[1] = mapper244_chr_perm[(data >> 4) & 0x07u][data & 0x07u];
    } else {
        reg[0] = mapper244_prg_perm[(data >> 4) & 0x03u][data & 0x03u];
    }
    nes_mapper_apply(nes);
}

/* Register both bytes so a save state restores the selected pages. */
static void nes_mapper_state_reapply(nes_t* nes) {
    nes_mapper_apply(nes);
}

int nes_mapper244_init(nes_t* nes) {
    nes->nes_mapper.mapper_init  = nes_mapper_init;
    nes->nes_mapper.mapper_write = nes_mapper_write;
    nes->nes_mapper.mapper_state_reapply = nes_mapper_state_reapply;
    return NES_OK;
}

