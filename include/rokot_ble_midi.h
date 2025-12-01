/**
 * @file rokot_ble_midi.h
 * @brief RokoT BLE-MIDI Library for Raspberry Pi Pico (RP2350/RP2040 + CYW43)
 *
 * A simple, easy-to-use BLE-MIDI library for creating wireless MIDI controllers
 * using the Raspberry Pi Pico W, Pico 2 W, or custom boards with Radio Module 2.
 *
 * @author RokoT
 * @license BSD-3-Clause
 */

#ifndef ROKOT_BLE_MIDI_H
#define ROKOT_BLE_MIDI_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

// ---------------------------------------------------------------------------
// Configuration (can be overridden before including this header)
// ---------------------------------------------------------------------------

/**
 * @brief SPI clock divider for CYW43 communication
 *
 * The SPI clock is derived from the system clock (typically 150 MHz).
 * - Divider 2 = 75 MHz (default for Pico W, may not work on custom boards)
 * - Divider 3 = 50 MHz (recommended for custom boards with Radio Module 2)
 * - Divider 4 = 37.5 MHz (conservative, very reliable)
 *
 * Set this in your CMakeLists.txt:
 *   add_compile_definitions(ROKOT_BLE_MIDI_SPI_CLK_DIV=3)
 */
#ifndef ROKOT_BLE_MIDI_SPI_CLK_DIV
#define ROKOT_BLE_MIDI_SPI_CLK_DIV 3
#endif

/**
 * @brief BLE connection interval minimum (in 1.25ms units)
 *
 * Lower values = lower latency but higher power consumption.
 * - 6 = 7.5ms (BLE minimum, best for real-time MIDI)
 * - 12 = 15ms (good balance)
 * - 24 = 30ms (power saving)
 */
#ifndef ROKOT_BLE_MIDI_CONN_INTERVAL_MIN
#define ROKOT_BLE_MIDI_CONN_INTERVAL_MIN 6
#endif

#ifndef ROKOT_BLE_MIDI_CONN_INTERVAL_MAX
#define ROKOT_BLE_MIDI_CONN_INTERVAL_MAX 12
#endif

  // ---------------------------------------------------------------------------
  // Types
  // ---------------------------------------------------------------------------

  /**
   * @brief Callback for received MIDI data
   * @param status MIDI status byte (e.g., 0x90 for Note On)
   * @param data1 First data byte (e.g., note number)
   * @param data2 Second data byte (e.g., velocity)
   */
  typedef void (*rokot_ble_midi_callback_t)(uint8_t status, uint8_t data1, uint8_t data2);

  /**
   * @brief Connection state
   */
  typedef enum
  {
    ROKOT_BLE_MIDI_DISCONNECTED = 0,
    ROKOT_BLE_MIDI_CONNECTED,
    ROKOT_BLE_MIDI_READY // Connected and notifications enabled
  } rokot_ble_midi_state_t;

  // ---------------------------------------------------------------------------
  // Initialization
  // ---------------------------------------------------------------------------

  /**
   * @brief Initialize the BLE-MIDI library
   *
   * This initializes the CYW43 driver, BTstack, and starts advertising.
   *
   * @param device_name The name to advertise (max 20 characters recommended)
   * @return 0 on success, negative error code on failure
   */
  int rokot_ble_midi_init(const char *device_name);

  /**
   * @brief Deinitialize the BLE-MIDI library
   */
  void rokot_ble_midi_deinit(void);

  // ---------------------------------------------------------------------------
  // Main Loop
  // ---------------------------------------------------------------------------

  /**
   * @brief Process BLE-MIDI events
   *
   * Call this regularly in your main loop. This handles all BLE communication.
   * Uses non-blocking async context polling.
   */
  void rokot_ble_midi_task(void);

  // ---------------------------------------------------------------------------
  // Status
  // ---------------------------------------------------------------------------

  /**
   * @brief Get the current connection state
   * @return Current state (DISCONNECTED, CONNECTED, or READY)
   */
  rokot_ble_midi_state_t rokot_ble_midi_get_state(void);

  /**
   * @brief Check if connected and ready to send MIDI
   * @return true if notifications are enabled and MIDI can be sent
   */
  bool rokot_ble_midi_is_ready(void);

  /**
   * @brief Check if a device is connected
   * @return true if a BLE central is connected
   */
  bool rokot_ble_midi_is_connected(void);

  /**
   * @brief Get the current connection interval in milliseconds
   * @return Connection interval in ms, or 0 if not connected
   */
  float rokot_ble_midi_get_connection_interval(void);

  // ---------------------------------------------------------------------------
  // Battery Service
  // ---------------------------------------------------------------------------

  /**
   * @brief Set the battery level (0-100%)
   *
   * This updates the Battery Level characteristic. If a device is connected
   * and has enabled notifications, it will be notified of the change.
   *
   * @param level Battery level percentage (0-100)
   */
  void rokot_ble_midi_set_battery_level(uint8_t level);

  /**
   * @brief Get the current battery level
   * @return Battery level percentage (0-100)
   */
  uint8_t rokot_ble_midi_get_battery_level(void);

  // ---------------------------------------------------------------------------
  // Sending MIDI Messages
  // ---------------------------------------------------------------------------

  /**
   * @brief Send a Note On message
   * @param channel MIDI channel (0-15)
   * @param note Note number (0-127)
   * @param velocity Velocity (0-127, 0 = note off)
   * @return 0 on success, negative on error
   */
  int rokot_ble_midi_note_on(uint8_t channel, uint8_t note, uint8_t velocity);

  /**
   * @brief Send a Note Off message
   * @param channel MIDI channel (0-15)
   * @param note Note number (0-127)
   * @return 0 on success, negative on error
   */
  int rokot_ble_midi_note_off(uint8_t channel, uint8_t note);

  /**
   * @brief Send a Control Change message
   * @param channel MIDI channel (0-15)
   * @param controller Controller number (0-127)
   * @param value Controller value (0-127)
   * @return 0 on success, negative on error
   */
  int rokot_ble_midi_control_change(uint8_t channel, uint8_t controller, uint8_t value);

  /**
   * @brief Send a Program Change message
   * @param channel MIDI channel (0-15)
   * @param program Program number (0-127)
   * @return 0 on success, negative on error
   */
  int rokot_ble_midi_program_change(uint8_t channel, uint8_t program);

  /**
   * @brief Send a Pitch Bend message
   * @param channel MIDI channel (0-15)
   * @param value Pitch bend value (-8192 to +8191, 0 = center)
   * @return 0 on success, negative on error
   */
  int rokot_ble_midi_pitch_bend(uint8_t channel, int16_t value);

  /**
   * @brief Send a Channel Pressure (Aftertouch) message
   * @param channel MIDI channel (0-15)
   * @param pressure Pressure value (0-127)
   * @return 0 on success, negative on error
   */
  int rokot_ble_midi_channel_pressure(uint8_t channel, uint8_t pressure);

  /**
   * @brief Send a raw MIDI message (1-3 bytes)
   * @param data Pointer to MIDI data
   * @param len Length of data (1-3 bytes)
   * @return 0 on success, negative on error
   */
  int rokot_ble_midi_send_raw(const uint8_t *data, uint8_t len);

  // ---------------------------------------------------------------------------
  // Receiving MIDI Messages
  // ---------------------------------------------------------------------------

  /**
   * @brief Set callback for received MIDI messages
   * @param callback Function to call when MIDI is received, or NULL to disable
   */
  void rokot_ble_midi_set_callback(rokot_ble_midi_callback_t callback);

  // ---------------------------------------------------------------------------
  // MIDI Constants (for convenience)
  // ---------------------------------------------------------------------------

#define MIDI_NOTE_OFF 0x80
#define MIDI_NOTE_ON 0x90
#define MIDI_POLY_PRESSURE 0xA0
#define MIDI_CONTROL_CHANGE 0xB0
#define MIDI_PROGRAM_CHANGE 0xC0
#define MIDI_CHANNEL_PRESSURE 0xD0
#define MIDI_PITCH_BEND 0xE0

// Common CC numbers
#define MIDI_CC_MOD_WHEEL 1
#define MIDI_CC_BREATH 2
#define MIDI_CC_VOLUME 7
#define MIDI_CC_PAN 10
#define MIDI_CC_EXPRESSION 11
#define MIDI_CC_SUSTAIN 64
#define MIDI_CC_ALL_NOTES_OFF 123

// Note names (octave 4)
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
