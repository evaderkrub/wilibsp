#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "usb_hcd.h"
#include "usb_parse.h"

typedef struct {
    uint8_t        addr;       // assigned device address
    uint8_t        ep0_mps;
    uint16_t       vid, pid;
    usb_cfg_info_t cfg;
} usb_device_t;

// Standard requests (all blocking, on the device's EP0)
hcd_result_t core_get_descriptor(uint8_t addr, uint8_t type, uint8_t index,
                                 void *buf, uint16_t len, uint16_t *actual);
hcd_result_t core_set_address(uint8_t new_addr);          // sent to address 0
hcd_result_t core_set_configuration(uint8_t addr, uint8_t config_value);
hcd_result_t core_clear_endpoint_halt(uint8_t addr, uint8_t ep_addr);

// Full enumeration of the device currently in default state (just reset,
// answering on address 0): reads descriptors, assigns `addr_to_assign`,
// parses config, sets configuration. Fills *dev.
hcd_result_t core_enumerate(uint8_t addr_to_assign, usb_device_t *dev);
