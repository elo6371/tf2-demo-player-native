#include "model_instance_cache.h"

#include <filesystem>
#include <iostream>

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: model_instance_cache_probe <model.mdl>\n";
    return 2;
  }
  const auto modelPath = std::filesystem::u8path(argv[1]);
  const auto inspection = tf2::native::ModelLoader::inspect(modelPath);
  tf2::native::ModelRenderRequest readyRequest;
  readyRequest.entityIndex = 7;
  readyRequest.classId = 42;
  readyRequest.className = "CTestModel";
  readyRequest.modelPath = "models/probe/scout.mdl";
  readyRequest.resolution = tf2::native::ModelAssetResolution::FoundLoose;
  readyRequest.companionSetComplete = true;
  readyRequest.renderable = true;
  readyRequest.inspection = inspection;

  auto duplicate = readyRequest;
  duplicate.entityIndex = 8;
  duplicate.modelPath = "MODELS\\PROBE\\SCOUT.MDL";
  std::vector<tf2::native::ModelRenderRequest> requests{readyRequest, duplicate};
  tf2::native::ModelRenderRequest missing;
  missing.entityIndex = 9;
  missing.modelPath = "models/missing_probe.mdl";
  missing.resolution = tf2::native::ModelAssetResolution::Missing;
  missing.diagnostic = "model not found in loose files or VPK archives";
  requests.push_back(missing);

  tf2::native::ModelInstanceCache cache;
  cache.rebuild(requests);
  const auto& instances = cache.instances();
  const bool shared = instances.size() == 3 && instances[0].mesh
    && instances[0].mesh == instances[1].mesh;
  const bool duplicateReady = instances.size() == 3
    && instances[0].status == tf2::native::ModelInstanceStatus::Ready
    && instances[1].status == tf2::native::ModelInstanceStatus::Ready;
  const bool missingSafe = instances.size() == 3
    && instances[2].status == tf2::native::ModelInstanceStatus::MissingAsset
    && !instances[2].mesh;
  tf2::native::ModelInstanceCache tiny({1, 1, 1, 1});
  tiny.rebuild({readyRequest});
  const bool budgetSafe = tiny.instances().size() == 1
    && tiny.instances()[0].status == tf2::native::ModelInstanceStatus::BudgetExceeded;
  tf2::native::ModelInstanceCache instanceLimit({1, 1, 10000, 10000});
  instanceLimit.rebuild({readyRequest, duplicate});
  const bool instanceLimitSafe = instanceLimit.instances().size() == 1
    && instanceLimit.instances()[0].status == tf2::native::ModelInstanceStatus::Ready
    && instanceLimit.droppedInstanceCount() == 1;
  const bool passed = inspection.renderableResourceSet && duplicateReady && shared
    && missingSafe && budgetSafe && instanceLimitSafe;
  std::cout << "{\"renderableResourceSet\":"
    << (inspection.renderableResourceSet ? "true" : "false")
    << ",\"instances\":" << instances.size()
    << ",\"uniqueMeshes\":" << cache.uniqueMeshCount()
    << ",\"totalMeshVertices\":" << cache.totalMeshVertices()
    << ",\"droppedInstances\":" << cache.droppedInstanceCount()
    << ",\"duplicateReady\":" << (duplicateReady ? "true" : "false")
    << ",\"sharedMesh\":" << (shared ? "true" : "false")
    << ",\"missingSafe\":" << (missingSafe ? "true" : "false")
    << ",\"budgetSafe\":" << (budgetSafe ? "true" : "false")
    << ",\"instanceLimitSafe\":" << (instanceLimitSafe ? "true" : "false")
    << ",\"status\":\"" << (passed ? "pass" : "fail") << "\"}\n";
  return passed ? 0 : 1;
}
