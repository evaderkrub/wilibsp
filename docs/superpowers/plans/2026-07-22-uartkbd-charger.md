# uartkbd Charger Telemetry + hello_charger Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Decode charger telemetry (status-frame bytes 10–21) in the FW2 UART keyboard driver and add an `apps/hello_charger` test screen that shows the whole frame live.

**Architecture:** The pure parser (`bsp/input/uartkbd_parse.*`) captures raw bytes 10–21 on every checksum-valid frame; all scaling runs on demand inside `uartkbd_parse_charger()` (owner requirement: no scaling unless the call is made). A one-line passthrough in `bsp/input/uartkbd.*` exposes it to apps. `hello_charger` renders a full-frame status screen into the PSRAM framebuffer.

**Tech Stack:** C11, Pico SDK (RP2350B), host CTest tree (`python tools/fw.py test`), ST7796 + font5x7 rendering per `apps/hello_keyboard`.

**Spec:** `docs/superpowers/specs/2026-07-22-uartkbd-charger-design.md`. Protocol authority: `C:\~prj\Dropbox\FreeWilli\vibe\Wilikeyboard.md`.

## Global Constraints

- **Repo root is `C:\~prj\Dropbox\vibeProjects\wilibsp`** — all paths and commands below are relative to it (NOT the wilikeyboard repo).
- Host tests: `python tools/fw.py test` (MinGW GCC + Ninja, no Pico SDK). Target build: `python tools/fw.py build <app>`.
- Never pass `-DPICO_BOARD` on any cmake command line (AGENTS.md invariant 1/8).
- Diagnostics via `DIAG()` only (SEGGER RTT; `%d %u %x %s %c`, **no floats**). No USB/UART stdio. `snprintf` into buffers for on-screen text is fine.
- Apps are `pico_set_binary_type(<app> copy_to_ram)`; framebuffers live in PSRAM (`PSRAM_BASE`).
- Conventional Commits (`feat:`, `test:`, `docs:` …), imperative subject.
- Scaling formulas (frame byte → engineering unit), from the spec:
  - byte10 VBUS: mV = 2600 + code·100
  - byte11 VSys: mV = 2304 + code·20
  - byte12 VBatt: mV = 2304 + code·20
  - byte13 Current: mA = code·50
  - byte14 Temp: tspct = code·465/100 + 210 (tenths of percent)
  - byte15 Charge Status: 0 NotCharging, 1 PreCharge, 2 FastCharge, 3 Done
  - byte16 VBus Status: 0 NoInput, 1 USB_HOST, 2 ADAPTER, 7 OTG
  - byte17 Fault: 0 Normal, 1 Input, 2 Thermal, 3 TimerExpired
  - byte18 Temp Rank: 0 Normal, 2 Warm, 3 Cool, 5 Cold, 6 Hot
  - byte19 bitfield: b4:3 CC tier (0 None, 1 500mA, 2 1A5, 3 3A), b2 vsys_regulation, b1 thermal_regulation, b0 vbus_attached
  - byte20/21 CC1/CC2: mV = code·8

---

### Task 1: Parser — raw capture + on-demand charger snapshot

**Files:**
- Modify: `bsp/input/uartkbd_parse.h`
- Modify: `bsp/input/uartkbd_parse.c`
- Test: `tests/test_uartkbd_parse.c`

**Interfaces:**
- Consumes: existing `uartkbd_parser_t`, `uartkbd_parse_init/byte`, test helpers `mk_frame()`/`feed()`/`prime()` in `tests/test_uartkbd_parse.c`.
- Produces: `uartkbd_charger_t` (struct below), `bool uartkbd_parse_charger(const uartkbd_parser_t *p, uartkbd_charger_t *out)`, `UARTKBD_CHG_*` / `UARTKBD_VBUS_*` / `UARTKBD_FAULT_*` / `UARTKBD_RANK_*` / `UARTKBD_CC_*` defines. Task 3's binding and Task 4's app use these exact names.

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_uartkbd_parse.c`, before `main()` (the existing `mk_frame`, `feed`, `prime` helpers stay as they are — `mk_frame` already checksums all 22 bytes, so zero charger bytes stay valid):

```c
/* Build a valid frame with the given charger payload bytes 10-21
 * (buttons all idle). */
static void mk_frame_chg(uint8_t f[UARTKBD_FRAME_LEN], const uint8_t chg[12])
{
    memset(f, 0, UARTKBD_FRAME_LEN);
    f[0] = 0xBD; f[1] = 0x1D;
    memcpy(&f[10], chg, 12);
    uint8_t sum = 0;
    for (int i = 0; i < UARTKBD_FRAME_LEN - 1; i++) sum = (uint8_t)(sum + f[i]);
    f[UARTKBD_FRAME_LEN - 1] = sum;
}

static void test_charger_unavailable_before_first_frame(void)
{
    uartkbd_parser_t p;
    uartkbd_parse_init(&p);
    uartkbd_charger_t c;
    CHECK(!uartkbd_parse_charger(&p, &c));
}

static void test_charger_available_from_priming_frame(void)
{
    /* The button baseline needs two frames (priming), but charger data is
     * level state — it must be readable after the very first valid frame. */
    uartkbd_parser_t p;
    uartkbd_parse_init(&p);
    prime(&p);                     /* one all-zero frame */
    uartkbd_charger_t c;
    CHECK(uartkbd_parse_charger(&p, &c));
    CHECK(c.vbus_mv == 2600);      /* code 0 endpoints */
    CHECK(c.vsys_mv == 2304);
    CHECK(c.vbatt_mv == 2304);
    CHECK(c.current_ma == 0);
    CHECK(c.temp_tspct == 210);
    CHECK(c.charge_status == UARTKBD_CHG_NOT_CHARGING);
    CHECK(c.vbus_status == UARTKBD_VBUS_NONE);
    CHECK(c.fault == UARTKBD_FAULT_NORMAL);
    CHECK(c.temp_rank == UARTKBD_RANK_NORMAL);
    CHECK(c.cc_tier == UARTKBD_CC_NONE);
    CHECK(!c.vsys_regulation && !c.thermal_regulation && !c.vbus_attached);
    CHECK(c.cc1_mv == 0 && c.cc2_mv == 0);
}

static void test_charger_scaling_and_decode(void)
{
    uartkbd_parser_t p;
    uartkbd_parse_init(&p);
    prime(&p);
    /* bytes 10-21: vbus=127, vsys=127, vbatt=64, curr=127, temp=255,
     * status=FastCharge, vbus_status=OTG, fault=Thermal, rank=Cold,
     * bitfield=0x1D (tier 3 + vsys_reg + vbus_attached), cc1=255, cc2=100 */
    const uint8_t chg[12] = { 127, 127, 64, 127, 255, 2, 7, 2, 5, 0x1D, 255, 100 };
    uint8_t f[UARTKBD_FRAME_LEN];
    mk_frame_chg(f, chg);
    feed(&p, f, UARTKBD_FRAME_LEN);

    uartkbd_charger_t c;
    CHECK(uartkbd_parse_charger(&p, &c));
    CHECK(c.vbus_mv == 15300);                 /* 2600 + 127*100 */
    CHECK(c.vsys_mv == 4844);                  /* 2304 + 127*20 */
    CHECK(c.vbatt_mv == 3584);                 /* 2304 + 64*20 */
    CHECK(c.current_ma == 6350);               /* 127*50 */
    CHECK(c.temp_tspct == 1395);               /* 255*465/100 + 210 */
    CHECK(c.charge_status == UARTKBD_CHG_FASTCHARGE);
    CHECK(c.vbus_status == UARTKBD_VBUS_OTG);
    CHECK(c.fault == UARTKBD_FAULT_THERMAL);
    CHECK(c.temp_rank == UARTKBD_RANK_COLD);
    CHECK(c.cc_tier == UARTKBD_CC_3A);
    CHECK(c.vsys_regulation);
    CHECK(!c.thermal_regulation);
    CHECK(c.vbus_attached);
    CHECK(c.cc1_mv == 2040);                   /* 255*8 */
    CHECK(c.cc2_mv == 800);                    /* 100*8 */
    /* no button events from a charger-only change */
    uartkbd_event_t ev;
    CHECK(!uartkbd_parse_next_event(&p, &ev));
}

static void test_charger_midpoint_scaling(void)
{
    uartkbd_parser_t p;
    uartkbd_parse_init(&p);
    prime(&p);
    const uint8_t chg[12] = { 50, 100, 1, 10, 100, 0, 0, 0, 0, 0, 0, 1 };
    uint8_t f[UARTKBD_FRAME_LEN];
    mk_frame_chg(f, chg);
    feed(&p, f, UARTKBD_FRAME_LEN);
    uartkbd_charger_t c;
    CHECK(uartkbd_parse_charger(&p, &c));
    CHECK(c.vbus_mv == 7600);      /* 2600 + 50*100 */
    CHECK(c.vsys_mv == 4304);      /* 2304 + 100*20 */
    CHECK(c.vbatt_mv == 2324);     /* 2304 + 1*20 */
    CHECK(c.current_ma == 500);    /* 10*50 */
    CHECK(c.temp_tspct == 675);    /* 100*465/100 + 210 */
    CHECK(c.cc2_mv == 8);
}

static void test_charger_undocumented_code_passthrough(void)
{
    /* Enum fields carry the raw code verbatim — vbus_status 5 is not in the
     * doc (0,1,2,7) and must pass through unclamped. */
    uartkbd_parser_t p;
    uartkbd_parse_init(&p);
    prime(&p);
    const uint8_t chg[12] = { 0, 0, 0, 0, 0, 9, 5, 7, 4, 0, 0, 0 };
    uint8_t f[UARTKBD_FRAME_LEN];
    mk_frame_chg(f, chg);
    feed(&p, f, UARTKBD_FRAME_LEN);
    uartkbd_charger_t c;
    CHECK(uartkbd_parse_charger(&p, &c));
    CHECK(c.charge_status == 9);
    CHECK(c.vbus_status == 5);
    CHECK(c.fault == 7);
    CHECK(c.temp_rank == 4);
}

static void test_charger_bad_checksum_keeps_old_snapshot(void)
{
    uartkbd_parser_t p;
    uartkbd_parse_init(&p);
    prime(&p);
    const uint8_t good[12] = { 50, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0 };
    uint8_t f[UARTKBD_FRAME_LEN];
    mk_frame_chg(f, good);
    feed(&p, f, UARTKBD_FRAME_LEN);

    const uint8_t bad[12] = { 99, 0, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0 };
    mk_frame_chg(f, bad);
    f[UARTKBD_FRAME_LEN - 1] ^= 0xFF;          /* corrupt checksum */
    feed(&p, f, UARTKBD_FRAME_LEN);

    uartkbd_charger_t c;
    CHECK(uartkbd_parse_charger(&p, &c));
    CHECK(c.vbus_mv == 7600);                  /* still the good frame's 50 */
    CHECK(c.charge_status == UARTKBD_CHG_PRECHARGE);
}
```

And register them in `main()` (after `test_first_frame_primes_without_events();`):

```c
    test_charger_unavailable_before_first_frame();
    test_charger_available_from_priming_frame();
    test_charger_scaling_and_decode();
    test_charger_midpoint_scaling();
    test_charger_undocumented_code_passthrough();
    test_charger_bad_checksum_keeps_old_snapshot();
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `python tools/fw.py test`
Expected: build FAILS — `unknown type name 'uartkbd_charger_t'`, `UARTKBD_CHG_NOT_CHARGING undeclared`. (A compile failure of the test target is the failing state for a to-be-written API.)

- [ ] **Step 3: Implement the parser changes**

In `bsp/input/uartkbd_parse.h`:

1. Update the header comment's protocol line to mention charger bytes: change
   `button bitmaps in bytes 2-5, additive 8-bit checksum` to
   `button bitmaps in bytes 2-5, charger telemetry in bytes 10-21, additive 8-bit checksum`.

2. After the `UARTKBD_FLAG_*` defines, add:

```c
/* Charger enum codes (fields carry the frame's raw code verbatim, so
 * undocumented codes pass through — compare, don't assume exhaustive). */
#define UARTKBD_CHG_NOT_CHARGING 0u   /* charge_status */
#define UARTKBD_CHG_PRECHARGE    1u
#define UARTKBD_CHG_FASTCHARGE   2u
#define UARTKBD_CHG_DONE         3u
#define UARTKBD_VBUS_NONE        0u   /* vbus_status */
#define UARTKBD_VBUS_USB_HOST    1u
#define UARTKBD_VBUS_ADAPTER     2u
#define UARTKBD_VBUS_OTG         7u
#define UARTKBD_FAULT_NORMAL     0u   /* fault */
#define UARTKBD_FAULT_INPUT      1u
#define UARTKBD_FAULT_THERMAL    2u
#define UARTKBD_FAULT_TIMER      3u
#define UARTKBD_RANK_NORMAL      0u   /* temp_rank */
#define UARTKBD_RANK_WARM        2u
#define UARTKBD_RANK_COOL        3u
#define UARTKBD_RANK_COLD        5u
#define UARTKBD_RANK_HOT         6u
#define UARTKBD_CC_NONE          0u   /* cc_tier */
#define UARTKBD_CC_500MA         1u
#define UARTKBD_CC_1A5           2u
#define UARTKBD_CC_3A            3u

/* Charger snapshot, scaled to engineering units on demand by
 * uartkbd_parse_charger() (frame time only captures raw bytes). */
typedef struct {
    uint16_t vbus_mv;              /* 2600 + code*100 */
    uint16_t vsys_mv;              /* 2304 + code*20  */
    uint16_t vbatt_mv;             /* 2304 + code*20  */
    uint16_t current_ma;           /* code*50         */
    uint16_t temp_tspct;           /* code*465/100 + 210, tenths of percent */
    uint8_t  charge_status;        /* UARTKBD_CHG_*   */
    uint8_t  vbus_status;          /* UARTKBD_VBUS_*  */
    uint8_t  fault;                /* UARTKBD_FAULT_* */
    uint8_t  temp_rank;            /* UARTKBD_RANK_*  */
    uint8_t  cc_tier;              /* UARTKBD_CC_*    */
    bool     vsys_regulation;
    bool     thermal_regulation;
    bool     vbus_attached;
    uint16_t cc1_mv;               /* code*8 */
    uint16_t cc2_mv;               /* code*8 */
} uartkbd_charger_t;
```

3. In `uartkbd_parser_t`, after the `flags` member, add:

```c
    uint8_t  charger_raw[12];              /* frame bytes 10-21, last valid */
    bool     charger_valid;                /* any valid frame seen yet */
```

4. After the `uartkbd_parse_flags` declaration, add:

```c
bool     uartkbd_parse_charger(const uartkbd_parser_t *p, uartkbd_charger_t *out);
```

In `bsp/input/uartkbd_parse.c`:

1. In `accept_frame()`, right after `p->flags = decode_flags(p->frame);`, add:

```c
    memcpy(p->charger_raw, &p->frame[10], sizeof p->charger_raw);
    p->charger_valid = true;
```

(This runs before the priming early-return on purpose: charger data is level
state and must be readable from the first valid frame.)

2. At the end of the file, add:

```c
/* Scaling per Wilikeyboard.md bytes 10-21 — applied only here, on demand. */
bool uartkbd_parse_charger(const uartkbd_parser_t *p, uartkbd_charger_t *out)
{
    if (!p->charger_valid) return false;
    const uint8_t *r = p->charger_raw;
    out->vbus_mv    = (uint16_t)(2600u + r[0] * 100u);
    out->vsys_mv    = (uint16_t)(2304u + r[1] * 20u);
    out->vbatt_mv   = (uint16_t)(2304u + r[2] * 20u);
    out->current_ma = (uint16_t)(r[3] * 50u);
    out->temp_tspct = (uint16_t)(r[4] * 465u / 100u + 210u);
    out->charge_status = r[5];
    out->vbus_status   = r[6];
    out->fault         = r[7];
    out->temp_rank     = r[8];
    out->cc_tier            = (uint8_t)((r[9] >> 3) & 0x03u);
    out->vsys_regulation    = (r[9] & 0x04u) != 0;
    out->thermal_regulation = (r[9] & 0x02u) != 0;
    out->vbus_attached      = (r[9] & 0x01u) != 0;
    out->cc1_mv = (uint16_t)(r[10] * 8u);
    out->cc2_mv = (uint16_t)(r[11] * 8u);
    return true;
}
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `python tools/fw.py test`
Expected: all tests pass, including `uartkbd_parse` printing `test_uartkbd_parse: all passed`.

- [ ] **Step 5: Commit**

```bash
git add bsp/input/uartkbd_parse.h bsp/input/uartkbd_parse.c tests/test_uartkbd_parse.c
git commit -m "feat: decode charger telemetry from UART keyboard status frame"
```

---

### Task 2: NTC temperature helper (tspct → °C)

**Files:**
- Modify: `bsp/input/uartkbd_parse.h`
- Modify: `bsp/input/uartkbd_parse.c`
- Modify: `tests/CMakeLists.txt` (link libm)
- Test: `tests/test_uartkbd_parse.c`

**Interfaces:**
- Consumes: `temp_tspct` values as produced by Task 1.
- Produces: `float uartkbd_charger_temp_c(uint16_t tspct)` — returns °C, or ≤ −100 (sentinel −273.15f) when tspct is outside the divider's physical range. Task 4's app uses exactly this signature and treats `<= -100.0f` as "no reading".

- [ ] **Step 1: Write the failing test**

Append to `tests/test_uartkbd_parse.c` (before `main()`), and add `test_charger_temp_c();` to `main()`:

```c
static void test_charger_temp_c(void)
{
    /* Hand-computed via the doc's formula:
     * tspct 589 -> R_low 7509 -> R_ntc ~10017 -> ~25.0 C
     * tspct 675 -> R_low 10883 -> R_ntc ~17079 -> ~11.8 C */
    ASSERT_NEAR(uartkbd_charger_temp_c(589), 25.0, 0.5);
    ASSERT_NEAR(uartkbd_charger_temp_c(675), 11.8, 0.5);
    /* out of physical range (R_low >= the 30k parallel leg, or x=0) */
    CHECK(uartkbd_charger_temp_c(0) <= -100.0f);
    CHECK(uartkbd_charger_temp_c(900) <= -100.0f);
    CHECK(uartkbd_charger_temp_c(1000) <= -100.0f);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python tools/fw.py test`
Expected: build FAILS — `implicit declaration of function 'uartkbd_charger_temp_c'`.

- [ ] **Step 3: Implement**

In `bsp/input/uartkbd_parse.h`, after the `uartkbd_parse_charger` declaration:

```c
/* tspct (tenths of percent, from uartkbd_charger_t.temp_tspct) -> degrees C
 * via the NTC divider math in Wilikeyboard.md. Returns -273.15f when tspct
 * is outside the divider's physical range (check <= -100.0f). Uses logf —
 * the only function here that pulls in libm. */
float    uartkbd_charger_temp_c(uint16_t tspct);
```

In `bsp/input/uartkbd_parse.c`, add `#include <math.h>` under the existing
`#include <string.h>`, and at the end of the file:

```c
float uartkbd_charger_temp_c(uint16_t tspct)
{
    float x = (float)tspct / 1000.0f;
    if (x <= 0.0f || x >= 1.0f) return -273.15f;
    float r_low = 5240.0f * x / (1.0f - x);        /* divider's lower leg */
    float inv = 1.0f / r_low - 1.0f / 30000.0f;    /* peel off 30k parallel */
    if (inv <= 0.0f) return -273.15f;              /* NTC absent / open */
    float r_ntc = 1.0f / inv;
    float t_k = 1.0f / (1.0f / 298.15f + logf(r_ntc / 10000.0f) / 3435.0f);
    return t_k - 273.15f;
}
```

In `tests/CMakeLists.txt`, after the `target_include_directories(test_uartkbd_parse ...)` block, add:

```cmake
target_link_libraries(test_uartkbd_parse m)
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `python tools/fw.py test`
Expected: all tests pass.

- [ ] **Step 5: Commit**

```bash
git add bsp/input/uartkbd_parse.h bsp/input/uartkbd_parse.c tests/CMakeLists.txt tests/test_uartkbd_parse.c
git commit -m "feat: NTC tspct-to-Celsius helper for charger temperature"
```

---

### Task 3: Driver binding passthrough + docs

**Files:**
- Modify: `bsp/input/uartkbd.h`
- Modify: `bsp/input/uartkbd.c`
- Modify: `docs/drivers/keyboard.md`

**Interfaces:**
- Consumes: `uartkbd_parse_charger()` and `uartkbd_charger_t` from Task 1; the driver's private `s_parser`.
- Produces: `bool uartkbd_charger(uartkbd_charger_t *out)` — Task 4's app calls exactly this.

(No host test — the binding is a one-line passthrough over hardware state; the
compile of `hello_keyboard` is the check.)

- [ ] **Step 1: Add the passthrough**

In `bsp/input/uartkbd.h`, after the `uartkbd_flags` declaration:

```c
bool     uartkbd_charger(uartkbd_charger_t *out);
```

In `bsp/input/uartkbd.c`, with the other one-line accessors at the bottom:

```c
bool     uartkbd_charger(uartkbd_charger_t *out) { return uartkbd_parse_charger(&s_parser, out); }
```

- [ ] **Step 2: Verify it compiles for the target**

Run: `python tools/fw.py build hello_keyboard`
Expected: build succeeds (no app uses the new call yet; this proves the BSP still compiles).

- [ ] **Step 3: Document**

In `docs/drivers/keyboard.md`, insert this block between the
`apps/hello_keyboard is the worked example ...` paragraph and the
`**Display note:**` paragraph:

```markdown
**Charger telemetry:** the same status frame carries charger data in bytes
10-21. The parser captures the raw bytes on every checksum-valid frame
(readable from the first frame — no priming wait); scaling to engineering
units runs only when you ask:

    uartkbd_charger_t chg;
    if (uartkbd_charger(&chg)) {
        /* chg.vbus_mv / vsys_mv / vbatt_mv / current_ma / temp_tspct
           (tenths of %), chg.charge_status (UARTKBD_CHG_*), chg.vbus_status
           (UARTKBD_VBUS_*), chg.fault (UARTKBD_FAULT_*), chg.temp_rank
           (UARTKBD_RANK_*), chg.cc_tier (UARTKBD_CC_*),
           chg.vsys_regulation / thermal_regulation / vbus_attached,
           chg.cc1_mv / cc2_mv */
    }
    float c = uartkbd_charger_temp_c(chg.temp_tspct);  /* NTC math; links libm;
                                                          <= -100 = no reading */

Returns false until the first valid frame. Enum-coded fields carry the
frame's raw code verbatim, so undocumented codes pass through unclamped.
`apps/hello_charger` is the worked example (live full-frame view).
```

- [ ] **Step 4: Commit**

```bash
git add bsp/input/uartkbd.h bsp/input/uartkbd.c docs/drivers/keyboard.md
git commit -m "feat: expose charger snapshot through uartkbd binding"
```

---

### Task 4: hello_charger test app

**Files:**
- Create: `apps/hello_charger/CMakeLists.txt`
- Create: `apps/hello_charger/main.c`
- Modify: `CMakeLists.txt` (top-level — add subdirectory)

**Interfaces:**
- Consumes: `uartkbd_init/task/next_event/buttons/flags/frames/errors`, `uartkbd_charger()`, `uartkbd_charger_temp_c()`, `UARTKBD_FLAG_*`, plus BSP display (`st7796_*`, `font5x7`, `psram_init`, `board_*`) — all via `#include "fw2.h"` and the explicit includes below.
- Produces: the flashable `hello_charger` app (terminal deliverable; nothing consumes it).

- [ ] **Step 1: Create `apps/hello_charger/CMakeLists.txt`**

```cmake
add_executable(hello_charger main.c)
target_link_libraries(hello_charger freewili2_bsp)
pico_set_binary_type(hello_charger copy_to_ram)   # required: firmware runs from SRAM
pico_add_extra_outputs(hello_charger)             # .uf2 / .bin / .map
```

- [ ] **Step 2: Create `apps/hello_charger/main.c`**

```c
// hello_charger — live view of the FW2 keyboard-coprocessor status frame:
// charger telemetry (frame bytes 10-21) plus connection detects, button
// bitmap, and link stats. Read-only smoke screen for the uartkbd charger
// API; no touch, no fw2kb. Redraws only when a frame changes something.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fw2.h"
#include "platform/diag.h"
#include "pico/stdlib.h"
#include "display/font5x7.h"
#include "platform/psram.h"

/* RGB565, byte-swapped to wire order per st7796.h (same as hello_keyboard) */
#define COL_BLACK  0x0000
#define COL_WHITE  0xFFFF
#define COL_DIM    0xE739   /* 0x39E7 */
#define COL_HDR    0x06FF   /* #ffe331 */

#define TEXT_SCALE 2
#define LINE_H     (8 * TEXT_SCALE)

/* Off-screen framebuffer in PSRAM (AGENTS.md: large buffers live in PSRAM),
 * flushed whole-screen by DMA so the main loop never blocks on SPI. */
static uint16_t *const s_fb = (uint16_t *)PSRAM_BASE;

/* Everything shown on screen; memset before filling so struct padding is
 * zeroed and memcmp is a valid change detector. */
typedef struct {
    bool              valid;
    uartkbd_charger_t chg;
    uint16_t          buttons;
    uint8_t           flags;
    uint32_t          frames, errors;
} snap_t;

static snap_t s_last;
static bool   s_drawn;

static void fb_fill_rect(int x, int y, int w, int h, uint16_t color_be)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > ST7796_W) w = ST7796_W - x;
    if (y + h > ST7796_H) h = ST7796_H - y;
    for (int yy = y; yy < y + h; yy++) {
        uint16_t *row = s_fb + (size_t)yy * ST7796_W + x;
        for (int xx = 0; xx < w; xx++) row[xx] = color_be;
    }
}

static void fb_draw_text(int x, int y, int scale, uint16_t fg_be, uint16_t bg_be,
                         const char *s)
{
    if (scale < 1) scale = 1;
    if (scale > 4) scale = 4;
    const int w = 6 * scale, h = 8 * scale;
    for (; *s; s++, x += w) {
        if (x + w > ST7796_W || y + h > ST7796_H || x < 0 || y < 0) break;
        char c = *s;
        const uint8_t *cols = (c >= FONT5X7_FIRST && c <= FONT5X7_LAST)
                                  ? font5x7[c - FONT5X7_FIRST]
                                  : font5x7[0];
        for (int gy = 0; gy < h; gy++) {
            int grow = gy / scale;
            uint16_t *row = s_fb + (size_t)(y + gy) * ST7796_W + x;
            for (int gx = 0; gx < w; gx++) {
                int col = gx / scale;
                bool on = col < 5 && grow < 7 && ((cols[col] >> grow) & 1);
                row[gx] = on ? fg_be : bg_be;
            }
        }
    }
}

/* Enum code -> name, or "code N" for anything undocumented (the driver
 * passes raw codes through verbatim). NULL entries = gaps in the doc. */
static const char *enum_str(uint8_t v, const char *const *names, int n,
                            char buf[12])
{
    if (v < n && names[v]) return names[v];
    snprintf(buf, 12, "code %u", v);
    return buf;
}

static const char *const k_chg[]   = { "NotCharging", "PreCharge", "FastCharge",
                                       "Done" };
static const char *const k_vbus[]  = { "NoInput", "UsbHost", "Adapter", NULL,
                                       NULL, NULL, NULL, "OTG" };
static const char *const k_fault[] = { "Normal", "InputFault", "Thermal",
                                       "TimerExp" };
static const char *const k_rank[]  = { "Normal", NULL, "Warm", "Cool", NULL,
                                       "Cold", "Hot" };
static const char *const k_tier[]  = { "None", "500mA", "1.5A", "3A" };

static void draw_screen(const snap_t *s)
{
    char ln[48], b1[12], b2[12];
    int y = 4;
    fb_fill_rect(0, 0, ST7796_W, ST7796_H, COL_BLACK);
    fb_draw_text(4, y, TEXT_SCALE, COL_HDR, COL_BLACK, "FW2 CHARGER / STATUS");
    y += LINE_H + 8;

    if (!s->valid) {
        fb_draw_text(4, y, TEXT_SCALE, COL_DIM, COL_BLACK,
                     "waiting for first frame...");
        y += LINE_H + 4;
    } else {
        const uartkbd_charger_t *c = &s->chg;
        snprintf(ln, sizeof ln, "VBUS  %5u mV   VSYS %4u mV",
                 c->vbus_mv, c->vsys_mv);
        fb_draw_text(4, y, TEXT_SCALE, COL_WHITE, COL_BLACK, ln);
        y += LINE_H + 4;
        snprintf(ln, sizeof ln, "VBATT %5u mV   CURR %4u mA",
                 c->vbatt_mv, c->current_ma);
        fb_draw_text(4, y, TEXT_SCALE, COL_WHITE, COL_BLACK, ln);
        y += LINE_H + 4;

        float tc = uartkbd_charger_temp_c(c->temp_tspct);
        if (tc > -100.0f) {
            int t10 = (int)(tc * 10.0f + (tc >= 0.0f ? 0.5f : -0.5f));
            snprintf(ln, sizeof ln, "TEMP  %u.%u%%  %d.%d C",
                     c->temp_tspct / 10, c->temp_tspct % 10,
                     t10 / 10, abs(t10 % 10));
        } else {
            snprintf(ln, sizeof ln, "TEMP  %u.%u%%  --- C",
                     c->temp_tspct / 10, c->temp_tspct % 10);
        }
        fb_draw_text(4, y, TEXT_SCALE, COL_WHITE, COL_BLACK, ln);
        y += LINE_H + 4;

        snprintf(ln, sizeof ln, "CHG   %-11s FAULT %s",
                 enum_str(s->chg.charge_status, k_chg, 4, b1),
                 enum_str(s->chg.fault, k_fault, 4, b2));
        fb_draw_text(4, y, TEXT_SCALE, COL_WHITE, COL_BLACK, ln);
        y += LINE_H + 4;
        snprintf(ln, sizeof ln, "VBUS  %-11s RANK  %s",
                 enum_str(s->chg.vbus_status, k_vbus, 8, b1),
                 enum_str(s->chg.temp_rank, k_rank, 7, b2));
        fb_draw_text(4, y, TEXT_SCALE, COL_WHITE, COL_BLACK, ln);
        y += LINE_H + 4;
        snprintf(ln, sizeof ln, "CC    %-5s %s%s%s",
                 enum_str(c->cc_tier, k_tier, 4, b1),
                 c->vbus_attached ? "ATT " : "",
                 c->vsys_regulation ? "VREG " : "",
                 c->thermal_regulation ? "TREG" : "");
        fb_draw_text(4, y, TEXT_SCALE, COL_WHITE, COL_BLACK, ln);
        y += LINE_H + 4;
        snprintf(ln, sizeof ln, "CC1   %4u mV    CC2  %4u mV",
                 c->cc1_mv, c->cc2_mv);
        fb_draw_text(4, y, TEXT_SCALE, COL_WHITE, COL_BLACK, ln);
        y += LINE_H + 8;
    }

    snprintf(ln, sizeof ln, "BTNS %04X  DET %s%s%s",
             s->buttons,
             (s->flags & UARTKBD_FLAG_AUDIO)   ? "AUD " : "",
             (s->flags & UARTKBD_FLAG_HOTPLUG) ? "HPD " : "",
             (s->flags & UARTKBD_FLAG_USB)     ? "USB" : "");
    fb_draw_text(4, y, TEXT_SCALE, COL_DIM, COL_BLACK, ln);
    y += LINE_H + 4;
    snprintf(ln, sizeof ln, "FRAMES %lu  ERRORS %lu",
             (unsigned long)s->frames, (unsigned long)s->errors);
    fb_draw_text(4, y, TEXT_SCALE, COL_DIM, COL_BLACK, ln);
}

int main(void)
{
    board_init();
    size_t psram_bytes = psram_init();
    if (psram_bytes < (size_t)ST7796_W * ST7796_H * 2) {
        DIAG("hello_charger: PSRAM absent/too small (%u bytes) - halting\n",
             (unsigned)psram_bytes);
        for (;;) tight_loop_contents();
    }
    st7796_init();
    board_backlight_set(1);
    uartkbd_init();
    DIAG("hello_charger up\n");

    uint64_t next_log = 0;
    while (true) {
        uartkbd_task();
        uartkbd_event_t ev;
        while (uartkbd_next_event(&ev))    /* drain so the ring never wraps */
            DIAG("uartkbd btn %d %s\n", (int)ev.btn, ev.pressed ? "down" : "up");

        snap_t s;
        memset(&s, 0, sizeof s);           /* zero padding for memcmp */
        s.valid   = uartkbd_charger(&s.chg);
        s.buttons = uartkbd_buttons();
        s.flags   = uartkbd_flags();
        s.frames  = uartkbd_frames();
        s.errors  = uartkbd_errors();

        /* If a change lands mid-flush we skip this pass; the snapshot still
         * differs from s_last next loop, so the redraw coalesces. */
        if ((!s_drawn || memcmp(&s, &s_last, sizeof s) != 0)
            && !st7796_flush_busy()) {
            s_last = s;
            s_drawn = true;
            draw_screen(&s);
            st7796_flush_async(0, 0, ST7796_W - 1, ST7796_H - 1, s_fb, NULL);
        }

        uint64_t now = time_us_64();
        if (now >= next_log) {
            DIAG("uartkbd frames=%u errors=%u flags=%x btns=%x\n",
                 (unsigned)s.frames, (unsigned)s.errors,
                 (unsigned)s.flags, (unsigned)s.buttons);
            next_log = now + 1000000;
        }
        sleep_ms(2);
    }
}
```

- [ ] **Step 3: Register the app**

In the top-level `CMakeLists.txt`, after `add_subdirectory(apps/hello_keyboard)`, add:

```cmake
add_subdirectory(apps/hello_charger)
```

- [ ] **Step 4: Build for the target**

Run: `python tools/fw.py build hello_charger`
Expected: build succeeds, producing `build/apps/hello_charger/hello_charger.uf2` (and `.elf`). Warnings are enabled repo-wide — expect none from the new files.

- [ ] **Step 5: Run the host tests once more (regression)**

Run: `python tools/fw.py test`
Expected: all tests pass.

- [ ] **Step 6: Commit**

```bash
git add apps/hello_charger/CMakeLists.txt apps/hello_charger/main.c CMakeLists.txt
git commit -m "feat: hello_charger app - live charger/status frame viewer"
```

---

## Out of scope / notes for the executor

- No charger-change events (snapshot polling only; frames arrive ~2 Hz).
- No coprocessor TX; bytes 6–9 stay unparsed.
- On-hardware smoke (flash `hello_charger`, watch values move while plugging
  USB/charger) is **owner-run** — do not attempt to flash hardware.
- `DIAG()` cannot print floats (AGENTS.md invariant 3) — that's why the app
  formats °C as integer tenths.
