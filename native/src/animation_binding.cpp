#include "animation_binding.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>

namespace tf2::native {
namespace {

// 3x4 row-major with the column-vector convention (v' = M * v). This is the
// engine's ConcatTransforms: `out = in1 * in2` in the sense that in1 is applied
// after in2, which is why a child's world matrix is parent * childLocal.
std::array<float, 12> concatTransforms(const std::array<float, 12>& a, const std::array<float, 12>& b) {
  std::array<float, 12> out{};
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 3; ++c) {
      float sum = 0.0f;
      for (int k = 0; k < 3; ++k) sum += a[r * 4 + k] * b[k * 4 + c];
      out[r * 4 + c] = sum;
    }
    float t = a[r * 4 + 3];
    for (int k = 0; k < 3; ++k) t += a[r * 4 + k] * b[k * 4 + 3];
    out[r * 4 + 3] = t;
  }
  return out;
}

std::array<float, 12> localTransform(const std::array<float, 3>& pos, const std::array<float, 4>& quat) {
  const float x = quat[0], y = quat[1], z = quat[2], w = quat[3];
  std::array<float, 12> m{};
  m[0] = 1.0f - 2.0f * (y * y + z * z); m[1] = 2.0f * (x * y - z * w); m[2] = 2.0f * (x * z + y * w);
  m[4] = 2.0f * (x * y + z * w); m[5] = 1.0f - 2.0f * (x * x + z * z); m[6] = 2.0f * (y * z - x * w);
  m[8] = 2.0f * (x * z - y * w); m[9] = 2.0f * (y * z + x * w); m[10] = 1.0f - 2.0f * (x * x + y * y);
  m[3] = pos[0]; m[7] = pos[1]; m[11] = pos[2];
  return m;
}

// poseToBone is stored row-major as 12 floats; the GPU wants column-major 4x4.
std::array<float, 16> toColumnMajor(const std::array<float, 12>& m) {
  return {
    m[0], m[4], m[8], 0.0f,
    m[1], m[5], m[9], 0.0f,
    m[2], m[6], m[10], 0.0f,
    m[3], m[7], m[11], 1.0f
  };
}

bool finite3(const std::array<float, 3>& v) {
  return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}

bool finite4(const std::array<float, 4>& v) {
  return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]) && std::isfinite(v[3]);
}

std::string companionPath(const std::string& modelPath) {
  if (modelPath.size() < 4) return {};
  const std::string stem = modelPath.substr(0, modelPath.size() - 4);
  const std::size_t slash = stem.rfind('/');
  const std::string directory = slash == std::string::npos ? std::string() : stem.substr(0, slash + 1);
  const std::string base = slash == std::string::npos ? stem : stem.substr(slash + 1);
  return directory + base + "_animations.mdl";
}

// Reads one file through the same route ModelLoader uses: ask the archive set
// which archives hold the path, then read from the first that answers. Opening a
// VPK per lookup is what made the old per-model path cost a second each.
std::vector<std::uint8_t> readAsset(const AssetRoot& root, const std::string& path) {
  std::vector<std::filesystem::path> holders;
  const VpkArchiveSet& archives = root.archives();
  archives.collectContaining(path, holders);
  for (const auto& archivePath : holders) {
    const VpkArchive* archive = archives.find(archivePath);
    if (archive == nullptr) continue;
    std::string error;
    const auto bytes = archive->read(path, &error);
    if (!bytes.empty()) return bytes;
  }
  // A loose file wins only when no archive answered, matching resolveAsset.
  std::error_code fileError;
  const auto loose = root.resolve(path);
  if (std::filesystem::is_regular_file(loose, fileError) && !fileError) {
    std::ifstream stream(loose, std::ios::binary);
    if (stream) {
      return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(stream),
        std::istreambuf_iterator<char>());
    }
  }
  return {};
}

} // namespace

const char* animationInputStatusName(AnimationInputStatus status) {
  switch (status) {
    case AnimationInputStatus::Ok: return "ok";
    case AnimationInputStatus::MissingSequence: return "missing";
    case AnimationInputStatus::ModelMissing: return "model-missing";
    case AnimationInputStatus::ModelHasNoAnimation: return "no-animation";
    case AnimationInputStatus::AnimSourceMissing: return "anim-source-missing";
    case AnimationInputStatus::SequenceOutOfRange: return "sequence-out-of-range";
    case AnimationInputStatus::BoneMismatch: return "bone-mismatch";
    case AnimationInputStatus::TooManyBones: return "too-many-bones";
    case AnimationInputStatus::SampleFailed: return "sample-failed";
  }
  return "unknown";
}

void AnimationBindingCache::reset(const AssetRoot* root) {
  root_ = root;
  entries_.clear();
  keys_.clear();
  resetStats();
}

void AnimationBindingCache::resetStats() {
  resolveCalls_ = 0;
  cacheHits_ = 0;
  archiveReads_ = 0;
  loadedBindings_ = 0;
  unusableBindings_ = 0;
}

const AnimationBinding* AnimationBindingCache::resolve(const std::string& modelPath,
    AnimationInputStatus& status, std::string& reason) {
  ++resolveCalls_;
  for (std::size_t i = 0; i < keys_.size(); ++i) {
    if (keys_[i] != modelPath) continue;
    ++cacheHits_;
    status = entries_[i].status;
    reason = entries_[i].reason;
    return entries_[i].usable ? &entries_[i].binding : nullptr;
  }

  Entry entry;
  entry.binding.modelPath = modelPath;
  const auto finish = [&](bool usable, AnimationInputStatus entryStatus, const std::string& entryReason) {
    entry.usable = usable;
    entry.status = entryStatus;
    entry.reason = entryReason;
    if (usable) ++loadedBindings_; else ++unusableBindings_;
    keys_.push_back(modelPath);
    entries_.push_back(std::move(entry));
    status = entries_.back().status;
    reason = entries_.back().reason;
    return entries_.back().usable ? &entries_.back().binding : nullptr;
  };

  if (root_ == nullptr || !root_->valid()) {
    return finish(false, AnimationInputStatus::ModelMissing, "no asset root");
  }

  ++archiveReads_;
  const auto modelBytes = readAsset(*root_, modelPath);
  if (modelBytes.empty()) {
    return finish(false, AnimationInputStatus::ModelMissing, "model file could not be read");
  }
  AnimationModel model;
  if (decodeAnimationModel(modelBytes.data(), modelBytes.size(), model) != AnimationStatus::Ok) {
    return finish(false, AnimationInputStatus::ModelMissing, "model header did not decode: " + model.reason);
  }
  entry.binding.renderBones = model.bones.size();

  // Route 1: the model animates itself. Route 2: a companion named by the
  // model's own include list. Route 3: the conventional <stem>_animations.mdl.
  // The include list is consulted first because it is what the model declares;
  // the convention is the fallback for files whose include list is empty.
  AnimationModel anims;
  std::string animPath = modelPath;
  if (decodeAnimationModel(modelBytes.data(), modelBytes.size(), anims) != AnimationStatus::Ok
      || anims.animations.empty()) {
    std::string candidate;
    for (const auto& include : model.includes) {
      if (include.find("_animations.mdl") != std::string::npos) { candidate = include; break; }
    }
    if (candidate.empty()) candidate = companionPath(modelPath);
    bool loaded = false;
    if (!candidate.empty()) {
      ++archiveReads_;
      const auto companionBytes = readAsset(*root_, candidate);
      if (!companionBytes.empty()) {
        AnimationModel decoded;
        if (decodeAnimationModel(companionBytes.data(), companionBytes.size(), decoded) == AnimationStatus::Ok
            && !decoded.animations.empty()) {
          anims = std::move(decoded);
          animPath = candidate;
          entry.binding.separateAnimSource = true;
          loaded = true;
        }
      }
    }
    if (!loaded) {
      // Distinguish "declared a companion that is absent" from "has no
      // animation at all": the first is a packaging fault, the second is a
      // static prop, and a caller that cannot tell them apart cannot report
      // which one it hit.
      if (!candidate.empty()) {
        return finish(false, AnimationInputStatus::AnimSourceMissing,
          "animation companion is absent: " + candidate);
      }
      return finish(false, AnimationInputStatus::ModelHasNoAnimation, "model carries no animation data");
    }
  }
  if (anims.animations.empty()) {
    return finish(false, AnimationInputStatus::ModelHasNoAnimation, "animation source carries no animations");
  }
  if (anims.bones.empty()) {
    return finish(false, AnimationInputStatus::BoneMismatch, "animation source has no bones");
  }

  // The render skeleton comes from the product loader so the inverse bind
  // matrices and the bone order are the ones the mesh was built against.
  // inspectVpk takes the archive that actually holds the model, so the asset
  // candidate is what names it -- guessing tf2_misc_dir.vpk would work for the
  // player models and fail for anything shipped in a per-weapon archive.
  const auto candidate = ModelLoader::resolveAsset(*root_, modelPath);
  const VpkArchiveSet& archiveSet = root_->archives();
  ModelInspection inspection;
  bool inspected = false;
  for (const auto& archivePath : candidate.vpkArchives) {
    const VpkArchive* archive = archiveSet.find(archivePath);
    if (archive == nullptr || !archive->contains(modelPath)) continue;
    inspection = ModelLoader::inspectVpk(*archive, modelPath);
    inspected = true;
    break;
  }
  if (!inspected) inspection = ModelLoader::inspect(root_->resolve(modelPath));
  if (!inspection.metadata.valid || inspection.metadata.bones.empty()) {
    return finish(false, AnimationInputStatus::BoneMismatch, "render skeleton could not be read");
  }
  entry.binding.renderBones = inspection.metadata.bones.size();
  entry.binding.poseToBone.resize(inspection.metadata.bones.size());
  entry.binding.poseToBoneValid.assign(inspection.metadata.bones.size(), 0);
  for (std::size_t i = 0; i < inspection.metadata.bones.size(); ++i) {
    entry.binding.poseToBone[i] = inspection.metadata.bones[i].poseToBone;
    entry.binding.poseToBoneValid[i] = inspection.metadata.bones[i].poseToBoneValid ? 1 : 0;
    if (inspection.metadata.bones[i].poseToBoneValid) ++entry.binding.validBindBones;
  }
  if (inspection.metadata.bones.size() > kMaxSkinningBones) {
    return finish(false, AnimationInputStatus::TooManyBones, "render skeleton is larger than the skinning buffer");
  }

  entry.binding.animToModel.assign(anims.bones.size(), -1);
  std::size_t mapped = 0;
  for (std::size_t i = 0; i < anims.bones.size(); ++i) {
    for (std::size_t j = 0; j < inspection.metadata.bones.size(); ++j) {
      if (inspection.metadata.bones[j].name != anims.bones[i].name) continue;
      if (inspection.metadata.bones[j].parent != anims.bones[i].parent) continue;
      entry.binding.animToModel[i] = static_cast<std::int32_t>(j);
      ++mapped;
      break;
    }
  }
  // Every animation bone has to land. The render skeleton is allowed to be
  // larger -- the tail is helper bones with no channel, and they keep their bind
  // transform -- but an animation bone with nowhere to go would silently drop
  // part of the pose.
  if (mapped != anims.bones.size()) {
    return finish(false, AnimationInputStatus::BoneMismatch,
      "animation bone did not line up with the render skeleton");
  }

  entry.binding.animPath = animPath;
  entry.binding.anims = std::move(anims);
  return finish(true, AnimationInputStatus::Ok, {});
}

bool composeLocalToModelMatrices(
    const std::vector<std::int32_t>& parents,
    const std::vector<std::array<float, 3>>& localPosition,
    const std::vector<std::array<float, 4>>& localRotation,
    std::vector<std::array<float, 12>>& modelMatrices,
    std::string& reason) {
  const std::size_t count = parents.size();
  if (localPosition.size() != count || localRotation.size() != count) {
    reason = "bone parent is outside the skeleton";
    return false;
  }
  modelMatrices.assign(count, {});
  for (std::size_t i = 0; i < count; ++i) {
    const auto parent = parents[i];
    // Only an earlier bone is composable. A forward or self reference would need
    // a fixpoint, and the engine's own format guarantees the order; treating a
    // violation as an error is what keeps a cycle from becoming a hang.
    if (parent < -1 || parent >= static_cast<std::int32_t>(i)) {
      reason = "bone parent is not an earlier bone";
      return false;
    }
    if (!finite3(localPosition[i]) || !finite4(localRotation[i])) {
      reason = "bone transform is not finite";
      return false;
    }
    const auto local = localTransform(localPosition[i], localRotation[i]);
    modelMatrices[i] = parent < 0
      ? local
      : concatTransforms(modelMatrices[static_cast<std::size_t>(parent)], local);
  }
  reason.clear();
  return true;
}

AnimationSampleResult buildAnimationSkinMatrices(
    const AnimationBinding* binding,
    const AnimationSampleRequest& request) {
  AnimationSampleResult result;
  result.validBones = binding != nullptr ? binding->validBindBones : 0;

  const auto refuse = [&](AnimationInputStatus status, const std::string& reason) {
    result.status = status;
    result.reason = reason;
    result.bindPose = true;
    result.skinMatrices.clear();
    result.skinBones = 0;
    return result;
  };

  if (binding == nullptr) return refuse(AnimationInputStatus::ModelMissing, "no animation binding");
  if (binding->poseToBone.empty()) return refuse(AnimationInputStatus::BoneMismatch, "render skeleton has no bones");
  if (binding->poseToBone.size() > kMaxSkinningBones) {
    return refuse(AnimationInputStatus::TooManyBones, "render skeleton is larger than the skinning buffer");
  }
  if (binding->anims.animations.empty()) {
    return refuse(AnimationInputStatus::ModelHasNoAnimation, "animation source carries no animations");
  }
  if (request.sequence < 0) {
    return refuse(AnimationInputStatus::MissingSequence, "entity carried no m_nSequence");
  }
  if (static_cast<std::size_t>(request.sequence) >= binding->anims.sequences.size()) {
    // The common real case for a TF2 weapon: the entity's m_nSequence indexes the
    // owner's animation set, not the weapon model's own tiny table. Rejecting it
    // is the correct outcome -- the alternative is playing an unrelated clip.
    return refuse(AnimationInputStatus::SequenceOutOfRange,
      "sequence " + std::to_string(request.sequence) + " is past the model's "
      + std::to_string(binding->anims.sequences.size()) + " sequences");
  }
  const auto& sequence = binding->anims.sequences[static_cast<std::size_t>(request.sequence)];
  result.sequenceLabel = sequence.label;
  result.animPath = binding->animPath;
  if (sequence.animDesc < 0 || static_cast<std::size_t>(sequence.animDesc) >= binding->anims.animations.size()) {
    return refuse(AnimationInputStatus::SequenceOutOfRange, "sequence has no animation binding");
  }
  const auto& anim = binding->anims.animations[static_cast<std::size_t>(sequence.animDesc)];
  result.frameCount = anim.frameCount;
  if (!(anim.fps > 0.0f) || !std::isfinite(anim.fps)) {
    return refuse(AnimationInputStatus::SampleFailed, "animation rate is not usable");
  }
  if (anim.frameCount <= 0) {
    return refuse(AnimationInputStatus::SampleFailed, "animation frame count is not usable");
  }

  // m_flCycle is a 0..1 position inside the sequence and is the more faithful
  // input when the demo carries it. It is converted into the tick the decoder
  // expects rather than bypassing the decoder, so the section/loop handling
  // stays in one place.
  std::int32_t sampleTick = request.tick;
  if (request.hasCycle && std::isfinite(request.cycle)) {
    const double frames = static_cast<double>(request.cycle) * static_cast<double>(anim.frameCount);
    const double ticks = frames * static_cast<double>(request.tickRate) / static_cast<double>(anim.fps);
    if (!std::isfinite(ticks) || ticks < -1.0e9 || ticks > 1.0e9) {
      return refuse(AnimationInputStatus::SampleFailed, "m_flCycle is not usable");
    }
    sampleTick = static_cast<std::int32_t>(ticks);
  }
  if (sampleTick < 0 || !(request.tickRate > 0.0f) || !std::isfinite(request.tickRate)) {
    return refuse(AnimationInputStatus::SampleFailed, "tick or tick rate is not usable");
  }

  const auto sample = sampleAnimation(binding->anims, request.sequence, sampleTick, request.tickRate);
  if (sample.status != AnimationStatus::Ok) {
    return refuse(AnimationInputStatus::SampleFailed, sample.reason);
  }
  result.frame = sample.frame;
  result.localFrame = sample.localFrame;
  result.sectionFrames = sample.sectionFrames;

  // The animation skeleton's own parent chain.
  std::vector<std::int32_t> parents;
  parents.reserve(binding->anims.bones.size());
  for (const auto& bone : binding->anims.bones) parents.push_back(bone.parent);
  std::vector<std::array<float, 12>> animWorld;
  std::string composeReason;
  if (!composeLocalToModelMatrices(parents, sample.localPosition, sample.localRotation,
      animWorld, composeReason)) {
    return refuse(AnimationInputStatus::SampleFailed, composeReason);
  }

  // skin[i] = animatedWorld[i] * poseToBone[i]. A render bone with no animation
  // channel keeps its bind transform, which for the skinning path is the
  // identity matrix -- i.e. "do not move this vertex", not "move it somewhere
  // the animation never asked for".
  static const std::array<float, 16> identity = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
  result.skinMatrices.assign(binding->poseToBone.size(), identity);
  std::size_t written = 0;
  for (std::size_t i = 0; i < binding->poseToBone.size(); ++i) {
    if (!binding->poseToBoneValid[i]) continue;
    const std::array<float, 12>* animated = nullptr;
    for (std::size_t a = 0; a < binding->animToModel.size(); ++a) {
      if (binding->animToModel[a] == static_cast<std::int32_t>(i)) { animated = &animWorld[a]; break; }
    }
    if (animated == nullptr) continue;  // helper bone: keep the identity above
    const auto skin = concatTransforms(*animated, binding->poseToBone[i]);
    for (int k = 0; k < 12; ++k) {
      if (!std::isfinite(skin[k])) {
        return refuse(AnimationInputStatus::SampleFailed, "skin matrix is not finite");
      }
    }
    result.skinMatrices[i] = toColumnMajor(skin);
    ++written;
  }
  if (written == 0) {
    return refuse(AnimationInputStatus::BoneMismatch, "no render bone accepted an animation channel");
  }
  result.skinBones = written;
  result.status = AnimationInputStatus::Ok;
  result.bindPose = false;
  result.reason.clear();
  return result;
}

} // namespace tf2::native
