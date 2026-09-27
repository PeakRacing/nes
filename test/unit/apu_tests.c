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

    /* Power-on default is 4-step mode with the IRQ enabled. */
    TEST_EQ_U32(0, nes->nes_apu.mode);
    TEST_EQ_U32(0, nes->nes_apu.irq_inhibit_flag);
    nes->nes_cpu.irq_pending = 0;
    apu_run_frame_counter_cycles(nes, 1);
    TEST_EQ_U32(1, nes->nes_apu.frame_interrupt);
    TEST_EQ_U32(1, nes->nes_cpu.irq_pending);

    /* Reading $4015 acknowledges the frame interrupt. */
    TEST_CHECK((nes_read_apu_register(nes, 0x4015) & 0x40) != 0);
    TEST_EQ_U32(0, nes->nes_apu.frame_interrupt);
    TEST_EQ_U32(0, nes->nes_cpu.irq_pending);

    /* $4017 with the inhibit bit set de-asserts the APU IRQ line: this is the
     * Nintendo World Championships 1990 boot fix (BRK loop in work RAM). */
    apu_run_frame_counter_cycles(nes, 1);
    TEST_EQ_U32(1, nes->nes_cpu.irq_pending);
    nes_write_apu_register(nes, 0x4017, 0x40);
    TEST_EQ_U32(0, nes->nes_apu.frame_interrupt);
    TEST_EQ_U32(0, nes->nes_cpu.irq_pending);

    /* In 5-step mode the frame counter never raises the IRQ. */
    nes_write_apu_register(nes, 0x4017, 0x80);
    TEST_EQ_U32(1, nes->nes_apu.mode);
    nes->nes_cpu.irq_pending = 0;
    apu_run_frame_counter_cycles(nes, 4);
    TEST_EQ_U32(0, nes->nes_apu.frame_interrupt);
    TEST_EQ_U32(0, nes->nes_cpu.irq_pending);

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
