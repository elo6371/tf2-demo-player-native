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

- sentinel `(255,0,255)` = **6,876 pixels** of 860,784 (0.80%), bbox x434..693
  y277..360; the frame carries only three colours in total (the sentinel plus the
  sky's `(89,158,97)` and the world's `(14,18,22)`), so the sentinel belongs to the
  entity pass alone.
- over that mask, the product view at tick 64161 (`--player-animation`) carries
  **1,660 distinct colours**, mean luma **43.9**, pure-black share **0.148** --
  a shaded body, not a flat fill and not a silhouette.

The A/B is the same tick, same camera, same demo, with `--player-animation` the
only difference:

| comparison | differ | differ by >= 24 |
| --- | --- | --- |
| animated vs the same tick with the drive off | **5,665 (82.39%)** | 3,580 (52.07%) |

That is the **on/off proof**: removing the flag changes 82% of the body pixels, so
the drive reaches the pixels, and the body stays a shaded surface in both.

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

The animated run reports `boneMismatch=61269` where the undriven run reports `0`.
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
path (`sampled.bindPose`) never enters the block at all. At ~5.7 instances per
frame it is stable across runs (61269/10777 frames, 69688/12327 frames) and
measures "player skeletons carry helper bones", not "the pose is wrong".

Consequence recorded for the follow-up list: that counter cannot separate an asset
property from a real mismatch, and no gate in this tree asserts it == 0 for
players; `weapon-animation-check.sh` asserts it == 0 for weapons, where it holds.

## 8. How this is gated

Two gates and two mutation suites, all new in this tree. Both gates are wired into
`verify-all.sh` (steps 25/26 and 26/26) with **pinned assertion counts**, so a
section that silently stops running is a red line rather than a smaller number of
greens.

| gate | what it asserts | forward | mutation |
|---|---|---|---|
| `player-animation-check.sh` | stages 2-3: the sequence table, the pose, continuity, the clock rate, the tick script -- plus three negative controls and a determinism repeat | **213 OK / 0 FAIL**, `PLAYER-ANIMATION-CHECK=PASS` | `player-animation-mutations.sh` m1-m5 |
| `player-animation-pixels.sh` + `t61-pixels.py` | stages 4-5: the entity pass's own mask, the body inside it, the on/off pair at one scene tick, and a byte-identical repeat capture | **19 OK / 0 FAIL**, `PIXEL-CHECK=PASS` | `player-animation-pixel-mutations.sh` m6-m7 |

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
| m1 | `animation_clock.cpp`: `delta = rate * dt` -> `0.0` | rc=1, 61 FAIL | `B scout distinct pose checksums`, `D movingOneSecond` |
| m2 | `animation_binding.cpp`: skin matrix -> identity (bind pose) | rc=1, 79 FAIL | `B scout distinct pose checksums` |
| m3 | `animation_clock.cpp`: rate -> `maxMovingRate` (ignores speed) | rc=1, 22 FAIL | `D movingOneSecond` |
| m4 | `animation_clock.cpp`: `delta = 0.5` regardless of dt | rc=1, 61 FAIL | `C scout maxStepDelta`, `B scout distinct pose checksums` |
| m5 | `animation_binding.cpp`: one matrix element -> NaN | rc=1, 79 FAIL | `reason=non-finite-pose` |

Every case restored byte-identically (`cmp -s`), every rebuild was rc=0 with 0
`error C` (a mutant that does not compile is INVALID, not caught), and the suite
ends on a green baseline (`MUT-RC=0`). m1 was re-run against the **final** gate
after the determinism block was added and still reddens the same two names
(rc=1, 63 FAIL): the only gate change since the suite ran is additive and cannot
turn a FAIL into a pass.

### 8.3 The pixel mutation suite (m6-m7)

Two cases, each reintroducing a real defect and requiring the gate to redden a
**named pixel** assertion -- not a missing trace line and not a vanished counter:
both cases keep `animsum` and the debug view intact, so only the picture changes.

| case | mutation | gate | named red |
|---|---|---|---|
| m6 | `native/src/native_renderer.cpp`: the four neutral entity-VS writes deleted (the original black-silhouette defect, reintroduced) | rc=1, 4 FAIL | `body pure-black share 0.840 > 0.35 (silhouette again)` |
| m7 | `native/src/main.cpp`: the sampling request self-assigned, so `--player-animation` changes nothing the user sees | rc=1, 4 FAIL | `animation-off moved 0.00% < 5.0% of body pixels` (the two frames became pixel-identical: `differ=0`) |

Both cases restored byte-identically (`cmp -s`), both rebuilds were rc=0 with 0
`error C`, and the suite ended on a green baseline (`final baseline green`).

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
