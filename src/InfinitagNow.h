// InfinitagNow – shared ESP-NOW protocol definitions for the Infinitag system.
//
// Protocol version 0x02 (2026-07-12): devices are identified solely by their
// eFuse MAC – the human-managed station_id/target_id world and the whole
// SETUP flow were removed. See PROTOCOL.md for the full specification.
// Pure C++ (no Arduino dependencies) so it compiles natively for unit tests
// and can be reused by station, target and config-box firmware.
//
// All multi-byte fields are little-endian (ESP32 native). Blob access uses
// memcpy so the code is also correct on any test host.

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace inow {

constexpr uint8_t PROTOCOL_VERSION = 0x02;
constexpr size_t PACKET_SIZE = 36;
constexpr size_t PAYLOAD_SIZE = 26;
constexpr size_t CONFIG_BLOB_MAX = 19;  // max blob bytes in DISCOVER_REPLY

// --- Message types -----------------------------------------------------------
enum MsgType : uint8_t {
  MSG_DISCOVER_REQ   = 0x01,  // Config-Box -> broadcast, payload[0] = device_type filter
  MSG_DISCOVER_REPLY = 0x02,  // device -> Config-Box (unicast)
  MSG_IDENTIFY       = 0x03,  // Config-Box -> device, payload[0] = duration in 100 ms
  MSG_HIT_REPORT     = 0x10,  // target -> broadcast, payload[0..5] = dest station MAC,
                              //   payload[6] = sound_id (broadcast keeps the
                              //   config box live monitor working)
  MSG_CFG_WRITE      = 0x30,  // Config-Box -> device (unicast), payload = config blob
  MSG_CFG_ACK        = 0x31,  // device -> Config-Box, payload[0] = AckStatus
  MSG_CFG_TEST_SOUND = 0x32,  // Config-Box -> station, payload[0] = sound_id (play only)
  MSG_IR_SELECT_ECHO = 0xC0,  // reserved/latent (IR pointer), payload[0] = token
  MSG_DEBUG_CMD      = 0xF0,  // Config-Box -> device: payload[0] = DebugTest, payload[1] = parameter
  MSG_DEBUG_RESULT   = 0xF1,  // device -> Config-Box: payload[0] = DebugTest, payload[1] = DebugResult
  MSG_UPDATE_BEGIN   = 0xF2,  // Config-Box -> device: enter SoftAP update mode,
                              //   payload[0] = timeout in minutes (0 = default 5)
  MSG_UPDATE_ACK     = 0xF3,  // device -> Config-Box: payload[0] = 0 OK (entering)
  MSG_PUSH_BEGIN     = 0xF4,  // Box -> device: ESP-NOW firmware push starts
  MSG_PUSH_ACK       = 0xF5,  // device -> Box: window/missing bitmap or final status
  MSG_PUSH_END       = 0xF6,  // Box -> device: all windows sent, finalize
};

// --- ESP-NOW firmware push (Doc 21 E3) --------------------------------------
// Control runs over normal packets (types above); the data phase uses RAW
// 250-byte ESP-NOW frames: 'I','N','W','D' + u32 frame index (LE) +
// PUSH_FRAME_DATA payload bytes. Frames of one window are buffered in RAM
// on the receiver and flashed sequentially once the window is complete.
constexpr size_t PUSH_FRAME_DATA = 242;   // 250 - 8 byte raw header
constexpr uint8_t PUSH_WINDOW_FRAMES = 16;

struct PushBegin {
  uint32_t size = 0;    // total image bytes
  uint32_t crc32 = 0;   // over the whole image
  uint8_t major = 0, minor = 0, patch = 0;
};

enum PushAckStatus : uint8_t {
  PUSH_ACK_WINDOW = 0,       // window ack, bitmap = missing frames
  PUSH_ACK_FINAL_OK = 1,     // flashed + verified, device reboots
  PUSH_ACK_FINAL_CRC = 2,    // image crc mismatch
  PUSH_ACK_FINAL_FLASH = 3,  // Update.begin/end failed
  PUSH_ACK_BUSY = 4,         // device cannot start (e.g. push active)
};

struct PushAck {
  uint16_t window = 0;
  uint32_t missing = 0;  // bit i = frame window*PUSH_WINDOW_FRAMES+i missing
  uint8_t status = PUSH_ACK_WINDOW;
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
  FLAG_ACK_REQUIRED = 0x01,
};

// --- CFG_ACK status --------------------------------------------------------
enum AckStatus : uint8_t {
  ACK_OK               = 0,
  ACK_NACK_PERSISTENCE = 1,
  ACK_NACK_VALIDATION  = 2,
};

// --- Wire format (fixed 36 bytes) -------------------------------------------
// Identity is the sender MAC from the ESP-NOW layer-2 header; the packet
// itself carries no device ids since v0x02.
typedef struct __attribute__((packed)) {
  uint8_t  version;      // PROTOCOL_VERSION
  uint8_t  msg_type;     // MsgType
  uint8_t  device_type;  // DeviceType of the *subject* of the message
  uint8_t  flags;
  uint8_t  token;        // random echo-protection token
  uint8_t  reserved[3];  // = 0
  uint8_t  payload[PAYLOAD_SIZE];
  uint16_t crc16;        // CRC-16/CCITT-FALSE over bytes 0..33, little-endian
} Packet;

static_assert(sizeof(Packet) == PACKET_SIZE, "Packet must be exactly 36 bytes");

// --- Config blobs ------------------------------------------------------------

// LED channel mask for the wand status colors (SK6812 RGBW).
// Any non-empty combination of the four dies is a valid color (1..15).
enum LedChannel : uint8_t {
  LED_R = 0x01,
  LED_G = 0x02,
  LED_B = 0x04,
  LED_W = 0x08,
  LED_MASK_MAX = 0x0F,
};

struct StationConfig {
  uint8_t volume_pct = 80;
  uint8_t led_ready = LED_G;  // wand color when ready to fire
  uint8_t led_busy = LED_R;   // wand color while busy (audio playing etc.)
};
constexpr size_t STATION_BLOB_SIZE = 16;

struct TargetConfig {
  uint8_t  station_mac[6] = {0};  // station that plays this target's sound
  uint8_t  sound_id = 1;
  uint16_t hit_time_ms = 10000;
  uint16_t cooldown_ms = 2000;
  uint8_t  sw_animation = 0;
  uint8_t  sw_channels = 0b00000111;  // bit0=SW1, bit1=SW_5V, bit2=SW_3V3
};
constexpr size_t TARGET_BLOB_SIZE = 16;

// --- DISCOVER_REPLY payload --------------------------------------------------
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

// CRC-32 (IEEE 802.3, reflected, init/xorout 0xFFFFFFFF) for the push.
uint32_t crc32(uint32_t crc, const uint8_t *data, size_t len);

// Push payload encode/decode (into/from Packet::payload).
void encodePushBegin(const PushBegin &b, uint8_t *payload);
void decodePushBegin(const uint8_t *payload, PushBegin &b);
void encodePushAck(const PushAck &a, uint8_t *payload);
void decodePushAck(const uint8_t *payload, PushAck &a);

// HIT_REPORT payload: [0..5] = destination station MAC, [6] = sound_id.
void encodeHitReport(const uint8_t stationMac[6], uint8_t soundId,
                     uint8_t *payload);
void decodeHitReport(const uint8_t *payload, uint8_t stationMac[6],
                     uint8_t &soundId);

// DISCOVER_REPLY payload encode/decode (into/from Packet::payload).
void encodeDiscoverReply(const DiscoverReply &r, uint8_t *payload);
void decodeDiscoverReply(const uint8_t *payload, DiscoverReply &r);

}  // namespace inow
