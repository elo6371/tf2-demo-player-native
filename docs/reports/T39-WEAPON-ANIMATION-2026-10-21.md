# T39 — weapon animation: the one-pixel identity pair and the rule that replaces it

Task: close `verify-all.sh` step 22, which was red by one pixel / three bytes in the
identity-skinning control pair, on the exact final source tree.
Branch `task/t39-weapon-animation`, from `34e1714`. Worktree
`D:\TF2_Native_Worktrees\t39-weapon-animation`.

Result: step 22 is green on the clean tree with a semantic assertion that measures
where the identity/skinning difference may be, not a byte bound that asks a raster
tie to disappear. The near plane was not touched (the T34 attribution is refuted and
stays refuted). Forward gate: `WEAPON-ANIMATION=PASS`, 57 OK, 0 FAIL, exit 0.

Commits on the branch: `bb37b15` (the rule, the tool, the gate and the chain pin),
`3d30319` (the logs and mutation-failure evidence), and the commit that adds this
report. Tree left clean; no processes left running.

## 1. The red, exactly

The failing assertion was `identity skinning does not deform the bind pose`, a
whole-frame `bmp-pixel-diff.py --max-delta 1 --max-diff-percent 0.001` between the
two runs of the standalone `--model` path that differ only in the shader branch:

```
tf2_demo_native.exe --tf-root <TF> --audio-device 6 --hidden-window --audio-disabled \
  --model models/player/scout.mdl --capture-frame <bmp> --model-skinning 1   # identity
tf2_demo_native.exe ... --model-skinning 0                                 # control
```

Both frames are 1264x681, 24 bpp BI_RGB, bottom-up, 2582406 bytes (54-byte header,
stride 3792, 2582352 pixel bytes, 860784 pixels). The pair reproduces the chain's own
frames bit-for-bit (the chain's evidence/verify/22-weapon-animation frames have the
same two hashes):

| arm | sha256 |
| --- | --- |
| identity (`--model-skinning 1`) | `a06f4e360b2be20257ae885804b40724e17b6cdecda7fede1ba4724a4c07670d` |
| control (`--model-skinning 0`) | `37e5eb2a473d5003c8f64d098be4e38d89069280cf98cc6b09a61e382c99939e` |

The whole frame differs in **3 bytes = 1 pixel**, at top-left coordinate
`(x=622, y=412)` (stored row 268 of the bottom-up file), file offsets
**1018176..1018178**:

| | stored B,G,R | RGB | delta |
| --- | --- | --- | --- |
| identity | 120, 114, 114 | (114, 114, 120) | |
| control | 179, 169, 169 | (169, 169, 179) | B +59, G +55, R +55 |

Every other one of the 860783 pixels is byte-identical.

The pixel sits on the boundary between two of the model's own surfaces: its four
neighbours are byte-identical in both arms and measure

```
left  (621,412) = (114,114,120)  dark
right (623,412) = (170,170,179)  bright
up    (622,411) = (163,163,172)  bright
down  (622,413) = (114,114,120)  dark
```

so the pixel is on the diagonal edge between the dark and bright surfaces. In the
identity arm it is owned by the dark surface, in the control arm by the bright one.
The 4-neighbour max-channel gradient at the pixel is 59 in the control frame (to the
left/down neighbours) and 59 in the identity frame (to the right neighbour): the pixel
is on the strongest local contrast in either frame, and the change is exactly that
surface-to-surface gap (59 in blue, 55 in red and green), not a small perturbation.

The two values are the same surface colour under two face orientations. The standalone
model path builds the vertex buffer once in `Renderer::uploadBindPoseModel`
(`native/src/native_renderer.cpp:2564`): vertices are centred and scaled by
`1.2 / max extent` (so the model always occupies the same 1.2-unit box in both arms —
the model bounds are identical by construction, not by measurement), and the vertex
colour is forced to `(0.72, 0.72, 0.76)` for every vertex. The model pixel shader
(`native_renderer.cpp:612`) is untextured and returns
`colour * (0.62 + 0.38 * saturate(dot(normal, L)))`. With colour (0.72,0.72,0.76):

- `114,114,120` = colour × 0.620 — a face turned away from the light (diffuse 0);
- `169,169,179` = colour × ~0.922 — a face turned toward it (diffuse ~0.80).

Both arms upload the same 78 identity matrices and the same vertex buffer; the only
product state that differs between them is the shader branch flag.

## 2. Classification: deterministic raster edge, not a pose, resource, or product error

Each of the candidate classes was tested against the pair; the readings:

**Not run noise / not nondeterministic.** Three repeats of each arm, in two flag
variants — the gate's own invocation and the mandated `--hidden-window
--audio-disabled` invocation — produce byte-identical frames: all three identity
repeats hash `a06f4e36...`, all three control repeats hash `37e5eb2a...`, and the
same-arm repeat diffs are 0 differing bytes. The gate-exact and quiet captures are
also pixel-identical to each other (0 differing bytes, max delta 0). So the residual
is deterministic to the byte on this machine, and the mandated flag set changes no
pixel.

**Not an animation pose difference.** Both arms run with no sequence
(`seq=-1 frame=0/0 reason=no --model-sequence`), so the matrices are identity and
the animation chain is not involved at all; the only trace difference between the
two runs is `skinning=1` vs `skinning=0`. The traces otherwise agree line for line
(timings modulo):

```
gpu: model skin source=identity bones=78 uploaded=78 skinning=1|0 seq=-1 frame=0/0 ...
gpu: world clip near=0.00051665 far=100.000000 nearMapUnits=4.00 farMapUnits=774222 horizontalScale=0.00012916 state=published
gpu: world vis ... drawnTriangles=179881 ... eyeMap=(3083.0,-2988.0,1024.0) ...
draw: end frames=1 pending=0
```

Same camera pose, same near/far, same draw list, same 78 bones, same model. A pose
change also cannot be 1 pixel: the animated arm (a real pose) differs from identity
in 42128 bytes / 14064 pixels.

**Not resource selection.** The two runs upload the same 9 model textures, the same
540153-vertex world lightmap pass, and the same drawn-triangle count; `uploaded=78`
bones in both. Nothing about which resource is bound differs.

**Not the near plane.** The T34 round attributed this pair to `--near-clip`; the
Lead's controlled test already refuted that (the pair still differed by 3 bytes at
delta 59 after the near-plane change), and this pair's two traces carry the identical
`world clip` line above. No near-plane change was made in T39 and none is needed.

**The remaining class, and the evidence for it: raster-edge ownership.** The pixel is
a single sample on the boundary between two surfaces. The skinning shader evaluates
`sum_i(w_i * M_i * float4(position,1)).xyz` with normalized weights
(`native_renderer.cpp:590`), a different float expression from the passthrough
`position`. With identity matrices the result equals `position` only up to float
rounding; two co-located triangles' shared edge can move by sub-ULP amounts that
differ per vertex (each vertex has its own weight set), so a sample exactly on the
edge is covered by a different triangle. The two triangles carry different surface
normals (the model is hard-edged there: the neighbours jump 55-59 in one step), so the
fragment takes the other face's lighting and the colour changes by 55/55/59. All
other 860783 pixels — including every other edge sample — are byte-identical.

The picture is therefore a correct rendering of the same geometry: no vertex moved
(enough to change any other pixel), no matrix or resource reading differs, and the
change is confined to one sample whose ownership is decided by float rounding at a
soft boundary. It is a deterministic raster edge, and the product is doing the right
thing. That is a statement about the evidence, not a wish: the three counters that
could have shown a real deformation (pixels moved, interior colour changed, bytes
changed away from an edge) are all at their strict limits — 1 changed pixel, 0
interior bytes changed, 0 changed pixels away from a measured edge.

The cheapest way to falsify this later is a build that dumps, for the sample
`(622,412)`, which triangle covered it and that triangle's normal in each arm. That
instrumentation is not in T39; the report calls it out under NOT VERIFIED.

## 3. The replacement rule

The old assertion asked for bit-identical rendering (`max_delta <= 1` everywhere,
`<= 0.001%` differing bytes). A raster ownership tie cannot satisfy that, and
byte-identity was not the claim the gate needed to make; squeezing the rule until the
number is green would have made it vacuous. It is replaced by a rule that states the
claim and measures it, implemented in the new `bmp-silhouette-diff.py` (root, next to
the existing `bmp-pixel-diff.py`):

1. **Edge mask, derived from the two frames.** A pixel is an edge pixel when its
   4-neighbour maximum per-channel difference in the `--edge-frame` reaches
   `--edge-threshold 32`. No pixel is listed by hand. The threshold is a stated
   parameter, not a natural break, so every run prints the population it selects.
   On the gate's control frame the gradient histogram is 823210 px at 0, 2813 at 1,
   734 at 2 ... 18446 at 32 or more, up to 222 at the strongest face boundary; the
   mask is **18446 px = 2.1429% of the 860784-pixel frame**, and the run also asserts
   it is between 1000 and 86078 px (10% of the frame), so an empty or whole-frame mask
   cannot make containment vacuous.
2. **Containment.** Every changed pixel must be inside the mask: `outside=0` is
   mandatory. Measured: 1 changed pixel, inside.
3. **Interior is strict.** Outside the mask the max channel delta must be <= 1
   (measured 0). So a change that moves colour anywhere off the measured edge is red,
   not merely bounded.
4. **Count.** Changed pixels are capped at 4. Measured 1. The cap is margin for this
   class of tie at a couple more samples, and it is three orders of magnitude below a
   real geometry change (a real pose is 14064 pixels; see the control).
5. **Global ceiling.** The whole-frame max channel delta must be <= 128 of 255. The
   measured edge value is 59 of 255, so the ceiling is above the measured raster edge
   and below a moved surface (the pose control measures 181). No large move can hide
   inside the mask.

The control is the third leg: **a real pose change must fail the same rule**. Against
the animated frame the rule reports `changed=14064 changed_bytes=42128 inside=2496
outside=11568 interior_max=172 global_max=181 status=FAIL`, i.e. it rejects on
containment, count, interior strictness and the ceiling at once. A rule that passes
the identity pair and rejects a real pose difference is a measurement of where a
permitted difference may be, not a filter.

The rule is also mutation-guarded: the gate now has `--mutation-identity`, which adds
`matrix[12] = 0.02f;` to the identity matrices in `native/src/main.cpp`
(`identityMatrices`, the translation row of the standalone model transform). The
picture moves; no counter, trace field, texture or resource reading changes. The gate
requires this mutation to be caught **by the silhouette rule's own failing line**
(it greps the FAIL lines for the exact label), so the mutation cannot be read as
caught by some unrelated assertion.

## 4. Readings

Forward gate on the final tree (product sources byte-identical to `34e1714`,
`git diff --exit-code -- native/src native/tools` clean; gate at this branch):

```
WEAPON-ANIMATION=PASS    exit 0, 57 OK, 0 FAIL
sections: 27 / 6 / 5 / 12 / 6 / 1 = 57
section 4: BMP-SILHOUETTE changed=1 changed_bytes=3 inside=1 outside=0
           mask_pixels=18446 mask_share=2.1429 interior_pixels=842338
           interior_max=0 global_max=59 edge_threshold=32
           extent=1264x681x24 frame_pixels=860784 status=PASS
           control: changed=14064 ... outside=11568 interior_max=172 global_max=181 status=FAIL
```

## 5. Mutations and the restore proof

All three step-22 mutations were run in the gate's own mutation mode on this branch.
Each run builds the mutated product, runs the gate, restores the source with the
gate's backup copy, and rebuilds. Source hashes below are `native/src/main.cpp` and
`native/src/animation_binding.cpp`; every run ended with
`git status --porcelain -- native/src native/tools` empty and `git diff --exit-code`
clean.

| invocation | source patch | gate reading | caught by |
| --- | --- | --- | --- |
| `--mutation` | sampled pose forced to the bind pose | 48 OK / 9 FAIL | section 1/2: no witness moved, pose counts 0 -> 0 |
| `--mutation-model` | inverse bind matrices loaded on the `--model` path | 18 OK / 1 FAIL | the silhouette rule: `changed=851985 outside=838069 interior_max=182 global_max=182` |
| `--mutation-identity` | `matrix[12] = 0.02f` in `identityMatrices` | 18 OK / 1 FAIL | the silhouette rule and nothing else: `changed=8747 outside=6744 interior_max=136 global_max=163` |

All three print `MUTATION-CAUGHT=PASS` and exit 0. The third is the one that guards
the replacement rule: it moves the model transform by 0.02 of the 1.2-unit box while
every counter, trace field and resource reading stays put, and the gate's tail
requires the failing lines to contain the exact label
`identity skinning changes nothing outside the measured edge mask` — so it cannot be
read as caught by an unrelated red. With 6744 of 8747 changed pixels off the measured
edge it is red on containment, count, interior strictness and the global ceiling at
once.

Restore proof, identical in all three runs:

```
SHA_BEFORE = SHA_AFTER (main.cpp 7d20b47e..., animation_binding.cpp 5b0facc...)
GIT_STATUS_BEFORE = []   GIT_STATUS_AFTER = []
GIT_DIFF_EXIT = 0        restore build rc = 0
MUTATION-CAUGHT = PASS
```

The restore rebuild produced `tf2_demo_native.exe` hash `10147e9e...` against the
pre-mutation build's `5fa0b853...` although the sources are byte-identical: this
MSVC link is not bit-reproducible (embedded link metadata). That is exactly why the
final forward run in section 7 is made on the restored binary rather than trusting the
earlier one.

Supplementary check (not part of any gate): the same identity/control pair on the
wave witness `models/props_ui/bannerflag_comp_l.mdl` (16 bones) is **byte-identical**
in both arms — same hash `5c2d848f...`, 0 differing bytes, mask 16115 px,
`BMP-SILHOUETTE changed=0 status=PASS`. So the scout residual is one sample's raster
tie on one model, not a systematic offset of the identity path; a real transform
error would have moved the 16-bone model too.

## 6. verify-all.sh step 22

The pin is now **57** (was 56), measured from the forward run above, with the
comment's arithmetic updated to section 4's 12 assertions (the old byte bound is
gone; the rule and its control are the two replacement assertions). The step now runs
the third mutation, `--mutation-identity`, after the existing two; on this branch the
gate prints `MUTATION-CAUGHT=PASS` for it (section 5), which is what the step's
`WEAPON-ANIMATION-MUTATION-IDENTITY=PASS` line reports when the chain runs its replay.
The chain itself was not run by T39 (it is the Lead's final step).

## 7. Gates re-run, and the final forward reading

The two ViewModel gates the chain runs after step 22 were run forward on the restored
tree, against the same `tf2_demo_native.exe` the restore produced (and a fresh
`viewmodel_animation_probe.exe` built for this run):

| gate | reading |
| --- | --- |
| `viewmodel-animation-check.sh` (chain step 23) | `VIEWMODEL-ANIMATION=PASS`, 154 OK, exit 0 — the chain's pin is 154 |
| `viewmodel-companion-check.sh` (chain step 24) | `VIEWMODEL-COMPANION=PASS`, 114 OK, exit 0 — the chain's pin is 114 |

Step 23's log is committed at `evidence/verify/23-viewmodel-animation.txt`; step 24's
is `.scratch/t39/evidence/24-viewmodel-companion.txt` (the chain writes its own copy
into its step output and does not commit that log, so it is not added here).

Then step 22's gate was run forward **again**, on the restored binary, as the final
check rather than trusting the pre-mutation run:

```
WEAPON-ANIMATION=PASS    exit 0, 57 OK, 0 FAIL
pair hashes: a06f4e36... / 37e5eb2a...  (identical to the failing chain's frames)
```

All of step 22's witness frames reproduce byte-for-byte across the two builds and the
two flag sets. Compared with the evidence the failing chain left on disk
(`D:\TF2_Native_Test\evidence\verify\22-weapon-animation`):

| frame | chain (no flag pair) | T39 (restored build, `--hidden-window --audio-disabled`) |
| --- | --- | --- |
| entity tick 2000/4000/6000 | `f841c39f...` (one picture, still camera) | `f841c39f...` (same) |
| model identity | `a06f4e36...` | `a06f4e36...` |
| model control | `37e5eb2a...` | `37e5eb2a...` |
| model animated | `7f4101b8...` | `7f4101b8...` |
| model wave-a | `1812c700...` | `1812c700...` |

So the `--hidden-window --audio-disabled` pair the gate now passes changes no pixel
on the model pair *or* on the three entity captures, and the second build of the same
sources renders the same frame set.

## 8. NOT VERIFIED

- The per-sample triangle/normal instrumentation described in section 2 was not
  written. The classification rests on the frame pair, the trace pair, the shader
  expression and the determinism repeats; the mechanism is inferred from those, not
  observed at the rasterizer.
- The mask threshold 32 is a stated parameter, not a natural break in this frame's
  gradient population (which is a continuum up to 222). What is measured is the
  population, the containment, the interior strictness, the count and the ceiling.
- The second-model check (`models/props_ui/bannerflag_comp_l.mdl`) is supplementary
  evidence only; the gate's assertion stays on the pinned scout pair.
- The link is not bit-reproducible here: two builds of byte-identical sources gave
  `tf2_demo_native.exe` hashes `5fa0b853...` and `10147e9e...`. The frames and the
  gates are identical, but any future assertion that two rebuilds of the same tree must
  share an exe hash would be red for a benign reason.
- The same single-sample tie was not re-run on a different GPU or driver. The rule is
  self-measuring (the mask comes from the run's own frame and the count is capped), so
  a different one-sample edge tie would still be contained and a real move would not,
  but that was not tested on another adapter.

## 9. Residual merge risk

- **The chain gets one more invocation.** `verify-all.sh` step 22 now builds, runs
  and restores the identity mutation as well: about +3 minutes on the chain (two
  rebuilds and the section 4/5/6 launches; sections 1-3 are skipped under this
  mutation, like the model mutation).
- **The pin and the gate must merge together.** Step 22's pin is 57, measured from
  this gate. The new gate emits 57 assertions and the old pin counts 56, and the old
  gate emits 56 while the new pin counts 57: either half alone leaves step 22 red on
  the count. They are one commit here for that reason.
- **The new tool must merge with the gate.** `weapon-animation-check.sh` lists
  `bmp-silhouette-diff.py` as a prerequisite and exits 2 if it is missing. It is a
  new tracked file at the repository root, in the same family as
  `bmp-pixel-diff.py`; a merge that drops it makes step 22 fail loudly, not silently.
- **If a future tree moves the residual to a non-edge pixel**, the gate goes red on
  containment and that is the honest outcome: it means colour changed where the
  reference frame has no edge. The fix then is to measure that frame again, not to
  widen the threshold without a reading.
- **The rule is not a general licence for edge noise.** It is asserted over one pinned
  pair with a fixed count cap and ceiling; the pose control in the same section proves
  it rejects a real pose change. If a later round wants to relax the cap or the
  ceiling, the control reading (14064 changed, outside 11568) is the number to argue
  against.
