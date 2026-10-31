#pragma once

// T76 (2026-10-29): declarations for the T51 HUD product layer that used to sit
// inside main.cpp's anonymous namespace (main.cpp:1728-2112 before the move).
//
// WHAT MOVED: the three HUD state objects (g_hud, g_hudTextTest,
// g_hudTraceEnabled), the hotkey lookup hudActionForKey, the folder picker, the
// layout discovery, hudPublishTrace -- which prints every hud: line the gates
// read -- and the hudApply*/hudUpdate*/hudImport*/hudToggle*/hudReset* helpers.
//
// WHAT DID NOT MOVE: registerKeybinds and isTextFieldFocused stay in main.cpp on
// purpose. hud-check.sh's mutation-3 deletes the first registerDefault line out
// of native/src/main.cpp by exact text, and the key table is shared with T50's
// camera actions (contract 1.2), so the table and its loader stay where the
// gates and the other callers already look for them.
//
// The state objects have external linkage (defined in main_hud.cpp) because
// main.cpp -- the argument scan and windowProc -- writes and reads the same
// objects; the HUD control handles stay defined in main.cpp and are only
// declared here.
//
// Not a public header: nothing in it is exported, no other target includes it,
// and no gate parses it.

#include <Windows.h>

#include <filesystem>
#include <string>

#include "hud_state.h"
#include "keybinds.h"
#include "native_renderer.h"

namespace tf2::native::main_internal {

// ---------------------------------------------------------------- HUD state
extern tf2::native::HudState g_hud;
extern bool g_hudTextTest;
extern bool g_hudTraceEnabled;

// Owned by main.cpp: the tray controls the HUD page writes readings into, and
// the renderer the layout is pushed at.
extern HWND g_uiHudPreview;
extern HWND g_uiHudScale;
extern HWND g_uiHudStatus;
extern tf2::native::Renderer* g_renderer;

// Owned by main.cpp (see above): the one hotkey table.
extern tf2::native::KeyBindTable g_keybinds;

// ---------------------------------------------------------------- HUD logic
std::string hudActionForKey(WPARAM key);
bool chooseHudFolder(HWND owner, std::filesystem::path& result);
std::string discoverHudLayout(const std::filesystem::path& root, std::string& detail);

// Prints the hud: settings/layout/rects/refuse/draw lines the gates read. A
// reading is printed even when the answer is "nothing happened".
void hudPublishTrace(HWND window);

// Where [hud] is persisted: the same ui.ini native_ui.cpp computes.
std::filesystem::path uiSettingsFilePath(std::string& status);

void hudApplyTextTest();
void hudApplyRects(HWND window);
void hudUpdateStatus(HWND window);
void hudImportInteractive(HWND window);
void hudTogglePreview(HWND window);
void hudResetToDefault(HWND window);

}  // namespace tf2::native::main_internal
