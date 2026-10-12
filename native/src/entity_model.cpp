#include "entity_model.h"
// normalizeModelPath: the shared path-comparison rule. entity_model.h does not
// pull this in because the rule is a model-loader concern, and only the one
// comparison below needs it.
#include "model_loader.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_set>

namespace tf2::native {
namespace {

// Set by the most recent buildInstances call. See ResolverStats in the header.
ResolverStats g_resolverStats;

bool suffixMatch(const std::string& name, const char* suffix) {
  ++g_resolverStats.propertyComparisons;
  const auto length = std::strlen(suffix);
  if (name.size() == length && name == suffix) return true;
  if (name.size() > length && name.compare(name.size() - length, length, suffix) == 0) {
    return name[name.size() - length - 1] == '.';
  }
  return false;
}

// Lower is better. A player carries its position twice: the copy the owning
// client receives (DT_TFLocalPlayerExclusive, full precision) and the quantized
// copy every other client receives (DT_TFNonLocalPlayerExclusive). Measured on
// bagel at server tick 129211 the two differ by ~4000 units -- the width of the
// map -- so picking "whichever the hash table yields first" is not a rounding
// detail, it is a teleport.
int exclusiveRank(const std::string& name) {
  if (name.find("LocalPlayerExclusive") != std::string::npos) return 0;
  if (name.find("NonLocalPlayerExclusive") != std::string::npos) return 2;
  return 1;
}

// Which of two equally-suffixed properties to use.
//
// Freshness first. Two slots can carry the same quantity while only one of them
// is still being written, and the one a later packet wrote is the one that
// describes the world now. Measured on the POV demo's entity 3 at server tick
// 53976: the Local slot was last written at 51596 and its value sits 2729 units
// from the camera the demo recorded, while the NonLocal slot was written at
// 53976 and sits 44 units from that camera. The rank rule below is exactly what
// prefers the stale one. On bagel the same rule is right -- Local written at
// 129277, NonLocal 73129 ticks stale -- which is why every count-based gate
// stayed green through both.
//
// Rank is the tie-break, not the decision, so a state that carries no packet
// ticks at all (a fixture, or any state built outside readEntityPropUpdates,
// where lastWriteTick is -1) is ordered exactly as it was before this rule
// existed. Equal ticks -- including two -1s -- therefore keep the old
// full-precision-Local-then-lexicographic order, and the choice stays a pure
// function of the state: no hash order, no wall clock.
bool preferCandidate(const std::string& name, std::int32_t lastWriteTick,
                     const std::string& bestName, std::int32_t bestTick) {
  if (lastWriteTick != bestTick) return lastWriteTick > bestTick;
  const int rank = exclusiveRank(name);
  const int bestRank = exclusiveRank(bestName);
  if (rank != bestRank) return rank < bestRank;
  return name < bestName;
}

const EntityPropertyValue* findProperty(const EntityState& state, const char* suffix) {
  // EntityState::properties is an unordered_map, so "first match wins" is not a
  // rule -- it is whichever bucket the hash landed in. Rank the candidates
  // instead, so the same state always yields the same property. This matters for
  // every suffix a player carries twice (m_vecOrigin, m_angEyeAngles[i], ...).
  const EntityPropertyValue* best = nullptr;
  std::string bestName;
  for (const auto& [name, value] : state.properties) {
    if (!suffixMatch(name, suffix)) continue;
    if (!best || preferCandidate(name, value.lastWriteTick, bestName, best->lastWriteTick)) {
      best = &value;
      bestName = name;
    }
  }
  return best;
}

// Reads a vector property by base name, filling in components that the vector's
// own encoding does not carry from the sibling scalar properties "<base>[i]".
//
// This is the P1 z defect: a player's m_vecOrigin is a VectorXY, so the wire
// format carries x and y only and the decoder leaves z at 0. The real z travels
// in a *separate* Float named "m_vecOrigin[2]" whose value lands in .x. Reading
// only the VectorXY's z therefore put every player on the ground plane at z = 0
// while every count-based check stayed green.
//
// The choice among duplicates is made deterministic (prefer the slot written at
// the later tick, then the full-precision Local variant, then the
// lexicographically smallest name) because EntityState::properties is an
// unordered_map and its iteration order is not specified.
bool readVectorProperty(const EntityState& state, const std::string& base, float out[3],
                        bool* complete) {
  if (complete) *complete = true;
  const EntityPropertyValue* best = nullptr;
  std::string bestName;
  for (const auto& [name, value] : state.properties) {
    if (!suffixMatch(name, base.c_str())) continue;
    if (value.type != SendPropType::Vector && value.type != SendPropType::VectorXY) continue;
    if (!best || preferCandidate(name, value.lastWriteTick, bestName, best->lastWriteTick)) {
      best = &value;
      bestName = name;
    }
  }
  if (!best) return false;
  const bool carriesZ = best->type == SendPropType::Vector;
  out[0] = best->x;
  out[1] = best->y;
  out[2] = carriesZ ? best->z : 0.0f;
  for (std::size_t component = carriesZ ? 3u : 2u; component < 3u; ++component) {
    const std::string scalar = base + "[" + std::to_string(component) + "]";
    const auto* value = findProperty(state, scalar.c_str());
    if (!value) {
      // The vector really is 2D and its z sibling is absent from this snapshot.
      // Zero is a guess, so say so rather than letting it pass as a reading.
      if (complete) *complete = false;
      continue;
    }
    out[component] = value->type == SendPropType::Int ? static_cast<float>(value->intValue) : value->x;
  }
  return std::isfinite(out[0]) && std::isfinite(out[1]) && std::isfinite(out[2]);
}

bool isPlayerClassName(const std::string& className) {
  return className.find("TFPlayer") != std::string::npos || className.find("Player") != std::string::npos;
}

bool isViewModelPath(const std::string& path) {
  const auto slash = path.find_last_of("/\\");
  const auto file = slash == std::string::npos ? path : path.substr(slash + 1);
  return file.size() > 6 && file.rfind("v_", 0) == 0 && file.size() >= 4
    && file.compare(file.size() - 4, 4, ".mdl") == 0;
}

// Source's CBaseHandle layout (source-sdk-2013, public/basehandle.h):
// NUM_ENT_ENTRY_BITS = 11, so the entity index is the low 11 bits and 2047 is
// the INVALID_EHANDLE_INDEX every field is initialized to. The serial number
// occupies the next 10 bits; see the header for why it is decoded but not
// validated here.
constexpr std::uint32_t kEntityHandleIndexBits = 11;
constexpr std::uint32_t kEntityHandleIndexMask = (1u << kEntityHandleIndexBits) - 1u;
constexpr std::uint32_t kInvalidEntityHandle = 0xFFFFFFFFu;
constexpr std::uint32_t kInvalidEntityIndex = kEntityHandleIndexMask;

// TF2 observer modes (source-sdk-2013, game/shared/observe_mode.h, shared by the
// server's CBasePlayer::m_iObserverMode and the client's camera logic):
//   0 none, 1 deathcam, 2 freezecam, 3 fixed, 4 in-eye, 5 chase, 6 roaming.
// Only in-eye and chase hang the camera on the target; the others move it by
// their own rules (deathcam/freezecam are scripted around the killer, fixed is
// the spectating player's own location, roaming is free movement).
constexpr std::int64_t kObserverModeInEye = 4;
constexpr std::int64_t kObserverModeChase = 5;

// A ViewModel class is identified by NAME. See the header for why a pinned class
// id would be the wrong test.
bool isViewModelClassName(const std::string& className) {
  return className.find("ViewModel") != std::string::npos;
}

// The same leaf-suffix matcher the transform path uses, for the slots a ViewModel
// entity's animation state lives in. `found` is what separates "the slot is
// absent" from "the slot holds zero" -- for m_nSequence those are different
// states, and only one of them means the model should stand still.
std::int64_t readIntSlot(const EntityState& state, const char* suffix, bool& found) {
  const auto* value = findProperty(state, suffix);
  found = value != nullptr;
  if (value == nullptr) return 0;
  return value->type == SendPropType::Float
    ? static_cast<std::int64_t>(value->x) : value->intValue;
}

double readFloatSlot(const EntityState& state, const char* suffix, bool& found) {
  const auto* value = findProperty(state, suffix);
  found = value != nullptr;
  if (value == nullptr) return 0.0;
  return value->type == SendPropType::Float
    ? static_cast<double>(value->x) : static_cast<double>(value->intValue);
}

} // namespace

const ResolverStats& lastResolverStats() { return g_resolverStats; }
void resetResolverStats() { g_resolverStats = ResolverStats{}; }

std::vector<PropertyCandidate> rankPropertyCandidates(const EntityState& state, const char* suffix) {
  // Same matcher and same ranking the selection rule uses, so what a diagnostic
  // prints is what the rule would do -- not a second opinion that can drift from
  // it. `properties` is an unordered_map, so the order has to be imposed here.
  std::vector<PropertyCandidate> candidates;
  for (const auto& [name, value] : state.properties) {
    if (!suffixMatch(name, suffix)) continue;
    candidates.push_back(PropertyCandidate{name, value.lastWriteTick});
  }
  std::sort(candidates.begin(), candidates.end(),
            [](const PropertyCandidate& left, const PropertyCandidate& right) {
              return preferCandidate(left.name, left.lastWriteTick, right.name, right.lastWriteTick);
            });
  return candidates;
}

ModelInstanceTransform EntityModelResolver::extractTransform(const EntityState& state) {
  ++g_resolverStats.transformsBuilt;
  ModelInstanceTransform transform;
  bool originComplete = true;
  if (float origin[3]; readVectorProperty(state, "m_vecOrigin", origin, &originComplete)) {
    transform.hasOrigin = true;
    transform.origin[0] = origin[0];
    transform.origin[1] = origin[1];
    transform.origin[2] = origin[2];
    if (!originComplete) transform.diagnostic = "origin-z-sibling-missing";
  }
  if (const auto* angles = findProperty(state, "m_angEyeAngles")) {
    if ((angles->type == SendPropType::Vector || angles->type == SendPropType::VectorXY)
        && std::isfinite(angles->x) && std::isfinite(angles->y) && std::isfinite(angles->z)) {
      transform.hasAngles = true;
      transform.angles[0] = angles->x;
      transform.angles[1] = angles->y;
      transform.angles[2] = angles->z;
    }
  }
  if (!transform.hasAngles) {
    const auto* pitch = findProperty(state, "m_angEyeAngles[0]");
    const auto* yaw = findProperty(state, "m_angEyeAngles[1]");
    if (pitch && yaw && std::isfinite(pitch->x) && std::isfinite(yaw->x)) {
      transform.hasAngles = true;
      transform.angles[0] = pitch->type == SendPropType::Float || pitch->type == SendPropType::Int ? (pitch->type == SendPropType::Int ? static_cast<float>(pitch->intValue) : pitch->x) : pitch->x;
      transform.angles[1] = yaw->type == SendPropType::Int ? static_cast<float>(yaw->intValue) : yaw->x;
      transform.angles[2] = 0.0f;
    }
  }
  if (!transform.hasAngles) {
    if (const auto* rotation = findProperty(state, "m_angRotation")) {
      if ((rotation->type == SendPropType::Vector || rotation->type == SendPropType::VectorXY)
          && std::isfinite(rotation->x) && std::isfinite(rotation->y) && std::isfinite(rotation->z)) {
        transform.hasAngles = true;
        transform.angles[0] = rotation->x;
        transform.angles[1] = rotation->y;
        transform.angles[2] = rotation->z;
      }
    }
  }
  if (const auto* team = findProperty(state, "m_iTeamNum")) {
    if (team->type == SendPropType::Int) {
      transform.hasTeam = true;
      transform.team = team->intValue;
    }
  }
  if (const auto* skin = findProperty(state, "m_nSkin")) {
    if (skin->type == SendPropType::Int) {
      transform.hasSkin = true;
      transform.skin = skin->intValue;
    }
  }
  if (const auto* playerClass = findProperty(state, "m_iClass")) {
    if (playerClass->type == SendPropType::Int && playerClass->intValue >= 1 && playerClass->intValue <= 9) {
      transform.hasPlayerClass = true;
      transform.playerClass = playerClass->intValue;
    }
  }
  if (!transform.hasOrigin) transform.diagnostic = "origin missing";
  else if (!transform.hasAngles) transform.diagnostic = "angles missing";
  return transform;
}

ObserverFocusResolution resolveObserverFocus(
    const EntityState& viewEntity,
    const std::vector<EntityState>& statesByIndex) {
  ObserverFocusResolution result;
  if (const auto* mode = findProperty(viewEntity, "m_iObserverMode")) {
    result.hasMode = true;
    result.mode = mode->intValue;
  }
  if (const auto* target = findProperty(viewEntity, "m_hObserverTarget")) {
    result.hasTarget = true;
    const auto handle = static_cast<std::uint32_t>(target->intValue);
    result.targetHandle = handle;
    result.targetIndex = static_cast<std::uint16_t>(handle & kEntityHandleIndexMask);
    result.targetSerial = static_cast<std::uint16_t>(handle >> kEntityHandleIndexBits);
    // Three ways a handle is not a place to look: the wire sentinel 0xFFFFFFFF,
    // the 2047 index that field initialization leaves behind, and an index past
    // the snapshot. Each is checked by name so a failure says which one fired.
    const bool sentinel = handle == kInvalidEntityHandle;
    const bool invalidIndex = result.targetIndex == kInvalidEntityIndex;
    result.targetInRange = !sentinel && !invalidIndex
      && result.targetIndex < statesByIndex.size();
    if (result.targetInRange) {
      result.targetPresent = statesByIndex[result.targetIndex].classId >= 0;
    }
    result.followsTarget = result.hasMode && result.targetPresent
      && (result.mode == kObserverModeInEye || result.mode == kObserverModeChase);
  }
  return result;
}

ViewModelEntityLookup findViewModelAnimationInput(
    const std::vector<EntityState>& statesByIndex,
    const std::vector<ServerClassSchema>& classSchemas,
    bool viewEntityKnown,
    std::uint32_t viewEntity,
    const std::unordered_map<std::uint16_t, std::string>& modelPrecache) {
  ViewModelEntityLookup lookup;
  lookup.viewEntityKnown = viewEntityKnown;
  lookup.viewEntity = viewEntity;
  if (!viewEntityKnown) {
    // Without svc_SetView there is no principled way to choose between the
    // snapshot's ViewModel entities, so nothing is chosen and the caller is told
    // which of the two failures this is rather than handed an arbitrary one.
    lookup.reason = "svc_SetView did not name a view entity";
    return lookup;
  }
  std::vector<std::int32_t> viewModelClassIds;
  for (const auto& schema : classSchemas) {
    if (isViewModelClassName(schema.name)) {
      viewModelClassIds.push_back(static_cast<std::int32_t>(schema.id));
    }
  }
  if (viewModelClassIds.empty()) {
    lookup.reason = "the server class table names no ViewModel class";
    return lookup;
  }
  // The view entity's own weapon handle, used below to choose between the two
  // ViewModel entities a player owns. m_hActiveWeapon is the live one; m_hWeapon
  // is the fallback slot the same value arrives in on some recordings.
  std::int64_t viewWeapon = -1;
  bool viewHasWeapon = false;
  if (viewEntity < statesByIndex.size()) {
    bool hasActive = false;
    viewWeapon = readIntSlot(statesByIndex[viewEntity], "m_hActiveWeapon", hasActive);
    viewHasWeapon = hasActive;
    if (!hasActive) {
      bool hasWeapon = false;
      viewWeapon = readIntSlot(statesByIndex[viewEntity], "m_hWeapon", hasWeapon);
      viewHasWeapon = hasWeapon;
    }
  }
  int bestIndex = -1;
  std::int64_t bestViewModelIndex = 0;
  bool bestWeaponMatches = false;
  bool bestHasModel = false;
  std::string bestModelPath;
  for (std::size_t index = 0; index < statesByIndex.size(); ++index) {
    const auto& state = statesByIndex[index];
    if (state.classId < 0) continue;
    if (std::find(viewModelClassIds.begin(), viewModelClassIds.end(), state.classId)
        == viewModelClassIds.end()) {
      continue;
    }
    ++lookup.viewModelEntitiesSeen;
    bool hasOwner = false;
    const std::int64_t owner = readIntSlot(state, "m_hOwner", hasOwner);
    // The same handle layout resolveObserverFocus uses: the entity index is the
    // low 11 bits. The serial above it is not validated, because EntityState
    // carries no serial -- a handle whose slot was reused still resolves to that
    // slot, and that limit is stated rather than worked around.
    if (!hasOwner || owner <= 0) continue;
    if ((static_cast<std::uint32_t>(owner) & kEntityHandleIndexMask) != viewEntity) continue;
    ++lookup.candidatesOwnedByViewEntity;
    bool hasWeapon = false;
    const std::int64_t weapon = readIntSlot(state, "m_hWeapon", hasWeapon);
    bool hasViewModelIndex = false;
    const std::int64_t viewModelIndex = readIntSlot(state, "m_nViewModelIndex", hasViewModelIndex);
    bool hasModelIndex = false;
    const std::int64_t modelIndex = readIntSlot(state, "m_nModelIndex", hasModelIndex);
    std::string entityModelPath;
    if (hasModelIndex && modelIndex >= 0 && modelIndex <= 0xffff) {
      const auto found = modelPrecache.find(static_cast<std::uint16_t>(modelIndex));
      if (found != modelPrecache.end()) entityModelPath = found->second;
    }
    const bool weaponMatches = viewHasWeapon && hasWeapon && weapon == viewWeapon;
    // A resolvable model outranks everything, and that ordering is a fix rather
    // than a preference. Measured on the POV demo: a player with no active weapon
    // carries the sentinel handle 2097151 in m_hWeapon, the EMPTY ViewModel slot
    // carries the same sentinel, and a weapon-first rule therefore selected the
    // empty slot -- whose m_nModelIndex is 0 and whose m_nSequence is a
    // trivially-in-range 0. That read as `ok` 857 times in one run while nothing
    // was actually held. An entity with no model is not the thing on screen.
    const bool hasModel = !entityModelPath.empty();
    // Then the entity holding the player's active weapon, then the lowest
    // m_nViewModelIndex (0 when the slot is absent). All three are stated because
    // choosing between two real candidates is a decision, and `reason` below
    // records which rule decided.
    const bool better = bestIndex < 0
      || (hasModel && !bestHasModel)
      || (hasModel == bestHasModel && weaponMatches && !bestWeaponMatches)
      || (hasModel == bestHasModel && weaponMatches == bestWeaponMatches
          && viewModelIndex < bestViewModelIndex);
    if (!better) continue;
    bestIndex = static_cast<int>(index);
    bestViewModelIndex = viewModelIndex;
    bestWeaponMatches = weaponMatches;
    bestHasModel = hasModel;
    bestModelPath = entityModelPath;
  }
  if (bestIndex < 0) {
    lookup.reason = lookup.viewModelEntitiesSeen == 0
      ? "the snapshot holds no ViewModel entity"
      : "no ViewModel entity names the view entity in m_hOwner";
    return lookup;
  }
  const auto& state = statesByIndex[static_cast<std::size_t>(bestIndex)];
  lookup.input.entityPresent = true;
  lookup.input.entityIndex = bestIndex;
  bool hasSequence = false;
  const std::int64_t sequence = readIntSlot(state, "m_nSequence", hasSequence);
  lookup.input.hasSequence = hasSequence;
  lookup.input.sequence = hasSequence ? static_cast<int>(sequence) : -1;
  bool hasRate = false;
  const double rate = readFloatSlot(state, "m_flPlaybackRate", hasRate);
  lookup.input.hasPlaybackRate = hasRate;
  lookup.input.playbackRate = hasRate ? static_cast<float>(rate) : 0.0f;
  // Read anyway and report the absence, rather than assuming it: DT_BaseViewModel
  // has no m_flCycle today, and if a protocol revision adds one this line starts
  // reporting it with no other change.
  bool hasCycle = false;
  const double cycle = readFloatSlot(state, "m_flCycle", hasCycle);
  lookup.input.hasCycle = hasCycle;
  lookup.input.cycle = hasCycle ? static_cast<float>(cycle) : 0.0f;
  lookup.input.entityModelPath = bestModelPath;
  if (bestHasModel && bestWeaponMatches) {
    lookup.reason = "the viewmodel entity with a model that holds the view entity's active weapon";
  } else if (bestHasModel) {
    lookup.reason = "the viewmodel entity with a resolvable model and the lowest m_nViewModelIndex";
  } else if (bestWeaponMatches) {
    lookup.reason = "the viewmodel entity holding the view entity's active weapon; no candidate has a resolvable model";
  } else {
    lookup.reason = "the lowest m_nViewModelIndex among the entities the view entity owns; no candidate has a resolvable model";
  }
  return lookup;
}

const char* viewModelWeaponBodyRefusalName(ViewModelWeaponBodyRefusal refusal) {
  switch (refusal) {
    case ViewModelWeaponBodyRefusal::None: return "none";
    case ViewModelWeaponBodyRefusal::NoViewModelEntity: return "noViewModelEntity";
    case ViewModelWeaponBodyRefusal::ViewModelEntityAbsent: return "viewModelEntityAbsent";
    case ViewModelWeaponBodyRefusal::NoWeaponSlot: return "noWeaponSlot";
    case ViewModelWeaponBodyRefusal::EmptyWeaponHandle: return "emptyWeaponHandle";
    case ViewModelWeaponBodyRefusal::WeaponPastSnapshot: return "weaponPastSnapshot";
    case ViewModelWeaponBodyRefusal::WeaponAbsent: return "weaponAbsent";
    case ViewModelWeaponBodyRefusal::NoWorldModelSlot: return "noWorldModelSlot";
    case ViewModelWeaponBodyRefusal::WorldModelIndexZero: return "worldModelIndexZero";
    case ViewModelWeaponBodyRefusal::WorldModelIndexOutOfRange: return "worldModelIndexOutOfRange";
    case ViewModelWeaponBodyRefusal::WorldModelNotDeclared: return "worldModelNotDeclared";
    case ViewModelWeaponBodyRefusal::WorldModelIsArms: return "worldModelIsArms";
  }
  return "unknown";
}

ViewModelWeaponBodyLookup findViewModelWeaponBodyInput(
    const std::vector<EntityState>& statesByIndex,
    const std::vector<ServerClassSchema>& classSchemas,
    int viewModelEntityIndex,
    const std::string& armModelPath,
    const std::unordered_map<std::uint16_t, std::string>& modelPrecache) {
  ViewModelWeaponBodyLookup lookup;
  if (viewModelEntityIndex < 0
      || static_cast<std::size_t>(viewModelEntityIndex) >= statesByIndex.size()) {
    lookup.refusal = ViewModelWeaponBodyRefusal::NoViewModelEntity;
    lookup.reason = "no ViewModel entity was selected, so no weapon entity can be named";
    return lookup;
  }
  const auto& viewModelState = statesByIndex[static_cast<std::size_t>(viewModelEntityIndex)];
  if (viewModelState.classId < 0) {
    // The selected index exists but holds no entity in this snapshot: the slot
    // was freed between the selection and this read. Naming it separately from
    // "the ViewModel entity has no weapon slot" is the difference between a
    // selection problem and a packet-stream problem.
    lookup.refusal = ViewModelWeaponBodyRefusal::ViewModelEntityAbsent;
    lookup.reason = "the selected ViewModel entity is not present in this snapshot";
    return lookup;
  }
  lookup.hasViewModelEntity = true;
  lookup.viewModelEntityIndex = viewModelEntityIndex;
  bool hasWeapon = false;
  const std::int64_t weaponHandle = readIntSlot(viewModelState, "m_hWeapon", hasWeapon);
  lookup.hasWeaponHandle = hasWeapon;
  if (!hasWeapon) {
    lookup.refusal = ViewModelWeaponBodyRefusal::NoWeaponSlot;
    lookup.reason = "the ViewModel entity carries no m_hWeapon slot";
    return lookup;
  }
  lookup.weaponHandle = weaponHandle;
  // The same three ways a handle is not a place to look that resolveObserverFocus
  // names: the wire sentinel 0xFFFFFFFF, the 2047 index that field
  // initialization leaves behind (the empty-slot ViewModel entities carry
  // exactly this), and an index past the snapshot.
  const std::uint32_t handle = static_cast<std::uint32_t>(weaponHandle);
  const bool sentinel = handle == kInvalidEntityHandle;
  const std::uint32_t targetIndex = handle & kEntityHandleIndexMask;
  const bool invalidIndex = targetIndex == kInvalidEntityIndex;
  if (sentinel || invalidIndex) {
    lookup.refusal = ViewModelWeaponBodyRefusal::EmptyWeaponHandle;
    lookup.reason = "the ViewModel entity's m_hWeapon is the empty handle sentinel";
    return lookup;
  }
  const int weaponIndex = static_cast<int>(targetIndex);
  lookup.weaponEntityIndex = weaponIndex;
  if (static_cast<std::size_t>(weaponIndex) >= statesByIndex.size()) {
    lookup.refusal = ViewModelWeaponBodyRefusal::WeaponPastSnapshot;
    lookup.reason = "weapon entity " + std::to_string(weaponIndex) + " is past the snapshot";
    return lookup;
  }
  const auto& weaponState = statesByIndex[static_cast<std::size_t>(weaponIndex)];
  if (weaponState.classId < 0) {
    // `m_hWeapon` names a slot the snapshot does not hold yet -- the weapon
    // entity is temporarily absent, which is a different state from "the handle
    // is empty" and repairs differently.
    lookup.refusal = ViewModelWeaponBodyRefusal::WeaponAbsent;
    lookup.reason = "weapon entity " + std::to_string(weaponIndex) + " is not in this snapshot";
    return lookup;
  }
  lookup.weaponEntityPresent = true;
  lookup.weaponClassId = weaponState.classId;
  for (const auto& schema : classSchemas) {
    if (schema.id == static_cast<std::uint32_t>(weaponState.classId)) {
      lookup.weaponClassName = schema.name;
      break;
    }
  }
  bool hasWorldModelIndex = false;
  const std::int64_t worldModelIndex =
    readIntSlot(weaponState, "m_iWorldModelIndex", hasWorldModelIndex);
  lookup.hasWorldModelIndex = hasWorldModelIndex;
  if (!hasWorldModelIndex) {
    lookup.refusal = ViewModelWeaponBodyRefusal::NoWorldModelSlot;
    lookup.reason = "weapon entity " + std::to_string(weaponIndex)
      + " carries no m_iWorldModelIndex slot";
    return lookup;
  }
  lookup.worldModelIndex = worldModelIndex;
  if (worldModelIndex == 0) {
    // Source's "no model" sentinel. Modelprecache does not declare index 0, so
    // this is separately reportable from "the index named nothing": only one of
    // the two is repairable by a different precache.
    lookup.refusal = ViewModelWeaponBodyRefusal::WorldModelIndexZero;
    lookup.reason = "the weapon's m_iWorldModelIndex is 0";
    return lookup;
  }
  if (worldModelIndex < 1 || worldModelIndex > 0xffff) {
    lookup.refusal = ViewModelWeaponBodyRefusal::WorldModelIndexOutOfRange;
    lookup.reason = "the weapon's m_iWorldModelIndex is out of range";
    return lookup;
  }
  const auto found = modelPrecache.find(static_cast<std::uint16_t>(worldModelIndex));
  if (found == modelPrecache.end() || found->second.empty()) {
    // In range but the table never declared it: the weapon names a model the
    // demo does not ship, so no draw request can be built for it.
    lookup.refusal = ViewModelWeaponBodyRefusal::WorldModelNotDeclared;
    lookup.reason = "m_iWorldModelIndex " + std::to_string(worldModelIndex)
      + " is not declared in modelprecache";
    return lookup;
  }
  // The defect signature this refusal exists for. A weapon carries two indices
  // next to each other in the send table, and the WRONG one -- m_nModelIndex,
  // which names the class's first-person arms composite -- still resolves to a
  // real, drawable file. Drawing it would put a second pair of hands in front
  // of the camera and every count-based reading would stay green, which is
  // exactly what the world pass did before cd36db1. Comparing the resolved path
  // against the arms the pass is drawing is what makes that case refuse here
  // instead of rendering.
  //
  // Both sides go through normalizeModelPath. The two paths come from different
  // places -- this one from modelprecache, the other from the ViewModel entity's
  // m_nModelIndex through the same table but possibly written with the other
  // separator or a different case -- and a raw comparison would miss the match
  // and draw the arms a second time while reporting a weapon body. Measured:
  // before this used the shared rule, the probe's backslash case failed.
  if (!armModelPath.empty()
      && normalizeModelPath(found->second) == normalizeModelPath(armModelPath)) {
    lookup.refusal = ViewModelWeaponBodyRefusal::WorldModelIsArms;
    const std::string refused = found->second;
    lookup.reason = "the weapon's world model resolves to the arms composite ("
      + refused + "), not to a weapon body";
    return lookup;
  }
  // Assigned last, so `worldModelPath` is non-empty exactly when this lookup
  // succeeded -- including the refusal above, which named a real file.
  lookup.worldModelPath = found->second;
  lookup.refusal = ViewModelWeaponBodyRefusal::None;
  lookup.reason = "the weapon entity's own m_iWorldModelIndex names the weapon body";
  return lookup;
}

std::string EntityModelResolver::defaultPlayerModelPath(std::int64_t tfClass) {
  switch (tfClass) {
    case 1: return "models/player/scout.mdl";
    case 2: return "models/player/sniper.mdl";
    case 3: return "models/player/soldier.mdl";
    case 4: return "models/player/demo.mdl";
    case 5: return "models/player/medic.mdl";
    case 6: return "models/player/heavy.mdl";
    case 7: return "models/player/pyro.mdl";
    case 8: return "models/player/spy.mdl";
    case 9: return "models/player/engineer.mdl";
    default: return {};
  }
}

std::vector<ModelInstance> EntityModelResolver::buildInstances(
    const std::vector<ModelRenderRequest>& requests,
    const std::vector<EntityState>& statesByIndex,
    const std::vector<ServerClassSchema>& classSchemas,
    std::size_t maxInstances) {
  if (maxInstances == 0 || maxInstances > kMaxInstances) maxInstances = kMaxInstances;
  g_resolverStats = ResolverStats{};
  g_resolverStats.instanceRequests = requests.size();
  std::vector<ModelInstance> instances;
  instances.reserve(std::min(requests.size() + 64u, maxInstances));
  std::unordered_set<std::uint16_t> covered;
  covered.reserve(requests.size() + 64u);

  auto classNameOf = [&](std::int32_t classId) -> std::string {
    if (classId < 0) return {};
    const auto index = static_cast<std::size_t>(classId);
    if (index >= classSchemas.size()) return {};
    return classSchemas[index].name;
  };

  for (const auto& request : requests) {
    if (instances.size() >= maxInstances) break;
    ModelInstance instance;
    instance.entityIndex = request.entityIndex;
    instance.classId = request.classId;
    instance.className = request.className.empty() ? classNameOf(request.classId) : request.className;
    instance.modelPath = request.modelPath;
    instance.cacheKey = request.cacheKey;
    instance.checksum = request.checksum;
    instance.resolution = request.resolution;
    instance.companionSetComplete = request.companionSetComplete;
    instance.renderable = request.renderable;
    instance.missingAssetFallback = !request.renderable;
    instance.viewModelSkipped = isViewModelPath(request.modelPath);
    instance.worldModelIndexPath = request.modelPathFromWorldModelIndex;
    instance.diagnostic = request.diagnostic;
    if (instance.viewModelSkipped) {
      instance.renderable = false;
      instance.diagnostic += "; world pass skips viewmodel";
    }
    if (request.entityIndex < statesByIndex.size()) {
      instance.transform = extractTransform(statesByIndex[request.entityIndex]);
    } else {
      instance.transform.diagnostic = "entity index outside snapshot";
    }
    covered.insert(request.entityIndex);
    instances.push_back(std::move(instance));
  }

  for (std::size_t entityIndex = 0; entityIndex < statesByIndex.size() && instances.size() < maxInstances; ++entityIndex) {
    if (entityIndex > 0xffffu) break;
    const auto index16 = static_cast<std::uint16_t>(entityIndex);
    if (covered.count(index16)) continue;
    ++g_resolverStats.fallbackEntitiesScanned;
    const auto& state = statesByIndex[entityIndex];
    const auto className = classNameOf(state.classId);
    if (!isPlayerClassName(className)) continue;
    // One lookup answers "is this a class we can draw a fallback for". Only an
    // entity that passes pays for the full transform. The check is the same
    // suffix and the same value range extractTransform applies to m_iClass, so
    // the set of entities that get an instance is unchanged -- and that is what
    // the probe's instance counts are there to confirm.
    const auto* playerClass = findProperty(state, "m_iClass");
    if (!playerClass || playerClass->type != SendPropType::Int
        || playerClass->intValue < 1 || playerClass->intValue > 9) continue;
    ++g_resolverStats.classLookups;
    const auto transform = extractTransform(state);
    if (!transform.hasPlayerClass) continue;
    const auto path = defaultPlayerModelPath(transform.playerClass);
    if (path.empty()) continue;
    ModelInstance instance;
    instance.entityIndex = index16;
    instance.classId = state.classId;
    instance.className = className;
    instance.modelPath = path;
    instance.cacheKey = path + "#class-fallback";
    instance.transform = transform;
    instance.resolution = ModelAssetResolution::Unknown;
    instance.playerClassFallback = true;
    instance.missingAssetFallback = true;
    instance.diagnostic = "player class fallback model path";
    instances.push_back(std::move(instance));
  }
  return instances;
}

bool buildEntityModelInstanceRows(
    const float origin[3],
    const float angles[3],
    bool hasAngles,
    const EntityModelWorldMap& world,
    EntityModelInstanceRows& rows) {
  if (!origin || !std::isfinite(origin[0]) || !std::isfinite(origin[1]) || !std::isfinite(origin[2])) return false;
  if (!std::isfinite(world.centerX) || !std::isfinite(world.centerY) || !std::isfinite(world.minZ)
      || !std::isfinite(world.horizontalScale) || !std::isfinite(world.zScale)
      || !(world.horizontalScale > 0.0f) || !(world.zScale > 0.0f)) return false;
  float m00 = 1.0f, m01 = 0.0f, m02 = 0.0f;
  float m10 = 0.0f, m11 = 1.0f, m12 = 0.0f;
  float m20 = 0.0f, m21 = 0.0f, m22 = 1.0f;
  if (hasAngles) {
    if (!angles || !std::isfinite(angles[0]) || !std::isfinite(angles[1]) || !std::isfinite(angles[2])) return false;
    const float deg = 0.01745329252f;
    const float sp = std::sin(angles[0] * deg), cp = std::cos(angles[0] * deg);
    const float sy = std::sin(angles[1] * deg), cy = std::cos(angles[1] * deg);
    const float sr = std::sin(angles[2] * deg), cr = std::cos(angles[2] * deg);
    m00 = cp * cy;
    m10 = cp * sy;
    m20 = -sp;
    m01 = sr * sp * cy + cr * -sy;
    m11 = sr * sp * sy + cr * cy;
    m21 = sr * cp;
    m02 = cr * sp * cy + sr * sy;
    m12 = cr * sp * sy + -sr * cy;
    m22 = cr * cp;
  }
  const float horizontal = world.horizontalScale;
  const float zScale = world.zScale;
  rows.positionRows[0] = horizontal * m00;
  rows.positionRows[1] = horizontal * m01;
  rows.positionRows[2] = horizontal * m02;
  rows.positionRows[3] = (origin[0] - world.centerX) * horizontal;
  rows.positionRows[4] = horizontal * m10;
  rows.positionRows[5] = horizontal * m11;
  rows.positionRows[6] = horizontal * m12;
  rows.positionRows[7] = (origin[1] - world.centerY) * horizontal;
  rows.positionRows[8] = zScale * m20;
  rows.positionRows[9] = zScale * m21;
  rows.positionRows[10] = zScale * m22;
  rows.positionRows[11] = (origin[2] - world.minZ) * zScale + 0.1f;
  rows.normalRows[0] = m00; rows.normalRows[1] = m01; rows.normalRows[2] = m02;
  rows.normalRows[3] = m10; rows.normalRows[4] = m11; rows.normalRows[5] = m12;
  rows.normalRows[6] = m20; rows.normalRows[7] = m21; rows.normalRows[8] = m22;
  return std::isfinite(rows.positionRows[3]) && std::isfinite(rows.positionRows[7]) && std::isfinite(rows.positionRows[11]);
}

} // namespace tf2::native
