// IrTelegram – the IR shot telegram of the Infinitag Now system (v0x03).
//
// Since protocol v0x03 a shot is no longer a plain 38-kHz burst but a short
// data telegram so the target knows WHO fired (dynamic hit routing) and how
// much damage the shot carries (hitpoints, later). Pure C++ without Arduino
// dependencies: the encoder emits a mark/space timing table for any 38-kHz
// transmitter, the decoder is fed with measured phase durations from the
// receiver ISR – both compile natively for unit tests.
//
// Physical layer (38 kHz carrier, TSOP4138 receiver, all times in µs):
//   header:  mark 2400 + space 600
//   bit:     mark 600 + space 600 (=0)  or  mark 600 + space 1200 (=1)
//   stop:    mark 600 (delimits the last bit's space)
//   16 data bits, MSB first:
//     [15..12] telegram version (IRT_VERSION = 0x1)
//     [11..8]  shooter_id (IR id of the firing station, 0..15)
//     [7..4]   damage     (hit strength, sender clamps to >= 1)
//     [3..0]   CRC-4 (poly x^4+x+1) over bits 15..4
//   duration: 22.8 ms (all 0) .. 32.4 ms (all 1) – fits the < 50 ms
//   IR-to-sound latency budget (Doc 11 item 41, revised 2026-07-24).

#pragma once

#include <stdint.h>
#include <stddef.h>

namespace inow {

constexpr uint8_t IRT_VERSION = 0x1;
constexpr size_t IRT_BITS = 16;

// Nominal timings (µs).
constexpr uint16_t IRT_HDR_MARK_US = 2400;
constexpr uint16_t IRT_HDR_SPACE_US = 600;
constexpr uint16_t IRT_BIT_MARK_US = 600;
constexpr uint16_t IRT_SPACE0_US = 600;
constexpr uint16_t IRT_SPACE1_US = 1200;

// Timing table size: header mark/space + 16 x (mark + space) + stop mark.
constexpr size_t IRT_PULSE_COUNT = 2 + IRT_BITS * 2 + 1;  // 35

// Receiver tolerance windows (µs). The TSOP stretches/shrinks pulses by a
// few hundred µs depending on signal strength, hence the generous bands.
constexpr uint32_t IRT_HDR_MARK_MIN = 1800, IRT_HDR_MARK_MAX = 3200;
constexpr uint32_t IRT_HDR_SPACE_MIN = 250, IRT_HDR_SPACE_MAX = 1000;
constexpr uint32_t IRT_BIT_MARK_MIN = 250, IRT_BIT_MARK_MAX = 1000;
constexpr uint32_t IRT_SPACE_MIN = 250;      // below: noise -> reset
constexpr uint32_t IRT_SPACE01_SPLIT = 900;  // < split = 0, >= split = 1
constexpr uint32_t IRT_SPACE_MAX = 1700;     // above: frame gap -> reset

// CRC-4 (poly x^4+x+1, init 0, MSB first) over the 12 data bits.
uint8_t irtCrc4(uint16_t data12);

// Builds a complete frame (version + shooter_id + damage + CRC).
// damage 0 is clamped to 1 – a shot always carries at least 1 damage.
uint16_t irtEncode(uint8_t shooterId, uint8_t damage);

// Validates version + CRC and extracts the fields. False = invalid frame.
bool irtDecodeFrame(uint16_t frame, uint8_t &shooterId, uint8_t &damage);

// Fills `durations` with the mark/space timing table of the frame:
// even index = mark (carrier on), odd index = space (carrier off).
// Returns the number of entries (IRT_PULSE_COUNT) or 0 if maxLen is too
// small.
size_t irtTimings(uint16_t frame, uint16_t *durations, size_t maxLen);

// Edge-fed receive decoder. The ISR measures the duration of each finished
// mark/space phase and calls feed(); ISR-safe (no allocation, tiny state).
// feed() returns true when a complete frame with valid CRC was received –
// fetch it with take().
class IrtDecoder {
 public:
  bool feed(bool mark, uint32_t us);
  bool take(uint16_t &frame);
  void reset() { _state = IDLE; }

 private:
  enum State : uint8_t { IDLE, HDR_SPACE, BIT_MARK, BIT_SPACE };
  // A mark in header range restarts a frame from any state.
  bool tryHeader(bool mark, uint32_t us);

  State _state = IDLE;
  uint8_t _nbits = 0;
  uint16_t _acc = 0;
  volatile bool _pending = false;
  volatile uint16_t _frame = 0;
};

}  // namespace inow
