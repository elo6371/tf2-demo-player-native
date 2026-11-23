// A2 (2026-11-23, docs/UI-SHELL-DESIGN-2026-11-22.md section 9.4): the three
// anomaly classes the startup analysis reports. A1's census (demo_tick_audit.h)
// records the distributions and deliberately decides nothing; this turns them into
// intervals a result list can show, and every threshold below is justified by that
// census rather than by a guess about what a "normal" step looks like
// (_shared/a1-motion-census-2026-11-23/SUMMARY.txt is the data).
//
// It is a pure function of the rows the census already keeps, so it can run over a
// recording that has already been scanned and cannot disagree with the numbers the
// census printed.
#ifndef TF2_NATIVE_DEMO_MOTION_DETECT_H
#define TF2_NATIVE_DEMO_MOTION_DETECT_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "demo_tick_audit.h"

namespace tf2::native {

struct MotionDetectThresholds {
  // A gap above stallGapTicks server ticks between two data packets opens a stall;
  // it is reported once it lasts stallMinTicks. The floor is 3 because every clean
  // STV recording in the census has p99 = 1 (a POV recorder emits several packets
  // per tick and reaches p99 = 2, which is why the floor must stay above 2), and the
  // 16-tick minimum is ~250 ms at 60.5 Hz -- the point where a viewer reads the
  // picture as stopped rather than stuttering. A per-recording floor is the better
  // rule in principle (a multiple of that recording's own p99) and is what the A1
  // README asks for; it collapses to these two numbers on every recording measured
  // so far, so the constant form is what ships until a recording needs otherwise.
  std::int32_t stallGapTicks = 3;
  std::int32_t stallMinTicks = 16;
  // 1100 units/s (the fastest legitimate player speed) is 16.5 units per tick at
  // 66.7 tick/s, so 120 units in one tick is already seven times a rocket-jump tick
  // and no jump, sticky, tunnel or roll can produce it. A single tick that large is
  // still reported, but two consecutive ticks at the floor (240 units) is what
  // separates a teleport from one bad snapshot correction.
  double teleportUnitsPerTick = 120.0;
  double teleportMinSumUnits = 240.0;
  // More than this many entities jumping on the SAME tick is a round transition or
  // an entity-slot reuse, not a player teleporting: the census's 6547-unit maximum
  // (bagel tick 80642) is exactly that, with dozens of entities moving together.
  // Candidates on such a tick are demoted, never reported.
  // A2 measured this on the four census recordings: with the demotion at >5 the
  // recordings still reported 417 / 703 / 1171 teleports (bagel / upward / cp_process
  // POV), and three of them land on the SAME tick (bagel 56481 has entities 7, 12 and
  // 13 jumping 2626-4031 units at once) -- that is a respawn wave, not three players
  // warping. >2 is the value that demotes those while leaving a lone player's jump.
  std::size_t teleportSceneResetEntities = 2;
  // Respawns and network warps are indistinguishable from the entity stream alone:
  // a respawn moves the player to a spawn room with no change the census keeps, and
  // the recordings have hundreds of them (bagel 417, upward 703, cp_process 1171
  // candidates above the per-tick floor). The report therefore keeps the LARGEST
  // teleportMaxReported of them and records how many were left out, so the counts
  // stay exact while the list stays something a person can review. A3 filters the
  // retained ones against the demo's player_death events, which is what separates a
  // respawn from a warp; that correlation is not available here.
  std::size_t teleportMaxReported = 32;
  // A flick shot is a sharp turn, and a sharp turn is what a viewer is watching for
  // when the shot lands. It is a HINT -- drawn on the turn-rate curve, never a red
  // entry in the anomaly list -- because the census cannot tell a flick from a snap
  // of the same magnitude.
  double sharpTurnDegPerTick = 135.0;
};

enum class MotionAnomalyKind { Stall = 0, Teleport = 1, SharpTurn = 2 };

// A stall carries entity = -1 and metric = its duration in server ticks; a teleport
// carries the entity and metric = the units accumulated over the run; a sharp turn
// is a single tick with metric = degrees in that tick.
struct MotionAnomaly {
  MotionAnomalyKind kind = MotionAnomalyKind::Stall;
  std::int32_t startTick = 0;
  std::int32_t endTick = 0;
  std::int32_t durationTicks = 0;
  std::int32_t entity = -1;
  double metric = 0.0;
};

const char* motionAnomalyKindName(MotionAnomalyKind kind);

struct MotionDetectReport {
  MotionDetectThresholds thresholds;
  std::vector<MotionAnomaly> anomalies;  // stalls and teleports, ordered by start tick
  std::vector<MotionAnomaly> hints;      // sharp turns, ordered by tick
  std::size_t dataPackets = 0;
  std::size_t stalls = 0;
  std::size_t teleports = 0;
  std::size_t sharpTurns = 0;
  std::size_t sceneResets = 0;      // ticks demoted because too many entities jumped
  std::size_t teleportCandidates = 0;  // steps above the per-tick floor, before grouping
  std::size_t teleportsOmitted = 0;    // detected, not reported (see teleportMaxReported)
};

MotionDetectReport detectMotionAnomalies(const std::vector<DemoTickAuditPacketRow>& packets,
    const DemoMotionCensus& census, const MotionDetectThresholds& thresholds = {});

// One line per header/anomaly/hint, ASCII, stable field order -- the same contract
// the other --scan emitters keep so a gate can grep them.
std::vector<std::string> formatMotionDetectLines(const MotionDetectReport& report);

}  // namespace tf2::native

#endif  // TF2_NATIVE_DEMO_MOTION_DETECT_H