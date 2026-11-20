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

// A sequence's blend grid, straight out of mstudioseqdesc_t: a sequence with more
// than one blend samples a width x height grid of local animations and
// interpolates between cells. The file states the two axes as pose parameter
// slots (paramIndex), each axis's sample range in the pose parameter's own units
// (paramStart/paramEnd), the cell count per axis (groupSize) and one local
// animation index per cell (cells).
//
// Flattening order: the engine reads cell k = index1 * groupSize[0] + index0, so
// paramIndex[1] is the row (outer) axis and paramIndex[0] the column (inner) one.
// The decoder carries the raw fields and does not apply that order yet (T105-n17
// Step 1 measures the file first); the census prints the cells in that order so
// the named poses in the file can confirm it.
//
// groupSize lives at sequence record +68/+72. The word at +64 is zero in every
// file we own, and a reader that takes groupSize from there sees a grid with no
// cells -- the failure is silent because everything else still decodes.
struct AnimationBlendGrid {
  bool valid = false;
  std::array<std::int32_t, 2> groupSize{};
  std::array<std::int32_t, 2> paramIndex{{-1, -1}};
  std::array<float, 2> paramStart{};
  std::array<float, 2> paramEnd{};
  std::vector<std::int16_t> cells;   // local animation (animdesc) index per cell
};

// The per-bone weight list a sequence carries. Source scales a blend layer's
// contribution per bone by these weights, which is what lets an aim layer cover
// the upper body and leave the legs on the locomotion pose. The file stores one
// float per bone at (sequence record + offset) and several sequences share one
// table, so this is a read, not a copy of model data.
struct AnimationWeightList {
  bool readable = false;
  std::int32_t offset = -1;      // record-relative, as stored
  std::size_t boneCount = 0;
  std::size_t zeroCount = 0;
  float minWeight = 0.0f;
  float maxWeight = 0.0f;
  float sum = 0.0f;
  // Kept only for grid sequences: the summary above answers the census for the
  // few hundred single-cell sequences without carrying their floats around.
  std::vector<float> weights;
};

// One pose parameter slot (mstudioposeparamdesc_t, 20 bytes). start/end are the
// range the grid's paramStart/paramEnd are expressed against, so they are the
// normalisation a sampler needs; loop != 0 means the slot wraps.
struct AnimationPoseParam {
  std::string name;
  std::int32_t flags = 0;
  float start = 0.0f;
  float end = 0.0f;
  float loop = 0.0f;
};

struct AnimationSequence {
  std::string label;
  std::int32_t flags = 0;
  std::int32_t activity = 0;
  std::int32_t blendCount = 0;
  // The cell this decoder samples today: the FIRST cell of the grid. Step 1
  // keeps it exactly as it was so the picture does not move; the grid below is
  // the reading Step 2 needs to move off it.
  std::int32_t animDesc = -1;
  bool looping = false;
  AnimationBlendGrid grid;
  AnimationWeightList weights;
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
  // Pose parameter slots (6 per player class). Empty when the model declares
  // none or the table does not fit -- that is a report, not a failure.
  std::vector<AnimationPoseParam> poseParameters;
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
  // Which frame *block* inside the animation the requested frame resolved to.
  // An animation may declare 30 frames while carrying a single block of data
  // (sectionframes == 1), in which case every frame is the same pose and the
  // cursor moves without the picture moving. Without this reading, "frame=13/30
  // but the pose never changes" is indistinguishable from a decoder that reads
  // the frame index but not the frame data.
  std::int32_t localFrame = 0;
  std::int32_t sectionFrames = 0;
  float fps = 0.0f;
  bool looping = false;
  std::vector<std::array<float, 3>> localPosition;
  std::vector<std::array<float, 4>> localRotation;
  std::vector<std::array<float, 3>> modelPosition;
};

// T105-n17 Step 1: the decoded model also carries every sequence's blend grid,
// its weight-list summary and the pose parameter table. Reading them can never
// turn a decodable model into a failed one: a grid that does not validate is
// reported through AnimationBlendGrid::valid, so the refusals and the frames
// they refuse stay exactly as they were.
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
