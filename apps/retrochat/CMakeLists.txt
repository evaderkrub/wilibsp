add_executable(retrochat
    main.c
    audio_glue.c
    protocol.c
    ui.c
    modem/crc16.c
    modem/frame.c
    modem/msgq.c
    modem/afsk_mod.c
    modem/afsk_demod.c)
target_include_directories(retrochat PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(retrochat freewili2_bsp pico_multicore pico_unique_id)
pico_set_binary_type(retrochat copy_to_ram)   # required: firmware runs from SRAM
pico_add_extra_outputs(retrochat)
