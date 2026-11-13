#include "review_workspace.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <map>
#include <set>
#include <system_error>
#include <utility>
#include <vector>

#include <windows.h>

namespace tf2::native {

namespace {

constexpr const char* kHeaderComment = "; tf2-demo-player review workspace";

std::int64_t nowUtcSeconds() {
  return static_cast<std::int64_t>(std::time(nullptr));
}

std::string formatFloatField(float value) {
  char buffer[32]{};
  // %.9g is FLT_DECIMAL_DIG: every float survives write -> read -> float with
  // the same bits. This mirrors camera_director.cpp's formatFloat; the store
  // keeps its own copy so the probe target does not have to link the camera
  // implementation for one formatting helper.
  std::snprintf(buffer, sizeof(buffer), "%.9g", static_cast<double>(value));
  return buffer;
}

std::string formatDoubleField(double value) {
  char buffer[48]{};
  std::snprintf(buffer, sizeof(buffer), "%.17g", value);
  return buffer;
}

bool parseI64(const std::string& text, std::int64_t& out) {
  if (text.empty()) return false;
  errno = 0;
  char* end = nullptr;
  const long long value = std::strtoll(text.c_str(), &end, 10);
  if (errno != 0 || end == text.c_str() || end == nullptr || *end != '\0') return false;
  out = static_cast<std::int64_t>(value);
  return true;
}

bool parseU64(const std::string& text, std::uint64_t& out) {
  if (text.empty() || text[0] == '-') return false;
  errno = 0;
  char* end = nullptr;
  const unsigned long long value = std::strtoull(text.c_str(), &end, 10);
  if (errno != 0 || end == text.c_str() || end == nullptr || *end != '\0') return false;
  out = static_cast<std::uint64_t>(value);
  return true;
}

bool parseF32(const std::string& text, float& out) {
  if (text.empty()) return false;
  errno = 0;
  char* end = nullptr;
  const float value = std::strtof(text.c_str(), &end);
  if (errno == ERANGE || end == text.c_str() || end == nullptr || *end != '\0' || !std::isfinite(value)) {
    return false;
  }
  out = value;
  return true;
}

bool parseF64(const std::string& text, double& out) {
  if (text.empty()) return false;
  errno = 0;
  char* end = nullptr;
  const double value = std::strtod(text.c_str(), &end);
  if (errno == ERANGE || end == text.c_str() || end == nullptr || *end != '\0' || !std::isfinite(value)) {
    return false;
  }
  out = value;
  return true;
}

bool parseBoolValue(const std::string& text, bool& out) {
  if (text == "0") { out = false; return true; }
  if (text == "1") { out = true; return true; }
  return false;
}

bool isKeyChar(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
    || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
}

bool isSectionChar(char c) {
  return isKeyChar(c);
}

std::string pathKey(const std::filesystem::path& path) {
  std::error_code error;
  std::filesystem::path normalized = std::filesystem::weakly_canonical(path, error);
  if (error) normalized = path.lexically_normal();
  std::string text = normalized.string();
  for (char& c : text) {
    if (c == '/') c = '\\';
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  return text;
}

struct RawSection {
  std::string name;
  std::vector<std::pair<std::string, std::string>> keys;
};

// Reads the workspace's INI dialect. Values are not trimmed and may contain
// '='; a line is split on the first '='. A key outside a section is only
// allowed for the leading "version" header.
bool parseIniText(const std::string& text, std::vector<RawSection>& sections,
                  std::string& preambleVersion, std::string& reason) {
  sections.clear();
  preambleVersion.clear();
  if (text.empty()) { reason = "empty-file"; return false; }
  std::size_t position = 0;
  if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF
      && static_cast<unsigned char>(text[1]) == 0xBB
      && static_cast<unsigned char>(text[2]) == 0xBF) {
    position = 3;
  }
  RawSection* current = nullptr;
  std::set<std::string> seenSections;
  while (position <= text.size()) {
    const std::size_t newline = text.find('\n', position);
    std::string line = newline == std::string::npos
      ? text.substr(position) : text.substr(position, newline - position);
    position = newline == std::string::npos ? text.size() + 1 : newline + 1;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.find('\0') != std::string::npos) { reason = "nul-byte"; return false; }
    if (line.empty()) continue;
    if (line[0] == ';' || line[0] == '#') continue;
    if (line[0] == '[') {
      if (line.size() < 3 || line.back() != ']') { reason = "malformed-section"; return false; }
      const std::string name = line.substr(1, line.size() - 2);
      if (name.empty()) { reason = "malformed-section"; return false; }
      for (char c : name) {
        if (!isSectionChar(c)) { reason = "malformed-section " + name; return false; }
      }
      if (!seenSections.insert(name).second) { reason = "duplicate-section " + name; return false; }
      sections.push_back(RawSection{name, {}});
      current = &sections.back();
      continue;
    }
    const std::size_t equals = line.find('=');
    if (equals == std::string::npos || equals == 0) { reason = "line-without-key"; return false; }
    const std::string key = line.substr(0, equals);
    for (char c : key) {
      if (!isKeyChar(c)) { reason = "invalid-key " + key; return false; }
    }
    const std::string value = line.substr(equals + 1);
    if (current == nullptr) {
      if (key != "version") { reason = "key-outside-section " + key; return false; }
      if (!preambleVersion.empty()) { reason = "duplicate-key version"; return false; }
      preambleVersion = value;
      continue;
    }
    for (const auto& entry : current->keys) {
      if (entry.first == key) { reason = "duplicate-key " + current->name + "." + key; return false; }
    }
    current->keys.push_back({key, value});
  }
  return true;
}

struct SectionValues {
  std::string name;
  const std::vector<std::pair<std::string, std::string>>* raw = nullptr;
  std::map<std::string, std::string> values;
  // Logical const: take() only records which keys the parser has claimed so
  // that collectExtras() can preserve the rest verbatim.
  mutable std::set<std::string> consumed;

  bool take(const std::string& key, std::string& out, std::string& reason) const {
    const auto it = values.find(key);
    if (it == values.end()) { reason = "missing-key " + name + "." + key; return false; }
    consumed.insert(key);
    out = it->second;
    return true;
  }
  bool takeText(const std::string& key, std::string& out, std::string& reason) const {
    std::string rawValue;
    if (!take(key, rawValue, reason)) return false;
    if (!workspacePercentDecode(rawValue, out)) { reason = "bad-encoding " + name + "." + key; return false; }
    return true;
  }
  bool takeI32(const std::string& key, std::int32_t& out, std::string& reason) const {
    std::string rawValue;
    if (!take(key, rawValue, reason)) return false;
    std::int64_t parsed = 0;
    if (!parseI64(rawValue, parsed) || parsed < INT32_MIN || parsed > INT32_MAX) {
      reason = "bad-integer " + name + "." + key; return false;
    }
    out = static_cast<std::int32_t>(parsed);
    return true;
  }
  bool takeU32(const std::string& key, std::uint32_t& out, std::string& reason) const {
    std::string rawValue;
    if (!take(key, rawValue, reason)) return false;
    std::uint64_t parsed = 0;
    if (!parseU64(rawValue, parsed) || parsed > UINT32_MAX) {
      reason = "bad-integer " + name + "." + key; return false;
    }
    out = static_cast<std::uint32_t>(parsed);
    return true;
  }
  bool takeU64(const std::string& key, std::uint64_t& out, std::string& reason) const {
    std::string rawValue;
    if (!take(key, rawValue, reason)) return false;
    if (!parseU64(rawValue, out)) { reason = "bad-integer " + name + "." + key; return false; }
    return true;
  }
  bool takeI64(const std::string& key, std::int64_t& out, std::string& reason) const {
    std::string rawValue;
    if (!take(key, rawValue, reason)) return false;
    if (!parseI64(rawValue, out)) { reason = "bad-integer " + name + "." + key; return false; }
    return true;
  }
  bool takeF32(const std::string& key, float& out, std::string& reason) const {
    std::string rawValue;
    if (!take(key, rawValue, reason)) return false;
    if (!parseF32(rawValue, out)) { reason = "bad-number " + name + "." + key; return false; }
    return true;
  }
  bool takeF64(const std::string& key, double& out, std::string& reason) const {
    std::string rawValue;
    if (!take(key, rawValue, reason)) return false;
    if (!parseF64(rawValue, out)) { reason = "bad-number " + name + "." + key; return false; }
    return true;
  }
  bool takeBool(const std::string& key, bool& out, std::string& reason) const {
    std::string rawValue;
    if (!take(key, rawValue, reason)) return false;
    if (!parseBoolValue(rawValue, out)) { reason = "bad-boolean " + name + "." + key; return false; }
    return true;
  }
};

SectionValues makeValues(const RawSection& section) {
  SectionValues values;
  values.name = section.name;
  values.raw = &section.keys;
  for (const auto& entry : section.keys) values.values[entry.first] = entry.second;
  return values;
}

bool startsWith(const std::string& text, const char* prefix) {
  return text.rfind(prefix, 0) == 0;
}

bool collectExtras(const SectionValues& values, WorkspaceDocument& document,
                   bool rejectDynamicPrefixes, std::string& reason) {
  if (values.raw == nullptr) return true;
  for (const auto& entry : *values.raw) {
    if (values.consumed.count(entry.first) != 0) continue;
    if (rejectDynamicPrefixes) {
      if (startsWith(entry.first, "slot.") || startsWith(entry.first, "panel.")
          || startsWith(entry.first, "player.")) {
        reason = "unexpected-key " + values.name + "." + entry.first;
        return false;
      }
    }
    document.extraKeys.push_back(WorkspaceExtraKey{values.name, entry.first, entry.second});
  }
  return true;
}

bool parseWorkspaceSection(const SectionValues& values, WorkspaceDocument& document,
                           std::string& reason) {
  std::string schema;
  if (!values.take("schema", schema, reason)) return false;
  std::int64_t schemaValue = 0;
  if (!parseI64(schema, schemaValue) || schemaValue < 0) { reason = "bad-schema"; return false; }
  document.schemaVersion = static_cast<std::uint32_t>(schemaValue);
  if (!values.takeText("name", document.name, reason)) return false;
  if (!values.takeText("demoHashAlgo", document.demoHashAlgo, reason)) return false;
  if (!values.takeText("demoHash", document.demoHash, reason)) return false;
  if (!values.takeText("demoPathHint", document.demoPathHint, reason)) return false;
  if (!values.takeI64("updatedUtc", document.updatedUtc, reason)) return false;
  if (!values.takeU64("nextBookmarkId", document.nextBookmarkId, reason)) return false;
  if (!values.takeBool("autosave", document.autosave, reason)) return false;
  return true;
}

bool parseCameraSection(const SectionValues& values, WorkspaceCameraState& camera,
                        std::string& reason) {
  if (!values.takeBool("valid", camera.valid, reason)) return false;
  if (!values.takeF32("focusX", camera.pose.focusX, reason)) return false;
  if (!values.takeF32("focusY", camera.pose.focusY, reason)) return false;
  if (!values.takeF32("focusZ", camera.pose.focusZ, reason)) return false;
  if (!values.takeF32("yaw", camera.pose.yaw, reason)) return false;
  if (!values.takeF32("pitch", camera.pose.pitch, reason)) return false;
  if (!values.takeF32("distance", camera.pose.distance, reason)) return false;
  camera.pose.valid = camera.valid;
  if (!values.takeF32("fov", camera.fov, reason)) return false;
  if (!values.takeText("mode", camera.mode, reason)) return false;
  if (!values.takeI32("cameraTick", camera.cameraTick, reason)) return false;
  if (!values.takeText("targetKind", camera.targetKind, reason)) return false;
  if (!values.takeI32("targetEntindex", camera.targetEntindex, reason)) return false;
  if (!values.takeText("targetName", camera.targetName, reason)) return false;
  if (!values.takeText("targetReason", camera.targetReason, reason)) return false;
  if (!values.takeText("cameraReason", camera.cameraReason, reason)) return false;
  std::uint32_t slotCount = 0;
  if (!values.takeU32("slotCount", slotCount, reason)) return false;
  if (slotCount > 8) { reason = "slot-count-range"; return false; }
  camera.slots.clear();
  for (std::uint32_t index = 0; index < slotCount; ++index) {
    const std::string prefix = "slot." + std::to_string(index + 1) + ".";
    WorkspaceCameraSlot slot;
    slot.slot = static_cast<int>(index) + 1;
    if (!values.takeI32(prefix + "slot", slot.slot, reason)) return false;
    if (!values.takeBool(prefix + "saved", slot.saved, reason)) return false;
    if (!values.takeText(prefix + "name", slot.name, reason)) return false;
    if (!values.takeF32(prefix + "focusX", slot.pose.focusX, reason)) return false;
    if (!values.takeF32(prefix + "focusY", slot.pose.focusY, reason)) return false;
    if (!values.takeF32(prefix + "focusZ", slot.pose.focusZ, reason)) return false;
    if (!values.takeF32(prefix + "yaw", slot.pose.yaw, reason)) return false;
    if (!values.takeF32(prefix + "pitch", slot.pose.pitch, reason)) return false;
    if (!values.takeF32(prefix + "distance", slot.pose.distance, reason)) return false;
    slot.pose.valid = slot.saved;
    if (!values.takeF32(prefix + "fov", slot.fov, reason)) return false;
    if (!values.takeText(prefix + "mode", slot.mode, reason)) return false;
    if (!values.takeI32(prefix + "tick", slot.tick, reason)) return false;
    camera.slots.push_back(std::move(slot));
  }
  return true;
}

bool parseHudSection(const SectionValues& values, WorkspaceHudState& hud, std::string& reason) {
  if (!values.takeBool("enabled", hud.enabled, reason)) return false;
  std::int32_t scale = 100;
  if (!values.takeI32("scale", scale, reason)) return false;
  hud.scalePercent = scale;
  if (!values.takeText("root", hud.root, reason)) return false;
  if (!values.takeText("layout", hud.layout, reason)) return false;
  std::uint32_t panelCount = 0;
  if (!values.takeU32("panelCount", panelCount, reason)) return false;
  if (panelCount > 64) { reason = "panel-count-range"; return false; }
  hud.panels.clear();
  for (std::uint32_t index = 0; index < panelCount; ++index) {
    const std::string prefix = "panel." + std::to_string(index) + ".";
    WorkspaceHudPanel panel;
    if (!values.takeText(prefix + "id", panel.id, reason)) return false;
    if (!values.takeBool(prefix + "visible", panel.visible, reason)) return false;
    if (!values.takeF32(prefix + "x", panel.x, reason)) return false;
    if (!values.takeF32(prefix + "y", panel.y, reason)) return false;
    if (!values.takeF32(prefix + "w", panel.width, reason)) return false;
    if (!values.takeF32(prefix + "h", panel.height, reason)) return false;
    hud.panels.push_back(std::move(panel));
  }
  return true;
}

bool parseMinimapSection(const SectionValues& values, WorkspaceMinimapState& minimap,
                         std::string& reason) {
  if (!values.takeBool("collapsed", minimap.collapsed, reason)) return false;
  if (!values.takeF32("zoom", minimap.zoom, reason)) return false;
  if (!values.takeF32("panX", minimap.panX, reason)) return false;
  if (!values.takeF32("panY", minimap.panY, reason)) return false;
  if (!values.takeText("mapName", minimap.mapName, reason)) return false;
  if (!values.takeBool("available", minimap.available, reason)) return false;
  if (!values.takeText("reason", minimap.unavailableReason, reason)) return false;
  return true;
}

bool parseEventFilterSection(const SectionValues& values, WorkspaceEventFilterState& filter,
                             std::string& reason) {
  if (!values.takeText("text", filter.text, reason)) return false;
  if (!values.takeText("eventName", filter.eventName, reason)) return false;
  if (!values.takeText("player", filter.player, reason)) return false;
  return true;
}

bool parsePlaybackSection(const SectionValues& values, WorkspacePlaybackState& playback,
                          std::string& reason) {
  if (!values.takeBool("restorable", playback.restorable, reason)) return false;
  if (!values.takeI32("tick", playback.tick, reason)) return false;
  if (!values.takeF64("speed", playback.speed, reason)) return false;
  if (!values.takeBool("paused", playback.paused, reason)) return false;
  if (!values.takeBool("reverse", playback.reverse, reason)) return false;
  if (!values.takeText("reason", playback.reason, reason)) return false;
  return true;
}

bool parseBookmarkRecord(const SectionValues& values, std::uint64_t sectionId,
                         const std::string& headerHash, WorkspaceBookmarkRecord& record,
                         bool& oldRecordVersion, std::string& reason) {
  if (!values.takeU64("id", record.id, reason)) return false;
  if (record.id != sectionId) { reason = "bookmark-id-mismatch"; return false; }
  if (!values.takeI32("tick", record.tick, reason)) return false;
  if (record.tick < 0) { reason = "bookmark-tick-range"; return false; }
  if (!values.takeText("title", record.title, reason)) return false;
  if (!values.takeText("note", record.note, reason)) return false;
  if (!values.takeBool("eventPresent", record.eventPresent, reason)) return false;
  if (!values.takeText("eventName", record.eventName, reason)) return false;
  if (!values.takeU32("eventId", record.eventId, reason)) return false;
  if (!values.takeI32("eventTick", record.eventTick, reason)) return false;
  if (!values.takeU64("eventPacketOrdinal", record.eventPacketOrdinal, reason)) return false;
  if (!values.takeU32("eventMessageBit", record.eventMessageBit, reason)) return false;
  if (record.eventPresent) {
    if (record.eventTick != record.tick) { reason = "bookmark-event-tick-mismatch"; return false; }
    if (record.eventName.empty()) { reason = "bookmark-event-name-empty"; return false; }
    if (record.eventMessageBit > 31) { reason = "bookmark-event-bit-range"; return false; }
  } else {
    if (record.eventTick != -1 || record.eventId != 0 || record.eventPacketOrdinal != 0
        || record.eventMessageBit != 0 || !record.eventName.empty()) {
      reason = "bookmark-manual-event-fields"; return false;
    }
  }
  std::uint32_t playerCount = 0;
  if (!values.takeU32("playerCount", playerCount, reason)) return false;
  if (playerCount > 64) { reason = "bookmark-player-count-range"; return false; }
  record.players.clear();
  for (std::uint32_t index = 0; index < playerCount; ++index) {
    const std::string prefix = "player." + std::to_string(index) + ".";
    WorkspacePlayerRef player;
    if (!values.takeText(prefix + "role", player.role, reason)) return false;
    if (!values.takeI32(prefix + "entindex", player.entindex, reason)) return false;
    if (!values.takeI32(prefix + "userid", player.userid, reason)) return false;
    if (!values.takeText(prefix + "name", player.name, reason)) return false;
    if (!values.takeText(prefix + "nameReason", player.nameReason, reason)) return false;
    if (player.name.empty() && player.nameReason.empty()) {
      reason = "bookmark-player-name-unavailable-unnamed"; return false;
    }
    record.players.push_back(std::move(player));
  }
  if (!values.takeText("identityReason", record.identityReason, reason)) return false;
  if (record.players.empty() && record.identityReason.empty()) {
    reason = "bookmark-identity-unavailable-unnamed"; return false;
  }
  if (!values.takeBool("cameraValid", record.camera.valid, reason)) return false;
  if (!values.takeF32("cameraFocusX", record.camera.pose.focusX, reason)) return false;
  if (!values.takeF32("cameraFocusY", record.camera.pose.focusY, reason)) return false;
  if (!values.takeF32("cameraFocusZ", record.camera.pose.focusZ, reason)) return false;
  if (!values.takeF32("cameraYaw", record.camera.pose.yaw, reason)) return false;
  if (!values.takeF32("cameraPitch", record.camera.pose.pitch, reason)) return false;
  if (!values.takeF32("cameraDistance", record.camera.pose.distance, reason)) return false;
  record.camera.pose.valid = record.camera.valid;
  if (!values.takeF32("cameraFov", record.camera.fov, reason)) return false;
  if (!values.takeText("cameraMode", record.camera.mode, reason)) return false;
  if (!values.takeI32("cameraTick", record.camera.cameraTick, reason)) return false;
  if (!values.takeI32("cameraSlot", record.camera.bookmarkSlot, reason)) return false;
  if (!values.takeText("cameraReason", record.camera.cameraReason, reason)) return false;
  if (!record.camera.valid && record.camera.cameraReason.empty()) {
    reason = "bookmark-camera-unavailable-unnamed"; return false;
  }
  if (!values.takeI64("createdUtc", record.createdAtUtc, reason)) return false;
  if (!values.takeI64("updatedUtc", record.updatedAtUtc, reason)) return false;
  if (!values.takeText("demoHash", record.demoHash, reason)) return false;
  if (!values.takeU32("recordVersion", record.recordVersion, reason)) return false;
  oldRecordVersion = record.recordVersion != 1;
  if (record.recordVersion == 1 && !record.demoHash.empty() && record.demoHash != headerHash) {
    reason = "bookmark-demo-hash-mismatch id=" + std::to_string(record.id);
    return false;
  }
  return true;
}

bool parseBookmarks(const std::map<std::string, const RawSection*>& sections,
                    const std::vector<const RawSection*>& bookmarkSections,
                    WorkspaceDocument& document, bool& oldRecordVersion, std::string& reason) {
  const auto countIt = sections.find("review.bookmarks");
  if (countIt == sections.end()) { reason = "missing-section review.bookmarks"; return false; }
  SectionValues countValues = makeValues(*countIt->second);
  std::uint32_t count = 0;
  if (!countValues.takeU32("count", count, reason)) return false;
  if (!collectExtras(countValues, document, true, reason)) return false;
  if (count != bookmarkSections.size()) {
    reason = "bookmark-count-mismatch expected=" + std::to_string(count)
      + " actual=" + std::to_string(bookmarkSections.size());
    return false;
  }
  std::set<std::uint64_t> ids;
  for (const RawSection* section : bookmarkSections) {
    const std::string suffix = section->name.substr(std::strlen("review.bookmark."));
    std::uint64_t id = 0;
    if (!parseU64(suffix, id) || id == 0) { reason = "bookmark-section-id " + section->name; return false; }
    if (!ids.insert(id).second) { reason = "duplicate-bookmark-id " + std::to_string(id); return false; }
    SectionValues values = makeValues(*section);
    WorkspaceBookmarkRecord record;
    if (!parseBookmarkRecord(values, id, document.demoHash, record, oldRecordVersion, reason)) return false;
    // A bookmark record is a fixed schema: an unknown key here is a corrupt
    // record, not something to carry forward (the count of records is already
    // strict, so silently accepting one would hide a writer bug).
    for (const auto& entry : *values.raw) {
      if (values.consumed.count(entry.first) == 0) {
        reason = "unexpected-key " + section->name + "." + entry.first;
        return false;
      }
    }
    document.bookmarks.push_back(std::move(record));
  }
  std::uint64_t maximumId = 0;
  for (const auto& record : document.bookmarks) maximumId = std::max(maximumId, record.id);
  if (document.nextBookmarkId < 1 || document.nextBookmarkId <= maximumId) {
    reason = "next-bookmark-id-range";
    return false;
  }
  return true;
}

} // namespace

const char* workspaceOpenStateName(WorkspaceOpenState state) {
  switch (state) {
    case WorkspaceOpenState::Opened: return "opened";
    case WorkspaceOpenState::Missing: return "missing";
    case WorkspaceOpenState::Corrupt: return "corrupt";
    case WorkspaceOpenState::HashMismatch: return "hash_mismatch";
    case WorkspaceOpenState::OldSchema: return "old_schema";
  }
  return "unknown";
}

const char* workspaceSaveStatusName(WorkspaceSaveStatus status) {
  switch (status) {
    case WorkspaceSaveStatus::Saved: return "ok";
    case WorkspaceSaveStatus::RefusedNotLoaded: return "refused_not_loaded";
    case WorkspaceSaveStatus::RefusedHashMismatch: return "refused_hash_mismatch";
    case WorkspaceSaveStatus::RefusedReadOnly: return "refused_read_only";
    case WorkspaceSaveStatus::WriteFailed: return "write_failed";
  }
  return "unknown";
}

const char* workspaceNewStatusName(WorkspaceNewStatus status) {
  switch (status) {
    case WorkspaceNewStatus::Created: return "created";
    case WorkspaceNewStatus::AlreadyExists: return "already_exists";
    case WorkspaceNewStatus::NoHash: return "no_hash";
    case WorkspaceNewStatus::WriteFailed: return "write_failed";
  }
  return "unknown";
}

const char* workspaceAutosaveStatusName(WorkspaceAutosaveStatus status) {
  switch (status) {
    case WorkspaceAutosaveStatus::Saved: return "saved";
    case WorkspaceAutosaveStatus::SkippedClean: return "skipped_clean";
    case WorkspaceAutosaveStatus::SkippedDisabled: return "skipped_disabled";
    case WorkspaceAutosaveStatus::Failed: return "failed";
  }
  return "unknown";
}

const char* workspaceRenameStatusName(WorkspaceRenameStatus status) {
  switch (status) {
    case WorkspaceRenameStatus::Renamed: return "renamed";
    case WorkspaceRenameStatus::RefusedEmptyName: return "refused_empty_name";
    case WorkspaceRenameStatus::RefusedTooLong: return "refused_too_long";
  }
  return "unknown";
}

const char* workspaceCloseStatusName(WorkspaceCloseStatus status) {
  switch (status) {
    case WorkspaceCloseStatus::Clean: return "clean";
    case WorkspaceCloseStatus::UnsavedExit: return "unsaved_exit";
  }
  return "unknown";
}

std::string WorkspaceStore::serialize(const WorkspaceDocument& document) {
  std::string out;
  out += kHeaderComment;
  out += "\nversion=1\n";
  auto key = [&out](const char* name, const std::string& value) {
    out += name;
    out += '=';
    out += value;
    out += '\n';
  };
  auto keyText = [&out](const char* name, const std::string& value) {
    out += name;
    out += '=';
    out += workspacePercentEncode(value);
    out += '\n';
  };
  auto keyBool = [&out](const char* name, bool value) { out += name; out += value ? "=1\n" : "=0\n"; };
  auto keyI64 = [&out](const char* name, std::int64_t value) { out += name; out += '='; out += std::to_string(value); out += '\n'; };
  auto keyU64 = [&out](const char* name, std::uint64_t value) { out += name; out += '='; out += std::to_string(value); out += '\n'; };
  auto keyF32 = [&out](const char* name, float value) { out += name; out += '='; out += formatFloatField(value); out += '\n'; };
  auto keyF64 = [&out](const char* name, double value) { out += name; out += '='; out += formatDoubleField(value); out += '\n'; };
  auto section = [&out](const char* name) { out += '['; out += name; out += "]\n"; };
  // Unknown keys are written back inside their own section, in file order, as
  // the raw value that was read: re-encoding a value this build does not
  // understand would corrupt it.
  auto emitExtras = [&document, &out](const std::string& name) {
    for (const auto& extra : document.extraKeys) {
      if (extra.section != name) continue;
      out += extra.key;
      out += '=';
      out += extra.value;
      out += '\n';
    }
  };

  section("workspace");
  key("schema", std::to_string(document.schemaVersion));
  keyText("name", document.name);
  keyText("demoHashAlgo", document.demoHashAlgo);
  keyText("demoHash", document.demoHash);
  keyText("demoPathHint", document.demoPathHint);
  keyI64("updatedUtc", document.updatedUtc);
  keyU64("nextBookmarkId", document.nextBookmarkId);
  keyBool("autosave", document.autosave);
  emitExtras("workspace");

  section("workspace.camera");
  keyBool("valid", document.camera.valid);
  keyF32("focusX", document.camera.pose.focusX);
  keyF32("focusY", document.camera.pose.focusY);
  keyF32("focusZ", document.camera.pose.focusZ);
  keyF32("yaw", document.camera.pose.yaw);
  keyF32("pitch", document.camera.pose.pitch);
  keyF32("distance", document.camera.pose.distance);
  keyF32("fov", document.camera.fov);
  keyText("mode", document.camera.mode);
  key("cameraTick", std::to_string(document.camera.cameraTick));
  keyText("targetKind", document.camera.targetKind);
  key("targetEntindex", std::to_string(document.camera.targetEntindex));
  keyText("targetName", document.camera.targetName);
  keyText("targetReason", document.camera.targetReason);
  keyText("cameraReason", document.camera.cameraReason);
  key("slotCount", std::to_string(document.camera.slots.size()));
  // The key is the slot POSITION in the list, not the number the record
  // carries: keying by the number would make a document whose slots are not a
  // dense 1..N run unreadable, while the number is data the record holds
  // anyway, so it travels as its own field.
  for (std::size_t position = 0; position < document.camera.slots.size(); ++position) {
    const WorkspaceCameraSlot& slot = document.camera.slots[position];
    const std::string prefix = "slot." + std::to_string(position + 1) + ".";
    key((prefix + "slot").c_str(), std::to_string(slot.slot));
    keyBool((prefix + "saved").c_str(), slot.saved);
    keyText((prefix + "name").c_str(), slot.name);
    keyF32((prefix + "focusX").c_str(), slot.pose.focusX);
    keyF32((prefix + "focusY").c_str(), slot.pose.focusY);
    keyF32((prefix + "focusZ").c_str(), slot.pose.focusZ);
    keyF32((prefix + "yaw").c_str(), slot.pose.yaw);
    keyF32((prefix + "pitch").c_str(), slot.pose.pitch);
    keyF32((prefix + "distance").c_str(), slot.pose.distance);
    keyF32((prefix + "fov").c_str(), slot.fov);
    keyText((prefix + "mode").c_str(), slot.mode);
    key((prefix + "tick").c_str(), std::to_string(slot.tick));
  }
  emitExtras("workspace.camera");

  section("workspace.hud");
  keyBool("enabled", document.hud.enabled);
  key("scale", std::to_string(document.hud.scalePercent));
  keyText("root", document.hud.root);
  keyText("layout", document.hud.layout);
  key("panelCount", std::to_string(document.hud.panels.size()));
  for (std::size_t index = 0; index < document.hud.panels.size(); ++index) {
    const auto& panel = document.hud.panels[index];
    const std::string prefix = "panel." + std::to_string(index) + ".";
    keyText((prefix + "id").c_str(), panel.id);
    keyBool((prefix + "visible").c_str(), panel.visible);
    keyF32((prefix + "x").c_str(), panel.x);
    keyF32((prefix + "y").c_str(), panel.y);
    keyF32((prefix + "w").c_str(), panel.width);
    keyF32((prefix + "h").c_str(), panel.height);
  }
  emitExtras("workspace.hud");

  section("workspace.minimap");
  keyBool("collapsed", document.minimap.collapsed);
  keyF32("zoom", document.minimap.zoom);
  keyF32("panX", document.minimap.panX);
  keyF32("panY", document.minimap.panY);
  keyText("mapName", document.minimap.mapName);
  keyBool("available", document.minimap.available);
  keyText("reason", document.minimap.unavailableReason);
  emitExtras("workspace.minimap");

  section("workspace.eventFilter");
  keyText("text", document.eventFilter.text);
  keyText("eventName", document.eventFilter.eventName);
  keyText("player", document.eventFilter.player);
  emitExtras("workspace.eventFilter");

  section("workspace.logs");
  keyText("tfUrl", document.logsTfUrl);
  emitExtras("workspace.logs");

  section("workspace.playback");
  keyBool("restorable", document.playback.restorable);
  key("tick", std::to_string(document.playback.tick));
  keyF64("speed", document.playback.speed);
  keyBool("paused", document.playback.paused);
  keyBool("reverse", document.playback.reverse);
  keyText("reason", document.playback.reason);
  emitExtras("workspace.playback");

  section("review.bookmarks");
  key("count", std::to_string(document.bookmarks.size()));
  for (const auto& record : document.bookmarks) {
    section(("review.bookmark." + std::to_string(record.id)).c_str());
    keyU64("id", record.id);
    key("tick", std::to_string(record.tick));
    keyText("title", record.title);
    keyText("note", record.note);
    keyBool("eventPresent", record.eventPresent);
    keyText("eventName", record.eventName);
    key("eventId", std::to_string(record.eventId));
    key("eventTick", std::to_string(record.eventTick));
    keyU64("eventPacketOrdinal", record.eventPacketOrdinal);
    key("eventMessageBit", std::to_string(record.eventMessageBit));
    key("playerCount", std::to_string(record.players.size()));
    for (std::size_t index = 0; index < record.players.size(); ++index) {
      const auto& player = record.players[index];
      const std::string prefix = "player." + std::to_string(index) + ".";
      keyText((prefix + "role").c_str(), player.role);
      key((prefix + "entindex").c_str(), std::to_string(player.entindex));
      key((prefix + "userid").c_str(), std::to_string(player.userid));
      keyText((prefix + "name").c_str(), player.name);
      keyText((prefix + "nameReason").c_str(), player.nameReason);
    }
    keyText("identityReason", record.identityReason);
    keyBool("cameraValid", record.camera.valid);
    keyF32("cameraFocusX", record.camera.pose.focusX);
    keyF32("cameraFocusY", record.camera.pose.focusY);
    keyF32("cameraFocusZ", record.camera.pose.focusZ);
    keyF32("cameraYaw", record.camera.pose.yaw);
    keyF32("cameraPitch", record.camera.pose.pitch);
    keyF32("cameraDistance", record.camera.pose.distance);
    keyF32("cameraFov", record.camera.fov);
    keyText("cameraMode", record.camera.mode);
    key("cameraTick", std::to_string(record.camera.cameraTick));
    key("cameraSlot", std::to_string(record.camera.bookmarkSlot));
    keyText("cameraReason", record.camera.cameraReason);
    keyI64("createdUtc", record.createdAtUtc);
    keyI64("updatedUtc", record.updatedAtUtc);
    keyText("demoHash", record.demoHash);
    key("recordVersion", std::to_string(record.recordVersion));
  }

  emitExtras("review.bookmarks");
  // Whole unknown sections are preserved after the known ones, raw.
  for (const auto& extra : document.extraSections) {
    section(extra.name.c_str());
    for (const auto& entry : extra.keys) {
      out += entry.first;
      out += '=';
      out += entry.second;
      out += '\n';
    }
  }
  return out;
}

bool WorkspaceStore::parse(const std::string& text, WorkspaceDocument& document,
                           std::string& reason, WorkspaceOpenState& state) {
  state = WorkspaceOpenState::Corrupt;
  document = WorkspaceDocument{};
  std::vector<RawSection> sections;
  std::string preambleVersion;
  if (!parseIniText(text, sections, preambleVersion, reason)) return false;
  if (preambleVersion.empty()) { reason = "missing-key version"; return false; }
  bool oldSchema = preambleVersion != "1";

  std::map<std::string, const RawSection*> byName;
  std::vector<const RawSection*> bookmarkSections;
  for (const auto& section : sections) {
    if (startsWith(section.name, "review.bookmark.")) bookmarkSections.push_back(&section);
    else byName[section.name] = &section;
  }
  auto require = [&](const char* name) -> const RawSection* {
    const auto it = byName.find(name);
    if (it == byName.end()) { reason = std::string("missing-section ") + name; return nullptr; }
    return it->second;
  };
  const RawSection* workspaceSection = require("workspace");
  if (workspaceSection == nullptr) return false;
  {
    SectionValues values = makeValues(*workspaceSection);
    if (!parseWorkspaceSection(values, document, reason)) return false;
    if (!collectExtras(values, document, false, reason)) return false;
  }
  if (document.schemaVersion != kWorkspaceSchemaVersion) oldSchema = true;
  if (document.demoHashAlgo != kWorkspaceHashAlgo) { reason = "unknown-hash-algo"; return false; }
  if (document.demoHash.size() != 64) { reason = "bad-demo-hash"; return false; }
  for (char c : document.demoHash) {
    unsigned value = 0;
    if (!workspaceHexDigit(c, value)) { reason = "bad-demo-hash"; return false; }
  }

  const RawSection* cameraSection = require("workspace.camera");
  if (cameraSection == nullptr) return false;
  {
    SectionValues values = makeValues(*cameraSection);
    if (!parseCameraSection(values, document.camera, reason)) return false;
    if (!collectExtras(values, document, true, reason)) return false;
  }
  const RawSection* hudSection = require("workspace.hud");
  if (hudSection == nullptr) return false;
  {
    SectionValues values = makeValues(*hudSection);
    if (!parseHudSection(values, document.hud, reason)) return false;
    if (!collectExtras(values, document, true, reason)) return false;
  }
  const RawSection* minimapSection = require("workspace.minimap");
  if (minimapSection == nullptr) return false;
  {
    SectionValues values = makeValues(*minimapSection);
    if (!parseMinimapSection(values, document.minimap, reason)) return false;
    if (!collectExtras(values, document, false, reason)) return false;
  }
  const RawSection* filterSection = require("workspace.eventFilter");
  if (filterSection == nullptr) return false;
  {
    SectionValues values = makeValues(*filterSection);
    if (!parseEventFilterSection(values, document.eventFilter, reason)) return false;
    if (!collectExtras(values, document, false, reason)) return false;
  }
  const RawSection* logsSection = require("workspace.logs");
  if (logsSection == nullptr) return false;
  {
    SectionValues values = makeValues(*logsSection);
    if (!values.takeText("tfUrl", document.logsTfUrl, reason)) return false;
    if (!collectExtras(values, document, false, reason)) return false;
  }
  const RawSection* playbackSection = require("workspace.playback");
  if (playbackSection == nullptr) return false;
  {
    SectionValues values = makeValues(*playbackSection);
    if (!parsePlaybackSection(values, document.playback, reason)) return false;
    if (!collectExtras(values, document, false, reason)) return false;
  }
  bool oldRecordVersion = false;
  if (!parseBookmarks(byName, bookmarkSections, document, oldRecordVersion, reason)) return false;
  if (oldRecordVersion) oldSchema = true;

  // Sections that are neither known nor bookmark records are preserved
  // verbatim. Known-section names are listed here so a typo in a known section
  // cannot silently become "extra".
  static const std::set<std::string> known = {
    "workspace", "workspace.camera", "workspace.hud", "workspace.minimap",
    "workspace.eventFilter", "workspace.logs", "workspace.playback", "review.bookmarks",
  };
  for (const auto& section : sections) {
    if (known.count(section.name) != 0) continue;
    if (startsWith(section.name, "review.bookmark.")) continue;
    WorkspaceExtraSection extra;
    extra.name = section.name;
    for (const auto& entry : section.keys) extra.keys.push_back(entry);
    document.extraSections.push_back(std::move(extra));
  }

  state = oldSchema ? WorkspaceOpenState::OldSchema : WorkspaceOpenState::Opened;
  if (oldSchema) { reason = "schema-version"; return false; }
  return true;
}

bool WorkspaceStore::readFile(const std::filesystem::path& path, std::string& out,
                              std::string& reason) {
  std::error_code error;
  if (!std::filesystem::exists(path, error)) { reason = "missing"; return false; }
  if (std::filesystem::is_directory(path, error)) { reason = "not-a-regular-file"; return false; }
  std::ifstream input(path, std::ios::binary);
  if (!input) { reason = "io-error"; return false; }
  std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  if (!input.good() && !input.eof()) { reason = "read-error"; return false; }
  out = std::move(text);
  return true;
}

void WorkspaceStore::setRoot(const std::filesystem::path& root) { root_ = root; }
void WorkspaceStore::setExplicitPath(const std::filesystem::path& path) { explicitPath_ = path; }
bool WorkspaceStore::hasExplicitPath() const { return !explicitPath_.empty(); }

std::filesystem::path WorkspaceStore::pathFor(const std::string& algo,
                                              const std::string& hash) const {
  if (!explicitPath_.empty()) return explicitPath_;
  if (root_.empty() || hash.empty()) return {};
  return root_ / (algo + "-" + hash + ".ini");
}

std::filesystem::path WorkspaceStore::resolvePath(const std::string& hash) const {
  return pathFor(kWorkspaceHashAlgo, hash);
}

const std::filesystem::path& WorkspaceStore::path() const { return path_; }
const WorkspaceDocument& WorkspaceStore::document() const { return document_; }
WorkspaceDocument& WorkspaceStore::document() { return document_; }
bool WorkspaceStore::loaded() const { return loaded_; }
bool WorkspaceStore::dirty() const { return dirty_; }
void WorkspaceStore::markDirty() { dirty_ = true; }
void WorkspaceStore::markClean() { dirty_ = false; }
const std::string& WorkspaceStore::storedHash() const { return storedHash_; }
const std::string& WorkspaceStore::actualHash() const { return actualHash_; }
WorkspaceOpenState WorkspaceStore::loadState() const { return loadState_; }
bool WorkspaceStore::hasMigrationCandidate() const { return hasMigrationCandidate_; }

WorkspaceOpenResult WorkspaceStore::open(const std::filesystem::path& demoPath,
                                         const std::string& actualHash) {
  WorkspaceOpenResult result;
  actualHash_ = actualHash;
  path_ = resolvePath(actualHash);
  result.path = path_;
  result.actualHash = actualHash;
  loaded_ = false;
  dirty_ = false;
  hasMigrationCandidate_ = false;
  if (actualHash.empty()) {
    loadState_ = WorkspaceOpenState::Missing;
    result.state = loadState_;
    result.reason = "no-demo-hash";
    return result;
  }
  std::string text;
  std::string reason;
  if (!readFile(path_, text, reason)) {
    loadState_ = reason == "missing" ? WorkspaceOpenState::Missing : WorkspaceOpenState::Corrupt;
    result.state = loadState_;
    result.reason = reason;
    return result;
  }
  WorkspaceDocument document;
  WorkspaceOpenState state = WorkspaceOpenState::Corrupt;
  if (!parse(text, document, reason, state)) {
    loadState_ = state;
    result.state = state;
    result.reason = reason;
    result.storedHash = document.demoHash;
    result.bookmarkCount = document.bookmarks.size();
    if (state == WorkspaceOpenState::OldSchema) {
      migrationCandidate_ = std::move(document);
      hasMigrationCandidate_ = true;
    }
    return result;
  }
  result.storedHash = document.demoHash;
  result.bookmarkCount = document.bookmarks.size();
  result.nextBookmarkId = document.nextBookmarkId;
  result.movedPath = !document.demoPathHint.empty()
    && pathKey(document.demoPathHint) != pathKey(demoPath);
  if (document.demoHash != actualHash) {
    loadState_ = WorkspaceOpenState::HashMismatch;
    result.state = loadState_;
    result.reason = "demo-hash-mismatch";
    result.storedHash = document.demoHash;
    storedHash_ = document.demoHash;
    return result;
  }
  storedHash_ = document.demoHash;
  document_ = std::move(document);
  loaded_ = true;
  dirty_ = false;
  loadState_ = WorkspaceOpenState::Opened;
  result.state = loadState_;
  result.loaded = true;
  result.reason = "loaded";
  return result;
}

WorkspaceNewStatus WorkspaceStore::create(const std::filesystem::path& demoPath,
                                          const std::string& actualHash, std::string& reason) {
  if (actualHash.empty()) { reason = "no-demo-hash"; return WorkspaceNewStatus::NoHash; }
  path_ = resolvePath(actualHash);
  std::error_code error;
  if (std::filesystem::exists(path_, error)) {
    reason = "already-exists";
    return WorkspaceNewStatus::AlreadyExists;
  }
  if (!path_.parent_path().empty()) {
    std::filesystem::create_directories(path_.parent_path(), error);
    if (error) { reason = "mkdir-failed"; return WorkspaceNewStatus::WriteFailed; }
  }
  WorkspaceDocument document;
  document.schemaVersion = kWorkspaceSchemaVersion;
  document.name.clear();
  document.demoHashAlgo = kWorkspaceHashAlgo;
  document.demoHash = actualHash;
  document.demoPathHint = std::filesystem::absolute(demoPath, error).string();
  document.updatedUtc = nowUtcSeconds();
  document.nextBookmarkId = 1;
  document.autosave = true;
  document.camera.targetKind = "none";
  document.camera.cameraReason = "no-renderer";
  document.minimap.collapsed = true;
  document.minimap.available = false;
  document.minimap.unavailableReason = "no-demo";
  document.playback.restorable = false;
  document.playback.reason = "no-playback-control";
  document_ = std::move(document);
  storedHash_ = actualHash;
  actualHash_ = actualHash;
  // "new" leaves a real file behind: an empty workspace that a later open()
  // finds as Opened with 0 bookmarks (T72 A15 wants "a saved empty workspace",
  // not a document that only exists in memory). The write is the same atomic
  // temp+replace as save(), so a failed create never leaves a half file.
  WorkspaceSaveStatus writeStatus = WorkspaceSaveStatus::WriteFailed;
  if (!writeDocument(document_, reason, writeStatus)) {
    loaded_ = false;
    dirty_ = false;
    loadState_ = WorkspaceOpenState::Missing;
    reason = "write-failed";
    return WorkspaceNewStatus::WriteFailed;
  }
  loaded_ = true;
  dirty_ = false;
  loadState_ = WorkspaceOpenState::Opened;
  reason = "created";
  return WorkspaceNewStatus::Created;
}

bool WorkspaceStore::writeDocument(const WorkspaceDocument& document, std::string& reason,
                                   WorkspaceSaveStatus& status) {
  const std::string text = serialize(document);
  std::error_code error;
  if (!path_.parent_path().empty()) {
    std::filesystem::create_directories(path_.parent_path(), error);
    if (error) { status = WorkspaceSaveStatus::WriteFailed; reason = "mkdir-failed"; return false; }
  }
  std::filesystem::path temp = path_;
  temp += ".tmp.";
  temp += std::to_string(GetCurrentProcessId());
  {
    std::ofstream output(temp, std::ios::binary | std::ios::trunc);
    if (!output) { status = WorkspaceSaveStatus::WriteFailed; reason = "temp-open-failed"; return false; }
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    output.flush();
    if (!output.good()) {
      output.close();
      std::filesystem::remove(temp, error);
      status = WorkspaceSaveStatus::WriteFailed;
      reason = "temp-write-failed";
      return false;
    }
  }
  if (!MoveFileExW(temp.c_str(), path_.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    const DWORD errorCode = GetLastError();
    std::filesystem::remove(temp, error);
    status = errorCode == ERROR_ACCESS_DENIED ? WorkspaceSaveStatus::RefusedReadOnly
                                              : WorkspaceSaveStatus::WriteFailed;
    reason = "replace-failed err=" + std::to_string(errorCode);
    return false;
  }
  status = WorkspaceSaveStatus::Saved;
  reason = "saved";
  return true;
}

WorkspaceSaveStatus WorkspaceStore::save(WorkspaceDocument& document, std::string& reason) {
  if (!loaded_) { reason = "workspace-not-loaded"; return WorkspaceSaveStatus::RefusedNotLoaded; }
  if (document.schemaVersion != kWorkspaceSchemaVersion) {
    reason = "document-schema";
    return WorkspaceSaveStatus::WriteFailed;
  }
  if (document.demoHash != storedHash_ || document.demoHash != actualHash_) {
    reason = "document-demo-hash";
    return WorkspaceSaveStatus::RefusedHashMismatch;
  }
  std::set<std::uint64_t> ids;
  std::uint64_t maximumId = 0;
  for (const auto& record : document.bookmarks) {
    if (record.demoHash != document.demoHash) {
      reason = "record-demo-hash id=" + std::to_string(record.id);
      return WorkspaceSaveStatus::RefusedHashMismatch;
    }
    if (record.recordVersion != 1) { reason = "record-version"; return WorkspaceSaveStatus::WriteFailed; }
    if (record.id == 0 || !ids.insert(record.id).second) {
      reason = "duplicate-bookmark-id"; return WorkspaceSaveStatus::WriteFailed;
    }
    maximumId = std::max(maximumId, record.id);
  }
  if (document.nextBookmarkId <= maximumId) {
    reason = "next-bookmark-id-range"; return WorkspaceSaveStatus::WriteFailed;
  }
  if (path_.empty()) { reason = "no-path"; return WorkspaceSaveStatus::WriteFailed; }
  std::error_code error;
  if (std::filesystem::exists(path_, error)) {
    const DWORD attributes = GetFileAttributesW(path_.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_READONLY) != 0) {
      reason = "target-read-only";
      return WorkspaceSaveStatus::RefusedReadOnly;
    }
  }
  WorkspaceSaveStatus status = WorkspaceSaveStatus::WriteFailed;
  if (!writeDocument(document, reason, status)) return status;
  dirty_ = false;
  return WorkspaceSaveStatus::Saved;
}

WorkspaceAutosaveStatus WorkspaceStore::autosave(WorkspaceDocument& document, bool dirty,
                                                 std::string& reason) {
  if (!document.autosave) { reason = "autosave-disabled"; return WorkspaceAutosaveStatus::SkippedDisabled; }
  if (!dirty) { reason = "clean"; return WorkspaceAutosaveStatus::SkippedClean; }
  const WorkspaceSaveStatus status = save(document, reason);
  return status == WorkspaceSaveStatus::Saved ? WorkspaceAutosaveStatus::Saved
                                              : WorkspaceAutosaveStatus::Failed;
}

WorkspaceRenameStatus WorkspaceStore::rename(WorkspaceDocument& document,
                                             const std::string& name, std::string& reason) {
  std::size_t begin = 0;
  std::size_t end = name.size();
  while (begin < end && (name[begin] == ' ' || name[begin] == '\t')) ++begin;
  while (end > begin && (name[end - 1] == ' ' || name[end - 1] == '\t')) --end;
  if (begin == end) { reason = "empty-name"; return WorkspaceRenameStatus::RefusedEmptyName; }
  if (end - begin > 120) { reason = "name-too-long"; return WorkspaceRenameStatus::RefusedTooLong; }
  document.name = name.substr(begin, end - begin);
  document.updatedUtc = nowUtcSeconds();
  dirty_ = true;
  reason = "renamed";
  return WorkspaceRenameStatus::Renamed;
}

WorkspaceCloseStatus WorkspaceStore::close(bool dirty) {
  loaded_ = false;
  const WorkspaceCloseStatus status = dirty ? WorkspaceCloseStatus::UnsavedExit
                                            : WorkspaceCloseStatus::Clean;
  if (status == WorkspaceCloseStatus::Clean) dirty_ = false;
  return status;
}

bool WorkspaceStore::migrate(const std::string& actualHash, std::string& reason) {
  if (!hasMigrationCandidate_) { reason = "no-migration-candidate"; return false; }
  if (path_.empty()) { reason = "no-path"; return false; }
  if (migrationCandidate_.schemaVersion > kWorkspaceSchemaVersion) {
    reason = "unsupported-version";
    return false;
  }
  // Migration rewrites hashes, so it must never be a way to launder a
  // workspace that belongs to a different demo.
  if (migrationCandidate_.demoHash != actualHash) {
    reason = "hash-mismatch";
    return false;
  }
  WorkspaceDocument document = migrationCandidate_;
  document.schemaVersion = kWorkspaceSchemaVersion;
  document.demoHashAlgo = kWorkspaceHashAlgo;
  document.demoHash = actualHash;
  for (auto& record : document.bookmarks) {
    record.demoHash = actualHash;
    record.recordVersion = 1;
  }
  document.updatedUtc = nowUtcSeconds();
  WorkspaceSaveStatus status = WorkspaceSaveStatus::WriteFailed;
  if (!writeDocument(document, reason, status)) return false;
  document_ = std::move(document);
  storedHash_ = actualHash;
  actualHash_ = actualHash;
  loaded_ = true;
  dirty_ = false;
  loadState_ = WorkspaceOpenState::Opened;
  hasMigrationCandidate_ = false;
  reason = "migrated";
  return true;
}

} // namespace tf2::native
