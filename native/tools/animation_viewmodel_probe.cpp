#include "animation_decoder.h"
#include "model_loader.h"
#include "viewmodel.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {
template <typename T>
void writeAt(std::vector<std::uint8_t>& bytes, std::size_t offset, T value) {
  if (offset + sizeof(T) > bytes.size()) bytes.resize(offset + sizeof(T), 0);
  std::memcpy(bytes.data() + offset, &value, sizeof(T));
}

void writeText(std::vector<std::uint8_t>& bytes, std::size_t offset, const char* text) {
  const auto length = std::strlen(text) + 1;
  if (offset + length > bytes.size()) bytes.resize(offset + length, 0);
  std::memcpy(bytes.data() + offset, text, length);
}

std::vector<std::uint8_t> studioShell(int bones, int anims, int sequences) {
  const std::size_t boneAt = 408;
  const std::size_t animAt = boneAt + static_cast<std::size_t>(bones) * 216u;
  const std::size_t sequenceAt = animAt + static_cast<std::size_t>(anims) * 100u;
  std::vector<std::uint8_t> bytes(sequenceAt + static_cast<std::size_t>(sequences) * 212u, 0);
  writeAt<std::uint32_t>(bytes, 0, 0x54534449u);
  writeAt<std::uint32_t>(bytes, 4, 48u);
  writeAt<std::uint32_t>(bytes, 156, static_cast<std::uint32_t>(bones));
  writeAt<std::uint32_t>(bytes, 160, static_cast<std::uint32_t>(boneAt));
  writeAt<std::uint32_t>(bytes, 180, static_cast<std::uint32_t>(anims));
  writeAt<std::uint32_t>(bytes, 184, static_cast<std::uint32_t>(animAt));
  writeAt<std::uint32_t>(bytes, 188, static_cast<std::uint32_t>(sequences));
  writeAt<std::uint32_t>(bytes, 192, static_cast<std::uint32_t>(sequenceAt));
  for (int i = 0; i < bones; ++i) {
    const auto bone = boneAt + static_cast<std::size_t>(i) * 216u;
    writeText(bytes, bone + 180, i == 0 ? "root" : "child");
    writeAt<std::int32_t>(bytes, bone, 180);
    writeAt<std::int32_t>(bytes, bone + 4, i == 0 ? -1 : i - 1);
    writeAt<float>(bytes, bone + 32, i == 0 ? 0.0f : 1.0f);
    writeAt<float>(bytes, bone + 56, 1.0f);
    for (int axis = 0; axis < 3; ++axis) {
      writeAt<float>(bytes, bone + 72 + static_cast<std::size_t>(axis) * 4, 1.0f);
      writeAt<float>(bytes, bone + 84 + static_cast<std::size_t>(axis) * 4, 1.0f);
    }
  }
  return bytes;
}

void finish(std::vector<std::uint8_t>& bytes) {
  writeAt<std::uint32_t>(bytes, 76, static_cast<std::uint32_t>(bytes.size()));
}

void bindSequence(std::vector<std::uint8_t>& bytes, int sequence, const char* label, int flags, int anim) {
  const std::size_t sequenceAt = 408u + 216u + 100u;
  const auto offset = sequenceAt + static_cast<std::size_t>(sequence) * 212u;
  writeText(bytes, offset + 160, label);
  writeAt<std::int32_t>(bytes, offset + 4, 160);
  writeAt<std::int32_t>(bytes, offset + 12, flags);
  writeAt<std::int32_t>(bytes, offset + 56, 1);
  writeAt<std::int32_t>(bytes, offset + 60, 180);
  writeAt<std::int16_t>(bytes, offset + 180, static_cast<std::int16_t>(anim));
}

bool samePositions(const std::vector<std::array<float, 3>>& left,
    const std::vector<std::array<float, 3>>& right) {
  if (left.size() != right.size()) return false;
  return std::memcmp(left.data(), right.data(), left.size() * sizeof(left[0])) == 0;
}

bool failedSample(const tf2::native::AnimationSample& sample, const char* needle) {
  return sample.status == tf2::native::AnimationStatus::Failed && sample.reason.find(needle) != std::string::npos;
}

bool parentChainMoves() {
  const float half = std::sqrt(0.5f);
  const std::vector<std::int32_t> parents = {-1, 0};
  const std::vector<std::array<float, 3>> local = {{{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}}};
  const std::vector<std::array<float, 4>> identity = {
    {0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 1.0f}};
  std::vector<std::array<float, 4>> turned = identity;
  turned[0] = {0.0f, 0.0f, half, half};
  std::vector<std::array<float, 3>> rest;
  std::vector<std::array<float, 3>> moved;
  std::string reason;
  if (!tf2::native::composeLocalToModel(parents, local, identity, rest, reason)) return false;
  if (!tf2::native::composeLocalToModel(parents, local, turned, moved, reason)) return false;
  if (rest.size() != 2 || moved.size() != 2) return false;
  if (rest[1][0] != 1.0f || rest[1][1] != 0.0f || rest[1][2] != 0.0f) return false;
  return std::fabs(moved[1][0]) < 1.0e-4f && std::fabs(moved[1][1] - 1.0f) < 1.0e-4f
    && std::fabs(moved[1][2]) < 1.0e-4f;
}

bool loopAndOnce() {
  auto bytes = studioShell(1, 1, 1);
  writeAt<float>(bytes, 624 + 8, 10.0f);
  writeAt<std::int32_t>(bytes, 624 + 16, 4);
  writeAt<std::int32_t>(bytes, 624 + 56, static_cast<std::int32_t>(bytes.size() - 624));
  const auto data = bytes.size();
  bytes.resize(data + 20, 0);
  bytes[data] = 0;
  bytes[data + 1] = 0x04;
  writeAt<std::int16_t>(bytes, data + 2, 0);
  writeAt<std::int16_t>(bytes, data + 4, 6);
  bytes[data + 10] = 4;
  bytes[data + 11] = 4;
  writeAt<std::int16_t>(bytes, data + 12, 0);
  writeAt<std::int16_t>(bytes, data + 14, 100);
  writeAt<std::int16_t>(bytes, data + 16, 200);
  writeAt<std::int16_t>(bytes, data + 18, 300);
  bindSequence(bytes, 0, "loop", 1, 0);
  finish(bytes);
  tf2::native::AnimationModel looping;
  if (tf2::native::decodeAnimationModel(bytes.data(), bytes.size(), looping) != tf2::native::AnimationStatus::Ok) {
    return false;
  }
  const auto start = tf2::native::sampleAnimation(looping, 0, 0, 10.0f);
  const auto wrapped = tf2::native::sampleAnimation(looping, 0, 4, 10.0f);
  const auto again = tf2::native::sampleAnimation(looping, 0, 4, 10.0f);
  if (start.status != tf2::native::AnimationStatus::Ok || wrapped.status != tf2::native::AnimationStatus::Ok) return false;
  if (start.frame != 0 || wrapped.frame != 0) return false;
  if (!samePositions(start.modelPosition, wrapped.modelPosition)) return false;
  if (!samePositions(wrapped.modelPosition, again.modelPosition)) return false;

  auto onceBytes = bytes;
  writeAt<std::int32_t>(onceBytes, 408 + 216 + 100 + 12, 0);
  tf2::native::AnimationModel once;
  if (tf2::native::decodeAnimationModel(onceBytes.data(), onceBytes.size(), once) != tf2::native::AnimationStatus::Ok) {
    return false;
  }
  const auto last = tf2::native::sampleAnimation(once, 0, 3, 10.0f);
  const auto clamped = tf2::native::sampleAnimation(once, 0, 100, 10.0f);
  return last.status == tf2::native::AnimationStatus::Ok
    && clamped.status == tf2::native::AnimationStatus::Ok
    && last.frame == 3 && clamped.frame == 3
    && samePositions(last.modelPosition, clamped.modelPosition)
    && !samePositions(start.modelPosition, last.modelPosition);
}

bool compressedRotations() {
  auto bytes = studioShell(1, 2, 2);
  const std::size_t animAt = 624;
  const std::size_t data = bytes.size();
  bytes.resize(data + 32, 0);
  writeAt<float>(bytes, animAt + 8, 30.0f);
  writeAt<std::int32_t>(bytes, animAt + 16, 1);
  writeAt<std::int32_t>(bytes, animAt + 56, static_cast<std::int32_t>(data - animAt));
  bytes[data] = 0;
  bytes[data + 1] = 0x02;
  writeAt<std::uint16_t>(bytes, data + 4, 32768);
  writeAt<std::uint16_t>(bytes, data + 6, 32768);
  writeAt<std::uint16_t>(bytes, data + 8, 16384);
  writeAt<float>(bytes, animAt + 100 + 8, 30.0f);
  writeAt<std::int32_t>(bytes, animAt + 100 + 16, 1);
  writeAt<std::int32_t>(bytes, animAt + 100 + 56, static_cast<std::int32_t>(data + 12 - (animAt + 100)));
  bytes[data + 12] = 0;
  bytes[data + 13] = 0x20;
  const std::uint64_t identity64 = static_cast<std::uint64_t>(1048576)
    | (static_cast<std::uint64_t>(1048576) << 21)
    | (static_cast<std::uint64_t>(1048576) << 42);
  writeAt<std::uint64_t>(bytes, data + 16, identity64);
  const std::size_t sequenceAt = animAt + 200;
  writeText(bytes, sequenceAt + 160, "raw48");
  writeAt<std::int32_t>(bytes, sequenceAt + 4, 160);
  writeAt<std::int32_t>(bytes, sequenceAt + 12, 0);
  writeAt<std::int32_t>(bytes, sequenceAt + 56, 1);
  writeAt<std::int32_t>(bytes, sequenceAt + 60, 180);
  writeAt<std::int16_t>(bytes, sequenceAt + 180, 0);
  writeText(bytes, sequenceAt + 212 + 160, "raw64");
  writeAt<std::int32_t>(bytes, sequenceAt + 212 + 4, 160);
  writeAt<std::int32_t>(bytes, sequenceAt + 212 + 56, 1);
  writeAt<std::int32_t>(bytes, sequenceAt + 212 + 60, 180);
  writeAt<std::int16_t>(bytes, sequenceAt + 212 + 180, 1);
  finish(bytes);
  tf2::native::AnimationModel model;
  if (tf2::native::decodeAnimationModel(bytes.data(), bytes.size(), model) != tf2::native::AnimationStatus::Ok) {
    std::cerr << "compressed decode: " << model.reason << "\n";
    return false;
  }
  const auto raw48 = tf2::native::sampleAnimation(model, 0, 0, 30.0f);
  const auto raw64 = tf2::native::sampleAnimation(model, 1, 0, 30.0f);
  const auto raw64Again = tf2::native::sampleAnimation(model, 1, 0, 30.0f);
  if (raw48.status != tf2::native::AnimationStatus::Ok || raw64.status != tf2::native::AnimationStatus::Ok) {
    std::cerr << "compressed sample: " << raw48.reason << " | " << raw64.reason << "\n";
    return false;
  }
  if (raw48.localRotation.empty() || raw64.localRotation.empty()) return false;
  const auto& q48 = raw48.localRotation[0];
  const auto& q64 = raw64.localRotation[0];
  const bool pass = q48[3] > 0.9f && q64[3] > 0.9f
    && samePositions(raw64.modelPosition, raw64Again.modelPosition);
  if (!pass) {
    std::cerr << "compressed quat48=" << q48[0] << "," << q48[1] << "," << q48[2] << "," << q48[3]
              << " quat64=" << q64[0] << "," << q64[1] << "," << q64[2] << "," << q64[3]
              << " same=" << samePositions(raw64.modelPosition, raw64Again.modelPosition) << "\n";
  }
  return pass;
}

bool malformedOffsets() {
  std::vector<std::uint8_t> shortHeader(500, 0);
  writeAt<std::uint32_t>(shortHeader, 0, 0x54534449u);
  writeAt<std::uint32_t>(shortHeader, 4, 48u);
  writeAt<std::uint32_t>(shortHeader, 76, 500u);
  writeAt<std::uint32_t>(shortHeader, 156, 1u);
  writeAt<std::uint32_t>(shortHeader, 160, 900u);
  tf2::native::AnimationModel outside;
  const bool boneRejected = tf2::native::decodeAnimationModel(shortHeader.data(), shortHeader.size(), outside)
      == tf2::native::AnimationStatus::Failed
    && outside.reason.find("bone table") != std::string::npos;

  auto badIndex = studioShell(1, 1, 1);
  writeAt<float>(badIndex, 624 + 8, 30.0f);
  writeAt<std::int32_t>(badIndex, 624 + 16, 2);
  writeAt<std::int32_t>(badIndex, 624 + 56, 500000);
  bindSequence(badIndex, 0, "bad", 0, 0);
  finish(badIndex);
  tf2::native::AnimationModel indexModel;
  const bool indexDecoded = tf2::native::decodeAnimationModel(badIndex.data(), badIndex.size(), indexModel)
    == tf2::native::AnimationStatus::Ok;
  const auto indexSample = tf2::native::sampleAnimation(indexModel, 0, 0, 30.0f);

  auto badLink = studioShell(1, 1, 1);
  writeAt<float>(badLink, 624 + 8, 30.0f);
  writeAt<std::int32_t>(badLink, 624 + 16, 1);
  writeAt<std::int32_t>(badLink, 624 + 56, static_cast<std::int32_t>(badLink.size() - 624));
  const auto linkAt = badLink.size();
  badLink.resize(linkAt + 4, 0);
  badLink[linkAt] = 0;
  writeAt<std::int16_t>(badLink, linkAt + 2, -4);
  bindSequence(badLink, 0, "link", 0, 0);
  finish(badLink);
  tf2::native::AnimationModel linkModel;
  tf2::native::decodeAnimationModel(badLink.data(), badLink.size(), linkModel);
  const auto linkSample = tf2::native::sampleAnimation(linkModel, 0, 0, 30.0f);

  auto badBone = studioShell(1, 1, 1);
  writeAt<float>(badBone, 624 + 8, 30.0f);
  writeAt<std::int32_t>(badBone, 624 + 16, 1);
  writeAt<std::int32_t>(badBone, 624 + 56, static_cast<std::int32_t>(badBone.size() - 624));
  const auto boneAt = badBone.size();
  badBone.resize(boneAt + 4, 0);
  badBone[boneAt] = 9;
  bindSequence(badBone, 0, "bone", 0, 0);
  finish(badBone);
  tf2::native::AnimationModel boneModel;
  tf2::native::decodeAnimationModel(badBone.data(), badBone.size(), boneModel);
  const auto boneSample = tf2::native::sampleAnimation(boneModel, 0, 0, 30.0f);
  return boneRejected && indexDecoded && failedSample(indexSample, "outside")
    && failedSample(linkSample, "negative") && failedSample(boneSample, "outside");
}

bool fovAndHands() {
  using tf2::native::normalizeViewModelFov;
  using tf2::native::viewModelHandMatrix;
  using tf2::native::ViewModelHand;
  const float missing = std::numeric_limits<float>::quiet_NaN();
  const bool fov = normalizeViewModelFov(0.0f) == 40.0f
    && normalizeViewModelFov(180.0f) == 120.0f
    && normalizeViewModelFov(80.0f) == 80.0f
    && std::isnan(normalizeViewModelFov(missing));
  const auto right = viewModelHandMatrix(ViewModelHand::Right);
  const auto left = viewModelHandMatrix(ViewModelHand::Left);
  const float rightX = right[0] * 1.0f + right[1] * 0.0f + right[2] * 0.0f + right[3];
  const float leftX = left[0] * 1.0f + left[1] * 0.0f + left[2] * 0.0f + left[3];
  return fov && rightX == 1.0f && leftX == -1.0f && right[5] == 1.0f && left[10] == 1.0f;
}

bool missingCompanions() {
  const auto directory = std::filesystem::temp_directory_path() / "tf2-viewmodel-missing-probe";
  std::error_code error;
  std::filesystem::remove_all(directory, error);
  std::filesystem::create_directories(directory, error);
  {
    std::ofstream out(directory / "v_missing.mdl", std::ios::binary);
    const char bytes[] = {'I', 'D', 'S', 'T'};
    out.write(bytes, 4);
  }
  const auto root = tf2::native::AssetRoot::fromPath(directory);
  const auto missing = tf2::native::buildViewModelRequest(root, "v_missing.mdl",
    tf2::native::ViewModelHand::Right, 0.0f);
  const auto absent = tf2::native::buildViewModelRequest(root, "v_absent.mdl",
    tf2::native::ViewModelHand::Left);
  const auto world = tf2::native::buildViewModelRequest(root, "models/player/scout.mdl",
    tf2::native::ViewModelHand::Right);
  const auto empty = tf2::native::buildViewModelRequest(root, "",
    tf2::native::ViewModelHand::Right, std::numeric_limits<float>::quiet_NaN());
  std::filesystem::remove_all(directory, error);
  return missing.status == tf2::native::ViewModelStatus::Failed
    && missing.reason.find("companions") != std::string::npos
    && missing.fov == 40.0f
    && absent.status == tf2::native::ViewModelStatus::Failed
    && world.status == tf2::native::ViewModelStatus::Failed
    && world.reason.find("not a viewmodel") != std::string::npos
    && empty.status == tf2::native::ViewModelStatus::Failed
    && std::isnan(empty.fov);
}

bool movingSequence(const tf2::native::AnimationModel& model, tf2::native::AnimationSample& start,
    tf2::native::AnimationSample& later, int& sequenceIndex, std::string& detail) {
  for (std::size_t i = 0; i < model.sequences.size(); ++i) {
    const auto& sequence = model.sequences[i];
    if (sequence.animDesc < 0) continue;
    const auto& anim = model.animations[static_cast<std::size_t>(sequence.animDesc)];
    if (anim.frameCount < 2 || !(anim.fps > 0.0f)) continue;
    const int tick = std::max(1, anim.frameCount / 2);
    auto first = tf2::native::sampleAnimation(model, static_cast<std::int32_t>(i), 0, anim.fps);
    auto second = tf2::native::sampleAnimation(model, static_cast<std::int32_t>(i), tick, anim.fps);
    if (first.status != tf2::native::AnimationStatus::Ok || second.status != tf2::native::AnimationStatus::Ok) {
      detail = first.status != tf2::native::AnimationStatus::Ok ? first.reason : second.reason;
      continue;
    }
    if (first.modelPosition.size() != model.bones.size()) {
      detail = "sample bone count does not match the skeleton";
      continue;
    }
    for (const auto& position : first.modelPosition) {
      if (!std::isfinite(position[0]) || !std::isfinite(position[1]) || !std::isfinite(position[2])) {
        detail = "sample position is not finite";
        return false;
      }
    }
    if (!samePositions(first.modelPosition, second.modelPosition)) {
      start = std::move(first);
      later = std::move(second);
      sequenceIndex = static_cast<int>(i);
      detail.clear();
      return true;
    }
  }
  if (detail.empty()) detail = "no sequence changed a bone between two ticks";
  return false;
}

bool sectionSample(const tf2::native::AnimationModel& model, std::string& detail) {
  for (std::size_t i = 0; i < model.sequences.size(); ++i) {
    const auto& sequence = model.sequences[i];
    if (sequence.animDesc < 0) continue;
    const auto& anim = model.animations[static_cast<std::size_t>(sequence.animDesc)];
    if (anim.sectionFrames <= 0 || anim.frameCount <= anim.sectionFrames) continue;
    const auto sample = tf2::native::sampleAnimation(model, static_cast<std::int32_t>(i),
      anim.sectionFrames, anim.fps);
    if (sample.status != tf2::native::AnimationStatus::Ok) {
      detail = sample.reason;
      return false;
    }
    detail.clear();
    return sample.modelPosition.size() == model.bones.size();
  }
  detail = "no sectioned sequence";
  return false;
}

bool realLoop(const tf2::native::AnimationModel& model) {
  for (std::size_t i = 0; i < model.sequences.size(); ++i) {
    const auto& sequence = model.sequences[i];
    if (!sequence.looping || sequence.animDesc < 0) continue;
    const auto& anim = model.animations[static_cast<std::size_t>(sequence.animDesc)];
    if (anim.frameCount < 2 || !(anim.fps > 0.0f) || anim.animBlock != 0) continue;
    const auto start = tf2::native::sampleAnimation(model, static_cast<std::int32_t>(i), 0, anim.fps);
    const auto wrapped = tf2::native::sampleAnimation(model, static_cast<std::int32_t>(i), anim.frameCount, anim.fps);
    if (start.status != tf2::native::AnimationStatus::Ok || wrapped.status != tf2::native::AnimationStatus::Ok) continue;
    return start.frame == 0 && wrapped.frame == 0 && samePositions(start.modelPosition, wrapped.modelPosition);
  }
  return false;
}
}

int main() {
  const bool parent = parentChainMoves();
  const bool loop = loopAndOnce();
  const bool compressed = compressedRotations();
  const bool malformed = malformedOffsets();
  const bool fov = fovAndHands();
  const bool missing = missingCompanions();

  const char* tfRoot = std::getenv("TF_ROOT");
  const std::filesystem::path root = tfRoot ? tfRoot : "D:/Steam/steamapps/common/Team Fortress 2/tf";
  tf2::native::VpkArchive archive;
  std::string openError;
  const bool opened = archive.open(root / "tf2_misc_dir.vpk", &openError);

  tf2::native::ModelInspection before;
  tf2::native::ModelInspection after;
  tf2::native::AnimationModel scout;
  tf2::native::AnimationModel scoutAnim;
  tf2::native::AnimationModel soldierAnim;
  std::string readError;
  bool bindPose = false;
  bool scoutOk = false;
  bool soldierOk = false;
  bool motion = false;
  bool section = false;
  bool wrapped = false;
  bool deterministic = false;
  std::string motionLabel;
  std::string motionDetail;
  std::string sectionDetail;
  int motionSequence = -1;
  if (opened) {
    before = tf2::native::ModelLoader::inspectVpk(archive, "models/player/scout.mdl");
    const auto scoutBytes = archive.read("models/player/scout.mdl", &readError);
    const auto scoutAnimBytes = archive.read("models/player/scout_animations.mdl", &readError);
    const auto soldierBytes = archive.read("models/player/soldier_animations.mdl", &readError);
    scoutOk = tf2::native::decodeAnimationModel(scoutBytes.data(), scoutBytes.size(), scout)
        == tf2::native::AnimationStatus::Ok
      && scout.bones.size() == 78
      && std::find(scout.includes.begin(), scout.includes.end(), "models/player/scout_animations.mdl")
        != scout.includes.end();
    const bool scoutAnimOk = tf2::native::decodeAnimationModel(scoutAnimBytes.data(), scoutAnimBytes.size(), scoutAnim)
      == tf2::native::AnimationStatus::Ok && !scoutAnim.bones.empty() && !scoutAnim.animations.empty();
    soldierOk = tf2::native::decodeAnimationModel(soldierBytes.data(), soldierBytes.size(), soldierAnim)
        == tf2::native::AnimationStatus::Ok
      && soldierAnim.bones.size() == 86 && !soldierAnim.animations.empty();
    after = tf2::native::ModelLoader::inspectVpk(archive, "models/player/scout.mdl");
    bindPose = before.metadata.valid && after.metadata.valid
      && !before.metadata.bones.empty() && before.metadata.bones.size() == after.metadata.bones.size()
      && before.metadata.bones[0].position == after.metadata.bones[0].position
      && before.metadata.bones[0].poseToBone == after.metadata.bones[0].poseToBone
      && before.metadata.sequenceDecodeReason == after.metadata.sequenceDecodeReason
      && before.metadata.sequenceDecodeReason.find("not decoded") != std::string::npos;
    if (scoutAnimOk) {
      tf2::native::AnimationSample start;
      tf2::native::AnimationSample later;
      motion = movingSequence(scoutAnim, start, later, motionSequence, motionDetail);
      if (motion) {
        motionLabel = start.sequenceLabel;
        const auto& anim = scoutAnim.animations[static_cast<std::size_t>(start.animDesc)];
        const auto repeat = tf2::native::sampleAnimation(scoutAnim, motionSequence, anim.frameCount / 2, anim.fps);
        deterministic = repeat.status == tf2::native::AnimationStatus::Ok
          && samePositions(later.modelPosition, repeat.modelPosition)
          && later.localRotation.size() == repeat.localRotation.size()
          && std::memcmp(later.localRotation.data(), repeat.localRotation.data(),
            later.localRotation.size() * sizeof(later.localRotation[0])) == 0;
      }
      section = sectionSample(scoutAnim, sectionDetail);
      wrapped = realLoop(scoutAnim);
    } else if (motionDetail.empty()) {
      motionDetail = scoutAnim.reason;
    }
    scoutOk = scoutOk && scoutAnimOk;
  } else {
    motionDetail = openError.empty() ? "tf2_misc_dir.vpk did not open" : openError;
  }

  tf2::native::ViewModelRequest viewModel;
  bool viewModelOk = false;
  if (opened) {
    const auto assets = tf2::native::AssetRoot::fromPath(root);
    viewModel = tf2::native::buildViewModelRequest(assets, "models/weapons/v_models/v_bat_scout.mdl",
      tf2::native::ViewModelHand::Left, 180.0f);
    const auto expected = tf2::native::viewModelHandMatrix(tf2::native::ViewModelHand::Left);
    const bool labelKept = !viewModel.sequenceLabels.empty();
    bool attachmentKept = false;
    for (const auto& attachment : viewModel.attachments) {
      if (!attachment.name.empty()) attachmentKept = true;
    }
    viewModelOk = viewModel.status == tf2::native::ViewModelStatus::Ok
      && viewModel.companionsComplete
      && viewModel.resolution == tf2::native::ModelAssetResolution::FoundVpk
      && viewModel.fov == 120.0f
      && viewModel.handTransform == expected
      && labelKept && attachmentKept;
  }

  const bool pass = parent && loop && compressed && malformed && fov && missing
    && bindPose && scoutOk && soldierOk && motion && section && wrapped && deterministic && viewModelOk;
  std::cout << "selfTest=" << (parent && loop && compressed && malformed ? "true" : "false")
            << " parent=" << (parent ? "true" : "false")
            << " loop=" << (loop ? "true" : "false")
            << " compressed=" << (compressed ? "true" : "false")
            << " malformed=" << (malformed ? "true" : "false")
            << " fov=" << (fov ? "true" : "false")
            << " missingCompanions=" << (missing ? "true" : "false")
            << " bindPose=" << (bindPose ? "true" : "false")
            << " scout=" << (scoutOk ? "true" : "false")
            << " soldier=" << (soldierOk ? "true" : "false")
            << " motion=" << (motion ? "true" : "false")
            << " section=" << (section ? "true" : "false")
            << " realLoop=" << (wrapped ? "true" : "false")
            << " deterministic=" << (deterministic ? "true" : "false")
            << " viewmodel=" << (viewModelOk ? "true" : "false")
            << " motionSequence=" << motionLabel
            << " bones=" << scoutAnim.bones.size()
            << " anims=" << scoutAnim.animations.size()
            << " viewSequences=" << viewModel.sequenceLabels.size()
            << " viewSequence=" << (viewModel.sequenceLabels.empty() ? std::string() : viewModel.sequenceLabels.front())
            << " viewAttachments=" << viewModel.attachments.size()
            << " viewAttachment=" << (viewModel.attachments.empty() ? std::string() : viewModel.attachments.front().name)
            << " confidence=" << tf2::native::kAnimationRotationConfidence
            << "\n";
  if (!pass) {
    if (!scout.reason.empty()) std::cerr << "scout: " << scout.reason << "\n";
    if (!scoutAnim.reason.empty()) std::cerr << "scoutAnim: " << scoutAnim.reason << "\n";
    if (!soldierAnim.reason.empty()) std::cerr << "soldierAnim: " << soldierAnim.reason << "\n";
    if (!motionDetail.empty()) std::cerr << "motion: " << motionDetail << "\n";
    if (!sectionDetail.empty()) std::cerr << "section: " << sectionDetail << "\n";
    if (!viewModelOk) {
      std::cerr << "viewmodel status=" << static_cast<int>(viewModel.status)
                << " resolution=" << static_cast<int>(viewModel.resolution)
                << " companions=" << viewModel.companionsComplete
                << " fov=" << viewModel.fov
                << " reason=" << viewModel.reason
                << " label=" << (viewModel.sequenceLabels.empty() ? std::string() : viewModel.sequenceLabels.front())
                << " attachment=" << (viewModel.attachments.empty() ? std::string() : viewModel.attachments.front().name)
                << "\n";
    }
  }
  return pass ? 0 : 1;
}
