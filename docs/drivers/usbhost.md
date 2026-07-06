# USB host MSC (`bsp/usbhost/`, `bsp/third_party/fatfs/`)

From-scratch polled RP2350 full-speed USB host: Bulk-Only Transport (BOT)
Mass Storage Class over a single hub tier (the on-board CH334F), hotplug
detect + 3-tier error recovery, and mount retry — with FatFs R0.15b sitting
on top via a thin diskio glue layer, and a small mount-manager wrapper
(`usb_store`) that owns port power and the mount/unmount lifecycle. Harvested
from `WiliIR` (`src/usb/*`), whose own origin is the owner's `usbmsc` driver
(vendored verbatim into WiliIR, then carried here unmodified). Hardware-proven
on WiliIR 2026-07-05/06 and again here via `apps/hello_usbdrive`.

## What

- **`usb_core`/`usb_hcd`** — the native RP2350 host-mode driver: bus reset,
  device enumeration (`core_enumerate`), control transfers, endpoint halt
  clearing. Uses the SIE directly, no TinyUSB.
- **`usb_hub`** — single-tier hub passthrough only: `usb_hub_attach()` finds a
  drive behind the CH334F, resets its port, and enumerates the drive at
  address 2; `usb_hub_wait_drive()` / `usb_hub_drive_present()` poll port
  status change bits for attach/detach. A second hub tier (a hub plugged into
  the CH334F) is out of scope — not implemented, not tested.
- **`usb_msc`** — BOT transport + the hotplug/enumeration state machine
  (`ST_DISCONNECTED` → `ST_READY` / `ST_FAILED`), SCSI bring-up (INQUIRY +
  READ CAPACITY), block read/write. **3-tier recovery:** tier 1 is per-command
  retry-once on a stalled CSW; tier 2 is `bot_reset_recovery()` (Bulk-Only
  Mass Storage Reset + clear both endpoint halts) on a transport-level
  failure; tier 3 is `enter_failed()` → full `teardown()` and re-enumerate
  from scratch on a 2 s cadence while the device stays physically attached.
- **`usb_parse`** — descriptor parsing (config/interface/endpoint) feeding
  `usb_device_t` during enumeration.
- **`msc_disk.c`** — FatFs `diskio` glue over `usb_msc_read/write`, 512-byte
  sectors, pdrv 0. No RTC on this board: `FF_FS_NORTC 1`, so every file gets
  a fixed 2026-07-01 date stamp (`get_fattime()` is dead code under this
  config) — cosmetic only, does not affect mount/read/write correctness.
- **`usb_store`** — the app-facing mount manager: `usb_store_init()` gates
  port power (`ioexp_usb_pwr(true)`) and starts the host stack;
  `usb_store_task()` mounts on the ready-edge, unmounts on the not-ready edge,
  and retries a failed mount every 2 s while the drive stays ready but
  unmounted (`try_mount()` / `s_retry_at` in `usb_store.c`) so a slow-to-settle
  drive isn't wedged until replug. Volume root is `"0:/"` while
  `usb_store_mounted()`.
- **FatFs R0.15b** (`bsp/third_party/fatfs/`) — vendored unmodified;
  `FF_VOLUMES 2`, `FF_USE_LFN 1`, `FF_CODE_PAGE 437` per its `README.md`.

## How (init → task-in-loop → mounted check)

```c
#include "fw2.h"
#include "ff.h"

usb_store_init();                     // ioexp_usb_pwr(true) + usb_msc_init()
bool was_mounted = false;
while (true) {
    usb_store_task();                 // call every main-loop iteration
    bool mounted = usb_store_mounted();
    if (mounted && !was_mounted) {
        DIR dir; FILINFO fi;
        f_opendir(&dir, "0:/");
        while (f_readdir(&dir, &fi) == FR_OK && fi.fname[0]) {
            /* fi.fname, fi.fattrib & AM_DIR */
        }
        f_closedir(&dir);
    }
    was_mounted = mounted;
    sleep_ms(2);
}
```

(Same init→task→mounted-check shape as `apps/hello_usbdrive/main.c`.)

## Dependencies

- **Native RP2350 USB controller in host mode** — mutually exclusive with
  TinyUSB device mode; nothing here goes through TinyUSB.
- **Polled, no IRQs** — `usb_msc_task()` (and everything it calls into,
  `usb_hcd`/`usb_hub`) is driven entirely from the main-loop call, matching
  the IR driver's polling convention. No USB interrupts are wired up.
- **`ioexp_usb_pwr()`** (PCAL6524 P0 bit 0 = HP1, P1 bit 4 = HP2, both
  active-high, off at power-on) — called from `usb_store_init()`; without it
  both USB-A ports are unpowered and no device will enumerate on either.

## Caveats (carried honestly)

- **Mount-retry path implemented but never exercised on any bench session.**
  Every hardware session to date — WiliIR's 2026-07-05 evening session, its
  2026-07-06 Plan 3 session, and this repo's `apps/hello_usbdrive` run — had
  the drive mount successfully on the very first `try_mount()` call, so the
  `s_retry_at` / 2 s-retry-while-ready-but-unmounted branch in `usb_store.c`
  has zero hardware evidence either way. Needs a stick that fails its first
  mount attempt (e.g. a drive that's still spinning up) to exercise.
- **Remove/remount (hotplug) is carried from WiliIR, not re-exercised here.**
  This repo's `apps/hello_usbdrive` session kept the stick seated throughout
  (mount-edge only, per its own task report) and did not unplug/replug. The
  remove → `drive removed, unmounted` → clean remount path *is*
  hardware-verified, but on WiliIR's board, twice, on the same
  byte-identical `usb_store.c`/`usb_msc.c`/`usb_hub.c` now vendored here: the
  2026-07-05 evening session logged a clean hotplug mount, and the
  2026-07-06 Plan 3 session logged `usb_store: drive removed, unmounted` on
  pull followed by a clean remount with full root listing on replug — see
  `WiliIR/docs/hardware-notes.md`. Given the harvest is a verbatim copy, this
  is taken as strong (not direct-in-this-repo) evidence, not re-verified via
  a wilibsp demo app.
- **`list_root()`'s display cap is 10 entries.** `usb_store.c`'s
  `list_root()` prints at most 10 directory entries per mount
  (`shown < 10` in its `f_readdir` loop) — a real FAT32 root with more than
  10 entries will mount and browse correctly (FatFs itself has no such
  limit) but only the first 10 names show up in the RTT DIAG log; this is a
  logging convenience, not a functional cap on the mount/read path.
- **`FF_FS_NORTC 1`** — no on-board RTC, so every file FatFs writes gets the
  fixed 2026-07-01 timestamp baked into `msc_disk.c`'s `get_fattime()`-alternative
  path; harmless for this board's use cases (no code depends on file
  mtimes), but worth knowing before trusting a directory listing's dates.

## Tests

No host-testable pure logic lives in this group — every file here touches
either the USB SIE, FatFs, or the I/O expander, so its verification is
exclusively hardware (`apps/hello_usbdrive`), not CTest. (Contrast
`bsp/ir/`, whose `.ir` parser and directory-sort logic are pure and
host-tested — those pieces travel with the IR harvest, not this one.)

## Provenance / hardware verification

- **Carried from WiliIR (verified there 2026-07-05/06):** full enumeration →
  mount → root listing, hotplug remove/remount (twice, per above), real
  `.ir` file reads through the DB Browser, and zero `ir_capture` overruns
  observed during enumeration and large-directory rebuilds — see
  `WiliIR/docs/hardware-notes.md`.
- **Verified in wilibsp this session (`apps/hello_usbdrive`, bench-verified):**
  `ioexp: USB HP1(P0_0)+HP2(P1_4) -> 1` port-power gate, `usb_store: [ USB]
  mount OK`, a full 10-entry root listing (Flipper-IRDB/, remotes/,
  carts/, songs/, playlists/, learned.ir, Roku_Standalone.ir, wiliir.cfg,
  System Volume Information/, mrbrightside.fwmv), and a correct `.ir` file
  count (2) via a real FatFs directory walk.
- **Not re-exercised this session:** USB unplug/replug and the mount-retry
  path — see Caveats above.
