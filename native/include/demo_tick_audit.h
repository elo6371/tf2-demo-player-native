#pragma once

#include <map>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "demo_header.h"

// task-35 (2026-11-21): anomaly tick audit.
//
// Two layers, both diagnostic only:
//   1. FILE LAYER -- the demo's own clock bookkeeping: per-packet demo tick
//      monotonicity, the packet in/out sequence numbers, and the drift between
//      the server clock (svc_NetTick) and the recording clock. A recording that
//      was clipped or spliced at the packet level shows up here as a backwards
//      tick, a backwards sequence number, or a clock offset step.
//   2. PLAYER LAYER -- "tick shift"/doubletap: more primary-attack events than a
//      weapon's fire interval allows, measured on the SERVER clock so a client
//      stall (which freezes the demo clock) cannot excuse it.
//
// This is an independent C++17 re-derivation from published behaviour and TF2
// send-table facts; no code was copied from the GPL-3.0 reference analyser (see
// docs/TICK-AUDIT-2026-11-21.md for the read-only record). The thresholds are
// ours: the reference package ships no file-layer check at all, and its player
// layer has no severity, no episode merging and no multi-player fuse.
namespace tf2::native {

enum class DemoTickAuditLevel : int { None = 0, Info = 1, Warning = 2, Edited = 3 };

const char* demoTickAuditLevelName(DemoTickAuditLevel level);

struct DemoTickAuditThresholds {
  // FILE LAYER.
  //
  // The demo clock and the server clock drift by one tick per dropped packet,
  // so a raw netTick-entryTick offset can never be compared against a constant.
  // The window size is what makes the comparison possible: over 2000 ticks a
  // healthy recording's offset stays within a couple of ticks of the window
  // median, while a spliced segment moves it by the number of ticks that were
  // removed. 66 ticks is one second at the demo's 66.67 tick rate -- large
  // enough that ordinary jitter cannot trip it, small enough that a one-second
  // cut does.
  std::size_t clockWindowTicks = 2000;
  std::int32_t clockJumpTicks = 66;
  // The first packets carry connection-time server ticks while the demo clock
  // starts at 0, so the offset is only meaningful after both clocks are running.
  std::size_t warmupTicks = 200;
  std::size_t maxIssues = 256;
  double tickRate = 66.6666667;

  // PLAYER LAYER.
  bool playerEnabled = true;
  // A weapon change within this many ticks of a shot makes the previous shot's
  // interval meaningless (the game re-arms the clip), so the shot is skipped.
  std::int32_t playerSwitchGuardTicks = 3;
  // The lag excuse looks this far back for a simulation-time catch-up step.
  std::int32_t simHistoryTicks = 30;
  std::int32_t maxFloorRatioPercent = 80;
  std::int32_t sameFrameRatioPercent = 50;
  // A fire interval can only be called impossible when the slack-adjusted
  // threshold is at least this many ticks; fast weapons have no margin.
  std::int32_t shortIntervalMinThreshold = 6;
  std::int32_t toleranceTicks = 0;
  // Minigun's rate check is off by default: the spin-up makes a per-shot
  // interval meaningless and the 66-tick window rule is a heuristic.
  bool minigunRateEnabled = false;
  std::size_t minigunWindowTicks = 66;
  std::int32_t minigunMaxEvents = 30;
  // Ours, not the reference's: when more than this many distinct players trip
  // in the same window, the per-player verdict is demoted instead of pointing
  // at a dozen names (a server-side condition, not a dozen cheaters).
  std::size_t fusePlayers = 12;
  std::size_t fuseWindowTicks = 2000;
};

struct DemoTickAuditIssue {
  DemoTickAuditLevel level = DemoTickAuditLevel::Info;
  std::string code;
  std::int32_t entryTick = 0;   // demo (playback) tick the issue belongs to
  std::int32_t serverTick = -1; // server (net_Tick) tick when known
  std::size_t packet = 0;       // ordinal of the packet in the demo index
  std::string detail;
};

// One player-layer detection, kept so a report can name the shot pair rather
// than only a count.
struct DemoTickAuditHit {
  std::int32_t serverTick = 0;
  std::int32_t entryTick = 0;
  std::int32_t playerEntity = -1;
  std::int32_t weaponEntity = -1;
  std::string weaponClass;
  std::int32_t itemDefinitionIndex = -1;
  std::string kind;   // "same-frame" or "short-interval"
  int weight = 0;
  std::int32_t intervalTicks = 0;
  std::int32_t thresholdTicks = 0;
  std::int32_t floorTicks = 0;
  std::int32_t shotsInFrame = 1;
};

struct DemoTickAuditPlayerSummary {
  std::int32_t playerEntity = -1;
  std::size_t hits = 0;
  int weight = 0;
  std::size_t episodes = 0;
  std::int32_t firstHitTick = -1;
  std::int32_t lastHitTick = -1;
  std::vector<std::string> weapons;
  std::string confidence;  // low / medium / high
};

struct DemoTickAuditReport {
  bool valid = false;
  std::string error;
  DemoTickAuditLevel verdict = DemoTickAuditLevel::None;

  // FILE LAYER.
  std::size_t packets = 0;
  std::size_t dataPackets = 0;
  std::size_t signonPackets = 0;
  std::size_t malformedPackets = 0;
  std::int64_t firstDataEntryTick = -1;
  std::int64_t lastDataEntryTick = -1;
  std::size_t tickBackwards = 0;
  std::int32_t largestTickBackwards = 0;
  std::size_t inSequenceBackwards = 0;
  std::size_t inSequenceGaps = 0;
  std::size_t outSequenceBackwards = 0;
  std::size_t outSequenceGaps = 0;
  std::size_t clockSamples = 0;
  std::size_t clockJumps = 0;
  std::int32_t largestClockJump = 0;
  std::int64_t clockOffsetFirst = 0;
  std::int64_t clockOffsetLast = 0;
  std::int64_t clockOffsetMin = 0;
  std::int64_t clockOffsetMax = 0;
  std::size_t headerMismatches = 0;
  std::size_t largestTickGap = 0;
  std::uint32_t headerTicks = 0;
  std::uint32_t headerFrames = 0;
  float headerPlaybackTime = 0.0f;
  std::string headerDetail;

  // PLAYER LAYER.
  std::size_t attackEvents = 0;
  std::size_t shots = 0;
  std::size_t shotsUnresolvedWeapon = 0;
  std::size_t simSamples = 0;
  std::size_t sameFrameHits = 0;
  std::size_t shortIntervalHits = 0;
  std::size_t lagExcused = 0;
  std::size_t switchGuarded = 0;
  std::size_t minigunRateHits = 0;
  std::size_t flaggedPlayers = 0;
  bool fuseTripped = false;

  std::size_t issuesDropped = 0;
  std::vector<DemoTickAuditIssue> issues;
  std::vector<DemoTickAuditPlayerSummary> players;
  std::vector<DemoTickAuditHit> hits;
};

// One row per packet, kept by the capture so the report does not have to infer
// the server clock from the summary's playback-relative table (which carries a
// dropped packet's tick forward and therefore cannot show a jump).
struct DemoTickAuditPacketRow {
  std::int32_t entryTick = 0;   // demo/playback tick stamped on the command
  std::int32_t serverTick = 0;  // net_Tick in force for this packet
  std::int32_t gap = 1;         // server ticks since the previous packet, >= 1
  std::uint8_t command = 0;     // 1 = signon, 2 = data
  std::size_t packetOrdinal = 0; // position among signon/data packets, 0-based
  bool ownNetTick = false;      // the packet carried svc_NetTick itself
};

// Opt-in, single-pass capture. It lives behind a pointer on
// DemoNetworkSummary so the product pays nothing unless a caller asks for it:
//   DemoTickAuditCapture capture; summary.tickAuditCapture = &capture;
// The scan then feeds it (a) every packet boundary, (b) every
// m_flSimulationTime write, and (c) every CTEPlayerAnimEvent, none of which are
// bounded by summary.tempEntityEvents' 16384-entry retention cap.
// 2026-11-22 (WORKPLAN P3, "where does a fire / reload animation come from"):
// what the ViewModel entity's animation properties did over the WHOLE recording.
// The entity history keeps checkpoints 128 ticks apart, thinned to 96 rows, so it
// cannot answer "did m_nSequence ever change"; this is fed from the same per-write
// hook the tick audit uses and sees every write. Opt-in through
// DemoTickAuditCapture::enableViewModelCensus(); off, nothing is recorded and the
// audit's own readings are unchanged.
struct DemoViewModelSequenceTransition {
  std::int32_t tick = 0;
  std::int64_t from = 0;
  std::int64_t to = 0;
};
struct DemoViewModelCensusEntity {
  std::int32_t entityIndex = -1;
  std::string className;
  std::size_t sequenceWrites = 0;    // every m_nSequence write, changed or not
  std::size_t sequenceChanges = 0;   // writes whose value differs from the last one
  std::size_t parityWrites = 0;      // m_nAnimationParity: a restart of the same clip
  std::size_t parityChanges = 0;
  std::size_t modelChanges = 0;      // m_nModelIndex changes (weapon switches)
  std::size_t cycleWrites = 0;       // m_flCycle (absent on the recordings measured so far)
  std::size_t playbackRateWrites = 0;
  std::int32_t firstTick = 0;
  std::int32_t lastTick = 0;
  bool haveSequence = false;
  std::int64_t lastSequence = 0;
  bool haveParity = false;
  std::int64_t lastParity = 0;
  bool haveModel = false;
  std::int64_t lastModel = 0;
  std::map<std::int64_t, std::size_t> sequenceWritesByValue;
  std::map<std::int64_t, std::int32_t> sequenceFirstTick;
  std::vector<DemoViewModelSequenceTransition> transitions;  // first 64 changes
};

// P4 (2026-11-22): --fx-census. What the effect-bearing properties did over the
// WHOLE recording -- the medigun's heal / charge state, the flamethrower's weapon
// state, the players' condition words -- so the assumptions gameplay_effects.h
// states (condition bit numbers, weapon-state values, item numbers) are checked
// against a real demo before a default depends on them. Same per-write hook and
// same opt-in rule as the ViewModel census: off, nothing is recorded.
struct DemoFxCensusProp {
  std::size_t writes = 0;
  std::size_t changes = 0;
  std::map<std::int64_t, std::size_t> values;  // first 32 distinct values
  std::size_t valuesOverflow = 0;
};
struct DemoFxChargeEdge {
  std::int32_t tick = 0;
  std::int32_t medigun = -1;
  std::int32_t owner = -1;
  std::int32_t target = -1;
  std::int64_t item = -1;
  bool on = false;
};
struct DemoFxCondEdge {
  std::int32_t tick = 0;
  std::int32_t entity = -1;
  std::int32_t word = 0;      // 0 = bits 0..31 (m_nPlayerCond / _condition_bits), 1 = Ex, 2 = Ex2 ...
  bool conditionList = false; // true for _condition_bits, false for m_nPlayerCond*
  std::uint32_t rose = 0;     // bits that went 0 -> 1 on this write
  std::uint32_t fell = 0;
};
struct DemoFxCensus {
  std::map<std::string, DemoFxCensusProp> props;  // "Class.prop"
  std::vector<DemoFxChargeEdge> chargeEdges;      // first 1024
  std::vector<DemoFxCondEdge> condEdges;          // first 65536
  std::size_t condEdgesDropped = 0;
  // Running state the edges need.
  std::map<std::int32_t, std::int64_t> medigunOwner;
  std::map<std::int32_t, std::int64_t> medigunTarget;
  std::map<std::int32_t, std::int64_t> medigunItem;
  std::map<std::int32_t, std::int64_t> medigunCharge;
  std::map<std::pair<std::int32_t, std::int32_t>, std::uint32_t> lastCond;  // (entity, word*2+list)
};

// A1 (2026-11-23, docs/UI-SHELL-DESIGN-2026-11-22.md section 9): --motion-census.
// The analysis window will list three anomaly kinds -- a stall (server ticks
// skipped between two data packets), a teleport (a player moved further in one
// tick than the game moves anyone), a snap (the eye angles turned further in one
// tick than a hand turns them). Each needs a threshold, and a threshold is only
// defensible against the distribution of the same reading on clean recordings.
// This records that distribution and decides nothing. Same per-write hook and
// opt-in rule as the other censuses: off, nothing is recorded.
//
// A step is taken between two consecutive packets that wrote the entity's origin
// (or eye angles). dt is the server ticks between them. Only steps with
// dt <= rateMaxDt enter the per-tick rate: a player the recording stopped
// updating (out of a POV recorder's view, or standing still on SourceTV) comes
// back with a long dt, and dividing by it would hide a jump inside an average.
// A step within guardTicks of the entity's life state, team / class, or
// TF_COND_TELEPORTED (condition bit 6) changing is counted as excluded: respawn
// and teleporters move a player across the map legitimately.
struct DemoMotionSample {
  std::int32_t tick = 0;           // tick of the later of the two samples
  std::int32_t entity = -1;
  std::int32_t dt = 0;
  double amount = 0.0;             // units (move) or degrees (turn) over dt
  double perTick = 0.0;            // amount / dt
  std::int32_t sinceLife = -1;     // ticks since m_lifeState last changed, -1 = never seen
  std::int32_t sinceTeleport = -1; // ticks since condition bit 6 last rose
  std::int32_t sinceTeam = -1;     // ticks since m_iTeamNum / m_iClass last changed
  bool excluded = false;
};
struct DemoMotionRate {
  float perTick = 0.0f;
  std::int32_t entity = -1;
};
struct DemoMotionCensus {
  std::int32_t guardTicks = 66;
  std::int32_t rateMaxDt = 4;
  std::size_t moveSamples = 0;
  std::size_t turnSamples = 0;
  std::size_t moveExcluded = 0;
  std::size_t turnExcluded = 0;
  std::size_t moveLongDt = 0;
  std::size_t turnLongDt = 0;
  std::map<std::int32_t, std::size_t> moveDt;  // dt bucket upper bound -> steps
  std::map<std::int32_t, std::size_t> turnDt;
  std::vector<DemoMotionRate> moveRates;       // every counted step, dt <= rateMaxDt
  std::vector<DemoMotionRate> turnRates;
  std::vector<DemoMotionSample> topMoves;      // largest per-tick steps, excluded ones included
  std::vector<DemoMotionSample> topTurns;
};

class DemoTickAuditCapture {
 public:
  explicit DemoTickAuditCapture(const DemoTickAuditThresholds& thresholds = {});
  DemoTickAuditCapture(const DemoTickAuditCapture&) = delete;
  DemoTickAuditCapture& operator=(const DemoTickAuditCapture&) = delete;

  void beginPacket(std::int32_t entryTick, std::uint8_t command, std::int32_t serverTickInForce);
  void observeEntityProperty(const DemoNetworkSummary& summary, std::int32_t serverTick,
      std::int32_t entityIndex, std::int32_t classId, const char* propName,
      const EntityPropertyValue& value);
  void observeTempEvent(const DemoNetworkSummary& summary, std::int32_t classId, const EntityState& eventState);
  void enableViewModelCensus(bool enabled) { viewModelCensusEnabled_ = enabled; }
  void enableFxCensus(bool enabled) { fxCensusEnabled_ = enabled; }
  const DemoFxCensus& fxCensus() const { return fxCensus_; }
  void enableMotionCensus(bool enabled) { motionCensusEnabled_ = enabled; }
  const DemoMotionCensus& motionCensus() const { return motionCensus_; }
  const std::map<std::int32_t, DemoViewModelCensusEntity>& viewModelCensus() const {
    return viewModelCensus_;
  }
  // finalServerTick is the packet's own svc_NetTick when it carried one,
  // otherwise the tick carried in from earlier packets.
  void endPacket(const DemoNetworkSummary& summary, std::int32_t finalServerTick, bool ownNetTick);

  const DemoTickAuditThresholds& thresholds() const { return thresholds_; }
  const std::vector<DemoTickAuditPacketRow>& packetRows() const { return packets_; }
  const std::vector<DemoTickAuditHit>& hits() const { return hits_; }

  std::size_t attackEvents() const { return attackEvents_; }
  std::size_t shots() const { return shots_; }
  std::size_t shotsUnresolvedWeapon() const { return shotsUnresolvedWeapon_; }
  std::size_t simSamples() const { return simSamples_; }
  std::size_t sameFrameHits() const { return sameFrameHits_; }
  std::size_t shortIntervalHits() const { return shortIntervalHits_; }
  std::size_t lagExcused() const { return lagExcused_; }
  std::size_t switchGuarded() const { return switchGuarded_; }
  std::size_t minigunRateHits() const { return minigunRateHits_; }
  std::size_t pendingEventsDropped() const { return pendingEventsDropped_; }

 private:
  struct ShotState {
    std::int32_t tick = 0;
    std::int32_t gap = 1;
    std::int32_t weaponEntity = -1;
    std::int32_t floorTicks = 0;
    std::int32_t itemDefinitionIndex = -1;
    bool intervalCheck = false;
  };

  struct SimSample {
    std::int32_t serverTick = 0;
    std::int64_t simTick = 0;
  };

  const char* classNameForClassId(const DemoNetworkSummary& summary, std::int32_t classId) const;
  static bool isPlayerClass(const char* className);
  std::int64_t decodeSimValue(const EntityPropertyValue& value, std::int32_t serverTick,
      std::int32_t entityIndex) const;
  std::int64_t maxSimStep(const std::int32_t playerEntity, std::int32_t serverTick) const;
  void settlePending(const DemoNetworkSummary& summary, std::int32_t serverTick, std::int32_t gap);
  void appendHit(const DemoTickAuditHit& hit);

  void observeViewModelProperty(const DemoNetworkSummary& summary, std::int32_t serverTick,
      std::int32_t entityIndex, std::int32_t classId, const char* propName,
      const EntityPropertyValue& value);
  bool viewModelCensusEnabled_ = false;
  void observeFxProperty(const DemoNetworkSummary& summary, std::int32_t serverTick,
      std::int32_t entityIndex, std::int32_t classId, const char* propName,
      const EntityPropertyValue& value);
  bool fxCensusEnabled_ = false;
  DemoFxCensus fxCensus_;
  // A1: per-entity running state for --motion-census. Writes are gathered per
  // packet (x/y and z arrive as separate properties, and a delta packet carries
  // only the ones that changed) and settled once, in endPacket.
  struct MotionState {
    bool touched = false;
    std::int32_t pendingTick = 0;
    bool havePos = false;
    bool pendPos = false;
    std::int32_t posTick = 0;
    double x = 0.0, y = 0.0, z = 0.0;
    double px = 0.0, py = 0.0, pz = 0.0;
    bool haveAng = false;
    bool pendAng = false;
    std::int32_t angTick = 0;
    double pitch = 0.0, yaw = 0.0;
    double ppitch = 0.0, pyaw = 0.0;
    bool haveLife = false;
    std::int64_t life = 0;
    std::int32_t lifeTick = -1;
    bool haveTeam = false;
    std::int64_t team = 0;
    bool haveClass = false;
    std::int64_t playerClass = 0;
    std::int32_t teamTick = -1;
    std::uint32_t cond = 0;
    std::uint32_t condBits = 0;
    std::int32_t teleTick = -1;
  };
  void observeMotionProperty(const DemoNetworkSummary& summary, std::int32_t serverTick,
      std::int32_t entityIndex, std::int32_t classId, const char* propName,
      const EntityPropertyValue& value);
  void settleMotion();
  void recordMotion(bool turn, std::int32_t entity, std::int32_t tick, std::int32_t dt,
      double amount, const MotionState& state);
  bool motionCensusEnabled_ = false;
  DemoMotionCensus motionCensus_;
  std::map<std::int32_t, MotionState> motionState_;
  std::vector<std::int32_t> motionTouched_;
  std::map<std::int32_t, DemoViewModelCensusEntity> viewModelCensus_;
  DemoTickAuditThresholds thresholds_;
  std::vector<DemoTickAuditPacketRow> packets_;
  std::vector<DemoTickAuditHit> hits_;
  std::vector<std::int64_t> pendingPlayers_;
  std::unordered_map<std::int32_t, std::deque<SimSample>> simHistory_;
  std::unordered_map<std::int32_t, std::deque<ShotState>> shotsByPlayer_;
  std::unordered_map<std::int32_t, std::int32_t> lastWeaponByPlayer_;
  std::unordered_map<std::int32_t, std::int32_t> lastAttackTickByPlayer_;
  std::unordered_map<std::int32_t, std::deque<std::int32_t>> minigunWindow_;

  std::size_t packetOrdinal_ = 0;
  std::int32_t entryTick_ = 0;
  std::int32_t packetServerTick_ = 0;
  std::int32_t prevPacketServerTick_ = -1;
  std::int32_t lastEventClassId_ = -1;
  std::int64_t animPlayer_ = 0;
  std::int64_t animEvent_ = 0;
  bool animPlayerValid_ = false;
  bool animEventValid_ = false;
  std::uint8_t command_ = 0;
  bool inPacket_ = false;

  std::size_t attackEvents_ = 0;
  std::size_t shots_ = 0;
  std::size_t shotsUnresolvedWeapon_ = 0;
  std::size_t simSamples_ = 0;
  std::size_t sameFrameHits_ = 0;
  std::size_t shortIntervalHits_ = 0;
  std::size_t lagExcused_ = 0;
  std::size_t switchGuarded_ = 0;
  std::size_t minigunRateHits_ = 0;
  std::size_t pendingEventsDropped_ = 0;
};

// Builds the report from the index and the capture. capture may be null: the
// file layer then reports the clock/sequence checks it can do from the index
// alone and marks the clock rows as unavailable rather than guessing them.
DemoTickAuditReport auditDemoTicks(const DemoHeader& header, const DemoIndex& index,
    const DemoTickAuditCapture* capture,
    const DemoTickAuditThresholds& thresholds = {});

// One line per fact. The prefix is fixed so a consumer can filter these out of
// a product trace, and no line changes an exit code or a render gate.
std::vector<std::string> formatDemoTickAuditLines(const DemoTickAuditReport& report);

}  // namespace tf2::native
