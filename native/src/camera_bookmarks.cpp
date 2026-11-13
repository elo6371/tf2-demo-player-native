#include "camera_bookmarks.h"

#include "ini_section.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace tf2::native {

namespace {

const std::wstring kEmpty;

std::wstring slotKey(std::size_t slot, const char* field) {
  std::wstring key = L"bookmark" + std::to_wstring(slot) + L".";
  std::wstring wide;
  utf8ToWide(field, wide);
  key += wide;
  return key;
}

std::int32_t readInt(const IniSection& section, const std::wstring& key, std::int32_t fallback) {
  const std::wstring& value = iniSectionValue(section, key, kEmpty);
  if (value.empty()) return fallback;
  wchar_t* end = nullptr;
  const long parsed = std::wcstol(value.c_str(), &end, 10);
  if (!end || *end != L'\0') return fallback;
  return static_cast<std::int32_t>(parsed);
}

std::string readString(const IniSection& section, const std::wstring& key) {
  std::string result;
  if (!wideToUtf8(iniSectionValue(section, key, kEmpty), result)) return {};
  return result;
}

} // namespace

std::string defaultBookmarkName(std::size_t slot) {
  return "Bookmark " + std::to_string(slot);
}

bool CameraBookmarkStore::writeSlotKey(std::size_t slot, const char* field, std::wstring& out) {
  if (slot < 1 || slot > kCameraBookmarkSlots) return false;
  out = slotKey(slot, field);
  return true;
}

bool CameraBookmarkStore::save(std::size_t slot, const std::string& name,
    const CameraPose& pose, const std::string& mode, std::int32_t tick, float fov) {
  if (slot < 1 || slot > kCameraBookmarkSlots || !pose.valid) {
    ++refusedCount_;
    return false;
  }
  CameraBookmark& bookmark = slots_[slot - 1];
  const std::string finalName = name.empty() ? defaultBookmarkName(slot) : name;
  if (finalName != bookmark.name) ++nameChangeCount_;
  bookmark.saved = true;
  bookmark.name = finalName;
  bookmark.pose = pose;
  // The same clamp Renderer::setViewModelFov applies, so the stored value is
  // the value a jump can actually restore (a stored 500 would come back 120 and
  // the exactness assertion would rightly fail).
  bookmark.fov = std::isfinite(fov) ? std::clamp(fov, 40.0f, 120.0f) : kDefaultBookmarkFov;
  bookmark.mode = mode.empty() ? "first-person" : mode;
  bookmark.tick = tick;
  ++saveCount_;
  return true;
}

bool CameraBookmarkStore::remove(std::size_t slot) {
  if (slot < 1 || slot > kCameraBookmarkSlots) { ++refusedCount_; return false; }
  if (!slots_[slot - 1].saved) { ++refusedCount_; return false; }
  slots_[slot - 1] = CameraBookmark{};
  ++deleteCount_;
  return true;
}

bool CameraBookmarkStore::rename(std::size_t slot, const std::string& name) {
  if (slot < 1 || slot > kCameraBookmarkSlots) { ++refusedCount_; return false; }
  CameraBookmark& bookmark = slots_[slot - 1];
  if (!bookmark.saved) { ++refusedCount_; return false; }
  const std::string finalName = name.empty() ? defaultBookmarkName(slot) : name;
  if (finalName != bookmark.name) ++nameChangeCount_;
  bookmark.name = finalName;
  return true;
}

void CameraBookmarkStore::clearAll() {
  for (auto& slot : slots_) slot = CameraBookmark{};
}

const CameraBookmark& CameraBookmarkStore::at(std::size_t slot) const {
  static const CameraBookmark empty;
  if (slot < 1 || slot > kCameraBookmarkSlots) return empty;
  return slots_[slot - 1];
}

bool CameraBookmarkStore::saved(std::size_t slot) const {
  return slot >= 1 && slot <= kCameraBookmarkSlots && slots_[slot - 1].saved;
}

std::size_t CameraBookmarkStore::savedCount() const {
  return static_cast<std::size_t>(std::count_if(slots_, slots_ + kCameraBookmarkSlots,
    [](const CameraBookmark& bookmark) { return bookmark.saved; }));
}

std::size_t CameraBookmarkStore::longestName() const {
  std::size_t longest = 0;
  for (const auto& slot : slots_) {
    if (slot.saved) longest = (std::max)(longest, slot.name.size());
  }
  return longest;
}

bool CameraBookmarkStore::loadFromSettings(const std::filesystem::path& file) {
  if (file.empty()) { return false; }
  IniSection section;
  if (!readIniSection(file, L"camera", section)) return false;
  for (std::size_t index = 0; index < kCameraBookmarkSlots; ++index) {
    const std::size_t slot = index + 1;
    CameraBookmark bookmark;
    bookmark.saved = readInt(section, slotKey(slot, "saved"), 0) != 0;
    bookmark.name = readString(section, slotKey(slot, "name"));
    bookmark.mode = readString(section, slotKey(slot, "mode"));
    bookmark.tick = readInt(section, slotKey(slot, "tick"), -1);
    if (bookmark.saved) {
      // The six pose fields are written separately (contract 1.1). They are
      // read through the same parse a pose string uses, so the two spellings
      // cannot disagree about what "a pose" is.
      std::string poseText;
      const char* fields[6] = { "focusX", "focusY", "focusZ", "yaw", "pitch", "distance" };
      for (int index = 0; index < 6; ++index) {
        if (index) poseText += ",";
        poseText += readString(section, slotKey(slot, fields[index]));
      }
      const std::string fovText = readString(section, slotKey(slot, "fov"));
      float fov = kDefaultBookmarkFov;
      bool fovValid = true;
      if (!fovText.empty()) {
        char* end = nullptr;
        const double parsed = std::strtod(fovText.c_str(), &end);
        fovValid = end && *end == '\0' && std::isfinite(parsed);
        if (fovValid) fov = std::clamp(static_cast<float>(parsed), 40.0f, 120.0f);
      }
      CameraPose pose;
      if (!parsePose(poseText, pose) || !fovValid) {
        // A saved flag with an unusable pose (or fov) is a corrupt slot, not a
        // bookmark: keeping it would make a jump restore whatever the parse
        // defaulted to.
        bookmark = CameraBookmark{};
        ++refusedCount_;
      } else {
        bookmark.pose = pose;
        bookmark.fov = fov;
        if (bookmark.name.empty()) bookmark.name = defaultBookmarkName(slot);
        if (bookmark.mode.empty()) bookmark.mode = "first-person";
      }
    } else {
      bookmark = CameraBookmark{};
    }
    slots_[index] = bookmark;
  }
  return true;
}

bool CameraBookmarkStore::saveToSettings(const std::filesystem::path& file) const {
  if (file.empty()) return false;
  // [camera] is written whole -- the store knows every slot -- and every other
  // section in settings.ini is left alone by writeIniSection. That split is why
  // the bookmarks live beside [renderer] rather than in a file of their own.
  IniSection section;
  section.reserve(kCameraBookmarkSlots * 11);
  for (std::size_t index = 0; index < kCameraBookmarkSlots; ++index) {
    const std::size_t slot = index + 1;
    const CameraBookmark& bookmark = slots_[index];
    std::wstring key;
    std::wstring value;
    if (!utf8ToWide(std::to_string(bookmark.saved ? 1 : 0), value)) return false;
    section.emplace_back(slotKey(slot, "saved"), value);
    if (!utf8ToWide(bookmark.saved ? bookmark.name : std::string(), value)) return false;
    section.emplace_back(slotKey(slot, "name"), value);
    // One line per pose field rather than one packed line, so a hand-edited
    // file names what it changed (contract 1.1). formatPose still defines the
    // number format and the order, which is why the two are read back through
    // the same parsePose.
    const char* fields[6] = { "focusX", "focusY", "focusZ", "yaw", "pitch", "distance" };
    const float values[6] = { bookmark.pose.focusX, bookmark.pose.focusY, bookmark.pose.focusZ,
      bookmark.pose.yaw, bookmark.pose.pitch, bookmark.pose.distance };
    for (int index = 0; index < 6; ++index) {
      if (!utf8ToWide(bookmark.saved ? formatPoseField(values[index]) : std::string(), value)) return false;
      section.emplace_back(slotKey(slot, fields[index]), value);
    }
    if (!utf8ToWide(bookmark.saved ? formatPoseField(bookmark.fov) : std::string(), value)) return false;
    section.emplace_back(slotKey(slot, "fov"), value);
    if (!utf8ToWide(bookmark.saved ? bookmark.mode : std::string(), value)) return false;
    section.emplace_back(slotKey(slot, "mode"), value);
    if (!utf8ToWide(std::to_string(bookmark.saved ? bookmark.tick : -1), value)) return false;
    section.emplace_back(slotKey(slot, "tick"), value);
  }
  return writeIniSection(file, L"camera", section);
}

} // namespace tf2::native
