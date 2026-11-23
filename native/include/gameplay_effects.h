#pragma once

// ============================================================================
// P4 (2026-11-22): gameplay effects read from the recording's own entity state.
//
//   * the Medi Gun heal beam            CWeaponMedigun m_bHealing / m_hHealingTarget
//   * Ubercharge on the medic + target  CWeaponMedigun m_bChargeRelease (+ item kind)
//   * Kritzkrieg crit boost             same, item definition 35
//   * the flamethrower's flame/airblast CTFFlameThrower m_iWeaponState / m_bCritFire
//   * player conditions (optional)      CTFPlayer m_nPlayerCond / _condition_bits
//
// WHAT THIS FILE IS. A pure reading of ONE snapshot (readGameplayFx) and a pure
// draw list built from that reading at ONE tick (buildGameplayFxDrawList). No D3D
// type appears here, so the whole chain is testable without a device
// (temp_effect_probe --gameplay-fx) and the same tick always yields the same list:
// every "random" offset is a hash of (entity, particle, tick bucket), never a
// clock and never rand().
//
// CLEAN-ROOM NOTE. The rules below are written from the networked property names
// the demo carries and from how the effects look in the game. No game source is
// copied. Three numbers are NOT read from the wire and are therefore stated as
// assumptions, each with the census that settles it (demo_open_probe --scan
// --fx-census):
//   - the condition bits (5 invulnerable, 8 invulnerable-wearing-off,
//     11 crit-boosted, 28 mega-heal). The default uber source is the medigun, so
//     no default picture depends on them; --gameplay-fx-uber-source cond|both
//     does, and the census prints how often each bit rises next to a charge.
//   - m_iWeaponState 1/2 = firing, 3 = airblast. The census prints the values seen.
//   - item definitions 35 (Kritzkrieg), 411 (Quick-Fix), 998 (Vaccinator); every
//     other medigun is treated as a stock-style Ubercharge.
// ============================================================================

#include "demo_header.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace tf2::native {

enum class MedigunChargeKind : std::uint8_t {
  Uber = 0,        // invulnerability (stock Medi Gun and its reskins)
  Kritz = 1,       // crit boost
  QuickFix = 2,    // mega-heal, no invulnerability
  Vaccinator = 3,  // resistance bubble
};

const char* medigunChargeKindName(MedigunChargeKind kind);
MedigunChargeKind medigunChargeKindForItem(std::int64_t itemDefinitionIndex);

// Where "this player is ubered / crit-boosted" comes from.
//   Medigun   the medigun's own charge state: its owner and its heal target.
//   Condition the player's condition bits (assumed numbering, see the header).
//   Both      either one.
enum class FxUberSource : std::uint8_t { Medigun = 0, Condition = 1, Both = 2 };

// TF2 wire team numbers.
constexpr std::int32_t kFxTeamRed = 2;
constexpr std::int32_t kFxTeamBlue = 3;

// The condition bits this module reads when asked to (see the header note).
constexpr std::uint32_t kFxCondInvulnerable = 5;
constexpr std::uint32_t kFxCondInvulnerableWearingOff = 8;
constexpr std::uint32_t kFxCondCritBoosted = 11;
constexpr std::uint32_t kFxCondMegaHeal = 28;

// CBaseHandle -> entity index (low 11 bits). -1 for a negative value, the 2047
// "no entity" index and the all-ones wire sentinel.
std::int32_t fxHandleIndex(std::int64_t handle);

// The entity that owns a weapon or a wearable: m_hOwnerEntity, then m_hOwner.
// -1 when neither is present or the handle is empty.
std::int32_t fxOwnerEntity(const EntityState& state);

struct FxPlayer {
  std::int32_t entity = -1;
  std::int32_t team = 0;
  bool hasOrigin = false;
  float origin[3] = {0.0f, 0.0f, 0.0f};
  bool hasEye = false;
  float eyePitch = 0.0f;
  float eyeYaw = 0.0f;
  std::int32_t activeWeapon = -1;   // m_hActiveWeapon index, -1 unknown
  bool hasCond = false;
  std::uint32_t cond[4] = {0u, 0u, 0u, 0u};  // bits 0..127

  // The decision, and where it came from. Both sources are kept so the trace
  // can say "the medigun says yes, the condition bits say no" instead of
  // collapsing it into one boolean.
  bool uber = false;
  bool uberFading = false;
  bool kritz = false;
  bool quickFix = false;
  bool vaccinator = false;
  bool uberFromMedigun = false;
  bool uberFromCond = false;
  bool critFromMedigun = false;
  bool critFromCond = false;
};

struct FxMedigun {
  std::int32_t entity = -1;
  std::int32_t owner = -1;
  std::int32_t target = -1;
  bool healing = false;
  bool chargeRelease = false;
  bool ownerActive = false;   // the owner's m_hActiveWeapon is this weapon
  bool ownerActiveKnown = false;
  bool hasChargeLevel = false;
  float chargeLevel = 0.0f;
  std::int64_t itemDefinitionIndex = -1;
  MedigunChargeKind kind = MedigunChargeKind::Uber;
};

struct FxFlamethrower {
  std::int32_t entity = -1;
  std::int32_t owner = -1;
  std::int64_t weaponState = 0;
  bool critFire = false;
  bool ownerActive = false;
  bool ownerActiveKnown = false;
  bool firing() const { return weaponState == 1 || weaponState == 2; }
  bool airblast() const { return weaponState == 3; }
};

struct GameplayFxReading {
  std::int32_t tick = -1;
  FxUberSource source = FxUberSource::Medigun;
  std::vector<FxPlayer> players;  // ascending entity index
  std::vector<FxMedigun> mediguns;
  std::vector<FxFlamethrower> flamethrowers;
  // Counters, every one a count of rows above.
  std::size_t healingBeams = 0;
  std::size_t chargesReleased = 0;
  std::size_t flamesFiring = 0;
  std::size_t airblasts = 0;
  std::size_t uberPlayers = 0;
  std::size_t kritzPlayers = 0;
  std::size_t medigunOnly = 0;  // ubered/crit by the medigun, not by the bits
  std::size_t condOnly = 0;     // ubered/crit by the bits, not by a medigun

  const FxPlayer* player(std::int32_t entity) const;
};

GameplayFxReading readGameplayFx(const std::vector<EntityState>& states,
                                 const DemoNetworkSummary& summary,
                                 std::int32_t tick,
                                 FxUberSource source);

// ---- the draw list -----------------------------------------------------------
// Map units, Z up, exactly like every other world vertex before the renderer's
// map-to-box transform. A sprite is a camera-facing soft disc; a ribbon is a
// camera-facing strip whose brightness falls off across its width.
enum class FxBlend : std::uint8_t { Additive = 0, Alpha = 1 };

struct FxSprite {
  float center[3] = {0.0f, 0.0f, 0.0f};
  float radius = 1.0f;
  float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  FxBlend blend = FxBlend::Additive;
};

struct FxRibbon {
  float a[3] = {0.0f, 0.0f, 0.0f};
  float b[3] = {0.0f, 0.0f, 0.0f};
  float width = 1.0f;
  float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  FxBlend blend = FxBlend::Additive;
};

struct FxBuildOptions {
  bool beam = true;
  bool flame = true;
  bool kritz = true;
  bool uberAura = true;
};

struct GameplayFxDrawList {
  bool enabled = false;
  std::int32_t tick = -1;
  std::vector<FxSprite> sprites;
  std::vector<FxRibbon> ribbons;
  std::size_t beams = 0;
  std::size_t chargedBeams = 0;
  std::size_t flames = 0;
  std::size_t airblasts = 0;
  std::size_t kritzSparks = 0;
  std::size_t auras = 0;
  std::size_t skippedNoOrigin = 0;  // a row whose player had no origin
};

GameplayFxDrawList buildGameplayFxDrawList(const GameplayFxReading& reading,
                                           std::int32_t tick,
                                           const FxBuildOptions& options);

// What the renderer's gameplay-effects pass did on the last frame it ran.
struct GameplayFxPassReading {
  bool drew = false;
  std::size_t sprites = 0;
  std::size_t ribbons = 0;
  std::size_t vertices = 0;
  std::size_t drawCalls = 0;
  std::size_t truncated = 0;  // primitives past the vertex cap, not drawn
};

// ---- tints -------------------------------------------------------------------
// The entity pass multiplies its instance colour into the albedo, so a tint is a
// per-channel factor. active == false means "leave the instance alone", which is
// what keeps an unaffected frame byte-identical.
struct FxTint {
  bool active = false;
  float rgb[3] = {1.0f, 1.0f, 1.0f};
};

FxTint uberBodyTint(const FxPlayer& player, std::int32_t tick);
FxTint critWeaponTint(const FxPlayer& player, std::int32_t tick);

// First-person overlay colours (rgb + strength in .w) for the arms and for the
// weapon body. Both {0,0,0,0} when nothing applies. Returns true when either is set.
bool firstPersonFxOverlay(const FxPlayer& player, std::int32_t tick, float arms[4], float weapon[4]);

}  // namespace tf2::native
