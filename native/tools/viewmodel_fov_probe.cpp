#include "native_renderer.h"

#include <cmath>
#include <iostream>

int main() {
  const bool low = tf2::native::clampViewModelFov(0.0f) == 40.0f;
  const bool high = tf2::native::clampViewModelFov(180.0f) == 120.0f;
  const bool middle = std::fabs(tf2::native::clampViewModelFov(80.0f) - 80.0f) < 1e-6f;
  std::cout << "{\"lowClamped\":" << (low ? "true" : "false")
    << ",\"highClamped\":" << (high ? "true" : "false")
    << ",\"middlePreserved\":" << (middle ? "true" : "false")
    << ",\"viewModelRender\":\"unknown\"}\n";
  return low && high && middle ? 0 : 1;
}
