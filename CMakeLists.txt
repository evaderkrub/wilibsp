cmake_minimum_required(VERSION 3.13)

# RP2350B board header (PICO_RP2350A=0 -> 48 GPIO, 16MB flash).
# Do NOT pass -DPICO_BOARD on the command line — that overrides this.
set(PICO_BOARD freewili2 CACHE STRING "Board type")
list(APPEND PICO_BOARD_HEADER_DIRS "${CMAKE_CURRENT_LIST_DIR}/bsp/boards")

include(pico_sdk_import.cmake)
project(wilibsp C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

add_subdirectory(bsp)

# Each app opts in; add new apps here.
add_subdirectory(apps/hello_display)
