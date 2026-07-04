# I2C sensor drivers (`bsp/sensors/`)

Four polled sensors on I2C1 (SDA=26/SCL=27, 400 kHz — brought up by
`board_init()`). All drivers are harvested verbatim from `sensorview` and
share the same shape: `bool <part>_init(void)` probes/configures and DIAGs
the result; `bool <part>_read(...)` returns false on NAK/short read. No
DMA, IRQs, or PIO — safe to call from any single core.

| Part | Addr | Init | Read output |
|---|---|---|---|
| SHT40 (`sht40.h`) | 0x44 | serial-number probe | `sht40_reading_t{temp_c, rh_pct, valid}` — CRC-checked, ~10 ms blocking |
| OPT4001 (`opt4001.h`) | 0x45 | continuous mode + DEVID | `float lux` (437.5e-6 factor — package-dependent, calibrate for absolute accuracy) |
| BMI323 (`bmi323.h`) | 0x68 | soft-reset, chip-id 0x43, ±4 g/±500 dps @100 Hz | `bmi323_reading_t{ax..az g, gx..gz dps, valid}` |
| BMM350 (`bmm350.h`) | 0x14 | chip-id 0x33, OTP download + compensation setup (~130 ms) | `bmm350_reading_t{mx,my,mz µT, magnitude, temp_c, valid}` |

Pure helpers (host-tested in `tests/`): `sht40_convert`, `opt4001_lux`,
`bmi323_accel_g`/`bmi323_gyro_dps`, and the fully-pure `bmm350_comp.h`
(`bmm350_sign24`, `bmm350_parse_otp`, `bmm350_compensate`).

Quirks (see facts.md): BMI323/BMM350 reads carry 2 leading dummy bytes;
`#ifdef PICO_BUILD` guards are SDK-defined (no config needed).

Demo: `apps/hello_sensors` (RTT dashboard, scaled-integer output).
