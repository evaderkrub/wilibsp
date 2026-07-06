#pragma once
#include "usb_core.h"

// Single-port hub passthrough (implemented in Task 10).
hcd_result_t usb_hub_attach(const usb_device_t *hub);
hcd_result_t usb_hub_wait_drive(usb_device_t *drive, uint32_t timeout_ms);
bool         usb_hub_drive_present(void);
void         usb_hub_detach(void);
