# T52 A3 -- world-skinned player silhouettes

Worktree `D:/TF2_Native_Worktrees/t79-integration`, branch `integration/t79-final`,
base HEAD `f1f98f0` plus the three files listed in section 3.
Input demo: `bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem`.
Capture fixture: `--capture-tick 64250 --playback-speed 8 --debug-camera 5699 -1611 150 14.4 -143.3`
(playback tick 64250 = server tick 74570, `serverTickOrigin=10320`).

## 1. Symptom

Every player silhouette the outline pass submitted came from the sampler's bind pose.
The pass reported `submitted=14 poseWorld=0 poseBind=14` even though nine
`models/player/*.mdl#<checksum>` materials were loaded and 13 of the 15 players had a
skinned mesh instance in the scene. Animated players therefore drew their skeleton's
bind pose, not the pose the frame rendered.

## 2. Root cause

Two defects, both confirmed by reading the two sides of the join.

1. **Key form.** World instances are keyed `<path>#<checksum>` (`native/src/model_loader.cpp:1041`).
   `native/src/entity_overlay.cpp:517-533` looked up the bare `<path>` and
   `<path>#class-fallback` only, so a world instance could never be found and every item
   fell back to `OverlayPoseSource::Bind`. The `model=` column of the probe (see
   `classes-74570.txt`) shows the loaded key with its checksum suffix, which is what
   made the mismatch visible.
2. **Join by cache key.** The draw/item join compared `cacheKey` and then `origin` to
   within 1e-3, although the code's own comment says the entity index is the join. Two
   instances of the same model share a cache key and can sit within a millimetre, and
   every re-uploaded instance gets a fresh cache key, so the join was both ambiguous and
   fragile.

## 3. Change (2 files, +53/-2, plus one struct field)

- `native/include/native_renderer.h`: `EntityModelDrawInstance` gains
  `std::int32_t entityIndex = -1;` -- a draw record now carries which scene instance it
  came from.
- `native/src/main.cpp`: the draw builder copies the instance index
  (`draw.entityIndex = static_cast<std::int32_t>(instance.entityIndex)`), and the join
  matches on it (`if (draw.entityIndex < 0) continue; if (static_cast<std::uint16_t>(draw.entityIndex) != item.entityIndex) continue;`).
  Items without a draw record keep `poseBind`, so `poseWorld + poseBind == submitted`
  still holds by construction.
- `native/src/entity_overlay.cpp`: builds a path -> world key map by truncating at the
  last `#` and binds an item to `OverlayPoseSource::World` when the skinned instance for
  its entity index is present.

## 4. Readings at the capture frame

> **Correction (2026-11-02, T52 A3b).** Most of the `poseBind` items reported below were
> not a skeleton fallback: they were entities the world pass had already dropped as
> corpses, which the overlay was still outlining. See
> `A3B-SILHOUETTE-FOLLOWS-BODY.md` for the measurement that separated the two, and
> section 9 below for what it changes in this report.

```
scene: setInstances playerDraws=2 ...
outline: draw ... silhouettes=... submitted=14 poseWorld=2 poseBind=12
```

The two world-posed silhouettes are exactly the two players the scene drew with a
skinned instance on that frame, so the join is consistent in both directions.

Per-entity coverage over the whole 3303-frame capture (`skinned-entities.txt`): 13 of
the 15 players carried a world-skinned silhouette on at least one frame; entity 16 and
17 never did, entity 12 only once. Per class: soldier 4/5, scout 3/3, demo 2/2,
medic 2/3, heavy 1/1, spy 1/1.

**This recording contains six classes** (soldier, scout, demo, medic, heavy, spy --
`classes-74570.txt`). Pyro, engineer and sniper do not appear in this demo, so a
"nine classes" statement cannot be measured here and is not made.

## 5. Pixel evidence

`pixel-diff.txt`, script `a3region.mjs`, frames from `capture-recipe.sh`:

- two runs with identical arguments: **0 differing pixels** (the frame is deterministic);
- animation ON vs animation OFF at the same tick and camera: **85 differing pixels**,
  maxdelta 210, nine of them inside the published silhouette boxes.

The nine pixels inside the published boxes are the animated pose inside its own region.
The 76 outside are explained in section 6 and are why the gate pins animation OFF.

## 6. Known limitation: a skinned instance publishes the sampler AABB

`native/src/native_renderer_frame.cpp:830-836`:

```cpp
const bool wantVertexBox = overlayVertexExtent_ && instance.skinned == 0
    && instance.vertexPositions && instance.vertexPositionCount > 0;
```

A skinned instance is deliberately excluded from the vertex-extent box, because on the
CPU its vertex positions are the bind pose: projecting them would publish a box around
a pose nobody drew. Skinned items therefore publish the sampler AABB (`extent=0` in
`itemdiag`), which is the pre-skinning bounds and can be smaller than the animated
silhouette -- measured 76 fringe pixels at this camera.

This is not fixed here. Fixing it means publishing the skinned instance's posed bounds
(e.g. the GPU-side transformed bounds) and is its own task; the consequence for the
outline gate is stated next.

## 7. What the gate does about it

The outline gate (`outline-check.sh`) pins `--no-player-animation`, so every silhouette
it measures is a bind silhouette whose published box is the vertex extent (`extent=1`,
11 on-screen items at the pinned tick). The animation path is evidenced by this report
and is not claimed by the gate. A gate run and this report are two different claims and
neither is allowed to stand in for the other.

## 8. Reproduction

```
bash capture-recipe.sh                       # four captures: on1 on2 bind1 bind_d
native/build-nmake/entity_overlay_probe.exe --demo <dem> --ticks 74570 > classes-74570.txt
node a3region.mjs                            # region counts against the published boxes
```

The four captures and the probe write to `D:/_shared/t52-a3/`; the frames are 2,582,406
byte 1920x1080 24bpp BMPs and are not committed. Both frames the pixel claim rests on
are reproducible from the recipe and the executable built at this HEAD.
## 9. Correction (T52 A3b, 2026-11-02)

Sections 1-8 stand as the record of the key-form and join defects, both of which were
real and both of which are fixed. One reading in them was mis-attributed:

* **`poseBind` was not (mostly) "the skeleton was not found".** The world pass drops the
  camera-hidden player (T81) and, on late-recording ticks, every player the director hides
  as a corpse (T66 rule 1) *before* it draws. The overlay's sample set applied neither
  rule, so it published silhouettes for entities the picture did not contain; those items
  matched no draw and fell to bind. At the gate's pinned tick, thirteen of fifteen
  silhouettes outlined nothing.
* Fixed in `native/src/main.cpp` by filtering the silhouette samples with the same two
  predicates the world pass uses, and by counting `poseJoinMissed` (items that matched no
  draw at all; zero is now the contract). On the same frame the bind count drops to zero
  for a different reason than the key-form fix: there is nothing left to mis-join.
* The gate around this also changed: it now shows corpses at the pinned tick (that frame
  otherwise carries a single body) and asserts `poseJoinMissed = 0`,
  `submitted == kept` and `removed >= 1` in a new section 5b. A run with those assertions
  is `evidence/outline/run-20261102-a3b`: 117 OK / 0 FAIL.

Detail and the before/after table: `A3B-SILHOUETTE-FOLLOWS-BODY.md`.
