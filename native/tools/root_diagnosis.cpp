// Root diagnosis probe. See root_diagnosis.h for what each check answers.
//
// Every reference function below names the Source function it mirrors. They are
// written from the file format, not copied; review them against
// tf2_src/public/bone_setup.cpp, mathlib/mathlib_base.cpp and public/studio.h.

#include "root_diagnosis.h"

#include "animation_binding.h"
#include "animation_decoder.h"
#include "asset_root.h"
#include "model_loader.h"
#include "vpk_archive.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <string>
#include <vector>

namespace tf2::native::probe {
namespace {

// ---------------------------------------------------------------------------
// Source constants (public/studio.h). Sequence and animdesc flags share one set;
// the per-record channel flags are a separate 8-bit set.
// ---------------------------------------------------------------------------
constexpr std::int32_t kStudioDelta = 0x0004;
constexpr std::int32_t kStudioPost = 0x0010;
constexpr std::int32_t kStudioAllZeros = 0x0020;
constexpr std::int32_t kStudioCyclePose = 0x0080;
constexpr std::uint8_t kAnimRawPos = 0x01;
constexpr std::uint8_t kAnimRawRot = 0x02;
constexpr std::uint8_t kAnimAnimPos = 0x04;
constexpr std::uint8_t kAnimAnimRot = 0x08;
constexpr std::uint8_t kAnimDelta = 0x10;
constexpr std::uint8_t kAnimRawRot2 = 0x20;
constexpr std::int32_t kBoneFixedAlignment = 0x00100000;
constexpr int kChainEnd = 0xFF;

// studiohdr_t / mstudiobone_t / mstudioanimdesc_t / mstudioseqdesc_t layout (v48).
constexpr std::uint32_t kIdStudio = 0x54534449u;
constexpr std::size_t kHdrBytes = 408;
constexpr std::size_t kHdrLength = 76;
constexpr std::size_t kHdrBoneCount = 156;
constexpr std::size_t kHdrBoneIndex = 160;
constexpr std::size_t kHdrAnimCount = 180;
constexpr std::size_t kHdrAnimIndex = 184;
constexpr std::size_t kHdrSeqCount = 188;
constexpr std::size_t kHdrSeqIndex = 192;
constexpr std::size_t kBoneStride = 216;
constexpr std::size_t kBonePos = 32;
constexpr std::size_t kBoneQuat = 44;
constexpr std::size_t kBoneRot = 60;
constexpr std::size_t kBonePosScale = 72;
constexpr std::size_t kBoneRotScale = 84;
constexpr std::size_t kBonePoseToBone = 96;
constexpr std::size_t kBoneAlignment = 144;
constexpr std::size_t kBoneFlags = 160;
constexpr std::size_t kAnimStride = 100;
constexpr std::size_t kSeqStride = 212;

constexpr double kPi = 3.14159265358979323846;
constexpr double kFltEpsilon = 1.1920928955078125e-07;

using Bytes = std::vector<std::uint8_t>;
using Vec3 = std::array<double, 3>;
using Quat = std::array<double, 4>;   // x y z w
using Mat34 = std::array<double, 12>; // row-major, v' = M v

constexpr Quat kIdentity{{0.0, 0.0, 0.0, 1.0}};
constexpr Vec3 kZero{{0.0, 0.0, 0.0}};

double toDeg(double r) { return r * 180.0 / kPi; }

// ---------------------------------------------------------------------------
// strings / bytes
// ---------------------------------------------------------------------------
std::string lower(std::string s) {
  for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}
bool startsWith(const std::string& s, const std::string& p) {
  return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}
bool contains(const std::string& s, const std::string& p) { return s.find(p) != std::string::npos; }

std::vector<std::string> splitList(const std::string& s) {
  std::vector<std::string> out;
  std::size_t start = 0;
  while (start <= s.size()) {
    const std::size_t comma = s.find(',', start);
    const std::string piece = s.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
    if (!piece.empty()) out.push_back(piece);
    if (comma == std::string::npos) break;
    start = comma + 1;
  }
  return out;
}

template <typename T>
bool rd(const Bytes& b, std::size_t at, T& out) {
  if (at > b.size() || sizeof(T) > b.size() - at) return false;
  std::memcpy(&out, b.data() + at, sizeof(T));
  return true;
}
double rdF(const Bytes& b, std::size_t at) {
  float f = 0.0f;
  rd(b, at, f);
  return static_cast<double>(f);
}
Vec3 rdVec3(const Bytes& b, std::size_t at) {
  return {{rdF(b, at), rdF(b, at + 4), rdF(b, at + 8)}};
}
Quat rdQuat(const Bytes& b, std::size_t at) {
  return {{rdF(b, at), rdF(b, at + 4), rdF(b, at + 8), rdF(b, at + 12)}};
}
bool fits(std::size_t at, std::size_t count, std::size_t stride, std::size_t size) {
  if (count != 0 && stride > static_cast<std::size_t>(-1) / count) return false;
  return at <= size && count * stride <= size - at;
}
std::string cstr(const Bytes& b, std::size_t at) {
  std::string s;
  for (std::size_t i = at; i < b.size() && i < at + 256; ++i) {
    if (b[i] == 0) break;
    if (b[i] < 32 || b[i] > 126) return {};
    s.push_back(static_cast<char>(b[i]));
  }
  return s;
}
std::string relStr(const Bytes& b, std::size_t base, std::int32_t rel) {
  if (rel <= 0) return {};
  const auto r = static_cast<std::size_t>(rel);
  if (base > b.size() || r > b.size() - base) return {};
  return cstr(b, base + r);
}

std::string fmtV(const Vec3& v) {
  char buf[96];
  std::snprintf(buf, sizeof(buf), "(%.3f,%.3f,%.3f)", v[0], v[1], v[2]);
  return buf;
}
std::string fmtQ(const Quat& q) {
  char buf[112];
  std::snprintf(buf, sizeof(buf), "(%.4f,%.4f,%.4f,%.4f)", q[0], q[1], q[2], q[3]);
  return buf;
}
std::string fmtDeg(const Vec3& radians) {
  char buf[96];
  std::snprintf(buf, sizeof(buf), "(%.2f,%.2f,%.2f)deg",
    toDeg(radians[0]), toDeg(radians[1]), toDeg(radians[2]));
  return buf;
}

// ---------------------------------------------------------------------------
// math (mathlib)
// ---------------------------------------------------------------------------
Vec3 add(const Vec3& a, const Vec3& b) { return {{a[0] + b[0], a[1] + b[1], a[2] + b[2]}}; }
Vec3 sub(const Vec3& a, const Vec3& b) { return {{a[0] - b[0], a[1] - b[1], a[2] - b[2]}}; }
Vec3 mul(const Vec3& a, double s) { return {{a[0] * s, a[1] * s, a[2] * s}}; }
double dot3(const Vec3& a, const Vec3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
Vec3 cross(const Vec3& a, const Vec3& b) {
  return {{a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]}};
}
double len3(const Vec3& a) { return std::sqrt(dot3(a, a)); }
Vec3 norm3(const Vec3& a) {
  const double l = len3(a);
  return l > 1e-12 ? mul(a, 1.0 / l) : kZero;
}

// Hamilton product p*q (QuaternionMult without the align step).
Quat qmul(const Quat& a, const Quat& b) {
  return {{a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1],
           a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
           a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3],
           a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2]}};
}
double qdot(const Quat& a, const Quat& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3]; }
Quat qneg(const Quat& q) { return {{-q[0], -q[1], -q[2], -q[3]}}; }
Quat qconj(const Quat& q) { return {{-q[0], -q[1], -q[2], q[3]}}; }
Quat qnorm(const Quat& q) {
  const double l = std::sqrt(qdot(q, q));
  if (!(l > 1e-12)) return kIdentity;
  return {{q[0] / l, q[1] / l, q[2] / l, q[3] / l}};
}
// QuaternionAlign: q flipped onto p's hemisphere.
Quat qalign(const Quat& p, const Quat& q) { return qdot(p, q) < 0.0 ? qneg(q) : q; }
// Rotation angle between two orientations, degrees, sign-agnostic.
double qangle(const Quat& a, const Quat& b) {
  double d = std::fabs(qdot(qnorm(a), qnorm(b)));
  if (d > 1.0) d = 1.0;
  return toDeg(2.0 * std::acos(d));
}
Vec3 qrot(const Quat& q, const Vec3& v) {
  const Vec3 u{{q[0], q[1], q[2]}};
  const Vec3 t = mul(cross(u, v), 2.0);
  return add(add(v, mul(t, q[3])), cross(u, t));
}
// AngleQuaternion(RadianEuler): x = roll about X, y = pitch about Y, z = yaw
// about Z; equal to qz * qy * qx (the self-test asserts that identity).
Quat eulerQ(const Vec3& e) {
  const double sr = std::sin(e[0] * 0.5), cr = std::cos(e[0] * 0.5);
  const double sp = std::sin(e[1] * 0.5), cp = std::cos(e[1] * 0.5);
  const double sy = std::sin(e[2] * 0.5), cy = std::cos(e[2] * 0.5);
  return {{sr * cp * cy - cr * sp * sy,
           cr * sp * cy + sr * cp * sy,
           cr * cp * sy - sr * sp * cy,
           cr * cp * cy + sr * sp * sy}};
}
// QuaternionScale: scales the rotation ANGLE by t and keeps the sign of w.
Quat qscale(const Quat& p, double t) {
  double sinom = std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
  if (sinom > 1.0) sinom = 1.0;
  const double sinsom = std::sin(std::asin(sinom) * t);
  const double k = sinsom / (sinom + kFltEpsilon);
  double r = 1.0 - sinsom * sinsom;
  if (r < 0.0) r = 0.0;
  r = std::sqrt(r);
  return {{p[0] * k, p[1] * k, p[2] * k, p[3] < 0.0 ? -r : r}};
}
// QuaternionMult: aligns q to p, then p*q.
Quat sourceMult(const Quat& p, const Quat& q) { return qmul(p, qalign(p, q)); }
// QuaternionSM(s, p, q) = normalize(scale(p, s) * q)  -- SlerpBones, delta, no STUDIO_POST
Quat qSM(double s, const Quat& p, const Quat& q) { return qnorm(sourceMult(qscale(p, s), q)); }
// QuaternionMA(p, s, q) = normalize(p * scale(q, s))  -- SlerpBones, delta, STUDIO_POST
Quat qMA(const Quat& p, double s, const Quat& q) { return qnorm(sourceMult(p, qscale(q, s))); }

// QuaternionMatrix + position, row-major 3x4.
Mat34 qmat(const Quat& q, const Vec3& t) {
  const double x = q[0], y = q[1], z = q[2], w = q[3];
  return {{1.0 - 2.0 * (y * y + z * z), 2.0 * (x * y - z * w), 2.0 * (x * z + y * w), t[0],
           2.0 * (x * y + z * w), 1.0 - 2.0 * (x * x + z * z), 2.0 * (y * z - x * w), t[1],
           2.0 * (x * z - y * w), 2.0 * (y * z + x * w), 1.0 - 2.0 * (x * x + y * y), t[2]}};
}
// ConcatTransforms: out(v) = a(b(v)).
Mat34 mmul(const Mat34& a, const Mat34& b) {
  Mat34 o{};
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 3; ++c) {
      double s = 0.0;
      for (int k = 0; k < 3; ++k) s += a[r * 4 + k] * b[k * 4 + c];
      o[r * 4 + c] = s;
    }
    double t = a[r * 4 + 3];
    for (int k = 0; k < 3; ++k) t += a[r * 4 + k] * b[k * 4 + 3];
    o[r * 4 + 3] = t;
  }
  return o;
}
Mat34 rigidInverse(const Mat34& m) {
  Mat34 o{};
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 3; ++c) o[r * 4 + c] = m[c * 4 + r];
  }
  for (int r = 0; r < 3; ++r) {
    o[r * 4 + 3] = -(o[r * 4 + 0] * m[3] + o[r * 4 + 1] * m[7] + o[r * 4 + 2] * m[11]);
  }
  return o;
}
struct IdentityError { double rot = 0.0; double trans = 0.0; };
IdentityError identityError(const Mat34& m) {
  IdentityError e;
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 3; ++c) {
      const double want = r == c ? 1.0 : 0.0;
      e.rot = (std::max)(e.rot, std::fabs(m[r * 4 + c] - want));
    }
    e.trans = (std::max)(e.trans, std::fabs(m[r * 4 + 3]));
  }
  return e;
}

// float16 (public/mathlib/float16.h): exponent 31 is +-65504 or 0 (NaN), exponent
// 0 is a denormal (mantissa / 1024 * 2^-14).
double sourceFloat16(std::uint16_t h) {
  const unsigned sign = (h >> 15) & 1u;
  const unsigned exponent = (h >> 10) & 31u;
  const unsigned mantissa = h & 0x3ffu;
  double v = 0.0;
  if (exponent == 31u) {
    v = mantissa == 0 ? 65504.0 : 0.0;
  } else if (exponent == 0u) {
    v = (static_cast<double>(mantissa) / 1024.0) * (1.0 / 16384.0);
  } else {
    v = std::ldexp(1.0 + static_cast<double>(mantissa) / 1024.0, static_cast<int>(exponent) - 15);
  }
  return sign ? -v : v;
}
// Quaternion48: x:16 y:16 z:15 wneg:1.
Quat quat48(std::uint16_t xw, std::uint16_t yw, std::uint16_t zw) {
  const double x = (static_cast<int>(xw) - 32768) * (1.0 / 32768.5);
  const double y = (static_cast<int>(yw) - 32768) * (1.0 / 32768.5);
  const double z = (static_cast<int>(zw & 0x7fffu) - 16384) * (1.0 / 16384.5);
  const double s = 1.0 - x * x - y * y - z * z;
  double w = s > 0.0 ? std::sqrt(s) : 0.0;
  if ((zw & 0x8000u) != 0) w = -w;
  return {{x, y, z, w}};
}
// Quaternion64: x:21 y:21 z:21 wneg:1.
Quat quat64(std::uint64_t packed) {
  const double x = static_cast<double>(static_cast<std::int64_t>(packed & 0x1fffffu) - 1048576) * (1.0 / 1048576.5);
  const double y = static_cast<double>(static_cast<std::int64_t>((packed >> 21) & 0x1fffffu) - 1048576) * (1.0 / 1048576.5);
  const double z = static_cast<double>(static_cast<std::int64_t>((packed >> 42) & 0x1fffffu) - 1048576) * (1.0 / 1048576.5);
  const double s = 1.0 - x * x - y * y - z * z;
  double w = s > 0.0 ? std::sqrt(s) : 0.0;
  if ((packed >> 63) != 0) w = -w;
  return {{x, y, z, w}};
}

// ---------------------------------------------------------------------------
// raw model (the reference's own parse; independent of the product decoder)
// ---------------------------------------------------------------------------
struct RawBone {
  std::string name;
  std::int32_t parent = -1;
  Vec3 pos{};
  Quat quat = kIdentity;
  Vec3 rot{};
  Vec3 posScale{};
  Vec3 rotScale{};
  Mat34 poseToBone{};
  Quat alignment = kIdentity;
  std::int32_t flags = 0;
};

struct RawAnimDesc {
  std::string name;
  float fps = 0.0f;
  std::int32_t flags = 0;
  std::int32_t numFrames = 0;
  std::int32_t animBlock = 0;
  std::int32_t animIndex = 0;
  std::int32_t sectionIndex = 0;
  std::int32_t sectionFrames = 0;
  std::size_t offset = 0;
};

struct RawModel {
  bool ok = false;
  std::string reason;
  Bytes bytes;
  std::vector<RawBone> bones;
  std::vector<RawAnimDesc> anims;
};

bool parseRaw(const Bytes& bytes, RawModel& out) {
  out = RawModel{};
  out.bytes = bytes;
  const auto fail = [&out](const char* why) { out.ok = false; out.reason = why; return false; };
  if (out.bytes.size() < kHdrBytes) return fail("shorter than the studio header");
  const Bytes& b = out.bytes;
  std::uint32_t id = 0;
  std::int32_t version = 0, length = 0;
  rd(b, 0, id);
  rd(b, 4, version);
  rd(b, kHdrLength, length);
  if (id != kIdStudio) return fail("signature is not IDST");
  if (version < 44 || version > 49) return fail("version outside 44-49");
  if (length <= 0 || static_cast<std::size_t>(length) > b.size()) return fail("declared length exceeds the file");
  std::int32_t boneCount = 0, boneIndex = 0, animCount = 0, animIndex = 0;
  rd(b, kHdrBoneCount, boneCount);
  rd(b, kHdrBoneIndex, boneIndex);
  rd(b, kHdrAnimCount, animCount);
  rd(b, kHdrAnimIndex, animIndex);
  if (boneCount < 0 || boneCount > 1024 || animCount < 0 || animCount > 8192 || boneIndex < 0 || animIndex < 0) {
    return fail("bone/anim counts outside the safe range");
  }
  if (!fits(static_cast<std::size_t>(boneIndex), static_cast<std::size_t>(boneCount), kBoneStride, b.size())) {
    return fail("bone table outside the file");
  }
  if (!fits(static_cast<std::size_t>(animIndex), static_cast<std::size_t>(animCount), kAnimStride, b.size())) {
    return fail("animdesc table outside the file");
  }
  for (std::int32_t i = 0; i < boneCount; ++i) {
    const std::size_t off = static_cast<std::size_t>(boneIndex) + static_cast<std::size_t>(i) * kBoneStride;
    RawBone bone;
    std::int32_t nameIndex = 0;
    rd(b, off, nameIndex);
    rd(b, off + 4, bone.parent);
    if (bone.parent < -1 || bone.parent >= i) return fail("bone parent is not an earlier bone");
    bone.name = relStr(b, off, nameIndex);
    bone.pos = rdVec3(b, off + kBonePos);
    bone.quat = rdQuat(b, off + kBoneQuat);
    bone.rot = rdVec3(b, off + kBoneRot);
    bone.posScale = rdVec3(b, off + kBonePosScale);
    bone.rotScale = rdVec3(b, off + kBoneRotScale);
    for (std::size_t k = 0; k < 12; ++k) bone.poseToBone[k] = rdF(b, off + kBonePoseToBone + k * 4);
    bone.alignment = rdQuat(b, off + kBoneAlignment);
    rd(b, off + kBoneFlags, bone.flags);
    out.bones.push_back(std::move(bone));
  }
  for (std::int32_t i = 0; i < animCount; ++i) {
    const std::size_t off = static_cast<std::size_t>(animIndex) + static_cast<std::size_t>(i) * kAnimStride;
    RawAnimDesc a;
    std::int32_t nameIndex = 0;
    rd(b, off + 4, nameIndex);
    rd(b, off + 8, a.fps);
    rd(b, off + 12, a.flags);
    rd(b, off + 16, a.numFrames);
    rd(b, off + 52, a.animBlock);
    rd(b, off + 56, a.animIndex);
    rd(b, off + 80, a.sectionIndex);
    rd(b, off + 84, a.sectionFrames);
    a.name = relStr(b, off, nameIndex);
    a.offset = off;
    out.anims.push_back(std::move(a));
  }
  out.ok = true;
  return true;
}

// ---------------------------------------------------------------------------
// reference sampler (Source semantics, integer frame, s = 0)
// ---------------------------------------------------------------------------

// ExtractAnimValue (single value): walk the RLE runs; inside a run's valid span
// read value[k], past it hold the run's last valid value.
bool extractAnimValue(const Bytes& b, std::size_t valuesAt, int frame, double scale,
    double& out, std::string& reason) {
  std::size_t at = valuesAt;
  int k = frame;
  for (int guard = 0; guard < 100000; ++guard) {
    if (at + 2 > b.size()) { reason = "anim value run outside the file"; return false; }
    const int valid = b[at];
    const int total = b[at + 1];
    if (total == 0) { reason = "anim value run with zero span (Source asserts here)"; return false; }
    if (total <= k) {
      k -= total;
      at += static_cast<std::size_t>(valid + 1) * 2;
      continue;
    }
    if (valid == 0) { reason = "anim value run with no samples"; return false; }
    const std::size_t entry = valid > k ? static_cast<std::size_t>(k + 1) : static_cast<std::size_t>(valid);
    std::int16_t raw = 0;
    if (!rd(b, at + entry * 2, raw)) { reason = "anim value outside the file"; return false; }
    out = static_cast<double>(raw) * scale;
    return true;
  }
  reason = "anim value runs do not terminate";
  return false;
}

struct RefBone {
  bool inChain = false;
  std::uint8_t flags = 0;
  Vec3 pos{};
  Quat rot = kIdentity;
  bool eulerChannel = false;  // ANIMROT: channel holds value * rotscale, before any base
  Vec3 channel{};
};

struct RefSample {
  bool ok = false;
  std::string reason;
  int frame = 0;
  int dataFrame = 0;
  int section = -1;
  bool noData = false;
  bool unsortedChain = false;
  std::size_t mixedLayout = 0;
  std::size_t chainLength = 0;
  std::vector<RefBone> bones;
};

// mstudioanimdesc_t::pAnim: with sections, the LAST frame of a long animation is
// stored in its own section (numframes / sectionframes + 1) as frame 0.
// REVIEW against studio.cpp; the C4 line reports the section it used.
bool locateFrame(const RawModel& m, const RawAnimDesc& a, int frame, std::size_t& at,
    int& dataFrame, int& section, bool& noData, std::string& reason) {
  dataFrame = frame;
  section = -1;
  noData = false;
  std::int32_t block = a.animBlock;
  std::int64_t index = a.animIndex;
  if (a.sectionFrames != 0) {
    if (a.sectionFrames < 0 || a.sectionIndex <= 0) { reason = "section table is unusable"; return false; }
    if (a.numFrames > a.sectionFrames && frame == a.numFrames - 1) {
      dataFrame = 0;
      section = a.numFrames / a.sectionFrames + 1;
    } else {
      section = frame / a.sectionFrames;
      dataFrame = frame - section * a.sectionFrames;
    }
    const std::size_t entry = a.offset + static_cast<std::size_t>(a.sectionIndex)
      + static_cast<std::size_t>(section) * 8;
    std::int32_t sBlock = 0, sIndex = 0;
    if (!rd(m.bytes, entry, sBlock) || !rd(m.bytes, entry + 4, sIndex)) {
      reason = "section table entry outside the file";
      return false;
    }
    block = sBlock;
    index = sIndex;
  }
  if (block == -1) { noData = true; return true; }
  if (block != 0) { reason = "external animblock (.ani) is not supported by the probe"; return false; }
  if (index < 0 || a.offset + static_cast<std::size_t>(index) + 4 > m.bytes.size()) {
    reason = "anim block outside the file";
    return false;
  }
  at = a.offset + static_cast<std::size_t>(index);
  return true;
}

// CalcBoneQuaternion + CalcBonePosition for one record (frame only, s = 0).
bool decodeRecord(const RawModel& m, const RawBone& base, std::size_t recordAt, int frame,
    RefBone& rb, std::size_t& mixedLayout, std::string& reason) {
  const Bytes& b = m.bytes;
  const std::uint8_t flags = b[recordAt + 1];
  const std::size_t data = recordAt + 4;
  rb.inChain = true;
  rb.flags = flags;
  const bool rawRot = (flags & kAnimRawRot) != 0;
  const bool rawRot2 = (flags & kAnimRawRot2) != 0;
  const bool animRot = (flags & kAnimAnimRot) != 0;
  const bool rawPos = (flags & kAnimRawPos) != 0;
  const bool animPos = (flags & kAnimAnimPos) != 0;
  const bool delta = (flags & kAnimDelta) != 0;
  // Source's accessors place pPosV after pRotV only, and pPos after the raw
  // quats only; a record mixing raw and animated channels is read differently
  // by Source and by a sequential reader. Counted, never guessed.
  if ((rawRot || rawRot2) && (animRot || animPos)) ++mixedLayout;
  if (animRot && rawPos) ++mixedLayout;
  if (rawRot && rawRot2) ++mixedLayout;

  if (rawRot) {
    std::uint16_t x = 0, y = 0, z = 0;
    if (!rd(b, data, x) || !rd(b, data + 2, y) || !rd(b, data + 4, z)) { reason = "quat48 outside the file"; return false; }
    rb.rot = quat48(x, y, z);
  } else if (rawRot2) {
    std::uint64_t packed = 0;
    if (!rd(b, data, packed)) { reason = "quat64 outside the file"; return false; }
    rb.rot = quat64(packed);
  } else if (!animRot) {
    rb.rot = delta ? kIdentity : base.quat;
  } else {
    Vec3 euler{};
    for (std::size_t j = 0; j < 3; ++j) {
      std::int16_t off = 0;
      if (!rd(b, data + j * 2, off)) { reason = "rot value pointer outside the file"; return false; }
      if (off > 0) {
        if (!extractAnimValue(b, data + static_cast<std::size_t>(off), frame, base.rotScale[j], euler[j], reason)) return false;
      }
    }
    rb.eulerChannel = true;
    rb.channel = euler;
    if (!delta) euler = add(euler, base.rot);
    rb.rot = eulerQ(euler);
  }

  if (rawPos) {
    const std::size_t at = data + (rawRot ? 6u : 0u) + (rawRot2 ? 8u : 0u);
    std::uint16_t x = 0, y = 0, z = 0;
    if (!rd(b, at, x) || !rd(b, at + 2, y) || !rd(b, at + 4, z)) { reason = "vector48 outside the file"; return false; }
    rb.pos = {{sourceFloat16(x), sourceFloat16(y), sourceFloat16(z)}};
  } else if (!animPos) {
    rb.pos = delta ? kZero : base.pos;
  } else {
    const std::size_t ptr = data + (animRot ? 6u : 0u);
    Vec3 pos{};
    for (std::size_t j = 0; j < 3; ++j) {
      std::int16_t off = 0;
      if (!rd(b, ptr + j * 2, off)) { reason = "pos value pointer outside the file"; return false; }
      if (off > 0) {
        if (!extractAnimValue(b, ptr + static_cast<std::size_t>(off), frame, base.posScale[j], pos[j], reason)) return false;
      }
    }
    if (!delta) pos = add(pos, base.pos);
    rb.pos = pos;
  }
  return true;
}

// CalcAnimation for one animdesc frame. `base` supplies pos/quat/rot/scales in
// animation-skeleton order (normally m.bones; the renderBase variant swaps in
// the render model's bind). Bones outside the chain: identity/zero for a
// STUDIO_DELTA animdesc, the bind otherwise. The root is NOT special.
bool refSample(const RawModel& m, const std::vector<RawBone>& base, int animIndex, int frame, RefSample& out) {
  out = RefSample{};
  if (animIndex < 0 || static_cast<std::size_t>(animIndex) >= m.anims.size()) {
    out.reason = "animdesc index outside the table";
    return false;
  }
  const RawAnimDesc& a = m.anims[static_cast<std::size_t>(animIndex)];
  const int frames = (std::max)(1, a.numFrames);
  out.frame = (std::min)((std::max)(frame, 0), frames - 1);
  const bool deltaDesc = (a.flags & kStudioDelta) != 0;
  out.bones.resize(base.size());
  for (std::size_t i = 0; i < base.size(); ++i) {
    out.bones[i].pos = deltaDesc ? kZero : base[i].pos;
    out.bones[i].rot = deltaDesc ? kIdentity : base[i].quat;
  }
  std::size_t at = 0;
  if (!locateFrame(m, a, out.frame, at, out.dataFrame, out.section, out.noData, out.reason)) return false;
  if (out.noData) { out.ok = true; return true; }
  int previous = -1;
  for (std::size_t step = 0; step <= base.size(); ++step) {
    if (at + 4 > m.bytes.size()) { out.reason = "anim record outside the file"; return false; }
    const int bone = m.bytes[at];
    std::int16_t next = 0;
    rd(m.bytes, at + 2, next);
    if (bone == kChainEnd && base.size() < static_cast<std::size_t>(kChainEnd)) break;
    if (bone >= static_cast<int>(base.size())) { out.reason = "anim record bone outside the skeleton"; return false; }
    if (bone <= previous) out.unsortedChain = true;
    previous = bone;
    ++out.chainLength;
    if (!decodeRecord(m, base[static_cast<std::size_t>(bone)], at, out.dataFrame,
        out.bones[static_cast<std::size_t>(bone)], out.mixedLayout, out.reason)) return false;
    if (next == 0) break;
    if (next < 4) { out.reason = "anim record link is too small"; return false; }
    at += static_cast<std::size_t>(next);
  }
  out.ok = true;
  return true;
}

// ---------------------------------------------------------------------------
// poses
// ---------------------------------------------------------------------------
struct Pose {
  std::vector<Vec3> pos;
  std::vector<Quat> rot;
};

Pose bindPose(const std::vector<RawBone>& bones) {
  Pose p;
  for (const auto& b : bones) {
    p.pos.push_back(b.pos);
    p.rot.push_back(b.quat);
  }
  return p;
}
Pose refPose(const RefSample& s) {
  Pose p;
  for (const auto& b : s.bones) {
    p.pos.push_back(b.pos);
    p.rot.push_back(b.rot);
  }
  return p;
}

bool productPose(const AnimationModel& model, int animDesc, int frame, float fps, Pose& out, std::string& reason) {
  // The product cursor is tick * fps / tickRate; passing tickRate == fps makes
  // the cursor the frame exactly (the probe samples integer frames only).
  if (!(fps > 0.0f)) { reason = "animdesc fps is not usable"; return false; }
  const AnimationSample s = sampleAnimDesc(model, animDesc, false, frame, fps);
  if (s.status != AnimationStatus::Ok) { reason = s.reason; return false; }
  out = Pose{};
  for (std::size_t i = 0; i < s.localRotation.size() && i < s.localPosition.size(); ++i) {
    const auto& q = s.localRotation[i];
    const auto& v = s.localPosition[i];
    out.rot.push_back({{q[0], q[1], q[2], q[3]}});
    out.pos.push_back({{v[0], v[1], v[2]}});
  }
  return true;
}

void compose(const std::vector<RawBone>& bones, const Pose& local, std::vector<Vec3>& mpos, std::vector<Quat>& mrot) {
  mpos.assign(bones.size(), kZero);
  mrot.assign(bones.size(), kIdentity);
  for (std::size_t i = 0; i < bones.size() && i < local.pos.size(); ++i) {
    const std::int32_t p = bones[i].parent;
    if (p < 0) {
      mpos[i] = local.pos[i];
      mrot[i] = qnorm(local.rot[i]);
    } else {
      const auto pi = static_cast<std::size_t>(p);
      mrot[i] = qnorm(qmul(mrot[pi], local.rot[i]));
      mpos[i] = add(mpos[pi], qrot(mrot[pi], local.pos[i]));
    }
  }
}

// SlerpBones for a STUDIO_DELTA sequence at weight s * boneweight.
Pose applyDelta(const Pose& base, const Pose& delta, const std::vector<double>& weights, bool post) {
  Pose out = base;
  for (std::size_t i = 0; i < base.rot.size() && i < delta.rot.size(); ++i) {
    const double s = i < weights.size() ? weights[i] : 1.0;
    if (!(s > 0.0)) continue;
    out.rot[i] = post ? qMA(base.rot[i], s, delta.rot[i]) : qSM(s, delta.rot[i], base.rot[i]);
    out.pos[i] = add(base.pos[i], mul(delta.pos[i], s));
  }
  return out;
}

struct PoseDiff {
  double maxAngle = 0.0;
  double maxPos = 0.0;
  std::size_t over1 = 0;
  std::size_t worst = 0;
};
PoseDiff diffPoses(const Pose& a, const Pose& b) {
  PoseDiff d;
  const std::size_t n = (std::min)(a.rot.size(), b.rot.size());
  for (std::size_t i = 0; i < n; ++i) {
    const double ang = qangle(a.rot[i], b.rot[i]);
    const double pos = len3(sub(a.pos[i], b.pos[i]));
    if (ang > 1.0) ++d.over1;
    if (ang > d.maxAngle) { d.maxAngle = ang; d.worst = i; }
    d.maxPos = (std::max)(d.maxPos, pos);
  }
  return d;
}

// ---------------------------------------------------------------------------
// uprightness
// ---------------------------------------------------------------------------
struct KeyBones {
  int head = -1, pelvis = -1, footL = -1, footR = -1, handL = -1, handR = -1, spine0 = -1;
  bool measurable() const { return head >= 0 && pelvis >= 0 && (footL >= 0 || footR >= 0); }
};

KeyBones findKeys(const std::vector<RawBone>& bones) {
  std::vector<std::string> names;
  for (const auto& b : bones) names.push_back(lower(b.name));
  const auto exact = [&names](const char* n) {
    for (std::size_t i = 0; i < names.size(); ++i) if (names[i] == n) return static_cast<int>(i);
    return -1;
  };
  const auto sub = [&names](std::initializer_list<const char*> must, std::initializer_list<const char*> mustNot) {
    for (std::size_t i = 0; i < names.size(); ++i) {
      bool ok = true;
      for (const char* m : must) if (!contains(names[i], m)) ok = false;
      for (const char* m : mustNot) if (contains(names[i], m)) ok = false;
      if (ok) return static_cast<int>(i);
    }
    return -1;
  };
  KeyBones k;
  k.head = exact("bip_head");
  if (k.head < 0) k.head = sub({"head"}, {"headgear", "hat", "jiggle"});
  k.pelvis = exact("bip_pelvis");
  if (k.pelvis < 0) k.pelvis = sub({"pelvis"}, {});
  k.footL = exact("bip_foot_l");
  if (k.footL < 0) k.footL = sub({"foot", "_l"}, {});
  k.footR = exact("bip_foot_r");
  if (k.footR < 0) k.footR = sub({"foot", "_r"}, {});
  k.handL = exact("bip_hand_l");
  if (k.handL < 0) k.handL = sub({"hand", "_l"}, {});
  k.handR = exact("bip_hand_r");
  if (k.handR < 0) k.handR = sub({"hand", "_r"}, {});
  k.spine0 = exact("bip_spine_0");
  if (k.spine0 < 0) k.spine0 = sub({"spine"}, {});
  return k;
}

struct UpFrame {
  bool ok = false;
  std::string reason;
  Vec3 up{};
  double bindHeight = 0.0;
};

Vec3 footPoint(const std::vector<Vec3>& mpos, const KeyBones& k, const Vec3& up, bool lowest) {
  if (k.footL >= 0 && k.footR >= 0) {
    const Vec3& l = mpos[static_cast<std::size_t>(k.footL)];
    const Vec3& r = mpos[static_cast<std::size_t>(k.footR)];
    if (!lowest) return mul(add(l, r), 0.5);
    return dot3(l, up) < dot3(r, up) ? l : r;
  }
  return mpos[static_cast<std::size_t>(k.footL >= 0 ? k.footL : k.footR)];
}

// The up axis is MEASURED from the bind pose (head minus feet), not assumed to
// be +Z; the line reports it so a non-Z-up skeleton is visible at once.
UpFrame upFrame(const std::vector<RawBone>& bones, const KeyBones& k) {
  UpFrame f;
  if (!k.measurable()) { f.reason = "head/pelvis/foot bones not found"; return f; }
  std::vector<Vec3> mpos;
  std::vector<Quat> mrot;
  compose(bones, bindPose(bones), mpos, mrot);
  const Vec3 feet = footPoint(mpos, k, kZero, false);
  const Vec3 d = sub(mpos[static_cast<std::size_t>(k.head)], feet);
  f.bindHeight = len3(d);
  if (!(f.bindHeight > 1.0)) { f.reason = "bind head and feet coincide"; return f; }
  f.up = norm3(d);
  f.ok = true;
  return f;
}

enum class Gate { None, Upright, Run, Crouch, Delta, DeltaAir };
const char* gateName(Gate g) {
  switch (g) {
    case Gate::Upright: return "upright";
    case Gate::Run: return "run";
    case Gate::Crouch: return "crouch";
    case Gate::Delta: return "delta";
    case Gate::DeltaAir: return "delta-air";
    default: return "none";
  }
}

struct Options {
  std::string tfRoot;
  std::vector<std::string> models;
  std::string upMode = "z";  // z (default, ruling 2026-11-24: Source model space is Z-up) | bind | mesh
  std::vector<std::string> sequences{"auto"};
  std::vector<std::string> frames{"0", "mid", "last"};
  std::vector<std::string> uprightPrefixes{"stand_", "run_", "walk_"};
  std::string baseLabel;
  double minUp = 0.8;
  double maxDevDeg = 15.0;
  bool quiet = false;
  bool dumpBones = false;
  bool requireProduct = false;
};

Gate classify(const std::string& label, std::int32_t seqFlags, const Options& o) {
  const std::string l = lower(label);
  if ((seqFlags & kStudioDelta) != 0) {
    // Airborne / swimming / losing-team layers tuck the legs or slump on purpose:
    // the body's extent along up is not a standing height. They keep the order
    // and direction checks and drop the height one (2026-11-24 ruling, first seen
    // on scout jump_double_loser frame 17: upDot 0.90, height 0.59).
    if (contains(l, "jump") || contains(l, "air") || contains(l, "swim") || contains(l, "loser")
        || contains(l, "land")) return Gate::DeltaAir;
    return Gate::Delta;
  }
  if (l == "ref" || l == "reference" || startsWith(l, "ref_")) return Gate::Upright;
  // *_LOSER is the losing team's slumped idle/run: a real pose that is not
  // upright by design, so it is reported like air/swim and never gated.
  if (contains(l, "loser")) return Gate::None;
  if (startsWith(l, "crouch")) return Gate::Crouch;
  // Running leans the torso and lifts a foot: its own, looser band.
  if (startsWith(l, "run_") || startsWith(l, "walk_")) return Gate::Run;
  for (const auto& p : o.uprightPrefixes) if (startsWith(l, lower(p))) return Gate::Upright;
  return Gate::None;
}

struct Upright {
  bool measured = false;
  bool pass = false;
  double upDot = 0.0;
  double heightRatio = 0.0;
  bool ordered = false;
};

Upright measure(const std::vector<Vec3>& mpos, const KeyBones& k, const UpFrame& f, Gate gate, double minUp) {
  Upright u;
  if (!f.ok || !k.measurable() || mpos.empty()) return u;
  u.measured = true;
  const Vec3& head = mpos[static_cast<std::size_t>(k.head)];
  const Vec3& pelvis = mpos[static_cast<std::size_t>(k.pelvis)];
  const Vec3 foot = footPoint(mpos, k, f.up, true);
  const double hh = dot3(head, f.up), ph = dot3(pelvis, f.up), fh = dot3(foot, f.up);
  u.ordered = hh > ph && ph > fh;
  u.upDot = dot3(norm3(sub(head, pelvis)), f.up);
  u.heightRatio = (hh - fh) / f.bindHeight;
  double needUp = 0.3, needHeight = 0.5;
  // Calibrated on the 2026-11-24 --up z census (280 misses over three models):
  // standing idles land at 0.79..0.85 of the fully extended bind height (scout
  // stands lowest), runs at 0.70..0.85. 0.85 was rejecting real stands.
  if (gate == Gate::Upright) { needUp = minUp; needHeight = 0.75; }
  // Run: 0.6 (~53 deg from vertical) separates a running lean (measured floor
  // 0.69, scout) from a body lying down (~0) with margin, instead of sitting on
  // the measured minimum.
  else if (gate == Gate::Run) { needUp = 0.6; needHeight = 0.70; }
  else if (gate == Gate::Crouch) { needUp = 0.5; needHeight = 0.5; }
  else if (gate == Gate::Delta) { needUp = 0.3; needHeight = 0.6; }
  else if (gate == Gate::DeltaAir) { needUp = 0.3; needHeight = -1.0; }
  u.pass = u.ordered && u.upDot >= needUp && u.heightRatio >= needHeight;
  return u;
}

std::string fmtUpright(const Upright& u) {
  if (!u.measured) return "-";
  char buf[48];
  std::snprintf(buf, sizeof(buf), "%s:%.2f/%.2f", u.pass ? "U" : "X", u.upDot, u.heightRatio);
  return buf;
}

const char* deviationClass(double deg, double maxDev) {
  if (deg <= maxDev) return "ok";
  if (deg >= 150.0) return "near180-sign-or-double";
  if (deg >= 75.0 && deg <= 105.0) return "near90-axis";
  return "large";
}

// ---------------------------------------------------------------------------
// variants (C5)
// ---------------------------------------------------------------------------
enum Variant {
  kVSource, kVRenderBase, kVProduct, kVProductSrcRoot, kVBindRoot, kVBindRotAnimHeight,
  kVConjRoot, kVChannelOnlyRoot, kVDoubleBaseRoot, kVBindPremulRoot, kVariantCount
};
const char* const kVariantNames[kVariantCount] = {
  "source", "renderBase", "product", "productSrcRoot", "bindRoot", "bindRotAnimHeight",
  "conjRoot", "channelOnlyRoot", "doubleBaseRoot", "bindPremulRoot"
};

struct Tally {
  std::size_t measured = 0;
  std::size_t pass = 0;
  void add(const Upright& u) {
    if (!u.measured) return;
    ++measured;
    if (u.pass) ++pass;
  }
  bool all() const { return measured > 0 && pass == measured; }
};

struct ModelResult {
  bool loaded = false;
  bool c1ok = false;
  bool c2ok = false;
  bool c3ok = false;
  std::array<Tally, kVariantCount> variant{};
  Tally deltaSource, deltaAlt, deltaProduct;
  std::size_t productSamples = 0;
  std::size_t productMatches = 0;
  std::size_t h1Tested = 0;
  std::size_t h1Hits = 0;
  std::size_t h1RootHits = 0;
  std::size_t seqFlagMismatch = 0;
  std::size_t mixedLayout = 0;
  std::size_t unsortedChains = 0;
  std::size_t refFailures = 0;
  std::size_t lastFrameSections = 0;
  // C8: the MDL header's own hull, read independently of every rotation rule.
  bool hullOk = false;
  std::size_t hullMeasured = 0;
  std::size_t hullSrcInside = 0;
  std::size_t hullProdInside = 0;
  std::size_t hullSrcEyeOk = 0;   // reused: samples whose hull span >= kMinHullSpan
  std::size_t hullProdEyeOk = 0;
};

Bytes readAssetBytes(const AssetRoot& root, const std::string& path) {
  const std::string normalized = normalizeModelPath(path);
  std::vector<std::filesystem::path> holders;
  const VpkArchiveSet& archives = root.archives();
  archives.collectContaining(normalized, holders);
  for (const auto& holder : holders) {
    const VpkArchive* archive = archives.find(holder);
    if (archive == nullptr) continue;
    std::string error;
    auto bytes = archive->read(normalized, &error);
    if (!bytes.empty()) return bytes;
  }
  std::error_code ec;
  const auto loose = root.resolve(std::filesystem::path(path));
  if (std::filesystem::is_regular_file(loose, ec) && !ec) {
    std::ifstream stream(loose, std::ios::binary);
    if (stream) return Bytes(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
  }
  return {};
}

std::vector<int> resolveFrames(const std::vector<std::string>& tokens, int numFrames) {
  const int n = (std::max)(1, numFrames);
  std::vector<int> out;
  for (const auto& t : tokens) {
    int f = 0;
    if (t == "mid") f = n / 2;
    else if (t == "last") f = n - 1;
    else f = std::atoi(t.c_str());
    f = (std::min)((std::max)(f, 0), n - 1);
    if (std::find(out.begin(), out.end(), f) == out.end()) out.push_back(f);
  }
  return out;
}

void printKeyBones(const std::string& where, const KeyBones& k, const std::vector<RawBone>& bones,
    const Pose& src, const Pose& prod) {
  const int keys[] = {k.pelvis, k.spine0, k.head, k.handL, k.handR};
  for (const int key : keys) {
    if (key < 0) continue;
    const auto i = static_cast<std::size_t>(key);
    if (i >= src.rot.size() || i >= prod.rot.size()) continue;
    std::printf("ROOTDIAG-BONES %s bone=%s src=%s prod=%s rotDiff=%.3fdeg posDiff=%.4f\n",
      where.c_str(), bones[i].name.c_str(), fmtQ(src.rot[i]).c_str(), fmtQ(prod.rot[i]).c_str(),
      qangle(src.rot[i], prod.rot[i]), len3(sub(src.pos[i], prod.pos[i])));
  }
}

// ---------------------------------------------------------------------------
// C8: studiohdr_t hull / eyeposition (offsets 80 / 104 / 116, v44-49)
// ---------------------------------------------------------------------------
// studiomdl writes the hull and the eye position into the header from the
// compiled reference skeleton in MODEL space. Nothing in this probe or in the
// product rotates them, so "do the head and feet land inside the hull, and is
// the head near the eye height" is a verdict on the pose that does not depend
// on any up-axis convention either side argued for.
constexpr std::size_t kHdrEyePosition = 80;
constexpr std::size_t kHdrHullMin = 104;
constexpr std::size_t kHdrHullMax = 116;
constexpr double kHullTolerance = 6.0;    // units: a hand or hat bone may poke past the hull
// The body's extent along the hull's own tall axis, as a share of the hull's
// length on that axis. Upright standing/running bodies measure 0.6..0.75; a
// body lying across the hull measures well under 0.3. Neither side's rotation
// rule enters this number: both the axis and its length come from the header.
constexpr double kMinHullSpan = 0.5;

struct StudioHull {
  bool ok = false;
  Vec3 min{};
  Vec3 max{};
  Vec3 eye{};
  int tallAxis = 2;
};

StudioHull readStudioHull(const Bytes& b) {
  StudioHull h;
  if (b.size() < kHdrBytes) return h;
  h.eye = rdVec3(b, kHdrEyePosition);
  h.min = rdVec3(b, kHdrHullMin);
  h.max = rdVec3(b, kHdrHullMax);
  double tallest = -1.0;
  for (int j = 0; j < 3; ++j) {
    const auto k = static_cast<std::size_t>(j);
    if (!std::isfinite(h.min[k]) || !std::isfinite(h.max[k]) || !(h.max[k] > h.min[k])) return h;
    if (h.max[k] - h.min[k] > tallest) { tallest = h.max[k] - h.min[k]; h.tallAxis = j; }
  }
  h.ok = true;
  return h;
}

bool insideHull(const StudioHull& h, const Vec3& v) {
  for (std::size_t j = 0; j < 3; ++j) {
    if (v[j] < h.min[j] - kHullTolerance || v[j] > h.max[j] + kHullTolerance) return false;
  }
  return true;
}

struct HullReading {
  bool measured = false;
  bool inside = false;
  double span = 0.0;     // (head - foot) along the hull tall axis / hull length on it
  bool spanOk = false;
  Vec3 head{};
  Vec3 foot{};
  Vec3 pelvis{};
};

HullReading hullReading(const std::vector<RawBone>& bones, const KeyBones& k, const Pose& pose,
    const StudioHull& hull) {
  HullReading r;
  if (!hull.ok || !k.measurable() || pose.rot.size() != bones.size()) return r;
  std::vector<Vec3> mp;
  std::vector<Quat> mr;
  compose(bones, pose, mp, mr);
  r.head = mp[static_cast<std::size_t>(k.head)];
  r.pelvis = mp[static_cast<std::size_t>(k.pelvis)];
  // The LOWEST foot along the hull's tall axis, as measure() does: a running
  // stride lifts one foot, and the feet midpoint would shorten the span.
  Vec3 tall{};
  tall[static_cast<std::size_t>(hull.tallAxis)] = 1.0;
  r.foot = footPoint(mp, k, tall, true);
  r.measured = true;
  r.inside = insideHull(hull, r.head) && insideHull(hull, r.pelvis) && insideHull(hull, r.foot);
  const auto axis = static_cast<std::size_t>(hull.tallAxis);
  const double length = hull.max[axis] - hull.min[axis];
  r.span = length > 0.0 ? (r.head[axis] - r.foot[axis]) / length : 0.0;
  r.spanOk = r.span >= kMinHullSpan;
  return r;
}

// ---------------------------------------------------------------------------
// per model
// ---------------------------------------------------------------------------
bool diagnoseModel(const AssetRoot& root, AnimationBindingCache& cache, const std::string& modelPath,
    const Options& o, ModelResult& res) {
  const char* model = modelPath.c_str();
  AnimationInputStatus status = AnimationInputStatus::ModelMissing;
  std::string why;
  const AnimationBinding* binding = cache.resolve(modelPath, status, why);
  if (binding == nullptr) {
    std::printf("ROOTDIAG-ERROR model=%s stage=binding status=%s reason=%s\n",
      model, animationInputStatusName(status), why.c_str());
    return false;
  }
  RawModel anim;
  if (!parseRaw(binding->anims.bytes, anim)) {
    std::printf("ROOTDIAG-ERROR model=%s stage=anim-parse reason=%s\n", model, anim.reason.c_str());
    return false;
  }
  RawModel render;
  if (!parseRaw(readAssetBytes(root, modelPath), render)) {
    std::printf("ROOTDIAG-ERROR model=%s stage=render-parse reason=%s\n", model, render.reason.c_str());
    return false;
  }
  // The animation source's OWN sequence table: the product's merged table keeps
  // the base model's flags (except LOOPING), and Source reads the seqdesc of
  // the model that owns the sequence.
  AnimationModel seqTable;
  if (decodeAnimationModel(binding->anims.bytes.data(), binding->anims.bytes.size(), seqTable) != AnimationStatus::Ok) {
    std::printf("ROOTDIAG-ERROR model=%s stage=anim-decode reason=%s\n", model, seqTable.reason.c_str());
    return false;
  }
  res.loaded = true;
  const StudioHull hull = readStudioHull(render.bytes);
  res.hullOk = hull.ok;
  if (hull.ok) {
    const char axisName[3] = {'x', 'y', 'z'};
    // eyeposition is printed for the record only: on the TF2 player models it is
    // stored along -X with z = 0 (measured 2026-11-24), i.e. not in the hull's
    // frame, so no verdict reads it.
    std::printf("ROOTDIAG-HULL model=%s hullMin=%s hullMax=%s eye(unused)=%s tallAxis=%c\n",
      model, fmtV(hull.min).c_str(), fmtV(hull.max).c_str(), fmtV(hull.eye).c_str(),
      axisName[hull.tallAxis]);
  } else {
    std::printf("ROOTDIAG-HULL model=%s unreadable\n", model);
  }
  std::printf("ROOTDIAG-MODEL model=%s animSource=%s separate=%d animBones=%zu renderBones=%zu animdescs=%zu sequences=%zu\n",
    model, binding->animPath.c_str(), binding->separateAnimSource ? 1 : 0, anim.bones.size(),
    render.bones.size(), anim.anims.size(), seqTable.sequences.size());

  std::vector<std::size_t> roots;
  for (std::size_t i = 0; i < anim.bones.size(); ++i) {
    if (anim.bones[i].parent >= 0) continue;
    roots.push_back(i);
    const RawBone& b = anim.bones[i];
    std::printf("ROOTDIAG-ROOT model=%s skeleton=anim bone=%zu name=%s pos=%s quat=%s rot=%s flags=0x%08x fixedAlignment=%d\n",
      model, i, b.name.c_str(), fmtV(b.pos).c_str(), fmtQ(b.quat).c_str(), fmtDeg(b.rot).c_str(),
      static_cast<unsigned>(b.flags), (b.flags & kBoneFixedAlignment) != 0 ? 1 : 0);
  }
  for (std::size_t i = 0; i < render.bones.size(); ++i) {
    if (render.bones[i].parent >= 0) continue;
    const RawBone& b = render.bones[i];
    std::printf("ROOTDIAG-ROOT model=%s skeleton=render bone=%zu name=%s pos=%s quat=%s rot=%s\n",
      model, i, b.name.c_str(), fmtV(b.pos).c_str(), fmtQ(b.quat).c_str(), fmtDeg(b.rot).c_str());
  }

  const KeyBones keys = findKeys(anim.bones);
  const KeyBones renderKeys = findKeys(render.bones);
  const auto nameOf = [&anim](int i) { return i >= 0 ? anim.bones[static_cast<std::size_t>(i)].name : std::string("-"); };
  std::printf("ROOTDIAG-KEYS model=%s head=%s pelvis=%s footL=%s footR=%s handL=%s handR=%s spine0=%s\n",
    model, nameOf(keys.head).c_str(), nameOf(keys.pelvis).c_str(), nameOf(keys.footL).c_str(),
    nameOf(keys.footR).c_str(), nameOf(keys.handL).c_str(), nameOf(keys.handR).c_str(), nameOf(keys.spine0).c_str());

  // ---- C1 ----
  const auto c1 = [&](const RawModel& m, const KeyBones& k, const char* which) {
    std::size_t bad = 0;
    double worst = 0.0;
    std::size_t worstBone = 0;
    bool keyBad = false;
    for (std::size_t i = 0; i < m.bones.size(); ++i) {
      const double a = qangle(eulerQ(m.bones[i].rot), m.bones[i].quat);
      if (a > worst) { worst = a; worstBone = i; }
      if (a <= 0.1) continue;
      ++bad;
      const int ii = static_cast<int>(i);
      if (m.bones[i].parent < 0 || ii == k.head || ii == k.pelvis || ii == k.spine0
          || ii == k.footL || ii == k.footR) keyBad = true;
    }
    std::printf("ROOTDIAG-C1 model=%s skeleton=%s bones=%zu over0.1deg=%zu worst=%.4fdeg@%s keyMismatch=%d\n",
      model, which, m.bones.size(), bad, worst,
      m.bones.empty() ? "-" : m.bones[worstBone].name.c_str(), keyBad ? 1 : 0);
    return !keyBad;
  };
  const bool c1Anim = c1(anim, keys, "anim");
  const bool c1Render = c1(render, renderKeys, "render");
  res.c1ok = c1Anim && c1Render;

  // ---- C2 ----
  {
    std::vector<Vec3> mpos;
    std::vector<Quat> mrot;
    compose(render.bones, bindPose(render.bones), mpos, mrot);
    std::size_t bad = 0;
    IdentityError worst;
    std::size_t worstBone = 0;
    for (std::size_t i = 0; i < render.bones.size(); ++i) {
      const IdentityError e = identityError(mmul(qmat(mrot[i], mpos[i]), render.bones[i].poseToBone));
      if (e.rot > 1e-3 || e.trans > 1e-2) ++bad;
      if (e.rot + e.trans > worst.rot + worst.trans) { worst = e; worstBone = i; }
    }
    res.c2ok = bad == 0;
    std::printf("ROOTDIAG-C2 model=%s bones=%zu bad=%zu worstRot=%.5f worstTrans=%.4f@%s %s\n",
      model, render.bones.size(), bad, worst.rot, worst.trans,
      render.bones.empty() ? "-" : render.bones[worstBone].name.c_str(), res.c2ok ? "PASS" : "FAIL");
  }

  // ---- C3 ----
  std::vector<int> animToRender(anim.bones.size(), -1);
  for (std::size_t a = 0; a < anim.bones.size(); ++a) {
    if (a < binding->animToModel.size() && binding->animToModel[a] >= 0
        && static_cast<std::size_t>(binding->animToModel[a]) < render.bones.size()) {
      animToRender[a] = binding->animToModel[a];
      continue;
    }
    const std::string n = lower(anim.bones[a].name);
    for (std::size_t j = 0; j < render.bones.size(); ++j) {
      if (lower(render.bones[j].name) == n) { animToRender[a] = static_cast<int>(j); break; }
    }
  }
  {
    std::size_t mapped = 0, posBad = 0, rotBad = 0, parentBad = 0, scaleBad = 0, skinBad = 0;
    double worstAngle = 0.0, worstPos = 0.0;
    std::size_t worstBone = 0;
    std::vector<Vec3> mpos;
    std::vector<Quat> mrot;
    compose(anim.bones, bindPose(anim.bones), mpos, mrot);
    IdentityError worstSkin;
    for (std::size_t a = 0; a < anim.bones.size(); ++a) {
      const int j = animToRender[a];
      if (j < 0) continue;
      ++mapped;
      const RawBone& ab = anim.bones[a];
      const RawBone& rb = render.bones[static_cast<std::size_t>(j)];
      const double ang = qangle(ab.quat, rb.quat);
      const double pd = len3(sub(ab.pos, rb.pos));
      if (ang > 0.1) ++rotBad;
      if (pd > 0.01) ++posBad;
      if (ang + pd > worstAngle + worstPos) { worstAngle = ang; worstPos = pd; worstBone = a; }
      const std::string ap = ab.parent >= 0 ? lower(anim.bones[static_cast<std::size_t>(ab.parent)].name) : "";
      const std::string rp = rb.parent >= 0 ? lower(render.bones[static_cast<std::size_t>(rb.parent)].name) : "";
      if (ap != rp) ++parentBad;
      if (len3(sub(ab.rotScale, rb.rotScale)) > 1e-6 || len3(sub(ab.posScale, rb.posScale)) > 1e-6) ++scaleBad;
      const IdentityError e = identityError(mmul(qmat(mrot[a], mpos[a]), rb.poseToBone));
      if (e.rot > 1e-3 || e.trans > 1e-2) ++skinBad;
      if (e.rot + e.trans > worstSkin.rot + worstSkin.trans) worstSkin = e;
      const int ai = static_cast<int>(a);
      if (!o.quiet && (ab.parent < 0 || ai == keys.pelvis || ai == keys.head) && (ang > 0.1 || pd > 0.01)) {
        std::printf("ROOTDIAG-C3-BONE model=%s bone=%s animPos=%s renderPos=%s animQuat=%s renderQuat=%s angle=%.3fdeg\n",
          model, ab.name.c_str(), fmtV(ab.pos).c_str(), fmtV(rb.pos).c_str(), fmtQ(ab.quat).c_str(),
          fmtQ(rb.quat).c_str(), ang);
      }
    }
    for (const auto& merged : binding->anims.sequences) {
      for (const auto& own : seqTable.sequences) {
        if (own.label != merged.label) continue;
        if ((own.flags & (kStudioDelta | kStudioPost)) != (merged.flags & (kStudioDelta | kStudioPost))) {
          ++res.seqFlagMismatch;
          if (!o.quiet && res.seqFlagMismatch <= 10) {
            std::printf("ROOTDIAG-C3-SEQFLAGS model=%s label=%s productFlags=0x%04x sourceFlags=0x%04x\n",
              model, own.label.c_str(), static_cast<unsigned>(merged.flags), static_cast<unsigned>(own.flags));
          }
        }
        break;
      }
    }
    res.c3ok = parentBad == 0 && skinBad == 0 && res.seqFlagMismatch == 0;
    std::printf("ROOTDIAG-C3 model=%s mapped=%zu/%zu rotOver0.1=%zu posOver0.01=%zu parentMismatch=%zu scaleMismatch=%zu worst=%s(%.3fdeg,%.4f) skinNotIdentity=%zu worstSkinRot=%.5f worstSkinTrans=%.4f seqFlagMismatch=%zu %s\n",
      model, mapped, anim.bones.size(), rotBad, posBad, parentBad, scaleBad,
      anim.bones.empty() ? "-" : anim.bones[worstBone].name.c_str(), worstAngle, worstPos,
      skinBad, worstSkin.rot, worstSkin.trans, res.seqFlagMismatch, res.c3ok ? "PASS" : "WARN");
  }

  // ---- reference frame ----
  UpFrame up = upFrame(anim.bones, keys);
  if (up.ok) {
    std::printf("ROOTDIAG-UP model=%s up=%s bindHeight=%.2f zAligned=%d\n",
      model, fmtV(up.up).c_str(), up.bindHeight, std::fabs(up.up[2]) > 0.9 ? 1 : 0);
  } else {
    std::printf("ROOTDIAG-UP model=%s unmeasurable reason=%s\n", model, up.reason.c_str());
  }

  // ---- C7: which frame is the MESH in? ----
  // The upright rule above measures along the BIND skeleton's own head-feet axis.
  // That is only the world's up if the mesh the product draws (skin = identity in
  // the bind pose) is in the same frame. This reads the LOD0 vertices the product
  // uploads and measures their head-weighted minus foot-weighted centroid, so
  // "bind is +Y" can be told apart from "the mesh is +Y" and from "the world is +Z".
  Vec3 meshUp{};
  bool meshUpOk = false;
  {
    const auto asset = ModelLoader::resolveAsset(root, modelPath);
    ModelInspection inspection;
    bool inspected = false;
    for (const auto& archivePath : asset.vpkArchives) {
      const VpkArchive* archive = root.archives().find(archivePath);
      if (archive == nullptr || !archive->contains(modelPath)) continue;
      inspection = ModelLoader::inspectVpk(*archive, modelPath);
      inspected = true;
      break;
    }
    if (!inspected) inspection = ModelLoader::inspect(asset.resources);
    std::string meshError;
    if (inspection.metadata.valid && ModelLoader::buildBindPoseMeshLod0(inspection.metadata, meshError)) {
      const auto& verts = inspection.metadata.bindPoseVertices;
      Vec3 lo{{1e30, 1e30, 1e30}}, hi{{-1e30, -1e30, -1e30}};
      Vec3 headSum{}, footSum{};
      double headN = 0.0, footN = 0.0;
      for (const auto& v : verts) {
        const Vec3 vp{{static_cast<double>(v.position[0]), static_cast<double>(v.position[1]),
                      static_cast<double>(v.position[2])}};
        for (std::size_t j = 0; j < 3; ++j) {
          lo[j] = (std::min)(lo[j], vp[j]);
          hi[j] = (std::max)(hi[j], vp[j]);
        }
        int dominant = -1;
        float best = 0.0f;
        for (std::size_t slot = 0; slot < 3 && slot < static_cast<std::size_t>(v.boneCount); ++slot) {
          if (v.weights[slot] > best) { best = v.weights[slot]; dominant = static_cast<int>(v.boneIndices[slot]); }
        }
        if (dominant >= 0 && dominant == renderKeys.head) { headSum = add(headSum, vp); headN += 1.0; }
        else if (dominant >= 0 && (dominant == renderKeys.footL || dominant == renderKeys.footR)) { footSum = add(footSum, vp); footN += 1.0; }
      }
      if (headN > 0.0 && footN > 0.0) {
        meshUp = norm3(sub(mul(headSum, 1.0 / headN), mul(footSum, 1.0 / footN)));
        meshUpOk = len3(meshUp) > 0.5;
      }
      const Vec3 ext = sub(hi, lo);
      const char* tallest = ext[2] >= ext[0] && ext[2] >= ext[1] ? "z" : (ext[1] >= ext[0] ? "y" : "x");
      std::printf("ROOTDIAG-MESH model=%s vertices=%zu bbox=%s..%s extent=%s tallestAxis=%s headVerts=%.0f footVerts=%.0f meshUp=%s skeletonUp=%s meshVsSkeleton=%.1fdeg\n",
        model, verts.size(), fmtV(lo).c_str(), fmtV(hi).c_str(), fmtV(ext).c_str(), tallest, headN, footN,
        meshUpOk ? fmtV(meshUp).c_str() : "-", up.ok ? fmtV(up.up).c_str() : "-",
        meshUpOk && up.ok ? toDeg(std::acos((std::max)(-1.0, (std::min)(1.0, dot3(meshUp, up.up))))) : -1.0);
    } else {
      std::printf("ROOTDIAG-MESH model=%s unmeasurable reason=%s\n", model,
        inspection.metadata.valid ? meshError.c_str() : "render model did not inspect");
    }
  }
  if (up.ok && o.upMode == "z") up.up = {{0.0, 0.0, 1.0}};
  else if (up.ok && o.upMode == "mesh" && meshUpOk) up.up = meshUp;
  std::printf("ROOTDIAG-UPMODE model=%s mode=%s up=%s%s\n", model, o.upMode.c_str(), fmtV(up.up).c_str(),
    (o.upMode == "mesh" && !meshUpOk) ? " (mesh unmeasurable, fell back to bind)" : "");

  std::vector<RawBone> renderBase = anim.bones;
  for (std::size_t a = 0; a < anim.bones.size(); ++a) {
    const int j = animToRender[a];
    if (j < 0) continue;
    const RawBone& rb = render.bones[static_cast<std::size_t>(j)];
    renderBase[a].pos = rb.pos;
    renderBase[a].quat = rb.quat;
    renderBase[a].rot = rb.rot;  // scales stay: they are the data's compression scales
  }

  // Base pose for delta layers.
  Pose deltaBase = bindPose(anim.bones);
  std::string deltaBaseLabel = "bind";
  for (const auto& seq : seqTable.sequences) {
    const std::string l = lower(seq.label);
    const bool wanted = o.baseLabel.empty() ? startsWith(l, "stand_") : l == lower(o.baseLabel);
    if (!wanted || (seq.flags & kStudioDelta) != 0 || seq.animDesc < 0) continue;
    RefSample s;
    if (refSample(anim, anim.bones, seq.animDesc, 0, s)) {
      deltaBase = refPose(s);
      deltaBaseLabel = seq.label;
      break;
    }
  }

  // ---- select sequences ----
  std::vector<std::size_t> selected;
  const bool all = o.sequences.size() == 1 && o.sequences[0] == "all";
  const bool autoSel = o.sequences.size() == 1 && o.sequences[0] == "auto";
  for (std::size_t i = 0; i < seqTable.sequences.size(); ++i) {
    const auto& seq = seqTable.sequences[i];
    bool take = all;
    if (autoSel) {
      const Gate g = classify(seq.label, seq.flags, o);
      take = g == Gate::Upright || g == Gate::Run || g == Gate::Crouch
        || ((g == Gate::Delta || g == Gate::DeltaAir) && seq.grid.valid && seq.grid.cells.size() > 1);
    } else if (!all) {
      for (const auto& token : o.sequences) {
        const bool numeric = !token.empty() && std::isdigit(static_cast<unsigned char>(token[0])) != 0;
        if ((numeric && static_cast<std::size_t>(std::atoi(token.c_str())) == i) || lower(token) == lower(seq.label)) take = true;
      }
    }
    if (take) selected.push_back(i);
  }

  // ---- samples ----
  for (const std::size_t si : selected) {
    const AnimationSequence& seq = seqTable.sequences[si];
    const Gate gate = classify(seq.label, seq.flags, o);
    std::vector<std::int32_t> cells;
    if (seq.grid.valid && !seq.grid.cells.empty()) {
      for (const auto c : seq.grid.cells) {
        if (std::find(cells.begin(), cells.end(), static_cast<std::int32_t>(c)) == cells.end()) cells.push_back(c);
      }
    } else if (seq.animDesc >= 0) {
      cells.push_back(seq.animDesc);
    }
    std::vector<double> weights;
    for (const float w : seq.weights.weights) weights.push_back(static_cast<double>(w));
    if (weights.size() != anim.bones.size()) weights.assign(anim.bones.size(), 1.0);
    const bool post = (seq.flags & kStudioPost) != 0;

    for (std::size_t ci = 0; ci < cells.size(); ++ci) {
      const int ad = cells[ci];
      if (ad < 0 || static_cast<std::size_t>(ad) >= anim.anims.size()) continue;
      const RawAnimDesc& desc = anim.anims[static_cast<std::size_t>(ad)];
      for (const int frame : resolveFrames(o.frames, desc.numFrames)) {
        char whereBuf[320];
        std::snprintf(whereBuf, sizeof(whereBuf), "model=%s seq=%zu label=%s cell=%zu anim=%d:%s frame=%d/%d",
          model, si, seq.label.c_str(), ci, ad, desc.name.c_str(), frame, desc.numFrames);
        const std::string where = whereBuf;
        RefSample ref;
        if (!refSample(anim, anim.bones, ad, frame, ref)) {
          ++res.refFailures;
          std::printf("ROOTDIAG-REFFAIL %s reason=%s\n", where.c_str(), ref.reason.c_str());
          continue;
        }
        if (ref.bones.empty()) continue;
        res.mixedLayout += ref.mixedLayout;
        if (ref.unsortedChain) ++res.unsortedChains;
        if (desc.sectionFrames != 0 && desc.numFrames > desc.sectionFrames && frame == desc.numFrames - 1) {
          ++res.lastFrameSections;
        }
        const Pose src = refPose(ref);
        Pose prod;
        std::string prodReason;
        const bool prodOk = productPose(binding->anims, ad, frame, desc.fps, prod, prodReason)
          && prod.rot.size() == src.rot.size();
        PoseDiff diff;
        if (prodOk) {
          diff = diffPoses(src, prod);
          ++res.productSamples;
          if (diff.maxAngle < 1.0 && diff.maxPos < 0.05) ++res.productMatches;
        }
        const std::size_t r0 = roots.empty() ? 0 : roots.front();
        const RefBone& rootRef = ref.bones[r0];
        const double rootDev = qangle(rootRef.rot, anim.bones[r0].quat);
        if (!o.quiet) {
          std::printf("ROOTDIAG-C4 %s gate=%s animFlags=0x%04x seqFlags=0x%04x cyclePose=%d allZeros=%d section=%d dataFrame=%d chain=%zu noData=%d root=%s inChain=%d rec=0x%02x channel=%s base=%s src=%s prod=%s prodVsSrc=%.3fdeg srcVsBind=%.2fdeg(%s)",
            where.c_str(), gateName(gate), static_cast<unsigned>(desc.flags), static_cast<unsigned>(seq.flags),
            (seq.flags & kStudioCyclePose) != 0 ? 1 : 0, (desc.flags & kStudioAllZeros) != 0 ? 1 : 0,
            ref.section, ref.dataFrame, ref.chainLength, ref.noData ? 1 : 0,
            anim.bones[r0].name.c_str(), rootRef.inChain ? 1 : 0, static_cast<unsigned>(rootRef.flags),
            rootRef.eulerChannel ? fmtDeg(rootRef.channel).c_str() : "-", fmtDeg(anim.bones[r0].rot).c_str(),
            fmtQ(rootRef.rot).c_str(), prodOk ? fmtQ(prod.rot[r0]).c_str() : prodReason.c_str(),
            prodOk ? qangle(rootRef.rot, prod.rot[r0]) : -1.0, rootDev, deviationClass(rootDev, o.maxDevDeg));
          if (prodOk) {
            std::printf(" maxBoneDiff=%.3fdeg@%s bonesOver1deg=%zu maxPosDiff=%.4f",
              diff.maxAngle, anim.bones[diff.worst].name.c_str(), diff.over1, diff.maxPos);
          }
          std::printf("\n");
          if (prodOk && diff.maxAngle > 1.0) printKeyBones(where, keys, anim.bones, src, prod);
        }
        if (o.dumpBones) {
          for (std::size_t i = 0; i < ref.bones.size(); ++i) {
            std::printf("ROOTDIAG-BONE %s bone=%zu name=%s inChain=%d rec=0x%02x src=%s srcPos=%s prod=%s prodPos=%s\n",
              where.c_str(), i, anim.bones[i].name.c_str(), ref.bones[i].inChain ? 1 : 0,
              static_cast<unsigned>(ref.bones[i].flags), fmtQ(src.rot[i]).c_str(), fmtV(src.pos[i]).c_str(),
              prodOk ? fmtQ(prod.rot[i]).c_str() : "-", prodOk ? fmtV(prod.pos[i]).c_str() : "-");
          }
        }

        if ((desc.flags & kStudioDelta) != 0 || gate == Gate::Delta || gate == Gate::DeltaAir) {
          const Gate deltaGate = gate == Gate::DeltaAir ? Gate::DeltaAir : Gate::Delta;
          // ---- C6: delta layer ----
          double magnitude = 0.0;
          for (const auto& q : src.rot) magnitude = (std::max)(magnitude, qangle(q, kIdentity));
          if (prodOk) {
            for (std::size_t i = 0; i < ref.bones.size(); ++i) {
              const bool candidate = !ref.bones[i].inChain || anim.bones[i].parent < 0;
              if (!candidate || qangle(src.rot[i], kIdentity) >= 2.0) continue;
              ++res.h1Tested;
              if (qangle(src.rot[i], prod.rot[i]) > 10.0) {
                ++res.h1Hits;
                if (anim.bones[i].parent < 0) ++res.h1RootHits;
              }
            }
          }
          std::vector<Vec3> mpos;
          std::vector<Quat> mrot;
          compose(anim.bones, applyDelta(deltaBase, src, weights, post), mpos, mrot);
          const Upright uSrc = measure(mpos, keys, up, deltaGate, o.minUp);
          compose(anim.bones, applyDelta(deltaBase, src, weights, !post), mpos, mrot);
          const Upright uAlt = measure(mpos, keys, up, deltaGate, o.minUp);
          Upright uProd;
          if (prodOk) {
            compose(anim.bones, applyDelta(deltaBase, prod, weights, post), mpos, mrot);
            uProd = measure(mpos, keys, up, deltaGate, o.minUp);
          }
          res.deltaSource.add(uSrc);
          res.deltaAlt.add(uAlt);
          res.deltaProduct.add(uProd);
          if (!o.quiet) {
            std::printf("ROOTDIAG-C6 %s base=%s order=%s magnitude=%.2fdeg srcRootVsIdentity=%.2fdeg prodRootVsIdentity=%.2fdeg source=%s altOrder=%s product=%s\n",
              where.c_str(), deltaBaseLabel.c_str(), post ? "post(MA)" : "pre(SM)", magnitude,
              qangle(src.rot[r0], kIdentity), prodOk ? qangle(prod.rot[r0], kIdentity) : -1.0,
              fmtUpright(uSrc).c_str(), fmtUpright(uAlt).c_str(), fmtUpright(uProd).c_str());
          }
          continue;
        }

        // ---- C8: the header hull, independent of every rotation rule ----
        if ((gate == Gate::Upright || gate == Gate::Run) && hull.ok) {
          const HullReading hullSrc = hullReading(anim.bones, keys, src, hull);
          const HullReading hullProd = prodOk ? hullReading(anim.bones, keys, prod, hull) : HullReading{};
          if (hullSrc.measured && hullProd.measured) {
            ++res.hullMeasured;
            if (hullSrc.inside) ++res.hullSrcInside;
            if (hullProd.inside) ++res.hullProdInside;
            if (hullSrc.spanOk) ++res.hullSrcEyeOk;
            if (hullProd.spanOk) ++res.hullProdEyeOk;
            if (!o.quiet || res.hullMeasured == 1) {
              std::printf("ROOTDIAG-C8 %s srcHead=%s srcFoot=%s srcInHull=%d srcSpan=%.2f prodHead=%s prodFoot=%s prodInHull=%d prodSpan=%.2f\n",
                where.c_str(), fmtV(hullSrc.head).c_str(), fmtV(hullSrc.foot).c_str(), hullSrc.inside ? 1 : 0,
                hullSrc.span, fmtV(hullProd.head).c_str(), fmtV(hullProd.foot).c_str(),
                hullProd.inside ? 1 : 0, hullProd.span);
            }
          }
        }

        // ---- C5: root variants ----
        RefSample refR;
        const bool haveR = refSample(anim, renderBase, ad, frame, refR);
        std::array<Upright, kVariantCount> u{};
        for (int v = 0; v < kVariantCount; ++v) {
          Pose pose;
          bool have = true;
          switch (v) {
            case kVSource: pose = src; break;
            case kVRenderBase: have = haveR; if (have) pose = refPose(refR); break;
            case kVProduct: have = prodOk; if (have) pose = prod; break;
            case kVProductSrcRoot:
              have = prodOk;
              if (have) { pose = prod; for (const auto r : roots) { pose.rot[r] = src.rot[r]; pose.pos[r] = src.pos[r]; } }
              break;
            default:
              pose = src;
              for (const auto r : roots) {
                const RawBone& bb = anim.bones[r];
                const RefBone& rb = ref.bones[r];
                const bool eulerOnly = rb.eulerChannel && (rb.flags & kAnimDelta) == 0;
                if (v == kVBindRoot) { pose.rot[r] = bb.quat; pose.pos[r] = bb.pos; }
                else if (v == kVBindRotAnimHeight) {
                  pose.rot[r] = bb.quat;
                  pose.pos[r] = up.ok ? add(bb.pos, mul(up.up, dot3(sub(src.pos[r], bb.pos), up.up))) : bb.pos;
                } else if (v == kVConjRoot) { pose.rot[r] = qconj(src.rot[r]); }
                else if (v == kVChannelOnlyRoot) { have = eulerOnly; if (have) pose.rot[r] = eulerQ(rb.channel); }
                else if (v == kVDoubleBaseRoot) { have = eulerOnly; if (have) pose.rot[r] = eulerQ(add(rb.channel, mul(bb.rot, 2.0))); }
                else if (v == kVBindPremulRoot) { pose.rot[r] = qnorm(qmul(bb.quat, src.rot[r])); }
              }
              break;
          }
          if (!have) continue;
          std::vector<Vec3> mpos;
          std::vector<Quat> mrot;
          compose(anim.bones, pose, mpos, mrot);
          u[static_cast<std::size_t>(v)] = measure(mpos, keys, up, gate, o.minUp);
          if (gate == Gate::Upright || gate == Gate::Run || gate == Gate::Crouch) res.variant[static_cast<std::size_t>(v)].add(u[static_cast<std::size_t>(v)]);
        }
        if (!o.quiet) {
          std::printf("ROOTDIAG-C5 %s gate=%s", where.c_str(), gateName(gate));
          for (int v = 0; v < kVariantCount; ++v) {
            std::printf(" %s=%s", kVariantNames[v], fmtUpright(u[static_cast<std::size_t>(v)]).c_str());
          }
          // Direction-free reading: where the head actually points, in model space,
          // and how long the body is against the bind. A pose that is upright along
          // an axis the gate is not using reads X above and (0,0,1)/~1.0 here.
          const auto bodyDir = [&](const Pose& bodyPose, Vec3& outDir, double& outRatio) {
            std::vector<Vec3> bodyPos;
            std::vector<Quat> bodyRot;
            compose(anim.bones, bodyPose, bodyPos, bodyRot);
            if (!keys.measurable() || !up.ok) return false;
            const Vec3 span = sub(bodyPos[static_cast<std::size_t>(keys.head)], footPoint(bodyPos, keys, up.up, false));
            outDir = norm3(span);
            outRatio = len3(span) / up.bindHeight;
            return true;
          };
          Vec3 bodyDirection{};
          double bodyRatio = 0.0;
          if (bodyDir(src, bodyDirection, bodyRatio)) std::printf(" srcDir=%s srcLen=%.2f", fmtV(bodyDirection).c_str(), bodyRatio);
          if (prodOk && bodyDir(prod, bodyDirection, bodyRatio)) std::printf(" prodDir=%s prodLen=%.2f", fmtV(bodyDirection).c_str(), bodyRatio);
          std::printf("\n");
        }
      }
    }
  }
  return true;
}

void printSummary(const std::string& model, const ModelResult& r) {
  std::printf("ROOTDIAG-SUMMARY model=%s C1=%s C2=%s C3=%s", model.c_str(),
    r.c1ok ? "PASS" : "FAIL", r.c2ok ? "PASS" : "FAIL", r.c3ok ? "PASS" : "WARN");
  for (int v = 0; v < kVariantCount; ++v) {
    const Tally& t = r.variant[static_cast<std::size_t>(v)];
    std::printf(" %s=%zu/%zu", kVariantNames[v], t.pass, t.measured);
  }
  std::printf(" hull=%s hullSrc=%zu/%zu hullProd=%zu/%zu spanSrc=%zu/%zu spanProd=%zu/%zu",
    r.hullOk ? "ok" : "unreadable", r.hullSrcInside, r.hullMeasured, r.hullProdInside, r.hullMeasured,
    r.hullSrcEyeOk, r.hullMeasured, r.hullProdEyeOk, r.hullMeasured);
  std::printf(" deltaSource=%zu/%zu deltaAltOrder=%zu/%zu deltaProduct=%zu/%zu productMatch=%zu/%zu H1=%s(%zu/%zu root=%zu) mixedLayout=%zu unsortedChains=%zu lastFrameSections=%zu refFailures=%zu\n",
    r.deltaSource.pass, r.deltaSource.measured, r.deltaAlt.pass, r.deltaAlt.measured,
    r.deltaProduct.pass, r.deltaProduct.measured, r.productMatches, r.productSamples,
    r.h1Tested == 0 ? "UNTESTED" : (r.h1Hits > 0 ? "CONFIRMED" : "REFUTED"),
    r.h1Hits, r.h1Tested, r.h1RootHits, r.mixedLayout, r.unsortedChains, r.lastFrameSections, r.refFailures);

  const auto hint = [&model](const char* text) { std::printf("ROOTDIAG-HINT model=%s %s\n", model.c_str(), text); };
  const Tally& s = r.variant[kVSource];
  const Tally& p = r.variant[kVProduct];
  if (!r.c1ok) hint("C1: the root or a key bone's rot does not convert to its quat -- Euler order is wrong; fix before anything else");
  if (!r.c2ok) hint("C2: render bind does not round-trip through poseToBone -- the bind reading is wrong; no pose built on it can be trusted");
  if (r.seqFlagMismatch > 0) hint("C3: the product's merged sequence table carries different DELTA/POST flags than the animation source's own seqdesc -- delta layers are being applied with the wrong rule");
  if (!r.c3ok && r.seqFlagMismatch == 0) hint("C3: animation-source bind and render bind disagree (parents or skin identity) -- compare the source and renderBase variants");
  if (r.hullMeasured > 0) {
    // The verdict reads the SPAN (extent along the hull's tall axis). Inside/outside
    // is printed but does not decide: a body lying inside a wide hull is still lying.
    const double srcShare = static_cast<double>(r.hullSrcEyeOk) / static_cast<double>(r.hullMeasured);
    const double prodShare = static_cast<double>(r.hullProdEyeOk) / static_cast<double>(r.hullMeasured);
    if (srcShare >= 0.8 && prodShare <= 0.2) hint("C8: along the MDL header hull's own tall axis the REFERENCE body spans the hull and the product body lies across it; the product's root rule is the error (independent of any up-axis convention)");
    else if (prodShare >= 0.8 && srcShare <= 0.2) hint("C8: along the MDL header hull's own tall axis the PRODUCT body spans the hull and the reference lies across it; the reference root rule is the error");
    else hint("C8: the hull does not separate the two poses; read the ROOTDIAG-C8 lines");
  }
  if (r.h1Hits > 0) hint("H1 CONFIRMED: delta animdescs carry the bind rotation on root/non-chain bones in the product; Source uses identity there, so the bind is applied twice");
  if (s.all() && p.measured > 0 && !p.all()) {
    if (r.variant[kVProductSrcRoot].all()) hint("only the root differs: the product's root rule is the cause");
    else hint("product fails even with the reference root: non-root bones differ -- read the ROOTDIAG-BONES lines");
  }
  if (s.measured > 0 && !s.all()) {
    bool found = false;
    for (int v = 0; v < kVariantCount; ++v) {
      if (v == kVSource || v == kVProduct || v == kVProductSrcRoot) continue;
      if (r.variant[static_cast<std::size_t>(v)].all()) {
        std::printf("ROOTDIAG-HINT model=%s reference is not upright but variant %s is upright on every gated sample -- re-check the reference root rule (C4 srcVsBind classes)\n",
          model.c_str(), kVariantNames[v]);
        found = true;
        break;
      }
    }
    if (!found) hint("no root interpretation is upright: the cause is not the root -- check C1/C3 and the bone composition");
  }
  if (r.deltaSource.all() && r.deltaProduct.measured > 0 && !r.deltaProduct.all()) hint("delta layers: reference stays upright, product delta does not");
  if (r.deltaSource.measured > 0 && !r.deltaSource.all() && r.deltaAlt.all()) hint("delta layers are upright only in the opposite SM/MA order -- the STUDIO_POST reading is inverted");
}

// ---------------------------------------------------------------------------
// self-test fixtures
// ---------------------------------------------------------------------------
struct Run {
  std::uint8_t valid = 0;
  std::uint8_t total = 0;
  std::vector<std::int16_t> values;
};
using Channels = std::array<std::vector<Run>, 3>;

void pushI16(Bytes& b, std::int16_t v) {
  std::uint8_t raw[2];
  std::memcpy(raw, &v, 2);
  b.push_back(raw[0]);
  b.push_back(raw[1]);
}
void pushU16(Bytes& b, std::uint16_t v) { pushI16(b, static_cast<std::int16_t>(v)); }

Bytes encodeRuns(const std::vector<Run>& runs) {
  Bytes out;
  for (const auto& r : runs) {
    out.push_back(r.valid);
    out.push_back(r.total);
    for (const auto v : r.values) pushI16(out, v);
  }
  return out;
}

// mstudioanim_valueptr_t blocks (rot at data, pos at data + 6 when both) and
// their runs; each offset is relative to its own valueptr.
Bytes animPayload(const Channels* rot, const Channels* pos) {
  Bytes out;
  std::size_t rotPtr = 0, posPtr = 0;
  if (rot != nullptr) { rotPtr = out.size(); out.resize(out.size() + 6, 0); }
  if (pos != nullptr) { posPtr = out.size(); out.resize(out.size() + 6, 0); }
  const auto emit = [&out](std::size_t ptr, const Channels& ch) {
    for (std::size_t j = 0; j < 3; ++j) {
      if (ch[j].empty()) continue;
      const std::size_t at = out.size();
      const Bytes runs = encodeRuns(ch[j]);
      out.insert(out.end(), runs.begin(), runs.end());
      const auto rel = static_cast<std::int16_t>(at - ptr);
      std::memcpy(out.data() + ptr + j * 2, &rel, 2);
    }
  };
  if (rot != nullptr) emit(rotPtr, *rot);
  if (pos != nullptr) emit(posPtr, *pos);
  return out;
}

struct Record {
  std::uint8_t bone = 0;
  std::uint8_t flags = 0;
  Bytes payload;
};
Bytes chainBytes(const std::vector<Record>& recs) {
  Bytes out;
  for (std::size_t i = 0; i < recs.size(); ++i) {
    out.push_back(recs[i].bone);
    out.push_back(recs[i].flags);
    pushI16(out, i + 1 < recs.size() ? static_cast<std::int16_t>(4 + recs[i].payload.size()) : static_cast<std::int16_t>(0));
    out.insert(out.end(), recs[i].payload.begin(), recs[i].payload.end());
  }
  return out;
}

struct FixtureBone {
  const char* name;
  std::int32_t parent;
  Vec3 pos;
  Vec3 rot;
};

Bytes buildMdl(const std::vector<FixtureBone>& bones, std::int32_t animFlags, std::int32_t seqFlags,
    std::int32_t numFrames, const Bytes& animData) {
  Bytes b(kHdrBytes, 0);
  const auto put = [&b](std::size_t at, auto value) { std::memcpy(b.data() + at, &value, sizeof(value)); };
  const auto addString = [&b](const std::string& s) {
    const std::size_t at = b.size();
    b.insert(b.end(), s.begin(), s.end());
    b.push_back(0);
    return at;
  };
  const std::size_t boneAt = b.size();
  b.resize(boneAt + bones.size() * kBoneStride, 0);
  const std::size_t animAt = b.size();
  b.resize(animAt + kAnimStride, 0);
  const std::size_t seqAt = b.size();
  b.resize(seqAt + kSeqStride, 0);
  const std::size_t cellAt = b.size();
  b.resize(cellAt + 4, 0);
  const std::size_t dataAt = b.size();
  b.insert(b.end(), animData.begin(), animData.end());
  while (b.size() % 4 != 0) b.push_back(0);

  put(0, kIdStudio);
  put(4, static_cast<std::int32_t>(48));
  const char modelName[] = "selftest.mdl";
  std::memcpy(b.data() + 12, modelName, sizeof(modelName));
  put(kHdrBoneCount, static_cast<std::int32_t>(bones.size()));
  put(kHdrBoneIndex, static_cast<std::int32_t>(boneAt));
  put(kHdrAnimCount, static_cast<std::int32_t>(1));
  put(kHdrAnimIndex, static_cast<std::int32_t>(animAt));
  put(kHdrSeqCount, static_cast<std::int32_t>(1));
  put(kHdrSeqIndex, static_cast<std::int32_t>(seqAt));
  for (std::size_t i = 0; i < bones.size(); ++i) {
    const std::size_t off = boneAt + i * kBoneStride;
    const std::size_t nameAt = addString(bones[i].name);
    const Quat q = eulerQ(bones[i].rot);
    put(off, static_cast<std::int32_t>(nameAt - off));
    put(off + 4, bones[i].parent);
    for (std::size_t j = 0; j < 3; ++j) {
      put(off + kBonePos + j * 4, static_cast<float>(bones[i].pos[j]));
      put(off + kBoneRot + j * 4, static_cast<float>(bones[i].rot[j]));
      put(off + kBonePosScale + j * 4, 0.01f);
      put(off + kBoneRotScale + j * 4, 0.001f);
    }
    for (std::size_t j = 0; j < 4; ++j) put(off + kBoneQuat + j * 4, static_cast<float>(q[j]));
    for (std::size_t j = 0; j < 3; ++j) put(off + kBonePoseToBone + (j * 4 + j) * 4, 1.0f);
    put(off + kBoneAlignment + 12, 1.0f);
  }
  const std::size_t animName = addString("a_selftest");
  put(animAt, -static_cast<std::int32_t>(animAt));
  put(animAt + 4, static_cast<std::int32_t>(animName - animAt));
  put(animAt + 8, 30.0f);
  put(animAt + 12, animFlags);
  put(animAt + 16, numFrames);
  put(animAt + 56, static_cast<std::int32_t>(dataAt - animAt));
  const std::size_t seqName = addString("selftest");
  put(seqAt + 4, static_cast<std::int32_t>(seqName - seqAt));
  put(seqAt + 12, seqFlags);
  put(seqAt + 16, static_cast<std::int32_t>(-1));
  put(seqAt + 56, static_cast<std::int32_t>(1));
  put(seqAt + 60, static_cast<std::int32_t>(cellAt - seqAt));
  put(seqAt + 68, static_cast<std::int32_t>(1));
  put(seqAt + 72, static_cast<std::int32_t>(1));
  put(seqAt + 76, static_cast<std::int32_t>(-1));
  put(seqAt + 80, static_cast<std::int32_t>(-1));
  put(cellAt, static_cast<std::int16_t>(0));
  while (b.size() % 4 != 0) b.push_back(0);
  put(kHdrLength, static_cast<std::int32_t>(b.size()));
  return b;
}

struct Fixture {
  std::string name;
  std::string meaning;  // what a product mismatch on this fixture means
  Bytes bytes;
  int frames = 1;
  std::vector<Pose> expected;
};

bool approxEq(double a, double b, double eps) { return std::fabs(a - b) <= eps; }
bool approxV(const Vec3& a, const Vec3& b, double eps) { return len3(sub(a, b)) <= eps; }

std::vector<Fixture> buildFixtures() {
  const std::vector<FixtureBone> bones = {
    {"root", -1, {{0.0, 0.0, 40.0}}, {{kPi / 2.0, 0.0, 0.0}}},
    {"child", 0, {{0.0, 0.0, 10.0}}, {{0.3, 0.0, 0.0}}},
  };
  const Quat rootBind = eulerQ(bones[0].rot);
  const Quat childBind = eulerQ(bones[1].rot);
  std::vector<Fixture> out;

  {  // F1: non-delta ANIMROT with two RLE runs, root RAWPOS.
    Fixture f;
    f.name = "F1-nondelta-rle";
    f.meaning = "root RAWPOS ignored = the T62 root lock; child mismatch = RLE or Euler-base error";
    f.frames = 8;
    Channels rot;
    rot[0] = {Run{2, 3, {100, 200}}, Run{1, 5, {300}}};
    Bytes rootPayload;
    pushU16(rootPayload, 0x0000);
    pushU16(rootPayload, 0x0000);
    pushU16(rootPayload, 0x51A0);  // float16 45.0
    f.bytes = buildMdl(bones, 0, 0, 8, chainBytes({{0, kAnimRawPos, rootPayload}, {1, kAnimAnimRot, animPayload(&rot, nullptr)}}));
    const int v[8] = {100, 200, 200, 300, 300, 300, 300, 300};
    for (int i = 0; i < 8; ++i) {
      Pose p;
      p.pos = {{{0.0, 0.0, 45.0}}, bones[1].pos};
      p.rot = {rootBind, eulerQ({{v[i] * static_cast<double>(0.001f) + bones[1].rot[0], 0.0, 0.0}})};
      f.expected.push_back(p);
    }
    out.push_back(std::move(f));
  }
  {  // F2: delta animdesc, root outside the chain.
    Fixture f;
    f.name = "F2-delta-defaults";
    f.meaning = "root/pos not identity/zero = H1 (bind applied inside a delta layer)";
    Channels rot;
    rot[0] = {Run{1, 1, {150}}};
    f.bytes = buildMdl(bones, kStudioDelta, kStudioDelta, 1,
      chainBytes({{1, static_cast<std::uint8_t>(kAnimAnimRot | kAnimDelta), animPayload(&rot, nullptr)}}));
    Pose p;
    p.pos = {kZero, kZero};
    p.rot = {kIdentity, eulerQ({{150 * static_cast<double>(0.001f), 0.0, 0.0}})};
    f.expected.push_back(p);
    out.push_back(std::move(f));
  }
  {  // F3: delta animdesc, root in the chain with zero channels.
    Fixture f;
    f.name = "F3-delta-root-in-chain";
    f.meaning = "root not identity = the root is special-cased ahead of the delta rule";
    const Channels zeroChannels{};
    Channels pos;
    pos[0] = {Run{1, 1, {500}}};
    f.bytes = buildMdl(bones, kStudioDelta, kStudioDelta, 1,
      chainBytes({{0, static_cast<std::uint8_t>(kAnimAnimRot | kAnimDelta), animPayload(&zeroChannels, nullptr)},
                  {1, static_cast<std::uint8_t>(kAnimAnimRot | kAnimAnimPos | kAnimDelta), animPayload(&zeroChannels, &pos)}}));
    Pose p;
    p.pos = {kZero, {{500 * static_cast<double>(0.01f), 0.0, 0.0}}};
    p.rot = {kIdentity, kIdentity};
    f.expected.push_back(p);
    out.push_back(std::move(f));
  }
  {  // F4: RAWROT (quat48) + RAWPOS.
    Fixture f;
    f.name = "F4-rawrot-rawpos";
    f.meaning = "quat48 / vector48 layout error";
    Bytes payload;
    pushU16(payload, 32768);
    pushU16(payload, 32768);
    pushU16(payload, 27970);  // z = sin 45, w positive
    pushU16(payload, 0x3C00);
    pushU16(payload, 0x4000);
    pushU16(payload, 0x4200);
    f.bytes = buildMdl(bones, 0, 0, 1, chainBytes({{1, static_cast<std::uint8_t>(kAnimRawRot | kAnimRawPos), payload}}));
    Pose p;
    p.pos = {bones[0].pos, {{1.0, 2.0, 3.0}}};
    p.rot = {rootBind, {{0.0, 0.0, std::sqrt(0.5), std::sqrt(0.5)}}};
    f.expected.push_back(p);
    out.push_back(std::move(f));
  }
  {  // F5: RAWROT2 (quat64), position from bind.
    Fixture f;
    f.name = "F5-rawrot2";
    f.meaning = "quat64 layout error";
    const std::uint64_t x = 1048576u + static_cast<std::uint64_t>(std::llround(std::sqrt(0.5) * 1048576.5));
    const std::uint64_t packed = x | (1048576ull << 21) | (1048576ull << 42);
    Bytes payload(8, 0);
    std::memcpy(payload.data(), &packed, 8);
    f.bytes = buildMdl(bones, 0, 0, 1, chainBytes({{1, kAnimRawRot2, payload}}));
    Pose p;
    p.pos = {bones[0].pos, bones[1].pos};
    p.rot = {rootBind, {{std::sqrt(0.5), 0.0, 0.0, std::sqrt(0.5)}}};
    f.expected.push_back(p);
    (void)childBind;
    out.push_back(std::move(f));
  }
  return out;
}

bool mathSelfTest() {
  const Quat qx = eulerQ({{kPi / 2.0, 0.0, 0.0}});
  const Quat qy = eulerQ({{0.0, kPi / 2.0, 0.0}});
  const Quat qz = eulerQ({{0.0, 0.0, kPi / 2.0}});
  const bool axes = approxV(qrot(qx, {{0.0, 1.0, 0.0}}), {{0.0, 0.0, 1.0}}, 1e-9)
    && approxV(qrot(qy, {{0.0, 0.0, 1.0}}), {{1.0, 0.0, 0.0}}, 1e-9)
    && approxV(qrot(qz, {{1.0, 0.0, 0.0}}), {{0.0, 1.0, 0.0}}, 1e-9);
  bool order = true;
  const Vec3 samples[3] = {{{0.3, -0.7, 1.1}}, {{1.2, 0.4, -2.0}}, {{-0.5, 1.3, 0.2}}};
  for (const auto& e : samples) {
    const Quat composed = qmul(eulerQ({{0.0, 0.0, e[2]}}), qmul(eulerQ({{0.0, e[1], 0.0}}), eulerQ({{e[0], 0.0, 0.0}})));
    if (qangle(eulerQ(e), composed) > 1e-6) order = false;
  }
  const bool scale = approxEq(qangle(qscale(qx, 0.5), kIdentity), 45.0, 1e-4)
    && qangle(qscale(qx, 1.0), qx) < 1e-4 && qangle(qscale(qx, 0.0), kIdentity) < 1e-6;
  const bool smma = qangle(qSM(1.0, qx, qz), qmul(qx, qz)) < 1e-4
    && qangle(qMA(qz, 1.0, qx), qmul(qz, qx)) < 1e-4
    && qangle(qmul(qx, qz), qmul(qz, qx)) > 1.0;
  bool rle = true;
  {
    const Bytes runs = encodeRuns({Run{2, 3, {100, 200}}, Run{1, 5, {300}}});
    const int want[8] = {100, 200, 200, 300, 300, 300, 300, 300};
    for (int f = 0; f < 8; ++f) {
      double v = 0.0;
      std::string reason;
      if (!extractAnimValue(runs, 0, f, 1.0, v, reason) || !approxEq(v, want[f], 1e-9)) rle = false;
    }
  }
  const bool f16 = approxEq(sourceFloat16(0x51A0), 45.0, 1e-9) && approxEq(sourceFloat16(0x3C00), 1.0, 1e-9)
    && approxEq(sourceFloat16(0xC000), -2.0, 1e-9) && approxEq(sourceFloat16(0x0000), 0.0, 1e-12);
  const bool q48 = qangle(quat48(32768, 32768, 27970), {{0.0, 0.0, std::sqrt(0.5), std::sqrt(0.5)}}) < 0.05;
  const std::uint64_t x64 = 1048576u + static_cast<std::uint64_t>(std::llround(std::sqrt(0.5) * 1048576.5));
  const bool q64 = qangle(quat64(x64 | (1048576ull << 21) | (1048576ull << 42)), {{std::sqrt(0.5), 0.0, 0.0, std::sqrt(0.5)}}) < 0.01;
  const Mat34 m = qmat(eulerQ({{0.4, -1.1, 2.3}}), {{3.0, -7.0, 11.0}});
  const IdentityError e = identityError(mmul(m, rigidInverse(m)));
  const bool mat = e.rot < 1e-9 && e.trans < 1e-9;
  std::printf("ROOTDIAG-SELFTEST euler-axes=%d euler-order=%d scale=%d sm-ma=%d rle=%d f16=%d quat48=%d quat64=%d mat=%d\n",
    axes ? 1 : 0, order ? 1 : 0, scale ? 1 : 0, smma ? 1 : 0, rle ? 1 : 0, f16 ? 1 : 0, q48 ? 1 : 0, q64 ? 1 : 0, mat ? 1 : 0);
  return axes && order && scale && smma && rle && f16 && q48 && q64 && mat;
}

int usage() {
  std::fprintf(stderr,
    "usage: model_pose_probe --root-diagnose --tf-root <tf> --model <a.mdl>[,<b.mdl>...]\n"
    "         [--sequences auto|all|<index|label>,...] [--frames 0,mid,last|<n>,...]\n"
    "         [--upright stand_,run_,walk_] [--base <label>] [--up z|bind|mesh] [--min-up 0.8] [--max-dev 15]\n"
    "         [--quiet] [--dump-bones] [--require-product]\n"
    "       model_pose_probe --root-diagnose-selftest [--require-product]\n");
  return 2;
}

} // namespace

int rootDiagnoseSelfTest(bool requireProduct) {
  const bool math = mathSelfTest();
  bool refOk = true;
  bool productOk = true;
  for (const Fixture& f : buildFixtures()) {
    RawModel raw;
    bool fixtureRef = parseRaw(f.bytes, raw);
    std::string refNote = fixtureRef ? "" : raw.reason;
    AnimationModel model;
    const bool decoded = decodeAnimationModel(f.bytes.data(), f.bytes.size(), model) == AnimationStatus::Ok;
    bool fixtureProduct = decoded;
    std::string prodNote = decoded ? "" : "product decoder refused the fixture: " + model.reason;
    double worstAngle = 0.0, worstPos = 0.0;
    std::string worstAt = "-";
    for (int frame = 0; fixtureRef && frame < f.frames; ++frame) {
      RefSample s;
      if (!refSample(raw, raw.bones, 0, frame, s)) { fixtureRef = false; refNote = s.reason; break; }
      const Pose got = refPose(s);
      const Pose& want = f.expected[static_cast<std::size_t>(frame)];
      for (std::size_t i = 0; i < want.rot.size(); ++i) {
        if (qangle(got.rot[i], want.rot[i]) > 0.05 || !approxV(got.pos[i], want.pos[i], 1e-3)) {
          fixtureRef = false;
          refNote = "reference disagrees with the expected pose at bone " + raw.bones[i].name
            + " frame " + std::to_string(frame);
        }
      }
      if (!decoded) continue;
      Pose prod;
      std::string reason;
      if (!productPose(model, 0, frame, 30.0f, prod, reason) || prod.rot.size() != got.rot.size()) {
        fixtureProduct = false;
        prodNote = "product sample failed: " + reason;
        continue;
      }
      for (std::size_t i = 0; i < got.rot.size(); ++i) {
        const double a = qangle(got.rot[i], prod.rot[i]);
        const double p = len3(sub(got.pos[i], prod.pos[i]));
        if (a > 0.05 || p > 1e-3) fixtureProduct = false;
        if (a + p > worstAngle + worstPos) {
          worstAngle = a;
          worstPos = p;
          worstAt = raw.bones[i].name + "@" + std::to_string(frame);
        }
      }
    }
    if (!fixtureRef) refOk = false;
    if (!fixtureProduct) productOk = false;
    std::printf("ROOTDIAG-FIXTURE name=%s ref=%s product=%s worst=%s angle=%.3fdeg pos=%.4f%s%s%s%s\n",
      f.name.c_str(), fixtureRef ? "PASS" : "FAIL", fixtureProduct ? "MATCH" : "MISMATCH", worstAt.c_str(),
      worstAngle, worstPos, refNote.empty() ? "" : " refNote=", refNote.c_str(),
      fixtureProduct ? "" : " meaning=", fixtureProduct ? "" : (prodNote.empty() ? f.meaning.c_str() : prodNote.c_str()));
  }
  const bool pass = math && refOk;
  std::printf("ROOTDIAG-SELFTEST=%s\nPRODUCT-FIXTURE=%s\n", pass ? "PASS" : "FAIL", productOk ? "PASS" : "FAIL");
  if (!pass) return 1;
  // 0 only when the product matches too. A product mismatch is 1 with
  // --require-product and 3 without it, so a bare run can never read as green.
  if (!productOk) return requireProduct ? 1 : 3;
  return 0;
}

int rootDiagnose(int argc, char** argv) {
  Options o;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    const auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : nullptr; };
    if (a == "--root-diagnose") continue;
    if (a == "--quiet") { o.quiet = true; continue; }
    if (a == "--dump-bones") { o.dumpBones = true; continue; }
    if (a == "--require-product") { o.requireProduct = true; continue; }
    const char* v = next();
    if (v == nullptr) return usage();
    if (a == "--tf-root") o.tfRoot = v;
    else if (a == "--model") o.models = splitList(v);
    else if (a == "--sequences") o.sequences = splitList(v);
    else if (a == "--frames") o.frames = splitList(v);
    else if (a == "--upright") o.uprightPrefixes = splitList(v);
    else if (a == "--base") o.baseLabel = v;
    else if (a == "--up") o.upMode = v;
    else if (a == "--min-up") o.minUp = std::atof(v);
    else if (a == "--max-dev") o.maxDevDeg = std::atof(v);
    else return usage();
  }
  if (o.tfRoot.empty() || o.models.empty() || o.sequences.empty() || o.frames.empty()) return usage();
  if (o.upMode != "bind" && o.upMode != "z" && o.upMode != "mesh") return usage();
  const AssetRoot root = AssetRoot::fromPath(std::filesystem::u8path(o.tfRoot));
  if (!root.valid()) {
    std::printf("ROOTDIAG-ERROR stage=tf-root reason=invalid path=%s\n", o.tfRoot.c_str());
    return 2;
  }
  AnimationBindingCache cache;
  cache.reset(&root);
  bool diagPass = true;
  bool productPass = true;
  for (const auto& model : o.models) {
    ModelResult r;
    if (!diagnoseModel(root, cache, model, o, r)) {
      diagPass = false;
      productPass = false;
      continue;
    }
    printSummary(model, r);
    const bool modelDiag = r.c1ok && r.c2ok && r.variant[kVSource].all()
      && (r.deltaSource.measured == 0 || r.deltaSource.all()) && r.refFailures == 0;
    const bool modelProduct = r.productSamples > 0 && r.productMatches == r.productSamples
      && r.variant[kVProduct].all() && (r.deltaProduct.measured == 0 || r.deltaProduct.all()) && r.h1Hits == 0;
    if (!modelDiag) diagPass = false;
    if (!modelProduct) productPass = false;
  }
  std::printf("ROOT-DIAG=%s models=%zu\nPRODUCT-MATCH=%s\n", diagPass ? "PASS" : "FAIL", o.models.size(),
    productPass ? "PASS" : "FAIL");
  if (!diagPass) return 1;
  if (!productPass) return o.requireProduct ? 1 : 3;
  return 0;
}

} // namespace tf2::native::probe
