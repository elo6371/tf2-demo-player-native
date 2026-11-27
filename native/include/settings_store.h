#pragma once

#include "native_renderer.h"

#include <filesystem>

namespace tf2::native {

struct PersistentSettings {
  RenderSettings render;
  // Owner 2026-11-26 20:15 (lead): whether this run ASKED for the two texture keys
  // -- the command line passed the switch, or the file being loaded already carried
  // the key. Only an asked-for key is written back. Deriving "asked" from the value
  // is what made a `--quality standard` run materialise `textureMipBias=0`: the
  // preset rewrites the field, the exit path saves the renderer's copy, and a
  // comparison against the compiled default sees a difference the run never asked
  // for. Comparing against the preset's default instead would just move the leak to
  // a legacy file whose `preset=1` has no mip key.
  bool textureMipBiasGiven = false;
  bool textureAtlasMaxGiven = false;
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
