# freewili2_bsp — shared board-support library.
# Starts as INTERFACE (headers/include-root only); driver .c files and their
# pico_sdk deps are added by Tasks 3-6 as the sources land.
add_library(freewili2_bsp INTERFACE)

target_include_directories(freewili2_bsp INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/segger_rtt)
