#ifndef SPI_BUS_H
#define SPI_BUS_H
#include <stdbool.h>
// Arbitrates the shared SPI1 bus between the LCD (owner by default) and the CC1101.
// Acquire spins until the display DMA flush is idle, then reconfigures the bus for
// the radio; release restores the LCD configuration. CS is driven separately so a
// caller can hold the bus across a short multi-byte burst.
void spi_bus_acquire_cc1101(void);
void spi_bus_release_cc1101(void);
void spi_bus_cc1101_cs(bool select);   // true = CS low (selected)
#endif
