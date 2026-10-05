#include "demo_header.h"
#include "effects_timeline.h"

#include <cmath>
#include <iostream>

int main() {
  using namespace tf2::native;
  TempEntityEvent source;
  source.className = "CTEFireBullets";
  source.tick = 120;
  source.hasOrigin = true;
  source.origin[0] = 1.0f; source.origin[1] = 2.0f; source.origin[2] = 3.0f;
  source.hasAngles = true;
  source.angles[0] = 10.0f; source.angles[1] = 20.0f; source.angles[2] = 30.0f;
  source.hasPlayer = true; source.player = 7;
  source.hasWeaponId = true; source.weaponId = 13;
  source.hasSeed = true; source.seed = 99;
  TempEffectTimeline timeline;
  const bool accepted = addFireBulletsEvent(source, timeline);

  TempEntityEvent wrongClass = source; wrongClass.className = "CTETFExplosion";
  const bool wrongRejected = !addFireBulletsEvent(wrongClass, timeline);
  TempEffectEvent nanPosition;
  nanPosition.tick = 1; nanPosition.kind = TempEffectKind::FireBullets; nanPosition.shots = 1;
  nanPosition.hasPosition = true; nanPosition.origin.x = NAN;
  const bool nanRejected = !timeline.add(nanPosition);
  TempEffectEvent zeroShots;
  zeroShots.tick = 1; zeroShots.kind = TempEffectKind::FireBullets;
  const bool zeroRejected = !timeline.add(zeroShots);
  TempEffectEvent negativeTick;
  negativeTick.tick = -1; negativeTick.kind = TempEffectKind::Explosion;
  const bool negativeRejected = !timeline.add(negativeTick);
  TempEntityEvent explosion;
  explosion.className = "CTEExplosion";
  explosion.tick = 240;
  explosion.hasOrigin = true; explosion.origin[0] = 4.0f; explosion.origin[1] = 5.0f; explosion.origin[2] = 6.0f;
  explosion.hasMagnitude = true; explosion.magnitude = 80;
  explosion.hasScale = true; explosion.scale = 2;
  explosion.hasRadius = true; explosion.radius = 128;
  const bool explosionAccepted = addExplosionEvent(explosion, timeline);
  TempEntityEvent explosionWrongClass = explosion; explosionWrongClass.className = "CTEFireBullets";
  const bool explosionWrongRejected = !addExplosionEvent(explosionWrongClass, timeline);
  TempEntityEvent explosionNegative = explosion; explosionNegative.magnitude = -1;
  const bool explosionNegativeAccepted = addExplosionEvent(explosionNegative, timeline);
  timeline.sortByTick();
  const auto early = timeline.range(0, 119);
  const auto active = timeline.range(120, 240);
  const auto explosionMagnitude = active.size() > 1 ? active[1].magnitude : -1;
  const auto explosionScale = active.size() > 1 ? active[1].scale : -1;
  const auto explosionRadius = active.size() > 1 ? active[1].radius : -1;
  timeline.clear();
  const bool lifecycleCleared = timeline.events().empty();
  const bool pass = accepted && wrongRejected && nanRejected && zeroRejected && negativeRejected
    && explosionAccepted && explosionWrongRejected && explosionNegativeAccepted
    && early.empty() && active.size() == 3 && lifecycleCleared;
  std::cout << "temp_effect_firebullets accepted=" << (accepted ? 1 : 0)
            << " events_before_clear=" << active.size()
            << " explosion_events=2"
            << " explosion_magnitude=" << explosionMagnitude
            << " explosion_scale=" << explosionScale
            << " explosion_radius=" << explosionRadius
            << " explosion_wrong_class_rejected=" << (explosionWrongRejected ? 1 : 0)
            << " wrong_class_rejected=" << (wrongRejected ? 1 : 0)
            << " nan_rejected=" << (nanRejected ? 1 : 0)
            << " zero_shots_rejected=" << (zeroRejected ? 1 : 0)
            << " negative_tick_rejected=" << (negativeRejected ? 1 : 0)
            << " early_range=" << early.size()
            << " active_range=" << active.size()
            << " lifecycle_cleared=" << (lifecycleCleared ? 1 : 0)
            << " status=" << (pass ? "pass" : "fail") << "\n";
  return pass ? 0 : 1;
}
