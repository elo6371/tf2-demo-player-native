# T48 -- the one-sample identity-skinning difference: measured and bounded

Status: **both host-lock holds complete.** Hold 1 (forward gate, 7m39s at commit
`5e90026`): `WEAPON-ANIMATION=PASS`, 57 OK / 0 FAIL, the pinned frames returned
byte-identical to the T43 manifest, the silhouette rule PASS, the real-pose
control still rejected, gate rc=0, tree clean. Hold 2 (mutation, 2m07s at commit
`8de25f1`): `MUTATION-CAUGHT=PASS`, the 0.001-of-the-box pose displacement rejected
on the silhouette rule's own label, `main.cpp` byte-identical after the run, tree
clean, and the identity/control pair re-captured on the restored binary at
`a06f4e36...`/`37e5eb2a...`. Sections 1-6 below are all measured.

Boundary: base commit `6888e6d`, worktree `D:\TF2_Native_Worktrees\t48-raster-sample`,
branch `task/t48-raster-sample`. Write scope: `weapon-animation-check.sh`,
`bmp-silhouette-diff.py`, `evidence/weapon-animation/`. No `native/` source is
changed; `verify-all.sh` and `evidence/verify/` are not touched.

## Verdict

The identity-skinning pair at the T43 inputs differs by **exactly one sample**
(pixel 622,412) and that sample's two candidate colours are the same vertex
colour under two different normals. The accepted silhouette rule is unchanged and
still passes. The difference is **bounded, not proven**:

* the skinning branch, with identity matrices, can move a vertex by at most
  **1e-6 of the model's 1.2-unit box** (the multiply by an identity row is exact;
  only the normalised-weight sum rounds);
* at the model's measured **223 px/unit** upper-bound scale that is
  **0.00022 px**;
* the model carries **1,069 visible boundary samples** at the rule's own
  gradient threshold, so the expected number of one-sample coverage ties from a
  shift of that size is **2 x 0.00022 x 1069 = 0.48** (0.07 at a realistic
  rounding error);
* observing one such sample is therefore the expected order of magnitude for
  this shift, not an anomaly -- and because the pipeline is deterministic, the
  same sample is the same sample on every run (verified byte-identical across
  two separate builds, T39's and T43's).

A displacement a thousand times larger is caught with a wide margin: the new
`--mutation-pose` moves the bind pose by 0.001 of the box (0.22 px) and the
accepted rule rejects the pair on its own label (`changed=3,322`, `outside=2,397`,
`interior_max=73`, `global_max=145`), with the source and the frame hash restored
byte-identically (section 5). The rule's measured floor is therefore about 1,000x
coarser than the float difference this pair is expected to tolerate, which is
what makes "one sample" an expected reading rather than a gap.

What is **not** proven is the per-sample ownership decision itself: no
instrumentation in this tree exposes the model path's clip position, post-divide
position, viewport position or depth (section 2), and T48 is diagnostic-only, so
the mechanism is stated as a *bound* -- "the sample lies within 0.00022 px of a
raster edge and the two paths' float difference can flip it" -- and **not** as
"the GPU chose triangle T".

## 1. The pair at the T43 inputs (step 1)

The T43 release chain (`verify-all.sh --quick` on `6888e6d`) produced the step-22
frames itself; they are still on disk under
`D:/TF2_Native_Test/evidence/verify/22-weapon-animation/` and hash equal to the
values the chain manifest recorded, and to the pair T39 measured:

| frame | sha256 |
|---|---|
| model-identity.bmp | `a06f4e36...07670d` |
| model-control.bmp | `37e5eb2a...c99939e` |
| model-animated.bmp | `7f4101b8...284f65e` |
| tick-2000/4000/6000.bmp | `f841c39f...2ced6b5` (all three identical: still camera) |

The two runs differ only in `--model-skinning 1` versus `--model-skinning 0`;
both upload the same 78 bone matrices, so the only difference in the vertex
shader is the branch at `native/src/native_renderer.cpp:585`.

Readings of the pair, and of the real-pose control (T43-input frames):

```
pair:   changed=1 changed_bytes=3 inside=1 outside=0 mask_pixels=18446 mask_share=2.1429
        interior_pixels=842338 interior_max=0 global_max=59 edge_threshold=32
        extent=1264x681x24 frame_pixels=860784 status=PASS
control changed=14064 changed_bytes=42128 inside=2496 outside=11568 mask_pixels=18445
        interior_pixels=842339 interior_max=172 global_max=181 status=FAIL
        reason=interior_max=172 exceeds 1; changed=14064 exceeds 4;
        global_max=181 exceeds 128; outside=11568 changed pixels are not on a measured edge
```

The changed sample, in the pair:

```
pixel 622,412   stored row 268   data offsets 1018122..1018124 (file offsets 1018176..1018178)
identity (114,114,120)   control (169,169,179)   delta (55,55,59) max_channel=59
neighbour left  (114,114,120) in BOTH frames      right (170,170,179) in BOTH
neighbour up    (163,163,172) in BOTH             down  (114,114,120) in BOTH
4-neighbour gradient 59 in both frames; the derived edge mask covers the sample
```

R channel of the identity frame around the sample (columns 618..626; `*` marks
it); the control frame is identical except for the marked cell:

```
y=408  114 114 114 114 114 114 114 114 114
y=409  171 172 172 114 114 114 114 114 114
y=410  114 114 114 114 114 114 114 114 114
y=411  162 162 162 163 163 163 163 164 164
y=412  114 114 114 114 *114 170 170 171 114
y=413  114 114 114 114 114 114 114 114 114
y=414  144 144 144 134 124 114 114 114 114
y=415  149 149 149 148 148 148 147 147 147
y=416  173 172 171 171 170 156 151 151 151
```

1 of the 81 pixels in that window differs. The window histogram is dominated by
`(114,114,120)` (43 samples) with bright bands of `(163,163,172)`, `(162,162,171)`
and the sample's `(169,169,179)` neighbours. The frame is 95.4% `(14,18,22)` and
the world near this window is dark too (R 27..30 at the window's edge), so both
candidates are **model** surfaces, not model-versus-world: the sample sits at the
corner of a **1-pixel-tall bright band** whose lower boundary steps down exactly
at the sample's column (in the identity frame the band starts at x=623 on row 412;
in the control frame it starts at x=622).

**Hold 1 confirmation.** The forward gate re-ran on this worktree under the lock
(commit `5e90026`, 7m39s, no `--force`): `WEAPON-ANIMATION=PASS`, 57 OK / 0 FAIL,
gate rc=0, and the pinned frames returned byte-identical -- `a06f4e36` (identity),
`37e5eb2a` (control), `7f4101b8` (animated), plus `1812c700`/`478607ec` for the wave
pair and `f841c39f` for all three tick frames. The rule's two lines in section 4
are that run's own output. Log:
`evidence/weapon-animation/t48/forward-hold1-readings.txt`.

The model's footprint, measured as the pixels the pose change moves
(identity frame vs animated frame, which cannot move the still world), is
`14,064 px` in a `218 x 155 px` box at `x=537..754, y=375..529`, of which
`9,247 px` carry the model shader's forced vertex colour `(0.72,0.72,0.76)`.

## 2. What the instrumentation permits (step 2)

The `--model` path's trace carries these lines and no others about the model:

```
gpu: model skin source=identity bones=78 uploaded=78 skinning=1 seq=-1 frame=0/0 reason=no --model-sequence
gpu: model skin source=identity bones=78 uploaded=78 skinning=0 seq=-1 frame=0/0 reason=no --model-sequence
gpu: world clip near=0.00051665 far=100.000000 nearMapUnits=4.00 farMapUnits=774222 horizontalScale=0.00012916 state=published
```

So, of the quantities step 2 asks for:

| quantity | status |
|---|---|
| interpolated clip position (pre-divide) | **NOT AVAILABLE** -- the product records no mvp or clip output for this path |
| post-divide NDC position | **NOT AVAILABLE** -- same |
| viewport position | **NOT AVAILABLE** -- same |
| depth | **NOT AVAILABLE** -- the frame capture stores colour only, no depth buffer |
| shader output at the sample | **MEASURED** (the two colours below), via the captured frame |

The two measured shader outputs reconcile exactly with the product's model pixel
shader (`native/src/native_renderer.cpp:612-619`, vertex colour forced to
`0.72,0.72,0.76` by `uploadBindPoseModel`, `lighting = 0.62 + 0.38*diffuse`):

```
identity (114,114,120) -> lighting 0.618192..0.621775, diffuse saturated at 0
control  (169,169,179) -> lighting 0.921053..0.923203, diffuse 0.792244..0.797902
```

Both are one vertex colour under two normals 0.80 apart in the diffuse dot
product: the two paths did not change the shading model, they changed **which
surface owns the sample**.

## 3. The bound (step 5: "bounded, not proven")

With `skinEnabled > 0` and an identity matrix, the shader's expression reduces
exactly to the passthrough:

```
mul(float4(p,1), identity).xyz  ==  p                    (each product is x*1, y*0, z*0, 1*0)
skinnedPos = sum_i (w_i / W) * p = p * (1 + e),  |e| <= a few ulp
```

so the only difference is the rounding of the normalised-weight division and the
three-term sum: at most about `1e-6` of a unit, i.e. `1e-6` of the model's
1.2-unit box, in the worst case. Scaled by the measured footprint
(`222.9 px/unit` upper bound from the 267.5 px box diagonal over the 1.2-unit
box) that is a **0.000223 px** displacement, and at a realistic rounding error
(`1.5e-7` units) **0.000033 px**.

The number of opportunities for such a shift to change a sample is the model's
population of contrasty boundary samples, measured inside its own footprint at
the rule's threshold:

```
gradient >= 32:  2,139 samples  -> 1,069 boundaries   expected ties 0.477 (worst case) / 0.071 (realistic)
gradient >= 48:  1,245 samples  ->   622 boundaries   expected ties 0.278 (worst case)
```

(the halving is because a boundary has two sides; each sample's boundary sits at
an unknown position inside its pixel, so the chance of a `d` px shift flipping it
is about `2d`.)

The pixel classification alone only bounds the band's edge to
`(621.5, 622.5)` in one arm and `(622.5, 623.5)` in the other. The shift bound
is what turns "the sample flipped" into "the sample is within 0.00022 px of the
edge": if each arm's edge lies on its own side of the sample centre (622.5) and
the arms differ by at most 0.00022 px, the edge is within 0.00022 px of the
centre in both.

So: a `0.0002 px` difference across `~1,000` boundaries predicts **of order one**
flipped sample, and **one** is what the pair shows. The flip is visible at all
because the two surfaces are 55..59 levels apart (the mask's own measured edge
delta), not because anything moved: outside the one sample, the pair's largest
difference anywhere is **0**, and its interior maximum is **0**.

The observation is deterministic, not flaky: the two frames are byte-identical
across T39's build and T43's build (different executables), so the same float
values reproduce the same sample on every run.

## 4. The rule is unchanged and still passes (step 3)

Re-ran the accepted rule on the T43-input frames (the tool's rule path is
untouched by this round; only the new `--inspect-pixel` mode was added):

* outside-mask max channel delta `<= 1` -> measured **0**;
* every changed pixel inside the derived edge mask -> **1 of 1** (outside **0**);
* changed pixels `<= 4` -> measured **1**;
* global max `<= 128` -> measured **59**;
* mask population within `[1000, 86078]` -> measured **18,446** (2.1429%);
* real-pose control (identity vs animated) -> **rejected**, `rc=1`,
  `outside=11,568`, `changed=14,064`, `interior_max=172`.

The T43 chain ran the same rule as step 22 with 57 assertions and 0 failures.

## 5. The pose-displacement mutation (step 4) -- caught, and the source restored

`--mutation-pose` patches `native/src/main.cpp` where the `--model` path builds
its identity matrices and adds `matrix[12] = 0.001f`: a rigid
**0.001-of-the-box** displacement of the bind pose, `0.22 px` at the measured
scale. Every counter in the gate stays at its forward value -- the mutated run's
`readings.txt` shows the same skin-upload lines and counts, and sections 1-3 are
skipped because they read the entity path -- so only the picture moves. The gate
requires the failure to come from the silhouette rule itself, the same
`CAUGHT_BY_RULE` check as `--mutation-identity`.

Hold 2 (2m07s, commit `8de25f1`) caught it:

```
MUTATION-PATCHED
mutation build rc=0
MUTATION-CAUGHT=PASS (pose mutation turns this step red)
mutation rc=0

identity skinning changes nothing outside the measured edge mask:
  BMP-SILHOUETTE changed=3322 changed_bytes=7699 inside=925 outside=2397
  mask_pixels=18446 interior_pixels=842338 interior_max=73 global_max=145
  status=FAIL reason=interior_max=73 exceeds 1; changed=3322 exceeds 4;
  global_max=145 exceeds 128; outside=2397 changed pixels are not on a measured edge
```

That is the rule's measured sensitivity floor. A displacement of about a fifth of
a pixel moves **3,322 samples**, 2,397 of them off the measured edge mask, with a
worst interior delta of **73** levels: the accepted rule is not passing a
bit-identical pair by accident, it is passing a pair whose float difference is
about 1,000x smaller than this mutation's picture change.

Restore proof, all from the same hold:

```
main.cpp before  f8badad54adce891111fdbe4f99562f1321227a83bbbcb58ec3b8f360ff27bbd
main.cpp after   f8badad54adce891111fdbe4f99562f1321227a83bbbcb58ec3b8f360ff27bbd
gate backup      f8badad54adce891111fdbe4f99562f1321227a83bbbcb58ec3b8f360ff27bbd
RESTORE-SOURCE=MATCH   RESTORE-BACKUP=MATCH   modified paths after: 0
restore build rc=0
RESTORE-HASH identity=MATCH a06f4e360b2be20257ae885804b40724e17b6cdecda7fede1ba4724a4c07670d
RESTORE-HASH control=MATCH 37e5eb2a473d5003c8f64d098be4e38d89069280cf98cc6b09a61e382c99939e
RESTORE-CMP model-identity=identical-to-hold1-forward
RESTORE-CMP model-control=identical-to-hold1-forward
```

The step-22 frame hash returns on the restored binary, byte for byte identical to
hold 1's forward frames. The claim this hold supports is exactly: **the gate
catches a 0.001f pose displacement and restores byte-identically** -- it does not
prove the per-sample tie of section 3.

Readings: `evidence/weapon-animation/t48/mutation-pose-readings.txt`.

## 6. NOT VERIFIED / measurement limits

1. **Per-sample ownership is not observed.** No instrumentation in this tree
   exposes the model path's clip/NDC/viewport/depth, and the frame capture stores
   no depth buffer. The bound above is derived from the shader's float expression
   plus the measured frame populations; it is not a readback of the GPU's
   sample decision.
2. **The float bound is analytic, not measured against the compiled shader.** It
   assumes IEEE binary32 multiply/add with the matrix row exactly
   `(1,0,0,0)`; FMA contraction could change the rounding pattern but not its
   order of magnitude.
3. **Which surface is in front at the sample is not measured.** The two candidates
   are both model surfaces 55..59 levels apart and the sample is at the corner of
   a 1-px-tall bright band; a coverage tie at the band's edge is the cheapest
   explanation, but a depth-winner tie between overlapping surfaces would obey the
   same bound, so the report does not choose between them.
4. Cheapest way to *prove* the mechanism (needs product-source instrumentation,
   outside T48's write scope): add a diagnostic that dumps, for the candidate
   triangles at a named sample, the pre-divide clip position, the post-divide
   NDC, the viewport transform, the depth, the three edge functions at the sample
   centre and the interpolated normal, for both skinning arms. A cheaper
   alternative is a viewport-offset sweep: re-render with sub-pixel viewport
   shifts at 1/256 px and read pixel (622,412) until the colour flips -- the flip
   offset is the sample's distance to the edge, on hardware.
5. **No GPU frame debugger is installed on this host** (no RenderDoc, PIX or
   Nsight; checked in the usual install locations and on PATH), so the
   per-sample history of a captured frame is not obtainable by an external tool
   either. That is why item 4 is the only route.

## 6a. Observation: the committed witness directory is a superseded snapshot

`evidence/weapon-animation/` at `6888e6d` still holds the **2026-10-10** run:
`readings.txt` is stamped `2026-10-10T11:41:33Z`, the two `mutation-failure-*.txt`
files quote the pre-T39 byte-bound assertions (`identity skinning does not
deform the bind pose: BMP-DIFF differing=2542251 ...`), and `model-identity.bmp`
and `model-control.bmp` are **the same bytes** (`442ff83b...`). A reader of that
directory would conclude the identity pair renders bit-identically, which is the
claim T39 replaced and T48 bounds. Those files are the gate's *default* output
(`OUT=evidence/weapon-animation`), so any later hand-run of the gate rewrites
them anyway; this task deliberately wrote its output to `.scratch/t48/` and did
not refresh them, because a 5 x 2.5 MB binary refresh is a decision for the
integration owner, not a diagnostic side effect. Recommended: refresh them once
(one forward gate run with the default `OUT`) before the next freeze, so the
committed witnesses show the pair the rule actually measures.

## 7. Reproduce

```
PY=C:/Users/Administrator/.workbuddy-ai/binaries/python/versions/3.13.12/python.exe
E=D:/TF2_Native_Test/evidence/verify/22-weapon-animation   # the T43 chain's own step-22 frames

$PY bmp-silhouette-diff.py $E/model-identity.bmp $E/model-control.bmp \
    --edge-frame $E/model-control.bmp --edge-threshold 32 --max-interior-delta 1 \
    --max-changed 4 --max-global-delta 128 --min-mask 1000 --max-mask 86078
$PY bmp-silhouette-diff.py $E/model-identity.bmp $E/model-control.bmp \
    --edge-frame $E/model-control.bmp --inspect-pixel 622,412 --inspect-window 9
$PY evidence/weapon-animation/t48/measure_edge_population.py \
    $E/model-identity.bmp $E/model-animated.bmp
$PY evidence/weapon-animation/t48/reconcile_lighting.py 114,114,120 169,169,179
```

Recorded output: `evidence/weapon-animation/t48/` (`pair-hashes.txt`,
`rule-t43-pair.txt`, `pixel-inspect-t43-pair.txt`, `shader-reconciliation.txt`,
`edge-population-t43-pair.txt`, `forward-hold1-readings.txt`,
`mutation-pose-readings.txt`).

Host-lock holds (both ran, one waiter each, no `--force`; 7m39s and 2m07s):

```
HOSTLOCK_TIMEOUT=5400 bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run t39 \
  bash /d/TF2_Native_Worktrees/_shared/t39/t48-forward.sh    # build + forward gate at the T43 inputs
HOSTLOCK_TIMEOUT=5400 bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run t39 \
  bash /d/TF2_Native_Worktrees/_shared/t39/t48-mutation.sh   # --mutation-pose + restore proof
```

Both redirect the gate's output into `.scratch/t48/` so the committed
`evidence/weapon-animation/` witnesses are not rewritten by a diagnostic round.

## 8. Merge risk

* `bmp-silhouette-diff.py`: +160 lines, all new code reachable only through
  `--inspect-pixel`; the mask, the bounds, the verdict and the "no bound given"
  error are unchanged (rc=2 on an unchecked run, verified). No existing printed
  field changed.
* `weapon-animation-check.sh`: +54 lines, one new case (`--mutation-pose`), one
  new target in the entity-sections skip, one new target in the
  `CAUGHT_BY_RULE` list. The forward path is untouched: a chain run still calls
  the script with no argument and asserts the same 57 readings.
* `evidence/weapon-animation/t48/`: new files only; the tracked witness frames
  (`model-*.bmp`, `tick-*.bmp`, `readings.txt`, `mutation-failure-*.txt`) are
  untouched.
* No change under `native/`, no change to `verify-all.sh`, no change under
  `evidence/verify/`. Wiring `--mutation-pose` into the chain would cost about
  three minutes per run and is the chain owner's (T49) decision, not this task's.
