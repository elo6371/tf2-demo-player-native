#include "demo_header.h"

#include <cmath>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

bool near(float value, float expected) {
  return std::isfinite(value) && std::fabs(value - expected) < 0.001f;
}

void putFloat(std::uint8_t* bytes, std::size_t offset, float value) {
  std::memcpy(bytes + offset, &value, sizeof(value));
}

bool checkViewMath() {
  if (!near(tf2::native::demoBitAngleToDegrees(0, 16), 0.0f)) return false;
  if (!near(tf2::native::demoBitAngleToDegrees(32768u, 16), 180.0f)) return false;
  if (!near(tf2::native::demoBitAngleToDegrees(16384u, 16), 90.0f)) return false;
  float forward[3] = {};
  tf2::native::demoViewForward(0.0f, 0.0f, forward);
  if (!near(forward[0], 1.0f) || !near(forward[1], 0.0f) || !near(forward[2], 0.0f)) return false;
  tf2::native::demoViewForward(0.0f, 90.0f, forward);
  if (!near(forward[0], 0.0f) || !near(forward[1], 1.0f) || !near(forward[2], 0.0f)) return false;
  tf2::native::demoViewForward(90.0f, 0.0f, forward);
  if (!near(forward[0], 0.0f) || !near(forward[1], 0.0f) || !near(forward[2], -1.0f)) return false;
  std::uint8_t cmdInfo[76] = {};
  putFloat(cmdInfo, 4, 10.0f); putFloat(cmdInfo, 8, 20.0f); putFloat(cmdInfo, 12, 30.0f);
  putFloat(cmdInfo, 16, 4.0f); putFloat(cmdInfo, 20, 90.0f); putFloat(cmdInfo, 24, 0.0f);
  tf2::native::DemoViewSample sample;
  if (!tf2::native::parseDemoCmdInfo(cmdInfo, sizeof(cmdInfo), sample)) return false;
  if (!sample.hasOrigin || !sample.hasAngles || sample.source != tf2::native::DemoViewSource::CmdInfo) return false;
  if (!near(sample.origin[0], 10.0f) || !near(sample.origin[2], 30.0f) || !near(sample.angles[1], 90.0f)) return false;
  tf2::native::DemoViewSample rejected;
  if (tf2::native::parseDemoCmdInfo(cmdInfo, 8, rejected)) return false;
  tf2::native::DemoNetworkSummary summary;
  sample.tick = 10;
  summary.viewSamples.push_back(sample);
  tf2::native::DemoViewSample later = sample;
  later.tick = 30;
  later.origin[0] = 40.0f;
  summary.viewSamples.push_back(later);
  tf2::native::DemoViewSample found;
  if (tf2::native::findObserverViewAtOrBeforeTick(summary, 5, found)) return false;
  if (!tf2::native::findObserverViewAtOrBeforeTick(summary, 20, found) || !near(found.origin[0], 10.0f)) return false;
  if (!tf2::native::findObserverViewAtOrBeforeTick(summary, 30, found) || !near(found.origin[0], 40.0f)) return false;
  return true;
}

bool checkRecording() {
  tf2::native::DemoHeader header;
  tf2::native::DemoNetworkSummary summary;
  const auto unknown = tf2::native::classifyDemoRecording(header, summary);
  if (unknown.kind != tf2::native::DemoRecordingKind::Unknown) return false;
  header.clientName = "player";
  header.recordingType = tf2::native::DemoRecordingType::PovHeuristic;
  const auto heuristic = tf2::native::classifyDemoRecording(header, summary);
  if (heuristic.kind != tf2::native::DemoRecordingKind::Pov || std::string(heuristic.label) != "POV (heuristic)") return false;
  summary.serverInfoCount = 1;
  const auto pov = tf2::native::classifyDemoRecording(header, summary);
  if (pov.kind != tf2::native::DemoRecordingKind::Pov || std::string(pov.label) != "POV") return false;
  summary.serverInfoHltv = true;
  const auto fromBit = tf2::native::classifyDemoRecording(header, summary);
  if (fromBit.kind != tf2::native::DemoRecordingKind::SourceTv || !fromBit.serverInfoHltv) return false;
  summary.serverInfoHltv = false;
  header.recordingType = tf2::native::DemoRecordingType::SourceTv;
  const auto fromName = tf2::native::classifyDemoRecording(header, summary);
  if (fromName.kind != tf2::native::DemoRecordingKind::SourceTv || !fromName.headerName) return false;

  // Regression (found 2026-10-06): every real SourceTV demo in the corpus has an
  // ordinary server hostname and puts the recorder's name in clientname. A
  // classifier that reads only servername calls all of them POV -- which is what
  // the P0 acceptance run reported for bagel/snakewater/ashville/comp/saytext2.
  // These two cases are the exact header pairs taken from those files.
  tf2::native::DemoNetworkSummary noStream;
  tf2::native::DemoHeader stvByName;
  stvByName.serverName = "Matcha Bookable";
  stvByName.clientName = "SourceTV Demo";
  const auto fromClientName = tf2::native::classifyDemoRecording(stvByName, noStream);
  if (fromClientName.kind != tf2::native::DemoRecordingKind::SourceTv) return false;
  if (!fromClientName.headerName) return false;
  if (std::string(fromClientName.label) != "SourceTV") return false;

  // Negative control: an ordinary POV header must still read POV, or the check
  // above would pass for the wrong reason.
  tf2::native::DemoHeader povByName;
  povByName.serverName = "169.254.129.46:10120";
  povByName.clientName = "Icewind | demos.tf";
  const auto povFromName = tf2::native::classifyDemoRecording(povByName, noStream);
  return povFromName.kind == tf2::native::DemoRecordingKind::Pov;
}

tf2::native::EntityPropertyValue vectorValue(float x, float y, float z) {
  tf2::native::EntityPropertyValue value;
  value.type = tf2::native::SendPropType::Vector;
  value.x = x; value.y = y; value.z = z;
  return value;
}

bool checkProjectile() {
  tf2::native::TempEntityEvent event;
  event.tick = 15;
  event.className = "CTEClientProjectile";
  event.properties["DT_TEClientProjectile.m_vecOrigin"] = vectorValue(1.0f, 2.0f, 3.0f);
  event.properties["m_vecVelocity"] = vectorValue(4.0f, 0.0f, -1.0f);
  tf2::native::EntityPropertyValue model;
  model.type = tf2::native::SendPropType::Int;
  model.intValue = 7;
  event.properties["m_nModelIndex"] = model;
  tf2::native::EntityPropertyValue owner = model;
  owner.intValue = 4;
  event.properties["m_hOwner"] = owner;
  tf2::native::EntityPropertyValue life = model;
  life.intValue = 12;
  event.properties["m_nLifeTime"] = life;
  tf2::native::applyTempEntityFields(event);
  tf2::native::ProjectileTimelineEvent timeline;
  if (!tf2::native::projectileFromTempEntity(event, timeline)) return false;
  if (!timeline.hasOrigin || !near(timeline.origin[0], 1.0f) || !near(timeline.origin[2], 3.0f)) return false;
  if (!timeline.hasVelocity || !near(timeline.velocity[0], 4.0f) || !near(timeline.velocity[2], -1.0f)) return false;
  if (!timeline.hasModelIndex || timeline.modelIndex != 7 || !timeline.hasOwner || timeline.owner != 4) return false;
  if (!timeline.hasLifeTime || timeline.lifeTime != 12) return false;
  event.properties["m_vecOrigin"] = vectorValue(std::nanf(""), 0.0f, 0.0f);
  tf2::native::applyTempEntityFields(event);
  if (event.hasOrigin || !tf2::native::projectileFromTempEntity(event, timeline) || timeline.hasOrigin) return false;
  event.className = "CTESprite";
  if (tf2::native::projectileFromTempEntity(event, timeline)) return false;
  return true;
}

void pushHistory(tf2::native::DemoNetworkSummary& summary, std::int32_t tick) {
  if (summary.entityStates.size() < 2) summary.entityStates.resize(2);
  summary.entityStates[1].classId = tick;
  tf2::native::EntityHistoryEvent event;
  event.tick = tick;
  event.entityIndex = 1;
  event.classId = tick;
  // This fixture encodes the tick in the state itself, so it must be replayed
  // as a whole-state event rather than a property delta.
  event.fullState = true;
  event.state.classId = tick;
  tf2::native::appendEntityHistoryPacket(summary, tick, false, -1, {event});
}

bool checkSeek() {
  tf2::native::DemoNetworkSummary summary;
  summary.entityHistoryLimits.maxEvents = 4;
  summary.entityHistoryLimits.maxCheckpoints = 2;
  summary.entityHistoryLimits.checkpointStride = 2;
  summary.entityHistoryLimits.archiveMax = 8;
  for (const std::int32_t tick : {10, 20, 30, 40, 50, 60}) pushHistory(summary, tick);
  if (summary.entityHistoryArchive.empty()) return false;
  std::vector<tf2::native::EntityState> states;
  const auto early = tf2::native::queryEntitySnapshotAtOrBeforeTick(summary, 10, states);
  if (early != tf2::native::EntitySnapshotQueryStatus::Checkpoint || states.size() < 2 || states[1].classId != 10) return false;
  const auto between = tf2::native::queryEntitySnapshotAtOrBeforeTick(summary, 20, states);
  if (between != tf2::native::EntitySnapshotQueryStatus::Checkpoint || states[1].classId != 10) return false;
  const auto mid = tf2::native::queryEntitySnapshotAtOrBeforeTick(summary, 40, states);
  if (mid != tf2::native::EntitySnapshotQueryStatus::Checkpoint || states[1].classId != 30) return false;
  const auto live = tf2::native::queryEntitySnapshotAtOrBeforeTick(summary, 60, states);
  if (live != tf2::native::EntitySnapshotQueryStatus::Available || states[1].classId != 60) return false;
  const auto before = tf2::native::queryEntitySnapshotAtOrBeforeTick(summary, 5, states);
  if (before != tf2::native::EntitySnapshotQueryStatus::TickBeforeHistory) return false;
  summary.entityHistoryHasGap = true;
  const auto gap = tf2::native::queryEntitySnapshotAtOrBeforeTick(summary, 60, states);
  if (gap != tf2::native::EntitySnapshotQueryStatus::Gap) return false;
  tf2::native::DemoNetworkSummary empty;
  if (tf2::native::queryEntitySnapshotAtOrBeforeTick(empty, 0, states) != tf2::native::EntitySnapshotQueryStatus::NoHistory) return false;
  return true;
}

// The retention policy has to hold the worst staleness near the bound its slot
// budget implies, and it has to do it on a *uniform* supply: the defect it
// replaces (bagel: a 54392-tick hole at 57829..112221, 13.7 minutes) reproduced
// with a supply that emitted one checkpoint every 128 ticks with no variation at
// all, so a fixture with lopsided activity would be testing the data instead of
// the policy.
//
// Asserted bound: `kept` checkpoints spanning `span` ticks cannot do better than
// span / (kept - 1) (pigeonhole), so "worst gap <= 2x that floor" is a real
// statement about the policy rather than about the input. The rule this replaces
// misses it by 6.5x; the retained-gap-first rule meets it at 1.1x.
bool checkHistoryCoverage(std::int32_t& worstGap, std::int32_t& slotFloor) {
  tf2::native::DemoNetworkSummary summary;
  summary.entityHistoryLimits.maxEvents = 4;       // flush on accumulated updates
  summary.entityHistoryLimits.maxCheckpoints = 4;  // ... in blocks of four
  summary.entityHistoryLimits.checkpointStride = 1;
  summary.entityHistoryLimits.archiveMax = 8;
  constexpr std::int32_t kStride = 128;
  constexpr std::size_t kPackets = 200;
  for (std::size_t i = 0; i < kPackets; ++i) {
    tf2::native::EntityHistoryEvent event;
    event.tick = static_cast<std::int32_t>(i) * kStride;
    event.entityIndex = 1;
    event.fullState = true;
    tf2::native::appendEntityHistoryPacket(summary, event.tick, false, -1, {event});
  }
  const auto& archive = summary.entityHistoryArchive;
  if (archive.size() < 2) return false;
  std::size_t distinct = 1;
  worstGap = 0;
  for (std::size_t i = 1; i < archive.size(); ++i) {
    worstGap = std::max(worstGap, archive[i].tick - archive[i - 1].tick);
    if (archive[i].tick != archive[i - 1].tick) ++distinct;
  }
  const std::int32_t span = archive.back().tick - archive.front().tick;
  slotFloor = span / static_cast<std::int32_t>(archive.size() - 1);
  // Every slot must hold a distinct tick: the pre-fix rule pushed two checkpoints
  // per flush at the same tick (bagel: 56489 56489, 124508 124508, ...), spending
  // half the budget on copies of a tick that was already represented.
  const bool budgetSpent = archive.size() == summary.entityHistoryLimits.archiveMax
      && distinct == archive.size();
  const bool bounded = slotFloor > 0 && worstGap <= 2 * slotFloor;
  // A Checkpoint answer must report the tick it came from. Without that the
  // 13.7-minute case was indistinguishable from a fresh hit: same status, same
  // renderer call.
  std::vector<tf2::native::EntityState> states;
  const std::int32_t probeTick = kStride * 150;
  std::int32_t resolved = -1;
  const auto status = tf2::native::queryEntitySnapshotAtOrBeforeTick(
      summary, probeTick, states, &resolved);
  const bool reportsAge = status == tf2::native::EntitySnapshotQueryStatus::Checkpoint
      && resolved >= 0 && resolved <= probeTick && probeTick - resolved <= worstGap;
  return budgetSpent && bounded && reportsAge;
}

int main() {
  const bool view = checkViewMath();
  const bool recording = checkRecording();
  const bool projectile = checkProjectile();
  const bool seek = checkSeek();
  std::int32_t worstGap = 0;
  std::int32_t slotFloor = 0;
  const bool coverage = checkHistoryCoverage(worstGap, slotFloor);
  const bool ok = view && recording && projectile && seek && coverage;
  std::cout << "{\"selfTest\":" << (ok ? "true" : "false")
    << ",\"viewMath\":" << (view ? "true" : "false")
    << ",\"recording\":" << (recording ? "true" : "false")
    << ",\"projectileFields\":" << (projectile ? "true" : "false")
    << ",\"fullSpanCheckpoint\":" << (seek ? "true" : "false")
    << ",\"historyCoverage\":" << (coverage ? "true" : "false")
    << ",\"historyWorstGap\":" << worstGap
    << ",\"historySlotFloor\":" << slotFloor
    << ",\"cameraFormula\":\"source-pitch-down\""
    << ",\"seekOutsideWindow\":\"checkpoint\"}\n";
  return ok ? 0 : 1;
}
