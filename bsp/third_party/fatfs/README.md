# FatFs R0.15b

Version: R0.15b (FFCONF_DEF 5385)

Vendored from `C:/~prj/Dropbox/vibeProjects/movieplayer/third_party/fatfs/`
(FatFs by ChaN, BSD-style license — see the header comment in `ff.h`).

Files: ff.c, ff.h, diskio.h, ffunicode.c, ffconf.h. The sample `diskio.c` was
NOT copied — the project-specific glue lives in `src/usb/msc_disk.c`.

ff.c carries one LOCAL PATCH (marked "LOCAL PATCH" in get_fileinfo): R0.15b
does not clear fno->fname at end-of-directory, violating the documented
f_readdir contract — the canonical `while (f_readdir()==FR_OK && fname[0])`
loop then spins forever on the last entry's stale name. The patch restores
the `fno->fname[0] = 0` invalidation that earlier releases performed.
Re-check when upgrading to R0.16+.

`ffconf.h` deltas vs upstream defaults, relevant to this repo:
- `FF_FS_READONLY 0` — write API enabled (Plan 3 saves files).
- `FF_USE_STRFUNC 1` — `f_gets()` used by `src/db/db_index.c`.
- `FF_FS_MINIMIZE 0` — full basic API.
- `FF_USE_LFN 1`, `FF_CODE_PAGE 437`, `FF_VOLUMES 2`, `FF_FS_NORTC 1` (no RTC;
  fixed timestamp).
