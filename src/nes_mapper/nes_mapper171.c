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
 * Mapper 171 - Kaiser 7058 (推倒胡麻将 等).
 * Authority: Mesen2 Core/NES/Mappers/Kaiser/Kaiser7058.h.
 *
 * 32KB of PRG is fixed at $8000; the board only switches two 4KB CHR pages, and the
 * register is picked out of the address (the data byte is the page number):
 *   $F000-$F07F -> CHR 4KB page 0
 *   $F080-$F0FF -> CHR 4KB page 1
 */

typedef struct {
    uint8_t chr[2];
} mapper171_t;

static void mapper171_apply(nes_t* nes) {
    mapper171_t* m = (mapper171_t*)nes->nes_mapper.mapper_register;
    nes_load_chrrom_4k(nes, 0, m->chr[0]);
    nes_load_chrrom_4k(nes, 1, m->chr[1]);
}

static void nes_mapper_init(nes_t* nes) {
    if (nes->nes_mapper.mapper_register == NULL) {
        nes->nes_mapper.mapper_register = nes_mapper_register_alloc(nes, (uint16_t)sizeof(mapper171_t));
        if (nes->nes_mapper.mapper_register == NULL) return;
    }
    mapper171_t* m = (mapper171_t*)nes->nes_mapper.mapper_register;
    nes_memset(m, 0, sizeof(mapper171_t));

    nes_load_prgrom_32k(nes, 0, 0);
    nes_load_chrrom_8k(nes, 0, 0);
    mapper171_apply(nes);
}

static void nes_mapper_write(nes_t* nes, uint16_t address, uint8_t data) {
    mapper171_t* m = (mapper171_t*)nes->nes_mapper.mapper_register;
    if (address < 0xF000u) return;

    if ((address & 0xF080u) == 0xF000u) {
        m->chr[0] = data;
    } else {
        m->chr[1] = data;
    }
    mapper171_apply(nes);
}

static void nes_mapper_state_reapply(nes_t* nes) {
    mapper171_apply(nes);
}

int nes_mapper171_init(nes_t* nes) {
    nes->nes_mapper.mapper_init          = nes_mapper_init;
    nes->nes_mapper.mapper_write         = nes_mapper_write;
    nes->nes_mapper.mapper_state_reapply = nes_mapper_state_reapply;
    return NES_OK;
}
