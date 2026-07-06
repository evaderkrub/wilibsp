// hello_usbdrive — on-hardware smoke test for the harvested USB host MSC
// stack (bsp/usbhost) + FatFs. RTT-only. Powers the HP1/HP2 ports, polls the
// host stack, and on every mount edge prints the volume root listing (the
// usb_store DIAGs) plus a non-recursive count of *.ir files at the volume
// root as a FatFs read exercise.
// Pass criteria with a FAT32 stick seated: "mount OK" + root listing within
// a few seconds of boot; pull/replug -> "drive removed" then a clean
// remount. No stick: the power-gate DIAG appears, nothing else — no crash.
#include "fw2.h"
#include "platform/diag.h"
#include "pico/stdlib.h"
#include "ff.h"
#include <string.h>
#include <strings.h>

static void count_ir_files(void) {
    DIR dir;
    FILINFO fi;
    unsigned n = 0;
    if (f_opendir(&dir, "0:/") != FR_OK) return;
    while (f_readdir(&dir, &fi) == FR_OK && fi.fname[0]) {
        size_t len = strlen(fi.fname);
        if (!(fi.fattrib & AM_DIR) && len > 3 &&
            !strcasecmp(fi.fname + len - 3, ".ir")) n++;
    }
    f_closedir(&dir);
    DIAG("hello_usbdrive: %u .ir file(s) at volume root\n", n);
}

int main(void) {
    board_init();
    DIAG("hello_usbdrive: up\n");
    usb_store_init();                 // ioexp_usb_pwr(true) + host stack init
    bool was_mounted = false;
    while (true) {
        usb_store_task();
        bool m = usb_store_mounted();
        if (m != was_mounted) {
            was_mounted = m;
            if (m) count_ir_files();
        }
        sleep_ms(2);
    }
}
