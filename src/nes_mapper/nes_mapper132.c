#include "nes.h"

/*
 * Mapper 132 - TXC 22211A (六角方块 [TXC] ...).
 * Authority: Mesen2 Core/NES/Mappers/Txc/Txc22211A.h plus Txc/TxcChip.h, constructed as
 *            TxcChip(false): mask 0x07 and NOT inverted at power-on (unlike mapper 147's
 *            JV001 chip).
 *
 *   PRG 32KB pages, CHR 8KB pages, one page pair selected by the scrambler's output:
 *     PRG page = (output >> 2) & 1 ; CHR page = output & 3
 *
 *   Scrambler registers (decoded by addr & 0xE103, so $4100-$4103 and $C100-$C103 both
 *   answer; the core routes those windows to mapper_apu and mapper_write respectively):
 *     $4100 write : increase ? accumulator++ : accumulator = ((acc & ~mask) | (staging & mask))
 *                                                           ^ (invert ? 0xFF : 0)
 *     $4101 write : invert   = bit 0
 *     $4102 write : staging  = value & mask ; inverter = value & ~mask
 *     $4103 write : increase = bit 0
 *     $8000+ write: output   = (accumulator & 0x0F) | ((inverter & 0x08) << 1)   [non-JV001]
 *     read        : (accumulator & mask) | ((inverter ^ (invert ? 0xFF : 0)) & ~mask)
 *
 *   Unlike mapper 147 the board does NOT scramble the CPU value, and it refreshes the
 *   mapping after every write (not just writes at $8000+).  Reading $4100/$4101/$4102/$4103
 *   returns (open bus & 0xF0) | (txc.Read() & 0x0F) - this core has no open bus value here,
 *   so the upper nibble is reported as 0.
 *
 *   Power-on: PRG page 0, CHR page 0.
 *
 * The old implementation was a plain two-register file on the APU window only, so the board
 * never saw the writes the games make above $8000.
 */

typedef struct {
    uint8_t accumulator;
    uint8_t inverter;
    uint8_t staging;
    uint8_t output;
    uint8_t increase;
    uint8_t invert;
} mapper132_t;

#define MAPPER132_MASK (0x07u)

static void nes_mapper_deinit(nes_t* nes) {
    nes_free(nes->nes_mapper.mapper_register);
    nes->nes_mapper.mapper_register = NULL;
}

static void mapper132_update_state(nes_t* nes) {
    mapper132_t* m = (mapper132_t*)nes->nes_mapper.mapper_register;
    nes_load_prgrom_32k(nes, 0, (uint16_t)((m->output >> 2) & 0x01u));
    nes_load_chrrom_8k(nes, 0, (uint8_t)(m->output & 0x03u));
}

static void mapper132_txc_write(mapper132_t* m, uint16_t address, uint8_t value) {
    if (address < 0x8000u) {
        switch (address & 0xE103u) {
        case 0x4100u:
            if (m->increase) {
                m->accumulator++;
            } else {
                m->accumulator = (uint8_t)((((m->accumulator & 0xF8u) |
                                             (m->staging & MAPPER132_MASK))
                                            ^ (m->invert ? 0xFFu : 0x00u)));
            }
            break;
        case 0x4101u:
            m->invert = (uint8_t)((value & 0x01u) != 0u);
            break;
        case 0x4102u:
            m->staging = (uint8_t)(value & MAPPER132_MASK);
            m->inverter = (uint8_t)(value & 0xF8u);
            break;
        case 0x4103u:
            m->increase = (uint8_t)((value & 0x01u) != 0u);
            break;
        default:
            break;
        }
    } else {
        m->output = (uint8_t)((m->accumulator & 0x0Fu) | ((m->inverter & 0x08u) << 1));
    }
}

static void nes_mapper_write(nes_t* nes, uint16_t address, uint8_t data) {
    mapper132_t* m = (mapper132_t*)nes->nes_mapper.mapper_register;
    mapper132_txc_write(m, address, (uint8_t)(data & 0x0Fu));
    mapper132_update_state(nes);
}

static void nes_mapper_apu(nes_t* nes, uint16_t address, uint8_t data) {
    mapper132_t* m = (mapper132_t*)nes->nes_mapper.mapper_register;
    mapper132_txc_write(m, address, (uint8_t)(data & 0x0Fu));
    mapper132_update_state(nes);
}

static uint8_t nes_mapper_read_apu(nes_t* nes, uint16_t address) {
    mapper132_t* m = (mapper132_t*)nes->nes_mapper.mapper_register;
    uint8_t value = 0;
    if ((address & 0x0103u) == 0x0100u) {
        value = (uint8_t)((m->accumulator & MAPPER132_MASK) |
                          ((m->inverter ^ (m->invert ? 0xFFu : 0x00u)) & 0xF8u));
        value = (uint8_t)(value & 0x0Fu);
    }
    mapper132_update_state(nes);
    return value;
}

static void nes_mapper_init(nes_t* nes) {
    if (nes->nes_mapper.mapper_register == NULL) {
        nes->nes_mapper.mapper_register = nes_mapper_register_alloc(nes, (uint16_t)sizeof(mapper132_t));
        if (nes->nes_mapper.mapper_register == NULL) return;
    }
    mapper132_t* m = (mapper132_t*)nes->nes_mapper.mapper_register;
    nes_memset(m, 0, sizeof(mapper132_t));
    mapper132_update_state(nes);
}

static void nes_mapper_state_reapply(nes_t* nes) {
    mapper132_update_state(nes);
}

int nes_mapper132_init(nes_t* nes) {
    nes->nes_mapper.mapper_init          = nes_mapper_init;
    nes->nes_mapper.mapper_deinit        = nes_mapper_deinit;
    nes->nes_mapper.mapper_write         = nes_mapper_write;
    nes->nes_mapper.mapper_apu           = nes_mapper_apu;
    nes->nes_mapper.mapper_read_apu      = nes_mapper_read_apu;
    nes->nes_mapper.mapper_state_reapply = nes_mapper_state_reapply;
    return NES_OK;
}
