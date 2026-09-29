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

/* https://www.nesdev.org/wiki/INES_Mapper_078
 * Irem 74HC161/32 and Jaleco JF-16 / Holy Diver.
 * Write $8000-$FFFF:
 *   bits[2:0] = 16KB PRG bank for $8000-$BFFF (fixed last at $C000)
 *   bits[7:4] = CHR 8KB bank
 *   bit[3]    = mirroring; Holy Diver uses H/V, while the other known variant
 *               uses one-screen A/B. iNES 1.0 needs a CRC/submapper override.
 */

/*
 * Jaleco JF-16 (mapper 78).  Mesen2's JalecoJf16 has exactly two wirings, selected by the NES 2.0
 * submapper: submapper 3 is Holy Diver, which drives the mirroring bit as H/V, while every other
 * board of the family - Cosmo Carrier is submapper 1 - uses one-screen A/B.
 *
 * iNES 1.0 headers carry no submapper and both games have the same shape (128KB PRG + 128KB CHR
 * with flags6 bit3 set), so the variant cannot be guessed from the sizes.  The previous shape
 * heuristic therefore classified Cosmo Carrier as Holy Diver, which put its title screen on the
 * wrong nametable and left the visible screen blank blue.  Identify the variant by ROM CRC.
 */
static uint8_t mapper78_is_holy_diver(nes_t* nes) {
    return nes->nes_rom.rom_crc == 0xBA51AC6Fu ||   /* Holy Diver (J) [!]    (submapper 3) */
           nes->nes_rom.rom_crc == 0x564F7161u ||   /* Holy Diver, other dump (submapper 3) */
           nes->nes_rom.rom_crc == 0xE2AB58BAu;     /* kept from the previous heuristic */
}

static void nes_mapper_init(nes_t* nes) {
    nes_load_prgrom_16k(nes, 0, 0);
    nes_load_prgrom_16k(nes, 1, (uint16_t)(nes->nes_rom.prg_rom_size - 1));
    if (nes->nes_rom.chr_rom_size > 0) {
        nes_load_chrrom_8k(nes, 0, 0);
    }
    nes_ppu_screen_mirrors(nes, mapper78_is_holy_diver(nes) ? NES_MIRROR_HORIZONTAL : NES_MIRROR_ONE_SCREEN0);
}

static void nes_mapper_write(nes_t* nes, uint16_t address, uint8_t data) {
    data &= nes->nes_cpu.prg_banks[(address >> 13) - 4][address & 0x1FFFu];
    nes_load_prgrom_16k(nes, 0, (uint16_t)(data & 0x07u));
    if (nes->nes_rom.chr_rom_size > 0) {
        nes_load_chrrom_8k(nes, 0, (uint16_t)((data >> 4) & 0x0Fu));
    }
    if (mapper78_is_holy_diver(nes)) {
        nes_ppu_screen_mirrors(nes, (data & 0x08u) ? NES_MIRROR_VERTICAL : NES_MIRROR_HORIZONTAL);
    } else {
        nes_ppu_screen_mirrors(nes, (data & 0x08u) ? NES_MIRROR_ONE_SCREEN1 : NES_MIRROR_ONE_SCREEN0);
    }
}

int nes_mapper78_init(nes_t* nes) {
    nes->nes_mapper.mapper_init  = nes_mapper_init;
    nes->nes_mapper.mapper_write = nes_mapper_write;
    return NES_OK;
}
