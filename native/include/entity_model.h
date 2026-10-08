#pragma once

#include "demo_header.h"
#include "model_loader.h"

#include <cstddef>
#include <cstdint>
#include <string>
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
