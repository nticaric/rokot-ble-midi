# RokoT BLE-MIDI Library
# CMakeLists.txt - Library build configuration

cmake_minimum_required(VERSION 3.13)

# Function to set up a target that uses rokot_ble_midi
# Call this AFTER creating your executable
function(rokot_ble_midi_configure_target TARGET_NAME)
    # SPI clock divider configuration
    # Default to 3 (50 MHz) for custom boards with Radio Module 2
    if(NOT DEFINED ROKOT_BLE_MIDI_SPI_CLK_DIV)
        set(ROKOT_BLE_MIDI_SPI_CLK_DIV 3)
    endif()

    # Add the library source directly to the target
    target_sources(${TARGET_NAME} PRIVATE
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/src/rokot_ble_midi.c"
    )
    
    # Include directories
    target_include_directories(${TARGET_NAME} PRIVATE
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/include"
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/src"
    )
    
    # Link required Pico SDK libraries
    target_link_libraries(${TARGET_NAME} PRIVATE
        pico_stdlib
        pico_btstack_ble
        pico_btstack_cyw43
        pico_cyw43_arch_none
    )
    
    # Apply SPI clock configuration
    target_compile_definitions(${TARGET_NAME} PRIVATE
        CYW43_PIO_CLOCK_DIV_INT=${ROKOT_BLE_MIDI_SPI_CLK_DIV}
        CYW43_PIO_CLOCK_DIV_FRAC8=0
    )
    
    # Generate GATT header from .gatt file
    pico_btstack_make_gatt_header(${TARGET_NAME} PRIVATE
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/src/rokot_ble_midi_service.gatt"
    )
endfunction()
