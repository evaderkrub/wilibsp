// bsp/fw2.h — umbrella include for the FreeWili2 BSP public API.
// Include this from an app to pull in the board + drivers.
// Naming: harvested drivers keep their proven names (st7796_*, ft6336_*,
// ws2812_*, board_*). See AGENTS.md.
#ifndef FW2_H
#define FW2_H

#include "platform/board.h"   // (Task 3)
#include "display/st7796.h" // (Task 4)
#include "input/ft6336.h"   // (Task 5)
#include "leds/ws2812_driver.h" // (Task 6)
#include "audio/tone_gen.h"          // (Task 1)
#include "audio/vu_meter.h"          // (Task 1)
#include "audio/codec_nau88c10.h"    // (Task 2)
#include "audio/audio_i2s_duplex.h"  // (Task 2)
#include "audio/audio_capture.h"     // (Task 3)

#endif // FW2_H
