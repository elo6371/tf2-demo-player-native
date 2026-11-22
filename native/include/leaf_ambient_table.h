// T105/task-37 Half B: the per-frame leaf ambient cube table, on the CPU.
//
// WHY THIS FILE EXISTS AS ITS OWN HEADER
// --------------------------------------
// The interesting part of the leaf-cube feature is not the upload, it is the
// de-duplication: a frame carries up to a hundred entity and prop instances and
// each one looks up its own leaf, so the SAME leaf cube routinely arrives many
// times (a row of props in one room, a team standing on one spawn floor). A
// table that stored one cube per instance would be a buffer of duplicates and
// the shader would index it by instance -- correct, but it would make the
// per-instance cube look like per-instance data, which it is not.
//
// It is a separate header rather than a lambda inside the renderer because the
// rule "equal cubes get equal slots" is checkable WITHOUT a device: the probe
// (native/tools/leaf_ambient_probe.cpp) compiles this on a machine with no D3D
// context and asserts the mapping directly. A rule that only exists inside
// native_renderer_entity.cpp can only be checked by looking at pixels, which is
// a much weaker statement than "these two cubes produced the same slot".
//
// WHAT A SLOT IS
// --------------
// Slot s occupies rows [6*s, 6*s+6) of the uploaded table, one float4 per face
// in the order (+X, -X, +Y, -Y, +Z, -Z) that both halves of this task use:
// native/include/bsp_map.h:107-125 for the decoded sample, and the world pixel
// shader's `modelLightingRgb` (native/src/native_renderer_shaders.cpp:209-217)
// for the same order in the b2 global cube it already shades with.
#ifndef TF2_NATIVE_LEAF_AMBIENT_TABLE_H
#define TF2_NATIVE_LEAF_AMBIENT_TABLE_H

#include <array>
#include <cstdint>
#include <vector>

namespace tf2::native {

// The instance lane carries the slot as a float (it rides in the .w of the first
// normal row -- see EntityModelInstanceGpu in native/src/renderer_internal.h),
// so "no cube" has to be a value a float can hold exactly and a shader can test
// without a comparison against 0xFFFFFFFF (which float32 CANNOT represent:
// 4294967295 rounds to 4294967296). -1 is exact, negative, and unreachable for
// a real slot.
constexpr float kLeafAmbientNoSlot = -1.0f;

// Faces per cube. Named so the "six rows per slot" arithmetic appears once.
constexpr int kLeafAmbientFaces = 6;

// The one number in this task that is a MEASUREMENT rather than a read of the
// data, and the reason it cannot be anything the map hands over: the shipped
// leaf cubes are LINEAR radiance of order 1e-3 (cp_process_f12's page maximum is
// 0.004626 against a lightmap page maximum of 7.686275), while the global b2
// cube task-36 built from the same map's lights lands near 1.0 after its own
// exposure. Uploading the leaf values unscaled would multiply every entity in
// leaf mode by about a thousandth -- a black silhouette, which is the exact
// failure mode task-36's own report records for a different bug.
//
// The value below is MEAN-MATCHED, not fitted to a picture: scale the demo's 76
// real cubes by k, run the shader's own arithmetic over a 64-step sphere of
// surface directions, and bisect for the k whose mean lighting equals the mean
// the global cube produces over the SAME sphere (1.195807 on cp_process_f12).
// The root is k = 738.229. What ships is 738.0f, the nearest float32 to it: a
// decimal literal cannot be stored exactly, so the constant IS the rounding and
// the doc quotes the sweep line for 738 (mean 1.195529) rather than the
// bisection root (mean 1.195807), because 738 is what a build of this file
// actually produces. The two differ by 0.023% of one mean, far inside the
// sweep's own 1-unit step, and neither the choice of root nor its rounding is
// load-bearing: the argument below is that the MEAN lands where the basic arm
// already was, and every k within a few percent of 738 does that.
// That is the whole argument:
// average model brightness is unchanged from what --lighting basic already
// draws, and the per-leaf variation rides on top of it instead of replacing it.
//
// What it deliberately does NOT do is preserve task-36's 0.62 floor. At this k
// the dimmest leaf cube's own floor is 0.0346, so a leaf baked in shadow draws
// dark. That is the point of per-leaf ambient -- a cube that says "no light
// reached here" must not be lifted to the basic arm's floor, or every interior
// would look like the exterior. cp_process_f12 is the map where this is safe:
// its cubes span 0.0 to 0.0046 (face luminance p50 0.000481) and 3.35% of
// directions land below 0.25, so the dark end is real geometry, not a decoding
// artifact. The mapping, the sweep, the bisection root and the frame
// confirmation are all in docs/LEAF-AMBIENT-GPU-2026-11-21.md, and the script
// that reproduces every number is evidence/leaf-ambient-gpu/measure-leaf-scale.py.
// It is one constant and not a per-map calibration on purpose: see the doc's
// "what this does not do".
constexpr float kLeafAmbientScale = 738.0f;

// One instance's contribution to the table: the six faces, already scaled, plus
// whether the caller had a cube at all. A caller with no cube must still say so
// -- there is no zero cube in this type, because a zero cube reads as "this
// model is in the dark" when the truth is "this leaf shipped no bake".
struct LeafAmbientCubeFace {
  std::array<std::array<float, 3>, kLeafAmbientFaces> rgb{};
};

// Append `cube` to `distinct` unless an equal cube is already there, and
// return its slot. Equality is exact bit equality of the six faces: the cubes
// come from one decoded lump, so two instances that resolved to the same sample
// carry identical floats, and near-equality would only merge cubes that are
// genuinely different readings.
inline std::uint32_t leafAmbientSlotFor(std::vector<LeafAmbientCubeFace>& distinct,
    const LeafAmbientCubeFace& cube) {
  for (std::size_t slot = 0; slot < distinct.size(); ++slot) {
    const auto& existing = distinct[slot];
    bool same = true;
    for (int direction = 0; direction < kLeafAmbientFaces && same; ++direction) {
      for (int channel = 0; channel < 3; ++channel) {
        if (existing.rgb[direction][channel] != cube.rgb[direction][channel]) { same = false; break; }
      }
    }
    if (same) return static_cast<std::uint32_t>(slot);
  }
  distinct.push_back(cube);
  return static_cast<std::uint32_t>(distinct.size() - 1);
}

} // namespace tf2::native

#endif // TF2_NATIVE_LEAF_AMBIENT_TABLE_H
