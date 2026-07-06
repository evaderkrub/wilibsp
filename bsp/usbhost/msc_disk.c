// bsp/usbhost/msc_disk.c — FatFs diskio over the usbmsc block API. Single volume
// (pdrv 0), 512-byte sectors only. No RTC on board: under FF_FS_NORTC 1
// (ffconf.h) FatFs stamps the fixed date 2026-07-01 on every write itself;
// get_fattime() below is kept, compiled out, for a future RTC.
#include "ff.h"
#include "diskio.h"
#include "usb_msc.h"

DSTATUS disk_status(BYTE pdrv) {
    if (pdrv != 0) return STA_NOINIT;
    return usb_msc_ready() ? 0 : (STA_NOINIT | STA_NODISK);
}

DSTATUS disk_initialize(BYTE pdrv) {
    // usbmsc enumerates on its own from usb_msc_task(); nothing to start here.
    return disk_status(pdrv);
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count) {
    if (pdrv != 0) return RES_PARERR;
    if (!usb_msc_ready()) return RES_NOTRDY;
    return usb_msc_read((uint32_t)sector, count, buff) == MSC_OK ? RES_OK : RES_ERROR;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count) {
    if (pdrv != 0) return RES_PARERR;
    if (!usb_msc_ready()) return RES_NOTRDY;
    return usb_msc_write((uint32_t)sector, count, buff) == MSC_OK ? RES_OK : RES_ERROR;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff) {
    if (pdrv != 0) return RES_PARERR;
    switch (cmd) {
    case CTRL_SYNC:
        return RES_OK;                        // usb_msc_write is synchronous
    case GET_SECTOR_COUNT:
        if (!usb_msc_ready()) return RES_NOTRDY;
        *(LBA_t *)buff = usb_msc_block_count();
        return RES_OK;
    case GET_SECTOR_SIZE:
        if (!usb_msc_ready()) return RES_NOTRDY;
        if (usb_msc_block_size() != 512) return RES_ERROR;   // only 512 supported
        *(WORD *)buff = 512;
        return RES_OK;
    case GET_BLOCK_SIZE:
        *(DWORD *)buff = 1;                   // erase block unknown -> 1 sector
        return RES_OK;
    default:
        return RES_PARERR;
    }
}

#if !FF_FS_NORTC
DWORD get_fattime(void) {
    // No RTC: fixed placeholder date, matching ffconf.h's FF_NORTC_YEAR/MON/MDAY
    // (bits: y-1980 << 25 | m << 21 | d << 16). Dead code today (FF_FS_NORTC is
    // 1); kept for a future RTC.
    return ((DWORD)(2026 - 1980) << 25) | ((DWORD)7 << 21) | ((DWORD)1 << 16);
}
#endif
