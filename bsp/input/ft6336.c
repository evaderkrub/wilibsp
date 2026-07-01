// src/input/ft6336.c — FT6336U over I2C1, polled. No INT; RST shares the panel HW reset.
// Mirrors the proven freewili reference driver (rmpLib/rpCAPTouchFT6336): a single
// 6-byte read of regs 0x02..0x07 so TD_STATUS and the point latch are coherent.
// Coordinate mapping is delegated to ft6336_map_point (host-tested pure logic).
#include "input/ft6336.h"
#include "input/ft6336_map.h"
#include "hardware/i2c.h"
#include "platform/diag.h"

#define FT6336_I2C             i2c1
#define FT6336_ADDR            0x38
#define FT6336_REG_DEVICE_MODE 0x00   // 0 = normal operating mode
#define FT6336_REG_TD_STATUS   0x02   // low nibble = number of touch points
#define FT6336_REG_CHIP_ID     0xA3   // chip-id register (presence check)

// Write the register pointer (no stop), then read n bytes.
static bool ft_rd(uint8_t reg, uint8_t* buf, uint8_t n) {
    if (i2c_write_blocking(FT6336_I2C, FT6336_ADDR, &reg, 1, true) != 1) return false;
    return i2c_read_blocking(FT6336_I2C, FT6336_ADDR, buf, n, false) == (int)n;
}

// Write one register = val.
static bool ft_wr(uint8_t reg, uint8_t val) {
    uint8_t b[2] = { reg, val };
    return i2c_write_blocking(FT6336_I2C, FT6336_ADDR, b, 2, false) == 2;
}

bool ft6336_init(void) {
    uint8_t id = 0;
    bool ok = ft_rd(FT6336_REG_CHIP_ID, &id, 1);
    (void)ft_wr(FT6336_REG_DEVICE_MODE, 0x00); // non-fatal: mode defaults to normal; don't gate ok on this
    DIAG("ft6336: init %s id=0x%02X\n", ok ? "ok" : "NO-ACK", (unsigned)id);
    return ok;
}

bool ft6336_poll(uint16_t* x, uint16_t* y) {
    // One transaction over regs 0x02..0x07 so TD_STATUS and the point latch are coherent.
    uint8_t b[6];
    if (!ft_rd(FT6336_REG_TD_STATUS, b, 6)) return false;
    if ((b[0] & 0x0F) != 1) return false;            // require exactly one touch point
    // b[1]=P1_XH b[2]=P1_XL (chip X) ; b[3]=P1_YH b[4]=P1_YL (chip Y).
    int chip_x = (((int)(b[1] & 0x0F)) << 8) | b[2];
    int chip_y = (((int)(b[3] & 0x0F)) << 8) | b[4];
    ft_point_t p = ft6336_map_point(chip_x, chip_y);
    *x = p.x;
    *y = p.y;
    return true;
}
