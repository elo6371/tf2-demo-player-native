#include "demo_header.h"
#include "effects_timeline.h"
#include "gameplay_effects.h"

#include <cmath>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

// P4 (2026-11-22): --gameplay-fx. A fixture snapshot with two mediguns (stock and
// Kritzkrieg), a firing flamethrower and five players, run through the reading,
// the draw list and the tints. No demo and no device: this is the contract the
// product's --gameplay-fx path is built on, checked where a failure has one cause.
using namespace tf2::native;

EntityPropertyValue intValue(std::int64_t value) {
  EntityPropertyValue v;
  v.type = SendPropType::Int;
  v.intValue = value;
  v.lastWriteTick = 100;
  return v;
}
EntityPropertyValue floatValue(float value) {
  EntityPropertyValue v;
  v.type = SendPropType::Float;
  v.x = value;
  v.lastWriteTick = 100;
  return v;
}
EntityPropertyValue vectorValue(float x, float y, float z) {
  EntityPropertyValue v;
  v.type = SendPropType::Vector;
  v.x = x; v.y = y; v.z = z;
  v.lastWriteTick = 100;
  return v;
}
std::int64_t handle(std::int32_t index, std::int32_t serial) {
  return static_cast<std::int64_t>(index) | (static_cast<std::int64_t>(serial) << 11);
}

void addPlayer(std::vector<EntityState>& states, std::int32_t index, std::int32_t team,
               float x, float y, float yaw, std::int32_t activeWeapon) {
  EntityState& s = states[static_cast<std::size_t>(index)];
  s.classId = 0;
  s.properties["DT_BaseEntity.m_vecOrigin"] = vectorValue(x, y, 0.0f);
  s.properties["DT_TFNonLocalPlayerExclusive.m_angEyeAngles[0]"] = floatValue(0.0f);
  s.properties["DT_TFNonLocalPlayerExclusive.m_angEyeAngles[1]"] = floatValue(yaw);
  s.properties["DT_BaseEntity.m_iTeamNum"] = intValue(team);
  if (activeWeapon >= 0) s.properties["DT_BaseCombatCharacter.m_hActiveWeapon"] = intValue(handle(activeWeapon, 3));
}

void addMedigun(std::vector<EntityState>& states, std::int32_t index, std::int32_t owner,
                std::int32_t target, bool healing, bool charge, std::int64_t item) {
  EntityState& s = states[static_cast<std::size_t>(index)];
  s.classId = 1;
  s.properties["DT_BaseEntity.m_hOwnerEntity"] = intValue(handle(owner, 5));
  s.properties["DT_WeaponMedigun.m_bHealing"] = intValue(healing ? 1 : 0);
  s.properties["DT_WeaponMedigun.m_hHealingTarget"] = intValue(target >= 0 ? handle(target, 9) : 0x1FFFFF);
  s.properties["DT_WeaponMedigun.m_bChargeRelease"] = intValue(charge ? 1 : 0);
  s.properties["DT_ScriptCreatedItem.m_iItemDefinitionIndex"] = intValue(item);
}

bool allFinite(const GameplayFxDrawList& list) {
  for (const auto& s : list.sprites) {
    for (float v : s.center) if (!std::isfinite(v)) return false;
    if (!std::isfinite(s.radius) || !(s.radius > 0.0f)) return false;
  }
  for (const auto& r : list.ribbons) {
    for (float v : r.a) if (!std::isfinite(v)) return false;
    for (float v : r.b) if (!std::isfinite(v)) return false;
  }
  return true;
}

bool sameList(const GameplayFxDrawList& a, const GameplayFxDrawList& b) {
  if (a.sprites.size() != b.sprites.size() || a.ribbons.size() != b.ribbons.size()) return false;
  for (std::size_t i = 0; i < a.sprites.size(); ++i) {
    if (std::memcmp(a.sprites[i].center, b.sprites[i].center, sizeof(a.sprites[i].center)) != 0) return false;
    if (std::memcmp(a.sprites[i].color, b.sprites[i].color, sizeof(a.sprites[i].color)) != 0) return false;
  }
  for (std::size_t i = 0; i < a.ribbons.size(); ++i) {
    if (std::memcmp(a.ribbons[i].a, b.ribbons[i].a, sizeof(a.ribbons[i].a)) != 0) return false;
  }
  return true;
}

int runGameplayFxSelfTest() {
  DemoNetworkSummary summary;
  summary.serverClassNames = {"CTFPlayer", "CWeaponMedigun", "CTFFlameThrower"};
  std::vector<EntityState> states(32);
  addPlayer(states, 1, kFxTeamRed, 0.0f, 0.0f, 0.0f, 10);     // stock medic, red
  addPlayer(states, 2, kFxTeamRed, 200.0f, 0.0f, 0.0f, -1);   // its patient
  addPlayer(states, 3, kFxTeamBlue, 0.0f, 300.0f, 90.0f, 11); // pyro, blue
  addPlayer(states, 4, kFxTeamBlue, 500.0f, 0.0f, 180.0f, 12);// kritz medic, blue
  addPlayer(states, 5, kFxTeamBlue, 300.0f, 0.0f, 0.0f, -1);  // its patient
  addPlayer(states, 6, kFxTeamRed, -300.0f, 0.0f, 0.0f, 13);  // medic holding something else
  addMedigun(states, 10, 1, 2, true, true, 29);
  addMedigun(states, 12, 4, 5, true, true, 35);
  addMedigun(states, 14, 6, 2, true, false, 29);              // not the active weapon of 6
  {
    EntityState& s = states[11];
    s.classId = 2;
    s.properties["DT_BaseEntity.m_hOwnerEntity"] = intValue(handle(3, 1));
    s.properties["DT_TFFlameThrower.m_iWeaponState"] = intValue(2);
  }

  const GameplayFxReading reading = readGameplayFx(states, summary, 1000, FxUberSource::Medigun);
  const FxPlayer* medic = reading.player(1);
  const FxPlayer* patient = reading.player(2);
  const FxPlayer* pyro = reading.player(3);
  const FxPlayer* kritzMedic = reading.player(4);
  const FxPlayer* kritzPatient = reading.player(5);
  const bool readOk = reading.players.size() == 6 && reading.mediguns.size() == 3
    && reading.flamethrowers.size() == 1 && reading.healingBeams == 2 && reading.chargesReleased == 2
    && reading.flamesFiring == 1 && reading.uberPlayers == 2 && reading.kritzPlayers == 2;
  const bool uberOk = medic && patient && medic->uber && patient->uber && !medic->kritz
    && pyro && !pyro->uber && kritzMedic && kritzPatient && kritzMedic->kritz && kritzPatient->kritz
    && !kritzMedic->uber;

  FxBuildOptions options;
  const GameplayFxDrawList list = buildGameplayFxDrawList(reading, 1000, options);
  const GameplayFxDrawList again = buildGameplayFxDrawList(reading, 1000, options);
  const GameplayFxDrawList later = buildGameplayFxDrawList(reading, 1007, options);
  const bool listOk = list.enabled && list.beams == 2 && list.chargedBeams == 2 && list.flames == 1
    && list.kritzSparks == 2 && list.auras == 2 && !list.sprites.empty() && !list.ribbons.empty()
    && allFinite(list) && allFinite(later);
  const bool deterministic = sameList(list, again);
  const bool animated = !sameList(list, later);

  // Parts off -> nothing of that part.
  FxBuildOptions none;
  none.beam = none.flame = none.kritz = none.uberAura = false;
  const GameplayFxDrawList empty = buildGameplayFxDrawList(reading, 1000, none);
  const bool partsOk = empty.sprites.empty() && empty.ribbons.empty() && empty.beams == 0;

  // No charge -> no uber; condition bits are ignored under the medigun source and
  // honoured under the condition source.
  std::vector<EntityState> quiet = states;
  quiet[10].properties["DT_WeaponMedigun.m_bChargeRelease"] = intValue(0);
  quiet[2].properties["DT_TFPlayerShared.m_nPlayerCond"] = intValue(1ll << kFxCondInvulnerable);
  const GameplayFxReading quietMedigun = readGameplayFx(quiet, summary, 1000, FxUberSource::Medigun);
  const GameplayFxReading quietCond = readGameplayFx(quiet, summary, 1000, FxUberSource::Condition);
  const bool sourceOk = !quietMedigun.player(1)->uber && !quietMedigun.player(2)->uber
    && quietCond.player(2)->uber && quietCond.player(2)->uberFromCond && !quietCond.player(1)->uber
    && quietCond.condOnly == 1;

  // Tints and the first-person overlay.
  const FxTint body = uberBodyTint(*medic, 1000);
  const FxTint none2 = uberBodyTint(*pyro, 1000);
  const FxTint crit = critWeaponTint(*kritzMedic, 1000);
  float arms[4];
  float weapon[4];
  const bool fpUber = firstPersonFxOverlay(*medic, 1000, arms, weapon) && arms[3] > 0.0f && weapon[3] > 0.0f;
  const bool fpKritz = firstPersonFxOverlay(*kritzMedic, 1000, arms, weapon) && arms[3] == 0.0f && weapon[3] > 0.0f;
  const bool fpNone = !firstPersonFxOverlay(*pyro, 1000, arms, weapon) && arms[3] == 0.0f && weapon[3] == 0.0f;
  const bool tintOk = body.active && body.rgb[0] > body.rgb[2] && !none2.active && crit.active
    && crit.rgb[2] > crit.rgb[0] && fpUber && fpKritz && fpNone;

  // Handles and kinds.
  const bool handleOk = fxHandleIndex(handle(10, 5)) == 10 && fxHandleIndex(0x1FFFFF) == -1
    && fxHandleIndex(2047) == -1 && fxHandleIndex(-1) == -1 && fxOwnerEntity(states[10]) == 1;
  const bool kindOk = medigunChargeKindForItem(35) == MedigunChargeKind::Kritz
    && medigunChargeKindForItem(411) == MedigunChargeKind::QuickFix
    && medigunChargeKindForItem(998) == MedigunChargeKind::Vaccinator
    && medigunChargeKindForItem(29) == MedigunChargeKind::Uber
    && medigunChargeKindForItem(-1) == MedigunChargeKind::Uber;

  const bool pass = readOk && uberOk && listOk && deterministic && animated && partsOk && sourceOk
    && tintOk && handleOk && kindOk;
  std::cout << "gameplay_fx_selftest read=" << (readOk ? 1 : 0)
            << " uber=" << (uberOk ? 1 : 0)
            << " list=" << (listOk ? 1 : 0)
            << " beams=" << list.beams << " charged=" << list.chargedBeams
            << " flames=" << list.flames << " kritz=" << list.kritzSparks << " auras=" << list.auras
            << " sprites=" << list.sprites.size() << " ribbons=" << list.ribbons.size()
            << " deterministic=" << (deterministic ? 1 : 0)
            << " animated=" << (animated ? 1 : 0)
            << " parts=" << (partsOk ? 1 : 0)
            << " source=" << (sourceOk ? 1 : 0)
            << " tint=" << (tintOk ? 1 : 0)
            << " handle=" << (handleOk ? 1 : 0)
            << " kind=" << (kindOk ? 1 : 0)
            << " status=" << (pass ? "pass" : "fail") << "\n";
  return pass ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc > 1 && std::strcmp(argv[1], "--gameplay-fx") == 0) return runGameplayFxSelfTest();
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
