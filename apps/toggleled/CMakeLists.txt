add_executable(toggleled main.c)
target_link_libraries(toggleled freewili2_bsp onewili_fwgui)
pico_set_binary_type(toggleled copy_to_ram)   # required: firmware runs from SRAM
pico_add_extra_outputs(toggleled)             # .uf2 / .bin / .map
