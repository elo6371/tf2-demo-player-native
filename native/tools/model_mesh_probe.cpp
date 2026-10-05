#include "model_loader.h"

#include <filesystem>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: model_mesh_probe <model.mdl>\n";
    return 2;
  }
  const auto inspection = tf2::native::ModelLoader::inspect(
    std::filesystem::u8path(argv[1]));
  auto metadata = inspection.metadata;
  std::string error;
  const bool bindPoseReady = !metadata.indices.empty()
    && tf2::native::ModelLoader::buildBindPoseMesh(metadata, 0, error);
  const bool indicesAreTriangles = metadata.bindPoseVertices.size() >= 3
    && metadata.bindPoseVertices.size() % 3 == 0;
  const bool vtxDecoded = inspection.metadata.vtxDiagnostics.vtxDescriptorCount > 0
    && inspection.metadata.vtxDiagnostics.vtxIndexCount >= metadata.bindPoseVertices.size()
    && inspection.metadata.vtxDiagnostics.vtxOutOfBoundsCount == 0;
  const bool resourcesReadable = inspection.mdl.signatureValid
    && inspection.vvd.signatureValid && inspection.renderableResourceSet;
  const bool passed = resourcesReadable && vtxDecoded && bindPoseReady && indicesAreTriangles;
  std::string modelPath = argv[1];
  for (auto& character : modelPath) if (character == '\\') character = '/';
  std::cout << "{\"model\":\"" << modelPath
    << "\",\"resourcesReadable\":" << (resourcesReadable ? "true" : "false")
    << ",\"renderableResourceSet\":" << (inspection.renderableResourceSet ? "true" : "false")
    << ",\"vtxDescriptorCount\":" << inspection.metadata.vtxDiagnostics.vtxDescriptorCount
    << ",\"vtxIndexCount\":" << inspection.metadata.vtxDiagnostics.vtxIndexCount
    << ",\"vtxOutOfBoundsCount\":" << inspection.metadata.vtxDiagnostics.vtxOutOfBoundsCount
    << ",\"bindPoseReady\":" << (bindPoseReady ? "true" : "false")
    << ",\"bindPoseVertexCount\":" << metadata.bindPoseVertices.size()
    << ",\"indicesAreTriangles\":" << (indicesAreTriangles ? "true" : "false")
    << ",\"status\":\"" << (passed ? "pass" : "fail") << "\"}\n";
  if (!passed && !error.empty()) std::cerr << "bind_pose_error=" << error << "\n";
  return passed ? 0 : 1;
}
