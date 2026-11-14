#include "demo_header.h"

#include "entity_model.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>

namespace tf2::native {

// The row width the header's sizing comment promises. If a field is added, the
// retention cost changes, and the number in that comment has to change with it --
// this assertion is what makes the two disagree loudly instead of quietly.
static_assert(sizeof(CameraTickSample) == 56, "CameraTickSample must stay 56 bytes");

namespace {

// THE SELECTION RULE IS entity_model's, NOT A SECOND COPY OF IT.
//
// A player carries the same logical property in several slots (a Local, a
// NonLocal and sometimes a plain one), and they are not equally fresh: on the POV
// demo the LocalPlayerExclusive origin sat 2379 ticks behind the checkpoint it was
// answered from. entity_model.cpp resolved that with one rule -- rankPropertyCandidates
// (rank by lastWriteTick, then exclusive rank, then name) -- and findProperty /
// readVectorProperty apply it. A camera row that picked its own slot would answer
// a different question from the one the renderer answers today, and the two would
// disagree invisibly. So every read below goes through the same function, and the
// only thing added here is the vectors-only filter readVectorProperty applies.
const EntityPropertyValue* findRankedProperty(const EntityState& state, const char* suffix,
                                              bool vectorsOnly) {
  const std::vector<PropertyCandidate> ranked = rankPropertyCandidates(state, suffix);
  for (const PropertyCandidate& candidate : ranked) {
    const auto entry = state.properties.find(candidate.name);
    if (entry == state.properties.end()) continue;
    const SendPropType type = entry->second.type;
    if (vectorsOnly && type != SendPropType::Vector && type != SendPropType::VectorXY) {
      continue;
    }
    return &entry->second;
  }
  return nullptr;
}

// Mirrors entity_model.cpp's readVectorProperty, including its handling of a
// VectorXY whose z lives in a separate "<base>[2]" sibling: Source flattens that
// pair, and reading only x/y would turn a real z into 0 without saying so. That is
// exactly why completeness is reported separately from the finite check -- a z
// that was never written is not a z of zero. Returns false when the value is
// absent or not finite; *complete is false when only the xy half could be read.
bool readVectorRanked(const EntityState& state, const char* base, float out[3], bool* complete) {
  const EntityPropertyValue* value = findRankedProperty(state, base, true);
  if (!value) {
    *complete = false;
    return false;
  }
  const bool carriesZ = value->type == SendPropType::Vector;
  out[0] = value->x;
  out[1] = value->y;
  out[2] = carriesZ ? value->z : 0.0f;
  *complete = carriesZ;
  if (!carriesZ) {
    const std::string zSuffix = std::string(base) + "[2]";
    const EntityPropertyValue* zSlot = findRankedProperty(state, zSuffix.c_str(), false);
    if (zSlot) {
      out[2] = zSlot->x;
      *complete = true;
    }
  }
  return std::isfinite(out[0]) && std::isfinite(out[1]) && std::isfinite(out[2]);
}

// Mirrors main.cpp's readIntSlot: the best-ranked candidate, Int read as intValue
// and anything else as its x, which is what the product does for a slot that a
// class declares as a plain scalar.
bool readIntRanked(const EntityState& state, const char* suffix, std::int32_t* out) {
  const EntityPropertyValue* value = findRankedProperty(state, suffix, false);
  if (!value) return false;
  const std::int64_t raw = value->type == SendPropType::Int
      ? value->intValue : static_cast<std::int64_t>(value->x);
  *out = static_cast<std::int32_t>(raw);
  return true;
}

// Reads one row's worth of state for `entityIndex` out of the live table. Returns
// false when there is nothing to describe: the index is beyond the table, or the
// slot holds no entity (classId -1). A row of zeros would be a claim that the
// entity exists at the origin with no flags, which is a different statement from
// "there is no entity here"; the caller counts those instead.
bool buildCameraTickSample(const DemoNetworkSummary& summary, std::uint32_t entityIndex,
                           CameraTickSample& sample) {
  if (entityIndex >= summary.entityStates.size()) return false;
  const EntityState& state = summary.entityStates[entityIndex];
  const std::int32_t classId = entityIndex < summary.entityClassByIndex.size()
      ? summary.entityClassByIndex[entityIndex] : -1;
  if (classId < 0) return false;

  sample = CameraTickSample{};
  sample.entityIndex = static_cast<std::uint16_t>(entityIndex);
  sample.classId = classId;
  if (entityIndex < summary.projectileTrackSlotGeneration.size()) {
    sample.generation = summary.projectileTrackSlotGeneration[entityIndex];
  }

  bool complete = false;
  if (readVectorRanked(state, "m_vecOrigin", sample.origin, &complete) && complete) {
    sample.flags |= kCameraHasOrigin;
  }
  // The view offset is reported as read, never gated here: the (0, 300] sanity
  // range the POV camera applies to it is the consumer's policy (main.cpp:517-523),
  // and a retention that silently dropped an implausible value would hide the very
  // readings that policy is tested against.
  float viewOffset[3] = {};
  complete = false;
  if (readVectorRanked(state, "m_vecViewOffset", viewOffset, &complete) && complete) {
    sample.viewOffsetZ = viewOffset[2];
    sample.flags |= kCameraHasViewOffset;
  }
  // Eye angles are what the camera is turned with; m_angRotation is the fallback
  // for entities that carry no eye angles (a non-player observer body).
  complete = false;
  if (readVectorRanked(state, "m_angEyeAngles", sample.angles, &complete) && complete) {
    sample.flags |= kCameraHasAngles;
  } else {
    complete = false;
    if (readVectorRanked(state, "m_angRotation", sample.angles, &complete) && complete) {
      sample.flags |= kCameraHasAngles;
    }
  }
  std::int32_t scalar = 0;
  if (readIntRanked(state, "m_lifeState", &scalar) || readIntRanked(state, "m_iLifeState", &scalar)) {
    sample.lifeState = scalar;
    sample.flags |= kCameraHasLifeState;
  }
  if (readIntRanked(state, "m_iObserverMode", &scalar)) {
    sample.observerMode = scalar;
    sample.flags |= kCameraHasObserverMode;
  }
  return true;
}

// The half-open [begin, end) row range whose tick equals / lies in the range.
// Precondition, enforced by cameraTickOrderingHolds rather than assumed:
// cameraTickSamples is tick non-decreasing, because rows are appended in packet
// order.
std::size_t cameraLowerBound(const std::vector<CameraTickSample>& rows, std::int32_t tick) {
  std::size_t low = 0, high = rows.size();
  while (low < high) {
    const std::size_t middle = low + (high - low) / 2;
    if (rows[middle].tick < tick) low = middle + 1;
    else high = middle;
  }
  return low;
}

std::size_t cameraUpperBound(const std::vector<CameraTickSample>& rows, std::int32_t tick) {
  std::size_t low = 0, high = rows.size();
  while (low < high) {
    const std::size_t middle = low + (high - low) / 2;
    if (rows[middle].tick <= tick) low = middle + 1;
    else high = middle;
  }
  return low;
}

} // namespace

bool cameraTickOrderingHolds(const DemoNetworkSummary& summary, std::size_t* firstInversion) {
  for (std::size_t i = 1; i < summary.cameraTickSamples.size(); ++i) {
    if (summary.cameraTickSamples[i].tick < summary.cameraTickSamples[i - 1].tick) {
      if (firstInversion) *firstInversion = i;
      return false;
    }
  }
  return true;
}

void resetCameraCaptureForPacket(DemoNetworkSummary& summary) {
  summary.cameraCaptureSeen = false;
  summary.cameraCaptureComplete = false;
  summary.cameraCaptureValid = false;
  summary.cameraCapturePacketTick = -1;
  summary.cameraCaptureSample = CameraTickSample{};
}

void captureCameraTickSample(DemoNetworkSummary& summary, std::int32_t packetTick) {
  summary.cameraCaptureSeen = true;
  summary.cameraCaptureComplete = true;
  summary.cameraCaptureValid = false;
  summary.cameraCapturePacketTick = -1;
  if (!summary.cameraTrackLimits.enabled) return;
  CameraTickSample sample;
  if (!buildCameraTickSample(summary, summary.lastViewEntity, sample)) return;
  summary.cameraCaptureSample = sample;
  summary.cameraCapturePacketTick = packetTick;
  summary.cameraCaptureValid = true;
}

void stampCameraTickRow(DemoNetworkSummary& summary, const DemoMessageStreamResult& stream,
                       std::size_t row, bool tickKnown) {
  (void)row;
  if (!summary.cameraTrackLimits.enabled) return;
  // THE ORDERING MEASUREMENT COMES FIRST, because it is a fact about the packet
  // and not about whether a row could be built: the question is whether svc_SetView
  // updated the view target before or after svc_PacketEntities rewrote the entity
  // table, and that answer is needed for every packet that carries both.
  if (stream.lastSetViewMessageBit >= 0) {
    ++summary.cameraPacketsWithSetView;
    if (stream.lastPacketEntitiesMessageBit >= 0) {
      if (stream.lastSetViewMessageBit > stream.lastPacketEntitiesMessageBit) {
        ++summary.cameraPacketsSetViewAfterEntities;
      } else {
        ++summary.cameraPacketsSetViewBeforeEntities;
      }
    }
  }
  // No final tick, no row: the row's whole value is that its tick is the tick the
  // playback clock publishes for this packet, and a guess would break that
  // equality silently.
  if (!tickKnown) {
    ++summary.cameraRowsNoTick;
    return;
  }
  const std::int32_t tick = stream.finalNetworkTick;
  const std::uint32_t target = summary.lastViewEntity;
  CameraTickSample sample;
  bool haveSample = false;
  if (summary.cameraCaptureValid) {
    sample = summary.cameraCaptureSample;
    haveSample = true;
    // The capture read the table while readPacketEntities was given ITS tick; the
    // stamp uses the tick the packet ends on. They differ when svc_NetTick follows
    // svc_PacketEntities inside the same packet. Counted rather than assumed equal:
    // on this corpus the two agreed (see the impl report), but the ordering is a
    // property of the wire, not of this layer.
    if (summary.cameraCapturePacketTick != tick) ++summary.cameraRowTickMismatch;
  } else if (summary.cameraCaptureComplete) {
    // svc_PacketEntities decoded to the end, but the view target's slot held no
    // state worth describing at that point in the demo. Counted with the tick, so
    // a run of them can be located instead of merely totaled.
    if (summary.cameraFirstNoTargetTick < 0) summary.cameraFirstNoTargetTick = tick;
    ++summary.cameraRowsNoTarget;
    return;
  } else if (summary.cameraCaptureSeen) {
    // The packet carried svc_PacketEntities and did not finish decoding it: the
    // live table may be half-updated, so this tick gets counted, not described.
    ++summary.cameraRowsIncompletePacket;
    return;
  } else {
    // No svc_PacketEntities in this packet at all, so the live table still holds
    // exactly the state this tick inherits -- reading it is the tick's real state,
    // not a fallback to an older one. (The POV demo has 25 such data packets.)
    if (!buildCameraTickSample(summary, target, sample)) {
      if (summary.cameraFirstNoTargetTick < 0) summary.cameraFirstNoTargetTick = tick;
      ++summary.cameraRowsNoTarget;
      return;
    }
    haveSample = true;
  }
  // The row must describe the target this packet ENDS with. A capture made before
  // svc_SetView describes the old target; a Delete+Enter inside the packet keeps
  // the index and changes the entity, which only the generation shows. Both are
  // rebuilt here from the table as this packet left it -- that table state IS the
  // state of this tick -- rather than dropped, and the two outcomes are counted
  // separately so a reader can tell how many ticks needed repair and how many the
  // repair produced.
  bool identityMismatch = target > 0xFFFFu;
  if (!identityMismatch && haveSample
      && sample.entityIndex != static_cast<std::uint16_t>(target)) {
    identityMismatch = true;
  }
  if (!identityMismatch && haveSample
      && sample.entityIndex < summary.projectileTrackSlotGeneration.size()
      && sample.generation != summary.projectileTrackSlotGeneration[sample.entityIndex]) {
    identityMismatch = true;
  }
  if (identityMismatch) {
    ++summary.cameraIdentityMismatch;
    CameraTickSample rebuilt;
    if (!buildCameraTickSample(summary, target, rebuilt)) {
      if (summary.cameraFirstNoTargetTick < 0) summary.cameraFirstNoTargetTick = tick;
      ++summary.cameraRowsNoTarget;
      return;
    }
    sample = rebuilt;
    ++summary.cameraIdentityRebuilt;
  }

  sample.tick = tick;
  const std::size_t limit = summary.cameraTrackLimits.maxSamples > 0
      ? summary.cameraTrackLimits.maxSamples : 1;
  // Grow for the packet in hand, not for the whole cap: maxSamples defaults to
  // 400k rows (22.4 MB) and a single pass over a 300k-packet demo would allocate
  // all of it up front for a vector that grows to whatever the demo actually
  // holds. The doubling cost is far below an unused reservation of that size.
  if (summary.cameraTickSamples.capacity() == 0) {
    summary.cameraTickSamples.reserve(std::min<std::size_t>(limit, 4096));
  }
  if (summary.cameraTickSamples.size() >= limit) {
    // Bounded, and the collision is counted rather than silently absorbed. This is
    // NOT a ring buffer: each eviction is O(n) (it shifts every retained row), so
    // past maxSamples the scan pays a full copy per packet. Unreachable at the
    // default cap on this corpus (the 300k-packet POV demo stays below 400k rows);
    // written down here so the next reader does not mistake it for free.
    summary.cameraTickSamples.erase(summary.cameraTickSamples.begin());
    ++summary.cameraRowsDropped;
  }
  if (!summary.cameraTickSamples.empty() && summary.cameraTickSamples.back().tick == tick) {
    // Several playback rows share one server tick when the recording holds a tick
    // across packets; the query answers with the last of them.
    ++summary.cameraDuplicateTickRows;
  }
  summary.cameraTickSamples.push_back(sample);
  ++summary.cameraRows;
}

CameraTickQueryResult queryCameraTickSample(const DemoNetworkSummary& summary, std::int32_t tick) {
  // Exact tick only. There is deliberately no at-or-before fallback: that fallback
  // is what turned "the properties at this tick" into "the properties at some
  // earlier tick", and its measured age on this corpus is 1707.8 ticks on average
  // (4576 worst). A caller that wants the distance to the nearest retained tick
  // reads previousTick; it does not get a sample it did not ask for.
  CameraTickQueryResult result;
  const std::vector<CameraTickSample>& rows = summary.cameraTickSamples;
  const std::size_t lower = cameraLowerBound(rows, tick);
  if (lower > 0) result.previousTick = rows[lower - 1].tick;
  if (lower >= rows.size()) return result;
  if (rows[lower].tick != tick) {
    result.nextTick = rows[lower].tick;
    return result;
  }
  const std::size_t upper = cameraUpperBound(rows, tick);
  result.hit = true;
  result.rowsAtTick = upper - lower;
  result.sample = &rows[upper - 1];
  if (upper < rows.size()) result.nextTick = rows[upper].tick;
  return result;
}

} // namespace tf2::native
