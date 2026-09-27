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
 * Save-state section IO.
 *
 * A save state is a 32 byte header followed by an ordered list of sections, each
 *   char tag[4]; uint8_t version; uint8_t reserved[3]; uint32_t len; uint8_t body[len];
 * and terminated by an 'END ' section.  Readers skip unknown tags, so new sections can be
 * added without breaking older builds, and a section may only grow by appending fields
 * (bumping its own version byte) - see docs/nessave-format.md.
 *
 * These types are declared unconditionally so that the mapper interface keeps the same
 * layout whether or not the file system is compiled in; the functions themselves only
 * exist for NES_USE_FS == 1.
 */

#ifndef _NES_STATE_IO_H_
#define _NES_STATE_IO_H_

#include "nes_default.h"

#ifdef __cplusplus
    extern "C" {
#endif

/* Section tags (4 ASCII bytes, no terminator). */
#define NES_STATE_TAG_INFO  "INFO"      /* producer, emulator version, ROM identity */
#define NES_STATE_TAG_BUS   "BUS "      /* scanline, mapping (PRG/CHR banks, nametables) */
#define NES_STATE_TAG_CPU   "CPU "      /* registers, IRQ flags, 2KB RAM */
#define NES_STATE_TAG_PPU   "PPU "      /* registers, 4KB VRAM, OAM, palette */
#define NES_STATE_TAG_APU   "APU "      /* APU registers and channel state */
#define NES_STATE_TAG_MAP   "MAP "      /* mapper register block + board specific extra data */
#define NES_STATE_TAG_SRAM  "SRAM"      /* battery backed $6000-$7FFF RAM */
#define NES_STATE_TAG_CRAM  "CRAM"      /* 8KB CHR-RAM backing store (CHR-RAM boards) */
#define NES_STATE_TAG_END   "END "      /* terminator, body = payload CRC32 */

/* Source of a mapped page, used to rebuild pointers on load. */
enum {
    NES_STATE_SRC_ROM     = 0,          /* cartridge ROM (bank number is meaningful) */
    NES_STATE_SRC_RAM     = 1,          /* core owned RAM buffer (CHR-RAM) */
    NES_STATE_SRC_VRAM    = 2,          /* PPU VRAM page (nametables) */
    NES_STATE_SRC_PRIVATE = 3           /* mapper private RAM: needs mapper_state_reapply */
};

typedef struct nes_state_writer {
    void*    file;                      /* nes_fopen() handle */
    uint32_t crc;                       /* running CRC32 over the payload */
    uint32_t bytes;                     /* payload bytes written so far */
    int      error;                     /* sticky error flag */
} nes_state_writer_t;

typedef struct nes_state_reader {
    void*    file;                      /* nes_fopen() handle */
    uint32_t crc;                       /* running CRC32 over the payload */
    uint32_t bytes;                     /* payload bytes consumed so far */
    uint32_t limit;                     /* payload_size from the header */
    int      error;                     /* sticky error flag */
} nes_state_reader_t;

#if (NES_USE_FS == 1)
/* Raw section body IO. Both feed the running payload CRC and fail closed. */
int nes_state_write(nes_state_writer_t* w, const void* data, uint32_t len);
int nes_state_read(nes_state_reader_t* r, void* data, uint32_t len);

/* Section framing: begin writes tag/version/placeholder length, end patches the length
 * and advances the CRC over the whole section (header included). */
int nes_state_section_begin(nes_state_writer_t* w, const char tag[4], uint8_t version, long* len_pos);
int nes_state_section_end(nes_state_writer_t* w, long len_pos);

/* Typed little-endian helpers used by the core and by mapper callbacks. */
int nes_state_write_u8(nes_state_writer_t* w, uint8_t value);
int nes_state_write_u16(nes_state_writer_t* w, uint16_t value);
int nes_state_write_u32(nes_state_writer_t* w, uint32_t value);
int nes_state_read_u8(nes_state_reader_t* r, uint8_t* value);
int nes_state_read_u16(nes_state_reader_t* r, uint16_t* value);
int nes_state_read_u32(nes_state_reader_t* r, uint32_t* value);
#endif

#ifdef __cplusplus
    }
#endif

#endif
