/* Blink a MAIN-CPU GPIO from the display CPU over the FwGUI link. */
#include "onewili.h"
#include "onewili_fwgui.h"
#include "pico/stdlib.h"

int main(void) {
    ow_device dev;
    if (ow_open_fwgui(&dev) != OW_OK) return 1;
    for (;;) {
        ow_io_gpio_set_io_toggle(&dev, 25);
        sleep_ms(500);
    }
}
