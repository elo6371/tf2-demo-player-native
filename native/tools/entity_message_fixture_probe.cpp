// Synthetic wire fixtures for the demo message decoders.
//
// Why this exists: the nine local demos only exercise the message layouts that
// happen to appear in them, and they cannot isolate a single layout. This probe
// builds bit-exact message streams by hand and drives the real decoder through
// decodeDemoMessageStream(), so every assertion is about the shipped code path,
// not a re-implementation.
//
// Two kinds of assertion are used:
//
//   1. Exact bit consumption. A message is followed by a sentinel net_Tick with
//      a known tick value. If the decoder consumed one bit too few or too many,
//      the sentinel is misaligned and summary.lastNetworkTick stops matching.
//      That is the only observable that catches an off-by-N-bits layout error.
//
//   2. Entity state reconstruction. svc_PacketEntities streams are built for
//      the baseline / delta / Preserve / Leave / Delete cases and the resulting
//      summary is inspected, including a delta that references a base frame the
//      decoder must report as unavailable rather than fake.
//
// Layouts come from the local Rust reference (work/_refs_demostf/src/demo/,
// demostf's tf_demo_parser) with the primitive widths cross-checked against
// Valve's source-sdk-2013 (src/public/bitbuf.cpp, src/engine/netmessages.h).

#include "demo_header.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

using tf2::native::DemoMessageStreamResult;
using tf2::native::DemoNetworkSummary;
using tf2::native::SendPropSchema;
using tf2::native::SendPropType;
using tf2::native::SendTableSchema;
using tf2::native::ServerClassSchema;

int g_failures = 0;

void check(bool ok, const std::string& label) {
  std::cout << (ok ? "PASS " : "FAIL ") << label << '\n';
  if (!ok) ++g_failures;
}

// ---------------------------------------------------------------- bit writer

struct BitWriter {
  std::vector<std::uint8_t> bytes;
  std::size_t bit = 0;

  void write(std::uint32_t value, std::size_t width) {
    for (std::size_t i = 0; i < width; ++i) {
      if (bit % 8u == 0) bytes.push_back(0);
      bytes.back() |= static_cast<std::uint8_t>(((value >> i) & 1u) << (bit % 8u));
      ++bit;
    }
  }

  void writeSigned(std::int32_t value, std::size_t width) {
    write(static_cast<std::uint32_t>(value), width);
  }

  void writeString(const std::string& value) {
    for (const char c : value) write(static_cast<std::uint8_t>(c), 8);
    write(0, 8);
  }

  // Mirrors MessageBits::readBitVar: a 2-bit width selector then the payload.
  void writeBitVar(std::uint32_t value) {
    if (value < 16u) { write(0, 2); write(value, 4); }
    else if (value < 256u) { write(1, 2); write(value, 8); }
    else if (value < 4096u) { write(2, 2); write(value, 12); }
    else { write(3, 2); write(value, 32); }
  }

  void append(const BitWriter& other) {
    for (std::size_t i = 0; i < other.bit; ++i) {
      write(static_cast<std::uint32_t>((other.bytes[i / 8u] >> (i % 8u)) & 1u), 1);
    }
  }

  // All-zero tail bits decode as net_NOP, so the stream stays valid and the
  // message loop's `remaining() > 6` guard cannot invent a message from padding.
  void padToByte() { while (bit % 8u != 0) write(0, 1); }
};

// ------------------------------------------------------------- summary setup

DemoNetworkSummary makeSummary(int networkProtocol = 24) {
  DemoNetworkSummary summary;
  summary.networkProtocol = networkProtocol;

  SendTableSchema table;
  table.name = "DT_TestEntity";
  SendPropSchema health;
  health.type = SendPropType::Int;
  health.name = "m_iHealth";
  health.ownerTable = "DT_TestEntity";
  health.bitCount = 8;
  health.flags = 1;  // signed
  SendPropSchema team;
  team.type = SendPropType::Int;
  team.name = "m_iTeamNum";
  team.ownerTable = "DT_TestEntity";
  team.bitCount = 8;
  team.flags = 1;
  table.flattenedProps = {health, team};
  table.props = table.flattenedProps;
  summary.sendTableSchemas.push_back(table);

  ServerClassSchema serverClass;
  serverClass.id = 0;
  serverClass.name = "CTestEntity";
  serverClass.dataTable = "DT_TestEntity";
  summary.serverClassSchemas.push_back(serverClass);
  summary.serverClassNames.push_back("CTestEntity");
  // One server class -> one class bit, matching Valve's Q_log2(count) + 1.
  summary.serverClassCount = 1;
  summary.dataTableServerClassCount = 1;
  return summary;
}

// --------------------------------------------------------------- message bits

constexpr std::uint32_t kSentinelTick = 4242u;

void writeSentinelTick(BitWriter& out) {
  out.write(3, 6);              // net_Tick
  out.write(kSentinelTick, 32);
  out.write(0, 16);             // frame time
  out.write(0, 16);             // standard deviation
}

struct RunOutcome {
  DemoMessageStreamResult result;
  bool ok = false;
};

RunOutcome run(DemoNetworkSummary& summary, BitWriter& stream) {
  stream.padToByte();
  RunOutcome outcome;
  outcome.ok = tf2::native::decodeDemoMessageStream(stream.bytes, stream.bit, 0, -1,
                                                    summary, outcome.result);
  return outcome;
}

// A stream whose last message is the sentinel: if every preceding layout was
// consumed exactly, the sentinel's tick is what the summary ends up holding.
bool sentinelReached(const DemoNetworkSummary& summary, const RunOutcome& outcome) {
  return outcome.ok && outcome.result.packetValid && !outcome.result.hitUnknownType &&
         summary.lastNetworkTick == static_cast<std::int32_t>(kSentinelTick);
}

// ------------------------------------------------------- svc_PacketEntities

void writePacketEntitiesHeader(BitWriter& out, std::uint32_t maxEntries, bool isDelta,
                               std::uint32_t deltaFrom, std::uint32_t updatedEntries,
                               std::uint32_t payloadBits) {
  out.write(maxEntries, 11);
  out.write(isDelta ? 1u : 0u, 1);
  if (isDelta) out.write(deltaFrom, 32);
  out.write(0, 1);              // baseline
  out.write(updatedEntries, 11);
  out.write(payloadBits, 20);
  out.write(0, 1);              // updateBaseline
}

void writeUpdate(BitWriter& out, std::uint32_t diff, std::uint32_t type) {
  out.writeBitVar(diff);
  out.write(type, 2);
}

void writeProp(BitWriter& out, std::uint32_t diff, std::int32_t value) {
  out.write(1, 1);              // hasProp
  out.writeBitVar(diff);
  out.writeSigned(value, 8);
}

void writePropListEnd(BitWriter& out) { out.write(0, 1); }

// Emits one whole svc_PacketEntities message: body first so the header can
// carry its exact bit length, which readPacketEntities requires.
void writePacketEntities(BitWriter& out, std::uint32_t maxEntries, bool isDelta,
                         std::uint32_t deltaFrom, std::uint32_t updatedEntries,
                         const BitWriter& body) {
  out.write(26, 6);             // svc_PacketEntities
  writePacketEntitiesHeader(out, maxEntries, isDelta, deltaFrom, updatedEntries,
                            static_cast<std::uint32_t>(body.bit));
  out.append(body);
}

// ------------------------------------------------------------------ fixtures

void fixtureMessageLayouts() {
  std::cout << "-- message layout fixtures\n";

  // net_File = 2: transfer_id(32) + null-terminated name + requested(1).
  {
    DemoNetworkSummary summary = makeSummary();
    BitWriter out;
    out.write(2, 6);
    out.write(0x12345678u, 32);
    out.writeString("models/test.mdl");
    out.write(1, 1);
    writeSentinelTick(out);
    const RunOutcome outcome = run(summary, out);
    check(sentinelReached(summary, outcome), "net_File: sentinel tick reached");
    check(summary.fileMessageCount == 1, "net_File: fileMessageCount == 1");
  }

  // svc_SetPause = 11: one bit. This is the message that used to truncate the
  // packet carrying the demo's next svc_PacketEntities.
  {
    DemoNetworkSummary summary = makeSummary();
    BitWriter out;
    out.write(11, 6);
    out.write(1, 1);
    writeSentinelTick(out);
    const RunOutcome outcome = run(summary, out);
    check(sentinelReached(summary, outcome), "svc_SetPause: sentinel tick reached");
    check(summary.setPauseCount == 1, "svc_SetPause: setPauseCount == 1");
    check(summary.setPauseState, "svc_SetPause: paused state latched");
  }

  // svc_BSPDecal = 21. Three shapes: all axes absent, one axis present with
  // both integer and fractional parts, and the entity/model index variant.
  {
    DemoNetworkSummary summary = makeSummary();
    BitWriter out;
    out.write(21, 6);
    out.write(0, 1); out.write(0, 1); out.write(0, 1);   // no coordinates
    out.write(7, 9);                                     // texture index
    out.write(0, 1);                                     // no ent/model
    out.write(1, 1);                                     // low priority
    writeSentinelTick(out);
    const RunOutcome outcome = run(summary, out);
    check(sentinelReached(summary, outcome), "svc_BSPDecal: no-coordinate form");
    check(summary.bspDecalCount == 1, "svc_BSPDecal: bspDecalCount == 1");
  }
  {
    DemoNetworkSummary summary = makeSummary();
    BitWriter out;
    out.write(21, 6);
    out.write(1, 1); out.write(0, 1); out.write(1, 1);   // x and z present
    // bf_read::ReadBitCoord, both parts present: has_int, has_frac, sign,
    // 14-bit integer, 5-bit fraction.
    out.write(1, 1); out.write(1, 1); out.write(0, 1); out.write(100, 14); out.write(3, 5);
    // has_int only: the fraction field is absent, so no 5 bits follow.
    out.write(1, 1); out.write(0, 1); out.write(0, 1); out.write(1, 14);
    out.write(511, 9);
    out.write(1, 1);                                     // has ent/model
    out.write(2047, 11);
    out.write(8191, 13);
    out.write(0, 1);
    writeSentinelTick(out);
    const RunOutcome outcome = run(summary, out);
    check(sentinelReached(summary, outcome), "svc_BSPDecal: bit-coord + ent/model form");
    check(summary.bspDecalCount == 1, "svc_BSPDecal: counted once");
  }
  {
    // Fraction-only coordinate: has_int absent, has_frac present, so sign(1)
    // and 5 fraction bits follow and the 14-bit integer field does not.
    DemoNetworkSummary summary = makeSummary();
    BitWriter out;
    out.write(21, 6);
    out.write(0, 1); out.write(1, 1); out.write(0, 1);
    out.write(0, 1); out.write(1, 1); out.write(1, 1); out.write(17, 5);
    out.write(0, 9);
    out.write(0, 1);
    out.write(0, 1);
    writeSentinelTick(out);
    const RunOutcome outcome = run(summary, out);
    check(sentinelReached(summary, outcome), "svc_BSPDecal: fraction-only coordinate form");
  }
  {
    // Mutation: claim the fraction field for a has_frac=0 coordinate. The extra
    // 5 bits must misalign the sentinel, which proves the fixture is sensitive
    // to the conditional-width rule rather than merely well-formed.
    DemoNetworkSummary summary = makeSummary();
    BitWriter out;
    out.write(21, 6);
    out.write(1, 1); out.write(0, 1); out.write(0, 1);
    out.write(1, 1); out.write(0, 1); out.write(0, 1); out.write(1, 14); out.write(0, 5);
    out.write(0, 9);
    out.write(0, 1);
    out.write(0, 1);
    writeSentinelTick(out);
    const RunOutcome outcome = run(summary, out);
    check(!sentinelReached(summary, outcome),
          "svc_BSPDecal: surplus fraction bits misalign the stream");
  }

  // svc_Menu = 29: kind(16) + byte length(16) + bytes.
  {
    DemoNetworkSummary summary = makeSummary();
    BitWriter out;
    out.write(29, 6);
    out.write(3, 16);
    out.write(5, 16);
    for (int i = 0; i < 5; ++i) out.write(static_cast<std::uint32_t>(0xa0 + i), 8);
    writeSentinelTick(out);
    const RunOutcome outcome = run(summary, out);
    check(sentinelReached(summary, outcome), "svc_Menu: sentinel tick reached");
    check(summary.menuCount == 1, "svc_Menu: menuCount == 1");
  }

  // svc_CmdKeyValues = 32: byte length(32) + bytes.
  {
    DemoNetworkSummary summary = makeSummary();
    BitWriter out;
    out.write(32, 6);
    out.write(4, 32);
    for (int i = 0; i < 4; ++i) out.write(0x5au, 8);
    writeSentinelTick(out);
    const RunOutcome outcome = run(summary, out);
    check(sentinelReached(summary, outcome), "svc_CmdKeyValues: sentinel tick reached");
    check(summary.cmdKeyValuesCount == 1, "svc_CmdKeyValues: cmdKeyValuesCount == 1");
  }

  // svc_Prefetch = 28. The index width is 14 bits from protocol 23 onward and
  // 13 before it; reading the wrong width shifts every later message.
  {
    DemoNetworkSummary summary = makeSummary(24);
    BitWriter out;
    out.write(28, 6);
    out.write(9000, 14);          // needs all 14 bits
    writeSentinelTick(out);
    const RunOutcome outcome = run(summary, out);
    check(sentinelReached(summary, outcome), "svc_Prefetch: protocol 24 uses 14 bits");
  }
  {
    DemoNetworkSummary summary = makeSummary(22);
    BitWriter out;
    out.write(28, 6);
    out.write(9000 & 0x1fffu, 13);  // needs all 13 bits
    writeSentinelTick(out);
    const RunOutcome outcome = run(summary, out);
    check(sentinelReached(summary, outcome), "svc_Prefetch: protocol 22 uses 13 bits");
  }
  {
    // Same bytes read as protocol 23 must misalign the sentinel. This is the
    // mutation that proves the width fixture can actually fail.
    DemoNetworkSummary summary = makeSummary(23);
    BitWriter out;
    out.write(28, 6);
    out.write(9000, 13);          // deliberately one bit short
    writeSentinelTick(out);
    const RunOutcome outcome = run(summary, out);
    check(!sentinelReached(summary, outcome), "svc_Prefetch: 13-bit stream misaligns protocol 23");
  }
}

void fixtureEntityUpdates() {
  std::cout << "-- svc_PacketEntities fixtures\n";

  const std::int32_t baseTick = 100;
  const std::int32_t preserveTick = 110;
  const std::int32_t leaveTick = 120;
  const std::int32_t deleteTick = 130;
  const std::int32_t afterDeleteTick = 140;
  const std::int32_t orphanDeltaTick = 150;
  const std::string healthKey = "DT_TestEntity.m_iHealth";
  const std::string teamKey = "DT_TestEntity.m_iTeamNum";

  DemoNetworkSummary summary = makeSummary();

  // Baseline frame: a non-delta packet that Enters entity 0 and writes both
  // properties. Nothing here depends on prior state.
  {
    BitWriter body;
    writeUpdate(body, 0, 2);       // diff 0 -> entity 0, Enter
    body.write(0, 1);              // class id 0 (one class bit)
    body.write(1, 10);             // serial
    writeProp(body, 0, 100);       // m_iHealth = 100
    writeProp(body, 0, 200);       // m_iTeamNum = 200
    writePropListEnd(body);

    BitWriter out;
    writePacketEntities(out, 16, false, 0, 1, body);
    BitWriter stream;
    stream.write(3, 6);
    stream.write(baseTick, 32);
    stream.write(0, 16);
    stream.write(0, 16);
    stream.append(out);
    writeSentinelTick(stream);

    const RunOutcome outcome = run(summary, stream);
    check(sentinelReached(summary, outcome), "baseline: sentinel tick reached");
    check(summary.packetEntityEnterCount == 1, "baseline: one Enter");
    check(summary.entityUnknownStateFailures == 0, "baseline: no entity failures");
    check(summary.packetEntitiesCount == 1, "baseline: one packet");
    check(summary.activeEntityCount == 1, "baseline: one active entity");
    check(summary.entityStates.size() > 0 && summary.entityStates[0].classId == 0,
          "baseline: entity 0 bound to class 0");
    check(summary.entityStates.size() > 0 &&
              summary.entityStates[0].properties[healthKey].intValue == 100,
          "baseline: m_iHealth == 100");
    check(summary.entityStates.size() > 0 &&
              summary.entityStates[0].properties[teamKey].intValue == 200,
          "baseline: m_iTeamNum == 200");
    check(summary.entityClassByIndex.size() > 0 && summary.entityClassByIndex[0] == 0,
          "baseline: entity -> class map populated");
  }

  // Delta frame that references the baseline by tick and Preserves entity 0,
  // changing only m_iHealth. The untouched property must survive: that is what
  // proves the base frame is applied rather than replaced.
  {
    BitWriter body;
    writeUpdate(body, 0, 0);       // Preserve entity 0
    writeProp(body, 0, 150);       // m_iHealth = 150
    writePropListEnd(body);
    body.write(0, 1);              // no removed entities

    BitWriter out;
    writePacketEntities(out, 16, true, static_cast<std::uint32_t>(baseTick), 1, body);
    BitWriter stream;
    stream.write(3, 6);
    stream.write(preserveTick, 32);
    stream.write(0, 16);
    stream.write(0, 16);
    stream.append(out);
    writeSentinelTick(stream);

    const RunOutcome outcome = run(summary, stream);
    check(sentinelReached(summary, outcome), "delta preserve: sentinel tick reached");
    check(summary.packetEntityPreserveCount == 1, "delta preserve: one Preserve");
    check(summary.entityUnknownStateFailures == 0, "delta preserve: no entity failures");
    check(summary.packetEntityDeltaBaseUnavailableCount == 0,
          "delta preserve: base frame found in history");
    check(summary.entityStates[0].properties[healthKey].intValue == 150,
          "delta preserve: m_iHealth updated to 150");
    check(summary.entityStates[0].properties[teamKey].intValue == 200,
          "delta preserve: untouched m_iTeamNum kept at 200");

    // The history record for a Preserve must be a delta, and replaying it must
    // reproduce the same state. This is the check on EntityHistoryEvent.
    bool foundPreserveEvent = false;
    for (const auto& event : summary.entityHistoryEvents) {
      if (event.removed || event.fullState || event.entityIndex != 0) continue;
      if (event.tick != preserveTick) continue;
      foundPreserveEvent = true;
      check(event.changes.size() == 1, "history: preserve event carries one property delta");
      check(event.changes.size() == 1 && event.changes[0].propIndex == 0,
            "history: delta addresses flattened prop 0");
    }
    check(foundPreserveEvent, "history: preserve event recorded");

    std::vector<tf2::native::EntityState> states;
    const auto status = tf2::native::queryEntitySnapshotAtOrBeforeTick(summary, preserveTick, states);
    check(status == tf2::native::EntitySnapshotQueryStatus::Available,
          "replay: snapshot status Available");
    check(states.size() > 0 && states[0].properties[healthKey].intValue == 150,
          "replay: m_iHealth == 150");
    check(states.size() > 0 && states[0].properties[teamKey].intValue == 200,
          "replay: untouched m_iTeamNum == 200 after delta replay");
  }

  // Leave: the entity leaves the PVS. Class and last state must be retained.
  {
    BitWriter body;
    writeUpdate(body, 0, 1);       // Leave entity 0
    body.write(0, 1);              // no removed entities

    BitWriter out;
    writePacketEntities(out, 16, true, static_cast<std::uint32_t>(preserveTick), 1, body);
    BitWriter stream;
    stream.write(3, 6);
    stream.write(leaveTick, 32);
    stream.write(0, 16);
    stream.write(0, 16);
    stream.append(out);
    writeSentinelTick(stream);

    const RunOutcome outcome = run(summary, stream);
    check(sentinelReached(summary, outcome), "delta leave: sentinel tick reached");
    check(summary.packetEntityLeaveCount == 1, "delta leave: one Leave");
    check(summary.entityUnknownStateFailures == 0, "delta leave: no entity failures");
    check(summary.entityClassByIndex[0] == 0, "delta leave: class retained");
    check(summary.entityStates[0].properties[healthKey].intValue == 150,
          "delta leave: last state retained");
  }

  // Delete: the entity is destroyed and its slot must be cleared.
  {
    BitWriter body;
    writeUpdate(body, 0, 3);       // Delete entity 0, no payload
    body.write(0, 1);              // no removed entities

    BitWriter out;
    writePacketEntities(out, 16, true, static_cast<std::uint32_t>(leaveTick), 1, body);
    BitWriter stream;
    stream.write(3, 6);
    stream.write(deleteTick, 32);
    stream.write(0, 16);
    stream.write(0, 16);
    stream.append(out);
    writeSentinelTick(stream);

    const RunOutcome outcome = run(summary, stream);
    check(sentinelReached(summary, outcome), "delta delete: sentinel tick reached");
    check(summary.packetEntityDeleteCount == 1, "delta delete: one Delete");
    check(summary.entityUnknownStateFailures == 0, "delta delete: no entity failures");
    check(summary.entityClassByIndex[0] == -1, "delta delete: class cleared");
    check(summary.activeEntityCount == 0, "delta delete: active entity count back to 0");
  }

  // A Preserve on a deleted entity has no base to stand on. The decoder must
  // count it as a failure; substituting the most recent frame would hide it.
  {
    BitWriter body;
    writeUpdate(body, 0, 0);       // Preserve entity 0
    writeProp(body, 0, 999);
    writePropListEnd(body);
    body.write(0, 1);

    BitWriter out;
    writePacketEntities(out, 16, true, static_cast<std::uint32_t>(deleteTick), 1, body);
    BitWriter stream;
    stream.write(3, 6);
    stream.write(afterDeleteTick, 32);
    stream.write(0, 16);
    stream.write(0, 16);
    stream.append(out);
    writeSentinelTick(stream);

    const RunOutcome outcome = run(summary, stream);
    check(sentinelReached(summary, outcome), "preserve-after-delete: sentinel tick reached");
    check(summary.entityUnknownStateFailures == 1,
          "preserve-after-delete: counted as an entity failure");
    check(summary.packetEntityDecodeFailures == 1,
          "preserve-after-delete: counted as a decode failure");
    check(summary.firstEntityUnknownStateTick == afterDeleteTick,
          "preserve-after-delete: first failure records the tick");
    check(summary.entityClassByIndex[0] == -1,
          "preserve-after-delete: no fabricated class binding");
  }

  // A delta naming a base frame that is not in the history must be reported as
  // such. It must not be silently rebased onto a nearby frame.
  {
    DemoNetworkSummary orphan = makeSummary();
    BitWriter body;
    writeUpdate(body, 0, 0);       // Preserve entity 0
    writeProp(body, 0, 5);
    writePropListEnd(body);
    body.write(0, 1);

    BitWriter out;
    writePacketEntities(out, 16, true, 999999u, 1, body);
    BitWriter stream;
    stream.write(3, 6);
    stream.write(orphanDeltaTick, 32);
    stream.write(0, 16);
    stream.write(0, 16);
    stream.append(out);
    writeSentinelTick(stream);

    const RunOutcome outcome = run(orphan, stream);
    check(sentinelReached(orphan, outcome), "orphan delta: sentinel tick reached");
    check(orphan.packetEntityDeltaBaseUnavailableCount == 1,
          "orphan delta: missing base frame reported");
    check(orphan.firstPacketEntitiesUnavailableTick == orphanDeltaTick,
          "orphan delta: first missing base records the tick");
    check(orphan.packetEntityDeltaCount == 1, "orphan delta: counted as a delta packet");
  }
}

}  // namespace

int main() {
  std::cout << "entity_message_fixture_probe\n";
  fixtureMessageLayouts();
  fixtureEntityUpdates();
  std::cout << "fixture_failures=" << g_failures << '\n';
  return g_failures == 0 ? 0 : 1;
}
