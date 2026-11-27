#include "pose_accumulate.h"

#include <cmath>

namespace tf2::native {
namespace {

constexpr float kMinNormalLength = 1.0e-8f;

float dot4(const std::array<float, 4>& a, const std::array<float, 4>& b) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
}

} // namespace

void quaternionScale(const std::array<float, 4>& p, float t, std::array<float, 4>& out) {
  // Source's QuaternionScale: the half-angle is atan2(|xyz|, w), so the angle is
  // continuous over the whole sphere and a rotation past 180 degrees stays past
  // it (w is allowed to come back positive for t > 0.5). Reading the half-angle
  // off |w| instead folds every rotation into [0, 180] and re-applying w's sign
  // afterwards cannot undo that: it scales the wrong angle and then relabels it.
  const float xyz = std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
  if (!(xyz > kMinNormalLength)) {
    // Zero or 360 degrees: the axis is undefined, so keep the identity.
    out = std::array<float, 4>{{0.0f, 0.0f, 0.0f, 1.0f}};
    return;
  }
  const float half = std::atan2(xyz, p[3]);
  const float scaledHalf = half * t;
  // xyz > 0 forces the half-angle into (0, pi), so sin(half) cannot vanish.
  const float factor = std::sin(scaledHalf) / std::sin(half);
  out[0] = p[0] * factor;
  out[1] = p[1] * factor;
  out[2] = p[2] * factor;
  out[3] = std::cos(scaledHalf);
}

void quaternionMult(const std::array<float, 4>& a, const std::array<float, 4>& b,
    std::array<float, 4>& out) {
  // q and -q are the same rotation, but they are different points on the
  // sphere; composing across that sign boundary takes the long way round. Align
  // b with a first so the product is always the short arc.
  const float sign = dot4(a, b) < 0.0f ? -1.0f : 1.0f;
  const float bx = b[0] * sign;
  const float by = b[1] * sign;
  const float bz = b[2] * sign;
  const float bw = b[3] * sign;
  out[0] = a[3] * bx + a[0] * bw + a[1] * bz - a[2] * by;
  out[1] = a[3] * by - a[0] * bz + a[1] * bw + a[2] * bx;
  out[2] = a[3] * bz + a[0] * by - a[1] * bx + a[2] * bw;
  out[3] = a[3] * bw - a[0] * bx - a[1] * by - a[2] * bz;
}

void quaternionSM(float s, const std::array<float, 4>& p, const std::array<float, 4>& q,
    std::array<float, 4>& out) {
  for (std::size_t axis = 0; axis < 4; ++axis) out[axis] = s * p[axis] + q[axis];
}

void quaternionMA(const std::array<float, 4>& p, float s, const std::array<float, 4>& q,
    std::array<float, 4>& out) {
  for (std::size_t axis = 0; axis < 4; ++axis) out[axis] = p[axis] + s * q[axis];
}

void quaternionNormalize(const std::array<float, 4>& in, std::array<float, 4>& out) {
  const float length = std::sqrt(dot4(in, in));
  if (!(length > kMinNormalLength)) {
    out = std::array<float, 4>{{0.0f, 0.0f, 0.0f, 1.0f}};
    return;
  }
  for (std::size_t axis = 0; axis < 4; ++axis) out[axis] = in[axis] / length;
}

void radianEulerToQuat(float roll, float pitch, float yaw, std::array<float, 4>& out) {
  const float halfRoll = roll * 0.5f;
  const float halfPitch = pitch * 0.5f;
  const float halfYaw = yaw * 0.5f;
  const float sr = std::sin(halfRoll);
  const float cr = std::cos(halfRoll);
  const float sp = std::sin(halfPitch);
  const float cp = std::cos(halfPitch);
  const float sy = std::sin(halfYaw);
  const float cy = std::cos(halfYaw);
  out[0] = sr * cp * cy - cr * sp * sy;
  out[1] = cr * sp * cy + sr * cp * sy;
  out[2] = cr * cp * sy - sr * sp * cy;
  out[3] = cr * cp * cy + sr * sp * sy;
}

} // namespace tf2::native
