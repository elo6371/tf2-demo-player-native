#pragma once

#include "demo_header.h"
#include "model_loader.h"
#include "viewmodel.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace tf2::native {

struct ModelInstanceTransform {
  bool hasOrigin = false;
  float origin[3] = {};
  bool hasAngles = false;
  float angles[3] = {};
  bool hasTeam = false;
  std::int64_t team = 0;
  bool hasSkin = false;
  std::int64_t skin = 0;
  bool hasPlayerClass = false;
  std::int64_t playerClass = 0;
  std::string diagnostic;
};

struct ModelInstance {
  std::uint16_t entityIndex = 0;
  std::int32_t classId = -1;
  std::string className;
  std::string modelPath;
  std::string cacheKey;
  std::uint32_t checksum = 0;
  ModelInstanceTransform transform;
  ModelAssetResolution resolution = ModelAssetResolution::Unknown;
  bool companionSetComplete = false;
  bool renderable = false;
  bool missingAssetFallback = false;
  bool playerClassFallback = false;
  bool viewModelSkipped = false;
  // This instance's path came from the entity's m_iWorldModelIndex -- the weapon
  // itself -- rather than from m_nModelIndex, which on a weapon names its class's
  // first-person arms composite.
  bool worldModelIndexPath = false;
  std::string diagnostic;
};

// Work counters for the resolver, so "how much does property lookup cost" can be
// answered from a reading instead of from an estimate. P1 asked for exactly this
// before any change: `extractTransform` scans the whole property map once per
// requested suffix, and the class-fallback sweep calls it for every entity in the
// snapshot, so the numbers worth having are how many property comparisons the
// transform path performs and how many entities it walks to find the players.
//
// These are plain counters set by the most recent buildInstances call, not
// thread-local: the resolver is called from the render loop and the probes, and
// neither runs it concurrently.
struct ResolverStats {
  std::size_t instanceRequests = 0;      // requests handed to buildInstances
  std::size_t fallbackEntitiesScanned = 0; // entities walked in the fallback sweep
  std::size_t transformsBuilt = 0;       // extractTransform calls
  std::size_t propertyComparisons = 0;   // name-vs-suffix comparisons performed
  std::size_t classLookups = 0;          // cheap pre-checks that avoided a transform
};

const ResolverStats& lastResolverStats();
void resetResolverStats();

class EntityModelResolver final {
public:
  static constexpr std::size_t kMaxInstances = 2048;

  static ModelInstanceTransform extractTransform(const EntityState& state);
  static std::string defaultPlayerModelPath(std::int64_t tfClass);
  static std::vector<ModelInstance> buildInstances(
    const std::vector<ModelRenderRequest>& requests,
    const std::vector<EntityState>& statesByIndex,
    const std::vector<ServerClassSchema>& classSchemas = {},
    std::size_t maxInstances = kMaxInstances);
};

// How a demo player's observer target resolves against one snapshot. A player
// whose entity owns the camera ("the view entity") can be watching somebody
// else: TF2 writes m_iObserverMode / m_hObserverTarget while a player is dead
// or spectating, and Source's observe_mode.h has only in-eye (4) and chase (5)
// hands the camera to the target -- 0 none, 1 deathcam, 2 freezecam, 3 fixed and
// 6 roaming all keep it elsewhere. Until this round nothing read either property.
//
// WHAT THIS DOES NOT LICENSE. It would be easy to read followsTarget and move the
// camera onto the target. When this round first measured the POV demo, doing so
// would have moved the view 2729 units off where dem_cmdinfo recorded it, because
// the coordinate the slot rule resolved for entity 3 was its
// DT_TFLocalPlayerExclusive copy -- last written at 51596, 2379 ticks behind the
// checkpoint it was answered from. The freshness round (2026-10-08) made the slot
// rule prefer the later-written slot, and that distance fell to 45 units, so the
// reason not to wire the follow is now a decision rather than a measurement. The
// resolution is reported and asserted; the renderer still does NOT follow it, and
// observer-focus-check.sh pins the 45 (and the per-axis split inside it) so whoever
// does wire it inherits the measured number rather than a memory of 2729.
//
// HANDLE LAYOUT -- Source's CBaseHandle packs the entity index into the low 11
// bits and a serial number into the next 10 (source-sdk-2013,
// public/basehandle.h: NUM_ENT_ENTRY_BITS 11, ENT_ENTRY_MASK). The serial is
// decoded and reported but NOT validated, because EntityState carries no serial:
// a handle whose slot was reused by a newer entity still resolves to that slot.
// That limit is stated here rather than worked around.
struct ObserverFocusResolution {
  bool hasMode = false;
  std::int64_t mode = 0;
  bool hasTarget = false;
  std::uint32_t targetHandle = 0;
  std::uint16_t targetIndex = 0;
  std::uint16_t targetSerial = 0;
  bool targetInRange = false;
  bool targetPresent = false;
  bool followsTarget = false;
};

ObserverFocusResolution resolveObserverFocus(
  const EntityState& viewEntity,
  const std::vector<EntityState>& statesByIndex);

// Which ViewModel entity belongs to the player the recording's camera is in, and
// what that entity's animation slots say.
//
// THE PAIRING IS READ, NOT GUESSED. `svc_SetView` names the player whose eyes
// the demo is from, and a ViewModel entity carries `m_hOwner`; the entity whose
// owner resolves to that player is the one whose sequence is in front of the
// camera. "The first ViewModel entity in the packet" would be a guess, and on
// the POV demo it would be the wrong one: there are 18 CTFViewModel entities in
// a snapshot and exactly two of them belong to the local player, because
// Source's MAX_VIEWMODELS is 2.
//
// THE CLASS TEST IS BY NAME. A class id is a property of one recording's server
// class table -- CTFViewModel was 320 and CBaseViewModel 18 on the demo measured
// on 2026-10-11 -- and the same name carries a different id on the next demo, so
// a pinned id would be a rule that quietly stops matching. The id is reported by
// the probe for diagnostics and never used as the test.
//
// THE THREE FIELDS ARE ANSWERED SEPARATELY, because their supply differs.
// DT_BaseViewModel carries m_nSequence and m_flPlaybackRate and does NOT carry
// m_flCycle, so `hasSequence` and `hasCycle` are independent answers and one
// "animation state present" flag would hide exactly the finding that matters.
struct ViewModelEntityLookup {
  bool viewEntityKnown = false;
  std::uint32_t viewEntity = 0;
  // How many entities in the snapshot are ViewModel entities at all, and how many
  // of those name the view entity in m_hOwner. Both are work counts: "no entity
  // was found" has to be distinguishable from "nothing was looked at".
  std::size_t viewModelEntitiesSeen = 0;
  std::size_t candidatesOwnedByViewEntity = 0;
  ViewModelAnimationInput input;
  // Which rule chose the entity, or why none was chosen.
  std::string reason;
};

ViewModelEntityLookup findViewModelAnimationInput(
  const std::vector<EntityState>& statesByIndex,
  const std::vector<ServerClassSchema>& classSchemas,
  bool viewEntityKnown,
  std::uint32_t viewEntity,
  const std::unordered_map<std::uint16_t, std::string>& modelPrecache);

// One property that matches a suffix, with the tick it was last written at.
struct PropertyCandidate {
  std::string name;
  std::int32_t lastWriteTick = -1;
};

// Every property whose name matches `suffix`, best first, in the order the
// selection rule itself ranks them. Exposed so a diagnostic prints the rule's own
// ordering rather than re-implementing it: a second implementation would drift
// from the first, and the drift would be invisible exactly when the rule is the
// thing under test. `lastWriteTick` is the server tick the slot was last written
// at (-1 when it did not come from a packet) -- the quantity the rule now ranks by
// first, so rank 0 is the slot findProperty and readVectorProperty would pick.
std::vector<PropertyCandidate> rankPropertyCandidates(
  const EntityState& state, const char* suffix);

struct EntityModelWorldMap {
  float centerX = 0.0f;
  float centerY = 0.0f;
  float minZ = 0.0f;
  float horizontalScale = 1.0f;
  float zScale = 1.0f;
};

struct EntityModelInstanceRows {
  float positionRows[12] = {};
  float normalRows[9] = {};
};

// One Source AngleMatrix plus the world-cube normalize, for dot(row, float4(local, 1)).
// Normal rows are the same rotation without translation or scale.
bool buildEntityModelInstanceRows(
  const float origin[3],
  const float angles[3],
  bool hasAngles,
  const EntityModelWorldMap& world,
  EntityModelInstanceRows& rows);

} // namespace tf2::native
