#ifndef FT6336_MAP_H
#define FT6336_MAP_H
#include <stdint.h>
typedef struct { uint16_t x, y; } ft_point_t;
// Map raw FT6336 chip coordinates to screen pixels (480x320 landscape),
// clamped to the panel. chip-Y -> screen X; chip-X -> screen Y (inverted).
ft_point_t ft6336_map_point(int chip_x, int chip_y);
#endif
