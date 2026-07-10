#include "onewili_binary.h"
#include <string.h>

ow_status ow_binary_open(ow_binary_device* bdev, const ow_transport* transport) {
    if (!bdev || !transport || !transport->read) return OW_ERR_ARG;
    memset(bdev, 0, sizeof *bdev);
    bdev->t = *transport;
    ow_bin_parser_init(&bdev->parser);
    return OW_OK;
}

void ow_binary_close(ow_binary_device* bdev) {
    (void)bdev;   /* the caller owns the port behind the transport */
}

int ow_binary_poll(ow_binary_device* bdev, ow_event* out) {
    if (!bdev || !out || !bdev->t.read) return -(int)OW_ERR_ARG;
    for (;;) {
        while (bdev->rx_pos < bdev->rx_len) {
            ow_bin_frame f;
            int ready = 0;
            size_t i;
            bdev->rx_pos += (uint32_t)ow_bin_parser_feed(
                &bdev->parser, bdev->rx + bdev->rx_pos,
                bdev->rx_len - bdev->rx_pos, &f, &ready);
            if (!ready) continue;
            for (i = 0; i < ow_event_decoder_count; ++i) {
                const ow_event_decoder* d = &ow_event_decoders[i];
                if (d->header_type != f.header_type) continue;
                if (d->payload_size != f.payload_len ||
                    d->decode(f.payload, f.payload_len, f.error, out) != OW_OK) {
                    ++bdev->size_mismatches;
                    break;
                }
                return 1;
            }
            if (i == ow_event_decoder_count) ++bdev->unknown_frames;
        }
        bdev->rx_pos = bdev->rx_len = 0;
        {
            int n = bdev->t.read(bdev->t.ctx, bdev->rx, sizeof bdev->rx, 0);
            if (n == 0) return 0;
            if (n < 0) return -(int)OW_ERR_IO;
            bdev->rx_len = (uint32_t)n;
        }
    }
}
