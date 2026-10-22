// WebServer with a short patience for silent connections.
//
// The stock WebServer::handleClient() serves ONE connection at a time and,
// once it has accepted one, waits up to HTTP_MAX_DATA_WAIT (5 s, a bare
// #define - not overridable) for the first request byte. Mobile browsers
// and captive-portal assistants open spare connections they may never use,
// so every page click could stall 5 s behind such a silent socket
// (observed on the config box 2026-10-22: 2-6 s per click, captive
// detection timing out). dropIdleClient() closes a connection that has
// sent nothing for graceMs; a browser that needed it simply reconnects.

#pragma once

// Arduino/ESP32 only – excluded from native test builds.
#ifdef ARDUINO
#include <WebServer.h>

class ResponsiveWebServer : public WebServer {
 public:
  using WebServer::WebServer;

  // Call right before handleClient() in every loop iteration.
  void dropIdleClient(uint32_t graceMs) {
    if (_currentStatus == HC_WAIT_READ && !_currentClient.available() &&
        millis() - _statusChange > graceMs) {
      _currentClient.stop();
      _currentClient = WiFiClient();
      _currentStatus = HC_NONE;
    }
  }
};

#endif  // ARDUINO
