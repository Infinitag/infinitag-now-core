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
  if (_store) {
    _server.on(
        "/store", HTTP_POST, [this]() { handleStoreDone(); },
        [this]() { handleStoreData(); });
  }
  _server.onNotFound([this]() { handleRoot(); });
  _server.begin();
  return true;
}

void WebUpdateService::loop() { _server.handleClient(); }

int WebUpdateService::clientCount() const { return WiFi.softAPgetStationNum(); }

String WebUpdateService::resultPage(const char *title, const String &body) {
  String h =
      "<!DOCTYPE html><html><head><meta charset='utf-8'>"
      "<meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<style>body{margin:0;background:#0c0e0f;font-family:Helvetica,Arial,"
      "sans-serif;color:#e8eaea;font-size:14px}.wrap{max-width:440px;"
      "margin:40px auto;padding:16px}.card{background:#131516;border:1px "
      "solid #23282a;border-radius:8px;padding:24px 28px}.bar{height:5px;"
      "background:linear-gradient(90deg,#79C8B4,#03817D);border-radius:"
      "8px 8px 0 0;margin:-24px -28px 18px}h2{font-size:15px;color:#fff;"
      "margin:0 0 10px}p{color:#8a9092;line-height:1.5}a{color:#4db3a2;"
      "text-decoration:none}</style></head><body><div class='wrap'>"
      "<div class='card'><div class='bar'></div><h2>";
  h += title;
  h += "</h2><p>";
  h += body;
  h += "</p><p><a href='/'>&larr; Zur&uuml;ck</a></p></div></div>"
       "</body></html>";
  return h;
}

void WebUpdateService::handleRoot() {
  if (_customPage) {
    _server.send(200, "text/html", _customPage);
    return;
  }
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
      "neu.</p>";
  if (_store) {
    html +=
        "<hr><h3>Ger&auml;te-Image ablegen</h3>"
        "<p>Firmware f&uuml;r Station/Target auf der Box speichern &ndash; "
        "wird sp&auml;ter per Funk verteilt, die Box flasht sich damit "
        "NICHT selbst.</p>"
        "<form method='POST' action='/store' enctype='multipart/form-data'>"
        "<p><input type='file' name='image' accept='.bin' required></p>"
        "<p><input type='submit' value='Image speichern'></p>"
        "</form>";
  }
  if (_extraHtml) html += _extraHtml;
  html += "</body></html>";
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

void WebUpdateService::handleStoreData() {
  if (!_store) return;
  HTTPUpload &up = _server.upload();

  switch (up.status) {
    case UPLOAD_FILE_START:
      _uploading = true;
      _storeOk = false;
      Serial.printf("[UPD] Image-Upload start: %s\n", up.filename.c_str());
      _storeActive = _store->begin(up.filename.c_str());
      break;

    case UPLOAD_FILE_WRITE:
      if (_storeActive && !_store->write(up.buf, up.currentSize)) {
        Serial.println("[UPD] Image-Schreibfehler");
        _store->end(false);
        _storeActive = false;
      }
      break;

    case UPLOAD_FILE_END:
      if (_storeActive) {
        _storeOk = _store->end(true);
        _storeActive = false;
        Serial.printf("[UPD] Image-Upload fertig: %u Bytes, %s\n",
                      up.totalSize, _storeOk ? "akzeptiert" : "abgelehnt");
      }
      _uploading = false;
      break;

    case UPLOAD_FILE_ABORTED:
      if (_storeActive) {
        _store->end(false);
        _storeActive = false;
      }
      _uploading = false;
      Serial.println("[UPD] Image-Upload abgebrochen");
      break;

    default:
      break;
  }
}

void WebUpdateService::handleStoreDone() {
  const char *info =
      (_store && _store->resultText) ? _store->resultText() : "";
  if (_storeOk) {
    _server.send(200, "text/html", resultPage("Image gespeichert", info));
  } else {
    String body =
        "Kein g&uuml;ltiges Infinitag-Firmware-Image (Marker fehlt) oder "
        "Speicherfehler. ";
    body += info;
    _server.send(400, "text/html", resultPage("Image abgelehnt", body));
  }
}

void WebUpdateService::handleUploadDone() {
  if (_done) {
    _server.send(200, "text/html",
                 resultPage("Update OK",
                            "Das Ger&auml;t startet jetzt automatisch neu."));
  } else if (_wrongFile) {
    String body = "Dieses Ger&auml;t ist <b>";
    body += _label;
    body += "</b> und erwartet <b>";
    body += _prefix;
    body +=
        "-vX.Y.Z.bin</b>. Es wurde nichts geflasht &ndash; bitte die "
        "passende Datei w&auml;hlen.";
    _server.send(400, "text/html",
                 resultPage("Falsche Firmware-Datei", body));
    _wrongFile = false;  // allow a retry without reboot
    _failed = false;
  } else {
    _server.send(
        500, "text/html",
        resultPage("Update fehlgeschlagen",
                   "Alte Firmware bleibt aktiv. Bitte erneut versuchen."));
    _failed = false;  // allow a retry without reboot
  }
}

#endif  // ARDUINO
