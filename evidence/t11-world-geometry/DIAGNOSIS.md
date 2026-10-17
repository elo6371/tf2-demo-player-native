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

## 6. Acceptance

`visual-baseline-check.sh` (repo root) pins the probe census (including
`flippedTriangles=47252` and the corrected pvs popcount 1692 on two
instruments), the product trace (material-fetch counters, the vis line with
`eyeMap`, the cull line, the debug-view line), and measurable object presence
in four world regions -- with the clear-colour fraction now ~0 in all four.
Forward + mutation: **PASS**.

Mutations, both measured red and restored byte-identical:

- M1 reverts the pvs offset read to the stride-8 single-table form: the pvs
  popcount falls back to 478 (another wrong row).
- M2 flips the per-class winding sign: the flip census moves to 41766.

Determinism: two identical captures of the reference frame on the acceptance
binary hash to `2486b77acc3655d7cf6cfa34f172c7064a9982d607c54ebcf0447b7ad34fad1f`.
The acceptance binary (the one the gate and the repeat pair ran on) is
`0dbd320f63446da3f84e4fafcb8be21e1f8d854acccf68c98725e32ff1fb3baa`; the
mutation gate's final restore rebuild of the same, byte-identical sources
links to `13c9c35e0e638e7a59b7bf8f0b5cfa10eeacd63a8c4021023643eb3e281d780f`
(MSVC embeds a link timestamp; the source restoration, not the binary hash,
is the restored-state proof, and it is recorded per mutation above).

The affected render gate `world-material-check.sh --mutation` was re-run
with its pins moved and passes with its own mutations. `frame-capture-check`
is unaffected by this task's paths and was not re-run.

## 7. Named remaining limitations (not claimed fixed)

- **Dark world materials**: with the geometry now covering the frame, the
  POV still shows large near-black and flat blue/white regions. These are
  material/lightmap failures (the terrain/blend path), i.e. the F2/T12 defect;
  the geometry work does not claim to fix them. Region pins W2/W3/W4 are
  pinned as dark-but-present for exactly this reason.
- **Top-down right side still culls**: the top-down flat capture shows a
  remaining cull region on the right of the map. The per-class orientation
  fixed the whole POV and most of the top-down, but at least one more group
  (or data region) does not follow the two-class rule. `flippedTriangles`
  makes the next step measurable; the region is named in
  `frames/frame-cls-top-flat.png`.
- **Sky on pl_odyssey** reads `state=unavailable` with a reason word that
  says the caller disabled it; the caller passed no sky flag. Its own task.
- 4790 degenerate triangles (zero area) remain in the buffer; they draw
  nothing and cost rasteriser time.
- 357 static triangles have an edge above 1024 units (largest 2560); these
  are authored large brush faces, not decoding artifacts (displacement max
  edge is 502 units).
