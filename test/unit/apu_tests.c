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
