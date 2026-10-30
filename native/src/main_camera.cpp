// T76 (2026-10-30): the T50 camera pose and bookmark layer, moved out of
// main.cpp as a pure move. The director is the pure half of the camera (no
// D3D11); the renderer is the half that draws. syncDirectorPoseToRenderer is
// the one place the two are connected, which is what makes "the pose the trace
// prints" and "the pose the frame is drawn from" the same number.
//
// The state objects are defined here rather than in main.cpp because main.cpp
// keeps them in an anonymous namespace, which no other translation unit can
// name. They are declared in main_camera.h and the key table is deliberately
// left behind (see that header).

#include "main_camera.h"

#include "playback_state.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace tf2::native::main_internal {

// Camera mode machine (T4). The controller owns the mode and the SPACE
// single/double classification; the per-scene-tick decision is
// decideCameraSceneAction and the counters below are what the trace reports
// and camera-ui-check.sh pins.
tf2::native::CameraModeController g_cameraMode;

// T50. The camera director owns the freeze rule and the camera operations; the
// bookmark store owns the named positions; the key table owns every binding the
// product has. Declared here rather than inside wWinMain because the window
// procedure's key arm needs all three.
tf2::native::CameraDirector g_cameraDirector;
tf2::native::CameraBookmarkStore g_cameraBookmarks;
// Written by jumpToCameraBookmark (below) and by handleCameraAction in main.cpp:
// the last mode the product actually applied, which the summary reports.
tf2::native::CameraMode g_lastAppliedCameraMode = tf2::native::CameraMode::FirstPerson;

// Pushes the director's pose into the renderer. The director is the pure half
// (it has no D3D11), so this is the one place the two are connected; every
// camera operation goes through it rather than touching the renderer directly,
// which is what makes "the pose the trace prints" and "the pose the frame is
// drawn from" the same number.
void syncDirectorPoseToRenderer(tf2::native::CameraMoveSource source) {
  if (!g_renderer) return;
  const auto& pose = g_cameraDirector.pose();
  if (!pose.valid) return;
  if (source == tf2::native::CameraMoveSource::Reset) {
    // A view reset is the renderer's own reset (it also clears the demo view),
    // and then the director's focus is re-applied so the two agree.
    g_renderer->resetCamera();
    g_renderer->setOrbitPose(pose.focusX, pose.focusY, pose.focusZ,
      pose.yaw, pose.pitch, pose.distance);
    return;
  }
  g_renderer->setOrbitPose(pose.focusX, pose.focusY, pose.focusZ,
    pose.yaw, pose.pitch, pose.distance);
}

// Reads the renderer's current pose back into the director. Called once per
// scene tick, so the director always holds what the frame was drawn from and a
// bookmark saved between ticks captures the drawn camera rather than a stale
// copy of it.
void syncRendererPoseToDirector(tf2::native::CameraMoveSource source) {
  if (!g_renderer) return;
  const auto reading = g_renderer->orbitPose();
  if (!reading.valid) return;
  tf2::native::CameraPose pose;
  pose.focusX = reading.focusX;
  pose.focusY = reading.focusY;
  pose.focusZ = reading.focusZ;
  pose.yaw = reading.yaw;
  pose.pitch = reading.pitch;
  pose.distance = reading.distance;
  pose.valid = true;
  // The SOURCE travels with the pose: it is what separates "the camera moved"
  // from "a bookmark moved it", and the summary reports both.
  g_cameraDirector.applyPose(pose, source);
}

const char* cameraModeToken(tf2::native::CameraMode mode) {
  return tf2::native::cameraModeName(mode);
}

// Saves the camera under `slot` with `name` (empty means the default name).
// Reads the pose from the renderer, because the renderer is what the last frame
// was drawn from.
bool saveCameraBookmark(std::size_t slot, const std::string& name) {
  if (!g_renderer) return false;
  syncRendererPoseToDirector();
  tf2::native::CameraPose pose = g_cameraDirector.pose();
  if (!pose.valid) return false;
  // The fov the frame behind this save was drawn through. Read from the same
  // reading the orbit fields come from, so a bookmark is the whole camera.
  const auto reading = g_renderer->orbitPose();
  const float fov = reading.valid ? reading.fovDegrees : tf2::native::kDefaultBookmarkFov;
  const bool ok = g_cameraBookmarks.save(slot, name, pose,
    cameraModeToken(g_cameraMode.mode()), g_playback.tick, fov);
  if (ok && g_renderer) {
    // The renderer keeps its own transient presets (F1..F8 via the old Ctrl
    // arms). Kept in step so an old binding path and the new bookmark store
    // cannot disagree about where slot n is.
    g_renderer->saveCameraPreset(slot - 1);
  }
  return ok;
}

// Jumps to `slot`. The pose is applied through setOrbitPose, which is the same
// clamp path a preset takes, and the camera mode saved with the bookmark is
// restored so the jump lands on the view the user saved.
bool jumpToCameraBookmark(std::size_t slot, std::string& reason) {
  if (!g_cameraBookmarks.saved(slot)) { reason = "empty"; return false; }
  const auto& bookmark = g_cameraBookmarks.at(slot);
  if (!bookmark.pose.valid) { reason = "no-pose"; return false; }
  if (!g_renderer) { reason = "no-renderer"; return false; }
  if (!g_renderer->setOrbitPose(bookmark.pose.focusX, bookmark.pose.focusY,
      bookmark.pose.focusZ, bookmark.pose.yaw, bookmark.pose.pitch,
      bookmark.pose.distance)) {
    reason = "rejected";
    return false;
  }
  // The projection is part of the saved view: an orbit restored into a
  // different FOV is a different frame, so the jump writes it back too.
  g_renderer->setViewModelFov(bookmark.fov);
  // A jump leaves the demo eye: the pose IS the new camera, and a demo view
  // left armed would overwrite it on the next scene tick.
  g_renderer->clearObserverDemoView();
  syncRendererPoseToDirector(tf2::native::CameraMoveSource::Bookmark);
  if (!bookmark.mode.empty()) {
    if (bookmark.mode == "first-person") g_cameraMode.setMode(tf2::native::CameraMode::FirstPerson);
    else if (bookmark.mode == "third-person") g_cameraMode.setMode(tf2::native::CameraMode::ThirdPerson);
    else if (bookmark.mode == "free-look") g_cameraMode.setMode(tf2::native::CameraMode::FreeLook);
    g_lastAppliedCameraMode = g_cameraMode.mode();
  }
  g_cameraBookmarks.noteJump();
  reason = "ok";
  return true;
}

}  // namespace tf2::native::main_internal
