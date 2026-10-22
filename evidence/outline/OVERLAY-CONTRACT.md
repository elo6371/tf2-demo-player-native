# Overlay contract (T52 / T53) -- one page

**Status:** DRAFT v1, 2026-10-22. Header: `native/include/entity_overlay.h`.
Owners: T52 (t52-outline) wrote the draft; T53 (t53-trajectory) must ack or
propose a change before either feature's implementation lands.

## Ownership

| Thing | Owner | Lives in |
|---|---|---|
| Team classification, sampling policy, entity sample, draw list, toggle, pass reading | **shared** | `native/include/entity_overlay.h` |
| Player filter, silhouette primitives | T52 | `buildOutlineDrawList` + T52's renderer call |
| Projectile filter, trajectory vertices, trail linger | T53 | T53's builder + the same renderer pass |
| The single overlay pass (depth state, buffers, restore) | **renderer** | `Renderer::draw()`, one `setOverlayDrawList` |

Nothing in the shared header includes `<d3d11.h>`, so a probe can build a
sample, a draw list and a pass reading with no GPU. That is what makes the
"identity + team + pose at every sampled tick" assertions measurable.

## Sampling API

```cpp
std::vector<OverlayEntitySample> sampleOverlayEntities(
    const std::vector<EntityState>&, const std::vector<ServerClassSchema>&,
    const std::unordered_map<std::uint16_t, std::string>& modelPrecache,
    OverlayEntityFilter, OverlaySampleStats* = nullptr);
```

* Stride is applied to the **demo tick**, not to the frame counter, so the same
  tick is sampled in every run of the same capture. Tiers: 1 / 2 / 4
  (`EveryTick` is the product default).
* Each row carries: entity index, class id + name, `hasOrigin`/`origin`,
  `hasAngles`/`angles`, `hasTeam`/`teamRaw`/`team`, `hasPlayerClass`/`playerClass`,
  resolved `modelPath`, and a `reason`.
* Pose comes from the **same** rule the entity mesh uses
  (`EntityModelResolver::extractTransform`, which prefers the fresher of the two
  origin slots). A silhouette at a different origin from its mesh is the defect
  that rule exists to prevent, so the contract forbids a second origin reader.
* Cost is counted, never timed: `OverlaySampleCost` is the same shape as
  `ResolverStats` so the two can be compared directly.

## Pass order

`world(1) → water(2) → entityMarkers(3) → entityModels(4) → projectileTracks(5)
→ **overlay(6)** → viewModel(7) → hud(8)`

The overlay is **before** the ViewModel pass because that pass clears the
device depth buffer and draws in its own space; an overlay after it would
either be visible through the weapon or have to clear depth again. The renderer
publishes `OverlayPassReading::stageOrdinal` so "the overlay drew before the
weapon" is a number, not a reading of the source.

The pass **must not change world geometry or ordinary visibility**:
depth writes off, comparison from `OverlayDepthMode` (`XRay` = always pass,
the product behaviour; `DepthTested` = ordinary `LESS_EQUAL`, the control the
"depth-tested-only" mutation needs), every state it touches restored before it
returns. With the overlay disabled nothing about the frame may move.

## Toggle and linger

* One `OverlayToggleState` for the whole slot, so T52's key and T53's key can
  never disagree about whether the overlay is on. `presses` counts edge
  presses, `stateChanges` counts real moves, `startupDefault` keeps
  "`--outline on` then toggled off" readable.
* **T52 pins `lingerTicks = 0`.** A despawned player must lose its silhouette on
  the next sampled tick; a lingering outline would be drawing an entity the
  snapshot no longer holds.
* T53 owns `kOverlayDefaultLingerTicks = 33` (~0.5 s at 66.66 tick) for trails.

## Performance tiers

| Tier | Stride | When |
|---|---|---|
| `EveryTick` | 1 | default; every tick the playback loop already resolves |
| `EveryOtherTick` | 2 | ~33 Hz at 66.66 tick |
| `EveryFourthTick` | 4 | trails visibly step; the contract says so instead of hiding it |

Reading back: `OverlayPassReading` separates four states a gate must not
confuse -- disabled/nothing drawn, enabled/nothing submitted, enabled/all
rejected, enabled/drew -- and carries the normalised-box bounds of what drew so
a pixel assertion can be about the entity's own region.

## What T53 needs to answer

1. Does `OverlayEntityFilter::Projectiles` cover the classes you need, or do you
   need a per-class predicate handed in?
2. Are ~33 ticks of linger at 66.66 tick the default you want, and is the
   per-item age carried on `OverlayDrawItem` or kept inside your builder?
3. Do you need a second depth mode (e.g. depth-tested trail + x-ray marker)?
   The contract keeps the mode per **list** on purpose; say so now if that is
   wrong, before both implementations exist.

## v2 (2026-10-22, after t53-trajectory's findings) -- what changed and why

T53 measured that per-tick projectile origins ARE on the wire and that the C++
entity history does NOT retain them: `queryEntitySnapshotAtOrBeforeTick` answers
most ticks from the newest retained checkpoint (worst staleness 1057 ticks on
the reference recording, median gap 822). Four answers went into the header
because a chat reply would have been contradicted by the code:

1. **`resolveOverlaySnapshot(summary, tick, states)`** -- the one shared accessor
   (question a). It calls the existing `queryEntitySnapshotAtOrBeforeTick` and
   adds the honest reading: `requestedTick`, `resolvedTick`, `stalenessTicks`,
   and `OverlaySnapshotFidelity::{ExactTick, CheckpointFallback, Unavailable}`.
   Tick domain is the **server tick** (`sceneTick = firstPacketTick + playback.tick`).
   It never hides a stale answer -- a consumer decides what to do with one.
2. **Retention is not the overlay's to widen** (question b). `EntityHistoryLimits`
   is a demo_header policy every consumer shares; changing it for one feature
   would move every reading taken against it. T52 does not need it (a stale
   snapshot is reported as stale). T53 owns that widening in demo_header, and the
   fidelity field is the reading that says whether it worked -- until then T53's
   fidelity will read `CheckpointFallback` on most ticks and its acceptance must
   say so instead of claiming per-tick trajectories.
3. **`OverlayEntityTracker`** (from T53's lifecycle point). `observe(tick,
   samples)` diffs consecutive sampled ticks by entity index and returns
   `Appeared` / `Persisted` / `Vanished`, so "the projectile vanished" and "the
   player despawned" are transitions rather than missing samples. A tick fed
   twice is refused rather than applied twice, so the answer cannot depend on how
   many frames asked.
4. **Settings and keys do not collide** (question c). The whole overlay slot gets
   exactly two `settings.ini` keys, both written by T52 through the existing
   `PersistentSettings` path: `outline=0|1` and `outline_depth=0|1`. T53 adds its
   own keys (linger, tier, player filter) under its own names in the same
   section and writes none of T52's. Two keys, two states, one pass.

Renderer entry point (question d): `setOverlayDrawList(const OverlayDrawList&)`
plus `overlayPassReading()`. One pass, one list -- line/trail primitives are
appended to T52's silhouettes by the product and share that pass's depth mode
(kept per list on purpose; T53 was told to object now if it needs otherwise).

**Entity identity** stays `entityIndex + classId + className` with matching by
**class name** only, never by a pinned id -- T53's own measurement (CTFViewModel
320 on one demo, 18 on another) is the same trap. **Team** is `m_iTeamNum` with
an explicit `OverlayTeam::Unknown` fallback that is a neutral grey, so an
unknown team can never pass a red/blue pixel assertion.
## v1 FROZEN (2026-10-22) -- final changes before implementation

Both reviewers changed the contract in the same direction: **decide nothing on
the caller's behalf that the caller has measured.**

1. **`OverlayClassSet`** (T53's requirement (a)). `sampleOverlayEntities` gained
   an optional `const OverlayClassSet* classSet`; a null set keeps the built-in
   name rule (T52's path), and T53 passes its measured class list. The rule was
   wrong in both directions on the real server class table: 7 false positives
   (CTFRocketLauncher, CTFGrenadeLauncher, CTFPipebombLauncher, CTFFlameThrower,
   CTFJar, CTFBottle, CTFPowerupBottle -- weapons and held items that carry
   `m_vecOrigin` and sit at (0,0,0) while held) and 1 false negative
   (CTFGrenadePipebombProjectile, no "Projectile" substring, 774 spawns = the
   second most common real projectile).
2. **One `OverlayDrawList` per depth mode** (T53's requirement (c)): the renderer
   accepts the list once per mode, so one frame can carry a depth-tested trail
   and an x-ray head marker. The invariant "one list = one depth mode" is
   unchanged -- it is what stops a mutation hiding one primitive's depth state
   behind another's.
3. **No per-item age field** (agreed as drafted). Linger state stays in T53's
   builder; `OverlayDrawList::rejectedLingered` is the shared reading.
4. **The gate's assertions live in the header** as predicates, not in the
   script: `overlayPassOrderValid` (the ordinal-6 assertion),
   `overlayPixelMatchesTeam` / `overlayPixelIsNeutral` (the pixel side), and the
   owner-bound `OverlayToggleState` (one slot, one owner, foreign presses
   refused and counted).

