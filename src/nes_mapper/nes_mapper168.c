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
 * Racermate (iNES mapper 168) — "Racermate Challenge II".
 *
 * Register map follows Mesen's Unlicensed/Racermate.h:
 *   $8000-$BFFF write: bits[7:6] = 16KB PRG bank at $8000-$BFFF
 *                      bits[3:0] = 4KB CHR page at $1000-$1FFF
 *   $C000-$FFFF write: reload the IRQ counter with 1024 and acknowledge the IRQ
 *   IRQ: a CPU cycle counter that fires every 1024 CPU cycles (external IRQ source)
 *   Power on: $C000-$FFFF = last 16KB PRG bank, $0000-$0FFF = CHR page 0
 *
 * The board has no CHR-ROM: it carries 64KB of battery backed CHR-RAM (the game uploads
 * its tiles through $2007 and the RAM keeps them across power cycles), so this mapper owns
 * that RAM and hands the battery content to the save-state machinery.
 */

#define MAPPER168_CHR_RAM_SIZE  (0x10000)   /* 64KB, sixteen 4KB pages */
#define MAPPER168_IRQ_RELOAD    (1024u)

typedef struct mapper168_register {
    uint16_t irq_counter;
    uint8_t* chr_ram;                       /* 64KB board RAM */
} mapper168_register_t;

static void mapper168_update_chr(nes_t* nes, uint8_t page) {
    mapper168_register_t* r = (mapper168_register_t*)nes->nes_mapper.mapper_register;
    uint8_t p;
    if (r == NULL || r->chr_ram == NULL) {
        return;
    }
    p = (uint8_t)(page & 0x0Fu);
    for (uint8_t i = 0; i < 4u; i++) {
        nes->nes_ppu.pattern_table[i] = r->chr_ram + (uint32_t)1024u * i;
        nes->nes_ppu.pattern_table[4 + i] = r->chr_ram + (uint32_t)4096u * p + (uint32_t)1024u * i;
    }
}

static void nes_mapper_init(nes_t* nes) {
    mapper168_register_t* r = (mapper168_register_t*)nes_mapper_register_alloc(nes, (uint16_t)sizeof(mapper168_register_t));
    if (r == NULL) {
        return;
    }
    nes->nes_mapper.mapper_register = r;
    r->chr_ram = (uint8_t*)nes_malloc(MAPPER168_CHR_RAM_SIZE);
    if (r->chr_ram == NULL) {
        return;
    }
    nes_memset(r->chr_ram, 0, MAPPER168_CHR_RAM_SIZE);
    r->irq_counter = MAPPER168_IRQ_RELOAD;

    nes_load_prgrom_16k(nes, 0, 0);
    nes_load_prgrom_16k(nes, 1, (uint16_t)(nes->nes_rom.prg_rom_size - 1u)); /* fixed last bank */
    mapper168_update_chr(nes, 0);
}

static void nes_mapper_deinit(nes_t* nes) {
    mapper168_register_t* r = (mapper168_register_t*)nes->nes_mapper.mapper_register;
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
    mapper168_register_t* r = (mapper168_register_t*)nes->nes_mapper.mapper_register;
    if (r == NULL) {
        return;
    }
    switch (address & 0xC000u) {
    case 0x8000:
        nes_load_prgrom_16k(nes, 0, (uint16_t)((data >> 6) & 0x03u));
        mapper168_update_chr(nes, (uint8_t)(data & 0x0Fu));
        break;
    case 0xC000:
        r->irq_counter = MAPPER168_IRQ_RELOAD;
        nes->nes_cpu.irq_pending = 0;
        break;
    default:
        break;
    }
}

/* 1024 CPU cycle IRQ: the counter is reloaded and the IRQ line asserted when it expires. */
static void nes_mapper_cpu_clock(nes_t* nes, uint16_t cycles) {
    mapper168_register_t* r = (mapper168_register_t*)nes->nes_mapper.mapper_register;
    if (r == NULL) {
        return;
    }
    if (r->irq_counter <= cycles) {
        r->irq_counter = (uint16_t)(MAPPER168_IRQ_RELOAD - (cycles - r->irq_counter));
        nes_cpu_irq(nes);
    } else {
        r->irq_counter = (uint16_t)(r->irq_counter - cycles);
    }
}

/* --- save state: the 64KB battery backed board RAM lives outside the core --- */

static void nes_mapper_state_reapply(nes_t* nes) {
    mapper168_register_t* r = (mapper168_register_t*)nes->nes_mapper.mapper_register;
    (void)r;
    mapper168_update_chr(nes, 0);
}

static int nes_mapper_state_save(nes_t* nes, nes_state_writer_t* writer) {
    mapper168_register_t* r = (mapper168_register_t*)nes->nes_mapper.mapper_register;
    if (r == NULL || r->chr_ram == NULL) {
        return NES_OK;
    }
    if (nes_state_write(writer, r->chr_ram, MAPPER168_CHR_RAM_SIZE) != NES_OK) {
        return NES_STATE_ERR_IO;
    }
    return NES_OK;
}

static int nes_mapper_state_load(nes_t* nes, nes_state_reader_t* reader) {
    mapper168_register_t* r = (mapper168_register_t*)nes->nes_mapper.mapper_register;
    if (r == NULL || r->chr_ram == NULL) {
        return NES_OK;
    }
    if (nes_state_read(reader, r->chr_ram, MAPPER168_CHR_RAM_SIZE) != NES_OK) {
        return NES_STATE_ERR_IO;
    }
    return NES_OK;
}

int nes_mapper168_init(nes_t* nes) {
    nes->nes_mapper.mapper_init         = nes_mapper_init;
    nes->nes_mapper.mapper_deinit       = nes_mapper_deinit;
    nes->nes_mapper.mapper_write        = nes_mapper_write;
    nes->nes_mapper.mapper_cpu_clock    = nes_mapper_cpu_clock;
#if (NES_USE_FS == 1)
    nes->nes_mapper.mapper_state_save   = nes_mapper_state_save;
    nes->nes_mapper.mapper_state_load   = nes_mapper_state_load;
    nes->nes_mapper.mapper_state_reapply = nes_mapper_state_reapply;
#endif
    return NES_OK;
}
