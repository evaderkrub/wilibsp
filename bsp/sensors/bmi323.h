#ifndef BMI323_H
#define BMI323_H
#include <stdint.h>
#include <stdbool.h>

typedef struct { float ax, ay, az; float gx, gy, gz; bool valid; } bmi323_reading_t;

// Pure conversions for the configured full-scale ranges.
float bmi323_accel_g(int16_t raw, int range_g);     // e.g. range_g = 4  -> ±4 g
float bmi323_gyro_dps(int16_t raw, int range_dps);  // e.g. range_dps = 500

bool bmi323_init(void);                  // soft-reset, enable accel+gyro, CHIP_ID check
bool bmi323_read(bmi323_reading_t *out); // burst-read 6 axes, convert
#endif
