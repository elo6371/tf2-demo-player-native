#pragma once

// ============================================================================
// T53: THE TRAJECTORY OVERLAY BUILDER
//
// Turns retained per-tick projectile rows into the points the renderer draws.
// This is where the task's actual requirements live, and each one is a decision
// with a reason rather than a filter chain:
//
//   * ALL PLAYERS vs ONE PLAYER. The filter compares the handle the projectile
//     names as its thrower against the selected player's entity index. A
//     projectile with NO owner is EXCLUDED when a player is selected and
//     INCLUDED when "all players" is selected -- an unowned projectile genuinely
//     belongs to nobody, so claiming it belongs to the selected player would be a
//     fabrication, while hiding it from the all-players view would lose a real
//     projectile. Both halves are counted.
//
//   * LINGER. A trail outlives the projectile that drew it by the configured
//     number of MILLISECONDS converted at the demo's own tick rate. The age is
//     carried on each point rather than applied in the renderer so that a
//     captured frame is reproducible from (tick, retained rows) alone.
//
//   * QUALITY. Low and High must differ in something MEASURED, or the dial is
//     decoration. Low samples every fourth tick; High samples every tick. The
//     gate asserts the two produce different point counts on the same tick.
//
// NOTHING HERE INTERPOLATES. A gap between two observed rows stays a gap: the
// builder emits the observed points and no others, and the renderer draws a
// segment only between points it was given. That is the mechanical guarantee
// behind "must not assume constant velocity".
// ============================================================================

#include "demo_header.h"
#include "projectile_track_point.h"

#include <cstddef>
#include <cstdint>
#include <vector>

// WHY THIS HEADER DOES NOT INCLUDE native_renderer.h
//
// The builder takes the four control values it needs as plain scalars rather
// than a RenderSettings reference. RenderSettings lives in native_renderer.h,
// which pulls in <d3d11.h>; a probe that included it would need a GPU-less build
// of the renderer header and would stop being the cheap instrument that proves
// the filter and linger rules. The product passes its own settings fields
// straight through, so there is still exactly one implementation of each rule.
//
// ProjectileTrackPoint lives in projectile_track_point.h, which includes nothing
// at all, so the probe and the renderer reach the same struct without the probe
// touching <d3d11.h> and without the renderer touching the decoder.

namespace tf2::native {

// The tick rate assumed when a caller cannot supply the demo's own. The demo
// header carries the real one and the builder reads it there when it can.
constexpr double kTrajectoryDefaultTickRate = 66.66;

struct TrajectoryBuildStats {
  std::size_t rowsAvailable = 0;    // retained rows in the linger window
  std::size_t rowsWithOwner = 0;
  std::size_t rowsWithoutOwner = 0; // excluded when a player is selected
  std::size_t rowsFilteredOut = 0;  // owned by somebody else
  std::size_t rowsAfterFilter = 0;
  std::size_t rowsAfterQuality = 0;
  std::size_t pointsEmitted = 0;
  std::size_t lifetimes = 0;
  std::int32_t lingerTicks = 0;
  // The distinct owner slots that survived the filter, so "all players" is a
  // count rather than a word.
  std::size_t ownersRepresented = 0;
};

// Builds the points for one scene tick.
//
// lowQuality selects the stride: High = every retained tick, Low = every
// fourth. The stride is applied to the DEMO TICK, not to the emission order, so
// the same tick yields the same points in every run of the same capture.
struct TrajectoryBuildRequest {
  // -1 = every projectile, which is the "all players" case.
  std::int32_t selectedPlayerEntity = -1;
  int lingerMs = 500;
  bool lowQuality = false;
  // The demo's own tick rate, when the caller has it. Zero falls back to
  // kTrajectoryDefaultTickRate.
  double tickRate = 0.0;
};

std::vector<ProjectileTrackPoint> buildProjectileTrajectoryPoints(
  const DemoNetworkSummary& summary,
  std::int32_t sceneTick,
  const TrajectoryBuildRequest& request,
  TrajectoryBuildStats* stats = nullptr);

// Milliseconds -> ticks at a given rate, rounded to whole ticks, minimum 0.
std::int32_t trajectoryLingerTicks(int lingerMs, double tickRate);

} // namespace tf2::native
