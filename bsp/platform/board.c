// src/platform/board.c
// Adapted from evaderkrub/usbcamfw — MIT, (c) 2026 Dave Robins.
#include "platform/board.h"
#include "platform/ioexp.h"
#include "platform/spi_bus.h"
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "hardware/vreg.h"

void board_init(void) {
    // Raise the core voltage before overclocking. The earlier 252 MHz fault was
    // marginal Vcore at 1.15 V during the heavy st7796 bring-up; the firmware runs
    // from RAM (copy_to_ram) so flash XIP timing doesn't cap clk_sys. 1.25 V gives
    // solid headroom for the overclock below.
    vreg_set_voltage(VREG_VOLTAGE_1_25);
    sleep_ms(10);
    set_sys_clock_khz(BOARD_SYS_CLOCK_KHZ, true);

    // After overclocking sys, re-source the peripheral clock from clk_sys so the
    // hardware SPI peripheral has a valid clock. WITHOUT this the SPI clock is dead
    // and the LCD shows nothing — the working reference driver does exactly this.
    uint32_t f = clock_get_hz(clk_sys);
    clock_configure(clk_peri, 0, CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS, f, f);

    // Bring up the shared SPI1 bus now that clk_peri is live, so BOTH the display
    // and a radio-only app (which never calls st7796_init) find the SSP enabled.
    // Without this, cc1101 SPI reads spin forever on a disabled peripheral.
    spi_bus_init();

    // Park the CC1101 radio's SPI CS high before any LCD traffic so it never
    // drives lines shared with the LCD.
    gpio_init(PIN_CC1101_CS);
    gpio_set_dir(PIN_CC1101_CS, GPIO_OUT);
    gpio_put(PIN_CC1101_CS, 1);

    // Backlight as a plain GPIO, matching the working reference (its PWM path is
    // disabled). PWM dimming returns in a later phase — one fewer bring-up variable.
    gpio_init(PIN_LCD_BL);
    gpio_set_dir(PIN_LCD_BL, GPIO_OUT);
    board_backlight_set(0);   // start dark; on after display init pushes a frame

    // I2C1 for touch (FT6336U), the PCAL6524 I/O expander, and sensors.
    board_i2c1_init();

    // PCAL6524: release LCD reset, enable I2C pulls, route the backlight to the
    // RP2350, set SPI1 buffer directions, and SELECT the CC1101 + sub-GHz antenna
    // (V1_1/V2_1). The CC1101 is off the shared bus until this runs.
    ioexp_init();
}

void board_backlight_set(uint8_t level) {
    gpio_put(PIN_LCD_BL, level != 0);
}

void board_i2c1_init(void) {
    i2c_init(i2c1, 400 * 1000);
    gpio_set_function(PIN_I2C1_SDA, GPIO_FUNC_I2C);
    gpio_set_function(PIN_I2C1_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(PIN_I2C1_SDA);
    gpio_pull_up(PIN_I2C1_SCL);
}
