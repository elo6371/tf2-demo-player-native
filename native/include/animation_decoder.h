#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace tf2::native {

enum class AnimationStatus { Ok, Failed };

// Quaternion layout for this decoder is xyzw. A +90 degree rotation about Z is
// (0, 0, sin(pi/4), cos(pi/4)) and takes local +X to model +Y. That is the
// contract checked by the probe. It is not a Source screenshot.
//
// RAWROT and RAWROT2 follow the Source compressed-quaternion bit layout.
// ANIMROT multiplies three RLE channels by the bone rotation scale and converts
// them with the Source RadianEuler formula (x roll, y pitch, z yaw). Euler
// order and position scale are not visually verified.
inline constexpr const char* kAnimationRotationConfidence =
  "bit-layout; Euler order and position scale are not visually verified";

struct AnimationBone {
  std::string name;
  std::int32_t parent = -1;
  std::array<float, 3> position{};
  std::array<float, 4> rotation{{0.0f, 0.0f, 0.0f, 1.0f}};
  std::array<float, 3> rotationEuler{};
  std::array<float, 3> positionScale{};
  std::array<float, 3> rotationScale{};
};

struct AnimationDesc {
  std::string name;
  float fps = 0.0f;
  std::int32_t flags = 0;
  std::int32_t frameCount = 0;
  std::int32_t animBlock = 0;
  std::int32_t animIndex = 0;
  std::int32_t sectionIndex = 0;
  std::int32_t sectionFrames = 0;
};

struct AnimationSequence {
  std::string label;
  std::int32_t flags = 0;
  std::int32_t activity = 0;
  std::int32_t blendCount = 0;
  std::int32_t animDesc = -1;
  bool looping = false;
};

struct AnimationModel {
  bool loaded = false;
  AnimationStatus status = AnimationStatus::Failed;
  std::string reason;
  std::string name;
  std::uint32_t version = 0;
  std::vector<AnimationBone> bones;
  std::vector<AnimationDesc> animations;
  std::vector<AnimationSequence> sequences;
  std::vector<std::string> includes;
  std::vector<std::uint8_t> bytes;
};

struct AnimationSample {
  AnimationStatus status = AnimationStatus::Failed;
  std::string reason;
  std::string sequenceLabel;
  std::string rotationConfidence = kAnimationRotationConfidence;
  std::int32_t animDesc = -1;
  std::int32_t frame = 0;
  std::int32_t frameCount = 0;
  float fps = 0.0f;
  bool looping = false;
  std::vector<std::array<float, 3>> localPosition;
  std::vector<std::array<float, 4>> localRotation;
  std::vector<std::array<float, 3>> modelPosition;
};

AnimationStatus decodeAnimationModel(const std::uint8_t* bytes, std::size_t size, AnimationModel& out);

AnimationSample sampleAnimation(const AnimationModel& model, std::int32_t sequenceIndex,
  int tick, float tickRate);

// Parents must be -1 or an earlier bone. Child model = parent model * local.
bool composeLocalToModel(const std::vector<std::int32_t>& parents,
  const std::vector<std::array<float, 3>>& localPosition,
  const std::vector<std::array<float, 4>>& localRotation,
  std::vector<std::array<float, 3>>& modelPosition,
  std::string& reason);

} // namespace tf2::native
