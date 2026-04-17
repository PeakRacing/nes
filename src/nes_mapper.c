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

#if (NES_ROM_STREAM == 1)

static inline uint16_t nes_cache_tick(nes_rom_info_t* rom) {
    rom->cache_tick++;
    if (rom->cache_tick == 0) {
        /* Counter wrapped — reset all timestamps */
        for (int i = 0; i < NES_PRG_CACHE_SLOTS; i++) rom->prg_cache[i].last_used = 0;
        for (int i = 0; i < NES_CHR_CACHE_SLOTS; i++) rom->chr_cache[i].last_used = 0;
        rom->cache_tick = 1;
    }
    return rom->cache_tick;
}

static inline uint8_t* nes_prg_cache_get(nes_t* nes, uint16_t src) {
    nes_rom_info_t* rom = &nes->nes_rom;
    uint16_t tick = nes_cache_tick(rom);
    /* Search for cache hit */
    for (int i = 0; i < NES_PRG_CACHE_SLOTS; i++) {
        if (rom->prg_cache[i].tag == src) {
            rom->prg_cache[i].last_used = tick;
            return rom->prg_rom + (uint32_t)8192 * i;
        }
    }
    /* Cache miss — find LRU entry not currently active */
    int lru_idx = -1;
    uint16_t lru_min = 0xFFFF;
    for (int i = 0; i < NES_PRG_CACHE_SLOTS; i++) {
        uint8_t* buf = rom->prg_rom + (uint32_t)8192 * i;
        int active = 0;
        for (int j = 0; j < 4; j++) {
            if (nes->nes_cpu.prg_banks[j] == buf) { active = 1; break; }
        }
        if (!active && rom->prg_cache[i].last_used < lru_min) {
            lru_min = rom->prg_cache[i].last_used;
            lru_idx = i;
        }
    }
    if (lru_idx < 0) lru_idx = 0; /* fallback */
    /* Load from file */
    rom->prg_cache[lru_idx].tag = src;
    rom->prg_cache[lru_idx].last_used = tick;
    uint8_t* buf = rom->prg_rom + (uint32_t)8192 * lru_idx;
    nes_fseek(rom->rom_file, rom->prg_data_offset + (long)8192 * src, SEEK_SET);
    nes_fread(buf, 8192, 1, rom->rom_file);
    return buf;
}

static inline uint8_t* nes_chr_cache_get(nes_t* nes, uint16_t src) {
    nes_rom_info_t* rom = &nes->nes_rom;
    uint16_t tick = nes_cache_tick(rom);
    /* Search for cache hit */
    for (int i = 0; i < NES_CHR_CACHE_SLOTS; i++) {
        if (rom->chr_cache[i].tag == src) {
            rom->chr_cache[i].last_used = tick;
            return rom->chr_rom + (uint32_t)1024 * i;
        }
    }
    /* Cache miss — find LRU entry not currently active */
    int lru_idx = -1;
    uint16_t lru_min = 0xFFFF;
    for (int i = 0; i < NES_CHR_CACHE_SLOTS; i++) {
        uint8_t* buf = rom->chr_rom + (uint32_t)1024 * i;
        int active = 0;
        for (int j = 0; j < 8; j++) {
            if (nes->nes_ppu.pattern_table[j] == buf) { active = 1; break; }
        }
        if (!active && rom->chr_cache[i].last_used < lru_min) {
            lru_min = rom->chr_cache[i].last_used;
            lru_idx = i;
        }
    }
    if (lru_idx < 0) lru_idx = 0; /* fallback */
    /* Load from file */
    rom->chr_cache[lru_idx].tag = src;
    rom->chr_cache[lru_idx].last_used = tick;
    uint8_t* buf = rom->chr_rom + (uint32_t)1024 * lru_idx;
    nes_fseek(rom->rom_file, rom->chr_data_offset + (long)1024 * src, SEEK_SET);
    nes_fread(buf, 1024, 1, rom->rom_file);
    return buf;
}

/* load 8k PRG-ROM from file with LRU cache */
void nes_load_prgrom_8k(nes_t* nes,uint8_t des, uint16_t src) {
    nes->nes_cpu.prg_banks[des] = nes_prg_cache_get(nes, src);
}

/* load 16k PRG-ROM from file with LRU cache */
void nes_load_prgrom_16k(nes_t* nes,uint8_t des, uint16_t src) {
    nes->nes_cpu.prg_banks[des * 2]     = nes_prg_cache_get(nes, src * 2);
    nes->nes_cpu.prg_banks[des * 2 + 1] = nes_prg_cache_get(nes, src * 2 + 1);
}

/* load 32k PRG-ROM from file with LRU cache */
void nes_load_prgrom_32k(nes_t* nes,uint8_t des, uint16_t src) {
    (void)des;
    for (int i = 0; i < 4; i++) {
        nes->nes_cpu.prg_banks[i] = nes_prg_cache_get(nes, (uint16_t)(src * 4 + i));
    }
}

/* load 1k CHR-ROM from file with LRU cache */
void nes_load_chrrom_1k(nes_t* nes,uint8_t des, uint8_t src) {
    if (nes->nes_rom.chr_rom_size) {
        nes->nes_ppu.pattern_table[des] = nes_chr_cache_get(nes, src);
    } else {
        nes->nes_ppu.pattern_table[des] = nes->nes_rom.chr_rom + (uint32_t)1024 * des;
    }
}

/* load 4k CHR-ROM from file with LRU cache */
void nes_load_chrrom_4k(nes_t* nes,uint8_t des, uint8_t src) {
    if (nes->nes_rom.chr_rom_size) {
        for (int i = 0; i < 4; i++) {
            nes->nes_ppu.pattern_table[des * 4 + i] = nes_chr_cache_get(nes, (uint16_t)(src * 4 + i));
        }
    } else {
        for (int i = 0; i < 4; i++) {
            nes->nes_ppu.pattern_table[des * 4 + i] = nes->nes_rom.chr_rom + (uint32_t)1024 * (des * 4 + i);
        }
    }
}

/* load 8k CHR-ROM from file with LRU cache */
void nes_load_chrrom_8k(nes_t* nes,uint8_t des, uint8_t src) {
    if (nes->nes_rom.chr_rom_size) {
        for (int i = 0; i < 8; i++) {
            nes->nes_ppu.pattern_table[des + i] = nes_chr_cache_get(nes, (uint16_t)(src * 8 + i));
        }
    } else {
        for (int i = 0; i < 8; i++) {
            nes->nes_ppu.pattern_table[des + i] = nes->nes_rom.chr_rom + (uint32_t)1024 * (des + i);
        }
    }
}

#else
void nes_load_prgrom_8k(nes_t* nes,uint8_t des, uint16_t src) {
    nes->nes_cpu.prg_banks[des] = nes->nes_rom.prg_rom + 8 * 1024 * src;
}

/* load 16k PRG-ROM */
void nes_load_prgrom_16k(nes_t* nes,uint8_t des, uint16_t src) {
    nes->nes_cpu.prg_banks[des * 2] = nes->nes_rom.prg_rom + 8 * 1024 * src * 2;
    nes->nes_cpu.prg_banks[des * 2 + 1] = nes->nes_rom.prg_rom + 8 * 1024 * (src * 2 + 1);
}

/* load 32k PRG-ROM */
void nes_load_prgrom_32k(nes_t* nes,uint8_t des, uint16_t src) {
    (void)des;
    nes->nes_cpu.prg_banks[0] = nes->nes_rom.prg_rom + 8 * 1024 * src * 4;
    nes->nes_cpu.prg_banks[1] = nes->nes_rom.prg_rom + 8 * 1024 * (src * 4 + 1);
    nes->nes_cpu.prg_banks[2] = nes->nes_rom.prg_rom + 8 * 1024 * (src * 4 + 2);
    nes->nes_cpu.prg_banks[3] = nes->nes_rom.prg_rom + 8 * 1024 * (src * 4 + 3);
}

/* load 1k CHR-ROM */
void nes_load_chrrom_1k(nes_t* nes,uint8_t des, uint8_t src) {
    nes->nes_ppu.pattern_table[des] = nes->nes_rom.chr_rom + 1024 * src;
}

/* load 4k CHR-ROM */
void nes_load_chrrom_4k(nes_t* nes,uint8_t des, uint8_t src) {
    for (size_t i = 0; i < 4; i++){
        nes->nes_ppu.pattern_table[des * 4 + i] = nes->nes_rom.chr_rom + 1024 * (src * 4 + i);
    }
}

/* load 8k CHR-ROM */
void nes_load_chrrom_8k(nes_t* nes,uint8_t des, uint8_t src) {
    for (size_t i = 0; i < 8; i++){
        nes->nes_ppu.pattern_table[des + i] = nes->nes_rom.chr_rom + 1024 * (src * 8 + i);
    }
}

#endif /* NES_ROM_STREAM */

#define NES_CASE_LOAD_MAPPER(mapper_id) case mapper_id: return nes_mapper##mapper_id##_init(nes)

int nes_load_mapper(nes_t* nes){
    switch (nes->nes_rom.mapper_number){
        NES_CASE_LOAD_MAPPER(0);
        NES_CASE_LOAD_MAPPER(1);
        NES_CASE_LOAD_MAPPER(2);
        NES_CASE_LOAD_MAPPER(3);
        NES_CASE_LOAD_MAPPER(4);
        NES_CASE_LOAD_MAPPER(5);
        NES_CASE_LOAD_MAPPER(7);
        NES_CASE_LOAD_MAPPER(31);
        NES_CASE_LOAD_MAPPER(71);
        NES_CASE_LOAD_MAPPER(94);
        NES_CASE_LOAD_MAPPER(177);
        NES_CASE_LOAD_MAPPER(180);
        default :
            NES_LOG_ERROR("mapper:%03d is unsupported\n",nes->nes_rom.mapper_number);
            return NES_ERROR;
    }
}
