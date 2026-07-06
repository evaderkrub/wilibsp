# IR (`bsp/ir/`)

IR remote-control receive/decode/transmit over unified microsecond timing
arrays: PIO2 edge-capture on RX, PIO2 carrier-modulated playback on TX, a
shared decoder/encoder set for ten common consumer-IR protocols, a burst
framer, and pure Flipper-format `.ir` file parse/write + directory-listing
sort. Harvested from `WiliIR` (`src/ir/` + `src/db/{ir_file,db_sort,ir_resolve}`),
hardware-verified there on 2026-07-05/06 and again here via `apps/hello_ir`
TX→RX loopback.

## What

- **Capture** (`ir_capture.{c,h}`, `ir_capture.pio`): free-running PIO2 SM0 +
  one endless-ring DMA channel measure how long `PIN_IR_RX` holds each level;
  `ir_capture_poll()` drains new edges into `ir_frame.c`'s burst builder and
  returns a complete frame once a gap ≥ the frame-gap threshold is seen.
- **Decode** (`ir_decode.h` + `ir_protocols.c`): dispatches a frame's duration
  array against all supported protocols — **NEC, NECext, Samsung32, RC5
  (incl. RC5X), RC6, SIRC/SIRC15/SIRC20, RCA, Kaseikyo** — and reports
  address/command/repeat in a single `ir_message_t`.
- **Encode + TX** (`ir_encode.h`, `ir_tx.{c,h}`, `ir_tx.pio`, `ir_tx_pack.c`):
  `ir_encode()` turns an `ir_message_t` back into a mark/space timing array;
  `ir_tx_send()` packs it into carrier-period FIFO words and plays it out
  `PIN_IR_TX` via PIO2 SM1 + a one-shot DMA streamer.
- **`.ir` files** (`ir_file.{c,h}`, `ir_resolve.{c,h}`): a line-based parser/
  writer for the Flipper `.ir` text format (parsed and raw entries) plus
  `ir_resolve()`, which turns a parsed entry into timings + carrier at a
  caller-supplied default carrier (raw entries use their own `frequency:`
  line, or the default when absent).
- **Directory sort** (`db_sort.{c,h}`): case-insensitive, dirs-first ordering
  for a FatFs-style directory listing — pure, no FatFs dependency itself.

## How (init → start → poll → decode; encode → send)

```c
#include "ir/ir_capture.h"
#include "ir/ir_decode.h"
#include "ir/ir_tx.h"
#include "ir/ir_encode.h"

ir_capture_init();                 // claims pio2 SM + DMA, powers IR rail
ir_tx_init(38000);                 // claims pio2 SM + DMA, default carrier
ir_capture_start();                // arm the capture ring

ir_frame_t frame;
if (ir_capture_poll(&frame)) {     // call every main-loop iteration
    ir_message_t msg;
    if (ir_decode(frame.durs, frame.count, &msg))
        /* msg.protocol / .address / .command */;
}

ir_message_t out = {IR_PROTO_NEC, 0x04, 0x08, false};
uint32_t durs[IR_MAX_TIMINGS];
uint32_t n = ir_encode(&out, durs, IR_MAX_TIMINGS);
if (n) ir_tx_send(durs, n, 38000);
```

(Copied from `WiliIR/docs/harvest.md` § "Usage snippet" — the reference
init→start→poll→decode / encode→send shape, unchanged by the harvest.)

## Dependencies

- **pio2 SM0** (capture) **+ SM1** (TX carrier modulator) — a consequence of
  init order (`ir_capture_init()` before `ir_tx_init()`), not a hardware
  requirement. pio0/pio1 are committed elsewhere in this repo (pio0 = audio
  I2S, pio1 = WS2812 LEDs), not free — but the real constraint is that
  **pio2 is shared with `bsp/radio/gdo_capture.c`** (radio GDO0 capture also
  runs on pio2). Instruction memory is tight: `gdo_capture` (11) +
  `ir_capture` (11) + `ir_tx` (9) = 31 of pio2's 32 instruction slots, 1
  free. **Init-order rule:** an app combining radio and IR must call
  `gdo_capture_init()` before `ir_capture_init()`/`ir_tx_init()` —
  `gdo_capture_init()`'s `pio_set_gpio_base(pio2, 16)` call must run before
  any other program occupies pio2's instruction memory, or the SDK call
  fails silently and radio ends up capturing the wrong pin (see
  `docs/hardware/facts.md` § "pio2 cohabitation: radio + IR"). No current
  app in this repo combines radio and IR.
- **2 DMA channels**, one per direction: an endless ring for capture, a
  one-shot streamer for TX duration words.
- **Polled, no IRQs** — matches the `gdo_capture`/`pdm_capture` precedent on
  this board. The main loop must call `ir_capture_poll()`; nothing here runs
  off a DMA or PIO interrupt.
- **`ioexp_ir_pwr()`** (PCAL6524 P2 bit 0, active-high, off at power-on) must
  be called — from `ir_capture_init()`, as it is today — before either state
  machine can see a live signal; skipping it leaves `PIN_IR_RX` permanently
  unpowered and stuck LOW (see `WiliIR/docs/hardware-notes.md` root cause 1).
  **Rail-settle caveat carried from `WiliIR/docs/harvest.md`:** `ir_capture_init()`
  applies no explicit settle delay after `ioexp_ir_pwr(true)` before arming
  the PIO/DMA — on WiliIR's board this worked only because display/LVGL
  bring-up (tens of ms) ran first and incidentally gave the rail time to
  stabilize. Any consumer that calls `ir_capture_init()` earlier in boot (a
  minimal test harness, no display bring-up first) should add an explicit
  settle delay rather than rely on that ordering. `apps/hello_ir` **is**
  that minimal no-display harness — RTT-only, no LVGL/display bring-up — and
  it adds an explicit `sleep_ms(5)` between `ir_capture_init()` and
  `ir_tx_init()` precisely because there's no display init to hide behind
  (see `apps/hello_ir/main.c`). This has been hardware-verified: six clean
  NEC TX→RX loopback pairs over a 20 s RTT window, `ovr 0` throughout (see
  `.superpowers/sdd/task-3-report.md`).

## Tests

11 host CTest binaries (MinGW GCC, no Pico SDK — pure C11 over `uint32_t`
timing arrays and `ir_message_t`/`ir_file_entry_t` structs): `test_ir_nec`,
`test_ir_pd` (shared pulse-distance decode), `test_ir_manchester` (RC5/RC6),
`test_ir_kaseikyo`, `test_ir_dispatch` (decoder dispatcher + ambiguity),
`test_ir_frame` (burst-to-frame gap logic), `test_ir_tx_pack`
(duration → PIO FIFO word packing), `test_ir_file` (`.ir` parser/writer
round-trips), `test_db_sort` (directory sort order), `test_ir_golden`
(real-remote timing-array fixtures captured on WiliIR's board), and
`test_ir_resolve`.

## Provenance / hardware verification

- **Carried from WiliIR (verified there 2026-07-05/06):** full 10/10
  protocol TX→RX loopback round-trip, the `ioexp_ir_pwr()` power-gate fix,
  the capture-restart PC-reset fix, and real-remote live decode across two
  physical extended-NEC devices (address 0xC7EA and 0xC0E0) plus a
  raw-fallback capture from a third, unknown-protocol remote — see
  `WiliIR/docs/hardware-notes.md`.
- **Verified in wilibsp this session (`apps/hello_ir`, bench-verified):** six
  clean on-board NEC TX→RX loopback pairs over a 20 s RTT window, `ovr 0`
  throughout, both PIO2 state machines claimed as expected (capture SM0 on
  GPIO24, TX SM1 on GPIO20). No external remote was pointed at the receiver
  in this session — that live-decode claim is carried from WiliIR, not
  re-verified here.
