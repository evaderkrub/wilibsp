add_executable(hello_keyboard main.c)
target_link_libraries(hello_keyboard freewili2_bsp)
pico_set_binary_type(hello_keyboard copy_to_ram)   # required: firmware runs from SRAM
pico_add_extra_outputs(hello_keyboard)             # .uf2 / .bin / .map
