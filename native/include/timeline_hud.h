#pragma once

// I6 step 2 (2026-11-23, docs/UI-SHELL-DESIGN-2026-11-22.md section 9): the match
// timeline strip drawn across the bottom of the client.
//
// Section 9 line 229 asks for: one background band per round, the band's top edge in
// the winner's colour (grey while the round is still running), and kill marks with RED
// kills drawn above the midline and BLU kills below it. (The Chinese source text is
// left in the design document: a C4819 warning on this machine's code page is a build
// diagnostic, and the gate treats one as a failure.)
// The data layer (demo_match_timeline.h) already puts rounds, kills and anomaly
// counts on ONE server-tick axis and publishes the playback->server offset. This
// file turns that into geometry and nothing else: no Direct3D, no window handle and
// no renderer type, so the composition can be checked from a probe without a
// device. main_hud.cpp is the only caller and converts these shapes into the
// renderer's HudPrimitive list (native_renderer.h), which is the product's single
// HUD drawing path.

#include "demo_match_timeline.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace tf2::native {

// The team colours are shared with the minimap: the strip and the minimap sit in the
// same frame and must agree on what RED looks like, so there is one definition.
inline constexpr float kTeamRedColour[4] = {0.90f, 0.20f, 0.18f, 1.0f};
inline constexpr float kTeamBluColour[4] = {0.20f, 0.42f, 0.95f, 1.0f};
inline constexpr float kTeamNeutralColour[4] = {0.92f, 0.90f, 0.85f, 1.0f};

// One drawable, in client pixels. Text carries no font: the caller picks its UI font
// (the strip has to render Chinese, which the embedded 8x8 ASCII face cannot).
struct TimelineHudShape {
  enum class Kind { Rect = 0, Text = 1 };
  Kind kind = Kind::Rect;
  float x = 0.0f;
  float y = 0.0f;
  float w = 0.0f;
  float h = 0.0f;
  float colour[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  std::string text;
  float pixelSize = 0.0f;
};

// A rectangle already on screen that the strip must not cover (the minimap panel).
// placed = false means there is nothing to avoid.
struct TimelineHudAvoidRect {
  float x = 0.0f;
  float y = 0.0f;
  float w = 0.0f;
  float h = 0.0f;
  bool placed = false;
};

struct TimelineHudBand {
  std::int32_t index = 0;
  std::int32_t winTeam = -1;
  float x = 0.0f;
  float w = 0.0f;
  bool labelPlaced = false;
};

struct TimelineHudStrip {
  bool visible = false;
  float x = 0.0f;      // the strip's own rectangle
  float y = 0.0f;
  float w = 0.0f;
  float h = 0.0f;
  float trackX0 = 0.0f;  // where firstServerTick maps
  float trackX1 = 0.0f;  // where lastServerTick maps
  float trackY = 0.0f;   // the kill midline
  float trackH = 0.0f;
  std::int32_t firstServerTick = 0;
  std::int32_t lastServerTick = 0;
  std::int32_t playheadServerTick = -1;  // -1 = no playhead on the axis
  float playheadX = 0.0f;
  std::size_t shapes = 0;
  std::size_t ticksRed = 0;
  std::size_t ticksBlu = 0;
  std::size_t ticksUnknown = 0;
  std::vector<TimelineHudBand> bands;
  std::vector<TimelineHudShape> primitives;
};

// clientW/clientH: the client area in pixels. playheadPlaybackTick: the player's
// current playback tick, or -1 when nothing is loaded; it is converted to the server
// axis with the report's own offset, so the caller never does that arithmetic.
// bottomInset: how much of the bottom edge is someone else's (a taskbar-like band the
// shell already reserves); 0 places the strip at the safe margin.
TimelineHudStrip composeTimelineStrip(const MatchTimelineReport& report,
                                      int clientW, int clientH,
                                      std::int32_t playheadPlaybackTick,
                                      const TimelineHudAvoidRect& avoid = TimelineHudAvoidRect{},
                                      float bottomInset = 0.0f);

// One machine-readable line per strip plus one per band, so a gate can assert the
// geometry without a screenshot. Text is percent-encoded (one line = one record).
std::vector<std::string> formatTimelineHudLines(const TimelineHudStrip& strip);

} // namespace tf2::native
