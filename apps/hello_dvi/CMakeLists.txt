add_executable(hello_dvi main.c)
target_link_libraries(hello_dvi freewili2_bsp)
pico_set_binary_type(hello_dvi copy_to_ram)
pico_add_extra_outputs(hello_dvi)
