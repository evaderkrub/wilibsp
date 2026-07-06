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
    radio/cc1101_regs.c
    radio/cc1101.c
    radio/gdo_capture.c
    radio/monitor_engine.c
    radio/ook_tx.c
    radio/scan_engine.c
    radio/capture_store.c
    pdm/pdm_capture.c
    dsp/cic.c
    dsp/dcblock.c
    sensors/sht40.c
    sensors/opt4001.c
    sensors/bmi323.c
    sensors/bmm350.c
    sensors/bmm350_comp.c
    ir/ir_protocols.c
    ir/ir_decode.c
    ir/ir_encode.c
    ir/ir_frame.c
    ir/ir_tx_pack.c
    ir/ir_file.c
    ir/ir_resolve.c
    ir/db_sort.c
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

# GDO0 = GPIO32; PIO must use the GPIO base window 16..47 to reach it.
target_compile_definitions(freewili2_bsp PUBLIC PICO_PIO_USE_GPIO_BASE=1)

# Generate the WS2812 PIO header into the build tree for ws2812_driver.c.
pico_generate_pio_header(freewili2_bsp
    ${CMAKE_CURRENT_SOURCE_DIR}/leds/ws2812.pio)

# Generate the I2S full-duplex PIO header into the build tree for audio_i2s_duplex.c.
pico_generate_pio_header(freewili2_bsp
    ${CMAKE_CURRENT_SOURCE_DIR}/audio/i2s_duplex.pio)

# Generate the GDO0 capture PIO header into the build tree for gdo_capture.c.
pico_generate_pio_header(freewili2_bsp
    ${CMAKE_CURRENT_SOURCE_DIR}/radio/gdo_capture.pio)

# Generate the PDM capture PIO header into the build tree for pdm_capture.c.
pico_generate_pio_header(freewili2_bsp
    ${CMAKE_CURRENT_SOURCE_DIR}/pdm/pdm_capture.pio)
