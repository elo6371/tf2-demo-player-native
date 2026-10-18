# T11 World Geometry Diagnosis and Repair

Reference recording `tf/demos/autorecord_2026-07-02_13-26-46.dem`, map
`pl_odyssey.bsp`, first-person observer camera, capture tick 2000, 1264x681.
Two fixed views are delivered: the POV frame and the diagnostic top-down
camera at `(-2424, 5566, -80, pitch 90, yaw 0)`.

## 1. Reproduction

Baseline `d3ab18a`. The POV frame reproduced the release-blocker look:
shards, clear-colour holes, disconnected surfaces. The probe census of the
same map (unchanged by this task) reads 99312 triangles / 46912 displacement
/ 4790 degenerate / maxEdge 2560 -- matching the baseline exactly, so the
defect was not a changed parse count.

## 2. The separating instrument: flat colour and depth-only

Two diagnostic modes were added to the product (both off by default, and the
trace states which one drew):

- `--world-flat` -- the world pass outputs one flat colour with no texture,
  lightmap, bump or env sample, so a hole in the frame is a triangle that is
  not there, never a triangle that is dark.
- `--world-depth` -- the same geometry through the same matrices outputs
  `SV_POSITION.z / w` as grey, so depth ordering is visible without shading.

## 3. Isolation matrix (flat, same POV/tick; see frames/)

| capture | vis | cull | what it showed |
| --- | --- | --- | --- |
| `frame-pov-flat-all.png` | off | off | geometry covers the whole frame except one true hole -- every triangle is present and placed |
| `frame-pov-flat.png` | on | on | ~35-40% fragments |
| iso vis-on, cull-off | on | off | large top/right holes -> **the pvs clip hides visible faces** |
| iso vis-off, cull-on | off | on | other holes -> **back-face culling also removes faces** |

Two independent hole-makers. Neither could be found by staring at the
textured frame, because dark materials there also read as "holes".

## 4. Root cause A (fixed): the pvs row table was decoded wrong twice

`dvis_t` is `{ int numclusters; int bitofs[2][numclusters]; }` -- two
contiguous offset tables, compressed rows in [0], plain rows in [1]. The
parser read a single table with a stride of 8 bytes, which lands on
`bitofs[0][2c]`: **every cluster asked another cluster's row**. It then ran a
run-length walk over rows that this map stores **uncompressed** ([1][c] >= 0
for all 2938 clusters, measured). Cluster 1032's visibility answered 1021
clusters -- a number from nowhere.

The fix parses both tables, prefers the uncompressed row when present, and
keeps a correct 0x80-run decoder for compressed maps. Verified by three
instruments agreeing on 1692:

- the product trace `visibleFromCamera=1692`,
- the probe (`world_geometry_probe --eye`) `visClusters=1692`,
- an independent python decode of the vis lump (same lump, same cluster).

Downstream effect: hidden triangles 22968 (was 36298), drawn 76344 (was
63312), and the clip's holes in the flat frame disappear.

The synthetic BSP fixture in `world_material.cpp` was written in the same
interleaved layout, i.e. it agreed with the bug; it now states the real
format (`bitofs[0] = -1`, `bitofs[1]` pointing at plain rows). The
world-material gate's pins for the fixture and for snakewater moved
accordingly (`vis.visibleFromCluster0` 384 -> 1462, visibleFromCamera
611 -> 2150, hidden 29609 -> 10872, drawn 170391 -> 189128).

## 5. Root cause B (fixed): the winding is mixed and the two emission paths disagree

The census (probe): 5416 of 16136 faces agree with the side-aware plane rule
-- the shipped winding is neither uniform nor a single global flip away from
uniform. The flat captures proved the consequence: any single global
orientation leaves one group of faces culled as back faces, and the groups
are the two emission paths -- displacement grids and brush fans ship
opposite conventions.

**Fix**: every emitted triangle (both paths, through the one `pushTriangle`)
is tested against its face's own plane normal and reversed when it points
the wrong way; the displacement grid is oriented along the plane normal and
the brush fan against it. The plane lump is ground truth, so the fan's start
vertex and the grid's (x, y) parametrisation stop deciding which side the
cull mode sees. The churn is a reading: **47252 of 99312 triangles reversed**
(probe `flippedTriangles=47252`) -- the measured split that no global flip
could ever have fixed.

Effect on the fixed POV flat frame: ~35% -> ~95% coverage; the residual
clear-colour gap is the one hole the no-cull frame also has (a genuine gap in
the map data at this camera position, not a culling artifact). The top-down
flat frame now shows a connected surface with building silhouettes across the
frame.

## 5b. The residual winding question, measured (follow-up round)

Section 5 makes each emission class consistent with itself. It does not make a
triangle point the way its face is *seen from*, and the difference is now a
reading instead of an assumption. `BspTriangle` carries the face's plane index
and side bit, so the probe can rebuild every face's own outward direction
(plane normal, flipped by `dface_t.side`) and ask each emitted triangle which way
it points:

```
geometry winding sideAware misfaced=53724 displacedMisfaced=38614 sideOneFaces=5388 of 16136
geometry side-census class=brush        planeSide=0 outward=10931 inward=5361
geometry side-census class=brush        planeSide=1 outward=4670  inward=3851
geometry side-census class=displacement planeSide=0 outward=23364 inward=15164
geometry side-census class=displacement planeSide=1 outward=4256  inward=4128
geometry tree-side outwardOpen=9589 inwardOpen=4002 ambiguous=58134
```

`misfaced` = 53724 of 99312: under the shipped rule more than half the buffer
points away from the side its own plane calls front. That is the size of the
disagreement between the shipped winding and the plane lump, measured, not
argued. (The tree census is the third opinion: it samples 2 units either way
along the outward direction and asks which side is open space. 9589 outward-open
against 4002 inward-open, with 58134 ambiguous because displacements do not
split the BSP tree -- so the tree cannot arbitrate either.)

Eleven candidate rules were then captured on both fixed views, flat-colour, same
tick, with the no-cull frame of each view as the maximum a rule can reach. Full
table and method in `analysis/rule-matrix.txt`; the shape of it:

| rule | POV | top-down |
| --- | --- | --- |
| no cull (maximum) | 0.9228 | 0.6742 |
| shipped: brush +plane, displacement -plane | 0.8953 | 0.4683 |
| all triangles n·F >= 0 | 0.5492 | 0.6181 |
| all triangles n·F <= 0 | 0.8755 | 0.3709 |
| brush n·F >= 0, displacement n·F <= 0 | 0.7052 | 0.4490 |
| brush n·F <= 0, displacement n·F >= 0 | 0.7724 | 0.5551 |
| stored order, no reversal | 0.7414 | 0.6668 |
| global reversal | 0.8974 | 0.3705 |
| brush +plane, displacement stored | 0.7286 | 0.5991 |

Three conclusions, all of them readings:

1. **No plane-derived rule draws both views.** The best POV rule (global
   reversal, 0.8974) leaves the top-down view at 55% of its maximum; the best
   top-down rule (stored order, 0.6668 = 98.9% of maximum) leaves the POV at 80%
   of its maximum. Every candidate buys one view with the other.
2. **The stored order is coherent in the top-down view and the shipped rule is
   coherent in the POV**, and both are measured against the same lump. The two
   views are covered by surface sets whose stored order disagrees, which is what
   `misfaced` counts.
3. **A uniform sign is not a small correction.** Both uniform rules were captured
   as images and both fill the frame with screen-sized wedges
   (`analysis/m2-pov.png`, `analysis/m6-top.png`) -- the exact defect this task
   opened with. That is why the orientation rule is unchanged on this evidence:
   replacing it with a rule that is *philosophically* consistent would have
   regressed the primary view by 30+ points and re-created the release blocker.

The instruments stay in the product: `misfaced` and the per-class census are
printed by `world_geometry_probe` and pinned by the gate as open-defect readings,
so the next attempt starts from a number rather than from a theory.

## 6. Acceptance
`visual-baseline-check.sh` (repo root) pins the probe census (including
`flippedTriangles=47252`, the side-aware `misfaced=53724` with its per-class
split, and the corrected pvs popcount 1692 on two instruments), the product trace
(material-fetch counters, the vis line with `eyeMap`, the cull line, the
debug-view line), and measurable object presence in four world regions -- with
the clear-colour fraction now ~0 in all four. Forward + mutation: **PASS**.

Mutations, all measured red and restored byte-identical:

- M1 reverts the pvs offset read to the stride-8 single-table form: the pvs
  popcount falls back to 478 (another wrong row).
- M2 flips the per-class winding sign: the flip census moves to 41766.
- M3 collapses the class split (both classes take the brush sign): the
  side-aware census moves to 23494 -- proof that the new reading is wired to the
  rule it claims to measure.

Determinism: two identical captures of the reference frame on the acceptance
binary hash to `2486b77acc3655d7cf6cfa34f172c7064a9982d607c54ebcf0447b7ad34fad1f`
(and the flat pair to `9f3ec27f7dc18f02ac6e4a7261f4752bfd04ac5eec7e64316870566e37c53f86`),
which is byte-identical to the hash the pre-follow-up build produced -- the added
instrumentation changed no pixel. The acceptance binary (the one the gate and the
repeat pair ran on) is
`0dbd320f63446da3f84e4fafcb8be21e1f8d854acccf68c98725e32ff1fb3baa`; the
mutation gate's final restore rebuild of the same, byte-identical sources
links to `ff970c531213d24b194f32c906fe528d739c9a726ac3d52fcfd61552de552f2d`
(MSVC embeds a link timestamp; the source restoration, not the binary hash,
is the restored-state proof, and it is recorded per mutation above).

The affected render gate `world-material-check.sh --mutation` was re-run
with its pins moved and passes with its own mutations. `frame-capture-check`
is unaffected by this task's paths and was not re-run.

## 7. Named remaining limitations (not claimed fixed)

- **The winding and the plane lump disagree for 53724 of 99312 triangles**
  (follow-up, section 5b). This is the open geometry defect of this task. It is
  now a pinned reading with a per-class split, and eight candidate rules were
  measured on both fixed views without finding one that draws both: the POV is
  best under the shipped rule / a global reversal (0.8953 / 0.8974 of a 0.9228
  maximum) and the top-down view is best under the stored order (0.6668 of
  0.6742). The next step is not another plane-derived rule -- it is a per-face
  ground truth that does not come from the plane lump. The tree census is the
  first candidate and is too ambiguous to use (58134 of 71725 PVS-kept triangles
  sample the same solidity on both sides, because displacement surfaces are not
  BSP splits).
- **Top-down right side**: restated. Under the shipped rule that region is
  `0.4683` of a `0.6742` no-cull maximum; under the stored order the same camera
  reaches `0.6668`, i.e. culling removes 0.7%. So the region is not a hole in the
  map and not missing geometry -- it is the same surfaces the shipped rule
  removes. It is the visual face of the `misfaced` reading above, not a separate
  defect, and it cannot be closed without closing that one.
- **Dark world materials**: with the geometry now covering the frame, the
  POV still shows large near-black and flat blue/white regions. These are
  material/lightmap failures (the terrain/blend path), i.e. the F2/T12 defect;
  the geometry work does not claim to fix them. Region pins W2/W3/W4 are
  pinned as dark-but-present for exactly this reason.
- **Sky on pl_odyssey** reads `state=unavailable` with a reason word that
  says the caller disabled it; the caller passed no sky flag. Its own task.
- 4790 degenerate triangles (zero area) remain in the buffer; they draw
  nothing and cost rasteriser time.
- 357 static triangles have an edge above 1024 units (largest 2560); these
  are authored large brush faces, not decoding artifacts (displacement max
  edge is 502 units).
