# hello_usbdrive

On-hardware smoke test for the harvested USB host MSC stack + FatFs
(`bsp/usbhost`): it powers the HP1/HP2 USB-A ports, polls the host stack for
a thumb drive, and on every mount edge prints the volume root listing plus a
count of `*.ir` files found there — a real FatFs read exercise, not just a
mount handshake.

    fw build hello_usbdrive
    fw flash hello_usbdrive
    fw rtt

Pass criteria: with a FAT32 stick seated, `mount OK` and the root listing
appear within a few seconds of boot, followed by the `.ir file(s) at volume
root` count; unplugging prints `drive removed` and a replug remounts
cleanly. With no stick seated, only the port-power DIAG appears — no crash.
