# T23 — World geometry completeness: the empty pixels, accounted

Date: 2026-10-19
Task: `NEXT-TASKS-2026-10-19.md`, T23 (world geometry completeness)
Branch: `task/t23-world-geometry`
Worktree: `D:/TF2_Native_Worktrees/t23-world-geometry`
Base commit: `9591499` (`p0-entity-protocol`; T17, T18 and T19 integrated)
Product source: **the renderer's decisions are unchanged**. The parser and the
probe gained read-only counters in `native/include/bsp_map.h`,
`native/src/bsp_map.cpp` and `native/tools/world_geometry_probe.cpp`; no winding,
cull, pvs, material or lighting rule was edited. Two new measurement tools are
added: `world-geometry-completeness-capture.sh` and
`world-geometry-accounting.py`.

## 1. What was still open

T11 closed the winding question with a measured decision (mask 07) and two
first-hit oracles, and left two `(class, side)` cells no single rule can orient.
T17 closed the missing pass: the world's flat coverage is 79.8% of the reference
frame and the remaining 20.2% is sky; with the sky pass on, only 0.04% of the
frame is clear colour. Its `--no-world-vis-clip` control differed from the
default frame in 0.15% of bytes, all of them behind other surfaces.

What remained is F1: **terrain/displacement coverage, holes, and suspicious
empty regions**. "Holes" had been a visual word. This round turns it into four
countable mechanisms and asks, for every pixel, which one applies:

| mechanism | what the pixel means |
| --- | --- |
| missing | no triangle exists along the pixel, even with vis and cull off |
| back-face | a nearer triangle exists but the cull removes it (winding) |
| visibility | a nearer triangle exists and faces the eye but the pvs removes it |
| shading | the product draws the nearest triangle and the material reads dark |

## 2. Instruments

### 2.1 The probe's first-hit classification (view-independent)

`world_geometry_probe --ray-eye <x,y,z> <N>` already cast a sphere of rays and
counted first hits. It now also carries the two facts the product uses to decide
whether to draw that hit -- the triangle's cluster and its emitted winding -- and
prints:

```
geometry ray-eye classification drawn=… visRejected=… backRejected=… bothRejected=… noGeometry=…
geometry ray-eye classification class=brush drawn=… rejected=…
geometry ray-eye classification class=displacement drawn=… rejected=…
geometry ray-eye classification fallbackQuad drawn=… rejected=…
geometry ray-eye occlusion firstHitRejected=… drawableBehind=… nothingDrawable=…
geometry ray-eye occlusion drawableBehind class=brush … displacement …
geometry ray-eye noGeometry down=… horizontal=… up=…
geometry ray-eye holes down=… horizontal=… up=…
geometry ray-eye drawn-tone dark=… of … atlas=…
```

`nothingDrawable` is the only first-hit outcome that is a hole in the strict
sense: geometry exists along the ray, the product draws none of it, and there is
no farther drawable surface either. `drawableBehind` is the different-surface
case: the frame still fills, with the surface behind the removed one. Directions are
split into down / horizontal / up (`dz < -0.3`, `|dz| <= 0.3`, `dz > 0.3`)
because "no geometry" straight up is sky, while "no geometry" downwards is a
question about terrain.

The probe is also `--fallback-orient`-capable: it scores what a proposed repair
of the displacement-fallback quads would buy **without changing the product**,
by treating those triangles as if they were oriented to their own face's
side-aware outward direction. That is the measured first-hit comparison the
task asks for before any winding/cull/pvs change.

### 2.2 The same-view capture matrix

`world-geometry-completeness-capture.sh` captures the reference recording at
tick 2000, 1264x681, with the world pass isolated (no ViewModel, water off):

| frame | switches | answers |
| --- | --- | --- |
| `flat-base` | `--world-flat` | is world geometry drawn here? |
| `flat-novis` | `--world-flat --no-world-vis-clip` | …without the pvs |
| `flat-nocull` | `--world-flat --no-world-cull` | …without the cull |
| `flat-novis-nocull` | both off | is there any triangle at all? |
| `depth-base` … `depth-novis-nocull` | the same four, `--world-depth` | which surface is drawn |
| `textured` | default shading | how dark is the drawn surface |
| `lightmap-off` | `--world-lightmap off` | is the darkness the lightmap's |
| `terrain` | `--world-geometry terrain` | is the pixel terrain |
| `none` | `--world-geometry none` | the frame's own clear colour |
| `ref` | default + ViewModel | the picture the pinned gate judges |

Every frame is captured with `--capture-tick 2000`. The capture draw is not
always the last draw the trace logs (a run often logs 1999 -> 2001 and still
writes the tick-2000 frame), so the script accepts the product's own
`capture_written=1` and records the last tick for provenance. The window mode is
a controlled variable rather than an assumption: `textured` was captured both
visible and hidden and the two files are byte-identical
(`ba6b61db…`), so the accounting matrix runs `--hidden-window` and does not
compete with the other tasks for the foreground window. The capture still
serialises itself against other `tf2_demo_native` processes where it can, and
every run that hung was killed and retried rather than kept.

The flat frames answer *fill* and the depth frames answer *identity*: when a
switch removes a nearer surface, the pixel still fills with the surface behind
it, so a coverage test cannot see it -- but its depth changes. Four depth frames
turn "the cull did something here" into a per-pixel fact.

### 2.3 The accounting tool

`world-geometry-accounting.py` joins the frames per pixel:

1. the flat colour is measured from the `flat-base` frame itself (the mode
   colour), so no constant is hardcoded;
2. `present[frame]` is "this pixel is exactly the flat world colour";
3. if the all-off frame has no triangle, the pixel is `missing`;
4. if the product frame fills, its depth is compared with the **cull-off** and
   **vis-off** depths separately: a switch whose depth differs from the
   product's by more than one 8-bit level changed the nearest surface, and the
   switch that did not is not blamed. (The first version compared everything
   against the all-off depth and mislabelled 30,617 cull-caused pixels as
   visibility; the corrected form makes the identity check at the end of §5
   close exactly);
5. if the product frame does not fill while a switch-off frame does, the switch
   that fills it is the cause;
6. a drawn pixel whose textured luminance is below the limit is counted as
   dark, and the `lightmap-off` frame splits that population into
   lightmap-driven and albedo-driven;
7. pixels that differ from the `none` frame's clear colour are the overlay
   (HUD/entities) and are excluded from the world account;
8. connected components of the missing mask are ranked by area, with their
   bounding box, whether they touch the frame edge (open sky) and their fill
   ratio (a compact hole versus a sprawling opening).

Output is a text report named `ACCOUNTING …` plus `accounting-mask.bmp`, whose
colours are: green drawn, near-black drawn-but-dark, blue back-face, magenta
visibility, orange both, red missing/open, white enclosed missing, grey overlay.

## 3. Face-level completeness

The probe now accounts for every face in the world model, not only the ones that
emitted triangles:

```
geometry faces total=18052 rendered=17321 sky=731 skippedNoDraw=0 skippedInvalid=0
geometry displacement coverage candidates=1392 emitted=1382 fallback=10
  (lump=0 polygon=0 index=0 power=0 range=0 corner=0 nonFinite=0 cell=10 limit=0 other=0)
geometry displacement fallback-quads triangles=20
```

- **No face is lost to a parse error.** `skippedInvalid=0`: every face with a
  usable surfedge range became a polygon, and `skippedNoDraw=0`: this map marks
  none of its faces `nodraw`. The 731 faces between `total` and `rendered` are
  its sky faces, which the renderer is supposed to omit; they are counted
  separately and are not holes.
- **Displacement coverage is 1382 of 1392 candidate faces (99.3%).** Ten faces
  carry a dispinfo but produce no grid; `cell=10` says every one of the ten was
  refused because a grid cell was degenerate or larger than 512 units. They are
  emitted as their flat quad (20 triangles) instead of a tessellated surface.
  The ten faces are:

```
index=17034 NATURE/BLENDCLIFFTOGRASS_ODYSSEY
index=17817 NATURE/BLENDGROUNDTOMARBLE01
index=17818 NATURE/BLENDGROUNDTOMARBLE01
index=17819 NATURE/BLENDGROUNDTOMARBLE01
index=17821 NATURE/BLENDGROUNDTOMARBLE01
index=18038 NATURE/BLENDGROUNDTOGRASS_ODYSSEY
index=18039 NATURE/BLENDGROUNDTOGRASS_ODYSSEY
index=18043 NATURE/BLENDGROUNDTOGRASS_ODYSSEY
index=18044 NATURE/BLENDGROUNDTOGRASS_ODYSSEY
index=18045 NATURE/BLENDGROUNDTOGRASS_ODYSSEY
```

This is the concrete content of "terrain/displacement coverage": the terrain is
present, but ten faces are flat where the map asked for a grid. It is not a
hole in the buffer and not a culling artifact; it is a named refusal with a
named fallback.

## 4. The first-hit account from the acceptance eye

`world_geometry_probe --eye -2423.6,5566.0,-1280.0 --ray-eye … 24` casts 1152
directions from the acceptance camera's own eye; 594 meet geometry, 558 do not
(410 of those are upward and are sky by construction).

| first hit | count | share of hits |
| --- | --- | --- |
| drawn (pvs-kept and winding faces the eye) | 457 | 0.769 |
| visibility-rejected (faces the eye, cluster hidden) | 1 | 0.002 |
| back-face-rejected (pvs-kept, winding away) | 129 | 0.217 |
| both rejected | 7 | 0.012 |

The shipped-rule line reads `shippedRuleKeeps=458`: the same count plus the one
direction the pvs removes. The two instruments agree exactly, which is what
makes the split below trustworthy rather than a second opinion.

Class split: brush 423 drawn / 99 rejected; displacement 34 drawn / 38 rejected.
The fallback quads are 0 drawn / 26 rejected.

**Rejection is not the same as a hole.** Of the 137 rejected first hits, 97 have
a farther drawable surface along the same ray (`drawableBehind`: the frame shows
a different surface, not empty space) and 40 have none (`nothingDrawable`). Those
40 are the only strict holes from this eye:

```
geometry ray-eye holes down=26 horizontal=13 up=1
```

The 26 downward holes are the 20 fallback-quad triangles. Every one of them is
`class=brush reason=back material=NATURE/BLENDGROUNDTOMARBLE01 cluster=-1
fallback=1`, at `z=-2176`, outward `(0,0,1)`, `outDotDir ≈ -0.75..-0.98`. The
eye is at `z=-1280`, so it looks down at a surface whose own side bit says it is
seen from below. The 13 horizontal holes are other back-facing surfaces
(`BLENDCLIFFTODIRT_ODYSSEY` 6, `MARBLE_WALL_01B` 3, `FISHINGNET002` 2,
`METALRAIL013C` 1, `GLASSWINDOW005A` 1); the single upward rejection is one
displacement surface seen from underneath. All 40 are `reason=back`; none is a
visibility hole.

**The obvious repair was measured and rejected.** `--fallback-orient` treats
exactly those 20 triangles as if the emission had oriented them to their face's
own side-aware outward direction -- the rule the missing grid would have used.
It changed nothing: the flag was wired (`evaluated=321`) and **zero** of those
evaluations flipped (`flipped=0`); holes stayed at 40 and drawn at 457. The reason is in the data: the quads' own outward direction is
`(0,0,+1)`, so "orient to outward" keeps them facing away from the eye above.
Closing these holes needs a per-face decision the plane lump does not carry
(the same class of missing ground truth T11 named for brush/side-1), not another
winding sign. No product rule was changed on the strength of a guess.

For completeness, the drawn first hits include 51 of 457 (11.2%) whose average
lightmap tone at the hit is below 24: dark surfaces the product *does* draw.
Those belong to the shading account (§8), not to geometry.

## 5. The screen-space account

`world-geometry-accounting.py` over the 13-frame matrix, 1264x681 (860,784
pixels), reports:

| class | pixels | share of frame | what it is |
| --- | ---: | ---: | --- |
| drawn | 653,277 | 0.7589 | neither switch changes the nearest surface |
| back-face | 167,030 | 0.1940 | the cull removes the nearer surface |
| visibility | 0 | 0.0000 | the pvs removes the nearer surface, cull held constant |
| both | 2,149 | 0.0025 | cull-off and vis-off each fill a pixel the product does not |
| missing | 38,328 | 0.0445 | no triangle at all, even with both switches off |
| overlay | 0 | 0.0000 | HUD/entities (the `none` frame is a uniform clear colour) |

The back-face population splits into **136,413 empty** pixels (cull-on draws
nothing; cull-off draws a back-facing triangle) and **30,617 swapped** pixels
(cull-on draws a farther surface; cull-off draws a nearer back-facing one).
The three identities close exactly:

```
base fill        683,894 = drawn 653,277 + back-face swapped 30,617
all-off fill     822,456 = drawn 653,277 + swapped 30,617 + empty 136,413 + both 2,149
missing           38,328 = 860,784 - all-off fill 822,456
```

Fill-level coverage is therefore 79.45% with the shipped switches and 95.55%
with both off. **The 16.1-point gap is the back-face cull, not missing
geometry**; the visibility clip accounts for 0.25 points at most (the `both`
population). Holding the cull constant, the vis clip changes the **depth of no
pixel at all**: the 2,149 pixels it fills in `flat-novis` are pixels the cull-off
frame already fills, and every pixel both `depth-base` and `depth-novis` draw has
the same depth. With the cull off the vis clip changes nothing at all:
`flat-novis-nocull` and `flat-nocull` are byte-identical, and so are
`depth-novis-nocull` and `depth-nocull`.

This also corrects a reading that only a depth control could produce: in the
flat frame alone the 5,411-pixel region inside W3 looks like a hole. With the
cull off it fills with a back-facing surface and its depth changes; it is a
**back-face-empty** region, not absent geometry. The crop
`analysis/w3-flat-vs-nocull.png` (flat-base | flat-nocull | accounting mask)
shows the sky-shaped gap filled completely when the cull is off, and the mask
colours it back-face blue. The
first version of the accounting tool blamed the visibility clip for such pixels
because it compared every difference against the all-off depth; it now compares
against the depth of the switch it is testing, and the visibility column falls
to zero. That mislabel is preserved here because it is the reason the tool takes
four depth frames rather than one.

## 6. Regions W1-W4

The four regions the pinned gate judges (the same rectangles, the same frame;
`dark` is the near-black population inside the region):

| region | pixels | drawn | back-face | visibility | missing | dark |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| W1 `0,0,420,200` | 84,000 | 81,988 (0.976) | 2,012 (0.024) | 0 | 0 | 1,865 (0.022) |
| W2 `0,560,420,121` | 50,820 | 46,510 (0.915) | 4,310 (0.085) | 0 | 0 | 9,554 (0.188) |
| W3 `1100,100,164,200` | 32,800 | 27,389 (0.835) | 5,411 (0.165) | 0 | 0 | 3,095 (0.094) |
| W4 `600,0,64,60` | 3,840 | 1,033 (0.269) | 0 | 0 | 2,807 (0.731) | 0 |

W4 is the only region dominated by missing pixels, and its 2,807 missing pixels
belong to the single top-edge sky component (below). W1/W2/W3 have **zero**
missing pixels: every pixel has a triangle, and their empty-looking parts are
back-face removals. That is the concrete correction to the "W2 is 24%
background" reading: the region has no absent geometry at all; 8.5% of it is a
cull-only pixel by the strict account, and the other near-background pixels are
drawn geometry whose colour is within 24 levels of the wall colour that
`frame-region-stats.py` samples from the frame's top-left corner. That tool's
"background" is a colour, not an emptiness test; the accounting is the emptiness
test.

All four regions' dark pixels are lightmap-driven: 1,865 + 9,554 + 3,095 = 14,514
of 14,514 brighten when `--world-lightmap off` is used. The 41 albedo-dark
pixels in the whole frame are elsewhere (§8).

## 7. Suspicious empty regions

The missing mask has **no significant enclosed hole**. Its components, ranked by
area:

```
rank area  bbox=(x=..,y=..)        edge fill
0    35678 x=430..701  y=0..193    1    0.676   the sky opening, touches the top edge
1      882 x=690..703  y=462..558  0    0.649
2      360 x=478..494  y=461..520  0    0.353
3      337 x=942..961  y=631..680  1    0.337
4      282 x=574..590  y=476..575  0    0.166
5      191 x=920..938  y=517..590  0    0.136
6      180 x=602..616  y=459..481  0    0.522
…      all remaining components are below 100 pixels
```

The largest component is 35,678 pixels and touches the top edge: it is the sky
between the ceiling and the left wall, not a hole in a surface. The crop
`analysis/top-gap-vs-alloff.png` (flat-base | flat-novis-nocull | mask) shows
the gap unchanged when both switches are off -- no triangle appears -- and the
mask marks it missing red. Every enclosed component is under 900 pixels; the 24
listed components contain 2,282 enclosed pixels and the tails add 31, so the
whole enclosed population is 2,313 -- 0.27% of the frame. For comparison, the
back-face population is 167,030 pixels. **On this frame there is no missing-geometry hole of
consequence.** What looked like holes in the flat frame are back-facing
surfaces the cull removed; the terrain fallback cluster of §4 has no pixel in
this POV's missing mask, because its 26 directions are below the camera.

The terrain-only frame (`--world-geometry terrain`) covers 121,494 pixels,
14.1% of the frame. The rest of the world picture is brush geometry, sky, and
the ViewModel-free background. "Terrain coverage" is therefore not the thing
that empties this frame either.

## 8. Dark is not missing

Of the 653,277 pixels the product draws, **182,944 (28.0%) are near-black**
(luminance < 16), 21.25% of the whole frame. The `--world-lightmap off` A/B
splits that population:

```
drawn_dark=182,944  lightmap_driven=182,903  albedo_driven=41
```

99.98% of the dark pixels brighten by at least 16 luminance levels when the
lightmap term is removed. The darkness is the baked lightmap, not a dark albedo
and not an untextured fallback. This is the same conclusion T17 reached from
the atlas tone histograms (`darkTriangles=26844`, unwritten texels 0, LDR and
HDR pages agreeing); T23 adds the screen-space population and the A/B.

The important consequence for this task is a classification, not a fix:
**dark is not missing.** The black areas in the middle and lower left of the
reference frame are drawn surfaces with a dark lightmap; they are not holes,
and no geometry change will fill them. They belong to the material/lighting
track (F2/T12, T24).

## 9. What is not verified, and what is refused

- **Depth is 8-bit.** Two different surfaces whose depth quantises to the same
  level are indistinguishable to the pixel account; the probe's first-hit
  oracle is the independent instrument for that case, and its ratios agree with
  the pixel account (21.7% of first hits rejected vs 19.4% of pixels
  back-face-affected).
- **Sky intent is not known.** The component analysis separates edge-touching
  components (open view) from enclosed ones (holes). It cannot know whether the
  map author intended an opening; the report therefore names the largest
  component as sky by its position and size, not by a material test.
- **The 10 fallback faces are below the POV.** Their 26 hole directions are
  outside the 90-degree capture. Whether they should be visible from above is a
  map-intent question: their own side bit says the front is up, so both the
  missing grid and the flat quad would be back-facing from above. The measured
  "orient to outward" repair gains 0 and is not applied.
- **Window mode is a controlled variable.** The four flat frames, the four depth
  frames and `lightmap-off` were captured with `--hidden-window`; `textured`,
  `none`, `terrain` and `ref` were captured visible. One configuration
  (`textured`) was captured both ways and the files are byte-identical
  (`ba6b61db52851023cbe833b86c3e20174945b1636d45ae71af8363d8a96dfe20`), so the
  mode does not change a pixel. The `ref` frame is the visible-window frame the
  pinned gate judges.
- **No product rule changed, no pin moved.** The winding/cull/pvs decision is
  T11's mask 07 and this round found no measurement that justifies changing it.
  The three visual-baseline pins this tree inherits are stale for a different
  reason (T19's material coverage: tiled 85, fromPakfile 85, vtfTooLarge 70,
  blend 10/10/10/85920) and are T16/T20's to reconcile, not T23's.
- **No new gate is added to the routine chain.** T20 owns the focused
  post-integration regression, and this branch is an investigation with no
  product-rule change to gate. The two tools are deterministic and exit non-zero
  on a malformed input; the numbers above are what a T23 gate would pin if the
  integration line wants one after T16/T20.
- **The frame still looks like shattered slabs.** That is the material and
  triangulation defect T11 named for T12; T23 measures geometry completeness and
  does not claim the picture is correct. `drawn` means "a triangle, not a hole",
  not "a correct material".

## 10. Reproduction

Base: `9591499` (`p0-entity-protocol`). Worktree
`D:/TF2_Native_Worktrees/t23-world-geometry`, branch `task/t23-world-geometry`.

```
bash build-target.sh tf2_demo_native world_geometry_probe          # rc=0
native/build-nmake/world_geometry_probe.exe \
  --tf-root "D:/SteamLibrary/steamapps/common/Team Fortress 2/tf" \
  --map maps/pl_odyssey.bsp --eye "-2423.6,5566.0,-1280.0" \
  --ray-down 96 --ray-eye "-2423.6,5566.0,-1280.0" 24              # rc=0
… same with --fallback-orient                                      # rc=0
bash world-geometry-completeness-capture.sh                        # rc=0, 13 frames
python world-geometry-accounting.py \
  --dir evidence/t23-world-geometry/captures \
  --out evidence/t23-world-geometry/analysis                       # rc=0
```

Executable identities (after the final build):

```
42077a85412d7a4279629d5eb9ea49381477f13e64c69021b1474e2d61d559b0  tf2_demo_native.exe
6059adc8fbeb49fd847afa96433c89f36f2212a92735f7783458404c02c5bd69  world_geometry_probe.exe
```

The matrix was captured with the product build from the base source
(`595d0a40…`). After the instrumentation was added, the final source was rebuilt
(`42077a85…`) and `flat-base` was captured again: it hashes
`d4d31160c52a1c77d5b7c04c2080d784c4ee5f8e82236f861c7274c22be8d38b`, byte-identical
to the matrix frame. The counters changed the executable, not the picture, and
every frame in the matrix is a picture of the current source tree.

Frame hashes (all 1264x681, `--capture-tick 2000`; `ref`/`textured`/`none`/
`terrain` visible-window, the switch matrix and `lightmap-off` hidden):

```
16a5c0b8…  ref                  (visible window, ViewModel companion)
ba6b61db…  textured             (identical to a visible-window capture of the same config)
c04021f7…  none
efc74f82…  terrain
da3592a4…  lightmap-off
d4d31160…  flat-base
fdecffff…  flat-nocull           ┐ byte-identical: with the cull off, the vis clip
fdecffff…  flat-novis-nocull     ┘ changes no fill pixel on this frame
84fcb18f…  flat-novis
d4c2298c…  depth-base
e4dc03af…  depth-nocull          ┐ byte-identical, same reason
e4dc03af…  depth-novis-nocull    ┘
5a0235ea…  depth-novis
```

Analysis outputs:

```
569720f9…  probe-current.txt
22e09f4f…  probe-fallback-orient.txt
46715830…  accounting.txt
5a32e69c…  accounting-mask.bmp
```

Worktree state: the three product/tool source files are modified; the two new
tools and `evidence/t23-world-geometry/` are untracked. No file outside the T23
scope was touched. The three diagnostic files under
`evidence/visual-baseline/` in the main worktree were not read into or written
from this worktree.

## 11. Verdict

F1 on the current tree is not one defect, and it is not mostly missing
geometry. The frame's empty pixels divide as follows:

| mechanism | share of the 1264x681 frame | evidence |
| --- | ---: | --- |
| back-face rejection | **19.40%** (16.1 points empty, 3.6 swapped) | four flat frames + four depth frames; 167,030 px |
| missing geometry | 4.45% (all but 0.27% is the top edge sky opening) | all-off frame; largest enclosed component 882 px |
| visibility rejection | 0.00% standalone, 0.25% at most (both) | `flat-novis` vs `flat-base` = 2,149 px; with cull off, zero |
| material darkness | **28.0% of drawn pixels** (21.25% of the frame) | `--world-lightmap off`: 182,903 of 182,944 brighten |

At face level the world is complete: 18,052 faces, 17,321 rendered, 731 sky, 0
invalid, 0 nodraw. At displacement level 1,382 of 1,392 candidates tessellate;
10 faces fall back to flat quads (20 triangles) because every grid cell is
degenerate or oversized. Those 20 triangles are the only first-hit holes the
probe can attribute to a named cause: 26 of the eye's 40 nothing-drawable
directions, all of them below the camera and outside the POV.

The measured first-hit account agrees with the pixel account: 594 eye
directions, 457 drawn, 1 visibility-rejected, 129 back-face-rejected, 7 both;
97 of the 137 rejections show a farther surface (the frame still fills, with a
different surface) and 40 show none. The probe's 21.7% rejection and the screen's 19.4% back-face
population are the same defect seen through two instruments.

**No winding, culling or PVS rule was changed.** The one repair that looked
mechanical -- orient the 10 fallback quads to their own face's outward direction
-- was scored first and bought exactly nothing (`flipped=0`, holes 40 -> 40,
drawn 457 -> 457), because the side bit itself points up while the eye is
above. Closing the residual needs a per-face ground truth the plane lump does
not carry, which is the same conclusion T11 reached for brush/side-1; guessing
a sign would re-create a release blocker. The product contribution of T23 is the
instrument that can now say which of the four mechanisms owns any given pixel,
and the account above.

What this means for the release claim: the POV's emptiness is dominated by
back-face removal and dark materials, not by absent terrain or by the PVS. A
"geometry completeness" claim that fills the frame by disabling the cull would
be trading a measured 19.4% for a different set of wrong surfaces; a claim that
the frame is correct still needs T12/T24 for the 28% dark population. Both are
explicitly outside this task.
