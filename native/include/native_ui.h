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

const char* uiScreenName(UiScreen screen);

} // namespace tf2::native
