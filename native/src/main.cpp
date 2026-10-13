#include "native_renderer.h"
#include "asset_root.h"
#include "settings_store.h"
#include "vpk_archive.h"
#include "vtf_texture.h"
#include "vmt_material.h"
#include "bsp_map.h"
#include "demo_header.h"
#include "audio_player.h"
#include "effects_timeline.h"
#include "playback_hud.h"
#include "model_loader.h"
#include "viewmodel.h"
#include "entity_model.h"
#include "animation_binding.h"
#include "item_schema.h"
#include "native_ui.h"

#include <Windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <bcrypt.h>
#include <dxgi.h>
#include <psapi.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <cstring>
#include <exception>
#include <fstream>
#include <shellapi.h>
#include <sstream>
#include <tuple>
#include <vector>
#include <memory>
#include <unordered_map>
#include <unordered_set>

namespace {
tf2::native::Renderer* g_renderer = nullptr;

// TEMP DIAGNOSTIC -- entity-material wiring regression, 2026-10-09.
// The process leaves with 0xC0000409 a fraction of a second after the main loop
// starts advancing ticks. The metrics file's last line cannot say where, and the
// exception is a fast-fail, so no filter that runs after the fact can. These
// checkpoints can: every line is flushed as it is written, so the last line in
// the file names the phase the process died in. Nothing opens the file unless
// TF2_NATIVE_TRACE names one, so every other run is byte-for-byte unaffected.
std::FILE* g_traceFile = nullptr;
LARGE_INTEGER g_traceFrequency{};
LARGE_INTEGER g_traceOrigin{};

double traceNowMs() {
  LARGE_INTEGER now{};
  QueryPerformanceCounter(&now);
  return 1000.0 * static_cast<double>(now.QuadPart - g_traceOrigin.QuadPart)
    / static_cast<double>(g_traceFrequency.QuadPart);
}

void traceCheckpoint(const char* what) {
  if (!g_traceFile) return;
  std::fprintf(g_traceFile, "%.1f %s\n", traceNowMs(), what);
  std::fflush(g_traceFile);
}

// The format string is NOT checked by the compiler. `_Printf_format_string_` was
// tried here and is inert for a user-defined variadic function: under /W4 a probe
// with `%d` fed a `const char*` produced no C4477, while the CRT's own printf did.
// Only /analyze acts on the annotation, and that is a project-wide flag rather than
// a local one. So a mismatch between this format and its arguments compiles
// silently -- one did: `%zu` fed a `uint32_t` printed uninitialised register bits as
// a bone count. Cast the argument to the type the conversion asks for, and do not
// trust the compiler to catch it.
void traceFmt(const char* format, ...) {
  if (!g_traceFile) return;
  // 192 was enough while every line was a handful of counters. A witness line
  // names a model path, and TF2 model paths run past 60 characters, so the
  // reading was being silently truncated mid-field -- the worst shape for a
  // diagnostic: it looks like it answered.
  char buffer[512];
  va_list args;
  va_start(args, format);
  std::vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  traceCheckpoint(buffer);
}

void openTrace() {
  // Read through the Win32 call rather than getenv/fopen so the steady build
  // keeps its one pre-existing warning and no more: a diagnostic channel that
  // adds noise to every build is a diagnostic channel people turn off.
  wchar_t path[MAX_PATH]{};
  if (GetEnvironmentVariableW(L"TF2_NATIVE_TRACE", path, MAX_PATH) == 0) return;
  if (_wfopen_s(&g_traceFile, path, L"w") != 0) return;
  if (!g_traceFile) return;
  QueryPerformanceFrequency(&g_traceFrequency);
  QueryPerformanceCounter(&g_traceOrigin);
  std::set_terminate([]() {
    traceCheckpoint("TERMINATE (a C++ exception left main)");
    std::abort();
  });
  // A fast-fail is not catchable, but a stack-cookie failure or a plain access
  // violation reaches this filter before the process dies, and the code is what
  // separates "our buffer was overrun" from "the heap was corrupted".
  SetUnhandledExceptionFilter([](LPEXCEPTION_POINTERS info) -> LONG {
    traceFmt("UNHANDLED 0x%08lX",
      static_cast<unsigned long>(info->ExceptionRecord->ExceptionCode));
    return EXCEPTION_EXECUTE_HANDLER;
  });
  traceCheckpoint("trace open");
}

tf2::native::NativeUiController* g_nativeUi = nullptr;
HWND g_uiStatus = nullptr;
HWND g_uiOpen = nullptr;
HWND g_uiCancel = nullptr;
HWND g_uiConfirm = nullptr;
HWND g_uiPlay = nullptr;
HWND g_uiStop = nullptr;
HWND g_uiBack = nullptr;
HWND g_uiForward = nullptr;
HWND g_uiReverse = nullptr;
HWND g_uiTimeline = nullptr;
constexpr int kUiOpen = 4101;
constexpr int kUiCancel = 4102;
constexpr int kUiConfirm = 4103;
constexpr int kUiPlay = 4104;
constexpr int kUiStop = 4105;
constexpr int kUiBack = 4106;
constexpr int kUiForward = 4107;
constexpr int kUiReverse = 4108;
constexpr int kUiTimeline = 4109;
bool g_orbiting = false;
POINT g_lastMouse{};

// Camera mode machine (T4). The controller owns the mode and the SPACE
// single/double classification; the per-scene-tick decision is
// decideCameraSceneAction and the counters below are what the trace reports
// and camera-ui-check.sh pins.
tf2::native::CameraModeController g_cameraMode;
tf2::native::PauseKeyController g_pauseKey;
tf2::native::CameraMode g_lastAppliedCameraMode = tf2::native::CameraMode::FirstPerson;
std::uint32_t g_cameraFollows = 0;       // scene ticks that followed an observe target
std::uint32_t g_cameraHoldsNoTarget = 0; // scene ticks with no target: camera held, never moved
std::uint32_t g_cameraFreezeFrames = 0;  // free-look scene ticks: nothing fed the camera

// Repositions every UI control from the planner's answer for the current
// client rect and window dpi. The planner guarantees containment and
// non-overlap; this function is the only place that turns a plan into
// MoveWindow calls, so a layout regression is a planner regression the probe
// can see, not a Win32 accident it cannot.
void applyUiLayout(HWND window) {
  if (!window) return;
  RECT client{};
  if (!GetClientRect(window, &client)) return;
  const UINT dpi = GetDpiForWindow(window);
  const auto plan = tf2::native::planUiLayout(client.right, client.bottom,
    static_cast<float>(dpi) / 96.0f);
  if (!plan.valid) return;
  const auto move = [&](tf2::native::UiControlId id, HWND control) {
    if (!control) return;
    const auto& rect = plan.rects[static_cast<std::size_t>(id)];
    MoveWindow(control, rect.x, rect.y, rect.w, rect.h, TRUE);
  };
  move(tf2::native::UiControlId::Status, g_uiStatus);
  move(tf2::native::UiControlId::Open, g_uiOpen);
  move(tf2::native::UiControlId::Cancel, g_uiCancel);
  move(tf2::native::UiControlId::Confirm, g_uiConfirm);
  move(tf2::native::UiControlId::Play, g_uiPlay);
  move(tf2::native::UiControlId::Stop, g_uiStop);
  move(tf2::native::UiControlId::Back, g_uiBack);
  move(tf2::native::UiControlId::Forward, g_uiForward);
  move(tf2::native::UiControlId::Reverse, g_uiReverse);
  move(tf2::native::UiControlId::Timeline, g_uiTimeline);
  if (g_nativeUi) {
    g_nativeUi->setDisplayMetrics(dpi, static_cast<unsigned>(client.right),
      static_cast<unsigned>(client.bottom), IsZoomed(window) != 0);
  }
}

// Reduce a `--model` argument to the form the animation cache is keyed by.
// Two legitimate shapes, and picking the wrong one is the difference between
// "model-missing" and a pose:
//   * a path under the tf root, e.g. tf/download/models/buildables/x.mdl --
//     loose custom models live there and are NOT reachable as `models/...`
//   * a VPK path, e.g. models/weapons/w_models/w_pistol.mdl
// So an absolute path under the root becomes root-relative first, and only a
// path with no root prefix is reduced to its `models/` suffix. Anything that
// matches neither is passed through unchanged, so a wrong path stays wrong and
// surfaces as model-missing with the path it tried.
std::string normalizedModelPath(const std::string& rawPath, const std::filesystem::path& tfRoot) {
  std::string path = rawPath;
  for (char& character : path) {
    if (character == '\\') character = '/';
  }
  const auto lower = [](std::string value) {
    for (char& character : value) {
      character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return value;
  };
  const std::string root = lower(tfRoot.generic_string());
  const std::string lowered = lower(path);
  if (!root.empty() && lowered.size() > root.size() && lowered.compare(0, root.size(), root) == 0) {
    std::size_t start = root.size();
    while (start < path.size() && path[start] == '/') ++start;
    if (start < path.size()) return path.substr(start);
  }
  // The VPK-relative path starts at the *first* "models/", not the last.
  // `rfind` was latent-wrong for any path containing "v_models/", because that
  // directory name itself contains "models/": `models/weapons/v_models/
  // v_scattergun_scout.mdl` was truncated to `models/v_scattergun_scout.mdl` and
  // every ViewModel then read as "was not found". The two `--model` paths the T1
  // gate uses (`models/player/scout.mdl`, `models/props_ui/bannerflag_comp_l.mdl`)
  // contain the substring once, so `find` and `rfind` agree on them and that
  // gate's readings are unchanged.
  const std::size_t marker = lowered.find("models/");
  if (marker == std::string::npos) return path;
  return path.substr(marker);
}

struct PlaybackState {
  bool enabled = false;
  bool paused = false;
  bool reverse = false;
  double speed = 1.0;
  double tickRate = 66.6666667;
  std::int32_t tick = 0;
  std::int32_t endTick = 0;
  std::int32_t anchorLookupTick = -1;
  bool anchorValid = false;
  tf2::native::DemoIndexEntry anchor{};
};
PlaybackState g_playback{};
constexpr double kTargetFrameSeconds = 1.0 / 120.0;

const wchar_t* entitySnapshotStatusName(tf2::native::EntitySnapshotQueryStatus status) {
  switch (status) {
    case tf2::native::EntitySnapshotQueryStatus::Available: return L"available";
    case tf2::native::EntitySnapshotQueryStatus::Checkpoint: return L"checkpoint";
    case tf2::native::EntitySnapshotQueryStatus::NoHistory: return L"no-history";
    case tf2::native::EntitySnapshotQueryStatus::TickBeforeHistory: return L"before-window";
    case tf2::native::EntitySnapshotQueryStatus::Gap: return L"gap";
    case tf2::native::EntitySnapshotQueryStatus::DeltaBaseMissing: return L"delta-base-missing";
    default: return L"unknown";
  }
}

void addSteamRegistryRoots(std::vector<std::filesystem::path>& roots) {
  constexpr std::array<const wchar_t*, 2> keys = {
    L"Software\\Valve\\Steam",
    L"Software\\WOW6432Node\\Valve\\Steam",
  };
  constexpr std::array<const wchar_t*, 2> values = { L"SteamPath", L"InstallPath" };
  for (const auto* key : keys) {
    for (const auto* value : values) {
      for (const auto hive : { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE }) {
        HKEY handle = nullptr;
        if (RegOpenKeyExW(hive, key, 0, KEY_READ, &handle) != ERROR_SUCCESS) continue;
        wchar_t buffer[MAX_PATH]{};
        DWORD bytes = sizeof(buffer);
        const LONG result = RegQueryValueExW(handle, value, nullptr, nullptr,
          reinterpret_cast<LPBYTE>(buffer), &bytes);
        RegCloseKey(handle);
        if (result == ERROR_SUCCESS && buffer[0] != L'\0') roots.emplace_back(buffer);
      }
    }
  }
}

void setUiControlVisible(HWND control, bool visible) {
  if (control) ShowWindow(control, visible ? SW_SHOW : SW_HIDE);
}

// Signature of everything updateNativeUiControls actually pushes to a control:
// the screen, the fields that appear in the status line, the camera mode the
// player status line shows, and the timeline range and position. `dpi`/
// `recentDemos`/`tfRoot` are deliberately absent -- nothing here renders them,
// so a change there must not force a refresh. dtors of the tuple members
// (std::string) are cheap next to a SendMessageW round trip.
bool uiControlsUnchanged(const tf2::native::UiSnapshot& state, int cameraMode,
    const std::tuple<int, std::int32_t, std::int32_t, bool, bool, std::string,
      std::string, bool, std::uint32_t, std::string, int>& signature) {
  return std::get<0>(signature) == static_cast<int>(state.screen)
    && std::get<1>(signature) == state.tick
    && std::get<2>(signature) == state.ticks
    && std::get<3>(signature) == state.playing
    && std::get<4>(signature) == state.reverse
    && std::get<5>(signature) == state.mapName
    && std::get<6>(signature) == state.recordingType
    && std::get<7>(signature) == state.bspAvailable
    && std::get<8>(signature) == state.missingResourceCount
    && std::get<9>(signature) == state.error
    && std::get<10>(signature) == cameraMode;
}

// Returns true when it actually pushed to the controls, false when the cached
// signature said there was nothing to push. Callers use the return value as the
// "work done" reading rather than counting the call itself.
bool updateNativeUiControls(HWND window) {
  if (!g_nativeUi) return false;
  const auto state = g_nativeUi->snapshot();
  const int cameraMode = static_cast<int>(g_cameraMode.mode());
  // Cached signature of the previous push. A static here is safe: there is one
  // window, hence one call site, and the whole reason this function is called
  // every iteration is to keep the controls in step with the playback state.
  static std::tuple<int, std::int32_t, std::int32_t, bool, bool, std::string,
    std::string, bool, std::uint32_t, std::string, int> lastSignature{};
  static bool signatureValid = false;
  const std::tuple<int, std::int32_t, std::int32_t, bool, bool, std::string,
    std::string, bool, std::uint32_t, std::string, int> signature{
      static_cast<int>(state.screen), state.tick, state.ticks, state.playing,
      state.reverse, state.mapName, state.recordingType, state.bspAvailable,
      state.missingResourceCount, state.error, cameraMode};
  if (signatureValid && uiControlsUnchanged(state, cameraMode, lastSignature)) return false;
  lastSignature = signature;
  signatureValid = true;
  const bool opening = state.screen == tf2::native::UiScreen::Opening;
  const bool review = state.screen == tf2::native::UiScreen::ImportReview;
  const bool player = state.screen == tf2::native::UiScreen::Player;
  const bool error = state.screen == tf2::native::UiScreen::Error;
  setUiControlVisible(g_uiOpen, opening || error);
  setUiControlVisible(g_uiCancel, review || error);
  setUiControlVisible(g_uiConfirm, review);
  setUiControlVisible(g_uiPlay, player);
  setUiControlVisible(g_uiStop, player);
  setUiControlVisible(g_uiBack, player);
  setUiControlVisible(g_uiForward, player);
  setUiControlVisible(g_uiReverse, player);
  setUiControlVisible(g_uiTimeline, player);
  if (g_uiTimeline) {
    SendMessageW(g_uiTimeline, TBM_SETRANGE, TRUE,
      MAKELONG(0, std::max<std::int32_t>(0, state.ticks)));
    SendMessageW(g_uiTimeline, TBM_SETPOS, TRUE, state.tick);
  }
  std::wstring status = L"Opening: choose a .dem file";
  if (review) {
    status = L"Import review: " + std::wstring(state.mapName.begin(), state.mapName.end())
      + L" | " + std::wstring(state.recordingType.begin(), state.recordingType.end())
      + L" | ticks=" + std::to_wstring(state.ticks)
      + L" | BSP=" + (state.bspAvailable ? L"ready" : L"missing")
      + L" | missing=" + std::to_wstring(state.missingResourceCount);
  } else if (player) {
    status = L"Player: tick " + std::to_wstring(state.tick) + L"/" + std::to_wstring(state.ticks)
      + (state.playing ? L" | playing" : L" | paused")
      + (state.reverse ? L" | reverse" : L"")
      + L" | camera " + [&]() {
          const char* modeName = tf2::native::cameraModeName(g_cameraMode.mode());
          return std::wstring(modeName, modeName + std::strlen(modeName));
        }();
  } else if (error) {
    status = L"Error: " + std::wstring(state.error.begin(), state.error.end());
  }
  if (g_uiStatus) SetWindowTextW(g_uiStatus, status.c_str());
  if (window) InvalidateRect(window, nullptr, FALSE);
  return true;
}

void createNativeUiControls(HWND window, HINSTANCE instance) {
  INITCOMMONCONTROLSEX common{sizeof(common), ICC_BAR_CLASSES};
  InitCommonControlsEx(&common);
  const auto make = [&](LPCWSTR className, LPCWSTR text, DWORD style, int id,
      int x, int y, int width, int height) {
    return CreateWindowExW(0, className, text, WS_CHILD | style,
      x, y, width, height, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
      instance, nullptr);
  };
  // The initial geometry comes from the same planner WM_SIZE will use later,
  // so the first paint and every resize after it follow one set of rules.
  RECT client{};
  UINT dpi = 96;
  if (GetClientRect(window, &client)) {
    dpi = GetDpiForWindow(window);
  } else {
    client.right = 1280;
    client.bottom = 720;
  }
  const auto plan = tf2::native::planUiLayout(client.right, client.bottom,
    static_cast<float>(dpi) / 96.0f);
  const auto& rects = plan.rects;
  const auto rectFor = [&](tf2::native::UiControlId id, int fallbackX, int fallbackY,
      int fallbackW, int fallbackH) {
    if (!plan.valid) return std::make_tuple(fallbackX, fallbackY, fallbackW, fallbackH);
    const auto& rect = rects[static_cast<std::size_t>(id)];
    return std::make_tuple(rect.x, rect.y, rect.w, rect.h);
  };
  auto [sx, sy, sw, sh] = rectFor(tf2::native::UiControlId::Status, 16, 12, 900, 28);
  g_uiStatus = make(L"STATIC", L"Opening: choose a .dem file", SS_LEFT, 0, sx, sy, sw, sh);
  auto [ox, oy, ow, oh] = rectFor(tf2::native::UiControlId::Open, 16, 48, 120, 30);
  g_uiOpen = make(L"BUTTON", L"Open Demo...", BS_PUSHBUTTON, kUiOpen, ox, oy, ow, oh);
  auto [cx, cy, cw, ch] = rectFor(tf2::native::UiControlId::Cancel, 144, 48, 90, 30);
  g_uiCancel = make(L"BUTTON", L"Cancel", BS_PUSHBUTTON, kUiCancel, cx, cy, cw, ch);
  auto [fx, fy, fw, fh] = rectFor(tf2::native::UiControlId::Confirm, 240, 48, 90, 30);
  g_uiConfirm = make(L"BUTTON", L"Import", BS_DEFPUSHBUTTON, kUiConfirm, fx, fy, fw, fh);
  auto [px, py, pw, ph] = rectFor(tf2::native::UiControlId::Play, 16, 84, 110, 30);
  g_uiPlay = make(L"BUTTON", L"Play/Pause", BS_PUSHBUTTON, kUiPlay, px, py, pw, ph);
  auto [tx, ty, tw, th] = rectFor(tf2::native::UiControlId::Stop, 132, 84, 80, 30);
  g_uiStop = make(L"BUTTON", L"Stop", BS_PUSHBUTTON, kUiStop, tx, ty, tw, th);
  auto [bx, by, bw, bh] = rectFor(tf2::native::UiControlId::Back, 218, 84, 80, 30);
  g_uiBack = make(L"BUTTON", L"< Tick", BS_PUSHBUTTON, kUiBack, bx, by, bw, bh);
  auto [nx, ny, nw, nh] = rectFor(tf2::native::UiControlId::Forward, 304, 84, 80, 30);
  g_uiForward = make(L"BUTTON", L"Tick >", BS_PUSHBUTTON, kUiForward, nx, ny, nw, nh);
  auto [rx, ry, rw, rh] = rectFor(tf2::native::UiControlId::Reverse, 390, 84, 90, 30);
  g_uiReverse = make(L"BUTTON", L"Reverse", BS_PUSHBUTTON, kUiReverse, rx, ry, rw, rh);
  auto [lx, ly, lw, lh] = rectFor(tf2::native::UiControlId::Timeline, 16, 122, 700, 30);
  g_uiTimeline = make(TRACKBAR_CLASSW, L"", TBS_AUTOTICKS | TBS_ENABLESELRANGE,
    kUiTimeline, lx, ly, lw, lh);
  updateNativeUiControls(window);
}

bool chooseDemoFile(HWND owner, std::filesystem::path& result) {
  wchar_t buffer[32768]{};
  OPENFILENAMEW dialog{};
  dialog.lStructSize = sizeof(dialog);
  dialog.hwndOwner = owner;
  dialog.lpstrFilter = L"Demo files (*.dem)\0*.dem\0All files (*.*)\0*.*\0\0";
  dialog.lpstrFile = buffer;
  dialog.nMaxFile = static_cast<DWORD>(std::size(buffer));
  dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
  if (!GetOpenFileNameW(&dialog)) return false;
  result = std::filesystem::path(buffer);
  return true;
}

LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  switch (message) {
  case WM_SIZE:
    if (g_renderer) g_renderer->resize(LOWORD(lParam), HIWORD(lParam));
    applyUiLayout(window);
    return 0;
  case WM_DPICHANGED: {
    // Per-monitor v2: the system suggests a new window rect scaled for the
    // new dpi; taking it and re-laying the controls keeps both the render
    // surface and the text rows proportional on a mixed-dpi desktop.
    if (lParam) {
      const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
      SetWindowPos(window, nullptr, suggested->left, suggested->top,
        suggested->right - suggested->left, suggested->bottom - suggested->top,
        SWP_NOZORDER | SWP_NOACTIVATE);
    }
    applyUiLayout(window);
    return 0;
  }
  case WM_RBUTTONDOWN:
    g_orbiting = true;
    g_lastMouse = POINT{static_cast<int>(static_cast<short>(LOWORD(lParam))), static_cast<int>(static_cast<short>(HIWORD(lParam)))};
    SetCapture(window);
    return 0;
  case WM_RBUTTONUP:
    g_orbiting = false;
    if (GetCapture() == window) ReleaseCapture();
    return 0;
  case WM_MOUSEMOVE:
    if (g_orbiting && g_renderer) {
      const POINT current{static_cast<int>(static_cast<short>(LOWORD(lParam))), static_cast<int>(static_cast<short>(HIWORD(lParam)))};
      g_renderer->orbitCamera(static_cast<float>(current.x - g_lastMouse.x), static_cast<float>(current.y - g_lastMouse.y));
      g_lastMouse = current;
    }
    return 0;
  case WM_MOUSEWHEEL:
    if (g_renderer) g_renderer->zoomCamera(static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / static_cast<float>(WHEEL_DELTA));
    return 0;
  case WM_DROPFILES: {
    if (g_nativeUi) {
      const HDROP drop = reinterpret_cast<HDROP>(wParam);
      wchar_t path[MAX_PATH]{};
      if (DragQueryFileW(drop, 0, path, static_cast<UINT>(std::size(path))) != 0) {
        const bool accepted = g_nativeUi->dropDemo(std::filesystem::path(path));
        const auto state = g_nativeUi->snapshot();
        std::wstring title = L"TF2 Demo Player - ";
        title += std::wstring(state.error.begin(), state.error.end());
        if (accepted) title = L"TF2 Demo Player - import review: "
          + std::wstring(state.mapName.begin(), state.mapName.end());
        SetWindowTextW(window, title.c_str());
        updateNativeUiControls(window);
      }
      DragFinish(drop);
    }
    return 0;
  }
  case WM_COMMAND: {
    if (!g_nativeUi || HIWORD(wParam) != BN_CLICKED) {
      return DefWindowProcW(window, message, wParam, lParam);
    }
    const auto id = LOWORD(wParam);
    if (id == kUiOpen) {
      std::filesystem::path path;
      if (chooseDemoFile(window, path)) g_nativeUi->openDemo(path);
    } else if (id == kUiCancel) {
      g_nativeUi->cancelImport();
    } else if (id == kUiConfirm) {
      if (g_nativeUi->confirmImport()) {
        const auto state = g_nativeUi->snapshot();
        g_playback.enabled = true;
        g_playback.endTick = state.ticks;
        g_playback.tick = 0;
        g_playback.paused = true;
        g_playback.reverse = false;
      }
    } else if (id == kUiPlay) {
      g_nativeUi->command(tf2::native::UiCommand::PlayPause);
    } else if (id == kUiStop) {
      g_nativeUi->command(tf2::native::UiCommand::Stop);
    } else if (id == kUiBack) {
      g_nativeUi->command(tf2::native::UiCommand::StepBackward);
    } else if (id == kUiForward) {
      g_nativeUi->command(tf2::native::UiCommand::StepForward);
    } else if (id == kUiReverse) {
      g_nativeUi->command(tf2::native::UiCommand::Reverse);
    } else {
      return DefWindowProcW(window, message, wParam, lParam);
    }
    const auto state = g_nativeUi->snapshot();
    g_playback.tick = state.tick;
    g_playback.paused = !state.playing;
    g_playback.reverse = state.reverse;
    updateNativeUiControls(window);
    return 0;
  }
  case WM_HSCROLL:
    if (g_nativeUi && reinterpret_cast<HWND>(lParam) == g_uiTimeline) {
      const auto position = static_cast<std::int32_t>(SendMessageW(
        g_uiTimeline, TBM_GETPOS, 0, 0));
      g_nativeUi->command(tf2::native::UiCommand::Scrub, position);
      const auto state = g_nativeUi->snapshot();
      g_playback.tick = state.tick;
      g_playback.paused = true;
      updateNativeUiControls(window);
      return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
  case WM_PAINT:
    {
      PAINTSTRUCT paint{};
      BeginPaint(window, &paint);
      EndPaint(window, &paint);
    }
    return 0;
  case WM_ERASEBKGND:
    return 1;
  case WM_KEYDOWN: {
    // Toggle commands must be edge-triggered; Windows may repeat WM_KEYDOWN
    // while a key is held, which otherwise flips state several times. 'P' is
    // in this list for the same reason: a held P would flip pause every
    // repeat.
    if ((lParam & (1u << 30)) != 0u
        && (wParam == VK_SPACE || wParam == 'R' || wParam == 'P' || wParam == VK_HOME
            || wParam == VK_END
            || (wParam >= VK_F1 && wParam <= VK_F8))) return 0;
    const bool functionKey = wParam >= VK_F1 && wParam <= VK_F8;
    if (functionKey && g_renderer) {
      const std::size_t slot = static_cast<std::size_t>(wParam - VK_F1);
      const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
      const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
      if (control && shift) g_renderer->clearCameraPreset(slot);
      else if (control) g_renderer->saveCameraPreset(slot);
      else g_renderer->applyCameraPreset(slot);
    } else if (wParam == VK_HOME) {
      if (g_renderer) g_renderer->resetCamera();
      if (g_playback.enabled) { g_playback.tick = 0; g_playback.reverse = false; }
    } else if (wParam == VK_END && g_playback.enabled) {
      g_playback.tick = g_playback.endTick;
    } else if (wParam == VK_SPACE && g_playback.enabled) {
      // SPACE is the camera cycle key now; P owns pause (see the 'P' arm
      // below). One edge press advances the mode; a second press inside the
      // controller's double-click window jumps to free-look. The controller
      // is pure, so the window is a number a gate measures, not a feeling.
      if (g_cameraMode.spacePress(static_cast<std::uint32_t>(GetTickCount64()))) {
        if (g_cameraMode.mode() != tf2::native::CameraMode::FirstPerson && g_renderer) {
          // Leaving first-person must release the demo eye immediately, not
          // at the next scene tick, or the orbit would fight a stale
          // observer view for one frame.
          g_renderer->clearObserverDemoView();
          g_lastAppliedCameraMode = g_cameraMode.mode();
        }
        updateNativeUiControls(window);
      }
    } else if (wParam == 'P' && g_playback.enabled) {
      // Pause moved from SPACE to P (T4): the pause toggle and the camera
      // cycle no longer share a key.
      g_playback.paused = g_pauseKey.toggle();
    } else if (wParam == 'R' && g_playback.enabled) {
      if (!g_playback.reverse && g_playback.tick == 0) g_playback.tick = g_playback.endTick;
      g_playback.reverse = !g_playback.reverse;
    } else if (wParam == VK_LEFT && g_playback.enabled) {
      const std::int32_t step = (GetKeyState(VK_SHIFT) & 0x8000) ? 66 : 1;
      g_playback.tick = std::max<std::int32_t>(0, g_playback.tick - step);
    } else if (wParam == VK_RIGHT && g_playback.enabled) {
      const std::int32_t step = (GetKeyState(VK_SHIFT) & 0x8000) ? 66 : 1;
      g_playback.tick = std::min(g_playback.endTick, g_playback.tick + step);
    } else if ((wParam == VK_OEM_PLUS || wParam == VK_ADD) && g_playback.enabled) {
      g_playback.speed = std::min(8.0, g_playback.speed * 2.0);
    } else if ((wParam == VK_OEM_MINUS || wParam == VK_SUBTRACT) && g_playback.enabled) {
      g_playback.speed = std::max(0.125, g_playback.speed * 0.5);
    }
    return 0;
  }
  case WM_DESTROY:
    PostQuitMessage(0);
    return 0;
  default:
    return DefWindowProcW(window, message, wParam, lParam);
  }
}

std::vector<std::uint8_t> readFile(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) return {};
  const auto size = file.tellg();
  if (size <= 0) return {};
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
  file.seekg(0);
  if (!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) return {};
  return bytes;
}

std::string sha256File(const std::filesystem::path& path, std::string& status) {
  std::ifstream file(path, std::ios::binary);
  if (!file) { status = "missing"; return {}; }
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  DWORD objectLength = 0, resultLength = 0;
  if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0
      || BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength), &resultLength, 0) != 0) {
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    status = "failed"; return {};
  }
  std::vector<std::uint8_t> object(objectLength);
  std::array<std::uint8_t, 32> digest{};
  std::string result;
  bool ok = BCryptCreateHash(algorithm, &hash, object.data(), objectLength, nullptr, 0, 0) == 0;
  // Keep the streaming buffer on the heap; the Windows default thread stack
  // is small enough that a 1 MiB local array can trigger STATUS_STACK_OVERFLOW.
  std::vector<std::uint8_t> chunk(1024u * 1024u);
  while (ok && file) {
    file.read(reinterpret_cast<char*>(chunk.data()), static_cast<std::streamsize>(chunk.size()));
    const auto count = file.gcount();
    if (count > 0) ok = BCryptHashData(hash, chunk.data(), static_cast<ULONG>(count), 0) == 0;
  }
  if (ok && BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) == 0) {
    static constexpr char hex[] = "0123456789abcdef";
    result.reserve(64);
    for (const auto byte : digest) { result.push_back(hex[byte >> 4u]); result.push_back(hex[byte & 15u]); }
    status = "ready";
  }
  if (hash) BCryptDestroyHash(hash);
  BCryptCloseAlgorithmProvider(algorithm, 0);
  if (result.empty()) status = "failed";
  return result;
}

std::filesystem::path localCacheDirectory(std::string& status) {
  wchar_t* value = nullptr; std::size_t length = 0;
  if (_wdupenv_s(&value, &length, L"LOCALAPPDATA") == 0 && value) {
    std::filesystem::path path = std::filesystem::path(value) / L"TF2 Demo Player" / L"cache";
    free(value);
    std::error_code error; std::filesystem::create_directories(path, error);
    if (error) { status = "unavailable"; return {}; }
    const auto probe = path / L".write-test";
    std::ofstream probeFile(probe, std::ios::binary | std::ios::trunc);
    if (!probeFile) { status = "unwritable"; return {}; }
    probeFile.close();
    std::filesystem::remove(probe, error);
    status = "ready";
    return path;
  }
  status = "unavailable";
  return {};
}

}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
  openTrace();
  // Keep the render surface in physical pixels on mixed-DPI desktop setups.
  if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
    SetProcessDPIAware();
  }
  const wchar_t* className = L"TF2DemoNativeRenderer";
  WNDCLASSW windowClass{};
  windowClass.hInstance = instance;
  windowClass.lpfnWndProc = windowProc;
  windowClass.lpszClassName = className;
  windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  if (!RegisterClassW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return 10;

  HWND window = CreateWindowExW(
    0, className, L"TF2 Demo Player - Native D3D11",
    WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1280, 720,
    nullptr, nullptr, instance, nullptr);
  if (!window) return 11;

  tf2::native::Renderer renderer;
  g_renderer = &renderer;
  // Which settings file to read has to be known *before* it is read, and the main
  // argument loop below runs after that read. So `--settings-file` is taken from a
  // pre-scan here; parsed down there it would arrive after the file had been loaded
  // and would silently do nothing.
  int argumentCount = 0;
  LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
  if (arguments) {
    for (int i = 1; i < argumentCount; ++i) {
      if (wcscmp(arguments[i], L"--settings-file") == 0) {
        // A flag with no value is refused rather than ignored: falling back to the
        // user's real settings file would make a run that asked to be hermetic read
        // the machine, and nothing in the trace would say so.
        if (i + 1 >= argumentCount) {
          OutputDebugStringW(L"TF2 Demo Player: --settings-file needs a path; refusing to guess.\n");
          return 13;
        }
        tf2::native::setSettingsFileOverride(std::filesystem::path(arguments[++i]));
      }
    }
  }
  auto persistent = tf2::native::loadSettings();
  tf2::native::NativeUiController nativeUi;
  nativeUi.setTfRoot(persistent.tfRoot);
  g_nativeUi = &nativeUi;
  DragAcceptFiles(window, TRUE);
  createNativeUiControls(window, instance);
  std::filesystem::path commandTfRoot;
  std::filesystem::path commandDemo;
  std::filesystem::path commandModel;
  UINT audioDevice = WAVE_MAPPER;
  bool audioDeviceRejected = false;
  // Test-only capture modes, off by default so the product path keeps the
  // window and the audio device exactly as before:
  //   --hidden-window   create the window and render normally, never show it
  //   --audio-disabled  never call waveOutOpen; events are counted and skipped
  bool hiddenWindow = false;
  bool audioDisabled = false;
  bool startPaused = false;
  std::filesystem::path metricsPath;
  std::filesystem::path captureFramePath;
  std::filesystem::path dumpEntityMaterialsPath;
  std::int64_t captureTick = 0;
  bool captureTickRejected = false;
  // Standalone `--model` skinning contract. The single-model path is a separate
  // contract from the entity-instance path: it uploads one skeleton to b1, so
  // it can and must enable the skinning branch explicitly. Three knobs:
  //   --model-sequence <n>  sample that sequence instead of the identity pose
  //   --model-tick <n>      the tick the standalone model samples at
  //   --model-skinning <0|1> 0 is the control: same matrices, branch disabled
  // The control exists so "bind pose does not deform" is a byte comparison
  // between two runs that differ only in the shader branch, not an assertion.
  std::int32_t modelSequence = -1;
  std::int64_t modelTick = 0;
  bool modelSkinningEnabled = true;
  bool modelArgumentRejected = false;
  // First-person ViewModel. The request is built once, at startup, and cached:
  // the plan forbids rescanning the VPK or re-parsing the MDL per frame.
  //   --viewmodel <path>        a `v_*.mdl`, loose or inside a VPK
  //   --viewmodel-hand <r|l>    which hand's attachment table to prefer
  //   --viewmodel-fov <n>       clamped to 40..120; omitted means the stored
  //                             setting, and the compiled default (90) when there
  //                             is no stored value. `--settings-file` (read by the
  //                             pre-scan near the top of wWinMain) is what makes
  //                             "no stored value" reachable from a gate.
  //   --viewmodel-identity-bones  a diagnostic, not a feature: upload one
  //                             identity matrix per bone so the pass takes its
  //                             skinning branch with a transform that must leave
  //                             the bind pose exactly where it was. Same shape as
  //                             T1's `--model-skinning` pair, and it is the only
  //                             way to show the ViewModel skinning branch runs
  //                             without deforming, since nothing else supplies a
  //                             bone table to this pass.
  //   --viewmodel-render <which> companion (the default) draws the model the
  //                             ViewModel entity's own m_nModelIndex names;
  //                             command draws exactly `--viewmodel` instead.
  //   --viewmodel-weapon-body <on|off>  the static weapon body, the second half
  //                             of the first-person composite: the model the
  //                             weapon entity's own m_iWorldModelIndex names,
  //                             drawn as its own draw request next to the arms.
  //                             `on` is the product default -- drawing it is
  //                             what this round adds; `off` renders the arms
  //                             alone and exists so the two frames can be
  //                             subtracted, which is what makes "the body
  //                             changed the picture" a measurement.
  //   --viewmodel-sequence <n>  an input OVERRIDE, not a feature: use sequence n
  //                             in place of the entity's own m_nSequence. It
  //                             exists because on both pinned recordings the
  //                             ViewModel entity carries sequences 1, 29 and 30
  //                             while the model it names can pose sequence 0 and
  //                             nothing else, so the accepted-pose branch cannot
  //                             be reached by waiting for a tick. Every reading
  //                             line reports `sequenceOverride=`, so an
  //                             overridden run cannot be read as a natural one.
  std::filesystem::path commandViewModel;
  bool viewModelLeftHand = false;
  float viewModelFov = 0.0f;
  bool viewModelFovGiven = false;
  bool viewModelIdentityBones = false;
  // Off by default, and the default is the point. This flag exists so the
  // accepted-pose path is reachable end to end and a gate can prove it moves the
  // picture; a natural run applies nothing and only reports what it refused.
  //
  // On both pinned recordings, turning it on changes nothing measurable: the
  // entity's sequences (1, 29, 30) are all inside the entity's own model's table
  // but none of them reaches an animation descriptor, so every frame is still
  // refused as `missing` and the bind pose is what gets drawn. Reaching a pose at
  // all on this input needs `--viewmodel-sequence` above.
  bool viewModelApplyAnimation = false;
  std::size_t viewModelBoneLimit = 0;   // 0 = upload the whole table
  // Which model the first-person pass draws.
  //
  // `companion` is the correct path this round exists to build: the model the
  // ViewModel entity's own `m_nModelIndex` names, which on every shipped
  // first-person composite is `models/weapons/c_models/c_<class>_arms.mdl`. That
  // model's own sequence table is what the entity's `m_nSequence` indexes, so
  // drawing it is what makes an accepted sequence a correct pose rather than a
  // coincidence (see viewmodel.h).
  //
  // `command` keeps T2's behaviour -- exactly the `--viewmodel` path -- and is
  // not a legacy switch. It is the product-side negative case: with it the pass
  // draws a model the entity did not name, and the contract must report
  // `modelMismatch` on every such frame. A run under `command` that reported
  // `ok` would mean the proof is not wired, which is a reading no fixture can
  // give because a fixture never reaches the draw path.
  bool viewModelRenderCompanion = true;
  // Negative = "use what the entity said", which is the only value a natural run
  // has. See the `--viewmodel-sequence` parse site for why the override exists.
  std::int32_t viewModelSequenceOverride = -1;
  // The static weapon body: the second half of the first-person composite, the
  // model the weapon entity's own `m_iWorldModelIndex` names. On by default,
  // because drawing it IS the product behaviour this round adds; `off` exists
  // as the A/B control that makes "the body changed the picture" a measured
  // statement instead of a claim. See the `--viewmodel-weapon-body` parse site.
  bool viewModelWeaponBodyEnabled = true;

  // Which material slot the pass binds. Negative = "the slot the drawn mesh
  // names", which is the only value a natural run has.
  //
  // The override exists because the negative cases the task must prove are not
  // reachable on the shipped input: all 36 shipped ViewModels resolve every slot,
  // and the only two `$translucent` slots both sit on meshes the pass does not
  // draw. A run that could only ever exercise slot 0 could not tell "alpha works"
  // from "alpha is never reached", so the override makes the alpha branch
  // reachable -- and every reading line reports `slotOverride=`, so an overridden
  // run cannot be mistaken for a natural one.
  std::int32_t viewModelMaterialSlotOverride = -1;
  // Forces one slot into the `MissingMaterial` state before the pass runs, so
  // the fallback branch is reachable. Negative = no injection. See the
  // `--viewmodel-material-missing-slot` parse site for why the input this
  // represents cannot be shipped as a fixture.
  std::int32_t viewModelMaterialMissingSlot = -1;
  // Forces the untinted path: every range is emitted with an empty key, so the
  // pass draws the flat colour and samples nothing.
  //
  // This is the control the pixel claim is measured against, and it is the same
  // state an unresolved slot reaches -- deliberately, because a control that took
  // a different route than the thing it controls would leave "the pixels changed"
  // and "the fallback is stable" as two separate claims about two separate code
  // paths. Here they are the same code path, measured twice.
  bool viewModelFlatColor = false;
  bool viewModelArgumentRejected = false;
  if (arguments) {
    for (int i = 1; i < argumentCount; ++i) {
      if (wcscmp(arguments[i], L"--quality") == 0 && i + 1 < argumentCount) {
        if (wcscmp(arguments[++i], L"standard") == 0) {
          persistent.render = tf2::native::RenderSettings::fromPreset(
            tf2::native::QualityPreset::Standard);
        } else if (wcscmp(arguments[i], L"performance") == 0) {
          persistent.render = tf2::native::RenderSettings::fromPreset(
            tf2::native::QualityPreset::Performance);
        }
      } else if (wcscmp(arguments[i], L"--vsync") == 0) {
        persistent.render.vsync = true;
      } else if (wcscmp(arguments[i], L"--no-vsync") == 0) {
        persistent.render.vsync = false;
      } else if (wcscmp(arguments[i], L"--tf-root") == 0 && i + 1 < argumentCount) {
        commandTfRoot = arguments[++i];
      } else if (wcscmp(arguments[i], L"--demo") == 0 && i + 1 < argumentCount) {
        commandDemo = arguments[++i];
      } else if (wcscmp(arguments[i], L"--model") == 0 && i + 1 < argumentCount) {
        commandModel = arguments[++i];
      } else if (wcscmp(arguments[i], L"--model-sequence") == 0 && i + 1 < argumentCount) {
        wchar_t* end = nullptr;
        const long long parsed = std::wcstoll(arguments[++i], &end, 10);
        if (end && *end == L'\0' && parsed >= 0) modelSequence = static_cast<std::int32_t>(parsed);
        else modelArgumentRejected = true;
      } else if (wcscmp(arguments[i], L"--model-sequence") == 0) {
        modelArgumentRejected = true;
      } else if (wcscmp(arguments[i], L"--model-tick") == 0 && i + 1 < argumentCount) {
        wchar_t* end = nullptr;
        const long long parsed = std::wcstoll(arguments[++i], &end, 10);
        if (end && *end == L'\0' && parsed >= 0) modelTick = static_cast<std::int64_t>(parsed);
        else modelArgumentRejected = true;
      } else if (wcscmp(arguments[i], L"--model-tick") == 0) {
        modelArgumentRejected = true;
      } else if (wcscmp(arguments[i], L"--model-skinning") == 0 && i + 1 < argumentCount) {
        if (wcscmp(arguments[++i], L"0") == 0) modelSkinningEnabled = false;
        else if (wcscmp(arguments[i], L"1") == 0) modelSkinningEnabled = true;
        else modelArgumentRejected = true;
      } else if (wcscmp(arguments[i], L"--model-skinning") == 0) {
        modelArgumentRejected = true;
      } else if (wcscmp(arguments[i], L"--viewmodel") == 0 && i + 1 < argumentCount) {
        commandViewModel = arguments[++i];
      } else if (wcscmp(arguments[i], L"--viewmodel") == 0) {
        viewModelArgumentRejected = true;
      } else if (wcscmp(arguments[i], L"--viewmodel-hand") == 0 && i + 1 < argumentCount) {
        if (wcscmp(arguments[++i], L"left") == 0) viewModelLeftHand = true;
        else if (wcscmp(arguments[i], L"right") == 0) viewModelLeftHand = false;
        else viewModelArgumentRejected = true;
      } else if (wcscmp(arguments[i], L"--viewmodel-hand") == 0) {
        viewModelArgumentRejected = true;
      } else if (wcscmp(arguments[i], L"--viewmodel-fov") == 0 && i + 1 < argumentCount) {
        wchar_t* end = nullptr;
        const double parsed = std::wcstod(arguments[++i], &end);
        // A non-numeric or non-finite FOV is refused rather than silently
        // becoming the default: "the gate asked for 40" and "the gate asked for
        // nothing" must not read the same.
        if (end && *end == L'\0' && std::isfinite(parsed)) {
          viewModelFov = static_cast<float>(parsed);
          viewModelFovGiven = true;
        } else {
          viewModelArgumentRejected = true;
        }
      } else if (wcscmp(arguments[i], L"--viewmodel-fov") == 0) {
        viewModelArgumentRejected = true;
      } else if (wcscmp(arguments[i], L"--viewmodel-apply-animation") == 0) {
        viewModelApplyAnimation = true;
      } else if (wcscmp(arguments[i], L"--viewmodel-identity-bones") == 0) {
        viewModelIdentityBones = true;
      } else if (wcscmp(arguments[i], L"--viewmodel-bone-limit") == 0 && i + 1 < argumentCount) {
        wchar_t* end = nullptr;
        const unsigned long long parsed = std::wcstoull(arguments[++i], &end, 10);
        if (end && *end == L'\0' && parsed > 0) viewModelBoneLimit = static_cast<std::size_t>(parsed);
        else viewModelArgumentRejected = true;
      } else if (wcscmp(arguments[i], L"--viewmodel-bone-limit") == 0) {
        viewModelArgumentRejected = true;
      } else if (wcscmp(arguments[i], L"--viewmodel-render") == 0 && i + 1 < argumentCount) {
        if (wcscmp(arguments[++i], L"companion") == 0) viewModelRenderCompanion = true;
        else if (wcscmp(arguments[i], L"command") == 0) viewModelRenderCompanion = false;
        else viewModelArgumentRejected = true;
      } else if (wcscmp(arguments[i], L"--viewmodel-render") == 0) {
        viewModelArgumentRejected = true;
      } else if (wcscmp(arguments[i], L"--viewmodel-weapon-body") == 0 && i + 1 < argumentCount) {
        // The A/B control for the second half of the composite. `off` is not a
        // legacy mode: it is the frame the `on` frame is compared against, and
        // without it "the body draw changed the picture" would be an assertion
        // about this file rather than a measurement of two bitmaps. The product
        // default is `on` -- see the declaration site.
        if (wcscmp(arguments[++i], L"on") == 0) viewModelWeaponBodyEnabled = true;
        else if (wcscmp(arguments[i], L"off") == 0) viewModelWeaponBodyEnabled = false;
        else viewModelArgumentRejected = true;
      } else if (wcscmp(arguments[i], L"--viewmodel-weapon-body") == 0) {
        viewModelArgumentRejected = true;
      } else if (wcscmp(arguments[i], L"--viewmodel-sequence") == 0 && i + 1 < argumentCount) {
        // An INPUT OVERRIDE, and named like one. The pinned recordings' ViewModel
        // entity carries sequences 1, 29 and 30; the model it names can pose
        // sequence 0 and nothing else, so the recording never selects the one
        // sequence that would produce a pose and the pose branch is unreachable
        // without constructing the input. Rather than let that branch go
        // unobserved -- or, worse, claim an acceptance the recording cannot
        // support -- the sequence can be named on the command line. It is
        // reported on every reading line as `sequenceOverride=`, so an overridden
        // run can never be mistaken for a natural one.
        wchar_t* end = nullptr;
        const long long parsed = std::wcstoll(arguments[++i], &end, 10);
        if (end && *end == L'\0' && parsed >= 0) {
          viewModelSequenceOverride = static_cast<std::int32_t>(parsed);
        } else {
          viewModelArgumentRejected = true;
        }
      } else if (wcscmp(arguments[i], L"--viewmodel-sequence") == 0) {
        viewModelArgumentRejected = true;
      } else if (wcscmp(arguments[i], L"--viewmodel-material-slot") == 0 && i + 1 < argumentCount) {
        // An INPUT OVERRIDE, reported on every material reading line as
        // `slotOverride=`, for the same reason `--viewmodel-sequence` is: the
        // shipped viewmodels draw ONE descriptor, so only one slot's material can
        // reach the screen naturally, and the slots that a model declares are not
        // therefore uninteresting -- two of the 144 shipped ViewModel material
        // slots are `$translucent` glass (v_sapper_spy slot 2, v_syringegun_medic
        // slot 2) and both sit on a mesh the pass does not draw. Naming the slot
        // makes the material path observable for any of them without pretending
        // the default changed.
        wchar_t* end = nullptr;
        const long long parsed = std::wcstoll(arguments[++i], &end, 10);
        if (end && *end == L'\0' && parsed >= 0) {
          viewModelMaterialSlotOverride = static_cast<std::int32_t>(parsed);
        } else {
          viewModelArgumentRejected = true;
        }
      } else if (wcscmp(arguments[i], L"--viewmodel-material-slot") == 0) {
        viewModelArgumentRejected = true;
      } else if (wcscmp(arguments[i], L"--viewmodel-material-missing-slot") == 0 && i + 1 < argumentCount) {
        // A LABELLED FAULT INJECTION, and it exists because the honest input for
        // this state does not ship: all 36 shipped ViewModels resolve every
        // material slot, and the 10 fixtures that DO carry the defect have no
        // geometry, so no fixture can reach the draw pass at all.
        //
        // So "a material that does not resolve falls back rather than drawing
        // garbage" cannot be observed on any input this machine has. What it can
        // be observed on is the state itself, forced here: the named slot is put
        // into the same status and the same cleared fields the resolver produces
        // for a genuinely absent material, and the reading line reports
        // `missingSlotInjected=`, so an injected run cannot be read as a natural
        // one. A test that could only ever see resolving materials would not know
        // the fallback branch is reachable at all.
        wchar_t* end = nullptr;
        const long long parsed = std::wcstoll(arguments[++i], &end, 10);
        if (end && *end == L'\0' && parsed >= 0) {
          viewModelMaterialMissingSlot = static_cast<std::int32_t>(parsed);
        } else {
          viewModelArgumentRejected = true;
        }
      } else if (wcscmp(arguments[i], L"--viewmodel-material-missing-slot") == 0) {
        viewModelArgumentRejected = true;
      } else if (wcscmp(arguments[i], L"--viewmodel-flat-color") == 0) {
        // The control for the pixel claim: the same draw with no material bound,
        // which is the picture this pass produced before the material chain
        // existed. Without it "the texture is bound" and "the texture changed the
        // picture in a way nobody measured" are the same observation.
        viewModelFlatColor = true;
      } else if (wcscmp(arguments[i], L"--audio-device") == 0 && i + 1 < argumentCount) {
        wchar_t* end = nullptr;
        const unsigned long parsed = std::wcstoul(arguments[++i], &end, 10);
        if (end && *end == L'\0' && parsed < waveOutGetNumDevs()) audioDevice = static_cast<UINT>(parsed);
        else audioDeviceRejected = true;
      } else if (wcscmp(arguments[i], L"--audio-device") == 0) {
        audioDeviceRejected = true;
      } else if (wcscmp(arguments[i], L"--hidden-window") == 0) {
        hiddenWindow = true;
      } else if (wcscmp(arguments[i], L"--audio-disabled") == 0) {
        audioDisabled = true;
      } else if (wcscmp(arguments[i], L"--start-paused") == 0) {
        startPaused = true;
      } else if (wcscmp(arguments[i], L"--metrics-file") == 0 && i + 1 < argumentCount) {
        metricsPath = arguments[++i];
      } else if (wcscmp(arguments[i], L"--dump-entity-materials") == 0 && i + 1 < argumentCount) {
        dumpEntityMaterialsPath = arguments[++i];
      } else if (wcscmp(arguments[i], L"--capture-frame") == 0 && i + 1 < argumentCount) {
        captureFramePath = arguments[++i];
      } else if (wcscmp(arguments[i], L"--capture-tick") == 0 && i + 1 < argumentCount) {
        wchar_t* end = nullptr;
        const long long parsed = std::wcstoll(arguments[++i], &end, 10);
        if (end && *end == L'\0' && parsed >= 0) captureTick = static_cast<std::int64_t>(parsed);
        else captureTickRejected = true;
      } else if (wcscmp(arguments[i], L"--capture-tick") == 0) {
        captureTickRejected = true;
      }
    }
    persistent.render.normalize();
  }
  if (audioDeviceRejected) {
    OutputDebugStringW(L"TF2 Demo Player: invalid --audio-device; default device not used.\n");
    return 13;
  }
  if (captureTickRejected) {
    OutputDebugStringW(L"TF2 Demo Player: invalid --capture-tick; refusing to guess a tick.\n");
    return 13;
  }
  if (modelArgumentRejected) {
    OutputDebugStringW(L"TF2 Demo Player: invalid --model-sequence/--model-tick/--model-skinning; refusing to guess.\n");
    return 13;
  }
  if (viewModelArgumentRejected) {
    OutputDebugStringW(L"TF2 Demo Player: invalid --viewmodel/--viewmodel-hand/--viewmodel-fov/--viewmodel-render/--viewmodel-weapon-body/--viewmodel-sequence; refusing to guess.\n");
    return 13;
  }
  renderer.setSettings(persistent.render);
  // The native shell uses the installed TF2 `tf` directory as its first
  // resource boundary; Demo header parsing and the first resource selection pass are active.
  std::vector<std::filesystem::path> candidates = {
    L"D:\\SteamLibrary\\steamapps\\common\\Team Fortress 2\\tf",
    L"C:\\Program Files (x86)\\Steam\\steamapps\\common\\Team Fortress 2\\tf",
    L"C:\\Program Files\\Steam\\steamapps\\common\\Team Fortress 2\\tf",
  };
  if (!commandTfRoot.empty()) {
    // An explicit command-line root is an operator decision. Do not silently
    // switch to another Steam installation when that path is moved or missing.
    candidates = {commandTfRoot};
  } else if (!persistent.tfRoot.empty()) {
    candidates.insert(candidates.begin(), persistent.tfRoot);
  }
  if (arguments) LocalFree(arguments);
  if (!commandDemo.empty()) {
    nativeUi.openDemo(commandDemo);
    if (nativeUi.snapshot().screen == tf2::native::UiScreen::ImportReview) {
      nativeUi.confirmImport();
    }
  }
  wchar_t* steamRoot = nullptr;
  size_t steamRootLength = 0;
  if (_wdupenv_s(&steamRoot, &steamRootLength, L"STEAM_DIR") == 0 && steamRoot) {
    candidates.emplace_back(std::filesystem::path(steamRoot)
      / L"steamapps\\common\\Team Fortress 2\\tf");
    free(steamRoot);
  }
  std::vector<std::filesystem::path> steamRoots = {
    L"C:\\Program Files (x86)\\Steam",
    L"C:\\Program Files\\Steam",
    L"D:\\SteamLibrary",
  };
  addSteamRegistryRoots(steamRoots);
  const auto steamCandidates = tf2::native::AssetRoot::steamLibraryCandidates(steamRoots);
  candidates.insert(candidates.end(), steamCandidates.begin(), steamCandidates.end());
  const auto assets = tf2::native::AssetRoot::findInstalled(candidates);
  nativeUi.setTfRoot(assets.valid() ? assets.tfDirectory : persistent.tfRoot);
  std::string cacheStatus;
  const auto cacheDirectory = localCacheDirectory(cacheStatus);
  std::string hashStatus = commandDemo.empty() ? "missing" : "failed";
  const std::string demoSha256 = commandDemo.empty() ? std::string{} : sha256File(commandDemo, hashStatus);
  std::size_t modelCompanionMissing = 0;
  std::size_t modelVpkHits = 0;
  std::size_t modelLooseHits = 0;
  std::size_t modelUnknownPaths = 0;
  std::size_t itemSchemaHits = 0;
  std::size_t itemSchemaMisses = 0;
  tf2::native::ModelFeatureStatus modelAttachmentStatus = tf2::native::ModelFeatureStatus::Unknown;
  tf2::native::ModelFeatureStatus modelBodygroupStatus = tf2::native::ModelFeatureStatus::Unknown;
  tf2::native::ModelFeatureStatus modelViewModelStatus = tf2::native::ModelFeatureStatus::Unknown;
  std::size_t modelBoneCount = 0;
  std::size_t modelAttachmentCount = 0;
  std::size_t modelBodyPartCount = 0;
  bool modelPosePreflight = false;
  std::string itemSchemaStatus = "missing";
  std::unique_ptr<tf2::native::ItemSchema> itemSchema;
  if (assets.valid()) {
    std::string itemSchemaError;
    itemSchema = tf2::native::ItemSchema::load(
      assets.tfDirectory / L"scripts/items/items_game.txt", itemSchemaError);
    if (itemSchema) itemSchemaStatus = "ready";
  }
  tf2::native::DemoHeader demoHeader;
  tf2::native::DemoIndex demoIndex;
  tf2::native::DemoNetworkSummary demoNetworkSummary;
  if (!commandDemo.empty()) {
    if (tf2::native::parseDemoHeaderFile(commandDemo, demoHeader)) {
      tf2::native::indexDemoFile(commandDemo, demoHeader, demoIndex);
      demoNetworkSummary.networkProtocol = demoHeader.networkProtocol;
      tf2::native::scanKnownDemoMessages(commandDemo, demoIndex, demoNetworkSummary);
      tf2::native::buildAssetReferenceList(demoNetworkSummary, demoNetworkSummary.assetReferences);
      for (const auto& reference : demoNetworkSummary.assetReferences) {
        const auto candidate = tf2::native::ModelLoader::resolveAssetReference(assets, reference, itemSchema.get());
        if (candidate.resolution == tf2::native::ModelAssetResolution::FoundLoose) ++modelLooseHits;
        else if (candidate.resolution == tf2::native::ModelAssetResolution::FoundVpk) ++modelVpkHits;
        else if (candidate.resolution == tf2::native::ModelAssetResolution::Unknown) ++modelUnknownPaths;
        if (reference.hasItemDefIndex) {
          if (itemSchema && itemSchema->find(static_cast<int>(reference.itemDefIndex))) ++itemSchemaHits;
          else ++itemSchemaMisses;
        }
      }
    }
  }
  tf2::native::ModelRenderRequestStats modelRequestStats;
  std::vector<tf2::native::ModelRenderRequest> modelRenderRequests;
  // Per-modelPath animation cache. It is filled lazily on the first scene update
  // that references a model, so a demo that never spawns a weapon never pays for
  // decoding one, and no model is decoded twice.
  tf2::native::AnimationBindingCache animationBindings;
  animationBindings.reset(&assets);
  // A second cache for the first-person pass, deliberately not the one above:
  // that cache's resolve counters are what weapon-animation-check.sh asserts on,
  // and routing the ViewModel's own model through it would move an entity
  // reading for a first-person reason. The two passes also have different
  // lifetimes -- the entity cache is hit for every instance of every scene
  // update, while this one holds the single model the pass draws.
  tf2::native::AnimationBindingCache viewModelAnimations;
  viewModelAnimations.reset(&assets);
  // What the last scene update actually animated, for the title and the gates.
  // These are the readings that make "the product is animating" checkable from
  // outside the process instead of only from a probe.
  struct AnimationSceneStats {
    std::size_t instancesWithAnimationInput = 0;
    std::size_t instancesSkinned = 0;
    std::size_t instancesBindPose = 0;
    std::size_t boneMatricesUploaded = 0;
    std::size_t bindingsResolved = 0;
    std::size_t bindingsUsable = 0;
    std::size_t cacheHits = 0;
    std::size_t archiveReads = 0;
    std::int32_t lastSequence = -1;
    int lastFrame = 0;
    int lastFrameCount = 0;
    std::string lastStatus = "none";
    std::string lastModel;
    std::string lastReason;
    // The T1 claim is not "matrices were uploaded" -- a bind pose uploads
    // matrices too. It is "the sampled frame of a real weapon/projectile entity
    // moves as the demo advances". That needs a named witness and a count of how
    // many times its frame actually changed, because a single scene update can
    // only ever show one frame.
    std::string witnessModel;
    std::int32_t witnessEntity = -1;
    std::int32_t witnessSequence = -1;
    int witnessFrame = 0;
    int witnessFrameCount = 0;
    std::size_t witnessObservations = 0;
    std::size_t witnessChanges = 0;
    std::size_t frameAdvances = 0;
    // Separates "the demo carries no cycle for these entities" from "it carries
    // one and it never moves". Without both, advances=0 is ambiguous between a
    // wiring gap and a static scene, and the two need different work.
    std::size_t cycleInstances = 0;
    std::size_t cycleNonZero = 0;
    std::size_t cycleChanges = 0;
    std::unordered_map<std::int32_t, int> lastFrameByEntity;
    std::unordered_map<std::int32_t, int> changesByEntity;
    std::unordered_map<std::int32_t, float> lastCycleByEntity;
    // The T1 target class is weapons, projectiles and wearables -- player
    // animation is a separate problem because these demos carry no sequence for
    // CTFPlayer. The witness is therefore restricted to that class, while
    // `frameAdvances` still counts every entity so a demo that only animates
    // world props reads as "advances=N, target witness none" instead of looking
    // like a wiring failure.
    std::size_t targetAdvances = 0;
    // How many weapon/projectile/wearable instances were skinned at all. Without
    // this, `targetAdvances=0` and `targetPose=0` have two opposite readings --
    // "no weapon was drawn in this window" and "weapons were drawn and did not
    // move" -- and the plan's T1 witness question ("is the cursor live, or does
    // the asset have no visible motion?") cannot be answered from the run. One is
    // a demo-content boundary, the other is a wiring defect.
    std::size_t targetClassInstances = 0;
    std::string targetWitnessModel;
    std::int32_t targetWitnessEntity = -1;
    std::int32_t targetWitnessSequence = -1;
    int targetWitnessFrame = 0;
    int targetWitnessFrameCount = 0;
    std::size_t targetWitnessChanges = 0;
    std::size_t targetWitnessObservations = 0;
    // Did the witness's own uploaded pose actually move, and if not, why not:
    // localFrame/sectionFrames come from the decoder and say whether the
    // animation carries one frame block for its whole span.
    int targetWitnessLocalFrame = 0;
    int targetWitnessSectionFrames = 0;
    std::size_t targetWitnessPoseChanges = 0;
    std::size_t targetPoseChanges = 0;
    std::size_t poseChanges = 0;
    std::size_t witnessPoseChanges = 0;
    int witnessLocalFrame = 0;
    int witnessSectionFrames = 0;
    std::unordered_map<std::int32_t, std::uint64_t> poseHashByEntity;
    std::unordered_map<std::int32_t, int> poseChangesByEntity;
    // "The uploaded bone count equals the valid bone count" as a reading rather
    // than as an assumption: every skinned instance must have produced exactly
    // as many matrices as it declared valid bind bones.
    std::size_t boneCountMismatches = 0;
    std::size_t gpuSkinnedInstances = 0;
    std::size_t gpuBoneMatrices = 0;
    // Playback position of the last scene update. A tick that goes backwards
    // means the demo was restarted or rewound, and the per-entity frame/cycle
    // history describes a different run from then on.
    std::int32_t lastObservedTick = 0;
    bool hasObservedTick = false;
    // Per-update readings are cleared before each scene update; the cumulative
    // ones above are not. Assigning a fresh AnimationSceneStats{} every update
    // (which is what this used to do) wiped the per-entity history along with
    // them, so no update could ever compare a frame against the previous one and
    // advances stayed at zero no matter how long the demo ran.
    void resetPerUpdate() {
      instancesWithAnimationInput = 0;
      instancesSkinned = 0;
      instancesBindPose = 0;
      boneMatricesUploaded = 0;
      // `targetClassInstances` is "how many weapon/projectile/wearable models are
      // in this update's draw list", so it belongs with the per-update readings
      // above. Left out, it accumulated across every scene update and read
      // 8139/16588/24637 at the three witness ticks -- a number that looks like
      // "how many weapons are drawn" and is actually a running total. It is
      // deliberately the only field added here: `poseChanges`, `advances`,
      // `targetPose` and `cycle` are cumulative by design (the gates read them as
      // "ever moved", and resetting them would silently change those readings),
      // which is exactly why the new field's kind has to be stated rather than
      // inferred from its neighbours.
      targetClassInstances = 0;
      bindingsResolved = 0;
      bindingsUsable = 0;
      cacheHits = 0;
      archiveReads = 0;
      lastSequence = -1;
      lastFrame = 0;
      lastFrameCount = 0;
      lastStatus = "none";
      lastModel.clear();
      lastReason.clear();
    }
  };
  AnimationSceneStats animationStats;
  // The T2.5 reading, kept out of AnimationSceneStats on purpose: that struct is
  // the entity pass's account and its counters are what weapon-animation-check.sh
  // asserts on, so a ViewModel field there would move an entity reading for a
  // first-person reason.
  struct ViewModelAnimationStats {
    std::size_t observations = 0;
    std::size_t entitiesFound = 0;
    std::size_t sequencesFound = 0;
    std::size_t cyclesFound = 0;
    std::size_t okPoses = 0;
    std::size_t posesApplied = 0;
    std::size_t outOfRange = 0;
    std::size_t boneMismatch = 0;
    std::size_t missing = 0;
    // The companion proof's own account. `modelMismatch` and `modelMatches`
    // partition the observations by the one question the proof asks first, so a
    // run that never reached the proof at all reads `0/0` rather than borrowing
    // the `missing` count.
    std::size_t modelMismatch = 0;
    std::size_t modelMatches = 0;
    std::size_t companionFrames = 0;
    // The sequence question, counted separately from the pose question. On the
    // pinned recordings the two disagree on every frame: the entity's sequence is
    // inside its own model's table and that entry names no pose, so a single
    // counter would report the model's own content as an index defect.
    std::size_t sequenceInRange = 0;
    // "The cursor moves" is a weaker reading than "the picture moves", and it is
    // the one this pass can honestly offer: the sequence belongs to the entity,
    // and the pose the pass draws comes from the drawn model's own table, which
    // that index does not address.
    std::int32_t lastSequence = -1;
    std::size_t sequenceChanges = 0;
    std::int32_t minSequence = -1;
    std::int32_t maxSequence = -1;
    int lastEntity = -1;
    std::string lastEntityModel;
    std::string lastStatus = "none";
    std::string lastReason;
    bool lastEntityModelMatchesDrawn = false;
    bool lastBindPose = true;
    bool lastSequenceAccepted = false;
    bool lastSequenceInRange = false;
    std::string lastDrawnModel;
  };
  ViewModelAnimationStats viewModelAnimationStats;
  // The weapon body's account, kept separate from the arms' for the same reason
  // the arms' is kept separate from the entity pass's: an arms reading that moved
  // because of a weapon-body event would make both unreadable.
  //
  // WHAT EACH COUNTER IS. `observations` counts scene updates that ran the
  // lookup at all. `refusalCounts` has one bucket per `ViewModelWeaponBodyRefusal`
  // and is indexed by it, so bucket 0 IS the success count and every refusal has
  // its own number: "the body never drew" repairs differently for each (no weapon
  // entity named, the weapon entity absent from the snapshot, the index zero, the
  // index naming nothing in modelprecache, and -- the defect signature -- the
  // index resolving to the arms model itself). A single "not found" counter would
  // make those look like one condition, and adding a refusal to the enum without
  // a bucket here would be a compile error rather than a silent merge.
  struct ViewModelWeaponBodyStats {
    std::size_t observations = 0;
    std::size_t weaponEntityNamed = 0;
    std::size_t weaponEntityPresent = 0;
    std::array<std::size_t, 12> refusalCounts{};
    std::size_t bodiesBuilt = 0;
    std::size_t bodiesUploaded = 0;
    std::size_t buildFailed = 0;
    int lastWeaponEntity = -1;
    int lastWorldModelIndex = 0;
    std::string lastWeaponClass;
    std::string lastPath;
    std::string lastReason;
    // The refusal, in the form the reading prints: `-` when the body drew, and
    // the specific condition when it did not. This is the field a gate reads to
    // tell "the body is off by request" from "the body's model was missing".
    std::string lastFallback;
    tf2::native::ViewModelWeaponBodyRefusal lastRefusal =
      tf2::native::ViewModelWeaponBodyRefusal::NoViewModelEntity;
    std::size_t lastMaterialSlots = 0;
    std::size_t lastMaterialNamed = 0;
    std::size_t lastMaterialVmt = 0;
    std::size_t lastMaterialVtf = 0;
    // The first slot that did not reach a .vtf, with the resource it stopped on.
    // It is the field that turns `weaponMaterials=2/2/1/2` into a repairable
    // statement: the count says one material is missing, this says which.
    std::string lastMaterialFailure;
  };
  ViewModelWeaponBodyStats viewModelWeaponStats;
  // The refusals that actually happened, in the order the enum declares them:
  // `none:41,worldModelIndexZero:3` reads as "41 scene updates resolved a body,
  // 3 were refused because the index was zero". Only non-zero buckets print, so
  // a clean run's text is just `none:<n>`.
  const auto viewModelWeaponRefusalText = [&]() -> std::string {
    std::string text;
    for (std::size_t index = 0; index < viewModelWeaponStats.refusalCounts.size(); ++index) {
      const std::size_t count = viewModelWeaponStats.refusalCounts[index];
      if (count == 0) continue;
      if (!text.empty()) text += ",";
      text += tf2::native::viewModelWeaponBodyRefusalName(
        static_cast<tf2::native::ViewModelWeaponBodyRefusal>(index));
      text += ":";
      text += std::to_string(count);
    }
    return text.empty() ? std::string("-") : text;
  };
  struct PreparedEntityMesh {
    std::string path;
    std::string cacheKey;
    std::vector<tf2::native::ModelDrawVertex> vertices;
    // Paint resolution for this model, filled from its MDL texture table. The
    // empty materialVtfPath means the model ships no readable main texture and
    // the draw loop keeps the world atlas for its instances.
    std::vector<std::string> materialCandidates;
    std::string materialName;
    std::string materialVmtPath;
    std::string materialVtfPath;
    std::size_t textureSlots = 0;
    std::size_t textureNamesRead = 0;
  };
  std::vector<PreparedEntityMesh> preparedEntityMeshes;
  std::unordered_map<std::string, std::string> entityMeshKeyByPath;
  // Paint accounting for the window title: how many prepared models reached a
  // readable VTF, and how many of those became a GPU texture.
  std::size_t entityMaterialResolvedModels = 0;
  std::size_t entityMaterialUploadedModels = 0;
  // One flag per prepared mesh, in the same order, so --dump-entity-materials
  // can say which models reached a GPU texture and which did not.
  std::vector<char> entityMaterialUploadFlags;
  if (assets.valid()) {
    modelRenderRequests = tf2::native::ModelLoader::buildRenderRequests(
      assets, demoNetworkSummary.assetReferences, itemSchema.get(), &modelRequestStats);
    std::vector<tf2::native::AssetReference> classFallbacks;
    for (std::int64_t tfClass = 1; tfClass <= 9; ++tfClass) {
      tf2::native::AssetReference fallback;
      fallback.hasModelPath = true;
      fallback.modelPath = tf2::native::EntityModelResolver::defaultPlayerModelPath(tfClass);
      classFallbacks.push_back(std::move(fallback));
    }
    auto classRequests = tf2::native::ModelLoader::buildRenderRequests(
      assets, classFallbacks, itemSchema.get(), nullptr);
    std::vector<tf2::native::ModelRenderRequest> meshSource = modelRenderRequests;
    meshSource.insert(meshSource.end(), classRequests.begin(), classRequests.end());
    std::unordered_set<std::string> preparedKeys;
    preparedEntityMeshes.reserve(64);
    for (auto& request : meshSource) {
      if (!request.renderable || request.inspection.viewModelPathDetected) continue;
      if (preparedKeys.count(request.cacheKey)) continue;
      std::string meshError;
      if (!tf2::native::ModelLoader::buildBindPoseMeshLod0(request.inspection.metadata, meshError)
          || request.inspection.metadata.bindPoseVertices.size() < 3) continue;
      PreparedEntityMesh prepared;
      prepared.path = request.modelPath;
      prepared.cacheKey = request.cacheKey;
      prepared.vertices = std::move(request.inspection.metadata.bindPoseVertices);
      prepared.textureSlots = request.inspection.metadata.textureCount;
      prepared.textureNamesRead = request.inspection.metadata.textureNames.size();
      if (!request.inspection.metadata.textureCandidates.empty()) {
        prepared.materialCandidates = request.inspection.metadata.textureCandidates.front();
      }
      if (!prepared.materialCandidates.empty()) {
        prepared.materialName = prepared.materialCandidates.front();
      }
      preparedKeys.insert(request.cacheKey);
      entityMeshKeyByPath[request.modelPath] = request.cacheKey;
      preparedEntityMeshes.push_back(std::move(prepared));
      if (preparedEntityMeshes.size() >= 64) break;
    }
  }
  std::size_t indexedVpkEntries = 0;
  std::vector<std::uint8_t> displayRgba;
  UINT displayWidth = 0;
  UINT displayHeight = 0;
  tf2::native::VmtMaterial displayMaterial;
  std::string displayShader;
  std::string displayBaseTexture;
  tf2::native::BspMap displayMap;
  std::vector<std::uint8_t> mapRgba;
  UINT mapWidth = 0;
  UINT mapHeight = 0;
  std::string mapMaterialName;
  std::string mapTexturePath;
  std::string mapBaseTexture2Path;
  std::string mapBumpMapPath;
  std::string mapEnvMapPath;
  std::vector<std::uint8_t> mapBumpRgba;
  std::vector<std::uint8_t> mapEnvRgba;
  UINT mapBumpWidth = 0, mapBumpHeight = 0, mapEnvWidth = 0, mapEnvHeight = 0;
  bool mapBaseTexture2Found = false;
  bool mapBumpMapFound = false;
  bool mapBaseTexture2Declared = false;
  bool mapBumpMapDeclared = false;
  tf2::native::WorldMaterialParams mapMaterialParams;
  std::vector<tf2::native::WorldTexture> worldTextures;
  std::vector<std::shared_ptr<tf2::native::VpkArchive>> soundArchives;
  std::unordered_map<std::string, std::vector<std::uint8_t>> materialVtfCache;
  const auto readMaterialVtf = [&](const std::string& path) -> const std::vector<std::uint8_t>& {
    auto found = materialVtfCache.find(path);
    if (found != materialVtfCache.end()) return found->second;
    auto bytes = readFile(assets.tfDirectory / std::filesystem::path(path));
    for (const auto& archive : soundArchives) {
      if (!bytes.empty()) break;
      bytes = archive->read(path);
    }
    return materialVtfCache.emplace(path, std::move(bytes)).first->second;
  };
  if (assets.valid()) {
    std::filesystem::path mapPath = L"maps/2koth_abbey.bsp";
    if (demoHeader.valid) mapPath = std::filesystem::path(L"maps") / std::filesystem::path(std::wstring(demoHeader.mapName.begin(), demoHeader.mapName.end()) + L".bsp");
    for (const auto* name : { L"pak01_dir.vpk", L"tf2_misc_dir.vpk", L"tf2_textures_dir.vpk", L"tf2_sound_misc_dir.vpk", L"tf2_sound_vo_english_dir.vpk" }) {
      auto archive = std::make_shared<tf2::native::VpkArchive>();
      if (archive->open(assets.tfDirectory / name)) soundArchives.push_back(std::move(archive));
    }
    auto mapBytes = readFile(assets.tfDirectory / mapPath);
    if (mapBytes.empty()) {
      const std::string mapResource =
        std::filesystem::path(mapPath).generic_string();
      for (const auto& archive : soundArchives) {
        mapBytes = archive->read(mapResource);
        if (!mapBytes.empty()) break;
      }
    }
    tf2::native::BspParser::parse(mapBytes, displayMap, 200000);
    const std::string displayVmtPath = "materials/vgui/logos/spray.vmt";
    auto displayVmt = readFile(assets.tfDirectory / L"materials/vgui/logos/spray.vmt");
    if (!displayVmt.empty()) {
      const std::string materialText(displayVmt.begin(), displayVmt.end());
      if (tf2::native::VmtParser::parse(materialText, displayMaterial)) {
        displayShader = displayMaterial.shader;
        displayBaseTexture = displayMaterial.baseTexture;
      }
    }
    const std::string displayPath = tf2::native::VmtParser::resourcePath(
      displayBaseTexture.empty() ? "vgui/logos/spray" : displayBaseTexture, ".vtf");
    auto displayVtf = readFile(assets.tfDirectory / std::filesystem::path(displayPath));
    for (const auto& archive : soundArchives) {
      indexedVpkEntries = std::max(indexedVpkEntries, archive->entryCount());
      if (displayVtf.empty()) displayVtf = archive->read(displayPath);
      if (displayVmt.empty()) displayVmt = archive->read(displayVmtPath);
    }
    if (displayVtf.empty()) displayVtf = readFile(assets.tfDirectory / L"materials/vgui/logos/spray.vtf");
    if (displayMap.valid) {
      for (const auto& triangle : displayMap.triangles) if (!triangle.material.empty()) { mapMaterialName = triangle.material; break; }
      if (!mapMaterialName.empty()) {
        const std::string mapVmtPath = tf2::native::VmtParser::resourcePath(mapMaterialName, ".vmt");
        auto mapVmt = readFile(assets.tfDirectory / std::filesystem::path(mapVmtPath));
        for (const auto& archive : soundArchives) {
          if (!mapVmt.empty()) break;
          mapVmt = archive->read(mapVmtPath);
        }
        tf2::native::VmtMaterial mapMaterial;
        if (!mapVmt.empty() && tf2::native::VmtParser::parse(std::string(mapVmt.begin(), mapVmt.end()), mapMaterial)) {
          mapBaseTexture2Declared = !mapMaterial.baseTexture2.empty();
          mapBumpMapDeclared = !mapMaterial.bumpMap.empty();
          mapMaterialParams.envMap = false;
          mapMaterialParams.waterMaterial = mapMaterial.waterShader;
          mapMaterialParams.selfIllum = mapMaterial.selfIllum;
          mapMaterialParams.bumpMapping = false;
          if (!mapMaterial.selfIllumTint.empty()) {
            std::string tint = mapMaterial.selfIllumTint;
            for (char& character : tint) {
              if (character == ',' || character == '[' || character == ']' || character == '{' || character == '}') character = ' ';
            }
            std::istringstream values(tint);
            float r = 0.0f, g = 0.0f, b = 0.0f;
            std::string trailing;
            if (values >> r >> g >> b && !(values >> trailing)
                && std::isfinite(r) && std::isfinite(g) && std::isfinite(b)) {
              mapMaterialParams.selfIllumR = std::clamp(r, 0.0f, 4.0f);
              mapMaterialParams.selfIllumG = std::clamp(g, 0.0f, 4.0f);
              mapMaterialParams.selfIllumB = std::clamp(b, 0.0f, 4.0f);
            }
          }
        }
        if (!mapVmt.empty() && mapMaterial.valid && !mapMaterial.baseTexture.empty()) {
          mapTexturePath = tf2::native::VmtParser::resourcePath(mapMaterial.baseTexture, ".vtf");
          const auto& mapVtf = readMaterialVtf(mapTexturePath);
          tf2::native::VtfTexture mapTexture;
          if (!mapVtf.empty() && mapTexture.parse(mapVtf) && !(mapRgba = mapTexture.decodeRgba(mapVtf)).empty()) { mapWidth = mapTexture.header().width; mapHeight = mapTexture.header().height; }
        }
        const auto findMaterialTexture = [&](const std::string& materialName, std::string& path) {
          if (materialName.empty()) return false;
          path = tf2::native::VmtParser::resourcePath(materialName, ".vtf");
          return !readMaterialVtf(path).empty();
        };
        mapBaseTexture2Found = findMaterialTexture(mapMaterial.baseTexture2, mapBaseTexture2Path);
        mapBumpMapFound = findMaterialTexture(mapMaterial.bumpMap, mapBumpMapPath);
        if (!mapBumpMapPath.empty()) {
          const auto& bytes = readMaterialVtf(mapBumpMapPath);
          tf2::native::VtfTexture texture;
          if (!bytes.empty() && texture.parse(bytes) && !texture.header().isCubemap()) {
            mapBumpRgba = texture.decodeRgba(bytes);
            mapBumpWidth = texture.header().width; mapBumpHeight = texture.header().height;
            mapMaterialParams.bumpMapping = !mapBumpRgba.empty();
          }
        }
        if (!mapMaterial.envMap.empty()) {
          mapEnvMapPath = tf2::native::VmtParser::resourcePath(mapMaterial.envMap, ".vtf");
          const auto& bytes = readMaterialVtf(mapEnvMapPath);
          tf2::native::VtfTexture texture;
          if (!bytes.empty() && texture.parse(bytes) && !texture.header().isCubemap()) {
            mapEnvRgba = texture.decodeRgba(bytes);
            mapEnvWidth = texture.header().width; mapEnvHeight = texture.header().height;
            mapMaterialParams.envMap = !mapEnvRgba.empty();
          }
        }
      }
    }
    if (displayMap.valid) {
      std::unordered_set<std::string> seenMaterials;
      for (const auto& triangle : displayMap.triangles) {
        if (worldTextures.size() >= 64u || triangle.material.empty() || !seenMaterials.insert(triangle.material).second) continue;
        const auto materialPath = tf2::native::VmtParser::resourcePath(triangle.material, ".vmt");
        auto materialBytes = readFile(assets.tfDirectory / std::filesystem::path(materialPath));
        if (materialBytes.empty()) for (const auto& archive : soundArchives) {
          materialBytes = archive->read(materialPath); if (!materialBytes.empty()) break;
        }
        tf2::native::VmtMaterial material;
        if (materialBytes.empty() || !tf2::native::VmtParser::parse(std::string(materialBytes.begin(), materialBytes.end()), material)
            || material.baseTexture.empty()) continue;
        const auto texturePath = tf2::native::VmtParser::resourcePath(material.baseTexture, ".vtf");
        auto textureBytes = readFile(assets.tfDirectory / std::filesystem::path(texturePath));
        if (textureBytes.empty()) for (const auto& archive : soundArchives) {
          textureBytes = archive->read(texturePath); if (!textureBytes.empty()) break;
        }
        tf2::native::VtfTexture texture;
        if (textureBytes.empty() || !texture.parse(textureBytes) || texture.header().width > 512 || texture.header().height > 512) continue;
        auto rgba = texture.decodeRgba(textureBytes);
        if (!rgba.empty()) worldTextures.push_back({triangle.material, std::move(rgba), texture.header().width, texture.header().height});
      }
    }
    if (!displayVtf.empty()) {
      tf2::native::VtfTexture texture;
      if (texture.parse(displayVtf) && !(displayRgba = texture.decodeRgba(displayVtf)).empty()) {
        displayWidth = texture.header().width;
        displayHeight = texture.header().height;
      }
    }
    // Entity model paint, stage one: walk each prepared model's candidate list
    // for its first slot -- a bare stem carries one entry per `$cdmaterials`
    // directory -- and keep the first one whose VMT parses and whose VTF reads.
    // The pixels stay unread here; reloadGpuResources decodes and uploads one
    // model at a time so the RGBA buffers are not all alive at once.
    for (auto& prepared : preparedEntityMeshes) {
      for (const auto& candidate : prepared.materialCandidates) {
        if (candidate.empty()) continue;
        const std::string preparedVmtPath = tf2::native::VmtParser::resourcePath(candidate, ".vmt");
        auto preparedVmt = readFile(assets.tfDirectory / std::filesystem::path(preparedVmtPath));
        if (preparedVmt.empty()) for (const auto& archive : soundArchives) {
          preparedVmt = archive->read(preparedVmtPath); if (!preparedVmt.empty()) break;
        }
        if (preparedVmt.empty()) continue;
        tf2::native::VmtMaterial preparedMaterial;
        if (!tf2::native::VmtParser::parse(std::string(preparedVmt.begin(), preparedVmt.end()), preparedMaterial)
            || preparedMaterial.baseTexture.empty()) continue;
        const auto preparedVtfPath = tf2::native::VmtParser::resourcePath(preparedMaterial.baseTexture, ".vtf");
        if (readMaterialVtf(preparedVtfPath).empty()) continue;
        prepared.materialName = candidate;
        prepared.materialVmtPath = preparedVmtPath;
        prepared.materialVtfPath = preparedVtfPath;
        ++entityMaterialResolvedModels;
        break;
      }
    }
  }
  g_playback.enabled = demoHeader.valid;
  g_playback.endTick = std::max<std::int32_t>(0, demoHeader.ticks);
  g_playback.tick = 0;
  g_playback.paused = startPaused;
  g_playback.reverse = false;
  g_playback.tickRate = (demoHeader.playbackTime > 0.0f && demoHeader.ticks > 0)
    ? std::clamp(static_cast<double>(demoHeader.ticks) / static_cast<double>(demoHeader.playbackTime), 30.0, 128.0)
    : 66.6666667;
  tf2::native::SoundEventTimeline soundTimeline;
  // TWO ORIGINS, because a demo carries two tick spaces and they are not the
  // same number:
  //   * `firstPacketTick` is the SERVER tick origin. Entity history is keyed by
  //     the tick `svc_NetTick` carries, and the signon block's entry tick holds
  //     the server tick at connect, so the first command 1 or 2 entry is the
  //     right origin for `sceneTick`. Measured here: 29368.
  //   * `soundOriginTick` is the DEMO ENTRY tick origin, because sound events
  //     are tagged with `entry.tick`. The recorded packets carry their own
  //     counter that starts near zero, so the signon tick is *above* the whole
  //     recording and `max(0, tick - origin)` folds it onto tick 0 -- measured
  //     5555 of 5563 events, i.e. a batch flush on the first frame. Measured
  //     here: 0.
  // They were one variable, so fixing the sounds silently emptied the scene
  // (entity snapshots read `tick-before-history`, states=0). Kept separate and
  // both reported so the next reader cannot repeat that.
  std::int32_t firstPacketTick = 0;
  const auto firstPacket = std::find_if(demoIndex.entries.begin(), demoIndex.entries.end(),
    [](const auto& entry) { return entry.command == 1 || entry.command == 2; });
  if (firstPacket != demoIndex.entries.end()) firstPacketTick = firstPacket->tick;
  bool soundOriginFound = false;
  const std::int32_t soundOriginTick = tf2::native::firstRecordedPacketTick(demoIndex, soundOriginFound);
  const std::size_t soundEventsRead = demoNetworkSummary.decodedSoundEvents.size();
  std::size_t soundEventsUnresolved = 0;
  std::size_t soundEventsUnplaceable = 0;
  std::size_t soundEventsRefused = 0;
  for (const auto& source : demoNetworkSummary.decodedSoundEvents) {
    // A decoded event with no precache entry has no name to resolve, so there is
    // nothing to schedule. Counted, not silently dropped: it is one of the four
    // named failure points the resolver reports.
    if (source.resourceName.empty()) { ++soundEventsUnresolved; continue; }
    // No origin at all means the recording's own tick counter is unknown. Every
    // event is then unplaceable; refusing them keeps the failure countable
    // instead of scheduling them at their absolute ticks, which would never play
    // and would read exactly like "this recording has no sounds".
    std::int32_t relativeTick = 0;
    if (!soundOriginFound
        || !tf2::native::soundEventRelativeTick(source.tick, soundOriginTick, relativeTick)) {
      ++soundEventsUnplaceable;
      continue;
    }
    tf2::native::ScheduledSound sound;
    sound.tick = relativeTick;
    sound.soundIndex = source.soundIndex;
    sound.name = source.resourceName;
    sound.volume = source.volume;
    sound.delaySeconds = source.delaySeconds;
    sound.origin[0] = source.origin[0];
    sound.origin[1] = source.origin[1];
    sound.origin[2] = source.origin[2];
    // scheduleSoundEvent rather than scheduleGameplaySound: voice, announcer and
    // music stay in the timeline flagged as skipped so the scheduler's
    // eventsSkippedVoice counter is a reading that can move. They are still never
    // resolved and never played.
    if (!tf2::native::scheduleSoundEvent(soundTimeline, std::move(sound), g_playback.tickRate)) {
      ++soundEventsRefused;
    }
  }
  soundTimeline.sortByTick();
  const tf2::native::SoundKindCounts soundKindCounts = tf2::native::countSoundKinds(soundTimeline);
  // The timeline build, reported before playback starts. Kept separate from the
  // exit summary because a run that is cut short (--capture-frame, a gate
  // timeout) still has a built timeline, and the fields that describe the build
  // are the ones that say whether the demo's sounds were placed at all.
  traceFmt("audio: timeline firstPacketTick=%d soundOriginTick=%d originFound=%d read=%zu scheduled=%zu"
    " unresolved=%zu unplaceable=%zu refused=%zu kinds=w%zu/f%zu/u%zu/o%zu/v%zu/a%zu/m%zu",
    static_cast<int>(firstPacketTick), static_cast<int>(soundOriginTick), soundOriginFound ? 1 : 0,
    soundEventsRead, soundTimeline.size(), soundEventsUnresolved, soundEventsUnplaceable,
    soundEventsRefused, soundKindCounts.weapon, soundKindCounts.footstep, soundKindCounts.uber,
    soundKindCounts.world, soundKindCounts.voice, soundKindCounts.announcer, soundKindCounts.music);
  tf2::native::TempEffectTimeline effectTimeline;
  for (const auto& source : demoNetworkSummary.tempEntityEvents) {
    tf2::native::addFireBulletsEvent(source, effectTimeline);
    tf2::native::addExplosionEvent(source, effectTimeline);
    tf2::native::addParticleEvent(source, effectTimeline);
  }
  effectTimeline.sortByTick();
  tf2::native::AudioEventScheduler soundScheduler;
  soundScheduler.setDeviceId(audioDevice);
  if (audioDisabled) {
    // --audio-disabled: the contract is that waveOutOpen is never called. A
    // sink that resolves each event and then answers `skipped` keeps the tick
    // and event accounting running without a device, and it is not the same
    // as --start-paused: pausing stops the scheduler advancing, this only
    // stops the device from being asked. `skipped` is distinct from `failed`,
    // so the readings can tell "no device by policy" from "resource missing".
    soundScheduler.setOutcomeSink([](const tf2::native::WavPcmData&, float, float) {
      return tf2::native::AudioEventScheduler::PlaybackOutcome::skipped;
    });
  }
  std::unordered_map<std::string, tf2::native::WavPcmData> soundCache;
  constexpr std::size_t kSoundCacheLimit = 32u * 1024u * 1024u;
  constexpr std::size_t kSoundCacheEntryLimit = 512u;
  std::size_t soundCacheBytes = 0;
  soundScheduler.setResolver([&soundArchives, &soundCache, &soundCacheBytes](const std::string& name, tf2::native::WavPcmData& out) {
    const auto cached = soundCache.find(name);
    if (cached != soundCache.end()) { out = cached->second; return true; }
    for (const auto& archive : soundArchives) {
      tf2::native::SoundResource resource;
      if (tf2::native::readSoundResource(*archive, name, resource) && resource.codec == tf2::native::SoundCodec::Wav) {
        if (!resource.wav.valid) return false;
        const std::size_t pcmBytes = resource.wav.pcm.size();
        if (soundCache.size() < kSoundCacheEntryLimit && pcmBytes <= kSoundCacheLimit - soundCacheBytes) {
          auto inserted = soundCache.emplace(name, std::move(resource.wav));
          soundCacheBytes += inserted.first->second.pcm.size();
          out = inserted.first->second;
        } else {
          out = std::move(resource.wav);
        }
        return true;
      }
    }
    return false;
  });
  std::wstring demoSuffix;
  if (!commandDemo.empty()) {
    if (demoHeader.valid) {
      const auto recording = tf2::native::classifyDemoRecording(demoHeader, demoNetworkSummary);
      const char* typeName = recording.label;
      demoSuffix = L" | Demo: " + std::wstring(demoHeader.mapName.begin(), demoHeader.mapName.end())
        + L" " + std::wstring(typeName, typeName + std::strlen(typeName))
        + L" ticks=" + std::to_wstring(demoHeader.ticks)
        + L" commands=" + std::to_wstring(demoIndex.commandCount)
        + L" packets=" + std::to_wstring(demoIndex.packetCount)
        + L" serverinfo=" + std::to_wstring(demoNetworkSummary.serverInfoCount)
        + L" signon=" + std::to_wstring(demoNetworkSummary.signonStateCount)
        + L" state=" + std::to_wstring(demoNetworkSummary.lastSignonState)
        + L" classes=" + std::to_wstring(demoNetworkSummary.serverClassCount)
        + L" setview=" + std::to_wstring(demoNetworkSummary.setViewCount)
        + L" viewent=" + std::to_wstring(demoNetworkSummary.lastViewEntity)
        + L" nettick=" + std::to_wstring(demoNetworkSummary.netTickCount)
        + L" print=" + std::to_wstring(demoNetworkSummary.printCount)
        + L" setconvar=" + std::to_wstring(demoNetworkSummary.setConVarCount)
        + L" convars=" + std::to_wstring(demoNetworkSummary.setConVarPairCount)
        + L" temp=" + std::to_wstring(demoNetworkSummary.tempEntitiesCount)
        + L" teheaders=" + std::to_wstring(demoNetworkSummary.tempEventHeadersDecoded)
        + L" tefail=" + std::to_wstring(demoNetworkSummary.tempEventDecodeFailures)
        + L" teprops=" + std::to_wstring(demoNetworkSummary.tempPropDecodedCount)
        + L" teproj=" + std::to_wstring(demoNetworkSummary.tempClientProjectileCount)
        + L" teparticle=" + std::to_wstring(demoNetworkSummary.tempParticleEffectCount)
        + L" teexplosion=" + std::to_wstring(demoNetworkSummary.tempExplosionCount)
        + L" tebullets=" + std::to_wstring(demoNetworkSummary.tempFireBulletsCount)
        + L" teanim=" + std::to_wstring(demoNetworkSummary.tempPlayerAnimEventCount)
        + L" tefbhits=" + std::to_wstring(demoNetworkSummary.tempFireBulletsFieldHits)
        + L" tepehits=" + std::to_wstring(demoNetworkSummary.tempParticleEffectFieldHits)
        + L" teexhits=" + std::to_wstring(demoNetworkSummary.tempExplosionFieldHits)
        + L" teanimhits=" + std::to_wstring(demoNetworkSummary.tempPlayerAnimEventFieldHits)
        + L" tecached=" + std::to_wstring(demoNetworkSummary.tempEntityEvents.size())
        + L" projectileTimeline=" + std::to_wstring(demoNetworkSummary.projectileTimeline.size())
        + L" assetRefs=" + std::to_wstring(demoNetworkSummary.assetReferences.size())
        + L" modelKnown=" + std::to_wstring(demoNetworkSummary.assetModelIndexKnown)
        + L" weaponKnown=" + std::to_wstring(demoNetworkSummary.assetWeaponKnown)
        + L" itemKnown=" + std::to_wstring(demoNetworkSummary.assetItemDefKnown)
        + L" paintKnown=" + std::to_wstring(demoNetworkSummary.assetPaintKitKnown)
        + L" skinKnown=" + std::to_wstring(demoNetworkSummary.assetSkinKnown)
        + L" qualityKnown=" + std::to_wstring(demoNetworkSummary.assetQualityKnown)
        + L" modelPathKnown=" + std::to_wstring(demoNetworkSummary.assetModelPathKnown)
        + L" weaponClassKnown=" + std::to_wstring(demoNetworkSummary.assetWeaponClassKnown)
        + L" assetUnknown=" + std::to_wstring(demoNetworkSummary.assetIdentityUnknown)
        + L" modelLoose=" + std::to_wstring(modelLooseHits)
        + L" modelVpk=" + std::to_wstring(modelVpkHits)
        + L" modelPathUnknown=" + std::to_wstring(modelUnknownPaths)
        + L" schema=" + std::wstring(itemSchemaStatus.begin(), itemSchemaStatus.end())
        + L" schemaHits=" + std::to_wstring(itemSchemaHits)
        + L" schemaMiss=" + std::to_wstring(itemSchemaMisses)
        + L" tempevents=" + std::to_wstring(demoNetworkSummary.tempEventCount)
        + L" sounds=" + std::to_wstring(demoNetworkSummary.soundMessageCount)
        + L" soundevents=" + std::to_wstring(demoNetworkSummary.soundEventCount)
        + L" sounddecoded=" + std::to_wstring(demoNetworkSummary.decodedSoundEvents.size())
        + L" sounddecodefail=" + std::to_wstring(demoNetworkSummary.decodedSoundEventFailures)
        + L" soundprecache=" + std::to_wstring(demoNetworkSummary.soundPrecache.size())
        + L" soundtablefail=" + std::to_wstring(demoNetworkSummary.soundPrecacheDecodeFailures)
        + L" soundtableid=" + std::to_wstring(demoNetworkSummary.soundPrecacheTableId)
        + L" soundupdates=" + std::to_wstring(demoNetworkSummary.soundPrecacheUpdateCount)
        + L" updateentries=" + std::to_wstring(demoNetworkSummary.lastStringTableUpdateEntries)
        + L" updatebits=" + std::to_wstring(demoNetworkSummary.lastStringTableUpdateBits)
        + L" soundupdate=" + std::to_wstring(demoNetworkSummary.lastStringTableUpdateId)
        + L" updateids=" + (demoNetworkSummary.stringTableUpdateIds.empty() ? L"" : std::to_wstring(demoNetworkSummary.stringTableUpdateIds.front()))
        + L" soundmatch=" + std::to_wstring(demoNetworkSummary.decodedSoundResourceMatches)
        + L" soundmiss=" + std::to_wstring(demoNetworkSummary.decodedSoundResourceMisses)
        + L" voice=" + std::to_wstring(demoNetworkSummary.voiceDataCount)
        + L" updates=" + std::to_wstring(demoNetworkSummary.updateStringTableCount)
        + L" entities=" + std::to_wstring(demoNetworkSummary.packetEntitiesCount)
        + L" entityupdates=" + std::to_wstring(demoNetworkSummary.packetEntityUpdates)
        + L" entityheaders=" + std::to_wstring(demoNetworkSummary.packetEntityHeaderUpdates)
        + L" enters=" + std::to_wstring(demoNetworkSummary.packetEntityEnterCount)
        + L" preserves=" + std::to_wstring(demoNetworkSummary.packetEntityPreserveCount)
        + L" leaves=" + std::to_wstring(demoNetworkSummary.packetEntityLeaveCount)
        + L" deletes=" + std::to_wstring(demoNetworkSummary.packetEntityDeleteCount)
        + L" entityfail=" + std::to_wstring(demoNetworkSummary.packetEntityDecodeFailures)
        + L" epmiss=" + std::to_wstring(demoNetworkSummary.entityPropMissingTableFailures)
        + L" epidx=" + std::to_wstring(demoNetworkSummary.entityPropIndexFailures)
        + L" epval=" + std::to_wstring(demoNetworkSummary.entityPropValueFailures)
        + L" epfirst=" + std::to_wstring(demoNetworkSummary.firstEntityPropFailureTick)
        + L" epent=" + std::to_wstring(demoNetworkSummary.firstEntityPropFailureEntity)
        + L" epclass=" + std::to_wstring(demoNetworkSummary.firstEntityPropFailureClass)
        + L" epidx0=" + std::to_wstring(demoNetworkSummary.firstEntityPropFailureIndex)
        + L" epstage=" + std::wstring(demoNetworkSummary.firstEntityPropFailureStage.begin(), demoNetworkSummary.firstEntityPropFailureStage.end())
        + L" epname=" + std::wstring(demoNetworkSummary.firstEntityPropFailureName.begin(), demoNetworkSummary.firstEntityPropFailureName.end())
        + L" eptype=" + std::to_wstring(demoNetworkSummary.firstEntityPropFailureType)
        + L" epflags=" + std::to_wstring(demoNetworkSummary.firstEntityPropFailureFlags)
        + L" epbits=" + std::to_wstring(demoNetworkSummary.firstEntityPropFailureBits)
        + L" netraw=" + (demoNetworkSummary.lastNetworkTickRawValid ? std::to_wstring(demoNetworkSummary.lastNetworkTickRaw) : L"-1")
        + L" tefirst=" + std::to_wstring(demoNetworkSummary.firstTempEntityFailureTick)
        + L" teclass=" + std::to_wstring(demoNetworkSummary.firstTempEntityFailureClass)
        + L" tebits=" + std::to_wstring(demoNetworkSummary.firstTempEntityFailurePayloadBits)
        + L" testage=" + std::wstring(demoNetworkSummary.firstTempEntityFailureStage.begin(), demoNetworkSummary.firstTempEntityFailureStage.end())
        + L" tename=" + std::wstring(demoNetworkSummary.firstTempEntityFailureName.begin(), demoNetworkSummary.firstTempEntityFailureName.end())
        + L" tetype=" + std::to_wstring(demoNetworkSummary.firstTempEntityFailureType)
        + L" teflags=" + std::to_wstring(demoNetworkSummary.firstTempEntityFailureFlags)
        + L" tepropbits=" + std::to_wstring(demoNetworkSummary.firstTempEntityFailureBits)
        + L" epstate=" + std::to_wstring(demoNetworkSummary.entityUnknownStateFailures)
        + L" ephdr=" + std::to_wstring(demoNetworkSummary.entityUpdateHeaderFailures)
        + L" base=" + std::to_wstring(demoNetworkSummary.instanceBaselineEntryCount)
        + L" baseok=" + std::to_wstring(demoNetworkSummary.instanceBaselineAppliedCount)
        + L" basemiss=" + std::to_wstring(demoNetworkSummary.instanceBaselineLookupMisses)
        + L" baseapplyfail=" + std::to_wstring(demoNetworkSummary.instanceBaselineApplyFailures)
        + L" basefirst=" + std::to_wstring(demoNetworkSummary.firstInstanceBaselineClassId)
        + L" baselook=" + std::to_wstring(demoNetworkSummary.firstInstanceBaselineLookupClassId)
        + L" basetable=" + (demoNetworkSummary.firstInstanceBaselineLookupHasTable ? L"1" : L"0")
        + L" baseentry=" + (demoNetworkSummary.firstInstanceBaselineLookupHasEntry ? L"1" : L"0")
        + L" baseapplied=" + (demoNetworkSummary.firstInstanceBaselineLookupApplied ? L"1" : L"0")
        + L" baseclass=" + std::to_wstring(demoNetworkSummary.firstInstanceBaselineClassId)
        + L" basefail=" + std::to_wstring(demoNetworkSummary.instanceBaselineDecodeFailures)
        + L" basezip=" + std::to_wstring(demoNetworkSummary.instanceBaselineCompressedCount)
        + L" basemagic=" + std::to_wstring(demoNetworkSummary.instanceBaselineMagic)
        + L" basec=" + std::to_wstring(demoNetworkSummary.instanceBaselineCompressedBytes)
        + L" based=" + std::to_wstring(demoNetworkSummary.instanceBaselineDecompressedBytes)
        + L" active=" + std::to_wstring(demoNetworkSummary.activeEntityCount)
        + L" activeMax=" + std::to_wstring(demoNetworkSummary.maxActiveEntityCount)
        + L" histEvents=" + std::to_wstring(demoNetworkSummary.entityHistoryEvents.size())
        + L" histCheckpoints=" + std::to_wstring(demoNetworkSummary.entityHistoryCheckpoints.size())
        + L" histDropped=" + std::to_wstring(demoNetworkSummary.entityHistoryDroppedPackets)
        + L" histDeltaMiss=" + std::to_wstring(demoNetworkSummary.entityHistoryDeltaBaseMisses)
        + L" histGap=" + (demoNetworkSummary.entityHistoryHasGap ? L"1" : L"0")
        + L" eventlists=" + std::to_wstring(demoNetworkSummary.gameEventListCount)
        + L" events=" + std::to_wstring(demoNetworkSummary.gameEventCount)
        + L" usermsg=" + std::to_wstring(demoNetworkSummary.userMessageCount)
        + L" prefetch=" + std::to_wstring(demoNetworkSummary.prefetchCount)
        + L" stringcmd=" + std::to_wstring(demoNetworkSummary.stringCommandCount)
        + L" fixangle=" + std::to_wstring(demoNetworkSummary.fixAngleCount)
        + L" cvar=" + std::to_wstring(demoNetworkSummary.getCvarValueCount)
        + L" entitymsg=" + std::to_wstring(demoNetworkSummary.entityMessageCount)
        + L" sendtables=" + std::to_wstring(demoNetworkSummary.sendTableCount)
        + L" dtables=" + std::to_wstring(demoNetworkSummary.dataTableDefinitionCount)
        + L" dtprops=" + std::to_wstring(demoNetworkSummary.dataTablePropCount)
        + L" flatprops=" + std::to_wstring(demoNetworkSummary.dataTableFlattenedPropCount)
        + L" dtclasses=" + std::to_wstring(demoNetworkSummary.dataTableServerClassCount)
        + L" unknown0=" + (demoNetworkSummary.unknownMessageTypes.empty() ? L"" : std::to_wstring(demoNetworkSummary.unknownMessageTypes.front()))
        + L" tables=" + std::to_wstring(demoNetworkSummary.stringTableCount)
        + L" table0=" + (demoNetworkSummary.stringTableNames.empty() ? L"" : std::wstring(demoNetworkSummary.stringTableNames.front().begin(), demoNetworkSummary.stringTableNames.front().end()))
        + L" servermap=" + std::wstring(demoNetworkSummary.serverMap.begin(), demoNetworkSummary.serverMap.end())
        + L" stv=" + (demoNetworkSummary.serverInfoHltv ? L"1" : L"0")
        + L" replaybit=" + (demoNetworkSummary.serverInfoReplayBit ? L"1" : L"0")
        + L" views=" + std::to_wstring(demoNetworkSummary.viewSamples.size());
    } else {
      demoSuffix = L" | Demo error: " + std::wstring(demoHeader.error.begin(), demoHeader.error.end());
    }
  }
  const std::string lightmapStateNarrow = tf2::native::BspParser::lightmapModeName(displayMap.lightmapMode);
  const std::wstring lightmapState(lightmapStateNarrow.begin(), lightmapStateNarrow.end());
  const std::wstring lightmapTone = displayMap.lightmapMode == tf2::native::BspLightmapMode::RgbExp32 ? L" tone=ldr-clamp" : L"";
  const std::wstring title = assets.valid()
    ? std::wstring(L"TF2 Demo Player - TF2 资源已连接: ") + assets.tfDirectory.wstring()
      + L" [VPK entries: " + std::to_wstring(indexedVpkEntries)
      + L", VTF: " + (displayWidth ? std::to_wstring(displayWidth) + L"x" + std::to_wstring(displayHeight) : L"fallback")
      + L", VMT: " + (displayShader.empty() ? L"unparsed" : std::wstring(displayShader.begin(), displayShader.end()))
      + L"/" + (displayBaseTexture.empty() ? L"no base texture" : std::wstring(displayBaseTexture.begin(), displayBaseTexture.end()))
      + L", BSP: " + (displayMap.valid ? std::to_wstring(displayMap.triangleCount) + L" tris" : (L"invalid/" + std::wstring(displayMap.error.begin(), displayMap.error.end())))
      + L", lightmap: samples=" + std::to_wstring(displayMap.lightmapSampleCount)
      + L" faces=" + std::to_wstring(displayMap.lightmapFaceCount)
      + L" tris=" + std::to_wstring(displayMap.lightmapTriangleCount)
      + L" bytes=" + std::to_wstring(displayMap.lightmapBytes)
      + L" state=" + lightmapState + lightmapTone
      + L" atlas=" + std::to_wstring(displayMap.lightmapAtlasWidth) + L"x" + std::to_wstring(displayMap.lightmapAtlasHeight)
      + L", mapmat: " + (mapMaterialName.empty() ? L"none" : std::wstring(mapMaterialName.begin(), mapMaterialName.end()))
      + L", deps: base2=" + (mapBaseTexture2Declared ? (mapBaseTexture2Found ? L"ok" : L"missing") : L"none")
      + L" bump=" + (mapBumpMapDeclared ? (mapBumpMapFound ? L"ok" : L"missing") : L"none")
      + L", maptex: " + (mapWidth ? std::to_wstring(mapWidth) + L"x" + std::to_wstring(mapHeight) : L"fallback") + L"]"
    : L"TF2 Demo Player - TF2 资源未找到";
  const std::wstring titleBase = title + demoSuffix;
  auto entitySnapshotStatus = tf2::native::EntitySnapshotQueryStatus::NoHistory;
  // How far behind the requested tick the drawn snapshot is. Non-zero means the
  // entity picture is frozen at an older snapshot, so this is the number that
  // says whether what is on screen is tick-exact -- without it `entity=checkpoint`
  // reads the same for a 5-tick-old snapshot and a 13-minute-old one.
  std::int32_t entitySnapshotStaleness = 0;
  std::size_t hudAudioPlayed = 0;
  std::size_t hudAudioMissing = 0;
  std::size_t hudTrailVertices = 0;
  std::size_t hudEffectsDue = 0;
  tf2::native::PlaybackHudState hudState;
  // Standalone --model skinning readings, filled by reloadGpuResources() and
  // read by the title below. The single-model path is a second skinning
  // contract (one skeleton in b1) and it keeps its own readings rather than
  // sharing the entity counters, so a title can never mix the two.
  std::string modelSkinSource = "none";
  std::string modelSkinReason;
  int modelSkinFrame = 0;
  int modelSkinFrameCount = 0;
  std::size_t modelSkinBones = 0;
  std::size_t modelSkinUploaded = 0;
  // The title is a pure function of the playback state, so the last string is
  // kept and SetWindowTextW is only called when the string actually moves.
  // Without this the title was rebuilt and pushed once per loop iteration --
  // ~1000/s idle -- for a string that changes only when a tick changes.
  std::wstring lastTitle;
  const auto refreshWindowTitle = [&]() -> bool {
    if (!g_playback.enabled) {
      if (lastTitle == titleBase) return false;
      SetWindowTextW(window, titleBase.c_str());
      lastTitle = titleBase;
      return true;
    }
    if (!g_playback.anchorValid || g_playback.tick < g_playback.anchorLookupTick
        || g_playback.tick - g_playback.anchorLookupTick >= 32) {
      g_playback.anchorValid = tf2::native::findDemoPacketAtOrBeforeTick(demoIndex, g_playback.tick, g_playback.anchor);
      g_playback.anchorLookupTick = g_playback.tick;
    }
    const std::wstring anchorSuffix = g_playback.anchorValid
      ? L" anchorAbs=" + std::to_wstring(g_playback.anchor.tick) + L"@" + std::to_wstring(g_playback.anchor.offset)
      : L" anchor=none";
    const std::wstring playbackSuffix = L" playback=" + std::to_wstring(g_playback.tick) + L"/" + std::to_wstring(g_playback.endTick)
      + L" speed=" + std::to_wstring(g_playback.speed) + L" rate=" + std::to_wstring(g_playback.tickRate)
      + anchorSuffix + (g_playback.reverse ? L" reverse" : L" forward")
      + (g_playback.paused ? L" paused" : L" playing")
      + L" HUD[" + [&]() {
          // The five fields a gate asserts on -- tick, demo name, playback
          // state, camera mode, error status -- are composed by
          // composePlaybackHudLine (native_ui), the same function the probe
          // pins, so the product title and the probe-tested format cannot
          // drift apart. The demo name is the file stem (what the player
          // picked), not the map the server reported -- demoSuffix already
          // carries the map name.
          const std::string hudStateName = g_playback.paused ? "paused"
            : (g_playback.reverse ? "reverse" : "playing");
          const char* hudCameraName = tf2::native::cameraModeName(g_cameraMode.mode());
          const std::string hudDemoName = commandDemo.empty()
            ? std::string("-")
            // u8string(), not an iterator cast of wstring(): the wide->narrow
            // iterator cast loses data (C4244) and the gate builds are /W4.
            // Non-ASCII file names arrive UTF-8 here, matching how the title
            // already carries every other narrow string.
            : commandDemo.filename().u8string();
          const std::string hudError = g_nativeUi ? g_nativeUi->snapshot().error : std::string{};
          const std::string composed = tf2::native::composePlaybackHudLine(
            {g_playback.tick, hudDemoName, hudStateName, hudCameraName, hudError});
          return std::wstring(composed.begin(), composed.end());
        }()
      + L" speed=" + std::to_wstring(g_playback.speed)
      + L" entity=" + std::wstring(entitySnapshotStatusName(entitySnapshotStatus))
      + L" stale=" + std::to_wstring(entitySnapshotStaleness)
      + L" audio=" + std::to_wstring(hudAudioPlayed) + L"/" + std::to_wstring(hudAudioMissing)
      + L" health=" + (hudState.healthKnown ? std::to_wstring(hudState.health) : L"?")
      + L" team=" + (hudState.teamKnown ? std::to_wstring(hudState.team) : L"?")
      + L" class=" + (hudState.classKnown ? std::to_wstring(hudState.playerClass) : L"?")
      + L" clip=" + (hudState.clipKnown ? std::to_wstring(hudState.clip) : L"?")
      + L" uber=" + (hudState.uberKnown ? std::to_wstring(hudState.uberCharge) : L"?")
      + L" snd=" + std::to_wstring(soundKindCounts.weapon) + L"/"
      + std::to_wstring(soundKindCounts.footstep) + L"/" + std::to_wstring(soundKindCounts.uber)
      + L" fxDue=" + std::to_wstring(hudEffectsDue)
      + L" trailVerts=" + std::to_wstring(hudTrailVertices) + L"/1024"
      + L" cache=" + std::wstring(cacheStatus.begin(), cacheStatus.end())
      + (cacheDirectory.empty() ? L"" : L"@" + cacheDirectory.wstring())
      + L" hash=" + std::wstring(hashStatus.begin(), hashStatus.end())
      + (demoSha256.empty() ? L"" : L":" + std::wstring(demoSha256.begin(), demoSha256.end()))
      + L" modelMissing=" + std::to_wstring(modelCompanionMissing)
      + L" vpkHits=" + std::to_wstring(modelVpkHits)
      + L" attachment=" + std::wstring(modelAttachmentStatus == tf2::native::ModelFeatureStatus::Available ? L"available" : modelAttachmentStatus == tf2::native::ModelFeatureStatus::Missing ? L"missing" : L"unknown")
      + L" bodygroup=" + std::wstring(modelBodygroupStatus == tf2::native::ModelFeatureStatus::Available ? L"available" : modelBodygroupStatus == tf2::native::ModelFeatureStatus::Missing ? L"missing" : L"unknown")
      + L" viewmodel=" + std::wstring(modelViewModelStatus == tf2::native::ModelFeatureStatus::Available ? L"available" : modelViewModelStatus == tf2::native::ModelFeatureStatus::Missing ? L"missing" : L"unknown")
      + L" entityMeshes=" + std::to_wstring(renderer.entityModelMeshCount())
      + L" entityModels=" + std::to_wstring(renderer.entityModelInstanceCount())
      + L" entityModelVerts=" + std::to_wstring(renderer.entityModelVertexCount())
      + L" entityMaterials=" + std::to_wstring(renderer.entityModelTexturedInstanceCount()) + L"/" + std::to_wstring(renderer.entityModelInstanceCount())
      + L" entityMaterialRanges=" + std::to_wstring(renderer.entityModelTexturedRangeCount()) + L"/" + std::to_wstring(renderer.entityModelDrawRangeCount())
      + L" entityMaterialModels=" + std::to_wstring(entityMaterialUploadedModels) + L"/" + std::to_wstring(preparedEntityMeshes.size())
      // Animation, stated as the readings a gate can assert from outside the
      // process: how many instances actually got a pose, how many bones went to
      // the GPU, and -- when none did -- which model and which reason.
      + L" animSkinned=" + std::to_wstring(animationStats.instancesSkinned)
      + L"/" + std::to_wstring(animationStats.instancesSkinned + animationStats.instancesBindPose)
      + L" animBones=" + std::to_wstring(animationStats.boneMatricesUploaded)
      + L" animSeq=" + std::to_wstring(animationStats.lastSequence)
      + L" animFrame=" + std::to_wstring(animationStats.lastFrame) + L"/" + std::to_wstring(animationStats.lastFrameCount)
      + L" animSource=" + std::wstring(animationStats.lastStatus.begin(), animationStats.lastStatus.end())
      + L" animBindings=" + std::to_wstring(animationStats.bindingsUsable) + L"/" + std::to_wstring(animationStats.bindingsResolved)
      + L" animCache=" + std::to_wstring(animationStats.cacheHits)
      + L" animAdv=" + std::to_wstring(animationStats.frameAdvances)
      + L" animWitness=" + std::wstring(animationStats.witnessModel.empty()
          ? L"-" : std::wstring(animationStats.witnessModel.begin(), animationStats.witnessModel.end()))
      + L" gpuSkin=" + std::to_wstring(renderer.entitySkinnedInstanceCount()) + L"/" + std::to_wstring(renderer.entityBoneMatrixCount())
      + L" posePreflight=" + (modelPosePreflight ? L"ready" : L"unknown")
      // The standalone --model skinning contract, reported separately from the
      // entity counters above so the two can never be read as one number.
      + L" modelSkin=" + std::wstring(modelSkinSource.begin(), modelSkinSource.end())
      + L" modelSeq=" + std::to_wstring(modelSequence)
      + L" modelFrame=" + std::to_wstring(modelSkinFrame) + L"/" + std::to_wstring(modelSkinFrameCount)
      + L" modelBones=" + std::to_wstring(modelSkinUploaded) + L"/" + std::to_wstring(modelSkinBones)
      + L" bones=" + std::to_wstring(modelBoneCount)
      + L" attachments=" + std::to_wstring(modelAttachmentCount)
      + L" bodyParts=" + std::to_wstring(modelBodyPartCount)
      + L" resources=" + ((modelCompanionMissing == 0 && demoNetworkSummary.decodedSoundResourceMisses == 0) ? L"ok" : L"missing") + L"]"
      + L" export=unavailable(native-encoder-missing)";
    std::wstring title = titleBase + playbackSuffix;
    if (title == lastTitle) return false;
    SetWindowTextW(window, title.c_str());
    lastTitle = std::move(title);
    return true;
  };
  refreshWindowTitle();
  if (!renderer.initialize(window)) {
    // The HRESULT is the only thing that distinguishes "no D3D device" from "a
    // shader failed to compile at run time", and both exit the same way.
    traceFmt("gpu: initialize failed stage=%s hr=0x%08lX",
      renderer.initStage(), static_cast<unsigned long>(renderer.lastError()));
    DestroyWindow(window);
    return 12;
  }
  renderer.setWorldMaterialParams(mapMaterialParams);
  tf2::native::ModelMetadata modelMetadata;
  bool modelReady = false;
  if (!commandModel.empty()) {
    auto inspection = tf2::native::ModelLoader::inspect(commandModel);
    // A game model lives *inside* a VPK, which the loose-file inspect cannot
    // see. Without this fallback `--model` was only usable with loose custom
    // content under tf/download, and every model that actually ships with the
    // game (models/player/scout.mdl, models/weapons/...) came back with zero
    // bones and no way to tell "not installed" from "wrong loader". The path is
    // reduced to the VPK form first, then read through the same archive set the
    // entity path uses.
    if (!inspection.metadata.valid) {
      const std::string relative = normalizedModelPath(commandModel.string(), assets.tfDirectory);
      const tf2::native::VpkArchiveSet& archives = assets.archives();
      std::vector<std::filesystem::path> holders;
      archives.collectContaining(relative, holders);
      for (const auto& archivePath : holders) {
        const tf2::native::VpkArchive* archive = archives.find(archivePath);
        if (archive == nullptr || !archive->contains(relative)) continue;
        auto vpkInspection = tf2::native::ModelLoader::inspectVpk(*archive, relative);
        if (vpkInspection.metadata.valid) { inspection = std::move(vpkInspection); break; }
      }
    }
    if (!inspection.metadata.valid) {
      // A `--model` that resolves to nothing has to say so. Otherwise the run is
      // a silent no-op: no bone upload and no `gpu: model skin` line, and the
      // only difference from a correct run is a line that is *absent* -- which
      // reads identically to "this path was never asked to do anything". The
      // loose reader and the VPK reader disagree about where a model lives, so
      // the reason matters: "not installed" and "wrong loader" are different
      // defects and only one of them is fixable here.
      traceFmt("gpu: model inspect path=%s valid=0 mdl=%d mdlReason=%s vvd=%d vtx=%d",
        commandModel.string().c_str(),
        static_cast<int>(inspection.mdl.exists), inspection.mdl.diagnostic.c_str(),
        static_cast<int>(inspection.vvd.exists), static_cast<int>(inspection.vtx.exists));
    }
    modelAttachmentStatus = inspection.attachmentStatus;
    modelBodygroupStatus = inspection.bodygroupStatus;
    modelViewModelStatus = inspection.viewModelStatus;
    modelBoneCount = inspection.metadata.bones.size();
    modelAttachmentCount = inspection.metadata.attachments.size();
    modelBodyPartCount = inspection.metadata.bodyParts.size();
    modelCompanionMissing = static_cast<std::size_t>(!inspection.vvd.exists)
      + static_cast<std::size_t>(!inspection.vtx.exists && !inspection.vtxDx80.exists && !inspection.vtxSw.exists);
    modelVpkHits = inspection.metadata.valid && inspection.mdl.diagnostic.find("VPK") != std::string::npos ? 1u : 0u;
    if (!inspection.metadata.indices.empty()) {
      std::string modelError;
      modelReady = tf2::native::ModelLoader::buildBindPoseMesh(inspection.metadata, 0, modelError);
      modelPosePreflight = inspection.mdl.signatureValid && inspection.vvd.signatureValid
        && modelReady && modelBoneCount > 0;
      modelMetadata = std::move(inspection.metadata);
    }
  }
  // ---- First-person ViewModel (T2) ----------------------------------------
  // Built once, here, rather than per frame. The plan is explicit that the model
  // path, the companion status, the mesh, the attachment table and the bone
  // mapping are resolved when the demo or the TF root changes -- this run has
  // exactly one of each, so "once at startup" *is* the cache. Nothing below
  // re-opens the VPK or re-parses the MDL.
  tf2::native::ViewModelRequest viewModelRequest;
  const bool viewModelRequested = !commandViewModel.empty();
  bool viewModelMeshUploaded = false;
  // The pass state that survives the GPU upload. The animation fields are
  // refreshed on every scene update, so what the upload produced has to live
  // outside the reload lambda -- a local there would be thrown away on the first
  // frame after the upload and every later frame would draw with a
  // default-constructed state.
  tf2::native::ViewModelDrawState viewModelBaseState;
  // The transform the pass consumes, read off a request. Shared by the
  // `--viewmodel` model and the entity's own model so the two cannot disagree
  // about how an attachment becomes a draw state: `mstudioattachment_t::local`
  // as a row-major 3x4, the three basis vectors as its columns and the origin as
  // its translation column -- the "attachment local -> owning bone / model space"
  // stage of the chain.
  const auto viewModelDrawStateFor = [](const tf2::native::ViewModelRequest& request,
      bool leftHand) {
    tf2::native::ViewModelDrawState state;
    state.fov = request.fov;
    state.leftHand = leftHand;
    state.handTransform = request.handTransform;
    state.attachmentValid = request.alignmentIndex >= 0
      && static_cast<std::size_t>(request.alignmentIndex) < request.attachments.size();
    if (state.attachmentValid) {
      const auto& attachment = request.attachments[static_cast<std::size_t>(request.alignmentIndex)];
      state.attachmentName = attachment.name;
      state.attachmentTransform = {
        attachment.axisX[0], attachment.axisY[0], attachment.axisZ[0], attachment.origin[0],
        attachment.axisX[1], attachment.axisY[1], attachment.axisZ[1], attachment.origin[1],
        attachment.axisX[2], attachment.axisY[2], attachment.axisZ[2], attachment.origin[2],
      };
    }
    return state;
  };
  // The entity's own model: built and uploaded at most once per distinct path,
  // and rebuilt only when the path changes. `requestedPath` starts empty, which
  // is not a path any entity can name, so the first scene update always builds.
  //
  // It cannot be built in the startup block with `--viewmodel`: this path comes
  // from a demo entity, and at that point no demo packet has been read. That is
  // also why the upload lives on this side -- the mesh is a property of the
  // recording, not of the command line.
  struct ViewModelCompanionCache {
    std::string requestedPath;
    tf2::native::ViewModelRequest request;
    tf2::native::ViewModelDrawState baseState;
  };
  ViewModelCompanionCache viewModelCompanion;
  // The pass has ONE vertex buffer, so whichever model is drawn has to be the one
  // in it. This records which mesh is in it, and it is why a switch in either
  // direction -- `--viewmodel` -> companion, or companion -> `--viewmodel` when a
  // recording's entity stops naming a model -- re-uploads instead of drawing the
  // new state through the old model's geometry. Empty means "nothing is in it",
  // which is what a device reset leaves behind.
  std::string viewModelMeshBufferPath;
  // Which material table the periodic trace should describe. Filled by the scene
  // update, which is the only place that knows whether `--viewmodel`'s own model
  // or the entity's companion won; a lambda reading `viewModelRequest` directly
  // would report the model the pass was *told* to draw on a run where it drew the
  // recording's. A pointer rather than a copy, because the tables own the slot
  // vectors and the two requests outlive every frame.
  struct ViewModelMaterialReadings {
    const tf2::native::ViewModelMaterialTable* table = nullptr;
    const tf2::native::ViewModelRequest* request = nullptr;
    std::string decodeFailure;
    std::size_t decodedSlots = 0;
    bool flatColorForced = false;
    std::int32_t slotOverride = -1;
    std::int32_t missingSlotInjected = -1;
    bool materialPassRan = false;
  } viewModelMaterialReadings;

  // The ranges the pass draws, from the resolved table. One range today, because
  // the ViewModel mesh is built from one VTX index descriptor; the plumbing is a
  // list because the material chain is per-slot, and a second descriptor would
  // otherwise have to re-derive which slot it belongs to.
  //
  // `--viewmodel-flat-color` empties the key on purpose: it is the control the
  // pixel claim is measured against, and it is the same state a slot that failed
  // to resolve reaches, so the control and the fallback cannot diverge.
  const auto viewModelDrawRangesFor = [&](const tf2::native::ViewModelRequest& request) {
    std::vector<tf2::native::ViewModelDrawRange> ranges;
    if (!request.resources.meshReady || request.mesh.vertices.size() < 3) return ranges;
    tf2::native::ViewModelDrawRange range;
    range.firstVertex = 0;
    range.vertexCount = static_cast<std::uint32_t>(request.mesh.vertices.size());
    const std::int32_t slot = viewModelMaterialSlotOverride >= 0
      ? viewModelMaterialSlotOverride : request.materials.drawnSlot;
    if (!viewModelFlatColor && slot >= 0
        && static_cast<std::size_t>(slot) < request.materials.slots.size()) {
      const auto& materialSlot = request.materials.slots[static_cast<std::size_t>(slot)];
      if (materialSlot.pixelsDecoded && !materialSlot.vtfPath.empty()) {
        range.slot = slot;
        range.materialKey = materialSlot.vtfPath;
        range.translucent = materialSlot.translucent;
        range.alphaTest = materialSlot.alphaTest;
      }
    }
    ranges.push_back(std::move(range));
    return ranges;
  };
  const auto ensureViewModelMesh = [&](const std::string& path,
      tf2::native::ViewModelRequest& request) {
    if (!path.empty() && viewModelMeshBufferPath == path) return true;
    // The injection, before anything reads the table: it has to land where the
    // resolver's own verdict would land -- the status, the cleared pixel flag,
    // and the cleared texture path -- or the fallback would be reached by a
    // different route than a genuine failure takes, and the two would stop being
    // evidence for each other.
    std::int32_t missingSlotApplied = -1;
    if (viewModelMaterialMissingSlot >= 0
        && static_cast<std::size_t>(viewModelMaterialMissingSlot) < request.materials.slots.size()) {
      auto& injected = request.materials.slots[static_cast<std::size_t>(viewModelMaterialMissingSlot)];
      injected.status = tf2::native::ViewModelMaterialStatus::MissingMaterial;
      injected.reason = "injected by --viewmodel-material-missing-slot";
      injected.pixelsDecoded = false;
      injected.vtfPath.clear();
      injected.baseTexture.clear();
      injected.alpha = false;
      injected.translucent = false;
      injected.alphaTest = false;
      injected.bump = false;
      injected.phong = false;
      injected.selfIllum = false;
      missingSlotApplied = viewModelMaterialMissingSlot;
    }
    // The material pass first: the ranges are derived from what the decoder
    // produced, so running it after the upload would build ranges against a table
    // whose `pixelsDecoded` flags are all still false and bind nothing.
    const std::int32_t uploadSlot = viewModelFlatColor ? -1
      : (viewModelMaterialSlotOverride >= 0 ? viewModelMaterialSlotOverride : request.materials.drawnSlot);
    const auto summary = tf2::native::decodeViewModelMaterialSlots(request.materials, uploadSlot,
      [&](const std::string& key, std::uint32_t width, std::uint32_t height,
          const std::vector<std::uint8_t>& rgba) {
        return renderer.uploadViewModelMaterialTexture(key, rgba, width, height);
      });
    tf2::native::applyViewModelMaterialResult(request);
    viewModelMaterialReadings.table = &request.materials;
    viewModelMaterialReadings.request = &request;
    viewModelMaterialReadings.decodeFailure = summary.firstFailure;
    viewModelMaterialReadings.decodedSlots = summary.decoded;
    viewModelMaterialReadings.flatColorForced = viewModelFlatColor;
    viewModelMaterialReadings.slotOverride = viewModelMaterialSlotOverride;
    viewModelMaterialReadings.missingSlotInjected = missingSlotApplied;
    viewModelMaterialReadings.materialPassRan = true;
    traceFmt("gpu: viewmodel materials path=%s slots=%zu resolved=%zu decoded=%zu decodeFailed=%zu uploadSlot=%d uploads=%zu cache=%zu drawnSlot=%d source=%s meshTable=%d meshStride=%zu meshStrideExercised=%d meshCount=%zu descriptorsValidated=%zu strideSource=%s reason=%s",
      request.path.empty() ? "-" : request.path.c_str(),
      request.materials.materialSlots(), request.materials.materialResolved(),
      summary.decoded, summary.decodeFailed, uploadSlot, summary.uploaded,
      renderer.viewModelMaterialCacheSize(), request.materials.drawnSlot,
      request.materials.drawnSlotSource.empty() ? "-" : request.materials.drawnSlotSource.c_str(),
      request.materials.meshTableAvailable ? 1 : 0, request.materials.meshStride,
      request.materials.meshStrideExercised ? 1 : 0, request.materials.meshCount,
      request.materials.descriptorsValidated,
      request.materials.modelStrideSource.empty() ? "-" : request.materials.modelStrideSource.c_str(),
      summary.firstFailure.empty()
        ? (request.materials.drawnSlotReason.empty() ? "-" : request.materials.drawnSlotReason.c_str())
        : summary.firstFailure.c_str());
    const auto ranges = viewModelDrawRangesFor(request);
    const bool uploaded = renderer.uploadViewModelMesh(request.mesh.vertices, ranges);
    viewModelMeshBufferPath = uploaded ? path : std::string();
    traceFmt("gpu: viewmodel ranges path=%s ranges=%zu firstRange=(slot %d, %u vertices, textured=%d translucent=%d alphatest=%d) uploaded=%d",
      request.path.empty() ? "-" : request.path.c_str(), ranges.size(),
      ranges.empty() ? -1 : ranges.front().slot,
      ranges.empty() ? 0u : ranges.front().vertexCount,
      (!ranges.empty() && !ranges.front().materialKey.empty()) ? 1 : 0,
      (!ranges.empty() && ranges.front().translucent) ? 1 : 0,
      (!ranges.empty() && ranges.front().alphaTest) ? 1 : 0,
      uploaded ? 1 : 0);
    return uploaded;
  };
  // The weapon body's own slot, built once per distinct path for the same reason
  // the companion is: the path comes from a demo entity, so it cannot be built at
  // startup, and re-parsing the MDL per frame is what the plan forbids.
  //
  // `requestedPath` is assigned the NULL result too -- a recording whose weapon
  // entity stops naming a model has to drop the last body instead of keeping it
  // alive as a stale reading.
  struct ViewModelWeaponBodyCache {
    std::string requestedPath;
    tf2::native::ViewModelRequest request;
    tf2::native::ViewModelDrawState baseState;
  };
  ViewModelWeaponBodyCache viewModelWeaponBody;
  std::string viewModelWeaponMeshBufferPath;
  // Returns whether the body's mesh is in its slot; `uploaded` reports whether
  // THIS call is the one that put it there, so the reading can count uploads
  // rather than frames -- a per-frame upload is the defect the plan's "no
  // per-frame model or texture reload" line names, and a counter that
  // incremented on every no-op call would hide it.
  const auto ensureViewModelWeaponMesh = [&](const std::string& path,
      const std::vector<tf2::native::ModelDrawVertex>& vertices, bool& uploaded) {
    uploaded = false;
    if (!path.empty() && viewModelWeaponMeshBufferPath == path) return true;
    uploaded = renderer.uploadViewModelWeaponMesh(vertices);
    viewModelWeaponMeshBufferPath = uploaded ? path : std::string();
    return uploaded;
  };
  if (viewModelRequested) {
    const std::string relative = normalizedModelPath(commandViewModel.string(), assets.tfDirectory);
    // The stored setting is the source of truth; --viewmodel-fov is an explicit
    // override. The setting already went through migrateLegacyViewModelFov, so a
    // file written by the old build (which stored 80 as its default) reads as 90
    // here rather than pinning the ViewModel at the retired default.
    const float requestedFov = viewModelFovGiven ? viewModelFov : persistent.render.viewModelFov;
    viewModelRequest = tf2::native::buildViewModelRequest(assets, relative,
      viewModelLeftHand ? tf2::native::ViewModelHand::Left : tf2::native::ViewModelHand::Right,
      requestedFov);
    traceFmt("gpu: viewmodel request path=%s status=%s resolution=%d companions=%d meshReady=%d attachments=%zu valid=%zu partial=%zu invalid=%zu alignment=%s fov=%.1f hand=%s reason=%s",
      viewModelRequest.path.c_str(),
      viewModelRequest.status == tf2::native::ViewModelStatus::Ok ? "ok" : "failed",
      static_cast<int>(viewModelRequest.resolution),
      viewModelRequest.companionsComplete ? 1 : 0,
      viewModelRequest.resources.meshReady ? 1 : 0,
      viewModelRequest.attachmentCount, viewModelRequest.attachmentsValid,
      viewModelRequest.attachmentsPartial, viewModelRequest.attachmentsInvalid,
      viewModelRequest.alignmentName.empty() ? "-" : viewModelRequest.alignmentName.c_str(),
      viewModelRequest.fov, viewModelLeftHand ? "left" : "right",
      viewModelRequest.reason.empty() ? "-" : viewModelRequest.reason.c_str());
  }
  const auto reloadGpuResources = [&]() {
    traceCheckpoint("gpu: reload begin");
    bool uploadedTexture = mapRgba.empty() && displayRgba.empty();
    if (!mapRgba.empty()) uploadedTexture = renderer.uploadTexture(mapRgba, mapWidth, mapHeight);
    else if (!displayRgba.empty()) uploadedTexture = renderer.uploadTexture(displayRgba, displayWidth, displayHeight);
    bool uploadedWorld = displayMap.triangles.empty();
    if (!renderer.uploadWorldAuxTextures(mapBumpRgba, mapBumpWidth, mapBumpHeight, mapEnvRgba, mapEnvWidth, mapEnvHeight)) return false;
    if (!renderer.uploadWorldLightmap(displayMap.lightmapAtlas,
        static_cast<UINT>(displayMap.lightmapAtlasWidth), static_cast<UINT>(displayMap.lightmapAtlasHeight))) return false;
    if (!uploadedWorld && !worldTextures.empty()) uploadedWorld = renderer.uploadWorldGeometry(displayMap, worldTextures);
    if (!uploadedWorld) uploadedWorld = renderer.uploadWorldGeometry(displayMap, mapMaterialName, mapWidth, mapHeight);
    const bool uploadedModel = !modelReady || renderer.uploadBindPoseModel(modelMetadata.bindPoseVertices);
    entityMaterialUploadFlags.assign(preparedEntityMeshes.size(), 0);
    for (std::size_t preparedIndex = 0; preparedIndex < preparedEntityMeshes.size(); ++preparedIndex) {
      const double modelBeginMs = traceNowMs();
      const auto& prepared = preparedEntityMeshes[preparedIndex];
      renderer.uploadEntityModelMesh(prepared.cacheKey, prepared.vertices);
      if (prepared.materialVtfPath.empty()) continue;
      // Stage two of the paint path: decode one model at a time so only one
      // RGBA buffer is alive, then hand it to the renderer as that model's t0.
      const auto& preparedVtf = readMaterialVtf(prepared.materialVtfPath);
      tf2::native::VtfTexture preparedTexture;
      std::vector<std::uint8_t> preparedRgba;
      if (preparedVtf.empty() || !preparedTexture.parse(preparedVtf)
          || (preparedRgba = preparedTexture.decodeRgba(preparedVtf)).empty()) {
        traceFmt("gpu: model %zu/%zu decode-failed %.1fms %s", preparedIndex,
          preparedEntityMeshes.size(), traceNowMs() - modelBeginMs, prepared.materialVtfPath.c_str());
        continue;
      }
      const UINT preparedWidth = preparedTexture.header().width;
      const UINT preparedHeight = preparedTexture.header().height;
      const bool textureUploaded =
        renderer.uploadEntityModelTexture(prepared.cacheKey, preparedRgba, preparedWidth, preparedHeight);
      if (textureUploaded) {
        ++entityMaterialUploadedModels;
        entityMaterialUploadFlags[preparedIndex] = 1;
      }
      traceFmt("gpu: model %zu/%zu %ux%u %.1fms uploaded=%d", preparedIndex,
        preparedEntityMeshes.size(), preparedWidth, preparedHeight, traceNowMs() - modelBeginMs,
        textureUploaded ? 1 : 0);
    }
    traceFmt("gpu: models done %zu uploaded=%zu", preparedEntityMeshes.size(),
      entityMaterialUploadedModels);
    bool uploadedBones = true;
    modelSkinSource = "none";
    modelSkinReason.clear();
    modelSkinBones = 0;
    modelSkinUploaded = 0;
    if (modelReady && !modelMetadata.bones.empty()) {
      const std::size_t bindBoneCount = modelMetadata.bones.size();
      modelSkinBones = bindBoneCount;
      // The single-model path is a *different* contract from the entity path:
      // it uploads one skeleton into b1, so it can and must turn the skinning
      // branch on explicitly. Two inputs, two poses:
      //   no --model-sequence -> identity skin matrices. Every vertex lands
      //     where the bind pose put it, but the branch really runs.
      //   --model-sequence N  -> skin[i] = animatedWorld[i] * poseToBone[i],
      //     the same product contract the entity instances use.
      // The previous code uploaded poseToBone here with skinning off. That is
      // wrong in both directions: poseToBone is an *inverse* bind matrix, so
      // enabling the branch with it would drag a static model into bone-local
      // space, and leaving the branch off meant this path could not skin at all.
      const auto identityMatrices = [&]() {
        std::vector<std::array<float, 16>> identity;
        identity.reserve(bindBoneCount);
        for (std::size_t i = 0; i < bindBoneCount; ++i) {
          std::array<float, 16> matrix{};
          matrix[0] = 1.0f; matrix[5] = 1.0f; matrix[10] = 1.0f; matrix[15] = 1.0f;
          identity.push_back(matrix);
        }
        return identity;
      };
      std::vector<std::array<float, 16>> boneMatrices;
      if (modelSequence < 0) {
        boneMatrices = identityMatrices();
        modelSkinSource = "identity";
        modelSkinReason = "no --model-sequence";
      } else {
        tf2::native::AnimationInputStatus skinStatus = tf2::native::AnimationInputStatus::ModelMissing;
        std::string skinReason;
        const auto* binding = animationBindings.resolve(
          normalizedModelPath(commandModel.string(), assets.tfDirectory), skinStatus, skinReason);
        tf2::native::AnimationSampleRequest request;
        request.sequence = modelSequence;
        request.tick = static_cast<std::int32_t>(modelTick);
        request.tickRate = 30.0f;
        const auto sampled = binding != nullptr
          ? tf2::native::buildAnimationSkinMatrices(binding, request)
          : tf2::native::AnimationSampleResult{};
        // The matrices are indexed by the render skeleton's bone order, which is
        // the same order the vertex bone indices use. A count that does not
        // match the model's own bone count would silently mis-index every
        // vertex, so it is refused rather than uploaded.
        if (binding != nullptr && !sampled.bindPose && !sampled.skinMatrices.empty()
            && sampled.skinMatrices.size() == bindBoneCount) {
          boneMatrices = sampled.skinMatrices;
          modelSkinSource = "animated";
          modelSkinFrame = sampled.frame;
          modelSkinFrameCount = sampled.frameCount;
          modelSkinReason = sampled.sequenceLabel;
        } else {
          boneMatrices = identityMatrices();
          modelSkinSource = "identity";
          modelSkinReason = binding != nullptr ? sampled.reason : skinReason;
          if (binding != nullptr && !sampled.skinMatrices.empty()
              && sampled.skinMatrices.size() != bindBoneCount) {
            modelSkinReason = "skin bone count does not match the model skeleton";
          }
        }
      }
      const bool matricesUsable = boneMatrices.size() == bindBoneCount
        && boneMatrices.size() <= tf2::native::kMaxSkinningBones;
      uploadedBones = matricesUsable
        && renderer.uploadBoneMatrices(boneMatrices, modelSkinningEnabled);
      if (uploadedBones) modelSkinUploaded = boneMatrices.size();
      traceFmt("gpu: model skin source=%s bones=%zu uploaded=%zu skinning=%d seq=%d frame=%d/%d reason=%s",
        modelSkinSource.c_str(), modelSkinBones, modelSkinUploaded,
        modelSkinningEnabled ? 1 : 0, modelSequence, modelSkinFrame, modelSkinFrameCount,
        modelSkinReason.c_str());
    }
    // First-person ViewModel. Uploaded here so a device reset re-uploads it with
    // everything else. It is deliberately *not* folded into the return value: the
    // plan says a missing or damaged ViewModel disables the ViewModel pass, not
    // the run, and `drawn=false` plus a reason is how that is reported.
    viewModelMeshUploaded = false;
    // Both the buffer and the record of what is in it are invalidated here: a
    // reset frees the buffer, and the scene update re-uploads whichever model it
    // then decides to draw.
    viewModelMeshBufferPath.clear();
    // The weapon body's slot is freed by the same reset and re-uploaded the same
    // way -- it is a separate buffer, so it needs its own record of what was in
    // it; leaving this one behind would let a post-reset scene update skip the
    // re-upload and draw an empty slot as if it were the body.
    viewModelWeaponMeshBufferPath.clear();
    if (viewModelRequested) {
      tf2::native::ViewModelDrawState& viewModelState = viewModelBaseState;
      viewModelState = viewModelDrawStateFor(viewModelRequest, viewModelLeftHand);
      if (viewModelRequest.status == tf2::native::ViewModelStatus::Ok
          && viewModelRequest.resources.meshReady) {
        // Through the material pass, NOT straight to uploadViewModelMesh.
        //
        // The direct upload this replaces was a real defect rather than a
        // shortcut: it put the mesh in the buffer and recorded the buffer's
        // contents, so when the scene update reached `ensureViewModelMesh` for the
        // same path the early-return fired and the material pass never ran. The
        // drawn model then had no ranges at all, and `--viewmodel-material-slot`
        // and `--viewmodel-material-missing-slot` were both silently ignored --
        // the readings said `slotOverride=-1` on a run that had been given one.
        // It went unnoticed because the shipped recording draws the *companion*,
        // whose path differs from `--viewmodel`, so the pass ran for that one.
        viewModelMeshUploaded = ensureViewModelMesh(viewModelRequest.path, viewModelRequest);
        viewModelState.enabled = viewModelMeshUploaded;
        if (!viewModelMeshUploaded) viewModelState.reason = "the viewmodel mesh did not upload";
      } else {
        viewModelState.enabled = false;
        viewModelState.reason = viewModelRequest.reason.empty()
          ? std::string("the viewmodel request produced no mesh") : viewModelRequest.reason;
      }
      // The T1 instrument, reapplied to this pass: identity skin matrices through
      // the skinning branch must leave the bind pose exactly where it was, while
      // a real pose would move it. It exists because the honest state of this
      // pass is `animationInput=missing` -- nothing drives a ViewModel's sequence
      // -- and "the branch never ran" and "the branch ran and changed nothing"
      // are the two readings that state has to be separated from. The count must
      // cover the whole skeleton: the pass refuses a short table rather than
      // skinning with one.
      if (viewModelIdentityBones && viewModelMeshUploaded && !viewModelRequest.bones.empty()) {
        // The skeleton the mesh declares and the table the pass is handed: they
        // differ only when --viewmodel-bone-limit truncates it. boneCount is the
        // skeleton's size in both cases, which is what makes the pass notice the
        // shortfall instead of skinning with whatever it was given.
        const std::size_t skeletonBones = viewModelRequest.bones.size();
        const std::size_t tableBones = viewModelBoneLimit == 0
          ? skeletonBones : std::min(viewModelBoneLimit, skeletonBones);
        std::vector<std::array<float, 16>> identity;
        identity.reserve(tableBones);
        for (std::size_t index = 0; index < tableBones; ++index) {
          std::array<float, 16> matrix{};
          matrix[0] = 1.0f; matrix[5] = 1.0f; matrix[10] = 1.0f; matrix[15] = 1.0f;
          identity.push_back(matrix);
        }
        const bool bonesUploaded = renderer.uploadViewModelBones(identity);
        viewModelState.skinningEnabled = bonesUploaded;
        viewModelState.boneCount = bonesUploaded
          ? static_cast<std::uint32_t>(skeletonBones) : 0u;
        if (!bonesUploaded) viewModelState.reason = "the identity bone table did not upload";
      }
      renderer.setViewModelState(viewModelState);
      traceFmt("gpu: viewmodel upload mesh=%d vertices=%zu bones=%zu alignment=%s enabled=%d reason=%s",
        viewModelMeshUploaded ? 1 : 0, viewModelRequest.mesh.vertices.size(),
        viewModelRequest.bones.size(),
        viewModelState.attachmentName.empty() ? "-" : viewModelState.attachmentName.c_str(),
        viewModelState.enabled ? 1 : 0,
        viewModelState.reason.empty() ? "-" : viewModelState.reason.c_str());
    }
    traceCheckpoint("gpu: reload end");
    return uploadedTexture && uploadedWorld && uploadedModel && uploadedBones;
  };
  if (!reloadGpuResources()) {
    renderer.shutdown();
    g_renderer = nullptr;
    DestroyWindow(window);
    return 14;
  }
  // The paint ledger: one row per prepared model, written after the GPU upload
  // so `uploaded` reflects what the renderer actually accepted. This is the
  // per-model account behind the two window-title counters, and it is the only
  // place the *names* of the models without paint are visible.
  if (!dumpEntityMaterialsPath.empty()) {
    std::ofstream materialDump(dumpEntityMaterialsPath, std::ios::out | std::ios::trunc);
    if (!materialDump) {
      OutputDebugStringW(L"TF2 Demo Player: cannot open --dump-entity-materials.\n");
    } else {
      materialDump << "path\tcacheKey\tslots\tnamesRead\tmaterialName\tvmt\tvtf\tresolved\tuploaded\n";
      for (std::size_t index = 0; index < preparedEntityMeshes.size(); ++index) {
        const auto& prepared = preparedEntityMeshes[index];
        const bool uploaded = index < entityMaterialUploadFlags.size() && entityMaterialUploadFlags[index] != 0;
        materialDump << prepared.path << '\t' << prepared.cacheKey << '\t' << prepared.textureSlots << '\t'
          << prepared.textureNamesRead << '\t' << prepared.materialName << '\t' << prepared.materialVmtPath << '\t'
          << prepared.materialVtfPath << '\t' << (prepared.materialVtfPath.empty() ? 0 : 1) << '\t'
          << (uploaded ? 1 : 0) << '\n';
      }
      materialDump << "# summary models=" << preparedEntityMeshes.size()
        << " resolved=" << entityMaterialResolvedModels
        << " uploaded=" << entityMaterialUploadedModels << '\n';
    }
  }
  if (hiddenWindow) {
    // --hidden-window: the window must exist -- D3D11 needs the HWND, the swap
    // chain binds to it, and close messages still dispatch -- but it is never
    // shown and UpdateWindow is never called. The class was registered without
    // WS_VISIBLE and CreateWindowExW does not show a window, so it is already
    // hidden here; the SW_HIDE makes that an explicit act of this branch.
    ShowWindow(window, SW_HIDE);
  } else {
    ShowWindow(window, showCommand);
    UpdateWindow(window);
  }
  // The window half of the quiet-capture contract, read back through the API
  // rather than restated from the flag. Emitted in both modes, so the normal
  // run is the control for the quiet one.
  traceFmt("mode: window_created=%d window_visible=%d hidden=%d audio_mode=%s",
    window ? 1 : 0, IsWindowVisible(window) ? 1 : 0, hiddenWindow ? 1 : 0,
    audioDisabled ? "disabled" : "device");

  LARGE_INTEGER frequency{};
  LARGE_INTEGER lastFrame{};
  if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0
      || !QueryPerformanceCounter(&lastFrame)) {
    renderer.shutdown();
    g_renderer = nullptr;
    return 13;
  }
  std::ofstream metricsFile;
  if (!metricsPath.empty()) {
    metricsFile.open(metricsPath, std::ios::out | std::ios::trunc);
    if (!metricsFile) {
      OutputDebugStringW(L"TF2 Demo Player: cannot open --metrics-file.\n");
      renderer.shutdown();
      g_renderer = nullptr;
      return 15;
    }
    metricsFile << "elapsed_seconds,rendered_frames,fps,tick,working_set_bytes,private_bytes"
      << ",main_loop_iterations,ui_update_calls,title_update_calls,metrics_write_calls"
      << ",wait_calls,wait_ms_total\n";
    metricsFile.flush();
  }
  std::uint64_t renderedFrames = 0;
  std::uint64_t metricsFrames = 0;
  // Idle-spin counters. The loop used to wait at most 1 ms even when the next
  // frame was 15 ms away, so an idle window ran the body ~1000 times a second
  // and called the UI/title/metrics updaters just as often. These five numbers
  // are how a gate can tell "the loop stopped spinning" from "the loop looks
  // the same but the counters went up" -- rendered_frames is the denominator,
  // so every other counter reads as work-per-frame rather than as a raw rate.
  std::uint64_t mainLoopIterations = 0;
  std::uint64_t uiUpdateCalls = 0;
  std::uint64_t titleUpdateCalls = 0;
  std::uint64_t metricsWriteCalls = 0;
  // The two readings that say whether the wait branch was reached at all. The
  // frame loop only waits when a frame is not yet due; when the render itself
  // takes longer than kTargetFrameSeconds (120 Hz target, ~8.3 ms), `elapsed`
  // already exceeds it on the next pass and the loop renders back to back
  // without ever waiting. In that regime min(remainingMs,1.0) and
  // clamp(...,1,16) are the same code path, so a comparison between them is
  // vacuous -- these counters are how a gate can tell "the fix holds" from "the
  // fix was never exercised", which a bare loop-per-frame ratio cannot.
  std::uint64_t waitCalls = 0;
  std::uint64_t waitMsTotal = 0;
  const LARGE_INTEGER metricsStart = lastFrame;
  LARGE_INTEGER metricsLast = lastFrame;
  // The runtime readings the plan asks for, assembled once here so the periodic
  // trace and the window title cannot disagree about what they mean. Each field
  // is a reading, not a claim: `drawn` is the pass's own draw counter, and
  // `reason` is the string the pass recorded when it did not draw.
  const auto viewModelResourceStatusText = [&]() -> std::string {
    if (!viewModelRequested) return "none";
    if (viewModelRequest.resources.companionsComplete && viewModelRequest.resources.meshReady) return "ok";
    return "incomplete(mdl=" + std::to_string(viewModelRequest.resources.mdlReadable ? 1 : 0)
      + ",vvd=" + std::to_string(viewModelRequest.resources.vvdPresent ? 1 : 0)
      + ",vtx=" + std::to_string(viewModelRequest.resources.vtxPresent ? 1 : 0)
      + ",mesh=" + std::to_string(viewModelRequest.resources.meshReady ? 1 : 0) + ")";
  };
  const auto viewModelAttachmentStatusText = [&]() -> std::string {
    if (!viewModelRequested) return "none";
    if (viewModelRequest.attachmentCount == 0) return "absent";
    return std::string(viewModelRequest.attachmentsInvalid == 0 && viewModelRequest.attachmentsPartial == 0
      ? "ok" : "degraded")
      + "(valid=" + std::to_string(viewModelRequest.attachmentsValid)
      + ",partial=" + std::to_string(viewModelRequest.attachmentsPartial)
      + ",invalid=" + std::to_string(viewModelRequest.attachmentsInvalid)
      + ",total=" + std::to_string(viewModelRequest.attachmentCount) + ")";
  };
  const auto writeViewModelReading = [&]() {
    if (!viewModelRequested) return;
    // `fov` is the FOV the pass is actually using (the request's, after the
    // clamp), not the settings value: those two differ whenever --viewmodel-fov
    // overrides, and reporting the settings one would have hidden that.
    traceFmt("viewModelPath=%s resourceStatus=%s attachmentStatus=%s sequenceCount=%zu fov=%.1f settingsFov=%.1f hand=%s drawn=%zu reason=%s",
      viewModelRequest.path.empty() ? "-" : viewModelRequest.path.c_str(),
      viewModelResourceStatusText().c_str(),
      viewModelAttachmentStatusText().c_str(),
      viewModelRequest.sequenceLabels.size(),
      renderer.viewModelState().enabled ? renderer.viewModelState().fov : viewModelRequest.fov,
      renderer.viewModelFov(),
      viewModelLeftHand ? "left" : "right",
      renderer.viewModelDrawCount(),
      renderer.viewModelDrawReason().c_str());
    // The nine material readings, on one line, every sample.
    //
    // Eight of the nine describe the table the pass was handed; `texturesUploaded`
    // and `texturesReused` are the renderer's own counters, which is why they can
    // prove "no per-frame recreation" and the table alone cannot: a table that
    // resolved a slot looks identical whether the texture behind it was created
    // once or sixty times a second. `uploaded` is monotonic for the life of the
    // process; `reused` counts binds that found the key already in the cache, so a
    // steady `reused>0 uploaded` across frames is the reading that says so.
    //
    // `passRan=0` is reported rather than omitted: a run that never reached the
    // upload would otherwise print the same zeros as a model with no materials,
    // and those are different states.
    //
    // `phongSlots`/`bumpSlots`/`selfillumSlots` count what the RESOURCE SET
    // DECLARES, not what this pass applies -- on the shipped arms every slot
    // declares both a bumpmap and a phong exponent, so those readings are 3 while
    // the shader reads none of them. They are printed next to `*Applied=0`
    // because a reader who saw `phongSlots=3` alone could take it for support.
    // Phase 1 is albedo, alpha and the fallback; phong, bump and selfillum are
    // NOT implemented, and this line says so on every frame rather than leaving
    // it to a document.
    const auto& materialTable = viewModelMaterialReadings.table;
    traceFmt("gpu: viewmodel materialReadings passRan=%d materialSlots=%zu materialResolved=%zu texturesUploaded=%zu texturesReused=%zu fallbackSlots=%zu alphaSlots=%zu alphaAppliedRanges=%zu phongSlots=%zu phongAppliedRanges=0 bumpSlots=%zu bumpAppliedRanges=0 selfillumSlots=%zu selfillumAppliedRanges=0 decodedSlots=%zu drawRanges=%zu fallbackRanges=%zu boundSlots=%zu blendedRanges=%zu alphaTestedRanges=%zu flatColor=%d slotOverride=%d missingSlotInjected=%d decodeFailure=%s",
      viewModelMaterialReadings.materialPassRan ? 1 : 0,
      materialTable ? materialTable->materialSlots() : 0u,
      materialTable ? materialTable->materialResolved() : 0u,
      renderer.viewModelTexturesUploaded(),
      renderer.viewModelTexturesReused(),
      materialTable ? materialTable->fallbackSlots() : 0u,
      materialTable ? materialTable->alphaSlots() : 0u,
      renderer.viewModelBlendedRanges() + renderer.viewModelAlphaTestedRanges(),
      materialTable ? materialTable->phongSlots() : 0u,
      materialTable ? materialTable->bumpSlots() : 0u,
      materialTable ? materialTable->selfillumSlots() : 0u,
      viewModelMaterialReadings.decodedSlots,
      renderer.viewModelDrawRangeCount(),
      renderer.viewModelFallbackRanges(),
      renderer.viewModelBoundSlots(),
      renderer.viewModelBlendedRanges(),
      renderer.viewModelAlphaTestedRanges(),
      viewModelMaterialReadings.flatColorForced ? 1 : 0,
      viewModelMaterialReadings.slotOverride,
      viewModelMaterialReadings.missingSlotInjected,
      viewModelMaterialReadings.decodeFailure.empty()
        ? "-" : viewModelMaterialReadings.decodeFailure.c_str());
    // The draw-order reading: the world pass is ordinal 1 and the ViewModel pass
    // must be later. Without this, "world then ViewModel" would only be a claim
    // about the order of two statements in draw().
    // `skinning` is what the pass *did*; `boneTableCoverage` is the table it was
    // handed against the skeleton the mesh declares. Read together:
    // `skinning=0 boneTableCoverage=1/45` is a refused table, `skinning=0
    // boneTableCoverage=0/0` is no table at all, and `skinning=1
    // boneTableCoverage=45/45` is a skinned draw. Without the pair, the refusal
    // and the no-table case are both just `skinning=0`.
    traceFmt("gpu: viewmodel pass order worldPass=%zu viewModelPass=%zu afterWorld=%d drawCalls=%zu vertices=%zu depthWrite=%d depthFunc=%s skinning=%d boneTableCoverage=%zu/%zu animationInput=%s meshRadius=%.2f alignment=%s vmAnimInput=%s vmAnimApplied=%d drawnModel=%s entityModelMatchesDrawn=%d sequenceInRange=%d sequenceAccepted=%d sequenceOverride=%d modelMismatch=%zu boneMismatch=%zu bindPose=%d",
      renderer.viewModelWorldPassOrdinal(), renderer.viewModelPassOrdinal(),
      renderer.viewModelPassOrdinal() > renderer.viewModelWorldPassOrdinal() ? 1 : 0,
      renderer.viewModelDrawCallCount(), renderer.viewModelVertexCount(),
      renderer.viewModelDepthWrite() ? 1 : 0, renderer.viewModelDepthFuncName().c_str(),
      renderer.viewModelSkinningEnabled() ? 1 : 0,
      renderer.viewModelBoneUploadCount(),
      // %zu reads a 64-bit value and this field is a uint32_t; on x64 the
      // upper half of the register is not guaranteed zeroed, so the first
      // order line of a run printed a bone count of 4598175219545276416 (the
      // bit pattern of 0.25). The gate read `tail -1` and never saw it.
      static_cast<std::size_t>(renderer.viewModelState().boneCount),
      renderer.viewModelAnimationInput().c_str(),
      renderer.viewModelMeshRadius(),
      renderer.viewModelState().attachmentName.empty() ? "-" : renderer.viewModelState().attachmentName.c_str(),
      renderer.viewModelState().animationStatus.empty() ? "missing" : renderer.viewModelState().animationStatus.c_str(),
      renderer.viewModelState().animationApplied ? 1 : 0,
      // The companion proof, on the pass's own line. `drawnModel` is the model
      // whose mesh is in the vertex buffer, which is what makes
      // `entityModelMatchesDrawn` a statement about the picture rather than about
      // the contract's inputs.
      viewModelAnimationStats.lastDrawnModel.empty() ? "-" : viewModelAnimationStats.lastDrawnModel.c_str(),
      viewModelAnimationStats.lastEntityModelMatchesDrawn ? 1 : 0,
      viewModelAnimationStats.lastSequenceInRange ? 1 : 0,
      viewModelAnimationStats.lastSequenceAccepted ? 1 : 0,
      viewModelSequenceOverride,
      viewModelAnimationStats.modelMismatch,
      viewModelAnimationStats.boneMismatch,
      viewModelAnimationStats.lastBindPose ? 1 : 0);
    const auto& clip = renderer.viewModelCenterClip();
    const auto& viewSpace = renderer.viewModelCenterViewSpace();
    const bool onScreen = clip[3] > 0.0f
      && std::fabs(clip[0]) <= clip[3] && std::fabs(clip[1]) <= clip[3];
    const float* eye = renderer.frameEye();
    const float* forward = renderer.frameForward();
    const float* target = renderer.viewModelPlacementTarget();
    traceFmt("gpu: viewmodel frame eye=(%.2f,%.2f,%.2f) forward=(%.3f,%.3f,%.3f) placement=(%.2f,%.2f,%.2f)",
      eye[0], eye[1], eye[2], forward[0], forward[1], forward[2],
      target[0], target[1], target[2]);
    traceFmt("gpu: viewmodel centre viewSpace=(%.2f,%.2f,%.2f) clip=(%.3f,%.3f,%.3f,%.3f) ndc=(%.3f,%.3f) onScreen=%d",
      viewSpace[0], viewSpace[1], viewSpace[2],
      clip[0], clip[1], clip[2], clip[3],
      clip[3] != 0.0f ? clip[0] / clip[3] : 0.0f,
      clip[3] != 0.0f ? clip[1] / clip[3] : 0.0f,
      onScreen ? 1 : 0);
    // ---- The weapon body's reading ----------------------------------------
    // One line, and every field is a reading rather than a claim. The two draw
    // call counts are the pass's own counters, each incremented at its own
    // Draw, so "the arms and the body are two draw requests" is a number here
    // and not a statement about the shape of drawViewModel. The index and the
    // path are what the weapon entity said; `weaponMaterials` is how far the
    // .vmt/.vtf chain got, spelled slots/vmt/vtf/named because a bare ratio
    // cannot say whether a model with no material slots and a model whose
    // materials all failed are the same picture (they are the same picture, and
    // they are not the same defect).
    //
    // `weaponVisible` is the body's own centre through its own mvp -- the same
    // on-screen test the arms centre gets -- so "the body drew but landed off
    // screen" stays distinguishable from "the body did not draw". It is only
    // meaningful together with `weaponDrawCalls`: a body that never drew has no
    // clip of its own and reads not-visible, which is why the pair is printed
    // on one line.
    const auto& weaponClip = renderer.viewModelWeaponCenterClip();
    const bool weaponOnScreen = weaponClip[3] > 0.0f
      && std::fabs(weaponClip[0]) <= weaponClip[3] && std::fabs(weaponClip[1]) <= weaponClip[3];
    traceFmt("gpu: viewmodel weaponBodyReading armDrawCalls=%zu weaponDrawCalls=%zu weaponModelIndex=%d weaponModelPath=%s weaponMaterials=%zu/%zu/%zu/%zu weaponFallback=%s weaponVisible=%d weaponReason=%s weaponEntity=%d weaponClass=%s weaponMeshRadius=%.2f weaponVertices=%u weaponClip=(%.3f,%.3f,%.3f,%.3f) weaponMaterialsFailure=%s",
      renderer.viewModelArmDrawCallCount(), renderer.viewModelWeaponDrawCallCount(),
      viewModelWeaponStats.lastWorldModelIndex,
      viewModelWeaponStats.lastPath.empty() ? "-" : viewModelWeaponStats.lastPath.c_str(),
      viewModelWeaponStats.lastMaterialSlots, viewModelWeaponStats.lastMaterialVmt,
      viewModelWeaponStats.lastMaterialVtf, viewModelWeaponStats.lastMaterialNamed,
      viewModelWeaponStats.lastFallback.c_str(),
      weaponOnScreen ? 1 : 0,
      renderer.viewModelWeaponDrawReason().c_str(),
      viewModelWeaponStats.lastWeaponEntity,
      viewModelWeaponStats.lastWeaponClass.empty() ? "-" : viewModelWeaponStats.lastWeaponClass.c_str(),
      renderer.viewModelWeaponMeshRadius(),
      renderer.viewModelWeaponVertexCount(),
      weaponClip[0], weaponClip[1], weaponClip[2], weaponClip[3],
      viewModelWeaponStats.lastMaterialFailure.empty()
        ? "-" : viewModelWeaponStats.lastMaterialFailure.c_str());
  };
  const auto writeMetrics = [&](LARGE_INTEGER sample, bool force) {
    if (!metricsFile) return;
    const double interval = static_cast<double>(sample.QuadPart - metricsLast.QuadPart)
      / static_cast<double>(frequency.QuadPart);
    if (!force && interval < 1.0) return;
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (!GetProcessMemoryInfo(GetCurrentProcess(),
        reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters))) {
      counters.WorkingSetSize = 0;
      counters.PrivateUsage = 0;
    }
    const double elapsedSeconds = static_cast<double>(sample.QuadPart - metricsStart.QuadPart)
      / static_cast<double>(frequency.QuadPart);
    const double fps = interval > 0.0
      ? static_cast<double>(renderedFrames - metricsFrames) / interval : 0.0;
    metricsFile << elapsedSeconds << ',' << renderedFrames << ',' << fps << ','
      << g_playback.tick << ',' << counters.WorkingSetSize << ','
      << counters.PrivateUsage << ','
      << mainLoopIterations << ',' << uiUpdateCalls << ','
      << titleUpdateCalls << ',' << metricsWriteCalls << ','
      << waitCalls << ',' << waitMsTotal << '\n';
    metricsFile.flush();
    traceFmt("metrics t=%.2f tick=%d iter=%llu", elapsedSeconds, static_cast<int>(g_playback.tick),
      static_cast<unsigned long long>(mainLoopIterations));
    writeViewModelReading();
    ++metricsWriteCalls;
    metricsLast = sample;
    metricsFrames = renderedFrames;
  };
  unsigned deviceRecoveryAttempts = 0;
  MSG message{};
  bool running = true;
  double tickAccumulator = 0.0;
  std::int32_t lastAudioTick = -1;
  std::int32_t lastSceneTick = -1;
  bool lastReverse = false;
  bool lastPaused = false;
  std::vector<tf2::native::EntityState> currentEntityStates;
  soundScheduler.reset(0);
  // `--capture-frame` is a one-shot diagnostic: arm it, wait until playback has
  // reached the requested tick, write one frame, then leave through the exit
  // code. A gate therefore never has to guess when to kill the process.
  bool captureArmed = !captureFramePath.empty();
  bool captureRequested = false;
  while (running) {
    ++mainLoopIterations;
    traceFmt("loop n=%llu tick=%d", static_cast<unsigned long long>(mainLoopIterations),
      static_cast<int>(g_playback.tick));
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      if (message.message == WM_QUIT) { running = false; break; }
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
    if (!running) break;

    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    const double measuredElapsed = static_cast<double>(now.QuadPart - lastFrame.QuadPart)
      / static_cast<double>(frequency.QuadPart);
    // Do not fast-forward a long interval after a debugger break, resize, or
    // temporary OS stall. The next frame resumes at the current playback rate.
    const double elapsed = std::clamp(measuredElapsed, 0.0, 0.25);
    if (measuredElapsed > 0.25) tickAccumulator = 0.0;
    if (g_playback.paused != lastPaused) {
      if (g_playback.paused) soundScheduler.stop();
      else soundScheduler.reset(g_playback.tick);
      lastPaused = g_playback.paused;
    }
    if (g_playback.enabled && !g_playback.paused) {
      const bool directionChanged = g_playback.reverse != lastReverse;
      if (directionChanged) {
        tickAccumulator = 0.0;
        lastReverse = g_playback.reverse;
        soundScheduler.reset(g_playback.tick);
      }
      tickAccumulator += elapsed * g_playback.tickRate * g_playback.speed;
      while (tickAccumulator >= 1.0) {
        if (g_playback.reverse) {
          if (g_playback.tick <= 0) { tickAccumulator = 0.0; break; }
          --g_playback.tick;
        } else {
          if (g_playback.tick >= g_playback.endTick) { tickAccumulator = 0.0; break; }
          ++g_playback.tick;
        }
        tickAccumulator -= 1.0;
      }
      if (!g_playback.reverse) {
        if (g_playback.tick < lastAudioTick) soundScheduler.reset(g_playback.tick);
        float listenerX = 0.0f, listenerY = 0.0f, listenerZ = 0.25f;
        if (renderer.observerFocusWorld(listenerX, listenerY, listenerZ)) {
          soundScheduler.setListenerPosition(listenerX, listenerY, listenerZ);
        }
        soundScheduler.advance(soundTimeline, g_playback.tick);
        // One line per tick the playback cursor actually moves. This is the
        // reading that separates "the scheduler advanced and found nothing" from
        // "the scheduler never advanced": the summary counters alone cannot tell
        // them apart, because both end with a small eventsSeen.
        if (g_playback.tick != lastAudioTick) {
          const auto& advanceStats = soundScheduler.stats();
          traceFmt("audio: advance tick=%d cursor=%d seen=%zu played=%zu skippedVoice=%zu"
            " missing=%zu dup=%zu",
            static_cast<int>(g_playback.tick), static_cast<int>(soundScheduler.lastTick()),
            advanceStats.eventsSeen, advanceStats.eventsPlayed, advanceStats.eventsSkippedVoice,
            advanceStats.eventsMissingResource, advanceStats.duplicateTicks);
        }
        hudAudioPlayed = soundScheduler.stats().eventsPlayed;
        hudAudioMissing = soundScheduler.stats().eventsMissingResource;
        lastAudioTick = g_playback.tick;
      }
    }
    if (g_renderer && g_playback.enabled && g_playback.tick != lastSceneTick) {
      const std::int32_t sceneTick = firstPacketTick + g_playback.tick;
      traceFmt("scene: begin tick=%d sceneTick=%d", static_cast<int>(g_playback.tick),
        static_cast<int>(sceneTick));
      std::int32_t resolvedSceneTick = sceneTick;
      entitySnapshotStatus = tf2::native::queryEntitySnapshotAtOrBeforeTick(
          demoNetworkSummary, sceneTick, currentEntityStates, &resolvedSceneTick);
      entitySnapshotStaleness = sceneTick - resolvedSceneTick;
      traceFmt("scene: snapshot status=%d stale=%d states=%zu",
        static_cast<int>(entitySnapshotStatus), static_cast<int>(entitySnapshotStaleness),
        currentEntityStates.size());
      tf2::native::DemoViewSample observerView;
      const bool haveObserverView =
        tf2::native::findObserverViewAtOrBeforeTick(demoNetworkSummary, sceneTick, observerView)
        && observerView.hasOrigin && observerView.hasAngles;
      // T4 camera modes. The mode machine and the no-target rule live in
      // native_ui (decideCameraSceneAction, probe-checked over the whole
      // matrix); this block only executes the decision. The historical
      // behaviour -- clearing the demo view whenever a tick had no observe
      // target -- is exactly the "moved the camera without a target" defect
      // the task names, and HoldCurrent is what replaces it.
      const tf2::native::CameraMode cameraMode = g_cameraMode.mode();
      if (cameraMode != g_lastAppliedCameraMode) {
        if (cameraMode != tf2::native::CameraMode::FirstPerson) {
          renderer.clearObserverDemoView();
        }
        g_lastAppliedCameraMode = cameraMode;
      }
      switch (tf2::native::decideCameraSceneAction(cameraMode, haveObserverView)) {
        case tf2::native::CameraSceneAction::SetDemoView:
          renderer.setObserverDemoViewWorld(observerView.origin[0], observerView.origin[1],
            observerView.origin[2], observerView.angles[0], observerView.angles[1]);
          ++g_cameraFollows;
          break;
        case tf2::native::CameraSceneAction::SetOrbitFocus:
          renderer.setObserverFocusWorld(observerView.origin[0], observerView.origin[1],
            observerView.origin[2]);
          ++g_cameraFollows;
          break;
        case tf2::native::CameraSceneAction::HoldCurrent:
          // No observe target at this tick: keep the current camera. No
          // clear, no focus update, no teleport to the orbit default.
          ++g_cameraHoldsNoTarget;
          break;
        case tf2::native::CameraSceneAction::FreezeCurrent:
          // Free-look: neither eye nor focus is fed from the demo, so the
          // camera stays exactly where the user's orbit left it.
          ++g_cameraFreezeFrames;
          break;
      }
      traceFmt("scene: camera mode=%s target=%d follows=%u holds=%u freeze=%u",
        tf2::native::cameraModeName(cameraMode), haveObserverView ? 1 : 0,
        g_cameraFollows, g_cameraHoldsNoTarget, g_cameraFreezeFrames);
      traceCheckpoint("scene: observer done");
      const bool snapshotDrawable = entitySnapshotStatus == tf2::native::EntitySnapshotQueryStatus::Available
          || entitySnapshotStatus == tf2::native::EntitySnapshotQueryStatus::Checkpoint;
      hudState = snapshotDrawable
        ? tf2::native::readPlaybackHud(currentEntityStates, demoNetworkSummary.lastViewEntity)
        : tf2::native::PlaybackHudState{};
      hudEffectsDue = effectTimeline.countThrough(sceneTick);
      traceFmt("scene: hud fxDue=%d drawable=%d", static_cast<int>(hudEffectsDue),
        snapshotDrawable ? 1 : 0);
      if (snapshotDrawable) {
        // TEMP DIAGNOSTIC: the last checkpoint before the unhandled 0xE06D7363
        // is the one above, so the throw is inside this call. Print what() and
        // both sizes before rethrowing, so nothing is hidden by the tracing.
        const auto buildGuarded = [&]() {
          try {
            return tf2::native::EntityModelResolver::buildInstances(
              modelRenderRequests, currentEntityStates, demoNetworkSummary.serverClassSchemas, 256);
          } catch (const std::exception& error) {
            traceFmt("scene: buildInstances THREW %s requests=%zu states=%zu schemas=%zu",
              error.what(), modelRenderRequests.size(), currentEntityStates.size(),
              demoNetworkSummary.serverClassSchemas.size());
            throw;
          } catch (...) {
            traceFmt("scene: buildInstances THREW non-std requests=%zu states=%zu",
              modelRenderRequests.size(), currentEntityStates.size());
            throw;
          }
        };
        const auto modelInstances = buildGuarded();
        traceFmt("scene: buildInstances=%zu", modelInstances.size());
        std::vector<tf2::native::EntityModelDrawInstance> draws;
        std::vector<tf2::native::EntityMarker> fallbackMarkers;
        // Every skinned instance's matrices, concatenated. One upload per scene
        // update; the instances index into it by boneOffset.
        std::vector<std::array<float, 16>> entityBoneMatrices;
        draws.reserve(96);
        fallbackMarkers.reserve(32);
        entityBoneMatrices.reserve(1024);
        if (animationStats.hasObservedTick && sceneTick < animationStats.lastObservedTick) {
          animationStats = AnimationSceneStats{};
        }
        animationStats.hasObservedTick = true;
        animationStats.lastObservedTick = sceneTick;
        animationStats.resetPerUpdate();
        // The properties that drive playback, read through the same resolver
        // suffix rule the transform path uses. A property the entity does not
        // carry is reported as missing rather than defaulted to sequence 0 --
        // "no sequence" and "sequence 0" are different states and only one of
        // them means the model should move.
        const auto readPlaybackState = [&](std::uint16_t entityIndex,
            tf2::native::AnimationSampleRequest& request, std::string& missingReason) {
          request.sequence = -1;
          request.hasCycle = false;
          request.hasPlaybackRate = false;
          if (entityIndex >= currentEntityStates.size()) { missingReason = "no entity state"; return; }
          const auto& state = currentEntityStates[entityIndex];
          for (const auto& entry : state.properties) {
            const auto& name = entry.first;
            const auto matches = [&](const char* suffix) {
              const std::size_t length = std::strlen(suffix);
              if (name.size() < length) return false;
              if (name.compare(name.size() - length, length, suffix) != 0) return false;
              return name.size() == length || name[name.size() - length - 1] == '.';
            };
            if (matches("m_nSequence")) {
              const auto& value = entry.second;
              request.sequence = value.type == tf2::native::SendPropType::Float
                ? static_cast<std::int32_t>(value.x) : static_cast<std::int32_t>(value.intValue);
            } else if (matches("m_flCycle")) {
              const auto& value = entry.second;
              request.cycle = value.type == tf2::native::SendPropType::Float
                ? value.x : static_cast<float>(value.intValue);
              request.hasCycle = true;
            } else if (matches("m_flPlaybackRate")) {
              const auto& value = entry.second;
              request.playbackRate = value.type == tf2::native::SendPropType::Float
                ? value.x : static_cast<float>(value.intValue);
              request.hasPlaybackRate = true;
            }
          }
          if (request.sequence < 0) missingReason = "m_nSequence is absent";
        };
        bool focused = false;
        const auto tryFocusOrigin = [&](float x, float y, float z) {
          if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return false;
          renderer.setObserverFocusWorld(x, y, z);
          return true;
        };
        if (demoNetworkSummary.lastViewEntity < currentEntityStates.size()) {
          const auto focus = tf2::native::EntityModelResolver::extractTransform(
            currentEntityStates[demoNetworkSummary.lastViewEntity]);
          if (focus.hasOrigin) focused = tryFocusOrigin(focus.origin[0], focus.origin[1], focus.origin[2]);
        }
        for (const auto& instance : modelInstances) {
          if (!instance.transform.hasOrigin) continue;
          const bool player = instance.className.find("Player") != std::string::npos
            || instance.className.find("TFPlayer") != std::string::npos
            || instance.playerClassFallback;
          if (!focused && player) {
            focused = tryFocusOrigin(instance.transform.origin[0], instance.transform.origin[1], instance.transform.origin[2]);
          }
          if (instance.viewModelSkipped) continue;
          std::string cacheKey = instance.cacheKey;
          if (cacheKey.empty() || entityMeshKeyByPath.count(instance.modelPath)) {
            const auto found = entityMeshKeyByPath.find(instance.modelPath);
            if (found != entityMeshKeyByPath.end()) cacheKey = found->second;
          }
          const bool hasMesh = !cacheKey.empty() && !instance.viewModelSkipped
            && (instance.renderable || instance.playerClassFallback);
          if (hasMesh && draws.size() < 96) {
            tf2::native::EntityModelDrawInstance draw;
            draw.cacheKey = cacheKey;
            draw.origin[0] = instance.transform.origin[0];
            draw.origin[1] = instance.transform.origin[1];
            draw.origin[2] = instance.transform.origin[2];
            draw.hasAngles = instance.transform.hasAngles;
            draw.angles[0] = instance.transform.angles[0];
            draw.angles[1] = instance.transform.angles[1];
            draw.angles[2] = instance.transform.angles[2];
            if (instance.transform.hasTeam && instance.transform.team == 2) {
              draw.color[0] = 0.86f; draw.color[1] = 0.28f; draw.color[2] = 0.22f; draw.color[3] = 1.0f;
            } else if (instance.transform.hasTeam && instance.transform.team == 3) {
              draw.color[0] = 0.28f; draw.color[1] = 0.52f; draw.color[2] = 0.86f; draw.color[3] = 1.0f;
            } else {
              draw.color[0] = 0.72f; draw.color[1] = 0.74f; draw.color[2] = 0.70f; draw.color[3] = 1.0f;
            }
            // Animation, when this entity has a sequence and the model has a
            // table to look it up in. Anything short of a complete pose leaves
            // the instance in the bind-pose path and records why.
            tf2::native::AnimationInputStatus animStatus = tf2::native::AnimationInputStatus::ModelMissing;
            std::string animReason;
            const auto* binding = animationBindings.resolve(instance.modelPath, animStatus, animReason);
            ++animationStats.bindingsResolved;
            if (binding != nullptr) ++animationStats.bindingsUsable;
            tf2::native::AnimationSampleRequest request;
            request.tick = resolvedSceneTick;
            // The demo's own tick rate, not a constant: it is what turns a tick
            // into an animation cursor, and it varies with the recording.
            request.tickRate = static_cast<float>(g_playback.tickRate);
            std::string missingReason;
            readPlaybackState(instance.entityIndex, request, missingReason);
            if (request.hasCycle) {
              ++animationStats.cycleInstances;
              if (request.cycle != 0.0f) ++animationStats.cycleNonZero;
              const auto previousCycle = animationStats.lastCycleByEntity.find(instance.entityIndex);
              if (previousCycle != animationStats.lastCycleByEntity.end()
                  && previousCycle->second != request.cycle) {
                ++animationStats.cycleChanges;
              }
              animationStats.lastCycleByEntity[instance.entityIndex] = request.cycle;
            }
            if (binding != nullptr) {
              const auto sampled = tf2::native::buildAnimationSkinMatrices(binding, request);
              if (!sampled.bindPose && !sampled.skinMatrices.empty()) {
                draw.skinningEnabled = true;
                draw.boneOffset = static_cast<std::uint32_t>(entityBoneMatrices.size());
                draw.boneCount = static_cast<std::uint32_t>(sampled.skinMatrices.size());
                if (sampled.skinBones != sampled.validBones
                    || sampled.skinBones != sampled.skinMatrices.size()) {
                  ++animationStats.boneCountMismatches;
                }
                entityBoneMatrices.insert(entityBoneMatrices.end(),
                  sampled.skinMatrices.begin(), sampled.skinMatrices.end());
                ++animationStats.instancesSkinned;
                animationStats.lastSequence = request.sequence;
                animationStats.lastFrame = sampled.frame;
                animationStats.lastFrameCount = sampled.frameCount;
                animationStats.lastStatus = tf2::native::animationInputStatusName(sampled.status);
                animationStats.lastModel = instance.modelPath;
                animationStats.lastReason = sampled.sequenceLabel;
                // Two readings that fail independently, kept apart on purpose:
                //   * the frame cursor advancing per tick (advances / target)
                //   * the uploaded pose actually changing (poseChanges / witness)
                // A sequence can advance its cursor through a block whose bone
                // data is one raw pose -- TF2 props do exactly that -- in which
                // case the first is true and the second is false. That is a
                // property of the asset, not of the wiring, and collapsing the
                // two into one number would report a static asset as a wiring
                // failure.
                const auto previous = animationStats.lastFrameByEntity.find(instance.entityIndex);
                const bool frameAdvanced = previous != animationStats.lastFrameByEntity.end()
                  && previous->second != sampled.frame;
                if (frameAdvanced) {
                  ++animationStats.frameAdvances;
                  ++animationStats.changesByEntity[instance.entityIndex];
                }

                std::uint64_t poseHash = 1469598103934665603ull;
                for (const auto& matrix : sampled.skinMatrices) {
                  for (const float value : matrix) {
                    std::uint32_t bits = 0;
                    std::memcpy(&bits, &value, sizeof(bits));
                    poseHash = (poseHash ^ bits) * 1099511628211ull;
                  }
                }
                const auto previousPose = animationStats.poseHashByEntity.find(instance.entityIndex);
                const bool poseChanged = previousPose != animationStats.poseHashByEntity.end()
                  && previousPose->second != poseHash;
                animationStats.poseHashByEntity[instance.entityIndex] = poseHash;
                if (poseChanged) {
                  ++animationStats.poseChanges;
                  // The witness is the entity whose uploaded pose has been
                  // caught changing most often: the one the picture visibly
                  // moves for.
                  const int changes = ++animationStats.poseChangesByEntity[instance.entityIndex];
                  if (changes > static_cast<int>(animationStats.witnessChanges)) {
                    animationStats.witnessChanges = static_cast<std::size_t>(changes);
                    animationStats.witnessEntity = instance.entityIndex;
                    animationStats.witnessModel = instance.modelPath;
                    animationStats.witnessSequence = request.sequence;
                    animationStats.witnessFrameCount = sampled.frameCount;
                    animationStats.witnessPoseChanges = static_cast<std::size_t>(changes);
                  }
                }
                if (instance.entityIndex == animationStats.witnessEntity) {
                  animationStats.witnessFrame = sampled.frame;
                  animationStats.witnessLocalFrame = sampled.localFrame;
                  animationStats.witnessSectionFrames = sampled.sectionFrames;
                  ++animationStats.witnessObservations;
                }
                // The plan's target class: weapons, projectiles, wearables. The
                // paths are the ones the demo and the item schema actually use;
                // a model that matches none of them still counts in frameAdvances
                // and poseChanges, it just is not allowed to be the witness.
                const bool targetClass = instance.modelPath.find("models/weapons/") != std::string::npos
                  || instance.modelPath.find("models/workshop/weapons/") != std::string::npos
                  || instance.modelPath.find("models/player/items/") != std::string::npos
                  || instance.modelPath.find("models/workshop/player/items/") != std::string::npos;
                if (targetClass && poseChanged) ++animationStats.targetPoseChanges;
                if (targetClass) ++animationStats.targetClassInstances;
                if (targetClass && frameAdvanced) {
                  ++animationStats.targetAdvances;
                  const int changes = animationStats.changesByEntity[instance.entityIndex];
                  if (changes > static_cast<int>(animationStats.targetWitnessChanges)) {
                    animationStats.targetWitnessChanges = static_cast<std::size_t>(changes);
                    animationStats.targetWitnessEntity = instance.entityIndex;
                    animationStats.targetWitnessModel = instance.modelPath;
                    animationStats.targetWitnessSequence = request.sequence;
                    animationStats.targetWitnessFrameCount = sampled.frameCount;
                  }
                }
                if (targetClass && instance.entityIndex == animationStats.targetWitnessEntity) {
                  animationStats.targetWitnessFrame = sampled.frame;
                  animationStats.targetWitnessLocalFrame = sampled.localFrame;
                  animationStats.targetWitnessSectionFrames = sampled.sectionFrames;
                  ++animationStats.targetWitnessObservations;
                }
                animationStats.lastFrameByEntity[instance.entityIndex] = sampled.frame;
              } else {
                ++animationStats.instancesBindPose;
                animationStats.lastStatus = tf2::native::animationInputStatusName(sampled.status);
                animationStats.lastReason = sampled.reason;
                animationStats.lastModel = instance.modelPath;
              }
            } else {
              ++animationStats.instancesBindPose;
              animationStats.lastStatus = tf2::native::animationInputStatusName(animStatus);
              animationStats.lastReason = animReason;
              animationStats.lastModel = instance.modelPath;
            }
            if (request.sequence >= 0) ++animationStats.instancesWithAnimationInput;
            draws.push_back(draw);
          } else if (fallbackMarkers.size() < 32) {
            tf2::native::EntityMarker marker;
            marker.position[0] = instance.transform.origin[0];
            marker.position[1] = instance.transform.origin[1];
            marker.position[2] = instance.transform.origin[2];
            marker.color[0] = 0.95f; marker.color[1] = 0.75f; marker.color[2] = 0.15f; marker.color[3] = 0.0f;
            fallbackMarkers.push_back(marker);
          }
        }
        traceFmt("scene: setInstances draws=%zu markers=%zu", draws.size(), fallbackMarkers.size());
        renderer.setEntityBoneMatrices(entityBoneMatrices);
        renderer.setEntityModelInstances(draws);
        renderer.setEntityMarkers(fallbackMarkers);
        animationStats.boneMatricesUploaded = entityBoneMatrices.size();
        // What the renderer actually accepted, not what the caller offered: an
        // instance whose bone range ran past the uploaded buffer is drawn
        // unskinned, and only the renderer knows that.
        animationStats.gpuSkinnedInstances = renderer.entitySkinnedInstanceCount();
        animationStats.gpuBoneMatrices = renderer.entityBoneMatrixCount();
        animationStats.bindingsResolved = animationBindings.resolveCalls();
        animationStats.cacheHits = animationBindings.cacheHits();
        animationStats.archiveReads = animationBindings.archiveReads();
        traceFmt("scene: anim skinned=%zu bindPose=%zu matrices=%zu gpuSkin=%zu/%zu boneMismatch=%zu poseChanges=%zu targetInstances=%zu targetPose=%zu advances=%zu targetAdv=%zu cycle=%zu/%zu tick=%d status=%s reason=%s",
          animationStats.instancesSkinned, animationStats.instancesBindPose,
          animationStats.boneMatricesUploaded, animationStats.gpuSkinnedInstances,
          animationStats.gpuBoneMatrices, animationStats.boneCountMismatches,
          animationStats.poseChanges, animationStats.targetClassInstances,
          animationStats.targetPoseChanges,
          animationStats.frameAdvances, animationStats.targetAdvances,
          animationStats.cycleInstances, animationStats.cycleNonZero,
          static_cast<int>(resolvedSceneTick),
          animationStats.lastStatus.c_str(), animationStats.lastReason.c_str());
        // The witnesses on their own line. `any` is the entity whose uploaded
        // pose changed most often -- the picture that visibly moves. `target`
        // is the best weapon/projectile/wearable by frame cursor, with the
        // decoder's localFrame/sectionFrames so "the cursor moved but the pose
        // did not" names its own cause instead of looking like a wiring gap.
        traceFmt("scene: animwitness any=%s ent=%d seq=%d frame=%d/%d lf=%d/%d poseChanges=%zu seen=%zu target=%s ent=%d seq=%d frame=%d/%d lf=%d/%d seen=%zu",
          animationStats.witnessModel.empty() ? "-" : animationStats.witnessModel.c_str(),
          static_cast<int>(animationStats.witnessEntity), static_cast<int>(animationStats.witnessSequence),
          animationStats.witnessFrame, animationStats.witnessFrameCount,
          animationStats.witnessLocalFrame, animationStats.witnessSectionFrames,
          animationStats.witnessPoseChanges, animationStats.witnessObservations,
          animationStats.targetWitnessModel.empty() ? "-" : animationStats.targetWitnessModel.c_str(),
          static_cast<int>(animationStats.targetWitnessEntity), static_cast<int>(animationStats.targetWitnessSequence),
          animationStats.targetWitnessFrame, animationStats.targetWitnessFrameCount,
          animationStats.targetWitnessLocalFrame, animationStats.targetWitnessSectionFrames,
          animationStats.targetWitnessObservations);
        traceCheckpoint("scene: instances set");
      } else {
        renderer.setEntityBoneMatrices({});
        renderer.setEntityModelInstances({});
        renderer.setEntityMarkers({});
      }
      // ---- First-person ViewModel animation input (companion proof) --------
      // Resolved on every scene update, because the state it reads is only
      // available here: a ViewModel's sequence changes with the tick, and the
      // model the entity names as its own is only known once a demo packet has
      // been read.
      //
      // WHAT THIS DOES. The entity's m_nSequence indexes the entity's OWN model
      // -- `models/weapons/c_models/c_<class>_arms.mdl` -- and not the
      // `v_<weapon>.mdl` T2 drew, so the pass is switched to that model here. The
      // contract then proves the switch took: owner, model index, path equality,
      // table membership and bone count are each re-derived from what the
      // snapshot said, and a failure in any of them leaves the bind pose in place
      // rather than a half-applied pose. viewmodel.h carries the order.
      if (viewModelRequested) {
        const auto lookup = tf2::native::findViewModelAnimationInput(
          currentEntityStates, demoNetworkSummary.serverClassSchemas,
          demoNetworkSummary.setViewCount > 0, demoNetworkSummary.lastViewEntity,
          demoNetworkSummary.modelPrecache);
        // The proof's own inputs, read here rather than taken on trust from the
        // lookup's selection. `rankPropertyCandidates` returns the selection
        // rule's own ordering (entity_model.h), so the value read is the value
        // the rule would have chosen -- a hand-rolled suffix match in this file
        // would be a second rule, free to drift from the first.
        tf2::native::ViewModelAnimationInput animationInput = lookup.input;
        animationInput.viewEntity = static_cast<int>(lookup.viewEntity);
        if (lookup.input.entityPresent
            && lookup.input.entityIndex >= 0
            && static_cast<std::size_t>(lookup.input.entityIndex) < currentEntityStates.size()) {
          const auto& entityState =
            currentEntityStates[static_cast<std::size_t>(lookup.input.entityIndex)];
          const auto readSlotValue = [&entityState](const char* suffix, std::int64_t& value) {
            const auto candidates = tf2::native::rankPropertyCandidates(entityState, suffix);
            if (candidates.empty()) return false;
            const auto property = entityState.properties.find(candidates.front().name);
            if (property == entityState.properties.end()) return false;
            // The same coercion the lookup applies: a Float slot that carries an
            // integer quantity is read as one.
            value = property->second.type == tf2::native::SendPropType::Float
              ? static_cast<std::int64_t>(property->second.x) : property->second.intValue;
            return true;
          };
          std::int64_t ownerHandle = 0;
          animationInput.hasOwner = readSlotValue("m_hOwner", ownerHandle);
          // The entity index is the handle's low 11 bits (source-sdk-2013,
          // public/basehandle.h) -- the same layout the lookup selected on, so a
          // disagreement here is a real disagreement and not a decoding one.
          animationInput.ownerEntity = animationInput.hasOwner
            ? static_cast<int>(static_cast<std::uint32_t>(ownerHandle) & 0x7ffu) : -1;
          std::int64_t modelIndex = 0;
          animationInput.hasModelIndex = readSlotValue("m_nModelIndex", modelIndex);
          animationInput.modelIndex = animationInput.hasModelIndex
            ? static_cast<int>(modelIndex) : -1;
        }
        // The override, applied after the entity has been read so it REPLACES a
        // value rather than supplying one: with the flag set the line reports the
        // same owner, the same model index and the same entity, and the only
        // field that differs is the one the caller named.
        if (viewModelSequenceOverride >= 0) {
          animationInput.hasSequence = true;
          animationInput.sequence = viewModelSequenceOverride;
        }
        // ---- The companion: the model the entity's index names --------------
        // Built once per distinct path. `requestedPath` is assigned even when the
        // entity names nothing, so a recording whose entity stops naming a model
        // drops back to `--viewmodel` instead of keeping the last companion alive
        // as a stale reading.
        if (viewModelRenderCompanion
            && viewModelCompanion.requestedPath != animationInput.entityModelPath) {
          viewModelCompanion.requestedPath = animationInput.entityModelPath;
          viewModelCompanion.baseState = tf2::native::ViewModelDrawState{};
          viewModelCompanion.request = animationInput.entityModelPath.empty()
            ? tf2::native::ViewModelRequest{}
            : tf2::native::buildViewModelCompanionRequest(assets, animationInput.entityModelPath,
                viewModelLeftHand ? tf2::native::ViewModelHand::Left : tf2::native::ViewModelHand::Right,
                viewModelFovGiven ? viewModelFov : persistent.render.viewModelFov);
          if (viewModelCompanion.request.status == tf2::native::ViewModelStatus::Ok) {
            viewModelCompanion.baseState =
              viewModelDrawStateFor(viewModelCompanion.request, viewModelLeftHand);
          }
          traceFmt("gpu: viewmodel companion path=%s status=%s meshReady=%d sequences=%zu bones=%zu attachment=%s reason=%s",
            viewModelCompanion.requestedPath.empty() ? "-" : viewModelCompanion.requestedPath.c_str(),
            viewModelCompanion.request.status == tf2::native::ViewModelStatus::Ok ? "ok" : "failed",
            viewModelCompanion.request.resources.meshReady ? 1 : 0,
            viewModelCompanion.request.sequenceLabels.size(),
            viewModelCompanion.request.bones.size(),
            viewModelCompanion.request.alignmentName.empty()
              ? "-" : viewModelCompanion.request.alignmentName.c_str(),
            viewModelCompanion.request.reason.empty()
              ? "-" : viewModelCompanion.request.reason.c_str());
        }
        // ---- The weapon body: the other half of the first-person composite ---
        // The arms above are the model the ViewModel entity names as its own
        // (`c_<class>_arms.mdl`). This is the second model the T2.5
        // investigation established the modern first person is made of: the
        // static weapon body the WEAPON ENTITY's own `m_iWorldModelIndex`
        // names, reached through the ViewModel entity's `m_hWeapon` slot. It is
        // a real second model -- its own skeleton, its own material slots, its
        // own mesh and its own draw request -- and not the arms mesh, and not a
        // `v_*.mdl` inferred from the weapon's name. entity_model.h carries the
        // chain and every refusal.
        //
        // The build reuses the companion builder on purpose: its one difference
        // from buildViewModelRequest is that it drops the `v_` prefix check, and
        // the reason it drops it -- the path came from an entity, so the name is
        // evidence rather than an assertion -- is exactly true of this path too.
        // A `v_*` check here would refuse `c_pro_rifle.mdl`, which is the model
        // the POV recording actually names (precache 533 at tick 16306).
        if (!viewModelWeaponBodyEnabled) {
          // The A/B control. The arms are drawn exactly as they were before this
          // round and the reading names the switch, so an `off` frame's missing
          // body can never be read as a lookup failure.
          if (!viewModelWeaponBody.requestedPath.empty()) {
            viewModelWeaponBody.requestedPath.clear();
            viewModelWeaponBody.request = tf2::native::ViewModelRequest{};
          }
          viewModelWeaponBody.baseState = tf2::native::ViewModelDrawState{};
          viewModelWeaponBody.baseState.enabled = false;
          viewModelWeaponBody.baseState.reason = "the weapon body is switched off";
          viewModelWeaponStats.lastWeaponEntity = -1;
          viewModelWeaponStats.lastWorldModelIndex = 0;
          viewModelWeaponStats.lastWeaponClass.clear();
          viewModelWeaponStats.lastPath.clear();
          viewModelWeaponStats.lastFallback = "off";
          // Not a refusal: no lookup ran, so `observations` does not move either
          // and the outcome counts stay untouched. The pair
          // (`refusal=none`, `fallback=off`) is what says "switched off" rather
          // than "asked for and refused".
          viewModelWeaponStats.lastRefusal = tf2::native::ViewModelWeaponBodyRefusal::None;
          viewModelWeaponStats.lastReason =
            "the weapon body is switched off (--viewmodel-weapon-body off)";
          viewModelWeaponStats.lastMaterialSlots = 0;
          viewModelWeaponStats.lastMaterialNamed = 0;
          viewModelWeaponStats.lastMaterialVmt = 0;
          viewModelWeaponStats.lastMaterialVtf = 0;
          viewModelWeaponStats.lastMaterialFailure.clear();
        } else {
          ++viewModelWeaponStats.observations;
          const auto weaponBody = tf2::native::findViewModelWeaponBodyInput(
            currentEntityStates, demoNetworkSummary.serverClassSchemas,
            lookup.input.entityIndex, animationInput.entityModelPath,
            demoNetworkSummary.modelPrecache);
          viewModelWeaponStats.lastWeaponEntity = weaponBody.weaponEntityIndex;
          viewModelWeaponStats.lastWorldModelIndex =
            static_cast<int>(weaponBody.worldModelIndex);
          viewModelWeaponStats.lastWeaponClass = weaponBody.weaponClassName;
          viewModelWeaponStats.lastReason = weaponBody.reason;
          viewModelWeaponStats.lastRefusal = weaponBody.refusal;
          if (weaponBody.hasWeaponHandle) ++viewModelWeaponStats.weaponEntityNamed;
          if (weaponBody.weaponEntityPresent) ++viewModelWeaponStats.weaponEntityPresent;
          ++viewModelWeaponStats.refusalCounts[static_cast<std::size_t>(weaponBody.refusal)];
          if (weaponBody.refusal != tf2::native::ViewModelWeaponBodyRefusal::None) {
            // A refusal has to leave no body behind. Keeping the previous
            // weapon's mesh in the slot would draw it while the reading said
            // the new one was refused -- a stale picture with a green count.
            if (!viewModelWeaponBody.requestedPath.empty()) {
              viewModelWeaponBody.requestedPath.clear();
              viewModelWeaponBody.request = tf2::native::ViewModelRequest{};
            }
            viewModelWeaponBody.baseState = tf2::native::ViewModelDrawState{};
            viewModelWeaponBody.baseState.enabled = false;
            viewModelWeaponBody.baseState.reason = weaponBody.reason;
            viewModelWeaponStats.lastPath.clear();
            viewModelWeaponStats.lastFallback =
              tf2::native::viewModelWeaponBodyRefusalName(weaponBody.refusal);
            // No request was built for a refused body, so there is no material
            // reading to carry over -- a stale "2/2/1" here would describe the
            // previous weapon.
            viewModelWeaponStats.lastMaterialSlots = 0;
            viewModelWeaponStats.lastMaterialNamed = 0;
            viewModelWeaponStats.lastMaterialVmt = 0;
            viewModelWeaponStats.lastMaterialVtf = 0;
            viewModelWeaponStats.lastMaterialFailure.clear();
          } else {
            // Built once per distinct path. Re-read on every scene update only
            // to compare the path: the MDL is parsed and the mesh uploaded at
            // most once per weapon, which is what keeps a weapon switch from
            // re-parsing per frame and a steady weapon from re-parsing at all.
            if (viewModelWeaponBody.requestedPath != weaponBody.worldModelPath) {
              viewModelWeaponBody.requestedPath = weaponBody.worldModelPath;
              viewModelWeaponBody.request = tf2::native::buildViewModelCompanionRequest(
                assets, weaponBody.worldModelPath,
                viewModelLeftHand ? tf2::native::ViewModelHand::Left : tf2::native::ViewModelHand::Right,
                viewModelFovGiven ? viewModelFov : persistent.render.viewModelFov);
              viewModelWeaponBody.baseState = tf2::native::ViewModelDrawState{};
              viewModelWeaponStats.lastPath = weaponBody.worldModelPath;
              if (viewModelWeaponBody.request.status == tf2::native::ViewModelStatus::Ok) {
                ++viewModelWeaponStats.bodiesBuilt;
                viewModelWeaponBody.baseState =
                  viewModelDrawStateFor(viewModelWeaponBody.request, viewModelLeftHand);
              } else {
                ++viewModelWeaponStats.buildFailed;
              }
              traceFmt("gpu: viewmodel weaponBody model=%s class=%s entity=%d worldModelIndex=%d path=%s status=%s meshReady=%d sequences=%zu bones=%zu vertices=%zu materials=%zu/%zu/%zu/%zu attachment=%s reason=%s",
                weaponBody.worldModelPath.empty() ? "-" : weaponBody.worldModelPath.c_str(),
                weaponBody.weaponClassName.empty() ? "-" : weaponBody.weaponClassName.c_str(),
                weaponBody.weaponEntityIndex, static_cast<int>(weaponBody.worldModelIndex),
                viewModelWeaponBody.requestedPath.empty()
                  ? "-" : viewModelWeaponBody.requestedPath.c_str(),
                viewModelWeaponBody.request.status == tf2::native::ViewModelStatus::Ok ? "ok" : "failed",
                viewModelWeaponBody.request.resources.meshReady ? 1 : 0,
                viewModelWeaponBody.request.sequenceLabels.size(),
                viewModelWeaponBody.request.bones.size(),
                viewModelWeaponBody.request.mesh.vertices.size(),
                viewModelWeaponBody.request.materialReading.slots,
                viewModelWeaponBody.request.materialReading.vmtResolved,
                viewModelWeaponBody.request.materialReading.vtfResolved,
                viewModelWeaponBody.request.materialReading.named,
                viewModelWeaponBody.request.alignmentName.empty()
                  ? "-" : viewModelWeaponBody.request.alignmentName.c_str(),
                viewModelWeaponBody.request.reason.empty()
                  ? "-" : viewModelWeaponBody.request.reason.c_str());
            }
            const auto& weaponMaterials = viewModelWeaponBody.request.materialReading;
            viewModelWeaponStats.lastMaterialSlots = weaponMaterials.slots;
            viewModelWeaponStats.lastMaterialNamed = weaponMaterials.named;
            viewModelWeaponStats.lastMaterialVmt = weaponMaterials.vmtResolved;
            viewModelWeaponStats.lastMaterialVtf = weaponMaterials.vtfResolved;
            viewModelWeaponStats.lastMaterialFailure = weaponMaterials.firstFailure;
            bool uploadedNow = false;
            const bool inSlot = viewModelWeaponBody.request.status == tf2::native::ViewModelStatus::Ok
              && viewModelWeaponBody.request.resources.meshReady
              && ensureViewModelWeaponMesh(viewModelWeaponBody.request.path,
                   viewModelWeaponBody.request.mesh.vertices, uploadedNow);
            if (uploadedNow) ++viewModelWeaponStats.bodiesUploaded;
            if (inSlot) {
              // `enabled` means "this state's mesh is the one in the weapon
              // slot", derived the same way the arms' is. The reason is left
              // empty so the pass reports its own draw reason on the frame it
              // draws.
              viewModelWeaponBody.baseState.enabled = true;
              viewModelWeaponBody.baseState.reason.clear();
              viewModelWeaponStats.lastFallback = "-";
            } else {
              viewModelWeaponBody.baseState.enabled = false;
              viewModelWeaponBody.baseState.reason = viewModelWeaponBody.request.reason.empty()
                ? std::string("the weapon body mesh did not upload")
                : viewModelWeaponBody.request.reason;
              viewModelWeaponStats.lastFallback =
                viewModelWeaponBody.request.status == tf2::native::ViewModelStatus::Ok
                  ? "uploadFailed" : "buildFailed";
            }
          }
        }
        // Built into a named local rather than passed as a temporary: %s reads a
        // pointer, and handing the variadic call a std::string is a C4840
        // warning for a real reason.
        const std::string weaponRefusalText = viewModelWeaponRefusalText();
        traceFmt("scene: weaponBody entity=%d class=%s worldModelIndex=%d path=%s refusal=%s fallback=%s reason=%s observations=%zu named=%zu present=%zu refusals=%s",
          viewModelWeaponStats.lastWeaponEntity,
          viewModelWeaponStats.lastWeaponClass.empty() ? "-" : viewModelWeaponStats.lastWeaponClass.c_str(),
          viewModelWeaponStats.lastWorldModelIndex,
          viewModelWeaponStats.lastPath.empty() ? "-" : viewModelWeaponStats.lastPath.c_str(),
          tf2::native::viewModelWeaponBodyRefusalName(viewModelWeaponStats.lastRefusal),
          viewModelWeaponStats.lastFallback.c_str(),
          viewModelWeaponStats.lastReason.empty() ? "-" : viewModelWeaponStats.lastReason.c_str(),
          viewModelWeaponStats.observations, viewModelWeaponStats.weaponEntityNamed,
          viewModelWeaponStats.weaponEntityPresent,
          weaponRefusalText.c_str());
        // Which model the pass draws.
        //
        // `--viewmodel-identity-bones` pins it to `--viewmodel`, and that is an
        // invariant of the instrument rather than a convenience: it uploads one
        // identity matrix per bone of a specific skeleton, so a model switch
        // underneath it would change which skeleton its claim -- "the skinning
        // branch runs and the bind pose does not move" -- is a claim about.
        const bool companionDrawn = viewModelRenderCompanion && !viewModelIdentityBones
          && viewModelCompanion.request.status == tf2::native::ViewModelStatus::Ok
          && viewModelCompanion.request.resources.meshReady
          && ensureViewModelMesh(viewModelCompanion.request.path,
               viewModelCompanion.request);
        if (!companionDrawn && viewModelRequest.status == tf2::native::ViewModelStatus::Ok
            && viewModelRequest.resources.meshReady) {
          // The fallback needs its mesh back in the buffer when a companion held
          // it; a no-op on every run where it already does.
          ensureViewModelMesh(viewModelRequest.path, viewModelRequest);
        }
        const tf2::native::ViewModelRequest& drawnRequest =
          companionDrawn ? viewModelCompanion.request : viewModelRequest;
        tf2::native::ViewModelDrawState state =
          companionDrawn ? viewModelCompanion.baseState : viewModelBaseState;
        // `enabled` means "this state's mesh is the one in the vertex buffer".
        // Deriving it from the buffer's recorded contents rather than from the
        // startup upload is what keeps the two from disagreeing after a switch.
        state.enabled = viewModelMeshBufferPath == drawnRequest.path;
        tf2::native::AnimationSampleRequest timing;
        timing.tick = resolvedSceneTick;
        // The demo's own tick rate, not a constant: it is what turns a tick into
        // an animation cursor, and it varies with the recording.
        timing.tickRate = static_cast<float>(g_playback.tickRate);
        tf2::native::AnimationInputStatus bindingStatus =
          tf2::native::AnimationInputStatus::ModelMissing;
        std::string bindingReason;
        const auto* binding = drawnRequest.status == tf2::native::ViewModelStatus::Ok
          ? viewModelAnimations.resolve(drawnRequest.path, bindingStatus, bindingReason)
          : nullptr;
        const auto outcome = tf2::native::resolveViewModelAnimationInput(
          animationInput, drawnRequest, binding, timing);
        state.animationStatus = tf2::native::viewModelAnimationStatusName(outcome.status);
        // The pose is AVAILABLE when the contract accepted the input, and APPLIED
        // only when the caller asked for it. Those are deliberately two different
        // things.
        //
        // What changed this round is what an acceptance MEANS: the pose now comes
        // from the entity's own model, which is the model the pass draws, so
        // applying it is no longer the plausible-looking wrong pose T2 refused to
        // apply. The opt-in stays anyway, because a reading that applies by
        // default cannot be compared against the frame that does not, and that
        // byte comparison is how "the pose moved the picture" is measured rather
        // than asserted. `vmAnimApplied` reports which of the two happened.
        const bool poseAvailable = !outcome.bindPose && !outcome.skinMatrices.empty();
        if (viewModelIdentityBones) {
          // The identity-bone instrument owns the pass state. It uploads one
          // matrix per bone at reload so the skinning branch runs without moving
          // the bind pose, and T2's byte-identical frame pair is what that
          // measures. Letting this block clear the table would silently disable
          // it: the first run of viewmodel-contract-check.sh after the wiring read
          // `vmident skinning=0 boneTableCoverage=0/0` and lost seven assertions
          // that way. `state` is a copy of the base state, so doing nothing here
          // *is* leaving the instrument alone -- and the contract reading is still
          // reported on the same frame, which is why the two are separate fields.
        } else if (poseAvailable && viewModelApplyAnimation
                   && renderer.uploadViewModelBones(outcome.skinMatrices)) {
          // All-or-nothing, the same rule the pass applies to the entity path: a
          // pose that cannot be uploaded leaves the bind pose in place rather than
          // a half-updated table.
          state.skinningEnabled = true;
          state.boneCount = static_cast<std::uint32_t>(outcome.skinMatrices.size());
          state.animationApplied = true;
          ++viewModelAnimationStats.posesApplied;
        } else {
          // No table behind a pose that is not applied. Without this the previous
          // frame's upload stays in the pass's count and `boneTableCoverage` reads
          // `45/0` -- a pair the pass never compared, because it only compares when
          // skinningEnabled is set.
          state.skinningEnabled = false;
          state.boneCount = 0;
          state.animationApplied = false;
          renderer.clearViewModelBones();
        }
        renderer.setViewModelState(state);
        // The second draw request's state, set in the same breath as the arms'
        // so a frame can never draw the body with a stale state. Its `enabled`
        // was already derived from what is in the weapon slot, and its reason is
        // empty exactly when the body is drawable -- the pass reports its own
        // refusal otherwise (see drawViewModel).
        renderer.setViewModelWeaponState(viewModelWeaponBody.baseState);
        ++viewModelAnimationStats.observations;
        if (lookup.input.entityPresent) ++viewModelAnimationStats.entitiesFound;
        if (lookup.input.hasSequence) ++viewModelAnimationStats.sequencesFound;
        if (lookup.input.hasCycle) ++viewModelAnimationStats.cyclesFound;
        if (outcome.entityModelMatchesDrawn) ++viewModelAnimationStats.modelMatches;
        else ++viewModelAnimationStats.modelMismatch;
        if (outcome.sequenceInRange) ++viewModelAnimationStats.sequenceInRange;
        if (companionDrawn) ++viewModelAnimationStats.companionFrames;
        switch (outcome.status) {
          case tf2::native::ViewModelAnimationStatus::Ok:
            ++viewModelAnimationStats.okPoses; break;
          case tf2::native::ViewModelAnimationStatus::SequenceOutOfRange:
            ++viewModelAnimationStats.outOfRange; break;
          case tf2::native::ViewModelAnimationStatus::BoneMismatch:
            ++viewModelAnimationStats.boneMismatch; break;
          default:
            ++viewModelAnimationStats.missing; break;
        }
        if (lookup.input.hasSequence) {
          if (viewModelAnimationStats.minSequence < 0
              || lookup.input.sequence < viewModelAnimationStats.minSequence) {
            viewModelAnimationStats.minSequence = lookup.input.sequence;
          }
          if (lookup.input.sequence > viewModelAnimationStats.maxSequence) {
            viewModelAnimationStats.maxSequence = lookup.input.sequence;
          }
          if (viewModelAnimationStats.lastSequence >= 0
              && viewModelAnimationStats.lastSequence != lookup.input.sequence) {
            ++viewModelAnimationStats.sequenceChanges;
          }
          viewModelAnimationStats.lastSequence = lookup.input.sequence;
        }
        viewModelAnimationStats.lastEntity = lookup.input.entityIndex;
        viewModelAnimationStats.lastEntityModel = lookup.input.entityModelPath;
        viewModelAnimationStats.lastStatus = state.animationStatus;
        viewModelAnimationStats.lastReason = outcome.reason;
        viewModelAnimationStats.lastEntityModelMatchesDrawn = outcome.entityModelMatchesDrawn;
        viewModelAnimationStats.lastBindPose = outcome.bindPose;
        viewModelAnimationStats.lastSequenceAccepted =
          outcome.status == tf2::native::ViewModelAnimationStatus::Ok;
        viewModelAnimationStats.lastSequenceInRange = outcome.sequenceInRange;
        viewModelAnimationStats.lastDrawnModel = drawnRequest.path;
        traceFmt("scene: vmAnim entity=%d sequence=%d rate=%.2f cyclePresent=%d model=%s status=%s poseAvailable=%d applied=%d frame=%d/%d entityRule=%s reason=%s owner=%d viewEntity=%d modelIndex=%d drawnModel=%s entityModelMatchesDrawn=%d sequenceInRange=%d bindPose=%d sequenceOverride=%d",
          lookup.input.entityIndex, lookup.input.sequence,
          static_cast<double>(lookup.input.playbackRate), lookup.input.hasCycle ? 1 : 0,
          lookup.input.entityModelPath.empty() ? "-" : lookup.input.entityModelPath.c_str(),
          state.animationStatus.c_str(), poseAvailable ? 1 : 0, state.animationApplied ? 1 : 0,
          outcome.frame, outcome.frameCount,
          lookup.reason.empty() ? "-" : lookup.reason.c_str(),
          outcome.reason.empty() ? "-" : outcome.reason.c_str(),
          animationInput.ownerEntity, animationInput.viewEntity, animationInput.modelIndex,
          drawnRequest.path.empty() ? "-" : drawnRequest.path.c_str(),
          outcome.entityModelMatchesDrawn ? 1 : 0, outcome.sequenceInRange ? 1 : 0,
          outcome.bindPose ? 1 : 0, viewModelSequenceOverride);
        traceFmt("scene: vmAnim summary observations=%zu entities=%zu sequences=%zu cycles=%zu ok=%zu applied=%zu outOfRange=%zu boneMismatch=%zu missing=%zu changes=%zu range=%d..%d lastEntity=%d lastStatus=%s modelMismatch=%zu modelMatches=%zu sequenceInRange=%zu companionFrames=%zu lastDrawnModel=%s",
          viewModelAnimationStats.observations, viewModelAnimationStats.entitiesFound,
          viewModelAnimationStats.sequencesFound, viewModelAnimationStats.cyclesFound,
          viewModelAnimationStats.okPoses, viewModelAnimationStats.posesApplied,
          viewModelAnimationStats.outOfRange,
          viewModelAnimationStats.boneMismatch, viewModelAnimationStats.missing,
          viewModelAnimationStats.sequenceChanges,
          viewModelAnimationStats.minSequence, viewModelAnimationStats.maxSequence,
          viewModelAnimationStats.lastEntity, viewModelAnimationStats.lastStatus.c_str(),
          viewModelAnimationStats.modelMismatch, viewModelAnimationStats.modelMatches,
          viewModelAnimationStats.sequenceInRange,
          viewModelAnimationStats.companionFrames,
          viewModelAnimationStats.lastDrawnModel.empty()
            ? "-" : viewModelAnimationStats.lastDrawnModel.c_str());
      }
      renderer.setProjectileTimeline(demoNetworkSummary.projectileTimeline, sceneTick);
      renderer.setCpuParticleTimeline(demoNetworkSummary.projectileTimeline, sceneTick);
      hudTrailVertices = renderer.projectileVertexCount();
      lastSceneTick = g_playback.tick;
      traceCheckpoint("scene: done");
    }
    nativeUi.setPlaybackState(g_playback.tick, g_playback.enabled && !g_playback.paused,
      g_playback.reverse, g_playback.speed);
    // All three are called every iteration on purpose -- the cheap part of each
    // is the state comparison, and that comparison is what lets the expensive
    // part (SendMessageW, SetWindowTextW, GetProcessMemoryInfo) run only when
    // the state behind it has moved. Both updaters return whether they pushed,
    // so the counters read "work done", not "calls made"; that is the number a
    // gate can distinguish "the cache works" with, because a counter that
    // incremented on every call would be identical to main_loop_iterations.
    if (updateNativeUiControls(window)) ++uiUpdateCalls;
    if (refreshWindowTitle()) ++titleUpdateCalls;
    writeMetrics(now, false);
    if (elapsed >= kTargetFrameSeconds) {
      traceFmt("draw: begin tick=%d frames=%llu", static_cast<int>(g_playback.tick),
        static_cast<unsigned long long>(renderedFrames));
      if (g_renderer && captureArmed && g_playback.tick >= captureTick) {
        renderer.requestFrameCapture(captureFramePath.wstring());
        captureArmed = false;
        captureRequested = true;
      }
      if (g_renderer && renderer.draw(0.055f, 0.07f, 0.085f)) ++renderedFrames;
      // Emitted on the frame the capture was requested for, so the readings and
      // the bitmap on disk describe the same frame.
      if (captureRequested) writeViewModelReading();
      traceFmt("draw: end frames=%llu pending=%d", static_cast<unsigned long long>(renderedFrames),
        renderer.frameCapturePending() ? 1 : 0);
      if (captureRequested && !renderer.frameCapturePending()) {
        PostQuitMessage(renderer.lastFrameCaptureSucceeded() ? 0 : 16);
        continue;
      }
      lastFrame = now;
      continue;
    }

    const double remainingMs = (kTargetFrameSeconds - elapsed) * 1000.0;
    // Wait until the next frame is actually due, not for at most 1 ms. The old
    // `min(remainingMs, 1.0)` meant a pending frame 15 ms out was still waited
    // for in fifteen 1-ms slices, and each slice ran the whole loop body: ~1000
    // iterations a second doing nothing but re-testing state that had not moved.
    // The clamp keeps a floor of 1 ms so the wait is never zero (a zero-timeout
    // MsgWait is a poll, which is the spin this removes) and a ceiling of 16 ms
    // so message processing stays responsive without a tight slice. The call is
    // still MsgWaitForMultipleObjectsEx with QS_ALLINPUT|MWMO_INPUTAVAILABLE, so
    // an arriving message wakes it immediately -- the wait is a sleeping wait,
    // not a busy one.
    const double waitMs = std::clamp(std::ceil(remainingMs), 1.0, 16.0);
    ++waitCalls;
    waitMsTotal += static_cast<std::uint64_t>(waitMs);
    MsgWaitForMultipleObjectsEx(0, nullptr, static_cast<DWORD>(waitMs), QS_ALLINPUT,
      MWMO_INPUTAVAILABLE);

    if (g_renderer && renderer.lastError() != S_OK) {
      const HRESULT error = renderer.lastError();
      if (error == DXGI_ERROR_DEVICE_REMOVED || error == DXGI_ERROR_DEVICE_RESET) {
        if (deviceRecoveryAttempts++ >= 1) {
          PostQuitMessage(4);
          continue;
        }
        renderer.shutdown();
        if (!renderer.initialize(window) || !reloadGpuResources()) {
          PostQuitMessage(2);
        }
      } else {
        PostQuitMessage(3);
      }
    }
  }
  LARGE_INTEGER metricsEnd{};
  if (QueryPerformanceCounter(&metricsEnd)) writeMetrics(metricsEnd, true);
  // The quiet-capture contract, read at the last point where everything it
  // names is still alive: a capture exit does not destroy the window, the
  // scheduler still holds its final counters, and the capture result is the
  // same value the exit code was chosen from. `audio_device_opened` is the
  // process-wide waveOutOpen call count -- 0 means no device was opened or
  // even asked for, which is the reading that separates "--audio-disabled
  // works" from "the flag was ignored". `events_skipped` folds the two skip
  // classes (voice, device-disabled); the contract cares that nothing played.
  {
    const bool windowAlive = window && IsWindow(window) != 0;
    const auto& audioStats = soundScheduler.stats();
    traceFmt("mode: summary window_created=%d window_visible=%d audio_mode=%s audio_device_opened=%zu events_seen=%zu events_played=%zu events_skipped=%zu events_missing=%zu drawn=%llu capture_written=%d metrics_written=%d exit=%d",
      windowAlive ? 1 : 0,
      (windowAlive && IsWindowVisible(window)) ? 1 : 0,
      audioDisabled ? "disabled" : "device",
      tf2::native::waveOutOpenCalls(),
      audioStats.eventsSeen, audioStats.eventsPlayed,
      audioStats.eventsSkippedVoice + audioStats.eventsSkippedDisabled,
      audioStats.eventsMissingResource,
      static_cast<unsigned long long>(renderedFrames),
      (captureRequested && renderer.lastFrameCaptureSucceeded()) ? 1 : 0,
      (!metricsPath.empty() && metricsFile.is_open() && metricsWriteCalls > 0) ? 1 : 0,
      static_cast<int>(message.wParam));
    // The eight fields the audio wiring is accepted on, on one line so a gate
    // reads them from a single place. `soundEventsRead` is what the decoder
    // produced, `soundEventsScheduled` what survived the origin/name filter and
    // entered the timeline, and the three scheduler counters are what the
    // scheduler did with them. `soundEventsSkippedVoice` is only meaningful
    // because voice events are kept in the timeline and flagged rather than
    // refused before `add` -- otherwise it would be a counter that can only ever
    // read zero. `duplicateTicks` proves the "same tick does not replay" guard
    // actually fired; `reverseRecoveries` counts resets that moved the cursor
    // backwards, which is what a reverse run has to do to replay anything.
    traceFmt("audio: summary soundEventsRead=%zu soundEventsScheduled=%zu soundEventsPlayed=%zu"
      " soundEventsSkippedVoice=%zu soundEventsMissingResource=%zu firstPacketTick=%d"
      " soundOriginTick=%d reverseRecoveries=%zu duplicateTicks=%zu",
      soundEventsRead, soundTimeline.size(), audioStats.eventsPlayed,
      audioStats.eventsSkippedVoice, audioStats.eventsMissingResource,
      static_cast<int>(firstPacketTick), static_cast<int>(soundOriginTick),
      audioStats.reverseRecoveries, audioStats.duplicateTicks);
    // The camera/HUD contract, on one line so camera-ui-check.sh can pin it:
    // the mode the run ended in, the double-click window the controller
    // advertises (a constant, so a drift shows up as a changed reading), the
    // key counters (0 in a gate run -- no input exists -- and the scene-side
    // follow/hold/freeze split, where a real demo with observe targets must
    // read follows>=1 and a run must never read holds as a clear.
    traceFmt("camera: summary mode=%s modeChanges=%u spacePresses=%u spaceSingles=%u"
      " spaceDoubles=%u doubleWindowMs=%u pauseToggles=%u follows=%u holdsNoTarget=%u"
      " freezeFrames=%u",
      tf2::native::cameraModeName(g_cameraMode.mode()),
      g_cameraMode.modeChangeCount(), g_cameraMode.spacePressCount(),
      g_cameraMode.spaceSingleCount(), g_cameraMode.spaceDoubleCount(),
      tf2::native::CameraModeController::doubleClickWindowMs(),
      g_pauseKey.toggles, g_cameraFollows, g_cameraHoldsNoTarget,
      g_cameraFreezeFrames);
  }
  renderer.shutdown();
  persistent.render = renderer.settings();
  if (assets.valid()) persistent.tfRoot = assets.tfDirectory;
  tf2::native::saveSettings(persistent);
  g_renderer = nullptr;
  return static_cast<int>(message.wParam);
}
