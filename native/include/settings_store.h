#pragma once

#include "native_renderer.h"

#include <filesystem>

namespace tf2::native {

struct PersistentSettings {
  RenderSettings render;
  std::filesystem::path tfRoot;
};

std::filesystem::path settingsPath();
PersistentSettings loadSettings();
bool saveSettings(const PersistentSettings& settings);

} // namespace tf2::native
