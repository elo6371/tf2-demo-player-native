#include "entity_dormant.h"

#include "entity_model.h"

#include <cstring>

namespace tf2::native {

namespace {

constexpr std::size_t kEntityDormantSlots = 2048u;

}  // namespace

void markEntityDormantResident(DemoNetworkSummary& summary, std::int32_t entityIndex) {
  // CA-01: the write path is opt-in. Disabled scans never allocate the bitmap or
  // touch counters/slots, which is what keeps them identical to 054fb628 even if
  // a caller skips the gate at the call site.
  if (!summary.entityDormantEnabled) return;
  if (entityIndex < 0 || static_cast<std::size_t>(entityIndex) >= kEntityDormantSlots) return;
  if (summary.entityDormant.size() < kEntityDormantSlots) {
    summary.entityDormant.assign(kEntityDormantSlots, 0u);
  }
  const std::size_t slot = static_cast<std::size_t>(entityIndex);
  if (summary.entityDormant[slot] != 0u) {
    summary.entityDormant[slot] = 0u;
    ++summary.entityDormantReturns;
  }
}

void markEntityDormantDeparted(DemoNetworkSummary& summary, std::int32_t entityIndex) {
  if (!summary.entityDormantEnabled) return;
  if (entityIndex < 0 || static_cast<std::size_t>(entityIndex) >= kEntityDormantSlots) return;
  if (summary.entityDormant.size() < kEntityDormantSlots) {
    summary.entityDormant.assign(kEntityDormantSlots, 0u);
  }
  const std::size_t slot = static_cast<std::size_t>(entityIndex);
  if (summary.entityDormant[slot] == 0u) {
    summary.entityDormant[slot] = 1u;
    ++summary.entityDormantDepartures;
  }
}

void clearEntityDormantSlot(DemoNetworkSummary& summary, std::int32_t entityIndex) {
  if (!summary.entityDormantEnabled) return;
  if (entityIndex < 0 || static_cast<std::size_t>(entityIndex) >= kEntityDormantSlots) return;
  const std::size_t slot = static_cast<std::size_t>(entityIndex);
  if (slot < summary.entityDormant.size()) {
    summary.entityDormant[slot] = 0u;
  }
}

void setEntityDormantEnabled(DemoNetworkSummary& summary, bool enabled) {
  summary.entityDormantEnabled = enabled;
}

EntityDormantStats summarizeEntityDormant(const DemoNetworkSummary& summary) {
  EntityDormantStats stats;
  stats.packetLeaves = summary.packetEntityLeaveCount;
  stats.departures = summary.entityDormantDepartures;
  stats.returns = summary.entityDormantReturns;
  for (const std::uint8_t bit : summary.entityDormant) {
    if (bit != 0u) ++stats.activeDormant;
  }
  stats.wiringSuspect = stats.packetLeaves > 0u && stats.departures == 0u;
  return stats;
}

bool entityDormantShouldDraw(const std::vector<std::uint8_t>& dormantByIndex,
                             std::size_t entityIndex) {
  if (entityIndex >= dormantByIndex.size()) return true;
  return dormantByIndex[entityIndex] == 0u;
}

EntityDormantSnapshot captureEntityDormantSnapshot(const DemoNetworkSummary& summary,
                                                   std::int32_t tick) {
  EntityDormantSnapshot snapshot;
  snapshot.tick = tick;
  if (summary.entityDormant.size() >= kEntityDormantSlots) {
    snapshot.dormantByIndex = summary.entityDormant;
  }
  return snapshot;
}

bool entityDormantAtTick(const std::vector<EntityDormantSnapshot>& snapshots,
                         std::size_t entityIndex, std::int32_t tick) {
  if (snapshots.empty()) return false;
  std::size_t low = 0u;
  std::size_t high = snapshots.size();
  while (low < high) {
    const std::size_t middle = low + (high - low) / 2u;
    if (snapshots[middle].tick <= tick) {
      low = middle + 1u;
    } else {
      high = middle;
    }
  }
  if (low == 0u) return false;
  const EntityDormantSnapshot& snapshot = snapshots[low - 1u];
  if (entityIndex >= snapshot.dormantByIndex.size()) return false;
  return snapshot.dormantByIndex[entityIndex] != 0u;
}

EntityDormantPlacement resolveEntityDormantStatePlacement(
    const std::vector<EntityState>& states, std::size_t entityIndex) {
  EntityDormantPlacement placement;
  if (entityIndex >= states.size() || entityIndex > 0xffffu) return placement;
  const EntityState& state = states[entityIndex];
  if (state.classId < 0) return placement;
  placement.entityIndex = static_cast<std::uint16_t>(entityIndex);
  const ModelInstanceTransform transform = EntityModelResolver::extractTransform(state);
  placement.hasOrigin = transform.hasOrigin;
  placement.hasAngles = transform.hasAngles;
  if (placement.hasOrigin) std::memcpy(placement.origin, transform.origin, sizeof(placement.origin));
  if (placement.hasAngles) std::memcpy(placement.angles, transform.angles, sizeof(placement.angles));
  placement.valid = placement.hasOrigin || placement.hasAngles;
  return placement;
}

}  // namespace tf2::native
