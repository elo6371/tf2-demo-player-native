#include "animation_decoder.h"

#include <cmath>
#include <cstring>
#include <limits>

namespace tf2::native {
namespace {

constexpr std::uint32_t kIdStudio = 0x54534449u;
constexpr std::size_t kMaxFileBytes = 64ull * 1024ull * 1024ull;
constexpr std::size_t kHeaderBytes = 408;
constexpr std::size_t kBoneStride = 216;
constexpr std::size_t kAnimStride = 100;
constexpr std::size_t kSequenceStride = 212;
constexpr std::size_t kIncludeStride = 8;
constexpr int kMaxBones = 1024;
constexpr int kMaxAnims = 8192;
constexpr int kMaxSequences = 8192;
constexpr int kMaxIncludes = 64;
constexpr int kLoopingFlag = 0x0001;
constexpr int kRawPos = 0x01;
constexpr int kRawRot = 0x02;
constexpr int kAnimPos = 0x04;
constexpr int kAnimRot = 0x08;
constexpr int kDelta = 0x10;
constexpr int kRawRot2 = 0x20;

using Bytes = std::vector<std::uint8_t>;

bool rangeFits(std::size_t offset, std::size_t count, std::size_t stride, std::size_t size) {
  if (count != 0 && stride > (std::numeric_limits<std::size_t>::max)() / count) return false;
  const auto bytes = count * stride;
  return offset <= size && bytes <= size - offset;
}

bool addOffset(std::size_t base, std::size_t delta, std::size_t limit, std::size_t& out) {
  if (base > limit || delta > limit - base) return false;
  out = base + delta;
  return true;
}

template <typename T>
bool readAt(const std::uint8_t* bytes, std::size_t size, std::size_t offset, T& out) {
  if (offset > size || sizeof(T) > size - offset) return false;
  std::memcpy(&out, bytes + offset, sizeof(T));
  return true;
}

std::string readString(const std::uint8_t* bytes, std::size_t size, std::size_t offset) {
  if (offset >= size) return {};
  std::size_t end = offset;
  const auto limit = std::min(size, offset + 256);
  while (end < limit && bytes[end] != 0) ++end;
  std::string text(reinterpret_cast<const char*>(bytes + offset), end - offset);
  for (const unsigned char c : text) {
    if (c < 32 || c > 126) return {};
  }
  return text;
}

std::string indexedString(const std::uint8_t* bytes, std::size_t size, std::size_t base, std::int32_t index) {
  if (index < 0) return {};
  const auto relative = static_cast<std::size_t>(index);
  if (base > size || relative > size - base) return {};
  return readString(bytes, size, base + relative);
}

bool finite3(const std::array<float, 3>& value) {
  return std::isfinite(value[0]) && std::isfinite(value[1]) && std::isfinite(value[2]);
}

bool finite4(const std::array<float, 4>& value) {
  return std::isfinite(value[0]) && std::isfinite(value[1]) && std::isfinite(value[2]) && std::isfinite(value[3]);
}

void quatMultiply(const std::array<float, 4>& a, const std::array<float, 4>& b, std::array<float, 4>& out) {
  out[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
  out[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
  out[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
  out[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
}

void quatRotate(const std::array<float, 4>& q, const std::array<float, 3>& v, std::array<float, 3>& out) {
  const float tx = 2.0f * (q[1] * v[2] - q[2] * v[1]);
  const float ty = 2.0f * (q[2] * v[0] - q[0] * v[2]);
  const float tz = 2.0f * (q[0] * v[1] - q[1] * v[0]);
  out[0] = v[0] + q[3] * tx + (q[1] * tz - q[2] * ty);
  out[1] = v[1] + q[3] * ty + (q[2] * tx - q[0] * tz);
  out[2] = v[2] + q[3] * tz + (q[0] * ty - q[1] * tx);
}

void radianEulerToQuat(float roll, float pitch, float yaw, std::array<float, 4>& out) {
  const float halfRoll = roll * 0.5f;
  const float halfPitch = pitch * 0.5f;
  const float halfYaw = yaw * 0.5f;
  const float sr = std::sin(halfRoll);
  const float cr = std::cos(halfRoll);
  const float sp = std::sin(halfPitch);
  const float cp = std::cos(halfPitch);
  const float sy = std::sin(halfYaw);
  const float cy = std::cos(halfYaw);
  out[0] = sr * cp * cy - cr * sp * sy;
  out[1] = cr * sp * cy + sr * cp * sy;
  out[2] = cr * cp * sy - sr * sp * cy;
  out[3] = cr * cp * cy + sr * sp * sy;
}

float sourceFloat16(std::uint16_t input) {
  const unsigned sign = (input >> 15) & 1u;
  const unsigned exponent = (input >> 10) & 31u;
  const unsigned mantissa = input & 0x3ffu;
  if (exponent == 31u) {
    if (mantissa == 0) return (sign ? -65504.0f : 65504.0f);
    return 0.0f;
  }
  if (exponent == 0) {
    const float magnitude = (static_cast<float>(mantissa) / 1024.0f) * (1.0f / 16384.0f);
    return sign ? -magnitude : magnitude;
  }
  const std::uint32_t bits = (sign << 31)
    | ((exponent - 15u + 127u) << 23)
    | (mantissa << 13);
  float value = 0.0f;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

bool decodeQuat48(const std::uint8_t* bytes, std::size_t size, std::size_t offset, std::array<float, 4>& out, std::string& reason) {
  std::uint16_t x = 0, y = 0, zword = 0;
  if (!readAt(bytes, size, offset, x) || !readAt(bytes, size, offset + 2, y) || !readAt(bytes, size, offset + 4, zword)) {
    reason = "animation block offset is outside the file";
    return false;
  }
  const float qx = static_cast<float>((static_cast<int>(x) - 32768) * (1.0 / 32768.5));
  const float qy = static_cast<float>((static_cast<int>(y) - 32768) * (1.0 / 32768.5));
  const float qz = static_cast<float>((static_cast<int>(zword & 0x7fffu) - 16384) * (1.0 / 16384.5));
  const float squared = qx * qx + qy * qy + qz * qz;
  float qw = squared <= 1.0f ? std::sqrt(1.0f - squared) : 0.0f;
  if ((zword & 0x8000u) != 0) qw = -qw;
  out = {qx, qy, qz, qw};
  return true;
}

bool decodeQuat64(const std::uint8_t* bytes, std::size_t size, std::size_t offset, std::array<float, 4>& out, std::string& reason) {
  std::uint64_t packed = 0;
  if (!readAt(bytes, size, offset, packed)) {
    reason = "animation block offset is outside the file";
    return false;
  }
  const int x = static_cast<int>(packed & 0x1fffffu);
  const int y = static_cast<int>((packed >> 21) & 0x1fffffu);
  const int z = static_cast<int>((packed >> 42) & 0x1fffffu);
  const float qx = static_cast<float>((x - 1048576) * (1.0 / 1048576.5));
  const float qy = static_cast<float>((y - 1048576) * (1.0 / 1048576.5));
  const float qz = static_cast<float>((z - 1048576) * (1.0 / 1048576.5));
  const float squared = qx * qx + qy * qy + qz * qz;
  float qw = squared <= 1.0f ? std::sqrt(1.0f - squared) : 0.0f;
  if ((packed >> 63) != 0) qw = -qw;
  out = {qx, qy, qz, qw};
  return true;
}

bool decodeVector48(const std::uint8_t* bytes, std::size_t size, std::size_t offset, std::array<float, 3>& out, std::string& reason) {
  std::uint16_t x = 0, y = 0, z = 0;
  if (!readAt(bytes, size, offset, x) || !readAt(bytes, size, offset + 2, y) || !readAt(bytes, size, offset + 4, z)) {
    reason = "animation block offset is outside the file";
    return false;
  }
  out = {sourceFloat16(x), sourceFloat16(y), sourceFloat16(z)};
  return finite3(out) ? true : (reason = "bone scale is not finite", false);
}

bool readAnimChannel(const std::uint8_t* bytes, std::size_t size, std::size_t pointer, int channel,
    int frame, float scale, float& out, std::string& reason) {
  std::int16_t relative = 0;
  if (!readAt(bytes, size, pointer + static_cast<std::size_t>(channel) * 2, relative)) {
    reason = "animation value pointer is outside the file";
    return false;
  }
  if (relative <= 0) {
    out = 0.0f;
    return true;
  }
  std::size_t cursor = pointer + static_cast<std::size_t>(relative);
  int remain = frame;
  for (int guard = 0; guard < frame + 2; ++guard) {
    if (cursor + 2 > size) {
      reason = "animation value run is outside the file";
      return false;
    }
    const int valid = bytes[cursor];
    const int total = bytes[cursor + 1];
    if (total == 0) {
      reason = "animation value run has a zero span";
      return false;
    }
    if (remain >= total) {
      remain -= total;
      cursor += static_cast<std::size_t>(2 + valid * 2);
      continue;
    }
    if (valid <= 0) {
      reason = "animation value run has no samples";
      return false;
    }
    const int sample = remain < valid ? remain : valid - 1;
    std::int16_t raw = 0;
    if (!readAt(bytes, size, cursor + 2 + static_cast<std::size_t>(sample) * 2, raw)) {
      reason = "animation value run is outside the file";
      return false;
    }
    out = static_cast<float>(raw) * scale;
    return std::isfinite(out) ? true : (reason = "bone scale is not finite", false);
  }
  reason = "animation value run exceeded the frame count";
  return false;
}

int selectFrame(double cursor, int frameCount, bool looping) {
  if (looping) {
    cursor = std::fmod(cursor, static_cast<double>(frameCount));
    if (cursor < 0.0) cursor += static_cast<double>(frameCount);
  } else if (cursor < 0.0) {
    cursor = 0.0;
  } else if (cursor > static_cast<double>(frameCount - 1)) {
    cursor = static_cast<double>(frameCount - 1);
  }
  const int frame = static_cast<int>(cursor);
  if (frame < 0) return 0;
  if (frame >= frameCount) return frameCount - 1;
  return frame;
}

bool locateFrame(const std::uint8_t* bytes, std::size_t size, std::size_t animOffset, const AnimationDesc& anim,
    int frame, std::size_t& dataOffset, int& localFrame, std::string& reason) {
  if (anim.sectionFrames == 0) {
    if (anim.animBlock != 0) {
      reason = "animation section is stored in an external anim block";
      return false;
    }
    if (anim.animIndex < 0
        || !addOffset(animOffset, static_cast<std::size_t>(anim.animIndex), size, dataOffset)) {
      reason = "animation block offset is outside the file";
      return false;
    }
    localFrame = frame;
    return true;
  }
  if (anim.sectionFrames < 0 || anim.sectionIndex <= 0 || anim.frameCount <= 0) {
    reason = "animation section table is outside the file";
    return false;
  }
  const int sectionCount = (anim.frameCount + anim.sectionFrames - 1) / anim.sectionFrames;
  if (sectionCount <= 0 || sectionCount > 256) {
    reason = "animation section table is outside the file";
    return false;
  }
  int section = frame / anim.sectionFrames;
  if (section >= sectionCount) section = sectionCount - 1;
  std::size_t table = 0;
  if (!addOffset(animOffset, static_cast<std::size_t>(anim.sectionIndex), size, table)
      || !rangeFits(table, static_cast<std::size_t>(sectionCount), 8, size)) {
    reason = "animation section table is outside the file";
    return false;
  }
  std::int32_t block = 0;
  std::int32_t index = 0;
  const auto entry = table + static_cast<std::size_t>(section) * 8;
  readAt(bytes, size, entry, block);
  readAt(bytes, size, entry + 4, index);
  if (block != 0) {
    reason = "animation section is stored in an external anim block";
    return false;
  }
  if (index < 0 || !addOffset(animOffset, static_cast<std::size_t>(index), size, dataOffset)) {
    reason = "animation block offset is outside the file";
    return false;
  }
  localFrame = frame - section * anim.sectionFrames;
  if (localFrame < 0) localFrame = 0;
  return true;
}

bool readBoneFrame(const std::uint8_t* bytes, std::size_t size, const AnimationBone& bone, std::size_t record,
    int frame, std::array<float, 3>& position, std::array<float, 4>& rotation, std::string& reason) {
  const auto flags = bytes[record + 1];
  const std::size_t data = record + 4;
  std::size_t rotationBytes = 0;
  if ((flags & kRawRot) != 0) {
    if (!decodeQuat48(bytes, size, data, rotation, reason)) return false;
    rotationBytes = 6;
  } else if ((flags & kRawRot2) != 0) {
    if (!decodeQuat64(bytes, size, data, rotation, reason)) return false;
    rotationBytes = 8;
  } else if ((flags & kAnimRot) != 0) {
    float x = 0.0f, y = 0.0f, z = 0.0f;
    if (!readAnimChannel(bytes, size, data, 0, frame, bone.rotationScale[0], x, reason)
        || !readAnimChannel(bytes, size, data, 1, frame, bone.rotationScale[1], y, reason)
        || !readAnimChannel(bytes, size, data, 2, frame, bone.rotationScale[2], z, reason)) return false;
    if ((flags & kDelta) == 0) {
      x += bone.rotationEuler[0];
      y += bone.rotationEuler[1];
      z += bone.rotationEuler[2];
    }
    radianEulerToQuat(x, y, z, rotation);
    rotationBytes = 6;
  } else if ((flags & kDelta) != 0) {
    rotation = {0.0f, 0.0f, 0.0f, 1.0f};
  } else {
    rotation = bone.rotation;
  }

  const std::size_t positionAt = data + rotationBytes;
  if ((flags & kRawPos) != 0) {
    if (!decodeVector48(bytes, size, positionAt, position, reason)) return false;
  } else if ((flags & kAnimPos) != 0) {
    if (!readAnimChannel(bytes, size, positionAt, 0, frame, bone.positionScale[0], position[0], reason)
        || !readAnimChannel(bytes, size, positionAt, 1, frame, bone.positionScale[1], position[1], reason)
        || !readAnimChannel(bytes, size, positionAt, 2, frame, bone.positionScale[2], position[2], reason)) return false;
    if ((flags & kDelta) == 0) {
      position[0] += bone.position[0];
      position[1] += bone.position[1];
      position[2] += bone.position[2];
    }
  } else if ((flags & kDelta) != 0) {
    position = {0.0f, 0.0f, 0.0f};
  } else {
    position = bone.position;
  }
  if (!finite3(position) || !finite4(rotation)) {
    reason = "bone scale is not finite";
    return false;
  }
  return true;
}

} // namespace

bool composeLocalToModel(const std::vector<std::int32_t>& parents,
    const std::vector<std::array<float, 3>>& localPosition,
    const std::vector<std::array<float, 4>>& localRotation,
    std::vector<std::array<float, 3>>& modelPosition,
    std::string& reason) {
  const auto count = parents.size();
  if (localPosition.size() != count || localRotation.size() != count) {
    reason = "bone parent is outside the skeleton";
    return false;
  }
  modelPosition.assign(count, {});
  std::vector<std::array<float, 4>> modelRotation(count);
  for (std::size_t i = 0; i < count; ++i) {
    const auto parent = parents[i];
    if (parent < -1 || parent >= static_cast<std::int32_t>(i)) {
      reason = "bone parent is outside the skeleton";
      return false;
    }
    if (!finite3(localPosition[i]) || !finite4(localRotation[i])) {
      reason = "bone bind rotation is not finite";
      return false;
    }
    if (parent < 0) {
      modelPosition[i] = localPosition[i];
      modelRotation[i] = localRotation[i];
      continue;
    }
    std::array<float, 3> rotated{};
    quatRotate(modelRotation[static_cast<std::size_t>(parent)], localPosition[i], rotated);
    modelPosition[i] = {
      rotated[0] + modelPosition[static_cast<std::size_t>(parent)][0],
      rotated[1] + modelPosition[static_cast<std::size_t>(parent)][1],
      rotated[2] + modelPosition[static_cast<std::size_t>(parent)][2],
    };
    quatMultiply(modelRotation[static_cast<std::size_t>(parent)], localRotation[i], modelRotation[i]);
  }
  reason.clear();
  return true;
}

AnimationStatus decodeAnimationModel(const std::uint8_t* bytes, std::size_t size, AnimationModel& out) {
  out = AnimationModel{};
  const auto fail = [&](const char* reason) {
    out.loaded = false;
    out.status = AnimationStatus::Failed;
    out.reason = reason;
    out.bytes.clear();
    return AnimationStatus::Failed;
  };
  if (bytes == nullptr || size == 0) return fail("MDL is empty");
  if (size > kMaxFileBytes) return fail("MDL is larger than the animation decoder limit");
  if (size < kHeaderBytes) return fail("MDL header is shorter than the animation header");
  std::uint32_t id = 0;
  std::uint32_t version = 0;
  std::uint32_t length = 0;
  readAt(bytes, size, 0, id);
  readAt(bytes, size, 4, version);
  readAt(bytes, size, 76, length);
  if (id != kIdStudio) return fail("MDL signature is not IDST");
  if (version < 44 || version > 49) return fail("MDL version is outside 44-49");
  if (length == 0 || length > size) return fail("MDL declared length exceeds the file");
  out.version = version;
  out.name = readString(bytes, size, 12);

  std::int32_t boneCount = 0, boneIndex = 0, animCount = 0, animIndex = 0, sequenceCount = 0, sequenceIndex = 0;
  std::int32_t includeCount = 0, includeIndex = 0;
  readAt(bytes, size, 156, boneCount);
  readAt(bytes, size, 160, boneIndex);
  readAt(bytes, size, 180, animCount);
  readAt(bytes, size, 184, animIndex);
  readAt(bytes, size, 188, sequenceCount);
  readAt(bytes, size, 192, sequenceIndex);
  readAt(bytes, size, 336, includeCount);
  readAt(bytes, size, 340, includeIndex);
  if (boneCount < 0 || boneCount > kMaxBones) return fail("bone count is outside the safe range");
  if (animCount < 0 || animCount > kMaxAnims) return fail("animation count is outside the safe range");
  if (sequenceCount < 0 || sequenceCount > kMaxSequences) return fail("sequence count is outside the safe range");
  if (includeCount < 0 || includeCount > kMaxIncludes) return fail("include model count is outside the safe range");
  if (!rangeFits(static_cast<std::size_t>(boneIndex), static_cast<std::size_t>(boneCount), kBoneStride, size)) {
    return fail("bone table is outside the file");
  }
  if (!rangeFits(static_cast<std::size_t>(animIndex), static_cast<std::size_t>(animCount), kAnimStride, size)) {
    return fail("animation table is outside the file");
  }
  if (!rangeFits(static_cast<std::size_t>(sequenceIndex), static_cast<std::size_t>(sequenceCount), kSequenceStride, size)) {
    return fail("sequence table is outside the file");
  }
  if (!rangeFits(static_cast<std::size_t>(includeIndex), static_cast<std::size_t>(includeCount), kIncludeStride, size)) {
    return fail("include model table is outside the file");
  }

  out.bones.reserve(static_cast<std::size_t>(boneCount));
  for (std::int32_t i = 0; i < boneCount; ++i) {
    const auto offset = static_cast<std::size_t>(boneIndex) + static_cast<std::size_t>(i) * kBoneStride;
    std::int32_t nameIndex = 0;
    std::int32_t parent = -1;
    readAt(bytes, size, offset, nameIndex);
    readAt(bytes, size, offset + 4, parent);
    if (parent < -1 || parent >= i) return fail("bone parent is outside the skeleton");
    AnimationBone bone;
    bone.name = indexedString(bytes, size, offset, nameIndex);
    bone.parent = parent;
    for (int axis = 0; axis < 3; ++axis) readAt(bytes, size, offset + 32 + static_cast<std::size_t>(axis) * 4, bone.position[static_cast<std::size_t>(axis)]);
    for (int axis = 0; axis < 4; ++axis) readAt(bytes, size, offset + 44 + static_cast<std::size_t>(axis) * 4, bone.rotation[static_cast<std::size_t>(axis)]);
    for (int axis = 0; axis < 3; ++axis) readAt(bytes, size, offset + 60 + static_cast<std::size_t>(axis) * 4, bone.rotationEuler[static_cast<std::size_t>(axis)]);
    for (int axis = 0; axis < 3; ++axis) readAt(bytes, size, offset + 72 + static_cast<std::size_t>(axis) * 4, bone.positionScale[static_cast<std::size_t>(axis)]);
    for (int axis = 0; axis < 3; ++axis) readAt(bytes, size, offset + 84 + static_cast<std::size_t>(axis) * 4, bone.rotationScale[static_cast<std::size_t>(axis)]);
    if (!finite3(bone.position) || !finite4(bone.rotation) || !finite3(bone.rotationEuler)
        || !finite3(bone.positionScale) || !finite3(bone.rotationScale)) {
      return fail("bone bind rotation is not finite");
    }
    out.bones.push_back(std::move(bone));
  }

  out.animations.reserve(static_cast<std::size_t>(animCount));
  for (std::int32_t i = 0; i < animCount; ++i) {
    const auto offset = static_cast<std::size_t>(animIndex) + static_cast<std::size_t>(i) * kAnimStride;
    std::int32_t nameIndex = 0;
    AnimationDesc anim;
    readAt(bytes, size, offset + 4, nameIndex);
    readAt(bytes, size, offset + 8, anim.fps);
    readAt(bytes, size, offset + 12, anim.flags);
    readAt(bytes, size, offset + 16, anim.frameCount);
    readAt(bytes, size, offset + 52, anim.animBlock);
    readAt(bytes, size, offset + 56, anim.animIndex);
    readAt(bytes, size, offset + 80, anim.sectionIndex);
    readAt(bytes, size, offset + 84, anim.sectionFrames);
    anim.name = indexedString(bytes, size, offset, nameIndex);
    out.animations.push_back(std::move(anim));
  }

  out.sequences.reserve(static_cast<std::size_t>(sequenceCount));
  for (std::int32_t i = 0; i < sequenceCount; ++i) {
    const auto offset = static_cast<std::size_t>(sequenceIndex) + static_cast<std::size_t>(i) * kSequenceStride;
    std::int32_t labelIndex = 0;
    AnimationSequence sequence;
    readAt(bytes, size, offset + 4, labelIndex);
    readAt(bytes, size, offset + 12, sequence.flags);
    readAt(bytes, size, offset + 16, sequence.activity);
    readAt(bytes, size, offset + 56, sequence.blendCount);
    sequence.label = indexedString(bytes, size, offset, labelIndex);
    sequence.looping = (sequence.flags & kLoopingFlag) != 0;
    if (sequence.blendCount > 0) {
      std::int32_t animPointer = 0;
      readAt(bytes, size, offset + 60, animPointer);
      std::size_t animAt = 0;
      if (animPointer <= 0
          || !addOffset(offset, static_cast<std::size_t>(animPointer), size, animAt)
          || animAt > size - 2) {
        return fail("sequence animdesc index is outside the animation table");
      }
      std::int16_t animDesc = -1;
      readAt(bytes, size, animAt, animDesc);
      if (animDesc < 0 || animDesc >= animCount) return fail("sequence animdesc index is outside the animation table");
      sequence.animDesc = animDesc;
    }
    out.sequences.push_back(std::move(sequence));
  }

  out.includes.reserve(static_cast<std::size_t>(includeCount));
  for (std::int32_t i = 0; i < includeCount; ++i) {
    const auto offset = static_cast<std::size_t>(includeIndex) + static_cast<std::size_t>(i) * kIncludeStride;
    std::int32_t nameIndex = 0;
    std::int32_t labelIndex = 0;
    readAt(bytes, size, offset + 4, nameIndex);
    readAt(bytes, size, offset, labelIndex);
    auto name = indexedString(bytes, size, offset, nameIndex);
    if (name.find(".mdl") == std::string::npos) name = indexedString(bytes, size, offset, labelIndex);
    out.includes.push_back(std::move(name));
  }

  out.bytes.assign(bytes, bytes + size);
  out.loaded = true;
  out.status = AnimationStatus::Ok;
  out.reason.clear();
  return AnimationStatus::Ok;
}

AnimationSample sampleAnimation(const AnimationModel& model, std::int32_t sequenceIndex, int tick, float tickRate) {
  AnimationSample sample;
  if (!model.loaded || model.bytes.empty()) {
    sample.reason = "animation model is not loaded";
    return sample;
  }
  if (sequenceIndex < 0 || static_cast<std::size_t>(sequenceIndex) >= model.sequences.size()) {
    sample.reason = "sequence index is outside the sequence table";
    return sample;
  }
  const auto& sequence = model.sequences[static_cast<std::size_t>(sequenceIndex)];
  sample.sequenceLabel = sequence.label;
  sample.looping = sequence.looping;
  sample.animDesc = sequence.animDesc;
  if (sequence.animDesc < 0 || static_cast<std::size_t>(sequence.animDesc) >= model.animations.size()) {
    sample.reason = "sequence has no animation binding";
    return sample;
  }
  const auto& anim = model.animations[static_cast<std::size_t>(sequence.animDesc)];
  sample.fps = anim.fps;
  sample.frameCount = anim.frameCount;
  if (tick < 0 || !(tickRate > 0.0f) || !std::isfinite(tickRate)) {
    sample.reason = "tick or tick rate is not usable";
    return sample;
  }
  if (anim.frameCount <= 0 || anim.frameCount > 100000) {
    sample.reason = "animation frame count is not usable";
    return sample;
  }
  if (!(anim.fps > 0.0f) || !std::isfinite(anim.fps)) {
    sample.reason = "animation rate is not usable";
    return sample;
  }
  const double cursor = static_cast<double>(tick) * static_cast<double>(anim.fps) / static_cast<double>(tickRate);
  const int frame = selectFrame(cursor, anim.frameCount, sequence.looping);
  sample.frame = frame;

  std::int32_t animTable = 0;
  readAt(model.bytes.data(), model.bytes.size(), 184, animTable);
  const auto animOffset = static_cast<std::size_t>(animTable) + static_cast<std::size_t>(sequence.animDesc) * kAnimStride;
  std::size_t dataOffset = 0;
  int localFrame = 0;
  if (!locateFrame(model.bytes.data(), model.bytes.size(), animOffset, anim, frame, dataOffset, localFrame, sample.reason)) {
    return sample;
  }

  sample.localPosition.assign(model.bones.size(), {});
  sample.localRotation.assign(model.bones.size(), {0.0f, 0.0f, 0.0f, 1.0f});
  for (std::size_t i = 0; i < model.bones.size(); ++i) {
    sample.localPosition[i] = model.bones[i].position;
    sample.localRotation[i] = model.bones[i].rotation;
  }
  std::vector<bool> seen(model.bones.size(), false);
  std::size_t boneCursor = dataOffset;
  for (std::size_t step = 0; step < model.bones.size(); ++step) {
    if (boneCursor + 4 > model.bytes.size()) {
      sample.reason = "animation block offset is outside the file";
      return sample;
    }
    const int boneIndex = model.bytes[boneCursor];
    std::int16_t next = 0;
    readAt(model.bytes.data(), model.bytes.size(), boneCursor + 2, next);
    if (boneIndex >= static_cast<int>(model.bones.size())) {
      sample.reason = "animation bone index is outside the skeleton";
      return sample;
    }
    if (seen[static_cast<std::size_t>(boneIndex)]) {
      sample.reason = "animation bone index repeats";
      return sample;
    }
    seen[static_cast<std::size_t>(boneIndex)] = true;
    if (!readBoneFrame(model.bytes.data(), model.bytes.size(), model.bones[static_cast<std::size_t>(boneIndex)],
        boneCursor, localFrame, sample.localPosition[static_cast<std::size_t>(boneIndex)],
        sample.localRotation[static_cast<std::size_t>(boneIndex)], sample.reason)) {
      return sample;
    }
    if (next == 0) break;
    if (next < 4) {
      sample.reason = next < 0 ? "animation bone link offset is negative" : "animation bone link offset is too small";
      return sample;
    }
    if (boneCursor + static_cast<std::size_t>(next) <= boneCursor) {
      sample.reason = "animation bone link offset is outside the file";
      return sample;
    }
    boneCursor += static_cast<std::size_t>(next);
    if (step + 1 == model.bones.size()) {
      sample.reason = "animation bone chain is longer than the skeleton";
      return sample;
    }
  }

  std::vector<std::int32_t> parents;
  parents.reserve(model.bones.size());
  for (const auto& bone : model.bones) parents.push_back(bone.parent);
  if (!composeLocalToModel(parents, sample.localPosition, sample.localRotation, sample.modelPosition, sample.reason)) {
    return sample;
  }
  sample.status = AnimationStatus::Ok;
  sample.reason.clear();
  return sample;
}

} // namespace tf2::native
