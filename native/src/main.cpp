#include "native_renderer.h"
#include "asset_root.h"
#include "map_lookup.h"
#include "settings_store.h"
#include "vpk_archive.h"
#include "asset_lookup.h"
#include "vtf_texture.h"
#include "world_material.h"
#include "world_material_features.h"
#include "world_lighting.h"
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
#include "install_lifecycle.h"
#include "logstf_panel.h"
#include "logstf_viewer.h"

// T37: the installer lifecycle modes. Declared at namespace scope rather than
// inside wWinMain so the enumeration and the argument scan are one readable
// thing; the state they fill is small and local to the process.
namespace {
enum class LifecycleMode { None, Version, Install, Uninstall, Status };

struct LifecycleRequest {
  LifecycleMode mode = LifecycleMode::None;
  tf2::native::installer::Options options;
  bool directoryGiven = false;
};

// Four modes, not "a playback run with a flag": each answers a question and
// exits. They are recognised before the settings file is read and before the
// window is created, which is what makes --version usable from a script and
// keeps an uninstall from depending on a working GPU, a demo or a saved root.
bool parseLifecycleRequest(int argumentCount, wchar_t** arguments, LifecycleRequest* request) {
  for (int i = 1; i < argumentCount; ++i) {
    const wchar_t* argument = arguments[i];
    if (wcscmp(argument, L"--version") == 0) request->mode = LifecycleMode::Version;
    else if (wcscmp(argument, L"--install") == 0) request->mode = LifecycleMode::Install;
    else if (wcscmp(argument, L"--uninstall") == 0) request->mode = LifecycleMode::Uninstall;
    else if (wcscmp(argument, L"--install-status") == 0) request->mode = LifecycleMode::Status;
    else if (wcscmp(argument, L"--install-dry-run") == 0) request->options.dryRun = true;
    else if (wcscmp(argument, L"--install-dir") == 0 && i + 1 < argumentCount) {
      request->options.installDir = std::filesystem::absolute(std::filesystem::path(arguments[++i]));
      request->directoryGiven = true;
    }
  }
  return request->mode != LifecycleMode::None;
}

// The default install directory is the executable's own, not the working
// directory: a user who double clicks the exe from an arbitrary folder must not
// install into that folder by accident.
std::filesystem::path defaultInstallDirectory() {
  wchar_t self[MAX_PATH]{};
  if (GetModuleFileNameW(nullptr, self, MAX_PATH) == 0) return {};
  return std::filesystem::path(self).parent_path();
}

// A REDIRECTED STDOUT IS THE CALLER'S AND IS NEVER REPLACED.
//   The first version of this attached to the parent console unconditionally
//   and then reopened CONOUT$ "to be safe". Under a shell redirect such as
//   "tf2_demo_native --version > out.txt" AttachConsole succeeds against the
//   shell's console, the reopen points stdout at the terminal, and the file the
//   caller redirected to stays 0 bytes while the reading appears on a terminal
//   nobody captured. Measured on 2026-10-20: 0 bytes in the file and exit 0 --
//   a silent, green-shaped lie, and the exact failure a gate that parses the
//   file would call "no output" or, worse, "passed". Only a process with no
//   usable stdout at all needs the console (a double click, where Windows hands
//   a GUI subsystem process an invalid handle), and only then is it attached,
//   used and freed.
int runLifecycle(LifecycleRequest& request) {
  if (!request.directoryGiven) request.options.installDir = defaultInstallDirectory();
  tf2::native::installer::Result result;
  switch (request.mode) {
    case LifecycleMode::Version: result = tf2::native::installer::version(request.options); break;
    case LifecycleMode::Install: result = tf2::native::installer::install(request.options); break;
    case LifecycleMode::Uninstall: result = tf2::native::installer::uninstall(request.options); break;
    case LifecycleMode::Status:
    default: result = tf2::native::installer::status(request.options); break;
  }
  const HANDLE given = GetStdHandle(STD_OUTPUT_HANDLE);
  const bool noStdout = given == nullptr || given == INVALID_HANDLE_VALUE;
  bool attached = false;
  std::FILE* sink = stdout;
  if (noStdout) {
    attached = AttachConsole(ATTACH_PARENT_PROCESS) != 0;
    if (attached) {
      std::FILE* reopened = nullptr;
      if (_wfreopen_s(&reopened, L"CONOUT$", L"w", stdout) == 0 && reopened != nullptr) sink = reopened;
    }
  }
  for (const auto& line : result.lines) {
    std::fprintf(sink, "%s\n", line.c_str());
    std::fflush(sink);
  }
  const int code = result.exitCode;
  if (attached) FreeConsole();
  return code;
}
} // namespace


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
#include <map>
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
  //
  // T21: 512 then did the same thing to the per-face trace. That line carries
  // two VTF paths, a tile rectangle, an atlas uv, three sampled texels and four
  // depths; measured on pl_odyssey it needs 540-odd bytes and lost its tail
  // (`depthMatch`, `frameRaw`) without saying so, so a point was being compared
  // with no depth verdict and read as "this face". The size is now generous and
  // the truncation that remains is written down instead of hidden: a line that
  // cannot be trusted says it cannot be trusted.
  char buffer[2048];
  va_list args;
  va_start(args, format);
  const int written = std::vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  if (written < 0) return;
  if (static_cast<std::size_t>(written) >= sizeof(buffer)) {
    static const char kTail[] = "...[TRUNCATED]";
    std::memcpy(buffer + sizeof(buffer) - sizeof(kTail), kTail, sizeof(kTail));
  }
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

// T54: argument text -> UTF-8. The logs panel's URL policy is defined in bytes
// (ASCII only, refusing everything else), so the conversion has to be exact and
// lossless-or-refused rather than "best effort": a URL that cannot survive the
// wide->narrow trip is refused by the policy instead of being mangled into
// something that passes the host allow-list.
std::string narrowAscii(const wchar_t* value) {
  if (value == nullptr) return {};
  std::string result;
  for (const wchar_t* cursor = value; *cursor != L'\0'; ++cursor) {
    if (*cursor < 0 || *cursor > 0x7F) {
      // Out of range on purpose: the policy refuses non-ASCII bytes, and this
      // is how they get to it instead of silently becoming '?'.
      result.push_back('\x7F');
      continue;
    }
    result.push_back(static_cast<char>(*cursor));
  }
  return result;
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

// T54: the logs panel's startup options. Parsed in the main argument loop and
// consumed once the window exists; the panel itself lives in logstf_panel.cpp
// so main.cpp only forwards messages to it.
tf2::native::LogsPanelOptions g_logsPanelOptions;
bool g_logsPanelArgumentRejected = false;

// Camera mode machine (T4). The controller owns the mode and the SPACE
// single/double classification; the per-scene-tick decision is
// decideCameraSceneAction and the counters below are what the trace reports
// and camera-ui-check.sh pins.
tf2::native::CameraModeController g_cameraMode;
bool g_debugCamera = false;
float g_debugCameraEye[3] = {0.0f, 0.0f, 0.0f};
float g_debugCameraPitch = 0.0f;
float g_debugCameraYaw = 0.0f;
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
    // T54: the logs panel is docked to the right edge and follows the same
    // client rect; it is laid out after the controls so the demo's minimum
    // width is what the planner measured, not what an earlier size allowed.
    tf2::native::logsPanelLayout(window);
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
    tf2::native::logsPanelLayout(window);
    return 0;
  }
  case WM_LBUTTONDOWN:
  case WM_LBUTTONUP:
    // T54: the panel's splitter. Consumed only when the press landed on the
    // splitter, so ordinary left clicks still reach the default procedure.
    if (tf2::native::logsPanelMouse(window, message, wParam, lParam)) return 0;
    return DefWindowProcW(window, message, wParam, lParam);
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
    // T54: a splitter drag owns the mouse until the button comes up.
    if (tf2::native::logsPanelMouse(window, message, wParam, lParam)) return 0;
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
    } else if (tf2::native::logsPanelCommand(window, static_cast<int>(id))) {
      // T54: the logs panel's own controls. Handled before the default so an
      // id it owns can never fall through to DefWindowProc and be lost.
      return 0;
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
  // T37: the installer lifecycle answers and exits before the settings file is
  // read, before the window exists and before any GPU state is created. An
  // uninstall must not depend on a working adapter, and --version must not
  // depend on a saved tf root.
  if (arguments) {
    LifecycleRequest lifecycle;
    if (parseLifecycleRequest(argumentCount, arguments, &lifecycle)) {
      LocalFree(arguments);
      return runLifecycle(lifecycle);
    }
  }
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
  // T54: the logs panel. Creating its controls is not creating a browser -- the
  // WebView2 object exists only after a URL is imported, which is what makes
  // "an empty panel fetches nothing" true of the product and not only of the
  // controller the probe drives.
  tf2::native::logsPanelAttach(window, instance, g_logsPanelOptions);
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
  //   --viewmodel-weapon-flat-color  draws the weapon body's ranges with no
  //                             texture. It is the body's own control, separate
  //                             from `--viewmodel-flat-color`: the material claim
  //                             is about the body's region, and a control that
  //                             also flattened the hands could not show that the
  //                             hands did not move. Same state as a range whose
  //                             slot failed to resolve, so a fallback and the
  //                             control produce the same picture on purpose.
  //   --viewmodel-weapon-model <path.mdl>  draw this model as the weapon body
  //                             instead of the one the weapon entity names. An
  //                             input override, reported as `override=` on the
  //                             body's material line: the recording's own body
  //                             has two slots on one atlas, so the per-range
  //                             binding claim needs a body whose slots differ.
  //   --viewmodel-weapon-material-slot <n>  bind slot n for EVERY body range,
  //                             mirroring `--viewmodel-material-slot` for the
  //                             arms. It is the body's own input override, and it
  //                             exists because the arms' claim ("each range is
  //                             painted by its own albedo") was only ever made
  //                             about the arms: the body's equivalent was a
  //                             count of the keys it *asked* for. Forcing slot 0
  //                             and slot 1 in turn partitions the body's pixels
  //                             into the ranges that natively use the other slot,
  //                             which is a picture claim instead of a request.
  //                             Reported as `slotOverride=` on the body's
  //                             material line, so an overridden run cannot be
  //                             read as a natural one, and it must move nothing
  //                             outside the body's own coverage.
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
  // The weapon body's own flat-colour control. Separate from
  // `--viewmodel-flat-color` on purpose: the T14 claim is "the material change is
  // limited to the weapon body", and a switch that also flattened the hands would
  // make the hands move and the claim untestable. Same state as a slot that
  // failed to resolve, so the control and the fallback cannot diverge.
  bool viewModelWeaponFlatColor = false;
  // An INPUT OVERRIDE: draw this model as the body instead of the one the weapon
  // entity's own `m_iWorldModelIndex` names. It exists because the recording's
  // weapon (`c_pro_rifle.mdl`) names two material slots that resolve to the SAME
  // atlas, so "each range uses its own albedo" has no picture to show on it --
  // a gate could only assert a slot count. `c_tfc_sniperrifle.mdl` is a shipped
  // weapon body of the same class whose two slots resolve to two different
  // textures, which is what makes the wrong-slot and missing-texture mutations
  // move pixels rather than only counters. Reported on every material line as
  // `override=`, so an overridden run can never be read as a natural one.
  std::filesystem::path commandWeaponModel;

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
  // The body's copy of the same override. Negative = each range binds the slot
  // its own descriptor names, which is the only value a natural run has. It is a
  // separate variable on purpose: the two draws share the texture cache but not
  // their material state, so a body-only override must be unable to repaint the
  // hands (T22 measures that with a containment test).
  std::int32_t viewModelWeaponMaterialSlotOverride = -1;
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
  // ---- T2 (2026-10-14): the three advanced-material switches --------------
  // Each effect gets its own on|off pair, and the reason is the pixel claim: a
  // `phong-off` frame and a `phong-on` frame have to differ in exactly one
  // input, and a single combined switch could not tell which of the three moved
  // the picture. `given` is tracked separately from `requested` so an explicit
  // `on` is distinguishable from the material's own answer, exactly as the sky
  // and water switches do.
  bool viewModelPhongGiven = false;
  bool viewModelPhongRequested = true;
  bool viewModelBumpGiven = false;
  bool viewModelBumpRequested = true;
  bool viewModelSelfillumGiven = false;
  bool viewModelSelfillumRequested = true;
  // Diagnostic: treat the drawn ViewModel material as if it declared `$selfillum
  // 1` with a tint, so the selfillum branch is executable on content that does
  // not ask for it. The same shape and the same justification as
  // `--world-env-force`: no ViewModel material on this install was found to
  // declare `$selfillum`, so without this the branch would be compiled, wired,
  // counted and never run -- which is exactly the state the task's rule about
  // "declared is not applied" exists to make visible. Every reading line names
  // the force, so a forced run cannot be mistaken for a natural one.
  bool viewModelSelfillumForce = false;
  // Counted rather than defaulted: an unrecognised value must fail the run,
  // because a gate that asked for `phong off` and got `phong on` would compare
  // a frame with itself and report a difference of zero as a pass.
  int invalidViewModelEffectArgument = 0;
  bool viewModelArgumentRejected = false;
  // Diagnostic: treat the world material as if it declared an env map, so the
  // six-face sampling branch can be run on content that does not ask for it.
  // No installed map's first material declares $envmap -- measured across all
  // 68 maps in this install -- so without this the branch would never execute
  // and a wrong register or a dead sampler in it would go unnoticed. The
  // product's own choice stays material-driven; this only says "run it anyway
  // and print the mode", and the mode is reported either way.
  bool forceWorldEnv = false;
  // T1 (2026-10-13): the skybox and the pvs clip are on by default because both
  // are things the world pass should be doing, and both have an explicit off
  // switch because a gate has to be able to see the two states side by side --
  // "the clip is applied" is only a reading if "the clip is not applied" prints
  // something different.
  bool worldSkyRequested = true;
  bool worldSkyGiven = false;
  bool worldVisClipRequested = true;
  bool worldVisClipGiven = false;
  // T10 diagnostic: the world pass rasterises with back-face culling by
  // default; --no-world-cull turns it off for one capture so the same frame can
  // be drawn culled and unculled and the missing faces attributed to the
  // winding. Same given/requested shape as the sky and vis switches above.
  bool worldCullRequested = true;
  bool worldCullGiven = false;
  // T11 diagnostic: --world-flat draws the world pass as one flat colour with
  // no texture/lightmap/bump/env sampling, --world-depth draws it as greyscale
  // depth. They separate "the triangle is not there" from "the triangle is
  // there but shaded wrong". Diagnostic only; the product's default is the
  // textured path, and giving both modes is refused rather than silently
  // resolved.
  int worldDebugView = 0;
  int worldDebugViewGiven = 0;
  int invalidWorldDebugArgument = 0;
  // T21: emit one chain record per sampled screen point on the captured frame.
  // Off by default: a per-face trace is a diagnostic, and a normal run's trace
  // should not carry 24 lines nobody asked for.
  bool worldFaceTraceRequested = false;
  // T26: the screen points the empty-pixel trace names, as "x,y;x,y;...". They
  // come from the accounting mask rather than from a fixed grid: the point of
  // the trace is to name the face under a pixel the frame does not fill.
  std::vector<std::pair<int, int>> worldHoleTracePoints;
  int invalidWorldHoleTraceArgument = 0;
  // T2 (2026-10-14): the water surface follows the same shape as T1's two
  // switches -- on by default, with an explicit off switch -- for the same
  // reason: "the water is drawn" is only a reading if "the water is not drawn"
  // prints something different, and the pixel claim in
  // world-material-effects-check.sh compares those two frames.
  bool worldWaterRequested = true;
  bool worldWaterGiven = false;
  // Counted rather than guessed at: `--world-water` with no value or with an
  // unrecognised one must fail the run instead of falling back to the default,
  // because a gate that asks for water-off and gets water-on has compared the
  // same frame twice and called it a difference.
  int invalidWorldWaterArgument = 0;
  // ---- T12 (2026-10-18): the terrain blend's two diagnostic switches ------
  // The task asks for terrain on/off/repeat frames and for a proof that the
  // difference lands in the terrain region rather than only in the counters.
  // Neither half is measurable without a switch that turns the feature off and
  // a frame that contains the terrain and nothing else, so both are switches
  // here rather than edits to a temporary build.
  bool worldBlendRequested = true;
  bool worldBlendGiven = false;
  int invalidWorldBlendArgument = 0;
  // all | terrain | sky | none. `none` is not a convenience: it is how the gate
  // measures the background colour of its own camera instead of assuming one,
  // and the terrain mask is `terrain` minus `none` pixel by pixel. T26 adds
  // `sky`: the BSP's sky-brush polygons, through the same camera and transform,
  // which is what turns "this pixel is empty" into "this pixel is the map's
  // sky" without guessing from a colour.
  enum class WorldGeometryFilter { All, Terrain, Sky, None };
  WorldGeometryFilter worldGeometry = WorldGeometryFilter::All;
  int invalidWorldGeometryArgument = 0;
  // T17: the lightmap A/B. The product had no way to draw the world with the
  // lightmap term removed, so "this surface is dark because its lightmap is
  // dark" was not a measurable statement. The switch changes one constant, not
  // the geometry or the materials.
  bool worldLightmapGiven = false;
  bool worldLightmapRequested = true;
  int invalidWorldLightmapArgument = 0;
  // ---- T24 (2026-10-19): one semantic at a time --------------------------
  // The three pass-level lighting controls used to be reachable only through
  // `--quality standard`, which flips eight fields at once (dynamic lighting,
  // shadows, bump, specular, water reflection, skybox, mip bias, anisotropy).
  // A capture from that switch cannot attribute a pixel to one semantic, so
  // each of the three gets its own on/off pair. They set exactly one field and
  // leave the preset label alone; the trace prints requested and effective
  // separately, so a switch that was asked for and did not reach the constant
  // buffer is a reading rather than a silent no-op.
  bool worldDynamicLightGiven = false;
  bool worldDynamicLightRequested = false;
  bool worldSpecularGiven = false;
  bool worldSpecularRequested = false;
  bool worldBumpGiven = false;
  bool worldBumpRequested = false;
  // The bump branch's diagnostic texture. Not an on/off pair: the reference
  // frame's slot owner declares no $bumpmap, so the only way to measure the
  // branch's pixels is to name one. Empty means "whatever the material says".
  std::wstring worldBumpTextureOverride;
  // T27: three switches, one per semantic, plus the detail branch's diagnostic
  // texture. `worldBump2Given` is only meaningful with `--world-material-bump
  // on` (layer 2's normal is blended into layer 1's, so there is nothing to
  // blend toward without it), and the trace prints both so a frame that asked
  // for bump2 alone is a reading rather than a silent no-op.
  bool worldMaterialBumpGiven = false;
  bool worldMaterialBumpRequested = false;
  bool worldBump2Given = false;
  bool worldBump2Requested = false;
  bool worldDetailGiven = false;
  bool worldDetailRequested = false;
  std::wstring worldDetailTextureOverride;
  // T27: `$bumpmap2`'s diagnostic texture. 4 of this map's 5 $bumpmap2
  // materials declare `$ssbump` (refused by name, and rightly) and the fifth is
  // under the map, so without an override the layer has no visible pixel and the
  // gate could only measure a counter. The override replaces the TEXELS; the
  // blend weight stays the material's own vertex alpha, which is the part of
  // this semantic the material owns.
  std::wstring worldBump2TextureOverride;
  int invalidWorldLightingArgument = 0;
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
      } else if (wcscmp(arguments[i], L"--world-env-force") == 0) {
        forceWorldEnv = true;
      } else if (wcscmp(arguments[i], L"--world-sky") == 0) {
        worldSkyRequested = true; worldSkyGiven = true;
      } else if (wcscmp(arguments[i], L"--no-world-sky") == 0) {
        worldSkyRequested = false; worldSkyGiven = true;
      } else if (wcscmp(arguments[i], L"--world-vis-clip") == 0) {
        worldVisClipRequested = true; worldVisClipGiven = true;
      } else if (wcscmp(arguments[i], L"--debug-camera") == 0 && i + 5 < argumentCount) {
        // T10 diagnostic: a fixed camera in MAP coordinates, north-up. Freezes
        // the per-frame camera so a frame can be compared across builds without
        // the observer view moving underneath it.
        g_debugCamera = true;
        g_debugCameraEye[0] = _wtof(arguments[++i]);
        g_debugCameraEye[1] = _wtof(arguments[++i]);
        g_debugCameraEye[2] = _wtof(arguments[++i]);
        g_debugCameraPitch = _wtof(arguments[++i]);
        g_debugCameraYaw = _wtof(arguments[++i]);
      } else if (wcscmp(arguments[i], L"--no-world-vis-clip") == 0) {
        worldVisClipRequested = false; worldVisClipGiven = true;
      } else if (wcscmp(arguments[i], L"--world-cull") == 0) {
        worldCullRequested = true; worldCullGiven = true;
      } else if (wcscmp(arguments[i], L"--no-world-cull") == 0) {
        worldCullRequested = false; worldCullGiven = true;
      } else if (wcscmp(arguments[i], L"--world-flat") == 0) {
        ++worldDebugViewGiven; worldDebugView = 1;
      } else if (wcscmp(arguments[i], L"--world-depth") == 0) {
        ++worldDebugViewGiven; worldDebugView = 2;
      } else if (wcscmp(arguments[i], L"--world-albedo") == 0) {
        // T21: the material path's own output, before any lighting. A capture
        // in this view is what a face trace can be compared against pixel for
        // pixel; the textured frame cannot, because its pixels are albedo
        // times a lightmap.
        ++worldDebugViewGiven; worldDebugView = 3;
      } else if (wcscmp(arguments[i], L"--world-face-id") == 0) {
        // T26: the face index as colour. It is the rasterizer's answer to
        // "which face owns this pixel", which is the independent check of the
        // CPU point oracle the empty-pixel trace uses.
        ++worldDebugViewGiven; worldDebugView = 4;
      } else if (wcscmp(arguments[i], L"--world-face-trace") == 0) {
        worldFaceTraceRequested = true;
      } else if (wcscmp(arguments[i], L"--world-hole-trace") == 0 && i + 1 < argumentCount) {
        // "x,y;x,y;..." -- parsed strictly, because a typo that silently
        // produced zero points would make the empty-pixel claim vacuous.
        const std::wstring list = arguments[++i];
        std::size_t at = 0;
        while (at <= list.size()) {
          const auto semicolon = list.find(L';', at);
          const std::wstring token = list.substr(at, semicolon == std::wstring::npos
            ? std::wstring::npos : semicolon - at);
          if (!token.empty()) {
            const auto comma = token.find(L',');
            if (comma == std::wstring::npos) { ++invalidWorldHoleTraceArgument; break; }
            wchar_t* endX = nullptr;
            wchar_t* endY = nullptr;
            const wchar_t* xText = token.c_str();
            const long x = std::wcstol(xText, &endX, 10);
            const long y = std::wcstol(token.c_str() + comma + 1, &endY, 10);
            if (endX == xText || *endX != L',' || endY == token.c_str() + comma + 1 || *endY != L'\0') {
              ++invalidWorldHoleTraceArgument;
              break;
            }
            worldHoleTracePoints.emplace_back(static_cast<int>(x), static_cast<int>(y));
          }
          if (semicolon == std::wstring::npos) break;
          at = semicolon + 1;
        }
      } else if (wcscmp(arguments[i], L"--world-hole-trace") == 0) {
        ++invalidWorldHoleTraceArgument;
      } else if (wcscmp(arguments[i], L"--world-lightmap") == 0 && i + 1 < argumentCount) {
        if (wcscmp(arguments[++i], L"on") == 0) { worldLightmapRequested = true; worldLightmapGiven = true; }
        else if (wcscmp(arguments[i], L"off") == 0) { worldLightmapRequested = false; worldLightmapGiven = true; }
        else { ++invalidWorldLightmapArgument; }
      } else if (wcscmp(arguments[i], L"--world-lightmap") == 0) {
        ++invalidWorldLightmapArgument;
      } else if (wcscmp(arguments[i], L"--world-dynamic-light") == 0 && i + 1 < argumentCount) {
        if (wcscmp(arguments[++i], L"on") == 0) { worldDynamicLightRequested = true; worldDynamicLightGiven = true; }
        else if (wcscmp(arguments[i], L"off") == 0) { worldDynamicLightRequested = false; worldDynamicLightGiven = true; }
        else { ++invalidWorldLightingArgument; }
      } else if (wcscmp(arguments[i], L"--world-dynamic-light") == 0) {
        ++invalidWorldLightingArgument;
      } else if (wcscmp(arguments[i], L"--world-specular") == 0 && i + 1 < argumentCount) {
        if (wcscmp(arguments[++i], L"on") == 0) { worldSpecularRequested = true; worldSpecularGiven = true; }
        else if (wcscmp(arguments[i], L"off") == 0) { worldSpecularRequested = false; worldSpecularGiven = true; }
        else { ++invalidWorldLightingArgument; }
      } else if (wcscmp(arguments[i], L"--world-specular") == 0) {
        ++invalidWorldLightingArgument;
      } else if (wcscmp(arguments[i], L"--world-bump") == 0 && i + 1 < argumentCount) {
        if (wcscmp(arguments[++i], L"on") == 0) { worldBumpRequested = true; worldBumpGiven = true; }
        else if (wcscmp(arguments[i], L"off") == 0) { worldBumpRequested = false; worldBumpGiven = true; }
        else { ++invalidWorldLightingArgument; }
      } else if (wcscmp(arguments[i], L"--world-bump") == 0) {
        ++invalidWorldLightingArgument;
      } else if (wcscmp(arguments[i], L"--world-bump-texture") == 0 && i + 1 < argumentCount) {
        worldBumpTextureOverride = arguments[++i];
      } else if (wcscmp(arguments[i], L"--world-bump-texture") == 0) {
        ++invalidWorldLightingArgument;
      } else if (wcscmp(arguments[i], L"--world-material-bump") == 0 && i + 1 < argumentCount) {
        // T27: the per-material normal path, separate from `--world-bump`
        // (T24's single-slot diagnostic). One switch per capture is the rule
        // this pair exists to keep: `--world-bump` measures one texture applied
        // to the whole world, this one measures every material's own.
        if (wcscmp(arguments[++i], L"on") == 0) { worldMaterialBumpRequested = true; worldMaterialBumpGiven = true; }
        else if (wcscmp(arguments[i], L"off") == 0) { worldMaterialBumpRequested = false; worldMaterialBumpGiven = true; }
        else { ++invalidWorldLightingArgument; }
      } else if (wcscmp(arguments[i], L"--world-material-bump") == 0) {
        ++invalidWorldLightingArgument;
      } else if (wcscmp(arguments[i], L"--world-bump2") == 0 && i + 1 < argumentCount) {
        if (wcscmp(arguments[++i], L"on") == 0) { worldBump2Requested = true; worldBump2Given = true; }
        else if (wcscmp(arguments[i], L"off") == 0) { worldBump2Requested = false; worldBump2Given = true; }
        else { ++invalidWorldLightingArgument; }
      } else if (wcscmp(arguments[i], L"--world-bump2") == 0) {
        ++invalidWorldLightingArgument;
      } else if (wcscmp(arguments[i], L"--world-detail") == 0 && i + 1 < argumentCount) {
        if (wcscmp(arguments[++i], L"on") == 0) { worldDetailRequested = true; worldDetailGiven = true; }
        else if (wcscmp(arguments[i], L"off") == 0) { worldDetailRequested = false; worldDetailGiven = true; }
        else { ++invalidWorldLightingArgument; }
      } else if (wcscmp(arguments[i], L"--world-detail") == 0) {
        ++invalidWorldLightingArgument;
      } else if (wcscmp(arguments[i], L"--world-bump2-texture") == 0 && i + 1 < argumentCount) {
        // The layer-2 diagnostic source, for the reason in the declaration
        // above: without it this map's only non-ssbump `$bumpmap2` material
        // contributes no visible pixel, so the branch's pixels cannot be
        // measured at all.
        worldBump2TextureOverride = arguments[++i];
      } else if (wcscmp(arguments[i], L"--world-bump2-texture") == 0) {
        ++invalidWorldLightingArgument;
      } else if (wcscmp(arguments[i], L"--world-detail-texture") == 0 && i + 1 < argumentCount) {
        // The detail branch's diagnostic source, for the same reason
        // `--world-bump-texture` exists: every `$detail` VMT on the reference
        // map is absent from this install, so the only way to measure the
        // branch's pixels is to name a texture. Empty means "whatever the
        // material's detail VMT says".
        worldDetailTextureOverride = arguments[++i];
      } else if (wcscmp(arguments[i], L"--world-detail-texture") == 0) {
        ++invalidWorldLightingArgument;
      } else if (wcscmp(arguments[i], L"--world-water") == 0 && i + 1 < argumentCount) {
        // on|off, and nothing else: a switch that accepts any string is a switch
        // that silently does nothing on a typo.
        if (wcscmp(arguments[++i], L"on") == 0) { worldWaterRequested = true; worldWaterGiven = true; }
        else if (wcscmp(arguments[i], L"off") == 0) { worldWaterRequested = false; worldWaterGiven = true; }
        else {
          ++invalidWorldWaterArgument;
        }
      } else if (wcscmp(arguments[i], L"--world-water") == 0) {
        ++invalidWorldWaterArgument;
      } else if (wcscmp(arguments[i], L"--world-blend") == 0 && i + 1 < argumentCount) {
        // on|off and nothing else, for the same reason --world-water takes only
        // those two words: a switch that accepts a typo silently runs the
        // default, and the gate then compares a frame against itself.
        if (wcscmp(arguments[++i], L"on") == 0) { worldBlendRequested = true; worldBlendGiven = true; }
        else if (wcscmp(arguments[i], L"off") == 0) { worldBlendRequested = false; worldBlendGiven = true; }
        else {
          ++invalidWorldBlendArgument;
        }
      } else if (wcscmp(arguments[i], L"--world-blend") == 0) {
        ++invalidWorldBlendArgument;
      } else if (wcscmp(arguments[i], L"--world-geometry") == 0 && i + 1 < argumentCount) {
        if (wcscmp(arguments[++i], L"all") == 0) worldGeometry = WorldGeometryFilter::All;
        else if (wcscmp(arguments[i], L"terrain") == 0) worldGeometry = WorldGeometryFilter::Terrain;
        else if (wcscmp(arguments[i], L"sky") == 0) worldGeometry = WorldGeometryFilter::Sky;
        else if (wcscmp(arguments[i], L"none") == 0) worldGeometry = WorldGeometryFilter::None;
        else ++invalidWorldGeometryArgument;
      } else if (wcscmp(arguments[i], L"--world-geometry") == 0) {
        ++invalidWorldGeometryArgument;
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
      } else if (wcscmp(arguments[i], L"--viewmodel-weapon-flat-color") == 0) {
        // No argument: the flag is the switch. A `--viewmodel-weapon-flat-color
        // off` spelling would only invite a reader to think the default was on.
        viewModelWeaponFlatColor = true;
      } else if (wcscmp(arguments[i], L"--viewmodel-weapon-model") == 0 && i + 1 < argumentCount) {
        commandWeaponModel = arguments[++i];
        if (commandWeaponModel.empty()) viewModelArgumentRejected = true;
      } else if (wcscmp(arguments[i], L"--viewmodel-weapon-model") == 0) {
        viewModelArgumentRejected = true;
      } else if (wcscmp(arguments[i], L"--viewmodel-weapon-material-slot") == 0 && i + 1 < argumentCount) {
        // The body's input override, and it is deliberately not the arms'
        // variable: the two draws share a texture cache, not their material
        // state, and a body run that repainted the hands would be a defect this
        // flag's own containment test has to be able to see.
        wchar_t* end = nullptr;
        const long long parsed = std::wcstoll(arguments[++i], &end, 10);
        if (end && *end == L'\0' && parsed >= 0) {
          viewModelWeaponMaterialSlotOverride = static_cast<std::int32_t>(parsed);
        } else {
          viewModelArgumentRejected = true;
        }
      } else if (wcscmp(arguments[i], L"--viewmodel-weapon-material-slot") == 0) {
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
      } else if (wcscmp(arguments[i], L"--viewmodel-phong") == 0 && i + 1 < argumentCount) {
        if (wcscmp(arguments[++i], L"on") == 0) { viewModelPhongRequested = true; viewModelPhongGiven = true; }
        else if (wcscmp(arguments[i], L"off") == 0) { viewModelPhongRequested = false; viewModelPhongGiven = true; }
        else ++invalidViewModelEffectArgument;
      } else if (wcscmp(arguments[i], L"--viewmodel-phong") == 0) {
        ++invalidViewModelEffectArgument;
      } else if (wcscmp(arguments[i], L"--viewmodel-bump") == 0 && i + 1 < argumentCount) {
        if (wcscmp(arguments[++i], L"on") == 0) { viewModelBumpRequested = true; viewModelBumpGiven = true; }
        else if (wcscmp(arguments[i], L"off") == 0) { viewModelBumpRequested = false; viewModelBumpGiven = true; }
        else ++invalidViewModelEffectArgument;
      } else if (wcscmp(arguments[i], L"--viewmodel-bump") == 0) {
        ++invalidViewModelEffectArgument;
      } else if (wcscmp(arguments[i], L"--viewmodel-selfillum") == 0 && i + 1 < argumentCount) {
        if (wcscmp(arguments[++i], L"on") == 0) { viewModelSelfillumRequested = true; viewModelSelfillumGiven = true; }
        else if (wcscmp(arguments[i], L"off") == 0) { viewModelSelfillumRequested = false; viewModelSelfillumGiven = true; }
        else ++invalidViewModelEffectArgument;
      } else if (wcscmp(arguments[i], L"--viewmodel-selfillum") == 0) {
        ++invalidViewModelEffectArgument;
      } else if (wcscmp(arguments[i], L"--viewmodel-selfillum-force") == 0) {
        viewModelSelfillumForce = true;
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
      } else if (wcscmp(arguments[i], L"--logstf-open") == 0) {
        g_logsPanelOptions.openAtStartup = true;
      } else if (wcscmp(arguments[i], L"--logstf-url") == 0 && i + 1 < argumentCount) {
        g_logsPanelOptions.url = narrowAscii(arguments[++i]);
        g_logsPanelOptions.openAtStartup = true;
      } else if (wcscmp(arguments[i], L"--logstf-url2") == 0 && i + 1 < argumentCount) {
        g_logsPanelOptions.url2 = narrowAscii(arguments[++i]);
      } else if (wcscmp(arguments[i], L"--logstf-width") == 0 && i + 1 < argumentCount) {
        wchar_t* end = nullptr;
        const long parsed = std::wcstol(arguments[++i], &end, 10);
        if (end && *end == L'\0' && parsed > 0) g_logsPanelOptions.width = static_cast<int>(parsed);
        else g_logsPanelArgumentRejected = true;
      } else if (wcscmp(arguments[i], L"--logstf-user-data") == 0 && i + 1 < argumentCount) {
        g_logsPanelOptions.userData = narrowAscii(arguments[++i]);
      } else if (wcscmp(arguments[i], L"--logstf-script") == 0 && i + 1 < argumentCount) {
        g_logsPanelOptions.script = narrowAscii(arguments[++i]);
        g_logsPanelOptions.openAtStartup = true;
      } else if (wcscmp(arguments[i], L"--logstf-pump-ms") == 0 && i + 1 < argumentCount) {
        wchar_t* end = nullptr;
        const long parsed = std::wcstol(arguments[++i], &end, 10);
        if (end && *end == L'\0' && parsed > 0) g_logsPanelOptions.pumpMilliseconds = static_cast<int>(parsed);
        else g_logsPanelArgumentRejected = true;
      } else if (wcscmp(arguments[i], L"--logstf-measure") == 0) {
        g_logsPanelOptions.measureFrames = true;
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
  if (worldDebugViewGiven > 1) ++invalidWorldDebugArgument;
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
  if (g_logsPanelArgumentRejected) {
    // T54: a rejected argument is a refusal, not a default. A gate that asked
    // for a width and got one the parser invented would measure the inventor.
    OutputDebugStringW(L"TF2 Demo Player: invalid --logstf-width/--logstf-pump-ms; refusing to guess.\n");
    return 13;
  }
  if (invalidWorldWaterArgument) {
    // T2 (2026-10-14): `--world-water` takes on|off and nothing else. Refusing
    // rather than defaulting matters because the pixel claim compares a water-on
    // frame against a water-off frame; a run that was asked for off and drew on
    // would compare a frame with itself and report no difference.
    OutputDebugStringW(L"TF2 Demo Player: invalid --world-water; it takes on or off and nothing else.\n");
    return 13;
  }
  if (invalidWorldDebugArgument) {
    // T11 diagnostic: --world-flat and --world-depth are two answers to the
    // same question and a run may state one. Asking for both would make the
    // capture's meaning depend on argument order, so it is refused.
    OutputDebugStringW(L"TF2 Demo Player: --world-flat and --world-depth cannot be combined.\n");
    return 13;
  }
  if (invalidWorldBlendArgument) {
    // T12 (2026-10-18): same rule, same reason. The terrain on/off pair is the
    // only evidence that the second layer reaches a pixel, so a typo that fell
    // back to `on` would silently produce two identical frames and a gate that
    // reported "no difference" as a pass.
    OutputDebugStringW(L"TF2 Demo Player: invalid --world-blend; it takes on or off and nothing else.\n");
    return 13;
  }
  if (invalidWorldLightmapArgument) {
    // T17: same rule. A typo that fell back to `on` would make the lightmap A/B
    // two copies of one frame and read as "the lightmap changes nothing".
    OutputDebugStringW(L"TF2 Demo Player: invalid --world-lightmap; it takes on or off and nothing else.\n");
    return 13;
  }
  if (invalidWorldLightingArgument) {
    // T24: same rule, and here it decides whether the A/B means anything. A
    // typo that fell back to `off` would produce two copies of the baseline and
    // the gate would report "this semantic changes no pixel" as a measurement.
    OutputDebugStringW(L"TF2 Demo Player: invalid --world-dynamic-light/--world-specular/--world-bump/"
      L"--world-material-bump/--world-bump2/--world-detail; each takes on or off and nothing else.\n");
    return 13;
  }
  // Applied after the argument loop and after normalize(), so an override is
  // the last word on its own field and nothing else. `--quality standard`
  // followed by `--world-bump off` is a valid A/B: the preset sets the other
  // seven fields, the switch removes this one.
  if (worldDynamicLightGiven) persistent.render.dynamicLighting = worldDynamicLightRequested;
  if (worldSpecularGiven) persistent.render.specular = worldSpecularRequested;
  if (worldBumpGiven) persistent.render.bumpMapping = worldBumpRequested;
  if (worldMaterialBumpGiven) persistent.render.materialBump = worldMaterialBumpRequested;
  // Layer 2's normal is a modifier of layer 1's, so asking for it alone is not a
  // frame this pass can draw: the request is recorded and reported, and the
  // effective flag below stays false. That is the same "requested != effective"
  // split T24 established for the pass switches.
  if (worldBump2Given) persistent.render.bumpMap2 = worldBump2Requested;
  if (worldDetailGiven) persistent.render.detailLayer = worldDetailRequested;
  if (invalidWorldGeometryArgument) {
    // T12: `all|terrain|sky|none`. `terrain`, `sky` and `none` are the frames
    // the region and empty-pixel proofs are computed from; a typo that fell back
    // to `all` would make the mask the whole world and the claim vacuous.
    OutputDebugStringW(L"TF2 Demo Player: invalid --world-geometry; it takes all, terrain, sky or none.\n");
    return 13;
  }
  if (invalidWorldHoleTraceArgument) {
    OutputDebugStringW(L"TF2 Demo Player: invalid --world-hole-trace; it takes screen points as x,y;x,y.\n");
    return 13;
  }
  if (invalidViewModelEffectArgument) {
    // T2 (2026-10-14): same rule for the three advanced-material switches.
    OutputDebugStringW(L"TF2 Demo Player: invalid --viewmodel-phong/--viewmodel-bump/--viewmodel-selfillum; each takes on or off.\n");
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
    // The Steam-library candidates below are therefore NOT appended in this
    // branch: with them in the list, findInstalled silently connects the
    // D:\SteamLibrary install whenever the operator's path is missing or
    // moved -- the wrong-path use this comment promises not to do. T4
    // reproduced exactly that: --tf-root pointing at a nonexistent directory
    // still started with the Steam install connected, and the window title
    // read as if the TF2 resources were fine. A valid explicit root is
    // unaffected: it is the only candidate here, so findInstalled either
    // returns it or the program reports the root as missing.
    candidates = {commandTfRoot};
  } else {
    if (!persistent.tfRoot.empty()) {
      candidates.insert(candidates.begin(), persistent.tfRoot);
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
  }
  if (arguments) LocalFree(arguments);
  if (!commandDemo.empty()) {
    nativeUi.openDemo(commandDemo);
    if (nativeUi.snapshot().screen == tf2::native::UiScreen::ImportReview) {
      nativeUi.confirmImport();
    }
  }
  const auto assets = tf2::native::AssetRoot::findInstalled(candidates);
  // T32: the root this run was asked for and the root it resolved to, as two
  // separate fields. They differ exactly when the request was refused, and the
  // refusal used to have no reading at all: an explicit --tf-root that did not
  // exist produced only a title that said the resources were not found, with no
  // path and no exit status to say which root was tried.
  traceFmt("resources: requested=%s tf_root=%s valid=%d",
    commandTfRoot.empty() ? "-" : commandTfRoot.string().c_str(),
    assets.tfDirectory.empty() ? "-" : assets.tfDirectory.string().c_str(),
    assets.valid() ? 1 : 0);
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
    // T14: the runtime table's own failures, which are not the same question as
    // the reachability counts above. A slot can reach its VTF and still fail to
    // decode, and a slot that resolved but whose range was never drawn is not a
    // drawing failure at all.
    std::string lastRuntimeFailure;
    // Why the first range drew flat colour: the slot's own reason (a missing VMT,
    // a texture that did not decode), so `weaponFallbackRanges=1` is followed by
    // a sentence rather than left as a count to interpret.
    std::string lastUnboundReason;
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
  // T32: archives that are ON DISK but could not be opened. Kept apart from an
  // archive that is simply absent -- the fixed name list this product opens is a
  // superset of what any one install carries, so absence is normal and silent --
  // and surfaced in the title when it is non-zero, because "this install has no
  // VPK" and "this install's VPK is unreadable" need different repairs.
  std::size_t refusedVpkCount = 0;
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
  // The map's own cubemap: six decoded faces in Source's order (+X,-X,+Y,-Y,+Z,-Z)
  // and the edge of the cube, or nothing when the map has no sample that decodes
  // all six. Decoded once at load and uploaded with the other world textures.
  std::array<std::vector<std::uint8_t>, 6> mapCubemapFaces;
  UINT mapCubemapSize = 0;
  std::string mapCubemapSource;
  std::string mapCubemapReason = "no env_cubemap sample decoded all six faces";
  // T1 (2026-10-13): the skybox's own reason, kept apart from the cubemap's
  // because "the sky has no sides" and "the map has no cube" are two different
  // absences and a shared string would let the gate read one for the other.
  std::string worldSkyReason = "no sky box was uploaded";
  float worldVisCameraEye[3] = {0.0f, 0.0f, 0.0f};
  // ---- T2 (2026-10-14): the water surface ---------------------------------
  // The warp's amplitude in uv units and the tick its phase is derived from.
  // Both are stated here rather than inlined at the call so the gate's fixture
  // can move them and a reader can see which values were used.
  //
  // The amplitude is 0.02 because the bound the task asks for is a *bounded*
  // warp, and a bound only means something next to the number that bounds it:
  // 0.02 is one twentieth of a texture's uv span, which is large enough to move
  // the picture and small enough that a warp that escaped its bound would show
  // up as a texture visibly sliding rather than as a subtle artefact.
  float waterVertexWarpAmplitude = 0.02f;
  // A paused launch is at tick 0, so the phase is 0 and the repeat frame is
  // byte-identical. A demo run advances it, which is what makes `water-on` and
  // `water-repeat` a statement about the warp rather than about a constant.
  std::int64_t waterWarpTick = 0;
  bool waterMaterialResolved_ = false;
  std::size_t waterFacesInMap_ = 0;
  std::string waterSurfaceReason = "no water geometry was built";
  tf2::native::WorldWaterParams waterMaterialParams_;
  bool mapBaseTexture2Found = false;
  bool mapBumpMapFound = false;
  bool mapBaseTexture2Declared = false;
  bool mapBumpMapDeclared = false;
  bool mapBumpTextureForced = false;
  // T27: the layer-2 diagnostic decoded and is in use. Passed to the renderer
  // before the world upload because it decides whether a material without a
  // layer-1 normal may still carry the layer-2 bit (the shader names the base).
  bool worldBump2Diagnostic = false;
  tf2::native::WorldMaterialParams mapMaterialParams;
  std::vector<tf2::native::WorldTexture> worldTextures;
  // T19: every material VMT that parsed, kept for the feature scan. The scan
  // runs after the first draw because `visibleTriangles` only exists once the
  // pvs clip has run; keeping the parsed fields here is what lets it run then
  // without re-reading the archive.
  std::vector<std::pair<std::string, tf2::native::VmtMaterial>> worldFetchedMaterials;
  // T21: the whole chain for every distinct material, in one table: which store
  // answered for the VMT, which for the VTF, what the source texture was, what
  // the atlas did with it, and -- for the materials the old 64-tile ceiling
  // would have dropped -- the position the pre-T19 order would have given it.
  // The face trace names a material per screen point and reads this table; a
  // refusal that is not in here cannot be named by the trace, so every branch
  // of the fetch below writes a reason.
  struct WorldMaterialChainRecord {
    std::string vmtPath;
    std::string vmtStore = "none";
    std::string vtfPath;
    std::string vtfStore = "none";
    std::string baseTexture;
    int sourceWidth = 0;
    int sourceHeight = 0;
    int tileWidth = 0;
    int tileHeight = 0;
    int batch = -1;
    int tile = -1;
    int tileX = 0;
    int tileY = 0;
    int atlasWidth = 0;
    int atlasHeight = 0;
    bool blend = false;
    std::size_t triangles = 0;
    // The index the reverse-alphabetical (pre-T19) fetch order would have
    // accepted this material at, counting only materials that resolve. 0 means
    // "would not have been accepted at all"; 65 and up means "the old cap would
    // have dropped it".
    std::size_t oldOrderAccepted = 0;
    bool beyondOldCap = false;
    std::string reason = "not-reached";
  };
  std::unordered_map<std::string, WorldMaterialChainRecord> worldMaterialChain;
  std::vector<std::shared_ptr<tf2::native::VpkArchive>> soundArchives;
  // The lookup policy as an object rather than as the six copied loops this
  // file used to carry. Sound keeps using `soundArchives` directly -- it wants
  // the archive objects, not a resolved byte range -- and the chain shares those
  // same objects, so nothing is opened twice.
  tf2::native::AssetStoreChain assetChain(assets.tfDirectory);
  // T24: the map's bytes must outlive the block that loads them. The pakfile
  // reader registered on the chain is its last store and is called from the
  // per-frame feature scan long after that block ends. Capturing `mapBytes` by
  // reference made those lookups a read of freed memory: pl_odyssey's 9
  // pakfile-only `$bumpmap` VTFs came back missing for exactly that reason,
  // while a reader that owns the bytes (the probe) found all 44. The owner
  // below is the fix; `mapPakfileBytes` is what the chain and the scan use.
  std::shared_ptr<const std::vector<std::uint8_t>> mapPakfileBytes;
  std::unordered_map<std::string, std::vector<std::uint8_t>> materialVtfCache;
  const auto readMaterialVtf = [&](const std::string& path) -> const std::vector<std::uint8_t>& {
    auto found = materialVtfCache.find(path);
    if (found != materialVtfCache.end()) return found->second;
    auto looked = assetChain.lookup(path);
    return materialVtfCache.emplace(path, std::move(looked.bytes)).first->second;
  };
  if (assets.valid()) {
    // ---- T9 (2026-10-16): where the map came from -------------------------
    // `demoHeader.mapName` is a NAME, not a path, and the three stores it may
    // live in are walked in the order map_lookup.h documents. The store that
    // answered is reported: "the map loaded" and "the map loaded from the copy
    // a server pushed into download/maps" are different statements, and only
    // the second one says anything about that directory.
    //
    // Before this the product built one path, `<tf>/maps/<map>.bsp`, and asked
    // the VPKs for the same string. A demo whose map existed only under
    // download/maps therefore played with `BSP: invalid/BSP header is invalid`
    // and a fallback material path -- T4 recorded two.
    const std::string requestedMap = demoHeader.valid
      ? demoHeader.mapName : std::string("2koth_abbey");
    const auto mapPlan = tf2::native::planMapLookup(requestedMap);
    // The stem is what the cubemap path is built from, so it has to survive the
    // not-found case too: `maps/<name>.bsp` is the name even when nothing has
    // that name on disk.
    std::filesystem::path mapPath = std::filesystem::path(L"maps")
      / std::filesystem::path(requestedMap + ".bsp");
    std::string mapLookupStore = "none";
    std::string mapLookupSelected;
    std::string mapLookupReason;
    std::vector<std::uint8_t> mapBytes;
    for (const auto* name : { L"pak01_dir.vpk", L"tf2_misc_dir.vpk", L"tf2_textures_dir.vpk", L"tf2_sound_misc_dir.vpk", L"tf2_sound_vo_english_dir.vpk" }) {
      const auto archivePath = assets.tfDirectory / name;
      auto archive = std::make_shared<tf2::native::VpkArchive>();
      std::string archiveError;
      if (archive->open(archivePath, &archiveError)) {
        assetChain.addArchive(archive, archivePath);
        soundArchives.push_back(std::move(archive));
        continue;
      }
      // T32: `open` fails for an archive this install simply does not have, and
      // that is the normal case -- the name list above is a superset of a real
      // install. Only a file that is present and still unreadable is a resource
      // finding; it is named with the archive reader own reason and counted,
      // instead of being skipped in silence and surfacing later as "map not
      // found in maps, download/maps or a vpk", which blames the wrong thing.
      std::error_code presentError;
      if (!std::filesystem::is_regular_file(archivePath, presentError) || presentError) continue;
      ++refusedVpkCount;
      // T42: the same stable attribution words vpk_query prints. A trace reader and
      // a query reader must not have to guess which failures are format problems,
      // which are truncated files, and which are permissions -- the three have
      // different repairs. The category is a function of the reader's own reason,
      // never of a path or a fixture name.
      const std::string archiveReason = archiveError.empty()
        ? std::string("open failed without a reason") : archiveError;
      const char* archiveAttribution = "open_failed";
      if (archiveReason == "unsupported VPK header") archiveAttribution = "unsupported";
      else if (archiveReason.rfind("truncated", 0) == 0) archiveAttribution = "truncated";
      else if (archiveReason == "cannot read VPK directory file") archiveAttribution = "unreadable";
      // Two lines, not one extended line. Existing readers take everything after
      // reason= as the reason and require file= immediately after "refused", so
      // inserting a field between them would silently rewrite their reading. The
      // first line is therefore byte-compatible with pre-T42 traces; the second is
      // the new attribution word, keyed to the same file.
      traceFmt("vpk: refused file=%s reason=%s",
        std::filesystem::path(name).string().c_str(), archiveReason.c_str());
      traceFmt("vpk: refused attribution=%s file=%s",
        archiveAttribution, std::filesystem::path(name).string().c_str());
    }
    traceFmt("vpk: archives opened=%zu refused=%zu", soundArchives.size(), refusedVpkCount);
    if (!mapPlan.nameUsable) {
      // Refused by name, before any store is touched. Reported as the refusal
      // it is, not as "not found": a map called `..\..\x` was never searched
      // for and saying so is the difference between a missing file and an
      // input that cannot be honoured.
      mapLookupReason = mapPlan.reason;
    } else {
      for (const auto& candidate : mapPlan.candidates) {
        if (candidate.packed) {
          for (const auto& archive : soundArchives) {
            mapBytes = archive->read(candidate.relativePath);
            if (!mapBytes.empty()) break;
          }
        } else {
          mapBytes = readFile(assets.tfDirectory
            / std::filesystem::path(candidate.relativePath));
        }
        if (!mapBytes.empty()) {
          mapLookupStore = candidate.store;
          mapLookupSelected = candidate.relativePath;
          mapPath = std::filesystem::path(candidate.relativePath);
          mapLookupReason = "found";
          break;
        }
      }
      if (mapBytes.empty()) {
        mapLookupReason = "not found in maps, download/maps or a vpk";
      }
    }
    traceFmt("map: lookup map=%s store=%s selected=%s bytes=%zu reason=%s",
      requestedMap.c_str(), mapLookupStore.c_str(),
      mapLookupSelected.empty() ? "-" : mapLookupSelected.c_str(),
      mapBytes.size(), mapLookupReason.c_str());
    tf2::native::BspParser::parse(mapBytes, displayMap, 200000);
    // T24: the bytes now live in an owner that survives this block, because the
    // pakfile reader below is called from outside it. `mapBytesView` is the
    // local name for the same bytes; `mapBytes` must not be used again.
    mapPakfileBytes = std::make_shared<const std::vector<std::uint8_t>>(std::move(mapBytes));
    const auto& mapBytesView = *mapPakfileBytes;
    // The map's cubemap, resolved the way the capability layer measures it: one
    // file per sample named after the sample's integer coordinates, in the tf
    // tree or the VPKs, and -- for every TF2 map -- inside the BSP's own pakfile
    // lump, which is the only store that has it. The first sample that yields six
    // faces is the one bound. One cube per map, because the world pass has one
    // env slot; which sample it came from is reported rather than assumed.
    for (const auto& sample : displayMap.cubemaps) {
      std::string cubemapPath;
      if (!tf2::native::worldCubemapVtfPath(std::filesystem::path(mapPath).stem().string(),
          sample.origin, cubemapPath)) continue;
      std::vector<std::uint8_t> cubemapBytes = readMaterialVtf(cubemapPath);
      std::string readReason;
      if (cubemapBytes.empty()
          && !tf2::native::bspPakfileEntry(mapBytesView, cubemapPath, cubemapBytes, &readReason)) {
        // The reader's own reason, not "missing": a pakfile entry stored with a
        // method this reader does not decode is not the same finding as a map
        // that has no cubemap, and the difference is the whole of this reading.
        mapCubemapReason = cubemapPath + ": " + readReason;
        continue;
      }
      if (cubemapBytes.empty()) { mapCubemapReason = "unreadable: " + cubemapPath; continue; }
      tf2::native::VtfTexture cube;
      std::string cubeError;
      if (!cube.parse(cubemapBytes, &cubeError)) { mapCubemapReason = cubemapPath + ": " + cubeError; continue; }
      std::array<std::vector<std::uint8_t>, 6> faces;
      bool complete = true;
      for (int face = 0; face < 6 && complete; ++face) {
        std::uint32_t faceWidth = 0, faceHeight = 0;
        faces[face] = cube.decodeCubeFaceRgba(cubemapBytes, face, faceWidth, faceHeight, &cubeError);
        if (faces[face].empty() || faceWidth != faceHeight
            || faceWidth != cube.header().width || faceHeight != cube.header().height) {
          mapCubemapReason = cubemapPath + ": face " + std::to_string(face) + " " + cubeError;
          complete = false;
        }
      }
      if (!complete) continue;
      mapCubemapFaces = std::move(faces);
      mapCubemapSize = cube.header().width;
      mapCubemapSource = cubemapPath;
      mapCubemapReason = "all six faces decoded";
      break;
    }
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
        if (!mapVmt.empty() && mapMaterial.valid
            && (!mapMaterial.baseTexture.empty() || !mapMaterial.baseTexture2.empty())) {
          const auto& displayBase = mapMaterial.baseTexture.empty()
            ? mapMaterial.baseTexture2 : mapMaterial.baseTexture;
          mapTexturePath = tf2::native::VmtParser::resourcePath(displayBase, ".vtf");
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
        // T24: the diagnostic bump binding. The reference frame's first material
        // (WOOD/WALL007A) declares no $bumpmap, so without this the bump branch
        // has no texture to sample and an A/B of the switch measures nothing.
        // The override binds exactly one named VTF for the whole world -- the
        // same one-slot limitation the refusal names -- and the trace says the
        // texture was forced rather than named by a material.
        if (!worldBumpTextureOverride.empty()) {
          const std::string overrideNarrow =
            std::filesystem::path(worldBumpTextureOverride).u8string();
          const std::string overridePath = tf2::native::VmtParser::resourcePath(
            overrideNarrow, ".vtf");
          const auto& bytes = readMaterialVtf(overridePath);
          tf2::native::VtfTexture texture;
          // The chain's pakfile reader is registered later in this block, so a
          // pakfile-only VTF would read as missing here. The owner is already
          // available, so ask it directly -- the same fallback the cubemap loop
          // above uses, for the same reason.
          std::vector<std::uint8_t> pakfileBytes;
          const std::vector<std::uint8_t>* textureBytes = &bytes;
          if (bytes.empty() && mapPakfileBytes
              && tf2::native::bspPakfileEntry(*mapPakfileBytes, overridePath, pakfileBytes, nullptr)) {
            textureBytes = &pakfileBytes;
          }
          if (!textureBytes->empty() && texture.parse(*textureBytes)
              && !texture.header().isCubemap()) {
            auto rgba = texture.decodeRgba(*textureBytes);
            if (!rgba.empty()) {
              mapBumpRgba = std::move(rgba);
              mapBumpWidth = texture.header().width;
              mapBumpHeight = texture.header().height;
              mapBumpMapPath = overridePath;
              mapMaterialParams.bumpMapping = true;
              mapBumpTextureForced = true;
            }
          }
          if (!mapBumpTextureForced) {
            OutputDebugStringW(L"TF2 Demo Player: --world-bump-texture did not decode; the bump branch is unavailable.\n");
          }
        }
        if (forceWorldEnv) mapMaterialParams.envMap = true;
        if (!mapMaterial.envMap.empty()) {
          // `envMap` means the MATERIAL declares an env map, not that the 2D
          // projection of it resolved. The two were the same field while the 2D
          // path was the only one: a material whose $envmap is a cubemap VTF
          // decoded to nothing, so the declaration read as absent and the map's
          // own TextureCube -- the resource built for exactly that material --
          // was never sampled. The renderer now picks between the cube and the
          // 2D texture from what is bound; the declaration only says the pass is
          // wanted at all.
          mapMaterialParams.envMap = true;
          mapEnvMapPath = tf2::native::VmtParser::resourcePath(mapMaterial.envMap, ".vtf");
          const auto& bytes = readMaterialVtf(mapEnvMapPath);
          tf2::native::VtfTexture texture;
          if (!bytes.empty() && texture.parse(bytes) && !texture.header().isCubemap()) {
            mapEnvRgba = texture.decodeRgba(bytes);
            mapEnvWidth = texture.header().width; mapEnvHeight = texture.header().height;
          }
        }
      }
    }
    if (displayMap.valid) {
      // T10: what the texture fetch actually did. The reference frame shows
      // whole surfaces flat, and this loop is where a material leaves the
      // textured path silently -- capped, unreadable, no $basetexture, VTF
      // unreadable, or larger than the 512-texel tile. Each exit is counted, and
      // the counts are printed, so "the map drew flat" becomes a number with a
      // reason instead of a look.
      // 256 is the ceiling, not the policy: the reference frame resolves 148,
      // and 256 tiles at 512 texels is 270 MiB of layer, which is under the
      // planner's 384 MiB budget. A map past it is reported, not truncated in
      // silence.
      constexpr std::size_t kMaxWorldMaterials = 256;
      std::size_t fetchDistinct = 0, fetchCapped = 0, fetchVmtMissing = 0, fetchVmtNoBase = 0;
      std::size_t fetchVtfMissing = 0, fetchVtfTooLarge = 0, fetchDecodeEmpty = 0, fetchTiled = 0;
      std::size_t fetchMaxWidth = 0, fetchMaxHeight = 0;
      std::size_t fetchFromPakfile = 0, fetchDownscaled = 0;
      // T12: the blend path's own counts. Declared is what the VMTs asked for;
      // resolved is what reached the atlas; refused is what did not, each with
      // a named reason on its own trace line.
      std::size_t fetchBlendDeclared = 0, fetchBlendResolved = 0, fetchBlendRefused = 0, fetchBlendResampled = 0;
      // T12 post-merge: the ordering's *outcome*, not just its input. `blendFirst`
      // counts how many materials declare a second layer; that number does not
      // move when the sort ignores the declaration, which is exactly the defect
      // the ordering exists to avoid. `blendLastRank` is the 1-based position of
      // the last blend material in the sorted order (0 when there is none), and
      // `blendCapped` is how many of them were dropped by the cap. With the sort
      // in place the rank is 3 and the cap drops none; with a triangle-count
      // order the rank runs past 64 and the cap drops the map's third terrain
      // material.
      std::size_t fetchBlendFirst = 0;
      std::size_t fetchBlendLastRank = 0;
      std::size_t fetchBlendCapped = 0;
      // T12: a layer skipped because `--world-blend off` asked for it is not a
      // refusal. Counting the two together would make the switch look like a
      // resolution failure in the summary line, which is the reading a reader
      // uses to decide whether the assets are there.
      std::size_t fetchBlendDisabled = 0;
      // T12: the switch's effective value, resolved once. `--world-blend` with
      // no `--world-blend` argument keeps the default (on), which is what every
      // existing gate relies on.
      const bool blendEnabled = worldBlendGiven ? worldBlendRequested : true;
      std::size_t fetchModulateDeclared = 0, fetchModulateResolved = 0, fetchModulateRefused = 0;
      // T27: the three feature layers, each with its own declared/resolved/
      // refused split. `bumpSsBump` is a refusal that is *not* a missing
      // resource -- the VTF is there and decodes -- so it is counted apart from
      // `bumpMissing`: folding them together would make a semantic refusal look
      // like a broken install.
      std::size_t fetchBumpDeclared = 0, fetchBumpResolved = 0, fetchBumpMissing = 0, fetchBumpSsBump = 0;
      std::size_t fetchBump2Declared = 0, fetchBump2Resolved = 0, fetchBump2Missing = 0;
      std::size_t fetchBump2SsBump = 0, fetchBump2Forced = 0;
      std::size_t fetchDetailDeclared = 0, fetchDetailResolved = 0, fetchDetailNoVmt = 0;
      std::size_t fetchDetailNoBase = 0, fetchDetailModeRefused = 0, fetchDetailForced = 0;
      std::size_t fetchDetailVtfMissing = 0;
      // The modes actually drawn, so the trace can say "2 materials at mode 0,
      // 1 at mode 8" instead of "3 detail layers". `$detailblendmode` decides
      // which branch of TextureCombine runs, so a count without the mode is a
      // count of layers that draw differently from one another.
      std::map<int, std::size_t> detailModeCounts;
      const bool materialBumpEnabled = persistent.render.materialBump;
      const bool bump2Enabled = persistent.render.bumpMap2 && materialBumpEnabled;
      const bool detailEnabled = persistent.render.detailLayer;
      // T27: the layer-2 diagnostic texture, decoded once before the loop.
      std::vector<std::uint8_t> bump2OverridePixels;
      std::string bump2OverridePath;
      UINT bump2OverrideWidth = 0, bump2OverrideHeight = 0;
      if (bump2Enabled && !worldBump2TextureOverride.empty()) {
        bump2OverridePath = tf2::native::VmtParser::resourcePath(
          std::filesystem::path(worldBump2TextureOverride).u8string(), ".vtf");
        const auto overrideBytes = assetChain.lookup(bump2OverridePath).bytes;
        tf2::native::VtfTexture overrideTexture;
        if (!overrideBytes.empty() && overrideTexture.parse(overrideBytes)
            && !overrideTexture.header().isCubemap()) {
          bump2OverridePixels = overrideTexture.decodeRgba(overrideBytes);
          bump2OverrideWidth = overrideTexture.header().width;
          bump2OverrideHeight = overrideTexture.header().height;
          worldBump2Diagnostic = !bump2OverridePixels.empty();
        }
        if (bump2OverridePixels.empty()) {
          // Named rather than ignored: a typo in the path would otherwise look
          // exactly like "the layer drew nothing", which is the failure mode
          // this whole task exists to make impossible to say.
          traceFmt("gpu: world feature layer=bump2 override status=refused path=%s"
            " reason=the --world-bump2-texture VTF did not resolve or decode",
            bump2OverridePath.c_str());
          bump2OverridePath.clear();
        }
      }
      // T19: the fetch no longer stops at 64. The renderer's plan chunks the
      // materials it is handed into as many atlases as they need, so the only
      // bound here is the hard ceiling -- a map with more distinct materials
      // than this is refused by name (`capped`) rather than silently drawn
      // flat, and the fetch order is still coverage order so a ceiling would
      // drop the smallest surfaces first.
      // T12: blend materials are offered before single-layer materials so a
      // future budget refusal cannot silently remove a terrain layer. The
      // blend declaration uses the same store chain as the actual fetch.
      std::unordered_map<std::string, bool> declaresBlend;
      std::vector<std::pair<std::size_t, std::string>> materialOrder;
      // The map's pakfile is the last store in the chain, and it only exists
      // once the map bytes do. Registering it before the declaration scan keeps
      // both the scan and the fetch on one lookup policy. Capturing the shared
      // owner by value keeps it alive for later feature scans as well.
      if (mapPakfileBytes && !mapPakfileBytes->empty()) {
        assetChain.setPakfileReader([mapPakfileBytes](const std::string& assetPath,
            std::vector<std::uint8_t>& bytes, std::string& reason) {
          return tf2::native::bspPakfileEntry(*mapPakfileBytes, assetPath, bytes, &reason);
        });
      }
      const auto readVmt = [&](const std::string& materialName) {
        const auto materialPath = tf2::native::VmtParser::resourcePath(materialName, ".vmt");
        return assetChain.lookup(materialPath).bytes;
      };
      {
        std::unordered_map<std::string, std::size_t> coverage;
        for (const auto& triangle : displayMap.triangles) {
          if (triangle.material.empty()) continue;
          ++coverage[triangle.material];
        }
        materialOrder.reserve(coverage.size());
        for (const auto& entry : coverage) materialOrder.emplace_back(entry.second, entry.first);
        // T19: the pair is (triangles, name), so the count is `first`. This
        // compared `second` -- the *name* -- which sorted the fetch into reverse
        // alphabetical order while the comment below called it coverage order.
        // Measured on cp_snakewater: the 64 tiles the old ceiling kept began
        // with `maps/.../blendgroundtograss008_wvt_patch` (117 triangles) and
        // the map's largest material, MEDIEVAL/PLASTERWALL002A at 80256
        // triangles, sat at index 71. The order is what decides which materials
        // a budget refusal drops first, so it has to be the one it claims.
        for (const auto& entry : materialOrder) {
          const auto bytes = readVmt(entry.second);
          if (bytes.empty()) continue;
          tf2::native::VmtMaterial material;
          if (!tf2::native::VmtParser::parse(std::string(bytes.begin(), bytes.end()), material)) continue;
          declaresBlend[entry.second] = !material.baseTexture2.empty()
            && material.baseTexture2 != material.baseTexture;
        }
        const auto blendOf = [&](const std::string& name) {
          const auto it = declaresBlend.find(name);
          return it != declaresBlend.end() && it->second;
        };
        std::sort(materialOrder.begin(), materialOrder.end(), [&](const auto& l, const auto& r) {
          const bool lb = blendOf(l.second), rb = blendOf(r.second);
          if (lb != rb) return lb;
          return l.first != r.first ? l.first > r.first : l.second < r.second;
        });
        std::size_t blendFirst = 0;
        for (std::size_t i = 0; i < materialOrder.size(); ++i) {
          if (!blendOf(materialOrder[i].second)) continue;
          ++blendFirst;
          fetchBlendLastRank = i + 1;  // 1-based: "the last blend material is the Nth to be offered a tile"
        }
        fetchBlendFirst = blendFirst;
      }
      // A VTF larger than the tile is downscaled to the tile instead of being
      // dropped: measured on pl_odyssey, 42 of 108 materials shipped
      // 1024-wide/2048-tall textures and every one of them drew flat.
      //
      // T12: `resampleTo` takes the destination size explicitly, because a
      // blend material's second layer has to land on the *same* pixel grid as
      // its first: the world shader samples both with one uv, and that is only
      // sound while both tiles are the same rectangle with the same texel
      // count. `boxDownscale` keeps its old meaning (fit inside a limit) and is
      // now a caller of it.
      const auto resampleTo = [](const std::vector<std::uint8_t>& source, UINT width, UINT height,
                                 UINT outWidth, UINT outHeight, std::vector<std::uint8_t>& out) {
        if (width == 0 || height == 0 || outWidth == 0 || outHeight == 0) return false;
        if (source.size() != static_cast<std::size_t>(width) * height * 4u) return false;
        if (width == outWidth && height == outHeight) { out = source; return true; }
        out.assign(static_cast<std::size_t>(outWidth) * outHeight * 4u, 0);
        for (UINT y = 0; y < outHeight; ++y) {
          const UINT sy0 = static_cast<UINT>(static_cast<std::uint64_t>(y) * height / outHeight);
          const UINT sy1 = std::max<UINT>(sy0 + 1, static_cast<UINT>(static_cast<std::uint64_t>(y + 1) * height / outHeight));
          for (UINT x = 0; x < outWidth; ++x) {
            const UINT sx0 = static_cast<UINT>(static_cast<std::uint64_t>(x) * width / outWidth);
            const UINT sx1 = std::max<UINT>(sx0 + 1, static_cast<UINT>(static_cast<std::uint64_t>(x + 1) * width / outWidth));
            std::size_t sum[4] = {0, 0, 0, 0};
            UINT count = 0;
            for (UINT sy = sy0; sy < sy1 && sy < height; ++sy) {
              for (UINT sx = sx0; sx < sx1 && sx < width; ++sx) {
                const std::size_t at = (static_cast<std::size_t>(sy) * width + sx) * 4u;
                for (int c = 0; c < 4; ++c) sum[c] += source[at + static_cast<std::size_t>(c)];
                ++count;
              }
            }
            if (count == 0) count = 1;
            const std::size_t at = (static_cast<std::size_t>(y) * outWidth + x) * 4u;
            for (int c = 0; c < 4; ++c) out[at + static_cast<std::size_t>(c)] = static_cast<std::uint8_t>(sum[c] / count);
          }
        }
        return true;
      };
      const auto boxDownscale = [&](const std::vector<std::uint8_t>& source, UINT width, UINT height,
                                    UINT limit, std::vector<std::uint8_t>& out, UINT& outWidth, UINT& outHeight) {
        if (width <= limit && height <= limit) {
          out = source; outWidth = width; outHeight = height;
          return true;
        }
        const float scale = static_cast<float>(limit) / static_cast<float>(std::max(width, height));
        outWidth = std::max<UINT>(1u, static_cast<UINT>(static_cast<float>(width) * scale));
        outHeight = std::max<UINT>(1u, static_cast<UINT>(static_cast<float>(height) * scale));
        if (outWidth == 0 || outHeight == 0) return false;
        return resampleTo(source, width, height, outWidth, outHeight, out);
      };
      // T27: `$detailscale` baked into pixels. The SDK multiplies the detail
      // texcoord in the vertex stage (`i.detailOrBumpAndEnvmapMaskTexCoord`),
      // which needs a second texcoord the world vertex does not carry. Repeating
      // the detail texture `repeat` times across the base tile is the same
      // picture, because the atlas uv is normalised over exactly one base tile:
      // source uv = atlas uv * repeat. Bilinear with wrap, so a scale of 1.9
      // reads as a repeat rather than as a seam at the tile edge.
      const auto tileRepeat = [](const std::vector<std::uint8_t>& source, UINT width, UINT height,
                                 float repeat, UINT outWidth, UINT outHeight,
                                 std::vector<std::uint8_t>& out) {
        if (width == 0 || height == 0 || outWidth == 0 || outHeight == 0) return false;
        if (source.size() != static_cast<std::size_t>(width) * height * 4u) return false;
        if (!(repeat > 0.0f) || !std::isfinite(repeat)) return false;
        out.assign(static_cast<std::size_t>(outWidth) * outHeight * 4u, 0);
        const auto wrap = [](int value, int limit) {
          const int period = limit > 0 ? limit : 1;
          int wrapped = value % period;
          if (wrapped < 0) wrapped += period;
          return wrapped;
        };
        for (UINT y = 0; y < outHeight; ++y) {
          const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(outHeight) * repeat;
          const int y0 = static_cast<int>(std::floor(v - 0.5f));
          const float fy = (v - 0.5f) - static_cast<float>(y0);
          for (UINT x = 0; x < outWidth; ++x) {
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(outWidth) * repeat;
            const int x0 = static_cast<int>(std::floor(u - 0.5f));
            const float fx = (u - 0.5f) - static_cast<float>(x0);
            float texel[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            for (int dy = 0; dy < 2; ++dy) {
              for (int dx = 0; dx < 2; ++dx) {
                const int sx = wrap(x0 + dx, static_cast<int>(width));
                const int sy = wrap(y0 + dy, static_cast<int>(height));
                const float weight = (dx == 0 ? 1.0f - fx : fx) * (dy == 0 ? 1.0f - fy : fy);
                const std::size_t at = (static_cast<std::size_t>(sy) * width
                  + static_cast<std::size_t>(sx)) * 4u;
                for (int c = 0; c < 4; ++c) texel[c] += weight * static_cast<float>(source[at + static_cast<std::size_t>(c)]);
              }
            }
            const std::size_t at = (static_cast<std::size_t>(y) * outWidth + x) * 4u;
            for (int c = 0; c < 4; ++c) {
              out[at + static_cast<std::size_t>(c)] = static_cast<std::uint8_t>(
                std::clamp(texel[c] + 0.5f, 0.0f, 255.0f));
            }
          }
        }
        return true;
      };
      for (const auto& entry : materialOrder) {
        const std::string& materialName = entry.second;
        ++fetchDistinct;
        auto& chain = worldMaterialChain[materialName];
        chain.triangles = entry.first;
        if (worldTextures.size() >= kMaxWorldMaterials) {
          if (declaresBlend.count(materialName) && declaresBlend[materialName]) ++fetchBlendCapped;
          ++fetchCapped;
          chain.reason = "world-material-cap";
          continue;
        }
        const auto materialPath = tf2::native::VmtParser::resourcePath(materialName, ".vmt");
        chain.vmtPath = materialPath;
        const auto materialLookup = assetChain.lookup(materialPath);
        chain.vmtStore = tf2::native::assetStoreName(materialLookup.store);
        auto materialBytes = materialLookup.bytes;
        if (materialLookup.store == tf2::native::AssetStore::Pakfile) ++fetchFromPakfile;
        tf2::native::VmtMaterial material;
        if (materialBytes.empty()) { ++fetchVmtMissing; chain.reason = "vmt-missing"; continue; }
        if (!tf2::native::VmtParser::parse(std::string(materialBytes.begin(), materialBytes.end()), material)) {
          ++fetchVmtNoBase; chain.reason = "vmt-unparsable"; continue;
        }
        // Recorded before the base-texture check: a material with no
        // `$basetexture` still declares `$detail` or `$ssbump`, and the feature
        // scan's question is what the map's materials ask for.
        worldFetchedMaterials.emplace_back(materialName, material);
        if (material.baseTexture.empty() && material.baseTexture2.empty()) {
          ++fetchVmtNoBase; chain.reason = "no-basetexture"; continue;
        }
        // T12: one lookup policy, written once and used for every resource a
        // material names. Loose file, then the archives in the order this file
        // opened them, then the map's own pakfile -- the same order for the
        // first layer, the second layer and the blend mask, so a material
        // cannot resolve one of them from a store the others were never
        // offered.
        const auto resolveTexture = [&](const std::string& name, std::string& path,
                                        std::vector<std::uint8_t>& bytes,
                                        std::string* storeOut = nullptr) {
          if (name.empty()) return false;
          path = tf2::native::VmtParser::resourcePath(name, ".vtf");
          auto looked = assetChain.lookup(path);
          if (looked.store == tf2::native::AssetStore::Pakfile) ++fetchFromPakfile;
          if (storeOut) *storeOut = tf2::native::assetStoreName(looked.store);
          bytes = std::move(looked.bytes);
          return !bytes.empty();
        };
        std::string texturePath, secondPath, modulatePath;
        std::vector<std::uint8_t> textureBytes, secondBytes, modulateBytes;
        chain.baseTexture = material.baseTexture.empty() ? material.baseTexture2 : material.baseTexture;
        const bool baseResolved = resolveTexture(material.baseTexture, texturePath, textureBytes,
          &chain.vtfStore);
        chain.vtfPath = texturePath;
        // A material is a blend material only when it names a *different*
        // second layer. The comparison is on the declared name, not on the
        // resolved path: `$basetexture` and `$basetexture2` naming the same
        // texture is a single-layer material with a redundant declaration, and
        // blending it toward itself would cost a sampler read and change
        // nothing.
        const bool secondDeclared = !material.baseTexture2.empty()
          && material.baseTexture2 != material.baseTexture;
        std::string secondStore;
        const bool secondResolved = secondDeclared && blendEnabled
          && resolveTexture(material.baseTexture2, secondPath, secondBytes, &secondStore);
        if (secondDeclared) ++fetchBlendDeclared;
        if (!baseResolved) {
          // The old behaviour, kept: a material whose `$basetexture` did not
          // resolve but whose `$basetexture2` did is drawn with the layer it
          // has. It cannot blend -- there is nothing to blend from -- and
          // `blendRefused` below is what says so rather than leaving a reader
          // to assume the second layer became the first.
          if (!secondResolved) { ++fetchVtfMissing; chain.reason = "vtf-missing"; continue; }
          texturePath = secondPath; textureBytes = std::move(secondBytes);
          chain.vtfPath = secondPath;
          chain.vtfStore = secondStore.empty() ? chain.vtfStore : secondStore;
        }
        tf2::native::VtfTexture texture;
        if (textureBytes.empty() || !texture.parse(textureBytes)) {
          ++fetchVtfMissing; chain.reason = "vtf-unreadable"; continue;
        }
        chain.sourceWidth = static_cast<int>(texture.header().width);
        chain.sourceHeight = static_cast<int>(texture.header().height);
        if (texture.header().width > fetchMaxWidth) fetchMaxWidth = texture.header().width;
        if (texture.header().height > fetchMaxHeight) fetchMaxHeight = texture.header().height;
        if (texture.header().width > 512 || texture.header().height > 512) ++fetchVtfTooLarge;
        auto rgba = texture.decodeRgba(textureBytes);
        if (rgba.empty()) { ++fetchDecodeEmpty; chain.reason = "decode-empty"; continue; }
        std::vector<std::uint8_t> tiledPixels;
        UINT tiledWidth = texture.header().width, tiledHeight = texture.header().height;
        if (!boxDownscale(rgba, texture.header().width, texture.header().height, 512,
                          tiledPixels, tiledWidth, tiledHeight)) {
          ++fetchDecodeEmpty; chain.reason = "downscale-failed"; continue;
        }
        chain.tileWidth = static_cast<int>(tiledWidth);
        chain.tileHeight = static_cast<int>(tiledHeight);
        chain.blend = secondDeclared;
        chain.reason = "tiled";
        // ---- T12: the second layer ------------------------------------------
        // What used to happen here was a CPU fold that used the *first
        // texture's alpha* as the blend mask. That is not what Source does and
        // it is why the terrain read wrong: on the reference map both layers
        // are opaque DXT1, so the mask was 1 everywhere and every displacement
        // drew the second layer flat, ignoring the weight the map author
        // painted into the displacement.
        //
        // The weight is per vertex (`CDispVert::m_flAlpha`), so it cannot be
        // folded into a texture at all. The layer is resampled onto the first
        // layer's tile grid and handed to the renderer, which blends it in the
        // pixel shader by the vertex weight.
        std::vector<std::uint8_t> blendPixels;
        std::vector<std::uint8_t> modulatePixels;
        std::string blendReason;
        UINT blendSourceWidth = 0, blendSourceHeight = 0;
        if (secondDeclared && !blendEnabled) {
          // The switch was asked for, so the layer is not fetched and no
          // reason is invented for it. The material keeps its first layer and
          // its geometry; only the second layer and the per-vertex weight are
          // withheld, which is what makes the two frames differ by the blend
          // alone.
          ++fetchBlendDisabled;
          traceFmt("gpu: world blend material=%s second=%s status=off reason=--world-blend off",
            materialName.c_str(), material.baseTexture2.c_str());
        } else if (secondDeclared) {
          // T12 post-merge: the decision lives in `resolveBlendLayer`, so the
          // six reasons below are the same code the refusal fixtures execute.
          // Inline, the only way to reach one of them was to find a map broken
          // in exactly that way, and on the reference map none was reached.
          const auto outcome = tf2::native::resolveBlendLayer(
            secondPath, secondBytes, baseResolved, tiledWidth, tiledHeight);
          blendReason = outcome.reason;
          if (outcome.status == tf2::native::BlendLayerOutcome::Status::Resolved) {
            blendPixels = outcome.pixels;
            blendSourceWidth = outcome.sourceWidth;
            blendSourceHeight = outcome.sourceHeight;
            ++fetchBlendResolved;
            if (outcome.resampled) ++fetchBlendResampled;
          }
          if (!blendReason.empty()) {
            ++fetchBlendRefused;
            traceFmt("gpu: world blend material=%s second=%s status=refused reason=%s",
              materialName.c_str(), secondPath.empty() ? material.baseTexture2.c_str() : secondPath.c_str(),
              blendReason.c_str());
          } else {
            // The dimensions are the reading the task asks for: what the
            // source layer was, and what it became on the tile grid.
            traceFmt("gpu: world blend material=%s first=%s second=%s source=%zux%zu tile=%zux%zu modulate=%s",
              materialName.c_str(), texturePath.c_str(), secondPath.c_str(),
              static_cast<std::size_t>(blendSourceWidth), static_cast<std::size_t>(blendSourceHeight),
              static_cast<std::size_t>(tiledWidth), static_cast<std::size_t>(tiledHeight),
              material.blendModulateTexture.c_str());
          }
        }
        // ---- T12: `$blendmodulatetexture` ------------------------------------
        // Source reads it as `smoothstep(g - r, g + r, vertexAlpha)`, so it is a
        // third texture on the same grid, not a tint. A material that declares
        // one and is drawn without it gets a hard cut where the map author
        // asked for a soft edge, which is the other half of "the terrain does
        // not look like the terrain".
        if (blendEnabled && !material.blendModulateTexture.empty()) {
          ++fetchModulateDeclared;
          // T12 post-merge: the three reasons live in `resolveBlendModulate`,
          // the same code `terrain_blend_probe --refusal-fixtures` executes.
          // Inline, reaching one of them required a map that ships a broken
          // modulate -- and the reference map has none, so `modulateRefused=0`
          // was a counter with no execution behind it.
          const bool secondResolvedForModulate = !blendPixels.empty();
          if (secondResolvedForModulate) {
            resolveTexture(material.blendModulateTexture, modulatePath, modulateBytes);
          }
          const auto modulateOutcome = tf2::native::resolveBlendModulate(
            material.blendModulateTexture, modulateBytes, secondResolvedForModulate,
            tiledWidth, tiledHeight);
          if (modulateOutcome.status == tf2::native::BlendModulateOutcome::Status::Resolved) {
            modulatePixels = modulateOutcome.pixels;
            ++fetchModulateResolved;
            traceFmt("gpu: world blend-modulate material=%s path=%s source=%zux%zu tile=%zux%zu",
              materialName.c_str(), modulatePath.c_str(),
              static_cast<std::size_t>(modulateOutcome.sourceWidth),
              static_cast<std::size_t>(modulateOutcome.sourceHeight),
              static_cast<std::size_t>(tiledWidth), static_cast<std::size_t>(tiledHeight));
          } else {
            // "No second layer to modulate" is not a resource refusal -- there
            // is nothing the lookup could have found -- so it is traced but not
            // counted, which is the reading the old inline code produced.
            if (modulateOutcome.refusal != tf2::native::BlendModulateOutcome::Refusal::NoSecondLayer) {
              ++fetchModulateRefused;
            }
            if (modulateOutcome.refusal == tf2::native::BlendModulateOutcome::Refusal::NoSecondLayer) {
              traceFmt("gpu: world blend-modulate material=%s status=refused reason=%s",
                materialName.c_str(), modulateOutcome.reason.c_str());
            } else {
              traceFmt("gpu: world blend-modulate material=%s path=%s status=refused reason=%s",
                materialName.c_str(), modulatePath.c_str(), modulateOutcome.reason.c_str());
            }
          }
        }
        // ---- T27: the three feature layers --------------------------------
        // Each is fetched onto the SAME pixel grid as the albedo tile
        // (`tiledWidth` x `tiledHeight`), because the renderer's feature atlas
        // reuses the albedo tile's rectangle: one uv addresses both, and a layer
        // on a different grid would sample the wrong texel. A layer is fetched
        // only when its own switch is on, so a capture changes one semantic and
        // the off frame allocates and samples nothing.
        std::vector<std::uint8_t> bumpPixels, bump2Pixels, detailPixels;
        if (materialBumpEnabled && !material.bumpMap.empty()) {
          ++fetchBumpDeclared;
          if (material.ssBump) {
            // Source compiles `$ssbump` as `BUMPMAP == 2`: the texel is NOT
            // remapped with `* 2 - 1` and is consumed as weights on
            // `bumpBasis[0..2]` against three lightmap directions
            // (lightmappedgeneric_ps2_3_x.h, the `BUMPMAP == 2` block). This
            // pass has one lightmap and no basis, so the material is refused by
            // name instead of being decoded as a tangent-space normal it is not.
            ++fetchBumpSsBump;
            traceFmt("gpu: world feature material=%s layer=bump path=%s status=refused"
              " reason=$ssbump: the texel is a bumpBasis weight, not a tangent-space normal",
              materialName.c_str(), material.bumpMap.c_str());
          } else {
            std::string bumpPath;
            std::vector<std::uint8_t> bumpBytes;
            tf2::native::VtfTexture bumpTexture;
            if (!resolveTexture(material.bumpMap, bumpPath, bumpBytes)
                || !bumpTexture.parse(bumpBytes) || bumpTexture.header().isCubemap()) {
              ++fetchBumpMissing;
              traceFmt("gpu: world feature material=%s layer=bump path=%s status=refused"
                " reason=the $bumpmap VTF did not resolve or decode",
                materialName.c_str(), bumpPath.empty() ? material.bumpMap.c_str() : bumpPath.c_str());
            } else {
              auto bumpRgba = bumpTexture.decodeRgba(bumpBytes);
              std::vector<std::uint8_t> resampled;
              if (bumpRgba.empty()
                  || !resampleTo(bumpRgba, bumpTexture.header().width, bumpTexture.header().height,
                                 tiledWidth, tiledHeight, resampled)) {
                ++fetchBumpMissing;
                traceFmt("gpu: world feature material=%s layer=bump path=%s status=refused"
                  " reason=the VTF decoded empty or would not resample to the tile grid",
                  materialName.c_str(), bumpPath.c_str());
              } else {
                bumpPixels = std::move(resampled);
                ++fetchBumpResolved;
                // The dimensions are the reading: a normal map on a different
                // grid from its albedo is the failure this line can show.
                traceFmt("gpu: world feature material=%s layer=bump path=%s source=%zux%zu"
                  " tile=%zux%zu ssbump=0 status=resolved",
                  materialName.c_str(), bumpPath.c_str(),
                  static_cast<std::size_t>(bumpTexture.header().width),
                  static_cast<std::size_t>(bumpTexture.header().height),
                  static_cast<std::size_t>(tiledWidth), static_cast<std::size_t>(tiledHeight));
              }
            }
          }
        }
        // T27: `$bumpmap2`, layer 2's normal. Three things can stop it, and each
        // one is counted separately because they mean different things:
        //   * the material declares `$ssbump`, so the DECLARED second-layer
        //     texel is a bumpBasis weight rather than a signed normal (the same
        //     refusal as the `$bumpmap` row, and 4 of this map's 5 $bumpmap2
        //     materials are in this state);
        //   * its VTF did not resolve;
        //   * the diagnostic override replaced the texels.
        // The override is tried first and counted as `bump2Forced`: it exists
        // because the two facts above leave this demo with no visible pixel of
        // the feature at all (the one non-ssbump material is under the map), and
        // a gate that cannot move a pixel cannot tell "implemented" from "counter
        // incremented". It replaces the texels only -- the blend weight the lerp
        // uses is still the material's own vertex alpha.
        if (bump2Enabled && !material.bumpMap2.empty()) {
          ++fetchBump2Declared;
          // `$ssbump` is a property of the material, so it is counted on every
          // path -- including the forced one, where the diagnostic texels mean it
          // does not stop the layer but the declaration is still true. The three
          // path counters (resolved / missing / forced) describe what was
          // sampled; this one describes what was declared.
          if (material.ssBump) ++fetchBump2SsBump;
          if (!bump2OverridePixels.empty()) {
            std::vector<std::uint8_t> resampled;
            if (!resampleTo(bump2OverridePixels, bump2OverrideWidth, bump2OverrideHeight,
                            tiledWidth, tiledHeight, resampled)) {
              ++fetchBump2Missing;
              traceFmt("gpu: world feature material=%s layer=bump2 path=%s status=refused"
                " reason=the --world-bump2-texture VTF would not resample to the tile grid",
                materialName.c_str(), material.bumpMap2.c_str());
            } else {
              bump2Pixels = std::move(resampled);
              ++fetchBump2Forced;
              traceFmt("gpu: world feature material=%s layer=bump2 path=%s declared=%s"
                " source=diagnostic tile=%zux%zu ssbump=%d status=forced",
                materialName.c_str(), bump2OverridePath.c_str(), material.bumpMap2.c_str(),
                static_cast<std::size_t>(tiledWidth), static_cast<std::size_t>(tiledHeight),
                material.ssBump ? 1 : 0);
            }
          } else if (material.ssBump) {
            traceFmt("gpu: world feature material=%s layer=bump2 path=%s status=refused"
              " reason=$ssbump: layer 2's texel is a bumpBasis weight, not a tangent-space normal",
              materialName.c_str(), material.bumpMap2.c_str());
          } else {
            std::string bump2Path;
            std::vector<std::uint8_t> bump2Bytes;
            tf2::native::VtfTexture bump2Texture;
            if (!resolveTexture(material.bumpMap2, bump2Path, bump2Bytes)
                || !bump2Texture.parse(bump2Bytes) || bump2Texture.header().isCubemap()) {
              ++fetchBump2Missing;
              traceFmt("gpu: world feature material=%s layer=bump2 path=%s status=refused"
                " reason=the $bumpmap2 VTF did not resolve or decode",
                materialName.c_str(), bump2Path.empty() ? material.bumpMap2.c_str() : bump2Path.c_str());
            } else {
              auto bump2Rgba = bump2Texture.decodeRgba(bump2Bytes);
              std::vector<std::uint8_t> resampled;
              if (bump2Rgba.empty()
                  || !resampleTo(bump2Rgba, bump2Texture.header().width, bump2Texture.header().height,
                                 tiledWidth, tiledHeight, resampled)) {
                ++fetchBump2Missing;
                traceFmt("gpu: world feature material=%s layer=bump2 path=%s status=refused"
                  " reason=the VTF decoded empty or would not resample to the tile grid",
                  materialName.c_str(), bump2Path.c_str());
              } else {
                bump2Pixels = std::move(resampled);
                ++fetchBump2Resolved;
                traceFmt("gpu: world feature material=%s layer=bump2 path=%s source=%zux%zu"
                  " tile=%zux%zu status=resolved",
                  materialName.c_str(), bump2Path.c_str(),
                  static_cast<std::size_t>(bump2Texture.header().width),
                  static_cast<std::size_t>(bump2Texture.header().height),
                  static_cast<std::size_t>(tiledWidth), static_cast<std::size_t>(tiledHeight));
              }
            }
          }
        }
        if (detailEnabled && !material.detailTexture.empty()) {
          ++fetchDetailDeclared;
          if (material.detailBlendModeUnsupported) {
            // TextureCombine has no branch here for the inputs this pass owns:
            // 4 needs the base's alpha, 5/6 run after lighting, 7 selects by
            // base alpha, 9 writes only alpha, 10/11 consume an SSBump detail.
            ++fetchDetailModeRefused;
            traceFmt("gpu: world feature material=%s layer=detail mode=%d status=refused"
              " reason=$detailblendmode has no TextureCombine branch this pass can feed",
              materialName.c_str(), material.detailBlendMode);
          } else {
            // The detail material's own texture. `$detail` names a VMT, not a
            // VTF, so the layer's pixels come from that material's
            // `$basetexture` -- and `%detailtype` is read from the same file.
            const std::string detailVmtPath =
              tf2::native::VmtParser::resourcePath(material.detailTexture, ".vmt");
            std::vector<std::uint8_t> detailRgba;
            tf2::native::VtfTexture detailTexture;
            std::string detailTexturePath;
            std::string detailType;
            std::vector<std::uint8_t> detailSourceBytes;
            UINT detailWidth = 0, detailHeight = 0;
            // The named reason this material's layer did not draw, or null when
            // it did. One variable rather than three counters compared against
            // zero: those counters are cumulative over the loop, so a failure on
            // an earlier material would have relabelled this one.
            const char* detailFailure = nullptr;
            if (!worldDetailTextureOverride.empty()) {
              // The diagnostic source, for the same reason T24's bump override
              // exists: every `$detail` VMT the reference map names is absent
              // from this install, so the branch has no pixels to sample and an
              // A/B would measure nothing.
              const std::string overrideNarrow =
                std::filesystem::path(worldDetailTextureOverride).u8string();
              detailTexturePath = tf2::native::VmtParser::resourcePath(overrideNarrow, ".vtf");
              detailSourceBytes = assetChain.lookup(detailTexturePath).bytes;
              if (!detailSourceBytes.empty() && detailTexture.parse(detailSourceBytes)
                  && !detailTexture.header().isCubemap()) {
                detailRgba = detailTexture.decodeRgba(detailSourceBytes);
                detailWidth = detailTexture.header().width;
                detailHeight = detailTexture.header().height;
              }
              if (detailRgba.empty()) {
                ++fetchDetailVtfMissing;
                detailFailure = "the --world-detail-texture VTF did not resolve or decode";
              } else {
                ++fetchDetailForced;
                // `%detailtype` still comes from the detail VMT when it exists,
                // even though the pixels do not: the type is the declaration
                // under test and the override only replaces the texels.
                const auto forcedVmtBytes = assetChain.lookup(detailVmtPath).bytes;
                tf2::native::VmtMaterial forcedDetail;
                if (!forcedVmtBytes.empty()
                    && tf2::native::VmtParser::parse(
                         std::string(forcedVmtBytes.begin(), forcedVmtBytes.end()), forcedDetail)) {
                  detailType = forcedDetail.detailType;
                }
              }
            } else {
              const auto detailVmtBytes = assetChain.lookup(detailVmtPath).bytes;
              tf2::native::VmtMaterial detailMaterial;
              if (detailVmtBytes.empty()
                  || !tf2::native::VmtParser::parse(
                       std::string(detailVmtBytes.begin(), detailVmtBytes.end()), detailMaterial)) {
                ++fetchDetailNoVmt;
                detailFailure = "the $detail VMT is absent from this install";
              } else {
                detailType = detailMaterial.detailType;
                std::vector<std::uint8_t> detailVtfBytes;
                if (detailMaterial.baseTexture.empty()
                    || !resolveTexture(detailMaterial.baseTexture, detailTexturePath, detailVtfBytes)
                    || !detailTexture.parse(detailVtfBytes)
                    || detailTexture.header().isCubemap()) {
                  ++fetchDetailNoBase;
                  detailFailure = "the detail material's $basetexture VTF did not resolve or decode";
                } else {
                  detailRgba = detailTexture.decodeRgba(detailVtfBytes);
                  detailWidth = detailTexture.header().width;
                  detailHeight = detailTexture.header().height;
                  if (detailRgba.empty()) {
                    ++fetchDetailVtfMissing;
                    detailFailure = "the detail VTF decoded empty";
                  }
                }
              }
            }
            std::vector<std::uint8_t> repeated;
            if (!detailFailure
                && !tileRepeat(detailRgba, detailWidth, detailHeight, material.detailScale,
                               tiledWidth, tiledHeight, repeated)) {
              ++fetchDetailVtfMissing;
              detailFailure = "the detail texture would not repeat onto the tile grid";
            }
            if (detailFailure) {
              traceFmt("gpu: world feature material=%s layer=detail vmt=%s texture=%s detailtype=%s"
                " mode=%d status=refused reason=%s",
                materialName.c_str(), detailVmtPath.c_str(), detailTexturePath.c_str(),
                detailType.empty() ? "-" : detailType.c_str(), material.detailBlendMode,
                detailFailure);
            } else {
              detailPixels = std::move(repeated);
              ++fetchDetailResolved;
              ++detailModeCounts[material.detailBlendMode];
              traceFmt("gpu: world feature material=%s layer=detail vmt=%s texture=%s detailtype=%s"
                " mode=%d factor=%.3f scale=%.3f tile=%zux%zu status=resolved",
                materialName.c_str(), detailVmtPath.c_str(), detailTexturePath.c_str(),
                detailType.empty() ? "-" : detailType.c_str(), material.detailBlendMode,
                static_cast<double>(material.detailBlendFactor),
                static_cast<double>(material.detailScale),
                static_cast<std::size_t>(tiledWidth), static_cast<std::size_t>(tiledHeight));
            }
          }
        }
        if (tiledWidth != texture.header().width || tiledHeight != texture.header().height) ++fetchDownscaled;
        ++fetchTiled;
        {
          tf2::native::WorldTexture tiled;
          tiled.material = materialName;
          tiled.rgba = std::move(tiledPixels);
          tiled.width = tiledWidth;
          tiled.height = tiledHeight;
          // T21: the uv normaliser. The tile was downscaled from these, and a
          // texcoord is in source texels.
          tiled.sourceWidth = texture.header().width;
          tiled.sourceHeight = texture.header().height;
          tiled.blendRgba = std::move(blendPixels);
          tiled.blendDeclared = secondDeclared;
          tiled.blendModulateRgba = std::move(modulatePixels);
          // T27: the three feature layers, already on this tile's grid. The
          // mode and factor are the *material's* `$detailblendmode` and
          // `$detailblendfactor`; the renderer copies them onto every vertex of
          // the material, because the pass has no per-material constant buffer.
          tiled.bumpRgba = std::move(bumpPixels);
          tiled.bumpDeclared = !material.bumpMap.empty();
          tiled.bump2Rgba = std::move(bump2Pixels);
          tiled.bump2Declared = !material.bumpMap2.empty();
          tiled.detailRgba = std::move(detailPixels);
          tiled.detailDeclared = !tiled.detailRgba.empty();
          tiled.detailMode = material.detailBlendMode;
          tiled.detailFactor = material.detailBlendFactor;
          tiled.detailScale = material.detailScale;
          tiled.ssBump = material.ssBump;
          worldTextures.push_back(std::move(tiled));
        }
      }
      traceFmt("gpu: world material-fetch distinct=%zu tiled=%zu capped=%zu vmtMissing=%zu vmtNoBase=%zu vtfMissing=%zu vtfTooLarge=%zu decodeEmpty=%zu maxWidth=%zu maxHeight=%zu fromPakfile=%zu downscaled=%zu blendFirst=%zu blendLastRank=%zu blendCapped=%zu",
        fetchDistinct, fetchTiled, fetchCapped, fetchVmtMissing, fetchVmtNoBase, fetchVtfMissing,
        fetchVtfTooLarge, fetchDecodeEmpty, fetchMaxWidth, fetchMaxHeight, fetchFromPakfile, fetchDownscaled,
        fetchBlendFirst, fetchBlendLastRank, fetchBlendCapped);
      traceFmt("gpu: world blend-fetch declared=%zu resolved=%zu refused=%zu disabled=%zu resampled=%zu modulateDeclared=%zu modulateResolved=%zu modulateRefused=%zu",
        fetchBlendDeclared, fetchBlendResolved, fetchBlendRefused, fetchBlendDisabled, fetchBlendResampled,
        fetchModulateDeclared, fetchModulateResolved, fetchModulateRefused);
      // T27: one line per layer, and the switches in it, so a reader can tell
      // "the switch was off" from "the switch was on and the resource was not
      // there" without diffing two traces. The mode histogram is on the detail
      // line because TextureCombine's branch is chosen by it: two detail layers
      // at different modes do not draw the same picture.
      {
        std::string detailModes;
        for (const auto& entry : detailModeCounts) {
          if (!detailModes.empty()) detailModes += ",";
          detailModes += std::to_string(entry.first) + "x" + std::to_string(entry.second);
        }
        if (detailModes.empty()) detailModes = "-";
        traceFmt("gpu: world feature-fetch bumpSwitch=%d bumpDeclared=%zu bumpResolved=%zu"
          " bumpMissing=%zu bumpSsBump=%zu bump2Switch=%d bump2Declared=%zu bump2Resolved=%zu"
          " bump2Missing=%zu bump2SsBump=%zu bump2Forced=%zu detailSwitch=%d detailDeclared=%zu"
          " detailResolved=%zu detailNoVmt=%zu detailNoBase=%zu detailVtfMissing=%zu"
          " detailModeRefused=%zu detailForced=%zu detailModes=%s",
          materialBumpEnabled ? 1 : 0, fetchBumpDeclared, fetchBumpResolved, fetchBumpMissing,
          fetchBumpSsBump, bump2Enabled ? 1 : 0, fetchBump2Declared, fetchBump2Resolved,
          fetchBump2Missing, fetchBump2SsBump, fetchBump2Forced, detailEnabled ? 1 : 0,
          fetchDetailDeclared, fetchDetailResolved,
          fetchDetailNoVmt, fetchDetailNoBase, fetchDetailVtfMissing, fetchDetailModeRefused,
          fetchDetailForced, detailModes.c_str());
      }
      // T21: what the PRE-T19 order would have done with the same materials.
      // The old comparator compared the pair's second field -- the material
      // NAME -- and therefore accepted materials in reverse alphabetical order
      // until `accepted.size()` reached 64. Reconstructing that order here is
      // what lets the trace say, for a face whose pixels moved, whether the
      // movement is a surface the old ceiling dropped (an expected newly
      // covered surface) or a surface that was already textured (a material
      // selection or sampling finding). Counting only materials that resolve
      // is deliberate: the old cap was checked after validation, so a missing
      // VMT did not consume a slot.
      {
        std::vector<std::string> oldOrder;
        oldOrder.reserve(worldMaterialChain.size());
        for (const auto& entry : worldMaterialChain) oldOrder.push_back(entry.first);
        std::sort(oldOrder.begin(), oldOrder.end(), [](const std::string& l, const std::string& r) {
          return l > r;
        });
        std::size_t accepted = 0;
        for (const auto& name : oldOrder) {
          auto found = worldMaterialChain.find(name);
          if (found == worldMaterialChain.end() || found->second.reason != "tiled") continue;
          found->second.oldOrderAccepted = ++accepted;
          found->second.beyondOldCap = accepted > 64;
        }
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
        auto preparedVmt = assetChain.lookup(preparedVmtPath).bytes;
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
  g_playback.tickRate = tf2::native::demoTickRate(demoHeader);
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
  // The placement itself (nameless drop, origin subtraction, scheduling) lives in
  // buildSoundTimeline so the probes that answer "what would the product
  // schedule" cannot re-implement it and disagree; see demo_header.h.
  const tf2::native::SoundTimelineBuildCounts soundBuild = tf2::native::buildSoundTimeline(
    demoNetworkSummary.decodedSoundEvents, soundOriginFound, soundOriginTick, g_playback.tickRate,
    soundTimeline);
  const std::size_t soundEventsRead = soundBuild.read;
  const std::size_t soundEventsUnresolved = soundBuild.unresolved;
  const std::size_t soundEventsUnplaceable = soundBuild.unplaceable;
  const std::size_t soundEventsRefused = soundBuild.refused;
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
      // The pre-T7 form of this condition also required `codec ==
      // SoundCodec::Wav`, and that test was how "this is audio the output path
      // can play" was spelled while the reader could only turn WAV bytes into
      // PCM. readSoundResource now hands back decoded PCM for MP3 as well, so
      // the same requirement is expressed as what it was always about: the
      // decoded buffer is playable at a rate the output path accepts. Keeping
      // the codec test here would have left this resolver refusing every event
      // the decode had just made playable -- the content map would read
      // `resolved` while the scheduler counted the same event as a missing
      // resource, and nothing would reach the sink.
      if (tf2::native::readSoundResource(*archive, name, resource)
          && resource.wav.valid && tf2::native::wavPcmIsPlayable(resource.wav)) {
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
  // T31: one line per MP3 event that reached a playback decision. Without it
  // the MP3 chain is only readable as totals, and an MP3 event is one
  // eventsSeen like any other -- a build that stopped decoding MP3 moves no
  // total at all. These lines are the per-event record the gate joins against
  // the content map (tick/index/path) and against the null-sink vs device runs
  // (outcome). Only MP3 is printed: printing every event would bury the record
  // under thousands of lines. The codec tested here is the one the resolver
  // actually produced (WavPcmData::codec), not a guess from the name.
  soundScheduler.setContentObserver([&](const tf2::native::SoundPlaybackEvent& event,
      const tf2::native::WavPcmData& wav, const char* outcomeName) {
    if (wav.codec != tf2::native::SoundCodec::Mp3) return;
    traceFmt("audio: mp3 tick=%d index=%u name=%s pcmBytes=%zu rate=%u channels=%u bits=%u"
      " seconds=%.3f outcome=%s",
      static_cast<int>(event.tick), static_cast<unsigned>(event.soundIndex), event.name.c_str(),
      static_cast<std::size_t>(wav.pcm.size()), static_cast<unsigned>(wav.sampleRate),
      static_cast<unsigned>(wav.channels), static_cast<unsigned>(wav.bitsPerSample),
      tf2::native::wavDurationSeconds(wav), outcomeName);
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
      + (refusedVpkCount ? (L", VPK refused: " + std::to_wstring(refusedVpkCount)) : std::wstring())
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
    : (commandTfRoot.empty()
        ? std::wstring(L"TF2 Demo Player - TF2 资源未找到")
        : std::wstring(L"TF2 Demo Player - TF2 资源未找到: ") + commandTfRoot.wstring());
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
  // T54: the scripted panel sequence runs once the renderer and the window are
  // fully alive -- the script pumps messages, and a WM_SIZE delivered to an
  // uninitialized renderer would be a resize the player never asked for. It
  // runs before the playback loop so the trace lines describe the panel that
  // the loop then measures.
  if (!g_logsPanelOptions.script.empty()) {
    traceFmt("%s", tf2::native::logsPanelRuntimeLine().c_str());
    tf2::native::logsPanelRunScript(window);
    traceFmt("%s", tf2::native::logsPanelSummaryLine().c_str());
  }
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

  // T2 (2026-10-14): the three advanced-material readings, for the slot the
  // picture actually uses. Kept as its own value rather than folded into the
  // struct above so the existing trace line's fields keep their meaning and the
  // new line is a strict superset -- a gate needle that matched the old line
  // still matches it.
  tf2::native::Renderer::ViewModelMaterialEffects viewModelEffectsReadings;
  std::int32_t viewModelEffectsSlot = -1;
  bool viewModelEffectsMaterialParsed = false;

  // The ranges the pass draws, from every valid LOD0 VTX descriptor. Each run
  // keeps its own material slot so multi-mesh arms do not collapse to mesh 0.
  //
  // `--viewmodel-flat-color` empties the key on purpose: it is the control the
  // pixel claim is measured against, and it is the same state a slot that failed
  // to resolve reaches, so the control and the fallback cannot diverge.
  const auto viewModelDrawRangesFor = [&](const tf2::native::ViewModelRequest& request,
      bool flatColor, std::int32_t slotOverride) {
    std::vector<tf2::native::ViewModelDrawRange> ranges;
    if (!request.resources.meshReady || request.mesh.vertices.size() < 3) return ranges;
    for (const auto& source : request.mesh.ranges) {
      tf2::native::ViewModelDrawRange range;
      range.firstVertex = source.firstVertex;
      range.vertexCount = source.vertexCount;
      const std::int32_t slot = slotOverride >= 0
        ? slotOverride : source.materialSlot;
      if (!flatColor && slot >= 0
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
    }
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
    const auto uploader = [&](const std::string& key, std::uint32_t width, std::uint32_t height,
        const std::vector<std::uint8_t>& rgba) {
      return renderer.uploadViewModelMaterialTexture(key, rgba, width, height);
    };
    auto summary = tf2::native::decodeViewModelMaterialSlots(request.materials,
      viewModelFlatColor ? -1 : -2, uploader);
    const std::int32_t uploadSlot = viewModelFlatColor ? -1 : -2;
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
    const auto ranges = viewModelDrawRangesFor(request, viewModelFlatColor,
      viewModelMaterialSlotOverride);
    // ---- T2 (2026-10-14): the advanced materials ---------------------------
    // Read for the slot the picture actually uses, re-parsed from the VMT the
    // slot resolved. The readers are pure and shared with the probe, so a
    // reading about this material and a fixture's reading about the same text
    // cannot disagree.
    //
    // The three switches force an effect off for the pixel claim: `phong-off`
    // and `phong-on` differ in exactly one input, which is what makes the frame
    // difference attributable to Phong rather than to the material changing.
    {
      tf2::native::Renderer::ViewModelMaterialEffects effects;
      effects.shaderReady = true;
      const std::int32_t effectSlot = ranges.empty() ? -1 : ranges.front().slot;
      const bool effectSlotValid = effectSlot >= 0
        && static_cast<std::size_t>(effectSlot) < request.materials.slots.size()
        && request.materials.slots[static_cast<std::size_t>(effectSlot)].pixelsDecoded;
      tf2::native::VmtMaterial effectMaterial;
      bool effectMaterialParsed = false;
      if (effectSlotValid) {
        const auto& slot = request.materials.slots[static_cast<std::size_t>(effectSlot)];
        const auto read = tf2::native::readViewModelResource(assets, slot.vmtPath);
        if (!read.bytes.empty()) {
          effectMaterialParsed = tf2::native::VmtParser::parse(
            std::string(read.bytes.begin(), read.bytes.end()), effectMaterial);
        }
      }
      if (effectMaterialParsed) {
        effects.phong = tf2::native::readViewModelPhong(effectMaterial);
        effects.bump = tf2::native::readViewModelBump(effectMaterial);
        effects.selfillum = tf2::native::readViewModelSelfillum(effectMaterial);
      }
      // The selfillum force, before the switches so an explicit `off` still
      // wins: a run that asked for selfillum off must not be forced back on.
      if (viewModelSelfillumForce && effectSlotValid) {
        effects.selfillum.declared = true;
        effects.selfillum.parsed = true;
        effects.selfillum.maskFromAlpha = true;
        effects.selfillum.tint[0] = 0.35f;
        effects.selfillum.tint[1] = 0.45f;
        effects.selfillum.tint[2] = 0.95f;
        effects.selfillum.reason += "; forced on by --viewmodel-selfillum-force"
          " (the albedo alpha masks)";
      }
      // The switches, applied after the readers so the reading still says what
      // the material asked for and the forced-off state is visibly the caller's
      // doing rather than a material that declares nothing.
      if (viewModelPhongGiven && !viewModelPhongRequested) {
        effects.phong.parsed = false;
        effects.phong.reason += "; forced off by --viewmodel-phong off";
      }
      if (viewModelBumpGiven && !viewModelBumpRequested) {
        effects.bump.parsed = false;
        effects.bump.reason += "; forced off by --viewmodel-bump off";
      }
      if (viewModelSelfillumGiven && !viewModelSelfillumRequested) {
        effects.selfillum.parsed = false;
        effects.selfillum.reason += "; forced off by --viewmodel-selfillum off";
      }
      // The normal map, decoded and uploaded through its own slot. `$bumpmap`
      // and `$normalmap` both name a VTF and both go here; the albedo cache is
      // keyed by the slot's own path, so this cannot overwrite it.
      if (effects.bump.parsed && !effects.bump.boundPath.empty() && !viewModelFlatColor) {
        const std::string bumpVtfPath =
          tf2::native::VmtParser::resourcePath(effects.bump.boundPath, ".vtf");
        const auto bumpRead = tf2::native::readViewModelResource(assets, bumpVtfPath);
        tf2::native::VtfTexture bumpTexture;
        std::vector<std::uint8_t> bumpRgba;
        if (!bumpRead.bytes.empty() && bumpTexture.parse(bumpRead.bytes)
            && !(bumpRgba = bumpTexture.decodeRgba(bumpRead.bytes)).empty()) {
          renderer.uploadViewModelBumpTexture(bumpRgba,
            bumpTexture.header().width, bumpTexture.header().height);
          effects.bump.uploaded = true;
        } else {
          // A named normal map that did not resolve is a fallback, not a
          // failure: the pass draws plain lighting and the reading names it.
          effects.bump.fallback = true;
          effects.bump.reason += "; the normal map did not decode";
        }
      }
      // The selfillum mask. A material with `$selfillum 1` and no mask uses the
      // albedo's alpha, which is Source's own default and is reported as such
      // rather than as a missing file.
      if (effects.selfillum.parsed && effects.selfillum.maskDeclared && !viewModelFlatColor) {
        const std::string maskVtfPath =
          tf2::native::VmtParser::resourcePath(effects.selfillum.maskPath, ".vtf");
        const auto maskRead = tf2::native::readViewModelResource(assets, maskVtfPath);
        tf2::native::VtfTexture maskTexture;
        std::vector<std::uint8_t> maskRgba;
        if (!maskRead.bytes.empty() && maskTexture.parse(maskRead.bytes)
            && !(maskRgba = maskTexture.decodeRgba(maskRead.bytes)).empty()) {
          renderer.uploadViewModelSelfillumMask(maskRgba,
            maskTexture.header().width, maskTexture.header().height);
          effects.selfillum.uploaded = true;
        } else {
          effects.selfillum.maskDeclared = false;
          effects.selfillum.maskFromAlpha = true;
          effects.selfillum.reason += "; the mask did not decode so the albedo alpha masks";
        }
      }
      renderer.setViewModelMaterialEffects(effects);
      viewModelEffectsReadings = effects;
      viewModelEffectsSlot = effectSlot;
      viewModelEffectsMaterialParsed = effectMaterialParsed;
    }
    const bool uploaded = renderer.uploadViewModelMesh(request.mesh.vertices, ranges);
    viewModelMeshBufferPath = uploaded ? path : std::string();
    // Per-range readings, not just the first one. `firstRange` answers "did the
    // first range bind a texture"; on a model with three ranges that is silent
    // about the other two, and the whole point of T13 is that the arms model has
    // more than one. The three lists are comma-separated in range order so a
    // reader can see WHICH range lost its material, not only that one did.
    std::string rangeSlots;
    std::string rangeVertices;
    std::string rangeTextured;
    for (std::size_t index = 0; index < ranges.size(); ++index) {
      if (index != 0) {
        rangeSlots += ",";
        rangeVertices += ",";
        rangeTextured += ",";
      }
      rangeSlots += std::to_string(ranges[index].slot);
      rangeVertices += std::to_string(ranges[index].vertexCount);
      rangeTextured += ranges[index].materialKey.empty() ? "0" : "1";
    }
    traceFmt("gpu: viewmodel ranges path=%s ranges=%zu firstRange=(slot %d, %u vertices, textured=%d translucent=%d alphatest=%d) uploaded=%d rangeSlots=%s rangeVertices=%s rangeTextured=%s",
      request.path.empty() ? "-" : request.path.c_str(), ranges.size(),
      ranges.empty() ? -1 : ranges.front().slot,
      ranges.empty() ? 0u : ranges.front().vertexCount,
      (!ranges.empty() && !ranges.front().materialKey.empty()) ? 1 : 0,
      (!ranges.empty() && ranges.front().translucent) ? 1 : 0,
      (!ranges.empty() && ranges.front().alphaTest) ? 1 : 0,
      uploaded ? 1 : 0,
      rangeSlots.empty() ? "-" : rangeSlots.c_str(),
      rangeVertices.empty() ? "-" : rangeVertices.c_str(),
      rangeTextured.empty() ? "-" : rangeTextured.c_str());
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
    // The body's range table, built once per distinct path with the material
    // table that was decoded on the same pass. It lives here rather than in the
    // renderer because it is a property of the model the entity named, and a
    // second weapon has its own slots.
    std::vector<tf2::native::ViewModelDrawRange> ranges;
  };
  ViewModelWeaponBodyCache viewModelWeaponBody;
  std::string viewModelWeaponMeshBufferPath;
  // Returns whether the body's mesh is in its slot; `uploaded` reports whether
  // THIS call is the one that put it there, so the reading can count uploads
  // rather than frames -- a per-frame upload is the defect the plan's "no
  // per-frame model or texture reload" line names, and a counter that
  // incremented on every no-op call would hide it.
  const auto ensureViewModelWeaponMesh = [&](const std::string& path,
      tf2::native::ViewModelRequest& request,
      const std::vector<tf2::native::ModelDrawVertex>& vertices, bool& uploaded) {
    uploaded = false;
    if (!path.empty() && viewModelWeaponMeshBufferPath == path) return true;
    // Every slot the body's ranges can name, not just `drawnSlot`.
    //   `c_pro_rifle.mdl` draws two LOD-0 descriptors -- the rifle on slot 0 and
    //   its scope on slot 1 -- and the old code decoded only the descriptor the
    //   mesh table pointed at, so the scope's own albedo was never uploaded and
    //   the whole body was painted with the rifle's. The arms' pass has decoded
    //   every slot since T2.5; this is the body reaching the same rule.
    const auto summary = tf2::native::decodeViewModelMaterialSlots(request.materials,
      viewModelWeaponFlatColor ? -1 : -2,
      [&](const std::string& key, std::uint32_t width, std::uint32_t height,
          const std::vector<std::uint8_t>& rgba) {
        return renderer.uploadViewModelMaterialTexture(key, rgba, width, height);
      });
    tf2::native::applyViewModelMaterialResult(request);
    viewModelWeaponStats.lastRuntimeFailure = summary.firstFailure;
    viewModelWeaponBody.ranges = viewModelDrawRangesFor(request, viewModelWeaponFlatColor,
      viewModelWeaponMaterialSlotOverride);
    // The reason behind the first range that will draw flat colour. Read off the
    // source range table, because a flat range's draw range carries no slot: the
    // builder leaves `slot` unset when it could not resolve one, and reading the
    // reason off the built range would then always answer "no slot" -- including
    // for the forced flat control, which is not a failure at all.
    viewModelWeaponStats.lastUnboundReason.clear();
    if (!viewModelWeaponFlatColor) {
      for (const auto& source : request.mesh.ranges) {
        // The slot the range WILL bind, not the one its descriptor names: under
        // the override a range whose natural slot resolves can still be bound to
        // a slot that does not, and the reason has to describe the draw.
        const auto slot = viewModelWeaponMaterialSlotOverride >= 0
          ? viewModelWeaponMaterialSlotOverride : source.materialSlot;
        const bool textured = slot >= 0
          && static_cast<std::size_t>(slot) < request.materials.slots.size()
          && request.materials.slots[static_cast<std::size_t>(slot)].pixelsDecoded
          && !request.materials.slots[static_cast<std::size_t>(slot)].vtfPath.empty();
        if (textured) continue;
        if (slot >= 0 && static_cast<std::size_t>(slot) < request.materials.slots.size()) {
          const auto& material = request.materials.slots[static_cast<std::size_t>(slot)];
          viewModelWeaponStats.lastUnboundReason = "slot " + std::to_string(slot) + ": "
            + (material.reason.empty() ? std::string("no reason recorded") : material.reason);
        } else {
          viewModelWeaponStats.lastUnboundReason =
            "the range names no material slot the model declares";
        }
        break;
      }
    }
    uploaded = renderer.uploadViewModelWeaponMesh(vertices, viewModelWeaponBody.ranges);
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
    traceFmt("gpu: reload-stage stage=texture lastError=0x%08lX", static_cast<unsigned long>(renderer.lastError()));
    bool uploadedWorld = displayMap.triangles.empty();
    if (!renderer.uploadWorldAuxTextures(mapBumpRgba, mapBumpWidth, mapBumpHeight, mapEnvRgba, mapEnvWidth, mapEnvHeight)) return false;
    traceFmt("gpu: reload-stage stage=aux lastError=0x%08lX", static_cast<unsigned long>(renderer.lastError()));
    if (!renderer.uploadWorldLightmap(displayMap.lightmapAtlas,
        static_cast<UINT>(displayMap.lightmapAtlasWidth), static_cast<UINT>(displayMap.lightmapAtlasHeight))) return false;
    traceFmt("gpu: reload-stage stage=lightmap lastError=0x%08lX", static_cast<unsigned long>(renderer.lastError()));
    // A cube that cannot be built is a capability this map does not get, not a
    // reason to lose the world: reloadGpuResources returning false ends the
    // program (main returns 14), and "this map's cubemap is malformed" has to
    // degrade to the 2D path with a reason attached, which is what the reading
    // is for. uploadWorldCubemap clears what it had before it validates.
    std::string cubemapUploadError;
    if (!renderer.uploadWorldCubemap(mapCubemapFaces, mapCubemapSize, &cubemapUploadError)) {
      mapCubemapReason = "upload refused: " + cubemapUploadError;
    }
    traceFmt("gpu: reload-stage stage=cubemap lastError=0x%08lX", static_cast<unsigned long>(renderer.lastError()));
    // T1 (2026-10-13): the skybox as geometry. The bytes come from
    // captureSkyFaces, which is the same resolve-and-decode walk measureSky
    // counts, so the sides the renderer uploads are the sides the probe reads.
    // The box is built by evaluateSkyDrawRequest -- a pure function with no GPU
    // and no file I/O, which is why the gate can assert the vertex count the
    // product drew.
    {
      // T12: a terrain-only frame has to contain the terrain and nothing else,
      // or the mask derived from it would include whatever else happened to be
      // drawn. The sky and the water are their own passes, so both are forced
      // off here rather than left to the caller's flags.
      const bool skyEnabled = (worldSkyGiven ? worldSkyRequested
        : persistent.render.skybox) && worldGeometry == WorldGeometryFilter::All;
      const auto skyCapture = tf2::native::captureSkyFaces(displayMap, readMaterialVtf);
      std::vector<tf2::native::WorldSkyVertex> skyVertices;
      tf2::native::WorldSkyDrawRequest skyRequest;
      skyRequest.enabled = skyEnabled;
      skyRequest.faces = skyCapture.faces;
      const auto skyDraw = tf2::native::evaluateSkyDrawRequest(skyRequest, skyVertices);
      std::string skyUploadError;
      const bool skyTextureUploaded = renderer.uploadWorldSky(skyCapture.faces, &skyUploadError);
      const bool skyGeometryUploaded = renderer.uploadWorldSkyGeometry(skyVertices);
    traceFmt("gpu: reload-stage stage=sky lastError=0x%08lX", static_cast<unsigned long>(renderer.lastError()));
      worldSkyReason = skyEnabled ? (skyUploadError.empty() ? skyDraw.reason : skyUploadError)
        : "skybox draw disabled by the caller";
      renderer.setWorldSkyEnabled(skyEnabled && skyTextureUploaded && skyGeometryUploaded, worldSkyReason);
      const auto skySample = renderer.worldMaterialSampleState();
      // Upload facts only: what the box is made of and why it is not drawn when
      // it is not. The draw count is deliberately absent -- this line is emitted
      // before the first frame, so a `draws=` here would be a zero that says
      // nothing about the renderer. The reading the gate asserts is the one
      // traceWorldDraw emits after the frame it describes.
      traceFmt("gpu: world sky-upload state=%s vertices=%u triangles=%u faces=%u side=%u"
        " resampled=%d reason=%s",
        tf2::native::worldSkyStateName(skySample.worldSkyState),
        skySample.worldSkyVertexCount, skySample.worldSkyTriangles, skySample.worldSkyFaces,
        skySample.worldSkySide, skySample.worldSkyResampled ? 1 : 0,
        worldSkyReason.empty() ? "-" : worldSkyReason.c_str());
      // ---- T8 (2026-10-16): which sky file is on which side of the box ----
      // Six lines, one per cube face, each naming the direction the geometry
      // uses, the suffix the naming convention gives that index, the .vmt and
      // .vtf that index resolved, and one sampled pixel. The pixel is the point:
      // the counts above cannot tell `up` from `dn` -- both decode, both upload,
      // both are 512x256 -- so a table with two entries swapped would pass every
      // assertion this file had. Sampled colour can, because the fixture puts a
      // different colour in each file and the gate then asks which index is
      // holding it.
      for (int skyFace = 0; skyFace < 6; ++skyFace) {
        const auto& origin = skyCapture.origins[static_cast<std::size_t>(skyFace)];
        traceFmt("gpu: world sky-face index=%d dir=%s suffix=%s vmt=%s vtf=%s rgba=%s",
          origin.face, origin.direction.c_str(), origin.suffix.c_str(),
          origin.materialPath.c_str(), origin.texturePath.c_str(), origin.sampleRgba.c_str());
      }
    }
    // T12: the two switches reach the renderer before the world is uploaded,
    // because both of them decide what the upload *contains* -- the second
    // layer's tiles and, for `--world-geometry terrain`, which triangles are
    // submitted at all. Setting them afterwards would change the draw without
    // changing the buffer, and the reading would describe the flag rather than
    // the picture.
    renderer.setWorldBlendEnabled(worldBlendGiven ? worldBlendRequested : true);
    renderer.setWorldTerrainOnly(worldGeometry == WorldGeometryFilter::Terrain);
    // T26: the sky mask is its own buffer and its face ids ride the vertex
    // colour, so both are upload-time decisions like the terrain filter.
    renderer.setWorldSkyFaceOnly(worldGeometry == WorldGeometryFilter::Sky);
    renderer.setWorldFaceIdUpload(worldDebugView == 4);
    renderer.setWorldDiagnosticIsolation(worldGeometry != WorldGeometryFilter::All);
    // T17: this one changes a draw-time constant, not the upload. It is set here
    // so the reading and the frames of one run describe one configuration.
    renderer.setWorldLightmapEnabled(worldLightmapGiven ? worldLightmapRequested : true);
    // T27: the three feature switches, for the same reason -- all three decide
    // what the upload contains (which feature atlases exist and which vertex
    // bits are set), so setting them after the upload would produce a frame that
    // describes the flag instead of the image.
    renderer.setWorldMaterialBumpEnabled(persistent.render.materialBump);
    renderer.setWorldBump2Enabled(persistent.render.bumpMap2 && persistent.render.materialBump);
    renderer.setWorldBump2Diagnostic(worldBump2Diagnostic);
    renderer.setWorldDetailEnabled(persistent.render.detailLayer);
    if (worldGeometry == WorldGeometryFilter::None) {
      // Nothing is uploaded, so no world vertex buffer exists and the world
      // draw's own guard skips it. That frame is the background measurement the
      // terrain mask is subtracted from; it is a control, not a feature.
      traceFmt("gpu: world geometry=none state=empty reason=--world-geometry none");
    } else if (worldGeometry == WorldGeometryFilter::Sky) {
      // T26: the sky brushes, and only them. The single-material overload is
      // used on purpose -- there is no atlas for sky materials, and the mask
      // pass is flat colour, so a texture would be a second thing to explain.
      uploadedWorld = renderer.uploadWorldGeometry(displayMap, std::string{}, 0, 0);
      traceFmt("gpu: world geometry=sky triangles=%zu faces=%zu state=%s",
        displayMap.skyTriangles.size(), displayMap.skyFaces, uploadedWorld ? "uploaded" : "refused");
    } else if (!uploadedWorld && !worldTextures.empty()) {
      uploadedWorld = renderer.uploadWorldGeometry(displayMap, worldTextures);
    }
    // The single-material overload is a fallback for an atlas that failed, not
    // for a filter that was asked for. Falling back here in `terrain` mode
    // would draw the whole world and call it the terrain mask.
    if (!uploadedWorld && worldGeometry == WorldGeometryFilter::All) {
      uploadedWorld = renderer.uploadWorldGeometry(displayMap, mapMaterialName, mapWidth, mapHeight);
    }
    traceFmt("gpu: world geometry=%s blend=%s",
      worldGeometry == WorldGeometryFilter::Terrain ? "terrain"
        : worldGeometry == WorldGeometryFilter::Sky ? "sky"
        : worldGeometry == WorldGeometryFilter::None ? "none" : "all",
      (worldBlendGiven ? worldBlendRequested : true) ? "on" : "off");
    // ---- T2 (2026-10-14): the water surface --------------------------------
    // The water triangles are taken out of the world buffer and uploaded as
    // their own draw. This is deliberately NOT a split of the world buffer: the
    // world pass still draws every triangle it was given, and the pvs clip's
    // `drawn + hidden == input` identity is untouched by this pass being on or
    // off. What the water pass adds is the surface -- its own texture, its own
    // warped uvs and its own switch -- over the flat shade the world pass draws.
    //
    // The material is the map's own, found by walking the map's water triangles
    // for the first material name they carry rather than by guessing a path:
    // TF2 names it in the BSP like any other face material.
    {
      const bool waterEnabled = (worldWaterGiven ? worldWaterRequested
        : persistent.render.waterSurface) && worldGeometry == WorldGeometryFilter::All;
      // Which material the water faces carry, counted while the buffer is built
      // so the count is of the triangles actually uploaded.
      std::string waterMaterialName;
      std::size_t waterFacesInMap = 0;
      for (const auto& triangle : displayMap.triangles) {
        if (!triangle.water) continue;
        ++waterFacesInMap;
        if (waterMaterialName.empty() && !triangle.material.empty()) {
          waterMaterialName = triangle.material;
        }
      }
      // The material's own parameters, read through the same parser the rest of
      // the material chain uses, so a reading about the water material cannot
      // disagree with a reading about the map's other materials.
      std::string waterVmtText;
      tf2::native::WorldWaterParams waterParams;
      if (!waterMaterialName.empty()) {
        const std::string waterVmtPath = tf2::native::VmtParser::resourcePath(waterMaterialName, ".vmt");
        std::vector<std::uint8_t> waterVmtBytes = assetChain.lookup(waterVmtPath).bytes;
        if (waterVmtBytes.empty()) waterVmtBytes = readMaterialVtf(waterVmtPath);
        waterVmtText.assign(waterVmtBytes.begin(), waterVmtBytes.end());
        waterParams = tf2::native::measureWaterParams(waterVmtText, waterVmtPath);
      } else {
        waterParams.reason = "the map's water triangles name no material";
      }
      // The texture, if the material named one that resolved. `$bumpmap` and
      // `$normalmap` are the two keys TF2's water shaders use; a plain water
      // material with neither is a real material and falls back to the flat
      // shade rather than being reported as a failure.
      std::vector<std::uint8_t> waterRgba;
      UINT waterWidth = 0, waterHeight = 0;
      const std::string& waterTextureName = !waterParams.normalMap.empty()
        ? waterParams.normalMap : waterParams.bumpMap;
      if (!waterTextureName.empty()) {
        const std::string waterVtfPath = tf2::native::VmtParser::resourcePath(waterTextureName, ".vtf");
        const auto& waterVtf = readMaterialVtf(waterVtfPath);
        tf2::native::VtfTexture waterTexture;
        if (!waterVtf.empty() && waterTexture.parse(waterVtf)) {
          waterRgba = waterTexture.decodeRgba(waterVtf);
          if (!waterRgba.empty()) {
            waterWidth = waterTexture.header().width;
            waterHeight = waterTexture.header().height;
          }
        }
      }
      const bool waterTextureResolved = !waterRgba.empty() && waterWidth > 0 && waterHeight > 0;
      // The vertices, in the same converted space uploadWorldGeometry produces,
      // with the same per-triangle shade so the fallback is visibly water-shaped.
      //
      // The build is guarded on worldBoundsValid() rather than on the transform
      // values: the initial transform is centre 0 / scale 1, which is a
      // legitimate transform, so a water pass that read it before the world
      // upload would place 36 triangles in raw map coordinates, draw them
      // somewhere off screen and report draws=1 with no pixel changed. That is
      // the exact defect this guard exists to make unreachable.
      std::vector<tf2::native::WorldWaterVertex> waterVertices;
      if (waterEnabled && renderer.worldBoundsValid()) {
        const float waterShade = 0.55f;
        for (const auto& triangle : displayMap.triangles) {
          if (!triangle.water) continue;
          // The same centring and scaling the world pass applies, read back from
          // the renderer that applied it, so the water surface lands on the water
          // the world pass already drew. A second transform computed here would
          // draw water in a different place from water.
          const auto toWorld = [&](const tf2::native::BspVertex& point) {
            tf2::native::WorldWaterVertex vertex{};
            vertex.x = (point.x - renderer.worldCenterX()) * renderer.worldHorizontalScale();
            vertex.y = (point.y - renderer.worldCenterY()) * renderer.worldHorizontalScale();
            vertex.z = (point.z - renderer.worldMinZ()) / renderer.worldSpanZ() * 0.8f + 0.1f;
            vertex.r = waterShade; vertex.g = waterShade; vertex.b = 0.72f; vertex.a = 1.0f;
            return vertex;
          };
          const auto ab = tf2::native::BspVertex{triangle.b.x - triangle.a.x, triangle.b.y - triangle.a.y, triangle.b.z - triangle.a.z};
          const auto ac = tf2::native::BspVertex{triangle.c.x - triangle.a.x, triangle.c.y - triangle.a.y, triangle.c.z - triangle.a.z};
          const auto nx = ab.y * ac.z - ab.z * ac.y;
          const auto ny = ab.z * ac.x - ab.x * ac.z;
          const auto nz = ab.x * ac.y - ab.y * ac.x;
          const auto normalLength = std::sqrt(nx * nx + ny * ny + nz * nz);
          const float invNormal = normalLength > 1e-6f ? 1.0f / normalLength : 0.0f;
          auto a = toWorld(triangle.a), b = toWorld(triangle.b), c = toWorld(triangle.c);
          a.nx = static_cast<float>(nx * invNormal); a.ny = static_cast<float>(ny * invNormal); a.nz = static_cast<float>(nz * invNormal);
          b.nx = a.nx; b.ny = a.ny; b.nz = a.nz;
          c.nx = a.nx; c.ny = a.ny; c.nz = a.nz;
          // Normalised into [0,1] by the texture's own dimensions, so the warp's
          // amplitude means the same thing whatever the texture's side is.
          const float invWidth = waterWidth > 0 ? 1.0f / static_cast<float>(waterWidth) : 0.0f;
          const float invHeight = waterHeight > 0 ? 1.0f / static_cast<float>(waterHeight) : 0.0f;
          a.u = triangle.au * invWidth; a.v = triangle.av * invHeight;
          b.u = triangle.bu * invWidth; b.v = triangle.bv * invHeight;
          c.u = triangle.cu * invWidth; c.v = triangle.cv * invHeight;
          waterVertices.push_back(a); waterVertices.push_back(b); waterVertices.push_back(c);
        }
      }
      tf2::native::WorldWaterDrawRequest waterRequest;
      waterRequest.enabled = waterEnabled;
      waterRequest.vertices = waterVertices;
      waterRequest.textureResolved = waterTextureResolved;
      waterRequest.params = waterParams;
      // The amplitude is the material's own when it declared one and the engine
      // default otherwise, and it is stated rather than hard-coded so a fixture
      // can move it.
      waterRequest.warpAmplitude = waterEnabled ? waterVertexWarpAmplitude : 0.0f;
      // The phase comes from the playback tick. A paused tick is a fixed phase,
      // which is what makes the repeat frame byte-identical rather than the same
      // wave at a different time.
      waterRequest.warpPhase = static_cast<double>(waterWarpTick) * (1.0 / 66.0);
      std::vector<tf2::native::WorldWaterVertex> warpedVertices;
      const auto waterReading = tf2::native::evaluateWaterDrawRequest(waterRequest, warpedVertices);
      const bool waterGeometryUploaded = renderer.uploadWorldWaterGeometry(warpedVertices);
    traceFmt("gpu: reload-stage stage=water lastError=0x%08lX", static_cast<unsigned long>(renderer.lastError()));
      const bool waterTextureUploaded = renderer.uploadWorldWaterTexture(waterRgba, waterWidth, waterHeight);
      renderer.setWorldWaterWarp(waterRequest.warpPhase, waterRequest.warpAmplitude);
      renderer.setWorldWaterEnabled(waterEnabled && waterGeometryUploaded);
      // The judge's verdict, handed to the renderer rather than recomputed
      // there: the state is a judgement about the request and the draw count is
      // a fact about the pass, and the two have to be able to move separately
      // for the mutation that deletes the Draw call to be visible.
      renderer.setWorldWaterReading(waterReading.state, waterReading.waterReflectionState,
        waterReading.waterRefractionState, waterReading.reason);
      waterSurfaceReason = waterReading.reason;
      waterFacesInMap_ = waterFacesInMap;
      waterMaterialResolved_ = waterTextureResolved;
      waterMaterialParams_ = waterParams;
      // UPLOAD FACTS ONLY, and the name says so. `draws` is deliberately
      // absent: this line is emitted before the first frame, so a draw count
      // here would be the number the request WOULD produce -- a reading that
      // describes its own copy of the guard rather than the pass. Measured:/n      // the first version of this line carried `draws=` from the request and a
      // mutation that deleted the Draw call still read draws=1, so the gate
      // could not have caught it. The reading the gate asserts is the one
      // traceWorldDraw emits after the frame it describes.
      traceFmt("gpu: world water-upload state=%s faces=%zu triangles=%zu"
        " warpPlanned=%d textureBound=%d fallback=%d reflection=%s refraction=%s reason=%s",
        tf2::native::worldWaterSurfaceStateName(waterReading.state),
        waterReading.waterFaces, waterReading.waterTriangles,
        waterReading.waterWarpApplied ? 1 : 0,
        waterTextureUploaded ? 1 : 0, waterReading.waterFallback ? 1 : 0,
        tf2::native::worldWaterSurfaceStateName(waterReading.waterReflectionState),
        tf2::native::worldWaterSurfaceStateName(waterReading.waterRefractionState),
        waterSurfaceReason.empty() ? "-" : waterSurfaceReason.c_str());
    }
    // The lightmap pass, read back from the renderer rather than assumed from
    // the parse. The window title already carries the BSP-side numbers; this is
    // the GPU-side pair (vertices carrying a per-face light uv / vertices
    // uploaded), which is what distinguishes "the atlas is bound" from "the map
    // is lit". It goes to the trace channel because the title is already at
    // capacity and a reading nobody prints is a reading nobody checks.
    {
      const auto worldSample = renderer.worldMaterialSampleState();
      const char* lightmapStatusName = "unknown";
      switch (worldSample.lightmapStatus) {
        case tf2::native::WorldLightmapStatus::Unavailable: lightmapStatusName = "unavailable"; break;
        case tf2::native::WorldLightmapStatus::FallbackUnlit: lightmapStatusName = "fallback-unlit"; break;
        case tf2::native::WorldLightmapStatus::Active: lightmapStatusName = "active"; break;
      }
      traceFmt("gpu: world lightmap status=%s intensity=%.3f atlas=%d bump=%d env=%d vertices=%u lightmapped=%u bounds=(%.2f,%.2f,%.2f)..(%.2f,%.2f,%.2f) lightmapEnabled=%d",
        lightmapStatusName, worldSample.lightmapIntensity,
        worldSample.lightmapAtlasBound ? 1 : 0, worldSample.bumpTextureBound ? 1 : 0,
        worldSample.envTextureBound ? 1 : 0, worldSample.worldVertexCount,
        worldSample.lightmappedVertexCount,
        worldSample.convertedMin[0], worldSample.convertedMin[1], worldSample.convertedMin[2],
        worldSample.convertedMax[0], worldSample.convertedMax[1], worldSample.convertedMax[2],
        worldLightmapGiven ? (worldLightmapRequested ? 1 : 0) : 1);
    // The env pass reads back as a mode rather than as a bound flag: approximate-2d
    // with a texture bound and six-face with a cube bound are the two states the
    // pixel shader branches on, and 'bound' alone cannot tell them apart.
    traceFmt("gpu: world geometry displacedTriangles=%u waterTriangles=%u",
      worldSample.displacedTriangles, worldSample.waterTriangles);
    {
      // T12: what the blend path put on the GPU. `tiles` is materials with a
      // second layer in the atlas; `weighted` is uploaded vertices that carry a
      // real weight. A tile with no weighted vertex would mean the material
      // reached the atlas and no geometry asked for it, which is the failure a
      // count of materials alone cannot see.
      const auto blendSample = renderer.worldBlendState();
      traceFmt("gpu: world blend enabled=%d terrainOnly=%d textures=%zu layers=%zu accepted=%zu acceptedLayers=%zu tiles=%zu modulateTiles=%zu malformedLayers=%zu weightedVertices=%zu modulatedVertices=%zu weightMin=%.3f weightMax=%.3f distinctWeights=%zu",
        blendSample.enabled ? 1 : 0, blendSample.terrainOnly ? 1 : 0,
        blendSample.texturesSeen, blendSample.layersSeen, blendSample.accepted, blendSample.acceptedLayers,
        blendSample.tiles, blendSample.modulateTiles, blendSample.malformedLayers,
        blendSample.weightedVertices, blendSample.modulatedVertices,
        static_cast<double>(blendSample.weightMin), static_cast<double>(blendSample.weightMax),
        blendSample.distinctWeights);
      // The atlas's own identity. A refused atlas is the difference between
      // "the terrain material resolved" and "the terrain material is what the
      // screen shows", and before this line the two read the same.
      traceFmt("gpu: world atlas size=%zu bytes=%zu atlases=%zu totalBytes=%zu state=%s",
        renderer.worldAtlasSize(), static_cast<std::size_t>(renderer.worldAtlasBytes()),
        renderer.worldAtlasCount(), static_cast<std::size_t>(renderer.worldAtlasTotalBytes()),
        renderer.worldAtlasReason().c_str());
    }
    {
      // T1 (2026-10-13): the pvs clip no longer measures the price of not
      // clipping -- it clips. The decision is made by clipWorldTrianglesByVis,
      // the same pure function the probe asserts, and the renderer is handed the
      // surviving triangle indices, so the triangles the pvs hid leave the
      // rasteriser as well as the reading. The trace line is emitted from the
      // frame loop instead, once the first clip has run, because a reading about
      // a draw has to be taken from the frame that drew.
      float eyeX = 0.0f, eyeY = 0.0f, eyeZ = 0.0f;
      // T10 fix: the MAP-space eye. observerFocusWorld() hands back the
      // normalized unit-box point the renderer cameras against; clusterAt
      // walks leaves in map units. Feeding it the normalized point pinned the
      // pvs to whatever leaf covers (±0.4, ±0.9, 0.1..0.9) in map coordinates
      // -- cluster 2017 for every camera position this task measured -- and
      // the clip hid an arbitrary half of the map from a place nobody was
      // looking at.
      if (!renderer.observerFocusMapWorld(eyeX, eyeY, eyeZ)) {
        // No observer yet: the first triangle's own vertex is a point the map
        // certainly contains, and clusterAt answers -1 for a point in no leaf,
        // which the line then reports rather than hides.
        eyeX = displayMap.triangles.empty() ? 0.0f : displayMap.triangles.front().a.x;
        eyeY = displayMap.triangles.empty() ? 0.0f : displayMap.triangles.front().a.y;
        eyeZ = displayMap.triangles.empty() ? 0.0f : displayMap.triangles.front().a.z;
      }
      worldVisCameraEye[0] = eyeX; worldVisCameraEye[1] = eyeY; worldVisCameraEye[2] = eyeZ;
    }
    traceFmt("gpu: world cubemap status=%s mode=%u preset=%s declared=%d faces=%u size=%u source=%s reason=%s",
      tf2::native::worldCubemapStatusName(worldSample.cubemapStatus), worldSample.envMode,
      renderer.settings().preset == tf2::native::QualityPreset::Standard ? "standard" : "performance",
      renderer.worldMaterialParams().envMap ? 1 : 0,
      worldSample.cubemapFaces, worldSample.cubemapSize,
      mapCubemapSource.empty() ? "-" : mapCubemapSource.c_str(),
      mapCubemapReason.c_str());
    }
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
    traceFmt("gpu: reload-stage stage=models lastError=0x%08lX", static_cast<unsigned long>(renderer.lastError()));
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
    // T12: `--world-geometry none` uploads no world on purpose, so its absence
    // is not a reload failure. Every other mode still has to produce a world
    // buffer, which is the invariant this line has always carried.
    const bool worldRequired = worldGeometry != WorldGeometryFilter::None;
    return uploadedTexture && (uploadedWorld || !worldRequired) && uploadedModel && uploadedBones;
  };
  if (!reloadGpuResources()) {
    renderer.shutdown();
    g_renderer = nullptr;
    DestroyWindow(window);
    return 14;
  }
  // T8 (2026-10-16) diagnostic: the device-error policy at the bottom of the
  // loop reads `lastError()` without asking what set it. A load-time refusal that
  // the product deliberately degraded (a cubemap or texture it could not upload)
  // therefore still reaches the policy and ends the program with code 3, which
  // reads as "the device failed" when nothing has drawn yet. Print the error
  // here -- after the load, before any draw -- so "already set when the loop
  // began" and "returned by Present" are two different readings.
  traceFmt("gpu: post-reload lastError=0x%08lX",
    static_cast<unsigned long>(renderer.lastError()));
  renderer.clearLastError();
  // ---- T1 (2026-10-13): the per-frame pvs clip ----------------------------
  // The clip has to follow the camera, not the load: a clip decided once at
  // upload would hide triangles for the whole run and the map would lose
  // whatever the observer walked towards. So the decision is re-made whenever
  // the camera's cluster changes, and the frame that draws is the frame that
  // decided -- the reading below is emitted from there for the same reason.
  std::vector<tf2::native::BspTriangle> worldVisKept;
  std::vector<std::size_t> worldVisKeptIndices;
  // A cluster-only projection of map.triangles, index-aligned with it. The clip
  // reads nothing but `cluster`, and handing it the real triangles would copy
  // 200k materials (and their strings) every time the observer crossed a leaf.
  // Built once, and asserted below to be the same length as the geometry it
  // stands for.
  std::vector<tf2::native::BspTriangle> worldVisProbe;
  worldVisProbe.resize(displayMap.triangles.size());
  for (std::size_t index = 0; index < displayMap.triangles.size(); ++index) {
    worldVisProbe[index].cluster = displayMap.triangles[index].cluster;
  }
  int worldVisAppliedCluster = -2;
  bool worldDrawTraceEmitted = false;
  tf2::native::WorldVisClipReading worldVisReading;
  const auto applyWorldVisClip = [&]() {
    const bool enabled = worldVisClipGiven ? worldVisClipRequested : true;
    renderer.setWorldVisClipEnabled(enabled);
    if (!enabled) {
      worldVisReading = tf2::native::WorldVisClipReading{};
      worldVisReading.state = tf2::native::WorldVisClipState::Disabled;
      worldVisReading.reason = "vis clip disabled by the caller";
      return;
    }
    float eyeX = worldVisCameraEye[0], eyeY = worldVisCameraEye[1], eyeZ = worldVisCameraEye[2];
    // T10 fix: map-space eye for a map-space cluster query (see the fill above).
    if (renderer.observerFocusMapWorld(eyeX, eyeY, eyeZ)) {
      worldVisCameraEye[0] = eyeX; worldVisCameraEye[1] = eyeY; worldVisCameraEye[2] = eyeZ;
    }
    const int cameraCluster = tf2::native::BspParser::clusterAt(displayMap, eyeX, eyeY, eyeZ);
    if (cameraCluster == worldVisAppliedCluster) return;
    worldVisAppliedCluster = cameraCluster;
    tf2::native::WorldVisClipRequest request;
    request.enabled = true;
    request.cameraCluster = cameraCluster;
    worldVisKeptIndices.clear();
    worldVisReading = tf2::native::clipWorldTrianglesByVis(displayMap, worldVisProbe,
      request, worldVisKept, &worldVisKeptIndices);
    tf2::native::WorldVisDrawList list;
    list.state = worldVisReading.state;
    list.applied = worldVisReading.clipApplied;
    list.inputTriangles = worldVisReading.inputTriangles;
    list.outputTriangles = worldVisReading.outputTriangles;
    list.hiddenTriangles = worldVisReading.hiddenTriangles;
    list.trianglesWithoutCluster = worldVisReading.trianglesWithoutCluster;
    list.cameraCluster = worldVisReading.cameraCluster;
    list.reason = worldVisReading.reason;
    // Three indices per surviving triangle: the renderer's vertex buffer is
    // three vertices per triangle in map.triangles order, so index 3i+j is
    // triangle i's j-th vertex.
    list.indices.reserve(worldVisKeptIndices.size() * 3u);
    for (const std::size_t triangle : worldVisKeptIndices) {
      const auto base = static_cast<std::uint32_t>(triangle * 3u);
      list.indices.push_back(base);
      list.indices.push_back(base + 1u);
      list.indices.push_back(base + 2u);
    }
    renderer.setWorldVisDrawList(list);
  };
  // Both of the readings below are taken from flags the draw calls set inside
  // themselves, so the only honest moment to read them is after a frame has
  // actually been drawn. Emitted once: the first frame is the one the passes
  // were configured for, and a per-frame line would bury it.
  const auto traceWorldDraw = [&]() {
    if (worldDrawTraceEmitted) return;
    worldDrawTraceEmitted = true;
    const auto sample = renderer.worldMaterialSampleState();
    traceFmt("gpu: world sky state=%s vertices=%u triangles=%u faces=%u draws=%u side=%u"
      " resampled=%d reason=%s",
      tf2::native::worldSkyStateName(sample.worldSkyState),
      sample.worldSkyVertexCount, sample.worldSkyTriangles, sample.worldSkyFaces,
      sample.worldSkyDraws, sample.worldSkySide, sample.worldSkyResampled ? 1 : 0,
      worldSkyReason.empty() ? "-" : worldSkyReason.c_str());
    // T29: the world projection's clip planes, in the box unit the matrix takes
    // AND in the map units they mean. uploadWorldGeometry scales the map into a
    // unit box (worldHorizontalScale_ map units per box unit), so a near plane
    // written in box units is a clip distance in MAP units: the shipped 0.05
    // was 611 map units on this map and the room the camera stands in was
    // clipped out of every frame. Read back from the renderer after the draw
    // (this lambda runs once, on the first drawn frame -- the same discipline
    // the sky and water lines here use), so a gate pins what the matrix was
    // built from instead of the constant someone intended.
    {
      const float nearBox = renderer.worldNearPlaneNormalized();
      const float farBox = renderer.worldFarPlaneNormalized();
      const float scale = renderer.worldHorizontalScale();
      const bool haveTransform = std::isfinite(scale) && scale > 0.0f;
      traceFmt("gpu: world clip near=%.8f far=%.6f nearMapUnits=%.2f farMapUnits=%.0f horizontalScale=%.8f state=%s",
        nearBox, farBox,
        haveTransform ? nearBox / scale : -1.0f,
        haveTransform ? farBox / scale : -1.0f,
        scale, haveTransform ? "published" : "no-transform");
    }
    // T2 (2026-10-14): the water surface as a DRAW, read from the renderer
    // after the frame -- the same discipline the sky and vis lines above use.
    // `draws` is set inside the pass itself, so a mutation that deletes the
    // Draw call moves it; the upload line cannot, because it runs first.
    {
      const auto water = renderer.waterSurfaceSampleState();
      traceFmt("gpu: world water state=%s faces=%u triangles=%u draws=%u visible=%d"
        " warp=%d textureBound=%d fallback=%d reflection=%s refraction=%s reason=%s",
        tf2::native::worldWaterSurfaceStateName(water.state),
        water.waterFaces, water.waterTriangles, water.waterDrawCalls,
        water.waterVisible ? 1 : 0, water.waterWarpApplied ? 1 : 0,
        water.waterTextureBound ? 1 : 0, water.waterFallback ? 1 : 0,
        tf2::native::worldWaterSurfaceStateName(water.reflectionState),
        tf2::native::worldWaterSurfaceStateName(water.refractionState),
        water.reason.empty() ? "-" : water.reason.c_str());
    }
    const int cameraCluster = sample.worldVisCameraCluster;
    traceFmt("gpu: world vis cameraCluster=%d visibleFromCamera=%d clusters=%d"
      " clusteredTriangles=%zu hiddenTriangles=%zu withoutCluster=%zu"
      " inputTriangles=%zu drawnTriangles=%zu clipApplied=%d state=%s"
      " eyeMap=(%.1f,%.1f,%.1f) reason=%s",
      cameraCluster,
      cameraCluster >= 0
        ? tf2::native::BspParser::countVisibleClusters(displayMap, cameraCluster) : -1,
      displayMap.visClusterCount,
      sample.worldVisInputTriangles - sample.worldVisTrianglesWithoutCluster,
      sample.worldVisHiddenTriangles, sample.worldVisTrianglesWithoutCluster,
      sample.worldVisInputTriangles, sample.worldVisDrawnTriangles,
      sample.worldVisClipApplied ? 1 : 0,
      tf2::native::worldVisClipStateName(sample.worldVisClipState),
      worldVisCameraEye[0], worldVisCameraEye[1], worldVisCameraEye[2],
      sample.worldVisClipReason.empty() ? "-" : sample.worldVisClipReason.c_str());
    traceFmt("gpu: world cull backfaces=%d mode=%s",
      sample.worldCullBackfaces ? 1 : 0,
      sample.worldCullBackfaces ? "cull-back" : "cull-none");
    traceFmt("gpu: world debug view=%s",
      sample.worldDebugView == 4 ? "face-id"
        : sample.worldDebugView == 3 ? "albedo"
        : sample.worldDebugView == 2 ? "depth" : sample.worldDebugView == 1 ? "flat" : "textured");
    // ---- T19: the atlas plan, and the per-material account behind it --------
    // The old reading was one number (`capped=51`) with no names. This is the
    // table that number was hiding: what each material asked for, how much of it
    // the camera's pvs actually reaches, and what the plan did with it. `visible`
    // is computed from the same kept-index list the draw uses, so "this
    // material affects the reference frame" is a count of triangles that
    // survived the clip and not an estimate.
    traceFmt("gpu: world atlas-plan batches=%zu blendBatches=%zu tiled=%zu refused=%zu"
      " malformed=%zu duplicate=%zu budget=%zu layerBytes=%llu reason=%s",
      sample.worldAtlasBatches, sample.worldAtlasBlendBatches, sample.worldTilesTiled,
      sample.worldTilesRefused, sample.worldTilesRefusedMalformed,
      sample.worldTilesRefusedDuplicate, sample.worldTilesRefusedBudget,
      static_cast<unsigned long long>(sample.worldAtlasLayerBytes),
      sample.worldAtlasPlanReason.empty() ? "-" : sample.worldAtlasPlanReason.c_str());
    {
      std::unordered_map<std::string, std::size_t> trianglesByMaterial;
      for (const auto& triangle : displayMap.triangles) ++trianglesByMaterial[triangle.material];
      std::unordered_map<std::string, std::size_t> visibleByMaterial;
      for (const std::size_t triangle : worldVisKeptIndices) {
        if (triangle < displayMap.triangles.size()) {
          ++visibleByMaterial[displayMap.triangles[triangle].material];
        }
      }
      for (const auto& row : sample.worldTileTable) {
        traceFmt("gpu: world tile material=%s tris=%zu visible=%zu batch=%d tile=%d"
          " size=%dx%d blend=%d modulate=%d reason=%s",
          row.material.empty() ? "-" : row.material.c_str(),
          trianglesByMaterial[row.material], visibleByMaterial[row.material],
          row.batch, row.tile, row.width, row.height, row.blend ? 1 : 0, row.modulate ? 1 : 0,
          row.reason.c_str());
      }
    }
    // The two cross-pass questions, read from the ledger the draws fill. A
    // zero here is the statement the task asks for; the mutation makes one of
    // them non-zero so the zero can be trusted.
    traceFmt("gpu: world isolation weaponDrawsWithWorldAtlas=%zu worldDrawsWithWeaponTexture=%zu"
      " slot0Binds=%zu batchDraws=%zu texturedBatchDraws=%zu",
      sample.weaponDrawsWithWorldAtlas, sample.worldDrawsWithWeaponTexture,
      sample.slot0Binds, sample.worldBatchDrawCalls, sample.worldBatchTexturedDrawCalls);
    // ---- T19: what the materials ask the shader for ------------------------
    // Declared, visible and resolved per feature, with the refusal each one
    // gets. The scan is shared with the probe (world_material_features.cpp), so
    // the gate's numbers and the product's numbers come from one rule.
    {
      std::unordered_map<std::string, std::size_t> visibleByMaterial;
      for (const std::size_t triangle : worldVisKeptIndices) {
        if (triangle < displayMap.triangles.size()) {
          ++visibleByMaterial[displayMap.triangles[triangle].material];
        }
      }
      std::unordered_map<std::string, std::size_t> trianglesByMaterial;
      for (const auto& triangle : displayMap.triangles) ++trianglesByMaterial[triangle.material];
      std::vector<tf2::native::WorldMaterialFeatureInput> inputs;
      inputs.reserve(worldFetchedMaterials.size());
      for (const auto& entry : worldFetchedMaterials) {
        tf2::native::WorldMaterialFeatureInput input;
        input.material = entry.first;
        input.vmt = &entry.second;
        input.triangles = trianglesByMaterial[entry.first];
        input.visibleTriangles = visibleByMaterial[entry.first];
        inputs.push_back(std::move(input));
      }
      tf2::native::WorldFeatureResolvers resolvers;
      // T24: the scan's resource lookup must search the same stores the world
      // material fetch does. It used to read only the AssetStoreChain (loose +
      // global VPKs), while pl_odyssey ships 9 of its 44 `$bumpmap` VTFs in the
      // map's own pakfile -- so the ledger reported `missing=9` for textures
      // that are present, and the refusal sentence blamed absence instead of
      // the one-slot limitation. Measured with
      // `lighting_semantics_probe --list-bump`: chainMissing=9, pakfileOnly=9,
      // absent=0.
      const auto readFeatureResource = [&](const std::string& path) {
        std::vector<std::uint8_t> bytes = assetChain.lookup(path).bytes;
        if (bytes.empty() && mapPakfileBytes && !mapPakfileBytes->empty()) {
          std::vector<std::uint8_t> packed;
          if (tf2::native::bspPakfileEntry(*mapPakfileBytes, path, packed, nullptr)) {
            bytes = std::move(packed);
          }
        }
        return bytes;
      };
      resolvers.resourceExists = [&](const std::string& path) {
        return !readFeatureResource(path).empty();
      };
      resolvers.readResource = [&](const std::string& path) {
        return readFeatureResource(path);
      };
      const auto features = tf2::native::scanWorldMaterialFeatures(inputs, resolvers);
      traceFmt("gpu: world features %s", features.summary.c_str());
      const auto emitFeature = [](const char* name, const tf2::native::WorldMaterialFeatureCount& count) {
        if (count.declared == 0) {
          traceFmt("gpu: world feature feature=%s declared=0 visible=0 resolved=0 missing=0"
            " first=- resource=- reason=-", name);
          return;
        }
        traceFmt("gpu: world feature feature=%s declared=%zu visible=%zu resolved=%zu missing=%zu"
          " first=%s resource=%s reason=%s", name, count.declared, count.visibleDeclared,
          count.resourcesResolved, count.resourcesMissing,
          count.firstMaterial.empty() ? "-" : count.firstMaterial.c_str(),
          count.firstResource.empty() ? "-" : count.firstResource.c_str(),
          count.reason.empty() ? "-" : count.reason.c_str());
      };
      emitFeature("bumpmap", features.bumpMap);
      emitFeature("bumpmap2", features.bumpMap2);
      emitFeature("ssbump", features.ssBump);
      emitFeature("detail", features.detail);
      traceFmt("gpu: world feature-detail distinctBumpPaths=%zu detailTypesRead=%zu firstDetailType=%s",
        features.distinctBumpPaths, features.detailTypesRead,
        features.firstDetailType.empty() ? "-" : features.firstDetailType.c_str());
      // ---- T24: the lighting controls, one row each -----------------------
      // The four feature rows above say what the materials ask for. These six
      // say what the pass does about it, including the three pass switches that
      // have no VMT key (dynamicLighting, specular, bump) and the two values
      // that decide whether a switch reached a constant: `requested` is the
      // settings field, `effective` is the value the shader was given.
      tf2::native::WorldLightingPassState passState;
      // The sample is re-read here rather than carried from the draw block:
      // `worldMaterialSampleState()` reports the constants the last draw
      // uploaded, and this block is outside that block's scope. Reading it at
      // the point of use is what keeps the trace's `effective` from being a
      // copy of an earlier frame.
      const auto lightingSample = renderer.worldMaterialSampleState();
      passState.dynamicLightingRequested = renderer.settings().dynamicLighting;
      passState.dynamicLightingEffective = lightingSample.dynamicLightingMode;
      passState.specularRequested = renderer.settings().specular;
      passState.specularEffective = lightingSample.specularMode;
      passState.bumpRequested = renderer.settings().bumpMapping;
      passState.bumpEffective = lightingSample.bumpMode;
      passState.bumpOwnerDeclared = mapBumpMapDeclared;
      passState.bumpOwnerMaterial = mapMaterialName;
      passState.bumpOwnerTexture = mapBumpMapPath;
      passState.bumpTextureSource = mapBumpMapPath.empty() ? "none"
        : (mapBumpTextureForced ? "forced" : "material");
      // T27: the per-material feature path. `effective` comes from the renderer's
      // post-upload state (atlas present AND a vertex bit set), never from the
      // settings field -- that is the whole point of the split.
      passState.materialBumpRequested = renderer.settings().materialBump;
      passState.materialBumpEffective = lightingSample.materialBumpMode;
      passState.materialBumpMaterials = lightingSample.bumpFeature.materials;
      passState.bump2Requested = renderer.settings().bumpMap2;
      passState.bump2Effective = lightingSample.bump2Mode;
      passState.bump2Materials = lightingSample.bump2Feature.materials;
      passState.bump2Diagnostic = worldBump2Diagnostic;
      // The 4 `$ssbump` materials have no layer-1 normal for the lerp to start
      // from; under the diagnostic they lerp onto the base the frame already
      // computed. The count comes from the same parse the feature rows used.
      passState.bump2DiagnosticBase = worldBump2Diagnostic ? features.bumpMap2SsBump : 0;
      // With an override in hand every effective layer-2 tile sampled it (the
      // fetch takes the override branch before it looks at the material's own
      // VTF), so the forced count is the effective count. With no override it is
      // zero by construction, which is what makes the ledger able to say "layer
      // 2 drew, and it drew from a diagnostic texture" as one sentence.
      passState.bump2Forced = worldBump2TextureOverride.empty()
        ? 0 : lightingSample.bump2Feature.materials;
      passState.detailRequested = renderer.settings().detailLayer;
      passState.detailEffective = lightingSample.detailMode;
      passState.detailMaterials = lightingSample.detailFeature.materials;
      const auto lighting = tf2::native::scanWorldLightingSemantics(inputs, resolvers, passState);
      traceFmt("gpu: world lighting dynamic=%d specular=%d bump=%d requestedDynamic=%d"
        " requestedSpecular=%d requestedBump=%d bumpOwner=%s bumpTexture=%s bumpTextureSource=%s"
        " bumpOwnerDeclared=%d",
        lightingSample.dynamicLightingMode ? 1 : 0, lightingSample.specularMode ? 1 : 0,
        lightingSample.bumpMode ? 1 : 0, renderer.settings().dynamicLighting ? 1 : 0,
        renderer.settings().specular ? 1 : 0, renderer.settings().bumpMapping ? 1 : 0,
        mapMaterialName.empty() ? "-" : mapMaterialName.c_str(),
        mapBumpMapPath.empty() ? "-" : mapBumpMapPath.c_str(),
        passState.bumpTextureSource.c_str(),
        mapBumpMapDeclared ? 1 : 0);
      traceFmt("gpu: world lighting-summary %s", lighting.summary.c_str());
      // T27: what reached the vertex buffer, per feature. These are the numbers
      // between "the material asked" (the feature-fetch line) and "a pixel
      // moved" (the capture): a tile in the atlas with no vertex bit is a
      // texture nothing samples, and a bit with no tile would sample the
      // filler. `*Enabled` here is the renderer's own flag, so a caller that set
      // the switch and skipped the upload reads as 0 rather than as on.
      traceFmt("gpu: world feature-apply bump enabled=%d tiles=%zu materials=%zu vertices=%zu"
        " bump2 enabled=%d tiles=%zu materials=%zu vertices=%zu weights=%zu(%.3f..%.3f)"
        " detail enabled=%d tiles=%zu materials=%zu vertices=%zu",
        lightingSample.bumpFeature.enabled ? 1 : 0, lightingSample.bumpFeature.tiles,
        lightingSample.bumpFeature.materials, lightingSample.bumpFeature.vertices,
        lightingSample.bump2Feature.enabled ? 1 : 0, lightingSample.bump2Feature.tiles,
        lightingSample.bump2Feature.materials, lightingSample.bump2Feature.vertices,
        lightingSample.bump2Feature.bump2DistinctWeights,
        static_cast<double>(lightingSample.bump2Feature.bump2WeightMin),
        static_cast<double>(lightingSample.bump2Feature.bump2WeightMax),
        lightingSample.detailFeature.enabled ? 1 : 0, lightingSample.detailFeature.tiles,
        lightingSample.detailFeature.materials, lightingSample.detailFeature.vertices);
      for (const auto& control : lighting.controls) {
        traceFmt("gpu: world lighting-control control=%s kind=%s state=%s declared=%zu visible=%zu"
          " resolved=%zu missing=%zu materials=%zu triangles=%zu requested=%d effective=%d"
          " parsed=%d first=%s resource=%s reason=%s",
          control.name.c_str(), tf2::native::worldLightingKindName(control.kind),
          tf2::native::worldLightingStateName(control.state), control.declared,
          control.visibleDeclared, control.resolved, control.missing, control.visibleMaterials,
          control.visibleTriangles, control.requested ? 1 : 0, control.effective ? 1 : 0,
          control.parsed ? 1 : 0,
          control.firstMaterial.empty() ? "-" : control.firstMaterial.c_str(),
          control.firstResource.empty() ? "-" : control.firstResource.c_str(),
          control.reason.empty() ? "-" : control.reason.c_str());
      }
    }
  };
  // ---- T21/T26: the map -> screen projection, written once ----------------
  // `uploadWorldGeometry` publishes the transform and `worldShaderConstants` is
  // the matrix the draw used, so this is the product's own camera and not a
  // second one. T21 walks point -> triangle for the material chain; T26 walks
  // point -> triangle for the empty pixel. Two copies of these twenty lines
  // would be two things that can disagree with the frame they explain.
  struct WorldTraceProjection {
    bool valid = false;
    float m[16] = {};
    int width = 0;
    int height = 0;
    float scale = 1.0f;
    float centerX = 0.0f;
    float centerY = 0.0f;
    float minZ = 0.0f;
    float spanZ = 1.0f;
  };
  const auto worldTraceProjection = [&]() {
    WorldTraceProjection out;
    const auto constants = renderer.worldShaderConstants();
    if (!constants.valid) return out;
    out.valid = true;
    for (int i = 0; i < 16; ++i) out.m[i] = constants.mvp[i];
    out.width = constants.width;
    out.height = constants.height;
    out.scale = renderer.worldHorizontalScale();
    out.centerX = renderer.worldCenterX();
    out.centerY = renderer.worldCenterY();
    out.minZ = renderer.worldMinZ();
    out.spanZ = renderer.worldSpanZ();
    return out;
  };
  // The same transform uploadWorldGeometry applied (T2 publishes it for exactly
  // this reason), then the shader's row-vector multiply. `depth` is the
  // post-divide z the depth buffer holds; `invW` is 1/w for the
  // perspective-correct interpolation of the attributes.
  const auto projectMapPoint = [](const WorldTraceProjection& p, const tf2::native::BspVertex& point,
                                  float& sx, float& sy, float& depth, float& invW) {
    const float x = (point.x - p.centerX) * p.scale;
    const float y = (point.y - p.centerY) * p.scale;
    const float z = (point.z - p.minZ) / p.spanZ * 0.8f + 0.1f;
    const float* m = p.m;
    const float cx = x * m[0] + y * m[4] + z * m[8] + m[12];
    const float cy = x * m[1] + y * m[5] + z * m[9] + m[13];
    const float cz = x * m[2] + y * m[6] + z * m[10] + m[14];
    const float cw = x * m[3] + y * m[7] + z * m[11] + m[15];
    if (!(cw > 1.0e-5f) || !std::isfinite(cw)) return false;
    sx = (cx / cw * 0.5f + 0.5f) * static_cast<float>(p.width);
    sy = (0.5f - cy / cw * 0.5f) * static_cast<float>(p.height);
    depth = cz / cw;
    invW = 1.0f / cw;
    return std::isfinite(sx) && std::isfinite(sy) && std::isfinite(depth);
  };
  // ---- T21: the per-face material chain -----------------------------------
  // A material-fetch count says how many materials got a tile. It cannot say
  // whether the tile that covers a wall is the wall's own texture, whether the
  // uv that reaches it is the face's own uv, or whether the pixel on screen is
  // the texel the shader sampled. This trace answers those three questions at
  // a fixed set of screen points by walking the chain in the other direction:
  // point -> front-most drawn triangle -> material -> VMT/VTF store -> tile
  // rectangle -> the texel the shader would fetch -> the shaded value the
  // pixel shader should output. The capture in --world-albedo mode is the
  // other half: it writes that albedo to the frame, so the prediction and the
  // pixel can be compared. Nothing here is aggregated: a line names one point
  // and one face, and the gate fails on the point it can name.
  const auto traceWorldFaces = [&]() {
    if (!worldFaceTraceRequested) return;
    const auto drawState = renderer.worldMaterialSampleState();
    const auto& tileTable = renderer.worldTileTable();
    std::unordered_map<std::string, const tf2::native::WorldTileReading*> tileByMaterial;
    tileByMaterial.reserve(tileTable.size());
    for (const auto& row : tileTable) tileByMaterial.emplace(row.material, &row);
    std::unordered_map<std::string, const tf2::native::WorldTexture*> textureByMaterial;
    textureByMaterial.reserve(worldTextures.size());
    for (const auto& texture : worldTextures) textureByMaterial.emplace(texture.material, &texture);

    // The sample points are fixed so two runs, two trees and two builds can be
    // compared: a grid across the frame, plus one point inside each of the four
    // regions visual-baseline-check.sh pins. They are screen pixels, top-left
    // origin, and they are the same pixels frame-region-stats.py reads.
    static constexpr int kPoints[][2] = {
      {90, 70}, {300, 70}, {510, 70}, {720, 70}, {930, 70}, {1140, 70},
      {90, 230}, {300, 230}, {510, 230}, {720, 230}, {930, 230}, {1140, 230},
      {90, 390}, {300, 390}, {510, 390}, {720, 390}, {930, 390}, {1140, 390},
      {90, 550}, {300, 550}, {510, 550}, {720, 550}, {930, 550}, {1140, 550},
      {200, 120}, {200, 620}, {1180, 200}, {630, 30}
    };
    const auto projection = worldTraceProjection();
    if (!projection.valid) {
      traceFmt("gpu: world face-trace status=unavailable reason=no-world-constants");
      return;
    }
    // The shading half of the chain is predicted from the constants the draw
    // used, not re-derived from the scene.
    const auto constants = renderer.worldShaderConstants();
    const int frameWidth = projection.width;
    const int frameHeight = projection.height;
    // The shared projector, bound to this frame's matrix.
    const auto project = [&](const tf2::native::BspVertex& point, float& sx, float& sy, float& depth,
                             float& invW) {
      return projectMapPoint(projection, point, sx, sy, depth, invW);
    };
    // Bilinear, with the texel-centre convention worldTileCoord writes:
    // texcoord t addresses texel centres at integer t, so texel i contributes
    // to [i-1, i+1]. Repeat is the material's own `wrap`.
    const auto sampleBilinear = [](const std::vector<std::uint8_t>& rgba, int width, int height,
                                   float u, float v, unsigned char out[3]) {
      if (rgba.empty() || width <= 0 || height <= 0) return false;
      const auto wrapIndex = [](int value, int size) {
        const int wrapped = value % size;
        return wrapped < 0 ? wrapped + size : wrapped;
      };
      const int x0 = static_cast<int>(std::floor(u));
      const int y0 = static_cast<int>(std::floor(v));
      const float fx = u - static_cast<float>(x0);
      const float fy = v - static_cast<float>(y0);
      float sum[3] = {0.0f, 0.0f, 0.0f};
      for (int dy = 0; dy < 2; ++dy) {
        for (int dx = 0; dx < 2; ++dx) {
          const int x = wrapIndex(x0 + dx, width);
          const int y = wrapIndex(y0 + dy, height);
          const float weight = (dx ? fx : 1.0f - fx) * (dy ? fy : 1.0f - fy);
          const std::size_t at = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width)
            + static_cast<std::size_t>(x)) * 4u;
          if (at + 2u >= rgba.size()) return false;
          for (int c = 0; c < 3; ++c) sum[c] += weight * static_cast<float>(rgba[at + static_cast<std::size_t>(c)]);
        }
      }
      for (int c = 0; c < 3; ++c) {
        out[c] = static_cast<unsigned char>(std::clamp(std::lround(sum[c]), 0l, 255l));
      }
      return true;
    };
    const auto shadeFor = [](const std::string& material) {
      const std::uint32_t hash = static_cast<std::uint32_t>(std::hash<std::string>{}(material));
      return 0.78f + static_cast<float>(hash % 23) / 100.0f;
    };
    const bool clipApplied = drawState.worldVisClipApplied
      && !worldVisKeptIndices.empty();
    std::size_t hits = 0, tiledHits = 0, untiledHits = 0, blendHits = 0, oversizedHits = 0;
    std::size_t beyondCapHits = 0, depthMatched = 0, discriminatingHits = 0;
    for (const auto& point : kPoints) {
      const int px = point[0];
      const int py = point[1];
      if (px < 0 || py < 0 || px >= frameWidth || py >= frameHeight) continue;
      const float fx = static_cast<float>(px) + 0.5f;
      const float fy = static_cast<float>(py) + 0.5f;
      int bestTriangle = -1;
      float bestDepth = 2.0f;
      float bestBary[3] = {0.0f, 0.0f, 0.0f};
      float bestInvW[3] = {1.0f, 1.0f, 1.0f};
      const auto consider = [&](std::size_t ordinal) {
        if (ordinal >= displayMap.triangles.size()) return;
        const auto& triangle = displayMap.triangles[ordinal];
        if (triangle.water) return;
        float ax, ay, az, aw, bx, by, bz, bw, cx, cy, cz, cw;
        if (!project(triangle.a, ax, ay, az, aw) || !project(triangle.b, bx, by, bz, bw)
            || !project(triangle.c, cx, cy, cz, cw)) return;
        // Front faces are clockwise in D3D's screen space; a back face is not
        // rasterised when the pass culls, so it cannot be the pixel's owner.
        const float area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
        if (drawState.worldCullBackfaces && !(area > 0.0f)) return;
        if (std::fabs(area) < 1.0e-6f) return;
        const float w0 = ((by - cy) * (fx - cx) + (cx - bx) * (fy - cy)) / area;
        const float w1 = ((cy - ay) * (fx - cx) + (ax - cx) * (fy - cy)) / area;
        const float w2 = 1.0f - w0 - w1;
        if (w0 < -1.0e-4f || w1 < -1.0e-4f || w2 < -1.0e-4f) return;
        const float depth = w0 * az + w1 * bz + w2 * cz;
        if (!(depth >= 0.0f) || depth >= bestDepth) return;
        bestDepth = depth;
        bestTriangle = static_cast<int>(ordinal);
        bestBary[0] = w0; bestBary[1] = w1; bestBary[2] = w2;
        bestInvW[0] = aw; bestInvW[1] = bw; bestInvW[2] = cw;
      };
      if (clipApplied) {
        for (const std::size_t ordinal : worldVisKeptIndices) consider(ordinal);
      } else {
        for (std::size_t ordinal = 0; ordinal < displayMap.triangles.size(); ++ordinal) consider(ordinal);
      }
      if (bestTriangle < 0) {
        traceFmt("gpu: world face-trace point=%d,%d tri=none reason=no-drawn-triangle",
          px, py);
        continue;
      }
      const auto& triangle = displayMap.triangles[static_cast<std::size_t>(bestTriangle)];
      // Does the frame's own depth agree that this face owns the pixel? A model,
      // the water pass or a nearer triangle would answer no. Unmeasured (-1)
      // is not a match either, so a run without a depth readback cannot pass
      // the pixel comparison by accident.
      const float frameDepth = renderer.capturedDepthAt(px, py);
      // The ViewModel pass CLEARS the depth buffer to 1.0 and then draws the
      // weapon, so after it a pixel the weapon touched is < 1.0 and a pixel it
      // did not is exactly the cleared value. Comparing the final depth with
      // the world depth would be meaningless -- the weapon lives in its own
      // space and its depth at this pixel was 0.9822 over a world depth of
      // 0.5657 -- so the test is "still cleared".
      const float finalDepth = renderer.capturedFinalDepthAt(px, py);
      const bool depthMatch = frameDepth >= 0.0f && std::fabs(frameDepth - bestDepth) <= 2.0e-5f
        && finalDepth >= 0.999f;
      const auto chainIt = worldMaterialChain.find(triangle.material);
      if (chainIt == worldMaterialChain.end()) {
        traceFmt("gpu: world face-trace point=%d,%d tri=%d material=%s reason=no-chain-record",
          px, py, bestTriangle, triangle.material.c_str());
        continue;
      }
      const auto& chain = chainIt->second;
      const auto tileIt = tileByMaterial.find(triangle.material);
      const tf2::native::WorldTileReading* tile = tileIt == tileByMaterial.end() ? nullptr : tileIt->second;
      const bool tiled = tile != nullptr && tile->batch >= 0;
      // Perspective-correct weights: b_i/w_i over the sum. This is what the
      // rasterizer does, and it is what makes a CPU prediction comparable to a
      // pixel on a triangle that recedes from the camera.
      float bary[3] = {bestBary[0] * bestInvW[0], bestBary[1] * bestInvW[1], bestBary[2] * bestInvW[2]};
      const float barySum = bary[0] + bary[1] + bary[2];
      if (std::fabs(barySum) > 1.0e-20f) {
        for (float& value : bary) value /= barySum;
      }
      // NOT the raw texcoord: the renderer maps each VERTEX's texcoord into the
      // atlas before the rasterizer interpolates, so a triangle that spans a
      // wrap seam interpolates across the tile the way the GPU does -- which is
      // not the way the unwrapped texcoord would. Interpolating the mapped uv
      // and inverting the map below reproduces the shader's input exactly.
      const auto mapTileCoord = [](float texcoord, int sourceSize, int tileOrigin,
                                   int tileExtent, int atlasSize) {
        const float size = static_cast<float>(sourceSize > 0 ? sourceSize : 1);
        const float wrapped = texcoord - std::floor(texcoord / size) * size;
        return (static_cast<float>(tileOrigin)
          + wrapped * static_cast<float>(tileExtent) / size + 0.5f)
          / static_cast<float>(atlasSize > 0 ? atlasSize : 1);
      };
      const float lu = bary[0] * triangle.alu + bary[1] * triangle.blu + bary[2] * triangle.clu;
      const float lv = bary[0] * triangle.alv + bary[1] * triangle.blv + bary[2] * triangle.clv;
      const float blendWeight = bary[0] * triangle.aAlpha + bary[1] * triangle.bAlpha + bary[2] * triangle.cAlpha;
      bool insideTile = false;
      std::string uvText = "-";
      std::string texelText = "-";
      std::string blendText = "-";
      std::string albedoText = "-";
      std::string albedoTileText = "-";
      std::string expectedText = "-";
      int albedo[3] = {0, 0, 0};
      const auto textureIt = textureByMaterial.find(triangle.material);
      const bool haveTexture = textureIt != textureByMaterial.end();
      if (tiled) {
        ++tiledHits;
        const int sourceWidth = tile->sourceWidth > 0 ? tile->sourceWidth : tile->width;
        const int sourceHeight = tile->sourceHeight > 0 ? tile->sourceHeight : tile->height;
        if (sourceWidth > tile->width || sourceHeight > tile->height) ++oversizedHits;
        // The uv the renderer writes for this vertex, recomputed from the
        // published rectangle and the published source size. `tile->x/y` is the
        // tile's first pixel inside its border.
        const int atlasWidth = tile->atlasWidth > 0 ? tile->atlasWidth : 1;
        const int atlasHeight = tile->atlasHeight > 0 ? tile->atlasHeight : 1;
        const float su = bary[0] * mapTileCoord(triangle.au, sourceWidth, tile->x, tile->width, atlasWidth)
          + bary[1] * mapTileCoord(triangle.bu, sourceWidth, tile->x, tile->width, atlasWidth)
          + bary[2] * mapTileCoord(triangle.cu, sourceWidth, tile->x, tile->width, atlasWidth);
        const float sv = bary[0] * mapTileCoord(triangle.av, sourceHeight, tile->y, tile->height, atlasHeight)
          + bary[1] * mapTileCoord(triangle.bv, sourceHeight, tile->y, tile->height, atlasHeight)
          + bary[2] * mapTileCoord(triangle.cv, sourceHeight, tile->y, tile->height, atlasHeight);
        const float insideU = su * static_cast<float>(tile->atlasWidth) - 0.5f;
        const float insideV = sv * static_cast<float>(tile->atlasHeight) - 0.5f;
        insideTile = insideU >= static_cast<float>(tile->x) - 0.5f
          && insideU <= static_cast<float>(tile->x + tile->width) - 0.5f
          && insideV >= static_cast<float>(tile->y) - 0.5f
          && insideV <= static_cast<float>(tile->y + tile->height) - 0.5f;
        char uvBuffer[96];
        std::snprintf(uvBuffer, sizeof(uvBuffer), "%.5f,%.5f", su, sv);
        uvText = uvBuffer;
        // The tile-local texel coordinate the atlas uv addresses. It is the
        // same number for all three layers: the second layer and the mask were
        // resampled onto this tile's grid before upload.
        // The sample coordinate in TILE texels, which is what the GPU's linear
        // filter sees. It is NOT the source texel coordinate: a texture larger
        // than 512 was box-downscaled into the tile before upload, so sampling
        // the full-resolution source at the same spot picks a different texel
        // -- measured, that difference alone was up to 40 LSB on a 1024-source
        // floor. The tile buffer is the texture the shader samples, so the
        // prediction samples it too.
        const float tileU = su * static_cast<float>(atlasWidth) - 0.5f - static_cast<float>(tile->x);
        const float tileV = sv * static_cast<float>(atlasHeight) - 0.5f - static_cast<float>(tile->y);
        // T21: the albedo the material path writes, as a function of one
        // tile-local texcoord. It has to be a function of the coordinate and
        // nothing else, because the fix under test moves the coordinate and
        // keeps everything else -- so the rival prediction has to walk the
        // second layer and the modulate mask too. Predicting the rival from the
        // first layer alone made a blend material disagree with the frame for a
        // reason that had nothing to do with the uv (measured: one point, delta
        // 113, at a face whose blend weight is 1.0).
        const auto predictAlbedo = [&](float localU, float localV, int out[3],
                                       std::string* blendOut) {
          unsigned char base[3];
          if (!haveTexture) return false;
          if (!sampleBilinear(textureIt->second->rgba, tile->width, tile->height,
                localU, localV, base)) return false;
          out[0] = base[0]; out[1] = base[1]; out[2] = base[2];
          if (!chain.blend) {
            if (blendOut) *blendOut = "none";
            return true;
          }
          const auto& texture = *textureIt->second;
          // The second layer was resampled onto this tile's grid before upload,
          // so the same tile-local texcoord addresses it. Where the byte count
          // does not match the tile the layer was refused at upload, and the
          // trace says so instead of sampling garbage.
          if (texture.blendRgba.empty()) {
            if (blendOut) *blendOut = "declared-no-layer";
            return true;
          }
          if (texture.blendRgba.size() != static_cast<std::size_t>(tile->width) * tile->height * 4u) {
            if (blendOut) *blendOut = "declared-grid-differs";
            return true;
          }
          unsigned char second[3];
          if (!sampleBilinear(texture.blendRgba, tile->width, tile->height,
                localU, localV, second)) return true;
          float weight = std::clamp(blendWeight < 0.0f ? 0.0f : blendWeight, 0.0f, 1.0f);
          // `$blendmodulatetexture` reshapes the weight: lower = sat(g-r),
          // upper = sat(g+r), weight = smoothstep(lower, upper, weight).
          // Skipping it is a real difference on this map -- T12 measured
          // 10 declared and 10 resolved modulate layers -- so the mirror
          // applies it whenever the mask arrived on this tile's grid.
          if (texture.blendModulateRgba.size()
              == static_cast<std::size_t>(tile->width) * tile->height * 4u) {
            unsigned char mask[3];
            if (sampleBilinear(texture.blendModulateRgba, tile->width, tile->height,
                  localU, localV, mask)) {
              const float lower = std::clamp((static_cast<float>(mask[1]) - static_cast<float>(mask[0])) / 255.0f, 0.0f, 1.0f);
              const float upper = std::clamp((static_cast<float>(mask[1]) + static_cast<float>(mask[0])) / 255.0f, 0.0f, 1.0f);
              const float span = std::max(upper - lower, 1.0e-5f);
              const float t = std::clamp((weight - lower) / span, 0.0f, 1.0f);
              weight = t * t * (3.0f - 2.0f * t);
            }
          }
          for (int c = 0; c < 3; ++c) {
            out[c] = static_cast<int>(std::lround(
              static_cast<float>(out[c]) * (1.0f - weight) + static_cast<float>(second[c]) * weight));
          }
          if (blendOut) {
            char blendBuffer[64];
            std::snprintf(blendBuffer, sizeof(blendBuffer), "%.3f texel=%u,%u,%u", weight,
              second[0], second[1], second[2]);
            *blendOut = blendBuffer;
          }
          return true;
        };
        unsigned char texel[3];
        if (haveTexture && sampleBilinear(textureIt->second->rgba, tile->width, tile->height,
              tileU, tileV, texel)) {
          // (the tile buffer is the downscaled texture, so this is the texel
          // the shader's linear sampler reads at the interpolated uv)
          char texelBuffer[48];
          std::snprintf(texelBuffer, sizeof(texelBuffer), "%u,%u,%u", texel[0], texel[1], texel[2]);
          texelText = texelBuffer;
        }
        if (chain.blend && haveTexture) ++blendHits;
        predictAlbedo(tileU, tileV, albedo, &blendText);
        // T21: the rival hypothesis, predicted from the same tile buffer and
        // the same barycentrics so that nothing but the uv normaliser differs.
        // Before this task the renderer normalised the texcoord by the TILE
        // size, not the source size, so a 1024 texture box-downscaled into a
        // 512 tile repeated twice across the face it belongs to. Both
        // predictions are emitted because "the new uv is right" is only shown
        // when the same pixel REJECTS the old one: run against a build that
        // still carries the old uv, `albedoTile` must match the frame and
        // `albedo` must not, and the reverse on a build with the fix. A point
        // where the two predictions agree cannot discriminate, so it is
        // counted separately and never claimed as evidence.
        if (haveTexture && (sourceWidth != tile->width || sourceHeight != tile->height)) {
          const float oldU = bary[0] * mapTileCoord(triangle.au, tile->width, tile->x, tile->width, atlasWidth)
            + bary[1] * mapTileCoord(triangle.bu, tile->width, tile->x, tile->width, atlasWidth)
            + bary[2] * mapTileCoord(triangle.cu, tile->width, tile->x, tile->width, atlasWidth);
          const float oldV = bary[0] * mapTileCoord(triangle.av, tile->height, tile->y, tile->height, atlasHeight)
            + bary[1] * mapTileCoord(triangle.bv, tile->height, tile->y, tile->height, atlasHeight)
            + bary[2] * mapTileCoord(triangle.cv, tile->height, tile->y, tile->height, atlasHeight);
          int rival[3] = {0, 0, 0};
          if (predictAlbedo(oldU * static_cast<float>(atlasWidth) - 0.5f - static_cast<float>(tile->x),
                oldV * static_cast<float>(atlasHeight) - 0.5f - static_cast<float>(tile->y),
                rival, nullptr)) {
            char oldBuffer[48];
            std::snprintf(oldBuffer, sizeof(oldBuffer), "%d,%d,%d", rival[0], rival[1], rival[2]);
            albedoTileText = oldBuffer;
          }
        }
      } else {
        ++untiledHits;
        const float shade = shadeFor(triangle.material);
        const int value = static_cast<int>(std::lround(std::clamp(shade, 0.0f, 1.0f) * 255.0f));
        albedo[0] = albedo[1] = albedo[2] = value;
      }
      if (chain.beyondOldCap) ++beyondCapHits;
      if (depthMatch) ++depthMatched;
      if (albedoTileText != "-" && albedoTileText != albedoText) ++discriminatingHits;
      // The pixel shader's output, computed from the constants it was given:
      // albedo, then the sRGB-encoded lightmap sample, then the diffuse term.
      float light[3] = {1.0f, 1.0f, 1.0f};
      if (constants.lightmapEnabled && triangle.lightmapped && lu >= 0.0f) {
        // T21: bilinear, not nearest. The shader asks its sampler for
        // `lightmapTexture.Sample(sampler0, lightUv)`, and the sampler is a
        // WRAP/linear sampler, so it blends the four texels around
        // lightUv*size - 0.5. Mirroring that with one floor()'d texel looked
        // close enough on a smooth floor and then missed by 8% on a wall whose
        // lightmap has a shadow edge across it -- one point at 1140,230, delta
        // 7 against a tolerance of 6, which is exactly the shape of a
        // "lighting is wrong" finding that is really a mirror defect.
        unsigned char lightTexel[3];
        if (sampleBilinear(displayMap.lightmapAtlas, displayMap.lightmapAtlasWidth,
              displayMap.lightmapAtlasHeight,
              lu * static_cast<float>(displayMap.lightmapAtlasWidth) - 0.5f,
              lv * static_cast<float>(displayMap.lightmapAtlasHeight) - 0.5f, lightTexel)) {
          for (int c = 0; c < 3; ++c) {
            const float linear = static_cast<float>(lightTexel[c]) / 255.0f;
            light[c] = std::pow(std::max(linear, 0.0f), 1.0f / 2.2f);
          }
        }
      }
      // The renderer gives all three vertices the triangle's own geometric
      // normal, so the interpolated normal is that normal; reproduced here the
      // same way rather than read from a vertex field that does not exist.
      const float abx = triangle.b.x - triangle.a.x, aby = triangle.b.y - triangle.a.y, abz = triangle.b.z - triangle.a.z;
      const float acx = triangle.c.x - triangle.a.x, acy = triangle.c.y - triangle.a.y, acz = triangle.c.z - triangle.a.z;
      float normal[3] = {aby * acz - abz * acy, abz * acx - abx * acz, abx * acy - aby * acx};
      const float normalLength = std::sqrt(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
      if (normalLength > 1.0e-8f) {
        for (float& value : normal) value /= normalLength;
      } else {
        normal[0] = 0.0f; normal[1] = 0.0f; normal[2] = 1.0f;
      }
      float lighting = 1.0f;
      if (constants.lightMode > 0.5f) {
        float direction[3] = {constants.lightDirection[0], constants.lightDirection[1], constants.lightDirection[2]};
        const float directionLength = std::sqrt(direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2]);
        if (directionLength > 1.0e-8f) for (float& value : direction) value /= directionLength;
        const float diffuse = std::clamp(normal[0] * direction[0] + normal[1] * direction[1] + normal[2] * direction[2], 0.0f, 1.0f);
        lighting = 1.0f * (1.0f - constants.lightMode) + (0.62f + 0.38f * diffuse) * constants.lightMode;
      }
      int expected[3];
      for (int c = 0; c < 3; ++c) {
        float value = static_cast<float>(albedo[c]) / 255.0f;
        if (constants.selfIllum > 0.0f) {
          value += value * constants.selfIllumTint[c] * std::clamp(constants.selfIllum, 0.0f, 1.0f);
        }
        value *= light[c] * lighting;
        expected[c] = static_cast<int>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
      }
      char albedoBuffer[48];
      std::snprintf(albedoBuffer, sizeof(albedoBuffer), "%d,%d,%d", albedo[0], albedo[1], albedo[2]);
      albedoText = albedoBuffer;
      char expectedBuffer[48];
      std::snprintf(expectedBuffer, sizeof(expectedBuffer), "%d,%d,%d", expected[0], expected[1], expected[2]);
      expectedText = expectedBuffer;
      ++hits;
      traceFmt("gpu: world face-trace point=%d,%d tri=%d material=%s vmt=%s vmtStore=%s vtf=%s"
        " vtfStore=%s src=%dx%d tile=%d/%d size=%dx%d rect=%d,%d atlas=%dx%d uv=%s uvInside=%d texel=%s"
        " blend=%s luv=%.4f,%.4f light=%d,%d,%d albedo=%s albedoTile=%s expected=%s reason=%s"
        " beyondOldCap=%d oldOrder=%zu depth=%.4f frameDepth=%.4f finalDepth=%.4f depthMatch=%d frameRaw=%08x",
        px, py, bestTriangle, triangle.material.c_str(),
        chain.vmtPath.empty() ? "-" : chain.vmtPath.c_str(), chain.vmtStore.c_str(),
        chain.vtfPath.empty() ? "-" : chain.vtfPath.c_str(), chain.vtfStore.c_str(),
        chain.sourceWidth, chain.sourceHeight,
        tiled ? tile->batch : -1, tiled ? tile->tile : -1,
        tiled ? tile->width : 0, tiled ? tile->height : 0,
        tiled ? tile->x : 0, tiled ? tile->y : 0,
        tiled ? tile->atlasWidth : 0, tiled ? tile->atlasHeight : 0,
        uvText.c_str(), insideTile ? 1 : 0, texelText.c_str(), blendText.c_str(), lu, lv,
        static_cast<int>(std::lround(light[0] * 255.0f)),
        static_cast<int>(std::lround(light[1] * 255.0f)),
        static_cast<int>(std::lround(light[2] * 255.0f)),
        albedoText.c_str(), albedoTileText.c_str(), expectedText.c_str(),
        tiled ? "tiled" : (tile != nullptr ? tile->reason.c_str() : chain.reason.c_str()),
        chain.beyondOldCap ? 1 : 0, chain.oldOrderAccepted, bestDepth, frameDepth, finalDepth,
        depthMatch ? 1 : 0, renderer.capturedDepthRawAt(px, py));
    }
    // Named refusals, weighted by the pvs-visible triangles they cost. The
    // aggregate material-fetch line says `vmtMissing=3`; this says how much of
    // THIS frame's geometry those three materials were.
    std::unordered_map<std::string, std::size_t> visibleByMaterial;
    for (const std::size_t ordinal : worldVisKeptIndices) {
      if (ordinal < displayMap.triangles.size()) ++visibleByMaterial[displayMap.triangles[ordinal].material];
    }
    std::unordered_map<std::string, std::pair<std::size_t, std::size_t>> refusalTotals;
    for (const auto& entry : worldMaterialChain) {
      if (entry.second.reason == "tiled") continue;
      auto& total = refusalTotals[entry.second.reason];
      ++total.first;
      total.second += visibleByMaterial[entry.first];
    }
    for (const auto& entry : refusalTotals) {
      traceFmt("gpu: world visible-refusal reason=%s materials=%zu visibleTriangles=%zu",
        entry.first.c_str(), entry.second.first, entry.second.second);
    }
    traceFmt("gpu: world face-trace-summary points=%d hits=%zu tiled=%zu untiled=%zu blend=%zu"
      " oversized=%zu beyondOldCap=%zu depthMatched=%zu discriminating=%zu clipApplied=%d"
      " frame=%dx%d depthBuffer=%dx%d",
      static_cast<int>(sizeof(kPoints) / sizeof(kPoints[0])), hits, tiledHits, untiledHits, blendHits,
      oversizedHits, beyondCapHits, depthMatched, discriminatingHits, clipApplied ? 1 : 0, frameWidth, frameHeight,
      renderer.capturedDepthWidth(), renderer.capturedDepthHeight());
  };
  // ---- T26: the empty-pixel trace -----------------------------------------
  // A missing pixel has to be attributed before any geometry rule is changed,
  // and a colour cannot do it: the sky box fills every pixel no world triangle
  // covers, so "the frame shows sky" is true whether the map intended sky or
  // has a hole. This trace answers the question with the map's own data, at the
  // product's own camera: for a named screen point it reports
  //
  //   drawn  the front-most world triangle the shipped switches would draw,
  //   any    the front-most world triangle with the cull and the pvs off,
  //   sky    the front-most BSP sky-brush triangle (the `--world-geometry sky`
  //          buffer), which is the map's statement of intent for that pixel.
  //
  // The rasterizer's own test is reused rather than a ray cast in another space:
  // project the three vertices with the frame's matrix, take the screen
  // barycentrics, keep the nearest. `frameDepth` is the depth the capture
  // actually holds, so a point the oracle calls empty and the buffer calls
  // occupied is a reading that contradicts itself and is printed as such.
  const auto traceWorldHoles = [&]() {
    if (worldHoleTracePoints.empty()) return;
    const auto projection = worldTraceProjection();
    if (!projection.valid) {
      traceFmt("gpu: world hole-trace status=unavailable reason=no-world-constants");
      return;
    }
    const auto drawState = renderer.worldMaterialSampleState();
    const bool cullEnabled = drawState.worldCullBackfaces;
    const bool clipApplied = drawState.worldVisClipApplied && !worldVisKeptIndices.empty();
    std::vector<std::uint8_t> kept;
    if (clipApplied) {
      kept.assign(displayMap.triangles.size(), 0);
      for (const std::size_t ordinal : worldVisKeptIndices) {
        if (ordinal < kept.size()) kept[ordinal] = 1;
      }
    }
    std::size_t drawnHits = 0, anyHits = 0, skyHits = 0, missingWithSky = 0, missingWithoutSky = 0;
    for (const auto& point : worldHoleTracePoints) {
      const int px = point.first, py = point.second;
      if (px < 0 || py < 0 || px >= projection.width || py >= projection.height) continue;
      const float fx = static_cast<float>(px) + 0.5f;
      const float fy = static_cast<float>(py) + 0.5f;
      int drawnOrdinal = -1, anyOrdinal = -1, skyOrdinal = -1;
      float drawnDepth = 2.0f, anyDepth = 2.0f, skyDepth = 2.0f;
      // The screen-space coverage test, shared by both lists: same projection,
      // same barycentrics, same depth.
      const auto covers = [&](const tf2::native::BspTriangle& triangle, float& depth, float& area) {
        float ax, ay, az, aw, bx, by, bz, bw, cx, cy, cz, cw;
        if (!projectMapPoint(projection, triangle.a, ax, ay, az, aw)
            || !projectMapPoint(projection, triangle.b, bx, by, bz, bw)
            || !projectMapPoint(projection, triangle.c, cx, cy, cz, cw)) return false;
        area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
        if (std::fabs(area) < 1.0e-6f) return false;
        const float w0 = ((by - cy) * (fx - cx) + (cx - bx) * (fy - cy)) / area;
        const float w1 = ((cy - ay) * (fx - cx) + (ax - cx) * (fy - cy)) / area;
        const float w2 = 1.0f - w0 - w1;
        if (w0 < -1.0e-4f || w1 < -1.0e-4f || w2 < -1.0e-4f) return false;
        depth = w0 * az + w1 * bz + w2 * cz;
        return depth >= 0.0f;
      };
      for (std::size_t ordinal = 0; ordinal < displayMap.triangles.size(); ++ordinal) {
        const auto& triangle = displayMap.triangles[ordinal];
        if (triangle.water) continue;
        float depth = 0.0f, area = 0.0f;
        if (!covers(triangle, depth, area)) continue;
        if (depth < anyDepth) { anyDepth = depth; anyOrdinal = static_cast<int>(ordinal); }
        const bool faces = area > 0.0f;
        const bool visible = !clipApplied || kept[ordinal] != 0;
        if ((!cullEnabled || faces) && visible && depth < drawnDepth) {
          drawnDepth = depth;
          drawnOrdinal = static_cast<int>(ordinal);
        }
      }
      for (std::size_t ordinal = 0; ordinal < displayMap.skyTriangles.size(); ++ordinal) {
        float depth = 0.0f, area = 0.0f;
        if (!covers(displayMap.skyTriangles[ordinal], depth, area)) continue;
        if (depth < skyDepth) { skyDepth = depth; skyOrdinal = static_cast<int>(ordinal); }
      }
      const auto describe = [&](int ordinal, bool sky, float depth, char* buffer, std::size_t size) {
        if (ordinal < 0) {
          std::snprintf(buffer, size, "-");
          return;
        }
        const auto& triangle = sky ? displayMap.skyTriangles[static_cast<std::size_t>(ordinal)]
                                   : displayMap.triangles[static_cast<std::size_t>(ordinal)];
        std::snprintf(buffer, size, "face=%d material=%s tri=%d depth=%.4f",
          triangle.faceIndex, triangle.material.empty() ? "-" : triangle.material.c_str(), ordinal, depth);
      };
      char drawnText[192], anyText[192], skyText[192];
      describe(drawnOrdinal, false, drawnDepth, drawnText, sizeof(drawnText));
      describe(anyOrdinal, false, anyDepth, anyText, sizeof(anyText));
      describe(skyOrdinal, true, skyDepth, skyText, sizeof(skyText));
      const float frameDepth = renderer.capturedDepthAt(px, py);
      traceFmt("gpu: world hole-trace point=%d,%d drawn=%s any=%s sky=%s frameDepth=%.4f",
        px, py, drawnText, anyText, skyText, frameDepth);
      if (drawnOrdinal >= 0) ++drawnHits;
      if (anyOrdinal >= 0) ++anyHits;
      if (skyOrdinal >= 0) ++skyHits;
      if (drawnOrdinal < 0) {
        if (skyOrdinal >= 0) ++missingWithSky; else ++missingWithoutSky;
      }
    }
    traceFmt("gpu: world hole-trace-summary points=%zu drawnHits=%zu anyHits=%zu skyHits=%zu"
      " emptyWithSky=%zu emptyWithoutSky=%zu cull=%d clipApplied=%d",
      worldHoleTracePoints.size(), drawnHits, anyHits, skyHits, missingWithSky, missingWithoutSky,
      cullEnabled ? 1 : 0, clipApplied ? 1 : 0);
  };
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
    traceFmt("gpu: viewmodel materialReadings passRan=%d materialSlots=%zu materialResolved=%zu texturesUploaded=%zu texturesReused=%zu fallbackSlots=%zu alphaSlots=%zu alphaAppliedRanges=%zu phongSlots=%zu phongAppliedRanges=%u bumpSlots=%zu bumpAppliedRanges=%u selfillumSlots=%zu selfillumAppliedRanges=%u decodedSlots=%zu drawRanges=%zu fallbackRanges=%zu boundSlots=%zu blendedRanges=%zu alphaTestedRanges=%zu flatColor=%d slotOverride=%d missingSlotInjected=%d decodeFailure=%s",
      viewModelMaterialReadings.materialPassRan ? 1 : 0,
      materialTable ? materialTable->materialSlots() : 0u,
      materialTable ? materialTable->materialResolved() : 0u,
      renderer.viewModelTexturesUploaded(),
      renderer.viewModelTexturesReused(),
      materialTable ? materialTable->fallbackSlots() : 0u,
      materialTable ? materialTable->alphaSlots() : 0u,
      renderer.viewModelBlendedRanges() + renderer.viewModelAlphaTestedRanges(),
      materialTable ? materialTable->phongSlots() : 0u,
      // T2 (2026-10-14): these three were the literal 0 in phase 1, because
      // nothing applied them. They are now the per-frame range counts the draw
      // itself incremented, and that change is the point of this round: the
      // declared-versus-applied split is only a reading if the applied half can
      // be non-zero. viewmodel-material-check.sh's three "NOT applied (phase 1)"
      // assertions are updated to match, and the report records the change.
      renderer.viewModelMaterialSampleState().phongAppliedRanges,
      materialTable ? materialTable->bumpSlots() : 0u,
      renderer.viewModelMaterialSampleState().bumpAppliedRanges,
      materialTable ? materialTable->selfillumSlots() : 0u,
      renderer.viewModelMaterialSampleState().selfillumAppliedRanges,
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
    // T2 (2026-10-14): the advanced-material line. A separate line rather than
    // more fields on the one above, so the T2.5 needles that match that line
    // keep matching it byte for byte.
    //
    // Split into two lines on purpose. traceFmt writes through a 512-byte buffer
    // and `vsnprintf` truncates silently, and this reading plus three material
    // reason strings runs past that -- measured, the first version ended mid
    // sentence at `selfillumApplied=0` and the reasons never appeared at all.
    // The counters go on one line and the prose on the next, so neither can
    // push the other off the end.
    {
      const auto fx = renderer.viewModelMaterialSampleState();
      traceFmt("gpu: viewmodel effects slot=%d parsed=%d"
        " phongState=%s phongDeclared=%d phongParsed=%d phongUploaded=%d phongApplied=%d"
        " phongAppliedRanges=%u phongExponent=%.3f phongBoost=%.3f phongFresnel=%.3f..%.3f"
        " bumpState=%s bumpDeclared=%d bumpParsed=%d bumpUploaded=%d bumpApplied=%d"
        " bumpAppliedRanges=%u"
        " selfillumState=%s selfillumDeclared=%d selfillumParsed=%d selfillumUploaded=%d"
        " selfillumApplied=%d selfillumAppliedRanges=%u"
        " selfillumMaskDeclared=%d selfillumMaskBound=%d",
        viewModelEffectsSlot, viewModelEffectsMaterialParsed ? 1 : 0,
        tf2::native::viewModelEffectStateName(fx.phongState),
        fx.phongDeclared ? 1 : 0, fx.phongParsed ? 1 : 0, fx.phongUploaded ? 1 : 0,
        fx.phongApplied ? 1 : 0, fx.phongAppliedRanges,
        fx.phongExponent, fx.phongBoost, fx.phongFresnelMin, fx.phongFresnelMax,
        tf2::native::viewModelEffectStateName(fx.bumpState),
        fx.bumpDeclared ? 1 : 0, fx.bumpParsed ? 1 : 0, fx.bumpUploaded ? 1 : 0,
        fx.bumpApplied ? 1 : 0, fx.bumpAppliedRanges,
        tf2::native::viewModelEffectStateName(fx.selfillumState),
        fx.selfillumDeclared ? 1 : 0, fx.selfillumParsed ? 1 : 0,
        fx.selfillumUploaded ? 1 : 0, fx.selfillumApplied ? 1 : 0,
        fx.selfillumAppliedRanges,
        fx.selfillumMaskDeclared ? 1 : 0, fx.selfillumMaskBound ? 1 : 0);
      // The texture readings and the three fallback flags get their own line.
      // Measured: the counters line above is 519 bytes with them on it and
      // traceFmt's buffer is 512, so `selfillumAppliedRanges` was being cut in
      // half -- the same silent truncation twice over, which is why the line is
      // now split into three rather than two.
      traceFmt("gpu: viewmodel effects-texture"
        " phongFallback=%d bumpFallback=%d selfillumFallback=%d"
        " bumpTextureResolved=%d bumpTextureUploaded=%d bumpTextureReused=%d"
        " bumpUploads=%u",
        fx.phongFallback ? 1 : 0, fx.bumpFallback ? 1 : 0,
        fx.selfillumFallback ? 1 : 0,
        fx.bumpTextureResolved ? 1 : 0, fx.bumpTextureUploaded ? 1 : 0,
        fx.bumpTextureReused ? 1 : 0, renderer.viewModelBumpTextureUploads());
      // The switches and the two whole-run texture counters go on the second
      // line with the prose. Measured: with them on the first line the whole
      // thing ran past traceFmt's 512-byte buffer and the last three fields
      // were silently dropped, which is the same silent truncation that hid
      // the reason strings before it was split.
      // The switches get their own line. Measured: the reasons line runs past
      // traceFmt's 512-byte buffer on the forced-selfillum run, where all three
      // reason strings are at their longest, and the switch fields at the end
      // were the ones dropped -- so a gate reading them saw an empty string and
      // reported "the switch is not off" for a run where it was. Four lines is
      // one more than is pretty and one fewer than a silent truncation.
      traceFmt("gpu: viewmodel effects-switch selfillumForce=%d phongSwitch=%s"
        " bumpSwitch=%s selfillumSwitch=%s texturesUploaded=%u texturesReused=%u",
        viewModelSelfillumForce ? 1 : 0,
        viewModelPhongGiven ? (viewModelPhongRequested ? "on" : "off") : "material",
        viewModelBumpGiven ? (viewModelBumpRequested ? "on" : "off") : "material",
        viewModelSelfillumGiven ? (viewModelSelfillumRequested ? "on" : "off") : "material",
        fx.texturesUploaded, fx.texturesReused);
      traceFmt("gpu: viewmodel effects-reason phong=%s bump=%s selfillum=%s",
        fx.phongReason.empty() ? "-" : fx.phongReason.c_str(),
        fx.bumpReason.empty() ? "-" : fx.bumpReason.c_str(),
        fx.selfillumReason.empty() ? "-" : fx.selfillumReason.c_str());
    }
    // The draw-order reading: the world pass is ordinal 1 and the ViewModel pass
    // must be later. Without this, "world then ViewModel" would only be a claim
    // about the order of two statements in draw().
    // `skinning` is what the pass *did*; `boneTableCoverage` is the table it was
    // handed against the skeleton the mesh declares. Read together:
    // `skinning=0 boneTableCoverage=1/45` is a refused table, `skinning=0
    // boneTableCoverage=0/0` is no table at all, and `skinning=1
    // boneTableCoverage=45/45` is a skinned draw. Without the pair, the refusal
    // and the no-table case are both just `skinning=0`.
    traceFmt("gpu: viewmodel pass order worldPass=%zu viewModelPass=%zu afterWorld=%d drawCalls=%zu vertices=%zu depthWrite=%d depthFunc=%s skinning=%d boneTableCoverage=%zu/%zu animationInput=%s meshRadius=%.2f alignment=%s vmAnimInput=%s vmAnimApplied=%d drawnModel=%s entityModelMatchesDrawn=%d sequenceInRange=%d sequenceAccepted=%d sequenceOverride=%d modelMismatch=%zu boneMismatch=%zu bindPose=%d ranges=%zu boundSlots=%zu",
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
      viewModelAnimationStats.lastBindPose ? 1 : 0,
      // How many ranges the draw actually issued, and how many distinct material
      // slots they bound. Two numbers because they answer different questions:
      // `ranges=2 boundSlots=1` is a model that draws two pieces from one
      // material, and `ranges=2 boundSlots=2` is the multi-material case T13 is
      // about. Read on the pass's own line so a partial range set cannot hide
      // behind an upload that counted all of them.
      renderer.viewModelDrawRangeCount(), renderer.viewModelBoundSlots());
    const auto& clip = renderer.viewModelCenterClip();
    const auto& viewSpace = renderer.viewModelCenterViewSpace();
    const bool onScreen = clip[3] > 0.0f
      && std::fabs(clip[0]) <= clip[3] && std::fabs(clip[1]) <= clip[3];
    const float* eye = renderer.frameEye();
    const float* forward = renderer.frameForward();
    const float* target = renderer.viewModelPlacementTarget();
    const auto& planes = renderer.viewModelFramePlanes();
    const auto& vmState = renderer.viewModelState();
    traceFmt("gpu: viewmodel frame eye=(%.2f,%.2f,%.2f) forward=(%.3f,%.3f,%.3f) placement=(%.2f,%.2f,%.2f) "
      "fov=%.2f near=%.4f far=%.4f distance=%.4f radius=%.4f hand=(%.0f,%.0f,%.0f,%.0f,%.0f,%.0f,%.0f,%.0f,%.0f,%.0f,%.0f,%.0f) attachment=%s",
      eye[0], eye[1], eye[2], forward[0], forward[1], forward[2],
      target[0], target[1], target[2],
      static_cast<double>(vmState.fov), static_cast<double>(planes[0]),
      static_cast<double>(planes[1]), static_cast<double>(planes[2]),
      static_cast<double>(planes[3]),
      static_cast<double>(vmState.handTransform[0]), static_cast<double>(vmState.handTransform[1]),
      static_cast<double>(vmState.handTransform[2]), static_cast<double>(vmState.handTransform[3]),
      static_cast<double>(vmState.handTransform[4]), static_cast<double>(vmState.handTransform[5]),
      static_cast<double>(vmState.handTransform[6]), static_cast<double>(vmState.handTransform[7]),
      static_cast<double>(vmState.handTransform[8]), static_cast<double>(vmState.handTransform[9]),
      static_cast<double>(vmState.handTransform[10]), static_cast<double>(vmState.handTransform[11]),
      vmState.attachmentName.empty() ? "-" : vmState.attachmentName.c_str());
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
    // T14: what the draw did with the slots above, on its own line.
    //   It is separate from the draw reading for a mechanical reason as well as
    //   a conceptual one: `traceFmt` formats into a 512-byte buffer, and
    //   appending these fields to the line above pushed it to 519 -- the last
    //   field was silently cut mid-path. A reading that is truncated by its own
    //   instrument is worse than no reading, and the existing line's fields are
    //   what every earlier gate pins.
    //
    //   The reachability counts above cannot answer this question: a body whose
    //   two slots both resolve and whose two ranges both bind one slot reads
    //   2/2/2/2 and looks correct. `weaponBoundSlots` and the key list are the
    //   readings that separate the two pictures.
    traceFmt("gpu: viewmodel weaponBodyMaterial ranges=%zu texturedRanges=%zu fallbackRanges=%zu boundSlots=%zu keys=%s runtimeFailure=%s unboundReason=%s flatColor=%d override=%d slotOverride=%d",
      renderer.viewModelWeaponDrawRangeCount(),
      renderer.viewModelWeaponTexturedRanges(),
      renderer.viewModelWeaponFallbackRanges(),
      renderer.viewModelWeaponBoundSlots(),
      renderer.viewModelWeaponMaterialKeys().empty()
        ? "-" : renderer.viewModelWeaponMaterialKeys().c_str(),
      viewModelWeaponStats.lastRuntimeFailure.empty()
        ? "-" : viewModelWeaponStats.lastRuntimeFailure.c_str(),
      viewModelWeaponStats.lastUnboundReason.empty()
        ? "-" : viewModelWeaponStats.lastUnboundReason.c_str(),
      viewModelWeaponFlatColor ? 1 : 0,
      commandWeaponModel.empty() ? 0 : 1,
      viewModelWeaponMaterialSlotOverride);
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
          // T27: `--capture-tick N` means "the frame drawn AT tick N", not "the
          // first frame drawn at or after N". Those differ whenever a drawn
          // frame spans more than one tick: the draw cadence is wall clock
          // (~8.3 ms target) while the tick cadence is the demo's `tickRate`
          // (~66.7 Hz), and both read the same `elapsed`, so a loaded machine
          // draws every tenth tick and a capture armed at N lands at N+10.
          // Holding the cursor at N until that draw happens makes the captured
          // tick exact by construction rather than by retrying, which is what
          // a pinned pixel diff needs. The hold lasts one frame: the draw that
          // satisfies it disarms the capture and the next iteration resumes.
          if (captureArmed && captureTick > 0 && g_playback.tick > captureTick) {
            g_playback.tick = static_cast<std::int32_t>(captureTick);
            tickAccumulator = 0.0;
            break;
          }
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
      // T34: the eye this scene tick resolved, in map units, read from the
      // same observer view the camera decision below consumes. Defaults keep
      // the line readable on a scene tick with no observe target.
      float cameraEyeX = 0.0f, cameraEyeY = 0.0f, cameraEyeZ = 0.0f;
      const bool haveObserverView =
        tf2::native::findObserverViewAtOrBeforeTick(demoNetworkSummary, sceneTick, observerView)
        && observerView.hasOrigin && observerView.hasAngles;
      if (haveObserverView) {
        cameraEyeX = observerView.origin[0];
        cameraEyeY = observerView.origin[1];
        cameraEyeZ = observerView.origin[2];
      }
      // T4 camera modes. The mode machine and the no-target rule live in
      // native_ui (decideCameraSceneAction, probe-checked over the whole
      // matrix); this block only executes the decision. The historical
      // behaviour -- clearing the demo view whenever a tick had no observe
      // target -- is exactly the "moved the camera without a target" defect
      // the task names, and HoldCurrent is what replaces it.
      const tf2::native::CameraMode cameraMode = g_debugCamera
        ? tf2::native::CameraMode::FreeLook : g_cameraMode.mode();
      if (cameraMode != g_lastAppliedCameraMode) {
        // T10 fix: the diagnostic debug camera IS a demo view -- it is set once
        // after the world upload and must survive this transition, or the first
        // frame clears it and the capture silently falls back to the orbit
        // camera (which is how two different --debug-camera positions produced
        // pixel-identical frames).
        if (cameraMode != tf2::native::CameraMode::FirstPerson && !g_debugCamera) {
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
      // T10 diagnostic camera, applied EVERY frame rather than once: the
      // one-shot apply after the world upload lost to an ordering this file
      // could not see -- three different --debug-camera positions captured
      // byte-identical default-orbit frames. Idempotent per frame, like the
      // vis clip above: the same numbers every frame cannot drift, and a
      // capture at tick 2000 is thousands of frames past the upload that
      // validates the conversion constants.
      if (g_debugCamera) {
        renderer.setObserverDemoViewWorld(g_debugCameraEye[0], g_debugCameraEye[1],
          g_debugCameraEye[2], g_debugCameraPitch, g_debugCameraYaw);
      }
      // T34: the camera pose this scene tick resolved, in MAP units.
      // Published because a gate that asserts "the picture changed between
      // two ticks" has to know whether the camera moved between them, or it
      // is measuring the world rather than the thing it names. The counters
      // above say the decision was taken; these numbers say where the camera
      // ended up, and `src` names the source the pose came from.
      traceFmt("scene: camera mode=%s target=%d follows=%u holds=%u freeze=%u"
        " eye=%.1f,%.1f,%.1f pitch=%.2f yaw=%.2f src=%s",
        tf2::native::cameraModeName(cameraMode), haveObserverView ? 1 : 0,
        g_cameraFollows, g_cameraHoldsNoTarget, g_cameraFreezeFrames,
        cameraEyeX, cameraEyeY, cameraEyeZ,
        g_debugCamera ? g_debugCameraPitch
          : (haveObserverView ? observerView.angles[0] : 0.0f),
        g_debugCamera ? g_debugCameraYaw
          : (haveObserverView ? observerView.angles[1] : 0.0f),
        g_debugCamera ? "debug" : (haveObserverView ? "demo" : "held"));
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
          // The runtime reasons describe a draw that is not happening; leaving
          // them would let the `off` frame explain a material it never bound.
          viewModelWeaponStats.lastRuntimeFailure.clear();
          viewModelWeaponStats.lastUnboundReason.clear();
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
            viewModelWeaponStats.lastRuntimeFailure.clear();
            viewModelWeaponStats.lastUnboundReason.clear();
          } else {
            // Built once per distinct path. Re-read on every scene update only
            // to compare the path: the MDL is parsed and the mesh uploaded at
            // most once per weapon, which is what keeps a weapon switch from
            // re-parsing per frame and a steady weapon from re-parsing at all.
            // The override replaces the PATH, not the lookup: a refused weapon
            // entity still refuses, because an override that could draw a body
            // where the chain found none would hide exactly the defect the
            // refusal buckets exist for.
            const std::string bodyPath = commandWeaponModel.empty()
              ? weaponBody.worldModelPath
              : normalizedModelPath(commandWeaponModel.string(), assets.tfDirectory);
            if (viewModelWeaponBody.requestedPath != bodyPath) {
              viewModelWeaponBody.requestedPath = bodyPath;
              viewModelWeaponBody.request = tf2::native::buildViewModelCompanionRequest(
                assets, bodyPath,
                viewModelLeftHand ? tf2::native::ViewModelHand::Left : tf2::native::ViewModelHand::Right,
                viewModelFovGiven ? viewModelFov : persistent.render.viewModelFov);
              viewModelWeaponBody.baseState = tf2::native::ViewModelDrawState{};
              viewModelWeaponStats.lastPath = bodyPath;
              if (viewModelWeaponBody.request.status == tf2::native::ViewModelStatus::Ok) {
                ++viewModelWeaponStats.bodiesBuilt;
                viewModelWeaponBody.baseState =
                  viewModelDrawStateFor(viewModelWeaponBody.request, viewModelLeftHand);
              } else {
                ++viewModelWeaponStats.buildFailed;
              }
              traceFmt("gpu: viewmodel weaponBody model=%s class=%s entity=%d worldModelIndex=%d path=%s status=%s meshReady=%d sequences=%zu bones=%zu vertices=%zu materials=%zu/%zu/%zu/%zu attachment=%s reason=%s",
                bodyPath.empty() ? "-" : bodyPath.c_str(),
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
                   viewModelWeaponBody.request, viewModelWeaponBody.request.mesh.vertices, uploadedNow);
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
        // WHERE THE POSE ACTUALLY PUTS THE MESH. The pass frames the model from
        // the bounding box of the vertices that were uploaded, and that box
        // describes the bind pose. Applying a pose does not move the box; it
        // moves the geometry inside it, so a frame that looks framed in the
        // bind pass can be a camera inside the mesh once the pose is applied.
        // Measured here, on the same vertices and the same matrices that were
        // just uploaded, so the number describes the draw rather than a second
        // model of it.
        tf2::native::ViewModelPoseExtent poseExtent;
        if (state.animationApplied && state.skinningEnabled) {
          poseExtent = tf2::native::measureViewModelPoseExtent(
            drawnRequest.mesh.vertices, outcome.skinMatrices, state.boneCount);
          traceFmt("gpu: viewmodel pose extent bindCenter=(%.3f,%.3f,%.3f) bindRadius=%.3f "
            "posedCenter=(%.3f,%.3f,%.3f) posedRadius=%.3f centerShift=%.3f vertices=%zu posedVertices=%zu "
            "nonFinite=%zu maxDisplacement=%.3f valid=%d reason=%s",
            poseExtent.bindCenter[0], poseExtent.bindCenter[1], poseExtent.bindCenter[2],
            static_cast<double>(poseExtent.bindRadius),
            poseExtent.posedCenter[0], poseExtent.posedCenter[1], poseExtent.posedCenter[2],
            static_cast<double>(poseExtent.posedRadius),
            static_cast<double>(poseExtent.centerShift), poseExtent.vertices,
            poseExtent.posedVertices, poseExtent.nonFinite,
            static_cast<double>(poseExtent.maxDisplacement),
            poseExtent.posedValid ? 1 : 0,
            poseExtent.reason.empty() ? "-" : poseExtent.reason.c_str());
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
        traceFmt("scene: vmAnim entity=%d sequence=%d entitySequence=%d rate=%.2f cyclePresent=%d model=%s status=%s poseAvailable=%d applied=%d frame=%d/%d entityRule=%s reason=%s owner=%d viewEntity=%d modelIndex=%d drawnModel=%s entityModelMatchesDrawn=%d sequenceInRange=%d bindPose=%d sequenceOverride=%d",
          lookup.input.entityIndex, animationInput.sequence, lookup.input.sequence,
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
    // T54: advance the panel's asynchronous work once per frame. Inert while
    // the panel is closed (the controller returns immediately).
    tf2::native::logsPanelPump(window);
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
        // T26: which tick the captured frame actually belongs to. The arm fires
        // on the first draw at or after the requested tick, and a run that
        // skips a tick captures a LATER camera than its neighbours -- measured:
        // a `world-ids` run drew 1999 then 2002, and its frame disagreed with
        // the matrix by ~15k pixels. A cross-frame account that mixes ticks is
        // comparing two cameras, so the tick travels with the frame instead of
        // being assumed from the flag.
        traceFmt("capture: armed requested=%d frame_tick=%d frames=%llu",
          static_cast<int>(captureTick), static_cast<int>(g_playback.tick),
          static_cast<unsigned long long>(renderedFrames));
      }
      if (g_renderer) {
        // The clip is decided before the draw it governs, so the frame that is
        // captured is a frame that was clipped.
        applyWorldVisClip();
        // T10 diagnostic: the cull mode is a property of the capture, so it is
        // (re)stated with the clip -- idempotent, and the reading the trace
        // prints comes from the renderer after the draw, not from this flag.
        renderer.setWorldBackfaceCull(worldCullGiven ? worldCullRequested : true);
        // T11 diagnostic: same shape for the flat/depth view; the reading is
        // taken from the renderer after the draw.
        renderer.setWorldDebugView(worldDebugView);
      }
      // T8 (2026-10-16) diagnostic, the second half of the pair above: if this
      // fires the error was already set when the frame began, so it came from a
      // load-time upload and the policy is about to kill the program for a
      // resource it had already refused. If the arm above fires but this one
      // never does, Present itself returned the failure.
      if (g_renderer && renderer.lastError() != S_OK) {
        traceFmt("gpu: pre-draw error hresult=0x%08lX drawn=%llu",
          static_cast<unsigned long>(renderer.lastError()),
          static_cast<unsigned long long>(renderedFrames));
      }
      if (g_renderer && renderer.draw(0.055f, 0.07f, 0.085f)) {
        ++renderedFrames;
        // T54: the responsiveness reading, taken where a FRAME exists. The
        // interval is the time since the previous draw (lastFrame is only moved
        // by the draw branch below), so it is a frame interval and not a loop
        // iteration, and the window it lands in is chosen by whether the panel
        // is open -- which is what makes "closed" and "open" the two halves of
        // one measurement rather than two different instruments.
        tf2::native::logsPanelSampleFrame(measuredElapsed * 1000.0);
        // After the draw, not before it: both readings count calls the passes
        // made, and a pass that has not run yet counts as zero. Taken here, the
        // line describes a frame that exists.
        traceWorldDraw();
      }
      // Emitted on the frame the capture was requested for, so the readings and
      // the bitmap on disk describe the same frame.
      if (captureRequested) writeViewModelReading();
      // T21: the face trace is taken on the same frame, after the draw. Its
      // screen points are pixels of the bitmap the capture just wrote.
      if (captureRequested) traceWorldFaces();
      // T26: the same frame, the other question. Emitted after the capture so
      // `frameDepth` is the depth buffer of the frame on disk.
      if (captureRequested) traceWorldHoles();
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
      // T8 (2026-10-16) diagnostic: the policy above can only report which arm
      // it took, so a run that dies here has been naming the branch and never the
      // error. Print the HRESULT itself -- DEVICE_HUNG (0x887A0006) is a device
      // loss the policy does not classify as one, DXGI_ERROR_INVALID_CALL
      // (0x887A0001) is a renderer state bug, and E_INVALIDARG / E_FAIL come
      // from a resource upload or the swap-chain target. `drawn` says whether
      // the failure is the first frame's or a later one's.
      traceFmt("gpu: device-error-policy hresult=0x%08lX drawn=%llu",
        static_cast<unsigned long>(error),
        static_cast<unsigned long long>(renderedFrames));
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
    // `audio_device_opened` is the historical field (every waveOutOpen call, so
    // a failed attempt moves it too); the T3 acceptance added the attempts /
    // successes pair so "the device was asked for N times and N opened" and
    // "N attempts, not one succeeded" are different readings. Under
    // --audio-disabled all three are 0.
    traceFmt("mode: summary window_created=%d window_visible=%d audio_mode=%s audio_device_opened=%zu"
      " audio_device_open_attempts=%u audio_device_opens_ok=%u events_seen=%zu events_played=%zu events_skipped=%zu events_missing=%zu drawn=%llu capture_written=%d metrics_written=%d exit=%d",
      windowAlive ? 1 : 0,
      (windowAlive && IsWindowVisible(window)) ? 1 : 0,
      audioDisabled ? "disabled" : "device",
      tf2::native::waveOutOpenCalls(),
      tf2::native::audioDeviceOpenAttempts(),
      tf2::native::audioDeviceOpenedCount(),
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
      " soundOriginTick=%d reverseRecoveries=%zu duplicateTicks=%zu"
      " soundEventsMp3Seen=%zu soundEventsMp3Played=%zu soundEventsMp3Skipped=%zu"
      " soundEventsMp3Failed=%zu soundEventsMp3Inaudible=%zu soundEventsInaudible=%zu",
      soundEventsRead, soundTimeline.size(), audioStats.eventsPlayed,
      audioStats.eventsSkippedVoice, audioStats.eventsMissingResource,
      static_cast<int>(firstPacketTick), static_cast<int>(soundOriginTick),
      audioStats.reverseRecoveries, audioStats.duplicateTicks,
      audioStats.mp3EventsSeen, audioStats.mp3EventsPlayed, audioStats.mp3EventsSkipped,
      audioStats.mp3EventsFailed, audioStats.mp3EventsInaudible, audioStats.eventsInaudible);
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
  // T54: the panel's browser must be gone before the process exits, and its
  // frame-time window is emitted here so the reading covers the whole run.
  {
    const std::string frameLine = tf2::native::logsPanelFrameLine();
    if (!frameLine.empty()) traceFmt("%s", frameLine.c_str());
    traceFmt("%s", tf2::native::logsPanelSummaryLine().c_str());
    traceFmt("%s", tf2::native::logsPanelRuntimeLine().c_str());
    tf2::native::logsPanelShutdown();
  }
  persistent.render = renderer.settings();
  if (assets.valid()) persistent.tfRoot = assets.tfDirectory;
  tf2::native::saveSettings(persistent);
  g_renderer = nullptr;
  return static_cast<int>(message.wParam);
}
