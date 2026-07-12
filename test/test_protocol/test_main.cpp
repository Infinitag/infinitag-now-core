// Unit tests for the InfinitagNow protocol library (v0x02).
// Run on the PC:  pio test -e native

#include <unity.h>
#include "InfinitagNow.h"

using namespace inow;

void test_packet_size() {
  TEST_ASSERT_EQUAL_size_t(36, sizeof(Packet));
}

void test_crc16_check_vector() {
  // CRC-16/CCITT-FALSE("123456789") = 0x29B1
  const uint8_t v[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  TEST_ASSERT_EQUAL_HEX16(0x29B1, crc16(v, sizeof(v)));
}

void test_seal_and_validate() {
  Packet p;
  init(p, MSG_DISCOVER_REQ, DEV_CONFIG_BOX);
  p.payload[0] = DEV_TARGET;
  p.token = 0xA5;
  seal(p);
  TEST_ASSERT_TRUE(validate(reinterpret_cast<uint8_t *>(&p), sizeof(p)));
}

void test_validate_rejects_corruption() {
  Packet p;
  init(p, MSG_IDENTIFY, DEV_TARGET);
  seal(p);
  uint8_t buf[sizeof(Packet)];
  memcpy(buf, &p, sizeof(p));
  buf[10] ^= 0xFF;  // flip payload bits
  TEST_ASSERT_FALSE(validate(buf, sizeof(buf)));
  TEST_ASSERT_FALSE(validate(reinterpret_cast<uint8_t *>(&p), 35));  // wrong length
}

void test_validate_rejects_wrong_version() {
  // v0x01 packets (and any other version) must be dropped.
  Packet p;
  init(p, MSG_HIT_REPORT, DEV_TARGET);
  p.version = 0x01;
  seal(p);
  TEST_ASSERT_FALSE(validate(reinterpret_cast<uint8_t *>(&p), sizeof(p)));
}

void test_station_blob_roundtrip() {
  StationConfig in;
  in.volume_pct = 55;
  in.led_ready = LED_R | LED_W;   // red + white die
  in.led_busy = LED_B;
  uint8_t blob[STATION_BLOB_SIZE];
  encodeStationConfig(in, blob);
  StationConfig out;
  decodeStationConfig(blob, sizeof(blob), out);
  TEST_ASSERT_EQUAL_UINT8(55, out.volume_pct);
  TEST_ASSERT_EQUAL_UINT8(LED_R | LED_W, out.led_ready);
  TEST_ASSERT_EQUAL_UINT8(LED_B, out.led_busy);
}

void test_station_blob_zero_led_falls_back() {
  // Zeroed LED fields (short or legacy blob) -> defaults green/red.
  uint8_t blob[STATION_BLOB_SIZE] = {0};
  blob[0] = 70;
  StationConfig out;
  decodeStationConfig(blob, sizeof(blob), out);
  TEST_ASSERT_EQUAL_UINT8(70, out.volume_pct);
  TEST_ASSERT_EQUAL_UINT8(LED_G, out.led_ready);
  TEST_ASSERT_EQUAL_UINT8(LED_R, out.led_busy);
}

void test_target_blob_roundtrip() {
  const uint8_t mac[6] = {0x24, 0x6F, 0x28, 0x22, 0x0A, 0xAC};
  TargetConfig in;
  memcpy(in.station_mac, mac, 6);
  in.sound_id = 6;
  in.hit_time_ms = 12345;
  in.cooldown_ms = 2500;
  in.sw_animation = 1;
  in.sw_channels = 0b101;
  uint8_t blob[TARGET_BLOB_SIZE];
  encodeTargetConfig(in, blob);
  TargetConfig out;
  decodeTargetConfig(blob, sizeof(blob), out);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(mac, out.station_mac, 6);
  TEST_ASSERT_EQUAL_UINT8(6, out.sound_id);
  TEST_ASSERT_EQUAL_UINT16(12345, out.hit_time_ms);
  TEST_ASSERT_EQUAL_UINT16(2500, out.cooldown_ms);
  TEST_ASSERT_EQUAL_UINT8(1, out.sw_animation);
  TEST_ASSERT_EQUAL_UINT8(0b101, out.sw_channels);
}

void test_target_blob_endianness() {
  // hit_time_ms = 0x1234 must serialize little-endian at offsets 7..8
  TargetConfig in;
  in.hit_time_ms = 0x1234;
  uint8_t blob[TARGET_BLOB_SIZE];
  encodeTargetConfig(in, blob);
  TEST_ASSERT_EQUAL_HEX8(0x34, blob[7]);
  TEST_ASSERT_EQUAL_HEX8(0x12, blob[8]);
}

void test_hit_report_roundtrip() {
  const uint8_t mac[6] = {0xA0, 0xB1, 0xC2, 0xD3, 0xE4, 0xF5};
  uint8_t payload[PAYLOAD_SIZE];
  encodeHitReport(mac, 9, payload);
  uint8_t outMac[6];
  uint8_t sound = 0;
  decodeHitReport(payload, outMac, sound);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(mac, outMac, 6);
  TEST_ASSERT_EQUAL_UINT8(9, sound);
}

void test_discover_reply_roundtrip() {
  DiscoverReply in;
  in.fw_major = 0;
  in.fw_minor = 2;
  in.fw_patch = 0;
  in.rssi_self = -42;
  in.uptime_min = 777;
  in.config_blob_len = TARGET_BLOB_SIZE;
  TargetConfig tc;
  tc.sound_id = 5;
  encodeTargetConfig(tc, in.config_blob);

  uint8_t payload[PAYLOAD_SIZE];
  encodeDiscoverReply(in, payload);
  DiscoverReply out;
  decodeDiscoverReply(payload, out);

  TEST_ASSERT_EQUAL_UINT8(2, out.fw_minor);
  TEST_ASSERT_EQUAL_INT8(-42, out.rssi_self);
  TEST_ASSERT_EQUAL_UINT16(777, out.uptime_min);
  TEST_ASSERT_EQUAL_UINT8(TARGET_BLOB_SIZE, out.config_blob_len);
  TargetConfig tcOut;
  decodeTargetConfig(out.config_blob, out.config_blob_len, tcOut);
  TEST_ASSERT_EQUAL_UINT8(5, tcOut.sound_id);
}

void test_blob_len_clamped() {
  DiscoverReply in;
  in.config_blob_len = 200;  // bogus – must be clamped to CONFIG_BLOB_MAX
  uint8_t payload[PAYLOAD_SIZE];
  encodeDiscoverReply(in, payload);
  TEST_ASSERT_EQUAL_UINT8(CONFIG_BLOB_MAX, payload[6]);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_packet_size);
  RUN_TEST(test_crc16_check_vector);
  RUN_TEST(test_seal_and_validate);
  RUN_TEST(test_validate_rejects_corruption);
  RUN_TEST(test_validate_rejects_wrong_version);
  RUN_TEST(test_station_blob_roundtrip);
  RUN_TEST(test_station_blob_zero_led_falls_back);
  RUN_TEST(test_target_blob_roundtrip);
  RUN_TEST(test_target_blob_endianness);
  RUN_TEST(test_hit_report_roundtrip);
  RUN_TEST(test_discover_reply_roundtrip);
  RUN_TEST(test_blob_len_clamped);
  return UNITY_END();
}
