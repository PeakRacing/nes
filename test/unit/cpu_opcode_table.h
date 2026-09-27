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
#include <stdint.h>
#include <stddef.h>
typedef enum { MODE_IMP, MODE_ACC, MODE_IMM, MODE_ZP, MODE_ZPX, MODE_ZPY, MODE_ABS, MODE_ABX, MODE_ABY, MODE_IND, MODE_IZX, MODE_IZY, MODE_REL, MODE_COUNT } cpu_mode_t;

/* cycles is the base (no page-cross, branch-not-taken) cycle count as
 * documented on https://www.oxyron.de/html/opcodes02.html and implemented by
 * src/nes_cpu.c. The cycle test executes each opcode from a fixed fixture
 * (see unit/cpu_tests.c) so the value is comparable. */
typedef struct { uint8_t opcode; uint8_t mode; uint8_t cycles; const char* name; } cpu_opcode_info_t;
const cpu_opcode_info_t* cpu_opcode_table(size_t* count);
