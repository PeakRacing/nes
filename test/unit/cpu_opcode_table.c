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
#include "cpu_opcode_table.h"

/* Official 6502 instruction set: 151 opcodes / 13 addressing modes.
 * E(opcode, mode, base cycles, mnemonic) — see cpu_opcode_table.h. */
#define E(op, mode, cycles, name) {op, mode, cycles, name}
static const cpu_opcode_info_t table[] = {
E(0x00,MODE_IMP,7,"BRK"),E(0x01,MODE_IZX,6,"ORA"),E(0x05,MODE_ZP,3,"ORA"),E(0x06,MODE_ZP,5,"ASL"),E(0x08,MODE_IMP,3,"PHP"),E(0x09,MODE_IMM,2,"ORA"),E(0x0A,MODE_ACC,2,"ASL"),E(0x0D,MODE_ABS,4,"ORA"),E(0x0E,MODE_ABS,6,"ASL"),
E(0x10,MODE_REL,2,"BPL"),E(0x11,MODE_IZY,5,"ORA"),E(0x15,MODE_ZPX,4,"ORA"),E(0x16,MODE_ZPX,6,"ASL"),E(0x18,MODE_IMP,2,"CLC"),E(0x19,MODE_ABY,4,"ORA"),E(0x1D,MODE_ABX,4,"ORA"),E(0x1E,MODE_ABX,7,"ASL"),
E(0x20,MODE_ABS,6,"JSR"),E(0x21,MODE_IZX,6,"AND"),E(0x24,MODE_ZP,3,"BIT"),E(0x25,MODE_ZP,3,"AND"),E(0x26,MODE_ZP,5,"ROL"),E(0x28,MODE_IMP,4,"PLP"),E(0x29,MODE_IMM,2,"AND"),E(0x2A,MODE_ACC,2,"ROL"),E(0x2C,MODE_ABS,4,"BIT"),E(0x2D,MODE_ABS,4,"AND"),E(0x2E,MODE_ABS,6,"ROL"),
E(0x30,MODE_REL,2,"BMI"),E(0x31,MODE_IZY,5,"AND"),E(0x35,MODE_ZPX,4,"AND"),E(0x36,MODE_ZPX,6,"ROL"),E(0x38,MODE_IMP,2,"SEC"),E(0x39,MODE_ABY,4,"AND"),E(0x3D,MODE_ABX,4,"AND"),E(0x3E,MODE_ABX,7,"ROL"),
E(0x40,MODE_IMP,6,"RTI"),E(0x41,MODE_IZX,6,"EOR"),E(0x45,MODE_ZP,3,"EOR"),E(0x46,MODE_ZP,5,"LSR"),E(0x48,MODE_IMP,3,"PHA"),E(0x49,MODE_IMM,2,"EOR"),E(0x4A,MODE_ACC,2,"LSR"),E(0x4C,MODE_ABS,3,"JMP"),E(0x4D,MODE_ABS,4,"EOR"),E(0x4E,MODE_ABS,6,"LSR"),
E(0x50,MODE_REL,2,"BVC"),E(0x51,MODE_IZY,5,"EOR"),E(0x55,MODE_ZPX,4,"EOR"),E(0x56,MODE_ZPX,6,"LSR"),E(0x58,MODE_IMP,2,"CLI"),E(0x59,MODE_ABY,4,"EOR"),E(0x5D,MODE_ABX,4,"EOR"),E(0x5E,MODE_ABX,7,"LSR"),
E(0x60,MODE_IMP,6,"RTS"),E(0x61,MODE_IZX,6,"ADC"),E(0x65,MODE_ZP,3,"ADC"),E(0x66,MODE_ZP,5,"ROR"),E(0x68,MODE_IMP,4,"PLA"),E(0x69,MODE_IMM,2,"ADC"),E(0x6A,MODE_ACC,2,"ROR"),E(0x6C,MODE_IND,5,"JMP"),E(0x6D,MODE_ABS,4,"ADC"),E(0x6E,MODE_ABS,6,"ROR"),
E(0x70,MODE_REL,2,"BVS"),E(0x71,MODE_IZY,5,"ADC"),E(0x75,MODE_ZPX,4,"ADC"),E(0x76,MODE_ZPX,6,"ROR"),E(0x78,MODE_IMP,2,"SEI"),E(0x79,MODE_ABY,4,"ADC"),E(0x7D,MODE_ABX,4,"ADC"),E(0x7E,MODE_ABX,7,"ROR"),
E(0x81,MODE_IZX,6,"STA"),E(0x84,MODE_ZP,3,"STY"),E(0x85,MODE_ZP,3,"STA"),E(0x86,MODE_ZP,3,"STX"),E(0x88,MODE_IMP,2,"DEY"),E(0x8A,MODE_IMP,2,"TXA"),E(0x8C,MODE_ABS,4,"STY"),E(0x8D,MODE_ABS,4,"STA"),E(0x8E,MODE_ABS,4,"STX"),
E(0x90,MODE_REL,2,"BCC"),E(0x91,MODE_IZY,6,"STA"),E(0x94,MODE_ZPX,4,"STY"),E(0x95,MODE_ZPX,4,"STA"),E(0x96,MODE_ZPY,4,"STX"),E(0x98,MODE_IMP,2,"TYA"),E(0x99,MODE_ABY,5,"STA"),E(0x9A,MODE_IMP,2,"TXS"),E(0x9D,MODE_ABX,5,"STA"),
E(0xA0,MODE_IMM,2,"LDY"),E(0xA1,MODE_IZX,6,"LDA"),E(0xA2,MODE_IMM,2,"LDX"),E(0xA4,MODE_ZP,3,"LDY"),E(0xA5,MODE_ZP,3,"LDA"),E(0xA6,MODE_ZP,3,"LDX"),E(0xA8,MODE_IMP,2,"TAY"),E(0xA9,MODE_IMM,2,"LDA"),E(0xAA,MODE_IMP,2,"TAX"),E(0xAC,MODE_ABS,4,"LDY"),E(0xAD,MODE_ABS,4,"LDA"),E(0xAE,MODE_ABS,4,"LDX"),
E(0xB0,MODE_REL,2,"BCS"),E(0xB1,MODE_IZY,5,"LDA"),E(0xB4,MODE_ZPX,4,"LDY"),E(0xB5,MODE_ZPX,4,"LDA"),E(0xB6,MODE_ZPY,4,"LDX"),E(0xB8,MODE_IMP,2,"CLV"),E(0xB9,MODE_ABY,4,"LDA"),E(0xBA,MODE_IMP,2,"TSX"),E(0xBC,MODE_ABX,4,"LDY"),E(0xBD,MODE_ABX,4,"LDA"),E(0xBE,MODE_ABY,4,"LDX"),
E(0xC0,MODE_IMM,2,"CPY"),E(0xC1,MODE_IZX,6,"CMP"),E(0xC4,MODE_ZP,3,"CPY"),E(0xC5,MODE_ZP,3,"CMP"),E(0xC6,MODE_ZP,5,"DEC"),E(0xC8,MODE_IMP,2,"INY"),E(0xC9,MODE_IMM,2,"CMP"),E(0xCA,MODE_IMP,2,"DEX"),E(0xCC,MODE_ABS,4,"CPY"),E(0xCD,MODE_ABS,4,"CMP"),E(0xCE,MODE_ABS,6,"DEC"),
E(0xD0,MODE_REL,2,"BNE"),E(0xD1,MODE_IZY,5,"CMP"),E(0xD5,MODE_ZPX,4,"CMP"),E(0xD6,MODE_ZPX,6,"DEC"),E(0xD8,MODE_IMP,2,"CLD"),E(0xD9,MODE_ABY,4,"CMP"),E(0xDD,MODE_ABX,4,"CMP"),E(0xDE,MODE_ABX,7,"DEC"),
E(0xE0,MODE_IMM,2,"CPX"),E(0xE1,MODE_IZX,6,"SBC"),E(0xE4,MODE_ZP,3,"CPX"),E(0xE5,MODE_ZP,3,"SBC"),E(0xE6,MODE_ZP,5,"INC"),E(0xE8,MODE_IMP,2,"INX"),E(0xE9,MODE_IMM,2,"SBC"),E(0xEA,MODE_IMP,2,"NOP"),E(0xEC,MODE_ABS,4,"CPX"),E(0xED,MODE_ABS,4,"SBC"),E(0xEE,MODE_ABS,6,"INC"),
E(0xF0,MODE_REL,2,"BEQ"),E(0xF1,MODE_IZY,5,"SBC"),E(0xF5,MODE_ZPX,4,"SBC"),E(0xF6,MODE_ZPX,6,"INC"),E(0xF8,MODE_IMP,2,"SED"),E(0xF9,MODE_ABY,4,"SBC"),E(0xFD,MODE_ABX,4,"SBC"),E(0xFE,MODE_ABX,7,"INC")
};
const cpu_opcode_info_t* cpu_opcode_table(size_t* count) { if (count) *count = sizeof(table)/sizeof(table[0]); return table; }
