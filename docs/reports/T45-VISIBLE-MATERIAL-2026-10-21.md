# T45 - the visible material semantics

Branch `task/t45-visible-material`, base `6888e6d`, worktree
`D:\TF2_Native_Worktrees\t45-visible-material`. Owner t45, shared task `task-7`.

T40 handed over four things it could not settle: (a) the specular lobe's
population at the tick-2000 reference camera is extinct after T29, so `S1 == S2`
byte-identical and the claim is printed NOT VERIFIED; (b) the material normal at
that camera moves pixels one way only (79 bytes brighter / 0 darker) with no
evidence whether that is the shader or the view; (c) 2,812 px where the detail
layer's effect toggles between the pre-T29 frame and the current frame on
surfaces the face-id mask says did not move; (d) no gate measured either material
consumer at a camera where the material population is on screen.

This report replaces (a) with a measured second view, answers (b) with a measured
population and a stated limitation, attributes (c) with a single-variable frame
mutation, and pins all of it in `world-material-semantics-check.sh`. Nothing in
`native/` is changed by this task.

## 1. The wide-area pose: qualified by measurement, not assumed

T35 derived nine material-framed poses with the probe but never launched the
product at any of them (the host queue starved it). T45 therefore qualified them:

1. `_shared/t45/qualify.sh` (hold A) built `tf2_demo_native` and
   `world_feature_probe`, captured the gate's own reference frame A twice, then
   captured `--world-specular on --world-material-bump on` at each of the nine
   poses at `--capture-tick 2000`.
2. `_shared/t45/wide-select.sh` (hold B) captured `S1w` (specular on, material
   normal off) at all nine poses and ranked each pose by the byte population its
   material-normal switch moves against that pose's `S2w` frame, same build and
   same flags. A pose qualified only if both directions were nonzero.

     pose           differing   brighter    darker   max
     WALL013          775,135    386,760   388,375    24   <- chosen
     GREEK_GROUND     109,963          0   109,963    24   one-sided
     IMETAL006         40,448     14,010    26,438    10
     WALL017           25,030     10,266    14,764    11
     METALDOOR001       4,069      1,797     2,272    11
     BEAM001              222        201        21     3
     DIRTGROUND4           22         21         1     3
     BEAM002                5          5         0     1
     IMETAL005              0          0         0     0

   (`differing`/`brighter`/`darker` count BYTES over the 2,582,352-byte frame:
   1264x681x24. The bmp tools' `total=` is the byte count, not the pixel count.)

Chosen pose: **WALL013**, `--debug-camera -3498.225 1021.852 1327.843 70.529329
134.999352`, captured at `draw: begin tick=2000`.

Exact tick and camera, measured: every pose capture's trace ends at
`draw: begin tick=2000`, and the reference frame A and its repeat A2 both hash
`6bb4d9a7a712e82ef4cf58929651cea1070d11c1ac082bdb25a6e6d7097e2f974b`, the
committed tick-2000 pin - so the reference camera and the draw are deterministic
in this tree at this tick (the T42 `main.cpp` vpk-refusal trace lines do not move
the frame).

**What the pose does and does not change.** `--debug-camera` moves the render
view only; the cluster cull is still the observer camera's. Measured: every pose
carries the byte-identical `gpu: world vis ... cameraCluster=1032
drawnTriangles=76344 eyeMap=(-2423.6,5566.0,-1280.0)` line and identical
bump/specular scopes (44 materials / 53,462 triangles; 100 materials / 76,231
triangles), while the frames differ from A on 2,570,233 of 2,582,352 bytes and
from each other. Consequences, all of which the gate's 5b text states:

* the trace counters cannot qualify a pose - the pose is pinned by frames
  (`Aw` must differ from `A`, `Aw2` must equal `Aw`);
* a pose frame shows the observer-camera-drawn set re-viewed, so it contains
  holes where geometry behind the pose camera was culled (visible in
  `evidence/t40-pixel-pins/wide-pose/S2w-wide-pose.png`);
* the pair readings stay valid because both frames of a pair share the same drawn
  set and differ by exactly one switch;
* the population claim is "pixels the reader moves in this diagnostic view", not
  "the whole scene at that camera".

## 2. The specular reader on a visible population (task step 2)

One switch apart on the same pose, same build:

| reading | value |
| --- | --- |
| `S1w` vs `S2w` (specular reads the material normal) | 775,135 bytes, max_delta 24 |
| direction | 386,760 bytes brighter / 388,375 darker (max up 24 / max down 21) |
| `Cw` vs `Dw` (material normal under the dynamic light) | 688,102 bytes, max_delta 60 |
| direction | 203,847 brighter / 484,255 darker (max up 14 / max down 60) |
| scope both consumers read | feature-apply bump 9 tiles / 9 materials / 19,305 vertices; specular control 100 materials / 76,231 triangles; bump control 44 materials / 53,462 triangles |
| switch identity | `S1w`/`S2w` lighting `dynamic=0 specular=1`; `Cw`/`Dw` `dynamic=1 specular=0`; bump row `refused` on S1w/Cw, `applied` on S2w/Dw |

Both directions are pinned exactly and each carries an `assert_ne` positive
control, so a future collapse of either direction is red rather than a quiet
pass. This is the claim the tick-2000 reference camera cannot make.

## 3. The tick-2000 79/0 direction: occlusion plus surface, not a defect

Measured on the reference camera first:

* `C` vs `D` (dynamic light on, material normal off vs on) at tick 2000: 28 px,
  79 bytes brighter / 0 darker, max 17 - the whole visible population of the
  nine materials at that camera (28 px of 860,784), and essentially on one face:
  26 px on face 3156 (bbox 412..440 x 118..143), plus one px on face 7530 and one
  on face 7054 (`.scratch/t45/analysis/bump_pixels_faces.py`).
* T40's classification of the pre-fix 220-px population: all 220 were
  `occluded_by_near` - the T29 near clip is what removed them.
* the same pair at tick 1200 (`C12` vs `R1`): 413 px, 458 brighter / 532 darker.
* `S1` vs `S2` at tick 2000: 0 px (the extinct specular pair).

And the same reader on other poses: 0/0 at IMETAL005, one-sided darkening at
GREEK_GROUND (0 / 109,963), two-sided at WALL013, IMETAL006, WALL017,
METALDOOR001, BEAM001, DIRTGROUND4. So the sign at tick 2000 is a property of the
one surviving surface (its orientation against the pass light direction) and the
28-px size is T29's near clip - **not** a shader or data defect, and equally not
evidence that the reader is verified at that camera: only 28 px are measured
there. That sentence is printed as NOT VERIFIED by the gate.

## 4. The 2,812-px detail residual: the near clip, measured (task step 4)

Population (`.scratch/t45/analysis/classify_detail_residual.py`, current F frame
vs kept pre-fix F frame, mask = T26 face-id frames; face id = `px[2] | px[1]<<8 |
px[0]<<16`): frame 860,784 px, mask same-face 309,116 px (35.91%); the detail
layer changes 3,458 px over 44 faces (max 42); restricted to same-face pixels the
toggle is 2,812 px (0.910%) over 27 faces, 1,320 pre-fix-only / 1,492
current-only. On those 2,812 px: depth bytes changed on 1,736, albedo on 97, the
flat layer on 0. The faces are WOOD/WALL007A, non-displaced; the largest is face
7536 (1,315 px). 172 connected components (largest 1,042 px, bbox 61x112) and
only 14.3% of residual px touch a pixel whose face id changed. At every residual
px exactly one build's F equals A byte-exactly: the detail multiply is present on
one build and exactly zero on the other.

Source identity, as attribution only: the pre-fix run's saved sources differ from
this tree's `native_renderer.cpp` by exactly the 24-line T29 near-clip hunk, and
`world_tile_atlas.cpp` is byte-identical; the detail resource, its atlas and its
shader leg are the same bytes on both sides. A source diff cannot prove a frame
cause, so the proof is a frame mutation:

* revert T29's near-plane expression to the box-unit constant `0.05` (T29's own
  m1 patch): source `85b00d68...` -> `fe39ae5d...`, exe `64ff69e7...`; the
  product then prints `gpu: world clip near=0.05000000 far=100.000000
  nearMapUnits=610.22 ... state=published` - the pre-fix 611-map-unit clip;
* `A_mut` = `f87f396a60dc72082f232bcd71547066c02c433e35ccda1576c3b21333ef9608` =
  the kept pre-fix A, **0 of 2,582,352 bytes differ**; `F_mut` =
  `21cb8aa28df87b6c869e12a12be240ab79c0d5d5370bf97b073652890280647f` = the kept
  pre-fix F, **0 bytes differ** (`max_delta` 0) - including all 2,812 residual px;
* `F_mut` vs the current F: 1,560,453 bytes (60.43%, max 234); `A_mut` vs A_ref:
  1,551,760 (60.09%). The detail leg is not a special case; it rides on the frame;
* restore: `cmp` clean, source sha back to `85b00d68...`, rebuilt; `A_res` =
  `6bb4d9a7...` and `F_res` = `2ffa7d2b...`, i.e. "restored A reproduces pin:
  YES / restored F reproduces pin: YES".

So the residual **is caused by T29's near-clip change**, measured at frame level
with one variable. Evidence: `evidence/t40-pixel-pins/detail-residual/`
(`mutation.md`, `readings.txt`, the analysis scripts and their outputs).

**Not isolated:** the pixel-level path inside the rasterizer that turns a
projection change into a different detail sample on an un-clipped face. A
depth-tie explanation is ruled out (a different winner would change the face id,
and the mask says it did not). No shader or data defect is claimed, and none is
needed.

## 5. Gates

| run | command | result | wall time |
| --- | --- | --- | --- |
| D1 | `bash world-material-semantics-check.sh` | **PASS**, 209 OK / 0 FAIL / 3 NOT VERIFIED, rc=0 | 17 min |
| D2 | `bash world-material-semantics-check.sh --mutation` | **PASS**, forward pins held, 5/5 mutations red, 5/5 sources back byte-identical, rc=0 | 20 min |
| D3 | `bash lighting-semantics-check.sh` | **PASS**, 96 OK / 0 FAIL = the T43 lighting 96/0 baseline, rc=0 | 7 min |
| D4 | `bash lighting-semantics-check.sh --mutation` | **PASS**, forward pins held, 4/4 mutations red, 4/4 sources back byte-identical, rc=0 | 12 min |

All four ran under the shared host lock, one waiter per run, one product launch at
a time, on the committed tree. Logs: `.scratch/t45/gates/<gate>-<mode>.log`.

**D1, material forward.** The wide-pose section reproduced its pins exactly: the
gate's own `Aw`/`Aw2`/`S1w`/`S2w`/`Cw`/`Dw` frames are byte-identical to the
qualifying hold's frames (`56aaa2f2...`, `56aaa2f2...`, `303010f2...`,
`124e7c48...`, `8ee623f5...`, `b4ff3e31...`), `Aw` differs from `A` on 2,570,233
bytes, `S1w vs S2w` = 775,135 / 24 / 386,760 / 388,375, `Cw vs Dw` = 688,102 / 60
/ 203,847 / 484,255, each direction with an `assert_ne` positive control. The run
printed 209 OK: 183 pre-existing assertions, which all re-passed, plus the 26
lines of the new section - so the new section did not disturb what it sits
beside.

**D2, material mutation.** Forward pins held, then:

| mutation | expected | observed |
| --- | --- | --- |
| m1 tangent basis -> geometric normal | tick-2000 bump pair collapses | differing 0 (was 79), row still reads applied - RED |
| m2 `$bumpmap2` samples layer 1's tile | bump2 pair moves | differing 11,746 (was 7,343) - RED |
| m3 `combineDetail` ignores `$detailblendmode` | detail histogram moves | differing 255,344 (was 271,060), histogram still 0x15 - RED |
| m4 feature bit from the VMT, not the blit | exports the whole VMT population | differing 417,045 (was 79), feature-apply still materials=45 - RED |
| m5 feature rect ignores the half-res atlas | 512x256 instead of 256x256 | `512x256` - RED |

Every mutation's source came back byte-identical (`RESTORE=OK`, five times).

**D3, lighting forward.** Exactly the T43 96/0 baseline: determinism `A vs A2`
0/0, `A->B` dynamic 1,776,259 / 77, `A->C` specular 390,361 / 15, `A->D` bump alone
0/0, `B->E` bump under dynamic 910,776 / 13 (830,580 brighter / 80,196 darker),
switch identity B `1/0/0`, C `0/1/0`, D `0/0/1`, E `1/0/1`, F requested 1 /
effective 0 with the `$bumpmap` refusal named, preset `0/0/0`, explicit switch
outranks the preset. The gate rewrote three evidence files whose only difference
was the run tag and trace timestamps; those were reverted rather than committed as
noise, so the committed lighting evidence still describes the same pins.

**D4, lighting mutation.** Forward pins re-passed (96 OK / 0 FAIL, identical to D3, including `A->B`
1,776,259 / 77, `A->C` 390,361 / 15, `A->D` 0/0, `B->E` 910,776 / 13), then:

| mutation | expected | observed |
| --- | --- | --- |
| m1 `$bumpmap2` reports applied instead of refused | the fixture still reads it as refused | "RED m1: `$bumpmap2` state = applied (was refused)" and the fixture's `bump2.refused` self-test goes red |
| m2 the detail material's `%detailtype` is no longer read | the fixture names the value | "RED m2: the fixture no longer reads `%detailtype`" |
| m3 `--world-dynamic-light` also flips specular | specular stays 0 in the dynamic-only run | "RED m3: specular = 1 in the dynamic-light-only run" |
| m4 bump reports effective without a bound texture | bump=0 with texture `-` | "RED m4: bump = 1 with bumpTexture = - (was 0 / -)" |

All four sources came back byte-identical (`RESTORE=OK` four times), the gate
restored `evidence/lighting-semantics/` and the final verdict is "PASS: forward
pins held, all four mutations went red, every source came back byte-identical."
The five evidence files the run rewrote differ only in the run tag and the
trace's millisecond timestamps, so they were reverted rather than committed as
churn; the committed lighting evidence still describes the same pins.

## 6. NOT VERIFIED

* The specular reader at the tick-2000 reference camera is a measured zero
  (`S1 == S2`, 0 bytes). The wide pose is a second view with a visible
  population, not a replacement; both are pinned side by side.
* The material-normal direction at the tick-2000 reference camera: 28 px, one
  way only. Measured and explained (occlusion by the T29 near clip plus one
  surface), not replaced.
* The pixel-level rasterizer mechanism behind the detail residual (see section 4).
* The wide pose is a diagnostic view of the observer camera's drawn set, so it
  contains holes; it does not verify the scene at that camera.

## 7. Files, commits, merge risk

```
d07dc6f  evidence(t45): reproduce T40's detail residual with the near clip, byte for byte
0816ad9  gate(t45): pin both material consumers at a qualified wide-area pose
98f73ef  evidence(t45): material gate forward PASS, five mutations red, wide pins exact
<this commit>  report(t45): T45 report, gate verdicts and the NOT VERIFIED list (tip of
task/t45-visible-material)
```

* `world-material-semantics-check.sh`: adds the wide-pose section (six captures,
  14 assertions, exact pins, `assert_ne` positive controls), rewrites the 5b
  detail line from "attributed but not explained" to "pixel-level mechanism only
  (the cause is measured)", and adds the wide-pose numbers to the two other 5b
  lines. No existing pin was weakened, no assertion removed.
* `evidence/t40-pixel-pins/wide-pose/`: the nine-pose qualification table,
  readings, the two pose frames (PNG) and a documentation copy of the run scripts.
* `evidence/t40-pixel-pins/detail-residual/`: the residual analysis, its inputs'
  sha256 list, the mutation readings and the run script copy.
* `evidence/material-semantics/`: rewritten by the gate run itself.

Merge risk, stated plainly:

1. the material gate is now longer: six more product launches per run (the
   mutation mode re-runs the forward section, so both modes pay it); measured
   wall time in section 5;
2. the gate now depends on `--debug-camera` honouring a pose. If that flag's
   parsing or application changes, `Aw` would equal `A` and the gate goes red by
   design ("wide pose did not move the view");
3. the wide pins are frame-content pins on a nonzero, two-sided population, so
   they are sensitive to any shader change that alters the material normal - as
   intended, but a red there is a real semantic change, not flakiness;
4. nothing in `native/` changes, so the renderer chain and the other gates are
   untouched by the code; the extra launches are the only cost to the chain.