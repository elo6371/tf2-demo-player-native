// Proves the skeletal skinning path end to end, on this machine, from files
// that ship with the game.
//
// Why a separate probe: the product's fallback uploads `poseToBone` with
// skinning disabled, so nothing in the chain has ever checked that the matrix
// fed to the shader is the right one. This probe states the contract that
// matters and makes every part of it able to fail:
//
//   1. Convention, checked against the MDL's own redundancy. `poseToBone` is
//      documented as the inverse of bone-to-world, and the bone table carries
//      both the local (pos, quat) and that inverse. Composing the local
//      transforms gives a bone-to-world; inverting it must reproduce
//      `poseToBone` to float noise. Nothing below is trustworthy if this fails,
//      and it needs no screenshot and no engine to check.
//   2. Identity at bind pose. Sampling the animation at the bind pose must give
//      skin matrices within noise of identity, because
//      skin = animWorld * poseToBone and animWorld == bindWorld there. This is
//      the assertion that would catch a transposed multiply or a wrong
//      multiply order -- both of which leave a plausible-looking but wrong
//      matrix behind.
//   3. Motion. A real sequence at two different ticks must move at least one
//      bone's skinned position, or the wiring would be invisible in the one
//      place it is supposed to show.
//
// Output is one JSON line on stdout, mirroring the other probes, so a shell
// gate can assert on named fields.
#include "animation_decoder.h"
#include "asset_root.h"
#include "model_loader.h"
#include "vpk_archive.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace {

using tf2::native::AnimationModel;
using tf2::native::AnimationSample;
using tf2::native::ModelMetadata;

// row-major 3x4 with column-vector convention (v' = M * v), matching the
// engine's ConcatTransforms: out[r] = in1[r] * in2 plus in1's translation.
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

// poseToBone is stored row-major as 12 floats; the GPU side expects a
// column-major 4x4, which is what main.cpp's poseToBoneToMatrix4x4 builds.
std::array<float, 16> toColumnMajor(const std::array<float, 12>& m) {
  return {
    m[0], m[4], m[8], 0.0f,
    m[1], m[5], m[9], 0.0f,
    m[2], m[6], m[10], 0.0f,
    m[3], m[7], m[11], 1.0f
  };
}

// skin = animWorld * poseToBone (row-major 3x4, then column-major for upload).
// `order` is only ever flipped by --mutation.
std::array<float, 16> skinMatrix(
    const std::array<float, 12>& animWorld, const std::array<float, 12>& poseToBone,
    bool reversed = false) {
  return reversed
    ? toColumnMajor(concatTransforms(poseToBone, animWorld))
    : toColumnMajor(concatTransforms(animWorld, poseToBone));
}

struct Failure {
  std::string reason;
  bool ok() const { return reason.empty(); }
};

// A bone as the skinning path needs it: the local bind transform (from the
// animation decoder's bone table, which parses both halves) next to the
// inverse bind matrix (from the product's loader). Two parsers read the same
// table, so the convention check below is also a cross-check between them.
struct SkinBone {
  std::int32_t parent = -1;
  std::array<float, 3> position{};
  std::array<float, 4> rotation{{0.0f, 0.0f, 0.0f, 1.0f}};
  std::array<float, 12> poseToBone{};
};

// 1. Compose bone-to-world from the bone table's own local transforms and
//    compare inverse() against the stored poseToBone. Uses only fields the MDL
//    carries twice, so a decoder bug in either copy shows up as a mismatch.
Failure checkConvention(const std::vector<SkinBone>& bones, float& worstError, std::size_t& worstBone,
    bool offsetFirstBone = false) {
  worstError = 0.0f;
  worstBone = 0;
  if (bones.empty()) return {"model has no bones"};
  std::vector<std::array<float, 12>> world(bones.size());
  for (std::size_t i = 0; i < bones.size(); ++i) {
    const auto& bone = bones[i];
    auto position = bone.position;
    if (offsetFirstBone && i == 0) position[0] += 1.0f;
    const auto local = localTransform(position, bone.rotation);
    if (bone.parent < 0) {
      world[i] = local;
    } else if (static_cast<std::size_t>(bone.parent) < i) {
      world[i] = concatTransforms(world[static_cast<std::size_t>(bone.parent)], local);
    } else {
      return {"bone parent is not an earlier bone"};
    }
  }
  for (std::size_t i = 0; i < bones.size(); ++i) {
    const auto inverted = invertRigid(world[i]);
    for (int j = 0; j < 12; ++j) {
      const float error = std::fabs(inverted[j] - bones[i].poseToBone[j]);
      if (error > worstError) { worstError = error; worstBone = i; }
    }
  }
  if (worstError > 1.0e-3f) return {"poseToBone is not the inverse of the composed bone-to-world"};
  return {};
}

// The mutation flips one thing at a time and each flip has to move a named
// reading. `--mutation` exists because a criterion nobody has seen fail is not
// a criterion: the three checks above could all be tautologies (for example, if
// the model's local transforms were decoded from the same bytes as poseToBone,
// the convention check would agree with itself no matter what the convention
// really is).
//
//   transpose  swap the multiply order of skin = animWorld * poseToBone. The
//              matrix stays finite and plausible, so only a real order
//              assertion catches it -- this is the failure the identity check
//              is for.
//   offset     rotate a single bone's local position by one unit. The
//              convention check must notice, because it recomposes and inverts.
enum class Mutation { None, Transpose, Offset };

} // namespace

int main(int argc, char** argv) {
  Mutation mutation = Mutation::None;
  for (int i = 1; i < argc; ++i) {
    const std::string flag = argv[i];
    if (flag == "--mutation") mutation = Mutation::Transpose;
    else if (flag == "--mutation-offset") mutation = Mutation::Offset;
    else {
      std::cerr << "usage: skeleton_skin_probe [--mutation] [--mutation-offset]\n";
      return 2;
    }
  }
  const char* tfRoot = std::getenv("TF_ROOT");
  const std::filesystem::path root = tfRoot
    ? std::filesystem::path(tfRoot)
    : std::filesystem::path("D:/SteamLibrary/steamapps/common/Team Fortress 2/tf");
  const std::string modelPath = "models/player/scout.mdl";
  const std::string animPath = "models/player/scout_animations.mdl";

  std::string reason;
  tf2::native::VpkArchive archive;
  if (!archive.open(root / "tf2_misc_dir.vpk", &reason)) {
    std::cout << "{\"status\":\"failed\",\"reason\":\"cannot open tf2_misc_dir.vpk: " << reason << "\"}\n";
    return 1;
  }

  const auto modelBytes = archive.read(modelPath, &reason);
  const auto animBytes = archive.read(animPath, &reason);
  AnimationModel model;
  AnimationModel anims;
  if (tf2::native::decodeAnimationModel(modelBytes.data(), modelBytes.size(), model) != tf2::native::AnimationStatus::Ok) {
    std::cout << "{\"status\":\"failed\",\"reason\":\"model decode: " << model.reason << "\"}\n";
    return 1;
  }
  if (tf2::native::decodeAnimationModel(animBytes.data(), animBytes.size(), anims) != tf2::native::AnimationStatus::Ok) {
    std::cout << "{\"status\":\"failed\",\"reason\":\"anim decode: " << anims.reason << "\"}\n";
    return 1;
  }

  // Build the skinning view of the skeleton: local transform from the
  // animation decoder, inverse bind from the product loader.
  ModelMetadata metadata;
  {
    auto inspection = tf2::native::ModelLoader::inspectVpk(archive, modelPath);
    metadata = inspection.metadata;
  }
  if (metadata.bones.size() != model.bones.size()) {
    std::cout << "{\"status\":\"failed\",\"reason\":\"skeleton size differs between loaders\"}\n";
    return 1;
  }
  std::vector<SkinBone> skeleton(metadata.bones.size());
  for (std::size_t i = 0; i < skeleton.size(); ++i) {
    skeleton[i].parent = metadata.bones[i].parent;
    skeleton[i].position = model.bones[i].position;
    skeleton[i].rotation = model.bones[i].rotation;
    skeleton[i].poseToBone = metadata.bones[i].poseToBone;
  }

  // The animation file and the model file are separate skeletons and they are
  // not the same size: scout.mdl carries 78 bones, scout_animations.mdl carries
  // 76. The two extra model bones are `hlp_forearm_L` / `hlp_forearm_R`
  // (parents 11 / 12), appended after the animated ones. Indices 0..75 agree on
  // name and parent; the tail is a helper append with no animation channel and
  // must simply keep its bind transform.
  //
  // Pin that relationship instead of assuming it: an animation bone index is
  // only usable against the model skeleton when the name and parent match.
  std::size_t mappedBones = 0;
  std::vector<std::int32_t> animToModel(anims.bones.size(), -1);
  for (std::size_t i = 0; i < anims.bones.size(); ++i) {
    if (i < skeleton.size()
        && anims.bones[i].name == model.bones[i].name
        && anims.bones[i].parent == skeleton[i].parent) {
      animToModel[i] = static_cast<std::int32_t>(i);
      ++mappedBones;
    }
  }
  if (mappedBones != anims.bones.size()) {
    std::cout << "{\"status\":\"failed\",\"reason\":\"animation bone did not line up with the model skeleton\","
              << "\"mappedBones\":" << mappedBones
              << ",\"animBones\":" << anims.bones.size()
              << ",\"modelBones\":" << skeleton.size() << "}\n";
    return 1;
  }

  float conventionError = 0.0f;
  std::size_t conventionBone = 0;
  const auto convention = checkConvention(skeleton, conventionError, conventionBone,
    mutation == Mutation::Offset);
  if (!convention.ok()) {
    std::cout << "{\"status\":\"failed\",\"reason\":\"" << convention.reason
              << "\",\"mutationCaught\":" << (mutation != Mutation::None ? "true" : "false")
              << ",\"worstError\":" << conventionError << ",\"worstBone\":" << conventionBone << "}\n";
    return 1;
  }
  if (mutation == Mutation::Offset) {
    std::cout << "{\"status\":\"failed\",\"reason\":\"offset bone did not move the reading\","
              << "\"mutationCaught\":false,\"worstError\":" << conventionError << "}\n";
    return 1;
  }

  // 2. Bind pose identity. Rebuild animWorld by composing the bone table's own
  //    local transforms -- which is by definition the bind pose -- and require
  //    skin = animWorld * poseToBone to be identity.
  //
  //    On its own this reading has a blind spot, and the --mutation run proves
  //    it: at the bind pose animWorld and poseToBone are exact inverses, so
  //    swapping the multiply order ALSO gives identity. Identity at bind pose
  //    is therefore a necessary condition that cannot discriminate the order.
  //    The discriminating reading is check 3's first-bone skinned position,
  //    which uses a real animated pose. Keep both; do not delete either.
  float identityWorst = 0.0f;
  std::size_t identityBones = 0;
  {
    std::vector<std::array<float, 12>> world(skeleton.size());
    for (std::size_t i = 0; i < skeleton.size(); ++i) {
      const auto local = localTransform(skeleton[i].position, skeleton[i].rotation);
      world[i] = skeleton[i].parent < 0
        ? local
        : concatTransforms(world[static_cast<std::size_t>(skeleton[i].parent)], local);
    }
    static const float identity[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    for (std::size_t i = 0; i < skeleton.size(); ++i) {
      const auto skin = skinMatrix(world[i], skeleton[i].poseToBone);
      for (int j = 0; j < 16; ++j) identityWorst = std::max(identityWorst, std::fabs(skin[j] - identity[j]));
      ++identityBones;
    }
    if (identityWorst > 1.0e-3f) {
      std::cout << "{\"status\":\"failed\",\"reason\":\"bind-pose skin matrix is not identity\","
                << "\"mutationCaught\":" << (mutation != Mutation::None ? "true" : "false")
                << ",\"identityWorst\":" << identityWorst << ",\"bones\":" << identityBones << "}\n";
      return 1;
    }
  }

  // 3. A real sequence must be samplable and must move a bone between two
  //    ticks -- otherwise the animation wiring would be invisible in the one
  //    place it must show. Then the discriminating assertion: take a bone's
  //    own origin in bind-pose model space, push it through the full skinning
  //    path, and require it to land on the animated bone's world position.
  //    This is what separates `animWorld * poseToBone` from `poseToBone *
  //    animWorld`: the two orders agree at the bind pose but not here.
  std::size_t sampledSequences = 0;
  float motionDelta = 0.0f;
  std::string motionSequence;
  float skinLands = 0.0f;       // correct order
  float skinReversed = 0.0f;    // mutated order, must be much larger
  std::string skinSequence;
  for (std::size_t s = 0; s < anims.sequences.size(); ++s) {
    const auto& sequence = anims.sequences[s];
    if (sequence.animDesc < 0) continue;
    const auto& anim = anims.animations[static_cast<std::size_t>(sequence.animDesc)];
    if (!(anim.fps > 0.0f) || anim.frameCount < 4) continue;
    const int tick = std::max(1, anim.frameCount / 2);
    const auto first = tf2::native::sampleAnimation(anims, static_cast<std::int32_t>(s), 0, anim.fps);
    const auto later = tf2::native::sampleAnimation(anims, static_cast<std::int32_t>(s), tick, anim.fps);
    if (first.status != tf2::native::AnimationStatus::Ok
        || later.status != tf2::native::AnimationStatus::Ok) continue;
    if (first.localPosition.size() != anims.bones.size()
        || later.localRotation.size() != anims.bones.size()) continue;
    ++sampledSequences;
    float delta = 0.0f;
    for (std::size_t i = 0; i < anims.bones.size(); ++i) {
      for (int k = 0; k < 3; ++k) {
        delta = std::max(delta, std::fabs(later.localPosition[i][k] - first.localPosition[i][k]));
      }
      for (int k = 0; k < 4; ++k) {
        delta = std::max(delta, std::fabs(later.localRotation[i][k] - first.localRotation[i][k]));
      }
    }
    if (delta > motionDelta) { motionDelta = delta; motionSequence = sequence.label; }

    // Compose the sampled pose into world matrices, then push rig bone 1's
    // bind-pose origin through animWorld * poseToBone. The result must equal
    // the animated origin of that bone.
    if (skinSequence.empty()) {
      const std::size_t probe = 1;  // bip_spine_0: not the root, so it can move
      std::vector<std::array<float, 12>> poseWorld(anims.bones.size());
      bool composed = true;
      for (std::size_t i = 0; i < anims.bones.size(); ++i) {
        const auto local = localTransform(later.localPosition[i], later.localRotation[i]);
        if (anims.bones[i].parent < 0) poseWorld[i] = local;
        else if (static_cast<std::size_t>(anims.bones[i].parent) < i) {
          poseWorld[i] = concatTransforms(poseWorld[static_cast<std::size_t>(anims.bones[i].parent)], local);
        } else { composed = false; break; }
      }
      if (composed) {
        // rig origin in bind-pose model space = poseToBone inverse applied to 0,
        // i.e. the translation column of the inverse of poseToBone.
        const auto bindWorld = invertRigid(skeleton[probe].poseToBone);
        const float origin[3] = {bindWorld[3], bindWorld[7], bindWorld[11]};
        const auto skinned = concatTransforms(poseWorld[probe], skeleton[probe].poseToBone);
        const float got[3] = {
          skinned[0] * origin[0] + skinned[1] * origin[1] + skinned[2] * origin[2] + skinned[3],
          skinned[4] * origin[0] + skinned[5] * origin[1] + skinned[6] * origin[2] + skinned[7],
          skinned[8] * origin[0] + skinned[9] * origin[1] + skinned[10] * origin[2] + skinned[11],
        };
        const float want[3] = {
          poseWorld[probe][3], poseWorld[probe][7], poseWorld[probe][11]};
        skinLands = std::max({std::fabs(got[0] - want[0]), std::fabs(got[1] - want[1]),
          std::fabs(got[2] - want[2])});
        const auto reversed = concatTransforms(skeleton[probe].poseToBone, poseWorld[probe]);
        const float bad[3] = {
          reversed[0] * origin[0] + reversed[1] * origin[1] + reversed[2] * origin[2] + reversed[3],
          reversed[4] * origin[0] + reversed[5] * origin[1] + reversed[6] * origin[2] + reversed[7],
          reversed[8] * origin[0] + reversed[9] * origin[1] + reversed[10] * origin[2] + reversed[11],
        };
        skinReversed = std::max({std::fabs(bad[0] - want[0]), std::fabs(bad[1] - want[1]),
          std::fabs(bad[2] - want[2])});
        skinSequence = sequence.label;
      }
    }
  }
  if (sampledSequences == 0) {
    std::cout << "{\"status\":\"failed\",\"reason\":\"no sequenced animation could be sampled\"}\n";
    return 1;
  }
  if (motionDelta <= 1.0e-5f) {
    std::cout << "{\"status\":\"failed\",\"reason\":\"no sequence moved a bone between two ticks\","
              << "\"sampledSequences\":" << sampledSequences << "}\n";
    return 1;
  }
  // The discriminating reading. The correct order must land the skinned origin
  // on the animated origin; a wrong order must miss it by a lot. The gap
  // between the two is what makes the order assertion able to fail.
  if (skinSequence.empty()) {
    std::cout << "{\"status\":\"failed\",\"reason\":\"no sequence had a composable pose to skin\"}\n";
    return 1;
  }
  const bool skinLandsOk = skinLands <= 1.0e-2f;
  const bool reversedMisses = skinReversed >= 1.0f;
  if (mutation == Mutation::Transpose) {
    // Report the mutated reading so the gate can assert the flip moved it.
    std::cout << "{\"status\":\"" << ((skinLandsOk && reversedMisses) ? "ok" : "failed") << "\""
              << ",\"mutation\":\"transpose\""
              << ",\"mutationCaught\":" << ((skinLandsOk && reversedMisses) ? "true" : "false")
              << ",\"skinLands\":" << skinLandsOk
              << ",\"skinReversed\":" << skinReversed
              << ",\"skinSequence\":\"" << skinSequence << "\"}\n";
    return (skinLandsOk && reversedMisses) ? 0 : 1;
  }
  if (!skinLandsOk || !reversedMisses) {
    std::cout << "{\"status\":\"failed\",\"reason\":\"skin order is not discriminated\","
              << "\"skinLands\":" << skinLands << ",\"skinReversed\":" << skinReversed << "}\n";
    return 1;
  }

  std::cout << "{\"status\":\"ok\""
            << ",\"model\":\"" << modelPath << "\""
            << ",\"animModel\":\"" << animPath << "\""
            << ",\"bones\":" << skeleton.size()
            << ",\"animBones\":" << anims.bones.size()
            << ",\"mappedBones\":" << mappedBones
            << ",\"conventionError\":" << conventionError
            << ",\"conventionBone\":" << conventionBone
            << ",\"identityBones\":" << identityBones
            << ",\"identityWorst\":" << identityWorst
            << ",\"skinLands\":" << skinLands
            << ",\"skinReversed\":" << skinReversed
            << ",\"skinSequence\":\"" << skinSequence << "\""
            << ",\"sampledSequences\":" << sampledSequences
            << ",\"motionDelta\":" << motionDelta
            << ",\"motionSequence\":\"" << motionSequence << "\""
            << "}\n";
  return 0;
}
