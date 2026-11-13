#include "effects_timeline.h"
#include "demo_header.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace tf2::native {

void TempEffectTimeline::clear() { events_.clear(); }

bool TempEffectTimeline::add(TempEffectEvent event) {
  if (event.tick < 0 || !std::isfinite(event.fireDelaySeconds) || event.fireDelaySeconds < 0.0f || event.fireDelaySeconds > 60.0f) return false;
  if (event.hasPosition && (!std::isfinite(event.origin.x) || !std::isfinite(event.origin.y) || !std::isfinite(event.origin.z))) return false;
  if (event.hasDirection && (!std::isfinite(event.direction.x) || !std::isfinite(event.direction.y) || !std::isfinite(event.direction.z))) return false;
  if (event.kind == TempEffectKind::FireBullets && event.shots == 0) return false;
  events_.push_back(event);
  return true;
}

void TempEffectTimeline::sortByTick() {
  std::stable_sort(events_.begin(), events_.end(), [](const auto& a, const auto& b) {
    if (a.tick != b.tick) return a.tick < b.tick;
    return a.fireDelaySeconds < b.fireDelaySeconds;
  });
}

std::vector<TempEffectEvent> TempEffectTimeline::range(std::int32_t firstTick, std::int32_t lastTick) const {
  if (firstTick > lastTick) std::swap(firstTick, lastTick);
  std::vector<TempEffectEvent> result;
  for (const auto& event : events_) if (event.tick >= firstTick && event.tick <= lastTick) result.push_back(event);
  return result;
}

std::size_t TempEffectTimeline::countThrough(std::int32_t tick) const {
  const auto end = std::upper_bound(events_.begin(), events_.end(), tick,
    [](std::int32_t value, const TempEffectEvent& event) { return value < event.tick; });
  return static_cast<std::size_t>(end - events_.begin());
}

bool addFireBulletsEvent(const TempEntityEvent& source, TempEffectTimeline& timeline) {
  if (source.className != "CTEFireBullets") return false;
  TempEffectEvent event;
  event.tick = source.tick;
  event.kind = TempEffectKind::FireBullets;
  event.hasPosition = source.hasOrigin;
  if (event.hasPosition) event.origin = {source.origin[0], source.origin[1], source.origin[2]};
  event.hasDirection = source.hasAngles;
  if (event.hasDirection) event.direction = {source.angles[0], source.angles[1], source.angles[2]};
  if (source.hasPlayer) event.entityIndex = static_cast<std::int32_t>(source.player);
  if (source.hasWeaponId) event.weaponId = static_cast<std::int32_t>(source.weaponId);
  if (source.hasSeed) event.seed = static_cast<std::uint32_t>(source.seed);
  event.shots = 1; // One CTEFireBullets event; protocol shot count is not decoded.
  return timeline.add(event);
}

bool addExplosionEvent(const TempEntityEvent& source, TempEffectTimeline& timeline) {
  if (source.className != "CTETFExplosion" && source.className != "CTEExplosion") return false;
  TempEffectEvent event;
  event.tick = source.tick;
  event.kind = TempEffectKind::Explosion;
  event.hasPosition = source.hasOrigin;
  if (event.hasPosition) event.origin = {source.origin[0], source.origin[1], source.origin[2]};
  if (source.hasMagnitude) { event.hasMagnitude = true; event.magnitude = static_cast<std::int32_t>(source.magnitude); }
  if (source.hasScale) { event.hasScale = true; event.scale = static_cast<std::int32_t>(source.scale); }
  if (source.hasRadius) { event.hasRadius = true; event.radius = static_cast<std::int32_t>(source.radius); }
  return timeline.add(event);
}

bool addParticleEvent(const TempEntityEvent& source, TempEffectTimeline& timeline) {
  if (source.className != "CTETFParticleEffect") return false;
  TempEffectEvent event;
  event.tick = source.tick;
  event.kind = TempEffectKind::Particle;
  event.hasPosition = source.hasOrigin;
  if (event.hasPosition) event.origin = {source.origin[0], source.origin[1], source.origin[2]};
  if (source.hasEffectIndex) event.particleSystemIndex = static_cast<std::int32_t>(source.effectIndex);
  if (source.hasParticleName) event.particleName = source.particleName;
  return timeline.add(event);
}

} // namespace tf2::native
