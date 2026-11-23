#pragma once

// H1 (2026-11-23, UI shell design section 7): the broadcast HUD -- the overlay a
// spectator stream puts on top of a demo instead of the player's own HUD.
//
// WHAT THIS MODULE OWNS
//   The spectator overlay only: a top bar (both scores and the round clock), the
//   two team rosters along the sides with health, class and Medic Gun charge, and
//   the followed player's box at the bottom. It owns no reading of its own -- the
//   roster arrives as the overview's OverviewPlayer rows (map_overview.h), the
//   clock and scores as the live readout's HudLiveReading, the charges as the
//   frame's gameplay-FX reading -- and it composes Renderer::HudPrimitive values
//   exactly as the timeline strip and the review banner do, so the whole overlay
//   is one HUD pass with no new shader path and no new texture.
//
// WHY IT IS A PRODUCT MODULE (not a probe-only pure module like timeline_hud.cpp)
//   Its inputs live behind TUs that link d3d11/dwrite: the roster reader is
//   map_overview.cpp and the live readout is main_hud.cpp. A demo_open_probe
//   switch would force that reader into tf2_core first; this module is verified
//   the way map_overview.cpp is -- a trace line a gate can assert and a capture
//   frame a gate can diff (--hud-broadcast --capture-tick). The compose call
//   itself is still pure: no window, no renderer, no file access.
//
// COLORS: team red and blue are the timeline module's kTeamRedColour /
// kTeamBluColour (timeline_hud.h:32-33). The overlay and the strip sit in the same
// frame and must agree about what RED looks like, so there is one definition.

#include <cstdint>
#include <string>
#include <vector>

#include "map_overview.h"
#include "native_renderer.h"

namespace tf2::native {

// One Medic Gun beam as the overlay draws it: who heals whom, and how charged the
// gun is. Filled from FxMedigun (gameplay_effects.h:102-114) by the caller, so
// this header does not depend on the gameplay reader.
struct BroadcastHudMedigun {
  std::int32_t entity = -1;             // the weapon entity
  std::int32_t owner = -1;              // the medic
  std::int32_t target = -1;             // who is being healed
  bool chargeRelease = false;           // m_bChargeRelease: the gun is firing uber
  bool chargeKnown = false;             // hasChargeLevel was true
  float chargeLevel = 0.0f;             // 0..1, the gun's own charge
  int kind = 0;                         // MedigunChargeKind as an int (trace only)
};

// The live readout, as a plain value. Deliberately a copy of the numbers and not a
// reference to HudLiveReading, which is local to main_hud.cpp: the one thing this
// module must not do is grow a second reader of the same net properties.
struct BroadcastHudLive {
  bool roundTimerKnown = false;
  double roundTimerSeconds = 0.0;
  bool roundTimerPaused = false;
  bool redKnown = false;
  std::int32_t red = 0;
  bool bluKnown = false;
  std::int32_t blu = 0;
};

struct BroadcastHudInput {
  int clientWidth = 0;
  int clientHeight = 0;
  std::string mapName;                  // printed under the bar, never read from disk
  std::string modeHint;                 // e.g. "6v6 Highlander"; empty prints mapName
  BroadcastHudLive live;
  std::vector<OverviewPlayer> players;  // mapOverviewBuildInput's rows
  std::vector<BroadcastHudMedigun> mediguns;
  // The player the camera is watching (the relay's decision, or the demo's view
  // entity as the fallback main_hud.cpp already resolved). -1 draws no box.
  std::int32_t observedEntity = -1;
  std::string observedName;
  bool observedNameKnown = false;
  std::int32_t serverTick = 0;          // the clock death countdowns are measured on
  double tickRate = 66.6666667;
};

// One roster row as it was placed. Published for the trace so a gate can compare
// "the row exists and says 76" with the frame, instead of counting pixels twice.
struct BroadcastHudRow {
  std::int32_t entity = -1;
  std::int32_t team = 0;                // 2 RED, 3 BLU
  std::int32_t cls = 0;
  bool medic = false;                   // row 0 of a column: the team's Medic Gun
  bool alive = true;
  bool placed = false;                  // false: had no room in the column
  int x = 0, y = 0, w = 0, h = 0;
  std::string name;
  std::int32_t health = 0;
  std::int32_t maxHealth = 0;
  double healthFraction = 0.0;
  bool healthKnown = false;
  bool overheal = false;                // health > maxHealth
  bool low = false;                     // healthFraction < 0.5
  bool chargeKnown = false;
  double chargeFraction = 0.0;
  bool chargeRelease = false;
  int chargePercent = 0;
  bool deathKnown = false;
  double deadSeconds = 0.0;
};

struct BroadcastHudDraw {
  bool active = false;                  // the switch was on this frame
  bool visible = false;                 // something was composed and can be drawn
  std::string reason = "inactive";      // active but invisible: why
  std::vector<Renderer::HudPrimitive> primitives;
  // Everything that changes the picture, in one string: the caller appends it to
  // its publish key, so a health tick republishes and a still frame does not.
  std::string signature;
  double scale = 1.0;
  int clientW = 0, clientH = 0;
  // geometry, all in client pixels (the trace prints these)
  int barX = 0, barY = 0, barW = 0, barH = 0;
  int hintX = 0, hintY = 0, hintW = 0, hintH = 0;
  int columnW = 0, rowH = 0, rowGap = 0;
  int bluX = 0, bluY = 0, redX = 0, redY = 0;
  int observedX = 0, observedY = 0, observedW = 0, observedH = 0;
  // counters: what a reader needs to tell "no players" from "rows clipped"
  int playersTotal = 0, rowsDrawn = 0, rowsClipped = 0;
  int bluRows = 0, redRows = 0, medicRows = 0, chargeRows = 0;
  int deadRows = 0, lowRows = 0, overRows = 0, unknownRows = 0;
  int observedDrawn = 0;
  int classChips = 0, classChipFallbacks = 0;
  std::size_t rectPrimitives = 0, textPrimitives = 0;
  std::vector<BroadcastHudRow> rows;    // placed and clipped, in drawn order
};

// Composes the overlay for one frame. Pure: the client size is an input, not a
// window query, so a gate (and the capture run) can ask for any frame size.
BroadcastHudDraw broadcastHudCompose(const BroadcastHudInput& input);

// One line per publish, then one line per row. Same grammar as the minimap,
// overview and timeline traces ("category: key=value key=value"), so a gate greps
// it with the same helpers.
std::vector<std::string> formatBroadcastHudLines(const BroadcastHudDraw& draw);

}  // namespace tf2::native
