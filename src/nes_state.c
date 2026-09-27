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

/*
 * Save states: "<rom dir>/<game name>.nessave", battery RAM: "<rom dir>/<game name>.sav".
 *
 * See docs/nessave-format.md for the on-disk layout and the section evolution policy.
 * The loader validates the whole file (header CRC, section table, payload CRC, ROM
 * identity) before it touches a single byte of machine state, so a corrupt or foreign
 * state can never leave the emulator half-restored.
 */

#include "nes.h"

#if (NES_USE_FS == 1)

#include <stdio.h>

/* Header layout (all little endian). */
#define NES_STATE_HEADER_SIZE       (32)
#define NES_STATE_HEADER_CRC_SIZE   (28)    /* bytes covered by header_crc */
#define NES_STATE_SECTION_HEADER    (12)    /* tag[4] + version + reserved[3] + len */

/* Header flag bits. */
#define NES_STATE_FLAG_SRAM         (0x01)
#define NES_STATE_FLAG_CHRRAM       (0x02)
#define NES_STATE_FLAG_APU          (0x04)
#define NES_STATE_FLAG_PARTIAL      (0x08)

/* Section version bytes (bump when a section grows by appending fields). */
#define NES_STATE_VER_INFO          (1)
#define NES_STATE_VER_BUS           (1)
#define NES_STATE_VER_CPU           (1)
#define NES_STATE_VER_PPU           (1)
#define NES_STATE_VER_APU           (1)
#define NES_STATE_VER_MAP           (1)
#define NES_STATE_VER_SRAM          (1)
#define NES_STATE_VER_CRAM          (1)

static const char* const NES_STATE_MAGIC = "NESSTATE";

/* Small local string helpers: the core only guarantees mem* wrappers. */
static size_t nes_state_strlen(const char* s) {
    size_t len = 0;
    if (s == NULL) {
        return 0;
    }
    while (s[len] != '\0') {
        len++;
    }
    return len;
}

static void nes_state_strcpy(char* dst, const char* src, size_t max) {
    size_t i = 0;
    if (dst == NULL || max == 0u) {
        return;
    }
    if (src != NULL) {
        while (src[i] != '\0' && i + 1u < max) {
            dst[i] = src[i];
            i++;
        }
    }
    dst[i] = '\0';
}
static const char* const NES_STATE_PRODUCER = "PeakRacing-nes";

/* ------------------------------------------------------------------ CRC32 (reflected) */

static const uint32_t nes_crc32_nibble[16] = {
    0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu,
    0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
    0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu,
    0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu
};

static uint32_t nes_state_crc(uint32_t crc, const uint8_t* data, uint32_t len) {
    while (len-- != 0u) {
        crc ^= *data++;
        crc = (crc >> 4) ^ nes_crc32_nibble[crc & 0x0Fu];
        crc = (crc >> 4) ^ nes_crc32_nibble[crc & 0x0Fu];
    }
    return crc;
}

#define NES_STATE_CRC_INIT   (0xFFFFFFFFu)
#define NES_STATE_CRC_FINISH(crc)   ((crc) ^ 0xFFFFFFFFu)

/* ------------------------------------------------------------------ section IO */

int nes_state_write(nes_state_writer_t* w, const void* data, uint32_t len) {
    const uint8_t* p = (const uint8_t*)data;
    if (w == NULL || (len != 0u && data == NULL)) {
        return NES_STATE_ERR_ARG;
    }
    if (w->error) {
        return NES_STATE_ERR_IO;
    }
    if (len != 0u && nes_fwrite(p, 1, len, (FILE*)w->file) != len) {
        w->error = 1;
        return NES_STATE_ERR_IO;
    }
    w->crc = nes_state_crc(w->crc, p, len);
    w->bytes += len;
    return NES_OK;
}

int nes_state_read(nes_state_reader_t* r, void* data, uint32_t len) {
    uint8_t* p = (uint8_t*)data;
    if (r == NULL || (len != 0u && data == NULL)) {
        return NES_STATE_ERR_ARG;
    }
    if (r->error) {
        return NES_STATE_ERR_IO;
    }
    if (len != 0u && nes_fread(p, 1, len, (FILE*)r->file) != len) {
        r->error = 1;
        return NES_STATE_ERR_TRUNC;
    }
    r->crc = nes_state_crc(r->crc, p, len);
    r->bytes += len;
    return NES_OK;
}

int nes_state_write_u8(nes_state_writer_t* w, uint8_t value) {
    return nes_state_write(w, &value, 1);
}

int nes_state_write_u16(nes_state_writer_t* w, uint16_t value) {
    uint8_t b[2];
    b[0] = (uint8_t)(value & 0xFFu);
    b[1] = (uint8_t)((value >> 8) & 0xFFu);
    return nes_state_write(w, b, 2);
}

int nes_state_write_u32(nes_state_writer_t* w, uint32_t value) {
    uint8_t b[4];
    b[0] = (uint8_t)(value & 0xFFu);
    b[1] = (uint8_t)((value >> 8) & 0xFFu);
    b[2] = (uint8_t)((value >> 16) & 0xFFu);
    b[3] = (uint8_t)((value >> 24) & 0xFFu);
    return nes_state_write(w, b, 4);
}

int nes_state_read_u8(nes_state_reader_t* r, uint8_t* value) {
    return nes_state_read(r, value, 1);
}

int nes_state_read_u16(nes_state_reader_t* r, uint16_t* value) {
    uint8_t b[2];
    int ret = nes_state_read(r, b, 2);
    if (ret != NES_OK) {
        return ret;
    }
    *value = (uint16_t)(b[0] | ((uint16_t)b[1] << 8));
    return NES_OK;
}

int nes_state_read_u32(nes_state_reader_t* r, uint32_t* value) {
    uint8_t b[4];
    int ret = nes_state_read(r, b, 4);
    if (ret != NES_OK) {
        return ret;
    }
    *value = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
    return NES_OK;
}

int nes_state_section_begin(nes_state_writer_t* w, const char tag[4], uint8_t version, long* len_pos) {
    uint8_t head[NES_STATE_SECTION_HEADER];
    if (w == NULL || tag == NULL || len_pos == NULL) {
        return NES_STATE_ERR_ARG;
    }
    head[0] = (uint8_t)tag[0];
    head[1] = (uint8_t)tag[1];
    head[2] = (uint8_t)tag[2];
    head[3] = (uint8_t)tag[3];
    head[4] = version;
    head[5] = 0;
    head[6] = 0;
    head[7] = 0;
    head[8] = 0;
    head[9] = 0;
    head[10] = 0;
    head[11] = 0;
    *len_pos = (long)nes_ftell((FILE*)w->file);
    if (*len_pos < 0) {
        w->error = 1;
        return NES_STATE_ERR_IO;
    }
    return nes_state_write(w, head, NES_STATE_SECTION_HEADER);
}

int nes_state_section_end(nes_state_writer_t* w, long len_pos) {
    uint8_t lenbuf[4];
    long end;
    uint32_t body_len;
    if (w == NULL || w->error) {
        return NES_STATE_ERR_IO;
    }
    end = (long)nes_ftell((FILE*)w->file);
    if (end < 0 || end < len_pos) {
        w->error = 1;
        return NES_STATE_ERR_IO;
    }
    body_len = (uint32_t)(end - len_pos) - NES_STATE_SECTION_HEADER;
    lenbuf[0] = (uint8_t)(body_len & 0xFFu);
    lenbuf[1] = (uint8_t)((body_len >> 8) & 0xFFu);
    lenbuf[2] = (uint8_t)((body_len >> 16) & 0xFFu);
    lenbuf[3] = (uint8_t)((body_len >> 24) & 0xFFu);
    if (nes_fseek((FILE*)w->file, len_pos + 8, SEEK_SET) != 0) {
        w->error = 1;
        return NES_STATE_ERR_IO;
    }
    if (nes_fwrite(lenbuf, 1, 4, (FILE*)w->file) != 4) {
        w->error = 1;
        return NES_STATE_ERR_IO;
    }
    if (nes_fseek((FILE*)w->file, end, SEEK_SET) != 0) {
        w->error = 1;
        return NES_STATE_ERR_IO;
    }
    return NES_OK;
}

/* ------------------------------------------------------------------ path helpers */

/* Build "<dir>/<base><ext>" from a ROM path: "a/b/Game.nes" -> "a/b/Game.nessave". */
static int nes_state_build_path(const char* rom_path, const char* ext, char* out, size_t out_size) {
    const char* base;
    const char* dot;
    size_t prefix;
    size_t base_len;
    size_t ext_len;
    if (rom_path == NULL || ext == NULL || out == NULL || out_size == 0u) {
        return NES_STATE_ERR_ARG;
    }
    base = rom_path;
    for (const char* p = rom_path; *p != '\0'; p++) {
        if (*p == '/' || *p == '\\') {
            base = p + 1;
        }
    }
    dot = NULL;
    for (const char* p = base; *p != '\0'; p++) {
        if (*p == '.') {
            dot = p;
        }
    }
    prefix = (size_t)(base - rom_path);
    base_len = (dot != NULL) ? (size_t)(dot - base) : nes_state_strlen(base);
    ext_len = nes_state_strlen(ext);
    if (prefix + base_len + ext_len + 1u > out_size) {
        return NES_STATE_ERR_ARG;
    }
    nes_memcpy(out, rom_path, prefix);
    nes_memcpy(out + prefix, base, base_len);
    nes_memcpy(out + prefix + base_len, ext, ext_len);
    out[prefix + base_len + ext_len] = '\0';
    return NES_OK;
}

int nes_state_default_path(nes_t* nes, char* out, size_t out_size) {
    if (nes == NULL) {
        return NES_STATE_ERR_ARG;
    }
    return nes_state_build_path(nes->nes_rom.rom_path, NES_STATE_EXT, out, out_size);
}

/* ------------------------------------------------------------------ mapping helpers */

/* Derive the bank number currently mapped into a PRG slot, or NES_STATE_SRC_PRIVATE. */
static uint8_t nes_state_prg_source(nes_t* nes, uint8_t slot, uint16_t* bank) {
    const uint8_t* base = nes->nes_rom.prg_rom;
    const uint8_t* p = nes->nes_cpu.prg_banks[slot];
    if (base == NULL || p == NULL) {
        return NES_STATE_SRC_PRIVATE;
    }
#if (NES_ROM_STREAM == 1)
    if (nes->nes_rom.rom_file != NULL) {
        size_t slot_index = (size_t)(p - base) / 8192u;
        if ((size_t)(p - base) % 8192u == 0u && slot_index < (size_t)NES_PRG_CACHE_SLOTS) {
            uint16_t tag = nes->nes_rom.prg_cache[slot_index].tag;
            if (tag != 0xFFFFu) {
                *bank = tag;
                return NES_STATE_SRC_ROM;
            }
        }
        return NES_STATE_SRC_PRIVATE;
    }
#endif
    *bank = (uint16_t)((size_t)(p - base) / 8192u);
    return NES_STATE_SRC_ROM;
}

/* Derive the 1KB page currently mapped into a CHR slot.  For CHR-RAM boards the page
 * index addresses the core owned 8KB backing store, which is saved as its own section. */
static uint8_t nes_state_chr_source(nes_t* nes, uint8_t slot, uint16_t* bank) {
    const uint8_t* base = nes->nes_rom.chr_rom;
    const uint8_t* p = nes->nes_ppu.pattern_table[slot];
    if (base == NULL || p == NULL) {
        return NES_STATE_SRC_PRIVATE;
    }
    if (nes->nes_rom.chr_rom_size == 0) {
        size_t index = (size_t)(p - base) / 1024u;
        if ((size_t)(p - base) % 1024u == 0u && index < 8u) {
            *bank = (uint16_t)index;
            return NES_STATE_SRC_RAM;
        }
        return NES_STATE_SRC_PRIVATE;
    }
#if (NES_ROM_STREAM == 1)
    if (nes->nes_rom.rom_file != NULL) {
        size_t slot_index = (size_t)(p - base) / 1024u;
        if ((size_t)(p - base) % 1024u == 0u && slot_index < (size_t)NES_CHR_CACHE_SLOTS) {
            uint16_t tag = nes->nes_rom.chr_cache[slot_index].tag;
            if (tag != 0xFFFFu) {
                *bank = tag;
                return NES_STATE_SRC_ROM;
            }
        }
        return NES_STATE_SRC_PRIVATE;
    }
#endif
    *bank = (uint16_t)((size_t)(p - base) / 1024u);
    return NES_STATE_SRC_ROM;
}

/* Nametables normally point at one of the four 1KB PPU VRAM pages; mappers that serve
 * them from CHR-ROM/ExRAM (Namco 163, MMC5, Sunsoft-4 ...) are rebuilt by their
 * mapper_state_reapply callback instead. */
static uint8_t nes_state_nt_source(nes_t* nes, uint8_t slot, uint16_t* page) {
    const uint8_t* base = &nes->nes_ppu.ppu_vram[0][0];
    const uint8_t* p = nes->nes_ppu.name_table[slot];
    if (p == NULL) {
        return NES_STATE_SRC_PRIVATE;
    }
    if (p >= base && p < base + 4096) {
        size_t index = (size_t)(p - base) / 1024u;
        if ((size_t)(p - base) % 1024u == 0u) {
            *page = (uint16_t)index;
            return NES_STATE_SRC_VRAM;
        }
    }
    return NES_STATE_SRC_PRIVATE;
}

/* ------------------------------------------------------------------ save */

static int nes_state_write_info(nes_t* nes, nes_state_writer_t* w, uint8_t* partial) {
    long len_pos = 0;
    char producer[16] = {0};
    char version[16] = {0};
    const char* name = nes->nes_rom.rom_path;
    const char* base = name;
    size_t name_len = 0;
    int ret;
    for (const char* p = name; *p != '\0'; p++) {
        if (*p == '/' || *p == '\\') {
            base = p + 1;
        }
    }
    name_len = nes_state_strlen(base);
    if (name_len > 96u) {
        name_len = 96u;
    }
    nes_state_strcpy(producer, NES_STATE_PRODUCER, sizeof(producer));
    nes_state_strcpy(version, NES_VERSION_STRING, sizeof(version));

    ret = nes_state_section_begin(w, NES_STATE_TAG_INFO, NES_STATE_VER_INFO, &len_pos);
    if (ret != NES_OK) {
        return ret;
    }
    (void)partial;
    if ((ret = nes_state_write(w, producer, sizeof(producer))) != NES_OK) return ret;
    if ((ret = nes_state_write(w, version, sizeof(version))) != NES_OK) return ret;
    if ((ret = nes_state_write_u16(w, nes->nes_rom.mapper_number)) != NES_OK) return ret;
    if ((ret = nes_state_write_u16(w, nes->nes_rom.prg_rom_size)) != NES_OK) return ret;
    if ((ret = nes_state_write_u16(w, nes->nes_rom.chr_rom_size)) != NES_OK) return ret;
    if ((ret = nes_state_write_u32(w, nes->nes_rom.rom_crc)) != NES_OK) return ret;
    if ((ret = nes_state_write_u32(w, 0u)) != NES_OK) return ret;   /* timestamp: no RTC */
    if ((ret = nes_state_write_u16(w, (uint16_t)name_len)) != NES_OK) return ret;
    if (name_len != 0u && (ret = nes_state_write(w, base, (uint32_t)name_len)) != NES_OK) return ret;
    return nes_state_section_end(w, len_pos);
}

static int nes_state_write_bus(nes_t* nes, nes_state_writer_t* w, uint8_t* partial) {
    long len_pos = 0;
    uint8_t prg_src[4];
    uint16_t prg_bank[4];
    uint8_t chr_src[8];
    uint16_t chr_bank[8];
    uint8_t nt_src[4];
    uint16_t nt_page[4];
    int ret;

    for (uint8_t i = 0; i < 4u; i++) {
        prg_bank[i] = 0;
        prg_src[i] = nes_state_prg_source(nes, i, &prg_bank[i]);
    }
    for (uint8_t i = 0; i < 8u; i++) {
        chr_bank[i] = 0;
        chr_src[i] = nes_state_chr_source(nes, i, &chr_bank[i]);
    }
    for (uint8_t i = 0; i < 4u; i++) {
        nt_page[i] = 0;
        nt_src[i] = nes_state_nt_source(nes, i, &nt_page[i]);
    }

    ret = nes_state_section_begin(w, NES_STATE_TAG_BUS, NES_STATE_VER_BUS, &len_pos);
    if (ret != NES_OK) {
        return ret;
    }
    if ((ret = nes_state_write_u16(w, nes->scanline)) != NES_OK) return ret;
#if (NES_FRAME_SKIP != 0)
    if ((ret = nes_state_write_u8(w, nes->nes_frame_skip_count)) != NES_OK) return ret;
#else
    if ((ret = nes_state_write_u8(w, 0u)) != NES_OK) return ret;
#endif
    if ((ret = nes_state_write_u8(w, nes->nes_rom.four_screen)) != NES_OK) return ret;
    if ((ret = nes_state_write(w, prg_src, 4)) != NES_OK) return ret;
    for (uint8_t i = 0; i < 4u; i++) {
        if ((ret = nes_state_write_u16(w, prg_bank[i])) != NES_OK) return ret;
        if (prg_src[i] == NES_STATE_SRC_PRIVATE) *partial = 1u;
    }
    if ((ret = nes_state_write(w, chr_src, 8)) != NES_OK) return ret;
    for (uint8_t i = 0; i < 8u; i++) {
        if ((ret = nes_state_write_u16(w, chr_bank[i])) != NES_OK) return ret;
        if (chr_src[i] == NES_STATE_SRC_PRIVATE) *partial = 1u;
    }
    if ((ret = nes_state_write(w, nt_src, 4)) != NES_OK) return ret;
    for (uint8_t i = 0; i < 4u; i++) {
        if ((ret = nes_state_write_u16(w, nt_page[i])) != NES_OK) return ret;
        if (nt_src[i] == NES_STATE_SRC_PRIVATE) *partial = 1u;
    }
    if (nes->nes_mapper.mapper_state_save == NULL) {
        /* Without a callback the mapper private pages cannot be rebuilt exactly. */
        for (uint8_t i = 0; i < 4u; i++) {
            if (prg_src[i] == NES_STATE_SRC_PRIVATE) *partial = 1u;
        }
    }
    return nes_state_section_end(w, len_pos);
}

static int nes_state_write_cpu(nes_t* nes, nes_state_writer_t* w) {
    long len_pos = 0;
    int ret;
    ret = nes_state_section_begin(w, NES_STATE_TAG_CPU, NES_STATE_VER_CPU, &len_pos);
    if (ret != NES_OK) return ret;
    if ((ret = nes_state_write_u8(w, nes->nes_cpu.A)) != NES_OK) return ret;
    if ((ret = nes_state_write_u8(w, nes->nes_cpu.X)) != NES_OK) return ret;
    if ((ret = nes_state_write_u8(w, nes->nes_cpu.Y)) != NES_OK) return ret;
    if ((ret = nes_state_write_u8(w, nes->nes_cpu.SP)) != NES_OK) return ret;
    if ((ret = nes_state_write_u16(w, nes->nes_cpu.PC)) != NES_OK) return ret;
    if ((ret = nes_state_write_u8(w, nes->nes_cpu.P)) != NES_OK) return ret;
    if ((ret = nes_state_write_u8(w, nes->nes_cpu.irq_counter)) != NES_OK) return ret;
    if ((ret = nes_state_write_u8(w, nes->nes_cpu.irq_nmi)) != NES_OK) return ret;
    if ((ret = nes_state_write_u8(w, nes->nes_cpu.irq_nmi_delay)) != NES_OK) return ret;
    if ((ret = nes_state_write_u8(w, nes->nes_cpu.irq_pending)) != NES_OK) return ret;
    if ((ret = nes_state_write_u8(w, nes->nes_cpu.opcode)) != NES_OK) return ret;
    if ((ret = nes_state_write_u8(w, 0u)) != NES_OK) return ret;
    if ((ret = nes_state_write_u16(w, nes->nes_cpu.cycles)) != NES_OK) return ret;
    if ((ret = nes_state_write_u16(w, nes->nes_cpu.joypad.joypad)) != NES_OK) return ret;
    if ((ret = nes_state_write(w, nes->nes_cpu.cpu_ram, NES_CPU_RAM_SIZE)) != NES_OK) return ret;
    return nes_state_section_end(w, len_pos);
}

static int nes_state_write_ppu(nes_t* nes, nes_state_writer_t* w) {
    long len_pos = 0;
    int ret;
    ret = nes_state_section_begin(w, NES_STATE_TAG_PPU, NES_STATE_VER_PPU, &len_pos);
    if (ret != NES_OK) return ret;
    if ((ret = nes_state_write_u8(w, nes->nes_ppu.ppu_ctrl)) != NES_OK) return ret;
    if ((ret = nes_state_write_u8(w, nes->nes_ppu.ppu_mask)) != NES_OK) return ret;
    if ((ret = nes_state_write_u8(w, nes->nes_ppu.ppu_status)) != NES_OK) return ret;
    if ((ret = nes_state_write_u8(w, (uint8_t)(nes->nes_ppu.x | (nes->nes_ppu.w << 3)))) != NES_OK) return ret;
    if ((ret = nes_state_write_u8(w, nes->nes_ppu.oam_addr)) != NES_OK) return ret;
    if ((ret = nes_state_write_u8(w, nes->nes_ppu.buffer)) != NES_OK) return ret;
    if ((ret = nes_state_write_u16(w, nes->nes_ppu.v_reg)) != NES_OK) return ret;
    if ((ret = nes_state_write_u16(w, nes->nes_ppu.t_reg)) != NES_OK) return ret;
    if ((ret = nes_state_write(w, nes->nes_ppu.ppu_vram, NES_PPU_VRAM_SIZE)) != NES_OK) return ret;
    if ((ret = nes_state_write(w, nes->nes_ppu.oam_data, NES_PPU_OAM_SIZE)) != NES_OK) return ret;
    if ((ret = nes_state_write(w, nes->nes_ppu.palette_indexes, 0x20)) != NES_OK) return ret;
    return nes_state_section_end(w, len_pos);
}

#if (NES_ENABLE_SOUND == 1)
static int nes_state_write_apu(nes_t* nes, nes_state_writer_t* w, uint8_t* flags) {
    long len_pos = 0;
    uint32_t size = (uint32_t)sizeof(nes_apu_t);
    int ret;
    ret = nes_state_section_begin(w, NES_STATE_TAG_APU, NES_STATE_VER_APU, &len_pos);
    if (ret != NES_OK) return ret;
    if ((ret = nes_state_write_u32(w, size)) != NES_OK) return ret;
    if ((ret = nes_state_write(w, &nes->nes_apu, size)) != NES_OK) return ret;
    ret = nes_state_section_end(w, len_pos);
    if (ret == NES_OK) {
        *flags |= NES_STATE_FLAG_APU;
    }
    return ret;
}
#endif

static int nes_state_write_mapper(nes_t* nes, nes_state_writer_t* w, uint8_t* flags) {
    long len_pos = 0;
    uint16_t reg_size = nes->nes_mapper.mapper_state_size;
    int ret;
    if (reg_size == 0u && nes->nes_mapper.mapper_state_save == NULL) {
        return NES_OK;      /* nothing board specific to store (NROM and friends) */
    }
    ret = nes_state_section_begin(w, NES_STATE_TAG_MAP, NES_STATE_VER_MAP, &len_pos);
    if (ret != NES_OK) return ret;
    if ((ret = nes_state_write_u16(w, reg_size)) != NES_OK) return ret;
    if (reg_size != 0u && nes->nes_mapper.mapper_register != NULL) {
        if ((ret = nes_state_write(w, nes->nes_mapper.mapper_register, reg_size)) != NES_OK) return ret;
    } else {
        *flags |= NES_STATE_FLAG_PARTIAL;
    }
    if (nes->nes_mapper.mapper_state_save != NULL) {
        ret = nes->nes_mapper.mapper_state_save(nes, w);
        if (ret != NES_OK) return ret;
    }
    return nes_state_section_end(w, len_pos);
}

static int nes_state_write_ram(nes_t* nes, nes_state_writer_t* w, uint8_t* flags) {
    long len_pos = 0;
    int ret;
    if (nes->nes_rom.sram != NULL) {
        ret = nes_state_section_begin(w, NES_STATE_TAG_SRAM, NES_STATE_VER_SRAM, &len_pos);
        if (ret != NES_OK) return ret;
        if ((ret = nes_state_write_u32(w, SRAM_SIZE)) != NES_OK) return ret;
        if ((ret = nes_state_write(w, nes->nes_rom.sram, SRAM_SIZE)) != NES_OK) return ret;
        if ((ret = nes_state_section_end(w, len_pos)) != NES_OK) return ret;
        *flags |= NES_STATE_FLAG_SRAM;
    }
    if (nes->nes_rom.chr_rom_size == 0 && nes->nes_rom.chr_rom != NULL) {
        ret = nes_state_section_begin(w, NES_STATE_TAG_CRAM, NES_STATE_VER_CRAM, &len_pos);
        if (ret != NES_OK) return ret;
        if ((ret = nes_state_write_u32(w, CHR_ROM_UNIT_SIZE)) != NES_OK) return ret;
        if ((ret = nes_state_write(w, nes->nes_rom.chr_rom, CHR_ROM_UNIT_SIZE)) != NES_OK) return ret;
        if ((ret = nes_state_section_end(w, len_pos)) != NES_OK) return ret;
        *flags |= NES_STATE_FLAG_CHRRAM;
    }
    return NES_OK;
}

int nes_state_save(nes_t* nes, const char* file_path) {
    FILE* file;
    nes_state_writer_t w;
    uint8_t header[NES_STATE_HEADER_SIZE];
    uint8_t flags = 0;
    uint32_t header_crc;
    long end_pos;
    int ret = NES_OK;

    if (nes == NULL || file_path == NULL || nes->nes_rom.prg_rom == NULL) {
        return NES_STATE_ERR_ARG;
    }
    if (nes->nes_cpu.PC == 0u && nes->nes_rom.rom_crc == 0u) {
        return NES_STATE_ERR_ROM;   /* no ROM loaded */
    }

    file = (FILE*)nes_fopen(file_path, "w+b");   /* read back for the payload CRC */
    if (file == NULL) {
        NES_LOG_ERROR("nes_state_save: cannot open %s\n", file_path);
        return NES_STATE_ERR_IO;
    }

    nes_memset(&w, 0, sizeof(w));
    w.file = file;
    w.crc = NES_STATE_CRC_INIT;

    /* Placeholder header, patched once the payload size and CRC are known. */
    nes_memset(header, 0, sizeof(header));
    if (nes_fwrite(header, 1, NES_STATE_HEADER_SIZE, file) != NES_STATE_HEADER_SIZE) {
        nes_fclose(file);
        return NES_STATE_ERR_IO;
    }

    ret = nes_state_write_info(nes, &w, &flags);
    if (ret == NES_OK) ret = nes_state_write_bus(nes, &w, &flags);
    if (ret == NES_OK) ret = nes_state_write_cpu(nes, &w);
    if (ret == NES_OK) ret = nes_state_write_ppu(nes, &w);
#if (NES_ENABLE_SOUND == 1)
    if (ret == NES_OK) ret = nes_state_write_apu(nes, &w, &flags);
#endif
    if (ret == NES_OK) ret = nes_state_write_mapper(nes, &w, &flags);
    if (ret == NES_OK) ret = nes_state_write_ram(nes, &w, &flags);

    if (ret == NES_OK) {
        long len_pos = 0;
        static const char end_marker[4] = { 'S', 'A', 'V', 'E' };
        ret = nes_state_section_begin(&w, NES_STATE_TAG_END, 1u, &len_pos);
        if (ret == NES_OK) ret = nes_state_write(&w, end_marker, 4);
        if (ret == NES_OK) ret = nes_state_section_end(&w, len_pos);
    }

    end_pos = (long)nes_ftell(file);
    if (ret != NES_OK || end_pos < 0) {
        nes_fclose(file);
        (void)nes_remove(file_path);
        return (ret != NES_OK) ? ret : NES_STATE_ERR_IO;
    }

    /* Second pass: the payload CRC covers every byte after the header (section headers,
     * bodies and the END marker alike), so it has to be computed once the section lengths
     * have been patched in place. */
    {
        uint8_t chunk[256];
        uint32_t left = (uint32_t)(end_pos - NES_STATE_HEADER_SIZE);
        uint32_t crc = NES_STATE_CRC_INIT;
        w.bytes = left;
        if (left == 0u || nes_fseek(file, NES_STATE_HEADER_SIZE, SEEK_SET) != 0) {
            nes_fclose(file);
            (void)nes_remove(file_path);
            return NES_STATE_ERR_IO;
        }
        while (left != 0u) {
            uint32_t take = (left > sizeof(chunk)) ? (uint32_t)sizeof(chunk) : left;
            if (nes_fread(chunk, 1, take, file) != take) {
                nes_fclose(file);
                (void)nes_remove(file_path);
                return NES_STATE_ERR_IO;
            }
            crc = nes_state_crc(crc, chunk, take);
            left -= take;
        }
        w.crc = crc;
        if (nes_fseek(file, end_pos, SEEK_SET) != 0) {
            nes_fclose(file);
            (void)nes_remove(file_path);
            return NES_STATE_ERR_IO;
        }
    }

    nes_memcpy(header, NES_STATE_MAGIC, 8);
    header[8] = (uint8_t)(NES_STATE_FORMAT_VERSION & 0xFFu);
    header[9] = (uint8_t)((NES_STATE_FORMAT_VERSION >> 8) & 0xFFu);
    header[10] = (uint8_t)(NES_STATE_HEADER_SIZE & 0xFFu);
    header[11] = (uint8_t)((NES_STATE_HEADER_SIZE >> 8) & 0xFFu);
    header[12] = (uint8_t)(nes->nes_rom.rom_crc & 0xFFu);
    header[13] = (uint8_t)((nes->nes_rom.rom_crc >> 8) & 0xFFu);
    header[14] = (uint8_t)((nes->nes_rom.rom_crc >> 16) & 0xFFu);
    header[15] = (uint8_t)((nes->nes_rom.rom_crc >> 24) & 0xFFu);
    header[16] = (uint8_t)(nes->nes_rom.mapper_number & 0xFFu);
    header[17] = (uint8_t)((nes->nes_rom.mapper_number >> 8) & 0xFFu);
    header[18] = flags;
    header[19] = 0;
    header[20] = (uint8_t)(w.bytes & 0xFFu);
    header[21] = (uint8_t)((w.bytes >> 8) & 0xFFu);
    header[22] = (uint8_t)((w.bytes >> 16) & 0xFFu);
    header[23] = (uint8_t)((w.bytes >> 24) & 0xFFu);
    {
        uint32_t payload_crc = NES_STATE_CRC_FINISH(w.crc);
        header[24] = (uint8_t)(payload_crc & 0xFFu);
        header[25] = (uint8_t)((payload_crc >> 8) & 0xFFu);
        header[26] = (uint8_t)((payload_crc >> 16) & 0xFFu);
        header[27] = (uint8_t)((payload_crc >> 24) & 0xFFu);
    }
    header_crc = NES_STATE_CRC_FINISH(nes_state_crc(NES_STATE_CRC_INIT, header, NES_STATE_HEADER_CRC_SIZE));
    header[28] = (uint8_t)(header_crc & 0xFFu);
    header[29] = (uint8_t)((header_crc >> 8) & 0xFFu);
    header[30] = (uint8_t)((header_crc >> 16) & 0xFFu);
    header[31] = (uint8_t)((header_crc >> 24) & 0xFFu);

    if (nes_fseek(file, 0, SEEK_SET) != 0 ||
        nes_fwrite(header, 1, NES_STATE_HEADER_SIZE, file) != NES_STATE_HEADER_SIZE) {
        nes_fclose(file);
        (void)nes_remove(file_path);
        return NES_STATE_ERR_IO;
    }
    if (nes_fclose(file) != 0) {
        return NES_STATE_ERR_IO;
    }

    /* Keep the game's own battery save in sync with the snapshot we just took. */
    (void)nes_sram_save(nes);
    return NES_OK;
}

/* ------------------------------------------------------------------ load */

typedef struct nes_state_header {
    uint16_t format_version;
    uint16_t header_size;
    uint32_t rom_crc;
    uint16_t mapper;
    uint8_t  flags;
    uint32_t payload_size;
    uint32_t payload_crc;
} nes_state_header_t;

static int nes_state_read_header(FILE* file, nes_state_header_t* h) {
    uint8_t header[NES_STATE_HEADER_SIZE];
    uint32_t header_crc;
    if (nes_fread(header, 1, NES_STATE_HEADER_SIZE, file) != NES_STATE_HEADER_SIZE) {
        return NES_STATE_ERR_TRUNC;
    }
    if (nes_memcmp(header, NES_STATE_MAGIC, 8) != 0) {
        return NES_STATE_ERR_FORMAT;
    }
    h->format_version = (uint16_t)(header[8] | ((uint16_t)header[9] << 8));
    h->header_size = (uint16_t)(header[10] | ((uint16_t)header[11] << 8));
    h->rom_crc = (uint32_t)header[12] | ((uint32_t)header[13] << 8) |
                 ((uint32_t)header[14] << 16) | ((uint32_t)header[15] << 24);
    h->mapper = (uint16_t)(header[16] | ((uint16_t)header[17] << 8));
    h->flags = header[18];
    h->payload_size = (uint32_t)header[20] | ((uint32_t)header[21] << 8) |
                      ((uint32_t)header[22] << 16) | ((uint32_t)header[23] << 24);
    h->payload_crc = (uint32_t)header[24] | ((uint32_t)header[25] << 8) |
                     ((uint32_t)header[26] << 16) | ((uint32_t)header[27] << 24);
    header_crc = (uint32_t)header[28] | ((uint32_t)header[29] << 8) |
                 ((uint32_t)header[30] << 16) | ((uint32_t)header[31] << 24);
    if (header_crc != NES_STATE_CRC_FINISH(nes_state_crc(NES_STATE_CRC_INIT, header, NES_STATE_HEADER_CRC_SIZE))) {
        return NES_STATE_ERR_CRC;
    }
    if (h->format_version > NES_STATE_FORMAT_VERSION) {
        return NES_STATE_ERR_FORMAT;
    }
    if (h->header_size != NES_STATE_HEADER_SIZE) {
        return NES_STATE_ERR_FORMAT;
    }
    return NES_OK;
}

/* Walk the section list once, verifying framing and the payload CRC.  When a section is
 * unknown its body is skipped (forward compatibility). */
static int nes_state_verify_sections(FILE* file, const nes_state_header_t* h) {
    nes_state_reader_t r;
    uint32_t remaining = h->payload_size;
    int saw_end = 0;
    nes_memset(&r, 0, sizeof(r));
    r.file = file;
    r.crc = NES_STATE_CRC_INIT;
    r.limit = h->payload_size;

    while (remaining >= NES_STATE_SECTION_HEADER) {
        uint8_t head[NES_STATE_SECTION_HEADER];
        uint32_t body_len;
        if (nes_fread(head, 1, NES_STATE_SECTION_HEADER, file) != NES_STATE_SECTION_HEADER) {
            return NES_STATE_ERR_TRUNC;
        }
        r.crc = nes_state_crc(r.crc, head, NES_STATE_SECTION_HEADER);
        body_len = (uint32_t)head[8] | ((uint32_t)head[9] << 8) |
                   ((uint32_t)head[10] << 16) | ((uint32_t)head[11] << 24);
        remaining -= NES_STATE_SECTION_HEADER;
        if (body_len > remaining) {
            return NES_STATE_ERR_TRUNC;
        }
        if (nes_memcmp(head, NES_STATE_TAG_END, 4) == 0) {
            saw_end = 1;
        }
        /* Feed the body through the CRC, then skip to the next section. */
        {
            uint8_t chunk[256];
            uint32_t left = body_len;
            while (left != 0u) {
                uint32_t take = (left > sizeof(chunk)) ? (uint32_t)sizeof(chunk) : left;
                if (nes_fread(chunk, 1, take, file) != take) {
                    return NES_STATE_ERR_TRUNC;
                }
                r.crc = nes_state_crc(r.crc, chunk, take);
                left -= take;
            }
        }
        remaining -= body_len;
    }
    if (remaining != 0u) {
        return NES_STATE_ERR_TRUNC;
    }
    if (!saw_end) {
        return NES_STATE_ERR_TRUNC;
    }
    if (NES_STATE_CRC_FINISH(r.crc) != h->payload_crc) {
        return NES_STATE_ERR_CRC;
    }
    return NES_OK;
}

/* Position the reader at the first section body. */
static int nes_state_begin_payload(FILE* file, nes_state_reader_t* r, const nes_state_header_t* h) {
    if (nes_fseek(file, NES_STATE_HEADER_SIZE, SEEK_SET) != 0) {
        return NES_STATE_ERR_IO;
    }
    nes_memset(r, 0, sizeof(*r));
    r->file = file;
    r->crc = NES_STATE_CRC_INIT;
    r->limit = h->payload_size;
    return NES_OK;
}

/* Read the next section header; *tag receives the four bytes, *len the body length. */
static int nes_state_next_section(nes_state_reader_t* r, char tag[4], uint8_t* version, uint32_t* len) {
    uint8_t head[NES_STATE_SECTION_HEADER];
    int ret = nes_state_read(r, head, NES_STATE_SECTION_HEADER);
    if (ret != NES_OK) {
        return ret;
    }
    tag[0] = (char)head[0];
    tag[1] = (char)head[1];
    tag[2] = (char)head[2];
    tag[3] = (char)head[3];
    *version = head[4];
    *len = (uint32_t)head[8] | ((uint32_t)head[9] << 8) |
           ((uint32_t)head[10] << 16) | ((uint32_t)head[11] << 24);
    return NES_OK;
}

static int nes_state_skip(nes_state_reader_t* r, uint32_t len) {
    uint8_t chunk[256];
    while (len != 0u) {
        uint32_t take = (len > sizeof(chunk)) ? (uint32_t)sizeof(chunk) : len;
        int ret = nes_state_read(r, chunk, take);
        if (ret != NES_OK) {
            return ret;
        }
        len -= take;
    }
    return NES_OK;
}

static int nes_state_apply_bus(nes_t* nes, nes_state_reader_t* r) {
    uint8_t prg_src[4];
    uint16_t prg_bank[4];
    uint8_t chr_src[8];
    uint16_t chr_bank[8];
    uint8_t nt_src[4];
    uint16_t nt_page[4];
    uint8_t frame_skip = 0;
    uint8_t four_screen = 0;
    uint16_t scanline = 0;
    int ret;

    if ((ret = nes_state_read_u16(r, &scanline)) != NES_OK) return ret;
    if ((ret = nes_state_read_u8(r, &frame_skip)) != NES_OK) return ret;
    if ((ret = nes_state_read_u8(r, &four_screen)) != NES_OK) return ret;
    if ((ret = nes_state_read(r, prg_src, 4)) != NES_OK) return ret;
    for (uint8_t i = 0; i < 4u; i++) {
        if ((ret = nes_state_read_u16(r, &prg_bank[i])) != NES_OK) return ret;
    }
    if ((ret = nes_state_read(r, chr_src, 8)) != NES_OK) return ret;
    for (uint8_t i = 0; i < 8u; i++) {
        if ((ret = nes_state_read_u16(r, &chr_bank[i])) != NES_OK) return ret;
    }
    if ((ret = nes_state_read(r, nt_src, 4)) != NES_OK) return ret;
    for (uint8_t i = 0; i < 4u; i++) {
        if ((ret = nes_state_read_u16(r, &nt_page[i])) != NES_OK) return ret;
    }

    nes->scanline = scanline;
    nes->nes_rom.four_screen = four_screen;
#if (NES_FRAME_SKIP != 0)
    nes->nes_frame_skip_count = frame_skip;
#else
    (void)frame_skip;
#endif
    for (uint8_t i = 0; i < 4u; i++) {
        if (prg_src[i] == NES_STATE_SRC_ROM) {
            nes_load_prgrom_8k(nes, i, prg_bank[i]);
        }
    }
    for (uint8_t i = 0; i < 8u; i++) {
        if (chr_src[i] == NES_STATE_SRC_ROM) {
            nes_load_chrrom_1k(nes, i, chr_bank[i]);
        } else if (chr_src[i] == NES_STATE_SRC_RAM && nes->nes_rom.chr_rom != NULL) {
            nes->nes_ppu.pattern_table[i] = nes->nes_rom.chr_rom + (uint32_t)1024u * chr_bank[i];
        }
    }
    for (uint8_t i = 0; i < 4u; i++) {
        if (nt_src[i] == NES_STATE_SRC_VRAM) {
            nes->nes_ppu.name_table[i] = nes->nes_ppu.ppu_vram[nt_page[i] & 0x03u];
            nes->nes_ppu.name_table_mirrors[i] = nes->nes_ppu.name_table[i];
        }
    }
    /* Mappers that serve pages from their own RAM rebuild them here. */
    if (nes->nes_mapper.mapper_state_reapply != NULL) {
        nes->nes_mapper.mapper_state_reapply(nes);
    }
    return NES_OK;
}

static int nes_state_apply_cpu(nes_t* nes, nes_state_reader_t* r) {
    uint8_t pad = 0;
    int ret;
    if ((ret = nes_state_read_u8(r, &nes->nes_cpu.A)) != NES_OK) return ret;
    if ((ret = nes_state_read_u8(r, &nes->nes_cpu.X)) != NES_OK) return ret;
    if ((ret = nes_state_read_u8(r, &nes->nes_cpu.Y)) != NES_OK) return ret;
    if ((ret = nes_state_read_u8(r, &nes->nes_cpu.SP)) != NES_OK) return ret;
    if ((ret = nes_state_read_u16(r, &nes->nes_cpu.PC)) != NES_OK) return ret;
    if ((ret = nes_state_read_u8(r, &nes->nes_cpu.P)) != NES_OK) return ret;
    if ((ret = nes_state_read_u8(r, &nes->nes_cpu.irq_counter)) != NES_OK) return ret;
    if ((ret = nes_state_read_u8(r, &nes->nes_cpu.irq_nmi)) != NES_OK) return ret;
    if ((ret = nes_state_read_u8(r, &nes->nes_cpu.irq_nmi_delay)) != NES_OK) return ret;
    if ((ret = nes_state_read_u8(r, &nes->nes_cpu.irq_pending)) != NES_OK) return ret;
    if ((ret = nes_state_read_u8(r, &nes->nes_cpu.opcode)) != NES_OK) return ret;
    if ((ret = nes_state_read_u8(r, &pad)) != NES_OK) return ret;
    if ((ret = nes_state_read_u16(r, &nes->nes_cpu.cycles)) != NES_OK) return ret;
    if ((ret = nes_state_read_u16(r, &nes->nes_cpu.joypad.joypad)) != NES_OK) return ret;
    if ((ret = nes_state_read(r, nes->nes_cpu.cpu_ram, NES_CPU_RAM_SIZE)) != NES_OK) return ret;
    return NES_OK;
}

static int nes_state_apply_ppu(nes_t* nes, nes_state_reader_t* r) {
    uint8_t xw = 0;
    int ret;
    if ((ret = nes_state_read_u8(r, &nes->nes_ppu.ppu_ctrl)) != NES_OK) return ret;
    if ((ret = nes_state_read_u8(r, &nes->nes_ppu.ppu_mask)) != NES_OK) return ret;
    if ((ret = nes_state_read_u8(r, &nes->nes_ppu.ppu_status)) != NES_OK) return ret;
    if ((ret = nes_state_read_u8(r, &xw)) != NES_OK) return ret;
    nes->nes_ppu.x = (uint8_t)(xw & 0x07u);
    nes->nes_ppu.w = (uint8_t)((xw >> 3) & 0x01u);
    if ((ret = nes_state_read_u8(r, &nes->nes_ppu.oam_addr)) != NES_OK) return ret;
    if ((ret = nes_state_read_u8(r, &nes->nes_ppu.buffer)) != NES_OK) return ret;
    if ((ret = nes_state_read_u16(r, &nes->nes_ppu.v_reg)) != NES_OK) return ret;
    if ((ret = nes_state_read_u16(r, &nes->nes_ppu.t_reg)) != NES_OK) return ret;
    if ((ret = nes_state_read(r, nes->nes_ppu.ppu_vram, NES_PPU_VRAM_SIZE)) != NES_OK) return ret;
    if ((ret = nes_state_read(r, nes->nes_ppu.oam_data, NES_PPU_OAM_SIZE)) != NES_OK) return ret;
    if ((ret = nes_state_read(r, nes->nes_ppu.palette_indexes, 0x20)) != NES_OK) return ret;
    return NES_OK;
}

#if (NES_ENABLE_SOUND == 1)
static int nes_state_apply_apu(nes_t* nes, nes_state_reader_t* r) {
    uint32_t size = 0;
    int ret = nes_state_read_u32(r, &size);
    if (ret != NES_OK) {
        return ret;
    }
    if (size != (uint32_t)sizeof(nes_apu_t)) {
        /* Different APU layout (another build): skip it rather than corrupt memory. */
        return nes_state_skip(r, size);
    }
    return nes_state_read(r, &nes->nes_apu, size);
}
#endif

static int nes_state_apply_mapper(nes_t* nes, nes_state_reader_t* r) {
    uint16_t reg_size = 0;
    int ret = nes_state_read_u16(r, &reg_size);
    if (ret != NES_OK) {
        return ret;
    }
    if (reg_size != 0u) {
        if (nes->nes_mapper.mapper_register == NULL || reg_size != nes->nes_mapper.mapper_state_size) {
            /* Board state from a different build: skip it, the mapper keeps its own defaults. */
            if ((ret = nes_state_skip(r, reg_size)) != NES_OK) {
                return ret;
            }
        } else {
            if ((ret = nes_state_read(r, nes->nes_mapper.mapper_register, reg_size)) != NES_OK) {
                return ret;
            }
        }
    }
    if (nes->nes_mapper.mapper_state_load != NULL) {
        ret = nes->nes_mapper.mapper_state_load(nes, r);
        if (ret != NES_OK) {
            return ret;
        }
    }
    return NES_OK;
}

static int nes_state_apply_ram(nes_t* nes, nes_state_reader_t* r, int is_chr) {
    uint32_t len = 0;
    uint32_t copy;
    uint8_t* dst;
    int ret = nes_state_read_u32(r, &len);
    if (ret != NES_OK) {
        return ret;
    }
    if (is_chr) {
        dst = nes->nes_rom.chr_rom;
        copy = CHR_ROM_UNIT_SIZE;
    } else {
        dst = nes->nes_rom.sram;
        copy = SRAM_SIZE;
    }
    if (dst == NULL) {
        return nes_state_skip(r, len);
    }
    if (copy > len) {
        copy = len;
    }
    if ((ret = nes_state_read(r, dst, copy)) != NES_OK) {
        return ret;
    }
    if (len > copy) {
        if ((ret = nes_state_skip(r, len - copy)) != NES_OK) {
            return ret;
        }
    }
    {
        uint32_t full = is_chr ? (uint32_t)CHR_ROM_UNIT_SIZE : (uint32_t)SRAM_SIZE;
        if (len < full) {
            nes_memset(dst + len, 0, (size_t)(full - len));
        }
    }
    return NES_OK;
}

int nes_state_load(nes_t* nes, const char* file_path) {
    FILE* file;
    nes_state_header_t h;
    nes_state_reader_t r;
    uint32_t remaining;
    int ret;

    if (nes == NULL || file_path == NULL) {
        return NES_STATE_ERR_ARG;
    }
    if (nes->nes_rom.prg_rom == NULL) {
        return NES_STATE_ERR_ROM;
    }
    file = (FILE*)nes_fopen(file_path, "rb");
    if (file == NULL) {
        NES_LOG_ERROR("nes_state_load: cannot open %s\n", file_path);
        return NES_STATE_ERR_IO;
    }

    /* Pass 1: validate everything.  Nothing is applied before this succeeds. */
    ret = nes_state_read_header(file, &h);
    if (ret == NES_OK) {
        ret = nes_state_verify_sections(file, &h);
    }
    if (ret == NES_OK) {
        if (h.rom_crc != nes->nes_rom.rom_crc || h.mapper != nes->nes_rom.mapper_number) {
            NES_LOG_ERROR("nes_state_load: state belongs to another ROM (crc %08X/%08X mapper %u/%u)\n",
                          h.rom_crc, nes->nes_rom.rom_crc, h.mapper, nes->nes_rom.mapper_number);
            ret = NES_STATE_ERR_ROM;
        }
    }
    if (ret != NES_OK) {
        nes_fclose(file);
        return ret;
    }

    /* Pass 2: apply. */
    ret = nes_state_begin_payload(file, &r, &h);
    remaining = h.payload_size;
    while (ret == NES_OK && remaining >= NES_STATE_SECTION_HEADER) {
        char tag[4];
        uint8_t version = 0;
        uint32_t len = 0;
        long before;
        ret = nes_state_next_section(&r, tag, &version, &len);
        if (ret != NES_OK) {
            break;
        }
        before = (long)nes_ftell(file);
        remaining -= NES_STATE_SECTION_HEADER;
        if (len > remaining) {
            ret = NES_STATE_ERR_TRUNC;
            break;
        }
        (void)version;
        if (nes_memcmp(tag, NES_STATE_TAG_BUS, 4) == 0) {
            ret = nes_state_apply_bus(nes, &r);
        } else if (nes_memcmp(tag, NES_STATE_TAG_CPU, 4) == 0) {
            ret = nes_state_apply_cpu(nes, &r);
        } else if (nes_memcmp(tag, NES_STATE_TAG_PPU, 4) == 0) {
            ret = nes_state_apply_ppu(nes, &r);
#if (NES_ENABLE_SOUND == 1)
        } else if (nes_memcmp(tag, NES_STATE_TAG_APU, 4) == 0) {
            ret = nes_state_apply_apu(nes, &r);
#endif
        } else if (nes_memcmp(tag, NES_STATE_TAG_MAP, 4) == 0) {
            ret = nes_state_apply_mapper(nes, &r);
        } else if (nes_memcmp(tag, NES_STATE_TAG_SRAM, 4) == 0) {
            ret = nes_state_apply_ram(nes, &r, 0);
        } else if (nes_memcmp(tag, NES_STATE_TAG_CRAM, 4) == 0) {
            ret = nes_state_apply_ram(nes, &r, 1);
        } else {
            ret = nes_state_skip(&r, len);
        }
        if (ret == NES_OK) {
            /* Land exactly on the next section header regardless of what was consumed. */
            if (nes_fseek(file, before + (long)len, SEEK_SET) != 0) {
                ret = NES_STATE_ERR_IO;
                break;
            }
        }
        remaining -= len;
        if (nes_memcmp(tag, NES_STATE_TAG_END, 4) == 0) {
            break;
        }
    }
    nes_fclose(file);
    if (ret != NES_OK) {
        return ret;
    }

    /* Derived data: palette cache and a guaranteed full redraw on the next frame. */
    nes_palette_generate(nes);
#if (NES_FRAME_SKIP != 0)
    nes->nes_frame_skip_count = 0;
#endif
    (void)nes_sram_save(nes);
    return NES_OK;
}

/* ------------------------------------------------------------------ battery RAM */

int nes_sram_save(nes_t* nes) {
    FILE* file;
#if (NES_STATE_BATTERY_FILE == 0)
    (void)nes;
    return NES_OK;      /* battery file disabled: the state file carries the SRAM instead */
#else
    if (nes == NULL || nes->nes_rom.sram_persist == 0u) {
        return NES_OK;  /* tests and the corpus run without persistent battery RAM */
    }
    {
        char path[NES_PATH_MAX];
        /* Boards whose battery is a mapper owned buffer (e.g. Racermate's 64KB CHR-RAM). */
        if (nes->nes_mapper.mapper_battery != NULL && nes->nes_mapper.mapper_battery_size != 0u) {
            if (nes_state_build_path(nes->nes_rom.rom_path, NES_STATE_SRAM_EXT, path, sizeof(path)) != NES_OK) {
                return NES_STATE_ERR_ARG;
            }
            file = (FILE*)nes_fopen(path, "wb");
            if (file == NULL) {
                return NES_STATE_ERR_IO;
            }
            if (nes_fwrite(nes->nes_mapper.mapper_battery, 1, nes->nes_mapper.mapper_battery_size, file) != nes->nes_mapper.mapper_battery_size) {
                nes_fclose(file);
                return NES_STATE_ERR_IO;
            }
            nes_fclose(file);
            nes->nes_rom.sram_dirty = 0;
            return NES_OK;
        }
    }
    char path[NES_PATH_MAX];
    int ret;
    if (nes == NULL || nes->nes_rom.sram == NULL) {
        return NES_STATE_ERR_ARG;
    }
    ret = nes_state_build_path(nes->nes_rom.rom_path, NES_STATE_SRAM_EXT, path, sizeof(path));
    if (ret != NES_OK) {
        return ret;
    }
    file = (FILE*)nes_fopen(path, "wb");
    if (file == NULL) {
        NES_LOG_ERROR("nes_sram_save: cannot open %s\n", path);
        return NES_STATE_ERR_IO;
    }
    if (nes_fwrite(nes->nes_rom.sram, 1, SRAM_SIZE, file) != SRAM_SIZE) {
        nes_fclose(file);
        return NES_STATE_ERR_IO;
    }
    if (nes_fclose(file) != 0) {
        return NES_STATE_ERR_IO;
    }
    nes->nes_rom.sram_dirty = 0;
    return NES_OK;
#endif
}

int nes_sram_load(nes_t* nes) {
    FILE* file;
#if (NES_STATE_BATTERY_FILE == 0)
    (void)nes;
    return NES_OK;
#else
    if (nes == NULL || nes->nes_rom.sram_persist == 0u) {
        return NES_OK;
    }
    {
        char path[NES_PATH_MAX];
        if (nes->nes_mapper.mapper_battery != NULL && nes->nes_mapper.mapper_battery_size != 0u) {
            long size;
            if (nes_state_build_path(nes->nes_rom.rom_path, NES_STATE_SRAM_EXT, path, sizeof(path)) != NES_OK) {
                return NES_STATE_ERR_ARG;
            }
            file = (FILE*)nes_fopen(path, "rb");
            if (file == NULL) {
                return NES_OK;      /* nothing saved yet */
            }
            size = nes_ftell(file);
            if (size > 0) {
                uint32_t want = (uint32_t)size < nes->nes_mapper.mapper_battery_size ? (uint32_t)size : nes->nes_mapper.mapper_battery_size;
                if (nes_fread(nes->nes_mapper.mapper_battery, 1, want, file) != want) {
                    nes_fclose(file);
                    return NES_STATE_ERR_IO;
                }
            }
            nes_fclose(file);
            nes->nes_rom.sram_dirty = 0;
            return NES_OK;
        }
    }
    char path[NES_PATH_MAX];
    size_t got;
    int ret;
    if (nes == NULL || nes->nes_rom.sram == NULL) {
        return NES_STATE_ERR_ARG;
    }
    ret = nes_state_build_path(nes->nes_rom.rom_path, NES_STATE_SRAM_EXT, path, sizeof(path));
    if (ret != NES_OK) {
        return ret;
    }
    file = (FILE*)nes_fopen(path, "rb");
    if (file == NULL) {
        return NES_OK;      /* no battery file yet: the RAM stays zeroed */
    }
    nes_memset(nes->nes_rom.sram, 0, SRAM_SIZE);
    got = nes_fread(nes->nes_rom.sram, 1, SRAM_SIZE, file);
    nes_fclose(file);
    if (got == 0u) {
        return NES_STATE_ERR_IO;
    }
    nes->nes_rom.sram_dirty = 0;
    return NES_OK;
#endif
}

#endif /* NES_USE_FS */
