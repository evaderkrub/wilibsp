// bsp/display/hstx_modes.h — shared HSTX 640x480p60 mode constants.
//
// Harvested from ../movieplayer/src/display/hstx_modes.h, trimmed to the plain-DVI
// scanout constants (the HDMI data-island preamble/guard/width macros were dropped
// with the island path). No values changed.
#ifndef HSTX_MODES_H
#define HSTX_MODES_H

// ----------------------------------------------------------------------------
// DVI/HDMI control symbols (10-bit TMDS control characters, one per lane).
#define TMDS_CTRL_00 0x354u
#define TMDS_CTRL_01 0x0abu
#define TMDS_CTRL_10 0x154u
#define TMDS_CTRL_11 0x2abu

// Per-line sync words (lane0 = ctrl, lanes1&2 = blanking). V/H = vsync/hsync active.
#define SYNC_V0_H0 (TMDS_CTRL_00 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20))
#define SYNC_V0_H1 (TMDS_CTRL_01 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20))
#define SYNC_V1_H0 (TMDS_CTRL_10 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20))
#define SYNC_V1_H1 (TMDS_CTRL_11 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20))

// ----------------------------------------------------------------------------
// 640x480p60 VESA timing.
#define MODE_H_FRONT_PORCH   16
#define MODE_H_SYNC_WIDTH    96
#define MODE_H_BACK_PORCH    48
#define MODE_H_ACTIVE_PIXELS 640
#define MODE_V_FRONT_PORCH   10
#define MODE_V_SYNC_WIDTH    2
#define MODE_V_BACK_PORCH    33
#define MODE_V_ACTIVE_LINES  480

// ----------------------------------------------------------------------------
// HSTX command-word opcodes (top nibble of a command dword).
#define HSTX_CMD_RAW         (0x0u << 12)
#define HSTX_CMD_RAW_REPEAT  (0x1u << 12)
#define HSTX_CMD_TMDS        (0x2u << 12)
#define HSTX_CMD_TMDS_REPEAT (0x3u << 12)

#endif // HSTX_MODES_H
