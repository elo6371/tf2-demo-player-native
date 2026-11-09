#include "asset_root.h"
#include "demo_header.h"
#include "entity_model.h"
// T60: the product and this audit share one material-slot resolver, so the
// published painted-slot count cannot disagree with what the product uploads.
#include "material_slot_resolution.h"
#include "model_loader.h"
#include "vmt_material.h"
#include "vpk_archive.h"
#include "vtf_texture.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <set>
#include <sstream>
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
  // T62 permutes the model axes into the world axes: model (x,y,z) -> world (z,x,y).
  // The world map translates X/Y by center and Z by minZ with a fixed 0.1 lift, so the
  // permutation happens before the scale and the offsets follow the *world* axis.
  // These expectations used to be written against the pre-T62 row order and went stale
  // the moment the permutation landed: the self-test has been red for every build since,
  // and because nothing but m10 ever ran it, the world-model fixture had silently become
  // unreachable -- which made m10's first assertion vacuous rather than green.
  float mapped[3] = {};
  applyPosition(rows, 1.0f, 2.0f, 3.0f, mapped);
  // world x = h*(origin.x - centerX) + h*model.z = 0.90 + 0.03
  // world y = h*(origin.y - centerY) + h*model.x = 1.80 + 0.01
  // world z = 0.1 + zScale*(origin.z - minZ) + zScale*model.y = 1.00 + 0.04
  if (!near(mapped[0], 0.93f) || !near(mapped[1], 1.81f) || !near(mapped[2], 1.04f)) return false;
  float normal[3] = {};
  applyNormal(rows, 0.0f, 1.0f, 0.0f, normal);
  // model +Y is world +Z under the permutation: it is the model's height axis.
  if (!near(normal[0], 0.0f) || !near(normal[1], 0.0f) || !near(normal[2], 1.0f)) return false;
  // Row 0 column 2 is the model z column, row 1 column 0 is the model x column, and the
  // translation sits in column 3 of the world row it belongs to.
  if (!near(rows.positionRows[0], 0.0f) || !near(rows.positionRows[2], world.horizontalScale)
      || !near(rows.positionRows[3], 0.9f) || !near(rows.normalRows[0], 0.0f)
      || !near(rows.normalRows[2], 1.0f)) return false;

  const float yaw90[3] = {0.0f, 90.0f, 0.0f};
  const float yawOrigin[3] = {10.0f, 0.0f, 0.0f};
  tf2::native::EntityModelWorldMap unit;
  unit.horizontalScale = 1.0f;
  unit.zScale = 1.0f;
  tf2::native::EntityModelInstanceRows yawRows;
  if (!tf2::native::buildEntityModelInstanceRows(yawOrigin, yaw90, true, unit, yawRows)) return false;
  // yaw +90 turns the model's +X (which the permutation sends to world +Y) into
  // world -X: 10 - 1, and the point keeps its 0.1 lift.
  applyPosition(yawRows, 1.0f, 0.0f, 0.0f, mapped);
  if (!near(mapped[0], 9.0f) || !near(mapped[1], 0.0f) || !near(mapped[2], 0.1f)) return false;
  applyNormal(yawRows, 1.0f, 0.0f, 0.0f, normal);
  if (!near(normal[0], -1.0f) || !near(normal[1], 0.0f) || !near(normal[2], 0.0f)) return false;

  const float pitch90[3] = {90.0f, 0.0f, 0.0f};
  tf2::native::EntityModelInstanceRows pitchRows;
  const float zero[3] = {};
  if (!tf2::native::buildEntityModelInstanceRows(zero, pitch90, true, unit, pitchRows)) return false;
  // pitch +90 drives the model's forward axis (model +Z, which the permutation sends
  // to world +X) down the world -Z: the 0.1 lift minus one unit.
  applyPosition(pitchRows, 0.0f, 0.0f, 1.0f, mapped);
  if (!near(mapped[0], 0.0f) || !near(mapped[1], 0.0f) || !near(mapped[2], -0.9f)) return false;

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
  // T85 8.3-2 player-model precedence fixture.
  //
  // The claim: when a player entity carries its own m_nModelIndex, that named
  // model is what gets drawn -- the m_iClass class fallback must not override it
  // even though both signals are present and they name different classes. This
  // is the case the snakewater green run cannot supply: at its captured tick all
  // eleven player bodies read no m_nModelIndex at all, so indexResolved was 0.
  //
  // Nothing here re-implements the route. The synthetic snapshot goes through
  // buildAssetReferenceList -> ModelLoader::buildRenderRequests ->
  // EntityModelResolver::buildInstances, the same three calls the world pass
  // makes; only the inputs are synthetic.
  //
  //   19  no m_nModelIndex, m_iClass 3  -> class fallback, soldier.mdl
  //   20  m_nModelIndex 12, m_iClass 3  -> index route, scout.mdl (NOT soldier)
  //
  // Entity 20 is the precedence case. The two routes name different models on
  // purpose: a resolver that let the class fallback win would draw soldier.mdl
  // and fail the path assertion instead of quietly agreeing.
  {
    tf2::native::DemoNetworkSummary precedence;
    tf2::native::ServerClassSchema playerSchema;
    playerSchema.id = 0;
    playerSchema.name = "CTFPlayer";
    precedence.serverClassSchemas.push_back(playerSchema);
    precedence.modelPrecache[12] = "models/player/scout.mdl";
    precedence.entityStates.resize(21);
    for (auto& state : precedence.entityStates) state.classId = 0;
    const auto setPrecedenceInt = [&precedence](std::size_t entity, const char* name, std::int64_t value) {
      tf2::native::EntityPropertyValue prop;
      prop.type = tf2::native::SendPropType::Int;
      prop.intValue = value;
      precedence.entityStates[entity].properties[name] = prop;
    };
    const auto setPrecedenceOrigin = [&precedence](std::size_t entity, float x) {
      tf2::native::EntityPropertyValue origin;
      origin.type = tf2::native::SendPropType::Vector;
      origin.x = x;
      origin.y = 200.0f;
      origin.z = 32.0f;
      precedence.entityStates[entity].properties["DT_BaseEntity.m_vecOrigin"] = origin;
    };
    // Entity 19: the class fallback half. No model index anywhere.
    setPrecedenceInt(19, "DT_TFPlayer.m_iClass", 3);
    setPrecedenceOrigin(19, 100.0f);
    // Entity 20: both signals; the index names scout (12), the class names
    // soldier (3). Whichever wins is visible in the drawn path.
    setPrecedenceInt(20, "DT_TFPlayer.m_iClass", 3);
    setPrecedenceInt(20, "DT_BaseEntity.m_nModelIndex", 12);
    setPrecedenceOrigin(20, 300.0f);
    tf2::native::buildAssetReferenceList(precedence, precedence.assetReferences);
    tf2::native::AssetRoot noAssets;
    const auto precedenceRequests =
      tf2::native::ModelLoader::buildRenderRequests(noAssets, precedence.assetReferences, nullptr);
    const auto precedenceInstances = tf2::native::EntityModelResolver::buildInstances(
      precedenceRequests, precedence.entityStates, precedence.serverClassSchemas);
    const tf2::native::ModelInstance* indexInstance = nullptr;
    const tf2::native::ModelInstance* classInstance = nullptr;
    for (const auto& instance : precedenceInstances) {
      if (instance.entityIndex == 20) indexInstance = &instance;
      if (instance.entityIndex == 19) classInstance = &instance;
    }
    std::fprintf(stderr,
                 "player-model-precedence-fixture refs=%zu requests=%zu instances=%zu\n",
                 precedence.assetReferences.size(), precedenceRequests.size(),
                 precedenceInstances.size());
    for (const auto& instance : precedenceInstances) {
      std::fprintf(stderr,
                   "  fixture entity=%u class=%lld path=%s classFallback=%d\n",
                   static_cast<unsigned>(instance.entityIndex),
                   static_cast<long long>(instance.transform.playerClass),
                   instance.modelPath.empty() ? "<none>" : instance.modelPath.c_str(),
                   instance.playerClassFallback ? 1 : 0);
    }
    std::fflush(stderr);
    // One draw request, and it is the one the model index named. Two instances:
    // the indexed entity plus the one the fallback sweep had to fill.
    if (precedence.assetReferences.size() != precedence.entityStates.size()) return false;
    if (precedenceRequests.size() != 1) return false;
    if (precedenceRequests[0].entityIndex != 20) return false;
    if (precedenceInstances.size() != 2) return false;
    if (indexInstance == nullptr || classInstance == nullptr) return false;
    // The named entity path wins: drawn from the index, not the class fallback.
    if (indexInstance->playerClassFallback) return false;
    if (indexInstance->modelPath != "models/player/scout.mdl") return false;
    // The class signal was present the whole time, so the fallback half must
    // independently still work -- otherwise "index wins" could just mean "no
    // fallback exists".
    if (indexInstance->transform.playerClass != 3) return false;
    if (!classInstance->playerClassFallback) return false;
    if (classInstance->modelPath != "models/player/soldier.mdl") return false;
  }

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

  // -------------------------------------------------------------------------
  // Slot-freshness wiring fixture.
  //
  // A player's origin arrives in two slots and the selection rule has to choose
  // between them. Four synthetic entities pin the rule end to end, built so that
  // every branch exists by construction and so that a branch going dead is a
  // changed reading rather than a silent hole:
  //
  //   A  Local fresh (1000) / NonLocal stale (100)  -> picks Local. bagel's shape.
  //   B  Local stale (100)  / NonLocal fresh (1000) -> picks NonLocal. The POV
  //      entity-3 shape: rank alone would take Local, so this is the one shape
  //      where freshness has to override rank, i.e. freshnessDecided = 1.
  //   C  both written at 500                        -> picks Local. Equal ticks are
  //      the tie the rank rule still decides, so the deterministic order the
  //      earlier round established is unchanged wherever freshness is silent.
  //   D  neither written by a packet (-1)           -> picks Local. A state built
  //      outside readEntityPropUpdates (this fixture, and any hand-built state)
  //      has no ticks at all and must order exactly as it did before the rule
  //      gained a freshness term.
  //
  // Two independent paths are checked, not one: rankPropertyCandidates (the
  // rule's own ordering, the same comparator findProperty and readVectorProperty
  // call) and EntityModelResolver::extractTransform (the public API the renderer
  // uses). If the ranking and the renderer's answer ever disagree, this fails
  // even though each alone would look right.
  // -------------------------------------------------------------------------
  {
    const std::string kLocalName = "DT_TFLocalPlayerExclusive.m_vecOrigin";
    const std::string kNonLocalName = "DT_TFNonLocalPlayerExclusive.m_vecOrigin";
    const auto buildState = [&](std::int32_t localTick, std::int32_t nonLocalTick) {
      tf2::native::EntityState state;
      state.classId = 247;
      tf2::native::EntityPropertyValue local;
      local.type = tf2::native::SendPropType::VectorXY;
      local.x = 1.0f; local.y = 2.0f;
      local.lastWriteTick = localTick;
      tf2::native::EntityPropertyValue nonLocal;
      nonLocal.type = tf2::native::SendPropType::VectorXY;
      nonLocal.x = 3.0f; nonLocal.y = 4.0f;
      nonLocal.lastWriteTick = nonLocalTick;
      state.properties[kLocalName] = local;
      state.properties[kNonLocalName] = nonLocal;
      return state;
    };
    // {localTick, nonLocalTick, the slot the rule must pick}
    struct Shape { std::int32_t localTick; std::int32_t nonLocalTick; const std::string* expected; };
    const Shape shapes[] = {
      {1000,  100, &kLocalName},
      { 100, 1000, &kNonLocalName},
      { 500,  500, &kLocalName},
      {  -1,   -1, &kLocalName},
    };
    const std::size_t shapeCount = sizeof(shapes) / sizeof(shapes[0]);
    std::size_t freshChosen = 0, staleChosen = 0, freshnessDecided = 0;
    std::size_t rankTiePrefersLocal = 0, apiMatches = 0, tickRoundTrip = 0;
    for (const auto& shape : shapes) {
      const auto state = buildState(shape.localTick, shape.nonLocalTick);
      const auto ranked = tf2::native::rankPropertyCandidates(state, "m_vecOrigin");
      if (ranked.size() != 2) return false;
      const auto& pick = ranked[0];
      if (pick.name != *shape.expected) return false;
      // The ranking must carry the same ticks the state does, in rank order --
      // i.e. the field survives the trip through the comparator.
      if (pick.lastWriteTick == (pick.name == kLocalName ? shape.localTick : shape.nonLocalTick)) {
        ++tickRoundTrip;
      }
      if (shape.localTick != shape.nonLocalTick) {
        if (pick.lastWriteTick == std::max(shape.localTick, shape.nonLocalTick)) ++freshChosen;
        else ++staleChosen;
      } else if (pick.name == kLocalName) {
        // No freshness signal: the rank rule must still be the one deciding.
        ++rankTiePrefersLocal;
      }
      // Rank alone would pick Local on every shape above, so any shape whose
      // pick is not Local is one freshness decided. Exactly one exists (B);
      // if it ever reads 0 the freshness term has become dead code that the
      // other counters would not notice.
      if (pick.name != kLocalName) ++freshnessDecided;
      // And the answer the renderer actually reads has to be the same answer.
      const auto transform = tf2::native::EntityModelResolver::extractTransform(state);
      if (transform.hasOrigin
          && transform.origin[0] == (pick.name == kLocalName ? 1.0f : 3.0f)) {
        ++apiMatches;
      }
    }
    std::fprintf(stderr,
                 "slot-freshness-fixture shapes=%zu freshChosen=%zu staleChosen=%zu "
                 "freshnessDecided=%zu rankTiePrefersLocal=%zu apiMatches=%zu tickRoundTrip=%zu\n",
                 shapeCount, freshChosen, staleChosen, freshnessDecided,
                 rankTiePrefersLocal, apiMatches, tickRoundTrip);
    std::fflush(stderr);
    if (freshChosen != 2) return false;
    if (staleChosen != 0) return false;
    if (freshnessDecided != 1) return false;
    if (rankTiePrefersLocal != 2) return false;
    if (apiMatches != shapeCount) return false;
    if (tickRoundTrip != shapeCount) return false;
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

// Animation-property availability, per server class. This is the reading that
// decides whether per-entity skeletal animation is even wired end to end: the
// player class in a TF2 demo does not network its animation state, so a renderer
// has no sequence to advance and no cycle to evaluate. That is a property of the
// protocol, not of this decoder -- source-sdk-2013 declares m_nSequence and
// m_flPlaybackRate in DT_BaseAnimating, but c_baseanimating.cpp:1168 records that
// "not all entities network down their m_nSequence (like multiplayer game player
// entities)", and TF2 strips them for CTFPlayer. Weapon and projectile classes
// keep theirs. A gate on this needs both halves: the absence (so nobody builds a
// per-entity playback loop on a sequence that will never arrive) and the presence
// (so the same rule is shown to be able to report "yes" somewhere).
//
// Three properties are counted per class, each by its own name so a future
// rename cannot make the check pass by counting something else:
//   m_nSequence      the animation to play
//   m_flCycle        how far into it
//   m_flPlaybackRate how fast it advances
// A class is reported only when at least one of the three is present, so the
// output stays short on a 400-class schema and the caller can assert on the set.
void printAnimationProps(const tf2::native::DemoNetworkSummary& summary) {
  std::size_t classesWithSequence = 0;
  std::size_t classesWithCycle = 0;
  std::size_t classesWithRate = 0;
  std::size_t tfPlayerSequence = 0, tfPlayerCycle = 0, tfPlayerRate = 0;
  std::size_t tfPlayerFound = 0;
  for (std::size_t id = 0; id < summary.serverClassSchemas.size(); ++id) {
    const auto& serverClass = summary.serverClassSchemas[id];
    const tf2::native::SendTableSchema* table = nullptr;
    for (const auto& candidate : summary.sendTableSchemas) {
      if (candidate.name == serverClass.dataTable) { table = &candidate; break; }
    }
    if (!table) continue;
    std::size_t sequence = 0, cycle = 0, rate = 0, pose = 0;
    for (const auto& prop : table->flattenedProps) {
      // Match the leaf name only: the owner-table prefix varies with the include
      // chain (DT_BaseAnimating vs DT_AnimatingOverlay), and the question here is
      // whether the slot exists in this class's wire format at all.
      const std::size_t dot = prop.name.rfind('.');
      const std::string leaf = dot == std::string::npos ? prop.name : prop.name.substr(dot + 1);
      if (leaf == "m_nSequence") ++sequence;
      else if (leaf == "m_flCycle") ++cycle;
      else if (leaf == "m_flPlaybackRate") ++rate;
      else if (leaf == "m_flPoseParameter") ++pose;
    }
    // The exact class name, not a substring. A first version of this used
    // name.find("Player") and swept in CPlayerDestructionDispenser (a building)
    // and CBasePlayer (the SDK base, whose table is not TF2's wire format),
    // reporting "playerSequence=32" while CTFPlayer -- the class that actually
    // decides the renderer's design -- was absent from the list entirely, because
    // its own count is zero and the guard below skipped it. That is a false
    // positive in the instrument, so the name is compared exactly now, and
    // CTFPlayer is reported on its own line whether or not it has any of the
    // three, so "absent from the list" can never again be confused with "has
    // zero".
    const bool isTfPlayer = serverClass.name == "CTFPlayer";
    if (isTfPlayer) {
      ++tfPlayerFound;
      tfPlayerSequence += sequence;
      tfPlayerCycle += cycle;
      tfPlayerRate += rate;
      std::fprintf(stderr, "animprop-player class=%s id=%zu sequence=%zu cycle=%zu rate=%zu pose=%zu\n",
                   serverClass.name.c_str(), id, sequence, cycle, rate, pose);
    }
    if (sequence == 0 && cycle == 0 && rate == 0) continue;
    if (sequence > 0) ++classesWithSequence;
    if (cycle > 0) ++classesWithCycle;
    if (rate > 0) ++classesWithRate;
    std::fprintf(stderr, "animprop class=%s id=%zu sequence=%zu cycle=%zu rate=%zu pose=%zu\n",
                 serverClass.name.c_str(), id, sequence, cycle, rate, pose);
  }
  std::fprintf(stderr,
    "animprop-summary classesWithSequence=%zu classesWithCycle=%zu classesWithRate=%zu "
    "tfPlayerFound=%zu tfPlayerSequence=%zu tfPlayerCycle=%zu tfPlayerRate=%zu\n",
    classesWithSequence, classesWithCycle, classesWithRate,
    tfPlayerFound, tfPlayerSequence, tfPlayerCycle, tfPlayerRate);
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

// Every slot that matches one suffix, in the order the selection rule ranks them,
// with the age of each. This is the instrument for the question the current rule
// cannot answer: a player's origin arrives in a LocalPlayerExclusive slot and a
// NonLocalPlayerExclusive slot, and on this POV demo the one the rule prefers is
// the stale one. Nothing here changes the rule -- the point is to make "the rule
// picked a slot written N ticks ago while a newer one was sitting right there"
// a printable number before any rule is allowed to depend on it.
//
// `chosenStale` counts the ticks where the rule's pick was NOT the freshest
// candidate: that is the defect's signature, and it is a counter so a gate can
// assert on it rather than on a wall of text.
void printPropCandidatesAt(const tf2::native::DemoNetworkSummary& summary,
                           const std::vector<std::int32_t>& ticks, int entityIndex,
                           const std::string& suffix) {
  if (entityIndex < 0 || suffix.empty()) {
    std::fprintf(stderr, "prop-candidates: needs --entity N and --candidate-suffix S\n");
    std::fflush(stderr);
    return;
  }
  std::size_t examined = 0, withCandidates = 0, candidates = 0, withTick = 0, chosenStale = 0;
  std::int32_t worstAge = 0;
  for (const std::int32_t tick : ticks) {
    std::vector<tf2::native::EntityState> states;
    std::int32_t resolvedTick = 0;
    const auto status = tf2::native::queryEntitySnapshotAtOrBeforeTick(summary, tick, states, &resolvedTick);
    std::fprintf(stderr, "prop-candidates at tick=%d status=%s resolved=%d\n",
                 tick, snapshotStatusName(status), resolvedTick);
    if (static_cast<std::size_t>(entityIndex) >= states.size()) continue;
    const auto& state = states[static_cast<std::size_t>(entityIndex)];
    if (state.classId < 0) continue;
    ++examined;
    const auto ranked = tf2::native::rankPropertyCandidates(state, suffix.c_str());
    if (ranked.empty()) continue;
    ++withCandidates;
    const char* className = "-";
    if (static_cast<std::size_t>(state.classId) < summary.serverClassSchemas.size()) {
      className = summary.serverClassSchemas[static_cast<std::size_t>(state.classId)].name.c_str();
    }
    std::fprintf(stderr, "  entity=%d class=%d %s suffix=%s candidates=%zu\n",
                 entityIndex, state.classId, className, suffix.c_str(), ranked.size());
    // The freshest write among the candidates, which is the age a rule with
    // freshness awareness would get. -1 means no candidate carries a tick.
    std::int32_t freshest = -1;
    for (const auto& candidate : ranked) {
      if (candidate.lastWriteTick > freshest) freshest = candidate.lastWriteTick;
    }
    for (std::size_t rank = 0; rank < ranked.size(); ++rank) {
      const auto& candidate = ranked[rank];
      ++candidates;
      const auto& value = state.properties.at(candidate.name);
      std::string age = "n/a";
      if (candidate.lastWriteTick >= 0) {
        ++withTick;
        age = std::to_string(resolvedTick - candidate.lastWriteTick);
      }
      std::fprintf(stderr,
                   "    rank=%zu%s name=%s type=%d x=%.6f y=%.6f z=%.6f int=%lld lastWrite=%d age=%s\n",
                   rank, rank == 0 ? " (chosen)" : "", candidate.name.c_str(),
                   static_cast<int>(value.type), value.x, value.y, value.z,
                   static_cast<long long>(value.intValue), candidate.lastWriteTick, age.c_str());
    }
    if (ranked.front().lastWriteTick >= 0 && freshest >= 0) {
      const std::int32_t age = resolvedTick - ranked.front().lastWriteTick;
      if (age > worstAge) worstAge = age;
      if (ranked.front().lastWriteTick < freshest) ++chosenStale;
    }
  }
  std::fprintf(stderr,
               "prop-candidates-summary ticks=%zu examined=%zu withCandidates=%zu candidates=%zu "
               "withTick=%zu chosenStale=%zu worstChosenAge=%d\n",
               ticks.size(), examined, withCandidates, candidates, withTick, chosenStale, worstAge);
  std::fflush(stderr);
}

} // namespace

// ---------------------------------------------------------------------------
// T57: the player-model audit.
//
// WHY THIS MODE EXISTS
//   The nine TF2 classes are the subject of the T57 round, and before this mode
//   the only readings of them were ad-hoc: one --dump-model-textures run per
//   model, a hand-written Python VPK reader for the LOD counts, and a gate that
//   checked two models' material-slot totals. A subject measured in three
//   places with three shapes cannot be asserted on, and "the list is complete"
//   was a claim about a spreadsheet rather than about the loader.
//
// WHAT IT MEASURES, AND WHY EACH NUMBER IS THE LOADER'S
//   * resolution/companion flags: ModelLoader::resolveAsset's own answer.
//   * LOD counts: VVD header word 12 and VTX header word 20, read from the same
//     bytes the loader reads. ModelMetadata does not retain them (it only needs
//     LOD0), so re-reading two fixed words is the reading rather than a second
//     parser.
//   * lod0RenderIndices / lod0Built / lod0Lost: the LOD0 descriptor sum from the
//     decoded VTX hierarchy, and the vertex count buildBindPoseMeshLod0 actually
//     produced under its 12000-vertex budget. The product calls the same
//     function with the same default, so lod0Lost > 0 is the truncation the
//     world pass pays, measured at the site that pays it.
//   * material slots: the MDL mstudiomesh_t.material table cross-validated
//     against the VTX descriptors (ModelMetadata::meshMaterials), plus slot 0
//     resolved end to end (name -> VMT -> base VTF). paintedSlots=1 is what the
//     world pass binds (main.cpp takes textureCandidates.front()); it is printed
//     next to slotsUsedByMeshes so "one paint for a seventeen-slot model" is a
//     comparison rather than a claim.
//
// A class that does not resolve to a renderable model is a FAILURE of the mode,
// not a row: the audit's first job is to prove the subject exists.
// ---------------------------------------------------------------------------

struct PlayerModelClassEntry {
  int tfClass = 0;
  std::string modelPath;
};

const char* playerClassDisplayName(int tfClass) {
  switch (tfClass) {
    case 1: return "Scout";
    case 2: return "Sniper";
    case 3: return "Soldier";
    case 4: return "Demoman";
    case 5: return "Medic";
    case 6: return "Heavy";
    case 7: return "Pyro";
    case 8: return "Spy";
    case 9: return "Engineer";
    default: return "Unknown";
  }
}

// The default table is the product's own mapping, not a second copy of it:
// a class table that drifted from EntityModelResolver::defaultPlayerModelPath
// would make the audit disagree with the thing it audits.
std::vector<PlayerModelClassEntry> defaultPlayerModelClassTable() {
  std::vector<PlayerModelClassEntry> table;
  for (int tfClass = 1; tfClass <= 9; ++tfClass) {
    table.push_back(PlayerModelClassEntry{tfClass, tf2::native::EntityModelResolver::defaultPlayerModelPath(tfClass)});
  }
  return table;
}

const char* modelAssetResolutionName(tf2::native::ModelAssetResolution resolution) {
  switch (resolution) {
    case tf2::native::ModelAssetResolution::Missing: return "Missing";
    case tf2::native::ModelAssetResolution::FoundLoose: return "FoundLoose";
    case tf2::native::ModelAssetResolution::FoundVpk: return "FoundVpk";
    case tf2::native::ModelAssetResolution::Ambiguous: return "Ambiguous";
    case tf2::native::ModelAssetResolution::Unknown: break;
  }
  return "Unknown";
}

std::uint32_t readLittleEndianU32(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
  if (offset + 4 > bytes.size()) return 0;
  return static_cast<std::uint32_t>(bytes[offset])
    | (static_cast<std::uint32_t>(bytes[offset + 1]) << 8)
    | (static_cast<std::uint32_t>(bytes[offset + 2]) << 16)
    | (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

std::vector<std::uint8_t> readAssetBytes(const tf2::native::AssetRoot& assets, const std::string& resource) {
  std::error_code error;
  const auto loose = assets.resolve(resource);
  if (!loose.empty() && std::filesystem::is_regular_file(loose, error)) {
    std::ifstream input(loose, std::ios::binary);
    if (input) {
      return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    }
  }
  std::vector<std::filesystem::path> holders;
  assets.archives().collectContaining(resource, holders);
  for (const auto& holder : holders) {
    const auto* archive = assets.archives().find(holder);
    if (!archive) continue;
    auto bytes = archive->read(resource);
    if (!bytes.empty()) return bytes;
  }
  return {};
}

// A class table is a file of "<tfClass> <model path>" lines, comments allowed.
// The tampered-table negative control is the reason this exists: a gate that
// can only run the built-in list cannot show that it reads a table at all.
bool loadPlayerModelClassTable(const std::string& path, std::vector<PlayerModelClassEntry>& table, std::string& error) {
  std::ifstream input(path);
  if (!input) { error = "cannot read " + path; return false; }
  std::string line;
  int lineNumber = 0;
  while (std::getline(input, line)) {
    ++lineNumber;
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
    std::size_t first = 0;
    while (first < line.size() && (line[first] == ' ' || line[first] == '\t')) ++first;
    if (first >= line.size() || line[first] == '#') continue;
    std::istringstream fields(line.substr(first));
    int tfClass = 0;
    std::string modelPath;
    if (!(fields >> tfClass >> modelPath)) {
      error = "line " + std::to_string(lineNumber) + " is not '<tfClass> <model path>'";
      return false;
    }
    table.push_back(PlayerModelClassEntry{tfClass, modelPath});
  }
  if (table.size() != 9) { error = "entries=" + std::to_string(table.size()) + " expected=9"; return false; }
  std::set<int> seen;
  for (const auto& entry : table) {
    if (entry.tfClass < 1 || entry.tfClass > 9) { error = "tfClass out of range: " + std::to_string(entry.tfClass); return false; }
    if (!seen.insert(entry.tfClass).second) { error = "duplicate tfClass: " + std::to_string(entry.tfClass); return false; }
    if (entry.modelPath.size() < 4 || entry.modelPath.compare(entry.modelPath.size() - 4, 4, ".mdl") != 0) {
      error = "model path is not .mdl: " + entry.modelPath;
      return false;
    }
  }
  return true;
}

bool resolveMaterialSlotChain(const tf2::native::AssetRoot& assets, const tf2::native::ModelMetadata& metadata,
                              std::uint32_t skinFamily, std::int32_t slot,
                              tf2::native::MaterialSlotResolution& out) {
  const auto reader = [&assets](const std::string& resource) { return readAssetBytes(assets, resource); };
  return tf2::native::resolveMaterialSlot(metadata.skinFamilies, metadata.textureCandidates,
    skinFamily, slot, reader, out);
}

// 8.3-2: a model index is not an anchor. Every claim this round makes about a
// sequence ("a_flinch01 is 13 frames", "six classes have no crouch locomotion")
// names a model path, and a path alone cannot tell two installs apart. The digest
// below is taken over exactly the bytes the loader reads (the VPK member, or the
// loose file when one is present), so the anchor is content, not a name.
std::string sha256Hex(const std::uint8_t* data, std::size_t size) {
  static const std::uint32_t kRound[64] = {
    0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
    0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
    0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
    0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
    0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
    0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
    0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
    0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u};
  std::uint32_t h[8] = {0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,
                        0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u};
  const auto rotr = [](std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); };
  std::vector<std::uint8_t> block;
  block.reserve(((size + 8) / 64 + 1) * 64);
  for (std::size_t i = 0; i < size; ++i) block.push_back(data[i]);
  block.push_back(0x80u);
  while (block.size() % 64 != 56) block.push_back(0u);
  const std::uint64_t bits = static_cast<std::uint64_t>(size) * 8u;
  for (int i = 7; i >= 0; --i) block.push_back(static_cast<std::uint8_t>((bits >> (i * 8)) & 0xffu));
  std::uint32_t w[64];
  for (std::size_t offset = 0; offset < block.size(); offset += 64) {
    for (int i = 0; i < 16; ++i) {
      const std::size_t at = offset + static_cast<std::size_t>(i) * 4;
      w[i] = (static_cast<std::uint32_t>(block[at]) << 24) | (static_cast<std::uint32_t>(block[at + 1]) << 16)
           | (static_cast<std::uint32_t>(block[at + 2]) << 8) | static_cast<std::uint32_t>(block[at + 3]);
    }
    for (int i = 16; i < 64; ++i) {
      const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
      const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; ++i) {
      const std::uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
      const std::uint32_t ch = (e & f) ^ (~e & g);
      const std::uint32_t t1 = hh + s1 + ch + kRound[i] + w[i];
      const std::uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
      const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
      const std::uint32_t t2 = s0 + maj;
      hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
  }
  char out[65];
  for (int i = 0; i < 8; ++i) std::snprintf(out + i * 8, 9, "%08x", h[i]);
  return std::string(out, 64);
}

// index -> file -> content digest, plus the skeleton the file declares. The rig
// name here is the studiohdr name (the file's own statement about what it is) and
// the root bone is printed beside it, because a rig "identity" made of one string
// is exactly the kind of assumption that later reads as a fact.
void printModelFingerprints(const tf2::native::DemoNetworkSummary& summary,
                            const tf2::native::AssetRoot& assets, const std::string& filter) {
  std::vector<std::pair<std::uint16_t, std::string>> entries(
      summary.modelPrecache.begin(), summary.modelPrecache.end());
  std::sort(entries.begin(), entries.end(),
            [](const auto& left, const auto& right) { return left.first < right.first; });
  std::size_t shown = 0, hashed = 0, missing = 0;
  for (const auto& [index, path] : entries) {
    if (filter != "*" && path.find(filter) == std::string::npos) continue;
    ++shown;
    const auto bytes = readAssetBytes(assets, path);
    const std::string digest = bytes.empty() ? std::string() : sha256Hex(bytes.data(), bytes.size());
    if (bytes.empty()) { ++missing; } else { ++hashed; }
    // The studiohdr fields a fingerprint needs are at fixed offsets, so the name,
    // checksum and bone count come straight out of the bytes already read -- no
    // second pass over the archive. Only the player models get a full inspection,
    // because that is where the rig name and sequence table live and where the
    // action table's claims are anchored; inspecting all nine hundred models would
    // turn a fingerprint run into a minute of parsing.
    std::string mdlName = "-";
    std::string rootBone = "-";
    std::uint32_t checksum = 0, bones = 0, sequences = 0;
    if (bytes.size() > 160) {
      std::size_t nameLength = 0;
      while (nameLength < 64u && bytes[12 + nameLength] != 0) ++nameLength;
      if (nameLength > 0) {
        mdlName.assign(reinterpret_cast<const char*>(bytes.data() + 12), nameLength);
      }
      checksum = readLittleEndianU32(bytes, 8);
      bones = readLittleEndianU32(bytes, 156);
    }
    if (path.find("models/player/") != std::string::npos) {
      const auto candidate = tf2::native::ModelLoader::resolveAsset(assets, path);
      const auto inspection = tf2::native::ModelLoader::inspect(candidate.resources);
      if (inspection.metadata.valid) {
        const auto& metadata = inspection.metadata;
        if (!metadata.name.empty()) mdlName = metadata.name;
        checksum = metadata.checksum != 0 ? metadata.checksum : checksum;
        bones = metadata.boneCount != 0 ? metadata.boneCount : bones;
        sequences = metadata.sequenceCount;
        if (!metadata.bones.empty() && !metadata.bones.front().name.empty()) {
          rootBone = metadata.bones.front().name;
        }
      }
    }
    std::fprintf(stdout,
      "model-fingerprint idx=%u path=%s bytes=%zu sha256=%s mdlName=%s checksum=%u bones=%u sequences=%u rootBone=%s\n",
      static_cast<unsigned>(index), path.c_str(), bytes.size(),
      digest.empty() ? "-" : digest.c_str(), mdlName.c_str(), checksum, bones, sequences, rootBone.c_str());
  }
  std::fprintf(stdout, "model-fingerprint summary entries=%zu shown=%zu hashed=%zu missing=%zu filter=%s\n",
               summary.modelPrecache.size(), shown, hashed, missing, filter.c_str());
  std::fflush(stdout);
}

int runPlayerModelAudit(const std::string& tfRoot, const std::string& classTablePath) {
  const auto assets = tf2::native::AssetRoot::fromPath(tfRoot);
  if (!assets.valid()) {
    std::fprintf(stdout, "player-model-audit FAIL tf-root is not a valid AssetRoot: %s\n", tfRoot.c_str());
    return 2;
  }
  std::vector<PlayerModelClassEntry> table = defaultPlayerModelClassTable();
  if (!classTablePath.empty()) {
    table.clear();
    std::string error;
    if (!loadPlayerModelClassTable(classTablePath, table, error)) {
      std::fprintf(stdout, "player-model-audit FAIL class-table %s\n", error.c_str());
      return 2;
    }
  }
  std::vector<tf2::native::AssetReference> references;
  references.reserve(table.size());
  for (const auto& entry : table) {
    tf2::native::AssetReference reference;
    reference.entityIndex = static_cast<std::uint16_t>(entry.tfClass);
    reference.hasModelPath = true;
    reference.modelPath = entry.modelPath;
    reference.className = "CTFPlayer";
    references.push_back(reference);
  }
  const auto requests = tf2::native::ModelLoader::buildRenderRequests(assets, references, nullptr, nullptr);
  std::unordered_map<std::string, const tf2::native::ModelRenderRequest*> requestByPath;
  for (const auto& request : requests) requestByPath[tf2::native::normalizeModelPath(request.modelPath)] = &request;

  std::size_t resolvedClasses = 0, renderableClasses = 0, companionCompleteClasses = 0;
  std::size_t lod0TruncatedClasses = 0, slot0ResolvedClasses = 0, lodCountKnownClasses = 0;
  std::size_t vvdLodsMin = 0, vvdLodsMax = 0, vtxLodsMin = 0, vtxLodsMax = 0;
  long long lod0LostTotal = 0;
  // T60 readings: what the product would paint, per class and in total.
  std::size_t lod0SkippedTotal = 0;
  std::size_t productPaintedSlots = 0;
  std::size_t headOnBodyClasses = 0;
  std::size_t redBluDistinctTotal = 0;
  // Sentinel, not 0: a class that paints nothing must not be able to overwrite
  // the minimum. With 0 as both the initial value and the "unset" test, the
  // first class set 0 and every later class overwrote it, so the summary read 2
  // while seven classes were 0 -- an absent value that read as benign.
  std::size_t paintedFamiliesMin = static_cast<std::size_t>(-1);
  std::size_t distinctPaintedStemsMin = static_cast<std::size_t>(-1);
  std::size_t classesWithPaint = 0;
  for (const auto& entry : table) {
    const char* display = playerClassDisplayName(entry.tfClass);
    const auto candidate = tf2::native::ModelLoader::resolveAsset(assets, entry.modelPath);
    const auto found = requestByPath.find(tf2::native::normalizeModelPath(entry.modelPath));
    if (found == requestByPath.end() || candidate.resolution == tf2::native::ModelAssetResolution::Missing) {
      std::fprintf(stdout, "player-model class=%s tfClass=%d path=%s resolved=0 renderable=0 missing=1 resolution=%s\n",
                   display, entry.tfClass, entry.modelPath.c_str(), modelAssetResolutionName(candidate.resolution));
      continue;
    }
    ++resolvedClasses;
    const auto& request = *found->second;
    const auto& inspection = request.inspection;
    const auto& metadata = inspection.metadata;
    const std::string stem = entry.modelPath.substr(0, entry.modelPath.size() - 4);
    const auto mdlBytes = readAssetBytes(assets, entry.modelPath);
    // studiohdr_t 220/224: the skin-reference table the renderer never reads. Read
    // here so "one paint for a seventeen-slot model" carries the slot table size
    // the asset actually declares, not a number copied from a doc.
    const std::uint32_t skinRefs = readLittleEndianU32(mdlBytes, 220);
    const std::uint32_t skinFamilies = readLittleEndianU32(mdlBytes, 224);
    const auto vvdBytes = readAssetBytes(assets, stem + ".vvd");
    const auto vtxBytes = readAssetBytes(assets, stem + ".dx90.vtx");
    const std::uint32_t vvdLods = readLittleEndianU32(vvdBytes, 12);
    const std::uint32_t vtxLods = readLittleEndianU32(vtxBytes, 20);
    if (vvdLods > 0 && vtxLods > 0) {
      ++lodCountKnownClasses;
      if (lodCountKnownClasses == 1) {
        vvdLodsMin = vvdLodsMax = vvdLods;
        vtxLodsMin = vtxLodsMax = vtxLods;
      } else {
        vvdLodsMin = std::min(vvdLodsMin, static_cast<std::size_t>(vvdLods));
        vvdLodsMax = std::max(vvdLodsMax, static_cast<std::size_t>(vvdLods));
        vtxLodsMin = std::min(vtxLodsMin, static_cast<std::size_t>(vtxLods));
        vtxLodsMax = std::max(vtxLodsMax, static_cast<std::size_t>(vtxLods));
      }
    }
    std::size_t lod0Descriptors = 0, lod0Indices = 0;
    for (const auto& descriptor : metadata.indices) {
      if (descriptor.lod != 0) continue;
      ++lod0Descriptors;
      lod0Indices += descriptor.renderIndexCount;
    }
    auto meshMetadata = metadata;
    std::string meshError;
    const bool meshBuilt = tf2::native::ModelLoader::buildBindPoseMeshLod0(meshMetadata, meshError);
    const std::size_t lod0Built = meshBuilt ? meshMetadata.bindPoseVertexCount : 0;
    // T60/A: the budget is derived from the model's own LOD0 index count, so it
    // is published next to the built count. A built value that equals the budget
    // is not a fit unless the budget itself came from this model.
    const std::size_t lod0Budget = meshMetadata.bindPoseLod0Budget;
    const std::size_t lod0Skipped = meshMetadata.bindPoseLod0Skipped;
    const long long lod0Lost = static_cast<long long>(lod0Indices) - static_cast<long long>(lod0Built);
    if (lod0Lost > 0) ++lod0TruncatedClasses;
    lod0LostTotal += lod0Lost > 0 ? lod0Lost : 0;
    lod0SkippedTotal += lod0Skipped;
    std::set<int> usedSlots;
    if (metadata.meshMaterials.available) {
      for (const auto& descriptor : metadata.indices) {
        const int slot = metadata.meshMaterials.slotFor(descriptor.bodyPart, descriptor.model, descriptor.mesh);
        if (slot >= 0) usedSlots.insert(slot);
      }
    }
    int meshMaterialSlotMax = -1;
    for (const int slot : usedSlots) meshMaterialSlotMax = std::max(meshMaterialSlotMax, slot);
    // T60/B: the runs are the LOD0 slot layout; the skin family table turns each
    // material slot into a texture slot for red (0) and blu (1). Both families
    // are resolved because a class model is worn by both teams.
    std::vector<tf2::native::ModelMeshMaterialRun> materialRuns;
    std::string materialRunsError;
    const bool runsBuilt = meshBuilt
      && tf2::native::ModelLoader::buildMaterialRuns(meshMetadata, materialRuns, materialRunsError);
    std::set<std::uint32_t> lod0Slots;
    std::uint32_t dominantSlot = 0;
    std::uint32_t dominantVertices = 0;
    for (const auto& run : materialRuns) {
      lod0Slots.insert(run.materialSlot);
      if (run.vertexCount > dominantVertices) {
        dominantVertices = run.vertexCount;
        dominantSlot = run.materialSlot;
      }
    }
    std::map<std::uint32_t, std::array<std::string, 2>> slotVtf;
    std::map<std::uint32_t, std::array<std::string, 2>> slotStem;
    std::size_t paintedSlots = 0;
    std::size_t familySlots[2] = {0, 0};
    std::string headStem;
    for (const std::uint32_t family : {0u, 1u}) {
      for (const auto slot : lod0Slots) {
        tf2::native::MaterialSlotResolution resolution;
        if (!resolveMaterialSlotChain(assets, meshMetadata, family, static_cast<std::int32_t>(slot), resolution)) {
          continue;
        }
        ++paintedSlots;
        ++familySlots[family];
        slotVtf[slot][family] = resolution.vtfPath;
        slotStem[slot][family] = resolution.textureStem;
        if (family == 0 && resolution.textureStem.find("_head") != std::string::npos) {
          headStem = resolution.textureStem;
        }
      }
    }
    std::size_t redBluDistinct = 0;
    for (const auto& entry : slotVtf) {
      if (entry.second[0].empty() || entry.second[1].empty()) continue;
      if (entry.second[0] != entry.second[1]) ++redBluDistinct;
    }
    // How many different textures this class would actually paint. One is the
    // T57 reading (slot 0 for every mesh); a correct per-run bind paints one per
    // distinct material slot, so this count is what tells them apart.
    std::set<std::string> paintedStems;
    for (const auto& entry : slotStem) {
      if (!entry.second[0].empty()) paintedStems.insert(entry.second[0]);
    }
    // "The head is not painted on the body". The body shell is the run with the
    // most vertices among runs whose OWN material slot is declared as a non-head
    // texture -- not simply the largest run: TF2 heads are often the largest
    // single mesh (sniper head 861 verts vs body 789; spy head 1036 vs body 361),
    // so the old "largest run is the body" definition was wrong for this family.
    // The declared stem comes from the skin table, the resolved stem from the
    // paint chain; if the chain fell back to slot 0 they would disagree and this
    // reading would go red.
    const auto declaredStemFor = [&meshMetadata](std::uint32_t slot) {
      const auto textureSlot = meshMetadata.skinFamilies.textureSlotFor(0, static_cast<std::int32_t>(slot));
      if (textureSlot < 0 || static_cast<std::size_t>(textureSlot) >= meshMetadata.textureNames.size()) return std::string();
      return meshMetadata.textureNames[static_cast<std::size_t>(textureSlot)];
    };
    std::uint32_t bodySlot = 0;
    std::uint32_t bodyVertices = 0;
    for (const auto& run : materialRuns) {
      const std::string declared = declaredStemFor(run.materialSlot);
      if (declared.empty() || declared.find("_head") != std::string::npos) continue;
      if (run.vertexCount > bodyVertices) { bodyVertices = run.vertexCount; bodySlot = run.materialSlot; }
    }
    const auto bodyResolved = slotStem.find(bodySlot);
    const std::string bodyStem = bodyResolved == slotStem.end() ? std::string() : bodyResolved->second[0];
    const bool headOnBody = !bodyStem.empty() && bodyStem.find("_head") != std::string::npos;
    const auto dominantStemEntry = slotStem.find(dominantSlot);
    const std::string dominantStem = dominantStemEntry == slotStem.end() ? std::string() : dominantStemEntry->second[0];
    if (headOnBody) ++headOnBodyClasses;
    productPaintedSlots += paintedSlots;
    redBluDistinctTotal += redBluDistinct;
    std::size_t paintedFamilies = 0;
    for (const std::uint32_t family : {0u, 1u}) {
      if (lod0Slots.empty()) continue;
      if (familySlots[family] == lod0Slots.size()) ++paintedFamilies;
    }
    if (paintedFamilies < paintedFamiliesMin) paintedFamiliesMin = paintedFamilies;
    if (paintedStems.size() < distinctPaintedStemsMin) distinctPaintedStemsMin = paintedStems.size();
    if (paintedSlots > 0) ++classesWithPaint;
    tf2::native::MaterialSlotResolution slot0Resolution;
    const bool slot0Resolved = resolveMaterialSlotChain(assets, meshMetadata, 0, 0, slot0Resolution);
    if (slot0Resolved) ++slot0ResolvedClasses;
    const std::string slot0Name = slot0Resolution.textureStem;
    const std::string slot0Vmt = slot0Resolution.vmtPath;
    const std::string slot0Vtf = slot0Resolution.vtfPath;
    if (request.renderable) ++renderableClasses;
    if (candidate.companionSetComplete) ++companionCompleteClasses;
    std::fprintf(stdout,
      "player-model class=%s tfClass=%d path=%s resolution=%s companions=vvd=%d,dx90=%d,dx80=%d,sw=%d,phy=%d "
      "companionComplete=%d renderable=%d mdlVersion=%u checksum=%u bytes=%llu bones=%u sequences=%u textures=%u "
      "bodyParts=%u attachments=%u vvdLods=%u vtxLods=%u vvdVertices=%zu lod0Descriptors=%zu lod0RenderIndices=%zu "
      "lod0Budget=%zu lod0Built=%zu lod0Skipped=%zu lod0Lost=%lld meshMaterialTable=%d meshMaterialSlotMax=%d "
      "materialRuns=%zu lod0MaterialSlots=%zu paintedSlots=%zu paintedFamilies=%zu redBluDistinctSlots=%zu "
      "distinctPaintedStems=%zu headOnBody=%d dominantSlot=%u dominantStem=%s bodySlot=%u bodyStem=%s headStem=%s "
      "skinRefs=%u skinFamilies=%u slotsUsedByMeshes=%zu slot0=%s "
      "slot0Vmt=%s slot0Vtf=%s slot0Resolved=%d\n",
      display, entry.tfClass, entry.modelPath.c_str(), modelAssetResolutionName(candidate.resolution),
      candidate.vvdFound ? 1 : 0, candidate.dx90VtxFound ? 1 : 0, candidate.dx80VtxFound ? 1 : 0,
      candidate.swVtxFound ? 1 : 0, candidate.phyFound ? 1 : 0, candidate.companionSetComplete ? 1 : 0,
      request.renderable ? 1 : 0, inspection.mdl.version, inspection.mdl.checksum,
      static_cast<unsigned long long>(inspection.mdl.bytes), metadata.boneCount, metadata.sequenceCount,
      metadata.textureCount, metadata.bodyPartCount, metadata.attachmentCount, vvdLods, vtxLods,
      metadata.vertices.size(), lod0Descriptors, lod0Indices, lod0Budget, lod0Built, lod0Skipped, lod0Lost,
      metadata.meshMaterials.available ? 1 : 0, meshMaterialSlotMax, materialRuns.size(), lod0Slots.size(),
      paintedSlots, paintedFamilies, redBluDistinct, paintedStems.size(),
      headOnBody ? 1 : 0, dominantSlot, dominantStem.empty() ? "-" : dominantStem.c_str(),
      bodySlot, bodyStem.empty() ? "-" : bodyStem.c_str(),
      headStem.empty() ? "-" : headStem.c_str(),
      skinRefs, skinFamilies, usedSlots.size(),
      slot0Name.empty() ? "-" : slot0Name.c_str(), slot0Vmt.empty() ? "-" : slot0Vmt.c_str(),
      slot0Vtf.empty() ? "-" : slot0Vtf.c_str(), slot0Resolved ? 1 : 0);
  }
  std::fprintf(stdout,
    "player-model-audit summary classes=%zu resolved=%zu renderable=%zu companionsComplete=%zu "
    "lodCountKnown=%zu vvdLodsMin=%zu vvdLodsMax=%zu vtxLodsMin=%zu vtxLodsMax=%zu "
    "lod0Truncated=%zu lod0LostTotal=%lld lod0SkippedTotal=%zu "
    "classesWithPaint=%zu productPaintedSlots=%zu paintedFamiliesMin=%zu distinctPaintedStemsMin=%zu headOnBodyClasses=%zu "
    "redBluDistinctSlotsTotal=%zu slot0Resolved=%zu\n",
    table.size(), resolvedClasses, renderableClasses, companionCompleteClasses, lodCountKnownClasses,
    vvdLodsMin, vvdLodsMax, vtxLodsMin, vtxLodsMax, lod0TruncatedClasses, lod0LostTotal, lod0SkippedTotal,
    classesWithPaint, productPaintedSlots,
    paintedFamiliesMin == static_cast<std::size_t>(-1) ? 0 : paintedFamiliesMin,
    distinctPaintedStemsMin == static_cast<std::size_t>(-1) ? 0 : distinctPaintedStemsMin,
    headOnBodyClasses, redBluDistinctTotal,
    slot0ResolvedClasses);
  if (resolvedClasses != table.size()) {
    std::fprintf(stderr, "player-model-audit FAIL %zu of %zu class models did not resolve\n", table.size() - resolvedClasses, table.size());
    return 1;
  }
  if (renderableClasses != table.size()) {
    std::fprintf(stderr, "player-model-audit FAIL %zu of %zu class models are not renderable\n",
                 table.size() - renderableClasses, table.size());
    return 1;
  }
  return 0;
}

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
  std::vector<std::int32_t> propCandidatesAt;
  std::string candidateSuffix;
  int propsAtEntity = -1;
  std::string classPropsFilter;
  std::string precacheFilter;
  std::string modelTexturePath;
  std::string modelSlotsPath;
  bool weaponModelDump = false;
  bool animationPropsDump = false;
  bool historyStats = false;
  std::size_t historyCoverageSamples = 0;
  bool renderedOrigins = false;
  bool playerModelAudit = false;
  bool modelFingerprints = false;
  std::string modelFingerprintFilter = "*";
  std::string classTablePath;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--self-test") selfTest = true;
    else if (arg == "--tf-root" && i + 1 < argc) tfRoot = argv[++i];
    else if (arg == "--demo" && i + 1 < argc) demoPath = argv[++i];
    else if (arg == "--dump-class-props" && i + 1 < argc) classPropsFilter = argv[++i];
    else if (arg == "--dump-precache" && i + 1 < argc) precacheFilter = argv[++i];
    else if (arg == "--dump-model-textures" && i + 1 < argc) modelTexturePath = argv[++i];
    else if (arg == "--dump-model-slots" && i + 1 < argc) modelSlotsPath = argv[++i];
    else if (arg == "--dump-weapon-models") weaponModelDump = true;
    else if (arg == "--anim-props") animationPropsDump = true;
    else if (arg == "--history-stats") historyStats = true;
    else if (arg == "--history-coverage" && i + 1 < argc) historyCoverageSamples = std::strtoul(argv[++i], nullptr, 10);
    else if (arg == "--rendered") renderedOrigins = true;
    else if (arg == "--entity" && i + 1 < argc) propsAtEntity = static_cast<int>(std::strtol(argv[++i], nullptr, 10));
    else if (arg == "--props-at" && i + 1 < argc) parseTickList(argv[++i], propsAt);
    else if (arg == "--camera-at" && i + 1 < argc) parseTickList(argv[++i], cameraAt);
    else if (arg == "--observer-focus-at" && i + 1 < argc) parseTickList(argv[++i], observerFocusAt);
    else if (arg == "--prop-candidates-at" && i + 1 < argc) parseTickList(argv[++i], propCandidatesAt);
    else if (arg == "--candidate-suffix" && i + 1 < argc) candidateSuffix = argv[++i];
    else if (arg == "--trajectory" && i + 1 < argc) trajectoryTicks = std::strtoul(argv[++i], nullptr, 10);
    else if (arg == "--trajectory-dump") trajectoryDump = true;
    else if (arg == "--trajectory-at" && i + 1 < argc) parseTickList(argv[++i], trajectoryAt);
    else if (arg == "--player-model-audit") playerModelAudit = true;
    else if (arg == "--model-fingerprints") { modelFingerprints = true; if (i + 1 < argc) modelFingerprintFilter = argv[++i]; }
    else if (arg == "--class-table" && i + 1 < argc) classTablePath = argv[++i];
  }
  if (selfTest && !runSelfTest()) return fail("self-test failed");
  if (playerModelAudit) {
    if (tfRoot.empty()) return fail("--player-model-audit needs --tf-root");
    return runPlayerModelAudit(tfRoot, classTablePath);
  }

  std::size_t uniqueRenderable = 0;
  std::size_t vpkRenderable = 0;
  std::size_t cacheHits = 0;
  std::size_t inspections = 0;
  std::size_t scoutVertices = 0;
  std::uint32_t scoutChecksum = 0;
  std::size_t instanceCount = 0;
  std::size_t playerFallbacks = 0;
  // The synthetic fixture at the end of the tf-root block is the only path where
  // the class-fallback sweep fires (see the comment there), so its resolver
  // counters are captured separately from any demo run's.
  std::size_t fixtureFallbackScanned = 0;
  std::size_t fixtureClassLookups = 0;
  std::size_t fixtureTransforms = 0;
  std::size_t fixturePropertyComparisons = 0;
  std::size_t fixtureInstanceCount = 0;
  std::size_t fixtureFallbacks = 0;
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
    // Dump the `mstudiotexture_t` name table of one named model. This is the
    // reading the entity-material round rests on: the paint the renderer can
    // reach for a model is exactly this list, each entry a material path stem
    // under `materials/`, and no other mode of this probe prints it. Silent
    // unless the flag is passed, so no existing consumer of this output moves.
    // Dump the material-slot chain of one named model: the texture table, the
    // skin family table, the per-mesh material index, and the LOD0 descriptor
    // slot mapping for red (0) and blu (1). T60/B rests on this: "which texture
    // do these triangles get" has one answer per team, and printing the skinref
    // table beside the mesh table is what makes the answer checkable instead of
    // asserted.
    if (!modelSlotsPath.empty()) {
      tf2::native::AssetReference slotProbe;
      slotProbe.entityIndex = 1;
      slotProbe.hasModelPath = true;
      slotProbe.modelPath = modelSlotsPath;
      slotProbe.className = "CBaseAnimating";
      const auto slotRequests = tf2::native::ModelLoader::buildRenderRequests(assets, {slotProbe}, nullptr, nullptr);
      if (slotRequests.size() != 1 || !slotRequests[0].renderable) {
        std::fprintf(stderr, "model-slots FAIL %s did not resolve to a renderable model\n", modelSlotsPath.c_str());
        return 2;
      }
      const auto& slotMetadata = slotRequests[0].inspection.metadata;
      const auto& skinTable = slotMetadata.skinFamilies;
      std::fprintf(stdout,
                   "model-slots path=%s textures=%u textureNames=%zu skinRefs=%u skinFamilies=%u available=%d reason=%s\n",
                   modelSlotsPath.c_str(), slotMetadata.textureCount, slotMetadata.textureNames.size(),
                   skinTable.skinRefCount, skinTable.skinFamilyCount, skinTable.available ? 1 : 0,
                   skinTable.reason.empty() ? "-" : skinTable.reason.c_str());
      for (std::size_t i = 0; i < slotMetadata.textureNames.size(); ++i) {
        std::fprintf(stdout, "model-slot-texture slot=%zu name=%s\n", i, slotMetadata.textureNames[i].c_str());
      }
      const auto textureNameAt = [&](std::int32_t slot) -> std::string {
        if (slot < 0 || static_cast<std::size_t>(slot) >= slotMetadata.textureNames.size()) return std::string("-");
        return slotMetadata.textureNames[static_cast<std::size_t>(slot)];
      };
      if (skinTable.available) {
        for (std::uint32_t family = 0; family < skinTable.skinFamilyCount && family < 2; ++family) {
          for (std::uint32_t ref = 0; ref < skinTable.skinRefCount; ++ref) {
            const std::int32_t slot = skinTable.textureSlotFor(family, static_cast<std::int32_t>(ref));
            std::fprintf(stdout, "model-slot-skinref family=%u ref=%u textureSlot=%d name=%s\n",
                         family, ref, slot, textureNameAt(slot).c_str());
          }
        }
      }
      const auto& materialTable = slotMetadata.meshMaterials;
      if (materialTable.available) {
        for (std::size_t bp = 0; bp < materialTable.slots.size(); ++bp) {
          for (std::size_t model = 0; model < materialTable.slots[bp].size(); ++model) {
            for (std::size_t mesh = 0; mesh < materialTable.slots[bp][model].size(); ++mesh) {
              const std::int32_t material = materialTable.slots[bp][model][mesh];
              std::fprintf(stdout, "model-slot-mesh bodyPart=%zu model=%zu mesh=%zu material=%d textureName=%s\n",
                           bp, model, mesh, material, textureNameAt(material).c_str());
            }
          }
        }
      }
      for (const auto& descriptor : slotMetadata.indices) {
        if (descriptor.lod != 0) continue;
        const std::int32_t material = materialTable.available
          ? materialTable.slotFor(descriptor.bodyPart, descriptor.model, descriptor.mesh) : -1;
        const std::int32_t redSlot = skinTable.available ? skinTable.textureSlotFor(0, material) : -1;
        const std::int32_t bluSlot = skinTable.available ? skinTable.textureSlotFor(1, material) : -1;
        std::fprintf(stdout,
                     "model-slot-descriptor lod0 bodyPart=%u model=%u mesh=%u renderIndexCount=%u material=%d "
                     "redSlot=%d redName=%s bluSlot=%d bluName=%s\n",
                     descriptor.bodyPart, descriptor.model, descriptor.mesh, descriptor.renderIndexCount,
                     material, redSlot, textureNameAt(redSlot).c_str(), bluSlot, textureNameAt(bluSlot).c_str());
      }
      return 0;
    }
    if (!modelTexturePath.empty()) {
      const auto readResource = [&](const std::string& resource) -> std::vector<std::uint8_t> {
        std::vector<std::filesystem::path> resourceHolders;
        assets.archives().collectContaining(resource, resourceHolders);
        for (const auto& holder : resourceHolders) {
          const auto* resourceArchive = assets.archives().find(holder);
          if (!resourceArchive) continue;
          auto bytes = resourceArchive->read(resource);
          if (!bytes.empty()) return bytes;
        }
        return {};
      };
      // Each slot resolved end to end, name -> .vmt -> base texture -> .vtf
      // bytes -> decoded RGBA. The slots are the model's whole paint budget, and
      // a slot whose material the archives cannot produce is one the renderer
      // will have to fall back for. Returns true only when the chain is complete
      // so the caller can count them.
      // A slot resolves by walking its candidate list -- for a bare stem, the
      // `$cdmaterials`-prefixed forms -- and taking the first one whose VMT and
      // VTF both read. Unresolved candidates still print a line, so a slot that
      // resolves on the second candidate shows both the miss and the hit, and a
      // slot that resolves on neither shows every directory that was tried.
      const auto resolveMaterialChain = [&](const std::vector<std::string>& candidates) {
        for (const auto& materialName : candidates) {
          const std::string vmtResource = tf2::native::VmtParser::resourcePath(materialName, ".vmt");
          const auto vmtBytes = readResource(vmtResource);
          tf2::native::VmtMaterial material;
          const bool materialParsed = !vmtBytes.empty()
            && tf2::native::VmtParser::parse(std::string(vmtBytes.begin(), vmtBytes.end()), material);
          if (!materialParsed || material.baseTexture.empty()) {
            std::fprintf(stderr, "model-material name=%s vmt=%s vmtBytes=%zu parsed=%d complete=0\n",
                         materialName.c_str(), vmtResource.c_str(), vmtBytes.size(), materialParsed ? 1 : 0);
            continue;
          }
          const std::string vtfResource = tf2::native::VmtParser::resourcePath(material.baseTexture, ".vtf");
          const auto vtfBytes = readResource(vtfResource);
          tf2::native::VtfTexture texture;
          const bool vtfParsed = !vtfBytes.empty() && texture.parse(vtfBytes);
          const std::size_t decodedBytes = vtfParsed ? texture.decodeRgba(vtfBytes).size() : 0;
          std::fprintf(stderr,
                       "model-material name=%s vmt=%s vmtBytes=%zu parsed=%d shader=%s base=%s"
                       " vtf=%s vtfBytes=%zu size=%ux%u decodedBytes=%zu complete=%d\n",
                       materialName.c_str(), vmtResource.c_str(), vmtBytes.size(), materialParsed ? 1 : 0,
                       material.shader.c_str(), material.baseTexture.c_str(),
                       vtfResource.c_str(), vtfBytes.size(), texture.header().width, texture.header().height,
                       decodedBytes, decodedBytes > 0 ? 1 : 0);
          if (decodedBytes > 0) return true;
        }
        return false;
      };
      tf2::native::AssetReference textureProbe;
      textureProbe.entityIndex = 1;
      textureProbe.hasModelPath = true;
      textureProbe.modelPath = modelTexturePath;
      textureProbe.className = "CBaseAnimating";
      const auto textureRequests = tf2::native::ModelLoader::buildRenderRequests(assets, {textureProbe}, nullptr, nullptr);
      if (textureRequests.size() == 1 && textureRequests[0].renderable) {
        const auto& textureMetadata = textureRequests[0].inspection.metadata;
        std::fprintf(stderr, "model-textures path=%s declared=%u read=%zu\n",
                     modelTexturePath.c_str(), textureMetadata.textureCount, textureMetadata.textureNames.size());
        std::fprintf(stderr, "model-cdtextures count=%zu", textureMetadata.cdTexturePaths.size());
        for (const auto& cdPath : textureMetadata.cdTexturePaths) {
          std::fprintf(stderr, " [%s]", cdPath.c_str());
        }
        std::fprintf(stderr, "\n");
        for (std::size_t slot = 0; slot < textureMetadata.textureNames.size(); ++slot) {
          std::fprintf(stderr, "  texture[%zu] %s\n", slot,
                       textureMetadata.textureNames[slot].empty() ? "<empty>" : textureMetadata.textureNames[slot].c_str());
        }
        // The candidate list behind each slot, in the order the resolver tries
        // them. Printed separately so the `texture[i]` lines keep their shape.
        for (std::size_t slot = 0; slot < textureMetadata.textureCandidates.size(); ++slot) {
          std::fprintf(stderr, "  texture-candidates[%zu]", slot);
          for (const auto& candidate : textureMetadata.textureCandidates[slot]) {
            std::fprintf(stderr, " [%s]", candidate.c_str());
          }
          std::fprintf(stderr, "\n");
        }
        // Every slot resolved, not just slot 0 -- see resolveMaterialChain above.
        std::size_t resolvedSlots = 0;
        for (const auto& candidates : textureMetadata.textureCandidates) {
          if (candidates.empty()) continue;
          if (resolveMaterialChain(candidates)) ++resolvedSlots;
        }
        std::fprintf(stderr, "model-material summary slots=%zu resolved=%zu\n",
                     textureMetadata.textureNames.size(), resolvedSlots);
      } else {
        std::fprintf(stderr, "model-textures path=%s unresolved requests=%zu\n",
                     modelTexturePath.c_str(), textureRequests.size());
      }
      // Raw header words, read straight out of the archive. When a table pointer
      // in the header is wrong, the parsed result is an empty or nonsensical
      // list and there is nothing to compare it against -- the failure is
      // silent. Printing the candidate words is what turns "the names came back
      // empty" into "numtextures is at 204 and the parser is reading 212".
      std::vector<std::filesystem::path> holders;
      assets.archives().collectContaining(modelTexturePath, holders);
      for (const auto& holder : holders) {
        const auto* archive = assets.archives().find(holder);
        if (!archive) continue;
        std::string archiveError;
        const auto mdlBytes = archive->read(modelTexturePath, &archiveError);
        if (mdlBytes.size() < 320) continue;
        std::fprintf(stderr, "model-header words offset:value");
        for (const std::size_t offset : {188u, 192u, 196u, 200u, 204u, 208u, 212u, 216u,
                                         220u, 224u, 228u, 232u, 236u, 240u, 244u, 248u, 252u}) {
          std::int32_t value = 0;
          std::memcpy(&value, mdlBytes.data() + offset, sizeof(value));
          std::fprintf(stderr, " %zu:%d", offset, value);
        }
        std::fprintf(stderr, " fileBytes=%zu\n", mdlBytes.size());
        break;
      }
    }
    if (inspections > 4) duplicateStable = false;

    std::vector<tf2::native::EntityState> states(8);
    tf2::native::EntityPropertyValue origin;
    origin.type = tf2::native::SendPropType::Vector;
    origin.x = 10; origin.y = 20; origin.z = 30;
    // The five requests above cover entity indices 1..5, so the sweep only walks
    // indices 0 and 6..7. The two entities below are placed on indices the
    // requests do *not* cover, or the sweep would skip them and show nothing.
    //
    // Entity 7: player-named (classId 0 -> "CTFPlayer") but with no m_iClass at
    // all. This is the entity the cheap pre-check exists for: the old sweep built
    // it a full transform and then threw that work away when the transform came
    // back with no player class, the new sweep answers the class question first
    // and skips the transform. Without an entity in this shape the fixture cannot
    // show the pre-check saving anything, which is why it is constructed here.
    states[7].classId = 0;
    states[7].properties["DT_BaseEntity.m_vecOrigin"] = origin;
    tf2::native::EntityPropertyValue playerClass;
    playerClass.type = tf2::native::SendPropType::Int;
    playerClass.intValue = 3;
    // Entity 6: player-named *and* carrying m_iClass=3, so it survives the
    // pre-check and still gets its fallback instance. The pair (7, 6) is what
    // makes the claim falsifiable: entity 6 pins that a passing entity is not
    // dropped, entity 7 pins that a failing one is not transformed.
    states[6].classId = 0;
    states[6].properties["DT_BaseEntity.m_vecOrigin"] = origin;
    states[6].properties["DT_TFPlayer.m_iClass"] = playerClass;
    std::vector<tf2::native::ServerClassSchema> schemas(1);
    schemas[0].name = "CTFPlayer";
    // The class-fallback sweep only runs over entities no request already covers.
    // On every demo in the corpus every player entity already has a request, so
    // that sweep is cold there and the counters read zero -- which tells a reader
    // nothing about the path P1 asked about. This synthetic states table is the
    // one place the sweep fires, so its counters are kept separately and printed
    // under their own names.
    const auto instances = tf2::native::EntityModelResolver::buildInstances(first, states, schemas, 64);
    const auto& fixtureStats = tf2::native::lastResolverStats();
    fixtureFallbackScanned = fixtureStats.fallbackEntitiesScanned;
    fixtureClassLookups = fixtureStats.classLookups;
    fixtureTransforms = fixtureStats.transformsBuilt;
    fixturePropertyComparisons = fixtureStats.propertyComparisons;
    // The fixture's own result, captured before the demo block below overwrites
    // instanceCount/playerFallbacks with the demo call's. The gate asserts this
    // pair stays put while the transform count moves, which is the whole claim:
    // the pre-check spares work without dropping an instance.
    fixtureInstanceCount = instances.size();
    for (const auto& instance : instances) if (instance.playerClassFallback) ++fixtureFallbacks;
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
    if (modelFingerprints) printModelFingerprints(summary, assets, modelFingerprintFilter);
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
    // Work counters for the resolver, printed from the buildInstances call above
    // (the same call whose result is instanceCount/playerFallbacks). P1 asked to
    // measure property-lookup cost before changing the data structures, so these
    // are the reading that decision rests on: transformsBuilt counts full
    // transforms, fallbackEntitiesScanned counts entities walked for the class
    // sweep, propertyComparisons counts name-vs-suffix comparisons, and
    // classLookups counts entities the cheap pre-check spared a transform.
    //
    // The demo call's sweep is cold on this corpus (every player already has a
    // request), so `resolverFallbackScanned` reading 0 here is a fact about the
    // demos, not a claim that the path was never taken. The `fixture*` counters
    // below are the sweep's own reading, from the synthetic states table where it
    // does fire; a change to the sweep has to show up there or it is unmeasured.
    const auto& resolver = tf2::native::lastResolverStats();
    std::cout << ",\"resolverTransforms\":" << resolver.transformsBuilt
      << ",\"resolverFallbackScanned\":" << resolver.fallbackEntitiesScanned
      << ",\"resolverPropertyComparisons\":" << resolver.propertyComparisons
      << ",\"resolverClassLookups\":" << resolver.classLookups
      << ",\"fixtureFallbackScanned\":" << fixtureFallbackScanned
      << ",\"fixtureClassLookups\":" << fixtureClassLookups
      << ",\"fixtureTransforms\":" << fixtureTransforms
      << ",\"fixturePropertyComparisons\":" << fixturePropertyComparisons
      << ",\"fixtureInstanceCount\":" << fixtureInstanceCount
      << ",\"fixtureFallbacks\":" << fixtureFallbacks;
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
    if (!propCandidatesAt.empty()) {
      printPropCandidatesAt(summary, propCandidatesAt, propsAtEntity, candidateSuffix);
    }
    if (!classPropsFilter.empty()) printClassProps(summary, classPropsFilter);
    if (animationPropsDump) printAnimationProps(summary);
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
