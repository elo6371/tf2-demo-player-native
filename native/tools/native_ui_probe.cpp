#include "native_ui.h"

#include <Windows.h>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <algorithm>

namespace {
std::filesystem::path findDemo() {
  const std::filesystem::path candidates[] = {
    L"D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/demos/autorecord_2026-09-30_23-06-57.dem",
    L"C:/Users/Administrator/Downloads/0f55b29be497e7ba673a1a888b252a78_matcha-20260808-1349-pl_upward_f12.dem",
  };
  for (const auto& path : candidates) if (std::filesystem::exists(path)) return path;
  return {};
}
}

int main() {
  using namespace tf2::native;
  wchar_t* previousLocalAppData = nullptr;
  std::size_t previousLength = 0;
  _wdupenv_s(&previousLocalAppData, &previousLength, L"LOCALAPPDATA");
  const auto isolatedSettingsRoot = std::filesystem::temp_directory_path() / L"tf2-native-ui-probe-settings";
  std::error_code cleanupError;
  std::filesystem::remove_all(isolatedSettingsRoot, cleanupError);
  _wputenv_s(L"LOCALAPPDATA", isolatedSettingsRoot.wstring().c_str());
  NativeUiController ui;
  assert(ui.snapshot().screen == UiScreen::Opening);
  int callbackCount = 0;
  ui.setCallbacks(UiCallbacks{[&](UiCommand) { ++callbackCount; }});

  const auto demo = findDemo();
  if (demo.empty()) {
    std::cout << "SKIP native_ui_probe: no real .dem fixture\n";
    return 0;
  }
  assert(ui.openDemo(demo));
  auto review = ui.snapshot();
  assert(review.screen == UiScreen::ImportReview);
  assert(review.ticks >= 0 && !review.mapName.empty());
  ui.cancelImport();
  assert(ui.snapshot().screen == UiScreen::Opening);
  assert(ui.openDemo(demo));
  review = ui.snapshot();
  assert(ui.confirmImport());
  assert(ui.snapshot().screen == UiScreen::Player);
  ui.command(UiCommand::OpenSettings);
  assert(ui.snapshot().screen == UiScreen::Settings);
  ui.command(UiCommand::Cancel);
  assert(ui.snapshot().screen == UiScreen::Player);
  ui.setDisplayMetrics(144, 2560, 1440, true);
  assert(ui.snapshot().dpi == 144 && ui.snapshot().clientWidth == 2560
    && ui.snapshot().clientHeight == 1440 && ui.snapshot().dpiScale == 1.5f);
  ui.command(UiCommand::ToggleFullscreen);
  assert(ui.snapshot().fullscreen);
  ui.command(UiCommand::ToggleMute);
  assert(ui.snapshot().muted);
  UiSettings changed = ui.settings();
  changed.standardQuality = true;
  changed.fov = 95.0f;
  changed.vsync = true;
  changed.volume = 0.35f;
  changed.tfRoot = std::filesystem::temp_directory_path() / L"tf2-root-probe";
  ui.updateSettings(changed);
  assert(ui.snapshot().screen == UiScreen::Player);
  assert(ui.snapshot().fov == 95.0f && ui.snapshot().standardQuality
    && ui.snapshot().vsync && ui.snapshot().volume == 0.35f
    && ui.snapshot().tfRoot == changed.tfRoot);
  assert(ui.saveSettings());
  NativeUiController reloaded;
  assert(reloaded.settings().standardQuality && reloaded.settings().vsync);
  assert(reloaded.settings().fov == 95.0f);
  assert(reloaded.settings().volume == 0.35f);
  assert(reloaded.settings().tfRoot == changed.tfRoot);
  ui.command(UiCommand::StepForward);
  assert(ui.snapshot().tick == 1 || review.ticks == 0);
  ui.command(UiCommand::Scrub, review.ticks + 100);
  assert(ui.snapshot().tick == review.ticks);
  ui.command(UiCommand::ToggleHud);
  assert(!ui.snapshot().hudVisible);
  ui.command(UiCommand::PlayPause);
  assert(callbackCount > 0 && ui.snapshot().playing);
  ui.command(UiCommand::Stop);
  assert(ui.snapshot().tick == 0 && !ui.snapshot().playing);
  ui.cancelImport();
  assert(ui.snapshot().screen == UiScreen::Player);

  NativeUiController corrupt;
  const auto path = std::filesystem::temp_directory_path() / L"tf2-native-ui-corrupt.dem";
  { std::ofstream file(path, std::ios::binary); file << "bad"; }
  assert(!corrupt.openDemo(path));
  assert(corrupt.snapshot().screen == UiScreen::Error);
  assert(!corrupt.snapshot().error.empty());
  corrupt.cancelImport();
  assert(corrupt.snapshot().screen == UiScreen::Opening);

  NativeUiController background;
  assert(background.beginOpenDemo(demo));
  for (int attempt = 0; attempt < 100 && background.importActive(); ++attempt) {
    background.pollImport();
    Sleep(1);
  }
  background.pollImport();
  assert(!background.importActive());
  assert(background.snapshot().screen == UiScreen::ImportReview);
  assert(background.snapshot().importStatus == ImportStatus::Complete);
  assert(background.snapshot().importProgress == 100);
  const auto recent = background.snapshot().recentDemos;
  assert(std::find(recent.begin(), recent.end(), demo.lexically_normal()) != recent.end());
  NativeUiController recentUi;
  assert(recentUi.openRecentDemo(0));
  for (int attempt = 0; attempt < 100 && recentUi.importActive(); ++attempt) {
    recentUi.pollImport();
    Sleep(1);
  }
  recentUi.pollImport();
  assert(recentUi.snapshot().screen == UiScreen::ImportReview);
  assert(!recentUi.openRecentDemo(99));

  NativeUiController cancelled;
  assert(cancelled.beginOpenDemo(demo));
  cancelled.cancelImport();
  for (int attempt = 0; attempt < 100 && cancelled.importActive(); ++attempt) {
    cancelled.pollImport();
    Sleep(1);
  }
  cancelled.pollImport();
  assert(cancelled.snapshot().importStatus == ImportStatus::Cancelled);
  assert(cancelled.snapshot().screen == UiScreen::Opening);
  std::error_code error;
  std::filesystem::remove(path, error);
  _wputenv_s(L"LOCALAPPDATA", previousLocalAppData ? previousLocalAppData : L"");
  if (previousLocalAppData) free(previousLocalAppData);
  std::filesystem::remove_all(isolatedSettingsRoot, cleanupError);
  std::cout << "PASS native_ui_probe screen=opening->import-review->player->opening"
    << " scrub=clamped corrupt=recoverable\n";
  return 0;
}
