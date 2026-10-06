#include "model_instance_cache.h"

#include <algorithm>
#include <cctype>
#include <unordered_map>

namespace tf2::native {
namespace {

std::string cacheKey(const std::string& path) {
  std::string key = path;
  std::replace(key.begin(), key.end(), '\\', '/');
  while (!key.empty() && key.front() == '/') key.erase(key.begin());
  std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return key;
}

ModelInstanceStatus unavailableStatus(const ModelRenderRequest& request) {
  if (request.resolution == ModelAssetResolution::Missing
      || request.resolution == ModelAssetResolution::Unknown) {
    return ModelInstanceStatus::MissingAsset;
  }
  return ModelInstanceStatus::Unrenderable;
}

} // namespace

ModelInstanceCache::ModelInstanceCache(ModelInstanceCacheLimits limits)
  : limits_(limits) {}

void ModelInstanceCache::clear() {
  instances_.clear();
  uniqueMeshCount_ = 0;
  totalMeshVertices_ = 0;
  droppedInstanceCount_ = 0;
  diagnostics_.clear();
}

void ModelInstanceCache::rebuild(const std::vector<ModelRenderRequest>& requests) {
  clear();
  instances_.reserve(std::min(requests.size(), limits_.maxInstances));
  std::unordered_map<std::string, std::shared_ptr<const ModelMeshData>> meshes;
  meshes.reserve(std::min(requests.size(), limits_.maxUniqueMeshes));

  for (const auto& request : requests) {
    if (instances_.size() >= limits_.maxInstances) {
      ++droppedInstanceCount_;
      continue;
    }
    ModelInstance instance;
    instance.entityIndex = request.entityIndex;
    instance.classId = request.classId;
    instance.className = request.className;
    instance.modelPath = request.modelPath;

    const auto key = cacheKey(request.modelPath);
    if (key.empty()) {
      instance.status = ModelInstanceStatus::InvalidRequest;
      instance.diagnostic = "model path is empty";
      instances_.push_back(std::move(instance));
      continue;
    }
    if (!request.renderable) {
      instance.status = unavailableStatus(request);
      instance.diagnostic = request.diagnostic.empty()
        ? "model resource set is not renderable"
        : request.diagnostic;
      instances_.push_back(std::move(instance));
      continue;
    }

    const auto cached = meshes.find(key);
    if (cached != meshes.end()) {
      instance.status = ModelInstanceStatus::Ready;
      instance.mesh = cached->second;
      instance.diagnostic = "shared cached mesh";
      instances_.push_back(std::move(instance));
      continue;
    }
    if (uniqueMeshCount_ >= limits_.maxUniqueMeshes) {
      instance.status = ModelInstanceStatus::BudgetExceeded;
      instance.diagnostic = "unique model mesh budget exceeded";
      diagnostics_.push_back(instance.diagnostic);
      instances_.push_back(std::move(instance));
      continue;
    }

    if (request.inspection.metadata.indices.empty()
        || request.inspection.metadata.indices.front().renderIndexCount > limits_.maxVerticesPerMesh
        || request.inspection.metadata.indices.front().renderIndexCount
            > limits_.maxTotalVertices - totalMeshVertices_) {
      instance.status = ModelInstanceStatus::BudgetExceeded;
      instance.diagnostic = "model descriptor exceeds vertex budget";
      diagnostics_.push_back(instance.diagnostic);
      instances_.push_back(std::move(instance));
      continue;
    }

    ModelMeshData mesh;
    std::string error;
    if (!ModelLoader::buildModelMeshData(request.inspection, 0, mesh, error)) {
      instance.status = ModelInstanceStatus::Unrenderable;
      instance.diagnostic = error.empty() ? "model mesh data build failed" : error;
      diagnostics_.push_back(instance.diagnostic);
      instances_.push_back(std::move(instance));
      continue;
    }
    if (mesh.vertices.empty() || !mesh.triangleList || mesh.vertices.size() % 3u != 0) {
      instance.status = ModelInstanceStatus::Unrenderable;
      instance.diagnostic = "model mesh is not a non-empty triangle list";
      diagnostics_.push_back(instance.diagnostic);
      instances_.push_back(std::move(instance));
      continue;
    }
    if (mesh.vertices.size() > limits_.maxVerticesPerMesh
        || totalMeshVertices_ > limits_.maxTotalVertices - mesh.vertices.size()) {
      instance.status = ModelInstanceStatus::BudgetExceeded;
      instance.diagnostic = "model vertex budget exceeded";
      diagnostics_.push_back(instance.diagnostic);
      instances_.push_back(std::move(instance));
      continue;
    }

    auto shared = std::make_shared<ModelMeshData>(std::move(mesh));
    meshes.emplace(key, shared);
    ++uniqueMeshCount_;
    totalMeshVertices_ += shared->vertices.size();
    instance.status = ModelInstanceStatus::Ready;
    instance.mesh = std::move(shared);
    instance.diagnostic = "mesh built and cached";
    instances_.push_back(std::move(instance));
  }
}

const char* modelInstanceStatusName(ModelInstanceStatus status) {
  switch (status) {
    case ModelInstanceStatus::Ready: return "ready";
    case ModelInstanceStatus::MissingAsset: return "missing_asset";
    case ModelInstanceStatus::Unrenderable: return "unrenderable";
    case ModelInstanceStatus::BudgetExceeded: return "budget_exceeded";
    case ModelInstanceStatus::InvalidRequest: return "invalid_request";
  }
  return "invalid_request";
}

} // namespace tf2::native
