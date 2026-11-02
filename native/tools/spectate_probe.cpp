// SourceTV spectate director probe (T66).
//
// Two modes, one binary:
//   --self-test    drives SpectateDirector with synthetic snapshots so every rule
//                  in native/include/spectate_target.h is a named assertion that a
//                  mutation can turn red without a GPU, a demo or a lock.
//   --demo <path>  replays a real demo's entity snapshots through the same class
//                  and prints what the relay actually did, so the gate can assert
//                  that the S1 sample contains at least one real death and one
//                  real handover rather than only the synthetic path.
#include "demo_header.h"
#include "entity_model.h"
#include "spectate_target.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

// The speed the unit tests replay at. Every call states it: the seek bound is
// derived from the speed, and a caller that inherits the wrong one misjudges a
// fast replay as a seek (T66 note 1). S7's speed checks pass their own numbers.
constexpr double kReplaySpeed = 8.0;

using tf2::native::EntityPropertyValue;
using tf2::native::EntitySnapshotQueryStatus;
using tf2::native::EntityState;
using tf2::native::SendPropType;
using tf2::native::SpectateDirector;
using tf2::native::SpectateSlot;
using tf2::native::SpectateSource;

int g_checked = 0;
int g_failed = 0;

void check(bool condition, const std::string& name) {
  ++g_checked;
  if (condition) {
    std::printf("OK %s\n", name.c_str());
    return;
  }
  ++g_failed;
  std::printf("FAIL %s\n", name.c_str());
}

bool nearly(double a, double b) {
  return std::fabs(a - b) <= 1.0e-3;
}

SpectateSlot makeSlot(int index, bool alive, float x, float y, float z, int team = 2) {
  SpectateSlot slot;
  slot.entityIndex = index;
  slot.isPlayer = true;
  slot.hasHealth = true;
  slot.health = alive ? 100 : 0;
  slot.hasLifeState = true;
  slot.lifeState = alive ? 0 : 2;
  slot.hasTeam = true;
  slot.team = team;
  slot.hasOrigin = true;
  slot.origin[0] = x;
  slot.origin[1] = y;
  slot.origin[2] = z;
  return slot;
}

void focusOf(const SpectateDirector& director, float out[3]) {
  out[0] = out[1] = out[2] = 0.0f;
  director.focus(out);
}

// ---------------------------------------------------------------------------
// Synthetic self-test
// ---------------------------------------------------------------------------
int runSelfTest() {
  // S9 first, because every later case relies on the handle decoder: the S1
  // reading (301073) is the example the report cites, so it is pinned here.
  {
    const auto handle = tf2::native::decodeEntityHandle(301073, 64);
    check(handle.valid && handle.entityIndex == 17 && handle.serial == 147,
          "S9 the S1 observer handle 301073 decodes to entity 17 serial 147");
    check(!tf2::native::decodeEntityHandle(301073, 16).valid,
          "S9 a handle whose entry index is past the entity count is refused");
    check(!tf2::native::decodeEntityHandle(0, 64).valid, "S9 a zero handle names no entity");
    check(tf2::native::decodeEntityHandle(-1, 64).valid == false, "S9 a negative handle names no entity");
  }

  // S1: the first adopt is not a handover and carries no transition window.
  {
    SpectateDirector director;
    std::vector<SpectateSlot> slots = { makeSlot(3, true, 100.0f, 0.0f, 0.0f),
                                        makeSlot(5, true, 200.0f, 0.0f, 0.0f) };
    director.update(0.0, slots, 0, 1000, kReplaySpeed);
    check(director.targetIndex() == 3, "S1 first target is the lowest living entity index");
    check(director.source() == SpectateSource::StableOrder, "S1 the first target comes from the stable order");
    check(director.handovers() == 0, "S1 the first adoption is not a handover");
    check(director.transitioning() == false, "S1 the first adoption has no transition window");
    float focus[3];
    focusOf(director, focus);
    check(nearly(focus[0], 100.0) && nearly(focus[1], 0.0), "S1 the focus lands on the first target");
    check(director.hidesModel(3) == false, "S1 a living target is drawn");
  }

  // S2: death -> relay -> wall-clock transition -> arrival on the new target.
  {
    SpectateDirector director;
    std::vector<SpectateSlot> slots = { makeSlot(3, true, 100.0f, 0.0f, 0.0f),
                                        makeSlot(5, true, 200.0f, 0.0f, 0.0f) };
    director.update(0.0, slots, 0, 1000, kReplaySpeed);
    slots[0] = makeSlot(3, false, 100.0f, 0.0f, 0.0f);   // the observed player dies
    director.update(0.02, slots, 0, 1001, kReplaySpeed);
    check(director.deaths() == 1, "S2 the death of the observed target is counted");
    check(director.lastDeathTick() == 1001, "S2 the death is stamped with the tick it was seen on");
    check(director.targetIndex() == 5, "S2 the camera relays to the next living player");
    check(director.handovers() == 1, "S2 the relay is counted as a handover");
    check(director.transitioning() == true, "S2 a handover opens a transition window");
    check(director.hidesModel(3) == true, "S2 the dead body stops being drawn at the death tick");
    float early[3];
    focusOf(director, early);
    check(nearly(early[0], 100.0),
          "S2 the handover starts from where the camera was instead of cutting to the new player");
    director.update(0.17, slots, 0, 1001, kReplaySpeed);   // same tick: ONLY the wall clock moved
    float middle[3];
    focusOf(director, middle);
    check(middle[0] > early[0], "S2 the focus keeps moving toward the new target while the tick is frozen");
    check(director.transitioning() == true, "S2 the window is still open before 0.3 s");
    check(director.resyncs() == 0, "S2 a frozen demo tick is not a seek");
    director.update(0.32, slots, 0, 1001, kReplaySpeed);
    float arrived[3];
    focusOf(director, arrived);
    check(director.transitioning() == false, "S2 the window closes once 0.3 s of wall time have passed");
    check(nearly(arrived[0], 200.0), "S2 the focus arrives exactly on the new target");
  }

  // S2b: a row the snapshot never resolved is not a corpse.
  {
    SpectateDirector director;
    std::vector<SpectateSlot> slots = { makeSlot(3, true, 100.0f, 0.0f, 0.0f) };
    SpectateSlot unresolved;
    unresolved.entityIndex = 7;
    unresolved.isPlayer = true;
    unresolved.hasOrigin = true;
    unresolved.origin[0] = 300.0f;
    slots.push_back(unresolved);
    director.update(0.0, slots, 0, 1000, kReplaySpeed);
    check(director.targetIndex() == 3, "S2b an unresolved row never becomes the camera target");
    check(director.hidesModel(7) == false,
          "S2b an unresolved row is not hidden: no reading is not evidence of a corpse");
    check(director.hiddenDead() == 0, "S2b the hidden counter counts corpses, not unresolved rows");
  }

  // S4: total wipe keeps the last valid camera and never falls back to the origin.
  {
    SpectateDirector director;
    std::vector<SpectateSlot> slots = { makeSlot(3, true, 100.0f, 0.0f, 0.0f),
                                        makeSlot(5, true, 200.0f, 0.0f, 0.0f) };
    director.update(0.0, slots, 0, 1000, kReplaySpeed);
    float before[3];
    focusOf(director, before);
    slots[0] = makeSlot(3, false, 100.0f, 0.0f, 0.0f);
    slots[1] = makeSlot(5, false, 200.0f, 0.0f, 0.0f);
    director.update(0.5, slots, 0, 1002, kReplaySpeed);
    float after[3];
    focusOf(director, after);
    check(director.noTargetFrames() >= 1, "S4 a total wipe is counted instead of silently ignored");
    check(nearly(after[0], before[0]) && nearly(after[1], before[1]) && nearly(after[2], before[2]),
          "S4 the camera keeps its last valid position when nobody is alive");
    check(!(nearly(after[0], 0.0) && nearly(after[1], 0.0) && nearly(after[2], 0.0)),
          "S4 the camera never falls back to the world origin");
    check(director.deaths() == 1, "S4 only the observed target's death is counted, not the whole wipe");
  }

  // S5: a manual pick applies in the same frame, with no window.
  {
    SpectateDirector director;
    std::vector<SpectateSlot> slots = { makeSlot(3, true, 100.0f, 0.0f, 0.0f),
                                        makeSlot(5, true, 200.0f, 0.0f, 0.0f) };
    director.update(0.0, slots, 0, 1000, kReplaySpeed);
    director.pickManual(5);
    director.update(0.001, slots, 0, 1000, kReplaySpeed);
    check(director.targetIndex() == 5, "S5 a manual pick changes the target");
    check(director.source() == SpectateSource::Manual, "S5 the manual pick is named as its source");
    check(director.manualPicks() == 1, "S5 the manual pick is counted");
    check(director.handovers() == 0, "S5 a manual pick is not a handover");
    check(director.transitioning() == false, "S5 a manual pick has no transition window");
    float focus[3];
    focusOf(director, focus);
    check(nearly(focus[0], 200.0), "S5 the manual pick lands in the same frame");
  }

  // S6: a manual pick on a corpse is refused, not obeyed.
  {
    SpectateDirector director;
    std::vector<SpectateSlot> slots = { makeSlot(3, true, 100.0f, 0.0f, 0.0f),
                                        makeSlot(5, false, 200.0f, 0.0f, 0.0f) };
    director.update(0.0, slots, 0, 1000, kReplaySpeed);
    director.pickManual(5);
    director.update(0.001, slots, 0, 1000, kReplaySpeed);
    check(director.manualRejected() == 1, "S6 a pick on a dead slot is rejected");
    check(director.targetIndex() == 3, "S6 a rejected pick leaves the target alone");
    check(director.manualPicks() == 0, "S6 a rejected pick is not counted as a pick");
  }

  // S7: a seek abandons a half-finished handover and is counted.
  {
    SpectateDirector director;
    std::vector<SpectateSlot> slots = { makeSlot(3, true, 100.0f, 0.0f, 0.0f),
                                        makeSlot(5, true, 200.0f, 0.0f, 0.0f) };
    director.update(0.0, slots, 0, 5000, kReplaySpeed);
    slots[0] = makeSlot(3, false, 100.0f, 0.0f, 0.0f);
    director.update(0.02, slots, 0, 5001, kReplaySpeed);
    check(director.transitioning() == true, "S7 a handover is open before the seek");
    director.update(0.03, slots, 0, 200, kReplaySpeed);   // rewind
    check(director.resyncs() == 1, "S7 a backwards tick is counted as a seek");
    check(director.transitioning() == false, "S7 a seek abandons the half-finished handover");
    director.update(0.04, slots, 0, 9000, kReplaySpeed);  // jump forward past the gap
    check(director.resyncs() == 2, "S7 a forward jump larger than the gap is counted as a seek");
  }

  // S7b: the seek bound follows the playback speed. A 240-tick step over 3 s of a
  // 16x replay is ordinary playback (3168 ticks could have passed); the old fixed
  // 200-tick gap counted it as a seek, which is the defect T66 note 1 records. The
  // same step in 10 ms of wall time is a seek at any speed, and the floor keeps a
  // paused screen that advances one tick from being read as one.
  {
    SpectateDirector fast;
    std::vector<SpectateSlot> slots = { makeSlot(3, true, 100.0f, 0.0f, 0.0f),
                                        makeSlot(5, true, 200.0f, 0.0f, 0.0f) };
    fast.update(0.0, slots, 0, 1000, 16.0);
    fast.update(3.0, slots, 0, 1240, 16.0);
    check(fast.resyncs() == 0, "S7b a 240-tick step over 3 s at 16x is playback, not a seek");
    fast.update(3.01, slots, 0, 1480, 16.0);
    check(fast.resyncs() == 1, "S7b the same 240-tick step in 10 ms at 16x is a seek");

    SpectateDirector paused;
    paused.update(0.0, slots, 0, 500, 16.0);
    paused.update(0.0, slots, 0, 501, 16.0);
    check(paused.resyncs() == 0, "S7b a one-tick step with no elapsed time is not a seek");

    SpectateDirector slow;
    slow.update(0.0, slots, 0, 1000, 1.0);
    slow.update(0.5, slots, 0, 1240, 1.0);
    check(slow.resyncs() == 1, "S7b 240 ticks in 0.5 s at 1x is a seek");
  }

  // S8: the demo's own director handle wins when it names a living player.
  {
    SpectateDirector director;
    std::vector<SpectateSlot> slots = { makeSlot(3, true, 100.0f, 0.0f, 0.0f),
                                        makeSlot(7, true, 300.0f, 0.0f, 0.0f) };
    const std::int64_t handle = (147LL << 11) | 7LL;
    director.update(0.0, slots, handle, 1000, kReplaySpeed);
    check(director.targetIndex() == 7, "S8 a living director handle decides the target before the stable order");
    check(director.source() == SpectateSource::Director, "S8 the source reads director");
    check(director.directorPicks() == 1, "S8 the director pick is counted");
    check(director.stablePicks() == 0, "S8 the stable order is not used when the director names a living player");
    // The same handle pointing at a spectator slot: team 1 is a camera, not a body.
    std::vector<SpectateSlot> spectator = { makeSlot(3, true, 100.0f, 0.0f, 0.0f),
                                            makeSlot(7, true, 300.0f, 0.0f, 0.0f, 1) };
    SpectateDirector second;
    second.update(0.0, spectator, handle, 1000, kReplaySpeed);
    check(second.targetIndex() == 3, "S8 a director handle on the spectator team is not an observation target");
    check(second.source() == SpectateSource::StableOrder, "S8 the stable order takes over when the handle is unusable");
  }

  // S10: the same input produces the same trace, which is the only form of
  // "stable" a frame-by-frame gate can assert.
  {
    const auto run = []() {
      SpectateDirector director;
      std::vector<SpectateSlot> slots = { makeSlot(3, true, 100.0f, 0.0f, 0.0f),
                                          makeSlot(5, true, 200.0f, 0.0f, 0.0f) };
      std::string trace;
      double now = 0.0;
      for (int frame = 0; frame < 24; ++frame) {
        const int tick = 1000 + frame;
        if (frame == 6) slots[0] = makeSlot(3, false, 100.0f, 0.0f, 0.0f);
        if (frame == 12) slots[1] = makeSlot(5, false, 200.0f, 0.0f, 0.0f);
        if (frame == 14) slots[0] = makeSlot(3, true, 110.0f, 0.0f, 0.0f);
        now += 0.033;
        director.update(now, slots, 0, tick, kReplaySpeed);
        trace += director.traceFields();
        trace += '\n';
      }
      return trace;
    };
    check(run() == run(), "S10 two runs of the same snapshot sequence produce the same trace");
  }

  std::printf("SPECTATE-SELF-TEST=%s checks=%d failures=%d\n",
              g_failed == 0 ? "PASS" : "FAIL", g_checked, g_failed);
  return g_failed == 0 ? 0 : 1;
}

// ---------------------------------------------------------------------------
// Real demo replay
// ---------------------------------------------------------------------------
const EntityPropertyValue* findNamed(const EntityState& state, const char* key) {
  const std::size_t length = std::strlen(key);
  const EntityPropertyValue* loose = nullptr;
  for (const auto& entry : state.properties) {
    const std::string& name = entry.first;
    if (name.size() == length && name.compare(0, length, key) == 0) return &entry.second;
    if (name.size() > length && name.compare(name.size() - length, length, key) == 0
        && name[name.size() - length - 1] == '.' && loose == nullptr) {
      loose = &entry.second;
    }
  }
  return loose;
}

bool readInt(const EntityState& state, const char* key, std::int64_t& out) {
  const EntityPropertyValue* value = findNamed(state, key);
  if (value == nullptr) return false;
  if (value->type != SendPropType::Int) return false;
  out = value->intValue;
  return true;
}

int runDemo(const std::string& demoPath, int fromTick, int toTick, int step, bool trace) {
  tf2::native::DemoHeader header;
  if (!tf2::native::parseDemoHeaderFile(demoPath, header)) {
    std::printf("FAIL demo header %s\n", demoPath.c_str());
    return 1;
  }
  tf2::native::DemoIndex index;
  if (!tf2::native::indexDemoFile(demoPath, header, index)) {
    std::printf("FAIL demo index %s\n", demoPath.c_str());
    return 1;
  }
  tf2::native::DemoNetworkSummary summary;
  // The scan refuses a zero protocol on purpose: decoding with protocol 0 takes the
  // wrong svc_CreateStringTable length branch and yields a plausible, wrong summary.
  summary.networkProtocol = header.networkProtocol;
  if (!tf2::native::scanKnownDemoMessages(demoPath, index, summary)) {
    std::printf("FAIL demo scan %s\n", demoPath.c_str());
    return 1;
  }
  std::printf("demo: map=%s source_tv=%d setview=%zu view_entity=%u ticks=%d frames=%d packets=%zu\n",
              summary.serverMap.c_str(), summary.sourceTv ? 1 : 0, summary.setViewCount,
              static_cast<unsigned>(summary.lastViewEntity), header.ticks, header.frames,
              index.packetCount);

  SpectateDirector director;
  int frames = 0;
  int transitionOpens = 0;
  int transitionCloses = 0;
  int originFocusHits = 0;
  std::uint32_t lastDeaths = 0;
  std::uint32_t lastHandovers = 0;
  bool previousTransition = false;
  std::int32_t previousTarget = -2;
  // The snapshot source is part of the reading, not a detail: a window answered
  // only from archived checkpoints is a lagging camera, and a gate that reported
  // its deaths without saying which source they came from would be quoting a
  // number it cannot defend. Skipped ticks are counted for the same reason --
  // "no frames" must never look like "no deaths".
  int availableFrames = 0;
  int checkpointFrames = 0;
  int skippedFrames = 0;
  int checkpointLagSum = 0;
  int checkpointLagWorst = 0;
  for (int tick = fromTick; tick <= toTick; tick += step) {
    std::vector<EntityState> states;
    std::int32_t resolvedTick = 0;
    const auto status = tf2::native::queryEntitySnapshotAtOrBeforeTick(summary, tick, states, &resolvedTick);
    if (status == EntitySnapshotQueryStatus::Available) {
      ++availableFrames;
    } else if (status == EntitySnapshotQueryStatus::Checkpoint) {
      ++checkpointFrames;
      const int lag = resolvedTick > 0 ? tick - resolvedTick : 0;
      checkpointLagSum += lag;
      if (lag > checkpointLagWorst) checkpointLagWorst = lag;
    } else {
      ++skippedFrames;
      continue;
    }
    std::vector<SpectateSlot> slots;
    for (std::size_t entity = 0; entity < states.size(); ++entity) {
      const EntityState& state = states[entity];
      if (state.classId < 0) continue;
      std::string className;
      if (static_cast<std::size_t>(state.classId) < summary.serverClassSchemas.size()) {
        className = summary.serverClassSchemas[static_cast<std::size_t>(state.classId)].name;
      }
      // Every entity row becomes a slot, player or not: the director handle is a
      // WORLD index, so the relay has to know how far the world reaches even when
      // most of those rows can never be watched (isPlayer decides that).
      SpectateSlot slot;
      slot.entityIndex = static_cast<std::int32_t>(entity);
      slot.isPlayer = className.find("Player") != std::string::npos;
      std::int64_t value = 0;
      if (readInt(state, "m_iHealth", value)) { slot.hasHealth = true; slot.health = value; }
      if (readInt(state, "m_lifeState", value)) { slot.hasLifeState = true; slot.lifeState = value; }
      if (readInt(state, "m_iTeamNum", value)) { slot.hasTeam = true; slot.team = value; }
      const auto transform = tf2::native::EntityModelResolver::extractTransform(state);
      slot.hasOrigin = transform.hasOrigin;
      if (transform.hasOrigin) {
        slot.origin[0] = transform.origin[0];
        slot.origin[1] = transform.origin[1];
        slot.origin[2] = transform.origin[2];
      }
      slots.push_back(slot);
    }
    std::int64_t handle = 0;
    if (static_cast<std::size_t>(summary.lastViewEntity) < states.size()) {
      readInt(states[static_cast<std::size_t>(summary.lastViewEntity)], "m_hObserverTarget", handle);
    }
    const double wall = static_cast<double>(tick - fromTick) / 66.0;
    director.update(wall, slots, handle, tick, kReplaySpeed);
    ++frames;
    if (director.transitioning() && !previousTransition) ++transitionOpens;
    if (!director.transitioning() && previousTransition) ++transitionCloses;
    previousTransition = director.transitioning();
    float focus[3];
    if (director.focusValid()) {
      focusOf(director, focus);
      if (nearly(focus[0], 0.0) && nearly(focus[1], 0.0) && nearly(focus[2], 0.0)) ++originFocusHits;
    }
    if (director.targetIndex() != previousTarget) {
      if (trace) std::printf("spectate: tick=%d target-change from=%d %s\n", tick, previousTarget,
                             director.traceFields().c_str());
      previousTarget = director.targetIndex();
    }
    lastDeaths = director.deaths();
    lastHandovers = director.handovers();
    if (trace && director.transitioning()) {
      std::printf("spectate: tick=%d %s\n", tick, director.traceFields().c_str());
    }
  }
  float finalFocus[3] = {0.0f, 0.0f, 0.0f};
  const bool haveFocus = director.focusValid();
  if (haveFocus) focusOf(director, finalFocus);
  std::printf("demo: source available=%d checkpoint=%d skipped=%d checkpoint_lag_mean=%.1f worst=%d\n",
              availableFrames, checkpointFrames, skippedFrames,
              checkpointFrames > 0 ? static_cast<double>(checkpointLagSum) / checkpointFrames : 0.0,
              checkpointLagWorst);
  std::printf("demo: frames=%d deaths=%u handovers=%u director=%u stable=%u manual=%u rejected=%u"
              " notarget=%u resync=%u hidden=%u transitions_open=%d transitions_closed=%d"
              " origin_focus_hits=%d final_target=%d focus_valid=%d final_focus=%.1f,%.1f,%.1f"
              " last_deaths=%u last_handovers=%u\n",
              frames, director.deaths(), director.handovers(), director.directorPicks(),
              director.stablePicks(), director.manualPicks(), director.manualRejected(),
              director.noTargetFrames(), director.resyncs(), director.hiddenDead(),
              transitionOpens, transitionCloses, originFocusHits, director.targetIndex(),
              haveFocus ? 1 : 0, static_cast<double>(finalFocus[0]), static_cast<double>(finalFocus[1]),
              static_cast<double>(finalFocus[2]), lastDeaths, lastHandovers);
  std::printf("SPECTATE-DEMO=%s frames=%d\n", frames > 0 ? "PASS" : "FAIL", frames);
  return frames > 0 ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::printf("usage: spectate_probe --self-test | --demo <path> [--from-tick N] [--to-tick N] [--step N] [--trace]\n");
    return 2;
  }
  bool selfTest = false;
  std::string demoPath;
  int fromTick = 0;
  int toTick = 0;
  int step = 1;
  bool trace = false;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--self-test") selfTest = true;
    else if (arg == "--demo" && i + 1 < argc) demoPath = argv[++i];
    else if (arg == "--from-tick" && i + 1 < argc) fromTick = std::atoi(argv[++i]);
    else if (arg == "--to-tick" && i + 1 < argc) toTick = std::atoi(argv[++i]);
    else if (arg == "--step" && i + 1 < argc) step = std::atoi(argv[++i]);
    else if (arg == "--trace") trace = true;
  }
  if (selfTest) return runSelfTest();
  if (demoPath.empty()) {
    std::printf("FAIL no mode: pass --self-test or --demo <path>\n");
    return 2;
  }
  if (step <= 0) step = 1;
  if (toTick > 0 && toTick < fromTick) {
    std::printf("FAIL range --from-tick %d is past --to-tick %d: the window selects no frame, "
                "and an empty result here is the argument's fault, not the relay's\n", fromTick, toTick);
    return 2;
  }
  if (toTick <= 0) toTick = fromTick + 2000;
  return runDemo(demoPath, fromTick, toTick, step, trace);
}
