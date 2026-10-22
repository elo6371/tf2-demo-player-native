#pragma once

#include "camera_director.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace tf2::native {

// ---------------------------------------------------------------------------
// Named camera bookmarks (T50).
//
// The product already had eight numeric camera presets in the renderer, saved by
// Ctrl+F1..F8 and restored by F1..F8. Two of the task's acceptance steps cannot
// be met by those: a bookmark has a NAME ("tower", "spawn door") that survives a
// restart, and the pose has to come back exactly -- not approximately, because
// "the frame is identical after a jump" is a claim a gate makes byte for byte.
// So the store lives here, the renderer keeps the transient presets, and the
// settings file is the only durable copy.
//
// The file format is contract 1.1: settings.ini [camera], keys bookmarkN.*, with
// the pose written by formatPose (%.9g fields) so a save/jump/save round trip is
// byte-identical. An empty slot keeps saved=0 and no pose, which is different
// from a slot holding the default pose.
// ---------------------------------------------------------------------------

constexpr std::size_t kCameraBookmarkSlots = 8;

struct CameraBookmark {
  bool saved = false;
  std::string name;
  CameraPose pose;
  // The camera mode active at save time, so a jump restores the view the user
  // saved and not only the numbers. Empty on a slot that was never saved.
  std::string mode;
  std::int32_t tick = -1;
};

// The name a slot falls back to when the user saves without typing one:
// "Bookmark 3". Spelled once so the UI, the trace and the gate agree.
std::string defaultBookmarkName(std::size_t slot);

class CameraBookmarkStore {
public:
  static constexpr std::size_t slotCount() { return kCameraBookmarkSlots; }

  // slot is 1-based, matching the key names (contract 1.1). Out-of-range slots
  // are refused rather than clamped: a bookmark the user cannot see is worse
  // than a refused save.
  bool save(std::size_t slot, const std::string& name, const CameraPose& pose,
    const std::string& mode, std::int32_t tick);
  bool remove(std::size_t slot);
  bool rename(std::size_t slot, const std::string& name);
  // Wipes every slot in memory. Deliberately separate from 'remove': "clear all
  // bookmarks" is a reset the user asked for, while a jump that misses must not
  // silently empty anything.
  void clearAll();

  const CameraBookmark& at(std::size_t slot) const;
  bool saved(std::size_t slot) const;
  std::size_t savedCount() const;
  // The longest name, in characters, so a UI can size its text field from the
  // content it already has instead of guessing a width.
  std::size_t longestName() const;

  // settings.ini [camera]. loadFromSettings REPLACES the in-memory store with
  // what the file says, so a missing file is an empty store rather than a
  // partial one. saveToSettings writes all eight slots, empty ones included, so
  // deleting a bookmark is durable.
  bool loadFromSettings(const std::filesystem::path& file);
  bool saveToSettings(const std::filesystem::path& file) const;
  static bool writeSlotKey(std::size_t slot, const char* field, std::wstring& out);

  std::uint32_t saveCount() const { return saveCount_; }
  std::uint32_t deleteCount() const { return deleteCount_; }
  std::uint32_t jumpCount() const { return jumpCount_; }
  std::uint32_t nameChangeCount() const { return nameChangeCount_; }
  std::uint32_t refusedCount() const { return refusedCount_; }
  void noteJump() { ++jumpCount_; }

private:
  CameraBookmark slots_[kCameraBookmarkSlots]{};
  std::uint32_t saveCount_ = 0;
  std::uint32_t deleteCount_ = 0;
  std::uint32_t jumpCount_ = 0;
  std::uint32_t nameChangeCount_ = 0;
  std::uint32_t refusedCount_ = 0;
};

} // namespace tf2::native
