# OneWili C API for WiliBSP (FreeWili 2 display CPU, RP2350B).
# COPY — source of truth: freewili-firmware/freewilimain/testprojects/onewili/wilibsp (menutool-generated; re-copy on regeneration, do not edit here).
# Drop this directory into your wilibsp tree and add_subdirectory() it, or
# point an out-of-tree app at it. Requires the pico-sdk (already initialized
# by the wilibsp top-level CMakeLists).
cmake_minimum_required(VERSION 3.20)

add_library(onewili_fwgui STATIC
    src/onewili.c
    src/onewili_events.c
    src/binary_framing.c
    src/binary_transport.c
    src/onewili_fwgui.c
)
target_include_directories(onewili_fwgui PUBLIC include)
target_link_libraries(onewili_fwgui PUBLIC pico_stdlib hardware_uart hardware_gpio)
