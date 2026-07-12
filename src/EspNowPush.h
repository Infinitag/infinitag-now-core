// ESP-NOW firmware push (Doc 21 Etappe 3), shared by all devices.
//
// The box (sender) streams a firmware image from its ImageStore to a
// device (receiver) in windows of PUSH_WINDOW_FRAMES raw 250-byte
// frames. The receiver buffers one window in RAM, flashes it
// sequentially via Update.h once complete and acknowledges each window
// with a bitmap of missing frames; the sender retransmits selectively.
// PUSH_END finalizes: crc check, Update.end(true), final ack, reboot by
// the caller. An aborted push never boots (boot slot switches only
// after a validated image).
//
// Both classes are driven from loop(); control packets (PUSH_*) must be
// forwarded by the device logic, raw data frames arrive through
// EspNowService::setRawHandler (receiver registers itself).

#pragma once

#ifdef ARDUINO
#include <Arduino.h>

#include "EspNowService.h"
#include "InfinitagNow.h"

// --- receiver (station/target) ------------------------------------------------

class EspNowPushReceiver {
 public:
  enum State : uint8_t { IDLE, RECEIVING, DONE, FAILED };

  // Registers the raw handler on the service. Call once in setup().
  void begin(EspNowService *net);

  // Forward PUSH_BEGIN / PUSH_END control packets here.
  void onControl(const RxPacket &rx);

  // Drain the frame queue, flash completed windows, send acks.
  void loop();

  State state() const { return _state; }
  bool active() const { return _state == RECEIVING; }
  size_t bytesDone() const { return _bytesDone; }
  size_t bytesTotal() const { return _begin.size; }
  // PushAckStatus of a failure (for the device's error screen)
  uint8_t failCode() const { return _failCode; }
  // Update-library error captured BEFORE the cleanup abort (abort would
  // overwrite it with "Aborted"); 0/"" when not a flash failure.
  uint8_t updateError() const { return _updErr; }
  const char *updateErrorString() const { return _updErrStr; }
  // ms since the last received frame/control (watchdog for the caller)
  uint32_t idleMs() const { return millis() - _lastRxMs; }

 private:
  static void rawHandlerStatic(const uint8_t mac[6], const uint8_t *data,
                               int len);
  void onRawFrame(const uint8_t *data, int len);
  void sendAck(uint16_t window, uint32_t missing, uint8_t status);
  uint32_t windowExpectMask() const;
  void finishWindow();
  void fail(uint8_t status);

  static EspNowPushReceiver *s_instance;
  EspNowService *_net = nullptr;

  inow::PushBegin _begin;
  uint8_t _srcMac[6] = {0};
  State _state = IDLE;

  // current window assembly
  uint32_t _totalFrames = 0;
  uint16_t _window = 0;
  uint32_t _haveMask = 0;   // bit i = frame i of current window received
  uint8_t _winBuf[inow::PUSH_WINDOW_FRAMES][inow::PUSH_FRAME_DATA];

  size_t _bytesDone = 0;
  uint8_t _failCode = 0;
  uint8_t _updErr = 0;
  char _updErrStr[32] = "";
  uint32_t _crc = 0;
  uint32_t _lastRxMs = 0;
  uint32_t _lastAckMs = 0;

  // Ring buffer filled from the WiFi task, drained in loop(). Sized for
  // more than one full window: the receiver's OLED redraw blocks ~100 ms
  // in which a whole 16-frame burst can arrive.
  static constexpr size_t QUEUE = 24;
  struct RawFrame {
    uint8_t len;
    uint8_t data[250];
  };
  RawFrame _queue[QUEUE];
  volatile size_t _qHead = 0, _qTail = 0;
  portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;
};

// --- sender (config box) --------------------------------------------------------

class EspNowPushSender {
 public:
  enum State : uint8_t { IDLE, PUSHING, DONE, FAILED };

  // Random-access source reader (LittleFS File on the box).
  using ReadFn = size_t (*)(void *ctx, uint32_t offset, uint8_t *buf,
                            size_t len);

  // Start a push. size/crc/version describe the image.
  bool start(EspNowService *net, const uint8_t mac[6], ReadFn read, void *ctx,
             uint32_t size, uint32_t crc, uint8_t maj, uint8_t min,
             uint8_t pat);

  // Forward PUSH_ACK control packets here.
  void onControl(const RxPacket &rx);

  // Drive the transfer; call frequently.
  void loop();

  State state() const { return _state; }
  size_t bytesDone() const { return _ackedBytes; }
  size_t bytesTotal() const { return _size; }
  uint8_t finalStatus() const { return _finalStatus; }

 private:
  void sendBegin();
  void sendWindow(uint32_t mask);  // send the frames set in mask
  void sendEnd();
  uint32_t windowMask(uint16_t window) const;  // full mask for a window

  EspNowService *_net = nullptr;
  uint8_t _mac[6] = {0};
  ReadFn _read = nullptr;
  void *_ctx = nullptr;

  uint32_t _size = 0, _crc = 0;
  uint8_t _maj = 0, _min = 0, _pat = 0;
  uint32_t _totalFrames = 0;
  uint16_t _windows = 0;

  State _state = IDLE;
  uint16_t _window = 0;
  bool _gotAnyAck = false;
  bool _endSent = false;
  uint8_t _retries = 0;
  uint32_t _deadline = 0;
  size_t _ackedBytes = 0;
  uint8_t _finalStatus = 0xFF;
};

#endif  // ARDUINO
