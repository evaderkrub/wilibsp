# PDM microphone driver harvest — design

**Date:** 2026-07-04
**Increment:** 2 of 4 (audio/RF/sensor driver harvest: I2S ✅ → **PDM mics** → CC1101 ✅ → I2C sensors)
**Source repo:** local `microphonearray` (`src/pdm/`, `src/dsp/`) — hardware-proven on this exact
FreeWili 2 board (real-time 4-mic DOA/beam-steering instrument ran on it).
**Approach:** verbatim harvest with four minimal adaptations (approved: approach A).

## Goal

Land the 4-channel PDM microphone capture driver (PIO + free-running ring DMA + integer CIC
decimation to 16 kHz int16 PCM) in `bsp/`, with a hardware-verifiable RTT demo app, host tests
for the pure DSP, and the standard catalog/pinmap/facts/driver-doc updates.

**Not in scope** (stays in the instrument repo — app domain, not BSP): FFT, DOA estimation,
beam-steering, display UI, mic-order remapping logic.

## Hardware facts

- 4 MEMS PDM mics, 2 data lines × 2 clock phases: MIC_CLK=GPIO28 (shared clock out),
  MIC_SIG1=GPIO29 (Mic A on clk-high, Mic B on clk-low), MIC_SIG2=GPIO30 (Mic C high, Mic D low).
  SIG1/SIG2 are consecutive so one PIO `in pins, 2` reads both.
- **PDM clock is 1.024 MHz** (= 16 kHz × 64). The FW2 mics did **not** output at the datasheet-
  typical 3.072 MHz on this board (measured in the source repo; matches the movieplayer mic).
- Mic power is gated by the PCAL6524 I/O expander, **MIC_PWR = P1 bit 7, active-high**;
  needs ~50 ms settle after power-on. Expander power-on default (and current `ioexp_init()`)
  leaves it off.
- **RP2350 pad-isolation trap:** input pads power up with the input buffer disabled and the ISO
  latch engaged — the PIO reads stuck-0 and the stream decimates to pure DC. The `.pio` init
  explicitly enables input buffers and clears ISO on both data pins (already in the harvested code).
- Physical left-to-right mic order is **D, B, A, C** (bench-measured phase ramp in the source
  repo), not A, B, C, D. Array-geometry knowledge for future DOA work — recorded in docs only.

## Files

| Source (`microphonearray/src/`) | Destination | Edits |
|---|---|---|
| `pdm/pdm_capture.c` | `bsp/pdm/pdm_capture.c` | `pio0`→`pio1`; `"board.h"`→`"platform/board.h"`; mic-power call in init (below) |
| `pdm/pdm_capture.h` | `bsp/pdm/pdm_capture.h` | none |
| `pdm/pdm_capture.pio` | `bsp/pdm/pdm_capture.pio` | none |
| `dsp/cic.c`, `dsp/cic.h` | `bsp/dsp/cic.{c,h}` | none |
| `dsp/dcblock.c`, `dsp/dcblock.h` | `bsp/dsp/dcblock.{c,h}` | none |

Platform-side changes:

- `bsp/platform/board.h`: add `PIN_MIC_CLK 28`, `PIN_MIC_SIG1 29`, `PIN_MIC_SIG2 30`,
  `PDM_CLK_HZ 1024000u` (carry the 1.024-MHz-not-3.072 comment). No `CIC_DECIMATE` define —
  `cic.h`'s default (64) already matches.
- `bsp/platform/ioexp.{c,h}`: add `void ioexp_mic_pwr(bool on)` driving P1 bit 7. The current
  driver is stateless; introduce a one-byte **P1 output shadow** initialized to
  `P1_BASE | ant_bits(ANT_CC1101_433)` at init; `ioexp_antenna()` updates the antenna bits in
  the shadow, `ioexp_mic_pwr()` the mic bit; both rewrite the output registers. `ioexp_init()`
  behavior for existing apps is unchanged (mic off).
- `bsp/CMakeLists.txt`: add `pdm/pdm_capture.c`, `dsp/cic.c`, `dsp/dcblock.c` to the library;
  `pico_generate_pio_header` for `pdm/pdm_capture.pio`.
- `bsp/fw2.h`: `#include "pdm/pdm_capture.h"`, `"dsp/cic.h"`, `"dsp/dcblock.h"`.

RAM note: the 32 KiB raw ring (`aligned(32768)`) is a static in `pdm_capture.o` — a STATIC-lib
object only linked into apps that reference `pdm_capture_*`. Apps that don't use mics pay nothing.

## Runtime design

**Init & power (adaptation, CC1101-lesson applied).** `pdm_capture_init()` itself calls
`ioexp_mic_pwr(true)` then `sleep_ms(50)` before starting the PIO/DMA, so any app that inits the
mics gets working mics without relying on another driver's side effects. Documented precondition:
`ioexp_init()` must have run (same rule as display/radio). The source repo did this in `main()`;
moving it into the driver is the same normalization as `spi_bus_init()` moving into `board_init()`
in increment 3.

**Resources.** One SM on **pio1** (shared with WS2812; programs are 4 + 4 instructions; GPIO
28-30 are inside pio1's default GPIO window — no `pio_set_gpio_base` needed, unlike pio2/radio).
One DMA channel, free-running write-address ring (count `0xFFFFFFFF` ≈ 9 h), **no IRQ** — the
shared-DMA_IRQ_0 invariant is not implicated (same posture as `gdo_capture`). Claims via
`pio_claim_unused_sm` / `dma_claim_unused_channel(true)` — panic on exhaustion like every other
BSP driver. PIO clkdiv derives from `clock_get_hz(clk_sys)` — correct at wilibsp's 250 MHz.

**Data flow.** PIO drives MIC_CLK and samples both data lines mid-phase on each clock level
(4 bits/period, autopush at 32 → 8 periods/word) → DMA → 32 KiB ring (~64 ms slack).
`pdm_capture_pull(out, max)` decimates everything between the read cursor and the live DMA write
cursor through 4 CIC decimators (3rd-order, R=64, headroom shift → int16), returning gap-free
16 kHz PCM per mic. `pdm_capture_block()` is the blocking convenience. **Single consumer, one
core** (shared CIC state) — documented in the header.

**Overrun semantics (inherited, documented).** If the consumer stalls > ~64 ms the DMA overwrites
unread words silently; the stream stays gap-free from the driver's view but the stalled span is
lost/torn. Acceptable for a capture driver — the source repo ran real-time beamforming on it.

## Demo app: `apps/hello_mics`

RTT-only (like `hello_cc1101`). `board_init()` → `ioexp_init()` → `pdm_capture_init()`; loop:
`pdm_capture_block()` 1600 frames (100 ms), `dcblock_inplace()` per channel, integer per-mic
RMS + peak via `DIAG` ~3×/s (no floats — invariant 3). Scaffold with `fw new-app hello_mics`
plus manual `add_subdirectory` in the top-level `CMakeLists.txt`.

**On-hardware pass criteria:** all 4 channels report low ambient at rest; tapping/speaking at
each physical mic position (order D, B, A, C left-to-right) spikes that channel and not the
others; no channel stuck at 0 / pure DC (would indicate the pad-ISO trap or mic power regressed).

## Host tests (`tests/`)

- `test_cic.c`: constant-1 input plateaus at the documented headroom value (2^18 >> 4 = 16384);
  constant-0 mirrors negative; exactly 1 output per 64 input bits; alternating 1/0 input decimates
  to ≈ 0.
- `test_dcblock.c`: a DC-offset input decays to ~0; an AC component survives with amplitude
  roughly preserved.

Both sources are pure integer C with no SDK includes — they compile on host untouched.

## Docs

- `docs/hardware/catalog.md`: PDM row → DONE; correct the harvest source to `microphonearray`.
- `docs/hardware/pinmap.md`: add GPIO 28/29/30.
- `docs/hardware/facts.md`: add the 1.024 MHz PDM clock fact, the RP2350 pad-ISO trap, the
  MIC_PWR expander bit, and the physical mic order (D, B, A, C).
- New `docs/drivers/pdm.md`: usage (init precondition, pull/block API, single-consumer rule,
  overrun semantics, RAM cost, mic-order note).
- `docs/superpowers/findings/`: hardware-verification findings doc after the on-target test.

## Verification plan

1. `fw test` — host CTest including the two new tests.
2. `fw build hello_mics` (plus `fw build` of an existing app to prove no regression).
3. Flash + `fw rtt` tap test per the pass criteria above (hardware permitting).
