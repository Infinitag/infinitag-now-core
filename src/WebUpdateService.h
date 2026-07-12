// SoftAP firmware updater, shared by all Infinitag-Now devices.
//
// begin() tears down ESP-NOW, opens an open SoftAP and serves a minimal
// upload page on http://192.168.4.1/ . A posted firmware.bin is written to
// the inactive OTA slot via Update.h; the boot partition only switches after
// a complete, checksum-valid transfer, so an aborted upload always boots the
// old firmware. The caller owns the surrounding UI (OLED text, LED pattern,
// timeout) and must reboot when updateDone() or its own deadline fires.

#pragma once

// Arduino/ESP32 only – excluded from native test builds.
#ifdef ARDUINO
#include <Arduino.h>
#include <WebServer.h>

class WebUpdateService {
 public:
  // apName: open SoftAP SSID, e.g. "infinitag-sta-220AAC".
  // fwVersion: running version shown on the upload page, e.g. "0.2.0".
  // deviceLabel: shown prominently on the page ("Station 220AAC") so the
  //   user always sees WHICH device they are about to flash.
  // filePrefix: expected firmware file name prefix ("infinitag-station");
  //   uploads whose name does not start with it are rejected. This guards
  //   against flashing the wrong device type with the SAME chip – images
  //   for a different chip are already rejected by the IDF image check
  //   (chip id) at Update.end().
  bool begin(const char *apName, const char *fwVersion,
             const char *deviceLabel, const char *filePrefix);

  // Service HTTP; call every loop() iteration while the mode is active.
  void loop();

  // True once a valid image is flashed; caller shows "OK" and reboots.
  bool updateDone() const { return _done; }

  // True while an upload is in progress (caller may show a spinner).
  bool uploadActive() const { return _uploading; }

  // Number of stations connected to our AP (0 = nobody there yet).
  int clientCount() const;

  const char *apIp() const { return "192.168.4.1"; }

 private:
  void handleRoot();
  void handleUploadData();
  void handleUploadDone();

  WebServer _server{80};
  char _version[16] = "?";
  char _label[32] = "?";
  char _prefix[32] = "";
  bool _uploading = false;
  bool _done = false;
  bool _failed = false;
  bool _wrongFile = false;  // rejected because the file name did not match
};

#endif  // ARDUINO
