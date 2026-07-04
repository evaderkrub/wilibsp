// hello_sensors — on-hardware smoke test for the four I2C1 sensors.
// RTT-only (fw rtt), no display. Each *_init() DIAGs its presence/chip-id;
// the loop prints one line per present sensor at ~2 Hz. DIAG has no float
// support, so values print as scaled integers:
//   sht40: centi-degC / centi-%RH      opt4001: deci-lux
//   bmi323: milli-g / centi-dps        bmm350: deci-uT (+ magnitude)
// Pass criteria in README.md.
#include "fw2.h"
#include "platform/diag.h"
#include "pico/stdlib.h"

int main(void) {
    board_init();   // 250 MHz + clk_peri re-source + I2C1 @ 400 kHz + ioexp
    DIAG("\n=== hello_sensors: I2C sensor smoke test ===\n");

    bool have_sht = sht40_init();
    bool have_opt = opt4001_init();
    bool have_imu = bmi323_init();
    bool have_mag = bmm350_init();
    DIAG("present: sht40=%d opt4001=%d bmi323=%d bmm350=%d\n",
         have_sht, have_opt, have_imu, have_mag);

    while (1) {
        if (have_sht) {
            sht40_reading_t s;
            if (sht40_read(&s))
                DIAG("sht40:   temp=%d centi-C  rh=%d centi-pct\n",
                     (int)(s.temp_c * 100.0f), (int)(s.rh_pct * 100.0f));
            else DIAG("sht40:   read FAIL\n");
        }
        if (have_opt) {
            float lux;
            if (opt4001_read(&lux))
                DIAG("opt4001: lux=%d deci-lux\n", (int)(lux * 10.0f));
            else DIAG("opt4001: read FAIL\n");
        }
        if (have_imu) {
            bmi323_reading_t m;
            if (bmi323_read(&m))
                DIAG("bmi323:  acc=%d,%d,%d milli-g  gyr=%d,%d,%d centi-dps\n",
                     (int)(m.ax * 1000.0f), (int)(m.ay * 1000.0f), (int)(m.az * 1000.0f),
                     (int)(m.gx * 100.0f), (int)(m.gy * 100.0f), (int)(m.gz * 100.0f));
            else DIAG("bmi323:  read FAIL\n");
        }
        if (have_mag) {
            bmm350_reading_t f;
            if (bmm350_read(&f))
                DIAG("bmm350:  mag=%d,%d,%d deci-uT  mag_abs=%d deci-uT  temp=%d centi-C\n",
                     (int)(f.mx * 10.0f), (int)(f.my * 10.0f), (int)(f.mz * 10.0f),
                     (int)(f.magnitude * 10.0f), (int)(f.temp_c * 100.0f));
            else DIAG("bmm350:  read FAIL\n");
        }
        DIAG("---\n");
        sleep_ms(500);
    }
}
