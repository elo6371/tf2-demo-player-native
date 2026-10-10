#include "native_ui.h"

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

std::filesystem::path uiSettingsPath() {
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
  settings.fov = static_cast<float>(readUiInt(path, L"fov", 90));
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
  const auto mapPath = settings_.tfRoot / L"maps" /
    std::filesystem::path(std::wstring(header_.mapName.begin(), header_.mapName.end()) + L".bsp");
  bspAvailable_ = pathExists(mapPath);
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

} // namespace tf2::native
