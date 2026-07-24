#include "IrTelegram.h"

namespace inow {

uint8_t irtCrc4(uint16_t data12) {
  uint8_t crc = 0;
  for (int i = 11; i >= 0; i--) {
    const uint8_t in = (data12 >> i) & 1;
    const uint8_t top = (crc >> 3) & 1;
    crc = (uint8_t)((crc << 1) & 0x0F);
    if (top ^ in) crc ^= 0x03;  // x^4 + x + 1
  }
  return crc;
}

uint16_t irtEncode(uint8_t shooterId, uint8_t damage) {
  if (damage == 0) damage = 1;
  const uint16_t data12 = (uint16_t)((IRT_VERSION & 0x0F) << 8) |
                          (uint16_t)((shooterId & 0x0F) << 4) |
                          (uint16_t)(damage & 0x0F);
  return (uint16_t)(data12 << 4) | irtCrc4(data12);
}

bool irtDecodeFrame(uint16_t frame, uint8_t &shooterId, uint8_t &damage) {
  const uint16_t data12 = frame >> 4;
  if (irtCrc4(data12) != (frame & 0x0F)) return false;
  if (((frame >> 12) & 0x0F) != IRT_VERSION) return false;
  shooterId = (frame >> 8) & 0x0F;
  damage = (frame >> 4) & 0x0F;
  return true;
}

size_t irtTimings(uint16_t frame, uint16_t *durations, size_t maxLen) {
  if (maxLen < IRT_PULSE_COUNT) return 0;
  size_t n = 0;
  durations[n++] = IRT_HDR_MARK_US;
  durations[n++] = IRT_HDR_SPACE_US;
  for (int i = IRT_BITS - 1; i >= 0; i--) {
    durations[n++] = IRT_BIT_MARK_US;
    durations[n++] = ((frame >> i) & 1) ? IRT_SPACE1_US : IRT_SPACE0_US;
  }
  durations[n++] = IRT_BIT_MARK_US;  // stop mark
  return n;
}

bool IrtDecoder::tryHeader(bool mark, uint32_t us) {
  if (mark && us >= IRT_HDR_MARK_MIN && us <= IRT_HDR_MARK_MAX) {
    _state = HDR_SPACE;
    return true;
  }
  _state = IDLE;
  return false;
}

bool IrtDecoder::feed(bool mark, uint32_t us) {
  switch (_state) {
    case IDLE:
      tryHeader(mark, us);
      return false;

    case HDR_SPACE:
      if (!mark && us >= IRT_HDR_SPACE_MIN && us <= IRT_HDR_SPACE_MAX) {
        _nbits = 0;
        _acc = 0;
        _state = BIT_MARK;
      } else {
        tryHeader(mark, us);
      }
      return false;

    case BIT_MARK:
      if (mark && us >= IRT_BIT_MARK_MIN && us <= IRT_BIT_MARK_MAX) {
        if (_nbits == IRT_BITS) {
          // stop mark -> frame complete; report only valid frames
          _state = IDLE;
          uint8_t id, dmg;
          if (irtDecodeFrame(_acc, id, dmg)) {
            _frame = _acc;
            _pending = true;
            return true;
          }
          return false;
        }
        _state = BIT_SPACE;
      } else {
        tryHeader(mark, us);
      }
      return false;

    case BIT_SPACE:
    default:
      if (!mark && us >= IRT_SPACE_MIN && us <= IRT_SPACE_MAX) {
        _acc = (uint16_t)(_acc << 1) | (us >= IRT_SPACE01_SPLIT ? 1 : 0);
        _nbits++;
        _state = BIT_MARK;
      } else {
        tryHeader(mark, us);
      }
      return false;
  }
}

bool IrtDecoder::take(uint16_t &frame) {
  if (!_pending) return false;
  frame = _frame;
  _pending = false;
  return true;
}

}  // namespace inow
