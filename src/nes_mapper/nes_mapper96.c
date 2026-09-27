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
 * Bandai Oeka Kids (iNES mapper 96) — the drawing-tablet board used by
 * "Oeka Kids - Anpanman to Oekaki Shiyou!!".
 *
 * Register map follows Mesen's OekaKids:
 *   write $8000-$FFFF (bus conflicts!): bits[1:0] = 32KB PRG bank for $8000-$FFFF
 *                                      bit[2]    = outer 4KB CHR-RAM page (0 or 4)
 *   CHR-RAM: 32KB, addressed as 4KB pages
 *     PPU $0000-$0FFF = 4KB page (outer | inner)
 *     PPU $1000-$1FFF = 4KB page (outer | 3)
 *   inner page = bits[9:8] of the VRAM address, latched whenever the address moves
 *   into the $2000-$2FFF (nametable) window — the game uses this to swap the drawing
 *   surface while scrolling the nametable.
 *
 * The board has no CHR-ROM at all: the tablet surface lives in the 32KB CHR-RAM that
 * the game fills through $2007, so this mapper owns that RAM (the core only provides
 * an 8KB CHR-RAM backing store for CHR-RAM boards).
 */

#define MAPPER96_CHR_RAM_SIZE   (0x8000)    /* 32KB, eight 4KB pages */

typedef struct mapper96_register {
    uint8_t  prg_bank;                      /* 32KB page at $8000-$FFFF */
    uint8_t  outer_chr;                     /* 0 or 4 */
    uint8_t  inner_chr;                     /* 0-3, latched from the VRAM address */
    uint16_t last_addr;                     /* previous VRAM address (transition detect) */
    uint8_t* chr_ram;                       /* 32KB board RAM */
} mapper96_register_t;

static void mapper96_update_chr(nes_t* nes) {
    mapper96_register_t* r = (mapper96_register_t*)nes->nes_mapper.mapper_register;
    uint8_t page0;
    uint8_t page1;
    if (r == NULL || r->chr_ram == NULL) {
        return;
    }
    page0 = (uint8_t)((r->outer_chr | r->inner_chr) & 0x07u);
    page1 = (uint8_t)((r->outer_chr | 0x03u) & 0x07u);
    for (uint8_t i = 0; i < 4u; i++) {
        nes->nes_ppu.pattern_table[i] = r->chr_ram + (uint32_t)4096u * page0 + (uint32_t)1024u * i;
        nes->nes_ppu.pattern_table[4 + i] = r->chr_ram + (uint32_t)4096u * page1 + (uint32_t)1024u * i;
    }
}

static void nes_mapper_init(nes_t* nes) {
    mapper96_register_t* r = (mapper96_register_t*)nes_mapper_register_alloc(nes, (uint16_t)sizeof(mapper96_register_t));
    if (r == NULL) {
        return;
    }
    nes->nes_mapper.mapper_register = r;
    r->chr_ram = (uint8_t*)nes_malloc(MAPPER96_CHR_RAM_SIZE);
    if (r->chr_ram == NULL) {
        return;
    }
    nes_memset(r->chr_ram, 0, MAPPER96_CHR_RAM_SIZE);

    nes_load_prgrom_32k(nes, 0, 0);
    mapper96_update_chr(nes);
}

static void nes_mapper_deinit(nes_t* nes) {
    mapper96_register_t* r = (mapper96_register_t*)nes->nes_mapper.mapper_register;
    if (r != NULL && r->chr_ram != NULL) {
        nes_free(r->chr_ram);
        r->chr_ram = NULL;
    }
    if (nes->nes_mapper.mapper_register != NULL) {
        nes_free(nes->nes_mapper.mapper_register);
        nes->nes_mapper.mapper_register = NULL;
    }
}

static void nes_mapper_write(nes_t* nes, uint16_t address, uint8_t data) {
    mapper96_register_t* r = (mapper96_register_t*)nes->nes_mapper.mapper_register;
    if (r == NULL) {
        return;
    }
    /* Bus conflicts: the board ANDs the written value with the mapped PRG byte. */
    data &= nes->nes_cpu.prg_banks[(address >> 13) & 0x03u][address & 0x1FFFu];
    nes_load_prgrom_32k(nes, 0, (uint16_t)(data & 0x03u));
    r->prg_bank = (uint8_t)(data & 0x03u);
    r->outer_chr = (uint8_t)(data & 0x04u);
    mapper96_update_chr(nes);
}

/* Per scanline (mode 1 = background): latch the nametable address bits like the board does. */
static void nes_mapper_render_screen(nes_t* nes, uint8_t mode) {
    mapper96_register_t* r = (mapper96_register_t*)nes->nes_mapper.mapper_register;
    uint16_t addr;
    if (r == NULL || mode != 1u) {
        return;
    }
    addr = nes->nes_ppu.v_reg;
    if ((addr & 0x3000u) == 0x2000u) {
        uint8_t inner = (uint8_t)((addr >> 8) & 0x03u);
        if (inner != r->inner_chr) {
            r->inner_chr = inner;
            mapper96_update_chr(nes);
        }
    }
    r->last_addr = addr;
}

/* --- save state: the 32KB board RAM and the latched page live outside the core --- */

/* Re-point the pattern tables after a state has been restored (the generic restore maps CHR
 * through nes_rom.chr_rom, which this board does not use). */
static void nes_mapper_state_reapply(nes_t* nes) {
    mapper96_update_chr(nes);
}

static int nes_mapper_state_save(nes_t* nes, nes_state_writer_t* writer) {
    mapper96_register_t* r = (mapper96_register_t*)nes->nes_mapper.mapper_register;
    if (r == NULL || r->chr_ram == NULL) {
        return NES_OK;
    }
    if (nes_state_write(writer, r->chr_ram, MAPPER96_CHR_RAM_SIZE) != NES_OK) {
        return NES_STATE_ERR_IO;
    }
    return NES_OK;
}

static int nes_mapper_state_load(nes_t* nes, nes_state_reader_t* reader) {
    mapper96_register_t* r = (mapper96_register_t*)nes->nes_mapper.mapper_register;
    if (r == NULL || r->chr_ram == NULL) {
        return NES_OK;
    }
    if (nes_state_read(reader, r->chr_ram, MAPPER96_CHR_RAM_SIZE) != NES_OK) {
        return NES_STATE_ERR_IO;
    }
    return NES_OK;
}

int nes_mapper96_init(nes_t* nes) {
    nes->nes_mapper.mapper_init         = nes_mapper_init;
    nes->nes_mapper.mapper_deinit       = nes_mapper_deinit;
    nes->nes_mapper.mapper_write        = nes_mapper_write;
    nes->nes_mapper.mapper_render_screen = nes_mapper_render_screen;
#if (NES_USE_FS == 1)
    nes->nes_mapper.mapper_state_save   = nes_mapper_state_save;
    nes->nes_mapper.mapper_state_load   = nes_mapper_state_load;
    nes->nes_mapper.mapper_state_reapply = nes_mapper_state_reapply;
#endif
    return NES_OK;
}
