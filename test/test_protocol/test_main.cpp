// Unit tests for the InfinitagNow protocol library (v0x03).
// Run on the PC:  pio test -e native

#include <unity.h>
#include "InfinitagNow.h"
#include "IrTelegram.h"
#include "SoundCatalog.h"

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
  // v0x01/v0x02 packets (and any other version) must be dropped.
  Packet p;
  init(p, MSG_HIT_REPORT, DEV_TARGET);
  p.version = 0x01;
  seal(p);
  TEST_ASSERT_FALSE(validate(reinterpret_cast<uint8_t *>(&p), sizeof(p)));
  p.version = 0x02;
  seal(p);
  TEST_ASSERT_FALSE(validate(reinterpret_cast<uint8_t *>(&p), sizeof(p)));
}

void test_rescue_messages_pass_any_version() {
  // Rescue anchor: DISCOVER_REQ/REPLY + UPDATE_BEGIN/ACK must validate
  // regardless of the version byte (past 0x02 and hypothetical future 0x7F).
  const uint8_t msgs[] = {MSG_DISCOVER_REQ, MSG_DISCOVER_REPLY,
                          MSG_UPDATE_BEGIN, MSG_UPDATE_ACK};
  const uint8_t versions[] = {0x01, 0x02, 0x7F};
  for (uint8_t m : msgs) {
    for (uint8_t v : versions) {
      Packet p;
      init(p, m, DEV_STATION);
      p.version = v;
      seal(p);
      TEST_ASSERT_TRUE(validate(reinterpret_cast<uint8_t *>(&p), sizeof(p)));
      TEST_ASSERT_TRUE(isRescueMsg(m));
    }
  }
}

void test_rescue_still_requires_crc() {
  // The version tolerance must NOT weaken the CRC check.
  Packet p;
  init(p, MSG_UPDATE_BEGIN, DEV_TARGET);
  p.version = 0x02;
  p.payload[0] = 5;
  seal(p);
  uint8_t buf[sizeof(Packet)];
  memcpy(buf, &p, sizeof(p));
  buf[9] ^= 0xFF;
  TEST_ASSERT_FALSE(validate(buf, sizeof(buf)));
}

void test_non_rescue_messages_stay_version_locked() {
  const uint8_t msgs[] = {MSG_HIT_REPORT, MSG_CFG_WRITE, MSG_CFG_ACK,
                          MSG_DEBUG_CMD, MSG_PUSH_BEGIN};
  for (uint8_t m : msgs) {
    TEST_ASSERT_FALSE(isRescueMsg(m));
    Packet p;
    init(p, m, DEV_STATION);
    p.version = 0x02;
    seal(p);
    TEST_ASSERT_FALSE(validate(reinterpret_cast<uint8_t *>(&p), sizeof(p)));
  }
}

void test_station_blob_roundtrip() {
  StationConfig in;
  in.volume_pct = 55;
  in.led_ready = LED_R | LED_W;   // red + white die
  in.led_busy = LED_B;
  in.laser_mode = LASER_MODE_ON;
  in.laser_glow = 4;              // 2 s
  in.ir_id = 7;
  in.led_bright_pct = 40;
  uint8_t blob[STATION_BLOB_SIZE];
  encodeStationConfig(in, blob);
  StationConfig out;
  decodeStationConfig(blob, sizeof(blob), out);
  TEST_ASSERT_EQUAL_UINT8(55, out.volume_pct);
  TEST_ASSERT_EQUAL_UINT8(LED_R | LED_W, out.led_ready);
  TEST_ASSERT_EQUAL_UINT8(LED_B, out.led_busy);
  TEST_ASSERT_EQUAL_UINT8(LASER_MODE_ON, out.laser_mode);
  TEST_ASSERT_EQUAL_UINT8(4, out.laser_glow);
  TEST_ASSERT_EQUAL_UINT8(7, out.ir_id);
  TEST_ASSERT_EQUAL_UINT8(40, out.led_bright_pct);
}

void test_led_bright_zero_falls_back_to_default() {
  // 0 = field not set (old sender) -> receivers keep 100 %.
  uint8_t blob[STATION_BLOB_SIZE] = {0};
  StationConfig sOut;
  decodeStationConfig(blob, sizeof(blob), sOut);
  TEST_ASSERT_EQUAL_UINT8(100, sOut.led_bright_pct);

  uint8_t tblob[TARGET_BLOB_SIZE] = {0};
  TargetConfig tOut;
  decodeTargetConfig(tblob, sizeof(tblob), tOut);
  TEST_ASSERT_EQUAL_UINT8(100, tOut.led_bright_pct);
}

void test_station_blob_ir_id_zero_is_valid() {
  // ir_id 0 = factory group, NOT "unset" – it must survive the roundtrip.
  StationConfig in;
  in.ir_id = 0;
  uint8_t blob[STATION_BLOB_SIZE];
  encodeStationConfig(in, blob);
  StationConfig out;
  out.ir_id = 9;  // stale value must be overwritten
  decodeStationConfig(blob, sizeof(blob), out);
  TEST_ASSERT_EQUAL_UINT8(0, out.ir_id);
}

void test_station_blob_laser_defaults() {
  // Old sender: bytes 3/4 are zero -> receiver keeps the defaults
  // (afterglow 500 ms), so pre-laser boxes stay compatible.
  uint8_t blob[STATION_BLOB_SIZE] = {0};
  blob[0] = 80;
  StationConfig out;
  decodeStationConfig(blob, sizeof(blob), out);
  TEST_ASSERT_EQUAL_UINT8(LASER_MODE_GLOW, out.laser_mode);
  TEST_ASSERT_EQUAL_UINT8(1, out.laser_glow);
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
  TargetConfig in;
  in.sound_id = 6;
  in.hit_time_ms = 12345;
  in.cooldown_ms = 2500;
  in.sw_animation = 1;
  in.sw_channels = 0b101;
  in.led_bright_pct = 65;
  uint8_t blob[TARGET_BLOB_SIZE];
  encodeTargetConfig(in, blob);
  TargetConfig out;
  decodeTargetConfig(blob, sizeof(blob), out);
  TEST_ASSERT_EQUAL_UINT8(6, out.sound_id);
  TEST_ASSERT_EQUAL_UINT16(12345, out.hit_time_ms);
  TEST_ASSERT_EQUAL_UINT16(2500, out.cooldown_ms);
  TEST_ASSERT_EQUAL_UINT8(1, out.sw_animation);
  TEST_ASSERT_EQUAL_UINT8(0b101, out.sw_channels);
  TEST_ASSERT_EQUAL_UINT8(65, out.led_bright_pct);
}

void test_target_blob_endianness() {
  // hit_time_ms = 0x1234 must serialize little-endian at offsets 1..2
  TargetConfig in;
  in.hit_time_ms = 0x1234;
  uint8_t blob[TARGET_BLOB_SIZE];
  encodeTargetConfig(in, blob);
  TEST_ASSERT_EQUAL_HEX8(0x34, blob[1]);
  TEST_ASSERT_EQUAL_HEX8(0x12, blob[2]);
}

void test_hit_report_roundtrip() {
  uint8_t payload[PAYLOAD_SIZE];
  encodeHitReport(7, 9, 3, payload);
  uint8_t shooter = 0, sound = 0, damage = 0;
  decodeHitReport(payload, shooter, sound, damage);
  TEST_ASSERT_EQUAL_UINT8(7, shooter);
  TEST_ASSERT_EQUAL_UINT8(9, sound);
  TEST_ASSERT_EQUAL_UINT8(3, damage);
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

void test_sound_catalog() {
  TEST_ASSERT_TRUE(SOUND_COUNT >= 1);
  TEST_ASSERT_EQUAL_UINT8(15, SOUND_COUNT);
  TEST_ASSERT_EQUAL_STRING("Test", soundName(0));
  TEST_ASSERT_EQUAL_STRING("Witch", soundName(SOUND_COUNT - 1));
  TEST_ASSERT_EQUAL_STRING("?", soundName(SOUND_COUNT));  // out of range
  // OLED rows break beyond 8 characters – enforce the display limit.
  for (uint8_t i = 0; i < SOUND_COUNT; i++) {
    TEST_ASSERT_TRUE(strlen(SOUND_CATALOG[i].name) <= 8);
    TEST_ASSERT_EQUAL_CHAR('/', SOUND_CATALOG[i].file[0]);
  }
}

void test_crc32_check_vector() {
  // CRC-32/IEEE("123456789") = 0xCBF43926
  const uint8_t v[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  TEST_ASSERT_EQUAL_HEX32(0xCBF43926, crc32(0, v, sizeof(v)));
  // incremental feeding must give the same result
  uint32_t c = crc32(0, v, 4);
  c = crc32(c, v + 4, 5);
  TEST_ASSERT_EQUAL_HEX32(0xCBF43926, c);
}

void test_push_begin_roundtrip() {
  PushBegin in;
  in.size = 857184;
  in.crc32 = 0xDEADBEEF;
  in.major = 0; in.minor = 3; in.patch = 1;
  uint8_t payload[PAYLOAD_SIZE];
  encodePushBegin(in, payload);
  PushBegin out;
  decodePushBegin(payload, out);
  TEST_ASSERT_EQUAL_UINT32(857184, out.size);
  TEST_ASSERT_EQUAL_HEX32(0xDEADBEEF, out.crc32);
  TEST_ASSERT_EQUAL_UINT8(3, out.minor);
  TEST_ASSERT_EQUAL_UINT8((uint8_t)PUSH_FRAME_DATA, payload[11]);
  TEST_ASSERT_EQUAL_UINT8(PUSH_WINDOW_FRAMES, payload[12]);
}

void test_push_ack_roundtrip() {
  PushAck in;
  in.window = 517;
  in.missing = 0x00010005;  // frames 0, 2 and 16 missing
  in.status = PUSH_ACK_WINDOW;
  in.detail = 9;  // e.g. UPDATE_ERROR_ACTIVATE relayed to the box
  uint8_t payload[PAYLOAD_SIZE];
  encodePushAck(in, payload);
  PushAck out;
  decodePushAck(payload, out);
  TEST_ASSERT_EQUAL_UINT16(517, out.window);
  TEST_ASSERT_EQUAL_HEX32(0x00010005, out.missing);
  TEST_ASSERT_EQUAL_UINT8(PUSH_ACK_WINDOW, out.status);
  TEST_ASSERT_EQUAL_UINT8(9, out.detail);
}

void test_blob_len_clamped() {
  DiscoverReply in;
  in.config_blob_len = 200;  // bogus – must be clamped to CONFIG_BLOB_MAX
  uint8_t payload[PAYLOAD_SIZE];
  encodeDiscoverReply(in, payload);
  TEST_ASSERT_EQUAL_UINT8(CONFIG_BLOB_MAX, payload[6]);
}

// --- IR telegram (IrTelegram.h, v0x03) ---------------------------------------

void test_irt_crc4_detects_bit_flips() {
  const uint16_t data12 = 0x175;  // arbitrary 12-bit value
  const uint8_t crc = irtCrc4(data12);
  TEST_ASSERT_TRUE(crc <= 0x0F);
  for (int i = 0; i < 12; i++) {
    TEST_ASSERT_NOT_EQUAL(crc, irtCrc4(data12 ^ (1 << i)));
  }
}

void test_irt_encode_decode_roundtrip() {
  for (uint8_t id = 0; id <= 15; id++) {
    const uint16_t frame = irtEncode(id, 5);
    uint8_t outId = 0xFF, outDmg = 0;
    TEST_ASSERT_TRUE(irtDecodeFrame(frame, outId, outDmg));
    TEST_ASSERT_EQUAL_UINT8(id, outId);
    TEST_ASSERT_EQUAL_UINT8(5, outDmg);
  }
}

void test_irt_encode_clamps_damage() {
  // A shot always carries at least 1 damage.
  uint8_t id = 0, dmg = 0;
  TEST_ASSERT_TRUE(irtDecodeFrame(irtEncode(3, 0), id, dmg));
  TEST_ASSERT_EQUAL_UINT8(1, dmg);
}

void test_irt_decode_rejects_bad_crc_and_version() {
  const uint16_t frame = irtEncode(4, 2);
  uint8_t id, dmg;
  TEST_ASSERT_FALSE(irtDecodeFrame(frame ^ 0x0001, id, dmg));  // CRC flip
  // version nibble changed, CRC recomputed to match -> version check hits
  const uint16_t data12 = (uint16_t)((frame >> 4) & 0x0FF) | (0x2 << 8);
  const uint16_t alien = (uint16_t)(data12 << 4) | irtCrc4(data12);
  TEST_ASSERT_FALSE(irtDecodeFrame(alien, id, dmg));
}

// Feeds a sender timing table into the decoder, optionally distorting every
// duration by `skewUs` (TSOP pulse stretching) – returns the decoded frame.
static bool playTimings(const uint16_t *t, size_t n, int skewUs,
                        uint16_t &frame) {
  IrtDecoder dec;
  bool got = false;
  for (size_t i = 0; i < n; i++) {
    const bool mark = (i % 2) == 0;
    // marks stretch, the enclosed spaces shrink by the same amount
    const int32_t us = (int32_t)t[i] + (mark ? skewUs : -skewUs);
    if (dec.feed(mark, (uint32_t)us)) got = true;
  }
  return got && dec.take(frame);
}

void test_irt_decoder_roundtrip_over_the_air() {
  const uint16_t sent = irtEncode(11, 3);
  uint16_t t[IRT_PULSE_COUNT];
  const size_t n = irtTimings(sent, t, IRT_PULSE_COUNT);
  TEST_ASSERT_EQUAL_size_t(IRT_PULSE_COUNT, n);

  uint16_t rx = 0;
  TEST_ASSERT_TRUE(playTimings(t, n, 0, rx));
  TEST_ASSERT_EQUAL_HEX16(sent, rx);
}

void test_irt_decoder_tolerates_tsop_skew() {
  // TSOP4138 stretches marks / shrinks spaces by up to a few 100 µs.
  const uint16_t sent = irtEncode(2, 15);
  uint16_t t[IRT_PULSE_COUNT];
  const size_t n = irtTimings(sent, t, IRT_PULSE_COUNT);
  for (int skew = -200; skew <= 200; skew += 100) {
    uint16_t rx = 0;
    TEST_ASSERT_TRUE(playTimings(t, n, skew, rx));
    TEST_ASSERT_EQUAL_HEX16(sent, rx);
  }
}

void test_irt_decoder_rejects_corrupted_frame() {
  // A flipped bit-space changes the payload -> CRC must kill the frame.
  const uint16_t sent = irtEncode(9, 1);
  uint16_t t[IRT_PULSE_COUNT];
  const size_t n = irtTimings(sent, t, IRT_PULSE_COUNT);
  uint16_t mutated[IRT_PULSE_COUNT];
  memcpy(mutated, t, sizeof(t));
  mutated[3] = (t[3] == IRT_SPACE0_US) ? IRT_SPACE1_US : IRT_SPACE0_US;
  uint16_t rx = 0;
  TEST_ASSERT_FALSE(playTimings(mutated, n, 0, rx));
}

void test_irt_decoder_ignores_remote_controls() {
  // NEC remote: 9 ms header (out of range) + short ~560 µs pulses – the
  // decoder must never report a frame, and the old plain 5-ms burst of the
  // v0x02 station must be ignored too.
  IrtDecoder dec;
  TEST_ASSERT_FALSE(dec.feed(true, 9000));  // NEC header mark
  TEST_ASSERT_FALSE(dec.feed(false, 4500));
  for (int i = 0; i < 32; i++) {
    TEST_ASSERT_FALSE(dec.feed(true, 560));
    TEST_ASSERT_FALSE(dec.feed(false, i % 2 ? 560 : 1690));
  }
  uint16_t rx;
  TEST_ASSERT_FALSE(dec.take(rx));
  TEST_ASSERT_FALSE(dec.feed(true, 5000));  // legacy v0x02 burst
  TEST_ASSERT_FALSE(dec.feed(false, 100000));
  TEST_ASSERT_FALSE(dec.take(rx));
}

void test_irt_decoder_resyncs_after_garbage() {
  // Noise first, then a clean frame – the header must resync the decoder.
  const uint16_t sent = irtEncode(1, 1);
  uint16_t t[IRT_PULSE_COUNT];
  const size_t n = irtTimings(sent, t, IRT_PULSE_COUNT);

  IrtDecoder dec;
  dec.feed(true, 400);
  dec.feed(false, 700);
  dec.feed(true, 2500);   // looks like a header mark...
  dec.feed(false, 5000);  // ...but the gap kills it
  bool got = false;
  for (size_t i = 0; i < n; i++) {
    if (dec.feed((i % 2) == 0, t[i])) got = true;
  }
  uint16_t rx = 0;
  TEST_ASSERT_TRUE(got && dec.take(rx));
  TEST_ASSERT_EQUAL_HEX16(sent, rx);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_packet_size);
  RUN_TEST(test_crc16_check_vector);
  RUN_TEST(test_seal_and_validate);
  RUN_TEST(test_validate_rejects_corruption);
  RUN_TEST(test_validate_rejects_wrong_version);
  RUN_TEST(test_rescue_messages_pass_any_version);
  RUN_TEST(test_rescue_still_requires_crc);
  RUN_TEST(test_non_rescue_messages_stay_version_locked);
  RUN_TEST(test_station_blob_roundtrip);
  RUN_TEST(test_station_blob_ir_id_zero_is_valid);
  RUN_TEST(test_led_bright_zero_falls_back_to_default);
  RUN_TEST(test_station_blob_laser_defaults);
  RUN_TEST(test_station_blob_zero_led_falls_back);
  RUN_TEST(test_target_blob_roundtrip);
  RUN_TEST(test_target_blob_endianness);
  RUN_TEST(test_hit_report_roundtrip);
  RUN_TEST(test_discover_reply_roundtrip);
  RUN_TEST(test_sound_catalog);
  RUN_TEST(test_crc32_check_vector);
  RUN_TEST(test_push_begin_roundtrip);
  RUN_TEST(test_push_ack_roundtrip);
  RUN_TEST(test_blob_len_clamped);
  RUN_TEST(test_irt_crc4_detects_bit_flips);
  RUN_TEST(test_irt_encode_decode_roundtrip);
  RUN_TEST(test_irt_encode_clamps_damage);
  RUN_TEST(test_irt_decode_rejects_bad_crc_and_version);
  RUN_TEST(test_irt_decoder_roundtrip_over_the_air);
  RUN_TEST(test_irt_decoder_tolerates_tsop_skew);
  RUN_TEST(test_irt_decoder_rejects_corrupted_frame);
  RUN_TEST(test_irt_decoder_ignores_remote_controls);
  RUN_TEST(test_irt_decoder_resyncs_after_garbage);
  return UNITY_END();
}
