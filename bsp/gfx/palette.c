#include "palette.h"

// High-contrast blue->orange waterfall ramp over [-100, -68] dBm (5 stops, 8 dB
// each), matched to this board's MEASURED range: noise floor ~-100 dBm, and real
// signals land in the -85..-70 dBm range on the bench. Blue is reserved for the
// floor (two blue stops keep quiet noise a calm blue); ANY real energy jumps to
// warm -- red-orange -> orange -> bright orange for the strongest. Blue/orange are
// complementary, so signals pop hard against the floor. Verified against a 433.92
// MHz YardStick One (measured floor=-100, ambient peak ~-80, low-power TX ~-73).
static const uint16_t STOPS[5] = { 0x0010, 0x001F, 0xFA00, 0xFC00, 0xFDE0 };

uint16_t inferno_rgb565(int rssi_dbm) {
    if (rssi_dbm <= -100) return STOPS[0];
    if (rssi_dbm >= -68)  return STOPS[4];
    int t = rssi_dbm + 100;        // 0..32
    int seg = t / 8;               // 0..3
    int f = t % 8;                 // 0..7 within segment
    uint16_t a = STOPS[seg], b = STOPS[seg + 1];
    int ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
    int br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
    int r = ar + (br - ar) * f / 8;
    int g = ag + (bg - ag) * f / 8;
    int bl = ab + (bb - ab) * f / 8;
    return (uint16_t)((r << 11) | (g << 5) | bl);
}
