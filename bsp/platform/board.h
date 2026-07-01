// src/platform/board.h — pin map + board bring-up for the FreeWili2 (RP2350B) +
// 480x320 ST7789-class panel. Adapted from evaderkrub/usbcamfw — MIT, (c) 2026 Dave Robins.
// There is NO LCD reset GPIO on this board — the panel RESX is hardware-handled,
// so the driver relies on SWRESET only.
#ifndef BOARD_H
#define BOARD_H
#include <stdint.h>

// --- LCD (ST7789-class panel, 480x320, over SPI1) ---
#define PIN_LCD_DC     8    // LCD_DC_D: data/command (also CC1101 MISO — see PIN_CC1101_MISO)
#define PIN_LCD_CS     9    // LCD_CS_D: chip select, active low
#define PIN_LCD_SCLK   10   // LCD_SCLK_D: SPI1 SCK
#define PIN_LCD_MOSI   11   // LCD_MOSI_D: SPI1 TX
#define PIN_LCD_TE     33   // LCD_TE: tearing-effect output (unused)
#define PIN_LCD_BL     25   // backlight: plain on/off

// --- CC1101 sub-GHz radio (shares SPI1 with the LCD) ---
#define PIN_CC1101_CS    40  // CC1101 chip-select (active low); park HIGH before LCD traffic
#define PIN_CC1101_MISO  8   // == PIN_LCD_DC. This pin is an OUTPUT (DC) for the LCD and
                             // must be switched to SPI1 RX (input) around CC1101 access.
#define PIN_CC1101_GDO0  32  // live data / sync (PIO-sampled, Plan 3/5)
#define PIN_CC1101_GDO2  37

// --- WS2812 RGB LEDs (16 pixels, single data line) ---
#define PIN_LED_DATA   21   // WS2812 data; driven by a pio1 state machine

// --- I2C1 (touch, sensors) ---
#define PIN_I2C1_SDA   26
#define PIN_I2C1_SCL   27

// --- External PSRAM (APS6404L 8MB) on the QSPI/QMI second chip select ---
// GPIO47 = XIP_CS1n (function 9), NOT on SPI1. Brought up + memory-mapped in Plan 4.
#define PIN_PSRAM_CS   47

// --- DMA-IRQ ownership model ---
// DMA_IRQ_0 is a SHARED handler line. The ST7796 display flush registers a
// shared handler (via irq_add_shared_handler) that acts only on its own DMA
// channel. Any future radio/PIO DMA (Plan 3/5) must likewise use
// irq_add_shared_handler(DMA_IRQ_0, ...) and guard on its own channel status.
// Do NOT use irq_set_exclusive_handler on DMA_IRQ_0.

// --- SPI1 baud rates (shared bus). LCD runs fast; CC1101 is limited. ---
#define LCD_SPI_BAUD     (100u * 1000u * 1000u)   // 100 MHz (divider-limited by clk_peri)
#define CC1101_SPI_BAUD  (5u * 1000u * 1000u)     // 5 MHz (CC1101 SPI ceiling ~6.5 MHz)

// --- Clocks: overclock to 250 MHz @ vreg 1.25 V, run from RAM (copy_to_ram). ---
#define BOARD_SYS_CLOCK_KHZ 250000

// Bring up clocks (250 MHz + vreg 1.25V + clk_peri re-source), park CC1101 CS, backlight off,
// and initialises I2C1 @ 400 kHz on GPIO 26/27.
void board_init(void);

// Backlight: 0 = off, nonzero = on (plain GPIO).
void board_backlight_set(uint8_t level);

// I2C1 @ 400 kHz on GPIO 26 (SDA) / 27 (SCL). Called from board_init().
void board_i2c1_init(void);

#endif
