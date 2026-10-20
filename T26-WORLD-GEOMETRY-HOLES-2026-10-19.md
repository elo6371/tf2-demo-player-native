# T26 — World geometry completeness: what the empty pixels are

- **Base:** `36d68b0` on `task/t26-world-holes`, worktree
  `/d/TF2_Native_Worktrees/t26-world-holes`.
- **Frame:** `pl_odyssey`, demo tick 2000, the same camera and tick as T23's
  matrix (1264×681 = 860,784 pixels).
- **Gate:** `world-geometry-hole-check.sh`, mutation coverage in the same file.
- **Outcome: no geometry rule was changed.** The reason is measured below, not
  assumed: every empty pixel in the frame is covered by the map's own sky brush,
  the enclosed population is a set of 14–17 px wide slivers that sit on the
  boundary between adjacent displacement cells, and the one winding change the
  oracle can see is a measured regression.

## 1. The question T23 could not close

T23 counted 38,328 pixels with no world triangle along them even with the
back-face cull and the pvs clip both off, and 2,313 of those in components that
do not touch the frame edge. It could not say whether those pixels are *the map's
sky* or *geometry that is missing*: the sky box is drawn before the world and
covers every pixel the world pass leaves empty, so the finished frame is
sky-coloured either way, and a colour test cannot separate the two.

T26 asks the question of a different instrument — one that never reads a colour:

| instrument | flag | what it may be believed about |
| --- | --- | --- |
| the map's sky, as geometry | `--world-geometry sky --world-flat` | whether a BSP sky face is along the pixel, drawn through the *world* transform |
| the face index as a pixel | `--world-face-id` | which `dface_t` the rasterizer itself put there |
| a CPU point oracle | `--world-hole-trace x,y;…` | which triangle is front-most along a named pixel, in the frame's own matrix |

All three reuse the world pass's transform, because a mask built on its own
bounds is a picture of a different map (`renderer::uploadWorldGeometry` computes
the bounds from `map.triangles` even when it uploads `map.skyTriangles`).

## 2. The account on this tree

`world-geometry-hole-account.py` over an 18-frame matrix; T23's own
`world-geometry-accounting.py` over the same frames reads the same population:

```
HOLE totals   missing=38328  sky=38328  unexplained=0
HOLE split    open=36015  enclosed=2313  enclosedUnexplained=0  components=44
HOLE dark     drawn=683894  dark=185044  limit=16
ACCOUNTING    drawn=653277 backface=167030 visibility=0 both=2149 missing=38328
              other=0 overlay=0
ACCOUNTING    dark drawn_dark=182417 of 653277 (0.2792)
              lightmap_driven=182368 albedo_driven=49 limit=16
```

- **38,328 missing (4.45%)**, of which **36,015** belong to edge-touching
  components (the largest is 35,678 px, `x=430..701 y=0..193`, the opening above
  the map) and **2,313 (0.27%)** to 42 enclosed components, most of them a few
  pixels wide.
- **`unexplained=0`**: every missing pixel has a sky face along it.
- **`backface=167,030` and `visibility=0`** reproduce T23 exactly, so T11's
  winding rule and T17's clip are untouched by this task.
- **`other=0`**: with the frames tick-aligned, every drawn pixel is attributable
  to one of the three switch classes; the earlier `other=3,631` was a symptom of
  the capture defect in §3.
- **Dark is not empty.** The two `dark` lines above count different populations
  on purpose: T26's `drawn` is "the world pass put something here" (683,894),
  T23's excludes the 30,617 pixels its winding test rejects (653,277). On either
  population roughly 27% of drawn pixels are below luminance 16, and T23's line
  splits them: 182,368 of 182,417 become bright with the lightmap off, so they
  are lighting on geometry, not absence of it. None of them is in the missing
  set, which is defined by coverage rather than colour.

## 3. A capture defect that changed the answer, and its fix

The first run of the matrix read **38,143** missing / 2,310 enclosed — 185 pixels
away from T23, with no source change to explain it. The cause was in the capture,
not the renderer: `--capture-tick 2000` arms on the first draw at or after tick
2000, and when a frame is slow the demo advances past it. Five frames of that
matrix were captured at tick **2001/2002**:

```
flat-novis-nocull  2002      depth-base  2001      ref          2001
flat-nocull        2001      world-ids   2002
```

`flat-novis-nocull` defines the missing mask, so the account was comparing a
tick-2002 camera against tick-2000 neighbours. Three changes close it:

1. the product records the tick it captured (`capture: armed requested=…
   frame_tick=…`), so the frame carries its own provenance;
2. `world-geometry-hole-capture.sh` refuses and retries a frame whose tick is
   not the requested one;
3. `world-geometry-hole-check.sh` re-derives the tick for every frame it runs
   itself and retries rather than judging a frame from another camera.

With every frame at tick 2000 the account reproduces T23 to the pixel — which is
the strongest statement available that this tree's world pass is T23's world
pass.

## 4. Why the sky mask cannot be the proof

The sky pass covers **860,783 of 860,784 pixels**. That is not a bug: a sealed
Source map surrounds the play space with sky brushes, so from inside every
direction ends on one. It is a real property of the map, and it settles one
question and destroys another:

- **Settled:** no empty pixel looks *outside* the map. Whatever the frame shows
  at a missing pixel, it is sky by the map's own data, never an undefined void.
- **Destroyed:** coverage cannot separate "the open sky above the map" from "a
  crack between two surfaces". Both are sky-backed. The account therefore prints
  `HOLE maskReach … degenerate=1` and the gate asserts that number as a
  *finding*, not as evidence for the enclosed population.

The separation has to come from elsewhere: shape, and the faces on either side.

The depth pair does not help on this build, and that is reported rather than
used. The debug depth view returns `position.z / position.w` into an 8-bit
target; with this camera's near/far ratio every surface past a few units lands on
255. `sky-depth` is literally one value (`255`) for all 860,783 covered pixels,
so `HOLE depthControl … saturated=1` and the gate asserts the saturation. The
depth statement this task does make is the honest one: the captured depth buffer
at every traced empty pixel is exactly the far plane (`frameDepth=1.0000`),
which is what "no triangle was drawn here" means in the buffer that drew it.

## 5. The enclosed population is slivers, not lost faces

A missing `dface_t` would leave a compact blob. What is there is the opposite:

| rank | px | bbox | w×h | fill | edge |
| --- | --- | --- | --- | --- | --- |
| 1 | 882 | x=690..703, y=462..558 | 14×97 | 0.649 | no |
| 2 | 360 | x=478..494, y=461..520 | 17×60 | 0.353 | no |
| 4 | 282 | x=574..590, y=476..575 | 17×100 | 0.166 | no |
| 5 | 191 | x=920..938, y=517..590 | 19×74 | 0.136 | no |

The gate asserts, for every enclosed component ≥ 200 px, that it is sky-backed,
that its narrow side is ≤ 32 px and its long side ≥ 40 px, and that it fills
less than 70% of its bounding box. Three components qualify; all three pass.

## 6. What the slivers sit between

The world face-id frames name the faces on the other side of each sliver — four
directions, nearest drawn pixel within 64 px. Two frames are needed: the
cull-on frame for the surface the product draws, and the cull-off frame for a
neighbour the product removes (that pixel has only the sky box colour in the
cull-on frame, which decodes to a face index in the millions). A neighbour only
the cull-off frame could name is printed with a `^`.

```
HOLE seam rank=1 point=699,494 left=17268:10 right=^17256:2 up=15310:36 down=17326:64
HOLE seam rank=1 point=696,516 left=17268:7  right=^17256:7 up=15310:58 down=17268:39
HOLE seam rank=2 point=489,481 left=15376:33 right=17262:5    up=15324:23 down=17262:35
HOLE seam rank=2 point=488,501 left=15376:35 right=17262:3    up=15324:43 down=17260:20
HOLE seam rank=4 point=577,501 left=17262:4  right=17266:2    up=17262:21 down=17260:30
HOLE seam rank=4 point=576,520 left=17260:2  right=17266:2    up=17262:36 down=17260:7
```

`world_geometry_probe --face-info` resolves every one of those indices against
the map's own face table:

| face | material | tris | disp | plane | side | bounds |
| --- | --- | --- | --- | --- | --- | --- |
| 17256 | NATURE/BLENDCLIFFTOGRASS_ODYSSEY | 32 | **1** | 1164 | 0 | (-2176.0,3086.3,0.0)..(-1514.2,3274.0,384.0) |
| 17260 | NATURE/BLENDCLIFFTOGRASS_ODYSSEY | 32 | **1** | 2347 | 1 | (-972.7,2068.6,0.0)..(-330.9,2575.5,233.9) |
| 17262 | NATURE/BLENDCLIFFTOGRASS_ODYSSEY | 32 | **1** | 2347 | 1 | (-876.7,2044.0,224.0)..(-409.4,2534.4,384.0) |
| 17266 | NATURE/BLENDCLIFFTOGRASS_ODYSSEY | 32 | **1** | 2347 | 1 | (-983.5,2426.1,9.9)..(-845.8,2632.2,384.0) |
| 17268 | NATURE/BLENDCLIFFTOGRASS_ODYSSEY | 32 | **1** | 2347 | 1 | (-1540.5,2581.1,0.0)..(-793.8,3182.5,384.0) |
| 17326 | NATURE/BLENDGROUNDTOGRASS_ODYSSEY | 32 | **1** | 30 | 0 | (-2176.0,2816.0,-30.0)..(-1024.0,3733.7,90.5) |
| 15310 | GREEK/GREEK_GROUND03B_COBBLE | 2 | 0 | 280 | 0 | (-1960.3,1616.6,384.0)..(-1872.6,1704.3,456.0) |
| 15324 | GREEK/GREEK_GROUND03B_COBBLE | 2 | 0 | 573 | 1 | (-1536.0,1024.0,384.0)..(-1536.0,1088.0,704.0) |
| 15376 | GREEK/GREEK_GROUND03B | 3 | 0 | 573 | 1 | (-1536.0,640.0,128.0)..(-1536.0,960.0,384.0) |

Three facts follow, and each is asserted by the gate:

- **Six of the nine bounding faces are tessellated displacement cells** (32
  triangles each, `displacement=1`, `fallback=0`), and four of them share plane
  2347 — adjacent cells of one displacement surface. The slivers run along the
  boundaries where those cells meet each other and the surrounding brush faces
  (`GREEK/GREEK_GROUND03B`, two of them coplanar on plane 573 with different
  materials).
- **Culling is not the cause.** Two of the traced directions are `^`: the pixel
  there is drawn only once the back-face cull is off, i.e. the sliver's boundary
  is a silhouette of a surface the product removes. But the sliver *itself* is in
  the missing mask, which is taken from the all-off frame — so the empty pixel
  survives the cull being off. Culling explains the *shape* of the boundary, not
  the emptiness.
- **The material is not dark, it is absent.** The sky behind every sliver is
  `TOOLS/TOOLSSKYBOX` at depth 0.75–0.90, not a dark surface.

The point oracle says the same thing at the sliver pixels themselves, and the
rasterizer agrees with it pixel for pixel:

```
point=699,494 drawn=- any=- sky=face=12719 material=TOOLS/TOOLSSKYBOX tri=675 depth=0.8189 frameDepth=1.0000
point=489,481 drawn=- any=- sky=face=10517 material=TOOLS/TOOLSSKYBOX tri=172 depth=0.8844 frameDepth=1.0000
…
HOLE-POINT-SUMMARY points=12 failures=0
```

`drawn=-` (nothing the product draws), `any=-` (nothing even with the cull and
the clip off) and `frameDepth=1.0000` (the depth buffer is at the far plane) are
three independent statements that nothing was there; `idFace == skyFace` for all
12 points is the rasterizer confirming the CPU's projection.

## 7. The one geometry change the oracle can see, refused

T11's rule table on this tree, re-measured by `world_geometry_probe`:

```
rule-table mask=07 eye=0.771   (shipped: brush/s0 keep, brush/s1 keep, disp/s0 keep, disp/s1 flip)
rule-table mask=05 eye=0.325   (brush/s1 flipped)
rule-table mask=13 eye=0.283   (disp/s0 flipped)
```

Flipping `brush/side-1` — the cell that would fill the largest share of the
back-face population — keeps 32.5% of the eye's first hits where the shipped rule
keeps 77.1%. It would trade two thirds of the eye's first hits for at most the
167,030 back-face pixels, and it would not touch the 2,313-pixel sliver
population at all (they are missing with the cull off). T23 measured the same
thing from the other side: `--fallback-orient` gives `flipped=0`.

There is a *plausible* repair that this evidence does not justify: making the
displacement tessellation share edge samples with its neighbour, which would
close cracks of this kind. It is not proposed here, because

- the account cannot tell an authored crack from a tessellation crack from the
  BSP alone — both would leave exactly this sliver;
- the measurable benefit is 2,313 px = 0.27% of the frame;
- a tessellation change moves vertices that T11's winding rule and T17's clip
  both judge, and neither has been characterised against it.

A task that wants to try it now has the exact faces, planes and screen
components to aim at, and a gate that will fail if the sliver population grows.

## 8. What this task does not claim

- **`drawn` is not `correct`.** A pixel with a triangle is not a pixel with the
  right material; 27.9% of the drawn frame is dark, and that is shading.
- **The sky mask is degenerate, and is reported as such.** Coverage proves the
  map is sealed; it does not prove a particular pixel is not a crack. The
  crack/lost-face question is answered by shape and by bounding faces, and that
  answer is weaker than a pixel-exact oracle — which this project does not have.
- **One camera, one tick.** No claim is made about other views; the gate's
  numbers are for tick 2000 only.
- **The depth view is saturated** and is not used as evidence anywhere; the
  far-plane test from the captured depth buffer replaces it.

## 9. Gate and mutation coverage

`world-geometry-hole-check.sh [--mutation] [--reuse]` — forward run:

```
0  source identity (clean native/, HEAD recorded into analysis/identity.txt; the
   hashes are of the working-tree bytes the compiler read, so a host that checks
   out with core.autocrlf=false gets LF and will not reproduce them -- compare
   with `sha256sum` in the checkout, not with `git show <sha>:<path>`)
1  build tf2_demo_native + world_geometry_probe
2  capture matrix, every frame refused unless it belongs to tick 2000
3  stored frame hashes pinned; the binary this run built re-captures `textured`
   and must produce the same bytes
4  probe: sky faces=731 triangles=1842, world bufferTriangles=99312,
   faces total=18052 rendered=17321 (the parse is additive)
5  first-hit oracle: mask 07 dominates the flip candidates
6  the empty-pixel account: 38328 / 38328 / 0, 2313 / 0, mask degenerate,
   depth control saturated, 3 enclosed components all slivers
6b T23's own accounting tool over the same frames: 38328 / 167030 / 0
7  the point oracle at 12 pixels, and `hole-point-verify.py` against the
   rasterizer's face-id frame: 12/12 agree
8  the faces bracketing each sliver, resolved by `--face-info`; at least one
   displacement cell and one sky face must appear
```

Mutation run, `--mutation`. Each mutation removes one rule the verdict stands on,
and the gate has to go red for it — judged by the same equality assertions the
forward run made, not by inspection. Measured on this tree:

| # | mutation | measured red |
| --- | --- | --- |
| M1 | parser keeps the sky face count but drops the polygons | no sky polygon means no upload: exit 14, no draw, no atlas, no frame — the account never runs |
| M2 | the sky buffer is filled from `map.triangles` | `sky=0 unexplained=38328 enclosedUnexplained=2313` |
| M3 | the face-id view returns a constant | `HOLE-POINT-SUMMARY points=12 failures=12` |
| M4 | the sky buffer is drawn through its own bounds | `HOLE-POINT-SUMMARY points=12 failures=2` |
| M5 | the account reads emptiness off the product's picture | `missing=176890` (pin 38328), `enclosed=16667` (pin 2313) |

```
-- M1: the parser keeps the sky count and drops the polygons --
  the product refused to draw (exit 14): gpu: reload-stage stage=models lastError=0x80070057
  PASS M1: no sky polygon means no upload -- exit 14, no draw, no atlas, no frame
-- M2: the sky pass uploads the world triangles --
  PASS M2: the gate goes red: sky=0 unexplained=38328 enclosedUnexplained=2313
-- M3: the face-id view returns a constant --
  PASS M3: HOLE-POINT-SUMMARY points=12 failures=12 ids=1264x681 depth=-
-- M4: the sky mask gets its own transform --
  PASS M4: HOLE-POINT-SUMMARY points=12 failures=2 ids=1264x681 depth=-
-- M5: the account reads empty off the product's picture --
  PASS M5: the gate goes red: missing=176890 sky=176890 enclosed=16667
-- after restore: the pinned frame comes back --
  PASS the restored build reproduces the pinned textured frame byte for byte
== mutation verdict ==
PASS: all five mutations turned the gate red and every source came back byte-identical.
```

Every mutation restores its source byte for byte (`cmp`), and the run ends by
rebuilding from the restored tree and re-capturing `textured`, which must equal
the pinned frame.

Three of these reds are worth stating plainly, because each one is a fact about
the instruments rather than about the code:

- **M1's red is a refusal, not an empty mask.** This product's rule, from T12, is
  that an empty upload is a refusal and not a zero-triangle buffer, so a sky mode
  with no sky polygon cannot produce a frame at all. The first version of this
  gate expected an empty mask, read the refusal as a tick drift, retried it four
  times and then skipped the judgement — a mutation passing for the wrong reason.
  The gate now accepts both reds and, for the refusal, asserts its four facts
  (exit 14, no draw, no atlas, no frame).
- **M4 is caught by 2 points of 12, and by nothing else.** Moving the sky buffer
  to its own bounds still leaves a mask that covers the frame (§4), so the
  coverage account cannot see the change at all; only the face-id decode can,
  and at this camera it moves the id at 2 of the 12 traced pixels. That is a thin
  margin, and it is the honest size of this instrument.
- **The first M5 was not coverage and was replaced.** It dropped the account's
  overlay exclusion; measured on these frames `overlay=0` (tick 2000 draws no
  HUD), so no pinned number moved and the gate refused it. The exclusion stays in
  the tool for frames that carry a HUD, is documented as inert here, and the
  mutation now attacks the definition the task actually stands on: the mask is
  read off the all-switches-off frame, not off the product's picture.

## 10. Files

| file | role |
| --- | --- |
| `native/include/bsp_map.h`, `native/src/bsp_map.cpp` | sky faces emitted into `skyTriangles`; every triangle carries its `dface_t` index |
| `native/include/native_renderer.h`, `native/src/native_renderer.cpp` | `--world-geometry sky`, `--world-face-id` (both upload overloads), the id encoding in one helper |
| `native/src/main.cpp` | `--world-geometry sky`, `--world-face-id`, `--world-hole-trace`, the shared world projection, the capture-tick record |
| `native/tools/world_geometry_probe.cpp` | `--face-info`, the sky census line |
| `world-geometry-hole-capture.sh` | the 18-frame matrix, tick-guarded |
| `world-geometry-hole-account.py` | coverage, components, mask reach, depth control, seam trace |
| `hole-point-verify.py` | CPU oracle vs rasterizer vs depth buffer |
| `world-geometry-hole-check.sh` | the gate and the five mutations |
| `evidence/t26-world-holes/captures/*.bmp` | the frames (traces are ignored; see `analysis/hashes.txt`) |
| `evidence/t26-world-holes/analysis/` | the account, T23's account, probe, oracle, point trace, face info — rewritten by every gate run, with `identity.txt` naming the commit that wrote it |

Verification status on the original task tree: the forward gate and mutation
run pass at `20d974390e1d` — 37 passing assertions, five mutations red, four
sources restored byte-identical each time, and the restored build reproducing
the pinned `textured` frame byte for byte. `analysis/identity.txt` names the
commit that run measured. Post-merge verification on the T25/T27 integration
tree is recorded below.

## 11. Post-merge verification

Pending on integration HEAD `5c8aac6`: rerun the focused T26 and T27 gates, then
the T25 forward visual regression gate. These checks use hidden windows and are
serialized; the full release chain is reserved for T28.
