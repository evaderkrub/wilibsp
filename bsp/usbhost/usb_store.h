// bsp/usbhost/usb_store.h — thumb-drive lifecycle: port power, hotplug, FAT mount.
// Polled from the main loop; volume root is "0:/" while usb_store_mounted().
#ifndef USB_STORE_H
#define USB_STORE_H
#include <stdbool.h>

void usb_store_init(void);      // ioexp_usb_pwr(true) + usb_msc_init()
void usb_store_task(void);      // usb_msc_task() + mount/unmount on ready edges
bool usb_store_mounted(void);
#endif
