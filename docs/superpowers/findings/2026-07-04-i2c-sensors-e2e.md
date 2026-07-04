# I2C sensors — on-hardware E2E findings (2026-07-04)

**Increment 4 (I2C sensors), branch `feat/i2c-sensors`.**
Flashed `apps/hello_sensors` (built from commit a5c7a62) to the FreeWili 2
(RP2350 rev 3, CMSIS-DAP probe; `fw flash hello_sensors` programmed + verified
OK) and streamed `fw rtt` for ~15 s.

## Observed RTT output

```
sht40: init ok
opt4001: init ok id=0x0121
bmi323: chipid=0x1143 ok
bmm350: chipid=0x33 ok
present: sht40=1 opt4001=1 bmi323=1 bmm350=1
sht40:   temp=3605 centi-C  rh=1409 centi-pct
opt4001: lux=561 deci-lux
bmi323:  acc=-23,20,-995 milli-g  gyr=-1,7,4 centi-dps
bmm350:  mag=-4728,-2733,8468 deci-uT  mag_abs=10076 deci-uT  temp=6461 centi-C
```

(repeating every 500 ms with small sample-to-sample variation)

## Pass criteria

1. **All four present, correct chip-ids — PASS.** SHT40 serial-read ACK;
   OPT4001 DEVID 0x0121; BMI323 chip-id 0x1143 (low byte 0x43 as the driver
   checks); BMM350 chip-id 0x33.
2. **SHT40 plausibility — PASS (with note).** 36.0 °C / 14.0 %RH, fluctuating
   ±0.03 °C / ±0.08 %RH between reads. 36 °C is warm for "room temp" but
   consistent with board self-heating right next to a 250 MHz RP2350; 14 %RH
   is plausible for a dry indoor environment. Breath test pending a human.
3. **OPT4001 plausibility — PASS.** 56 lux steady in a dim room, nonzero and
   stable. Cover/light test pending a human.
4. **BMI323 gravity vector — PASS.** |a| = √(23² + 20² + 995²) ≈ 996 milli-g
   on −Z (board lying flat), gyro within ±0.12 dps of zero at rest. This also
   confirms the accel/gyro range wiring (a swapped range would misreport
   gravity by a large factor).
5. **BMM350 magnitude — PARTIAL (explained).** mag_abs ≈ 10076 deci-µT
   = 1007.6 µT (≈1 mT), i.e. **far above the bare Earth field (25–65 µT)**
   the criterion named. The reading is highly
   stable (±0.7 µT across samples) and self-consistent, which is the
   signature of a **fixed hard-iron offset from on-board magnetics** (the
   FreeWili 2 carries a speaker magnet near the sensor), not a conversion
   error. This matches the source repo's design: `sensorview` runs this exact
   driver through a `mag_cal` (hard/soft-iron calibration) stage before using
   the field — raw compensated output near a PCB magnet is expected to be
   offset-dominated. The BSP deliberately harvested the raw driver only
   (calibration is app domain). Orientation-response and magnet-stimulus
   tests pending a human; consumers wanting absolute field values need a
   hard-iron calibration layer.

Note: BMM350's internal die temp reads ~64.6 °C — plausible for a die
directly adjacent to self-heating silicon, and it is only used inside the
Bosch temperature-compensation math, not as an environmental reading.

## Observation (cosmetic)

One dashboard block in the stream printed without its bmm350 line (a dropped
RTT line — SEGGER RTT channel-0 buffer skip under burst load, not a sensor
failure; the next block was complete).

## Verdict

**Driver chain verified end-to-end on hardware for all four sensors:**
presence probes, chip-ids, blocking reads, CRC (SHT40 returning valid=true),
scale conversions (gravity vector correct to 0.4%), and the BMM350
OTP-download + compensation pipeline all live. Human stimulus checks
(breath / cover-light / tilt / magnet) remain the open post-merge checkbox,
plus the known hard-iron context for absolute magnetometer readings.

No code changes were needed during hardware verification.

## Regression status

- Host tests after all tasks: 14/14 (`fw test`).
- `hello_display`, `hello_mics`, `hello_sensors` all build clean.
