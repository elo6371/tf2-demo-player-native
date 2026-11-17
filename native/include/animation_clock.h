#pragma once

// ============================================================================
// T61: THE ANIMATION CLOCK -- a pure function from (tick, speed) to cycle.
// T105-n17: the IDLE contract changed -- see "IDLE ADVANCES" below.
//
// WHY THIS IS SEPARATE FROM EVERYTHING ELSE
//   The product needs a per-player animation cursor that is a function of the
//   demo's own ticks and the player's own speed, not of wall-clock time. A
//   clock driven by wall time makes two runs of the same capture produce
//   different poses, so a pose digest is not a reading a gate can hold; a
//   clock driven by the demo tick makes the pose reproducible from the trace.
//   Keeping the arithmetic here -- no model, no device, no entity -- is what
//   lets a probe test the one thing that is easy to get wrong (dt, the rate
//   clamp, and the wrap) without building a world first.
//
// THE CONTRACT, IN ONE PLACE
//   * dt = (tick - lastTick) / tickRate, in seconds.
//   * Moving rate = clamp(speed / strideUnits, minMovingRate, maxMovingRate),
//     in cycles per second. strideUnits is the distance one cycle covers, so
//     "one stride per 250 units" is the model's own scale rather than a
//     per-class special case.
//   * IDLE ADVANCES (T105, 2026-11-18). The old rule froze the cycle while
//     idle (idleRate = 0 by design). That contradicted TF2, where a standing
//     body keeps playing its stand cycle, and it is why every idle reading in
//     the T61 evidence sat at cycle=0/frame=0 forever. The clock still does
//     not invent a rate: the CALLER supplies idleRate from the idle sequence's
//     own duration (animationCycleRate(frameCount, fps) below -- TF2's
//     convention is one section = one cycle, so one cycle takes
//     frameCount / fps seconds). With idleRate = 0 (the default, i.e. no
//     sequence timing was resolved) the cycle is still frozen, which is the
//     honest reading for a body whose idle clip is unknown.
//   * deltaCycle = rate * dt, and the stored cycle is wrapped to [0, 1).
//   * Non-finite input (speed, tickRate) is REJECTED: the state is not
//     touched and the step reports advanced=false. A NaN that reached the
//     sampler would make every bone matrix NaN and every gate reading "red
//     for an unknown reason"; refusing it at the clock is the cheap end.
//
// WHAT "advanced" MEANS. It is "the cycle moved on this tick", i.e.
// deltaCycle > 0. It is false for the first tick (there is no dt yet), for a
// repeated or backwards tick, for a body whose idle timing is unknown (rate 0)
// and for rejected input. A caller that needs "was the tick accepted at all"
// reads samples (which counts every accepted tick, first one included).
//
// DETERMINISM. The cycle is a function of (tick sequence, speed, idleRate) and
// nothing else: the same tick sampled twice leaves it bit-identical (dt <= 0
// advances nothing), and the same run twice produces the same cycle at the
// same tick. Deriving idleRate from MODEL DATA (frameCount/fps) keeps that
// property: it is a constant for a given model, not a wall-clock reading.
// ============================================================================

#include <cstdint>

namespace tf2::native {

// Which drive the clock is in. Idle is not "slow": it is the stand cycle at
// its own rate (or frozen when the caller has no timing for it).
enum class AnimationDriveState : std::uint8_t { Idle = 0, Moving = 1 };

const char* animationDriveStateName(AnimationDriveState state);

struct AnimationClockConfig {
  // Units per second below which a player counts as standing still. TF2's
  // walk speed is ~100 and run ~300 units/s, so 0.5 separates "no origin
  // delta at all" from "moving" without classifying a teleport jitter as a
  // walk.
  float movingSpeedThreshold = 0.5f;
  // Units of world travel per animation cycle. 250 is the scale of a TF2
  // player's stride: at run speed (~300 u/s) that is ~1.2 cycles/s.
  float strideUnits = 250.0f;
  // The rate clamp. The floor keeps a very slow walk visibly moving rather
  // than freezing mid-stride; the ceiling keeps a demo teleport from
  // spinning through thousands of cycles in one tick.
  float minMovingRate = 0.05f;
  float maxMovingRate = 3.0f;
  // Cycles per second while idle. The CALLER sets this from the idle
  // sequence's own duration via animationCycleRate(); the default 0 means
  // "no idle timing was resolved", which freezes the cycle rather than
  // inventing a speed.
  float idleRate = 0.0f;
};

struct AnimationClockState {
  bool hasPrevious = false;
  std::int32_t lastTick = 0;
  float cycle = 0.0f;   // always in [0, 1)
  AnimationDriveState state = AnimationDriveState::Idle;
  std::uint64_t samples = 0;  // accepted ticks, the first (baseline) one included
};

struct AnimationClockStep {
  bool advanced = false;        // the cycle moved: deltaCycle > 0
  AnimationDriveState state = AnimationDriveState::Idle;
  float rate = 0.0f;            // cycles/s applied to this tick
  float cycle = 0.0f;           // wrapped cycle after the step
  float deltaCycle = 0.0f;      // rate * dt, before wrapping
  float speed = 0.0f;           // speed the tick was classified with
};

// Wraps any finite cycle into [0, 1). Non-finite input maps to 0 so a caller
// that already corrupted the state cannot propagate NaN into a pose.
float wrapCycle(float cycle);

// One cycle of a sequence that plays frameCount frames at fps takes
// frameCount / fps seconds, so it advances at fps / frameCount cycles per
// second (TF2's one-section-one-cycle convention). Returns 0 -- i.e. "frozen"
// -- when frameCount or fps cannot describe a positive duration, so a body
// whose clip has no usable timing keeps the old, honest behaviour instead of
// being given a made-up rate.
//
// This exists so NOBODY hardcodes a frame count: the product asks the idle
// sequence's own AnimationDesc, and the probe asks the same descriptor, which
// is what keeps the two readings comparable.
float animationCycleRate(std::int32_t frameCount, float fps);

// The drive state a speed selects. Exposed because the product's trace and
// the probe must classify a speed with the same threshold, not two copies of
// it.
AnimationDriveState animationDriveStateForSpeed(
  float speed, const AnimationClockConfig& config = AnimationClockConfig{});

// Moving rate = clamp(speed / strideUnits, min, max); idle rate below the
// threshold. Non-finite speed returns idleRate (the caller should have
// rejected it; this is the defensive answer).
float animationRateForSpeed(
  float speed, const AnimationClockConfig& config = AnimationClockConfig{});

// Advances the clock to the given tick. The first call establishes the
// baseline and reports advanced=false. A repeated tick advances nothing; a
// backwards tick resynchronises lastTick (a demo restart must not freeze the
// clock forever). Non-finite tickRate/speed is rejected without touching state.
AnimationClockStep advanceAnimationClock(
  AnimationClockState& state,
  std::int32_t tick,
  float tickRate,
  float speed,
  const AnimationClockConfig& config = AnimationClockConfig{});

} // namespace tf2::native
