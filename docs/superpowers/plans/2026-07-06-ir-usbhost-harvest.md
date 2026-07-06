# IR + USB-Host BSP Harvest Implementation Plan (WiliIR â†’ wilibsp)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Lift WiliIR's hardware-proven IR engine into `wilibsp/bsp/ir/` and its polled USB-host MSC stack (+ FatFs) into `wilibsp/bsp/usbhost/`, each with a flashable demo app, carried host tests, and catalog/driver docs â€” completing WiliIR's founding side goal.

**Architecture:** Pure verbatim copies wherever possible (the modules were written to the bsp convention from day one â€” see `WiliIR/docs/harvest.md`, the binding contract for this work). Only three kinds of edits are allowed: (1) rooted `"usb/x.h"`/`"src path"` include/comment adjustments where the directory name changes, (2) additive wiring (CMake, `fw2.h`, `board.h`, `ioexp`), (3) the two new demo apps and docs. The demo apps are the hardware verification vehicle: the board + CMSIS-DAP probe are attached to this machine and a FAT32 stick with Flipper-IRDB content is seated, so both harvests get bench-verified via TXâ†’RX loopback and a real mount â€” no human needed at the bench.

**Tech Stack:** wilibsp conventions throughout â€” `fw build <app>` / `fw flash <app>` / `fw rtt` / `fw test` (see `AGENTS.md`), `freewili2_bsp` static lib with include root `bsp/`, host CTest tree in `tests/` (MinGW GCC, no Pico SDK).

## Global Constraints

- **Work in `C:\~prj\Dropbox\vibeProjects\wilibsp`** on branch `harvest-ir-usbhost` (off wilibsp's `main`). The source repo `C:\~prj\Dropbox\vibeProjects\WiliIR` is READ-ONLY for Tasks 1â€“5 (Task 6 makes one small WiliIR commit).
- **Copy verbatim; keep the proven names** (`ir_*`, `usb_*`, `ioexp_*` â€” no `fw2_` renames; wilibsp `AGENTS.md` Â§ Naming). Any non-verbatim change beyond the three allowed kinds is a spec violation.
- **The harvest contract is `C:\~prj\Dropbox\vibeProjects\WiliIR\docs\harvest.md`** â€” module map, power-gate requirements, PIO/IRQ policy, board.h pins. Read it before any task.
- **Hardware invariants carry over:** pio2 = IR only (capture SM0 by init order, TX SM1); polled, NO IRQs for IR or USB; RTT-only diagnostics (`DIAG`); the IR rail (PCAL6524 P2_0) and USB port power (P0_0 + P1_4) are gated and OFF at power-on â€” `ir_capture_init()` / `usb_store_init()` must keep turning them on.
- **wilibsp shared-resource facts** (docs/hardware/facts.md): shared SPI1, shared DMA_IRQ_0 (display flush uses it â€” IR/USB must NOT), copy_to_ram SRAM budget. IR claims 2 pio2 SMs + 2 DMA channels (polled); usbmsc claims the native USB controller.
- **Both repos' test suites stay green:** `fw test` in wilibsp after every task; WiliIR is untouched until Task 6 (its 14 tests must still pass after that task's doc-only commit).
- **Docs honesty:** hardware claims only for what a demo app demonstrably did over RTT this session; everything else says "carried from WiliIR (verified there 2026-07-05/06)".
- Flashing demo apps overwrites the WiliIR firmware on the board â€” expected; Task 5's wrap-up step reflashes WiliIR.

## Module map recap (from harvest.md, + one post-contract addition)

- `bsp/ir/` â† `WiliIR/src/ir/*` (all) + `WiliIR/src/db/{ir_file,db_sort,ir_resolve}.{c,h}`. `ir_resolve` post-dates harvest.md but is pure (ir_file â†’ timings resolver, host-tested) and belongs with `ir_file`; Task 6 records the addition in harvest.md.
- `bsp/usbhost/` â† `WiliIR/src/usb/*` + `WiliIR/third_party/fatfs/*` â†’ `bsp/third_party/fatfs/`.
- `bsp/platform/` â† WiliIR's `ioexp.{c,h}` (a strict superset of wilibsp's: adds `ioexp_ir_pwr`, `ioexp_usb_pwr`, the s_p0/s_p2 shadows â€” verified by diff) + 2 `PIN_*` defines in `board.h`.
- NOT harvested: `src/app/`, `src/remote/`, `src/ui/`, `src/db/{db_index,ir_file_write}` (app-level, per harvest.md).

---

### Task 1: bsp/ir pure-logic group + host tests

**Files:**
- Create: `bsp/ir/` with verbatim copies from `C:\~prj\Dropbox\vibeProjects\WiliIR\src\ir\`: `ir_types.h`, `ir_protocols.c`, `ir_protocols.h`, `ir_decode.c`, `ir_decode.h`, `ir_encode.c`, `ir_encode.h`, `ir_frame.c`, `ir_frame.h`, `ir_tx_pack.c`, `ir_tx_pack.h`; and from `...\WiliIR\src\db\`: `ir_file.c`, `ir_file.h`, `db_sort.c`, `db_sort.h`, `ir_resolve.c`, `ir_resolve.h`
- Create: `tests/test_ir_nec.c`, `test_ir_pd.c`, `test_ir_manchester.c`, `test_ir_kaseikyo.c`, `test_ir_dispatch.c`, `test_ir_frame.c`, `test_ir_tx_pack.c`, `test_ir_file.c`, `test_db_sort.c`, `test_ir_golden.c`, `test_ir_resolve.c` (verbatim from `WiliIR\tests\`)
- Modify: `bsp/CMakeLists.txt`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: nothing (this whole group is C11 stdlib only â€” no Pico SDK).
- Produces: every pure header Task 2's drivers and Task 3's app include (`ir_types.h`, `ir_decode.h`, `ir_encode.h`, `ir_frame.h`, `ir_tx_pack.h`, `ir_file.h`, `ir_resolve.h`, `db_sort.h`).

- [ ] **Step 1: Branch + copy the 16 source files**

```bash
cd /c/~prj/Dropbox/vibeProjects/wilibsp
git checkout -b harvest-ir-usbhost
mkdir -p bsp/ir
cp /c/~prj/Dropbox/vibeProjects/WiliIR/src/ir/{ir_types.h,ir_protocols.c,ir_protocols.h,ir_decode.c,ir_decode.h,ir_encode.c,ir_encode.h,ir_frame.c,ir_frame.h,ir_tx_pack.c,ir_tx_pack.h} bsp/ir/
cp /c/~prj/Dropbox/vibeProjects/WiliIR/src/db/{ir_file.c,ir_file.h,db_sort.c,db_sort.h,ir_resolve.c,ir_resolve.h} bsp/ir/
```

Then fix ONLY the file-top path comments (`// src/ir/ir_types.h` â†’ `// bsp/ir/ir_types.h`, `// src/db/ir_file.c` â†’ `// bsp/ir/ir_file.c`, etc.) in each copied file â€” nothing else. All cross-file includes in this group are bare (`"ir_types.h"`, `"ir_encode.h"`) and now resolve same-directory; verify with a grep that no copied file contains `#include "db/` or `#include "ir/`.

- [ ] **Step 2: Copy the 11 test files + reconcile test_util.h**

```bash
cd /c/~prj/Dropbox/vibeProjects/wilibsp
cp /c/~prj/Dropbox/vibeProjects/WiliIR/tests/{test_ir_nec.c,test_ir_pd.c,test_ir_manchester.c,test_ir_kaseikyo.c,test_ir_dispatch.c,test_ir_frame.c,test_ir_tx_pack.c,test_ir_file.c,test_db_sort.c,test_ir_golden.c,test_ir_resolve.c} tests/
```

wilibsp already has its own `tests/test_util.h`. Diff it against `WiliIR/tests/test_util.h`: if the macros the copied tests use (`ASSERT_TRUE`, `TEST_RETURN`) exist with the same semantics, keep wilibsp's file untouched. If any macro is missing, add it to wilibsp's `test_util.h` (additive only â€” existing wilibsp tests must not change behavior). Do NOT copy WiliIR's file over wilibsp's.

- [ ] **Step 3: Wire the test tree and run it**

Append to `tests/CMakeLists.txt`, following the existing entries' pattern exactly (sources compiled directly from `../bsp`), one block per test:

```cmake
# --- bsp/ir pure-logic group (harvested from WiliIR; see docs/drivers/ir.md) ---
set(BSPIR ${CMAKE_CURRENT_SOURCE_DIR}/../bsp/ir)
add_executable(test_ir_nec test_ir_nec.c ${BSPIR}/ir_decode.c ${BSPIR}/ir_encode.c ${BSPIR}/ir_protocols.c)
target_include_directories(test_ir_nec PRIVATE ${BSPIR} ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME ir_nec COMMAND test_ir_nec)
```

â€¦and equivalently for the other ten, with these source lists (all `target_include_directories` are `${BSPIR} ${CMAKE_CURRENT_SOURCE_DIR}`):
- `test_ir_pd`, `test_ir_manchester`, `test_ir_kaseikyo`, `test_ir_dispatch`: same sources as `test_ir_nec`
- `test_ir_frame`: + `${BSPIR}/ir_frame.c`
- `test_ir_tx_pack`: `${BSPIR}/ir_tx_pack.c` only
- `test_ir_file`: `${BSPIR}/ir_file.c ${BSPIR}/ir_protocols.c`
- `test_db_sort`: `${BSPIR}/db_sort.c` only
- `test_ir_golden`: `${BSPIR}/ir_decode.c ${BSPIR}/ir_protocols.c`
- `test_ir_resolve`: `${BSPIR}/ir_resolve.c ${BSPIR}/ir_encode.c ${BSPIR}/ir_decode.c ${BSPIR}/ir_protocols.c`

Run: `python tools/fw.py test` (or `tools\fw.cmd test`) from the wilibsp root.
Expected: all pre-existing wilibsp tests PLUS the 11 new ones pass.

- [ ] **Step 4: Add the pure sources to the BSP library**

In `bsp/CMakeLists.txt`, add to the `add_library(freewili2_bsp STATIC ...)` list (keep alphabetical-ish grouping, after `sensors/`):

```cmake
    ir/ir_protocols.c
    ir/ir_decode.c
    ir/ir_encode.c
    ir/ir_frame.c
    ir/ir_tx_pack.c
    ir/ir_file.c
    ir/ir_resolve.c
    ir/db_sort.c
```

Run: `python tools/fw.py build hello_display` â€” expected: clean build (proves the lib still compiles for target with the new sources).

- [ ] **Step 5: Commit**

```bash
git add bsp/ir tests bsp/CMakeLists.txt
git commit -m "feat(ir): harvest WiliIR pure IR engine - decoders/encoders/framer/.ir parser + 11 host tests"
```

---

### Task 2: IR hardware drivers + platform additions (ioexp, board.h, fw2.h)

**Files:**
- Create: `bsp/ir/ir_capture.c`, `ir_capture.h`, `ir_capture.pio`, `ir_tx.c`, `ir_tx.h`, `ir_tx.pio` (verbatim from `WiliIR/src/ir/`)
- Overwrite: `bsp/platform/ioexp.c`, `bsp/platform/ioexp.h` (with `WiliIR/src/platform/ioexp.{c,h}` â€” a verified strict superset: adds `ioexp_ir_pwr`, `ioexp_usb_pwr`, s_p0/s_p2 output shadows; no wilibsp-only content is lost, confirmed by diff during planning)
- Modify: `bsp/platform/board.h`, `bsp/CMakeLists.txt`, `bsp/fw2.h`

**Interfaces:**
- Consumes: `platform/board.h` (`PIN_IR_RX`/`PIN_IR_TX` â€” added here), `platform/diag.h`, `platform/ioexp.h` (`ioexp_ir_pwr`), Task 1's pure headers.
- Produces: `ir_capture_init/start/stop/poll/overruns`, `ir_tx_init/set_carrier/send/busy/stop` â€” Task 3's app uses them; `ioexp_usb_pwr` â€” Task 4 uses it.

- [ ] **Step 1: Copy drivers + overwrite ioexp**

```bash
cd /c/~prj/Dropbox/vibeProjects/wilibsp
cp /c/~prj/Dropbox/vibeProjects/WiliIR/src/ir/{ir_capture.c,ir_capture.h,ir_capture.pio,ir_tx.c,ir_tx.h,ir_tx.pio} bsp/ir/
cp /c/~prj/Dropbox/vibeProjects/WiliIR/src/platform/{ioexp.c,ioexp.h} bsp/platform/
```

Fix file-top path comments in all copied files (`// src/...` â†’ `// bsp/...`). The drivers' rooted includes (`"platform/board.h"`, `"platform/diag.h"`, `"platform/ioexp.h"`) resolve unchanged against the `bsp/` include root; their bare includes (`"ir_frame.h"`, `"ir_tx_pack.h"`, generated `"ir_capture.pio.h"`) resolve same-directory/build-dir. Verify with grep that no include needs edits beyond that.

- [ ] **Step 2: board.h pins**

Add to `bsp/platform/board.h`, next to the existing `PIN_*` defines (copy verbatim from `WiliIR/src/platform/board.h`):

```c
#define PIN_IR_TX  20   // IR transmitter LED (PIO carrier-modulated, pio2)
#define PIN_IR_RX  24   // IR receiver, TSOP-style demodulated envelope: idle HIGH, mark = LOW
```

- [ ] **Step 3: CMake + umbrella header**

`bsp/CMakeLists.txt`:
1. Add `ir/ir_capture.c` and `ir/ir_tx.c` to the library source list (next to the Task 1 ir/ block).
2. Add PIO generation next to the existing WS2812 example:

```cmake
pico_generate_pio_header(freewili2_bsp ${CMAKE_CURRENT_SOURCE_DIR}/ir/ir_capture.pio)
pico_generate_pio_header(freewili2_bsp ${CMAKE_CURRENT_SOURCE_DIR}/ir/ir_tx.pio)
```

3. Check `target_link_libraries(freewili2_bsp PUBLIC ...)` already lists `hardware_pio` and `hardware_dma` (the radio/LED drivers almost certainly pull them in) â€” add whichever is missing, nothing else.

`bsp/fw2.h`: add, following the existing grouped-include style:

```c
#include "ir/ir_capture.h"   // (harvested: IR receiver, PIO2 SM0 + DMA ring, WiliIR)
#include "ir/ir_tx.h"        // (harvested: IR transmitter, PIO2 SM1 carrier modulator, WiliIR)
#include "ir/ir_decode.h"    // (harvested: protocol decoders, pure)
#include "ir/ir_encode.h"    // (harvested: protocol encoders, pure)
#include "ir/ir_file.h"      // (harvested: Flipper .ir parser/writer, pure)
#include "ir/ir_resolve.h"   // (harvested: .ir entry -> timings resolver, pure)
```

- [ ] **Step 4: Build + test + commit**

Run: `python tools/fw.py build hello_display` â€” expected: clean (lib compiles with drivers + PIO headers).
Run: `python tools/fw.py test` â€” expected: all green (ioexp overwrite touches no host test).

```bash
git add bsp/ir bsp/platform bsp/CMakeLists.txt bsp/fw2.h
git commit -m "feat(ir): harvest capture/TX PIO drivers; ioexp gains IR + USB power gates; IR pins in board.h"
```

---

### Task 3: apps/hello_ir demo + on-hardware loopback verification

**Files:**
- Create: `apps/hello_ir/main.c`, `apps/hello_ir/CMakeLists.txt`, `apps/hello_ir/README.md`
- Modify: `apps/CMakeLists.txt` (or however wilibsp registers apps â€” check how `hello_mics` is added and mirror it)

**Interfaces:**
- Consumes: Task 1 + Task 2 surfaces via `fw2.h`.
- Produces: the catalog demo referenced by Task 6's docs.

- [ ] **Step 1: Write the app**

`apps/hello_ir/CMakeLists.txt` (mirror `apps/hello_mics/CMakeLists.txt` exactly, name `hello_ir`):

```cmake
add_executable(hello_ir main.c)
target_link_libraries(hello_ir freewili2_bsp)
pico_set_binary_type(hello_ir copy_to_ram)
pico_add_extra_outputs(hello_ir)
```

`apps/hello_ir/main.c`:

```c
// hello_ir â€” on-hardware smoke test for the harvested IR engine (bsp/ir).
// RTT-only (fw rtt), no display. Every 5 s it encodes and transmits one NEC
// frame (A:0x04 C:0x08); the on-board TSOP receiver hears the on-board TX
// LED, so the capture->decode path reports it back: a zero-equipment
// loopback. Any external remote pointed at the receiver decodes live too.
// Pass criteria: "tx: sent" alternating with "rx: ... NEC A:0x4 C:0x8"
// every 5 s; a real remote press prints its own decode line; overruns 0.
#include "fw2.h"
#include "platform/diag.h"
#include "pico/stdlib.h"

int main(void) {
    board_init();
    DIAG("hello_ir: up\n");
    ir_capture_init();               // powers the IR rail (PCAL6524 P2_0)
    sleep_ms(5);                     // rail settle: no display bring-up here
                                     // to hide behind (see WiliIR harvest.md)
    ir_tx_init(38000);
    ir_capture_start();
    DIAG("hello_ir: capture running, NEC loopback every 5 s\n");

    static uint32_t durs[IR_MAX_TIMINGS];
    ir_frame_t frame;
    ir_message_t msg;
    absolute_time_t next_tx = make_timeout_time_ms(1000);

    while (true) {
        if (time_reached(next_tx) && !ir_tx_busy()) {
            const ir_message_t out = {IR_PROTO_NEC, 0x04, 0x08, false};
            uint32_t n = ir_encode(&out, durs, IR_MAX_TIMINGS);
            if (n && ir_tx_send(durs, n, 38000)) DIAG("tx: sent NEC A:0x4 C:0x8\n");
            next_tx = make_timeout_time_ms(5000);
        }
        if (ir_capture_poll(&frame)) {
            if (ir_decode(frame.durs, frame.count, &msg))
                DIAG("rx: %lu edges  %s A:0x%lX C:0x%lX%s  (ovr %lu)\n",
                     (unsigned long)frame.count, ir_protocol_name(msg.protocol),
                     (unsigned long)msg.address, (unsigned long)msg.command,
                     msg.repeat ? " rpt" : "",
                     (unsigned long)ir_capture_overruns());
            else
                DIAG("rx: %lu edges  RAW first=%lu,%lu,%lu\n",
                     (unsigned long)frame.count, (unsigned long)frame.durs[0],
                     (unsigned long)frame.durs[1], (unsigned long)frame.durs[2]);
        }
        sleep_ms(2);
    }
}
```

`apps/hello_ir/README.md`: three sentences â€” what it does, `fw build hello_ir` / `fw flash hello_ir` / `fw rtt`, the pass criteria from the header comment.

Register the app the same way `hello_mics` is registered (find the `add_subdirectory(hello_mics)`-style line in the apps-level CMakeLists and add `hello_ir` alongside).

- [ ] **Step 2: Build**

Run: `python tools/fw.py build hello_ir` â€” expected: clean `hello_ir.elf`/`.uf2`.

- [ ] **Step 3: Flash + RTT verify (hardware gate â€” probe attached, no human needed)**

Run: `python tools/fw.py flash hello_ir`, then capture ~15 s of RTT (wilibsp's `fw rtt` or the OpenOCD RTT pattern from `tools/`).
Expected RTT evidence (record it in the task report verbatim):
- boot line, `capture running`
- at least two `tx: sent` / `rx: ... NEC A:0x4 C:0x8` loopback pairs
- `ovr 0` throughout

If NO FRAME appears: the two historical root causes are the IR power gate (must see the `ioexp: IR_PWR (P2_0) -> 1` DIAG from `ir_capture_init`) and capture-SM program-counter reset (already fixed in the harvested source) â€” diagnose against `WiliIR/docs/hardware-notes.md` before touching code.

- [ ] **Step 4: Commit**

```bash
git add apps
git commit -m "feat(ir): hello_ir demo - 5 s NEC loopback + live decode, bench-verified"
```

---

### Task 4: bsp/usbhost + FatFs vendoring

**Files:**
- Create: `bsp/usbhost/` with verbatim copies from `WiliIR/src/usb/`: `usb_core.c/.h`, `usb_hcd.c/.h`, `usb_hub.c/.h`, `usb_msc.c/.h`, `usb_parse.c/.h`, plus `msc_disk.c`, `usb_store.c`, `usb_store.h` (two include adjustments, below)
- Create: `bsp/third_party/fatfs/` â€” verbatim copy of `WiliIR/third_party/fatfs/` (all files, including its README and `ffconf.h` â€” the WiliIR ffconf deltas and the 2026-07-01 NORTC date travel as-is)
- Modify: `bsp/CMakeLists.txt`, `bsp/fw2.h`

**Interfaces:**
- Consumes: `platform/ioexp.h` (`ioexp_usb_pwr`, from Task 2), `platform/diag.h`, native USB controller (Pico SDK regs only â€” the usbmsc core is self-contained and polled).
- Produces: `usb_store_init/task/mounted` + the whole FatFs API (`ff.h`) for Task 5's app and future consumers.

- [ ] **Step 1: Copy everything**

```bash
cd /c/~prj/Dropbox/vibeProjects/wilibsp
mkdir -p bsp/usbhost bsp/third_party/fatfs
cp /c/~prj/Dropbox/vibeProjects/WiliIR/src/usb/{usb_core.c,usb_core.h,usb_hcd.c,usb_hcd.h,usb_hub.c,usb_hub.h,usb_msc.c,usb_msc.h,usb_parse.c,usb_parse.h,msc_disk.c,usb_store.c,usb_store.h} bsp/usbhost/
cp /c/~prj/Dropbox/vibeProjects/WiliIR/third_party/fatfs/* bsp/third_party/fatfs/
```

- [ ] **Step 2: The only include adjustments in the whole harvest**

The five usbmsc core files use bare includes (verified) â€” untouched. The two WiliIR-written glue files used rooted `"usb/â€¦"` paths because WiliIR's directory was `src/usb/`; here they live in `bsp/usbhost/` so those become same-directory bare includes:
- `bsp/usbhost/msc_disk.c`: `#include "usb/usb_msc.h"` â†’ `#include "usb_msc.h"`
- `bsp/usbhost/usb_store.c`: `#include "usb/usb_store.h"` â†’ `#include "usb_store.h"`, `#include "usb/usb_msc.h"` â†’ `#include "usb_msc.h"` (the `"platform/â€¦"` and `"ff.h"` includes stay).

Fix file-top path comments in all copied files. Then grep `bsp/usbhost/` for `#include "usb/` â€” expected: zero hits.

- [ ] **Step 3: Wire CMake + fw2.h**

`bsp/CMakeLists.txt`:
1. Sources:

```cmake
    usbhost/usb_core.c
    usbhost/usb_hcd.c
    usbhost/usb_hub.c
    usbhost/usb_msc.c
    usbhost/usb_parse.c
    usbhost/msc_disk.c
    usbhost/usb_store.c
    third_party/fatfs/ff.c
    third_party/fatfs/ffunicode.c
```

2. Include dir: add `${CMAKE_CURRENT_SOURCE_DIR}/third_party/fatfs` to `target_include_directories` (FatFs headers are included bare as `"ff.h"`), and `${CMAKE_CURRENT_SOURCE_DIR}/usbhost` if the bare cross-file includes inside the usbmsc core don't resolve via same-directory quoting alone (they should â€” add only if the build proves otherwise).
3. Link libs: check WiliIR's top-level `CMakeLists.txt` link list vs wilibsp's â€” the usbmsc core needs the RP2350 USB controller registers (part of `pico_stdlib`/hardware structs) and `hardware_clocks`; add only components the build actually demands. Do NOT add any TinyUSB component (this driver replaces it).

`bsp/fw2.h`:

```c
#include "usbhost/usb_store.h"  // (harvested: USB thumb-drive mount manager, WiliIR/usbmsc)
```

- [ ] **Step 4: Build + test + commit**

Run: `python tools/fw.py build hello_display` â€” expected: clean.
Run: `python tools/fw.py test` â€” expected: all green (nothing host-compiled changed).

```bash
git add bsp/usbhost bsp/third_party/fatfs bsp/CMakeLists.txt bsp/fw2.h
git commit -m "feat(usbhost): harvest WiliIR polled USB host MSC stack + FatFs R0.15b vendoring"
```

---

### Task 5: apps/hello_usbdrive demo + on-hardware mount verification

**Files:**
- Create: `apps/hello_usbdrive/main.c`, `CMakeLists.txt`, `README.md`; register the app like Task 3 did.

**Interfaces:**
- Consumes: `usb_store_*` via `fw2.h`, FatFs `f_opendir/f_readdir`.

- [ ] **Step 1: Write the app**

`apps/hello_usbdrive/CMakeLists.txt`: mirror `hello_ir`'s, name `hello_usbdrive`.

`apps/hello_usbdrive/main.c`:

```c
// hello_usbdrive â€” on-hardware smoke test for the harvested USB host MSC
// stack (bsp/usbhost) + FatFs. RTT-only. Powers the HP1/HP2 ports, polls the
// host stack, and on every mount edge prints the volume root listing (the
// usb_store DIAGs) plus a recursive count of *.ir files in the top two
// levels as a FatFs read exercise.
// Pass criteria with a FAT32 stick seated: "mount OK" + root listing within
// a few seconds of boot; pull/replug -> "drive removed" then a clean
// remount. No stick: the power-gate DIAG appears, nothing else â€” no crash.
#include "fw2.h"
#include "platform/diag.h"
#include "pico/stdlib.h"
#include "ff.h"

static void count_ir_files(void) {
    DIR dir;
    FILINFO fi;
    unsigned n = 0;
    if (f_opendir(&dir, "0:/") != FR_OK) return;
    while (f_readdir(&dir, &fi) == FR_OK && fi.fname[0]) {
        size_t len = strlen(fi.fname);
        if (!(fi.fattrib & AM_DIR) && len > 3 &&
            !strcasecmp(fi.fname + len - 3, ".ir")) n++;
    }
    f_closedir(&dir);
    DIAG("hello_usbdrive: %u .ir file(s) at volume root\n", n);
}

int main(void) {
    board_init();
    DIAG("hello_usbdrive: up\n");
    usb_store_init();                 // ioexp_usb_pwr(true) + host stack init
    bool was_mounted = false;
    while (true) {
        usb_store_task();
        bool m = usb_store_mounted();
        if (m != was_mounted) {
            was_mounted = m;
            if (m) count_ir_files();
        }
        sleep_ms(2);
    }
}
```

(Add `#include <string.h>` / `<strings.h>` as the compiler demands for `strlen`/`strcasecmp`; arm-none-eabi newlib provides `strcasecmp` via `<strings.h>`.)

`apps/hello_usbdrive/README.md`: what it does, the three fw commands, the pass criteria.

- [ ] **Step 2: Build**

Run: `python tools/fw.py build hello_usbdrive` â€” expected: clean.

- [ ] **Step 3: Flash + RTT verify (hardware gate â€” stick is already seated)**

Run: `python tools/fw.py flash hello_usbdrive`, capture ~20 s of RTT.
Expected evidence (record verbatim in the task report): `ioexp: USB HP1(P0_0)+HP2(P1_4) -> 1`, `usb_store: [ USB] mount OK`, the root listing (Flipper-IRDB/, learned.ir, remotes/, wiliir.cfg, ...), and the `.ir file(s) at volume root` count (expect â‰¥2: Roku_Standalone.ir + learned.ir).

- [ ] **Step 4: Restore the board + commit**

Reflash the WiliIR firmware so the bench board goes back to its normal state:
`cd /c/~prj/Dropbox/vibeProjects/WiliIR && powershell -File tools/flash.ps1` (its build output is still current). Confirm via a short RTT window that WiliIR boots (selftest 10/10 line).

```bash
cd /c/~prj/Dropbox/vibeProjects/wilibsp
git add apps
git commit -m "feat(usbhost): hello_usbdrive demo - mount + root listing, bench-verified"
```

---

### Task 6: Docs â€” catalog, pinmap, driver docs, and the WiliIR back-reference

**Files:**
- Modify: `docs/hardware/catalog.md`, `docs/hardware/pinmap.md`
- Create: `docs/drivers/ir.md`, `docs/drivers/usbhost.md`
- Modify (in the WiliIR repo, separate commit): `WiliIR/docs/harvest.md`, `WiliIR/CLAUDE.md`

- [ ] **Step 1: catalog.md**

Move the IR row (currently in TODO: "IR TX/RX | TX=GPIO20, RX=GPIO24 | `sensorview` or a new harvest â€” owner repo not yet confirmed") to the DONE table: location `bsp/ir/`, harvested from `WiliIR` (which vendored `usbmsc` for USB and proved everything on hardware 2026-07-05/06), demo `apps/hello_ir`, note the pio2 SM0/SM1 + 2 DMA + polled/no-IRQ budget and the `ioexp_ir_pwr` gate.

Add a DONE row for the native-controller USB host MSC: location `bsp/usbhost/` + `bsp/third_party/fatfs/`, harvested from `WiliIR` (origin: the owner's `usbmsc` driver, vendored verbatim), demo `apps/hello_usbdrive`, note CH334F single-hub-tier support, polled/no-IRQ, `ioexp_usb_pwr` gate. Leave the separate Pico-PIO-USB (GPIO42/43) TODO row untouched â€” it is a different peripheral.

- [ ] **Step 2: pinmap.md**

Move/confirm GPIO20 (IR TX) and GPIO24 (IR RX) into the verified table, referencing the new `PIN_IR_TX`/`PIN_IR_RX` defines in `bsp/platform/board.h` and the PCAL6524 P2_0 power gate.

- [ ] **Step 3: driver docs**

`docs/drivers/ir.md`, following the existing `docs/drivers/*.md` pattern: what (capture + decode + encode + TX over unified Âµs timing arrays; protocol list NEC/NECext/Samsung32/RC5/RC5X/RC6/SIRC/15/20/RCA/Kaseikyo; Flipper `.ir` parse/write), how (the init â†’ start â†’ poll â†’ decode and encode â†’ send snippet â€” copy from `WiliIR/docs/harvest.md` Â§ Usage snippet), dependencies (pio2 SM0+SM1, 2 DMA channels, polled/no-IRQ, `ioexp_ir_pwr` â€” including the rail-settle caveat from harvest.md Â§ power gate), tests (the 11 host tests), provenance (WiliIR, hardware-verified there + `hello_ir` loopback here).

`docs/drivers/usbhost.md`, same pattern: what (from-scratch polled RP2350 full-speed host, Bulk-Only Transport MSC, single hub tier, hotplug + 3-tier recovery + mount retry; FatFs R0.15b on `msc_disk.c` diskio glue; `usb_store` mount manager, volume `0:`), how (init â†’ task-in-loop â†’ `usb_store_mounted()` snippet from hello_usbdrive), dependencies (native USB controller â€” mutually exclusive with TinyUSB device mode; `ioexp_usb_pwr`), caveats carried honestly (mount-retry path implemented but never exercised on the bench; FF_FS_NORTC fixed 2026-07-01 date stamps), provenance (owner's `usbmsc` via WiliIR).

- [ ] **Step 4: WiliIR back-reference (separate repo, separate commit)**

In `WiliIR/docs/harvest.md`: add a dated status header at the top â€” harvested 2026-07-06 to wilibsp `bsp/ir/` + `bsp/usbhost/` (branch/commit refs), `ir_resolve.{c,h}` added to the bsp/ir group beyond the original map, demos `hello_ir`/`hello_usbdrive` bench-verified. In `WiliIR/CLAUDE.md`: update the one orientation line that says harvest is pending. Run WiliIR's `powershell -File tools/test.ps1` (14/14 â€” nothing but docs changed) and commit in WiliIR: `docs: record completed BSP harvest into wilibsp`.

- [ ] **Step 5: wilibsp build + test + commit**

`python tools/fw.py test` and `python tools/fw.py build hello_ir` â€” green.

```bash
git add docs
git commit -m "docs: IR + USB-host catalog/pinmap/driver docs for the WiliIR harvest"
```

---

## Self-review notes

- harvest.md module map: every row covered (pure group T1, drivers T2, usbmsc+glue+FatFs T4, ioexp gates T2, board.h pins T2, tests T1, demo apps T3/T5, usage snippets â†’ driver docs T6). `ir_resolve` addition recorded in T6.
- add-driver skill steps 1-10: source repo confirmed (T-), verbatim copy (T1/2/4), CMake (T1/2/4), fw2.h (T2/4), proven names (constraint), catalog (T6), pinmap (T6), driver doc (T6), pure/host tests (T1), build via app (T3/5).
- Directory-name deviation from the skill's "match source dir" rule: `src/usb/` lands as `bsp/usbhost/` because harvest.md (the contract) says so; the two resulting include edits are explicitly enumerated in T4 Step 2.
- Hardware gates need no human: TXâ†’RX loopback (T3) and the seated stick (T5). Board restored to WiliIR firmware in T5 Step 4.

## Execution handoff

Plan complete. Execute on wilibsp branch `harvest-ir-usbhost`; Tasks 3 and 5 include flashing the bench board (probe attached).
