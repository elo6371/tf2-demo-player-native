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
  std::cout << "PASS native_ui_probe screen=opening->import-review->player->opening"
    << " scrub=clamped corrupt=recoverable\n";
  return 0;
}
