#pragma once

#include <cmath>

namespace tf2::native {

// Source's player FOV is horizontal at a 4:3 reference aspect. DirectXMath's
// perspective helper expects the vertical angle, so convert once at the
// projection boundary. Keeping vertical FOV constant lets each pane's own
// aspect ratio determine its horizontal field of view.
inline float sourceHorizontalFovToVertical(float horizontalDegrees) {
  if (!std::isfinite(horizontalDegrees) || horizontalDegrees <= 0.0f
      || horizontalDegrees >= 180.0f) return -1.0f;
  constexpr float kPi = 3.14159265358979323846f;
  const float halfRadians = horizontalDegrees * (kPi / 360.0f);
  return 2.0f * std::atan(std::tan(halfRadians) * 0.75f) * (180.0f / kPi);
}

} // namespace tf2::native
