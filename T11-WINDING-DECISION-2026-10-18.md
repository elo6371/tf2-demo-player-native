# T11 third round -- the winding decision

Date: 2026-10-18
Task: `RELEASE-BLOCKERS-NEXT-WAVE-2026-10-17.md`, T11 (world geometry), remaining
item. Branch `task/t11-world-geometry`, worktree
`D:\TF2_Native_Worktrees\t11-world-geometry`.

## 1. What was still open, and why the previous answer was not an answer

The follow-up round left one item open: `misfaced=53724 of 99312`, the count of
emitted triangles whose winding points away from the side their own face's plane
side bit calls the front, with the top-down diagnostic camera losing its right
half to the cull. Its conclusion was "no plane-derived rule draws both views",
measured with flat-colour coverage against a no-cull reference.

That conclusion was true and unusable. Coverage counts a pixel as "world present"
whether the surface drawn there is the nearest one or a far one seen through a
culled near surface, so a rule that culls the nearest surface still scores: the
frame fills with whatever is behind it. Eleven rules were ranked by a metric that
cannot tell "correct surface" from "some surface".

## 2. The premise was wrong: the buffer ships one convention, not two

The class split the shipped rule reproduced came from the face POLYGON census.
The buffer is not the polygon: the brush fan emits
`pushTriangle(polygon[0], polygon[i + 1], polygon[i])`, the reverse of the
polygon's own order. Measured on the emitted order before any rule:

```
geometry pre-rule class=brush         alongPlane=41624 againstPlane=396
geometry pre-rule class=displacement  alongPlane=46856 againstPlane=142
```

Both classes ship the same convention (99.1% and 99.7%). There was never a class
split to reproduce, and the shipped rule was reversing 38614 displacement
triangles that did not need reversing.

## 3. The instrument: first-hit oracles

A back-face cull is correct exactly when, for every ray from the camera, the
first triangle the ray meets survives. That is a property of the data, not of a
rule, and it needs no shading, no materials and no thresholds. Two oracles were
added to `world_geometry_probe`:

- `--ray-down N` -- a vertical ray per grid cell across the map bounds; the
  topmost triangle at that (x, y) must be drawn by a camera looking down, i.e.
  its emitted winding must face up.
- `--ray-eye x,y,z N` -- a sphere of directions from the eye; the first triangle
  each ray meets must be drawn, i.e. face the eye.

Every triangle now carries `windingFlipped`, so the census recovers the STORED
winding and scores any rule without rebuilding it. Both oracles reduce to four
cells -- (class, side) -- and a rule is a keep/flip choice per cell, so all
sixteen rules are scored on both views in one run.

## 4. The decision table

`world_geometry_probe --ray-down 96 --ray-eye -2423.6,5566.0,-1280.0 24`
(5381 cells from above, 594 directions from the eye; full table in
`analysis/rule-decision.txt`):

| mask | brush/s0 | brush/s1 | disp/s0 | disp/s1 | top | eye | worst |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 03 (was shipped) | keep | keep | flip | flip | 0.311 | **0.820** | 0.311 |
| 05 | keep | flip | keep | flip | 0.980 | 0.325 | 0.325 |
| 07 (**now shipped**) | keep | keep | keep | flip | 0.708 | 0.771 | 0.708 |
| 13 | keep | flip | keep | keep | **0.983** | 0.283 | 0.283 |
| 15 (stored order) | keep | keep | keep | keep | 0.711 | 0.729 | **0.711** |

Three things the table settles:

1. The shipped rule is the eye's optimum and the top-down view's near-worst. It
   was not a compromise; it was a choice of one view.
2. No rule exceeds 0.711 on its worse view. The family's ceiling is 0.820 on the
   eye or 0.983 on the top-down, at different rules, never both.
3. The two views disagree in exactly TWO cells: brush/side-1 and
   displacement/side-0. That is the residual, with a number on each cell.

## 5. The change

`native/src/bsp_map.cpp`, one condition, no class sign, no side folding:

```cpp
if (haveFaceOutward && displacement) {
  if (nx * faceOutwardX + ny * faceOutwardY + nz * faceOutwardZ < 0.0f) {
    std::swap(b, c);
    ...
```

Brush triangles keep the order the map stores; displacement triangles are
reversed when they point away from their face's side-aware outward direction.
This is mask 07: within 0.003 of the best max-min of the sixteen, and the shipped
rule's smallest functional delta that recovers the top-down view. It is also the
cell the previous round had already named: `displacedMisfaced=38614`.

## 6. Readings

```
flippedTriangles   47252 -> 8514
misfaced           53724 -> 15126      (brush/side-1 only)
displacedMisfaced  38614 -> 0
top-down first hits drawn   0.311 -> 0.708
eye first hits drawn        0.820 -> 0.771
```

Frames, same camera and tick:

| frame | before | after |
| --- | --- | --- |
| top-down flat | `frames-followup/fin-top-flat.png` -- right half black | `frames-third-round/top-flat-mask07.png` -- drawn |
| top-down flat, mask 05 (the other extreme) | -- | `frames-third-round/top-flat-mask05.png` |
| POV textured | `frames-followup/fin-pov-normal.png` | `frames-third-round/pov-mask07.png` |

The POV frame is still a jumble of slabs under every rule in the family
(`frames-third-round/pov-stored-order.png` is the same camera with the rule
deleted), which is the material/triangulation defect that belongs to T12 -- the
winding rule is not what makes the POV look like slabs, and the eye oracle (not
the picture) is what ranks the rules there.

## 7. Gate

`bash visual-baseline-check.sh` -- **PASS**. Pins moved: the three winding
census numbers above, plus three new pins that are the decision itself
(`drawnByShippedRule=3809 of 5381`, `shippedRuleKeeps=458 of 594`,
`rule-table mask=07 top=0.708 eye=0.771`). Region pins W3/W4 re-pinned: the
bright panel that used to fill W3 was a displacement surface drawn from behind,
so the region is now the correct -- and dark -- terrain surface. That darkness is
F2/T12's defect and those two pins are expected to move again, upward, when it
lands.

`bash visual-baseline-check.sh --mutation` -- **PASS**, three mutations red and
three sources restored byte-identical:

- M1 reverts the pvs offset read: popcount 1692 -> 478.
- M2 orients brush as well (mask 05): flip census 8514 -> 23640, eye oracle
  458 of 594 -> 204 of 594.
- M3 inverts the split (mask 13): top-down oracle 3809 of 5381 -> 5297 of 5381,
  `misfaced` 15126 -> 8514.

Both mutations are rules the table scores, so the oracles are shown to be wired
to the rule they claim to measure.

Determinism: two independent captures (forward run and mutation run, separate
processes) hash to
`059200862d98d39cd478d7c93b5cc8408b472a24e051c75969a5e43754a30720`.

## 8. Remaining, named

- **Two (class, side) cells no single rule can orient** -- brush/side-1 and
  displacement/side-0. The residual `misfaced=15126` is the first of them; the
  second costs the eye 5 points. Closing either needs a per-face truth that
  neither the plane lump nor the side bit supplies.
- **Dark world materials** (F2/T12): the POV's near-black and flat blue/white
  regions. Not this task's.
- **4790 degenerate triangles**, **357 static triangles with an edge above 1024**
  (authored, not decoding artifacts), **sky `state=unavailable` on pl_odyssey**:
  unchanged from the previous round.
