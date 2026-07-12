// Arduino/ESP32 only – excluded from native test builds.
#ifdef ARDUINO
#include "WebUpdateService.h"

#include <WiFi.h>
#include <Update.h>
#include <esp_now.h>

bool WebUpdateService::begin(const char *apName, const char *fwVersion,
                             const char *deviceLabel, const char *filePrefix) {
  strncpy(_version, fwVersion, sizeof(_version) - 1);
  strncpy(_label, deviceLabel, sizeof(_label) - 1);
  strncpy(_prefix, filePrefix, sizeof(_prefix) - 1);

  // Leave the ESP-NOW world: deinit is safe even if it was never started.
  esp_now_deinit();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP);
  if (!WiFi.softAP(apName)) return false;  // open AP, default 192.168.4.1

  _server.on("/", HTTP_GET, [this]() { handleRoot(); });
  _server.on(
      "/update", HTTP_POST, [this]() { handleUploadDone(); },
      [this]() { handleUploadData(); });
  _server.onNotFound([this]() { handleRoot(); });
  _server.begin();
  return true;
}

void WebUpdateService::loop() { _server.handleClient(); }

int WebUpdateService::clientCount() const { return WiFi.softAPgetStationNum(); }

void WebUpdateService::handleRoot() {
  String html =
      "<!DOCTYPE html><html><head><meta charset='utf-8'>"
      "<meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<title>";
  html += _label;
  html +=
      " – Update</title></head>"
      "<body style='font-family:sans-serif;max-width:26em;margin:2em auto'>"
      "<h2>";
  html += _label;
  html +=
      "</h2>"
      "<p>Infinitag Firmware-Update &ndash; laufende Version: <b>";
  html += _version;
  html +=
      "</b></p>"
      "<form method='POST' action='/update' enctype='multipart/form-data'>"
      "<p><input type='file' name='firmware' accept='.bin' required></p>"
      "<p><input type='submit' value='Update starten'></p>"
      "</form>"
      "<p style='color:#666'>Erwartete Datei: <b>";
  html += _prefix;
  html +=
      "-vX.Y.Z.bin</b> &ndash; andere Dateinamen werden abgelehnt.<br>"
      "Nach erfolgreichem Upload startet das Ger&auml;t automatisch "
      "neu.</p></body></html>";
  _server.send(200, "text/html", html);
}

void WebUpdateService::handleUploadData() {
  HTTPUpload &up = _server.upload();

  switch (up.status) {
    case UPLOAD_FILE_START:
      _uploading = true;
      _failed = false;
      _wrongFile = false;
      Serial.printf("[UPD] Upload start: %s\n", up.filename.c_str());
      // Device-type guard: our release assets are named
      // "<prefix>-vX.Y.Z.bin"; anything else is likely the wrong device.
      if (_prefix[0] != '\0' &&
          strncmp(up.filename.c_str(), _prefix, strlen(_prefix)) != 0) {
        Serial.printf("[UPD] Abgelehnt: Dateiname passt nicht zu %s-*.bin\n",
                      _prefix);
        _failed = true;
        _wrongFile = true;
        break;
      }
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
        Update.printError(Serial);
        _failed = true;
      }
      break;

    case UPLOAD_FILE_WRITE:
      if (!_failed && Update.write(up.buf, up.currentSize) != up.currentSize) {
        Update.printError(Serial);
        _failed = true;
      }
      break;

    case UPLOAD_FILE_END:
      if (_wrongFile) {
        _uploading = false;
        break;  // never reached Update.begin()
      }
      if (!_failed && Update.end(true)) {  // true = set boot partition
        Serial.printf("[UPD] Fertig: %u Bytes\n", up.totalSize);
        _done = true;
      } else {
        Update.printError(Serial);
        _failed = true;
      }
      _uploading = false;
      break;

    case UPLOAD_FILE_ABORTED:
      Update.abort();
      _uploading = false;
      _failed = true;
      Serial.println("[UPD] Upload abgebrochen");
      break;

    default:
      break;
  }
}

void WebUpdateService::handleUploadDone() {
  if (_done) {
    _server.send(200, "text/html",
                 "<h2>Update OK &ndash; Ger&auml;t startet neu.</h2>");
  } else if (_wrongFile) {
    String html =
        "<h2>Falsche Firmware-Datei!</h2><p>Dieses Ger&auml;t ist <b>";
    html += _label;
    html += "</b> und erwartet <b>";
    html += _prefix;
    html +=
        "-vX.Y.Z.bin</b>. Es wurde nichts geflasht &ndash; zur&uuml;ck "
        "und die passende Datei w&auml;hlen.</p>";
    _server.send(400, "text/html", html);
    _wrongFile = false;  // allow a retry without reboot
    _failed = false;
  } else {
    _server.send(500, "text/html",
                 "<h2>Update fehlgeschlagen &ndash; alte Firmware bleibt "
                 "aktiv. Zur&uuml;ck und erneut versuchen.</h2>");
    _failed = false;  // allow a retry without reboot
  }
}

#endif  // ARDUINO
