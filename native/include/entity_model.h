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
// camera onto the target, and that is wrong on the first demo measured: the POV
// recorder declares in-eye on entity 3 while the camera dem_cmdinfo recorded sits
// on the recorder's own eye (pitch byte-identical, position 0.012 units off in x),
// 2729 units from the coordinate this pipeline's slot rule resolves for entity 3
// -- a slot 2379 ticks stale by its own m_nTickBase. So the resolution is reported
// and asserted, and the renderer does NOT follow it yet. observer-focus-check.sh
// pins that distance so the day the slot rule changes, someone is told.
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
