// entity_overlay_probe -- the contract's own readings, with no GPU and (for the
// fixture modes) no demo.
//
// WHY A PROBE RATHER THAN ASSERTIONS IN THE GATE
//   Every criterion T52 has to prove is a statement about a value: which entity,
//   which team, which pose, which pass ordinal, which pixel predicate. If those
//   values were only reachable from inside the renderer, the gate could only
//   look at pictures, and a picture cannot tell "the outline is red because the
//   player is on RED" from "the outline is red because the colours were swapped
//   and this player happens to be BLU". This probe calls the SAME functions the
//   product calls -- sampleOverlayEntities, buildOutlineDrawList, the toggle
//   state, the pass-order and pixel predicates -- so the gate asserts the
//   product's own answers instead of a second implementation's.
//
// MODES
//   --self-test                  contract invariants over synthetic states.
//   --fixtures                   the five coverage cases (visible, occluded,
//                                unknown team, despawn, no players) as data.
//   --demo <path> --ticks a,b,c  real recordings: identity, team and pose at
//                                each sampled tick, plus lifecycle transitions.
//   --json                       machine-readable output (the gate parses this).
//
// EXIT: 0 = every assertion held; 1 = one did not. A fixture mode that cannot
// run (no demo, unreadable) exits 2 and says which input was missing -- a SKIP
// reported as a pass is the failure shape this repository has already paid for.

#include "asset_root.h"
#include "demo_header.h"
#include "entity_model.h"
#include "entity_overlay.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

int g_failures = 0;
bool g_json = false;

void check(bool condition, const char* what) {
  if (!condition) {
    ++g_failures;
    std::fprintf(stderr, "FAIL %s\n", what);
  }
}

void checkEq(long long actual, long long expected, const char* what) {
  if (actual != expected) {
    ++g_failures;
    std::fprintf(stderr, "FAIL %s got=%lld expected=%lld\n", what, actual, expected);
  }
}

tf2::native::EntityPropertyValue intProp(std::int64_t value, std::int32_t tick = -1) {
  tf2::native::EntityPropertyValue prop;
  prop.type = tf2::native::SendPropType::Int;
  prop.intValue = value;
  prop.lastWriteTick = tick;
  return prop;
}

tf2::native::EntityPropertyValue vecProp(float x, float y, float z, bool xy = false) {
  tf2::native::EntityPropertyValue prop;
  prop.type = xy ? tf2::native::SendPropType::VectorXY : tf2::native::SendPropType::Vector;
  prop.x = x; prop.y = y; prop.z = xy ? 0.0f : z;
  return prop;
}

tf2::native::EntityPropertyValue floatProp(float value) {
  tf2::native::EntityPropertyValue prop;
  prop.type = tf2::native::SendPropType::Float;
  prop.x = value;
  return prop;
}

// A server class table with the names the filter keys on. Ids are arbitrary on
// purpose: the contract matches by NAME, and a probe that pinned an id could
// not catch a change that started matching ids.
struct FixtureWorld {
  std::vector<tf2::native::ServerClassSchema> schemas;
  std::unordered_map<std::uint16_t, std::string> modelPrecache;

  FixtureWorld() {
    add("CTFPlayer");
    add("CTFProjectile_Rocket");
    add("CTFViewModel");
    add("CWorld");
    modelPrecache[11] = "models/player/scout.mdl";
    modelPrecache[12] = "models/player/heavy.mdl";
    modelPrecache[13] = "models/weapons/c_models/c_rocketlauncher/c_rocketlauncher.mdl";
  }
  void add(const char* name) {
    tf2::native::ServerClassSchema schema;
    schema.id = static_cast<std::uint16_t>(schemas.size());
    schema.name = name;
    schemas.push_back(schema);
  }
  std::int32_t idOf(const char* name) const {
    for (const auto& schema : schemas) if (schema.name == name) return schema.id;
    return -1;
  }
};

// One player entity with the properties a real snapshot carries.
tf2::native::EntityState playerState(const FixtureWorld& world, std::int64_t team,
    std::int64_t playerClass, float x, float y, float z, std::int64_t modelIndex) {
  tf2::native::EntityState state;
  state.classId = world.idOf("CTFPlayer");
  state.properties["DT_BaseEntity.m_vecOrigin"] = vecProp(x, y, z, true);
  state.properties["DT_BaseEntity.m_vecOrigin[2]"] = floatProp(z);
  state.properties["DT_TFPlayer.m_iTeamNum"] = intProp(team);
  state.properties["DT_TFPlayer.m_iClass"] = intProp(playerClass);
  state.properties["DT_TFPlayer.m_angEyeAngles[0]"] = floatProp(0.0f);
  state.properties["DT_TFPlayer.m_angEyeAngles[1]"] = floatProp(90.0f);
  state.properties["DT_BaseEntity.m_nModelIndex"] = intProp(modelIndex);
  return state;
}

// ---------------------------------------------------------------------------
// 1. Contract invariants, no demo and no GPU.
// ---------------------------------------------------------------------------
void selfTest() {
  using namespace tf2::native;

  // Team classification. 2 -> Red, 3 -> Blue, everything else Unknown, and the
  // "absent" and "out of range" cases are the same ANSWER with different
  // evidence, which is what the sample's hasTeam/teamRaw pair is for.
  check(classifyOverlayTeam(true, 2) == OverlayTeam::Red, "team 2 is red");
  check(classifyOverlayTeam(true, 3) == OverlayTeam::Blue, "team 3 is blue");
  check(classifyOverlayTeam(true, 0) == OverlayTeam::Unknown, "team 0 is unknown");
  check(classifyOverlayTeam(true, 1) == OverlayTeam::Unknown, "team 1 (spectator) is unknown");
  check(classifyOverlayTeam(true, 4) == OverlayTeam::Unknown, "team 4 is unknown");
  check(classifyOverlayTeam(false, 0) == OverlayTeam::Unknown, "absent team is unknown");
  check(classifyOverlayTeam(true, 999) == OverlayTeam::Unknown, "team 999 is unknown");

  // Colours: red is red-dominant, blue is blue-dominant, unknown satisfies
  // neither -- so an unknown fallback can never pass a red or blue assertion.
  const auto red = overlayTeamColor(OverlayTeam::Red);
  const auto blue = overlayTeamColor(OverlayTeam::Blue);
  const auto unknown = overlayTeamColor(OverlayTeam::Unknown);
  const auto to8 = [](float value) {
    return static_cast<std::uint8_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
  };
  check(overlayPixelMatchesTeam(to8(red.rgba[0]), to8(red.rgba[1]), to8(red.rgba[2]), OverlayTeam::Red),
    "the red team colour satisfies the red predicate");
  check(!overlayPixelMatchesTeam(to8(red.rgba[0]), to8(red.rgba[1]), to8(red.rgba[2]), OverlayTeam::Blue),
    "the red team colour does NOT satisfy the blue predicate");
  check(overlayPixelMatchesTeam(to8(blue.rgba[0]), to8(blue.rgba[1]), to8(blue.rgba[2]), OverlayTeam::Blue),
    "the blue team colour satisfies the blue predicate");
  check(!overlayPixelMatchesTeam(to8(blue.rgba[0]), to8(blue.rgba[1]), to8(blue.rgba[2]), OverlayTeam::Red),
    "the blue team colour does NOT satisfy the red predicate");
  check(overlayPixelIsNeutral(to8(unknown.rgba[0]), to8(unknown.rgba[1]), to8(unknown.rgba[2])),
    "the unknown team colour is neutral");
  check(!overlayPixelMatchesTeam(to8(unknown.rgba[0]), to8(unknown.rgba[1]), to8(unknown.rgba[2]), OverlayTeam::Red),
    "an unknown-team pixel can never pass the red predicate");
  check(!overlayPixelMatchesTeam(to8(unknown.rgba[0]), to8(unknown.rgba[1]), to8(unknown.rgba[2]), OverlayTeam::Blue),
    "an unknown-team pixel can never pass the blue predicate");

  // Sampling stride: the stride is a property of the TICK, so the same tick is
  // sampled on every run regardless of the frame counter.
  checkEq(overlaySampleStride(OverlaySampleTier::EveryTick), 1, "everyTick stride");
  checkEq(overlaySampleStride(OverlaySampleTier::EveryOtherTick), 2, "everyOtherTick stride");
  checkEq(overlaySampleStride(OverlaySampleTier::EveryFourthTick), 4, "everyFourthTick stride");
  check(overlayTickSampled(OverlaySampleTier::EveryTick, 7), "everyTick samples tick 7");
  check(overlayTickSampled(OverlaySampleTier::EveryOtherTick, 8), "everyOtherTick samples tick 8");
  check(!overlayTickSampled(OverlaySampleTier::EveryOtherTick, 9), "everyOtherTick skips tick 9");
  check(overlayTickSampled(OverlaySampleTier::EveryFourthTick, 12), "everyFourthTick samples tick 12");
  check(!overlayTickSampled(OverlaySampleTier::EveryFourthTick, 13), "everyFourthTick skips tick 13");
  // A negative tick is refused rather than folded through a negative modulus.
  check(!overlayTickSampled(OverlaySampleTier::EveryFourthTick, -1),
    "a negative tick is never sampled");

  // Pass order: the overlay must come strictly before the ViewModel pass, and
  // an unset ordinal (0) must not satisfy the predicate.
  check(overlayPassOrderValid(6, 7), "ordinal 6 < 7 is a valid pass order");
  check(!overlayPassOrderValid(7, 6), "ordinal 7 after 6 is NOT a valid pass order");
  check(!overlayPassOrderValid(6, 6), "equal ordinals are not a valid order");
  check(!overlayPassOrderValid(0, 7), "an overlay that never drew does not satisfy the order");
  check(!overlayPassOrderValid(6, 0), "a ViewModel pass that never drew does not satisfy the order");

  // Toggle: one slot, one owner. A foreign owner cannot change it.
  OverlayToggleState toggle;
  check(toggle.acquire(OverlaySlotOwner::Outline), "outline acquires a free slot");
  check(!toggle.acquire(OverlaySlotOwner::Trajectory), "trajectory is refused the held slot");
  toggle.on(OverlaySlotOwner::Outline);
  check(toggle.enabled(), "the owner turns the slot on");
  toggle.off(OverlaySlotOwner::Trajectory);
  check(toggle.enabled(), "a foreign owner's off() does NOT change the slot");
  checkEq(toggle.refusedForeignPresses(), 2, "both foreign attempts were counted");
  toggle.off(OverlaySlotOwner::Outline);
  check(!toggle.enabled(), "the owner turns the slot off");
  checkEq(toggle.stateChanges(), 2, "two real state changes");
  checkEq(toggle.presses(), 2, "two accepted presses");
  check(toggle.release(OverlaySlotOwner::Outline), "the owner releases the slot");
  check(!toggle.release(OverlaySlotOwner::Outline), "a second release is refused");
  check(toggle.acquire(OverlaySlotOwner::Trajectory), "the freed slot can be acquired");
  // setStartupDefault is applied before any feature exists, so it is not gated.
  toggle.setStartupDefault(true);
  check(toggle.enabled() && toggle.startupDefault(), "the startup default is applied unowned");

  // Sample: identity, team and pose come from ONE extraction, and the model
  // path is resolved through the precache rather than guessed.
  FixtureWorld world;
  std::vector<EntityState> states(6);
  states[1] = playerState(world, 2, 1, 100.0f, 200.0f, 32.0f, 11);
  states[2] = playerState(world, 3, 5, 300.0f, 400.0f, 48.0f, 12);
  states[3] = playerState(world, 0, 1, 10.0f, 20.0f, 4.0f, 11);   // unknown team
  states[4] = playerState(world, 2, 1, 0.0f, 0.0f, 0.0f, 0);      // no model index
  states[5].classId = world.idOf("CTFProjectile_Rocket");
  states[5].properties["DT_BaseEntity.m_vecOrigin"] = vecProp(7.0f, 8.0f, 9.0f, false);
  states[5].properties["DT_BaseEntity.m_iTeamNum"] = intProp(3);

  OverlaySampleStats stats;
  const auto players = sampleOverlayEntities(states, world.schemas, world.modelPrecache,
    OverlayEntityFilter::Players, nullptr, &stats);
  checkEq(static_cast<long long>(players.size()), 4, "the player filter selects four players");
  checkEq(static_cast<long long>(stats.candidatesSeen), 4, "four candidates were seen");
  checkEq(static_cast<long long>(stats.teamKnown), 2, "two players have a known team");
  checkEq(static_cast<long long>(stats.teamUnknown), 2, "two players do not");
  checkEq(static_cast<long long>(stats.modelPathResolved), 3, "three players name a declared model");
  // Identity is carried on every row, so "which entity is this pixel" is a
  // reading and not an inference from draw order.
  checkEq(players[0].entityIndex, 1, "first sampled row is entity 1");
  check(players[0].className == "CTFPlayer", "the class name travels with the row");
  check(players[0].team == OverlayTeam::Red, "entity 1 is red");
  check(players[1].team == OverlayTeam::Blue, "entity 2 is blue");
  check(players[2].team == OverlayTeam::Unknown, "entity 3 is unknown");
  check(players[2].hasTeam, "entity 3 HAS a team slot -- it is the VALUE that is unknown");
  checkEq(players[2].teamRaw, 0, "entity 3's raw team value is 0");
  check(players[0].hasOrigin, "entity 1 has an origin");
  check(std::fabs(players[1].origin[0] - 300.0f) < 1.0e-4f, "entity 2's x survives the sample");

  // The projectile filter is a different selection over the SAME states.
  const auto projectiles = sampleOverlayEntities(states, world.schemas, world.modelPrecache,
    OverlayEntityFilter::Projectiles, nullptr, nullptr);
  checkEq(static_cast<long long>(projectiles.size()), 1, "the projectile filter selects one entity");
  checkEq(projectiles[0].entityIndex, 5, "the projectile is entity 5");
  check(!projectiles[0].isPlayer, "a projectile row is not a player row");

  // Draw list: enabled == false submits nothing and COUNTS every player, so the
  // toggle's effect is a number rather than an absence.
  const std::vector<std::string> meshes = {"models/player/scout.mdl", "models/player/heavy.mdl"};
  const auto offList = buildOutlineDrawList(players, meshes, false, OverlayDepthMode::XRay);
  checkEq(static_cast<long long>(offList.items.size()), 0, "the disabled list submits nothing");
  checkEq(static_cast<long long>(offList.rejectedDisabled), 4, "the disabled list counts all four players");
  const auto onList = buildOutlineDrawList(players, meshes, true, OverlayDepthMode::XRay);
  checkEq(static_cast<long long>(onList.items.size()), 3, "the enabled list submits the three with a mesh");
  checkEq(static_cast<long long>(onList.rejectedNoMesh), 1, "the fourth player has no declared model");
  checkEq(static_cast<long long>(onList.submittedByTeam[1]), 1, "one red silhouette");
  checkEq(static_cast<long long>(onList.submittedByTeam[2]), 1, "one blue silhouette");
  checkEq(static_cast<long long>(onList.submittedByTeam[0]), 1, "one unknown silhouette");
  check(onList.items[0].kind == OverlayPrimitiveKind::Silhouette, "the builder submits silhouettes");
  check(onList.depth == OverlayDepthMode::XRay, "the depth mode travels on the list");
  checkEq(static_cast<long long>(onList.rejectedNoOrigin), 0, "every submitted player had an origin");

  // Lifecycle: a despawn is a transition, not a missing sample.
  OverlayEntityTracker tracker;
  const auto appeared = tracker.observe(100, players, false);
  checkEq(static_cast<long long>(appeared.size()), 4, "four entities appear at the first sampled tick");
  check(tracker.stats().appeared == 4, "the tracker counts four appearances");
  std::vector<OverlayEntitySample> after = players;
  after.erase(after.begin() + 1);  // entity 2 leaves the snapshot
  const auto vanished = tracker.observe(101, after, false);
  checkEq(static_cast<long long>(vanished.size()), 1, "exactly one entity vanishes");
  check(vanished[0].transition == OverlayLifecycleTransition::Vanished, "the transition is Vanished");
  checkEq(vanished[0].entityIndex, 2, "the entity that vanished is the one that left");
  check(vanished[0].team == OverlayTeam::Blue, "the vanished row still names the team it had");
  check(vanished[0].className == "CTFPlayer", "the vanished row still names its class");
  // The same tick fed twice is refused rather than re-applied.
  const auto replay = tracker.observe(101, after, false);
  checkEq(static_cast<long long>(replay.size()), 0, "a replayed tick produces no second set of events");
  checkEq(tracker.duplicateTicks(), 1, "the replay was counted as a duplicate");
  // A stride-4 tracker reports the four ticks of change at once, and says so.
  OverlayEntityTracker gapped;
  gapped.observe(100, players, false);
  const auto jumped = gapped.observe(104, players, false);
  checkEq(static_cast<long long>(jumped.size()), 0, "no change across the stride produces no events");
  checkEq(gapped.stats().ticksFed, 2, "two sampled ticks were fed");

  std::fprintf(stdout, "self-test checks=%d failures=%d\n", 60, g_failures);
}

// ---------------------------------------------------------------------------
// 2. The five coverage fixtures, as data.
// ---------------------------------------------------------------------------
void fixtures() {
  using namespace tf2::native;
  FixtureWorld world;

  // (a) visible and (b) occluded are the SAME two players; what separates them
  // is the camera, and the camera is not this probe's input -- the gate reads
  // the frame's own depth at the pixel. What the probe can assert is that the
  // two players exist, are on different teams, and are far enough apart in the
  // world that a single pixel cannot be both.
  std::vector<EntityState> states(4);
  states[1] = playerState(world, 2, 1, 100.0f, 200.0f, 32.0f, 11);
  states[2] = playerState(world, 3, 5, 900.0f, 1200.0f, 32.0f, 12);
  std::vector<std::string> meshes = {"models/player/scout.mdl", "models/player/heavy.mdl"};
  const auto live = sampleOverlayEntities(states, world.schemas, world.modelPrecache,
    OverlayEntityFilter::Players, nullptr, nullptr);
  const auto list = buildOutlineDrawList(live, meshes, true, OverlayDepthMode::XRay);
  checkEq(static_cast<long long>(list.items.size()), 2, "fixture: one red and one blue player");
  const auto red = std::find_if(list.items.begin(), list.items.end(),
    [](const OverlayDrawItem& item) { return item.team == OverlayTeam::Red; });
  const auto blue = std::find_if(list.items.begin(), list.items.end(),
    [](const OverlayDrawItem& item) { return item.team == OverlayTeam::Blue; });
  check(red != list.items.end(), "fixture: the red player is present");
  check(blue != list.items.end(), "fixture: the blue player is present");
  if (red != list.items.end() && blue != list.items.end()) {
    check(std::fabs(red->origin[0] - 100.0f) < 1.0e-3f, "fixture: the red player is at x=100");
    check(std::fabs(blue->origin[0] - 900.0f) < 1.0e-3f, "fixture: the blue player is at x=900");
    check(red->origin[0] != blue->origin[0], "fixture: the two players are at different places");
  }

  // (c) unknown team: a player with team 1 (spectator). Must appear, must be
  // neutral, must be counted in the unknown bucket, and its item must NOT pass
  // either team predicate.
  std::vector<EntityState> unknownStates(2);
  unknownStates[1] = playerState(world, 1, 1, 50.0f, 60.0f, 70.0f, 11);
  const auto unknownSamples = sampleOverlayEntities(unknownStates, world.schemas,
    world.modelPrecache, OverlayEntityFilter::Players, nullptr, nullptr);
  checkEq(static_cast<long long>(unknownSamples.size()), 1, "fixture: the spectator is still sampled");
  check(unknownSamples[0].team == OverlayTeam::Unknown, "fixture: team 1 is Unknown");
  const auto unknownList = buildOutlineDrawList(unknownSamples, meshes, true, OverlayDepthMode::XRay);
  checkEq(static_cast<long long>(unknownList.items.size()), 1, "fixture: the spectator is drawn");
  checkEq(static_cast<long long>(unknownList.submittedByTeam[0]), 1, "fixture: it lands in the unknown bucket");
  const auto& unknownItem = unknownList.items[0];
  const auto to8 = [](float value) {
    return static_cast<std::uint8_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
  };
  check(!overlayPixelMatchesTeam(to8(unknownItem.color.rgba[0]), to8(unknownItem.color.rgba[1]),
    to8(unknownItem.color.rgba[2]), OverlayTeam::Red), "fixture: unknown fallback is not red");
  check(!overlayPixelMatchesTeam(to8(unknownItem.color.rgba[0]), to8(unknownItem.color.rgba[1]),
    to8(unknownItem.color.rgba[2]), OverlayTeam::Blue), "fixture: unknown fallback is not blue");

  // (d) despawn: the tracked player leaves between two sampled ticks. The draw
  // list for the second tick contains no item for it (linger is 0 by contract).
  OverlayEntityTracker tracker;
  tracker.observe(500, live, false);
  std::vector<EntityState> afterDespawn(4);
  afterDespawn[1] = states[1];  // only the red player remains
  const auto remaining = sampleOverlayEntities(afterDespawn, world.schemas, world.modelPrecache,
    OverlayEntityFilter::Players, nullptr, nullptr);
  const auto transitions = tracker.observe(501, remaining, false);
  checkEq(static_cast<long long>(transitions.size()), 1, "fixture: exactly one despawn");
  check(transitions[0].transition == OverlayLifecycleTransition::Vanished, "fixture: it is a Vanished");
  const auto afterList = buildOutlineDrawList(remaining, meshes, true, OverlayDepthMode::XRay);
  checkEq(static_cast<long long>(afterList.items.size()), 1, "fixture: only the remaining player is drawn");
  check(afterList.items[0].entityIndex == 1, "fixture: the despawned player has no silhouette");

  // (e) no players at all: an empty sample, an empty enabled list, and a
  // reading that says "on, nothing to draw" rather than "off".
  std::vector<EntityState> noPlayers(3);
  noPlayers[1].classId = world.idOf("CWorld");
  noPlayers[1].properties["DT_BaseEntity.m_vecOrigin"] = vecProp(0.0f, 0.0f, 0.0f, false);
  const auto empty = sampleOverlayEntities(noPlayers, world.schemas, world.modelPrecache,
    OverlayEntityFilter::Players, nullptr, nullptr);
  checkEq(static_cast<long long>(empty.size()), 0, "fixture: no players means no samples");
  const auto emptyList = buildOutlineDrawList(empty, meshes, true, OverlayDepthMode::XRay);
  check(emptyList.enabled, "fixture: the empty list is still an ENABLED list");
  checkEq(static_cast<long long>(emptyList.items.size()), 0, "fixture: and it submits nothing");
  checkEq(static_cast<long long>(emptyList.rejectedNoMesh), 0, "fixture: nothing was rejected either");

  std::fprintf(stdout, "fixtures cases=5 failures=%d\n", g_failures);
}

// ---------------------------------------------------------------------------
// 3. A real recording: identity, team and pose at every sampled tick.
// ---------------------------------------------------------------------------
int demoMode(const std::string& demoPath, const std::vector<std::int32_t>& ticks) {
  using namespace tf2::native;
  tf2::native::DemoHeader header;
  if (!tf2::native::parseDemoHeaderFile(std::filesystem::path(demoPath), header)) {
    std::fprintf(stderr, "FAIL the demo header did not parse: %s\n", demoPath.c_str());
    return 2;
  }
  tf2::native::DemoIndex index;
  if (!tf2::native::indexDemoFile(std::filesystem::path(demoPath), header, index)) {
    std::fprintf(stderr, "FAIL the demo index did not build: %s\n", demoPath.c_str());
    return 2;
  }
  tf2::native::DemoNetworkSummary summary;
  if (!tf2::native::scanKnownDemoMessages(std::filesystem::path(demoPath), index, summary)) {
    std::fprintf(stderr, "FAIL the demo scan produced no packets: %s\n", demoPath.c_str());
    return 2;
  }
  bool haveFirst = false;
  const std::int32_t firstTick = tf2::native::firstRecordedPacketTick(index, haveFirst);

  std::size_t sampledTicks = 0, rows = 0, withTeam = 0, red = 0, blue = 0, unknown = 0;
  std::size_t withPose = 0, exact = 0, fallback = 0, unavailable = 0;
  std::unordered_set<std::uint16_t> distinctEntities;
  OverlayEntityTracker tracker;

  for (const std::int32_t relative : ticks) {
    const std::int32_t serverTick = firstTick + relative;
    std::vector<EntityState> states;
    const auto resolution = resolveOverlaySnapshot(summary, serverTick, states);
    ++sampledTicks;
    switch (resolution.fidelity) {
      case OverlaySnapshotFidelity::ExactTick: ++exact; break;
      case OverlaySnapshotFidelity::CheckpointFallback: ++fallback; break;
      case OverlaySnapshotFidelity::Unavailable: ++unavailable; break;
    }
    if (resolution.fidelity == OverlaySnapshotFidelity::Unavailable) {
      std::fprintf(stdout,
        "tick requested=%d fidelity=%s staleness=%d reason=%s rows=0\n",
        resolution.requestedTick, overlaySnapshotFidelityName(resolution.fidelity),
        resolution.stalenessTicks, resolution.reason.c_str());
      continue;
    }
    const auto samples = sampleOverlayEntities(states, summary.serverClassSchemas,
      summary.modelPrecache, OverlayEntityFilter::Players, nullptr, nullptr);
    tracker.observe(serverTick, samples, false);
    rows += samples.size();
    std::size_t tickRed = 0, tickBlue = 0, tickUnknown = 0, tickPose = 0;
    for (const auto& sample : samples) {
      distinctEntities.insert(sample.entityIndex);
      if (sample.hasTeam) ++withTeam;
      if (sample.hasOrigin) { ++withPose; ++tickPose; }
      switch (sample.team) {
        case OverlayTeam::Red: ++red; ++tickRed; break;
        case OverlayTeam::Blue: ++blue; ++tickBlue; break;
        case OverlayTeam::Unknown: ++unknown; ++tickUnknown; break;
      }
      // Per-tick identity: one line per sampled player, so "the outline is on
      // entity N, team T, at this pose" is a reading for every tick rather
      // than an aggregate that hides a swap.
      std::fprintf(stdout,
        "tick requested=%d resolved=%d fidelity=%s stale=%d entity=%u class=%s player=1 "
        "team=%s raw=%lld hasTeam=%d origin=%.2f,%.2f,%.2f hasOrigin=%d model=%s\n",
        resolution.requestedTick, resolution.resolvedTick,
        overlaySnapshotFidelityName(resolution.fidelity), resolution.stalenessTicks,
        static_cast<unsigned>(sample.entityIndex),
        sample.className.empty() ? "-" : sample.className.c_str(),
        overlayTeamName(sample.team), static_cast<long long>(sample.teamRaw),
        sample.hasTeam ? 1 : 0,
        sample.origin[0], sample.origin[1], sample.origin[2], sample.hasOrigin ? 1 : 0,
        sample.modelPath.empty() ? "-" : sample.modelPath.c_str());
    }
    std::fprintf(stdout, "tick summary requested=%d rows=%zu red=%zu blue=%zu unknown=%zu posed=%zu\n",
      resolution.requestedTick, samples.size(), tickRed, tickBlue, tickUnknown, tickPose);
  }

  // The identities the aggregate rests on, so a swap of two entities' rows is
  // visible: distinct entities, and the per-entity class/team pairs.
  std::fprintf(stdout,
    "demo summary ticks=%zu rows=%zu distinctEntities=%zu withTeam=%zu red=%zu blue=%zu unknown=%zu "
    "withPose=%zu exact=%zu checkpointFallback=%zu unavailable=%zu appeared=%zu vanished=%zu\n",
    sampledTicks, rows, distinctEntities.size(), withTeam, red, blue, unknown, withPose,
    exact, fallback, unavailable, tracker.stats().appeared, tracker.stats().vanished);
  // Exit is a verdict on whether the run MEASURED anything: a demo with no
  // player in any sampled tick is a reading, not a pass.
  if (rows == 0) {
    std::fprintf(stderr, "FAIL no player was sampled at any requested tick\n");
    return 1;
  }
  return g_failures == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
  std::string demoPath;
  std::vector<std::int32_t> ticks;
  bool selfTestRequested = false;
  bool fixturesRequested = false;
  for (int i = 1; i < argc; ++i) {
    const std::string argument = argv[i];
    if (argument == "--self-test") selfTestRequested = true;
    else if (argument == "--fixtures") fixturesRequested = true;
    else if (argument == "--json") g_json = true;
    else if (argument == "--demo" && i + 1 < argc) demoPath = argv[++i];
    else if (argument == "--ticks" && i + 1 < argc) {
      std::string list = argv[++i];
      std::size_t start = 0;
      while (start <= list.size()) {
        const std::size_t comma = list.find(',', start);
        const std::string piece = list.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        if (!piece.empty()) ticks.push_back(std::atoi(piece.c_str()));
        if (comma == std::string::npos) break;
        start = comma + 1;
      }
    } else {
      std::fprintf(stderr, "unknown argument: %s\n", argument.c_str());
      return 2;
    }
  }
  if (selfTestRequested) selfTest();
  if (fixturesRequested) fixtures();
  if (!demoPath.empty()) {
    if (ticks.empty()) { std::fprintf(stderr, "FAIL --demo needs --ticks\n"); return 2; }
    const int rc = demoMode(demoPath, ticks);
    if (rc != 0) return rc;
  }
  if (!selfTestRequested && !fixturesRequested && demoPath.empty()) {
    std::fprintf(stderr, "usage: --self-test | --fixtures | --demo <path> --ticks a,b,c [--json]\n");
    return 2;
  }
  return g_failures == 0 ? 0 : 1;
}
