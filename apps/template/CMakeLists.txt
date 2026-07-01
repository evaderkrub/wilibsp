# App target name MUST be the single token 'template' — `fw new-app <name>`
# rewrites every 'template' occurrence to the new app name.
add_executable(template main.c)
target_link_libraries(template freewili2_bsp)
pico_set_binary_type(template copy_to_ram)   # required: firmware runs from SRAM
pico_add_extra_outputs(template)             # .uf2 / .bin / .map
