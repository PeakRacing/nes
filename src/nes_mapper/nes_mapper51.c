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
 * Mapper 51 - BMC 11-in-1 / 22-in-1 (球类11合1 等).
 * Authority: Mesen2 Core/NES/Mappers/Unlicensed/Bmc51.h (identical logic in FCEUX
 * src/boards/51.cpp).  PRG pages are 8KB, CHR pages are 8KB, and the board has no
 * RAM: its registers live at $6000-$FFFF and are selected by the *address*:
 *
 *   addr <= $7FFF                        : mode = ((v >> 3) & 2) | ((v >> 1) & 1)
 *   addr $C000-$DFFF                     : bank = v & 0x0F; mode = ((v >> 3) & 2) | (mode & 1)
 *   addr $8000-$BFFF / $E000-$FFFF       : bank = v & 0x0F
 *
 *   mode bit0 == 1 : 32KB PRG at $8000 = bank          ($6000 window = PRG page 0x23 | bank<<2)
 *   mode bit0 == 0 : 16KB PRG at $8000 = (bank << 1) | (mode >> 1)
 *                    16KB PRG at $C000 = (bank << 1) | 7 ($6000 window = PRG page 0x2F | bank<<2)
 *
 *   mirroring      : mode == 3 -> horizontal, otherwise vertical
 *
 * Power-on state is bank = 0, mode = 1 (i.e. 32KB of bank 0), which is what the
 * multicarts rely on before they write anything.
 *
 * The $6000-$7FFF window is PRG-ROM, not RAM.  In the streamed-ROM build
 * (NES_ROM_STREAM=1) only the mapper's active banks are resident, so the window is
 * left empty there rather than reaching into the streaming cache; the default build
 * (and every desktop target) serves it directly out of the in-memory image.
 */

typedef struct {
    uint8_t  bank;
    uint8_t  mode;
    uint16_t window_page;      /* 8KB PRG page visible at $6000-$7FFF */
} mapper51_t;

static void mapper51_apply(nes_t* nes) {
    mapper51_t* m = (mapper51_t*)nes->nes_mapper.mapper_register;

    if (m->mode & 0x01u) {
        nes_load_prgrom_32k(nes, 0, m->bank);
        m->window_page = (uint16_t)(0x23u | ((uint16_t)m->bank << 2));
    } else {
        nes_load_prgrom_16k(nes, 0, (uint16_t)(((uint16_t)m->bank << 1) | (m->mode >> 1)));
        nes_load_prgrom_16k(nes, 1, (uint16_t)(((uint16_t)m->bank << 1) | 0x07u));
        m->window_page = (uint16_t)(0x2Fu | ((uint16_t)m->bank << 2));
    }

    nes_ppu_screen_mirrors(nes, (m->mode == 0x03u) ? NES_MIRROR_HORIZONTAL : NES_MIRROR_VERTICAL);
}

static void nes_mapper_init(nes_t* nes) {
    if (nes->nes_mapper.mapper_register == NULL) {
        nes->nes_mapper.mapper_register = nes_mapper_register_alloc(nes, (uint16_t)sizeof(mapper51_t));
        if (nes->nes_mapper.mapper_register == NULL) return;
    }
    mapper51_t* m = (mapper51_t*)nes->nes_mapper.mapper_register;
    nes_memset(m, 0, sizeof(mapper51_t));
    m->bank = 0;
    m->mode = 1;

    nes_load_chrrom_8k(nes, 0, 0);
    mapper51_apply(nes);
}

/* $6000-$7FFF: the board's register window (writes) / a PRG-ROM window (reads).
 * The core routes $6000-$7FFF writes to mapper_sram and $8000+ writes to mapper_write,
 * so the very same decoder is installed on both (the 68合1 lesson). */
static void mapper51_write_register(nes_t* nes, uint16_t address, uint8_t data) {
    mapper51_t* m = (mapper51_t*)nes->nes_mapper.mapper_register;

    if (address < 0x8000u) {
        m->mode = (uint8_t)(((data >> 3) & 0x02u) | ((data >> 1) & 0x01u));
    } else if (address >= 0xC000u && address <= 0xDFFFu) {
        m->bank = (uint8_t)(data & 0x0Fu);
        m->mode = (uint8_t)(((data >> 3) & 0x02u) | (m->mode & 0x01u));
    } else {
        m->bank = (uint8_t)(data & 0x0Fu);
    }
    mapper51_apply(nes);
}

static void nes_mapper_write(nes_t* nes, uint16_t address, uint8_t data) {
    mapper51_write_register(nes, address, data);
}

static void nes_mapper_sram(nes_t* nes, uint16_t address, uint8_t data) {
    mapper51_write_register(nes, address, data);
}

static uint8_t nes_mapper_read_sram(nes_t* nes, uint16_t address) {
    mapper51_t* m = (mapper51_t*)nes->nes_mapper.mapper_register;
    const uint16_t offset = (uint16_t)(address & 0x1FFFu);
#if (NES_ROM_STREAM == 1)
    (void)m;
    (void)offset;
    return 0;                                    /* see the file comment */
#else
    if (nes->nes_rom.prg_rom == NULL) return 0;
    {
        const uint16_t total_8k = (uint16_t)(nes->nes_rom.prg_rom_size * 2u);
        uint16_t page = m->window_page;
        if (total_8k != 0u && page >= total_8k) page = (uint16_t)(page % total_8k);
        return nes->nes_rom.prg_rom[(uint32_t)page * 8192u + offset];
    }
#endif
}

static void nes_mapper_state_reapply(nes_t* nes) {
    mapper51_apply(nes);
}

int nes_mapper51_init(nes_t* nes) {
    nes->nes_mapper.mapper_init          = nes_mapper_init;
    nes->nes_mapper.mapper_write         = nes_mapper_write;
    nes->nes_mapper.mapper_sram          = nes_mapper_sram;
    nes->nes_mapper.mapper_read_sram     = nes_mapper_read_sram;
    nes->nes_mapper.mapper_state_reapply = nes_mapper_state_reapply;
    return NES_OK;
}
