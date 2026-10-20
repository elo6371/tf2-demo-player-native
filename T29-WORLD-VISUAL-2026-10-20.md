# T29 — World visual quality: the near clip plane was written in the wrong unit

- **Base:** `36303c2` on `task/t29-world-visual`, worktree
  `D:/TF2_Native_Worktrees/t29-world-visual`.
- **Inputs:** map `D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/maps/pl_odyssey.bsp`,
  demo `D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/demos/autorecord_2026-07-02_13-26-46.dem`,
  resource root `D:/SteamLibrary/steamapps/common/Team Fortress 2/tf`.
- **Frame:** tick 2000, 1264x681, `--hidden-window --audio-disabled`, the same
  camera as T25/T26/T27.
- **Gate:** `world-near-clip-check.sh` (new: focused forward gate + a real
  mutation), `visual-baseline-check.sh` (region pins re-measured, clip reading
  added), `trace-near-points.py` (the pixel reading both use).

## 1. The frame before the fix, and what was not wrong with it

Region readings of the pinned frame before the fix (`frame-region-stats.py`,
the gate's own tool; it reads BMP rows in file order, so its `y` is measured
from file row 0 — the numbers are comparable with every earlier pin in
`visual-baseline-check.sh`):

| region | mean | std | nearBlack | meanGradient |
| --- | --- | --- | --- | --- |
| W1 x=0,y=0 420x200 | 75.3 | 56.65 | 0.1905 | 12.76 |
| W2 x=0,y=560 420x121 | 56.5 | 17.39 | 0.0237 | 3.92 |
| W3 x=1100,y=100 164x200 | 91.9 | 71.12 | 0.3900 | 0.97 |
| W4 x=600,y=0 64x60 | 42.9 | 55.55 | 0.6784 | 4.18 |

Whole frame: **183,571 px (21.3%) drawn but black** (luminance < 16).

The material chain is **not** the defect. T21's per-pixel trace resolves
point -> triangle -> material -> VMT store -> VTF store -> tile rectangle ->
texel -> lightmap uv -> light texel -> expected pixel. On the black band
(`point=720,390`): `material=GREEK/GREEK_GROUND03B_COBBLE`,
`vmtStore=pakfile`, `vtfStore=pakfile`, tile inside its rectangle,
`texel=177,169,154`, `albedo=177,169,154`, **`light=0,0,0`** ->
`expected=0,0,0`. The albedo is bright; the lightmap sample is zero.

That zero is not a uv error either. Reading the map's lighting lumps directly
(the code's own RGBExp32 decode, replicated offline; the offline packer
reproduces the product's `atlas=4096x2251` exactly):

- 898 of 18,052 faces have a pure-black lightmap rectangle;
- **140,960 of the 183,571 black pixels (76.8%) are back faces** of their own
  `dface_t` plane (the eye is on the plane's non-front side);
- the rectangles are reached correctly (no overlapping or unwritten texels),
  so the data is black because those surfaces' *lit* side faces elsewhere.

Those are the backs of floors and walls that the frame was seeing *through*.
The question became: why is the frame looking at the far side of the map?

## 2. Root cause: a clip plane written in the normalized box's unit

`uploadWorldGeometry` scales the map into a normalized box:

    x' = (x - centerX) * worldHorizontalScale_
    z' = (z - minZ) / spanZ * 0.8 + 0.1

with `worldHorizontalScale_ = 1.8 / max(spanX, spanY)` over the world
triangles. On `pl_odyssey` that is `1.8 / 22000 = 8.182e-05` (measured
`horizontalScale=0.00008194` in the trace), i.e. **one box unit is 12,222 map
units**. The world projection was built with

    XMMatrixPerspectiveFovLH(fov, aspect, 0.05f, 100.0f)

so the near plane was **0.05 box units = 611 map units**. Everything within 611
map units of the camera was clipped out of every frame. Three independent
readings say so:

1. **A CPU ray cast from the demo eye** over all 57,006 BSP triangles (no product
   code): 4,000 directions, 3,841 hits; the nearest *front-facing* surface is at
   a **median of 183 map units**, and 906 directions hit one within 64 units.
   The camera is inside a room surrounded by close geometry.
2. **The product's own trace** converts per-pixel depth back to map units
   (`trace-near-points.py`, using the near plane and scale the draw published):
   **0 of the 28 named points were nearer than 611 map units** before the fix
   (nearest 617.6). The drawn geometry began *at the plane* — the fingerprint of
   a clip.
3. **The black population follows from it**: with the room clipped out, the
   frame shows only the far side of the map, and much of that far side is seen
   from behind. Back faces are lit for their front side, and the map's lighting
   lumps carry zero there.

The pre-fix frame is a dollhouse: 903 faces drawn, every one of them further
than ~630 map units from the eye.

## 3. The fix

One plane, stated in the unit it means:

```cpp
const float kWorldNearMapUnits = 4.0f;
const float worldNearPlane = (std::isfinite(worldHorizontalScale_) && worldHorizontalScale_ > 0.0f)
  ? std::max(worldHorizontalScale_ * kWorldNearMapUnits, 1.0e-6f) : 0.05f;
const float worldFarPlane = 100.0f;   // box units; also contains the sky box (half extent 40)
```

The renderer publishes the pair it used and the product prints both units:

    gpu: world clip near=0.00032776 far=100.000000 nearMapUnits=4.00
      farMapUnits=1220442 horizontalScale=0.00008194 state=published

The near plane is 4 map units — the usual Source first-person distance — which
puts the camera's own room inside the frame. The far plane is untouched, so the
sky box pass (drawn with the same projection, half extent 40 box units) is
unaffected.

## 4. What changed, measured

Same camera, same tick, hidden window, `--viewmodel ... --viewmodel-render companion`:

| reading | before | after |
| --- | --- | --- |
| named points nearer than the old 611-unit clip | **0 of 24** | **18 of 25** (nearest 94.3 units) |
| nearest traced point | 617.6 map units | 94.3 map units |
| W1 mean / std | 75.3 / 56.65 | **18.8 / 21.90** |
| W2 mean / std | 56.5 / 17.39 | **57.0 / 24.19** |
| W3 mean / std | 91.9 / 71.12 | **133.9 / 31.03** |
| W4 mean / std | 42.9 / 55.55 | **42.9 / 55.55** (control, unchanged) |
| W4 nearBlack / gradient | 0.6784 / 4.18 | **0.6784 / 4.18** (control, unchanged) |
| frame dark pixels (lum<16) | 183,571 (21.3%) | 208,630 (24.2%) |

Three things this table says plainly:

- **The camera's room is in the picture now.** 18 of the 28 named pixels are
  closer than the old clip, and the nearest is 94 map units away. This is a
  depth reading at named pixels, not a brightness.
- **W4 is the control and it did not move.** The patch of sky between the slabs
  that W4 has measured since T10 reads exactly the same numbers before and
  after. The fix moved the near half of the frame; it did not repaint the sky.
- **The black population did not fall — it moved.** The frame now shows the room
  instead of the far side of the map, and the room's left wall is in the map's
  own shadow, so 24.2% is still black. This report does *not* claim the frame got
  brighter, and the gate does not assert it did. What was fixed is *which*
  surfaces are on screen; the surfaces now on screen carry the map's own
  (dark) lighting. The visible result is a readable scene — a courtyard with a
  cart on grass, a wooden fence, stone buildings and sky — see
  `evidence/t29-world-visual/ref-before.png` and `ref-after.png`.

## 5. The control that makes this a projection fix and not a material change

Every reading the world pass prints about *what* it draws is **identical**
before and after the fix:

    material-fetch distinct=108 tiled=85 capped=0 vmtMissing=3 vmtNoBase=20
                   vtfMissing=0 vtfTooLarge=70 decodeEmpty=0 fromPakfile=85
    lightmap       status=active intensity=0.898 atlas=1 vertices=297936
                   lightmapped=296490
    sky-upload     state=active vertices=36 triangles=12 faces=6
    blend          enabled=1 textures=85 layers=10 accepted=85 tiles=10
                   weightedVertices=85920 distinctWeights=34
    vis            cameraCluster=1032 visibleFromCamera=1692 hiddenTriangles=22968
                   drawnTriangles=76344 clipApplied=1
                   eyeMap=(-2423.6,5566.0,-1280.0)

No material resolution, lightmap texel, sky face, blend tile or pvs decision
moved. Winding, PVS and culling were **not** changed.

## 6. The twelve traced pixels

`evidence/t29-world-visual/chain-after.txt` and `chain-before.txt` hold the full
28-point chain for both builds (point -> triangle -> material -> VMT store ->
VTF store -> tile -> uv -> texel -> lightmap uv -> light -> albedo -> expected
pixel -> depth -> frame depth). Representative rows from the fixed build:

| point | material | vmt/vtf store | tile | texel | light | luv | expected | depth |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 90,70 | GREEK/GREEK_GROUND03B | pakfile/pakfile | 0/11 | 205,190,171 | 87,110,142 | 0.7145,0.1321 | 70,82,95 | 0.9898 |
| 300,70 | GREEK/GREEK_WALL04_WHITE | pakfile/pakfile | 0/19 | 190,173,154 | 70,64,64 | 0.6011,0.6961 | 52,43,38 | 0.9959 |
| 720,70 | WOOD/WALL007A | vpk-volume/vpk-volume | 0/10 | 126,108,90 | 206,197,202 | 0.4564,0.1278 | 102,84,71 | 0.9953 |
| 930,70 | GREEK/GREEK_GROUND03B_COBBLE | pakfile/pakfile | 0/20 | 200,187,168 | 53,46,39 | 0.6527,0.7303 | 41,34,25 | 0.9893 |
| 1140,70 | GREEK/GREEK_GROUND03B | pakfile/pakfile | 0/11 | 189,171,151 | 167,141,111 | 0.7798,0.7249 | 124,94,66 | 0.9895 |
| 720,230 | SWAMP/PROPS/FISHINGNET002 | vpk-volume/vpk-volume | 0/13 | 57,52,49 | 255,248,229 | 0.5236,0.7568 | 57,50,44 | 0.9933 |
| 930,230 | WOOD/WALL007A | vpk-volume/vpk-volume | 0/10 | 129,112,96 | 255,255,252 | 0.5605,0.0445 | 129,112,95 | 0.9854 |
| 510,390 | NATURE/BLENDGROUNDTOGRASS_ODYSSEY | pakfile/pakfile | 0/2 | 185,165,145 | 252,255,255 | 0.2757,0.9047 | 147,135,106 | 0.9798 |
| 720,390 | WOOD/WOOD_FLOOR004 | pakfile/pakfile | 0/15 | 111,96,79 | 198,186,174 | 0.6811,0.2826 | 86,70,54 | 0.9740 |
| 930,390 | NATURE/BLENDGROUNDTOGRASS_ODYSSEY | pakfile/pakfile | 0/2 | 166,147,126 | 255,255,255 | 0.2908,0.8990 | 82,79,40 | 0.9772 |
| 1140,390 | NATURE/BLENDGROUNDTOGRASS_ODYSSEY | pakfile/pakfile | 0/2 | 182,164,145 | 255,255,255 | 0.2891,0.8970 | 102,96,59 | 0.9792 |
| 510,550 | NATURE/BLENDCLIFFTOGRASS_ODYSSEY | pakfile/pakfile | 0/0 | 170,161,148 | 144,177,227 | 0.5914,0.8245 | 96,112,132 | 0.9982 |
| 200,620 | GREEK/GREEK_GROUND03B_COBBLE | pakfile/pakfile | 0/20 | 172,161,150 | 0,0,0 | 0.8533,0.7052 | 0,0,0 | 0.9904 |
| 1180,200 | WOOD/WALL007A | vpk-volume/vpk-volume | 0/10 | 131,118,102 | 255,255,255 | 0.4306,0.0298 | 131,118,102 | 0.9745 |
| 300,390 | GREEK/GREEK_GROUND03B | pakfile/pakfile | 0/11 | 199,187,169 | 0,0,0 | 0.8608,0.7045 | 0,0,0 | 0.9925 |

The last two rows are the remaining honest defect: some near surfaces still
sample a zero lightmap (they are lit for their other side). The clip fix put
them on screen; making their lighting right is a lightmap-composition question,
not a projection question, and is listed as NOT VERIFIED below.

## 4b. The residual black of the fixed frame, attributed block by block

The black areas that remain must be named, not papered over. Three frames from
the same build and camera classify every black pixel: the textured frame,
`--world-flat` (a pixel that is not the flat colour has no world triangle) and
`--world-lightmap off` (the sky pass is unaffected by the lightmap; albedo is).
Measured over the 208,630 black pixels:

| bucket | px | share | meaning |
| --- | --- | --- | --- |
| A sky/background | 17 | 0.008% | no world triangle, identical with the lightmap off |
| B lightmap-driven | 205,911 | **98.7%** | a world triangle is drawn, bright with the lightmap off |
| C albedo/shader dark | 2,702 | 1.3% | a world triangle is drawn, still dark with the lightmap off |

The residual black is **not** sky, **not** a void, and **not** missing
geometry: it is drawn world geometry whose lightmap sample is (near) zero. By
cell (visual 8x6 grid, y=0 at the top) it sits in the lower-right quadrant
(cells 50-98% black at y=454..680), the bottom band (88% and 98% in two cells)
and the left edge (35-54% at y=113..453); the overlay is
`evidence/t29-world-visual/residual-black-buckets.png` and the numbers
`residual-black.txt`.

What share of bucket B is the **back** of a face (76.8% in the pre-fix frame) is
**NOT VERIFIED** for this frame: it needs a `--world-face-id` capture on the
fixed build and the same offline plane test section 1 used. Cheapest next step,
one capture:

    tf2_demo_native --tf-root <tf> --demo <demo> --hidden-window --capture-tick 2000 \
      --capture-frame after-ids.bmp --world-face-id --audio-disabled

## 5b. Limitations

- **Depth precision.** The near plane is 3.2776e-04 box units and the far plane
  100 box units, a 1:305,101 ratio on a D24_UNORM_S8 buffer. Depth resolution at
  view distance z is `z^2 * 2^-24 / near`: about **0.02 map units at 1,220**,
  0.56 at 6,100, 2.2 at 12,200 and **7.2 at 22,000** map units (the map's far
  corner). The old plane was "precise" only in the region it refused to draw.
  **Z-fighting is NOT VERIFIED**: the reference view shows none, but no
  coplanar/separation test was run. Cheapest verification: capture the same view
  at near = 4 and near = 1 map units and diff — pixels that move are
  depth-order-sensitive; or probe the map's known coplanar pair
  (GREEK/GREEK_GROUND03B, plane 573, two faces).
- **A far plane larger than the map.** 100 box units is ~55 map spans and exists
  because the sky box pass (half extent 40 box units) shares this projection.
  It is untouched by this fix; a smaller far plane would improve precision but
  would need its own sky-box change and its own pins.

## 7. Verification

All builds, launches, captures and mutations ran under
`bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run t29 <cmd>`; every product
launch is force-killable (`timeout -k 5`) because two launches deadlocked during
load on this host.

| check | result |
| --- | --- |
| `visual-baseline-check.sh` (forward, re-pinned regions + clip reading) | **PASS** — 2026-10-20 13:10 on this tree, log `evidence/t29-world-visual/visual-baseline-forward.log`. All probe pins, all material/lightmap/sky/blend/vis pins, the four region pins, the three exact W4 control assertions and the new `gpu: world clip` assertions held. |
| `world-near-clip-check.sh` (forward) | **NOT VERIFIED as a verdict (one instrument bug, fixed; re-run queued).** The first run held every product reading — `nearMapUnits=4.00`, near box = nearMap x scale, `far=100.000000`, `state=published`, W1/W2/W3 in pin, W4 exact (42.9 / 55.55 / 0.6784), 19 control readings exact — and failed exactly one assertion: the near-point count read `0 of 0` because that gate's capture did not yet pass `--world-face-trace`. The flag is in the gate now and the run is queued. The failed log is kept as `near-clip-forward-attempt1.log`; it is not rewritten as a pass. |
| `world-near-clip-check.sh --mutation` | **NOT VERIFIED (queued).** Expected RED, from the pre-fix measurements in this report: `nearMapUnits=610.22`, W1 meanLum ~75.3 (outside [16.8,20.8]), near-point count 0 of 28, and the restored build's frame byte-identical. The source is restored with `cmp` and rebuilt before the frame comparison. |
| `world-material-face-check.sh` (forward, the relevant material gate) | **NOT VERIFIED (queued in the same run)** — the chain it resolves is unchanged (section 5), but its own albedo/textured frame comparisons have not been run on the fixed tree. |
| `visual-baseline-check.sh --mutation` | NOT RUN. The five pre-existing mutations are unchanged by this fix; M5's W1 bound now follows the re-pinned 18.8. This is the cheapest next gate run after the focused mutation. |
| restored build reproduces the frame byte for byte | part of the focused mutation run; **NOT VERIFIED** until it runs. |

The mutation is a hard delivery requirement, not a nicety: a forward pass whose
own source-revert does not reproduce the old readings would be a threshold
change wearing a fix's clothes. The gate keeps that distinction in its verdict
line.

## 8. Superseded readings (kept, not rewritten)

| reading | superseded value | new value | why it moved |
| --- | --- | --- | --- |
| W1 (x=0,y=0) mean/std | 75.3 / 56.65 | 18.8 / 21.90 | the room's near wall, previously clipped, is in this region |
| W2 (x=0,y=560) mean/std | 56.5 / 17.39 | 57.0 / 24.19 | near geometry replaces far back faces |
| W3 (x=1100,y=100) mean/std | 91.9 / 71.12 | 133.9 / 31.03 | the far wall fills it; the 39% near-black is gone |
| W4 (x=600,y=0) mean/std | 42.9 / 55.55 | 42.9 / 55.55 | **control: unchanged** |
| frame dark pixels | 183,571 | 208,630 | the room's shadowed wall is now on screen |
| `gpu: world clip nearMapUnits` | 610.22 | 4.00 | the fix |

No pin was widened to make a failure pass: W1 moved *down* by 56 points,
W3 moved *up* by 42, and the one region the clip could not change (W4) is
asserted exactly.

## 9. NOT VERIFIED, and the cheapest next step for each

- **T26's sky-account pins are stale by construction.** T26's gate counts
  pixels with no world triangle from an all-switches-off matrix; this fix
  changes those frames (near geometry now covers part of them). The pins were
  **not touched** (as required). Cheapest next verification: run
  `bash world-geometry-hole-check.sh --mutation` on this branch, then re-pin
  its totals from the new frames. The account tool and its 12-point oracle are
  unchanged; only the frame population moves. **This is a Lead decision** — it
  also touches T23's accounting pins. The gate was **not run** on the fixed tree
  by this task; the integration owner should run it once on the merge tree. The
  expected shape: the pin `missing=38328 / sky=38328 / unexplained=0` must move
  because the all-switches-off frame now draws near geometry that used to be
  clipped; the sky-face and geometry/PVS counts are frame-independent and should
  not. Re-pin only the frame-derived totals and say so in the commit.
- **The near surfaces' zero lightmaps.** 5 of the 25 traced points in the fixed
  frame still read `light=0,0,0`: those surfaces are lit for the side the map
  authored as front. Whether Source would draw them at all (back-face cull) is
  the T11/T26 winding question, and this task did not change winding, PVS or
  culling. Cheapest next verification: the T26 eye first-hit oracle with and
  without the mesh's back faces, at this camera, plus a same-view pixel count.
- **"Correct" lighting of the newly visible room.** The room had never been in
  any frame before, so no earlier pin describes it. What is measured is that the
  material chain resolves at those pixels (section 6) and that the frame is not
  a counter artefact (W4 exact, mutation returns the old readings).
- **One camera, one tick.** All numbers are tick 2000 on this demo.
- **The headless-host deadlock.** Two of the product launches in this session
  deadlocked during load (all threads waiting, no trace progress) and were
  killed; the successful runs were force-killed-capable. Every capture in the
  gates used `timeout -k 5 150`.

## 10. New task-sized blocker (T34 candidate): lightmap-driven black

The residual black of the fixed frame is named, not hidden in a limitations
list. It is a **new blocker** with its own task:

- **Reading.** 205,911 px of the fixed reference frame (98.7% of all its black
  pixels) are *drawn world geometry* that is bright with the lightmap off and
  black with it on. 2,702 px (1.3%) are dark even without the lightmap, and 17
  px (0.008%) are sky.
- **Cause as measured.** The surfaces' own lightmap rectangles read zero in
  both lighting pages (LDR lump 8 and HDR lump 53; 898 of 18,052 faces are
  pure-black-rect faces), the uv reaches the rectangle correctly (the offline
  packer reproduces the product's `4096x2251` atlas exactly), and the material
  chain resolves at those pixels (section 6). In the pre-fix frame, 76.8% of
  this population was *back faces* of their own `dface_t` plane — surfaces
  drawn from the side they were never lit for. Whether the fixed frame keeps
  that share is NOT VERIFIED (needs the face-id capture in section 4b).
- **Why it is task-sized.** Fixing it means either (a) not drawing the back of
  a face — a winding/cull decision, which this repo's rules allow only with a
  first-hit oracle and a same-view pixel improvement (T26 measured the eye
  oracle: shipped mask 07 keeps 458/594, mask 05 drops to 0.325), or (b)
  lighting the back of a face — a lightmap-composition decision that needs its
  own two-sided-light rule and its own pins.
- **Cheapest next verification** (for whoever takes it): one `--world-face-id`
  capture at tick 2000 on this branch, the offline plane test in
  `.scratch/t29/backface.py` (kept in the worktree; the script is small enough
  to re-derive from the report), and the T26 eye oracle run on the same camera.

This blocker is *not* a regression from this task: before the fix this
population was 183,571 px and the frame showed a dollhouse; now it is 205,911 px
on the correct surfaces. It is the next visual-quality task, not a reason to
revert a projection fix.

## 11. Files

| file | role |
| --- | --- |
| `native/include/native_renderer.h` | publishes the near/far planes the draw used |
| `native/src/native_renderer.cpp` | the world near plane is converted from map units (4.0) |
| `native/src/main.cpp` | the `gpu: world clip` reading, in box units and map units |
| `world-near-clip-check.sh` | focused forward gate + the near-clip mutation |
| `trace-near-points.py` | depth -> map-unit distance at the named trace points |
| `visual-baseline-check.sh` | region pins re-measured, clip reading added, M5 bound follows W1 |
| `evidence/t29-world-visual/` | frames (before/after bmp+png), region readings, chain tables, clip reading, hashes, gate logs |
