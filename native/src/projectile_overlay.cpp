#include "projectile_overlay.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace tf2::native {
namespace {

// 66.66 tick is TF2's rate but a demo carries its own, and a linger stated in
// milliseconds must follow the recording it is applied to rather than a constant.
// Rounded to the nearest tick and floored at zero: a negative linger would mean
// "draw the future".

} // namespace

std::int32_t trajectoryLingerTicks(int lingerMs, double tickRate) {
  if (lingerMs <= 0) return 0;
  if (!(tickRate > 0.0)) tickRate = kTrajectoryDefaultTickRate;
  const double ticks = static_cast<double>(lingerMs) * tickRate / 1000.0;
  if (ticks < 0.0) return 0;
  return static_cast<std::int32_t>(ticks + 0.5);
}

std::vector<ProjectileTrackPoint> buildProjectileTrajectoryPoints(
    const DemoNetworkSummary& summary,
    std::int32_t sceneTick,
    const TrajectoryBuildRequest& request,
    TrajectoryBuildStats* stats) {
  std::vector<ProjectileTrackPoint> points;
  TrajectoryBuildStats local;
  const std::int32_t linger = trajectoryLingerTicks(request.lingerMs, request.tickRate);
  local.lingerTicks = linger;

  // THE WINDOW. Everything from (sceneTick - linger) through sceneTick, exactly.
  // Not "the last N ticks that happen to be retained": a window anchored on the
  // scene tick is what makes two runs of the same capture produce the same
  // trail, and it is why the query is by tick range rather than by count.
  const std::int32_t firstTick = sceneTick - linger;
  const auto rows = queryProjectileTracksInRange(summary, firstTick, sceneTick);
  local.rowsAvailable = rows.size();

  const std::int32_t selected = request.selectedPlayerEntity;
  const bool allPlayers = selected < 0;

  // Per-lifetime ordering. A trail must be drawn per projectile, not per slot and
  // not interleaved: two projectiles alive on the same tick are two separate
  // trails, and joining them would draw a line between unrelated things.
  std::map<std::pair<std::uint16_t, std::uint32_t>, std::vector<ProjectileTrackSample>> byLifetime;
  std::set<std::uint16_t> owners;
  for (const auto& row : rows) {
    if (row.hasOwner) ++local.rowsWithOwner;
    else ++local.rowsWithoutOwner;
    if (!allPlayers) {
      // An unowned projectile is not the selected player's, so it is excluded --
      // and counted, so "nothing was drawn" cannot be confused with "everything
      // was filtered out for the wrong reason".
      if (!row.hasOwner) { ++local.rowsFilteredOut; continue; }
      if (row.ownerIndex != static_cast<std::uint16_t>(selected)) { ++local.rowsFilteredOut; continue; }
    }
    ++local.rowsAfterFilter;
    if (row.hasOwner) owners.insert(row.ownerIndex);
    byLifetime[std::make_pair(row.entityIndex, row.generation)].push_back(row);
  }
  local.ownersRepresented = owners.size();

  // The stride, applied to the DEMO TICK so it is a property of the recording
  // rather than of how many frames were drawn.
  const int stride = request.lowQuality ? 4 : 1;

  for (auto& entry : byLifetime) {
    auto& samples = entry.second;
    std::sort(samples.begin(), samples.end(),
              [](const ProjectileTrackSample& left, const ProjectileTrackSample& right) {
                return left.tick < right.tick;
              });
    // Two rows for one tick would be drawn as a zero-length segment; keep the
    // first, which is the earlier arrival.
    samples.erase(std::unique(samples.begin(), samples.end(),
                              [](const ProjectileTrackSample& left, const ProjectileTrackSample& right) {
                                return left.tick == right.tick;
                              }),
                  samples.end());
    if (samples.empty()) continue;
    ++local.lifetimes;
    for (const auto& sample : samples) {
      if (stride > 1 && (sample.tick % stride) != 0) continue;
      ++local.rowsAfterQuality;
      ProjectileTrackPoint point;
      point.x = sample.origin[0];
      point.y = sample.origin[1];
      point.z = sample.origin[2];
      // Age in ticks. A row from the current tick is fresh; a row from the start
      // of the linger window is about to disappear.
      point.ageTicks = static_cast<float>(sceneTick - sample.tick);
      point.entityIndex = sample.entityIndex;
      points.push_back(point);
    }
  }

  local.pointsEmitted = points.size();
  if (stats) *stats = local;
  return points;
}

} // namespace tf2::native
