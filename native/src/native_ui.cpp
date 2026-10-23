#include "native_ui.h"

#include "asset_root.h"
#include "map_lookup.h"
#include "vpk_archive.h"
#include "viewmodel.h"

#include <Windows.h>
#include <algorithm>
#include <cwctype>
#include <cmath>
#include <utility>

namespace tf2::native {
namespace {

constexpr std::int32_t kSecondStepTicks = 66;

bool isDemoPath(const std::filesystem::path& path) {
  if (path.empty() || !path.has_filename()) return false;
  auto extension = path.extension().wstring();
  std::transform(extension.begin(), extension.end(), extension.begin(),
    [](wchar_t character) { return static_cast<wchar_t>(::towlower(character)); });
  return extension == L".dem";
}

bool pathExists(const std::filesystem::path& path) {
  std::error_code error;
  return !path.empty() && std::filesystem::exists(path, error) && !error;
}

// Empty means "the user's real ui.ini". Set by setUiSettingsFileOverride, which
// exists for the same reason settings_store.h's override does: a gate whose
// expected value came out of %LOCALAPPDATA% is reading the machine, not the
// build -- and it would be WRITING the machine too.
std::filesystem::path g_uiSettingsFileOverride;

std::filesystem::path computeUiSettingsPath() {
  if (!g_uiSettingsFileOverride.empty()) return g_uiSettingsFileOverride;
  wchar_t* value = nullptr;
  std::size_t length = 0;
  if (_wdupenv_s(&value, &length, L"LOCALAPPDATA") != 0 || !value) return {};
  std::filesystem::path path = std::filesystem::path(value) / L"TF2 Demo Player" / L"ui.ini";
  free(value);
  std::error_code error;
  std::filesystem::create_directories(path.parent_path(), error);
  return error ? std::filesystem::path{} : path;
}



void writeUiInt(const std::filesystem::path& path, const wchar_t* key, int value) {
  wchar_t buffer[32]{};
  _snwprintf_s(buffer, _TRUNCATE, L"%d", value);
  WritePrivateProfileStringW(L"ui", key, buffer, path.c_str());
}

int readUiInt(const std::filesystem::path& path, const wchar_t* key, int fallback) {
  return GetPrivateProfileIntW(L"ui", key, fallback, path.c_str());
}

void loadUiSettings(UiSettings& settings) {
  const auto path = uiSettingsPath();
  if (path.empty()) return;
  settings.standardQuality = readUiInt(path, L"standardQuality", 0) != 0;
  settings.fov = static_cast<float>(migrateLegacyViewModelFov(readUiInt(path, L"fov", kDefaultViewModelFovInt)));
  settings.vsync = readUiInt(path, L"vsync", 0) != 0;
  settings.volume = static_cast<float>(readUiInt(path, L"volumePercent", 100)) / 100.0f;
  wchar_t root[32768]{};
  GetPrivateProfileStringW(L"ui", L"tfRoot", L"", root,
    static_cast<DWORD>(std::size(root)), path.c_str());
  if (root[0] != L'\0') settings.tfRoot = root;
  for (std::size_t index = 0; index < 8; ++index) {
    const std::wstring key = L"recentDemo" + std::to_wstring(index);
    wchar_t recent[32768]{};
    GetPrivateProfileStringW(L"ui", key.c_str(), L"", recent,
      static_cast<DWORD>(std::size(recent)), path.c_str());
    if (recent[0] != L'\0') settings.recentDemos.emplace_back(recent);
  }
}

} // namespace

// The public entry point, outside the anonymous namespace on purpose: main.cpp
// needs it to write [hud], and a second anonymous-namespace definition would
// shadow this declaration and link-fail instead of telling anyone why.
std::filesystem::path uiSettingsPath() { return computeUiSettingsPath(); }

void setUiSettingsFileOverride(const std::filesystem::path& file) {
  g_uiSettingsFileOverride = file;
  // The directory must exist before anything writes into it, and the override is
  // the one case where "the parent is there" cannot be assumed.
  if (!file.empty()) {
    std::error_code error;
    std::filesystem::create_directories(file.parent_path(), error);
  }
}

NativeUiController::NativeUiController() {
  loadUiSettings(settings_);
  settings_.fov = (std::clamp)(settings_.fov, 40.0f, 120.0f);
  settings_.volume = (std::clamp)(settings_.volume, 0.0f, 1.0f);
}

NativeUiController::~NativeUiController() {
  cancelImportRequested_.store(true, std::memory_order_release);
  if (importWorker_.joinable()) importWorker_.join();
}

void NativeUiController::setCallbacks(UiCallbacks callbacks) {
  callbacks_ = std::move(callbacks);
}

void NativeUiController::setSettings(const UiSettings& settings) {
  settings_ = settings;
  settings_.fov = (std::clamp)(settings_.fov, 40.0f, 120.0f);
  settings_.volume = (std::clamp)(settings_.volume, 0.0f, 1.0f);
  settings_.recentDemos = settings.recentDemos;
  refreshResourceState();
}

void NativeUiController::updateSettings(const UiSettings& settings) {
  setSettings(settings);
  if (screen_ == UiScreen::Settings) screen_ = settingsReturnScreen_;
}

bool NativeUiController::saveSettings() const {
  const auto path = uiSettingsPath();
  if (path.empty()) return false;
  writeUiInt(path, L"standardQuality", settings_.standardQuality ? 1 : 0);
  writeUiInt(path, L"fov", static_cast<int>(settings_.fov + 0.5f));
  writeUiInt(path, L"vsync", settings_.vsync ? 1 : 0);
  writeUiInt(path, L"volumePercent", static_cast<int>(settings_.volume * 100.0f + 0.5f));
  WritePrivateProfileStringW(L"ui", L"tfRoot", settings_.tfRoot.wstring().c_str(), path.c_str());
  for (std::size_t index = 0; index < 8; ++index) {
    const std::wstring key = L"recentDemo" + std::to_wstring(index);
    const std::wstring value = index < settings_.recentDemos.size()
      ? settings_.recentDemos[index].wstring() : L"";
    WritePrivateProfileStringW(L"ui", key.c_str(), value.c_str(), path.c_str());
  }
  return true;
}

void NativeUiController::setDisplayMetrics(unsigned dpi, unsigned clientWidth,
    unsigned clientHeight, bool fullscreen) {
  dpi_ = (std::max)(1u, dpi);
  clientWidth_ = clientWidth;
  clientHeight_ = clientHeight;
  fullscreen_ = fullscreen;
}

void NativeUiController::setTfRoot(const std::filesystem::path& root) {
  settings_.tfRoot = root;
  refreshResourceState();
}

bool NativeUiController::hasLoadedDemo() const {
  return header_.valid && !demoPath_.empty();
}

void NativeUiController::setError(std::string message) {
  error_ = std::move(message);
  screen_ = UiScreen::Error;
  playing_ = false;
}

bool NativeUiController::openDemo(const std::filesystem::path& path) {
  if (importWorker_.joinable()) {
    cancelImportRequested_.store(true, std::memory_order_release);
    importWorker_.join();
  }
  error_.clear();
  playing_ = false;
  reverse_ = false;
  tick_ = 0;
  if (!isDemoPath(path)) {
    setError("Demo file must have a .dem extension");
    return false;
  }
  DemoHeader header;
  if (!parseDemoHeaderFile(path, header)) {
    setError(header.error.empty() ? "Unable to read Demo header" : header.error);
    return false;
  }
  DemoIndex index;
  if (!indexDemoFile(path, header, index)) {
    setError(index.error.empty() ? "Unable to index Demo commands" : index.error);
    return false;
  }
  demoPath_ = path;
  header_ = std::move(header);
  index_ = std::move(index);
  screen_ = UiScreen::ImportReview;
  refreshResourceState();
  rememberDemo(path);
  saveSettings();
  importStatus_ = ImportStatus::Complete;
  importProgress_.store(100, std::memory_order_release);
  return true;
}

bool NativeUiController::openRecentDemo(std::size_t index) {
  if (index >= settings_.recentDemos.size()) return false;
  const auto path = settings_.recentDemos[index];
  std::error_code error;
  if (!std::filesystem::is_regular_file(path, error) || error) {
    settings_.recentDemos.erase(settings_.recentDemos.begin() + static_cast<std::ptrdiff_t>(index));
    saveSettings();
    return false;
  }
  return beginOpenDemo(path);
}

bool NativeUiController::beginOpenDemo(const std::filesystem::path& path) {
  if (!isDemoPath(path)) {
    setError("Demo file must have a .dem extension");
    importStatus_ = ImportStatus::Failed;
    return false;
  }
  if (importWorker_.joinable()) {
    cancelImportRequested_.store(true, std::memory_order_release);
    importWorker_.join();
  }
  error_.clear();
  demoPath_ = path;
  header_ = {};
  index_ = {};
  screen_ = UiScreen::Opening;
  playing_ = false;
  cancelImportRequested_.store(false, std::memory_order_release);
  importWorkerDone_.store(false, std::memory_order_release);
  importProgress_.store(1, std::memory_order_release);
  importStatus_ = ImportStatus::ReadingHeader;
  {
    std::lock_guard<std::mutex> lock(importMutex_);
    importResult_ = {};
  }
  importWorker_ = std::thread([this, path] {
    ImportResult result;
    DemoHeader header;
    if (cancelImportRequested_.load(std::memory_order_acquire)) {
      importWorkerDone_.store(true, std::memory_order_release);
      return;
    }
    if (!parseDemoHeaderFile(path, header)) {
      result.error = header.error.empty() ? "Unable to read Demo header" : header.error;
    } else {
      importProgress_.store(50, std::memory_order_release);
      if (cancelImportRequested_.load(std::memory_order_acquire)) {
      } else {
        DemoIndex index;
        if (!indexDemoFile(path, header, index)) {
          result.error = index.error.empty() ? "Unable to index Demo commands" : index.error;
        } else {
          result.header = std::move(header);
          result.index = std::move(index);
          result.ready = true;
        }
      }
    }
    {
      std::lock_guard<std::mutex> lock(importMutex_);
      importResult_ = std::move(result);
    }
    importWorkerDone_.store(true, std::memory_order_release);
  });
  return true;
}

bool NativeUiController::importActive() const {
  return !importWorkerDone_.load(std::memory_order_acquire);
}

void NativeUiController::pollImport() {
  if (importActive()) {
    if (cancelImportRequested_.load(std::memory_order_acquire)) {
      importStatus_ = ImportStatus::CancelRequested;
    } else if (importProgress_.load(std::memory_order_acquire) >= 50) {
      importStatus_ = ImportStatus::Indexing;
    }
    return;
  }
  if (!importWorker_.joinable() || importStatus_ == ImportStatus::Idle) return;
  importWorker_.join();
  finishImportIfReady();
}

void NativeUiController::finishImportIfReady() {
  ImportResult result;
  {
    std::lock_guard<std::mutex> lock(importMutex_);
    result = std::move(importResult_);
    importResult_ = {};
  }
  if (cancelImportRequested_.load(std::memory_order_acquire)) {
    importStatus_ = ImportStatus::Cancelled;
    screen_ = UiScreen::Opening;
    demoPath_.clear();
    header_ = {};
    index_ = {};
    error_.clear();
    importProgress_.store(0, std::memory_order_release);
    return;
  }
  if (!result.ready) {
    setError(result.error.empty() ? "Demo import failed" : std::move(result.error));
    importStatus_ = ImportStatus::Failed;
    importProgress_.store(0, std::memory_order_release);
    return;
  }
  header_ = std::move(result.header);
  index_ = std::move(result.index);
  screen_ = UiScreen::ImportReview;
  tick_ = 0;
  refreshResourceState();
  rememberDemo(demoPath_);
  saveSettings();
  importStatus_ = ImportStatus::Complete;
  importProgress_.store(100, std::memory_order_release);
}

bool NativeUiController::confirmImport() {
  if (screen_ != UiScreen::ImportReview || !hasLoadedDemo()) return false;
  screen_ = UiScreen::Player;
  tick_ = 0;
  playing_ = false;
  return true;
}

void NativeUiController::cancelImport() {
  if (importWorker_.joinable() && importStatus_ != ImportStatus::Complete
      && importStatus_ != ImportStatus::Failed && importStatus_ != ImportStatus::Cancelled) {
    cancelImportRequested_.store(true, std::memory_order_release);
    importStatus_ = ImportStatus::CancelRequested;
    return;
  }
  if (screen_ == UiScreen::ImportReview || screen_ == UiScreen::Error) {
    screen_ = UiScreen::Opening;
    demoPath_.clear();
    header_ = {};
    index_ = {};
    error_.clear();
    tick_ = 0;
    playing_ = false;
  }
}

void NativeUiController::clearError() {
  if (screen_ == UiScreen::Error) {
    error_.clear();
    screen_ = hasLoadedDemo() ? UiScreen::ImportReview : UiScreen::Opening;
  }
}

void NativeUiController::rememberDemo(const std::filesystem::path& path) {
  if (path.empty()) return;
  const auto normalized = path.lexically_normal();
  std::vector<std::filesystem::path> updated{normalized};
  for (const auto& recent : settings_.recentDemos) {
    if (recent.lexically_normal() != normalized && updated.size() < 8) updated.push_back(recent);
  }
  settings_.recentDemos = std::move(updated);
}

void NativeUiController::setPlaybackState(std::int32_t tick, bool playing,
    bool reverse, double speed) {
  if (!hasLoadedDemo() || screen_ != UiScreen::Player) return;
  tick_ = (std::clamp)(tick, std::int32_t(0), (std::max)(std::int32_t(0), header_.ticks));
  playing_ = playing;
  reverse_ = reverse;
  speed_ = (std::clamp)(speed, 0.125, 8.0);
}

void NativeUiController::command(UiCommand value, std::int32_t requestedTick) {
  if (callbacks_.command) callbacks_.command(value);
  if (value == UiCommand::OpenSettings) {
    settingsReturnScreen_ = screen_;
    screen_ = UiScreen::Settings;
    return;
  }
  if (value == UiCommand::OpenDiagnostics) return;
  if (value == UiCommand::Cancel) {
    if (screen_ == UiScreen::Settings) screen_ = settingsReturnScreen_;
    else cancelImport();
    return;
  }
  if (value == UiCommand::ConfirmImport) { confirmImport(); return; }
  if (value == UiCommand::ToggleHud) { hudVisible_ = !hudVisible_; return; }
  if (value == UiCommand::ToggleMute) { muted_ = !muted_; return; }
  if (value == UiCommand::ToggleFullscreen) { fullscreen_ = !fullscreen_; return; }
  if (value == UiCommand::Export) { return; }
  if (!hasLoadedDemo() || screen_ != UiScreen::Player) return;
  const auto endTick = (std::max)(std::int32_t(0), header_.ticks);
  switch (value) {
    case UiCommand::PlayPause: playing_ = !playing_; break;
    case UiCommand::Stop: playing_ = false; tick_ = 0; reverse_ = false; break;
    case UiCommand::StepBackward: playing_ = false; tick_ = (std::max)(std::int32_t(0), tick_ - 1); break;
    case UiCommand::StepForward: playing_ = false; tick_ = (std::min)(endTick, tick_ + 1); break;
    case UiCommand::StepSecondBackward: playing_ = false; tick_ = (std::max)(std::int32_t(0), tick_ - kSecondStepTicks); break;
    case UiCommand::StepSecondForward: playing_ = false; tick_ = (std::min)(endTick, tick_ + kSecondStepTicks); break;
    case UiCommand::Reverse: reverse_ = !reverse_; break;
    case UiCommand::Scrub: playing_ = false; tick_ = (std::clamp)(requestedTick, 0, endTick); break;
    case UiCommand::ResetCamera: break;
    default: break;
  }
}

void NativeUiController::refreshResourceState() {
  tfRootAvailable_ = pathExists(settings_.tfRoot);
  bspAvailable_ = false;
  missingResourceCount_ = 0;
  if (!hasLoadedDemo()) return;
  const auto plan = planMapLookup(header_.mapName);
  if (plan.nameUsable) {
    const auto root = AssetRoot::fromPath(settings_.tfRoot);
    for (const auto& candidate : plan.candidates) {
      if (!candidate.packed) {
        if (pathExists(settings_.tfRoot / std::filesystem::path(candidate.relativePath))) {
          bspAvailable_ = true;
          break;
        }
        continue;
      }
      std::vector<std::filesystem::path> archives;
      root.archives().collectContaining(candidate.relativePath, archives);
      if (!archives.empty()) {
        bspAvailable_ = true;
        break;
      }
    }
  }
  if (!tfRootAvailable_) ++missingResourceCount_;
  if (!bspAvailable_) ++missingResourceCount_;
}

UiSnapshot NativeUiController::snapshot() const {
  UiSnapshot result;
  result.screen = screen_;
  result.demoPath = demoPath_;
  result.mapName = header_.mapName;
  result.recordingType = demoRecordingTypeName(header_.recordingType);
  result.ticks = header_.ticks;
  result.durationSeconds = header_.playbackTime;
  result.tick = tick_;
  result.playing = playing_;
  result.reverse = reverse_;
  result.hudVisible = hudVisible_;
  result.muted = muted_;
  result.fullscreen = fullscreen_;
  result.bspAvailable = bspAvailable_;
  result.tfRootAvailable = tfRootAvailable_;
  result.missingResourceCount = missingResourceCount_;
  result.error = error_;
  result.speed = speed_;
  result.fov = settings_.fov;
  result.standardQuality = settings_.standardQuality;
  result.vsync = settings_.vsync;
  result.volume = settings_.volume;
  result.importStatus = importStatus_;
  result.importProgress = importProgress_.load(std::memory_order_acquire);
  result.dpi = dpi_;
  result.clientWidth = clientWidth_;
  result.clientHeight = clientHeight_;
  result.dpiScale = static_cast<float>(dpi_) / 96.0f;
  result.recentDemos = settings_.recentDemos;
  result.tfRoot = settings_.tfRoot;
  return result;
}

const char* uiScreenName(UiScreen screen) {
  switch (screen) {
    case UiScreen::Opening: return "opening";
    case UiScreen::ImportReview: return "import-review";
    case UiScreen::Player: return "player";
    case UiScreen::Settings: return "settings";
    case UiScreen::Error: return "error";
    default: return "unknown";
  }
}

const char* cameraModeName(CameraMode mode) {
  switch (mode) {
    case CameraMode::FirstPerson: return "first-person";
    case CameraMode::ThirdPerson: return "third-person";
    case CameraMode::FreeLook: return "free-look";
    default: return "unknown";
  }
}

bool CameraModeController::setMode(CameraMode mode) {
  if (mode == mode_) return false;
  mode_ = mode;
  ++modeChanges_;
  return true;
}

bool CameraModeController::cycleMode() {
  switch (mode_) {
    case CameraMode::FirstPerson: return setMode(CameraMode::ThirdPerson);
    case CameraMode::ThirdPerson: return setMode(CameraMode::FreeLook);
    case CameraMode::FreeLook: return setMode(CameraMode::FirstPerson);
    default: return false;
  }
}

bool CameraModeController::spacePress(std::uint32_t nowMs) {
  ++spacePresses_;
  bool isDouble = false;
  if (haveLastPress_) {
    const std::uint32_t interval = nowMs - lastPressMs_;
    lastIntervalMs_ = interval;
    // Strictly inside the window counts as the double; the boundary itself
    // (interval == window) is a fresh single. Both sides of that boundary are
    // probed, which is what makes the window a measured constant.
    isDouble = interval < kDoubleClickWindowMs;
  } else {
    lastIntervalMs_ = 0xFFFFFFFFu;
  }
  haveLastPress_ = true;
  lastPressMs_ = nowMs;
  lastWasDouble_ = isDouble;
  if (isDouble) {
    ++spaceDoubles_;
    // The double click's job: straight into free-look, the mode the player
    // cannot reach in one step from first-person. From third-person it is a
    // no-op on the mode (still counted); the return value answers "did the
    // mode change", so a double landing on free-look already reports false.
    return setMode(CameraMode::FreeLook);
  }
  ++spaceSingles_;
  return cycleMode();
}

CameraSceneAction decideCameraSceneAction(CameraMode mode, bool haveObserverView) {
  switch (mode) {
    case CameraMode::FirstPerson:
      return haveObserverView ? CameraSceneAction::SetDemoView : CameraSceneAction::HoldCurrent;
    case CameraMode::ThirdPerson:
      return haveObserverView ? CameraSceneAction::SetOrbitFocus : CameraSceneAction::HoldCurrent;
    case CameraMode::FreeLook:
      return CameraSceneAction::FreezeCurrent;
    default:
      return CameraSceneAction::HoldCurrent;
  }
}

namespace {

int scaleDimension(int value, float dpiScale) {
  // Round-half-up so a 1.25 scale does not silently floor every row height.
  return static_cast<int>(static_cast<float>(value) * dpiScale + 0.5f);
}

} // namespace

UiLayoutPlan planUiLayout(int clientWidth, int clientHeight, float dpiScale) {
  UiLayoutPlan plan;
  if (clientWidth <= 0 || clientHeight <= 0 || !(dpiScale > 0.0f) || !std::isfinite(dpiScale)) {
    return plan;
  }
  plan.valid = true;
  // Physical margins: 16 px at 96 dpi, scaled with the dpi.
  const int margin = scaleDimension(16, dpiScale);
  const int gap = scaleDimension(6, dpiScale);
  const int usableWidth = (std::max)(0, clientWidth - 2 * margin);

  // The fixed rows, at their 96-dpi geometry scaled by dpi. Rows are
  // independent, so a clamp in one row cannot push another row out of place.
  struct RowSpec {
    UiControlId first;
    int count;
    int baseY;
    int baseH;
    int widths[5];
  };
  const RowSpec rows[5] = {
    { UiControlId::Open, 3, 48, 30, { 120, 90, 90, 0, 0 } },
    { UiControlId::Play, 5, 84, 30, { 110, 80, 80, 80, 90 } },
    { UiControlId::Timeline, 1, 122, 30, { 700, 0, 0, 0, 0 } },
    // T50: the camera row -- free-fly, reset-to-player, reset-view -- at the
    // same geometry rules as the rows above. The widths are 96-dpi numbers the
    // dpi scale multiplies, and every rect is still clamped inside the client
    // by the loop below, so the 42-case probe covers them unchanged.
    { UiControlId::FreeFly, 3, 158, 30, { 90, 90, 90, 0, 0 } },
    // The bookmark row: save, the name text field (wide enough to type into),
    // delete, the slot cycler, and the restore-default-keys button.
    { UiControlId::BookmarkSave, 5, 194, 28, { 90, 240, 90, 110, 110 } },
  };
  // The camera-mode readout is a label, not a control: it sits at the end of
  // the camera row, in the space the three buttons above leave. Planned here
  // rather than left unplanned because an unplanned rect is exactly what "every
  // rect inside the client, no two overlapping" cannot be asserted about, and
  // the probe walks every rect in the array.
  {
    auto& modeRect = plan.rects[static_cast<std::size_t>(UiControlId::CameraMode)];
    int x = margin;
    for (int index = 0; index < 3; ++index) {
      x += scaleDimension(90, dpiScale) + gap;
    }
    modeRect.x = (std::min)(x, clientWidth);
    modeRect.y = (std::min)(scaleDimension(158, dpiScale), clientHeight);
    modeRect.h = (std::min)(scaleDimension(30, dpiScale),
      (std::max)(0, clientHeight - modeRect.y));
    modeRect.w = (std::max)(0, (std::min)(scaleDimension(200, dpiScale),
      (std::max)(0, clientWidth - margin - modeRect.x)));
  }
  for (const auto& row : rows) {
    // A row whose scaled y lands past the bottom has no room: it collapses to
    // a zero-height rect pinned at the client's bottom edge, which stays
    // inside the client and cannot overlap the rows above it.
    int y = scaleDimension(row.baseY, dpiScale);
    const int h = scaleDimension(row.baseH, dpiScale);
    if (y > clientHeight) y = clientHeight;
    int x = margin;
    for (int index = 0; index < row.count; ++index) {
      auto& rect = plan.rects[static_cast<std::size_t>(row.first) + static_cast<std::size_t>(index)];
      const int wanted = scaleDimension(row.widths[index], dpiScale);
      // The cursor itself is clamped: once a squeezed row runs out of room,
      // the remaining controls collapse to zero width pinned at the client's
      // right edge instead of marching past it (gap keeps adding up).
      const int cx = (std::min)(x, clientWidth);
      const int room = (std::max)(0, clientWidth - margin - cx);
      // A control never leaves the client and never intrudes on the next
      // one's start position; the floor keeps a squeezed control grabbable.
      rect.x = cx;
      rect.y = y;
      rect.h = (std::min)(h, (std::max)(0, clientHeight - y));
      rect.w = (std::clamp)(wanted, (std::min)(24, room), room);
      x = cx + rect.w + gap;
    }
  }
  // The status line spans the window between the margins: text can only clip
  // at its own edge, never run over the buttons below (different row) or past
  // the window (clamped).
  {
    auto& status = plan.rects[static_cast<std::size_t>(UiControlId::Status)];
    status.x = margin;
    status.y = scaleDimension(12, dpiScale);
    if (status.y > clientHeight) status.y = clientHeight;
    status.h = (std::min)(scaleDimension(28, dpiScale),
      (std::max)(0, clientHeight - status.y));
    status.w = usableWidth;
  }
  return plan;
}

bool uiRectsOverlap(const UiRect& a, const UiRect& b) {
  // Strict interiors: sharing exactly one edge pixel counts as overlapping so
  // a layout that merely touches is not waved through.
  return a.x < b.x + b.w && b.x < a.x + a.w
    && a.y < b.y + b.h && b.y < a.y + a.h;
}

UiHudLayoutPlan planHudLayout(const UiLayoutPlan& base, int clientWidth,
    int clientHeight, float dpiScale) {
  UiHudLayoutPlan plan;
  if (!base.valid || clientWidth <= 0 || clientHeight <= 0 || !(dpiScale > 0.0f)
      || !std::isfinite(dpiScale)) {
    return plan;
  }
  plan.valid = true;
  // The row sits below the lowest base rect. Taking the maximum over every rect
  // rather than adding a constant to Timeline is what keeps this correct when
  // T50 adds ids of its own: the HUD row follows whatever is lowest, not what
  // the base plan happened to look like on the day this was written.
  int lowest = 0;
  for (const auto& rect : base.rects) {
    const int bottom = rect.y + rect.h;
    if (bottom > lowest) lowest = bottom;
  }
  const int margin = scaleDimension(16, dpiScale);
  const int gap = scaleDimension(6, dpiScale);
  int y = lowest + gap;
  const int h = scaleDimension(30, dpiScale);
  if (y > clientHeight) y = clientHeight;
  const int heights = (std::min)(h, (std::max)(0, clientHeight - y));
  // Widths at 96 dpi: import button, preview toggle, scale slider, reset, then a
  // status line that takes whatever is left of the row.
  const int widths[static_cast<std::size_t>(UiHudControlId::Count)] = {
    scaleDimension(130, dpiScale), scaleDimension(120, dpiScale),
    scaleDimension(160, dpiScale), scaleDimension(140, dpiScale), 0,
  };
  int x = margin;
  for (int index = 0; index < static_cast<int>(UiHudControlId::Count); ++index) {
    auto& rect = plan.rects[static_cast<std::size_t>(index)];
    const int cx = (std::min)(x, clientWidth);
    const int room = (std::max)(0, clientWidth - margin - cx);
    rect.x = cx;
    rect.y = y;
    rect.h = heights;
    const bool isStatus = index == static_cast<int>(UiHudControlId::Status);
    const int wanted = isStatus ? room : widths[index];
    rect.w = (std::clamp)(wanted, (std::min)(isStatus ? 0 : 24, room), room);
    x = cx + rect.w + gap;
  }
  return plan;
}
std::string composePlaybackHudLine(const PlaybackHudLine& fields) {
  auto squash = [](std::string value) {
    for (char& character : value) {
      if (character == '\r' || character == '\n' || character == '\t') character = ' ';
    }
    return value;
  };
  std::string line = "tick=" + std::to_string(fields.tick);
  line += " demo=" + squash(fields.demoName.empty() ? "-" : fields.demoName);
  line += " state=" + squash(fields.state.empty() ? "-" : fields.state);
  line += " camera=" + squash(fields.camera.empty() ? "-" : fields.camera);
  line += " error=" + squash(fields.error.empty() ? "none" : fields.error);
  return line;
}

} // namespace tf2::native
