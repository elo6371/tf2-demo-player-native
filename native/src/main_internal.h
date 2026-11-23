#pragma once

// T76 (2026-10-29): declarations for the code split out of main.cpp.
//
// WHY THIS HEADER EXISTS
//   main.cpp was 11,291 lines and every one of its helpers lived in one
//   anonymous namespace: internal linkage, no declaration anywhere, no way to
//   move a function out without re-typing its signature in the caller. T76 moves
//   the low-risk, self-contained pieces into their own translation units; the
//   declarations they need in order to stay callable from main.cpp are here.
//
// WHAT THIS IS NOT
//   Not a public header, not a contract, not included by any other target.
//   Nothing in it is exported and no gate parses it. The product's external
//   contract -- command line switches, trace text, exit codes -- is unchanged by
//   the move and is asserted by the gates, not by this file.
//
// WHY THE CALL SITES KEEP THEIR OLD SPELLING
//   main.cpp includes this header and adds one `using namespace
//   tf2::native::main_internal;` after its includes. That is the whole change on
//   the calling side: a call written `traceFmt("mode: summary ...")` compiles
//   exactly as before. Renaming every call site would have made the move
//   unprovable as pure motion and produced a larger diff than the code moved.

#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "demo_header.h"
#include "keybinds.h"
#include "native_ui.h"
#include "install_lifecycle.h"

namespace tf2::native {
class ReviewSession;
class Renderer;
struct AssetRoot;
struct PersistentSettings;
}  // namespace tf2::native

namespace tf2::native::main_internal {

// ------------------------------------------------------------------- trace
// TEMP DIAGNOSTIC -- entity-material wiring regression, 2026-10-09 (moved out
// of main.cpp:236-318). Nothing opens the trace file unless TF2_NATIVE_TRACE
// names one, so every other run is byte-for-byte unaffected.
double traceNowMs();
void traceCheckpoint(const char* what);
void traceFmt(const char* format, ...);
void openTrace();

// ------------------------------------------------ renderer self-test (N24 P1)
// Moved out of main.cpp by N24 phase 1 (2026-11-19) as pure code motion; the
// trace text, the --renderer-resize-selftest switch and the exit codes
// (0 pass, 14 initialize failed, 15 checks failed) are unchanged.
int runRendererSelftest(tf2::native::Renderer& renderer, HWND window, bool faultSpecIgnored);

// --------------------------------------------- shutdown persistence (N24 B2-1)
// Moved out of wWinMain (main.cpp:15997-16065 as HEAD 54d7f3e7 had it) as pure
// code motion: the bookmarks/keybinds save, renderer.shutdown(), the logs panel's
// last frame, saveSettings(), the T51 hud: keys reading, the [hud] save/summary
// and the review session's finish/reset, in the order the exit path always ran
// them. g_renderer = nullptr and the exit code deliberately stay in wWinMain.
//
// The five globals the tail names -- g_cameraBookmarks, g_hud, g_reviewSession,
// g_keyBinds and g_keybinds -- stay globals: main_shutdown.cpp declares them
// through the same headers main.cpp uses, so no second copy exists. renderer,
// persistent and assets are wWinMain locals and travel by reference, as do the
// three HUD switch values -- the tail only reads them, and they outlive the call.
struct ShutdownPersistenceInputs {
  tf2::native::Renderer& renderer;
  tf2::native::PersistentSettings& persistent;
  const tf2::native::AssetRoot& assets;
  bool hudConfigRequested = false;
  const std::filesystem::path& hudRoot;
  const std::string& hudLayout;
};
void runShutdownPersistence(ShutdownPersistenceInputs& inputs);

// ----------------------------------------------------- installer lifecycle
// T37 (moved out of main.cpp:72-151): four modes, not "a playback run with a
// flag": each answers a question and exits. They are recognised before the
// settings file is read and before the window is created, which is what makes
// --version usable from a script and keeps an uninstall from depending on a
// working GPU, a demo or a saved root.
enum class LifecycleMode { None, Version, Install, Uninstall, Status };

struct LifecycleRequest {
  LifecycleMode mode = LifecycleMode::None;
  tf2::native::installer::Options options;
  bool directoryGiven = false;
  // Set when `--install-dry-run` was given without `--install` and with another
  // explicit mode beside it: the combination has no meaning, and answering it
  // with a product launch or with a silent "the other mode wins" hides it.
  bool dryRunConflict = false;
};

bool parseLifecycleRequest(int argumentCount, wchar_t** arguments, LifecycleRequest* request);
std::filesystem::path defaultInstallDirectory();
int runLifecycle(LifecycleRequest& request);

// --------------------------------------------------- file, path, byte helpers
// T76 stage 2 (moved out of main.cpp:179-2311 as a pure move). Seven functions
// with no captured state and no globals: the ones the camera script, the
// argument scan and the frame writers call when they need a file read, a hash,
// a normalized model path or the per-user cache directory.
//
// narrowAscii is the logs-panel URL policy's conversion: bytes in, bytes out,
// and a non-ASCII character becomes 0x7F so the policy refuses the argument
// instead of a '?' quietly passing it.
std::string narrowAscii(const wchar_t* value);
std::string normalizedModelPath(const std::string& rawPath, const std::filesystem::path& tfRoot);
const wchar_t* entitySnapshotStatusName(tf2::native::EntitySnapshotQueryStatus status);
void addSteamRegistryRoots(std::vector<std::filesystem::path>& roots);
std::vector<std::uint8_t> readFile(const std::filesystem::path& path);
std::string sha256File(const std::filesystem::path& path, std::string& status);
std::filesystem::path localCacheDirectory(std::string& status);

// ------------------------------------------------- the native Win32 controls
// T76 stage 3 (moved out of main.cpp). src/main_ui.cpp owns the code; the
// handles and the two flags below stay DEFINED in main.cpp so the objects keep
// the initialisation order that translation unit already had -- only their
// linkage changed, which is what lets the new unit name them.
//
// g_uiBookmarkSlot is the slot the bookmark controls act on: 1-based, matching
// the store and the settings keys, so the control and the saved file cannot
// drift by one.
extern HWND g_uiStatus;
extern HWND g_uiOpen;
extern HWND g_uiCancel;
extern HWND g_uiConfirm;
extern HWND g_uiPlay;
extern HWND g_uiStop;
extern HWND g_uiBack;
extern HWND g_uiForward;
extern HWND g_uiReverse;
extern HWND g_uiTimeline;
extern HWND g_uiCameraMode;
extern HWND g_uiFreeFly;
extern HWND g_uiResetToPlayer;
extern HWND g_uiResetView;
extern HWND g_uiBookmarkSave;
extern HWND g_uiBookmarkName;
extern HWND g_uiBookmarkDelete;
extern HWND g_uiBookmarkList;
extern HWND g_uiKeybindReset;
extern HWND g_uiHudImport;
extern HWND g_uiHudReset;
extern tf2::native::NativeUiController* g_nativeUi;
extern std::size_t g_uiBookmarkSlot;
extern bool g_keybindsLoaded;
extern bool g_textFieldFocused;

void applyUiLayout(HWND window);
void setUiControlVisible(HWND control, bool visible);
bool uiControlsUnchanged(
    const tf2::native::UiSnapshot& state,
    int cameraMode,
    const std::tuple<int, std::int32_t, std::int32_t, bool, bool, std::string, std::string,
                     bool, std::uint32_t, std::string, int, std::size_t, bool, bool>& signature);
bool updateNativeUiControls(HWND window);
void createNativeUiControls(HWND window, HINSTANCE instance);

// S1 (2026-11-23, docs/UI-SHELL-DESIGN-2026-11-22.md): borderless fullscreen.
// One implementation for every entry point (Alt+Enter, Esc, --fullscreen, the
// future menu item): the window drops its frame, covers the monitor it is on,
// and gets its exact previous style and placement back on exit. Never DXGI
// exclusive mode (the renderer turns DXGI's own Alt+Enter off).
//   sizePinned  --window-size was given: refused, because every frame such a
//               run captures was promised at the pinned size.
//   source      a stable word for the trace ("alt-enter", "escape", "cli", ...).
// Returns true when the window ends in the requested state.
bool windowFullscreen();
bool setWindowFullscreen(HWND window, bool wanted, bool sizePinned, const char* source);
// Re-cover the monitor after a dpi or display-mode change; no-op when windowed.
void fullscreenRefit(HWND window, const char* reason);
// I2 (2026-11-23): F11 toggles fullscreen. This is the one call both the window
// procedure and the camera script's `fullscreen` step use, so the key and the
// gate cannot disagree about what the key does. source is the stable trace word
// ("f11", "script"); returns true when the window ends in the requested state.
bool toggleProductFullscreen(const char* source);
// N16 (2026-11-23): the pane layout --pane-layout sets, declared so the camera
// script can drive it too. name is "split" or "inset"; source is the stable trace
// word ("cli", "script"); applied receives the layout the renderer holds
// afterwards. Returns false on an unknown name or before a renderer exists.
bool setPaneLayoutNamed(const char* name, const char* source, std::string* applied);
// --fullscreen-selftest N: N enter/exit cycles, each checked against the monitor
// rect and against the rect, client size, style, zoom and visibility the window
// had before the first one. One trace line; returns true on status=pass.
bool runFullscreenSelfTest(HWND window, int cycles, bool sizePinned);

// I1 (2026-11-23): watch the next / previous player (left / right mouse click,
// camera-script `switch-player next|prev`). Queues FollowAction::Next/Prev; refused
// in free-look, free-fly, with the map overview open, or with no demo. One
// "input: player-switch" trace line per call. reasonOut may be null.
bool requestPlayerSwitch(bool next, const char* source, std::string* reasonOut);


// The control ids the Win32 layer creates and dispatches. Moved here with the
// layer itself so main_ui.cpp and main.cpp name the same numbers; inline so the
// header stays header-only and no gate has to link anything.
inline constexpr int kUiOpen = 4101;
inline constexpr int kUiCancel = 4102;
inline constexpr int kUiConfirm = 4103;
inline constexpr int kUiPlay = 4104;
inline constexpr int kUiStop = 4105;
inline constexpr int kUiBack = 4106;
inline constexpr int kUiForward = 4107;
inline constexpr int kUiReverse = 4108;
inline constexpr int kUiTimeline = 4109;
inline constexpr int kUiFreeFly = 4110;
inline constexpr int kUiResetToPlayer = 4111;
inline constexpr int kUiResetView = 4112;
inline constexpr int kUiBookmarkSave = 4113;
inline constexpr int kUiBookmarkName = 4114;
inline constexpr int kUiBookmarkDelete = 4115;
inline constexpr int kUiBookmarkList = 4116;
inline constexpr int kUiKeybindReset = 4117;
inline constexpr int kUiHudImport = 4121;
inline constexpr int kUiHudPreview = 4122;
inline constexpr int kUiHudScale = 4123;
inline constexpr int kUiHudReset = 4124;

// ------------------------------------------------- T76 stage 4 shared state
// The camera-script driver (src/camera_script.cpp) names the key table, the
// pause controller, the review session and the two hotkey dispatchers. They
// stay DEFINED in main.cpp -- same objects, same order among themselves -- and
// only their linkage changed, which is the stage-3 pattern above.
extern tf2::native::PauseKeyController g_pauseKey;
extern tf2::native::KeyBindTable g_keyBinds;
extern std::string g_lastDispatchedAction;
extern std::uint32_t g_hotkeySuppressedByFocus;
extern std::unique_ptr<tf2::native::ReviewSession> g_reviewSession;

bool handleCameraAction(const std::string& action, std::string& reason);
bool dispatchHotkey(unsigned vk, bool ctrl, bool alt, bool shift, bool character,
    std::string& action, std::string& result);

// ----------------------------------------- renderer & HUD startup (N24 B2-2)
// Moved out of wWinMain (main.cpp:8296-8376) as pure code motion.
// registerKeybinds stays DEFINED in main.cpp (hud-check.sh mutation-3 edits
// its registerDefault line there); g_renderer stays DEFINED in main.cpp and
// keeps its nullptr teardown and exit-code ownership in wWinMain.
extern tf2::native::Renderer* g_renderer;
void registerKeybinds(const std::filesystem::path& settingsFile);

struct RendererHudInitInputs {
  tf2::native::Renderer& renderer;
  HWND window = nullptr;
  const std::filesystem::path& hudRoot;
  const std::string& hudLayout;
  int hudScale = 100;
  bool hudScaleGiven = false;
  bool hudOnGiven = false;
  bool hudOn = false;
  bool hudResetRequested = false;
  bool hudApplyRequested = false;
};
bool initializeRendererAndHud(const RendererHudInitInputs& inputs);

}  // namespace tf2::native::main_internal
