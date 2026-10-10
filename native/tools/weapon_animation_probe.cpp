// Proves the weapon / projectile / wearable animation path against a REAL demo,
// link by link, and makes every link able to fail.
//
// Why this probe exists. The skinning contract itself was already proven
// (skeleton_skin_probe, `00b6033`): poseToBone is the inverse bind matrix, the
// composition order is row-major with `skin = animWorld * poseToBone`, and the
// matrices are rigid. What that probe never touched is the half that comes
// *before* the matrix: nothing in the tree has ever read an entity's own
// `m_nSequence` out of a demo and turned it into a pose. `main.cpp` uploads
// `poseToBone` with skinning disabled, and animation_decoder.cpp is not even
// compiled into the product target.
//
// So this probe answers, in order, with a reading for each answer:
//
//   1. INVENTORY. Which weapon / projectile / wearable models does a real demo
//      actually reference, and do those MDLs carry animation data at all? A
//      weapon world model in TF2 is often static; if so that is a fact to
//      report, not a failure to hide. The probe separates "has no animations"
//      from "has animations but no sequence reached it".
//   2. SEQUENCE VALUES. For every such entity, read `m_nSequence` at several
//      ticks. A sequence index outside the model's own table is a failure.
//   3. THE MATRICES. Sample the sequence at the tick, compose the parent chain,
//      multiply by poseToBone, and check the three properties that would each
//      catch a different wiring mistake:
//        bind pose error   -- at the bind pose the skin matrix must be identity
//        rigidity          -- the rotation part must stay orthonormal, det +1
//        motion            -- two ticks must not produce the same pose
//      plus the discriminating reading: the skinned position of a bone must
//      land on that bone's animated position, and the reversed multiply order
//      must miss it by a lot.
//   4. NEGATIVES. A sequence index past the table, a non-earlier parent bone,
//      and a non-rigid matrix must all be rejected, with a reason, and must not
//      report a pose.
//
// WHAT THIS DOES NOT PROVE. It does not prove the picture on screen is right.
// Nothing here opens a window or reads a pixel. It proves that the matrices the
// product would upload are the ones the contract requires, on real demo input.
//
// Mutations (`--mutation <name>`) each move one named reading, so a gate can
// require the flip to be visible rather than trusting the green:
//   order     swap the multiply order   -> skinLands blows up
//   bindpose  ignore the sampled pose   -> motionDelta collapses
#include "animation_decoder.h"
#include "asset_root.h"
#include "demo_header.h"
#include "model_loader.h"
#include "vpk_archive.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

using tf2::native::AnimationModel;
using tf2::native::AnimationStatus;
using tf2::native::DemoIndex;
using tf2::native::DemoHeader;
using tf2::native::DemoNetworkSummary;
using tf2::native::EntityPropertyValue;
using tf2::native::EntityState;
using tf2::native::ModelMetadata;

constexpr float kBindPoseTolerance = 1.0e-3f;
constexpr float kRigidityTolerance = 1.0e-3f;
constexpr float kSkinLandsTolerance = 1.0e-2f;
constexpr float kReversedMustMiss = 1.0f;
constexpr float kMotionFloor = 1.0e-5f;
constexpr std::size_t kMaxUploadBones = 128;
// Demo recordings are 30 Hz; the tick rate is what turns a tick into an
// animation cursor. Passed explicitly rather than derived, so the mapping is a
// stated decision and not an accident of where the number came from.
constexpr float kDemoTickRate = 30.0f;

// ---------------------------------------------------------------------------
// 3x4 row-major transforms with the column-vector convention (v' = M * v),
// matching the engine's ConcatTransforms. Duplicated from skeleton_skin_probe on
// purpose: a probe that shares its matrix helper with the thing it checks cannot
// notice a change in that helper.
// ---------------------------------------------------------------------------
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

std::array<float, 12> invertRigid(const std::array<float, 12>& m) {
  std::array<float, 12> out{};
  const float t[3] = {m[3], m[7], m[11]};
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 3; ++c) out[r * 4 + c] = m[c * 4 + r];
    out[r * 4 + 3] = -(out[r * 4 + 0] * t[0] + out[r * 4 + 1] * t[1] + out[r * 4 + 2] * t[2]);
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

std::array<float, 16> toColumnMajor(const std::array<float, 12>& m) {
  return {
    m[0], m[4], m[8], 0.0f,
    m[1], m[5], m[9], 0.0f,
    m[2], m[6], m[10], 0.0f,
    m[3], m[7], m[11], 1.0f
  };
}

// How far the rotation block of a 3x4 is from orthonormal with determinant +1.
// A transposed or scaled rotation leaves a matrix that still looks like a
// transform, so this is the reading that catches it without a screenshot.
float rigidityError(const std::array<float, 12>& m) {
  float worst = 0.0f;
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 3; ++c) {
      float dot = 0.0f;
      for (int k = 0; k < 3; ++k) dot += m[r * 4 + k] * m[c * 4 + k];
      worst = std::max(worst, std::fabs(dot - (r == c ? 1.0f : 0.0f)));
    }
  }
  const float det =
      m[0] * (m[5] * m[10] - m[6] * m[9])
    - m[1] * (m[4] * m[10] - m[6] * m[8])
    + m[2] * (m[4] * m[9] - m[5] * m[8]);
  return std::max(worst, std::fabs(det - 1.0f));
}

// ---------------------------------------------------------------------------
// Property lookup. entity_model.cpp's findProperty is file-local, so this is a
// second implementation -- deliberately, so a change to the resolver's slot
// preference cannot silently change what this probe measured.
// ---------------------------------------------------------------------------
const EntityPropertyValue* findProp(const EntityState& state, const char* suffix) {
  const std::size_t length = std::strlen(suffix);
  const EntityPropertyValue* best = nullptr;
  std::int32_t bestTick = -1;
  for (const auto& entry : state.properties) {
    const std::string& name = entry.first;
    if (name.size() < length) continue;
    if (name.compare(name.size() - length, length, suffix) != 0) continue;
    if (name.size() > length && name[name.size() - length - 1] != '.') continue;
    // Same freshness rule the resolver uses: a later-written slot wins. Without
    // it a weapon carrying m_nSequence twice could be read from the stale copy.
    if (best == nullptr || entry.second.lastWriteTick > bestTick) {
      best = &entry.second;
      bestTick = entry.second.lastWriteTick;
    }
  }
  return best;
}

bool readIntProp(const EntityState& state, const char* suffix, std::int64_t& out) {
  const auto* value = findProp(state, suffix);
  if (value == nullptr) return false;
  out = value->type == tf2::native::SendPropType::Float
    ? static_cast<std::int64_t>(value->x) : value->intValue;
  return true;
}

bool isAnimatedClass(const std::string& className) {
  static const char* kNeedles[] = {"Weapon", "Projectile", "Wearable", "Grenade"};
  for (const char* needle : kNeedles) {
    if (className.find(needle) != std::string::npos) return true;
  }
  return false;
}

// A weapon world model carries its own animations; a player model does not and
// names a `_animations` companion instead. Both routes are tried, in that order,
// because guessing wrong produces a model that decodes perfectly and animates
// nothing.
std::string companionAnimPath(const std::string& modelPath) {
  if (modelPath.size() < 4) return {};
  const std::string stem = modelPath.substr(0, modelPath.size() - 4);
  const std::size_t slash = stem.rfind('/');
  const std::string directory = slash == std::string::npos ? std::string() : stem.substr(0, slash + 1);
  const std::string base = slash == std::string::npos ? stem : stem.substr(slash + 1);
  return directory + base + "_animations.mdl";
}

// ---------------------------------------------------------------------------
// Per-model animation inventory. One entry per distinct model path the demo
// references from an animated class.
// ---------------------------------------------------------------------------
struct ModelInventory {
  std::string modelPath;
  std::string animPath;
  bool modelDecoded = false;
  bool animDecoded = false;
  bool animIsSeparate = false;
  bool loaded = false;
  std::size_t modelBones = 0;
  std::size_t animBones = 0;
  std::size_t mappedBones = 0;
  std::size_t animations = 0;
  std::size_t sequences = 0;
  std::vector<std::int32_t> animToModel;
  std::string reason;
};

bool mapAnimBonesToModel(const AnimationModel& anims, const ModelMetadata& metadata,
    std::vector<std::int32_t>& animToModel, std::string& reason) {
  animToModel.assign(anims.bones.size(), -1);
  std::size_t mapped = 0;
  for (std::size_t i = 0; i < anims.bones.size(); ++i) {
    for (std::size_t j = 0; j < metadata.bones.size(); ++j) {
      if (metadata.bones[j].name != anims.bones[i].name) continue;
      if (metadata.bones[j].parent != anims.bones[i].parent) continue;
      animToModel[i] = static_cast<std::int32_t>(j);
      ++mapped;
      break;
    }
  }
  if (mapped != anims.bones.size()) {
    reason = "animation bone did not line up with the model skeleton";
    return false;
  }
  reason.clear();
  return true;
}

// True when the animation model decodes and its bones all land on the model
// skeleton. `staticModel` is set when the model simply carries no animation --
// a reportable state, not a failure.
bool loadInventory(const tf2::native::VpkArchive& archive, const std::string& modelPath,
    ModelInventory& out, bool& staticModel) {
  out = ModelInventory{};
  out.modelPath = modelPath;
  staticModel = false;
  std::string error;
  const auto modelBytes = archive.read(modelPath, &error);
  if (modelBytes.empty()) {
    out.reason = "model file could not be read from the archive";
    return false;
  }
  AnimationModel model;
  if (tf2::native::decodeAnimationModel(modelBytes.data(), modelBytes.size(), model) != AnimationStatus::Ok) {
    out.reason = "model animation header did not decode: " + model.reason;
    return false;
  }
  out.modelDecoded = true;
  out.modelBones = model.bones.size();

  const auto inspection = tf2::native::ModelLoader::inspectVpk(archive, modelPath);
  if (!inspection.metadata.valid || inspection.metadata.bones.empty()) {
    out.reason = "model loader could not read the skeleton";
    return false;
  }

  AnimationModel anims;
  std::string animPath = modelPath;
  if (tf2::native::decodeAnimationModel(modelBytes.data(), modelBytes.size(), anims) != AnimationStatus::Ok
      || anims.animations.empty()) {
    const std::string companion = companionAnimPath(modelPath);
    if (!companion.empty()) {
      const auto companionBytes = archive.read(companion, &error);
      if (!companionBytes.empty()) {
        AnimationModel candidate;
        if (tf2::native::decodeAnimationModel(companionBytes.data(), companionBytes.size(), candidate)
            == AnimationStatus::Ok && !candidate.animations.empty()) {
          anims = std::move(candidate);
          animPath = companion;
          out.animIsSeparate = true;
        }
      }
    }
  }
  out.animPath = animPath;
  if (anims.animations.empty()) {
    out.reason = "model carries no animation data";
    out.animations = 0;
    out.sequences = anims.sequences.size();
    out.animBones = anims.bones.size();
    out.loaded = true;
    staticModel = true;
    return true;
  }
  out.animDecoded = true;
  out.animBones = anims.bones.size();
  out.animations = anims.animations.size();
  out.sequences = anims.sequences.size();
  std::string mapReason;
  if (!mapAnimBonesToModel(anims, inspection.metadata, out.animToModel, mapReason)) {
    out.reason = mapReason;
    return false;
  }
  out.mappedBones = anims.bones.size();
  out.reason.clear();
  out.loaded = true;
  return true;
}

// ---------------------------------------------------------------------------
// Self-tests. These need no demo, so a missing demo can never turn a broken
// composition into a SKIP.
// ---------------------------------------------------------------------------
bool selfTestCompose() {
  const float h = std::sqrt(0.5f);
  const std::vector<std::int32_t> parents{-1, 0};
  const std::vector<std::array<float, 3>> pos{{{0, 0, 0}, {1, 0, 0}}};
  const std::vector<std::array<float, 4>> identity{{{0, 0, 0, 1}, {0, 0, 0, 1}}};
  std::vector<std::array<float, 4>> turned = identity;
  turned[0] = {0, 0, h, h};
  std::vector<std::array<float, 3>> rest, moved;
  std::string reason;
  if (!tf2::native::composeLocalToModel(parents, pos, identity, rest, reason)) return false;
  if (!tf2::native::composeLocalToModel(parents, pos, turned, moved, reason)) return false;
  return std::fabs(rest[1][0] - 1.0f) < 1e-4f
      && std::fabs(moved[1][0]) < 1e-4f
      && std::fabs(moved[1][1] - 1.0f) < 1e-4f;
}

// A parent index that is not an earlier bone is not a skeleton. The decoder's
// own check must reject it rather than composing a cycle.
bool selfTestBadParent() {
  const std::vector<std::int32_t> parents{-1, 3};
  const std::vector<std::array<float, 3>> pos(2);
  const std::vector<std::array<float, 4>> rot(2, {0, 0, 0, 1});
  std::vector<std::array<float, 3>> out;
  std::string reason;
  return !tf2::native::composeLocalToModel(parents, pos, rot, out, reason) && !reason.empty();
}

// Sequence past the table must be refused with a reason, not clamped silently.
bool selfTestSequenceOutOfRange(const AnimationModel& model, std::string& detail) {
  const auto sample = tf2::native::sampleAnimation(model, 100000, 0, kDemoTickRate);
  detail = sample.reason;
  return sample.status == AnimationStatus::Failed && !sample.reason.empty();
}

// Rigidity must be able to fail: scale a rotation block and require the reading
// to move. A check that cannot go red is not a check.
bool selfTestRigidity() {
  std::array<float, 12> m{};
  m[0] = m[5] = m[10] = 1.0f;
  const float clean = rigidityError(m);
  auto scaled = m;
  scaled[0] = 2.0f;
  const float dirty = rigidityError(scaled);
  return clean < kRigidityTolerance && dirty > kRigidityTolerance;
}

enum class Mutation { None, Order, BindPose };

struct SamplePoint {
  std::int32_t tick = 0;
  int frame = 0;
  std::vector<std::array<float, 3>> localPosition;
  std::vector<std::array<float, 4>> localRotation;
};

} // namespace

int main(int argc, char** argv) {
  std::string demoPath;
  std::string tfRoot;
  std::string mutationName;
  std::size_t tickCount = 6;
  bool diagnose = false;
  bool selfTestOnly = false;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--demo" && i + 1 < argc) demoPath = argv[++i];
    else if (arg == "--tf-root" && i + 1 < argc) tfRoot = argv[++i];
    else if (arg == "--ticks" && i + 1 < argc) tickCount = static_cast<std::size_t>(std::stoul(argv[++i]));
    else if (arg == "--mutation" && i + 1 < argc) mutationName = argv[++i];
    else if (arg == "--diagnose") diagnose = true;
    else if (arg == "--self-test") selfTestOnly = true;
    else {
      std::cerr << "usage: weapon_animation_probe [--demo <path>] [--tf-root <path>] [--ticks N]"
                   " [--mutation order|bindpose] [--diagnose] [--self-test]\n";
      return 2;
    }
  }

  Mutation mutation = Mutation::None;
  if (mutationName == "order") mutation = Mutation::Order;
  else if (mutationName == "bindpose") mutation = Mutation::BindPose;
  else if (!mutationName.empty()) {
    std::cerr << "unknown mutation: " << mutationName << "\n";
    return 2;
  }

  const bool composeOk = selfTestCompose();
  const bool badParentOk = selfTestBadParent();
  const bool rigidityOk = selfTestRigidity();
  const std::size_t selfTestsPassed = static_cast<std::size_t>(composeOk)
    + static_cast<std::size_t>(badParentOk) + static_cast<std::size_t>(rigidityOk);
  const std::size_t selfTestsTotal = 3;

  if (selfTestOnly) {
    std::cout << "{\"status\":\"" << (selfTestsPassed == selfTestsTotal ? "ok" : "failed") << "\""
              << ",\"selfTests\":" << selfTestsPassed
              << ",\"selfTestsTotal\":" << selfTestsTotal
              << ",\"compose\":" << (composeOk ? "true" : "false")
              << ",\"badParent\":" << (badParentOk ? "true" : "false")
              << ",\"rigidity\":" << (rigidityOk ? "true" : "false") << "}\n";
    return selfTestsPassed == selfTestsTotal ? 0 : 1;
  }

  const char* envRoot = std::getenv("TF_ROOT");
  if (tfRoot.empty()) tfRoot = envRoot ? envRoot : "D:/SteamLibrary/steamapps/common/Team Fortress 2/tf";

  tf2::native::VpkArchive archive;
  std::string archiveError;
  if (!archive.open(std::filesystem::path(tfRoot) / "tf2_misc_dir.vpk", &archiveError)) {
    std::cout << "{\"status\":\"failed\",\"reason\":\"cannot open tf2_misc_dir.vpk: "
              << archiveError << "\"}\n";
    return 1;
  }

  // ---- the one self-test that needs a real file ---------------------------
  bool outOfRangeOk = false;
  std::string outOfRangeDetail;
  {
    AnimationModel scout;
    std::string readError;
    const auto bytes = archive.read("models/player/scout_animations.mdl", &readError);
    if (tf2::native::decodeAnimationModel(bytes.data(), bytes.size(), scout) == AnimationStatus::Ok) {
      outOfRangeOk = selfTestSequenceOutOfRange(scout, outOfRangeDetail);
    }
  }

  if (demoPath.empty()) {
    const bool ok = composeOk && badParentOk && rigidityOk && outOfRangeOk;
    std::cout << "{\"status\":\"" << (ok ? "ok" : "failed") << "\""
              << ",\"mode\":\"self-test\""
              << ",\"selfTests\":" << selfTestsPassed << ",\"selfTestsTotal\":" << selfTestsTotal
              << ",\"sequenceOutOfRange\":" << (outOfRangeOk ? "true" : "false")
              << ",\"outOfRangeReason\":\"" << outOfRangeDetail << "\"}\n";
    return ok ? 0 : 1;
  }

  // ---- real demo ----------------------------------------------------------
  DemoHeader header;
  DemoIndex index;
  DemoNetworkSummary summary;
  if (!tf2::native::parseDemoHeaderFile(demoPath, header)) {
    std::cout << "{\"status\":\"failed\",\"reason\":\"demo header parse failed\"}\n";
    return 1;
  }
  tf2::native::indexDemoFile(demoPath, header, index);
  summary.networkProtocol = header.networkProtocol;
  if (!tf2::native::scanKnownDemoMessages(demoPath, index, summary)) {
    std::cout << "{\"status\":\"failed\",\"reason\":\"demo scan failed\"}\n";
    return 1;
  }
  tf2::native::buildAssetReferenceList(summary, summary.assetReferences);

  // entity -> the model path the renderer would use for it.
  std::map<std::uint16_t, std::string> entityModel;
  std::map<std::uint16_t, std::string> entityClass;
  for (const auto& reference : summary.assetReferences) {
    if (!reference.hasModelPath || reference.modelPath.empty()) continue;
    entityClass[reference.entityIndex] = reference.className;
    if (!isAnimatedClass(reference.className)) continue;
    // A weapon's m_nModelIndex names its class's first-person arms composite;
    // m_iWorldModelIndex names the weapon itself. The animation belongs to the
    // weapon, so prefer the world path when the demo carries one.
    if (reference.modelPathFromWorldModelIndex) {
      entityModel[reference.entityIndex] = reference.modelPath;
    } else if (entityModel.find(reference.entityIndex) == entityModel.end()) {
      entityModel[reference.entityIndex] = reference.modelPath;
    }
  }

  std::int32_t firstTick = 0;
  std::int32_t lastTick = 0;
  if (!summary.entityHistoryPackets.empty()) {
    firstTick = summary.entityHistoryPackets.front().tick;
    lastTick = summary.entityHistoryPackets.back().tick;
  }
  if (lastTick <= firstTick) {
    std::cout << "{\"status\":\"failed\",\"reason\":\"demo carries no usable entity history\","
              << "\"packets\":" << summary.entityHistoryPackets.size() << "}\n";
    return 1;
  }
  if (tickCount < 2) tickCount = 2;

  std::set<std::string> distinctModels;
  std::map<std::string, ModelInventory> inventories;
  std::map<std::string, std::int64_t> firstSequence;
  std::map<std::string, std::int64_t> lastSequence;
  std::map<std::string, SamplePoint> firstPoint;
  std::map<std::string, SamplePoint> lastPoint;
  std::map<std::string, std::string> entityModelByKey;
  std::map<std::string, std::string> entityClassByKey;

  std::size_t snapshots = 0;
  std::size_t entitiesScanned = 0;
  std::size_t sequenceReads = 0;
  std::size_t sequencesOutOfRange = 0;
  std::size_t sampledFrames = 0;
  std::size_t boneMatricesBuilt = 0;
  std::size_t validBindBones = 0;
  std::size_t skinOrderChecks = 0;
  std::size_t modelsWithAnimations = 0;
  std::size_t modelsWithoutAnimations = 0;
  std::size_t loadFailures = 0;
  float bindPoseWorst = 0.0f;
  float rigidityWorst = 0.0f;
  float skinLands = 0.0f;
  float skinReversed = 0.0f;
  std::string skinSequence;

  std::vector<EntityState> states;
  const std::int32_t span = lastTick - firstTick;
  for (std::size_t step = 0; step < tickCount; ++step) {
    const std::int32_t tick = firstTick + static_cast<std::int32_t>(
      (static_cast<long long>(span) * static_cast<long long>(step)) / static_cast<long long>(tickCount - 1));
    std::int32_t resolvedTick = tick;
    if (tf2::native::queryEntitySnapshotAtOrBeforeTick(summary, tick, states, &resolvedTick)
        != tf2::native::EntitySnapshotQueryStatus::Available) continue;
    ++snapshots;
    for (std::size_t entity = 0; entity < states.size(); ++entity) {
      const auto modelIt = entityModel.find(static_cast<std::uint16_t>(entity));
      if (modelIt == entityModel.end()) continue;
      const std::string& modelPath = modelIt->second;
      std::int64_t sequence = 0;
      if (!readIntProp(states[entity], "m_nSequence", sequence)) continue;
      ++entitiesScanned;
      ++sequenceReads;
      const std::string key = std::to_string(entity) + "@" + modelPath;
      distinctModels.insert(modelPath);
      entityModelByKey[key] = modelPath;
      entityClassByKey[key] = entityClass[static_cast<std::uint16_t>(entity)];
      if (firstSequence.find(key) == firstSequence.end()) firstSequence[key] = sequence;
      lastSequence[key] = sequence;

      auto invIt = inventories.find(modelPath);
      if (invIt == inventories.end()) {
        ModelInventory inventory;
        bool staticModel = false;
        const bool ok = loadInventory(archive, modelPath, inventory, staticModel);
        if (ok && staticModel) ++modelsWithoutAnimations;
        else if (ok) ++modelsWithAnimations;
        else ++loadFailures;
        invIt = inventories.emplace(modelPath, std::move(inventory)).first;
      }
      const ModelInventory& inventory = invIt->second;
      if (diagnose) {
        std::cerr << "wa-model path=" << modelPath
                  << " class=" << entityClassByKey[key]
                  << " animPath=" << inventory.animPath
                  << " modelBones=" << inventory.modelBones
                  << " animBones=" << inventory.animBones
                  << " mapped=" << inventory.mappedBones
                  << " animations=" << inventory.animations
                  << " sequences=" << inventory.sequences
                  << " reason=" << inventory.reason << "\n";
      }
      if (inventory.animations == 0 || inventory.mappedBones == 0) continue;

      AnimationModel anims;
      std::string readError;
      const auto animBytes = archive.read(inventory.animPath, &readError);
      if (tf2::native::decodeAnimationModel(animBytes.data(), animBytes.size(), anims) != AnimationStatus::Ok) continue;
      if (sequence < 0 || static_cast<std::size_t>(sequence) >= anims.sequences.size()) {
        ++sequencesOutOfRange;
        // Which entity, which class, which model, which sequence, and how big the
        // model's own table is. Without this the count is a number nobody can act
        // on: "48 out of range" is only meaningful once it is known whether the
        // model resolution picked the wrong model or the demo really does carry
        // sequences that name a table this model does not own.
        if (diagnose) {
          std::cerr << "wa-outofrange entity=" << entity
                    << " class=" << entityClassByKey[key]
                    << " sequence=" << sequence
                    << " sequences=" << anims.sequences.size()
                    << " model=" << modelPath << "\n";
        }
        continue;
      }
      if (anims.sequences[static_cast<std::size_t>(sequence)].animDesc < 0) continue;
      const auto& anim = anims.animations[static_cast<std::size_t>(
        anims.sequences[static_cast<std::size_t>(sequence)].animDesc)];
      if (!(anim.fps > 0.0f) || anim.frameCount <= 0) continue;

      // The sequence advances with the demo tick. fps/tickRate turns a tick into
      // an animation cursor, and selectFrame inside the decoder owns the loop
      // and the section boundary -- this probe does not re-implement either.
      const auto sample = tf2::native::sampleAnimation(anims, static_cast<std::int32_t>(sequence),
        resolvedTick, kDemoTickRate);
      if (sample.status != AnimationStatus::Ok) continue;
      ++sampledFrames;
      if (diagnose) {
        std::cerr << "wa-sample entity=" << entity
                  << " class=" << entityClassByKey[key]
                  << " sequence=" << sequence
                  << " sequences=" << anims.sequences.size()
                  << " frame=" << sample.frame
                  << " frameCount=" << anim.frameCount
                  << " model=" << modelPath << "\n";
      }

      SamplePoint point;
      point.tick = resolvedTick;
      point.frame = sample.frame;
      point.localPosition = sample.localPosition;
      point.localRotation = sample.localRotation;
      if (mutation == Mutation::BindPose) {
        // Mutation: pretend the sampled pose *is* the bind pose. Everything
        // downstream then reports what the wiring would look like if it ignored
        // m_nSequence entirely.
        for (std::size_t i = 0; i < point.localPosition.size() && i < anims.bones.size(); ++i) {
          point.localPosition[i] = anims.bones[i].position;
          point.localRotation[i] = anims.bones[i].rotation;
        }
      }
      if (firstPoint.find(key) == firstPoint.end()) firstPoint[key] = point;
      lastPoint[key] = point;

      const auto inspection = tf2::native::ModelLoader::inspectVpk(archive, modelPath);
      if (!inspection.metadata.valid) continue;

      // animWorld per animation bone.
      std::vector<std::array<float, 12>> animWorld(anims.bones.size());
      bool composed = true;
      for (std::size_t i = 0; i < anims.bones.size(); ++i) {
        const auto local = localTransform(point.localPosition[i], point.localRotation[i]);
        if (anims.bones[i].parent < 0) animWorld[i] = local;
        else if (static_cast<std::size_t>(anims.bones[i].parent) < i) {
          animWorld[i] = concatTransforms(animWorld[static_cast<std::size_t>(anims.bones[i].parent)], local);
        } else { composed = false; break; }
      }
      if (!composed) continue;

      // Bind-pose world, built from the model's own bone table. Only used to
      // report the identity reading; the animation path never consumes it.
      std::vector<std::array<float, 12>> modelBindWorld(inspection.metadata.bones.size());
      for (std::size_t i = 0; i < inspection.metadata.bones.size(); ++i) {
        const auto& bone = inspection.metadata.bones[i];
        const auto local = localTransform(bone.position, {0.0f, 0.0f, 0.0f, 1.0f});
        modelBindWorld[i] = bone.parent < 0
          ? local
          : concatTransforms(modelBindWorld[static_cast<std::size_t>(bone.parent)], local);
      }

      std::size_t matrices = 0;
      std::size_t probeBone = 0;
      bool probeChosen = false;
      for (std::size_t i = 0; i < inspection.metadata.bones.size(); ++i) {
        const auto& bone = inspection.metadata.bones[i];
        if (!bone.poseToBoneValid) continue;
        const std::array<float, 12>* world = nullptr;
        for (std::size_t a = 0; a < anims.bones.size(); ++a) {
          if (inventory.animToModel[a] != static_cast<std::int32_t>(i)) continue;
          world = &animWorld[a];
          break;
        }
        const std::array<float, 12>& animated = world == nullptr ? modelBindWorld[i] : *world;
        // Rigidity is measured on the 3x4 composition, before the column-major
        // lift: the upload layout is a pure reordering, so measuring it after the
        // lift would only measure the reordering.
        const auto composed = mutation == Mutation::Order
          ? concatTransforms(bone.poseToBone, animated)
          : concatTransforms(animated, bone.poseToBone);
        const auto skin = toColumnMajor(composed);
        ++matrices;
        if (matrices > kMaxUploadBones) break;
        rigidityWorst = std::max(rigidityWorst, rigidityError(composed));
        // The bind-pose identity reading is only defined where the pose *is* the
        // bind pose: an unanimated helper bone, or the bindpose mutation.
        if (world == nullptr || mutation == Mutation::BindPose) {
          static const float identity[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
          for (int j = 0; j < 16; ++j) {
            bindPoseWorst = std::max(bindPoseWorst, std::fabs(skin[j] - identity[j]));
          }
        }
        if (!probeChosen && world != nullptr && bone.parent >= 0) { probeBone = i; probeChosen = true; }
      }
      boneMatricesBuilt += matrices;
      validBindBones += matrices;

      // The discriminating reading: push the probe bone's bind-pose origin
      // through the full skinning path and require it to land on that bone's
      // animated origin. The reversed order must miss it by a lot.
      if (probeChosen) {
        const auto& bone = inspection.metadata.bones[probeBone];
        const auto bindWorld = invertRigid(bone.poseToBone);
        const float origin[3] = {bindWorld[3], bindWorld[7], bindWorld[11]};
        const std::array<float, 12>* world = nullptr;
        for (std::size_t a = 0; a < anims.bones.size(); ++a) {
          if (inventory.animToModel[a] == static_cast<std::int32_t>(probeBone)) { world = &animWorld[a]; break; }
        }
        if (world != nullptr) {
          // The contract, stated once: skin = animWorld * poseToBone. `--mutation
          // order` flips it here, which is why the flipped run reads a skinLands
          // that is orders of magnitude larger.
          const auto skinned = mutation == Mutation::Order
            ? concatTransforms(bone.poseToBone, *world)
            : concatTransforms(*world, bone.poseToBone);
          const float got[3] = {
            skinned[0] * origin[0] + skinned[1] * origin[1] + skinned[2] * origin[2] + skinned[3],
            skinned[4] * origin[0] + skinned[5] * origin[1] + skinned[6] * origin[2] + skinned[7],
            skinned[8] * origin[0] + skinned[9] * origin[1] + skinned[10] * origin[2] + skinned[11],
          };
          const float want[3] = {(*world)[3], (*world)[7], (*world)[11]};
          skinLands = std::max(skinLands, std::max({std::fabs(got[0] - want[0]),
            std::fabs(got[1] - want[1]), std::fabs(got[2] - want[2])}));
          ++skinOrderChecks;
          if (skinSequence.empty()) {
            skinSequence = anims.sequences[static_cast<std::size_t>(sequence)].label;
          }
          const auto reversed = concatTransforms(bone.poseToBone, *world);
          const float bad[3] = {
            reversed[0] * origin[0] + reversed[1] * origin[1] + reversed[2] * origin[2] + reversed[3],
            reversed[4] * origin[0] + reversed[5] * origin[1] + reversed[6] * origin[2] + reversed[7],
            reversed[8] * origin[0] + reversed[9] * origin[1] + reversed[10] * origin[2] + reversed[11],
          };
          skinReversed = std::max(skinReversed, std::max({std::fabs(bad[0] - want[0]),
            std::fabs(bad[1] - want[1]), std::fabs(bad[2] - want[2])}));
        }
      }
    }
  }

  // Motion: the same entity's pose at the first and the last tick it appeared
  // at. Rotation counts as much as position -- a spinning weapon or a projectile
  // that only turns would otherwise read as motionless, and "the pose advanced"
  // is the claim.
  float motionDelta = 0.0f;
  float motionRotationDelta = 0.0f;
  std::string motionEntity;
  std::size_t frameAdvanced = 0;
  std::size_t entitiesCompared = 0;
  for (const auto& entry : firstPoint) {
    const auto last = lastPoint.find(entry.first);
    if (last == lastPoint.end()) continue;
    if (entry.second.tick == last->second.tick) continue;
    ++entitiesCompared;
    if (last->second.frame > entry.second.frame) ++frameAdvanced;
    float delta = 0.0f;
    float rotationDelta = 0.0f;
    const std::size_t count = std::min(entry.second.localPosition.size(), last->second.localPosition.size());
    for (std::size_t i = 0; i < count; ++i) {
      for (int k = 0; k < 3; ++k) {
        delta = std::max(delta, std::fabs(last->second.localPosition[i][k] - entry.second.localPosition[i][k]));
      }
      for (int k = 0; k < 4; ++k) {
        rotationDelta = std::max(rotationDelta,
          std::fabs(last->second.localRotation[i][k] - entry.second.localRotation[i][k]));
      }
    }
    if (std::max(delta, rotationDelta) > std::max(motionDelta, motionRotationDelta)) {
      motionEntity = entry.first;
    }
    motionDelta = std::max(motionDelta, delta);
    motionRotationDelta = std::max(motionRotationDelta, rotationDelta);
  }
  const float motionWorst = std::max(motionDelta, motionRotationDelta);

  std::size_t changedEntities = 0;
  std::size_t staticEntities = 0;
  for (const auto& entry : firstSequence) {
    const auto last = lastSequence.find(entry.first);
    if (last != lastSequence.end() && last->second != entry.second) ++changedEntities;
    else ++staticEntities;
  }

  const bool skinLandsOk = skinLands <= kSkinLandsTolerance;
  const bool reversedMisses = skinReversed >= kReversedMustMiss;
  const bool motionOk = motionWorst > kMotionFloor;
  const bool bindPoseOk = bindPoseWorst <= kBindPoseTolerance;
  const bool rigidityOkReal = rigidityWorst <= kRigidityTolerance;
  const bool orderDiscriminated = skinOrderChecks > 0 && skinLandsOk && reversedMisses;

  if (mutation != Mutation::None) {
    // A mutation run reports the reading it was aimed at and exits 0 only when
    // the flip actually moved it. A gate that cannot see the flip measures nothing.
    const bool moved = mutation == Mutation::Order ? !skinLandsOk : !motionOk;
    std::cout << "{\"status\":\"" << (moved ? "ok" : "failed") << "\""
              << ",\"mutation\":\"" << mutationName << "\""
              << ",\"mutationMoved\":" << (moved ? "true" : "false")
              << ",\"skinLands\":" << skinLands
              << ",\"skinReversed\":" << skinReversed
              << ",\"motionDelta\":" << motionDelta
              << ",\"motionRotationDelta\":" << motionRotationDelta
              << ",\"bindPoseWorst\":" << bindPoseWorst
              << ",\"rigidityWorst\":" << rigidityWorst << "}\n";
    return moved ? 0 : 1;
  }

  std::size_t failed = 0;
  const auto require = [&](bool condition, const char* what) {
    if (!condition) { std::cerr << "weapon_animation_probe: " << what << "\n"; ++failed; }
  };
  require(composeOk, "parent chain composition");
  require(badParentOk, "a non-earlier parent must be rejected");
  require(rigidityOk, "the rigidity reading must be able to fail");
  require(outOfRangeOk, "a sequence past the table must be rejected");
  require(snapshots > 0, "no entity snapshot could be queried");
  require(sequenceReads > 0, "no weapon/projectile/wearable carried m_nSequence");
  require(sequencesOutOfRange == 0, "a demo sequence index was outside its model's table");
  require(sampledFrames > 0, "no sequence could be sampled");
  require(boneMatricesBuilt > 0, "no skin matrix was built");
  require(bindPoseOk, "a bind-pose skin matrix was not identity");
  require(rigidityOkReal, "a skin matrix was not rigid");
  require(orderDiscriminated, "the multiply order was not discriminated");
  require(modelsWithAnimations > 0, "no referenced model carried animation data");

  std::cout << "{\"status\":\"" << (failed == 0 ? "ok" : "failed") << "\""
            << ",\"demo\":\"" << demoPath << "\""
            << ",\"selfTests\":" << selfTestsPassed
            << ",\"selfTestsTotal\":" << selfTestsTotal
            << ",\"sequenceOutOfRange\":" << (outOfRangeOk ? "true" : "false")
            << ",\"snapshots\":" << snapshots
            << ",\"tickCount\":" << tickCount
            << ",\"tickFirst\":" << firstTick
            << ",\"tickLast\":" << lastTick
            << ",\"entitiesScanned\":" << entitiesScanned
            << ",\"sequenceReads\":" << sequenceReads
            << ",\"sequencesOutOfRange\":" << sequencesOutOfRange
            << ",\"distinctModels\":" << distinctModels.size()
            << ",\"modelsWithAnimations\":" << modelsWithAnimations
            << ",\"modelsWithoutAnimations\":" << modelsWithoutAnimations
            << ",\"modelLoadFailures\":" << loadFailures
            << ",\"sampledFrames\":" << sampledFrames
            << ",\"entitiesCompared\":" << entitiesCompared
            << ",\"frameAdvanced\":" << frameAdvanced
            << ",\"staticEntities\":" << staticEntities
            << ",\"changedEntities\":" << changedEntities
            << ",\"boneMatricesBuilt\":" << boneMatricesBuilt
            << ",\"validBindBones\":" << validBindBones
            << ",\"skinOrderChecks\":" << skinOrderChecks
            << ",\"bindPoseWorst\":" << bindPoseWorst
            << ",\"rigidityWorst\":" << rigidityWorst
            << ",\"motionDelta\":" << motionDelta
            << ",\"motionRotationDelta\":" << motionRotationDelta
            << ",\"motionWorst\":" << motionWorst
            << ",\"motionEntity\":\"" << motionEntity << "\""
            << ",\"skinLands\":" << skinLands
            << ",\"skinReversed\":" << skinReversed
            << ",\"skinSequence\":\"" << skinSequence << "\""
            << ",\"failures\":" << failed << "}\n";
  return failed == 0 ? 0 : 1;
}
