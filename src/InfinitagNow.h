// InfinitagNow – shared ESP-NOW protocol definitions for the Infinitag system.
//
// Implements the packet format frozen 2026-05-18, documented in
// wissensbasis/12-refactor-station-v2.md §3.4–3.6. Pure C++ (no Arduino
// dependencies) so it compiles natively for unit tests and can later be
// reused by station and target firmware.
//
// All multi-byte fields are little-endian (ESP32 native). Blob access uses
// memcpy so the code is also correct on any test host.

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace inow {

constexpr uint8_t PROTOCOL_VERSION = 0x01;
constexpr size_t PACKET_SIZE = 36;
constexpr size_t PAYLOAD_SIZE = 26;
constexpr size_t CONFIG_BLOB_MAX = 19;  // max blob bytes in DISCOVER_REPLY

// --- Message types (Doc 12 §3.5) ------------------------------------------
enum MsgType : uint8_t {
  MSG_DISCOVER_REQ   = 0x01,  // Config-Box -> broadcast, payload[0] = device_type filter
  MSG_DISCOVER_REPLY = 0x02,  // device -> Config-Box (unicast)
  MSG_IDENTIFY       = 0x03,  // Config-Box -> device, payload[0] = duration in 100 ms
  MSG_HIT_REPORT     = 0x10,  // target -> broadcast, payload[0] = sound_id
  MSG_SETUP_BEGIN    = 0x20,  // Config-Box -> broadcast, payload[0] = timeout s
  MSG_SETUP_TAKE     = 0x21,  // station -> broadcast, payload[0] = new station_id
  MSG_CFG_WRITE      = 0x30,  // Config-Box -> device (unicast), payload = config blob
  MSG_CFG_ACK        = 0x31,  // device -> Config-Box, payload[0] = AckStatus
  MSG_CFG_TEST_SOUND = 0x32,  // Config-Box -> station, payload[0] = sound_id (play only, no persist)
  MSG_IR_SELECT_ECHO = 0xC0,  // reserved/latent (IR pointer), payload[0] = token
  MSG_DEBUG_CMD      = 0xF0,  // Config-Box -> device: payload[0] = DebugTest, payload[1] = parameter
  MSG_DEBUG_RESULT   = 0xF1,  // device -> Config-Box: payload[0] = DebugTest, payload[1] = DebugResult
};

// --- Self-test catalog (MSG_DEBUG_CMD payload[0]) ---------------------------
enum DebugTest : uint8_t {
  DBG_SOUND   = 1,  // param = sound index (play only)
  DBG_LED     = 2,  // run LED test pattern (visual check)
  DBG_LASER   = 3,  // param = seconds on (auto-off, clamped to 10)
  DBG_IR      = 4,  // param = burst ms; result = TSOP self-reception
  DBG_TRIGGER = 5,  // param = timeout s; result when trigger pressed / timeout
};

enum DebugResult : uint8_t {
  DBG_RES_OK          = 0,
  DBG_RES_FAIL        = 1,
  DBG_RES_TIMEOUT     = 2,
  DBG_RES_UNSUPPORTED = 3,
};

// --- Device types ----------------------------------------------------------
enum DeviceType : uint8_t {
  DEV_STATION    = 1,
  DEV_TARGET     = 2,
  DEV_CONFIG_BOX = 3,
  DEV_ANY        = 0xFF,
};

// --- Header flags ----------------------------------------------------------
enum : uint8_t {
  FLAG_ACK_REQUIRED    = 0x01,
  FLAG_SETUP_MODE_ONLY = 0x02,
};

// --- CFG_ACK status --------------------------------------------------------
enum AckStatus : uint8_t {
  ACK_OK               = 0,
  ACK_NACK_PERSISTENCE = 1,
  ACK_NACK_VALIDATION  = 2,
};

// --- Wire format (fixed 36 bytes, Doc 12 §3.4) -----------------------------
typedef struct __attribute__((packed)) {
  uint8_t  version;      // PROTOCOL_VERSION
  uint8_t  msg_type;     // MsgType
  uint8_t  device_type;  // DeviceType of the *subject* of the message
  uint8_t  station_id;   // 1..99, 0 = unset / not relevant
  uint8_t  target_id;    // 1..99, 0 = unset / not relevant
  uint8_t  flags;
  uint8_t  token;        // random echo-protection token
  uint8_t  reserved;     // = 0
  uint8_t  payload[PAYLOAD_SIZE];
  uint16_t crc16;        // CRC-16/CCITT-FALSE over bytes 0..33, little-endian
} Packet;

static_assert(sizeof(Packet) == PACKET_SIZE, "Packet must be exactly 36 bytes");

// --- Config blobs (Doc 12 §3.6.2 / §3.6.3) ---------------------------------

struct StationConfig {
  uint8_t station_id = 1;
  uint8_t volume_pct = 80;
  uint8_t default_setup_sound = 13;
};
constexpr size_t STATION_BLOB_SIZE = 16;

struct TargetConfig {
  uint8_t  target_id = 1;
  uint8_t  station_id = 1;
  uint8_t  sound_id = 1;
  uint16_t hit_time_ms = 10000;
  uint16_t cooldown_ms = 2000;
  uint8_t  sw_animation = 0;
  uint8_t  sw_channels = 0b00000111;  // bit0=SW1, bit1=SW_5V, bit2=SW_3V3
};
constexpr size_t TARGET_BLOB_SIZE = 16;

// --- DISCOVER_REPLY payload (Doc 12 §3.6.1) --------------------------------
struct DiscoverReply {
  uint8_t  fw_major = 0;
  uint8_t  fw_minor = 0;
  uint8_t  fw_patch = 0;
  int8_t   rssi_self = 0;      // device's own RX RSSI of our DISCOVER_REQ
  uint16_t uptime_min = 0;
  uint8_t  config_blob_len = 0;
  uint8_t  config_blob[CONFIG_BLOB_MAX] = {0};
};

// --- Functions --------------------------------------------------------------

// CRC-16/CCITT-FALSE (init 0xFFFF, poly 0x1021, no reflection, xorout 0).
// Check vector: "123456789" -> 0x29B1.
uint16_t crc16(const uint8_t *data, size_t len);

// Zero-initializes a packet and sets version/msg_type/device_type.
void init(Packet &p, uint8_t msgType, uint8_t deviceType);

// Computes and stores the CRC. Call after all fields are filled, before send.
void seal(Packet &p);

// Length, version and CRC check. Returns true for a valid Infinitag packet.
bool validate(const uint8_t *data, size_t len);

// Blob encode/decode. Encode writes exactly *_BLOB_SIZE bytes.
void encodeStationConfig(const StationConfig &c, uint8_t *blob);
void decodeStationConfig(const uint8_t *blob, size_t len, StationConfig &c);
void encodeTargetConfig(const TargetConfig &c, uint8_t *blob);
void decodeTargetConfig(const uint8_t *blob, size_t len, TargetConfig &c);

// DISCOVER_REPLY payload encode/decode (into/from Packet::payload).
void encodeDiscoverReply(const DiscoverReply &r, uint8_t *payload);
void decodeDiscoverReply(const uint8_t *payload, DiscoverReply &r);

}  // namespace inow
