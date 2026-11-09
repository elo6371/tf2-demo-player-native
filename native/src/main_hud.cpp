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

#include <cmath>
#include <cstdio>
#include <cstdlib>
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

namespace {

// T97: whether the live readout has primitives in the renderer right now. It is
// declared up here because hudApplyRects -- which comes first in this file -- needs
// it for the pass-enable rule; the rest of the readout's state is defined with the
// readout itself, further down.
bool g_hudLiveActive = false;
// T97: the readout's own primitive list, kept here for the same reason: the
// capture path (main.cpp, right before the frame it writes) and every WM_SIZE
// re-run the layout pass, and setHudRects REPLACES the renderer's list. Without a
// copy the text would be deleted on exactly the frame a gate captures, so the
// layout pass merges this list back in instead of overwriting it.
std::vector<tf2::native::Renderer::HudPrimitive> g_hudLivePrimitives;

}  // namespace

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
  if (g_hudLiveActive) {
    // setHudRects() replaces the whole primitive list, so publishing the layout
    // alone would delete the readout. Build the same Rect primitives it would and
    // append the readout's text after them: layout blocks underneath, text on top.
    std::vector<tf2::native::Renderer::HudPrimitive> merged;
    merged.reserve(rects.size() + g_hudLivePrimitives.size());
    for (const auto& rect : rects) {
      tf2::native::Renderer::HudPrimitive primitive;
      primitive.kind = tf2::native::Renderer::HudPrimitiveKind::Rect;
      primitive.x = rect.left;
      primitive.y = rect.top;
      primitive.w = rect.right - rect.left;
      primitive.h = rect.bottom - rect.top;
      for (int channel = 0; channel < 4; ++channel) primitive.colour[channel] = rect.colour[channel];
      primitive.filled = false;
      merged.push_back(primitive);
    }
    merged.insert(merged.end(), g_hudLivePrimitives.begin(), g_hudLivePrimitives.end());
    g_renderer->setHudPrimitives(merged);
  } else {
    g_renderer->setHudRects(rects);
  }
  // The pass draws only when there is something to draw: the user enabled the
  // imported layout AND the document produced rectangles, or the live readout has
  // primitives in the renderer. Both terms are readings, not assumptions -- the
  // trace above prints enabled= and the last hud: live line prints the readout, so
  // "the pass was on" can be checked against the numbers that asked for it.
  g_renderer->setHudEnabled((g_hud.settings().enabled && !rects.empty()) || g_hudLiveActive);
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

// ------------------------------------------------------------- T97: live readout
//
// WHY THIS EXISTS. Every number the product drew before this was a probe: the
// --hud-text-test list is constants, and the panels come from a .res document a
// user has to import by hand. A player who just launches the demo sees a picture
// and no readings at all. This is the frame-loop half of the answer: it turns the
// state main.cpp already computes into three lines of text and hands them to the
// renderer as HUD primitives.
//
// EVERY VALUE IS READ, NONE IS CHOSEN. The clock comes from the CTeamRoundTimer
// entity when the demo carries one and from the transport (tick / tickRate) when
// it does not, and the source is published so "8:42" is never ambiguous. The
// score comes from the two CTeam entities (classId 125 on this build; the class is
// matched by schema NAME, not by that number, because a hard-coded id is exactly
// the sort of accidental constant this project keeps having to retire). The
// target's health/class/team come from readPlaybackHud, which is the same reader
// the rest of the product uses.
//
// WHAT IS NOT KNOWN IS SAID OUT LOUD. A player name is userinfo (svc_UserInfo),
// and nothing in this decoder reads userinfo -- evidence/t63-sourcetv/FIELD_TABLE.md:43
// records it as gap G7, evidence/t68-events/GAME-EVENT-SPEC.md:435 as T64 G5, and
// evidence/t70-t72/DESIGN.md:1126 states the name is not obtainable. The third
// line therefore carries "NAME ?" and the trace carries
// name=unavailable reason=no-userinfo, with a counter. A placeholder that looked
// like a name would be worse than the question mark.
namespace {

constexpr float kHudLiveX = 24.0f;
constexpr float kHudLiveY = 24.0f;
constexpr float kHudLiveLineHeight = 22.0f;
constexpr float kHudLiveScale = 2.0f;
constexpr float kHudLiveCell = 8.0f * kHudLiveScale;

// g_hudLiveActive is defined near the top of the file (hudApplyRects reads it).
// g_hudLiveOffReported covers both ways of being off (a switch or an evidence run):
// one process has one of them, and each is published exactly once.
bool g_hudLiveOffReported = false;
std::string g_hudLiveLastKey;
std::size_t g_hudLiveUpdates = 0;
std::size_t g_hudLiveSkips = 0;
std::size_t g_hudLiveNameMissing = 0;
std::size_t g_hudLiveTargetAbsent = 0;
std::size_t g_hudLiveScoreMissing = 0;
std::size_t g_hudLiveTimerFallback = 0;
// T97: the counters above are read off the PUBLISH line, and that line is only
// written when the reading changes. So a run that publishes once publishes
// skips=0 even if it called this function a thousand times: the number is a
// snapshot taken at the first publish, not a tally. The skip half of the rule
// ("an unchanged frame is not re-uploaded") therefore needs a reading written at
// the END of the run, which is what hudLiveSummaryAtExit is for -- registered
// once, on the first call, and written from std::atexit so the trace file
// (never closed by openTrace) is still open.
std::size_t g_hudLiveCalls = 0;
std::size_t g_hudLivePublished = 0;
bool g_hudLiveSummaryRegistered = false;
std::string g_hudLiveLastSource;
std::string g_hudLiveLastLines[3];

void hudLiveSummaryAtExit();

void hudLiveRegisterSummary() {
  if (g_hudLiveSummaryRegistered) return;
  g_hudLiveSummaryRegistered = true;
  std::atexit(&hudLiveSummaryAtExit);
}

// Written once, at process exit. Every number is a tally over the whole run, so
// "updates" and "skips" here cannot be a snapshot of one frame, and the last
// reading is repeated so a run whose text never changed still says what it showed.
void hudLiveSummaryAtExit() {
  traceFmt("hud: live summary calls=%zu updates=%zu skips=%zu published=%zu"
    " nameMissing=%zu targetAbsent=%zu scoreMissing=%zu timerFallback=%zu"
    " lastSource=%s lastText1=\"%s\" lastText2=\"%s\" lastText3=\"%s\"",
    g_hudLiveCalls, g_hudLiveUpdates, g_hudLiveSkips, g_hudLivePublished,
    g_hudLiveNameMissing, g_hudLiveTargetAbsent, g_hudLiveScoreMissing,
    g_hudLiveTimerFallback, g_hudLiveLastSource.c_str(),
    g_hudLiveLastLines[0].c_str(), g_hudLiveLastLines[1].c_str(),
    g_hudLiveLastLines[2].c_str());
}

struct HudLiveReading {
  bool roundTimerKnown = false;
  double roundTimerSeconds = 0.0;
  bool roundTimerPaused = false;
  bool redKnown = false;
  std::int32_t red = 0;
  bool bluKnown = false;
  std::int32_t blu = 0;
};

struct HudLiveText {
  std::string time;
  std::string score;
  std::string target;
  std::string source;
};

// The property lookup rule is the one playback_hud.cpp already uses: an exact
// name, else a name that is the dotted tail of the stored one (a flattened
// table stores "DT_Team.m_iScore", not "m_iScore"). It is repeated here, and not
// shared, because the original lives in that file's anonymous namespace; a
// second copy of four lines is a smaller risk than exporting a helper that only
// two call sites want. The gate cross-checks the numbers this picks against the
// ones readPlaybackHud reports for the same frame, so the two rules cannot drift
// without a red assertion.
const tf2::native::EntityPropertyValue* hudLiveFind(const tf2::native::EntityState& state,
    const char* key) {
  const std::string token = key;
  const tf2::native::EntityPropertyValue* exact = nullptr;
  const tf2::native::EntityPropertyValue* dotted = nullptr;
  for (const auto& entry : state.properties) {
    if (entry.first == token) {
      exact = &entry.second;
    } else if (entry.first.size() > token.size()
        && entry.first[entry.first.size() - token.size() - 1] == '.'
        && entry.first.compare(entry.first.size() - token.size(), token.size(), token) == 0) {
      dotted = &entry.second;
    }
  }
  return exact ? exact : dotted;
}

bool hudLiveInt(const tf2::native::EntityState& state, const char* key, std::int32_t& out) {
  const tf2::native::EntityPropertyValue* value = hudLiveFind(state, key);
  if (!value || value->type != tf2::native::SendPropType::Int) return false;
  out = static_cast<std::int32_t>(value->intValue);
  return true;
}

bool hudLiveFloat(const tf2::native::EntityState& state, const char* key, float& out) {
  const tf2::native::EntityPropertyValue* value = hudLiveFind(state, key);
  if (!value || value->type != tf2::native::SendPropType::Float) return false;
  if (!std::isfinite(value->x)) return false;
  out = value->x;
  return true;
}

// classId -> schema name, by id. The vector is indexed by class id on every
// corpus this product has read, but the lookup compares ids anyway: an index that
// happens to line up today is not a contract.
const std::string* hudLiveSchemaName(const tf2::native::DemoNetworkSummary& summary,
    std::int32_t classId) {
  if (classId < 0) return nullptr;
  const auto wanted = static_cast<std::uint16_t>(classId);
  for (const auto& schema : summary.serverClassSchemas) {
    if (schema.id == wanted) return &schema.name;
  }
  return nullptr;
}

const char* hudLiveClassName(std::int32_t playerClass) {
  switch (playerClass) {
    case 1: return "SCOUT";
    case 2: return "SNIPER";
    case 3: return "SOLDIER";
    case 4: return "DEMOMAN";
    case 5: return "MEDIC";
    case 6: return "ENGINEER";
    case 7: return "HEAVY";
    case 8: return "PYRO";
    case 9: return "SPY";
    default: return "?";
  }
}

const char* hudLiveTeamName(std::int32_t team) {
  if (team == 2) return "RED";
  if (team == 3) return "BLU";
  return "?";
}

// M:SS. A clock is the one number where rounding has to be decided once: 0.5s
// rounds up so a countdown never displays the second it has already left.
std::string hudLiveClock(double seconds) {
  if (!(seconds > 0.0)) seconds = 0.0;
  const long total = static_cast<long>(seconds + 0.5);
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%ld:%02ld", total / 60, total % 60);
  return buffer;
}

HudLiveReading hudLiveRead(const std::vector<tf2::native::EntityState>& states,
    const tf2::native::DemoNetworkSummary& summary) {
  HudLiveReading reading;
  for (const auto& state : states) {
    const std::string* name = hudLiveSchemaName(summary, state.classId);
    if (!name) continue;
    if (*name == "CTeamRoundTimer") {
      float remaining = 0.0f;
      if (!hudLiveFloat(state, "m_flTimeRemaining", remaining)) continue;
      std::int32_t paused = 0;
      reading.roundTimerKnown = true;
      reading.roundTimerSeconds = static_cast<double>(remaining);
      reading.roundTimerPaused = hudLiveInt(state, "m_bTimerPaused", paused) && paused != 0;
    } else if (*name == "CTeam") {
      std::int32_t team = 0;
      std::int32_t score = 0;
      if (!hudLiveInt(state, "m_iTeamNum", team)) continue;
      if (!hudLiveInt(state, "m_iScore", score)) continue;
      if (team == 2) {
        reading.redKnown = true;
        reading.red = score;
      } else if (team == 3) {
        reading.bluKnown = true;
        reading.blu = score;
      }
    }
  }
  return reading;
}

HudLiveText hudLiveCompose(const tf2::native::main_internal::PlaybackState& playback,
    const tf2::native::PlaybackHudState& target, const HudLiveReading& reading) {
  HudLiveText text;
  if (reading.roundTimerKnown) {
    text.source = "round-timer";
    text.time = "TIME ";
    text.time += hudLiveClock(reading.roundTimerSeconds);
    if (reading.roundTimerPaused) text.time += " PAUSED";
  } else if (playback.tickRate > 0.0 && playback.endTick > 0) {
    // The transport fallback is the same arithmetic the title bar uses. It is
    // labelled "playback-clock" in the trace precisely because it is NOT the
    // round clock: it counts the recording down, not the round.
    text.source = "playback-clock";
    text.time = "TIME ";
    text.time += hudLiveClock((static_cast<double>(playback.endTick - playback.tick))
      / playback.tickRate);
    if (playback.paused) text.time += " PAUSED";
  } else {
    text.source = "none";
    text.time = "TIME ?";
  }
  text.score = "RED ";
  text.score += reading.redKnown ? std::to_string(reading.red) : "?";
  text.score += "  BLU ";
  text.score += reading.bluKnown ? std::to_string(reading.blu) : "?";
  text.target = "NAME ?";  // userinfo is not decoded: see the block comment above
  text.target += "  HP ";
  text.target += target.healthKnown ? std::to_string(target.health) : "?";
  text.target += "  ";
  text.target += target.classKnown ? hudLiveClassName(target.playerClass) : "?";
  text.target += "  ";
  text.target += target.teamKnown ? hudLiveTeamName(target.team) : "?";
  return text;
}

}  // namespace

// T97, lead ruling 2026-11-09: a readout that is on by default must not deform the
// frames this tree treats as evidence. Every pixel gate here launches the product
// with --capture-frame and compares the captured bitmap byte-for-byte against a pin
// committed in the repository; three lines of white text on one of those frames
// would turn gates red that are measuring world geometry, weapons or materials --
// and the alternative, teaching twenty-odd scripts a new switch, would mean editing
// files that two other lanes are editing in this same window. So the readout has
// three states, and the mode is decided from PARSED flags only:
//
//   --hud-off    never draw. reason=cli-hud-off. The strongest switch wins.
//   --hud-live   always draw, mode=forced. This is how a gate asks for the pixels
//                on purpose; hud-live-check.sh runs its whole positive half so.
//   neither      an evidence run (--capture-frame armed) suppresses the readout and
//                publishes reason=evidence-run once; a plain interactive launch
//                draws it with mode=interactive.
//
// --hud-text-test outranks all three: the T67 probe owns the primitive list.
void hudApplyLive(HWND window, bool evidenceRun, bool hudOff, bool hudLive,
    const tf2::native::main_internal::PlaybackState& playback,
    const tf2::native::PlaybackHudState& target,
    const std::vector<tf2::native::EntityState>& states,
    const tf2::native::DemoNetworkSummary& summary) {
  (void)window;
  if (!g_renderer) return;
  // While the T67 probe runs it owns the primitive list. Two writers would make
  // the probe's pixel assertions depend on whether this build also has a readout.
  if (g_hudTextTest) return;
  hudLiveRegisterSummary();
  ++g_hudLiveCalls;
  // The suppression is decided by the run's mode, never by "the last frame drew
  // something": a capture run has exactly one sampled frame, so a readout that was
  // painted on frame N-1 and cleared on frame N would still leave nothing on the
  // frame under comparison -- but only by accident. Here it is never painted.
  const bool suppressed = hudOff || (!hudLive && evidenceRun);
  if (suppressed) {
    if (!g_hudLiveOffReported) {
      g_hudLiveOffReported = true;
      traceFmt("hud: live enabled=0 reason=%s drawn=0",
        hudOff ? "cli-hud-off" : "evidence-run");
    }
    if (g_hudLiveActive) {
      g_hudLiveActive = false;
      g_hudLivePrimitives.clear();
      g_renderer->setHudPrimitives({});
    }
    return;
  }
  const HudLiveReading reading = hudLiveRead(states, summary);
  // Four counters, one per thing that can be missing from a frame. A frame that
  // shows "?" must be countable, or the question mark is decoration.
  if (!reading.roundTimerKnown) ++g_hudLiveTimerFallback;
  if (!reading.redKnown || !reading.bluKnown) ++g_hudLiveScoreMissing;
  if (!target.healthKnown && !target.teamKnown && !target.classKnown) ++g_hudLiveTargetAbsent;
  ++g_hudLiveNameMissing;  // every frame that asks for a name and cannot have one
  const HudLiveText text = hudLiveCompose(playback, target, reading);
  std::string key = text.time;
  key += '\n';
  key += text.score;
  key += '\n';
  key += text.target;
  key += '\n';
  key += text.source;
  if (key == g_hudLiveLastKey) {
    ++g_hudLiveSkips;
    return;
  }
  g_hudLiveLastKey = key;
  ++g_hudLiveUpdates;
  ++g_hudLivePublished;
  g_hudLiveLastSource = text.source;
  g_hudLiveLastLines[0] = text.time;
  g_hudLiveLastLines[1] = text.score;
  g_hudLiveLastLines[2] = text.target;

  const std::string lines[3] = { text.time, text.score, text.target };
  std::vector<tf2::native::Renderer::HudPrimitive> primitives;
  std::size_t glyphs = 0;
  float widest = 0.0f;
  for (std::size_t index = 0; index < 3; ++index) {
    tf2::native::Renderer::HudPrimitive primitive;
    primitive.kind = tf2::native::Renderer::HudPrimitiveKind::Text;
    primitive.x = kHudLiveX;
    primitive.y = kHudLiveY + kHudLiveLineHeight * static_cast<float>(index);
    // Only printable ASCII is composed, so bytes == codepoints here. The renderer
    // still walks the UTF-8 properly; this arithmetic is for the region and the
    // expected glyph count the trace publishes.
    primitive.w = static_cast<float>(lines[index].size()) * kHudLiveCell;
    primitive.h = kHudLiveCell;
    primitive.scale = kHudLiveScale;
    primitive.colour[0] = 1.0f;
    primitive.colour[1] = 1.0f;
    primitive.colour[2] = 1.0f;
    primitive.colour[3] = 1.0f;
    primitive.text = lines[index];
    glyphs += lines[index].size();
    if (primitive.w > widest) widest = primitive.w;
    primitives.push_back(primitive);
  }
  g_hudLivePrimitives = primitives;
  g_renderer->setHudPrimitives(primitives);
  g_hudLiveActive = true;
  // The pass is enabled by the readout itself: a plain launch has no imported
  // layout, so hudApplyRects' "panels AND enabled" rule would otherwise leave the
  // readout invisible until somebody imported a .res file. hudApplyRects was
  // taught the same fact (it ORs g_hudLiveActive in), so a resize mid-run cannot
  // switch the readout off.
  g_renderer->setHudEnabled(true);
  const int regionX = static_cast<int>(kHudLiveX);
  const int regionY = static_cast<int>(kHudLiveY);
  const int regionW = static_cast<int>(widest);
  const int regionH = static_cast<int>(kHudLiveLineHeight * 2.0f + kHudLiveCell);
  // reason= says why this line exists (the reading changed); nameReason= says why
  // the name field is a question mark. Two different questions, so two fields --
  // folding them into one "reason" would have made either one unreadable.
  traceFmt("hud: live tick=%d enabled=1 mode=%s source=%s"
    " text1=\"%s\" text2=\"%s\" text3=\"%s\""
    " glyphs=%zu region=%d,%d,%d,%d updates=%zu skips=%zu name=unavailable"
    " reason=text-changed nameReason=no-userinfo nameMissing=%zu targetAbsent=%zu scoreMissing=%zu"
    " timerFallback=%zu red=%d blu=%d paused=%d",
    playback.tick, hudLive ? "forced" : "interactive", text.source.c_str(),
    text.time.c_str(), text.score.c_str(),
    text.target.c_str(), glyphs, regionX, regionY, regionW, regionH,
    g_hudLiveUpdates, g_hudLiveSkips, g_hudLiveNameMissing, g_hudLiveTargetAbsent,
    g_hudLiveScoreMissing, g_hudLiveTimerFallback,
    reading.redKnown ? reading.red : -1, reading.bluKnown ? reading.blu : -1,
    playback.paused ? 1 : 0);
}

}  // namespace tf2::native::main_internal
