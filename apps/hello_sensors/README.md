# hello_sensors

On-hardware smoke test for the four I2C1 sensor drivers (`bsp/sensors/`).
RTT-only — no display.

    fw build hello_sensors
    fw flash hello_sensors
    fw rtt

All values print as scaled integers (DIAG has no floats): centi-degC,
centi-%RH, deci-lux, milli-g, centi-dps, deci-uT. Expected:

- All four init lines report ok (chip-ids: BMI323 0x43, BMM350 0x33).
- sht40: plausible room temp (~2000-3000 centi-C); rises breathed on.
- opt4001: drops toward 0 covered, rises under light.
- bmi323: gravity ~1000 milli-g on the down axis; reacts to tilt;
  gyro ~0 at rest.
- bmm350: mag_abs in the Earth-field range (~250-650 deci-uT); disturbed
  by nearby metal/magnets.
