#pragma once
// T67: the generic HUD panel container.
//
// WHAT THIS IS
//   The layout rule for a set of anchored panels: which corner each one hangs
//   from, how they stack, what happens when the client is too narrow, and what a
//   collapsed panel occupies. It is PURE arithmetic -- no device, no drawing, no
//   file I/O -- so the rule can be tested without a window, and the renderer
//   only ever receives the rectangles this module produced. The T67 foundation
//   does not wire real player/objective data into panels yet; that is the next
//   step (T63 fields), and keeping the container pure is what makes that wiring
//   a caller change rather than a layout rewrite.
//
// COORDINATES
//   Back-buffer pixels, top-left origin, matching the HUD pass. safeInset is the
//   margin every panel keeps from the client edge (the top bar needs the centre
//   clear; the caller passes the inset it wants).

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

namespace tf2::native {

struct HudPanelSpec {
  std::string id;
  int anchor = 0;        // 0=top-left, 1=top-right, 2=bottom-left, 3=bottom-right
  int order = 0;         // lower first within an anchor
  float width = 0.0f;
  float height = 0.0f;
  float marginX = 0.0f;  // gap from the anchored edge
  float marginY = 0.0f;  // gap between stacked panels
  bool visible = true;
  bool collapsed = false; // a collapsed panel keeps its place with height 0
};

struct HudPanelRect {
  std::string id;
  int x = 0, y = 0, w = 0, h = 0;
  bool placed = false;
};

struct HudPanelLayout {
  std::vector<HudPanelRect> panels;
  std::size_t dropped = 0;   // panels that did not fit
  std::size_t collapsed = 0; // placed panels drawn at height 0
};

struct HudPanelSelfTest {
  std::size_t cases = 0;
  std::size_t passed = 0;
  std::size_t failed = 0;
};

inline HudPanelLayout hudLayoutPanels(const std::vector<HudPanelSpec>& specs,
                                      int clientWidth, int clientHeight, int safeInset) {
  HudPanelLayout layout;
  if (clientWidth <= 0 || clientHeight <= 0) {
    layout.dropped = specs.size();
    return layout;
  }
  std::vector<const HudPanelSpec*> ordered;
  ordered.reserve(specs.size());
  for (const HudPanelSpec& spec : specs) {
    if (spec.visible) ordered.push_back(&spec);
  }
  std::stable_sort(ordered.begin(), ordered.end(),
    [](const HudPanelSpec* a, const HudPanelSpec* b) {
      if (a->order != b->order) return a->order < b->order;
      return a->id < b->id;
    });
  const int usableWidth = (std::max)(0, clientWidth - 2 * safeInset);
  int topLeftY = safeInset;
  int topRightY = safeInset;
  int bottomLeftY = clientHeight - safeInset;
  int bottomRightY = clientHeight - safeInset;
  for (const HudPanelSpec* spec : ordered) {
    int width = static_cast<int>(spec->width + 0.5f);
    int height = spec->collapsed ? 0 : static_cast<int>(spec->height + 0.5f);
    if (width < 0) width = 0;
    if (height < 0) height = 0;
    if (width > usableWidth) width = usableWidth;
    const int marginX = static_cast<int>(spec->marginX + 0.5f);
    const int marginY = static_cast<int>(spec->marginY + 0.5f);
    const bool right = (spec->anchor == 1 || spec->anchor == 3);
    const bool bottom = (spec->anchor == 2 || spec->anchor == 3);
    int x = right ? (clientWidth - safeInset - marginX - width)
                  : (safeInset + marginX);
    if (x < safeInset) x = safeInset;
    if (x + width > clientWidth - safeInset) {
      x = (std::max)(safeInset, clientWidth - safeInset - width);
    }
    // marginY is the gap from the anchored edge and between stacked panels, so
    // the cursor stores the previous panel's edge and each step adds the gap
    // exactly once. (Adding it on both sides of a panel doubles the gap and makes
    // "players y=56" drift to 60 at marginY=4 -- the self-test pins this.)
    int y = 0;
    if (bottom) {
      int& cursor = (spec->anchor == 3) ? bottomRightY : bottomLeftY;
      y = cursor - marginY - height;
      cursor = y;
    } else {
      int& cursor = (spec->anchor == 1) ? topRightY : topLeftY;
      y = cursor + marginY;
      cursor = y + height;
    }
    HudPanelRect rect;
    rect.id = spec->id;
    rect.x = x;
    rect.y = y;
    rect.w = width;
    rect.h = height;
    if (y < safeInset || y + height > clientHeight - safeInset) {
      ++layout.dropped;
      continue;
    }
    rect.placed = true;
    if (spec->collapsed) ++layout.collapsed;
    layout.panels.push_back(rect);
  }
  return layout;
}

// A handful of deterministic cases: stacking, right/bottom anchoring, collapse,
// visibility, ordering, narrow clamping and the drop path. The product probe
// prints the counts so a broken rule is a reading, not a screenshot opinion.
inline HudPanelSelfTest hudPanelSelfTest() {
  HudPanelSelfTest test;
  auto check = [&test](bool condition) {
    ++test.cases;
    if (condition) ++test.passed; else ++test.failed;
  };
  std::vector<HudPanelSpec> specs;
  specs.push_back({"score", 0, 0, 200.0f, 40.0f, 0.0f, 4.0f, true, false});
  specs.push_back({"players", 0, 1, 200.0f, 120.0f, 0.0f, 4.0f, true, false});
  specs.push_back({"minimap", 1, 0, 160.0f, 160.0f, 4.0f, 4.0f, true, false});
  specs.push_back({"objective", 2, 0, 240.0f, 48.0f, 0.0f, 4.0f, true, false});
  specs.push_back({"collapsed", 1, 1, 160.0f, 100.0f, 4.0f, 4.0f, true, true});
  specs.push_back({"hidden", 0, 2, 100.0f, 100.0f, 0.0f, 0.0f, false, false});
  const HudPanelLayout layout = hudLayoutPanels(specs, 1280, 720, 8);
  // The order is (order, id), so equal-order panels come out alphabetically:
  // minimap, objective, score, then collapsed, players. The ids are asserted as
  // one case because the ORDER is part of the contract, not an accident of the
  // input vector.
  check(layout.panels.size() == 5);
  check(layout.dropped == 0);
  check(layout.collapsed == 1);
  check(layout.panels[0].id == "minimap" && layout.panels[1].id == "objective"
        && layout.panels[2].id == "score" && layout.panels[3].id == "collapsed"
        && layout.panels[4].id == "players");
  check(layout.panels[0].x == 1280 - 8 - 4 - 160 && layout.panels[0].y == 12);
  check(layout.panels[1].x == 8 && layout.panels[1].y == 720 - 8 - 4 - 48);
  check(layout.panels[2].x == 8 && layout.panels[2].y == 12);
  check(layout.panels[3].h == 0 && layout.panels[3].y == 8 + 4 + 160 + 4);
  check(layout.panels[4].y == 56);  // score 12 + 40 + marginY 4
  const HudPanelLayout narrow = hudLayoutPanels(specs, 200, 720, 8);
  check(!narrow.panels.empty() && narrow.panels[2].w == 200 - 2 * 8 && narrow.panels[0].w == 160);
  std::vector<HudPanelSpec> tall;
  tall.push_back({"tall", 0, 0, 100.0f, 900.0f, 0.0f, 0.0f, true, false});
  check(hudLayoutPanels(tall, 1280, 720, 8).dropped == 1);
  return test;
}

} // namespace tf2::native
