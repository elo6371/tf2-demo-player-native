#include "native_ui.h"

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

} // namespace

NativeUiController::NativeUiController() = default;

void NativeUiController::setCallbacks(UiCallbacks callbacks) {
  callbacks_ = std::move(callbacks);
}

void NativeUiController::setSettings(const UiSettings& settings) {
  settings_ = settings;
  settings_.fov = std::clamp(settings_.fov, 40.0f, 120.0f);
  settings_.volume = std::clamp(settings_.volume, 0.0f, 1.0f);
  refreshResourceState();
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
  return true;
}

bool NativeUiController::confirmImport() {
  if (screen_ != UiScreen::ImportReview || !hasLoadedDemo()) return false;
  screen_ = UiScreen::Player;
  tick_ = 0;
  playing_ = false;
  return true;
}

void NativeUiController::cancelImport() {
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

void NativeUiController::setPlaybackState(std::int32_t tick, bool playing,
    bool reverse, double speed) {
  if (!hasLoadedDemo() || screen_ != UiScreen::Player) return;
  tick_ = std::clamp(tick, 0, std::max<std::int32_t>(0, header_.ticks));
  playing_ = playing;
  reverse_ = reverse;
  speed_ = std::clamp(speed, 0.125, 8.0);
}

void NativeUiController::command(UiCommand value, std::int32_t requestedTick) {
  if (callbacks_.command) callbacks_.command(value);
  if (value == UiCommand::OpenSettings) { screen_ = UiScreen::Settings; return; }
  if (value == UiCommand::OpenDiagnostics) return;
  if (value == UiCommand::Cancel) { cancelImport(); return; }
  if (value == UiCommand::ConfirmImport) { confirmImport(); return; }
  if (value == UiCommand::ToggleHud) { hudVisible_ = !hudVisible_; return; }
  if (value == UiCommand::ToggleMute) { muted_ = !muted_; return; }
  if (value == UiCommand::ToggleFullscreen) { fullscreen_ = !fullscreen_; return; }
  if (value == UiCommand::Export) { return; }
  if (!hasLoadedDemo() || screen_ != UiScreen::Player) return;
  const auto endTick = std::max<std::int32_t>(0, header_.ticks);
  switch (value) {
    case UiCommand::PlayPause: playing_ = !playing_; break;
    case UiCommand::Stop: playing_ = false; tick_ = 0; reverse_ = false; break;
    case UiCommand::StepBackward: playing_ = false; tick_ = std::max<std::int32_t>(0, tick_ - 1); break;
    case UiCommand::StepForward: playing_ = false; tick_ = std::min(endTick, tick_ + 1); break;
    case UiCommand::StepSecondBackward: playing_ = false; tick_ = std::max<std::int32_t>(0, tick_ - kSecondStepTicks); break;
    case UiCommand::StepSecondForward: playing_ = false; tick_ = std::min(endTick, tick_ + kSecondStepTicks); break;
    case UiCommand::Reverse: reverse_ = !reverse_; break;
    case UiCommand::Scrub: playing_ = false; tick_ = std::clamp(requestedTick, 0, endTick); break;
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
