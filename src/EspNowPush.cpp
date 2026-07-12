// Arduino/ESP32 only – excluded from native test builds.
#ifdef ARDUINO
#include "EspNowPush.h"

#include <Update.h>

using namespace inow;

// Raw data frame: 'I' 'N' 'W' 'D' + u32 frame index (LE) + data.
static const uint8_t RAW_MAGIC[4] = {'I', 'N', 'W', 'D'};
static constexpr size_t RAW_HDR = 8;

static uint32_t rdU32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}
static void wrU32(uint8_t *p, uint32_t v) {
  p[0] = v & 0xFF;
  p[1] = (v >> 8) & 0xFF;
  p[2] = (v >> 16) & 0xFF;
  p[3] = (v >> 24) & 0xFF;
}

// ==============================================================================
// receiver
// ==============================================================================

EspNowPushReceiver *EspNowPushReceiver::s_instance = nullptr;

void EspNowPushReceiver::begin(EspNowService *net) {
  _net = net;
  s_instance = this;
  net->setRawHandler(rawHandlerStatic);
}

void EspNowPushReceiver::rawHandlerStatic(const uint8_t mac[6],
                                          const uint8_t *data, int len) {
  EspNowPushReceiver *self = s_instance;
  if (!self || self->_state != RECEIVING) return;
  if (len < (int)RAW_HDR || len > 250) return;
  if (memcmp(data, RAW_MAGIC, 4) != 0) return;
  if (memcmp(mac, self->_srcMac, 6) != 0) return;  // only our sender

  portENTER_CRITICAL(&self->_mux);
  const size_t next = (self->_qHead + 1) % QUEUE;
  if (next != self->_qTail) {  // drop frame if full – retransmit heals it
    RawFrame &slot = self->_queue[self->_qHead];
    slot.len = (uint8_t)len;
    memcpy(slot.data, data, len);
    self->_qHead = next;
  }
  portEXIT_CRITICAL(&self->_mux);
}

void EspNowPushReceiver::sendAck(uint16_t window, uint32_t missing,
                                 uint8_t status) {
  Packet p;
  init(p, MSG_PUSH_ACK, 0);  // device type irrelevant for the ack
  PushAck a;
  a.window = window;
  a.missing = missing;
  a.status = status;
  encodePushAck(a, p.payload);
  _net->send(_srcMac, p);
  _lastAckMs = millis();
}

void EspNowPushReceiver::fail(uint8_t status) {
  // capture the REAL update error before abort() overwrites it
  _updErr = Update.getError();
  strncpy(_updErrStr, Update.errorString(), sizeof(_updErrStr) - 1);
  Update.abort();
  _failCode = status;
  sendAck(0xFFFF, 0, status);
  delay(2);
  sendAck(0xFFFF, 0, status);  // twice – a lost error ack stalls the box
  _state = FAILED;
}

void EspNowPushReceiver::onControl(const RxPacket &rx) {
  const Packet &p = rx.pkt;

  if (p.msg_type == MSG_PUSH_BEGIN) {
    if (_state == RECEIVING && memcmp(rx.mac, _srcMac, 6) != 0) {
      // another box tries to push while we are busy
      Packet busy;
      init(busy, MSG_PUSH_ACK, 0);
      PushAck a;
      a.window = 0xFFFF;
      a.status = PUSH_ACK_BUSY;
      encodePushAck(a, busy.payload);
      _net->send(rx.mac, busy);
      return;
    }
    if (_state != RECEIVING) {
      decodePushBegin(p.payload, _begin);
      memcpy(_srcMac, rx.mac, 6);
      if (_begin.size == 0 || !Update.begin(_begin.size)) {
        Update.printError(Serial);
        fail(PUSH_ACK_FINAL_FLASH);
        return;
      }
      _totalFrames =
          (_begin.size + PUSH_FRAME_DATA - 1) / PUSH_FRAME_DATA;
      _window = 0;
      _haveMask = 0;
      _bytesDone = 0;
      _crc = 0;
      _state = RECEIVING;
      Serial.printf("[PUSH] Empfang: %u Bytes, v%u.%u.%u, %u Frames\n",
                    _begin.size, _begin.major, _begin.minor, _begin.patch,
                    _totalFrames);
    }
    // (duplicate BEGIN from our sender: ignore, transfer continues)
    _lastRxMs = millis();
    return;
  }

  if (p.msg_type == MSG_PUSH_END && _state == RECEIVING &&
      memcmp(rx.mac, _srcMac, 6) == 0) {
    _lastRxMs = millis();
    const uint32_t framesDone = (uint32_t)_window * PUSH_WINDOW_FRAMES;
    if (framesDone < _totalFrames) {
      // still missing data – re-ack the current window instead
      const uint32_t expect = windowExpectMask();
      sendAck(_window, expect & ~_haveMask, PUSH_ACK_WINDOW);
      return;
    }
    Serial.printf("[PUSH] END: %u/%u Bytes, %u Fenster, CRC %08X/%08X\n",
                  (unsigned)_bytesDone, _begin.size, _window,
                  _crc, _begin.crc32);
    if (_bytesDone != _begin.size || _crc != _begin.crc32) {
      Serial.println("[PUSH] CRC-/Groessen-Fehler");
      fail(PUSH_ACK_FINAL_CRC);
      return;
    }
    if (!Update.end(true)) {
      Update.printError(Serial);
      fail(PUSH_ACK_FINAL_FLASH);
      return;
    }
    Serial.println("[PUSH] Fertig, Boot-Slot gewechselt");
    sendAck(0xFFFF, 0, PUSH_ACK_FINAL_OK);
    delay(50);  // let the ack leave the radio
    sendAck(0xFFFF, 0, PUSH_ACK_FINAL_OK);  // once more, acks are precious
    _state = DONE;
  }
}

// Expected frame mask of the current window (last window may be partial).
uint32_t EspNowPushReceiver::windowExpectMask() const {
  const uint32_t first = (uint32_t)_window * PUSH_WINDOW_FRAMES;
  uint32_t remain = _totalFrames - first;
  if (remain > PUSH_WINDOW_FRAMES) remain = PUSH_WINDOW_FRAMES;
  return remain >= 32 ? 0xFFFFFFFF : ((1UL << remain) - 1);
}

void EspNowPushReceiver::finishWindow() {
  const uint32_t first = (uint32_t)_window * PUSH_WINDOW_FRAMES;
  uint32_t frames = _totalFrames - first;
  if (frames > PUSH_WINDOW_FRAMES) frames = PUSH_WINDOW_FRAMES;

  for (uint32_t i = 0; i < frames; i++) {
    const uint32_t frameIdx = first + i;
    size_t n = PUSH_FRAME_DATA;
    if ((frameIdx + 1) * PUSH_FRAME_DATA > _begin.size) {
      n = _begin.size - frameIdx * PUSH_FRAME_DATA;
    }
    if (Update.write(_winBuf[i], n) != n) {
      Update.printError(Serial);
      fail(PUSH_ACK_FINAL_FLASH);
      return;
    }
    _crc = crc32(_crc, _winBuf[i], n);
    _bytesDone += n;
  }
  sendAck(_window, 0, PUSH_ACK_WINDOW);  // window complete
  delay(2);
  sendAck(_window, 0, PUSH_ACK_WINDOW);  // twice – a lost ack stalls all
  _window++;
  _haveMask = 0;
}

void EspNowPushReceiver::loop() {
  if (_state != RECEIVING) return;

  // drain the queue
  for (;;) {
    RawFrame frame;
    bool have = false;
    portENTER_CRITICAL(&_mux);
    if (_qTail != _qHead) {
      frame = _queue[_qTail];
      _qTail = (_qTail + 1) % QUEUE;
      have = true;
    }
    portEXIT_CRITICAL(&_mux);
    if (!have) break;

    _lastRxMs = millis();
    const uint32_t frameIdx = rdU32(frame.data + 4);
    const uint32_t first = (uint32_t)_window * PUSH_WINDOW_FRAMES;
    if (frameIdx < first) {
      // Frame of an already flashed window: our window ack got lost and
      // the sender is retransmitting. Re-ack that window (throttled) so
      // the sender advances instead of giving up after its retries.
      if (millis() - _lastAckMs > 100) {
        sendAck((uint16_t)(frameIdx / PUSH_WINDOW_FRAMES), 0,
                PUSH_ACK_WINDOW);
      }
      continue;
    }
    if (frameIdx >= first + PUSH_WINDOW_FRAMES) {
      continue;  // frame from a future window (should not happen)
    }
    const uint32_t i = frameIdx - first;
    const size_t n = frame.len - RAW_HDR;
    if (n > PUSH_FRAME_DATA) continue;
    memcpy(_winBuf[i], frame.data + RAW_HDR, n);
    _haveMask |= (1UL << i);

    if ((_haveMask & windowExpectMask()) == windowExpectMask()) {
      finishWindow();
      if (_state != RECEIVING) return;
    }
  }

  // periodic ack so the sender learns about missing frames even when the
  // tail of a window got lost completely
  if (millis() - _lastAckMs > 250 && millis() - _lastRxMs > 150 &&
      _haveMask != 0) {
    sendAck(_window, windowExpectMask() & ~_haveMask, PUSH_ACK_WINDOW);
  }
}

// ==============================================================================
// sender
// ==============================================================================

bool EspNowPushSender::start(EspNowService *net, const uint8_t mac[6],
                             ReadFn read, void *ctx, uint32_t size,
                             uint32_t crc, uint8_t maj, uint8_t min,
                             uint8_t pat) {
  if (size == 0 || !read) return false;
  _net = net;
  memcpy(_mac, mac, 6);
  _read = read;
  _ctx = ctx;
  _size = size;
  _crc = crc;
  _maj = maj;
  _min = min;
  _pat = pat;
  _totalFrames = (size + PUSH_FRAME_DATA - 1) / PUSH_FRAME_DATA;
  _windows = (uint16_t)((_totalFrames + PUSH_WINDOW_FRAMES - 1) /
                        PUSH_WINDOW_FRAMES);
  _window = 0;
  _gotAnyAck = false;
  _endSent = false;
  _retries = 0;
  _ackedBytes = 0;
  _finalStatus = 0xFF;
  _state = PUSHING;

  sendBegin();
  sendWindow(windowMask(0));
  _deadline = millis() + 500;
  return true;
}

uint32_t EspNowPushSender::windowMask(uint16_t window) const {
  const uint32_t first = (uint32_t)window * PUSH_WINDOW_FRAMES;
  uint32_t remain = _totalFrames - first;
  if (remain > PUSH_WINDOW_FRAMES) remain = PUSH_WINDOW_FRAMES;
  return remain >= 32 ? 0xFFFFFFFF : ((1UL << remain) - 1);
}

void EspNowPushSender::sendBegin() {
  Packet p;
  init(p, MSG_PUSH_BEGIN, 0);
  PushBegin b;
  b.size = _size;
  b.crc32 = _crc;
  b.major = _maj;
  b.minor = _min;
  b.patch = _pat;
  encodePushBegin(b, p.payload);
  _net->send(_mac, p);
}

void EspNowPushSender::sendWindow(uint32_t mask) {
  uint8_t frame[250];
  memcpy(frame, RAW_MAGIC, 4);
  const uint32_t first = (uint32_t)_window * PUSH_WINDOW_FRAMES;
  for (uint8_t i = 0; i < PUSH_WINDOW_FRAMES; i++) {
    if (!(mask & (1UL << i))) continue;
    const uint32_t frameIdx = first + i;
    if (frameIdx >= _totalFrames) break;
    size_t n = PUSH_FRAME_DATA;
    if ((frameIdx + 1) * PUSH_FRAME_DATA > _size) {
      n = _size - frameIdx * PUSH_FRAME_DATA;
    }
    wrU32(frame + 4, frameIdx);
    if (_read(_ctx, frameIdx * PUSH_FRAME_DATA, frame + RAW_HDR, n) != n) {
      _finalStatus = PUSH_ACK_FINAL_FLASH;
      _state = FAILED;
      return;
    }
    _net->sendRaw(_mac, frame, RAW_HDR + n);
    delay(2);  // pace the radio; lost frames are healed by the ack bitmap
  }
}

void EspNowPushSender::sendEnd() {
  Packet p;
  init(p, MSG_PUSH_END, 0);
  _net->send(_mac, p);
  _endSent = true;
}

void EspNowPushSender::onControl(const RxPacket &rx) {
  if (_state != PUSHING || memcmp(rx.mac, _mac, 6) != 0) return;
  if (rx.pkt.msg_type != MSG_PUSH_ACK) return;

  PushAck a;
  decodePushAck(rx.pkt.payload, a);
  _gotAnyAck = true;
  _retries = 0;

  if (a.status == PUSH_ACK_FINAL_OK) {
    _ackedBytes = _size;
    _finalStatus = a.status;
    _state = DONE;
    return;
  }
  if (a.status != PUSH_ACK_WINDOW) {  // final error / busy
    Serial.printf("[PUSH] Geraet meldet Fehler-Status %u\n", a.status);
    _finalStatus = a.status;
    _state = FAILED;
    return;
  }
  if (a.window != _window) return;  // stale ack

  if (a.missing == 0) {
    // window done on the device
    const uint32_t first = (uint32_t)_window * PUSH_WINDOW_FRAMES;
    uint32_t frames = _totalFrames - first;
    if (frames > PUSH_WINDOW_FRAMES) frames = PUSH_WINDOW_FRAMES;
    _ackedBytes += frames * PUSH_FRAME_DATA;
    if (_ackedBytes > _size) _ackedBytes = _size;

    _window++;
    if (_window >= _windows) {
      sendEnd();
      _deadline = millis() + 2000;
    } else {
      sendWindow(windowMask(_window));
      _deadline = millis() + 500;
    }
  } else {
    sendWindow(a.missing);  // selective retransmit
    _deadline = millis() + 500;
  }
}

void EspNowPushSender::loop() {
  if (_state != PUSHING) return;
  if (millis() < _deadline) return;

  // no (or lost) ack – retry the current step
  if (++_retries > (_endSent ? 8 : 20)) {
    Serial.printf("[PUSH] Abbruch: keine Antwort (Fenster %u/%u, End=%d)\n",
                  _window, _windows, _endSent);
    _finalStatus = 0xFE;  // give-up marker: device unreachable
    _state = FAILED;
    return;
  }
  if (_endSent) {
    sendEnd();
    _deadline = millis() + 2000;
  } else {
    if (!_gotAnyAck) sendBegin();  // device may have missed the start
    sendWindow(windowMask(_window));
    _deadline = millis() + 500;
  }
}

#endif  // ARDUINO
