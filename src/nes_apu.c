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

#endif



