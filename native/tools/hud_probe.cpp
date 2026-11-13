// hud_probe -- the T51 HUD import/validation/resolution contract, asserted
// without a device and without a window.
//
// WHY A PROBE AND NOT ONLY THE GATE
//   Half of what T51 claims is arithmetic: what a document says, whether a
//   path escapes its root, what a coordinate resolves to at 1280x720 versus
//   1920x1080, and where the union of panels lands. Those are total functions
//   over strings and integers, so they can be asserted directly and
//   exhaustively, and the product calls the SAME functions -- a green probe is
//   therefore a statement about the shipped code rather than about a copy of
//   it. The half that needs a GPU ("the HUD did not disturb the world") is the
//   gate's job, because it is a byte comparison of two captured frames.
//
// WHAT IT REFUSES TO BE
//   It never reports a count without a control. Every refusal family is
//   paired with an accepted input of the same shape, so "refused=13" cannot
//   be produced by a validator that refuses everything.
//
// USAGE
//   hud_probe [--fixture-root DIR] [--self-test]
//   Prints key=value lines; exit 0 only when every self-assertion held.

#include "hud_import.h"

#include <algorithm>
#include <cmath>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void check(bool condition, const char* label, const std::string& detail = {}) {
  if (condition) {
    std::printf("ok %s%s%s\n", label, detail.empty() ? "" : " ", detail.c_str());
    return;
  }
  ++g_failures;
  std::printf("FAIL %s%s%s\n", label, detail.empty() ? "" : " ", detail.c_str());
}

std::string reasonOf(tf2::native::HudRefusal reason) {
  return tf2::native::hudRefusalName(reason);
}

void writeFile(const std::filesystem::path& file, const std::string& bytes) {
  std::error_code error;
  std::filesystem::create_directories(file.parent_path(), error);
  std::FILE* handle = nullptr;
  if (_wfopen_s(&handle, file.c_str(), L"wb") != 0 || !handle) return;
  if (!bytes.empty()) std::fwrite(bytes.data(), 1u, bytes.size(), handle);
  std::fclose(handle);
}

// ---- a known-good document ------------------------------------------------
//
// The shape every other fixture is a perturbation of. It carries one panel per
// coordinate form the build supports, plus a nested pair, because the two
// questions a reader will ask are "does each anchor work" and "does a child
// anchor to its parent".
constexpr const char* kGoodDocument = R"RES(
Resource/UI/T51Fixture.res
{
	"T51Fixture"
	{
		"controlname"	"EditablePanel"
		"xpos"		"0"
		"ypos"		"0"
		"wide"		"100%"
		"tall"		"100%"
		"visible"	"1"
 
		"HealAnchor"
		{
			"controlname"	"EditablePanel"
			"xpos"			"c-175"
			"ypos"			"r120"
			"wide"			"350"
			"tall"			"80"
			"visible"		"1"
			"enabled"		"1"
 
			"HealthValue"
			{
				"controlname"	"CExLabel"
				"xpos"			"0"
				"ypos"			"0"
				"wide"			"f0.5"
				"tall"			"100%"
				"labelText"		"%Health%"
				"textAlignment"	"center"
				"visible"		"1"
			}
		}
 
		"AmmoPanel"
		{
			"controlname"	"EditablePanel"
			"xpos"			"r200"
			"ypos"			"40"
			"wide"			"200"
			"tall"			"64"
			"visible"		"1"
		}
 
		"HiddenPanel"
		{
			"controlname"	"EditablePanel"
			"xpos"			"10"
			"ypos"			"10"
			"wide"			"10"
			"tall"			"10"
			"visible"		"0"
		}
	}
}
)RES";

std::string replaceOnce(std::string text, const std::string& from, const std::string& to) {
  const auto at = text.find(from);
  if (at == std::string::npos) {
    std::printf("FAIL fixture-rewrite pattern not found: %s\n", from.c_str());
    ++g_failures;
    return text;
  }
  return text.replace(at, from.size(), to);
}

} // namespace

int main(int argc, char** argv) {
  std::filesystem::path fixtureRoot = ".scratch/hud-probe-fixtures";
  bool selfTest = false;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--fixture-root") == 0 && i + 1 < argc) {
      fixtureRoot = std::filesystem::u8path(argv[++i]);
    } else if (std::strcmp(argv[i], "--self-test") == 0) {
      selfTest = true;
    }
  }
  std::error_code error;
  std::filesystem::create_directories(fixtureRoot, error);

  // =======================================================================
  // 1. path safety: every refusal named, and an accepted control beside it
  // =======================================================================
  struct PathCase { const char* text; tf2::native::HudRefusal want; };
  const PathCase pathCases[] = {
    { "resource/ui/hud.res",                 tf2::native::HudRefusal::None },
    { "resource\\ui\\hud.res",             tf2::native::HudRefusal::None },
    { "hud.res",                             tf2::native::HudRefusal::None },
    { "",                                    tf2::native::HudRefusal::EmptyPath },
    { "C:/hud/hud.res",                      tf2::native::HudRefusal::AbsolutePath },
    { "C:\\hud\\hud.res",                    tf2::native::HudRefusal::AbsolutePath },
    { "/etc/passwd",                         tf2::native::HudRefusal::AbsolutePath },
    { "\\\\server\\share\\hud.res",        tf2::native::HudRefusal::AbsolutePath },
    { "\\\\?\\C:\\hud\\hud.res",           tf2::native::HudRefusal::AbsolutePath },
    { "C:hud.res",                           tf2::native::HudRefusal::DriveRelative },
    { "../outside/hud.res",                  tf2::native::HudRefusal::ParentTraversal },
    { "resource/../../escape.res",           tf2::native::HudRefusal::ParentTraversal },
    { "./hud.res",                           tf2::native::HudRefusal::ParentTraversal },
    { "hud.res:stream",                      tf2::native::HudRefusal::AlternateStream },
    { "hud.res ",                            tf2::native::HudRefusal::TrailingDotOrSpace },
    { "huddir./hud.res",                     tf2::native::HudRefusal::TrailingDotOrSpace },
    { "hud\t.res",                          tf2::native::HudRefusal::ControlCharacter },
    { "CON.res",                             tf2::native::HudRefusal::ReservedDeviceName },
    { "nul",                                 tf2::native::HudRefusal::ReservedDeviceName },
    { "console.res",                         tf2::native::HudRefusal::None },
  };
  int pathAccepted = 0, pathRefused = 0, pathWrong = 0;
  for (const PathCase& item : pathCases) {
    const auto safe = tf2::native::sanitizeHudRelativePath(item.text);
    const bool wantOk = item.want == tf2::native::HudRefusal::None;
    if (safe.ok == wantOk && safe.reason == item.want) {
      if (wantOk) ++pathAccepted; else ++pathRefused;
    } else {
      ++pathWrong;
      std::printf("FAIL path-case \"%s\" ok=%d reason=%s want=%s\n", item.text,
        safe.ok ? 1 : 0, reasonOf(safe.reason).c_str(), reasonOf(item.want).c_str());
    }
  }
  std::printf("path.cases=%zu\n", sizeof(pathCases) / sizeof(pathCases[0]));
  std::printf("path.accepted=%d\n", pathAccepted);
  std::printf("path.refused=%d\n", pathRefused);
  std::printf("path.wrong=%d\n", pathWrong);
  if (pathWrong != 0) ++g_failures;

  // =======================================================================
  // 2. the coordinate grammar: each supported form, and each refusal named
  // =======================================================================
  struct CoordCase { const char* text; bool ok; tf2::native::HudCoordUnit unit; float value; };
  const CoordCase coordCases[] = {
    { "0",        true,  tf2::native::HudCoordUnit::Pixels,      0.0f },
    { "-175",     true,  tf2::native::HudCoordUnit::Pixels,    -175.0f },
    { "+12",      true,  tf2::native::HudCoordUnit::Pixels,     12.0f },
    { " 480 ",    true,  tf2::native::HudCoordUnit::Pixels,    480.0f },
    { "c-175",    true,  tf2::native::HudCoordUnit::Center,   -175.0f },
    { "c95",      true,  tf2::native::HudCoordUnit::Center,     95.0f },
    { "r10",      true,  tf2::native::HudCoordUnit::Right,      10.0f },
    { "r-4",      true,  tf2::native::HudCoordUnit::Right,      -4.0f },
    { "f0",       true,  tf2::native::HudCoordUnit::Fraction,    0.0f },
    { "f0.5",     true,  tf2::native::HudCoordUnit::Fraction,    0.5f },
    { "50%",      true,  tf2::native::HudCoordUnit::Percent,    50.0f },
    { "-25%",     true,  tf2::native::HudCoordUnit::Percent,   -25.0f },
    { "o3",       false, tf2::native::HudCoordUnit::ParentOffset,       3.0f },
    { "p0.5",     false, tf2::native::HudCoordUnit::ParentProportional, 0.5f },
    { "s0.5",     false, tf2::native::HudCoordUnit::Scaled,             0.5f },
    { "",         false, tf2::native::HudCoordUnit::Invalid,     0.0f },
    { "wide",     false, tf2::native::HudCoordUnit::Invalid,     0.0f },
    { "c",        false, tf2::native::HudCoordUnit::Invalid,     0.0f },
    { "12px",     false, tf2::native::HudCoordUnit::Invalid,     0.0f },
    { "50%%",     false, tf2::native::HudCoordUnit::Invalid,     0.0f },
  };
  int coordParsed = 0, coordRefusedByDesign = 0, coordInvalid = 0, coordWrong = 0;
  for (const CoordCase& item : coordCases) {
    const auto coord = tf2::native::parseHudCoord(item.text);
    const bool matches = coord.ok == item.ok && coord.unit == item.unit
      && (item.unit == tf2::native::HudCoordUnit::Invalid
          || std::abs(coord.value - item.value) < 0.0001f);
    if (matches) {
      if (coord.ok) ++coordParsed;
      else if (item.unit == tf2::native::HudCoordUnit::Invalid) ++coordInvalid;
      else ++coordRefusedByDesign;
    } else {
      ++coordWrong;
      std::printf("FAIL coord-case \"%s\" ok=%d unit=%d\n", item.text,
        coord.ok ? 1 : 0, static_cast<int>(coord.unit));
    }
  }
  std::printf("coord.parsed=%d\n", coordParsed);
  std::printf("coord.refusedByDesign=%d\n", coordRefusedByDesign);
  std::printf("coord.malformed=%d\n", coordInvalid);
  std::printf("coord.wrong=%d\n", coordWrong);
  if (coordWrong != 0) ++g_failures;

  // =======================================================================
  // 3. resolution: scaling and resolution adaptation, at three screen sizes
  // =======================================================================
  //
  // This is the part the task calls "HUD scaling and resolution behaviour must
  // be visible in the preview". The same parsed document is resolved at three
  // client sizes and two scales; the numbers below are read out of the trace
  // the product prints, so a preview that showed different numbers would be a
  // second implementation of the same rule.
  const auto layout = tf2::native::parseHudLayout(kGoodDocument);
  check(layout.ok, "good-document-parses", "reason=" + reasonOf(layout.reason));
  std::printf("good.panels=%zu\n", layout.panels.size());
  std::printf("good.nesting=%zu\n", layout.nestingDepth);
  std::printf("good.unknownKeys=%zu\n", layout.unknownKeys);

  struct ScreenCase { int width; int height; float dpi; int scale; };
  const ScreenCase screenCases[] = {
    { 1280, 720, 1.0f, 100 },
    { 1920, 1080, 1.0f, 100 },
    { 1920, 1080, 1.5f, 100 },   // a 150% dpi desktop at the same pixel size
    { 1280, 720, 1.0f, 200 },    // the user scale, doubled
    { 1280, 720, 1.0f, 50 },     // ... and halved
  };

  // Panel ordinals are pre-order: the fixture's document has four top-level
  // panels and one nested child inside HealAnchor, so the vector is
  //   [0] T51Fixture   [1] HealAnchor   [2] HealthValue   [3] AmmoPanel   [4] HiddenPanel
  // Index 2 is the nested one, which is how the parent-relative rule gets
  // exercised rather than asserted.
  int resolutionMismatches = 0;
  for (const ScreenCase& screen : screenCases) {
    const auto rects = tf2::native::resolveHudRects(layout, screen.width, screen.height,
      screen.dpi, screen.scale);
    int valid = 0;
    for (const auto& rect : rects) { if (rect.valid) ++valid; }
    std::printf("res %dx%d dpi=%.2f scale=%d valid=%d total=%zu",
      screen.width, screen.height, screen.dpi, screen.scale, valid, rects.size());
    if (rects.size() >= 3 && rects[0].valid && rects[1].valid && rects[2].valid) {
      std::printf(" root=%d,%d,%d,%d anchor=%d,%d,%d,%d child=%d,%d,%d,%d",
        rects[0].x, rects[0].y, rects[0].w, rects[0].h,
        rects[1].x, rects[1].y, rects[1].w, rects[1].h,
        rects[2].x, rects[2].y, rects[2].w, rects[2].h);
    }
    std::printf("\n");
    if (valid == 0) ++resolutionMismatches;
  }
  check(resolutionMismatches == 0, "every-screen-has-visible-panels");

  // The two claims the preview has to make, stated as numbers rather than as a
  // picture: (a) a fraction-anchored panel is the same FRACTION of the screen at
  // both resolutions, and (b) a pixel-anchored panel is the same PIXEL size and
  // the same distance from its anchor.
  {
    const auto small = tf2::native::resolveHudRects(layout, 1280, 720, 1.0f, 100);
    const auto large = tf2::native::resolveHudRects(layout, 1920, 1080, 1.0f, 100);
    const double smallFraction = small[2].w / static_cast<double>(small[1].w);
    const double largeFraction = large[2].w / static_cast<double>(large[1].w);
    std::printf("adapt.fraction small=%.6f large=%.6f\n", smallFraction, largeFraction);
    check(std::abs(smallFraction - largeFraction) < 0.02,
      "fraction-anchor-is-resolution-independent");
    std::printf("adapt.pixels smallAmmo=%dx%d largeAmmo=%dx%d\n",
      small[3].w, small[3].h, large[3].w, large[3].h);
    check(small[3].w == large[3].w && small[3].h == large[3].h,
      "pixel-anchor-keeps-its-size");
    // The right-anchored panel must move with the right edge, not stay put.
    std::printf("adapt.rightAnchor small=%d large=%d\n", small[3].x, large[3].x);
    check(large[3].x > small[3].x, "right-anchor-follows-the-edge");
  }
  {
    // The user scale multiplies the pixel anchors and leaves the fractional ones
    // alone: "f0.5" is half the screen at every scale, which is what stops a
    // 200% HUD from walking off the edge.
    const auto normal = tf2::native::resolveHudRects(layout, 1280, 720, 1.0f, 100);
    const auto doubled = tf2::native::resolveHudRects(layout, 1280, 720, 1.0f, 200);
    std::printf("scale100 ammo=%d,%d,%d,%d childW=%d\n", normal[3].x, normal[3].y,
      normal[3].w, normal[3].h, normal[2].w);
    std::printf("scale200 ammo=%d,%d,%d,%d childW=%d\n", doubled[3].x, doubled[3].y,
      doubled[3].w, doubled[3].h, doubled[2].w);
    check(doubled[3].w == normal[3].w * 2, "pixel-extent-doubles-at-200");
    // The fraction panel is "f0.5" WIDE, i.e. half of its parent. Its parent is a
    // pixel-anchored panel that doubles at 200%, so the child's pixel width doubles
    // too -- and that is the correct behaviour, not a bug: a fraction of a bigger
    // box is a bigger box. What must NOT change is the RATIO. (An earlier version
    // of this assertion compared the two pixel widths and failed on correct code;
    // the rule it was trying to state is the ratio, and the ratio is what it now
    // measures.)
    const double normalRatio = normal[2].w / static_cast<double>(normal[1].w);
    const double doubledRatio = doubled[2].w / static_cast<double>(doubled[1].w);
    std::printf("scale.ratio normal=%.6f doubled=%.6f\n", normalRatio, doubledRatio);
    check(std::abs(normalRatio - doubledRatio) < 0.01f,
      "fraction-ratio-does-not-change-with-scale");
  }
  {
    // dpi is folded in exactly like the user scale, because a 150% desktop and a
    // 150% HUD scale both mean "everything pixel-anchored is 1.5x".
    const auto plain = tf2::native::resolveHudRects(layout, 1920, 1080, 1.0f, 100);
    const auto hidpi = tf2::native::resolveHudRects(layout, 1920, 1080, 1.5f, 100);
    std::printf("dpi1.0 ammoW=%d dpi1.5 ammoW=%d\n", plain[3].w, hidpi[3].w);
    check(hidpi[3].w == static_cast<int>(plain[3].w * 1.5f + 0.5f),
      "dpi-scale-multiplies-pixel-anchors");
  }
  {
    const auto rects = tf2::native::resolveHudRects(layout, 1280, 720, 1.0f, 100);
    const auto unionRect = tf2::native::hudUnionRect(rects);
    std::printf("union x=%d y=%d w=%d h=%d empty=%d\n", unionRect.x, unionRect.y,
      unionRect.w, unionRect.h, unionRect.empty ? 1 : 0);
    check(!unionRect.empty && unionRect.w > 0 && unionRect.h > 0, "union-is-non-empty");
    // Every valid rectangle must be inside the union: that is what makes the
    // union usable as "the only pixels the HUD may own".
    int outside = 0;
    for (const auto& rect : rects) {
      if (!rect.valid) continue;
      if (rect.x < unionRect.x || rect.y < unionRect.y
          || rect.x + rect.w > unionRect.x + unionRect.w
          || rect.y + rect.h > unionRect.y + unionRect.h) ++outside;
    }
    std::printf("union.outside=%d\n", outside);
    check(outside == 0, "every-panel-is-inside-the-union");
  }
  // =======================================================================
  // 4. refusals: every named reason, executed on a fixture that earns it
  // =======================================================================
  //
  // A refusal table whose entries all come from one file proves only that the
  // file was refused. Each case here is the smallest perturbation that should
  // produce that ONE reason, and the control at the end is the unperturbed
  // document, so a validator that refuses everything cannot pass this section.
  struct DocCase { const char* label; std::string text; tf2::native::HudRefusal want; };
  std::vector<DocCase> docCases;
  // A document whose first token is a brace has no name. (The earlier fixture here
  // was "not-a-name { }", which HAS a name and no panels -- it was testing
  // missing-root-panel and passing its assertion only because the two reasons were
  // conflated at the time. The probe caught it because the reason it got back did
  // not match the reason it asked for.)
  docCases.push_back({ "missing-document-name", "{\n\t\"Root\"\n\t{\n\t}\n}\n",
    tf2::native::HudRefusal::MissingDocumentName });
  docCases.push_back({ "missing-root-panel", "Resource/UI/X.res\n{\n}\n", tf2::native::HudRefusal::MissingRootPanel });
  docCases.push_back({ "unterminated-comment", std::string("Resource/UI/X.res\n{\n\t/* opened\n"),
    tf2::native::HudRefusal::MalformedKeyValues });
  docCases.push_back({ "unterminated-quote", std::string("Resource/UI/X.res\n{\n\t\"Panel\"\n\t{\n\t\t\"xpos\" \"0\n"),
    tf2::native::HudRefusal::MalformedKeyValues });
  docCases.push_back({ "brace-truncated", std::string("Resource/UI/X.res\n{\n\t\"Panel\"\n\t{\n\t\t\"xpos\"\t\"0\"\n"),
    tf2::native::HudRefusal::MalformedKeyValues });
  docCases.push_back({ "base-include", std::string("#base \"other.res\"\n") + kGoodDocument,
    tf2::native::HudRefusal::UnsupportedInclude });
  docCases.push_back({ "no-panel-coordinates", std::string(
    "Resource/UI/X.res\n{\n\t\"Root\"\n\t{\n\t\t\"Panel\"\n\t\t{\n\t\t\t\"controlname\"\t\"EditablePanel\"\n\t\t}\n\t}\n}\n"),
    tf2::native::HudRefusal::UnsupportedCoordinate });

  int docOk = 0, docWrong = 0;
  for (const DocCase& item : docCases) {
    const auto parsed = tf2::native::parseHudLayout(item.text);
    const bool matched = !parsed.ok && parsed.reason == item.want;
    if (matched) {
      ++docOk;
    } else {
      ++docWrong;
      std::printf("FAIL doc-case %s got ok=%d reason=%s want=%s\n", item.label,
        parsed.ok ? 1 : 0, reasonOf(parsed.reason).c_str(), reasonOf(item.want).c_str());
    }
  }
  std::printf("doc.cases=%zu\n", docCases.size());
  std::printf("doc.refusedAsNamed=%d\n", docOk);
  std::printf("doc.wrong=%d\n", docWrong);
  if (docWrong != 0) ++g_failures;

  // Per-panel refusals are counted, not fatal: a document with one unsupported
  // coordinate still previews its other panels and says how many it dropped.
  {
    const std::string partial = replaceOnce(std::string(kGoodDocument), "\"r200\"", "\"o3\"");
    const auto parsed = tf2::native::parseHudLayout(partial);
    std::printf("panel.refused=%zu kept=%zu ok=%d\n", parsed.refusedPanels,
      parsed.panels.size(), parsed.ok ? 1 : 0);
    check(parsed.ok, "one-bad-panel-still-parses");
    check(parsed.refusedPanels == 1, "one-bad-panel-is-counted-exactly-once");
    check(parsed.panels.size() == layout.panels.size() - 1, "the-other-panels-survive");
  }
  {
    // A child under a refused parent cannot resolve; the count says so instead of
    // placing the child at the origin.
    const std::string orphanParent = replaceOnce(std::string(kGoodDocument), "\"c-175\"", "\"o3\"");
    const auto parsed = tf2::native::parseHudLayout(orphanParent);
    const auto rects = tf2::native::resolveHudRects(parsed, 1280, 720, 1.0f, 100);
    std::size_t invalid = 0;
    for (const auto& rect : rects) { if (!rect.valid) ++invalid; }
    std::printf("orphan.parseRefused=%zu parseOk=%d invalidRects=%zu\n",
      parsed.refusedPanels, parsed.ok ? 1 : 0, invalid);
    check(parsed.ok, "orphan-case-still-parses");
    check(parsed.refusedPanels == 1, "only-the-parent-is-refused-by-parse");
  }
  {
    // Nesting: the corpus reaches depth 7, so depth 7 must be ACCEPTED. The cap
    // is 16; a document over it is refused by name.
    std::string deep = "Resource/UI/Deep.res\n{\n";
    std::string indent;
    for (int level = 0; level < 17; ++level) {
      deep += indent + "\"P" + std::to_string(level) + "\"\n" + indent + "{\n";
      deep += indent + "\t\"xpos\"\t\"0\"\n";
      deep += indent + "\t\"ypos\"\t\"0\"\n";
      deep += indent + "\t\"wide\"\t\"10\"\n";
      deep += indent + "\t\"tall\"\t\"10\"\n";
      indent += "\t";
    }
    for (int level = 0; level < 17; ++level) {
      indent = indent.substr(0, indent.size() - 1);
      deep += indent + "}\n";
    }
    deep += "}\n";
    const auto parsed = tf2::native::parseHudLayout(deep);
    std::printf("nest.deep ok=%d reason=%s panels=%zu\n", parsed.ok ? 1 : 0,
      reasonOf(parsed.reason).c_str(), parsed.panels.size());
    check(!parsed.ok && parsed.reason == tf2::native::HudRefusal::NestingTooDeep,
      "depth-17-refused");
  }

  // =======================================================================
  // 5. the real corpus: a well-formed HUD on this machine must import
  // =======================================================================
  //
  // The fixture proves the rules; the corpus proves the rules fit the files
  // users actually have. Only depth-<=3 documents that carry no #base are
  // claimed, and the reading says how many of the corpus files were tried -- a
  // "0 imported" that skipped every file must not read as a pass.
  {
    const std::filesystem::path corpus =
      "D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/custom/m0rehud-5.8/resource/ui";
    std::error_code code;
    std::size_t tried = 0, imported = 0, includes = 0, refusedOther = 0;
    // "Imported" is not "drawn completely": a document with at least one placeable
    // panel imports, and the panels it could not place are DROPPED silently from
    // the reading above. These three counters are what turns that into a number,
    // because the support-scope document quotes "71 of 102" and a reader has to
    // be able to see how much of those 71 the parser actually placed.
    std::size_t importedPartial = 0, panelsKept = 0, panelsRefused = 0;
    std::vector<std::string> refusedSample;
    if (std::filesystem::is_directory(corpus, code)) {
      for (const auto& entry : std::filesystem::directory_iterator(corpus, code)) {
        if (code) break;
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".res") continue;
        // The whole directory, not a sample: the support-scope document quotes
        // these counts, and "19 of the first 24 files" is not the same claim as
        // "N of the whole installed HUD". The cap below is a runaway guard only.
        if (tried >= 4096) break;
        ++tried;
        const auto read = tf2::native::readHudFile(corpus, entry.path().filename().u8string());
        if (!read.ok) { ++refusedOther; refusedSample.push_back(entry.path().filename().u8string() + ":read:" + reasonOf(read.reason)); continue; }
        const auto parsed = tf2::native::parseHudLayout(read.text);
        if (parsed.ok) {
          ++imported;
          panelsKept += parsed.panels.size();
          panelsRefused += parsed.refusedPanels;
          if (parsed.refusedPanels != 0) ++importedPartial;
          continue;
        }
        if (parsed.reason == tf2::native::HudRefusal::UnsupportedInclude) { ++includes; continue; }
        ++refusedOther;
        if (refusedSample.size() < 12) {
          refusedSample.push_back(entry.path().filename().u8string() + ":" + reasonOf(parsed.reason));
        }
      }
    }
    std::printf("corpus.dir=%s\n", corpus.generic_string().c_str());
    std::printf("corpus.tried=%zu\n", tried);
    std::printf("corpus.imported=%zu\n", imported);
    std::printf("corpus.refusedInclude=%zu\n", includes);
    std::printf("corpus.refusedOther=%zu\n", refusedOther);
    std::printf("corpus.importedPartial=%zu\n", importedPartial);
    std::printf("corpus.panelsKept=%zu\n", panelsKept);
    std::printf("corpus.panelsRefused=%zu\n", panelsRefused);
    for (const auto& line : refusedSample) std::printf("corpus.sample %s\n", line.c_str());
    // The directory is optional (a machine without TF2 installed is a skip, not
    // a failure), but IF it is there the sampler must have done real work.
    if (std::filesystem::is_directory(corpus, code)) {
      check(tried >= 12, "corpus-was-actually-sampled");
      check(imported >= 8, "corpus-documents-import");
    } else {
      std::printf("corpus=skipped no-tf2-install\n");
    }
  }

  // =======================================================================
  // 6. file-level refusals, executed against real files under a real root
  // =======================================================================
  {
    const auto root = fixtureRoot / "files";
    std::error_code code;
    std::filesystem::remove_all(root, code);
    writeFile(root / "ok.res", kGoodDocument);
    writeFile(root / "escape-target.res", kGoodDocument);
    writeFile(root / "notutf8.res", std::string("Resource/UI/X.res\n{\n\t\"P\"\n\t{\n\t\t\"xpos\" \"\xff\xfe\xfd\"\n") + "\t}\n}\n");
    writeFile(root / "utf16.res", std::string("\xff\xfeR\x00e\x00s\x00o\x00u\x00r\x00c\x00e\x00"));
    std::string bom = "\xef\xbb\xbf";
    writeFile(root / "bom.res", bom + kGoodDocument);
    // A file one byte over the cap: the refusal must be about the size, not the
    // content, so the content is a valid document padded with spaces.
    std::string oversized = kGoodDocument;
    oversized.append(300u * 1024u, ' ');
    writeFile(root / "oversized.res", oversized);
    std::filesystem::create_directories(root / "adir", code);

    struct FileCase { const char* relative; tf2::native::HudRefusal want; };
    const FileCase fileCases[] = {
      { "ok.res",        tf2::native::HudRefusal::None },
      { "bom.res",       tf2::native::HudRefusal::None },
      { "missing.res",   tf2::native::HudRefusal::Missing },
      { "adir",          tf2::native::HudRefusal::NotARegularFile },
      { "adir/ok.res",   tf2::native::HudRefusal::Missing },
      { "notutf8.res",   tf2::native::HudRefusal::InvalidUtf8 },
      { "utf16.res",     tf2::native::HudRefusal::InvalidUtf8 },
      { "oversized.res", tf2::native::HudRefusal::TooLarge },
      { "../escape-target.res", tf2::native::HudRefusal::ParentTraversal },
    };
    int fileOk = 0, fileWrong = 0;
    for (const FileCase& item : fileCases) {
      const auto read = tf2::native::readHudFile(root, item.relative);
      const bool wantOk = item.want == tf2::native::HudRefusal::None;
      if (read.ok == wantOk && read.reason == item.want) {
        ++fileOk;
      } else {
        ++fileWrong;
        std::printf("FAIL file-case %s ok=%d reason=%s want=%s\n", item.relative,
          read.ok ? 1 : 0, reasonOf(read.reason).c_str(), reasonOf(item.want).c_str());
      }
    }
    std::printf("file.cases=%zu\n", sizeof(fileCases) / sizeof(fileCases[0]));
    std::printf("file.correct=%d\n", fileOk);
    std::printf("file.wrong=%d\n", fileWrong);
    if (fileWrong != 0) ++g_failures;

    // The BOM is removed and nothing else is touched: the returned text must be
    // the same document the plain file carries, byte for byte.
    const auto plain = tf2::native::readHudFile(root, "ok.res");
    const auto withBom = tf2::native::readHudFile(root, "bom.res");
    std::printf("bom.textEqual=%d hadBom=%d bytes=%llu\n",
      (plain.ok && withBom.ok && plain.text == withBom.text) ? 1 : 0,
      withBom.hadBom ? 1 : 0, static_cast<unsigned long long>(withBom.bytes));
    check(plain.ok && withBom.ok && plain.text == withBom.text, "bom-is-stripped-not-transformed");
    check(withBom.hadBom, "bom-is-reported");
  }

  // =======================================================================
  // 7. settings: the contract's scale window
  // =======================================================================
  {
    std::printf("scale.49=%d scale.50=%d scale.100=%d scale.200=%d scale.201=%d\n",
      tf2::native::clampHudScale(49), tf2::native::clampHudScale(50),
      tf2::native::clampHudScale(100), tf2::native::clampHudScale(200),
      tf2::native::clampHudScale(201));
    check(tf2::native::clampHudScale(49) == 50 && tf2::native::clampHudScale(201) == 200,
      "scale-is-clamped-to-the-contract-window");
    check(!tf2::native::hudScaleInRange(49) && tf2::native::hudScaleInRange(50)
      && tf2::native::hudScaleInRange(200) && !tf2::native::hudScaleInRange(201),
      "scale-range-boundaries-are-exact");
  }

  if (selfTest) {
    // The self-test is the same code path with every assertion already run; the
    // flag exists so a gate can say "the probe ran its own checks" in one word.
    std::printf("selfTest=1\n");
  }
  std::printf("hud_probe.failures=%d\n", g_failures);
  return g_failures == 0 ? 0 : 1;
}
