// src/platform/ioexp.c — minimal PCAL6524 bring-up for the FreeWili2 display board.
// See ioexp.h.
//
// The CC1101 shares SPI1 SCLK/MOSI/MISO with the LCD (MISO is GPIO8, muxed by the
// spi_bus arbiter between LCD DC-out and SPI RX-in) and is chip-selected on GPIO40.
// The expander's radio job is the antenna/radio select (V1_1/V2_1): a 2-bit value
// routes 0=LoRa / 1=CC1101-433 / 2=CC1101-315-415 / 3=CC1101-915 (see ioexp.h).
// ioexp_antenna() drives it; main() auto-selects per band.
#include "platform/ioexp.h"
#include "platform/board.h"
#include "platform/diag.h"
#include "hardware/i2c.h"

#define IOEXP_I2C   i2c1
#define IOEXP_ADDR  0x23
#define REG_OUTPUT0 0x04   // output ports 0x04..0x06 (auto-increment)
#define REG_CONFIG0 0x0C   // direction ports 0x0C..0x0E (1 = input)

// Port-1 output base: bit2=SCREEN_NRST, bit5=GPIO25_RP_DIR, bit6=I2C_PULL.
#define P1_BASE   (0x04 | 0x20 | 0x40)   // = 0x64
// Antenna/radio select. Per the FreeWili2 schematic V1_1 (P1 bit3) and V2_1
// (P1 bit1) form a 2-bit selector: 0=LoRa, 1=CC1101 433, 2=CC1101 315/415,
// 3=CC1101 915. Values 1..3 keep the CC1101 on SPI1. V1_1 = value bit0,
// V2_1 = value bit1. At power-on the expander is high-Z, so nothing is routed
// until ioexp_init() drives it.
// Port-0 bits 4..7 = SPI1 buffer directions (TX/RX/CS/SCLK), driven 1 (display-
// verified). The CC1101 MISO direction is handled on the RP2350 side: the spi_bus
// arbiter switches GPIO8 between DC output and SPI RX input per transaction.
#define P0_OUT    0xF0
#define P2_OUT    0x00

static void write_outputs(uint8_t p0, uint8_t p1) {
    uint8_t out[4] = { REG_OUTPUT0, p0, p1, P2_OUT };
    i2c_write_blocking(IOEXP_I2C, IOEXP_ADDR, out, 4, false);
}

// Map an ANT_* value to the V1_1/V2_1 output bits within Port 1.
static uint8_t ant_bits(uint8_t sel) {
    return (uint8_t)(((sel & 1u) ? 0x08u : 0u) | ((sel & 2u) ? 0x02u : 0u));
}

void ioexp_antenna(uint8_t sel) {
    write_outputs(P0_OUT, (uint8_t)(P1_BASE | ant_bits(sel)));
}

bool ioexp_init(void) {
    // Default to the CC1101 + 433 MHz antenna (keeps the CC1101 on SPI1 for
    // cc1101_init); main() then auto-selects the antenna per band.
    uint8_t out[4] = { REG_OUTPUT0, P0_OUT,
                       (uint8_t)(P1_BASE | ant_bits(ANT_CC1101_433)), P2_OUT };
    uint8_t cfg[4] = { REG_CONFIG0, 0x00, 0x00, 0x04 };   // all output except MCLR (P2_2)
    // Outputs FIRST, then directions (glitch-free: a pin drives its latched value
    // only when its direction flips to output; keeps SCREEN_NRST from pulsing low).
    bool ok = i2c_write_blocking(IOEXP_I2C, IOEXP_ADDR, out, 4, false) == 4;
    ok = (i2c_write_blocking(IOEXP_I2C, IOEXP_ADDR, cfg, 4, false) == 4) && ok;
    DIAG("ioexp: init %s (LCD reset released; CC1101 + 433 MHz antenna default)\n", ok ? "ok" : "NAK");
    return ok;
}
