#pragma once

/* The benchmark writes its own CSV records; core diagnostic logs would make
 * the output harder to parse and would affect the measured work. */
#define NES_LOG_LEVEL 0

int nes_log_printf(const char* format, ...);
