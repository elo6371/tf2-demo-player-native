# Camera / HUD shared UI-settings-hotkey contract (T50 + T51)

Status: **frozen for implementation**, 2026-10-22. Owner of this file: T50
(`task/t50-camera`). T51 implements against the same document; neither side
invents its own table, its own settings file or its own control layout.

Baseline: `6b71aeb` (p0-entity-protocol). Everything below is inside `native/`
except the two gate scripts.

The product currently has **two** INI files, both written with
`WritePrivateProfileStringW`, and they stay two files:

| file | path | who owns it today |
|---|---|---|
| `settings.ini` | `%LOCALAPPDATA%\TF2 Demo Player\settings.ini` | `src/settings_store.cpp` (`[renderer]` section) |
| `ui.ini` | `%LOCALAPPDATA%\TF2 Demo Player\ui.ini` | `src/native_ui.cpp` (`[ui]` section) |

T50 adds sections, it does not add a third file: bookmarks and key bindings go
into **`settings.ini`** beside `[renderer]` because they are camera state that
must survive an uninstall of the HUD; T51 adds `[hud]` to **`ui.ini`** because
that file is already the UI-owned one. Both files are plain INI; no JSON, no
version field is added (an absent key is the version-0 state).

## 1. settings.ini — T50 sections (exact key names)

### 1.1 `[camera]` — named camera bookmarks

Eight slots, 1-based in the UI, 0-based in the file suffix.

| key | type | default | meaning |
|---|---|---|---|
| `bookmarkN.name` | string (UTF-8 written as wide) | `` (empty) | user name; empty + `bookmarkN.saved=0` means the slot is free |
| `bookmarkN.saved` | int 0/1 | 0 | 1 <=> the pose fields below are valid |
| `bookmarkN.focusX` | float, `%.9g` | 0 | orbit focus in the renderer's normalized box, exactly what `Renderer::saveCameraPreset` stores |
| `bookmarkN.focusY` | float | 0 | same |
| `bookmarkN.focusZ` | float | 0.5 | same |
| `bookmarkN.yaw` | float, radians, `[-pi,pi)` | 0 | orbit yaw |
| `bookmarkN.pitch` | float, radians, `[0.25,1.45]` | 0.55 | orbit pitch |
| `bookmarkN.distance` | float, `[0.8,12]` | 3.0 | orbit distance |
| `bookmarkN.mode` | string | `first-person` | `first-person` | `third-person` | `free-look`, the camera mode active at save time; jumping restores it |
| `bookmarkN.tick` | int | -1 | playback tick at save time, informational only (a jump never seeks) |

Floats are written with 9 significant digits; a jump restores the pose by
writing those exact values back, so a save/jump/save round trip is
byte-identical in the file. `-1` and `saved=0` are the "no bookmark" state.

### 1.2 `[keys]` — the one shortcut table (shared with T51)

One line per **action**, key = the action id, value = the serialized chord.

```ini
[keys]
camera.freeFly=F9
camera.resetToPlayer=F10
camera.resetView=F11
camera.bookmark.jump1=F1
camera.bookmark.save1=Ctrl+F1
camera.bookmark.delete1=Ctrl+Shift+F1
; T51 registers its three actions into this same section:
hud.toggle=H
hud.preview=Ctrl+P
hud.reset=Ctrl+Shift+H
```

**Serialization (canonical, both sides):**

```
chord   := "" | [ "Ctrl+" ] [ "Alt+" ] [ "Shift+" ] keyName
keyName := "A".."Z" | "0".."9" | "F1".."F24" | "Numpad0".."Numpad9"
         | "Space" | "Tab" | "Enter" | "Escape" | "Backspace" | "Insert"
         | "Delete" | "Home" | "End" | "PageUp" | "PageDown"
         | "Left" | "Right" | "Up" | "Down" | "Plus" | "Minus"
         | "VK:<decimal virtual-key code>"
```

Modifier order is fixed `Ctrl+Alt+Shift+`; the parser accepts any order and
re-emits canonical, so `Shift+Ctrl+F1` reads back as `Ctrl+Shift+F1`.
`""` (or a missing key) is **unbound**, which is a legal state and must be
reported as unbound, never defaulted at dispatch time. `VK:<n>` is the escape
hatch for keys with no name; `KeyChord` keeps the raw `vk` **and** the
modifier mask, so dispatch is an exact (vk, ctrl, alt, shift) compare.

**Conflict rule (one function, used by both sides):** two actions conflict iff
their chords compare equal in canonical form *and* both are bound. A conflict
is a **state**, not a refusal: the later `set` wins the key and the earlier
action keeps its (now unbound) slot only if the caller asked for that; the
default API reports the conflict and refuses the assignment
(`KeyBindTable::set` returns `false`, the table is unchanged) so a UI can ask
the user. The HUD and camera actions share the same namespace, so
`hud.toggle=H` conflicts with a hypothetical `camera.something=H` and is
detected without either side knowing the other's list.

**Reset semantics (three distinct operations):**

* `resetAction(a)` — restore *this* action's compiled default.
* `resetAll()` — restore every action's compiled default (what the "Restore
  defaults" button does); persisted immediately.
* `unbindAll()` — every action unbound; the product keeps working, hotkeys do
  nothing, and every dispatch reports `unbound` rather than falling back to a
  hard-coded key.

**Missing binding:** `actionForKey()` returns `nullptr`; `dispatch` records
`unboundActions` and does nothing. No silent legacy fallback exists — F1..F8
no longer reach the old transient preset arms.

**File write semantics (added 2026-10-22):** `KeyBindTable::saveToSettings`
**merges** with the `[keys]` section already on disk rather than replacing it:
actions this build knows are written, actions it does not know are carried
through unchanged, and the result is sorted by action id so two saves of the
same state are byte-identical. This is what keeps one shared table safe across
two features that ship in different builds — a camera-only build must never
delete the `hud.*` lines. `CameraBookmarkStore::saveToSettings` writes the
whole `[camera]` section (it owns every slot) and `saveSettings()` in
`settings_store.cpp` carries every *other* section across its
write-to-temporary-and-move cycle, so `[renderer]`, `[camera]` and `[keys]`
cannot delete each other.

**Keyboard focus in text fields (T50 asserts this, T51 may rely on it):**
`shouldDispatchHotkey(bool textFieldFocused, bool keyIsCharacter)` — when a
text field has focus **no** bound action fires, and character keys are left to
the control (`WM_CHAR`), including keys that are also bound (`P`, `H`, `F1`).

### 1.3 `[ui]` (in `ui.ini`) — T51 section

| key | type | default |
|---|---|---|
| `hud.enabled` | int 0/1 | 0 |
| `hud.root` | string path | `` |
| `hud.layout` | string relative path | `` |
| `hud.scale` | int percent, 50..200 | 100 |
| `hud.preview` | int 0/1 | 1 |

T51 owns every `hud.*` key; T50 owns every `camera.*` and `keys.*` key. A key
that belongs to neither prefix is `ui.*` and read-only for both (T51 may read
`ui.dpiScale`, `ui.width`, `ui.height` if it needs them, and must not write
them).

## 2. Shared code modules (include paths, one implementation each)

| module | file | owner | both sides use it for |
|---|---|---|---|
| key table | `native/include/keybinds.h`, `src/keybinds.cpp` | T50 | parsing/serializing chords, defaults, conflict detection, reset, dispatch, focus rule. **T51 must not add a second table.** |
| bookmarks | `native/include/camera_bookmarks.h`, `src/camera_bookmarks.cpp` | T50 | the 8-slot named store + settings round trip |
| camera director | `native/include/camera_director.h` | T50 | free-fly/orbit/reset-to-player/reset-view state machine |
| HUD settings | T51's own files | T51 | `[hud]` load/save only |

Registration for T51, exactly one call, defaults declared by the action's first
registration:

```cpp
tf2::native::KeyBindTable table = tf2::native::defaultKeyBindings(); // T50 actions
table.registerDefault("hud.toggle",  tf2::native::parseChord("H"));
table.registerDefault("hud.preview", tf2::native::parseChord("Ctrl+P"));
table.registerDefault("hud.reset",   tf2::native::parseChord("Ctrl+Shift+H"));
table.loadFromSettings(settingsPath());   // [keys] overrides the defaults
```

## 3. UI controls — who owns which rectangle

The layout planner (`planUiLayout`, `UiControlId`) stays the single source of
rects. T50 reserves new ids **immediately after `Timeline`**, in this order:

```
CameraMode, FreeFly, ResetToPlayer, ResetView,
BookmarkSave, BookmarkNameEdit, BookmarkDelete, BookmarkList,
KeybindReset
```

T51 reserves nothing from `UiControlId` and **does not insert into it**. The
frozen resolution of this (addendum 2026-10-22, proposed by T51, approved by
T50) is smaller than the original wording and is what both branches implement:

```cpp
// native_ui.h, AFTER the UiControlId enum and without touching it
enum class UiHudControlId { Import = 0, Preview, Scale, Reset, Status, Count };
struct UiHudLayoutPlan { UiRect rects[static_cast<std::size_t>(UiHudControlId::Count)]{}; bool valid = false; };
// Places one HUD row BENEATH base; it reads base and never rewrites a rect in it.
UiHudLayoutPlan planHudLayout(const UiLayoutPlan& base, int clientWidth, int clientHeight,
  float dpiScale);
```

Reasons this is the frozen form rather than "append after T50's block": a
second insertion point at the tail of one enum is a guaranteed merge conflict
on the same hunk, and the failure mode when a side is lost is silent (a shifted
`Count`, every rect in the array misindexed). A separate enum plus a function
that consumes `UiLayoutPlan` keeps the two write scopes disjoint: T50 owns the
enum body and `planUiLayout`, T51 owns the new declarations and its own
function. `Count` stays last inside T50's block.

The status row, margins, gap and dpi scaling rule (`scaleDimension`,
round-half-up) are shared and must not be re-derived.

T50-owned controls: camera mode indicator, free-fly toggle, reset-to-player,
reset-view, save-bookmark, bookmark name text field, delete-bookmark, bookmark
list/cycle, restore-default-keys. T51-owned: HUD import picker button, preview
toggle, scale slider, reset-to-default-HUD. Reused verbatim from the existing UI
(the T51 side must not write its own): `UiCommand` plumbing through
`NativeUiController::command`, the `make` control helper and `applyUiLayout` in
`main.cpp`, `isDemoPath`/`pathExists` validation helpers, the Open-file dialog
helper `chooseDemoFile` (copy its shape for a folder picker; do not use
`GetOpenFileNameW` for a directory), and the hidden-window gate discipline
(`--hidden-window --audio-disabled`) every capture uses.

Image-affecting rule for both: HUD/UI drawing must not change the world/model
pixels. T50's gate captures two hidden-window frames byte-exactly; T51's gate
asserts the HUD region is outside that comparison, so **both features must be
off by default in a capture-only run**: no bookmark outline, no HUD panel,
unless the run opted in.

## 4. What the gates observe (so the two gates cannot disagree)

T50's trace readings (all `camera:` prefixed, emitted only when the feature is
exercised or at exit):

```
camera: snapshot label=<a> tick=<n> mode=<m> focus=<x,y,z> yaw=<y> pitch=<p> dist=<d> src=<..>
camera: bookmark action=save|jump|delete slot=<n> name=<n> ok=<0|1> pose=<x,y,z;y;p;d>
camera: keys action=<id> chord=<canonical> result=ok|conflict|unbound|reset
camera: focus textField=1 dispatched=0 suppressed=<n>
camera: director tick=<n> paused=<0|1> moved=<0|1> pose=<...>
```

T51's gate reads its own `hud:` lines and must not parse `camera:` lines, and
vice versa.

## 5. Change control

Any change to a field name, a default, the chord grammar, the conflict rule or
the `UiControlId` reservation is a change to **this** file first, then to both
implementations, then to both gates. A one-sided edit is a contract break.
