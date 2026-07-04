#ifndef SPI_BUS_H
#define SPI_BUS_H
#include <stdbool.h>
// Bring up the shared SPI1 peripheral: enable the SSP and route SCK/MOSI to it.
// The bus is a PLATFORM resource shared by the LCD and the CC1101, so board_init()
// calls this once — every SPI1 consumer (display OR radio-only app) then finds the
// peripheral live. The acquire/release arbiter below only re-baudrates and muxes the
// shared GPIO8; it assumes the SSP is already enabled by this.
void spi_bus_init(void);

// Arbitrates the shared SPI1 bus between the LCD (owner by default) and the CC1101.
// Acquire spins until the display DMA flush is idle, then reconfigures the bus for
// the radio; release restores the LCD configuration. CS is driven separately so a
// caller can hold the bus across a short multi-byte burst.
void spi_bus_acquire_cc1101(void);
void spi_bus_release_cc1101(void);
void spi_bus_cc1101_cs(bool select);   // true = CS low (selected)
#endif
