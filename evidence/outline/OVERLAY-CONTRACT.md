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
* **The silhouette's pose source is conditional and is recorded, never assumed
  (v5, 2026-10-22).** The mesh that answers the join is asked for in this order:
  first the mesh the entity pass uploaded AND DREW for this frame -- that mesh
  carries the bones of the pose on screen -- and only if there is none, the
  overlay's own bind-pose upload of the class model (`<path>#class-fallback`).
  Every draw reports which one answered (`poseSource=world|bind`, counted per
  list as `poseWorld`/`poseBind`). The bind case draws a BIND POSE at the player
  origin: it does not follow the animation, and no assertion may claim it does.
  The condition is not a loophole, it is the measurement: on the reference
  recording the entity pass has no player body mesh at all (see v5).
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
## v3 (2026-10-22, T52 implementation) -- what the code forced, and what is now a reading

Three things below changed the contract AFTER v2, and all three are additive: no
consumer of v2 has to change and T53 reads none of them. They are recorded here
because a header that differs from the page describing it is how the next reader
gets lied to.

1. **`OverlayEntityFilter::Projectiles` is gone** (approved amendment (a)). The
   filter enum still selects players; projectile membership is a CALLER-provided
   optional class-name set (`OverlayClassSet`), and T52 passes `nullptr`. A fixed
   projectile filter inside the contract would have forced T53 to change the
   header the moment its class list changed, and T53 had already measured that
   list differing between recordings.
2. **Two `OverlayDrawList` submissions per frame** (approved amendment (b)), one
   per depth mode. v2 said "one pass, one list"; the pass is still one pass, but
   the renderer now keeps **one list per depth slot** and rebuilds its GPU
   buffers from BOTH on every submission. This was not a documentation nicety:
   the first implementation rebuilt the buffers from the list just submitted, so
   the second call of a frame erased the first one's geometry, and the per-slot
   line draw used the wrong start offset. Both defects are why the gate asserts
   `red=`/`blue=` counts AND the drawn vertex/line counts, not just "drew=1".
3. **`OverlayPassReading` publishes the depth state it actually bound**:
   `depthTestEnabled` and `depthWriteEnabled`, filled from
   `ID3D11DepthStencilState::GetDesc()` on the two states at init. The contract has
   always said "x-ray is depth test OFF and depth write OFF"; until this it was a
   sentence beside the lines that set the flags, and the write half is invisible in
   any screenshot because the ViewModel pass clears depth immediately afterwards.
   It is now a number in the trace and an assertion in the gate, with mutation m5
   (write ON) as its proof that the assertion can go red.

### The tick domain, measured (this is why the gate never passed)

The entity history retains a WINDOW, not the recording, and the product fills its
scene states through the same accessor (`queryEntitySnapshotAtOrBeforeTick`).
Sampled ticks outside the window therefore answer `unavailable` with no rows, and a
frame captured at one has NO player in it at all -- the overlay draws nothing
because there is nothing to draw, which reads exactly like a broken overlay.
Measured on the reference recording before the fix: requested playback ticks
1001/1201/1401/1601/1801/2001, six answers, all "the tick precedes every retained
snapshot", `rows=0`. The probe now prints the window and every tick inside it that
answers with players (`--find-ticks`), and the gate's tick list is inside it.

### Harness changes needed to observe any of this

* `--playback-speed <multiplier>` (0.125 .. 64). A capture pinned near the end of a
  ~64000-tick recording costs ~16 minutes of wall clock at 1x, and the gate needs
  more than ten frames. The captured frame is unchanged because the cursor is HELD
  at the requested tick and the scene is resolved from the recording, not
  accumulated frame by frame -- and that sentence is an ASSERTION, not a remark:
  the gate captures the same tick at speed=1 and at the default speed and requires
  the two frames to be byte-identical (`cmp`), with mutation m6 (capture lands NEAR
  the tick instead of AT it) as the proof that the comparison can go red.
* `--start-paused` is NOT usable with `--capture-tick`: it freezes the cursor at 0,
  so the one-shot capture waits forever for `tick >= N` and the run times out with
  no frame. The inherited gate passed it, which is consistent with the artifacts the
  earlier runs left behind: none.

## v5 (2026-10-22, T52 first run with the join fixed) -- the overlay had no mesh to draw

The class fallback fixed the join and changed nothing on screen, and the reason is
the next layer down:

```text
gpu: models done 59 uploaded=59          <- every model the process ever prepared
scene: buildInstances=256                <- instance requests, capped
scene: setInstances draws=96 markers=32  <- 32 entities fall back to a MARKER
grep -c models/player pick-64229.trace   -> 0   (1.3 MB, 33 seconds)
```

**The world pass never draws a player BODY on this recording.** A player with no
demo model path becomes a marker, so `draws` contains no player mesh, so the
silhouette builder -- which joins against `draws` on purpose, because that is the
only way it can be the same mesh the screen shows -- had nothing to join to. That
is a hole in the product, not in the overlay: 21 players are sampled per frame and
none of them is drawn as a body.

**What the overlay does about it, and what it refuses to do.** It uploads the nine
class models itself, keyed exactly as `EntityModelResolver` keys its class
fallback, and uses them ONLY as a fallback behind the world pass's mesh. It does
not draw a coloured quad: a team-coloured square is not an outline of a player,
and it would put a coloured box on the screen for a player nobody drew. It also
does not prefer its own mesh: the world pass's mesh wins whenever one exists,
so on the day the body hole is fixed the bind source drops to 0 with no code
change -- that is the whole reason `poseSource` is counted rather than assumed.

Two consequences written down rather than hidden:

1. The bind silhouettes do not follow the animation. A gate may assert that the
   mask is STABLE at one tick and that `poseSource` agrees with which mesh exists;
   it may not assert that the outline tracks the animated pose.
2. The mesh cap in `Renderer::uploadEntityModelMesh` was 64. The reference
   recording uses 59 for the world, so five of the nine class models were REFUSED
   with E_OUTOFMEMORY -- and a refused key is submitted, counted, and drawn from
   nothing, which is exactly the defect mutation m8 injects on purpose. The cap is
   now 96 and the keys that actually landed are the only ones the builder is told
   about; an upload refusal is a trace line, not a silent hole.
## v4 (2026-10-22, T52 first green build) -- the model join, and how it was found

The feature as first written could not draw anything, on any tick, and the gate had
no way to say so. This section is the reading that found it and the rule it forced.

### What the numbers said

A capture at playback tick 64332 (sceneTick 74584, inside the retained window) wrote
778 frames of trace and every one of them carried

    outline: draw ... drew=0 submitted=0 samples=21 noMesh=20 noOrigin=1

i.e. the sampler found the players (21 rows, 6 red / 7 blue / 8 unknown, 20 with a
pose) and the builder rejected ALL of them. The probe at the same tick prints the
reason in its own column: `model=-` on all 21 rows.

**No player on this recording carries `m_nModelIndex`.** Identity, team, origin and
pose all resolve; the model does not. The sampler resolved the model ONLY from
`m_nModelIndex`, so every row reached the join with an empty path.

### The rule it forced: one fact, one producer

The world entity pass has answered "which model is this player" since before this
feature existed: `EntityModelResolver::buildInstances` falls back to
`defaultPlayerModelPath(m_iClass)` and keys the mesh `"<path>#class-fallback"`.
The overlay answered the same question a different way, so the two disagreed about
what a player looks like: the mesh pass uploaded 96 instances and the overlay drew
none of them, and both reported success on their own lines.

The fix is not "also try m_iClass". The fix is that the outline now asks the same
producer and carries THE KEY THE MESH WAS UPLOADED UNDER, because the renderer looks
the uploaded mesh up by that key. A silhouette with the bare path is submitted,
counted, and drawn from nothing -- mutation m8 is that exact defect, and it is the
reason the join carries `cacheKey` on the item rather than recomputing it at draw
time.

### Two mutations, because the fix has two halves

* **m7** removes the class fallback -> `submitted` must fall back to 0 and `noMesh`
  must rise to `samples`. If it does not, the fallback is not what draws the
  silhouettes and the fix proved nothing.
* **m8** keeps the resolved path but drops the upload key from the item ->
  `submitted` stays > 0 (asserted as the positive control) while `silhouettes`
  drawn drops to 0. This is the half a pixel gate cannot see on its own: the list is
  right and the frame is empty.

### The tick origin has ONE answer, and it is the product's

Three numbers were circulating for `firstPacketTick` on one recording: a comment in
main.cpp said 29368, the probe printed 1, the product's trace said 10252. Only the
third is the product speaking about itself ("scene: begin tick=0 sceneTick=10252").
The comment now carries the derivation and no number; the probe prints
`serverTickOrigin` (the first command 1 or 2 entry, the product's own rule) and
`firstRecordedPacketTick` separately; and the capture tick is computed as
`sceneTick - (the product's own offset)` from a five-second capture at playback 0
instead of being derived from a probe tick and a guess.
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

