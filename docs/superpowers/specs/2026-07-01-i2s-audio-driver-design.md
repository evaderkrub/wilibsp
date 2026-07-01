# I2S full-duplex audio driver — design

**Date:** 2026-07-01
**Status:** Approved design (pending user review of this spec)
**Increment:** 1 of 4 in the "extract audio/RF/sensor drivers" effort
(I2S audio → PDM mics → CC1101 radio → I2C sensors, each its own
spec → plan → implement cycle).

## Goal

Add a **full-duplex I2S audio** capability to `freewili2_bsp`: play PCM out
the on-board NAU88C10 codec (speaker **or** 3.5 mm headphone/line jack) **and**
capture the codec ADC (external 3.5 mm mic) **at the same time**, over one I2S
bus driven by a single PIO state machine. Ship a `hello_audio` demo app that
doubles as the on-hardware smoke test.

## Source & provenance

Harvested from **`evaderkrub/freewili2-fullduplex-audio`** (GitHub, **MIT**,
cloned/read 2026-07-01). That repo is the *same codebase family* as `wilibsp`
— identical `platform/board.*`, `boards/freewili2.h`, `display/st7796.*`,
`platform/ioexp_pcal6524.*`, `third_party/segger_rtt`, `DIAG()` diagnostics,
and `docs/superpowers/{specs,plans,findings}` layout — so the audio files
harvest **near-verbatim** into the existing `bsp/` include structure.

That repo's full-duplex path is **hardware-validated** (its
`docs/superpowers/findings/2026-06-19-fullduplex-e2e.md`): 16 kHz, 1 kHz sine
out the speaker while the external mic captures, proven four ways (RTT VU
stream never starves, eMeet-mic acoustic capture tracks the commanded
frequency 1 k→2 k, codec register readback, headphone-jack A/B). We inherit
that proof and re-verify on the `wilibsp` board.

## Scope

**In scope**
- I2S full-duplex engine (PIO0 SM0 + DMA): TX playback ring, RX capture.
- NAU88C10 codec bring-up + speaker/headphone output routing over I2C1.
- General PCM-block **capture API** ("audio in") + a pure VU-meter helper.
- Pure, host-tested DSP: `tone_gen` (sine), `vu_meter` (peak → bar).
- `apps/hello_audio` demo / on-hardware smoke test.
- Docs: `catalog.md` row flip, a `docs/drivers/audio.md` usage page.

**Out of scope (explicit)**
- PDM microphone array — that is Increment 2 (separate spec). The I2S codec
  mic (3.5 mm jack) is *not* the PDM array; they are different hardware.
- Configurable sample rate at runtime — fixed **16 kHz** (the proven rate);
  the driver derives clocks from `clk_sys` so other rates are a later knob.
- PSRAM-backed long capture clips — the capture API hands out live PCM
  blocks; recording-to-PSRAM is a follow-up.
- Any change to `board.c`'s proven 250 MHz / 1.25 V bring-up (see Clocks).

## Architecture

Three driver modules under `bsp/audio/`, plus two pure DSP modules, plus one
PIO program. Data flows through one PIO0 SM0 that clocks the codec (it is an
I2S **slave**, MCLK-direct) and, in the same per-bit loop, shifts the DAC bit
**out** (GPIO5) and latches the ADC bit **in** (GPIO4). Frame = 32 bits
`[L16 | R16]`, MSB-first, left-justified.

```
                         ┌─────────────── PIO0 SM0 (i2s_duplex.pio) ───────────────┐
  TX ring buf ──DMA──►   │ sideset: LRCK(6),BCLK(7)   OUT: SPK_DIN(5)  IN: SPK_DOUT(4) │
  (chained, no IRQ)      └───────────────┬───────────────────────────┬──────────────┘
                                         │ TX FIFO                    │ RX FIFO
                          audio_i2s_duplex_play_loop()   audio_capture (ping-pong DMA)
                                                                      │ DMA_IRQ_0 (SHARED)
                                                              latest [L16|R16] PCM block
                                                                      │
                                                            vu_peak(block, frames, slot)
   MCLK 256·fs ── PWM on GPIO22 (derived from clk_sys)
   I2C1 (26/27) ── codec_nau88c10_init() / _set_output()
```

### Modules

| File (`bsp/audio/`) | Role | Harvest |
|---|---|---|
| `audio_i2s_duplex.c/.h` | PIO+DMA engine: `init(fs)`, `play_loop(buf,frames)`, `play_stop()`, RX FIFO/DREQ accessors | verbatim |
| `i2s_duplex.pio` | Full-duplex I2S PIO program (3 instr/bit → PIO clk = 96·fs) | verbatim |
| `codec_nau88c10.c/.h` | NAU88C10 register bring-up + `_set_output(SPEAKER\|HEADPHONE)`, mute, dump, `_input_ok()` | verbatim (see codec note) |
| `audio_capture.c/.h` | **Generalized** from `vu_capture`: ping-pong DMA → latest PCM block + ready flag | modified (see Capture API) |
| `vu_meter.c/.h` | Pure DSP: `vu_peak(block, frames, slot)` + bar mapping | verbatim |
| `tone_gen.c/.h` | Pure DSP: fill a buffer with N whole periods of a sine | verbatim |

`i2s_rx.pio` (RX-only, superseded by the duplex program in the source repo)
is **not** harvested — full-duplex covers it.

### Capture API ("audio in") — the one designed piece

The source ships `vu_capture` (ping-pong DMA whose only public output is a
per-block *peak*). We generalize it to `audio_capture` so apps get **raw
PCM**, with the VU peak as a helper on top:

```c
// bsp/audio/audio_capture.h
#define AUDIO_CAPTURE_BLOCK_FRAMES 256   // ~16 ms @ 16 kHz
#define AUDIO_MIC_I2S_SLOT         1     // mono NAU88C10 ADC streams on the RIGHT slot

void  audio_capture_start(void);          // claim 2 DMA ch, ping-pong, SHARED DMA_IRQ_0
bool  audio_capture_block_ready(void);    // a new block completed since last read
// Pointer to the most-recently-completed block: AUDIO_CAPTURE_BLOCK_FRAMES
// frames of uint32 [L16|R16]. Clears the ready flag. NULL if none ready.
const uint32_t *audio_capture_block(void);
```

`vu_peak()` stays in `vu_meter.c` as a pure function over `(block, frames,
slot)`, so the demo is `audio_capture_block()` → `vu_peak()` → draw bar, and
any future app can run its own DSP over the same block. Internals (two
`uint32_t[256]` SRAM buffers, chained DMA, IRQ-flagged done index) are the
proven `vu_capture` mechanism, unchanged except the API surface and the IRQ
registration (below).

## Invariant compliance

1. **DMA_IRQ_0 is shared (must-fix).** `vu_capture.c` registers
   `irq_set_exclusive_handler(DMA_IRQ_0, …)`. The ST7796 flush already owns a
   *shared* handler on that line (invariant #4). `audio_capture.c` MUST use
   `irq_add_shared_handler(DMA_IRQ_0, dma_irq, PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY)`;
   the handler already guards on its own channel via
   `dma_channel_get_irq0_status()`, so no logic change beyond registration.
2. **250 MHz clock, unchanged `board.c`.** The audio driver derives **both**
   the MCLK PWM divider (`clock_get_hz(clk_sys)/(256·fs)`) and the PIO clkdiv
   (`clock_get_hz(clk_sys)/(96·fs)`) at runtime, so it runs at `wilibsp`'s
   proven 250 MHz with **no** driver edit. We keep `board.c`'s 250 MHz / vreg
   1.25 V bring-up and **ignore** the source repo's 153.6 MHz choice (that was
   a *board* decision made without the vreg bump; `wilibsp` is hardware-proven
   at 250 MHz). Consequence to verify: at 250 MHz, `256·fs`(16k)=4.096 MHz but
   integer PWM wrap lands MCLK at 250e6/61 ≈ **4.098 MHz (+0.06 %)** — a
   negligible pitch/ratio error, but re-checked on hardware.
3. **No floats in DIAG.** Codec/capture emit only integers via `DIAG`. The
   clkdiv `float` in `init()` is a compile-of-divider value, never logged.
4. **copy_to_ram / SRAM budget.** Static buffers are tiny: capture
   `2 × 256 × 4 B = 2 KB`; the demo tone buffer `≤ 256 B`. Well within 512 KB.
5. **PIO allocation.** Audio uses **pio0 SM0**; WS2812 LEDs use **pio1**; the
   ST7796 uses SPI1 (no PIO). No contention. (Future CC1101 GDO capture also
   wants pio0 but a different SM — noted for Increment 3, not a conflict now.)
6. **DMA channel budget.** Display 1 + TX ring 2 + RX ping-pong 2 = 5 of 16.

## Board / pin additions

Add to `bsp/platform/board.h` (I2C1 26/27 already present):

```c
// --- I2S audio (NAU88C10 codec) ---
#define PIN_AUDIO_DATA 5    // SPK_DIN:  I2S data into codec (PIO out / DAC)
#define PIN_AUDIO_DIN  4    // SPK_DOUT: codec ADC data into MCU (PIO in)
#define PIN_AUDIO_LRCK 6    // SPK_LRCK: I2S word clock (sideset bit 0)
#define PIN_AUDIO_BCLK 7    // SPK_BCLK: I2S bit clock  (sideset bit 1)
#define PIN_AUDIO_MCLK 22   // SPK_MCLK: 256*fs square wave (PWM)
```

No pin overlaps with the LCD/CC1101/LED/PSRAM sets. Update `docs/hardware/pinmap.md`
to move these five rows from the "not yet in board.h" table to the verified table.

**Codec I2C note:** `codec_nau88c10_init()` re-initialises I2C1 (pull-ups +
`i2c_init(i2c1, 400k)`), which `board_init()`/`board_i2c1_init()` already did.
This is redundant but idempotent; harvest verbatim (keeps the codec module
self-contained and matches the proven driver). A later cleanup could drop it.

## Demo app — `apps/hello_audio`

Scaffolded via `fw new-app hello_audio` (then add `add_subdirectory(apps/hello_audio)`
to the top-level `CMakeLists.txt`). Behavior mirrors the source repo's demo,
retargeted to `fw2.h`:

1. `board_init()`, `ioexp_init()`, `st7796_init()`, backlight on.
2. `codec_nau88c10_init()`; `codec_nau88c10_input_ok()` logged via RTT.
3. `audio_i2s_duplex_init(16000)`; `audio_capture_start()`.
4. Fill a power-of-two, size-aligned tone buffer via `tone_gen` (1 kHz @ 16 k);
   `audio_i2s_duplex_play_loop(tone, 64)`.
5. Main loop cycles **SILENCE → SPEAKER (4 s) → HEADPHONE (4 s)** via
   `codec_nau88c10_set_output()`, draws a per-state banner + a live mic VU bar
   on the ST7796 (`audio_capture_block()` → `vu_peak()`), and streams `vu:`
   peaks over RTT.

**Pass criteria (on-hardware smoke test):** codec init OK over RTT
(`rev!=0`, `pm2=0x015`); tone audible from the speaker in the SPEAKER window
and from a headphone in the HEADPHONE window; RTT `vu:` stream runs
continuously with no capture starvation; banner + VU bar render on the panel.

## Host tests (`fw test`)

Add standalone CTest entries mirroring the source repo's host harnesses
(`tests/host/tone_gen_harness.c`, `vu_meter_harness.c`) into `wilibsp`'s
`tests/` tree:
- `test_tone_gen`: N whole periods, amplitude/DC bounds, wrap continuity.
- `test_vu_meter`: peak of a known block/slot, silence → 0, slot selection.

Both are pure (no Pico SDK), consistent with `wilibsp`'s standalone host-test
project. `audio_i2s_duplex`/`codec`/`audio_capture` are hardware-bound (no
host test), matching the repo's existing pattern.

## Wiring checklist (mechanical, per AGENTS.md "How to add a driver")

1. Copy the six `bsp/audio/*` files + `i2s_duplex.pio` into `bsp/audio/`.
2. `bsp/CMakeLists.txt`: add the five `.c` files to `add_library(freewili2_bsp …)`;
   add `pico_generate_pio_header(freewili2_bsp …/audio/i2s_duplex.pio)`;
   add `hardware_pwm` to `target_link_libraries` (pio/dma/i2c already linked).
3. `bsp/fw2.h`: add `#include "audio/audio_i2s_duplex.h"`, `"audio/codec_nau88c10.h"`,
   `"audio/audio_capture.h"`, `"audio/vu_meter.h"`, `"audio/tone_gen.h"`.
4. `bsp/platform/board.h`: add the five `PIN_AUDIO_*` defines.
5. `apps/hello_audio` + top-level `add_subdirectory`.
6. `tests/`: add the two host tests + CMake entries.
7. Docs: flip the I2S row in `docs/hardware/catalog.md` to DONE; update
   `docs/hardware/pinmap.md`; add `docs/drivers/audio.md`.

## Risks & verification

- **RX slot/phase alignment** (known bring-up risk, flagged in `i2s_duplex.pio`
  and `AUDIO_MIC_I2S_SLOT`): if captured peaks sit at the noise floor while the
  mic is driven, apply the documented knobs in order — flip
  `AUDIO_MIC_I2S_SLOT`, move `in pins,1` to the other BCLK phase, or swap the
  channel side values. Proven-good defaults are harvested; this is a
  contingency.
- **MCLK +0.06 % at 250 MHz** (see invariant #2): expected negligible;
  confirmed by the on-hardware tone tracking in the smoke test.
- **DMA_IRQ_0 coexistence**: the shared-handler change is the critical
  correctness item; verified by the display continuing to flush while capture
  runs (both fire on DMA_IRQ_0).
- Final gate: the `apps/hello_audio` on-hardware smoke test above.

## Follow-ups (next increments / later)

- Increment 2: **PDM mic array** (`microphonearray`) — separate spec.
- Runtime-selectable sample rate; PSRAM capture-to-clip; line-in gain/ALC
  tuning; PWM-dimmed backlight already tracked elsewhere.
