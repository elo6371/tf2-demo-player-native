#pragma once

// N14/U3a (2026-11-15): the minimap basemap, as rectangles.
//
// WHY A HEADER AND NOT A .cpp. The product does not link src/minimap.cpp -- only
// minimap_probe does (native/cmake/tf2-probes-minimap.cmake: add_executable(
// minimap_probe tools/minimap_probe.cpp src/minimap.cpp)) -- and main_hud.cpp is
// not in the probe target. An algorithm written into either .cpp would therefore
// exist in one binary and not the other. The same constraint is why the inline
// projection helpers already live in native/include/minimap.h (:327-376). This
// file follows that rule rather than inventing a second one: the whole basemap is
// header-inline, so the product and any probe or gate that includes this header
// compute with the SAME code.
//
// WHAT IS DRAWN, AND WHY IT IS RECTANGLES. The HUD has three primitive kinds --
// Rect, Text, Circle (native/include/native_renderer.h HudPrimitiveKind) -- and no
// line or polygon, so a floor plan has to be emitted as axis-aligned boxes. The
// pipeline is the one N14 measured (tools/n14-oracle, docs/N14-MINIMAP-BASEMAP-
// 2026-11-15.md section 7.2/7.3, strategy C):
//
//   1 FILL    every kept floor triangle into a panel-sized bitmask, even-odd
//             scanline, at the PANEL'S OWN pixel size (not a fixed 160x160: a
//             narrow window clamps the panel through hudLayoutPanels, and a
//             buffer that assumed 160 square would draw a map that is not the
//             one the markers are on).
//   2 EDGES   the 1-px boundary of that mask (a filled pixel with an empty
//             4-neighbour). Outline, not silhouette: a filled silhouette on a
//             multi-storey map reads as one floor, and all seven maps in the
//             table are multi-storey (main layer share 0.31-0.78).
//   3 LAYERS  the same fill/edge pass repeated inside each 256 u z band that
//             carries at least 5 % of the floor area, ORed into the outline. This
//             is the only thing that shows a second storey at all; without it
//             every map is a single outline of its top surface.
//   4 MERGE   equal-width runs on consecutive rows become one taller box. The HUD
//             shader fills an axis-aligned box, so this costs nothing and the
//             merged list is strictly smaller for exactly the same pixels.
//
// WHAT IS NOT DRAWN, AND MUST NOT BE. No height shading (one neutral colour is
// all the palette has room for, and a shaded basemap would fight the team-coloured
// objective rings the gate compares for brightness). No player dots -- the
// overlay's player samples are up to 1057 ticks (~16 s) stale on the reference
// recording, and a moving dot with no staleness marker would be a fabricated
// reading. No payload cart -- only pl_upward_f12 has a team_train_watcher and it
// is not a control point, so the data gap is named instead of guessed.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "minimap.h"

namespace tf2::native {

// MinimapFloorTriangle (the input this file consumes) is declared in
// native/include/minimap.h beside MinimapHudFrame, because it is part of the
// contract between main.cpp and the HUD and not part of this algorithm.

// A finished box in PANEL-LOCAL pixels. x0/y0 are the pixel indices of the first
// covered pixel; x1/y1 are EXCLUSIVE (x1 - x0 is the width the shader is given).
// Half-open on purpose: every rect of a merged run then describes exactly the
// pixels the mask had, and a reader can add the areas up without a fencepost.
struct MinimapLineRect {
  int x0 = 0; int y0 = 0; int x1 = 0; int y1 = 0;
};

// What one build produced. The counters are printed once per build, so a reader
// can tell "the basemap is empty" from "the basemap was never built" from "the
// basemap was built from geometry that was not there".
struct MinimapLinework {
  bool valid = false;
  int width = 0;                  // panel pixels this was built for
  int height = 0;
  std::size_t triangles = 0;      // floor triangles handed in
  std::size_t kept = 0;           // of those, inside the band window of a pass
  std::size_t bands = 0;          // z bands that cleared the 5 % share
  std::size_t edgePixels = 0;     // set bits in the combined outline mask
  std::size_t runs = 0;           // horizontal runs before merging
  std::vector<MinimapLineRect> rects;
};

// The thresholds, named once. They are the numbers N14 measured with, so a change
// here is a change in what the gate's expected counts mean and must move together
// with the gate (minimap-hud-check.sh) and the oracle (tools/n14-oracle).
inline constexpr double kMinimapBandHeight = 256.0;     // world units per z band
inline constexpr double kMinimapBandMinShare = 0.05;    // of floor area
inline constexpr double kMinimapMinFaceArea = 1024.0;   // world units^2
inline constexpr double kMinimapBandPad = 16.0;         // a band's own z window
inline constexpr double kMinimapUpwardNormalZ = 0.7;    // floor test

// Footprint area of a triangle on the panel's plane (the xy projection).
// |cross| / 2, written with the same sign-free absolute the oracle uses so the
// two implementations cannot disagree about which way a face winds.
inline double minimapTriangleAreaXY(const MinimapFloorTriangle& t) {
  const double cross = (static_cast<double>(t.bx) - t.ax)
      * (static_cast<double>(t.cy) - t.ay)
      - (static_cast<double>(t.cx) - t.ax)
      * (static_cast<double>(t.by) - t.ay);
  return std::fabs(cross) * 0.5;
}

inline double minimapTriangleMeanZ(const MinimapFloorTriangle& t) {
  return (static_cast<double>(t.az) + t.bz + t.cz) / 3.0;
}

// The z band a triangle's own mean height falls in. Truncating toward zero would
// fold (-256,-1) and (-1,1) into band 0, so the floor is taken explicitly.
inline int minimapBandOf(double meanZ) {
  return static_cast<int>(std::floor(meanZ / kMinimapBandHeight));
}

// Even-odd scanline fill of the kept triangles into a width x height mask.
//
// The projection is the caller's -- the product passes minimapPanelProjection's
// result, which at a 160x160 panel is exactly the equal-aspect centred fit the
// oracle recomputes from the declared rectangle. Rows are sampled at their pixel
// CENTRE (yy + 0.5) and a horizontal span covers every pixel index whose centre
// falls inside it, which is what makes the mask a picture of pixels rather than
// of coordinates.
//
// zMin/zMax are an optional band window on the triangle's mean height; when the
// window is absent (hadBand false) every kept triangle is filled.
inline void minimapFillMask(const std::vector<MinimapFloorTriangle>& triangles,
                            const MinimapProjection& projection,
                            int width, int height,
                            bool hasBand, double zMin, double zMax,
                            std::vector<std::uint8_t>& mask,
                            std::size_t& kept) {
  mask.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0);
  kept = 0;
  if (!projection.valid || width <= 0 || height <= 0) return;
  const double scale = projection.scale;
  // minimapPanelPoint is the one definition of "world -> panel"; restating its
  // arithmetic here instead of calling it per vertex would be a second copy of
  // the y flip, and the y flip is the sign a wrong basemap gets wrong.
  const auto px = [&](float wx) { return static_cast<double>(wx) * scale
      + projection.offsetX; };
  const auto py = [&](float wy) { return projection.offsetY
      - static_cast<double>(wy) * scale; };
  for (const MinimapFloorTriangle& t : triangles) {
    if (minimapTriangleAreaXY(t) < kMinimapMinFaceArea) continue;
    if (hasBand) {
      const double zc = minimapTriangleMeanZ(t);
      if (zc < zMin || zc > zMax) continue;
    }
    ++kept;
    const double xs[3] = {px(t.ax), px(t.bx), px(t.cx)};
    const double ys[3] = {py(t.ay), py(t.by), py(t.cy)};
    double yLo = ys[0], yHi = ys[0];
    for (int k = 1; k < 3; ++k) {
      if (ys[k] < yLo) yLo = ys[k];
      if (ys[k] > yHi) yHi = ys[k];
    }
    // One row of slack each side: a triangle whose edge lands exactly on a row
    // boundary still has to paint the row whose centre it crosses.
    int rowLo = static_cast<int>(yLo) - 1;
    int rowHi = static_cast<int>(yHi) + 1;
    if (rowLo < 0) rowLo = 0;
    if (rowHi > height - 1) rowHi = height - 1;
    for (int yy = rowLo; yy <= rowHi; ++yy) {
      const double yc = yy + 0.5;
      double crossings[3];
      int count = 0;
      for (int k = 0; k < 3; ++k) {
        const int next = (k + 1) % 3;
        const double ay = ys[k], by = ys[next];
        if ((ay <= yc && yc < by) || (by <= yc && yc < ay)) {
          const double ax = xs[k], bx = xs[next];
          crossings[count++] = ax + (yc - ay) / (by - ay) * (bx - ax);
        }
      }
      if (count < 2) continue;
      // Three edges give at most two crossings; when both are present the
      // smaller is the entry and the larger the exit.
      double xIn = crossings[0], xOut = crossings[1];
      if (xOut < xIn) { const double swap = xIn; xIn = xOut; xOut = swap; }
      int xa = static_cast<int>(xIn);
      int xb = static_cast<int>(xOut);
      if (xa < 0) xa = 0;
      if (xb > width - 1) xb = width - 1;
      const std::size_t base = static_cast<std::size_t>(yy)
          * static_cast<std::size_t>(width);
      for (int xx = xa; xx <= xb; ++xx) mask[base + static_cast<std::size_t>(xx)] = 1;
    }
  }
}

// The 1-px boundary of a footprint: a set pixel with an empty 4-neighbour. The
// panel's own edge counts as empty, so the outline of a shape that runs off the
// panel is still drawn along the cut -- a basemap with an unclosed edge would
// look like a hole in the map.
inline void minimapEdgeOf(const std::vector<std::uint8_t>& mask, int width, int height,
                          std::vector<std::uint8_t>& out, std::size_t& edgePixels) {
  out.assign(mask.size(), 0);
  edgePixels = 0;
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const std::size_t index = static_cast<std::size_t>(y)
          * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
      if (mask[index] == 0) continue;
      const bool empty = x == 0 || x == width - 1 || y == 0 || y == height - 1
          || mask[index - 1] == 0 || mask[index + 1] == 0
          || mask[index - static_cast<std::size_t>(width)] == 0
          || mask[index + static_cast<std::size_t>(width)] == 0;
      if (empty) {
        out[index] = 1;
        ++edgePixels;
      }
    }
  }
}

// Turn a combined outline mask into merged boxes. Runs are keyed by their
// (x0, x1) pair and a key remembers the LAST ROW it was seen on; the key is
// dropped when its last row is older than the row being processed minus one.
// Comparing against the previous SPAN's row instead -- the obvious way to write
// this -- drops the key on the second span of a row, because a row change
// happens between spans too, and the merge then silently does nothing.
inline void minimapMergeRuns(const std::vector<std::uint8_t>& mask, int width, int height,
                             std::size_t& runs, std::vector<MinimapLineRect>& rects) {
  rects.clear();
  runs = 0;
  struct Active { int x0; int x1; int lastRow; std::size_t box; };
  std::vector<Active> active;
  for (int y = 0; y < height; ++y) {
    for (std::size_t index = active.size(); index-- > 0;) {
      if (active[index].lastRow < y - 1) active.erase(active.begin() + index);
    }
    const std::size_t base = static_cast<std::size_t>(y) * static_cast<std::size_t>(width);
    int x = 0;
    while (x < width) {
      if (mask[base + static_cast<std::size_t>(x)] == 0) { ++x; continue; }
      const int x0 = x;
      while (x < width && mask[base + static_cast<std::size_t>(x)] != 0) ++x;
      ++runs;
      bool extended = false;
      for (Active& entry : active) {
        if (entry.x0 == x0 && entry.x1 == x) {
          rects[entry.box].y1 = y + 1;
          entry.lastRow = y;
          extended = true;
          break;
        }
      }
      if (extended) continue;
      MinimapLineRect box;
      box.x0 = x0; box.y0 = y; box.x1 = x; box.y1 = y + 1;
      rects.push_back(box);
      Active entry;
      entry.x0 = x0; entry.x1 = x; entry.lastRow = y; entry.box = rects.size() - 1;
      active.push_back(entry);
    }
  }
}

// The whole basemap for one panel size: fill, outline, layer bands, merge.
//
// Zero horizontal runs is a VALID result (a map whose floor is entirely outside
// this panel) and is reported as valid with an empty rect list, not as a failure:
// "the basemap is empty here" and "the basemap could not be built" send a reader
// to two different places.
inline MinimapLinework minimapBuildLinework(const std::vector<MinimapFloorTriangle>& triangles,
                                            const MinimapProjection& projection,
                                            int width, int height) {
  MinimapLinework linework;
  linework.width = width;
  linework.height = height;
  linework.triangles = triangles.size();
  if (!projection.valid || width <= 0 || height <= 0) return linework;

  std::vector<std::uint8_t> allMask;
  std::size_t keptAll = 0;
  minimapFillMask(triangles, projection, width, height, false, 0.0, 0.0, allMask, keptAll);
  linework.kept = keptAll;

  std::vector<std::uint8_t> combined;
  minimapEdgeOf(allMask, width, height, combined, linework.edgePixels);

  // The layer pass. A band counts only when it carries at least 5 % of the floor
  // area, so the thin decorative shelves do not turn into noise: N14 measured the
  // share per band on all seven maps and 5 % is the cutoff that keeps every real
  // storey and drops every sliver.
  std::vector<double> bandArea;   // by band index, offset by the lowest band seen
  int lowestBand = 0;
  bool haveBand = false;
  for (const MinimapFloorTriangle& t : triangles) {
    const double area = minimapTriangleAreaXY(t);
    if (area < kMinimapMinFaceArea) continue;
    const int band = minimapBandOf(minimapTriangleMeanZ(t));
    if (!haveBand) { lowestBand = band; haveBand = true; bandArea.clear(); }
    if (band < lowestBand) {
      bandArea.insert(bandArea.begin(),
        static_cast<std::size_t>(lowestBand - band), 0.0);
      lowestBand = band;
    }
    const std::size_t slot = static_cast<std::size_t>(band - lowestBand);
    if (bandArea.size() <= slot) bandArea.resize(slot + 1, 0.0);
    bandArea[slot] += area;
  }
  double totalArea = 0.0;
  for (double area : bandArea) totalArea += area;
  if (totalArea > 0.0) {
    for (std::size_t slot = 0; slot < bandArea.size(); ++slot) {
      if (bandArea[slot] / totalArea < kMinimapBandMinShare) continue;
      ++linework.bands;
      const double bandBase = static_cast<double>(lowestBand + static_cast<int>(slot))
          * kMinimapBandHeight;
      std::vector<std::uint8_t> bandMask;
      std::size_t keptBand = 0;
      minimapFillMask(triangles, projection, width, height, true,
        bandBase - kMinimapBandPad, bandBase + kMinimapBandHeight + kMinimapBandPad,
        bandMask, keptBand);
      std::vector<std::uint8_t> bandEdges;
      std::size_t ignored = 0;
      minimapEdgeOf(bandMask, width, height, bandEdges, ignored);
      for (std::size_t index = 0; index < combined.size(); ++index) {
        if (bandEdges[index] != 0 && combined[index] == 0) {
          combined[index] = 1;
          ++linework.edgePixels;
        }
      }
    }
  }

  minimapMergeRuns(combined, width, height, linework.runs, linework.rects);
  linework.valid = true;
  return linework;
}

}  // namespace tf2::native
