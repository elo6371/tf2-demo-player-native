#pragma once

// ============================================================================
// POSE UPRIGHT -- a pure reading of "is the drawn body standing", taken from the
// bone matrices the product actually skins with and the matrix the entity is
// actually drawn with.
//
// WHY THIS EXISTS
//   PLAYER-ANIMATION-PIXELS=PASS proved the pose reached the screen; it did not
//   prove the pose was right (the 64161 frame passed while the body stood on its
//   head). The gate needs a number that a wrong pose cannot satisfy. This header
//   computes it from (binding, boneModelMatrices, modelToWorld) only -- no
//   device, no entity record, no draw -- so the product trace and any probe
//   reach the same reading.
//
// WORLD SPACE, JUDGED ALONG WORLD +Z
//   The renderer's world is Z-up (X/Y horizontal, Z altitude) and the entity
//   draw matrix carries the T62 permutation model (x,y,z) -> Source (z,x,y)
//   (native/src/entity_model.cpp:837-857). A reading taken in model space alone
//   accepts a body that is upright in its own authoring frame while lying flat
//   on screen, and a permutation applied TWICE cancels without a trace -- which
//   is exactly the 64161 class of bug. So every key point is pushed through the
//   entity matrix first, and the comparisons run along world +Z:
//     ordered     head.z > pelvis.z > lowest foot.z
//     upDot       dot(normalize(head - pelvis), +Z): ~1 upright, ~0 lying,
//                 ~-1 inverted.
//     heightRatio (head.z - lowest foot.z) / world-space bind height.
//   `up` is therefore the RULER and is always (0,0,1). `bindUp` is the bind
//   axis expressed in world space and is DIAGNOSTIC ONLY: a bind pose whose own
//   up is horizontal means the model was placed sideways, and ordered/upDot/
//   height above will say so.
//
// BONES ARE FOUND BY NAME, NEVER BY INDEX
//   The existing `anim: posebones` line reads indices 6/5/13/19/20 and
//   `anim: posecheck pelvis=` reads index 0. Those are not the same bones across
//   the nine classes. Here every key bone is resolved through the animation
//   skeleton's names and binding.animToModel, and a bone that does not resolve
//   is reported by name instead of read from a neighbour.
//
// MODEL -> WORLD MATRIX
//   `modelToWorld` is 12 floats, row-major 3x4, translation in 3/7/11 -- the
//   same layout as boneModelMatrices. It is applied as world = M * model:
//     w[r] = m[r*4+0]*x + m[r*4+1]*y + m[r*4+2]*z + m[r*4+3].
//   It must be the matrix the entity is DRAWN with (T62 permutation included),
//   not a rotation rebuilt from the entity angles: rebuilding it is how the
//   permutation gets lost or doubled. A null pointer is reported as
//   missing=model-to-world rather than silently measuring model space.
//
// THE READING
//   up          the ruler, world +Z.
//   bindUp      the bind axis (bind head minus bind feet midpoint) in world
//               space; diagnostic only.
//   ordered     head > pelvis > lowest foot along world +Z, in the drawn pose.
//   upDot       dot(normalize(head - pelvis), +Z). Upright ~1, lying ~0,
//               inverted ~-1.
//   heightRatio (head - lowest foot along world +Z) / world bind height.
//   bindHeight  world-space bind height (same matrix as the animated pose, so a
//               uniform entity scale cancels out of heightRatio).
//   legDevDeg   largest angle between a thigh/shin segment and its bind
//               direction, both expressed in the pelvis frame -- so a whole-body
//               yaw is not motion, and a body frozen in its bind pose reads ~0.
//               Frame-invariant: it does not need the world matrix.
//   nonFinite   bone matrices carrying NaN/Inf.
// Thresholds are the gate's business (pose-invariant-check.sh), not this file's.
// ============================================================================

#include "animation_binding.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

namespace tf2::native {

struct PoseUprightReading {
  bool found = false;          // head, pelvis, a foot and the bind height resolved
  std::string missing = "-";   // first key bone that did not resolve
  bool ordered = false;
  float upDot = 0.0f;
  float heightRatio = 0.0f;
  bool legsFound = false;
  float legDevDeg = 0.0f;
  std::size_t nonFinite = 0;
  std::array<float, 3> up{};      // the ruler, world +Z
  float bindHeight = 0.0f;        // world-space bind height
  std::array<float, 3> bindUp{};  // diagnostic: bind axis in world space
};

// Result of the header self-test (poseUprightSelfTest below). Kept as data so a
// probe can print it and a gate can assert failures == 0.
struct PoseUprightSelfTest {
  int cases = 0;
  int failures = 0;
  std::string firstFailure;
};

namespace pose_upright_detail {

using V3 = std::array<double, 3>;

inline bool sameName(const std::string& a, const char* b) {
  std::size_t i = 0;
  for (; i < a.size() && b[i] != '\0'; ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
  }
  return i == a.size() && b[i] == '\0';
}

// Render-skeleton index of an animated bone, by name; -1 when absent.
inline int renderBone(const AnimationBinding& binding, const char* name) {
  for (std::size_t a = 0; a < binding.anims.bones.size(); ++a) {
    if (!sameName(binding.anims.bones[a].name, name)) continue;
    if (a >= binding.animToModel.size()) return -1;
    return binding.animToModel[a];
  }
  return -1;
}

inline V3 sub(const V3& a, const V3& b) { return {{a[0] - b[0], a[1] - b[1], a[2] - b[2]}}; }
inline double dot(const V3& a, const V3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
inline double len(const V3& a) { return std::sqrt(dot(a, a)); }
inline V3 scale(const V3& a, double s) { return {{a[0] * s, a[1] * s, a[2] * s}}; }
// `near` would be an empty macro under minwindef.h; the name is spelled out.
inline bool nearEqual(double a, double b, double tol = 2e-3) { return std::fabs(a - b) <= tol; }

// Animated model-space origin of a bone (row-major 3x4, translation in 3/7/11).
inline V3 animatedPos(const std::array<float, 12>& m) {
  return {{static_cast<double>(m[3]), static_cast<double>(m[7]), static_cast<double>(m[11])}};
}
// poseToBone maps model -> bone: v_b = R v_m + t, so the bone origin in model
// space is -R^T t.
inline V3 bindPos(const std::array<float, 12>& p2b) {
  V3 out{};
  for (int c = 0; c < 3; ++c) {
    out[static_cast<std::size_t>(c)] = -(static_cast<double>(p2b[0 * 4 + c]) * p2b[3]
      + static_cast<double>(p2b[1 * 4 + c]) * p2b[7]
      + static_cast<double>(p2b[2 * 4 + c]) * p2b[11]);
  }
  return out;
}
// A model-space vector expressed in the animated bone's frame: R_a^T v.
inline V3 intoAnimatedFrame(const std::array<float, 12>& m, const V3& v) {
  V3 out{};
  for (int c = 0; c < 3; ++c) {
    out[static_cast<std::size_t>(c)] = static_cast<double>(m[0 * 4 + c]) * v[0]
      + static_cast<double>(m[1 * 4 + c]) * v[1] + static_cast<double>(m[2 * 4 + c]) * v[2];
  }
  return out;
}
// The same vector expressed in the BIND bone frame: bone->model is R^T, so its
// inverse is R itself.
inline V3 intoBindFrame(const std::array<float, 12>& p2b, const V3& v) {
  V3 out{};
  for (int r = 0; r < 3; ++r) {
    out[static_cast<std::size_t>(r)] = static_cast<double>(p2b[r * 4 + 0]) * v[0]
      + static_cast<double>(p2b[r * 4 + 1]) * v[1] + static_cast<double>(p2b[r * 4 + 2]) * v[2];
  }
  return out;
}
inline double angleDeg(const V3& a, const V3& b) {
  const double la = len(a), lb = len(b);
  if (!(la > 1e-9) || !(lb > 1e-9)) return 0.0;
  double c = dot(a, b) / (la * lb);
  if (c > 1.0) c = 1.0;
  if (c < -1.0) c = -1.0;
  return std::acos(c) * 57.29577951308232;
}

// world = M * model, M row-major 3x4 with translation in 3/7/11.
inline V3 toWorld(const float* m, const V3& p) {
  V3 out{};
  for (int r = 0; r < 3; ++r) {
    const std::size_t b = static_cast<std::size_t>(r) * 4;
    out[static_cast<std::size_t>(r)] = static_cast<double>(m[b + 0]) * p[0]
      + static_cast<double>(m[b + 1]) * p[1] + static_cast<double>(m[b + 2]) * p[2]
      + static_cast<double>(m[b + 3]);
  }
  return out;
}

// The key points the reading is computed from, in model space. Split out from
// measurePoseUpright so the world-space math can be self-tested without an
// AnimationBinding.
struct PosePoints {
  V3 head{};
  V3 pelvis{};
  V3 footL{}, footR{};
  V3 bindHead{};
  V3 bindFootL{}, bindFootR{};
  bool haveFootL = false;
  bool haveFootR = false;
};

// Height below which a world-space bind height is treated as unusable (a model
// whose bind head and feet coincide in world space carries no height to divide
// by; reporting it beats an infinite ratio).
constexpr double kMinBindHeight = 1e-3;

inline PoseUprightReading measureUprightPoints(const PosePoints& p, const float* modelToWorld) {
  PoseUprightReading r;
  if (modelToWorld == nullptr) { r.missing = "model-to-world"; return r; }
  V3 bindFeet{};
  if (p.haveFootL && p.haveFootR) {
    bindFeet = {{(p.bindFootL[0] + p.bindFootR[0]) * 0.5, (p.bindFootL[1] + p.bindFootR[1]) * 0.5,
                 (p.bindFootL[2] + p.bindFootR[2]) * 0.5}};
  }
  else if (p.haveFootL) { bindFeet = p.bindFootL; }
  else if (p.haveFootR) { bindFeet = p.bindFootR; }
  else { r.missing = "bip_foot_L/R"; return r; }

  const V3 bindSpan = sub(toWorld(modelToWorld, p.bindHead), toWorld(modelToWorld, bindFeet));
  const double bindHeight = len(bindSpan);
  if (bindHeight > 1e-9) {
    // Diagnostic: where the bind axis points on screen.
    const V3 u = scale(bindSpan, 1.0 / bindHeight);
    r.bindUp = {{static_cast<float>(u[0]), static_cast<float>(u[1]), static_cast<float>(u[2])}};
  }
  if (!(bindHeight > kMinBindHeight)) { r.missing = "bind-height"; return r; }

  const V3 up{{0.0, 0.0, 1.0}};
  const V3 headW = toWorld(modelToWorld, p.head);
  const V3 pelvisW = toWorld(modelToWorld, p.pelvis);
  double footH = 0.0;
  if (p.haveFootL && p.haveFootR) footH = (std::min)(toWorld(modelToWorld, p.footL)[2], toWorld(modelToWorld, p.footR)[2]);
  else footH = toWorld(modelToWorld, p.haveFootL ? p.footL : p.footR)[2];
  const V3 torso = sub(headW, pelvisW);
  const double torsoLen = len(torso);

  r.found = true;
  r.ordered = headW[2] > pelvisW[2] && pelvisW[2] > footH;
  r.upDot = torsoLen > 1e-9 ? static_cast<float>(torso[2] / torsoLen) : 0.0f;
  r.heightRatio = static_cast<float>((headW[2] - footH) / bindHeight);
  r.up = {{static_cast<float>(up[0]), static_cast<float>(up[1]), static_cast<float>(up[2])}};
  r.bindHeight = static_cast<float>(bindHeight);
  return r;
}

inline void selftestCheck(PoseUprightSelfTest& t, const char* name, bool ok, const std::string& note) {
  ++t.cases;
  if (ok) return;
  ++t.failures;
  if (t.firstFailure.empty()) t.firstFailure = std::string(name) + ": " + note;
}

} // namespace pose_upright_detail

// The reading for one drawn frame.
//   binding       the instance's animation skeleton (names + poseToBone)
//   boneModel     the model-space bone matrices the draw will skin with
//   modelToWorld  the entity draw matrix, row-major 3x4 (see the header comment)
inline PoseUprightReading measurePoseUpright(const AnimationBinding& binding,
    const std::vector<std::array<float, 12>>& boneModel, const float* modelToWorld) {
  using namespace pose_upright_detail;
  PoseUprightReading r;
  for (const auto& m : boneModel) {
    for (const float v : m) {
      if (!std::isfinite(v)) { ++r.nonFinite; break; }
    }
  }
  const std::size_t n = boneModel.size();
  const auto usable = [&](int i) {
    return i >= 0 && static_cast<std::size_t>(i) < n
      && static_cast<std::size_t>(i) < binding.poseToBone.size()
      && static_cast<std::size_t>(i) < binding.poseToBoneValid.size()
      && binding.poseToBoneValid[static_cast<std::size_t>(i)] != 0;
  };
  const auto need = [&](const char* name) {
    const int i = renderBone(binding, name);
    if (!usable(i) && r.missing == "-") r.missing = name;
    return usable(i) ? i : -1;
  };
  const int head = need("bip_head");
  const int pelvis = need("bip_pelvis");
  const int footL = renderBone(binding, "bip_foot_L");
  const int footR = renderBone(binding, "bip_foot_R");
  const bool haveL = usable(footL), haveR = usable(footR);
  if (!haveL && !haveR && r.missing == "-") r.missing = "bip_foot_L/R";
  const std::size_t nonFinite = r.nonFinite;
  if (head < 0 || pelvis < 0 || (!haveL && !haveR) || nonFinite != 0) return r;

  const auto bp = [&](int i) { return bindPos(binding.poseToBone[static_cast<std::size_t>(i)]); };
  const auto ap = [&](int i) { return animatedPos(boneModel[static_cast<std::size_t>(i)]); };

  PosePoints p;
  p.head = ap(head);
  p.pelvis = ap(pelvis);
  p.bindHead = bp(head);
  if (haveL) { p.footL = ap(footL); p.bindFootL = bp(footL); p.haveFootL = true; }
  if (haveR) { p.footR = ap(footR); p.bindFootR = bp(footR); p.haveFootR = true; }

  r = measureUprightPoints(p, modelToWorld);
  r.nonFinite = nonFinite;
  if (!r.found) return r;

  // legDevDeg is frame-invariant (both sides live in the pelvis frame), so it
  // does not involve modelToWorld.
  const char* const chain[2][3] = {{"bip_hip_L", "bip_knee_L", "bip_foot_L"},
                                   {"bip_hip_R", "bip_knee_R", "bip_foot_R"}};
  const auto& pelvisAnim = boneModel[static_cast<std::size_t>(pelvis)];
  const auto& pelvisBind = binding.poseToBone[static_cast<std::size_t>(pelvis)];
  double worst = 0.0;
  std::size_t segments = 0;
  for (const auto& side : chain) {
    const int idx[3] = {renderBone(binding, side[0]), renderBone(binding, side[1]), renderBone(binding, side[2])};
    for (int s = 0; s < 2; ++s) {
      if (!usable(idx[s]) || !usable(idx[s + 1])) continue;
      const V3 va = intoAnimatedFrame(pelvisAnim, sub(ap(idx[s + 1]), ap(idx[s])));
      const V3 vb = intoBindFrame(pelvisBind, sub(bp(idx[s + 1]), bp(idx[s])));
      const double a = angleDeg(va, vb);
      if (a > worst) worst = a;
      ++segments;
    }
  }
  r.legsFound = segments == 4;
  r.legDevDeg = static_cast<float>(worst);
  return r;
}

// Header self-test of the world-space math (no product data, no files). Run it
// from a probe (print cases/failures) and assert failures == 0.
inline PoseUprightSelfTest poseUprightSelfTest() {
  using namespace pose_upright_detail;
  PoseUprightSelfTest t;

  const std::array<float, 12> identity{{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0}};
  // model +Y -> world +Z: world = (x, -z, y). This is the T62 permutation shape.
  const std::array<float, 12> yUp{{1, 0, 0, 0, 0, 0, -1, 0, 0, 1, 0, 0}};
  // The same permutation with the sign flipped: model +Y -> world -Z. A body
  // that is upright in model space reads upside down here -- the "permutation
  // applied twice / applied backwards" class this header exists to catch.
  const std::array<float, 12> yFlipped{{1, 0, 0, 0, 0, 0, 1, 0, 0, -1, 0, 0}};
  // The same permutation, uniformly scaled by 2 (a scale is not a placement;
  // it must cancel out of heightRatio).
  const std::array<float, 12> scale2{{2, 0, 0, 0, 0, 0, -2, 0, 0, 2, 0, 0}};
  const std::array<float, 12> moved{{1, 0, 0, 11, 0, 1, 0, 22, 0, 0, 1, 33}};

  // A body standing along model +Y, feet 12 apart on X.
  const auto standY = [](double h) {
    PosePoints p;
    p.head = {{0.0, h, 0.0}};
    p.pelvis = {{0.0, h * 0.5, 0.0}};
    p.footL = {{-6.0, 0.0, 0.0}};
    p.footR = {{6.0, 0.0, 0.0}};
    p.bindHead = p.head;
    p.bindFootL = p.footL;
    p.bindFootR = p.footR;
    p.haveFootL = true;
    p.haveFootR = true;
    return p;
  };
  // The same body standing along model +Z (identity placement).
  const auto standZ = [](double h) {
    PosePoints p;
    p.head = {{0.0, 0.0, h}};
    p.pelvis = {{0.0, 0.0, h * 0.5}};
    p.footL = {{-6.0, 0.0, 0.0}};
    p.footR = {{6.0, 0.0, 0.0}};
    p.bindHead = p.head;
    p.bindFootL = p.footL;
    p.bindFootR = p.footR;
    p.haveFootL = true;
    p.haveFootR = true;
    return p;
  };

  const auto expect = [&](const char* name, const PoseUprightReading& rd, bool wantOrdered,
                          double wantDot, double wantHeight) {
    const bool ok = rd.found && (rd.ordered == wantOrdered) && nearEqual(rd.upDot, wantDot)
      && nearEqual(rd.heightRatio, wantHeight);
    selftestCheck(t, name, ok, std::string("found=") + (rd.found ? "1" : "0")
      + " ordered=" + (rd.ordered ? "1" : "0") + " upDot=" + std::to_string(rd.upDot)
      + " height=" + std::to_string(rd.heightRatio) + " missing=" + rd.missing);
  };

  expect("world-z-bind", measureUprightPoints(standZ(70.0), identity.data()), true, 1.0, 1.0);
  expect("permutation-applied", measureUprightPoints(standY(70.0), yUp.data()), true, 1.0, 1.0);

  {
    PosePoints p = standY(70.0);
    p.head = {{0.0, -70.0, 0.0}};
    p.pelvis = {{0.0, -35.0, 0.0}};
    expect("inverted", measureUprightPoints(p, yUp.data()), false, -1.0, -1.0);
  }
  {
    // Model-space upright, world-space upside down: must not read as standing.
    const PoseUprightReading rd = measureUprightPoints(standY(70.0), yFlipped.data());
    expect("double-permutation", rd, false, -1.0, -1.0);
  }
  expect("scale-cancels", measureUprightPoints(standY(70.0), scale2.data()), true, 1.0, 1.0);
  expect("translation-only", measureUprightPoints(standZ(70.0), moved.data()), true, 1.0, 1.0);
  {
    // Bind axis on screen is horizontal (+X) while the drawn body stands: the
    // reading follows the drawn pose, and bindUp reports the sideways placement.
    PosePoints p = standY(70.0);
    p.bindHead = {{70.0, 0.0, 0.0}};
    p.bindFootL = {{0.0, 0.0, 0.0}};
    p.bindFootR = {{0.0, 0.0, 0.0}};
    const PoseUprightReading rd = measureUprightPoints(p, yUp.data());
    expect("bind-axis-lying", rd, true, 1.0, 1.0);
    selftestCheck(t, "bind-axis-lying:bindUp", nearEqual(rd.bindUp[0], 1.0) && nearEqual(rd.bindUp[1], 0.0)
      && nearEqual(rd.bindUp[2], 0.0), std::string("bindUp=") + std::to_string(rd.bindUp[0]) + ","
      + std::to_string(rd.bindUp[1]) + "," + std::to_string(rd.bindUp[2]));
  }
  {
    // Lying on screen: head and pelvis at the same altitude as the feet.
    PosePoints p = standY(70.0);
    p.head = {{70.0, 0.0, 0.0}};
    p.pelvis = {{35.0, 0.0, 0.0}};
    expect("lying", measureUprightPoints(p, yUp.data()), false, 0.0, 0.0);
  }
  {
    const PoseUprightReading rd = measureUprightPoints(standZ(70.0), nullptr);
    selftestCheck(t, "null-matrix", !rd.found && rd.missing == "model-to-world",
      std::string("found=") + (rd.found ? "1" : "0") + " missing=" + rd.missing);
  }
  {
    PosePoints p = standY(70.0);
    p.bindHead = {{0.0, 0.0, 0.0}};
    const PoseUprightReading rd = measureUprightPoints(p, yUp.data());
    selftestCheck(t, "bind-height-guard", !rd.found && rd.missing == "bind-height",
      std::string("found=") + (rd.found ? "1" : "0") + " missing=" + rd.missing);
  }
  {
    // Rolled 90 degrees on screen: model +Y maps to world +X, so a body that is
    // upright in model space is DRAWN lying down. The ruler is world +Z, so this
    // must not read as standing (the model-space reading would say it is).
    const std::array<float, 12> roll{{0, 1, 0, 0, -1, 0, 0, 0, 0, 0, 1, 0}};
    expect("ruler-is-world-z", measureUprightPoints(standY(70.0), roll.data()), false, 0.0, 0.0);
  }
  return t;
}

} // namespace tf2::native
