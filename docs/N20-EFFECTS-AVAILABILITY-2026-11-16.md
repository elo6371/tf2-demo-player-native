# N20 effects availability: the product counts temp effects but draws none (2026-11-16)

Branch `candidate/e5`, worktree `D:/TF2_Native_Worktrees/t105-e5`.
Predecessor: `docs/N17-N20-CHAR-COVERAGE-2026-11-15.md`, `docs/N19-ATTACH-WEAPON-BUDGET-2026-11-16.md`.

## 1. Conclusion

The N20 ask was three *rendered* effect samples (pyro flame, medic heal beam, bleeding).
**This build has no particle renderer at all**, so no such sample can be produced here, and none
is claimed. What the build does have is a temp-entity *timeline* that is counted once per frame
and never submitted to any draw pass.

## 2. Data plane (what the demo parser already extracts)

`native/src/effects_timeline.cpp` (82 lines) turns each `TempEntityEvent` into a `TempEffectTimeline` entry:

- `addFireBulletsEvent`: only `className == "CTEFireBullets"` (shots hard-coded to 1)
- `addExplosionEvent`: `"CTETFExplosion"` / `"CTEExplosion"`
- `addParticleEvent`: `"CTETFParticleEffect"` (uses `m_iEffectIndex`, `m_szParticleName`)
- validation: `tick >= 0`, `fireDelaySeconds` in 0..60, finite origin/direction, FireBullets shots != 0

`native/src/demo_header_events.cpp` (352 lines) `applyTempEntityFields` decodes these fields:

| class | fields |
| --- | --- |
| CTEFireBullets | `m_iPlayer`, `m_iWeaponID`, `m_iMode`, `m_iSeed` |
| CTEPlayerAnimEvent | `m_hPlayer`, `m_iEvent` |
| CTETFParticleEffect | `m_iEffectIndex`, `m_szParticleName` |
| CTETFExplosion / CTEExplosion | `m_iMagnitude`, `m_iScale`, `m_iRadius` |
| CTEClientProjectile | `m_vecVelocity`, `m_nModelIndex`, `m_hOwner`, `m_nLifeTime` |

Storage cap: `summary.tempEntityEvents` stops at 16384 entries (`native/src/demo_header_events.cpp:310`).
No `CTEBloodSprite` handler and no heal-beam-specific path were found, so bleeding is very likely
undecoded and the medic beam most likely arrives as a `CTETFParticleEffect` particle name.
Both are **unverified** (see section 6).

## 3. The only consumer is an HUD counter

`native/src/main.cpp:11664-11666`:

```cpp
hudEffectsDue = sceneTickKnown ? effectTimeline.countThrough(sceneTick) : 0;
traceFmt("scene: hud fxDue=%d drawable=%d", static_cast<int>(hudEffectsDue),
  snapshotDrawable ? 1 : 0);
```

`fxDue` is a number printed into the trace; `drawable` describes the *entity snapshot*, not effects.
Nothing downstream of `countThrough` produces geometry, so an effect has no pixels to compare.

## 4. Clock and anchors

- Timeline keys are demo `event.tick` values; `countThrough(sceneTick)` is a pure function of the
  scene tick, so the same tick returns the same count regardless of wall time, pause or replay
  position. That is the only clock-invariance this build supports, and it is structural, not measured.
- `fireDelaySeconds` (0..60) is clamped at insert time, so a malformed event cannot leak into a later
  tick's count.
- No independent effect clock exists: `--capture-tick` plays forward to the tick and clamps
  (`docs/T85-BUDGET-SUMMARY.md` §7.4), so a trace tick is a playback-domain value.

## 5. What is actually rendered (and it is not particles)

`native/src/native_renderer_overlay.cpp` carries `setProjectileTimeline` (extrapolated generation
events) and the T53 observed-trajectory pass `setProjectileTrackPoints` (LINE LIST, `kLingerTicks = 33`,
warm colour 1.0/0.72/0.25 fading with age; a segment is dropped when `entityIndex` changes or
`ageTicks` jumps by more than 1.5). Both draw *trajectory debug lines*, not sprites or particles.

## 6. Status and next action

N20 as specified (three pixel-verified effect samples) is **not deliverable on this build**; it needs a
particle-rendering card first. This card stops at the data/clock audit above. Reported to lead for a
scope decision rather than faked with placeholder geometry.

Unverified assumptions, in order of cheapness to settle:

1. Does this demo contain particle/explosion/firebullet temp entity events at all?
   Next command (read-only, CPU, ~seconds):
   `./native/build-nmake/demo_open_probe.exe --scan <demo.dem>`
   and read `temp_events=`, `temp_particle_effect=`, `temp_explosions=`, `temp_firebullets=`,
   `temp_failures=` plus `first_temp_failure_*` (`native/tools/demo_open_probe.cpp:838-842,896-903`;
   the counters live in `native/include/demo_header.h:721-729`).
2. Which particle names the medic beam and pyro flame use on the demo — needs a per-name dump that no
   existing binary prints; `temp_effect_probe.exe` is a unit test with no demo input.
3. Whether `CTEBloodSprite` appears at all (no handler in the parser, so it would be dropped silently).

Partial evidence already captured (not committed, `.scratch/` is git-ignored):
`.scratch/card-char/n20/open.out` — one line from a plain run without `--scan`:
`path=<wide-path> header=1 header_error= index=1 index_error= recording=SourceTV map=pl_upward_f12
malformed_offset=0 commands=85205 packets=85201 ticks=85204 frames=85198`.
That run confirms the demo opens and indexes; it does **not** contain the temp counters.

## 7. Data plane measured (2026-11-16, 10:10-10:20 window)

Assumption 1 above is now settled, and it settles harder than "events exist": the demo carries them,
the decoder is clean, and the product's own count is **truncated**.

**Census** (read-only, CPU, no lock needed). `./native/build-nmake/demo_open_probe.exe --scan <demo>`,
demo sha256 `7e047aff1f7743915e355eac907a95dc3d981aa0fae5591b6b15346fb77dc4a3`, rc=0:

```
temp_events=68133 temp_failures=0 particles=986 explosions=1873 firebullets=4307
projectiles=1538 sounds=35278 sound_failures=20923
first_temp_failure_stage= first_temp_failure_class=-1 first_temp_failure_tick=-1 (all empty/-1)
```

So: 986 particle effects, 1873 explosions, 4307 fire-bullet events, **0 temp entity decode failures**.
The events N20 needs are in the demo; nothing is being dropped on the wire side.

**Product reading** (under `tools/lockrun.sh`, exe sha256 `e61c7fd9...bb078`, `--capture-tick 84000
--playback-speed 32 --window-size 1280x720`; rc=0, frame=yes, trace 16,019,920 B). Artifacts:
`D:/_shared/EXEC-2026-11-15/INST4/p3a-n20/fx-84000.{trace,bmp,log}`.

- Title line prints `tecached=16384` (twice) -> the summary's temp entity list is at its cap.
- 933 `scene: hud fxDue=` samples: first 0, second 20, then monotone growth, and the **last 692
  samples are all 1538**. Max = 1538.

**Why it plateaus (code, not inference).** `native/src/demo_header_events.cpp:310` guards the push with
`if (summary.tempEntityEvents.size() < 16384u)`, so only the **first 16384 of 68133** temp events are
retained. Both derived timelines are built from that truncated list: the effect timeline at
`native/src/main.cpp:7079-7085` (iterates `demoNetworkSummary.tempEntityEvents`) and the projectile
timeline at `native/src/demo_header_events.cpp:325-327` (same cap block). The HUD counter is the sole
consumer: `native/src/main.cpp:11664-11666` -> `effectTimeline.countThrough(sceneTick)`.

Arithmetic that confirms this is the cap and not a validation drop: the cap keeps 16384/68133 = 24.05%
of events; the demo's effect-capable events total 986+1873+4307 = 7166, so a proportional slice is
about 1723, and 1538 is 0.89x of that. Same order, one direction. A validation drop would have to
remove events selectively and would not land this close to the proportional expectation.

**The 1538 == 1538 coincidence is explained, not a lead.** `--scan` prints `projectiles=` as
`summary.projectileTimeline.size()` (`native/tools/demo_open_probe.cpp:843`), and
`projectileFromTempEntity` (`native/src/demo_header_events.cpp:77-80`) accepts exactly
FireBullets/TFParticleEffect/TFExplosion/Explosion/ClientProjectile **inside the same cap block**.
The two timelines therefore cover the same capped window; their equality says no ClientProjectile
landed inside that window and no effect event was rejected by `effects_timeline.cpp` validation
(fireDelay in 0..60, finite origin/direction, FireBullets shots != 0). It is not a second, independent
measurement of the same events.

**What this means for the card.** Two independent gaps, and the counting one is new:

1. There is still no particle draw pass - `fxDue` is a count, nothing rasterises a particle. Unchanged
   by this measurement.
2. Even as a count it is truncated: 1538 reported against 7166 effect-capable events in the demo. A
   card that adds particle drawing on top of this list will silently lose everything after the 16384th
   temp event, at whatever tick that lands (roughly the first quarter of the demo). The cap has to be
   lifted or the list has to be streamed/windowed **before** any renderer work can be called complete.

Side note for the audio card (N21/P3b), same demo, same run: `sounds=35278 sound_failures=20923` -
59% of decoded sound events failed to resolve. Different card, but do not quote a clean audio plane.
