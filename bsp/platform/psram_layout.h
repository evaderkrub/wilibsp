// src/platform/psram_layout.h — named PSRAM regions (APS6404L, 8 MB @ PSRAM_BASE).
// Single source of truth for who owns what in PSRAM. Byte offsets from PSRAM_BASE.
// Framebuffers occupy the first ~324 KB; the capture clip store starts at 1 MB
// with 4 MB of headroom — clear of the framebuffers and within the 8 MB device.
#ifndef PSRAM_LAYOUT_H
#define PSRAM_LAYOUT_H
#include <stddef.h>
#include <stdint.h>
#include "platform/psram.h"
#include "ui/screen_analyzer.h"   // ANALYZER_WF_W / ANALYZER_WF_H

#define PSRAM_WF_OFFSET      0u                                    // analyzer waterfall fb
#define PSRAM_MON_FB_OFFSET  ((size_t)ANALYZER_WF_W * ANALYZER_WF_H * 2u)  // monitor trace fb
#define PSRAM_CAPTURE_OFFSET 0x100000u                             // 1 MB: capture clip store
#define CAPTURE_MAX_DURS     0x100000u                             // 1,048,576 durations = 4 MB

#define PSRAM_CAPTURE_ADDR   ((uint32_t *)(PSRAM_BASE + PSRAM_CAPTURE_OFFSET))
#endif
