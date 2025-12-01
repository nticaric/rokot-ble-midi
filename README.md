# RokoT BLE-MIDI Library

A Raspberry Pi Pico SDK library for BLE-MIDI (Bluetooth Low Energy MIDI) on RP2350/RP2040 with CYW43 (Pico W, Pico 2 W, or Radio Module 2).

## Features

- **BLE-MIDI 1.0 compliant** - Works with macOS, iOS, Windows, Android, and Linux
- **Low-latency connection** - Configurable 7.5ms connection interval for real-time MIDI
- **Battery Service** - Report battery level to connected host
- **Device Information Service** - Manufacturer name and firmware version
- **Full MIDI support** - Note On/Off, Control Change, Program Change, Pitch Bend, Channel Pressure
- **Configurable SPI clock** - Default 50 MHz for Radio Module 2 compatibility
- **Simple API** - Easy to integrate into existing projects
- **Receive callbacks** - Handle incoming MIDI messages from host

## Hardware Requirements

- **RP2350** (Raspberry Pi Pico 2 W) or **RP2040** (Raspberry Pi Pico W)
- **CYW43439** WiFi/Bluetooth chip (built into Pico W boards, or Radio Module 2)
- USB connection for power and serial output

### Radio Module 2 GPIO Connections (Custom Boards)

| GPIO | Function |
|------|----------|
| GPIO29 | gSPI CLK |
| GPIO25 | gSPI CS |
| GPIO24 | nIRQ, gSPI DATA (bidirectional) |
| GPIO23 | gSPI ON (WiFi/BT power enable) |

## Installation

### Option 1: Add as Git Submodule

```bash
cd your-project
git submodule add https://github.com/nticaric/rokot-ble-midi.git lib/rokot-ble-midi
```

### Option 2: Copy to Your Project

Copy the `rokot-ble-midi` folder into your project's `lib/` directory.

## Usage

### CMakeLists.txt Setup

```cmake
cmake_minimum_required(VERSION 3.13)

# Set board BEFORE including SDK (pico_w, pico2_w, etc.)
set(PICO_BOARD pico2_w)

# Include Pico SDK
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(my_ble_midi_project C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initialize the SDK
pico_sdk_init()

# Add rokot-ble-midi library
add_subdirectory(lib/rokot-ble-midi)

# Create your executable
add_executable(my_ble_midi_app
    main.c
)

# Configure the target with rokot-ble-midi
rokot_ble_midi_configure_target(my_ble_midi_app)

# Enable USB serial output
pico_enable_stdio_usb(my_ble_midi_app 1)
pico_enable_stdio_uart(my_ble_midi_app 0)

# Create UF2 output
pico_add_extra_outputs(my_ble_midi_app)
```

### Basic Example

```c
#include <stdio.h>
#include "pico/stdlib.h"
#include "rokot_ble_midi.h"

int main() {
    stdio_init_all();
    
    // Initialize BLE-MIDI with device name
    if (rokot_ble_midi_init("My MIDI Device") != 0) {
        printf("BLE-MIDI init failed!\n");
        return 1;
    }
    
    printf("BLE-MIDI started. Waiting for connection...\n");
    
    uint32_t last_note_time = 0;
    bool note_on = false;
    
    while (true) {
        // Run the BLE-MIDI background task
        rokot_ble_midi_task();
        
        // Only send MIDI when ready (connected + notifications enabled)
        if (rokot_ble_midi_is_ready()) {
            uint32_t now = to_ms_since_boot(get_absolute_time());
            
            // Toggle note every second
            if (now - last_note_time >= 1000) {
                last_note_time = now;
                note_on = !note_on;
                
                if (note_on) {
                    rokot_ble_midi_note_on(0, MIDI_NOTE_C4, 100);
                    printf("Note ON: C4\n");
                } else {
                    rokot_ble_midi_note_off(0, MIDI_NOTE_C4);
                    printf("Note OFF: C4\n");
                }
            }
        }
    }
    
    return 0;
}
```

### Battery Level Example

```c
#include "rokot_ble_midi.h"

int main() {
    stdio_init_all();
    
    rokot_ble_midi_init("Battery MIDI");
    
    // Set initial battery level
    rokot_ble_midi_set_battery_level(75);
    
    while (true) {
        rokot_ble_midi_task();
        
        // Update battery level periodically
        // (in real use, read from ADC)
        uint8_t level = read_battery_adc();  // Your function
        rokot_ble_midi_set_battery_level(level);
    }
}
```

### Receiving MIDI Messages

```c
#include "rokot_ble_midi.h"

// Callback for incoming MIDI data
void midi_callback(uint8_t status, uint8_t data1, uint8_t data2) {
    uint8_t type = status & 0xF0;
    uint8_t channel = status & 0x0F;
    
    if (type == MIDI_NOTE_ON) {
        printf("Note ON: ch=%d note=%d vel=%d\n", channel, data1, data2);
    } else if (type == MIDI_NOTE_OFF) {
        printf("Note OFF: ch=%d note=%d\n", channel, data1);
    }
}

int main() {
    stdio_init_all();
    
    rokot_ble_midi_init("My MIDI Device");
    rokot_ble_midi_set_callback(midi_callback);
    
    while (true) {
        rokot_ble_midi_task();
    }
}
```

## API Reference

### Initialization

```c
int rokot_ble_midi_init(const char *device_name);
```
Initialize BLE-MIDI with the specified device name (max 29 characters).

```c
void rokot_ble_midi_deinit(void);
```
Shutdown BLE-MIDI and release resources.

```c
void rokot_ble_midi_task(void);
```
Run the BLE-MIDI background task. Call this in your main loop.

### Connection Status

```c
bool rokot_ble_midi_is_connected(void);
```
Returns `true` if a BLE host is connected.

```c
bool rokot_ble_midi_is_ready(void);
```
Returns `true` if connected AND notifications are enabled (ready to send MIDI).

```c
rokot_ble_midi_state_t rokot_ble_midi_get_state(void);
```
Returns `ROKOT_BLE_MIDI_DISCONNECTED`, `ROKOT_BLE_MIDI_CONNECTED`, or `ROKOT_BLE_MIDI_READY`.

```c
float rokot_ble_midi_get_connection_interval(void);
```
Returns the current connection interval in milliseconds.

### Device Information

```c
void rokot_ble_midi_set_manufacturer(const char *manufacturer);
void rokot_ble_midi_set_firmware_version(const char *version);
```
Set device information (call before `rokot_ble_midi_init()` to override defaults).

### Battery Service

```c
void rokot_ble_midi_set_battery_level(uint8_t level);
uint8_t rokot_ble_midi_get_battery_level(void);
```
Set/get battery level (0-100%). Automatically notifies connected host when level changes.

### Sending MIDI Messages

```c
int rokot_ble_midi_note_on(uint8_t channel, uint8_t note, uint8_t velocity);
int rokot_ble_midi_note_off(uint8_t channel, uint8_t note);
int rokot_ble_midi_control_change(uint8_t channel, uint8_t controller, uint8_t value);
int rokot_ble_midi_program_change(uint8_t channel, uint8_t program);
int rokot_ble_midi_pitch_bend(uint8_t channel, int16_t value);  // -8192 to +8191
int rokot_ble_midi_channel_pressure(uint8_t channel, uint8_t pressure);
int rokot_ble_midi_send_raw(const uint8_t *data, uint8_t len);
```
All send functions return `0` on success, negative on error.

### Receiving MIDI

```c
typedef void (*rokot_ble_midi_callback_t)(uint8_t status, uint8_t data1, uint8_t data2);
void rokot_ble_midi_set_callback(rokot_ble_midi_callback_t callback);
```
Set a callback to receive incoming MIDI messages from the host.

### MIDI Constants

```c
// Message types
MIDI_NOTE_OFF, MIDI_NOTE_ON, MIDI_POLY_PRESSURE, MIDI_CONTROL_CHANGE,
MIDI_PROGRAM_CHANGE, MIDI_CHANNEL_PRESSURE, MIDI_PITCH_BEND

// Common Control Change numbers
MIDI_CC_MOD_WHEEL, MIDI_CC_VOLUME, MIDI_CC_PAN, MIDI_CC_EXPRESSION,
MIDI_CC_SUSTAIN, MIDI_CC_ALL_NOTES_OFF

// Note numbers (Middle C octave)
MIDI_NOTE_C4, MIDI_NOTE_CS4, MIDI_NOTE_D4, ... MIDI_NOTE_B4
```

## Configuration

### SPI Clock Divider (Custom Boards)

The default SPI clock is 50 MHz (divider 3). For standard Pico W, you can use 75 MHz:

```cmake
# In your CMakeLists.txt, before add_subdirectory:
add_compile_definitions(CYW43_PIO_CLOCK_DIV_INT=2)  # 75 MHz for Pico W
```

| Divider | Clock Speed | Use Case |
|---------|-------------|----------|
| 2 | 75 MHz | Standard Pico W |
| 3 | 50 MHz | Radio Module 2 (default) |
| 4 | 37.5 MHz | Longer SPI traces |

### Connection Interval

Define these before including the header to customize connection parameters:

```c
#define ROKOT_BLE_MIDI_CONN_INTERVAL_MIN 6   // 7.5ms (minimum, default)
#define ROKOT_BLE_MIDI_CONN_INTERVAL_MAX 12  // 15ms (fallback, default)
#include "rokot_ble_midi.h"
```

### Device Information Defaults

```c
#define ROKOT_BLE_MIDI_MANUFACTURER "RokoT"
#define ROKOT_BLE_MIDI_FIRMWARE_VERSION "1.0.0"
```

## Building and Flashing

```bash
mkdir build && cd build
cmake -G Ninja ..
ninja
picotool load my_ble_midi_app.uf2 -fx
```

## Testing on macOS

1. Build and flash your firmware

2. Open **Audio MIDI Setup** (in /Applications/Utilities)

3. Go to **Window → Show MIDI Studio**

4. Click the **Bluetooth** icon in the toolbar

5. Your device should appear - click **Connect**

6. Open **GarageBand** or any DAW with a software instrument

7. You should hear the MIDI notes playing!

### Serial Monitor

Connect to see debug output:
```bash
screen /dev/tty.usbmodem* 115200
```

To exit screen: `Ctrl+A` then `K`, then `Y`

## BLE Services

The library provides these BLE services:

| Service | UUID | Description |
|---------|------|-------------|
| Device Information | 0x180A | Manufacturer, Firmware Version |
| Battery Service | 0x180F | Battery level (0-100%) |
| BLE-MIDI | 03B80E5A-EDE8-4B33-A751-6CE34EC4C700 | MIDI data |

## Troubleshooting

### Device not appearing in Bluetooth scan

- Ensure the device is powered and running
- Check serial output for "BLE-MIDI started" message
- Try restarting Bluetooth on your computer

### "hdr mismatch" errors on custom board

- Your SPI clock is too fast
- Use `CYW43_PIO_CLOCK_DIV_INT=3` for 50 MHz
- Try divider 4 if issues persist

### Connected but no sound

- Make sure a DAW is open with a software instrument
- Check that MIDI channel matches (default: channel 0)
- Use `rokot_ble_midi_is_ready()` not just `is_connected()`

### High latency

- Request lower connection interval
- macOS typically negotiates 15-30ms even when 7.5ms is requested

## License

MIT License - See LICENSE file for details.

## Credits

- Based on BTstack Bluetooth stack
- Raspberry Pi Pico SDK
- BLE-MIDI specification by the MIDI Manufacturers Association
