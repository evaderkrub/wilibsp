// src/display/font5x7.h — minimal 5x7 ASCII font for status text (column-major,
// bit 0 = top row). Glyphs for chars 0x20..0x5F; lowercase maps to uppercase.
#ifndef FONT5X7_H
#define FONT5X7_H
#include <stdint.h>

#define FONT5X7_FIRST 0x20
#define FONT5X7_LAST  0x5F

extern const uint8_t font5x7[FONT5X7_LAST - FONT5X7_FIRST + 1][5];

#endif
