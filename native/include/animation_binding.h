#pragma once

// The product-side animation layer.
//
// animation_decoder.h already decodes MDLs, samples a sequence at a tick and
// composes local transforms into model space. What it deliberately does not do
// is anything an application needs around that: it does not know which model an
// entity is, it does not cache, it does not map an animation skeleton onto a
// render skeleton, and it does not produce the skin matrices a GPU wants.
// This header is that missing layer and nothing else -- no rendering, no window,
// no entity protocol.
//
// Two facts about TF2 models drive the shape of it:
//
//   1. A model and its animation data are frequently in two files. The player
//      models are the well-known case (scout.mdl + scout_animations.mdl), and
//      the relationship is declared in the model's own include list, so it is
//      read rather than guessed.
//   2. The two skeletons are not the same size. scout.mdl carries 78 bones and
//      scout_animations.mdl 76; the extra ones are helpers with no animation
//      channel. Those keep their bind transform. A bone that cannot be placed by
//      name AND parent is not "approximately right" -- it rejects the whole
//      instance, because a partially guessed skeleton is a wrong pose that looks
//      plausible.
//
// The other half of the contract is what happens when the input is unusable.
// `buildAnimationSkinMatrices` never returns a pose it cannot justify: an
// out-of-range sequence, a model with no animation data, a bone mismatch or a
// missing file all come back as `bindPose = true` with a named status and a
// reason string, so the caller can keep drawing the entity and still say why it
// is not moving.
#include "animation_decoder.h"
#include "asset_root.h"
#include "model_loader.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace tf2::native {

// Why an instance is or is not animating. The names are the vocabulary the
// window title and the gates use, so a title that says `animSource=missing`
// and a gate that asserts it are talking about the same thing.
enum class AnimationInputStatus {
  Ok,
  MissingSequence,       // the entity carried no m_nSequence at all
  ModelMissing,          // the model file could not be read
  ModelHasNoAnimation,   // the model carries no animation data (a static prop)
  AnimSourceMissing,     // the declared *_animations.mdl companion is absent
  SequenceOutOfRange,    // m_nSequence is past the model's own sequence table
  BoneMismatch,          // an animation bone did not line up with the skeleton
  TooManyBones,          // more bones than the skinning constant buffer holds
  SampleFailed,          // the decoder refused the frame (fps, frameCount, block)
};

const char* animationInputStatusName(AnimationInputStatus status);

// One model's animation data, mapped onto its render skeleton.
struct AnimationBinding {
  std::string modelPath;
  std::string animPath;            // == modelPath when the model animates itself
  bool separateAnimSource = false;
  AnimationModel anims;
  // animToModel[i] is the render-skeleton bone that animation bone i drives, or
  // -1 when the animation file has no channel for that bone.
  std::vector<std::int32_t> animToModel;
  // The render skeleton's inverse bind matrices, in render-bone order, kept here
  // so the render loop never has to re-inspect a model per frame. The render
  // skeleton is the product loader's, not the animation file's: they differ by
  // the helper bones the animation file does not have.
  std::vector<std::array<float, 12>> poseToBone;
  std::vector<std::uint8_t> poseToBoneValid;
  // The render skeleton's parent index per bone (-1 for a root). Kept next to
  // poseToBone so a bone the animation file does not drive can be placed on its
  // nearest animated ancestor instead of on the model origin.
  std::vector<std::int32_t> boneParent;
  std::size_t renderBones = 0;
  std::size_t validBindBones = 0;
};

// T105-n17 Step 1: the decoded blend grids as assertion lines. Every line is
// complete and already named ("GRID-SUMMARY model=...", "WEIGHTLIST ...") so a
// gate can match its first token; the caller supplies the trace prefix. This is a
// reading of the FILE, never of a pose, so it cannot move what the product draws.
// A binding with no grid sequences still yields the GRID-SUMMARY line (with
// nbMismatch=0) rather than printing nothing.
void animationGridCensus(const AnimationBinding& binding, std::vector<std::string>& out);

// Per-modelPath cache. The point of it is that the render loop must not re-read
// a VPK per frame: a demo with 24 distinct weapon models would otherwise decode
// every one of them 60 times a second.
class AnimationBindingCache {
public:
  void reset(const AssetRoot* root);
  // nullptr when the model cannot be animated. `status` and `reason` say why.
  const AnimationBinding* resolve(const std::string& modelPath,
    AnimationInputStatus& status, std::string& reason);

  std::size_t resolveCalls() const { return resolveCalls_; }
  std::size_t cacheHits() const { return cacheHits_; }
  std::size_t archiveReads() const { return archiveReads_; }
  std::size_t loadedBindings() const { return loadedBindings_; }
  std::size_t unusableBindings() const { return unusableBindings_; }
  std::size_t entries() const { return entries_.size(); }
  void resetStats();

private:
  struct Entry {
    AnimationBinding binding;
    bool usable = false;
    AnimationInputStatus status = AnimationInputStatus::ModelMissing;
    std::string reason;
  };
  const AssetRoot* root_ = nullptr;
  std::vector<Entry> entries_;
  std::vector<std::string> keys_;
  std::size_t resolveCalls_ = 0;
  std::size_t cacheHits_ = 0;
  std::size_t archiveReads_ = 0;
  std::size_t loadedBindings_ = 0;
  std::size_t unusableBindings_ = 0;
};

// T105-n17: one layer of a blended pose. `sequence` names the clip, `weight`
// carries how much of the final pose it supplies, and `cycle` is that clip's
// own position inside itself -- a static aim pose sits at 0 while the
// locomotion cycles advance. A negative cycle means "use the request's cycle".
inline constexpr std::size_t kMaxBlendLayers = 4;

struct AnimationBlendLayer {
  std::int32_t sequence = -1;
  float cycle = -1.0f;
  float weight = 0.0f;
};

// T105-n17 Step 2: the pose parameters a blend grid is indexed by, in the units
// the pose parameter table uses (m_flPoseParameter: degrees for the aim axes,
// -1..1 for the movement axes; a negative moveX is backward).
//
// These values are NOT on the wire: m_flPoseParameter is not sent for a player,
// so the caller derives them by the rule the Step 2 report states and hands them
// in. A grid whose axis this input cannot name keeps the legacy single-cell pose
// and reports why through poseGridReason, rather than silently indexing with 0.
struct AnimationPoseInput {
  bool hasMove = false;
  float moveX = 0.0f;     // forward (+) / backward (-), -1..1
  float moveY = 0.0f;     // right (+) / left (-), -1..1
  bool hasAim = false;
  float aimYaw = 0.0f;    // degrees, + = right of the facing the mesh is drawn in
  float aimPitch = 0.0f;  // degrees, + = up
};

// Legacy plays the sequence bound cell -- the whole 1D locomotion ramp for the
// run grids -- and is the default, so every existing reading and the pose hash
// stay byte-identical until a caller opts in. PoseGrid indexes a sequence grid
// with AnimationPoseInput and blends the four neighbouring cells bilinearly.
enum class AnimationBlendMode { Legacy, PoseGrid };

const char* animationBlendModeName(AnimationBlendMode mode);
bool parseAnimationBlendMode(const std::string& value, AnimationBlendMode& out);

// Which pose parameter drives a grid axis, resolved from the slot NAME and not
// the slot index: the roster stores move_x/move_y in slots (4,5) for
// scout/heavy/sniper and in (5,4) for the other six classes, so an index-based
// reading would be wrong for half of it. Axis 0 is the file inner index
// (column) and axis 1 its outer one (row): cell k = i1 * groupSize[0] + i0.
enum class PoseGridAxisRole { None, MoveX, MoveY, BodyYaw, BodyPitch, Unmapped };

const char* poseGridAxisRoleName(PoseGridAxisRole role);

// One bilinear lookup. Corner order is [0]=(i0,i1), [1]=(i0+1,i1),
// [2]=(i0,i1+1), [3]=(i0+1,i1+1); fraction is the 0..1 position toward the
// upper corner of each axis, position the same position in cell units.
struct PoseGridCoordinate {
  std::array<std::int32_t, 2> index{{0, 0}};
  std::array<float, 2> fraction{{0.0f, 0.0f}};
  std::array<float, 2> position{{0.0f, 0.0f}};
  std::array<bool, 2> clamped{{false, false}};
  std::array<PoseGridAxisRole, 2> role{{PoseGridAxisRole::None, PoseGridAxisRole::None}};
  std::array<std::int32_t, 4> cell{{-1, -1, -1, -1}};
  std::array<float, 4> weight{{0.0f, 0.0f, 0.0f, 0.0f}};
};

// Reads the sequence grid and turns the pose input into four corner cells and
// weights. Returns false (with a reason) when the grid is absent, an axis names
// a pose parameter this input cannot drive, or a corner cell is not an animdesc
// the model has; the caller then keeps its single-cell sample.
bool poseGridCoordinates(const AnimationModel& model, const AnimationSequence& sequence,
  const AnimationPoseInput& input, PoseGridCoordinate& out, std::string& reason);

// T105-n17 Step 3: the per-bone gate a weight list applies. A list that did not
// decode, or a bone past its end, supplies the bone to every layer -- an
// unreadable table must degrade to the legacy blend, not freeze the skeleton.
bool poseWeightListGatesBone(const AnimationWeightList& weights, std::size_t bone);

// One layer local pose, in the decoder layout.
struct AnimationLocalPose {
  std::vector<std::array<float, 3>> position;
  std::vector<std::array<float, 4>> rotation;
};

// Per-bone blend with optional per-layer bone gates. nullptr gates is the
// legacy blend, unchanged bit for bit. Otherwise a layer supplies a bone only
// when its weight is positive AND its gate for that bone is non-zero; the
// participating weights are renormalised per bone (so a gated-off layer does not
// shrink the bone), and a bone no layer may supply falls back to the dominant
// layer instead of collapsing toward the origin. gatedBones counts bones whose
// blend changed because a contributor was gated off; isolatedBones counts the
// fallbacks.
bool blendPoseLayers(const std::vector<AnimationLocalPose>& poses,
  const std::vector<float>& weights,
  const std::vector<std::vector<std::uint8_t>>* boneGates,
  AnimationLocalPose& out, std::size_t& gatedBones, std::size_t& isolatedBones,
  std::string& reason);
// What the entity said. `hasCycle` matters: m_flCycle is a 0..1 position inside
// the sequence and is the more faithful input when the demo carries it, while
// `tick` is the fallback that always exists.
struct AnimationSampleRequest {
  std::int32_t sequence = -1;
  bool hasCycle = false;
  float cycle = 0.0f;
  bool hasPlaybackRate = false;
  float playbackRate = 0.0f;
  std::int32_t tick = 0;
  float tickRate = 30.0f;
  // T105-n17: when blendLayerCount > 0 the sampler mixes these layers instead
  // of playing `sequence` alone. Slots are fixed (idle, walk, run, aim) so the
  // same input produces the same weights in the same order.
  std::size_t blendLayerCount = 0;
  std::array<AnimationBlendLayer, kMaxBlendLayers> blendLayers{};
  // T105-n17 Step 2/3: opt-in grid sampling, and the weight_list gate that only
  // PoseGrid mode can use. Both default off so a request written before this
  // change behaves exactly as it did.
  AnimationBlendMode blendMode = AnimationBlendMode::Legacy;
  AnimationPoseInput pose;
  bool poseBoneMask = false;
};

struct AnimationSampleResult {
  AnimationInputStatus status = AnimationInputStatus::MissingSequence;
  std::string reason;
  std::string sequenceLabel;
  std::string animPath;
  int frame = 0;
  int frameCount = 0;
  // Frame block the requested frame resolved to, and how many frames a block
  // covers. sectionFrames == 1 (or a constant localFrame across advancing
  // frames) means the animation carries one pose for its whole span, so the
  // cursor moves and the pose does not -- an asset property, not a wiring gap.
  int localFrame = 0;
  int sectionFrames = 0;
  std::size_t skinBones = 0;      // matrices written
  std::size_t validBones = 0;     // render bones with a usable inverse bind
  bool bindPose = true;           // true => the caller should not enable skinning
  // Column-major 4x4, the layout the skinning constant buffer and the
  // StructuredBuffer both want. Empty when bindPose is true.
  std::vector<std::array<float, 16>> skinMatrices;
  // Row-major 3x4 per render bone: that bone's local -> model transform at this
  // sample, i.e. the matrix `skinMatrices[i]` was built from (skin = boneModel *
  // poseToBone). This is the quantity source-side `pBoneToWorld` names, which is
  // why an attachment rides on it: an entity on a bone is placed at
  // `boneModel * attachment->local`. Identity -- not zero -- for a bone no
  // channel reached, so "the helper bone did not move" stays distinguishable
  // from "collapse this thing onto the origin". Empty when bindPose is true.
  std::vector<std::array<float, 12>> boneModelMatrices;
  // T105-n17: the blend actually applied. blendWeights/blendSequences are
  // indexed by LAYER SLOT and carry the normalised weights, so a caller can
  // assert "sum == 1" and name each layer's clip from one reading.
  // blendRawWeightSum is the caller's own sum BEFORE normalisation: a dropped
  // layer shows up there instead of being silently scaled away.
  std::size_t blendLayers = 0;
  std::size_t blendDropped = 0;
  float blendWeightSum = 0.0f;
  float blendRawWeightSum = 0.0f;
  std::array<float, kMaxBlendLayers> blendWeights{{0.0f, 0.0f, 0.0f, 0.0f}};
  std::array<std::int32_t, kMaxBlendLayers> blendSequences{{-1, -1, -1, -1}};
  std::string blendReason;
  // T105-n17 Step 2: the grid lookup actually taken. These describe the FIRST
  // sequence sampled through a grid in this call (the primary sequence when it
  // has one, otherwise the first grid layer), and poseGridLayers counts how many
  // sequences went through a grid. All zero when the mode is Legacy or no sample
  // carried a valid grid.
  bool poseGridApplied = false;
  std::size_t poseGridLayers = 0;
  std::array<std::int32_t, 2> poseGridIndex{{0, 0}};
  std::array<float, 2> poseGridFraction{{0.0f, 0.0f}};
  std::array<std::int32_t, 2> poseGridRoles{{0, 0}};  // PoseGridAxisRole per axis
  std::array<bool, 2> poseGridClamped{{false, false}};
  std::array<std::int32_t, 4> poseGridCells{{-1, -1, -1, -1}};
  std::array<float, 4> poseGridWeights{{0.0f, 0.0f, 0.0f, 0.0f}};
  std::array<std::int32_t, 4> poseGridFrames{{0, 0, 0, 0}};
  std::size_t poseMaskGatedBones = 0;
  std::size_t poseMaskIsolatedBones = 0;
  // Why a grid that should have been used was not: the trace prints it instead of
  // showing a grid-mode sample that silently kept cell 0.
  std::string poseGridReason;
};

// Composes two row-major 3x4 transforms in the convention the skinning path
// uses: out(v) = a(b(v)), i.e. the engine's ConcatTransforms. Exposed because
// placing a child model on a bone attachment is exactly one such composition,
// and a multiply re-derived at the call site is how a transposed copy ships.
std::array<float, 12> multiplyTransforms(
  const std::array<float, 12>& a, const std::array<float, 12>& b);

// Composes the sampled local transforms along the parent chain into full
// model-space 3x4 matrices (rotation AND translation). The position-only
// `composeLocalToModel` in animation_decoder.h cannot drive a GPU: a skinning
// matrix that drops rotation deforms every bone about the wrong origin.
bool composeLocalToModelMatrices(
  const std::vector<std::int32_t>& parents,
  const std::vector<std::array<float, 3>>& localPosition,
  const std::vector<std::array<float, 4>>& localRotation,
  std::vector<std::array<float, 12>>& modelMatrices,
  std::string& reason);

// skin[i] = animatedWorld[i] * poseToBone[i], in column-major 4x4.
// On any unusable input this returns bindPose = true, an empty matrix list and a
// reason; it never returns a partial pose.
AnimationSampleResult buildAnimationSkinMatrices(
  const AnimationBinding* binding,
  const AnimationSampleRequest& request);

// The cap the renderer's skinning constant buffer enforces.
inline constexpr std::size_t kMaxSkinningBones = 128;

} // namespace tf2::native
