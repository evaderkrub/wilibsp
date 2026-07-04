# I2C sensor drivers harvest — design

**Date:** 2026-07-04
**Increment:** 4 of 4 — the FINAL increment of the audio/RF/sensor driver harvest
(I2S ✅ → PDM mics ✅ → CC1101 ✅ → **I2C sensors**).
**Source repo:** local `sensorview` (`src/sensors/`) — hardware-proven on this exact FreeWili 2
board (the sensorview instrument app ran all four sensors live).
**Approach:** fully verbatim harvest, zero code edits (approved: approach A).

## Goal

Land the four I2C sensor drivers in `bsp/sensors/`, with an RTT demo app, host tests for every
pure conversion function, and the standard catalog/facts/driver-doc updates.

**Not in scope** (stays in the instrument repo — app domain, not BSP): `sensor_hub` (couples to
AHRS fusion, mag_cal, auto-backlight), the fusion/motion/field math, all UI.

## Hardware facts

All four sensors sit on **I2C1** (SDA=GPIO26 / SCL=GPIO27, 400 kHz), which `board_init()`
already brings up — **no new pins, no ioexp changes, no DMA/IRQ/PIO**. Every scarce-resource
invariant is untouched by this increment.

| Sensor | Part | Addr | Presence check | Quirks |
|---|---|---|---|---|
| Temp/humidity | Sensirion SHT40-AD1B | 0x44 | serial-number read | CRC-8 (poly 0x31) per 2-byte word; high-precision measure blocks ~10 ms |
| Ambient light | TI OPT4001 | 0x45 (ADDR high) | DEVID register | continuous 100 ms conversions; lux = (mantissa<<exponent) × 437.5e-6 (package-dependent factor — calibration caveat, see source comment) |
| IMU | Bosch BMI323 | 0x68 | CHIP_ID (0x43) | 16-bit LE registers; **I2C reads return 2 leading dummy bytes** (confirmed on HW); soft-reset in init; ±4 g / ±500 dps @ 100 Hz |
| Magnetometer | Bosch BMM350 | 0x14 | CHIP_ID (0x33) | **2 leading dummy bytes** too; init = soft reset → 11-word OTP download → magnetic/flux-guide resets → ODR/avg → normal mode (~130 ms of settles); readings compensated (Bosch OTP math) to µT + sqrtf magnitude |

## Guard convention (the one reconciliation)

The source files split pure logic from hardware halves with `#ifdef PICO_BUILD`. **PICO_BUILD=1
is defined automatically by the Pico SDK for all target builds** (verified:
`~/.pico-sdk/sdk/2.2.0/src/rp2350/pico_platform/CMakeLists.txt:11`) and is naturally absent in
the standalone host CTest tree. So the guards work with ZERO configuration: hardware halves
compile on target, pure functions only on host. This differs from the repo's existing
`#ifndef HOST_TEST` convention (ook_tx/scan_engine) — deliberately accepted to keep all 10
files byte-identical to hardware-proven source; document the two-convention situation in
facts.md.

## Files

All 10 copied **byte-identical** from `sensorview/src/sensors/` into `bsp/sensors/`:
`sht40.{c,h}`, `opt4001.{c,h}`, `bmi323.{c,h}`, `bmm350.{c,h}`, `bmm350_comp.{c,h}`
(`bmm350_comp.c` is 100% pure — no guards at all).

Wiring:
- `bsp/CMakeLists.txt`: add the 5 `.c` files to the library source list.
- `bsp/fw2.h`: `#include "sensors/{sht40,opt4001,bmi323,bmm350,bmm350_comp}.h"`.
- No board.h edits (addresses live in the drivers, as in source). No new link libraries
  (`hardware_i2c` already linked; sqrtf comes with the SDK's libm).

## Public API (unchanged from source)

- `bool sht40_init(void)` / `bool sht40_read(sht40_reading_t*)` → `{temp_c, rh_pct, valid}`
- `bool opt4001_init(void)` / `bool opt4001_read(float *lux)`
- `bool bmi323_init(void)` / `bool bmi323_read(bmi323_reading_t*)` → `{ax..az g, gx..gz dps}`
- `bool bmm350_init(void)` / `bool bmm350_read(bmm350_reading_t*)` → `{mx,my,mz µT, magnitude, temp_c}`
- Pure (host-tested): `sht40_convert`, `opt4001_lux`, `bmi323_accel_g`, `bmi323_gyro_dps`,
  `bmm350_sign24`, `bmm350_parse_otp`, `bmm350_compensate`

Error handling as in source: `*_init()` returns presence (probe/chip-id) and DIAGs the result;
`*_read()` returns false on NAK/short read (SHT40 also on CRC failure) — callers poll and skip
invalid readings. All calls are blocking I2C; single-threaded polled use (any one core).

## Demo app: `apps/hello_sensors`

RTT-only (like hello_mics). `board_init()` → the four `*_init()` calls (each DIAGs ok/NAK; app
continues with whatever is present) → 2 Hz loop printing a compact dashboard, floats scaled to
integers for DIAG (no %f):

- SHT40: `temp` and `RH` in centi-units (`23.47 C` → `2347`)
- OPT4001: lux ×10 (deci-lux; avoids overflow at bright light where m-lux would not)
- BMI323: accel in milli-g, gyro in centi-dps
- BMM350: mag in deci-µT per axis + magnitude

**On-hardware pass criteria:** all four init lines report ok with correct chip-ids; SHT40 temp
plausible (~20-30 °C indoors) and rises when breathed on; OPT4001 lux drops when covered /
rises under light; BMI323 gravity vector ≈ 1000 milli-g on the down axis, reacts to tilt, gyro
≈ 0 at rest; BMM350 magnitude in the Earth-field range (~250-650 deci-µT) and disturbed by
nearby metal/magnet. Unattended verification covers presence/chip-ids, plausible ambient
values, and the gravity vector; the stimulus tests need a human.

## Host tests (`tests/`)

Four new test files, each compiling the source `.c` directly (no defines needed — PICO_BUILD
absent on host, pure functions only):

- `test_sht40.c` — CRC-8 vectors (known-good frame passes, corrupted byte fails), conversion
  formula spot values (0x0000/0xFFFF ticks → -45/+130 °C ends), RH clamping to 0..100.
- `test_opt4001.c` — `opt4001_lux`: mantissa/exponent identities (0→0, exponent shift doubles,
  known datasheet-style value).
- `test_bmi323.c` — `bmi323_accel_g` / `bmi323_gyro_dps` at 0, +full-scale (32767→~range),
  -full-scale (-32768→-range).
- `test_bmm350_comp.c` — `bmm350_sign24` boundaries (0x7FFFFF→+, 0x800000→-2^23);
  `bmm350_parse_otp` on a synthetic word set with hand-computed expected coefficients;
  `bmm350_compensate` with zeroed coefficients ≈ pure LSB scaling (sanity of the pipeline).

Float assertions use an epsilon helper: add one `ASSERT_NEAR(actual, expected, eps)` macro to
the shared `tests/test_util.h`, styled after the existing `ASSERT_EQ`.

## Docs

- `docs/hardware/catalog.md`: the four sensor rows move TODO → DONE (peripheral count 7 → 11);
  compiled-sources prose gains `sensors/*.c`.
- `docs/hardware/facts.md`: PICO_BUILD guard convention (and why it coexists with HOST_TEST);
  BMI323/BMM350 2-dummy-byte I2C quirk; OPT4001 lux-factor calibration caveat; BMM350 init
  timing (~130 ms of settles).
- New `docs/drivers/sensors.md`: API, addresses, quirks, demo pointer.
- `docs/hardware/pinmap.md`: update the I2C1-peripherals address table (addresses now confirmed
  from driver code, including BMI323 0x68 / BMM350 0x14 that FwDisplayVibe.md lacked).
- `docs/superpowers/findings/`: hardware-verification findings doc after the on-target run.

## Verification plan

1. `fw test` — host CTest including the 4 new tests (14 total).
2. `fw build hello_sensors` + `fw build hello_display` (regression).
3. Flash + `fw rtt`: unattended criteria (init/chip-ids, ambient plausibility, gravity vector);
   human stimulus tests recorded as pending if unattended.
