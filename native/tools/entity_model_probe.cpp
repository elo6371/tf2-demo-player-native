#include "asset_root.h"
#include "demo_header.h"
#include "entity_model.h"
#include "model_loader.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

int fail(const std::string& message) {
  std::cout << "{\"ok\":false,\"error\":\"" << message << "\"}\n";
  return 1;
}

// A path that names a class's first-person arms composite
// (models/weapons/c_models/c_*_arms/*_arms.mdl). Counted separately from the
// world-model route because the two are independent readings of the same claim:
// the route flag says which index the resolver chose, this says what the chosen
// path actually is. A weapon drawn with its arms model would keep the route
// reading honest-looking only if both were wrong in the same way, and they are
// not computed from each other.
bool isArmsPath(const std::string& path) {
  const auto slash = path.find_last_of("/\\");
  const std::string file = slash == std::string::npos ? path : path.substr(slash + 1);
  return file.size() > 9 && file.compare(file.size() - 9, 9, "_arms.mdl") == 0;
}

bool runSelfTest() {
  tf2::native::EntityState state;
  tf2::native::EntityPropertyValue origin;
  origin.type = tf2::native::SendPropType::Vector;
  origin.x = 100.0f; origin.y = 200.0f; origin.z = 32.0f;
  state.properties["DT_BaseEntity.m_vecOrigin"] = origin;
  tf2::native::EntityPropertyValue team;
  team.type = tf2::native::SendPropType::Int;
  team.intValue = 2;
  state.properties["DT_TFPlayerResource.m_iTeamNum"] = team;
  tf2::native::EntityPropertyValue playerClass;
  playerClass.type = tf2::native::SendPropType::Int;
  playerClass.intValue = 1;
  state.properties["DT_TFPlayer.m_iClass"] = playerClass;
  const auto transform = tf2::native::EntityModelResolver::extractTransform(state);
  if (!transform.hasOrigin || transform.origin[0] != 100.0f || transform.origin[2] != 32.0f) return false;
  if (!transform.hasTeam || transform.team != 2) return false;
  if (!transform.hasPlayerClass || transform.playerClass != 1) return false;
  if (tf2::native::EntityModelResolver::defaultPlayerModelPath(1) != "models/player/scout.mdl") return false;

  std::vector<tf2::native::AssetReference> malformed;
  tf2::native::AssetReference empty;
  empty.hasModelPath = true;
  malformed.push_back(empty);
  tf2::native::AssetReference bad;
  bad.hasModelPath = true;
  bad.modelPath = "models/not/a/real_model.mdl";
  bad.entityIndex = 3;
  malformed.push_back(bad);
  tf2::native::AssetRoot missingRoot;
  const auto missing = tf2::native::ModelLoader::buildRenderRequests(missingRoot, malformed, nullptr);
  if (missing.size() != 1) return false;
  if (missing[0].renderable) return false;

  std::vector<tf2::native::ModelRenderRequest> flood;
  flood.resize(3000);
  for (std::size_t i = 0; i < flood.size(); ++i) {
    flood[i].entityIndex = static_cast<std::uint16_t>(i > 0xffffu ? 0xffffu : i);
    flood[i].modelPath = "models/player/scout.mdl";
    flood[i].cacheKey = "models/player/scout.mdl#1";
  }
  std::vector<tf2::native::EntityState> states(4);
  states[3] = state;
  const auto bounded = tf2::native::EntityModelResolver::buildInstances(flood, states, {}, 2048);
  if (bounded.size() > tf2::native::EntityModelResolver::kMaxInstances) return false;

  const auto near = [](float actual, float expected) {
    return std::fabs(actual - expected) <= 1.0e-4f;
  };
  const auto applyPosition = [](const tf2::native::EntityModelInstanceRows& rows, float x, float y, float z, float out[3]) {
    const float point[4] = {x, y, z, 1.0f};
    for (int row = 0; row < 3; ++row) {
      out[row] = rows.positionRows[row * 4] * point[0] + rows.positionRows[row * 4 + 1] * point[1]
        + rows.positionRows[row * 4 + 2] * point[2] + rows.positionRows[row * 4 + 3] * point[3];
    }
  };
  const auto applyNormal = [](const tf2::native::EntityModelInstanceRows& rows, float x, float y, float z, float out[3]) {
    for (int row = 0; row < 3; ++row) {
      out[row] = rows.normalRows[row * 3] * x + rows.normalRows[row * 3 + 1] * y + rows.normalRows[row * 3 + 2] * z;
    }
  };
  tf2::native::EntityModelWorldMap world;
  world.centerX = 10.0f;
  world.centerY = 20.0f;
  world.minZ = 5.0f;
  world.horizontalScale = 0.01f;
  world.zScale = 0.02f;
  const float worldOrigin[3] = {100.0f, 200.0f, 50.0f};
  tf2::native::EntityModelInstanceRows rows;
  if (!tf2::native::buildEntityModelInstanceRows(worldOrigin, nullptr, false, world, rows)) return false;
  float mapped[3] = {};
  applyPosition(rows, 1.0f, 2.0f, 3.0f, mapped);
  if (!near(mapped[0], 0.91f) || !near(mapped[1], 1.82f) || !near(mapped[2], 1.06f)) return false;
  float normal[3] = {};
  applyNormal(rows, 0.0f, 1.0f, 0.0f, normal);
  if (!near(normal[0], 0.0f) || !near(normal[1], 1.0f) || !near(normal[2], 0.0f)) return false;
  if (!near(rows.positionRows[0], world.horizontalScale) || !near(rows.normalRows[0], 1.0f)) return false;

  const float yaw90[3] = {0.0f, 90.0f, 0.0f};
  const float yawOrigin[3] = {10.0f, 0.0f, 0.0f};
  tf2::native::EntityModelWorldMap unit;
  unit.horizontalScale = 1.0f;
  unit.zScale = 1.0f;
  tf2::native::EntityModelInstanceRows yawRows;
  if (!tf2::native::buildEntityModelInstanceRows(yawOrigin, yaw90, true, unit, yawRows)) return false;
  applyPosition(yawRows, 1.0f, 0.0f, 0.0f, mapped);
  if (!near(mapped[0], 10.0f) || !near(mapped[1], 1.0f) || !near(mapped[2], 0.1f)) return false;
  applyNormal(yawRows, 1.0f, 0.0f, 0.0f, normal);
  if (!near(normal[0], 0.0f) || !near(normal[1], 1.0f) || !near(normal[2], 0.0f)) return false;

  const float pitch90[3] = {90.0f, 0.0f, 0.0f};
  tf2::native::EntityModelInstanceRows pitchRows;
  const float zero[3] = {};
  if (!tf2::native::buildEntityModelInstanceRows(zero, pitch90, true, unit, pitchRows)) return false;
  applyPosition(pitchRows, 0.0f, 0.0f, 1.0f, mapped);
  if (!near(mapped[0], 1.0f) || !near(mapped[1], 0.0f) || !near(mapped[2], 0.1f)) return false;

  const float badOrigin[3] = {0.0f, 0.0f, std::numeric_limits<float>::quiet_NaN()};
  if (tf2::native::buildEntityModelInstanceRows(badOrigin, nullptr, false, unit, rows)) return false;

  // -------------------------------------------------------------------------
  // World-model wiring fixture.
  //
  // Every branch of the route is exercised by construction here rather than by
  // hoping a corpus demo happens to contain it. Ten synthetic entities against a
  // synthetic modelprecache table, then buildAssetReferenceList -- the same call
  // the real scan makes, not a re-implementation of it:
  //
  //   1  model  10 (arms)  world 11          -> names the weapon, not the arms
  //   2  model  12         world  0          -> the sentinel falls back
  //   3  model  12         world 99 undeclared -> unresolved, then falls back
  //   4  model   0         world 14          -> the route fills the gap
  //   5  model  12         world 0xFFFFFFEA  -> out of range, then falls back
  //   6  no model index at all, world 11     -> the snakewater shape
  //   7  model  12 view 12 world 11          -> a real view/model conflict
  //   8  model  12 view  0 world 11          -> an unset view slot
  //   9  model  10 (arms)  world  0          -> an arms path off the fallback
  //  10  model  10 (arms)  world 10          -> a weapon left naming its arms
  //
  // Entity 10 is the one the real demos must never produce: it is here so the
  // counter that has to stay zero on them is known to be able to fire.
  // -------------------------------------------------------------------------
  static const char* kArmsPath = "models/weapons/c_models/c_test_arms.mdl";
  static const char* kWeaponPath = "models/weapons/c_models/c_test_weapon.mdl";
  static const char* kPlayerPath = "models/player/scout.mdl";
  static const char* kOtherPath = "models/weapons/c_models/c_test_other.mdl";
  tf2::native::DemoNetworkSummary wiring;
  wiring.modelPrecache[10] = kArmsPath;
  wiring.modelPrecache[11] = kWeaponPath;
  wiring.modelPrecache[12] = kPlayerPath;
  wiring.modelPrecache[14] = kOtherPath;
  wiring.entityStates.resize(11);
  for (std::size_t i = 0; i < wiring.entityStates.size(); ++i) wiring.entityStates[i].classId = 0;
  const auto setIndex = [&wiring](std::size_t entity, const char* name, std::int64_t value) {
    tf2::native::EntityPropertyValue prop;
    prop.type = tf2::native::SendPropType::Int;
    prop.intValue = value;
    wiring.entityStates[entity].properties[name] = prop;
  };
  const char* kModelKey = "DT_BaseEntity.m_nModelIndex";
  const char* kWorldKey = "DT_BaseCombatWeapon.m_iWorldModelIndex";
  const char* kViewKey = "DT_BaseCombatWeapon.m_iViewModelIndex";
  setIndex(1, kModelKey, 10); setIndex(1, kWorldKey, 11); setIndex(1, kViewKey, 10);
  setIndex(2, kModelKey, 12); setIndex(2, kWorldKey, 0); setIndex(2, kViewKey, 0);
  setIndex(3, kModelKey, 12); setIndex(3, kWorldKey, 99); setIndex(3, kViewKey, 12);
  setIndex(4, kModelKey, 0); setIndex(4, kWorldKey, 14); setIndex(4, kViewKey, 0);
  setIndex(5, kModelKey, 12); setIndex(5, kWorldKey, 4294967274LL); setIndex(5, kViewKey, 12);
  setIndex(6, kWorldKey, 11); setIndex(6, kViewKey, 11);
  setIndex(7, kModelKey, 12); setIndex(7, kWorldKey, 11); setIndex(7, kViewKey, 11);
  setIndex(8, kModelKey, 12); setIndex(8, kWorldKey, 11); setIndex(8, kViewKey, 0);
  setIndex(9, kModelKey, 10); setIndex(9, kWorldKey, 0); setIndex(9, kViewKey, 10);
  setIndex(10, kModelKey, 10); setIndex(10, kWorldKey, 10); setIndex(10, kViewKey, 10);
  tf2::native::buildAssetReferenceList(wiring, wiring.assetReferences);
  const auto& refs = wiring.assetReferences;
  std::size_t fixtureArmsWeapon = 0;
  for (const auto& reference : refs) {
    if (reference.hasWorldModelIndex && reference.worldModelIndex > 0
        && isArmsPath(reference.modelPath)) {
      ++fixtureArmsWeapon;
    }
  }
  // Printed before the assertions, not after: when one of them fires, this line is
  // the only place the actual numbers appear, and a diagnostic that only prints on
  // success is a diagnostic that is never there when it is needed.
  std::fprintf(stderr,
               "weapon-wiring-fixture refs=%zu known=%zu resolved=%zu zero=%zu unresolved=%zu "
               "outOfRange=%zu unresolvedMax=%lld onlyWorld=%zu fromWorld=%zu agrees=%zu "
               "viewZero=%zu differs=%zu armsWeapon=%zu\n",
               refs.size(), wiring.assetWorldModelIndexKnown, wiring.assetWorldModelIndexResolved,
               wiring.assetWorldModelIndexZero, wiring.assetWorldModelIndexUnresolved,
               wiring.assetWorldModelIndexOutOfRange,
               static_cast<long long>(wiring.assetWorldModelIndexUnresolvedMax),
               wiring.assetModelPathWorldModelOnly, wiring.assetModelPathFromWorldModelIndex,
               wiring.assetWeaponViewModelIndexAgrees, wiring.assetWeaponViewModelIndexZero,
               wiring.assetWeaponViewModelIndexDiffers, fixtureArmsWeapon);
  for (const auto& reference : refs) {
    std::fprintf(stderr,
                 "  fixture entity=%u modelIndex=%lld viewModelIndex=%lld worldModelIndex=%lld path=%s source=%s\n",
                 static_cast<unsigned>(reference.entityIndex),
                 static_cast<long long>(reference.modelIndex),
                 static_cast<long long>(reference.viewModelIndex),
                 static_cast<long long>(reference.worldModelIndex),
                 reference.modelPath.empty() ? "<none>" : reference.modelPath.c_str(),
                 reference.modelPathFromWorldModelIndex ? "world" : "model");
  }
  std::fflush(stderr);
  if (refs.size() != wiring.entityStates.size()) return false;
  if (wiring.assetWorldModelIndexKnown != 10) return false;
  if (wiring.assetWorldModelIndexResolved != 6) return false;
  if (wiring.assetWorldModelIndexZero != 2) return false;
  if (wiring.assetWorldModelIndexUnresolved != 1) return false;
  if (wiring.assetWorldModelIndexOutOfRange != 1) return false;
  if (wiring.assetWorldModelIndexUnresolvedMax != 99) return false;
  if (wiring.assetModelPathFromWorldModelIndex != 6) return false;
  if (wiring.assetModelPathWorldModelOnly != 2) return false;
  if (wiring.assetWeaponViewModelIndexAgrees != 6) return false;
  if (wiring.assetWeaponViewModelIndexZero != 2) return false;
  if (wiring.assetWeaponViewModelIndexDiffers != 1) return false;
  // Exactly one, and only because entity 10 was built to be that case. This is
  // what makes "armsWeapon is 0 on both real demos" a reading rather than a
  // counter nobody has ever seen move.
  if (fixtureArmsWeapon != 1) return false;
  // The second claim the fixture carries: the route is what decided each path.
  // A count that came out right off the wrong path would be invisible otherwise.
  if (std::string(refs[1].modelPath) != kWeaponPath || !refs[1].modelPathFromWorldModelIndex) return false;
  if (std::string(refs[2].modelPath) != kPlayerPath || refs[2].modelPathFromWorldModelIndex) return false;
  if (std::string(refs[3].modelPath) != kPlayerPath || refs[3].modelPathFromWorldModelIndex) return false;
  if (std::string(refs[4].modelPath) != kOtherPath || !refs[4].modelPathFromWorldModelIndex) return false;
  if (std::string(refs[5].modelPath) != kPlayerPath || refs[5].modelPathFromWorldModelIndex) return false;
  if (std::string(refs[6].modelPath) != kWeaponPath || !refs[6].modelPathFromWorldModelIndex) return false;
  if (std::string(refs[9].modelPath) != kArmsPath || refs[9].modelPathFromWorldModelIndex) return false;
  if (std::string(refs[10].modelPath) != kArmsPath || !refs[10].modelPathFromWorldModelIndex) return false;

  // -------------------------------------------------------------------------
  // Observer-focus fixture.
  //
  // resolveObserverFocus has one job: say whether the camera belongs on
  // somebody else, and be right about when it does not. Every way of not
  // having a target is built here rather than trusted to the corpus, because
  // the corpus only exercises the two branches that occur (in-eye with a
  // present target, and mode 0):
  //
  //   1  mode 4, target h(2,867)      -> follows; serial decodes to 867
  //   2  mode 0                       -> playing, nothing to follow
  //   3  mode 4, target h(1000,1)     -> index past the snapshot
  //   4  mode 4, target h(5,1)        -> slot 5 holds nothing (classId -1)
  //   6  mode 1, target h(2,869)      -> deathcam: a scripted move, NOT followed
  //   7  no m_iObserverMode           -> mode absent is not mode 0
  //   8  mode 4, no target            -> nothing to follow
  //   9  mode 4, target h(2047,3)     -> INVALID_EHANDLE_INDEX (the field's init)
  //  10  mode 5, target 0xFFFFFFFF    -> the wire sentinel
  //  11  mode 5, target h(2,868)      -> chase follows too
  //
  // Entity 6 is the case worth guarding: if deathcam were ever folded into the
  // follow set, the corpus would happily agree (both demos die at least once),
  // so only this fixture can say the set is exactly {in-eye, chase}.
  // -------------------------------------------------------------------------
  {
    std::vector<tf2::native::EntityState> obsStates(12);
    for (auto& obsState : obsStates) obsState.classId = 0;
    const auto setObs = [&obsStates](std::size_t entity, const char* name, std::int64_t value) {
      tf2::native::EntityPropertyValue prop;
      prop.type = tf2::native::SendPropType::Int;
      prop.intValue = value;
      obsStates[entity].properties[name] = prop;
    };
    const auto handle = [](std::uint32_t index, std::uint32_t serial) {
      return static_cast<std::int64_t>((serial << 11) | index);
    };
    const char* kModeKey = "DT_BasePlayer.m_iObserverMode";
    const char* kTargetKey = "DT_BasePlayer.m_hObserverTarget";
    setObs(1, kModeKey, 4); setObs(1, kTargetKey, handle(2, 867));
    setObs(2, kModeKey, 0);
    setObs(3, kModeKey, 4); setObs(3, kTargetKey, handle(1000, 1));
    setObs(4, kModeKey, 4); setObs(4, kTargetKey, handle(5, 1));
    setObs(5, kModeKey, 4); setObs(5, kTargetKey, handle(6, 1));
    obsStates[5].classId = -1; // "(1000, 1)"-style out-of-range guard: slot empty
    setObs(6, kModeKey, 1); setObs(6, kTargetKey, handle(2, 869));
    setObs(7, kTargetKey, handle(2, 870));
    setObs(8, kModeKey, 4);
    setObs(9, kModeKey, 4); setObs(9, kTargetKey, handle(2047, 3));
    setObs(10, kModeKey, 5); setObs(10, kTargetKey, 0xFFFFFFFFLL);
    setObs(11, kModeKey, 5); setObs(11, kTargetKey, handle(2, 868));

    std::size_t fixtureFollows = 0, fixturePresent = 0, fixtureInRange = 0;
    std::size_t fixtureOutOfRange = 0, fixtureMissing = 0, fixtureWithMode = 0;
    std::size_t fixtureNonZero = 0, fixtureHasTarget = 0;
    std::uint16_t fixtureSerial = 0;
    for (std::size_t entity = 0; entity < obsStates.size(); ++entity) {
      if (obsStates[entity].classId < 0) continue;
      const auto resolution = tf2::native::resolveObserverFocus(obsStates[entity], obsStates);
      if (resolution.hasMode) {
        ++fixtureWithMode;
        if (resolution.mode != 0) ++fixtureNonZero;
      }
      if (resolution.hasTarget) {
        ++fixtureHasTarget;
        if (resolution.targetInRange) {
          ++fixtureInRange;
          if (resolution.targetPresent) ++fixturePresent;
          else ++fixtureMissing;
        } else {
          ++fixtureOutOfRange;
        }
      }
      if (resolution.followsTarget) ++fixtureFollows;
      if (entity == 1) fixtureSerial = resolution.targetSerial;
      std::fprintf(stderr,
                   "  observer-fixture entity=%zu mode=%lld targetIndex=%u serial=%u inRange=%d "
                   "present=%d follows=%d\n",
                   entity, static_cast<long long>(resolution.mode),
                   static_cast<unsigned>(resolution.targetIndex),
                   static_cast<unsigned>(resolution.targetSerial),
                   resolution.targetInRange ? 1 : 0, resolution.targetPresent ? 1 : 0,
                   resolution.followsTarget ? 1 : 0);
    }
    std::fprintf(stderr,
                 "observer-focus-fixture cases=%zu withMode=%zu modeNonZero=%zu hasTarget=%zu "
                 "inRange=%zu present=%zu missing=%zu outOfRange=%zu follows=%zu serial=%u\n",
                 obsStates.size() - 1, fixtureWithMode, fixtureNonZero, fixtureHasTarget,
                 fixtureInRange, fixturePresent, fixtureMissing, fixtureOutOfRange,
                 fixtureFollows, static_cast<unsigned>(fixtureSerial));
    std::fflush(stderr);
    if (fixtureWithMode != 9) return false;
    if (fixtureNonZero != 8) return false;
    if (fixtureHasTarget != 8) return false;
    if (fixtureInRange != 5) return false;
    if (fixturePresent != 4) return false;
    if (fixtureMissing != 1) return false;
    if (fixtureOutOfRange != 3) return false;
    // Exactly the two built to be followed -- in-eye at a present target, chase
    // at a present target. If this ever reads 3, deathcam leaked into the set.
    if (fixtureFollows != 2) return false;
    if (fixtureSerial != 867) return false;
    // And per-case, because a counter that is right for the wrong pair of cases
    // is exactly what the count alone cannot see.
    const auto focusAt = [&obsStates](std::size_t entity) {
      return tf2::native::resolveObserverFocus(obsStates[entity], obsStates);
    };
    if (!focusAt(1).followsTarget || focusAt(1).targetIndex != 2) return false;
    if (focusAt(2).followsTarget) return false;
    if (focusAt(3).followsTarget || focusAt(3).targetInRange) return false;
    if (focusAt(4).followsTarget || !focusAt(4).targetInRange || focusAt(4).targetPresent) return false;
    if (focusAt(6).followsTarget) return false;
    if (focusAt(6).mode != 1) return false;
    if (focusAt(7).followsTarget || focusAt(7).hasMode) return false;
    if (focusAt(8).followsTarget || focusAt(8).hasTarget) return false;
    if (focusAt(9).followsTarget || focusAt(9).targetInRange) return false;
    if (focusAt(10).followsTarget || focusAt(10).targetInRange) return false;
    if (!focusAt(11).followsTarget || focusAt(11).mode != 5) return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// Per-tick displacement reading.
//
// Until now nothing measured entity motion: entity_model_probe took one static
// snapshot, so "entities are at the right place" was an assertion with no
// reading behind it. This walks N ticks across the demo, rebuilds the entity
// snapshot at each one through the same query the renderer uses, and reports how
// far each entity actually travelled.
//
// It is opt-in (--trajectory N) so the default output stays byte-identical to
// what evidence/probe-baseline freezes.
// ---------------------------------------------------------------------------

using Origin = std::array<float, 3>;

// Mirrors EntityModelResolver's private lookup: a property whose name ends in
// ".m_vecOrigin". Deliberately not exported from the library, because the point
// of this probe is to observe the decoder's raw output rather than to re-use the
// resolver's own selection logic.
const tf2::native::EntityPropertyValue* rawOriginProperty(const tf2::native::EntityState& state) {
  constexpr std::size_t kSuffixLength = 11;  // "m_vecOrigin"
  for (const auto& [name, value] : state.properties) {
    if (name.size() > kSuffixLength
        && name.compare(name.size() - kSuffixLength, kSuffixLength, "m_vecOrigin") == 0
        && name[name.size() - kSuffixLength - 1] == '.') {
      return &value;
    }
  }
  return nullptr;
}

std::uint64_t fnv1a(std::uint64_t hash, std::uint64_t value) {
  for (int byte = 0; byte < 8; ++byte) {
    hash ^= (value >> (byte * 8)) & 0xffu;
    hash *= 1099511628211ull;
  }
  return hash;
}

struct TrajectorySample {
  std::int32_t tick = 0;
  std::vector<std::pair<std::uint16_t, Origin>> origins;
};

struct TrajectoryStats {
  std::int32_t firstTick = 0;
  std::int32_t lastTick = 0;
  std::size_t ticks = 0;
  std::size_t available = 0;
  std::size_t checkpoint = 0;
  std::size_t unavailable = 0;
  std::size_t samples = 0;        // (entity, tick) pairs carrying a finite origin
  std::size_t entities = 0;       // distinct entity indices carrying a finite origin
  std::size_t moved = 0;          // entities whose origin changed between two samples
  std::size_t noOriginProperty = 0;
  std::size_t originNotFinite = 0;
  std::size_t zZero = 0;
  std::size_t zNonZero = 0;
  std::size_t rawZZero = 0;        // control: VectorXY z as decoded (0 for players)
  std::size_t rawZNonZero = 0;
  std::size_t zSiblingMissing = 0; // origin rendered with a guessed z of 0
  double totalDistance = 0.0;
  double maxStep = 0.0;
  double minZ = 0.0;
  double maxZ = 0.0;
  bool anyZ = false;
  std::uint64_t digest = 1469598103934665603ull;
  std::vector<TrajectorySample> trace;
};

void measureTrajectory(const tf2::native::DemoNetworkSummary& summary, std::size_t sampleCount,
                       TrajectoryStats& stats) {
  std::int32_t firstTick = 0;
  std::int32_t lastTick = 0;
  bool haveWindow = false;
  const auto extend = [&](std::int32_t tick) {
    if (!haveWindow) { firstTick = tick; lastTick = tick; haveWindow = true; return; }
    firstTick = std::min(firstTick, tick);
    lastTick = std::max(lastTick, tick);
  };
  for (const auto& checkpoint : summary.entityHistoryArchive) extend(checkpoint.tick);
  for (const auto& checkpoint : summary.entityHistoryCheckpoints) extend(checkpoint.tick);
  if (!haveWindow || lastTick <= firstTick || sampleCount == 0) return;

  stats.firstTick = firstTick;
  stats.lastTick = lastTick;
  stats.ticks = sampleCount;

  std::unordered_map<std::uint16_t, Origin> previous;
  std::unordered_set<std::uint16_t> moved;
  std::vector<tf2::native::EntityState> states;
  for (std::size_t i = 0; i < sampleCount; ++i) {
    const double fraction = sampleCount == 1 ? 0.0
      : static_cast<double>(i) / static_cast<double>(sampleCount - 1);
    const std::int32_t tick = firstTick + static_cast<std::int32_t>(
      std::lround(fraction * static_cast<double>(lastTick - firstTick)));
    const auto status = tf2::native::queryEntitySnapshotAtOrBeforeTick(summary, tick, states);
    if (status == tf2::native::EntitySnapshotQueryStatus::Available) ++stats.available;
    else if (status == tf2::native::EntitySnapshotQueryStatus::Checkpoint) ++stats.checkpoint;
    else { ++stats.unavailable; continue; }

    TrajectorySample sample;
    sample.tick = tick;
    for (std::size_t entity = 0; entity < states.size(); ++entity) {
      if (states[entity].classId < 0) continue;
      // Two readings of the same entity, deliberately side by side:
      //
      //   raw      -- the VectorXY property exactly as the decoder wrote it.
      //               Its z is structurally 0 for a player, because the wire
      //               format for VectorXY carries x and y only. This is the
      //               signature of the bug, so it is kept as a control.
      //   rendered -- what EntityModelResolver::extractTransform hands the
      //               renderer, i.e. the number that actually matters.
      //
      // A reading that only reports the rendered value cannot tell you whether
      // the fix is doing anything; a reading that only reports the raw value
      // cannot tell you whether the renderer is affected.
      if (const auto* raw = rawOriginProperty(states[entity]); raw != nullptr) {
        if (raw->z == 0.0f) ++stats.rawZZero; else ++stats.rawZNonZero;
      }
      const auto transform = tf2::native::EntityModelResolver::extractTransform(states[entity]);
      if (!transform.hasOrigin) { ++stats.noOriginProperty; continue; }
      if (!std::isfinite(transform.origin[0]) || !std::isfinite(transform.origin[1])
          || !std::isfinite(transform.origin[2])) {
        ++stats.originNotFinite;
        continue;
      }
      const auto index = static_cast<std::uint16_t>(entity);
      const Origin origin{transform.origin[0], transform.origin[1], transform.origin[2]};
      if (transform.diagnostic == "origin-z-sibling-missing") ++stats.zSiblingMissing;
      ++stats.samples;
      if (origin[2] == 0.0f) ++stats.zZero; else ++stats.zNonZero;
      if (!stats.anyZ) { stats.minZ = origin[2]; stats.maxZ = origin[2]; stats.anyZ = true; }
      else {
        stats.minZ = std::min<double>(stats.minZ, origin[2]);
        stats.maxZ = std::max<double>(stats.maxZ, origin[2]);
      }
      const auto seen = previous.find(index);
      if (seen != previous.end()) {
        const double dx = static_cast<double>(origin[0]) - seen->second[0];
        const double dy = static_cast<double>(origin[1]) - seen->second[1];
        const double dz = static_cast<double>(origin[2]) - seen->second[2];
        const double step = std::sqrt(dx * dx + dy * dy + dz * dz);
        stats.totalDistance += step;
        stats.maxStep = std::max(stats.maxStep, step);
        if (step > 1.0e-3) moved.insert(index);
      }
      previous[index] = origin;
      stats.digest = fnv1a(stats.digest, index);
      stats.digest = fnv1a(stats.digest, static_cast<std::uint64_t>(
        static_cast<std::int64_t>(std::lround(origin[0] * 1000.0f))));
      stats.digest = fnv1a(stats.digest, static_cast<std::uint64_t>(
        static_cast<std::int64_t>(std::lround(origin[1] * 1000.0f))));
      stats.digest = fnv1a(stats.digest, static_cast<std::uint64_t>(
        static_cast<std::int64_t>(std::lround(origin[2] * 1000.0f))));
      sample.origins.emplace_back(index, origin);
    }
    stats.trace.push_back(std::move(sample));
  }
  stats.entities = previous.size();
  stats.moved = moved.size();
}

// The trace the oracle check aligns against: the entity that moved furthest.
void printTrajectoryDump(const TrajectoryStats& stats) {
  if (stats.trace.empty()) return;
  std::unordered_map<std::uint16_t, double> travelled;
  for (std::size_t i = 1; i < stats.trace.size(); ++i) {
    for (const auto& [index, origin] : stats.trace[i].origins) {
      for (const auto& [previousIndex, previousOrigin] : stats.trace[i - 1].origins) {
        if (previousIndex != index) continue;
        const double dx = static_cast<double>(origin[0]) - previousOrigin[0];
        const double dy = static_cast<double>(origin[1]) - previousOrigin[1];
        const double dz = static_cast<double>(origin[2]) - previousOrigin[2];
        travelled[index] += std::sqrt(dx * dx + dy * dy + dz * dz);
        break;
      }
    }
  }
  std::uint16_t best = 0;
  double bestDistance = -1.0;
  for (const auto& [index, distance] : travelled) {
    if (distance > bestDistance) { bestDistance = distance; best = index; }
  }
  // Diagnostics go to stderr; stdout stays a single parseable JSON object.
  std::fprintf(stderr, "trajectory-dump entity=%u travelled=%.3f\n", best, bestDistance);
  for (const auto& sample : stats.trace) {
    for (const auto& [index, origin] : sample.origins) {
      if (index != best) continue;
      std::fprintf(stderr, "traj tick=%d entity=%u x=%.6f y=%.6f z=%.6f\n",
                   sample.tick, index, origin[0], origin[1], origin[2]);
      break;
    }
  }
  std::fflush(stderr);
}

// Every property whose name carries ".m_vecOrigin", with its owner table, for
// one tick. A player carries four of them:
//   DT_TFLocalPlayerExclusive.m_vecOrigin      VectorXY (full precision x/y)
//   DT_TFLocalPlayerExclusive.m_vecOrigin[2]   Float    (full precision z)
//   DT_TFNonLocalPlayerExclusive.m_vecOrigin   VectorXY (quantized x/y)
//   DT_TFNonLocalPlayerExclusive.m_vecOrigin[2] Float   (quantized z)
// so printing all four is what makes both the z split and the Local/NonLocal
// choice observable instead of assumed. This is the shape the Rust oracle
// prints, so the two can be diffed.
//
// The match is a prefix match, not a suffix match: an earlier version required
// the name to *end* with "m_vecOrigin", which silently excluded the "[2]" float
// and made the decoder look like it was dropping a property it was in fact
// keeping. An instrument that cannot observe the failure it is looking for is
// worse than no instrument.
bool isOriginProperty(const std::string& name) {
  constexpr const char* kNeedle = ".m_vecOrigin";
  constexpr std::size_t kNeedleLength = 12;
  const auto at = name.rfind(kNeedle);
  if (at == std::string::npos) return false;
  const std::string rest = name.substr(at + kNeedleLength);
  if (rest.empty()) return true;
  return rest.size() >= 2 && rest.front() == '[' && rest.back() == ']';
}

// Comma-separated tick list, e.g. "56148,70760". Empty tokens are skipped so a
// trailing comma is not a parse error.
void parseTickList(const std::string& list, std::vector<std::int32_t>& out) {
  std::size_t start = 0;
  while (start <= list.size()) {
    const auto comma = list.find(',', start);
    const auto token = list.substr(start, comma == std::string::npos ? comma : comma - start);
    if (!token.empty()) out.push_back(static_cast<std::int32_t>(std::strtol(token.c_str(), nullptr, 10)));
    if (comma == std::string::npos) break;
    start = comma + 1;
  }
}

// How far apart the retained snapshots are, and therefore how stale the answer
// to "what did the world look like at tick T" can be. The query interface
// resolves a tick to the newest checkpoint at or before it, so the *gap* between
// consecutive checkpoints is the positional error a renderer inherits. Reporting
// the gap distribution turns "the entity positions look wrong" into a number.
struct HistoryStats {
  std::size_t archiveCount = 0;
  std::int32_t archiveFirstTick = 0;
  std::int32_t archiveLastTick = 0;
  std::int32_t archiveMaxGap = 0;
  std::int32_t archiveMedianGap = 0;
  std::size_t liveCheckpointCount = 0;
  std::int32_t liveFirstTick = 0;
  std::int32_t liveLastTick = 0;
  std::int32_t liveMaxGap = 0;
  std::size_t packetsRetained = 0;
  std::size_t eventsRetained = 0;
  std::size_t droppedPackets = 0;
  std::size_t flushes = 0;
  std::int32_t firstPacketTick = 0;
  std::int32_t lastPacketTick = 0;
};

std::int32_t medianGap(const std::vector<std::int32_t>& ticks) {
  if (ticks.size() < 2) return 0;
  std::vector<std::int32_t> gaps;
  gaps.reserve(ticks.size() - 1);
  for (std::size_t i = 1; i < ticks.size(); ++i) gaps.push_back(ticks[i] - ticks[i - 1]);
  std::sort(gaps.begin(), gaps.end());
  return gaps[gaps.size() / 2];
}

void measureHistory(const tf2::native::DemoNetworkSummary& summary, HistoryStats& stats) {
  std::vector<std::int32_t> archiveTicks;
  archiveTicks.reserve(summary.entityHistoryArchive.size());
  for (const auto& checkpoint : summary.entityHistoryArchive) archiveTicks.push_back(checkpoint.tick);
  stats.archiveCount = archiveTicks.size();
  if (!archiveTicks.empty()) {
    stats.archiveFirstTick = archiveTicks.front();
    stats.archiveLastTick = archiveTicks.back();
    for (std::size_t i = 1; i < archiveTicks.size(); ++i) {
      stats.archiveMaxGap = std::max(stats.archiveMaxGap, archiveTicks[i] - archiveTicks[i - 1]);
    }
    stats.archiveMedianGap = medianGap(archiveTicks);
  }
  std::vector<std::int32_t> liveTicks;
  liveTicks.reserve(summary.entityHistoryCheckpoints.size());
  for (const auto& checkpoint : summary.entityHistoryCheckpoints) liveTicks.push_back(checkpoint.tick);
  stats.liveCheckpointCount = liveTicks.size();
  if (!liveTicks.empty()) {
    stats.liveFirstTick = liveTicks.front();
    stats.liveLastTick = liveTicks.back();
    for (std::size_t i = 1; i < liveTicks.size(); ++i) {
      stats.liveMaxGap = std::max(stats.liveMaxGap, liveTicks[i] - liveTicks[i - 1]);
    }
  }
  stats.packetsRetained = summary.entityHistoryPackets.size();
  stats.eventsRetained = summary.entityHistoryEvents.size();
  stats.droppedPackets = summary.entityHistoryDroppedPackets;
  stats.flushes = summary.entityHistoryFlushes;
  if (!summary.entityHistoryPackets.empty()) {
    stats.firstPacketTick = summary.entityHistoryPackets.front().tick;
    stats.lastPacketTick = summary.entityHistoryPackets.back().tick;
  }
}

// ---------------------------------------------------------------------------
// What the retention policy costs, and what it buys.
//
// `--history-stats` reports the retained tick list; from that a reader can see the
// gap distribution but not what holding it costs, and the budget is exactly the
// trade-off being made. Two derived readings:
//
//   approxBytes -- a checkpoint owns the class-index vector, a dense vector of
//     EntityState, and one unordered_map per occupied entity whose nodes own a
//     heap copy of the property key (the composed names are longer than the
//     15-byte small-string buffer, so every key allocates). Counted from the
//     containers themselves plus one link pointer and one allocation header per
//     node. This is a deterministic lower bound, not a working-set measurement --
//     it can be re-derived from this file, which a profiler reading cannot.
//
//   retainedGap -- with `resolvedTick` now reported by the query, staleness is a
//     reading rather than an inference: the worst staleness over the sampled ticks
//     is printed next to the worst gap between retained checkpoints, and on a
//     healthy policy they agree with the gap list.
// ---------------------------------------------------------------------------
struct HistoryCoverage {
  std::int32_t firstTick = 0;
  std::int32_t lastTick = 0;
  std::size_t archiveCount = 0;
  std::size_t archiveBytes = 0;
  std::size_t liveCount = 0;
  std::size_t liveBytes = 0;
  std::size_t packetBytes = 0;
  std::size_t eventBytes = 0;
  std::size_t retained = 0;
  std::size_t distinctTicks = 0;
  std::int32_t worstGap = 0;
  std::int32_t worstGapFrom = 0;
  std::int32_t worstGapTo = 0;
  std::int32_t medianRetainedGap = 0;
  std::size_t samples = 0;
  std::size_t exact = 0;
  std::size_t checkpoint = 0;
  std::size_t unavailable = 0;
  std::int32_t worstStaleness = -1;
  std::int32_t worstStalenessAt = 0;
  std::uint64_t stalenessSum = 0;
};

// Per property, on top of the key bytes: the map node's forward link, the
// allocation header, the pair's std::string header and the value itself.
constexpr unsigned long long kHistoryPerEntryFixed =
    sizeof(std::string) + sizeof(tf2::native::EntityPropertyValue) + 3 * sizeof(void*);

unsigned long long entityStateApproxBytes(const tf2::native::EntityState& state) {
  if (state.properties.empty()) return 0;
  unsigned long long bytes =
      static_cast<unsigned long long>(state.properties.bucket_count()) * sizeof(void*);
  for (const auto& entry : state.properties) {
    bytes += kHistoryPerEntryFixed + entry.first.size() + 1;
  }
  return bytes;
}

unsigned long long checkpointApproxBytes(const tf2::native::EntityHistoryCheckpoint& checkpoint) {
  unsigned long long bytes =
      static_cast<unsigned long long>(checkpoint.classByIndex.capacity()) * sizeof(std::int32_t) +
      static_cast<unsigned long long>(checkpoint.states.capacity()) * sizeof(tf2::native::EntityState);
  for (const auto& state : checkpoint.states) bytes += entityStateApproxBytes(state);
  return bytes;
}

void measureHistoryCoverage(const tf2::native::DemoNetworkSummary& summary,
                            std::size_t sampleCount, HistoryCoverage& coverage) {
  std::vector<std::int32_t> retained;
  retained.reserve(summary.entityHistoryArchive.size() + summary.entityHistoryCheckpoints.size());
  for (const auto& checkpoint : summary.entityHistoryArchive) {
    retained.push_back(checkpoint.tick);
    coverage.archiveBytes += checkpointApproxBytes(checkpoint);
  }
  coverage.archiveCount = summary.entityHistoryArchive.size();
  for (const auto& checkpoint : summary.entityHistoryCheckpoints) {
    retained.push_back(checkpoint.tick);
    coverage.liveBytes += checkpointApproxBytes(checkpoint);
  }
  coverage.liveCount = summary.entityHistoryCheckpoints.size();
  // The retained event log is what makes a tick inside the window exact instead
  // of merely near; its cost is reported next to the checkpoints' so the two can
  // be traded against each other.
  coverage.packetBytes = summary.entityHistoryPackets.size() * sizeof(tf2::native::EntityHistoryPacket);
  for (const auto& event : summary.entityHistoryEvents) {
    coverage.eventBytes += sizeof(tf2::native::EntityHistoryEvent);
    coverage.eventBytes += event.changes.size() * sizeof(tf2::native::EntityPropChange);
    if (event.fullState) coverage.eventBytes += entityStateApproxBytes(event.state);
  }
  coverage.retained = retained.size();
  if (retained.size() < 2) return;
  coverage.firstTick = retained.front();
  coverage.lastTick = retained.back();
  std::vector<std::int32_t> sorted = retained;
  std::sort(sorted.begin(), sorted.end());
  coverage.distinctTicks = static_cast<std::size_t>(
      std::unique(sorted.begin(), sorted.end()) - sorted.begin());
  std::vector<std::int32_t> gaps;
  gaps.reserve(retained.size() - 1);
  for (std::size_t i = 1; i < retained.size(); ++i) {
    const std::int32_t gap = retained[i] - retained[i - 1];
    gaps.push_back(gap);
    if (gap > coverage.worstGap) {
      coverage.worstGap = gap;
      coverage.worstGapFrom = retained[i - 1];
      coverage.worstGapTo = retained[i];
    }
  }
  std::sort(gaps.begin(), gaps.end());
  coverage.medianRetainedGap = gaps[gaps.size() / 2];
  if (coverage.lastTick <= coverage.firstTick || sampleCount == 0) return;

  coverage.samples = sampleCount;
  std::vector<tf2::native::EntityState> states;
  for (std::size_t i = 0; i < sampleCount; ++i) {
    const double fraction = sampleCount == 1 ? 0.0
        : static_cast<double>(i) / static_cast<double>(sampleCount - 1);
    const std::int32_t tick = coverage.firstTick + static_cast<std::int32_t>(
        std::lround(fraction * static_cast<double>(coverage.lastTick - coverage.firstTick)));
    std::int32_t resolved = tick;
    const auto status = tf2::native::queryEntitySnapshotAtOrBeforeTick(summary, tick, states, &resolved);
    if (status == tf2::native::EntitySnapshotQueryStatus::Available) ++coverage.exact;
    else if (status == tf2::native::EntitySnapshotQueryStatus::Checkpoint) ++coverage.checkpoint;
    else { ++coverage.unavailable; continue; }
    const std::int32_t staleness = tick - resolved;
    if (staleness < 0) continue;
    coverage.stalenessSum += static_cast<std::uint64_t>(staleness);
    if (staleness > coverage.worstStaleness) {
      coverage.worstStaleness = staleness;
      coverage.worstStalenessAt = tick;
    }
  }
}

const char* snapshotStatusName(tf2::native::EntitySnapshotQueryStatus status) {
  switch (status) {
    case tf2::native::EntitySnapshotQueryStatus::Available: return "available";
    case tf2::native::EntitySnapshotQueryStatus::Checkpoint: return "checkpoint";
    case tf2::native::EntitySnapshotQueryStatus::NoHistory: return "no-history";
    case tf2::native::EntitySnapshotQueryStatus::TickBeforeHistory: return "before-window";
    case tf2::native::EntitySnapshotQueryStatus::Gap: return "gap";
    case tf2::native::EntitySnapshotQueryStatus::DeltaBaseMissing: return "delta-base-missing";
  }
  return "unavailable";
}

// All properties of one entity at the given ticks. This is the oracle-alignment
// instrument: `m_nTickBase` is a server tick carried in the player's own state,
// so finding the tick at which our reconstruction agrees with the oracle's
// `m_nTickBase` pins the C++-tick to demo-tick mapping by value instead of by a
// hard-coded constant that nobody re-checks.
void printPropsAt(const tf2::native::DemoNetworkSummary& summary,
                  const std::vector<std::int32_t>& ticks, int entityIndex) {
  std::vector<tf2::native::EntityState> states;
  for (const std::int32_t tick : ticks) {
    const auto status = tf2::native::queryEntitySnapshotAtOrBeforeTick(summary, tick, states);
    std::fprintf(stderr, "at tick=%d status=%s\n", tick, snapshotStatusName(status));
    for (std::size_t entity = 0; entity < states.size(); ++entity) {
      if (states[entity].classId < 0) continue;
      if (entityIndex >= 0 && entity != static_cast<std::size_t>(entityIndex)) continue;
      const char* className = "-";
      if (static_cast<std::size_t>(states[entity].classId) < summary.serverClassSchemas.size()) {
        className = summary.serverClassSchemas[static_cast<std::size_t>(states[entity].classId)].name.c_str();
      }
      // A map has no stable order, so the lines are sorted before printing:
      // otherwise two runs of the same binary can emit the same set of values in
      // a different order and a diff would look like a disagreement.
      std::vector<std::pair<std::string, tf2::native::EntityPropertyValue>> ordered(
          states[entity].properties.begin(), states[entity].properties.end());
      std::sort(ordered.begin(), ordered.end(),
                [](const auto& left, const auto& right) { return left.first < right.first; });
      for (const auto& [name, value] : ordered) {
        if (entityIndex < 0 && !isOriginProperty(name)) continue;
        std::fprintf(stderr, "  entity=%zu class=%d %s %s type=%d x=%.6f y=%.6f z=%.6f int=%lld\n",
                     entity, states[entity].classId, className, name.c_str(),
                     static_cast<int>(value.type), value.x, value.y, value.z,
                     static_cast<long long>(value.intValue));
      }
    }
  }
  std::fflush(stderr);
}

// Origin-only view of every entity: the wrapper the --trajectory-at flag uses.
void printOriginsAt(const tf2::native::DemoNetworkSummary& summary,
                    const std::vector<std::int32_t>& ticks) {
  printPropsAt(summary, ticks, -1);
}

// What the renderer is actually handed, per entity: the output of
// EntityModelResolver::extractTransform. This is the end-to-end reading -- the
// raw property dump above shows what the decoder decoded, this shows what the
// player would see, and the two differ exactly where the z merge matters.
void printRenderedAt(const tf2::native::DemoNetworkSummary& summary,
                     const std::vector<std::int32_t>& ticks) {
  std::vector<tf2::native::EntityState> states;
  for (const std::int32_t tick : ticks) {
    const auto status = tf2::native::queryEntitySnapshotAtOrBeforeTick(summary, tick, states);
    std::fprintf(stderr, "at tick=%d status=%s\n", tick, snapshotStatusName(status));
    for (std::size_t entity = 0; entity < states.size(); ++entity) {
      if (states[entity].classId < 0) continue;
      const auto transform = tf2::native::EntityModelResolver::extractTransform(states[entity]);
      if (!transform.hasOrigin) continue;
      const char* className = "-";
      if (static_cast<std::size_t>(states[entity].classId) < summary.serverClassSchemas.size()) {
        className = summary.serverClassSchemas[static_cast<std::size_t>(states[entity].classId)].name.c_str();
      }
      std::fprintf(stderr, "  rendered entity=%zu class=%d %s x=%.6f y=%.6f z=%.6f%s%s\n",
                   entity, states[entity].classId, className,
                   transform.origin[0], transform.origin[1], transform.origin[2],
                   transform.diagnostic.empty() ? "" : " diag=",
                   transform.diagnostic.c_str());
    }
  }
  std::fflush(stderr);
}

// Same names the Rust oracle prints, so a diff of the two tables is readable.
const char* sendPropTypeName(tf2::native::SendPropType type) {
  switch (type) {
    case tf2::native::SendPropType::Int: return "Int";
    case tf2::native::SendPropType::Float: return "Float";
    case tf2::native::SendPropType::Vector: return "Vector";
    case tf2::native::SendPropType::VectorXY: return "VectorXY";
    case tf2::native::SendPropType::String: return "String";
    case tf2::native::SendPropType::Array: return "Array";
    case tf2::native::SendPropType::DataTable: return "DataTable";
  }
  return "?";
}

// Flattened send-table props of every class whose name contains `filter`, one
// line per slot. This is deliberately the same shape as the Rust oracle's mode
// 3 so the two tables can be diffed slot by slot: the slot number is the index
// svc_PacketEntities puts on the wire, so a slot that exists in one table and
// not the other is exactly the property the decoder cannot name.
void printClassProps(const tf2::native::DemoNetworkSummary& summary, const std::string& filter) {
  for (std::size_t id = 0; id < summary.serverClassSchemas.size(); ++id) {
    const auto& serverClass = summary.serverClassSchemas[id];
    if (serverClass.name.find(filter) == std::string::npos) continue;
    const tf2::native::SendTableSchema* table = nullptr;
    for (const auto& candidate : summary.sendTableSchemas) {
      if (candidate.name == serverClass.dataTable) { table = &candidate; break; }
    }
    if (!table) {
      std::fprintf(stderr, "class id=%zu name=%s table=%s <no table>\n",
                   id, serverClass.name.c_str(), serverClass.dataTable.c_str());
      continue;
    }
    std::fprintf(stderr, "class id=%zu name=%s table=%s flat=%zu\n",
                 id, serverClass.name.c_str(), serverClass.dataTable.c_str(),
                 table->flattenedProps.size());
    for (std::size_t slot = 0; slot < table->flattenedProps.size(); ++slot) {
      const auto& prop = table->flattenedProps[slot];
      // flags/bitCount/range are the raw words the decoder actually uses, so
      // printing them is what turns "the tables look the same" into "the tables
      // agree on the encoding" -- a name-only diff cannot see a coord-vs-float
      // mistake, which is exactly the kind that produces plausible garbage.
      std::fprintf(stderr, "    [%zu] %s%s %s flags=0x%04x bits=%u range=%s",
                   slot,
                   prop.ownerTable.empty() ? "" : (prop.ownerTable + ".").c_str(),
                   prop.name.c_str(), sendPropTypeName(prop.type),
                   prop.flags, prop.bitCount,
                   prop.hasFloatRange ? "yes" : "no");
      if (prop.hasFloatRange) {
        std::fprintf(stderr, "[%.6g,%.6g]", prop.lowValue, prop.highValue);
      }
      if (prop.elementCount > 0) {
        std::fprintf(stderr, " elems=%u elemType=%s", prop.elementCount,
                     sendPropTypeName(prop.arrayElementType));
      }
      std::fprintf(stderr, "\n");
    }
  }
  std::fflush(stderr);
}

// The modelprecache table itself, index by index. Every other reading here says
// "index N resolved"; this is the only one that says *to what*, which is what a
// reader needs when two candidate indices disagree (a weapon carries both its
// world-model index and its view-model index and they are different numbers).
void printPrecache(const tf2::native::DemoNetworkSummary& summary, const std::string& filter) {
  std::vector<std::pair<std::uint16_t, std::string>> entries(
      summary.modelPrecache.begin(), summary.modelPrecache.end());
  std::sort(entries.begin(), entries.end(),
            [](const auto& left, const auto& right) { return left.first < right.first; });
  std::size_t shown = 0;
  for (const auto& [index, path] : entries) {
    if (filter != "*" && path.find(filter) == std::string::npos) continue;
    std::fprintf(stderr, "precache idx=%u %s\n", static_cast<unsigned>(index), path.c_str());
    ++shown;
  }
  std::fprintf(stderr, "precache entries=%zu shown=%zu filter=%s\n",
               summary.modelPrecache.size(), shown, filter.c_str());
  std::fflush(stderr);
}

// One line per entity that carries a world-model index, plus the totals. This is
// the per-entity view a check can assert on, the same way --props-at is the
// per-entity view of the decoder: the counters say every index resolved, this
// says for which entity and to which model.
void printWeaponModels(const tf2::native::DemoNetworkSummary& summary) {
  std::size_t withWorld = 0, worldNonZero = 0, fromWorld = 0, stillArms = 0, weaponArms = 0;
  std::size_t armsNonWeapon = 0;
  for (const auto& reference : summary.assetReferences) {
    if (!reference.hasWorldModelIndex) {
      // A path naming a class's arms model on something that carries no world
      // model index is not a weapon at all: it is a first-person-only wearable
      // (CTFWearableVM), which belongs to the ViewModel round, not to this one.
      // Counted and listed so the count in the summary line can never be
      // mistaken for the defect this round fixed.
      if (isArmsPath(reference.modelPath)) {
        ++armsNonWeapon;
        std::fprintf(stderr, "arms-nonweapon entity=%u class=%d %s modelIndex=%lld path=%s\n",
                     static_cast<unsigned>(reference.entityIndex), reference.classId,
                     reference.className.c_str(),
                     static_cast<long long>(reference.modelIndex),
                     reference.modelPath.c_str());
      }
      continue;
    }
    ++withWorld;
    if (reference.worldModelIndex > 0) {
      ++worldNonZero;
      if (isArmsPath(reference.modelPath)) ++weaponArms;
    }
    if (reference.modelPathFromWorldModelIndex) ++fromWorld;
    if (isArmsPath(reference.modelPath)) ++stillArms;
    std::fprintf(stderr,
                 "weapon entity=%u class=%d %s present=%s%s%s modelIndex=%lld viewModelIndex=%lld "
                 "worldModelIndex=%lld path=%s source=%s\n",
                 static_cast<unsigned>(reference.entityIndex), reference.classId,
                 reference.className.c_str(),
                 reference.hasModelIndex ? "m" : "-",
                 reference.hasViewModelIndex ? "v" : "-",
                 reference.hasWorldModelIndex ? "w" : "-",
                 static_cast<long long>(reference.modelIndex),
                 static_cast<long long>(reference.viewModelIndex),
                 static_cast<long long>(reference.worldModelIndex),
                 reference.modelPath.empty() ? "<none>" : reference.modelPath.c_str(),
                 reference.modelPathFromWorldModelIndex ? "world" : "model");
  }
  std::fprintf(stderr,
               "weapon-dump refs=%zu withWorldIndex=%zu worldIndexNonZero=%zu pathFromWorld=%zu "
               "pathStillArms=%zu armsWeapon=%zu armsNonWeapon=%zu known=%zu resolved=%zu "
               "unresolved=%zu zero=%zu outOfRange=%zu agrees=%zu viewIndexZero=%zu differs=%zu\n",
               summary.assetReferences.size(), withWorld, worldNonZero, fromWorld, stillArms,
               weaponArms, armsNonWeapon, summary.assetWorldModelIndexKnown,
               summary.assetWorldModelIndexResolved, summary.assetWorldModelIndexUnresolved,
               summary.assetWorldModelIndexZero, summary.assetWorldModelIndexOutOfRange,
               summary.assetWeaponViewModelIndexAgrees, summary.assetWeaponViewModelIndexZero,
               summary.assetWeaponViewModelIndexDiffers);
  std::fflush(stderr);
}

const char* viewSourceName(tf2::native::DemoViewSource source) {
  switch (source) {
    case tf2::native::DemoViewSource::CmdInfo: return "cmdinfo";
    case tf2::native::DemoViewSource::FixAngle: return "fixangle";
    case tf2::native::DemoViewSource::None: return "none";
  }
  return "none";
}

// The demo's own camera track at the given ticks: the sample the playback loop
// would hand the renderer via findObserverViewAtOrBeforeTick. This is the
// independent witness for "where was the recording player actually looking" --
// dem_cmdinfo is written by the recording client itself, so it cannot be biased
// by anything this decoder believes about observer targets.
void printCameraAt(const tf2::native::DemoNetworkSummary& summary,
                   const std::vector<std::int32_t>& ticks) {
  for (const std::int32_t tick : ticks) {
    tf2::native::DemoViewSample sample;
    const bool found = tf2::native::findObserverViewAtOrBeforeTick(summary, tick, sample);
    std::fprintf(stderr,
                 "camera at tick=%d found=%d sample_tick=%d source=%s origin=%.6f,%.6f,%.6f "
                 "angles=%.6f,%.6f originValid=%d anglesValid=%d\n",
                 tick, found ? 1 : 0, sample.tick, viewSourceName(sample.source),
                 sample.origin[0], sample.origin[1], sample.origin[2],
                 sample.angles[0], sample.angles[1],
                 sample.hasOrigin ? 1 : 0, sample.hasAngles ? 1 : 0);
  }
  std::fflush(stderr);
}

// One line per (tick, entity) for the observer-focus resolution: what mode the
// entity's own state says it is in, where its m_hObserverTarget points, and
// whether the camera should follow that target. With --entity N only that view
// entity is printed; without it every entity whose mode is non-zero is printed
// (a zero-mode entity has nothing to say) and the summary line counts every
// entity that was examined, printed or not.
void printObserverFocusAt(const tf2::native::DemoNetworkSummary& summary,
                          const std::vector<std::int32_t>& ticks, int viewEntityIndex) {
  std::size_t examined = 0, emitted = 0, withMode = 0, modeNonZero = 0, follows = 0;
  std::size_t targetPresent = 0, targetInRange = 0, targetOutOfRange = 0, targetMissing = 0;
  for (const std::int32_t tick : ticks) {
    std::vector<tf2::native::EntityState> states;
    std::int32_t resolvedTick = 0;
    const auto status = tf2::native::queryEntitySnapshotAtOrBeforeTick(summary, tick, states, &resolvedTick);
    // `resolved` is the tick the states actually came from. An archive answer is
    // stale by construction -- it is the newest archived snapshot at or before the
    // query tick, which can be a thousand ticks behind it -- so printing the pair
    // makes the staleness a reading. A check that compares a checkpoint value with
    // a packet written later is comparing two different instants, and this line is
    // what lets it say so.
    std::fprintf(stderr, "observer-focus at tick=%d status=%s resolved=%d states=%zu\n",
                 tick, snapshotStatusName(status), resolvedTick, states.size());
    for (std::size_t entity = 0; entity < states.size(); ++entity) {
      if (viewEntityIndex >= 0 && entity != static_cast<std::size_t>(viewEntityIndex)) continue;
      if (states[entity].classId < 0) continue;
      ++examined;
      // A synthetic entity in the fixture has no snapshot status; the demos do.
      const auto resolution = tf2::native::resolveObserverFocus(states[entity], states);
      if (resolution.hasMode) {
        ++withMode;
        if (resolution.mode != 0) ++modeNonZero;
      }
      if (resolution.hasTarget) {
        if (resolution.targetInRange) {
          ++targetInRange;
          if (resolution.targetPresent) ++targetPresent;
          else ++targetMissing;
        } else {
          ++targetOutOfRange;
        }
      }
      if (resolution.followsTarget) ++follows;
      // Quiet unless there is something to say: with no --entity this walks
      // every entity in the snapshot, and a line per zero-mode player would bury
      // the few that are actually spectating.
      const bool interesting = viewEntityIndex >= 0 || (resolution.hasMode && resolution.mode != 0);
      if (!interesting) continue;
      ++emitted;
      const char* className = "-";
      if (static_cast<std::size_t>(states[entity].classId) < summary.serverClassSchemas.size()) {
        className = summary.serverClassSchemas[static_cast<std::size_t>(states[entity].classId)].name.c_str();
      }
      std::fprintf(stderr,
                   "  observer entity=%zu class=%d %s mode=%lld targetHandle=%u targetIndex=%u "
                   "targetSerial=%u targetPresent=%d follows=%d",
                   entity, states[entity].classId, className,
                   static_cast<long long>(resolution.mode), resolution.targetHandle,
                   static_cast<unsigned>(resolution.targetIndex),
                   static_cast<unsigned>(resolution.targetSerial),
                   resolution.targetPresent ? 1 : 0, resolution.followsTarget ? 1 : 0);
      // Where the focus would land, read through the same extractTransform the
      // playback loop uses -- a property being decoded is not the claim; the
      // claim is that the camera has somewhere correct to go.
      if (resolution.followsTarget) {
        const auto targetTransform = tf2::native::EntityModelResolver::extractTransform(
            states[resolution.targetIndex]);
        if (targetTransform.hasOrigin) {
          std::fprintf(stderr, " targetOrigin=%.6f,%.6f,%.6f",
                       targetTransform.origin[0], targetTransform.origin[1], targetTransform.origin[2]);
        } else {
          std::fprintf(stderr, " targetOrigin=<none>");
        }
      }
      const auto selfTransform = tf2::native::EntityModelResolver::extractTransform(states[entity]);
      if (selfTransform.hasOrigin) {
        std::fprintf(stderr, " selfOrigin=%.6f,%.6f,%.6f",
                     selfTransform.origin[0], selfTransform.origin[1], selfTransform.origin[2]);
      }
      std::fprintf(stderr, "\n");
    }
  }
  std::fprintf(stderr,
               "observer-focus-summary ticks=%zu examined=%zu emitted=%zu withMode=%zu modeNonZero=%zu "
               "follows=%zu targetInRange=%zu targetPresent=%zu targetMissing=%zu targetOutOfRange=%zu\n",
               ticks.size(), examined, emitted, withMode, modeNonZero, follows,
               targetInRange, targetPresent, targetMissing, targetOutOfRange);
  std::fflush(stderr);
}

} // namespace

int main(int argc, char** argv) {
  std::string tfRoot;
  std::string demoPath;
  bool selfTest = false;
  std::size_t trajectoryTicks = 0;
  bool trajectoryDump = false;
  std::vector<std::int32_t> trajectoryAt;
  std::vector<std::int32_t> propsAt;
  std::vector<std::int32_t> cameraAt;
  std::vector<std::int32_t> observerFocusAt;
  int propsAtEntity = -1;
  std::string classPropsFilter;
  std::string precacheFilter;
  bool weaponModelDump = false;
  bool historyStats = false;
  std::size_t historyCoverageSamples = 0;
  bool renderedOrigins = false;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--self-test") selfTest = true;
    else if (arg == "--tf-root" && i + 1 < argc) tfRoot = argv[++i];
    else if (arg == "--demo" && i + 1 < argc) demoPath = argv[++i];
    else if (arg == "--dump-class-props" && i + 1 < argc) classPropsFilter = argv[++i];
    else if (arg == "--dump-precache" && i + 1 < argc) precacheFilter = argv[++i];
    else if (arg == "--dump-weapon-models") weaponModelDump = true;
    else if (arg == "--history-stats") historyStats = true;
    else if (arg == "--history-coverage" && i + 1 < argc) historyCoverageSamples = std::strtoul(argv[++i], nullptr, 10);
    else if (arg == "--rendered") renderedOrigins = true;
    else if (arg == "--entity" && i + 1 < argc) propsAtEntity = static_cast<int>(std::strtol(argv[++i], nullptr, 10));
    else if (arg == "--props-at" && i + 1 < argc) parseTickList(argv[++i], propsAt);
    else if (arg == "--camera-at" && i + 1 < argc) parseTickList(argv[++i], cameraAt);
    else if (arg == "--observer-focus-at" && i + 1 < argc) parseTickList(argv[++i], observerFocusAt);
    else if (arg == "--trajectory" && i + 1 < argc) trajectoryTicks = std::strtoul(argv[++i], nullptr, 10);
    else if (arg == "--trajectory-dump") trajectoryDump = true;
    else if (arg == "--trajectory-at" && i + 1 < argc) parseTickList(argv[++i], trajectoryAt);
  }
  if (selfTest && !runSelfTest()) return fail("self-test failed");

  std::size_t uniqueRenderable = 0;
  std::size_t vpkRenderable = 0;
  std::size_t cacheHits = 0;
  std::size_t inspections = 0;
  std::size_t scoutVertices = 0;
  std::uint32_t scoutChecksum = 0;
  std::size_t instanceCount = 0;
  std::size_t playerFallbacks = 0;
  std::string scoutKey;
  bool duplicateStable = true;

  if (!tfRoot.empty()) {
    const auto assets = tf2::native::AssetRoot::fromPath(tfRoot);
    if (!assets.valid()) return fail("tf-root is not a valid AssetRoot");
    std::vector<tf2::native::AssetReference> references;
    const char* models[] = {
      "models/player/scout.mdl",
      "models/player/soldier.mdl",
      "models/player/scout.mdl",
      "models/weapons/w_models/w_rocketlauncher.mdl",
      "models/not/a/real_model.mdl",
    };
    for (int i = 0; i < 5; ++i) {
      tf2::native::AssetReference reference;
      reference.entityIndex = static_cast<std::uint16_t>(i + 1);
      reference.hasModelPath = true;
      reference.modelPath = models[i];
      reference.className = i < 2 ? "CTFPlayer" : "CBaseAnimating";
      references.push_back(reference);
    }
    tf2::native::ModelRenderRequestStats stats;
    const auto first = tf2::native::ModelLoader::buildRenderRequests(assets, references, nullptr, &stats);
    const auto second = tf2::native::ModelLoader::buildRenderRequests(assets, references, nullptr, nullptr);
    inspections = stats.inspections;
    cacheHits = stats.inspectionCacheHits;
    if (first.size() != second.size()) duplicateStable = false;
    for (std::size_t i = 0; i < first.size() && i < second.size(); ++i) {
      if (first[i].cacheKey != second[i].cacheKey) duplicateStable = false;
      if (first[i].renderable) ++uniqueRenderable;
      if (first[i].renderable && first[i].inspectedFromVpk) ++vpkRenderable;
      if (first[i].modelPath.find("scout.mdl") != std::string::npos && first[i].renderable) {
        scoutKey = first[i].cacheKey;
        scoutChecksum = first[i].checksum;
        std::string error;
        auto metadata = first[i].inspection.metadata;
        if (tf2::native::ModelLoader::buildBindPoseMeshLod0(metadata, error)) {
          scoutVertices = metadata.bindPoseVertices.size();
        } else {
          scoutKey += ";mesh=" + error + ";idx=" + std::to_string(metadata.indices.size())
            + ";renderIdx=" + std::to_string(metadata.renderIndices.size())
            + ";verts=" + std::to_string(metadata.vertices.size())
            + ";vtxFail=" + metadata.vtxDiagnostics.failureReason
            + ";vtxDesc=" + std::to_string(metadata.vtxDiagnostics.vtxDescriptorCount);
          for (const auto& diagnostic : first[i].inspection.diagnostics) {
            if (scoutKey.size() < 900) scoutKey += "|" + diagnostic;
          }
        }
      }
    }
    if (first.size() >= 3 && first[0].cacheKey != first[2].cacheKey) duplicateStable = false;
    if (inspections > 4) duplicateStable = false;

    std::vector<tf2::native::EntityState> states(8);
    tf2::native::EntityPropertyValue origin;
    origin.type = tf2::native::SendPropType::Vector;
    origin.x = 10; origin.y = 20; origin.z = 30;
    states[1].classId = 0;
    states[1].properties["DT_BaseEntity.m_vecOrigin"] = origin;
    tf2::native::EntityPropertyValue playerClass;
    playerClass.type = tf2::native::SendPropType::Int;
    playerClass.intValue = 3;
    states[6].classId = 0;
    states[6].properties["DT_BaseEntity.m_vecOrigin"] = origin;
    states[6].properties["DT_TFPlayer.m_iClass"] = playerClass;
    std::vector<tf2::native::ServerClassSchema> schemas(1);
    schemas[0].name = "CTFPlayer";
    const auto instances = tf2::native::EntityModelResolver::buildInstances(first, states, schemas, 64);
    instanceCount = instances.size();
    for (const auto& instance : instances) if (instance.playerClassFallback) ++playerFallbacks;
  }

  if (!demoPath.empty() && !tfRoot.empty()) {
    tf2::native::DemoHeader header;
    tf2::native::DemoIndex index;
    tf2::native::DemoNetworkSummary summary;
    if (!tf2::native::parseDemoHeaderFile(demoPath, header)) return fail("demo header parse failed");
    tf2::native::indexDemoFile(demoPath, header, index);
    // The protocol has to be copied in before the scan: svc_CreateStringTable
    // reads its payload length as a varint above protocol 23 and as a fixed
    // 20-bit field at or below it. Leaving this at 0 took the wrong branch and
    // produced a plausible-looking summary -- 673 asset references and 0 render
    // requests -- that had nothing to do with the demo's actual contents.
    summary.networkProtocol = header.networkProtocol;
    if (!tf2::native::scanKnownDemoMessages(demoPath, index, summary)) {
      return fail("demo scan failed (network protocol not set?)");
    }
    tf2::native::buildAssetReferenceList(summary, summary.assetReferences);
    const auto assets = tf2::native::AssetRoot::fromPath(tfRoot);
    tf2::native::ModelRenderRequestStats stats;
    const auto requests = tf2::native::ModelLoader::buildRenderRequests(
      assets, summary.assetReferences, nullptr, &stats);
    std::size_t demoRenderable = 0;
    for (const auto& request : requests) if (request.renderable) ++demoRenderable;
    std::size_t worldModelRequests = 0;
    std::size_t armsRequests = 0;
    for (const auto& request : requests) {
      if (request.modelPathFromWorldModelIndex) ++worldModelRequests;
      if (isArmsPath(request.modelPath)) ++armsRequests;
    }
    // The claim this round is answerable for: a reference whose world index is a
    // real value ends up naming the world model, never the arms. `worldIndexNonZero`
    // is the population, `assetModelPathFromWorldModel` the ones that took the
    // route, and `armsWeaponRefs` the counter that has to stay 0 -- a weapon left
    // naming its arms would trip exactly that one and nothing else. Refs naming an
    // arms model without a world index are first-person-only wearables and are
    // reported separately so they cannot be mistaken for that defect.
    std::size_t worldIndexNonZero = 0;
    std::size_t armsWeaponRefs = 0;
    std::size_t armsNonWeaponRefs = 0;
    for (const auto& reference : summary.assetReferences) {
      const bool arms = isArmsPath(reference.modelPath);
      if (!reference.hasWorldModelIndex) {
        if (arms) ++armsNonWeaponRefs;
        continue;
      }
      // Only a *real* world index counts here. On both demos the CTFSpellBook
      // carries the property with the value 0 and its own m_nModelIndex names
      // c_demo_arms, so its path is an arms path by the entity's own statement --
      // folding that into the defect counter would make the counter measure the
      // corpus instead of the wiring.
      if (reference.worldModelIndex > 0) {
        ++worldIndexNonZero;
        if (arms) ++armsWeaponRefs;
      }
    }
    // Built over the same end-state the reference list came from and with the
    // renderer's own cap, so this reading is at the depth main.cpp actually uses
    // rather than a second, differently-shaped pipeline.
    std::size_t worldModelInstances = 0;
    {
      const auto instances = tf2::native::EntityModelResolver::buildInstances(
        requests, summary.entityStates, summary.serverClassSchemas, 256);
      for (const auto& instance : instances) if (instance.worldModelIndexPath) ++worldModelInstances;
    }
    if (weaponModelDump) printWeaponModels(summary);
    TrajectoryStats trajectory;
    if (trajectoryTicks > 0) measureTrajectory(summary, trajectoryTicks, trajectory);
    std::cout << "{\"ok\":true"
      << ",\"selfTest\":" << (selfTest ? "true" : "false")
      << ",\"demo\":\"" << demoPath << "\""
      << ",\"assetRefs\":" << summary.assetReferences.size()
      << ",\"requests\":" << requests.size()
      << ",\"demoRenderable\":" << demoRenderable
      << ",\"assetWorldModelKnown\":" << summary.assetWorldModelIndexKnown
      << ",\"assetWorldModelResolved\":" << summary.assetWorldModelIndexResolved
      << ",\"assetWorldModelUnresolved\":" << summary.assetWorldModelIndexUnresolved
      << ",\"assetWorldModelZero\":" << summary.assetWorldModelIndexZero
      << ",\"assetWorldModelOutOfRange\":" << summary.assetWorldModelIndexOutOfRange
      << ",\"assetModelPathFromWorldModel\":" << summary.assetModelPathFromWorldModelIndex
      << ",\"assetModelPathWorldModelOnly\":" << summary.assetModelPathWorldModelOnly
      << ",\"weaponViewModelAgrees\":" << summary.assetWeaponViewModelIndexAgrees
      << ",\"weaponViewModelZero\":" << summary.assetWeaponViewModelIndexZero
      << ",\"weaponViewModelDiffers\":" << summary.assetWeaponViewModelIndexDiffers
      << ",\"worldModelRequests\":" << worldModelRequests
      << ",\"armsRequests\":" << armsRequests
      << ",\"worldIndexNonZero\":" << worldIndexNonZero
      << ",\"armsWeaponRefs\":" << armsWeaponRefs
      << ",\"armsNonWeaponRefs\":" << armsNonWeaponRefs
      << ",\"worldModelInstances\":" << worldModelInstances
      << ",\"inspections\":" << stats.inspections
      << ",\"inspectionCacheHits\":" << stats.inspectionCacheHits
      << ",\"vpkExtracts\":" << stats.vpkExtracts
      << ",\"uniqueRenderable\":" << uniqueRenderable
      << ",\"vpkRenderable\":" << vpkRenderable
      << ",\"scoutVertices\":" << scoutVertices
      << ",\"scoutChecksum\":" << scoutChecksum
      << ",\"duplicateStable\":" << (duplicateStable ? "true" : "false")
      << ",\"instanceCount\":" << instanceCount
      << ",\"playerFallbacks\":" << playerFallbacks;
    // Opt-in, so the default line stays byte-identical to the frozen baseline.
    if (trajectoryTicks > 0) {
      std::cout << ",\"trajectoryTicks\":" << trajectory.ticks
        << ",\"trajectoryFirstTick\":" << trajectory.firstTick
        << ",\"trajectoryLastTick\":" << trajectory.lastTick
        << ",\"trajectoryAvailable\":" << trajectory.available
        << ",\"trajectoryCheckpoint\":" << trajectory.checkpoint
        << ",\"trajectoryUnavailable\":" << trajectory.unavailable
        << ",\"trajectorySamples\":" << trajectory.samples
        << ",\"trajectoryEntities\":" << trajectory.entities
        << ",\"trajectoryMoved\":" << trajectory.moved
        << ",\"trajectoryNoOrigin\":" << trajectory.noOriginProperty
        << ",\"trajectoryNotFinite\":" << trajectory.originNotFinite
        << ",\"trajectoryZZero\":" << trajectory.zZero
        << ",\"trajectoryZNonZero\":" << trajectory.zNonZero
        << ",\"trajectoryRawZZero\":" << trajectory.rawZZero
        << ",\"trajectoryRawZNonZero\":" << trajectory.rawZNonZero
        << ",\"trajectoryZSiblingMissing\":" << trajectory.zSiblingMissing
        << ",\"trajectoryTotalDistance\":" << std::lround(trajectory.totalDistance)
        << ",\"trajectoryMaxStep\":" << trajectory.maxStep
        << ",\"trajectoryMinZ\":" << trajectory.minZ
        << ",\"trajectoryMaxZ\":" << trajectory.maxZ
        << ",\"trajectoryDigest\":\"" << std::hex << trajectory.digest << std::dec << "\"";
    }
    std::cout << "}\n";
    if (trajectoryDump && trajectoryTicks > 0) printTrajectoryDump(trajectory);
    // Independent of --trajectory: this one answers "which m_vecOrigin does the
    // decoder actually hand the renderer at tick T", which is what the Rust
    // oracle prints. Kept separate so it can be run on its own.
    if (!trajectoryAt.empty()) {
      if (renderedOrigins) printRenderedAt(summary, trajectoryAt);
      else printOriginsAt(summary, trajectoryAt);
    }
    if (!propsAt.empty()) printPropsAt(summary, propsAt, propsAtEntity);
    if (!cameraAt.empty()) printCameraAt(summary, cameraAt);
    if (!observerFocusAt.empty()) printObserverFocusAt(summary, observerFocusAt, propsAtEntity);
    if (!classPropsFilter.empty()) printClassProps(summary, classPropsFilter);
    if (!precacheFilter.empty()) printPrecache(summary, precacheFilter);
    if (historyStats) {
      HistoryStats history;
      measureHistory(summary, history);
      std::fprintf(stderr,
        "history archive=%zu ticks=[%d..%d] medianGap=%d maxGap=%d\n"
        "history liveCheckpoints=%zu ticks=[%d..%d] maxGap=%d packets=%zu events=%zu dropped=%zu\n",
        history.archiveCount, history.archiveFirstTick, history.archiveLastTick,
        history.archiveMedianGap, history.archiveMaxGap,
        history.liveCheckpointCount, history.liveFirstTick, history.liveLastTick,
        history.liveMaxGap, history.packetsRetained, history.eventsRetained,
        history.droppedPackets);
      // `dropped` mixes the two ways a packet leaves the live window, and the
      // split is what tells a reader which rule moved: a flush happens because
      // maxEvents updates accumulated, a gap drop because a delta had no base.
      std::fprintf(stderr, "history flushes=%zu gapDropped=%zu\n",
                   history.flushes, history.droppedPackets - history.flushes);
      // The tick list itself, so the gap distribution can be recomputed by hand
      // instead of trusted. A summary that cannot be re-derived from its own raw
      // output is not evidence.
      std::fprintf(stderr, "history archiveTicks=");
      for (const auto& checkpoint : summary.entityHistoryArchive) {
        std::fprintf(stderr, "%d,", checkpoint.tick);
      }
      std::fprintf(stderr, "\n");
      std::fprintf(stderr, "history liveTicks=");
      for (const auto& checkpoint : summary.entityHistoryCheckpoints) {
        std::fprintf(stderr, "%d,", checkpoint.tick);
      }
      std::fprintf(stderr, "\n");
      std::fflush(stderr);
    }
    if (historyCoverageSamples > 0) {
      HistoryCoverage coverage;
      measureHistoryCoverage(summary, historyCoverageSamples, coverage);
      const std::int32_t span = coverage.lastTick - coverage.firstTick;
      const std::int32_t floorGap = coverage.retained >= 2
          ? span / static_cast<std::int32_t>(coverage.retained - 1) : 0;
      std::fprintf(stderr,
        "history coverage samples=%zu exact=%zu checkpoint=%zu unavailable=%zu\n"
        "history retained archive=%zu live=%zu distinct=%zu span=[%d..%d]\n"
        "history gap worst=%d at=[%d..%d] median=%d floor=%d budget=%zu\n"
        "history staleness worst=%d at=%d mean=%.1f\n"
        "history bytes archive=%zu live=%zu checkpointApprox=%zu packets=%zu events=%zu eventsApprox=%zu\n",
        coverage.samples, coverage.exact, coverage.checkpoint, coverage.unavailable,
        coverage.archiveCount, coverage.liveCount, coverage.distinctTicks,
        coverage.firstTick, coverage.lastTick,
        coverage.worstGap, coverage.worstGapFrom, coverage.worstGapTo,
        coverage.medianRetainedGap, floorGap,
        summary.entityHistoryLimits.archiveMax,
        coverage.worstStaleness, coverage.worstStalenessAt,
        coverage.samples > coverage.unavailable
            ? static_cast<double>(coverage.stalenessSum) /
              static_cast<double>(coverage.samples - coverage.unavailable)
            : 0.0,
        coverage.archiveBytes, coverage.liveBytes,
        coverage.archiveCount > 0 ? coverage.archiveBytes / coverage.archiveCount : 0,
        coverage.packetBytes, summary.entityHistoryEvents.size(), coverage.eventBytes);
      std::fflush(stderr);
    }
    return (selfTest && demoRenderable + uniqueRenderable == 0) ? 2 : 0;
  }

  std::cout << "{\"ok\":true"
    << ",\"selfTest\":" << (selfTest ? "true" : "false")
    << ",\"uniqueRenderable\":" << uniqueRenderable
    << ",\"vpkRenderable\":" << vpkRenderable
    << ",\"inspections\":" << inspections
    << ",\"inspectionCacheHits\":" << cacheHits
    << ",\"scoutVertices\":" << scoutVertices
    << ",\"scoutChecksum\":" << scoutChecksum
    << ",\"scoutKey\":\"" << scoutKey << "\""
    << ",\"duplicateStable\":" << (duplicateStable ? "true" : "false")
    << ",\"instanceCount\":" << instanceCount
    << ",\"playerFallbacks\":" << playerFallbacks
    << "}\n";
  return 0;
}
