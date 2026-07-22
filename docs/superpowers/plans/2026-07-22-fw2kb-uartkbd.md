# fw2kb + UART Keyboard Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Harvest the fw2kb chord keyboard into the wilibsp BSP, add an RX-only driver for the FW2 UART keyboard (23-byte button frames on GPIO39), and ship `apps/hello_keyboard` — type on the ST7796 with real buttons + touch space/backspace.

**Architecture:** Three units. `bsp/keyboard/fw2kb*` is the frozen chord engine copied verbatim from the wilikeyboard repo. `bsp/input/uartkbd_parse.{c,h}` is a pure byte-at-a-time frame parser with edge detection (host-tested); `bsp/input/uartkbd.{c,h}` binds it to UART1 @ 62500 8N1 on GPIO38/39 (UART-AUX pin function), polled from the main loop like every other wilibsp driver. `apps/hello_keyboard` renders 5 colored soft buttons + a text area and routes buttons/touch into fw2kb.

**Tech Stack:** C11, Pico SDK (RP2350B), CMake, host CTest via `python tools/fw.py test`, SEGGER RTT via `DIAG()`.

**Spec:** `docs/superpowers/specs/2026-07-22-fw2kb-uartkbd-design.md`
**Protocol authority:** `C:\~prj\Dropbox\FreeWilli\vibe\Wilikeyboard.md` (the firmware repo's rpPICComm is stale — do not consult it for message bytes).
**Harvest source:** `C:\~prj\Dropbox\vibeProjects\wilikeyboard\` (fw2kb library + its tests, reviewed and frozen — copy verbatim, never edit the copies).

## Global Constraints

- fw2kb sources are copied VERBATIM from `wilikeyboard/src/` — zero edits. If something seems wrong with them, report it; do not patch the copies.
- UART keyboard frame (Wilikeyboard.md): 23 bytes; byte0=0xBD, byte1=0x1D; byte2 b0..b5 = GREY,YELLOW,GREEN,BLUE,RED,NAV_CENTER; byte3 b0..b5 = NAV_DOWN,HOTPLUG_DET,AUDIO_DET,NAV_RIGHT,NAV_UP,NAV_LEFT; byte4 b2=USB_DET_PIC, b7=HOME_PB; byte5 b0..b2 = OK_PB,CANCEL_PB,PAGE_PB; bytes6-21 reserved; byte22 = additive 8-bit checksum of bytes 0-21 (`sum & 0xFF`). Button bits 1 = pressed. Reserved bits must be masked.
- UART1, GPIO38 TX / GPIO39 RX, **62500 baud 8N1**, pin function `GPIO_FUNC_UART_AUX` (plain UART function on these pins is CTS/RTS). RX-only — never transmit.
- Polled, no IRQs (BSP convention). Diagnostics via `DIAG()` only — never printf/stdio (AGENTS.md invariant 3).
- Never pass `-DPICO_BOARD` on any cmake command line (AGENTS.md invariants 1/8). Build only via `python tools/fw.py ...`.
- Event rings: 8 entries, oldest dropped (both fw2kb and uartkbd_parse).
- Demo layout: 480x320; button bar y 272-319, five 96x48 buttons colored gray/yellow/green/blue/red; text area y 0-271; fw2kb touch threshold 136.
- Conventional Commits.
- Commands (repo root `C:\~prj\Dropbox\vibeProjects\wilibsp`):
  - Host tests: `python tools/fw.py test`
  - Target build: `python tools/fw.py build [app]` (default hello_display)

---

### Task 1: Harvest fw2kb into the BSP

**Files:**
- Create: `bsp/keyboard/fw2kb.h`, `bsp/keyboard/fw2kb.c`, `bsp/keyboard/fw2kb_hidmap.c` (verbatim copies)
- Create: `tests/test_fw2kb.c`, `tests/test_fw2kb_hid.c` (verbatim copies)
- Modify: `tests/test_util.h` (append compat shim), `tests/CMakeLists.txt`, `bsp/CMakeLists.txt`, `bsp/fw2.h`

**Interfaces:**
- Consumes: nothing.
- Produces: the fw2kb API for Tasks 4 (`fw2kb_t`, `fw2kb_init`, `fw2kb_set_touch_threshold`, `fw2kb_press(kb, FW2KB_BTN_GRAY..RED | FW2KB_BTN_AI)`, `fw2kb_touch(kb,x,y)`, `fw2kb_next_event(kb, fw2kb_event*)` with `FW2KB_KEY_CHAR/BACKSPACE/ENTER/TAB/...`, `fw2kb_get_labels(kb, const char *labels[5])`). All in `keyboard/fw2kb.h`, exposed via `fw2.h`.

- [ ] **Step 1: Copy the five files verbatim**

```bash
cd "C:/~prj/Dropbox/vibeProjects/wilibsp"
mkdir -p bsp/keyboard
cp "C:/~prj/Dropbox/vibeProjects/wilikeyboard/src/fw2kb.h"        bsp/keyboard/
cp "C:/~prj/Dropbox/vibeProjects/wilikeyboard/src/fw2kb.c"        bsp/keyboard/
cp "C:/~prj/Dropbox/vibeProjects/wilikeyboard/src/fw2kb_hidmap.c" bsp/keyboard/
cp "C:/~prj/Dropbox/vibeProjects/wilikeyboard/tests/test_fw2kb.c"     tests/
cp "C:/~prj/Dropbox/vibeProjects/wilikeyboard/tests/test_fw2kb_hid.c" tests/
```

Do NOT copy wilikeyboard's `tests/test_util.h` — wilibsp already has a
different `tests/test_util.h` (ASSERT_EQ style) with the same include guard.

- [ ] **Step 2: Add the compat shim to `tests/test_util.h`**

The copied fw2kb tests use `CHECK(...)`, `g_test_failures` (directly), and
`test_failures()`. Append this block to `tests/test_util.h` immediately
before the final `#endif`:

```c
/* --- compat for tests harvested from the wilikeyboard repo --- */
#define g_test_failures g_failures
#define CHECK(cond) do { \
    if (!(cond)) { \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        g_failures++; \
    } \
} while (0)
static inline int test_failures(void) { return g_failures; }
```

- [ ] **Step 3: Register the two test executables**

Append to `tests/CMakeLists.txt`:

```cmake
# --- bsp/keyboard fw2kb chord engine (harvested from ../wilikeyboard) ---
set(BSPKBD ${CMAKE_CURRENT_SOURCE_DIR}/../bsp/keyboard)
add_executable(test_fw2kb test_fw2kb.c ${BSPKBD}/fw2kb.c)
target_include_directories(test_fw2kb PRIVATE ${BSPKBD} ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME fw2kb COMMAND test_fw2kb)

add_executable(test_fw2kb_hid test_fw2kb_hid.c ${BSPKBD}/fw2kb.c ${BSPKBD}/fw2kb_hidmap.c)
target_include_directories(test_fw2kb_hid PRIVATE ${BSPKBD} ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME fw2kb_hid COMMAND test_fw2kb_hid)
```

(The copied tests `#include "fw2kb.h"` — the `${BSPKBD}` include dir
resolves that without editing the copies.)

- [ ] **Step 4: Run host tests**

Run: `python tools/fw.py test`
Expected: all existing tests still pass, plus `fw2kb` and `fw2kb_hid` pass
(look for `100% tests passed`).

- [ ] **Step 5: Wire into the BSP library and umbrella header**

In `bsp/CMakeLists.txt`, add to the `add_library(freewili2_bsp STATIC ...)`
source list, after the `gfx/palette.c` line:

```cmake
    keyboard/fw2kb.c
    keyboard/fw2kb_hidmap.c
```

In `bsp/fw2.h`, add after the `usbhost/usb_store.h` include line:

```c
#include "keyboard/fw2kb.h"     // (harvested: two-press chord keyboard engine, ../wilikeyboard)
```

- [ ] **Step 6: Verify the target build still compiles**

Run: `python tools/fw.py build`
Expected: hello_display builds clean (this compiles the BSP library
including the two new keyboard sources).

- [ ] **Step 7: Commit**

```bash
git add bsp/keyboard tests/test_fw2kb.c tests/test_fw2kb_hid.c tests/test_util.h tests/CMakeLists.txt bsp/CMakeLists.txt bsp/fw2.h
git commit -m "feat: harvest fw2kb chord keyboard engine from wilikeyboard"
```

---

### Task 2: uartkbd_parse — pure frame parser with edge detection

**Files:**
- Create: `bsp/input/uartkbd_parse.h`, `bsp/input/uartkbd_parse.c`
- Modify: `bsp/CMakeLists.txt`, `tests/CMakeLists.txt`
- Test: `tests/test_uartkbd_parse.c`

**Interfaces:**
- Consumes: nothing (pure C, no hardware includes).
- Produces (Task 3 wraps these; Task 4 sees them through uartkbd.h):
  - `uartkbd_btn_t` enum: `UARTKBD_BTN_GREY=0, YELLOW, GREEN, BLUE, RED, NAV_CENTER, NAV_UP, NAV_DOWN, NAV_LEFT, NAV_RIGHT, HOME, OK, CANCEL, PAGE, UARTKBD_BTN_COUNT`
  - `uartkbd_event_t { uartkbd_btn_t btn; bool pressed; }`
  - `uartkbd_parser_t` + `uartkbd_parse_init/byte/next_event/buttons/flags/frames/errors`
  - Flags: `UARTKBD_FLAG_AUDIO 0x01`, `UARTKBD_FLAG_HOTPLUG 0x02`, `UARTKBD_FLAG_USB 0x04`

- [ ] **Step 1: Write the header**

`bsp/input/uartkbd_parse.h`:

```c
/*
 * uartkbd_parse — pure frame parser for the FW2 UART keyboard.
 * Protocol: C:\~prj\Dropbox\FreeWilli\vibe\Wilikeyboard.md (23-byte frame,
 * sync 0xBD 0x1D, button bitmaps in bytes 2-5, additive 8-bit checksum of
 * bytes 0-21 in byte 22). No hardware includes — host-testable.
 */
#ifndef UARTKBD_PARSE_H
#define UARTKBD_PARSE_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    UARTKBD_BTN_GREY = 0,
    UARTKBD_BTN_YELLOW,
    UARTKBD_BTN_GREEN,
    UARTKBD_BTN_BLUE,
    UARTKBD_BTN_RED,
    UARTKBD_BTN_NAV_CENTER,
    UARTKBD_BTN_NAV_UP,
    UARTKBD_BTN_NAV_DOWN,
    UARTKBD_BTN_NAV_LEFT,
    UARTKBD_BTN_NAV_RIGHT,
    UARTKBD_BTN_HOME,
    UARTKBD_BTN_OK,
    UARTKBD_BTN_CANCEL,
    UARTKBD_BTN_PAGE,
    UARTKBD_BTN_COUNT
} uartkbd_btn_t;

typedef struct {
    uartkbd_btn_t btn;
    bool          pressed;   /* true = press edge, false = release edge */
} uartkbd_event_t;

/* Level flags (connection detects) — never emitted as events */
#define UARTKBD_FLAG_AUDIO   0x01u
#define UARTKBD_FLAG_HOTPLUG 0x02u
#define UARTKBD_FLAG_USB     0x04u

#define UARTKBD_FRAME_LEN  23
#define UARTKBD_EVENT_RING 8

typedef struct {
    uint8_t  state;                        /* 0 hunt 0xBD, 1 expect 0x1D, 2 collect */
    uint8_t  count;
    uint8_t  frame[UARTKBD_FRAME_LEN];
    uint16_t buttons;                      /* bit N = uartkbd_btn_t N, 1 = down */
    uint8_t  flags;                        /* UARTKBD_FLAG_* */
    uartkbd_event_t ring[UARTKBD_EVENT_RING];
    uint8_t  ring_head, ring_count;
    uint32_t frames;                       /* checksum-valid frames */
    uint32_t errors;                       /* sync misses + checksum failures */
} uartkbd_parser_t;

void     uartkbd_parse_init(uartkbd_parser_t *p);
void     uartkbd_parse_byte(uartkbd_parser_t *p, uint8_t b);
bool     uartkbd_parse_next_event(uartkbd_parser_t *p, uartkbd_event_t *ev);
uint16_t uartkbd_parse_buttons(const uartkbd_parser_t *p);
uint8_t  uartkbd_parse_flags(const uartkbd_parser_t *p);
uint32_t uartkbd_parse_frames(const uartkbd_parser_t *p);
uint32_t uartkbd_parse_errors(const uartkbd_parser_t *p);

#endif /* UARTKBD_PARSE_H */
```

- [ ] **Step 2: Write the failing test**

`tests/test_uartkbd_parse.c`:

```c
#include <string.h>
#include "uartkbd_parse.h"
#include "test_util.h"

/* Build a valid 23-byte frame with the given payload bytes 2-5. */
static void mk_frame(uint8_t f[UARTKBD_FRAME_LEN],
                     uint8_t b2, uint8_t b3, uint8_t b4, uint8_t b5)
{
    memset(f, 0, UARTKBD_FRAME_LEN);
    f[0] = 0xBD; f[1] = 0x1D;
    f[2] = b2; f[3] = b3; f[4] = b4; f[5] = b5;
    uint8_t sum = 0;
    for (int i = 0; i < UARTKBD_FRAME_LEN - 1; i++) sum = (uint8_t)(sum + f[i]);
    f[UARTKBD_FRAME_LEN - 1] = sum;
}

static void feed(uartkbd_parser_t *p, const uint8_t *d, int n)
{
    for (int i = 0; i < n; i++) uartkbd_parse_byte(p, d[i]);
}

static void test_valid_frame_latches_buttons(void)
{
    uartkbd_parser_t p;
    uartkbd_parse_init(&p);
    uint8_t f[UARTKBD_FRAME_LEN];
    mk_frame(f, 0x11, 0, 0, 0);            /* GREY (b0) + RED (b4) */
    feed(&p, f, UARTKBD_FRAME_LEN);

    CHECK(uartkbd_parse_frames(&p) == 1);
    CHECK(uartkbd_parse_errors(&p) == 0);
    CHECK(uartkbd_parse_buttons(&p) ==
          ((1u << UARTKBD_BTN_GREY) | (1u << UARTKBD_BTN_RED)));

    uartkbd_event_t ev;
    CHECK(uartkbd_parse_next_event(&p, &ev));
    CHECK(ev.btn == UARTKBD_BTN_GREY && ev.pressed);
    CHECK(uartkbd_parse_next_event(&p, &ev));
    CHECK(ev.btn == UARTKBD_BTN_RED && ev.pressed);
    CHECK(!uartkbd_parse_next_event(&p, &ev));
}

static void test_press_then_release_edges(void)
{
    uartkbd_parser_t p;
    uartkbd_parse_init(&p);
    uint8_t f[UARTKBD_FRAME_LEN];
    uartkbd_event_t ev;

    mk_frame(f, 0x01, 0, 0, 0);            /* GREY down */
    feed(&p, f, UARTKBD_FRAME_LEN);
    CHECK(uartkbd_parse_next_event(&p, &ev));
    CHECK(ev.btn == UARTKBD_BTN_GREY && ev.pressed);

    mk_frame(f, 0x01, 0, 0, 0);            /* GREY still down: no new event */
    feed(&p, f, UARTKBD_FRAME_LEN);
    CHECK(!uartkbd_parse_next_event(&p, &ev));

    mk_frame(f, 0x00, 0, 0, 0);            /* GREY up */
    feed(&p, f, UARTKBD_FRAME_LEN);
    CHECK(uartkbd_parse_next_event(&p, &ev));
    CHECK(ev.btn == UARTKBD_BTN_GREY && !ev.pressed);
    CHECK(uartkbd_parse_buttons(&p) == 0);
}

static void test_full_button_mapping(void)
{
    /* one frame with every button down: byte2 b0-b5, byte3 b0/b3/b4/b5,
       byte4 b7, byte5 b0-b2 */
    uartkbd_parser_t p;
    uartkbd_parse_init(&p);
    uint8_t f[UARTKBD_FRAME_LEN];
    mk_frame(f, 0x3F, 0x39, 0x80, 0x07);
    feed(&p, f, UARTKBD_FRAME_LEN);
    CHECK(uartkbd_parse_buttons(&p) == 0x3FFF);   /* all 14 bits */
    CHECK(uartkbd_parse_flags(&p) == 0);
}

static void test_checksum_reject(void)
{
    uartkbd_parser_t p;
    uartkbd_parse_init(&p);
    uint8_t f[UARTKBD_FRAME_LEN];
    mk_frame(f, 0x01, 0, 0, 0);
    f[UARTKBD_FRAME_LEN - 1] ^= 0xFF;      /* corrupt checksum */
    feed(&p, f, UARTKBD_FRAME_LEN);

    uartkbd_event_t ev;
    CHECK(uartkbd_parse_frames(&p) == 0);
    CHECK(uartkbd_parse_errors(&p) == 1);
    CHECK(uartkbd_parse_buttons(&p) == 0);
    CHECK(!uartkbd_parse_next_event(&p, &ev));

    mk_frame(f, 0x01, 0, 0, 0);            /* recovery: next good frame parses */
    feed(&p, f, UARTKBD_FRAME_LEN);
    CHECK(uartkbd_parse_frames(&p) == 1);
    CHECK(uartkbd_parse_next_event(&p, &ev));
    CHECK(ev.btn == UARTKBD_BTN_GREY && ev.pressed);
}

static void test_resync_through_garbage(void)
{
    uartkbd_parser_t p;
    uartkbd_parse_init(&p);
    const uint8_t junk[] = { 0x00, 0xFF, 0xBD, 0x77, 0x1D, 0xBD };
    feed(&p, junk, sizeof junk);           /* includes a false 0xBD start */

    uint8_t f[UARTKBD_FRAME_LEN];
    mk_frame(f, 0x02, 0, 0, 0);            /* YELLOW */
    /* NOTE: last junk byte was 0xBD, so the parser may be in expect-0x1D
       state; a real frame starts 0xBD 0x1D — feeding it works either way
       because its first byte re-arms the hunt. */
    feed(&p, f, UARTKBD_FRAME_LEN);
    /* Depending on junk alignment the first frame may be consumed by a
       false sync; feed a second identical frame to prove steady-state. */
    mk_frame(f, 0x02, 0, 0, 0);
    feed(&p, f, UARTKBD_FRAME_LEN);
    CHECK(uartkbd_parse_frames(&p) >= 1);
    CHECK(uartkbd_parse_buttons(&p) == (1u << UARTKBD_BTN_YELLOW));
}

static void test_reserved_bits_masked(void)
{
    uartkbd_parser_t p;
    uartkbd_parse_init(&p);
    uint8_t f[UARTKBD_FRAME_LEN];
    /* only reserved bits set: byte2 b6-7, byte3 b6-7, byte4 all but b2/b7,
       byte5 b3-7 */
    mk_frame(f, 0xC0, 0xC0, 0x79, 0xF8);
    feed(&p, f, UARTKBD_FRAME_LEN);
    uartkbd_event_t ev;
    CHECK(uartkbd_parse_frames(&p) == 1);
    CHECK(uartkbd_parse_buttons(&p) == 0);
    CHECK(uartkbd_parse_flags(&p) == 0);
    CHECK(!uartkbd_parse_next_event(&p, &ev));
}

static void test_flags_update_without_events(void)
{
    uartkbd_parser_t p;
    uartkbd_parse_init(&p);
    uint8_t f[UARTKBD_FRAME_LEN];
    mk_frame(f, 0, 0x06, 0x04, 0);   /* AUDIO_DET + HOTPLUG_DET + USB_DET */
    feed(&p, f, UARTKBD_FRAME_LEN);
    uartkbd_event_t ev;
    CHECK(uartkbd_parse_flags(&p) ==
          (UARTKBD_FLAG_AUDIO | UARTKBD_FLAG_HOTPLUG | UARTKBD_FLAG_USB));
    CHECK(uartkbd_parse_buttons(&p) == 0);
    CHECK(!uartkbd_parse_next_event(&p, &ev));
}

static void test_ring_overflow_drops_oldest(void)
{
    uartkbd_parser_t p;
    uartkbd_parse_init(&p);
    uint8_t f[UARTKBD_FRAME_LEN];
    mk_frame(f, 0x3F, 0x39, 0x80, 0x07);   /* 14 press edges -> 8-slot ring */
    feed(&p, f, UARTKBD_FRAME_LEN);

    uartkbd_event_t ev;
    int n = 0;
    uartkbd_btn_t first = UARTKBD_BTN_COUNT;
    while (uartkbd_parse_next_event(&p, &ev)) {
        if (n == 0) first = ev.btn;
        n++;
    }
    CHECK(n == 8);                          /* 6 oldest dropped */
    CHECK(first == UARTKBD_BTN_NAV_UP);     /* bits 0-5 were dropped */
}

static void test_split_delivery(void)
{
    uartkbd_parser_t p;
    uartkbd_parse_init(&p);
    uint8_t f[UARTKBD_FRAME_LEN];
    mk_frame(f, 0x04, 0, 0, 0);            /* GREEN */
    feed(&p, f, 7);                        /* arrives in two chunks */
    feed(&p, f + 7, UARTKBD_FRAME_LEN - 7);
    CHECK(uartkbd_parse_frames(&p) == 1);
    CHECK(uartkbd_parse_buttons(&p) == (1u << UARTKBD_BTN_GREEN));
}

int main(void)
{
    test_valid_frame_latches_buttons();
    test_press_then_release_edges();
    test_full_button_mapping();
    test_checksum_reject();
    test_resync_through_garbage();
    test_reserved_bits_masked();
    test_flags_update_without_events();
    test_ring_overflow_drops_oldest();
    test_split_delivery();
    if (g_failures == 0) printf("test_uartkbd_parse: all passed\n");
    TEST_RETURN();
}
```

Add to `tests/CMakeLists.txt`:

```cmake
add_executable(test_uartkbd_parse
    test_uartkbd_parse.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../bsp/input/uartkbd_parse.c)
target_include_directories(test_uartkbd_parse PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../bsp/input
    ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME uartkbd_parse COMMAND test_uartkbd_parse)
```

- [ ] **Step 3: Run to verify it fails**

Run: `python tools/fw.py test`
Expected: FAIL — `uartkbd_parse.c` does not exist yet (configure or compile error).

- [ ] **Step 4: Implement the parser**

`bsp/input/uartkbd_parse.c`:

```c
#include "uartkbd_parse.h"
#include <string.h>

enum { ST_HUNT = 0, ST_SYNC2 = 1, ST_COLLECT = 2 };

static void push_event(uartkbd_parser_t *p, uartkbd_btn_t btn, bool pressed)
{
    if (p->ring_count == UARTKBD_EVENT_RING) {          /* drop oldest */
        p->ring_head = (uint8_t)((p->ring_head + 1) % UARTKBD_EVENT_RING);
        p->ring_count--;
    }
    uint8_t tail = (uint8_t)((p->ring_head + p->ring_count) % UARTKBD_EVENT_RING);
    p->ring[tail].btn = btn;
    p->ring[tail].pressed = pressed;
    p->ring_count++;
}

/* Bytes 2-5 -> 14-bit button state (bit = uartkbd_btn_t). Reserved bits
 * are never read. Mapping per Wilikeyboard.md. */
static uint16_t decode_buttons(const uint8_t *f)
{
    uint16_t b = 0;
    if (f[2] & 0x01) b |= 1u << UARTKBD_BTN_GREY;
    if (f[2] & 0x02) b |= 1u << UARTKBD_BTN_YELLOW;
    if (f[2] & 0x04) b |= 1u << UARTKBD_BTN_GREEN;
    if (f[2] & 0x08) b |= 1u << UARTKBD_BTN_BLUE;
    if (f[2] & 0x10) b |= 1u << UARTKBD_BTN_RED;
    if (f[2] & 0x20) b |= 1u << UARTKBD_BTN_NAV_CENTER;
    if (f[3] & 0x01) b |= 1u << UARTKBD_BTN_NAV_DOWN;
    if (f[3] & 0x08) b |= 1u << UARTKBD_BTN_NAV_RIGHT;
    if (f[3] & 0x10) b |= 1u << UARTKBD_BTN_NAV_UP;
    if (f[3] & 0x20) b |= 1u << UARTKBD_BTN_NAV_LEFT;
    if (f[4] & 0x80) b |= 1u << UARTKBD_BTN_HOME;
    if (f[5] & 0x01) b |= 1u << UARTKBD_BTN_OK;
    if (f[5] & 0x02) b |= 1u << UARTKBD_BTN_CANCEL;
    if (f[5] & 0x04) b |= 1u << UARTKBD_BTN_PAGE;
    return b;
}

static uint8_t decode_flags(const uint8_t *f)
{
    uint8_t fl = 0;
    if (f[3] & 0x04) fl |= UARTKBD_FLAG_AUDIO;
    if (f[3] & 0x02) fl |= UARTKBD_FLAG_HOTPLUG;
    if (f[4] & 0x04) fl |= UARTKBD_FLAG_USB;
    return fl;
}

static void accept_frame(uartkbd_parser_t *p)
{
    uint8_t sum = 0;
    for (int i = 0; i < UARTKBD_FRAME_LEN - 1; i++)
        sum = (uint8_t)(sum + p->frame[i]);
    if (sum != p->frame[UARTKBD_FRAME_LEN - 1]) {
        p->errors++;
        return;
    }
    p->frames++;
    p->flags = decode_flags(p->frame);
    uint16_t nb = decode_buttons(p->frame);
    uint16_t changed = (uint16_t)(nb ^ p->buttons);
    for (int i = 0; i < UARTKBD_BTN_COUNT; i++)
        if (changed & (1u << i))
            push_event(p, (uartkbd_btn_t)i, (nb >> i) & 1u);
    p->buttons = nb;
}

void uartkbd_parse_init(uartkbd_parser_t *p)
{
    memset(p, 0, sizeof *p);
}

void uartkbd_parse_byte(uartkbd_parser_t *p, uint8_t b)
{
    switch (p->state) {
    case ST_HUNT:
        if (b == 0xBD) { p->frame[0] = b; p->count = 1; p->state = ST_SYNC2; }
        break;
    case ST_SYNC2:
        if (b == 0x1D) {
            p->frame[1] = b; p->count = 2; p->state = ST_COLLECT;
        } else {
            p->errors++;
            /* the miss byte may itself be a new sync start */
            if (b == 0xBD) { p->frame[0] = b; p->count = 1; }
            else p->state = ST_HUNT;
        }
        break;
    case ST_COLLECT:
        p->frame[p->count++] = b;
        if (p->count == UARTKBD_FRAME_LEN) {
            p->state = ST_HUNT;
            accept_frame(p);
        }
        break;
    }
}

bool uartkbd_parse_next_event(uartkbd_parser_t *p, uartkbd_event_t *ev)
{
    if (p->ring_count == 0) return false;
    *ev = p->ring[p->ring_head];
    p->ring_head = (uint8_t)((p->ring_head + 1) % UARTKBD_EVENT_RING);
    p->ring_count--;
    return true;
}

uint16_t uartkbd_parse_buttons(const uartkbd_parser_t *p) { return p->buttons; }
uint8_t  uartkbd_parse_flags(const uartkbd_parser_t *p)   { return p->flags; }
uint32_t uartkbd_parse_frames(const uartkbd_parser_t *p)  { return p->frames; }
uint32_t uartkbd_parse_errors(const uartkbd_parser_t *p)  { return p->errors; }
```

Add `input/uartkbd_parse.c` to the `add_library(freewili2_bsp STATIC ...)`
list in `bsp/CMakeLists.txt` (after `keyboard/fw2kb_hidmap.c`).

- [ ] **Step 5: Run tests to verify they pass**

Run: `python tools/fw.py test`
Expected: `uartkbd_parse` passes along with everything else (`100% tests passed`).

- [ ] **Step 6: Commit**

```bash
git add bsp/input/uartkbd_parse.h bsp/input/uartkbd_parse.c tests/test_uartkbd_parse.c tests/CMakeLists.txt bsp/CMakeLists.txt
git commit -m "feat: pure UART keyboard frame parser with edge detection"
```

---

### Task 3: uartkbd — UART1 hardware binding

**Files:**
- Create: `bsp/input/uartkbd.h`, `bsp/input/uartkbd.c`
- Modify: `bsp/CMakeLists.txt`, `bsp/fw2.h`

**Interfaces:**
- Consumes: `uartkbd_parse` (Task 2).
- Produces (Task 4 uses these):
  - `void uartkbd_init(void)` — UART1 @ 62500 8N1, GPIO38/39 UART-AUX
  - `void uartkbd_task(void)` — drain RX FIFO; call every main-loop iteration
  - `bool uartkbd_next_event(uartkbd_event_t *ev)`
  - `uint16_t uartkbd_buttons(void)`, `uint8_t uartkbd_flags(void)`,
    `uint32_t uartkbd_frames(void)`, `uint32_t uartkbd_errors(void)`

- [ ] **Step 1: Write the header and binding**

`bsp/input/uartkbd.h`:

```c
/*
 * uartkbd — FW2 UART keyboard binding: UART1 @ 62500 8N1 on GPIO38 (TX,
 * claimed but never driven) / GPIO39 (RX). Frames arrive unsolicited;
 * RX-only. Polled — call uartkbd_task() every main-loop iteration.
 */
#ifndef UARTKBD_H
#define UARTKBD_H

#include "uartkbd_parse.h"

void     uartkbd_init(void);
void     uartkbd_task(void);
bool     uartkbd_next_event(uartkbd_event_t *ev);
uint16_t uartkbd_buttons(void);
uint8_t  uartkbd_flags(void);
uint32_t uartkbd_frames(void);
uint32_t uartkbd_errors(void);

#endif /* UARTKBD_H */
```

`bsp/input/uartkbd.c`:

```c
#include "uartkbd.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"

/* On GPIO38/39 the plain UART function is UART1 CTS/RTS; the UART-AUX
 * function routes UART1 TX/RX here instead (RP2350 datasheet GPIO muxing). */
#define UARTKBD_UART   uart1
#define UARTKBD_TX_PIN 38
#define UARTKBD_RX_PIN 39
#define UARTKBD_BAUD   62500

static uartkbd_parser_t s_parser;

void uartkbd_init(void)
{
    uartkbd_parse_init(&s_parser);
    uart_init(UARTKBD_UART, UARTKBD_BAUD);
    gpio_set_function(UARTKBD_TX_PIN, GPIO_FUNC_UART_AUX);
    gpio_set_function(UARTKBD_RX_PIN, GPIO_FUNC_UART_AUX);
    uart_set_format(UARTKBD_UART, 8, 1, UART_PARITY_NONE);
    uart_set_hw_flow(UARTKBD_UART, false, false);
    uart_set_fifo_enabled(UARTKBD_UART, true);
}

void uartkbd_task(void)
{
    while (uart_is_readable(UARTKBD_UART))
        uartkbd_parse_byte(&s_parser, (uint8_t)uart_getc(UARTKBD_UART));
}

bool     uartkbd_next_event(uartkbd_event_t *ev) { return uartkbd_parse_next_event(&s_parser, ev); }
uint16_t uartkbd_buttons(void) { return uartkbd_parse_buttons(&s_parser); }
uint8_t  uartkbd_flags(void)   { return uartkbd_parse_flags(&s_parser); }
uint32_t uartkbd_frames(void)  { return uartkbd_parse_frames(&s_parser); }
uint32_t uartkbd_errors(void)  { return uartkbd_parse_errors(&s_parser); }
```

- [ ] **Step 2: Wire into build and umbrella header**

`bsp/CMakeLists.txt`: add `input/uartkbd.c` to the source list (after
`input/uartkbd_parse.c`).

`bsp/fw2.h`: add after the `keyboard/fw2kb.h` include:

```c
#include "input/uartkbd.h"      // (FW2 UART keyboard: 14 buttons @ UART1 62500, GPIO38/39)
```

- [ ] **Step 3: Verify target build**

Run: `python tools/fw.py build`
Expected: clean build (compiles uartkbd.c against the Pico SDK; host tests
don't touch this file). If `GPIO_FUNC_UART_AUX` is undefined, the SDK in use
predates RP2350 AUX support — STOP and report; do not substitute
`GPIO_FUNC_UART`.

- [ ] **Step 4: Run host tests (unchanged, regression check)**

Run: `python tools/fw.py test`
Expected: `100% tests passed`.

- [ ] **Step 5: Commit**

```bash
git add bsp/input/uartkbd.h bsp/input/uartkbd.c bsp/CMakeLists.txt bsp/fw2.h
git commit -m "feat: UART1 binding for the FW2 UART keyboard (RX-only, polled)"
```

---

### Task 4: hello_keyboard demo app

**Files:**
- Create: `apps/hello_keyboard/CMakeLists.txt`, `apps/hello_keyboard/main.c`
- Modify: `CMakeLists.txt` (top level — add subdirectory)

**Interfaces:**
- Consumes: `fw2kb` (Task 1), `uartkbd` (Task 3), `st7796_*`, `ft6336_*`,
  `board_*`, `DIAG()` — all via `#include "fw2.h"` + `platform/diag.h`.
- Produces: the `hello_keyboard` app target.

- [ ] **Step 1: App CMakeLists**

`apps/hello_keyboard/CMakeLists.txt`:

```cmake
add_executable(hello_keyboard main.c)
target_link_libraries(hello_keyboard freewili2_bsp)
pico_set_binary_type(hello_keyboard copy_to_ram)   # required: firmware runs from SRAM
pico_add_extra_outputs(hello_keyboard)             # .uf2 / .bin / .map
```

Top-level `CMakeLists.txt`: add after the `add_subdirectory(apps/toggleled)` line:

```cmake
add_subdirectory(apps/hello_keyboard)
```

- [ ] **Step 2: Write the app**

`apps/hello_keyboard/main.c`:

```c
// hello_keyboard — fw2kb two-press chord keyboard on the ST7796, driven by
// the FW2 UART keyboard (GREY/YELLOW/GREEN/BLUE/RED chords, PAGE cycles
// pages) with touch space/backspace (below/above the split line).
// Layout (480x320 landscape): text area y 0-271, button bar y 272-319.
#include <string.h>
#include "fw2.h"
#include "platform/diag.h"
#include "pico/stdlib.h"

/* RGB565 colors, byte-swapped to wire (big-endian) order per st7796.h */
#define COL_BLACK  0x0000
#define COL_WHITE  0xFFFF
#define COL_GRAY   0x1084   /* 0x8410 */
#define COL_YELLOW 0xE0FF   /* 0xFFE0 */
#define COL_GREEN  0xE007   /* 0x07E0 */
#define COL_BLUE   0x1F00   /* 0x001F */
#define COL_RED    0x00F8   /* 0xF800 */
#define COL_DIM    0xE739   /* 0x39E7 */

#define BAR_Y       272
#define BAR_H       48
#define BTN_W       96
#define TOUCH_SPLIT 136     /* fw2kb threshold: y > 136 = space, else backspace */

#define TEXT_SCALE 2
#define CHAR_W     (6 * TEXT_SCALE)
#define LINE_H     (8 * TEXT_SCALE)
#define TEXT_COLS  (ST7796_W / CHAR_W)
#define TEXT_ROWS  (BAR_Y / LINE_H)
#define TEXT_MAX   1024

static fw2kb_t s_kb;
static char    s_text[TEXT_MAX];
static int     s_len;
static bool    s_text_dirty = true;
static bool    s_bar_dirty  = true;
static char    s_bar_cache[5][6];

static const uint16_t k_btn_cols[5] =
    { COL_GRAY, COL_YELLOW, COL_GREEN, COL_BLUE, COL_RED };

static void draw_bar(void)
{
    const char *labels[5];
    fw2kb_get_labels(&s_kb, labels);
    for (int i = 0; i < 5; i++) {
        int x = i * BTN_W;
        st7796_fill_rect(x, BAR_Y, BTN_W, BAR_H, k_btn_cols[i]);
        int len = (int)strlen(labels[i]);
        if (len > 5) len = 5;
        int tx = x + (BTN_W - len * CHAR_W) / 2;
        int ty = BAR_Y + (BAR_H - 8 * TEXT_SCALE) / 2;
        st7796_draw_text(tx, ty, TEXT_SCALE, COL_BLACK, k_btn_cols[i], labels[i]);
        strncpy(s_bar_cache[i], labels[i], 5);
        s_bar_cache[i][5] = 0;
    }
}

static bool bar_changed(void)
{
    const char *labels[5];
    fw2kb_get_labels(&s_kb, labels);
    for (int i = 0; i < 5; i++)
        if (strncmp(labels[i], s_bar_cache[i], 5) != 0) return true;
    return false;
}

static void draw_text_area(void)
{
    st7796_fill_rect(0, 0, ST7796_W, BAR_Y, COL_BLACK);
    st7796_fill_rect(0, TOUCH_SPLIT, ST7796_W, 1, COL_DIM);
    st7796_draw_text(4, TOUCH_SPLIT - 10, 1, COL_DIM, COL_BLACK, "tap above = backspace");
    st7796_draw_text(4, TOUCH_SPLIT + 3,  1, COL_DIM, COL_BLACK, "tap below = space");

    int col = 0, row = 0;
    for (int i = 0; i < s_len && row < TEXT_ROWS; i++) {
        char c = s_text[i];
        if (c == '\n') { row++; col = 0; continue; }
        if (col >= TEXT_COLS) { row++; col = 0; }
        if (row >= TEXT_ROWS) break;
        char s[2] = { c, 0 };
        st7796_draw_text(col * CHAR_W, row * LINE_H, TEXT_SCALE,
                         COL_WHITE, COL_BLACK, s);
        col++;
    }
    if (row < TEXT_ROWS)   /* cursor underline at the next cell */
        st7796_fill_rect(col * CHAR_W, row * LINE_H + LINE_H - 2,
                         CHAR_W, 2, COL_WHITE);
}

static void append_char(char c)
{
    if (s_len < TEXT_MAX - 1) { s_text[s_len++] = c; s_text_dirty = true; }
}

static void handle_buttons(void)
{
    uartkbd_event_t ev;
    while (uartkbd_next_event(&ev)) {
        DIAG("uartkbd btn %d %s\n", (int)ev.btn, ev.pressed ? "down" : "up");
        if (!ev.pressed) continue;
        if (ev.btn <= UARTKBD_BTN_RED)
            fw2kb_press(&s_kb, (fw2kb_btn)ev.btn);   /* GREY..RED == GRAY..RED */
        else if (ev.btn == UARTKBD_BTN_PAGE)
            fw2kb_press(&s_kb, FW2KB_BTN_AI);        /* page cycle / chord cancel */
    }
}

static void handle_touch(void)
{
    static bool was_down = false;
    uint16_t x, y;
    bool down = ft6336_poll(&x, &y);
    if (down && !was_down && y < BAR_Y)              /* touch-down edge only */
        fw2kb_touch(&s_kb, (int)x, (int)y);
    was_down = down;
}

static void handle_fw2kb_events(void)
{
    fw2kb_event ev;
    while (fw2kb_next_event(&s_kb, &ev)) {
        switch (ev.key) {
        case FW2KB_KEY_CHAR:      append_char(ev.ch); break;
        case FW2KB_KEY_BACKSPACE: if (s_len) { s_len--; s_text_dirty = true; } break;
        case FW2KB_KEY_ENTER:     append_char('\n'); break;
        case FW2KB_KEY_TAB:       for (int i = 0; i < 4; i++) append_char(' '); break;
        default: DIAG("fw2kb key %d\n", (int)ev.key); break;
        }
    }
}

int main(void)
{
    board_init();
    st7796_init();
    board_backlight_set(1);
    ft6336_init();
    uartkbd_init();
    fw2kb_init(&s_kb);
    fw2kb_set_touch_threshold(&s_kb, TOUCH_SPLIT);
    DIAG("hello_keyboard up\n");

    uint64_t next_link_log = 0;
    while (true) {
        uartkbd_task();
        handle_buttons();
        handle_touch();
        handle_fw2kb_events();

        if (bar_changed()) s_bar_dirty = true;
        if (s_text_dirty) { draw_text_area(); s_text_dirty = false; }
        if (s_bar_dirty)  { draw_bar();       s_bar_dirty  = false; }

        uint64_t now = time_us_64();
        if (now >= next_link_log) {
            DIAG("uartkbd frames=%u errors=%u\n",
                 (unsigned)uartkbd_frames(), (unsigned)uartkbd_errors());
            next_link_log = now + 1000000;
        }
        sleep_ms(2);
    }
}
```

- [ ] **Step 3: Build the app**

Run: `python tools/fw.py build hello_keyboard`
Expected: clean build producing `build/apps/hello_keyboard/hello_keyboard.elf`.
(If `board_backlight_set` doesn't exist under that name, check
`bsp/platform/board.h` for the actual backlight call used by
`apps/hello_display/main.c` and use that — do not invent one.)

- [ ] **Step 4: Run host tests (regression)**

Run: `python tools/fw.py test`
Expected: `100% tests passed`.

- [ ] **Step 5: Commit**

```bash
git add apps/hello_keyboard CMakeLists.txt
git commit -m "feat: hello_keyboard demo — chord typing via UART keyboard + touch"
```

---

### Task 5: Docs, catalog, and hardware smoke checklist

**Files:**
- Create: `docs/drivers/keyboard.md`
- Modify: `docs/hardware/catalog.md:40`

**Interfaces:**
- Consumes: everything above.
- Produces: documentation; the on-hardware acceptance checklist for the owner.

- [ ] **Step 1: Write `docs/drivers/keyboard.md`**

```markdown
# Keyboard (`bsp/keyboard/`, `bsp/input/uartkbd*`) — fw2kb chord engine + FW2 UART keyboard

**What it does:** two-press chord text entry on the 5 colored FW2 buttons
(gray, yellow, green, blue, red): first press picks a group of 5 characters,
second press picks the character — every a-y letter is exactly two presses.
PAGE cycles pages (upper / lower / numbers / symbols / hex; mid-chord it
cancels the chord). Touch is space (below the app's split line) or
backspace (above). Button state arrives from the FW2 UART keyboard
(button coprocessor) as unsolicited 23-byte frames.

**Pieces:**
- `keyboard/fw2kb.{h,c}`, `keyboard/fw2kb_hidmap.c` — the chord engine,
  harvested verbatim from the owner's `wilikeyboard` repo (poll-based,
  no malloc, no callbacks; host-tested by `tests/test_fw2kb*.c`). The HID
  map translates USB HID usage+modifier pairs — unused on-device today,
  available for a future USB/PC-bridge input source.
- `input/uartkbd_parse.{h,c}` — pure frame parser + press/release edge
  detection (host-tested by `tests/test_uartkbd_parse.c`). Protocol per
  `FreeWilli/vibe/Wilikeyboard.md`: sync 0xBD 0x1D, buttons in bytes 2-5,
  additive 8-bit checksum of bytes 0-21 in byte 22.
- `input/uartkbd.{h,c}` — UART1 binding @ 62500 8N1, GPIO38 (TX, claimed,
  never driven) / GPIO39 (RX), `GPIO_FUNC_UART_AUX` (the plain UART
  function on these pins is CTS/RTS). RX-only, polled.

**How to use:**

    #include "fw2.h"

    fw2kb_t kb;
    uartkbd_init();
    fw2kb_init(&kb);
    fw2kb_set_touch_threshold(&kb, 136);

    for (;;) {
        uartkbd_task();                         /* drain UART each loop */
        uartkbd_event_t bev;
        while (uartkbd_next_event(&bev)) {
            if (!bev.pressed) continue;
            if (bev.btn <= UARTKBD_BTN_RED) fw2kb_press(&kb, (fw2kb_btn)bev.btn);
            else if (bev.btn == UARTKBD_BTN_PAGE) fw2kb_press(&kb, FW2KB_BTN_AI);
        }
        fw2kb_event kev;
        while (fw2kb_next_event(&kb, &kev)) { /* CHAR/BACKSPACE/... */ }
        const char *labels[5];
        fw2kb_get_labels(&kb, labels);          /* draw on the soft buttons */
    }

`apps/hello_keyboard` is the worked example (soft-button bar + text area +
touch zones). Link health: `uartkbd_frames()` / `uartkbd_errors()` — a
healthy link shows frames climbing and errors static.

**Protocol assumptions to re-verify when the keyboard firmware lands**
(the PIC-side firmware was being updated when this driver was written):
checksum = additive 8-bit sum of bytes 0-21; button bits 1 = pressed;
AUDIO/HOTPLUG/USB detect bits are level flags. All three live in
`uartkbd_parse.c` only.

**Dependencies:** none beyond `pico_stdlib` (`hardware_uart` comes with it).
UART1 must stay free for this driver (UART0 is the OneWili/FwGUI link).
```

- [ ] **Step 2: Update the catalog row**

In `docs/hardware/catalog.md`, replace line 40 (the
`| 14-button serial coprocessor | ... | Owner repo not yet confirmed ... |` row) with:

```markdown
| 14-button serial coprocessor | TX=GPIO38, RX=GPIO39 (UART1 @ 62500 8N1, RX-only) | **DONE** — `bsp/input/uartkbd*` frame parser + `bsp/keyboard/fw2kb*` chord engine (harvested from `../wilikeyboard`). See docs/drivers/keyboard.md |
```

- [ ] **Step 3: Full verification**

Run: `python tools/fw.py test`
Expected: `100% tests passed` (all pre-existing + fw2kb + fw2kb_hid + uartkbd_parse).

Run: `python tools/fw.py build hello_keyboard`
Expected: clean build.

- [ ] **Step 4: Commit**

```bash
git add docs/drivers/keyboard.md docs/hardware/catalog.md
git commit -m "docs: keyboard driver doc + catalog DONE for the button coprocessor"
```

- [ ] **Step 5: On-hardware smoke checklist (manual, owner-run)**

Not agent-executable unless the board + probe are attached. With the
FreeWili 2 connected and the updated keyboard firmware on the button
coprocessor:

```
python tools/fw.py flash hello_keyboard
python tools/fw.py rtt          # in a second terminal
```

1. Screen shows a black text area, a dim split line with zone hints, and
   5 colored buttons labeled `ABCDE FGHIJ KLMNO PQRST UVWXY` (the display font folds lowercase to uppercase; the engine is on the lowercase page).
2. RTT shows `uartkbd frames=N errors=0` with N climbing (~ frame rate of
   the keyboard firmware). frames=0 → link problem (pins/baud/firmware).
3. Press GREEN then GREEN: types `m` — shown as `M` on screen; RTT `fw2kb char 'm'` is the authoritative case check.
4. PAGE cycles pages, but the lower and upper letter pages look identical on screen (uppercase folding) — verify the transition via RTT char case (`'m'` vs `'M'`) and via the numbers page labels (`01234 56789 ...`).
5. Tap below the split line = space; above = backspace.
6. Every button press/release appears in RTT (`uartkbd btn N down/up`).
7. If the checksum assumption is wrong (errors climbing, frames=0), fix
   `accept_frame()` in `bsp/input/uartkbd_parse.c` and re-run the parse
   tests — the assumption is isolated there by design.
8. Symbol characters ` { } | ~ render as blanks on screen (font5x7 covers 0x20-0x5F only); RTT is authoritative.
```
