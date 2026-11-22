#pragma once

#include "model_loader.h"

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace tf2::native {

// ===========================================================================
// task-41: WEARABLE BONE MERGE -- the pose half of drawing a cosmetic
//
// WHY THIS EXISTS. task-40 (native/src/wearable_instance.cpp) answers which
// cosmetic instance belongs to which player and which model file it names. It
// does NOT answer where that model goes on the body, and the demo cannot answer
// it either: the measured origin/angles of every wearable instance on
// cp_process_f12 is 0,0,0 (docs/WEARABLE-ATTRIBUTION-2026-11-21.md, 332 of 332
// instances), so placing a hat by the entity transform puts it at the map
// origin, on the floor. The only honest placement left is the OWNER'S
// SKELETON: the same rule Source uses, where a wearable's bones are replaced by
// the owner's bones of the same name and the piece is then skinned with its own
// vertex weights.
//
// WHAT THE MATRICES ARE. One column-major 4x4 per WEARABLE render bone, in the
// wearable's own bone order, in the exact layout the entity pass reads
// (StructuredBuffer<float4> boneColumns, native/src/native_renderer_shaders.cpp:857,
// four float4 columns per bone). The value is
//
//   skin[i] = ownerBoneModel[j] * wearableBone.poseToBone[i]
//
// which is the standard skinning formula (native/src/animation_binding.cpp:1148,
// skin = boneModel * poseToBone) with ONE substitution: the wearable's own
// model-space bone frame is replaced by the owner's frame for the bone of the
// same name. poseToBone stays the WEARABLE's, because it maps the wearable's
// bind-pose model space into that bone's space -- it is the inverse bind of the
// mesh being drawn, not of the body it rides.
//
// Both operands are row-major 3x4 with the column-vector convention (v' = M*v),
// and the product is the engine's ConcatTransforms, reached through
// multiplyTransforms (native/include/animation_binding.h:305) rather than
// re-derived here.
//
// WHY THE NAME AND NOT AN ATTACHMENT. m_iParentAttachment is 0 on all 332
// measured wearable instances, so the recording never names the bone the piece
// rides. The name is the only correspondence the two models share.
//
// WHAT IS REFUSED. There is no origin/angles path in this module and there
// never will be: ownerPoseAvailable == false is the only answer when the owner
// has no pose, and the caller must then NOT DRAW the piece. A cosmetic dropped
// from a frame is a counted skip; a cosmetic at the map origin is a lie about
// where the player was.

// The wearable-side half of the correspondence: one entry per wearable render
// bone, in the order the model's vertex bone indices use.
struct WearableBoneBinding {
  std::string name;
  // The wearable's own inverse bind: maps the wearable's bind-pose MODEL space
  // into this bone's space. Row-major 3x4, v' = M*v.
  std::array<float, 12> poseToBone{};
  bool poseToBoneValid = false;
};

// What one wearable's merge actually did, so a report can say WHICH bones were
// placed by the owner and which stayed in the model's authored bind pose. A
// piece whose bones mostly fall back is mostly unposed, and that is a finding
// about the assets, not a footnote to hide.
struct WearableBoneMerge {
  // False => the caller must not draw this piece at all.
  bool ownerPoseAvailable = false;
  // Non-null, short, ASCII: why the merge was refused or partly degraded.
  const char* reason = "owner-pose-unavailable";
  std::size_t bones = 0;           // render bones the wearable model has
  std::size_t mergedBones = 0;     // bones that took the owner's frame
  std::size_t bindPoseBones = 0;   // bones the owner's model does not name
  std::size_t invalidBones = 0;    // bones with no usable inverse bind
  // Column-major 4x4 per wearable render bone. Empty when ownerPoseAvailable is
  // false, so a caller that ignores the flag draws nothing rather than drawing
  // an identity pose at the wrong place.
  std::vector<std::array<float, 16>> matrices;
};

// The merge itself. `ownerBoneNames` and `ownerBoneModelMatrices` are the OWNER
// model's bone table and that model's bone frames AT THIS TICK (the row-major 3x4
// model-space matrices AnimationSampleResult::boneModelMatrices carries,
// native/include/animation_binding.h:267), in the same index order -- which is
// the order both the animation binding and rememberEntityRig build them in, so
// index i names the same bone on both sides.
WearableBoneMerge buildWearableBoneMatrices(
  const std::vector<WearableBoneBinding>& wearableBones,
  const std::vector<std::string>& ownerBoneNames,
  const std::vector<std::array<float, 12>>& ownerBoneModelMatrices);

// Adaptor from the loader's bone table. Exists so the caller never builds the
// binding list by hand: a second construction site is where the index order
// would drift from the vertex bone indices.
std::vector<WearableBoneBinding> wearableBoneBindings(const std::vector<ModelBone>& bones);

// --------------------------------------------------------------- self-test
// The checks this module can make about itself, run by
// `demo_open_probe --cosmetics-pose-selftest`. Returns the number of failures
// and appends one line per check to `report`.
std::size_t runWearablePoseSelftest(std::vector<std::string>& report);

} // namespace tf2::native
