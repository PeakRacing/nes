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
 * Emulator owned save states, independent of the game's own battery save.
 *
 * The whole API only exists when NES_USE_FS == 1: saving needs a file system, and MCU
 * builds without one must not pay for this code at all.
 *
 * File layout, versioning and the per-section evolution policy are documented in
 * docs/nessave-format.md.
 */

#ifndef _NES_STATE_H_
#define _NES_STATE_H_

#include "nes_default.h"

#if (NES_USE_FS == 1)

#include "nes_state_io.h"

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

#endif
