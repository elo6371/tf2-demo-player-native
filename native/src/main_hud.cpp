// T76 (2026-10-29): the T51 HUD product layer, moved verbatim out of main.cpp
// (main.cpp:1728-2112 before the move). The bodies, the comments and every
// hud: trace line are the shipped bytes; only the surrounding namespace and
// the state's linkage changed -- the three objects are defined here and declared
// extern in main_hud.h so main.cpp's argument scan and windowProc keep reaching
// the same instances.
//
// Moved as motion, not as a rewrite: no line of a function body below differs
// from the one in the pre-split main.cpp.

#include "main_hud.h"

#include <Windows.h>
#include <objbase.h>
#include <shlobj.h>

#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "hud_import.h"
#include "hud_state.h"
#include "keybinds.h"
#include "main_internal.h"
#include "native_renderer.h"
#include "native_ui.h"

namespace tf2::native::main_internal {

// ---------------------------------------------------------------------------
// T51: the custom HUD, at the product level.
//
// The split is deliberate and mirrors hud_import.h / hud_state.h: the pure rules
// (paths, documents, coordinates) live in hud_import.cpp and are asserted by
// hud_probe without a window; the settings round trip lives in hud_state.cpp;
// only the two things that need a window -- a folder picker and pushing
// rectangles at the renderer -- are here.
//
// Every state change funnels through hudPublishTrace(), which prints the hud:
// lines the gate reads. A reading is printed even when the answer is "nothing
// happened", because a gate that only sees lines when something works cannot
// tell "off" from "broken".
// ---------------------------------------------------------------------------

tf2::native::HudState g_hud;
// T67: --hud-text-test draws the fixed primitive list instead of the imported
// layout. A file-scope flag because both the argument scan and the resize path
// (hudApplyRects) need the same answer.
bool g_hudTextTest = false;
// The discovered document is kept in g_hud.settings().layout, which is the value
// that gets persisted -- a second copy here would be a second thing that can
// disagree with what the settings file says was imported.
bool g_hudTraceEnabled = true;


// The action a key press maps to, or empty. Modifiers come from GetKeyState
// rather than the message because WM_KEYDOWN does not carry them.
std::string hudActionForKey(WPARAM key) {
  tf2::native::KeyChord chord;
  chord.vk = static_cast<unsigned>(key);
  chord.ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
  chord.alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
  chord.shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
  const auto* action = g_keybinds.actionForKey(chord);
  if (!action) return {};
  return *action;
}

// A folder picker. GetOpenFileNameW cannot select a directory, which is the
// whole reason this is a second helper rather than a flag on the existing one.
bool chooseHudFolder(HWND owner, std::filesystem::path& result) {
  BROWSEINFOW browse{};
  browse.hwndOwner = owner;
  browse.lpszTitle = L"Select the folder that contains the HUD (the one with resource/ui)";
  browse.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
  PIDLIST_ABSOLUTE idList = SHBrowseForFolderW(&browse);
  if (!idList) return false;
  wchar_t path[MAX_PATH]{};
  const bool ok = SHGetPathFromIDListW(idList, path) != FALSE;
  // SHBrowseForFolderW returns a PIDLIST_ABSOLUTE allocated with CoTaskMemAlloc,
  // so it is freed with CoTaskMemFree and not with ILFree (which is for the
  // shell item's child pidls). Getting this wrong leaks one small block per
  // dialog; the gate opens the dialog zero times, so the leak would be invisible
  // in every reading -- stated here instead of discovered later.
  CoTaskMemFree(idList);
  if (!ok) return false;
  result = std::filesystem::path(path);
  return true;
}

// Which document inside the folder is the HUD. The candidates are ordered from
// most specific to least, and the answer is reported so a user whose folder has
// three .res files can see which one was taken.
std::string discoverHudLayout(const std::filesystem::path& root, std::string& detail) {
  std::error_code error;
  const char* candidates[] = {
    "resource/ui/hudlayout.res",
    "scripts/hudlayout.res",
    "ui/hudlayout.res",
    "hudlayout.res",
  };
  for (const char* candidate : candidates) {
    const auto file = root / std::filesystem::u8path(candidate);
    if (std::filesystem::is_regular_file(file, error)) {
      detail = std::string("candidate:") + candidate;
      return candidate;
    }
  }
  // Fall back to an enumeration: the single .res under resource/ui, then the
  // single .res anywhere under the folder. More than one is refused rather than
  // guessed -- picking the alphabetically first of fifteen panels is not "the
  // HUD", and a silently-wrong pick is exactly the kind of claim this task
  // forbids.
  std::vector<std::string> found;
  const auto collect = [&](const std::filesystem::path& directory, bool recursive) {
    std::error_code scanError;
    if (!std::filesystem::is_directory(directory, scanError)) return;
    if (recursive) {
      for (const auto& entry : std::filesystem::recursive_directory_iterator(directory, scanError)) {
        if (scanError) break;
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".res") continue;
        if (found.size() < 64u) found.push_back(entry.path().lexically_relative(root).generic_u8string());
      }
    } else {
      for (const auto& entry : std::filesystem::directory_iterator(directory, scanError)) {
        if (scanError) break;
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".res") continue;
        if (found.size() < 64u) found.push_back(entry.path().lexically_relative(root).generic_u8string());
      }
    }
  };
  collect(root / "resource" / "ui", false);
  if (found.size() == 1u) { detail = "unique:resource/ui"; return found.front(); }
  found.clear();
  collect(root, true);
  if (found.size() == 1u) { detail = "unique:recursive"; return found.front(); }
  detail = "ambiguous:" + std::to_string(found.size()) + "-documents";
  return {};
}

void hudPublishTrace(HWND window) {
  if (!g_hudTraceEnabled) return;
  const auto& settings = g_hud.settings();
  const auto unionRect = g_hud.unionRect();
  traceFmt("hud: settings enabled=%d scale=%d preview=%d root=%s layout=%s imported=%d",
    settings.enabled ? 1 : 0, settings.scalePercent, settings.preview ? 1 : 0,
    settings.root.empty() ? "-" : settings.root.c_str(),
    settings.layout.empty() ? "-" : settings.layout.c_str(),
    g_hud.imported() ? 1 : 0);
  traceFmt("hud: layout ok=%d document=%s panels=%zu refused=%zu nesting=%zu"
    " unknownKeys=%zu duplicates=%zu includes=%zu",
    g_hud.layout().ok ? 1 : 0,
    g_hud.layout().documentName.empty() ? "-" : g_hud.layout().documentName.c_str(),
    g_hud.layout().panels.size(), g_hud.layout().refusedPanels,
    g_hud.layout().nestingDepth, g_hud.layout().unknownKeys,
    g_hud.layout().duplicateNames, g_hud.layout().includeCount);
  // Three counts, named for what they measure. `resolved` counts geometry that
  // came out; `visible` counts what will actually be drawn; `invalid` counts the
  // panels whose geometry failed. Collapsing them is how "it resolved 5 panels"
  // and "it drew 4" become the same sentence.
  traceFmt("hud: rects resolved=%zu visible=%zu invalid=%zu union=%d,%d,%d,%d empty=%d",
    g_hud.resolvedPanels(), g_hud.visiblePanels(), g_hud.invalidPanels(),
    unionRect.x, unionRect.y, unionRect.w, unionRect.h, unionRect.empty ? 1 : 0);
  traceFmt("hud: refuse accepted=%d refused=%d lastReason=%s detail=%s",
    g_hud.acceptedImports(), g_hud.refusedImports(),
    tf2::native::hudRefusalName(g_hud.lastRefusal()),
    g_hud.lastRefusalDetail().empty() ? "-" : g_hud.lastRefusalDetail().c_str());
  if (g_hud.repaired()) traceFmt("hud: repaired detail=%s", g_hud.repairDetail().c_str());
  // The draw reading. It is emitted on every publish so a run whose HUD is off
  // says so out loud instead of leaving the gate to infer it from a missing line.
  if (g_renderer) {
    traceFmt("hud: draw drawn=%zu rects=%zu ordinal=%zu scissor=%d region=%d,%d,%d,%d",
      g_renderer->hudDrawnRects(), g_renderer->hudRectCount(),
      g_renderer->hudPassOrdinal(), g_renderer->hudScissorApplied() ? 1 : 0,
      g_renderer->hudLastRegion().x, g_renderer->hudLastRegion().y,
      g_renderer->hudLastRegion().w, g_renderer->hudLastRegion().h);
  }
  (void)window;
}


// T51: where ui.ini is. native_ui.cpp's uiSettingsPath() is in an anonymous
// namespace inside that translation unit, and the [hud] section has to go into
// the SAME file as [ui] -- a second path computed here that drifted from that
// one would produce a HUD that never persists. It mirrors the same rule and is
// cross-checked by the gate, which reads the path out of the trace and checks the
// file exists.
// T51: where ui.ini is. The path itself lives in native_ui.cpp (that file owns
// it, and the [hud] section has to be in the SAME file as [ui]); this wrapper
// only turns "no path" into a reading, because a silent empty path would look
// like a HUD that simply did not persist.
std::filesystem::path uiSettingsFilePath(std::string& status) {
  const auto path = tf2::native::uiSettingsPath();
  status = path.empty() ? "no-ui-settings-path" : "ok";
  return path;
}

// T67: the fixed primitive list the --hud-text-test run draws. It is a probe,
// not a layout: the coordinates are constants so a gate can compare the frame
// against the same numbers. The last character is non-ASCII on purpose -- it has
// no glyph, so it must take the fallback cell and be counted as unmapped, which
// is what makes "unmapped text has a visible placeholder" a reading.
void hudApplyTextTest() {
  if (!g_renderer) return;
  std::vector<tf2::native::Renderer::HudPrimitive> primitives;
  tf2::native::Renderer::HudPrimitive text;
  text.kind = tf2::native::Renderer::HudPrimitiveKind::Text;
  text.x = 40.0f;
  text.y = 60.0f;
  // 22 codepoints (21 ASCII + one non-ASCII), 8x8 cells at scale 2.
  text.w = 22.0f * 8.0f * 2.0f;
  text.h = 8.0f * 2.0f;
  text.scale = 2.0f;
  text.colour[0] = 1.0f; text.colour[1] = 1.0f; text.colour[2] = 1.0f; text.colour[3] = 1.0f;
  text.text = "TF2 0:00 RED 12 BLU 7\xE4\xB8\xAD";
  primitives.push_back(text);
  tf2::native::Renderer::HudPrimitive panel;
  panel.kind = tf2::native::Renderer::HudPrimitiveKind::Rect;
  panel.x = 40.0f; panel.y = 120.0f; panel.w = 240.0f; panel.h = 40.0f;
  panel.filled = true;
  panel.colour[0] = 0.12f; panel.colour[1] = 0.28f; panel.colour[2] = 0.65f; panel.colour[3] = 1.0f;
  primitives.push_back(panel);
  tf2::native::Renderer::HudPrimitive red;
  red.kind = tf2::native::Renderer::HudPrimitiveKind::Circle;
  red.x = 60.0f; red.y = 200.0f; red.w = 24.0f; red.h = 24.0f;
  red.colour[0] = 1.0f; red.colour[1] = 0.0f; red.colour[2] = 0.0f; red.colour[3] = 1.0f;
  primitives.push_back(red);
  tf2::native::Renderer::HudPrimitive blue;
  blue.kind = tf2::native::Renderer::HudPrimitiveKind::Circle;
  blue.x = 120.0f; blue.y = 200.0f; blue.w = 24.0f; blue.h = 24.0f;
  blue.colour[0] = 0.1f; blue.colour[1] = 0.4f; blue.colour[2] = 1.0f; blue.colour[3] = 1.0f;
  primitives.push_back(blue);
  g_renderer->setHudPrimitives(primitives);
  g_renderer->setHudEnabled(true);
}

// Pushes the current layout at the renderer for the window's current client size.
void hudApplyRects(HWND window) {
  if (!g_renderer) return;
  if (g_hudTextTest) { hudApplyTextTest(); return; }
  RECT client{};
  if (!window || !GetClientRect(window, &client)) { client.right = 1280; client.bottom = 720; }
  const float dpiScale = window ? static_cast<float>(GetDpiForWindow(window)) / 96.0f : 1.0f;
  g_hud.layoutFor(client.right, client.bottom, dpiScale);
  std::vector<tf2::native::Renderer::HudDrawRect> rects;
  // Colors are per-panel and fixed: this build rasterises no font and no image,
  // so the colour is the only thing that distinguishes one panel from another
  // and a stable palette is what lets a reviewer name a block from a screenshot.
  static const float kPalette[][4] = {
    {0.16f, 0.35f, 0.55f, 0.72f},
    {0.20f, 0.50f, 0.28f, 0.72f},
    {0.55f, 0.35f, 0.14f, 0.72f},
    {0.45f, 0.20f, 0.45f, 0.72f},
  };
  std::size_t index = 0;
  for (const auto& rect : g_hud.rects()) {
    // Hidden panels keep their geometry but are not drawn; the index is advanced
    // anyway so a panel's colour depends on the document, not on which of its
    // siblings happen to be hidden in this frame.
    if (!rect.valid || !rect.visible) { ++index; continue; }
    tf2::native::Renderer::HudDrawRect draw;
    draw.left = static_cast<float>(rect.x);
    draw.top = static_cast<float>(rect.y);
    draw.right = static_cast<float>(rect.x + rect.w);
    draw.bottom = static_cast<float>(rect.y + rect.h);
    const float* colour = kPalette[index % 4u];
    draw.colour[0] = colour[0]; draw.colour[1] = colour[1];
    draw.colour[2] = colour[2]; draw.colour[3] = colour[3];
    rects.push_back(draw);
    ++index;
  }
  g_renderer->setHudRects(rects);
  // The pass draws only when the user enabled the HUD AND there is something to
  // draw. Both conditions are visible in the trace above.
  g_renderer->setHudEnabled(g_hud.settings().enabled && !rects.empty());
}

void hudUpdateStatus(HWND window) {
  if (!g_uiHudStatus) return;
  const auto& settings = g_hud.settings();
  std::wstring text = L"HUD: none";
  if (g_hud.imported()) {
    text = L"HUD: " + std::wstring(g_hud.layout().documentName.begin(), g_hud.layout().documentName.end())
      + L" | panels=" + std::to_wstring(g_hud.visiblePanels())
      + L" | scale=" + std::to_wstring(settings.scalePercent) + L"%"
      + (settings.enabled ? L" | on" : L" | off");
    if (g_hud.layout().refusedPanels != 0 || g_hud.invalidPanels() != 0) {
      text += L" | refused=" + std::to_wstring(g_hud.layout().refusedPanels + g_hud.invalidPanels());
    }
  } else if (g_hud.lastRefusal() != tf2::native::HudRefusal::None) {
    const char* reason = tf2::native::hudRefusalName(g_hud.lastRefusal());
    text = L"HUD refused: " + std::wstring(reason, reason + std::strlen(reason));
  }
  SetWindowTextW(g_uiHudStatus, text.c_str());
  if (g_uiHudPreview) {
    SendMessageW(g_uiHudPreview, BM_SETCHECK, settings.preview ? BST_CHECKED : BST_UNCHECKED, 0);
  }
  if (g_uiHudScale) SendMessageW(g_uiHudScale, TBM_SETPOS, TRUE, settings.scalePercent);
  if (window) InvalidateRect(window, nullptr, FALSE);
}

void hudImportInteractive(HWND window) {
  std::filesystem::path folder;
  if (!chooseHudFolder(window, folder)) {
    traceFmt("hud: import action=cancelled");
    return;
  }
  std::string detail;
  const std::string layout = discoverHudLayout(folder, detail);
  traceFmt("hud: import root=%s discover=%s", folder.u8string().c_str(), detail.c_str());
  if (layout.empty()) {
    // Nothing to import: report the discovery result as the refusal rather than
    // silently doing nothing, because "I clicked Import and nothing happened" is
    // the failure mode this whole feature exists to avoid.
    const bool ambiguous = detail.compare(0, 10, "ambiguous:") == 0;
    traceFmt("hud: import refused reason=%s", ambiguous ? "ambiguous-document" : "missing");
    if (g_uiHudStatus) {
      g_hud.noteDiscoveryRefusal(ambiguous ? "ambiguous-document" : "missing");
      SetWindowTextW(g_uiHudStatus, ambiguous
        ? L"HUD refused: several documents; pick a folder with one"
        : L"HUD refused: no .res document in that folder");
    }
    return;
  }
  RECT client{};
  GetClientRect(window, &client);
  const float dpiScale = static_cast<float>(GetDpiForWindow(window)) / 96.0f;
  tf2::native::HudImportRequest request;
  request.root = folder;
  request.layout = layout;
  const auto result = g_hud.import(request, client.right, client.bottom, dpiScale);
  traceFmt("hud: import result=%s document=%s", tf2::native::hudApplyResultName(result),
    layout.c_str());
  if (result == tf2::native::HudApplyResult::Applied) {
    hudApplyRects(window);
  }
  hudUpdateStatus(window);
  hudPublishTrace(window);
}

void hudTogglePreview(HWND window) {
  const bool checked = g_uiHudPreview
    && SendMessageW(g_uiHudPreview, BM_GETCHECK, 0, 0) == BST_CHECKED;
  // The checkbox is the feature switch, not a second "enabled": it drives
  // hud.enabled, which is the one key the contract gives this feature.
  g_hud.setEnabled(checked);
  g_hud.setPreview(checked);
  traceFmt("hud: toggle enabled=%d", checked ? 1 : 0);
  hudApplyRects(window);
  hudUpdateStatus(window);
  hudPublishTrace(window);
}

void hudResetToDefault(HWND window) {
  const auto result = g_hud.resetToDefault();
  traceFmt("hud: reset result=%s", tf2::native::hudApplyResultName(result));
  // The renderer is told there is nothing to draw BEFORE the status line is
  // updated, so a reset cannot leave a frame with a stale HUD on it.
  if (g_renderer) {
    g_renderer->setHudRects({});
    g_renderer->setHudEnabled(false);
  }
  hudUpdateStatus(window);
  hudPublishTrace(window);
}

}  // namespace tf2::native::main_internal
