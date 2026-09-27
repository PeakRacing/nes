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
 * 6502 core tests.
 *
 * Fixture ROMs use TEST_ROM_FILL_STUB, whose vector table points at $8000 in
 * every 16KB bank, so NMI/IRQ/BRK all land on $8000 regardless of banking.
 */
#include "test.h"
#include "cpu_opcode_table.h"

#define P_C (0x01u)
#define P_Z (0x02u)
#define P_I (0x04u)
#define P_D (0x08u)
#define P_B (0x10u)
#define P_U (0x20u)
#define P_V (0x40u)
#define P_N (0x80u)

#define P_C_CLR (0xFEu)
#define P_Z_CLR (0xFDu)
#define P_I_CLR (0xFBu)
#define P_V_CLR (0xBFu)
#define P_N_CLR (0x7Fu)

#define NMI_VECTOR_PC (0x8000u)

static int cpu_fixture(test_fixture_t* f) {
    test_rom_spec_t spec;
    memset(&spec, 0, sizeof(spec));
    spec.mapper = 0;
    spec.prg_units = 2;
    spec.chr_units = 1;
    spec.save = 1;
    spec.fill = TEST_ROM_FILL_STUB;
    return test_fixture_make(f, &spec);
}

static void cpu_reset_ram(nes_t* nes) {
    nes_memset(nes->nes_cpu.cpu_ram, 0, NES_CPU_RAM_SIZE);
}

static void cpu_put(nes_t* nes, uint16_t address, const uint8_t* bytes, size_t count) {
    for (size_t i = 0; i < count; ++i) nes->nes_cpu.cpu_ram[address + i] = bytes[i];
}

/* Force every conditional branch into its "not taken" case. */
static void cpu_force_branch_not_taken(nes_t* nes, uint8_t opcode) {
    switch (opcode) {
        case 0x10: nes->nes_cpu.P |= P_N; break;        /* BPL: N=1 */
        case 0x30: nes->nes_cpu.P &= P_N_CLR; break; /* BMI: N=0 */
        case 0x50: nes->nes_cpu.P |= P_V; break;        /* BVC: V=1 */
        case 0x70: nes->nes_cpu.P &= P_V_CLR; break; /* BVS: V=0 */
        case 0x90: nes->nes_cpu.P |= P_C; break;        /* BCC: C=1 */
        case 0xB0: nes->nes_cpu.P &= P_C_CLR; break; /* BCS: C=0 */
        case 0xD0: nes->nes_cpu.P |= P_Z; break;        /* BNE: Z=1 */
        case 0xF0: nes->nes_cpu.P &= P_Z_CLR; break; /* BEQ: Z=0 */
        default: break;
    }
}

static int cpu_step(nes_t* nes, uint16_t* cycles) {
    uint16_t value = 0;
    int rc = nes_test_cpu_step(nes, &value);
    if (cycles) *cycles = value;
    return rc;
}

/* ---------------------------------------------------------------- */

int test_cpu_opcode_coverage(void) {
    size_t count = 0;
    const cpu_opcode_info_t* table = cpu_opcode_table(&count);
    TEST_EQ_U32(151, count);

    int seen[256];
    memset(seen, 0, sizeof(seen));
    unsigned modes = 0;
    for (size_t i = 0; i < count; ++i) {
        TEST_CHECK(!seen[table[i].opcode]);
        seen[table[i].opcode] = 1;
        modes |= 1u << table[i].mode;
        TEST_CHECK(table[i].name != NULL && table[i].name[0] != 0);
        TEST_CHECK(table[i].cycles >= 2 && table[i].cycles <= 7);
    }
    for (int i = 0; i < MODE_COUNT; ++i) TEST_CHECK((modes & (1u << i)) != 0);

    test_fixture_t f;
    TEST_CHECK(cpu_fixture(&f));
    for (size_t i = 0; i < count; ++i) {
        cpu_reset_ram(f.nes);
        nes_test_cpu_prepare(f.nes, 0x200);
        f.nes->nes_cpu.cpu_ram[0x200] = table[i].opcode;
        uint16_t cycles = 0;
        TEST_EQ_I32(NES_OK, cpu_step(f.nes, &cycles));
        TEST_CHECK(cycles > 0 && cycles <= 12);
    }
    test_fixture_free(&f);
    return TEST_PASS;
}

int test_cpu_opcode_cycles(void) {
    size_t count = 0;
    const cpu_opcode_info_t* table = cpu_opcode_table(&count);
    test_fixture_t f;
    TEST_CHECK(cpu_fixture(&f));

    for (size_t i = 0; i < count; ++i) {
        cpu_reset_ram(f.nes);
        nes_test_cpu_prepare(f.nes, 0x200);
        f.nes->nes_cpu.cpu_ram[0x200] = table[i].opcode;
        if (table[i].mode == MODE_REL) cpu_force_branch_not_taken(f.nes, table[i].opcode);
        uint16_t cycles = 0;
        TEST_EQ_I32(NES_OK, cpu_step(f.nes, &cycles));
        if (cycles != table[i].cycles) {
            char expected[32], actual[32];
            snprintf(expected, sizeof(expected), "%u", table[i].cycles);
            snprintf(actual, sizeof(actual), "%u", cycles);
            char expression[48];
            snprintf(expression, sizeof(expression), "%s ($%02X) cycles", table[i].name, table[i].opcode);
            test_record_failure(__FILE__, __LINE__, expression, expected, actual);
            test_fixture_free(&f);
            return TEST_FAIL;
        }
    }

    /* Page-crossing penalties: LDA $00FF,X with X=1 crosses into $0100. */
    cpu_reset_ram(f.nes);
    nes_test_cpu_prepare(f.nes, 0x200);
    f.nes->nes_cpu.cpu_ram[0x200] = 0xBD;   /* LDA ABX */
    f.nes->nes_cpu.cpu_ram[0x201] = 0xFF;
    f.nes->nes_cpu.cpu_ram[0x202] = 0x00;
    f.nes->nes_cpu.X = 1;
    { uint16_t cycles = 0; cpu_step(f.nes, &cycles); TEST_EQ_U32(5, cycles); }

    cpu_reset_ram(f.nes);
    nes_test_cpu_prepare(f.nes, 0x200);
    f.nes->nes_cpu.cpu_ram[0x200] = 0xBD;
    f.nes->nes_cpu.cpu_ram[0x201] = 0x00;
    f.nes->nes_cpu.cpu_ram[0x202] = 0x02;
    f.nes->nes_cpu.X = 1;
    { uint16_t cycles = 0; cpu_step(f.nes, &cycles); TEST_EQ_U32(4, cycles); }

    /* RMW absolute (INC $0000) never takes the read penalty: 6 cycles. */
    cpu_reset_ram(f.nes);
    nes_test_cpu_prepare(f.nes, 0x200);
    f.nes->nes_cpu.cpu_ram[0x200] = 0xEE;
    { uint16_t cycles = 0; cpu_step(f.nes, &cycles); TEST_EQ_U32(6, cycles); }

    /* (indirect),Y crossing a page: pointer $00FF -> $0100 + Y=1. */
    cpu_reset_ram(f.nes);
    nes_test_cpu_prepare(f.nes, 0x200);
    f.nes->nes_cpu.cpu_ram[0x200] = 0xB1;   /* LDA IZY */
    f.nes->nes_cpu.cpu_ram[0x201] = 0xFF;
    f.nes->nes_cpu.cpu_ram[0x00FF] = 0xFF;
    f.nes->nes_cpu.cpu_ram[0x0000] = 0x00;
    f.nes->nes_cpu.Y = 1;
    { uint16_t cycles = 0; cpu_step(f.nes, &cycles); TEST_EQ_U32(6, cycles); }

    /* Branch taken but staying inside the page: 3 cycles. */
    cpu_reset_ram(f.nes);
    nes_test_cpu_prepare(f.nes, 0x200);
    f.nes->nes_cpu.cpu_ram[0x200] = 0xD0;   /* BNE, Z=0 -> taken */
    f.nes->nes_cpu.cpu_ram[0x201] = 0x00;
    { uint16_t cycles = 0; cpu_step(f.nes, &cycles); TEST_EQ_U32(3, cycles); }

    /* Branch taken across a page boundary: 4 cycles. */
    cpu_reset_ram(f.nes);
    nes_test_cpu_prepare(f.nes, 0x2FD);
    f.nes->nes_cpu.cpu_ram[0x2FD] = 0xD0;   /* BNE, target $02FF + $7F = $037E */
    f.nes->nes_cpu.cpu_ram[0x2FE] = 0x7F;
    {
        uint16_t cycles = 0;
        cpu_step(f.nes, &cycles);
        TEST_EQ_U32(4, cycles);
        TEST_EQ_U32(0x037E, f.nes->nes_cpu.PC);
    }

    test_fixture_free(&f);
    return TEST_PASS;
}

int test_cpu_flags(void) {
    test_fixture_t f;
    TEST_CHECK(cpu_fixture(&f));
    nes_t* nes = f.nes;

    /* LDA #$80 / #$00 */
    cpu_reset_ram(nes);
    nes_test_cpu_prepare(nes, 0x200);
    nes->nes_cpu.cpu_ram[0x200] = 0xA9;
    nes->nes_cpu.cpu_ram[0x201] = 0x80;
    cpu_step(nes, NULL);
    TEST_EQ_U32(0x80, nes->nes_cpu.A);
    TEST_CHECK((nes->nes_cpu.P & P_N) != 0);
    TEST_CHECK((nes->nes_cpu.P & P_Z) == 0);

    cpu_reset_ram(nes);
    nes_test_cpu_prepare(nes, 0x200);
    nes->nes_cpu.cpu_ram[0x200] = 0xA9;
    nes->nes_cpu.cpu_ram[0x201] = 0x00;
    cpu_step(nes, NULL);
    TEST_CHECK((nes->nes_cpu.P & P_Z) != 0);
    TEST_CHECK((nes->nes_cpu.P & P_N) == 0);

    /* ADC binary overflow cases (2A03 has no decimal mode). */
    static const struct { uint8_t a, operand, carry_in, result, flags; } adc[] = {
        { 0x7F, 0x01, 0, 0x80, P_N | P_V },
        { 0xFF, 0x01, 0, 0x00, P_Z | P_C },
        { 0x80, 0x80, 0, 0x00, P_Z | P_C | P_V },
        { 0x00, 0x00, 1, 0x01, 0 },
        { 0x50, 0x50, 0, 0xA0, P_N | P_V },
    };
    for (size_t i = 0; i < sizeof(adc) / sizeof(adc[0]); ++i) {
        cpu_reset_ram(nes);
        nes_test_cpu_prepare(nes, 0x200);
        nes->nes_cpu.A = adc[i].a;
        if (adc[i].carry_in) nes->nes_cpu.P |= P_C; else nes->nes_cpu.P &= P_C_CLR;
        nes->nes_cpu.cpu_ram[0x200] = 0x69;
        nes->nes_cpu.cpu_ram[0x201] = adc[i].operand;
        cpu_step(nes, NULL);
        TEST_EQ_U32(adc[i].result, nes->nes_cpu.A);
        TEST_EQ_U32(adc[i].flags, (uint8_t)(nes->nes_cpu.P & (P_N | P_V | P_Z | P_C)));
    }

    /* SBC */
    cpu_reset_ram(nes);
    nes_test_cpu_prepare(nes, 0x200);
    nes->nes_cpu.A = 0x00;
    nes->nes_cpu.P |= P_C;
    nes->nes_cpu.cpu_ram[0x200] = 0xE9;
    nes->nes_cpu.cpu_ram[0x201] = 0x01;
    cpu_step(nes, NULL);
    TEST_EQ_U32(0xFF, nes->nes_cpu.A);
    TEST_EQ_U32(P_N, (uint8_t)(nes->nes_cpu.P & (P_N | P_V | P_Z | P_C)));

    cpu_reset_ram(nes);
    nes_test_cpu_prepare(nes, 0x200);
    nes->nes_cpu.A = 0x80;
    nes->nes_cpu.P |= P_C;
    nes->nes_cpu.cpu_ram[0x200] = 0xE9;
    nes->nes_cpu.cpu_ram[0x201] = 0x01;
    cpu_step(nes, NULL);
    TEST_EQ_U32(0x7F, nes->nes_cpu.A);
    TEST_EQ_U32(P_C | P_V, (uint8_t)(nes->nes_cpu.P & (P_N | P_V | P_Z | P_C)));

    /* The decimal flag is ignored by the 2A03. */
    cpu_reset_ram(nes);
    nes_test_cpu_prepare(nes, 0x200);
    nes->nes_cpu.A = 0x01;
    nes->nes_cpu.P |= P_D;
    nes->nes_cpu.cpu_ram[0x200] = 0x69;
    nes->nes_cpu.cpu_ram[0x201] = 0x01;
    cpu_step(nes, NULL);
    TEST_EQ_U32(0x02, nes->nes_cpu.A);
    TEST_CHECK((nes->nes_cpu.P & P_C) == 0);

    /* CMP */
    cpu_reset_ram(nes);
    nes_test_cpu_prepare(nes, 0x200);
    nes->nes_cpu.A = 0x80;
    nes->nes_cpu.cpu_ram[0x200] = 0xC9;
    nes->nes_cpu.cpu_ram[0x201] = 0x80;
    cpu_step(nes, NULL);
    TEST_EQ_U32(P_C | P_Z, (uint8_t)(nes->nes_cpu.P & (P_N | P_Z | P_C)));

    cpu_reset_ram(nes);
    nes_test_cpu_prepare(nes, 0x200);
    nes->nes_cpu.A = 0x7F;
    nes->nes_cpu.cpu_ram[0x200] = 0xC9;
    nes->nes_cpu.cpu_ram[0x201] = 0x80;
    cpu_step(nes, NULL);
    TEST_EQ_U32(P_N, (uint8_t)(nes->nes_cpu.P & (P_N | P_Z | P_C)));

    /* BIT $10: N/V come from the operand, Z from A & operand. */
    cpu_reset_ram(nes);
    nes_test_cpu_prepare(nes, 0x200);
    nes->nes_cpu.cpu_ram[0x0010] = 0xC0;
    nes->nes_cpu.cpu_ram[0x200] = 0x24;
    nes->nes_cpu.cpu_ram[0x201] = 0x10;
    cpu_step(nes, NULL);
    TEST_EQ_U32(P_N | P_V | P_Z, (uint8_t)(nes->nes_cpu.P & (P_N | P_V | P_Z)));

    cpu_reset_ram(nes);
    nes_test_cpu_prepare(nes, 0x200);
    nes->nes_cpu.A = 0x40;
    nes->nes_cpu.cpu_ram[0x0010] = 0xC0;
    nes->nes_cpu.cpu_ram[0x200] = 0x24;
    nes->nes_cpu.cpu_ram[0x201] = 0x10;
    cpu_step(nes, NULL);
    TEST_EQ_U32(P_N | P_V, (uint8_t)(nes->nes_cpu.P & (P_N | P_V | P_Z)));

    /* Shifts / rotates */
    cpu_reset_ram(nes);
    nes_test_cpu_prepare(nes, 0x200);
    nes->nes_cpu.A = 0x81;
    nes->nes_cpu.cpu_ram[0x200] = 0x0A;     /* ASL A */
    cpu_step(nes, NULL);
    TEST_EQ_U32(0x02, nes->nes_cpu.A);
    TEST_EQ_U32(P_C, (uint8_t)(nes->nes_cpu.P & (P_N | P_Z | P_C)));

    cpu_reset_ram(nes);
    nes_test_cpu_prepare(nes, 0x200);
    nes->nes_cpu.A = 0x81;
    nes->nes_cpu.cpu_ram[0x200] = 0x4A;     /* LSR A */
    cpu_step(nes, NULL);
    TEST_EQ_U32(0x40, nes->nes_cpu.A);
    TEST_EQ_U32(P_C, (uint8_t)(nes->nes_cpu.P & (P_N | P_Z | P_C)));

    cpu_reset_ram(nes);
    nes_test_cpu_prepare(nes, 0x200);
    nes->nes_cpu.A = 0x40;
    nes->nes_cpu.P |= P_C;
    nes->nes_cpu.cpu_ram[0x200] = 0x2A;     /* ROL A */
    cpu_step(nes, NULL);
    TEST_EQ_U32(0x81, nes->nes_cpu.A);
    TEST_EQ_U32(P_N, (uint8_t)(nes->nes_cpu.P & (P_N | P_Z | P_C)));

    cpu_reset_ram(nes);
    nes_test_cpu_prepare(nes, 0x200);
    nes->nes_cpu.A = 0x01;
    nes->nes_cpu.P |= P_C;
    nes->nes_cpu.cpu_ram[0x200] = 0x6A;     /* ROR A */
    cpu_step(nes, NULL);
    TEST_EQ_U32(0x80, nes->nes_cpu.A);
    TEST_EQ_U32(P_N | P_C, (uint8_t)(nes->nes_cpu.P & (P_N | P_Z | P_C)));

    /* INC/DEC zero page */
    cpu_reset_ram(nes);
    nes_test_cpu_prepare(nes, 0x200);
    nes->nes_cpu.cpu_ram[0x0010] = 0xFF;
    nes->nes_cpu.cpu_ram[0x200] = 0xE6;
    nes->nes_cpu.cpu_ram[0x201] = 0x10;
    cpu_step(nes, NULL);
    TEST_EQ_U32(0x00, nes->nes_cpu.cpu_ram[0x0010]);
    TEST_EQ_U32(P_Z, (uint8_t)(nes->nes_cpu.P & (P_N | P_Z | P_C)));

    /* Flag instructions */
    cpu_reset_ram(nes);
    nes_test_cpu_prepare(nes, 0x200);
    nes->nes_cpu.cpu_ram[0x200] = 0x38; cpu_step(nes, NULL); TEST_CHECK((nes->nes_cpu.P & P_C) != 0);
    nes->nes_cpu.cpu_ram[0x201] = 0x18; cpu_step(nes, NULL); TEST_CHECK((nes->nes_cpu.P & P_C) == 0);
    nes->nes_cpu.cpu_ram[0x202] = 0xF8; cpu_step(nes, NULL); TEST_CHECK((nes->nes_cpu.P & P_D) != 0);
    nes->nes_cpu.cpu_ram[0x203] = 0xD8; cpu_step(nes, NULL); TEST_CHECK((nes->nes_cpu.P & P_D) == 0);
    nes->nes_cpu.cpu_ram[0x204] = 0x58; cpu_step(nes, NULL); TEST_CHECK((nes->nes_cpu.P & P_I) == 0);
    nes->nes_cpu.cpu_ram[0x205] = 0x78; cpu_step(nes, NULL); TEST_CHECK((nes->nes_cpu.P & P_I) != 0);
    nes->nes_cpu.P |= P_V;
    nes->nes_cpu.cpu_ram[0x206] = 0xB8; cpu_step(nes, NULL); TEST_CHECK((nes->nes_cpu.P & P_V) == 0);

    /* TXS writes SP without touching flags; TSX sets N/Z from SP. */
    cpu_reset_ram(nes);
    nes_test_cpu_prepare(nes, 0x200);
    nes->nes_cpu.X = 0x80;
    nes->nes_cpu.cpu_ram[0x200] = 0x9A;     /* TXS */
    cpu_step(nes, NULL);
    TEST_EQ_U32(0x80, nes->nes_cpu.SP);
    TEST_EQ_U32(P_I | P_U, nes->nes_cpu.P);

    cpu_reset_ram(nes);
    nes_test_cpu_prepare(nes, 0x200);
    nes->nes_cpu.SP = 0x80;
    nes->nes_cpu.cpu_ram[0x200] = 0xBA;     /* TSX */
    cpu_step(nes, NULL);
    TEST_EQ_U32(0x80, nes->nes_cpu.X);
    TEST_CHECK((nes->nes_cpu.P & P_N) != 0);

    test_fixture_free(&f);
    return TEST_PASS;
}

int test_cpu_stack(void) {
    test_fixture_t f;
    TEST_CHECK(cpu_fixture(&f));
    nes_t* nes = f.nes;
    const uint8_t* stack = nes->nes_cpu.cpu_ram + 0x100;

    /* JSR pushes the address of the last byte of the instruction. */
    cpu_reset_ram(nes);
    nes_test_cpu_prepare(nes, 0x200);
    {
        const uint8_t code[] = { 0x20, 0x34, 0x12 };
        cpu_put(nes, 0x200, code, sizeof(code));
    }
    cpu_step(nes, NULL);
    TEST_EQ_U32(0x1234, nes->nes_cpu.PC);
    TEST_EQ_U32(0xFB, nes->nes_cpu.SP);
    TEST_EQ_U32(0x02, stack[0xFD]);
    TEST_EQ_U32(0x02, stack[0xFC]);

    /* RTS pops and adds one. */
    cpu_reset_ram(nes);
    nes_test_cpu_prepare(nes, 0x200);
    nes->nes_cpu.SP = 0xFA;
    nes->nes_cpu.cpu_ram[0x1FB] = 0x02;
    nes->nes_cpu.cpu_ram[0x1FC] = 0x02;
    nes->nes_cpu.cpu_ram[0x200] = 0x60;
    cpu_step(nes, NULL);
    TEST_EQ_U32(0x0203, nes->nes_cpu.PC);
    TEST_EQ_U32(0xFC, nes->nes_cpu.SP);

    /* BRK pushes PC+2 and P with B set. */
    cpu_reset_ram(nes);
    nes_test_cpu_prepare(nes, 0x200);
    nes->nes_cpu.cpu_ram[0x200] = 0x00;
    cpu_step(nes, NULL);
    TEST_EQ_U32(NMI_VECTOR_PC, nes->nes_cpu.PC);
    TEST_EQ_U32(0xFA, nes->nes_cpu.SP);
    TEST_EQ_U32(0x02, stack[0xFD]);
    TEST_EQ_U32(0x02, stack[0xFC]);
    TEST_CHECK((stack[0xFB] & P_B) != 0);
    TEST_CHECK((stack[0xFB] & P_U) != 0);
    TEST_CHECK((nes->nes_cpu.P & P_I) != 0);

    /* RTI restores P and PC verbatim (no +1). */
    cpu_reset_ram(nes);
    nes_test_cpu_prepare(nes, 0x200);
    nes->nes_cpu.SP = 0xFC;
    nes->nes_cpu.cpu_ram[0x1FD] = P_C | P_U;
    nes->nes_cpu.cpu_ram[0x1FE] = 0x34;
    nes->nes_cpu.cpu_ram[0x1FF] = 0x12;
    nes->nes_cpu.cpu_ram[0x200] = 0x40;
    cpu_step(nes, NULL);
    TEST_EQ_U32(0x1234, nes->nes_cpu.PC);
    TEST_EQ_U32(0xFF, nes->nes_cpu.SP);
    TEST_CHECK((nes->nes_cpu.P & P_C) != 0);
    TEST_CHECK((nes->nes_cpu.P & P_U) != 0);
    TEST_CHECK((nes->nes_cpu.P & P_B) == 0);

    /* PHA/PHP stack bytes and SP wrap at $00. */
    cpu_reset_ram(nes);
    nes_test_cpu_prepare(nes, 0x200);
    nes->nes_cpu.SP = 0x00;
    nes->nes_cpu.A = 0x5A;
    nes->nes_cpu.cpu_ram[0x200] = 0x48;     /* PHA */
    cpu_step(nes, NULL);
    TEST_EQ_U32(0xFF, nes->nes_cpu.SP);
    TEST_EQ_U32(0x5A, stack[0x00]);

    cpu_reset_ram(nes);
    nes_test_cpu_prepare(nes, 0x200);
    nes->nes_cpu.cpu_ram[0x200] = 0x08;     /* PHP */
    cpu_step(nes, NULL);
    TEST_CHECK((stack[0xFD] & P_B) != 0);
    TEST_CHECK((stack[0xFD] & P_U) != 0);

    /* PLP restores flags and forces U=1/B=0. */
    cpu_reset_ram(nes);
    nes_test_cpu_prepare(nes, 0x200);
    nes->nes_cpu.SP = 0xFC;
    nes->nes_cpu.cpu_ram[0x1FD] = P_C | P_B;
    nes->nes_cpu.cpu_ram[0x200] = 0x28;     /* PLP */
    cpu_step(nes, NULL);
    TEST_EQ_U32(0xFD, nes->nes_cpu.SP);
    TEST_CHECK((nes->nes_cpu.P & P_C) != 0);
    TEST_CHECK((nes->nes_cpu.P & P_U) != 0);
    TEST_CHECK((nes->nes_cpu.P & P_B) == 0);

    /* NMI pushes P with B clear, IRQ likewise. */
    cpu_reset_ram(nes);
    nes_test_cpu_prepare(nes, 0x200);
    nes->nes_cpu.cpu_ram[0x200] = 0xEA;
    nes->nes_cpu.irq_nmi = 1;
    cpu_step(nes, NULL);
    TEST_EQ_U32(NMI_VECTOR_PC, nes->nes_cpu.PC);
    TEST_EQ_U32(0xFA, nes->nes_cpu.SP);
    TEST_CHECK((stack[0xFB] & P_B) == 0);
    TEST_CHECK((nes->nes_cpu.P & P_I) != 0);

    cpu_reset_ram(nes);
    nes_test_cpu_prepare(nes, 0x200);
    nes->nes_cpu.P &= P_I_CLR;
    nes->nes_cpu.cpu_ram[0x200] = 0xEA;
    nes->nes_cpu.irq_pending = 1;
    cpu_step(nes, NULL);
    TEST_EQ_U32(NMI_VECTOR_PC, nes->nes_cpu.PC);
    TEST_CHECK((stack[0xFB] & P_B) == 0);

    test_fixture_free(&f);
    return TEST_PASS;
}

int test_cpu_undocumented(void) {
    static const uint8_t kil[] = { 0x02, 0x12, 0x22, 0x32, 0x42, 0x52, 0x62, 0x72,
                                   0x92, 0xB2, 0xD2, 0xF2 };
    test_fixture_t f;
    TEST_CHECK(cpu_fixture(&f));
    nes_t* nes = f.nes;

    /* Every opcode in the 256-entry matrix must terminate the step. */
    for (unsigned opcode = 0; opcode < 256; ++opcode) {
        cpu_reset_ram(nes);
        nes_test_cpu_prepare(nes, 0x200);
        nes->nes_cpu.cpu_ram[0x200] = (uint8_t)opcode;
        uint16_t cycles = 0;
        TEST_EQ_I32(NES_OK, cpu_step(nes, &cycles));
        TEST_CHECK(cycles >= 1 && cycles <= 12);
    }

    /* KIL (JAM) is modelled as a zero-cycle no-op: the budget loop therefore
     * runs the following byte too, which is a BRK at $0201 in this fixture. */
    for (size_t i = 0; i < sizeof(kil) / sizeof(kil[0]); ++i) {
        cpu_reset_ram(nes);
        nes_test_cpu_prepare(nes, 0x200);
        nes->nes_cpu.cpu_ram[0x200] = kil[i];
        cpu_step(nes, NULL);
        if (nes->nes_cpu.PC != NMI_VECTOR_PC) {
            test_record_failure(__FILE__, __LINE__, "KIL consumes no cycles (next byte = BRK)",
                                "0x8000", "other");
            test_fixture_free(&f);
            return TEST_FAIL;
        }
    }

    /* LAX: A = X = memory */
    cpu_reset_ram(nes);
    nes_test_cpu_prepare(nes, 0x200);
    nes->nes_cpu.cpu_ram[0x0010] = 0x42;
    nes->nes_cpu.cpu_ram[0x200] = 0xA7;
    nes->nes_cpu.cpu_ram[0x201] = 0x10;
    cpu_step(nes, NULL);
    TEST_EQ_U32(0x42, nes->nes_cpu.A);
    TEST_EQ_U32(0x42, nes->nes_cpu.X);

    /* SAX: memory = A & X */
    cpu_reset_ram(nes);
    nes_test_cpu_prepare(nes, 0x200);
    nes->nes_cpu.A = 0x0F;
    nes->nes_cpu.X = 0xF0;
    nes->nes_cpu.cpu_ram[0x200] = 0x87;
    nes->nes_cpu.cpu_ram[0x201] = 0x10;
    cpu_step(nes, NULL);
    TEST_EQ_U32(0x00, nes->nes_cpu.cpu_ram[0x0010]);

    /* DCP: decrement then compare */
    cpu_reset_ram(nes);
    nes_test_cpu_prepare(nes, 0x200);
    nes->nes_cpu.A = 0x42;
    nes->nes_cpu.cpu_ram[0x0010] = 0x42;
    nes->nes_cpu.cpu_ram[0x200] = 0xC7;
    nes->nes_cpu.cpu_ram[0x201] = 0x10;
    cpu_step(nes, NULL);
    TEST_EQ_U32(0x41, nes->nes_cpu.cpu_ram[0x0010]);
    TEST_EQ_U32(P_C, (uint8_t)(nes->nes_cpu.P & (P_Z | P_C)));

    /* ISC: increment then subtract */
    cpu_reset_ram(nes);
    nes_test_cpu_prepare(nes, 0x200);
    nes->nes_cpu.A = 0x02;
    nes->nes_cpu.P |= P_C;
    nes->nes_cpu.cpu_ram[0x0010] = 0x00;
    nes->nes_cpu.cpu_ram[0x200] = 0xE7;
    nes->nes_cpu.cpu_ram[0x201] = 0x10;
    cpu_step(nes, NULL);
    TEST_EQ_U32(0x01, nes->nes_cpu.cpu_ram[0x0010]);
    TEST_EQ_U32(0x01, nes->nes_cpu.A);
    TEST_CHECK((nes->nes_cpu.P & P_C) != 0);

    /* SLO: shift left then OR */
    cpu_reset_ram(nes);
    nes_test_cpu_prepare(nes, 0x200);
    nes->nes_cpu.A = 0x00;
    nes->nes_cpu.cpu_ram[0x0010] = 0x81;
    nes->nes_cpu.cpu_ram[0x200] = 0x07;
    nes->nes_cpu.cpu_ram[0x201] = 0x10;
    cpu_step(nes, NULL);
    TEST_EQ_U32(0x02, nes->nes_cpu.cpu_ram[0x0010]);
    TEST_EQ_U32(0x02, nes->nes_cpu.A);
    TEST_CHECK((nes->nes_cpu.P & P_C) != 0);

    /* ANC: A &= imm, C = bit 7 of the result */
    cpu_reset_ram(nes);
    nes_test_cpu_prepare(nes, 0x200);
    nes->nes_cpu.A = 0x81;
    nes->nes_cpu.cpu_ram[0x200] = 0x0B;
    nes->nes_cpu.cpu_ram[0x201] = 0xFF;
    cpu_step(nes, NULL);
    TEST_EQ_U32(0x81, nes->nes_cpu.A);
    TEST_CHECK((nes->nes_cpu.P & P_C) != 0);

    /* ALR: A = (A & imm) >> 1 */
    cpu_reset_ram(nes);
    nes_test_cpu_prepare(nes, 0x200);
    nes->nes_cpu.A = 0xFF;
    nes->nes_cpu.cpu_ram[0x200] = 0x4B;
    nes->nes_cpu.cpu_ram[0x201] = 0x80;
    cpu_step(nes, NULL);
    TEST_EQ_U32(0x40, nes->nes_cpu.A);
    TEST_CHECK((nes->nes_cpu.P & P_C) == 0);

    test_fixture_free(&f);
    return TEST_PASS;
}

int test_cpu_interrupts(void) {
    test_fixture_t f;
    TEST_CHECK(cpu_fixture(&f));
    nes_t* nes = f.nes;

    /* A pending NMI is serviced at the next instruction boundary. */
    cpu_reset_ram(nes);
    nes_test_cpu_prepare(nes, 0x200);
    nes->nes_cpu.cpu_ram[0x200] = 0xEA;
    nes->nes_cpu.irq_nmi = 1;
    cpu_step(nes, NULL);
    TEST_EQ_U32(NMI_VECTOR_PC, nes->nes_cpu.PC);
    TEST_CHECK(nes->nes_cpu.irq_nmi == 0);

    /* $2000 enabling NMI while VBlank is set has a one-instruction delay. */
    cpu_reset_ram(nes);
    nes_test_cpu_prepare(nes, 0x200);
    nes->nes_cpu.cpu_ram[0x200] = 0xEA;
    nes->nes_cpu.cpu_ram[0x201] = 0xEA;
    nes->nes_ppu.ppu_ctrl = 0;
    nes->nes_ppu.STATUS_V = 1;
    nes_write_ppu_register(nes, 0x2000, 0x80);
    TEST_CHECK(nes->nes_cpu.irq_nmi_delay == 1);
    cpu_step(nes, NULL);
    TEST_EQ_U32(0x0201, nes->nes_cpu.PC);       /* not serviced yet */
    cpu_step(nes, NULL);
    TEST_EQ_U32(NMI_VECTOR_PC, nes->nes_cpu.PC);/* serviced one instruction later */

    /* Level triggered IRQ: stays asserted after being serviced. */
    cpu_reset_ram(nes);
    nes_test_cpu_prepare(nes, 0x200);
    nes->nes_cpu.P &= P_I_CLR;
    nes->nes_cpu.cpu_ram[0x200] = 0xEA;
    nes->nes_cpu.irq_pending = 1;
    cpu_step(nes, NULL);
    TEST_EQ_U32(NMI_VECTOR_PC, nes->nes_cpu.PC);
    TEST_CHECK(nes->nes_cpu.irq_pending == 1);

    /* CLI takes effect one instruction late. */
    cpu_reset_ram(nes);
    nes_test_cpu_prepare(nes, 0x200);
    nes->nes_cpu.P |= P_I;
    nes->nes_cpu.cpu_ram[0x200] = 0x58;         /* CLI */
    nes->nes_cpu.cpu_ram[0x201] = 0xEA;         /* NOP */
    nes->nes_cpu.irq_pending = 1;
    cpu_step(nes, NULL);
    TEST_EQ_U32(0x0201, nes->nes_cpu.PC);
    TEST_CHECK((nes->nes_cpu.P & P_I) == 0);
    cpu_step(nes, NULL);
    TEST_EQ_U32(NMI_VECTOR_PC, nes->nes_cpu.PC);

    /* SEI does not mask an IRQ that was already sampled. */
    cpu_reset_ram(nes);
    nes_test_cpu_prepare(nes, 0x200);
    nes->nes_cpu.P &= P_I_CLR;
    nes->nes_cpu.cpu_ram[0x200] = 0x78;         /* SEI */
    nes->nes_cpu.irq_pending = 1;
    cpu_step(nes, NULL);
    TEST_EQ_U32(NMI_VECTOR_PC, nes->nes_cpu.PC);
    TEST_CHECK((nes->nes_cpu.P & P_I) != 0);

    /* RTI uses the restored I flag with no delay. */
    cpu_reset_ram(nes);
    nes_test_cpu_prepare(nes, 0x200);
    nes->nes_cpu.SP = 0xFC;
    nes->nes_cpu.cpu_ram[0x1FD] = P_U;          /* I clear */
    nes->nes_cpu.cpu_ram[0x1FE] = 0x00;
    nes->nes_cpu.cpu_ram[0x1FF] = 0x02;
    nes->nes_cpu.cpu_ram[0x200] = 0x40;         /* RTI */
    nes->nes_cpu.irq_pending = 1;
    cpu_step(nes, NULL);
    TEST_EQ_U32(NMI_VECTOR_PC, nes->nes_cpu.PC);

    /* nes_cpu_irq() only raises the level; the CPU services it when I=0. */
    cpu_reset_ram(nes);
    nes_test_cpu_prepare(nes, 0x200);
    nes->nes_cpu.P |= P_I;
    nes->nes_cpu.cpu_ram[0x200] = 0xEA;
    nes_cpu_irq(nes);
    cpu_step(nes, NULL);
    TEST_EQ_U32(0x0201, nes->nes_cpu.PC);
    nes->nes_cpu.P &= P_I_CLR;
    nes->nes_cpu.cpu_ram[0x201] = 0xEA;
    cpu_step(nes, NULL);
    TEST_EQ_U32(NMI_VECTOR_PC, nes->nes_cpu.PC);

    test_fixture_free(&f);
    return TEST_PASS;
}

int test_cpu_oam_dma(void) {
    test_fixture_t f;
    TEST_CHECK(cpu_fixture(&f));
    nes_t* nes = f.nes;

    /* $0200 page -> OAM, oam_addr = 0 */
    cpu_reset_ram(nes);
    nes_test_cpu_prepare(nes, 0x200);
    for (unsigned i = 0; i < 256; ++i) nes->nes_cpu.cpu_ram[0x200 + i] = (uint8_t)(0xA5u ^ i);
    nes_test_cpu_write(nes, 0x4014, 0x02);
    for (unsigned i = 0; i < 256; ++i) {
        if (nes->nes_ppu.oam_data[i] != (uint8_t)(0xA5u ^ i)) {
            test_record_failure(__FILE__, __LINE__, "OAM DMA from $0200", "A5^i", "other");
            test_fixture_free(&f);
            return TEST_FAIL;
        }
    }
    TEST_EQ_U32(513, nes->nes_cpu.cycles);

    /* Odd CPU cycle costs one extra cycle. */
    cpu_reset_ram(nes);
    nes_test_cpu_prepare(nes, 0x200);
    nes->nes_cpu.cycles = 1;
    nes_test_cpu_write(nes, 0x4014, 0x02);
    TEST_EQ_U32(515, nes->nes_cpu.cycles);      /* 1 + 513 + 1 */

    /* A non-zero OAMADDR rotates the copied page. */
    cpu_reset_ram(nes);
    nes_test_cpu_prepare(nes, 0x200);
    for (unsigned i = 0; i < 256; ++i) nes->nes_cpu.cpu_ram[0x300 + i] = (uint8_t)i;
    nes->nes_ppu.oam_addr = 0x10;
    nes_test_cpu_write(nes, 0x4014, 0x03);
    for (unsigned i = 0; i < 256; ++i) {
        if (nes->nes_ppu.oam_data[(0x10 + i) & 0xFF] != (uint8_t)i) {
            test_record_failure(__FILE__, __LINE__, "OAM DMA rotation", "src[i]", "other");
            test_fixture_free(&f);
            return TEST_FAIL;
        }
    }

    /* Work RAM source ($6000) goes through the CPU bus instead of a NULL pointer. */
    if (nes->nes_rom.sram != NULL) {
        cpu_reset_ram(nes);
        nes_test_cpu_prepare(nes, 0x200);
        nes->nes_ppu.oam_addr = 0;
        for (unsigned i = 0; i < 256; ++i) nes->nes_rom.sram[i] = (uint8_t)(0x5Au + i);
        nes_test_cpu_write(nes, 0x4014, 0x60);
        for (unsigned i = 0; i < 256; ++i) {
            if (nes->nes_ppu.oam_data[i] != (uint8_t)(0x5Au + i)) {
                test_record_failure(__FILE__, __LINE__, "OAM DMA from $6000", "0x5A+i", "other");
                test_fixture_free(&f);
                return TEST_FAIL;
            }
        }
    }

    /* $3000 mirrors the PPU registers; it must not crash and must still cost cycles. */
    cpu_reset_ram(nes);
    nes_test_cpu_prepare(nes, 0x200);
    nes_test_cpu_write(nes, 0x4014, 0x30);
    TEST_EQ_U32(513, nes->nes_cpu.cycles);

    test_fixture_free(&f);
    return TEST_PASS;
}

static uint8_t test_dma_prg_read(nes_t* nes, uint16_t addr) {
    (void)nes;
    return (uint8_t)(addr ^ 0x5Au);
}

int test_cpu_oam_dma_mapper_read(void) {
    const test_rom_spec_t spec = {
        .mapper = 0,
        .prg_units = 2,
        .chr_units = 1,
        .fill = TEST_ROM_FILL_RANDOM
    };
    test_fixture_t f;
    TEST_CHECK(test_fixture_make(&f, &spec));
    nes_t* nes = f.nes;

    /* Mapper 0 has no custom PRG reader, so $8000 still takes the memcpy path. */
    nes_memset(nes->nes_ppu.oam_data, 0, NES_PPU_OAM_SIZE);
    nes->nes_ppu.oam_addr = 0;
    nes->nes_cpu.cycles = 0;
    nes_test_cpu_write(nes, 0x4014, 0x80);
    for (unsigned i = 0; i < 4; ++i) {
        if (nes->nes_ppu.oam_data[i] != nes->nes_cpu.prg_banks[0][i]) {
            test_record_failure(__FILE__, __LINE__, "OAM DMA direct PRG", "prg_banks[0][i]", "other");
            test_fixture_free(&f);
            return TEST_FAIL;
        }
    }
    TEST_EQ_U32(513, nes->nes_cpu.cycles);

    nes->nes_mapper.mapper_read_prg = test_dma_prg_read;
    nes_memset(nes->nes_ppu.oam_data, 0, NES_PPU_OAM_SIZE);
    nes->nes_ppu.oam_addr = 0;
    nes->nes_cpu.cycles = 0;
    nes_test_cpu_write(nes, 0x4014, 0x80);
    for (unsigned i = 0; i < 256; ++i) {
        if (nes->nes_ppu.oam_data[i] != (uint8_t)((0x8000u + i) ^ 0x5Au)) {
            test_record_failure(__FILE__, __LINE__, "OAM DMA mapper PRG read", "addr^0x5A", "other");
            test_fixture_free(&f);
            return TEST_FAIL;
        }
    }
    TEST_EQ_U32(513, nes->nes_cpu.cycles);

    nes->nes_cpu.cycles = 1;
    nes_test_cpu_write(nes, 0x4014, 0x80);
    TEST_EQ_U32(515, nes->nes_cpu.cycles);   /* 1 + 513 + 1 */

    /* Bus reads must retain the normal OAMADDR rotation and wrap. */
    nes_memset(nes->nes_ppu.oam_data, 0, NES_PPU_OAM_SIZE);
    nes->nes_ppu.oam_addr = 0x10;
    nes->nes_cpu.cycles = 0;
    nes_test_cpu_write(nes, 0x4014, 0x80);
    for (unsigned i = 0; i < 256; ++i) {
        if (nes->nes_ppu.oam_data[(0x10u + i) & 0xFFu] != (uint8_t)((0x8000u + i) ^ 0x5Au)) {
            test_record_failure(__FILE__, __LINE__, "OAM DMA mapper rotation", "addr^0x5A", "other");
            test_fixture_free(&f);
            return TEST_FAIL;
        }
    }

    test_fixture_free(&f);
    return TEST_PASS;
}
