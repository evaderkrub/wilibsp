# CC1101 sub-GHz Radio Driver Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Harvest the proven CC1101 sub-GHz radio stack from the `subghz` repo into `wilibsp`'s `freewili2_bsp` library (Increment 3), with host tests and an RTT self-verify demo, following the Increment-1 harvest cycle.

**Architecture:** Copy the seven `subghz/src/radio/*` modules verbatim into `bsp/radio/`, changing exactly one line (`gdo_capture` moves `pio0`→`pio2` to avoid the GPIO-base-16 collision with the audio (pio0) and LED (pio1) drivers). Pure logic (`cc1101_regs`, `monitor_engine`, `scan_engine`, `ook_tx`, `capture_store`) is host-tested via the existing standalone CTest tree; hardware modules (`cc1101`, `gdo_capture`) are verified by build + on-hardware demo.

**Tech Stack:** C11, Raspberry Pi Pico SDK 2.2.0, RP2350B, CMake + Ninja, `tools/fw.py` CLI, SEGGER RTT diagnostics.

## Global Constraints

- **Board select is CMake-only.** `set(PICO_BOARD freewili2 ...)` lives in the top-level `CMakeLists.txt`. NEVER pass `-DPICO_BOARD` on any cmake/`fw` command line.
- **Diagnostics via `DIAG()` only** (`bsp/platform/diag.h` → `SEGGER_RTT_printf`). No UART/USB stdio. `%d %u %x %s %c` and field widths only — **no floats** in any `DIAG()` format string.
- **DMA_IRQ_0 is a shared line.** Any DMA IRQ user must `irq_add_shared_handler(DMA_IRQ_0, ...)` and guard on its own channel. (This increment adds no DMA IRQ handler — `gdo_capture` polls `write_addr`.)
- **GPIO8 is dual-function** (LCD_DC / CC1101 MISO); the staged `spi_bus` arbiter muxes it. Do not touch it directly.
- **Harvest verbatim.** Copy source `.c`/`.h`/`.pio` byte-for-byte except the single documented `pio0`→`pio2` edit in `gdo_capture.c`. Keep the source's proven names (`cc1101_*`, `gdo_capture_*`, etc.) — no `fw2_` rename.
- **Apps are `copy_to_ram`.** Every app binary uses `pico_set_binary_type(<app> copy_to_ram)`.
- **License/provenance:** all harvested files are MIT © 2026 Dave Robins (inherited from the source repo root); no header changes needed.
- **Quoted includes resolve by same-directory search.** Source files use a mix of bare (`"cc1101_regs.h"`) and prefixed (`"radio/monitor_engine.h"`) includes; both resolve because the compiler searches the including file's own directory first. **Test files in `tests/` must use the `radio/` prefix** (their include root is `../bsp`).

**Source location:** `C:\~prj\Dropbox\vibeProjects\subghz\src\radio\` (originals). Copy from there.

---

### Task 1: Harvest `cc1101_regs` (pure register math) + host test

Pure integer frequency/RSSI/band math. No Pico SDK. First radio module + red→green host test.

**Files:**
- Create: `bsp/radio/cc1101_regs.h`
- Create: `bsp/radio/cc1101_regs.c`
- Create: `tests/test_cc1101_regs.c`
- Modify: `tests/CMakeLists.txt` (append a test target)

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `typedef struct { uint8_t f2, f1, f0; } cc1101_freq_regs_t;`
  - `typedef enum { CC1101_MOD_2FSK=0, CC1101_MOD_GFSK=1, CC1101_MOD_ASK_OOK=3, CC1101_MOD_4FSK=4, CC1101_MOD_MSK=7 } cc1101_mod_t;`
  - `cc1101_freq_regs_t cc1101_freq_to_regs(uint32_t hz);`
  - `int cc1101_rssi_to_dbm(uint8_t raw);`
  - `uint8_t cc1101_mdmcfg2_mod_bits(cc1101_mod_t m);`
  - `bool cc1101_freq_in_band(uint32_t hz);`

- [ ] **Step 1: Write the ported host test**

Create `tests/test_cc1101_regs.c` (note the `radio/` include prefix — differs from the subghz original):

```c
#include "test_util.h"
#include "radio/cc1101_regs.h"

int main(void) {
    // 433.92 MHz: FREQ = round(433920000 * 65536 / 26000000) = 1093745 = 0x10B071
    cc1101_freq_regs_t r = cc1101_freq_to_regs(433920000u);
    ASSERT_EQ(r.f2, 0x10);
    ASSERT_EQ(r.f1, 0xB0);
    ASSERT_EQ(r.f0, 0x71);

    // RSSI -> dBm
    ASSERT_EQ(cc1101_rssi_to_dbm(0),   -74);   // 0/2 - 74
    ASSERT_EQ(cc1101_rssi_to_dbm(96),  -26);   // 96/2 - 74 = 48-74
    ASSERT_EQ(cc1101_rssi_to_dbm(128), -138);  // (128-256)/2 - 74 = -64-74
    ASSERT_EQ(cc1101_rssi_to_dbm(129), -137);  // odd negative: (129-256)/2 = -63 (trunc) - 74

    // Modulation -> MDMCFG2 MOD_FORMAT bits
    ASSERT_EQ(cc1101_mdmcfg2_mod_bits(CC1101_MOD_2FSK),    0x00);
    ASSERT_EQ(cc1101_mdmcfg2_mod_bits(CC1101_MOD_GFSK),    0x10);
    ASSERT_EQ(cc1101_mdmcfg2_mod_bits(CC1101_MOD_ASK_OOK), 0x30);
    ASSERT_EQ(cc1101_mdmcfg2_mod_bits(CC1101_MOD_4FSK),    0x40);
    ASSERT_EQ(cc1101_mdmcfg2_mod_bits(CC1101_MOD_MSK),     0x70);

    // Band limits (interior, gaps, and inclusive boundaries)
    ASSERT_TRUE(cc1101_freq_in_band(433920000u));
    ASSERT_TRUE(cc1101_freq_in_band(868000000u));
    ASSERT_TRUE(cc1101_freq_in_band(300000000u));  // band-1 lower boundary
    ASSERT_TRUE(cc1101_freq_in_band(348000000u));  // band-1 upper boundary
    ASSERT_TRUE(!cc1101_freq_in_band(360000000u)); // 348-387 gap
    ASSERT_TRUE(!cc1101_freq_in_band(500000000u)); // gap between 464 and 779
    ASSERT_TRUE(!cc1101_freq_in_band(950000000u)); // above 928
    ASSERT_TRUE(!cc1101_freq_in_band(100000000u)); // below range
    TEST_RETURN();
}
```

- [ ] **Step 2: Wire the test target**

Append to `tests/CMakeLists.txt`:

```cmake
add_executable(test_cc1101_regs
    test_cc1101_regs.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../bsp/radio/cc1101_regs.c)
target_include_directories(test_cc1101_regs PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../bsp
    ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME cc1101_regs COMMAND test_cc1101_regs)
```

- [ ] **Step 3: Run tests, verify the new one FAILS to build**

Run: `tools/fw test`
Expected: configure/build error — `../bsp/radio/cc1101_regs.c` does not exist yet (No such file). The existing 3 tests are unaffected.

- [ ] **Step 4: Create the module (verbatim copy)**

Create `bsp/radio/cc1101_regs.h`:

```c
#ifndef CC1101_REGS_H
#define CC1101_REGS_H
#include <stdint.h>
#include <stdbool.h>

typedef struct { uint8_t f2, f1, f0; } cc1101_freq_regs_t;
typedef enum {
    CC1101_MOD_2FSK = 0, CC1101_MOD_GFSK = 1, CC1101_MOD_ASK_OOK = 3,
    CC1101_MOD_4FSK = 4, CC1101_MOD_MSK = 7
} cc1101_mod_t;

cc1101_freq_regs_t cc1101_freq_to_regs(uint32_t hz);
int     cc1101_rssi_to_dbm(uint8_t raw);
uint8_t cc1101_mdmcfg2_mod_bits(cc1101_mod_t m);
bool    cc1101_freq_in_band(uint32_t hz);
#endif
```

Create `bsp/radio/cc1101_regs.c`:

```c
#include "cc1101_regs.h"
#define XTAL_HZ 26000000u

cc1101_freq_regs_t cc1101_freq_to_regs(uint32_t hz) {
    // round(f_hz * 2^16 / 26e6): add half-divisor before the integer divide
    uint32_t f = (uint32_t)((((uint64_t)hz << 16) + (XTAL_HZ / 2u)) / XTAL_HZ);
    cc1101_freq_regs_t r = { (uint8_t)(f >> 16), (uint8_t)(f >> 8), (uint8_t)f };
    return r;
}

int cc1101_rssi_to_dbm(uint8_t raw) {
    int v = (raw >= 128) ? (int)raw - 256 : (int)raw;
    return v / 2 - 74;
}

uint8_t cc1101_mdmcfg2_mod_bits(cc1101_mod_t m) {
    return (uint8_t)((m & 0x07) << 4);
}

bool cc1101_freq_in_band(uint32_t hz) {
    return (hz >= 300000000u && hz <= 348000000u) ||
           (hz >= 387000000u && hz <= 464000000u) ||
           (hz >= 779000000u && hz <= 928000000u);
}
```

- [ ] **Step 5: Run tests, verify PASS**

Run: `tools/fw test`
Expected: all tests PASS, including `cc1101_regs` (now 4 tests total).

- [ ] **Step 6: Commit**

```bash
git add bsp/radio/cc1101_regs.h bsp/radio/cc1101_regs.c tests/test_cc1101_regs.c tests/CMakeLists.txt
git commit -m "feat(radio): harvest cc1101_regs pure math + host test"
```

---

### Task 2: Harvest `monitor_engine` (pure edge stats) + host test

Pure burst/edge/gap statistics over drained durations. No Pico SDK.

**Files:**
- Create: `bsp/radio/monitor_engine.h`
- Create: `bsp/radio/monitor_engine.c`
- Create: `tests/test_monitor_engine.c`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `#define MON_WIDTH_BINS 8`, `#define MON_IDLE_TICKS 20000u`
  - `typedef struct { uint32_t bins[MON_WIDTH_BINS]; uint32_t edges; uint32_t burst_ticks; uint32_t max_gap_ticks; bool level; bool in_burst; } monitor_stats_t;`
  - `int monitor_width_bin(uint32_t ticks);`
  - `void monitor_reset(monitor_stats_t *st, bool start_level);`
  - `void monitor_feed(monitor_stats_t *st, uint32_t ticks);`

- [ ] **Step 1: Write the ported host test**

Create `tests/test_monitor_engine.c`:

```c
// tests/test_monitor_engine.c
#include "test_util.h"
#include "radio/monitor_engine.h"

int main(void) {
    // Width bins are monotonic log-ish buckets: shorter ticks -> lower bin.
    ASSERT_TRUE(monitor_width_bin(50)  < monitor_width_bin(500));
    ASSERT_TRUE(monitor_width_bin(500) < monitor_width_bin(5000));
    ASSERT_EQ(monitor_width_bin(0), 0);
    // Saturated/huge widths clamp to the top bin, never out of range.
    ASSERT_TRUE(monitor_width_bin(0xFFFFFFFFu) == MON_WIDTH_BINS - 1);

    monitor_stats_t st;
    monitor_reset(&st, false);              // start level low
    ASSERT_EQ((int)st.level, 0);
    ASSERT_EQ((int)st.in_burst, 0);

    // A burst of 4 pulses (~350us each), then a 30ms idle gap.
    monitor_feed(&st, 350); monitor_feed(&st, 350);
    monitor_feed(&st, 350); monitor_feed(&st, 350);
    ASSERT_EQ((int)st.in_burst, 1);
    ASSERT_EQ(st.edges, 4u);
    ASSERT_EQ(st.burst_ticks, 1400u);
    ASSERT_EQ((int)st.level, 0);            // toggled 4x from low -> back to low
    ASSERT_EQ(st.bins[3], 4u);              // four ~350-tick pulses land in bin 3

    monitor_feed(&st, 30000);               // >= MON_IDLE_TICKS -> idle
    ASSERT_EQ((int)st.in_burst, 0);
    ASSERT_EQ(st.max_gap_ticks, 30000u);

    // Next pulse after idle starts a fresh burst (edges/burst_ticks reset).
    monitor_feed(&st, 200);
    ASSERT_EQ((int)st.in_burst, 1);
    ASSERT_EQ(st.edges, 1u);
    ASSERT_EQ(st.burst_ticks, 200u);
    TEST_RETURN();
}
```

- [ ] **Step 2: Wire the test target**

Append to `tests/CMakeLists.txt`:

```cmake
add_executable(test_monitor_engine
    test_monitor_engine.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../bsp/radio/monitor_engine.c)
target_include_directories(test_monitor_engine PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../bsp
    ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME monitor_engine COMMAND test_monitor_engine)
```

- [ ] **Step 3: Run tests, verify the new one FAILS to build**

Run: `tools/fw test`
Expected: build error — `../bsp/radio/monitor_engine.c` does not exist yet.

- [ ] **Step 4: Create the module (verbatim copy)**

Create `bsp/radio/monitor_engine.h`:

```c
// src/radio/monitor_engine.h
#ifndef MONITOR_ENGINE_H
#define MONITOR_ENGINE_H
#include <stdint.h>
#include <stdbool.h>

// Edge-duration stream: each value is the number of 1 us ticks the CURRENT level
// was held before an edge; the level toggles per value. A value >= MON_IDLE_TICKS
// is a saturated idle gap (PIO overflow sentinel) and marks a burst boundary.
#define MON_WIDTH_BINS 8
#define MON_IDLE_TICKS 20000u   // >= 20 ms held == idle / burst boundary

typedef struct {
    uint32_t bins[MON_WIDTH_BINS];  // cumulative pulse-width histogram
    uint32_t edges;                 // pulses in the current/last burst
    uint32_t burst_ticks;           // active ticks in the current/last burst
    uint32_t max_gap_ticks;         // largest idle gap seen
    bool     level;                 // reconstructed level after the last edge
    bool     in_burst;              // inside a burst (not idle)
} monitor_stats_t;

int  monitor_width_bin(uint32_t ticks);            // 0..MON_WIDTH_BINS-1
void monitor_reset(monitor_stats_t *st, bool start_level);
void monitor_feed(monitor_stats_t *st, uint32_t ticks);
#endif
```

Create `bsp/radio/monitor_engine.c`:

```c
// src/radio/monitor_engine.c
#include "radio/monitor_engine.h"

// Log-ish width buckets by tick (us) upper bounds; last bucket catches the rest.
static const uint32_t BIN_MAX[MON_WIDTH_BINS] = {
    64, 128, 256, 512, 1024, 2048, 4096, 0xFFFFFFFFu
};

int monitor_width_bin(uint32_t ticks) {
    for (int i = 0; i < MON_WIDTH_BINS; i++)
        if (ticks <= BIN_MAX[i]) return i;
    return MON_WIDTH_BINS - 1;
}

void monitor_reset(monitor_stats_t *st, bool start_level) {
    for (int i = 0; i < MON_WIDTH_BINS; i++) st->bins[i] = 0;
    st->edges = 0; st->burst_ticks = 0; st->max_gap_ticks = 0;
    st->level = start_level; st->in_burst = false;
}

void monitor_feed(monitor_stats_t *st, uint32_t ticks) {
    if (ticks >= MON_IDLE_TICKS) {              // idle gap: close the burst
        if (ticks > st->max_gap_ticks) st->max_gap_ticks = ticks;
        st->in_burst = false;
        st->level = !st->level;
        return;
    }
    if (!st->in_burst) {                        // first pulse after idle: new burst
        st->in_burst = true;
        st->edges = 0; st->burst_ticks = 0;
    }
    st->bins[monitor_width_bin(ticks)]++;
    st->edges++;
    st->burst_ticks += ticks;
    st->level = !st->level;
}
```

- [ ] **Step 5: Run tests, verify PASS**

Run: `tools/fw test`
Expected: all PASS including `monitor_engine` (5 total).

- [ ] **Step 6: Commit**

```bash
git add bsp/radio/monitor_engine.h bsp/radio/monitor_engine.c tests/test_monitor_engine.c tests/CMakeLists.txt
git commit -m "feat(radio): harvest monitor_engine burst stats + host test"
```

---

### Task 3: Harvest `capture_store` (pure clip FIFO) + host test

Pure single-clip FIFO over a caller buffer. Depends only on `cc1101_regs.h` (for `cc1101_mod_t`), created in Task 1.

**Files:**
- Create: `bsp/radio/capture_store.h`
- Create: `bsp/radio/capture_store.c`
- Create: `tests/test_capture_store.c`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `cc1101_mod_t` (Task 1).
- Produces:
  - `void capture_store_init(uint32_t *buf, uint32_t capacity);`
  - `void capture_store_begin(uint32_t freq, cc1101_mod_t mod, bool start_level, int antenna);`
  - `uint32_t capture_store_append(const uint32_t *durs, uint32_t n);`
  - `bool capture_store_full(void);` `uint32_t capture_store_len(void);` `const uint32_t *capture_store_data(void);`
  - `uint32_t capture_store_freq(void);` `cc1101_mod_t capture_store_mod(void);` `bool capture_store_start_level(void);` `int capture_store_antenna(void);` `uint64_t capture_store_total_ticks(void);`

- [ ] **Step 1: Write the ported host test**

Create `tests/test_capture_store.c`:

```c
// tests/test_capture_store.c
#include "test_util.h"
#include "radio/capture_store.h"

int main(void) {
    static uint32_t buf[8];
    capture_store_init(buf, 8);
    ASSERT_EQ((int)capture_store_len(), 0);
    ASSERT_TRUE(!capture_store_full());

    capture_store_begin(433920000u, CC1101_MOD_ASK_OOK, true, 7);
    ASSERT_EQ((long)capture_store_freq(), 433920000L);
    ASSERT_EQ((int)capture_store_mod(), (int)CC1101_MOD_ASK_OOK);
    ASSERT_TRUE(capture_store_start_level());
    ASSERT_EQ(capture_store_antenna(), 7);

    uint32_t a[3] = { 10, 20, 30 };
    ASSERT_EQ((int)capture_store_append(a, 3), 3);
    ASSERT_EQ((int)capture_store_len(), 3);
    ASSERT_EQ((int)capture_store_total_ticks(), 60);
    ASSERT_EQ((int)capture_store_data()[1], 20);

    // Append crossing capacity: room = 5, offer 6 -> takes 5, now full.
    uint32_t b[6] = { 1, 2, 3, 4, 5, 6 };
    ASSERT_EQ((int)capture_store_append(b, 6), 5);
    ASSERT_EQ((int)capture_store_len(), 8);
    ASSERT_TRUE(capture_store_full());
    ASSERT_EQ((int)capture_store_append(a, 1), 0);        // no room left
    ASSERT_EQ((int)capture_store_total_ticks(), 75);      // 60 + 1+2+3+4+5

    // begin() resets length, ticks, and metadata.
    capture_store_begin(315000000u, CC1101_MOD_2FSK, false, 3);
    ASSERT_EQ((int)capture_store_len(), 0);
    ASSERT_EQ((int)capture_store_total_ticks(), 0);
    ASSERT_TRUE(!capture_store_full());
    ASSERT_TRUE(!capture_store_start_level());
    ASSERT_EQ((int)capture_store_mod(), (int)CC1101_MOD_2FSK);
    ASSERT_EQ(capture_store_antenna(), 3);
    TEST_RETURN();
}
```

- [ ] **Step 2: Wire the test target**

Append to `tests/CMakeLists.txt`:

```cmake
add_executable(test_capture_store
    test_capture_store.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../bsp/radio/capture_store.c)
target_include_directories(test_capture_store PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../bsp
    ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME capture_store COMMAND test_capture_store)
```

- [ ] **Step 3: Run tests, verify the new one FAILS to build**

Run: `tools/fw test`
Expected: build error — `../bsp/radio/capture_store.c` does not exist yet.

- [ ] **Step 4: Create the module (verbatim copy)**

Create `bsp/radio/capture_store.h`:

```c
// src/radio/capture_store.h — one PSRAM clip of GDO0 edge durations + metadata.
// Pure: the core operates on a caller-supplied buffer (PSRAM on target, a plain
// array in host tests). No Pico SDK dependency.
#ifndef CAPTURE_STORE_H
#define CAPTURE_STORE_H
#include <stdint.h>
#include <stdbool.h>
#include "radio/cc1101_regs.h"   // cc1101_mod_t

// Bind the store to a duration buffer + capacity (in durations). Clears the clip.
void     capture_store_init(uint32_t *buf, uint32_t capacity);
// Start a new clip: reset length + total ticks, record clip metadata.
// `antenna` is the opaque ANT_* value routed at record time (for TX replay).
void     capture_store_begin(uint32_t freq, cc1101_mod_t mod, bool start_level, int antenna);
// Append up to n durations; returns how many were stored (< n once it fills).
uint32_t capture_store_append(const uint32_t *durs, uint32_t n);
bool     capture_store_full(void);
uint32_t capture_store_len(void);          // durations in the current clip
const uint32_t *capture_store_data(void);  // pointer to the duration array

uint32_t     capture_store_freq(void);
cc1101_mod_t capture_store_mod(void);
bool         capture_store_start_level(void);
int          capture_store_antenna(void);       // ANT_* routed at record time
uint64_t     capture_store_total_ticks(void);   // sum of stored durations (us)
#endif
```

Create `bsp/radio/capture_store.c`:

```c
// src/radio/capture_store.c
#include "radio/capture_store.h"

static uint32_t    *s_buf;
static uint32_t     s_cap;
static uint32_t     s_len;
static uint32_t     s_freq;
static cc1101_mod_t s_mod;
static bool         s_start_level;
static int          s_antenna;       // ANT_* routed at record time (opaque here)
static uint64_t     s_total_ticks;   // 64-bit: a near-full idle-heavy clip overflows 32-bit us

void capture_store_init(uint32_t *buf, uint32_t capacity) {
    s_buf = buf; s_cap = capacity;
    s_len = 0; s_total_ticks = 0;
    s_freq = 0; s_mod = CC1101_MOD_ASK_OOK; s_start_level = false; s_antenna = 0;
}

void capture_store_begin(uint32_t freq, cc1101_mod_t mod, bool start_level, int antenna) {
    s_len = 0; s_total_ticks = 0;
    s_freq = freq; s_mod = mod; s_start_level = start_level; s_antenna = antenna;
}

uint32_t capture_store_append(const uint32_t *durs, uint32_t n) {
    uint32_t room = (s_len < s_cap) ? (s_cap - s_len) : 0u;
    uint32_t take = (n < room) ? n : room;
    for (uint32_t i = 0; i < take; i++) {
        s_buf[s_len + i] = durs[i];
        s_total_ticks += durs[i];
    }
    s_len += take;
    return take;
}

bool            capture_store_full(void)        { return s_len >= s_cap; }
uint32_t        capture_store_len(void)         { return s_len; }
const uint32_t *capture_store_data(void)        { return s_buf; }
uint32_t        capture_store_freq(void)        { return s_freq; }
cc1101_mod_t    capture_store_mod(void)         { return s_mod; }
bool            capture_store_start_level(void) { return s_start_level; }
int             capture_store_antenna(void)     { return s_antenna; }
uint64_t        capture_store_total_ticks(void) { return s_total_ticks; }
```

- [ ] **Step 5: Run tests, verify PASS**

Run: `tools/fw test`
Expected: all PASS including `capture_store` (6 total).

- [ ] **Step 6: Commit**

```bash
git add bsp/radio/capture_store.h bsp/radio/capture_store.c tests/test_capture_store.c tests/CMakeLists.txt
git commit -m "feat(radio): harvest capture_store clip FIFO + host test"
```

---

### Task 4: Harvest `ook_tx` (OOK transmit) + host test

Split module: `ook_tx_clamp_us()` is pure (host-tested); `ook_tx_send()` is hardware (behind `#ifndef HOST_TEST`). The host test compiles with `HOST_TEST` defined to exclude the GPIO half.

**Files:**
- Create: `bsp/radio/ook_tx.h`
- Create: `bsp/radio/ook_tx.c`
- Create: `tests/test_ook_tx.c`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `PIN_CC1101_GDO0` (from `platform/board.h`, already present) — only in the hardware half.
- Produces:
  - `#define OOK_TX_MAX_US 100000u`
  - `uint32_t ook_tx_clamp_us(uint32_t us);` (pure)
  - `void ook_tx_send(const uint32_t *durs, uint32_t n, bool start_level);` (hardware)

- [ ] **Step 1: Write the ported host test**

Create `tests/test_ook_tx.c`:

```c
// tests/test_ook_tx.c
#include "test_util.h"
#include "radio/ook_tx.h"

int main(void) {
    ASSERT_EQ((int)ook_tx_clamp_us(0), 0);
    ASSERT_EQ((int)ook_tx_clamp_us(50000), 50000);
    ASSERT_EQ((int)ook_tx_clamp_us(OOK_TX_MAX_US), (int)OOK_TX_MAX_US);
    ASSERT_EQ((int)ook_tx_clamp_us(OOK_TX_MAX_US + 1u), (int)OOK_TX_MAX_US);
    ASSERT_EQ((int)ook_tx_clamp_us(0xFFFFFFFFu), (int)OOK_TX_MAX_US);
    TEST_RETURN();
}
```

- [ ] **Step 2: Wire the test target (with `HOST_TEST` define)**

Append to `tests/CMakeLists.txt`:

```cmake
add_executable(test_ook_tx
    test_ook_tx.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../bsp/radio/ook_tx.c)
target_include_directories(test_ook_tx PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../bsp
    ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(test_ook_tx PRIVATE HOST_TEST)
add_test(NAME ook_tx COMMAND test_ook_tx)
```

- [ ] **Step 3: Run tests, verify the new one FAILS to build**

Run: `tools/fw test`
Expected: build error — `../bsp/radio/ook_tx.c` does not exist yet.

- [ ] **Step 4: Create the module (verbatim copy)**

Create `bsp/radio/ook_tx.h`:

```c
// src/radio/ook_tx.h — drive GDO0 with a captured OOK duration timeline for RF
// re-transmit. The CC1101 must already be in async-transparent OOK TX
// (cc1101_tx_ook_start); this owns GPIO32 while sending.
#ifndef OOK_TX_H
#define OOK_TX_H
#include <stdint.h>
#include <stdbool.h>

// Clamp a single duration so a pathological saturated-idle value can't hang the
// blocking transmit. Real OOK symbols/gaps are far shorter than this.
#define OOK_TX_MAX_US 100000u

uint32_t ook_tx_clamp_us(uint32_t us);   // pure, host-tested

// Blocking: drive GDO0 to each level (starting at start_level, toggling per
// duration) for that many microseconds, using accumulated absolute deadlines so
// the burst can't drift. Leaves the pin low (carrier off) at the end.
void     ook_tx_send(const uint32_t *durs, uint32_t n, bool start_level);
#endif
```

Create `bsp/radio/ook_tx.c`:

```c
// src/radio/ook_tx.c
#include "radio/ook_tx.h"

uint32_t ook_tx_clamp_us(uint32_t us) {
    return us > OOK_TX_MAX_US ? OOK_TX_MAX_US : us;
}

#ifndef HOST_TEST
#include "platform/board.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"

void ook_tx_send(const uint32_t *durs, uint32_t n, bool start_level) {
    // Take GDO0 from the capture PIO and drive it as a plain output.
    gpio_set_function(PIN_CC1101_GDO0, GPIO_FUNC_SIO);
    gpio_set_dir(PIN_CC1101_GDO0, GPIO_OUT);

    bool level = start_level;
    absolute_time_t t = get_absolute_time();
    for (uint32_t i = 0; i < n; i++) {
        gpio_put(PIN_CC1101_GDO0, level);
        t = delayed_by_us(t, ook_tx_clamp_us(durs[i]));   // accumulate -> no drift
        busy_wait_until(t);
        level = !level;
    }
    gpio_put(PIN_CC1101_GDO0, 0);   // carrier off
}
#endif
```

- [ ] **Step 5: Run tests, verify PASS**

Run: `tools/fw test`
Expected: all PASS including `ook_tx` (7 total).

- [ ] **Step 6: Commit**

```bash
git add bsp/radio/ook_tx.h bsp/radio/ook_tx.c tests/test_ook_tx.c tests/CMakeLists.txt
git commit -m "feat(radio): harvest ook_tx transmit + host test"
```

---

### Task 5: Harvest `scan_engine` (RSSI sweep) + host test

Split module: `scan_freq_at()`, `scan_track_peak()`, `scan_preset()` are pure; the sweep state machine (behind `#ifndef HOST_TEST`) calls `cc1101_*`. Host test compiles with `HOST_TEST`. The hardware half `#include`s `radio/cc1101.h` — that header does not exist until Task 6, but it is excluded under `HOST_TEST`, so the host build is unaffected; the target build gets the header in Task 6.

**Files:**
- Create: `bsp/radio/scan_engine.h`
- Create: `bsp/radio/scan_engine.c`
- Create: `tests/test_scan_engine.c`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes (hardware half only, guarded): `cc1101_set_frequency`, `cc1101_strobe_rx`, `cc1101_read_rssi_dbm`, `cc1101_freq_in_band` (Tasks 1/6).
- Produces:
  - `typedef struct { uint32_t freq_hz; int rssi_dbm; bool valid; } scan_peak_t;`
  - `uint32_t scan_freq_at(uint32_t start_hz, uint32_t step_hz, uint16_t n, uint16_t i);`
  - `scan_peak_t scan_track_peak(scan_peak_t cur, uint32_t freq_hz, int rssi_dbm);`
  - `void scan_begin(uint32_t start_hz, uint32_t step_hz, uint16_t n);`
  - `void scan_step(void);`
  - `scan_peak_t scan_get_peak(void);`
  - `bool scan_get_last(uint32_t *freq_hz, int *rssi_dbm);`
  - `bool scan_preset(int idx, uint32_t *start_hz, uint32_t *step_hz, uint16_t *n);`
  - `bool scan_take_row(int *out, uint16_t n);`

- [ ] **Step 1: Write the ported host test**

Create `tests/test_scan_engine.c` (note the `radio/` include prefix):

```c
#include "test_util.h"
#include "radio/scan_engine.h"

int main(void) {
    // frequency stepping
    ASSERT_EQ(scan_freq_at(433000000u, 100000u, 10, 0), 433000000u);
    ASSERT_EQ(scan_freq_at(433000000u, 100000u, 10, 5), 433500000u);

    // peak tracking keeps the strongest (least-negative) RSSI
    scan_peak_t p = { 0, -200, false };
    p = scan_track_peak(p, 433000000u, -80);
    ASSERT_EQ((int)p.valid, 1);
    ASSERT_EQ(p.freq_hz, 433000000u);
    ASSERT_EQ(p.rssi_dbm, -80);
    p = scan_track_peak(p, 434000000u, -50);   // stronger -> replaces
    ASSERT_EQ(p.freq_hz, 434000000u);
    ASSERT_EQ(p.rssi_dbm, -50);
    p = scan_track_peak(p, 435000000u, -90);   // weaker -> ignored
    ASSERT_EQ(p.freq_hz, 434000000u);

    // band presets (pure): idx 1 = 433 band, 128 bins over 433.0..435.0 MHz
    uint32_t ps_start = 0, ps_step = 0; uint16_t ps_n = 0;
    ASSERT_EQ((int)scan_preset(1, &ps_start, &ps_step, &ps_n), 1);
    ASSERT_EQ(ps_start, 433000000u);
    ASSERT_EQ(ps_n, 128);
    ASSERT_EQ(ps_step, (435000000u - 433000000u) / 128u);   // 15625
    ASSERT_EQ((int)scan_preset(0, &ps_start, &ps_step, &ps_n), 1);
    ASSERT_EQ(ps_start, 314000000u);
    ASSERT_EQ((int)scan_preset(9, &ps_start, &ps_step, &ps_n), 0);  // out of range
    TEST_RETURN();
}
```

- [ ] **Step 2: Wire the test target (with `HOST_TEST` define)**

Append to `tests/CMakeLists.txt`:

```cmake
add_executable(test_scan_engine
    test_scan_engine.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../bsp/radio/scan_engine.c)
target_include_directories(test_scan_engine PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../bsp
    ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(test_scan_engine PRIVATE HOST_TEST)
add_test(NAME scan_engine COMMAND test_scan_engine)
```

- [ ] **Step 3: Run tests, verify the new one FAILS to build**

Run: `tools/fw test`
Expected: build error — `../bsp/radio/scan_engine.c` does not exist yet.

- [ ] **Step 4: Create the module (verbatim copy)**

Create `bsp/radio/scan_engine.h`:

```c
#ifndef SCAN_ENGINE_H
#define SCAN_ENGINE_H
#include <stdint.h>
#include <stdbool.h>

typedef struct { uint32_t freq_hz; int rssi_dbm; bool valid; } scan_peak_t;

uint32_t   scan_freq_at(uint32_t start_hz, uint32_t step_hz, uint16_t n, uint16_t i);
scan_peak_t scan_track_peak(scan_peak_t cur, uint32_t freq_hz, int rssi_dbm);

void       scan_begin(uint32_t start_hz, uint32_t step_hz, uint16_t n);
void       scan_step(void);
scan_peak_t scan_get_peak(void);
bool       scan_get_last(uint32_t *freq_hz, int *rssi_dbm);
bool       scan_preset(int idx, uint32_t *start_hz, uint32_t *step_hz, uint16_t *n);
bool       scan_take_row(int *out, uint16_t n);
#endif
```

Create `bsp/radio/scan_engine.c` (verbatim; the bare `#include "scan_engine.h"` resolves via same-directory search):

```c
#include "scan_engine.h"

uint32_t scan_freq_at(uint32_t start_hz, uint32_t step_hz, uint16_t n, uint16_t i) {
    (void)n;
    return start_hz + (uint32_t)step_hz * i;
}

scan_peak_t scan_track_peak(scan_peak_t cur, uint32_t freq_hz, int rssi_dbm) {
    if (!cur.valid || rssi_dbm > cur.rssi_dbm) {
        cur.valid = true; cur.freq_hz = freq_hz; cur.rssi_dbm = rssi_dbm;
    }
    return cur;
}

bool scan_preset(int idx, uint32_t *start_hz, uint32_t *step_hz, uint16_t *n) {
    static const uint32_t BANDS[4][2] = {
        { 314000000u, 316000000u },   // 0: 315 MHz ISM
        { 433000000u, 435000000u },   // 1: 433.92 MHz ISM
        { 867000000u, 869000000u },   // 2: 868 MHz ISM
        { 914000000u, 916000000u },   // 3: 915 MHz ISM
    };
    if (idx < 0 || idx > 3) return false;
    *n = 128;
    *start_hz = BANDS[idx][0];
    *step_hz = (BANDS[idx][1] - BANDS[idx][0]) / 128u;
    return true;
}

#ifndef HOST_TEST
#include "radio/cc1101.h"
#include "radio/cc1101_regs.h"
#include "pico/stdlib.h"

#define SCAN_MAX_BINS 128
// Fixed RX settle after each hop. Per-hop auto-cal is OFF (fast PLL relock), so the
// limiter is AGC/RSSI settling, not calibration or MARCSTATE. Wait a fixed window
// so RSSI is read settled (reading too early returns an inflated AGC transient).
#define RSSI_SETTLE_US 400

static uint32_t s_start, s_step; static uint16_t s_n, s_i;
static scan_peak_t s_peak; static uint32_t s_last_f; static int s_last_rssi; static bool s_have_last;
static int s_row[SCAN_MAX_BINS];        // RSSI of the current sweep, per bin
static int s_row_done[SCAN_MAX_BINS];   // last fully-completed sweep
static bool s_row_ready;
typedef enum { ST_SET_FREQ, ST_ARM_RX, ST_WAIT, ST_READ } st_t;
static st_t s_state; static absolute_time_t s_settle_at;

void scan_begin(uint32_t start_hz, uint32_t step_hz, uint16_t n) {
    if (n > SCAN_MAX_BINS) n = SCAN_MAX_BINS;
    s_start = start_hz; s_step = step_hz; s_n = n; s_i = 0;
    s_peak = (scan_peak_t){0,-200,false}; s_have_last = false; s_state = ST_SET_FREQ;
    s_row_ready = false;
    for (int k = 0; k < SCAN_MAX_BINS; k++) s_row[k] = s_row_done[k] = -200;
}

// Advance to the next bin; on wrap to index 0 a full sweep just completed, so
// snapshot the per-bin row. Fires whether the last bin was read OR skipped
// (out-of-band), so scan_take_row() can never stall on an out-of-band tail bin.
static void scan_advance(void) {
    bool wrapped = (s_i == s_n - 1);
    s_i = (uint16_t)((s_i + 1) % s_n);
    if (wrapped) {
        for (int k = 0; k < s_n; k++) s_row_done[k] = s_row[k];
        s_row_ready = true;
    }
}

void scan_step(void) {
    if (s_n == 0) return;
    uint32_t f = scan_freq_at(s_start, s_step, s_n, s_i);
    switch (s_state) {
        case ST_SET_FREQ:
            if (!cc1101_freq_in_band(f)) {           // skip out-of-band, one advance/call
                scan_advance();
                break;
            }
            cc1101_set_frequency(f);
            s_state = ST_ARM_RX;
            break;
        case ST_ARM_RX:
            cc1101_strobe_rx();
            s_settle_at = make_timeout_time_us(RSSI_SETTLE_US);
            s_state = ST_WAIT;
            break;
        case ST_WAIT:                                // fixed AGC/RSSI settle (no bus op)
            if (absolute_time_diff_us(get_absolute_time(), s_settle_at) <= 0)
                s_state = ST_READ;
            break;
        case ST_READ: {
            int rssi = cc1101_read_rssi_dbm();
            s_last_f = f; s_last_rssi = rssi; s_have_last = true;
            s_row[s_i] = rssi;                       // s_i < s_n <= SCAN_MAX_BINS (clamped in scan_begin)
            s_peak = scan_track_peak(s_peak, f, rssi);
            scan_advance();                          // advances + snapshots on sweep completion
            s_state = ST_SET_FREQ;
            break;
        }
    }
}

bool scan_take_row(int *out, uint16_t n) {
    if (!s_row_ready) return false;
    uint16_t m = (n < s_n) ? n : s_n;
    for (uint16_t k = 0; k < m; k++) out[k] = s_row_done[k];
    s_row_ready = false;
    return true;
}

scan_peak_t scan_get_peak(void) { return s_peak; }
bool scan_get_last(uint32_t *freq_hz, int *rssi_dbm) {
    if (!s_have_last) return false;
    *freq_hz = s_last_f; *rssi_dbm = s_last_rssi; return true;
}
#endif
```

- [ ] **Step 5: Run tests, verify PASS**

Run: `tools/fw test`
Expected: all PASS including `scan_engine` (8 total).

- [ ] **Step 6: Commit**

```bash
git add bsp/radio/scan_engine.h bsp/radio/scan_engine.c tests/test_scan_engine.c tests/CMakeLists.txt
git commit -m "feat(radio): harvest scan_engine RSSI sweep + host test"
```

---

### Task 6: Harvest hardware core (`cc1101` + `gdo_capture`) and wire the BSP library

Add the two hardware modules (the second with the single `pio0`→`pio2` edit), then wire all seven radio sources + the PIO header + `PICO_PIO_USE_GPIO_BASE=1` into `bsp/CMakeLists.txt`, and activate the radio includes in `bsp/fw2.h`. Deliverable: the BSP library compiles the whole radio into an existing app.

**Files:**
- Create: `bsp/radio/cc1101.h`
- Create: `bsp/radio/cc1101.c`
- Create: `bsp/radio/gdo_capture.h`
- Create: `bsp/radio/gdo_capture.c` (with the `pio2` edit)
- Create: `bsp/radio/gdo_capture.pio`
- Modify: `bsp/CMakeLists.txt`
- Modify: `bsp/fw2.h`

**Interfaces:**
- Consumes: `spi_bus_acquire_cc1101/release_cc1101/cc1101_cs` (`platform/spi_bus.h`), `PIN_CC1101_*` (`platform/board.h`), `DIAG` (`platform/diag.h`), `cc1101_regs` (Task 1) — all present.
- Produces (cc1101): `cc1101_init`, `cc1101_read_version`, `cc1101_set_frequency`, `cc1101_set_modulation`, `cc1101_strobe_rx`, `cc1101_calibrate`, `cc1101_read_rssi_dbm`, `cc1101_monitor_rx`, `cc1101_monitor_stop`, `cc1101_tx_ook_start`, `cc1101_tx_ook_stop`.
- Produces (gdo_capture): `gdo_capture_init`, `gdo_capture_start`, `gdo_capture_stop`, `gdo_capture_attach_pin`, `gdo_capture_drain`.

- [ ] **Step 1: Create `bsp/radio/cc1101.h` (verbatim copy)**

```c
#ifndef CC1101_H
#define CC1101_H
#include <stdint.h>
#include <stdbool.h>
#include "radio/cc1101_regs.h"

bool    cc1101_init(void);
uint8_t cc1101_read_version(void);
void    cc1101_set_frequency(uint32_t hz);
void    cc1101_set_modulation(cc1101_mod_t m);
void    cc1101_strobe_rx(void);
void    cc1101_calibrate(void);   // manual PLL cal at current freq (once per band; MCSM0 auto-cal is off)
int     cc1101_read_rssi_dbm(void);
void cc1101_monitor_rx(uint32_t hz, cc1101_mod_t mod);  // async-transparent RX; GDO0 = data
void cc1101_monitor_stop(void);                         // SIDLE + restore IOCFG0
void cc1101_tx_ook_start(uint32_t hz);                  // async-transparent OOK TX; MCU drives GDO0
void cc1101_tx_ook_stop(void);                          // SIDLE + restore RX regs
#endif
```

- [ ] **Step 2: Create `bsp/radio/cc1101.c` (verbatim copy)**

Copy the full file from `subghz/src/radio/cc1101.c` byte-for-byte (237 lines). Its includes (`"radio/cc1101.h"`, `"platform/spi_bus.h"`, `"platform/board.h"`, `"platform/diag.h"`, `"hardware/spi.h"`, `"pico/stdlib.h"`) all resolve against the `bsp/` include root and the Pico SDK. No edits.

- [ ] **Step 3: Create `bsp/radio/gdo_capture.h` (verbatim copy)**

```c
// src/radio/gdo_capture.h
#ifndef GDO_CAPTURE_H
#define GDO_CAPTURE_H
#include <stdint.h>
#include <stdbool.h>

// Off-bus GDO0 edge capture (PIO + DMA). Free-running once started; never touches
// SPI1 / PIN_LCD_CS. Durations are ~1 us ticks the line held each level.
void     gdo_capture_init(void);                       // claim PIO/SM/DMA (once)
void     gdo_capture_start(void);                       // latch level, arm PIO+DMA
void     gdo_capture_stop(void);                        // halt PIO+DMA
void     gdo_capture_attach_pin(void);                  // re-route GDO0 to the PIO (after TX drove it as SIO)
uint32_t gdo_capture_drain(uint32_t *dst, uint32_t max);// copy new durations, return count
#endif
```

- [ ] **Step 4: Create `bsp/radio/gdo_capture.pio` (verbatim copy)**

```
; src/radio/gdo_capture.pio
; Measure how long GDO0 holds each level, pushing one word per edge. x counts
; DOWN from 0xFFFFFFFF; `mov isr, ~x` yields the elapsed loop count (~= ticks).
; Each measure loop is 2 PIO cycles, so run the SM at 2 MHz (clkdiv 125 @ 250 MHz)
; for ~1 us ticks. On counter underflow the pushed value saturates near
; 0xFFFFFFFF -> software reads it as idle. Autopush is disabled; the program
; manually pushes ISR to the RX FIFO (`push noblock`), which DMA then drains.
.program gdo_capture
.wrap_target
    mov x, ~null          ; x = 0xFFFFFFFF
low_loop:
    jmp pin low_done      ; pin high -> the low level ended
    jmp x-- low_loop      ; still low: count down
low_done:
    mov isr, ~x           ; isr = elapsed at low
    push noblock
    mov x, ~null
high_loop:
    jmp pin high_cont     ; pin still high: keep counting
    jmp high_done         ; pin low -> the high level ended
high_cont:
    jmp x-- high_loop
high_done:
    mov isr, ~x           ; isr = elapsed at high
    push noblock
.wrap

% c-sdk {
static inline void gdo_capture_program_init(PIO pio, uint sm, uint offset, uint pin) {
    pio_sm_config c = gdo_capture_program_get_default_config(offset);
    sm_config_set_jmp_pin(&c, pin);          // `jmp pin` tests GDO0
    sm_config_set_in_pins(&c, pin);
    sm_config_set_in_shift(&c, false, false, 32);   // MSB-first, manual push
    sm_config_set_clkdiv(&c, 125.0f);        // 250 MHz / 125 = 2 MHz -> ~1 us/tick
    pio_gpio_init(pio, pin);
    pio_sm_set_consecutive_pindirs(pio, sm, pin, 1, false);  // input
    pio_sm_init(pio, sm, offset, &c);
}
%}
```

- [ ] **Step 5: Create `bsp/radio/gdo_capture.c` (the ONE adaptation: `pio0`→`pio2`)**

Copy `subghz/src/radio/gdo_capture.c` verbatim EXCEPT line 14, which changes the PIO instance from `pio0` to `pio2` (avoids the GPIO-base-16 collision with the audio driver on pio0 and the LED driver on pio1):

```c
// src/radio/gdo_capture.c
#include "radio/gdo_capture.h"
#include "platform/board.h"
#include "platform/diag.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "gdo_capture.pio.h"

#define GDO_RING_LEN 4096u                 // power of two for DMA ring wrap
_Static_assert((1u << 14) == GDO_RING_LEN * sizeof(uint32_t),
    "DMA ring size_bits (14 in channel_config_set_ring) must match GDO_RING_LEN*sizeof(uint32_t)");
static uint32_t s_ring[GDO_RING_LEN] __attribute__((aligned(GDO_RING_LEN * sizeof(uint32_t))));
static PIO   s_pio = pio2;   // ADAPTATION: subghz used pio0; wilibsp reserves pio0=audio, pio1=LEDs
static uint  s_sm, s_offset;
static int   s_dma = -1;
static uint32_t s_tail;                    // next unread index into s_ring

void gdo_capture_init(void) {
    // Reach GPIO32: PIO GPIO base window 16..47 (needs PICO_PIO_USE_GPIO_BASE=1).
    pio_set_gpio_base(s_pio, 16);
    s_offset = pio_add_program(s_pio, &gdo_capture_program);
    s_sm = pio_claim_unused_sm(s_pio, true);
    gdo_capture_program_init(s_pio, s_sm, s_offset, PIN_CC1101_GDO0);

    s_dma = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config(s_dma);
    channel_config_set_read_increment(&c, false);          // read the PIO FIFO
    channel_config_set_write_increment(&c, true);
    channel_config_set_dreq(&c, pio_get_dreq(s_pio, s_sm, false));  // RX dreq
    // Ring on the write address: size_bits counts BYTES, not words.
    // GDO_RING_LEN words * 4 B = 16384 B = 2^14, so wrap every 16384 bytes = 4096 words.
    channel_config_set_ring(&c, true, 14);   // wrap every 16384 bytes = 4096 words
    dma_channel_configure(s_dma, &c, s_ring, &s_pio->rxf[s_sm], 0xFFFFFFFFu, false);
    DIAG("gdo_capture: init pio2 sm=%u dma=%d\n", (unsigned)s_sm, s_dma);
}

void gdo_capture_start(void) {
    s_tail = 0;
    pio_sm_clear_fifos(s_pio, s_sm);
    pio_sm_restart(s_pio, s_sm);
    dma_channel_set_write_addr(s_dma, s_ring, false);
    // 0xFFFFFFFF trans_count = RP2350 MODE=0xF (ENDLESS): transfer forever into the
    // ring. Progress is read from write_addr (see dma_head), not transfer_count,
    // which does not decrement in ENDLESS mode.
    dma_channel_set_trans_count(s_dma, 0xFFFFFFFFu, false);
    dma_channel_start(s_dma);
    pio_sm_set_enabled(s_pio, s_sm, true);
}

void gdo_capture_stop(void) {
    pio_sm_set_enabled(s_pio, s_sm, false);
    dma_channel_abort(s_dma);
}

// Re-assert GDO0 (GPIO32) as a PIO input after OOK TX temporarily drove it as an
// SIO output. Mirrors the pin setup in gdo_capture_program_init; call before
// gdo_capture_start() to resume capture.
void gdo_capture_attach_pin(void) {
    pio_gpio_init(s_pio, PIN_CC1101_GDO0);
    pio_sm_set_consecutive_pindirs(s_pio, s_sm, PIN_CC1101_GDO0, 1, false);
}

// Current ring write index, derived from the DMA WRITE_ADDR. We do NOT use
// transfer_count: on RP2350 a 0xFFFFFFFF trans_count selects MODE=0xF (ENDLESS),
// in which transfer_count does not decrement — so write_addr is the only reliable
// progress indicator. write_addr wraps inside the 16384-byte ring, so subtracting
// the aligned base and dividing by the word size gives the head index directly.
static uint32_t dma_head(void) {
    uint32_t off = dma_channel_hw_addr(s_dma)->write_addr - (uint32_t)(uintptr_t)s_ring;
    return (off / sizeof(uint32_t)) & (GDO_RING_LEN - 1u);
}

uint32_t gdo_capture_drain(uint32_t *dst, uint32_t max) {
    uint32_t head = dma_head();
    uint32_t n = 0;
    while (s_tail != head && n < max) {
        dst[n++] = s_ring[s_tail];
        s_tail = (s_tail + 1u) & (GDO_RING_LEN - 1u);
    }
    return n;
}
```

- [ ] **Step 6: Wire `bsp/CMakeLists.txt`**

In `add_library(freewili2_bsp STATIC ...)`, add the seven radio sources after the `audio/*.c` block (before `gfx/palette.c`):

```cmake
    radio/cc1101_regs.c
    radio/cc1101.c
    radio/gdo_capture.c
    radio/monitor_engine.c
    radio/ook_tx.c
    radio/scan_engine.c
    radio/capture_store.c
```

Add a `target_compile_definitions` block (GPIO≥32 PIO access needs the multi-base window) right after the `target_link_libraries(freewili2_bsp ...)` block:

```cmake
# GDO0 = GPIO32; PIO must use the GPIO base window 16..47 to reach it.
target_compile_definitions(freewili2_bsp PUBLIC PICO_PIO_USE_GPIO_BASE=1)
```

Add the PIO header generation next to the existing `pico_generate_pio_header` calls:

```cmake
# Generate the GDO0 capture PIO header into the build tree for gdo_capture.c.
pico_generate_pio_header(freewili2_bsp
    ${CMAKE_CURRENT_SOURCE_DIR}/radio/gdo_capture.pio)
```

(`hardware_pio`, `hardware_dma`, `hardware_spi` are already in `target_link_libraries` — no new components.)

- [ ] **Step 7: Activate the radio includes in `bsp/fw2.h`**

Add before the closing `#endif // FW2_H`:

```c
#include "radio/cc1101.h"          // (Task 3-radio)
#include "radio/gdo_capture.h"     // (Task 3-radio)
#include "radio/scan_engine.h"     // (Task 3-radio)
#include "radio/ook_tx.h"          // (Task 3-radio)
#include "radio/monitor_engine.h"  // (Task 3-radio)
#include "radio/capture_store.h"   // (Task 3-radio)
```

- [ ] **Step 8: Verify the BSP library compiles into an existing app**

Set up the Pico SDK env (paths confirmed present) if not already:

```bash
export PICO_SDK_PATH="$HOME/.pico-sdk/sdk/2.2.0"
export PICO_TOOLCHAIN_PATH="$HOME/.pico-sdk/toolchain/14_2_Rel1"
export PATH="$HOME/.pico-sdk/picotool/2.2.0-a4/picotool:$PATH"
```

Run: `tools/fw build hello_audio`
Expected: clean build. The `freewili2_bsp` static library now compiles all seven radio `.c` files and generates `gdo_capture.pio.h`; `hello_audio` links unchanged. (No radio symbols are referenced by `hello_audio`, but every radio `.c` must compile.)

- [ ] **Step 9: Re-run host tests (guard against regressions)**

Run: `tools/fw test`
Expected: 8/8 PASS (the 5 radio tests still build against the same source files).

- [ ] **Step 10: Commit**

```bash
git add bsp/radio/cc1101.h bsp/radio/cc1101.c bsp/radio/gdo_capture.h bsp/radio/gdo_capture.c bsp/radio/gdo_capture.pio bsp/CMakeLists.txt bsp/fw2.h
git commit -m "feat(radio): harvest cc1101 core + gdo_capture (pio2); wire into BSP"
```

---

### Task 7: Demo app `apps/hello_cc1101` (RTT self-verify)

Linear probe → RSSI sweep → same-pad TX/capture check, all over RTT. No display, no LVGL. Verifies SPI, RSSI, PIO2/DMA capture, and OOK-TX timing with zero external RF gear.

**Files:**
- Create: `apps/hello_cc1101/main.c`
- Create: `apps/hello_cc1101/CMakeLists.txt`
- Modify: `CMakeLists.txt` (top-level: add `add_subdirectory`)

**Interfaces:**
- Consumes: `board_init`, `ioexp_antenna`/`ANT_CC1101_433`, and all `cc1101_*`, `scan_*`, `gdo_capture_*`, `ook_tx_send` from the BSP (via `fw2.h`).
- Produces: an RTT diagnostic sequence and a flashable ELF.

- [ ] **Step 1: Write the demo `main.c`**

Create `apps/hello_cc1101/main.c`:

```c
// hello_cc1101 — on-hardware smoke test for the CC1101 sub-GHz radio driver.
// Three phases over SEGGER RTT (fw rtt), no display, no external RF gear:
//   1) probe the chip over SPI1 (PARTNUM/VERSION);
//   2) sweep RSSI across the 433 MHz ISM band and report floor/peak;
//   3) same-pad TX->capture check: drive an OOK pulse train on GDO0 while the
//      capture PIO (pio2) + DMA records the same pad, then report the edge count.
// Phase 3 is a plumbing test (one pin can't RX its own TX over the air); it proves
// the SPI TX-config path, ook_tx timing, and the PIO2/DMA/drain chain end to end.
#include "fw2.h"
#include "platform/diag.h"
#include "platform/ioexp.h"
#include "pico/stdlib.h"

int main(void) {
    board_init();   // 250 MHz + vreg + clk_peri re-source; also ioexp_init + I2C1
    DIAG("\n=== hello_cc1101: sub-GHz radio smoke test ===\n");

    // --- Phase 1: probe ---
    ioexp_antenna(ANT_CC1101_433);          // route the 433 MHz antenna
    bool ok = cc1101_init();                // DIAGs PARTNUM/VERSION internally
    DIAG("cc1101: probe %s\n", ok ? "PASS" : "FAIL");
    if (!ok) {
        DIAG("cc1101: halting — no radio on SPI1 (check bus/power/antenna)\n");
        while (1) tight_loop_contents();
    }

    // --- Phase 2: RSSI sweep across the 433 MHz ISM band ---
    uint32_t start_hz, step_hz; uint16_t nbins;
    scan_preset(1, &start_hz, &step_hz, &nbins);   // idx 1 = 433 band, 128 bins
    cc1101_set_modulation(CC1101_MOD_ASK_OOK);
    cc1101_set_frequency(start_hz + step_hz * (nbins / 2));  // band center
    cc1101_calibrate();                            // one manual cal for the band
    scan_begin(start_hz, step_hz, nbins);
    // Drive the async sweep to completion of one full row (each bin needs
    // ST_SET_FREQ -> ST_ARM_RX -> ST_WAIT (400us settle) -> ST_READ, so several
    // scan_step() calls per bin; give generous slack). row[] is pre-zeroed so the
    // floor computation is safe even if the row never completes within the guard.
    int row[128] = {0};
    bool have_row = false;
    for (int guard = 0; guard < nbins * 16 && !have_row; guard++) {
        have_row = scan_take_row(row, nbins);
        scan_step();
        sleep_us(100);
    }
    scan_peak_t pk = scan_get_peak();   // peak accumulates per-bin, valid with or without a full row
    int floor_dbm = 0;
    for (int i = 0; i < nbins; i++) if (row[i] < floor_dbm) floor_dbm = row[i];
    DIAG("scan: row=%s floor=%d dBm  peak=%d dBm @ %u Hz\n",
         have_row ? "ok" : "partial", floor_dbm, pk.rssi_dbm, (unsigned)pk.freq_hz);

    // --- Phase 3: same-pad TX -> capture check ---
    gdo_capture_init();                     // claim pio2 SM + DMA
    gdo_capture_start();
    cc1101_tx_ook_start(433920000u);        // OOK TX regs + key carrier; 3-states chip GDO0
    // Synthesized OOK train: 24 pulses of 500 us each (level toggles per entry).
    static const uint32_t train[24] = {
        500,500,500,500,500,500,500,500,
        500,500,500,500,500,500,500,500,
        500,500,500,500,500,500,500,500,
    };
    ook_tx_send(train, 24, true);           // drives GDO0/GPIO32 as SIO output
    cc1101_tx_ook_stop();
    gdo_capture_attach_pin();               // re-route GDO0 to the capture PIO
    sleep_ms(2);
    static uint32_t drained[128];
    uint32_t got = gdo_capture_drain(drained, 128);
    DIAG("capture: sent=24 pulses, drained=%u edges (nonzero => PIO2/DMA path live)\n",
         (unsigned)got);
    DIAG("cc1101: smoke test complete\n");

    while (1) tight_loop_contents();
}
```

- [ ] **Step 2: Write the app `CMakeLists.txt`**

Create `apps/hello_cc1101/CMakeLists.txt`:

```cmake
add_executable(hello_cc1101 main.c)
target_link_libraries(hello_cc1101 freewili2_bsp)
pico_set_binary_type(hello_cc1101 copy_to_ram)
pico_add_extra_outputs(hello_cc1101)
```

- [ ] **Step 3: Register the app in the top-level `CMakeLists.txt`**

Append after `add_subdirectory(apps/hello_audio)`:

```cmake
add_subdirectory(apps/hello_cc1101)
```

- [ ] **Step 4: Build the demo**

Ensure the Pico SDK env is set (see Task 6 Step 8), then run: `tools/fw build hello_cc1101`
Expected: clean build; produces `build/apps/hello_cc1101/hello_cc1101.elf` (+ `.uf2`).

- [ ] **Step 5: Re-run host tests (no regressions)**

Run: `tools/fw test`
Expected: 8/8 PASS.

- [ ] **Step 6: Commit**

```bash
git add apps/hello_cc1101/main.c apps/hello_cc1101/CMakeLists.txt CMakeLists.txt
git commit -m "feat(radio): add hello_cc1101 RTT smoke-test app"
```

---

### Task 8: Documentation — catalog, facts, pinmap, driver doc

Record the increment as DONE and capture the PIO2 decision so it is never relearned.

**Files:**
- Modify: `docs/hardware/catalog.md`
- Modify: `docs/hardware/facts.md`
- Modify: `docs/hardware/pinmap.md`
- Create: `docs/drivers/radio.md`

- [ ] **Step 1: Flip the catalog row to DONE**

In `docs/hardware/catalog.md`, move the **Sub-GHz radio — CC1101** entry out of the TODO table into the DONE table with a row like:

```markdown
| Sub-GHz radio — CC1101 | `bsp/radio/{cc1101,cc1101_regs,gdo_capture,monitor_engine,ook_tx,scan_engine,capture_store}.{c,h}`, `bsp/radio/gdo_capture.pio` | SPI1 (shared with LCD via `spi_bus` arbiter, 5 MHz); GDO0 capture on **pio2** + ENDLESS DMA (polled, no IRQ); OOK TX bit-bangs GDO0. Harvested from `subghz` (MIT). Demo: `apps/hello_cc1101`. |
```

Update the "DONE count" prose at the top and bottom of the file (five → six peripherals) and confirm the source list matches `bsp/CMakeLists.txt`.

- [ ] **Step 2: Add the PIO2 / GPIO-base invariant to `facts.md`**

Append a record to `docs/hardware/facts.md`:

```markdown
## Radio: GDO0 capture runs on PIO2, not PIO0

`bsp/radio/gdo_capture.c` sets `s_pio = pio2` and calls `pio_set_gpio_base(pio2, 16)`
so it can reach GDO0 = GPIO32 (the PIO GPIO-base window must move to 16..47).
`pio_set_gpio_base` shifts the base for the WHOLE PIO block, so this cannot run on
`pio0` (audio I2S, GPIO 4–7) or `pio1` (WS2812 LEDs, GPIO 21) without breaking their
low-GPIO access. `pio2` is otherwise unused. The BSP is built with
`PICO_PIO_USE_GPIO_BASE=1` (a PUBLIC compile def on `freewili2_bsp`) — required for any
GPIO≥32 PIO access. The subghz source used `pio0`; the `pio0`→`pio2` change is the only
functional edit made during the harvest. GDO2 (GPIO37) remains unused. The capture DMA
is ENDLESS and polled (`write_addr`), so it registers no `DMA_IRQ_0` handler.
```

- [ ] **Step 3: Update `pinmap.md`**

In `docs/hardware/pinmap.md`, update the CC1101 rows so GDO0/GPIO32 notes read "PIO2-sampled edge capture (`gdo_capture`)" instead of the "deferred / not yet harvested" note, and confirm GDO2/GPIO37 is marked unused.

- [ ] **Step 4: Write the driver usage doc**

Create `docs/drivers/radio.md` documenting: init/probe (`cc1101_init` presence check semantics), frequency/modulation setters, the async RSSI sweep (`scan_preset`/`scan_begin`/`scan_step`/`scan_take_row`/`scan_get_peak`), monitor RX + `gdo_capture` drain, OOK TX (`cc1101_tx_ook_start`/`ook_tx_send`/`cc1101_tx_ook_stop` + `gdo_capture_attach_pin`), `capture_store` clip API, and antenna selection (`ioexp_antenna(ANT_CC1101_*)`). Note the pio2 fact and that all diagnostics are integer-only (RTT has no float support).

- [ ] **Step 5: Commit**

```bash
git add docs/hardware/catalog.md docs/hardware/facts.md docs/hardware/pinmap.md docs/drivers/radio.md
git commit -m "docs(radio): mark CC1101 DONE; record pio2 capture invariant + usage"
```

---

## After all tasks

1. **Whole-branch review** — `superpowers:requesting-code-review` (opus final pass) over the `feat/cc1101-radio` branch diff vs `master`.
2. **On-hardware verify** — with the Pico SDK env set: `tools/fw flash hello_cc1101` then `tools/fw rtt -s 15`. Expect RTT to show: `cc1101: PARTNUM=0x00 VERSION=0x…` with a plausible (non-0x00/0xFF) version and `probe PASS`; a `scan:` line with a sane floor (e.g. −90…−110 dBm) and peak; and a `capture:` line with `drained=` on the order of the 24 pulses sent (nonzero confirms the PIO2/DMA path). Record results under `docs/superpowers/findings/2026-07-04-cc1101-radio-e2e.md`.
3. **Finish the branch** — `superpowers:finishing-a-development-branch`: merge `feat/cc1101-radio` → `master`.

## Self-Review Notes

- **Spec coverage:** All 7 modules (Tasks 1–6), CMake + fw2.h wiring (Task 6), 5 host tests (Tasks 1–5), the pio0→pio2 adaptation + `PICO_PIO_USE_GPIO_BASE=1` (Task 6), the RTT demo (Task 7), and all doc updates (Task 8) trace to spec sections. On-hardware verify + review are in the After-all-tasks section.
- **Type consistency:** function names and signatures in the Interfaces blocks match the harvested headers verbatim (`cc1101_*`, `scan_*`, `gdo_capture_*`, `ook_tx_*`, `capture_store_*`, `monitor_*`).
- **Placeholder scan:** `cc1101.c` (Task 6 Step 2) is the one file referenced by "copy verbatim" rather than inlined, because it is a 237-line byte-for-byte copy with no edits and its full text is in the source repo; every file that receives ANY change (`gdo_capture.c`, all tests, CMake, fw2.h, demo) is shown in full.
