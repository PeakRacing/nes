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

#if defined(NES_TEST_MODE) && (NES_TEST_MODE == 1)

#include "nes_test.h"

#include <stdio.h>
#include <stdlib.h>

/* NES_DBG_WLOG=<file>: append "PC ADDR DATA" for every mapper write.  Opened lazily, hard capped
 * so a runaway log cannot fill the disk.  Called from the CPU write path (test builds only). */
/* NES_DBG_RLOG=<file>: "ADDR PC" for every CPU read.  Address+PC only (cheap, no value plumbing);
 * the frequency distribution is what identifies a polling loop. */
void nes_test_rlog(nes_t* nes, uint16_t address, uint8_t value, uint16_t pc) {
    static FILE* rlog;
    static long lines;
    static int tried;
    /* PRG-PROBE: on the first fetch from the start of the $8000 window, dump the bank wiring
     * (PRG buffer, the four CPU slots, slot 0's offset) — enough to tell "wrong bank mapped"
     * from "wrong bytes read".  AD&D (Hillsfar) tripped this probe while its MMC1 write counter
     * was mis-loading registers; see the 2026-09-29 note in AGENTS.md. */
    {
        static int probed;
        if (!probed && address == 0x8000u && pc >= 0x8000u && pc <= 0x8030u) {
            probed = 1;
            fprintf(stderr, "[PRG-PROBE] pc=%04X value=%02X prg_rom=%p prg_rom_size=%u\n",
                    (unsigned)pc, (unsigned)value, (void*)nes->nes_rom.prg_rom,
                    (unsigned)nes->nes_rom.prg_rom_size);
            fprintf(stderr, "[PRG-PROBE] prg_banks[0]=%p [1]=%p [2]=%p [3]=%p\n",
                    (void*)nes->nes_cpu.prg_banks[0], (void*)nes->nes_cpu.prg_banks[1],
                    (void*)nes->nes_cpu.prg_banks[2], (void*)nes->nes_cpu.prg_banks[3]);
            if (nes->nes_rom.prg_rom) {
                fprintf(stderr, "[PRG-PROBE] offset(banks[0]) = %ld  (bank 单位=16KB，应为 9*16384=147456)\n",
                        (long)(nes->nes_cpu.prg_banks[0] - nes->nes_rom.prg_rom));
            }
        }
    }
    if (!tried) {
        const char* path = getenv("NES_DBG_RLOG");
        if (path != NULL && *path != '\0') {
            rlog = fopen(path, "w");
        }
        tried = 1;
    }
    /* Keep $4016/$4017 (controller reads) - they reveal "waiting for input" loops. */
    if (address < 0x2000u) {
        return;
    }
    if (address >= 0x4000u && address < 0x6000u && address != 0x4016u && address != 0x4017u) {
        return;
    }
    /* $2002 is polled thousands of times per frame by every game; it drowns the log. */
    if (address == 0x2002u) {
        return;
    }
    if (rlog == NULL || lines >= 200000) {
        return;
    }
    fprintf(rlog, "%04X %02X %04X\n", (unsigned)address, (unsigned)value, (unsigned)pc);
    lines++;
}

/* NES_DBG_MMC1=<file>: the MMC1 serial port in the raw — every CPU write into $8000-$FFFF with
 * the register its address selects, whether it resets the shifter ($80 set) or shifts one bit
 * in, and the shifter/counter state the mapper is left in.  A mis-aligned sequence is visible
 * directly: count the "shift" lines between two LOADs, or look for a LOAD on the wrong write. */
void nes_test_mmc1_log(nes_t* nes, uint16_t address, uint8_t data, uint8_t reg,
                       uint8_t reset, uint8_t loaded, uint8_t shift, uint8_t count,
                       uint8_t value, uint8_t dropped) {
    static FILE* log;
    static long lines;
    static int tried;
    static const char* const reg_name[4] = { "CTL", "CHR0", "CHR1", "PRG" };
    const char* name = reg_name[reg & 0x03u];
    if (!tried) {
        const char* path = getenv("NES_DBG_MMC1");
        if (path != NULL && *path != '\0') {
            log = fopen(path, "w");
        }
        tried = 1;
        if (log != NULL) {
            fprintf(log, "# MMC1 raw serial writes: pc addr data bit class register shift count\n");
            fprintf(log, "# model=%s  $8000-$9FFF=CTL  $A000-$BFFF=CHR0  $C000-$DFFF=CHR1  $E000-$FFFF=PRG\n",
                    nes->nes_rom.mmc1_strict ? "5-write-counter" : "sentinel-bit");
        }
    }
    if (log == NULL || lines >= 200000) {
        return;
    }
    if (loaded) {
        fprintf(log, "%6ld pc=%04X addr=%04X data=%02X bit=%u %-5s %-4s shift=%02X cnt=%u  LOAD %s=%02X%s\n",
                lines + 1, (unsigned)nes->nes_cpu.PC, (unsigned)address, (unsigned)data,
                (unsigned)(data & 1u), reset ? "RESET" : "shift", name,
                (unsigned)shift, (unsigned)count, name, (unsigned)value,
                dropped ? "  DROPPED (RMW follow-up)" : "");
    } else {
        fprintf(log, "%6ld pc=%04X addr=%04X data=%02X bit=%u %-5s %-4s shift=%02X cnt=%u%s\n",
                lines + 1, (unsigned)nes->nes_cpu.PC, (unsigned)address, (unsigned)data,
                (unsigned)(data & 1u), reset ? "RESET" : "shift", name,
                (unsigned)shift, (unsigned)count,
                dropped ? "  DROPPED (RMW follow-up)" : "");
    }
    lines++;
}

void nes_test_wlog(nes_t* nes, uint16_t address, uint8_t data, uint16_t pc) {
    static FILE* wlog;
    static long lines;
    static int tried;
    if (!tried) {
        const char* path = getenv("NES_DBG_WLOG");
        if (path != NULL && *path != '\0') {
            wlog = fopen(path, "w");
        }
        tried = 1;
    }
    /* Default focus: PPU registers only (NES_DBG_WLOG_ALL=1 widens it to mapper/SRAM too). */
    if (!getenv("NES_DBG_WLOG_ALL") && (address < 0x2000u || address >= 0x4000u)) {
        return;
    }
    /* CHR-DUMP: the first non-zero $2007 upload is when real tile data arrives. */
    if (address == 0x2007u && data != 0x00u) {
        static int dumped;
        if (!dumped) {
            const uint8_t* chr = nes->nes_rom.chr_rom;
            long nz_chr = 0, nz_nt = 0;
            unsigned i;
            dumped = 1;
            fprintf(stderr, "[CHR-DUMP] pc=%04X data=%02X chr_rom=%p chr_rom_size=%u\n",
                    (unsigned)pc, (unsigned)data, (const void*)chr,
                    (unsigned)nes->nes_rom.chr_rom_size);
            for (i = 0; chr && i < 8192u; i++) { if (chr[i]) nz_chr++; }
            fprintf(stderr, "[CHR-DUMP] CHR 非零字节 = %ld / 8192\n", nz_chr);
            for (i = 0; i < 8u; i++) {
                fprintf(stderr, "[CHR-DUMP] pattern_table[%u] offset = %ld\n", i,
                        chr ? (long)(nes->nes_ppu.pattern_table[i] - chr) : -1L);
            }
            for (i = 0; i < 2048u; i++) { if (nes->nes_ppu.name_table[i]) nz_nt++; }
            fprintf(stderr, "[CHR-DUMP] nametable[0] 非零字节 = %ld / 2048\n", nz_nt);
            {
                unsigned p;
                int distinct = 0;
                unsigned seen[64] = {0};
                fprintf(stderr, "[PALETTE-DUMP]");
                for (p = 0; p < 32u; p++) {
                    fprintf(stderr, " %02X", (unsigned)nes->nes_ppu.palette_indexes[p]);
                }
                fprintf(stderr, "\n");
                for (p = 0; p < 32u; p++) {
                    unsigned v = (unsigned)nes->nes_ppu.palette_indexes[p] & 0x3Fu;
                    if (!seen[v]) { seen[v] = 1u; distinct++; }
                }
                fprintf(stderr, "[PALETTE-DUMP] 不同颜色数 = %d / 32\n", distinct);
            }
        }
    }
    if (wlog == NULL || lines >= 200000) {
        return;
    }
    /* $2007 writes carry their destination in the PPU address register; logging it shows which
     * part of pattern/name space a game uploads to (e.g. whether it needs the full 8KB CHR-RAM). */
    if (address == 0x2007u) {
        fprintf(wlog, "%04X %02X %04X V=%04X\n", (unsigned)address, (unsigned)data,
                (unsigned)pc, (unsigned)nes->nes_ppu.v_reg);
        lines++;
        return;
    }
    fprintf(wlog, "%04X %02X %04X\n", (unsigned)address, (unsigned)data, (unsigned)pc);
    lines++;
}

static uint32_t test_crc32(const uint8_t* data, size_t size) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1u) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1u)));
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

static uint32_t test_crc32_update(uint32_t crc, const uint8_t* data, size_t size) {
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1u) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1u)));
        }
    }
    return crc;
}

static uint32_t test_hash_bytes(uint32_t hash, const uint8_t* data, size_t size) {
    for (size_t i = 0; i < size; ++i) {
        hash ^= data[i];
        hash *= 16777619u;
    }
    return hash;
}

int nes_test_cpu_prepare(nes_t* nes, uint16_t pc) {
    if (nes == NULL) return NES_ERROR;
    nes->nes_cpu.PC = pc;
    nes->nes_cpu.A = 0;
    nes->nes_cpu.X = 0;
    nes->nes_cpu.Y = 0;
    nes->nes_cpu.SP = 0xFD;
    nes->nes_cpu.P = 0x24;
    nes->nes_cpu.cycles = 0;
    nes->nes_cpu.irq_counter = 0;
    nes->nes_cpu.irq_nmi = 0;
    nes->nes_cpu.irq_nmi_delay = 0;
    nes->nes_cpu.irq_pending = 0;
    return NES_OK;
}

int nes_test_cpu_step(nes_t* nes, uint16_t* cycles) {
    if (nes == NULL || cycles == NULL) return NES_ERROR;
    nes->nes_cpu.cycles = 0;
    nes_opcode(nes, 1);
    /* nes_opcode(nes, 1) leaves the one-cycle request as residual accounting.
     * Add the request back to expose the cycles consumed by the stepped work. */
    *cycles = (uint16_t)(nes->nes_cpu.cycles + 1u);
    return NES_OK;
}

int nes_test_reset(nes_t* nes) {
    if (nes == NULL) return NES_ERROR;
    nes_cpu_reset(nes);
    return NES_OK;
}

int nes_test_ppu_tick(nes_t* nes, uint32_t ppu_cycles) {
    if (nes == NULL) return NES_ERROR;
    while (ppu_cycles >= 3u) {
        nes_opcode(nes, 1);
        ppu_cycles -= 3u;
    }
    return NES_OK;
}

int nes_test_apu_tick(nes_t* nes, uint32_t apu_frames) {
    if (nes == NULL) return NES_ERROR;
#if (NES_ENABLE_SOUND == 1)
    while (apu_frames--) nes_apu_frame(nes);
#else
    (void)apu_frames;
#endif
    return NES_OK;
}

int nes_test_reset_deterministic(nes_t* nes) {
    if (nes == NULL) return NES_ERROR;

    const uint32_t banks_before = nes_test_bank_hash(nes, 3, 7);
    nes_test_cpu_prepare(nes, 0x1234);
    nes_cpu_reset(nes);
    const uint8_t pc_low_1 = (uint8_t)nes->nes_cpu.PC;
    const uint8_t pc_high_1 = (uint8_t)(nes->nes_cpu.PC >> 8);
    const uint8_t sp_1 = nes->nes_cpu.SP;
    const uint8_t p_1 = nes->nes_cpu.P;

    nes_test_cpu_prepare(nes, 0x1234);
    nes_cpu_reset(nes);
    const uint8_t pc_low_2 = (uint8_t)nes->nes_cpu.PC;
    const uint8_t pc_high_2 = (uint8_t)(nes->nes_cpu.PC >> 8);
    const uint8_t sp_2 = nes->nes_cpu.SP;
    const uint8_t p_2 = nes->nes_cpu.P;

    return (pc_low_1 == pc_low_2 && pc_high_1 == pc_high_2 &&
            sp_1 == sp_2 && p_1 == p_2 &&
            banks_before == nes_test_bank_hash(nes, 3, 7)) ? NES_OK : NES_ERROR;
}

static uint32_t test_frame_budget;

/* ------------------------------------------------------------------ */
/* Hot path profiling                                                  */
/* ------------------------------------------------------------------ */

#if defined(NES_TEST_PROFILE) && (NES_TEST_PROFILE == 1)
static nes_test_profile_t test_profile;
static uint64_t test_profile_region_start[NES_PROF_COUNT];
static int test_profile_active;
static int test_profile_regions = 1;

void nes_test_profile_begin(nes_t* nes) {
    (void)nes;
    nes_memset(&test_profile, 0, sizeof(test_profile));
    nes_memset(test_profile_region_start, 0, sizeof(test_profile_region_start));
    test_profile_active = 1;
}

const nes_test_profile_t* nes_test_profile_get(void) {
    return &test_profile;
}

void nes_test_profile_regions(int enable) {
    test_profile_regions = enable ? 1 : 0;
}

int nes_test_profile_regions_enabled(void) {
    return test_profile_regions;
}

void nes_test_profile_region_begin(nes_t* nes, int region) {
    (void)nes;
    if (!test_profile_active || (!test_profile_regions && region != NES_PROF_FRAME)) return;
    if (region < 0 || region >= NES_PROF_COUNT) return;
    test_profile_region_start[region] = nes_test_time_us();
}

void nes_test_profile_region_end(nes_t* nes, int region) {
    uint64_t delta;
    (void)nes;
    if (!test_profile_active || (!test_profile_regions && region != NES_PROF_FRAME)) return;
    if (region < 0 || region >= NES_PROF_COUNT) return;
    delta = nes_test_time_us() - test_profile_region_start[region];
    if (region == NES_PROF_FRAME) {
        test_profile.frames++;
        test_profile.frame_us += delta;
        if (delta > test_profile.frame_max_us) test_profile.frame_max_us = delta;
    } else {
        test_profile.region_us[region] += delta;
    }
    test_profile.region_calls[region]++;
}

void nes_test_profile_stream(nes_t* nes, int chr, int hit) {
    (void)nes;
    if (!test_profile_active) return;
    if (chr) {
        if (hit) test_profile.stream_chr_hit++; else test_profile.stream_chr_miss++;
    } else {
        if (hit) test_profile.stream_prg_hit++; else test_profile.stream_prg_miss++;
    }
}

#endif

void nes_test_frame_tick(nes_t* nes) {
    if (nes == NULL || test_frame_budget == 0u) return;
    if (--test_frame_budget == 0u) nes->nes_quit = 1u;
}
int nes_test_run_frames(nes_t* nes, uint32_t frames) {
    if (nes == NULL || frames == 0u) return NES_ERROR;
    nes->nes_quit = 0u;
    test_frame_budget = frames;
    nes_run(nes);
    test_frame_budget = 0u;
    return NES_OK;
}

uint32_t nes_test_frame_hash(const nes_t* nes) {
    if (nes == NULL) return 0;
    uint32_t hash = 2166136261u;
    hash = test_hash_bytes(hash, (const uint8_t*)nes->nes_draw_data,
                           sizeof(nes->nes_draw_data));
    return hash;
}

nes_test_audio_stats_t nes_test_audio_stats(const nes_t* nes) {
    nes_test_audio_stats_t stats = {0};
    if (nes == NULL) return stats;
#if (NES_ENABLE_SOUND == 1)
    const nes_apu_t* apu = &nes->nes_apu;
    stats.frames = (uint32_t)(apu->clock_count / 4u);
    stats.samples = NES_APU_SAMPLE_PER_SYNC;
    stats.min_sample = 255u;
    stats.max_sample = 0u;
    stats.checksum = 2166136261u;
    for (size_t i = 0; i < NES_APU_SAMPLE_PER_SYNC; ++i) {
        const uint32_t sample = apu->sample_buffer[i];
        if (sample < stats.min_sample) stats.min_sample = sample;
        if (sample > stats.max_sample) stats.max_sample = sample;
        stats.sum_sample += sample;
        stats.sum_sample_sq += (uint64_t)sample * sample;
        stats.checksum = test_hash_bytes(stats.checksum, (const uint8_t*)&sample, 1);
    }
#endif
    return stats;
}

int nes_test_bank_check(nes_t* nes) {
    if (nes == NULL || nes->nes_rom.prg_rom == NULL || nes->nes_cpu.prg_banks[0] == NULL) {
        return NES_ERROR;
    }
    for (int i = 0; i < 4; ++i) {
        if (nes->nes_cpu.prg_banks[i] == NULL) return NES_ERROR;
    }
    for (int i = 0; i < 8; ++i) {
        if (nes->nes_ppu.pattern_table[i] == NULL) return NES_ERROR;
    }
    return NES_OK;
}

uint32_t nes_test_bank_hash(nes_t* nes, uint8_t cpu_slot, uint8_t ppu_slot) {
    if (nes == NULL || cpu_slot >= 4u || ppu_slot >= 8u ||
        nes->nes_cpu.prg_banks[cpu_slot] == NULL ||
        nes->nes_ppu.pattern_table[ppu_slot] == NULL) return 0u;
    uint32_t hash = 2166136261u;
    hash = test_hash_bytes(hash, nes->nes_cpu.prg_banks[cpu_slot], 8192u);
    hash = test_hash_bytes(hash, nes->nes_ppu.pattern_table[ppu_slot], 1024u);
    return hash;
}

uint32_t nes_test_bank_crc(nes_t* nes) {
    if (nes == NULL || nes->nes_rom.prg_rom_size == 0u) return 0u;
    uint32_t crc = 0xFFFFFFFFu;
    const size_t prg_size = (size_t)nes->nes_rom.prg_rom_size * PRG_ROM_UNIT_SIZE;
    const size_t chr_size = (size_t)nes->nes_rom.chr_rom_size * CHR_ROM_UNIT_SIZE;
#if (NES_ROM_STREAM == 1)
    uint8_t buffer[1024];
    if (nes->nes_rom.rom_file == NULL) {
        /* Image loaded from memory: hash the buffers directly, like the
         * non-streaming build does. */
        if (nes->nes_rom.prg_rom == NULL) return 0u;
        crc = test_crc32_update(crc, nes->nes_rom.prg_rom, prg_size);
        if (chr_size) {
            if (nes->nes_rom.chr_rom == NULL) return 0u;
            crc = test_crc32_update(crc, nes->nes_rom.chr_rom, chr_size);
        }
        return crc ^ 0xFFFFFFFFu;
    }
    nes_fseek(nes->nes_rom.rom_file, nes->nes_rom.prg_data_offset, SEEK_SET);
    for (size_t done = 0; done < prg_size; done += sizeof(buffer)) {
        const size_t count = (prg_size - done < sizeof(buffer)) ? prg_size - done : sizeof(buffer);
        if (nes_fread(buffer, 1, count, nes->nes_rom.rom_file) != count) return 0u;
        crc = test_crc32_update(crc, buffer, count);
    }
    nes_fseek(nes->nes_rom.rom_file, nes->nes_rom.chr_data_offset, SEEK_SET);
    for (size_t done = 0; done < chr_size; done += sizeof(buffer)) {
        const size_t count = (chr_size - done < sizeof(buffer)) ? chr_size - done : sizeof(buffer);
        if (nes_fread(buffer, 1, count, nes->nes_rom.rom_file) != count) return 0u;
        crc = test_crc32_update(crc, buffer, count);
    }
#else
    if (nes->nes_rom.prg_rom == NULL) return 0u;
    crc = test_crc32_update(crc, nes->nes_rom.prg_rom, prg_size);
    if (chr_size) {
        if (nes->nes_rom.chr_rom == NULL) return 0u;
        crc = test_crc32_update(crc, nes->nes_rom.chr_rom, chr_size);
    }
#endif
    return crc ^ 0xFFFFFFFFu;
}

nes_test_rom_result_t nes_test_parse_rom(const uint8_t* data, size_t size,
                                        nes_test_rom_layout_t* layout) {
    if (data == NULL || layout == NULL) return NES_TEST_ROM_INVALID_ARGUMENT;
    if (size < sizeof(nes_header_ines_t)) return NES_TEST_ROM_TRUNCATED_HEADER;
    if (nes_memcmp(data, "NES\x1a", 4) != 0) return NES_TEST_ROM_INVALID_MAGIC;

    const nes_header_ines_t* ines = (const nes_header_ines_t*)data;
    *layout = (nes_test_rom_layout_t){0};
    layout->trainer = ines->trainer;
    layout->mirroring_type = ines->mirroring;
    layout->four_screen = ines->four_screen;
    layout->save_ram = ines->save;
    layout->nes2 = (ines->identifier == 2u);
    if (layout->nes2) {
        const nes_header_nes2_t* nes2 = (const nes_header_nes2_t*)data;
        layout->prg_rom_size = (uint16_t)(((nes2->prg_rom_size_m & 0x0Fu) << 8) |
                                          nes2->prg_rom_size_l);
        layout->chr_rom_size = (uint16_t)(((nes2->chr_rom_size_m & 0x0Fu) << 8) |
                                          nes2->chr_rom_size_l);
        layout->mapper_number = (uint16_t)(((nes2->mapper_number_h & 0x0Fu) << 8) |
                                           ((nes2->mapper_number_m & 0x0Fu) << 4) |
                                           (nes2->mapper_number_l & 0x0Fu));
        layout->submapper = nes2->submapper;
    } else {
        layout->prg_rom_size = ines->prg_rom_size;
        layout->chr_rom_size = ines->chr_rom_size;
        if (ines->Reserved[1] | ines->Reserved[2] | ines->Reserved[3] | ines->Reserved[4]) {
            layout->mapper_number = ines->mapper_number_l;
        } else {
            layout->mapper_number = (uint16_t)(ines->mapper_number_l |
                                               ((uint16_t)ines->mapper_number_h << 4));
        }
    }

    layout->trainer_offset = sizeof(nes_header_ines_t);
    layout->prg_offset = layout->trainer_offset + (layout->trainer ? TRAINER_SIZE : 0u);
    layout->chr_offset = layout->prg_offset + (size_t)PRG_ROM_UNIT_SIZE * layout->prg_rom_size;
    layout->total_size = layout->chr_offset + (size_t)CHR_ROM_UNIT_SIZE * layout->chr_rom_size;

    if (layout->nes2 && (layout->prg_rom_size == 0u ||
                         layout->prg_rom_size > 0x0FFFu ||
                         layout->chr_rom_size > 0x0FFFu)) {
        return NES_TEST_ROM_UNSUPPORTED_LAYOUT;
    }
    if (layout->trainer && size < layout->prg_offset) return NES_TEST_ROM_TRUNCATED_TRAINER;
    if (layout->prg_rom_size == 0u || size < layout->chr_offset) return NES_TEST_ROM_TRUNCATED_PRG;
    if (size < layout->total_size) return NES_TEST_ROM_TRUNCATED_CHR;
    layout->crc32 = test_crc32(data + layout->prg_offset,
                               layout->total_size - layout->prg_offset);
    return NES_TEST_ROM_OK;
}

nes_test_rom_result_t nes_test_load_rom_checked(nes_t* nes, const uint8_t* data,
                                               size_t size,
                                               nes_test_rom_layout_t* layout) {
    if (nes == NULL) return NES_TEST_ROM_INVALID_ARGUMENT;
    nes_test_rom_layout_t parsed = {0};
    const nes_test_rom_result_t result = nes_test_parse_rom(data, size, &parsed);
    if (result != NES_TEST_ROM_OK) return result;
    if (nes_load_rom(nes, data) != NES_OK) return NES_TEST_ROM_LOAD_FAILED;
    if (parsed.trainer && nes->nes_rom.sram) {
        nes_memcpy(nes->nes_rom.sram + 0x1000, data + parsed.trainer_offset, TRAINER_SIZE);
    }
    if (layout) *layout = parsed;
    return NES_TEST_ROM_OK;
}

#endif

static uint16_t trace_at = 0xC110u;
static long trace_n = 2000L;

/* Per-instruction trace: armed when the PC hits NES_DBG_TRACE_AT, then logs N instructions. */
void nes_test_trace(nes_t* nes, uint16_t pc) {
    static FILE* trace;
    static int tried;
    static int armed;
    static long left;
    static long total;
    if (!tried) {
        const char* path = getenv("NES_DBG_TRACE");
        tried = 1;
        if (path != NULL && *path != '\0') {
            trace = fopen(path, "w");
            if (trace != NULL) {
                const char* at;
                armed = 0;
                at = getenv("NES_DBG_TRACE_AT");
                trace_at = (uint16_t)((at != NULL && *at != '\0') ? strtoul(at, NULL, 16) : 0xC110u);
                at = getenv("NES_DBG_TRACE_N");
                trace_n = (at != NULL && *at != '\0') ? strtol(at, NULL, 10) : 2000L;
            }
        }
    }
    if (trace == NULL) return;
    if (!armed) {
        if (pc != trace_at) return;
        armed = 1;
        left = trace_n;
        fprintf(trace, "--- armed at pc=%04X ---\n", (unsigned)pc);
    }
    if (left > 0) {
        fprintf(trace, "%04X A=%02X X=%02X Y=%02X SP=%02X P=%02X CYC=%llu\n",
                (unsigned)pc, (unsigned)nes->nes_cpu.A, (unsigned)nes->nes_cpu.X, (unsigned)nes->nes_cpu.Y,
                (unsigned)nes->nes_cpu.SP, (unsigned)(nes->nes_cpu.P & 0xFFu),
                (unsigned long long)total++);
        left--;
        if (left == 0) {
            fprintf(trace, "--- end ---\n");
            fclose(trace);
            trace = NULL;
        }
    }
}
