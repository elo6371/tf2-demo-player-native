#include "demo_header.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <queue>
#include <chrono>
#include <unordered_map>
#include <unordered_set>

#include "demo_header_internal.h"

namespace tf2::native {

bool readEntityPropUpdates(MessageBits& bits, const SendTableSchema* table, EntityState& state, DemoNetworkSummary* summary,
    std::int32_t packetTick, std::int32_t entityIndex, const char* stage,
    std::vector<EntityPropChange>* changes) {
  const bool isTempStage = summary != nullptr && std::strcmp(stage, "temp") == 0;
  auto recordTempFailure = [&](const char* name, const SendPropSchema* prop) {
    if (!isTempStage || summary->firstTempEntityFailureTick >= 0) return;
    summary->firstTempEntityFailureTick = packetTick;
    summary->firstTempEntityFailureClass = state.classId;
    summary->firstTempEntityFailureStage = stage;
    summary->firstTempEntityFailureName = name;
    if (prop) {
      summary->firstTempEntityFailureType = static_cast<std::int32_t>(prop->type);
      summary->firstTempEntityFailureFlags = prop->flags;
      summary->firstTempEntityFailureBits = prop->bitCount;
    }
  };
  std::int32_t lastProp = -1;
  if (!table) {
    if (summary) {
      ++summary->entityPropMissingTableFailures;
      recordTempFailure("<missing-table>", nullptr);
      if (summary->firstEntityPropFailureTick < 0) { summary->firstEntityPropFailureClass = state.classId; summary->firstEntityPropFailureTick = packetTick; summary->firstEntityPropFailureEntity = entityIndex; summary->firstEntityPropFailureStage = stage; }
    }
    return false;
  }
  while (bits.remaining() > 0) {
    std::uint32_t hasProp = 0;
    if (!bits.read(1, hasProp)) return false;
    if (!hasProp) return true;
    std::uint32_t diff = 0;
    if (!bits.readBitVar(diff)) return false;
    lastProp += static_cast<std::int32_t>(diff) + 1;
    if (lastProp < 0 || static_cast<std::size_t>(lastProp) >= table->flattenedProps.size()) {
      if (summary) {
        ++summary->entityPropIndexFailures;
        recordTempFailure("index", nullptr);
        if (summary->firstEntityPropFailureTick < 0) {
          summary->firstEntityPropFailureClass = state.classId; summary->firstEntityPropFailureIndex = lastProp; summary->firstEntityPropFailureTick = packetTick; summary->firstEntityPropFailureEntity = entityIndex; summary->firstEntityPropFailureStage = stage; summary->firstEntityPropFailureName = "index";
        }
      }
      return false;
    }
    const auto& prop = table->flattenedProps[static_cast<std::size_t>(lastProp)];
    EntityPropertyValue value;
    if (!readSendPropValue(bits, prop, value)) {
      if (summary) {
        ++summary->entityPropValueFailures;
        recordTempFailure(prop.name.c_str(), &prop);
        if (summary->firstEntityPropFailureTick < 0) {
          summary->firstEntityPropFailureClass = state.classId; summary->firstEntityPropFailureIndex = lastProp; summary->firstEntityPropFailureTick = packetTick; summary->firstEntityPropFailureEntity = entityIndex; summary->firstEntityPropFailureStage = stage; summary->firstEntityPropFailureName = prop.name; summary->firstEntityPropFailureType = static_cast<std::int32_t>(prop.type); summary->firstEntityPropFailureFlags = prop.flags; summary->firstEntityPropFailureBits = prop.bitCount;
        }
      }
      return false;
    }
    const std::string key = prop.ownerTable.empty() ? prop.name : prop.ownerTable + "." + prop.name;
    // Stamp before the copy below, so the history record carries the same tick as
    // the state: a replayed value must not look fresher than it is. One stamp here
    // covers all four call sites (preserve / baseline / enter / temp) because they
    // all pass their packet tick into this function.
    value.lastWriteTick = packetTick;
    if (changes) {
      EntityPropChange change;
      change.propIndex = static_cast<std::uint32_t>(lastProp);
      change.value = value;  // copy for the history record; the state takes the original
      changes->push_back(std::move(change));
    }
    state.properties[key] = std::move(value);
  }
  return false;
}
namespace {

// One gap between two retained checkpoints, ordered so that a max-heap yields
// the widest tick gap first. Ties break on the left index, so the retained set is
// a pure function of the input and two runs cannot disagree.
struct HistoryGap {
  std::int32_t width = 0;
  std::size_t left = 0;
  std::size_t right = 0;
};
struct HistoryGapWider {
  bool operator()(const HistoryGap& left, const HistoryGap& right) const {
    if (left.width != right.width) return left.width < right.width;
    return left.left > right.left;
  }
};

void thinHistoryArchive(std::vector<EntityHistoryCheckpoint>& archive, std::size_t maxCount) {
  if (archive.size() <= maxCount || maxCount < 2) return;
  // Keep the two endpoints and spend every remaining slot splitting whichever gap
  // is currently the widest *in ticks*. That is the quantity a caller feels: a
  // query for a tick outside the live window is answered with the newest retained
  // checkpoint at or before it, so the worst answer quality over the whole demo is
  // the largest tick gap between two neighbours that survived.
  //
  // The previous rule divided the *index* range evenly
  // (`index = (last * slot) / (maxCount - 1)`). It only ever runs in the state
  // where maxCount is just above the archive size, because it runs on every flush;
  // there floor() maps the earliest slots onto their own index, so the head of the
  // archive is pinned verbatim while everything after it is decimated again and
  // again. Measured on bagel: 19 checkpoints frozen inside 56148..57829 (1681
  // ticks), then a 54392-tick hole up to 112221, then the recent tail. The hole is
  // not caused by uneven entity activity -- it reproduces on a supply that emits
  // one checkpoint every 128 ticks with no variation at all.
  const std::size_t last = archive.size() - 1;
  std::vector<char> kept(archive.size(), 0);
  kept[0] = 1;
  kept[last] = 1;
  std::size_t used = 2;
  std::priority_queue<HistoryGap, std::vector<HistoryGap>, HistoryGapWider> gaps;
  gaps.push(HistoryGap{archive[last].tick - archive[0].tick, 0, last});
  while (used < maxCount && !gaps.empty()) {
    const HistoryGap gap = gaps.top();
    gaps.pop();
    if (gap.right <= gap.left + 1) continue;  // no candidate left inside it
    // Nearest candidate to the tick midpoint. `archive` is tick-ordered, so this
    // is a binary search rather than a scan.
    const std::int64_t midpoint =
        (static_cast<std::int64_t>(archive[gap.left].tick) + archive[gap.right].tick) / 2;
    const auto lower = std::lower_bound(
        archive.begin() + static_cast<std::ptrdiff_t>(gap.left + 1),
        archive.begin() + static_cast<std::ptrdiff_t>(gap.right), midpoint,
        [](const EntityHistoryCheckpoint& item, std::int64_t value) { return item.tick < value; });
    std::size_t pick = static_cast<std::size_t>(lower - archive.begin());
    if (pick > gap.left + 1) {
      const std::size_t before = pick - 1;
      const std::int64_t distanceBefore = midpoint - archive[before].tick;
      const std::int64_t distanceAt = pick < gap.right
          ? archive[pick].tick - midpoint : std::numeric_limits<std::int64_t>::max();
      if (distanceBefore <= distanceAt) pick = before;
    }
    if (pick <= gap.left) pick = gap.left + 1;
    if (pick >= gap.right) pick = gap.right - 1;
    kept[pick] = 1;
    ++used;
    gaps.push(HistoryGap{archive[pick].tick - archive[gap.left].tick, gap.left, pick});
    gaps.push(HistoryGap{archive[gap.right].tick - archive[pick].tick, pick, gap.right});
  }
  std::vector<EntityHistoryCheckpoint> reduced;
  reduced.reserve(used);
  for (std::size_t index = 0; index < archive.size(); ++index) {
    if (kept[index]) reduced.push_back(std::move(archive[index]));
  }
  archive = std::move(reduced);
}

void appendEntityHistory(DemoNetworkSummary& summary, std::int32_t tick, bool isDelta,
                         std::int32_t deltaFrom, std::vector<EntityHistoryEvent>&& events) {
  const std::size_t maxEvents = std::max<std::size_t>(1, summary.entityHistoryLimits.maxEvents);
  const std::size_t maxCheckpoints = std::max<std::size_t>(1, summary.entityHistoryLimits.maxCheckpoints);
  const std::size_t stride = std::max<std::size_t>(1, summary.entityHistoryLimits.checkpointStride);
  const std::size_t archiveMax = std::max<std::size_t>(2, summary.entityHistoryLimits.archiveMax);
  if (summary.entityHistoryHasGap) {
    if (isDelta) {
      ++summary.entityHistoryDroppedPackets;
      return;
    }
    summary.entityHistoryHasGap = false;
  }
  std::uint32_t packetOrdinal = static_cast<std::uint32_t>(summary.entityHistoryPackets.size());
  if (summary.entityHistoryEvents.size() + events.size() > maxEvents ||
      (packetOrdinal > 0 && packetOrdinal % stride == 0 &&
       summary.entityHistoryCheckpoints.size() >= maxCheckpoints)) {
    // The live entity table already contains the state after this packet. Keep
    // the checkpoints we are about to drop so a tick anywhere in the demo can
    // resolve to the newest retained snapshot. The open window still replays
    // its own events exactly. Ticks between archived snapshots are Checkpoint,
    // not a second full event log.
    ++summary.entityHistoryDroppedPackets;
    ++summary.entityHistoryFlushes;
    summary.entityHistoryArchive.insert(summary.entityHistoryArchive.end(),
                                        summary.entityHistoryCheckpoints.begin(),
                                        summary.entityHistoryCheckpoints.end());
    thinHistoryArchive(summary.entityHistoryArchive, archiveMax);
    summary.entityHistoryEvents.clear();
    summary.entityHistoryPackets.clear();
    summary.entityHistoryCheckpoints.clear();
    packetOrdinal = 0;
    // The checkpoint for this packet is pushed by the rule below, which fires on
    // packetOrdinal == 0. Pushing one here as well put two checkpoints on the same
    // tick (bagel: every duplicate pair in the archive tick list, e.g. 56489
    // 56489 and 124508 124508), spending half the archive budget on a copy of a
    // tick that was already represented.
  }
  const std::size_t firstEvent = summary.entityHistoryEvents.size();
  for (auto& event : events) event.packetOrdinal = packetOrdinal;
  if (isDelta) {
    const auto base = std::lower_bound(
        summary.entityHistoryPackets.begin(), summary.entityHistoryPackets.end(),
        deltaFrom,
        [](const EntityHistoryPacket& packet, std::int32_t tick) {
          return packet.tick < tick;
        });
    if (base == summary.entityHistoryPackets.end() || base->tick != deltaFrom) {
      ++summary.entityHistoryDeltaBaseMisses;
    }
  }
  summary.entityHistoryEvents.insert(summary.entityHistoryEvents.end(),
                                     std::make_move_iterator(events.begin()),
                                     std::make_move_iterator(events.end()));
  summary.entityHistoryPackets.push_back({tick, deltaFrom, packetOrdinal, firstEvent,
                                           events.size(), isDelta});
  if (summary.entityHistoryCheckpoints.empty() ||
      packetOrdinal % stride == 0) {
    summary.entityHistoryCheckpoints.push_back({tick, packetOrdinal,
                                                summary.entityClassByIndex,
                                                summary.entityStates});
  }
}
} // namespace

bool readPacketEntities(MessageBits& bits, DemoNetworkSummary& summary, std::int32_t packetTick) {
  std::uint32_t maxEntries = 0, isDelta = 0, deltaFrom = 0, baseline = 0, updatedEntries = 0, payloadBits = 0, updateBaseline = 0;
  if (!bits.read(11, maxEntries) || !bits.read(1, isDelta)) return false;
  if (isDelta && !bits.read(32, deltaFrom)) return false;
  if (!bits.read(1, baseline) || !bits.read(11, updatedEntries) || !bits.read(20, payloadBits) || !bits.read(1, updateBaseline)) return false;
  if (updatedEntries > maxEntries || maxEntries > 2048u) return false;
  if (summary.firstPacketEntitiesTick < 0) { summary.firstPacketEntitiesTick = packetTick; summary.firstPacketEntitiesMaxEntries = static_cast<std::int32_t>(maxEntries); summary.firstPacketEntitiesUpdatedEntries = static_cast<std::int32_t>(updatedEntries); summary.firstPacketEntitiesPayloadBits = static_cast<std::int32_t>(payloadBits); summary.firstPacketEntitiesDelta = static_cast<std::int32_t>(isDelta); }
  if (payloadBits > bits.remaining()) return false;
  const std::size_t payloadEnd = bits.remaining() - payloadBits;
  bits.setMinimumRemaining(payloadEnd);
  const bool deltaBaseUnavailable = isDelta != 0 &&
      (summary.entityHistoryHasGap ||
       summary.entityHistoryPackets.empty() ||
       [&summary, deltaFrom] {
         const auto base = std::lower_bound(
             summary.entityHistoryPackets.begin(), summary.entityHistoryPackets.end(),
             static_cast<std::int32_t>(deltaFrom),
             [](const EntityHistoryPacket& packet, std::int32_t tick) {
               return packet.tick < tick;
             });
         return base == summary.entityHistoryPackets.end()
             || base->tick != static_cast<std::int32_t>(deltaFrom);
       }());
  if (isDelta) {
    ++summary.packetEntityDeltaCount;
    if (summary.firstPacketEntitiesDeltaTick < 0) {
      summary.firstPacketEntitiesDeltaTick = packetTick;
      summary.firstPacketEntitiesDeltaFrom = static_cast<std::int32_t>(deltaFrom);
    }
    if (deltaBaseUnavailable) {
      ++summary.packetEntityDeltaBaseUnavailableCount;
      if (summary.firstPacketEntitiesUnavailableTick < 0) {
        summary.firstPacketEntitiesUnavailableTick = packetTick;
        summary.firstPacketEntitiesUnavailableFrom = static_cast<std::int32_t>(deltaFrom);
      }
    }
  }
  std::int32_t lastEntity = -1;
  bool entityUpdatesComplete = true;
  std::vector<EntityHistoryEvent> historyEvents;
  for (std::uint32_t i = 0; i < updatedEntries; ++i) {
    const std::size_t updateBitOffset = bits.offsetBits();
    std::uint32_t diff = 0, updateType = 0;
    if (!bits.readBitVar(diff) || !bits.read(2, updateType)) { ++summary.packetEntityDecodeFailures; ++summary.entityUpdateHeaderFailures; entityUpdatesComplete = false; break; }
    lastEntity += static_cast<std::int32_t>(diff) + 1;
    ++summary.packetEntityHeaderUpdates;
    if (updateType == 0) {
      if (summary.firstPacketEntitiesPreserveTick < 0) {
        summary.firstPacketEntitiesPreserveTick = packetTick;
        summary.firstPacketEntitiesFirstUpdateBit = static_cast<std::int64_t>(updateBitOffset);
        summary.firstPacketEntitiesFirstDiff = static_cast<std::int32_t>(diff);
      }
      ++summary.packetEntityPreserveCount;
      if (lastEntity < 0 || lastEntity >= 2048 || static_cast<std::size_t>(lastEntity) >= summary.entityClassByIndex.size() || summary.entityClassByIndex[static_cast<std::size_t>(lastEntity)] < 0) { ++summary.entityUnknownStateFailures; if (summary.firstEntityUnknownStateTick < 0) { summary.firstEntityUnknownStateTick = packetTick; summary.firstEntityUnknownStateEntity = lastEntity; summary.firstEntityUnknownStateUpdate = static_cast<std::int32_t>(updateType); summary.firstEntityUnknownStateMaxEntries = static_cast<std::int32_t>(maxEntries); summary.firstEntityUnknownStateUpdatedEntries = static_cast<std::int32_t>(updatedEntries); summary.firstEntityUnknownStatePayloadBits = static_cast<std::int32_t>(payloadBits); summary.firstEntityUnknownStateDiff = static_cast<std::int32_t>(diff); } entityUpdatesComplete = false; break; }
      const auto classId = static_cast<std::uint32_t>(summary.entityClassByIndex[static_cast<std::size_t>(lastEntity)]);
      const auto* table = tableForClass(summary, classId);
      // std::swap instead of copy-and-move-back. EntityState owns an
      // unordered_map with hundreds of entries, and this runs once per updated
      // entity -- 1.4M times for koth_bagel_rc13. The previous form made two
      // full copies of that map per update.
      auto& liveState = summary.entityStates[static_cast<std::size_t>(lastEntity)];
      EntityState candidate;
      std::swap(candidate, liveState);
      std::vector<EntityPropChange> changes;
      if (!readEntityPropUpdates(bits, table, candidate, &summary, packetTick, lastEntity, "preserve", &changes)) {
        std::swap(candidate, liveState);  // failure: restore the original state
        ++summary.packetEntityDecodeFailures; entityUpdatesComplete = false; break;
      }
      std::swap(candidate, liveState);
      historyEvents.push_back({packetTick, 0, static_cast<std::uint16_t>(lastEntity),
                               summary.entityClassByIndex[static_cast<std::size_t>(lastEntity)], false,
                               false, {}, std::move(changes)});
    } else if (updateType == 1) {
      ++summary.packetEntityLeaveCount;
      // Leave means the entity left the PVS, not that it was destroyed. Keep
      // its class and last state so a later Preserve/Enter can resolve it.
    } else if (updateType == 3) {
      ++summary.packetEntityDeleteCount;
      if (lastEntity >= 0 && static_cast<std::size_t>(lastEntity) < summary.entityClassByIndex.size() && summary.entityClassByIndex[static_cast<std::size_t>(lastEntity)] >= 0) { summary.entityClassByIndex[static_cast<std::size_t>(lastEntity)] = -1; if (static_cast<std::size_t>(lastEntity) < summary.entityStates.size()) summary.entityStates[static_cast<std::size_t>(lastEntity)] = {}; if (summary.activeEntityCount > 0) --summary.activeEntityCount; }
      // T53: the lifetime in this slot has ended. Advancing here as well as on
      // Enter means a slot that is deleted and later re-enters is separated even
      // if the server reuses the index without a fresh Enter being observable.
      if (lastEntity >= 0 && lastEntity < 2048) {
        if (summary.projectileTrackSlotGeneration.size() < 2048u) {
          summary.projectileTrackSlotGeneration.resize(2048u, 0u);
        }
        ++summary.projectileTrackSlotGeneration[static_cast<std::size_t>(lastEntity)];
      }
      if (lastEntity >= 0 && lastEntity < 2048) historyEvents.push_back({packetTick, 0, static_cast<std::uint16_t>(lastEntity), -1, true, false, {}, {}});
    } else if (updateType == 2) {
      if (summary.firstPacketEntitiesEnterTick < 0) {
        summary.firstPacketEntitiesEnterTick = packetTick;
        summary.firstPacketEntitiesFirstUpdateBit = static_cast<std::int64_t>(updateBitOffset);
        summary.firstPacketEntitiesFirstDiff = static_cast<std::int32_t>(diff);
      }
      ++summary.packetEntityEnterCount;
      const std::uint32_t serverClassCount = effectiveServerClassCount(summary);
      std::uint32_t classBits = 1; while (classBits < 31u && (1u << classBits) <= std::max<std::uint32_t>(1u, serverClassCount)) ++classBits;
      std::uint32_t classId = 0, serial = 0;
      if (!bits.read(classBits, classId) || !bits.read(10, serial)) { ++summary.packetEntityDecodeFailures; entityUpdatesComplete = false; break; }
      if (lastEntity < 0 || lastEntity >= 2048 || classId >= summary.serverClassSchemas.size()) { ++summary.packetEntityDecodeFailures; entityUpdatesComplete = false; break; }
      if (summary.entityClassByIndex.size() < 2048u) summary.entityClassByIndex.resize(2048u, -1);
      if (summary.entityStates.size() < 2048u) summary.entityStates.resize(2048u);
      EntityState candidate;
      candidate.classId = static_cast<std::int32_t>(classId);
      const auto* table = tableForClass(summary, classId);
      const auto baselineEntry = summary.instanceBaselines.find(static_cast<std::uint16_t>(classId));
      if (summary.firstInstanceBaselineClassId < 0) {
        summary.firstInstanceBaselineClassId = static_cast<std::int32_t>(classId);
      }
      if (summary.firstInstanceBaselineLookupClassId < 0) {
        summary.firstInstanceBaselineLookupClassId = static_cast<std::int32_t>(classId);
        summary.firstInstanceBaselineLookupHasTable = table != nullptr;
        summary.firstInstanceBaselineLookupHasEntry = baselineEntry != summary.instanceBaselines.end();
      }
      if (table && baselineEntry != summary.instanceBaselines.end()) {
        MessageBits baselineBits(baselineEntry->second);
        if (readEntityPropUpdates(baselineBits, table, candidate, &summary, packetTick, lastEntity, "baseline")) {
          ++summary.instanceBaselineAppliedCount;
          if (summary.firstInstanceBaselineLookupClassId == static_cast<std::int32_t>(classId)) summary.firstInstanceBaselineLookupApplied = true;
        } else ++summary.instanceBaselineApplyFailures;
      } else {
        ++summary.instanceBaselineLookupMisses;
      }
      if (!readEntityPropUpdates(bits, table, candidate, &summary, packetTick, lastEntity, "enter")) { ++summary.packetEntityDecodeFailures; entityUpdatesComplete = false; break; }
      auto& classSlot = summary.entityClassByIndex[static_cast<std::size_t>(lastEntity)];
      if (classSlot < 0) ++summary.activeEntityCount;
      classSlot = static_cast<std::int32_t>(classId);
      // T53: an Enter begins a new entity lifetime in this slot, so the slot's
      // generation advances. Without this the retention would glue every
      // projectile that ever occupied the slot into one series. See the note on
      // ProjectileTrackSample::generation.
      if (static_cast<std::size_t>(lastEntity) < 2048) {
        if (summary.projectileTrackSlotGeneration.size() < 2048u) {
          summary.projectileTrackSlotGeneration.resize(2048u, 0u);
        }
        ++summary.projectileTrackSlotGeneration[static_cast<std::size_t>(lastEntity)];
        ++summary.projectileTrackSeriesStarted;
      }
      summary.maxActiveEntityCount = std::max(summary.maxActiveEntityCount, summary.activeEntityCount);
      summary.entityStates[static_cast<std::size_t>(lastEntity)] = std::move(candidate);
      historyEvents.push_back({packetTick, 0, static_cast<std::uint16_t>(lastEntity),
                               static_cast<std::int32_t>(classId), false, true,
                               summary.entityStates[static_cast<std::size_t>(lastEntity)], {}});
    } else { ++summary.packetEntityDecodeFailures; ++summary.entityUpdateHeaderFailures; entityUpdatesComplete = false; break; }
    if (bits.remaining() <= payloadEnd) break;
  }
  if (entityUpdatesComplete && isDelta) {
    while (bits.remaining() > payloadEnd) {
      std::uint32_t hasRemoved = 0;
      if (!bits.read(1, hasRemoved)) { ++summary.packetEntityDecodeFailures; entityUpdatesComplete = false; break; }
      if (!hasRemoved) break;
      std::uint32_t removedEntity = 0;
      if (!bits.read(11, removedEntity)) { ++summary.packetEntityDecodeFailures; entityUpdatesComplete = false; break; }
      if (removedEntity < summary.entityClassByIndex.size() && summary.entityClassByIndex[removedEntity] >= 0) {
        summary.entityClassByIndex[removedEntity] = -1;
        if (removedEntity < summary.entityStates.size()) summary.entityStates[removedEntity] = {};
        if (summary.activeEntityCount > 0) --summary.activeEntityCount;
        if (removedEntity < 2048) historyEvents.push_back({packetTick, 0, static_cast<std::uint16_t>(removedEntity), -1, true, false, {}, {}});
      }
    }
  }
  if (bits.remaining() > payloadEnd) { ++summary.packetEntityDecodeFailures; if (!bits.skip(bits.remaining() - payloadEnd)) return false; }
  bits.setMinimumRemaining(0);
  ++summary.packetEntitiesCount;
  summary.packetEntityUpdates += updatedEntries;
  summary.packetEntityPayloadBits += payloadBits;
  if (bits.remaining() < payloadEnd) return false;
  if (bits.remaining() > payloadEnd) return false;
  if (maxEntries > 2048) return false;
  if (!entityUpdatesComplete) {
    summary.entityHistoryEvents.clear();
    summary.entityHistoryPackets.clear();
    summary.entityHistoryCheckpoints.clear();
    summary.entityHistoryHasGap = true;
    summary.entityHistoryGapTick = packetTick;
    // Source keeps the live entity directory across packet decode failures and
    // delta gaps. Clearing it turns the next Preserve updates into a cascade
    // of false unknown-state failures; history remains unavailable until a
    // complete packet re-establishes a checkpoint.
    return true;
  }
  appendEntityHistory(summary, packetTick, isDelta != 0, static_cast<std::int32_t>(deltaFrom), std::move(historyEvents));
  // T53: additive per-tick projectile retention. Placed after the existing
  // history append and after the incomplete-packet early return above, so it runs
  // on exactly the packets that decoded completely -- the same condition
  // entityHistory uses. It touches no existing field.
  appendProjectileTrackSamples(summary, packetTick);
  return true;
}

EntitySnapshotQueryStatus queryEntitySnapshotAtOrBeforeTick(
    const DemoNetworkSummary& summary, std::int32_t tick, std::vector<EntityState>& states,
    std::int32_t* resolvedTick) {
  if (summary.entityHistoryHasGap) return EntitySnapshotQueryStatus::Gap;
  const bool liveWindow = !summary.entityHistoryCheckpoints.empty()
      && tick >= summary.entityHistoryCheckpoints.front().tick;
  if (!liveWindow) {
    if (summary.entityHistoryArchive.empty() && summary.entityHistoryCheckpoints.empty()) {
      return EntitySnapshotQueryStatus::NoHistory;
    }
    if (!summary.entityHistoryArchive.empty()) {
      const auto archived = std::upper_bound(
          summary.entityHistoryArchive.begin(), summary.entityHistoryArchive.end(), tick,
          [](std::int32_t value, const EntityHistoryCheckpoint& item) { return value < item.tick; });
      if (archived != summary.entityHistoryArchive.begin()) {
        const auto& fallback = *std::prev(archived);
        // A Checkpoint answer is stale by construction: it is the newest archived
        // snapshot at or before the query tick, not the query tick itself. The
        // caller gets the tick it actually came from so the staleness is a reading
        // rather than an inference from the archive's tick list.
        if (resolvedTick) *resolvedTick = fallback.tick;
        states = fallback.states;
        return EntitySnapshotQueryStatus::Checkpoint;
      }
    }
    return EntitySnapshotQueryStatus::TickBeforeHistory;
  }
  const auto checkpoint = std::upper_bound(
      summary.entityHistoryCheckpoints.begin(), summary.entityHistoryCheckpoints.end(), tick,
      [](std::int32_t value, const EntityHistoryCheckpoint& item) { return value < item.tick; });
  if (checkpoint == summary.entityHistoryCheckpoints.begin()) return EntitySnapshotQueryStatus::TickBeforeHistory;
  const auto& base = *std::prev(checkpoint);
  if (resolvedTick) *resolvedTick = tick;  // replayed through `tick` exactly
  states = base.states;
  for (const auto& packet : summary.entityHistoryPackets) {
    if (packet.packetOrdinal <= base.packetOrdinal || packet.tick > tick) continue;
    // History events contain the complete post-update state for every changed
    // entity. The live decoder applies deltas in stream order, so replaying a
    // retained window does not require retaining the packet named by
    // deltaFrom (that reference may intentionally point outside the bounded
    // history window).
    const std::size_t end = packet.firstEvent + packet.eventCount;
    if (end > summary.entityHistoryEvents.size()) return EntitySnapshotQueryStatus::Gap;
    for (std::size_t i = packet.firstEvent; i < end; ++i) {
      const auto& event = summary.entityHistoryEvents[i];
      if (event.entityIndex >= states.size()) states.resize(2048u);
      auto& target = states[event.entityIndex];
      if (event.removed) { target = EntityState{}; continue; }
      if (event.fullState) { target = event.state; continue; }
      // Preserve events carry only the properties the packet wrote, so they are
      // applied on top of the state already replayed from the checkpoint.
      const auto* table = event.classId >= 0
          ? tableForClass(summary, static_cast<std::uint32_t>(event.classId)) : nullptr;
      for (const auto& change : event.changes) {
        if (!table || static_cast<std::size_t>(change.propIndex) >= table->flattenedProps.size()) {
          return EntitySnapshotQueryStatus::Gap;
        }
        const auto& prop = table->flattenedProps[change.propIndex];
        target.properties[prop.ownerTable.empty() ? prop.name : prop.ownerTable + "." + prop.name] = change.value;
      }
    }
  }
  return EntitySnapshotQueryStatus::Available;
}

bool findEntitySnapshotAtOrBeforeTick(const DemoNetworkSummary& summary, std::int32_t tick,
                                      std::vector<EntityState>& states) {
  const auto status = queryEntitySnapshotAtOrBeforeTick(summary, tick, states);
  return status == EntitySnapshotQueryStatus::Available || status == EntitySnapshotQueryStatus::Checkpoint;
}

void appendEntityHistoryPacket(DemoNetworkSummary& summary, std::int32_t tick, bool isDelta,
                               std::int32_t deltaFrom, std::vector<EntityHistoryEvent> events) {
  appendEntityHistory(summary, tick, isDelta, deltaFrom, std::move(events));
}

} // namespace tf2::native
