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

#ifndef NES_EXPANSION_AUDIO_H
#define NES_EXPANSION_AUDIO_H

#include "nes_default.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Cartridge expansion audio (VRC6 / Sunsoft 5B / Namco 163 / MMC5 / VRC7).
 *
 * Enabled by NES_ENABLE_EXPANSION_AUDIO (0 = off).  The desktop SDL targets turn it on
 * together with NES_ENABLE_SOUND; MCU/core builds leave both at 0, in which case this whole
 * module compiles away and the APU mixer keeps its original shape.
 *
 * A board declares which chip it carries by setting nes->nes_mapper.mapper_audio to one of
 * the NES_EXP_AUDIO_* ids and forwards its register window writes to nes_exp_audio_write().
 * Once per APU segment the mixer calls nes_exp_audio_render(), which advances the chip by
 * that segment's CPU cycles and mixes its output into the sample buffer.
 */

#define NES_EXP_AUDIO_NONE      (0)
#define NES_EXP_AUDIO_VRC6      (1)
#define NES_EXP_AUDIO_S5B       (2)
#define NES_EXP_AUDIO_N163      (3)
#define NES_EXP_AUDIO_MMC5      (4)
#define NES_EXP_AUDIO_VRC7      (5)

#if (NES_ENABLE_EXPANSION_AUDIO == 1)

typedef struct {
    /* --- VRC6 (mapper 24/26): two pulses + saw, all clocked once per CPU cycle --- */
    struct {
        uint8_t  volume;
        uint8_t  duty;
        uint8_t  ignore_duty;
        uint16_t frequency;
        uint8_t  enabled;
        int32_t  timer;
        uint8_t  step;
    } vrc6_pulse[2];
    struct {
        uint8_t  acc_rate;
        uint8_t  accumulator;
        uint16_t frequency;
        uint8_t  enabled;
        int32_t  timer;
        uint8_t  step;
    } vrc6_saw;
    uint8_t  vrc6_shift;
    uint8_t  vrc6_halt;

    /* --- Namco 163 (mapper 19/210/163): 128 bytes of wave/channel RAM --- */
    uint8_t  n163_ram[0x80];
    int16_t  n163_channel_out[8];
    uint8_t  n163_ram_position;
    uint8_t  n163_auto_increment;
    uint8_t  n163_update_counter;
    int8_t   n163_current_channel;
    int16_t  n163_last_output;
    uint8_t  n163_disable;
    uint32_t n163_acc;              /* 1/256 CPU cycle timer accumulator */

    /* --- Sunsoft 5B (mapper 69): AY-3-8910 style, three square channels --- */
    uint8_t  s5b_regs[0x10];
    uint8_t  s5b_current_register;
    uint8_t  s5b_volume_lut[0x10];
    int16_t  s5b_timer[3];
    uint8_t  s5b_step[3];
    uint32_t s5b_acc;               /* 1/256 CPU cycle accumulator (channels tick at CPU/2) */
} nes_exp_audio_t;

void nes_exp_audio_init(nes_t* nes);
/* Register write.  `address` is the CPU address the board saw. */
void nes_exp_audio_write(nes_t* nes, uint16_t address, uint8_t data);
/* Register read (Namco 163 wave RAM port). */
uint8_t nes_exp_audio_read(nes_t* nes, uint16_t address);
/* Advance the declared chip by `cycles` CPU clocks and mix it into buffer[start..start+count). */
void nes_exp_audio_render(nes_t* nes, uint8_t* buffer, uint16_t start, uint16_t count, uint32_t cycles);

#endif /* NES_ENABLE_EXPANSION_AUDIO */

#ifdef __cplusplus
}
#endif

#endif /* NES_EXPANSION_AUDIO_H */
