#include "platform/spi_bus.h"
#include "platform/board.h"
#include "display/st7796.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"

#define BUS_SPI spi1

// Enable the SPI1 SSP and route the shared SCK/MOSI to it (12 mA, matching the
// display's proven drive). GPIO8 (LCD_DC / CC1101 MISO) is muxed per-transaction by
// the arbiter, not here. Idempotent: st7796_init() also does this, so a display app
// simply re-inits the SSP to the same config — harmless, nothing is mid-transfer.
void spi_bus_init(void) {
    spi_init(BUS_SPI, LCD_SPI_BAUD);                     // enable SSP; 8-bit mode-0 MSB-first
    gpio_set_function(PIN_LCD_SCLK, GPIO_FUNC_SPI);      // GPIO10 = SPI1 SCK
    gpio_set_function(PIN_LCD_MOSI, GPIO_FUNC_SPI);      // GPIO11 = SPI1 TX
    gpio_set_drive_strength(PIN_LCD_SCLK, GPIO_DRIVE_STRENGTH_12MA);
    gpio_set_drive_strength(PIN_LCD_MOSI, GPIO_DRIVE_STRENGTH_12MA);
}

// GPIO 8 is shared: it is the LCD DC line (an OUTPUT, driven by the display) by
// default, and the CC1101's MISO/SO (an INPUT, SPI1 RX) while the radio is on the
// bus. The arbiter switches the pin's function around every CC1101 transaction:
// SPI RX (input) before chip-select, back to SIO output (DC) on release.

void spi_bus_cc1101_cs(bool select) { gpio_put(PIN_CC1101_CS, select ? 0 : 1); }

void spi_bus_acquire_cc1101(void) {
    while (st7796_flush_busy()) tight_loop_contents();   // never interrupt an LCD DMA flush
    gpio_set_function(PIN_CC1101_MISO, GPIO_FUNC_SPI);   // GPIO 8 -> SPI1 RX (input from CC1101 SO)
    spi_set_baudrate(BUS_SPI, CC1101_SPI_BAUD);          // LCD runs fast; CC1101 is limited
    gpio_put(PIN_CC1101_CS, 1);                          // ensure CC1101 deselected before we start
    // The LCD driver does write-only transfers and never drains the RX FIFO, so it
    // holds stale bytes; flush them or the radio's first reads return LCD garbage.
    while (spi_is_readable(BUS_SPI)) (void)spi_get_hw(BUS_SPI)->dr;
}

void spi_bus_release_cc1101(void) {
    gpio_put(PIN_CC1101_CS, 1);                          // deselect radio
    gpio_set_function(PIN_LCD_DC, GPIO_FUNC_SIO);        // GPIO 8 back to DC OUTPUT for the LCD
    gpio_set_dir(PIN_LCD_DC, GPIO_OUT);
    spi_set_baudrate(BUS_SPI, LCD_SPI_BAUD);             // hand the bus back to the LCD
}
