// Unit tests for the IR id negotiation (IrIdNegotiator, PROTOCOL.md
// "IR-ID-Aushandlung"). Run on the PC:  pio test -e native
//
// Harness: nodes record what they send; a virtual broadcast bus delivers
// it to all other booted nodes on the next 10-ms tick (optionally with
// packet loss from a deterministic LCG). Time is fully simulated.

#include <unity.h>
#include <string.h>
#include <vector>

#include "IrIdNegotiator.h"

using namespace inow;

struct Tx {
  IrIdNegotiator::TxKind kind;
  uint8_t id;
  bool unicast;
  uint8_t dst[6];
  uint8_t src[6];
};

struct Node {
  IrIdNegotiator neg;
  uint8_t mac[6] = {0};
  uint8_t pref = 0;
  uint32_t bootAt = 0;
  bool booted = false;
  std::vector<Tx> out;
  uint32_t msOnZeroAfterHeld = 0;  // yield window measurement
  bool wasHeld = false;
};

static void sendCb(IrIdNegotiator::TxKind kind, uint8_t id,
                   const uint8_t *dst, void *ctx) {
  Node *n = static_cast<Node *>(ctx);
  Tx t;
  t.kind = kind;
  t.id = id;
  t.unicast = dst != nullptr;
  if (dst) memcpy(t.dst, dst, 6);
  memcpy(t.src, n->mac, 6);
  n->out.push_back(t);
}

static void setMac(uint8_t *m, uint8_t last) {
  m[0] = 0x24; m[1] = 0x6F; m[2] = 0x28; m[3] = 0x00; m[4] = 0x00; m[5] = last;
}

struct Rng {
  uint32_t s;
  uint32_t next() { s = s * 1664525u + 1013904223u; return s; }
  bool chance(uint32_t pct) { return ((next() >> 8) % 100) < pct; }
};

struct Bus {
  std::vector<Node *> nodes;
  uint32_t now = 0;
  uint32_t lossPct = 0;
  Rng rng{12345};

  void deliver() {
    std::vector<Tx> all;
    for (Node *n : nodes) {
      for (const Tx &t : n->out) all.push_back(t);
      n->out.clear();
    }
    for (const Tx &t : all) {
      for (Node *n : nodes) {
        if (!n->booted || memcmp(n->mac, t.src, 6) == 0) continue;
        if (t.unicast && memcmp(n->mac, t.dst, 6) != 0) continue;
        if (lossPct && rng.chance(lossPct)) continue;
        if (t.kind == IrIdNegotiator::TX_PROBE)
          n->neg.onProbe(t.src, t.id, now);
        else
          n->neg.onBeacon(t.src, t.id, now);
      }
    }
  }

  void step(uint32_t ms) {
    for (uint32_t i = 0; i < ms; i += 10) {
      now += 10;
      for (Node *n : nodes) {
        if (!n->booted && (int32_t)(now - n->bootAt) >= 0) {
          n->booted = true;
          n->neg.begin(n->mac, n->pref, now, sendCb, n);
        }
        if (!n->booted) continue;
        n->neg.tick(now);
        if (n->neg.state() == IrIdNegotiator::HELD) n->wasHeld = true;
        if (n->wasHeld && n->neg.irId() == 0) n->msOnZeroAfterHeld += 10;
      }
      deliver();
    }
  }
};

static size_t countTx(const Node &n, IrIdNegotiator::TxKind kind) {
  size_t c = 0;
  for (const Tx &t : n.out) if (t.kind == kind) c++;
  return c;
}

// --- 1) single station ------------------------------------------------------

void test_single_station_listen_probe_held() {
  Node a; setMac(a.mac, 0x07);  // (7 % 15) + 1 = 8
  Bus bus; bus.nodes = {&a};
  bus.step(IRID_T_LISTEN_MS - 10);
  TEST_ASSERT_EQUAL(IrIdNegotiator::LISTEN, a.neg.state());
  TEST_ASSERT_EQUAL_UINT8(0, a.neg.irId());
  bus.step(20);
  TEST_ASSERT_EQUAL(IrIdNegotiator::PROBE, a.neg.state());
  TEST_ASSERT_EQUAL_UINT8(8, a.neg.candidate());
  bus.step(IRID_T_PROBE_MS * IRID_N_PROBE + 20);
  TEST_ASSERT_EQUAL(IrIdNegotiator::HELD, a.neg.state());
  TEST_ASSERT_EQUAL_UINT8(8, a.neg.irId());
  TEST_ASSERT_TRUE(a.neg.consumeIdChanged());
  TEST_ASSERT_FALSE(a.neg.consumeIdChanged());
}

void test_single_station_prefers_nvs_id() {
  Node a; setMac(a.mac, 0x07); a.pref = 5;
  Bus bus; bus.nodes = {&a};
  bus.step(IRID_T_LISTEN_MS + IRID_T_PROBE_MS * IRID_N_PROBE + 50);
  TEST_ASSERT_EQUAL_UINT8(5, a.neg.irId());
}

void test_held_station_beacons_periodically() {
  Node a; setMac(a.mac, 0x01);
  Bus bus; bus.nodes = {&a};
  bus.step(IRID_T_LISTEN_MS + 600);
  TEST_ASSERT_EQUAL(IrIdNegotiator::HELD, a.neg.state());
  a.out.clear();
  // bus.deliver() clears out every tick, so count via a direct tick loop
  size_t beacons = 0;
  for (uint32_t t = 0; t < IRID_T_BEACON_MS * 3; t += 10) {
    a.neg.tick(bus.now + t);
    beacons += countTx(a, IrIdNegotiator::TX_BEACON);
    a.out.clear();
  }
  TEST_ASSERT_EQUAL_size_t(3, beacons);
}

void test_held_station_defends_against_probe() {
  Node a; setMac(a.mac, 0x01);
  Bus bus; bus.nodes = {&a};
  bus.step(IRID_T_LISTEN_MS + 600);
  const uint8_t held = a.neg.irId();
  uint8_t other[6]; setMac(other, 0x50);
  a.out.clear();
  a.neg.onProbe(other, held, bus.now);
  TEST_ASSERT_EQUAL_size_t(2, a.out.size());
  TEST_ASSERT_TRUE(a.out[0].unicast);
  TEST_ASSERT_EQUAL_MEMORY(other, a.out[0].dst, 6);
  TEST_ASSERT_FALSE(a.out[1].unicast);
  TEST_ASSERT_EQUAL_UINT8(held, a.out[1].id);
  // probe for a different id: no reaction
  a.out.clear();
  a.neg.onProbe(other, (uint8_t)(held % 15 + 1), bus.now);
  TEST_ASSERT_EQUAL_size_t(0, a.out.size());
  // A holder beats a prober regardless of MAC order: a SMALLER prober MAC
  // must not evict the holder (MAC tie-break only applies PROBE-vs-PROBE
  // and HELD-vs-HELD).
  uint8_t smaller[6]; setMac(smaller, 0x00);
  a.out.clear();
  a.neg.consumeIdChanged();  // clear the 0 -> held transition
  a.neg.onProbe(smaller, held, bus.now);
  TEST_ASSERT_EQUAL(IrIdNegotiator::HELD, a.neg.state());
  TEST_ASSERT_EQUAL_UINT8(held, a.neg.irId());
  TEST_ASSERT_EQUAL_size_t(2, a.out.size());
  TEST_ASSERT_FALSE(a.neg.consumeIdChanged());  // no yield
}

void test_ignores_own_mac_and_invalid_ids() {
  Node a; setMac(a.mac, 0x01);
  a.neg.begin(a.mac, 0, 0, sendCb, &a);
  a.neg.onBeacon(a.mac, 2, 0);   // own echo
  uint8_t other[6]; setMac(other, 0x50);
  a.neg.onBeacon(other, 0, 0);   // 0 is never "held"
  a.neg.onBeacon(other, 16, 0);  // out of range
  for (uint8_t id = 0; id < 16; id++) TEST_ASSERT_FALSE(a.neg.isOccupied(id, 0));
}

// --- 2) simultaneous boot, same candidate ---------------------------------

void test_simultaneous_boot_tie_break() {
  Node a, b;
  setMac(a.mac, 0x03);  // (3 % 15) + 1 = 4
  setMac(b.mac, 0x12);  // (18 % 15) + 1 = 4 – same candidate, bigger MAC
  Bus bus; bus.nodes = {&a, &b};
  bus.step(IRID_T_LISTEN_MS + 2000);
  TEST_ASSERT_EQUAL(IrIdNegotiator::HELD, a.neg.state());
  TEST_ASSERT_EQUAL(IrIdNegotiator::HELD, b.neg.state());
  TEST_ASSERT_EQUAL_UINT8(4, a.neg.irId());  // smaller MAC keeps it
  TEST_ASSERT_NOT_EQUAL(4, b.neg.irId());
  TEST_ASSERT_NOT_EQUAL(0, b.neg.irId());
}

// --- 3) late joiner meets a holder ----------------------------------------

void test_late_joiner_yields_to_holder() {
  Node a, b;
  setMac(a.mac, 0x12);  // bigger MAC, but holder – holders always win
  setMac(b.mac, 0x03);  // same base candidate 4
  b.bootAt = 10000;
  Bus bus; bus.nodes = {&a, &b};
  bus.step(10000 + IRID_T_LISTEN_MS + 2000);
  TEST_ASSERT_EQUAL_UINT8(4, a.neg.irId());
  TEST_ASSERT_EQUAL(IrIdNegotiator::HELD, b.neg.state());
  TEST_ASSERT_NOT_EQUAL(4, b.neg.irId());
  TEST_ASSERT_NOT_EQUAL(0, b.neg.irId());
}

// --- 4) two holders of the same id meet -------------------------------------

void test_two_holders_meet_bigger_mac_yields_fast() {
  Node a, b;
  setMac(a.mac, 0x03);
  setMac(b.mac, 0x12);
  // Boot each one alone (out of range of the other): both end up on 4.
  Bus solo1; solo1.nodes = {&a}; solo1.step(IRID_T_LISTEN_MS + 1000);
  Bus solo2; solo2.nodes = {&b}; solo2.step(IRID_T_LISTEN_MS + 1000);
  TEST_ASSERT_EQUAL_UINT8(4, a.neg.irId());
  TEST_ASSERT_EQUAL_UINT8(4, b.neg.irId());
  a.out.clear(); b.out.clear();

  // Now they hear each other.
  Bus bus; bus.nodes = {&a, &b}; bus.now = 20000;
  bus.step(IRID_T_BEACON_MS * 2 + 1000);
  TEST_ASSERT_EQUAL_UINT8(4, a.neg.irId());
  TEST_ASSERT_EQUAL(IrIdNegotiator::HELD, b.neg.state());
  TEST_ASSERT_NOT_EQUAL(4, b.neg.irId());
  TEST_ASSERT_NOT_EQUAL(0, b.neg.irId());
  TEST_ASSERT_EQUAL_UINT32(0, a.msOnZeroAfterHeld);
  // Yield window: probes only, no second LISTEN (spec: <= ~0.5 s).
  TEST_ASSERT_TRUE(b.msOnZeroAfterHeld <= IRID_T_PROBE_MS * IRID_N_PROBE + 50);
  TEST_ASSERT_TRUE(b.neg.consumeIdChanged());
}

// --- 5) HIT_REPORT as evidence ---------------------------------------------

void test_hit_report_is_evidence() {
  Node a; setMac(a.mac, 0x07);  // base 8
  Bus bus; bus.nodes = {&a};
  bus.step(100);
  a.neg.onHitReport(8, bus.now);  // an old-firmware station shoots as 8
  a.neg.onHitReport(0, bus.now);  // shooter 0 carries no information
  TEST_ASSERT_TRUE(a.neg.isOccupied(8, bus.now));
  bus.step(IRID_T_LISTEN_MS);
  TEST_ASSERT_EQUAL(IrIdNegotiator::PROBE, a.neg.state());
  TEST_ASSERT_EQUAL_UINT8(9, a.neg.candidate());
  // ...and a hit for the current candidate aborts the probe
  a.neg.onHitReport(9, bus.now);
  TEST_ASSERT_EQUAL_UINT8(10, a.neg.candidate());
}

// --- 6) evidence expires ---------------------------------------------------

void test_evidence_expires() {
  Node a; setMac(a.mac, 0x07);
  a.neg.begin(a.mac, 0, 0, sendCb, &a);
  uint8_t other[6]; setMac(other, 0x50);
  a.neg.onBeacon(other, 8, 1000);
  TEST_ASSERT_TRUE(a.neg.isOccupied(8, 1000 + IRID_T_EXPIRE_MS - 1));
  TEST_ASSERT_FALSE(a.neg.isOccupied(8, 1000 + IRID_T_EXPIRE_MS));
}

// --- 7) all ids taken -> stay 0, retry --------------------------------------

void test_all_ids_taken_falls_back_to_zero_and_retries() {
  Node a; setMac(a.mac, 0x07);
  Bus bus; bus.nodes = {&a};
  bus.step(100);
  for (uint8_t id = 1; id <= 15; id++) {
    uint8_t other[6]; setMac(other, (uint8_t)(0x40 + id));
    a.neg.onBeacon(other, id, bus.now);
  }
  bus.step(IRID_T_LISTEN_MS + 100);
  TEST_ASSERT_EQUAL(IrIdNegotiator::LISTEN, a.neg.state());
  TEST_ASSERT_EQUAL_UINT8(0, a.neg.irId());
  TEST_ASSERT_FALSE(a.neg.consumeIdChanged());
  // Nobody beacons anymore -> evidence expires -> the retry succeeds.
  bus.step(IRID_T_RETRY_MS + 1000);
  TEST_ASSERT_EQUAL(IrIdNegotiator::HELD, a.neg.state());
  TEST_ASSERT_EQUAL_UINT8(8, a.neg.irId());
}

// --- 8) bus simulation ------------------------------------------------------

static void runSim(size_t n, uint32_t lossPct, uint32_t seed, bool expectAll) {
  std::vector<Node> nodes(n);
  Bus bus;
  bus.lossPct = lossPct;
  bus.rng.s = seed;
  Rng boot{seed * 7 + 1};
  for (size_t i = 0; i < n; i++) {
    setMac(nodes[i].mac, (uint8_t)(0x10 + i * 15 % 97));  // many shared bases
    nodes[i].bootAt = boot.next() % 5000;
    bus.nodes.push_back(&nodes[i]);
  }
  bus.step(45000);

  bool used[16] = {false};
  size_t held = 0;
  for (Node &nd : nodes) {
    const uint8_t id = nd.neg.irId();
    if (id == 0) continue;
    TEST_ASSERT_EQUAL(IrIdNegotiator::HELD, nd.neg.state());
    TEST_ASSERT_FALSE_MESSAGE(used[id], "duplicate ir_id after settling");
    used[id] = true;
    held++;
  }
  if (expectAll) TEST_ASSERT_EQUAL_size_t(n, held);
  else TEST_ASSERT_EQUAL_size_t(15, held);
}

void test_simulation_six_stations_with_loss() {
  for (uint32_t seed = 1; seed <= 8; seed++) runSim(6, 10, seed, true);
}

void test_simulation_fifteen_stations_lossless() {
  runSim(15, 0, 3, true);
}

void test_simulation_sixteen_stations_one_stays_zero() {
  runSim(16, 0, 5, false);
}

void test_simulation_stable_after_settling() {
  // Once settled, nobody changes id anymore (no churn from beacons).
  std::vector<Node> nodes(5);
  Bus bus;
  for (size_t i = 0; i < 5; i++) {
    setMac(nodes[i].mac, (uint8_t)(0x03 + i * 15));  // all base 4
    bus.nodes.push_back(&nodes[i]);
  }
  bus.step(15000);
  uint8_t ids[5];
  for (size_t i = 0; i < 5; i++) {
    ids[i] = nodes[i].neg.irId();
    nodes[i].neg.consumeIdChanged();
  }
  bus.step(30000);
  for (size_t i = 0; i < 5; i++) {
    TEST_ASSERT_EQUAL_UINT8(ids[i], nodes[i].neg.irId());
    TEST_ASSERT_FALSE(nodes[i].neg.consumeIdChanged());
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_single_station_listen_probe_held);
  RUN_TEST(test_single_station_prefers_nvs_id);
  RUN_TEST(test_held_station_beacons_periodically);
  RUN_TEST(test_held_station_defends_against_probe);
  RUN_TEST(test_ignores_own_mac_and_invalid_ids);
  RUN_TEST(test_simultaneous_boot_tie_break);
  RUN_TEST(test_late_joiner_yields_to_holder);
  RUN_TEST(test_two_holders_meet_bigger_mac_yields_fast);
  RUN_TEST(test_hit_report_is_evidence);
  RUN_TEST(test_evidence_expires);
  RUN_TEST(test_all_ids_taken_falls_back_to_zero_and_retries);
  RUN_TEST(test_simulation_six_stations_with_loss);
  RUN_TEST(test_simulation_fifteen_stations_lossless);
  RUN_TEST(test_simulation_sixteen_stations_one_stays_zero);
  RUN_TEST(test_simulation_stable_after_settling);
  return UNITY_END();
}
