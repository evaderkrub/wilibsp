#ifndef BMM350_H
#define BMM350_H
#include <stdint.h>
#include <stdbool.h>
typedef struct { float mx, my, mz, magnitude, temp_c; bool valid; } bmm350_reading_t;
bool bmm350_init(void);                  // validated init incl. OTP download + parse
bool bmm350_read(bmm350_reading_t *out); // 12-byte burst, sign-correct, compensate, magnitude
#endif
