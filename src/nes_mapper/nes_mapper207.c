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
 * Mapper 207 - Taito X1-005 with alternate mirroring (不动明王传 / Fudou Myouou Den).
 * Authority: Mesen2 Core/NES/Mappers/Taito/TaitoX1005.h, constructed as
 * `TaitoX1005(true)`; mapper 80 is the very same class with alternateMirroring = false.
 *
 * PRG pages are 8KB (page 3 hardwired to the last one), CHR pages are 1KB, and every
 * register lives in $7EF0-$7EFF:
 *
 *   $7EF0 : CHR 1KB pages 0/1 = value, value+1; NT slots 0/1 = value bit 7
 *   $7EF1 : CHR 1KB pages 2/3 = value, value+1; NT slots 2/3 = value bit 7
 *   $7EF2..$7EF5 : CHR 1KB pages 4..7 = value
 *   $7EF6/$7EF7  : mirroring on plain X1-005 only - NOT decoded here
 *   $7EF8/$7EF9  : RAM permission; the 128-byte $7F00-$7FFF window opens on $A3
 *   $7EFA/$7EFB : PRG 8KB page 0     $7EFC/$7EFD : PRG 8KB page 1
 *   $7EFE/$7EFF : PRG 8KB page 2
 *
 * The 128-byte RAM is mirrored once (both halves of $7F00-$7FFF alias) and, like every
 * other board RAM, is allocated unconditionally rather than only for battery carts.
 */

typedef struct {
    uint8_t chr[6];
    uint8_t prg[3];
    uint8_t ram_permission;
} mapper207_register_t;

static void mapper207_update_chr(nes_t* nes) {
    mapper207_register_t* r = (mapper207_register_t*)nes->nes_mapper.mapper_register;
    nes_load_chrrom_1k(nes, 0, r->chr[0]);
    nes_load_chrrom_1k(nes, 1, (uint16_t)(r->chr[0] + 1u));
    nes_load_chrrom_1k(nes, 2, r->chr[1]);
    nes_load_chrrom_1k(nes, 3, (uint16_t)(r->chr[1] + 1u));
    nes_load_chrrom_1k(nes, 4, r->chr[2]);
    nes_load_chrrom_1k(nes, 5, r->chr[3]);
    nes_load_chrrom_1k(nes, 6, r->chr[4]);
    nes_load_chrrom_1k(nes, 7, r->chr[5]);
}

static void mapper207_update_prg(nes_t* nes) {
    mapper207_register_t* r = (mapper207_register_t*)nes->nes_mapper.mapper_register;
    const uint16_t total_8k = (uint16_t)(nes->nes_rom.prg_rom_size * 2u);
    nes_load_prgrom_8k(nes, 0, r->prg[0]);
    nes_load_prgrom_8k(nes, 1, r->prg[1]);
    nes_load_prgrom_8k(nes, 2, r->prg[2]);
    nes_load_prgrom_8k(nes, 3, (total_8k != 0u) ? (uint16_t)(total_8k - 1u) : 0u);
}

static void mapper207_update_nt(nes_t* nes) {
    mapper207_register_t* r = (mapper207_register_t*)nes->nes_mapper.mapper_register;
    if (nes->nes_rom.four_screen) return;
    /* Alternate mirroring: bit 7 of the two CHR bank registers picks the 1KB VRAM page
       for each pair of nametable slots (only 2KB of VRAM exists, so it is A or B). */
    nes->nes_ppu.name_table[0] = nes->nes_ppu.ppu_vram[r->chr[0] >> 7];
    nes->nes_ppu.name_table[1] = nes->nes_ppu.ppu_vram[r->chr[0] >> 7];
    nes->nes_ppu.name_table[2] = nes->nes_ppu.ppu_vram[r->chr[1] >> 7];
    nes->nes_ppu.name_table[3] = nes->nes_ppu.ppu_vram[r->chr[1] >> 7];
    nes->nes_ppu.name_table_mirrors[0] = nes->nes_ppu.name_table[0];
    nes->nes_ppu.name_table_mirrors[1] = nes->nes_ppu.name_table[1];
    nes->nes_ppu.name_table_mirrors[2] = nes->nes_ppu.name_table[2];
    nes->nes_ppu.name_table_mirrors[3] = nes->nes_ppu.name_table[3];
}

static void nes_mapper_init(nes_t* nes) {
    if (nes->nes_mapper.mapper_register == NULL) {
        nes->nes_mapper.mapper_register = nes_mapper_register_alloc(nes, (uint16_t)sizeof(mapper207_register_t));
        if (nes->nes_mapper.mapper_register == NULL) return;
    }
    mapper207_register_t* r = (mapper207_register_t*)nes->nes_mapper.mapper_register;
    nes_memset(r, 0, sizeof(mapper207_register_t));

    if (nes->nes_rom.sram == NULL) {
        nes->nes_rom.sram = (uint8_t*)nes_malloc(SRAM_SIZE);
        if (nes->nes_rom.sram != NULL) {
            nes_memset(nes->nes_rom.sram, 0, SRAM_SIZE);
        }
    }

    if (nes->nes_rom.chr_rom_size == 0) nes_load_chrrom_8k(nes, 0, 0);
    mapper207_update_chr(nes);
    mapper207_update_prg(nes);
    mapper207_update_nt(nes);
}

static void nes_mapper_sram(nes_t* nes, uint16_t address, uint8_t data) {
    mapper207_register_t* r = (mapper207_register_t*)nes->nes_mapper.mapper_register;
    switch (address) {
    case 0x7EF0u: r->chr[0] = data; mapper207_update_chr(nes); mapper207_update_nt(nes); break;
    case 0x7EF1u: r->chr[1] = data; mapper207_update_chr(nes); mapper207_update_nt(nes); break;
    case 0x7EF2u: r->chr[2] = data; mapper207_update_chr(nes); break;
    case 0x7EF3u: r->chr[3] = data; mapper207_update_chr(nes); break;
    case 0x7EF4u: r->chr[4] = data; mapper207_update_chr(nes); break;
    case 0x7EF5u: r->chr[5] = data; mapper207_update_chr(nes); break;
    case 0x7EF6u:
    case 0x7EF7u:
        break;                                  /* not decoded: alternate mirroring */
    case 0x7EF8u:
    case 0x7EF9u:
        r->ram_permission = data;
        break;
    case 0x7EFAu: case 0x7EFBu: r->prg[0] = data; mapper207_update_prg(nes); break;
    case 0x7EFCu: case 0x7EFDu: r->prg[1] = data; mapper207_update_prg(nes); break;
    case 0x7EFEu: case 0x7EFFu: r->prg[2] = data; mapper207_update_prg(nes); break;
    default:
        break;
    }
}

/* 128-byte RAM: both halves alias, and the window is gated by $7EF8/$7EF9 == $A3. */
static uint16_t mapper207_ram_index(uint16_t address) {
    return (uint16_t)(0x1F00u + (address & 0x007Fu));
}

static uint8_t nes_mapper_read_sram(nes_t* nes, uint16_t address) {
    mapper207_register_t* r = (mapper207_register_t*)nes->nes_mapper.mapper_register;
    if ((address & 0xFF00u) != 0x7F00u || r->ram_permission != 0xA3u) return 0;
    if (nes->nes_rom.sram == NULL) return 0;
    return nes->nes_rom.sram[mapper207_ram_index(address)];
}

static void nes_mapper_state_reapply(nes_t* nes) {
    mapper207_update_chr(nes);
    mapper207_update_prg(nes);
    mapper207_update_nt(nes);
}

int nes_mapper207_init(nes_t* nes) {
    nes->nes_mapper.mapper_init          = nes_mapper_init;
    nes->nes_mapper.mapper_sram          = nes_mapper_sram;
    nes->nes_mapper.mapper_read_sram     = nes_mapper_read_sram;
    nes->nes_mapper.mapper_state_reapply = nes_mapper_state_reapply;
    return NES_OK;
}
