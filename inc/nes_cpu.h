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


// https://www.nesdev.org/wiki/CPU_memory_map
#define NES_CPU_RAM_SIZE        0x800   /*  2KB */

#define NES_VERCTOR_NMI         0xFFFA  /*  NMI vector (NMI=not maskable interupts) */
#define NES_VERCTOR_RESET       0xFFFC  /*  Reset vector */
#define NES_VERCTOR_IRQBRK      0xFFFE  /*  IRQ vector */

struct nes;
typedef struct nes nes_t;

/*
Bit No. 15      14      13      12      11      10      9       8
        A1      B1      Select1 Start1  Up1     Down1   Left1   Right1 
Bit No. 7       6       5       4       3       2       1       0
        A2      B2      Select2 Start2  Up2     Down2   Left2   Right2 
*/
typedef struct nes_joypad{
    uint8_t offset1;
    uint8_t offset2;
    uint8_t mask;
    uint8_t exp_keyboard;               /*  $4016 bit 2: expansion-port keyboard (Family BASIC) selected,
                                                so $4017 belongs to it instead of controller 2 */
    union {
        struct {
            uint8_t R2:1;   
            uint8_t L2:1;    
            uint8_t D2:1;    
            uint8_t U2:1;  
            uint8_t ST2:1; 
            uint8_t SE2:1;
            uint8_t B2:1;    
            uint8_t A2:1;
            uint8_t R1:1;   
            uint8_t L1:1;    
            uint8_t D1:1;    
            uint8_t U1:1;  
            uint8_t ST1:1; 
            uint8_t SE1:1;
            uint8_t B1:1;    
            uint8_t A1:1;  
        };
        uint16_t joypad;
    };
    /* VS. UniSystem arcade switches.  The cabinet has no second gamepad: the coin/credit/service
       inputs share the upper bits of the controller ports, which the games read *outside* the
       shift loop (VS Battle City tests $4016 bit 4 for the coin, $4016 bit 3 for the credit/start
       input and $4017 bit 2 for service).  The standard 8-bit shift is bit 0 only, so keeping
       these bits out of `joypad` leaves ordinary controller reads untouched. */
#if (NES_VS_SYSTEM == 1)
    uint8_t vs_coin;
    uint8_t vs_start;
    uint8_t vs_service;
    uint8_t vs_protection_counter;   /* VS. System protection counter: $5E00 resets, $5E01 advances */
#endif
} nes_joypad_t;

// https://www.nesdev.org/wiki/CPU_registers
typedef struct nes_cpu{
    /*  CPU registers */
    uint8_t A;                          /*  Accumulator */
    uint8_t X;                          /*  Indexes X */
    uint8_t Y;                          /*  Indexes Y */
    uint8_t SP;                         /*  Stack Pointer */
    uint16_t PC;                        /*  Program Counter */
    union {
        struct {
            uint8_t C:1;                /*  carry flag (1 on unsigned overflow) */
            uint8_t Z:1;                /*  zero flag (1 when all bits of a result are 0) */
            uint8_t I:1;                /*  IRQ flag (when 1, no interupts will occur (exceptions are IRQs forced by BRK and NMIs)) */
            uint8_t D:1;                /*  decimal flag (1 when CPU in BCD mode) */
            uint8_t B:1;                /*  break flag (1 when interupt was caused by a BRK) */
            uint8_t U:1;                /*  unused (always 1) */
            uint8_t V:1;                /*  overflow flag (1 on signed overflow) */
            uint8_t N:1;                /*  negative flag (1 when result is negative) */
        };
        uint8_t P;                      /*  Status Register */
    };
    uint8_t irq_counter;
    uint8_t irq_nmi;
    uint8_t irq_nmi_delay;              /* delayed NMI from $2000 write during VBlank (fires 1 instruction later) */
    uint8_t irq_pending;                   /*  IRQ line asserted (level-triggered, polled per instruction) */
    uint8_t opcode;
    uint8_t write_burst;                /*  bus writes made by the instruction being executed (0 at fetch).
                                             A 6502 read-modify-write writes twice (old value, then the
                                             modified one), and mappers can latch the first: MMC1 ignores
                                             a bit write that arrives right after another one. */
    uint16_t cycles;
    uint8_t cpu_ram[NES_CPU_RAM_SIZE];
    uint8_t* prg_banks[4];              /*  4 bank ( 8Kb * 4 ) = 32KB  */
    nes_joypad_t joypad;
} nes_cpu_t;

void nes_cpu_init(nes_t *nes);
void nes_cpu_reset(nes_t* nes);
void nes_cpu_irq(nes_t* nes);
#if (NES_ENABLE_SOUND == 1)
/* Read one byte the way the DMC's sample fetcher does: straight off the CPU bus
 * (mapper-mapped PRG/SRAM included) and without touching the test-mode read log. */
uint8_t nes_cpu_dma_read(nes_t* nes, uint16_t address);
#endif

void nes_opcode(nes_t* nes,uint16_t ticks);

#ifdef __cplusplus          
    }
#endif
