#include "uartkbd.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"

/* On GPIO38/39 the plain UART function is UART1 CTS/RTS; the UART-AUX
 * function routes UART1 TX/RX here instead (RP2350 datasheet GPIO muxing). */
#define UARTKBD_UART   uart1
#define UARTKBD_TX_PIN 38
#define UARTKBD_RX_PIN 39
#define UARTKBD_BAUD   62500

static uartkbd_parser_t s_parser;

void uartkbd_init(void)
{
    uartkbd_parse_init(&s_parser);
    uart_init(UARTKBD_UART, UARTKBD_BAUD);
    gpio_set_function(UARTKBD_TX_PIN, GPIO_FUNC_UART_AUX);
    gpio_set_function(UARTKBD_RX_PIN, GPIO_FUNC_UART_AUX);
    uart_set_format(UARTKBD_UART, 8, 1, UART_PARITY_NONE);
    uart_set_hw_flow(UARTKBD_UART, false, false);
    uart_set_fifo_enabled(UARTKBD_UART, true);
}

void uartkbd_task(void)
{
    while (uart_is_readable(UARTKBD_UART))
        uartkbd_parse_byte(&s_parser, (uint8_t)uart_getc(UARTKBD_UART));
}

bool     uartkbd_next_event(uartkbd_event_t *ev) { return uartkbd_parse_next_event(&s_parser, ev); }
uint16_t uartkbd_buttons(void) { return uartkbd_parse_buttons(&s_parser); }
uint8_t  uartkbd_flags(void)   { return uartkbd_parse_flags(&s_parser); }
uint32_t uartkbd_frames(void)  { return uartkbd_parse_frames(&s_parser); }
uint32_t uartkbd_errors(void)  { return uartkbd_parse_errors(&s_parser); }
