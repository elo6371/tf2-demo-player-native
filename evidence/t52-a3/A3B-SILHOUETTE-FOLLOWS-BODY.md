# T52 A3b — the silhouette follows the body

Status: **implemented and measured on 2026-11-02**, gate `OUTLINE-CHECK=PASS` (117 OK / 0 FAIL)
in `evidence/outline/run-20261102-a3b`. Branch `integration/t79-final`, commit at the
foot of this file.

## 1. The defect

An outline is published per *sampled* entity; a body is drawn per *admitted* entity. The
two sets were built by different code with different rules, and the overlay's set was the
larger one.

The world pass drops two kinds of entity before it draws (the `drawCandidates` loop in
`native/src/main.cpp`):

* the player the relay camera stands inside (T81, `g_spectateCameraHiddenEntity`), and
* every player the director hides as a corpse (T66 rule 1, `g_spectateDirector.hidesModel`).

The overlay's sample set (`sampleOverlayEntities`, `OverlayEntityFilter::Players`) applied
neither rule. An entity dropped by the world pass therefore still got a silhouette, the
join that pairs a silhouette with a drawn body (`main.cpp`, the `poseJoinMissed` loop)
found nothing for it, and the item fell back to bind pose without a bone range.

That is what the A3 report read as "two of fourteen silhouettes take the world skeleton,
twelve fall back to bind". It was not a skeleton defect. Thirteen of the fifteen
silhouettes at the gate's pinned tick outlined players the picture did not contain, and
the late-recording ticks are almost entirely corpses — at tick 64250 nineteen of the
twenty samples are entities the world pass no longer draws.

## 2. The measurement that separated the two explanations

`--spectate-show-corpses` is the switch that turns T66 rule 1 off. Same binary, same
tick, same camera, the flag as the only difference:

| tick | corpse rule | samples | removed | kept | submitted | poseWorld | poseBind | poseJoinMissed |
|---|---|---|---|---|---|---|---|---|
| 30000, before | on | 20 | — | — | 14 | 10 | 4 | — |
| 30000, after | on | 20 | 10 | 10 | 10 | 10 | 0 | 0 |
| 64250, before | on | 20 | — | — | 15 | 2 | 13 | — |
| 64250, after | on | 20 | 19 | 1 | 1 | 1 | 0 | 0 |
| 64250, after | off | 20 | 1 | 19 | 14 | 14 | 0 | 0 |

Read the last line: with the bodies back, every silhouette joins, so nothing falls to
bind. `removed=1` on that row is the camera-hidden player, T81's rule firing on its own.

Reproduction: `/d/_shared/a3diag/run4.sh`, log `run4.log`, hostlock owner `a3b`.

## 3. The fix (product)

`native/src/main.cpp`, four edits, +41/−1:

1. `OverlayStats::lastJoinMissed` — items whose entity index matched no draw on this
   frame. Zero is the contract, not a coincidence.
2. The join loop counts a miss (`joined`) instead of silently continuing.
3. The trace line carries `poseJoinMissed=%zu` beside `poseWorld`/`poseBind`, so the
   reading is in the artifact and not only in a report.
4. The sample set is filtered with the *same two predicates* the world pass uses
   (`cameraHides = g_spectateCameraHiddenEntity >= 0 && entityIndex == g_spectateCameraHiddenEntity`,
   `corpseHides = !g_spectateShowCorpses && g_spectateDirector.hidesModel(entityIndex)`)
   before the draw list is built, and a `overlay: corpseFilter removed=%zu samples=%zu kept=%zu`
   line records what it removed.

The set relation is the point: silhouette ⊆ body **by construction**. The old code was
correct only on frames where nothing had been dropped, which is why it survived.

## 4. The fix (gate) — and why the old fixture could not see this

At the pinned tick with corpses hidden the frame carries **one** body. A gate that
measures silhouette-to-body joins on a frame with one body cannot fail: thirteen of its
fifteen silhouettes pointed at nothing and it stayed green.

`outline-check.sh` now passes `--spectate-show-corpses` to every capture of the main
fixture (`CORPSE_ARGS`, defined beside `ANIM_ARGS`), which is a decision about what this
frame can measure, not a product change. The same tick and camera then carry fourteen
silhouettes of both teams (6 red, 6 blue, 2 unknown, 5 noMesh) and every one of them is
joined to a drawn body.

Three assertions were added:

* `silhouettes with no body behind them (overlay on) = 0` — `poseJoinMissed` on the main
  fixture.
* new section `5b/6 the corpse rule: no silhouette for a body the world pass dropped` —
  the same frame re-run with the rule **on** (`CORPSE_ARGS=()`), asserting
  `players the corpse rule removed >= 1`, `silhouettes submitted == the samples the rule kept`,
  and `silhouettes with no body behind them (corpse rule on) = 0`.
* `submitted == kept` is the one that would have caught the original defect directly:
  the overlay used to publish the sampled set, not the kept set.

## 5. Readings this run produced

`OUTLINE_OUT=evidence/outline/run-20261102-a3b bash outline-check.sh` — **117 OK / 0 FAIL**,
`OUTLINE-CHECK=PASS`, log `D:/_shared/a3b/gate-a3b.log`, 21:53–22:23, hostlock owner
`a3b-gate2`. Key lines:

```
  on frame: submitted=14 red=6 blue=6 unknown=2 noMesh=5
  on.poseWorld=0 on.poseBind=14        (bind fixture: poseBind == submitted by design)
  silhouettes with no body behind them (overlay on) = 0
  regions=10; off frame team_inside=0 team_outside=2872
  corpse rule run: kept=2 submitted=2 poseJoinMissed=0
```

`kept=2` against the table's `kept=1` is the animation flag, not a contradiction: the
table's 64250 row is `--player-animation`, the gate pins `--no-player-animation`.

## 6. What this does not claim

* **Animated silhouettes.** The gate pins `--no-player-animation`; `A3-REPORT.md` carries
  the animation evidence and no assertion here stands in for it.
* **Third-person body pixels.** The camera mode and orbit exist; the appearance of the
  body in third person is not measured by this run.
* **World-pass coverage.** No silhouette for a dropped entity is now the contract; that
  the world pass draws the right bodies is T66/T60 territory.

## 7. Reproduction

```
bash build-target.sh tf2_demo_native entity_overlay_probe   # the gate does not build
OUTLINE_OUT=evidence/outline/run-<id> bash outline-check.sh
```

The stale-binary guard is load-bearing: the first attempt at this run failed before any
assertion because `native/build-nmake/entity_overlay_probe.exe` was older than
`native/src/audio_resource.cpp` and `native/src/audio_timeline.cpp`. The gate refuses to
measure a binary the tree does not describe, and it is right to.
