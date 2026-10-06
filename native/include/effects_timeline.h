#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace tf2::native {

struct TempEntityEvent;

enum class TempEffectKind : std::uint8_t {
  FireBullets = 0,
  Particle,
  Explosion,
};

struct EffectVec3 { float x = 0.0f; float y = 0.0f; float z = 0.0f; };

struct TempEffectEvent {
  std::int32_t tick = 0;
  float fireDelaySeconds = 0.0f;
  TempEffectKind kind = TempEffectKind::Particle;
  EffectVec3 origin;
  EffectVec3 direction;
  std::int32_t entityIndex = -1;
  std::int32_t weaponId = -1;
  std::int32_t particleSystemIndex = -1;
  std::int32_t customParticleIndex = -1;
  std::uint32_t seed = 0;
  std::uint16_t shots = 0;
  std::int32_t magnitude = 0;
  std::int32_t scale = 0;
  std::int32_t radius = 0;
  bool hasMagnitude = false;
  bool hasScale = false;
  bool hasRadius = false;
  bool hasPosition = false;
  bool hasDirection = false;
  std::string particleName;
};

class TempEffectTimeline {
public:
  void clear();
  bool add(TempEffectEvent event);
  void sortByTick();
  const std::vector<TempEffectEvent>& events() const { return events_; }
  std::vector<TempEffectEvent> range(std::int32_t firstTick, std::int32_t lastTick) const;
  // Requires sortByTick. Counts events whose tick is at or before the target.
  std::size_t countThrough(std::int32_t tick) const;

private:
  std::vector<TempEffectEvent> events_;
};

// Converts only fields already decoded for CTEFireBullets. delayRaw is kept out
// because its time-unit semantics are not established by the demo reader.
bool addFireBulletsEvent(const TempEntityEvent& source, TempEffectTimeline& timeline);

bool addExplosionEvent(const TempEntityEvent& source, TempEffectTimeline& timeline);

// CTETFParticleEffect only. delayRaw is not converted; its time unit is not
// established by the demo reader, so fireDelaySeconds stays 0.
bool addParticleEvent(const TempEntityEvent& source, TempEffectTimeline& timeline);

} // namespace tf2::native
