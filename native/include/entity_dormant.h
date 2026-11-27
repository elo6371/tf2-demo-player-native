#pragma once

// ============================================================================
// CA-01: PVS DORMANCY AS A VIEW FACT
//
// THE DEFECT THIS REMOVES. When a client-side entity stops transmitting, the
// product keeps drawing it from its last state, so on a POV recording an enemy
// who walks behind a wall stays standing where he left -- the ghost named in
// CODE-AUDIT-2026-11-26.md section 1, CA-01. On STV recordings with
// tv_transmitall the server keeps transmitting, so the defect is invisible
// there; it is a POV-only wrong picture, not a parse error.
//
// WHAT THE WIRE ACTUALLY SAYS. Source's client marks an entity dormant on the
// should-transmit END edge: engine/cl_ents_parse.cpp:530-535 turns a
// PacketEntities Leave into NotifyShouldTransmit(SHOULDTRANSMIT_END), which
// sets m_bDormant = true (game/client/c_baseentity.cpp:2093-2104), and the draw
// test is ShouldDraw() && !IsDormant() (game/client/c_baseentity.cpp:1399).
// On the demo side that edge is exactly updateType == 1 (FHDR_LEAVEPVS without
// FHDR_DELETE) in svc_PacketEntities (header semantics:
// engine/baseclientstate.cpp:243-275, :199-233; Leave handler:
// engine/client.cpp:1342-1362). So dormancy is NOT a demo-header field and NOT
// "the entity was missing from this packet":
//   - TF2 does not transmit m_bDormant to clients, so no header flag exists;
//   - an unchanged entity omitted from a delta packet is PreserveEnt
//     (engine/baseclientstate.cpp:204-215, engine/client.cpp:1372-1407), not a
//     PVS exit;
//   - only an explicit Leave (updateType == 1) says "stop transmitting".
//
// WHAT DOWNSTREAM MUST READ WHILE DORMANT. The last known state/placement stays
// frozen in EntityState (Leave retains class and state rather than deleting or
// zeroing to the world origin (0,0,0)). The entity is not drawn
// (entityDormantShouldDraw returns false). Its animation clock does not
// advance. Once CA-02a/CA-02b land, kEntityTransformDormant carries the same
// bit into EntityTransformSample so interpolation holds on the last point
// (reason=dormant) and a returning entity starts a fresh generation on Enter.
//
// RULE 10 / D-41 TRACE PLACEMENT. Per MASTER-TASKLIST section 2.7 /
// HANDOFF-COMPLETE-20261127 section 3.2 rule 10, new trace formatting lives in
// anim_trace.cpp (not here and not in main.cpp). See WIRING-SPEC.md.
// ============================================================================

#include "demo_header.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace tf2::native {

struct EntityDormantLimits {
  bool enabled = false;  // --entity-dormant on|off, default off
  bool trace = false;    // emit dormant: / draw: skip=dormant lines in anim_trace.cpp
};

// Counters the CA-01 gate reads from DemoNetworkSummary (works standalone on
// 054fb628 without requiring CA-02a's EntityTransformSample track).
struct EntityDormantStats {
  std::size_t packetLeaves = 0;   // summary.packetEntityLeaveCount
  std::size_t departures = 0;     // Leave -> dormant transitions recorded
  std::size_t returns = 0;        // dormant -> resident (Preserve/Enter) transitions
  std::size_t activeDormant = 0;  // slots currently marked dormant in entityDormant
  // True when the decoder counted Leave updates (packetLeaves > 0) while the
  // residency hook recorded zero departures. That is what a deleted
  // markEntityDormantDeparted() call looks like, turning the mutation gate red.
  bool wiringSuspect = false;
};

EntityDormantStats summarizeEntityDormant(const DemoNetworkSummary& summary);

// THE DRAW GATE. Dormant entities are not drawn (c_baseentity.cpp:1399). An
// index the bitmap does not describe fails OPEN (returns true): the bitmap is
// populated by readPacketEntities / EntityHistoryCheckpoint, and a missing
// entry means "no dormancy state recorded for this slot", which must not
// silently hide an entity.
bool entityDormantShouldDraw(const std::vector<std::uint8_t>& dormantByIndex,
                             std::size_t entityIndex);

// Per-tick residency bitmap snapshot for callers keeping a timeline outside
// EntityHistoryCheckpoint. One byte per slot (1 = dormant, 0 = resident).
struct EntityDormantSnapshot {
  std::int32_t tick = -1;
  std::vector<std::uint8_t> dormantByIndex;
};

EntityDormantSnapshot captureEntityDormantSnapshot(const DemoNetworkSummary& summary,
                                                   std::int32_t tick);

// Dormancy standing at `tick`, from snapshots in non-decreasing tick order.
// Returns false (resident) when no snapshot covers the tick (fail open).
bool entityDormantAtTick(const std::vector<EntityDormantSnapshot>& snapshots,
                         std::size_t entityIndex, std::int32_t tick);

// Frozen placement of an entity from a snapshot state vector (e.g., returned by
// queryEntitySnapshotAtOrBeforeTick). Leave keeps EntityState intact rather than
// zeroing origin/angles, so this never invents (0,0,0).
struct EntityDormantPlacement {
  bool valid = false;
  std::uint16_t entityIndex = 0;
  bool hasOrigin = false;
  bool hasAngles = false;
  float origin[3] = {0.0f, 0.0f, 0.0f};
  float angles[3] = {0.0f, 0.0f, 0.0f};
};

EntityDormantPlacement resolveEntityDormantStatePlacement(
    const std::vector<EntityState>& states, std::size_t entityIndex);

}  // namespace tf2::native
