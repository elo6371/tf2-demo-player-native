// CA-01 PVS dormancy self-test probe.
//
// Drives the real svc_PacketEntities decoder (decodeDemoMessageStream) and
// snapshot history replay (queryEntitySnapshotAtOrBeforeTick) with synthetic
// wire streams so every PVS transition rule is verified against the shipped
// decoder path:
//   - Enter -> resident, Leave -> dormant (state & placement frozen, never 0)
//   - Replay while dormant -> dormantByIndex[slot] == 1, draw gate = false
//   - Replay across checkpoint/archive flush -> dormantByIndex preserved
//   - Re-Enter or DeltaEnt/Preserve -> resident, returns incremented
//   - Delete or explicit remove -> slot & dormancy cleared without return bump
//   - Mutation check: packetEntityLeaveCount > 0 with 0 departures => wiringSuspect
//
// Usage:
//   entity_dormant_probe --self-test
//
// Exit: 0 = every assertion held; 1 = one did not; 2 = usage error.

#include "demo_header.h"
#include "entity_dormant.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

int g_checks = 0;
int g_failures = 0;

void check(bool condition, const char* what) {
  ++g_checks;
  if (condition) return;
  ++g_failures;
  std::printf("SELF-TEST FAILED: %s\n", what);
}

void checkSize(std::size_t actual, std::size_t expected, const char* what) {
  ++g_checks;
  if (actual == expected) return;
  ++g_failures;
  std::printf("SELF-TEST FAILED: %s (expected %zu, got %zu)\n", what, expected, actual);
}

struct BitWriter {
  std::vector<std::uint8_t> bytes;
  std::size_t bit = 0;

  void write(std::uint32_t value, std::size_t width) {
    for (std::size_t i = 0; i < width; ++i) {
      if (bit % 8u == 0u) bytes.push_back(0u);
      bytes.back() |= static_cast<std::uint8_t>(((value >> i) & 1u) << (bit % 8u));
      ++bit;
    }
  }

  void writeFloat32(float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    write(bits, 32u);
  }

  void writeBitVar(std::uint32_t value) {
    if (value < 16u) {
      write(0u, 2u);
      write(value, 4u);
    } else if (value < 256u) {
      write(1u, 2u);
      write(value, 8u);
    } else if (value < 4096u) {
      write(2u, 2u);
      write(value, 12u);
    } else {
      write(3u, 2u);
      write(value, 32u);
    }
  }

  void append(const BitWriter& other) {
    for (std::size_t i = 0; i < other.bit; ++i) {
      write(static_cast<std::uint32_t>((other.bytes[i / 8u] >> (i % 8u)) & 1u), 1u);
    }
  }

  void padToByte() {
    while (bit % 8u != 0u) write(0u, 1u);
  }
};

tf2::native::DemoNetworkSummary makeSummary(bool enableDormancy = true) {
  tf2::native::DemoNetworkSummary summary;
  summary.networkProtocol = 24;
  // CA-01: the write path is opt-in. The lifecycle tests below exercise the
  // enabled capture; testDisabledGateIsBaseBehavior checks the off path stays at
  // 054fb628 behaviour (no Leave event, no bitmap, no counters).
  tf2::native::setEntityDormantEnabled(summary, enableDormancy);

  tf2::native::SendTableSchema table;
  table.name = "DT_BaseEntity";
  tf2::native::SendPropSchema origin;
  origin.type = tf2::native::SendPropType::Vector;
  origin.name = "m_vecOrigin";
  origin.ownerTable = "DT_BaseEntity";
  origin.bitCount = 32;
  origin.flags = 4u;  // SPROP_NOSCALE: 3x 32-bit floats
  table.flattenedProps = {origin};
  table.props = table.flattenedProps;
  summary.sendTableSchemas.push_back(table);

  tf2::native::ServerClassSchema serverClass;
  serverClass.id = 0;
  serverClass.name = "CBaseEntity";
  serverClass.dataTable = "DT_BaseEntity";
  summary.serverClassSchemas.push_back(serverClass);
  summary.serverClassNames.push_back("CBaseEntity");
  summary.serverClassCount = 1u;
  summary.dataTableServerClassCount = 1u;
  return summary;
}

void writeUpdate(BitWriter& out, std::uint32_t diff, std::uint32_t type) {
  out.writeBitVar(diff);
  out.write(type, 2u);
}

void writeOriginProp(BitWriter& out, float x, float y, float z) {
  out.write(1u, 1u);     // hasProp
  out.writeBitVar(0u);   // propIndex 0 = m_vecOrigin
  out.writeFloat32(x);
  out.writeFloat32(y);
  out.writeFloat32(z);
  out.write(0u, 1u);     // end of prop list
}

bool decodePacketEntities(tf2::native::DemoNetworkSummary& summary,
                          std::int32_t tick,
                          bool isDelta,
                          std::uint32_t deltaFrom,
                          std::uint32_t updatedEntries,
                          const BitWriter& body) {
  BitWriter stream;
  stream.write(3u, 6u);                            // net_Tick
  stream.write(static_cast<std::uint32_t>(tick), 32u);
  stream.write(0u, 16u);
  stream.write(0u, 16u);

  stream.write(26u, 6u);                           // svc_PacketEntities
  stream.write(16u, 11u);                          // maxEntries
  stream.write(isDelta ? 1u : 0u, 1u);
  if (isDelta) stream.write(deltaFrom, 32u);
  stream.write(0u, 1u);                            // baseline
  stream.write(updatedEntries, 11u);
  stream.write(static_cast<std::uint32_t>(body.bit), 20u);
  stream.write(0u, 1u);                            // updateBaseline
  stream.append(body);
  stream.padToByte();

  tf2::native::DemoMessageStreamResult result;
  return tf2::native::decodeDemoMessageStream(stream.bytes, stream.bit, tick, -1, summary, result)
      && result.packetValid && !result.hitUnknownType;
}

bool nearOrigin(const tf2::native::EntityDormantPlacement& p, float x, float y, float z) {
  return p.valid && p.hasOrigin
      && std::fabs(p.origin[0] - x) < 1e-4f
      && std::fabs(p.origin[1] - y) < 1e-4f
      && std::fabs(p.origin[2] - z) < 1e-4f;
}

void testWireLifecycleAndHistoryReplay() {
  tf2::native::DemoNetworkSummary summary = makeSummary();

  // 1. Tick 100: Enter entity 0 at (123.5, -45.0, 67.25).
  {
    BitWriter body;
    writeUpdate(body, 0u, 2u);  // EnterPVS
    body.write(0u, 1u);         // class 0
    body.write(1u, 10u);        // serial
    writeOriginProp(body, 123.5f, -45.0f, 67.25f);
    check(decodePacketEntities(summary, 100, false, 0u, 1u, body), "Enter packet decodes");
    checkSize(summary.entityDormant.size(), 2048u,
              "enabled capture allocates the 2048-slot bitmap");
    check(tf2::native::entityDormantShouldDraw(summary.entityDormant, 0u),
          "entered entity is drawable");
    checkSize(summary.entityDormantDepartures, 0u, "no departures on Enter");
    checkSize(summary.entityDormantReturns, 0u, "no returns on initial Enter");
  }

  // 2. Tick 110: DeltaEnt ("preserve" updateType 0) moves entity 0 to (200, 10, 32).
  {
    BitWriter body;
    writeUpdate(body, 0u, 0u);  // DeltaEnt
    writeOriginProp(body, 200.0f, 10.0f, 32.0f);
    body.write(0u, 1u);         // no explicit deletes
    check(decodePacketEntities(summary, 110, true, 100u, 1u, body), "Preserve packet decodes");
    check(tf2::native::entityDormantShouldDraw(summary.entityDormant, 0u),
          "preserved resident entity stays drawable");
    checkSize(summary.entityDormantReturns, 0u, "resident preserve does not increment returns");
  }

  // 3. Tick 120: LeavePVS (updateType 1). State stays frozen at (200, 10, 32), draw gate blocks.
  {
    BitWriter body;
    writeUpdate(body, 0u, 1u);  // LeavePVS
    body.write(0u, 1u);         // no explicit deletes
    check(decodePacketEntities(summary, 120, true, 110u, 1u, body), "Leave packet decodes");
    check(!tf2::native::entityDormantShouldDraw(summary.entityDormant, 0u),
          "dormant entity is gated off from drawing");
    checkSize(summary.entityDormantDepartures, 1u, "Leave increments departures");
    check(summary.entityClassByIndex[0] == 0, "Leave retains entity class");
    const tf2::native::EntityDormantPlacement liveHeld =
        tf2::native::resolveEntityDormantStatePlacement(summary.entityStates, 0u);
    check(nearOrigin(liveHeld, 200.0f, 10.0f, 32.0f),
          "Leave freezes last known origin instead of zeroing to (0,0,0)");
  }

  // 4. Replay tick 120 via queryEntitySnapshotAtOrBeforeTick: dormantByIndex carries dormant=1.
  {
    std::vector<tf2::native::EntityState> states;
    std::vector<std::uint8_t> dormantByIndex;
    std::int32_t resolved = -1;
    const auto status = tf2::native::queryEntitySnapshotAtOrBeforeTick(
        summary, 120, states, &resolved, &dormantByIndex);
    check(status == tf2::native::EntitySnapshotQueryStatus::Available && resolved == 120,
          "history replay resolves tick 120");
    check(!tf2::native::entityDormantShouldDraw(dormantByIndex, 0u),
          "replayed snapshot marks left entity dormant");
    check(nearOrigin(tf2::native::resolveEntityDormantStatePlacement(states, 0u),
                     200.0f, 10.0f, 32.0f),
          "replayed snapshot keeps last known placement frozen");
  }

  // 5. Tick 130: EnterPVS re-entry at (300, 20, 40) clears dormancy and increments returns.
  {
    BitWriter body;
    writeUpdate(body, 0u, 2u);  // EnterPVS
    body.write(0u, 1u);
    body.write(1u, 10u);
    writeOriginProp(body, 300.0f, 20.0f, 40.0f);
    body.write(0u, 1u);
    check(decodePacketEntities(summary, 130, true, 120u, 1u, body), "Re-Enter packet decodes");
    check(tf2::native::entityDormantShouldDraw(summary.entityDormant, 0u),
          "re-entering entity is drawable again");
    checkSize(summary.entityDormantReturns, 1u, "Enter after Leave increments returns");
    std::vector<tf2::native::EntityState> states;
    std::vector<std::uint8_t> dormantByIndex;
    const auto status = tf2::native::queryEntitySnapshotAtOrBeforeTick(
        summary, 130, states, nullptr, &dormantByIndex);
    check(status == tf2::native::EntitySnapshotQueryStatus::Available,
          "history replay resolves tick 130");
    check(tf2::native::entityDormantShouldDraw(dormantByIndex, 0u),
          "replayed snapshot clears dormant bit on re-Enter");
    check(nearOrigin(tf2::native::resolveEntityDormantStatePlacement(states, 0u),
                     300.0f, 20.0f, 40.0f),
          "replayed snapshot places returning entity directly at Enter origin");
  }

  // 6. Tick 140 Leave -> Tick 150 Delete (updateType 3): Delete clears slot without return count.
  {
    BitWriter leaveBody;
    writeUpdate(leaveBody, 0u, 1u);
    leaveBody.write(0u, 1u);
    check(decodePacketEntities(summary, 140, true, 130u, 1u, leaveBody), "second Leave decodes");
    checkSize(summary.entityDormantDepartures, 2u, "second Leave increments departures");

    BitWriter delBody;
    writeUpdate(delBody, 0u, 3u);  // Delete
    delBody.write(0u, 1u);
    check(decodePacketEntities(summary, 150, true, 140u, 1u, delBody), "Delete packet decodes");
    check(summary.entityClassByIndex[0] == -1, "Delete clears class");
    check(tf2::native::entityDormantShouldDraw(summary.entityDormant, 0u),
          "Delete resets dormant bit");
    checkSize(summary.entityDormantReturns, 1u,
              "Delete clears slot without counting a PVS return");
  }

  // 7. Enter -> Leave -> Explicit removal list clears dormant bit without counting return.
  {
    BitWriter enterBody;
    writeUpdate(enterBody, 0u, 2u);
    enterBody.write(0u, 1u);
    enterBody.write(2u, 10u);
    writeOriginProp(enterBody, 10.0f, 20.0f, 30.0f);
    enterBody.write(0u, 1u);
    check(decodePacketEntities(summary, 160, true, 150u, 1u, enterBody), "third Enter decodes");

    BitWriter leaveBody;
    writeUpdate(leaveBody, 0u, 1u);
    leaveBody.write(0u, 1u);
    check(decodePacketEntities(summary, 170, true, 160u, 1u, leaveBody), "third Leave decodes");

    BitWriter removeBody;
    removeBody.write(1u, 1u);   // hasRemoved = 1
    removeBody.write(0u, 11u);  // removedEntity = 0
    removeBody.write(0u, 1u);   // end of explicit removals
    check(decodePacketEntities(summary, 180, true, 170u, 0u, removeBody),
          "explicit removal packet decodes");
    check(summary.entityClassByIndex[0] == -1, "explicit removal clears class");
    check(tf2::native::entityDormantShouldDraw(summary.entityDormant, 0u),
          "explicit removal resets dormant bit");
    checkSize(summary.entityDormantReturns, 1u,
              "explicit removal does not count as PVS return");
  }
}

void testCheckpointArchiveCarry() {
  tf2::native::DemoNetworkSummary summary = makeSummary();
  summary.entityHistoryLimits.maxEvents = 2u;
  summary.entityHistoryLimits.maxCheckpoints = 2u;
  summary.entityHistoryLimits.checkpointStride = 1u;
  summary.entityHistoryLimits.archiveMax = 8u;

  BitWriter enterBody;
  writeUpdate(enterBody, 0u, 2u);
  enterBody.write(0u, 1u);
  enterBody.write(1u, 10u);
  writeOriginProp(enterBody, 50.0f, 60.0f, 70.0f);
  check(decodePacketEntities(summary, 10, false, 0u, 1u, enterBody), "archive Enter decodes");

  BitWriter leaveBody;
  writeUpdate(leaveBody, 0u, 1u);
  leaveBody.write(0u, 1u);
  check(decodePacketEntities(summary, 20, true, 10u, 1u, leaveBody), "archive Leave decodes");

  // Push extra updates on entity 1 to flush tick 20's checkpoint into entityHistoryArchive.
  BitWriter enter1;
  writeUpdate(enter1, 1u, 2u);
  enter1.write(0u, 1u);
  enter1.write(1u, 10u);
  writeOriginProp(enter1, 1.0f, 2.0f, 3.0f);
  enter1.write(0u, 1u);
  check(decodePacketEntities(summary, 30, true, 20u, 1u, enter1), "archive flush 1 decodes");

  BitWriter preserve1;
  writeUpdate(preserve1, 1u, 0u);
  writeOriginProp(preserve1, 4.0f, 5.0f, 6.0f);
  preserve1.write(0u, 1u);
  check(decodePacketEntities(summary, 40, true, 30u, 1u, preserve1), "archive flush 2 decodes");

  check(!summary.entityHistoryArchive.empty(), "history checkpoints flushed into archive");
  std::vector<tf2::native::EntityState> states;
  std::vector<std::uint8_t> dormantByIndex;
  std::int32_t resolved = -1;
  const auto status = tf2::native::queryEntitySnapshotAtOrBeforeTick(
      summary, 25, states, &resolved, &dormantByIndex);
  check(status == tf2::native::EntitySnapshotQueryStatus::Checkpoint && resolved == 20,
        "archive query resolves dormant checkpoint at tick 20");
  check(!tf2::native::entityDormantShouldDraw(dormantByIndex, 0u),
        "archived checkpoint preserves entity 0 dormant state");
  check(nearOrigin(tf2::native::resolveEntityDormantStatePlacement(states, 0u),
                   50.0f, 60.0f, 70.0f),
        "archived checkpoint preserves frozen origin");
}

void testPreserveClearsDormantReturn() {
  tf2::native::DemoNetworkSummary summary = makeSummary();
  BitWriter enterBody;
  writeUpdate(enterBody, 0u, 2u);
  enterBody.write(0u, 1u);
  enterBody.write(1u, 10u);
  writeOriginProp(enterBody, 1.0f, 1.0f, 1.0f);
  check(decodePacketEntities(summary, 100, false, 0u, 1u, enterBody), "preserve-return Enter");

  BitWriter leaveBody;
  writeUpdate(leaveBody, 0u, 1u);
  leaveBody.write(0u, 1u);
  check(decodePacketEntities(summary, 110, true, 100u, 1u, leaveBody), "preserve-return Leave");

  BitWriter preserveBody;
  writeUpdate(preserveBody, 0u, 0u);
  writeOriginProp(preserveBody, 9.0f, 8.0f, 7.0f);
  preserveBody.write(0u, 1u);
  check(decodePacketEntities(summary, 120, true, 110u, 1u, preserveBody), "preserve-return DeltaEnt");
  check(tf2::native::entityDormantShouldDraw(summary.entityDormant, 0u),
        "Preserve clears dormancy");
  checkSize(summary.entityDormantReturns, 1u, "Preserve after Leave counts as return");

  std::vector<tf2::native::EntityState> states;
  std::vector<std::uint8_t> dormantByIndex;
  const auto status = tf2::native::queryEntitySnapshotAtOrBeforeTick(
      summary, 120, states, nullptr, &dormantByIndex);
  check(status == tf2::native::EntitySnapshotQueryStatus::Available,
        "preserve-return replay Available");
  check(tf2::native::entityDormantShouldDraw(dormantByIndex, 0u),
        "preserve-return replay clears dormant bit");
}

void testWiringSuspectMutation() {
  tf2::native::DemoNetworkSummary mutated;
  mutated.packetEntityLeaveCount = 5u;
  mutated.entityDormantDepartures = 0u;
  const tf2::native::EntityDormantStats bad = tf2::native::summarizeEntityDormant(mutated);
  check(bad.wiringSuspect, "Leave updates with zero departures trigger wiringSuspect");

  mutated.entityDormantDepartures = 5u;
  const tf2::native::EntityDormantStats ok = tf2::native::summarizeEntityDormant(mutated);
  check(!ok.wiringSuspect, "recorded departures clear wiringSuspect");
}

void testDrawGateAndSnapshots() {
  std::vector<std::uint8_t> bitmap = {0u, 1u};
  check(tf2::native::entityDormantShouldDraw(bitmap, 0u), "resident slot is drawn");
  check(!tf2::native::entityDormantShouldDraw(bitmap, 1u), "dormant slot is not drawn");
  check(tf2::native::entityDormantShouldDraw(bitmap, 2u), "undescribed slot fails open");

  tf2::native::DemoNetworkSummary summary;
  summary.entityDormant.assign(2048u, 0u);
  summary.entityDormant[11u] = 1u;
  std::vector<tf2::native::EntityDormantSnapshot> snapshots;
  snapshots.push_back(tf2::native::captureEntityDormantSnapshot(summary, 10));
  summary.entityDormant[11u] = 0u;
  snapshots.push_back(tf2::native::captureEntityDormantSnapshot(summary, 20));
  check(tf2::native::entityDormantAtTick(snapshots, 11u, 15), "snapshot at 10 covers tick 15");
  check(!tf2::native::entityDormantAtTick(snapshots, 11u, 25), "snapshot at 20 reflects return");
  check(!tf2::native::entityDormantAtTick(snapshots, 11u, 5), "before first snapshot fails open");
  check(!tf2::native::entityDormantAtTick({}, 11u, 15), "empty snapshots fail open");
}

void testDisabledGateIsBaseBehavior() {
  // CA-01 v2: with the write path disabled the decoder must behave like
  // 054fb628 -- the leave counter still runs, but no Leave history event is
  // pushed, the bitmap is never allocated and the dormant counters stay at 0.
  tf2::native::DemoNetworkSummary summary = makeSummary(false);

  BitWriter enterBody;
  writeUpdate(enterBody, 0u, 2u);
  enterBody.write(0u, 1u);
  enterBody.write(1u, 10u);
  writeOriginProp(enterBody, 10.0f, 20.0f, 30.0f);
  check(decodePacketEntities(summary, 100, false, 0u, 1u, enterBody), "disabled Enter decodes");
  check(summary.entityDormant.empty(), "disabled Enter allocates no bitmap");
  checkSize(summary.entityDormantDepartures, 0u, "disabled Enter writes no departure");

  const std::size_t eventsAfterEnter = summary.entityHistoryEvents.size();
  BitWriter leaveBody;
  writeUpdate(leaveBody, 0u, 1u);
  leaveBody.write(0u, 1u);
  check(decodePacketEntities(summary, 120, true, 100u, 1u, leaveBody), "disabled Leave decodes");
  checkSize(summary.packetEntityLeaveCount, 1u, "disabled Leave still counts the leave");
  check(summary.entityHistoryEvents.size() == eventsAfterEnter,
        "disabled Leave pushes no history event");
  checkSize(summary.entityDormantDepartures, 0u, "disabled Leave writes no departure");
  checkSize(summary.entityDormantReturns, 0u, "disabled Leave writes no return");
  check(summary.entityDormant.empty(), "disabled Leave allocates no bitmap");
  bool anyDormantEvent = false;
  for (const auto& event : summary.entityHistoryEvents) anyDormantEvent = anyDormantEvent || event.dormant;
  check(!anyDormantEvent, "disabled scan emits no dormant history event");
  bool anyCheckpointBitmap = false;
  for (const auto& checkpoint : summary.entityHistoryCheckpoints) {
    anyCheckpointBitmap = anyCheckpointBitmap || !checkpoint.dormantByIndex.empty();
  }
  check(!anyCheckpointBitmap, "disabled scan stores no dormant bitmap in checkpoints");

  BitWriter deleteBody;
  writeUpdate(deleteBody, 0u, 3u);
  deleteBody.write(0u, 1u);
  check(decodePacketEntities(summary, 150, true, 120u, 1u, deleteBody), "disabled Delete decodes");
  checkSize(summary.packetEntityDeleteCount, 1u, "disabled Delete still counts the delete");
  check(summary.entityDormant.empty(), "disabled Delete keeps the bitmap unallocated");

  std::vector<tf2::native::EntityState> states;
  std::vector<std::uint8_t> dormantByIndex;
  std::int32_t resolved = -1;
  const auto status = tf2::native::queryEntitySnapshotAtOrBeforeTick(
      summary, 120, states, &resolved, &dormantByIndex);
  check(status == tf2::native::EntitySnapshotQueryStatus::Available,
        "disabled replay resolves inside the live history window");
  checkSize(dormantByIndex.size(), 2048u, "disabled replay returns the zeroed draw bitmap");
  std::size_t dormantBits = 0;
  for (const std::uint8_t bit : dormantByIndex) {
    if (bit != 0u) ++dormantBits;
  }
  checkSize(dormantBits, 0u, "disabled replay records no dormant slot");
  check(tf2::native::entityDormantShouldDraw(dormantByIndex, 0u),
        "disabled replay keeps the base draw gate (fail-open)");
}

void runEnabledScanDemo() {
  // Delivery evidence: one enabled scan over the same synthetic wire as the
  // lifecycle test, printing the summarized counters the review asked to see.
  tf2::native::DemoNetworkSummary summary = makeSummary(true);
  BitWriter enterBody;
  writeUpdate(enterBody, 0u, 2u);
  enterBody.write(0u, 1u);
  enterBody.write(1u, 10u);
  writeOriginProp(enterBody, 10.0f, 20.0f, 30.0f);
  check(decodePacketEntities(summary, 100, false, 0u, 1u, enterBody), "demo scan Enter decodes");
  BitWriter leaveBody;
  writeUpdate(leaveBody, 0u, 1u);
  leaveBody.write(0u, 1u);
  check(decodePacketEntities(summary, 120, true, 100u, 1u, leaveBody), "demo scan Leave decodes");
  const tf2::native::EntityDormantStats stats = tf2::native::summarizeEntityDormant(summary);
  std::printf("enabled scan: packets=%zu events=%zu leaves=%zu departures=%zu returns=%zu dormant=%zu bitmap=%zu wiringSuspect=%d\n",
              summary.packetEntitiesCount, summary.entityHistoryEvents.size(), stats.packetLeaves,
              stats.departures, stats.returns, stats.activeDormant, summary.entityDormant.size(),
              stats.wiringSuspect ? 1 : 0);
}

int runSelfTest() {
  testDisabledGateIsBaseBehavior();
  testWireLifecycleAndHistoryReplay();
  testCheckpointArchiveCarry();
  testPreserveClearsDormantReturn();
  testWiringSuspectMutation();
  testDrawGateAndSnapshots();
  runEnabledScanDemo();
  if (g_failures != 0) {
    std::printf("SELF-TEST FAILED: %d of %d assertions\n", g_failures, g_checks);
    return 1;
  }
  std::printf("SELF-TEST OK: %d assertions\n", g_checks);
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--self-test") == 0) return runSelfTest();
  }
  std::printf("usage: entity_dormant_probe --self-test\n");
  return 2;
}
