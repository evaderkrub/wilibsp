# hello_ir

On-hardware smoke test for the harvested IR engine (`bsp/ir`): every 5 s it
encodes and transmits one NEC frame (A:0x04 C:0x08) on the on-board TX LED,
and the on-board TSOP receiver hears it, so the capture -> decode path
reports the same frame back over RTT — a zero-equipment TX->RX loopback
(any external remote pointed at the receiver decodes live too).

    fw build hello_ir
    fw flash hello_ir
    fw rtt

Pass criteria: `tx: sent` alternating with `rx: ... NEC A:0x4 C:0x8` every
5 s, a real remote press prints its own decode line, and `ovr` (capture
overrun count) stays 0 throughout.
