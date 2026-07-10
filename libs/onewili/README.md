# OneWili C API for WiliBSP

The full FreeWili OneWili command API for firmware running on the FreeWili 2
**display CPU** (WiliBSP, RP2350B). The library is the same generated C
package the PC serial target uses; only the transport differs: commands
travel to the main CPU over the FwGUI display link (UART0, 8 Mbaud, hardware
flow control on GPIO0-3) and responses/events travel back on the same link.

The main CPU must run the default FreeWili 2 firmware (which carries the
OneWili display bridge).

## Use

```c
#include "onewili.h"
#include "onewili_fwgui.h"

ow_device dev;
ow_open_fwgui(&dev);                 /* UART0 + link handshake */
/* any generated call, e.g.: */
ow_io_gpio_set_io_toggle(&dev, 25);  /* toggles a MAIN-CPU gpio */
```

Text events arrive in-band — poll with `ow_poll_text_line(&dev, ...)`.
Binary events (`onewili_binary.h`):

```c
ow_binary_device bdev;
ow_transport bt = ow_fwgui_binary_transport();
ow_binary_open(&bdev, &bt);
ow_event ev;
while (ow_binary_poll(&bdev, &ev) == 1) { /* ... */ }
```

Poll events regularly: each stream buffers 1024 bytes and whole frames are
dropped (counted by `ow_fwgui_dropped_frames()`) when a buffer is full.
Logic-analyzer binary reports are never mirrored over the display link.

## Build

`CMakeLists.txt` builds a `onewili_fwgui` static library against the
pico-sdk. Link it from your wilibsp app target and add `include/` (PUBLIC,
automatic via CMake). `examples/blink.c` is a minimal app body.
