#include "model_instance_contract.h"

#include <iostream>
#include <limits>

int main() {
  using namespace tf2::native;
  const ModelMeshDataContract meshData{42, 128, 384};
  const ModelInstanceTransformContract first{7, &meshData, {10.0f, 20.0f, 30.0f}, {0.0f, 90.0f, 0.0f}};
  const ModelInstanceTransformContract second{8, &meshData, {-5.0f, 4.0f, 12.0f}, {15.0f, 180.0f, 2.0f}};
  auto invalid = second;
  invalid.origin[1] = std::numeric_limits<float>::quiet_NaN();
  const bool meshShared = first.mesh == second.mesh;
  const bool transformsIndependent = first.entityIndex != second.entityIndex
    && first.origin != second.origin && first.angles != second.angles;
  const bool finiteAccepted = validModelInstanceTransform(first) && validModelInstanceTransform(second);
  const bool nonFiniteRejected = !validModelInstanceTransform(invalid);
  std::cout << "{\"sharedMesh\":" << (meshShared ? "true" : "false")
    << ",\"transformsIndependent\":" << (transformsIndependent ? "true" : "false")
    << ",\"finiteAccepted\":" << (finiteAccepted ? "true" : "false")
    << ",\"nonFiniteRejected\":" << (nonFiniteRejected ? "true" : "false")
    << ",\"animationStatus\":\"unsupported\",\"viewModelStatus\":\"unknown\"}\n";
  return meshShared && transformsIndependent && finiteAccepted && nonFiniteRejected ? 0 : 1;
}
