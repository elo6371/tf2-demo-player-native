#include "animation_binding.h"
#include "animation_clock.h"
#include "asset_root.h"
#include "demo_header.h"
#include "entity_model.h"
#include "entity_overlay.h"
#include "model_loader.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
bool finite3(const std::array<float, 3>& value) {
  return std::isfinite(value[0]) && std::isfinite(value[1]) && std::isfinite(value[2]);
}
float length3(const std::array<float, 3>& value) {
  return std::sqrt(value[0] * value[0] + value[1] * value[1] + value[2] * value[2]);
}

template <typename T>
void writeAt(std::vector<std::uint8_t>& bytes, std::size_t offset, T value) {
  std::memcpy(bytes.data() + offset, &value, sizeof(T));
}

bool hasDiagnostic(const tf2::native::ModelInspection& inspection, const std::string& needle) {
  for (const auto& diagnostic : inspection.diagnostics) {
    if (diagnostic.find(needle) != std::string::npos) return true;
  }
  for (const auto& diagnostic : inspection.metadata.diagnostics) {
    if (diagnostic.find(needle) != std::string::npos) return true;
  }
  return false;
}

int selfTest() {
  const auto directory = std::filesystem::temp_directory_path();
  const auto attachmentPath = directory / "tf2-model-pose-probe-attachment-invalid.mdl";
  const auto bodyPartPath = directory / "tf2-model-pose-probe-bodypart-invalid.mdl";
  const auto bonePath = directory / "tf2-model-pose-probe-bone-invalid.mdl";
  const auto sequencePath = directory / "tf2-model-pose-probe-sequence-invalid.mdl";
  std::vector<std::uint8_t> bytes(316, 0);
  writeAt<std::uint32_t>(bytes, 0, 0x54534449u);
  writeAt<std::uint32_t>(bytes, 4, 48u);
  writeAt<std::uint32_t>(bytes, 8, 1u);
  writeAt<std::uint32_t>(bytes, 76, 316u);
  // studiohdr_t words: 232/236 bodyparts, 240/244 attachments. These two
  // fixtures used to patch 240/244 for the *bodypart* table and 248/252 for the
  // *attachment* table, which is the pair the parser itself read -- so the
  // fixture agreed with the bug and stayed green while the attachment table
  // pointer was actually pointing into the node table. Patching the words the
  // parser is supposed to read is what makes this a test of the parser rather
  // than of the two mistakes cancelling out.
  writeAt<std::uint32_t>(bytes, 240, 1u);
  writeAt<std::uint32_t>(bytes, 244, 400u);
  { std::ofstream out(attachmentPath, std::ios::binary); out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size()); }
  const auto attachmentInspection = tf2::native::ModelLoader::inspect(attachmentPath);
  const bool attachmentRejected = hasDiagnostic(attachmentInspection, "attachment table is outside");
  const bool attachmentPreflight = attachmentInspection.mdl.signatureValid
    && attachmentInspection.vvd.signatureValid;
  writeAt<std::uint32_t>(bytes, 240, 0u);
  writeAt<std::uint32_t>(bytes, 244, 0u);
  writeAt<std::uint32_t>(bytes, 232, 1u);
  writeAt<std::uint32_t>(bytes, 236, 400u);
  { std::ofstream out(bodyPartPath, std::ios::binary); out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size()); }
  const auto bodyPartInspection = tf2::native::ModelLoader::inspect(bodyPartPath);
  const bool bodyPartRejected = hasDiagnostic(bodyPartInspection, "bodypart table is outside");
  const bool bodyPartPreflight = bodyPartInspection.mdl.signatureValid
    && bodyPartInspection.vvd.signatureValid;
  writeAt<std::uint32_t>(bytes, 232, 0u);
  writeAt<std::uint32_t>(bytes, 236, 0u);
  writeAt<std::uint32_t>(bytes, 156, 1u);
  writeAt<std::uint32_t>(bytes, 160, 400u);
  { std::ofstream out(bonePath, std::ios::binary); out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size()); }
  const auto boneInspection = tf2::native::ModelLoader::inspect(bonePath);
  const bool boneRejected = hasDiagnostic(boneInspection, "bone table is outside");
  tf2::native::ModelMetadata mutatedMetadata;
  mutatedMetadata.boneCount = 1;
  mutatedMetadata.vertices.push_back(tf2::native::ModelVertex{});
  mutatedMetadata.vertices[0].boneCount = 1;
  mutatedMetadata.vertices[0].weights = {1.0f, 0.0f, 0.0f};
  mutatedMetadata.vertices[0].boneIndices = {1, 0, 0};
  mutatedMetadata.renderIndices.push_back(0);
  mutatedMetadata.indices.push_back(tf2::native::ModelIndexDescriptor{0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 1});
  std::string mutationError;
  const bool bindPoseMutationRejected = !tf2::native::ModelLoader::buildBindPoseMesh(mutatedMetadata, 0, mutationError);
  tf2::native::ModelDrawVertex mutatedVertex{};
  mutatedVertex.boneCount = 1;
  mutatedVertex.weights = {std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f};
  mutatedVertex.boneIndices = {0, 0, 0};
  std::array<float, 3> mutatedPosition{};
  std::string mutatedSkinError;
  const bool cpuSkinWeightMutationRejected = !tf2::native::ModelLoader::cpuSkinVertexReference(
    mutatedMetadata, mutatedVertex, mutatedPosition, mutatedSkinError);
  writeAt<std::uint32_t>(bytes, 156, 0u);
  writeAt<std::uint32_t>(bytes, 160, 0u);
  writeAt<std::uint32_t>(bytes, 188, 1u);
  writeAt<std::uint32_t>(bytes, 192, 400u);
  { std::ofstream out(sequencePath, std::ios::binary); out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size()); }
  const auto sequenceInspection = tf2::native::ModelLoader::inspect(sequencePath);
  const bool sequenceRejected = hasDiagnostic(sequenceInspection, "sequence table is outside");
  std::error_code ignored;
  std::filesystem::remove(attachmentPath, ignored);
  std::filesystem::remove(bodyPartPath, ignored);
  std::filesystem::remove(bonePath, ignored);
  std::filesystem::remove(sequencePath, ignored);
  std::cout << "{\"attachmentOutOfBoundsRejected\":" << (attachmentRejected ? "true" : "false")
    << ",\"bodyPartOutOfBoundsRejected\":" << (bodyPartRejected ? "true" : "false")
    << ",\"boneTableOutOfBoundsRejected\":" << (boneRejected ? "true" : "false")
    << ",\"bindPoseMutationRejected\":" << (bindPoseMutationRejected ? "true" : "false")
    << ",\"cpuSkinWeightMutationRejected\":" << (cpuSkinWeightMutationRejected ? "true" : "false")
    << ",\"sequenceTableOutOfBoundsRejected\":" << (sequenceRejected ? "true" : "false")
    << ",\"corruptPosePreflight\":" << ((!attachmentPreflight && !bodyPartPreflight) ? "false" : "true") << "}\n";
  return attachmentRejected && bodyPartRejected && boneRejected && bindPoseMutationRejected
    && cpuSkinWeightMutationRejected && sequenceRejected
    && !attachmentPreflight && !bodyPartPreflight ? 0 : 1;
}
// ===========================================================================
// T61 -- THE PLAYER ANIMATION PROBE
//
// The product now drives a per-player animation clock from the demo's own
// ticks and the player's own speed. That chain has four links that can each
// be wrong on their own: the sequence table the product picks from, the pose
// the sampler returns for a cycle, the continuity of that pose from one tick
// to the next, and the clock arithmetic that produces the cycle. This mode
// prints one line per link so a gate can assert the product's own answers
// instead of a second implementation's.
//
// WHAT IS A READING AND WHAT IS A VERDICT. animtable/animpose/animcont/
// animrate/boundDiag are readings; the gate holds the thresholds. The mode
// exits non-zero only when a reading could not be produced at all (a missing
// model, a sequence that does not exist, a non-finite pose), because "the
// probe could not measure" must not look like "the measurement was bad".
// ===========================================================================

struct PlayerAnimationOptions {
  std::string tfRoot;
  std::string modelPath;
  std::string demoPath;
  std::vector<std::int32_t> ticks;
  std::int32_t sequence = -1;
  std::int32_t idleSequence = -1;
  std::int32_t movingSequence = -1;
  std::int32_t motionWindowStart = -1;
  std::int32_t motionWindowEnd = -1;
};

int failPlayerAnimation(const std::string& reason, const std::string& detail) {
  std::cerr << "FAIL player-animation-test reason=" << reason;
  if (!detail.empty()) std::cerr << " detail=" << detail;
  std::cerr << "\n";
  return 2;
}

std::string lowerCopy(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
    [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
  return value;
}

std::uint64_t poseChecksum(const std::vector<std::array<float, 16>>& matrices) {
  std::uint64_t hash = 1469598103934665603ull;
  for (const auto& matrix : matrices) {
    for (float value : matrix) {
      std::uint32_t bits = 0;
      if (value != 0.0f) std::memcpy(&bits, &value, sizeof(bits));
      hash ^= bits;
      hash *= 1099511628211ull;
    }
  }
  return hash;
}

float maxDeltaFromBind(const std::vector<std::array<float, 16>>& matrices) {
  float worst = 0.0f;
  for (const auto& matrix : matrices) {
    for (int column = 0; column < 4; ++column) {
      for (int row = 0; row < 4; ++row) {
        const float expected = row == column ? 1.0f : 0.0f;
        const float delta = std::fabs(matrix[column * 4 + row] - expected);
        if (delta > worst) worst = delta;
      }
    }
  }
  return worst;
}

float maxMatrixDelta(const std::vector<std::array<float, 16>>& first,
    const std::vector<std::array<float, 16>>& second) {
  float worst = 0.0f;
  for (std::size_t bone = 0; bone < first.size() && bone < second.size(); ++bone) {
    for (std::size_t element = 0; element < 16; ++element) {
      const float delta = std::fabs(first[bone][element] - second[bone][element]);
      if (delta > worst) worst = delta;
    }
  }
  return worst;
}

bool allFinite(const std::vector<std::array<float, 16>>& matrices) {
  for (const auto& matrix : matrices) {
    for (float value : matrix) if (!std::isfinite(value)) return false;
  }
  return true;
}

std::int32_t findSequenceByNeedles(const tf2::native::AnimationModel& anims,
    const std::vector<std::string>& needles) {
  for (const auto& needle : needles) {
    for (std::size_t i = 0; i < anims.sequences.size(); ++i) {
      if (lowerCopy(anims.sequences[i].label) == needle) return static_cast<std::int32_t>(i);
    }
  }
  for (const auto& needle : needles) {
    for (std::size_t i = 0; i < anims.sequences.size(); ++i) {
      if (lowerCopy(anims.sequences[i].label).find(needle) != std::string::npos) {
        return static_cast<std::int32_t>(i);
      }
    }
  }
  return -1;
}

std::int32_t animationFrameCount(const tf2::native::AnimationModel& anims, std::int32_t sequence) {
  if (sequence < 0 || static_cast<std::size_t>(sequence) >= anims.sequences.size()) return 0;
  const std::int32_t descriptor = anims.sequences[static_cast<std::size_t>(sequence)].animDesc;
  if (descriptor < 0 || static_cast<std::size_t>(descriptor) >= anims.animations.size()) return 0;
  return anims.animations[static_cast<std::size_t>(descriptor)].frameCount;
}

// A sequence the sampler actually moves through. The label-based pick can land
// on a single-frame aim pose (PRIMARY_aimmatrix_run carries frameCount=1), which
// is a valid sequence but cannot demonstrate a cycle: four quarter-cycle samples
// would all be the same frame. This walks candidates and keeps the first whose
// four quarter-cycle poses are pairwise different, preferring the product's own
// moving pick and then the walk/run family.
std::int32_t findAnimatingSequence(const tf2::native::AnimationBinding& binding,
    const tf2::native::AnimationModel& anims, std::int32_t preferred, std::string* label) {
  bool sawNonFinite = false;
  const auto quarterPosesDiffer = [&](std::int32_t sequence) {
    if (animationFrameCount(anims, sequence) < 4) return false;
    tf2::native::AnimationSampleRequest request;
    request.sequence = sequence;
    request.hasCycle = true;
    request.tickRate = 30.0f;
    std::vector<std::uint64_t> checksums;
    for (int quarter = 0; quarter < 4; ++quarter) {
      request.cycle = 0.25f * static_cast<float>(quarter);
      const tf2::native::AnimationSampleResult sample =
        tf2::native::buildAnimationSkinMatrices(&binding, request);
      if (sample.status != tf2::native::AnimationInputStatus::Ok || sample.bindPose
          || sample.skinMatrices.empty()) return false;
      if (!allFinite(sample.skinMatrices)) {
        sawNonFinite = true;
        return false;
      }
      checksums.push_back(poseChecksum(sample.skinMatrices));
    }
    for (std::size_t a = 0; a < checksums.size(); ++a) {
      for (std::size_t b = a + 1; b < checksums.size(); ++b) {
        if (checksums[a] == checksums[b]) return false;
      }
    }
    return true;
  };
  const auto trySequence = [&](std::int32_t sequence) {
    if (sequence < 0) return false;
    if (!quarterPosesDiffer(sequence)) return false;
    if (label != nullptr) *label = anims.sequences[static_cast<std::size_t>(sequence)].label;
    return true;
  };
  if (trySequence(preferred)) return preferred;
  static const char* const kNeedles[] = {"walk", "run", "move"};
  for (const char* needle : kNeedles) {
    for (std::size_t i = 0; i < anims.sequences.size(); ++i) {
      if (lowerCopy(anims.sequences[i].label).find(needle) == std::string::npos) continue;
      if (trySequence(static_cast<std::int32_t>(i))) return static_cast<std::int32_t>(i);
    }
  }
  for (std::size_t i = 0; i < anims.sequences.size(); ++i) {
    if (trySequence(static_cast<std::int32_t>(i))) return static_cast<std::int32_t>(i);
  }
  // -2 names the real cause: every candidate was rejected because a matrix
  // element was not finite, not because the poses were identical.
  return sawNonFinite ? -2 : -1;
}

// C needs a clip whose per-frame step is small compared with the skeleton, yet
// whose half-cycle swing is large enough that a cycle jumping by 0.5 (mutation
// m4) is unmistakably a jump. Both properties are measured, not estimated: a
// fast clip can move a matrix element by 72 units between two adjacent frames,
// and an average over a quarter cycle hides exactly that jump. Candidates are
// ranked by a sparse sweep of adjacent frame pairs and then validated on every
// frame pair the C sweep can land on (the first half of the clip).
std::int32_t findContinuitySequence(const tf2::native::AnimationBinding& binding,
    const tf2::native::AnimationModel& anims, float boundDiag, std::string* label,
    float* stepOut) {
  struct Candidate {
    std::int32_t sequence;
    std::string label;
    std::int32_t frames;
    float sparse;
    float half;
  };
  std::vector<Candidate> candidates;
  const auto sampleAt = [&](std::int32_t sequence, float cycle,
      std::vector<std::array<float, 16>>* out) {
    tf2::native::AnimationSampleRequest request;
    request.sequence = sequence;
    request.hasCycle = true;
    request.tickRate = 30.0f;
    request.cycle = cycle;
    const tf2::native::AnimationSampleResult sample =
      tf2::native::buildAnimationSkinMatrices(&binding, request);
    if (sample.status != tf2::native::AnimationInputStatus::Ok || sample.bindPose
        || sample.skinMatrices.empty() || !allFinite(sample.skinMatrices)) return false;
    *out = sample.skinMatrices;
    return true;
  };
  // A frame k is sampled at cycle (k + 0.5) / frames so floating point cannot
  // round the sample down onto frame k - 1.
  const auto frameCycle = [](std::int32_t frame, std::int32_t frames) {
    return (static_cast<float>(frame) + 0.5f) / static_cast<float>(frames);
  };
  for (std::size_t index = 0; index < anims.sequences.size(); ++index) {
    const std::int32_t sequence = static_cast<std::int32_t>(index);
    const std::int32_t frames = animationFrameCount(anims, sequence);
    if (frames < 8) continue;
    std::vector<std::array<float, 16>> zero, half;
    if (!sampleAt(sequence, 0.0f, &zero) || !sampleAt(sequence, 0.5f, &half)) continue;
    const float halfSwing = maxMatrixDelta(zero, half);
    if (!(halfSwing > boundDiag * 0.5f)) continue;
    const std::int32_t halfFrames = std::max<std::int32_t>(2, frames / 2);
    const std::int32_t stride = std::max<std::int32_t>(1, halfFrames / 12);
    float sparse = 0.0f;
    bool ok = true;
    for (std::int32_t frame = 0; frame + 1 < halfFrames && ok; frame += stride) {
      std::vector<std::array<float, 16>> a, b;
      if (!sampleAt(sequence, frameCycle(frame, frames), &a)
          || !sampleAt(sequence, frameCycle(frame + 1, frames), &b)) {
        ok = false;
        break;
      }
      sparse = std::max(sparse, maxMatrixDelta(a, b));
    }
    if (!ok) continue;
    candidates.push_back({sequence, anims.sequences[index].label, frames, sparse, halfSwing});
  }
  std::sort(candidates.begin(), candidates.end(),
    [](const Candidate& a, const Candidate& b) { return a.sparse < b.sparse; });
  for (std::size_t i = 0; i < candidates.size() && i < 5; ++i) {
    std::printf("contcand rank=%zu sequence=%d label=%s frames=%d sparseStep=%.6f halfSwing=%.6f\n",
      i, candidates[i].sequence, candidates[i].label.c_str(), candidates[i].frames,
      candidates[i].sparse, candidates[i].half);
  }
  // Validate with the exact sweep C runs. The sparse ranking can miss a single
  // frame jump, and the sampler's frame mapping is its own: only this sweep
  // measures the number the gate will assert on.
  const auto sweepMaxStep = [&](std::int32_t sequence, float* out) {
    tf2::native::AnimationClockState clock;
    tf2::native::AnimationSampleRequest request;
    request.sequence = sequence;
    request.hasCycle = true;
    request.tickRate = 60.0f;
    std::vector<std::array<float, 16>> previous;
    float worst = 0.0f;
    for (std::int32_t tick = 0; tick <= 60; ++tick) {
      const tf2::native::AnimationClockStep step =
        tf2::native::advanceAnimationClock(clock, tick, 60.0f, 125.0f);
      request.cycle = step.cycle;
      const tf2::native::AnimationSampleResult sample =
        tf2::native::buildAnimationSkinMatrices(&binding, request);
      if (sample.status != tf2::native::AnimationInputStatus::Ok || sample.bindPose
          || !allFinite(sample.skinMatrices)) return false;
      if (!previous.empty()) worst = std::max(worst, maxMatrixDelta(sample.skinMatrices, previous));
      previous = sample.skinMatrices;
    }
    *out = worst;
    return true;
  };
  const float limit = boundDiag * 0.25f;
  for (std::size_t i = 0; i < candidates.size() && i < 16; ++i) {
    float sweep = 0.0f;
    if (!sweepMaxStep(candidates[i].sequence, &sweep)) continue;
    if (!(sweep > 0.0f) || sweep > limit * 0.98f) continue;
    std::printf("contpick sequence=%d label=%s frames=%d sweepStep=%.6f limit=%.6f halfSwing=%.6f\n",
      candidates[i].sequence, candidates[i].label.c_str(), candidates[i].frames, sweep, limit,
      candidates[i].half);
    if (label != nullptr) *label = candidates[i].label;
    if (stepOut != nullptr) *stepOut = sweep;
    return candidates[i].sequence;
  }
  return -1;
}

std::string labelList(const tf2::native::AnimationModel& anims, std::size_t limit) {
  std::string out;
  const std::size_t count = std::min(limit, anims.sequences.size());
  for (std::size_t i = 0; i < count; ++i) {
    std::string label = anims.sequences[i].label;
    for (char& character : label) {
      if (character == '|' || std::isspace(static_cast<unsigned char>(character))) character = '_';
    }
    if (!out.empty()) out.push_back('|');
    out += label.empty() ? "-" : label;
  }
  if (anims.sequences.size() > count) out += "|...";
  return out;
}

// A deterministic tick script: half a second of a moving player, then half a
// second of a standing one. It is the shape the product sees -- speed from an
// origin delta -- without needing a recording, so the clock rule is asserted
// exactly and not through demo jitter.
void printSyntheticTicks() {
  tf2::native::AnimationClockState clock;
  std::size_t movingTicks = 0, movingAdvanced = 0, idleTicks = 0, idleAdvanced = 0;
  float maxMovingDelta = 0.0f, maxIdleDelta = 0.0f;
  for (std::int32_t tick = 0; tick <= 60; ++tick) {
    const float speed = tick <= 30 ? 300.0f : 0.0f;
    const auto step = tf2::native::advanceAnimationClock(clock, tick, 60.0f, speed);
    if (step.state == tf2::native::AnimationDriveState::Moving) {
      ++movingTicks;
      if (step.advanced) ++movingAdvanced;
      maxMovingDelta = std::max(maxMovingDelta, step.deltaCycle);
    } else {
      ++idleTicks;
      if (step.advanced) ++idleAdvanced;
      maxIdleDelta = std::max(maxIdleDelta, std::fabs(step.deltaCycle));
    }
    if (tick == 0 || tick == 10 || tick == 20 || tick == 30 || tick == 31
        || tick == 40 || tick == 50 || tick == 60) {
      std::printf("animtick mode=synthetic tick=%d speed=%.3f state=%s cycle=%.9f deltaCycle=%.9f advanced=%d\n",
        tick, speed, tf2::native::animationDriveStateName(step.state), step.cycle,
        step.deltaCycle, step.advanced ? 1 : 0);
    }
  }
  std::printf("animticks mode=synthetic ticks=61 movingTicks=%zu movingAdvanced=%zu idleTicks=%zu "
    "idleAdvanced=%zu maxMovingDelta=%.9f maxIdleDelta=%.9f\n",
    movingTicks, movingAdvanced, idleTicks, idleAdvanced, maxMovingDelta, maxIdleDelta);
}

// The same rule against a real recording: origins come from the entity history
// the product reads, speed is the distance between two consecutive sampled
// ticks times the tick rate, and one clock per player entity is advanced with
// it. A stale checkpoint repeats an origin, which reads as speed 0 and is a
// real idle tick -- the reading says how many of each were seen.
int printDemoTicks(const PlayerAnimationOptions& options) {
  using namespace tf2::native;
  if (options.motionWindowStart < 0 || options.motionWindowEnd <= options.motionWindowStart) {
    return failPlayerAnimation("motion-window-invalid", "expected --motion-window <first>,<last>");
  }
  DemoHeader header;
  if (!parseDemoHeaderFile(std::filesystem::path(options.demoPath), header)) {
    return failPlayerAnimation("demo-unreadable", options.demoPath);
  }
  DemoIndex index;
  if (!indexDemoFile(std::filesystem::path(options.demoPath), header, index)) {
    return failPlayerAnimation("demo-index-failed", options.demoPath);
  }
  DemoNetworkSummary summary;
  summary.networkProtocol = header.networkProtocol;
  if (!scanKnownDemoMessages(std::filesystem::path(options.demoPath), index, summary)) {
    return failPlayerAnimation("demo-scan-failed", options.demoPath);
  }
  bool haveFirst = false;
  const std::int32_t firstTick = firstRecordedPacketTick(index, haveFirst);
  constexpr float kDemoTickRate = 66.6667f;

  const auto sampleOrigins = [&](std::int32_t relativeTick,
      std::unordered_map<std::uint16_t, std::array<float, 3>>& origins,
      std::int32_t& resolvedTick) -> bool {
    std::vector<EntityState> states;
    const auto resolution = resolveOverlaySnapshot(summary, firstTick + relativeTick, states);
    if (resolution.fidelity == OverlaySnapshotFidelity::Unavailable) return false;
    resolvedTick = resolution.resolvedTick;
    const auto samples = sampleOverlayEntities(states, summary.serverClassSchemas,
      summary.modelPrecache, OverlayEntityFilter::Players, nullptr, nullptr);
    for (const auto& sample : samples) {
      if (!sample.hasOrigin) continue;
      origins[sample.entityIndex] = {sample.origin[0], sample.origin[1], sample.origin[2]};
    }
    return true;
  };

  std::unordered_map<std::uint16_t, std::array<float, 3>> previous;
  std::int32_t previousResolved = 0;
  if (!sampleOrigins(options.motionWindowStart, previous, previousResolved)) {
    return failPlayerAnimation("demo-window-unavailable",
      std::to_string(options.motionWindowStart));
  }
  std::unordered_map<std::uint16_t, AnimationClockState> clocks;
  std::size_t movingTicks = 0, movingAdvanced = 0, idleTicks = 0, idleAdvanced = 0;
  std::size_t transitions = 0, printed = 0, staleTicks = 0;
  float maxMovingDelta = 0.0f, maxIdleDelta = 0.0f;
  std::int32_t lastResolved = previousResolved;
  for (std::int32_t relative = options.motionWindowStart + 1;
       relative <= options.motionWindowEnd; ++relative) {
    std::unordered_map<std::uint16_t, std::array<float, 3>> current;
    std::int32_t resolved = 0;
    if (!sampleOrigins(relative, current, resolved)) continue;
    // The entity history answers most ticks from a retained checkpoint at or
    // before them (median gap ~822 ticks on bagel). A repeated answer is not a
    // tick the player stood still: it is the same snapshot read twice, so it
    // must not advance a clock and must not be counted as an idle tick.
    if (resolved == lastResolved) { ++staleTicks; continue; }
    lastResolved = resolved;
    for (const auto& [entity, origin] : current) {
      const auto previousIt = previous.find(entity);
      if (previousIt == previous.end()) continue;
      const float dx = origin[0] - previousIt->second[0];
      const float dy = origin[1] - previousIt->second[1];
      const float dz = origin[2] - previousIt->second[2];
      AnimationClockState& clock = clocks[entity];
      // The origins are separated by however many ticks the checkpoint
      // resolution skipped (median ~822), not by one tick, so the speed must
      // divide by that span: the product wiring does the same with
      // elapsedTicks from consecutive scene ticks.
      const float spanTicks = clock.hasPrevious
        ? static_cast<float>(resolved - clock.lastTick) : 1.0f;
      const float speed = std::sqrt(dx * dx + dy * dy + dz * dz) * kDemoTickRate
        / (spanTicks > 0.0f ? spanTicks : 1.0f);
      // A clock's first observation is a baseline: it advances nothing by
      // definition, so it is counted in the summary but never printed. Every
      // printed line therefore has a previous tick, and a printed Moving line
      // that did not advance (or an Idle line that did) is a real defect.
      const bool hadPrevious = clock.hasPrevious;
      const auto step = advanceAnimationClock(clock, resolved, kDemoTickRate, speed);
      ++transitions;
      if (step.state == AnimationDriveState::Moving) {
        ++movingTicks;
        if (step.advanced) ++movingAdvanced;
        maxMovingDelta = std::max(maxMovingDelta, step.deltaCycle);
      } else {
        ++idleTicks;
        if (step.advanced) ++idleAdvanced;
        maxIdleDelta = std::max(maxIdleDelta, std::fabs(step.deltaCycle));
      }
      if (printed < 8 && hadPrevious) {
        std::printf("animtick mode=demo tick=%d entity=%u speed=%.3f state=%s cycle=%.9f "
          "deltaCycle=%.9f advanced=%d\n", resolved, static_cast<unsigned>(entity), speed,
          animationDriveStateName(step.state), step.cycle, step.deltaCycle, step.advanced ? 1 : 0);
        ++printed;
      }
    }
    previous.swap(current);
  }
  std::printf("animticks mode=demo window=%d,%d transitions=%zu entities=%zu movingTicks=%zu "
    "movingAdvanced=%zu idleTicks=%zu idleAdvanced=%zu staleTicks=%zu "
    "maxMovingDelta=%.9f maxIdleDelta=%.9f\n",
    options.motionWindowStart, options.motionWindowEnd, transitions, clocks.size(), movingTicks,
    movingAdvanced, idleTicks, idleAdvanced, staleTicks, maxMovingDelta, maxIdleDelta);
  if (transitions == 0) return failPlayerAnimation("demo-no-players", options.demoPath);
  if (movingTicks == 0) return failPlayerAnimation("demo-no-motion", options.demoPath);
  return 0;
}

int playerAnimationTest(int argc, char** argv) {
  using namespace tf2::native;
  PlayerAnimationOptions options;
  for (int i = 1; i < argc; ++i) {
    const std::string argument = argv[i];
    const auto next = [&](const char* what) -> const char* {
      if (i + 1 >= argc) { std::cerr << "missing value for " << what << "\n"; return nullptr; }
      return argv[++i];
    };
    if (argument == "--player-animation-test") continue;
    else if (argument == "--tf-root") { const char* v = next("--tf-root"); if (!v) return 2; options.tfRoot = v; }
    else if (argument == "--model") { const char* v = next("--model"); if (!v) return 2; options.modelPath = v; }
    else if (argument == "--demo") { const char* v = next("--demo"); if (!v) return 2; options.demoPath = v; }
    else if (argument == "--sequence") { const char* v = next("--sequence"); if (!v) return 2; options.sequence = std::atoi(v); }
    else if (argument == "--idle-sequence") { const char* v = next("--idle-sequence"); if (!v) return 2; options.idleSequence = std::atoi(v); }
    else if (argument == "--moving-sequence") { const char* v = next("--moving-sequence"); if (!v) return 2; options.movingSequence = std::atoi(v); }
    else if (argument == "--ticks") {
      const char* v = next("--ticks"); if (!v) return 2;
      std::string list = v; std::size_t start = 0;
      while (start <= list.size()) {
        const std::size_t comma = list.find(',', start);
        const std::string piece = list.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        if (!piece.empty()) options.ticks.push_back(std::atoi(piece.c_str()));
        if (comma == std::string::npos) break;
        start = comma + 1;
      }
    }
    else if (argument == "--motion-window") {
      const char* v = next("--motion-window"); if (!v) return 2;
      std::string list = v;
      const std::size_t comma = list.find(',');
      if (comma == std::string::npos) return failPlayerAnimation("motion-window-invalid", list);
      options.motionWindowStart = std::atoi(list.substr(0, comma).c_str());
      options.motionWindowEnd = std::atoi(list.substr(comma + 1).c_str());
    }
    else return failPlayerAnimation("unknown-argument", argument);
  }
  if (options.tfRoot.empty()) return failPlayerAnimation("tf-root-missing", "");
  if (options.modelPath.empty()) return failPlayerAnimation("model-missing-argument", "");

  const AssetRoot root = AssetRoot::fromPath(std::filesystem::u8path(options.tfRoot));
  if (!root.valid()) return failPlayerAnimation("tf-root-invalid", options.tfRoot);
  AnimationBindingCache cache;
  cache.reset(&root);
  AnimationInputStatus status = AnimationInputStatus::ModelMissing;
  std::string reason;
  const AnimationBinding* binding = cache.resolve(options.modelPath, status, reason);
  if (binding == nullptr) {
    const std::string name = animationInputStatusName(status);
    if (status == AnimationInputStatus::ModelMissing) {
      return failPlayerAnimation("model-missing", options.modelPath);
    }
    if (status == AnimationInputStatus::ModelHasNoAnimation) {
      return failPlayerAnimation("model-has-no-animation", options.modelPath + " " + reason);
    }
    return failPlayerAnimation("binding-" + name, reason);
  }
  const auto& anims = binding->anims;
  if (anims.sequences.size() < 2) {
    return failPlayerAnimation("sequence-table-too-small", std::to_string(anims.sequences.size()));
  }

  const auto validateSequence = [&](std::int32_t sequence, const char* what) -> int {
    if (sequence < 0) return 0;
    if (static_cast<std::size_t>(sequence) >= anims.sequences.size()) {
      return failPlayerAnimation("sequence-out-of-range",
        std::string(what) + "=" + std::to_string(sequence) + " sequences=" + std::to_string(anims.sequences.size()));
    }
    return 0;
  };
  if (const int rc = validateSequence(options.sequence, "--sequence")) return rc;
  if (const int rc = validateSequence(options.idleSequence, "--idle-sequence")) return rc;
  if (const int rc = validateSequence(options.movingSequence, "--moving-sequence")) return rc;

  std::int32_t idleSequence = options.idleSequence;
  if (idleSequence < 0) idleSequence = findSequenceByNeedles(anims, {"idle", "stand"});
  std::int32_t movingSequence = options.movingSequence;
  if (movingSequence < 0) movingSequence = findSequenceByNeedles(anims, {"run", "walk"});
  if (idleSequence < 0 || movingSequence < 0) {
    return failPlayerAnimation("sequence-picker-missing",
      "idle=" + std::to_string(idleSequence) + " moving=" + std::to_string(movingSequence));
  }
  std::int32_t poseSequence = options.sequence;
  std::string poseLabel;
  if (poseSequence < 0) {
    poseSequence = findAnimatingSequence(*binding, anims, movingSequence, &poseLabel);
    if (poseSequence == -2) {
      return failPlayerAnimation("non-finite-pose",
        "skin matrices contain a non-finite element");
    }
    if (poseSequence < 0) {
      return failPlayerAnimation("no-animating-sequence",
        "no sequence with four distinct quarter-cycle poses");
    }
  } else {
    poseLabel = anims.sequences[static_cast<std::size_t>(poseSequence)].label;
  }

  // The skeleton's bind radius, composed in model space. boundDiag is the
  // diameter of that sphere plus one unit: a pose that leaves it is not a pose
  // this model can make, and a per-tick step that leaves a quarter of it is a
  // jump rather than an animation.
  std::vector<std::int32_t> parents;
  std::vector<std::array<float, 3>> bindPositions;
  std::vector<std::array<float, 4>> bindRotations;
  for (const auto& bone : anims.bones) {
    parents.push_back(bone.parent);
    bindPositions.push_back(bone.position);
    bindRotations.push_back(bone.rotation);
  }
  std::vector<std::array<float, 3>> bindModelPositions;
  std::string composeReason;
  const bool composed = composeLocalToModel(parents, bindPositions, bindRotations,
    bindModelPositions, composeReason);
  float bindRadius = 0.0f;
  for (const auto& position : bindModelPositions) {
    const float length = std::sqrt(position[0] * position[0] + position[1] * position[1]
      + position[2] * position[2]);
    if (std::isfinite(length) && length > bindRadius) bindRadius = length;
  }
  const float boundDiag = 2.0f * bindRadius + 1.0f;

  std::printf("animtable model=%s animSource=%s separate=%d bones=%zu bindBones=%zu "
    "validBindBones=%zu sequences=%zu idleSeq=%d idleLabel=%s movingSeq=%d movingLabel=%s "
    "poseSeq=%d poseLabel=%s poseFrames=%d labels=%s\n",
    options.modelPath.c_str(), binding->animPath.c_str(), binding->separateAnimSource ? 1 : 0,
    binding->renderBones, binding->poseToBone.size(), binding->validBindBones,
    anims.sequences.size(), idleSequence, anims.sequences[static_cast<std::size_t>(idleSequence)].label.c_str(),
    movingSequence, anims.sequences[static_cast<std::size_t>(movingSequence)].label.c_str(),
    poseSequence, poseLabel.c_str(), animationFrameCount(anims, poseSequence),
    labelList(anims, 24).c_str());
  std::printf("boundDiag bindRadius=%.6f boundDiag=%.6f bones=%zu composed=%d reason=%s\n",
    bindRadius, boundDiag, anims.bones.size(), composed ? 1 : 0,
    composed ? "-" : composeReason.c_str());
  if (!composed || !(boundDiag > 1.0f) || !std::isfinite(boundDiag)) {
    std::cerr << "FAIL player-animation-test reason=bind-skeleton-unusable\n";
    return 1;
  }

  std::string continuityLabel;
  float continuityEstimate = 0.0f;
  const std::int32_t continuitySequence = findContinuitySequence(
    *binding, anims, boundDiag, &continuityLabel, &continuityEstimate);
  if (continuitySequence < 0) {
    return failPlayerAnimation("no-continuity-sequence",
      "no clip with a half-cycle swing above half the bound and a per-frame step below a quarter");
  }
  std::printf("animcontseq sequence=%d label=%s frames=%d sweepStep=%.6f\n",
    continuitySequence, continuityLabel.c_str(), animationFrameCount(anims, continuitySequence),
    continuityEstimate);

  // B: four positions in one cycle. The gate asserts the checksums differ --
  // a sampler that ignores the cycle returns the same pose four times.
  AnimationSampleRequest poseRequest;
  poseRequest.sequence = poseSequence;
  poseRequest.hasCycle = true;
  poseRequest.tickRate = 30.0f;
  bool posesUsable = true;
  // The four poses are produced by the same clock the product runs, not by a
  // literal cycle: quarter-second steps at 250 units/s (1 cycle/s) put the
  // cycle at 0, .25, .5, .75. A clock whose deltaCycle is zero (mutation m1)
  // collapses all four poses into one, which is the defect B exists to catch.
  AnimationClockState poseClock;
  for (int poseIndex = 0; poseIndex < 4; ++poseIndex) {
    const AnimationClockStep poseStep =
      advanceAnimationClock(poseClock, poseIndex * 15, 60.0f, 250.0f);
    poseRequest.cycle = poseStep.cycle;
    const AnimationSampleResult sample = buildAnimationSkinMatrices(binding, poseRequest);
    if (sample.status != AnimationInputStatus::Ok || sample.bindPose || sample.skinMatrices.empty()) {
      std::printf("animpose cycle=%.4f status=%s reason=%s usable=0\n", poseStep.cycle,
        animationInputStatusName(sample.status), sample.reason.c_str());
      posesUsable = false;
      continue;
    }
    const bool finite = allFinite(sample.skinMatrices);
    std::printf("animpose cycle=%.4f sequence=%d label=%s frame=%d/%d localFrame=%d sectionFrames=%d "
      "bones=%zu checksum=%016llx maxDelta=%.6f finite=%d\n",
      poseStep.cycle, poseSequence, sample.sequenceLabel.c_str(), sample.frame, sample.frameCount,
      sample.localFrame, sample.sectionFrames, sample.skinMatrices.size(),
      static_cast<unsigned long long>(poseChecksum(sample.skinMatrices)),
      maxDeltaFromBind(sample.skinMatrices), finite ? 1 : 0);
    if (!finite) posesUsable = false;
  }
  if (!posesUsable) {
    std::cerr << "FAIL player-animation-test reason=pose-unusable\n";
    return 1;
  }

  // C: continuity. 60 steps of one sixtieth of a second at 125 units/s (half a
  // cycle per second, so the run does not wrap inside the window), and the
  // largest per-element change between two consecutive poses. The clip is the
  // scanned continuity clip, not the pose clip: the bound is a claim about the
  // sampler's step, and a clip that moves 72 units in one frame would fail it
  // for being a fast clip rather than for being a jump.
  {
    AnimationClockState clock;
    AnimationSampleRequest request;
    request.sequence = continuitySequence;
    request.hasCycle = true;
    request.tickRate = 60.0f;
    std::vector<std::array<float, 16>> previous;
    float maxStepDelta = 0.0f;
    std::size_t worstBone = 0, worstElement = 0;
    std::int32_t worstTick = -1;
    bool usable = true;
    for (std::int32_t tick = 0; tick <= 60; ++tick) {
      const AnimationClockStep step = advanceAnimationClock(clock, tick, 60.0f, 125.0f);
      request.cycle = step.cycle;
      const AnimationSampleResult sample = buildAnimationSkinMatrices(binding, request);
      if (sample.status != AnimationInputStatus::Ok || sample.bindPose) { usable = false; break; }
      if (!allFinite(sample.skinMatrices)) { usable = false; break; }
      if (!previous.empty()) {
        for (std::size_t bone = 0; bone < sample.skinMatrices.size() && bone < previous.size(); ++bone) {
          for (std::size_t element = 0; element < 16; ++element) {
            const float delta = std::fabs(sample.skinMatrices[bone][element] - previous[bone][element]);
            if (delta > maxStepDelta) {
              maxStepDelta = delta;
              worstBone = bone;
              worstElement = element;
              worstTick = tick;
            }
          }
        }
      }
      previous = sample.skinMatrices;
    }
    std::printf("animcont sequence=%d steps=60 tickRate=60.000 speed=125.000 rate=%.6f "
      "finalCycle=%.9f maxStepDelta=%.6f worstBone=%zu worstElement=%zu worstTick=%d usable=%d\n",
      continuitySequence, animationRateForSpeed(125.0f), clock.cycle, maxStepDelta,
      worstBone, worstElement, worstTick, usable ? 1 : 0);
    if (!usable) { std::cerr << "FAIL player-animation-test reason=continuity-unusable\n"; return 1; }
  }

  // D: one second of ticks at rest and at run speed. The moving delta must be
  // the rate times the second; the idle delta must be exactly zero.
  {
    const auto runOneSecond = [](float speed) {
      AnimationClockState clock;
      float delta = 0.0f;
      for (std::int32_t tick = 0; tick <= 60; ++tick) {
        delta += advanceAnimationClock(clock, tick, 60.0f, speed).deltaCycle;
      }
      return delta;
    };
    const float idleRate = animationRateForSpeed(0.0f);
    const float movingRate = animationRateForSpeed(250.0f);
    const float idleDelta = runOneSecond(0.0f);
    const float movingDelta = runOneSecond(250.0f);
    // The expectation is derived from the CONFIG CONTRACT, not from the
    // function under test: strideUnits=250 and speed=250 give exactly 1.0
    // cycles/s inside [minMovingRate=0.05, maxMovingRate=3.0]. Deriving it from
    // animationRateForSpeed() would make D unfalsifiable -- a rate mutation
    // would move the expectation with it (T61 mutation m3 depends on this).
    const float expectedMoving = 250.0f / 250.0f;
    std::printf("animrate idleRate=%.6f movingRate=%.6f idleOneSecond=%.9f movingOneSecond=%.9f "
      "expected=%.9f\n", idleRate, movingRate, idleDelta, movingDelta, expectedMoving);
  }

  // E: the tick script (and, when a recording is given, the same rule against
  // real consecutive-tick origins).
  printSyntheticTicks();
  if (!options.demoPath.empty()) {
    const int rc = printDemoTicks(options);
    if (rc != 0) return rc;
  }
  return 0;
}

}

int main(int argc, char** argv) {
  if (argc == 2 && std::string(argv[1]) == "--self-test") return selfTest();
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--player-animation-test") return playerAnimationTest(argc, argv);
  }
  if (argc != 2) {
    std::cerr << "usage: model_pose_probe <model.mdl> | --self-test"
      << " | --player-animation-test --tf-root <tf> --model <model.mdl>"
      << " [--sequence N] [--idle-sequence N] [--moving-sequence N]"
      << " [--demo <demo.dem> --motion-window <first>,<last>]\n";
    return 2;
  }
  auto inspection = tf2::native::ModelLoader::inspect(
    std::filesystem::u8path(argv[1]));
  std::size_t invalidParents = 0;
  std::size_t invalidAttachments = 0;
  std::size_t invalidAttachmentAxes = 0;
  std::size_t invalidPoseToBone = 0;
  for (const auto& bone : inspection.metadata.bones) {
    if (bone.parent < -1 || bone.parent >= static_cast<std::int32_t>(inspection.metadata.bones.size())
        || !finite3(bone.position)) ++invalidParents;
    if (!bone.poseToBoneValid) ++invalidPoseToBone;
  }
  for (const auto& attachment : inspection.metadata.attachments) {
    if (attachment.bone < 0 || attachment.bone >= static_cast<std::int32_t>(inspection.metadata.bones.size())
        || !finite3(attachment.origin)) ++invalidAttachments;
    if (!finite3(attachment.axisX) || !finite3(attachment.axisY) || !finite3(attachment.axisZ)
        || length3(attachment.axisX) < 0.5f || length3(attachment.axisY) < 0.5f
        || length3(attachment.axisZ) < 0.5f) ++invalidAttachmentAxes;
  }
  const bool mdlReadable = inspection.mdl.signatureValid && inspection.metadata.valid;
  std::string bindPoseError;
  auto poseMetadata = inspection.metadata;
  const bool bindPoseReady = !poseMetadata.indices.empty()
    && tf2::native::ModelLoader::buildBindPoseMesh(poseMetadata, 0, bindPoseError);
  std::array<float, 3> referencePosition{};
  std::string referenceSkinError;
  const bool cpuSkinReferenceReady = bindPoseReady && !poseMetadata.bindPoseVertices.empty()
    && tf2::native::ModelLoader::cpuSkinVertexReference(inspection.metadata, poseMetadata.bindPoseVertices.front(), referencePosition, referenceSkinError);
  std::size_t invalidBindPoseWeights = 0;
  std::size_t invalidBindPoseBoneIndices = 0;
  std::size_t bindPoseWeightedVertices = 0;
  for (const auto& vertex : poseMetadata.bindPoseVertices) {
    const float weightSum = vertex.weights[0] + vertex.weights[1] + vertex.weights[2];
    if (!std::isfinite(weightSum) || weightSum < 0.99f || weightSum > 1.01f
        || !std::isfinite(vertex.weights[0]) || !std::isfinite(vertex.weights[1])
        || !std::isfinite(vertex.weights[2])) {
      ++invalidBindPoseWeights;
    }
    if (vertex.boneCount > 0) ++bindPoseWeightedVertices;
    for (std::size_t i = 0; i < vertex.boneCount && i < vertex.boneIndices.size(); ++i) {
      if (vertex.boneIndices[i] >= inspection.metadata.boneCount) {
        ++invalidBindPoseBoneIndices;
        break;
      }
    }
  }
  const bool posePreflight = mdlReadable && inspection.vvd.signatureValid
    && inspection.metadata.bones.size() > 0 && bindPoseReady;
  const bool passed = mdlReadable && invalidParents == 0 && invalidAttachments == 0
    && invalidAttachmentAxes == 0;
  const char* viewModelStatus = inspection.viewModelStatus == tf2::native::ModelFeatureStatus::Available ? "available"
    : inspection.viewModelStatus == tf2::native::ModelFeatureStatus::Missing ? "missing" : "unknown";
  std::cout << "{\"mdlReadable\":" << (mdlReadable ? "true" : "false")
    << ",\"bones\":" << inspection.metadata.bones.size()
    << ",\"mdlVersion\":" << inspection.metadata.version
    << ",\"boneStride\":" << inspection.metadata.boneStride
    << ",\"poseToBoneOffset\":" << inspection.metadata.poseToBoneOffset
    << ",\"sequenceFrameCountAvailable\":" << (inspection.metadata.sequenceFrameCountAvailable ? "true" : "false")
    << ",\"sequenceDecodeReason\":\"" << inspection.metadata.sequenceDecodeReason << "\""
    << ",\"attachments\":" << inspection.metadata.attachments.size()
    << ",\"bodyParts\":" << inspection.metadata.bodyParts.size()
    << ",\"invalidParents\":" << invalidParents
    << ",\"invalidAttachments\":" << invalidAttachments
    << ",\"invalidAttachmentAxes\":" << invalidAttachmentAxes
    << ",\"invalidPoseToBone\":" << invalidPoseToBone
    << ",\"companionSetComplete\":" << (inspection.renderableResourceSet ? "true" : "false")
    << ",\"posePreflight\":" << (posePreflight ? "true" : "false")
    << ",\"skinningVertexCount\":" << inspection.metadata.skinningVertexCount
    << ",\"invalidSkinVertexCount\":" << inspection.metadata.invalidSkinVertexCount
    << ",\"skinningDataAvailable\":" << (inspection.metadata.skinningDataAvailable ? "true" : "false")
    << ",\"bindPoseMatricesAvailable\":" << (inspection.metadata.bindPoseMatricesAvailable ? "true" : "false")
    << ",\"skinningBlockedReason\":\"" << inspection.metadata.skinningBlockedReason << "\""
    << ",\"bindPoseSkinningAttributes\":" << poseMetadata.bindPoseVertices.size()
    << ",\"bindPoseVertexCount\":" << poseMetadata.bindPoseVertexCount
    << ",\"vtxIndexCount\":" << inspection.metadata.vtxDiagnostics.vtxIndexCount
    << ",\"vtxDescriptorCount\":" << inspection.metadata.vtxDiagnostics.vtxDescriptorCount
    << ",\"vtxOutOfBoundsCount\":" << inspection.metadata.vtxDiagnostics.vtxOutOfBoundsCount
    << ",\"vtxDegenerateTriangleCount\":" << inspection.metadata.vtxDiagnostics.vtxDegenerateTriangleCount
    << ",\"bindPoseReady\":" << (bindPoseReady ? "true" : "false")
    << ",\"bindPoseWeightedVertices\":" << bindPoseWeightedVertices
    << ",\"invalidBindPoseWeights\":" << invalidBindPoseWeights
    << ",\"invalidBindPoseBoneIndices\":" << invalidBindPoseBoneIndices
    << ",\"bindPoseSkinningAttributesValid\":" << (bindPoseReady && invalidBindPoseWeights == 0 && invalidBindPoseBoneIndices == 0 ? "true" : "false")
    << ",\"skinMatricesUploaded\":false"
    << ",\"vertexWeightsApplied\":false"
    << ",\"cpuSkinReferenceReady\":" << (cpuSkinReferenceReady ? "true" : "false")
    << ",\"gpuSkinning\":\"missing\",\"viewModel\":\"" << viewModelStatus << "\""
    << ",\"viewModelResourceDetected\":" << (inspection.viewModelPathDetected ? "true" : "false")
    << ",\"viewModelResourceSetComplete\":" << (inspection.renderableResourceSet && inspection.viewModelPathDetected ? "true" : "false")
    << ",\"viewModelPathDetected\":" << (inspection.viewModelPathDetected ? "true" : "false") << "}\n";
  return passed ? 0 : 1;
}

