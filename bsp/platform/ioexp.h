// bsp/platform/ioexp.h — PCAL6524 I/O expander on I2C1 (addr 0x23).
//
// On the FreeWili2 the sub-GHz section has TWO radios (CC1101 + LoRa) selected by
// the expander pins V1_1 and V2_1: driving BOTH high routes the CC1101 + sub-GHz
// antenna onto the shared SPI1 bus (verified on-target). At power-on the expander
// pins are high-Z, so the CC1101 is off the bus until this runs. The CC1101 shares
// SPI1 with the LCD; its MISO is GPIO8 (muxed with LCD DC by the spi_bus arbiter)
// and its chip-select is GPIO40.
//
// ioexp_init() also releases the LCD reset (SCREEN_NRST), enables the I2C bus
// pulls, routes the backlight (GPIO25) to the RP2350, and sets the SPI1 bus
// buffer directions. Call after I2C1 is up and before using the display or radio.
#ifndef IOEXP_H
#define IOEXP_H
#include <stdbool.h>
#include <stdint.h>

// Antenna select via the expander pins V1_1 (P1 bit3) + V2_1 (P1 bit1). Per the
// FreeWili2 schematic the 2-bit value routes one of four antennas. Values 1..3
// keep the CC1101 on SPI1; value 0 routes the LoRa path.
enum {
    ANT_LORA            = 0,   // LoRa radio + LoRa antenna
    ANT_CC1101_433      = 1,   // CC1101 + 433 MHz antenna
    ANT_CC1101_315_415  = 2,   // CC1101 + 315/415 MHz antenna
    ANT_CC1101_915      = 3,   // CC1101 + 915 MHz antenna
};

bool ioexp_init(void);         // returns true on I2C ACK; defaults to the CC1101 433 antenna
void ioexp_antenna(uint8_t sel);   // route one of ANT_* (drives V1_1/V2_1)
// MIC_PWR (P1 bit 7, active-high): power rail for the 4 PDM MEMS microphones.
// Off at power-on and after ioexp_init(); pdm_capture_init() turns it on and
// waits ~50 ms for the mics to settle. Preserves the antenna-select bits.
void ioexp_mic_pwr(bool on);
// IR_PWR (P2 bit 0, active-high): power rail for the IR receiver (and possibly
// the TX LED driver). Off at power-on and after ioexp_init(); ir_capture_init()
// turns it on. Pin table: sensorview ioexp_pcal6524.h (IOEXP_IR_PWR = P2_0).
void ioexp_ir_pwr(bool on);
// USB host port power (CH334F hub rails): HP1 = P0 bit 0, HP2 = P1 bit 4,
// both active-high (pin table: sensorview ioexp_pcal6524.h). Off at power-on;
// usb_store_init() turns both on before enumeration.
void ioexp_usb_pwr(bool on);
// USB DEVICE D+ 1.5K pull-up enable (PCAL6524 P2 bit 1, active-high). Distinct
// from ioexp_usb_pwr() (the CH334F HOST hub port power). Off after ioexp_init()
// (P2_1 is configured as an output driven low = device detached); the PIO-USB
// device firmware asserts it AFTER its stack is ready to signal USB attach.
void ioexp_usb_dplus(bool on);
#endif
