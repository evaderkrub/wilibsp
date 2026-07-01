cmake_minimum_required(VERSION 3.13)
project(wilibsp_tests C)
set(CMAKE_C_STANDARD 11)
enable_testing()

# Standalone tree: no Pico SDK, no pico_sdk_init(). Pure-logic modules from
# ../bsp are compiled directly against the host C compiler so tests run
# natively on Windows/Linux with no hardware.

add_executable(test_led_color
    test_led_color.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../bsp/leds/led_color.c)
target_include_directories(test_led_color PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../bsp
    ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME led_color COMMAND test_led_color)
