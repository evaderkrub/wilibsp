# I2S Full-Duplex Audio Driver Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a full-duplex I2S audio capability to `freewili2_bsp` — play PCM out the NAU88C10 codec (speaker or 3.5 mm jack) while capturing the codec ADC (mic) on one I2S bus — plus a `hello_audio` demo/smoke-test app.

**Architecture:** Harvest the proven, MIT-licensed driver from `evaderkrub/freewili2-fullduplex-audio` (same codebase lineage as this repo) near-verbatim into `bsp/audio/`. One PIO0 SM0 clocks the codec and shifts DAC-out (GPIO5) + ADC-in (GPIO4) in the same loop; TX playback is a zero-CPU chained-DMA read-ring; RX capture is a ping-pong DMA (generalized here from the source's VU-only `vu_capture` to a reusable PCM-block API). The codec is configured over I2C1 @ 0x1A.

**Tech Stack:** C11, Raspberry Pi Pico SDK 2.x (RP2350B), CMake, PIO, DMA, PWM, I2C; SEGGER RTT diagnostics; standalone host CTest for pure DSP.

**Spec:** `docs/superpowers/specs/2026-07-01-i2s-audio-driver-design.md`

## Global Constraints

- **Board select is CMake-only:** never pass `-DPICO_BOARD` on the command line (top-level `CMakeLists.txt` sets `PICO_BOARD freewili2`).
- **Clock is 250 MHz, `board.c` untouched:** the audio driver derives MCLK PWM + PIO clkdiv from `clock_get_hz(clk_sys)` at runtime, so it runs at the repo's proven 250 MHz / vreg 1.25 V with no edit. Ignore the source repo's 153.6 MHz board choice.
- **Diagnostics = `DIAG()` (SEGGER RTT) only:** no `printf`/USB/UART stdio; RTT `%d %u %x %s %c` only, **no float formatting**. (Float *computation* in `tone_gen`/`vu_meter` is fine; just never `DIAG` a float.)
- **`DMA_IRQ_0` is a SHARED line:** any DMA IRQ user must call `irq_add_shared_handler(DMA_IRQ_0, …)` and act only on its own channel status. Never `irq_set_exclusive_handler(DMA_IRQ_0, …)`.
- **copy_to_ram / 512 KB SRAM budget:** every app is `pico_set_binary_type(<app> copy_to_ram)`; large buffers go to PSRAM. (This driver's buffers are ≤ 2 KB — fine in SRAM.)
- **Harvested drivers keep their proven names** (`codec_nau88c10_*`, `audio_i2s_duplex_*`, `tone_gen_*`, `vu_*`); `fw2.h` is the umbrella include.
- **Build env (Windows):** `fw build/flash/rtt` need the Pico SDK env in scope:
  ```bash
  export PICO_SDK_PATH="$HOME/.pico-sdk/sdk/2.2.0"
  export PICO_TOOLCHAIN_PATH="$HOME/.pico-sdk/toolchain/14_2_Rel1"
  export PATH="$HOME/.pico-sdk/picotool/2.2.0-a4/picotool:$PATH"
  ```
  `fw test` needs none of this (standalone host project: MinGW GCC + Ninja).

---

## Prep: ensure the harvest source is available

Several tasks copy files verbatim from the source repo. Ensure it is cloned to a
stable path (re-clone if the scratchpad copy was reaped). All harvest `cp`
commands below use this variable:

```bash
export SRC="C:/Users/dave/AppData/Local/Temp/claude/C---prj-Dropbox-vibeProjects-wilibsp/750b5d5d-0751-434d-9028-cdc087bca06f/scratchpad/freewili2-fullduplex-audio"
[ -f "$SRC/src/audio/audio_i2s_duplex.c" ] || git clone --depth 1 https://github.com/evaderkrub/freewili2-fullduplex-audio.git "$SRC"
```

Run from the repo root: `cd "C:/~prj/Dropbox/vibeProjects/wilibsp"`.

## File Structure

**New — `bsp/audio/` (driver + DSP):**
- `tone_gen.{c,h}` — pure sine generator (host-tested). *verbatim*
- `vu_meter.{c,h}` — pure VU math: sample extract, peak, bar px, color (host-tested). *verbatim*
- `codec_nau88c10.{c,h}` — NAU88C10 bring-up + speaker/headphone routing over I2C1. *verbatim*
- `audio_i2s_duplex.{c,h}` — PIO0 SM0 engine: init, play_loop, play_stop, RX FIFO/DREQ accessors. *verbatim*
- `i2s_duplex.pio` — full-duplex I2S PIO program. *verbatim*
- `audio_capture.{c,h}` — **new/modified:** ping-pong RX DMA → latest PCM block + ready flag; SHARED `DMA_IRQ_0`. Generalized from source `vu_capture`.

**New — app + tests:**
- `apps/hello_audio/{main.c,CMakeLists.txt}` — demo / on-hardware smoke test.
- `tests/test_tone_gen.c`, `tests/test_vu_meter.c` — host CTest.

**Modified:**
- `bsp/platform/board.h` — add five `PIN_AUDIO_*` defines.
- `bsp/CMakeLists.txt` — add audio sources, PIO header gen, `hardware_pwm`.
- `bsp/fw2.h` — add audio includes.
- `tests/CMakeLists.txt` — add the two host tests.
- `CMakeLists.txt` (top) — `add_subdirectory(apps/hello_audio)`.
- `docs/hardware/catalog.md`, `docs/hardware/pinmap.md` — status/pin updates.
- `docs/drivers/audio.md` — **new** usage doc.

---

### Task 1: Pure DSP modules + host tests (no SDK, no hardware)

Harvest the two pure DSP files and cover them with the repo's standalone host CTest. This task is fully verifiable with `fw test` alone.

**Files:**
- Create: `bsp/audio/tone_gen.c`, `bsp/audio/tone_gen.h` (verbatim from `$SRC`)
- Create: `bsp/audio/vu_meter.c`, `bsp/audio/vu_meter.h` (verbatim from `$SRC`)
- Create: `tests/test_tone_gen.c`, `tests/test_vu_meter.c`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces (from `tone_gen.h`): `void tone_gen_fill(int16_t* buf, unsigned n, float hz, float fs, float* phase);`
- Produces (from `vu_meter.h`): `int16_t vu_sample(uint32_t frame, int slot);` · `uint16_t vu_peak(const uint32_t *frames, uint32_t n, int slot);` · `int vu_bar_px(uint16_t peak, int max_px);` · `uint16_t vu_color_be(uint16_t peak);` · macros `VU_YELLOW_PEAK 8192`, `VU_RED_PEAK 24576`, `VU_DB_FLOOR -48`.

- [ ] **Step 1: Harvest the four DSP files verbatim**

```bash
mkdir -p bsp/audio
cp "$SRC/src/audio/tone_gen.c" bsp/audio/tone_gen.c
cp "$SRC/src/audio/tone_gen.h" bsp/audio/tone_gen.h
cp "$SRC/src/audio/vu_meter.c" bsp/audio/vu_meter.c
cp "$SRC/src/audio/vu_meter.h" bsp/audio/vu_meter.h
```

- [ ] **Step 2: Write the tone_gen host test**

Create `tests/test_tone_gen.c`:

```c
#include "test_util.h"
#include "audio/tone_gen.h"

int main(void) {
    // 65 samples of a 1 kHz sine at 16 kHz (64 = 4 whole periods, +1 to check wrap).
    int16_t buf[65];
    float ph = 0.0f;
    tone_gen_fill(buf, 65, 1000.0f, 16000.0f, &ph);

    // Peak amplitude is ~28000 scale, minus the half-step sample-center offset.
    int pk = 0;
    for (int i = 0; i < 64; i++) { int a = buf[i] < 0 ? -buf[i] : buf[i]; if (a > pk) pk = a; }
    ASSERT_TRUE(pk > 27000 && pk <= 28000);

    // 4 periods over 64 samples => 8 zero crossings.
    int zc = 0;
    for (int i = 1; i < 64; i++) if ((buf[i-1] < 0) != (buf[i] < 0)) zc++;
    ASSERT_EQ(zc, 8);

    // Phase-continuous wrap: sample 64 ~= sample 0 within int16 rounding.
    int d = buf[64] - buf[0]; if (d < 0) d = -d;
    ASSERT_TRUE(d <= 2);

    // Near-zero DC over a whole number of periods.
    long sum = 0;
    for (int i = 0; i < 64; i++) sum += buf[i];
    ASSERT_TRUE(sum > -2000 && sum < 2000);

    TEST_RETURN();
}
```

- [ ] **Step 3: Write the vu_meter host test**

Create `tests/test_vu_meter.c`:

```c
#include "test_util.h"
#include "audio/vu_meter.h"

int main(void) {
    // slot 0 = left = high 16 bits; slot 1 = right = low 16 bits.
    uint32_t f = 0x1234ABCDu;
    ASSERT_EQ((int16_t)vu_sample(f, 0), (int16_t)0x1234);
    ASSERT_EQ((int16_t)vu_sample(f, 1), (int16_t)0xABCD);

    // Right-slot values: 0x0064=+100, 0x0FA0=+4000, 0xF060=-4000, 0x0000=0 -> peak 4000.
    uint32_t frames[4] = { 0x00000064u, 0x00000FA0u, 0x0000F060u, 0x00000000u };
    ASSERT_EQ(vu_peak(frames, 4, 1), 4000);

    // Silence -> 0.
    uint32_t z[2] = { 0, 0 };
    ASSERT_EQ(vu_peak(z, 2, 1), 0);

    // Bar length: 0 -> 0, full-scale -> max_px.
    ASSERT_EQ(vu_bar_px(0, 100), 0);
    ASSERT_EQ(vu_bar_px(32767, 100), 100);

    // Color thresholds (big-endian RGB565): green < 8192 <= yellow < 24576 <= red.
    ASSERT_EQ(vu_color_be(100),   (uint16_t)((0x07E0u >> 8) | (0x07E0u << 8)));
    ASSERT_EQ(vu_color_be(10000), (uint16_t)((0xFFE0u >> 8) | (0xFFE0u << 8)));
    ASSERT_EQ(vu_color_be(30000), (uint16_t)((0xF800u >> 8) | (0xF800u << 8)));

    TEST_RETURN();
}
```

- [ ] **Step 4: Register both tests in the host CTest tree**

Append to `tests/CMakeLists.txt` (after the existing `test_led_color` block). `vu_meter.c`/`tone_gen.c` use `sinf`/`log10f`, so link `m` (a no-op stub on MinGW, real libm on Linux — cross-platform safe):

```cmake
add_executable(test_tone_gen
    test_tone_gen.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../bsp/audio/tone_gen.c)
target_include_directories(test_tone_gen PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../bsp
    ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_tone_gen m)
add_test(NAME tone_gen COMMAND test_tone_gen)

add_executable(test_vu_meter
    test_vu_meter.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../bsp/audio/vu_meter.c)
target_include_directories(test_vu_meter PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../bsp
    ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_vu_meter m)
add_test(NAME vu_meter COMMAND test_vu_meter)
```

- [ ] **Step 5: Run the host tests — verify they pass**

Run (from repo root): `python tools/fw.py test`
Expected: CTest reports **3/3** passing — `led_color`, `tone_gen`, `vu_meter` (100% tests passed). If `fw test` reconfigures, that's expected on first run.

- [ ] **Step 6: Commit**

```bash
git add bsp/audio/tone_gen.c bsp/audio/tone_gen.h bsp/audio/vu_meter.c bsp/audio/vu_meter.h \
        tests/test_tone_gen.c tests/test_vu_meter.c tests/CMakeLists.txt
git commit -m "feat: harvest tone_gen + vu_meter DSP with host tests"
```

---

### Task 2: Harvest the I2S engine + codec + board pins (target compiles)

Copy the hardware-bound engine, codec, and PIO program verbatim; add the audio pin defines; wire them into the BSP library. Verified by a clean target build.

**Files:**
- Create: `bsp/audio/audio_i2s_duplex.c`, `bsp/audio/audio_i2s_duplex.h`, `bsp/audio/i2s_duplex.pio` (verbatim from `$SRC`)
- Create: `bsp/audio/codec_nau88c10.c`, `bsp/audio/codec_nau88c10.h` (verbatim from `$SRC`)
- Modify: `bsp/platform/board.h` (add `PIN_AUDIO_*`)
- Modify: `bsp/CMakeLists.txt` (sources, PIO header, `hardware_pwm`)
- Modify: `bsp/fw2.h` (includes)

**Interfaces:**
- Produces (from `audio_i2s_duplex.h`): `void audio_i2s_duplex_init(uint32_t sample_rate);` · `volatile const void *audio_i2s_duplex_rxf(void);` · `uint audio_i2s_duplex_rx_dreq(void);` · `void audio_i2s_duplex_play_loop(const uint32_t *buf, uint frames);` · `void audio_i2s_duplex_play_stop(void);`
- Produces (from `codec_nau88c10.h`): `void codec_nau88c10_init(void);` · `void codec_nau88c10_dac_mute(bool);` · `void codec_nau88c10_set_output(codec_out_t);` · `void codec_nau88c10_log_output(void);` · `bool codec_nau88c10_input_ok(void);` · enum `codec_out_t { CODEC_OUT_SPEAKER=0, CODEC_OUT_HEADPHONE=1 }`.
- Produces (from `board.h`): `PIN_AUDIO_DATA 5`, `PIN_AUDIO_DIN 4`, `PIN_AUDIO_LRCK 6`, `PIN_AUDIO_BCLK 7`, `PIN_AUDIO_MCLK 22`.
- Consumes: `PIN_I2C1_SDA/SCL` (already in `board.h`); `DIAG()` (`platform/diag.h`).

- [ ] **Step 1: Harvest the engine, codec, and PIO verbatim**

```bash
cp "$SRC/src/audio/audio_i2s_duplex.c" bsp/audio/audio_i2s_duplex.c
cp "$SRC/src/audio/audio_i2s_duplex.h" bsp/audio/audio_i2s_duplex.h
cp "$SRC/src/audio/i2s_duplex.pio"    bsp/audio/i2s_duplex.pio
cp "$SRC/src/audio/codec_nau88c10.c"  bsp/audio/codec_nau88c10.c
cp "$SRC/src/audio/codec_nau88c10.h"  bsp/audio/codec_nau88c10.h
```

- [ ] **Step 2: Add the audio pin defines to `board.h`**

In `bsp/platform/board.h`, insert this block immediately **after** the `PIN_I2C1_SCL 27` line (the `--- I2C1 (touch, sensors) ---` section):

```c

// --- I2S audio (NAU88C10 codec; pins from FW2Display_pin_definitions.h) ---
#define PIN_AUDIO_DATA 5    // SPK_DIN:  I2S data into codec (PIO out / DAC)
#define PIN_AUDIO_DIN  4    // SPK_DOUT: codec ADC data into MCU (PIO in)
#define PIN_AUDIO_LRCK 6    // SPK_LRCK: I2S word clock  (PIO sideset bit 0)
#define PIN_AUDIO_BCLK 7    // SPK_BCLK: I2S bit clock   (PIO sideset bit 1)
#define PIN_AUDIO_MCLK 22   // SPK_MCLK: 256*fs square wave from PWM
```

- [ ] **Step 3: Wire the sources into `bsp/CMakeLists.txt`**

In `bsp/CMakeLists.txt`, add the four new `.c` files to the `add_library(freewili2_bsp STATIC …)` list (place after `leds/led_ui.c`, before the `gfx/palette.c` / `third_party` lines):

```cmake
    audio/tone_gen.c
    audio/vu_meter.c
    audio/codec_nau88c10.c
    audio/audio_i2s_duplex.c
```

Add `hardware_pwm` to the `target_link_libraries(freewili2_bsp PUBLIC …)` list (the MCLK generator uses PWM; `hardware_pio`/`hardware_dma`/`hardware_i2c` are already linked):

```cmake
    hardware_pwm
```

Add a PIO header generation call next to the existing WS2812 one (near the bottom of the file):

```cmake
pico_generate_pio_header(freewili2_bsp
    ${CMAKE_CURRENT_SOURCE_DIR}/audio/i2s_duplex.pio)
```

- [ ] **Step 4: Activate the includes in `bsp/fw2.h`**

In `bsp/fw2.h`, add these lines after the existing `#include "leds/ws2812_driver.h"` line (before `#endif`):

```c
#include "audio/tone_gen.h"          // (Task 1)
#include "audio/vu_meter.h"          // (Task 1)
#include "audio/codec_nau88c10.h"    // (Task 2)
#include "audio/audio_i2s_duplex.h"  // (Task 2)
```

- [ ] **Step 5: Build the target — verify the library compiles with the new sources**

Run (with the Pico SDK env exported — see Global Constraints):
`python tools/fw.py build hello_display`
Expected: configures and builds to completion; `freewili2_bsp` compiles `audio/codec_nau88c10.c` and `audio/audio_i2s_duplex.c` (and generates `i2s_duplex.pio.h`) with **no errors**; `build/apps/hello_display/hello_display.elf` is produced. (`hello_display` doesn't use the audio API yet — this only proves the library builds.)

- [ ] **Step 6: Commit**

```bash
git add bsp/audio/audio_i2s_duplex.c bsp/audio/audio_i2s_duplex.h bsp/audio/i2s_duplex.pio \
        bsp/audio/codec_nau88c10.c bsp/audio/codec_nau88c10.h \
        bsp/platform/board.h bsp/CMakeLists.txt bsp/fw2.h
git commit -m "feat: harvest I2S duplex engine + NAU88C10 codec, add audio pins"
```

---

### Task 3: Generalized PCM-block capture API

Replace the source's VU-only `vu_capture` with a reusable capture module that hands out the latest raw PCM block, and fix the `DMA_IRQ_0` handler to be shared (the one required behavioral change vs. the source).

**Files:**
- Create: `bsp/audio/audio_capture.c`, `bsp/audio/audio_capture.h`
- Modify: `bsp/CMakeLists.txt` (add source)
- Modify: `bsp/fw2.h` (add include)

**Interfaces:**
- Consumes: `audio_i2s_duplex_rxf()`, `audio_i2s_duplex_rx_dreq()` (Task 2).
- Produces (from `audio_capture.h`): `void audio_capture_start(void);` · `bool audio_capture_block_ready(void);` · `const uint32_t *audio_capture_block(void);` · macros `AUDIO_CAPTURE_BLOCK_FRAMES 256`, `AUDIO_MIC_I2S_SLOT 1`.

- [ ] **Step 1: Write the capture header**

Create `bsp/audio/audio_capture.h`:

```c
// bsp/audio/audio_capture.h — free-running ping-pong DMA from the I2S RX FIFO into
// PCM block buffers. Generalized from the source repo's vu_capture (peak-only) to
// expose the latest completed 32-bit-frame [L16|R16] PCM block to any consumer.
// The VU meter is one such consumer (vu_peak() in vu_meter.h).
#ifndef AUDIO_CAPTURE_H
#define AUDIO_CAPTURE_H
#include <stdint.h>
#include <stdbool.h>

#define AUDIO_CAPTURE_BLOCK_FRAMES 256   // ~16 ms at 16 kHz
#define AUDIO_MIC_I2S_SLOT         1     // right slot: the mono NAU88C10 ADC
                                         // streams here (left slot reads silent 0)

// audio_i2s_duplex_init() must already be running (RX FIFO live). Claims two DMA
// channels chained ping-pong into two block buffers and installs a SHARED handler
// on DMA_IRQ_0 (the ST7796 flush also shares this line — see board.h invariant).
void audio_capture_start(void);

// True once a new block has completed since the last audio_capture_block().
bool audio_capture_block_ready(void);

// Pointer to the most-recently-completed block: AUDIO_CAPTURE_BLOCK_FRAMES frames
// of uint32 [L16|R16]. Clears the ready flag. Returns NULL if no block is ready.
const uint32_t *audio_capture_block(void);

#endif // AUDIO_CAPTURE_H
```

- [ ] **Step 2: Write the capture implementation**

Create `bsp/audio/audio_capture.c`. This is the source's `vu_capture.c` mechanism (two chained DMA channels ping-ponging the RX FIFO into two SRAM buffers, IRQ flags the filled one) with two changes: the public API returns the raw block instead of a peak, and the IRQ is a **shared** handler on `DMA_IRQ_0`:

```c
// bsp/audio/audio_capture.c — two DMA channels ping-pong frames from the I2S RX
// FIFO into two block buffers. The completion IRQ (SHARED on DMA_IRQ_0) flags the
// just-filled buffer; audio_capture_block() hands it to the consumer. Generalized
// from the source repo's vu_capture.c (which exposed only a per-block peak).
#include "audio/audio_capture.h"
#include "audio/audio_i2s_duplex.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include <stddef.h>

static uint32_t s_buf[2][AUDIO_CAPTURE_BLOCK_FRAMES];
static int s_dma[2];
static volatile int s_done = -1;    // index of a freshly filled buffer, or -1
static volatile bool s_ready = false;

static void start_channel(int ch, int other_ch, uint32_t *dst) {
    dma_channel_config c = dma_channel_get_default_config(ch);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, true);
    channel_config_set_dreq(&c, audio_i2s_duplex_rx_dreq());
    channel_config_set_chain_to(&c, other_ch);   // ping-pong
    dma_channel_configure(ch, &c, dst, audio_i2s_duplex_rxf(),
                          AUDIO_CAPTURE_BLOCK_FRAMES, false);
}

static void dma_irq(void) {
    for (int i = 0; i < 2; i++) {
        if (dma_channel_get_irq0_status(s_dma[i])) {
            dma_channel_acknowledge_irq0(s_dma[i]);
            // Rearm this channel for its next turn (the other is now running).
            dma_channel_set_write_addr(s_dma[i], s_buf[i], false);
            s_done = i;
            s_ready = true;
            break;
        }
    }
}

void audio_capture_start(void) {
    s_dma[0] = dma_claim_unused_channel(true);
    s_dma[1] = dma_claim_unused_channel(true);
    start_channel(s_dma[0], s_dma[1], s_buf[0]);
    start_channel(s_dma[1], s_dma[0], s_buf[1]);

    dma_channel_set_irq0_enabled(s_dma[0], true);
    dma_channel_set_irq0_enabled(s_dma[1], true);
    // SHARED handler: the ST7796 flush already owns a shared handler on DMA_IRQ_0
    // and each handler acts only on its own channel's status (board.h invariant).
    irq_add_shared_handler(DMA_IRQ_0, dma_irq,
                           PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY);
    irq_set_enabled(DMA_IRQ_0, true);

    dma_channel_start(s_dma[0]);   // channel 1 is chained from channel 0
}

bool audio_capture_block_ready(void) { return s_ready; }

const uint32_t *audio_capture_block(void) {
    if (!s_ready) return NULL;
    int idx = s_done;
    s_ready = false;
    if (idx < 0) return NULL;
    return s_buf[idx];
}
```

- [ ] **Step 3: Add the source to `bsp/CMakeLists.txt`**

In `bsp/CMakeLists.txt`, add to the `add_library(freewili2_bsp STATIC …)` list, immediately after the `audio/audio_i2s_duplex.c` line from Task 2:

```cmake
    audio/audio_capture.c
```

- [ ] **Step 4: Activate the include in `bsp/fw2.h`**

In `bsp/fw2.h`, add after the `#include "audio/audio_i2s_duplex.h"` line:

```c
#include "audio/audio_capture.h"     // (Task 3)
```

- [ ] **Step 5: Build the target — verify it compiles**

Run (with the Pico SDK env exported): `python tools/fw.py build hello_display`
Expected: `freewili2_bsp` now also compiles `audio/audio_capture.c` with no errors; `hello_display.elf` is produced. (Compile-only check — `audio_capture` links but isn't called yet.)

- [ ] **Step 6: Commit**

```bash
git add bsp/audio/audio_capture.c bsp/audio/audio_capture.h bsp/CMakeLists.txt bsp/fw2.h
git commit -m "feat: add generalized PCM-block audio capture (shared DMA_IRQ_0)"
```

---

### Task 4: `hello_audio` demo app

Build the demo that plays a 1 kHz tone (speaker → headphone jack) while running a live mic VU bar — the on-hardware smoke test for the increment.

**Files:**
- Create: `apps/hello_audio/main.c`, `apps/hello_audio/CMakeLists.txt`
- Modify: `CMakeLists.txt` (top-level)

**Interfaces:**
- Consumes: `board_init()`, `board_backlight_set()`, `st7796_init/fill_screen/fill_rect/draw_text()`, `codec_nau88c10_init/input_ok/dac_mute/set_output/log_output()`, `audio_i2s_duplex_init/play_loop/play_stop()`, `audio_capture_start/block_ready/block()`, `tone_gen_fill()`, `vu_peak/vu_bar_px/vu_color_be()` — all via `fw2.h`.

- [ ] **Step 1: Scaffold the app directory**

```bash
python tools/fw.py new-app hello_audio
```
This copies `apps/template` → `apps/hello_audio` and rewrites the CMake target name. (The next steps overwrite `main.c` and confirm `CMakeLists.txt`.)

- [ ] **Step 2: Confirm the app `CMakeLists.txt`**

Ensure `apps/hello_audio/CMakeLists.txt` reads exactly (rewrite if the scaffold differs):

```cmake
add_executable(hello_audio main.c)
target_link_libraries(hello_audio freewili2_bsp)
pico_set_binary_type(hello_audio copy_to_ram)
pico_add_extra_outputs(hello_audio)
```

- [ ] **Step 3: Write the demo `main.c`**

Overwrite `apps/hello_audio/main.c`:

```c
// hello_audio — on-hardware smoke test for the I2S full-duplex audio driver.
// Plays a 1 kHz tone out the NAU88C10 (speaker, then the 3.5 mm jack) while
// capturing the codec ADC (external mic) on the same I2S bus, and draws a live
// mic VU bar on the ST7796. Diagnostics over SEGGER RTT (fw rtt).
#include "fw2.h"
#include "platform/diag.h"
#include "pico/stdlib.h"

// Big-endian RGB565 (the panel sends the high byte first).
static inline uint16_t rgb565_be(uint8_t r, uint8_t g, uint8_t b) {
    uint16_t c = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    return (uint16_t)((c >> 8) | (c << 8));
}

// VU bar geometry on the 480x320 panel.
#define VU_X 20
#define VU_Y 200
#define VU_W 440
#define VU_H 60

static void vu_draw(uint16_t peak) {
    int px = vu_bar_px(peak, VU_W);
    uint16_t fg = vu_color_be(peak);
    uint16_t bg = rgb565_be(0, 0, 40);
    if (px > 0)     st7796_fill_rect(VU_X, VU_Y, px, VU_H, fg);
    if (px < VU_W)  st7796_fill_rect(VU_X + px, VU_Y, VU_W - px, VU_H, bg);
}

int main(void) {
    board_init();   // 250 MHz + vreg + clk_peri re-source; also ioexp_init + I2C1
    DIAG("\n=== hello_audio: full-duplex boot ===\n");

    st7796_init();
    st7796_fill_screen(rgb565_be(0, 0, 40));
    st7796_draw_text(8, 8, 2, rgb565_be(255,255,255), rgb565_be(0,0,40),
                     "AUDIO OUT + MIC");
    board_backlight_set(1);

    codec_nau88c10_init();
    bool codec_ok = codec_nau88c10_input_ok();
    if (codec_ok) DIAG("codec: input path ready\n");
    st7796_draw_text(8, 40, 2,
                     codec_ok ? rgb565_be(0,255,0) : rgb565_be(255,0,0),
                     rgb565_be(0,0,40),
                     codec_ok ? "CODEC OK" : "CODEC FAIL");

    // 1 kHz tone ring: 64 frames @ 16 kHz = 4 whole periods = 256 bytes (ring-aligned).
    static uint32_t tone_buf[64] __attribute__((aligned(256)));
    int16_t mono[64]; float ph = 0.0f;
    tone_gen_fill(mono, 64, 1000.0f, 16000.0f, &ph);
    for (int i = 0; i < 64; i++) {
        uint16_t s = (uint16_t)mono[i];
        tone_buf[i] = ((uint32_t)s << 16) | s;   // same sample on L and R slots
    }

    codec_nau88c10_dac_mute(false);      // DAC live (init left it soft-muted)
    audio_i2s_duplex_init(16000);
    audio_capture_start();
    DIAG("duplex: streaming; cycling SILENCE/SPEAKER/JACK...\n");

    st7796_draw_text(VU_X, VU_Y - 24, 2, rgb565_be(255,255,255),
                     rgb565_be(0,0,40), "MIC LEVEL");
    vu_draw(0);

    // SILENCE 3s -> SPEAKER 4s -> 3.5mm JACK 4s, looping. The 1 kHz DAC stream runs
    // continuously across SPEAKER+JACK; only codec_nau88c10_set_output() flips the
    // analog routing (speaker <-> headphone). Mic capture runs through all states.
    enum { ST_SILENCE, ST_SPEAKER, ST_JACK } state = ST_SILENCE;
    const char *st_name[] = { "SIL", "SPK", "JACK" };
    st7796_fill_rect(8, 80, 464, 44, rgb565_be(60,0,0));
    st7796_draw_text(16, 90, 3, rgb565_be(255,255,255), rgb565_be(60,0,0), "TONE OFF");
    absolute_time_t t_state = get_absolute_time();
    uint32_t blk = 0;
    for (;;) {
        int64_t held = absolute_time_diff_us(t_state, get_absolute_time());
        if (state == ST_SILENCE && held > 3000000) {
            state = ST_SPEAKER; t_state = get_absolute_time();
            codec_nau88c10_set_output(CODEC_OUT_SPEAKER);
            audio_i2s_duplex_play_loop(tone_buf, 64);
            st7796_fill_rect(8, 80, 464, 44, rgb565_be(0,120,0));
            st7796_draw_text(16, 90, 3, rgb565_be(255,255,255), rgb565_be(0,120,0),
                             "SPEAKER 1kHz");
            DIAG("state=SPEAKER (tone -> onboard speaker)\n");
            codec_nau88c10_log_output();
        } else if (state == ST_SPEAKER && held > 4000000) {
            state = ST_JACK; t_state = get_absolute_time();
            codec_nau88c10_set_output(CODEC_OUT_HEADPHONE);   // route to 3.5mm jack
            st7796_fill_rect(8, 80, 464, 44, rgb565_be(0,90,160));
            st7796_draw_text(16, 90, 3, rgb565_be(255,255,255), rgb565_be(0,90,160),
                             "3.5mm JACK 1kHz");
            DIAG("state=JACK (tone -> 3.5mm jack; speaker muted)\n");
            codec_nau88c10_log_output();
        } else if (state == ST_JACK && held > 4000000) {
            state = ST_SILENCE; t_state = get_absolute_time();
            audio_i2s_duplex_play_stop();
            st7796_fill_rect(8, 80, 464, 44, rgb565_be(60,0,0));
            st7796_draw_text(16, 90, 3, rgb565_be(255,255,255), rgb565_be(60,0,0),
                             "TONE OFF");
            DIAG("state=SILENCE\n");
        }
        if (audio_capture_block_ready()) {
            const uint32_t *b = audio_capture_block();
            if (b) {
                uint16_t pk = vu_peak(b, AUDIO_CAPTURE_BLOCK_FRAMES, AUDIO_MIC_I2S_SLOT);
                vu_draw(pk);
                if ((blk++ & 0x0F) == 0)
                    DIAG("vu: state=%s blk=%u peak=%u\n", st_name[state],
                         (unsigned)blk, (unsigned)pk);
            }
        }
        tight_loop_contents();
    }
}
```

- [ ] **Step 4: Register the app in the top-level `CMakeLists.txt`**

In `CMakeLists.txt` (repo root), add after the `add_subdirectory(apps/hello_display)` line:

```cmake
add_subdirectory(apps/hello_audio)
```

- [ ] **Step 5: Build the app — verify it links**

Run (with the Pico SDK env exported): `python tools/fw.py build hello_audio`
Expected: configures and builds; produces `build/apps/hello_audio/hello_audio.elf` and `.uf2` with no errors or unresolved symbols (all audio/codec/capture/DSP symbols resolve from `freewili2_bsp`).

- [ ] **Step 6: Commit**

```bash
git add apps/hello_audio/main.c apps/hello_audio/CMakeLists.txt CMakeLists.txt
git commit -m "feat: add hello_audio full-duplex demo/smoke-test app"
```

---

### Task 5: Documentation

Reflect the new capability in the hardware docs and add a driver usage page.

**Files:**
- Modify: `docs/hardware/catalog.md`, `docs/hardware/pinmap.md`
- Create: `docs/drivers/audio.md`

- [ ] **Step 1: Flip the I2S row in `catalog.md`**

In `docs/hardware/catalog.md`, remove the `I2S audio codec — NAU88C10YG` row from the **TODO** table and add a row to the **DONE** table:

```markdown
| Audio — I2S full-duplex (NAU88C10 codec) | `bsp/audio/{audio_i2s_duplex,codec_nau88c10,audio_capture,tone_gen,vu_meter}.{c,h}`, `bsp/audio/i2s_duplex.pio` | PIO0 SM0 clocks the codec (slave, MCLK-direct); TX zero-CPU ring DMA, RX ping-pong DMA on SHARED DMA_IRQ_0. Playback (speaker/headphone) + mic capture (PCM blocks). Harvested from evaderkrub/freewili2-fullduplex-audio (MIT). Demo: `apps/hello_audio`. |
```

Also update the "Confirming this catalog" section: the count of DONE peripherals is now **five** (platform, display, touch, LEDs, audio), and `bsp/CMakeLists.txt` now also compiles `audio/*.c` + generates `i2s_duplex.pio.h`.

- [ ] **Step 2: Move the audio pins to the verified table in `pinmap.md`**

In `docs/hardware/pinmap.md`, remove the five `SPK_*` / `MCLK` rows (GPIO 4/5/6/7/22) from the "Broader board inventory (`FwDisplayVibe.md`) — not yet in `board.h`" table, and add them to the "Verified (`bsp/platform/board.h`) — has a driver" table:

```markdown
| `PIN_AUDIO_DIN`  | 4  | I2S ADC data (SPK_DOUT, codec -> MCU) | PIO0 in; mic capture |
| `PIN_AUDIO_DATA` | 5  | I2S DAC data (SPK_DIN, MCU -> codec)  | PIO0 out; playback |
| `PIN_AUDIO_LRCK` | 6  | I2S word clock (SPK_LRCK)             | PIO sideset bit 0 |
| `PIN_AUDIO_BCLK` | 7  | I2S bit clock (SPK_BCLK)              | PIO sideset bit 1 |
| `PIN_AUDIO_MCLK` | 22 | Codec master clock (SPK_MCLK)         | 256*fs square wave (PWM) |
```

- [ ] **Step 3: Write the driver usage doc**

Create `docs/drivers/audio.md`:

```markdown
# Audio (I2S full-duplex, NAU88C10)

Full-duplex I2S audio on the FreeWili2: play PCM out the NAU88C10 codec
(onboard speaker or 3.5 mm headphone/line jack) while capturing the codec ADC
(external 3.5 mm mic) — one PIO0 state machine drives both directions on a
shared I2S bus. Harvested from `evaderkrub/freewili2-fullduplex-audio` (MIT).

**Note:** this is the *I2S codec mic* (3.5 mm jack), not the on-board PDM mic
array (a separate driver — see `docs/hardware/catalog.md`).

## Pins

`PIN_AUDIO_DATA` 5 (DAC out), `PIN_AUDIO_DIN` 4 (ADC in), `PIN_AUDIO_LRCK` 6,
`PIN_AUDIO_BCLK` 7, `PIN_AUDIO_MCLK` 22 (PWM 256·fs). Codec control on I2C1
(GPIO 26/27) at address 0x1A.

## Bring-up order

```c
board_init();                 // 250 MHz; also brings up I2C1 + ioexp
codec_nau88c10_init();        // NAU88C10 register sequence (16 kHz, speaker)
codec_nau88c10_input_ok();    // optional: DIAGs rev + PM2, returns bool
codec_nau88c10_dac_mute(false);
audio_i2s_duplex_init(16000); // MCLK PWM + PIO0 SM0; RX runs immediately
audio_capture_start();        // ping-pong RX DMA -> PCM blocks (SHARED DMA_IRQ_0)
```

## Playback

The TX path is a zero-CPU DMA read-ring over a pre-filled buffer. The buffer
must hold whole tone periods, be a power-of-two **bytes**, and be aligned to
its byte size:

```c
static uint32_t tone[64] __attribute__((aligned(256))); // 64 frames = 256 bytes
// fill tone[i] = ((uint32_t)left16 << 16) | right16;   // e.g. via tone_gen_fill
audio_i2s_duplex_play_loop(tone, 64);
audio_i2s_duplex_play_stop();                 // park DAC at silence
codec_nau88c10_set_output(CODEC_OUT_SPEAKER); // or CODEC_OUT_HEADPHONE (3.5mm)
```

## Capture ("audio in")

```c
if (audio_capture_block_ready()) {
    const uint32_t *b = audio_capture_block();      // 256 frames [L16|R16], or NULL
    if (b) {
        uint16_t pk = vu_peak(b, AUDIO_CAPTURE_BLOCK_FRAMES, AUDIO_MIC_I2S_SLOT);
        // ... your DSP over the raw PCM block ...
    }
}
```

The mono NAU88C10 ADC streams on the **right** slot (`AUDIO_MIC_I2S_SLOT = 1`).

## Constraints & gotchas

- **Sample rate** is fixed at 16 kHz in the current codec register set. The
  driver derives MCLK + PIO clkdiv from `clk_sys`, so it runs at the board's
  250 MHz unchanged; MCLK lands ~0.06 % off ideal (negligible).
- **DMA_IRQ_0 is shared** with the ST7796 flush — `audio_capture` uses
  `irq_add_shared_handler`; never register an exclusive handler on it.
- **RX slot/phase alignment** is the known bring-up risk: if captured peaks sit
  at the noise floor while the mic is driven, apply the knobs documented in
  `bsp/audio/i2s_duplex.pio` (flip `AUDIO_MIC_I2S_SLOT`, move the `in pins,1` to
  the other BCLK phase, or swap the channel side values).
- No floats over RTT: `DIAG` integer values only.

See `apps/hello_audio/main.c` for a complete worked example.
```

- [ ] **Step 4: Commit**

```bash
git add docs/hardware/catalog.md docs/hardware/pinmap.md docs/drivers/audio.md
git commit -m "docs: mark I2S audio DONE; add pinmap rows + audio driver doc"
```

---

### Task 6: On-hardware smoke test (manual verification)

Not automatable in CI — requires the physical board, the CMSIS-DAP probe, a
speaker/headphone, and (ideally) a sound source near the 3.5 mm mic. Perform and
record the result in a findings note.

**Files:**
- Create: `docs/superpowers/findings/2026-07-01-i2s-audio-e2e.md` (record the result)

- [ ] **Step 1: Flash and attach RTT**

```bash
python tools/fw.py flash hello_audio
python tools/fw.py rtt
```

- [ ] **Step 2: Verify — check each against the RTT stream and the panel**

- RTT shows `=== hello_audio: full-duplex boot ===`, then codec bring-up with
  `codec: rev(0x3F)=…` (rev ≠ 0) and `pm2(0x02)=0x015` → `codec: input path ready`.
- Panel shows `AUDIO OUT + MIC`, a green `CODEC OK`, a `MIC LEVEL` label, and the
  banner cycling `TONE OFF` → `SPEAKER 1kHz` → `3.5mm JACK 1kHz`.
- During `SPEAKER 1kHz`: a 1 kHz tone is audible from the onboard speaker.
- During `3.5mm JACK 1kHz`: the tone moves to a headphone in the 3.5 mm jack;
  RTT logs `codec: out regs R36=0x040 R38=0x001` (speaker muted, HP on).
- RTT `vu:` lines stream continuously in **all** states with no stall/starvation
  (proves capture runs concurrently with playback and the shared DMA_IRQ_0 didn't
  break the display flush — the display keeps redrawing banners/VU bar).
- If mic peaks stay at the floor when a sound source is applied, this is the known
  RX-alignment knob (see `i2s_duplex.pio`); note it and try the documented fixes.

- [ ] **Step 3: Record the result**

Write `docs/superpowers/findings/2026-07-01-i2s-audio-e2e.md` capturing PASS/FAIL,
the observed RTT lines, and any RX-alignment adjustment needed. Commit:

```bash
git add docs/superpowers/findings/2026-07-01-i2s-audio-e2e.md
git commit -m "docs: record hello_audio on-hardware E2E result"
```

---

## Self-Review

**Spec coverage:**
- Full-duplex engine (PIO+DMA) → Task 2. ✓
- NAU88C10 codec + speaker/headphone routing → Task 2. ✓
- General PCM-block capture API + VU helper → Task 3 (capture) + Task 1 (`vu_peak`). ✓
- Pure host-tested DSP (`tone_gen`, `vu_meter`) → Task 1. ✓
- `apps/hello_audio` demo/smoke test → Task 4. ✓
- board.h pins, CMake wiring, fw2.h, catalog/pinmap/audio.md → Tasks 2, 3, 5. ✓
- Invariants: DMA_IRQ_0 shared (Task 3), 250 MHz clock-portable (Global Constraints + verified Task 2/4 build), no-float DIAG (code uses ints), PIO0 vs pio1 LEDs / DMA budget (design, no code conflict), copy_to_ram (app CMake, Task 4). ✓
- On-hardware verification → Task 6. ✓

**Placeholder scan:** No TBD/TODO/"handle edge cases"/"similar to"/vague steps. Every code step shows complete content; verbatim harvests give exact `cp` commands against a guaranteed-present `$SRC`. ✓

**Type consistency:** `audio_capture_start/block_ready/block` and the `AUDIO_CAPTURE_BLOCK_FRAMES`/`AUDIO_MIC_I2S_SLOT` macros are defined in Task 3 and consumed identically in Task 4. `codec_out_t`/`CODEC_OUT_SPEAKER`/`CODEC_OUT_HEADPHONE`, `audio_i2s_duplex_*`, `tone_gen_fill`, `vu_peak`/`vu_bar_px`/`vu_color_be` signatures match the harvested headers (Tasks 1–2) and their Task-4 call sites. `PIN_AUDIO_*` names match between board.h (Task 2) and the driver's existing `#include`s. ✓
