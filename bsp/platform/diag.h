// src/platform/diag.h — on-target diagnostics over SEGGER RTT (channel 0).
// FW2 has no UART and host-mode USB, so RTT via the debug probe is the only
// text channel. View with tools/rtt.ps1 (openocd RTT server).
// SEGGER_RTT_printf supports %d %u %x %s %c and field widths — NO floats.
#ifndef DIAG_H
#define DIAG_H
#include "SEGGER_RTT.h"

#define DIAG(...) SEGGER_RTT_printf(0, __VA_ARGS__)

#endif
