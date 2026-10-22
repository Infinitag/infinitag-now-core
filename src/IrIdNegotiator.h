// IrIdNegotiator – zero-config allocation of the station's IR id (1..15).
//
// Since protocol v0x03 a shot carries the 4-bit `shooter_id` of the firing
// station and targets broadcast every hit with that id; a station plays the
// hit sound when the id matches its own `ir_id`. Nobody wants to hand out
// those ids by hand, so stations negotiate them among themselves over
// ESP-NOW broadcast – the pattern of IPv4 link-local address conflict
// detection (listen → probe → announce), see PROTOCOL.md "IR-ID-Aushandlung".
//
//   LISTEN  ir_id 0, T_LISTEN after boot: only collect evidence of ids held
//           by other stations (IRID_BEACON, IRID_PROBE, HIT_REPORT).
//   PROBE   ir_id 0: pick a free candidate, broadcast IRID_PROBE N_PROBE
//           times every T_PROBE. Any evidence for the candidate from another
//           MAC aborts the probe (next candidate). Two stations probing the
//           same id at once: the numerically smaller MAC proceeds.
//   HELD    ir_id 1..15: announce with IRID_BEACON (twice at once, then
//           every T_BEACON), answer probes for the own id with a beacon.
//           Beacon for the own id from a smaller MAC -> yield: straight back
//           to PROBE with the next candidate (no second LISTEN phase, the
//           wand must not sit on id 0 for seconds mid-game).
//
// Pure C++: time is passed in (ms ticks), sending goes through a callback –
// the whole state machine runs natively in the unit tests, including a
// multi-station bus simulation. The caller persists the held id (NVS) and
// hands it back as `preferredId` on the next boot so ids stay stable.

#pragma once

#include <stdint.h>
#include <stddef.h>

namespace inow {

// Timing (ms) – rationale in PROTOCOL.md.
constexpr uint32_t IRID_T_LISTEN_MS = 3000;  // > one beacon period + playback
constexpr uint32_t IRID_T_PROBE_MS = 150;    // gap between probes
constexpr uint8_t IRID_N_PROBE = 3;          // probes before HELD
constexpr uint32_t IRID_T_BEACON_MS = 2000;  // beacon period while HELD
constexpr uint32_t IRID_T_EXPIRE_MS = 8000;  // evidence lifetime
constexpr uint32_t IRID_T_RETRY_MS = 30000;  // all 15 ids taken: retry

class IrIdNegotiator {
 public:
  enum State : uint8_t { LISTEN, PROBE, HELD };
  enum TxKind : uint8_t { TX_PROBE, TX_BEACON };

  // dstMac == nullptr -> broadcast, otherwise unicast to that station.
  using SendFn = void (*)(TxKind kind, uint8_t id, const uint8_t *dstMac,
                          void *ctx);

  // preferredId: last held id from NVS (0 or out of range = none).
  void begin(const uint8_t ownMac[6], uint8_t preferredId, uint32_t nowMs,
             SendFn send, void *ctx);

  // Call every loop() pass (and from playback housekeeping).
  void tick(uint32_t nowMs);

  // Radio inputs (packets already CRC/version validated by the caller).
  void onProbe(const uint8_t srcMac[6], uint8_t id, uint32_t nowMs);
  void onBeacon(const uint8_t srcMac[6], uint8_t id, uint32_t nowMs);
  void onHitReport(uint8_t shooterId, uint32_t nowMs);

  State state() const { return _state; }
  uint8_t irId() const { return _id; }  // 0 unless HELD
  uint8_t candidate() const { return _candidate; }  // PROBE only, else 0

  // True once after irId() changed (0 -> n: persist to NVS; n -> 0: yield).
  bool consumeIdChanged();

  // Evidence of another station holding `id` within IRID_T_EXPIRE_MS.
  bool isOccupied(uint8_t id, uint32_t nowMs) const;

 private:
  void note(uint8_t id, uint32_t nowMs);          // record evidence
  bool otherWins(const uint8_t otherMac[6]) const;  // tie-break
  uint8_t pickCandidate(uint32_t nowMs) const;
  void startProbe(uint32_t nowMs);  // next candidate or LISTEN/retry
  void sendProbe(uint32_t nowMs);
  void sendBeacon(const uint8_t *dstMac, uint32_t nowMs);
  void becomeHeld(uint32_t nowMs);
  void setId(uint8_t id);

  SendFn _send = nullptr;
  void *_ctx = nullptr;
  uint8_t _mac[6] = {0};
  uint8_t _preferred = 0;

  State _state = LISTEN;
  uint8_t _id = 0;
  uint8_t _candidate = 0;
  bool _idChanged = false;

  uint32_t _listenUntil = 0;  // LISTEN: when to start probing
  uint32_t _lastProbeMs = 0;  // PROBE
  uint8_t _probesSent = 0;    // PROBE
  uint32_t _lastBeaconMs = 0;  // HELD

  // Evidence table, index = id (1..15). Lazy expiry via timestamps.
  uint32_t _seenMs[16] = {0};
  bool _seen[16] = {false};
};

}  // namespace inow
