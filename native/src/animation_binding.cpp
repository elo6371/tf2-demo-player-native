#include "animation_binding.h"

#include "animation_blend.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
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
  //
  // Measured 2026-10-16 (T6): route 1's "the model animates itself" test is
  // `animations.empty()`, and a TF2 arms/player model carries a STUB anim table
  // -- c_sniper_arms.mdl has one 1-frame animation (`@ref`) and 43 sequences of
  // which exactly one (index 0, `ref`) has an animdesc; scout.mdl has two of
  // 306. So route 1 answered "yes" for a model whose own table cannot pose any
  // of the sequences the entity actually plays (the demo's CTFViewModel carries
  // m_nSequence 1..30), and the include that carries the real data
  // (c_sniper_animations.mdl: 88 animations, 76 sequences all bound, the same
  // 58 bones) was never read. The fix below keeps route 1's precedence unless
  // the declared companion binds STRICTLY MORE of the model's own sequences by
  // the sequence NAME -- the name is the deliberate link between the two
  // tables; the index is not (the base table carries one extra sequence at
  // index 0, so index 30 and index 29 name different sequences in the two
  // files). A companion that does not strictly improve the binding is not used,
  // so a model whose own table is real keeps it byte for byte.
  AnimationModel anims;
  std::string animPath = modelPath;
  // Candidate animation sources: every declared include that names an
  // "_animations.mdl", then the naming convention as the last resort.
  //
  // WHY ALL OF THEM AND NOT THE FIRST MATCH (2026-10-24, T57/T60)
  //   The first-match rule picked `models/player/<class>_user_animations.mdl`
  //   for all nine player models, because that name contains "_animations.mdl"
  //   and TF2 lists it FIRST in the include table. That file carries one
  //   sequence, so the strictly-more-bindings test below could never fire and
  //   the entity kept the model's own two-sequence stub; the real source
  //   (`<class>_animations.mdl`, 377 sequences all bound for scout) was never
  //   read. Scoring every candidate by how many of the model's own sequences it
  //   binds by name is the difference between "the model declares a companion"
  //   and "the model can actually be posed".
  std::vector<std::string> companionCandidates;
  for (const auto& include : model.includes) {
    if (include.find("_animations.mdl") == std::string::npos) continue;
    if (std::find(companionCandidates.begin(), companionCandidates.end(), include)
        != companionCandidates.end()) continue;
    companionCandidates.push_back(include);
  }
  const std::string conventionalCompanion = companionPath(modelPath);
  if (!conventionalCompanion.empty()
      && std::find(companionCandidates.begin(), companionCandidates.end(), conventionalCompanion)
        == companionCandidates.end()) {
    companionCandidates.push_back(conventionalCompanion);
  }
  const auto boundInBase = [](const AnimationModel& source) {
    std::size_t bound = 0;
    for (const auto& sequence : source.sequences) {
      if (sequence.animDesc >= 0
          && static_cast<std::size_t>(sequence.animDesc) < source.animations.size()) {
        ++bound;
      }
    }
    return bound;
  };
  const auto nameBoundAgainst = [&model](const AnimationModel& source) {
    std::size_t bound = 0;
    for (const auto& baseSequence : model.sequences) {
      for (const auto& candidateSequence : source.sequences) {
        if (candidateSequence.label != baseSequence.label) continue;
        if (candidateSequence.animDesc >= 0
            && static_cast<std::size_t>(candidateSequence.animDesc) < source.animations.size()) {
          ++bound;
        }
        break;
      }
    }
    return bound;
  };
  // Score every candidate; the best one wins. Ties keep the earlier candidate,
  // so the declared order still breaks them.
  std::size_t bestNameBound = 0;
  std::string bestCompanion;
  AnimationModel bestCompanionModel;
  for (const auto& candidate : companionCandidates) {
    ++archiveReads_;
    const auto companionBytes = readAsset(*root_, candidate);
    if (companionBytes.empty()) continue;
    AnimationModel decoded;
    if (decodeAnimationModel(companionBytes.data(), companionBytes.size(), decoded) != AnimationStatus::Ok
        || decoded.animations.empty()) continue;
    const auto nameBound = nameBoundAgainst(decoded);
    if (bestCompanion.empty() || nameBound > bestNameBound) {
      bestNameBound = nameBound;
      bestCompanion = candidate;
      bestCompanionModel = std::move(decoded);
    }
  }
  const bool baseDecoded =
    decodeAnimationModel(modelBytes.data(), modelBytes.size(), anims) == AnimationStatus::Ok
    && !anims.animations.empty();
  if (!baseDecoded) {
    if (bestCompanion.empty()) {
      // Distinguish "declared a companion that is absent" from "has no
      // animation at all": the first is a packaging fault, the second is a
      // static prop, and a caller that cannot tell them apart cannot report
      // which one it hit.
      if (!companionCandidates.empty()) {
        return finish(false, AnimationInputStatus::AnimSourceMissing,
          "animation companion is absent: " + companionCandidates.front());
      }
      return finish(false, AnimationInputStatus::ModelHasNoAnimation, "model carries no animation data");
    }
    anims = std::move(bestCompanionModel);
    animPath = bestCompanion;
    entry.binding.separateAnimSource = true;
  } else if (!bestCompanion.empty() && bestNameBound > boundInBase(anims)) {
    // Route 1 answered, but the answer can still be a stub. Adopt the companion
    // that binds the most of the same sequences by name.
    //
    // The companion's animation data is used, but the base model's sequence
    // table stays the index space: the entity's m_nSequence is an index into the
    // model it named, and the two tables are not index compatible. Every
    // sequence is resolved by name against the companion; one that has no
    // same-named companion sequence is left unbound and refuses with the
    // decoder's existing words rather than playing a neighbour's clip.
    std::vector<AnimationSequence> merged = model.sequences;
    for (auto& sequence : merged) {
      sequence.animDesc = -1;
      sequence.blendCount = 0;
      for (const auto& candidateSequence : bestCompanionModel.sequences) {
        if (candidateSequence.label != sequence.label) continue;
        if (candidateSequence.animDesc >= 0
            && static_cast<std::size_t>(candidateSequence.animDesc) < bestCompanionModel.animations.size()) {
          sequence.animDesc = candidateSequence.animDesc;
          // The base model can carry a zero-blend sequence stub. Report the
          // blend count of the animation source whose descriptor we sample.
          sequence.blendCount = candidateSequence.blendCount;
          // Step 1: the grid belongs to the descriptor whose frames we sample,
          // exactly like the blend count above. A base sequence stub carries no
          // grid, so dropping these two here is what would make the census read
          // "no grid" for every player model.
          sequence.grid = candidateSequence.grid;
          sequence.weights = candidateSequence.weights;
          // Included descriptors own playback semantics (CStudioHdr::pSeqdesc).
          // A base STUDIO_OVERRIDE stub is not necessarily marked looping.
          // Keep the base index/activity and unrelated flags, but take the
          // STUDIO_LOOPING bit from the descriptor whose frames we sample.
          sequence.looping = candidateSequence.looping;
          sequence.flags = (sequence.flags & ~1) | (candidateSequence.flags & 1);
        }
        break;
      }
    }
    anims = std::move(bestCompanionModel);
    anims.sequences = std::move(merged);
    animPath = bestCompanion;
    entry.binding.separateAnimSource = true;
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
  entry.binding.boneParent.assign(inspection.metadata.bones.size(), -1);
  for (std::size_t i = 0; i < inspection.metadata.bones.size(); ++i) {
    entry.binding.poseToBone[i] = inspection.metadata.bones[i].poseToBone;
    entry.binding.poseToBoneValid[i] = inspection.metadata.bones[i].poseToBoneValid ? 1 : 0;
    entry.binding.boneParent[i] = inspection.metadata.bones[i].parent;
    if (inspection.metadata.bones[i].poseToBoneValid) ++entry.binding.validBindBones;
  }
  if (inspection.metadata.bones.size() > kMaxSkinningBones) {
    return finish(false, AnimationInputStatus::TooManyBones, "render skeleton is larger than the skinning buffer");
  }

  // The same "do not replace a working answer" rule as the sequence tables: a
  // companion that cannot place every one of its bones on the render skeleton
  // would turn a model that at least refused per sequence into one that refuses
  // wholesale, so the model's own table is retried before giving up.
  const auto mapBones = [&](const AnimationModel& source) {
    entry.binding.animToModel.assign(source.bones.size(), -1);
    std::size_t mapped = 0;
    for (std::size_t i = 0; i < source.bones.size(); ++i) {
      for (std::size_t j = 0; j < inspection.metadata.bones.size(); ++j) {
        if (inspection.metadata.bones[j].name != source.bones[i].name) continue;
        if (inspection.metadata.bones[j].parent != source.bones[i].parent) continue;
        entry.binding.animToModel[i] = static_cast<std::int32_t>(j);
        ++mapped;
        break;
      }
    }
    // Every animation bone has to land. The render skeleton is allowed to be
    // larger -- the tail is helper bones with no channel, and they keep their bind
    // transform -- but an animation bone with nowhere to go would silently drop
    // part of the pose.
    return mapped == source.bones.size();
  };
  bool bonesMapped = mapBones(anims);
  if (!bonesMapped && entry.binding.separateAnimSource) {
    AnimationModel ownModel;
    if (decodeAnimationModel(modelBytes.data(), modelBytes.size(), ownModel) == AnimationStatus::Ok
        && !ownModel.animations.empty()) {
      anims = std::move(ownModel);
      animPath = modelPath;
      entry.binding.separateAnimSource = false;
      bonesMapped = mapBones(anims);
    }
  }
  if (!bonesMapped) {
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

namespace {

// T105-n17: the m_flCycle -> decoder-tick conversion, kept in ONE place so a
// blended layer and a single-sequence sample cannot disagree about what a
// cycle means.
bool cycleToSampleTick(const AnimationDesc& anim, const AnimationSampleRequest& request,
    bool hasCycle, float cycle, std::int32_t& sampleTick, std::string& reason) {
  sampleTick = request.tick;
  if (hasCycle && std::isfinite(cycle)) {
    const double frames = static_cast<double>(cycle) * static_cast<double>(anim.frameCount);
    const double ticks = frames * static_cast<double>(request.tickRate) / static_cast<double>(anim.fps);
    if (!std::isfinite(ticks) || ticks < -1.0e9 || ticks > 1.0e9) {
      reason = "m_flCycle is not usable";
      return false;
    }
    sampleTick = static_cast<std::int32_t>(ticks);
  }
  if (sampleTick < 0 || !(request.tickRate > 0.0f) || !std::isfinite(request.tickRate)) {
    reason = "tick or tick rate is not usable";
    return false;
  }
  return true;
}

// One layer's local pose, in the decoder's own layout. The public name exists so
// the Step 3 gate can be asserted without a demo; this TU keeps the short alias.
using LocalPose = AnimationLocalPose;

// Weighted average of local transforms: positions linearly, rotations with a
// sign-aligned weighted sum and a renormalise (nlerp). The sign alignment is
// load-bearing: q and -q are the same rotation, but averaging them unaligned
// cancels the bone. Same layers in the same order produce the same bits.
bool blendLocalPoses(const std::vector<LocalPose>& poses, const std::vector<float>& weights,
    LocalPose& out, std::string& reason) {
  if (poses.empty() || poses.size() != weights.size()) { reason = "no blend layers"; return false; }
  const std::size_t bones = poses[0].position.size();
  if (bones == 0 || poses[0].rotation.size() != bones) {
    reason = "blend layers carry no bones";
    return false;
  }
  for (const auto& pose : poses) {
    if (pose.position.size() != bones || pose.rotation.size() != bones) {
      reason = "blend layers disagree on bone count";
      return false;
    }
  }
  out.position.assign(bones, std::array<float, 3>{{0.0f, 0.0f, 0.0f}});
  out.rotation.assign(bones, std::array<float, 4>{{0.0f, 0.0f, 0.0f, 1.0f}});
  for (std::size_t bone = 0; bone < bones; ++bone) {
    const auto& reference = poses[0].rotation[bone];
    double x = 0.0, y = 0.0, z = 0.0, w = 0.0;
    for (std::size_t layer = 0; layer < poses.size(); ++layer) {
      const float weight = weights[layer];
      if (!(weight > 0.0f) || !std::isfinite(weight)) continue;
      const auto& position = poses[layer].position[bone];
      out.position[bone][0] += position[0] * weight;
      out.position[bone][1] += position[1] * weight;
      out.position[bone][2] += position[2] * weight;
      const auto& rotation = poses[layer].rotation[bone];
      const double dot = static_cast<double>(reference[0]) * rotation[0]
        + static_cast<double>(reference[1]) * rotation[1]
        + static_cast<double>(reference[2]) * rotation[2]
        + static_cast<double>(reference[3]) * rotation[3];
      const double sign = dot < 0.0 ? -1.0 : 1.0;
      x += sign * static_cast<double>(rotation[0]) * weight;
      y += sign * static_cast<double>(rotation[1]) * weight;
      z += sign * static_cast<double>(rotation[2]) * weight;
      w += sign * static_cast<double>(rotation[3]) * weight;
    }
    const double length = std::sqrt(x * x + y * y + z * z + w * w);
    if (length > 1.0e-6 && std::isfinite(length)) {
      out.rotation[bone][0] = static_cast<float>(x / length);
      out.rotation[bone][1] = static_cast<float>(y / length);
      out.rotation[bone][2] = static_cast<float>(z / length);
      out.rotation[bone][3] = static_cast<float>(w / length);
    } else {
      // Opposed rotations cancel; the reference bone is the honest fallback,
      // not a zero quaternion that would collapse the bone onto its origin.
      out.rotation[bone] = reference;
    }
    for (int axis = 0; axis < 3; ++axis) {
      if (!std::isfinite(out.position[bone][axis])) {
        out.position[bone][axis] = poses[0].position[bone][axis];
      }
    }
  }
  return true;
}

// T105-n17 Step 2: pose parameter name -> grid axis role. Name, not slot index:
// the roster swaps move_x/move_y between slots 4 and 5 depending on the class.
PoseGridAxisRole poseGridAxisRoleFromName(const std::string& name) {
  std::string lower;
  lower.reserve(name.size());
  for (char c : name) lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  if (lower == "move_x") return PoseGridAxisRole::MoveX;
  if (lower == "move_y") return PoseGridAxisRole::MoveY;
  if (lower == "body_yaw") return PoseGridAxisRole::BodyYaw;
  if (lower == "body_pitch") return PoseGridAxisRole::BodyPitch;
  return PoseGridAxisRole::Unmapped;
}

// One grid sequence sampled at one cycle: the bilinear pose plus the readings the
// result and the probe report.
struct GridPoseSample {
  LocalPose pose;
  PoseGridCoordinate coordinates;
  std::array<std::int32_t, 4> frames{{0, 0, 0, 0}};
  AnimationSample dominant;
  bool hasDominant = false;
  std::vector<std::uint8_t> boneMask;
};

// Every cell is evaluated at the same CYCLE, not at the same tick: cells of one
// grid can have different frame counts (the swim family mixes 31 and 61 frame
// clips), and converting each cell with its own frame count keeps the phase
// aligned. Returns false when the lookup or a needed cell is unusable; the
// caller then keeps the single-cell sample it already has.
bool samplePoseGrid(const AnimationBinding& binding, const AnimationSequence& sequence,
    const AnimationSampleRequest& request, bool hasCycle, float cycle,
    GridPoseSample& out, std::string& reason) {
  const AnimationModel& model = binding.anims;
  if (!poseGridCoordinates(model, sequence, request.pose, out.coordinates, reason)) return false;
  std::vector<LocalPose> poses;
  std::vector<float> weights;
  out.boneMask.assign(model.bones.size(), 0);
  float dominantWeight = -1.0f;
  for (std::size_t corner = 0; corner < 4; ++corner) {
    const float weight = out.coordinates.weight[corner];
    if (!(weight > 0.0f) || !std::isfinite(weight)) continue;
    const std::int32_t cell = out.coordinates.cell[corner];
    if (cell < 0 || static_cast<std::size_t>(cell) >= model.animations.size()) {
      reason = "blend grid cell is outside the animation table";
      return false;
    }
    const auto& cellAnim = model.animations[static_cast<std::size_t>(cell)];
    out.frames[corner] = cellAnim.frameCount;
    std::int32_t cellTick = request.tick;
    if (!cycleToSampleTick(cellAnim, request, hasCycle, cycle, cellTick, reason)) return false;
    AnimationSample cellSample = sampleAnimDesc(model, cell, sequence.looping, cellTick, request.tickRate);
    if (cellSample.status != AnimationStatus::Ok) {
      reason = "blend grid cell " + cellAnim.name + ": " + cellSample.reason;
      return false;
    }
    // A cell only animates part of the skeleton; the layer may supply exactly the
    // union of what its corners animate.
    for (std::size_t bone = 0; bone < out.boneMask.size() && bone < cellSample.boneAnimated.size(); ++bone) {
      if (cellSample.boneAnimated[bone] != 0) out.boneMask[bone] = 1;
    }
    LocalPose cellPose;
    cellPose.position = std::move(cellSample.localPosition);
    cellPose.rotation = std::move(cellSample.localRotation);
    poses.push_back(std::move(cellPose));
    weights.push_back(weight);
    if (weight > dominantWeight) {
      dominantWeight = weight;
      out.dominant = std::move(cellSample);
      out.hasDominant = true;
    }
  }
  if (poses.empty()) {
    reason = "blend grid produced no corner";
    return false;
  }
  if (poses.size() == 1) {
    out.pose = std::move(poses[0]);
    return true;
  }
  float rawSum = 0.0f;
  if (!normalizeBlendWeights(weights.data(), weights.size(), &rawSum)) {
    reason = "blend grid weights are not usable";
    return false;
  }
  return blendLocalPoses(poses, weights, out.pose, reason);
}

} // namespace

const char* animationBlendModeName(AnimationBlendMode mode) {
  switch (mode) {
  case AnimationBlendMode::PoseGrid: return "posegrid";
  case AnimationBlendMode::Legacy: break;
  }
  return "legacy";
}

bool parseAnimationBlendMode(const std::string& value, AnimationBlendMode& out) {
  if (value == "legacy") { out = AnimationBlendMode::Legacy; return true; }
  if (value == "posegrid") { out = AnimationBlendMode::PoseGrid; return true; }
  return false;
}

const char* poseGridAxisRoleName(PoseGridAxisRole role) {
  switch (role) {
  case PoseGridAxisRole::MoveX: return "move_x";
  case PoseGridAxisRole::MoveY: return "move_y";
  case PoseGridAxisRole::BodyYaw: return "body_yaw";
  case PoseGridAxisRole::BodyPitch: return "body_pitch";
  case PoseGridAxisRole::Unmapped: return "unmapped";
  case PoseGridAxisRole::None: break;
  }
  return "none";
}

bool poseWeightListGatesBone(const AnimationWeightList& weights, std::size_t bone) {
  // An unreadable table must degrade to the legacy blend, not freeze bones: only
  // a table that decoded can take a bone away from a layer.
  if (!weights.readable) return true;
  if (bone >= weights.weights.size()) return true;
  return weights.weights[bone] > 0.0f;
}

bool poseGridCoordinates(const AnimationModel& model, const AnimationSequence& sequence,
    const AnimationPoseInput& input, PoseGridCoordinate& out, std::string& reason) {
  out = PoseGridCoordinate{};
  const AnimationBlendGrid& grid = sequence.grid;
  if (!grid.valid || grid.cells.empty()) {
    reason = "sequence has no blend grid";
    return false;
  }
  const std::int32_t width = grid.groupSize[0];
  const std::int32_t height = grid.groupSize[1];
  if (width <= 0 || height <= 0
      || static_cast<std::size_t>(width) * static_cast<std::size_t>(height) != grid.cells.size()) {
    reason = "blend grid cell count does not match its group sizes";
    return false;
  }
  for (std::size_t axis = 0; axis < 2; ++axis) {
    const std::int32_t count = grid.groupSize[axis];
    if (count <= 1) continue;
    const std::int32_t slot = grid.paramIndex[axis];
    if (slot < 0 || static_cast<std::size_t>(slot) >= model.poseParameters.size()) {
      reason = "blend grid axis is not a pose parameter";
      return false;
    }
    const AnimationPoseParam& param = model.poseParameters[static_cast<std::size_t>(slot)];
    const PoseGridAxisRole role = poseGridAxisRoleFromName(param.name);
    out.role[axis] = role;
    if (role == PoseGridAxisRole::Unmapped) {
      reason = "pose parameter '" + param.name + "' drives a grid axis this input cannot supply";
      return false;
    }
    // A recognised axis with no input keeps its neutral value (0) rather than
    // refusing: a caller that supplies only movement still gets the centre of an
    // aim grid, which is the bind-pose row.
    float value = 0.0f;
    if (role == PoseGridAxisRole::MoveX) { if (input.hasMove) value = input.moveX; }
    else if (role == PoseGridAxisRole::MoveY) { if (input.hasMove) value = input.moveY; }
    else if (role == PoseGridAxisRole::BodyYaw) { if (input.hasAim) value = input.aimYaw; }
    else if (role == PoseGridAxisRole::BodyPitch) { if (input.hasAim) value = input.aimPitch; }
    if (!std::isfinite(value)) value = 0.0f;
    const float low = std::min(grid.paramStart[axis], grid.paramEnd[axis]);
    const float high = std::max(grid.paramStart[axis], grid.paramEnd[axis]);
    const float clamped = std::min(std::max(value, low), high);
    out.clamped[axis] = clamped != value;
    const float span = grid.paramEnd[axis] - grid.paramStart[axis];
    float position = 0.0f;
    if (span > 0.0f || span < 0.0f) {
      position = (clamped - grid.paramStart[axis]) / span * static_cast<float>(count - 1);
    }
    if (!std::isfinite(position)) position = 0.0f;
    out.position[axis] = position;
    std::int32_t lower = static_cast<std::int32_t>(std::floor(position));
    if (lower < 0) lower = 0;
    if (lower > count - 1) lower = count - 1;
    if (lower > count - 2) {
      // The last cell is a corner, not a band: keep the lower corner at count-2
      // and let the fraction reach 1, so interpolation does not fold back.
      lower = count - 2;
      out.fraction[axis] = 1.0f;
    } else {
      out.fraction[axis] = position - static_cast<float>(lower);
    }
    out.index[axis] = lower;
  }
  const auto at = [&grid, width](std::int32_t column, std::int32_t row) -> std::int32_t {
    const std::size_t k = static_cast<std::size_t>(row) * static_cast<std::size_t>(width)
      + static_cast<std::size_t>(column);
    return k < grid.cells.size() ? grid.cells[k] : -1;
  };
  const std::int32_t i0 = out.index[0];
  const std::int32_t i1 = out.index[1];
  const std::int32_t columnHigh = width > 1 ? std::min<std::int32_t>(i0 + 1, width - 1) : i0;
  const std::int32_t rowHigh = height > 1 ? std::min<std::int32_t>(i1 + 1, height - 1) : i1;
  out.cell[0] = at(i0, i1);
  out.cell[1] = at(columnHigh, i1);
  out.cell[2] = at(i0, rowHigh);
  out.cell[3] = at(columnHigh, rowHigh);
  const float fx = width > 1 ? out.fraction[0] : 0.0f;
  const float fy = height > 1 ? out.fraction[1] : 0.0f;
  out.weight[0] = (1.0f - fx) * (1.0f - fy);
  out.weight[1] = fx * (1.0f - fy);
  out.weight[2] = (1.0f - fx) * fy;
  out.weight[3] = fx * fy;
  for (std::size_t corner = 0; corner < 4; ++corner) {
    if (!(out.weight[corner] > 0.0f)) continue;
    const std::int32_t cell = out.cell[corner];
    if (cell < 0 || static_cast<std::size_t>(cell) >= model.animations.size()) {
      reason = "blend grid cell is outside the animation table";
      return false;
    }
  }
  return true;
}

bool blendPoseLayers(const std::vector<AnimationLocalPose>& poses,
    const std::vector<float>& weights,
    const std::vector<std::vector<std::uint8_t>>* boneGates,
    AnimationLocalPose& out, std::size_t& gatedBones, std::size_t& isolatedBones,
    std::string& reason) {
  gatedBones = 0;
  isolatedBones = 0;
  if (boneGates == nullptr) return blendLocalPoses(poses, weights, out, reason);
  if (poses.empty() || poses.size() != weights.size()) { reason = "no blend layers"; return false; }
  if (boneGates->size() != poses.size()) {
    reason = "bone gates disagree with the layer count";
    return false;
  }
  const std::size_t bones = poses[0].position.size();
  if (bones == 0 || poses[0].rotation.size() != bones) {
    reason = "blend layers carry no bones";
    return false;
  }
  for (const auto& pose : poses) {
    if (pose.position.size() != bones || pose.rotation.size() != bones) {
      reason = "blend layers disagree on bone count";
      return false;
    }
  }
  std::size_t dominant = 0;
  for (std::size_t layer = 1; layer < poses.size(); ++layer) {
    if (weights[layer] > weights[dominant]) dominant = layer;
  }
  out.position.assign(bones, std::array<float, 3>{{0.0f, 0.0f, 0.0f}});
  out.rotation.assign(bones, std::array<float, 4>{{0.0f, 0.0f, 0.0f, 1.0f}});
  for (std::size_t bone = 0; bone < bones; ++bone) {
    const auto& reference = poses[0].rotation[bone];
    double x = 0.0, y = 0.0, z = 0.0, w = 0.0;
    double px = 0.0, py = 0.0, pz = 0.0;
    double participant = 0.0;
    bool excluded = false;
    for (std::size_t layer = 0; layer < poses.size(); ++layer) {
      const float weight = weights[layer];
      if (!(weight > 0.0f) || !std::isfinite(weight)) continue;
      const std::vector<std::uint8_t>& gate = (*boneGates)[layer];
      if (bone < gate.size() && gate[bone] == 0) { excluded = true; continue; }
      const auto& position = poses[layer].position[bone];
      px += static_cast<double>(position[0]) * weight;
      py += static_cast<double>(position[1]) * weight;
      pz += static_cast<double>(position[2]) * weight;
      const auto& rotation = poses[layer].rotation[bone];
      const double dot = static_cast<double>(reference[0]) * rotation[0]
        + static_cast<double>(reference[1]) * rotation[1]
        + static_cast<double>(reference[2]) * rotation[2]
        + static_cast<double>(reference[3]) * rotation[3];
      const double sign = dot < 0.0 ? -1.0 : 1.0;
      x += sign * static_cast<double>(rotation[0]) * weight;
      y += sign * static_cast<double>(rotation[1]) * weight;
      z += sign * static_cast<double>(rotation[2]) * weight;
      w += sign * static_cast<double>(rotation[3]) * weight;
      participant += weight;
    }
    if (excluded) ++gatedBones;
    if (!(participant > 0.0)) {
      ++isolatedBones;
      out.position[bone] = poses[dominant].position[bone];
      out.rotation[bone] = poses[dominant].rotation[bone];
      continue;
    }
    const double inverse = 1.0 / participant;
    const double position[3] = {px * inverse, py * inverse, pz * inverse};
    for (int axis = 0; axis < 3; ++axis) {
      out.position[bone][axis] = std::isfinite(position[axis])
        ? static_cast<float>(position[axis])
        : poses[dominant].position[bone][axis];
    }
    const double length = std::sqrt(x * x + y * y + z * z + w * w);
    if (length > 1.0e-6 && std::isfinite(length)) {
      out.rotation[bone][0] = static_cast<float>(x / length);
      out.rotation[bone][1] = static_cast<float>(y / length);
      out.rotation[bone][2] = static_cast<float>(z / length);
      out.rotation[bone][3] = static_cast<float>(w / length);
    } else {
      out.rotation[bone] = poses[dominant].rotation[bone];
    }
  }
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

  // T105-n17: when layers are requested the pose is their weighted mix, not the
  // single sequence above. The single sample still ran: it is the fallback when
  // every layer is unusable, and it is what the single-sequence callers
  // (weapon attach, crouch/flinch layers) keep getting bit for bit.
  LocalPose pose;
  pose.position = sample.localPosition;
  pose.rotation = sample.localRotation;

  // T105-n17 Step 2: a grid sequence is INDEXED by pose parameters, not played as
  // its first cell. The single sample above stays the fallback: a grid whose axis
  // this call has no input for keeps the legacy picture and says why.
  const bool poseGridMode = request.blendMode == AnimationBlendMode::PoseGrid;
  const auto applyGridReadings = [&result](const GridPoseSample& grid) {
    result.poseGridIndex = grid.coordinates.index;
    result.poseGridFraction = grid.coordinates.fraction;
    result.poseGridRoles[0] = static_cast<std::int32_t>(grid.coordinates.role[0]);
    result.poseGridRoles[1] = static_cast<std::int32_t>(grid.coordinates.role[1]);
    result.poseGridClamped = grid.coordinates.clamped;
    result.poseGridCells = grid.coordinates.cell;
    result.poseGridWeights = grid.coordinates.weight;
    result.poseGridFrames = grid.frames;
  };
  std::vector<std::uint8_t> primaryMask;
  if (poseGridMode && sequence.grid.valid) {
    GridPoseSample grid;
    std::string gridReason;
    if (samplePoseGrid(*binding, sequence, request, request.hasCycle, request.cycle,
        grid, gridReason)) {
      pose = std::move(grid.pose);
      primaryMask = std::move(grid.boneMask);
      applyGridReadings(grid);
      result.poseGridApplied = true;
      ++result.poseGridLayers;
      if (grid.hasDominant) {
        result.frame = grid.dominant.frame;
        result.frameCount = grid.dominant.frameCount;
        result.localFrame = grid.dominant.localFrame;
        result.sectionFrames = grid.dominant.sectionFrames;
      }
    } else {
      result.poseGridReason = gridReason;
    }
  }
  if (request.blendLayerCount > 0) {
    std::vector<LocalPose> poses;
    std::vector<float> weights;
    std::vector<std::vector<std::uint8_t>> gates;
    const std::size_t requested = request.blendLayerCount < kMaxBlendLayers
      ? request.blendLayerCount : kMaxBlendLayers;
    float dominantWeight = -1.0f;
    std::size_t dropped = 0;
    for (std::size_t slot = 0; slot < requested; ++slot) {
      const AnimationBlendLayer& layer = request.blendLayers[slot];
      if (!(layer.weight > 0.0f) || !std::isfinite(layer.weight)) { ++dropped; continue; }
      if (layer.sequence < 0) { ++dropped; continue; }
      if (static_cast<std::size_t>(layer.sequence) >= binding->anims.sequences.size()) {
        ++dropped;
        continue;
      }
      const auto& layerSequence =
        binding->anims.sequences[static_cast<std::size_t>(layer.sequence)];
      if (layerSequence.animDesc < 0
          || static_cast<std::size_t>(layerSequence.animDesc) >= binding->anims.animations.size()) {
        ++dropped;
        continue;
      }
      const auto& layerAnim =
        binding->anims.animations[static_cast<std::size_t>(layerSequence.animDesc)];
      if (!(layerAnim.fps > 0.0f) || !std::isfinite(layerAnim.fps) || layerAnim.frameCount <= 0) {
        ++dropped;
        continue;
      }
      const bool layerHasCycle = layer.cycle >= 0.0f || request.hasCycle;
      const float layerCycle = layer.cycle >= 0.0f ? layer.cycle : request.cycle;
      LocalPose layerPose;
      std::vector<std::uint8_t> layerMask;
      AnimationSample layerSample;
      bool layerSampled = false;
      if (poseGridMode && layerSequence.grid.valid) {
        GridPoseSample grid;
        std::string gridReason;
        if (samplePoseGrid(*binding, layerSequence, request, layerHasCycle, layerCycle,
            grid, gridReason)) {
          layerPose = std::move(grid.pose);
          layerMask = std::move(grid.boneMask);
          layerSample = std::move(grid.dominant);
          layerSampled = true;
          if (!result.poseGridApplied) applyGridReadings(grid);
          result.poseGridApplied = true;
          ++result.poseGridLayers;
        } else if (result.poseGridReason.empty()) {
          result.poseGridReason = gridReason;
        }
      }
      if (!layerSampled) {
        std::int32_t layerTick = request.tick;
        std::string tickReason;
        if (!cycleToSampleTick(layerAnim, request, layerHasCycle, layerCycle, layerTick, tickReason)) {
          ++dropped;
          continue;
        }
        layerSample = sampleAnimation(binding->anims, layer.sequence, layerTick, request.tickRate);
        if (layerSample.status != AnimationStatus::Ok) { ++dropped; continue; }
        layerPose.position = layerSample.localPosition;
        layerPose.rotation = layerSample.localRotation;
        layerMask = layerSample.boneAnimated;
      }
      poses.push_back(std::move(layerPose));
      weights.push_back(layer.weight);
      if (poseGridMode && request.poseBoneMask) {
        std::vector<std::uint8_t> gate(binding->anims.bones.size(), 1);
        for (std::size_t bone = 0; bone < gate.size(); ++bone) {
          bool supplies = poseWeightListGatesBone(layerSequence.weights, bone);
          if (supplies && bone < layerMask.size()) supplies = layerMask[bone] != 0;
          gate[bone] = supplies ? 1 : 0;
        }
        gates.push_back(std::move(gate));
      }
      result.blendSequences[slot] = layer.sequence;
      result.blendWeights[slot] = layer.weight;
      if (layer.weight > dominantWeight) {
        dominantWeight = layer.weight;
        result.sequenceLabel = layerSequence.label;
        result.frame = layerSample.frame;
        result.frameCount = layerSample.frameCount;
        result.localFrame = layerSample.localFrame;
        result.sectionFrames = layerSample.sectionFrames;
      }
    }
    result.blendDropped = dropped;
    float rawSum = 0.0f;
    if (!poses.empty() && normalizeBlendWeights(weights.data(), weights.size(), &rawSum)) {
      result.blendRawWeightSum = rawSum;
      result.blendWeightSum = 1.0f;
      result.blendLayers = poses.size();
      std::size_t used = 0;
      for (std::size_t slot = 0; slot < requested; ++slot) {
        if (result.blendWeights[slot] > 0.0f && used < weights.size()) {
          result.blendWeights[slot] = weights[used++];
        }
      }
      const std::vector<std::vector<std::uint8_t>>* gateList =
        (poseGridMode && request.poseBoneMask && gates.size() == poses.size()) ? &gates : nullptr;
      std::size_t gatedBones = 0;
      std::size_t isolatedBones = 0;
      if (!blendPoseLayers(poses, weights, gateList, pose, gatedBones, isolatedBones,
          result.blendReason)) {
        pose.position = sample.localPosition;
        pose.rotation = sample.localRotation;
        result.blendLayers = 0;
        result.blendReason = "blend kept the single sample: " + result.blendReason;
      } else {
        result.poseMaskGatedBones = gatedBones;
        result.poseMaskIsolatedBones = isolatedBones;
      }
    } else {
      result.blendReason = "every blend layer was unusable; played the single sequence";
    }
  }

  // The animation skeleton's own parent chain.
  std::vector<std::int32_t> parents;
  parents.reserve(binding->anims.bones.size());
  for (const auto& bone : binding->anims.bones) parents.push_back(bone.parent);
  std::vector<std::array<float, 12>> animWorld;
  std::string composeReason;
  if (!composeLocalToModelMatrices(parents, pose.position, pose.rotation,
      animWorld, composeReason)) {
    return refuse(AnimationInputStatus::SampleFailed, composeReason);
  }

  // skin[i] = currentBoneModel[i] * poseToBone[i]. Bones without animation
  // channels keep their bind-relative local transform under the animated parent.
  static const std::array<float, 16> identity = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
  static const std::array<float, 12> identity12 = {1,0,0, 0, 0,1,0, 0, 0,0,1, 0};
  result.skinMatrices.assign(binding->poseToBone.size(), identity);
  result.boneModelMatrices.assign(binding->poseToBone.size(), identity12);
  std::size_t written = 0;
  std::size_t acceptedChannels = 0;
  for (std::size_t i = 0; i < binding->poseToBone.size(); ++i) {
    const std::array<float, 12>* animated = nullptr;
    for (std::size_t a = 0; a < binding->animToModel.size(); ++a) {
      if (binding->animToModel[a] == static_cast<std::int32_t>(i)) { animated = &animWorld[a]; break; }
    }
    if (animated == nullptr) {
      // No channel: the bone did not move relative to its parent, so its
      // CURRENT model-space frame is the parent's current frame times the
      // bind-relative transform of this bone inside that parent.
      //
      // Read `poseToBone` the way Source does: it maps MODEL space into BONE
      // space (it is the inverse bind), so the bone's bind frame is its
      // inverse. Measured on demo.mdl: bip_hand_R lands at (-20.35, 46.30,
      // 3.35) and its child weapon_bone at (-19.34, 40.17, 6.43) -- 6.9 units
      // from the hand, i.e. the grip. Reading the same numbers as if they were
      // bone->model puts the weapon bone at y = -6.4, under the floor, which is
      // the "gun at the feet" the first attempt produced.
      //
      //   B[i]     = inverse(poseToBone[i])                (bone -> model, bind)
      //   local    = inverse(B[parent]) * B[i] = poseToBone[parent] * B[i]
      //   current  = current[parent] * local
      //
      // Neither the identity (a child of this bone lands on the MODEL origin)
      // nor the raw inverse bind (a weapon behind the back) is that answer.
      const std::int32_t parent =
        i < binding->boneParent.size() ? binding->boneParent[i] : -1;
      const auto inverseBind = [](const std::array<float, 12>& p2b, std::array<float, 12>& out) {
        for (int r = 0; r < 3; ++r) {
          for (int c = 0; c < 3; ++c) out[r * 4 + c] = p2b[c * 4 + r];
          out[r * 4 + 3] = -(out[r * 4 + 0] * p2b[3]
            + out[r * 4 + 1] * p2b[7]
            + out[r * 4 + 2] * p2b[11]);
        }
      };
      if (parent >= 0 && static_cast<std::size_t>(parent) < i
          && binding->poseToBoneValid[i] && binding->poseToBoneValid[parent]) {
        std::array<float, 12> childBind{};
        inverseBind(binding->poseToBone[i], childBind);
        const auto localBind = concatTransforms(binding->poseToBone[parent], childBind);
        const auto model = concatTransforms(result.boneModelMatrices[parent], localBind);
        bool finite = true;
        for (int k = 0; k < 12; ++k) {
          if (!std::isfinite(model[k])) finite = false;
        }
        result.boneModelMatrices[i] = finite ? model : childBind;
      } else if (binding->poseToBoneValid[i]) {
        // No usable ancestor: this bone's own bind frame is the honest answer.
        std::array<float, 12> childBind{};
        inverseBind(binding->poseToBone[i], childBind);
        result.boneModelMatrices[i] = childBind;
      }
      if (binding->poseToBoneValid[i]) {
        const auto skin = concatTransforms(result.boneModelMatrices[i], binding->poseToBone[i]);
        for (int k = 0; k < 12; ++k) {
          if (!std::isfinite(skin[k])) {
            return refuse(AnimationInputStatus::SampleFailed, "helper skin matrix is not finite");
          }
        }
        result.skinMatrices[i] = toColumnMajor(skin);
        ++written;
      }
      continue;
    }
    // Published even when the inverse bind is unusable: an attachment only needs
    // the bone's frame, not the skinned-vertex matrix derived from it.
    result.boneModelMatrices[i] = *animated;
    if (!binding->poseToBoneValid[i]) continue;
    const auto skin = concatTransforms(*animated, binding->poseToBone[i]);
    for (int k = 0; k < 12; ++k) {
      if (!std::isfinite(skin[k])) {
        return refuse(AnimationInputStatus::SampleFailed, "skin matrix is not finite");
      }
    }
    result.skinMatrices[i] = toColumnMajor(skin);
    ++written;
    ++acceptedChannels;
  }
  if (acceptedChannels == 0) {
    return refuse(AnimationInputStatus::BoneMismatch, "no render bone accepted an animation channel");
  }
  result.skinBones = written;
  result.status = AnimationInputStatus::Ok;
  result.bindPose = false;
  result.reason.clear();
  return result;
}

std::array<float, 12> multiplyTransforms(const std::array<float, 12>& a, const std::array<float, 12>& b) {
  return concatTransforms(a, b);
}

void animationGridCensus(const AnimationBinding& binding, std::vector<std::string>& out) {
  const AnimationModel& model = binding.anims;
  const auto cellName = [&model](std::int16_t cell) -> std::string {
    if (cell < 0 || static_cast<std::size_t>(cell) >= model.animations.size()) return std::string("-");
    const std::string& name = model.animations[static_cast<std::size_t>(cell)].name;
    return name.empty() ? std::string("-") : name;
  };
  const auto cellFrames = [&model](std::int16_t cell) -> std::int32_t {
    if (cell < 0 || static_cast<std::size_t>(cell) >= model.animations.size()) return -1;
    return model.animations[static_cast<std::size_t>(cell)].frameCount;
  };

  std::size_t multi = 0;
  std::size_t nine = 0;
  std::size_t twelve = 0;
  std::size_t oneDim = 0;
  std::size_t other = 0;
  std::size_t nbMismatch = 0;
  std::size_t cellsBad = 0;
  std::size_t gridsValid = 0;
  std::size_t weightReadable = 0;
  for (const auto& sequence : model.sequences) {
    if (sequence.blendCount <= 1) continue;
    ++multi;
    const std::int64_t gridCells = static_cast<std::int64_t>(sequence.grid.groupSize[0])
      * static_cast<std::int64_t>(sequence.grid.groupSize[1]);
    if (gridCells != sequence.blendCount) ++nbMismatch;
    if (sequence.grid.cells.size() != static_cast<std::size_t>(sequence.blendCount)) ++cellsBad;
    if (sequence.grid.valid) ++gridsValid;
    if (sequence.weights.readable) ++weightReadable;
    if (sequence.grid.groupSize[0] == 3 && sequence.grid.groupSize[1] == 3) ++nine;
    else if (sequence.grid.groupSize[0] == 3 && sequence.grid.groupSize[1] == 4) ++twelve;
    else if (sequence.grid.groupSize[0] == 1 || sequence.grid.groupSize[1] == 1) ++oneDim;
    else ++other;
  }
  // Always present, even for a binding that decodes to no grid at all: a gate
  // that counts zero grids needs a line to count.
  {
    char line[768];
    std::snprintf(line, sizeof(line),
      "GRID-SUMMARY model=%s sequences=%zu multi=%zu nine=%zu twelve=%zu oneDim=%zu other=%zu"
      " nbMismatch=%zu cellsBad=%zu gridsValid=%zu weights=%zu poseParams=%zu",
      binding.modelPath.c_str(), model.sequences.size(), multi, nine, twelve, oneDim, other,
      nbMismatch, cellsBad, gridsValid, weightReadable, model.poseParameters.size());
    out.emplace_back(line);
  }

  // GRID-CORNERS: the cell matrix in the engine's flattening order, one row per
  // line-segment, so the names can be checked against the poses the file ships
  // (a 3x3 move grid reads SW,S,SE | W,Center,E | NW,N,NE). One line per distinct
  // grid shape: a class carries one move family, one aim family, a few 1D ones.
  std::vector<std::string> signatures;
  for (std::size_t i = 0; i < model.sequences.size() && signatures.size() < 3; ++i) {
    const auto& sequence = model.sequences[i];
    if (sequence.blendCount <= 1 || !sequence.grid.valid) continue;
    char signature[160];
    std::snprintf(signature, sizeof(signature), "%dx%d:%d:%d:%.3f:%.3f:%.3f:%.3f",
      static_cast<int>(sequence.grid.groupSize[0]), static_cast<int>(sequence.grid.groupSize[1]),
      static_cast<int>(sequence.grid.paramIndex[0]), static_cast<int>(sequence.grid.paramIndex[1]),
      static_cast<double>(sequence.grid.paramStart[0]), static_cast<double>(sequence.grid.paramStart[1]),
      static_cast<double>(sequence.grid.paramEnd[0]), static_cast<double>(sequence.grid.paramEnd[1]));
    if (std::find(signatures.begin(), signatures.end(), std::string(signature)) != signatures.end()) continue;
    signatures.emplace_back(signature);
    const int columns = static_cast<int>(sequence.grid.groupSize[0]);
    const int rows = static_cast<int>(sequence.grid.groupSize[1]);
    std::string cellRows;
    bool complete = true;
    int printed = 0;
    for (int row = 0; row < rows && printed < 24; ++row) {
      if (row != 0) cellRows += '|';
      for (int column = 0; column < columns && printed < 24; ++column) {
        if (column != 0) cellRows += ',';
        const std::size_t cellIndex = static_cast<std::size_t>(row) * static_cast<std::size_t>(columns)
          + static_cast<std::size_t>(column);
        if (cellIndex >= sequence.grid.cells.size()) {
          cellRows += '-';
          complete = false;
          continue;
        }
        const std::string name = cellName(sequence.grid.cells[cellIndex]);
        if (name == "-") complete = false;
        cellRows += name;
        ++printed;
      }
    }
    char line[2048];
    std::snprintf(line, sizeof(line),
      "GRID-CORNERS model=%s seq=%zu label=%s cells=%dx%d param0=%d pos0=%.2f..%.2f"
      " param1=%d pos1=%.2f..%.2f pass=%d rows=%s",
      binding.modelPath.c_str(), i, sequence.label.c_str(), columns, rows,
      static_cast<int>(sequence.grid.paramIndex[0]),
      static_cast<double>(sequence.grid.paramStart[0]), static_cast<double>(sequence.grid.paramEnd[0]),
      static_cast<int>(sequence.grid.paramIndex[1]),
      static_cast<double>(sequence.grid.paramStart[1]), static_cast<double>(sequence.grid.paramEnd[1]),
      complete ? 1 : 0, cellRows.c_str());
    out.emplace_back(line);
  }

  // GRID-FRAMES: the cell frame counts and the cell at the grid centre. Step 2
  // samples every cell at the same cycle, so cells whose clips have different
  // lengths (swim swings between 31 and 61 frames) are the cases its phase rule
  // has to survive; a uniform column means the naive fix is enough there.
  std::size_t uniform = 0;
  std::size_t varying = 0;
  std::size_t centerFlat = 0;
  std::size_t centerMissing = 0;
  std::size_t framesShown = 0;
  for (std::size_t i = 0; i < model.sequences.size(); ++i) {
    const auto& sequence = model.sequences[i];
    if (sequence.blendCount <= 1 || sequence.grid.cells.empty()) continue;
    std::string frames;
    std::int32_t first = -1;
    bool isUniform = true;
    for (std::size_t cell = 0; cell < sequence.grid.cells.size(); ++cell) {
      const std::int32_t frameCount = cellFrames(sequence.grid.cells[cell]);
      if (cell == 0) first = frameCount;
      else if (frameCount != first) isUniform = false;
      if (!frames.empty()) frames += ',';
      frames += std::to_string(frameCount);
    }
    if (isUniform) ++uniform;
    else ++varying;
    const int columns = sequence.grid.groupSize[0] > 0 ? static_cast<int>(sequence.grid.groupSize[0]) : 1;
    const int rows = sequence.grid.groupSize[1] > 0 ? static_cast<int>(sequence.grid.groupSize[1]) : 1;
    const std::size_t centerIndex = static_cast<std::size_t>(rows / 2) * static_cast<std::size_t>(columns)
      + static_cast<std::size_t>(columns / 2);
    const std::int32_t center = centerIndex < sequence.grid.cells.size()
      ? cellFrames(sequence.grid.cells[centerIndex]) : -1;
    if (center < 0) ++centerMissing;
    else if (center == first) ++centerFlat;
    if (framesShown < 8 && (framesShown == 0 || !isUniform)) {
      ++framesShown;
      char line[1024];
      std::snprintf(line, sizeof(line), "GRID-FRAMES seq=%zu label=%s frames=%s uniform=%d center=%d",
        i, sequence.label.c_str(), frames.c_str(), isUniform ? 1 : 0, static_cast<int>(center));
      out.emplace_back(line);
    }
  }
  {
    char line[512];
    std::snprintf(line, sizeof(line),
      "GRID-FRAMES-SUMMARY model=%s grids=%zu uniform=%zu varying=%zu centerFlat=%zu centerMissing=%zu",
      binding.modelPath.c_str(), uniform + varying, uniform, varying, centerFlat, centerMissing);
    out.emplace_back(line);
  }

  // WEIGHTLIST: the per-bone mask behind each grid. All-ones is the neutral case
  // and all-zero is the override/placeholder case, so only the mixed patterns are
  // printed; a mixed pattern is what Step 3 has to respect bone by bone.
  std::size_t allOnes = 0;
  std::size_t allZero = 0;
  std::size_t partial = 0;
  std::size_t unreadable = 0;
  std::size_t weightShown = 0;
  for (std::size_t i = 0; i < model.sequences.size(); ++i) {
    const auto& sequence = model.sequences[i];
    if (sequence.blendCount <= 1) continue;
    const AnimationWeightList& list = sequence.weights;
    if (!list.readable || list.boneCount == 0) {
      ++unreadable;
      continue;
    }
    const bool ones = list.zeroCount == 0 && list.minWeight == 1.0f && list.maxWeight == 1.0f
      && list.sum == static_cast<float>(list.boneCount);
    const bool zeros = list.zeroCount == list.boneCount;
    if (ones) ++allOnes;
    else if (zeros) ++allZero;
    else ++partial;
    if (!ones && weightShown < 8) {
      ++weightShown;
      char line[512];
      std::snprintf(line, sizeof(line),
        "WEIGHTLIST seq=%zu label=%s bones=%zu zero=%zu sum=%.3f min=%.3f max=%.3f",
        i, sequence.label.c_str(), list.boneCount, list.zeroCount,
        static_cast<double>(list.sum), static_cast<double>(list.minWeight), static_cast<double>(list.maxWeight));
      out.emplace_back(line);
    }
  }
  {
    char line[512];
    std::snprintf(line, sizeof(line),
      "WEIGHTLIST-SUMMARY model=%s grids=%zu allOnes=%zu allZero=%zu partial=%zu unreadable=%zu",
      binding.modelPath.c_str(), multi, allOnes, allZero, partial, unreadable);
    out.emplace_back(line);
  }

  // GRID-POSE-PARAMS: the slots the grid axes name, in slot order. The slot that
  // carries move_x differs by class ((4,5) vs (5,4)), so a reader that hardcodes
  // a slot index is wrong for half the roster; this line is the evidence.
  {
    std::string slots;
    for (std::size_t i = 0; i < model.poseParameters.size() && i < 8; ++i) {
      const AnimationPoseParam& param = model.poseParameters[i];
      if (!slots.empty()) slots += ' ';
      slots += std::to_string(i);
      slots += ':';
      slots += param.name.empty() ? std::string("-") : param.name;
      char range[96];
      std::snprintf(range, sizeof(range), "[%.1f..%.1f]",
        static_cast<double>(param.start), static_cast<double>(param.end));
      slots += range;
    }
    char line[1024];
    std::snprintf(line, sizeof(line), "GRID-POSE-PARAMS model=%s slots=%zu%s%s",
      binding.modelPath.c_str(), model.poseParameters.size(),
      slots.empty() ? "" : " ", slots.c_str());
    out.emplace_back(line);
  }

  // GRID-DELTA / GRID-GATE (Step 3): the two facts that decide whether per-bone
  // gating can do anything at all -- do grid cells carry SEQUENCE_DELTA channels,
  // and does a grid weight_list actually disagree with the bones its cells animate.
  // Every cell is walked once here through the same channel census the probe uses.
  std::size_t deltaCells = 0;
  std::size_t deltaCellsWithDelta = 0;
  std::size_t deltaChannels = 0;
  std::size_t deltaCellFailures = 0;
  std::size_t deltaShown = 0;
  std::size_t gatePartial = 0;
  std::size_t gateAllZero = 0;
  std::size_t gateShown = 0;
  for (std::size_t i = 0; i < model.sequences.size(); ++i) {
    const auto& sequence = model.sequences[i];
    if (!sequence.grid.valid) continue;
    std::vector<std::uint8_t> animated(model.bones.size(), 0);
    std::size_t seqCells = 0;
    std::size_t seqDeltaCells = 0;
    std::size_t seqDeltaChannels = 0;
    std::size_t seqFailures = 0;
    for (std::size_t k = 0; k < sequence.grid.cells.size(); ++k) {
      const std::int32_t cell = sequence.grid.cells[k];
      if (cell < 0 || static_cast<std::size_t>(cell) >= model.animations.size()) {
        ++seqFailures;
        continue;
      }
      AnimationChannelCensus channels;
      if (!censusAnimationChannels(model, cell, channels) || !channels.ok) {
        ++seqFailures;
        continue;
      }
      ++seqCells;
      if (channels.deltaBones > 0) {
        ++seqDeltaCells;
        seqDeltaChannels += channels.deltaBones;
      }
      for (std::size_t bone = 0; bone < animated.size() && bone < channels.boneAnimated.size(); ++bone) {
        if (channels.boneAnimated[bone] != 0) animated[bone] = 1;
      }
    }
    deltaCells += seqCells;
    deltaCellsWithDelta += seqDeltaCells;
    deltaChannels += seqDeltaChannels;
    deltaCellFailures += seqFailures;
    if (seqDeltaCells > 0 && deltaShown < 8) {
      ++deltaShown;
      char line[512];
      std::snprintf(line, sizeof(line),
        "GRID-DELTA seq=%zu label=%s cells=%zu cellsWithDelta=%zu deltaChannels=%zu failures=%zu",
        i, sequence.label.c_str(), seqCells, seqDeltaCells, seqDeltaChannels, seqFailures);
      out.emplace_back(line);
    }

    const AnimationWeightList& list = sequence.weights;
    if (!list.readable || list.boneCount == 0) continue;
    const bool ones = list.zeroCount == 0 && list.minWeight == 1.0f && list.maxWeight == 1.0f
      && list.sum == static_cast<float>(list.boneCount);
    if (ones) continue;
    const bool zeros = list.zeroCount == list.boneCount;
    if (zeros) ++gateAllZero;
    else ++gatePartial;
    if (gateShown >= 8) continue;
    ++gateShown;
    std::size_t supplied = 0;
    std::size_t animatedCount = 0;
    std::size_t both = 0;
    std::string ranges;
    std::size_t rangeRun = 0;
    for (std::size_t bone = 0; bone < animated.size(); ++bone) {
      const bool anim = animated[bone] != 0;
      const bool sup = poseWeightListGatesBone(list, bone);
      if (sup) ++supplied;
      if (anim) ++animatedCount;
      if (sup && anim) ++both;
      if (sup == anim) {
        if (rangeRun > 0) {
          if (!ranges.empty()) ranges += ',';
          ranges += std::to_string(bone - rangeRun);
          if (rangeRun > 1) {
            ranges += '-';
            ranges += std::to_string(bone - 1);
          }
          rangeRun = 0;
        }
      } else {
        ++rangeRun;
      }
    }
    if (rangeRun > 0) {
      if (!ranges.empty()) ranges += ',';
      ranges += std::to_string(animated.size() - rangeRun);
      if (rangeRun > 1) {
        ranges += '-';
        ranges += std::to_string(animated.size() - 1);
      }
    }
    char line[1024];
    std::snprintf(line, sizeof(line),
      "GRID-GATE seq=%zu label=%s bones=%zu supplied=%zu animated=%zu both=%zu disagree=%s",
      i, sequence.label.c_str(), animated.size(), supplied, animatedCount, both,
      ranges.empty() ? "-" : ranges.c_str());
    out.emplace_back(line);
  }
  {
    char line[512];
    std::snprintf(line, sizeof(line),
      "GRID-DELTA-SUMMARY model=%s cells=%zu cellsWithDelta=%zu deltaChannels=%zu cellFailures=%zu",
      binding.modelPath.c_str(), deltaCells, deltaCellsWithDelta, deltaChannels, deltaCellFailures);
    out.emplace_back(line);
  }
  {
    char line[512];
    std::snprintf(line, sizeof(line),
      "GRID-GATE-SUMMARY model=%s grids=%zu partial=%zu allZero=%zu",
      binding.modelPath.c_str(), multi, gatePartial, gateAllZero);
    out.emplace_back(line);
  }
}

} // namespace tf2::native
