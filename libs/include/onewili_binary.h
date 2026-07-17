/* Binary (FTDI/WILI) event transport for the OneWili C API.
 * The binary port is a plain serial port (see serial_pc.h); pass its
 * ow_transport here. Find it by FTDI identity (VID 0x0403) - NEVER by the
 * "FW2" name substring: the main text port's product string is "FW2 v01". */
#ifndef ONEWILI_BINARY_H
#define ONEWILI_BINARY_H
#include "onewili.h"
#include "onewili_binary_framing.h"
#include "onewili_events.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct ow_binary_device {
    ow_transport t;               /* binary-port transport */
    ow_bin_parser parser;
    uint8_t  rx[512];
    uint32_t rx_pos, rx_len;      /* unparsed remainder of the last read */
    uint32_t unknown_frames, size_mismatches;
} ow_binary_device;

ow_status ow_binary_open(ow_binary_device* bdev, const ow_transport* transport);
void      ow_binary_close(ow_binary_device* bdev);

/* Non-blocking pump: zero-timeout read -> WILI parser -> decoder table.
 * Unknown/size-mismatched frames are counted and skipped within the call.
 * Returns 1 = *out filled, 0 = no complete event, negative = -(ow_status). */
int ow_binary_poll(ow_binary_device* bdev, ow_event* out);

#ifdef __cplusplus
}
#endif
#endif /* ONEWILI_BINARY_H */
