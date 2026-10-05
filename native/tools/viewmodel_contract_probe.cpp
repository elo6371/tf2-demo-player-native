#include "model_loader.h"
#include "viewmodel.h"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>

namespace {
bool finite3(const std::array<float, 3>& v) {
  return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}
float length3(const std::array<float, 3>& v) {
  return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}
const char* status(tf2::native::ModelFeatureStatus value) {
  return value == tf2::native::ModelFeatureStatus::Available ? "available"
    : value == tf2::native::ModelFeatureStatus::Missing ? "missing" : "unknown";
}
}

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: viewmodel_contract_probe <v_model.mdl>\n";
    return 2;
  }
  const auto inspection = tf2::native::ModelLoader::inspect(std::filesystem::u8path(argv[1]));
  std::size_t invalidAttachments = 0;
  std::size_t validAttachments = 0;
  for (const auto& attachment : inspection.metadata.attachments) {
    const bool valid = attachment.bone >= 0
      && attachment.bone < static_cast<std::int32_t>(inspection.metadata.bones.size())
      && finite3(attachment.origin) && finite3(attachment.axisX)
      && finite3(attachment.axisY) && finite3(attachment.axisZ)
      && length3(attachment.axisX) >= 0.5f && length3(attachment.axisY) >= 0.5f
      && length3(attachment.axisZ) >= 0.5f;
    if (valid) ++validAttachments; else ++invalidAttachments;
  }
  const auto right = tf2::native::applyViewModelHand({1.0f, 2.0f, 3.0f}, tf2::native::ViewModelHand::Right);
  const auto left = tf2::native::applyViewModelHand({1.0f, 2.0f, 3.0f}, tf2::native::ViewModelHand::Left);
  const bool handProbePassed = right[0] == 1.0f && left[0] == -1.0f
    && right[1] == left[1] && right[2] == left[2];
  const bool resourceComplete = inspection.mdl.signatureValid && inspection.vvd.signatureValid
    && (inspection.vtx.signatureValid || inspection.vtxDx80.signatureValid || inspection.vtxSw.signatureValid);
  const bool attachmentsValid = invalidAttachments == 0
    && inspection.metadata.attachments.size() == inspection.metadata.attachmentCount;
  tf2::native::ViewModelRenderRequest request;
  request.modelPath = argv[1];
  request.resourceComplete = resourceComplete;
  request.attachmentStatus = status(inspection.attachmentStatus);
  request.bodygroupStatus = status(inspection.bodygroupStatus);
  request.sequenceCount = static_cast<std::uint32_t>(inspection.metadata.sequences.size());
  request.sequenceStatus = inspection.metadata.sequenceFrameCountAvailable ? "available" : "unsupported";
  request.sequenceDecodeReason = inspection.metadata.sequenceDecodeReason;
  std::cout << "{\"mdlReadable\":" << (inspection.metadata.valid ? "true" : "false")
    << ",\"resourceComplete\":" << (resourceComplete ? "true" : "false")
    << ",\"viewModelPathDetected\":" << (inspection.viewModelPathDetected ? "true" : "false")
    << ",\"viewModelStatus\":\"" << status(inspection.viewModelStatus) << "\""
    << ",\"attachmentStatus\":\"" << status(inspection.attachmentStatus) << "\""
    << ",\"attachments\":" << inspection.metadata.attachments.size()
    << ",\"validAttachments\":" << validAttachments
    << ",\"invalidAttachments\":" << invalidAttachments
    << ",\"bodygroupStatus\":\"" << status(inspection.bodygroupStatus) << "\""
    << ",\"bodyParts\":" << inspection.metadata.bodyParts.size()
    << ",\"sequenceCount\":" << inspection.metadata.sequences.size()
    << ",\"sequenceFrameCountAvailable\":" << (inspection.metadata.sequenceFrameCountAvailable ? "true" : "false")
    << ",\"sequenceDecodeReason\":\"" << inspection.metadata.sequenceDecodeReason << "\""
    << ",\"defaultFov\":" << tf2::native::defaultViewModelFov()
    << ",\"fovRangeValid\":" << (tf2::native::validViewModelFov(80.0f) ? "true" : "false")
    << ",\"rightHandX\":" << right[0]
    << ",\"leftHandX\":" << left[0]
    << ",\"handProbePassed\":" << (handProbePassed ? "true" : "false")
    << ",\"animationStatus\":\"unsupported\""
    << ",\"requestResourceComplete\":" << (request.resourceComplete ? "true" : "false")
    << ",\"requestAttachmentStatus\":\"" << request.attachmentStatus << "\""
    << ",\"requestBodygroupStatus\":\"" << request.bodygroupStatus << "\""
    << ",\"requestSequenceCount\":" << request.sequenceCount
    << ",\"requestSequenceStatus\":\"" << request.sequenceStatus << "\""
    << ",\"firstPersonDrawn\":false}\n";
  return (resourceComplete && inspection.viewModelPathDetected && attachmentsValid && handProbePassed) ? 0 : 1;
}
