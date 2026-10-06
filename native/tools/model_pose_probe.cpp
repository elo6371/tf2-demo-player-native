#include "model_loader.h"

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {
bool finite3(const std::array<float, 3>& value) {
  return std::isfinite(value[0]) && std::isfinite(value[1]) && std::isfinite(value[2]);
}
float length3(const std::array<float, 3>& value) {
  return std::sqrt(value[0] * value[0] + value[1] * value[1] + value[2] * value[2]);
}

template <typename T>
void writeAt(std::vector<std::uint8_t>& bytes, std::size_t offset, T value) {
  std::memcpy(bytes.data() + offset, &value, sizeof(T));
}

bool hasDiagnostic(const tf2::native::ModelInspection& inspection, const std::string& needle) {
  for (const auto& diagnostic : inspection.diagnostics) {
    if (diagnostic.find(needle) != std::string::npos) return true;
  }
  for (const auto& diagnostic : inspection.metadata.diagnostics) {
    if (diagnostic.find(needle) != std::string::npos) return true;
  }
  return false;
}

int selfTest() {
  const auto directory = std::filesystem::temp_directory_path();
  const auto attachmentPath = directory / "tf2-model-pose-probe-attachment-invalid.mdl";
  const auto bodyPartPath = directory / "tf2-model-pose-probe-bodypart-invalid.mdl";
  const auto bonePath = directory / "tf2-model-pose-probe-bone-invalid.mdl";
  const auto sequencePath = directory / "tf2-model-pose-probe-sequence-invalid.mdl";
  std::vector<std::uint8_t> bytes(316, 0);
  writeAt<std::uint32_t>(bytes, 0, 0x54534449u);
  writeAt<std::uint32_t>(bytes, 4, 48u);
  writeAt<std::uint32_t>(bytes, 8, 1u);
  writeAt<std::uint32_t>(bytes, 76, 316u);
  writeAt<std::uint32_t>(bytes, 248, 1u);
  writeAt<std::uint32_t>(bytes, 252, 400u);
  { std::ofstream out(attachmentPath, std::ios::binary); out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size()); }
  const auto attachmentInspection = tf2::native::ModelLoader::inspect(attachmentPath);
  const bool attachmentRejected = hasDiagnostic(attachmentInspection, "attachment table is outside");
  const bool attachmentPreflight = attachmentInspection.mdl.signatureValid
    && attachmentInspection.vvd.signatureValid;
  writeAt<std::uint32_t>(bytes, 248, 0u);
  writeAt<std::uint32_t>(bytes, 252, 0u);
  writeAt<std::uint32_t>(bytes, 240, 1u);
  writeAt<std::uint32_t>(bytes, 244, 400u);
  { std::ofstream out(bodyPartPath, std::ios::binary); out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size()); }
  const auto bodyPartInspection = tf2::native::ModelLoader::inspect(bodyPartPath);
  const bool bodyPartRejected = hasDiagnostic(bodyPartInspection, "bodypart table is outside");
  const bool bodyPartPreflight = bodyPartInspection.mdl.signatureValid
    && bodyPartInspection.vvd.signatureValid;
  writeAt<std::uint32_t>(bytes, 240, 0u);
  writeAt<std::uint32_t>(bytes, 244, 0u);
  writeAt<std::uint32_t>(bytes, 156, 1u);
  writeAt<std::uint32_t>(bytes, 160, 400u);
  { std::ofstream out(bonePath, std::ios::binary); out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size()); }
  const auto boneInspection = tf2::native::ModelLoader::inspect(bonePath);
  const bool boneRejected = hasDiagnostic(boneInspection, "bone table is outside");
  tf2::native::ModelMetadata mutatedMetadata;
  mutatedMetadata.boneCount = 1;
  mutatedMetadata.vertices.push_back(tf2::native::ModelVertex{});
  mutatedMetadata.vertices[0].boneCount = 1;
  mutatedMetadata.vertices[0].weights = {1.0f, 0.0f, 0.0f};
  mutatedMetadata.vertices[0].boneIndices = {1, 0, 0};
  mutatedMetadata.renderIndices.push_back(0);
  mutatedMetadata.indices.push_back(tf2::native::ModelIndexDescriptor{0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 1});
  std::string mutationError;
  const bool bindPoseMutationRejected = !tf2::native::ModelLoader::buildBindPoseMesh(mutatedMetadata, 0, mutationError);
  tf2::native::ModelDrawVertex mutatedVertex{};
  mutatedVertex.boneCount = 1;
  mutatedVertex.weights = {std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f};
  mutatedVertex.boneIndices = {0, 0, 0};
  std::array<float, 3> mutatedPosition{};
  std::string mutatedSkinError;
  const bool cpuSkinWeightMutationRejected = !tf2::native::ModelLoader::cpuSkinVertexReference(
    mutatedMetadata, mutatedVertex, mutatedPosition, mutatedSkinError);
  writeAt<std::uint32_t>(bytes, 156, 0u);
  writeAt<std::uint32_t>(bytes, 160, 0u);
  writeAt<std::uint32_t>(bytes, 188, 1u);
  writeAt<std::uint32_t>(bytes, 192, 400u);
  { std::ofstream out(sequencePath, std::ios::binary); out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size()); }
  const auto sequenceInspection = tf2::native::ModelLoader::inspect(sequencePath);
  const bool sequenceRejected = hasDiagnostic(sequenceInspection, "sequence table is outside");
  std::error_code ignored;
  std::filesystem::remove(attachmentPath, ignored);
  std::filesystem::remove(bodyPartPath, ignored);
  std::filesystem::remove(bonePath, ignored);
  std::filesystem::remove(sequencePath, ignored);
  std::cout << "{\"attachmentOutOfBoundsRejected\":" << (attachmentRejected ? "true" : "false")
    << ",\"bodyPartOutOfBoundsRejected\":" << (bodyPartRejected ? "true" : "false")
    << ",\"boneTableOutOfBoundsRejected\":" << (boneRejected ? "true" : "false")
    << ",\"bindPoseMutationRejected\":" << (bindPoseMutationRejected ? "true" : "false")
    << ",\"cpuSkinWeightMutationRejected\":" << (cpuSkinWeightMutationRejected ? "true" : "false")
    << ",\"sequenceTableOutOfBoundsRejected\":" << (sequenceRejected ? "true" : "false")
    << ",\"corruptPosePreflight\":" << ((!attachmentPreflight && !bodyPartPreflight) ? "false" : "true") << "}\n";
  return attachmentRejected && bodyPartRejected && boneRejected && bindPoseMutationRejected
    && cpuSkinWeightMutationRejected && sequenceRejected
    && !attachmentPreflight && !bodyPartPreflight ? 0 : 1;
}
}

int main(int argc, char** argv) {
  if (argc == 2 && std::string(argv[1]) == "--self-test") return selfTest();
  if (argc != 2) {
    std::cerr << "usage: model_pose_probe <model.mdl> | --self-test\n";
    return 2;
  }
  auto inspection = tf2::native::ModelLoader::inspect(
    std::filesystem::u8path(argv[1]));
  std::size_t invalidParents = 0;
  std::size_t invalidAttachments = 0;
  std::size_t invalidAttachmentAxes = 0;
  std::size_t invalidPoseToBone = 0;
  for (const auto& bone : inspection.metadata.bones) {
    if (bone.parent < -1 || bone.parent >= static_cast<std::int32_t>(inspection.metadata.bones.size())
        || !finite3(bone.position)) ++invalidParents;
    if (!bone.poseToBoneValid) ++invalidPoseToBone;
  }
  for (const auto& attachment : inspection.metadata.attachments) {
    if (attachment.bone < 0 || attachment.bone >= static_cast<std::int32_t>(inspection.metadata.bones.size())
        || !finite3(attachment.origin)) ++invalidAttachments;
    if (!finite3(attachment.axisX) || !finite3(attachment.axisY) || !finite3(attachment.axisZ)
        || length3(attachment.axisX) < 0.5f || length3(attachment.axisY) < 0.5f
        || length3(attachment.axisZ) < 0.5f) ++invalidAttachmentAxes;
  }
  const bool mdlReadable = inspection.mdl.signatureValid && inspection.metadata.valid;
  std::string bindPoseError;
  auto poseMetadata = inspection.metadata;
  const bool bindPoseReady = !poseMetadata.indices.empty()
    && tf2::native::ModelLoader::buildBindPoseMesh(poseMetadata, 0, bindPoseError);
  std::array<float, 3> referencePosition{};
  std::string referenceSkinError;
  const bool cpuSkinReferenceReady = bindPoseReady && !poseMetadata.bindPoseVertices.empty()
    && tf2::native::ModelLoader::cpuSkinVertexReference(inspection.metadata, poseMetadata.bindPoseVertices.front(), referencePosition, referenceSkinError);
  std::size_t invalidBindPoseWeights = 0;
  std::size_t invalidBindPoseBoneIndices = 0;
  std::size_t bindPoseWeightedVertices = 0;
  for (const auto& vertex : poseMetadata.bindPoseVertices) {
    const float weightSum = vertex.weights[0] + vertex.weights[1] + vertex.weights[2];
    if (!std::isfinite(weightSum) || weightSum < 0.99f || weightSum > 1.01f
        || !std::isfinite(vertex.weights[0]) || !std::isfinite(vertex.weights[1])
        || !std::isfinite(vertex.weights[2])) {
      ++invalidBindPoseWeights;
    }
    if (vertex.boneCount > 0) ++bindPoseWeightedVertices;
    for (std::size_t i = 0; i < vertex.boneCount && i < vertex.boneIndices.size(); ++i) {
      if (vertex.boneIndices[i] >= inspection.metadata.boneCount) {
        ++invalidBindPoseBoneIndices;
        break;
      }
    }
  }
  const bool posePreflight = mdlReadable && inspection.vvd.signatureValid
    && inspection.metadata.bones.size() > 0 && bindPoseReady;
  const bool passed = mdlReadable && invalidParents == 0 && invalidAttachments == 0
    && invalidAttachmentAxes == 0;
  const char* viewModelStatus = inspection.viewModelStatus == tf2::native::ModelFeatureStatus::Available ? "available"
    : inspection.viewModelStatus == tf2::native::ModelFeatureStatus::Missing ? "missing" : "unknown";
  std::cout << "{\"mdlReadable\":" << (mdlReadable ? "true" : "false")
    << ",\"bones\":" << inspection.metadata.bones.size()
    << ",\"mdlVersion\":" << inspection.metadata.version
    << ",\"boneStride\":" << inspection.metadata.boneStride
    << ",\"poseToBoneOffset\":" << inspection.metadata.poseToBoneOffset
    << ",\"sequenceFrameCountAvailable\":" << (inspection.metadata.sequenceFrameCountAvailable ? "true" : "false")
    << ",\"sequenceDecodeReason\":\"" << inspection.metadata.sequenceDecodeReason << "\""
    << ",\"attachments\":" << inspection.metadata.attachments.size()
    << ",\"bodyParts\":" << inspection.metadata.bodyParts.size()
    << ",\"invalidParents\":" << invalidParents
    << ",\"invalidAttachments\":" << invalidAttachments
    << ",\"invalidAttachmentAxes\":" << invalidAttachmentAxes
    << ",\"invalidPoseToBone\":" << invalidPoseToBone
    << ",\"companionSetComplete\":" << (inspection.renderableResourceSet ? "true" : "false")
    << ",\"posePreflight\":" << (posePreflight ? "true" : "false")
    << ",\"skinningVertexCount\":" << inspection.metadata.skinningVertexCount
    << ",\"invalidSkinVertexCount\":" << inspection.metadata.invalidSkinVertexCount
    << ",\"skinningDataAvailable\":" << (inspection.metadata.skinningDataAvailable ? "true" : "false")
    << ",\"bindPoseMatricesAvailable\":" << (inspection.metadata.bindPoseMatricesAvailable ? "true" : "false")
    << ",\"skinningBlockedReason\":\"" << inspection.metadata.skinningBlockedReason << "\""
    << ",\"bindPoseSkinningAttributes\":" << poseMetadata.bindPoseVertices.size()
    << ",\"bindPoseVertexCount\":" << poseMetadata.bindPoseVertexCount
    << ",\"vtxIndexCount\":" << inspection.metadata.vtxDiagnostics.vtxIndexCount
    << ",\"vtxDescriptorCount\":" << inspection.metadata.vtxDiagnostics.vtxDescriptorCount
    << ",\"vtxOutOfBoundsCount\":" << inspection.metadata.vtxDiagnostics.vtxOutOfBoundsCount
    << ",\"vtxDegenerateTriangleCount\":" << inspection.metadata.vtxDiagnostics.vtxDegenerateTriangleCount
    << ",\"bindPoseReady\":" << (bindPoseReady ? "true" : "false")
    << ",\"bindPoseWeightedVertices\":" << bindPoseWeightedVertices
    << ",\"invalidBindPoseWeights\":" << invalidBindPoseWeights
    << ",\"invalidBindPoseBoneIndices\":" << invalidBindPoseBoneIndices
    << ",\"bindPoseSkinningAttributesValid\":" << (bindPoseReady && invalidBindPoseWeights == 0 && invalidBindPoseBoneIndices == 0 ? "true" : "false")
    << ",\"skinMatricesUploaded\":false"
    << ",\"vertexWeightsApplied\":false"
    << ",\"cpuSkinReferenceReady\":" << (cpuSkinReferenceReady ? "true" : "false")
    << ",\"gpuSkinning\":\"missing\",\"viewModel\":\"" << viewModelStatus << "\""
    << ",\"viewModelResourceDetected\":" << (inspection.viewModelPathDetected ? "true" : "false")
    << ",\"viewModelResourceSetComplete\":" << (inspection.renderableResourceSet && inspection.viewModelPathDetected ? "true" : "false")
    << ",\"viewModelPathDetected\":" << (inspection.viewModelPathDetected ? "true" : "false") << "}\n";
  return passed ? 0 : 1;
}

