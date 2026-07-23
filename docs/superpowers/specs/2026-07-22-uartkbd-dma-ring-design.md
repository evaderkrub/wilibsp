# uartkbd DMA RX ring (design)

Date: 2026-07-22
Status: approved (owner-proposed)

## Purpose

Hardware acceptance of the framebuffer rework showed `uartkbd_errors()` still
climbing during fast typing: full-screen repaints into PSRAM block the CPU
~18 ms, past the UART RX FIFO's ~5 ms capacity, so keyboard frames are lost.
Fix it at the BSP layer, per owner direction: DMA drains the UART FIFO into a
memory ring continuously, so NO CPU stall (present or future, any app) can
drop keyboard bytes. App rendering code needs no care about stall budgets.

## Design

`bsp/input/uartkbd.c` only (parser, header API, callers unchanged):

- `uartkbd_init()` additionally claims a free DMA channel
  (`dma_claim_unused_channel(true)`) configured as: read fixed from
  `&uart_get_hw(uart1)->dr`, 8-bit transfers, paced by `DREQ_UART1_RX`,
  write incrementing into a **1024-byte, 1024-aligned static SRAM ring**
  with DMA write-address ring wrap (`channel_config_set_ring(&c, true, 10)`),
  transfer count in **endless mode** (RP2350 `TRANS_COUNT.MODE = 0xF`: the
  count never decrements; the channel runs forever). No IRQ — zero interrupt
  footprint, preserving the BSP polled convention and the shared-DMA_IRQ_0
  invariant (nothing registered on it).
  - If the SDK in use lacks an endless-mode constant, fall back to writing
    the register directly: `dma_channel_hw_addr(ch)->transfer_count` is not
    used for arming in this mode — arm via `al1_transfer_count_trig` style
    raw write of `(0xFu << 28)`; the implementer verifies against
    `hardware/regs/dma.h` (`DMA_CH0_TRANS_COUNT_MODE_VALUE_ENDLESS`).
- `uartkbd_task()` computes the DMA's current write index from the channel's
  live `write_addr` register (`(write_addr - (uintptr_t)ring) & 1023`) and
  feeds bytes from the software read index up to it into
  `uartkbd_parse_byte()`, advancing the read index.
- UART itself unchanged: 62500 8N1, GPIO38/39 UART-AUX, FIFO enabled (the
  FIFO now just feeds the DREQ). RX-only as before.

## Capacity / failure modes

- Ring holds 1024 bytes = ~164 ms of line traffic at 6250 B/s — 9x the worst
  observed stall (18 ms repaint) and 30x the old FIFO budget. If the CPU ever
  stalls > 164 ms the ring overwrites unread bytes; the parser then sees a
  torn frame, counts checksum errors, and resyncs — same degradation mode as
  today, at a vastly higher threshold. No detection logic added (YAGNI; the
  errors counter already surfaces it).
- DMA vs CPU ring access: DMA writes, CPU only reads bytes strictly behind
  the write pointer snapshot taken at entry to `uartkbd_task()` — no torn
  reads (single-byte elements, one reader, one writer, reader always behind).

## Acceptance (on hardware)

Repeat the failed gate: full text screen, ~15 s of fastest-possible typing.
`uartkbd frames` keeps climbing at the idle rate (~234/s) and `uartkbd errors`
stays FLAT through the burst. Typed characters all appear.

## Out of scope

- Incremental rendering in hello_keyboard (no longer needed for input
  integrity; full repaint's ~18 ms display latency is acceptable).
- IRQ-driven UART RX (rejected: BSP is polled/no-IRQ by convention; the DMA
  ring achieves the same immunity with no interrupt).
- uartkbd_parse / API / doc-visible behavior changes (keyboard.md gets a
  one-paragraph note about the DMA ring).
