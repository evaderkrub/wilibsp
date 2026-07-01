// src/input/ft6336.h — FT6336U capacitive touch on I2C1, polled (no INT; RST shares
// the panel HW reset). Mirrors the proven freewili reference driver
// (rmpLib/rpCAPTouchFT6336): single 6-byte status read, DEVICE_MODE set at init, and
// coordinates returned already oriented to the 480x320 landscape panel.
#ifndef FT6336_H
#define FT6336_H
#include <stdint.h>
#include <stdbool.h>

// The I2C bus must already be up (board_i2c1_init brings up i2c1 @ 400 kHz). Reads the
// chip-ID to confirm presence and writes DEVICE_MODE=normal. Returns true on ACK.
bool ft6336_init(void);

// Poll for a single touch. On exactly one finger down, writes SCREEN coordinates
// (x: 0..479, y: 0..319 — already oriented to the panel, matching the reference driver)
// and returns true; returns false when nothing (or more than one point) is touched.
bool ft6336_poll(uint16_t* x, uint16_t* y);

#endif // FT6336_H
