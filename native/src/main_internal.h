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
#include <string>
#include <tuple>
#include <vector>

#include "demo_header.h"
#include "native_ui.h"
#include "install_lifecycle.h"

namespace tf2::native::main_internal {

// ------------------------------------------------------------------- trace
// TEMP DIAGNOSTIC -- entity-material wiring regression, 2026-10-09 (moved out
// of main.cpp:236-318). Nothing opens the trace file unless TF2_NATIVE_TRACE
// names one, so every other run is byte-for-byte unaffected.
double traceNowMs();
void traceCheckpoint(const char* what);
void traceFmt(const char* format, ...);
void openTrace();

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

}  // namespace tf2::native::main_internal
