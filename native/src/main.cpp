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
#include "entity_model.h"
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

void traceFmt(const char* format, ...) {
  if (!g_traceFile) return;
  char buffer[192];
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

// Convert 3x4 poseToBone matrix to 4x4 for GPU upload
std::array<float, 16> poseToBoneToMatrix4x4(const std::array<float, 12>& poseToBone) {
  // poseToBone is stored as [row0(4), row1(4), row2(4)]
  // Convert to column-major 4x4 for HLSL
  return {
    poseToBone[0], poseToBone[4], poseToBone[8],  0.0f,
    poseToBone[1], poseToBone[5], poseToBone[9],  0.0f,
    poseToBone[2], poseToBone[6], poseToBone[10], 0.0f,
    poseToBone[3], poseToBone[7], poseToBone[11], 1.0f
  };
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
// the screen, the fields that appear in the status line, and the timeline range
// and position. `dpi`/`recentDemos`/`tfRoot` are deliberately absent -- nothing
// here renders them, so a change there must not force a refresh. dtors of the
// tuple members (std::string) are cheap next to a SendMessageW round trip.
bool uiControlsUnchanged(const tf2::native::UiSnapshot& state,
    const std::tuple<int, std::int32_t, std::int32_t, bool, bool, std::string,
      std::string, bool, std::uint32_t, std::string>& signature) {
  return std::get<0>(signature) == static_cast<int>(state.screen)
    && std::get<1>(signature) == state.tick
    && std::get<2>(signature) == state.ticks
    && std::get<3>(signature) == state.playing
    && std::get<4>(signature) == state.reverse
    && std::get<5>(signature) == state.mapName
    && std::get<6>(signature) == state.recordingType
    && std::get<7>(signature) == state.bspAvailable
    && std::get<8>(signature) == state.missingResourceCount
    && std::get<9>(signature) == state.error;
}

// Returns true when it actually pushed to the controls, false when the cached
// signature said there was nothing to push. Callers use the return value as the
// "work done" reading rather than counting the call itself.
bool updateNativeUiControls(HWND window) {
  if (!g_nativeUi) return false;
  const auto state = g_nativeUi->snapshot();
  // Cached signature of the previous push. A static here is safe: there is one
  // window, hence one call site, and the whole reason this function is called
  // every iteration is to keep the controls in step with the playback state.
  static std::tuple<int, std::int32_t, std::int32_t, bool, bool, std::string,
    std::string, bool, std::uint32_t, std::string> lastSignature{};
  static bool signatureValid = false;
  const std::tuple<int, std::int32_t, std::int32_t, bool, bool, std::string,
    std::string, bool, std::uint32_t, std::string> signature{
      static_cast<int>(state.screen), state.tick, state.ticks, state.playing,
      state.reverse, state.mapName, state.recordingType, state.bspAvailable,
      state.missingResourceCount, state.error};
  if (signatureValid && uiControlsUnchanged(state, lastSignature)) return false;
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
      + (state.reverse ? L" | reverse" : L"");
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
  g_uiStatus = make(L"STATIC", L"Opening: choose a .dem file", SS_LEFT, 0, 16, 12, 900, 28);
  g_uiOpen = make(L"BUTTON", L"Open Demo...", BS_PUSHBUTTON, kUiOpen, 16, 48, 120, 30);
  g_uiCancel = make(L"BUTTON", L"Cancel", BS_PUSHBUTTON, kUiCancel, 144, 48, 90, 30);
  g_uiConfirm = make(L"BUTTON", L"Import", BS_DEFPUSHBUTTON, kUiConfirm, 240, 48, 90, 30);
  g_uiPlay = make(L"BUTTON", L"Play/Pause", BS_PUSHBUTTON, kUiPlay, 16, 84, 110, 30);
  g_uiStop = make(L"BUTTON", L"Stop", BS_PUSHBUTTON, kUiStop, 132, 84, 80, 30);
  g_uiBack = make(L"BUTTON", L"< Tick", BS_PUSHBUTTON, kUiBack, 218, 84, 80, 30);
  g_uiForward = make(L"BUTTON", L"Tick >", BS_PUSHBUTTON, kUiForward, 304, 84, 80, 30);
  g_uiReverse = make(L"BUTTON", L"Reverse", BS_PUSHBUTTON, kUiReverse, 390, 84, 90, 30);
  g_uiTimeline = make(TRACKBAR_CLASSW, L"", TBS_AUTOTICKS | TBS_ENABLESELRANGE,
    kUiTimeline, 16, 122, 700, 30);
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
    return 0;
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
    // while a key is held, which otherwise flips state several times.
    if ((lParam & (1u << 30)) != 0u
        && (wParam == VK_SPACE || wParam == 'R' || wParam == VK_HOME || wParam == VK_END
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
      g_playback.paused = !g_playback.paused;
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
  auto persistent = tf2::native::loadSettings();
  tf2::native::NativeUiController nativeUi;
  nativeUi.setTfRoot(persistent.tfRoot);
  g_nativeUi = &nativeUi;
  DragAcceptFiles(window, TRUE);
  createNativeUiControls(window, instance);
  int argumentCount = 0;
  std::filesystem::path commandTfRoot;
  std::filesystem::path commandDemo;
  std::filesystem::path commandModel;
  UINT audioDevice = WAVE_MAPPER;
  bool audioDeviceRejected = false;
  bool startPaused = false;
  std::filesystem::path metricsPath;
  std::filesystem::path captureFramePath;
  std::filesystem::path dumpEntityMaterialsPath;
  std::int64_t captureTick = 0;
  bool captureTickRejected = false;
  LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
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
      } else if (wcscmp(arguments[i], L"--audio-device") == 0 && i + 1 < argumentCount) {
        wchar_t* end = nullptr;
        const unsigned long parsed = std::wcstoul(arguments[++i], &end, 10);
        if (end && *end == L'\0' && parsed < waveOutGetNumDevs()) audioDevice = static_cast<UINT>(parsed);
        else audioDeviceRejected = true;
      } else if (wcscmp(arguments[i], L"--audio-device") == 0) {
        audioDeviceRejected = true;
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
  std::int32_t firstPacketTick = 0;
  const auto firstPacket = std::find_if(demoIndex.entries.begin(), demoIndex.entries.end(),
    [](const auto& entry) { return entry.command == 1 || entry.command == 2; });
  if (firstPacket != demoIndex.entries.end()) firstPacketTick = firstPacket->tick;
  for (const auto& source : demoNetworkSummary.decodedSoundEvents) {
    if (source.resourceName.empty()) continue;
    tf2::native::ScheduledSound sound;
    sound.tick = std::max<std::int32_t>(0, source.tick - firstPacketTick);
    sound.soundIndex = source.soundIndex;
    sound.name = source.resourceName;
    sound.volume = source.volume;
    sound.delaySeconds = source.delaySeconds;
    sound.origin[0] = source.origin[0];
    sound.origin[1] = source.origin[1];
    sound.origin[2] = source.origin[2];
    tf2::native::scheduleGameplaySound(soundTimeline, std::move(sound), g_playback.tickRate);
  }
  soundTimeline.sortByTick();
  const tf2::native::SoundKindCounts soundKindCounts = tf2::native::countSoundKinds(soundTimeline);
  tf2::native::TempEffectTimeline effectTimeline;
  for (const auto& source : demoNetworkSummary.tempEntityEvents) {
    tf2::native::addFireBulletsEvent(source, effectTimeline);
    tf2::native::addExplosionEvent(source, effectTimeline);
    tf2::native::addParticleEvent(source, effectTimeline);
  }
  effectTimeline.sortByTick();
  tf2::native::AudioEventScheduler soundScheduler;
  soundScheduler.setDeviceId(audioDevice);
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
      + L" HUD[tick=" + std::to_wstring(g_playback.tick)
      + L" state=" + (g_playback.paused ? L"paused" : (g_playback.reverse ? L"reverse" : L"playing"))
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
      + L" posePreflight=" + (modelPosePreflight ? L"ready" : L"unknown")
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
    DestroyWindow(window);
    return 12;
  }
  renderer.setWorldMaterialParams(mapMaterialParams);
  tf2::native::ModelMetadata modelMetadata;
  bool modelReady = false;
  if (!commandModel.empty()) {
    auto inspection = tf2::native::ModelLoader::inspect(commandModel);
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
    if (modelReady && !modelMetadata.bones.empty()) {
      std::vector<std::array<float, 16>> boneMatrices;
      boneMatrices.reserve(modelMetadata.bones.size());
      bool poseMatricesValid = modelMetadata.bones.size() <= 128;
      for (const auto& bone : modelMetadata.bones) {
        if (!bone.poseToBoneValid) { poseMatricesValid = false; break; }
        boneMatrices.push_back(poseToBoneToMatrix4x4(bone.poseToBone));
      }
      // The MVP uploads normalized model vertices. poseToBone is an inverse
      // bind matrix, not a bind skin matrix, so applying it here would move a
      // static model into bone-local space. Keep the fallback in bind pose
      // until AnimationPlayer supplies animatedWorld * inverse(bindWorld).
      uploadedBones = poseMatricesValid && renderer.uploadBoneMatrices(boneMatrices, false);
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
  ShowWindow(window, showCommand);
  UpdateWindow(window);

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
      if (tf2::native::findObserverViewAtOrBeforeTick(demoNetworkSummary, sceneTick, observerView)
          && observerView.hasOrigin && observerView.hasAngles) {
        renderer.setObserverDemoViewWorld(observerView.origin[0], observerView.origin[1], observerView.origin[2],
          observerView.angles[0], observerView.angles[1]);
      } else {
        renderer.clearObserverDemoView();
      }
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
        draws.reserve(96);
        fallbackMarkers.reserve(32);
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
        renderer.setEntityModelInstances(draws);
        renderer.setEntityMarkers(fallbackMarkers);
        traceCheckpoint("scene: instances set");
      } else {
        renderer.setEntityModelInstances({});
        renderer.setEntityMarkers({});
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
  renderer.shutdown();
  persistent.render = renderer.settings();
  if (assets.valid()) persistent.tfRoot = assets.tfDirectory;
  tf2::native::saveSettings(persistent);
  g_renderer = nullptr;
  return static_cast<int>(message.wParam);
}
