/**
 * RokoT BLE-MIDI Library - Simple Note Example
 *
 * This example demonstrates basic BLE-MIDI usage:
 * - Initialize BLE-MIDI with a device name
 * - Play a note every second when connected
 *
 * To test:
 * 1. Flash to your RP2350 + Radio Module 2 board
 * 2. Open Audio MIDI Setup on macOS → Window → Show MIDI Studio
 * 3. Click Bluetooth icon and connect to "RokoT MIDI"
 * 4. Open GarageBand with a software instrument
 * 5. You should hear C4 notes playing every second
 */

#include <stdio.h>
#include "pico/stdlib.h"
#include "rokot_ble_midi.h"

#define DEVICE_NAME "RokoT MIDI"
#define NOTE_INTERVAL_MS 1000

int main()
{
  // Initialize stdio for USB serial output
  stdio_init_all();

  // Wait a moment for USB to enumerate
  sleep_ms(1000);

  printf("=================================\n");
  printf("RokoT BLE-MIDI Simple Note Example\n");
  printf("=================================\n\n");

  // Initialize BLE-MIDI
  printf("Initializing BLE-MIDI...\n");
  if (rokot_ble_midi_init(DEVICE_NAME) != 0)
  {
    printf("ERROR: BLE-MIDI initialization failed!\n");
    printf("Check your hardware connections.\n");
    while (1)
    {
      tight_loop_contents();
    }
  }

  printf("BLE-MIDI initialized successfully!\n");
  printf("Device name: %s\n", DEVICE_NAME);
  printf("Waiting for BLE connection...\n\n");

  uint32_t last_note_time = 0;
  bool note_on = false;
  bool was_connected = false;

  while (true)
  {
    // Run the BLE-MIDI background task
    // This must be called regularly to handle BLE events
    rokot_ble_midi_task();

    bool is_connected = rokot_ble_midi_is_connected();

    // Print connection status changes
    if (is_connected && !was_connected)
    {
      printf(">>> Connected! Starting MIDI playback...\n");
    }
    else if (!is_connected && was_connected)
    {
      printf(">>> Disconnected. Waiting for connection...\n");
      note_on = false; // Reset note state
    }
    was_connected = is_connected;

    // Only send MIDI when connected
    if (is_connected)
    {
      uint32_t now = to_ms_since_boot(get_absolute_time());

      // Toggle note on/off every second
      if (now - last_note_time >= NOTE_INTERVAL_MS)
      {
        last_note_time = now;
        note_on = !note_on;

        if (note_on)
        {
          // Send Note On for middle C (C4) with velocity 100
          int result = rokot_ble_midi_note_on(0, MIDI_NOTE_C4, 100);
          printf("Note ON:  C4 (note=%d, velocity=100) - %s\n",
                 MIDI_NOTE_C4, result == 0 ? "OK" : "FAILED");
        }
        else
        {
          // Send Note Off
          int result = rokot_ble_midi_note_off(0, MIDI_NOTE_C4);
          printf("Note OFF: C4 (note=%d) - %s\n",
                 MIDI_NOTE_C4, result == 0 ? "OK" : "FAILED");
        }
      }
    }
  }

  return 0;
}
