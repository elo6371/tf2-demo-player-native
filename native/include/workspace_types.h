#pragma once

// T71: the on-disk review workspace is a per-demo document. These are the
// types shared by the workspace serializer (review_workspace.*) and the T72
// bookmark logic (review_bookmarks.*). Nothing here touches Win32: the probe
// links this header without a window.

#include "camera_director.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace tf2::native {

inline constexpr std::uint32_t kWorkspaceSchemaVersion = 1;
inline constexpr const char* kWorkspaceHashAlgo = "sha256";

struct WorkspacePlayerRef {
  std::string role;
  std::int32_t entindex = -1;
  std::int32_t userid = -1;
  std::string name;
  std::string nameReason;
};

struct WorkspaceCameraRef {
  bool valid = false;
  CameraPose pose{};
  float fov = 90.0f;
  std::string mode;
  std::int32_t cameraTick = -1;
  std::int32_t bookmarkSlot = 0;
  std::string cameraReason;
};

struct WorkspaceBookmarkRecord {
  std::uint64_t id = 0;
  std::int32_t tick = -1;
  std::string title;
  std::string note;
  bool eventPresent = false;
  std::string eventName;
  std::uint32_t eventId = 0;
  std::int32_t eventTick = -1;
  std::uint64_t eventPacketOrdinal = 0;
  std::uint32_t eventMessageBit = 0;
  std::vector<WorkspacePlayerRef> players;
  std::string identityReason;
  WorkspaceCameraRef camera;
  std::int64_t createdAtUtc = 0;
  std::int64_t updatedAtUtc = 0;
  std::string demoHash;
  std::uint32_t recordVersion = 1;
};

// T71: the eight T50 camera slots, serialized as part of the workspace rather
// than as settings.ini state. A slot that was never saved stays saved=0 but
// keeps the defaults the T50 store reports, so a save/load round trip is
// byte-stable.
struct WorkspaceCameraSlot {
  int slot = 0;
  bool saved = false;
  std::string name;
  CameraPose pose{};
  float fov = 90.0f;
  std::string mode;
  std::int32_t tick = -1;
};

// T71: "current camera and target". targetKind is player/free/bookmark/none;
// targetReason names why no target is stored (no-demo, no-renderer, ...).
struct WorkspaceCameraState {
  bool valid = false;
  CameraPose pose{};
  float fov = 90.0f;
  std::string mode;
  std::int32_t cameraTick = -1;
  std::string targetKind;
  std::int32_t targetEntindex = -1;
  std::string targetName;
  std::string targetReason;
  std::string cameraReason;
  std::vector<WorkspaceCameraSlot> slots;
};

struct WorkspaceHudPanel {
  std::string id;
  bool visible = false;
  float x = 0.0f;
  float y = 0.0f;
  float width = 0.0f;
  float height = 0.0f;
};

struct WorkspaceHudState {
  bool enabled = false;
  int scalePercent = 100;
  std::string root;
  std::string layout;
  std::vector<WorkspaceHudPanel> panels;
};

struct WorkspaceMinimapState {
  bool collapsed = true;
  float zoom = 1.0f;
  float panX = 0.0f;
  float panY = 0.0f;
  std::string mapName;
  bool available = false;
  std::string unavailableReason;
};

struct WorkspaceEventFilterState {
  std::string text;
  std::string eventName;
  std::string player;
};

// T71: playback position/speed is separately marked restorable. restorable=0
// means the workspace deliberately did not capture a playback position (no
// demo loaded, no T70 control); the reason says why.
struct WorkspacePlaybackState {
  bool restorable = false;
  std::int32_t tick = 0;
  double speed = 1.0;
  bool paused = true;
  bool reverse = false;
  std::string reason;
};

// Unknown keys inside a known section and whole unknown sections are carried
// through a load/save cycle instead of being dropped. The workspace format is
// versioned, so forward-written fields must not be destroyed by an older
// build.
struct WorkspaceExtraKey {
  std::string section;
  std::string key;
  std::string value;
};

struct WorkspaceExtraSection {
  std::string name;
  std::vector<std::pair<std::string, std::string>> keys;
};

struct WorkspaceDocument {
  std::uint32_t schemaVersion = kWorkspaceSchemaVersion;
  std::string name;
  std::string demoHashAlgo = kWorkspaceHashAlgo;
  std::string demoHash;
  std::string demoPathHint;
  std::int64_t updatedUtc = 0;
  std::uint64_t nextBookmarkId = 1;
  bool autosave = true;
  std::vector<WorkspaceBookmarkRecord> bookmarks;
  WorkspaceCameraState camera;
  WorkspaceHudState hud;
  WorkspaceMinimapState minimap;
  WorkspaceEventFilterState eventFilter;
  std::string logsTfUrl;
  WorkspacePlaybackState playback;
  std::vector<WorkspaceExtraKey> extraKeys;
  std::vector<WorkspaceExtraSection> extraSections;
};

// --- percent encoding ------------------------------------------------------
//
// INI values cannot carry newlines or section brackets, and a title may hold
// any UTF-8 byte. Encode everything outside [A-Za-z0-9._~-] as %XX (uppercase
// hex). The empty string stays empty. Decoding rejects a stray '%', a
// truncated escape and any non-UTF-8 byte sequence; the workspace parser turns
// that into a named corrupt result instead of guessing.
inline bool workspaceHexDigit(char c, unsigned& value) {
  if (c >= '0' && c <= '9') { value = static_cast<unsigned>(c - '0'); return true; }
  if (c >= 'a' && c <= 'f') { value = static_cast<unsigned>(c - 'a') + 10u; return true; }
  if (c >= 'A' && c <= 'F') { value = static_cast<unsigned>(c - 'A') + 10u; return true; }
  return false;
}

inline bool workspaceUtf8Valid(const std::string& text) {
  std::size_t index = 0;
  while (index < text.size()) {
    const unsigned char lead = static_cast<unsigned char>(text[index]);
    if (lead == 0) return false;
    if (lead < 0x80) { ++index; continue; }
    std::size_t extra = 0;
    unsigned codePoint = 0;
    if ((lead & 0xE0) == 0xC0) { extra = 1; codePoint = lead & 0x1Fu; }
    else if ((lead & 0xF0) == 0xE0) { extra = 2; codePoint = lead & 0x0Fu; }
    else if ((lead & 0xF8) == 0xF0) { extra = 3; codePoint = lead & 0x07u; }
    else return false;
    if (index + extra >= text.size()) return false;
    for (std::size_t offset = 1; offset <= extra; ++offset) {
      const unsigned char continuation = static_cast<unsigned char>(text[index + offset]);
      if ((continuation & 0xC0) != 0x80) return false;
      codePoint = (codePoint << 6) | (continuation & 0x3Fu);
    }
    if (extra == 1 && codePoint < 0x80) return false;
    if (extra == 2 && codePoint < 0x800) return false;
    if (extra == 3 && codePoint < 0x10000) return false;
    if (codePoint >= 0xD800 && codePoint <= 0xDFFF) return false;
    if (codePoint > 0x10FFFF) return false;
    index += extra + 1;
  }
  return true;
}

inline std::string workspacePercentEncode(const std::string& text) {
  static const char* hex = "0123456789ABCDEF";
  std::string out;
  out.reserve(text.size());
  for (const char raw : text) {
    const unsigned char byte = static_cast<unsigned char>(raw);
    const bool plain = (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z')
      || (byte >= '0' && byte <= '9') || byte == '.' || byte == '_' || byte == '~' || byte == '-';
    if (plain) {
      out.push_back(static_cast<char>(byte));
    } else {
      out.push_back('%');
      out.push_back(hex[(byte >> 4) & 0x0F]);
      out.push_back(hex[byte & 0x0F]);
    }
  }
  return out;
}

inline bool workspacePercentDecode(const std::string& text, std::string& out) {
  out.clear();
  out.reserve(text.size());
  for (std::size_t index = 0; index < text.size(); ++index) {
    const char raw = text[index];
    if (raw != '%') {
      out.push_back(raw);
      continue;
    }
    if (index + 2 >= text.size()) return false;
    unsigned high = 0;
    unsigned low = 0;
    if (!workspaceHexDigit(text[index + 1], high)) return false;
    if (!workspaceHexDigit(text[index + 2], low)) return false;
    const unsigned byte = (high << 4) | low;
    if (byte == 0) return false;
    out.push_back(static_cast<char>(byte));
    index += 2;
  }
  return workspaceUtf8Valid(out);
}

} // namespace tf2::native
