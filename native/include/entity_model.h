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
