# infinitag-now-core

Gemeinsame **ESP-NOW-Protokoll-Bibliothek** (`InfinitagNow`) für alle Geräte des
Infinitag-Now-Systems (Config-Box, Targets, Stationen) – plus die verbindliche
Protokoll-Spezifikation in [`PROTOCOL.md`](PROTOCOL.md).

Die Lib ist bewusst **frei von Arduino-Abhängigkeiten** (reines C++), damit sie
nativ auf dem PC kompiliert und getestet werden kann.

## Einbindung in eine Geräte-Firmware (PlatformIO)

```ini
lib_deps =
    https://github.com/Infinitag/infinitag-now-core.git
```

Solange das Repo privat ist, braucht das lokale Git Zugriff auf die Org
(SSH-Key oder Credential Helper). Alternativ SSH-Form:
`git@github.com:Infinitag/infinitag-now-core.git`.

Sobald Versions-Tags existieren, bitte pinnen: `…core.git#v0.1.0`.

## Tests ausführen

```bash
pio test -e native
```

## Versionierungs-Regel

Das `version`-Byte im Paket-Header und die Tags dieses Repos bewegen sich
zusammen: **Protokollbruch = neues Major-Tag** (und neues `version`-Byte).
Geräte-Repos pinnen ihre `lib_deps` auf ein Tag und ziehen bewusst nach.

## Lizenz

CC BY-NC-SA 4.0 – wie das übrige Infinitag-Projekt.
