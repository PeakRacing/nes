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

/*
 * Emulator owned save states, independent of the game's own battery save.
 *
 * The whole API only exists when NES_USE_FS == 1: saving needs a file system, and MCU
 * builds without one must not pay for this code at all.
 *
 * File layout, versioning and the per-section evolution policy are documented in
 * docs/nessave-format.md.
 */


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

#include "nes_default.h"

#if (NES_USE_FS == 1)


#ifdef __cplusplus
    extern "C" {
#endif

#define NES_STATE_FORMAT_VERSION  (1)
#define NES_STATE_EXT             ".nessave"   /* emulator state file */
#define NES_STATE_SRAM_EXT        ".sav"       /* raw battery RAM, shared with other emulators */

/* Error codes (negative, distinct from NES_ERROR). */
#define NES_STATE_ERR_ARG         (-2)     /* bad arguments */
#define NES_STATE_ERR_IO          (-3)     /* could not open/read/write the file */
#define NES_STATE_ERR_ROM         (-4)     /* state belongs to another ROM/board */
#define NES_STATE_ERR_FORMAT      (-5)     /* not a state file, or format newer than we support */
#define NES_STATE_ERR_CRC         (-6)     /* payload checksum mismatch */
#define NES_STATE_ERR_TRUNC       (-7)     /* truncated or short file */

/*
 * Save the full machine state to file_path, then refresh the battery save next to the ROM.
 *
 * The caller must invoke this between frames (never mid-frame); the SDL port calls it from
 * nes_frame(), tests call it between nes_test_run_frames() batches.
 *
 * Returns NES_OK on success, or one of the NES_STATE_ERR_* codes.
 */
int nes_state_save(nes_t* nes, const char* file_path);

/*
 * Restore a state written by nes_state_save.
 *
 * The file is validated completely (header, section table, payload CRC, ROM identity)
 * before a single byte of emulator state is touched, so a failed load leaves the running
 * machine exactly as it was. The battery RAM stored in the state is restored as well, and
 * the companion .sav file is rewritten to match, keeping both files consistent.
 *
 * Returns NES_OK on success, or one of the NES_STATE_ERR_* codes.
 */
int nes_state_load(nes_t* nes, const char* file_path);

/*
 * Write "<rom dir>/<rom base name>.nessave" for the ROM currently loaded into out.
 *
 * Returns NES_OK on success, NES_STATE_ERR_ARG when the buffer is too small or no ROM with
 * a path is loaded.
 */
int nes_state_default_path(nes_t* nes, char* out, size_t out_size);

/* Write/read the battery RAM as "<rom dir>/<rom base name>.sav" (raw bytes, no header). */
int nes_sram_save(nes_t* nes);
int nes_sram_load(nes_t* nes);

#ifdef __cplusplus
    }
#endif

#endif /* NES_USE_FS */

