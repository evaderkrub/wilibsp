// bsp/platform/ioexp.c — minimal PCAL6524 bring-up for the FreeWili2 display board.
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

// Shadow of the Port-0 output byte: SPI1 buffer dirs + USB host port 1 power.
// Default matches ioexp_init(): HP1 OFF.
#define P0_HP1_EN 0x01   // P0 bit 0: USB host port 1 power, active-high
static uint8_t s_p0 = P0_OUT;

// Shadow of the Port-2 output byte. Default matches ioexp_init(): IR power OFF.
#define P2_IR_PWR 0x01  // P2 bit 0, active-high IR power (pin table: sensorview ioexp_pcal6524.h)
#define P2_USB_DPLUS 0x02  // P2 bit 1, active-high: USB DEVICE D+ 1.5K pull-up
static uint8_t s_p2 = 0x00;

static void write_outputs(uint8_t p0, uint8_t p1) {
    uint8_t out[4] = { REG_OUTPUT0, p0, p1, s_p2 };
    i2c_write_blocking(IOEXP_I2C, IOEXP_ADDR, out, 4, false);
}

// Map an ANT_* value to the V1_1/V2_1 output bits within Port 1.
static uint8_t ant_bits(uint8_t sel) {
    return (uint8_t)(((sel & 1u) ? 0x08u : 0u) | ((sel & 2u) ? 0x02u : 0u));
}

// Shadow of the Port-1 output byte: base control bits + antenna select + MIC_PWR.
// Default matches ioexp_init(): CC1101 433 antenna, mic power OFF.
#define P1_MIC_PWR 0x80   // P1 bit 7, active-high mic rail
#define P1_HP2_EN 0x10    // P1 bit 4: USB host port 2 power, active-high
#define P1_ANT_MASK 0x0A  // V1_1 (bit3) | V2_1 (bit1)
static uint8_t s_p1 = P1_BASE | 0x08;   // == P1_BASE | ant_bits(ANT_CC1101_433)

void ioexp_antenna(uint8_t sel) {
    s_p1 = (uint8_t)((s_p1 & (uint8_t)~P1_ANT_MASK) | ant_bits(sel));
    write_outputs(s_p0, s_p1);
}

void ioexp_mic_pwr(bool on) {
    s_p1 = on ? (uint8_t)(s_p1 | P1_MIC_PWR) : (uint8_t)(s_p1 & (uint8_t)~P1_MIC_PWR);
    write_outputs(s_p0, s_p1);
    DIAG("ioexp: MIC_PWR (P1_7) -> %d\n", on ? 1 : 0);
}

void ioexp_ir_pwr(bool on) {
    s_p2 = on ? (uint8_t)(s_p2 | P2_IR_PWR) : (uint8_t)(s_p2 & (uint8_t)~P2_IR_PWR);
    write_outputs(s_p0, s_p1);
    DIAG("ioexp: IR_PWR (P2_0) -> %d\n", on ? 1 : 0);
}

void ioexp_usb_pwr(bool on) {
    s_p0 = on ? (uint8_t)(s_p0 | P0_HP1_EN) : (uint8_t)(s_p0 & (uint8_t)~P0_HP1_EN);
    s_p1 = on ? (uint8_t)(s_p1 | P1_HP2_EN) : (uint8_t)(s_p1 & (uint8_t)~P1_HP2_EN);
    write_outputs(s_p0, s_p1);
    DIAG("ioexp: USB HP1(P0_0)+HP2(P1_4) -> %d\n", on ? 1 : 0);
}

void ioexp_usb_dplus(bool on) {
    s_p2 = on ? (uint8_t)(s_p2 | P2_USB_DPLUS) : (uint8_t)(s_p2 & (uint8_t)~P2_USB_DPLUS);
    write_outputs(s_p0, s_p1);   // write_outputs always emits the current s_p2
    DIAG("ioexp: USB D+ pull-up (P2_1) -> %d\n", on ? 1 : 0);
}

bool ioexp_init(void) {
    // Default to the CC1101 + 433 MHz antenna (keeps the CC1101 on SPI1 for
    // cc1101_init); main() then auto-selects the antenna per band.
    s_p0 = P0_OUT;                                          // HP1 (USB) power OFF
    s_p1 = (uint8_t)(P1_BASE | ant_bits(ANT_CC1101_433));   // mic + HP2 (USB) power OFF
    s_p2 = 0x00;                                            // IR power OFF
    uint8_t out[4] = { REG_OUTPUT0, s_p0, s_p1, s_p2 };
    uint8_t cfg[4] = { REG_CONFIG0, 0x00, 0x00, 0x04 };   // all output except MCLR (P2_2)
    // Outputs FIRST, then directions (glitch-free: a pin drives its latched value
    // only when its direction flips to output; keeps SCREEN_NRST from pulsing low).
    bool ok = i2c_write_blocking(IOEXP_I2C, IOEXP_ADDR, out, 4, false) == 4;
    ok = (i2c_write_blocking(IOEXP_I2C, IOEXP_ADDR, cfg, 4, false) == 4) && ok;
    DIAG("ioexp: init %s (LCD reset released; CC1101 + 433 MHz antenna default)\n", ok ? "ok" : "NAK");
    return ok;
}
