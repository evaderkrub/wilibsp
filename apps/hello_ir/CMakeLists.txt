add_executable(hello_ir main.c)
target_link_libraries(hello_ir freewili2_bsp)
pico_set_binary_type(hello_ir copy_to_ram)
pico_add_extra_outputs(hello_ir)
