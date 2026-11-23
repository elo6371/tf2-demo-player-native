#include "gameplay_effects.h"

#include "entity_model.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace tf2::native {
namespace {

constexpr float kPi = 3.14159265358979323846f;

// ---- property access ---------------------------------------------------------
// The same matching rule playback_hud.cpp uses (the bare name, or the name after
// a table prefix and a '.'), with one addition that matters for a draw list: when
// a property exists twice, the later write wins and equal ticks are broken by the
// name, so the choice never depends on unordered_map iteration order.
bool nameMatches(const std::string& name, const char* key) {
  const std::size_t length = std::strlen(key);
  if (name.size() == length) return name == key;
  if (name.size() < length + 1) return false;
  if (name.compare(name.size() - length, length, key) != 0) return false;
  return name[name.size() - length - 1] == '.';
}

const EntityPropertyValue* findValue(const EntityState& state, const char* key) {
  const EntityPropertyValue* best = nullptr;
  const std::string* bestName = nullptr;
  for (const auto& entry : state.properties) {
    if (!nameMatches(entry.first, key)) continue;
    if (best == nullptr || entry.second.lastWriteTick > best->lastWriteTick
        || (entry.second.lastWriteTick == best->lastWriteTick && entry.first < *bestName)) {
      best = &entry.second;
      bestName = &entry.first;
    }
  }
  return best;
}

bool readInt(const EntityState& state, const char* key, std::int64_t& out) {
  const EntityPropertyValue* value = findValue(state, key);
  if (value == nullptr) return false;
  if (value->type == SendPropType::Float) {
    if (!std::isfinite(value->x)) return false;
    out = static_cast<std::int64_t>(value->x);
    return true;
  }
  if (value->type != SendPropType::Int) return false;
  out = value->intValue;
  return true;
}

bool readFloat(const EntityState& state, const char* key, float& out) {
  const EntityPropertyValue* value = findValue(state, key);
  if (value == nullptr) return false;
  if (value->type == SendPropType::Int) {
    out = static_cast<float>(value->intValue);
    return true;
  }
  if (value->type != SendPropType::Float || !std::isfinite(value->x)) return false;
  out = value->x;
  return true;
}

const char* className(const DemoNetworkSummary& summary, std::int32_t classId) {
  if (classId < 0) return nullptr;
  const auto index = static_cast<std::size_t>(classId);
  if (index < summary.serverClassNames.size() && !summary.serverClassNames[index].empty()) {
    return summary.serverClassNames[index].c_str();
  }
  for (const auto& schema : summary.serverClassSchemas) {
    if (schema.id == index) return schema.name.c_str();
  }
  return nullptr;
}

bool isPlayerClass(const char* name) {
  return name != nullptr && std::strcmp(name, "CTFPlayer") == 0;
}

// ---- deterministic noise -------------------------------------------------------
std::uint32_t mix(std::uint32_t value) {
  value ^= value >> 16;
  value *= 0x7feb352du;
  value ^= value >> 15;
  value *= 0x846ca68bu;
  value ^= value >> 16;
  return value;
}

std::uint32_t hash3(std::uint32_t a, std::uint32_t b, std::uint32_t c) {
  return mix(a * 0x9e3779b9u ^ mix(b + 0x632be5abu) ^ mix(c * 0x85ebca6bu + 0x27d4eb2fu));
}

float unit(std::uint32_t h) {
  return static_cast<float>(h >> 8) * (1.0f / 16777216.0f);
}

float fract(float value) { return value - std::floor(value); }

// ---- small vector helpers --------------------------------------------------------
struct V3 { float x, y, z; };
V3 add(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 sub(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 scale(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float length(V3 a) { return std::sqrt(dot(a, a)); }
V3 normalizeOr(V3 a, V3 fallback) {
  const float l = length(a);
  return (l > 1.0e-5f && std::isfinite(l)) ? scale(a, 1.0f / l) : fallback;
}
V3 lerp(V3 a, V3 b, float t) { return add(a, scale(sub(b, a), t)); }

// Source's angle convention: pitch positive looks DOWN, yaw 0 looks along +X.
V3 aimForward(float pitchDegrees, float yawDegrees) {
  const float p = pitchDegrees * (kPi / 180.0f);
  const float y = yawDegrees * (kPi / 180.0f);
  return {std::cos(p) * std::cos(y), std::cos(p) * std::sin(y), -std::sin(p)};
}
V3 flatRight(float yawDegrees) {
  const float y = yawDegrees * (kPi / 180.0f);
  return {std::sin(y), -std::cos(y), 0.0f};
}
V3 flatForward(float yawDegrees) {
  const float y = yawDegrees * (kPi / 180.0f);
  return {std::cos(y), std::sin(y), 0.0f};
}

V3 originOf(const FxPlayer& player) { return {player.origin[0], player.origin[1], player.origin[2]}; }

// The point a held weapon's business end sits at, in map units: a fixed offset
// from the feet origin along the view. Stated, not measured off a model -- the
// world weapon attachment is a later card (STATUS P1 "武器 attachment/bone A/B").
V3 muzzlePoint(const FxPlayer& player, float height, float forward, float right) {
  const float yaw = player.hasEye ? player.eyeYaw : 0.0f;
  return add(add(add(originOf(player), V3{0.0f, 0.0f, height}),
                 scale(flatForward(yaw), forward)), scale(flatRight(yaw), right));
}

void teamColor(std::int32_t team, float out[3]) {
  if (team == kFxTeamBlue) { out[0] = 0.30f; out[1] = 0.58f; out[2] = 1.00f; return; }
  if (team == kFxTeamRed) { out[0] = 1.00f; out[1] = 0.30f; out[2] = 0.22f; return; }
  out[0] = 0.85f; out[1] = 0.85f; out[2] = 0.85f;
}

void pushSprite(GameplayFxDrawList& list, V3 c, float radius, const float rgb[3], float alpha, FxBlend blend) {
  if (!std::isfinite(c.x) || !std::isfinite(c.y) || !std::isfinite(c.z) || !(radius > 0.0f) || !(alpha > 0.0f)) return;
  FxSprite s;
  s.center[0] = c.x; s.center[1] = c.y; s.center[2] = c.z;
  s.radius = radius;
  s.color[0] = rgb[0]; s.color[1] = rgb[1]; s.color[2] = rgb[2]; s.color[3] = std::min(alpha, 1.0f);
  s.blend = blend;
  list.sprites.push_back(s);
}

void pushRibbon(GameplayFxDrawList& list, V3 a, V3 b, float width, const float rgb[3], float alpha, FxBlend blend) {
  if (!std::isfinite(a.x) || !std::isfinite(b.x) || !std::isfinite(a.z) || !std::isfinite(b.z) || !(alpha > 0.0f)) return;
  FxRibbon r;
  r.a[0] = a.x; r.a[1] = a.y; r.a[2] = a.z;
  r.b[0] = b.x; r.b[1] = b.y; r.b[2] = b.z;
  r.width = std::max(width, 0.5f);
  r.color[0] = rgb[0]; r.color[1] = rgb[1]; r.color[2] = rgb[2]; r.color[3] = std::min(alpha, 1.0f);
  r.blend = blend;
  list.ribbons.push_back(r);
}

bool condBit(const FxPlayer& player, std::uint32_t bit) {
  if (!player.hasCond || bit >= 128u) return false;
  return ((player.cond[bit / 32u] >> (bit % 32u)) & 1u) != 0u;
}

// The beam, a quadratic curve that leaves the gun along the medic's aim and bends
// onto the target -- the shape the game draws when the medic looks away from the
// patient. Core + glow strips, and sparks travelling from medic to patient.
void buildBeam(GameplayFxDrawList& list, const FxPlayer& medic, const FxPlayer& target,
               const FxMedigun& gun, std::int32_t tick) {
  const V3 start = muzzlePoint(medic, 46.0f, 22.0f, 7.0f);
  const V3 end = add(originOf(target), V3{0.0f, 0.0f, 42.0f});
  const float distance = length(sub(end, start));
  if (!(distance > 1.0f) || distance > 2400.0f) return;
  const V3 aim = medic.hasEye ? aimForward(medic.eyePitch, medic.eyeYaw)
                              : normalizeOr(sub(end, start), V3{1.0f, 0.0f, 0.0f});
  const V3 control = add(start, scale(aim, distance * 0.45f));
  const bool charged = gun.chargeRelease;
  float rgb[3];
  teamColor(medic.team, rgb);
  float core[3] = {0.55f + 0.45f * rgb[0], 0.55f + 0.45f * rgb[1], 0.55f + 0.45f * rgb[2]};
  constexpr int kSegments = 20;
  V3 previous = start;
  const float t0 = static_cast<float>(tick);
  for (int i = 1; i <= kSegments; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(kSegments);
    const float u = 1.0f - t;
    V3 point = add(add(scale(start, u * u), scale(control, 2.0f * u * t)), scale(end, t * t));
    // A gentle vertical shimmer, zero at both ends so the beam stays attached.
    point.z += 1.6f * std::sin(t * kPi) * std::sin(t0 * 0.35f + t * 9.0f + static_cast<float>(gun.entity));
    pushRibbon(list, previous, point, charged ? 15.0f : 10.0f, rgb, charged ? 0.42f : 0.30f, FxBlend::Additive);
    pushRibbon(list, previous, point, charged ? 3.6f : 2.4f, core, 0.92f, FxBlend::Additive);
    previous = point;
  }
  const int sparks = charged ? 16 : 9;
  for (int i = 0; i < sparks; ++i) {
    const float t = fract(static_cast<float>(i) / static_cast<float>(sparks) + t0 * (charged ? 0.06f : 0.045f));
    const float u = 1.0f - t;
    const V3 point = add(add(scale(start, u * u), scale(control, 2.0f * u * t)), scale(end, t * t));
    pushSprite(list, point, charged ? 4.5f : 3.4f, core, 0.85f, FxBlend::Additive);
  }
  pushSprite(list, end, charged ? 16.0f : 10.0f, rgb, charged ? 0.32f : 0.22f, FxBlend::Additive);
  ++list.beams;
  if (charged) ++list.chargedBeams;
}

void flameColor(float age, bool crit, std::int32_t team, float out[3]) {
  // blue-white core -> yellow -> orange -> deep red, by age.
  const V3 stops[4] = {{0.55f, 0.70f, 1.00f}, {1.00f, 0.86f, 0.40f}, {1.00f, 0.48f, 0.10f}, {0.80f, 0.16f, 0.03f}};
  const float pos[4] = {0.0f, 0.14f, 0.45f, 1.0f};
  V3 c = stops[3];
  for (int i = 0; i < 3; ++i) {
    if (age <= pos[i + 1]) {
      c = lerp(stops[i], stops[i + 1], (age - pos[i]) / (pos[i + 1] - pos[i]));
      break;
    }
  }
  if (crit) {
    float tc[3];
    teamColor(team, tc);
    c = lerp(c, V3{tc[0], tc[1], tc[2]}, 0.35f);
  }
  out[0] = c.x; out[1] = c.y; out[2] = c.z;
}

void buildFlame(GameplayFxDrawList& list, const FxPlayer& pyro, const FxFlamethrower& gun, std::int32_t tick) {
  const V3 aim = pyro.hasEye ? aimForward(pyro.eyePitch, pyro.eyeYaw) : V3{1.0f, 0.0f, 0.0f};
  const V3 side = normalizeOr(cross(aim, V3{0.0f, 0.0f, 1.0f}), V3{0.0f, 1.0f, 0.0f});
  const V3 lift = normalizeOr(cross(side, aim), V3{0.0f, 0.0f, 1.0f});
  const V3 muzzle = add(muzzlePoint(pyro, 40.0f, 0.0f, 9.0f), scale(aim, 30.0f));
  const auto entity = static_cast<std::uint32_t>(gun.entity);
  if (gun.firing()) {
    constexpr int kParticles = 56;
    constexpr float kLifetimeTicks = 22.0f;
    for (int i = 0; i < kParticles; ++i) {
      const auto index = static_cast<std::uint32_t>(i);
      const float phase = unit(hash3(entity, index, 0u));
      const float clock = static_cast<float>(tick) / kLifetimeTicks + phase;
      const float age = fract(clock);
      // A new set of offsets every time a particle is reborn, so the plume does
      // not look like 56 dots sliding along fixed rails.
      const auto life = static_cast<std::uint32_t>(static_cast<std::int64_t>(std::floor(clock)) & 0x7fffffff);
      const std::uint32_t seed = hash3(entity, index, life + 1u);
      const float reach = (12.0f + age * 330.0f) * (0.80f + 0.40f * unit(mix(seed ^ 0x1u)));
      const float spread = 3.0f + age * 46.0f;
      const float angle = 2.0f * kPi * unit(mix(seed ^ 0x2u));
      const float radial = spread * std::sqrt(unit(mix(seed ^ 0x3u)));
      V3 point = add(muzzle, scale(aim, reach));
      point = add(point, scale(side, radial * std::cos(angle)));
      point = add(point, scale(lift, radial * std::sin(angle)));
      point.z += age * age * 36.0f;
      const float size = (5.0f + age * 26.0f) * (0.70f + 0.60f * unit(mix(seed ^ 0x4u)));
      float rgb[3];
      flameColor(age, gun.critFire, pyro.team, rgb);
      const float alpha = 0.55f * std::pow(std::max(0.0f, 1.0f - age), 0.7f);
      pushSprite(list, point, size, rgb, alpha, FxBlend::Additive);
      if (age > 0.72f) {
        const float smoke[3] = {0.17f, 0.15f, 0.14f};
        pushSprite(list, add(point, V3{0.0f, 0.0f, 6.0f}), size * 1.25f, smoke,
                   0.22f * (1.0f - age) / 0.28f, FxBlend::Alpha);
      }
    }
    const float glow[3] = {1.0f, 0.62f, 0.22f};
    pushSprite(list, muzzle, 14.0f, glow, 0.45f, FxBlend::Additive);
    ++list.flames;
  } else if (gun.airblast()) {
    const float phase = fract(static_cast<float>(tick) / 10.0f);
    const float puff[3] = {0.82f, 0.88f, 0.95f};
    for (int ring = 0; ring < 3; ++ring) {
      const float reach = 20.0f + (static_cast<float>(ring) + phase) * 26.0f;
      const float radius = 10.0f + (static_cast<float>(ring) + phase) * 12.0f;
      for (int k = 0; k < 10; ++k) {
        const float angle = 2.0f * kPi * static_cast<float>(k) / 10.0f;
        V3 point = add(muzzle, scale(aim, reach));
        point = add(point, scale(side, radius * std::cos(angle)));
        point = add(point, scale(lift, radius * std::sin(angle)));
        pushSprite(list, point, 7.0f + 3.0f * static_cast<float>(ring), puff,
                   0.20f * (1.0f - (static_cast<float>(ring) + phase) / 3.0f), FxBlend::Alpha);
      }
    }
    ++list.airblasts;
  }
}

// Electric crackle around the weapon of a crit-boosted player: three short
// jagged arcs, re-drawn every three ticks, and a soft glow under them.
void buildKritz(GameplayFxDrawList& list, const FxPlayer& player, std::int32_t tick) {
  const V3 hand = muzzlePoint(player, 44.0f, 16.0f, 9.0f);
  float rgb[3];
  teamColor(player.team, rgb);
  const float bright[3] = {0.5f + 0.5f * rgb[0], 0.5f + 0.5f * rgb[1], 0.5f + 0.5f * rgb[2]};
  const auto entity = static_cast<std::uint32_t>(player.entity);
  const auto bucket = static_cast<std::uint32_t>((tick < 0 ? 0 : tick) / 3);
  for (std::uint32_t arc = 0; arc < 3u; ++arc) {
    std::uint32_t seed = hash3(entity, bucket, arc + 7u);
    V3 point = add(hand, V3{(unit(mix(seed ^ 1u)) - 0.5f) * 18.0f, (unit(mix(seed ^ 2u)) - 0.5f) * 18.0f,
                            (unit(mix(seed ^ 3u)) - 0.5f) * 18.0f});
    for (int step = 0; step < 5; ++step) {
      seed = mix(seed + 0x9e37u);
      const V3 jump = normalizeOr(V3{unit(mix(seed ^ 4u)) - 0.5f, unit(mix(seed ^ 5u)) - 0.5f,
                                     unit(mix(seed ^ 6u)) - 0.5f}, V3{0.0f, 0.0f, 1.0f});
      const V3 next = add(point, scale(jump, 5.0f));
      pushRibbon(list, point, next, 1.1f, bright, 0.95f, FxBlend::Additive);
      point = next;
    }
  }
  pushSprite(list, hand, 15.0f, rgb, 0.24f, FxBlend::Additive);
  ++list.kritzSparks;
}

}  // namespace

const char* medigunChargeKindName(MedigunChargeKind kind) {
  switch (kind) {
    case MedigunChargeKind::Uber: return "uber";
    case MedigunChargeKind::Kritz: return "kritz";
    case MedigunChargeKind::QuickFix: return "quickfix";
    case MedigunChargeKind::Vaccinator: return "vaccinator";
  }
  return "uber";
}

MedigunChargeKind medigunChargeKindForItem(std::int64_t itemDefinitionIndex) {
  switch (itemDefinitionIndex) {
    case 35: return MedigunChargeKind::Kritz;
    case 411: return MedigunChargeKind::QuickFix;
    case 998: return MedigunChargeKind::Vaccinator;
    default: return MedigunChargeKind::Uber;
  }
}

std::int32_t fxHandleIndex(std::int64_t handle) {
  if (handle < 0 || handle == 0xFFFFFFFFll) return -1;
  const auto index = static_cast<std::int32_t>(handle & 0x7FF);
  return index == 0x7FF ? -1 : index;
}

std::int32_t fxOwnerEntity(const EntityState& state) {
  std::int64_t handle = 0;
  if (readInt(state, "m_hOwnerEntity", handle)) {
    const std::int32_t index = fxHandleIndex(handle);
    if (index > 0) return index;
  }
  if (readInt(state, "m_hOwner", handle)) {
    const std::int32_t index = fxHandleIndex(handle);
    if (index > 0) return index;
  }
  return -1;
}

const FxPlayer* GameplayFxReading::player(std::int32_t entity) const {
  const auto found = std::lower_bound(players.begin(), players.end(), entity,
    [](const FxPlayer& row, std::int32_t wanted) { return row.entity < wanted; });
  if (found == players.end() || found->entity != entity) return nullptr;
  return &*found;
}

GameplayFxReading readGameplayFx(const std::vector<EntityState>& states,
                                 const DemoNetworkSummary& summary,
                                 std::int32_t tick,
                                 FxUberSource source) {
  GameplayFxReading reading;
  reading.tick = tick;
  reading.source = source;
  for (std::size_t index = 0; index < states.size(); ++index) {
    const EntityState& state = states[index];
    if (state.classId < 0 || state.properties.empty()) continue;
    const char* name = className(summary, state.classId);
    if (name == nullptr) continue;
    const auto entity = static_cast<std::int32_t>(index);
    if (isPlayerClass(name)) {
      FxPlayer player;
      player.entity = entity;
      const ModelInstanceTransform transform = EntityModelResolver::extractTransform(state);
      player.hasOrigin = transform.hasOrigin;
      for (int axis = 0; axis < 3; ++axis) player.origin[axis] = transform.origin[axis];
      player.hasEye = transform.hasAngles;
      player.eyePitch = transform.angles[0];
      player.eyeYaw = transform.angles[1];
      player.team = transform.hasTeam ? static_cast<std::int32_t>(transform.team) : 0;
      std::int64_t handle = 0;
      if (readInt(state, "m_hActiveWeapon", handle)) player.activeWeapon = fxHandleIndex(handle);
      std::int64_t word = 0;
      if (readInt(state, "m_nPlayerCond", word)) {
        player.hasCond = true;
        player.cond[0] |= static_cast<std::uint32_t>(word & 0xFFFFFFFFll);
      }
      if (readInt(state, "_condition_bits", word)) {
        player.hasCond = true;
        player.cond[0] |= static_cast<std::uint32_t>(word & 0xFFFFFFFFll);
      }
      if (readInt(state, "m_nPlayerCondEx", word)) {
        player.hasCond = true;
        player.cond[1] |= static_cast<std::uint32_t>(word & 0xFFFFFFFFll);
      }
      if (readInt(state, "m_nPlayerCondEx2", word)) {
        player.hasCond = true;
        player.cond[2] |= static_cast<std::uint32_t>(word & 0xFFFFFFFFll);
      }
      if (readInt(state, "m_nPlayerCondEx3", word)) {
        player.hasCond = true;
        player.cond[3] |= static_cast<std::uint32_t>(word & 0xFFFFFFFFll);
      }
      reading.players.push_back(player);
    } else if (std::strcmp(name, "CWeaponMedigun") == 0) {
      FxMedigun gun;
      gun.entity = entity;
      gun.owner = fxOwnerEntity(state);
      std::int64_t value = 0;
      if (readInt(state, "m_bHealing", value)) gun.healing = value != 0;
      if (readInt(state, "m_hHealingTarget", value)) gun.target = fxHandleIndex(value);
      if (readInt(state, "m_bChargeRelease", value)) gun.chargeRelease = value != 0;
      gun.hasChargeLevel = readFloat(state, "m_flChargeLevel", gun.chargeLevel);
      if (readInt(state, "m_iItemDefinitionIndex", value)) gun.itemDefinitionIndex = value;
      gun.kind = medigunChargeKindForItem(gun.itemDefinitionIndex);
      reading.mediguns.push_back(gun);
    } else if (std::strcmp(name, "CTFFlameThrower") == 0) {
      FxFlamethrower gun;
      gun.entity = entity;
      gun.owner = fxOwnerEntity(state);
      std::int64_t value = 0;
      if (readInt(state, "m_iWeaponState", value)) gun.weaponState = value;
      if (readInt(state, "m_bCritFire", value)) gun.critFire = value != 0;
      reading.flamethrowers.push_back(gun);
    }
  }
  // players are already ascending (index order), which player() relies on.

  auto playerAt = [&reading](std::int32_t entity) -> FxPlayer* {
    const auto found = std::lower_bound(reading.players.begin(), reading.players.end(), entity,
      [](const FxPlayer& row, std::int32_t wanted) { return row.entity < wanted; });
    if (found == reading.players.end() || found->entity != entity) return nullptr;
    return &*found;
  };

  const bool useMedigun = source != FxUberSource::Condition;
  const bool useCond = source != FxUberSource::Medigun;
  for (auto& gun : reading.mediguns) {
    if (FxPlayer* owner = playerAt(gun.owner)) {
      gun.ownerActiveKnown = owner->activeWeapon >= 0;
      gun.ownerActive = owner->activeWeapon == gun.entity;
    }
    if (gun.healing && gun.target >= 0 && (gun.ownerActive || !gun.ownerActiveKnown)) ++reading.healingBeams;
    if (!gun.chargeRelease) continue;
    ++reading.chargesReleased;
    if (!useMedigun) continue;
    const bool fading = gun.hasChargeLevel && gun.chargeLevel < 0.08f;
    FxPlayer* touched[2] = {playerAt(gun.owner), gun.healing ? playerAt(gun.target) : nullptr};
    for (FxPlayer* player : touched) {
      if (player == nullptr) continue;
      switch (gun.kind) {
        case MedigunChargeKind::Uber:
          player->uberFromMedigun = true;
          player->uberFading = player->uberFading || fading;
          break;
        case MedigunChargeKind::Kritz: player->critFromMedigun = true; break;
        case MedigunChargeKind::QuickFix: player->quickFix = true; break;
        case MedigunChargeKind::Vaccinator: player->vaccinator = true; break;
      }
    }
  }
  for (auto& gun : reading.flamethrowers) {
    if (FxPlayer* owner = playerAt(gun.owner)) {
      gun.ownerActiveKnown = owner->activeWeapon >= 0;
      gun.ownerActive = owner->activeWeapon == gun.entity;
    }
    const bool held = gun.ownerActive || !gun.ownerActiveKnown;
    if (held && gun.firing()) ++reading.flamesFiring;
    if (held && gun.airblast()) ++reading.airblasts;
  }
  for (auto& player : reading.players) {
    player.uberFromCond = useCond && (condBit(player, kFxCondInvulnerable)
      || condBit(player, kFxCondInvulnerableWearingOff));
    player.critFromCond = useCond && condBit(player, kFxCondCritBoosted);
    if (useCond && condBit(player, kFxCondInvulnerableWearingOff)) player.uberFading = true;
    if (useCond && condBit(player, kFxCondMegaHeal)) player.quickFix = true;
    player.uber = player.uberFromMedigun || player.uberFromCond;
    player.kritz = player.critFromMedigun || player.critFromCond;
    if (player.uber) ++reading.uberPlayers;
    if (player.kritz) ++reading.kritzPlayers;
    if ((player.uberFromMedigun && !player.uberFromCond) || (player.critFromMedigun && !player.critFromCond)) {
      ++reading.medigunOnly;
    }
    if ((player.uberFromCond && !player.uberFromMedigun) || (player.critFromCond && !player.critFromMedigun)) {
      ++reading.condOnly;
    }
  }
  return reading;
}

GameplayFxDrawList buildGameplayFxDrawList(const GameplayFxReading& reading,
                                           std::int32_t tick,
                                           const FxBuildOptions& options) {
  GameplayFxDrawList list;
  list.enabled = true;
  list.tick = tick;
  if (options.beam) {
    for (const auto& gun : reading.mediguns) {
      if (!gun.healing || gun.target < 0) continue;
      if (gun.ownerActiveKnown && !gun.ownerActive) continue;
      const FxPlayer* medic = reading.player(gun.owner);
      const FxPlayer* target = reading.player(gun.target);
      if (medic == nullptr || target == nullptr || !medic->hasOrigin || !target->hasOrigin) {
        ++list.skippedNoOrigin;
        continue;
      }
      buildBeam(list, *medic, *target, gun, tick);
    }
  }
  if (options.flame) {
    for (const auto& gun : reading.flamethrowers) {
      if (!gun.firing() && !gun.airblast()) continue;
      if (gun.ownerActiveKnown && !gun.ownerActive) continue;
      const FxPlayer* pyro = reading.player(gun.owner);
      if (pyro == nullptr || !pyro->hasOrigin) {
        ++list.skippedNoOrigin;
        continue;
      }
      buildFlame(list, *pyro, gun, tick);
    }
  }
  for (const auto& player : reading.players) {
    if (!player.hasOrigin) continue;
    if (options.kritz && player.kritz) buildKritz(list, player, tick);
    if (!options.uberAura) continue;
    float rgb[3];
    teamColor(player.team, rgb);
    const V3 chest = add(originOf(player), V3{0.0f, 0.0f, 40.0f});
    if (player.uber) {
      const bool off = player.uberFading && ((tick / 5) % 2) != 0;
      if (!off) {
        pushSprite(list, chest, 38.0f, rgb, 0.16f, FxBlend::Additive);
        ++list.auras;
      }
    } else if (player.vaccinator) {
      pushSprite(list, chest, 48.0f, rgb, 0.14f, FxBlend::Alpha);
      ++list.auras;
    } else if (player.quickFix) {
      pushSprite(list, chest, 30.0f, rgb, 0.14f, FxBlend::Additive);
      ++list.auras;
    }
  }
  return list;
}

FxTint uberBodyTint(const FxPlayer& player, std::int32_t tick) {
  FxTint tint;
  if (!player.uber) return tint;
  if (player.uberFading && ((tick / 5) % 2) != 0) return tint;
  tint.active = true;
  if (player.team == kFxTeamBlue) {
    tint.rgb[0] = 0.42f; tint.rgb[1] = 0.70f; tint.rgb[2] = 1.95f;
  } else {
    tint.rgb[0] = 1.90f; tint.rgb[1] = 0.45f; tint.rgb[2] = 0.38f;
  }
  return tint;
}

FxTint critWeaponTint(const FxPlayer& player, std::int32_t tick) {
  FxTint tint;
  if (!player.kritz) return tint;
  // A slow pulse, like the game's glowing crit weapon.
  const float pulse = 0.85f + 0.15f * std::sin(static_cast<float>(tick) * 0.3f);
  tint.active = true;
  if (player.team == kFxTeamBlue) {
    tint.rgb[0] = 0.55f * pulse; tint.rgb[1] = 0.80f * pulse; tint.rgb[2] = 1.80f * pulse;
  } else {
    tint.rgb[0] = 1.70f * pulse; tint.rgb[1] = 0.55f * pulse; tint.rgb[2] = 0.45f * pulse;
  }
  return tint;
}

bool firstPersonFxOverlay(const FxPlayer& player, std::int32_t tick, float arms[4], float weapon[4]) {
  for (int i = 0; i < 4; ++i) { arms[i] = 0.0f; weapon[i] = 0.0f; }
  float rgb[3];
  if (player.team == kFxTeamBlue) { rgb[0] = 0.20f; rgb[1] = 0.45f; rgb[2] = 1.00f; }
  else { rgb[0] = 1.00f; rgb[1] = 0.18f; rgb[2] = 0.12f; }
  bool any = false;
  if (player.uber && !(player.uberFading && ((tick / 5) % 2) != 0)) {
    for (int i = 0; i < 3; ++i) { arms[i] = rgb[i]; weapon[i] = rgb[i]; }
    arms[3] = 0.55f;
    weapon[3] = 0.55f;
    any = true;
  }
  if (player.kritz) {
    const float pulse = 0.30f + 0.12f * std::sin(static_cast<float>(tick) * 0.3f);
    for (int i = 0; i < 3; ++i) weapon[i] = rgb[i];
    weapon[3] = std::max(weapon[3], pulse);
    any = true;
  }
  return any;
}

}  // namespace tf2::native
