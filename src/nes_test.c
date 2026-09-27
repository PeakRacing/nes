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
