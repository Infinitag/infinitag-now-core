#include "InfinitagNow.h"

namespace inow {

uint16_t crc16(const uint8_t *data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= static_cast<uint16_t>(data[i]) << 8;
    for (int b = 0; b < 8; b++) {
      crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1);
    }
  }
  return crc;
}

void init(Packet &p, uint8_t msgType, uint8_t deviceType) {
  memset(&p, 0, sizeof(p));
  p.version = PROTOCOL_VERSION;
  p.msg_type = msgType;
  p.device_type = deviceType;
}

void seal(Packet &p) {
  p.crc16 = crc16(reinterpret_cast<const uint8_t *>(&p), PACKET_SIZE - 2);
}

bool validate(const uint8_t *data, size_t len) {
  if (len != PACKET_SIZE || data == nullptr) return false;
  if (data[0] != PROTOCOL_VERSION) return false;
  uint16_t rx;
  memcpy(&rx, data + PACKET_SIZE - 2, 2);  // little-endian on ESP32 & test hosts
  return rx == crc16(data, PACKET_SIZE - 2);
}

// --- little-endian helpers ---------------------------------------------------
static void putU16(uint8_t *dst, uint16_t v) {
  dst[0] = v & 0xFF;
  dst[1] = v >> 8;
}
static uint16_t getU16(const uint8_t *src) {
  return static_cast<uint16_t>(src[0]) | (static_cast<uint16_t>(src[1]) << 8);
}

// --- station blob (PROTOCOL.md, v0x02 layout) --------------------------------
void encodeStationConfig(const StationConfig &c, uint8_t *blob) {
  memset(blob, 0, STATION_BLOB_SIZE);
  blob[0] = c.volume_pct;
  blob[1] = c.led_ready;
  blob[2] = c.led_busy;
}

void decodeStationConfig(const uint8_t *blob, size_t len, StationConfig &c) {
  c = StationConfig{};
  if (len >= 1) c.volume_pct = blob[0];
  // 0 = field not set (e.g. short blob) -> keep the default color
  if (len >= 2 && blob[1] != 0) c.led_ready = blob[1];
  if (len >= 3 && blob[2] != 0) c.led_busy = blob[2];
}

// --- target blob (PROTOCOL.md, v0x02 layout) ----------------------------------
void encodeTargetConfig(const TargetConfig &c, uint8_t *blob) {
  memset(blob, 0, TARGET_BLOB_SIZE);
  memcpy(blob, c.station_mac, 6);
  blob[6] = c.sound_id;
  putU16(blob + 7, c.hit_time_ms);
  putU16(blob + 9, c.cooldown_ms);
  blob[11] = c.sw_animation;
  blob[12] = c.sw_channels;
}

void decodeTargetConfig(const uint8_t *blob, size_t len, TargetConfig &c) {
  c = TargetConfig{};
  if (len >= 6) memcpy(c.station_mac, blob, 6);
  if (len >= 7) c.sound_id = blob[6];
  if (len >= 9) c.hit_time_ms = getU16(blob + 7);
  if (len >= 11) c.cooldown_ms = getU16(blob + 9);
  if (len >= 12) c.sw_animation = blob[11];
  if (len >= 13) c.sw_channels = blob[12];
}

// --- CRC-32 (IEEE, reflected, bitwise – speed is irrelevant here) -------------
uint32_t crc32(uint32_t crc, const uint8_t *data, size_t len) {
  crc = ~crc;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int b = 0; b < 8; b++) {
      crc = (crc >> 1) ^ (0xEDB88320UL & (-(int32_t)(crc & 1)));
    }
  }
  return ~crc;
}

// --- ESP-NOW push control payloads ---------------------------------------------
static void putU32(uint8_t *dst, uint32_t v) {
  dst[0] = v & 0xFF;
  dst[1] = (v >> 8) & 0xFF;
  dst[2] = (v >> 16) & 0xFF;
  dst[3] = (v >> 24) & 0xFF;
}
static uint32_t getU32(const uint8_t *src) {
  return (uint32_t)src[0] | ((uint32_t)src[1] << 8) |
         ((uint32_t)src[2] << 16) | ((uint32_t)src[3] << 24);
}

void encodePushBegin(const PushBegin &b, uint8_t *payload) {
  memset(payload, 0, PAYLOAD_SIZE);
  putU32(payload, b.size);
  putU32(payload + 4, b.crc32);
  payload[8] = b.major;
  payload[9] = b.minor;
  payload[10] = b.patch;
  payload[11] = (uint8_t)PUSH_FRAME_DATA;    // for forward compatibility
  payload[12] = PUSH_WINDOW_FRAMES;
}

void decodePushBegin(const uint8_t *payload, PushBegin &b) {
  b.size = getU32(payload);
  b.crc32 = getU32(payload + 4);
  b.major = payload[8];
  b.minor = payload[9];
  b.patch = payload[10];
}

void encodePushAck(const PushAck &a, uint8_t *payload) {
  memset(payload, 0, PAYLOAD_SIZE);
  putU16(payload, a.window);
  putU32(payload + 2, a.missing);
  payload[6] = a.status;
  payload[7] = a.detail;
}

void decodePushAck(const uint8_t *payload, PushAck &a) {
  a.window = getU16(payload);
  a.missing = getU32(payload + 2);
  a.status = payload[6];
  a.detail = payload[7];
}

// --- HIT_REPORT payload -------------------------------------------------------
void encodeHitReport(const uint8_t stationMac[6], uint8_t soundId,
                     uint8_t *payload) {
  memset(payload, 0, PAYLOAD_SIZE);
  memcpy(payload, stationMac, 6);
  payload[6] = soundId;
}

void decodeHitReport(const uint8_t *payload, uint8_t stationMac[6],
                     uint8_t &soundId) {
  memcpy(stationMac, payload, 6);
  soundId = payload[6];
}

// --- DISCOVER_REPLY payload (Doc 12 §3.6.1) ----------------------------------
void encodeDiscoverReply(const DiscoverReply &r, uint8_t *payload) {
  memset(payload, 0, PAYLOAD_SIZE);
  payload[0] = r.fw_major;
  payload[1] = r.fw_minor;
  payload[2] = r.fw_patch;
  payload[3] = static_cast<uint8_t>(r.rssi_self);
  putU16(payload + 4, r.uptime_min);
  uint8_t n = r.config_blob_len;
  if (n > CONFIG_BLOB_MAX) n = CONFIG_BLOB_MAX;
  payload[6] = n;
  memcpy(payload + 7, r.config_blob, n);
}

void decodeDiscoverReply(const uint8_t *payload, DiscoverReply &r) {
  r = DiscoverReply{};
  r.fw_major = payload[0];
  r.fw_minor = payload[1];
  r.fw_patch = payload[2];
  r.rssi_self = static_cast<int8_t>(payload[3]);
  r.uptime_min = getU16(payload + 4);
  r.config_blob_len = payload[6];
  if (r.config_blob_len > CONFIG_BLOB_MAX) r.config_blob_len = CONFIG_BLOB_MAX;
  memcpy(r.config_blob, payload + 7, r.config_blob_len);
}

}  // namespace inow
