#include "IrIdNegotiator.h"

#include <string.h>

namespace inow {

// Wrap-safe "at least `ms` passed since `since`".
static inline bool elapsed(uint32_t now, uint32_t since, uint32_t ms) {
  return (uint32_t)(now - since) >= ms;
}

static inline bool validId(uint8_t id) { return id >= 1 && id <= 15; }

void IrIdNegotiator::begin(const uint8_t ownMac[6], uint8_t preferredId,
                           uint32_t nowMs, SendFn send, void *ctx) {
  memcpy(_mac, ownMac, 6);
  _preferred = validId(preferredId) ? preferredId : 0;
  _send = send;
  _ctx = ctx;
  _state = LISTEN;
  _id = 0;
  _candidate = 0;
  _idChanged = false;
  _listenUntil = nowMs + IRID_T_LISTEN_MS;
  memset(_seen, 0, sizeof(_seen));
}

void IrIdNegotiator::tick(uint32_t nowMs) {
  switch (_state) {
    case LISTEN:
      if ((int32_t)(nowMs - _listenUntil) >= 0) startProbe(nowMs);
      break;

    case PROBE:
      if (elapsed(nowMs, _lastProbeMs, IRID_T_PROBE_MS)) {
        if (_probesSent < IRID_N_PROBE) {
          sendProbe(nowMs);
        } else {
          becomeHeld(nowMs);  // nobody objected
        }
      }
      break;

    case HELD:
      if (elapsed(nowMs, _lastBeaconMs, IRID_T_BEACON_MS)) {
        sendBeacon(nullptr, nowMs);
      }
      break;
  }
}

void IrIdNegotiator::onProbe(const uint8_t srcMac[6], uint8_t id,
                             uint32_t nowMs) {
  if (!validId(id) || memcmp(srcMac, _mac, 6) == 0) return;
  // A probe is evidence as well: the prober will most likely end up there.
  if (id != _id) note(id, nowMs);

  if (_state == PROBE && id == _candidate) {
    // Simultaneous boot, same candidate: the smaller MAC proceeds.
    if (otherWins(srcMac)) startProbe(nowMs);
  } else if (_state == HELD && id == _id) {
    // Defend the held id: tell the prober directly and everybody else.
    sendBeacon(srcMac, nowMs);
    sendBeacon(nullptr, nowMs);
  }
}

void IrIdNegotiator::onBeacon(const uint8_t srcMac[6], uint8_t id,
                              uint32_t nowMs) {
  if (!validId(id) || memcmp(srcMac, _mac, 6) == 0) return;
  if (id != _id) note(id, nowMs);

  if (_state == PROBE && id == _candidate) {
    // Somebody holds it – a holder always beats a prober.
    startProbe(nowMs);
  } else if (_state == HELD && id == _id) {
    // Two holders of the same id met (booted out of range of each other).
    if (otherWins(srcMac)) {
      note(id, nowMs);  // the winner keeps it – never pick it again now
      setId(0);
      startProbe(nowMs);  // straight to PROBE, no second LISTEN
    } else {
      sendBeacon(nullptr, nowMs);  // make the other one yield
    }
  }
}

void IrIdNegotiator::onHitReport(uint8_t shooterId, uint32_t nowMs) {
  // shooter 0 = somebody still negotiating (or a target self-test): no info.
  if (!validId(shooterId) || shooterId == _id) return;
  note(shooterId, nowMs);
  if (_state == PROBE && shooterId == _candidate) startProbe(nowMs);
}

bool IrIdNegotiator::consumeIdChanged() {
  const bool c = _idChanged;
  _idChanged = false;
  return c;
}

bool IrIdNegotiator::isOccupied(uint8_t id, uint32_t nowMs) const {
  if (!validId(id) || !_seen[id]) return false;
  return !elapsed(nowMs, _seenMs[id], IRID_T_EXPIRE_MS);
}

// --- internals ---------------------------------------------------------------

void IrIdNegotiator::note(uint8_t id, uint32_t nowMs) {
  if (!validId(id)) return;
  _seen[id] = true;
  _seenMs[id] = nowMs;
}

bool IrIdNegotiator::otherWins(const uint8_t otherMac[6]) const {
  return memcmp(otherMac, _mac, 6) < 0;
}

uint8_t IrIdNegotiator::pickCandidate(uint32_t nowMs) const {
  // 1) last held id (NVS), 2) MAC-derived, 3) upwards from there, cyclic.
  if (_preferred != 0 && !isOccupied(_preferred, nowMs)) return _preferred;
  const uint8_t base = (uint8_t)((_mac[5] % 15) + 1);
  for (uint8_t i = 0; i < 15; i++) {
    const uint8_t id = (uint8_t)(((base - 1 + i) % 15) + 1);
    if (!isOccupied(id, nowMs)) return id;
  }
  return 0;  // all taken
}

void IrIdNegotiator::startProbe(uint32_t nowMs) {
  const uint8_t c = pickCandidate(nowMs);
  if (c == 0) {
    // Fully booked: stay (or fall back to) id 0 and try again later.
    _state = LISTEN;
    _candidate = 0;
    _listenUntil = nowMs + IRID_T_RETRY_MS;
    return;
  }
  _state = PROBE;
  _candidate = c;
  _probesSent = 0;
  sendProbe(nowMs);
}

void IrIdNegotiator::sendProbe(uint32_t nowMs) {
  _lastProbeMs = nowMs;
  _probesSent++;
  if (_send) _send(TX_PROBE, _candidate, nullptr, _ctx);
}

void IrIdNegotiator::sendBeacon(const uint8_t *dstMac, uint32_t nowMs) {
  _lastBeaconMs = nowMs;
  if (_send) _send(TX_BEACON, _id, dstMac, _ctx);
}

void IrIdNegotiator::becomeHeld(uint32_t nowMs) {
  _state = HELD;
  setId(_candidate);
  _candidate = 0;
  _seen[_id] = false;  // the own id is never "occupied by someone else"
  // Announce twice at once – broadcast is unacknowledged.
  sendBeacon(nullptr, nowMs);
  sendBeacon(nullptr, nowMs);
}

void IrIdNegotiator::setId(uint8_t id) {
  if (id == _id) return;
  _id = id;
  _idChanged = true;
}

}  // namespace inow
