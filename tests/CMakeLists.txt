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

add_executable(test_tone_gen
    test_tone_gen.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../bsp/audio/tone_gen.c)
target_include_directories(test_tone_gen PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../bsp
    ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_tone_gen m)
add_test(NAME tone_gen COMMAND test_tone_gen)

add_executable(test_vu_meter
    test_vu_meter.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../bsp/audio/vu_meter.c)
target_include_directories(test_vu_meter PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../bsp
    ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_vu_meter m)
add_test(NAME vu_meter COMMAND test_vu_meter)

add_executable(test_cc1101_regs
    test_cc1101_regs.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../bsp/radio/cc1101_regs.c)
target_include_directories(test_cc1101_regs PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../bsp
    ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME cc1101_regs COMMAND test_cc1101_regs)

add_executable(test_monitor_engine
    test_monitor_engine.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../bsp/radio/monitor_engine.c)
target_include_directories(test_monitor_engine PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../bsp
    ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME monitor_engine COMMAND test_monitor_engine)

add_executable(test_capture_store
    test_capture_store.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../bsp/radio/capture_store.c)
target_include_directories(test_capture_store PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../bsp
    ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME capture_store COMMAND test_capture_store)

add_executable(test_ook_tx
    test_ook_tx.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../bsp/radio/ook_tx.c)
target_include_directories(test_ook_tx PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../bsp
    ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(test_ook_tx PRIVATE HOST_TEST)
add_test(NAME ook_tx COMMAND test_ook_tx)

add_executable(test_scan_engine
    test_scan_engine.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../bsp/radio/scan_engine.c)
target_include_directories(test_scan_engine PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../bsp
    ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(test_scan_engine PRIVATE HOST_TEST)
add_test(NAME scan_engine COMMAND test_scan_engine)

add_executable(test_cic
    test_cic.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../bsp/dsp/cic.c)
target_include_directories(test_cic PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../bsp
    ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME cic COMMAND test_cic)
