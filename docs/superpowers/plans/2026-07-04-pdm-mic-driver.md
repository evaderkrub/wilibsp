# PDM Microphone Driver Harvest Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Land the 4-channel PDM microphone capture driver (PIO + free-running ring DMA + integer CIC → 16 kHz int16 PCM ×4) in `bsp/`, with an RTT demo app, host tests, and docs — increment 2 of the audio/RF/sensor harvest.

**Architecture:** Verbatim harvest from the local `microphonearray` repo (`src/pdm/`, `src/dsp/`) with four approved adaptations: PDM PIO moves `pio0`→`pio1` (audio owns pio0), include path `"board.h"`→`"platform/board.h"`, mic pins + `PDM_CLK_HZ` added to `bsp/platform/board.h`, and a new `ioexp_mic_pwr()` (PCAL6524 P1 bit 7) that `pdm_capture_init()` calls itself (+50 ms settle) so the driver is self-contained. Capture DMA is IRQ-free (free-running write-address ring), so the shared-DMA_IRQ_0 invariant is not implicated.

**Tech Stack:** RP2350B (Pico SDK: hardware_pio/dma/i2c), SEGGER RTT diagnostics, host CTest (MinGW GCC + Ninja) via `fw test`, target builds via `fw build <app>`.

**Spec:** `docs/superpowers/specs/2026-07-04-pdm-mic-driver-design.md`

## Global Constraints

- Branch: all work on `feat/pdm-mics` (already created; spec committed).
- Never pass `-DPICO_BOARD` on any cmake command line (AGENTS.md invariant 1/8). Always build via `fw build [app]` / `fw test` from the repo root.
- Diagnostics via `DIAG()` (SEGGER RTT) only — no printf/stdio, **no floats in DIAG format strings** (`%d %u %x %s %c` only).
- No `irq_set_exclusive_handler(DMA_IRQ_0, ...)` anywhere (invariant 4). This plan adds **no** DMA IRQ user at all.
- PIO ownership: pio0 = audio, pio1 = LEDs (+ **PDM, this plan**), pio2 = radio (re-based to GPIO 16 — do NOT put PDM there).
- Harvested code stays verbatim except the edits explicitly shown in this plan.
- Conventional Commits (`feat:`, `fix:`, `docs:`, `test:` with scope), imperative subject.
- Commit trailer on every commit:
  `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>` and
  `Claude-Session: https://claude.ai/code/session_019aDYPyjrh7VK9ZJDhwe7hf`
- Host tests use the existing `tests/test_util.h` macros (`ASSERT_EQ`, `ASSERT_TRUE`, `TEST_RETURN`).

---

### Task 1: Harvest `dsp/cic` with host test

**Files:**
- Create: `bsp/dsp/cic.h`, `bsp/dsp/cic.c` (verbatim from `C:\~prj\Dropbox\vibeProjects\microphonearray\src\dsp\cic.{h,c}`)
- Create: `tests/test_cic.c`
- Modify: `tests/CMakeLists.txt` (append test target)

**Interfaces:**
- Produces: `cic_t`, `void cic_init(cic_t*)`, `int cic_push_bit(cic_t* c, int bit, int16_t* out)` — returns 1 and writes one PCM sample per `CIC_DECIMATE` (64) bits, else 0. Full-scale DC plateau = ±16384. Task 4's `pdm_capture.c` includes `"dsp/cic.h"`.

- [ ] **Step 1: Write the failing test**

Create `tests/test_cic.c`:

```c
// tests/test_cic.c — host tests for the 3rd-order CIC decimator (pure integer).
#include "dsp/cic.h"
#include "test_util.h"
#include <stdlib.h>

int main(void) {
    // 1. Exactly one output per CIC_DECIMATE input bits.
    cic_t c;
    cic_init(&c);
    int16_t pcm = 0;
    int outputs = 0;
    for (int i = 0; i < 64 * 10; i++)
        if (cic_push_bit(&c, 1, &pcm)) outputs++;
    ASSERT_EQ(outputs, 10);

    // 2. Constant-1 input plateaus at +16384 (2^18 >> 4, the documented
    //    headroom scaling) once the comb pipeline fills (~3 outputs).
    ASSERT_EQ(pcm, 16384);

    // 3. Constant-0 input mirrors to -16384.
    cic_init(&c);
    for (int i = 0; i < 64 * 10; i++) cic_push_bit(&c, 0, &pcm);
    ASSERT_EQ(pcm, -16384);

    // 4. Alternating 1/0 (Nyquist-rate PDM) decimates to ~0 (deep stopband).
    cic_init(&c);
    for (int i = 0; i < 64 * 10; i++) cic_push_bit(&c, i & 1, &pcm);
    ASSERT_TRUE(abs(pcm) < 100);

    TEST_RETURN();
}
```

- [ ] **Step 2: Run test to verify it fails**

Append to `tests/CMakeLists.txt`:

```cmake
add_executable(test_cic
    test_cic.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../bsp/dsp/cic.c)
target_include_directories(test_cic PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../bsp
    ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME cic COMMAND test_cic)
```

Run: `python tools/fw.py test`
Expected: FAIL — configure/build error, `bsp/dsp/cic.c` does not exist yet.

- [ ] **Step 3: Harvest the implementation**

Copy the two files **verbatim, no edits** (only the leading path comment differs from the source repo, matching its `src/`-relative convention — keep the file's own header comment exactly as-is):

```powershell
New-Item -ItemType Directory -Force C:\~prj\Dropbox\vibeProjects\wilibsp\bsp\dsp
Copy-Item C:\~prj\Dropbox\vibeProjects\microphonearray\src\dsp\cic.h C:\~prj\Dropbox\vibeProjects\wilibsp\bsp\dsp\cic.h
Copy-Item C:\~prj\Dropbox\vibeProjects\microphonearray\src\dsp\cic.c C:\~prj\Dropbox\vibeProjects\wilibsp\bsp\dsp\cic.c
```

For reference, the copied `bsp/dsp/cic.h` must be exactly:

```c
// src/dsp/cic.h — 3rd-order CIC decimator: 1-bit PDM in, 16-bit PCM out.
#ifndef CIC_H
#define CIC_H
#include <stdint.h>

#define CIC_ORDER 3
#ifndef CIC_DECIMATE
#define CIC_DECIMATE 64u
#endif

typedef struct {
    int32_t integ[CIC_ORDER];   // integrator accumulators
    int32_t comb[CIC_ORDER];    // comb delay registers (at decimated rate)
    uint32_t phase;             // 0..CIC_DECIMATE-1
} cic_t;

void cic_init(cic_t* c);

// Feed one PDM bit (0 or 1, mapped to -1/+1 internally). Returns 1 and writes a
// PCM sample to *out exactly once per CIC_DECIMATE bits; otherwise returns 0.
int cic_push_bit(cic_t* c, int bit, int16_t* out);

#endif // CIC_H
```

and `bsp/dsp/cic.c` exactly:

```c
// src/dsp/cic.c — 3rd-order CIC. Gain = CIC_DECIMATE^CIC_ORDER; we right-shift
// to land a full-scale PDM swing near int16 range.
#include "dsp/cic.h"

// log2(64^3) = 18 bits of CIC growth; shift by 20 (not 18) to leave ~6 dB of
// headroom: full-scale DC -> 2^18 >> 4 = 16384, well below int16 clip.
#define CIC_SHIFT 20

void cic_init(cic_t* c) {
    for (int i = 0; i < CIC_ORDER; i++) { c->integ[i] = 0; c->comb[i] = 0; }
    c->phase = 0;
}

int cic_push_bit(cic_t* c, int bit, int16_t* out) {
    int32_t x = bit ? 1 : -1;          // map PDM {0,1} -> {-1,+1}
    // Integrator cascade (runs at full PDM rate).
    c->integ[0] += x;
    for (int i = 1; i < CIC_ORDER; i++) c->integ[i] += c->integ[i-1];

    if (++c->phase < CIC_DECIMATE) return 0;
    c->phase = 0;

    // Comb cascade (runs at decimated rate).
    int32_t v = c->integ[CIC_ORDER-1];
    for (int i = 0; i < CIC_ORDER; i++) {
        int32_t d = v - c->comb[i];
        c->comb[i] = v;
        v = d;
    }
    int32_t s = v >> (CIC_SHIFT - 16);  // scale toward int16
    if (s > 32767) s = 32767;
    if (s < -32768) s = -32768;
    *out = (int16_t)s;
    return 1;
}
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `python tools/fw.py test`
Expected: all tests pass including new `cic` (9 total: led_color, tone_gen, vu_meter, cc1101_regs, monitor_engine, capture_store, ook_tx, scan_engine, cic).

- [ ] **Step 5: Commit**

```bash
git add bsp/dsp/cic.c bsp/dsp/cic.h tests/test_cic.c tests/CMakeLists.txt
git commit -m "feat(dsp): harvest 3rd-order CIC decimator from microphonearray (host-tested)"
```

---

### Task 2: Harvest `dsp/dcblock` with host test

**Files:**
- Create: `bsp/dsp/dcblock.h`, `bsp/dsp/dcblock.c` (verbatim from `microphonearray/src/dsp/`)
- Create: `tests/test_dcblock.c`
- Modify: `tests/CMakeLists.txt` (append test target)

**Interfaces:**
- Produces: `void dcblock_inplace(int16_t* buf, unsigned n)` — in-place one-pole DC-blocking high-pass (R=0.90, ~250 Hz cutoff @ 16 kHz), stateless across calls, `buf[0]` forced to 0. Task 5's demo app calls it per mic block.

- [ ] **Step 1: Write the failing test**

Create `tests/test_dcblock.c`:

```c
// tests/test_dcblock.c — host tests for the one-pole DC-blocking high-pass.
#include "dsp/dcblock.h"
#include "test_util.h"
#include <stdlib.h>

#define N 512u

int main(void) {
    // 1. Pure DC offset decays to ~0 by the tail of the block.
    int16_t buf[N];
    for (unsigned i = 0; i < N; i++) buf[i] = 1000;
    dcblock_inplace(buf, N);
    ASSERT_EQ(buf[0], 0);                    // first output defined as 0
    ASSERT_TRUE(abs(buf[N - 1]) <= 2);       // DC fully blocked

    // 2. Alternating +/-1000 (high-frequency AC) passes with the analytic
    //    steady-state gain 2/(1+R) = 2/1.9 ~ x1.05: tail amplitude ~1052.
    for (unsigned i = 0; i < N; i++) buf[i] = (i & 1) ? -1000 : 1000;
    dcblock_inplace(buf, N);
    ASSERT_TRUE(abs(buf[N - 1]) >= 900);
    ASSERT_TRUE(abs(buf[N - 1]) <= 1200);
    ASSERT_TRUE((buf[N - 1] > 0) != (buf[N - 2] > 0));   // still alternating

    // 3. AC riding on DC: offset removed, AC survives.
    for (unsigned i = 0; i < N; i++) buf[i] = (int16_t)(5000 + ((i & 1) ? -1000 : 1000));
    dcblock_inplace(buf, N);
    ASSERT_TRUE(abs(buf[N - 1]) >= 900);
    ASSERT_TRUE(abs(buf[N - 1]) <= 1200);

    // 4. n=0 is a no-op (must not crash).
    dcblock_inplace(buf, 0);

    TEST_RETURN();
}
```

- [ ] **Step 2: Run test to verify it fails**

Append to `tests/CMakeLists.txt`:

```cmake
add_executable(test_dcblock
    test_dcblock.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../bsp/dsp/dcblock.c)
target_include_directories(test_dcblock PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../bsp
    ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME dcblock COMMAND test_dcblock)
```

Run: `python tools/fw.py test`
Expected: FAIL — `bsp/dsp/dcblock.c` does not exist yet.

- [ ] **Step 3: Harvest the implementation**

```powershell
Copy-Item C:\~prj\Dropbox\vibeProjects\microphonearray\src\dsp\dcblock.h C:\~prj\Dropbox\vibeProjects\wilibsp\bsp\dsp\dcblock.h
Copy-Item C:\~prj\Dropbox\vibeProjects\microphonearray\src\dsp\dcblock.c C:\~prj\Dropbox\vibeProjects\wilibsp\bsp\dsp\dcblock.c
```

Copied `bsp/dsp/dcblock.h` must be exactly:

```c
// src/dsp/dcblock.h — one-pole DC-blocking high-pass for int16 PCM.
// y[n] = x[n] - x[n-1] + R*y[n-1]. R=0.90 -> cutoff ~250 Hz @ 16 kHz, keeps 1 kHz.
// The FW2 PDM idle stream is heavily DC-biased and the room/handling carries
// strong sub-200 Hz energy; a gentle blocker leaves a leakage skirt that floods
// the low FFT bins and crushes the tone SNR. An aggressive high-pass removes it.
// Applied in the analysis path only (not the CIC).
#ifndef DCBLOCK_H
#define DCBLOCK_H
#include <stdint.h>

#ifndef DCBLOCK_R
#define DCBLOCK_R 0.90f
#endif

// In-place high-pass filter `n` samples of `buf`. Stateless across calls
// (each block re-initialises from buf[0]).
void dcblock_inplace(int16_t* buf, unsigned n);

#endif // DCBLOCK_H
```

and `bsp/dsp/dcblock.c` exactly:

```c
// src/dsp/dcblock.c — one-pole DC-blocking high-pass (see dcblock.h).
#include "dsp/dcblock.h"

void dcblock_inplace(int16_t* buf, unsigned n) {
    if (n == 0) return;
    float prev_x = (float)buf[0];
    float prev_y = 0.0f;
    buf[0] = 0;  // first output is defined as 0 (no prior sample)
    for (unsigned i = 1; i < n; i++) {
        float x = (float)buf[i];
        float y = x - prev_x + DCBLOCK_R * prev_y;
        prev_x = x;
        prev_y = y;
        if (y > 32767.0f) y = 32767.0f;
        else if (y < -32768.0f) y = -32768.0f;
        buf[i] = (int16_t)y;
    }
}
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `python tools/fw.py test`
Expected: all 10 tests pass (previous 9 + `dcblock`).

- [ ] **Step 5: Commit**

```bash
git add bsp/dsp/dcblock.c bsp/dsp/dcblock.h tests/test_dcblock.c tests/CMakeLists.txt
git commit -m "feat(dsp): harvest DC-blocking high-pass from microphonearray (host-tested)"
```

---

### Task 3: `ioexp_mic_pwr()` — mic power via PCAL6524 P1 bit 7

**Files:**
- Modify: `bsp/platform/ioexp.h` (add prototype + doc)
- Modify: `bsp/platform/ioexp.c` (introduce P1 output shadow; add `ioexp_mic_pwr`)

**Interfaces:**
- Consumes: existing `write_outputs(p0, p1)`, `ant_bits(sel)`, `P1_BASE`, `P0_OUT` statics in `ioexp.c`.
- Produces: `void ioexp_mic_pwr(bool on)` — drives MIC_PWR (P1 bit 7, active-high) and preserves the antenna-select bits. Task 4's `pdm_capture_init()` calls it. `ioexp_antenna()` keeps its exact signature and preserves the mic bit.

No host test — this is hardware I2C glue with no pure logic (same posture as the rest of `ioexp.c`); it is exercised on-target in Task 7. Verified here by compiling.

- [ ] **Step 1: Edit `bsp/platform/ioexp.h`**

Add after the `ioexp_antenna` prototype (line 29):

```c
// MIC_PWR (P1 bit 7, active-high): power rail for the 4 PDM MEMS microphones.
// Off at power-on and after ioexp_init(); pdm_capture_init() turns it on and
// waits ~50 ms for the mics to settle. Preserves the antenna-select bits.
void ioexp_mic_pwr(bool on);
```

- [ ] **Step 2: Edit `bsp/platform/ioexp.c`**

Replace the `ioexp_antenna` function (lines 42-44) with a shadowed version plus the new function. The current file computes P1 on the fly; introduce a single shadow byte so the antenna bits and the mic-power bit compose instead of clobbering each other:

```c
// Shadow of the Port-1 output byte: base control bits + antenna select + MIC_PWR.
// Default matches ioexp_init(): CC1101 433 antenna, mic power OFF.
#define P1_MIC_PWR 0x80   // P1 bit 7, active-high mic rail
#define P1_ANT_MASK 0x0A  // V1_1 (bit3) | V2_1 (bit1)
static uint8_t s_p1 = P1_BASE | 0x08;   // == P1_BASE | ant_bits(ANT_CC1101_433)

void ioexp_antenna(uint8_t sel) {
    s_p1 = (uint8_t)((s_p1 & (uint8_t)~P1_ANT_MASK) | ant_bits(sel));
    write_outputs(P0_OUT, s_p1);
}

void ioexp_mic_pwr(bool on) {
    s_p1 = on ? (uint8_t)(s_p1 | P1_MIC_PWR) : (uint8_t)(s_p1 & (uint8_t)~P1_MIC_PWR);
    write_outputs(P0_OUT, s_p1);
    DIAG("ioexp: MIC_PWR (P1_7) -> %d\n", on ? 1 : 0);
}
```

Then make `ioexp_init()` (lines 46-58) reset and use the shadow so a re-init is consistent — replace its `out[4]` line with:

```c
    s_p1 = (uint8_t)(P1_BASE | ant_bits(ANT_CC1101_433));   // mic power OFF
    uint8_t out[4] = { REG_OUTPUT0, P0_OUT, s_p1, P2_OUT };
```

(The rest of `ioexp_init()` is unchanged.)

- [ ] **Step 3: Verify it compiles (target build)**

Run: `python tools/fw.py build hello_display`
Expected: build succeeds (no app uses `ioexp_mic_pwr` yet; this proves the BSP still compiles and existing behavior is untouched).

- [ ] **Step 4: Commit**

```bash
git add bsp/platform/ioexp.h bsp/platform/ioexp.c
git commit -m "feat(platform): add ioexp_mic_pwr (PCAL6524 P1_7) with P1 output shadow"
```

---

### Task 4: Harvest `pdm_capture` (PIO + ring DMA), wire into BSP

**Files:**
- Create: `bsp/pdm/pdm_capture.h` (verbatim), `bsp/pdm/pdm_capture.pio` (verbatim), `bsp/pdm/pdm_capture.c` (verbatim + 3 approved edits)
- Modify: `bsp/platform/board.h` (mic pins + `PDM_CLK_HZ`)
- Modify: `bsp/CMakeLists.txt` (sources + pio header)
- Modify: `bsp/fw2.h` (umbrella includes)

**Interfaces:**
- Consumes: `cic_init`/`cic_push_bit` (Task 1), `ioexp_mic_pwr` (Task 3), `PIN_MIC_CLK`/`PIN_MIC_SIG1`/`PDM_CLK_HZ` (added here).
- Produces: `PDM_NUM_MICS` (4), `enum { MIC_A, MIC_B, MIC_C, MIC_D }`, `void pdm_capture_init(void)`, `unsigned pdm_capture_pull(int16_t* out[PDM_NUM_MICS], unsigned max)`, `unsigned pdm_capture_block(int16_t* out[PDM_NUM_MICS], unsigned frames)` — Task 5's demo consumes all of these via `fw2.h`.

- [ ] **Step 1: Add mic pins to `bsp/platform/board.h`**

Insert after the I2S audio block (after line 36, `PIN_AUDIO_MCLK`):

```c
// --- PDM microphones (4 MEMS mics, 2 data lines x 2 clock phases) ---
#define PIN_MIC_CLK    28   // shared PDM clock out (PIO side-set)
#define PIN_MIC_SIG1   29   // data line 1: Mic A (clk-high) + Mic B (clk-low)
#define PIN_MIC_SIG2   30   // data line 2: Mic C (clk-high) + Mic D (clk-low)
// MIC_SIG1/2 are consecutive so one PIO `in pins, 2` reads both.
// PDM clock 1.024 MHz (= 16 kHz x 64): the FW2 MEMS mics did NOT output at the
// datasheet-typical 3.072 MHz on this board (measured in microphonearray repo);
// 1.024 MHz matches the known-working movieplayer mic on this hardware.
#define PDM_CLK_HZ     1024000u
```

- [ ] **Step 2: Copy the PIO program verbatim**

```powershell
New-Item -ItemType Directory -Force C:\~prj\Dropbox\vibeProjects\wilibsp\bsp\pdm
Copy-Item C:\~prj\Dropbox\vibeProjects\microphonearray\src\pdm\pdm_capture.pio C:\~prj\Dropbox\vibeProjects\wilibsp\bsp\pdm\pdm_capture.pio
Copy-Item C:\~prj\Dropbox\vibeProjects\microphonearray\src\pdm\pdm_capture.h   C:\~prj\Dropbox\vibeProjects\wilibsp\bsp\pdm\pdm_capture.h
```

No edits to either file. The `.pio`'s `pdm_capture_program_init` derives its clkdiv from `clock_get_hz(clk_sys)` (correct at wilibsp's 250 MHz) and already contains the RP2350 pad-isolation fix (input-enable + ISO clear on both data pins).

- [ ] **Step 3: Create `bsp/pdm/pdm_capture.c` (verbatim + 3 edits)**

Copy from `microphonearray/src/pdm/pdm_capture.c`, then apply exactly these edits — (a) include path + new includes, (b) `pio0`→`pio1` with an ADAPTATION note (same convention as `bsp/radio/gdo_capture.c`), (c) self-contained mic power-on in init. Resulting full file:

```c
// src/pdm/pdm_capture.c — PIO + FREE-RUNNING DMA raw PDM ring -> 4 CICs -> PCM.
// Bit layout per 32-bit ISR word (shift-left, MSB-first): groups of 4 bits
// [sig2_hi, sig1_hi, sig2_lo, sig1_lo] x 8, oldest in the high bits.
// IN_BASE is SIG1 (GPIO29), which is the LSB of each `in pins, 2` sample.
//
// The DMA writes raw PIO words into a power-of-two ring CONTINUOUSLY (address
// wrap, count ~= infinite), so capture never stops and has no per-block gap or
// warmup discard. A single consumer (pdm_capture_pull, on the real-time audio
// core) decimates everything between its read cursor and the DMA's live write
// cursor. This is what makes glitch-free continuous beam audio possible.
#include "pdm/pdm_capture.h"
#include "dsp/cic.h"
#include "platform/board.h"
#include "platform/ioexp.h"
#include "platform/diag.h"
#include "pdm_capture.pio.h"          // generated by pico_generate_pio_header
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "pico/stdlib.h"

// Raw ring: 8192 32-bit words = 32 KiB. Each word = 8 PDM periods; CIC_DECIMATE
// (64) bits -> 1 PCM sample => 8 words per PCM sample per mic, so the ring holds
// 1024 PCM samples (~64 ms @ 16 kHz) of slack before the consumer must catch up.
// MUST be aligned to its byte size for the DMA write-address ring wrap.
#define RAW_WORDS     8192u
#define RAW_RING_BITS 15u             // log2(RAW_WORDS * 4 bytes)
static uint32_t s_raw[RAW_WORDS] __attribute__((aligned(RAW_WORDS * 4)));

static PIO  s_pio = pio1;   // ADAPTATION: microphonearray used pio0; wilibsp
                            // reserves pio0=audio, pio2=radio (re-based to GPIO
                            // 16). pio1 (LEDs) has room: 4+4 instructions, and
                            // GPIO 28-30 are inside its default pin window.
static uint s_sm, s_off, s_dma;
static cic_t s_cic[PDM_NUM_MICS];
static unsigned s_rd;                  // consumer read cursor (word index 0..RAW_WORDS-1)

void pdm_capture_init(void) {
    // ADAPTATION: the source app powered the mics in main(); the BSP driver is
    // self-contained (same normalization as spi_bus_init moving into board_init).
    // Precondition: ioexp_init() has run (board_init() does this).
    ioexp_mic_pwr(true);
    sleep_ms(50);                      // MEMS mic power-on settle

    for (int i = 0; i < PDM_NUM_MICS; i++) cic_init(&s_cic[i]);
    s_sm  = pio_claim_unused_sm(s_pio, true);
    s_off = pio_add_program(s_pio, &pdm_capture_program);
    pdm_capture_program_init(s_pio, s_sm, s_off, PIN_MIC_CLK, PIN_MIC_SIG1);

    s_dma = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config(s_dma);
    channel_config_set_read_increment(&c, false);          // PIO RX FIFO (fixed)
    channel_config_set_write_increment(&c, true);
    channel_config_set_ring(&c, true, RAW_RING_BITS);       // wrap WRITE addr in the ring
    channel_config_set_dreq(&c, pio_get_dreq(s_pio, s_sm, false));
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    // count = 0xFFFFFFFF: with the write-address ring wrap this runs ~9 hours
    // before the count expires (well beyond any session); the ring wrap keeps
    // every write inside s_raw.
    dma_channel_configure(s_dma, &c, s_raw, &s_pio->rxf[s_sm], 0xFFFFFFFFu, true);
    s_rd = 0;
    DIAG("pdm: init pio1 sm=%u dma=%u (4 mics, %u Hz PCM)\n",
         (unsigned)s_sm, (unsigned)s_dma, (unsigned)(PDM_CLK_HZ / CIC_DECIMATE));
}

// Decode one raw word: push its 32 bits to the 4 CICs in line/phase order,
// storing any decimated PCM into out[ch][idx[ch]] up to cap.
static void feed_word(uint32_t w, int16_t* out[PDM_NUM_MICS], unsigned* idx,
                      unsigned cap) {
    for (int g = 0; g < 8; g++) {
        int s1h = (w >> 30) & 1; int s2h = (w >> 31) & 1;   // SIG1=IN_BASE=LSB of each pair
        int s1l = (w >> 28) & 1; int s2l = (w >> 29) & 1;
        w <<= 4;
        int16_t pcm;
        if (cic_push_bit(&s_cic[MIC_A], s1h, &pcm) && idx[MIC_A] < cap) out[MIC_A][idx[MIC_A]++] = pcm;
        if (cic_push_bit(&s_cic[MIC_C], s2h, &pcm) && idx[MIC_C] < cap) out[MIC_C][idx[MIC_C]++] = pcm;
        if (cic_push_bit(&s_cic[MIC_B], s1l, &pcm) && idx[MIC_B] < cap) out[MIC_B][idx[MIC_B]++] = pcm;
        if (cic_push_bit(&s_cic[MIC_D], s2l, &pcm) && idx[MIC_D] < cap) out[MIC_D][idx[MIC_D]++] = pcm;
    }
}

unsigned pdm_capture_pull(int16_t* out[PDM_NUM_MICS], unsigned max) {
    unsigned idx[PDM_NUM_MICS] = {0,0,0,0};
    const uintptr_t base = (uintptr_t)s_raw;
    // Live DMA write cursor (word index within the ring).
    unsigned wr = (unsigned)(((uintptr_t)dma_hw->ch[s_dma].write_addr - base) / 4u);
    while (idx[MIC_A] < max) {
        if (s_rd == wr) {                       // caught up to the DMA — re-sample once
            wr = (unsigned)(((uintptr_t)dma_hw->ch[s_dma].write_addr - base) / 4u);
            if (s_rd == wr) break;              // truly nothing new available
        }
        uint32_t word = s_raw[s_rd];
        s_rd = (s_rd + 1u) & (RAW_WORDS - 1u);
        feed_word(word, out, idx, max);
    }
    return idx[MIC_A];
}

unsigned pdm_capture_block(int16_t* out[PDM_NUM_MICS], unsigned frames) {
    // Blocking convenience wrapper: spin on pull() until `frames` are collected.
    unsigned got = 0;
    while (got < frames) {
        int16_t* dst[PDM_NUM_MICS];
        for (int m = 0; m < PDM_NUM_MICS; m++) dst[m] = out[m] + got;
        got += pdm_capture_pull(dst, frames - got);
    }
    return got;
}
```

- [ ] **Step 4: Wire into `bsp/CMakeLists.txt` and `bsp/fw2.h`**

In `bsp/CMakeLists.txt` add to the `add_library(freewili2_bsp STATIC ...)` list (after the `radio/` block, before `gfx/palette.c`):

```cmake
    pdm/pdm_capture.c
    dsp/cic.c
    dsp/dcblock.c
```

and append after the gdo_capture `pico_generate_pio_header` block:

```cmake
# Generate the PDM capture PIO header into the build tree for pdm_capture.c.
pico_generate_pio_header(freewili2_bsp
    ${CMAKE_CURRENT_SOURCE_DIR}/pdm/pdm_capture.pio)
```

In `bsp/fw2.h` add after the `radio/` includes:

```c
#include "pdm/pdm_capture.h"       // (Increment 2: PDM mics)
#include "dsp/cic.h"               // (Increment 2: PDM mics)
#include "dsp/dcblock.h"           // (Increment 2: PDM mics)
```

- [ ] **Step 5: Verify it compiles (target build)**

Run: `python tools/fw.py build hello_display`
Expected: build succeeds. (No app calls `pdm_capture_init` yet; this proves the PIO header generates and the driver compiles against the 250 MHz board.)

- [ ] **Step 6: Commit**

```bash
git add bsp/pdm/ bsp/platform/board.h bsp/CMakeLists.txt bsp/fw2.h
git commit -m "feat(pdm): harvest 4-mic PDM capture (pio1 + IRQ-free ring DMA) from microphonearray"
```

---

### Task 5: `apps/hello_mics` demo app

**Files:**
- Create: `apps/hello_mics/CMakeLists.txt`, `apps/hello_mics/main.c`, `apps/hello_mics/README.md`
- Modify: `CMakeLists.txt` (repo root — `add_subdirectory(apps/hello_mics)`)

**Interfaces:**
- Consumes: `pdm_capture_init`/`pdm_capture_block`/`PDM_NUM_MICS`/`MIC_A..D` (Task 4), `dcblock_inplace` (Task 2), `board_init`, `DIAG`.
- Produces: the on-hardware verification vehicle for Task 7.

- [ ] **Step 1: Create `apps/hello_mics/CMakeLists.txt`**

```cmake
add_executable(hello_mics main.c)
target_link_libraries(hello_mics freewili2_bsp)
pico_set_binary_type(hello_mics copy_to_ram)
pico_add_extra_outputs(hello_mics)
```

- [ ] **Step 2: Create `apps/hello_mics/main.c`**

```c
// hello_mics — on-hardware smoke test for the 4-channel PDM microphone driver.
// RTT-only (fw rtt), no display: prints per-mic RMS + peak a few times a second.
// Pass criteria: all 4 channels show low ambient at rest; tapping/speaking at
// each physical mic position (left-to-right order D, B, A, C) spikes THAT
// channel; no channel stuck at 0 / pure DC (pad-ISO or mic-power regression).
#include "fw2.h"
#include "platform/diag.h"
#include "pico/stdlib.h"

#define BLOCK 1600u   // 100 ms of 16 kHz PCM per pull

static int16_t s_pcm[PDM_NUM_MICS][BLOCK];

// Integer sqrt (no floats in DIAG, invariant 3; keep the whole path integer).
static uint32_t isqrt32(uint32_t x) {
    uint32_t r = 0, b = 1u << 30;
    while (b > x) b >>= 2;
    while (b) {
        if (x >= r + b) { x -= r + b; r = (r >> 1) + b; }
        else r >>= 1;
        b >>= 2;
    }
    return r;
}

int main(void) {
    board_init();   // 250 MHz + clk_peri re-source + I2C1 + ioexp_init
    DIAG("\n=== hello_mics: 4-channel PDM microphone smoke test ===\n");
    DIAG("physical mic order left->right: D, B, A, C\n");

    pdm_capture_init();   // mic power on (+50 ms), pio1 SM + free-running DMA

    static const char* name = "ABCD";
    unsigned blocks = 0;
    while (1) {
        int16_t* dst[PDM_NUM_MICS] = { s_pcm[0], s_pcm[1], s_pcm[2], s_pcm[3] };
        pdm_capture_block(dst, BLOCK);
        if (++blocks % 3 != 0) continue;   // print ~3x/s (every 3rd 100 ms block)

        for (int m = 0; m < PDM_NUM_MICS; m++) {
            dcblock_inplace(s_pcm[m], BLOCK);
            uint64_t sumsq = 0;
            int peak = 0;
            for (unsigned i = 0; i < BLOCK; i++) {
                int v = s_pcm[m][i];
                if (v < 0) v = -v;
                if (v > peak) peak = v;
                sumsq += (uint64_t)((int64_t)s_pcm[m][i] * s_pcm[m][i]);
            }
            unsigned rms = (unsigned)isqrt32((uint32_t)(sumsq / BLOCK));
            DIAG("mic %c: rms=%5u peak=%5d   ", name[m], rms, peak);
        }
        DIAG("\n");
    }
}
```

- [ ] **Step 3: Create `apps/hello_mics/README.md`**

```markdown
# hello_mics

On-hardware smoke test for the 4-channel PDM microphone driver
(`bsp/pdm/pdm_capture`). RTT-only — no display.

    fw build hello_mics
    fw flash hello_mics
    fw rtt

Prints per-mic RMS + peak (after DC-blocking) ~3x/s. Expected:

- All 4 channels alive with low ambient noise at rest.
- Tapping / speaking directly at each mic position spikes that channel.
  Physical left-to-right order is **D, B, A, C** (bench-measured in the
  source `microphonearray` repo), not A-D.
- A channel stuck at rms=0 (or frozen values) indicates the RP2350
  pad-isolation trap or the MIC_PWR expander rail regressed.
```

- [ ] **Step 4: Register the app in the top-level `CMakeLists.txt`**

Find the existing `add_subdirectory(apps/...)` lines in the repo-root `CMakeLists.txt` and add, in alphabetical order with the others:

```cmake
add_subdirectory(apps/hello_mics)
```

- [ ] **Step 5: Build**

Run: `python tools/fw.py build hello_mics`
Expected: `hello_mics.elf` builds. Also run `python tools/fw.py test` — all 10 host tests still pass.

- [ ] **Step 6: Commit**

```bash
git add apps/hello_mics/ CMakeLists.txt
git commit -m "feat(pdm): add hello_mics RTT smoke-test app"
```

---

### Task 6: Documentation

**Files:**
- Modify: `docs/hardware/catalog.md` (PDM row TODO → DONE)
- Modify: `docs/hardware/pinmap.md` (mic pins → verified table)
- Modify: `docs/hardware/facts.md` (PDM facts section)
- Create: `docs/drivers/pdm.md`

**Interfaces:** none (docs only) — but keep every claim consistent with the code landed in Tasks 1-5.

- [ ] **Step 1: `docs/hardware/catalog.md`**

Remove the PDM row from the TODO table and add to the DONE table (after the CC1101 row):

```markdown
| PDM microphones — 4-mic array | `bsp/pdm/pdm_capture.{c,h}`, `bsp/pdm/pdm_capture.pio`, `bsp/dsp/{cic,dcblock}.{c,h}` | `pio1` (shared with LEDs), MIC_CLK=28 / SIG1=29 / SIG2=30, 1.024 MHz PDM → 16 kHz int16 PCM ×4 via integer CIC; free-running ring DMA, **no IRQ**. Mic power via `ioexp_mic_pwr()` (P1_7), driven by `pdm_capture_init()`. Harvested from local `microphonearray` (supersedes the earlier `usbcamfw`/`wili8c` pointer). Demo: `apps/hello_mics`. |
```

Also update the two "exactly six peripherals" prose blocks (intro of DONE section and the "Confirming this catalog" section) to seven, adding `pdm/*.c` + `dsp/*.c` to the source-list description.

- [ ] **Step 2: `docs/hardware/pinmap.md`**

Move the three MIC rows out of the "Broader board inventory" table into the "Verified (`bsp/platform/board.h`)" table:

```markdown
| `PIN_MIC_CLK` | 28 | PDM mic array (shared clock out) | `pio1` side-set, 1.024 MHz (NOT 3.072 — see facts.md) |
| `PIN_MIC_SIG1` | 29 | PDM data line 1 | Mic A (clk-high) + Mic B (clk-low); consecutive with SIG2 for `in pins, 2` |
| `PIN_MIC_SIG2` | 30 | PDM data line 2 | Mic C (clk-high) + Mic D (clk-low) |
```

- [ ] **Step 3: `docs/hardware/facts.md`**

Append a new section (match the file's existing heading style — read it first):

```markdown
## PDM microphones (increment 2, 2026-07-04)

- **PDM clock is 1.024 MHz, not the datasheet-typical 3.072 MHz.** The FW2
  MEMS mics did not output data at 3.072 MHz on this board (measured in the
  `microphonearray` repo; matches the known-working movieplayer mic). 1.024 MHz
  × CIC decimate 64 → 16 kHz PCM. `PDM_CLK_HZ` lives in `bsp/platform/board.h`.
- **RP2350 pad-isolation trap:** input pads power up with the input buffer
  disabled AND the ISO latch (PADS bit 8) engaged — the PIO reads stuck-0 and
  the PDM stream decimates to pure DC. `pdm_capture.pio`'s init explicitly
  enables the input buffers and clears ISO on GPIO 29/30.
- **Mic power is PCAL6524 P1 bit 7 (`MIC_PWR`, active-high)** — off at
  power-on and after `ioexp_init()`; `pdm_capture_init()` drives it on via
  `ioexp_mic_pwr(true)` and waits 50 ms.
- **Physical left-to-right mic order is D, B, A, C** (bench-measured phase
  ramp in the `microphonearray` repo), not A, B, C, D. Channel indices MIC_A..D
  are line/phase order: SIG1-high, SIG1-low, SIG2-high, SIG2-low.
- **Overrun semantics:** the capture DMA free-runs into a 32 KiB ring
  (~64 ms of slack). A consumer stalled longer than that silently loses/tears
  the stalled span — there is no overrun flag. Single consumer only (shared
  CIC state).
```

- [ ] **Step 4: Create `docs/drivers/pdm.md`**

```markdown
# PDM microphone driver (`bsp/pdm/`, `bsp/dsp/`)

4-channel PDM MEMS microphone capture: one PIO state machine (pio1) drives the
shared 1.024 MHz mic clock (GPIO 28) and samples two data lines (GPIO 29/30)
mid-phase on both clock levels — 2 mics per line × 2 lines. A free-running
DMA channel (write-address ring, **no IRQ**) streams raw PDM words into a
32 KiB ring; `pdm_capture_pull()` decimates through four integer 3rd-order
CIC filters (R=64) to gap-free 16 kHz int16 PCM per mic.

**These are NOT the I2S codec mic** (3.5 mm jack / NAU88C10, `bsp/audio/`) —
different hardware, different driver.

## API (`pdm/pdm_capture.h`)

- `pdm_capture_init()` — powers the mics (`ioexp_mic_pwr(true)` + 50 ms),
  claims a pio1 SM + one DMA channel, starts free-running capture.
  Precondition: `ioexp_init()` has run (`board_init()` does).
- `pdm_capture_pull(int16_t* out[PDM_NUM_MICS], unsigned max)` — non-blocking;
  decimates everything new, returns PCM frames written per mic (0 if none).
- `pdm_capture_block(out, frames)` — blocking convenience wrapper.

**Single consumer, one core** (shared CIC state). If the consumer stalls
> ~64 ms the ring is silently overwritten (no flag) — see facts.md.

## DSP helpers (`dsp/`)

- `dsp/cic.h` — 3rd-order CIC decimator, pure integer, host-tested
  (`tests/test_cic.c`). Full-scale DC plateaus at ±16384 (6 dB headroom).
- `dsp/dcblock.h` — one-pole DC-blocking high-pass (R=0.90, ~250 Hz cutoff
  @ 16 kHz), host-tested. The PDM idle stream is heavily DC-biased: run this
  on every block before RMS/FFT work. Analysis path only — not inside the CIC.

## Channel ↔ position map

`MIC_A..MIC_D` are line/phase indices (SIG1-high, SIG1-low, SIG2-high,
SIG2-low). Physical left-to-right order on the board is **D, B, A, C**
(bench-measured). Spatial DSP (DOA/beamforming — app domain, not BSP) must
remap; see the `microphonearray` repo's `PHYS[]` table.

## Demo

`apps/hello_mics` — per-mic RMS/peak over RTT; see its README for pass
criteria.
```

- [ ] **Step 5: Commit**

```bash
git add docs/hardware/catalog.md docs/hardware/pinmap.md docs/hardware/facts.md docs/drivers/pdm.md
git commit -m "docs(pdm): mark PDM mics DONE; record clock/pad-ISO/mic-order/power facts"
```

---

### Task 7: On-hardware verification + findings

**Files:**
- Create: `docs/superpowers/findings/2026-07-04-pdm-mics-e2e.md`

**Interfaces:**
- Consumes: `apps/hello_mics` (Task 5), a FreeWili 2 attached via the cmsis-dap probe.

- [ ] **Step 1: Flash and watch RTT**

```bash
python tools/fw.py flash hello_mics
python tools/fw.py rtt
```

Expected RTT output: the banner, `ioexp: MIC_PWR (P1_7) -> 1`, `pdm: init pio1 sm=<n> dma=<n> (4 mics, 16000 Hz PCM)`, then per-mic `rms=/peak=` lines ~3×/s.

- [ ] **Step 2: Pass criteria (needs a human or at least ambient sound)**

1. All 4 channels report nonzero, low ambient RMS at rest (not stuck at 0, not pegged).
2. Tapping/speaking at each physical mic position (left→right = D, B, A, C) spikes that channel distinctly.
3. Values recover to ambient after the stimulus.

If no hardware is attached (flash fails to find the probe), record the increment as **implemented, hardware verification pending** and stop — do not fake results.

- [ ] **Step 3: Write the findings doc**

`docs/superpowers/findings/2026-07-04-pdm-mics-e2e.md`, same shape as
`2026-07-04-cc1101-radio-e2e.md`: what was flashed, exact RTT observations
(numbers), pass/fail per criterion, any bugs found + fixes committed.

- [ ] **Step 4: Commit**

```bash
git add docs/superpowers/findings/2026-07-04-pdm-mics-e2e.md
git commit -m "docs(pdm): record PDM mics on-hardware E2E results"
```

---

## Completion

After Task 7: run the full gate one more time (`python tools/fw.py test`, `python tools/fw.py build hello_mics`, `python tools/fw.py build hello_display`), then use superpowers:finishing-a-development-branch (review → merge `feat/pdm-mics` into `master`, delete branch), and update the auto-memory status note for the harvest effort.
