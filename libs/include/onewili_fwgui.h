/* FwGUI display-link transport for the OneWili C API (WiliBSP / RP2350B).
 * Talks to the FreeWili 2 main CPU over UART0 (Rx=GPIO0, Tx=GPIO1,
 * RTS=GPIO2, CTS=GPIO3) at 8,000,000 baud with hardware flow control.
 * Single link per board: the state behind these transports is static. */
#ifndef ONEWILI_FWGUI_H
#define ONEWILI_FWGUI_H
#include "onewili.h"

/* Inits UART0 + pins, installs the command transport, and calls ow_open
 * (which performs the 0x02 reset handshake). Call once at startup. */
ow_status ow_open_fwgui(ow_device* dev);

/* The binary-event stream (FWGUI_API_ONEWILL_BINARY frames) as a transport
 * for ow_binary_open/ow_binary_poll. Valid after ow_open_fwgui. */
ow_transport ow_fwgui_binary_transport(void);

/* Frames dropped because a stream buffer was full (poll more often). */
uint32_t ow_fwgui_dropped_frames(void);

#endif /* ONEWILI_FWGUI_H */
