#pragma once

#include <array>

namespace tf2::native {

// T108: the pose arithmetic a layered pose needs, in one place so it can be
// unit-tested without an MDL file (the probe's --pose-math-selftest).
//
// The names and argument order follow the Source engine's mathlib, because these
// are the operations the blending code is reproducing; the bodies are derived
// from the quaternion identities here rather than transcribed from the engine.
//
// A quaternion is xyzw throughout, and a +90 degree rotation about Z is
// (0, 0, sin(pi/4), cos(pi/4)), the same contract animation_decoder.h states.

// out = p scaled to rotate by t of p's own angle. Unlike slerp(identity, p, t)
// this keeps p's hemisphere: a p whose w is negative stays negative, so scaling a
// rotation past 180 degrees does not silently mirror it onto the short arc. That
// distinction is why the delta blend uses this and not a slerp from identity.
void quaternionScale(const std::array<float, 4>& p, float t, std::array<float, 4>& out);

// out = a * b, with b flipped into a's hemisphere first when the two point away
// from each other (dot < 0), so the product is the short-arc composition. Source
// QuaternionMult aligns before multiplying for the same reason.
void quaternionMult(const std::array<float, 4>& a, const std::array<float, 4>& b,
    std::array<float, 4>& out);

// out = s * p + q and out = p + s * q: the two accumulate steps of a lerped
// quaternion blend (Source QuaternionSM / QuaternionMA). They are component-wise
// and do not renormalize; the caller normalizes once the accumulation is done.
void quaternionSM(float s, const std::array<float, 4>& p, const std::array<float, 4>& q,
    std::array<float, 4>& out);
void quaternionMA(const std::array<float, 4>& p, float s, const std::array<float, 4>& q,
    std::array<float, 4>& out);

// Unit-length copy. A quaternion too short to normalize becomes identity rather
// than NaN, so a degenerate channel cannot poison the whole pose.
void quaternionNormalize(const std::array<float, 4>& in, std::array<float, 4>& out);

// Moved here from animation_decoder.cpp's file-local helpers so the Euler
// convention can be tested on its own: radians in, Source RadianEuler order
// (x roll, y pitch, z yaw). The decoder still calls it unqualified.
void radianEulerToQuat(float roll, float pitch, float yaw, std::array<float, 4>& out);

} // namespace tf2::native
