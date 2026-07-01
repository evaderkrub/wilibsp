/*
 * FreeWili 2 board header for the Raspberry Pi Pico SDK.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

// -----------------------------------------------------
// NOTE: THIS HEADER IS ALSO INCLUDED BY ASSEMBLER SO
//       SHOULD ONLY CONSIST OF PREPROCESSOR DIRECTIVES
// -----------------------------------------------------

#ifndef _BOARDS_FREEWILI2_H
#define _BOARDS_FREEWILI2_H

pico_board_cmake_set(PICO_PLATFORM, rp2350)

// For board detection
#define FREEWILI2

// --- RP2350 VARIANT ---
// FreeWili 2 uses the RP2350B (48-GPIO package, not the 30-GPIO "A" variant).
// Setting PICO_RP2350A=0 causes the SDK to set NUM_BANK0_GPIOS=48.
// See: sdk/src/rp2350/hardware_regs/include/hardware/platform_defs.h
#define PICO_RP2350A 0

// --- FLASH ---
// 16 MB external QSPI flash.
#define PICO_BOOT_STAGE2_CHOOSE_W25Q080 1

#ifndef PICO_FLASH_SPI_CLKDIV
#define PICO_FLASH_SPI_CLKDIV 2
#endif

pico_board_cmake_set_default(PICO_FLASH_SIZE_BYTES, (16 * 1024 * 1024))
#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (16 * 1024 * 1024)
#endif

// --- UART (stdio) ---
// USB is host-mode on FreeWili 2, so stdio goes over UART.
// GPIO0/GPIO1 are the SDK default UART0 TX/RX.
// TODO(hardware): These pins are UNVERIFIED / TBD — confirm which GPIOs the
//                 on-board debug-probe UART connects to before final firmware.
#ifndef PICO_DEFAULT_UART
#define PICO_DEFAULT_UART 0
#endif
#ifndef PICO_DEFAULT_UART_TX_PIN
#define PICO_DEFAULT_UART_TX_PIN 0
#endif
#ifndef PICO_DEFAULT_UART_RX_PIN
#define PICO_DEFAULT_UART_RX_PIN 1
#endif

// --- LED ---
// No default LED: GPIO25 is the LCD backlight on FreeWili 2, not an LED.

pico_board_cmake_set_default(PICO_RP2350_A2_SUPPORTED, 1)
#ifndef PICO_RP2350_A2_SUPPORTED
#define PICO_RP2350_A2_SUPPORTED 1
#endif

#endif // _BOARDS_FREEWILI2_H
