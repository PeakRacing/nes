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
 * APU tests: length counters / $4015 semantics, frame counter IRQ behaviour and
 * sample output sanity. The APU uses fixed point synthesis at 44100 Hz and every
 * frame flushes NES_APU_SAMPLE_PER_SYNC bytes through nes_sound_output().
 */
#include "test.h"

static int apu_fixture(test_fixture_t* f) {
    test_rom_spec_t spec;
    memset(&spec, 0, sizeof(spec));
    spec.mapper = 0;
    spec.prg_units = 2;
    spec.chr_units = 1;
    spec.save = 1;
    spec.fill = TEST_ROM_FILL_STUB;
    return test_fixture_make(f, &spec);
}

/* Four nes_apu_frame() calls complete one 4-step frame counter sequence. */
static void apu_run_frame_counter_cycles(nes_t* nes, unsigned cycles) {
    for (unsigned i = 0; i < cycles * 4u; ++i) nes_apu_frame(nes);
}

int test_apu_length_counters(void) {
    test_fixture_t f;
    TEST_CHECK(apu_fixture(&f));
    nes_t* nes = f.nes;

    /* A length counter only loads while its channel is enabled. */
    nes_write_apu_register(nes, 0x4003, 0x08);
    TEST_EQ_U32(0, nes->nes_apu.pulse1.length_counter);
    TEST_EQ_U32(0, nes_read_apu_register(nes, 0x4015) & 0x0F);

    nes_write_apu_register(nes, 0x4015, 0x01);      /* enable pulse 1 */
    nes_write_apu_register(nes, 0x4003, 0x08);      /* length index 1 */
    TEST_CHECK(nes->nes_apu.pulse1.length_counter > 0);
    TEST_EQ_U32(0x01, nes_read_apu_register(nes, 0x4015) & 0x0F);

    /* Enabling the remaining channels and loading them sets their bits too. */
    nes_write_apu_register(nes, 0x4015, 0x0F);
    nes_write_apu_register(nes, 0x4007, 0x08);
    nes_write_apu_register(nes, 0x400B, 0x08);
    nes_write_apu_register(nes, 0x400F, 0x08);
    TEST_EQ_U32(0x0F, nes_read_apu_register(nes, 0x4015) & 0x0F);

    /* Disabling a channel clears its length counter immediately. */
    nes_write_apu_register(nes, 0x4015, 0x00);
    TEST_EQ_U32(0x00, nes_read_apu_register(nes, 0x4015) & 0x0F);
    TEST_EQ_U32(0, nes->nes_apu.pulse1.length_counter);
    TEST_EQ_U32(0, nes->nes_apu.noise.length_counter);

    /* The length counter decrements when the channel does not halt it. */
    nes_write_apu_register(nes, 0x4015, 0x01);
    nes_write_apu_register(nes, 0x4000, 0x00);      /* halt clear, duty 0 */
    nes_write_apu_register(nes, 0x4003, 0x08);
    const uint8_t before = nes->nes_apu.pulse1.length_counter;
    apu_run_frame_counter_cycles(nes, 1);
    TEST_CHECK(nes->nes_apu.pulse1.length_counter < before);

    test_fixture_free(&f);
    return TEST_PASS;
}

int test_apu_frame_irq(void) {
    test_fixture_t f;
    TEST_CHECK(apu_fixture(&f));
    nes_t* nes = f.nes;

    /* Power-on default is 4-step mode with the IRQ enabled.  The frame IRQ lives on the
       APU's own line (nes_apu_irq_pending), not in nes_cpu.irq_pending: that flag also
       carries the mapper IRQ line, which a $4015 read must not acknowledge. */
    TEST_EQ_U32(0, nes->nes_apu.mode);
    TEST_EQ_U32(0, nes->nes_apu.irq_inhibit_flag);
    nes->nes_cpu.irq_pending = 0;
    apu_run_frame_counter_cycles(nes, 1);
    TEST_EQ_U32(1, nes->nes_apu.frame_interrupt);
    TEST_EQ_U32(1, nes_apu_irq_pending(nes));

    /* Reading $4015 acknowledges the frame interrupt... */
    TEST_CHECK((nes_read_apu_register(nes, 0x4015) & 0x40) != 0);
    TEST_EQ_U32(0, nes->nes_apu.frame_interrupt);
    TEST_EQ_U32(0, nes_apu_irq_pending(nes));

    /* ...but must leave a pending mapper IRQ alone (the bug this split fixes). */
    nes_cpu_irq(nes);
    (void)nes_read_apu_register(nes, 0x4015);
    TEST_EQ_U32(1, nes->nes_cpu.irq_pending);
    nes->nes_cpu.irq_pending = 0;

    /* $4017 with the inhibit bit set de-asserts the APU IRQ line: this is the
     * Nintendo World Championships 1990 boot fix (BRK loop in work RAM). */
    apu_run_frame_counter_cycles(nes, 1);
    TEST_EQ_U32(1, nes_apu_irq_pending(nes));
    nes_write_apu_register(nes, 0x4017, 0x40);
    TEST_EQ_U32(0, nes->nes_apu.frame_interrupt);
    TEST_EQ_U32(0, nes_apu_irq_pending(nes));

    /* In 5-step mode the frame counter never raises the IRQ. */
    nes_write_apu_register(nes, 0x4017, 0x80);
    TEST_EQ_U32(1, nes->nes_apu.mode);
    nes->nes_cpu.irq_pending = 0;
    apu_run_frame_counter_cycles(nes, 4);
    TEST_EQ_U32(0, nes->nes_apu.frame_interrupt);
    TEST_EQ_U32(0, nes_apu_irq_pending(nes));

    test_fixture_free(&f);
    return TEST_PASS;
}

int test_apu_dmc(void) {
    test_fixture_t f;
    TEST_CHECK(apu_fixture(&f));
    nes_t* nes = f.nes;

    /* Sample data is fetched through the CPU bus: put 17 bytes at $C000 (a 32KB NROM image
       maps the third 8KB bank there) so the delta decoder has something to chew on. */
    uint8_t* prg = nes->nes_rom.prg_rom;
    TEST_CHECK(prg != NULL);
    for (int i = 0; i < 17; i++) {
        prg[0x4000 + i] = (uint8_t)((i & 1) ? 0xAA : 0x55);
    }

    /* $4012 = 0 -> address $C000, $4013 = 1 -> 17 bytes, rate 15 (fastest). */
    nes_write_apu_register(nes, 0x4012, 0x00);
    nes_write_apu_register(nes, 0x4013, 0x01);
    nes_write_apu_register(nes, 0x4010, 0x0F);

    /* Disabled channel: nothing playing and $4015 bit 4 stays clear. */
    TEST_EQ_U32(0, nes->nes_apu.dmc.bytes_remaining);
    TEST_EQ_U32(0, (unsigned)(nes_read_apu_register(nes, 0x4015) & 0x10));

    /* Enabling DMC restarts the sample: 17 bytes pending, status bit set. */
    nes_write_apu_register(nes, 0x4015, 0x10);
    TEST_CHECK(nes->nes_apu.dmc.bytes_remaining > 0);
    TEST_CHECK((nes_read_apu_register(nes, 0x4015) & 0x10) != 0);

    /* Let it run: the DAC must move away from its power-on level and the sample must end
       (17 bytes x 8 bits x 54 cycles is well under one frame). */
    for (unsigned i = 0; i < 8; ++i) nes_apu_frame(nes);
    TEST_EQ_U32(0, nes->nes_apu.dmc.bytes_remaining);
    TEST_EQ_U32(0, (unsigned)(nes_read_apu_register(nes, 0x4015) & 0x10));
    TEST_CHECK(nes->nes_apu.dmc.out_level != 0);

    /* With the IRQ enabled the end of the sample raises the APU line, and reading $4015
       (or writing $4010 with bit 7) clears it again. */
    nes_write_apu_register(nes, 0x4015, 0x10);
    nes_write_apu_register(nes, 0x4010, 0x8F);
    for (unsigned i = 0; i < 8; ++i) nes_apu_frame(nes);
    TEST_EQ_U32(1, nes->nes_apu.dmc.irq_flag);
    TEST_EQ_U32(1, nes_apu_irq_pending(nes));
    TEST_CHECK((nes_read_apu_register(nes, 0x4015) & 0x80) != 0);
    TEST_EQ_U32(0, nes->nes_apu.dmc.irq_flag);
    TEST_EQ_U32(0, nes_apu_irq_pending(nes));

    /* Writing $4011 moves the DAC immediately. */
    nes_write_apu_register(nes, 0x4011, 0x7F);
    TEST_EQ_U32(0x7F, nes->nes_apu.dmc.out_level);
    nes_write_apu_register(nes, 0x4011, 0x00);
    TEST_EQ_U32(0x00, nes->nes_apu.dmc.out_level);

    test_fixture_free(&f);
    return TEST_PASS;
}

#if (NES_ENABLE_EXPANSION_AUDIO == 1)
/* Cartridge expansion audio.  Authorities: Mesen2 Core/NES/Mappers/Audio/Namco163Audio.h,
 * Vrc6Audio.h / Vrc6Pulse.h / Vrc6Saw.h. */
int test_apu_expansion_audio(void) {
    test_rom_spec_t spec;
    uint8_t* buf;
    nes_exp_audio_t* a;

    /* --- Namco 163 through a real mapper 19 board (registers via the CPU bus) --- */
    memset(&spec, 0, sizeof(spec));
    spec.mapper = 19;
    spec.prg_units = 16;
    spec.chr_units = 8;
    spec.fill = TEST_ROM_FILL_RANDOM;
    test_fixture_t f;
    TEST_CHECK(test_fixture_make(&f, &spec));
    nes_t* nes = f.nes;
    a = &nes->nes_apu.exp_audio;
    buf = nes->nes_apu.sample_buffer;
    TEST_EQ_U32(NES_EXP_AUDIO_N163, nes->nes_mapper.mapper_audio);

    /* $F800 sets the position + auto-increment, $4800 is the wave RAM port. */
    nes_test_cpu_write(nes, 0x4800u, 0x11u);            /* no $F800 yet -> position 0 */
    TEST_EQ_U32(0x11, a->n163_ram[0]);
    nes_test_cpu_write(nes, 0xF800u, 0x80u | 0x20u);    /* position 0x20, auto-increment on */
    nes_test_cpu_write(nes, 0x4800u, 0x22u);
    nes_test_cpu_write(nes, 0x4800u, 0x33u);
    TEST_EQ_U32(0x22, a->n163_ram[0x20]);
    TEST_EQ_U32(0x33, a->n163_ram[0x21]);
    /* The read port returns the byte at the current position (auto-increment had walked it
       to 0x22 after the two writes above, so pin it back to 0x20 first). */
    nes_test_cpu_write(nes, 0xF800u, 0x20u);            /* position 0x20, auto-increment off */
    TEST_EQ_U32(0x22, nes_test_cpu_read(nes, 0x4800u));
    nes_test_cpu_write(nes, 0x4800u, 0x44u);
    nes_test_cpu_write(nes, 0x4800u, 0x55u);
    /* Without auto-increment both writes land on position 0x20 (the second one wins) and
       0x21 keeps the byte the incrementing writes put there. */
    TEST_EQ_U32(0x55, a->n163_ram[0x20]);
    TEST_EQ_U32(0x33, a->n163_ram[0x21]);

    /* One audible channel (ram[0x7F] bits 4-6 = 0 -> channel 7 only). */
    a->n163_ram[0x7F] = 0x00;
    a->n163_ram[0x40 + 7 * 8 + 0] = 0x00;               /* frequency low */
    a->n163_ram[0x40 + 7 * 8 + 2] = 0x00;               /* frequency mid */
    a->n163_ram[0x40 + 7 * 8 + 4] = 0x00;               /* frequency high / wave length */
    a->n163_ram[0x40 + 7 * 8 + 6] = 0x00;               /* wave address */
    a->n163_ram[0x40 + 7 * 8 + 7] = 0x0F;               /* volume 15 */
    a->n163_ram[0x00] = 0x0F;                           /* wave nibbles (low nibble first) */
    a->n163_last_output = 0;
    nes_exp_audio_render(nes, buf, 0, 64, 64u * 15u);
    /* Advance lands the phase on sample 15 -> (15 - 8) * 15 = 105. */
    TEST_EQ_U32(105, (uint32_t)a->n163_channel_out[7]);
    /* The chip level is that single channel divided by (count + 1) = 105. */
    TEST_EQ_U32(105, (uint32_t)a->n163_last_output);

    /* $E000 bit 6 disables the sound channel. */
    nes_test_cpu_write(nes, 0xE000u, 0x40u);
    TEST_CHECK(a->n163_disable != 0);
    nes_test_cpu_write(nes, 0xE000u, 0x00u);
    TEST_EQ_U32(0, a->n163_disable);
    test_fixture_free(&f);

    /* --- VRC6 through a real mapper 24 board --- */
    memset(&spec, 0, sizeof(spec));
    spec.mapper = 24;
    spec.prg_units = 16;
    spec.chr_units = 8;
    spec.fill = TEST_ROM_FILL_RANDOM;
    TEST_CHECK(test_fixture_make(&f, &spec));
    nes = f.nes;
    a = &nes->nes_apu.exp_audio;
    buf = nes->nes_apu.sample_buffer;
    TEST_EQ_U32(NES_EXP_AUDIO_VRC6, nes->nes_mapper.mapper_audio);

    nes_test_cpu_write(nes, 0x9000u, 0x8Fu);            /* pulse1: volume 15, ignore duty */
    nes_test_cpu_write(nes, 0x9001u, 0x10u);            /* frequency low */
    nes_test_cpu_write(nes, 0x9002u, 0x80u);            /* frequency high 0 + enable */
    TEST_EQ_U32(15, a->vrc6_pulse[0].volume);
    TEST_EQ_U32(1, a->vrc6_pulse[0].ignore_duty);
    TEST_EQ_U32(1, a->vrc6_pulse[0].enabled);
    TEST_EQ_U32(0x010, a->vrc6_pulse[0].frequency);

    /* One frame's worth of clocks: the timer walks but ignore-duty holds the level at 15. */
    nes_exp_audio_render(nes, buf, 0, 128, 128u * 40u);
    TEST_EQ_U32(15, (uint32_t)a->vrc6_pulse[0].volume);
    TEST_CHECK(buf[0] >= 7);                            /* level / 2 got mixed in */

    /* $9003 bit 0 halts all three channels. */
    nes_test_cpu_write(nes, 0x9003u, 0x01u);
    TEST_EQ_U32(1, a->vrc6_halt);
    const int32_t frozen = a->vrc6_pulse[0].timer;
    nes_exp_audio_render(nes, buf, 0, 32, 32u * 40u);
    TEST_EQ_U32((uint32_t)frozen, (uint32_t)a->vrc6_pulse[0].timer);
    /* $9003 bits 1/2 select the frequency shift (4 / 8). */
    nes_test_cpu_write(nes, 0x9003u, 0x04u);
    TEST_EQ_U32(8, a->vrc6_shift);
    nes_test_cpu_write(nes, 0x9003u, 0x02u);
    TEST_EQ_U32(4, a->vrc6_shift);
    nes_test_cpu_write(nes, 0x9003u, 0x00u);
    TEST_EQ_U32(0, a->vrc6_shift);

    /* Sawtooth: the accumulator grows on every second step and resets on step 0. */
    nes_test_cpu_write(nes, 0xB000u, 0x20u);            /* accumulator rate */
    nes_test_cpu_write(nes, 0xB001u, 0x01u);            /* frequency low */
    nes_test_cpu_write(nes, 0xB002u, 0x80u);            /* enable */
    TEST_EQ_U32(1, a->vrc6_saw.enabled);
    TEST_EQ_U32(0x20, a->vrc6_saw.acc_rate);
    nes_exp_audio_render(nes, buf, 0, 64, 64u * 8u);
    TEST_CHECK(a->vrc6_saw.accumulator > 0);
    /* Clearing the enable bit clears the accumulator. */
    nes_test_cpu_write(nes, 0xB002u, 0x00u);
    TEST_EQ_U32(0, a->vrc6_saw.accumulator);

    /* A board without a chip must leave the buffer untouched. */
    nes->nes_mapper.mapper_audio = NES_EXP_AUDIO_NONE;
    buf[0] = 0x40;
    nes_exp_audio_render(nes, buf, 0, 8, 64u);
    TEST_EQ_U32(0x40, buf[0]);

    test_fixture_free(&f);

    /* --- Sunsoft 5B through a real mapper 69 board --- */
    memset(&spec, 0, sizeof(spec));
    spec.mapper = 69;
    spec.prg_units = 16;
    spec.chr_units = 8;
    spec.fill = TEST_ROM_FILL_RANDOM;
    TEST_CHECK(test_fixture_make(&f, &spec));
    nes = f.nes;
    a = &nes->nes_apu.exp_audio;
    buf = nes->nes_apu.sample_buffer;
    TEST_EQ_U32(NES_EXP_AUDIO_S5B, nes->nes_mapper.mapper_audio);

    /* The volume table grows by 1.5 dB per step (Mesen's double ramp, truncated). */
    TEST_EQ_U32(0, a->s5b_volume_lut[0]);
    TEST_EQ_U32(1, a->s5b_volume_lut[1]);
    TEST_CHECK(a->s5b_volume_lut[15] > a->s5b_volume_lut[8]);
    TEST_CHECK(a->s5b_volume_lut[8] > a->s5b_volume_lut[4]);

    /* $C000 selects the register, $E000 writes it; registers above 0x0F are ignored. */
    nes_test_cpu_write(nes, 0xC000u, 0x00u);            /* channel A period low */
    nes_test_cpu_write(nes, 0xE000u, 0x10u);
    nes_test_cpu_write(nes, 0xC000u, 0x08u);            /* channel A volume */
    nes_test_cpu_write(nes, 0xE000u, 0x0Fu);
    TEST_EQ_U32(0x10, a->s5b_regs[0x00]);
    TEST_EQ_U32(0x0F, a->s5b_regs[0x08]);
    nes_test_cpu_write(nes, 0xC000u, 0x20u);            /* out of range select */
    nes_test_cpu_write(nes, 0xE000u, 0xFFu);
    TEST_EQ_U32(0, a->s5b_regs[0x01]);                  /* untouched */

    /* Run the chip: the channel outputs its volume (buffer moves up by volume / 3). */
    for (uint16_t i = 0; i < 256u; i++) buf[i] = 0x00;
    a->s5b_timer[0] = 0;                                /* force a step on the first tick */
    a->s5b_step[0] = 0;
    nes_exp_audio_render(nes, buf, 0, 256, 256u * 40u);
    TEST_CHECK(buf[255] == (uint8_t)(a->s5b_volume_lut[0x0F] / 3));

    /* Disabling the tone bit (regs[7] bit 0) silences the channel. */
    nes_test_cpu_write(nes, 0xC000u, 0x07u);
    nes_test_cpu_write(nes, 0xE000u, 0x01u);
    for (uint16_t i = 0; i < 256u; i++) buf[i] = 0x00;
    nes_exp_audio_render(nes, buf, 0, 256, 256u * 40u);
    TEST_EQ_U32(0x00, buf[255]);

    test_fixture_free(&f);

    /* --- MMC5 audio through a real mapper 5 board --- */
    memset(&spec, 0, sizeof(spec));
    spec.mapper = 5;
    spec.prg_units = 16;
    spec.chr_units = 8;
    spec.fill = TEST_ROM_FILL_RANDOM;
    TEST_CHECK(test_fixture_make(&f, &spec));
    nes = f.nes;
    a = &nes->nes_apu.exp_audio;
    buf = nes->nes_apu.sample_buffer;
    TEST_EQ_U32(NES_EXP_AUDIO_MMC5, nes->nes_mapper.mapper_audio);

    /* Square 1: constant volume 15, duty 2, period 0x40, length counter index 1. */
    nes_test_cpu_write(nes, 0x5000u, 0x9Fu);            /* duty 2, halt, constant, volume 15 */
    nes_test_cpu_write(nes, 0x5002u, 0x40u);            /* timer low */
    TEST_EQ_U32(2, a->mmc5_square[0].duty);
    TEST_EQ_U32(15, a->mmc5_square[0].volume);
    TEST_EQ_U32(0x40, a->mmc5_square[0].period);
    /* $5001 (sweep on the APU) has no effect on this board. */
    nes_test_cpu_write(nes, 0x5001u, 0x8Fu);
    TEST_EQ_U32(0x40, a->mmc5_square[0].period);

    /* $5015 enables the channel; the length counter only loads while it is enabled. */
    nes_test_cpu_write(nes, 0x5015u, 0x01u);
    TEST_EQ_U32(1, a->mmc5_square[0].enabled);
    TEST_EQ_U32(0, a->mmc5_square[0].length_counter);
    nes_test_cpu_write(nes, 0x5003u, 0x08u);            /* timer high 0 + length index 1 */
    TEST_CHECK(a->mmc5_square[0].length_counter > 0);
    TEST_EQ_U32(0x01, nes_test_cpu_read(nes, 0x5015u));   /* status read */

    /* Run a segment over a mid-level buffer: the duty-high steps must pull samples down
       (MMC5 polarity is reversed compared to the APU). */
    for (uint16_t i = 0; i < 256u; i++) buf[i] = 0x80;
    a->mmc5_square[0].duty_pos = 1;
    nes_exp_audio_render(nes, buf, 0, 256, 256u * 40u);
    uint8_t lowest = 0xFFu;
    for (uint16_t i = 0; i < 256u; i++) {
        if (buf[i] < lowest) lowest = buf[i];
    }
    TEST_CHECK(lowest < 0x80);
    /* With the channel disabled the level must stay flat. */
    nes_test_cpu_write(nes, 0x5015u, 0x00u);
    nes_test_cpu_write(nes, 0x5015u, 0x01u);
    nes_test_cpu_write(nes, 0x5003u, 0x08u);
    nes_test_cpu_write(nes, 0x5015u, 0x00u);            /* ...and disabled again */
    for (uint16_t i = 0; i < 64u; i++) buf[i] = 0x80;
    nes_exp_audio_render(nes, buf, 0, 64, 64u * 40u);
    TEST_EQ_U32(0x80, buf[0]);

    /* PCM: $5011 sets the DAC, a written 0 keeps the previous level, and read mode
       ignores writes entirely. */
    nes_test_cpu_write(nes, 0x5011u, 0xC0u);
    TEST_EQ_U32(0xC0, a->mmc5_pcm_output);
    nes_test_cpu_write(nes, 0x5011u, 0x00u);
    TEST_EQ_U32(0xC0, a->mmc5_pcm_output);
    nes_test_cpu_write(nes, 0x5010u, 0x01u);            /* read mode on */
    nes_test_cpu_write(nes, 0x5011u, 0x40u);
    TEST_EQ_U32(0xC0, a->mmc5_pcm_output);
    nes_test_cpu_write(nes, 0x5010u, 0x00u);

    /* Disabling $5015 bit 0 clears the length counter and the status read. */
    nes_test_cpu_write(nes, 0x5015u, 0x00u);
    TEST_EQ_U32(0, a->mmc5_square[0].length_counter);
    TEST_EQ_U32(0x00, nes_test_cpu_read(nes, 0x5015u));

    test_fixture_free(&f);
    return TEST_PASS;
}
#endif

int test_apu_samples(void) {
    test_fixture_t f;
    TEST_CHECK(apu_fixture(&f));
    nes_t* nes = f.nes;

    /* Program pulse 1: 50% duty, constant volume 15, audible period. */
    nes_write_apu_register(nes, 0x4015, 0x01);
    nes_write_apu_register(nes, 0x4000, 0xBF);
    nes_write_apu_register(nes, 0x4002, 0x40);
    nes_write_apu_register(nes, 0x4003, 0x08);
    for (unsigned i = 0; i < 8; ++i) nes_apu_frame(nes);

    nes_test_audio_stats_t stats = nes_test_audio_stats(nes);
    TEST_CHECK(stats.samples > 0);
    TEST_CHECK(stats.max_sample <= 255);
    TEST_CHECK(stats.sum_sample_sq > 0);

    /* A triangle voice also reaches the mixer. */
    nes_write_apu_register(nes, 0x4015, 0x05);
    nes_write_apu_register(nes, 0x4008, 0xFF);
    nes_write_apu_register(nes, 0x400A, 0x40);
    nes_write_apu_register(nes, 0x400B, 0x08);
    for (unsigned i = 0; i < 8; ++i) nes_apu_frame(nes);
    TEST_CHECK(nes_test_audio_stats(nes).sum_sample_sq > 0);

    /* With every channel disabled the mixer settles back to silence. */
    nes_write_apu_register(nes, 0x4015, 0x00);
    for (unsigned i = 0; i < 4; ++i) nes_apu_frame(nes);
    stats = nes_test_audio_stats(nes);
    TEST_EQ_U32(0, (unsigned)stats.sum_sample_sq);
    TEST_EQ_U32(0, stats.max_sample);

    test_fixture_free(&f);
    return TEST_PASS;
}

#if (NES_ENABLE_EXPANSION_AUDIO == 1)
/* Mapper 85 carries a VRC7 (YM2413/OPLL).
   Authority: Mesen2 Core/NES/Mappers/Konami/VRC7.h + Mappers/Audio/Vrc7Audio.h */
int test_apu_vrc7_audio(void) {
    test_rom_spec_t spec;
    memset(&spec, 0, sizeof(spec));
    spec.mapper = 85;
    spec.prg_units = 8;
    spec.chr_units = 8;
    spec.fill = TEST_ROM_FILL_RANDOM;
    test_fixture_t f;
    TEST_CHECK(test_fixture_make(&f, &spec));
    nes_t* nes = f.nes;
    nes_exp_audio_t* a = &nes->nes_apu.exp_audio;
    uint8_t* buf = nes->nes_apu.sample_buffer;

    TEST_EQ_U32(NES_EXP_AUDIO_VRC7, nes->nes_mapper.mapper_audio);
    /* The OPLL core is created lazily, so a game that never touches the chip allocates nothing. */
    TEST_CHECK(a->vrc7.active == 0u);

    /* Instrument 1 at full volume, then key on channel 0 (block 1, F-num high 5, F-num low 0x80). */
    nes_test_cpu_write(nes, 0x9010u, 0x30u);
    nes_test_cpu_write(nes, 0x9030u, 0x10u);
    nes_test_cpu_write(nes, 0x9010u, 0x20u);
    nes_test_cpu_write(nes, 0x9030u, 0x15u);
    nes_test_cpu_write(nes, 0x9010u, 0x10u);
    nes_test_cpu_write(nes, 0x9030u, 0x80u);
    TEST_CHECK(a->vrc7.key_status != 0u);
    TEST_EQ_U32(0x10u, a->vrc7.current_reg);

    /* Render a segment: the FM level has to move the buffer off its DC baseline.  A silent or
       mis-clocked chip would leave every sample at 128 - that is the assertion that matters. */
    for (uint16_t i = 0; i < 512u; i++) buf[i] = 128u;
    nes_exp_audio_render(nes, buf, 0, 512u, 512u * 40u);
    uint32_t moved = 0;
    for (uint16_t i = 0; i < 512u; i++) {
        if (buf[i] != 128u) moved++;
    }
    TEST_CHECK(moved > 64u);

    /* $E000 bit6 mutes the chip. */
    nes_test_cpu_write(nes, 0xE000u, 0x40u);
    TEST_EQ_U32(1u, a->vrc7.muted);

    test_fixture_free(&f);
    return TEST_PASS;
}
#endif
