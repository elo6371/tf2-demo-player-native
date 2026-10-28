# T61 -- third-person player animation: the body was a black silhouette, and now it moves

Branch `task/t61-anim`, base `7b447a3` (the p0 tree that carries T60's model fixes).
Everything below is a measurement from this tree; nothing is inferred from a
sibling task's report.

## 1. The defect, as it presented

A third-person camera pointed at a player drew the body as a **pure black
silhouette**: correct shape, correct pose, correct size, no colour. The scene was
otherwise fine (world, props, HUD, the viewmodel all drew normally).

The silhouette was the visible end of a five-link chain, and the T61 claim is not
"the body is coloured now" but "every link is measured, and the chain is driven by
the demo's own ticks":

```
decode  ->  binding  ->  sampling  ->  draw  ->  pixels
(the demo    (model,       (the pose     (the entity  (the frame
 carries no   skeleton,     for a tick)   pass)        shows it)
 sequence)    sequences)
```

## 2. Stage 1 -- decode: the demo does not carry a player's sequence

Measured, not assumed: with the drive off, the product's own scene summary reads

```
scene: anim skinned=53 bindPose=43 matrices=152 gpuSkin=52/152 boneMismatch=0 ...
      tick=74481 status=missing reason=entity carried no m_nSequence
```

`status=missing` with that exact reason is the product saying it looked for the
player's `m_nSequence` (sendprop 166) in the recorded entity data and did not find
it. This is the same fact the animation-availability step (`verify-all.sh` step
21/24) established for the whole nine-demo corpus, now re-read on the specific
tick this task captures.

So the product cannot animate a player by reading the demo. T61's drive instead
**derives** the animation input from what the demo does carry: the entity's
velocity (`m_vecVelocity`) and the playback tick. That derivation is
`native/src/animation_clock.cpp` (`advance`), and the tick it is fed is the
product's own resolved scene tick, not a wall clock.

## 3. Stage 2 -- binding: the model, its skeleton and its sequence table

Each player class has a separate `_animations.mdl` next to its `_c.mdl`/`.mdl`
(the T60 finding: the animation source must be chosen by name, not by directory
order). The binding resolves, per model, the render skeleton and the pose-to-bone
table, then composes `skin[i] = animatedWorld[i] * poseToBone[i]`
(`native/src/animation_binding.cpp:500-529`).

`player-animation-check.sh` asserts this stage per class model (`model_pose_probe`
links the same `animation_binding.cpp` / `animation_decoder.cpp` the product
links). Nine classes, each measured:

| class | sequences | bindBones == bones | valid bind bones | animation source |
| --- | --- | --- | --- | --- |
| scout | 306 | 78 == 78 | 78 | separate `_animations.mdl` |
| sniper | 240 | 74 == 74 | 74 | separate |
| soldier | 361 | 86 == 86 | 86 | separate |
| demo | 276 | 84 == 84 | 84 | separate |
| medic | 213 | 92 == 92 | 92 | separate |
| nine classes total | every one >= 200 | every one equal | every one >= 1 | every one a `_animations.mdl` |

The three negative controls fail by name before any of this counts:
`tf-root-invalid` (a tf root with no `*_dir.vpk`), `model-missing` (a model that
does not exist) and `sequence-out-of-range` (a sequence past the table), each with
exit code 2 and its own reason string.

## 4. Stage 3 -- sampling: the pose for a tick

The sampler is read at four quarter-cycle points per class, from the product's own
code path:

| reading | scout | sniper | soldier | bound | verdict |
| --- | --- | --- | --- | --- | --- |
| distinct pose checksums | 4 | 4 | 4 | 4 | a sampler that ignored the cycle would return one pose four times |
| max deviation from bind, cycle 0.00 | 69.67 | 74.90 | -- | boundDiag 148.07 / 168.44 | inside the skeleton's own sphere |
| max deviation from bind, cycle 0.25 | 68.72 | 73.13 | -- | same | inside |
| max deviation from bind, cycle 0.50 | 68.75 | 70.74 | -- | same | inside |
| max deviation from bind, cycle 0.75 | 68.86 | 74.08 | -- | same | inside |

Every matrix element of every pose is asserted finite.

**Continuity** (C): sixty 1/60 s steps at 125 units/s move no matrix element by
more than a quarter of `boundDiag`: measured **13.934902 <= 37.017498** for the
scout, and greater than zero (a frozen pose would pass "small step" trivially).

**The clock rate** (D), which is the link that ties the demo to the animation:

| reading | measured | asserted |
| --- | --- | --- |
| `movingRate` | 1.000000 | > `idleRate` |
| `idleRate` | 0.000000 | == 0 |
| a moving second advances the cycle by | 0.999999702 | 1.000000 +/- 1% |
| an idle second advances the cycle by | 0.000000000 | == 0 |

**The tick script** (E), synthetic and from a real recording: 61 synthetic ticks
give 31 moving ticks, 30 of them advancing (one baseline excluded), 0 idle ticks
advancing, max moving delta 0.02. From the bagel recording over ticks
60000..62000: 39 transitions across 13 entities, 39 moving ticks, 26 advancing,
**1997 stale ticks** (a repeated Checkpoint answer is not an idle tick -- the
distinction that makes the rate meaningful) and 0 idle ticks advancing, max moving
delta 11.905984879. The product's own printed lines are then re-read back: moving
ticks that did not advance = 0, idle ticks that advanced = 0.

## 5. Stage 4 -- draw: the entity pass, and the bug that made it black

This stage is where the defect actually lived, and it took three measurements to
localise it, because "the body is black" is compatible with at least six different
failures.

### 5.1 What the body pixels were doing (measured before the fix)

`--world-albedo` renders the albedo term the shading actually starts from. On the
10,845-pixel body blob at tick 64161 the albedo view was **`(0,0,0)` on all
10,845 pixels**, while:

| reading | measurement | what it ruled out |
| --- | --- | --- |
| `--world-texel` (raw t0 sample) | mean `[85.5 56.5 47.3]`, 4,110 distinct colours, 889/10,845 at or below max-channel 8 | texture missing, texture black, sheet corrupt |
| `--world-uv` | 11,307 distinct colours over the frame | uv degenerate or collapsed |
| `--world-flat` | 0 black pixels over the body | geometry absent |
| `--world-face-id` | team blue `(71,133,219)` and grey `(184,189,178)` | instance colour zero |
| trace | `scene: entityRanges ranges=92 texturedRanges=80 texturedInstances=87` | `if (!range.textureView) continue;` skipping the entity ranges |

A stacked crop of the same box (product / texel / albedo, 3x NEAREST) settled it by
eye as well: the **same silhouette** is flat black in the product view, fully
textured in the texel view (orange and blue clothing panels, dark boots, hair
strands -- an actual TF2 character in the correct pose), and flat black again in
the albedo view.

### 5.2 The measurement that named the mechanism

A dedicated debug arm returned the two blend terms as colour:

```hlsl
if (envFeatures.x > 8.5) {
  uint entityDetail = ((uint)feature.x) & 4u;
  return float4(saturate(blend), saturate(blendModulate), entityDetail != 0u ? 1.0 : 0.0, 1.0);
}
```

Over the whole blob: red>128 on 10,845 pixels, green>128 on 10,845 pixels, blue 0.
That is `saturate(blend) == 1.0` and `saturate(blendModulate) == 1.0` on **every
body pixel** -- and `blend` is supposed to be the *multi-layer* weight whose
`-1.0` value means "single layer".

The cause: the entity vertex shader (`entityModelVertexSource`) never wrote
TEXCOORD4..7, while the pixel shader it is paired with is the **shared world pixel
shader** (`worldPixelSource`). Seven passes bind that one pixel shader
(`native/src/native_renderer.cpp:5646, :5677, :5820, :5863, :5895, :6283, :6317`)
and only `:5895` is the entity pass, so forking the shader was the wrong fix. The
undeclared interpolator read 1.0 on this driver, so the entity pass ran

```hlsl
sampled = lerp(sampled, blendTexture.Sample(sampler0, uv).rgb, blendWeight);
```

with `blendWeight = 1.0` and the blend atlas **unbound in the entity pass**, i.e.
lerped the correct texel all the way to black; then
`float3 albedo = lerp(colour.rgb, sampled * colour.rgb, colour.a);` with
`colour.a = 1` carried the black through.

### 5.3 The fix

The entity vertex shader now declares the four interpolators and writes their
neutral values (`native/src/native_renderer.cpp:817-820, :858-861`):

```cpp
float blend : TEXCOORD4;   float blendModulate : TEXCOORD5;
float4 feature : TEXCOORD6;  float4 tangent : TEXCOORD7;
...
output.blend = -1.0;                       // single-layer sentinel, so `blend >= 0.0` is false
output.blendModulate = 0.0;
output.feature = float4(0.0, 0.0, 0.0, 0.0);
output.tangent = float4(1.0, 0.0, 0.0, 0.0);
```

`-1.0` is load-bearing: the world shader treats a *negative* weight as "this surface
has one layer", and `0.0` would have been read as "two layers, blend weight zero".

## 6. Stage 5 -- pixels: the frame

The mask is not a hardcoded box (a box keeps passing if the entity pass stops
drawing there). `--world-entity-coverage` paints the entity pass with a sentinel and
nothing else, so the mask is whatever the entity pass actually drew:

- sentinel `(255,0,255)` = **3,497 pixels** of 860,784 (0.41%), bbox x434..635
  y320..388 (top-down rows); the frame carries only three colours in total (the
  sentinel plus the sky's `(89,158,97)` and the world's `(14,18,22)`), so the
  sentinel belongs to the entity pass alone.
- over that mask, the product view at tick 64161 (`--player-animation`) carries
  **841 distinct colours**, mean luma **100.6**, pure-black share **0.100** --
  a shaded body, not a flat fill and not a silhouette.

The A/B is the same tick, same camera, same demo, with `--player-animation` the
only difference:

| comparison | differ | differ by >= 24 |
| --- | --- | --- |
| animated vs the same tick with the drive off | **2,268 (64.86%)** | 1,625 (46.47%) |

That is the **on/off proof**: removing the flag changes 65% of the body pixels
(46% of them by 24 or more levels), so the drive reaches the pixels, and the body
stays a shaded surface in both.

Both captures are proven to describe the same moment before they are compared, on
two readings that exist in *both* runs: `capture: armed requested=64161
frame_tick=64161` and the same resolved `scene: anim ... tick=74481`.

### 6.1 Two caveats about the mask, stated rather than hidden

1. **The mask is captured with the drive off** (bind pose), and is then used to
   measure the *animated* frame. A driven pose can extend past the bind silhouette,
   so the measured region is a **subset** of the animated body. That makes the
   assertions conservative in the direction that matters: the shaded-body readings
   (distinct colours, luma, black share) are taken inside a region the animated body
   is guaranteed to cover, and the moved-share is a lower bound.
2. An earlier exploration of the same tree used a mask captured **with** the drive
   on (**12,035 pixels**) and read the same story on it: 1,907 distinct colours,
   mean luma 19.1, black share 0.142, a later tick (64161 -> 64251) differing on
   11,058 of 12,035 pixels (91.88%). The **time proof** (the body is drawn in a
   different pose at a different tick) comes from that round; the committed gate
   asserts the on/off proof and the shaded body, and `t61-pixels.py` already accepts
   a `--later` frame so the arm can be switched on without editing the script.

## 7. One number in the trace that must not be read as a defect

The animated run reports `boneMismatch=34800` where the undriven run reports `0`.
It is **not** a pose defect, and it is not evidence of one. The counter
(`native/src/main.cpp:9113-9117`) increments when

```cpp
sampled.skinBones != sampled.validBones || sampled.skinBones != sampled.skinMatrices.size()
```

but `skinMatrices` is sized to the **whole render skeleton**
(`binding->poseToBone.size()`, `native/src/animation_binding.cpp:503`) while
`skinBones` counts only the bones that **received an animation channel**
(:529). A render bone with no channel keeps the identity on purpose ("helper
bone", :510). So the second clause is true for every skeleton that has helper
bones, whatever the pose is; it fires only when the drive runs because the bind
path (`sampled.bindPose`) never enters the block at all. It scales with the number
of driven instances rather than with anything the pose does: the three driven
captures of one forward run read `34800` over 7,208 frames, `31111` over 6,390 and
`30202` over 6,244 -- about 4.8 per frame, while the two undriven captures read
`0`. It therefore measures "player skeletons carry helper bones", not "the pose is
wrong".

Consequence recorded for the follow-up list: that counter cannot separate an asset
property from a real mismatch, and no gate in this tree asserts it == 0 for
players; `weapon-animation-check.sh` asserts it == 0 for weapons, where it holds.

## 8. How this is gated

Two gates and three mutation suites, all new in this tree. Both gates are wired into
`verify-all.sh` (steps 25/30 and 26/30) with **pinned assertion counts**, so a
section that silently stops running is a red line rather than a smaller number of
greens; the mutation suites are deliberately **not** wired in, because each one
rewrites sources and rebuilds between cases.

| gate | what it asserts | forward | mutation |
|---|---|---|---|
| `player-animation-check.sh` | stages 2-3: the sequence table, the pose, continuity, the clock rate, the tick script -- plus three negative controls, a determinism repeat, and (T61.1 review 2) eleven matcher assertions and eight entity-id assertions | **232 OK / 0 FAIL**, `PLAYER-ANIMATION-CHECK=PASS` | `player-animation-mutations.sh` m1-m5; `player-animation-unit-mutations.sh` u1-u10 (8.7) |
| `player-animation-pixels.sh` + `t61-pixels.py` | stages 4-5: the entity pass's own mask, the body inside it, the on/off pair at one scene tick, a byte-identical repeat capture, the sequence the drive actually resolved, and (T61.1 review 2) two mandatory identity-frame clock assertions | **28 OK / 0 FAIL**, `PIXEL-CHECK=PASS`, `ENTITY-ID=PASS` | `player-animation-pixel-mutations.sh` m6-m12 |

### 8.1 The negative controls run first

Before any forward assertion, the counter gate runs three inputs that must fail
with their own names, so "the gate is green" cannot mean "the gate measured
nothing":

| input | result |
|---|---|
| a tf root with no `*_dir.vpk` | `FAIL player-animation-test reason=tf-root-invalid` (rc=2) |
| a model that does not exist | `reason=model-missing` (rc=2) |
| a sequence past the table | `reason=sequence-out-of-range` (rc=2) |

The pixel gate has its equivalent: every capture must exit 0 and must publish the
view name it claims (`entity-coverage` for the mask, `textured` for the two product
frames), the animated capture must publish its `animsum` line (otherwise the drive
never ran and the on/off pair compares two undriven frames), and `t61-pixels.py`
must print `PIXEL-CHECK=PASS` -- a missing verdict is a failure, because an empty
log reads as "no FAIL lines".

### 8.2 The counter mutation suite (m1-m5)

Each case patches one line, rebuilds, runs the gate, requires rc != 0 **and** the
named assertion **and** a byte-identical restore, then rebuilds and re-runs the gate
to prove the tree is green again.

| case | mutation | gate | named red |
|---|---|---|---|
| m1 | `animation_clock.cpp`: `delta = rate * dt` -> `0.0` | rc=1, 63 FAIL | `B scout distinct pose checksums`, `D movingOneSecond` |
| m2 | `animation_binding.cpp`: skin matrix -> identity (bind pose) | rc=1, 81 FAIL | `B scout distinct pose checksums` |
| m3 | `animation_clock.cpp`: rate -> `maxMovingRate` (ignores speed) | rc=1, 22 FAIL | `D movingOneSecond` |
| m4 | `animation_clock.cpp`: `delta = 0.5` regardless of dt | rc=1, 63 FAIL | `C scout maxStepDelta`, `B scout distinct pose checksums` |
| m5 | `animation_binding.cpp`: one matrix element -> NaN | rc=1, 81 FAIL | `reason=non-finite-pose` |

Every case restored byte-identically (`cmp -s`), every rebuild was rc=0 with 0
`error C` (a mutant that does not compile is INVALID, not caught), and the suite
ends on a green baseline (`MUT-RC=0`).

The whole suite was re-run against the round-6 gate (232 assertions, two new
sections) after the pixel suite finished, because the counts above are only
meaningful against the gate that exists: `m1` 63, `m2` 81, `m3` 22, `m4` 63,
`m5` 81 FAIL lines, every named reason present, every file restored
byte-identically, and the baseline green again after each restore. The two new
sections (matcher, entity id) are pure-function tests over the probe and do not
touch a capture, so they add no FAIL lines to any of these five; the +2 on m1,
m2 and m4 is the determinism block's repeat run, which reddens twice when the
pose digest is broken.

### 8.3 The pixel mutation suite (m6-m12)

Seven cases, each reintroducing a real defect and requiring the gate to redden a
**named** assertion -- a pixel reading, a named pick, or a named per-entity line --
not a missing trace line and not a vanished counter:
every case keeps `animsum` and the debug view intact, so only the picture -- or,
for m8-m11, the sequence the picture is drawn from -- changes.

| case | mutation | gate | named red |
|---|---|---|---|
| m6 | `native/src/native_renderer.cpp`: the four neutral entity-VS writes deleted (the original black-silhouette defect, reintroduced) | rc=1, 4 FAIL | `body pure-black share 0.539 > 0.35 (silhouette again)` |
| m7 | `native/src/main.cpp`: the sampling request self-assigned, so `--player-animation` changes nothing the user sees | rc=1, 41 FAIL | `animation-off moved 0.00% < 5.0% of body pixels` (the two frames became pixel-identical: `differ=0`), and the per-entity band follows: `subject seq=-1 but its own model pick for moving is 147`, `no drawn player body carries a drive state and a skinned sequence` |
| m8 | `native/src/main.cpp`: **one token** at the selector call site -- the moving arm asks for `Substring` instead of `WholeToken`, which is the pre-T61.1 matcher and drops the leading-token preference as a consequence | rc=1, 3 FAIL | `a moving pick is an air-walk clip: PRIMARY_airwalk_reload_start ReloadAirwalk_PRIMARY ReloadAirwalk_SECONDARY a_PRIMARY_reload_airwalk a_SECONDARY_reload_airwalk layer_reload_airwalk_arms_SECONDARY [T61.1 regression -- the needle matched a substring again]` -- all six spellings return in one run -- plus `a moving pick is not a run cycle` and `the rendered scene reports reason=PRIMARY_airwalk_reload_start` |
| m9 | `native/include/animation_sequence_match.h`: `const bool primary = wholeToken && sequenceLabelPrimaryTokenMatches(...)` becomes `const bool primary = wholeToken;` -- the boundary survives, only the first-token preference goes | rc=1, 1 FAIL | `a moving pick is not a run cycle: armslayer_PDA_run_fire layer_AttackStandMelee_runS_arms layer_AttackStandMelee_runW_arms layer_gesture_primary_help_runN_armL [the rule must prefer the label whose first token is the needle]` -- one red, and it is exactly the one this case owns (`OK no moving pick is an air-walk clip` stays green, as it must: the boundary is intact) |
| m10 | `native/src/main.cpp`: `chosen` swaps the idle and moving branches, so the pick line still says `run_PRIMARY` while the body is drawn from `AttackStand_PRIMARY` | rc=1, 37 FAIL | `the rendered scene reports reason=AttackStand_PRIMARY, which is none of the picks for the drive state animsum reports moving=2 idle=0 (run_PRIMARY run_SECONDARY ): the pick and the picture disagree`, then the per-entity band names each affected body (`ent=9 models/player/soldier.mdl moving sampled seq=10 label=run_PRIMARY but its pick is seq=201 label=run_PRIMARY`) |
| m11 | `native/src/main.cpp`: the sampling request overridden to the idle sequence on the capture frame only, so every pick line, the scene reason and the counters all stay correct and **one body** is drawn from the wrong clip | rc=1, 5 FAIL | `subject seq=9 but its own model pick for moving is 147`, `subject label=AttackStand_PRIMARY but its own model pick for moving is run_PRIMARY`, `ledger id=95 ent=5 models/player/demo.mdl moving sampled seq=9 label=AttackStand_PRIMARY but its pick is seq=147 label=run_PRIMARY`, `ent=5 models/player/demo.mdl moving sampled seq=9 label=run_PRIMARY but its pick is seq=147 label=run_PRIMARY`, and the summary `the per-entity chain failed ... 4 named reason(s)` |
| m12 | `native/src/main.cpp`: the entity id collapses to `1 + (draws.size() % 2)`, so every body shares an id with the next -- the ledger claims 96 lines under two ids | rc=1, 3 FAIL | `two bodies share one entity id: the ledger claims an id twice, so the id colour cannot name one body and the subject mask could be read from the wrong body`, over the decoder's `DUP    id=1 kept ent=40 model=models/props_gameplay/resupply_locker.mdl ; dropped ent=49 model=models/props_2fort/lantern001.mdl` and `FAIL 94 ledger line(s) reuse an entity id another body already claims (ids 1, 2)` |

m11 is the case the first review's P1 asked for, and it is the only case in the
suite that the scene-level readings cannot see. It leaves `animsum` unchanged, it
leaves every `anim: pick` line unchanged, and it leaves the scene reason unchanged
-- the substitution happens inside one instance's request, on the capture frame.
What reddens is the per-entity chain: the F5 reading compares the sequence each
drawn body **sampled** against the sequence the pick table resolved **for that
model**, so a body drawn from `AttackStand_PRIMARY` while its model's moving pick
is `run_PRIMARY` (seq 147) is named, with the entity index and the model path.
One of the five reds carries `label=run_PRIMARY` next to `seq=9`: the label is
read back out of the sequence table **by index**, so it names whatever the table
says at index 9, not what was requested -- which is exactly why the per-entity
check compares `seq` and not only a label.

m12 is the case P1 of the third review asked for, and it changes nothing the
picture shows: the pixel readings stay green (`PIXEL-CHECK=PASS`, `body pure-black
share 0.100`), because the mutation is inside the *id* the decoder reads, not in
what is drawn. What reddens is the per-entity chain's first stage -- the decoder
refuses a ledger in which one id names two bodies before it reads one pixel of the
id frame. The first run of this case reddened for the wrong reason, a `TypeError`
inside the refusal itself (8.8 records the repair), and that run's log is kept as
`m12.log.crash-before-sort-fix`: the case found a defect in the fix, which is the
reason it exists.

Every case restored byte-identically (`cmp -s`), every rebuild was rc=0 with 0
`error C` (a mutant that does not compile is INVALID, not caught), and the suite
ends on a green baseline after the last restore: `baseline` and `final` both read
**28 OK / 0 FAIL** against the round-6 gate, and the seven mutant logs read 25/4,
23/41, 25/3, 27/1, 26/37, 27/5, 27/3.

**Third run, 2026-10-27 (this review).** The suite was re-run end to end from a
clean check of the tree, as one job, with no source edit while it ran: baseline,
six cases, final baseline, 1 h 44 min. It reproduced every number above exactly --
baseline 28/0, `m6` 25/4, `m7` 23/41, `m8` 25/3, `m9` 27/1, `m10` 26/37, `m11`
27/5, final 28/0, each named reason on a `  FAIL ` line, every restore
byte-identical, `MUTATION-SUITE=PASS` -- and the source diffstat was
`7 files changed, 733 insertions(+), 102 deletions(-)` both before and after, so
the suite measured the tree under review rather than a tree it left behind.

**Fourth run, 2026-10-27 (this round, after P1 and P2).** `m11` and `m12` were
re-run together from a fresh baseline after the decoder and the gate changed, so the
two cases whose evidence the fixes touch are measured against the fixed gate:
baseline 28/0, `m11` 27/5 (the same five per-entity reds as above), `m12`
27/3 (the three refusal lines below), final 28/0, `MUTATION-SUITE=PASS`,
one job, no source
edit while it ran. `m6`-`m10` were not re-run: their mutants touch
`native_renderer.cpp` and `main.cpp` only, and the fix adds no assertion that can
fire on any of their failures, so their numbers stand as measured on the same tree
(the diffstat is identical before and after both runs). The first `m12` run -- the
one that found the decoder defect -- is kept as `m12.log.crash-before-sort-fix` and
is **not** the run counted here.

**Fifth run, 2026-10-27 (this round, after the 8.8 hardening).** The traps,
recovery and exit codes changed after the fourth run, so the whole suite was
re-run end to end to show the repair did not disturb the passing path: one job,
16:16-18:09 (1 h 53 min), no source edit while it ran, baseline 28/0 first and
28/0 last, `m6` 25/4, `m7` 23/41, `m8` 25/3, `m9` 27/1, `m10` 26/37,
`m11` 27/5, `m12` 27/3, every named reason on a `  FAIL ` line, all seven
restores `cmp -s` byte-identical, no `TRAP-*`, no `STALE-BINARY`,
`MUTATION-SUITE=PASS`, `SUITE-RC=0`, and the diffstat still
`7 files changed, 846 insertions(+), 111 deletions(-)`. The gate logs under
`evidence/t61-animation/pixel-mutations/` are this run's.

**Sixth run, 2026-10-27/28 (this round, after the 8.9 repair).** The per-case
restore path changed in both harnesses, so the whole suite was re-run end to end
once more: one job, 22:53-00:52 (1 h 59 min), no source edit while it ran,
baseline 28/0 first (`baseline.log`, 23:05:30) and 28/0 last (`final.log`,
00:52:28), `m6` 25/4 (23:18:00), `m7` 23/41 (23:30:57), `m8` 25/3 (23:44:20),
`m9` 27/1 (23:58:42), `m10` 26/37 (00:12:57), `m11` 27/5 (00:26:12), `m12`
27/3 (00:39:28), every named reason on a `  FAIL ` line, all seven restores
`cmp -s` byte-identical, no `TRAP-*`, no `STALE-BINARY`,
`MUTATION-SUITE=PASS`, `PIXEL-SUITE-RC=0`, and the diffstat still
`7 files changed, 911 insertions(+), 118 deletions(-)`. The gate logs under
`evidence/t61-animation/pixel-mutations/` are this run's. The two attempts
before it are recorded rather than deleted: the first (19:08-21:02) reddened
every case exactly as this one does, but its *final baseline* failed on an
environment-level spawn error -- the repeat capture exited 127 with a zero-byte
log, no `animated2.bmp` and no trace, while the other four captures in the same
gate ran normally -- so the three assertions built on that capture reddened and
`MUTATION-SUITE=FAIL`; the gate has no retry by design, because a missing frame
must never be re-rolled into a pass. Its log is
`pixel-mutations/final.log.run6-spawn127`, the suite log
`suite-run6-spawn127.log`. The second attempt (21:05-22:04) was stopped
mid-`m9` by an operator order; the hard kill left the mutant source and the
mutant binary behind, and 8.9 records both the repair and what it taught about
the freshness check.


m8 and m10 each exposed a defect in **the harness**, and both are the same
species: a check that could be satisfied by something other than the failure it
names.

1. *m8 took three forms, and the first two were the wrong mutant.* The first
   rewrote only the leading-token preference, so the whole-token boundary still
   rejected `airwalk`: the gate's own line `OK no moving pick is an air-walk clip`
   stayed green and the only red was the run-cycle assertion -- which is what m9
   exists to prove. Removing one of the two rules on its own is masked by the
   other. The second replaced the whole matcher header, which proves only that a
   mangled matcher is caught, and the second review rejected it as INVALID: it is
   not the regression, and it cannot show that the call site is wired to the mode
   it names. The third (the one reported above) changes **one argument** at the
   call site, so the defect returns through the path that carried it.
2. *The named-reason check matched a `PASS` line.* It ran `grep -qF "$pattern"
   "$log"` over the whole gate log, so m8's expectation `air-walk clip` was
   satisfied by the green line `OK   no moving pick is an air-walk clip` while the
   real failure was about run cycles. A reason now counts only when it appears on
   a line beginning `  FAIL `, and m10 is the case that proves the difference:
   before the fix its earlier form printed `OK named PIXEL reason present` on a
   run that had `rc=0 FAIL-count=0`.

m10 was also re-aimed. Its first form rewrote `runtime.sequenceLabel`, which
cannot move the reading it was supposed to move: the `scene: anim reason=` field
is filled from `animation_binding.cpp`'s `result.sequenceLabel = sequence.label;`
-- the label read back **out of the sequence table by index** -- not from the
runtime variable. Swapping `chosen` instead makes the pick line and the drawn
sequence disagree, which is the only shape the scene assertion can see; and m10 is
why that assertion was changed from "does not contain *airwalk*" to "is one of
the picks resolved for the drive state `animsum` reports". Forbidding one wrong
word passes for every other wrong clip.

**m6 took three runs, and both rejected runs are kept.**

1. *First run* (`.scratch/t61-pixmut.launch.log`): the gate went red and
   `body pure-black share` matched, but the expectation list also named
   `flat silhouette` -- the distinct-colours assertion. Measured, m6 does not
   reproduce the original frame exactly: the unwritten interpolant is *undefined*
   rather than a constant, so the mutant frame has 537 distinct body colours and
   an 84.0% black share, not one flat colour. The assertion that names the defect
   is the black-share line, whose own text is "(silhouette again)". Kept as
   `m6.log.first-expectation`, `baseline.log.run1`, `final.log.run1`.
2. *Second run*: correcting that name, I put the explanatory comment **inside**
   the shell line continuation, so `run_case` received four arguments instead of
   six and its expectation list became empty. The `for pattern in "$@"` loop then
   printed `MUTATION-SUITE=PASS` without checking anything, two lines under
   `body pure-black share: command not found` -- `set -uo pipefail` does not catch
   a lost argument. Kept as `m6.log.run2-no-guard`, `baseline.log.run2`.
3. *Third run* (the one reported here): the note moved above the call, the
   expectations corrected, and `run_case` now **fails a case whose expectation
   list is empty** -- an assertion that never executes is invisible, because it is
   the one that would have printed the failure.

That distinction is also why the pixel gate asserts a **share** and not merely
"the body is not one colour": removing the neutral writes degrades the body instead
of flattening it, and a gate that only demanded "not flat" would have stayed green
on a frame that is 84% black.

**Seventh run, 2026-10-28 (this round, after the 8.10 repairs).** The restore
state machine changed once more -- a failed `touch` and the two build-failure
paths now refuse -- so the whole suite was re-run end to end a seventh time: one
job, 09:52-11:46 (1 h 54 min), no source edit while it ran, baseline 28/0 first
(`baseline.log`, 10:04:37) and 28/0 last (`final.log`, 11:46:07), `m6` 25/4
(10:17:04), `m7` 23/41 (10:29:28), `m8` 25/3 (10:42:09), `m9` 27/1 (10:55:28),
`m10` 26/37 (11:08:47), `m11` 27/5 (11:21:19), `m12` 27/3 (11:33:47), every
named reason on a `  FAIL ` line, all seven restores `cmp -s` byte-identical, no
`TRAP-*`, no `STALE-BINARY`, no `CASE-RESTORE-*`, no `REFUSAL`,
`MUTATION-SUITE=PASS`, `SUITE-RC=0`, and the tracked diffstat still
`7 files changed, 962 insertions(+), 122 deletions(-)`. The probe was last
rebuilt by the unit suite (09:49:11, 865,280 bytes) and the product by the last
case's post-restore rebuild (11:34:02, 2,657,280 bytes).

**Eighth run, 2026-10-28 (this round, after the review-7 object proof).** The
restore state machine gained its last requirement -- after a restore the objects
that depend on the restored file must be *proven* recompiled -- so the pixel
suite was run end to end an eighth time: one job under the host lock,
13:45:31-15:43:00 (1 h 57 min), no source edit while it ran, baseline 28/0 first
(`baseline.log`, 13:57:42) and 28/0 last (`final.log`, 15:42:59), `m6` 25/4
(14:10:50), `m7` 23/41 (14:23:07), `m8` 25/3 (14:35:35), `m9` 27/1 (14:48:55),
`m10` 26/37 (15:02:16), `m11` 27/5 (15:15:38), `m12` 27/3 (15:29:02). Every case
printed both `  OK   restored byte-identical (cmp -s)` and `  OK   every object
that depends on a restored file, plus the product, is newer than it (the
recompile is proven)` -- seven of each -- with no `TRAP-*`, no `STALE-*`, no
`CASE-RESTORE-*`, no `REFUSAL`, `MUTATION-SUITE=PASS` and `PIXEL-SUITE-RC=0`.
The tracked diffstat was still `7 files changed, 1120 insertions(+), 130
deletions(-)` after the suite, and the product was left by the last case's
post-restore rebuild (15:30, 2,657,280 bytes); this suite never touches the
probe, which still carries the unit suite's 13:43 build (865,280 bytes). The
first attempt at this run refused instead of passing, because the object proof
itself was broken then -- 8.11.1 has that episode, and the one-line cause.

**Ninth run, 2026-10-28 (this round, after the 8.12 clean-exit proof).** The
clean-exit proof changed `on_exit` on both harnesses, so the pixel suite was run
end to end a ninth time: one job under the host lock, 19:00:01-21:10:34 (2 h
10 min), no source edit while it ran, baseline 28/0 first (`baseline.log`,
19:12:41) and 28/0 last (`final.log`, 21:10:21), `m6` 25/4 (19:24:51), `m7`
23/41 (19:37:38), `m8` 25/3 (19:50:40), `m9` 27/1 (20:05:20), `m10` 26/37
(20:20:41), `m11` 27/5 (20:37:21), `m12` 27/3 (20:55:37). Every case printed
both `  OK   restored byte-identical (cmp -s)` and `  OK   every object that
depends on a restored file, plus the product, is newer than it (the recompile is
proven)` -- seven of each -- every named PIXEL reason appeared on a `  FAIL `
line, and the harness ended with `MUTATION-SUITE=PASS`, no `TRAP-*`, no
`STALE-BINARY`, no `CASE-RESTORE-*`, no `REFUSAL`, and the clean-exit proof
itself: `  OK   clean exit: every restored source still matches its backup, and
every object that depends on it plus the product is newer than it (nothing was
touched after the proof)`. The tracked diffstat was still `7 files changed, 1163
insertions(+), 130 deletions(-)`, the product was left by the last case's
post-restore rebuild (20:57:22, 2,657,280 bytes), and the probe still carried the
unit suite's 18:56:20 build (865,280 bytes).

One ordering fact this run exposed, because the runner executes the focused
gates *after* the mutation suites. Every product rebuild that is triggered by a
header makes `build-target.sh:124-135` run `touch native/src/*.cpp
native/tools/*.cpp`, so `native/tools/model_pose_probe.cpp` ends up newer than
the probe binary although its bytes never changed -- and the check gate's first
assertion then fails, truthfully, with `FAIL STALE-BINARY=1 probe=... older
than .../native/tools/model_pose_probe.cpp`. The repair is a rebuild, not a
source edit: `bash build-target.sh model_pose_probe tf2_demo_native` (21:25:15,
probe 865,280 bytes, product unchanged at 20:57:22), then the gates again --
check 232 assertions / 0 FAIL (`r12-gates/check-after-rebuild.log`) and pixels
28 / 0 (`r12-gates/pixels-after-rebuild.log`), both `=PASS`. A plain
`verify-all.sh` run cannot hit this: it builds the tree and gates it before
anything mutates a header.

### 8.4 What these gates do not cover

- The pixel gate measures **one class at one camera pose**; the nine-class sweep is
  counter-level (stage 2). Section 9 keeps that distinction.
- The mask is the bind-pose region (section 6.1), so the pixel assertions are
  conservative: they cannot see a driven pixel outside the bind silhouette.
- Neither gate claims the pose **matches the server's** -- the demo does not record
  `m_nSequence`, so there is nothing to compare against. They claim the chain moves
  the body, within bounds, reproducibly.

### 8.5 The gates this change could have broken

The entity vertex shader is shared by every model draw, so "the animation gate is
green" does not by itself prove the model path survived. Both pre-existing gates
were re-run on this tree after the fix, under the host lock:

| gate | result |
|---|---|
| `player-model-check.sh` (T60) | rc=0, **167 OK / 0 FAIL** -- negative controls named-failed, A `lod0Truncated=0`, B painted slots per slot x family, C `separateAnimSource=1` |
| `animation-availability-check.sh` | rc=0, **23 OK / 0 FAIL**, `ANIMATION-AVAILABILITY=PASS` |

Three probes (`entity_model_probe`, `entity_overlay_probe`,
`model_robustness_probe`) had to be built first: this worktree had only ever built
the two targets the animation work needed, and a missing input is a FATAL, not a
skip.

### 8.6 The second review's three findings, and what each one changed

The second review accepted the idle-selection restore and the F5 per-entity chain,
and rejected the suite on three counts. All three are fixed in this tree, and none
of the fixes was a threshold move.

**1. m8 and m9 were invalid mutants.** m8 rewrote the whole matcher header, which
proves only that a mangled matcher is caught; m9 replaced the preference with
`false`, which is not the regression either. Both are now the smallest edit that
restores the actual defect:

| case | now patches | which is |
|---|---|---|
| m8 | `native/src/main.cpp`, one token at the selector call site: the moving arm asks for `Substring` | the pre-T61.1 matcher, and it switches the leading-token preference off as a consequence (`primary = wholeToken && ...`) |
| m9 | `native/include/animation_sequence_match.h`: `const bool primary = wholeToken && sequenceLabelPrimaryTokenMatches(...)` becomes `const bool primary = wholeToken;` | the boundary survives, only the first-token preference goes |

m8 now also proves the call site is **wired** to the mode it names: a selector that
ignored its `mode` argument would leave the mutant green.

**2. The selector and the ranker disagreed about order (P1).**
`chooseSequenceByNeedles` (the product's pick) is sequence-major;
`rankSequenceCandidates` (the probe's fallback walk) nested the needle loop
*outside* the index loop, so it put "the first sequence matching needle 0" ahead of
an earlier sequence matching needle 1. A probe walking that list could try -- and
then report a pose for -- a sequence the product would never have chosen first.
The ranker is now sequence-major, its doc comment (which had claimed "the same
nesting the selector uses" while doing the opposite) is true, and the property is
pinned by `model_pose_probe --matcher-selftest`, which is run by a new section
`1b/6` of `player-animation-check.sh`: eleven assertions, including the rank
order **by value** (`0,3,1,4` on a table built so the two nestings disagree -- the
real player tables make them coincide, which is why a green pixel gate never saw
this). The probe's `--self-test` had existed for weeks with **no caller**; that is
recorded as its own lesson, because a unit test no gate runs is a comment. The
preference now has a mutant as well: `u1` in section 8.7 reddens `matcher leading`
and `matcher leading-token pick` on the same one-token edit m9 makes.

**3. The identity frame's clock assertions were permissive (P2), and the id space
was unbounded.** The guard was `[ -n "$id_ft" ] && [ "$id_ft" != "$TICK" ]`, so a
capture that published **no** `frame_tick` passed; `scene_tick` was extracted,
printed, and never asserted. Both are now mandatory and the scene tick must equal
the animated capture's, because the identity frame is compared pixel-for-pixel
against that frame. The id itself moved into `native/include/entity_id_color.h`
with a named ceiling (`kEntityIdMax = 255`) and an encoder that **refuses** rather
than clamps; the product exits **17** (unused: 0 success, 2/3/4 device loss, 13
argument refusals, 16 capture failed) instead of reporting a frame in which two
bodies share a colour. That refusal is unreachable from a scene -- the draw budget
caps the id at 96 -- so it is tested where the decision lives, by a new
`--entity-id-selftest` and eight assertions in section `1c/6`. Proven to have
teeth by replacing the refusal with a clamp: `over=0`, rc=1 -- and that one-off
demonstration is now a standing case, `u2` in section 8.7.

**4. Executing the pins found a defect in the release chain itself.** The pin
edits above were verified by extracting steps 25 and 26 **verbatim** out of
`verify-all.sh` (`sed -n` between the two `step "N/30"` banners) and running
them, rather than by retyping the assertions. Step 25 printed
`rc=0`, `ok=232`, `expected 232`, and **FAIL**. The middle term of its `&&`
chain was `grep -q 'PLAYER-ANIMATION-CHECK=PASS'`, and `player-animation-check.sh`
has never printed that string: `git log -S 'PLAYER-ANIMATION-CHECK=PASS' --
player-animation-check.sh` returns nothing, while the same search on
`verify-all.sh` returns exactly `02610db`, the commit that added the step. The
step had therefore been permanently red since it was written, and no full chain
ran between then and now. Both gates now print `NAME=PASS` / `NAME=FAIL` on
their last line and both steps grep that token; step 26 moved off the inner
tool's `PIXEL-CHECK=PASS` for the same reason one level down. Re-run of the two
extracted steps on this tree: `PLAYER-ANIMATION-CHECK=PASS (232 assertions)` and
`PLAYER-ANIMATION-PIXELS=PASS (28 assertions)`.

`verify-all.sh` step 25 moved **213 -> 232** assertions and step 26 moved **26 ->
28**, both pinned, so neither new section can silently stop running.

### 8.7 The unit-section mutation suite (u1-u10)

The pixel suite mutates `native_renderer.cpp` and the two animation sources and
runs `player-animation-pixels.sh`. It never runs `player-animation-check.sh`, and
the nineteen assertions added in review 2 -- eleven matcher assertions in section
`1b/6`, eight entity-id assertions in `1c/6` -- live in that gate. Round 6's
evidence for them was a forward pass plus the one-off clamp demonstration quoted in
8.6: a demonstration, not a case in any suite, which rots as soon as the harness or
the anchors move. `player-animation-unit-mutations.sh` (new in this tree) turns
both into standing cases. It rebuilds **the probe** with each mutant, runs the check
gate, and requires

- rc != 0 and a FAIL count above the 232-line baseline;
- each named reason on a line beginning `  FAIL ` -- the 8.3 lesson, inherited;
- **no** `STALE-BINARY=1` in the log: the gate refuses a probe older than its
  headers, so a unit mutant that skipped the rebuild would redden for freshness
  instead of for its defect. The mutant has to reach the assertion, not the guard;
- a byte-identical restore (`cmp -s`), a rebuild after it, and a green final
  baseline.

| case | mutation | gate | named red |
|---|---|---|---|
| u1 | `native/include/animation_sequence_match.h`: the same one-token edit as m9 -- the two-line `primary` test becomes `const bool primary = wholeToken;` | rc=1, 3 FAIL | `matcher leading`, `matcher leading-token pick`. The selftest line captured **under the mutant** reads `leading=0 ... leadingPick=0` while `wholeTokenPick=1` and `ranked=0,3,1,4` are unchanged: the boundary survives, only the preference dies |
| u2 | `native/include/entity_id_color.h`: the refusal becomes a clamp (`id > kEntityIdMax` -> `id = kEntityIdMax`) | rc=1, 2 FAIL | `entity id over`. The line under the mutant reads `over=0` with `maxId=255` still printed |

Beyond those two, the round-8 coverage finding (8.12) added eight cases, one for
each assertion u1/u2 left green. Each is one literal edit, the same
build/run/expect/restore/rebuild sequence, and the FAIL count is what the r12 run
measured:

- **u3** -- `rankSequenceCandidates` walks the needles as the outer loop (the
  needle-major nesting review 2 named): 4 FAIL -- `matcher ordered`, `matcher
  head`, `matcher ranked order = 3,0,1,4, expected 0,3,1,4`, and the exit.
- **u4** -- `if (primary && animatedPrimary < 0)` becomes `if (primary)`, so the
  last primary match wins: 4 FAIL -- `matcher head`, `matcher selector agrees with
  rank head = 3, expected 0`, the exit, and the model gate's ` B medic maxDelta`
  bound, which this selector change also moves.
- **u5** -- the selector's `const bool wholeToken = mode ==
  SequenceMatchMode::WholeToken;` becomes `= false`: 5 FAIL -- `matcher
  boundary`, `matcher whole-token pick`, `matcher leading`, `matcher
  leading-token pick`, and the exit.
- **u6** -- the same line becomes `= true`, so Substring mode is forgotten: 3
  FAIL -- `matcher substringDefect`, `matcher substring pick = 1, expected 0`,
  and the exit.
- **u7** -- the encoder divides by `kEntityIdMax - 1`: 4 FAIL -- `entity id
  one`, `entity id max`, `entity id injective`, and the exit.
- **u8** -- the ceiling is written into the channel instead of refused: 2 FAIL --
  `entity id over` and the exit.
- **u9** -- the lower bound is removed, so 0 and -3 count as ids: 3 FAIL --
  `entity id zero`, `entity id negative`, and the exit.
- **u10** -- `kEntityIdMax` becomes 254: 3 FAIL -- `entity id injective`,
  `entity id ceiling = 254, expected 255`, and the exit.

The per-assertion mapping -- all nineteen labels, the case that reddens each, and
the measured line -- is the coverage table later in this section.

`baseline` **232 OK / 0 FAIL**, `u1` 229/3, `u2` 230/2, `final` 232/0,
`UNIT-MUTATION-SUITE=PASS` (2026-10-27 12:07-12:16). The logs are in
`evidence/t61-animation/unit-mutations/`, and the mutant-time probe lines are kept
as `u1.matcher-selftest.txt` and `u2.entity-id-selftest.txt`, because the gate
rewrites its own copies on every run -- without that the evidence for "the
assertion saw the mutant" would survive only as the FAIL lines in the gate log.

**Re-run, 2026-10-27 14:21-14:30 (this round, after the P1/P2 edits).** The suite ran
again end to end on the fixed tree: baseline 232/0, `u1` rc=1 with **3** FAIL
(`matcher leading`, `matcher leading-token pick`, `matcher selftest exit`), `u2`
rc=1 with **2** FAIL (`entity id over`, `entity id selftest exit`), final 232/0,
`UNIT-MUTATION-SUITE=PASS`, `SUITE-RC=0`, every restore byte-identical and no
`STALE-BINARY=1`. Same numbers as the 12:07 run.

**Re-run once more, 2026-10-27 15:59-16:07, after the 8.8 hardening.** The trap and
the exit-code rule changed between the 14:21 run and this one, and a passing run is
the path they must not disturb: baseline **232/0**, `u1` rc=1 with 3 FAIL, `u2`
rc=1 with 2 FAIL, final **232/0**, `UNIT-MUTATION-SUITE=PASS`, `SUITE-RC=0`, every
restore `cmp -s` identical, no `STALE-BINARY=1`, and the exit code stayed 0 (the
new rc-3 rule fires only when the tree could not be left clean). The logs and the
under-mutant selftest dumps in `evidence/t61-animation/unit-mutations/` are this
run's.

**Re-run again, 2026-10-27 19:00-19:08, after the per-case refusal repair
(8.9).** The per-case restore is now a gate, so the passing path was measured
again on the repaired harness: baseline **232/0** (19:00:14), `u1` rc=1 with 3
FAIL (19:02:38), `u2` rc=1 with 2 FAIL (19:06:24), final **232/0** (19:08:50),
`UNIT-MUTATION-SUITE=PASS`, `SUITE-RC=0`, every restore `cmp -s` identical, no
`STALE-BINARY`, no `CASE-RESTORE-*` line and the final baseline *was* measured
-- the refusal branch is only reachable when a restore fails. The logs in
`evidence/t61-animation/unit-mutations/` are this run's.

**Re-run after the 8.10 repairs, 2026-10-28 09:41-09:51.** Both
`restore_case_sources()` and the two build-failure paths changed again (8.10), so
the passing path was measured once more on the repaired harness: baseline
**232/0** (09:41:13), `u1` rc=1 with 3 FAIL (09:44:21), `u2` rc=1 with 2 FAIL
(09:48:53), final **232/0** (09:51:33), `UNIT-MUTATION-SUITE=PASS`,
`SUITE-RC=0`, every restore `cmp -s` identical, no `STALE-BINARY`, no
`CASE-RESTORE-*` line. The logs in `evidence/t61-animation/unit-mutations/` are
this run's.

**Re-run again with the review-7 proof, 2026-10-28 13:35-13:45.** The harness
changed once more in 8.11 -- every restore now has to *prove* the recompile (the
restored file newer than `.headers-stamp`, every object CMake says depends on it
newer than that file, and the probe and the product newer than every restored
file), and 8.11.1 fixed the CRLF parse in that proof -- so the suite was measured
again on the final text: baseline **232/0** (13:36:31), `u1` rc=1 with 3 FAIL on
the gate's `  FAIL ` lines (case log 13:39:06, restore `cmp -s` identical, then
`every object that depends on a restored file, plus the probe and the product, is
newer than it (the recompile is proven)`), `u2` rc=1 with 2 FAIL (case log
13:43:02, same object proof), final **232/0** (13:45:30),
`UNIT-MUTATION-SUITE=PASS`, **`UNIT-SUITE-RC=0`** (13:45:31). The two mutated
sources were compared against their backups afterwards
(`U1-SOURCE-ORIGINAL`, `U2-SOURCE-ORIGINAL`), and the rebuilds left the probe at
865,280 bytes (13:43:18) and the product at 2,657,280 bytes (13:44:21). Logs:
`evidence/t61-animation/r10-unit-mutations/{baseline,u1,u2,final}.log`.
**The review-8 run, 2026-10-28 18:06-19:00 (r12).** Review 8 measured that only
five of the nineteen assertions this task adds were reddened by u1/u2, so this
round added eight cases -- u3-u10 -- until every one of the nineteen has a mutant
that reddens it, and re-ran all ten in one job under the host lock: baseline
**232/0** first (18:07:03, `baseline.log`), `u1` 3 FAIL (18:09:24), `u2` 2 (18:13:27),
`u3` 4 (18:17:20), `u4` 4 (18:21:59), `u5` 5 (18:27:47), `u6` 3 (18:33:37),
`u7` 4 (18:39:06), `u8` 2 (18:44:46), `u9` 3 (18:50:42), `u10` 3 (18:55:58),
final **232/0** (18:59:30), `UNIT-MUTATION-SUITE=PASS`, **`UNIT-SUITE-RC=0`**
(19:00:01). Every case printed `  OK   restored byte-identical (cmp -s)` and
`  OK   every object that depends on a restored file, plus the probe and the
product, is newer than it (the recompile is proven)`; there was no
`STALE-BINARY`, no `CASE-RESTORE-*` and no `REFUSAL`. The **whole harness
stdout**, not only the gate logs, is kept as
`evidence/t61-animation/r12-unit-mutations/harness.stdout.log` (64,289 bytes,
573 lines): the per-case verdicts, the final baseline line and the
`UNIT-MUTATION-SUITE=PASS` line are in it, which is the evidence review 8's
second P2 found missing from the r10 run. The gate logs are
`r12-unit-mutations/{baseline,u1..u10,final}.log`; the r10 logs are kept beside
them, not overwritten.

**Coverage, one row per added assertion.** Review 2 added nineteen assertions --
eleven in section `1b/6` (the matcher) and eight in `1c/6` (the entity-id
encoder). This table names, for each one, the case whose mutation reddens it and
the FAIL line the r12 run measured. The harness anchors each label to a line that
begins `  FAIL ` (not a bare substring: a passing line carrying the same words
cannot satisfy it), so the row is the measurement, not a reading of the log.

| assertion (gate label) | expected | red under | the FAIL line measured in r12 |
|---|---|---|---|
| `matcher selftest exit` | 0 | u1, u3, u4, u5, u6 | `  FAIL matcher selftest exit = 1, expected 0` (u1) |
| `matcher ordered` | 1 | u3 | `  FAIL matcher ordered = 0, expected 1` (u3) |
| `matcher head` | 1 | u3, u4 | `  FAIL matcher head = 0, expected 1` (u3) |
| `matcher boundary` | 1 | u5 | `  FAIL matcher boundary = 0, expected 1` (u5) |
| `matcher substringDefect` | 1 | u6 | `  FAIL matcher substringDefect = 0, expected 1` (u6) |
| `matcher leading` | 1 | u1, u5 | `  FAIL matcher leading = 0, expected 1` (u1) |
| `matcher ranked order` | `0,3,1,4` | u3 | `  FAIL matcher ranked order = 3,0,1,4, expected 0,3,1,4` (u3) |
| `matcher selector agrees with rank head` | 0 | u4 | `  FAIL matcher selector agrees with rank head = 3, expected 0` (u4) |
| `matcher whole-token pick` | 1 | u5 | `  FAIL matcher whole-token pick = 0, expected 1` (u5) |
| `matcher substring pick` | 0 | u6 | `  FAIL matcher substring pick = 1, expected 0` (u6) |
| `matcher leading-token pick` | 1 | u1, u5 | `  FAIL matcher leading-token pick = 0, expected 1` (u1) |
| `entity id selftest exit` | 0 | u2, u7, u8, u9, u10 | `  FAIL entity id selftest exit = 1, expected 0` (u2) |
| `entity id one` | 1 | u7 | `  FAIL entity id one = 0, expected 1` (u7) |
| `entity id max` | 1 | u7 | `  FAIL entity id max = 0, expected 1` (u7) |
| `entity id over` | 1 | u2, u8 | `  FAIL entity id over = 0, expected 1` (u2) |
| `entity id zero` | 1 | u9 | `  FAIL entity id zero = 0, expected 1` (u9) |
| `entity id negative` | 1 | u9 | `  FAIL entity id negative = 0, expected 1` (u9) |
| `entity id injective` | 1 | u7, u10 | `  FAIL entity id injective = 0, expected 1` (u7) |
| `entity id ceiling` | 255 | u10 | `  FAIL entity id ceiling = 254, expected 255` (u10) |

**The ten cases, and the edit each one makes.** Each case is one literal
find-and-replace in the header the probe is built from, then the same
build-run-expect-restore-rebuild sequence; the `reds` column is the FAIL count
the r12 run measured, which for u4 includes one assertion outside the two new
sections (below).

| case | file | the edit | reds |
|---|---|---|---|
| u1 | `native/include/animation_sequence_match.h` | `const bool primary = wholeToken && sequenceLabelPrimaryTokenMatches(candidate, needleText)` -> `const bool primary = wholeToken;` (the boundary kept, the preference dropped) | 3 |
| u2 | `native/include/entity_id_color.h` | the ceiling refuses -> clamps to `kEntityIdMax`, so 256 and 255 share one colour | 2 |
| u3 | `native/include/animation_sequence_match.h` | `rankSequenceCandidates` walks the needles as the OUTER loop (the needle-major nesting review 2 named) | 4 |
| u4 | `native/include/animation_sequence_match.h` | `if (primary && animatedPrimary < 0) {` -> `if (primary) {` (the LAST primary wins, not the first) | 4 |
| u5 | `native/include/animation_sequence_match.h` | the selector's `const bool wholeToken = mode == SequenceMatchMode::WholeToken;` -> `= false` (the whole-token rule dropped) | 5 |
| u6 | `native/include/animation_sequence_match.h` | the same line -> `= true` (Substring mode forgotten) | 3 |
| u7 | `native/include/entity_id_color.h` | the encoder divides by `kEntityIdMax - 1`, so id 1 no longer maps to 1/255 and id 255 maps past 1.0 | 4 |
| u8 | `native/include/entity_id_color.h` | the ceiling is written into the channel instead of refused | 2 |
| u9 | `native/include/entity_id_color.h` | the lower bound `id < 1` is removed, so 0 and -3 are ids | 3 |
| u10 | `native/include/entity_id_color.h` | `inline constexpr int kEntityIdMax = 255;` -> `= 254;` (the ceiling off by one) | 3 |

u4's fourth red is `  FAIL B medic maxDelta cycle=0.0000 = 159.109039, expected <=
158.545471` -- the model gate's own bound, not one of the nineteen: moving the
selector to the last primary match changes which clip the probe reports, and that
is visible in the model sections too. The case names the three assertions it was
built for, and the harness tolerates the extra red (the check is that each named
label is present, not that nothing else is).

**The release policy, applied.** `docs/RELEASE-POLICY.md:12-13` requires a
mutation for every assertion a task ships and that the mutation must turn the
assertion red for the right reason. For the nineteen assertions this task adds,
the table above is that evidence: every label has at least one case whose edit
reddens it by name, the reds were measured in the r12 run, and the forward pins
(`verify-all.sh` step 25, 232) keep the same nineteen green on the unmutated
tree. The final baseline at **232/0** after ten mutants is the other half: the
tree was left exactly as found. What the table does not claim is that each
assertion has a mutant that reddens *only* it -- u1 and u5 share the leading
readings, u7 and u10 share injectivity -- and that is deliberate: the mutations
are one-line edits to real code, not nineteen synthetic sabotages.

Two things this suite taught, both kept in the harness:

1. **u1 corrected my expectation, not the code.** I first demanded three reds, the
   third being `matcher selector agrees with rank head`. That one stays **green**
   under u1, and correctly: the synthetic table's index 0 (`walk_SECONDARY`) is
   already the first whole-token match in index order, so `chosen` is 0 before and
   after the mutation and the rank head still agrees with the pick. The first run
   therefore ended `UNIT-MUTATION-SUITE=FAIL` on an assertion the mutant cannot
   break -- the harness refusing to count a green line as evidence, which is the
   behaviour I want from it. The case now names the two assertions the mutant really
   breaks, and the comment in the script records the rejected expectation.
2. **"The selector agrees with the rank head" was never the property.** Review 2's
   finding was that the *preference* -- the rule that stops an arm-layer clip merely
   containing `run` from beating a real cycle -- was absent from the order.
   `matcher leading` reads that preference directly, so m9 and u1 are the two ends of
   one edit: m9 proves the drawn frame changes, u1 proves the specific assertions
   red rather than the selftest merely exiting 1.

The m1-m5 counter suite still does not red these nineteen -- it mutates the clock
and the binding only, which is what 8.2 says from the other side -- so the three
suites are complementary rather than redundant: m1-m5 the counter sections, m6-m12
the pixel readings, u1-u10 the two new unit sections, one case for every assertion
that needed one.

### 8.8 The third and fourth reviews' findings

Review 3 returned **do not submit yet** with two findings and two accuracy items;
review 4 then returned **do not submit yet** again, having found three holes in
the P2 repair plus one signal it did not cover. Everything is repaired here and
every repair is *exercised* rather than asserted. The two accuracy items -- what the unit cases actually prove, and the commit
manifest -- are answered in 8.7 and in section 10.

**P1: a repeated entity id was silently dropped.** The decoder keyed the ledger by
`id`, and on a repeat kept the first line and `continue`d. Two bodies painted one
colour would therefore make the second body's pixels read as the first body's: a
per-entity "pass" that names the wrong body, which is exactly the reading F5 sells.
The decoder now records the collision and refuses. `parse_summary()` returns a
fifth value, `duplicates` (`t61-entityid.py:85-113`), filled at the point the old
code skipped (`t61-entityid.py:100`); `main()` unpacks it (`t61-entityid.py:128`)
and, before reading a single pixel, prints up to eight
`DUP    id=%d kept ent=%s model=%s ; dropped ent=%s model=%s` lines
(`t61-entityid.py:134-141`), then
`FAIL %d ledger line(s) reuse an entity id another body already claims (ids %s):
the id colour names one body twice, so the subject mask could be read from the
wrong body` (`t61-entityid.py:144-147`), `ENTITY-ID=FAIL`
(`t61-entityid.py:148`) and exit 1. The gate gives the refusal its own line --
`two bodies share one entity id: the ledger claims an id twice, so the id colour
cannot name one body and the subject mask could be read from the wrong body`
(`player-animation-pixels.sh:333-338`) -- so a mutant can demand that reading
instead of the generic chain failure; it is fail-only, so the 28-OK pin is
unchanged.

The reading has teeth: **m12** (`player-animation-pixel-mutations.sh`) replaces
`const int entityId = static_cast<int>(draws.size()) + 1;` (`native/src/main.cpp:10122`,
unique) with `const int entityId = 1 + (static_cast<int>(draws.size()) % 2);`, so
every body in the frame alternates between two ids. The case is measured, and its three reds are precisely the three lines this fix
adds: baseline 28/0, then **27 OK / 3 FAIL** -- the decoder's own
`FAIL 94 ledger line(s) reuse an entity id another body already claims (ids 1, 2):
the id colour names one body twice, so the subject mask could be read from the
wrong body`, the gate's
`FAIL two bodies share one entity id: the ledger claims an id twice, so the id
colour cannot name one body and the subject mask could be read from the wrong
body`, and the chain summary
`the per-entity chain failed (see .../pixels/mutations/entityid.txt): 1 named
reason(s)`. The harness accepts the case on the middle line -- a named reason on a
`  FAIL ` line -- and every pixel reading above it stays green, so the mutation
proves the refusal did the catching, not the picture.

m12 also found a defect in the repair itself, kept here because it is the
difference between a refusal and a crash. The first m12 run reddened the gate --
but through `TypeError: '<' not supported between instances of 'dict' and 'dict'`
at the `sorted(duplicates)` line: sorting ties compares the kept and dropped rows,
which are dicts. The synthetic fixture used while writing P1 had exactly one
duplicate line, so no tie was ever compared; the mutant produced 94. The decoder
now sorts by id alone (`sorted(duplicates, key=lambda t: t[0])`), and the same
captured frame then prints the eight `DUP` lines, `... and 86 more duplicated
ledger line(s)`, the named `FAIL 94 ledger line(s) reuse an entity id ... (ids 1,
2)` and `ENTITY-ID=FAIL`; the running game's own ledger
(`evidence/t61-animation/pixels/identity.summary`) still reads
`LEDGER lines=96 distinct-ids=96` and `ENTITY-ID=PASS`. The case was then re-run from a clean baseline on the repaired decoder and passed
its own harness: 27 OK / 3 FAIL, `MUTATION-SUITE=PASS`, the accepted log kept as
`evidence/t61-animation/pixel-mutations/m12.log`, and the crash of the first
attempt kept beside it as `m12.log.crash-before-sort-fix`.

**P2: an interrupted suite could leave a mutant binary behind -- and the first
repair of that gap was not enough.** Review 3 found that the traps restored the
sources only, so an interrupt between a mutant build and its restore left the
product built from the mutant, and a later baseline would have measured it.
Review 4 then attacked the repair itself and found three holes in it plus one
signal it did not cover: a missing backup was skipped as if restored, the delete
before the rebuild was never verified, a failed restore still exited with the
case's code, and only INT/TERM were trapped. What follows is the state machine as
it now stands, branch by branch, and then the proof that each branch runs.

The restore is verified per file (`restore_all`,
`player-animation-unit-mutations.sh:148-189`,
`player-animation-pixel-mutations.sh:166-207`). For each `TOUCHED` entry:

- backup absent -> `TRAP-RESTORE-MISSING-BACKUP <file> (expected <backup>)`
  (`unit:154-158`, `pixel:172-176`);
- backup unreadable -> `TRAP-RESTORE-UNREADABLE-BACKUP <backup>`
  (`unit:160-164`, `pixel:178-182`);
- the copy does not verify (`cmp -s`) -> `TRAP-RESTORE-MISMATCH <file>`
  (`unit:166-173`, `pixel:184-191`);

each setting `RESTORE_FAILED=1`, and a file is `touch`ed only after a verified
copy. Review 4's first finding was the absent-backup case: the old loop skipped
it, so a rebuild could compile unrestored mutant source and still print
`INTERRUPT-RECOVERY=rebuilt`.

The recovery deletes before it rebuilds, and verifies the deletion
(`recover_binaries`, `unit:258-292`, `pixel:276-310`):

- `rm -f` runs with `|| true`, then existence is re-checked; if a binary
  survives -> `INTERRUPT-RECOVERY=NOT-FOR-ACCEPTANCE: a mutant binary could NOT
  be deleted (...)`, a marker file whose text says the binary *survived* the
  delete, `RESTORE_FAILED=1`, and no rebuild (`unit:265-270`, `pixel:283-288`);
- if the restore did not verify, the binaries stay deleted and there is no
  rebuild, because a rebuild at that point would compile the mutant
  (`unit:274-278`, `pixel:292-296`);
- otherwise `build-target.sh ...` rebuilds; success prints
  `INTERRUPT-RECOVERY=rebuilt: sources restored and both binaries rebuilt from
  them, so nothing mutant is left to run` and removes the marker
  (`unit:279-282`; the pixel variant says `...the product rebuilt`,
  `pixel:297-300`); failure prints `INTERRUPT-RECOVERY=NOT-FOR-ACCEPTANCE: ...
  the rebuild FAILED` and writes the marker (`unit:288-291`, `pixel:306-309`).

Exit codes and signals (`on_exit`, `unit:294-324`, `pixel:312-342`). The handler
first `trap - EXIT INT TERM HUP`, restores, recovers when `INTERRUPTED=1` or
`RESTORE_FAILED=1`, and then: if the recovery or the restore failed and the
case's own code was 0, the run exits **3** -- "the suite itself finished, the
tree did not"; otherwise the case's code survives. That is review 4's third
finding. The signals are `INT`/`TERM` -> 130 and `HUP` -> 129 (`unit:327-328`,
`pixel:345-346`), so closing a terminal is covered as well as ^C; review 4's
fourth finding was that only INT/TERM were trapped.

The normal path is untouched: no deletion and no rebuild unless a case was
interrupted or a restore failed, and the last case's own rebuild already leaves
the tree built from restored sources.

**Every branch is executed.** `player-animation-recovery-branches.sh` (in the tree; 549 lines as of this
round) extracts `restore_all`, `restore_case_sources`,
`recover_binaries`, `on_exit` and `run_case` **verbatim** from **both**
harnesses -- `sed` from each function head to its
closing brace -- sources them, and calls the whole `on_exit()` in six isolated
temp trees, with no build: (A) missing backup -> `TRAP-RESTORE-MISSING-BACKUP`,
exit **3**, marker written, both binaries gone, nothing rebuilt; (B) a copy that
cannot take (the target is a directory) -> `TRAP-RESTORE-MISMATCH`, exit 3,
marker; (C) a binary that cannot be deleted (the product slot is a directory)
-> `could NOT be deleted`, the marker says the binary survived, the deletable
probe is still removed, exit 3; (D) a restore failure discovered on a run whose
case was passing -> exit **3**, not 0; (E) a clean exit keeps the case's own
code (**7**); (F) a clean run exits 0 and rebuilds nothing. These six branches are the
part-4 `on_exit` cases of the script; the rest are the per-case restore, wiring
and clean-exit assertions added for reviews 5 to 8 (8.9-8.12). `ON-EXIT-BRANCHES=PASS`, log
`evidence/t61-animation/r12-branches/driver.log` (291 assertions in the r12b run, 8.12).
Because the functions are extracted rather than copied, a harness edit changes
what this test drives; `bash -n` and the suites' own baselines cover the
non-extracted remainder.

**The live path.** `player-animation-interrupt-demo.sh` starts one case of one
suite under `set -m` (a background job of a job-control-less shell inherits an
ignored SIGINT and its trap can never run, which is why the first attempt looked
like the trap failing), waits for `patched (backup`, sends the signal, and
asserts: wrapper exit 130 for INT / 129 for HUP, no
`TRAP-RESTORE-(MISMATCH|MISSING-BACKUP|UNREADABLE-BACKUP)` line, an
`INTERRUPT-RECOVERY=rebuilt` line, and no marker file. Three runs on this tree,
all `INTERRUPT-DEMO=PASS` with `DEMO-RC=0`:

| signal | mutant on disk | signal sent | wrapper exited | binaries rebuilt |
|---|---|---|---|---|
| `unit` SIGINT | `u1` header | 15:37:59 | 15:40:40, rc **130** | probe 15:39 (865,280 B), product 15:40 (2,657,280 B) |
| `unit` SIGHUP | `u1` header | 15:41:48 | 15:44:30, rc **129** | probe 15:43, product 15:44 |
| `pixel` SIGINT | `m12` main.cpp | 15:56:48 | 15:57:16, rc **130** | product 15:57 (2,657,280 B) |

The per-case restore path changed after those three runs, so all three were run
again on 2026-10-28, from a green tree, and all three still pass:

| signal | mutant on disk | signal sent | wrapper exited | binaries rebuilt |
|---|---|---|---|---|
| `unit` SIGINT | `u1` header | 01:04:51 | 01:07:41, rc **130** | probe 01:07, product 01:07 |
| `unit` SIGHUP | `u1` header | 01:08:56 | 01:11:51, rc **129** | probe 01:11, product 01:11 |
| `pixel` SIGINT | `m12` main.cpp | 01:24:08 | 01:24:36, rc **130** | product 01:24 (2,657,280 B) |

The restore paths changed again in this round (8.10), so all three were run a
third time on 2026-10-28 after the seventh pixel suite had left the tree green,
and all three still pass:

| signal | mutant on disk | signal sent | wrapper exited | binaries rebuilt |
|---|---|---|---|---|
| `unit` SIGINT | `u1` header | 12:10:09 | 12:13:54, rc **130** | probe 12:12 (865,280 B), product 12:13 (2,657,280 B) |
| `unit` SIGHUP | `u1` header | 11:51:27 | 11:53:55, rc **129** | probe 11:52 (865,280 B), product 11:53 (2,657,280 B) |
| `pixel` SIGINT | `m12` main.cpp | 12:06:13 | 12:06:57, rc **130** | product 12:06 (2,657,280 B) |

The restore paths changed a fourth time in this round -- the object proof now
demands that every object which depends on a restored file, and the binaries,
be newer than that file (8.11) -- so all three were run again on 2026-10-28,
from the tree the eighth pixel suite left green (15:42:59), and all three still
pass:

| signal | mutant on disk | signal sent | wrapper exited | binaries rebuilt |
|---|---|---|---|---|
| `unit` SIGINT | `u1` header | 15:47:48 | 15:50:44, rc **130** | probe 15:49 (865,280 B), product 15:50 (2,657,280 B) |
| `unit` SIGHUP | `u1` header | 15:51:54 | 15:54:34, rc **129** | probe 15:53 (865,280 B), product 15:54 (2,657,280 B) |
| `pixel` SIGINT | `m12` main.cpp | 16:07:26 | 16:07:59, rc **130** | probe 15:53 (865,280 B; this suite never touches it), product 16:07 (2,657,280 B) |

Each recovery line in that run names the object proof too: `INTERRUPT-RECOVERY=
rebuilt: sources restored and both binaries rebuilt from them, with every
object they touch newer than the restored file, so nothing mutant is left to
run`.

After the third demo nothing was left to chance about the state of the tree
either: both gates were then run once, with no mutation, into fresh output
directories (`evidence/t61-animation/r10-after-demos/`), and both passed --
`PLAYER-ANIMATION-CHECK=PASS` (232 assertions, `check.log`, 16:10) and
`PIXEL-CHECK=PASS` (28, `pixels.log`, 16:22). The tree the demos left behind is
therefore the same green tree the eighth suite left, not a tree that merely
looks fresh.

Logs: the 15:xx runs are `evidence/t61-animation/interrupt-demo/{unit,unit-hup,pixel}.log`;
the 01:xx re-runs are the same names with `.r6` appended, the 11:xx-12:xx runs
with `.r7`, and this round's 15:xx-16:xx runs with `.r8` (the demo's own stdout
beside each one as `demo-{unit-int,unit-hup,pixel-int}.r8.log`). The `.r7` unit directories carry one
oddity worth stating: the demo names its suite log by target only, so the SIGHUP
run (11:51:27) overwrote the SIGINT run's log. The SIGINT demo was therefore run
once more, alone, at 12:10:09 -- the row above is that run -- and its log is
`unit.log.r7`; the 11:51 SIGHUP log was copied before the repeat and is
`unit-hup.log.r7`. Nothing else about the two runs differs: both exited with
their expected code and both left a rebuilt probe and product.

The hardened handler was then re-exercised the ordinary way as well, because a
trap that fires only on signals is still on the path a passing run takes at exit:
the unit suite was re-run 15:59-16:07 (baseline 232/0, `u1` 3 FAIL, `u2` 2 FAIL,
final 232/0, `SUITE-RC=0`) and the pixel suite was re-run end to end
16:16-18:09, its fifth run, with every number unchanged (8.3) and
`MUTATION-SUITE=PASS`. Both re-runs left the source diffstat at
`7 files changed, 846 insertions(+), 111 deletions(-)` -- that was that round's
tree; this round's re-runs (8.3, 8.7) stand at `7 files changed, 1120
insertions(+), 130 deletions(-)`.

What stays uncovered, stated rather than hidden: a **SIGKILL or power loss**
leaves a mutant binary -- and the mutant source beside it -- because no trap
runs, and the gate's freshness check cannot see that state (section 9); the
branch driver uses
a real `cp` that cannot overwrite its target rather than a *live* filesystem
failure, since the harness cannot break the filesystem on purpose; and the
marker is a refusal to accept, not a repair -- a tree carrying
`BINARIES-NOT-FOR-ACCEPTANCE.txt` must be restored by hand before any gate run.

### 8.9 The fifth review's finding: a failed per-case restore

Review 5 returned **do not submit yet** again: the four review-4 repairs held,
but the reviewer reproduced a gap *between* cases -- a failed per-case restore
could leave a mutant binary behind with no marker. It also found a standards
problem in the branch script (a hardcoded checkout path and a fixed `rm -rf`
target) and noted that the branch test only drove the unit harness's recovery
functions, so neither harness's per-case restore was exercised.

**The gap.** `run_case()` ended with a bare `cp -p` of each backup and a `cmp
-s` that only *printed* `OK`/`FAIL`; a copy that failed fell through to
`touch` and to the rebuild after the restore. If the copy failed but the EXIT
trap's second restore then succeeded, `RESTORE_FAILED` stayed 0, so
`recover_binaries` never ran -- the sources ended original and the binary
stayed mutant, with no `BINARIES-NOT-FOR-ACCEPTANCE.txt` to say so. The pixel
gate's F1 freshness check could then accept that binary as newer than the
restored sources. The reviewer reproduced exactly that on both harnesses by
extracting the real `run_case()` and `on_exit` and injecting one `cp` failure:
`source-after-exit=original, binary-after-exit=mutant, marker=absent`.

**The repair: the per-case restore is a gate now, not a report.**
`restore_case_sources()` (`player-animation-unit-mutations.sh:195-236`,
`player-animation-pixel-mutations.sh:213-254`) walks the same `TOUCHED` list
`restore_all` walks, and for each entry:

- backup absent -> `  FAIL CASE-RESTORE-MISSING-BACKUP <file> (expected
  <backup>): the mutant cannot be undone` (`unit:202`, `pixel:220`);
- backup unreadable -> `  FAIL CASE-RESTORE-UNREADABLE-BACKUP <backup>`
  (`unit:207`, `pixel:225`);
- the copy does not verify -> `  FAIL CASE-RESTORE-MISMATCH <file>: the copy
  from <backup> did not verify, so the tree may still carry the mutant`
  (`unit:214`, `pixel:232`);

each case sets the sticky `RESTORE_FAILED=1`, and the file is `touch`ed only
after a verified copy. When any of them fires the case prints
`  REFUSAL the tree could not be restored: no further build, no further case, no
baseline. The EXIT trap deletes both binaries and writes
<OUT>/BINARIES-NOT-FOR-ACCEPTANCE.txt.` (`unit:233`, `pixel:251`; the pixel wording says
"the product") and returns 1.

`run_case()` calls it where the bare copy was (`unit:509`, `pixel:511`):
`if ! restore_case_sources; then fail_all=1; return 1; fi`. So on a failed
restore the rebuild after the restore never runs, and the `restored
byte-identical (cmp -s)` line is only printed for a verified copy.

The refusal is sticky past the case. The case loop checks `RESTORE_FAILED`
after every case and, when it is set, prints `=== REFUSAL: the per-case restore
failed; the remaining cases are not run and no build is attempted ===` and
breaks (`unit:662`, `pixel:663`). The final baseline is not measured, because a
baseline there would describe a mutant build: `=== final: NOT MEASURED -- the
tree could not be left clean, so a baseline here would describe a mutant
build ===` plus `  FAIL final baseline not measured (a CASE-RESTORE-* line
above failed)` (`unit:671-672`, `pixel:672-673`). The run then reaches the
EXIT trap with `RESTORE_FAILED=1`, and that is what makes `recover_binaries`
delete the binaries instead of rebuilding them, write the marker, and turn the
exit code into 3 (8.8).

**The reviewer's repro, kept as a test.** `player-animation-recovery-branches.sh`
grew from 73 to 190 lines (325 in 8.10, 549 now) and now extracts `restore_all`,
`restore_case_sources`, `recover_binaries`, `on_exit` and `run_case`
**verbatim** from **both** harnesses into `mktemp -d` trees (`sed` from each
function head to its closing brace). The hardcoded worktree path and the fixed
`rm -rf` target of the first version are gone -- the script locates itself with
`cd "$(dirname "$0")"` and cleans up its own unique temp dir -- which answers
review 5's standards note. It keeps the six `on_exit` branches A-F (14
assertions) and adds:

- a **wiring check** that greps each harness for the anchors of the new path --
  `restore_case_sources() {`, `if ! restore_case_sources; then`, `REFUSAL the tree
  could not be restored`, `note_cause`, `refuse`, the five refusal cause codes and
  the marker filename (four anchors when this round added it, eleven now), so the
  test fails if a refusal path is edited away;
- **per-case refusal scenarios** for unit and pixel, each injected and control:
  the extracted `run_case()` is driven with a real mutated source and a one-shot
  `cp` shim on `PATH` that fails only when the copy's destination is the mutated
  source (the backup copy writes `*.orig`, so it is unaffected), then
  `( exit "$rc_case" ); on_exit` runs the real trap.

Injected: exit **1**, `CASE-RESTORE-MISMATCH` named, the refusal announced,
`rebuilds=1` (the mutant build only -- no build after the refusal), the trap's
retry leaves the source byte-identical to its backup, product **and** probe
deleted, marker written and the marker naming the cause. Control: exit 0,
`rebuilds=2`, source original, no marker. Review 6 added four more transitions
and the trap-touch case to the same driver; 8.10 carries those and their totals
(**131 assertions, 0 failures** at that point, `ON-EXIT-BRANCHES=PASS`, exit 0,
log `evidence/t61-animation/recovery-branches.log`; the review-7 rewrite in 8.11
stands at 256).

**One lesson the test itself taught.** The extracted functions do not carry the
harness's top-level state, and both `on_exit` and `restore_case_sources` read
`INTERRUPTED` and `RESTORE_FAILED`. Under this script's `set -u` the first
version therefore aborted inside the trap before it reached `recover_binaries`
-- and its refusal assertions failed with the binaries still present. That is
precisely the failure class the test hunts: the scenario now seeds
`INTERRUPTED=0` and `RESTORE_FAILED=0` (`:132-137`) so the trap under test
runs to its end. The harnesses themselves initialise both variables at the top
of the file, which is why the live path never had this defect.

**The interruption this round produced for real.** The suite was stopped twice
while this round was being measured. The second stop was a hard process kill
during `m9`: the mutation had been applied and the product rebuilt, the trap
never ran, and the tree was left with the mutant header and a 2,656,256-byte
product against the green 2,657,280. The repair was `cmp -s` against the
harness's own backup (DIFFERS), `cp -p` back, a re-`cmp` (byte-identical), a
tracked-diffstat check (911/118 over the same seven files) and a rebuild
(BUILD-RC=0, green size). Two things about that state are worth recording.
First, restoring with `cp -p` alone also restores the *old* mtime, so the
mutant binary stays newer than every source and `F1`
(`player-animation-pixels.sh:66-76`) accepts it -- the repair only became
detectable because the copy was followed by `touch`, which made the binary
stale. Second, what catches the state in general is the source comparison
against the reviewed diff manifest, not the gate. Section 9 carries the
consequence: the artifact for each round is generated after its suite ends,
never while one is running, and every regenerated artifact is checked for a
clean hunk rather than trusted.

### 8.10 The sixth review's findings: the last three ways a restore could be believed

Review 6 returned **do not submit yet** with two P1s and one P2, all three inside
the state machine review 5 had just built. Each is closed here, and each closure
is *executed* by the branch driver rather than argued.

**P1: a `touch` that failed was counted as a successful restore.** The per-case
restore copies the backup back with `cp -p`, which restores the *original mtime*
along with the bytes. `build-target.sh:131` recompiles only when a source or
header is newer than `.headers-stamp`, so a restored header older than the stamp
skips the recompile: the object built from the mutant stays, the relink links
mutant code onto a clean-looking source, and the resulting binary is newer than
every source on disk, so `F1` (`player-animation-pixels.sh:66-76`) accepts it.
A restore that cannot move the mtime is not a restore.

Both harnesses now refuse it:

- `restore_all` (`player-animation-unit-mutations.sh:148-189`,
  `player-animation-pixel-mutations.sh:166-207`) still touches a file only after a
  verified copy, and a `touch` that now fails prints
  `TRAP-RESTORE-TOUCH-FAILED <file>` (`unit:177`, `pixel:195`), records the
  cause (`unit:178`, `pixel:196`) and sets `RESTORE_FAILED=1`;
- `restore_case_sources` (`unit:195-236`, `pixel:213-254`) prints
  `  FAIL CASE-RESTORE-TOUCH-FAILED <file>: the file is byte-identical but its
  mtime could not be advanced, so the next build would skip the recompile and
  keep the mutant object` (`unit:221`, `pixel:239`), records the cause and
  returns 1 -- so `run_case`'s `if ! restore_case_sources; then`
  (`unit:509`, `pixel:511`) refuses before the rebuild that follows the restore.

**P1: the two build-failure paths never entered the refusal state.** On a mutant
that would not compile, the old branch restored the source with its own
`cp -p`/`touch` and rebuilt with `|| true`, swallowing every error; on a rebuild
from the restored sources that failed, it only set `fail_all`. In both cases the
binaries on disk were unverified and the suite could still reach its final
baseline. Both paths now go through `refuse()` (`unit:246`, `pixel:264`):
`CASE-BUILD-MUTANT-FAILED` (`unit:465`, `pixel:477`) and
`CASE-REBUILD-RESTORED-FAILED` (`unit:515`, `pixel:517`), where
`refuse <code> <detail>` = `note_cause` + `RESTORE_FAILED=1` + `fail_all=1` +
`  FAIL <code>: <detail>` + `  REFUSAL the case cannot be undone (<code>); no
further build, no further case, no baseline.`

`note_cause()` (`unit:243`, `pixel:261`) keeps the **first** reason, so the
marker names the root cause rather than the last symptom. Every refusal reason in
both harnesses calls it: `CASE-RESTORE-MISSING-BACKUP`,
`CASE-RESTORE-UNREADABLE-BACKUP`, `CASE-RESTORE-MISMATCH`,
`CASE-RESTORE-TOUCH-FAILED`, `TRAP-RESTORE-TOUCH-FAILED`,
`CASE-BUILD-MUTANT-FAILED`, `CASE-REBUILD-RESTORED-FAILED`. The trap's marker
now reads `NOT-FOR-ACCEPTANCE: the <unit|pixel> mutation suite refused (<code>:
<detail>), so the sources may still carry a mutant and both binaries were
DELETED. ...`, and the final-baseline refusal prints the same cause instead of
the old generic phrase.

**P2: the unreadable-backup branch was named but never run.** Review 6 was right:
it was in the `case` statement, in the report, and in neither test. It cannot be
produced with file permissions on this host -- `chmod 000` leaves the file
readable to this Administrator token (`[ -r ]` says READABLE=true) and an
`icacls` deny is rejected (rc 87, with MSYS mangling `/deny` into a path) -- so the
branch driver injects the *state* with a bash function shim over `[` that answers
"not readable" for exactly the backup path and sends every other test to
`builtin [`. Driving the real `run_case`: both harnesses take
`CASE-RESTORE-UNREADABLE-BACKUP` (`unit:207`, `pixel:225`), the trap takes
`TRAP-RESTORE-UNREADABLE-BACKUP` (`unit:158`, `pixel:176`), the binaries are
deleted, the marker names the cause and the exit is 1 -- and because the backup
genuinely could not be read, the source is left mutant, and the test asserts that
the run does **not** claim a restore. The injection is labelled SYNTHETIC in the
driver's own output; the OS-level unreadable state itself remains NOT VERIFIED
(section 9).

**The driver -- 325 lines then, 438 after the 8.11 rewrite -- drives both
harnesses' per-case restore.**
`player-animation-recovery-branches.sh` extracts `REFUSAL_CAUSE`, `note_cause`,
`refuse`, `restore_all`, `restore_case_sources`, `recover_binaries`,
`on_exit` and `run_case` **verbatim** from both harnesses (`:32`), and per
harness runs:

- the six `on_exit` branches A-F (unit) plus G, the same missing-backup branch
  driven through the pixel harness (`:58-116`);
- an **11-anchor wiring check** per harness (`:120-140`) -- `restore_case_sources()`,
  the `if ! restore_case_sources` call, the `REFUSAL` line, `note_cause`,
  `refuse`, and the five cause codes, plus the marker filename -- so the test
  fails if any refusal path is edited out;
- **six per-case scenarios** through the real `run_case` (`:151` called in
  loops at `:258-291`): mismatch, clean control, build-fail, rebuild-fail,
  touch-fail and unreadable, each asserted by `assert_scenario` (`:222`) or
  `assert_clean` (`:244`);
- the **trap-restore touch** scenario (`:293-320`) on a run that would otherwise
  have exited 0.

Every refusal scenario asserts the same seven things: a nonzero exit (**1**; **3**
for the trap case, where a passing run is upgraded), the cause named on a
`  FAIL ` line, the `REFUSAL` line, **no build after the refusal** (`rebuilds`
unchanged: 1, or 2 for `rebuild-fail`, where the failed rebuild is the second),
the product -- and, for the unit harness, the probe -- deleted, the marker
written, and the marker naming the cause.

| scenario (run on both harnesses) | exit | rebuilds | cause recorded | source after the trap |
|---|---|---|---|---|
| mismatch (the review-5 repro) | 1 | 1 | `CASE-RESTORE-MISMATCH` | restored by the trap's retry |
| build-fail | 1 | 1 | `CASE-BUILD-MUTANT-FAILED` | restored |
| rebuild-fail | 1 | 2 | `CASE-REBUILD-RESTORED-FAILED` | restored |
| touch-fail | 1 | 1 | `CASE-RESTORE-TOUCH-FAILED` | bytes original, mtime not moved |
| unreadable (synthetic) | 1 | 1 | `CASE-RESTORE-UNREADABLE-BACKUP` | **not** claimed restored |
| clean control | 0 | 2 | none | original, no marker |
| trap-touch (passing run) | **3** | none | `TRAP-RESTORE-TOUCH-FAILED` | bytes original |

Measured on this tree after that round: **131 assertions, 0 failures**,
`ON-EXIT-BRANCHES=PASS`, exit 0, log
`evidence/t61-animation/recovery-branches.log`; `bash -n` clean on all four
scripts. The counts are the same for the unit and the pixel harness because both
are driven, not just the one whose functions the first version extracted. That is
the review-6 state of the driver; 8.11 carries the review-7 rewrite and the
8.12 additions take it to 291 assertions, 0 failures (r12b log).

**Re-run on the hardened harnesses.** The two focused suites and the three
interrupt demos were all re-run after these edits, because the trap path changed
again. The unit suite ran 09:41:13-09:51:33: baseline **232 OK / 0 FAIL**, `u1`
**3 FAIL**, `u2` **2 FAIL**, final baseline **232/0**, `UNIT-MUTATION-SUITE=PASS`,
`SUITE-RC=0`, every restore verified with `cmp -s`, no `STALE-BINARY`, no
`TRAP-*` line. The pixel suite's seventh run and the demos are in 8.3 and 8.8.

**On the standards note.** Review 6 repeated review 5's non-blocking observation
that the two harnesses each implement the same backup/restore/verify logic and
must be kept in step. The duplication is deliberate -- either harness must be
runnable alone, so the pixel harness cannot import the unit harness's file -- and
it is now *driven* on both sides: the branch driver runs the same fault injection
against both copies, so a drift between them fails the driver instead of passing
unseen.

### 8.11 The seventh review's finding: a touch that returns 0 is not a restore

Review 7 accepted this round's four earlier repairs (backup missing, binary
deletion confirmed, failure exits nonzero, SIGHUP) and then found one more way a
restore could be believed:

> **[P1] 恢复仍可能留下变异对象。** restore_all() skipped the one action that
> makes the next build recompile whenever the bytes already matched
> (cmp -s "$b" "$f" && continue), and restore_case_sources() checked only
> touch's exit status. His isolated repro printed RESTORE_RC=0
> RESTORE_FAILED=0, HEADER_STAMP_GUARD=SKIPS_SOURCE_TOUCH,
> OBJECT_DEPENDENCY=NEWER_THAN_RESTORED_HEADER, TRAP_RESTORE_TOUCH_CALLS=0:
> the recovery called the tree restored, and the build guard would have touched
> nothing and relinked the object compiled from the mutant.

He also asked for the trap-level reasons to reach the marker (P2), and for the
report to stop calling every refusal an exit 3 (P2).

**Why a touch exit status is not evidence.** build-target.sh:124-135 decides
what to recompile from timestamps, not from anything the harness reports:

```
  STAMP="$BUILD/.headers-stamp"
  ...
  elif [ -n "$(find native/include native/src native/tools native/third_party \
      \( -name '*.h' -o -name '*.hpp' \) -newer "$STAMP" -print -quit 2>/dev/null)" ]; then
    echo "note: a header is newer than $STAMP; touching sources (header deps are untracked)"
    touch native/src/*.cpp native/tools/*.cpp
  fi
  cmake --build "$BUILD" --target "$@" > "$LOG" 2>&1
```

A restored header whose mtime is older than that stamp makes the guard skip
`touch native/src/*.cpp`, nmake then compares the `.obj` against the
`.cpp` and recompiles nothing, and the link that follows relinks the object
built from the mutant. A touch that returns 0 says the call was accepted; it does
not say the mtime moved past the line the guard reads. So the fix could not be
"check touch's exit code harder". It had to be the three things below, each of
which is evidence:

1. **Move past the stamp, and prove it.** Both restore paths now touch
   unconditionally -- the byte-equality shortcut is gone from both -- and then
   require `mtime_newer_than_stamp "$f"` (player-animation-unit-mutations.sh:179-187,
   pixel :197-205). A failing touch is `TRAP-RESTORE-TOUCH-FAILED` /
   `CASE-RESTORE-TOUCH-FAILED`; a touch that returns 0 without moving the mtime
   past `$STAMP` (`${T61_STAMP:-$OBJ_ROOT/.headers-stamp}`) is the new
   `TRAP-RESTORE-MTIME-NOT-ADVANCED` / `CASE-RESTORE-MTIME-NOT-ADVANCED`.
2. **Prove the recompile happened, from CMake's own dependency graph.**
   `object_paths_for()` (unit :95-117) reads
   `$OBJ_ROOT/CMakeFiles/<target>.dir/compiler_depend.make` -- CMake's generated
   dependency list, not a list this harness maintains -- and prints every object
   whose dependencies name the restored file. `verify_objects_are_fresh()`
   (unit :119-143) then requires each of those objects to be newer than the
   restored file (`-STALE-OBJECT`), and refuses outright when CMake says no object
   depends on it (`-NO-OBJECT-DEPENDENCY`): a recompile that cannot be located
   cannot be proven. `verify_rebuild_is_fresh()` (unit :353-374) does the same
   for the binaries (`-STALE-BINARY`, `-NO-BINARY`), and
   `verify_case_build_is_fresh()` (unit :376-388) is called after **every** case
   rebuild and again after the recovery rebuild; the codes there are prefixed
   `CASE-REBUILD-` and `RECOVERY-`. If either check fails the run refuses, the
   binaries are deleted, and the marker is written.
3. **The marker names the trap-level reason.** The trap branches for missing
   backup, unreadable backup, mismatch, touch failure and mtime failure all call
   `note_cause()` (unit :243-245, first reason wins), so the marker reads
   `NOT-FOR-ACCEPTANCE: ... refused (TRAP-RESTORE-MISMATCH: ...)` and not merely
   "a restore failed". The driver asserts the reason text, not that a marker
   file exists.

**One state machine, two files.** The final round's rule was no new abstraction,
so the two harnesses cannot share a library. What they share instead is a
contiguous region delimited by

```
# >>> SHARED-RECOVERY-STATE-MACHINE-BEGIN   (unit :61, pixel :79)
# <<< SHARED-RECOVERY-STATE-MACHINE-END     (unit :421, pixel :439)
```

361 lines that are **identical after line-ending normalisation** -- the pixel
harness keeps CRLF and the unit harness is LF, so the driver's `region_of`
converts CRLF to LF and only then compares with `cmp -s`. What that proves is
line-ending-normalized text equality, not byte equality of the two files; the
driver's header and its log line say exactly that, and so does this report.
Everything suite-specific
(`GATE`, `PROBE`, `PRODUCT`, `SUITE_LABEL`, `T61_BUILD_TARGETS`,
`OBJ_ROOT`, `OUT`, `BACKUP` and the exported evidence dirs) is set in the
prelude above the region, which is why the region can be identical and why a
drift in one copy now fails the driver instead of passing unseen.

**The branch driver now runs the whole matrix.**
player-animation-recovery-branches.sh (533 lines) extracts the region, each
harness's own `run_case()`, and drives the **real** functions in isolated
`mktemp` trees. `rebuild()` is stubbed and counts its calls, so "no further build
after a refusal" is measured rather than argued; the object graph is a fake
`CMakeFiles` tree whose `compiler_depend.make` names the object that depends on
the restored file, so `-STALE-OBJECT` and `-NO-OBJECT-DEPENDENCY` execute for
real.

| matrix (each row runs on **both** harnesses) | injected fault | expected outcome |
|---|---|---|
| per-case: clean control | none | rc 0, 2 builds, source original, no marker, object proof passes |
| per-case: mismatch | cp fails once into the source | rc 1, CASE-RESTORE-MISMATCH, binaries deleted + marker, the trap's retry restores |
| per-case: unreadable (synthetic) | `[ -r ]` shim on the backup | rc 1, CASE-RESTORE-UNREADABLE-BACKUP, no restore claimed |
| per-case: touch-fail | touch returns 1 | rc 1, CASE-RESTORE-TOUCH-FAILED |
| per-case: lie-touch | touch returns 0, mtime unchanged, stamp newer than the file | rc 1, CASE-RESTORE-MTIME-NOT-ADVANCED |
| per-case: stale-object | rebuild leaves objects older than the restored file | rc 1, CASE-REBUILD-STALE-OBJECT |
| per-case: no-object | the dependency list does not name the restored file | rc 1, CASE-REBUILD-NO-OBJECT-DEPENDENCY |
| per-case: stale-binary | rebuild leaves the binaries older | rc 1, CASE-REBUILD-STALE-BINARY |
| per-case: build-fail | the mutant build fails | rc 1, CASE-BUILD-MUTANT-FAILED, no gate runs |
| per-case: rebuild-fail | the post-restore build fails | rc 1, CASE-REBUILD-RESTORED-FAILED, 2 builds |
| trap: missing / mismatch / unreadable / touch-fail / lie-touch | the same five states, seen by on_exit | rc 3, the named TRAP-* reason in the log **and in the marker**, binaries deleted, 0 builds |
| trap: equal-old | bytes already equal, mtime behind the stamp, real touch | rc 0, one recovery build, INTERRUPT-RECOVERY=rebuilt, no marker, no false touch failure |
| trap: deletion | the product cannot be deleted (a directory) | rc 3, "could NOT be deleted" in the log, the marker says it survived, 0 builds |
| exit codes | clean 7, SIGINT, SIGTERM, SIGHUP | 7 / 130 / 130 / 129 measured from `$BASHPID` |

**Measured on this tree, 2026-10-28 17:44:49 (the r12 run):** **289
assertions, 0 failures**, `ON-EXIT-BRANCHES=PASS`, `DRIVER-RC=0`, log
`evidence/t61-animation/r12-branches/driver.log`. Part 1 reports the
361-line region as "identical after line-ending normalisation (361 lines)" and
part 2 reports 30/30 wiring anchors in **each** harness. Part 6 drives the
clean-exit proof on both harnesses: `clean-exit-proof` measures rc 0, 2 builds
(the exit itself builds nothing), an unchanged source mtime, the proof re-run
and passed, no marker, 2/2 and 1/1 dependent objects newer than the source, and
both binaries newer; `clean-exit-stale` (the old touch-at-exit behaviour
injected as an extra `touch`) measures rc 3, the refusal naming
`CLEAN-EXIT-NOT-FRESH`, a marker naming `CLEAN-EXIT-STALE-BINARY`, and the
product **deleted**. The previous rounds' runs are kept as
`recovery-branches.r8.log` (256 assertions) and `recovery-branches.r7.log` (131
assertions). `bash -n` is clean on all four scripts.

**Exit codes, as they behave.** A per-case refusal returns **1**: the suite stops,
skips the final baseline, and the trap deletes the binaries and writes the
marker. Only a run that was going to exit **0** is upgraded to **3** when the
trap then finds a failed restore. The signal traps exit **130** for INT and TERM
and **129** for HUP, which is what the demos in 8.8 measured. The previous
report's blanket "refusals exit 3" was wrong; this paragraph replaces it, and
8.10's table (which already said 1) is the review-6 state of that same rule.

**What this does not prove.** The object graph is exercised on a synthetic
`CMakeFiles` tree (fault injection); the real tree's graph is exercised by the two
focused suites in 8.3 and 8.7, not by the driver. A `kill -9`, a power loss or a
filesystem that lies about mtimes remain outside what a shell trap can promise,
and section 9 keeps them.

### 8.11.1 The first run of this suite refused, and it was right to

The first attempt to run the two focused suites against the new proof
(2026-10-28 13:22-13:28, lock `t61-final-r1`, log `.scratch/r9-suites.log`)
did not reach a verdict. The unit suite ran its green baseline (232 OK / 0
FAIL), mutated `native/include/animation_sequence_match.h` for `u1`, collected
its three named failures, restored the source byte-identically (`cmp -s`) and
rebuilt with rc 0 -- and the new proof then refused the case:

```
FAIL CASE-REBUILD-NO-OBJECT-DEPENDENCY native/include/animation_sequence_match.h: no object under native/build-nmake/CMakeFiles/<target>.dir depends on it, so a recompile cannot be proven
FAIL CASE-REBUILD-NOT-FRESH: ...
REFUSAL the case cannot be undone (CASE-REBUILD-NOT-FRESH); no further build, no further case, no baseline.
```

`u2` was skipped, the final baseline was NOT MEASURED, the run exited 1, and
the trap deleted both binaries, leaving
`evidence/t61-animation/r9-unit-mutations/BINARIES-NOT-FOR-ACCEPTANCE.txt`.
The sources were then compared against their backups and were pristine
(`U1-SOURCE-ORIGINAL`): the tree was safe, and the harness simply could not
*prove* the recompile, so it refused instead of believing itself. That is the
behaviour this round is built on, and the reason the r10 suites exist.

The defect was in the proof, not the state machine. CMake writes its dependency
database with CRLF line endings --
`native/build-nmake/CMakeFiles/tf2_demo_native.dir/compiler_depend.make` is
1,409,369 bytes with 10,138 CRLF line ends -- while `object_paths_for()` reads
lines with `while IFS= read -r line`, so the trailing carriage return stayed
attached to the path and matched no restored file. The fix is one line,
`line=${line%$'\r'}` (unit :103, pixel :121); with it the real tree answers
`animation_sequence_match.h` ->
`model_pose_probe.dir/tools/model_pose_probe.cpp.obj` and
`tf2_demo_native.dir/src/main.cpp.obj`, and `entity_id_color.h` the same two.

Two guards came out of it. The branch driver now writes its synthetic
`compiler_depend.make` with `printf '%s: %s\r\n'` so the driver itself feeds
the parser CRLF (player-animation-recovery-branches.sh:153), and a bite test was
run again this round: copies of the driver and both harnesses in
`.scratch/cr-bite-r12` with the strip line deleted from the two copies
produced `ASSERTIONS-OK=253 ASSERTIONS-FAIL=38`, `ON-EXIT-BRANCHES=FAIL` and exit 1 --
the object lookup finds nothing, so the positive branches refuse for the stated
reason (`FAIL unit/clean: no object-proof OK line`, `FAIL unit/trap-equal-old:
exit code: rc=3 expected 0`) -- while with the line in place the driver is
291 assertions and 0 failures.

### 8.12 The eighth review's four findings

Review 8 returned do-not-submit with two P1 spec findings and two P2 items. All
four are about what the evidence proved, not about what the code computed, and
all four are answered in this round.

**P1: a clean exit restored the tree and then called it proven.** `on_exit` used
to call `restore_all` on every exit, so on a normal exit it walked `TOUCHED`
and ran `cmp -s`/`cp -p`/`touch` over every file even though nothing had
failed. The copies were identical, but each `touch` advanced the source mtime
above the objects and binaries the last case had built: after `on_exit` returned
0 the tree was restored and *unproven*, because the next build would recompile
those sources while the binaries on disk were the ones the last case produced. The
reviewer reproduced that with the real functions. The repair is that a clean exit
no longer touches anything: it verifies. `verify_clean_exit()`
(`player-animation-unit-mutations.sh:397-415`,
`player-animation-pixel-mutations.sh:415-433`) runs only when
`INTERRUPTED` and `RESTORE_FAILED` are both 0, and for every restored file
compares it with its backup (`cmp -s`; a difference prints
`  FAIL CLEAN-EXIT-SOURCE-DIFFERS <file>` and records the cause), then calls
`verify_case_build_is_fresh CLEAN-EXIT` (`unit:379-388`, `pixel:397-406`),
which requires the object of every file it depends on plus the probe and the
product to be newer than the source. One old object is enough to
`refuse CLEAN-EXIT-NOT-FRESH "the tree at exit is not the tree the last case
proved: ..."`, which sets `RESTORE_FAILED=1`, so the trap deletes both
binaries, writes the marker with `CLEAN-EXIT-STALE-BINARY`, and the exit code is
upgraded to 3. The `on_exit` ordering is
`player-animation-unit-mutations.sh:294-324` /
`player-animation-pixel-mutations.sh:312-342`.

Part 6 of `player-animation-recovery-branches.sh` drives that path on **both**
harnesses. `clean-exit-proof` runs a case to a clean exit and asserts rc 0, the
expected rebuild count, that every restored source still matches its backup, that
its mtime did not move, that the proof itself re-ran and passed, that no marker
was written, and that every object check (2/2 unit, 1/1 pixel) plus the product
and probe are newer than the sources. `clean-exit-stale` injects one extra
`command touch` after the restore to reproduce the old behaviour and asserts
rc 3, `FAIL CLEAN-EXIT-STALE-BINARY` and `FAIL CLEAN-EXIT-STALE-OBJECT`, the
`CLEAN-EXIT-NOT-FRESH` refusal, a marker that names `CLEAN-EXIT-STALE-BINARY`,
and that the product was deleted. The driver run behind these assertions is
291 assertions with 0 failures (`evidence/t61-animation/r12c-branches/driver.log`;
the same part was already green in r12b).

**P1: fourteen of the nineteen added assertions had no mutant of their own.**
That was accurate as the report stood: u1 and u2 reddened five labels, and the
rest stayed green no matter what was broken. §8.7 now carries one row per label
(label, expected value, the case that reddens it, and the measured FAIL line),
ten new unit cases u3-u10 make all nineteen red, and the section states the
policy the round is measured against: every new assertion ships a mutation that
reddens it for the stated reason (`docs/RELEASE-POLICY.md:12-13`).

**P2: "byte-identical" claimed more than the comparison proved.** The region
comparison drops CR before `cmp` because the working copy materialises the two
harnesses with different conventions -- this host has `core.autocrlf=true`, the
unit harness is LF here and the pixel harness CRLF here, while the blobs the
repository stores are LF for both -- so what the comparison proves is text
equality after line-ending normalisation, and the report wrote "byte-identical".
The report and the driver now say *identical after line-ending normalisation*,
and part 1 of the driver adds two assertions so the normalisation cannot hide a
real change: each file must keep **one** convention throughout (CR bytes either
0 or equal to the line count), which catches a stray CR that normalisation would
erase. Those assertions are deliberately not "0 CR" / "n CR": the counts are a
property of the checkout, not of the commit, and a fresh clone (where
`core.autocrlf` makes both files CRLF) would fail a driver that pinned them. It
has teeth: copies of the driver and both harnesses in `.scratch/eol-bite` with
one CR injected on line 100 of the unit harness give `ASSERTIONS-OK=290
ASSERTIONS-FAIL=1`, `ON-EXIT-BRANCHES=FAIL`, rc 1 and the line
`FAIL player-animation-unit-mutations.sh mixes line endings (CR bytes 1, lines
691): normalising would hide the stray CR`
(`evidence/t61-animation/r12c-branches/driver-mixed-eol.log`); on the real tree
the same driver is 291 assertions, 0 failures, printing `CR bytes 0, lines 691`
and `CR bytes 691, lines 691` for the record
(`evidence/t61-animation/r12c-branches/driver.log`).

**P2: the suite verdict could not be reproduced from the saved evidence.** The
r10 runs kept per-gate logs but not the harness's own stdout, so
`UNIT-MUTATION-SUITE=PASS` and the suite exit code could not be checked by
anyone who was not watching the terminal. Every r12 run keeps its full stdout:
`evidence/t61-animation/r12-unit-mutations/harness.stdout.log` (64,289 bytes,
573 lines, containing the `UNIT-MUTATION-SUITE=PASS` line and `SUITE-RC=0`),
the matching `r12-pixel-mutations/harness.stdout.log` for the pixel suite, and
the runner's own `.scratch/p8/r12-suite.outer.log` with a `START`/`RC`/`END`
line per step. Each run writes into a new run-id directory (`r12-*`, `r12b-*`,
`r12c-*`);
no older evidence file is overwritten.

What this round re-ran after these changes: `bash -n` on all four scripts, the
branch driver, the three interrupt demos, the unit suite (§8.7) and the pixel
suite (§8.3), the two focused gates after a rebuild, and the mixed-line-ending bite
test (§8.12). Everything reported for review 9 is a
reading from one of those runs.

## 9. What is NOT verified

- **Exclusive fullscreen / a real desktop.** Every capture here runs in the
  product's windowed capture path with `window_visible=0`; T49.1's isolated-desktop
  question is untouched and remains NOT VERIFIED.
- **Other player classes.** The pixel proof is on the snakewater recording at one
  camera pose; the class sweep is counter-level (stage 2), not pixel-level.
- **Animation fidelity.** The drive moves the body; it does not claim the pose
  matches the server's. A demo does not record the player's sequence, so there is
  nothing to compare against -- that is a property of the format, not a gap in the
  test.
- **The `boneMismatch` reading** (section 7) is explained but not repaired.
- **Audio** stays disabled in these runs (`audio_mode=disabled`).
- **The entity-id overflow refusal is proven by unit test, not by a capture.**
  `kEntityIdMax = 255` cannot be reached from a scene today: the draw budget caps
  `draws.size()` at 96, so the product's exit 17 path is dead code in every
  current run. `--entity-id-selftest` proves the encoder refuses and leaves the
  colour untouched, and the product's wiring (paint block -> `entityIdOverflow` ->
  exit 17) is reviewed but not executed. A capture that reached 256 bodies would
  be the only end-to-end proof, and the budget forbids it. Since 8.7 the refusal
  also has a standing mutant (`u2`), so the unit proof cannot silently rot; what
  stays unexecuted is the product's own exit 17 path, not the encoder's rule.
- **The ranker's fallback path is unit-tested, not exercised end to end.**
  `rankSequenceCandidates` is only used by `model_pose_probe` when the
  preferred sequence fails its own pose filter; on these nine class models the
  preferred sequence always passes, so the walk never runs in a real probe. The
  order property is therefore pinned by `--matcher-selftest`, not by a capture.
  8.7 adds the mutant side of that pin (`u1` reddens the two preference
  assertions), so the rule is held from both ends; the end-to-end walk itself
  remains unexecuted.
- **A kill -9 or a power cut mid-case.** The traps cover INT, TERM and HUP
  (8.8), so ^C and a closed terminal both recover; none of them can cover
  SIGKILL. What survives such a kill is a mutant source *and* a mutant binary
  (this round produced that state for real; 8.9 has the instance). The
  freshness check is not a backstop here, and an earlier version of this
  bullet said it was: restoring the source with `cp -p` from the harness's
  backup also restores the old mtime, so the mutant binary stays newer than
  every source and `F1` (`player-animation-pixels.sh:66-76`) passes it. Only
  comparing the tracked sources against the reviewed diff manifest sees the
  state; a restore that is *proven* fresh (the mtime past the stamp, and every
  object that depends on the file plus the binaries newer than it -- 8.11) makes
  the binary stale, and `F1` then forces the rebuild. A `touch` whose exit
  status is 0 but whose mtime did not move is refused, not believed.
- **The unreadable-backup branch is exercised synthetically, not by the file
  system.** `chmod 000` does not make a file unreadable to this Administrator
  token and an `icacls` deny is rejected (rc 87), so the branch driver injects
  the *state* with a bash function shim over `[` for one path (8.10). What is
  proven is that every consumer of "unreadable" refuses correctly; what is not
  proven is that this host can produce the state at all.
- **A `touch` that returns 0 without moving the mtime is injected, not
  observed.** This host's `touch` does move the mtime, so 8.11 replaces it
  with a function that returns 0 and leaves the file behind the stamp. What is
  proven is that both harnesses refuse on the *stamp comparison* and on the
  object/binary ages, never on the exit status alone; what is not proven is that
  a real `touch` can fail this way on this file system.
- **m12's defect is not one the product can produce today.** There is exactly one
  `entityId` per draw (`static_cast<int>(draws.size()) + 1`,
  `native/src/main.cpp:10122`), so a real ledger cannot repeat an id; m12 simulates a
  collapse of that invariant. What the gate proves is that *if* it collapses, the
  per-entity reading refuses instead of naming the wrong body -- defence in depth,
  not evidence about a live defect.
- **Two bodies painted one colour under two ledger ids.** The refusal reads the
  ledger, so it catches one id claimed twice (m12's shape). It does not compare the
  id frame's distinct colours against the ledger's distinct ids, so a hypothetical
  collapse inside the colour encoder -- two ids, one colour -- would still produce a
  self-consistent per-entity read while the mask lumped two bodies together. The
  comparison is cheap (distinct non-sentinel colours versus distinct ids) and is
  not done here; 8.4 carries it as uncovered.
- **The mutant captures are transient by design.** Every case writes into one
  capture directory (`T61_PIXEL_OUT`) and the final baseline rewrites it, so a
  mutant's durable evidence is its gate log: `m12.log` keeps the `DUP` and `FAIL`
  lines verbatim, as `m11.log` keeps its five per-entity reds. The logs are
  committed; the mutant bitmaps are not, and reproducing them is one suite run.


## 10. The tree to submit

Review 3 counted **58 modified tracked files and 31 untracked paths** and put them
against a report that had said "7 source files". Both are right; they count
different things, and the report should have said which. This is the manifest.

**Source: 13 files -- 7 modified, 6 new.**

| file | status | why it is in the change |
|---|---|---|
| `native/include/animation_sequence_match.h` | new, 306 lines | the whole-token / leading-token matcher and `rankSequenceCandidates` (8.5) |
| `native/include/entity_id_color.h` | new, 31 lines | the entity-id colour encoder, the 255 ceiling and its refusal (8.6) |
| `native/tools/model_pose_probe.cpp` | modified | `--matcher-selftest`, `--entity-id-selftest`, the ranked sequence walk, the probe's per-entity print-out |
| `native/src/main.cpp` | modified | the per-draw entity id and the exit-17 overflow wiring (8.6) |
| `native/src/native_renderer.cpp` | modified | `view=entity-coverage`, the id frame, the pose/probe plumbing (8.3, 8.5) |
| `player-animation-check.sh` | modified | sections `1b/6` and `1c/6`, 213 -> 232 assertions, `STALE-BINARY`, `PLAYER-ANIMATION-CHECK=PASS` (8.7) |
| `player-animation-pixels.sh` | modified | F1-F5 forward readings, 26 -> 28 assertions, `PIXEL-CHECK=PASS`, the per-entity chain and its two refusal lines (8.3, 8.8) |
| `player-animation-pixel-mutations.sh` | modified | cases m6-m12, the `  FAIL ` line rule, restore-and-rebuild, interrupt recovery, and the same 361-line recovery region, identical to the unit harness's after line-ending normalisation (8.3, 8.8, 8.11) |
| `player-animation-unit-mutations.sh` | new, 691 lines | cases u1-u10 against the check gate (u1/u5 the matcher, u2/u7-u10 the encoder), the `STALE-BINARY` refusal, the per-case restore gate, restore-then-rebuild recovery, the clean-exit proof, and the shared 361-line recovery state machine with the object proof (8.7, 8.8, 8.9, 8.11, 8.12) |
| `player-animation-interrupt-demo.sh` | new, 126 lines | drives a live SIGINT and SIGHUP through a real case and checks what is left (8.8) |
| `player-animation-recovery-branches.sh` | new, 550 lines | extracts the 361-line recovery region from **both** harnesses (plus each one's `run_case`) and compares it as line-ending-normalized text, then drives 10 per-case modes, 7 trap modes, 4 exit codes and 2 clean-exit modes in unique temp trees against a synthetic `CMakeFiles` object graph: 291 assertions (8.8, 8.9, 8.10, 8.11, 8.12) |
| `t61-entityid.py` | new, 302 lines | the frame decoder: ledger, colours, picks, the duplicate-id refusal (8.8) |
| `verify-all.sh` | modified | steps 25/26 pinned at 232 and 28 and moved onto the gates' `NAME=PASS` tokens (8.6) |

`git diff --stat` restricted to those seven tracked paths reads
**7 files changed, 1163 insertions(+), 130 deletions(-)**; the six new files are
2,006 lines (306 + 31 + 302 + 691 + 126 + 550). The evidence-file modifications below are not source and are not in
that diffstat.

**Report: 1 file.** `evidence/t61-animation/T61-ANIMATION-REPORT.md` (this file,
1,780 lines). Every number in it is reproducible from the commands its
sections give, and the ones re-measured in this round are dated on the spot.

**Evidence: all of `evidence/t61-animation/`** -- 421 files, 347
of them untracked in this tree today. That directory is the citation base for sections 1-8: the
gate logs, the mutant logs (m1-m5 on the counter, m6-m12 on the pixels, u1-u10 on the
units), the self-test dumps captured *under* each mutant
(`u1.matcher-selftest.txt`, `u2.entity-id-selftest.txt`), the captures the pixel
numbers are read from, and the three interrupt-demo logs.

Thirteen files are kept although **no number in this report rests on them**,
because deleting a failed attempt is not the same as not having made one:
`pixel-mutations/m6.log.run2-no-guard`, `m6.log.run-mut9-interrupted`,
`m7.log.run-mut9-interrupted`, `m8.log.round5-invalid`,
`m9.log.round5-invalid` (the 8.3 harness defect and the interrupted runs around
it), `m12.log.crash-before-sort-fix` (the 8.8 decoder defect), the early
baseline/final copies (`baseline.log.run1`, `baseline.log.run2`,
`final.log.run1`) and this round's spawn-failure set
(`final.log.run6-spawn127`, `suite-run6-spawn127.log`,
`animated2.summary.run6-spawn127`, plus `m6.log.first-expectation`). The
filename says which attempt each one is; the accepted run of every case is the
plain `mN.log` beside it.

**Not in the tree, deliberately.**

- `evidence/verify-2526/` -- a 13 MB throwaway output copy of the extracted
  steps-25/26 run. Deleted in this round; the same numbers are in 8.6 and in the
  committed `evidence/t61-animation/runtime/` and `pixels/` logs, and nothing
  cites it.
- `.scratch/` -- every backup, mutant rescue (`.scratch/m12-captures/`) and
  interrupt-demo working directory. Git-ignored (`.gitignore:31`); reproducing any
  of it costs one suite run.

**Counts, and nothing committed.** `git status --porcelain` on this tree reads
**115 entries, 58 modified and 57 untracked** (the 57 entries are 353 files: git
collapses new directories to one entry, and 347 of those files are evidence plus
the six new source files): seven modified source files, this report, the
evidence logs and captures under `evidence/t61-animation/`, and six new source
files -- nothing else.
Nothing in this round was committed, merged or pushed (`git log -1` is still
`db3316e`, the commit review 3 read), and `verify-all.sh` was not run end to end,
because the review asked for the affected suites, not the whole chain: 8.6 records
the two steps extracted and run verbatim, 8.3, 8.7 and 8.8 the suites.

To re-measure the changed verdicts without the full chain:

```sh
bash player-animation-pixel-mutations.sh m6 m7 m8 m9 m10 m11 m12   # ~2 h
bash player-animation-unit-mutations.sh                           # ~55 min, u1-u10
bash player-animation-recovery-branches.sh                          # seconds, no build
bash player-animation-interrupt-demo.sh unit INT                    # ~3 min
bash player-animation-interrupt-demo.sh unit HUP                    # ~3 min
bash player-animation-interrupt-demo.sh pixel INT                   # ~12 min
```

Every one of those runs is independent, but the focused gates at the end are
not: after a mutation suite the tree's binaries are only as fresh as the last
case left them, and a header-triggered `touch` also advances
`native/tools/model_pose_probe.cpp`. So when the gates are run *after* a suite,
rebuild first (`bash build-target.sh model_pose_probe tf2_demo_native`) and then
run `bash player-animation-check.sh` and `bash player-animation-pixels.sh`;
that is what 8.3's ninth run did (21:25:15 rebuild, 232/0 and 28/0). A plain
`verify-all.sh` needs no such step.
