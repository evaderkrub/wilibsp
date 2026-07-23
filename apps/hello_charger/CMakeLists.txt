add_executable(hello_charger main.c)
target_link_libraries(hello_charger freewili2_bsp)
pico_set_binary_type(hello_charger copy_to_ram)   # required: firmware runs from SRAM
pico_add_extra_outputs(hello_charger)             # .uf2 / .bin / .map
