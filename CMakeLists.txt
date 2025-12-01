# RokoT BLE-MIDI Library
# CMakeLists.txt - Library build configuration

cmake_minimum_required(VERSION 3.13)

# Library name
set(ROKOT_BLE_MIDI_LIB rokot_ble_midi)

# Create the library as INTERFACE (header + source to be compiled with the app)
add_library(${ROKOT_BLE_MIDI_LIB} INTERFACE)

# Include directories
target_include_directories(${ROKOT_BLE_MIDI_LIB} INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${CMAKE_CURRENT_LIST_DIR}/src
)

# Source files (compiled as part of the target that links this library)
target_sources(${ROKOT_BLE_MIDI_LIB} INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/src/rokot_ble_midi.c
)

# Required Pico SDK libraries
target_link_libraries(${ROKOT_BLE_MIDI_LIB} INTERFACE
    pico_stdlib
    pico_btstack_ble
    pico_btstack_cyw43
    pico_cyw43_arch_none
)

# Apply SPI clock divider configuration
# Default to 3 (50 MHz) for custom boards with Radio Module 2
# Can be overridden by defining ROKOT_BLE_MIDI_SPI_CLK_DIV before including this
if(NOT DEFINED ROKOT_BLE_MIDI_SPI_CLK_DIV)
    set(ROKOT_BLE_MIDI_SPI_CLK_DIV 3)
endif()

target_compile_definitions(${ROKOT_BLE_MIDI_LIB} INTERFACE
    CYW43_PIO_CLOCK_DIV_INT=${ROKOT_BLE_MIDI_SPI_CLK_DIV}
    CYW43_PIO_CLOCK_DIV_FRAC8=0
)

# Function to set up a target that uses rokot_ble_midi
# This generates the GATT header and links everything properly
function(rokot_ble_midi_configure_target TARGET_NAME)
    # Link the library
    target_link_libraries(${TARGET_NAME} PRIVATE ${ROKOT_BLE_MIDI_LIB})
    
    # Generate GATT header from .gatt file
    pico_btstack_make_gatt_header(${TARGET_NAME} PRIVATE
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/src/rokot_ble_midi_service.gatt"
    )
endfunction()
