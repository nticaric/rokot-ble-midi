# Simple Note Example
# CMakeLists.txt

cmake_minimum_required(VERSION 3.13)

# Include Pico SDK
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(simple_note_example C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initialize the SDK
pico_sdk_init()

# Add rokot-ble-midi library
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../.. rokot_ble_midi)

# Create the executable
add_executable(simple_note_example
    main.c
)

# Configure the target with rokot-ble-midi (links library and generates GATT)
rokot_ble_midi_configure_target(simple_note_example)

# Enable USB serial output
pico_enable_stdio_usb(simple_note_example 1)
pico_enable_stdio_uart(simple_note_example 0)

# Create UF2 and other output formats
pico_add_extra_outputs(simple_note_example)
