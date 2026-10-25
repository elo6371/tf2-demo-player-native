#include "animation_clock.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace tf2::native {
namespace {

// A config that arrived from a config file, a demo, or a caller that never
// validated it must not be able to poison the clock. Non-finite fields fall
// back to their defaults; the clamp order and the stride are repaired so that
// clamp(x, min, max) is never undefined and a division is never by zero.
AnimationClockConfig sanitize(const AnimationClockConfig& config) {
  AnimationClockConfig out;
  const AnimationClockConfig defaults;
  out.movingSpeedThreshold = std::isfinite(config.movingSpeedThreshold)
    ? config.movingSpeedThreshold : defaults.movingSpeedThreshold;
  out.strideUnits = std::isfinite(config.strideUnits) && config.strideUnits > 0.0f
    ? config.strideUnits : defaults.strideUnits;
  out.minMovingRate = std::isfinite(config.minMovingRate)
    ? config.minMovingRate : defaults.minMovingRate;
  out.maxMovingRate = std::isfinite(config.maxMovingRate)
    ? config.maxMovingRate : defaults.maxMovingRate;
  if (out.minMovingRate > out.maxMovingRate) {
    // Keep the intent (a clamp) rather than the broken order: the floor wins
    // because freezing is the failure this clock exists to avoid.
    out.maxMovingRate = out.minMovingRate;
  }
  if (out.minMovingRate < 0.0f) out.minMovingRate = 0.0f;
  out.idleRate = std::isfinite(config.idleRate) ? config.idleRate : defaults.idleRate;
  return out;
}

} // namespace

const char* animationDriveStateName(AnimationDriveState state) {
  switch (state) {
    case AnimationDriveState::Idle: return "idle";
    case AnimationDriveState::Moving: return "moving";
  }
  return "unknown";
}

float wrapCycle(float cycle) {
  if (!std::isfinite(cycle)) return 0.0f;
  // fmod in double: a float cycle accumulated over hours can exceed 2^24
  // where a float fmod would lose the fractional part entirely. The double
  // keeps the wrap exact for any tick count a demo can produce.
  double wrapped = std::fmod(static_cast<double>(cycle), 1.0);
  if (wrapped < 0.0) wrapped += 1.0;
  float result = static_cast<float>(wrapped);
  // A value just below 1 can round UP to 1.0f when narrowed back to float.
  // [0, 1) is the contract, so the wrap closes that hole here.
  if (!(result >= 0.0f) || result >= 1.0f) result = 0.0f;
  return result;
}

AnimationDriveState animationDriveStateForSpeed(
    float speed, const AnimationClockConfig& config) {
  const AnimationClockConfig cfg = sanitize(config);
  if (!std::isfinite(speed)) return AnimationDriveState::Idle;
  return speed >= cfg.movingSpeedThreshold ? AnimationDriveState::Moving
                                           : AnimationDriveState::Idle;
}

float animationRateForSpeed(float speed, const AnimationClockConfig& config) {
  const AnimationClockConfig cfg = sanitize(config);
  if (!std::isfinite(speed)) return cfg.idleRate;
  if (speed < cfg.movingSpeedThreshold) return cfg.idleRate;
  const float raw = speed / cfg.strideUnits;
  return std::clamp(raw, cfg.minMovingRate, cfg.maxMovingRate);
}

AnimationClockStep advanceAnimationClock(
    AnimationClockState& state,
    std::int32_t tick,
    float tickRate,
    float speed,
    const AnimationClockConfig& config) {
  const AnimationClockConfig cfg = sanitize(config);

  AnimationClockStep step;
  step.state = state.state;
  step.cycle = state.cycle;
  step.speed = speed;

  // Rejected input: the state is not touched, so a caller that feeds a broken
  // tick does not corrupt a clock that was healthy before it.
  if (!std::isfinite(tickRate) || tickRate <= 0.0f || !std::isfinite(speed)) {
    step.rate = cfg.idleRate;
    return step;
  }

  step.state = animationDriveStateForSpeed(speed, cfg);
  step.rate = animationRateForSpeed(speed, cfg);

  if (!state.hasPrevious) {
    // Baseline. There is no dt for the first tick, so nothing advances and
    // the cycle is left exactly where the caller set it.
    state.hasPrevious = true;
    state.lastTick = tick;
    state.state = step.state;
    state.cycle = wrapCycle(state.cycle);
    ++state.samples;
    step.cycle = state.cycle;
    return step;
  }

  const double dt = (static_cast<double>(tick) - static_cast<double>(state.lastTick))
    / static_cast<double>(tickRate);
  if (!(dt > 0.0)) {
    // Same tick twice, or a demo restart going backwards. A repeated tick
    // advances nothing; a backwards one resynchronises the baseline so the
    // next forward tick advances again instead of the clock freezing forever.
    if (tick < state.lastTick) state.lastTick = tick;
    state.state = step.state;
    ++state.samples;
    step.cycle = state.cycle;
    return step;
  }

  const double delta = static_cast<double>(step.rate) * dt;
  state.lastTick = tick;
  state.state = step.state;
  state.cycle = wrapCycle(static_cast<float>(
    static_cast<double>(state.cycle) + delta));
  ++state.samples;

  step.deltaCycle = static_cast<float>(delta);
  step.cycle = state.cycle;
  step.advanced = delta > 0.0;
  return step;
}

} // namespace tf2::native
