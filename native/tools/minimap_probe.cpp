#include "minimap.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

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

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2 || std::string(argv[1]) != "--minimap-test") {
    std::fprintf(stderr, "usage: minimap_probe --minimap-test\n");
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
