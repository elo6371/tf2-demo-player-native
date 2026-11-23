#pragma once

#include "demo_header.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace tf2::native {

enum class UiScreen { Opening, ImportReview, Player, Settings, Error };
enum class ImportStatus { Idle, ReadingHeader, Indexing, Complete, CancelRequested, Cancelled, Failed };

// ---------------------------------------------------------------------------
// Camera mode machine (T4).
//
// Three camera states, switched with SPACE the way TF2's own demo UI does
// (I1, 2026-11-23, user decision; docs/UI-SHELL-DESIGN-2026-11-22.md section 8):
//   first-person  the camera sits at the demo observer's eye and looks along
//                 the demo's own angles (the product's historical behaviour);
//   third-person  an orbit camera that keeps looking at the observer's eye
//                 position, at the player's chosen orbit distance;
//   free-look     an orbit camera nothing feeds anymore: it stays exactly
//                 where the user's right-drag orbit left it.
//
// Key contract, measurable because both halves live here rather than in the
// window procedure:
//   * a single SPACE press toggles first-person <-> third-person, and from
//     free-look it goes back to the player (first-person). There is no longer
//     a three-step loop: free-look is reached only by the double press below
//     (or the free-fly action), and one press always gets the player back.
//     SPACE does not toggle pause -- P owns pause, see PauseKeyController;
//   * a second press inside kDoubleClickWindowMs of the previous one is a
//     double click and jumps straight to free-look, so the window is a named
//     constant a probe can measure at both boundaries rather than a timing
//     accident. The first press of the pair has already acted (first -> third),
//     so a double from first-person shows third-person for up to 300 ms before
//     free-look -- accepted on 2026-11-23 over delaying every single press;
//   * a press with no observe target never moves the camera -- the per-tick
//     decision is decideCameraSceneAction below.
enum class CameraMode { FirstPerson = 0, ThirdPerson, FreeLook };

const char* cameraModeName(CameraMode mode);

class CameraModeController {
public:
  // 300 ms: the classic Windows double-click time. Named here so a gate can
  // measure the boundary rather than infer it from behaviour.
  static constexpr std::uint32_t kDoubleClickWindowMs = 300;

  CameraModeController() = default;

  CameraMode mode() const { return mode_; }
  static constexpr std::uint32_t doubleClickWindowMs() { return kDoubleClickWindowMs; }

  // Explicit jump, e.g. a UI button. Returns true when the mode changed.
  bool setMode(CameraMode mode);
  // What one single SPACE press does: first <-> third, free -> first.
  // (The name is historical; it is no longer a three-step cycle.) Returns true
  // when the mode changed.
  bool cycleMode();
  // One SPACE edge press at wall-clock nowMs. Returns true when the mode
  // changed. Intervals are computed modulo 2^32, so GetTickCount64 truncated
  // to 32 bits is a valid clock here.
  bool spacePress(std::uint32_t nowMs);

  // Readings. `lastPressIntervalMs` is 0xFFFFFFFF until a second press exists,
  // so "no interval yet" cannot collide with a real interval.
  std::uint32_t spacePressCount() const { return spacePresses_; }
  std::uint32_t spaceSingleCount() const { return spaceSingles_; }
  std::uint32_t spaceDoubleCount() const { return spaceDoubles_; }
  std::uint32_t modeChangeCount() const { return modeChanges_; }
  std::uint32_t lastPressIntervalMs() const { return lastIntervalMs_; }
  bool lastPressWasDouble() const { return lastWasDouble_; }

private:
  CameraMode mode_ = CameraMode::FirstPerson;
  bool haveLastPress_ = false;
  std::uint32_t lastPressMs_ = 0;
  std::uint32_t lastIntervalMs_ = 0xFFFFFFFFu;
  bool lastWasDouble_ = false;
  std::uint32_t spacePresses_ = 0;
  std::uint32_t spaceSingles_ = 0;
  std::uint32_t spaceDoubles_ = 0;
  std::uint32_t modeChanges_ = 0;
};

// What one scene tick should do to the renderer's camera, given the mode and
// whether this tick actually has an observe target (a demo view sample with
// origin and angles). Pure, so the probe can walk the whole 3x2 matrix:
//   first-person + target -> SetDemoView    (follow the demo eye)
//   first-person + no     -> HoldCurrent    (keep the camera; never teleport)
//   third-person + target -> SetOrbitFocus  (orbit around the demo eye)
//   third-person + no     -> HoldCurrent    (keep the orbit where it is)
//   free-look    + any    -> FreezeCurrent  (feed nothing; the orbit stands)
enum class CameraSceneAction { SetDemoView, SetOrbitFocus, HoldCurrent, FreezeCurrent };

CameraSceneAction decideCameraSceneAction(CameraMode mode, bool haveObserverView);

// T80: is this demo eye a camera at all?
//
// A SourceTV recording whose svc_SetView names the recorder itself reports an
// eye of exactly (0,0,0) on every scene tick -- measured on the KOTH ashville
// SourceTV sample: 3312 of 3312 camera lines said eye=0.0,0.0,0.0. Adopting
// that is how the picture gets rendered from a point no player can stand at,
// because every TF2 map puts its origin inside the geometry.
//
// The test is deliberately narrow: non-finite or all-zero is refused, nothing
// else. A bounds check against the loaded map would make the camera depend on
// which map is open rather than on what the demo said, and a demo that names a
// real eye just outside the current map's box is still a demo that named an eye.
bool observerEyeUsable(float x, float y, float z);

// The P-key pause contract, kept next to the camera controller so one probe
// covers both keys the player presses. The counter exists so a trace can
// report "pause was touched" even when the run never pressed P.
class PauseKeyController {
public:
  bool paused = false;
  std::uint32_t toggles = 0;
  // Returns the new paused state.
  bool toggle() {
    paused = !paused;
    ++toggles;
    return paused;
  }
};

// ---------------------------------------------------------------------------
// UI layout planner (T4).
//
// The five control rows used to sit at hard-coded pixel offsets sized for a
// 1280x720 window at 96 dpi: a smaller client left the status text hanging
// past the window edge and a different dpi left everything unscaled. The
// planner owns the arithmetic instead, pure and probe-checkable: one plan per
// (client size, dpi scale), every rect inside the client, no two rects
// overlapping, and the status / timeline rows clamped to the window so text
// can only ever clip at its own control's edge, never overlap another control.
enum class UiControlId {
  Status = 0, Open, Cancel, Confirm, Play, Stop, Back, Forward, Reverse,
  Timeline,
  // T50: the camera controls, reserved immediately after Timeline in the order
  // native/docs/camera-ui-contract.md section 3 pins. T51 appends its own ids
  // after `Count` and extends the planner with its own function, so the two
  // features cannot move each other's rects. Kept inside the enum (rather than
  // a second enum) because planUiLayout returns one rect array and a split
  // would need two containment proofs.
  CameraMode, FreeFly, ResetToPlayer, ResetView,
  BookmarkSave, BookmarkNameEdit, BookmarkDelete, BookmarkList, KeybindReset,
  Count,
};

struct UiRect {
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;
};

struct UiLayoutPlan {
  UiRect rects[static_cast<std::size_t>(UiControlId::Count)]{};
  bool valid = false;
};

// clientWidth/clientHeight are physical pixels; dpiScale is windowDpi / 96.
// Never returns an invalid plan for positive sizes: clamping, not refusing, is
// the point -- a 200x150 window still lays out, it just clips.
UiLayoutPlan planUiLayout(int clientWidth, int clientHeight, float dpiScale);

// Axis-aligned overlap test, exposed so the probe and the planner agree on
// what "overlapping" means (sharing edge pixels counts as overlapping).
bool uiRectsOverlap(const UiRect& a, const UiRect& b);

// ---------------------------------------------------------------------------
// HUD line composer (T4).
//
// The playback HUD is the window title. This assembles the one line a gate
// parses: tick, demo name, playback state, camera mode, error status. The
// contract that keeps the text readable at any window size: exactly one line
// -- every CR/LF/tab in user-controlled strings (a demo file name, an error
// message) is squashed to a space -- and the five fields always present, so a
// field that goes missing reads as a gate failure rather than as whitespace.
struct PlaybackHudLine {
  std::int32_t tick = 0;
  std::string demoName;  // demo file name (no directory), "-" when absent
  std::string state;     // "playing" | "paused" | "reverse"
  std::string camera;    // cameraModeName(CameraMode), "-" when no demo
  std::string error;     // error text, "none" when empty
};

// Returns "tick=<n> demo=<demoName> state=<state> camera=<camera> error=<error|none>".
std::string composePlaybackHudLine(const PlaybackHudLine& fields);

enum class UiCommand {
  Open,
  Cancel,
  ConfirmImport,
  PlayPause,
  Stop,
  StepBackward,
  StepForward,
  StepSecondBackward,
  StepSecondForward,
  Reverse,
  Scrub,
  ToggleHud,
  ToggleMute,
  ToggleFullscreen,
  ResetCamera,
  OpenSettings,
  OpenDiagnostics,
  Export,
};

struct UiSettings {
  bool standardQuality = false;
  float fov = 90.0f;
  bool vsync = false;
  float volume = 1.0f;
  std::filesystem::path tfRoot;
  std::vector<std::filesystem::path> recentDemos;
};

// I3 (2026-11-23, docs/UI-SHELL-DESIGN-2026-11-22.md section 9.4 A2/A3): what the
// analysis screen shows about the recording itself. The detector lives in
// demo_motion_detect.h and produces this; the UI library deliberately does not include
// that header -- the product converts, so the shell keeps no dependency on the audit
// internals and a probe can build a report by hand.
struct UiMotionAnomaly {
  int kind = 0;  // 0 stall, 1 teleport, 2 sharpTurn (matches MotionAnomalyKind)
  std::int32_t startTick = 0;
  std::int32_t endTick = 0;
  std::int32_t durationTicks = 0;
  std::int32_t entity = -1;
  double metric = 0.0;
};

struct UiMotionAnalysis {
  bool valid = false;  // false until a scan ran; the screen then says why it is empty
  std::size_t stalls = 0;
  std::size_t teleports = 0;
  std::size_t sharpTurns = 0;
  std::size_t sceneResets = 0;
  std::size_t teleportsOmitted = 0;
  std::vector<UiMotionAnomaly> anomalies;  // stalls + teleports, tick order
  std::vector<UiMotionAnomaly> hints;      // sharp turns, tick order
};

// I3b (2026-11-24, design section 9.1): the analysis screen's result list.
//
// WHY A PROJECTION AND NOT A SECOND ANALYSIS
//   The detector already returns anomalies with a tick range and the entity each one
//   belongs to. The list is that data plus the two decisions the screen makes and the
//   detector must not: where a click lands (kResultSeekLeadTicks before the anomaly
//   starts) and whether the row also switches who the camera follows -- a teleport or
//   a sharp turn happened TO somebody, a stall belongs to the recording. Keeping both
//   here means the product and the probe agree on the arithmetic instead of each
//   re-deriving it, and an out-of-range click is refused rather than clamped.
constexpr std::int32_t kResultSeekLeadTicks = 200;  // 3 s at 66 tick/s
// The tick rate the list converts durations with. 66 is the demo's rate (the same one
// the design's "200 ticks = 3 s" assumes); a recording at another rate would render a
// slightly wrong duration, not a wrong seek -- the seek is in ticks.
constexpr std::int32_t kResultTickRate = 66;

struct UiResultRow {
  int index = 0;                    // 0-based position in the list; the click payload
  int kind = 0;                     // 0 stall, 1 teleport, 2 sharpTurn
  std::int32_t startTick = 0;
  std::int32_t endTick = 0;
  std::int32_t durationTicks = 0;
  std::int32_t entity = -1;
  double metric = 0.0;
  std::int32_t seekTick = 0;        // where a click lands (never negative)
  bool switchesPlayer = false;      // teleport or sharp turn: follow that entity
};

const char* uiMotionAnomalyKindName(int kind);
// startTick - kResultSeekLeadTicks, clamped at zero: a row that starts before tick
// 200 lands on tick 0 rather than on a negative tick the transport would refuse.
std::int32_t analysisResultSeekTick(std::int32_t startTick);
// One list, sorted by start tick: a hint that happens before an anomaly is listed
// before it, because the screen draws a timeline and not two lists. Index is assigned
// after the sort, so a click's payload is the row's visible position.
std::vector<UiResultRow> buildResultRows(const UiMotionAnalysis& analysis);
// "result: row index=2 start=12000 end=12040 ticks=40 seconds=0.6 kind=teleport entity=13 seek=11800"
std::string formatResultRowLine(const UiResultRow& row, std::int32_t tickRate);
// One row as the list control shows it: kind, tick range, duration, entity, and where
// a click lands. ASCII on purpose -- the control is created with CreateWindowExW while
// the shell converts std::string byte-wide everywhere else, so a non-ASCII byte here
// would render as a replacement glyph instead of as text.
std::string formatResultRowLabel(const UiResultRow& row, std::int32_t tickRate);

struct UiResultActivation {
  bool valid = false;
  UiResultRow row;
};
// An out-of-range index is refused, not clamped: a click that addresses nothing must
// not silently jump to the first row.
UiResultActivation activateResultRow(const std::vector<UiResultRow>& rows, std::size_t index);

struct UiSnapshot {
  UiScreen screen = UiScreen::Opening;
  std::filesystem::path demoPath;
  std::string mapName;
  std::string recordingType;
  std::int32_t ticks = 0;
  float durationSeconds = 0.0f;
  std::int32_t tick = 0;
  bool playing = false;
  bool reverse = false;
  bool hudVisible = true;
  bool muted = false;
  bool fullscreen = false;
  bool bspAvailable = false;
  bool tfRootAvailable = false;
  std::uint32_t missingResourceCount = 0;
  std::string error;
  double speed = 1.0;
  float fov = 90.0f;
  bool standardQuality = false;
  bool vsync = false;
  float volume = 1.0f;
  bool exportAvailable = false;
  ImportStatus importStatus = ImportStatus::Idle;
  unsigned importProgress = 0;
  UiMotionAnalysis motionAnalysis;
  unsigned dpi = 96;
  unsigned clientWidth = 1280;
  unsigned clientHeight = 720;
  float dpiScale = 1.0f;
  std::vector<std::filesystem::path> recentDemos;
  std::filesystem::path tfRoot;
};

struct UiCallbacks {
  std::function<void(UiCommand)> command;
};

class NativeUiController {
public:
  NativeUiController();
  ~NativeUiController();
  NativeUiController(const NativeUiController&) = delete;
  NativeUiController& operator=(const NativeUiController&) = delete;

  void setCallbacks(UiCallbacks callbacks);
  void setSettings(const UiSettings& settings);
  const UiSettings& settings() const { return settings_; }
  void setTfRoot(const std::filesystem::path& root);

  // AUD-03: the audio path reads the master volume and the mute flag from here,
  // and the product's volume keys write them. Deliberately not a new
  // UiControlId: planUiLayout's rectangles are pinned by camera-ui-check and by
  // the camera/UI contract, so a new control would move a rectangle a gate
  // asserts. setVolume() clamps for the same reason setSettings() does.
  float volume() const { return settings_.volume; }
  bool muted() const { return muted_; }
  void setVolume(float volume) {
    settings_.volume = volume < 0.0f ? 0.0f : (volume > 1.0f ? 1.0f : volume);
  }
  void toggleMute() { muted_ = !muted_; }

  bool openDemo(const std::filesystem::path& path);
  bool openRecentDemo(std::size_t index);
  bool beginOpenDemo(const std::filesystem::path& path);
  void pollImport();
  bool importActive() const;
  bool dropDemo(const std::filesystem::path& path) { return openDemo(path); }
  bool confirmImport();
  void cancelImport();
  void clearError();
  void command(UiCommand value, std::int32_t tick = 0);
  void setPlaybackState(std::int32_t tick, bool playing, bool reverse, double speed);
  void updateSettings(const UiSettings& settings);
  bool saveSettings() const;
  void setDisplayMetrics(unsigned dpi, unsigned clientWidth, unsigned clientHeight,
      bool fullscreen);
  // I3: the analysis screen's numbers. Set once per load by the product, which owns the
  // scan; the shell only renders it, so the UI never decides what counts as an anomaly.
  void setMotionAnalysis(const UiMotionAnalysis& analysis);

  UiSnapshot snapshot() const;

private:
  void refreshResourceState();
  void setError(std::string message);
  bool hasLoadedDemo() const;
  void rememberDemo(const std::filesystem::path& path);
  void finishImportIfReady();

  struct ImportResult {
    bool ready = false;
    DemoHeader header;
    DemoIndex index;
    std::string error;
  };

  UiSettings settings_{};
  UiCallbacks callbacks_{};
  UiScreen screen_ = UiScreen::Opening;
  UiScreen settingsReturnScreen_ = UiScreen::Opening;
  std::filesystem::path demoPath_;
  DemoHeader header_{};
  DemoIndex index_{};
  bool playing_ = false;
  bool reverse_ = false;
  bool hudVisible_ = true;
  bool muted_ = false;
  bool fullscreen_ = false;
  std::int32_t tick_ = 0;
  double speed_ = 1.0;
  bool bspAvailable_ = false;
  bool tfRootAvailable_ = false;
  std::uint32_t missingResourceCount_ = 0;
  std::string error_;
  UiMotionAnalysis motionAnalysis_{};
  ImportStatus importStatus_ = ImportStatus::Idle;
  std::atomic<unsigned> importProgress_{0};
  std::atomic<bool> cancelImportRequested_{false};
  std::atomic<bool> importWorkerDone_{true};
  mutable std::mutex importMutex_;
  ImportResult importResult_{};
  std::thread importWorker_;
  unsigned dpi_ = 96;
  unsigned clientWidth_ = 1280;
  unsigned clientHeight_ = 720;
};

// T51: the ui.ini override, mirroring settings_store.h's setSettingsFileOverride.
//
// WHY THIS EXISTS
//   The [hud] section lives in ui.ini, and a gate that writes it would be editing
//   the machine's real settings -- so a green run would depend on, and destroy,
//   whatever the tester had configured. settings_store.h already solved exactly
//   this for settings.ini; this is the same rule for the other file, in the file
//   that owns the path. Must be called before the controller is constructed.
void setUiSettingsFileOverride(const std::filesystem::path& file);
std::filesystem::path uiSettingsPath();

const char* uiScreenName(UiScreen screen);

// ---------------------------------------------------------------------------
// T51: the custom HUD controls (contract addendum, agreed with T50 2026-10-22).
//
// WHY A SEPARATE ENUM RATHER THAN MORE UiControlId VALUES
//   T50 reserves the ids immediately after Timeline and owns every rect
//   planUiLayout produces. Extending that enum from here would put both teams in
//   one hunk -- and a merge that dropped one side has ids would move every rect
//   silently. So the HUD owns its own id set and its own planner, which CONSUMES
//   the base plan instead of editing it: the base rects are read, never written.
// ---------------------------------------------------------------------------
enum class UiHudControlId { Import = 0, Preview, Scale, Reset, Status, Count };

struct UiHudLayoutPlan {
  UiRect rects[static_cast<std::size_t>(UiHudControlId::Count)]{};
  bool valid = false;
};

// Places one row of HUD controls immediately below the base plan lowest row,
// sharing the base planner margin, gap and dpi rule. Every rect is inside the
// client and inside the row; the row itself may fall past the bottom on a small
// window, in which case its rects collapse to the client bottom edge exactly
// as the base planner collapses its own (containment, not refusal, is the rule).
UiHudLayoutPlan planHudLayout(const UiLayoutPlan& base, int clientWidth,
  int clientHeight, float dpiScale);

// ---------------------------------------------------------------------------
// I3c (2026-11-24, design section 9.1): the analysis result list on screen.
//
// WHY ANOTHER ID SET AND ANOTHER PLANNER
//   T51's reason, one level down: the list must sit below BOTH the base plan and
//   the HUD row, and it is the only control on the review screen that is not a
//   button. Consuming both plans means a later base row or HUD row cannot push it
//   under a control -- the list follows whatever is lowest, never a constant.
// ---------------------------------------------------------------------------
enum class UiAnalysisControlId { Results = 0, Count };

struct UiAnalysisLayoutPlan {
  UiRect rects[static_cast<std::size_t>(UiAnalysisControlId::Count)]{};
  bool valid = false;
};

// Fills the space below the lowest control with the results list, sharing the base
// margin and dpi rule. Containment, not refusal: on a short window the list collapses
// to zero height at the bottom edge exactly as the base planner collapses its rows.
UiAnalysisLayoutPlan planAnalysisLayout(const UiLayoutPlan& base,
  const UiHudLayoutPlan& hud, int clientWidth, int clientHeight, float dpiScale);

// The HUD commands. Kept beside the existing UiCommand rather than added to it,
// for the same reason as the ids: one enum, one owner.
enum class UiHudCommand { ImportFolder, ImportFile, TogglePreview, SetScale, Reset };


} // namespace tf2::native
