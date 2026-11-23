#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace tf2::native {

// ---------------------------------------------------------------------------
// The product's ONE keyboard binding table (T50; shared with T51's HUD).
//
// Why this file exists
//   Every key the product understood was an if-arm inside the window procedure,
//   which made four separate questions unanswerable from outside: what a key is
//   called, whether two features wanted the same key, what happens when the user
//   rebinds one, and what a key with no binding does. The if-arms also disagreed
//   with each other: F1..F8 silently reached a transient camera preset, so a
//   key the product "did not bind" still did something.
//
//   This table is the single implementation. An action is an id string
//   ("camera.freeFly", "hud.toggle"); a binding is a KeyChord. Both features
//   register into one table, so a conflict between them is detected without
//   either side knowing the other's action list.
//
// The contract this implements is native/docs/camera-ui-contract.md 1.2.
// ---------------------------------------------------------------------------

// A key plus its modifiers. Unbound is vk == 0; virtual-key codes are never 0,
// so "no key" cannot collide with a real one.
struct KeyChord {
  unsigned vk = 0;
  bool ctrl = false;
  bool alt = false;
  bool shift = false;

  bool bound() const { return vk != 0; }
};

bool operator==(const KeyChord& a, const KeyChord& b);
bool operator!=(const KeyChord& a, const KeyChord& b);

// Canonical text form: [Ctrl+][Alt+][Shift+]KeyName, modifiers always in that
// order. Unbound is the empty string. formatChord(parseChord(text)) == text for
// every canonical text, which is what makes the settings file byte-stable.
std::string formatChord(const KeyChord& chord);

// Parses any modifier order. Malformed text is unbound (never a guess), so a
// hand-edited settings file cannot invent a binding the product does not have.
KeyChord parseChord(const std::string& text);

// Same parse, but tells "empty" from "malformed":
//   returns true  for "" and for a legal chord,
//   returns false for text this grammar cannot express.
bool chordParseable(const std::string& text);

enum class KeyDispatchResult {
  Ok,        // the chord is bound to an action and that action was dispatched
  Unbound,   // the chord itself is unbound
  NoAction,  // the chord is bound-looking but no action claims it
};

struct KeyDispatch {
  std::string action;                       // empty unless result == Ok
  KeyDispatchResult result = KeyDispatchResult::Unbound;
};

// The rules a UI needs in order to be honest about what it is showing. Text
// fields are the case that bites: a bound character key (P, H) typed into a
// name field must reach the field, not the action.
bool shouldDispatchHotkey(bool textFieldFocused, bool keyIsCharacter);

class KeyBindTable {
public:
  struct Binding {
    KeyChord current;
    KeyChord fallback;    // the compiled default, what resetAction restores
    bool isDefault = true;
  };

  // Registers an action and its default. Re-registering an existing action
  // replaces the default and leaves the user's current binding alone, so a second
  // feature declaring its keys cannot silently discard a rebind.
  void registerDefault(const std::string& action, const KeyChord& chord);
  bool registered(const std::string& action) const;
  std::vector<std::string> actions() const;    // sorted, stable across runs

  KeyChord actionKey(const std::string& action) const;
  bool unbound(const std::string& action) const;

  // False when the assignment would duplicate another action's chord (the table
  // is then unchanged) or the action is not registered. This refusal is the
  // conflict detection every UI asks about; see conflicts() for the list.
  bool set(const std::string& action, const KeyChord& chord);
  bool bind(const std::string& action, const KeyChord& chord) { return set(action, chord); }
  void unbind(const std::string& action);

  // Every action whose bound chord equals this one. Empty when there is none;
  // includes `action` itself when its own chord is duplicated, so a caller that
  // wants "the others" filters it out or uses conflictsFor().
  std::vector<std::string> conflicts(const KeyChord& chord) const;
  std::vector<std::string> conflictsFor(const std::string& action) const;

  // The action a chord is bound to, or nullptr. The pointer stays valid until
  // the table is modified.
  const std::string* actionForKey(const KeyChord& chord) const;

  // Dispatch, with the reading the trace prints.
  KeyDispatch dispatch(const KeyChord& chord);

  // Records that a keystroke was withheld because a text field owned it. The
  // focus rule itself lives in shouldDispatchHotkey() -- it is a property of the
  // UI, not of the table -- but the TABLE keeps the count, so "a focused field
  // really did swallow keystrokes" is a reading on the same summary line as the
  // rest of the key statistics.
  void noteSuppressedByFocus() { ++suppressedByFocus_; }

  // Three distinct resets (contract 1.2): one action, every action, no bindings.
  bool resetAction(const std::string& action);
  void resetAll();
  void unbindAll();

  // settings.ini [keys]. loadFromSettings applies the file on top of whatever
  // defaults are registered: an action absent from the file keeps its default,
  // and an action present with an empty value is explicitly unbound.
  bool loadFromSettings(const std::filesystem::path& file);
  bool saveToSettings(const std::filesystem::path& file) const;

  // Readings, printed once at exit so an acceptance run can pin them.
  std::uint32_t setCount() const { return setCount_; }
  std::uint32_t conflictCount() const { return conflictCount_; }
  std::uint32_t resetCount() const { return resetCount_; }
  std::uint32_t dispatchOkCount() const { return dispatchOk_; }
  std::uint32_t dispatchUnboundCount() const { return dispatchUnbound_; }
  std::uint32_t suppressedByFocusCount() const { return suppressedByFocus_; }

private:
  std::map<std::string, Binding> bindings_;
  std::uint32_t setCount_ = 0;
  std::uint32_t conflictCount_ = 0;
  std::uint32_t resetCount_ = 0;
  std::uint32_t dispatchOk_ = 0;
  std::uint32_t dispatchUnbound_ = 0;
  std::uint32_t suppressedByFocus_ = 0;
};

// The compiled defaults for T50's camera actions. T51 adds its three
// hud.* actions on top of this returned table (contract 2).
KeyBindTable defaultKeyBindings();

// The action ids this product ships, spelled once so the trace, the settings
// writer and the gates cannot drift apart.
namespace camera_actions {
constexpr const char* kFreeFly = "camera.freeFly";
constexpr const char* kResetToPlayer = "camera.resetToPlayer";
constexpr const char* kResetView = "camera.resetView";
// 1-based, bookmark slots 1..8.
std::string jumpBookmark(std::size_t oneBasedSlot);
std::string saveBookmark(std::size_t oneBasedSlot);
std::string deleteBookmark(std::size_t oneBasedSlot);
} // namespace camera_actions

} // namespace tf2::native
