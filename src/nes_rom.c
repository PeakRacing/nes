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

int nes_rom_stream_error(const nes_t* nes) {
#if (NES_ROM_STREAM == 1)
    return (nes != NULL) ? nes->nes_rom.stream_error : NES_STREAM_ERR_NO_FILE;
#else
    (void)nes;
    return NES_STREAM_OK;
#endif
}

static uint32_t nes_crc32_update(uint32_t crc, const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++)
            crc = (crc >> 1u) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1u)));
    }
    return crc;
}

typedef struct { uint32_t crc32; uint16_t mapper; uint8_t vrc4d; uint8_t pal; uint8_t mirror; uint8_t mmc1_strict; uint8_t prg_ram; } nes_romdb_entry_t;

/* Boards whose mirroring cannot be derived from the iNES header (pirate multicarts, wrong
 * header bits).  0 = leave the header's AUTO resolution alone. */
static void nes_rom_apply_mirror_override(nes_t* nes) {
    switch (nes->nes_rom.mirror_override) {
    case 1: nes_ppu_screen_mirrors(nes, NES_MIRROR_VERTICAL); break;
    case 2: nes_ppu_screen_mirrors(nes, NES_MIRROR_HORIZONTAL); break;
    case 3: nes_ppu_screen_mirrors(nes, NES_MIRROR_ONE_SCREEN0); break;
    case 4: nes_ppu_screen_mirrors(nes, NES_MIRROR_ONE_SCREEN1); break;
    default: break;
    }
}

/* PRG+CHR CRC32 table — corrects ROMs with wrong mapper in iNES header */
static const nes_romdb_entry_t romdb[] = {
    /* Arkanoid II (J) [!] — header says mapper 70, actually Taito TC0190FMC (mapper 33) */
    { 0x0F141525u, 33u, 0u },
    /* Blackjack by Nice Code V2 (Unl) [o1] — NES 2.0 header claims mapper 4 (MMC3), but the
       code never touches the MMC3 bank registers ($8001 is never written), so it runs on a
       plain NROM board: 32KB PRG fixed and the 8KB CHR mapped straight through.  Under
       mapper 4 the all-zero CHR registers put bank 0 in every slot above $1000 and the
       table/panel graphics collapse into garbage fragments. */
    { 0xBA481DD8u, 0u, 0u },
    /* Same Nice Code Blackjack, the 16KB PRG dump of it — identical board (NROM). */
    { 0x4A9B9DC0u, 0u, 0u },
    /* Super Mario Bros.+Tetris+Nintendo World Cup (E) [!] — header says mapper 4, actually PAL-ZZ (mapper 37) */
    { 0x73298C87u, 37u, 0u },
    /* Death Race (U) [!] — header says mapper 11, actual hardware is AGCI PCB (mapper 144).
       PRG fixed to last 32KB (bank1); CHR switched via upper nibble of write data.
       mapper11 breaks: PRG-switch at $804C sends CPU to bank0 whose NMI handler
       never enables PPUMASK. mapper3 (CNROM bus-conflict) gives wrong CHR banks.
       mapper144 = fixed PRG last bank + CHR via bits[7:4], which is the correct behavior. */
    { 0x5CAA3E61u, 144u, 0u },
    /* Family Circuit '91 (J) [!] — header says mapper 19, actual PCB is NAMCOT-175 (mapper 210).
       NAMCOT-175 has hardwired mirroring, no audio expansion, and no NT-bank redirection.
       Under mapper 19, CHR banks 0xE0-0xFF (valid CHR-ROM pages in this 256KB CHR ROM)
       are incorrectly rerouted to PPU VRAM, corrupting all background tiles. */
    { 0xC247CC80u, 210u, 0u },
    /* Dragon Ball Z 3 - 烈战人造人间 (Waixing, Unl) — header says mapper 74, but the PCB is
       Waixing board G = mapper 199: CHR banks 0-7 address an 8KB CHR-RAM that the game
       fills with its Chinese font and dynamic tiles, banks 8+ come from CHR-ROM.
       Under mapper 74/192 those uploads (8192 bytes at boot, again per screen change)
       are dropped as CHR-ROM writes, so the game renders the ROM's leftover Japanese
       glyph pages => "乱码".  Rule verified against MAME's Waixing board G
       (nes_waixing_g_device::chr_cb: bank < 0x08 ? CHRRAM : CHRROM). */
    { 0x62DDE924u, 199u, 0u },
    /* 激龟忍者传2 (TMNT2, Konami) — VRC4d board: the mirroring register is wired with the opposite
       polarity (0 = horizontal, i.e. NT0 = NT1).  The game writes its status bar into
       $2000-$23BF and scans it out of $2400, so vertical mirroring leaves the top six tile
       rows blank and hides the HUD (verified against Mesen, which shows the bar). */
    { 0x9247C38Du, 119u, 0u, 1u }, /* Pin Bot (E) - PAL cartridge, header carries no region bit */
    { 0x91B4B1D7u, 66u, 0u, 0u, 1u }, /* 2合1 (pirate GxROM multicart): header says horizontal, board is vertical */
    { 0xA713DD30u, 69u, 0u, 1u }, /* Mr Gimmick (PAL) */
    /* AD&D英雄冒险 (Hillsfar): the header's mapper 1 is already correct, so this entry only
       selects the counter serial model.  The ROM writes exactly five bits per MMC1 register
       (verified from its raw $8000-$FFFF write stream); both models agree on such a stream. */
    { 0x2C33161Du, 1u, 0u, 0u, 0u, 1u },
    /* FC的Basic语言文件 (Family BASIC, HVC-FB): an NROM board that carries 2KB of work RAM at
       $6000-$7FFF (with a write-protect switch).  Without it the interpreter prints
       "バックアップ スイッチ ヲ OFF ニ シテクダサイ" and stops, because its RAM write test fails;
       in the SDL/port builds (NES_USE_SRAM=0) nothing allocates that window. */
    { 0x69759626u, 0u, 0u, 0u, 0u, 0u, 1u },
    { 0x0DBDD55Du, 25u, 1u },
    /* sf97.nes (Street Fighter 97 multicart menu, "灰屏") — the header claims mapper 187, but the
       board is the A9711-A9713 "Panda Prince" pirate (mapper 121 = MMC3 + protection latch):
       the program seeds the latch at $8003 and reads its answer back from $5000-$5FFF, and while
       the latch is armed the board owns the upper three 8KB PRG slots.  Under mapper 187 the
       protection never resolves, $A000/$C000/$E000 stay on the boot banks and the game never
       enables rendering.  Mesen's database (NewRisingSun) lists this CRC as mapper 121. */
    { 0xDDCFB058u, 121u, 0u },
    /* Raid 2020 (Color Dreams, Unl) — the header claims mapper 7 (AxROM) and leaves bytes 8-15
       dirty, but the dump carries 64KB of CHR-ROM, which an AxROM board cannot have: AxROM has
       no CHR banking at all.  The game's own bank table (executed at $FFD4, data at $FFE0)
       holds "0C 0D 1C 1D 2C 2D 3C 3D 4C 4D 5C 5D" and it indexes it per screen: the low nibble
       picks PRG bank 0/1 and the HIGH nibble picks CHR bank 0-5 — exactly the Color Dreams
       register (mapper 11, CCCC PPPP).  Under mapper 7 those high bits are discarded, so the
       title screen (CHR bank 0) is mostly fine while the play field, which switches to CHR
       banks 1-5, renders with the wrong tiles ("画面乱").  Verified: with mapper 11 the
       in-game graphics become coherent (characters complete, no torn left column). */
    { 0xB1D03104u, 11u, 0u },
};

/*
 * iNES trainers are 512 bytes of patch code that the console copies to $7000-$71FF at power-on, and
 * games rely on it:  Q版沙罗曼蛇's IRQ vector is $71A0, i.e. inside the trainer.  Two things have to
 * hold, and neither did before:
 *   - the copy lands at $7000 ($6000 + 0x1000), not at the start of the window, and
 *   - it happens after mapper_init(), because on most boards (and in the SDL/port builds with
 *     NES_USE_SRAM=0) the mapper is what allocates the $6000-$7FFF window in the first place.
 * Like the real console the patch is written last, so it wins over a battery image it overlaps.
 * It is boot-time state, not a game write, so the battery file must not be marked dirty by it.
 */
static int nes_rom_apply_trainer(nes_t* nes, void* nes_file, long trainer_offset) {
    if (nes->nes_rom.sram == NULL) return NES_OK;   /* board without a $6000-$7FFF window */
    if (nes_fseek(nes_file, trainer_offset, SEEK_SET) != 0) return NES_ERROR;
    if (nes_fread(nes->nes_rom.sram + 0x1000, TRAINER_SIZE, 1, nes_file) == 0) return NES_ERROR;
    return NES_OK;
}

static void nes_romdb_lookup(nes_t* nes) {
    if (nes->nes_rom.prg_rom == NULL) return;
    size_t prg_len = (size_t)PRG_ROM_UNIT_SIZE * nes->nes_rom.prg_rom_size;
    size_t chr_len = (size_t)CHR_ROM_UNIT_SIZE * nes->nes_rom.chr_rom_size;
    uint32_t c = 0xFFFFFFFFu;
    c = nes_crc32_update(c, nes->nes_rom.prg_rom, prg_len);
    if (chr_len > 0u && nes->nes_rom.chr_rom != NULL)
        c = nes_crc32_update(c, nes->nes_rom.chr_rom, chr_len);
    uint32_t crc = c ^ 0xFFFFFFFFu;
    nes->nes_rom.rom_crc = crc;
    for (size_t i = 0; i < sizeof(romdb) / sizeof(romdb[0]); i++) {
        if (romdb[i].crc32 == crc) {
        if (romdb[i].pal) { nes_timing_set_pal(nes); }
            NES_LOG_INFO("romdb: CRC32=%08X mapper %d->%d\n",
                         crc, nes->nes_rom.mapper_number, romdb[i].mapper);
            nes->nes_rom.mapper_number = romdb[i].mapper;
            nes->nes_rom.vrc4d = romdb[i].vrc4d;
            nes->nes_rom.mirror_override = romdb[i].mirror;
            nes->nes_rom.mmc1_strict = romdb[i].mmc1_strict;
            nes->nes_rom.prg_ram = romdb[i].prg_ram;
            return;
        }
    }
}

#if (NES_USE_FS == 1)
int nes_load_file(nes_t* nes, const char* file_path ){
    nes_header_ines_t nes_header_info = {0};

    void* nes_file = nes_fopen(file_path, "rb");
    if (nes_file == NULL){
        NES_LOG_ERROR("nes_load_file: failed to open file %s\n", file_path);
        goto error;
    }
    /* Remember where the ROM came from: save states and the battery file live next to it. */
    {
        size_t path_len = 0;
        while (file_path[path_len] != '\0' && path_len + 1u < (size_t)NES_PATH_MAX) {
            nes->nes_rom.rom_path[path_len] = file_path[path_len];
            path_len++;
        }
        nes->nes_rom.rom_path[path_len] = '\0';
    }
#if (NES_USE_SRAM == 1)
    nes->nes_rom.sram = (uint8_t*)nes_malloc(SRAM_SIZE);
    if (nes->nes_rom.sram == NULL) {
        goto error;
    }
    nes_memset(nes->nes_rom.sram, 0x00, SRAM_SIZE);
#endif
    if (nes_fread(&nes_header_info, sizeof(nes_header_info), 1, nes_file)) {
        if (nes_memcmp(nes_header_info.identification, "NES\x1a", 4)){
            goto error;
        }
        if (nes_header_info.trainer){
            /* The 512 trainer bytes are copied to $7000-$71FF later, once mapper_init() has made
             * sure the $6000-$7FFF window exists (see nes_rom_apply_trainer). */
            nes_fseek(nes_file, TRAINER_SIZE, SEEK_CUR);
        }
        if (nes_header_info.identifier==2){ //NES 2.0
            nes_header_nes2_t* nes2_header_info = (nes_header_nes2_t*)&nes_header_info;
            nes->nes_rom.prg_rom_size = ((nes2_header_info->prg_rom_size_m << 8) & 0xF00) | nes2_header_info->prg_rom_size_l;
            nes->nes_rom.chr_rom_size = ((nes2_header_info->chr_rom_size_m << 8) & 0xF00) | nes2_header_info->chr_rom_size_l;
            nes->nes_rom.mapper_number = ((nes2_header_info->mapper_number_h << 8) & 0xF00) | ((nes2_header_info->mapper_number_m << 4) & 0xF0) | (nes2_header_info->mapper_number_l & 0x0F);

        }else{  //INES
            nes_header_ines_t* ines_header_info = (nes_header_ines_t*)&nes_header_info;
            nes->nes_rom.prg_rom_size = ines_header_info->prg_rom_size;
            nes->nes_rom.chr_rom_size = ines_header_info->chr_rom_size;
            /* Detect dirty iNES header: if bytes 12-15 are not all zero, ignore upper mapper nibble */
            if (ines_header_info->Reserved[1] | ines_header_info->Reserved[2] | ines_header_info->Reserved[3] | ines_header_info->Reserved[4]) {
                nes->nes_rom.mapper_number = ines_header_info->mapper_number_l;
            } else {
                nes->nes_rom.mapper_number = ines_header_info->mapper_number_l | ines_header_info->mapper_number_h << 4;
            }
        }
        nes->nes_rom.mirroring_type = nes_header_info.mirroring;
        nes->nes_rom.four_screen = nes_header_info.four_screen;
        nes->nes_rom.save_ram = nes_header_info.save;
#if (NES_ROM_STREAM == 1)
        /* Stream mode: only allocate LRU cache buffers, keep file open */
        nes->nes_rom.prg_data_offset = (long)sizeof(nes_header_info) + (nes_header_info.trainer ? TRAINER_SIZE : 0);
        nes->nes_rom.chr_data_offset = nes->nes_rom.prg_data_offset + (long)PRG_ROM_UNIT_SIZE * nes->nes_rom.prg_rom_size;
        nes->nes_rom.stream_error = NES_STREAM_OK;
        /* PRG: NES_PRG_CACHE_SLOTS x 8KB LRU cache */
        nes->nes_rom.prg_rom = (uint8_t*)nes_malloc(8192 * NES_PRG_CACHE_SLOTS);
        if (nes->nes_rom.prg_rom == NULL) {
            goto error;
        }
        nes_memset(nes->nes_rom.prg_rom, 0, 8192 * NES_PRG_CACHE_SLOTS);
        /* CHR: NES_CHR_CACHE_SLOTS x 1KB LRU cache */
        nes->nes_rom.chr_rom = (uint8_t*)nes_malloc(1024 * NES_CHR_CACHE_SLOTS);
        if (nes->nes_rom.chr_rom == NULL) {
            goto error;
        }
        nes_memset(nes->nes_rom.chr_rom, 0, 1024 * NES_CHR_CACHE_SLOTS);
        /* Init LRU cache entries */
        nes->nes_rom.cache_tick = 0;
        for (int i = 0; i < NES_PRG_CACHE_SLOTS; i++) {
            nes->nes_rom.prg_cache[i].tag = 0xFFFF;
            nes->nes_rom.prg_cache[i].last_used = 0;
        }
        for (int i = 0; i < NES_CHR_CACHE_SLOTS; i++) {
            nes->nes_rom.chr_cache[i].tag = 0xFFFF;
            nes->nes_rom.chr_cache[i].last_used = 0;
        }
        /* Keep file handle open for streaming */
        nes->nes_rom.rom_file = nes_file;
        nes_file = NULL;
#else
        nes->nes_rom.prg_rom = (uint8_t*)nes_malloc(PRG_ROM_UNIT_SIZE * nes->nes_rom.prg_rom_size);
        if (nes->nes_rom.prg_rom == NULL) {
            goto error;
        }
        if (nes_fread(nes->nes_rom.prg_rom, PRG_ROM_UNIT_SIZE, nes->nes_rom.prg_rom_size, nes_file)==0){
            goto error;
        }
        nes->nes_rom.chr_rom = (uint8_t*)nes_malloc(CHR_ROM_UNIT_SIZE * (nes->nes_rom.chr_rom_size ? nes->nes_rom.chr_rom_size : 1));
        if (nes->nes_rom.chr_rom == NULL) {
            goto error;
        }
        if (nes->nes_rom.chr_rom_size){
            if (nes_fread(nes->nes_rom.chr_rom, CHR_ROM_UNIT_SIZE, (nes->nes_rom.chr_rom_size), nes_file)==0){
                goto error;
            }
        }
#endif
    }else{
        goto error;
    }
    /* The file stays open until the trainer (if any) has been copied, see below. */
    nes_cpu_init(nes);
#if (NES_ENABLE_SOUND==1)
    nes_apu_init(nes);
#endif
    nes_ppu_init(nes);
#if (NES_ROM_STREAM != 1)
    nes_romdb_lookup(nes);
#endif
    if(nes_load_mapper(nes)){
        goto error;
    }
    nes->nes_mapper.mapper_init(nes);
    nes_rom_apply_mirror_override(nes);
    /* Battery RAM is loaded after mapper_init(): mappers may allocate the SRAM themselves
     * (NES_USE_SRAM == 0 builds), and the game's own save must be in place before it runs. */
    (void)nes_sram_load(nes);
    /* The trainer patch goes in last: it needs the window mapper_init() may have just created. */
#if (NES_ROM_STREAM == 1)
    /* Stream builds handed the handle over to the bank cache. */
    void* trainer_file = (nes_file != NULL) ? nes_file : nes->nes_rom.rom_file;
#else
    void* trainer_file = nes_file;
#endif
    if (nes_header_info.trainer &&
        nes_rom_apply_trainer(nes, trainer_file, (long)sizeof(nes_header_info)) != NES_OK) {
        goto error;
    }
#if (NES_ROM_STREAM != 1)
    nes_fclose(nes_file);
#endif
    return NES_OK;
error:
    if (nes_file){
        nes_fclose(nes_file);
    }
    if (nes){
        nes_unload_file(nes);
    }
    return NES_ERROR;

}


int nes_unload_file(nes_t* nes){
    /* Flush the game's battery save before the mapper (and its SRAM) go away. */
    if (nes->nes_rom.sram_persist && nes->nes_rom.rom_path[0] != '\0' && (nes->nes_rom.sram_dirty || nes->nes_mapper.mapper_battery != NULL)) {
        (void)nes_sram_save(nes);
    }
    if (nes->nes_mapper.mapper_deinit) {
        nes->nes_mapper.mapper_deinit(nes);
    }
#if (NES_ROM_STREAM == 1)
    if (nes->nes_rom.rom_file){
        nes_fclose(nes->nes_rom.rom_file);
        nes->nes_rom.rom_file = NULL;
    }
#endif
    if (nes->nes_rom.prg_rom){
        nes_free(nes->nes_rom.prg_rom);
        nes->nes_rom.prg_rom = NULL;
    }
    if (nes->nes_rom.chr_rom){
        nes_free(nes->nes_rom.chr_rom);
        nes->nes_rom.chr_rom = NULL;
    }
    if (nes->nes_rom.sram){
        nes_free(nes->nes_rom.sram);
        nes->nes_rom.sram = NULL;
    }
    return NES_OK;
}

#endif

int nes_load_rom(nes_t* nes, const uint8_t* nes_rom){
    nes_header_ines_t* nes_header_info = (nes_header_ines_t*)nes_rom;
#if (NES_ROM_STREAM == 1)
    nes->nes_rom.stream_error = NES_STREAM_OK;
#endif
#if (NES_USE_SRAM == 1)
    nes->nes_rom.sram = (uint8_t*)nes_malloc(SRAM_SIZE);
    if (nes->nes_rom.sram == NULL) {
        goto error;
    }
    nes_memset(nes->nes_rom.sram, 0x00, SRAM_SIZE);
#endif
    if ( nes_memcmp( nes_header_info->identification, "NES\x1a", 4 )){
        goto error;
    }
    uint8_t* nes_bin = (uint8_t*)nes_rom + sizeof(nes_header_ines_t);
    if (nes_header_info->trainer){
#if (NES_USE_SRAM == 1)
#else
#endif
        nes_bin += TRAINER_SIZE;
    }

    if (nes_header_info->identifier==2){ //NES 2.0
        nes_header_nes2_t* nes2_header_info = (nes_header_nes2_t*)nes_header_info;
        nes->nes_rom.prg_rom_size = ((nes2_header_info->prg_rom_size_m << 8) & 0xF00) | nes2_header_info->prg_rom_size_l;
        nes->nes_rom.chr_rom_size = ((nes2_header_info->chr_rom_size_m << 8) & 0xF00) | nes2_header_info->chr_rom_size_l;
        nes->nes_rom.mapper_number = ((nes2_header_info->mapper_number_h << 8) & 0xF00) | ((nes2_header_info->mapper_number_m << 4) & 0xF0) | (nes2_header_info->mapper_number_l & 0x0F);

    }else{  //INES
        nes_header_ines_t* ines_header_info = (nes_header_ines_t*)nes_header_info;
        nes->nes_rom.prg_rom_size = ines_header_info->prg_rom_size;
        nes->nes_rom.chr_rom_size = ines_header_info->chr_rom_size;
        /* Detect dirty iNES header: if bytes 12-15 are not all zero, ignore upper mapper nibble */
        if (ines_header_info->Reserved[1] | ines_header_info->Reserved[2] | ines_header_info->Reserved[3] | ines_header_info->Reserved[4]) {
            nes->nes_rom.mapper_number = ines_header_info->mapper_number_l;
        } else {
            nes->nes_rom.mapper_number = ines_header_info->mapper_number_l | ines_header_info->mapper_number_h << 4;
        }
    }

    nes->nes_rom.mirroring_type = (nes_header_info->mirroring);
    nes->nes_rom.four_screen = (nes_header_info->four_screen);
    nes->nes_rom.save_ram = (nes_header_info->save);

    nes->nes_rom.prg_rom = nes_bin;
    nes_bin += PRG_ROM_UNIT_SIZE * nes->nes_rom.prg_rom_size;

    if (nes->nes_rom.chr_rom_size){
        /* CHR-ROM lives in the caller's buffer. */
        nes->nes_rom.chr_rom = nes_bin;
    } else {
        /* CHR-RAM board: allocate the writable backing store, mirroring
         * nes_load_file(). Without it every mapper that maps CHR-RAM into the
         * pattern tables would end up with pointers based on a NULL base. */
        nes->nes_rom.chr_rom = (uint8_t*)nes_malloc(CHR_ROM_UNIT_SIZE);
        if (nes->nes_rom.chr_rom == NULL){
            goto error;
        }
        nes_memset(nes->nes_rom.chr_rom, 0, CHR_ROM_UNIT_SIZE);
    }
    nes_cpu_init(nes);
#if (NES_ENABLE_SOUND==1)
    nes_apu_init(nes);
#endif
    nes_ppu_init(nes);
    nes_romdb_lookup(nes);
    if(nes_load_mapper(nes)){
        return NES_ERROR;
    }
    nes->nes_mapper.mapper_init(nes);
    nes_rom_apply_mirror_override(nes);
    /* The trainer is 512 bytes of boot-time patch code for $7000-$71FF; it needs the window that
     * mapper_init() may just have allocated (see nes_rom_apply_trainer). */
    if (nes_header_info->trainer && nes->nes_rom.sram != NULL) {
        nes_memcpy(nes->nes_rom.sram + 0x1000, (const uint8_t*)nes_rom + sizeof(nes_header_ines_t),
                   TRAINER_SIZE);
    }
    return NES_OK;
error:
    if (nes){
        nes_unload_rom(nes);
    }
    return NES_ERROR;
}

int nes_unload_rom(nes_t* nes){
    if (nes->nes_mapper.mapper_deinit) {
        nes->nes_mapper.mapper_deinit(nes);
    }
    if (nes->nes_rom.sram) {
        nes_free(nes->nes_rom.sram);
        nes->nes_rom.sram = NULL;
    }
    /* CHR-RAM boards own an allocated backing store; CHR-ROM pointers alias the
     * caller's image buffer and must not be freed here. */
    if (nes->nes_rom.chr_rom_size == 0u && nes->nes_rom.chr_rom != NULL) {
        nes_free(nes->nes_rom.chr_rom);
        nes->nes_rom.chr_rom = NULL;
    }
    return NES_OK;
}
