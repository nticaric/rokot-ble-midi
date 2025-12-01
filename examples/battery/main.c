/**
 * @file main.c
 * @brief Battery Example - BLE-MIDI with Battery Service
 *
 * Demonstrates the rokot-ble-midi library with Battery Service.
 * - Advertises as "RokoTMidi BLE"
 * - Reports battery level as 50%
 * - Plays a note when connected (like simple_note example)
 */

#include <stdio.h>
#include "pico/stdlib.h"
#include "rokot_ble_midi.h"

#define DEVICE_NAME "RokoTMidi BLE"
#define BATTERY_LEVEL 50  // 50%

int main(void)
{
    stdio_init_all();
    sleep_ms(2000);  // Wait for USB serial

    printf("\n\n");
    printf("=========================================\n");
    printf("  RokoT BLE-MIDI - Battery Example\n");
    printf("=========================================\n\n");

    // Initialize BLE-MIDI
    printf("Initializing BLE-MIDI...\n");
    if (rokot_ble_midi_init(DEVICE_NAME) != 0)
    {
        printf("Failed to initialize BLE-MIDI!\n");
        return -1;
    }
    printf("BLE-MIDI initialized successfully\n");
    printf("Device name: %s\n", DEVICE_NAME);

    // Set battery level to 50%
    rokot_ble_midi_set_battery_level(BATTERY_LEVEL);
    printf("Battery level set to: %d%%\n\n", BATTERY_LEVEL);
    printf("Waiting for connection...\n\n");

    // Track state for note playing
    bool note_on = false;
    uint32_t last_note_time = 0;

    // Main loop
    while (true)
    {
        // Process BLE events
        rokot_ble_midi_task();

        // Get current time
        uint32_t now = to_ms_since_boot(get_absolute_time());

        // Play notes every second when ready
        if (rokot_ble_midi_is_ready() && (now - last_note_time >= 1000))
        {
            if (note_on)
            {
                rokot_ble_midi_note_off(0, MIDI_NOTE_C4);
                printf("Note Off: C4\n");
                note_on = false;
            }
            else
            {
                rokot_ble_midi_note_on(0, MIDI_NOTE_C4, 100);
                printf("Note On: C4, velocity 100\n");
                note_on = true;
            }
            last_note_time = now;
        }

        // Print status periodically
        static uint32_t last_status = 0;
        if (now - last_status >= 5000)
        {
            rokot_ble_midi_state_t state = rokot_ble_midi_get_state();
            const char *state_str = (state == ROKOT_BLE_MIDI_READY) ? "READY" :
                                    (state == ROKOT_BLE_MIDI_CONNECTED) ? "CONNECTED" : "DISCONNECTED";
            printf("Status: %s, Battery: %d%%\n", state_str, rokot_ble_midi_get_battery_level());
            last_status = now;
        }
    }

    return 0;
}
