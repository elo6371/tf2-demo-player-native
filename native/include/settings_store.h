#pragma once

#include "native_renderer.h"

#include <filesystem>

namespace tf2::native {

struct PersistentSettings {
  RenderSettings render;
  std::filesystem::path tfRoot;
  // T50: the two sections this struct does NOT own but must not destroy. They
  // live in the same file, and saveSettings rewrites the file through a
  // temporary, so a writer that only knew [renderer] deleted [camera] and [keys]
  // on every exit. saveSettings now carries every other section across; this
  // comment is here so the next reader does not add a third file for the same
  // reason.
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
