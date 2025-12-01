# RokoT BLE-MIDI Library

A Raspberry Pi Pico SDK library for BLE-MIDI (Bluetooth Low Energy MIDI) on RP2350 with Radio Module 2 (RM2).

## Features

- **BLE-MIDI 1.0 compliant** - Works with macOS, iOS, Windows, Android, and Linux
- **Low-latency connection** - Configurable 7.5ms connection interval for real-time MIDI
- **Full MIDI support** - Note On/Off, Control Change, Program Change, Pitch Bend, Channel Pressure, Polyphonic Aftertouch, System Exclusive
- **Configurable SPI clock** - Default 50 MHz for Radio Module 2 compatibility
- **Simple API** - Easy to integrate into existing projects
- **Receive callbacks** - Handle incoming MIDI messages from host

## Hardware Requirements

- **RP2350** (Raspberry Pi Pico 2 or custom board)
- **Raspberry Pi Radio Module 2** (CYW43439) for WiFi/Bluetooth
- USB connection for power and serial output

### Radio Module 2 GPIO Connections

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
git submodule add https://github.com/yourusername/rokot-ble-midi.git lib/rokot-ble-midi
```

### Option 2: Copy to Your Project

Copy the `rokot-ble-midi` folder into your project's `lib/` directory.

## Usage

### CMakeLists.txt Setup

```cmake
cmake_minimum_required(VERSION 3.13)

# Include Pico SDK
include(pico_sdk_import.cmake)

project(my_ble_midi_project C CXX ASM)

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
        
        // Only send MIDI when connected
        if (rokot_ble_midi_is_connected()) {
            uint32_t now = to_ms_since_boot(get_absolute_time());
            
            // Toggle note every second
            if (now - last_note_time >= 1000) {
                last_note_time = now;
                note_on = !note_on;
                
                if (note_on) {
                    rokot_ble_midi_send_note_on(0, MIDI_NOTE_C4, 100);
                    printf("Note ON: C4\n");
                } else {
                    rokot_ble_midi_send_note_off(0, MIDI_NOTE_C4, 0);
                    printf("Note OFF: C4\n");
                }
            }
        }
    }
    
    return 0;
}
```

### Receiving MIDI Messages

```c
#include "rokot_ble_midi.h"

// Callback for incoming MIDI data
void my_midi_receive_callback(const uint8_t *data, uint16_t length) {
    // Skip BLE-MIDI header/timestamp bytes
    // Parse MIDI messages from data
    for (int i = 0; i < length; i++) {
        printf("MIDI byte: 0x%02X\n", data[i]);
    }
}

int main() {
    stdio_init_all();
    
    rokot_ble_midi_init("My MIDI Device");
    
    // Register receive callback
    rokot_ble_midi_set_receive_callback(my_midi_receive_callback);
    
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
Initialize BLE-MIDI with the specified device name (max 14 characters recommended for advertising).

```c
void rokot_ble_midi_task(void);
```
Run the BLE-MIDI background task. Call this in your main loop.

### Connection Status

```c
bool rokot_ble_midi_is_connected(void);
```
Returns `true` if a BLE host is connected.

### Sending MIDI Messages

```c
int rokot_ble_midi_send_note_on(uint8_t channel, uint8_t note, uint8_t velocity);
int rokot_ble_midi_send_note_off(uint8_t channel, uint8_t note, uint8_t velocity);
int rokot_ble_midi_send_control_change(uint8_t channel, uint8_t controller, uint8_t value);
int rokot_ble_midi_send_program_change(uint8_t channel, uint8_t program);
int rokot_ble_midi_send_pitch_bend(uint8_t channel, uint16_t value);
int rokot_ble_midi_send_channel_pressure(uint8_t channel, uint8_t pressure);
int rokot_ble_midi_send_poly_aftertouch(uint8_t channel, uint8_t note, uint8_t pressure);
int rokot_ble_midi_send_raw(const uint8_t *data, uint16_t length);
```
All send functions return `0` on success, `-1` on error.

### Receiving MIDI

```c
typedef void (*rokot_ble_midi_receive_callback_t)(const uint8_t *data, uint16_t length);
void rokot_ble_midi_set_receive_callback(rokot_ble_midi_receive_callback_t callback);
```
Set a callback to receive incoming MIDI data from the host.

### MIDI Constants

```c
// MIDI Note Numbers
MIDI_NOTE_C0  ... MIDI_NOTE_B0   // Octave 0 (C0 = 12)
MIDI_NOTE_C1  ... MIDI_NOTE_B1   // Octave 1
...
MIDI_NOTE_C4  ... MIDI_NOTE_B4   // Middle C octave (C4 = 60)
...
MIDI_NOTE_C8  ... MIDI_NOTE_G8   // Octave 8

// Common Control Change Numbers
MIDI_CC_MOD_WHEEL      // 1
MIDI_CC_VOLUME         // 7
MIDI_CC_PAN            // 10
MIDI_CC_EXPRESSION     // 11
MIDI_CC_SUSTAIN        // 64
MIDI_CC_ALL_NOTES_OFF  // 123
```

## Configuration

### SPI Clock Divider

The default SPI clock divider is 3 (50 MHz), which is required for Radio Module 2. To change it:

```cmake
# In your CMakeLists.txt, before add_subdirectory:
set(ROKOT_BLE_MIDI_SPI_CLK_DIV 2)  # 75 MHz for Pico W
add_subdirectory(lib/rokot-ble-midi)
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

Lower intervals = lower latency but higher power consumption.

## Testing on macOS

1. Build and flash your firmware:
   ```bash
   cd build
   cmake -G Ninja ..
   ninja my_ble_midi_app
   picotool load my_ble_midi_app.elf -fx
   ```

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

## BLE-MIDI Packet Format

BLE-MIDI uses a specific packet format:

```
[Header] [Timestamp] [MIDI Status] [Data1] [Data2]
  0x80     0x80        0x90        0x3C    0x64
```

- **Header byte**: Bit 7 = 1, bits 5-0 = timestamp high 6 bits
- **Timestamp byte**: Bit 7 = 1, bits 6-0 = timestamp low 7 bits
- **MIDI data**: Standard MIDI message bytes

The library handles this encoding/decoding automatically.

## Troubleshooting

### Device not appearing in Bluetooth scan

- Ensure the device is powered and running
- Check serial output for "BLE-MIDI started" message
- Try restarting Bluetooth on your computer
- Device name appears in scan response, service UUID in advertising data

### "hdr mismatch" errors on custom board

- Your SPI clock is too fast
- Use `set(ROKOT_BLE_MIDI_SPI_CLK_DIV 3)` for 50 MHz
- Try divider 4 if issues persist

### Connected but no sound

- Make sure a DAW (GarageBand, Logic, Ableton, etc.) is open
- Select the BLE-MIDI device as MIDI input in your DAW
- Ensure a software instrument track is selected
- Check that the MIDI channel matches (default: channel 0)

### High latency

- Request lower connection interval in code
- Check that the host supports low-latency connections
- macOS typically negotiates 15-30ms even when 7.5ms is requested

## License

MIT License - See LICENSE file for details.

## Credits

- Based on BTstack Bluetooth stack
- Raspberry Pi Pico SDK
- BLE-MIDI specification by the MIDI Manufacturers Association
