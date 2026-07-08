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

// --- station blob (Doc 12 §3.6.2) -------------------------------------------
void encodeStationConfig(const StationConfig &c, uint8_t *blob) {
  memset(blob, 0, STATION_BLOB_SIZE);
  blob[0] = c.station_id;
  blob[1] = c.volume_pct;
  blob[2] = c.default_setup_sound;
}

void decodeStationConfig(const uint8_t *blob, size_t len, StationConfig &c) {
  c = StationConfig{};
  if (len >= 1) c.station_id = blob[0];
  if (len >= 2) c.volume_pct = blob[1];
  if (len >= 3) c.default_setup_sound = blob[2];
}

// --- target blob (Doc 12 §3.6.3) ---------------------------------------------
void encodeTargetConfig(const TargetConfig &c, uint8_t *blob) {
  memset(blob, 0, TARGET_BLOB_SIZE);
  blob[0] = c.target_id;
  blob[1] = c.station_id;
  blob[2] = c.sound_id;
  putU16(blob + 3, c.hit_time_ms);
  putU16(blob + 5, c.cooldown_ms);
  blob[7] = c.sw_animation;
  blob[8] = c.sw_channels;
}

void decodeTargetConfig(const uint8_t *blob, size_t len, TargetConfig &c) {
  c = TargetConfig{};
  if (len >= 1) c.target_id = blob[0];
  if (len >= 2) c.station_id = blob[1];
  if (len >= 3) c.sound_id = blob[2];
  if (len >= 5) c.hit_time_ms = getU16(blob + 3);
  if (len >= 7) c.cooldown_ms = getU16(blob + 5);
  if (len >= 8) c.sw_animation = blob[7];
  if (len >= 9) c.sw_channels = blob[8];
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
