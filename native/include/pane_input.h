#pragma once

// U4 step 3: which pane a client-space x lands in.
//
// The window procedure owns the mouse, but "which pane did this click select" has
// to be the same answer a probe and a command line can ask for -- otherwise the
// interaction layer and its tests are two different hit tests, and the one that
// ships is the one nobody tested. So the rule is a pure function here: tiny, total,
// and stated as numbers the probe can feed.
//
//   * width <= 0, paneCount == 0, or x outside [0, width)  -> -1 (no pane)
//   * one pane                                             -> 0 for the whole client
//   * two panes                                            -> the left half is
//     [0, width / 2), the right half takes the remainder -- exactly the split
//     drawSplit() makes (native_renderer_frame.cpp: the left viewport is width_ / 2
//     and the right one gets the rest). A one-pixel client therefore has NO left
//     pixel: x = 0 selects pane 1.
//
// paneCount is clamped to kPaneMaxPanes rather than rejected: a caller asking for
// more panes than exist gets the panes that exist. The clamp is a negative control
// in native_ui_probe, so "asked for three, got two" cannot silently pass as "asked
// for three, got three".

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "pane_boundary.h"

namespace tf2::native {

inline int paneForClientX(int x, int width, std::uint32_t paneCount) {
  if (width <= 0 || paneCount == 0u) return -1;
  if (x < 0 || x >= width) return -1;
  const std::size_t panes = std::min<std::size_t>(paneCount, kPaneMaxPanes);
  if (panes == 0u) return -1;
  if (panes == 1u) return 0;
  const int split = width / 2;
  return x < split ? 0 : 1;
}

}  // namespace tf2::native
