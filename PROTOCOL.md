# Infinitag Now – ESP-NOW-Protokoll (Version 0x01)

Verbindliche Spezifikation. Ausführliche Herleitung und Flows:
Wissensbasis Doc 12 § 3 (Repo `infinitag-now`, `docs/`).
Festgeschrieben 2026-05-18, `CFG_TEST_SOUND` ergänzt 2026-07-08.

## Grundlagen

- Alle Geräte laufen als `WIFI_STA` (kein Connect) fest auf **Kanal 1**.
- `WiFi.disconnect()` nach `WiFi.mode()`, `WiFi.setSleep(WIFI_PS_NONE)`.
- **Broadcast** (FF:FF:FF:FF:FF:FF): `DISCOVER_REQ`, `SETUP_BEGIN`,
  `SETUP_TAKE`, `HIT_REPORT`.
- **Unicast**: `DISCOVER_REPLY`, `IDENTIFY`, `CFG_WRITE`, `CFG_ACK`,
  `CFG_TEST_SOUND`. Peers per LRU-Cache (max. ~18 dynamisch) registrieren.
- Identität = 48-Bit-MAC (eFuse). Anwenderlesbare IDs (`station_id`,
  `target_id`, 1–99, 0 = ungesetzt) liegen im NVS des Geräts.
- Keine ESP-NOW-Verschlüsselung (bewusst, siehe Doc 12 § 3.9).

## Paketformat (fix 36 Byte, little-endian, packed)

| Offset | Feld | Typ | Bedeutung |
|---|---|---|---|
| 0 | `version` | u8 | `0x01` |
| 1 | `msg_type` | u8 | siehe unten |
| 2 | `device_type` | u8 | 1 = Station, 2 = Target, 3 = Config-Box, 0xFF = any |
| 3 | `station_id` | u8 | 1–99, 0 = ungesetzt/irrelevant |
| 4 | `target_id` | u8 | 1–99, 0 = ungesetzt/irrelevant |
| 5 | `flags` | u8 | bit0 = ACK_REQUIRED, bit1 = SETUP_MODE_ONLY |
| 6 | `token` | u8 | Zufallswert, Echo-Schutz (Discovery) |
| 7 | `reserved` | u8 | 0 |
| 8–33 | `payload[26]` | u8[] | typabhängig |
| 34–35 | `crc16` | u16 | CRC-16/CCITT-FALSE über Bytes 0–33 |

## Nachrichtentypen

| Code | Name | Sender → Empfänger | Adressierung | Payload |
|---|---|---|---|---|
| 0x01 | `DISCOVER_REQ` | Config-Box → alle | Broadcast | `[0]` = device_type-Filter |
| 0x02 | `DISCOVER_REPLY` | Gerät → Config-Box | Unicast | siehe unten |
| 0x03 | `IDENTIFY` | Config-Box → Gerät | Unicast | `[0]` = Dauer in 100 ms (Default 7) |
| 0x10 | `HIT_REPORT` | Target → Stationen | Broadcast | `[0]` = sound_id; Filter über `station_id` im Header |
| 0x20 | `SETUP_BEGIN` | Config-Box → Stationen | Broadcast | `[0]` = Timeout s (Default 60); Header-`station_id` = zu vergebende ID, 0 = aktuelle behalten (präzisiert 2026-07-11) |
| 0x21 | `SETUP_TAKE` | Station → alle | Broadcast | `[0]` = neue station_id |
| 0x30 | `CFG_WRITE` | Config-Box → Gerät | Unicast | Config-Blob (siehe unten) |
| 0x31 | `CFG_ACK` | Gerät → Config-Box | Unicast | `[0]`: 0 = OK, 1 = NACK Persist., 2 = NACK Valid. |
| 0x32 | `CFG_TEST_SOUND` | Config-Box → Station | Unicast | `[0]` = sound_id, nur abspielen, nicht persistieren |
| 0xC0 | `IR_SELECT_ECHO` | Target → Config-Box | Broadcast | reserviert/latent (IR-Pointer) |

Reserviert: 0x40–0x4F (Reads), 0x80–0xBF (Telemetrie), 0xF0–0xFF (Debug/OTA).

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
| 0 | `station_id` | u8 | 1 |
| 1 | `volume_pct` | u8 | 80 |
| 2 | `default_setup_sound` | u8 | 13 |
| 3–15 | reserviert (0) | | |

## Target-Config-Blob (16 Byte)

| Offset | Feld | Typ | Default |
|---|---|---|---|
| 0 | `target_id` | u8 | 1 |
| 1 | `station_id` | u8 | 1 |
| 2 | `sound_id` | u8 | 1 |
| 3–4 | `hit_time_ms` | u16 | 10000 |
| 5–6 | `cooldown_ms` | u16 | 2000 |
| 7 | `sw_animation` | u8 | 0 |
| 8 | `sw_channels` | u8 | 0b111 (bit0 = SW1, bit1 = SW_5V, bit2 = SW_3V3) |
| 9–15 | reserviert (0) | | |

## Kern-Flows (Kurzform)

- **Spielbetrieb:** Target empfängt IR-Treffer → `HIT_REPORT` (Broadcast) →
  Stationen mit passender `station_id` spielen `sound_id`. Ziel < 50 ms.
- **Discovery/Config:** `DISCOVER_REQ` → Replies (Unicast) → Liste →
  Cursor sendet alle 500 ms `IDENTIFY` (Gerät leuchtet 700 ms selbstlöschend) →
  `CFG_WRITE` → Gerät persistiert NVS → `CFG_ACK`.
- **Stations-Setup:** `SETUP_BEGIN` (Rebroadcast alle 5 s) → Stationen lila,
  Trigger = Bestätigen → gewählte Station broadcastet `SETUP_TAKE`,
  andere zurück zu IDLE.

## Versionierung

`version`-Byte 0x01 ↔ Tags `v0.x`/`v1.x` dieses Repos.
Inkompatible Änderung ⇒ `version`-Byte erhöhen + Major-Tag.
