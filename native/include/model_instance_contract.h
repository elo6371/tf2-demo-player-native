#pragma once

#include <array>
#include <cmath>
#include <cstdint>

namespace tf2::native {

struct ModelMeshDataContract {
  std::uint64_t meshId = 0;
  std::uint32_t vertexCount = 0;
  std::uint32_t indexCount = 0;
};

struct ModelInstanceTransformContract {
  std::uint16_t entityIndex = 0;
  const ModelMeshDataContract* mesh = nullptr;
  std::array<float, 3> origin{};
  std::array<float, 3> angles{};
};

inline bool validModelInstanceTransform(const ModelInstanceTransformContract& instance) {
  for (const float value : instance.origin) if (!std::isfinite(value)) return false;
  for (const float value : instance.angles) if (!std::isfinite(value)) return false;
  return instance.mesh != nullptr && instance.mesh->meshId != 0
    && instance.mesh->vertexCount > 0 && instance.mesh->indexCount > 0;
}

} // namespace tf2::native
