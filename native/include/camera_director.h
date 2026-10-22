#pragma once

#include <cstdint>
#include <string>

namespace tf2::native {

// ---------------------------------------------------------------------------
// Camera director (T50).
//
// The task this file answers is one sentence: "pause must freeze demo time
// while camera motion continues". Two halves of the product own those two
// clocks and, before this file, both were spelled inline in the main loop:
// the tick advanced iff enabled && !paused, and the camera moved whenever a
// mouse message arrived. Neither half was a reading, so "pause is frozen" could
// only ever be asserted by a human watching the title.
//
// The director is the single place that decides both:
//   * demoClockAdvances(paused) -- the whole freeze rule, one expression;
//   * noteDemoTick(tick) -- records what the loop actually did and counts the
//     frames where the rule was violated. A mutation that removes the paused
//     term from the loop moves that counter, which is what makes the freeze
//     assertion able to go red rather than merely able to pass.
//   * the camera operations, which are deliberately NOT gated on paused: an
//     orbit, a zoom, a free-fly step and a bookmark jump all apply while the
//     demo clock stands still. cameraMovesWhilePaused is the reading that says
//     so.
// ---------------------------------------------------------------------------

// A camera pose in the renderer's own units: the orbit focus in the normalized
// world box, the orbit angles in radians and the distance in box units. Stored
// as floats because that is what Renderer::saveCameraPreset stores; the
// round-trip through the settings file is bit-exact by construction (see
// formatPose / parsePose below), which is what "exact pose restoration" means
// here -- not "close enough to look the same".
struct CameraPose {
  float focusX = 0.0f;
  float focusY = 0.0f;
  float focusZ = 0.5f;
  float yaw = 0.0f;
  float pitch = 0.55f;
  float distance = 3.0f;
  bool valid = false;
};

// Numeric comparison. Deliberately not an epsilon: a bookmark that comes back
// 1e-6 away from where it was saved would still produce a slightly different
// frame, and "the pose was restored" is a claim about the frame.
bool poseEqual(const CameraPose& a, const CameraPose& b);

// "x,y,z,yaw,pitch,distance" with 9 significant digits, the shortest form that
// round-trips a float exactly (FLT_DECIMAL_DIG == 9). Written without a locale
// dependence so the file is the same on every machine.
std::string formatPose(const CameraPose& pose);
// One field, in the same format formatPose uses, for a file that stores the six
// components separately (contract 1.1).
std::string formatPoseField(float value);
bool parsePose(const std::string& text, CameraPose& pose);

// "focus=.. yaw=.. pitch=.. dist=.." -- the trace form, always the same four
// fields so a gate can cut one out.
std::string poseTraceFields(const CameraPose& pose);

enum class CameraMoveSource { Orbit, Zoom, FreeFly, Bookmark, Reset, Player };

class CameraDirector {
public:
  // ---- the demo clock -----------------------------------------------------
  // The freeze rule, stated once. Every caller that advances the playback tick
  // asks this first; a caller that does not is the defect the counter below
  // catches.
  static bool demoClockAdvances(bool paused) { return !paused; }

  // Called once per main-loop iteration with the tick the loop is about to use.
  // Returns true when this frame advanced the tick. While paused the tick must
  // not move: a paused frame whose tick differs from the previous one is counted
  // as a violation rather than silently accepted.
  bool noteDemoTick(bool paused, std::int32_t tick);

  std::uint32_t frozenDemoFrames() const { return frozenDemoFrames_; }
  std::uint32_t advancedDemoFrames() const { return advancedDemoFrames_; }
  std::uint32_t pauseViolations() const { return pauseViolations_; }
  std::int32_t lastTick() const { return lastTick_; }
  std::uint32_t tickReadings() const { return tickReadings_; }

  // ---- the camera ---------------------------------------------------------
  void applyPose(const CameraPose& pose, CameraMoveSource source);
  void orbit(float deltaX, float deltaY);
  void zoom(float delta);
  void setPaused(bool paused) { paused_ = paused; }
  bool paused() const { return paused_; }
  void setFreeFly(bool enabled);
  bool freeFly() const { return freeFly_; }
  // "Back to the player": the next scene tick with an observe target adopts the
  // demo eye. Latched rather than applied immediately, because the eye belongs
  // to the scene tick and applying it between ticks would be overwritten.
  void requestResetToPlayer();
  bool takeResetToPlayer();
  // "Reset the view": the orbit returns to the shipped defaults and the camera
  // returns to first-person. Bookmarks are NOT touched -- resetting the view is
  // not deleting the user's saved positions.
  void resetView();

  const CameraPose& pose() const { return pose_; }
  std::uint32_t cameraMoves() const { return cameraMoves_; }
  std::uint32_t cameraMovesWhilePaused() const { return cameraMovesWhilePaused_; }
  std::uint32_t freeFlySteps() const { return freeFlySteps_; }
  std::uint32_t bookmarkApplies() const { return bookmarkApplies_; }
  std::uint32_t resetToPlayerRequests() const { return resetToPlayerRequests_; }
  std::uint32_t resetToPlayerApplies() const { return resetToPlayerApplies_; }
  std::uint32_t resetViewCount() const { return resetViewCount_; }
  std::uint32_t poseChanges() const { return poseChanges_; }

private:
  void noteMove(CameraMoveSource source);

  bool paused_ = false;
  bool freeFly_ = false;
  bool resetToPlayerPending_ = false;
  CameraPose pose_{};
  std::int32_t lastTick_ = -1;
  std::uint32_t frozenDemoFrames_ = 0;
  std::uint32_t advancedDemoFrames_ = 0;
  std::uint32_t pauseViolations_ = 0;
  std::uint32_t tickReadings_ = 0;
  std::uint32_t cameraMoves_ = 0;
  std::uint32_t cameraMovesWhilePaused_ = 0;
  std::uint32_t freeFlySteps_ = 0;
  std::uint32_t bookmarkApplies_ = 0;
  std::uint32_t resetToPlayerRequests_ = 0;
  std::uint32_t resetToPlayerApplies_ = 0;
  std::uint32_t resetViewCount_ = 0;
  std::uint32_t poseChanges_ = 0;
};

// The shipped orbit defaults, spelled once. Renderer::resetCamera uses the same
// numbers; the director cannot call the renderer (it is the pure half), so the
// product asserts the two agree at the call site.
constexpr float kDefaultCameraYaw = 0.0f;
constexpr float kDefaultCameraPitch = 0.36f;
constexpr float kDefaultCameraDistance = 1.65f;
constexpr float kDefaultFocusZ = 0.25f;

} // namespace tf2::native
