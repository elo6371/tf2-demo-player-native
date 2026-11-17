#pragma once

// ============================================================================
// T105-n17: THE BLEND WEIGHTS -- a pure function from (speed, angles) to the
// weights the player body's pose is mixed with.
//
// WHY A SEPARATE HEADER
//   The product (native/src/main.cpp) and the probe
//   (native/tools/model_pose_probe.cpp) have to reach the SAME weights, and a
//   gate has to be able to read them. Keeping the arithmetic here -- no
//   binding, no device, no entity -- is the same reason animation_clock.h is
//   separate: a weight that only exists inside a draw loop cannot be asserted.
//
// THE MODEL (first version; an approximation, not TF2 parity)
//   * Locomotion is stand / walk / run, selected by ground speed with a
//     two-stage linear ramp over (idleSpeed, walkSpeed, runSpeed). On every
//     segment exactly two weights are nonzero and they are t and 1-t, so the
//     sum is 1 by construction rather than by a later division.
//   * The aim layer's weight grows with the angle between the body's heading
//     and the view: looking along the body leaves the locomotion pose alone,
//     aiming away from it hands the picture to the aim pose.
//   * Everything is a function of numbers already in the trace (speed,
//     m_angEyeAngles, the entity's yaw), so two runs of the same capture
//     produce the same weights at the same tick.
// ============================================================================

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace tf2::native {

// Speed thresholds in units/second. idleSpeed matches the clock's
// movingSpeedThreshold so "the clock says idle" and "the weights say stand"
// cannot disagree; walkSpeed/runSpeed are the TF2 shapes (walk is roughly
// 100-190, run roughly 300-400) at the demo's own scale.
struct LocomotionBlendConfig {
  float idleSpeed = 0.5f;
  float walkSpeed = 150.0f;
  float runSpeed = 300.0f;
};

struct LocomotionWeights {
  float idle = 1.0f;
  float walk = 0.0f;
  float run = 0.0f;
};

inline float clampWeight01(float value) {
  if (!(value > 0.0f)) return 0.0f;
  if (value > 1.0f) return 1.0f;
  return value;
}

// Degenerate thresholds (run <= walk, walk <= idle) are repaired monotonically,
// the same way the clock repairs its clamp, so a caller that fed garbage gets a
// usable ramp rather than a division by zero.
inline LocomotionBlendConfig sanitizeLocomotionBlendConfig(const LocomotionBlendConfig& config) {
  LocomotionBlendConfig out;
  const LocomotionBlendConfig defaults;
  out.idleSpeed = std::isfinite(config.idleSpeed) ? config.idleSpeed : defaults.idleSpeed;
  out.walkSpeed = std::isfinite(config.walkSpeed) ? config.walkSpeed : defaults.walkSpeed;
  out.runSpeed = std::isfinite(config.runSpeed) ? config.runSpeed : defaults.runSpeed;
  if (out.idleSpeed < 0.0f) out.idleSpeed = 0.0f;
  if (!(out.walkSpeed > out.idleSpeed)) out.walkSpeed = out.idleSpeed + 1.0f;
  if (!(out.runSpeed > out.walkSpeed)) out.runSpeed = out.walkSpeed + 1.0f;
  return out;
}

// speed -> (idle, walk, run). Sum is 1 by construction.
inline LocomotionWeights locomotionBlendWeights(
    float speed, const LocomotionBlendConfig& config = LocomotionBlendConfig{}) {
  const LocomotionBlendConfig cfg = sanitizeLocomotionBlendConfig(config);
  LocomotionWeights weights;
  if (!std::isfinite(speed) || speed <= cfg.idleSpeed) {
    weights.idle = 1.0f;
    return weights;
  }
  if (speed < cfg.walkSpeed) {
    const float t = clampWeight01((speed - cfg.idleSpeed) / (cfg.walkSpeed - cfg.idleSpeed));
    weights.idle = 1.0f - t;
    weights.walk = t;
    weights.run = 0.0f;
    return weights;
  }
  if (speed < cfg.runSpeed) {
    const float t = clampWeight01((speed - cfg.walkSpeed) / (cfg.runSpeed - cfg.walkSpeed));
    weights.idle = 0.0f;
    weights.walk = 1.0f - t;
    weights.run = t;
    return weights;
  }
  weights.idle = 0.0f;
  weights.walk = 0.0f;
  weights.run = 1.0f;
  return weights;
}

// Signed difference of two angles in degrees, wrapped into [-180, 180). The
// wrap is done in double so a yaw that arrived as 7203.5 cannot lose its
// fractional part before the comparison.
inline float wrappedDegreeDifference(float a, float b) {
  if (!std::isfinite(a) || !std::isfinite(b)) return 0.0f;
  double delta = static_cast<double>(a) - static_cast<double>(b);
  delta = std::fmod(delta, 360.0);
  if (delta < -180.0) delta += 360.0;
  if (delta >= 180.0) delta -= 360.0;
  return static_cast<float>(delta);
}

// Aim layer weight: |view yaw - body yaw| / fullYawDeg, clamped to [0,1].
// 0 = looking along the body (locomotion alone), 1 = aiming fullYawDeg or more
// off the body (the aim pose takes over). The full angle is a parameter, not a
// literal, so the product and any future reading agree on the knob.
inline float aimLayerWeightDegrees(
    float viewYawDeg, float bodyYawDeg, float fullYawDeg = 90.0f) {
  const float full = std::isfinite(fullYawDeg) && fullYawDeg > 1.0e-3f ? fullYawDeg : 90.0f;
  const float difference = std::fabs(wrappedDegreeDifference(viewYawDeg, bodyYawDeg));
  return clampWeight01(difference / full);
}

// Renormalises a weight vector to sum 1, dropping non-finite/negative entries.
// Returns false (weights untouched) when nothing positive is left, which the
// caller reports rather than blending with a zero total.
inline bool normalizeBlendWeights(float* weights, std::size_t count, float* outSum = nullptr) {
  float sum = 0.0f;
  for (std::size_t i = 0; i < count; ++i) {
    if (!std::isfinite(weights[i]) || weights[i] < 0.0f) weights[i] = 0.0f;
    sum += weights[i];
  }
  if (outSum != nullptr) *outSum = sum;
  if (!(sum > 0.0f) || !std::isfinite(sum)) return false;
  for (std::size_t i = 0; i < count; ++i) weights[i] /= sum;
  return true;
}

} // namespace tf2::native
