#pragma once

#include <array>
#include <string>

namespace tf2::native {

enum class ViewModelHand { Right, Left };

struct ViewModelRenderRequest {
  std::string modelPath;
  float fov = 80.0f;
  ViewModelHand hand = ViewModelHand::Right;
  std::string attachmentName;
  bool bodygroupSelectionKnown = false;
};

constexpr float defaultViewModelFov() { return 80.0f; }

inline std::array<float, 3> applyViewModelHand(const std::array<float, 3>& value, ViewModelHand hand) {
  if (hand == ViewModelHand::Right) return value;
  return {-value[0], value[1], value[2]};
}

inline bool validViewModelFov(float value) {
  return value >= 40.0f && value <= 120.0f;
}

} // namespace tf2::native
