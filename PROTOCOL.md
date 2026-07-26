# Infinitag Now – ESP-NOW-Protokoll (Version 0x03)

Verbindliche Spezifikation. Ausführliche Herleitung und Flows:
Wissensbasis Doc 12 § 3 (Repo `infinitag-now`, `docs/`).
V0x01 festgeschrieben 2026-05-18. V0x02 seit 2026-07-12: Geräte werden
ausschließlich über ihre eFuse-MAC identifiziert – `station_id`/`target_id`
und der komplette Setup-Flow (`SETUP_BEGIN`/`SETUP_TAKE`) sind entfallen;
neu waren `UPDATE_BEGIN`/`UPDATE_ACK` (SoftAP-Update) und `PUSH_*`
(Funk-Update). **V0x03 seit 2026-07-24**: Der IR-Schuss ist ein
**Datentelegramm** mit Schützen-ID (`ir_id` der Station) und Schadenswert
statt eines rohen Bursts – Treffer werden **dynamisch** zur schießenden
Station geroutet (jeder Zauberstab schießt auf jedes Target); die statische
Target→Station-Zuordnung (`station_mac` im Target-Blob) ist entfallen.
Spezifikation des IR-Telegramms: siehe unten.

## Grundlagen

- Alle Geräte laufen als `WIFI_STA` (kein Connect) fest auf **Kanal 1**.
- `WiFi.disconnect()` nach `WiFi.mode()`, `WiFi.setSleep(WIFI_PS_NONE)`.
- **Broadcast** (FF:FF:FF:FF:FF:FF): `DISCOVER_REQ`, `HIT_REPORT`.
- **Unicast**: alles andere. Peers per LRU-Cache (max. ~18 dynamisch).
- **Identität = 48-Bit-MAC (eFuse).** Es gibt keine anwender-vergebenen
  IDs mehr; Menschen unterscheiden Geräte über das MAC-Suffix (letzte
  3 Byte, z. B. `220AAC`) plus Identify-Blinken.
- Keine ESP-NOW-Verschlüsselung (bewusst, siehe Doc 12 § 3.9).

## Paketformat (fix 36 Byte, little-endian, packed)

| Offset | Feld | Typ | Bedeutung |
|---|---|---|---|
| 0 | `version` | u8 | `0x03` |
| 1 | `msg_type` | u8 | siehe unten |
| 2 | `device_type` | u8 | 1 = Station, 2 = Target, 3 = Config-Box, 0xFF = any |
| 3 | `flags` | u8 | bit0 = ACK_REQUIRED |
| 4 | `token` | u8 | Zufallswert, Echo-Schutz (Discovery) |
| 5–7 | `reserved` | u8[3] | 0 |
| 8–33 | `payload[26]` | u8[] | typabhängig |
| 34–35 | `crc16` | u16 | CRC-16/CCITT-FALSE über Bytes 0–33 |

Die Absender-Identität steckt im ESP-NOW-Layer-2-Header (Quell-MAC) und
wird dem `recv_cb` mitgegeben – sie fährt nicht im Payload mit.

## Nachrichtentypen

| Code | Name | Sender → Empfänger | Adressierung | Payload |
|---|---|---|---|---|
| 0x01 | `DISCOVER_REQ` | Config-Box → alle | Broadcast | `[0]` = device_type-Filter |
| 0x02 | `DISCOVER_REPLY` | Gerät → Config-Box | Unicast | siehe unten |
| 0x03 | `IDENTIFY` | Config-Box → Gerät | Unicast | `[0]` = Dauer in 100 ms (Default 7) |
| 0x10 | `HIT_REPORT` | Target → alle | Broadcast | `[0]` = shooter_id (`ir_id` aus dem IR-Telegramm), `[1]` = sound_id des Targets, `[2]` = damage |
| 0x30 | `CFG_WRITE` | Config-Box → Gerät | Unicast | Config-Blob (siehe unten) |
| 0x31 | `CFG_ACK` | Gerät → Config-Box | Unicast | `[0]`: 0 = OK, 1 = NACK Persist., 2 = NACK Valid. |
| 0x32 | `CFG_TEST_SOUND` | Config-Box → Station | Unicast | `[0]` = sound_id, nur abspielen, nicht persistieren |
| 0xC0 | `IR_SELECT_ECHO` | Target → Config-Box | Broadcast | reserviert/latent (IR-Pointer) |
| 0xF0 | `DEBUG_CMD` | Config-Box → Gerät | Unicast | Selbsttest: `[0]` = Test-Nr., `[1]` = Parameter (siehe unten) |
| 0xF1 | `DEBUG_RESULT` | Gerät → Config-Box | Unicast | `[0]` = Test-Nr., `[1]` = Ergebnis (0 = OK, 1 = FAIL, 2 = TIMEOUT, 3 = UNSUPPORTED) |
| 0xF2 | `UPDATE_BEGIN` | Config-Box → Gerät | Unicast | `[0]` = Timeout in Minuten (0 = Default 5). Gerät wechselt in den SoftAP-Update-Modus. Neu 2026-07-12 |
| 0xF3 | `UPDATE_ACK` | Gerät → Config-Box | Unicast | `[0]` = 0 (Gerät wechselt jetzt in den Update-Modus) |
| 0xF4 | `PUSH_BEGIN` | Config-Box → Gerät | Unicast | ESP-NOW-Funk-Update startet: `[0..3]` Größe (u32), `[4..7]` CRC32, `[8..10]` Version, `[11]` Frame-Nutzbytes (242), `[12]` Fenstergröße (16). Neu 2026-07-12 |
| 0xF5 | `PUSH_ACK` | Gerät → Config-Box | Unicast | `[0..1]` Fenster (u16), `[2..5]` Bitmap fehlender Frames (u32), `[6]` Status (0 = Fenster-ACK, 1 = fertig/OK, 2 = CRC-Fehler, 3 = Flash-Fehler, 4 = busy) |
| 0xF6 | `PUSH_END` | Config-Box → Gerät | Unicast | alle Fenster gesendet → Gerät prüft CRC32, `Update.end`, finaler `PUSH_ACK`, Reboot |

Reserviert: 0x40–0x4F (Reads), 0x80–0xBF (Telemetrie), 0xF7–0xFF (Debug/OTA).
Entfallen seit v0x02: 0x20 `SETUP_BEGIN`, 0x21 `SETUP_TAKE`.

### Versions-toleranter Rettungsanker (seit 2026-07-24)

Vier Nachrichten passieren die Versionsprüfung in `validate()` mit
**jedem** `version`-Byte (`isRescueMsg()`): `DISCOVER_REQ`,
`DISCOVER_REPLY`, `UPDATE_BEGIN`, `UPDATE_ACK`. Damit kann eine neuere
Config-Box ältere Geräte auch über einen Protokollbruch hinweg **finden**
und in den SoftAP-Update-Modus **schicken** – ohne diesen Anker würde
ein Versionssprung verbaute Geräte vom Funk-Update aussperren (USB-Zwang;
genau das ist bei der v0x02→v0x03-Migration einmal passiert).

Dafür sind folgende Punkte **über alle künftigen Protokollversionen
eingefroren** und dürfen nie geändert werden:

- Paketlänge 36 Byte, Header-Layout Bytes 0–7, CRC-16-Verfahren und
  -Position (Bytes 34–35) – die CRC-Prüfung gilt unverändert auch für
  Rettungsanker-Pakete.
- `DISCOVER_REQ`: `[0]` = device_type-Filter.
- `DISCOVER_REPLY`: `[0..2]` = fw_version, `[3]` = rssi_self,
  `[4..5]` = uptime_min. (Der Config-Blob ab `[6]` ist **nicht**
  eingefroren – bei fremder Version darf er nicht interpretiert werden!)
- `UPDATE_BEGIN`: `[0]` = Timeout in Minuten. `UPDATE_ACK`: `[0]` = 0.

Alle anderen Nachrichten (Config, Treffer, Debug, `PUSH_*`) bleiben
versionsgesperrt – der Rettungsweg für fremde Versionen ist immer der
**SoftAP-Upload**, nicht der Funk-Push. Die Config-Box markiert Geräte
fremder Protokollversion in der Liste (`!`) und bietet für sie nur
„Update (OTA)" an. Gilt ab Firmwares mit Core ≥ v3.1.0 – Geräte, die
noch ohne den Anker gebaut wurden, bleiben rückwirkend unerreichbar.

### `HIT_REPORT`-Routing (dynamisch seit v0x03)

Die schießende Station codiert ihre `ir_id` als `shooter_id` ins
IR-Telegramm. Das Target decodiert den Schuss, prüft die CRC und
broadcastet den Treffer mit `shooter_id`, dem eigenen `sound_id` und dem
empfangenen `damage`. **Stationen spielen, wenn `shooter_id` der eigenen
`ir_id` entspricht** – so wandert der Treffer-Sound automatisch zu der
Station, deren Zauberstab geschossen hat. Es gibt keine statische
Target→Station-Zuordnung mehr; ein Target braucht ab Werk keinerlei
Funk-Konfiguration. Broadcast statt Unicast ist Absicht: so sieht der
**Live-Monitor der Config-Box** alle Treffer mit.

`ir_id` 0 ist ein **gültiger Wert** („Werksgruppe"): Ab Werk haben alle
Stationen `ir_id` 0 und spielen gegenseitig ihre Treffer – ein
Einzelplatz-Aufbau funktioniert ohne Konfiguration. Bei mehreren
Stationen vergibt die Config-Box eindeutige IDs (1–15).

## IR-Telegramm (Schuss, seit v0x03)

Physikalische Schicht: 38-kHz-Träger (TX ~33 % Duty), Empfang TSOP4138.
Puls-Distanz-Codierung (Information in der Space-Länge), alle Zeiten µs:

| Element | Mark (Träger an) | Space (Träger aus) |
|---|---|---|
| Header | 2400 | 600 |
| Bit „0" | 600 | 600 |
| Bit „1" | 600 | 1200 |
| Stop | 600 | – |

16 Datenbits, MSB zuerst:

| Bits | Feld | Bedeutung |
|---|---|---|
| 15–12 | `irt_version` | `0x1` |
| 11–8 | `shooter_id` | `ir_id` der schießenden Station (0–15) |
| 7–4 | `damage` | Schadenswert (Sender clampt auf ≥ 1; Reserve für Hitpoints) |
| 3–0 | `crc4` | CRC-4 (Poly x⁴+x+1, Init 0) über Bits 15–4 |

Telegrammdauer 22,8–32,4 ms; mit Decode + `HIT_REPORT` + Sound-Start
bleibt das Latenzbudget IR → Sound < 50 ms erfüllt. Empfänger-Toleranzen
und Decoder-Zustandsmaschine: `src/IrTelegram.h` (nativ getestet).
Fernbedienungen (NEC: 9-ms-Header, ~560-µs-Pulse) und der alte
v0x02-Burst (5 ms) erzeugen keine gültigen Telegramme – ohne gültige CRC
gibt es keinen Treffer.

### Selbsttest-Katalog (`DEBUG_CMD`, Station)

| Test-Nr. | Test | Parameter `[1]` | Ergebnis |
|---|---|---|---|
| 1 | Sound abspielen | Sound-Index (0-basiert) | OK nach Abspielende |
| 2 | LED-Testmuster | – | OK (Sichtprüfung, R→G→B→W) |
| 3 | Laser | Sekunden an (max. 10, Auto-Aus) | OK (Sichtprüfung) |
| 4 | IR-Burst | Burst-Dauer in ms | OK = TSOP hat den eigenen Burst empfangen, FAIL = nicht |
| 5 | Trigger-Test | Timeout in Sekunden | OK sobald Trigger gedrückt, sonst TIMEOUT |
| 6 | Kalibriermodus | Minuten an (0 = aus, Clamp 60) | OK bei Zustandswechsel |

Unbekannte Test-Nummern beantwortet das Gerät mit UNSUPPORTED.

### Selbsttest-Katalog (`DEBUG_CMD`, Target)

| Test-Nr. | Test | Parameter `[1]` | Ergebnis |
|---|---|---|---|
| 2 | LED-Ring-Testmuster | – | OK (Sichtprüfung R/G/B/W) |
| 5 | IR-Empfangs-Test | Timeout in Sekunden (0 = 10) | OK beim nächsten gültigen Telegramm (grüner Blitz, keine Hit-Aktion – zum Einschießen), sonst TIMEOUT |
| 7 | Treffer-Simulation | – | OK = angenommen; das Target fährt die komplette Treffer-Sequenz (Grün-Blitz, rote Welle, Prop-Muster, Cooldown) und broadcastet einen `HIT_REPORT` mit `shooter_id` 0 – zum Einstellen von Hit-Zeit/Cooldown ohne Station |

**Kalibriermodus (Test 6):** Laser und IR-LED-Treiber leuchten dauerhaft
(kein 38-kHz-Burst-Muster) – zum optischen Ausrichten wird die IR-LED
vorübergehend gegen eine weiße LED getauscht, Optik zentriert, danach
zurückgetauscht. Das Gerät beendet den Modus selbst nach Ablauf der
Minuten (Auto-Aus); `param = 0` beendet sofort. Schuss-Trigger ist im
Kalibriermodus gesperrt.

### Update-Modus (`UPDATE_BEGIN`)

Das Gerät quittiert mit `UPDATE_ACK`, beendet ESP-NOW und öffnet einen
offenen SoftAP `infinitag-<typ>-<MACSUFFIX>` (`sta`/`tgt`/`cfg`, z. B.
`infinitag-sta-220AAC`) mit Upload-Seite auf `http://192.168.4.1/`
(geteiltes Modul `WebUpdateService` in dieser Lib). Der Boot-Slot wird erst
nach vollständigem, prüfsummen-validiertem Empfang umgeschaltet; ohne
Upload innerhalb des Timeouts rebootet das Gerät in die alte Firmware.
Die Config-Box selbst startet den gleichen Modus lokal über ihr
Tools-Menü. Firmware-Versionen prüft man nach dem Update per Discovery
(`fw_version` im `DISCOVER_REPLY`).

### ESP-NOW-Funk-Update (`PUSH_*`, Doc 21 Etappe 3)

Die **Datenphase** läuft nicht über das 36-Byte-Paketformat, sondern über
**rohe ESP-NOW-Frames** (bis 250 Byte): `'I' 'N' 'W' 'D'` + Frame-Index
(u32, LE) + bis zu 242 Nutzbytes. Der Empfänger puffert ein Fenster
(16 Frames) im RAM, flasht es sequenziell in den inaktiven OTA-Slot und
quittiert per `PUSH_ACK` mit Bitmap der fehlenden Frames (selektive
Nachlieferung). Der Boot-Slot wechselt erst nach vollständigem Empfang
und CRC32-Prüfung – ein abgebrochener Push bootet immer die alte
Firmware. Geteilte Implementierung: `EspNowPush.h` (Sender = Box,
Empfänger = Station/Target); Frames laufen am normalen Paket-Validator
vorbei über den Raw-Handler des `EspNowService`.

## `DISCOVER_REPLY`-Payload

| Offset | Feld |
|---|---|
| 0–2 | fw_version major/minor/patch |
| 3 | `rssi_self` (int8, eigener RX-RSSI des Requests) |
| 4–5 | `uptime_min` (u16) |
| 6 | `config_blob_len` (max. 19) |
| 7–25 | `config_blob` (Interpretation je `device_type` im Header) |

## Station-Config-Blob (16 Byte)

| Offset | Feld | Typ | Default |
|---|---|---|---|
| 0 | `volume_pct` | u8 | 80 |
| 1 | `led_ready` | u8 | 0x02 (Grün) – Stab-Farbe „schussbereit", LED-Maske |
| 2 | `led_busy` | u8 | 0x01 (Rot) – Stab-Farbe „beschäftigt" (z. B. Audio spielt), LED-Maske |
| 3 | `laser_mode` | u8 | 3 – Schuss-Laser: 0 = nicht gesetzt (→ Default), 1 = aus, 2 = dauerhaft an, 3 = Nachleuchten |
| 4 | `laser_glow` | u8 | 1 – Nachleuchtdauer in 500-ms-Schritten (nur `laser_mode` = 3; Clamp 20 = 10 s) |
| 5 | `ir_id` | u8 | 0 – Schützen-ID im IR-Telegramm (0–15; **0 ist gültig** = Werksgruppe, kein Unset-Wert) |
| 6 | `led_bright_pct` | u8 | 100 – Stab-LED-Helligkeit in % (1–100; 0 = nicht gesetzt → Default) |
| 7–15 | reserviert (0) | | |

**LED-Maske:** Kanal-Bitmaske der SK6812-RGBW-Dies – bit0 = R, bit1 = G,
bit2 = B, bit3 = W. Gültig sind alle 15 nicht-leeren Kombinationen (1–15);
0 bedeutet „Feld nicht gesetzt" → Empfänger nutzt den Default.

## Target-Config-Blob (16 Byte)

Seit v0x03 ohne `station_mac` – das Routing läuft über die `shooter_id`
aus dem IR-Telegramm.

| Offset | Feld | Typ | Default |
|---|---|---|---|
| 0 | `sound_id` | u8 | 1 |
| 1–2 | `hit_time_ms` | u16 | 10000 |
| 3–4 | `cooldown_ms` | u16 | 2000 |
| 5 | `sw_animation` | u8 | 0 |
| 6 | `sw_channels` | u8 | 0b111 (bit0 = SW1, bit1 = SW_5V, bit2 = SW_3V3) |
| 7 | `led_bright_pct` | u8 | 100 – Ring-Helligkeit in % (1–100; 0 = nicht gesetzt → Default) |
| 8–15 | reserviert (0) | | (Reserve u. a. für Hitpoints) |

## Kern-Flows (Kurzform)

- **Spielbetrieb:** Station schießt IR-Telegramm (eigene `ir_id` als
  `shooter_id`) → Target decodiert + CRC-Check → `HIT_REPORT` (Broadcast:
  shooter_id, sound_id, damage) → Station mit passender `ir_id` spielt
  `sound_id`. Ziel < 50 ms.
- **Discovery/Config:** `DISCOVER_REQ` → Replies (Unicast) → Liste →
  Cursor sendet alle 500 ms `IDENTIFY` (Gerät blinkt weiß, selbstlöschend
  nach 700 ms) → `CFG_WRITE` → Gerät persistiert NVS → `CFG_ACK`.
  Eine neue Station braucht kein Setup mehr – auspacken, einschalten,
  sie erscheint in der Liste.
- **Firmware-Update:** `UPDATE_BEGIN` → `UPDATE_ACK` → Gerät im
  SoftAP-Update-Modus (siehe oben) → Browser-Upload → Reboot →
  Versions-Check per Discovery.

## Versionierung

`version`-Byte 0x03 ↔ Tags `v3.x` dieses Repos.
Inkompatible Änderung ⇒ `version`-Byte erhöhen + Major-Tag.
Geräte-Repos pinnen `lib_deps` auf den Tag.

**Migration v0x02 → v0x03 (Flag-Day):** Empfänger verwerfen fremde
Versionen am ersten Byte, und der v0x02-Burst triggert den
Telegramm-Decoder nicht – alle Geräte mussten gemeinsam aktualisiert
werden (Geräte zuerst, Box zuletzt). **Künftige Versionssprünge
brauchen dank des Rettungsankers (oben) keine Reihenfolge mehr:** Die
Box findet Geräte jeder Version und schickt sie per `UPDATE_BEGIN` in
den SoftAP-Update-Modus.
