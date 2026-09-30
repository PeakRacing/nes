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

#include "nes_default.h"

#ifdef __cplusplus
    extern "C" {
#endif

#define NES_APU_SAMPLE_RATE         (44100)
#define NES_APU_SAMPLE_PER_SYNC     (NES_APU_SAMPLE_RATE/60)
/* PAL runs at 50Hz: 882 samples per frame.  Buffers are sized for the larger value; the
 * per frame count comes from nes->timing.samples_per_frame. */
#define NES_APU_SAMPLE_PER_SYNC_MAX (NES_APU_SAMPLE_RATE/50)

struct nes;
typedef struct nes nes_t;

/* ==== cartridge expansion audio (moved here from the old nes_expansion_audio module) ==== */




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

/* One MMC5 pulse channel: APU-style envelope + length counter, no sweep unit. */
typedef struct {
    uint8_t  duty;
    uint8_t  volume;            /* current envelope volume */
    uint8_t  constant_volume;
    uint8_t  halt;
    uint8_t  enabled;
    uint8_t  duty_pos;
    uint16_t period;
    int32_t  timer;
    uint8_t  length_counter;
    uint8_t  length_reload;
    uint8_t  env_start;
    uint8_t  env_divider;
    uint8_t  env_decay;
    uint32_t tick_acc;          /* 1/256 CPU cycle accumulator for the 240 Hz tick */
} nes_mmc5_square_t;

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

    /* --- MMC5 (mapper 5): two pulse channels plus an 8-bit PCM DAC --- */
    nes_mmc5_square_t mmc5_square[2];
    uint8_t  mmc5_pcm_output;
    uint8_t  mmc5_pcm_read_mode;
    uint8_t  mmc5_pcm_irq_enabled;

    /* --- VRC7 (mapper 85): YM2413/OPLL, driven by the vendored emu2413 core ---
       `vrc7_opll` stays NULL until the first $9010 write, so boards without the chip
       (and every non-VRC7 game) never allocate it. */
    void*    vrc7_opll;
    uint8_t  vrc7_current_reg;
    uint8_t  vrc7_muted;
    int16_t  vrc7_last_output;
    uint32_t vrc7_acc;              /* CPU cycles owed to the next OPLL sample (432 per sample) */
} nes_exp_audio_t;

void nes_exp_audio_init(nes_t* nes);
/* Register write.  `address` is the CPU address the board saw. */
void nes_exp_audio_write(nes_t* nes, uint16_t address, uint8_t data);
/* Register read (Namco 163 wave RAM port). */
uint8_t nes_exp_audio_read(nes_t* nes, uint16_t address);
/* Advance the declared chip by `cycles` CPU clocks and mix it into buffer[start..start+count). */
void nes_exp_audio_render(nes_t* nes, uint8_t* buffer, uint16_t start, uint16_t count, uint32_t cycles);

#endif /* NES_ENABLE_EXPANSION_AUDIO */


// https://www.nesdev.org/wiki/APU
// https://www.nesdev.org/apu_ref.txt

// https://www.nesdev.org/wiki/APU#Pulse_($4000-$4007)
typedef struct {
    union {
        struct {
            uint8_t envelope_lowers:4;      /*  Sets the direct volume if constant, otherwise controls the rate which the envelope lowers. 
                                                VVVV: 0000=silence 1111=maximum */
            uint8_t constant_volume:1;      /*  If C is set the volume will be a constant. If clear, an envelope will be used, 
                                                starting at volume 15 and lowering to 0 over time. */
            uint8_t len_counter_halt:1;     /*  1 = Infinite play, 0 = One-shot. If 1, the length counter will be frozen at its current value, 
                                                and the envelope will repeat forever.
                                                The length counter and envelope units are clocked by the frame counter.
                                                If the length counter's current value is 0 the channel will be silenced whether or not this bit is set.
                                                When using a one-shot envelope, the length counter should be loaded with a time longer than the length of the envelope to prevent it from being cut off early.
                                                When looping, after reaching 0 the envelope will restart at volume 15 at its next period. */
            uint8_t duty:2;                 /*  The width of the pulse is controlled by the duty bits in $4000/$4004. See APU Pulse for details. 
                                                DD: 00=12.5% 01=25% 10=50% 11=75% */
        };                                  /*  $4000/$4004 */  
        uint8_t control0;
    };
    union {
        struct {
            uint8_t shift:3;                /*  Shift count (number of bits).
                                                If SSS is 0, then behaves like E=0. */
            uint8_t negate:1;               /*  Negate flag
                                                0: add to period, sweeping toward lower frequencies
                                                1: subtract from period, sweeping toward higher frequencies */
            uint8_t period:3;               /*  The divider's period is P + 1 half-frames */
            uint8_t enabled:1;              /*  Enabled flag */
        };                                  /*  $4001 Sweep unit Side effects:Sets the reload flag */
        uint8_t control1;
    };

    uint8_t timer_low;                      /*  Timer low (T) Low 8 bits of raw period */
    union {
        struct {
            uint8_t timer_high:3;           /*  timer high High 3 bits of raw period */
            uint8_t len_counter_load:5;     /*  Length counter load */
        };
        uint8_t control3;
    };
    uint8_t length_counter;
    uint8_t sweep_reload;
    uint8_t envelope_restart;
    uint16_t cur_period;
    uint8_t sweep_divider;
    uint8_t envelope_divider;
    uint8_t envelope_volume;
    uint32_t phase_acc;
} pulse_t;

// https://www.nesdev.org/wiki/APU#Triangle_($4008-$400B)
typedef struct {
    union {
        struct {
            uint8_t linear_counter_load:7;  /*  This reload value will be applied to the linear counter on the next frame counter tick, but only if its reload flag is set.
                                                A write to $400B is needed to raise the reload flag.
                                                After a frame counter tick applies the load value R, the reload flag will only be cleared if C is also clear, otherwise it will continually reload (i.e. halt). */
            uint8_t len_counter_halt:1;     /*  This bit controls both the length counter and linear counter at the same time.
                                                When set this will stop the length counter in the same way as for the pulse/noise channels.
                                                When set it prevents the linear counter's internal reload flag from clearing, which effectively halts it if $400B is written after setting C.
                                                The linear counter silences the channel after a specified time with a resolution of 240Hz in NTSC (see frame counter below).
                                                Because both the length and linear counters are be enabled at the same time, whichever has a longer setting is redundant.
                                                See APU Triangle for more linear counter details. */
        };  
        uint8_t control0;
    };

    uint8_t timer_low;                      /*  Timer low Low 8 bits of raw period */
    union {
        struct {    
            uint8_t timer_high:3;           /*  timer high High 3 bits of raw period */
            uint8_t len_counter_load:5;     /*  Length counter load */
        };
        uint8_t control3;
    };
    uint8_t length_counter;
    uint8_t linear_counter;
    uint8_t linear_restart;
    uint16_t cur_period;
    uint32_t phase_acc;
} triangle_t;

// https://www.nesdev.org/wiki/APU#Noise_($400C-$400F)
typedef struct {
    union {
        struct {    
            uint8_t volume_envelope:4;      /*  volume/envelope (V) VVVV: 0000=silence 1111=maximum */
            uint8_t constant_volume:1;      /*  constant volume (C) */
            uint8_t len_counter_halt:1;     /*  length counter halt (L) */
        };                                  //  Envelope loop
        uint8_t control0;
    };
    union {
        struct {    
            uint8_t noise_period:4;         /*  noise period (P) Period */
            uint8_t :3;
            uint8_t loop_noise:1;           /*  Loop noise (L) Tone mode enable */
        };
        uint8_t control2;
    };
    union {
        struct {    
            uint8_t :3;
            uint8_t len_counter_load:5;      /*  Length counter load (L) */
        };
        uint8_t control3;
    };
    union {
        struct {    
            uint16_t lfsr_d0:1;
            uint16_t lfsr_d1:1;
            uint16_t :4;
            uint16_t lfsr_d6:1;
            uint16_t :9;
        };
        uint16_t lfsr;
    };
    uint8_t length_counter;
    uint8_t envelope_restart;
    uint8_t envelope_divider;
    uint8_t envelope_volume;
    uint32_t lfsr_acc;
} noise_t;

typedef struct {
    union {
        struct {    
            uint8_t frequency:4;            /*  frequency (R)  */
            uint8_t :2;
            uint8_t loop:1;                 /*  loop (L) */
            uint8_t irq_enable:1;           /*  IRQ enable (I) */
        };
        uint8_t control0;
    };
    union {
        struct {    
            uint8_t load_counter:7;         /*  Load counter (D) */
            uint8_t :1;
        };
        uint8_t control1;
    };
    uint8_t sample_address;                 /*	Sample address (A) */
    uint8_t sample_length;                  /*	Sample length (L) */

    /* --- playback state -------------------------------------------------
     * $4015 bit 4 enables the channel; the sample is re-fetched one byte at a time
     * through the CPU bus, decoded one bit per timer tick and accumulated into the
     * 7-bit DAC (out_level).  bytes_remaining doubles as the "DMC active" status bit. */
    uint8_t  enabled;
    uint8_t  irq_flag;                      /* sample finished with IRQ enabled */
    uint16_t cur_address;                   /* CPU address of the next fetch */
    uint16_t bytes_remaining;
    uint16_t timer_period;                  /* CPU cycles per output bit */
    uint8_t  sample_buffer;                 /* byte waiting to be shifted out */
    uint8_t  shift_reg;                     /* 8 sample bits, LSB first */
    uint8_t  bits_remaining;
    uint8_t  silence;                       /* no sample playing: hold the DAC level */
    uint8_t  out_level;                     /* 0-127 DAC */
    uint32_t timer_acc;                     /* fixed point (1/256 CPU cycle) timer */
} dmc_t;

// https://www.nesdev.org/wiki/APU#Registers
typedef struct nes_apu{
    pulse_t pulse1;                         /*	$4000–$4003 First pulse wave */
    pulse_t pulse2;                         /*	$4004–$4007 Second pulse wave */
    triangle_t triangle;                    /*	$4008–$400B Triangle wave */
    noise_t noise;                          /*	$400C–$400F Noise */
    dmc_t dmc;                              /*	$4010–$4013 DMC (sample playback). */
    union {
        struct {
            uint8_t status_pulse1:1;        /*  write:Enable pulse1 channels   read:length counter of pulse1 */
            uint8_t status_pulse2:1;        /*  write:Enable pulse2 channels   read:length counter of pulse2 */
            uint8_t status_triangle:1;      /*  write:Enable triangle channels read:length counter of triangle */
            uint8_t status_noise:1;         /*  write:Enable noise channels    read:length counter of noise */
            uint8_t status_dmc:1;           /*  write:Enable dmc channels      read:dmc active */
            uint8_t :1;
            uint8_t frame_interrupt:1;      /*  frame interrupt (F) only read */
            uint8_t dmc_interrupt:1;        /*  DMC interrupt (I) only read */
        };                                  
        uint8_t status;                     /*  Status ($4015) 
                                                The status register is used to enable and disable individual channels, 
                                                control the DMC, and can read the status of length counters and APU interrupts. */
    };

    union {
        struct {
            uint8_t :6;
            uint8_t irq_inhibit_flag:1;     /*  IRQ inhibit flag (I)*/
            uint8_t mode:1;                 /*  Mode (M, 0 = 4-step, 1 = 5-step) */
        };
        uint8_t frame_counter;              //  Frame Counter ($4017)
    };

    uint64_t clock_count;
    /* The APU drives its own IRQ line: it must not be folded into nes_cpu.irq_pending,
     * because the mappers acknowledge *their* IRQ by clearing that flag and a $4015 read
     * would then silently swallow a mapper interrupt (and vice versa). */
    uint8_t irq_line;
#if (NES_ENABLE_EXPANSION_AUDIO == 1)
    /* Cartridge expansion audio lives here so it is saved together with the APU state. */
    nes_exp_audio_t exp_audio;
#endif
    // sample_buffer: pulse1 pulse2 triangle noise dmc output
    uint8_t sample_buffer[NES_APU_SAMPLE_PER_SYNC_MAX];
    uint16_t sample_index;
} nes_apu_t;

void nes_apu_init(nes_t *nes);
void nes_apu_frame(nes_t *nes);
uint8_t nes_read_apu_register(nes_t *nes,uint16_t address);
void nes_write_apu_register(nes_t* nes,uint16_t address,uint8_t data);
/* APU IRQ line (frame counter or DMC), sampled by the CPU before every instruction. */
uint8_t nes_apu_irq_pending(const nes_t* nes);

#ifdef __cplusplus          
    }
#endif

