#ifndef PALETTE_H
#define PALETTE_H
#include <stdint.h>
// RSSI (dBm) -> RGB565 (native), Inferno ramp over [-100, -20], clamped.
// Floor anchored at the measured ~-100 dBm noise floor so noise reads dark.
uint16_t inferno_rgb565(int rssi_dbm);
#endif
