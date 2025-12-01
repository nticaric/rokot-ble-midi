/**
 * @file rokot_ble_midi.c
 * @brief RokoT BLE-MIDI Library Implementation
 */

#include "rokot_ble_midi.h"

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

#include "btstack.h"
#include "ble/att_db.h"
#include "ble/att_server.h"

#include "rokot_ble_midi_service.h"

// ---------------------------------------------------------------------------
// Internal State
// ---------------------------------------------------------------------------

static struct {
  hci_con_handle_t con_handle;
  bool notifications_enabled;
  bool battery_notifications_enabled;
  uint16_t connection_interval;
  rokot_ble_midi_callback_t rx_callback;
  btstack_packet_callback_registration_t hci_event_callback_registration;
  char device_name[32];
  char manufacturer[32];
  char firmware_version[16];
  uint8_t battery_level;
  bool initialized;
} ble_midi_state = {
  .con_handle = HCI_CON_HANDLE_INVALID,
  .notifications_enabled = false,
  .battery_notifications_enabled = false,
  .connection_interval = 0,
  .rx_callback = NULL,
  .manufacturer = ROKOT_BLE_MIDI_MANUFACTURER,
  .firmware_version = ROKOT_BLE_MIDI_FIRMWARE_VERSION,
  .battery_level = 100,
  .initialized = false,
};

// ---------------------------------------------------------------------------
// Advertising Data
// ---------------------------------------------------------------------------

#define APP_AD_FLAGS 0x06

static uint8_t adv_data[] = {
  0x02, BLUETOOTH_DATA_TYPE_FLAGS, APP_AD_FLAGS,
  0x11, BLUETOOTH_DATA_TYPE_COMPLETE_LIST_OF_128_BIT_SERVICE_CLASS_UUIDS,
  0x00, 0xC7, 0xC4, 0x4E, 0xE3, 0x6C, 0x51, 0xA7,
  0x33, 0x4B, 0xE8, 0xED, 0x5A, 0x0E, 0xB8, 0x03,
};

static uint8_t scan_resp_data[32];
static uint8_t scan_resp_data_len = 0;

static void build_scan_response(const char *name) {
  size_t name_len = strlen(name);
  if (name_len > 29) name_len = 29;
  scan_resp_data[0] = (uint8_t)(name_len + 1);
  scan_resp_data[1] = BLUETOOTH_DATA_TYPE_COMPLETE_LOCAL_NAME;
  memcpy(&scan_resp_data[2], name, name_len);
  scan_resp_data_len = (uint8_t)(name_len + 2);
}

// ---------------------------------------------------------------------------
// BLE-MIDI Packet Encoding
// ---------------------------------------------------------------------------

static uint16_t encode_ble_midi_packet(uint8_t *dst, const uint8_t *midi, uint8_t midi_len) {
  dst[0] = 0x80;
  dst[1] = 0x80;
  memcpy(&dst[2], midi, midi_len);
  return midi_len + 2;
}

static int send_midi_internal(const uint8_t *midi, uint8_t len) {
  if (!ble_midi_state.notifications_enabled || ble_midi_state.con_handle == HCI_CON_HANDLE_INVALID)
    return -1;

  uint8_t packet[32];
  uint16_t packet_len = encode_ble_midi_packet(packet, midi, len);

  if (!att_server_can_send_packet_now(ble_midi_state.con_handle))
    return -2;

  int result = att_server_notify(ble_midi_state.con_handle,
      ATT_CHARACTERISTIC_7772E5DB_3868_4112_A1A9_F2669D106BF3_01_VALUE_HANDLE, packet, packet_len);
  return (result == 0) ? 0 : -3;
}

// ---------------------------------------------------------------------------
// ATT Callbacks
// ---------------------------------------------------------------------------

static uint16_t att_read_callback(hci_con_handle_t connection_handle, uint16_t att_handle,
                                  uint16_t offset, uint8_t *buffer, uint16_t buffer_size) {
  UNUSED(connection_handle);

  // Manufacturer Name
  if (att_handle == ATT_CHARACTERISTIC_ORG_BLUETOOTH_CHARACTERISTIC_MANUFACTURER_NAME_STRING_01_VALUE_HANDLE) {
    return att_read_callback_handle_blob((const uint8_t *)ble_midi_state.manufacturer,
        strlen(ble_midi_state.manufacturer), offset, buffer, buffer_size);
  }

  // Firmware Revision
  if (att_handle == ATT_CHARACTERISTIC_ORG_BLUETOOTH_CHARACTERISTIC_FIRMWARE_REVISION_STRING_01_VALUE_HANDLE) {
    return att_read_callback_handle_blob((const uint8_t *)ble_midi_state.firmware_version,
        strlen(ble_midi_state.firmware_version), offset, buffer, buffer_size);
  }

  // Battery Level
  if (att_handle == ATT_CHARACTERISTIC_ORG_BLUETOOTH_CHARACTERISTIC_BATTERY_LEVEL_01_VALUE_HANDLE) {
    return att_read_callback_handle_byte(ble_midi_state.battery_level, offset, buffer, buffer_size);
  }

  // BLE-MIDI
  if (att_handle == ATT_CHARACTERISTIC_7772E5DB_3868_4112_A1A9_F2669D106BF3_01_VALUE_HANDLE) {
    return 0;
  }

  return 0;
}

static int att_write_callback(hci_con_handle_t connection_handle, uint16_t att_handle,
                              uint16_t transaction_mode, uint16_t offset, uint8_t *buffer, uint16_t buffer_size) {
  UNUSED(transaction_mode);
  UNUSED(offset);

  // Battery CCCD
  if (att_handle == ATT_CHARACTERISTIC_ORG_BLUETOOTH_CHARACTERISTIC_BATTERY_LEVEL_01_CLIENT_CONFIGURATION_HANDLE) {
    ble_midi_state.battery_notifications_enabled =
        (little_endian_read_16(buffer, 0) == GATT_CLIENT_CHARACTERISTICS_CONFIGURATION_NOTIFICATION);
    ble_midi_state.con_handle = connection_handle;
    return 0;
  }

  // MIDI CCCD
  if (att_handle == ATT_CHARACTERISTIC_7772E5DB_3868_4112_A1A9_F2669D106BF3_01_CLIENT_CONFIGURATION_HANDLE) {
    ble_midi_state.notifications_enabled =
        (little_endian_read_16(buffer, 0) == GATT_CLIENT_CHARACTERISTICS_CONFIGURATION_NOTIFICATION);
    ble_midi_state.con_handle = connection_handle;
    return 0;
  }

  // Incoming MIDI
  if (att_handle == ATT_CHARACTERISTIC_7772E5DB_3868_4112_A1A9_F2669D106BF3_01_VALUE_HANDLE) {
    if (buffer_size > 2 && ble_midi_state.rx_callback) {
      if (buffer_size >= 5) ble_midi_state.rx_callback(buffer[2], buffer[3], buffer[4]);
      else if (buffer_size >= 4) ble_midi_state.rx_callback(buffer[2], buffer[3], 0);
    }
    return 0;
  }

  return 0;
}

// ---------------------------------------------------------------------------
// HCI Event Handler
// ---------------------------------------------------------------------------

static void packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size) {
  UNUSED(size);
  UNUSED(channel);

  if (packet_type != HCI_EVENT_PACKET) return;

  uint8_t event_type = hci_event_packet_get_type(packet);

  switch (event_type) {
  case BTSTACK_EVENT_STATE:
    if (btstack_event_state_get_state(packet) == HCI_STATE_WORKING) {
      bd_addr_t null_addr = {0};
      gap_advertisements_set_params(0x0020, 0x0040, 0, 0, null_addr, 0x07, 0x00);
      gap_advertisements_set_data(sizeof(adv_data), adv_data);
      gap_scan_response_set_data(scan_resp_data_len, scan_resp_data);
      gap_advertisements_enable(1);
    }
    break;

  case HCI_EVENT_LE_META:
    switch (hci_event_le_meta_get_subevent_code(packet)) {
    case HCI_SUBEVENT_LE_CONNECTION_COMPLETE:
      ble_midi_state.con_handle = hci_subevent_le_connection_complete_get_connection_handle(packet);
      ble_midi_state.connection_interval = hci_subevent_le_connection_complete_get_conn_interval(packet);
      gap_request_connection_parameter_update(ble_midi_state.con_handle,
          ROKOT_BLE_MIDI_CONN_INTERVAL_MIN, ROKOT_BLE_MIDI_CONN_INTERVAL_MAX, 0, 100);
      break;
    case HCI_SUBEVENT_LE_CONNECTION_UPDATE_COMPLETE:
      ble_midi_state.connection_interval = hci_subevent_le_connection_update_complete_get_conn_interval(packet);
      break;
    }
    break;

  case HCI_EVENT_DISCONNECTION_COMPLETE:
    ble_midi_state.con_handle = HCI_CON_HANDLE_INVALID;
    ble_midi_state.notifications_enabled = false;
    ble_midi_state.battery_notifications_enabled = false;
    ble_midi_state.connection_interval = 0;
    gap_advertisements_enable(1);
    break;
  }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

int rokot_ble_midi_init(const char *device_name) {
  if (ble_midi_state.initialized) return -1;

  strncpy(ble_midi_state.device_name, device_name, sizeof(ble_midi_state.device_name) - 1);
  ble_midi_state.device_name[sizeof(ble_midi_state.device_name) - 1] = '\0';
  build_scan_response(device_name);

  if (cyw43_arch_init()) return -2;

  l2cap_init();
  sm_init();
  att_server_init(profile_data, att_read_callback, att_write_callback);

  ble_midi_state.hci_event_callback_registration.callback = &packet_handler;
  hci_add_event_handler(&ble_midi_state.hci_event_callback_registration);
  att_server_register_packet_handler(packet_handler);

  hci_power_control(HCI_POWER_ON);
  ble_midi_state.initialized = true;
  return 0;
}

void rokot_ble_midi_deinit(void) {
  if (!ble_midi_state.initialized) return;
  hci_power_control(HCI_POWER_OFF);
  cyw43_arch_deinit();
  ble_midi_state.initialized = false;
  ble_midi_state.con_handle = HCI_CON_HANDLE_INVALID;
  ble_midi_state.notifications_enabled = false;
  ble_midi_state.battery_notifications_enabled = false;
}

void rokot_ble_midi_task(void) {
  if (!ble_midi_state.initialized) return;
  async_context_poll(cyw43_arch_async_context());
  async_context_wait_for_work_until(cyw43_arch_async_context(), make_timeout_time_ms(1));
}

rokot_ble_midi_state_t rokot_ble_midi_get_state(void) {
  if (ble_midi_state.notifications_enabled) return ROKOT_BLE_MIDI_READY;
  if (ble_midi_state.con_handle != HCI_CON_HANDLE_INVALID) return ROKOT_BLE_MIDI_CONNECTED;
  return ROKOT_BLE_MIDI_DISCONNECTED;
}

bool rokot_ble_midi_is_ready(void) {
  return ble_midi_state.notifications_enabled && ble_midi_state.con_handle != HCI_CON_HANDLE_INVALID;
}

bool rokot_ble_midi_is_connected(void) {
  return ble_midi_state.con_handle != HCI_CON_HANDLE_INVALID;
}

float rokot_ble_midi_get_connection_interval(void) {
  return ble_midi_state.connection_interval * 1.25f;
}

// Device Information
void rokot_ble_midi_set_manufacturer(const char *manufacturer) {
  strncpy(ble_midi_state.manufacturer, manufacturer, sizeof(ble_midi_state.manufacturer) - 1);
  ble_midi_state.manufacturer[sizeof(ble_midi_state.manufacturer) - 1] = '\0';
}

void rokot_ble_midi_set_firmware_version(const char *version) {
  strncpy(ble_midi_state.firmware_version, version, sizeof(ble_midi_state.firmware_version) - 1);
  ble_midi_state.firmware_version[sizeof(ble_midi_state.firmware_version) - 1] = '\0';
}

// Battery
void rokot_ble_midi_set_battery_level(uint8_t level) {
  if (level > 100) level = 100;
  ble_midi_state.battery_level = level;

  if (ble_midi_state.battery_notifications_enabled &&
      ble_midi_state.con_handle != HCI_CON_HANDLE_INVALID &&
      att_server_can_send_packet_now(ble_midi_state.con_handle)) {
    att_server_notify(ble_midi_state.con_handle,
        ATT_CHARACTERISTIC_ORG_BLUETOOTH_CHARACTERISTIC_BATTERY_LEVEL_01_VALUE_HANDLE, &level, 1);
  }
}

uint8_t rokot_ble_midi_get_battery_level(void) {
  return ble_midi_state.battery_level;
}

// MIDI
int rokot_ble_midi_note_on(uint8_t channel, uint8_t note, uint8_t velocity) {
  uint8_t midi[3] = {(uint8_t)(MIDI_NOTE_ON | (channel & 0x0F)), note & 0x7F, velocity & 0x7F};
  return send_midi_internal(midi, 3);
}

int rokot_ble_midi_note_off(uint8_t channel, uint8_t note) {
  uint8_t midi[3] = {(uint8_t)(MIDI_NOTE_OFF | (channel & 0x0F)), note & 0x7F, 0};
  return send_midi_internal(midi, 3);
}

int rokot_ble_midi_control_change(uint8_t channel, uint8_t controller, uint8_t value) {
  uint8_t midi[3] = {(uint8_t)(MIDI_CONTROL_CHANGE | (channel & 0x0F)), controller & 0x7F, value & 0x7F};
  return send_midi_internal(midi, 3);
}

int rokot_ble_midi_program_change(uint8_t channel, uint8_t program) {
  uint8_t midi[2] = {(uint8_t)(MIDI_PROGRAM_CHANGE | (channel & 0x0F)), program & 0x7F};
  return send_midi_internal(midi, 2);
}

int rokot_ble_midi_pitch_bend(uint8_t channel, int16_t value) {
  uint16_t bend = (uint16_t)(value + 8192);
  uint8_t midi[3] = {(uint8_t)(MIDI_PITCH_BEND | (channel & 0x0F)), (uint8_t)(bend & 0x7F), (uint8_t)((bend >> 7) & 0x7F)};
  return send_midi_internal(midi, 3);
}

int rokot_ble_midi_channel_pressure(uint8_t channel, uint8_t pressure) {
  uint8_t midi[2] = {(uint8_t)(MIDI_CHANNEL_PRESSURE | (channel & 0x0F)), pressure & 0x7F};
  return send_midi_internal(midi, 2);
}

int rokot_ble_midi_send_raw(const uint8_t *data, uint8_t len) {
  if (len == 0 || len > 3) return -1;
  return send_midi_internal(data, len);
}

void rokot_ble_midi_set_callback(rokot_ble_midi_callback_t callback) {
  ble_midi_state.rx_callback = callback;
}
