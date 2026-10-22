#include "keybinds.h"

#include "ini_section.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstdlib>

namespace tf2::native {

namespace {

struct NamedKey {
  unsigned vk;
  const char* name;
};

// The named half of the grammar (contract 1.2). Written as one table so
// parse and format read the same list: a name that could be parsed but not
// printed would make formatChord non-idempotent, and the settings file would
// change every time it was saved.
constexpr NamedKey kNamedKeys[] = {
  { 'A', "A" }, { 'B', "B" }, { 'C', "C" }, { 'D', "D" }, { 'E', "E" },
  { 'F', "F" }, { 'G', "G" }, { 'H', "H" }, { 'I', "I" }, { 'J', "J" },
  { 'K', "K" }, { 'L', "L" }, { 'M', "M" }, { 'N', "N" }, { 'O', "O" },
  { 'P', "P" }, { 'Q', "Q" }, { 'R', "R" }, { 'S', "S" }, { 'T', "T" },
  { 'U', "U" }, { 'V', "V" }, { 'W', "W" }, { 'X', "X" }, { 'Y', "Y" },
  { 'Z', "Z" },
  { '0', "0" }, { '1', "1" }, { '2', "2" }, { '3', "3" }, { '4', "4" },
  { '5', "5" }, { '6', "6" }, { '7', "7" }, { '8', "8" }, { '9', "9" },
  { VK_SPACE, "Space" }, { VK_TAB, "Tab" }, { VK_RETURN, "Enter" },
  { VK_ESCAPE, "Escape" }, { VK_BACK, "Backspace" }, { VK_INSERT, "Insert" },
  { VK_DELETE, "Delete" }, { VK_HOME, "Home" }, { VK_END, "End" },
  { VK_PRIOR, "PageUp" }, { VK_NEXT, "PageDown" }, { VK_LEFT, "Left" },
  { VK_RIGHT, "Right" }, { VK_UP, "Up" }, { VK_DOWN, "Down" },
  { VK_OEM_PLUS, "Plus" }, { VK_OEM_MINUS, "Minus" },
  { VK_NUMPAD0, "Numpad0" }, { VK_NUMPAD1, "Numpad1" }, { VK_NUMPAD2, "Numpad2" },
  { VK_NUMPAD3, "Numpad3" }, { VK_NUMPAD4, "Numpad4" }, { VK_NUMPAD5, "Numpad5" },
  { VK_NUMPAD6, "Numpad6" }, { VK_NUMPAD7, "Numpad7" }, { VK_NUMPAD8, "Numpad8" },
  { VK_NUMPAD9, "Numpad9" },
};

std::string upperAscii(std::string text) {
  for (char& character : text) {
    if (character >= 'a' && character <= 'z') character = static_cast<char>(character - 'a' + 'A');
  }
  return text;
}

const char* keyName(unsigned vk) {
  for (const auto& entry : kNamedKeys) {
    if (entry.vk == vk) return entry.name;
  }
  return nullptr;
}

bool parseInt(const std::string& text, int& value) {
  if (text.empty()) return false;
  for (const char character : text) {
    if (character < '0' || character > '9') return false;
  }
  const long parsed = std::strtol(text.c_str(), nullptr, 10);
  if (parsed <= 0 || parsed > 0xFE) return false;
  value = static_cast<int>(parsed);
  return true;
}

// One key name to a virtual-key code. Case-insensitive on the letters only, so
// "f9" and "F9" are the same key, and a name this grammar does not define is a
// refusal rather than a fallback.
bool parseKeyName(std::string text, unsigned& vk) {
  if (text.empty()) return false;
  const std::string upper = upperAscii(text);
  for (const auto& entry : kNamedKeys) {
    if (upper == entry.name) { vk = entry.vk; return true; }
  }
  if (upper.size() >= 3 && upper.size() <= 4 && upper[0] == 'F') {
    int number = 0;
    if (parseInt(upper.substr(1), number) && number >= 1 && number <= 24) {
      // The F-key block is contiguous: F1..F24 map to VK_F1 + n - 1.
      vk = static_cast<unsigned>(VK_F1) + static_cast<unsigned>(number - 1);
      return true;
    }
  }
  if (upper.size() > 3 && upper.compare(0, 3, "VK:") == 0) {
    int number = 0;
    if (parseInt(upper.substr(3), number)) { vk = static_cast<unsigned>(number); return true; }
  }
  return false;
}

} // namespace

bool operator==(const KeyChord& a, const KeyChord& b) {
  return a.vk == b.vk && a.ctrl == b.ctrl && a.alt == b.alt && a.shift == b.shift;
}

bool operator!=(const KeyChord& a, const KeyChord& b) { return !(a == b); }

std::string formatChord(const KeyChord& chord) {
  if (!chord.bound()) return {};
  std::string text;
  if (chord.ctrl) text += "Ctrl+";
  if (chord.alt) text += "Alt+";
  if (chord.shift) text += "Shift+";
  const char* name = keyName(chord.vk);
  if (name) {
    text += name;
  } else {
    text += "VK:" + std::to_string(chord.vk);
  }
  return text;
}

KeyChord parseChord(const std::string& text) {
  KeyChord chord;
  if (text.empty()) return chord;
  std::vector<std::string> parts;
  std::size_t start = 0;
  while (true) {
    const auto plus = text.find('+', start);
    if (plus == std::string::npos) { parts.push_back(text.substr(start)); break; }
    // "Ctrl+" at the end leaves an empty final part, which parseKeyName rejects.
    parts.push_back(text.substr(start, plus - start));
    start = plus + 1;
  }
  if (parts.empty() || parts.size() > 4) return KeyChord{};
  const std::string keyText = parts.back();
  for (std::size_t index = 0; index + 1 < parts.size(); ++index) {
    const std::string modifier = upperAscii(parts[index]);
    if (modifier == "CTRL") { if (chord.ctrl) return KeyChord{}; chord.ctrl = true; }
    else if (modifier == "ALT") { if (chord.alt) return KeyChord{}; chord.alt = true; }
    else if (modifier == "SHIFT") { if (chord.shift) return KeyChord{}; chord.shift = true; }
    else return KeyChord{};
  }
  unsigned vk = 0;
  if (!parseKeyName(keyText, vk)) return KeyChord{};
  chord.vk = vk;
  return chord;
}

bool chordParseable(const std::string& text) {
  if (text.empty()) return true;
  return parseChord(text).bound();
}

void KeyBindTable::registerDefault(const std::string& action, const KeyChord& chord) {
  if (action.empty()) return;
  auto found = bindings_.find(action);
  if (found == bindings_.end()) {
    Binding binding;
    binding.current = chord;
    binding.fallback = chord;
    binding.isDefault = true;
    bindings_.emplace(action, binding);
    return;
  }
  // A second declaration only moves the default. The user's current binding is
  // the product's promise: registering a feature again must not undo a rebind.
  found->second.fallback = chord;
}

bool KeyBindTable::registered(const std::string& action) const {
  return bindings_.find(action) != bindings_.end();
}

std::vector<std::string> KeyBindTable::actions() const {
  std::vector<std::string> names;
  names.reserve(bindings_.size());
  for (const auto& entry : bindings_) names.push_back(entry.first);
  return names;   // std::map keeps this sorted
}

KeyChord KeyBindTable::actionKey(const std::string& action) const {
  const auto found = bindings_.find(action);
  return found == bindings_.end() ? KeyChord{} : found->second.current;
}

bool KeyBindTable::unbound(const std::string& action) const {
  return !actionKey(action).bound();
}

bool KeyBindTable::set(const std::string& action, const KeyChord& chord) {
  auto found = bindings_.find(action);
  if (found == bindings_.end()) return false;
  if (chord.bound()) {
    for (const auto& entry : bindings_) {
      if (entry.first == action) continue;
      if (!entry.second.current.bound()) continue;
      if (entry.second.current == chord) {
        // The conflict IS the refusal: the table is left untouched so the UI can
        // ask the user, and the count makes "a conflict was detected" a reading.
        ++conflictCount_;
        return false;
      }
    }
  }
  found->second.current = chord;
  found->second.isDefault = (chord == found->second.fallback);
  ++setCount_;
  return true;
}

void KeyBindTable::unbind(const std::string& action) {
  auto found = bindings_.find(action);
  if (found == bindings_.end()) return;
  found->second.current = KeyChord{};
  found->second.isDefault = false;
  ++setCount_;
}

std::vector<std::string> KeyBindTable::conflicts(const KeyChord& chord) const {
  std::vector<std::string> result;
  if (!chord.bound()) return result;
  for (const auto& entry : bindings_) {
    if (entry.second.current.bound() && entry.second.current == chord) {
      result.push_back(entry.first);
    }
  }
  return result;
}

std::vector<std::string> KeyBindTable::conflictsFor(const std::string& action) const {
  std::vector<std::string> result;
  const auto found = bindings_.find(action);
  if (found == bindings_.end()) return result;
  const KeyChord own = found->second.current;
  if (!own.bound()) return result;
  for (const auto& entry : bindings_) {
    if (entry.first == action) continue;
    if (entry.second.current.bound() && entry.second.current == own) result.push_back(entry.first);
  }
  return result;
}

const std::string* KeyBindTable::actionForKey(const KeyChord& chord) const {
  if (!chord.bound()) return nullptr;
  for (const auto& entry : bindings_) {
    if (entry.second.current.bound() && entry.second.current == chord) return &entry.first;
  }
  return nullptr;
}

KeyDispatch KeyBindTable::dispatch(const KeyChord& chord) {
  KeyDispatch result;
  const std::string* action = actionForKey(chord);
  if (!action) {
    result.result = KeyDispatchResult::Unbound;
    ++dispatchUnbound_;
    return result;
  }
  result.action = *action;
  result.result = KeyDispatchResult::Ok;
  ++dispatchOk_;
  return result;
}

bool KeyBindTable::resetAction(const std::string& action) {
  auto found = bindings_.find(action);
  if (found == bindings_.end()) return false;
  found->second.current = found->second.fallback;
  found->second.isDefault = true;
  ++resetCount_;
  return true;
}

void KeyBindTable::resetAll() {
  for (auto& entry : bindings_) {
    entry.second.current = entry.second.fallback;
    entry.second.isDefault = true;
  }
  ++resetCount_;
}

void KeyBindTable::unbindAll() {
  for (auto& entry : bindings_) {
    entry.second.current = KeyChord{};
    entry.second.isDefault = false;
  }
  ++resetCount_;
}

bool KeyBindTable::loadFromSettings(const std::filesystem::path& file) {
  if (file.empty()) return false;
  IniSection section;
  if (!readIniSection(file, L"keys", section)) return false;
  for (const auto& entry : section) {
    std::string action;
    if (!wideToUtf8(entry.first, action)) continue;
    const auto found = bindings_.find(action);
    if (found == bindings_.end()) continue;   // an action this build does not ship
    std::string text;
    if (!wideToUtf8(entry.second, text)) continue;
    if (text.empty()) {
      // Present with an empty value is an explicit unbind, which is different
      // from absent (absent keeps the default). Contract 1.2.
      found->second.current = KeyChord{};
      found->second.isDefault = false;
      continue;
    }
    const KeyChord chord = parseChord(text);
    if (!chord.bound()) continue;             // malformed: keep the default
    // A file that binds one chord to two actions is the user's own doing; the
    // first action encountered (map order, so deterministic) wins and the second
    // is left at its default rather than both being bound.
    bool taken = false;
    for (const auto& other : bindings_) {
      if (other.first == action) continue;
      if (other.second.current.bound() && other.second.current == chord) { taken = true; break; }
    }
    if (taken) { ++conflictCount_; continue; }
    found->second.current = chord;
    found->second.isDefault = (chord == found->second.fallback);
  }
  return true;
}

bool KeyBindTable::saveToSettings(const std::filesystem::path& file) const {
  if (file.empty()) return false;
  // The write MERGES with what is on disk rather than replacing the section.
  // The [keys] section is one table shared by two features (contract 1.2), and
  // each feature registers its own actions; a build that only knows the camera
  // actions must not delete the hud.* lines a newer build wrote, or a user who
  // runs an older binary once loses every HUD binding. Unknown keys are carried
  // through unchanged and in their original order.
  IniSection existing;
  if (!readIniSection(file, L"keys", existing)) return false;
  std::vector<std::wstring> ordered;
  std::map<std::wstring, std::wstring> merged;
  for (const auto& entry : existing) {
    if (merged.emplace(entry.first, entry.second).second) ordered.push_back(entry.first);
  }
  for (const auto& entry : bindings_) {
    std::wstring key;
    if (!utf8ToWide(entry.first, key)) return false;
    std::wstring value;
    if (!utf8ToWide(formatChord(entry.second.current), value)) return false;
    if (merged.find(key) == merged.end()) ordered.push_back(key);
    merged[key] = value;
  }
  // Sorted by key so two saves of the same state are byte-identical, which is
  // what lets a gate compare a reloaded file with the one it wrote.
  std::sort(ordered.begin(), ordered.end());
  IniSection section;
  section.reserve(ordered.size());
  for (const auto& key : ordered) section.emplace_back(key, merged[key]);
  return writeIniSection(file, L"keys", section);
}

// --- the shipped defaults ----------------------------------------------------
//
// These are the keys T4 had hard-coded in the window procedure: F1..F8 jumped to
// a camera preset, Ctrl+F1..F8 saved one, Ctrl+Shift+F1..F8 cleared one. Moving
// them into the table is what turns "the product has keys" into "the product has
// bindings a user can see and change"; a gate pins that the move kept the
// meanings. F9/F10/F11 are new and deliberately outside the old preset range.
KeyBindTable defaultKeyBindings() {
  KeyBindTable table;
  for (std::size_t slot = 1; slot <= 8; ++slot) {
    table.registerDefault(camera_actions::jumpBookmark(slot), parseChord("F" + std::to_string(slot)));
    table.registerDefault(camera_actions::saveBookmark(slot), parseChord("Ctrl+F" + std::to_string(slot)));
    table.registerDefault(camera_actions::deleteBookmark(slot), parseChord("Ctrl+Shift+F" + std::to_string(slot)));
  }
  table.registerDefault(camera_actions::kFreeFly, parseChord("F9"));
  table.registerDefault(camera_actions::kResetToPlayer, parseChord("F10"));
  table.registerDefault(camera_actions::kResetView, parseChord("F11"));
  return table;
}

namespace camera_actions {

std::string jumpBookmark(std::size_t oneBasedSlot) {
  return "camera.bookmark.jump" + std::to_string(oneBasedSlot);
}
std::string saveBookmark(std::size_t oneBasedSlot) {
  return "camera.bookmark.save" + std::to_string(oneBasedSlot);
}
std::string deleteBookmark(std::size_t oneBasedSlot) {
  return "camera.bookmark.delete" + std::to_string(oneBasedSlot);
}

} // namespace camera_actions

bool shouldDispatchHotkey(bool textFieldFocused, bool keyIsCharacter) {
  if (!textFieldFocused) return true;
  // A text field owns every keystroke while it has focus: a bound F-key would
  // otherwise move the camera under a user typing a bookmark name, and a bound
  // character key (P, H) must reach the control rather than toggle playback.
  // keyIsCharacter is carried because the answer is deliberately the same for
  // both -- the argument exists so a caller cannot accidentally implement the
  // "characters go to the field, function keys still act" rule, which would be
  // a second policy nobody agreed to.
  (void)keyIsCharacter;
  return false;
}

} // namespace tf2::native
