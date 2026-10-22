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
