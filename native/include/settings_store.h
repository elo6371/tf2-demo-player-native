#pragma once

#include "native_renderer.h"

#include <filesystem>

namespace tf2::native {

struct PersistentSettings {
  RenderSettings render;
  std::filesystem::path tfRoot;
};

std::filesystem::path settingsPath();
// Read settings from, and write them to, this path for the whole process. For
// callers that must not touch the user's real settings file: an acceptance run
// pins its own FOV contract, and a gate whose expected value came out of
// `%LOCALAPPDATA%` would be reading the machine rather than the build. Passing a
// path that does not exist is the useful case -- it makes the compiled defaults
// the input. Must be set before `loadSettings()`; a later call changes where the
// exit-time save goes and nothing else.
void setSettingsFileOverride(const std::filesystem::path& file);
PersistentSettings loadSettings();
bool saveSettings(const PersistentSettings& settings);

} // namespace tf2::native
