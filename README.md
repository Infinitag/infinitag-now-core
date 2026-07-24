# Infinitag Now – Core

**Das gemeinsame Fundament aller Infinitag-Now-Geräte:** die
Protokoll-Bibliothek `InfinitagNow` für das Zauberstab-Spiel – ein
36-Byte-Paketformat über ESP-NOW, dazu die wiederverwendbaren Bausteine
für Funk und Firmware-Updates. Die verbindliche Spezifikation steht in
[`PROTOCOL.md`](PROTOCOL.md).

![Plattform](https://img.shields.io/badge/Plattform-ESP32%20%2F%20nativ-blue)
![Framework](https://img.shields.io/badge/Framework-Arduino%20%2F%20PlatformIO-orange)
![Protokoll](https://img.shields.io/badge/Protokoll-v0x02-purple)
![Lizenz](https://img.shields.io/badge/Lizenz-PolyForm%20NC%201.0.0-lightgrey)

## Inhalt

| Baustein | Datei | Zweck |
|---|---|---|
| Protokoll | `src/InfinitagNow.*` | Paketformat, CRC-16, Config-Blobs, Encode/Decode – **reines C++ ohne Arduino-Abhängigkeit**, nativ testbar |
| Funk-Service | `src/EspNowService.*` | ESP-NOW-Init, Peer-LRU, RX-Ringpuffer, CRC-Validierung (Arduino-only, `#ifdef`-gekapselt) |
| Update-Service | `src/WebUpdateService.*` | SoftAP + Browser-Upload-Seite + OTA-Flash für alle Gerätetypen, inkl. Geräte-Label und Dateinamen-Check |
| Spezifikation | [`PROTOCOL.md`](PROTOCOL.md) | Nachrichten, Blobs, Flows – die einzige verbindliche Quelle |
| Tests | `test/test_protocol/` | Unit-Tests des Protokolls, laufen nativ auf dem PC |

Geräte identifizieren sich seit Protokoll v0x02 allein über ihre
MAC-Adresse; Discovery, Konfiguration, Treffer-Meldungen, Selbsttest
und der Update-Modus laufen über dieses Paketformat.

## Einbindung (PlatformIO)

```ini
lib_deps =
    ; lokale Arbeitskopie (immer aktueller Stand, kein Cache-Drift):
    symlink:///Volumes/Basteln/Infinitag/repos/infinitag-now-core
    ; alternativ auf einen Versions-Tag pinnen:
    ; https://github.com/Infinitag/infinitag-now-core.git#v2.0.0
```

Solange das Repo privat ist, braucht die Git-Variante Org-Zugriff
(SSH-Key oder Credential Helper).

## Tests

```bash
pio test -e native
```

Die Protokoll-Tests kompilieren ohne Arduino auf dem PC – jede
Protokolländerung kommt mit angepassten Tests im selben Commit.

## Versionierung

Das `version`-Byte im Paket-Header und die Tags dieses Repos bewegen
sich zusammen: aktuell **0x02 ↔ `v2.x`**. Ein Protokollbruch erhöht
beides (neues Major-Tag), die Geräte-Repos ziehen bewusst nach.
Empfänger verwerfen Pakete fremder Versionen am ersten Byte.

## Entwicklung

Änderungen laufen über Pull Requests (Squash-Merge, Typ-Label –
Template liegt in `.github/`). Protokolländerungen gehören hierher:
Code + `PROTOCOL.md` + Tests in einem Commit, nie verteilt über die
Geräte-Repos.

## Lizenz

[PolyForm Noncommercial 1.0.0](LICENSE) – © 2026 Tobias Stewen.
Kommerzielle Nutzung nur mit Genehmigung: info@hallow-tech.de.
Teil von [Infinitag Now](https://github.com/Infinitag/infinitag-now),
einem Zauberstab-Spiel als komplette Neuentwicklung – Ursprung des
Namens: das Lasertag-Projekt [Infinitag](https://github.com/Infinitag) (2017).
