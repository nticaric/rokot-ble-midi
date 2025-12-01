/**
 * @file rokot_ble_midi.h
 * @brief RokoT BLE-MIDI Library for Raspberry Pi Pico (RP2350/RP2040 + CYW43)
 */

#ifndef ROKOT_BLE_MIDI_H
#define ROKOT_BLE_MIDI_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Configuration Defaults
// ---------------------------------------------------------------------------

#ifndef ROKOT_BLE_MIDI_SPI_CLK_DIV
#define ROKOT_BLE_MIDI_SPI_CLK_DIV 3
#endif

#ifndef ROKOT_BLE_MIDI_CONN_INTERVAL_MIN
#define ROKOT_BLE_MIDI_CONN_INTERVAL_MIN 6
#endif

#ifndef ROKOT_BLE_MIDI_CONN_INTERVAL_MAX
#define ROKOT_BLE_MIDI_CONN_INTERVAL_MAX 12
#endif

// Device Information Defaults
#ifndef ROKOT_BLE_MIDI_MANUFACTURER
#define ROKOT_BLE_MIDI_MANUFACTURER "RokoT"
#endif

#ifndef ROKOT_BLE_MIDI_FIRMWARE_VERSION
#define ROKOT_BLE_MIDI_FIRMWARE_VERSION "1.0.0"
#endif

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

typedef void (*rokot_ble_midi_callback_t)(uint8_t status, uint8_t data1, uint8_t data2);

typedef enum {
  ROKOT_BLE_MIDI_DISCONNECTED = 0,
  ROKOT_BLE_MIDI_CONNECTED,
  ROKOT_BLE_MIDI_READY
} rokot_ble_midi_state_t;

// ---------------------------------------------------------------------------
// Initialization
// ---------------------------------------------------------------------------

int rokot_ble_midi_init(const char *device_name);
void rokot_ble_midi_deinit(void);

// ---------------------------------------------------------------------------
// Main Loop
// ---------------------------------------------------------------------------

void rokot_ble_midi_task(void);

// ---------------------------------------------------------------------------
// Status
// ---------------------------------------------------------------------------

rokot_ble_midi_state_t rokot_ble_midi_get_state(void);
bool rokot_ble_midi_is_ready(void);
bool rokot_ble_midi_is_connected(void);
float rokot_ble_midi_get_connection_interval(void);

// ---------------------------------------------------------------------------
// Device Information
// ---------------------------------------------------------------------------

void rokot_ble_midi_set_manufacturer(const char *manufacturer);
void rokot_ble_midi_set_firmware_version(const char *version);

// ---------------------------------------------------------------------------
// Battery Service
// ---------------------------------------------------------------------------

void rokot_ble_midi_set_battery_level(uint8_t level);
uint8_t rokot_ble_midi_get_battery_level(void);

// ---------------------------------------------------------------------------
// Sending MIDI Messages
// ---------------------------------------------------------------------------

int rokot_ble_midi_note_on(uint8_t channel, uint8_t note, uint8_t velocity);
int rokot_ble_midi_note_off(uint8_t channel, uint8_t note);
int rokot_ble_midi_control_change(uint8_t channel, uint8_t controller, uint8_t value);
int rokot_ble_midi_program_change(uint8_t channel, uint8_t program);
int rokot_ble_midi_pitch_bend(uint8_t channel, int16_t value);
int rokot_ble_midi_channel_pressure(uint8_t channel, uint8_t pressure);
int rokot_ble_midi_send_raw(const uint8_t *data, uint8_t len);

// ---------------------------------------------------------------------------
// Receiving MIDI Messages
// ---------------------------------------------------------------------------

void rokot_ble_midi_set_callback(rokot_ble_midi_callback_t callback);

// ---------------------------------------------------------------------------
// MIDI Constants
// ---------------------------------------------------------------------------

#define MIDI_NOTE_OFF 0x80
#define MIDI_NOTE_ON 0x90
#define MIDI_POLY_PRESSURE 0xA0
#define MIDI_CONTROL_CHANGE 0xB0
#define MIDI_PROGRAM_CHANGE 0xC0
#define MIDI_CHANNEL_PRESSURE 0xD0
#define MIDI_PITCH_BEND 0xE0

#define MIDI_CC_MOD_WHEEL 1
#define MIDI_CC_BREATH 2
#define MIDI_CC_VOLUME 7
#define MIDI_CC_PAN 10
#define MIDI_CC_EXPRESSION 11
#define MIDI_CC_SUSTAIN 64
#define MIDI_CC_ALL_NOTES_OFF 123

#define MIDI_NOTE_C4 60
#define MIDI_NOTE_CS4 61
#define MIDI_NOTE_D4 62
#define MIDI_NOTE_DS4 63
#define MIDI_NOTE_E4 64
#define MIDI_NOTE_F4 65
#define MIDI_NOTE_FS4 66
#define MIDI_NOTE_G4 67
#define MIDI_NOTE_GS4 68
#define MIDI_NOTE_A4 69
#define MIDI_NOTE_AS4 70
#define MIDI_NOTE_B4 71

#ifdef __cplusplus
}
#endif

#endif // ROKOT_BLE_MIDI_H
