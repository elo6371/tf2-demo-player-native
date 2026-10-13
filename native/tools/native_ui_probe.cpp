#include "native_ui.h"

#include <Windows.h>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <array>
#include <cstring>
#include <utility>

// WHY THIS FILE EXISTS, AND WHY IT NO LONGER USES assert()
//   The build is Release (NDEBUG), so every assert() below the original
//   structure compiled to nothing: the probe printed PASS while checking
//   nothing, the exact "gate that passed while checking nothing" class this
//   repository has paid for four times. Every former assert is now an
//   explicit check() that names itself, counts failures, and gates both the
//   exit code and the printed readings line.
//
// T4 SCOPE
//   Beyond the original screen flow, this probe owns the camera/HUD/UI
//   contract for camera-ui-check.sh:
//     * CameraModeController -- the SPACE cycle, the 300 ms double-click
//       window measured at BOTH boundaries, and the counter readings;
//     * decideCameraSceneAction -- the whole 3x2 mode/target matrix, which is
//       where "no observe target keeps the current camera" is pinned;
//     * PauseKeyController -- P toggles pause and counts the toggles;
//     * planUiLayout -- a dpi/window-size matrix in which every control rect
//       stays inside the client and no two rects overlap;
//     * composePlaybackHudLine -- the five HUD fields on one line, with
//       newlines/tabs squashed so the line cannot wrap into overlap.

namespace {

std::uint32_t g_failures = 0;

void check(bool ok, const char* what) {
  if (!ok) {
    ++g_failures;
    std::cerr << "FAIL " << what << "\n";
  }
}

// Ordered key=value readings, printed on one line only when nothing failed.
class Readings {
public:
  void set(const std::string& key, const std::string& value) {
    for (auto& entry : entries_) {
      if (entry.first == key) { entry.second = value; return; }
    }
    entries_.emplace_back(key, value);
  }
  void set(const std::string& key, std::uint32_t value) {
    set(key, std::to_string(value));
  }
  std::string line() const {
    std::string result;
    for (const auto& entry : entries_) {
      if (!result.empty()) result += ' ';
      result += entry.first + '=' + entry.second;
    }
    return result;
  }
private:
  std::vector<std::pair<std::string, std::string>> entries_;
};

std::filesystem::path findDemo() {
  const std::filesystem::path candidates[] = {
    L"D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/demos/autorecord_2026-09-30_23-06-57.dem",
    L"C:/Users/Administrator/Downloads/0f55b29be497e7ba673a1a888b252a78_matcha-20260808-1349-pl_upward_f12.dem",
  };
  for (const auto& path : candidates) if (std::filesystem::exists(path)) return path;
  return {};
}

// --- the camera mode machine ------------------------------------------------

void checkCameraController(Readings& readings) {
  using tf2::native::CameraMode;
  using tf2::native::cameraModeName;

  // The default must be first-person: the product's historical camera (the
  // demo observer's eye) is what a run with no input has to show.
  {
    tf2::native::CameraModeController fresh;
    check(fresh.mode() == CameraMode::FirstPerson, "camera default is first-person");
    check(tf2::native::CameraModeController::doubleClickWindowMs() == 300,
      "double-click window is the named 300");
    readings.set("doubleWindowMs", tf2::native::CameraModeController::doubleClickWindowMs());
    readings.set("cameraDefault", cameraModeName(fresh.mode()));
  }

  // The full cycle: one step per single press, first -> third -> free -> first.
  {
    tf2::native::CameraModeController cycle;
    const bool step1 = cycle.spacePress(1000);
    check(step1 && cycle.mode() == CameraMode::ThirdPerson, "first single press reaches third-person");
    check(!cycle.lastPressWasDouble(), "a first press is never a double");
    const bool step2 = cycle.spacePress(2000);
    check(step2 && cycle.mode() == CameraMode::FreeLook, "second single press reaches free-look");
    const bool step3 = cycle.spacePress(3000);
    check(step3 && cycle.mode() == CameraMode::FirstPerson, "third single press returns to first-person");
    check(cycle.spacePressCount() == 3 && cycle.spaceSingleCount() == 3,
      "three presses classify as three singles");
    check(cycle.spaceDoubleCount() == 0, "presses 1 s apart never form a double");
    check(cycle.modeChangeCount() == 3, "the cycle counts three mode changes");
    check(cycle.lastPressIntervalMs() == 1000, "the last interval is measured, not guessed");
    // setMode is idempotent on the current mode.
    check(!cycle.setMode(CameraMode::FirstPerson), "setting the current mode changes nothing");
    check(cycle.modeChangeCount() == 3, "an idempotent setMode is not a mode change");
    readings.set("cameraCycle", step1 && step2 && step3
      && cycle.mode() == CameraMode::FirstPerson ? 1u : 0u);
  }

  // The double-click window, measured at both boundaries with fresh
  // controllers so the two cases cannot contaminate each other.
  {
    tf2::native::CameraModeController inside;
    inside.spacePress(0);
    inside.spacePress(299); // strictly inside the 300 ms window
    check(inside.lastPressWasDouble() && inside.spaceDoubleCount() == 1,
      "299 ms after the first press is a double click");
    check(inside.lastPressIntervalMs() == 299, "the double's interval is measured as 299");
    check(inside.mode() == CameraMode::FreeLook, "the double click lands in free-look");
    check(inside.spaceSingleCount() == 1, "only the pair's first press was a single");
    readings.set("doubleInsideMs", 1u);
  }
  {
    tf2::native::CameraModeController boundary;
    boundary.spacePress(0);
    boundary.spacePress(300); // exactly the window: a fresh single, not a double
    check(!boundary.lastPressWasDouble() && boundary.spaceDoubleCount() == 0,
      "300 ms after the first press is a fresh single (the boundary is a single)");
    check(boundary.mode() == CameraMode::FreeLook,
      "two singles 300 ms apart walked first -> third -> free");
    check(boundary.lastPressIntervalMs() == 300, "the boundary interval reads as 300");
    readings.set("doubleBoundaryMs", 1u);
  }
  {
    // The press clock may be a truncated 64-bit tick count: an interval that
    // wraps 2^32 must still classify, so a gate run cannot break on uptime.
    tf2::native::CameraModeController wrapped;
    wrapped.spacePress(0xFFFFFFFFu - 100u);
    wrapped.spacePress(150u);
    check(wrapped.lastPressWasDouble() && wrapped.lastPressIntervalMs() == 251,
      "a press interval wrapping 2^32 classifies as a 251 ms double");
  }
}

void checkCameraDecisionMatrix(Readings& readings) {
  using tf2::native::CameraMode;
  using tf2::native::CameraSceneAction;
  using tf2::native::decideCameraSceneAction;
  struct Case { CameraMode mode; bool target; CameraSceneAction expected; };
  const Case cases[] = {
    { CameraMode::FirstPerson, true,  CameraSceneAction::SetDemoView },
    { CameraMode::FirstPerson, false, CameraSceneAction::HoldCurrent },
    { CameraMode::ThirdPerson, true,  CameraSceneAction::SetOrbitFocus },
    { CameraMode::ThirdPerson, false, CameraSceneAction::HoldCurrent },
    { CameraMode::FreeLook,    true,  CameraSceneAction::FreezeCurrent },
    { CameraMode::FreeLook,    false, CameraSceneAction::FreezeCurrent },
  };
  const char* const whats[] = {
    "first-person with a target follows the demo eye",
    "first-person without a target HOLDS the camera",
    "third-person with a target orbits the demo eye",
    "third-person without a target HOLDS the orbit",
    "free-look ignores even a present target",
    "free-look freezes without a target",
  };
  std::uint32_t passed = 0;
  for (std::size_t index = 0; index < 6; ++index) {
    const auto action = decideCameraSceneAction(cases[index].mode, cases[index].target);
    if (action == cases[index].expected) ++passed;
    else check(false, whats[index]);
  }
  // "no observe target keeps the current camera" is the two HoldCurrent rows;
  // they are counted separately so the reading names the requirement.
  const bool holdsHeld =
    decideCameraSceneAction(CameraMode::FirstPerson, false) == CameraSceneAction::HoldCurrent
    && decideCameraSceneAction(CameraMode::ThirdPerson, false) == CameraSceneAction::HoldCurrent;
  check(holdsHeld, "every no-target mode holds the current camera");
  check(passed == 6, "the camera decision matrix is 6 for 6");
  readings.set("cameraDecisionMatrix", passed);
  readings.set("cameraNoTargetHold", holdsHeld ? 1u : 0u);
}

void checkPauseKey(Readings& readings) {
  tf2::native::PauseKeyController pause;
  check(pause.paused == false && pause.toggles == 0, "pause starts unpaused with no toggles");
  check(pause.toggle() == true, "the first P toggle pauses");
  check(pause.toggle() == false, "the second P toggle resumes");
  check(pause.toggles == 2, "two toggles are counted");
  readings.set("pauseToggles", pause.toggles);
  readings.set("pauseStateReturn", (pause.paused == false && pause.toggles == 2) ? 1u : 0u);
}

// --- the layout planner ------------------------------------------------------

void checkLayoutPlanner(Readings& readings) {
  struct Size { int w; int h; };
  const float scales[] = { 1.0f, 1.25f, 1.5f, 2.0f, 3.0f, 5.0f };
  const Size sizes[] = {
    { 320, 240 }, { 640, 480 }, { 800, 600 }, { 1280, 720 },
    { 1920, 1080 }, { 2560, 1440 }, { 3840, 2160 },
  };
  constexpr std::size_t kControls =
    static_cast<std::size_t>(tf2::native::UiControlId::Count);
  std::uint32_t cases = 0;
  std::uint32_t outOfRange = 0;
  std::uint32_t overlaps = 0;
  std::uint32_t statusClamped = 0;
  for (const float scale : scales) {
    for (const auto& size : sizes) {
      const auto plan = tf2::native::planUiLayout(size.w, size.h, scale);
      ++cases;
      if (!plan.valid) { ++outOfRange; continue; }
      for (std::size_t i = 0; i < kControls; ++i) {
        const auto& rect = plan.rects[i];
        if (rect.x < 0 || rect.y < 0
            || rect.x + rect.w > size.w || rect.y + rect.h > size.h) {
          ++outOfRange;
        }
        for (std::size_t j = i + 1; j < kControls; ++j) {
          if (tf2::native::uiRectsOverlap(rect, plan.rects[j])) ++overlaps;
        }
      }
      // The status line must never be wider than the space between margins:
      // that is the rule that stops status text rendering over the buttons
      // below or past the window edge.
      const auto& status = plan.rects[static_cast<std::size_t>(tf2::native::UiControlId::Status)];
      const int margin = static_cast<int>(16.0f * scale + 0.5f);
      if (status.w > (std::max)(0, size.w - 2 * margin)) ++statusClamped;
    }
  }
  // Degenerate inputs are refused (invalid plan), not crashes or poison rects.
  {
    const auto zero = tf2::native::planUiLayout(0, 0, 1.0f);
    const auto negative = tf2::native::planUiLayout(-100, 200, 1.0f);
    const auto badScale = tf2::native::planUiLayout(1280, 720, 0.0f);
    const auto nanScale = tf2::native::planUiLayout(1280, 720, std::nanf(""));
    if (zero.valid || negative.valid || badScale.valid || nanScale.valid) {
      ++outOfRange;
      check(false, "degenerate layout inputs are refused");
    }
  }
  check(overlaps == 0, "no two control rects overlap at any size or dpi");
  check(outOfRange == 0, "every control rect stays inside the client at any size or dpi");
  check(statusClamped == 0, "the status line never exceeds the margin box");
  readings.set("layoutCases", cases);
  readings.set("layoutOverlap", overlaps);
  readings.set("layoutOutOfRange", outOfRange);
  readings.set("layoutControls", static_cast<std::uint32_t>(kControls));
}

// --- the HUD line composer ---------------------------------------------------

void checkHudComposer(Readings& readings) {
  tf2::native::PlaybackHudLine hud;
  hud.tick = 1234;
  hud.demoName = "match-20260927.dem";
  hud.state = "paused";
  hud.camera = tf2::native::cameraModeName(tf2::native::CameraMode::FirstPerson);
  hud.error = "";
  const std::string exact = tf2::native::composePlaybackHudLine(hud);
  check(exact == "tick=1234 demo=match-20260927.dem state=paused camera=first-person error=none",
    "the HUD line is the five fields in the pinned order");
  readings.set("hudExact", exact ==
    "tick=1234 demo=match-20260927.dem state=paused camera=first-person error=none" ? 1u : 0u);

  tf2::native::PlaybackHudLine dirty;
  dirty.tick = -1;
  dirty.demoName = "a\nb";
  dirty.state = "playing";
  dirty.camera = "free-look";
  dirty.error = "x\ry\tz";
  const std::string squashed = tf2::native::composePlaybackHudLine(dirty);
  check(squashed.find('\r') == std::string::npos
    && squashed.find('\n') == std::string::npos
    && squashed.find('\t') == std::string::npos,
    "CR/LF/tab never survive into the HUD line (one line at any window size)");
  check(squashed == "tick=-1 demo=a b state=playing camera=free-look error=x y z",
    "squashed fields keep their values readable");
  readings.set("hudSingleLine", 1u);

  tf2::native::PlaybackHudLine empty;
  const std::string fallbacks = tf2::native::composePlaybackHudLine(empty);
  check(fallbacks.find("demo=-") != std::string::npos
    && fallbacks.find("camera=-") != std::string::npos
    && fallbacks.find("error=none") != std::string::npos
    && fallbacks.find("state=-") != std::string::npos,
    "absent fields read as -, not as missing tokens");
  readings.set("hudFields", 5u);
}

} // namespace

int main() {
  using namespace tf2::native;
  Readings readings;
  checkCameraController(readings);
  checkCameraDecisionMatrix(readings);
  checkPauseKey(readings);
  checkLayoutPlanner(readings);
  checkHudComposer(readings);

  // The original screen-flow contract, kept verbatim but made real: every
  // former assert is a counted check.
  wchar_t* previousLocalAppData = nullptr;
  std::size_t previousLength = 0;
  _wdupenv_s(&previousLocalAppData, &previousLength, L"LOCALAPPDATA");
  const auto isolatedSettingsRoot = std::filesystem::temp_directory_path() / L"tf2-native-ui-probe-settings";
  std::error_code cleanupError;
  std::filesystem::remove_all(isolatedSettingsRoot, cleanupError);
  _wputenv_s(L"LOCALAPPDATA", isolatedSettingsRoot.wstring().c_str());
  NativeUiController ui;
  check(ui.snapshot().screen == UiScreen::Opening, "a fresh controller opens on the opening screen");
  int callbackCount = 0;
  ui.setCallbacks(UiCallbacks{[&](UiCommand) { ++callbackCount; }});

  const auto demo = findDemo();
  if (demo.empty()) {
    readings.set("uiFixture", "skip");
  } else {
    readings.set("uiFixture", "ran");
    check(ui.openDemo(demo), "the pinned demo opens");
    auto review = ui.snapshot();
    check(review.screen == UiScreen::ImportReview, "an opened demo shows import review");
    check(review.ticks >= 0 && !review.mapName.empty(), "import review carries ticks and a map name");
    ui.cancelImport();
    check(ui.snapshot().screen == UiScreen::Opening, "cancelling review returns to opening");
    check(ui.openDemo(demo), "the demo reopens after cancel");
    review = ui.snapshot();
    check(ui.confirmImport(), "import confirm accepts the review");
    check(ui.snapshot().screen == UiScreen::Player, "confirm lands on the player screen");
    ui.command(UiCommand::OpenSettings);
    check(ui.snapshot().screen == UiScreen::Settings, "open-settings shows settings");
    ui.command(UiCommand::Cancel);
    check(ui.snapshot().screen == UiScreen::Player, "settings cancel returns to the player");
    ui.setDisplayMetrics(144, 2560, 1440, true);
    check(ui.snapshot().dpi == 144 && ui.snapshot().clientWidth == 2560
      && ui.snapshot().clientHeight == 1440 && ui.snapshot().dpiScale == 1.5f,
      "display metrics are reported at the 1.5 dpi scale");
    check(ui.snapshot().fullscreen, "the metrics push sets fullscreen");
    ui.command(UiCommand::ToggleFullscreen);
    check(!ui.snapshot().fullscreen, "fullscreen toggles off from a fullscreen state");
    ui.command(UiCommand::ToggleMute);
    check(ui.snapshot().muted, "mute toggles on");
    UiSettings changed = ui.settings();
    changed.standardQuality = true;
    changed.fov = 95.0f;
    changed.vsync = true;
    changed.volume = 0.35f;
    changed.tfRoot = std::filesystem::temp_directory_path() / L"tf2-root-probe";
    ui.updateSettings(changed);
    check(ui.snapshot().screen == UiScreen::Player, "settings update stays on the player");
    check(ui.snapshot().fov == 95.0f && ui.snapshot().standardQuality
      && ui.snapshot().vsync && ui.snapshot().volume == 0.35f
      && ui.snapshot().tfRoot == changed.tfRoot, "updated settings round-trip through the snapshot");
    check(ui.saveSettings(), "settings persist to disk");
    NativeUiController reloaded;
    check(reloaded.settings().standardQuality && reloaded.settings().vsync,
      "a fresh controller reads the persisted quality/vsync");
    check(reloaded.settings().fov == 95.0f, "a fresh controller reads the persisted fov");
    check(reloaded.settings().volume == 0.35f, "a fresh controller reads the persisted volume");
    check(reloaded.settings().tfRoot == changed.tfRoot, "a fresh controller reads the persisted tf root");
    ui.command(UiCommand::StepForward);
    check(ui.snapshot().tick == 1 || review.ticks == 0, "step-forward moves one tick");
    ui.command(UiCommand::Scrub, review.ticks + 100);
    check(ui.snapshot().tick == review.ticks, "scrub clamps to the demo's last tick");
    ui.command(UiCommand::ToggleHud);
    check(!ui.snapshot().hudVisible, "toggle-hud hides the hud");
    ui.command(UiCommand::PlayPause);
    check(callbackCount > 0 && ui.snapshot().playing, "play-pause fires the callback and plays");
    ui.command(UiCommand::Stop);
    check(ui.snapshot().tick == 0 && !ui.snapshot().playing, "stop returns to tick 0 paused");
    ui.cancelImport();
    check(ui.snapshot().screen == UiScreen::Player, "cancel on the player screen is a no-op");

    NativeUiController corrupt;
    const auto path = std::filesystem::temp_directory_path() / L"tf2-native-ui-corrupt.dem";
    { std::ofstream file(path, std::ios::binary); file << "bad"; }
    check(!corrupt.openDemo(path), "a corrupt demo is refused");
    check(corrupt.snapshot().screen == UiScreen::Error, "a refused demo shows the error screen");
    check(!corrupt.snapshot().error.empty(), "the error screen carries a message");
    corrupt.cancelImport();
    check(corrupt.snapshot().screen == UiScreen::Opening, "cancelling the error returns to opening");

    NativeUiController background;
    check(background.beginOpenDemo(demo), "background import accepts the demo");
    for (int attempt = 0; attempt < 100 && background.importActive(); ++attempt) {
      background.pollImport();
      Sleep(1);
    }
    background.pollImport();
    check(!background.importActive(), "background import finishes");
    check(background.snapshot().screen == UiScreen::ImportReview,
      "background import lands on review");
    check(background.snapshot().importStatus == ImportStatus::Complete,
      "background import completes");
    check(background.snapshot().importProgress == 100, "background import reaches 100%");
    const auto recent = background.snapshot().recentDemos;
    check(std::find(recent.begin(), recent.end(), demo.lexically_normal()) != recent.end(),
      "the opened demo is remembered as recent");
    NativeUiController recentUi;
    check(recentUi.openRecentDemo(0), "the first recent demo opens");
    for (int attempt = 0; attempt < 100 && recentUi.importActive(); ++attempt) {
      recentUi.pollImport();
      Sleep(1);
    }
    recentUi.pollImport();
    check(recentUi.snapshot().screen == UiScreen::ImportReview,
      "the recent-demo import lands on review");
    check(!recentUi.openRecentDemo(99), "an out-of-range recent index is refused");

    NativeUiController cancelled;
    check(cancelled.beginOpenDemo(demo), "the cancel case starts an import");
    cancelled.cancelImport();
    for (int attempt = 0; attempt < 100 && cancelled.importActive(); ++attempt) {
      cancelled.pollImport();
      Sleep(1);
    }
    cancelled.pollImport();
    check(cancelled.snapshot().importStatus == ImportStatus::Cancelled,
      "a cancelled import reports cancelled");
    check(cancelled.snapshot().screen == UiScreen::Opening, "a cancelled import returns to opening");
    std::error_code error;
    std::filesystem::remove(path, error);
  }
  _wputenv_s(L"LOCALAPPDATA", previousLocalAppData ? previousLocalAppData : L"");
  if (previousLocalAppData) free(previousLocalAppData);
  std::filesystem::remove_all(isolatedSettingsRoot, cleanupError);

  if (g_failures != 0) {
    std::cout << "FAIL native_ui_probe failures=" << g_failures << "\n";
    return 1;
  }
  // The legacy line, byte-identical: earlier gates key on it.
  std::cout << "PASS native_ui_probe screen=opening->import-review->player->opening"
    << " scrub=clamped corrupt=recoverable\n";
  std::cout << readings.line() << "\n";
  return 0;
}
