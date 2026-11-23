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

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "hud_import.h"
#include "hud_state.h"
#include "hud_panel.h"
#include "keybinds.h"
#include "minimap.h"
#include "minimap_linework.h"
#include "map_overview.h"
#include "main_internal.h"
#include "native_renderer.h"
#include "native_ui.h"
#include "timeline_hud.h"

// ---------------------------------------------------------------------------
// E9 (2026-11-13): the minimap panel's inputs.
//
// main.cpp hands the panel the camera pose it is about to draw, as a value. Nothing
// here points back into the frame loop, so the panel cannot outlive the tick that
// produced it. Two accessors keep the storage private: a translation unit that never
// pushed a frame (a run without --hud-minimap) is indistinguishable from one that
// never linked the panel at all.
// ---------------------------------------------------------------------------
namespace tf2::native {

namespace {
MinimapHudFrame g_minimapHudFrame;
bool g_minimapHudFramePushed = false;
}  // namespace

void minimapHudSetFrame(const MinimapHudFrame& frame) {
  g_minimapHudFrame = frame;
  g_minimapHudFramePushed = true;
}

bool minimapHudFramePushed() { return g_minimapHudFramePushed; }

const MinimapHudFrame& minimapHudFrameRef() { return g_minimapHudFrame; }

// N14/U3a: the basemap geometry, pushed once per run and never mutated after. The
// counter rather than a bool, because the same process can load a second demo --
// the geometry then changes even though "something was pushed" stays true, and a
// cached rectangle list keyed on a bool would keep drawing the first map's floors.
namespace {
std::vector<MinimapFloorTriangle> g_minimapFloorGeometry;
std::size_t g_minimapFloorGeometryRevision = 0;
}  // namespace

void minimapHudSetFloorGeometry(std::vector<MinimapFloorTriangle> triangles) {
  g_minimapFloorGeometry = std::move(triangles);
  ++g_minimapFloorGeometryRevision;
}

std::size_t minimapHudFloorGeometryRevision() { return g_minimapFloorGeometryRevision; }

const std::vector<MinimapFloorTriangle>& minimapHudFloorGeometryRef() {
  return g_minimapFloorGeometry;
}

// 2026-11-22: --hud-minimap-bounds table|bsp. false (table) is the default and
// keeps an untabulated map at bounds-not-tabulated, byte-identical to before.
namespace {
bool g_minimapBoundsFromFloor = false;
}  // namespace

void minimapHudSetBoundsFromFloor(bool enabled) { g_minimapBoundsFromFloor = enabled; }

bool minimapHudBoundsFromFloor() { return g_minimapBoundsFromFloor; }

namespace {
bool g_minimapAvoidStatus = false;
}  // namespace

void minimapHudSetAvoidStatus(bool enabled) { g_minimapAvoidStatus = enabled; }

bool minimapHudAvoidStatus() { return g_minimapAvoidStatus; }

}  // namespace tf2::native

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
// The panel shapes (minimap / map-overview) are stored apart from the text: on a
// window resize only the text has to be re-centred, and the panel is recomposed
// by the next hudApplyLive with the new client size.
std::vector<tf2::native::Renderer::HudPrimitive> g_hudLiveExtraPrimitives;
// The three readout lines, kept so a resize can rebuild their primitives without
// waiting for the next reading (a captured frame can arrive first).
std::string g_hudLiveLastLines[3];
// Where the heading goes, in client pixels. Lines 1-2 (time, score) are centred
// on the client; line 3 (name/HP) stays at the left margin because it is the
// watched player's reading, not the score, and the user asked only for the time
// and score to sit at the top centre. Published so the gate can assert it.
struct HudLiveLayout {
  int lineX[3] = {0, 0, 0};
  int lineW[3] = {0, 0, 0};
  int headerX = 0;
  int headerW = 0;
  int nameX = 0;
  int nameW = 0;
  int regionX = 0;
  int regionW = 0;
};
HudLiveLayout hudLiveLayoutFor(HWND window, const std::string lines[3]);

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
// F0 (2026-11-23): --hud-font-test, the DirectWrite text card (hudApplyFontTest).
bool g_hudFontTest = false;
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

// F0 (2026-11-23): the --hud-font-test card. A layout probe for the DirectWrite
// text path, built like the broadcast HUD's team column (docs/UI-SHELL-DESIGN
// section 7) so the picture answers "does it read like the reference": a header,
// a medic row, player rows with right-aligned health (normal / overheal / low),
// a dead row with a respawn timer, an ellipsized long name, a Cyrillic and a CJK
// name, and a size ladder. Fixed coordinates and fixed strings, like the T67
// card above, so two runs on one machine draw the same frame. One trace line
// per run with the engine's own readings.
void hudApplyFontTest() {
  if (!g_renderer) return;
  using tf2::native::HudTextAlign;
  using tf2::native::HudTextFont;
  using Primitive = tf2::native::Renderer::HudPrimitive;
  using Kind = tf2::native::Renderer::HudPrimitiveKind;
  std::vector<Primitive> primitives;
  const auto rect = [&](float x, float y, float w, float h, float r, float g, float b, float a) {
    Primitive p;
    p.kind = Kind::Rect;
    p.filled = true;
    p.x = x; p.y = y; p.w = w; p.h = h;
    p.colour[0] = r; p.colour[1] = g; p.colour[2] = b; p.colour[3] = a;
    primitives.push_back(p);
  };
  const auto text = [&](const char* utf8, HudTextFont font, float px, float x, float y,
      HudTextAlign align, float maxWidth, float r, float g, float b) {
    Primitive p;
    p.kind = Kind::Text;
    p.font = font;
    p.pixelSize = px;
    p.align = align;
    p.maxWidth = maxWidth;
    p.x = x; p.y = y; p.w = 0.0f; p.h = 0.0f;
    p.colour[0] = r; p.colour[1] = g; p.colour[2] = b; p.colour[3] = 1.0f;
    p.text = utf8;
    primitives.push_back(p);
  };
  // Header strip.
  rect(300.0f, 0.0f, 680.0f, 38.0f, 0.055f, 0.063f, 0.075f, 0.82f);
  rect(560.0f, 0.0f, 40.0f, 38.0f, 0.235f, 0.490f, 0.651f, 1.0f);
  rect(760.0f, 0.0f, 40.0f, 38.0f, 0.710f, 0.216f, 0.227f, 1.0f);
  text("Harbor Lights", HudTextFont::UiSemiBold, 22.0f, 548.0f, 6.0f, HudTextAlign::Right, 240.0f, 0.50f, 0.71f, 0.85f);
  text("1", HudTextFont::UiBold, 26.0f, 580.0f, 4.0f, HudTextAlign::Center, 0.0f, 1.0f, 1.0f, 1.0f);
  text("4:12", HudTextFont::UiSemiBold, 24.0f, 680.0f, 5.0f, HudTextAlign::Center, 0.0f, 1.0f, 1.0f, 1.0f);
  text("0", HudTextFont::UiBold, 26.0f, 780.0f, 4.0f, HudTextAlign::Center, 0.0f, 1.0f, 1.0f, 1.0f);
  text("Quarry Unit", HudTextFont::UiSemiBold, 22.0f, 812.0f, 6.0f, HudTextAlign::Left, 240.0f, 0.90f, 0.45f, 0.46f);
  // Team column, BLU, rows 25 px with a 2 px gap.
  struct Row { const char* name; const char* hp; float fill; int tone; bool dead; };
  const Row rows[] = {
    {"Medi Gun", "82", 0.82f, 3, false},
    {"lolevont", "125", 1.0f, 0, false},
    {"Spoon bad-dragon.com", "200", 1.0f, 0, false},
    {"Clav bad-dragon.com", "44", 0.25f, 2, false},
    {"walker \xD0\xBF\xD0\xBB\xD0\xBE\xD1\x85\xD0\xBE-\xD0\xB4\xD1\x80\xD0\xB0\xD0\xBA\xD0\xBE\xD0\xBD.", "358", 1.0f, 1, false},
    {"\xE4\xB8\xAD\xE6\x96\x87\xE5\x90\x8D\xE7\x8E\xA9\xE5\xAE\xB6", "115", 0.92f, 0, false},
    {"dkt bad-dragon.com", "150", 1.0f, 0, false},
    {"broken", "7s", 0.0f, 0, true},
    {"yucca", "72", 0.58f, 0, false},
  };
  float y = 148.0f;
  for (const Row& row : rows) {
    const float h = row.tone == 3 ? 27.0f : 25.0f;
    rect(0.0f, y, 24.0f, h, 0.055f, 0.063f, 0.075f, 0.55f);
    rect(24.0f, y, 181.0f, h, 0.055f, 0.063f, 0.075f, row.dead ? 0.45f : 0.62f);
    if (!row.dead && row.fill > 0.0f) rect(24.0f, y, 181.0f * row.fill, h, 0.235f, 0.490f, 0.651f, 1.0f);
    const float tr = row.dead ? 0.70f : 0.95f;
    text(row.name, HudTextFont::UiSemiBold, 16.0f, 30.0f, y + 3.0f, HudTextAlign::Left, 128.0f, tr, tr, tr);
    float hr = 1.0f, hg = 1.0f, hb = 1.0f;
    if (row.tone == 1) { hr = 0.49f; hg = 0.88f; hb = 0.48f; }
    if (row.tone == 2) { hr = 0.95f; hg = 0.66f; hb = 0.23f; }
    text(row.hp, row.dead ? HudTextFont::UiSemiBold : HudTextFont::UiBold, row.dead ? 14.0f : 17.0f,
      199.0f, y + (row.dead ? 5.0f : 3.0f), HudTextAlign::Right, 0.0f, hr, hg, hb);
    y += h + 2.0f + (row.tone == 3 ? 6.0f : 0.0f);
  }
  // Size ladder.
  const float sizes[] = {12.0f, 14.0f, 18.0f, 22.0f, 28.0f, 36.0f};
  float ladderY = 460.0f;
  for (const float px : sizes) {
    char line[64];
    std::snprintf(line, sizeof(line), "%.0f px  Medic 150  UBER 27%%", static_cast<double>(px));
    text(line, HudTextFont::UiSemiBold, px, 320.0f, ladderY, HudTextAlign::Left, 0.0f, 1.0f, 1.0f, 1.0f);
    ladderY += px * 1.35f;
  }
  g_renderer->setHudPrimitives(primitives);
  g_renderer->setHudEnabled(true);
  const tf2::native::HudTextStats stats = tf2::native::hudTextStats();
  traceFmt("hud: font-test available=%d reason=%s semibold=\"%s\" bold=\"%s\" faces=%zu primitives=%zu"
    " lastDraw uiText=%zu uiGlyphs=%zu missing=%zu fromFallback=%zu bitmapFallbacks=%zu relayouts=%zu"
    " atlasRevision=%llu uploaded=%llu glyphs=%zu resets=%zu",
    stats.available ? 1 : 0, stats.reason.c_str(), stats.semiBoldFamily.c_str(), stats.boldFamily.c_str(),
    stats.faces, primitives.size(), g_renderer->hudUiTextQuads(), g_renderer->hudUiGlyphs(),
    g_renderer->hudUiMissing(), g_renderer->hudUiFromFallback(), g_renderer->hudUiBitmapFallbacks(),
    g_renderer->hudUiRelayouts(), static_cast<unsigned long long>(stats.revision),
    static_cast<unsigned long long>(g_renderer->hudUiAtlasRevisionUploaded()), stats.glyphsCached, stats.resets);
}

// ---- panel label text -----------------------------------------------------
//
// WHAT A PANEL'S labelText BECOMES. The document's own string, with the tokens
// this build can answer replaced by the value the frame actually carries. The
// numbers are readPlaybackHud's and the words are the live readout's own tables
// (hudLiveTeamName / hudLiveClassName), so a panel and the readout cannot say two
// different things about the same frame.
//
// A TOKEN WITH NO VALUE IS LEFT EXACTLY AS THE DOCUMENT WROTE IT, and counted.
// "%MaxHealth%" printed on the panel is a visible limitation; a substituted
// number that was never measured would read as data, which is the one failure the
// rest of this file refuses over and over (see the '?' rules in the readout).
struct HudPanelValues {
  std::string health;    // "%Health%"
  std::string clip;      // "%Clip%" and "%Clip1%" (the same reading, two spellings)
  std::string team;      // "%Team%"  (RED / BLU, the readout's own words)
  std::string className; // "%Class%" (the readout's own words)
};
HudPanelValues g_hudPanelValues;

// One panel block plus, when the document gave the panel a label, the text that
// answers it. `drawn` is what hudDrawnRects() counts (Rect primitives only), so
// the gate's "drawn == visible" rule is unchanged by the text.
struct HudPanelDraw {
  std::size_t drawn = 0;
  std::size_t labelled = 0;
  std::size_t glyphs = 0;
  std::size_t truncated = 0;
  std::size_t unresolved = 0;    // %Tokens% this build had no value for
  std::size_t unknownPanels = 0; // rect.panel out of range: never expected, counted
};

std::string hudPanelExpandLabel(const std::string& labelText, std::size_t& unresolved) {
  std::string out;
  out.reserve(labelText.size());
  for (std::size_t index = 0; index < labelText.size();) {
    if (labelText[index] != '%') { out.push_back(labelText[index++]); continue; }
    const std::size_t close = labelText.find('%', index + 1);
    if (close == std::string::npos) { out.append(labelText.substr(index)); break; }
    const std::string token = labelText.substr(index + 1, close - index - 1);
    const std::string* value = nullptr;
    if (token == "Health") value = &g_hudPanelValues.health;
    else if (token == "Clip" || token == "Clip1") value = &g_hudPanelValues.clip;
    else if (token == "Team") value = &g_hudPanelValues.team;
    else if (token == "Class") value = &g_hudPanelValues.className;
    if (value && !value->empty()) out.append(*value);
    else { ++unresolved; out.append(labelText, index, close - index + 1); }
    index = close + 1;
  }
  return out;
}

HudPanelDraw hudPanelPrimitives(std::vector<tf2::native::Renderer::HudPrimitive>& out) {
  // Colours are per-panel and fixed: the colour is what lets a reviewer name a
  // block from a screenshot, and a stable palette makes two runs comparable.
  static const float kPalette[][4] = {
    {0.16f, 0.35f, 0.55f, 0.72f},
    {0.20f, 0.50f, 0.28f, 0.72f},
    {0.55f, 0.35f, 0.14f, 0.72f},
    {0.45f, 0.20f, 0.45f, 0.72f},
  };
  HudPanelDraw draw;
  const auto& panels = g_hud.layout().panels;
  std::size_t index = 0;
  for (const auto& rect : g_hud.rects()) {
    // Hidden panels keep their geometry but are not drawn; the index is advanced
    // anyway so a panel's colour depends on the document, not on which of its
    // siblings happen to be hidden in this frame.
    if (!rect.valid || !rect.visible) { ++index; continue; }
    tf2::native::Renderer::HudPrimitive block;
    block.kind = tf2::native::Renderer::HudPrimitiveKind::Rect;
    block.x = static_cast<float>(rect.x);
    block.y = static_cast<float>(rect.y);
    block.w = static_cast<float>(rect.w);
    block.h = static_cast<float>(rect.h);
    const float* colour = kPalette[index % 4u];
    for (int channel = 0; channel < 4; ++channel) block.colour[channel] = colour[channel];
    // filled=false keeps the bordered-block look every earlier reading was taken
    // against; the text below is what the block was always missing.
    block.filled = false;
    out.push_back(block);
    ++draw.drawn;
    ++index;
    if (rect.panel >= panels.size()) { ++draw.unknownPanels; continue; }
    const tf2::native::HudPanel& panel = panels[rect.panel];
    std::string label = hudPanelExpandLabel(panel.labelText, draw.unresolved);
    if (label.empty()) continue;
    // The renderer clips glyphs to the CLIENT, not to the primitive, so a label
    // wider than its panel would paint outside the rectangle the layout owns --
    // and outside the union rectangle a capture gate compares pixel for pixel.
    // Scale down first, then shorten, so a long value stays inside its own block.
    float scale = rect.h >= 24 ? 2.0f : 1.0f;
    if (scale > 1.0f && static_cast<int>(label.size()) * 16 + 8 > rect.w) scale = 1.0f;
    const std::size_t cell = static_cast<std::size_t>(8.0f * scale);
    const std::size_t room = rect.w > 8 ? (static_cast<std::size_t>(rect.w) - 8) / cell : 0;
    if (label.size() > room) { label.resize(room); ++draw.truncated; }
    if (label.empty()) continue;
    tf2::native::Renderer::HudPrimitive text;
    text.kind = tf2::native::Renderer::HudPrimitiveKind::Text;
    text.w = static_cast<float>(label.size() * cell);
    text.h = static_cast<float>(cell);
    text.scale = scale;
    text.colour[0] = 1.0f;
    text.colour[1] = 1.0f;
    text.colour[2] = 1.0f;
    text.colour[3] = 1.0f;
    // "center" is the one alignment the documents this build has been tested
    // against use (the fixture's HealthValue is a CExLabel with centre text);
    // anything else keeps the left margin rather than guessing.
    int x = rect.x + 4;
    if (panel.textAlignment == "center") {
      x = rect.x + (rect.w - static_cast<int>(text.w)) / 2;
    }
    int y = rect.y + (rect.h - static_cast<int>(cell)) / 2;
    if (x < rect.x) x = rect.x;
    if (y < rect.y) y = rect.y;
    text.x = static_cast<float>(x);
    text.y = static_cast<float>(y);
    text.text = label;
    out.push_back(text);
    ++draw.labelled;
    draw.glyphs += label.size();
  }
  return draw;
}

// Pushes the current layout at the renderer for the window's current client size.
void hudApplyRects(HWND window) {
  if (!g_renderer) return;
  if (g_hudTextTest) { hudApplyTextTest(); return; }
  if (g_hudFontTest) { hudApplyFontTest(); return; }
  RECT client{};
  if (!window || !GetClientRect(window, &client)) { client.right = 1280; client.bottom = 720; }
  const float dpiScale = window ? static_cast<float>(GetDpiForWindow(window)) / 96.0f : 1.0f;
  g_hud.layoutFor(client.right, client.bottom, dpiScale);
  std::vector<tf2::native::Renderer::HudPrimitive> primitives;
  const HudPanelDraw panels = hudPanelPrimitives(primitives);
  // A panel's label is a reading like any other: report how many blocks carry
  // text, how many codepoints that is, and how many tokens this build could not
  // answer -- and print it when the composition CHANGES, not once per frame,
  // because the capture path calls this every frame.
  static std::string lastPanelKey;
  {
    std::string key = std::to_string(panels.drawn) + "|" + std::to_string(panels.labelled)
      + "|" + std::to_string(panels.glyphs) + "|" + std::to_string(panels.truncated)
      + "|" + std::to_string(panels.unresolved) + "|" + std::to_string(panels.unknownPanels);
    if (key != lastPanelKey) {
      lastPanelKey = key;
      traceFmt("hud: panels drawn=%zu labelled=%zu glyphs=%zu truncated=%zu unresolved=%zu unknown=%zu",
        panels.drawn, panels.labelled, panels.glyphs, panels.truncated,
        panels.unresolved, panels.unknownPanels);
    }
  }
  if (g_hudLiveActive) {
    // setHudPrimitives() replaces the whole primitive list, so publishing the
    // layout alone would delete the readout: the panels go underneath, the
    // readout's text on top.
    std::vector<tf2::native::Renderer::HudPrimitive> merged = std::move(primitives);
    // Rebuild the three text lines for the CURRENT client width before appending
    // the panel shapes. setHudPrimitives replaces the list, so without this a
    // resize would leave the centred header at its old x until the next reading.
    const HudLiveLayout layout = hudLiveLayoutFor(window, g_hudLiveLastLines);
    for (int lineIndex = 0; lineIndex < 3; ++lineIndex) {
      if (g_hudLiveLastLines[lineIndex].empty()) continue;
      tf2::native::Renderer::HudPrimitive primitive;
      primitive.kind = tf2::native::Renderer::HudPrimitiveKind::Text;
      primitive.x = static_cast<float>(layout.lineX[lineIndex]);
      primitive.y = 24.0f + 22.0f * static_cast<float>(lineIndex);
      primitive.w = static_cast<float>(layout.lineW[lineIndex]);
      primitive.h = 16.0f;
      primitive.scale = 2.0f;
      primitive.colour[0] = 1.0f;
      primitive.colour[1] = 1.0f;
      primitive.colour[2] = 1.0f;
      primitive.colour[3] = 1.0f;
      primitive.text = g_hudLiveLastLines[lineIndex];
      merged.push_back(primitive);
    }
    merged.insert(merged.end(), g_hudLiveExtraPrimitives.begin(),
      g_hudLiveExtraPrimitives.end());
    g_renderer->setHudPrimitives(merged);
  } else {
    g_renderer->setHudPrimitives(primitives);
  }
  // The pass draws only when there is something to draw: the user enabled the
  // imported layout AND the document produced rectangles, or the live readout has
  // primitives in the renderer. Both terms are readings, not assumptions -- the
  // trace above prints enabled= and the last hud: live line prints the readout, so
  // "the pass was on" can be checked against the numbers that asked for it.
  g_renderer->setHudEnabled((g_hud.settings().enabled && panels.drawn > 0) || g_hudLiveActive);
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
// WHAT IS NOT KNOWN IS SAID OUT LOUD. The player name now comes from the demo's
// own userinfo string table (T100: one verbatim player_info_t per client slot,
// slot -> entity index == slot + 1, gated by userinfo-check.sh), so the third
// line carries the server's name for the entity the camera is watching -- at the
// tick being played, not at the tick the demo was decoded to. When that table has
// no observation for the slot at or before that tick (or the camera is not on a
// player, or no userinfo arrived at all) the line carries "NAME ?" and the trace
// carries name=unavailable with nameReason=no-userinfo-slot | no-userinfo-tick |
// no-userinfo-empty | no-tick, plus a counter. A placeholder that looked like a
// name would be worse than the question mark; so would a name from a later tick.
namespace {

constexpr float kHudLiveX = 24.0f;
constexpr float kHudLiveY = 24.0f;
constexpr float kHudLiveLineHeight = 22.0f;
constexpr float kHudLiveScale = 2.0f;
constexpr float kHudLiveCell = 8.0f * kHudLiveScale;

// Lines 1-2 (time, score) are centred on the client; line 3 (name/HP) stays at
// kHudLiveX. The centring is clamped to the left margin so a window narrower than
// the text keeps the line readable instead of running off the edge. Integer
// arithmetic: the gate checks |x - (cw - chars*16)/2| <= tolerance, so truncating
// a half pixel is inside tolerance and avoids a float-vs-int mismatch.
HudLiveLayout hudLiveLayoutFor(HWND window, const std::string lines[3]) {
  HudLiveLayout layout;
  int clientW = 0;
  if (window) {
    RECT client{};
    if (GetClientRect(window, &client)) {
      clientW = static_cast<int>(client.right - client.left);
    }
  }
  for (int index = 0; index < 3; ++index) {
    layout.lineW[index] = static_cast<int>(lines[index].size()) * static_cast<int>(kHudLiveCell);
    layout.lineX[index] = static_cast<int>(kHudLiveX);
    if (index < 2 && clientW > 0) {
      int x = (clientW - layout.lineW[index]) / 2;
      if (x < static_cast<int>(kHudLiveX)) x = static_cast<int>(kHudLiveX);
      layout.lineX[index] = x;
    }
  }
  layout.headerX = std::min(layout.lineX[0], layout.lineX[1]);
  const int headerRight = std::max(layout.lineX[0] + layout.lineW[0],
    layout.lineX[1] + layout.lineW[1]);
  layout.headerW = headerRight - layout.headerX;
  layout.nameX = layout.lineX[2];
  layout.nameW = layout.lineW[2];
  layout.regionX = std::min(layout.headerX, layout.nameX);
  const int regionRight = std::max(headerRight, layout.nameX + layout.nameW);
  layout.regionW = regionRight - layout.regionX;
  return layout;
}

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
// T126: the name is resolved for the player the RELAY is watching, and the two
// readings below say which way it went. g_hudLiveNameFallbacks counts the frames
// that had to stand the demo's view entity back in (no relay decision yet, or no
// userinfo sample for the relay's player at this tick); the four g_hudName*
// values are the last "hud: name" line's tuple, printed on change because the
// relay holds one target for thousands of ticks.
std::size_t g_hudLiveNameFallbacks = 0;
bool g_hudNameHaveReading = false;
std::int32_t g_hudNameLastEntity = -2;
std::int32_t g_hudNameLastSlot = -2;
int g_hudNameLastKnown = -1;
int g_hudNameLastFallback = -1;
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
    " nameFallbackCalls=%zu"
    " lastSource=%s lastText1=\"%s\" lastText2=\"%s\" lastText3=\"%s\"",
    g_hudLiveCalls, g_hudLiveUpdates, g_hudLiveSkips, g_hudLivePublished,
    g_hudLiveNameMissing, g_hudLiveTargetAbsent, g_hudLiveScoreMissing,
    g_hudLiveTimerFallback, g_hudLiveNameFallbacks, g_hudLiveLastSource.c_str(),
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

// T100 WIRING: the player's name is userinfo -- the demo carries it as the
// 'userinfo' string table, one verbatim player_info_t per client slot -- so the
// readout can say a name instead of a question mark. What it must never do is
// invent one: every way of having no name still ends in '?', and the trace says
// which way it was. The block comment above the readout has the long version.
struct HudLiveName {
  bool known = false;
  const char* reason = "no-userinfo-slot";
  std::int32_t tick = -1;
  std::int32_t entity = -1;
  std::int32_t slot = -1;
  std::string display;
  std::string slug;
  std::string rawHex;
};

// The font is ASCII (hud_font.h covers U+0020..U+007E; anything else takes the
// hollow fallback cell and the gate counts it as an unmapped glyph), and a name
// is whatever the server sent -- UTF-8, control bytes and colour codes included.
// So the display form keeps printable ASCII, maps everything else to '?',
// collapses runs of spaces (two spaces are the field separator on that line) and
// caps the length so one long name cannot push the panel across the frame. The
// exact bytes survive in the trace as hex, so the projection loses nothing.
constexpr std::size_t kHudLiveMaxNameChars = 24;

void hudLiveProjectName(const std::string& raw, HudLiveName& name) {
  for (const char ch : raw) {
    if (name.display.size() >= kHudLiveMaxNameChars) break;
    const unsigned char byte = static_cast<unsigned char>(ch);
    const char out = (byte >= 0x20u && byte <= 0x7eu) ? static_cast<char>(byte) : '?';
    if (out == ' ' && !name.display.empty() && name.display.back() == ' ') continue;
    name.display.push_back(out);
  }
  while (!name.display.empty() && name.display.back() == ' ') name.display.pop_back();
  // The trace field the gate greps is [A-Za-z0-9._-]+, which a name generally is
  // not; the slug is that same name folded into the charset, the display form is
  // what the frame carries.
  for (const char ch : name.display) {
    const bool safe = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')
      || (ch >= '0' && ch <= '9') || ch == '.' || ch == '_' || ch == '-';
    name.slug.push_back(safe ? ch : '_');
  }
  if (name.slug.empty()) name.slug = "unnamed";
  char hex[3];
  for (const char ch : raw) {
    std::snprintf(hex, sizeof(hex), "%02x", static_cast<unsigned>(static_cast<unsigned char>(ch)));
    name.rawHex += hex;
  }
}

// entity -> slot -> (slot, tick) -> name. No userid anywhere: the demo's own
// table is keyed by client slot, DemoUserInfoEntry::entityIndex is the decoder's
// derivation of the entity that slot drives (slot + 1 -- an engine convention the
// demo cannot confirm, since userid is a different namespace), and
// findUserInfoAtTick is a pure function of (summary, tick) so a seek backwards
// returns the name the tick really had. Recomputing the slot as entity - 1 here
// would be a second copy of an assumption; reading the decoder's field is not.
HudLiveName hudLiveResolveName(const tf2::native::DemoNetworkSummary& summary,
    std::int32_t viewEntity, std::int32_t playbackTick) {
  HudLiveName name;
  name.entity = viewEntity;
  if (viewEntity < 1) return name;
  if (summary.firstDataEntryTick < 0) {
    name.reason = "no-tick";
    return name;
  }
  const tf2::native::DemoUserInfoEntry* slot = nullptr;
  for (const auto& entry : summary.userInfos) {
    if (entry.entityIndex == static_cast<std::uint32_t>(viewEntity)) {
      slot = &entry;
      break;
    }
  }
  if (slot == nullptr) return name;
  name.slot = static_cast<std::int32_t>(slot->entryIndex);
  // The samples are stamped with the *server* tick that was in force when their
  // packet was decoded (svc_NetTick), not with the demo's entry tick: on the nine
  // demos T100 measured, entry ticks run 1..N while a slot's first observation
  // lands at server tick 16349/25595/35176, and this gate's own demo puts its
  // first observation at 10319 (D73 window step 2)
  // (evidence/t100-userinfo/MEASURED.md:137-142). Asking in firstDataEntryTick +
  // playback tick answers '?' for the first net_minus_entry ticks of every demo
  // -- about 10318 of snakewater's 64352 -- and then goes stale. packetServerTicks
  // is already indexed by playback tick (demo_header.h:609-621) and main.cpp:
  // 9788-9805 resolves the scene tick through the same table and the same ladder.
  if (playbackTick < 0
      || static_cast<std::size_t>(playbackTick) >= summary.packetServerTicks.size()) {
    name.reason = "no-tick";  // no data packet at this playback tick
    return name;
  }
  if (summary.packetServerTickKnown[playbackTick] == 0) {
    name.reason = "no-tick";  // before the first recorded packet: not inferred
    return name;
  }
  name.tick = summary.packetServerTicks[playbackTick];
  tf2::native::DemoUserInfoEntry resolved;
  if (!tf2::native::findUserInfoAtTick(summary, name.tick, slot->entryIndex, resolved)) {
    name.reason = "no-userinfo-tick";
    return name;
  }
  if (resolved.name.empty()) {
    name.reason = "no-userinfo-empty";
    return name;
  }
  name.known = true;
  name.reason = "ok";
  hudLiveProjectName(resolved.name, name);
  return name;
}

HudLiveText hudLiveCompose(const tf2::native::main_internal::PlaybackState& playback,
    const tf2::native::PlaybackHudState& target, const HudLiveReading& reading,
    const HudLiveName& name) {
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
  // The name is the one field on this line that is text, so it is the one field
  // that can be wrong in a way no counter notices: it is either the demo's name
  // for this slot at this tick or a question mark, never a placeholder.
  text.target = "NAME ";
  text.target += name.known ? name.display.c_str() : "?";
  text.target += "  HP ";
  text.target += target.healthKnown ? std::to_string(target.health) : "?";
  text.target += "  ";
  text.target += target.classKnown ? hudLiveClassName(target.playerClass) : "?";
  text.target += "  ";
  text.target += target.teamKnown ? hudLiveTeamName(target.team) : "?";
  return text;
}

// ---------------------------------------------------------------------------
// E9 (2026-11-13): the minimap panel -- what it draws, and why it draws it there.
//
// WHAT A PANEL IS. One measured world rectangle (the map's own bounds) projected
// equal-aspect into a 160-pixel square, plus the control points measured on that
// map, the viewer's position, and a three-dot arrow along the camera yaw. No bitmap
// is involved: none of the seven supported maps ships a radar overview (T135
// MINIMAP-DATA-REPORT.md section 4 looked in the pakfile, the VPKs and the loose
// files), so a map here is geometry and points, not an image.
//
// WHY THE TABLE IS COMPILED IN. Bounds and control points are the map's design, not
// a row the BSP exposes -- BspMap carries no entity list -- so at load time there is
// nothing to read a control point's origin from. minimapFindMapRow() is the lookup;
// a map that is not in the table draws NOTHING and publishes the reason, because an
// unknown map rendered as an empty panel is a canvas nobody can tell from a map with
// no objectives (the T65 failure).
//
// WHY THESE COLOURS ARE OPAQUE. The gate compares a --hud-minimap frame against a
// frame without the switch, then reads the rings' own colours back out of the
// bitmap; a blend of two primitives would make both readings ambiguous.
// ---------------------------------------------------------------------------
namespace {

constexpr float kMinimapRadius = 4.0f;       // control-point ring, in pixels
constexpr float kMinimapViewerHalf = 1.5f;   // the viewer dot is 3x3 pixels
constexpr float kMinimapArrowStart = 4.0f;   // first arrow dot, from the centre
constexpr float kMinimapArrowStep = 1.0f;
constexpr int kMinimapArrowDots = 3;
constexpr float kMinimapArrowHalf = 1.0f;    // each arrow dot is 2x2 pixels
constexpr float kMinimapBorderWidth = 2.0f;
constexpr float kMinimapBackgroundColour[4] = {0.05f, 0.06f, 0.08f, 1.0f};
constexpr float kMinimapBorderColour[4] = {0.62f, 0.64f, 0.66f, 1.0f};
// The three team colours live in timeline_hud.h as kTeamRedColour / kTeamBluColour /
// kTeamNeutralColour: the minimap rings and the timeline's kill marks are in the same
// frame, and a gate reads both, so there is one definition rather than two that agree
// until someone edits one.
constexpr float kMinimapViewerColour[4] = {0.10f, 1.00f, 0.30f, 1.0f};
// The basemap is a line, not a surface: it sits between the background and the
// border in brightness so the objective rings still win the contrast, and it is
// NOT team-coloured. A filled silhouette was the alternative and it was rejected
// in N14 7.2 -- on a multi-storey map a fill reads as one floor.
constexpr float kMinimapFloorColour[4] = {0.30f, 0.34f, 0.38f, 1.0f};
// A merged rectangle costs one primitive. The list is bounded rather than trusted:
// N14 7.2 measured 235-570 rectangles across the seven tabulated maps, so a list
// that blows past this bound is a build bug and not a map, and it is reported
// instead of uploaded.
constexpr std::size_t kMinimapLineworkMaxRects = 4000;

void minimapPushRect(std::vector<tf2::native::Renderer::HudPrimitive>& into, float x,
    float y, float w, float h, const float colour[4]) {
  tf2::native::Renderer::HudPrimitive primitive;
  primitive.kind = tf2::native::Renderer::HudPrimitiveKind::Rect;
  primitive.x = x;
  primitive.y = y;
  primitive.w = w;
  primitive.h = h;
  primitive.filled = true;
  for (int channel = 0; channel < 4; ++channel) primitive.colour[channel] = colour[channel];
  into.push_back(primitive);
}

// A circle primitive is its BOUNDING BOX: the HUD shader discards outside half the
// box (native_renderer_shaders.cpp, kind 3), so w == h == 2r, the centre is the box
// centre, and the rim is hard -- there is no antialiasing to average the colour the
// gate samples.
void minimapPushCircle(std::vector<tf2::native::Renderer::HudPrimitive>& into, float x,
    float y, float diameter, const float colour[4]) {
  tf2::native::Renderer::HudPrimitive primitive;
  primitive.kind = tf2::native::Renderer::HudPrimitiveKind::Circle;
  primitive.x = x;
  primitive.y = y;
  primitive.w = diameter;
  primitive.h = diameter;
  for (int channel = 0; channel < 4; ++channel) primitive.colour[channel] = colour[channel];
  into.push_back(primitive);
}

const float* minimapTeamColour(int team) {
  if (team == 2) return tf2::native::kTeamRedColour;
  if (team == 3) return tf2::native::kTeamBluColour;
  return tf2::native::kTeamNeutralColour;
}

// Whether a point's ring fits inside the panel with its radius to spare. The HUD
// pass scissors to the union of the primitives it was handed, not to this panel, so a
// shape that hung over the edge would paint on top of the world outside the panel --
// and the gate's "nothing outside the panel changed" half would then be red for a
// reason that had nothing to do with projection.
bool minimapPointFits(const tf2::native::MinimapPoint& pixel,
    const tf2::native::HudPanelRect& panel, float halfExtent) {
  // E10/U3b (2026-11-16): the spare is measured on the panel's own width and its
  // own height. One number for both axes is only right while the panel is square;
  // a narrow client clamps the width and keeps the height, and this cull then
  // dropped markers that were still inside the panel.
  return pixel.inside
    && tf2::native::hudPanelPointInside(panel, pixel.px, pixel.py, halfExtent);
}

struct HudMinimapDraw {
  bool active = false;      // a frame was pushed: the switch is on for this run
  bool ready = false;       // a tabulated map, this frame's pose, and a placed panel
  const char* reason = "no-frame";
  const tf2::native::MinimapMapRow* row = nullptr;
  tf2::native::MinimapBounds bounds;
  tf2::native::MinimapProjection projection;
  tf2::native::HudPanelRect rect;
  float angle = 0.0f;
  float viewerPx = 0.0f;
  float viewerPy = 0.0f;
  bool viewerInside = false;
  int arrowDots = 0;
  std::size_t pointsDrawn = 0;
  // N14/U3a: the basemap. The rectangle list is the built result for this panel
  // size and this geometry revision; the counters beside it describe what it was
  // built from, because "no basemap" and "a basemap with nothing in it" are
  // different readings and the trace has to be able to say which one happened.
  bool lineworkReady = false;
  const char* lineworkReason = "no-geometry";
  std::size_t floorTriangles = 0;   // triangles handed over by main.cpp
  std::size_t floorFaces = 0;       // distinct surfaces among them
  std::size_t floorKept = 0;        // triangles whose face cleared the thresholds
  std::size_t floorFacesKept = 0;   // distinct faces that cleared them
  std::size_t floorBands = 0;
  std::size_t lineworkRuns = 0;
  std::size_t lineworkRects = 0;
  // The number of those rectangles this publish actually PUSHED into primitives.
  // lineworkRects is what the builder built; this is what the panel handed over.
  // They differ when the budget refused the plan (reason=rect-budget), and the trace
  // has to be able to say which of the two happened.
  std::size_t floorRectsPushed = 0;
  std::string signature;
  std::vector<tf2::native::Renderer::HudPrimitive> primitives;
};

// The panel rectangle comes from the same rule the hud_panel self-test pins --
// top-right, 160x160, 4 pixels inside the 8-pixel inset -- and is not arithmetic
// repeated here: the gate recomputes the expected rectangle from the client size on
// its own and compares it with the published one, so a drifted panel is caught by two
// independent computations instead of being agreed with by one.
tf2::native::HudPanelRect hudMinimapPanelRect(HWND window) {
  RECT client{};
  if (!window || !GetClientRect(window, &client)) {
    client.right = 1280;
    client.bottom = 720;
  }
  std::vector<tf2::native::HudPanelSpec> specs;
  specs.push_back(tf2::native::hudMinimapPanelSpec());
  const tf2::native::HudPanelLayout layout = tf2::native::hudLayoutPanels(specs,
    static_cast<int>(client.right), static_cast<int>(client.bottom),
    tf2::native::kHudPanelSafeInset);
  if (layout.panels.empty()) return tf2::native::HudPanelRect{};
  tf2::native::HudPanelRect rect = layout.panels.front();
  // 2026-11-22: --hud-minimap-avoid-status. The native UI's status line is laid
  // out by planUiLayout at y = 12 dip, 28 dip tall, across the window -- the same
  // band the panel starts in (docs/N15-DPI-LAYOUT-2026-11-15.md section 2.1). The
  // status rectangle is taken from that planner rather than restated here, so the
  // two cannot drift. Off by default: this block is skipped and the rectangle is
  // exactly the one the layout rule produced.
  if (rect.placed && tf2::native::minimapHudAvoidStatus()) {
    const float dpiScale = window ? static_cast<float>(GetDpiForWindow(window)) / 96.0f : 1.0f;
    const UiLayoutPlan ui = planUiLayout(static_cast<int>(client.right),
      static_cast<int>(client.bottom), dpiScale > 0.0f ? dpiScale : 1.0f);
    const UiRect& status = ui.rects[static_cast<std::size_t>(UiControlId::Status)];
    const bool overlaps = status.w > 0 && status.h > 0
      && rect.x < status.x + status.w && status.x < rect.x + rect.w
      && rect.y < status.y + status.h && status.y < rect.y + rect.h;
    if (overlaps) {
      const int top = status.y + status.h + static_cast<int>(tf2::native::kHudMinimapPanelMargin);
      const int bottomLimit = static_cast<int>(client.bottom) - tf2::native::kHudPanelSafeInset;
      int height = rect.h;
      if (top + height > bottomLimit) height = bottomLimit - top;
      // A panel squeezed below a usable size keeps its original place: an overlap
      // is a cosmetic defect, a 10-pixel map is not a map.
      if (height >= 48) {
        rect.y = top;
        rect.h = height;
      }
    }
  }
  return rect;
}

// N14/U3a: the basemap, built once per (geometry revision, panel size).
//
// The build is a scanline fill of every floor triangle plus one pass per storey
// band, and none of its inputs change between frames, so paying it per frame would
// be paying it sixty times a second for the same picture. The cache key is the
// geometry REVISION and not a bool: a process that loads a second demo pushes a
// second geometry, and a bool would keep drawing the first map's floors on the
// second map.
namespace {
struct MinimapLineworkCache {
  std::size_t revision = 0;
  int width = 0;
  int height = 0;
  bool built = false;
  tf2::native::MinimapLinework linework;
};
MinimapLineworkCache g_minimapLineworkCache;
}  // namespace

const tf2::native::MinimapLinework& hudMinimapLineworkFor(
    const tf2::native::MinimapProjection& projection, int width, int height) {
  const std::size_t revision = tf2::native::minimapHudFloorGeometryRevision();
  MinimapLineworkCache& cache = g_minimapLineworkCache;
  if (cache.built && cache.revision == revision && cache.width == width
      && cache.height == height) {
    return cache.linework;
  }
  cache.linework = tf2::native::minimapBuildLinework(
    tf2::native::minimapHudFloorGeometryRef(), projection, width, height);
  cache.revision = revision;
  cache.width = width;
  cache.height = height;
  cache.built = true;
  return cache.linework;
}

// 2026-11-22: the stand-in row for an untabulated map under
// --hud-minimap-bounds bsp. Rebuilt only when the geometry revision or the map
// changes; the trace line is printed once per rebuild so a run says where its
// rectangle came from. The row's map pointer refers to the cache's own string.
struct MinimapDerivedRowCache {
  bool built = false;
  bool valid = false;
  std::size_t revision = 0;
  std::string map;
  tf2::native::MinimapMapRow row;
};
MinimapDerivedRowCache g_minimapDerivedRow;

const tf2::native::MinimapMapRow* hudMinimapDerivedRow(const std::string& mapName) {
  const std::size_t revision = tf2::native::minimapHudFloorGeometryRevision();
  if (revision == 0) return nullptr;
  MinimapDerivedRowCache& cache = g_minimapDerivedRow;
  const std::string name = tf2::native::minimapNormalizeMapName(mapName);
  if (!cache.built || cache.revision != revision || cache.map != name) {
    const tf2::native::MinimapDerivedBounds derived =
      tf2::native::minimapBoundsFromFloor(tf2::native::minimapHudFloorGeometryRef());
    cache.built = true;
    cache.revision = revision;
    cache.map = name;
    cache.valid = derived.valid;
    cache.row = tf2::native::MinimapMapRow{};
    cache.row.map = cache.map.c_str();
    cache.row.minX = derived.minX;
    cache.row.minY = derived.minY;
    cache.row.maxX = derived.maxX;
    cache.row.maxY = derived.maxY;
    traceFmt("minimap: bounds source=floor map=%s valid=%d triangles=%zu area=%.0f"
      " min=%.1f,%.1f max=%.1f,%.1f cps=0",
      name.c_str(), derived.valid ? 1 : 0, derived.triangles, derived.area,
      static_cast<double>(derived.minX), static_cast<double>(derived.minY),
      static_cast<double>(derived.maxX), static_cast<double>(derived.maxY));
  }
  return cache.valid ? &cache.row : nullptr;
}

HudMinimapDraw hudMinimapCompose(HWND window) {
  HudMinimapDraw draw;
  if (!tf2::native::minimapHudFramePushed()) return draw;
  draw.active = true;
  const tf2::native::MinimapHudFrame& frame = tf2::native::minimapHudFrameRef();

  // The signature is what the change key publishes on: the map, the panel, and the
  // camera's projection of the viewer. The tick is deliberately NOT in it -- two
  // ticks with the same camera draw the same picture, and republishing them would be
  // an upload nobody can see (the readout it rides on exists to say "the reading
  // changed").
  draw.signature = "minimap|";
  draw.signature += frame.mapName;
  draw.signature += '|';
  draw.signature += frame.valid ? "camera" : "held";

  draw.row = tf2::native::minimapFindMapRow(frame.mapName);
  if (draw.row == nullptr && tf2::native::minimapHudBoundsFromFloor()) {
    // Opt-in only. A table row always wins, so a tabulated map draws exactly as
    // before under either setting.
    draw.row = hudMinimapDerivedRow(frame.mapName);
    if (draw.row == nullptr) {
      draw.reason = "bounds-not-derivable";
      return draw;
    }
    draw.signature += "|floor";
  }
  if (draw.row == nullptr) {
    draw.reason = "bounds-not-tabulated";
    return draw;
  }
  if (!frame.valid) {
    // Without this frame's pose the rings would be a real map drawn at the last
    // camera's place, and the panel would be lying by one frame. Not drawing is the
    // honest answer, and the reading says which case this is.
    draw.reason = "no-camera-pose";
    return draw;
  }
  draw.rect = hudMinimapPanelRect(window);
  if (!draw.rect.placed) {
    draw.reason = "panel-not-placed";
    return draw;
  }
  draw.bounds = tf2::native::minimapBoundsForRow(*draw.row, draw.rect.w, draw.rect.h);
  draw.projection = tf2::native::minimapPanelProjection(draw.bounds);
  if (!draw.projection.valid) {
    draw.reason = "no-projection";
    return draw;
  }
  draw.ready = true;
  draw.reason = "ok";

  // Everything below is panel-LOCAL pixels; the panel's origin is added once, when a
  // primitive is handed to the renderer.
  const tf2::native::MinimapPoint viewer = tf2::native::minimapPanelPoint(draw.bounds,
    draw.projection, frame.eyeX, frame.eyeY);
  draw.viewerPx = viewer.px;
  draw.viewerPy = viewer.py;
  draw.viewerInside = minimapPointFits(viewer, draw.rect, kMinimapViewerHalf);
  draw.angle = tf2::native::minimapScreenAngleDegrees(frame.yawDegrees);

  {
    char numbers[192];
    std::snprintf(numbers, sizeof(numbers), "|%d,%d,%d|%.2f,%.2f|%d|%.2f|%.2f",
      draw.rect.x, draw.rect.y, draw.rect.w, static_cast<double>(draw.viewerPx),
      static_cast<double>(draw.viewerPy), draw.viewerInside ? 1 : 0,
      static_cast<double>(frame.yawDegrees), static_cast<double>(draw.angle));
    draw.signature += numbers;
  }

  // N14/U3a: the basemap. Admission is deliberately NOT "does geometry exist" --
  // it is the same tabulated-map lookup that got us this far, plus a pushed
  // geometry, because an untabulated map must stay byte-identical to a frame drawn
  // without the switch (gate A11) and drawing floors under a panel whose bounds
  // nobody measured would put a real map in a wrong rectangle.
  draw.floorTriangles = tf2::native::minimapHudFloorGeometryRef().size();
  if (tf2::native::minimapHudFloorGeometryRevision() == 0) {
    draw.lineworkReason = "no-geometry";
  } else {
    const tf2::native::MinimapLinework& linework =
      hudMinimapLineworkFor(draw.projection, draw.rect.w, draw.rect.h);
    draw.lineworkReady = linework.valid;
    draw.lineworkReason = linework.valid ? "ok" : "not-built";
    draw.floorKept = linework.kept;
    draw.floorFaces = linework.faces;
    draw.floorFacesKept = linework.facesKept;
    draw.floorBands = linework.bands;
    draw.lineworkRuns = linework.runs;
    draw.lineworkRects = linework.rects.size();
    if (linework.rects.size() > kMinimapLineworkMaxRects) {
      // Reported, not drawn: a rectangle list this long means the build went
      // wrong, and uploading it would cost every later frame for a picture that
      // cannot be read anyway.
      draw.lineworkReady = false;
      draw.lineworkReason = "rect-budget";
    }
  }

  const float originX = static_cast<float>(draw.rect.x);
  const float originY = static_cast<float>(draw.rect.y);
  // E10/U3b (2026-11-16): the frame's four edges come from the panel's own width
  // and height. The product used to take rect.w for both, which is only right
  // while the panel is square: on a narrow client the bottom edge and both side
  // edges sat at the width's distance, with the floor plan below them.
  const tf2::native::HudPanelFrame panelFrame =
    tf2::native::hudPanelFrame(draw.rect, kMinimapBorderWidth);
  minimapPushRect(draw.primitives, panelFrame.background.x, panelFrame.background.y,
    panelFrame.background.w, panelFrame.background.h, kMinimapBackgroundColour);
  minimapPushRect(draw.primitives, panelFrame.top.x, panelFrame.top.y,
    panelFrame.top.w, panelFrame.top.h, kMinimapBorderColour);
  minimapPushRect(draw.primitives, panelFrame.bottom.x, panelFrame.bottom.y,
    panelFrame.bottom.w, panelFrame.bottom.h, kMinimapBorderColour);
  minimapPushRect(draw.primitives, panelFrame.left.x, panelFrame.left.y,
    panelFrame.left.w, panelFrame.left.h, kMinimapBorderColour);
  minimapPushRect(draw.primitives, panelFrame.right.x, panelFrame.right.y,
    panelFrame.right.w, panelFrame.right.h, kMinimapBorderColour);
  // The floor plan sits between the frame and the markers: under the rings, so a
  // ring on a wall is still a ring, and over the background, so the outline is
  // read against the dark panel and not against the world.
  if (draw.lineworkReady) {
    const tf2::native::MinimapLinework& linework = hudMinimapLineworkFor(draw.projection,
      draw.rect.w, draw.rect.h);
    for (const tf2::native::MinimapLineRect& box : linework.rects) {
      minimapPushRect(draw.primitives, originX + static_cast<float>(box.x0),
        originY + static_cast<float>(box.y0),
        static_cast<float>(box.x1 - box.x0), static_cast<float>(box.y1 - box.y0),
        kMinimapFloorColour);
      ++draw.floorRectsPushed;
    }
  }
  for (std::size_t index = 0; index < draw.row->pointCount; ++index) {
    const tf2::native::MinimapControlPointRow& point = draw.row->points[index];
    const tf2::native::MinimapPoint pixel = tf2::native::minimapPanelPoint(draw.bounds,
      draw.projection, point.x, point.y);
    if (!minimapPointFits(pixel, draw.rect, kMinimapRadius)) continue;
    ++draw.pointsDrawn;
    minimapPushCircle(draw.primitives, originX + pixel.px - kMinimapRadius,
      originY + pixel.py - kMinimapRadius, kMinimapRadius * 2.0f,
      minimapTeamColour(point.team));
  }
  // The rings first, the viewer last: the dot a reader looks for must never be buried
  // under an objective it happens to stand on.
  if (draw.viewerInside) {
    minimapPushRect(draw.primitives, originX + draw.viewerPx - kMinimapViewerHalf,
      originY + draw.viewerPy - kMinimapViewerHalf, kMinimapViewerHalf * 2.0f,
      kMinimapViewerHalf * 2.0f, kMinimapViewerColour);
    // World yaw is counter-clockwise from +x with +y north; the panel is +x right and
    // +y DOWN. The heading of a yaw in panel space is therefore (cos, -sin) -- the
    // minus IS the flip, and it is the one sign a wrong implementation gets wrong.
    const double radians =
      static_cast<double>(frame.yawDegrees) * 3.14159265358979323846 / 180.0;
    const float forwardX = static_cast<float>(std::cos(radians));
    const float forwardY = -static_cast<float>(std::sin(radians));
    for (int dot = 0; dot < kMinimapArrowDots; ++dot) {
      const float distance = kMinimapArrowStart + kMinimapArrowStep * static_cast<float>(dot);
      const float px = draw.viewerPx + distance * forwardX;
      const float py = draw.viewerPy + distance * forwardY;
      if (!tf2::native::hudPanelPointInside(draw.rect, px, py, kMinimapArrowHalf)) {
        continue;
      }
      ++draw.arrowDots;
      minimapPushRect(draw.primitives, originX + px - kMinimapArrowHalf,
        originY + py - kMinimapArrowHalf, kMinimapArrowHalf * 2.0f,
        kMinimapArrowHalf * 2.0f, kMinimapViewerColour);
    }
  }
  return draw;
}

// The panel's readings: one line per publish plus one line per control point. A gate
// reads a pixel and this number together, so the ring it samples is the ring the
// product says it drew; a human reads where a ring went without a debugger.
//
// Two primitive counts, not one. `shapes` is every primitive the panel handed over,
// which since N14/U3a includes the basemap's rectangles (1230 of the 1243 on
// pl_upward_f12); `markers` is that same list minus the pushed basemap rectangles,
// i.e. the frame + rings + viewer + heading dots, which is the number a reader means
// by "thirteen shapes". The gate asserts `markers` plus the composition
// shapes == markers + floorRects plus floorRects == rects, so the basemap's size is
// never pinned as a constant and a plan that stops being pushed is still caught.
void hudMinimapTrace(const HudMinimapDraw& draw) {
  if (!draw.active) return;
  const tf2::native::MinimapHudFrame& frame = tf2::native::minimapHudFrameRef();
  traceFmt("minimap: hud state=%s map=%s reason=%s panel=%d,%d,%d,%d"
    " scale=%.6f offsetX=%.6f offsetY=%.6f angle=%.2f yaw=%.2f eye=%.1f,%.1f"
    " viewerPx=%.2f viewerPy=%.2f viewerInside=%d arrowDots=%d cps=%zu drawn=%zu"
    " shapes=%zu markers=%zu floorRects=%zu lw=%s floor=%zu faces=%zu kept=%zu"
    " fkept=%zu bands=%zu runs=%zu rects=%zu tick=%d",
    draw.ready ? "ready" : "unavailable", frame.mapName.c_str(), draw.reason,
    draw.rect.x, draw.rect.y, draw.rect.w, draw.rect.h,
    static_cast<double>(draw.projection.scale),
    static_cast<double>(draw.projection.offsetX),
    static_cast<double>(draw.projection.offsetY),
    static_cast<double>(draw.angle), static_cast<double>(frame.yawDegrees),
    static_cast<double>(frame.eyeX), static_cast<double>(frame.eyeY),
    static_cast<double>(draw.viewerPx), static_cast<double>(draw.viewerPy),
    draw.viewerInside ? 1 : 0, draw.arrowDots,
    draw.row ? draw.row->pointCount : static_cast<std::size_t>(0), draw.pointsDrawn,
    draw.primitives.size(), draw.primitives.size() - draw.floorRectsPushed,
    draw.floorRectsPushed, draw.lineworkReason, draw.floorTriangles, draw.floorFaces,
    draw.floorKept, draw.floorFacesKept, draw.floorBands, draw.lineworkRuns,
    draw.lineworkRects, frame.tick);
  if (!draw.ready) return;
  for (std::size_t index = 0; index < draw.row->pointCount; ++index) {
    const tf2::native::MinimapControlPointRow& point = draw.row->points[index];
    const tf2::native::MinimapPoint pixel = tf2::native::minimapPanelPoint(draw.bounds,
      draw.projection, point.x, point.y);
    traceFmt("minimap: cp name=%s index=%d team=%d world=%.1f,%.1f px=%.2f,py=%.2f"
      " inside=%d drawn=%d",
      point.name, point.index, point.team, static_cast<double>(point.x),
      static_cast<double>(point.y), static_cast<double>(pixel.px),
      static_cast<double>(pixel.py), pixel.inside ? 1 : 0,
      minimapPointFits(pixel, draw.rect, kMinimapRadius) ? 1 : 0);
  }
}

}  // namespace

}  // namespace

// N13 step 1 (2026-11-15): the follow trace line names the player it pinned.
//
// The name is already resolved for the HUD readout by hudLiveResolveName, above,
// and the pinned player is by definition the player the readout is watching once
// the pin agrees with the relay -- so this reuses that resolution instead of
// building a second one against the same user-info table. Nothing here is a new
// lookup and hudLiveResolveName is neither moved nor changed; HudLiveName lives
// in the anonymous namespace closed just above, which is still visible here.
FollowNameInfo hudFollowResolveName(const DemoNetworkSummary& summary,
    std::int32_t entityIndex, std::int32_t playbackTick) {
  FollowNameInfo out;
  const HudLiveName name = hudLiveResolveName(summary, entityIndex, playbackTick);
  out.known = name.known;
  out.display = name.display;
  // The only source this resolution has today is the user-info table, so a
  // resolved name is "userinfo" and anything else says so instead of implying a
  // source that does not exist.
  out.source = name.known ? "userinfo" : "none";
  out.reason = name.reason != nullptr ? name.reason : "no-userinfo-slot";
  return out;
}

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
// I6 step 2 (2026-11-23): the match timeline strip's report.
//
// It arrives as a value from main.cpp's startup analysis, like the minimap frame, so
// this file never reaches into the scan and the two cannot disagree about which demo
// the strip describes. The revision is what makes a second scan republish: the strip's
// key would otherwise be identical and the frame would keep the first match forever.
namespace {
tf2::native::MatchTimelineReport g_hudMatchTimeline;
// A3: the exception track's marks, from the same scan as the report above. Set by the
// same call site, so the two revisions move together and a stale lane is impossible.
std::vector<tf2::native::TimelineHudAnomaly> g_hudTimelineAnomalies;
// A3b: which of those the per-tick row is unfolded around, and the strip the last frame
// composed. The hit test measures THAT strip, not a recomputation: a resize between the
// click and the frame would otherwise move every cell out from under the pointer.
int g_hudTimelineFocusIndex = -1;
tf2::native::TimelineHudStrip g_hudTimelineStrip;
// Section 9 line 238's "jump to three seconds before", in ticks. The recording runs at
// seconds is 200 ticks -- the same number the design's strip gates use for 3 s.
constexpr std::int32_t kTimelineFocusLeadTicks = 200;
std::uint64_t g_hudMatchTimelineRevision = 0;
// Change-driven trace state, the same rule as the minimap's: geometry only changes on
// a resize or a reload, so a line per frame would be noise around 60 identical rows.
struct HudTimelineTraceState {
  std::uint64_t revision = 0;
  int clientW = 0;
  int clientH = 0;
  int visible = -1;
  std::size_t bands = 0;
};
HudTimelineTraceState g_hudTimelineTrace;
}  // namespace

void hudSetMatchTimeline(const tf2::native::MatchTimelineReport& report) {
  g_hudMatchTimeline = report;
  ++g_hudMatchTimelineRevision;
}

void hudSetTimelineAnomalies(const std::vector<tf2::native::TimelineHudAnomaly>& anomalies) {
  g_hudTimelineAnomalies = anomalies;
  // A new list invalidates the selection: the index would otherwise point at a different
  // anomaly than the one the user picked.
  g_hudTimelineFocusIndex = -1;
  ++g_hudMatchTimelineRevision;
}

void hudSetTimelineFocusIndex(int index) {
  const int next = (index >= 0 && index < static_cast<int>(g_hudTimelineAnomalies.size()))
      ? index : -1;
  if (next == g_hudTimelineFocusIndex) return;
  g_hudTimelineFocusIndex = next;
  ++g_hudMatchTimelineRevision;
}

int hudTimelineFocusIndex() {
  return g_hudTimelineFocusIndex;
}

std::size_t hudTimelineAnomalyCount() {
  return g_hudTimelineAnomalies.size();
}

int hudTimelineStepFocus(int direction, std::int32_t playbackTick,
                         std::int32_t& jumpPlaybackTick) {
  jumpPlaybackTick = -1;
  if (direction == 0 || g_hudTimelineAnomalies.empty()) {
    hudSetTimelineFocusIndex(-1);
    return -1;
  }
  // "Next" is next on the AXIS, not next in the vector: blocks and hints arrive as two
  // lists, so the vector is not tick-ordered once a hint precedes a block.
  std::vector<std::size_t> order(g_hudTimelineAnomalies.size());
  for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
  std::stable_sort(order.begin(), order.end(), [](std::size_t a, std::size_t b) {
    return g_hudTimelineAnomalies[a].startTick < g_hudTimelineAnomalies[b].startTick;
  });
  // The anchor is the current selection when there is one, otherwise the playhead: with a
  // row open, PgDn means "the next one after the one I am looking at".
  const std::int32_t offset = g_hudMatchTimeline.valid ? g_hudMatchTimeline.serverToEntryOffset : 0;
  std::int32_t cursor = playbackTick + offset;
  if (g_hudTimelineFocusIndex >= 0
      && g_hudTimelineFocusIndex < static_cast<int>(g_hudTimelineAnomalies.size())) {
    cursor = g_hudTimelineAnomalies[static_cast<std::size_t>(g_hudTimelineFocusIndex)].startTick;
  }
  int chosen = -1;
  if (direction > 0) {
    for (std::size_t i = 0; i < order.size(); ++i) {
      if (g_hudTimelineAnomalies[order[i]].startTick > cursor) {
        chosen = static_cast<int>(order[i]);
        break;
      }
    }
    if (chosen < 0) chosen = static_cast<int>(order.front());  // wrap to the first
  } else {
    for (std::size_t i = order.size(); i > 0; --i) {
      if (g_hudTimelineAnomalies[order[i - 1]].startTick < cursor) {
        chosen = static_cast<int>(order[i - 1]);
        break;
      }
    }
    if (chosen < 0) chosen = static_cast<int>(order.back());  // wrap to the last
  }
  hudSetTimelineFocusIndex(chosen);
  std::int32_t seek = g_hudTimelineAnomalies[static_cast<std::size_t>(chosen)].startTick
      - kTimelineFocusLeadTicks - offset;
  if (seek < 0) seek = 0;
  jumpPlaybackTick = seek;
  return chosen;
}

bool hudTimelineHitTest(int clientX, int clientY, std::int32_t& playbackTick) {
  playbackTick = -1;
  std::int32_t serverTick = 0;
  if (!tf2::native::timelineStripTickAt(g_hudTimelineStrip, static_cast<float>(clientX),
                                        static_cast<float>(clientY), serverTick)) {
    return false;
  }
  const std::int32_t offset = g_hudMatchTimeline.valid ? g_hudMatchTimeline.serverToEntryOffset : 0;
  playbackTick = serverTick - offset;
  if (playbackTick < 0) playbackTick = 0;
  return true;
}

void hudApplyLive(HWND window, bool evidenceRun, bool hudOff, bool hudLive,
    const std::string& mapName,
    const tf2::native::main_internal::PlaybackState& playback,
    const tf2::native::PlaybackHudState& target,
    const std::vector<tf2::native::EntityState>& states,
    const tf2::native::DemoNetworkSummary& summary,
    std::int32_t relayEntity, bool relayValid) {
  // window is used below: the minimap panel asks it for the client rectangle it
  // lays itself out in (E9). It was unused before the panel existed.
  if (!g_renderer) return;
  // While the T67 probe runs it owns the primitive list. Two writers would make
  // the probe's pixel assertions depend on whether this build also has a readout.
  if (g_hudTextTest || g_hudFontTest) return;
  hudLiveRegisterSummary();
  ++g_hudLiveCalls;
  // The imported layout's label tokens are answered from the SAME reader the
  // readout prints, and they are filled BEFORE the suppression branch below: the
  // run that photographs an imported layout is exactly the run whose readout is
  // suppressed, and a panel that fell back to "%Health%" because the readout
  // happened to be off would be a reading that depended on an unrelated switch.
  g_hudPanelValues.health = target.healthKnown ? std::to_string(target.health) : std::string();
  g_hudPanelValues.clip = target.clipKnown ? std::to_string(target.clip) : std::string();
  g_hudPanelValues.team = target.teamKnown ? hudLiveTeamName(target.team) : std::string();
  g_hudPanelValues.className = target.classKnown ? hudLiveClassName(target.playerClass) : std::string();
  // The suppression is decided by the run's mode, never by "the last frame drew
  // something": a capture run has exactly one sampled frame, so a readout that was
  // painted on frame N-1 and cleared on frame N would still leave nothing on the
  // frame under comparison -- but only by accident. Here it is never painted.
  // An explicit --overview-open is a request for pixels on the same footing as
  // --hud-live: the map panel is the thing such a run exists to photograph, so
  // suppressing it would leave an evidence run with a blank screen. Every other
  // evidence run keeps the mode rule above. The readout rides along on these
  // runs, which is why only the overview gate passes the flag.
  // E9, 2026-11-21: --hud-minimap is on that same footing. The minimap panel is
  // the thing a `--hud-minimap` evidence run exists to photograph, and the frame it
  // draws is pushed ONLY when the switch was passed (main.cpp minimapHudSetFrame),
  // so minimapHudFramePushed() answers "this run asked for the panel". Before this
  // term the switch was silently dead in every capture run: the frame was pushed,
  // then the readout's suppression returned before hudMinimapCompose ever ran, and
  // the capture came out byte-identical to a run with no --hud-minimap at all
  // (measured: hd-hudminimap-pg-3000.bmp and hd-pg-3000.bmp, same md5
  // 9a798a63536660c85c664a726dec672d). The readout rides along, exactly as it does
  // for --overview-open.
  const bool suppressed = hudOff || (evidenceRun && !hudLive
    && !tf2::native::mapOverviewOpen() && !tf2::native::minimapHudFramePushed());
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
  // T126: THE NAME BELONGS TO THE PLAYER THE CAMERA IS WATCHING, NOT TO THE
  // DEMO'S VIEW ENTITY. summary.lastViewEntity is written once, by the signon
  // svc_SetView (demo_header_net.cpp), so it names the recording's own view --
  // for a SourceTV demo that is the spectator, not the player on screen. Measured
  // on this gate's demo, the reading said name=MatchaTV at playback ticks 0 and
  // 1043 while the relay decision (native/include/spectate_target.h) had already
  // put the camera behind entity 2 (advance.trace:493 spectate target=2 vs :3033
  // nameEntity=1). The director is the frame's own answer to "who is being
  // watched", so main.cpp hands it in beside the rest of the frame state, and the
  // decoder lookup itself is untouched: entity -> slot -> (slot, tick) -> name.
  //
  // Falling back is a READING, not a silent rescue. When the relay has no target
  // yet, or has one the userinfo table cannot name at this tick, the readout keeps
  // the demo's view entity -- the only target the recording itself names -- and
  // says nameFallback=1. That is what lets a gate tell "the name follows the
  // camera" from "the name is lastViewEntity again" instead of inferring it from
  // the absence of a line.
  const bool relayUsable = relayValid && relayEntity >= 1;
  HudLiveName name = hudLiveResolveName(summary,
    relayUsable ? relayEntity : summary.lastViewEntity, playback.tick);
  bool nameFallback = false;
  if (!relayUsable) {
    // No relay decision on this frame (pre-snapshot): the view entity above is
    // exactly the fallback, and a gate should be able to see that it was taken.
    nameFallback = true;
  } else if (!name.known && static_cast<std::uint32_t>(relayEntity) != summary.lastViewEntity) {
    const HudLiveName viewName = hudLiveResolveName(summary, summary.lastViewEntity, playback.tick);
    if (viewName.known) {
      name = viewName;
      nameFallback = true;
    }
  }
  if (nameFallback) ++g_hudLiveNameFallbacks;
  // Printed on change, not per frame: the relay holds one target for thousands of
  // ticks, and this file's traces are change-driven for exactly that reason. The
  // tuple is what a reader needs -- which entity the name was asked of, which slot
  // the decoder assigned it, whether a name came back, and whether the view entity
  // had to stand in -- so a change in any one of the four is the line.
  const int nameKnownValue = name.known ? 1 : 0;
  const int nameFallbackValue = nameFallback ? 1 : 0;
  if (!g_hudNameHaveReading || name.entity != g_hudNameLastEntity
      || name.slot != g_hudNameLastSlot || nameKnownValue != g_hudNameLastKnown
      || nameFallbackValue != g_hudNameLastFallback) {
    g_hudNameHaveReading = true;
    g_hudNameLastEntity = name.entity;
    g_hudNameLastSlot = name.slot;
    g_hudNameLastKnown = nameKnownValue;
    g_hudNameLastFallback = nameFallbackValue;
    traceFmt("hud: name nameEntity=%d nameSlot=%d nameKnown=%d nameFallback=%d"
      " nameReason=%s nameSlug=%s nameDisplay=\"%s\" relay=%d relayValid=%d",
      static_cast<int>(name.entity), static_cast<int>(name.slot), nameKnownValue,
      nameFallbackValue, name.reason, name.slug.c_str(), name.display.c_str(),
      static_cast<int>(relayUsable ? relayEntity : -1), relayValid ? 1 : 0);
  }
  if (!name.known) ++g_hudLiveNameMissing;  // every frame that asks for a name and cannot have one
  const HudLiveText text = hudLiveCompose(playback, target, reading, name);
  // The three lines and where they go: lines 1-2 centred on the client, line 3 at
  // the left margin. Computed before the panel composes because the panel's
  // overlap rule needs the readout's rectangle as an input.
  const std::string lines[3] = { text.time, text.score, text.target };
  const HudLiveLayout layout = hudLiveLayoutFor(window, lines);
  // E9: the panel is composed from the same frame as the text, after the
  // suppression branch above -- a suppressed run never publishes a minimap
  // reading that a gate could mistake for pixels the frame does not have.
  // E11: the 250px map-overview panel supersedes the 160px panel while it is open,
  // so only one of the two ever contributes shapes. Composing the old panel and
  // then dropping its shapes would still cost the linework build every frame.
  const bool overviewOpen = tf2::native::mapOverviewOpen();
  HudMinimapDraw minimap;
  if (!overviewOpen) minimap = hudMinimapCompose(window);
  tf2::native::OverviewDraw overview;
  if (overviewOpen) {
    tf2::native::OverviewInput overviewInput = tf2::native::mapOverviewBuildInput(
      states, summary, mapName, 0, 0, playback.tick, playback.tickRate);
    // The readout block, so the panel can drop itself rather than overlap it.
    overviewInput.readoutX = layout.regionX;
    overviewInput.readoutY = static_cast<int>(kHudLiveY);
    overviewInput.readoutW = layout.regionW;
    overviewInput.readoutH = static_cast<int>(kHudLiveLineHeight * 2.0f + kHudLiveCell);
    // g_renderer is the device the basemap texture uploads into; the compose
    // call is the only place that knows both the panel and the GPU.
    overview = tf2::native::mapOverviewCompose(window, overviewInput, g_renderer);
  }
  // I6 step 2: the match timeline strip, along the bottom of the client. Composed
  // from THIS frame's playback tick, so the playhead is where the player is, and it
  // gives way to the minimap panel when that panel is on screen.
  int timelineClientW = 1280;
  int timelineClientH = 720;
  {
    RECT client{};
    if (window != nullptr && GetClientRect(window, &client)) {
      timelineClientW = static_cast<int>(client.right);
      timelineClientH = static_cast<int>(client.bottom);
    }
  }
  tf2::native::TimelineHudStrip timelineStrip;
  if (g_hudMatchTimeline.valid) {
    tf2::native::TimelineHudAvoidRect timelineAvoid;
    if (!minimap.primitives.empty()) {
      const tf2::native::HudPanelRect panel = hudMinimapPanelRect(window);
      if (panel.placed) {
        timelineAvoid.x = static_cast<float>(panel.x);
        timelineAvoid.y = static_cast<float>(panel.y);
        timelineAvoid.w = static_cast<float>(panel.w);
        timelineAvoid.h = static_cast<float>(panel.h);
        timelineAvoid.placed = true;
      }
    }
    // A3b: the row's focus, or an unfocused strip (which draws exactly what A3 did).
    tf2::native::TimelineHudFocus timelineFocus;
    if (g_hudTimelineFocusIndex >= 0
        && g_hudTimelineFocusIndex < static_cast<int>(g_hudTimelineAnomalies.size())) {
      const tf2::native::TimelineHudAnomaly& focused =
          g_hudTimelineAnomalies[static_cast<std::size_t>(g_hudTimelineFocusIndex)];
      timelineFocus.valid = true;
      timelineFocus.kind = focused.kind;
      timelineFocus.startTick = focused.startTick;
      timelineFocus.endTick = focused.endTick;
    }
    timelineStrip = tf2::native::composeTimelineStrip(g_hudMatchTimeline,
        timelineClientW, timelineClientH, playback.tick, timelineAvoid, 0.0f,
        g_hudTimelineAnomalies, timelineFocus);
    g_hudTimelineStrip = timelineStrip;
    if (timelineStrip.visible) {
      const HudTimelineTraceState& last = g_hudTimelineTrace;
      if (last.revision != g_hudMatchTimelineRevision || last.clientW != timelineClientW
          || last.clientH != timelineClientH || last.visible != 1
          || last.bands != timelineStrip.bands.size()) {
        traceFmt("timeline: hud visible=1 revision=%llu client=%dx%d rounds=%zu"
          " shapes=%zu red=%zu blu=%zu unknown=%zu anomalies=%zu stalls=%zu"
          " teleports=%zu hints=%zu x=%d y=%d w=%d h=%d"
          " focus=%d cells=%zu anomalyCells=%zu hintCells=%zu cellW=%d cellsY=%d",
          static_cast<unsigned long long>(g_hudMatchTimelineRevision),
          timelineClientW, timelineClientH, timelineStrip.bands.size(),
          timelineStrip.shapes, timelineStrip.ticksRed, timelineStrip.ticksBlu,
          timelineStrip.ticksUnknown, timelineStrip.anomalyBlocks, timelineStrip.stalls,
          timelineStrip.teleports, timelineStrip.anomalyHints,
          static_cast<int>(timelineStrip.x),
          static_cast<int>(timelineStrip.y), static_cast<int>(timelineStrip.w),
          static_cast<int>(timelineStrip.h),
          timelineStrip.focusValid ? 1 : 0, timelineStrip.cells,
          timelineStrip.anomalyCells, timelineStrip.hintCells,
          static_cast<int>(std::lround(timelineStrip.cellW)),
          static_cast<int>(std::lround(timelineStrip.cellsY)));
        g_hudTimelineTrace.revision = g_hudMatchTimelineRevision;
        g_hudTimelineTrace.clientW = timelineClientW;
        g_hudTimelineTrace.clientH = timelineClientH;
        g_hudTimelineTrace.visible = 1;
        g_hudTimelineTrace.bands = timelineStrip.bands.size();
      }
    }
  }
  std::string key = text.time;
  key += '\n';
  key += text.score;
  key += '\n';
  key += text.target;
  key += '\n';
  key += text.source;
  // E9: the panel joins the key only when the switch asked for it. With the switch
  // off the signature is empty and the key is byte-for-byte the one this readout
  // has always built; with it on, a camera move that changes no text still
  // republishes -- which is the whole point of a map beside a readout.
  if (!minimap.signature.empty()) {
    key += '\n';
    key += minimap.signature;
  }
  if (!overview.signature.empty()) {
    key += '\n';
    key += overview.signature;
  }
  // I6: a visible strip makes the playhead part of the frame, so the readout must
  // republish as it moves. Only a VISIBLE strip joins the key: with no analysis the
  // key is byte-for-byte the one this file built before the strip existed.
  if (timelineStrip.visible) {
    key += '\n';
    key += "timeline:";
    key += std::to_string(g_hudMatchTimelineRevision);
    key += ':';
    key += std::to_string(timelineStrip.playheadServerTick);
    key += ':';
    key += std::to_string(timelineClientW);
  }
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

  std::vector<tf2::native::Renderer::HudPrimitive> primitives;
  std::size_t glyphs = 0;
  float widest = 0.0f;
  for (std::size_t index = 0; index < 3; ++index) {
    tf2::native::Renderer::HudPrimitive primitive;
    primitive.kind = tf2::native::Renderer::HudPrimitiveKind::Text;
    primitive.x = static_cast<float>(layout.lineX[index]);
    primitive.y = kHudLiveY + kHudLiveLineHeight * static_cast<float>(index);
    // Only printable ASCII is composed, so bytes == codepoints here. The renderer
    // still walks the UTF-8 properly; this arithmetic is for the region and the
    // expected glyph count the trace publishes.
    primitive.w = static_cast<float>(layout.lineW[index]);
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
  // E9: the panel's shapes ride in the same list, after the text. hudApplyRects
  // rebuilds this list on every resize from g_hudLivePrimitives, so a window
  // resize keeps the panel without either module knowing about the other.
  // Panels are stored apart from the text: a resize rebuilds the text primitives
  // (their x depends on the client width) and appends the panel shapes unchanged.
  g_hudLiveExtraPrimitives.clear();
  g_hudLiveExtraPrimitives.insert(g_hudLiveExtraPrimitives.end(),
    minimap.primitives.begin(), minimap.primitives.end());
  g_hudLiveExtraPrimitives.insert(g_hudLiveExtraPrimitives.end(),
    overview.primitives.begin(), overview.primitives.end());
  // I6: the strip rides with the panels, after them, because it is chrome rather than
  // part of the readout's three lines. Shapes are converted here, at the one place
  // that knows both the pure composer and the renderer's primitive.
  for (const tf2::native::TimelineHudShape& shape : timelineStrip.primitives) {
    tf2::native::Renderer::HudPrimitive primitive;
    const bool isText = shape.kind == tf2::native::TimelineHudShape::Kind::Text;
    primitive.kind = isText ? tf2::native::Renderer::HudPrimitiveKind::Text
                            : tf2::native::Renderer::HudPrimitiveKind::Rect;
    primitive.x = shape.x;
    primitive.y = shape.y;
    primitive.w = shape.w;
    primitive.h = shape.h;
    for (int channel = 0; channel < 4; ++channel) primitive.colour[channel] = shape.colour[channel];
    primitive.filled = !isText;
    primitive.text = shape.text;
    if (isText) {
      primitive.font = tf2::native::HudTextFont::UiSemiBold;
      primitive.pixelSize = shape.pixelSize;
      primitive.maxWidth = shape.w;
    }
    g_hudLiveExtraPrimitives.push_back(std::move(primitive));
  }
  primitives.insert(primitives.end(), g_hudLiveExtraPrimitives.begin(),
    g_hudLiveExtraPrimitives.end());
  // g_hudLivePrimitives stays the READOUT only, so hudApplyRects can prepend the
  // panels without duplicating them when a resize rebuilds this list.
  g_hudLivePrimitives = primitives;
  {
    // The imported layout rides UNDER the readout. Until this, a republish replaced
    // the whole list and the panels vanished until the next resize rebuilt them --
    // an imported HUD that disappears whenever the clock ticks is not a HUD. The
    // panels are rebuilt here rather than cached because their labels answer from
    // this frame's values.
    std::vector<tf2::native::Renderer::HudPrimitive> withPanels;
    withPanels.reserve(primitives.size() + 8);
    hudPanelPrimitives(withPanels);
    withPanels.insert(withPanels.end(), primitives.begin(), primitives.end());
    g_renderer->setHudPrimitives(withPanels);
  }
  g_hudLiveActive = true;
  // The pass is enabled by the readout itself: a plain launch has no imported
  // layout, so hudApplyRects' "panels AND enabled" rule would otherwise leave the
  // readout invisible until somebody imported a .res file. hudApplyRects was
  // taught the same fact (it ORs g_hudLiveActive in), so a resize mid-run cannot
  // switch the readout off.
  g_renderer->setHudEnabled(true);
  // Region: the union of the two published blocks (centred header + left-hand
  // name line), still 60px tall from y=24. It grows and shifts with the window
  // now, which is the honest reading of a centred heading.
  const int regionX = layout.regionX;
  const int regionY = static_cast<int>(kHudLiveY);
  const int regionW = layout.regionW;
  const int regionH = static_cast<int>(kHudLiveLineHeight * 2.0f + kHudLiveCell);
  // reason= says why this line exists (the reading changed); nameReason= says why
  // the name field is a question mark. Two different questions, so two fields --
  // folding them into one "reason" would have made either one unreadable.
  // name= is the gate-facing slug (a name is not in [A-Za-z0-9._-], the field is),
  // nameRawHex is the exact bytes and nameReason is why it is '?' when it is.
  // The nameTick/nameBaseTick/nameEntity/nameSlot fields are what make the reading
  // checkable from outside: the gate compares the sample clock (nameTick) with the
  // entry clock (nameBaseTick + tick) and looks the same slot up in
  // demo_open_probe.
  // E9: the panel's own readings go out before the readout summary, so the summary
  // stays the last line of a publish and a reader who greps for it is not skipped
  // over the panel.
  hudMinimapTrace(minimap);
  if (overview.open) tf2::native::mapOverviewTrace(overview);
  traceFmt("hud: live tick=%d enabled=1 mode=%s source=%s"
    " text1=\"%s\" text2=\"%s\" text3=\"%s\""
    " glyphs=%zu region=%d,%d,%d,%d updates=%zu skips=%zu name=%s"
    " reason=text-changed nameReason=%s nameMissing=%zu"
    " nameTick=%d nameBaseTick=%d nameEntity=%d nameSlot=%d nameRawHex=%s"
    " targetAbsent=%zu scoreMissing=%zu timerFallback=%zu red=%d blu=%d paused=%d"
    " lineX=%d,%d,%d lineW=%d,%d,%d header=%d,%d,%d,%d nameBlock=%d,%d,%d,%d",
    playback.tick, hudLive ? "forced" : "interactive", text.source.c_str(),
    text.time.c_str(), text.score.c_str(),
    text.target.c_str(), glyphs, regionX, regionY, regionW, regionH,
    g_hudLiveUpdates, g_hudLiveSkips,
    name.known ? name.slug.c_str() : "unavailable", name.reason,
    g_hudLiveNameMissing, name.tick, summary.firstDataEntryTick, name.entity, name.slot,
    name.rawHex.c_str(),
    g_hudLiveTargetAbsent, g_hudLiveScoreMissing, g_hudLiveTimerFallback,
    reading.redKnown ? reading.red : -1, reading.bluKnown ? reading.blu : -1,
    playback.paused ? 1 : 0,
    layout.lineX[0], layout.lineX[1], layout.lineX[2],
    layout.lineW[0], layout.lineW[1], layout.lineW[2],
    // header= is the two centred score lines only: 22px line pitch + one 16px
    // cell = 38px tall, so it stops at y=62 and can never touch the name line at
    // y=68. region= stays 60 tall because it is the union of header AND name.
    layout.headerX, static_cast<int>(kHudLiveY), layout.headerW,
    static_cast<int>(kHudLiveLineHeight + kHudLiveCell),
    layout.nameX, static_cast<int>(kHudLiveY + kHudLiveLineHeight * 2.0f), layout.nameW,
    static_cast<int>(kHudLiveCell));
}

}  // namespace tf2::native::main_internal
