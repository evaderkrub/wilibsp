# DVI Video + OSD Harvest — Design

**Date:** 2026-07-06
**Source:** `../movieplayer` (the FreeWili 2 movie player), stage `stage6-hstx-dvi`.
**Goal:** Harvest the movie player's **plain DVI video output** (640×480p60 over the
RP2350 HSTX block, GPIO 12–19) plus its **software OSD** into `freewili2_bsp` as a
reusable driver, with a `hello_dvi` demo app. The player's HDMI-audio-island mode is
**out of scope** and is stripped out during the harvest.

## Scope decisions (from brainstorming)

- **Plain DVI + OSD text.** Harvest the video scanout driver and the pure-software OSD
  rasterizer. Drop the TERC4 HDMI-audio-island mode, the vendored `pico_hdmi`, the
  `hstx_audio_islands` builders, the `hdmi_audio_ring` coupling, and the player-specific
  SRAM `hstx_arena` union (which aliases USB/FatFs/mic buffers and would drag half the
  player's memory layout into the BSP).
- **Clock: bump the board to 252 MHz.** `hstx_dvi` derives `clk_hstx = clk_sys / 2`, and
  the HSTX CSR `CLKDIV = 5` gives `clk_hstx / 5` as the pixel clock. At the BSP's current
  250 MHz that is 25.0 MHz (0.7% below the 640×480p60 standard 25.175 MHz); at 252 MHz it
  is exactly 25.2 MHz — the value the movie player proved. 252 MHz is already validated on
  this exact board (movie player, at 1.25 V Vcore) across USB host, PSRAM, audio, and
  decode; the historical "252 fault" was marginal Vcore at 1.15 V, since fixed. So the
  board moves to 252 MHz and **invariant 2 is revised** accordingly.
- **Stored region 480×320 (full panel).** With the player's SRAM-arena constraint gone
  (it capped the stored height at 272), the BSP driver restores a clean full-panel cap:
  `HSTX_VID_W_MAX = 480`, `HSTX_VID_H_MAX = 320`, a compile-time `#define`. The ≤480×320
  video sits centered in 640×480 with ~80 px borders top/bottom/sides — ample room for the
  bottom OSD strip.

## Hardware context (confirmed)

The FreeWili 2 routes the RP2350 HSTX pins to a DVI connector in the standard
picodvi-hstx / Adafruit Fruit Jam pinout. Pins are otherwise unused in the BSP:

| Signal | GPIO (_N / _P) |
|--------|----------------|
| DVI_CLK | 12 / 13 |
| DVI_D0  | 14 / 15 |
| DVI_D1  | 16 / 17 |
| DVI_D2  | 18 / 19 |

The FreeWili 2 differential polarity (the movie player's colour fix): each pair's **_P**
pin (the ODD gpio → HSTX odd bit) is NON-inverted; the **_N** pin (even gpio) is INVERTED.
Baked into `hstx_setup_peripheral()`.

## Architecture & module boundaries

The BSP grows by harvesting a proven driver, not rewriting one. This harvest copies the
plain-DVI path verbatim in behaviour and strips the compiled-in HDMI-audio path.

### Files harvested into `bsp/display/`

| File | Treatment |
|------|-----------|
| `hstx_dvi.c` | **Stripped** to the plain-DVI path only (see "The strip"). |
| `hstx_dvi.h` | **Stripped** — remove the `audio/hdmi_audio_ring.h` include and every `*_hdmi_*` / island API entry; keep the plain-DVI surface. |
| `hstx_modes.h` | Copied; trim the HDMI-island-only macros (video preamble, guard band, `W_PREAMBLE`, `W_DATA_ISLAND`, data-island preambles). Keep the DVI control symbols, per-line sync words, 640×480p60 VESA timing, and HSTX command opcodes. |
| `dvi_osd.c` / `dvi_osd.h` | **Verbatim.** Pure, no Pico SDK dependency, host-testable. |
| `font5x7.{c,h}` | **Not harvested** — already present in `bsp/display/` and byte-identical to the source. The OSD reuses it. |

**Explicitly dropped:** `hstx_audio_islands.{c,h}`, `third_party/pico_hdmi/`, `hstx_arena.h`,
the `hdmi_audio_ring` coupling, and the SRAM-arena union.

### Driver public surface (`hstx_dvi.h`, after strip)

```c
// clk_hstx = clk_sys/2; configure HSTX + GPIO 12-19; build the scanout command
// buffer for a vid_w x vid_h video centered in 640x480; start zero-IRQ scanout.
// clk_sys must already be at its final (even MHz) value. Video region cleared to black.
void hstx_dvi_init(int vid_w, int vid_h);

// Rebuild for a new centered video size (rounded even, clamped to HSTX_VID_*_MAX).
void hstx_dvi_set_geometry(int vid_w, int vid_h);

// The video region as a strided native-endian RGB565 framebuffer.
uint16_t *hstx_dvi_video_base(void);   // top-left of the centered movie
uint16_t *hstx_dvi_region_base(void);  // pixel(0,0) of the fixed region (OSD origin)
int       hstx_dvi_video_stride(void); // uint16 elements between rows
int       hstx_dvi_video_w(void);
int       hstx_dvi_video_h(void);
int       hstx_dvi_region_h(void);     // fixed region height (HSTX_VID_H_MAX)

// Blank/unblank the 3 DATA lanes (CLK keeps running so the monitor stays locked).
void hstx_dvi_enable(bool on);
```

Plus the OSD helpers, drawing into the strided region framebuffer:
`dvi_osd_strip_h`, `dvi_osd_fill_rect`, `dvi_osd_text`, `dvi_osd_text_msg`,
`dvi_osd_progress`, `dvi_osd_clear_strip`.

### Contract & invariants

- **Writer:** the app writes native-endian (little-endian) RGB565 pixels into
  `hstx_dvi_video_base()` at `y * hstx_dvi_video_stride()`. Pixels are physically
  interleaved with HSTX command words, hence the stride; the OSD only writes
  `x ∈ [0, region_w)` so it never touches command words.
- **Scanout:** HSTX reads the buffer 60×/s autonomously; the 640×480 border is
  HSTX-generated (`HSTX_CMD_TMDS_REPEAT`, one word → N pixels, ~zero bandwidth), not
  stored. Framebuffer is in **SRAM** (PSRAM scanout underruns the FIFO and wedges flash).
- **Zero IRQs.** The plain-DVI scanout is a SCREEN channel (streams the command buffer to
  the HSTX FIFO) + a LOOPER channel (rewrites SCREEN's `read_addr` and chains back). No
  interrupt handler is installed → **respects invariant 4** (never registers a
  `DMA_IRQ_0` handler). Two channels claimed dynamically via `dma_claim_unused_channel`.

### The strip (what leaves `hstx_dvi.c`)

Remove:
- includes: `pico_hdmi/hstx_packet.h`, `display/hstx_audio_islands.h`,
  `display/hstx_arena.h`, `audio/hdmi_audio_ring.h`, and the unused
  `hardware/structs/bus_ctrl.h`;
- all `if (g_hdmi_audio)` branches in `hstx_build` / `hstx_dma_start`;
- the island state block, `hstx_hdmi_schedule_islands`, `hstx_hdmi_seed_islands`,
  `hstx_hdmi_map_audio_offsets`, `hstx_hdmi_refresh`, `hstx_frame_irq`, and the entire
  HDMI-audio public API (`hstx_dvi_init_hdmi_audio`, `_set_hdmi_ring`, `_set_hdmi_topup`,
  `_hdmi_irq_attach_this_core`, `_hdmi_samples_played`);
- the `g_hstx_arena[HSTX_ARENA_DWORDS]` declaration and its `_Static_assert`s against the
  arena.

Replace the arena array with a plainly-sized `static uint32_t framebuf[HSTX_PLAIN_DWORDS];`
where `HSTX_PLAIN_DWORDS = 200 + HSTX_VID_H_MAX*(11 + HSTX_VID_W_MAX/2) +
(MODE_V_ACTIVE_LINES - HSTX_VID_H_MAX)*8`. At 480×320 that is ≈ 81,800 dwords ≈ **327 KB**
SRAM. The remaining `hstx_build` / `hstx_dma_start` / `hstx_dvi_init` /
`hstx_dvi_set_geometry` / accessors / `hstx_dvi_enable` are behaviourally identical to the
player's plain-DVI path.

## Clock / invariant-2 change

`bsp/platform/board.h`: `BOARD_SYS_CLOCK_KHZ` 250000 → **252000**.

Ripple edits (all part of this work):
- `bsp/platform/board.c` — update the clock comment (252 MHz is now the operating point;
  the old "252 fault" was 1.15 V Vcore, since resolved at 1.25 V).
- `AGENTS.md` invariant 2 — record 252 MHz (was 250 MHz).
- `docs/hardware/facts.md` — same.

Risk note: other BSP drivers compute their dividers from `clk_sys` at runtime (SPI via the
`clk_peri` re-source, audio/PDM/etc.), so they self-adjust; and 252 MHz is already proven
on this board by the movie player. Low risk, but existing apps' on-hardware behaviour
should be re-confirmed as part of the Task-9 smoke once that lands.

## Demo app — `apps/hello_dvi`

Following the `hello_display` / `hello_usbdrive` pattern (`copy_to_ram`, links
`freewili2_bsp`). No PSRAM needed (framebuffer is SRAM).

1. `board_init()` (now 252 MHz).
2. `hstx_dvi_init(480, 288)` — a video **shorter** than the 320-tall region, so the
   letterbox leaves a real ~16 px bottom margin (`dvi_osd_strip_h ≥ 8`) for the OSD strip
   instead of falling back to overlay mode.
3. Paint a test pattern into the video region via `hstx_dvi_video_base()/stride()/w()/h()`,
   native-endian RGB565: SMPTE-style vertical colour bars + a centered box outline
   (validates colour, endianness, and centering).
4. OSD: a title (`dvi_osd_text_msg`) and an animating progress bar (`dvi_osd_progress`) in
   the bottom margin — motion proves the scanout is live.
5. `DIAG("dvi: up, clk_hstx=%u kHz", ...)`.
6. Loop advancing the progress bar; human verifies picture + colour + lock on a bench
   monitor.

## Wiring (per AGENTS.md "how to add a driver")

1. `bsp/CMakeLists.txt`: add `display/hstx_dvi.c` and `display/dvi_osd.c` to the
   `freewili2_bsp` STATIC source list. Confirm HSTX needs only the already-linked
   `hardware_dma` / `hardware_clocks` / `hardware_gpio` / `hardware_irq` (the HSTX
   registers come from header-only `hardware/structs/hstx_*.h`); add a link only if the
   configure step shows one missing.
2. `bsp/fw2.h`: add `#include "display/hstx_dvi.h"` and `#include "display/dvi_osd.h"`.
3. `apps/hello_dvi/` scaffolded from `apps/template`; add `add_subdirectory(apps/hello_dvi)`
   to the top-level `CMakeLists.txt`.
4. Docs: flip the DVI row in `docs/hardware/catalog.md` TODO → DONE; add the DVI pins to
   `docs/hardware/pinmap.md`; add `docs/drivers/dvi.md`; apply the invariant-2 clock edits
   above.

## Testing

- **Host (`fw test`)**: `tests/test_dvi_osd.c` covers the pure OSD geometry logic —
  `dvi_osd_strip_h` (letterbox margin vs. `DVI_OSD_OVERLAY_H` fallback when the bar is
  < 8 px) and `dvi_osd_progress` fill math (`bar_w * cur / total`, clamping, label-fills-row
  → no bar). `dvi_osd.c` has no Pico SDK dependency, so it compiles into the host tree
  as-is. Wire into `tests/CMakeLists.txt`.
- **On-hardware**: `fw build hello_dvi` → `fw flash hello_dvi` → `fw rtt` shows
  `dvi: up, clk_hstx=126000 kHz`; a bench monitor locks to 640×480 with correct colours and
  a stable centered pattern + moving OSD bar. (Human-verified; no automated hardware gate.)

## Risks

- **25.2 MHz lock on the bench monitor** — the exact pixel clock is validated on the movie
  player; the BSP inherits it verbatim at 252 MHz. Primary human-verify target.
- **252 MHz board-wide** — proven on this board by the movie player; existing BSP apps to
  be re-smoked (fold into Task 9).
- **327 KB SRAM framebuffer** — fits a `copy_to_ram` demo comfortably, but is a large fixed
  cost; `HSTX_VID_H_MAX` is a compile-time `#define` an app can lower if it links the DVI
  driver alongside the USB/FatFs + LCD-strip stack in one binary.
