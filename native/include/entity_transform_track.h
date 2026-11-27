#pragma once

// ============================================================================
// CA-02a: THE ENTITY TRANSFORM TRACK -- analysis and sizing over a retention
// that records a drawable entity's placement only when it CHANGES.
//
// WHY A SEPARATE FILE. The retention itself (EntityTransformSample, the limits,
// the append at the packet decode, the PVS residency hooks) lives in
// demo_header.{h,cpp} beside the decoder, exactly where T53's projectile rows and
// N12-C's camera rows live, so it adds no new link dependency to any existing
// target. THIS file is the layer that turns the retained rows into per-lifetime
// series and reports what they cost. Only the probe and the gates ask those
// questions, and only they need this header.
//
// WHAT THIS LAYER IS FOR, AND WHAT IT IS NOT.
//   CA-02a is RETENTION AND SIZING ONLY. Nothing here interpolates; CA-02b adds
//   the interpolation view (one place, reading these rows) and CA-02c wires it
//   behind --entity-interp. The two numbers this round is judged on live here:
//   retained bytes per recording (budget <= 64 MB) and the row/lifetime counts
//   that are compared against the Rust side's prop-track counts.
//
// SIZING MODEL. Every retained row is 48 B (asserted on EntityTransformSample).
// The row vector is grown in 64k-row steps rather than by geometric doubling, and the
// LAST step is clipped to the remaining budget (min(64k, maxSamples - size)), so
// capacity never exceeds maxSamples even though it can exceed the final size by up to
// 65536 rows. The byte report is therefore bounded by construction instead of by luck.
// entityTransformRetainedBytes counts CAPACITY, not size, because capacity is what the
// process actually holds.
// ============================================================================

#include "demo_header.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace tf2::native {

// ONE LIFETIME of one entity slot, in retained-row order.
//
// The identity is (entityIndex, generation), never the index alone: Source
// recycles slots, and the same mistake T53 documents on bagel (slot 377 carrying
// 25 separate lifetimes) applies here. A series is built per pair.
struct EntityTransformSeries {
  std::uint16_t entityIndex = 0;
  std::uint32_t generation = 0;
  std::int32_t classId = -1;
  std::int32_t firstTick = 0;
  std::int32_t lastTick = 0;
  std::size_t rows = 0;
  // Rows whose origin differs from the previous row of this lifetime. A held
  // entity (a dormant prop, a parented cosmetic) reads 0; a player reads rows-1
  // while it moves. This is the count the change-only rule is meant to produce.
  std::size_t movedRows = 0;
  std::size_t dormantRows = 0;
  // Rows that carried a move parent, i.e. the lifetime is attached rather than
  // placed in the world. Reported because an attached entity's own origin is not
  // what a renderer uses, and the interpolation view must not lerp the parent's
  // motion into it (CA-02b).
  std::size_t parentRows = 0;
  bool hasOrigin = false;
  bool hasAngles = false;
  bool playerClass = false;
  bool drawableByModel = false;
};

// Split the flat retained row list into one series per (entityIndex, generation),
// ordered by first appearance. Rows are appended in non-decreasing tick order, so
// each series' rows keep that order.
std::vector<EntityTransformSeries> buildEntityTransformSeries(
    const std::vector<EntityTransformSample>& samples);

// What one scan retained and what it cost. All counts are per recording.
struct EntityTransformTrackStats {
  std::size_t rows = 0;
  std::size_t lifetimes = 0;
  std::size_t entities = 0;
  std::size_t movingLifetimes = 0;   // lifetimes with movedRows > 0
  std::size_t playerLifetimes = 0;
  std::size_t modelLifetimes = 0;    // drawable by m_nModelIndex rather than by class
  std::size_t dormantRows = 0;
  std::size_t droppedRows = 0;       // 1 when the cap stopped the capture; not a row count
  std::size_t packetsScanned = 0;
  std::size_t slotsWalked = 0;
  std::size_t rowsUnchanged = 0;     // comparisons whose answer was "no change"
  std::size_t rowsStrideSkipped = 0; // movement rows skipped by minTickStride
  std::size_t lifetimesNotDrawable = 0;
  std::int32_t firstTick = -1;
  std::int32_t lastTick = -1;
  // Capacity-based, so it is what the process holds. See the sizing model above.
  std::size_t retainedBytes = 0;
  bool captureStopped = false;       // maxSamples was reached; later packets kept nothing
};

EntityTransformTrackStats summarizeEntityTransformTrack(const DemoNetworkSummary& summary);

// Bytes the retention currently holds, counting vector CAPACITY for the row array
// and the side tables' sizes. Used by the memory gate and printed by the probe.
std::size_t entityTransformRetainedBytes(const DemoNetworkSummary& summary);

// The rows of ONE lifetime, in tick order. Empty when that lifetime retained
// nothing; there is deliberately no nearest-neighbour fallback (the fallback is
// what N12-C removed from the camera track and what CA-02b must not reintroduce).
std::vector<EntityTransformSample> queryEntityTransformRows(
    const std::vector<EntityTransformSample>& samples,
    std::uint16_t entityIndex, std::uint32_t generation);

}  // namespace tf2::native
