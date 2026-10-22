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
  // True when modelPath came from the player-class fallback instead of
  // m_nModelIndex. THE RECORDING THIS FEATURE WAS BUILT ON HAS NO PLAYER
  // m_nModelIndex AT ALL: 21 of 21 sampled players resolved identity, team,
  // origin and pose and none resolved a model, so every silhouette was rejected
  // as "no uploaded mesh" and the pass drew nothing in 778 of 778 frames. The
  // world pass never had that problem because EntityModelResolver falls back to
  // the class model; the outline has to answer the same way, and the flag is what
  // tells the mesh join which cache key to look for.
  bool modelPathFromClass = false;
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

// WHICH CLASSES COUNT IS THE CALLER'S DECISION, NOT A RULE HIDDEN HERE.
// T53 measured the name rule on the reference recording's real server class
// table (355 classes) and it is wrong in BOTH directions:
//   7 false positives -- CTFRocketLauncher, CTFGrenadeLauncher,
//       CTFPipebombLauncher, CTFFlameThrower, CTFJar, CTFBottle,
//       CTFPowerupBottle. Every one is a WEAPON or a held item that carries
//       DT_BaseEntity.m_vecOrigin and sits at (0,0,0) while held, so a keyword
//       filter draws a trail from the world origin for each of them.
//   1 false negative -- CTFGrenadePipebombProjectile. It contains no
//       "Projectile" at all and is the second most common real projectile on
//       that demo (774 spawns against 1228 CTFProjectile_Rocket).
// A rule inside the shared sampler can only be wrong in one direction or the
// other, so the SET is an input. A null classSet keeps the built-in default
// rule, which is what T52 uses (TF2 has exactly one player class, CTFPlayer,
// and its substring rule matches it); T53 passes its measured list.
struct OverlayClassSet {
  std::vector<std::string> classNames;  // exact class-name matches
};

std::vector<OverlayEntitySample> sampleOverlayEntities(
  const std::vector<EntityState>& statesByIndex,
  const std::vector<ServerClassSchema>& classSchemas,
  const std::unordered_map<std::uint16_t, std::string>& modelPrecache,
  OverlayEntityFilter filter,
  const OverlayClassSet* classSet = nullptr,
  OverlaySampleStats* stats = nullptr);

// ---------------------------------------------------------------------------
// 4b. HOW A TICK IS RESOLVED, AND HOW HONEST IT IS ABOUT RESOLUTION
//
// T53 raised the question this section answers: per-tick projectile positions
// exist on the wire (each CTFProjectile_* / grenade entity gets an m_vecOrigin
// update every packet while alive), but the entity history does not retain
// every packet -- queryEntitySnapshotAtOrBeforeTick answers most ticks from the
// newest retained CHECKPOINT at-or-before them, and on the reference recording
// the worst observed staleness is 1057 ticks (16 s at 66.66 tick). A consumer
// that read such a snapshot as "the state at tick T" would draw a trail of
// eight frozen positions and call it a trajectory.
//
// THE CONTRACT'S ANSWER IS NOT A SECOND QUERY. resolveOverlaySnapshot calls
// queryEntitySnapshotAtOrBeforeTick -- the one implementation -- and adds only
// the honest reading a consumer has to act on: which tick the answer really
// came from, how stale that is, and which of the three fidelities applies.
// A feature decides what to do about a stale answer; the accessor never hides
// one.
//
//   ExactTick           the live window replayed the tick itself (staleness 0)
//   CheckpointFallback  an archived checkpoint at-or-before the tick; the
//                       answer is a real snapshot, but it is NOT this tick
//   Unavailable         no history for this tick (before the window, a gap, or
//                       no history at all) -- states is empty and reason says
//                       which one
//
// WHO WIDENS THE RETAINED WINDOW. Neither task does it inside the overlay.
// EntityHistoryLimits is a demo_header policy every consumer shares (the
// instance cache, the trace, the probes), and changing it to fit one feature
// would move every reading taken against it. T52 does not need it: a silhouette
// is a statement about the snapshot the frame was drawn from, and a stale
// snapshot is reported as stale. T53's per-tick trail does need it and owns
// that change in demo_header, with the fidelity field below as the reading that
// says whether the widening worked -- until then T53's fidelity will read
// CheckpointFallback on most ticks and its acceptance must say so.
// ---------------------------------------------------------------------------
enum class OverlaySnapshotFidelity : std::uint8_t {
  Unavailable = 0,
  CheckpointFallback = 1,
  ExactTick = 2,
};

const char* overlaySnapshotFidelityName(OverlaySnapshotFidelity fidelity);

// The tick domain is the SERVER tick (entity history is keyed by it;
// sceneTick = firstPacketTick + playback.tick is what the product passes in).
struct OverlaySnapshotResolution {
  std::int32_t requestedTick = 0;
  std::int32_t resolvedTick = 0;
  std::int32_t stalenessTicks = 0;
  OverlaySnapshotFidelity fidelity = OverlaySnapshotFidelity::Unavailable;
  // The demo_header status this came from, kept so a caller can separate
  // "before the retained window" from "a gap in it" without re-querying.
  // Printed as a name by the trace; never compared as a number.
  std::string reason;
};

// Resolves one tick into states. Returns the resolution; states is empty only
// when fidelity is Unavailable.
OverlaySnapshotResolution resolveOverlaySnapshot(
  const DemoNetworkSummary& summary,
  std::int32_t tick,
  std::vector<EntityState>& states);

// ---------------------------------------------------------------------------
// 4c. LIFECYCLE -- "DISAPPEARED" IS A TRANSITION, NOT A MISSING SAMPLE
//
// T53's acceptance needs "the projectile vanished" and T52's needs "the player
// despawned", and a bare sample list cannot tell either from "this tick was
// never sampled". The tracker diffs consecutive sample sets by entity index and
// returns one row per entity whose presence changed. It is a pure value the
// probe drives, so the despawn case and the no-player case are testable without
// a demo that happens to contain a despawn at the right tick.
//
// The tracker is fed only the ticks the policy actually sampled
// (overlayTickSampled), so a stride of 4 reports four ticks of change at once --
// which is the truth about what was observed, and the reason
// ticksSinceLastSample is on the row.
// ---------------------------------------------------------------------------
enum class OverlayLifecycleTransition : std::uint8_t {
  Appeared = 0,   // present now, not in the previous sampled set
  Persisted = 1,  // present in both (emitted only when includePersisted)
  Vanished = 2,   // present before, absent now: the despawn case
};

const char* overlayLifecycleTransitionName(OverlayLifecycleTransition transition);

struct OverlayLifecycleEvent {
  OverlayLifecycleTransition transition = OverlayLifecycleTransition::Appeared;
  std::uint16_t entityIndex = 0;
  std::int32_t classId = -1;
  std::string className;
  bool isPlayer = false;
  OverlayTeam team = OverlayTeam::Unknown;
  std::int32_t tick = 0;
  std::int32_t ticksSinceLastSample = 0;
};

struct OverlayLifecycleStats {
  std::size_t appeared = 0;
  std::size_t persisted = 0;
  std::size_t vanished = 0;
  std::size_t ticksFed = 0;
};

class OverlayEntityTracker {
public:
  // Feeds the samples of one sampled tick. Returns the transitions and folds
  // the same counts into stats() incrementally. A tick fed twice is refused
  // (returns empty, increments duplicateTicks) rather than applied twice: the
  // tracker's answer must not depend on how many times a frame asked.
  std::vector<OverlayLifecycleEvent> observe(
    std::int32_t tick,
    const std::vector<OverlayEntitySample>& samples,
    bool includePersisted = false);
  void reset();
  const OverlayLifecycleStats& stats() const { return stats_; }
  std::size_t duplicateTicks() const { return duplicates_; }
  bool hasSample() const { return haveTick_; }

private:
  bool haveTick_ = false;
  std::int32_t lastTick_ = 0;
  // entityIndex -> the row, so a Vanished event can still name the class and
  // team the entity had on its last observed tick.
  std::unordered_map<std::uint16_t, OverlayEntitySample> previous_;
  OverlayLifecycleStats stats_{};
  std::size_t duplicates_ = 0;
};


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

// Where a silhouette's MESH came from, printed per draw and counted per list.
// None means "no mesh answered", which the builder reports as rejectedNoMesh.
enum class OverlayPoseSource : std::uint8_t {
  None = 0,
  World = 1, // the entity pass uploaded and drew this mesh this frame
  Bind = 2,  // the overlay uploaded it for itself; no bones, bind pose
};

const char* overlayPoseSourceName(OverlayPoseSource source);

struct OverlayDrawItem {
  OverlayPrimitiveKind kind = OverlayPrimitiveKind::Silhouette;
  // Entity identity the item was built from. Reported by the renderer on the
  // reading line so a pixel can be traced back to the entity that owns it.
  std::uint16_t entityIndex = 0;
  bool isPlayer = false;
  OverlayTeam team = OverlayTeam::Unknown;
  OverlayColor color{};
  // Silhouette: the uploaded mesh to draw and where to put it.
  //
  // WHERE THE POSE COMES FROM, recorded per item rather than asserted once for
  // the whole feature (2026-10-22). Two sources exist and they are not
  // equivalent:
  //
  //   World  the mesh the entity pass uploaded FOR THIS FRAME, joined through
  //          the instance the pass drew, so the skinning fields below are the
  //          bones of the pose on screen.
  //   Bind   a mesh the overlay uploaded for itself, as bind-pose triangles.
  //          There is nothing to skin: the outline stands in a T-pose-ish
  //          bind pose at the player origin.
  //
  // Measured on the reference recording, the second case is the ONLY one that
  // happens: the entity pass prepares 59 models (`gpu: models done 59
  // uploaded=59`) and draws 96 instances plus 32 markers, and the string
  // "models/player" never appears in a 33-second trace -- players have no body
  // mesh in the world pass at all. The join therefore finds no world mesh and
  // the overlay falls back to the class model. When that upstream gap is fixed
  // the sources change by themselves and the counters below go to 0; that is
  // the reason this is a READING and not a comment.
  OverlayPoseSource poseSource = OverlayPoseSource::None;
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

// ONE LIST PER DEPTH MODE, NOT ONE LIST PER FEATURE.
// T53 needs a depth-tested trail (so a path behind a wall reads as 3D) and an
// x-ray marker (so a live projectile can still be found behind cover) in the
// SAME frame. The depth mode is a property of the list, so the renderer takes
// one list per mode -- setOverlayDrawList(list) is called once for XRay and
// once for DepthTested -- and the pass issues at most two state changes.
// Keeping the mode on the list rather than on the item is what stops a
// mutation from hiding one primitive's depth state behind another's, which is
// the whole reason it is not on OverlayDrawItem.
struct OverlayDrawList {
  // Read by the renderer, never inferred: an empty list with enabled == true is
  // "the overlay is on and there is nothing to draw", which is a different
  // reading from "the overlay is off".
  bool enabled = false;
  OverlayDepthMode depth = OverlayDepthMode::XRay;
  std::vector<OverlayDrawItem> items;
  // Counters filled by the builder, before the renderer rejects anything.
  std::size_t submittedByTeam[3] = {0, 0, 0};  // indexed by OverlayTeam
  // Where the submitted silhouettes got their mesh from. These two are the
  // retirement condition for the overlay's own mesh upload: when the world pass
  // starts drawing player bodies, poseSourceBind goes to 0 with no code change,
  // and a gate that asserted "bind > 0" would go red on the day it got better.
  std::size_t poseSourceWorld = 0;
  std::size_t poseSourceBind = 0;
  // Why an entity that was sampled is not in items.
  std::size_t rejectedDisabled = 0;
  std::size_t rejectedNoMesh = 0;
  std::size_t rejectedNoOrigin = 0;
  std::size_t rejectedNotPlayer = 0;
  std::size_t rejectedLingered = 0;
};

// T52's builder: sampled players -> silhouette items, coloured by team. Pure.
//
// THE MESH SOURCE IS ORDERED, and the order is the whole of requirement (1)
// from the 2026-10-22 review: the world pass's mesh for THIS FRAME is asked for
// first, and the overlay's own bind-pose upload is the fallback. It is not the
// other way round, and it is not "whichever key matches" -- a silhouette that
// preferred its own bind mesh would silently stop following the animation on
// the day the world pass started drawing players.
//
//   worldMeshes     cache keys the entity pass uploaded and DREW this frame
//   fallbackMeshes  cache keys the overlay uploaded for itself (bind pose)
//
// Both sets are spelled in the two ways the same fact gets written: the bare
// model path and "<path>#class-fallback", because EntityModelResolver keys the
// class fallback that way (entity_model.cpp). Whichever set answers, the item
// carries THE KEY THAT MATCHED -- the renderer looks the mesh up by it.
//
// An item whose key is in neither set is counted in rejectedNoMesh and not
// submitted; enabled == false submits nothing and counts every sampled player in
// rejectedDisabled, so the toggle's effect is a count and not an absence.
OverlayDrawList buildOutlineDrawList(
  const std::vector<OverlayEntitySample>& samples,
  const std::vector<std::string>& worldMeshes,
  const std::vector<std::string>& fallbackMeshes,
  bool enabled,
  OverlayDepthMode depth);

// ---------------------------------------------------------------------------
// 6. THE TOGGLE STATE -- ONE SLOT, ONE OWNER
//
// T52's key and T53's key must not be able to disagree about whether "the
// overlay" is on, and they must not be able to disturb each other either.
// Both halves of that sentence are mechanical here:
//
//   * there is ONE boolean, so two features cannot hold two opinions about it;
//   * the boolean has exactly ONE OWNER at a time. Acquiring is the claim, and
//     a call from a feature that does not own the slot is REFUSED and counted
//     (refusedForeignPresses) rather than applied. So "T53 turned the overlay
//     off while T52 was driving it" is an observable refusal, not a silent
//     change of what T52's frame means.
//
// WHY OWNERSHIP AT ALL. The two features draw into one pass, so a frame's
// overlay is one thing. If both could write the flag, the trace would show a
// toggle count that neither feature can account for, and a gate that pressed
// F9 could not attribute the resulting frame to the feature it was testing.
// Ownership makes "who is driving" a field (owner), not an assumption.
//
// presses counts every call, stateChanges only the calls that moved the state,
// so "pressed twice" and "pressed once and was refused" are different
// readings. startupDefault keeps "started on, then toggled off" readable as
// exactly that.
// ---------------------------------------------------------------------------
enum class OverlaySlotOwner : std::uint8_t {
  None = 0,
  Outline = 1,     // T52 controls the slot
  Trajectory = 2,  // T53 controls the slot
};

const char* overlaySlotOwnerName(OverlaySlotOwner owner);

class OverlayToggleState {
public:
  // Claims the slot for `owner`. Succeeds when the slot is free or already
  // owned by that same feature; refuses (returns false, counts the refusal)
  // when another feature holds it. The renderer's overlay pass reads enabled()
  // and never acquires.
  bool acquire(OverlaySlotOwner owner);
  // Gives the slot up. Only the owner may release it; a foreign release is
  // refused and counted like a foreign press.
  bool release(OverlaySlotOwner owner);

  OverlaySlotOwner owner() const { return owner_; }
  bool enabled() const { return enabled_; }

  // The two setters both REQUIRE ownership. A call from a non-owner returns
  // the current state unchanged and increments refusedForeignPresses, so a
  // gate can show the interference did not land.
  bool set(OverlaySlotOwner who, bool value);
  bool toggle(OverlaySlotOwner who);
  bool on(OverlaySlotOwner who) { return set(who, true); }
  bool off(OverlaySlotOwner who) { return set(who, false); }

  std::uint32_t presses() const { return presses_; }
  std::uint32_t stateChanges() const { return stateChanges_; }
  std::uint32_t refusedForeignPresses() const { return refusedForeign_; }
  std::uint32_t acquisitions() const { return acquisitions_; }
  std::uint32_t releases() const { return releases_; }

  // The state written by the command line. Kept apart from enabled_ so a run
  // that started with --outline on and later toggled off reads as "started on,
  // toggled", not as "started off". Does not require ownership: it is applied
  // before any feature exists.
  void setStartupDefault(bool value) { startupDefault_ = value; enabled_ = value; }
  bool startupDefault() const { return startupDefault_; }

private:
  OverlaySlotOwner owner_ = OverlaySlotOwner::None;
  bool enabled_ = false;
  bool startupDefault_ = false;
  std::uint32_t presses_ = 0;
  std::uint32_t stateChanges_ = 0;
  std::uint32_t refusedForeign_ = 0;
  std::uint32_t acquisitions_ = 0;
  std::uint32_t releases_ = 0;
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
// ONE ROW PER SILHOUETTE THE PASS ACTUALLY DREW, with the screen box it covers
// and the depth its own geometry projects to. This is what lets a gate ask
// "is THIS player's region red" instead of "does the frame contain red", and
// what lets the depth-tested control be judged on the pixels a wall occludes.
// The box is the projected AABB in pixels (top-left origin, as the BMP is
// read), clamped to the viewport; `depth` is the projected depth of the box's
// centre, 0..1, which is the number the depth buffer is compared against.
struct OverlayItemReading {
  std::uint16_t entityIndex = 0;
  OverlayTeam team = OverlayTeam::Unknown;
  OverlayDepthMode depthMode = OverlayDepthMode::XRay;
  bool onScreen = false;
  int x0 = 0;
  int y0 = 0;
  int x1 = 0;
  int y1 = 0;
  // The box the constant +/-20, +/-36 map-unit hull would have produced for the
  // same silhouette. Kept so one line carries both, which is the only way to
  // tell "the box was fixed" from "the assertion was relaxed".
  int legacyX0 = 0;
  int legacyY0 = 0;
  int legacyX1 = 0;
  int legacyY1 = 0;
  float depth = 0.0f;
};

struct OverlayPassReading {

  bool enabled = false;
  OverlayDepthMode depth = OverlayDepthMode::XRay;
  bool drewThisFrame = false;
  // The depth state the pass ACTUALLY configured for the list it drew, read
  // back from the state object it bound rather than repeated from the intent:
  // x-ray is depth test OFF and depth write OFF, the depth-tested control is
  // test ON and write OFF. Both halves are published because an "x-ray" list
  // that writes depth keeps the picture plausible while it changes what every
  // later pass sees, and no screenshot would show it.
  bool depthTestEnabled = false;
  bool depthWriteEnabled = false;
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
  std::vector<OverlayItemReading> items;
};

// ---------------------------------------------------------------------------
// 7b. THE RENDERER ENTRY POINT (answered for T53's question d)
//
// The overlay pass is ONE pass that draws ONE list. T52 and T53 do not each
// get a pass; they each contribute items through their own builder and the
// product submits the merged list once per frame:
//
//   renderer.setOverlayDrawList(const OverlayDrawList& list);
//   const OverlayPassReading& Renderer::overlayPassReading() const;
//
// setOverlayDrawList takes a copy: the renderer keeps the list across frames
// until it is replaced or cleared, so a product loop that only rebuilds on a
// sampled tick does not clear the overlay on the frames in between. Passing an
// empty list with enabled = false is the off state; passing an empty list with
// enabled = true is "on and nothing to draw", which the reading distinguishes.
//
// Line and Marker primitives (T53's) are built by the renderer into vertex
// buffers from `start`/`end` using the same map-to-box transform as every other
// world vertex, so a trail's line lands on the world it describes. They share
// the overlay pass's depth mode; the header keeps that mode per list on purpose
// (see OverlayDepthMode) and T53 said if that is wrong it has to say so before
// both implementations exist.

// What a caller merges: buildOutlineDrawList's items plus T53's. The merge is
// a plain append; the renderer re-derives every count from the list it was
// handed, so a feature cannot inflate another's reading.

// ---------------------------------------------------------------------------
// 8. SETTINGS AND KEY BINDINGS -- ONE PLACE, NOT TWO
//
// T53's question (c): the two features must not each add their own key to
// settings.ini and then race to write the file. The rule:
//
//   * settings.ini gains exactly TWO keys for the whole overlay slot:
//       [renderer] outline=0|1        (T52's on/off, the product default 0)
//       [renderer] outline_depth=0|1  (0 = XRay product, 1 = DepthTested control)
//     Written only by T52, through settings_store's existing PersistentSettings
//     rather than by a second ini writer. T53 reads none of them.
//   * T53's trajectory settings (linger ticks, sampling tier, player filter)
//     are T53's to add, under their own key names, in the same section. The
//     contract does not fix their names; it fixes that neither task writes the
//     other's keys.
//   * key bindings: T52 owns the outline key (F9, the first key past the F1..F8
//     camera-preset range), T53 owns its own, and both go through the single
//     OverlayToggleState-shaped path in main.cpp's WM_KEYDOWN arm with the
//     repeat-bit guard that arm already applies. Two keys, two states, one
//     pass -- a key that toggled the shared pass state from two places would
//     make "pressed F9" and "pressed F10" indistinguishable in the trace.


// ---------------------------------------------------------------------------
// 9. THE GATE'S ASSERTIONS, IN THE CONTRACT RATHER THAN IN A SCRIPT
//
// Each acceptance criterion has one predicate here, so the probe, the product
// trace and the gate cannot hold three slightly different opinions about what
// "the outline is red" or "the overlay drew in the right slot" means.
//
// (a) PASS ORDER. overlayPassOrderValid(overlayOrdinal, viewModelOrdinal) is the
//     whole assertion: the overlay must be drawn strictly BEFORE the ViewModel
//     pass. The product prints both ordinals, the probe asserts this predicate,
//     and the mutation that moves the overlay after the ViewModel pass makes it
//     false AND makes the weapon visibly outlined -- one reading, two symptoms.
//
// (b) TEAM COLOUR. overlayPixelMatchesTeam(rgb) is the pixel side: a red-team
//     pixel must be red-dominant by a margin the predicate states, a blue-team
//     pixel blue-dominant, and a neutral pixel must satisfy NEITHER. Swapping
//     the two colours therefore fails BOTH directions, and an unknown team that
//     fell back to red would fail the neutral test.
//
// (c) DEPTH AWARENESS -- what the depth-tested mutation has to show. The
//     assertion is NOT "some pixels changed"; it is: in the region where the
//     world's own depth buffer proves a player is occluded by world geometry,
//       XRay frame          -> that region contains team-coloured outline pixels
//       DepthTested frame   -> that region contains NONE, and is byte-identical
//                              to the same region of the overlay-off frame
//     The occluded region is not guessed: captureDepthBuffer(false) already
//     stores the world depth of the frame, so the probe reads the frame's own
//     depth at a pixel and asks whether the silhouette's projected depth is
//     behind it. A threshold on a pixel count would pass for a silhouette that
//     merely moved; this cannot.
//
// (d) NON-INTERFERENCE. With the overlay on, every pixel OUTSIDE the drawn
//     silhouette's bounds must be byte-identical to the overlay-off frame at
//     the same tick. That is the direct evidence for "does not change world
//     geometry or ordinary visibility", and it is why the pass publishes its
//     bounds at all.
//
// (e) TOGGLE OWNERSHIP. acquire/toggle from a foreign owner must be refused
//     (refusedForeignPresses moves, enabled() does not), so "two features each
//     toggling" is a measurable refusal instead of a race.
// ---------------------------------------------------------------------------

// (a) The pass must come before the ViewModel pass. False when either ordinal
// is unset (0), which is what a pass that never ran reports -- so a missing
// overlay cannot pass this by reading both as 0.
bool overlayPassOrderValid(std::size_t overlayOrdinal, std::size_t viewModelOrdinal);

// (b) The pixel predicate, in 8-bit channels as the BMP holds them.
// A red pixel: r >= green + 48 and r >= blue + 48 and r >= 96.
// A blue pixel: b >= red + 48 and b >= green + 48 and b >= 96.
// A neutral pixel: neither of the above.
// 48 is ~19% of full scale: well above the map's own warm albedo (whose
// measured red-minus-green on the reference frames stays under 30) and well
// below the team colours this contract defines, so the predicate separates
// them instead of splitting hairs.
bool overlayPixelMatchesTeam(std::uint8_t r, std::uint8_t g, std::uint8_t b, OverlayTeam team);
bool overlayPixelIsNeutral(std::uint8_t r, std::uint8_t g, std::uint8_t b);
constexpr int kOverlayTeamChannelMargin = 48;
constexpr int kOverlayTeamChannelFloor = 96;

} // namespace tf2::native
