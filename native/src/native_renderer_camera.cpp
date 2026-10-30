// Camera: orbit/zoom, observer focus, demo-view world conversion and camera presets.
//
// One translation unit of the renderer split: the bodies were moved verbatim out
// of native_renderer.cpp, so pass order, thresholds and trace text are unchanged.

#include "native_renderer.h"
#include "renderer_internal.h"

#include "entity_model.h"
#include "hud_font.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace tf2::native {


void Renderer::orbitCamera(float deltaX, float deltaY) {
  if (!std::isfinite(deltaX) || !std::isfinite(deltaY)) return;
  observerDemoView_ = false;
  constexpr float kPi = 3.14159265358979323846f;
  const float yaw = std::remainder(cameraYaw_ + deltaX * 0.008f, kPi * 2.0f);
  const float pitch = cameraPitch_ + deltaY * 0.008f;
  if (std::isfinite(yaw)) cameraYaw_ = yaw;
  if (std::isfinite(pitch)) cameraPitch_ = std::clamp(pitch, 0.25f, 1.45f);
}

void Renderer::zoomCamera(float delta) {
  if (!std::isfinite(delta)) return;
  const float distance = cameraDistance_ * std::exp(-delta * 0.08f);
  if (std::isfinite(distance)) cameraDistance_ = std::clamp(distance, 0.8f, 12.0f);
}

void Renderer::setObserverFocus(float x, float y, float z) {
  if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return;
  observerFocusX_ = std::clamp(x, -1.0e6f, 1.0e6f);
  observerFocusY_ = std::clamp(y, -1.0e6f, 1.0e6f);
  observerFocusZ_ = std::clamp(z, -1.0e6f, 1.0e6f);
  observerFocusValid_ = true;
}

void Renderer::setObserverFocusWorld(float x, float y, float z) {
  if (!worldBoundsValid_ || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)
      || !std::isfinite(worldCenterX_) || !std::isfinite(worldCenterY_)
      || !std::isfinite(worldMinZ_) || !std::isfinite(worldSpanZ_)
      || !std::isfinite(worldHorizontalScale_) || worldSpanZ_ <= 0.0f
      || worldHorizontalScale_ <= 0.0f) return;
  const float normalizedX = (x - worldCenterX_) * worldHorizontalScale_;
  const float normalizedY = (y - worldCenterY_) * worldHorizontalScale_;
  const float normalizedZ = (z - worldMinZ_) / worldSpanZ_ * 0.8f + 0.1f;
  if (!std::isfinite(normalizedX) || !std::isfinite(normalizedY) || !std::isfinite(normalizedZ)) return;
  setObserverFocus(normalizedX, normalizedY, normalizedZ);
}

bool Renderer::observerFocusWorld(float& x, float& y, float& z) const {
  if (!observerFocusValid_) return false;
  x = observerFocusX_; y = observerFocusY_; z = observerFocusZ_;
  return true;
}

bool Renderer::observerFocusMapWorld(float& x, float& y, float& z) const {
  if (!observerFocusValid_) return false;
  if (!std::isfinite(worldMinZ_) || !std::isfinite(worldSpanZ_)
      || !std::isfinite(worldHorizontalScale_) || worldSpanZ_ <= 0.0f
      || worldHorizontalScale_ <= 0.0f) return false;
  // The exact inverse of setObserverFocusWorld's three lines, in the same
  // order a reader of that function meets them. A division instead of the
  // forward multiplication, and the z bias unwound before the span: getting
  // either backwards would hand the pvs a point that is not where the camera
  // is, which is the defect this exists to fix.
  x = (observerFocusX_ - 0.0f) / worldHorizontalScale_ + worldCenterX_;
  y = (observerFocusY_ - 0.0f) / worldHorizontalScale_ + worldCenterY_;
  z = (observerFocusZ_ - 0.1f) / 0.8f * worldSpanZ_ + worldMinZ_;
  if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return false;
  return true;
}

void Renderer::clearObserverDemoView() { observerDemoView_ = false; }

void Renderer::setObserverDemoViewWorld(float x, float y, float z, float pitchDegrees, float yawDegrees) {
  if (!std::isfinite(pitchDegrees) || !std::isfinite(yawDegrees)) return;
  setObserverFocusWorld(x, y, z);
  if (!observerFocusValid_) { observerDemoView_ = false; return; }
  observerDemoView_ = true;
  observerDemoEyeX_ = observerFocusX_;
  observerDemoEyeY_ = observerFocusY_;
  observerDemoEyeZ_ = observerFocusZ_;
  observerDemoPitchDeg_ = pitchDegrees;
  observerDemoYawDeg_ = yawDegrees;
}

bool Renderer::saveCameraPreset(std::size_t slot) {
  if (slot >= cameraPresets_.size() || !std::isfinite(cameraYaw_)
      || !std::isfinite(cameraPitch_) || !std::isfinite(cameraDistance_)) return false;
  const float focusX = observerFocusValid_ ? observerFocusX_ : 0.0f;
  const float focusY = observerFocusValid_ ? observerFocusY_ : 0.0f;
  const float focusZ = observerFocusValid_ ? observerFocusZ_ : 0.5f;
  if (!std::isfinite(focusX) || !std::isfinite(focusY) || !std::isfinite(focusZ)) return false;
  auto& preset = cameraPresets_[slot];
  preset.focusX = std::clamp(focusX, -1.0e6f, 1.0e6f);
  preset.focusY = std::clamp(focusY, -1.0e6f, 1.0e6f);
  preset.focusZ = std::clamp(focusZ, -1.0e6f, 1.0e6f);
  preset.yaw = std::remainder(cameraYaw_, 6.28318530717958647692f);
  preset.pitch = std::clamp(cameraPitch_, 0.25f, 1.45f);
  preset.distance = std::clamp(cameraDistance_, 0.8f, 12.0f);
  preset.valid = true;
  return true;
}

bool Renderer::applyCameraPreset(std::size_t slot) {
  if (slot >= cameraPresets_.size() || !cameraPresets_[slot].valid) return false;
  const auto& preset = cameraPresets_[slot];
  if (!std::isfinite(preset.focusX) || !std::isfinite(preset.focusY)
      || !std::isfinite(preset.focusZ) || !std::isfinite(preset.yaw)
      || !std::isfinite(preset.pitch) || !std::isfinite(preset.distance)) return false;
  setObserverFocus(preset.focusX, preset.focusY, preset.focusZ);
  cameraYaw_ = std::remainder(preset.yaw, 6.28318530717958647692f);
  cameraPitch_ = std::clamp(preset.pitch, 0.25f, 1.45f);
  cameraDistance_ = std::clamp(preset.distance, 0.8f, 12.0f);
  return true;
}

bool Renderer::clearCameraPreset(std::size_t slot) {
  if (slot >= cameraPresets_.size()) return false;
  cameraPresets_[slot] = CameraPreset{};
  return true;
}

void Renderer::clearCameraPresets() {
  cameraPresets_.fill(CameraPreset{});
}

void Renderer::resetCamera() {
  cameraYaw_ = 0.0f;
  cameraPitch_ = 0.36f;
  cameraDistance_ = 1.65f;
  observerDemoView_ = false;
  observerFocusValid_ = false;
  observerFocusX_ = 0.0f;
  observerFocusY_ = 0.0f;
  observerFocusZ_ = 0.25f;
}

// T50. The three numbers above are the shipped defaults, repeated in
// camera_director.h as kDefaultCameraYaw/Pitch/Distance/FocusZ so the pure half
// can reset without linking the renderer. Two spellings of one constant is how
// they drift, so the product asserts they agree at the call site (main.cpp) and
// camera_probe pins the director's copies. Read here for the trace as well.

Renderer::OrbitPoseReading Renderer::orbitPose() const {
  OrbitPoseReading reading;
  if (!observerFocusValid_) return reading;
  if (!std::isfinite(observerFocusX_) || !std::isfinite(observerFocusY_)
      || !std::isfinite(observerFocusZ_) || !std::isfinite(cameraYaw_)
      || !std::isfinite(cameraPitch_) || !std::isfinite(cameraDistance_)) return reading;
  reading.valid = true;
  reading.focusX = observerFocusX_;
  reading.focusY = observerFocusY_;
  reading.focusZ = observerFocusZ_;
  reading.yaw = cameraYaw_;
  reading.pitch = cameraPitch_;
  reading.distance = cameraDistance_;
  reading.demoView = observerDemoView_;
  reading.fovDegrees = settings_.viewModelFov;
  return reading;
}

bool Renderer::setOrbitPose(float focusX, float focusY, float focusZ,
    float yaw, float pitch, float distance) {
  if (!std::isfinite(focusX) || !std::isfinite(focusY) || !std::isfinite(focusZ)
      || !std::isfinite(yaw) || !std::isfinite(pitch) || !std::isfinite(distance)) {
    return false;
  }
  // The same clamps applyCameraPreset applies, so a pose that went through a
  // bookmark and a pose that went through a preset reach the draw identically.
  setObserverFocus(std::clamp(focusX, -1.0e6f, 1.0e6f),
    std::clamp(focusY, -1.0e6f, 1.0e6f), std::clamp(focusZ, -1.0e6f, 1.0e6f));
  cameraYaw_ = std::remainder(yaw, 6.28318530717958647692f);
  cameraPitch_ = std::clamp(pitch, 0.25f, 1.45f);
  cameraDistance_ = std::clamp(distance, 0.8f, 12.0f);
  return true;
}

bool Renderer::cameraEyeMapWorld(float& x, float& y, float& z) const {
  if (observerDemoView_) {
    // A demo view already stores its eye in the box; convert that one back
    // rather than reading the demo degrees, so one direction of conversion is
    // used for both camera kinds.
    return observerFocusMapWorld(x, y, z);
  }
  if (!observerFocusValid_ || !std::isfinite(cameraYaw_) || !std::isfinite(cameraPitch_)
      || !std::isfinite(cameraDistance_) || !std::isfinite(worldHorizontalScale_)
      || worldHorizontalScale_ <= 0.0f || !std::isfinite(worldSpanZ_)
      || worldSpanZ_ <= 0.0f) {
    return false;
  }
  // The orbit eye, computed in the SAME box the view matrix is built from
  // (draw(): target + (cos yaw cos pitch, sin yaw cos pitch, sin pitch) *
  // distance) and then converted to map units by the one inverse in this file.
  const float horizontal = std::cos(cameraPitch_) * cameraDistance_;
  const float boxX = observerFocusX_ + std::cos(cameraYaw_) * horizontal;
  const float boxY = observerFocusY_ + std::sin(cameraYaw_) * horizontal;
  const float boxZ = observerFocusZ_ + std::sin(cameraPitch_) * cameraDistance_;
  x = (boxX - 0.0f) / worldHorizontalScale_ + worldCenterX_;
  y = (boxY - 0.0f) / worldHorizontalScale_ + worldCenterY_;
  z = (boxZ - 0.1f) / 0.8f * worldSpanZ_ + worldMinZ_;
  return std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
}

} // namespace tf2::native
