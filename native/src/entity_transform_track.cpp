#include "entity_transform_track.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <unordered_set>

#include "demo_header_internal.h"

namespace tf2::native {

namespace {

constexpr std::size_t kEntityTransformSlots = 2048u;
constexpr std::uint32_t kNoEntityTransformSample = 0xffffffffu;
// Source packs an entity index into the low 11 bits of a handle and a serial into
// the next 10 (basehandle.h: NUM_ENT_ENTRY_BITS 11, ENT_ENTRY_MASK). An invalid
// handle is all ones; the index is recorded only when the handle is valid.
constexpr std::uint32_t kTransformHandleIndexMask = 0x7ffu;
constexpr std::uint32_t kTransformInvalidHandle = 0xffffffffu;
// Cache-only bit on entityTransformSlotFlags: the classification below has run
// for the generation in entityTransformSlotGeneration. Slot flags are a separate
// column from the sample flags, so the top bit cannot collide with a row's bits.
constexpr std::uint16_t kEntityTransformSlotClassified = 1u << 15;

// The growth step of the row vector. Not a doubling: maxSamples bounds the rows,
// and a geometric vector can hold up to twice the rows it reports. Reserving in
// fixed 64k-row steps keeps capacity within 65536 rows (3 MiB) of size, which is
// what makes "<= 64 MB" a property of the design rather than of an allocator.
constexpr std::size_t kEntityTransformReserveStep = 65536u;

bool transformSuffixMatch(const std::string& name, const char* suffix) {
  const std::size_t length = std::strlen(suffix);
  if (name.size() < length) return false;
  if (name.compare(name.size() - length, length, suffix) != 0) return false;
  return name.size() == length || name[name.size() - length - 1] == '.';
}

// THE ORIGIN READER mirrors readTrackOrigin in demo_header_track.cpp, which in
// turn mirrors EntityModelResolver::extractTransform: match names ending in
// m_vecOrigin, freshest lastWriteTick wins, and a VectorXY slot contributes z = 0.
// It is a copy rather than a call because readTrackOrigin is private to that
// translation unit; the two stay in step because they implement the same written
// rule, and a divergence is a defect in whichever one changed without the other.
bool readTransformOrigin(const EntityState& state, float out[3]) {
  const EntityPropertyValue* best = nullptr;
  for (const auto& entry : state.properties) {
    if (!transformSuffixMatch(entry.first, "m_vecOrigin")) continue;
    const EntityPropertyValue& value = entry.second;
    if (value.type != SendPropType::Vector && value.type != SendPropType::VectorXY) continue;
    if (best == nullptr || value.lastWriteTick > best->lastWriteTick) best = &value;
  }
  if (best == nullptr) return false;
  out[0] = best->x;
  out[1] = best->y;
  out[2] = best->type == SendPropType::Vector ? best->z : 0.0f;
  for (int axis = 0; axis < 3; ++axis) {
    if (!std::isfinite(out[axis])) return false;
  }
  return true;
}

// THE ANGLE READER follows EntityModelResolver::extractTransform's order exactly:
// m_angEyeAngles as a Vector, then its split [0]/[1] pair, then m_angRotation.
// The order is load-bearing for players: a player carries both, and the eye
// angles are the ones the renderer orients the body with.
bool readTransformAngles(const EntityState& state, float out[3]) {
  const auto* angles = findPropertySuffix(state.properties, "m_angEyeAngles");
  if (angles != nullptr && (angles->type == SendPropType::Vector || angles->type == SendPropType::VectorXY)
      && std::isfinite(angles->x) && std::isfinite(angles->y) && std::isfinite(angles->z)) {
    out[0] = angles->x;
    out[1] = angles->y;
    out[2] = angles->z;
    return true;
  }
  const auto* pitch = findPropertySuffix(state.properties, "m_angEyeAngles[0]");
  const auto* yaw = findPropertySuffix(state.properties, "m_angEyeAngles[1]");
  if (pitch != nullptr && yaw != nullptr) {
    const auto scalar = [](const EntityPropertyValue& value) {
      return value.type == SendPropType::Int ? static_cast<float>(value.intValue) : value.x;
    };
    out[0] = scalar(*pitch);
    out[1] = scalar(*yaw);
    out[2] = 0.0f;
    if (std::isfinite(out[0]) && std::isfinite(out[1])) return true;
  }
  const auto* rotation = findPropertySuffix(state.properties, "m_angRotation");
  if (rotation != nullptr && (rotation->type == SendPropType::Vector || rotation->type == SendPropType::VectorXY)
      && std::isfinite(rotation->x) && std::isfinite(rotation->y) && std::isfinite(rotation->z)) {
    out[0] = rotation->x;
    out[1] = rotation->y;
    out[2] = rotation->type == SendPropType::Vector ? rotation->z : 0.0f;
    return true;
  }
  return false;
}

bool findTransformModelIndex(const EntityState& state, std::int64_t* out) {
  const auto* model = findPropertySuffix(state.properties, "m_nModelIndex");
  if (model == nullptr) return false;
  if (model->type == SendPropType::Int) {
    *out = model->intValue;
    return true;
  }
  if (model->type == SendPropType::Float) {
    *out = static_cast<std::int64_t>(model->x);
    return true;
  }
  return false;
}

// m_hMoveParent / m_iParentAttachment are the two fields a renderer needs to know
// that an entity's placement is its parent's, not its own. A valid handle is kept as
// the entity INDEX, because the retained row is about placement, not about handle
// serials. An INVALID handle (0xffffffff) means "no parent" and is reported as
// "field absent", not as index -1: the caller must not flag an unparented entity as
// parented, or CA-02b would skip its interpolation.
bool readTransformHandle(const EntityState& state, const char* suffix, std::int32_t* out) {
  const auto* value = findPropertySuffix(state.properties, suffix);
  if (value == nullptr) return false;
  std::int64_t raw = 0;
  if (value->type == SendPropType::Int) {
    raw = value->intValue;
  } else if (value->type == SendPropType::Float) {
    raw = static_cast<std::int64_t>(value->x);
  } else {
    return false;
  }
  const auto bits = static_cast<std::uint32_t>(raw);
  if (bits == kTransformInvalidHandle) {
    // 0xffffffff is Source's "no parent" sentinel, not a parent index of -1. Reporting
    // it as present made every entity that HAS the field look parented (C, CA-02a v2).
    return false;
  }
  *out = static_cast<std::int32_t>(bits & kTransformHandleIndexMask);
  return true;
}

bool readTransformInt(const EntityState& state, const char* suffix, std::int32_t* out) {
  const auto* value = findPropertySuffix(state.properties, suffix);
  if (value == nullptr) return false;
  if (value->type == SendPropType::Int) {
    *out = static_cast<std::int32_t>(value->intValue);
    return true;
  }
  if (value->type == SendPropType::Float) {
    *out = static_cast<std::int32_t>(value->x);
    return true;
  }
  return false;
}

}  // namespace

// Called once per completely decoded packet, immediately after the camera capture
// (demo_header_entity.cpp). It walks the live entity table for the same reason
// T53's append does: an entity that was not written on this packet still EXISTS
// at this tick, and its stored properties are the placement it holds.
//
// THE CHANGE-ONLY RULE is the whole point of this retention, and it is what makes
// a 30-minute recording affordable: static geometry retains one row, a parented
// cosmetic retains one row per attachment change, and only things that actually
// move pay per tick. The rule is Source's own (c_baseentity.cpp:2546-2568 sends a
// network update when a field changes, not once per tick), applied on the
// receiving side so that "changed" means "the value differs from the value last
// retained", not "the wire carried it".
void appendEntityTransformSamples(DemoNetworkSummary& summary, std::int32_t tick) {
  if (!summary.entityTransformLimits.enabled) return;
  // CA-02a tags rows with CA-01's dormancy bit. Arming here is a LATE SAFETY NET only:
  // this function runs after the packet's decode hooks, so a scan that was not armed
  // before it started has already lost the first packet's Leave events and bitmap
  // writes. Callers that need complete dormancy evidence (demo_open_probe does) must
  // call setEntityDormantEnabled BEFORE scanning; this line keeps later packets right.
  // Disabled runs never reach this function, so 054fb628 is untouched.
  setEntityDormantEnabled(summary, true);
  const EntityTransformLimits& limits = summary.entityTransformLimits;
  if (summary.entityTransformSlotSample.size() < kEntityTransformSlots) {
    summary.entityTransformSlotSample.assign(kEntityTransformSlots, kNoEntityTransformSample);
  }
  if (summary.entityTransformSlotGeneration.size() < kEntityTransformSlots) {
    summary.entityTransformSlotGeneration.assign(kEntityTransformSlots, 0u);
  }
  if (summary.entityTransformSlotFlags.size() < kEntityTransformSlots) {
    summary.entityTransformSlotFlags.assign(kEntityTransformSlots, 0u);
  }
  // The class mask is built once per recording, by NAME, from this recording's
  // server class table -- never from a pinned class id, which belongs to one
  // recording (see the T53 note: CTFViewModel measures 320 on one demo and 18 on
  // another). It is indexed by class id, which is dense in this corpus and is the
  // same indexing T53's projectileTrackClassMask uses.
  if (summary.entityTransformClassMask.size() < summary.serverClassSchemas.size()) {
    summary.entityTransformClassMask.assign(summary.serverClassSchemas.size(), 0u);
    for (const ServerClassSchema& schema : summary.serverClassSchemas) {
      if (static_cast<std::size_t>(schema.id) >= summary.entityTransformClassMask.size()) continue;
      if (schema.name.rfind("CTFPlayer", 0) == 0) {
        summary.entityTransformClassMask[schema.id] = kEntityTransformPlayerClass;
      }
    }
  }
  ++summary.entityTransformPacketsScanned;
  // Once the row budget is spent the capture stops for good and says so. Later
  // packets are not walked at all: continuing to walk them would pay the per-packet
  // cost for rows that cannot be stored, and would inflate slotsWalked into a
  // number that describes work instead of coverage.
  if (summary.entityTransformCaptureStopped) return;
  const std::int32_t stride = limits.minTickStride > 1 ? limits.minTickStride : 1;
  const std::size_t maxSamples = limits.maxSamples > 0 ? limits.maxSamples : 1u;
  const auto started = std::chrono::steady_clock::now();
  std::size_t walked = 0;
  const std::size_t walk = std::min(kEntityTransformSlots, summary.entityStates.size());
  for (std::size_t slot = 0; slot < walk; ++slot) {
    const std::int32_t classId = slot < summary.entityClassByIndex.size() ? summary.entityClassByIndex[slot] : -1;
    if (classId < 0) continue;
    ++walked;
    const std::uint32_t generation = slot < summary.projectileTrackSlotGeneration.size()
        ? summary.projectileTrackSlotGeneration[slot] : 0u;
    std::uint16_t slotFlags = summary.entityTransformSlotFlags[slot];
    // Classification is cached per (slot, generation): scanning an entity's
    // properties for m_nModelIndex once per packet for every slot on the map is
    // the same mistake T53 measured at 727k string comparisons per packet.
    if (summary.entityTransformSlotGeneration[slot] != generation || (slotFlags & kEntityTransformSlotClassified) == 0) {
      // A plain conditional over two uint16_t promotes to int, and narrowing that
      // back would trip C4244 under /W4 /WX; the branch keeps every step uint16_t.
      std::uint16_t classBits = static_cast<std::uint16_t>(0u);
      if (static_cast<std::size_t>(classId) < summary.entityTransformClassMask.size()) {
        classBits = summary.entityTransformClassMask[static_cast<std::size_t>(classId)];
      }
      slotFlags = kEntityTransformSlotClassified;
      if ((classBits & kEntityTransformPlayerClass) != 0) {
        slotFlags = static_cast<std::uint16_t>(slotFlags | kEntityTransformPlayerClass);
      }
      std::int64_t modelIndex = 0;
      if (findTransformModelIndex(summary.entityStates[slot], &modelIndex) && modelIndex != 0) {
        slotFlags = static_cast<std::uint16_t>(slotFlags | kEntityTransformDrawableByModel);
      }
      if ((slotFlags & (kEntityTransformPlayerClass | kEntityTransformDrawableByModel)) == 0) {
        ++summary.entityTransformLifetimesNotDrawable;
      }
      summary.entityTransformSlotFlags[slot] = slotFlags;
      summary.entityTransformSlotGeneration[slot] = generation;
    }
    const std::uint16_t drawableBits = static_cast<std::uint16_t>(slotFlags & (kEntityTransformPlayerClass | kEntityTransformDrawableByModel));
    if (drawableBits == 0) {
      ++summary.entityTransformSlotsSkipped;
      continue;
    }
    const EntityState& state = summary.entityStates[slot];
    float origin[3] = {0.0f, 0.0f, 0.0f};
    float angles[3] = {0.0f, 0.0f, 0.0f};
    std::uint16_t sampleFlags = drawableBits;
    if (readTransformOrigin(state, origin)) sampleFlags = static_cast<std::uint16_t>(sampleFlags | kEntityTransformHasOrigin);
    if (readTransformAngles(state, angles)) sampleFlags = static_cast<std::uint16_t>(sampleFlags | kEntityTransformHasAngles);
    std::int32_t moveParent = -1;
    std::int32_t parentAttachment = 0;
    const bool hasMoveParent = readTransformHandle(state, "m_hMoveParent", &moveParent);
    if (hasMoveParent) {
      sampleFlags = static_cast<std::uint16_t>(sampleFlags | kEntityTransformHasMoveParent);
      // -1 is the "no attachment point" sentinel, and without a parent the field
      // describes nothing, so flag it only when both are present.
      if (readTransformInt(state, "m_iParentAttachment", &parentAttachment) && parentAttachment != -1) {
        sampleFlags = static_cast<std::uint16_t>(sampleFlags | kEntityTransformHasParentAttachment);
      }
    }
    if (slot < summary.entityDormant.size() && summary.entityDormant[slot] != 0) sampleFlags = static_cast<std::uint16_t>(sampleFlags | kEntityTransformDormant);
    const std::uint32_t lastIndex = summary.entityTransformSlotSample[slot];
    if (lastIndex != kNoEntityTransformSample && lastIndex < summary.entityTransformSamples.size()) {
      const EntityTransformSample& last = summary.entityTransformSamples[lastIndex];
      if (last.generation == generation) {
        const bool sameFlags = last.flags == sampleFlags;
        const bool sameParent = last.moveParent == moveParent && last.parentAttachment == parentAttachment;
        const bool sameOrigin = std::memcmp(last.origin, origin, sizeof(origin)) == 0;
        const bool sameAngles = std::memcmp(last.angles, angles, sizeof(angles)) == 0;
        if (sameFlags && sameParent && sameOrigin && sameAngles) {
          ++summary.entityTransformRowsUnchanged;
          continue;
        }
        // Stride downsamples MOVEMENT only. A structural transition (dormancy,
        // parent, attachment, or a flag appearing/disappearing) must keep its
        // exact tick even when the caller asked for every Nth tick, because those
        // are the hold reasons CA-02b keys on; dropping them would turn a hold
        // into a one-row-late lie.
        const bool structural = !sameFlags || !sameParent;
        if (!structural && stride > 1 && tick - last.tick < stride) {
          ++summary.entityTransformRowsStrideSkipped;
          continue;
        }
      }
    }
    if (summary.entityTransformSamples.size() >= maxSamples) {
      summary.entityTransformCaptureStopped = true;
      ++summary.entityTransformRowsDropped;
      break;
    }
    if (summary.entityTransformSamples.size() == summary.entityTransformSamples.capacity()) {
      const std::size_t remaining = maxSamples - summary.entityTransformSamples.size();
      summary.entityTransformSamples.reserve(summary.entityTransformSamples.size()
          + std::min(kEntityTransformReserveStep, remaining));
    }
    EntityTransformSample sample;
    sample.tick = tick;
    sample.entityIndex = static_cast<std::uint16_t>(slot);
    sample.flags = sampleFlags;
    sample.generation = generation;
    sample.classId = classId;
    std::memcpy(sample.origin, origin, sizeof(origin));
    std::memcpy(sample.angles, angles, sizeof(angles));
    sample.moveParent = moveParent;
    sample.parentAttachment = parentAttachment;
    summary.entityTransformSlotSample[slot] = static_cast<std::uint32_t>(summary.entityTransformSamples.size());
    summary.entityTransformSamples.push_back(sample);
    if (summary.entityTransformFirstTick < 0) summary.entityTransformFirstTick = tick;
    summary.entityTransformLastTick = tick;
  }
  summary.entityTransformSlotsWalked += walked;
  // The append's cost is a CA-02c reading (it runs on the decode path), so it is
  // behind a flag for the same reason T53's is: a run must not pay a clock read
  // per packet for a distribution nobody asked for.
  if (limits.measureAppendCost) {
    const auto finished = std::chrono::steady_clock::now();
    const double millis = std::chrono::duration<double, std::milli>(finished - started).count();
    summary.entityTransformAppendMillis.push_back(millis);
    summary.entityTransformAppendEntities.push_back(static_cast<std::uint32_t>(walked));
  }
}

// PVS residency. The bitmap, the departure/return counters and their reset rules are
// CA-01's (entity_dormant.cpp); this file must not keep a second copy of them. The
// wrappers exist for two CA-02a reasons only: the decoder already knows every
// transition, and a Delete must also invalidate the slot's retained-row cache so a
// reused slot is never compared against its predecessor's placement. They are cheap
// and stay unconditional, so a scan that arms CA-02a later still sees a correct
// dormancy bitmap.
void markEntityTransformResident(DemoNetworkSummary& summary, std::int32_t entityIndex) {
  markEntityDormantResident(summary, entityIndex);
}

void markEntityTransformDeparted(DemoNetworkSummary& summary, std::int32_t entityIndex) {
  markEntityDormantDeparted(summary, entityIndex);
}

// A Delete ends the lifetime in the slot, so the slot cache describes a lifetime that
// no longer exists. The generation bump alone would invalidate the classification, but
// the last-row pointer is cleared here so a reused slot can never be compared against
// its predecessor's placement. The dormancy bit itself is CA-01's to clear.
void clearEntityTransformSlot(DemoNetworkSummary& summary, std::int32_t entityIndex) {
  clearEntityDormantSlot(summary, entityIndex);
  if (entityIndex < 0 || static_cast<std::size_t>(entityIndex) >= kEntityTransformSlots) return;
  const std::size_t slot = static_cast<std::size_t>(entityIndex);
  if (slot < summary.entityTransformSlotSample.size()) summary.entityTransformSlotSample[slot] = kNoEntityTransformSample;
  if (slot < summary.entityTransformSlotFlags.size()) summary.entityTransformSlotFlags[slot] = 0;
  if (slot < summary.entityTransformSlotGeneration.size()) summary.entityTransformSlotGeneration[slot] = 0;
}

std::vector<EntityTransformSeries> buildEntityTransformSeries(
    const std::vector<EntityTransformSample>& samples) {
  struct Cursor {
    float origin[3] = {0.0f, 0.0f, 0.0f};
    bool hasOrigin = false;
  };
  std::vector<EntityTransformSeries> series;
  std::vector<Cursor> cursors;
  std::unordered_map<std::uint64_t, std::size_t> byLifetime;
  byLifetime.reserve(samples.size() / 4u + 16u);
  for (const EntityTransformSample& sample : samples) {
    const std::uint64_t key = (static_cast<std::uint64_t>(sample.generation) << 16)
        | static_cast<std::uint64_t>(sample.entityIndex);
    auto found = byLifetime.find(key);
    if (found == byLifetime.end()) {
      EntityTransformSeries fresh;
      fresh.entityIndex = sample.entityIndex;
      fresh.generation = sample.generation;
      fresh.classId = sample.classId;
      fresh.firstTick = sample.tick;
      fresh.lastTick = sample.tick;
      fresh.playerClass = (sample.flags & kEntityTransformPlayerClass) != 0;
      fresh.drawableByModel = (sample.flags & kEntityTransformDrawableByModel) != 0;
      series.push_back(fresh);
      cursors.push_back(Cursor{});
      found = byLifetime.emplace(key, series.size() - 1u).first;
    }
    const std::size_t at = found->second;
    EntityTransformSeries& target = series[at];
    Cursor& cursor = cursors[at];
    if (sample.tick < target.firstTick) target.firstTick = sample.tick;
    if (sample.tick > target.lastTick) target.lastTick = sample.tick;
    ++target.rows;
    if ((sample.flags & kEntityTransformDormant) != 0) ++target.dormantRows;
    if ((sample.flags & kEntityTransformHasMoveParent) != 0) ++target.parentRows;
    if ((sample.flags & kEntityTransformHasOrigin) != 0) {
      target.hasOrigin = true;
      if (cursor.hasOrigin && std::memcmp(cursor.origin, sample.origin, sizeof(sample.origin)) != 0) {
        ++target.movedRows;
      }
      std::memcpy(cursor.origin, sample.origin, sizeof(sample.origin));
      cursor.hasOrigin = true;
    }
    if ((sample.flags & kEntityTransformHasAngles) != 0) target.hasAngles = true;
  }
  return series;
}

std::size_t entityTransformRetainedBytes(const DemoNetworkSummary& summary) {
  std::size_t bytes = summary.entityTransformSamples.capacity() * sizeof(EntityTransformSample);
  bytes += summary.entityTransformSlotSample.capacity() * sizeof(std::uint32_t);
  bytes += summary.entityTransformSlotGeneration.capacity() * sizeof(std::uint32_t);
  bytes += summary.entityTransformSlotFlags.capacity() * sizeof(std::uint16_t);
  bytes += summary.entityTransformClassMask.capacity() * sizeof(std::uint16_t);
  bytes += summary.entityDormant.capacity() * sizeof(std::uint8_t);
  bytes += summary.entityTransformAppendMillis.capacity() * sizeof(double);
  bytes += summary.entityTransformAppendEntities.capacity() * sizeof(std::uint32_t);
  return bytes;
}

EntityTransformTrackStats summarizeEntityTransformTrack(const DemoNetworkSummary& summary) {
  EntityTransformTrackStats stats;
  stats.rows = summary.entityTransformSamples.size();
  stats.droppedRows = summary.entityTransformRowsDropped;
  stats.packetsScanned = summary.entityTransformPacketsScanned;
  stats.slotsWalked = summary.entityTransformSlotsWalked;
  stats.rowsUnchanged = summary.entityTransformRowsUnchanged;
  stats.rowsStrideSkipped = summary.entityTransformRowsStrideSkipped;
  stats.lifetimesNotDrawable = summary.entityTransformLifetimesNotDrawable;
  stats.firstTick = summary.entityTransformFirstTick;
  stats.lastTick = summary.entityTransformLastTick;
  stats.captureStopped = summary.entityTransformCaptureStopped;
  stats.retainedBytes = entityTransformRetainedBytes(summary);
  const std::vector<EntityTransformSeries> series = buildEntityTransformSeries(summary.entityTransformSamples);
  stats.lifetimes = series.size();
  std::unordered_set<std::uint16_t> entities;
  for (const EntityTransformSeries& lifetime : series) {
    entities.insert(lifetime.entityIndex);
    if (lifetime.movedRows > 0) ++stats.movingLifetimes;
    if (lifetime.playerClass) ++stats.playerLifetimes;
    if (lifetime.drawableByModel) ++stats.modelLifetimes;
    stats.dormantRows += lifetime.dormantRows;
  }
  stats.entities = entities.size();
  return stats;
}

std::vector<EntityTransformSample> queryEntityTransformRows(
    const std::vector<EntityTransformSample>& samples,
    std::uint16_t entityIndex, std::uint32_t generation) {
  std::vector<EntityTransformSample> rows;
  for (const EntityTransformSample& sample : samples) {
    if (sample.entityIndex == entityIndex && sample.generation == generation) rows.push_back(sample);
  }
  return rows;
}

}  // namespace tf2::native
