/* toggleled — blink MAIN-CPU GPIO 25 from the display CPU over the FwGUI
 * link (UART0 @ 8 Mbaud, HW flow control on GPIO 0-3). The main CPU must
 * run the stock FreeWili 2 firmware, which carries the OneWili bridge. */
#include "fw2.h"
#include "platform/diag.h"
#include "pico/stdlib.h"
#include "onewili.h"
#include "onewili_fwgui.h"

int main(void) {
    board_init();   /* must precede ow_open_fwgui: uart_init reads clk_peri */

    static ow_device dev;   /* ~37 KB of buffers - far too big for the 2 KB stack */
    /* ow_open_fwgui currently cannot fail (it only sends the reset byte,
     * no handshake) - this loop future-proofs a smarter open. A missing
     * bridge surfaces later as toggle timeouts on the DIAG below. */
    while (ow_open_fwgui(&dev) != OW_OK) {
        DIAG("toggleled: FwGUI link open failed (is the main CPU running stock fw?), retry in 1 s\n");
        sleep_ms(1000);
    }
    DIAG("toggleled: link up, toggling main-CPU GPIO 25 every 500 ms\n");

    for (;;) {
        ow_status s = ow_io_gpio_set_io_toggle(&dev, 25);
        if (s != OW_OK)
            DIAG("toggleled: toggle failed, status %d\n", (int)s);
        sleep_ms(500);
    }
}
