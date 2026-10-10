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
  std::size_t renderBones = 0;
  std::size_t validBindBones = 0;
};

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
};

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
