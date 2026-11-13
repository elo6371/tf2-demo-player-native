#include "camera_director.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace tf2::native {

namespace {

bool finitePose(const CameraPose& pose) {
  return std::isfinite(pose.focusX) && std::isfinite(pose.focusY)
    && std::isfinite(pose.focusZ) && std::isfinite(pose.yaw)
    && std::isfinite(pose.pitch) && std::isfinite(pose.distance);
}

std::string formatFloat(float value) {
  char buffer[32]{};
  // %.9g is FLT_DECIMAL_DIG: every float survives write -> read -> float with
  // the same bits, which is the whole basis of "the pose came back exactly".
  std::snprintf(buffer, sizeof(buffer), "%.9g", static_cast<double>(value));
  return buffer;
}

std::string formatFloat6(float value) {
  char buffer[32]{};
  std::snprintf(buffer, sizeof(buffer), "%.6f", static_cast<double>(value));
  return buffer;
}

} // namespace

bool poseEqual(const CameraPose& a, const CameraPose& b) {
  if (a.valid != b.valid) return false;
  if (!a.valid) return true;
  // A numeric comparison rather than a bit comparison: std::remainder can
  // produce -0 for a yaw of exactly zero, and +0 and -0 draw the same frame, so
  // the claim "the same pose" is the numeric one.
  return a.focusX == b.focusX && a.focusY == b.focusY && a.focusZ == b.focusZ
    && a.yaw == b.yaw && a.pitch == b.pitch && a.distance == b.distance;
}

std::string formatPoseField(float value) { return formatFloat(value); }

std::string formatPose(const CameraPose& pose) {
  if (!pose.valid) return {};
  return formatFloat(pose.focusX) + "," + formatFloat(pose.focusY) + ","
    + formatFloat(pose.focusZ) + "," + formatFloat(pose.yaw) + ","
    + formatFloat(pose.pitch) + "," + formatFloat(pose.distance);
}

bool parsePose(const std::string& text, CameraPose& pose) {
  pose = CameraPose{};
  if (text.empty()) return false;
  float values[6] = {};
  std::size_t start = 0;
  for (int index = 0; index < 6; ++index) {
    const auto comma = text.find(',', start);
    const std::string field = comma == std::string::npos
      ? text.substr(start) : text.substr(start, comma - start);
    if (field.empty()) return false;
    char* end = nullptr;
    const double parsed = std::strtod(field.c_str(), &end);
    if (!end || *end != '\0' || !std::isfinite(parsed)) return false;
    values[index] = static_cast<float>(parsed);
    if (index < 5) {
      if (comma == std::string::npos) return false;
      start = comma + 1;
    } else if (comma != std::string::npos) {
      // A seventh field means the text is not a pose this parser wrote.
      return false;
    }
  }
  pose.focusX = values[0];
  pose.focusY = values[1];
  pose.focusZ = values[2];
  pose.yaw = values[3];
  pose.pitch = values[4];
  pose.distance = values[5];
  pose.valid = true;
  return true;
}

std::string poseTraceFields(const CameraPose& pose) {
  return "focus=" + formatFloat6(pose.focusX) + "," + formatFloat6(pose.focusY) + ","
    + formatFloat6(pose.focusZ) + " yaw=" + formatFloat6(pose.yaw)
    + " pitch=" + formatFloat6(pose.pitch) + " dist=" + formatFloat6(pose.distance);
}

bool CameraDirector::noteDemoTick(bool paused, std::int32_t tick) {
  ++tickReadings_;
  bool advanced = false;
  if (lastTick_ >= 0) {
    if (paused) {
      if (tick != lastTick_) {
        // The freeze was broken by whatever produced this tick. Counted, not
        // corrected: a director that silently put the tick back would hide the
        // defect from the gate that is supposed to see it.
        ++pauseViolations_;
      } else {
        ++frozenDemoFrames_;
      }
    } else if (tick != lastTick_) {
      advanced = true;
      ++advancedDemoFrames_;
    }
  }
  lastTick_ = tick;
  return advanced;
}

void CameraDirector::noteMove(CameraMoveSource source) {
  ++cameraMoves_;
  if (paused_) ++cameraMovesWhilePaused_;
  if (source == CameraMoveSource::FreeFly) ++freeFlySteps_;
  if (source == CameraMoveSource::Bookmark) ++bookmarkApplies_;
}

void CameraDirector::applyPose(const CameraPose& pose, CameraMoveSource source) {
  if (!pose.valid || !finitePose(pose)) return;
  if (!poseEqual(pose_, pose)) ++poseChanges_;
  pose_ = pose;
  noteMove(source);
}

void CameraDirector::orbit(float deltaX, float deltaY) {
  if (!std::isfinite(deltaX) || !std::isfinite(deltaY)) return;
  // Same constants Renderer::orbitCamera uses, so the pure half and the draw
  // half cannot drift apart: 0.008 rad per pixel, pitch clamped to the range the
  // projection matrix can actually look along.
  constexpr float kPi = 3.14159265358979323846f;
  CameraPose next = pose_;
  next.valid = true;
  next.yaw = std::remainder(pose_.yaw + deltaX * 0.008f, kPi * 2.0f);
  next.pitch = std::clamp(pose_.pitch + deltaY * 0.008f, 0.25f, 1.45f);
  applyPose(next, CameraMoveSource::Orbit);
}

void CameraDirector::zoom(float delta) {
  if (!std::isfinite(delta)) return;
  CameraPose next = pose_;
  next.valid = true;
  next.distance = std::clamp(pose_.distance * std::exp(-delta * 0.08f), 0.8f, 12.0f);
  applyPose(next, CameraMoveSource::Zoom);
}

void CameraDirector::setFreeFly(bool enabled) {
  freeFly_ = enabled;
}

void CameraDirector::requestResetToPlayer() {
  resetToPlayerPending_ = true;
  ++resetToPlayerRequests_;
}

bool CameraDirector::takeResetToPlayer() {
  if (!resetToPlayerPending_) return false;
  resetToPlayerPending_ = false;
  ++resetToPlayerApplies_;
  return true;
}

void CameraDirector::resetView() {
  CameraPose next;
  next.valid = true;
  next.focusX = pose_.focusX;
  next.focusY = pose_.focusY;
  next.focusZ = kDefaultFocusZ;
  next.yaw = kDefaultCameraYaw;
  next.pitch = kDefaultCameraPitch;
  next.distance = kDefaultCameraDistance;
  freeFly_ = false;
  ++resetViewCount_;
  applyPose(next, CameraMoveSource::Reset);
}

} // namespace tf2::native
