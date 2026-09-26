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

/* https://www.nesdev.org/wiki/INES_Mapper_199
 * Mapper 199 — Waixing board G (Dragon Ball Z II/III (C), San Guo Zhi 2, ...):
 * an MMC3 clone whose CHR banks 0-7 address an 8KB CHR-RAM instead of CHR-ROM,
 * while banks 8 and above come from CHR-ROM.  The games load their Chinese
 * font/dynamic tiles into that RAM through $2007 and then switch CHR-ROM pages
 * in for the rest of the graphics.
 * Reference implementation: MAME src/devices/bus/nes/waixing.cpp,
 * nes_waixing_g_device::chr_cb() ("bank < 0x08 ? CHRRAM : CHRROM").
 */

#define MAPPER199_CHR_RAM_SIZE 8192u

typedef struct {
    uint8_t bank_select;
    uint8_t bank_values[8];
    uint8_t mirroring;
    uint8_t irq_latch;
    uint8_t irq_counter;
    uint8_t irq_reload;
    uint8_t irq_enabled;
    uint8_t prg_bank_count;
    uint16_t chr_bank_count;    /* 1KB banks; 32x8KB CHR = 256, must not be uint8_t */
    uint8_t chr_1k_hi[2];       /* bank_select $0A/$0B: the 1KB pages for slots 1 and 3 */
    uint8_t chr_ram[MAPPER199_CHR_RAM_SIZE]; /* banks 0-7 */
} mapper199_t;

static void nes_mapper_deinit(nes_t* nes) {
    nes_free(nes->nes_mapper.mapper_register);
    nes->nes_mapper.mapper_register = NULL;
}

/* Bank 0-7 = CHR-RAM page, 8+ = CHR-ROM (see file header). */
static void mapper199_load_chr1k(nes_t* nes, mapper199_t* m, uint8_t slot, uint16_t bank) {
    if (bank < 8u) {
        nes->nes_ppu.pattern_table[slot] = m->chr_ram + ((uint32_t)bank * 1024u);
    } else if (m->chr_bank_count > 0u) {
        nes_load_chrrom_1k(nes, slot, (uint16_t)(bank % m->chr_bank_count));
    }
}

static void mapper199_update_banks(nes_t* nes) {
    mapper199_t* m = (mapper199_t*)nes->nes_mapper.mapper_register;
    uint8_t prg_mode = (m->bank_select >> 6) & 1u;
    uint8_t chr_mode = (m->bank_select >> 7) & 1u;
    uint8_t last  = (uint8_t)(m->prg_bank_count - 1u);
    uint8_t slast = (uint8_t)(m->prg_bank_count - 2u);

    if (prg_mode == 0u) {
        nes_load_prgrom_8k(nes, 0, m->bank_values[6] % m->prg_bank_count);
        nes_load_prgrom_8k(nes, 1, m->bank_values[7] % m->prg_bank_count);
        nes_load_prgrom_8k(nes, 2, slast);
        nes_load_prgrom_8k(nes, 3, last);
    } else {
        nes_load_prgrom_8k(nes, 0, slast);
        nes_load_prgrom_8k(nes, 1, m->bank_values[7] % m->prg_bank_count);
        nes_load_prgrom_8k(nes, 2, m->bank_values[6] % m->prg_bank_count);
        nes_load_prgrom_8k(nes, 3, last);
    }

    if (m->chr_bank_count == 0u && nes->nes_rom.chr_rom_size == 0u) {
        /* CHR-RAM-only board: every slot is the embedded RAM. */
        for (uint8_t i = 0; i < 8u; i++) {
            nes->nes_ppu.pattern_table[i] = m->chr_ram + ((uint32_t)i * 1024u);
        }
        return;
    }

    if (chr_mode == 0u) {
        mapper199_load_chr1k(nes, m, 0, m->bank_values[0]);
        mapper199_load_chr1k(nes, m, 1, m->chr_1k_hi[0]);
        mapper199_load_chr1k(nes, m, 2, m->bank_values[1]);
        mapper199_load_chr1k(nes, m, 3, m->chr_1k_hi[1]);
        mapper199_load_chr1k(nes, m, 4, m->bank_values[2]);
        mapper199_load_chr1k(nes, m, 5, m->bank_values[3]);
        mapper199_load_chr1k(nes, m, 6, m->bank_values[4]);
        mapper199_load_chr1k(nes, m, 7, m->bank_values[5]);
    } else {
        mapper199_load_chr1k(nes, m, 0, m->bank_values[2]);
        mapper199_load_chr1k(nes, m, 1, m->bank_values[3]);
        mapper199_load_chr1k(nes, m, 2, m->bank_values[4]);
        mapper199_load_chr1k(nes, m, 3, m->bank_values[5]);
        mapper199_load_chr1k(nes, m, 4, m->bank_values[0]);
        mapper199_load_chr1k(nes, m, 5, m->chr_1k_hi[0]);
        mapper199_load_chr1k(nes, m, 6, m->bank_values[1]);
        mapper199_load_chr1k(nes, m, 7, m->chr_1k_hi[1]);
    }
}

static void nes_mapper_init(nes_t* nes) {
    if (nes->nes_mapper.mapper_register == NULL) {
        nes->nes_mapper.mapper_register = nes_malloc(sizeof(mapper199_t));
        if (nes->nes_mapper.mapper_register == NULL) return;
    }
    mapper199_t* m = (mapper199_t*)nes->nes_mapper.mapper_register;
    nes_memset(m, 0, sizeof(mapper199_t));
    m->prg_bank_count = (uint8_t)(nes->nes_rom.prg_rom_size * 2u);
    m->chr_bank_count = (uint16_t)(nes->nes_rom.chr_rom_size * 8u);
    m->bank_values[6] = 0;
    m->bank_values[7] = 1;

    /* Board G carries 8KB of work RAM at $6000-$7FFF and the games live in it:
     * Dragon Ball Z 3 keeps its screen-script pointers there ($69ED etc.).  The
     * core only allocates it when the build sets NES_USE_SRAM=1; SDL/RT-Thread
     * ports use 0, so without this the game loses its scripts and every screen
     * after the intro stays black (same reason mapper4/74/192 allocate it). */
    if (nes->nes_rom.sram == NULL) {
        nes->nes_rom.sram = (uint8_t*)nes_malloc(SRAM_SIZE);
        if (nes->nes_rom.sram != NULL) {
            nes_memset(nes->nes_rom.sram, 0, SRAM_SIZE);
        }
    }

    if (nes->nes_rom.chr_rom_size == 0u) nes_load_chrrom_8k(nes, 0, 0);
    mapper199_update_banks(nes);
}

static void nes_mapper_write(nes_t* nes, uint16_t address, uint8_t data) {
    mapper199_t* m = (mapper199_t*)nes->nes_mapper.mapper_register;
    switch (address & 0xE001u) {
    case 0x8000: m->bank_select = data; mapper199_update_banks(nes); break;
    case 0x8001: {
        /* Waixing board G register layout: bank select $00-$05 are the 1KB CHR
         * pages for slots 0/2/4/5/6/7, $0A/$0B are the 1KB pages for slots 1/3
         * (the second page of the classic 2KB pairs), $06/$07 are the PRG
         * registers, $08/$09 are unused on this board.
         * Proof: the game's own bank helpers (ROM $FF14/$FF1D/$FF22/$FF27/
         * $FF2C/$FF31/$FF36/$FF3B) initialise banks 0..7 into slots 0..7. */
        uint8_t bs = m->bank_select & 0x0Fu;
        if (bs == 0x0Au)      m->chr_1k_hi[0] = data;
        else if (bs == 0x0Bu) m->chr_1k_hi[1] = data;
        else if (bs <= 0x07u) m->bank_values[bs] = data;
        else return;                    /* $08/$09/$0C-$0F: not connected */
        mapper199_update_banks(nes);
        break;
    }
    case 0xA000:
        /* Waixing mirroring register: 0 = vertical, 1 = horizontal,
         * 2 = one-screen low (NT0), 3 = one-screen high (NT1).
         * MAME: nes_waixing_a_device::set_mirror().  Dragon Ball Z 3 writes 2
         * and 3 for its title/story screens, so masking to one bit (V/H only)
         * renders those screens with the wrong nametable. */
        m->mirroring = data & 0x03u;
        if (nes->nes_rom.four_screen == 0) {
            static const nes_mirror_type_t table[4] = {
                NES_MIRROR_VERTICAL, NES_MIRROR_HORIZONTAL,
                NES_MIRROR_ONE_SCREEN0, NES_MIRROR_ONE_SCREEN1,
            };
            nes_ppu_screen_mirrors(nes, table[m->mirroring]);
        }
        break;
    case 0xA001: break;
    case 0xC000: m->irq_latch   = data; break;
    case 0xC001: m->irq_reload  = 1; break;
    case 0xE000: m->irq_enabled = 0; nes->nes_cpu.irq_pending = 0; break;
    case 0xE001: m->irq_enabled = 1; break;
    default: break;
    }
}

static void nes_mapper_hsync(nes_t* nes) {
    mapper199_t* m = (mapper199_t*)nes->nes_mapper.mapper_register;
    if (nes->nes_ppu.MASK_b == 0 && nes->nes_ppu.MASK_s == 0) return;
    if (m->irq_counter == 0u || m->irq_reload) {
        m->irq_counter = m->irq_latch;
    } else {
        m->irq_counter--;
    }
    if (m->irq_counter == 0u && m->irq_enabled) nes_cpu_irq(nes);
    m->irq_reload = 0;
}

int nes_mapper199_init(nes_t* nes) {
    nes->nes_mapper.mapper_init      = nes_mapper_init;
    nes->nes_mapper.mapper_deinit    = nes_mapper_deinit;
    nes->nes_mapper.mapper_write     = nes_mapper_write;
    nes->nes_mapper.mapper_hsync     = nes_mapper_hsync;
    return NES_OK;
}
