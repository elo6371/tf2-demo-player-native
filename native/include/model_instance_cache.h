#pragma once

#include "model_loader.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace tf2::native {

enum class ModelInstanceStatus {
  Ready,
  MissingAsset,
  Unrenderable,
  BudgetExceeded,
  InvalidRequest,
};

struct ModelInstanceCacheLimits {
  std::size_t maxInstances = 2048;
  std::size_t maxUniqueMeshes = 256;
  std::size_t maxVerticesPerMesh = 250000;
  std::size_t maxTotalVertices = 1000000;
};

struct ModelInstance {
  std::uint16_t entityIndex = 0;
  std::int32_t classId = -1;
  std::string className;
  std::string modelPath;
  ModelInstanceStatus status = ModelInstanceStatus::InvalidRequest;
  std::shared_ptr<const ModelMeshData> mesh;
  std::string diagnostic;
};

// Renderer-neutral model selection/cache. ModelMeshData is immutable after
// construction and may be shared by all entities referencing the same model.
class ModelInstanceCache final {
public:
  explicit ModelInstanceCache(ModelInstanceCacheLimits limits = {});

  void clear();
  void rebuild(const std::vector<ModelRenderRequest>& requests);

  const std::vector<ModelInstance>& instances() const { return instances_; }
  std::size_t uniqueMeshCount() const { return uniqueMeshCount_; }
  std::size_t totalMeshVertices() const { return totalMeshVertices_; }
  std::size_t droppedInstanceCount() const { return droppedInstanceCount_; }
  const std::vector<std::string>& diagnostics() const { return diagnostics_; }
  const ModelInstanceCacheLimits& limits() const { return limits_; }

private:
  ModelInstanceCacheLimits limits_;
  std::vector<ModelInstance> instances_;
  std::size_t uniqueMeshCount_ = 0;
  std::size_t totalMeshVertices_ = 0;
  std::size_t droppedInstanceCount_ = 0;
  std::vector<std::string> diagnostics_;
};

const char* modelInstanceStatusName(ModelInstanceStatus status);

} // namespace tf2::native
