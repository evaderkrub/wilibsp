// bsp/usbhost/usb_store.c
#include "usb_store.h"
#include "usb_msc.h"
#include "platform/ioexp.h"
#include "platform/diag.h"
#include "ff.h"
#include "pico/stdlib.h"

static FATFS s_fs;
static bool  s_ready;           // usbmsc ready, edge-tracked
static bool  s_mounted;
static absolute_time_t s_retry_at;

void usb_store_init(void) {
    ioexp_usb_pwr(true);
    usb_msc_init();
}

bool usb_store_mounted(void) { return s_mounted; }

static void list_root(void) {
    DIR dir; FILINFO fi;
    if (f_opendir(&dir, "0:/") != FR_OK) { DIAG("usb_store: opendir root failed\n"); return; }
    int shown = 0;
    while (f_readdir(&dir, &fi) == FR_OK && fi.fname[0] && shown < 10) {
        DIAG("usb_store:   %s%s\n", fi.fname, (fi.fattrib & AM_DIR) ? "/" : "");
        shown++;
    }
    f_closedir(&dir);
}

static void try_mount(void) {
    FRESULT fr = f_mount(&s_fs, "0:", 1);          // force-mount now
    s_mounted = (fr == FR_OK);
    DIAG("usb_store: [%s] mount %s\n", usb_msc_drive_name(),
         s_mounted ? "OK" : "FAILED");
    if (s_mounted) list_root();
    else s_retry_at = make_timeout_time_ms(2000);
}

void usb_store_task(void) {
    usb_msc_task();
    bool ready = usb_msc_ready();
    if (ready != s_ready) {
        s_ready = ready;
        if (ready) {
            try_mount();
        } else {
            f_unmount("0:");
            s_mounted = false;
            DIAG("usb_store: drive removed, unmounted\n");
        }
        return;
    }
    // A failed mount no longer wedges until replug: retry on a 2 s cadence
    // while the drive stays ready but unmounted.
    if (s_ready && !s_mounted && time_reached(s_retry_at)) try_mount();
}
