// src/platform/psram.h — APS6404L (8 MB) on the RP2350 QMI/XIP M1 window.
// Adapted from evaderkrub/usbcamfw. Memory-mapped at PSRAM_BASE after psram_init().
#ifndef PSRAM_H
#define PSRAM_H
#include <stddef.h>

#define PSRAM_BASE 0x11000000u   // M1 (XIP CS1) memory-mapped base

// Bring up the APS6404L in QPI and map it at PSRAM_BASE. Call AFTER clk_sys is at
// its final frequency. Returns the detected size in bytes, or 0 if not present.
size_t psram_init(void);

// Walking-value RAM test over the first test_bytes. Returns 1 on pass, 0 on mismatch.
int psram_selftest(size_t test_bytes);

#endif
