cmake_minimum_required(VERSION 3.12)

# Change your executable name to something creative!
set(NAME swd-program) # <-- Name your project/executable here!

include(pimoroni_pico_import.cmake)
include(pico_sdk_import.cmake)

# Gooey boilerplate
project(${NAME} C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initialize the SDK
pico_sdk_init()

# Add your source files
add_executable(${NAME}
    main.cpp # <-- Add source files here!
    swd_load.cpp
)

# Include required libraries
# This assumes `pimoroni-pico` is stored alongside your project

# Don't forget to link the libraries you need!
target_link_libraries(${NAME}
    pico_stdlib
    pico_multicore
    hardware_pio
)

pico_generate_pio_header(${NAME} ${CMAKE_CURRENT_LIST_DIR}/swd.pio)

pico_enable_stdio_usb(${NAME} 1)

# create map/bin/hex file etc.
pico_add_extra_outputs(${NAME})

# Set up files for the release packages
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/${NAME}.uf2
    ${CMAKE_CURRENT_LIST_DIR}/README.md
    DESTINATION .
)

set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
set(CPACK_GENERATOR "ZIP" "TGZ")
include(CPack)
