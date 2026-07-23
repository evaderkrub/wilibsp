# uartkbd charger telemetry + hello_charger test app

**Date:** 2026-07-22
**Repo:** wilibsp (all work here; the parser does not exist in wilikeyboard)
**Protocol authority:** `C:\~prj\Dropbox\FreeWilli\vibe\Wilikeyboard.md` (updated with bytes 10–21)

## Goal

The FW2 keyboard coprocessor's 23-byte status frame carries charger telemetry
in bytes 10–21, which `uartkbd_parse` currently ignores. Decode it, expose it
through the driver, and add a BSP test screen to view it live.

## Frame fields (bytes 10–21)

| Byte | Field | Scaling |
| ---- | ----- | ------- |
| 10 | Charger VBUS | mV = 2600 + code·100 (2600–15300) |
| 11 | Charger VSys | mV = 2304 + code·20 (2304–4844) |
| 12 | Charger VBatt | mV = 2304 + code·20 (2304–4844) |
| 13 | Charger Current | mA = code·50 (0–6350) |
| 14 | Temp Sensor | tspct = code·465/100 + 210 (210–1395, tenths of %) |
| 15 | Charger Status | 0 NotCharging, 1 PreCharge, 2 FastCharge, 3 Done |
| 16 | VBus Status | 0 NoInput, 1 USB_HOST, 2 ADAPTER, 7 OTG |
| 17 | Charge Fault | 0 Normal, 1 Input, 2 Thermal, 3 TimerExpired |
| 18 | Temp Rank | 0 Normal, 2 Warm, 3 Cool, 5 Cold, 6 Hot |
| 19 | Bitfield | b4:3 CC tier (0 None, 1 500mA, 2 1A5, 3 3A), b2 vsys_reg, b1 thermal_reg, b0 vbus_attached |
| 20 | USB CC1 Voltage | mV = code·8 |
| 21 | USB CC2 Voltage | mV = code·8 |

Bytes 6–9 remain reserved (not captured).

tspct → °C (x = tspct/1000): R_low = 5240·x/(1−x); R_ntc = 1/(1/R_low −
1/30000); T = 1/(1/298.15 + ln(R_ntc/10000)/3435) − 273.15.

## Design

### 1. Parser — `bsp/input/uartkbd_parse.*` (pure, host-testable)

- `uartkbd_parser_t` gains `uint8_t charger_raw[12]` and `bool charger_valid`.
  `accept_frame()` memcpys frame bytes 10–21 into `charger_raw` and sets
  `charger_valid` on every checksum-valid frame. **No scaling at frame time**
  — raw capture only (owner requirement).
- New type `uartkbd_charger_t`:
  - `uint16_t vbus_mv, vsys_mv, vbatt_mv, current_ma, temp_tspct`
  - `uint8_t charge_status` (`UARTKBD_CHG_NOT_CHARGING/PRECHARGE/FASTCHARGE/DONE`)
  - `uint8_t vbus_status` (`UARTKBD_VBUS_NONE/USB_HOST/ADAPTER/OTG`)
  - `uint8_t fault` (`UARTKBD_FAULT_NORMAL/INPUT/THERMAL/TIMER`)
  - `uint8_t temp_rank` (`UARTKBD_RANK_NORMAL/WARM/COOL/COLD/HOT`)
  - `uint8_t cc_tier` (`UARTKBD_CC_NONE/500MA/1A5/3A`)
  - `bool vsys_regulation, thermal_regulation, vbus_attached`
  - `uint16_t cc1_mv, cc2_mv`
  - Enum fields hold the raw code verbatim, so undocumented codes (e.g. vbus
    status 3–6) pass through rather than being clamped.
- `bool uartkbd_parse_charger(const uartkbd_parser_t *p, uartkbd_charger_t *out)`
  — applies all scaling **on demand**; returns false (out untouched) until the
  first valid frame.
- `float uartkbd_charger_temp_c(uint16_t tspct)` — the NTC math above, using
  `logf`. Separate function so only callers that want °C link libm.

### 2. Binding — `bsp/input/uartkbd.*`

One passthrough: `bool uartkbd_charger(uartkbd_charger_t *out)`.

### 3. Tests — `tests/test_uartkbd_parse.c`

- Scaling at range endpoints and a midpoint per analog field (codes 0, 255/max,
  one interior value → expected mV/mA/tspct).
- Enum + bitfield decode, including an undocumented vbus-status code passing
  through verbatim.
- `uartkbd_parse_charger` returns false before any valid frame.
- °C spot-check against a hand-computed value, tolerance ±0.5 °C.

### 4. Test app — `apps/hello_charger`

Structure follows `hello_keyboard`: PSRAM framebuffer, `st7796_flush_async`,
~2 ms poll loop, `font5x7`. Static labels drawn once; values redrawn only when
a new frame changes them. Shows:

- Voltages/current: VBUS, VSys, VBatt (mV), current (mA)
- Temp: tspct and computed °C
- Enums as text: charge status, vbus status, fault, temp rank, CC tier
- Bitfield flags: vsys_reg / thermal_reg / vbus_attached
- CC1/CC2 mV
- Link stats (frames/errors) and live button/detect bitmap — doubles as a
  whole-frame smoke screen

No touch, no fw2kb.

### 5. Docs

Add a charger-API section to `docs/drivers/keyboard.md`.

## Out of scope

- No charger-change events (snapshot polling only; frames arrive ~2 Hz).
- No coprocessor TX (link stays RX-only).
- Bytes 6–9 stay unparsed.
