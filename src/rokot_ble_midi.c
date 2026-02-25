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

static struct
{
  hci_con_handle_t con_handle;
  bool notifications_enabled;
  bool battery_notifications_enabled;
  uint16_t connection_interval;
  rokot_ble_midi_callback_t rx_callback;
  rokot_ble_midi_raw_callback_t rx_raw_callback;
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
    .rx_raw_callback = NULL,
    .manufacturer = ROKOT_BLE_MIDI_MANUFACTURER,
    .firmware_version = ROKOT_BLE_MIDI_FIRMWARE_VERSION,
    .battery_level = 100,
    .initialized = false,
};

// ---------------------------------------------------------------------------
// BLE-MIDI TX Queue
// ---------------------------------------------------------------------------

#define BLE_MIDI_TX_QUEUE_SIZE 64
#define BLE_MIDI_TX_SYSEX_MAX_LEN 1024
#define BLE_MIDI_RX_SYSEX_MAX_LEN 1024

typedef struct
{
  uint8_t len;
  uint8_t data[3];
} ble_midi_tx_item_t;

static ble_midi_tx_item_t tx_queue[BLE_MIDI_TX_QUEUE_SIZE];
static uint8_t tx_head = 0;
static uint8_t tx_tail = 0;
static uint8_t tx_count = 0;
static bool tx_all_notes_off_pending = false;
static uint8_t tx_sysex_buffer[BLE_MIDI_TX_SYSEX_MAX_LEN];
static uint16_t tx_sysex_len = 0;
static uint16_t tx_sysex_pos = 0;
static bool tx_sysex_active = false;
static uint8_t rx_sysex_buffer[BLE_MIDI_RX_SYSEX_MAX_LEN];
static uint16_t rx_sysex_len = 0;
static bool rx_sysex_active = false;
static bool rx_sysex_overflow = false;

static bool tx_queue_is_empty(void)
{
  return tx_count == 0;
}

static bool tx_queue_is_full(void)
{
  return tx_count >= BLE_MIDI_TX_QUEUE_SIZE;
}

static int tx_queue_push(const uint8_t *midi, uint8_t len)
{
  if (!midi || len == 0 || len > 3)
    return -1;
  if (tx_queue_is_full())
    return -2;

  tx_queue[tx_tail].len = len;
  memcpy(tx_queue[tx_tail].data, midi, len);
  tx_tail = (uint8_t)((tx_tail + 1) % BLE_MIDI_TX_QUEUE_SIZE);
  tx_count++;
  return 0;
}

static const ble_midi_tx_item_t *tx_queue_peek_at(uint8_t offset)
{
  if (offset >= tx_count)
    return NULL;
  uint8_t idx = (uint8_t)((tx_head + offset) % BLE_MIDI_TX_QUEUE_SIZE);
  return &tx_queue[idx];
}

static void tx_queue_drop(uint8_t n)
{
  if (n > tx_count)
    n = tx_count;
  tx_head = (uint8_t)((tx_head + n) % BLE_MIDI_TX_QUEUE_SIZE);
  tx_count -= n;
}

// ---------------------------------------------------------------------------
// Advertising Data
// ---------------------------------------------------------------------------

#define APP_AD_FLAGS 0x06

static uint8_t adv_data[] = {
    0x02,
    BLUETOOTH_DATA_TYPE_FLAGS,
    APP_AD_FLAGS,
    0x11,
    BLUETOOTH_DATA_TYPE_COMPLETE_LIST_OF_128_BIT_SERVICE_CLASS_UUIDS,
    0x00,
    0xC7,
    0xC4,
    0x4E,
    0xE3,
    0x6C,
    0x51,
    0xA7,
    0x33,
    0x4B,
    0xE8,
    0xED,
    0x5A,
    0x0E,
    0xB8,
    0x03,
};

static uint8_t scan_resp_data[32];
static uint8_t scan_resp_data_len = 0;

static void build_scan_response(const char *name)
{
  size_t name_len = strlen(name);
  if (name_len > 29)
    name_len = 29;
  scan_resp_data[0] = (uint8_t)(name_len + 1);
  scan_resp_data[1] = BLUETOOTH_DATA_TYPE_COMPLETE_LOCAL_NAME;
  memcpy(&scan_resp_data[2], name, name_len);
  scan_resp_data_len = (uint8_t)(name_len + 2);
}

// ---------------------------------------------------------------------------
// BLE-MIDI Packet Encoding
// ---------------------------------------------------------------------------

// Get current 13-bit BLE-MIDI timestamp (milliseconds, wraps at 8191).
static uint16_t ble_midi_timestamp(void)
{
  return (uint16_t)((time_us_64() / 1000) & 0x1FFF);
}

// Encode the BLE-MIDI header byte (bit7=1, bit6=0, bits5-0 = timestampHigh).
static uint8_t ble_midi_header_byte(uint16_t ts13)
{
  return 0x80 | ((ts13 >> 7) & 0x3F);
}

// Encode a BLE-MIDI timestamp byte (bit7=1, bits6-0 = timestampLow).
static uint8_t ble_midi_ts_byte(uint16_t ts13)
{
  return 0x80 | (ts13 & 0x7F);
}

static bool is_system_realtime_status(uint8_t status)
{
  return status >= 0xF8;
}

static uint8_t midi_status_len(uint8_t status)
{
  if ((status & 0x80) == 0)
    return 0;

  if ((status & 0xF0) < 0xF0)
  {
    switch (status & 0xF0)
    {
    case MIDI_NOTE_OFF:
    case MIDI_NOTE_ON:
    case MIDI_POLY_PRESSURE:
    case MIDI_CONTROL_CHANGE:
    case MIDI_PITCH_BEND:
      return 3;
    case MIDI_PROGRAM_CHANGE:
    case MIDI_CHANNEL_PRESSURE:
      return 2;
    default:
      return 0;
    }
  }

  switch (status)
  {
  case 0xF0: // SysEx start (variable)
    return 0;
  case 0xF1: // MTC quarter frame
  case 0xF3: // Song select
    return 2;
  case 0xF2: // Song position pointer
    return 3;
  case 0xF6: // Tune request
  case 0xF7: // EOX (standalone)
    return 1;
  default:
    if (status >= 0xF8)
      return 1; // Real-time
    return 0;
  }
}

static void dispatch_midi_message(const uint8_t *msg, uint8_t len)
{
  if (!msg || len == 0)
    return;

  if (ble_midi_state.rx_raw_callback)
    ble_midi_state.rx_raw_callback(msg, len);

  if (ble_midi_state.rx_callback)
  {
    uint8_t data1 = (len >= 2) ? msg[1] : 0;
    uint8_t data2 = (len >= 3) ? msg[2] : 0;
    ble_midi_state.rx_callback(msg[0], data1, data2);
  }
}

static void rx_sysex_reset(void)
{
  rx_sysex_len = 0;
  rx_sysex_active = false;
  rx_sysex_overflow = false;
}

static void rx_sysex_begin(void)
{
  rx_sysex_len = 0;
  rx_sysex_active = true;
  rx_sysex_overflow = false;
  if (rx_sysex_len < sizeof(rx_sysex_buffer))
  {
    rx_sysex_buffer[rx_sysex_len++] = 0xF0;
  }
  else
  {
    rx_sysex_overflow = true;
  }
}

static void rx_sysex_append_byte(uint8_t b)
{
  if (!rx_sysex_active || rx_sysex_overflow)
    return;

  if (rx_sysex_len >= sizeof(rx_sysex_buffer))
  {
    rx_sysex_overflow = true;
    return;
  }

  rx_sysex_buffer[rx_sysex_len++] = b;
}

static void rx_sysex_end(void)
{
  if (rx_sysex_active && !rx_sysex_overflow && ble_midi_state.rx_raw_callback && rx_sysex_len > 0)
    ble_midi_state.rx_raw_callback(rx_sysex_buffer, rx_sysex_len);
  rx_sysex_reset();
}

static bool sysex_payload_is_valid(const uint8_t *data, uint16_t len)
{
  if (!data || len < 2 || data[0] != 0xF0 || data[len - 1] != 0xF7)
    return false;

  for (uint16_t i = 1; i + 1 < len; i++)
  {
    uint8_t b = data[i];
    if ((b & 0x80) && !is_system_realtime_status(b))
      return false;
  }

  return true;
}

static int send_sysex_internal(const uint8_t *data, uint16_t len)
{
  if (!ble_midi_state.notifications_enabled || ble_midi_state.con_handle == HCI_CON_HANDLE_INVALID)
    return -1;
  if (len > BLE_MIDI_TX_SYSEX_MAX_LEN || !sysex_payload_is_valid(data, len))
    return -2;
  if (tx_sysex_active)
    return -3;

  memcpy(tx_sysex_buffer, data, len);
  tx_sysex_len = len;
  tx_sysex_pos = 0;
  tx_sysex_active = true;
  return 0;
}

static int send_midi_internal(const uint8_t *midi, uint8_t len)
{
  if (!ble_midi_state.notifications_enabled || ble_midi_state.con_handle == HCI_CON_HANDLE_INVALID)
    return -1;
  if (len == 0 || len > 3 || !midi)
    return -2;

  // While SysEx is being transmitted, only System Real-Time is allowed.
  if (tx_sysex_active && !(len == 1 && is_system_realtime_status(midi[0])))
    return -3;

  // Always enqueue — the task loop batches multiple messages per BLE packet.
  if (tx_queue_push(midi, len) != 0)
  {
    // Queue overflow: request an All Notes Off as a fail-safe.
    tx_all_notes_off_pending = true;
    return -4;
  }

  return 0;
}

// Parse a raw MIDI byte stream (no BLE-MIDI framing, for WebBluetooth compat).
static void decode_raw_midi(const uint8_t *buffer, uint16_t size)
{
  uint8_t running_status = 0;
  uint8_t msg[3];
  uint8_t msg_len = 0;
  uint8_t msg_pos = 0;

  for (uint16_t i = 0; i < size; i++)
  {
    uint8_t b = buffer[i];
    if (b & 0x80)
    {
      if (b == 0xF0)
      {
        rx_sysex_begin();
        continue;
      }

      if (rx_sysex_active)
      {
        if (b == 0xF7)
        {
          rx_sysex_append_byte(0xF7);
          rx_sysex_end();
          continue;
        }

        if (is_system_realtime_status(b))
        {
          dispatch_midi_message(&b, 1);
          continue;
        }

        rx_sysex_reset();
      }

      msg_len = midi_status_len(b);
      if (!msg_len)
        continue;

      if (msg_len == 1)
      {
        dispatch_midi_message(&b, 1);
        continue;
      }

      if (b < 0xF0)
      {
        running_status = b;
      }
      else
      {
        running_status = 0;
      }
      msg[0] = b;
      msg_pos = 1;
    }
    else
    {
      if (rx_sysex_active)
      {
        rx_sysex_append_byte(b);
        continue;
      }

      // Data byte.
      if (msg_pos > 0 && msg_pos < msg_len)
      {
        msg[msg_pos++] = b;
        if (msg_pos >= msg_len)
        {
          dispatch_midi_message(msg, msg_len);
          msg_pos = 1; // ready for running status
        }
      }
      else if (running_status)
      {
        msg[0] = running_status;
        msg_len = midi_status_len(running_status);
        msg[1] = b;
        msg_pos = 2;
        if (msg_len == 2)
        {
          dispatch_midi_message(msg, 2);
          msg_pos = 1;
        }
      }
    }
  }
}

// Spec-compliant BLE-MIDI packet decoder (Section 7).
static void decode_ble_midi_and_dispatch(const uint8_t *buffer, uint16_t buffer_size)
{
  if ((!ble_midi_state.rx_callback && !ble_midi_state.rx_raw_callback) || !buffer || buffer_size < 2)
    return;

  // BLE-MIDI header byte: bit7=1 bit6=0 → range 0x80..0xBF.
  uint8_t b0 = buffer[0];
  bool valid_header = ((b0 & 0xC0) == 0x80);
  bool b1_is_timestamp = (buffer_size > 1) ? ((buffer[1] & 0x80) != 0) : false;

  // Allow SysEx continuation packets (Section 8): header + data bytes (no timestamp).
  if (!valid_header || (!rx_sysex_active && !b1_is_timestamp))
  {
    decode_raw_midi(buffer, buffer_size);
    return;
  }

  // --- BLE-MIDI spec-compliant parser (Section 7) ---
  // Packet = header_byte { [timestamp_byte] midi_message } ...
  //
  // Between messages:
  //   MSB=1 → timestamp byte (consume, then expect status or running data)
  //   MSB=0 → running status data byte (reuses previous timestamp)
  //
  // After timestamp byte:
  //   MSB=1 → new MIDI status byte
  //   MSB=0 → running status data byte

  uint16_t i = 1; // skip header byte
  bool after_timestamp = false;
  uint8_t running_status = 0;
  bool require_timestamp_for_running = false;
  uint8_t msg[3];
  uint8_t msg_len = 0;
  uint8_t msg_pos = 0;
  bool collecting = false;

  while (i < buffer_size)
  {
    uint8_t b = buffer[i];

    if (rx_sysex_active)
    {
      if ((b & 0x80) == 0)
      {
        rx_sysex_append_byte(b);
        i++;
        continue;
      }

      // In SysEx mode, MSB=1 is interpreted as timestamp byte.
      i++;
      if (i >= buffer_size)
        break;

      uint8_t sb = buffer[i];
      if ((sb & 0x80) == 0)
      {
        // Invalid after timestamp in SysEx context.
        continue;
      }

      if (sb == 0xF7)
      {
        rx_sysex_append_byte(0xF7);
        rx_sysex_end();
      }
      else if (is_system_realtime_status(sb))
      {
        dispatch_midi_message(&sb, 1);
      }

      i++;
      continue;
    }

    if (collecting)
    {
      // Collecting data bytes for an in-progress message.
      if (b & 0x80)
      {
        // Unexpected MSB=1 during data collection. Per spec, System Real-Time
        // messages are deinterleaved from non-SysEx in BLE-MIDI. Treat as end
        // of current (incomplete) message and re-parse this byte.
        collecting = false;
        after_timestamp = false;
        continue; // re-process b
      }
      msg[msg_pos++] = b;
      i++;
      if (msg_pos >= msg_len)
      {
        dispatch_midi_message(msg, msg_len);
        collecting = false;
        after_timestamp = false;
      }
      continue;
    }

    // Between messages.
    if (b & 0x80)
    {
      if (!after_timestamp)
      {
        // This is a timestamp byte — consume it.
        after_timestamp = true;
        i++;
        continue;
      }

      // After timestamp byte, MSB=1 → MIDI status byte.
      after_timestamp = false;
      i++;

      if (b == 0xF0)
      {
        rx_sysex_begin();
        require_timestamp_for_running = true;
        continue;
      }

      msg_len = midi_status_len(b);
      if (!msg_len)
        continue;

      if (msg_len == 1)
      {
        // System Common / Real-Time keep running status, but the next running
        // status message must carry a timestamp (BLE-MIDI Section 7.3, rule 3).
        if (b >= 0xF0)
          require_timestamp_for_running = true;
        dispatch_midi_message(&b, 1);
        continue;
      }

      if (b < 0xF0)
      {
        running_status = b;
        require_timestamp_for_running = false;
      }
      else
      {
        require_timestamp_for_running = true;
      }

      msg[0] = b;
      msg_pos = 1;
      collecting = true;
      continue;
    }

    // MSB=0 between messages: running status data byte.
    if (running_status)
    {
      if (require_timestamp_for_running && !after_timestamp)
      {
        i++;
        continue;
      }

      msg_len = midi_status_len(running_status);
      msg[0] = running_status;
      msg[1] = b;
      msg_pos = 2;
      i++;
      if (msg_len == 2)
      {
        dispatch_midi_message(msg, 2);
        if (require_timestamp_for_running && after_timestamp)
          require_timestamp_for_running = false;
      }
      else
      {
        collecting = true;
        if (require_timestamp_for_running && after_timestamp)
          require_timestamp_for_running = false;
      }
    }
    else
    {
      // Data byte without running status — skip.
      i++;
    }
    after_timestamp = false;
  }
}

// ---------------------------------------------------------------------------
// ATT Callbacks
// ---------------------------------------------------------------------------

static uint16_t att_read_callback(hci_con_handle_t connection_handle, uint16_t att_handle,
                                  uint16_t offset, uint8_t *buffer, uint16_t buffer_size)
{
  UNUSED(connection_handle);

  // Manufacturer Name
  if (att_handle == ATT_CHARACTERISTIC_ORG_BLUETOOTH_CHARACTERISTIC_MANUFACTURER_NAME_STRING_01_VALUE_HANDLE)
  {
    return att_read_callback_handle_blob((const uint8_t *)ble_midi_state.manufacturer,
                                         strlen(ble_midi_state.manufacturer), offset, buffer, buffer_size);
  }

  // Firmware Revision
  if (att_handle == ATT_CHARACTERISTIC_ORG_BLUETOOTH_CHARACTERISTIC_FIRMWARE_REVISION_STRING_01_VALUE_HANDLE)
  {
    return att_read_callback_handle_blob((const uint8_t *)ble_midi_state.firmware_version,
                                         strlen(ble_midi_state.firmware_version), offset, buffer, buffer_size);
  }

  // Battery Level
  if (att_handle == ATT_CHARACTERISTIC_ORG_BLUETOOTH_CHARACTERISTIC_BATTERY_LEVEL_01_VALUE_HANDLE)
  {
    return att_read_callback_handle_byte(ble_midi_state.battery_level, offset, buffer, buffer_size);
  }

  // BLE-MIDI
  if (att_handle == ATT_CHARACTERISTIC_7772E5DB_3868_4112_A1A9_F2669D106BF3_01_VALUE_HANDLE)
  {
    return 0;
  }

  return 0;
}

static int att_write_callback(hci_con_handle_t connection_handle, uint16_t att_handle,
                              uint16_t transaction_mode, uint16_t offset, uint8_t *buffer, uint16_t buffer_size)
{
  UNUSED(transaction_mode);
  UNUSED(offset);

  // Battery CCCD
  if (att_handle == ATT_CHARACTERISTIC_ORG_BLUETOOTH_CHARACTERISTIC_BATTERY_LEVEL_01_CLIENT_CONFIGURATION_HANDLE)
  {
    ble_midi_state.battery_notifications_enabled =
        (little_endian_read_16(buffer, 0) == GATT_CLIENT_CHARACTERISTICS_CONFIGURATION_NOTIFICATION);
    ble_midi_state.con_handle = connection_handle;
    return 0;
  }

  // MIDI CCCD
  if (att_handle == ATT_CHARACTERISTIC_7772E5DB_3868_4112_A1A9_F2669D106BF3_01_CLIENT_CONFIGURATION_HANDLE)
  {
    ble_midi_state.notifications_enabled =
        (little_endian_read_16(buffer, 0) == GATT_CLIENT_CHARACTERISTICS_CONFIGURATION_NOTIFICATION);
    ble_midi_state.con_handle = connection_handle;
    return 0;
  }

  // Incoming MIDI
  // Some clients end up writing to the characteristic declaration handle (value_handle - 1).
  // Accept both.
  if (att_handle == ATT_CHARACTERISTIC_7772E5DB_3868_4112_A1A9_F2669D106BF3_01_VALUE_HANDLE ||
      att_handle == (ATT_CHARACTERISTIC_7772E5DB_3868_4112_A1A9_F2669D106BF3_01_VALUE_HANDLE - 1))
  {
    decode_ble_midi_and_dispatch(buffer, buffer_size);
    return 0;
  }

  return 0;
}

// ---------------------------------------------------------------------------
// HCI Event Handler
// ---------------------------------------------------------------------------

static void packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
  UNUSED(size);
  UNUSED(channel);

  if (packet_type != HCI_EVENT_PACKET)
    return;

  uint8_t event_type = hci_event_packet_get_type(packet);

  switch (event_type)
  {
  case BTSTACK_EVENT_STATE:
    if (btstack_event_state_get_state(packet) == HCI_STATE_WORKING)
    {
      bd_addr_t null_addr = {0};
      gap_advertisements_set_params(0x0020, 0x0040, 0, 0, null_addr, 0x07, 0x00);
      gap_advertisements_set_data(sizeof(adv_data), adv_data);
      gap_scan_response_set_data(scan_resp_data_len, scan_resp_data);
      gap_advertisements_enable(1);
    }
    break;

  case HCI_EVENT_LE_META:
    switch (hci_event_le_meta_get_subevent_code(packet))
    {
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
    tx_queue_drop(tx_count);
    tx_sysex_active = false;
    tx_sysex_len = 0;
    tx_sysex_pos = 0;
    tx_all_notes_off_pending = false;
    rx_sysex_reset();
    gap_advertisements_enable(1);
    break;
  }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

int rokot_ble_midi_init(const char *device_name)
{
  if (ble_midi_state.initialized)
    return -1;

  strncpy(ble_midi_state.device_name, device_name, sizeof(ble_midi_state.device_name) - 1);
  ble_midi_state.device_name[sizeof(ble_midi_state.device_name) - 1] = '\0';
  build_scan_response(device_name);

  if (cyw43_arch_init())
    return -2;

  l2cap_init();
  sm_init();
  att_server_init(profile_data, att_read_callback, att_write_callback);

  ble_midi_state.hci_event_callback_registration.callback = &packet_handler;
  hci_add_event_handler(&ble_midi_state.hci_event_callback_registration);
  att_server_register_packet_handler(packet_handler);

  hci_power_control(HCI_POWER_ON);
  ble_midi_state.initialized = true;
  tx_queue_drop(tx_count);
  tx_sysex_active = false;
  tx_sysex_len = 0;
  tx_sysex_pos = 0;
  tx_all_notes_off_pending = false;
  rx_sysex_reset();
  return 0;
}

void rokot_ble_midi_deinit(void)
{
  if (!ble_midi_state.initialized)
    return;
  hci_power_control(HCI_POWER_OFF);
  cyw43_arch_deinit();
  ble_midi_state.initialized = false;
  ble_midi_state.con_handle = HCI_CON_HANDLE_INVALID;
  ble_midi_state.notifications_enabled = false;
  ble_midi_state.battery_notifications_enabled = false;
  tx_queue_drop(tx_count);
  tx_sysex_active = false;
  tx_sysex_len = 0;
  tx_sysex_pos = 0;
  tx_all_notes_off_pending = false;
  rx_sysex_reset();
}

void rokot_ble_midi_task(void)
{
  if (!ble_midi_state.initialized)
    return;
  // Use the CYW43 non-blocking poll path to avoid parking the main loop.
  cyw43_arch_poll();

  if (!ble_midi_state.notifications_enabled || ble_midi_state.con_handle == HCI_CON_HANDLE_INVALID)
    return;

  // If queue overflowed, drop all pending messages and enqueue a safety reset.
  if (tx_all_notes_off_pending)
  {
    tx_queue_drop(tx_count);
    tx_sysex_active = false;
    tx_sysex_len = 0;
    tx_sysex_pos = 0;
    uint8_t ano[3] = {(uint8_t)(MIDI_CONTROL_CHANGE | 0x00), MIDI_CC_ALL_NOTES_OFF, 0};
    tx_queue_push(ano, 3);
    tx_all_notes_off_pending = false;
  }

  while (att_server_can_send_packet_now(ble_midi_state.con_handle) &&
         (tx_sysex_active || !tx_queue_is_empty()))
  {
    uint16_t mtu = att_server_get_mtu(ble_midi_state.con_handle);
    uint16_t max_payload = (mtu > 3) ? (mtu - 3) : 20;
    uint8_t packet[128];
    if (max_payload > sizeof(packet))
      max_payload = (uint16_t)sizeof(packet);

    if (tx_sysex_active)
    {
      uint16_t pos = 0;
      uint16_t header_ts = ble_midi_timestamp();
      packet[pos++] = ble_midi_header_byte(header_ts);

      if (tx_sysex_pos == 0)
      {
        // First SysEx packet starts with timestamp + F0.
        if ((max_payload - pos) < 2)
          break;
        packet[pos++] = ble_midi_ts_byte(header_ts);
        packet[pos++] = tx_sysex_buffer[tx_sysex_pos++]; // F0
      }

      while (tx_sysex_pos < tx_sysex_len && pos < max_payload)
      {
        // Real-time messages are allowed to interleave inside SysEx.
        const ble_midi_tx_item_t *rt = tx_queue_peek_at(0);
        if (rt && rt->len == 1 && is_system_realtime_status(rt->data[0]))
        {
          if ((max_payload - pos) < 2)
            break;
          uint16_t rt_ts = ble_midi_timestamp();
          packet[pos++] = ble_midi_ts_byte(rt_ts);
          packet[pos++] = rt->data[0];
          tx_queue_drop(1);
          continue;
        }

        uint8_t b = tx_sysex_buffer[tx_sysex_pos];

        if (b == 0xF7 || is_system_realtime_status(b))
        {
          // EOX and real-time bytes must carry their own timestamp in SysEx mode.
          if ((max_payload - pos) < 2)
            break;
          uint16_t ts = ble_midi_timestamp();
          packet[pos++] = ble_midi_ts_byte(ts);
          packet[pos++] = b;
          tx_sysex_pos++;
          continue;
        }

        if (b & 0x80)
        {
          // Invalid byte inside SysEx payload; skip it to avoid stalling.
          tx_sysex_pos++;
          continue;
        }

        packet[pos++] = b;
        tx_sysex_pos++;
      }

      if (pos <= 1)
        break;

      int result = att_server_notify(ble_midi_state.con_handle,
                                     ATT_CHARACTERISTIC_7772E5DB_3868_4112_A1A9_F2669D106BF3_01_VALUE_HANDLE,
                                     packet, pos);
      if (result != 0)
        break;

      if (tx_sysex_pos >= tx_sysex_len)
      {
        tx_sysex_active = false;
        tx_sysex_len = 0;
        tx_sysex_pos = 0;
      }
      continue;
    }

    // Flush queued non-SysEx messages — batch multiple messages per BLE packet.
    uint16_t pos = 0;
    uint8_t batched = 0;
    uint16_t header_ts = ble_midi_timestamp();
    packet[pos++] = ble_midi_header_byte(header_ts);

    while (pos < max_payload)
    {
      const ble_midi_tx_item_t *item = tx_queue_peek_at(batched);
      if (!item)
        break;

      uint16_t needed = (uint16_t)(1 + item->len);
      if (pos + needed > max_payload)
        break;

      // Section 7.2: first timestamp byte must match the packet header timestamp.
      uint16_t ts = (batched == 0) ? header_ts : ble_midi_timestamp();
      packet[pos++] = ble_midi_ts_byte(ts);
      memcpy(&packet[pos], item->data, item->len);
      pos += item->len;
      batched++;
    }

    if (batched == 0)
      break;

    int result = att_server_notify(ble_midi_state.con_handle,
                                   ATT_CHARACTERISTIC_7772E5DB_3868_4112_A1A9_F2669D106BF3_01_VALUE_HANDLE,
                                   packet, pos);
    if (result != 0)
      break;

    tx_queue_drop(batched);
  }
}

rokot_ble_midi_state_t rokot_ble_midi_get_state(void)
{
  if (ble_midi_state.notifications_enabled)
    return ROKOT_BLE_MIDI_READY;
  if (ble_midi_state.con_handle != HCI_CON_HANDLE_INVALID)
    return ROKOT_BLE_MIDI_CONNECTED;
  return ROKOT_BLE_MIDI_DISCONNECTED;
}

bool rokot_ble_midi_is_ready(void)
{
  return ble_midi_state.notifications_enabled && ble_midi_state.con_handle != HCI_CON_HANDLE_INVALID;
}

bool rokot_ble_midi_is_connected(void)
{
  return ble_midi_state.con_handle != HCI_CON_HANDLE_INVALID;
}

float rokot_ble_midi_get_connection_interval(void)
{
  return ble_midi_state.connection_interval * 1.25f;
}

// Device Information
void rokot_ble_midi_set_manufacturer(const char *manufacturer)
{
  strncpy(ble_midi_state.manufacturer, manufacturer, sizeof(ble_midi_state.manufacturer) - 1);
  ble_midi_state.manufacturer[sizeof(ble_midi_state.manufacturer) - 1] = '\0';
}

void rokot_ble_midi_set_firmware_version(const char *version)
{
  strncpy(ble_midi_state.firmware_version, version, sizeof(ble_midi_state.firmware_version) - 1);
  ble_midi_state.firmware_version[sizeof(ble_midi_state.firmware_version) - 1] = '\0';
}

// Battery
void rokot_ble_midi_set_battery_level(uint8_t level)
{
  if (level > 100)
    level = 100;
  ble_midi_state.battery_level = level;

  if (ble_midi_state.battery_notifications_enabled &&
      ble_midi_state.con_handle != HCI_CON_HANDLE_INVALID &&
      att_server_can_send_packet_now(ble_midi_state.con_handle))
  {
    att_server_notify(ble_midi_state.con_handle,
                      ATT_CHARACTERISTIC_ORG_BLUETOOTH_CHARACTERISTIC_BATTERY_LEVEL_01_VALUE_HANDLE, &level, 1);
  }
}

uint8_t rokot_ble_midi_get_battery_level(void)
{
  return ble_midi_state.battery_level;
}

// MIDI
int rokot_ble_midi_note_on(uint8_t channel, uint8_t note, uint8_t velocity)
{
  uint8_t midi[3] = {(uint8_t)(MIDI_NOTE_ON | (channel & 0x0F)), note & 0x7F, velocity & 0x7F};
  return send_midi_internal(midi, 3);
}

int rokot_ble_midi_note_off(uint8_t channel, uint8_t note)
{
  uint8_t midi[3] = {(uint8_t)(MIDI_NOTE_OFF | (channel & 0x0F)), note & 0x7F, 0};
  return send_midi_internal(midi, 3);
}

int rokot_ble_midi_control_change(uint8_t channel, uint8_t controller, uint8_t value)
{
  uint8_t midi[3] = {(uint8_t)(MIDI_CONTROL_CHANGE | (channel & 0x0F)), controller & 0x7F, value & 0x7F};
  return send_midi_internal(midi, 3);
}

int rokot_ble_midi_program_change(uint8_t channel, uint8_t program)
{
  uint8_t midi[2] = {(uint8_t)(MIDI_PROGRAM_CHANGE | (channel & 0x0F)), program & 0x7F};
  return send_midi_internal(midi, 2);
}

int rokot_ble_midi_pitch_bend(uint8_t channel, int16_t value)
{
  uint16_t bend = (uint16_t)(value + 8192);
  uint8_t midi[3] = {(uint8_t)(MIDI_PITCH_BEND | (channel & 0x0F)), (uint8_t)(bend & 0x7F), (uint8_t)((bend >> 7) & 0x7F)};
  return send_midi_internal(midi, 3);
}

int rokot_ble_midi_channel_pressure(uint8_t channel, uint8_t pressure)
{
  uint8_t midi[2] = {(uint8_t)(MIDI_CHANNEL_PRESSURE | (channel & 0x0F)), pressure & 0x7F};
  return send_midi_internal(midi, 2);
}

int rokot_ble_midi_send_raw(const uint8_t *data, uint16_t len)
{
  if (!data || len == 0)
    return -1;

  if (data[0] == 0xF0)
    return send_sysex_internal(data, len);

  if (len > 3)
    return -2;

  return send_midi_internal(data, (uint8_t)len);
}

void rokot_ble_midi_set_callback(rokot_ble_midi_callback_t callback)
{
  ble_midi_state.rx_callback = callback;
}

void rokot_ble_midi_set_raw_callback(rokot_ble_midi_raw_callback_t callback)
{
  ble_midi_state.rx_raw_callback = callback;
}
