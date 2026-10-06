#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "model_loader.h"

namespace tf2::native {

// Same clamp as native_renderer.h clampViewModelFov. NaN stays NaN because
// both comparisons are false. Kept here so ViewModel code does not include
// the D3D renderer header. The default used when the caller omits a value is 80.
inline constexpr float kDefaultViewModelFov = 80.0f;

inline float normalizeViewModelFov(float value) {
  return value < 40.0f ? 40.0f : (value > 120.0f ? 120.0f : value);
}

enum class ViewModelHand { Right, Left };

enum class ViewModelStatus { Ok, Failed };

// Compatibility contract used by the existing renderer-neutral probe. These
// fields describe a request only; drawing remains outside this module.
struct ViewModelRenderRequest {
  std::string modelPath;
  float fov = kDefaultViewModelFov;
  ViewModelHand hand = ViewModelHand::Right;
  std::string attachmentName;
  bool bodygroupSelectionKnown = false;
  bool resourceComplete = false;
  std::string attachmentStatus = "unknown";
  std::string bodygroupStatus = "unknown";
  std::string sequenceStatus = "unsupported";
  std::string sequenceDecodeReason;
  std::uint32_t sequenceCount = 0;
  bool firstPersonDrawn = false;
};

inline constexpr float defaultViewModelFov() { return kDefaultViewModelFov; }
inline bool validViewModelFov(float value) {
  return value >= 40.0f && value <= 120.0f;
}
inline std::array<float, 3> applyViewModelHand(const std::array<float, 3>& value, ViewModelHand hand) {
  return hand == ViewModelHand::Right ? value : std::array<float, 3>{-value[0], value[1], value[2]};
}

struct ViewModelAttachment {
  std::string name;
  std::array<float, 3> origin{};
};

// Renderer-neutral request. It does not draw and does not upload a mesh.
struct ViewModelRequest {
  ViewModelStatus status = ViewModelStatus::Failed;
  std::string reason;
  std::string path;
  ModelAssetResolution resolution = ModelAssetResolution::Missing;
  bool companionsComplete = false;
  float fov = kDefaultViewModelFov;
  ViewModelHand hand = ViewModelHand::Right;
  // Row-major 3x4. Right is identity. Left mirrors X, so +X becomes -X.
  std::array<float, 12> handTransform{};
  std::vector<std::string> sequenceLabels;
  std::vector<ViewModelAttachment> attachments;
};

std::array<float, 12> viewModelHandMatrix(ViewModelHand hand);

ViewModelRequest buildViewModelRequest(const AssetRoot& root, const std::string& path,
  ViewModelHand hand, std::optional<float> fov = std::nullopt);

} // namespace tf2::native
