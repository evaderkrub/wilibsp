// hello_ir — on-hardware smoke test for the harvested IR engine (bsp/ir).
// RTT-only (fw rtt), no display. Every 5 s it encodes and transmits one NEC
// frame (A:0x04 C:0x08); the on-board TSOP receiver hears the on-board TX
// LED, so the capture->decode path reports it back: a zero-equipment
// loopback. Any external remote pointed at the receiver decodes live too.
// Pass criteria: "tx: sent" alternating with "rx: ... NEC A:0x4 C:0x8"
// every 5 s; a real remote press prints its own decode line; overruns 0.
#include "fw2.h"
#include "platform/diag.h"
#include "pico/stdlib.h"

int main(void) {
    board_init();
    DIAG("hello_ir: up\n");
    ir_capture_init();               // powers the IR rail (PCAL6524 P2_0)
    sleep_ms(5);                     // rail settle: no display bring-up here
                                     // to hide behind (see WiliIR harvest.md)
    ir_tx_init(38000);
    ir_capture_start();
    DIAG("hello_ir: capture running, NEC loopback every 5 s\n");

    static uint32_t durs[IR_MAX_TIMINGS];
    ir_frame_t frame;
    ir_message_t msg;
    absolute_time_t next_tx = make_timeout_time_ms(1000);

    while (true) {
        if (time_reached(next_tx) && !ir_tx_busy()) {
            const ir_message_t out = {IR_PROTO_NEC, 0x04, 0x08, false};
            uint32_t n = ir_encode(&out, durs, IR_MAX_TIMINGS);
            if (n && ir_tx_send(durs, n, 38000)) DIAG("tx: sent NEC A:0x4 C:0x8\n");
            next_tx = make_timeout_time_ms(5000);
        }
        if (ir_capture_poll(&frame)) {
            if (ir_decode(frame.durs, frame.count, &msg))
                DIAG("rx: %lu edges  %s A:0x%lX C:0x%lX%s  (ovr %lu)\n",
                     (unsigned long)frame.count, ir_protocol_name(msg.protocol),
                     (unsigned long)msg.address, (unsigned long)msg.command,
                     msg.repeat ? " rpt" : "",
                     (unsigned long)ir_capture_overruns());
            else
                DIAG("rx: %lu edges  RAW first=%lu,%lu,%lu\n",
                     (unsigned long)frame.count, (unsigned long)frame.durs[0],
                     (unsigned long)frame.durs[1], (unsigned long)frame.durs[2]);
        }
        sleep_ms(2);
    }
}
