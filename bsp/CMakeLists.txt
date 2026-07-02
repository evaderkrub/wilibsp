# freewili2_bsp — shared board-support library (STATIC once it has sources).
add_library(freewili2_bsp STATIC
    platform/board.c
    platform/ioexp.c
    platform/psram.c
    platform/spi_bus.c
    display/st7796.c
    display/font5x7.c
    input/ft6336.c
    input/ft6336_map.c
    leds/led_color.c
    leds/ws2812_driver.c
    leds/led_ui.c
    audio/tone_gen.c
    audio/vu_meter.c
    audio/codec_nau88c10.c
    audio/audio_i2s_duplex.c
    audio/audio_capture.c
    gfx/palette.c
    third_party/segger_rtt/SEGGER_RTT.c
    third_party/segger_rtt/SEGGER_RTT_printf.c)

target_include_directories(freewili2_bsp PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/segger_rtt)

target_link_libraries(freewili2_bsp PUBLIC
    pico_stdlib
    hardware_i2c
    hardware_spi
    hardware_dma
    hardware_pio
    hardware_clocks
    hardware_vreg
    hardware_pwm)

# Generate the WS2812 PIO header into the build tree for ws2812_driver.c.
pico_generate_pio_header(freewili2_bsp
    ${CMAKE_CURRENT_SOURCE_DIR}/leds/ws2812.pio)

# Generate the I2S full-duplex PIO header into the build tree for audio_i2s_duplex.c.
pico_generate_pio_header(freewili2_bsp
    ${CMAKE_CURRENT_SOURCE_DIR}/audio/i2s_duplex.pio)
