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
#pragma once



#ifdef __cplusplus
    extern "C" {
#endif

#include "nes_state_io.h"

struct nes;
typedef struct nes nes_t;

/* https://www.nesdev.org/wiki/Mapper   */
typedef struct {
    void (*mapper_init)(nes_t* nes);
    void (*mapper_deinit)(nes_t* nes);
    void (*mapper_write)(nes_t* nes, uint16_t write_addr, uint8_t data);
    uint8_t (*mapper_read_prg)(nes_t* nes, uint16_t read_addr);
    void (*mapper_sram)(nes_t* nes, uint16_t write_addr, uint8_t data);
    uint8_t (*mapper_read_sram)(nes_t* nes, uint16_t read_addr);
    void (*mapper_apu)(nes_t* nes, uint16_t write_addr, uint8_t data);
    uint8_t (*mapper_read_apu)(nes_t* nes, uint16_t write_addr);
    /* Callback after CPU instructions consume cycles */
    void (*mapper_cpu_clock)(nes_t* nes, uint16_t cycles);
    /* Callback at VSync */
    void (*mapper_vsync)(nes_t* nes);
    /* Callback at HSync */
    void (*mapper_hsync)(nes_t* nes);
    /* Callback at selected PPU pattern fetches */
    void (*mapper_ppu)(nes_t* nes, uint16_t write_addr);
    uint8_t mapper_ppu_tile_min;
    uint8_t mapper_ppu_tile_max;
    /* Callback at Rendering Screen 1:BG, 0:Sprite */
    void (*mapper_render_screen)(nes_t* nes, uint8_t mode);
    /* ExRAM mode 1 (MMC5): non-NULL when exram_mode==1.
     * Each byte: bits[5:0] = 4KB CHR bank selector, bits[7:6] = palette override.
     * mapper_chr_hi holds upper CHR bank bits ($5130 bits[1:0]). */
    uint8_t* mapper_exram;
    uint8_t  mapper_chr_hi;
    void* mapper_register;
    /* Optional board battery: when set, <game>.sav stores this buffer instead of the
     * core's 8KB nes_rom.sram (used by boards whose battery IS the CHR-RAM). */
    uint8_t* mapper_battery;
    uint32_t mapper_battery_size;
    void* mapper_data;
    /* --- save state support (see inc/nes_state_io.h) ---
     * mapper_state_size is filled in by nes_mapper_register_alloc(); the two callbacks are
     * optional and carry board specific data (private RAM, IRQ counters, ...) that the core
     * cannot derive, while mapper_state_reapply rebuilds PRG/CHR/nametable pointers after a
     * state has been restored. */
    uint16_t mapper_state_size;
    int  (*mapper_state_save)(nes_t* nes, nes_state_writer_t* writer);
    int  (*mapper_state_load)(nes_t* nes, nes_state_reader_t* reader);
    void (*mapper_state_reapply)(nes_t* nes);
} nes_mapper_t;

/* Allocate the mapper register block and remember its size for save states. */
void* nes_mapper_register_alloc(nes_t* nes, uint16_t size);

/* prg rom */
int nes_load_prgrom_8k(nes_t* nes,uint8_t des, uint16_t src);
int nes_load_prgrom_16k(nes_t* nes,uint8_t des, uint16_t src);
int nes_load_prgrom_32k(nes_t* nes,uint8_t des, uint16_t src);

/* chr rom */
int nes_load_chrrom_1k(nes_t* nes,uint8_t des, uint16_t src);
int nes_load_chrrom_4k(nes_t* nes,uint8_t des, uint16_t src);
int nes_load_chrrom_8k(nes_t* nes,uint8_t des, uint16_t src);

/* mapper */
int nes_load_mapper(nes_t* nes);

#ifdef __cplusplus          
    }
#endif
