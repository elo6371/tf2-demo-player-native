#pragma once

// ============================================================================
// T53: PROJECTILE TRAJECTORY ANALYSIS -- what a retained per-tick series can say
// about its own fidelity, and the two shortcuts it must be able to reject.
//
// WHY THIS FILE EXISTS SEPARATELY FROM THE RETENTION
//   The retention itself (ProjectileTrackSample, the append at the packet
//   decode, the query) lives in demo_header.{h,cpp} beside the decoder, so it
//   adds no new link dependency to any existing target. THIS file is the
//   analysis layer: it turns retained rows into per-entity series and measures
//   how wrong the two plausible shortcuts would be. Only the probe and the
//   product link it, because only they ask those questions.
//
// THE FINDING THE ANALYSIS IS BUILT TO PROVE
//   The demo DOES carry an observed position series for projectiles: on
//   koth_bagel_rc13 the independent oracle shows CTFProjectile_Rocket and
//   CTFGrenadePipebombProjectile writing m_vecOrigin on EVERY packet for the
//   whole flight (entity 801: 75 contiguous ticks, 75 origins, no gap), and the
//   series contains gravity, a real collision, a bounce and a deflection. So
//   "must not assume constant velocity" is measurable rather than rhetorical.
//
// WHAT IS *NOT* CLAIMED
//   These functions never produce a point that was not observed. There is no
//   interpolation here on purpose: the two error functions exist to show how
//   wrong interpolation is, not to perform it.
// ============================================================================

#include "demo_header.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace tf2::native {

// ONE PROJECTILE'S observed rows -- not one slot's.
//
// The identity is (entityIndex, generation), because Source reuses an entity slot
// as soon as the projectile in it is destroyed. Measured on bagel, slot 377
// carries 25 separate projectile lifetimes; keying a series on the index alone
// merged all of them into a single 12057-sample "flight" spanning 70,596 ticks
// and reported a constant-velocity error of 1.27 million map units on a map about
// 3400 units across. The number was an artifact of the merge, which is precisely
// the kind of plausible-looking nonsense this layer exists to prevent.
struct ProjectileSeries {
  std::uint16_t entityIndex = 0;
  std::uint32_t generation = 0;
  std::int32_t classId = -1;
  std::string className;
  std::int32_t firstTick = 0;
  std::int32_t lastTick = 0;
  std::vector<ProjectileTrackSample> samples;
  // Tick positions in [firstTick, lastTick] with no row. A contiguous flight
  // reads 0; a gap is reported rather than smoothed over.
  std::size_t missingTicks = 0;
  bool contiguous = false;
  // The bounce/deflection signature: the vertical step changes sign at least
  // once. Reported because "this series contains a collision" is exactly the
  // property that makes a constant-velocity model wrong, and it should be a
  // reading, not something a reader has to eyeball off a table.
  bool verticalDirectionReversed = false;
  float zMin = 0.0f;
  float zMax = 0.0f;
};

// Split a flat retained row list into one series per PROJECTILE LIFETIME,
// ordered by first appearance. Rows for a class that is not a projectile are
// ignored. Rows of one slot across different lifetimes are NOT merged.
std::vector<ProjectileSeries> buildProjectileSeries(
  const std::vector<ProjectileTrackSample>& samples,
  const std::vector<ServerClassSchema>& classSchemas);

// ---------------------------------------------------------------------------
// SHORTCUT 1: constant velocity.
//
// Take the first two observed samples, derive a velocity, extrapolate over the
// whole series, and report the distance to what the demo actually recorded.
// This is the shortcut an implementation reaches for when it has positions but
// wants a smooth curve -- and it is the one the task forbids by name.
//
// Measured on bagel entity 801: max 1246.8, mean 576.0 map units over 75 ticks.
// The map is roughly 3400 units across, so the shortcut lands a third of a map
// away from the truth.
// ---------------------------------------------------------------------------
struct ConstantVelocityError {
  bool ok = false;           // false with fewer than 3 samples: nothing to test
  std::size_t samples = 0;
  float maxError = 0.0f;
  float meanError = 0.0f;
  std::int32_t worstTick = 0;
  float worstObserved[3] = {0.0f, 0.0f, 0.0f};
  float worstPredicted[3] = {0.0f, 0.0f, 0.0f};
  // The velocity the shortcut derived, kept so a reader can see where it came
  // from rather than trusting the arithmetic.
  float velocity[3] = {0.0f, 0.0f, 0.0f};
};

ConstantVelocityError constantVelocityError(const ProjectileSeries& series);

// ---------------------------------------------------------------------------
// SHORTCUT 2: a straight line between the first and last observed sample.
//
// The sibling shortcut, and the one a mutation uses: an implementation that
// rejected "constant velocity" by name but drew a two-point line would pass a
// gate that only tested shortcut 1. Same lie, different name, so it gets its
// own reading.
// ---------------------------------------------------------------------------
struct TwoPointLineError {
  bool ok = false;
  std::size_t samples = 0;
  float maxError = 0.0f;
  std::int32_t worstTick = 0;
  float worstObserved[3] = {0.0f, 0.0f, 0.0f};
  float worstPredicted[3] = {0.0f, 0.0f, 0.0f};
};

TwoPointLineError twoPointLineError(const ProjectileSeries& series);

} // namespace tf2::native
