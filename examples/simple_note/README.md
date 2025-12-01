# Simple Note Example

This example demonstrates basic BLE-MIDI functionality - playing a note every second.

## Building

1. Set your Pico SDK path:
   ```bash
   export PICO_SDK_PATH=/path/to/pico-sdk
   ```

2. Create build directory and configure:
   ```bash
   mkdir build
   cd build
   cmake -G Ninja ..
   ```

3. Build:
   ```bash
   ninja
   ```

4. Flash:
   ```bash
   picotool load simple_note_example.elf -fx
   ```
   
   Or if the device doesn't auto-reboot:
   - Unplug USB
   - Hold BOOTSEL button
   - Plug in USB while holding BOOTSEL
   - Release BOOTSEL
   - Drag `simple_note_example.uf2` to the RPI-RP2 drive

## Testing on macOS

1. Open **Audio MIDI Setup** (in /Applications/Utilities)
2. Go to **Window → Show MIDI Studio**
3. Click the **Bluetooth** icon in the toolbar
4. Connect to "RokoT MIDI"
5. Open **GarageBand** with a software instrument
6. You should hear C4 notes every second!

## Serial Output

```bash
screen /dev/tty.usbmodem* 115200
```

You'll see:
```
=================================
RokoT BLE-MIDI Simple Note Example
=================================

Initializing BLE-MIDI...
BLE-MIDI initialized successfully!
Device name: RokoT MIDI
Waiting for BLE connection...

>>> Connected! Starting MIDI playback...
Note ON:  C4 (note=60, velocity=100) - OK
Note OFF: C4 (note=60) - OK
Note ON:  C4 (note=60, velocity=100) - OK
...
```
