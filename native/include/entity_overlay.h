#pragma once

// ============================================================================
// THE OVERLAY CONTRACT -- shared by T52 (team-colored x-ray player outlines)
// and T53 (projectile trajectory overlay).  DRAFT v1, 2026-10-22.
//
// WHY THIS FILE EXISTS BEFORE EITHER FEATURE DOES
//   T52 and T53 both draw "something about entities" over a world frame they
//   must not change. They were allowed to be prototyped in parallel only if
//   they agree on one ownership and tick-sampling API first
//   (NEXT-TASKS-2026-10-21.md, "Product-feature execution and parallelism"
//   item 3). Two independently invented overlays would each look correct and
//   together would double-sample every entity, both write the same depth
//   state, and both claim the same pass slot -- and the second one to land
//   would silently change the first one's pixels. So the split is fixed here,
//   in one header that neither task owns alone.
//
// WHAT IS SHARED, AND WHAT IS NOT
//   SHARED (this file): the pass slot and its order, team classification, the
//   sampling policy and its cost counters, the per-tick entity sample, the
//   draw-list shape both features submit, the toggle/linger state machine, and
//   the reading the renderer publishes back.
//   T52 owns: which entities are players and their team/pose at each sampled
//   tick (sampleOverlayEntities, Players filter), and the silhouette primitives
//   it submits (buildOutlineDrawList).
//   T53 owns: projectile identity (sampleOverlayEntities, Projectiles filter),
//   trajectory vertex generation from the sampled positions, and the linger
//   policy for its own trails.
//   The RENDERER owns exactly one overlay pass. It does not know what a
//   "player" or a "projectile" is; it receives an OverlayDrawList and draws it
//   in the slot below.
//
// THE ONE RULE BOTH FEATURES ARE HELD TO
//   The overlay pass must not change world geometry or ordinary visibility.
//   Concretely, and checkably: with the overlay disabled the frame is
//   byte-identical to a build that has no overlay code; with it enabled the
//   only pixels that differ are pixels the overlay drew, and the world/entity
//   depth buffer is unchanged (the pass writes no depth). The renderer enforces
//   that mechanically: the pass disables depth writes, takes the depth
//   comparison from OverlayDepthMode, and restores every state it touches
//   before returning.
//
// NO D3D TYPES IN THIS HEADER. It is deliberately free of <d3d11.h> so that a
// probe can build a sample, a draw list and a reading on a machine with no
// device: the identity/team/pose assertions must be measurable without a GPU,
// and a contract whose only consumer can be the renderer cannot be tested.

#include "demo_header.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace tf2::native {

// ---------------------------------------------------------------------------
// 1. THE PASS ORDER
//
// The overlay owns ONE slot. It is numbered rather than "drawn somewhere after
// the entity pass", so "the overlay drew before the weapon" is a reading the
// renderer reports (OverlayPassReading::stageOrdinal and draw()) instead of a
// claim about the order of two statements in a 5000-line function.
//
//   1 world        the map
//   2 water        the water surface, over the world's triangles
//   3 entityMarkers  fallback cross markers (no mesh resolved)
//   4 entityModels  players, props, weapons -- the meshes the silhouette reuses
//   5 projectileTracks  the existing temp-entity track pass
//   6 overlay      <<< THIS SLOT: T52 silhouettes and T53 trails
//   7 viewModel    first person; clears depth, so it must stay last of the
//                  scene passes or a weapon would be drawn under a silhouette
//   8 hud
//
// WHY BEFORE THE VIEWMODEL AND NOT AFTER IT. The ViewModel pass CLEARS the
// device depth buffer and draws in its own space (native_renderer.cpp, the
// note above the captureDepthBuffer(false) call). An overlay drawn after it
// would either have to re-test depth against cleared 1.0 (so it would be
// visible through the weapon, which reads as a bug) or clear depth again. Both
// are worse than drawing the overlay while the scene's own depth is still the
// one the frame was composed with.
// ---------------------------------------------------------------------------
enum class OverlayStage : std::uint8_t {
  World = 1,
  Water = 2,
  EntityMarkers = 3,
  EntityModels = 4,
  ProjectileTracks = 5,
  Overlay = 6,
  ViewModel = 7,
  Hud = 8,
};

const char* overlayStageName(OverlayStage stage);
constexpr std::uint8_t kOverlayStageOrdinal = static_cast<std::uint8_t>(OverlayStage::Overlay);

// ---------------------------------------------------------------------------
// 2. TEAM CLASSIFICATION
//
// TF2 writes team ids on the wire as m_iTeamNum: 0 unassigned, 1 spectator,
// 2 RED, 3 BLU, 4 (rarely) "other". Only 2 and 3 are teams the product may
// color; everything else is Unknown and must fall back to a colour that is
// neither red nor blue, so "the mapping did not fire" can never be mistaken
// for "this player is BLU".
//
// hasTeam == false and hasTeam == true with an out-of-range value are
// deliberately ONE answer (Unknown) and two different reasons on the sample
// (hasTeam plus teamRaw): the first is a snapshot that has not carried the
// slot yet, the second is a value the mapping refuses. A gate that needs them
// apart reads both fields.
// ---------------------------------------------------------------------------
enum class OverlayTeam : std::uint8_t { Unknown = 0, Red = 1, Blue = 2 };

const char* overlayTeamName(OverlayTeam team);
constexpr std::int64_t kOverlayTeamRedRaw = 2;
constexpr std::int64_t kOverlayTeamBlueRaw = 3;
OverlayTeam classifyOverlayTeam(bool hasTeam, std::int64_t teamRaw);

// Straight-alpha colour, 0..1, the same layout the renderer's other vertex
// structures use. Unknown is a neutral grey on purpose: red-ish or blue-ish
// fallback colours would make the unknown-team case pass a red/blue pixel
// assertion, which is the exact confusion this enum exists to prevent.
struct OverlayColor {
  float rgba[4] = {1.0f, 1.0f, 1.0f, 1.0f};
};
OverlayColor overlayTeamColor(OverlayTeam team);

// The band the silhouette is expanded by, in NORMALISED WORLD-BOX units (the
// renderer's world buffer is a unit box; one world unit is about
// 1/worldHorizontalScale_ map units -- 20 map units on the reference map).
// It is a named constant so a gate can predict where the outline's outer edge
// lands instead of searching the frame for "wherever it changed".
constexpr float kOverlaySilhouetteExpand = 0.004f;

// ---------------------------------------------------------------------------
// 3. THE TICK SAMPLING POLICY
//
// The demo is sampled at a fixed stride, not "whenever the renderer asks".
// Two reasons: the product must stay cheap on a 30-minute recording (a
// per-frame entity walk is the cost T38 measured and would undo), and a
// stride is a NUMBER a gate can hold a reading to, while "sampled often
// enough" is not.
//
// The stride is applied to the DEMO tick, not to the frame counter: the same
// tick must always be sampled or not sampled regardless of how many frames
// the renderer drew, or two runs of the same capture would sample different
// ticks and the frame comparison would be comparing two cameras.
//
// TIERS (the performance dial both features expose):
//   EveryTick         stride 1  -- the product default; every demo tick whose
//                                  snapshot the playback loop resolves anyway.
//   EveryOtherTick    stride 2  -- half the entity walks, ~33 Hz at 66.6 tick.
//   EveryFourthTick   stride 4  -- a quarter; trails visibly step at this tier
//                                  and the contract says so rather than hiding it.
// ---------------------------------------------------------------------------
enum class OverlaySampleTier : std::uint8_t {
  EveryTick = 0,
  EveryOtherTick = 1,
  EveryFourthTick = 2,
};

int overlaySampleStride(OverlaySampleTier tier);
bool overlayTickSampled(OverlaySampleTier tier, std::int32_t tick);
const char* overlaySampleTierName(OverlaySampleTier tier);

// T53's default linger: about 0.5 s of demo time at TF2's 66.66 tick rate,
// rounded to a whole number of ticks. T52 does not linger at all (a player
// that despawns must lose its silhouette on the next sampled tick, or the
// overlay would be drawing an entity the snapshot no longer holds), so the
// field is shared but T52 pins it to 0 and asserts that it is 0.
constexpr std::int32_t kOverlayDefaultLingerTicks = 33;

struct OverlaySamplePolicy {
  OverlaySampleTier tier = OverlaySampleTier::EveryTick;
  // 0 = no linger: the draw list describes this tick and nothing else.
  std::int32_t lingerTicks = 0;
};

// What one sampling pass cost, counted rather than timed, because a wall-clock
// reading on this host moves with every other process on it (T38's finding).
// entitiesInspected is the number of EntityStates the sampler looked at and
// propertiesRead the number of property slots it matched -- the same two
// quantities ResolverStats reports, so the two can be compared.
struct OverlaySampleCost {
  std::size_t ticksRequested = 0;
  std::size_t ticksSampled = 0;
  std::size_t entitiesInspected = 0;
  std::size_t propertiesRead = 0;
  std::size_t entitiesDropped = 0;
};

// ---------------------------------------------------------------------------
// 4. THE PER-TICK ENTITY SAMPLE
//
// One row per entity the filter selected from ONE snapshot. This is the
// shared half of "prove identity, team and pose at each sampled tick": the
// probe and the product call this same function, so the identity a gate
// asserts is the identity the draw list was built from, not a second opinion.
//
// present is false only for a row the caller explicitly keeps as a negative
// case (a despawned entity); a normal snapshot produces rows only for entities
// that exist. Filters:
//   Players      classes whose name contains "Player" (CWorld.m_iClass is then
//                read for the pose; a player with no m_iClass still appears,
//                with hasPlayerClass = false, because "no class" is a real
//                state after a spawn and must not hide the entity).
//   Projectiles  classes whose name names a projectile (CTFProjectile_*,
//                CBaseGrenade, ...). T53 owns the list; this header only fixes
//                the filter's shape so both features sample the same way.
// ---------------------------------------------------------------------------
struct OverlayEntitySample {
  bool present = false;
  std::uint16_t entityIndex = 0;
  std::int32_t classId = -1;
  std::string className;
  bool isPlayer = false;
  // Pose. hasOrigin is the same reading EntityModelResolver::extractTransform
  // produces (prefer-fresh-slot rule included), because a silhouette drawn at a
  // different origin from the mesh it outlines is the defect this field exists
  // to catch.
  bool hasOrigin = false;
  float origin[3] = {0.0f, 0.0f, 0.0f};
  bool hasAngles = false;
  float angles[3] = {0.0f, 0.0f, 0.0f};
  // Team, kept as both the wire value and the mapping's answer.
  bool hasTeam = false;
  std::int64_t teamRaw = 0;
  OverlayTeam team = OverlayTeam::Unknown;
  // Player class, when the filter is Players. 1..9 are TF2's classes; the
  // value is reported even when out of that range so "the slot was written"
  // and "the slot holds a class" stay separate readings.
  bool hasPlayerClass = false;
  std::int64_t playerClass = 0;
  // The model the entity's own m_nModelIndex resolves to, when modelprecache
  // knows it. Empty means "this entity names no model", which is not an error:
  // a player between spawn and its first network update carries none.
  std::string modelPath;
  // Why a row is absent or partial, as a stable string for the trace.
  std::string reason;
};

enum class OverlayEntityFilter : std::uint8_t {
  Players = 0,
  Projectiles = 1,
};

struct OverlaySampleStats {
  std::size_t entitiesSeen = 0;
  std::size_t candidatesSeen = 0;
  std::size_t sampled = 0;
  std::size_t withOrigin = 0;
  std::size_t withAngles = 0;
  std::size_t teamKnown = 0;
  std::size_t teamUnknown = 0;
  std::size_t playerClassKnown = 0;
  std::size_t viewModelsSkipped = 0;
  std::size_t modelPathResolved = 0;
};

std::vector<OverlayEntitySample> sampleOverlayEntities(
  const std::vector<EntityState>& statesByIndex,
  const std::vector<ServerClassSchema>& classSchemas,
  const std::unordered_map<std::uint16_t, std::string>& modelPrecache,
  OverlayEntityFilter filter,
  OverlaySampleStats* stats = nullptr);

// ---------------------------------------------------------------------------
// 5. THE DRAW LIST
//
// What a feature submits. The two features submit different primitives into
// the SAME list, and the renderer draws them in one pass, so neither can
// change the other's pass order or depth state.
//
// THE SILHOUETTE PRIMITIVE IS A SHELL, NOT A HOLLOW RING. It is the entity's
// own mesh, skinned with the same matrices the entity pass used and expanded
// along its normals by kOverlaySilhouetteExpand, drawn in the team colour with
// depth writes off. A hollow ring needs a stencil pre-pass and produces a band
// a few pixels wide at the distances these recordings place players at; the
// filled silhouette is what makes "this pixel belongs to that entity, and that
// entity is RED" measurable at all. The expansion is what gives it its outline
// edge: the model's own pixels are covered by the shell's interior, and the
// margin between the model and the shell's edge is the outline band.
//
// A submitted item whose cacheKey is not an uploaded mesh is REJECTED and
// counted (rejectedNoMesh), never drawn as a fallback quad: a fallback would
// put a team-coloured square where a player is not, which is the same class of
// error as drawing a silhouette for an entity that has despawned.
// ---------------------------------------------------------------------------
enum class OverlayPrimitiveKind : std::uint8_t {
  Silhouette = 0,  // T52: a player's shell in its team colour
  Line = 1,        // T53: two vertices, start..end
  Marker = 2,      // T53: a camera-facing cross at a point
};

const char* overlayPrimitiveKindName(OverlayPrimitiveKind kind);

// The depth behaviour is a property of the DRAW LIST, not of an item: mixing
// the two inside one list would let a mutation hide one primitive's depth
// state behind the other's. XRay is the product behaviour (draw through
// walls); DepthTested is the control the acceptance's mutation needs, and the
// renderer reports which one it used so a frame cannot be read as the wrong
// one.
enum class OverlayDepthMode : std::uint8_t {
  XRay = 0,        // depth comparison always passes; no depth write
  DepthTested = 1, // ordinary LESS_EQUAL; no depth write
};

const char* overlayDepthModeName(OverlayDepthMode mode);

struct OverlayDrawItem {
  OverlayPrimitiveKind kind = OverlayPrimitiveKind::Silhouette;
  // Entity identity the item was built from. Reported by the renderer on the
  // reading line so a pixel can be traced back to the entity that owns it.
  std::uint16_t entityIndex = 0;
  bool isPlayer = false;
  OverlayTeam team = OverlayTeam::Unknown;
  OverlayColor color{};
  // Silhouette: the uploaded mesh to draw and where to put it. The skinning
  // fields are copied from the same instance the entity pass drew, so the
  // outline follows the pose instead of the bind pose.
  std::string cacheKey;
  float origin[3] = {0.0f, 0.0f, 0.0f};
  bool hasAngles = false;
  float angles[3] = {0.0f, 0.0f, 0.0f};
  bool skinned = false;
  std::uint32_t boneOffset = 0;
  std::uint32_t boneCount = 0;
  // Line / Marker endpoints, world (map) units, converted by the renderer with
  // the same map-to-box transform it uses for every other world vertex.
  float start[3] = {0.0f, 0.0f, 0.0f};
  float end[3] = {0.0f, 0.0f, 0.0f};
};

struct OverlayDrawList {
  // Read by the renderer, never inferred: an empty list with enabled == true is
  // "the overlay is on and there is nothing to draw", which is a different
  // reading from "the overlay is off".
  bool enabled = false;
  OverlayDepthMode depth = OverlayDepthMode::XRay;
  std::vector<OverlayDrawItem> items;
  // Counters filled by the builder, before the renderer rejects anything.
  std::size_t submittedByTeam[3] = {0, 0, 0};  // indexed by OverlayTeam
  // Why an entity that was sampled is not in items.
  std::size_t rejectedDisabled = 0;
  std::size_t rejectedNoMesh = 0;
  std::size_t rejectedNoOrigin = 0;
  std::size_t rejectedNotPlayer = 0;
  std::size_t rejectedLingered = 0;
};

// T52's builder: sampled players -> silhouette items, coloured by team. Pure.
// knownMeshes is the set of cache keys the renderer has uploaded; an item
// whose key is absent is counted in rejectedNoMesh and not submitted.
// enabled == false submits nothing and counts every sampled player in
// rejectedDisabled -- so the toggle's effect is a count and not an absence.
OverlayDrawList buildOutlineDrawList(
  const std::vector<OverlayEntitySample>& samples,
  const std::vector<std::string>& knownMeshes,
  bool enabled,
  OverlayDepthMode depth);

// ---------------------------------------------------------------------------
// 6. THE TOGGLE STATE
//
// One state machine for the whole overlay slot, so T52's key and T53's key
// cannot disagree about whether "the overlay" is on. Both features' items
// live in one pass; two independent enable flags would let a frame be drawn
// with one feature's items and the other's pass state.
//
// presses counts edge presses and stateChanges counts actual changes: a held
// key that repeats must not toggle repeatedly (the reason main.cpp's
// WM_KEYDOWN arm already filters the repeat bit), and "the counter moved" is
// how the gate sees a key that was pressed and ignored.
// ---------------------------------------------------------------------------
class OverlayToggleState {
public:
  bool enabled() const { return enabled_; }
  // Returns the new state. A call whose state did not change still counts a
  // press, so "pressed twice" and "pressed once" are different readings.
  bool set(bool value);
  bool toggle();
  bool on() { return set(true); }
  bool off() { return set(false); }
  std::uint32_t presses() const { return presses_; }
  std::uint32_t stateChanges() const { return stateChanges_; }
  // The state written by the command line, kept apart from enabled_: a run
  // that started with --outline on and later toggled off must be readable as
  // "started on, toggled", not as "started off".
  void setStartupDefault(bool value) { startupDefault_ = value; enabled_ = value; }
  bool startupDefault() const { return startupDefault_; }

private:
  bool enabled_ = false;
  bool startupDefault_ = false;
  std::uint32_t presses_ = 0;
  std::uint32_t stateChanges_ = 0;
};

// ---------------------------------------------------------------------------
// 7. WHAT THE RENDERER PUBLISHES BACK
//
// The pass's own account of what it did. A gate must be able to tell these
// four apart without looking at a pixel:
//   * disabled and drew nothing        (enabled=false, drawn=0)
//   * enabled, submitted nothing       (enabled=true,  submitted=0)
//   * enabled, submitted, all rejected (enabled=true,  submitted>0, drawn=0)
//   * enabled, drew                    (enabled=true,  drawn>0)
// stageOrdinal is the pass's position in draw()'s own counter, so "the overlay
// drew after the world" is a number, not an ordering of statements.
// ---------------------------------------------------------------------------
struct OverlayPassReading {
  bool enabled = false;
  OverlayDepthMode depth = OverlayDepthMode::XRay;
  bool drewThisFrame = false;
  std::size_t submitted = 0;
  std::size_t drawn = 0;
  std::size_t silhouettes = 0;
  std::size_t lines = 0;
  std::size_t markers = 0;
  std::size_t rejectedNoMesh = 0;
  std::size_t rejectedNoOrigin = 0;
  std::size_t vertexCount = 0;
  std::size_t drawCalls = 0;
  std::size_t stageOrdinal = 0;
  float expandWorldUnits = kOverlaySilhouetteExpand;
  // The world-space (normalised box) rectangle the drawn silhouettes cover,
  // accumulated from the drawn items. Empty when nothing drew. This is what
  // lets a pixel assertion be about "the region this entity covers" rather
  // than about the whole frame.
  bool hasBounds = false;
  float boundsMin[3] = {0.0f, 0.0f, 0.0f};
  float boundsMax[3] = {0.0f, 0.0f, 0.0f};
};

} // namespace tf2::native
