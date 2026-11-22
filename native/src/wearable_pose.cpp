#include "wearable_pose.h"

#include "animation_binding.h"

#include <cmath>
#include <cstdio>
#include <unordered_map>
#include <utility>

namespace tf2::native {
namespace {

constexpr std::array<float, 16> kIdentityColumnMajor = {
  1.0f, 0.0f, 0.0f, 0.0f,
  0.0f, 1.0f, 0.0f, 0.0f,
  0.0f, 0.0f, 1.0f, 0.0f,
  0.0f, 0.0f, 0.0f, 1.0f,
};

bool finiteTransform(const std::array<float, 12>& m) {
  for (const float value : m) {
    if (!std::isfinite(value)) return false;
  }
  return true;
}

bool finiteColumnMajor(const std::array<float, 16>& m) {
  for (const float value : m) {
    if (!std::isfinite(value)) return false;
  }
  return true;
}

// The bone's own bind frame, i.e. the inverse of its inverse bind. This is the
// same algebra native/src/animation_binding.cpp:1107-1114 performs for a bone no
// animation channel reached: poseToBone maps MODEL space into BONE space, so the
// bone's frame in model space is its inverse, and reading it the other way is
// what put a held weapon under the floor on the first attempt.
//
// A bone this function cannot invert gets the identity instead of a guess, and
// the caller counts it: an unusable inverse bind is a property of the asset, and
// substituting a translation would move geometry nobody measured.
std::array<float, 12> inverseBindOrIdentity(const WearableBoneBinding& bone, bool& inverted) {
  std::array<float, 12> out{1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
  inverted = false;
  if (!bone.poseToBoneValid || !finiteTransform(bone.poseToBone)) return out;
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 3; ++c) out[r * 4 + c] = bone.poseToBone[c * 4 + r];
    out[r * 4 + 3] = -(out[r * 4 + 0] * bone.poseToBone[3]
      + out[r * 4 + 1] * bone.poseToBone[7]
      + out[r * 4 + 2] * bone.poseToBone[11]);
  }
  if (!finiteTransform(out)) {
    out = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
    return out;
  }
  inverted = true;
  return out;
}

}  // namespace

std::vector<WearableBoneBinding> wearableBoneBindings(const std::vector<ModelBone>& bones) {
  std::vector<WearableBoneBinding> out;
  out.reserve(bones.size());
  for (const ModelBone& bone : bones) {
    WearableBoneBinding binding;
    binding.name = bone.name;
    binding.poseToBone = bone.poseToBone;
    binding.poseToBoneValid = bone.poseToBoneValid;
    out.push_back(std::move(binding));
  }
  return out;
}

WearableBoneMerge buildWearableBoneMatrices(
    const std::vector<WearableBoneBinding>& wearableBones,
    const std::vector<std::string>& ownerBoneNames,
    const std::vector<std::array<float, 12>>& ownerBoneModelMatrices) {
  WearableBoneMerge merge;
  merge.bones = wearableBones.size();
  if (wearableBones.empty()) {
    merge.reason = "no-render-bones";
    return merge;
  }
  // The owner's pose is what makes the merge possible at all. A body that was
  // not sampled this frame -- budget-dropped, off camera, bind-pose only -- has
  // no bone frames, and there is no second-best answer: the piece is not drawn.
  if (ownerBoneNames.empty() || ownerBoneModelMatrices.empty()
      || ownerBoneNames.size() != ownerBoneModelMatrices.size()) {
    merge.reason = "owner-pose-unavailable";
    return merge;
  }

  // Name -> index. First occurrence wins, which is the rule every other
  // name-matching site in this codebase applies (a duplicate bone name is an
  // asset defect, and taking the last would silently pick a different one
  // depending on iteration order).
  std::unordered_map<std::string, std::size_t> ownerIndexByName;
  ownerIndexByName.reserve(ownerBoneNames.size() * 2);
  for (std::size_t i = 0; i < ownerBoneNames.size(); ++i) {
    if (ownerBoneNames[i].empty()) continue;
    ownerIndexByName.emplace(ownerBoneNames[i], i);
  }

  merge.matrices.assign(wearableBones.size(), kIdentityColumnMajor);
  for (std::size_t i = 0; i < wearableBones.size(); ++i) {
    const WearableBoneBinding& bone = wearableBones[i];
    bool inverted = false;
    const auto ownFrame = inverseBindOrIdentity(bone, inverted);
    if (!inverted) {
      // No usable inverse bind: identity, counted. The vertex stays where the
      // wearable's own bind pose puts it rather than being flung by a guess.
      ++merge.invalidBones;
      continue;
    }
    const auto found = bone.name.empty() ? ownerIndexByName.end()
                                         : ownerIndexByName.find(bone.name);
    if (found == ownerIndexByName.end()) {
      // The owner's model does not name this bone. The piece keeps its own bind
      // frame -- which, composed with its own inverse bind, is the identity, so
      // this bone does not move relative to the wearable's authored pose.
      ++merge.bindPoseBones;
      continue;
    }
    // The substitution that makes this a merge: the OWNER's model-space frame
    // for the bone of the same name replaces the wearable's own, and the
    // wearable's inverse bind still maps its bind-pose vertices into that
    // frame's space.
    const auto& ownerFrame = ownerBoneModelMatrices[found->second];
    if (!finiteTransform(ownerFrame)) {
      ++merge.invalidBones;
      continue;
    }
    const auto skin = multiplyTransforms(ownerFrame, ownFrame);
    const std::array<float, 16> columnMajor = {
      skin[0], skin[4], skin[8], 0.0f,
      skin[1], skin[5], skin[9], 0.0f,
      skin[2], skin[6], skin[10], 0.0f,
      skin[3], skin[7], skin[11], 1.0f,
    };
    if (!finiteColumnMajor(columnMajor)) {
      // One non-finite element rejects the renderer's WHOLE bone upload
      // (native/src/native_renderer_entity.cpp:796-800), which would take every
      // skinned body in the frame with it. Drop this bone to identity and count.
      ++merge.invalidBones;
      continue;
    }
    merge.matrices[i] = columnMajor;
    ++merge.mergedBones;
  }
  merge.ownerPoseAvailable = true;
  merge.reason = merge.mergedBones == 0 ? "no-bone-name-matched"
                : merge.bindPoseBones == 0 && merge.invalidBones == 0 ? "merged"
                : "merged-partial";
  return merge;
}

// ------------------------------------------------------------------ self-test
namespace {

bool closeTo(float a, float b, float tolerance) {
  return std::fabs(a - b) <= tolerance;
}

}  // namespace

std::size_t runWearablePoseSelftest(std::vector<std::string>& report) {
  std::size_t failures = 0;
  const auto check = [&](bool ok, const char* name) {
    if (!ok) ++failures;
    char line[192];
    std::snprintf(line, sizeof(line), "wearable_pose_selftest\t%s\t%s", name,
                  ok ? "PASS" : "FAIL");
    report.emplace_back(line);
  };

  const std::array<float, 12> identity12 = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};

  // 1. An identity owner frame reproduces exactly the wearable's own skin
  //    matrix, which is the definition of "the piece did not move".
  {
    WearableBoneBinding bone;
    bone.name = "bip_head";
    bone.poseToBone = {1, 0, 0, -4.0f, 0, 1, 0, -8.0f, 0, 0, 1, -2.0f};
    bone.poseToBoneValid = true;
    const auto merge = buildWearableBoneMatrices({bone}, {"bip_head"}, {identity12});
    // poseToBone maps model->bone; the bone's own frame is its inverse, and
    // owner * ownFrame with owner == identity must give back that inverse.
    const std::array<float, 16> expected = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 4.0f, 8.0f, 2.0f, 1};
    bool same = merge.ownerPoseAvailable && merge.matrices.size() == 1;
    for (std::size_t k = 0; same && k < 16; ++k) same = closeTo(merge.matrices[0][k], expected[k], 1e-5f);
    check(same, "identity-owner-reproduces-bind-frame");
    check(merge.mergedBones == 1 && merge.bindPoseBones == 0 && merge.invalidBones == 0,
          "identity-owner-counts-merged");
  }

  // 2. The owner's TRANSLATION lands in the last column of the column-major
  //    matrix -- the layout check. A row-major 3x4 uploaded raw would put the
  //    translation at 3/7/11 and transpose the rotation.
  {
    WearableBoneBinding bone;
    bone.name = "bip_head";
    bone.poseToBone = identity12;
    bone.poseToBoneValid = true;
    std::array<float, 12> owner = identity12;
    owner[3] = 1.0f; owner[7] = 2.0f; owner[11] = 3.0f;
    const auto merge = buildWearableBoneMatrices({bone}, {"bip_head"}, {owner});
    const bool placed = merge.matrices.size() == 1
      && closeTo(merge.matrices[0][12], 1.0f, 1e-5f)
      && closeTo(merge.matrices[0][13], 2.0f, 1e-5f)
      && closeTo(merge.matrices[0][14], 3.0f, 1e-5f)
      && closeTo(merge.matrices[0][15], 1.0f, 1e-5f)
      && closeTo(merge.matrices[0][3], 0.0f, 1e-5f);
    check(placed, "owner-translation-is-last-column");
  }

  // 3. Bones are matched BY NAME, not by index. Reversing the owner's table must
  //    not move which frame the wearable bone receives.
  {
    WearableBoneBinding a;
    a.name = "bip_head"; a.poseToBone = identity12; a.poseToBoneValid = true;
    WearableBoneBinding b;
    b.name = "bip_pelvis"; b.poseToBone = identity12; b.poseToBoneValid = true;
    std::array<float, 12> headFrame = identity12; headFrame[7] = 10.0f;
    std::array<float, 12> pelvisFrame = identity12; pelvisFrame[7] = -10.0f;
    // Order on the owner side: pelvis first, head second.
    const auto merge = buildWearableBoneMatrices(
      {a, b}, {"bip_pelvis", "bip_head"}, {pelvisFrame, headFrame});
    const bool byName = merge.matrices.size() == 2
      && closeTo(merge.matrices[0][13], 10.0f, 1e-5f)
      && closeTo(merge.matrices[1][13], -10.0f, 1e-5f);
    check(byName, "matched-by-name-not-index");
  }

  // 4. A bone the owner does not name stays in the wearable's own bind pose and
  //    is counted, never silently merged.
  {
    WearableBoneBinding named;
    named.name = "bip_head"; named.poseToBone = identity12; named.poseToBoneValid = true;
    WearableBoneBinding unnamed;
    unnamed.name = "hat_ribbon_04"; unnamed.poseToBone = identity12; unnamed.poseToBoneValid = true;
    const auto merge = buildWearableBoneMatrices({named, unnamed}, {"bip_head"}, {identity12});
    check(merge.bindPoseBones == 1 && merge.mergedBones == 1,
          "unmatched-bone-counted-as-bind-pose");
    check(merge.matrices.size() == 2
          && closeTo(merge.matrices[1][0], 1.0f, 1e-5f)
          && closeTo(merge.matrices[1][12], 0.0f, 1e-5f),
          "unmatched-bone-keeps-bind-frame");
    check(std::string(merge.reason) == "merged-partial", "partial-merge-reason");
  }

  // 5. No owner pose => refused, and NO matrices. The drawer keys its
  //    poseUnavailable counter off this, so an empty list here is what stops a
  //    cosmetic from being placed at the entity origin.
  {
    WearableBoneBinding bone;
    bone.name = "bip_head"; bone.poseToBone = identity12; bone.poseToBoneValid = true;
    const auto empty = buildWearableBoneMatrices({bone}, {}, {});
    check(!empty.ownerPoseAvailable && empty.matrices.empty(), "no-owner-pose-refused");
    check(std::string(empty.reason) == "owner-pose-unavailable", "no-owner-pose-reason");
    const auto mismatch = buildWearableBoneMatrices({bone}, {"a", "b"}, {identity12});
    check(!mismatch.ownerPoseAvailable && mismatch.matrices.empty(),
          "owner-table-size-mismatch-refused");
    const auto noBones = buildWearableBoneMatrices({}, {"bip_head"}, {identity12});
    check(!noBones.ownerPoseAvailable && std::string(noBones.reason) == "no-render-bones",
          "empty-wearable-refused");
  }

  // 6. A non-finite owner frame is dropped to identity and COUNTED. The renderer
  //    rejects the whole upload on one NaN, so this is the guard that keeps one
  //    bad cosmetic bone from un-skinning every body in the frame.
  {
    WearableBoneBinding bone;
    bone.name = "bip_head"; bone.poseToBone = identity12; bone.poseToBoneValid = true;
    std::array<float, 12> poisoned = identity12;
    poisoned[3] = std::nanf("");
    const auto merge = buildWearableBoneMatrices({bone}, {"bip_head"}, {poisoned});
    bool allFinite = merge.matrices.size() == 1;
    for (std::size_t k = 0; allFinite && k < 16; ++k) allFinite = std::isfinite(merge.matrices[0][k]);
    check(allFinite && merge.invalidBones == 1, "non-finite-owner-frame-dropped");
  }

  // 7. A bone with no usable inverse bind is counted and left at identity.
  {
    WearableBoneBinding bone;
    bone.name = "bip_head"; bone.poseToBoneValid = false;
    const auto merge = buildWearableBoneMatrices({bone}, {"bip_head"}, {identity12});
    check(merge.invalidBones == 1 && merge.mergedBones == 0, "invalid-inverse-bind-counted");
    check(merge.matrices.size() == 1 && closeTo(merge.matrices[0][0], 1.0f, 1e-5f),
          "invalid-inverse-bind-identity");
  }

  // 8. Every matrix the merge produces is finite and unit-translation-w, for a
  //    mixed table. This is the invariant setEntityBoneMatrices depends on.
  {
    WearableBoneBinding a; a.name = "bip_head"; a.poseToBone = identity12; a.poseToBoneValid = true;
    WearableBoneBinding b; b.name = "bip_pelvis"; b.poseToBone = {0, -1, 0, 3, 1, 0, 0, 4, 0, 0, 1, 5}; b.poseToBoneValid = true;
    WearableBoneBinding c; c.name = "ghost"; c.poseToBoneValid = false;
    std::array<float, 12> owner = {0, 1, 0, 7, -1, 0, 0, 8, 0, 0, 1, 9};
    const auto merge = buildWearableBoneMatrices({a, b, c}, {"bip_head", "bip_pelvis"},
                                                 {owner, owner});
    bool ok = merge.matrices.size() == 3;
    for (const auto& m : merge.matrices) {
      for (const float v : m) if (!std::isfinite(v)) ok = false;
      if (!closeTo(m[15], 1.0f, 1e-5f)) ok = false;
      if (!closeTo(m[3], 0.0f, 1e-5f) || !closeTo(m[7], 0.0f, 1e-5f)
          || !closeTo(m[11], 0.0f, 1e-5f)) ok = false;
    }
    check(ok, "every-matrix-finite-and-affine");
    check(merge.bones == 3, "bone-count-reported");
  }

  return failures;
}

} // namespace tf2::native
