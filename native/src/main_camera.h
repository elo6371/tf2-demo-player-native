#pragma once

// T76 (2026-10-30): declarations for the T50 camera pose/bookmark layer that
// used to sit inside main.cpp's anonymous namespace (main.cpp:436-536 before
// the move).
//
// WHAT MOVED: the five pose/bookmark functions and the four state objects they
// own (the mode controller, the last applied mode, the director and the
// bookmark store).
//
// WHAT DID NOT MOVE: handleCameraAction, dispatchHotkey, the key table
// (g_keyBinds), the text-field focus flag, the T71/T72 review ports and the
// camera-script driver. They call the functions here through this header and
// keep their own gate anchors in main.cpp.
//
// g_renderer is declared here as well as in main_hud.h: both modules read it and
// a second declaration of the same object is not a second object.
//
// Not a public header: nothing in it is exported, no other target includes it,
// and no gate parses it.

#include <cstddef>
#include <cstdint>
#include <string>

#include "camera_bookmarks.h"
#include "camera_director.h"
#include "native_renderer.h"
#include "native_ui.h"

namespace tf2::native::main_internal {

// ---------------------------------------------------------------- camera state
extern tf2::native::Renderer* g_renderer;
extern tf2::native::CameraModeController g_cameraMode;
extern tf2::native::CameraMode g_lastAppliedCameraMode;
extern tf2::native::CameraDirector g_cameraDirector;
extern tf2::native::CameraBookmarkStore g_cameraBookmarks;

// --------------------------------------------------------------- camera logic
void syncDirectorPoseToRenderer(tf2::native::CameraMoveSource source);

// The default argument is the T50 contract (a scene-tick read-back is a Player
// move); it lives here because the callers in main.cpp rely on it.
void syncRendererPoseToDirector(
    tf2::native::CameraMoveSource source = tf2::native::CameraMoveSource::Player);

const char* cameraModeToken(tf2::native::CameraMode mode);
bool saveCameraBookmark(std::size_t slot, const std::string& name);
bool jumpToCameraBookmark(std::size_t slot, std::string& reason);

}  // namespace tf2::native::main_internal
