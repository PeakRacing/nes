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

#if (NES_ENABLE_SOUND == 1)

// https://www.nesdev.org/wiki/APU_Length_Counter
static const uint8_t length_counter_table[32] = {
/*       |   0    1    2    3    4    5    6    7    8    9    A    B    C    D    E    F
---------+---------------------------------------------------------------- */
/*00-0F*/  0x0A,0xFE,0x14,0x02,0x28,0x04,0x50,0x06,0xA0,0x08,0x3C,0x0A,0x0E,0x0C,0x1A,0x0E,
/*10-1F*/  0x0C,0x10,0x18,0x12,0x30,0x14,0x60,0x16,0xC0,0x18,0x48,0x1A,0x10,0x1C,0x20,0x1E,
};

static const uint8_t apu_pulse_wave[4][8] = {
    {0, 1, 0, 0, 0, 0, 0, 0},
    {0, 1, 1, 0, 0, 0, 0, 0},
    {0, 1, 1, 1, 1, 0, 0, 0},
    {1, 0, 0, 1, 1, 1, 1, 1}
};

static const uint8_t apu_triangle_wave[32] = {
    15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0,
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
};

// https://www.nesdev.org/wiki/APU_Noise#Timer_period_lookup_table_(NTSC)
static const uint16_t noise_period_table[16] = {
    4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068
};


/* The APU's own IRQ line: frame counter and DMC both feed it.  Keeping it separate from
 * nes_cpu.irq_pending is what stops a $4015 read from acknowledging a mapper's IRQ. */
static inline void nes_apu_update_irq_line(nes_t* nes) {
    nes->nes_apu.irq_line = (uint8_t)((nes->nes_apu.frame_interrupt || nes->nes_apu.dmc.irq_flag) ? 1u : 0u);
}

uint8_t nes_apu_irq_pending(const nes_t* nes) {
    return nes->nes_apu.irq_line;
}

/* Defined next to the other DMC helpers below; the mixer loop needs it earlier. */
static inline void nes_apu_dmc_advance(nes_t* nes, uint32_t cycles_q8);

static inline void nes_apu_pulse_sweep(pulse_t* pulse, uint8_t period_one){    if (pulse->sweep_divider == 0 && pulse->enabled && pulse->shift){
        if (pulse->cur_period >= 8 && pulse->cur_period <= 0x7ff){
            if (pulse->negate){
                    pulse->cur_period = pulse->cur_period - (pulse->cur_period >> pulse->shift) - period_one;
            }else{
                pulse->cur_period = pulse->cur_period + (pulse->cur_period >> pulse->shift);
            }
        }
    }
    if (pulse->sweep_reload || (pulse->sweep_divider == 0)){ //扫描单元重新开始
        pulse->sweep_reload = 0;
        pulse->sweep_divider = pulse->period;
    }else{
        pulse->sweep_divider--;
    }
}

static inline void nes_apu_length_counter_and_sweep(nes_t* nes){
    // length_counter
    if (!nes->nes_apu.pulse1.len_counter_halt && nes->nes_apu.pulse1.length_counter){
        nes->nes_apu.pulse1.length_counter--;
    }
    if (!nes->nes_apu.pulse2.len_counter_halt && nes->nes_apu.pulse2.length_counter){
        nes->nes_apu.pulse2.length_counter--;
    }
    if (!nes->nes_apu.triangle.len_counter_halt && nes->nes_apu.triangle.length_counter){
        nes->nes_apu.triangle.length_counter--;
    }
    if (!nes->nes_apu.noise.len_counter_halt && nes->nes_apu.noise.length_counter){
        nes->nes_apu.noise.length_counter--;
    }
    // sweep
    nes_apu_pulse_sweep(&nes->nes_apu.pulse1,1);
    nes_apu_pulse_sweep(&nes->nes_apu.pulse2,0);
}

static inline void nes_apu_pulse_envelopes(pulse_t* pulse){
    if (pulse->envelope_restart){//包络重新开始
        pulse->envelope_restart = 0;
        pulse->envelope_divider = pulse->envelope_lowers;
        pulse->envelope_volume = 15;
    }else{
        if (pulse->envelope_divider){
            pulse->envelope_divider--;
        }else{
            pulse->envelope_divider = pulse->envelope_lowers;
            if (pulse->envelope_volume){
                pulse->envelope_volume--;
            }else{
                if (pulse->len_counter_halt){
                    pulse->envelope_volume = 15;
                }
            }
        }
    }
}

static inline void nes_apu_noise_envelopes(noise_t* noise){
    if (noise->envelope_restart){//包络重新开始
        noise->envelope_restart = 0;
        noise->envelope_divider = noise->volume_envelope;
        noise->envelope_volume = 15;
    }else{
        if (noise->envelope_divider){
            noise->envelope_divider--;
        }else{
            noise->envelope_divider = noise->volume_envelope;
            if (noise->envelope_volume){
                noise->envelope_volume--;
            }else{
                if (noise->len_counter_halt){
                    noise->envelope_volume = 15;
                }
            }
        }
    }
}

static inline void nes_apu_triangle_linear_counter(triangle_t* triangle){
    if (triangle->linear_restart){
        triangle->linear_counter = triangle->linear_counter_load;
    }else if (triangle->linear_counter){
        triangle->linear_counter--;
    }
    if (!triangle->len_counter_halt)
        triangle->linear_restart = 0;
}

static inline void nes_apu_envelopes_and_linear_counter(nes_t *nes){
    nes_apu_pulse_envelopes(&nes->nes_apu.pulse1);
    nes_apu_pulse_envelopes(&nes->nes_apu.pulse2);
    nes_apu_triangle_linear_counter(&nes->nes_apu.triangle);
    nes_apu_noise_envelopes(&nes->nes_apu.noise);
}

// https://www.nesdev.org/wiki/APU_Pulse
static inline void nes_apu_play(nes_t* nes){
    nes_apu_t* apu = &nes->nes_apu;
    const uint16_t seg = (uint16_t)(apu->clock_count & 3);
    const uint16_t sample_start = (uint16_t)(seg * nes->timing.samples_per_frame / 4);
    const uint16_t sample_end = (uint16_t)((seg + 1) * nes->timing.samples_per_frame / 4);

    // Pulse 1 状态
    pulse_t* p1 = &apu->pulse1;
    const uint8_t p1_active = apu->status_pulse1 && p1->length_counter > 0 &&
                              p1->cur_period >= 8 && p1->cur_period < 0x800;
    const uint8_t p1_vol = p1_active ? (p1->constant_volume ? p1->envelope_lowers : p1->envelope_volume) : 0;
    // fpulse = fCPU/(16*(t+1)), phase_inc = 65536 * fCPU / (sample_rate * 16 * (t+1))
    const uint32_t p1_inc = (p1->cur_period >= 8) ?
        (uint32_t)((uint64_t)nes->timing.cpu_clock * 65536 / ((uint64_t)NES_APU_SAMPLE_RATE * 16 * (p1->cur_period + 1))) : 0;
    const uint8_t* p1_duty = apu_pulse_wave[p1->duty];

    // Pulse 2 状态
    pulse_t* p2 = &apu->pulse2;
    const uint8_t p2_active = apu->status_pulse2 && p2->length_counter > 0 &&
                              p2->cur_period >= 8 && p2->cur_period < 0x800;
    const uint8_t p2_vol = p2_active ? (p2->constant_volume ? p2->envelope_lowers : p2->envelope_volume) : 0;
    const uint32_t p2_inc = (p2->cur_period >= 8) ?
        (uint32_t)((uint64_t)nes->timing.cpu_clock * 65536 / ((uint64_t)NES_APU_SAMPLE_RATE * 16 * (p2->cur_period + 1))) : 0;
    const uint8_t* p2_duty = apu_pulse_wave[p2->duty];

    // Triangle 状态 - 三角波定时器每CPU周期计时(不除以2),32步序列: freq = fCPU/(32*(t+1))
    triangle_t* tri = &apu->triangle;
    const uint8_t tri_active = apu->status_triangle && tri->length_counter > 0 &&
                               tri->linear_counter > 0 && tri->cur_period >= 2;
    const uint32_t tri_inc = tri_active ?
        (uint32_t)((uint64_t)nes->timing.cpu_clock * 65536 / ((uint64_t)NES_APU_SAMPLE_RATE * 32 * (tri->cur_period + 1))) : 0;

    // Noise 状态 - 使用NTSC周期查找表
    noise_t* noi = &apu->noise;
    const uint8_t noi_active = apu->status_noise && noi->length_counter > 0;
    const uint8_t noi_vol = noi_active ? (noi->constant_volume ? noi->volume_envelope : noi->envelope_volume) : 0;
    const uint16_t noi_period = noise_period_table[noi->noise_period];
    const uint32_t noi_inc = noi_active ?
        (uint32_t)((uint64_t)nes->timing.cpu_clock * 65536 / ((uint64_t)NES_APU_SAMPLE_RATE * noi_period)) : 0;
    const uint8_t noi_mode = noi->loop_noise;

    // DMC: 每个输出采样把定时器推进"每采样对应的 CPU 周期"（1/256 周期定点），
    // 这样 DAC 电平的变化跟得上采样率，而不是每 1/4 帧才跳一次。
    uint32_t dmc_step_q8;
    {
        const uint32_t seg_samples = (uint32_t)(sample_end - sample_start);
        const uint32_t seg_cycles = (uint32_t)(nes->timing.cpu_clock / ((uint32_t)4u * nes->timing.frame_rate));
        dmc_step_q8 = (seg_samples != 0u) ? (uint32_t)(((uint64_t)seg_cycles << 8) / seg_samples) : 0u;
    }
    // 缓存到局部变量加速热循环
    uint32_t p1_phase = p1->phase_acc;
    uint32_t p2_phase = p2->phase_acc;
    uint32_t tri_phase = tri->phase_acc;
    uint32_t noi_acc = noi->lfsr_acc;
    uint16_t lfsr = noi->lfsr;

    for (uint16_t i = sample_start; i < sample_end; i++) {
        // Pulse 1: step = phase / (65536/8) = phase >> 13
        const uint8_t p1_out = p1_duty[(p1_phase >> 13) & 7] * p1_vol;
        p1_phase += p1_inc;

        // Pulse 2
        const uint8_t p2_out = p2_duty[(p2_phase >> 13) & 7] * p2_vol;
        p2_phase += p2_inc;

        // Triangle: step = phase / (65536/32) = phase >> 11
        const uint8_t tri_out = tri_active ? apu_triangle_wave[(tri_phase >> 11) & 31] : 0;
        tri_phase += tri_inc;

        // Noise LFSR
        uint8_t noi_out = 0;
        if (noi_active) {
            noi_acc += noi_inc;
            uint32_t steps = noi_acc >> 16;
            noi_acc &= 0xFFFF;
            for (uint32_t s = 0; s < steps; s++) {
                uint16_t fb;
                if (noi_mode) {
                    fb = ((lfsr ^ (lfsr >> 6)) & 1) << 14;
                } else {
                    fb = ((lfsr ^ (lfsr >> 1)) & 1) << 14;
                }
                lfsr = (lfsr >> 1) | fb;
            }
            noi_out = (uint8_t)((lfsr & 1) * noi_vol);
        }

        // 混音: 线性近似 https://www.nesdev.org/wiki/APU_Mixer#Linear_Approximation
        // pulse_out ≈ 0.00752*(p1+p2), tnd_out ≈ 0.00851*tri + 0.00494*noise + 0.00335*dmc
        // 乘以256并使用定点 >>7: (247*(p1+p2) + 279*tri + 162*noise + 110*dmc) >> 7
        const uint8_t dmc_out = apu->dmc.out_level;
        uint16_t mixed = (uint16_t)((247 * ((uint16_t)p1_out + p2_out) + 279 * tri_out + 162 * noi_out
                                     + 110 * dmc_out) >> 7);
        apu->sample_buffer[i] = (uint8_t)(mixed > 255 ? 255 : mixed);

        // DMC 定时器推进（只在本通道使能时才有开销）
        if (apu->dmc.enabled) nes_apu_dmc_advance(nes, dmc_step_q8);
    }

    // 写回相位累加器
    p1->phase_acc = p1_phase;
    p2->phase_acc = p2_phase;
    tri->phase_acc = tri_phase;
    noi->lfsr_acc = noi_acc;
    noi->lfsr = lfsr;

    // 每4段输出一次音频(每视频帧一次)
    if (seg == 3) {
        nes_sound_output(apu->sample_buffer, nes->timing.samples_per_frame);
    }
}

#if (NES_ENABLE_EXPANSION_AUDIO == 1)
/* Cartridge expansion audio is mixed in per segment: one indirect call per 1/4 frame, and
 * only for the boards that actually carry a chip (mapper_audio == NES_APU_EXP_NONE is the
 * common case and returns immediately). */
static inline void nes_apu_render_expansion(nes_t* nes) {
    nes_apu_t* apu = &nes->nes_apu;
    const uint16_t seg = (uint16_t)(apu->clock_count & 3);
    const uint16_t sample_start = (uint16_t)(seg * nes->timing.samples_per_frame / 4);
    const uint16_t sample_end = (uint16_t)((seg + 1) * nes->timing.samples_per_frame / 4);
    const uint32_t seg_cycles = (uint32_t)(nes->timing.cpu_clock / ((uint32_t)4u * nes->timing.frame_rate));
    nes_apu_expansion_render(nes, apu->sample_buffer, sample_start,
                         (uint16_t)(sample_end - sample_start), seg_cycles);
}
#endif

static inline void nes_apu_frame_irq(nes_t *nes){
    if (nes->nes_apu.irq_inhibit_flag==0){
        nes->nes_apu.frame_interrupt = 1;
        nes_apu_update_irq_line(nes);
    }
}

/*
 * DMC ($4010-$4013).  The sample is fetched one byte at a time through the CPU bus and
 * decoded one bit per timer tick; the DAC level is what the mixer adds in.
 * https://www.nesdev.org/wiki/APU_DMC
 */
static const uint16_t dmc_rate_table[16] = {
    428, 380, 340, 320, 286, 254, 226, 214, 190, 160, 142, 128, 106, 84, 72, 54
};

static void nes_apu_dmc_stop(dmc_t* d) {
    d->bytes_remaining = 0;
    d->silence = 1;
    d->bits_remaining = 0;
}

/* $4015 bit 4 written as 1: (re)start the sample when nothing is playing. */
static void nes_apu_dmc_restart(dmc_t* d) {
    d->cur_address = (uint16_t)(0xC000u + ((uint16_t)d->sample_address << 6));
    d->bytes_remaining = (uint16_t)(((uint16_t)d->sample_length << 4) + 1u);
    d->silence = 0;
    d->bits_remaining = 0;
}

static void nes_apu_dmc_tick(nes_t* nes) {
    dmc_t* d = &nes->nes_apu.dmc;

    if (!d->enabled) return;

    if (!d->silence) {
        if (d->bits_remaining == 0u) {
            d->shift_reg = d->sample_buffer;
            d->bits_remaining = 8u;
        }
        if (d->shift_reg & 1u) {
            if (d->out_level <= 125u) d->out_level = (uint8_t)(d->out_level + 2u);
        } else {
            if (d->out_level >= 2u)   d->out_level = (uint8_t)(d->out_level - 2u);
        }
        d->shift_reg >>= 1;
        d->bits_remaining--;
    }

    /* Refill the sample buffer when it has been shifted out. */
    if (d->bits_remaining == 0u && d->bytes_remaining > 0u) {
        d->sample_buffer = nes_cpu_dma_read(nes, d->cur_address);
        d->cur_address = (d->cur_address == 0xFFFFu) ? 0x8000u : (uint16_t)(d->cur_address + 1u);
        d->bytes_remaining--;
        if (d->bytes_remaining == 0u) {
            if (d->loop) {
                nes_apu_dmc_restart(d);
            } else {
                d->silence = 1;
                if (d->irq_enable) {
                    d->irq_flag = 1;
                    nes_apu_update_irq_line(nes);
                }
            }
        }
    }
}

/* Advance the DMC by `cycles_q8` (1/256 CPU cycle units). */
static inline void nes_apu_dmc_advance(nes_t* nes, uint32_t cycles_q8) {
    dmc_t* d = &nes->nes_apu.dmc;
    uint32_t period = (uint32_t)d->timer_period << 8;
    if (period == 0u) period = 256u;
    d->timer_acc += cycles_q8;
    while (d->timer_acc >= period) {
        d->timer_acc -= period;
        nes_apu_dmc_tick(nes);
        if (!d->enabled) { d->timer_acc = 0; break; }
    }
}

/*
https://www.nesdev.org/wiki/APU#Frame_Counter_($4017)
https://www.nesdev.org/wiki/APU_Frame_Counter

mode 0:    mode 1:       function
---------  -----------  -----------------------------
 - - - f    - - - - -    IRQ (if bit 6 is clear)
 - l - l    - l - - l    Length counter and sweep
 e e e e    e e e - e    Envelope and linear counter
*/
void nes_apu_frame(nes_t* nes){
    if(nes->nes_apu.mode){// 5 step mode
        switch(nes->nes_apu.clock_count % 5){
            case 0:
                nes_apu_envelopes_and_linear_counter(nes);
                break;
            case 1:
                nes_apu_length_counter_and_sweep(nes);
                nes_apu_envelopes_and_linear_counter(nes);
                break;
            case 2:
                nes_apu_envelopes_and_linear_counter(nes);
                break;
            case 4:
                nes_apu_length_counter_and_sweep(nes);
                nes_apu_envelopes_and_linear_counter(nes);
                break;
        }
    }else{  // 4 step mode
        switch(nes->nes_apu.clock_count % 4){
            case 0:
                nes_apu_envelopes_and_linear_counter(nes);
                break;
            case 1:
                nes_apu_length_counter_and_sweep(nes);
                nes_apu_envelopes_and_linear_counter(nes);
                break;
            case 2:
                nes_apu_envelopes_and_linear_counter(nes);
                break;
            case 3:
                nes_apu_frame_irq(nes);
                nes_apu_length_counter_and_sweep(nes);
                nes_apu_envelopes_and_linear_counter(nes);
                break;
            default:
                break;
        }
    }
    nes_apu_play(nes);
#if (NES_ENABLE_EXPANSION_AUDIO == 1)
    nes_apu_render_expansion(nes);
#endif
    nes->nes_apu.clock_count++;
}

void nes_apu_init(nes_t *nes){
    nes->nes_apu.status = 0;
    nes->nes_apu.noise.lfsr = 1;
    /* DMC idle: 4KB sample window, rate index 0, DAC at 0, no IRQ. */
    nes->nes_apu.dmc.enabled = 0;
    nes->nes_apu.dmc.silence = 1;
    nes->nes_apu.dmc.timer_period = dmc_rate_table[0];
    nes->nes_apu.dmc.out_level = 0;
    nes->nes_apu.dmc.bytes_remaining = 0;
    nes->nes_apu.dmc.irq_flag = 0;
    nes->nes_apu.dmc.timer_acc = 0;
    nes->nes_apu.irq_line = 0;
#if (NES_ENABLE_EXPANSION_AUDIO == 1)
    nes_apu_expansion_init(nes);
#endif
}

uint8_t nes_read_apu_register(nes_t *nes,uint16_t address){
    uint8_t data = 0;
    if(address==0x4015){
        /* Bits 6/7 are the live interrupt flags (frame counter and DMC), not whatever was
         * last written to the enable register. */
        data = (uint8_t)((nes->nes_apu.frame_interrupt ? 0x40u : 0u) |
                         (nes->nes_apu.dmc.irq_flag ? 0x80u : 0u));

        if (nes->nes_apu.pulse1.length_counter) data |= 1;
        if (nes->nes_apu.pulse2.length_counter) data |= (1 << 1);
        if (nes->nes_apu.triangle.length_counter) data |= (1 << 2);
        if (nes->nes_apu.noise.length_counter) data |= (1 << 3);
        if (nes->nes_apu.dmc.bytes_remaining) data |= (1 << 4);

        /* Reading $4015 acknowledges the APU's own interrupts only.  It must NOT clear
         * nes_cpu.irq_pending: that flag also carries the mapper IRQ line (MMC3 & friends
         * acknowledge by writing it directly), and wiping it here silently ate those. */
        nes->nes_apu.frame_interrupt = 0;
        nes->nes_apu.dmc.irq_flag = 0;
        nes_apu_update_irq_line(nes);
    }else{
        NES_LOG_DEBUG("nes_read apu %04X %02X\n",address,data);
    }
    return data;
}

void nes_write_apu_register(nes_t* nes,uint16_t address,uint8_t data){
    switch(address){
        // Pulse ($4000–$4007)
        // Pulse0 ($4000–$4003)
        case 0x4000:
            nes->nes_apu.pulse1.control0=data;
            break;
        case 0x4001:
            nes->nes_apu.pulse1.control1=data;
            nes->nes_apu.pulse1.sweep_reload=1;
            break;
        case 0x4002:
            nes->nes_apu.pulse1.timer_low=data;
            nes->nes_apu.pulse1.cur_period=nes->nes_apu.pulse1.timer_high<<8|nes->nes_apu.pulse1.timer_low;
            break;
        case 0x4003:
            nes->nes_apu.pulse1.control3=data;
            if (nes->nes_apu.status_pulse1){
                nes->nes_apu.pulse1.length_counter = length_counter_table[nes->nes_apu.pulse1.len_counter_load];
            }
            nes->nes_apu.pulse1.cur_period=nes->nes_apu.pulse1.timer_high<<8|nes->nes_apu.pulse1.timer_low;
            nes->nes_apu.pulse1.envelope_restart = 1;
            nes->nes_apu.pulse1.phase_acc = 0;
            break;
        // Pulse1 ($4004–$4007)
        case 0x4004:
            nes->nes_apu.pulse2.control0=data;
            break;
        case 0x4005:
            nes->nes_apu.pulse2.control1=data;
            nes->nes_apu.pulse2.sweep_reload=1;
            break;
        case 0x4006:
            nes->nes_apu.pulse2.timer_low=data;
            nes->nes_apu.pulse2.cur_period=nes->nes_apu.pulse2.timer_high<<8|nes->nes_apu.pulse2.timer_low;
            break;
        case 0x4007:
            nes->nes_apu.pulse2.control3=data;
            if (nes->nes_apu.status_pulse2){
                nes->nes_apu.pulse2.length_counter = length_counter_table[nes->nes_apu.pulse2.len_counter_load];
            }
            nes->nes_apu.pulse2.cur_period=nes->nes_apu.pulse2.timer_high<<8|nes->nes_apu.pulse2.timer_low;
            nes->nes_apu.pulse2.envelope_restart = 1;
            nes->nes_apu.pulse2.phase_acc = 0;
            break;
        // Triangle ($4008–$400B)
        case 0x4008:
            nes->nes_apu.triangle.control0=data;
            break;
        // case 0x4009:
        //     break;
        case 0x400A:
            nes->nes_apu.triangle.timer_low=data;
            nes->nes_apu.triangle.cur_period=nes->nes_apu.triangle.timer_high<<8|nes->nes_apu.triangle.timer_low;
            break;
        case 0x400B:
            nes->nes_apu.triangle.control3=data;
            if (nes->nes_apu.status_triangle){
                nes->nes_apu.triangle.length_counter = length_counter_table[nes->nes_apu.triangle.len_counter_load];
            }
            nes->nes_apu.triangle.cur_period=nes->nes_apu.triangle.timer_high<<8|nes->nes_apu.triangle.timer_low;
            nes->nes_apu.triangle.linear_restart = 1;
            break;
        // Noise ($400C–$400F)
        case 0x400C:
            nes->nes_apu.noise.control0=data;
            break;
        // case 0x400D:
        //     break;
        case 0x400E:
            nes->nes_apu.noise.control2=data;
            break;
        case 0x400F:
            nes->nes_apu.noise.control3=data;
            if (nes->nes_apu.status_noise){
                nes->nes_apu.noise.length_counter = length_counter_table[nes->nes_apu.noise.len_counter_load];
            }
            nes->nes_apu.noise.envelope_restart = 1;
            break;
        // DMC ($4010–$4013)
        case 0x4010:
            nes->nes_apu.dmc.control0=data;
            nes->nes_apu.dmc.timer_period = dmc_rate_table[nes->nes_apu.dmc.frequency];
            if (nes->nes_apu.dmc.irq_enable) {
                /* Writing $4010 with the IRQ enable bit set clears the DMC interrupt flag. */
                nes->nes_apu.dmc.irq_flag = 0;
                nes_apu_update_irq_line(nes);
            }
            break;
        case 0x4011:
            /* 7-bit DAC load: takes effect immediately. */
            nes->nes_apu.dmc.control1=data;
            nes->nes_apu.dmc.out_level = (uint8_t)(data & 0x7Fu);
            break;
        case 0x4012:
            nes->nes_apu.dmc.sample_address=data;
            break;
        case 0x4013:
            nes->nes_apu.dmc.sample_length=data;
            break;
        // case 0x4014:
        //     break;
        // Status ($4015) https://www.nesdev.org/wiki/APU#Status_($4015)
        case 0x4015:
            nes->nes_apu.status=data;
            if (nes->nes_apu.status_pulse1==0){
                nes->nes_apu.pulse1.length_counter=0;
            }
            if (nes->nes_apu.status_pulse2==0){
                nes->nes_apu.pulse2.length_counter=0;
            }
            if (nes->nes_apu.status_triangle==0){
                nes->nes_apu.triangle.length_counter=0;
            }
            if (nes->nes_apu.status_noise==0){
                nes->nes_apu.noise.length_counter=0;
            }
            /* DMC enable: 0->1 restarts the sample when nothing is playing, 1->0 stops it
             * and clears the interrupt flag. */
            if (nes->nes_apu.status_dmc) {
                if (nes->nes_apu.dmc.bytes_remaining == 0u) {
                    nes_apu_dmc_restart(&nes->nes_apu.dmc);
                }
            } else {
                nes_apu_dmc_stop(&nes->nes_apu.dmc);
                nes->nes_apu.dmc.irq_flag = 0;
            }
            nes->nes_apu.dmc.enabled = nes->nes_apu.status_dmc;
            nes_apu_update_irq_line(nes);
            break;
        case 0x4017:
            nes->nes_apu.frame_counter=data;
            if (nes->nes_apu.irq_inhibit_flag){
                /* IRQ inhibit de-asserts the frame counter's contribution to the APU line
                 * (and only that: a mapper IRQ is a different source, see $4015). */
                nes->nes_apu.frame_interrupt = 0;
                nes_apu_update_irq_line(nes);
            }
            /* Writing $4017 resets the frame counter sequence. */
            nes->nes_apu.clock_count = 0;
            if (nes->nes_apu.mode){
                nes_apu_length_counter_and_sweep(nes);
                nes_apu_envelopes_and_linear_counter(nes);
            }
            break;
        default:
            NES_LOG_DEBUG("nes_write apu %04X %02X\n",address,data);
            break;
    }
}


/* ==== cartridge expansion audio (moved here from the expansion audio module) ==== */
#if (NES_ENABLE_EXPANSION_AUDIO == 1)

   
/*
 * VRC6 (Konami, mapper 24/26) - Authority: Mesen2 Core/NES/Mappers/Audio/Vrc6Audio.h,
 * Vrc6Pulse.h and Vrc6Saw.h.  Three channels clocked once per CPU cycle:
 *   $9000-$9002 pulse 1, $A000-$A002 pulse 2, $B000-$B002 saw, $9003 = halt + frequency shift.
 *   pulse: reg0 = volume(0-3)/duty(4-6)/ignore-duty(7), reg1 = frequency low,
 *          reg2 = frequency high(0-3)/enable(7); every (frequency >> shift) + 1 cycles the
 *          16-step duty counter advances, and the channel outputs its volume while
 *          step <= dutyCycle (or always, when ignore-duty is set).
 *   saw:   reg0 = accumulator rate(0-5), reg1/reg2 = frequency (8 bit + 4 bit), enable = reg2
 *          bit 7; a 14-step counter adds the rate on every second step and resets the
 *          accumulator on step 0; the top 5 accumulator bits are the output.
 * The chip's total contribution is (pulse1 + pulse2 + saw) * 15.
 *
 * Namco 163 (mapper 19/210) - Authority: Mesen2 Core/NES/Mappers/Audio/Namco163Audio.h.
 *   $4800-$4FFF  : write/read the 128-byte internal RAM at the current position (auto-increment)
 *   $E000        : bit 6 disables the sound
 *   $F800        : bits 0-6 set the RAM position, bit 7 enables auto-increment
 *   The 8 channels live in RAM at 0x40 + channel * 8 (frequency low/mid/high, phase
 *   low/mid/high, wave address, wave length, volume).  Every 15 CPU cycles one channel is
 *   updated, round-robin over the audible ones (8 - count .. 7, where count = ram[0x7F] >> 4).
 *   A channel reads a 4-bit sample from the wave RAM and outputs (sample - 8) * volume; the
 *   chip's level is the sum of the audible channels divided by (count + 1).
 */

/* ------------------------------------------------------------------ N163 ---- */

static uint32_t n163_frequency(nes_apu_exp_t* a, uint8_t channel) {
    const uint8_t base = (uint8_t)(0x40u + channel * 8u);
    return ((uint32_t)(a->n163_ram[base + 4] & 0x03u) << 16) |
           ((uint32_t)a->n163_ram[base + 2] << 8) |
           (uint32_t)a->n163_ram[base + 0];
}

static void n163_set_phase(nes_apu_exp_t* a, uint8_t channel, uint32_t phase) {
    const uint8_t base = (uint8_t)(0x40u + channel * 8u);
    a->n163_ram[base + 5] = (uint8_t)((phase >> 16) & 0xFFu);
    a->n163_ram[base + 3] = (uint8_t)((phase >> 8) & 0xFFu);
    a->n163_ram[base + 1] = (uint8_t)(phase & 0xFFu);
}

static uint8_t n163_channel_count(nes_apu_exp_t* a) {
    return (uint8_t)((a->n163_ram[0x7F] >> 4) & 0x07u);
}

static int16_t n163_output_level(nes_apu_exp_t* a) {
    const uint8_t count = n163_channel_count(a);
    int16_t summed = 0;
    for (int i = 7, min = 7 - count; i >= min; i--) {
        summed = (int16_t)(summed + a->n163_channel_out[i]);
    }
    return (int16_t)(summed / (int16_t)(count + 1u));
}

static void n163_update_channel(nes_apu_exp_t* a, uint8_t channel) {
    const uint8_t base = (uint8_t)(0x40u + channel * 8u);
    const uint32_t freq = n163_frequency(a, channel);
    const uint16_t length = (uint16_t)(256u - (a->n163_ram[base + 4] & 0xFCu));
    const uint8_t  offset = a->n163_ram[base + 6];
    const uint8_t  volume = (uint8_t)(a->n163_ram[base + 7] & 0x0Fu);

    uint32_t phase = ((uint32_t)a->n163_ram[base + 5] << 16) |
                     ((uint32_t)a->n163_ram[base + 3] << 8) |
                     (uint32_t)a->n163_ram[base + 1];
    const uint32_t modulo = (uint32_t)length << 16;
    phase = (uint32_t)((phase + freq) % modulo);

    const uint8_t sample_position = (uint8_t)(((phase >> 16) + offset) & 0xFFu);
    int8_t sample;
    if (sample_position & 0x01u) {
        sample = (int8_t)(a->n163_ram[sample_position / 2] >> 4);
    } else {
        sample = (int8_t)(a->n163_ram[sample_position / 2] & 0x0Fu);
    }

    a->n163_channel_out[channel] = (int16_t)((sample - 8) * (int8_t)volume);
    a->n163_last_output = n163_output_level(a);
    n163_set_phase(a, channel, phase);
}

/* Advance the N163 by `cycles_q8` (1/256 CPU cycle units) and return its level. */
static int16_t n163_advance(nes_apu_exp_t* a, uint32_t cycles_q8) {
    if (a->n163_disable) return 0;

    a->n163_acc += cycles_q8;
    /* One channel update every 15 CPU cycles. */
    while (a->n163_acc >= (15u << 8)) {
        a->n163_acc -= (15u << 8);
        n163_update_channel(a, (uint8_t)a->n163_current_channel);
        a->n163_current_channel--;
        if (a->n163_current_channel < (int8_t)(7 - n163_channel_count(a))) {
            a->n163_current_channel = 7;
        }
    }
    return a->n163_last_output;
}

/* ------------------------------------------------------------------ VRC6 ---- */

static void vrc6_pulse_write(nes_apu_exp_t* a, uint8_t index, uint16_t address, uint8_t value) {
    switch (address & 0x03u) {
    case 0:
        a->vrc6_pulse[index].volume = (uint8_t)(value & 0x0Fu);
        a->vrc6_pulse[index].duty = (uint8_t)((value & 0x70u) >> 4);
        a->vrc6_pulse[index].ignore_duty = (uint8_t)((value & 0x80u) != 0u);
        break;
    case 1:
        a->vrc6_pulse[index].frequency =
            (uint16_t)((a->vrc6_pulse[index].frequency & 0x0F00u) | value);
        break;
    default:
        a->vrc6_pulse[index].frequency =
            (uint16_t)((a->vrc6_pulse[index].frequency & 0x00FFu) | ((uint16_t)(value & 0x0Fu) << 8));
        a->vrc6_pulse[index].enabled = (uint8_t)((value & 0x80u) != 0u);
        if (!a->vrc6_pulse[index].enabled) {
            a->vrc6_pulse[index].step = 0;
        }
        break;
    }
}

static void vrc6_saw_write(nes_apu_exp_t* a, uint16_t address, uint8_t value) {
    switch (address & 0x03u) {
    case 0:
        a->vrc6_saw.acc_rate = (uint8_t)(value & 0x3Fu);
        break;
    case 1:
        a->vrc6_saw.frequency = (uint16_t)((a->vrc6_saw.frequency & 0x0F00u) | value);
        break;
    default:
        a->vrc6_saw.frequency =
            (uint16_t)((a->vrc6_saw.frequency & 0x00FFu) | ((uint16_t)(value & 0x0Fu) << 8));
        a->vrc6_saw.enabled = (uint8_t)((value & 0x80u) != 0u);
        if (!a->vrc6_saw.enabled) {
            a->vrc6_saw.accumulator = 0;
            a->vrc6_saw.step = 0;
        }
        break;
    }
}

static uint8_t vrc6_pulse_volume(const nes_apu_exp_t* a, uint8_t index) {
    const uint8_t volume = a->vrc6_pulse[index].volume;
    if (!a->vrc6_pulse[index].enabled) return 0;
    if (a->vrc6_pulse[index].ignore_duty) return volume;
    return (a->vrc6_pulse[index].step <= a->vrc6_pulse[index].duty) ? volume : 0u;
}

/* Clock the three VRC6 channels once per CPU cycle and return the summed level. */
static void vrc6_clock(nes_apu_exp_t* a) {
    if (a->vrc6_halt) return;

    for (uint8_t i = 0; i < 2u; i++) {
        if (a->vrc6_pulse[i].enabled) {
            a->vrc6_pulse[i].timer--;
            if (a->vrc6_pulse[i].timer == 0) {
                a->vrc6_pulse[i].step = (uint8_t)((a->vrc6_pulse[i].step + 1u) & 0x0Fu);
                a->vrc6_pulse[i].timer =
                    (int32_t)(a->vrc6_pulse[i].frequency >> a->vrc6_shift) + 1;
            }
        }
    }

    if (a->vrc6_saw.enabled) {
        a->vrc6_saw.timer--;
        if (a->vrc6_saw.timer == 0) {
            a->vrc6_saw.step = (uint8_t)((a->vrc6_saw.step + 1u) % 14u);
            a->vrc6_saw.timer = (int32_t)(a->vrc6_saw.frequency >> a->vrc6_shift) + 1;
            if (a->vrc6_saw.step == 0u) {
                a->vrc6_saw.accumulator = 0;
            } else if ((a->vrc6_saw.step & 0x01u) == 0u) {
                a->vrc6_saw.accumulator =
                    (uint8_t)(a->vrc6_saw.accumulator + a->vrc6_saw.acc_rate);
            }
        }
    }
}

static uint8_t vrc6_level(const nes_apu_exp_t* a) {
    const uint8_t pulses = (uint8_t)(vrc6_pulse_volume(a, 0) + vrc6_pulse_volume(a, 1));
    const uint8_t saw = a->vrc6_saw.enabled ? (uint8_t)(a->vrc6_saw.accumulator >> 3) : 0u;
    return (uint8_t)(pulses + saw);
}

/* --------------------------------------------------------------- Sunsoft 5B ---- */

/*
 * Sunsoft 5B (mapper 69) - Authority: Mesen2 Core/NES/Mappers/Audio/Sunsoft5bAudio.h.
 *   $C000 = register select, $E000 = data (registers 0-15).
 *   regs[ch*2] / regs[ch*2+1] = 16-bit tone period, regs[7] bits 0-2 disable tone,
 *   regs[8+ch] bits 0-3 = volume (through a +1.5 dB per step table).
 *   The three channels tick on every SECOND CPU cycle; a 16-step counter driven by the
 *   period outputs the channel volume while step < 8 (a 50% square).
 *
 * Mesen (and this implementation) sum the tone channels only: the envelope generator and
 * the noise channel are not synthesised, which matches the reference implementation.
 */
static void s5b_build_volume_lut(nes_apu_exp_t* a) {
    /* output *= 1.1885022^2 per step, kept in Q16 and truncated like Mesen's double. */
    uint32_t output_q16 = 65536u;                     /* 1.0 */
    a->s5b_volume_lut[0] = 0;
    for (int i = 1; i < 0x10; i++) {
        output_q16 = (uint32_t)(((uint64_t)output_q16 * 92564u) >> 16);   /* *1.41253 */
        a->s5b_volume_lut[i] = (uint8_t)(output_q16 >> 16);
    }
}

static void s5b_clock(nes_apu_exp_t* a) {
    for (int ch = 0; ch < 3; ch++) {
        a->s5b_timer[ch]--;
        if (a->s5b_timer[ch] <= 0) {
            const uint16_t period = (uint16_t)(a->s5b_regs[ch * 2] |
                                               ((uint16_t)a->s5b_regs[ch * 2 + 1] << 8));
            a->s5b_timer[ch] = (int16_t)period;
            a->s5b_step[ch] = (uint8_t)((a->s5b_step[ch] + 1u) & 0x0Fu);
        }
    }
}

static uint16_t s5b_level(const nes_apu_exp_t* a) {
    uint16_t summed = 0;
    for (int ch = 0; ch < 3; ch++) {
        const uint8_t tone_enabled = (uint8_t)(((a->s5b_regs[7] >> ch) & 0x01u) == 0u);
        if (tone_enabled && a->s5b_step[ch] < 0x08u) {
            summed = (uint16_t)(summed + a->s5b_volume_lut[a->s5b_regs[8 + ch] & 0x0Fu]);
        }
    }
    return summed;
}

/* ------------------------------------------------------------------- MMC5 ---- */

/*
 * MMC5 audio (mapper 5) - Authority: Mesen2 Core/NES/Mappers/Audio/Mmc5Audio.h.
 *   $5000-$5003 / $5004-$5007 : two pulse channels with the APU's register layout
 *     (reg0 = volume/duty/halt/constant, reg1 = unused - the MMC5 pulses have no sweep,
 *      reg2 = timer low, reg3 = timer high + length counter load)
 *   $5010 : bit 0 = PCM read mode, bit 7 = PCM IRQ enable (IRQs are not implemented)
 *   $5011 : 8-bit PCM DAC - a written 0 keeps the previous level, and writes are ignored
 *           while read mode is on
 *   $5015 : bit 0/1 enable square 1/2; reading it returns the two length counter flags
 *
 * The two squares tick once per CPU cycle and advance their 8-step duty every
 * 2 * (period + 1) CPU cycles, so a given period sounds at the APU pulse's pitch; the
 * envelope and length counter run off a ~240 Hz tick.  MMC5 output polarity is reversed
 * compared to the APU, so the summed level is negated.
 */

/* Same duty patterns as the APU pulse channels. */
static const uint8_t mmc5_duty_table[4][8] = {
    { 0, 1, 0, 0, 0, 0, 0, 0 },
    { 0, 1, 1, 0, 0, 0, 0, 0 },
    { 0, 1, 1, 1, 1, 0, 0, 0 },
    { 1, 0, 0, 1, 1, 1, 1, 1 }
};

static const uint8_t mmc5_length_table[32] = {
    10, 254, 20, 2, 40, 4, 80, 6, 160, 8, 60, 10, 14, 12, 26, 14,
    12, 16, 24, 18, 48, 20, 96, 22, 192, 24, 72, 26, 16, 28, 32, 30
};

static void mmc5_square_write(nes_apu_exp_t* a, uint8_t index, uint16_t address, uint8_t value) {
    switch (address & 0x03u) {
    case 0:
        a->mmc5_square[index].duty = (uint8_t)((value >> 6) & 0x03u);
        a->mmc5_square[index].halt = (uint8_t)((value & 0x20u) != 0u);
        a->mmc5_square[index].constant_volume = (uint8_t)((value & 0x10u) != 0u);
        if (a->mmc5_square[index].constant_volume) {
            a->mmc5_square[index].volume = (uint8_t)(value & 0x0Fu);
        }
        break;
    case 1:
        /* No sweep unit on the MMC5 pulses: $5001/$5005 have no effect. */
        break;
    case 2:
        a->mmc5_square[index].period =
            (uint16_t)((a->mmc5_square[index].period & 0x0700u) | value);
        break;
    default:
        a->mmc5_square[index].period =
            (uint16_t)((a->mmc5_square[index].period & 0x00FFu) | ((uint16_t)(value & 0x07u) << 8));
        a->mmc5_square[index].length_reload = (uint8_t)(value >> 3);
        a->mmc5_square[index].env_start = 1;
        /* The length counter reloads immediately (there is no frame counter on this board). */
        if (a->mmc5_square[index].enabled) {
            a->mmc5_square[index].length_counter = mmc5_length_table[value >> 3];
        }
        break;
    }
}

/* Envelope + length counter tick (~240 Hz). */
static void mmc5_square_tick(nes_apu_exp_t* a, uint8_t index) {
    nes_apu_mmc5_square_t* sq = &a->mmc5_square[index];    /* ~240 Hz envelope / length counter tick. */
    if (sq->env_start) {
        sq->env_start = 0;
        sq->env_decay = 0x0Fu;
        sq->env_divider = (uint8_t)(sq->length_reload + 1u);
    } else if (sq->env_divider > 0u) {
        sq->env_divider--;
    } else {
        sq->env_divider = (uint8_t)(sq->length_reload + 1u);
        if (sq->env_decay > 0u) {
            sq->env_decay--;
        } else if (sq->halt) {
            sq->env_decay = 0x0Fu;
        }
    }
    if (!sq->constant_volume) {
        sq->volume = sq->env_decay;
    }
    if (!sq->halt && sq->length_counter > 0u) {
        sq->length_counter--;
    }
}

static uint8_t mmc5_square_output(const nes_apu_exp_t* a, uint8_t index) {
    const nes_apu_mmc5_square_t* sq = &a->mmc5_square[index];
    if (!sq->enabled || sq->length_counter == 0u) return 0;
    return mmc5_duty_table[sq->duty][sq->duty_pos];
}

/* Mix one segment's worth of MMC5 audio.  `step_q8` is CPU cycles per sample in 1/256. */
static void mmc5_render(nes_t* nes, uint8_t* buffer, uint16_t start, uint16_t count,
                        uint32_t step_q8) {
    nes_apu_exp_t* a = &nes->nes_apu.exp_audio;
    /* One tick every 240th of a second: 1.789773 MHz / 240 = 7457 CPU cycles. */
    const uint32_t tick_q8 = 7457u << 8;

    for (uint16_t i = 0; i < count; i++) {
        for (uint8_t ch = 0; ch < 2u; ch++) {
            nes_apu_mmc5_square_t* sq = &a->mmc5_square[ch];
            /* The duty timer is a countdown in 1/256 CPU cycles, so the channel keeps the
               APU pulse pitch: one duty step per 2 * (period + 1) CPU cycles. */
            sq->timer -= (int32_t)step_q8;
            if (sq->timer <= 0) {
                sq->duty_pos = (uint8_t)((sq->duty_pos - 1u) & 0x07u);
                sq->timer += (int32_t)((2u * ((uint32_t)sq->period + 1u)) << 8);
            }
            sq->tick_acc += step_q8;
            while (sq->tick_acc >= tick_q8) {
                sq->tick_acc -= tick_q8;
                mmc5_square_tick(a, ch);
            }
        }

        const int32_t sq1 = (int32_t)mmc5_square_output(a, 0) * (int32_t)a->mmc5_square[0].volume;
        const int32_t sq2 = (int32_t)mmc5_square_output(a, 1) * (int32_t)a->mmc5_square[1].volume;
        /* "The sound output of the square channels are equivalent in volume to the
           corresponding APU channels" -> reuse the APU pulse weight (247/128).
           Mesen adds the raw unsigned PCM DAC to that sum; it is halved here so the DAC's
           DC offset (which the NES's analogue high-pass removes) cannot swamp this 8-bit
           mixer.  At power-on the DAC reads 0, so the channel stays silent either way. */
        const int32_t squares = (sq1 + sq2) * 247 / 128;
        const int32_t pcm = (int32_t)a->mmc5_pcm_output / 2;
        const int32_t level = -(squares + pcm);          /* MMC5 polarity is reversed */
        const int32_t mixed = (int32_t)buffer[start + i] + level;
        buffer[start + i] = (uint8_t)(mixed < 0 ? 0 : (mixed > 255 ? 255 : mixed));
    }
}

/* ------------------------------------------------------------------- API ---- */


void nes_apu_expansion_init(nes_t* nes) {
    nes_memset(&nes->nes_apu.exp_audio.vrc7, 0, sizeof(vrc7_t));
    nes_apu_exp_t* a = &nes->nes_apu.exp_audio;
    nes_memset(a, 0, sizeof(nes_apu_exp_t));
    a->vrc6_pulse[0].frequency = 1;
    a->vrc6_pulse[1].frequency = 1;
    a->vrc6_saw.frequency = 1;
    a->vrc6_pulse[0].timer = 1;
    a->vrc6_pulse[1].timer = 1;
    a->vrc6_saw.timer = 1;
    a->n163_current_channel = 7;
    s5b_build_volume_lut(a);
}

/* Quarter sine, Q15: sine[i] = sin((i + 0.5) * pi / 512).  One cycle is 1024 steps. */
static const int16_t vrc7_sine[256] = {
       101,    302,    503,    704,    905,   1106,   1307,   1507,
      1708,   1909,   2110,   2310,   2511,   2711,   2911,   3112,
      3312,   3512,   3712,   3911,   4111,   4310,   4509,   4708,
      4907,   5106,   5305,   5503,   5701,   5899,   6096,   6294,
      6491,   6688,   6885,   7081,   7277,   7473,   7669,   7864,
      8059,   8254,   8448,   8642,   8836,   9030,   9223,   9416,
      9608,   9800,   9992,  10183,  10374,  10564,  10754,  10944,
     11133,  11322,  11511,  11699,  11886,  12074,  12260,  12446,
     12632,  12817,  13002,  13187,  13370,  13554,  13736,  13919,
     14101,  14282,  14462,  14643,  14822,  15001,  15180,  15358,
     15535,  15712,  15888,  16063,  16238,  16413,  16586,  16759,
     16932,  17104,  17275,  17445,  17615,  17784,  17953,  18121,
     18288,  18454,  18620,  18785,  18950,  19113,  19276,  19438,
     19600,  19761,  19921,  20080,  20238,  20396,  20553,  20709,
     20865,  21019,  21173,  21326,  21479,  21630,  21781,  21930,
     22079,  22227,  22375,  22521,  22667,  22812,  22956,  23099,
     23241,  23382,  23522,  23662,  23801,  23938,  24075,  24211,
     24346,  24480,  24613,  24746,  24877,  25007,  25137,  25265,
     25393,  25519,  25645,  25770,  25893,  26016,  26138,  26259,
     26378,  26497,  26615,  26732,  26848,  26962,  27076,  27189,
     27300,  27411,  27521,  27629,  27737,  27843,  27949,  28053,
     28157,  28259,  28360,  28460,  28560,  28658,  28755,  28850,
     28945,  29039,  29131,  29223,  29313,  29403,  29491,  29578,
     29664,  29749,  29832,  29915,  29997,  30077,  30156,  30234,
     30311,  30387,  30462,  30535,  30607,  30679,  30749,  30818,
     30885,  30952,  31017,  31082,  31145,  31206,  31267,  31327,
     31385,  31442,  31498,  31553,  31607,  31659,  31710,  31760,
     31809,  31857,  31903,  31949,  31993,  32036,  32077,  32118,
     32157,  32195,  32232,  32267,  32302,  32335,  32367,  32397,
     32427,  32455,  32482,  32508,  32533,  32556,  32578,  32599,
     32619,  32637,  32655,  32671,  32685,  32699,  32711,  32722,
     32732,  32741,  32748,  32755,  32759,  32763,  32766,  32767,
};

/* The chip's built-in instrument ROM: 15 instruments x 8 bytes (hardware data).  Each is two
 * operators of AM/VIB/EG-type/KSR/MUL, KSL/TL, AR/DR, SL/RR. */
static const uint8_t vrc7_patch_rom[15][8] = {
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x03, 0x21, 0x05, 0x06, 0xE8, 0x81, 0x42, 0x27 },
    { 0x13, 0x41, 0x14, 0x0D, 0xD8, 0xF6, 0x23, 0x12 },
    { 0x11, 0x11, 0x08, 0x08, 0xFA, 0xB2, 0x20, 0x12 },
    { 0x31, 0x61, 0x0C, 0x07, 0xA8, 0x64, 0x61, 0x27 },
    { 0x32, 0x21, 0x1E, 0x06, 0xE1, 0x76, 0x01, 0x28 },
    { 0x02, 0x01, 0x06, 0x00, 0xA3, 0xE2, 0xF4, 0xF4 },
    { 0x21, 0x61, 0x1D, 0x07, 0x82, 0x81, 0x11, 0x07 },
    { 0x23, 0x21, 0x22, 0x17, 0xA2, 0x72, 0x01, 0x17 },
    { 0x35, 0x11, 0x25, 0x00, 0x40, 0x73, 0x72, 0x01 },
    { 0xB5, 0x01, 0x0F, 0x0F, 0xA8, 0xA5, 0x51, 0x02 },
    { 0x17, 0xC1, 0x24, 0x07, 0xF8, 0xF8, 0x22, 0x12 },
    { 0x71, 0x23, 0x11, 0x06, 0x65, 0x74, 0x18, 0x16 },
    { 0x01, 0x02, 0xD3, 0x05, 0xC9, 0x95, 0x03, 0x02 },
    { 0x61, 0x63, 0x0C, 0x00, 0x94, 0xC0, 0x33, 0xF6 },
};

/* ---- VRC7 (mapper 85): compact 6-channel 2-operator FM -----------------------------------
 * Our own model.  The register map and the 8-byte instrument-dump layout follow the YM2413/OPLL
 * documentation (nesdev wiki; Mesen2 Core/NES/Mappers/Konami/VRC7.h shows how the VRC7 wires the
 * chip).  The implementation, the sine table and all arithmetic are ours.
 *
 * Left out on purpose, to stay cheap on MCU targets:
 *   - rhythm mode (the VRC7 ignores register $0E),
 *   - the chip's internal 49716 Hz rate and its resampler: we synthesise at the APU sample rate
 *     and rescale the envelope, so the timing still sounds right,
 *   - feedback and KSL: an OPLL instrument dump carries neither.
 * TL is applied as a linear attenuation instead of the exact 0.75 dB/step table.
 */
#define VRC7_EG_OFF        (0)
#define VRC7_EG_ATTACK     (1)
#define VRC7_EG_DECAY      (2)
#define VRC7_EG_SUSTAIN    (3)
#define VRC7_EG_RELEASE    (4)
/* envelope rate 0..15 -> shift (0 = slowest) */
static const uint8_t vrc7_eg_shift[16] = { 14, 13, 12, 11, 10, 9, 9, 8, 8, 7, 6, 5, 4, 3, 2, 1 };
/* patch MUL 0..15 -> phase multiplier, doubled (MUL 0 means 0.5) */
static const uint8_t vrc7_mul_x2[16] = { 1, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 20, 24, 24, 30, 30 };

static inline int16_t vrc7_sine_at(uint32_t index) {
    const uint8_t quad = (uint8_t)((index >> 8) & 3u);
    const uint8_t pos = (uint8_t)(index & 0xFFu);
    const int16_t s = vrc7_sine[(quad & 1u) ? (uint8_t)(0xFFu - pos) : pos];
    return (quad & 2u) ? (int16_t)(-s) : s;
}

static void vrc7_decode(const uint8_t *dump, vrc7_slot_t *s) {
    s->mul = (uint8_t)(dump[0] & 0x0Fu);
    s->tl = (uint8_t)(dump[1] & 0x3Fu);
    s->ar = (uint8_t)((dump[2] >> 4) & 0x0Fu);
    s->dr = (uint8_t)(dump[2] & 0x0Fu);
    s->sl = (uint8_t)((dump[3] >> 4) & 0x0Fu);
    s->rr = (uint8_t)(dump[3] & 0x0Fu);
    s->am = (uint8_t)((dump[0] & 0x80u) != 0u);
    s->vib = (uint8_t)((dump[0] & 0x40u) != 0u);
}

/* instrument 0 = the $00-$07 user dump, 1..15 = the chip's built-in ROM */
static void vrc7_set_instrument(vrc7_t *v, uint8_t ch, uint8_t inst) {
    const uint8_t *d = (inst == 0u) ? v->inst_dump : vrc7_patch_rom[inst - 1u];
    vrc7_decode(&d[0], &v->slot[ch * 2u]);
    vrc7_decode(&d[4], &v->slot[ch * 2u + 1u]);
}

/* f = fnum * 2^(block-1) * 49716 / 2^19 Hz as Q16 sine steps per output sample:
 * inc = fnum * 2^block * 73871 / 1024.  (64-bit product: it exceeds 32 bits.) */
static void vrc7_update_inc(vrc7_t *v, uint8_t ch) {
    const uint8_t  ctl = v->reg[0x20u + ch];
    const uint16_t fnum = (uint16_t)(((uint16_t)(ctl & 0x01u) << 8) | v->reg[0x10u + ch]);
    const uint8_t  blk = (uint8_t)((ctl >> 1) & 0x07u);
    const uint32_t steps = (uint32_t)(((uint64_t)fnum << blk) * 73871u >> 10);
    v->slot[ch * 2u].inc = steps;
    v->slot[ch * 2u + 1u].inc = steps;
}

static void vrc7_key_on(vrc7_t *v, uint8_t ch, uint8_t on) {
    vrc7_slot_t *m = &v->slot[ch * 2u];
    vrc7_slot_t *c = &v->slot[ch * 2u + 1u];
    if (on) {
        m->key = c->key = 1u;
        m->eg_state = c->eg_state = VRC7_EG_ATTACK;
        if (m->eg == 0u) m->eg = 1u;
        if (c->eg == 0u) c->eg = 1u;
        v->key_status |= (uint8_t)(1u << ch);
    } else {
        m->key = c->key = 0u;
        m->eg_state = c->eg_state = VRC7_EG_RELEASE;
        v->key_status &= (uint8_t)~(1u << ch);
    }
    v->active = (uint8_t)(v->key_status != 0u);
}

static uint16_t vrc7_sustain_level(uint8_t sl) {
    return (uint16_t)(0x3FFu - (uint16_t)(((uint32_t)sl * 0x3FFu / 15u) * 3u / 4u));
}

static void vrc7_eg_tick(vrc7_slot_t *s) {
    switch (s->eg_state) {
    case VRC7_EG_ATTACK:
        if (s->ar == 0u) {          /* rate 0: no attack ramp, the note starts at peak */
            s->eg = 0x3FFu;
            s->eg_state = VRC7_EG_DECAY;
            break;
        }
        s->eg = (uint16_t)(s->eg + (((0x3FFu - s->eg) >> vrc7_eg_shift[s->ar]) + 1u));
        if (s->eg >= 0x3FFu) { s->eg = 0x3FFu; s->eg_state = VRC7_EG_DECAY; }
        break;
    case VRC7_EG_DECAY: {
        const uint16_t target = vrc7_sustain_level(s->sl);
        if (s->eg > target) {
            s->eg = (uint16_t)(s->eg - (((s->eg - target) >> vrc7_eg_shift[s->dr]) + 1u));
        }
        if (s->eg <= target) { s->eg = target; s->eg_state = VRC7_EG_SUSTAIN; }
        break;
    }
    case VRC7_EG_SUSTAIN:
        if (!s->key) s->eg_state = VRC7_EG_RELEASE;
        break;
    case VRC7_EG_RELEASE:
    default:
        if (s->eg > 1u) {
            s->eg = (uint16_t)(s->eg - ((s->eg >> vrc7_eg_shift[s->rr]) + 1u));
        } else {
            s->eg = 0u;
            s->eg_state = VRC7_EG_OFF;
        }
        break;
    }
}

/* one output sample: the modulator phase-modulates the carrier (no feedback path) */
static int16_t vrc7_sample(vrc7_t *v) {
    int32_t acc = 0;
    for (uint8_t ch = 0u; ch < VRC7_CHANNELS; ch++) {
        if ((v->key_status & (uint8_t)(1u << ch)) == 0u) continue;
        vrc7_slot_t *m = &v->slot[ch * 2u];
        vrc7_slot_t *c = &v->slot[ch * 2u + 1u];
        int32_t mod = vrc7_sine_at((m->phase >> 16) & 0x3FFu);
        int32_t gain = (int32_t)(m->eg >> 2);
        gain = (gain * (int32_t)(64u - m->tl)) >> 6;
        mod = (mod * gain) >> 8;
        int32_t idx = (int32_t)((c->phase >> 16) & 0x3FFu) + (mod >> 6);
        int32_t car = vrc7_sine_at((uint32_t)idx & 0x3FFu);
        gain = (int32_t)(c->eg >> 2);
        gain = (gain * (int32_t)(64u - c->tl)) >> 6;
        car = (car * gain) >> 8;
        /* $3x low nibble is an attenuation: 0 = loudest, 15 = quietest. */
        acc += (car * (int32_t)(16u - v->volume[ch])) >> 4;
        m->phase += (m->inc * vrc7_mul_x2[m->mul]) >> 1;
        c->phase += (c->inc * vrc7_mul_x2[c->mul]) >> 1;
    }
    return (int16_t)(acc >> 6);
}

static void nes_exp_vrc7_write(nes_t *nes, uint16_t address, uint8_t data) {
    vrc7_t *v = &nes->nes_apu.exp_audio.vrc7;
    switch (address & 0xF030u) {
    case 0x9010u:                                   /* register latch */
        v->current_reg = data;
        return;
    case 0x9030u:                                   /* data port */
        break;
    case 0xE000u:                                   /* bit6 mutes the chip */
        v->muted = (uint8_t)((data & 0x40u) != 0u);
        return;
    default:
        return;
    }
    if (v->muted) return;

    const uint8_t reg = (uint8_t)(v->current_reg & 0x3Fu);
    v->reg[reg] = data;
    if (reg <= 0x07u) {                             /* user instrument dump */
        v->inst_dump[reg] = data;
        for (uint8_t ch = 0u; ch < VRC7_CHANNELS; ch++) {
            if (((v->reg[0x30u + ch] >> 4) & 0x0Fu) == 0u) vrc7_set_instrument(v, ch, 0u);
        }
    } else if (reg >= 0x30u && reg <= 0x38u) {      /* instrument + volume */
        const uint8_t ch = (uint8_t)(reg - 0x30u);
        v->volume[ch] = (uint8_t)(data & 0x0Fu);
        vrc7_set_instrument(v, ch, (uint8_t)((data >> 4) & 0x0Fu));
    } else if (reg >= 0x10u && reg <= 0x18u) {      /* F-number low */
        vrc7_update_inc(v, (uint8_t)(reg - 0x10u));
    } else if (reg >= 0x20u && reg <= 0x28u) {      /* block / F-number high / key on-off */
        const uint8_t ch = (uint8_t)(reg - 0x20u);
        vrc7_update_inc(v, ch);
        vrc7_key_on(v, ch, (uint8_t)((data & 0x10u) != 0u));
    }
}

static void nes_exp_vrc7_render(nes_t *nes, uint8_t *buffer, uint16_t start, uint16_t count, uint32_t step_q8) {
    vrc7_t *v = &nes->nes_apu.exp_audio.vrc7;
    if (v->active == 0u) return;        /* nothing keyed on: one test per segment */
    const uint32_t eg_need = 256u * 4u * 44100u / 49716u;
    for (uint16_t i = 0u; i < count; i++) {
        const int16_t level = vrc7_sample(v);
        int32_t mixed = (int32_t)buffer[start + i] + (level >> 4);
        if (mixed > 255) mixed = 255;
        else if (mixed < 0) mixed = 0;
        buffer[start + i] = (uint8_t)mixed;
        v->eg_acc += 256u;
        if (v->eg_acc >= eg_need) {
            v->eg_acc -= eg_need;
            for (uint8_t ch = 0u; ch < VRC7_CHANNELS; ch++) {
                if ((v->key_status & (uint8_t)(1u << ch)) == 0u) continue;
                vrc7_eg_tick(&v->slot[ch * 2u]);
                vrc7_eg_tick(&v->slot[ch * 2u + 1u]);
            }
        }
    }
    (void)step_q8;
}
void nes_apu_expansion_write(nes_t* nes, uint16_t address, uint8_t data) {
    nes_apu_exp_t* a = &nes->nes_apu.exp_audio;

    switch (nes->nes_mapper.mapper_audio) {
    case NES_APU_EXP_VRC6:
        switch (address & 0xF003u) {
        case 0x9000u: case 0x9001u: case 0x9002u:
            vrc6_pulse_write(a, 0, address, data);
            break;
        case 0x9003u: {
            a->vrc6_halt = (uint8_t)((data & 0x01u) != 0u);
            a->vrc6_shift = (uint8_t)(((data & 0x04u) != 0u) ? 8u : (((data & 0x02u) != 0u) ? 4u : 0u));
            break;
        }
        case 0xA000u: case 0xA001u: case 0xA002u:
            vrc6_pulse_write(a, 1, address, data);
            break;
        case 0xB000u: case 0xB001u: case 0xB002u:
            vrc6_saw_write(a, address, data);
            break;
        default:
            break;
        }
        break;

    case NES_APU_EXP_N163:
        switch (address & 0xF800u) {
        case 0x4800u:
            a->n163_ram[a->n163_ram_position & 0x7Fu] = data;
            if (a->n163_auto_increment) {
                a->n163_ram_position = (uint8_t)((a->n163_ram_position + 1u) & 0x7Fu);
            }
            break;
        case 0xE000u:
            a->n163_disable = (uint8_t)((data & 0x40u) != 0u);
            break;
        case 0xF800u:
            a->n163_ram_position = (uint8_t)(data & 0x7Fu);
            a->n163_auto_increment = (uint8_t)((data & 0x80u) != 0u);
            break;
        default:
            break;
        }
        break;

    case NES_APU_EXP_S5B:
        switch (address & 0xE000u) {
        case 0xC000u:
            a->s5b_current_register = data;
            break;
        case 0xE000u:
            if (a->s5b_current_register <= 0x0Fu) {
                a->s5b_regs[a->s5b_current_register] = data;
            }
            break;
        default:
            break;
        }
        break;

    case NES_APU_EXP_VRC7:
        nes_exp_vrc7_write(nes, address, data);
        break;
    case NES_APU_EXP_MMC5:
        switch (address) {
        case 0x5000u: case 0x5001u: case 0x5002u: case 0x5003u:
            mmc5_square_write(a, 0, address, data);
            break;
        case 0x5004u: case 0x5005u: case 0x5006u: case 0x5007u:
            mmc5_square_write(a, 1, address, data);
            break;
        case 0x5010u:
            /* PCM read mode / PCM IRQ enable (IRQs are not implemented, as in Mesen). */
            a->mmc5_pcm_read_mode = (uint8_t)((data & 0x01u) != 0u);
            a->mmc5_pcm_irq_enabled = (uint8_t)((data & 0x80u) != 0u);
            break;
        case 0x5011u:
            /* A written 0 keeps the previous level, and read mode ignores writes. */
            if (!a->mmc5_pcm_read_mode && data != 0u) {
                a->mmc5_pcm_output = data;
            }
            break;
        case 0x5015u:
            a->mmc5_square[0].enabled = (uint8_t)((data & 0x01u) != 0u);
            a->mmc5_square[1].enabled = (uint8_t)((data & 0x02u) != 0u);
            if (!a->mmc5_square[0].enabled) a->mmc5_square[0].length_counter = 0;
            if (!a->mmc5_square[1].enabled) a->mmc5_square[1].length_counter = 0;
            break;
        default:
            break;
        }
        break;

    default:
        break;
    }
}

uint8_t nes_apu_expansion_read(nes_t* nes, uint16_t address) {
    nes_apu_exp_t* a = &nes->nes_apu.exp_audio;

    if (nes->nes_mapper.mapper_audio == NES_APU_EXP_N163 && (address & 0xF800u) == 0x4800u) {
        const uint8_t value = a->n163_ram[a->n163_ram_position & 0x7Fu];
        if (a->n163_auto_increment) {
            a->n163_ram_position = (uint8_t)((a->n163_ram_position + 1u) & 0x7Fu);
        }
        return value;
    }

    if (nes->nes_mapper.mapper_audio == NES_APU_EXP_MMC5) {
        switch (address) {
        case 0x5010u:
            return 0;                       /* PCM IRQ status (not implemented, as in Mesen) */
        case 0x5015u: {
            uint8_t status = 0;
            if (a->mmc5_square[0].length_counter > 0u) status |= 0x01u;
            if (a->mmc5_square[1].length_counter > 0u) status |= 0x02u;
            return status;
        }
        default:
            break;
        }
    }
    return 0;
}

void nes_apu_expansion_render(nes_t* nes, uint8_t* buffer, uint16_t start, uint16_t count, uint32_t cycles) {
    nes_apu_exp_t* a = &nes->nes_apu.exp_audio;
    if (count == 0u || nes->nes_mapper.mapper_audio == NES_APU_EXP_NONE) return;

    const uint32_t step_q8 = (uint32_t)(((uint64_t)cycles << 8) / count);

    switch (nes->nes_mapper.mapper_audio) {
    case NES_APU_EXP_N163: {
        /* The chip's level is a signed average; scale it into the 8-bit mix and clamp. */
        for (uint16_t i = 0; i < count; i++) {
            const int16_t level = n163_advance(a, step_q8);
            const int32_t mixed = (int32_t)buffer[start + i] + (int32_t)level / 3;
            buffer[start + i] = (uint8_t)(mixed < 0 ? 0 : (mixed > 255 ? 255 : mixed));
        }
        break;
    }

    case NES_APU_EXP_VRC6: {
        /* VRC6 is clocked per CPU cycle; accumulate the fractional per-sample count. */
        uint32_t acc_q8 = a->n163_acc;             /* reused as a generic sub-cycle accumulator */
        for (uint16_t i = 0; i < count; i++) {
            acc_q8 += step_q8;
            while (acc_q8 >= 256u) {
                acc_q8 -= 256u;
                vrc6_clock(a);
            }
            const int32_t mixed = (int32_t)buffer[start + i] + (int32_t)vrc6_level(a) / 2;
            buffer[start + i] = (uint8_t)(mixed < 0 ? 0 : (mixed > 255 ? 255 : mixed));
        }
        a->n163_acc = acc_q8;
        break;
    }

    case NES_APU_EXP_S5B: {
        /* The channels tick every second CPU cycle, so accumulate cycles and consume them
           two at a time. */
        uint32_t acc_q8 = a->s5b_acc;
        for (uint16_t i = 0; i < count; i++) {
            acc_q8 += step_q8;
            while (acc_q8 >= 512u) {           /* 2 CPU cycles */
                acc_q8 -= 512u;
                s5b_clock(a);
            }
            const int32_t mixed = (int32_t)buffer[start + i] + (int32_t)s5b_level(a) / 3;
            buffer[start + i] = (uint8_t)(mixed < 0 ? 0 : (mixed > 255 ? 255 : mixed));
        }
        a->s5b_acc = acc_q8;
        break;
    }

    case NES_APU_EXP_MMC5:
        mmc5_render(nes, buffer, start, count, step_q8);
        break;

    case NES_APU_EXP_VRC7:
        nes_exp_vrc7_render(nes, buffer, start, count, step_q8);
        break;

    default:
        break;
    }
}

#endif
#endif



