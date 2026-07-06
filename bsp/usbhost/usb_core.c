#include "usb_core.h"
#include <string.h>
#include "pico/stdlib.h"

enum { DT_DEVICE = 1, DT_CONFIG = 2 };

hcd_result_t core_get_descriptor(uint8_t addr, uint8_t type, uint8_t index,
                                 void *buf, uint16_t len, uint16_t *actual) {
    const uint8_t setup[8] = {0x80, 6, index, type,
                              0, 0, (uint8_t)len, (uint8_t)(len >> 8)};
    uint16_t n = len;
    hcd_result_t r = hcd_control_xfer(addr, setup, buf, &n);
    if (actual) *actual = n;
    return r;
}

hcd_result_t core_set_address(uint8_t new_addr) {
    const uint8_t setup[8] = {0x00, 5, new_addr, 0, 0, 0, 0, 0};
    hcd_result_t r = hcd_control_xfer(0, setup, NULL, NULL);
    sleep_ms(10);     // SET_ADDRESS settle time (spec: 2 ms)
    return r;
}

hcd_result_t core_set_configuration(uint8_t addr, uint8_t config_value) {
    const uint8_t setup[8] = {0x00, 9, config_value, 0, 0, 0, 0, 0};
    return hcd_control_xfer(addr, setup, NULL, NULL);
}

hcd_result_t core_clear_endpoint_halt(uint8_t addr, uint8_t ep_addr) {
    const uint8_t setup[8] = {0x02, 1, 0, 0, ep_addr, 0, 0, 0};
    return hcd_control_xfer(addr, setup, NULL, NULL);
}

hcd_result_t core_enumerate(uint8_t addr_to_assign, usb_device_t *dev) {
    memset(dev, 0, sizeof *dev);
    hcd_set_ep0_mps(8);
    uint8_t buf[256];
    uint16_t got;

    // 1. First 8 bytes of device descriptor at address 0 -> bMaxPacketSize0
    hcd_result_t r = core_get_descriptor(0, DT_DEVICE, 0, buf, 8, &got);
    if (r != HCD_OK) return r;
    if (got < 8 || buf[1] != DT_DEVICE) return HCD_ERR_DATA;
    dev->ep0_mps = buf[7];
    if (dev->ep0_mps != 8 && dev->ep0_mps != 16 &&
        dev->ep0_mps != 32 && dev->ep0_mps != 64) return HCD_ERR_DATA;
    hcd_set_ep0_mps(dev->ep0_mps);

    // 2. Assign address
    r = core_set_address(addr_to_assign);
    if (r != HCD_OK) return r;
    dev->addr = addr_to_assign;

    // 3. Full device descriptor (18 bytes)
    r = core_get_descriptor(dev->addr, DT_DEVICE, 0, buf, 18, &got);
    if (r != HCD_OK) return r;
    if (got < 18) return HCD_ERR_DATA;
    dev->vid = (uint16_t)(buf[8] | (buf[9] << 8));
    dev->pid = (uint16_t)(buf[10] | (buf[11] << 8));

    // 4. Config descriptor: header first for wTotalLength, then the whole thing
    r = core_get_descriptor(dev->addr, DT_CONFIG, 0, buf, 9, &got);
    if (r != HCD_OK) return r;
    if (got < 9 || buf[1] != DT_CONFIG) return HCD_ERR_DATA;
    uint16_t total = (uint16_t)(buf[2] | (buf[3] << 8));
    if (total > sizeof buf) total = sizeof buf;
    r = core_get_descriptor(dev->addr, DT_CONFIG, 0, buf, total, &got);
    if (r != HCD_OK) return r;

    // 5. Parse and configure
    if (!usb_parse_config(buf, got, &dev->cfg)) return HCD_ERR_DATA;
    return core_set_configuration(dev->addr, dev->cfg.config_value);
}
