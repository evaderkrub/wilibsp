#include "usb_hub.h"
#include <string.h>
#include "pico/stdlib.h"

// USB 2.0 ch. 11: hub class requests and port features
enum {
    HUB_FEAT_PORT_RESET = 4,
    HUB_FEAT_PORT_POWER = 8,
    HUB_FEAT_C_PORT_CONNECTION = 16,
    HUB_FEAT_C_PORT_RESET = 20,
};
// wPortStatus bits
#define PORT_STAT_CONNECTION 0x0001u
#define PORT_STAT_LOW_SPEED  0x0200u
// wPortChange bits
#define PORT_CHG_CONNECTION  0x0001u

static usb_device_t hub;
static uint8_t      n_ports;
static int          drive_port = -1;    // 1-based; -1 = none
static bool         active;

static hcd_result_t hub_set_port_feature(uint8_t port, uint8_t feature) {
    const uint8_t setup[8] = {0x23, 3, feature, 0, port, 0, 0, 0};
    return hcd_control_xfer(hub.addr, setup, NULL, NULL);
}

static hcd_result_t hub_clear_port_feature(uint8_t port, uint8_t feature) {
    const uint8_t setup[8] = {0x23, 1, feature, 0, port, 0, 0, 0};
    return hcd_control_xfer(hub.addr, setup, NULL, NULL);
}

static hcd_result_t hub_get_port_status(uint8_t port, uint16_t *status,
                                        uint16_t *change) {
    const uint8_t setup[8] = {0xA3, 0, 0, 0, port, 0, 4, 0};
    uint8_t b[4];
    uint16_t len = 4;
    hcd_result_t r = hcd_control_xfer(hub.addr, setup, b, &len);
    if (r != HCD_OK) return r;
    if (len != 4) return HCD_ERR_DATA;
    *status = (uint16_t)(b[0] | (b[1] << 8));
    *change = (uint16_t)(b[2] | (b[3] << 8));
    return HCD_OK;
}

hcd_result_t usb_hub_attach(const usb_device_t *dev) {
    hub = *dev;
    drive_port = -1;

    // Hub descriptor (class type 0x29): bNbrPorts at offset 2,
    // bPwrOn2PwrGood (2 ms units) at offset 5
    const uint8_t setup[8] = {0xA0, 6, 0, 0x29, 0, 0, 9, 0};
    uint8_t hd[9];
    uint16_t len = 9;
    hcd_result_t r = hcd_control_xfer(hub.addr, setup, hd, &len);
    if (r != HCD_OK) return r;
    if (len < 7) return HCD_ERR_DATA;
    n_ports = hd[2];
    if (n_ports > 7) n_ports = 7;   // status bitmap handling supports ports 1..7 (1-byte report)

    for (uint8_t p = 1; p <= n_ports; p++) {
        r = hub_set_port_feature(p, HUB_FEAT_PORT_POWER);
        if (r != HCD_OK) return r;
    }
    sleep_ms(2 * hd[5] + 20);   // power-on to power-good

    // NOTE (deviation from the plan, deliberate): the status-change interrupt
    // endpoint is installed at the END of a successful usb_hub_wait_drive(),
    // not here. Reason: SIE_STATUS.TRANS_COMPLETE is shared -- regs/usb.h says
    // it is "Raised by host if: ... An IN packet is received and the
    // `LAST_BUFF` bit is set in the buffer control register", which includes
    // hardware-polled interrupt-EP completions. Installing here guarantees
    // the hub's first change report (the already-connected drive) lands while
    // wait_drive is busy with EPX control transfers, spuriously completing
    // sie_wait_trans_complete(). Installing late also avoids leaving the int
    // EP polling a stale address across the bus reset in the wait-drive-
    // timeout retry path. Nothing is lost: wait_drive detects changes by
    // GetPortStatus polling, and a hub holds change bits until polled, so the
    // first int-EP report after a late install still reflects earlier events.
    return HCD_OK;
}

// Scan ports for a connected FS device; reset it; enumerate it at address 2.
// Note: after PORT_RESET the downstream device answers at address 0; the hub
// itself keeps its address, so core_enumerate's address-0 phase is
// unambiguous. Deliberately no hcd_bus_reset here -- that would reset the
// hub too; the port reset already reset the device.
hcd_result_t usb_hub_wait_drive(usb_device_t *drive, uint32_t timeout_ms) {
    absolute_time_t deadline = make_timeout_time_ms(timeout_ms);
    while (!time_reached(deadline)) {
        for (uint8_t p = 1; p <= n_ports; p++) {
            uint16_t st, chg;
            if (hub_get_port_status(p, &st, &chg) != HCD_OK) continue;
            if (chg & PORT_CHG_CONNECTION)
                hub_clear_port_feature(p, HUB_FEAT_C_PORT_CONNECTION);
            if (!(st & PORT_STAT_CONNECTION)) continue;
            if (st & PORT_STAT_LOW_SPEED) continue;    // LS not supported

            sleep_ms(100);                              // connect debounce
            if (hub_set_port_feature(p, HUB_FEAT_PORT_RESET) != HCD_OK) continue;
            // Wait for reset-complete (C_PORT_RESET), max 500 ms
            absolute_time_t rst_deadline = make_timeout_time_ms(500);
            bool reset_done = false;
            while (!time_reached(rst_deadline)) {
                if (hub_get_port_status(p, &st, &chg) != HCD_OK) break;
                if (chg & (1u << 4)) {                  // C_PORT_RESET
                    hub_clear_port_feature(p, HUB_FEAT_C_PORT_RESET);
                    reset_done = true;
                    break;
                }
                sleep_ms(10);
            }
            if (!reset_done) continue;
            sleep_ms(20);                               // post-reset recovery

            if (core_enumerate(2, drive) == HCD_OK && drive->cfg.is_msc) {
                drive_port = p;
                // Start the hardware-polled status pipe now that no more
                // EPX control traffic is pending (see note in attach).
                hcd_int_ep_install(hub.addr, hub.cfg.hub_int_ep,
                                   hub.cfg.hub_int_mps, hub.cfg.hub_int_interval);
                active = true;
                return HCD_OK;
            }
        }
        sleep_ms(50);
    }
    return HCD_ERR_TIMEOUT;
}

// Called from usb_msc_task() while READY: consume hub status-change reports
// and re-check the drive's port on any change.
bool usb_hub_drive_present(void) {
    if (!active || drive_port < 0) return false;
    uint8_t bitmap[4];
    int n = hcd_int_ep_poll(bitmap, sizeof bitmap);
    if (n <= 0) return true;                            // no change reported
    // Bit (port N) of the bitmap = change on port N (bit 0 = hub itself)
    if (!(bitmap[drive_port / 8] & (1u << (drive_port % 8)))) return true;
    uint16_t st, chg;
    if (hub_get_port_status((uint8_t)drive_port, &st, &chg) != HCD_OK) return false;
    if (chg & PORT_CHG_CONNECTION)
        hub_clear_port_feature((uint8_t)drive_port, HUB_FEAT_C_PORT_CONNECTION);
    return (st & PORT_STAT_CONNECTION) != 0;
}

void usb_hub_detach(void) {
    if (active) hcd_int_ep_remove();
    active = false;
    drive_port = -1;
}
