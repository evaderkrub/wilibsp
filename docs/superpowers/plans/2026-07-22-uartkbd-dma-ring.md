# uartkbd DMA RX Ring Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace uartkbd's CPU FIFO polling with a continuous DMA drain into a 1 KB SRAM ring so no CPU stall can drop keyboard bytes.

**Architecture:** One file (`bsp/input/uartkbd.c`): a DMA channel in endless mode, paced by `DREQ_UART1_RX`, ring-wraps its write address over a 1024-byte aligned static buffer; `uartkbd_task()` drains from the software read index up to the DMA's live `write_addr`. Parser/API unchanged.

**Tech Stack:** Pico SDK `hardware_dma` (already linked into the BSP), RP2350 endless-mode TRANS_COUNT.

**Spec:** `docs/superpowers/specs/2026-07-22-uartkbd-dma-ring-design.md`

## Global Constraints

- No IRQ registration of any kind (polled convention; DMA_IRQ_0 stays untouched — the st7796 flush owns its shared handler).
- Ring: `static uint8_t __attribute__((aligned(1024))) s_ring[1024];` — alignment is REQUIRED for `channel_config_set_ring(&c, true, 10)` (2^10 = 1024).
- Endless mode: RP2350 `TRANS_COUNT` MODE field = 0xF (`DMA_CH0_TRANS_COUNT_MODE_VALUE_ENDLESS` in `hardware/regs/dma.h`). If that macro is absent in the SDK in use, STOP and report BLOCKED — do not silently substitute a finite count.
- UART config unchanged (uart1, 62500 8N1, GPIO38/39 GPIO_FUNC_UART_AUX, FIFO enabled, RX-only).
- Never pass `-DPICO_BOARD`; build via `python tools/fw.py build hello_keyboard`; host tests `python tools/fw.py test` (30/30, unchanged — this file is hardware-only).
- Conventional Commits with the session trailer.

---

### Task 1: DMA ring in uartkbd.c + doc note

**Files:**
- Modify: `bsp/input/uartkbd.c`
- Modify: `docs/drivers/keyboard.md` (one paragraph)

**Interfaces:**
- Consumes: existing `uartkbd_parse_*` API; `hardware/dma.h`.
- Produces: same public `uartkbd_*` API, now stall-immune.

- [ ] **Step 1: Rewrite `bsp/input/uartkbd.c`**

Replace the whole file with:

```c
#include "uartkbd.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"
#include "hardware/dma.h"

/* On GPIO38/39 the plain UART function is UART1 CTS/RTS; the UART-AUX
 * function routes UART1 TX/RX here instead (RP2350 datasheet GPIO muxing). */
#define UARTKBD_UART   uart1
#define UARTKBD_TX_PIN 38
#define UARTKBD_RX_PIN 39
#define UARTKBD_BAUD   62500

/* DMA drains the UART RX FIFO into this ring continuously (endless mode,
 * no IRQ), so keyboard bytes survive arbitrarily long CPU stalls up to one
 * full ring (~164 ms of line traffic at 62500 baud). uartkbd_task() reads
 * strictly behind the DMA write pointer. Alignment is required for the
 * DMA write-address ring wrap. */
#define RING_BITS 10
#define RING_SIZE (1u << RING_BITS)
static uint8_t __attribute__((aligned(RING_SIZE))) s_ring[RING_SIZE];
static uint32_t s_rd;          /* software read index into s_ring */
static int      s_dma_chan = -1;

static uartkbd_parser_t s_parser;

void uartkbd_init(void)
{
    uartkbd_parse_init(&s_parser);
    s_rd = 0;

    uart_init(UARTKBD_UART, UARTKBD_BAUD);
    gpio_set_function(UARTKBD_TX_PIN, GPIO_FUNC_UART_AUX);
    gpio_set_function(UARTKBD_RX_PIN, GPIO_FUNC_UART_AUX);
    uart_set_format(UARTKBD_UART, 8, 1, UART_PARITY_NONE);
    uart_set_hw_flow(UARTKBD_UART, false, false);
    uart_set_fifo_enabled(UARTKBD_UART, true);

    s_dma_chan = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config(s_dma_chan);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, true);
    channel_config_set_ring(&c, true, RING_BITS);      /* wrap write addr */
    channel_config_set_dreq(&c, DREQ_UART1_RX);
    dma_channel_configure(s_dma_chan, &c,
                          s_ring,                        /* write */
                          &uart_get_hw(UARTKBD_UART)->dr,/* read  */
                          0, false);                     /* count set below */
    /* Endless mode: TRANS_COUNT.MODE = 0xF -> count never decrements, the
     * channel runs forever. Writing the trigger alias starts it. */
    dma_channel_hw_addr(s_dma_chan)->al1_transfer_count_trig =
        ((uint32_t)DMA_CH0_TRANS_COUNT_MODE_VALUE_ENDLESS
             << DMA_CH0_TRANS_COUNT_MODE_LSB) | 1u;
}

void uartkbd_task(void)
{
    if (s_dma_chan < 0) return;
    uint32_t wr = (uint32_t)(dma_channel_hw_addr(s_dma_chan)->write_addr
                             - (uintptr_t)s_ring) & (RING_SIZE - 1);
    while (s_rd != wr) {
        uartkbd_parse_byte(&s_parser, s_ring[s_rd]);
        s_rd = (s_rd + 1) & (RING_SIZE - 1);
    }
}

bool     uartkbd_next_event(uartkbd_event_t *ev) { return uartkbd_parse_next_event(&s_parser, ev); }
uint16_t uartkbd_buttons(void) { return uartkbd_parse_buttons(&s_parser); }
uint8_t  uartkbd_flags(void)   { return uartkbd_parse_flags(&s_parser); }
uint32_t uartkbd_frames(void)  { return uartkbd_parse_frames(&s_parser); }
uint32_t uartkbd_errors(void)  { return uartkbd_parse_errors(&s_parser); }
```

- [ ] **Step 2: Doc note in `docs/drivers/keyboard.md`**

In the `input/uartkbd.{h,c}` bullet, after "RX-only, polled." append:

```markdown
  RX bytes are drained by a dedicated DMA channel (endless mode, no IRQ)
  into a 1 KB SRAM ring; `uartkbd_task()` reads behind the DMA write
  pointer, so keyboard input survives CPU stalls up to ~164 ms (e.g. long
  blocking renders) without losing frames.
```

- [ ] **Step 3: Build + regression**

Run: `python tools/fw.py build hello_keyboard` → clean (compiles the new uartkbd.c; if `DMA_CH0_TRANS_COUNT_MODE_VALUE_ENDLESS` is undefined, STOP → BLOCKED per Global Constraints).
Run: `python tools/fw.py test` → `100% tests passed, 0 tests failed out of 30`.

- [ ] **Step 4: Commit**

```bash
git add bsp/input/uartkbd.c docs/drivers/keyboard.md
git commit -m "feat: DMA ring drain for the UART keyboard - input immune to CPU stalls"
```

- [ ] **Step 5: On-hardware acceptance (owner-run)**

Flash, RTT, fill the screen, type flat-out ~15 s:
- `uartkbd frames` keeps climbing at the idle rate (~234/s) throughout.
- `uartkbd errors` stays FLAT across the burst (the previous run climbed ~4/s).
- All typed characters appear.
