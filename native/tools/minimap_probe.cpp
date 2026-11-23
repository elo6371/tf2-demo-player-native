#include "minimap.h"
#include "hud_text.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>

namespace {

using tf2::native::HudTextAlign;
using tf2::native::HudTextFont;
using tf2::native::HudTextQuad;
using tf2::native::HudTextRun;
using tf2::native::HudTextStats;
using tf2::native::hudTextAtlasPixels;
using tf2::native::hudTextLayout;
using tf2::native::hudTextMeasure;
using tf2::native::hudTextStats;
using tf2::native::kHudTextAtlasSize;
using tf2::native::MinimapBounds;
using tf2::native::MinimapPanel;
using tf2::native::MinimapPlayerState;
using tf2::native::MinimapPoint;
using tf2::native::MinimapProjection;
using tf2::native::minimapHitTest;
using tf2::native::minimapToWorld;
using tf2::native::makeMinimapProjection;
using tf2::native::worldToMinimap;

// The synthetic map is deliberately round numbers so the expected pixels can be
// derived by hand in the gate rather than copied from this program's output:
// 4096 world units square into 512x512 pixels gives scale 0.125 exactly,
// offsetX 0 and offsetY 512, so px = x/8 and py = 512 - y/8.
MinimapBounds syntheticBounds() {
  MinimapBounds bounds;
  bounds.minX = 0.0f;
  bounds.minY = 0.0f;
  bounds.maxX = 4096.0f;
  bounds.maxY = 4096.0f;
  bounds.pixelWidth = 512;
  bounds.pixelHeight = 512;
  bounds.valid = true;
  return bounds;
}

struct WorldPoint {
  int index;
  float x;
  float y;
};

const WorldPoint kPoints[] = {
    {0, 0.0f, 0.0f},
    {1, 4096.0f, 4096.0f},
    {2, 1024.0f, 3072.0f},
    {3, 2048.0f, 1024.0f},
};

std::vector<MinimapPlayerState> syntheticPlayers() {
  std::vector<MinimapPlayerState> players;
  // index 0: near the left middle, visible
  players.push_back(MinimapPlayerState{0, 2, 100, true, true, 1024.0f, 1024.0f, "alpha"});
  // index 1: upper left, visible
  players.push_back(MinimapPlayerState{1, 3, 80, true, true, 512.0f, 512.0f, "bravo"});
  // index 2: hidden -- the hit test must not see it even when the cursor is on it
  players.push_back(MinimapPlayerState{2, 2, 0, false, false, 0.0f, 0.0f, "charlie"});
  // index 3: right middle, visible; ties with index 0 around the panel centre
  players.push_back(MinimapPlayerState{3, 2, 100, true, true, 3072.0f, 1024.0f, "delta"});
  return players;
}

// Round-trip error expressed in PIXELS: reproject the recovered world point and
// measure the distance to the pixel we started from. A world-unit error would
// change meaning with the map scale; pixels are the unit the caller draws in.
float roundTripErrorPx(const MinimapBounds& bounds,
                       const MinimapProjection& projection, float worldX,
                       float worldY) {
  const MinimapPoint pixel = worldToMinimap(bounds, projection, worldX, worldY);
  float recoveredX = 0.0f;
  float recoveredY = 0.0f;
  if (!minimapToWorld(bounds, projection, pixel.px, pixel.py, recoveredX,
                      recoveredY)) {
    return 1.0e9f;
  }
  const MinimapPoint again =
      worldToMinimap(bounds, projection, recoveredX, recoveredY);
  const float dx = again.px - pixel.px;
  const float dy = again.py - pixel.py;
  return std::sqrt(dx * dx + dy * dy);
}

// 2026-11-22: --floor-bounds-test, the self-test of minimapBoundsFromFloor (the
// rectangle --hud-minimap-bounds bsp uses for an untabulated map). A separate
// mode so --minimap-test's pinned output does not move by a byte.
//
// Synthetic floor: a 4096 x 4096 square of 64-unit cells (2 triangles each),
// plus one tiny far-away triangle standing in for a skybox island. The expected
// rectangle is derived by hand: centroids span ~21..4075, the 0.5 % area trim
// moves each end in by about 20 units, and the margin is max(128, 4 % of the
// extent) ~= 162, so each edge lands within a few hundred units outside the
// square and the island at 100000 is trimmed away.
void pushCell(std::vector<tf2::native::MinimapFloorTriangle>& out, float x0, float y0, float size) {
  tf2::native::MinimapFloorTriangle a;
  a.ax = x0;        a.ay = y0;        a.az = 0.0f;
  a.bx = x0 + size; a.by = y0;        a.bz = 0.0f;
  a.cx = x0 + size; a.cy = y0 + size; a.cz = 0.0f;
  tf2::native::MinimapFloorTriangle b;
  b.ax = x0;        b.ay = y0;        b.az = 0.0f;
  b.bx = x0 + size; b.by = y0 + size; b.bz = 0.0f;
  b.cx = x0;        b.cy = y0 + size; b.cz = 0.0f;
  out.push_back(a);
  out.push_back(b);
}

int runFloorBoundsTest() {
  int checks = 0;
  int failed = 0;
  const auto expect = [&checks, &failed](const char* label, bool held) {
    ++checks;
    if (!held) ++failed;
    std::printf("minimap: floor-bounds check=%s %s\n", label, held ? "OK" : "FAIL");
  };

  std::vector<tf2::native::MinimapFloorTriangle> floor;
  for (int row = 0; row < 64; ++row) {
    for (int column = 0; column < 64; ++column) {
      pushCell(floor, static_cast<float>(column) * 64.0f, static_cast<float>(row) * 64.0f, 64.0f);
    }
  }
  const tf2::native::MinimapDerivedBounds square = tf2::native::minimapBoundsFromFloor(floor);
  std::printf("minimap: floor-bounds case=square valid=%d triangles=%zu min=%.3f,%.3f max=%.3f,%.3f\n",
    square.valid ? 1 : 0, square.triangles, static_cast<double>(square.minX),
    static_cast<double>(square.minY), static_cast<double>(square.maxX), static_cast<double>(square.maxY));
  expect("square-valid", square.valid);
  expect("square-triangles", square.triangles == 8192u);
  expect("square-minX", square.minX < 0.0f && square.minX > -300.0f);
  expect("square-minY", square.minY < 0.0f && square.minY > -300.0f);
  expect("square-maxX", square.maxX > 4096.0f && square.maxX < 4400.0f);
  expect("square-maxY", square.maxY > 4096.0f && square.maxY < 4400.0f);

  // The island: area 0.5 at (100000, 100000). It must not stretch the panel.
  std::vector<tf2::native::MinimapFloorTriangle> withIsland = floor;
  tf2::native::MinimapFloorTriangle island;
  island.ax = 100000.0f; island.ay = 100000.0f;
  island.bx = 100001.0f; island.by = 100000.0f;
  island.cx = 100000.0f; island.cy = 100001.0f;
  withIsland.push_back(island);
  const tf2::native::MinimapDerivedBounds trimmed = tf2::native::minimapBoundsFromFloor(withIsland);
  std::printf("minimap: floor-bounds case=island valid=%d triangles=%zu max=%.3f,%.3f\n",
    trimmed.valid ? 1 : 0, trimmed.triangles, static_cast<double>(trimmed.maxX),
    static_cast<double>(trimmed.maxY));
  expect("island-counted", trimmed.triangles == 8193u);
  expect("island-trimmed", trimmed.valid && trimmed.maxX < 5000.0f && trimmed.maxY < 5000.0f);

  // Degenerate inputs are refused, never turned into a default rectangle.
  const tf2::native::MinimapDerivedBounds empty =
    tf2::native::minimapBoundsFromFloor(std::vector<tf2::native::MinimapFloorTriangle>{});
  expect("empty-refused", !empty.valid && empty.triangles == 0u);
  std::vector<tf2::native::MinimapFloorTriangle> flat(4);   // zero-area triangles
  const tf2::native::MinimapDerivedBounds zero = tf2::native::minimapBoundsFromFloor(flat);
  expect("zero-area-refused", !zero.valid && zero.triangles == 0u);

  std::printf("minimap: floor-bounds verdict=%s checks=%d failed=%d\n",
    failed == 0 ? "PASS" : "FAIL", checks, failed);
  return failed == 0 ? 0 : 1;
}

// F0 (2026-11-23): --hud-text-test, the device-free self-test of hud_text.cpp,
// the DirectWrite HUD text the renderer draws with. It checks the contract, not
// pixels (those depend on the installed font versions): a face loads; Latin,
// Cyrillic and CJK resolve (the last two may come from fallback faces); width
// grows with size; right/centre alignment put the run where they say; an
// ellipsized run fits its limit; the same layout twice is byte-identical, quads
// and atlas; an unmappable code point is counted, not dropped silently.
std::uint32_t atlasHash() {
  const std::uint8_t* pixels = hudTextAtlasPixels();
  if (!pixels) return 0u;
  std::uint32_t hash = 2166136261u;
  const std::size_t count = static_cast<std::size_t>(kHudTextAtlasSize) * kHudTextAtlasSize;
  for (std::size_t i = 0; i < count; ++i) { hash ^= pixels[i]; hash *= 16777619u; }
  return hash;
}

bool sameQuads(const HudTextRun& a, const HudTextRun& b) {
  if (a.quads.size() != b.quads.size()) return false;
  for (std::size_t i = 0; i < a.quads.size(); ++i) {
    if (std::memcmp(&a.quads[i], &b.quads[i], sizeof(HudTextQuad)) != 0) return false;
  }
  return true;
}

int runHudTextTest() {
  int checks = 0;
  int failed = 0;
  const auto expect = [&](const char* name, bool ok) {
    ++checks;
    if (!ok) ++failed;
    std::printf("hud-text: check %s=%s\n", name, ok ? "ok" : "FAIL");
  };
  const HudTextStats initial = hudTextStats();
  std::printf("hud-text: engine available=%d reason=%s semibold=\"%s\" bold=\"%s\" faces=%zu\n",
    initial.available ? 1 : 0, initial.reason.c_str(), initial.semiBoldFamily.c_str(),
    initial.boldFamily.c_str(), initial.faces);
  expect("available", initial.available);
  if (!initial.available) {
    std::printf("hud-text: verdict=FAIL checks=%d failed=%d\n", checks, failed);
    return 1;
  }
  const std::string latin = "Medi Gun 82 Kestrel";
  const std::string cyrillic = "\xD0\xBF\xD0\xBB\xD0\xBE\xD1\x85\xD0\xBE";            // плохо
  const std::string cjk = "\xE4\xB8\xAD\xE6\x96\x87\xE5\x90\x8D";                    // 中文名
  const HudTextRun a = hudTextLayout(latin, HudTextFont::UiSemiBold, 18.0f, 100.0f, 40.0f, HudTextAlign::Left, 0.0f);
  const std::uint32_t hashAfterA = atlasHash();
  const HudTextRun again = hudTextLayout(latin, HudTextFont::UiSemiBold, 18.0f, 100.0f, 40.0f, HudTextAlign::Left, 0.0f);
  const std::uint32_t hashAfterAgain = atlasHash();
  const HudTextRun big = hudTextLayout(latin, HudTextFont::UiSemiBold, 28.0f, 100.0f, 40.0f, HudTextAlign::Left, 0.0f);
  const HudTextRun right = hudTextLayout(latin, HudTextFont::UiSemiBold, 18.0f, 500.0f, 40.0f, HudTextAlign::Right, 0.0f);
  const HudTextRun centre = hudTextLayout(latin, HudTextFont::UiSemiBold, 18.0f, 300.0f, 40.0f, HudTextAlign::Center, 0.0f);
  const HudTextRun cut = hudTextLayout("SDCK AlesKee tf2easy.com", HudTextFont::UiSemiBold, 18.0f, 0.0f, 0.0f, HudTextAlign::Left, 120.0f);
  const HudTextRun ru = hudTextLayout(cyrillic, HudTextFont::UiSemiBold, 18.0f, 0.0f, 0.0f, HudTextAlign::Left, 0.0f);
  const HudTextRun zh = hudTextLayout(cjk, HudTextFont::UiSemiBold, 18.0f, 0.0f, 0.0f, HudTextAlign::Left, 0.0f);
  const HudTextRun bold = hudTextLayout("358", HudTextFont::UiBold, 18.0f, 0.0f, 0.0f, HudTextAlign::Left, 0.0f);
  // U+10FFFD, the last plane-16 private-use code point: no shipped face maps it.
  const HudTextRun nothing = hudTextLayout("\xF4\x8F\xBF\xBD", HudTextFont::UiSemiBold, 18.0f, 0.0f, 0.0f, HudTextAlign::Left, 0.0f);
  const float measured = hudTextMeasure(latin, HudTextFont::UiSemiBold, 18.0f);
  std::printf("hud-text: latin px=%d width=%.2f quads=%zu lineHeight=%.0f ascent=%.0f fallback=%zu missing=%zu\n",
    a.pixelSize, static_cast<double>(a.width), a.quads.size(), static_cast<double>(a.lineHeight),
    static_cast<double>(a.ascent), a.fromFallback, a.missing);
  std::printf("hud-text: big width=%.2f right left=%.2f centre left=%.2f cut width=%.2f ellipsized=%d\n",
    static_cast<double>(big.width), static_cast<double>(right.left), static_cast<double>(centre.left),
    static_cast<double>(cut.width), cut.ellipsized ? 1 : 0);
  std::printf("hud-text: cyrillic quads=%zu fallback=%zu missing=%zu cjk quads=%zu fallback=%zu missing=%zu privateUse missing=%zu\n",
    ru.quads.size(), ru.fromFallback, ru.missing, zh.quads.size(), zh.fromFallback, zh.missing, nothing.missing);
  expect("latin-drawn", a.quads.size() >= 15u && a.width > 50.0f && a.missing == 0u);
  expect("measure-matches-layout", std::fabs(measured - a.width) < 0.01f);
  expect("deterministic-quads", sameQuads(a, again));
  // The second identical layout finds every glyph cached: nothing is written.
  expect("deterministic-atlas", hashAfterA != 0u && hashAfterA == hashAfterAgain);
  expect("bigger-is-wider", big.width > a.width * 1.4f);
  expect("right-align", std::fabs(right.left + right.width - 500.0f) < 0.01f);
  expect("centre-align", std::fabs(centre.left + centre.width * 0.5f - 300.0f) < 0.01f);
  expect("ellipsis-fits", cut.ellipsized && cut.width <= 120.01f && !cut.quads.empty());
  expect("cyrillic-resolves", ru.missing == 0u && ru.quads.size() == 5u);
  expect("cjk-resolves", zh.missing == 0u && zh.quads.size() == 3u);
  expect("bold-loads", !bold.quads.empty());
  expect("unmappable-counted", nothing.missing == 1u);
  expect("quads-inside-atlas", [&]() {
    for (const HudTextRun* run : {&a, &big, &ru, &zh}) {
      for (const HudTextQuad& q : run->quads) {
        if (!(q.u0 >= 0.0f && q.v0 >= 0.0f && q.u1 <= 1.0f && q.v1 <= 1.0f && q.u1 > q.u0 && q.v1 > q.v0)) return false;
      }
    }
    return true;
  }());
  // Re-laying the first run after many others must still produce the same quads
  // (same generation, glyphs cached, nothing moved).
  const HudTextRun later = hudTextLayout(latin, HudTextFont::UiSemiBold, 18.0f, 100.0f, 40.0f, HudTextAlign::Left, 0.0f);
  expect("cache-stable", sameQuads(a, later) || later.generation != a.generation);
  const HudTextStats stats = hudTextStats();
  std::printf("hud-text: stats glyphs=%zu rasterized=%zu resets=%zu atlasUsedHeight=%d revision=%llu generation=%u faces=%zu\n",
    stats.glyphsCached, stats.rasterized, stats.resets, stats.atlasUsedHeight,
    static_cast<unsigned long long>(stats.revision), static_cast<unsigned>(stats.generation), stats.faces);
  std::printf("hud-text: verdict=%s checks=%d failed=%d\n", failed == 0 ? "PASS" : "FAIL", checks, failed);
  return failed == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc == 2 && std::string(argv[1]) == "--hud-text-test") {
    return runHudTextTest();
  }
  if (argc == 2 && std::string(argv[1]) == "--floor-bounds-test") {
    return runFloorBoundsTest();
  }
  if (argc != 2 || std::string(argv[1]) != "--minimap-test") {
    std::fprintf(stderr, "usage: minimap_probe --minimap-test | --floor-bounds-test | --hud-text-test\n");
    return 2;
  }

  const MinimapBounds bounds = syntheticBounds();
  const MinimapProjection projection = makeMinimapProjection(bounds, 1.0f, 0.0f, 0.0f);

  std::printf("minimap: probe begin version=1\n");
  std::printf("minimap: synthetic bounds minX=%.6f minY=%.6f maxX=%.6f maxY=%.6f pixel=%dx%d valid=%d\n",
              static_cast<double>(bounds.minX), static_cast<double>(bounds.minY),
              static_cast<double>(bounds.maxX), static_cast<double>(bounds.maxY),
              bounds.pixelWidth, bounds.pixelHeight, bounds.valid ? 1 : 0);
  std::printf("minimap: projection valid=%d scale=%.6f offsetX=%.6f offsetY=%.6f yFlip=%d zoom=%.6f\n",
              projection.valid ? 1 : 0, static_cast<double>(projection.scale),
              static_cast<double>(projection.offsetX),
              static_cast<double>(projection.offsetY),
              projection.yFlip ? 1 : 0, 1.0);

  for (const WorldPoint& point : kPoints) {
    const MinimapPoint pixel =
        worldToMinimap(bounds, projection, point.x, point.y);
    std::printf("minimap: point index=%d worldX=%.6f worldY=%.6f px=%.6f py=%.6f inside=%d\n",
                point.index, static_cast<double>(point.x),
                static_cast<double>(point.y), static_cast<double>(pixel.px),
                static_cast<double>(pixel.py), pixel.inside ? 1 : 0);
  }

  for (const WorldPoint& point : kPoints) {
    std::printf("minimap: roundtrip index=%d errorPx=%.9f\n", point.index,
                static_cast<double>(
                    roundTripErrorPx(bounds, projection, point.x, point.y)));
  }

  const std::vector<MinimapPlayerState> players = syntheticPlayers();

  // nearest: the cursor sits on index 3, which is NOT the first visible entry in
  // the vector, so a "first hit wins" implementation returns 0 here.
  const int nearest =
      minimapHitTest(players, bounds, projection, 300.0f, 384.0f, 250.0f);
  std::printf("minimap: hittest case=nearest query=%.6f,%.6f radius=%.6f result=%d\n",
              300.0, 384.0, 250.0, nearest);

  // tie: indices 0 and 3 are exactly 128 px from the cursor; the smaller index
  // must win regardless of the vector order.
  const int tie = minimapHitTest(players, bounds, projection, 256.0f, 384.0f, 200.0f);
  std::printf("minimap: hittest case=tie query=%.6f,%.6f radius=%.6f result=%d\n",
              256.0, 384.0, 200.0, tie);

  // outside: no visible player within the radius.
  const int outside =
      minimapHitTest(players, bounds, projection, 500.0f, 500.0f, 10.0f);
  std::printf("minimap: hittest case=outside query=%.6f,%.6f radius=%.6f result=%d\n",
              500.0, 500.0, 10.0, outside);

  // invisible: the cursor is 2.8 px from index 2, which is not visible.
  const int invisible =
      minimapHitTest(players, bounds, projection, 2.0f, 510.0f, 10.0f);
  std::printf("minimap: hittest case=invisible query=%.6f,%.6f radius=%.6f result=%d\n",
              2.0, 510.0, 10.0, invisible);

  // order independence: the same query over the reversed vector must answer the
  // same index (the tie-break is the player index, not the slot).
  std::vector<MinimapPlayerState> reversed(players.rbegin(), players.rend());
  const int nearestReversed =
      minimapHitTest(reversed, bounds, projection, 300.0f, 384.0f, 250.0f);
  const int tieReversed =
      minimapHitTest(reversed, bounds, projection, 256.0f, 384.0f, 200.0f);
  std::printf("minimap: hittest order forward_nearest=%d reversed_nearest=%d forward_tie=%d reversed_tie=%d\n",
              nearest, nearestReversed, tie, tieReversed);

  MinimapPanel panel;
  panel.setMap(bounds, "synthetic");
  std::printf("minimap: panel initial collapsed=%d zoom=%.6f expandCount=%llu collapseCount=%llu\n",
              panel.collapsed() ? 1 : 0, static_cast<double>(panel.zoom()),
              static_cast<unsigned long long>(panel.expandCount()),
              static_cast<unsigned long long>(panel.collapseCount()));
  panel.expand();
  std::printf("minimap: panel after_expand collapsed=%d expandCount=%llu collapseCount=%llu\n",
              panel.collapsed() ? 1 : 0,
              static_cast<unsigned long long>(panel.expandCount()),
              static_cast<unsigned long long>(panel.collapseCount()));
  panel.collapse();
  std::printf("minimap: panel after_collapse collapsed=%d expandCount=%llu collapseCount=%llu\n",
              panel.collapsed() ? 1 : 0,
              static_cast<unsigned long long>(panel.expandCount()),
              static_cast<unsigned long long>(panel.collapseCount()));

  const MinimapPoint zoom1Point =
      worldToMinimap(bounds, projection, 2048.0f, 1024.0f);
  const float zoom1Distance =
      std::sqrt((zoom1Point.px - 256.0f) * (zoom1Point.px - 256.0f) +
                (zoom1Point.py - 256.0f) * (zoom1Point.py - 256.0f));
  std::printf("minimap: zoom1 point=3 px=%.6f py=%.6f center=%.6f,%.6f distance=%.6f\n",
              static_cast<double>(zoom1Point.px),
              static_cast<double>(zoom1Point.py), 256.0, 256.0,
              static_cast<double>(zoom1Distance));

  panel.setZoom(2.0f);
  std::printf("minimap: panel after_zoom zoom=%.6f zoomChanges=%llu\n",
              static_cast<double>(panel.zoom()),
              static_cast<unsigned long long>(panel.zoomChanges()));

  const MinimapProjection zoom2Projection = panel.projection();
  const MinimapPoint zoom2Point =
      worldToMinimap(bounds, zoom2Projection, 2048.0f, 1024.0f);
  const float zoom2Distance =
      std::sqrt((zoom2Point.px - 256.0f) * (zoom2Point.px - 256.0f) +
                (zoom2Point.py - 256.0f) * (zoom2Point.py - 256.0f));
  std::printf("minimap: zoom2 point=3 px=%.6f py=%.6f center=%.6f,%.6f distance=%.6f\n",
              static_cast<double>(zoom2Point.px),
              static_cast<double>(zoom2Point.py), 256.0, 256.0,
              static_cast<double>(zoom2Distance));
  std::printf("minimap: roundtrip_zoom2 point=3 errorPx=%.9f\n",
              static_cast<double>(
                  roundTripErrorPx(bounds, zoom2Projection, 2048.0f, 1024.0f)));

  // The panel's hit test must use the SAME zoomed projection it draws with:
  // at 2x, index 0 sits at pixel (0,512). A hit test that ignores zoom still
  // looks for it at (128,384) and answers -1 here.
  const int zoom2Hit = panel.hitTest(players, 0.0f, 512.0f, 10.0f);
  std::printf("minimap: zoom2_hittest point=0 query=%.6f,%.6f radius=%.6f result=%d\n",
              0.0, 512.0, 10.0, zoom2Hit);

  panel.resetView();
  std::printf("minimap: panel after_reset zoom=%.6f panX=%.6f panY=%.6f zoomChanges=%llu\n",
              static_cast<double>(panel.zoom()),
              static_cast<double>(panel.panX()),
              static_cast<double>(panel.panY()),
              static_cast<unsigned long long>(panel.zoomChanges()));
  std::printf("minimap: panel state %s\n", panel.stateLine().c_str());

  // The unavailable path: a map whose BSP is not on disk arrives as valid=false.
  // It must produce a NAMED reason, not a plausible blank canvas.
  MinimapBounds unavailable = bounds;
  unavailable.valid = false;
  MinimapPanel unavailablePanel;
  unavailablePanel.setMap(unavailable, "koth_bagel_rc13");
  std::printf("minimap: unavailable map=koth_bagel_rc13 boundsValid=0 projectionValid=%d reason=%s available=%d\n",
              unavailablePanel.projection().valid ? 1 : 0,
              unavailablePanel.unavailableReason().c_str(),
              unavailablePanel.available() ? 1 : 0);

  // A degenerate span is a second way to be unusable; the reason must name it.
  MinimapBounds degenerate = bounds;
  degenerate.maxX = degenerate.minX;
  MinimapPanel degeneratePanel;
  degeneratePanel.setMap(degenerate, "degenerate");
  std::printf("minimap: unavailable map=degenerate boundsValid=1 projectionValid=%d reason=%s available=%d\n",
              degeneratePanel.projection().valid ? 1 : 0,
              degeneratePanel.unavailableReason().c_str(),
              degeneratePanel.available() ? 1 : 0);

  // T65 ledger cross-check. The projection constants T65 published for the two
  // audited maps must be what THIS builder produces from the same canvas
  // rectangle. The rectangles below are the ledger's projection.minX/maxX/minY/
  // maxY (world units) and the expected scales are its scalePxPerUnit; both come
  // from evidence/t65-minimap/data/projection_ledger.json (sha256 e2d35aae...)
  // and are a TEST FIXTURE here, not product constants -- the product derives
  // the rectangle from BSP bounds at load time.
  struct LedgerCase {
    const char* map;
    float minX;
    float maxX;
    float minY;
    float maxY;
    double expectedScale;
  };
  const LedgerCase kLedgerCases[] = {
      {"cp_snakewater_final1", -6011.52f, 7067.52f, -6331.52f, 6747.52f,
       0.07829320806420043},
      {"pl_badwater", -3094.4f, 4726.4f, -3789.4f, 4031.4f,
       0.1309328968903437},
  };
  for (const LedgerCase& ledger : kLedgerCases) {
    MinimapBounds ledgerBounds;
    ledgerBounds.minX = ledger.minX;
    ledgerBounds.maxX = ledger.maxX;
    ledgerBounds.minY = ledger.minY;
    ledgerBounds.maxY = ledger.maxY;
    ledgerBounds.pixelWidth = 1024;
    ledgerBounds.pixelHeight = 1024;
    ledgerBounds.valid = true;
    const MinimapProjection ledgerProjection =
        makeMinimapProjection(ledgerBounds, 1.0f, 0.0f, 0.0f);
    std::printf(
        "minimap: ledger map=%s scale=%.12f expected=%.12f offsetX=%.6f "
        "offsetY=%.6f valid=%d\n",
        ledger.map, static_cast<double>(ledgerProjection.scale),
        ledger.expectedScale, static_cast<double>(ledgerProjection.offsetX),
        static_cast<double>(ledgerProjection.offsetY),
        ledgerProjection.valid ? 1 : 0);
  }

  std::printf("minimap: probe done ok=1\n");
  return 0;
}
