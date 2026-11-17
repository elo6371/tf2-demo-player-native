# World holes 2026-11-17 — geometry face account across the nine demo maps

Branch `work/world-holes` (worktree `D:/TF2_Native_Worktrees/t105-holes`), base commit `cd04373f`
(`candidate/e5`). All numbers below are from this commit.

The complaint being answered, verbatim:

> 地板很多结构缺面，楼梯斜坡也有

> 地图还是一堆破洞和奇怪的bug

**Status of the complaint, stated honestly:** the face identity closes on all nine maps, three
mechanisms are dead (budget, displacement fallback, atlas nolight), and one live class is named
and sized (M4, refused brush models — 283 visible faces on upward alone). The floor/stair class
the user is actually reporting is **not yet explained**: it is pending the black-pixel taxonomy
and the cull A/B in §4. An earlier revision of this page could be read as saying the census had
answered the complaint; it has not, and §1a says exactly what a census of counters is blind to.

Scope of this document: the GEOMETRY half of that report — faces that the map contains but
the frame does not draw. Materials (team-tinted tint, white-noise fences, black faces that
*are* drawn) are a separate card and are only mentioned where an instrument had to rule them
out.

## 1. The account has to close before any mechanism is named

Every face in the BSP is in exactly one bucket:

    faces total = rendered + sky + skippedNoDraw + skippedInvalid + skippedBrushModel

with a definitional catch that bit an earlier reading of these numbers: `rendered` EXCLUDES
sky, and it is itself

    rendered = (worldFaces - sky - skippedNoDraw - skippedInvalid) + brushFacesDrawn

Probe: `native/build-nmake/world_geometry_probe.exe --tf-root <tf> --map <bsp>` (same source as
the product, commit `cd04373f`). Runner: `world-holes-census.sh` (repo root); raw output `.scratch/world-holes/census/<map>.txt`.

| map | total | rendered | sky | noDraw | invalid | brushRefused | worldFaces | brushFaces | triangles | disp cand/emitted/fallback |
|---|---|---|---|---|---|---|---|---|---|---|
| pl_upward_f12 | 15155 | 14505 | 172 | 2 | 0 | 476 | 13497 | 1182 | 66707 | 568/567/1 |
| cp_process_f12 | 12935 | 12145 | 356 | 0 | 0 | 434 | 11905 | 596 | 71999 | 934/934/0 |
| cp_sunshine | 20449 | 19156 | 760 | 0 | 0 | 533 | 16787 | 3129 | 82859 | 642/642/0 |
| cp_sultry_b8a | 21289 | 20251 | 720 | 0 | 0 | 318 | 12131 | 8840 | 68966 | 342/342/0 |
| cp_gullywash_f9 | 17683 | 16819 | 518 | 0 | 0 | 346 | 16221 | 1116 | 107623 | 615/615/0 |
| cp_reckoner | 32695 | 31807 | 568 | 6 | 0 | 314 | 16359 | 16022 | 121780 | 509/509/0 |
| koth_bagel_rc11 | 18231 | 17237 | 677 | 0 | 0 | 317 | 11157 | 6757 | 97769 | 1427/1418/9 |
| koth_ashville_final1 | 6457 | 5944 | 304 | 0 | 0 | 209 | 5011 | 1237 | 23276 | 77/74/3 |
| koth_product_final | 11175 | 10469 | 576 | 0 | 0 | 130 | 9610 | 1435 | 55878 | 557/553/4 |

The identity closes on all nine maps, to the face. So the census is a complete account of where
every face went: nothing is unattributed, which is what makes the mechanism ranking below a
ranking and not a list of guesses.

## 1a. What this census cannot see

The account above counts FACES. Three ways a face can be counted and still be a hole are invisible
to it by construction, and all three are live suspects for 地板缺面:

1. **A face counted `rendered` whose emitted triangles are wound against the view.** The counter
   sees emission, the rasteriser sees `D3D11_CULL_BACK`. The source already concedes this number
   at `native/src/bsp_map.cpp:842-850`: *"misfaced … 15126 of 99312 after the third-round
   decision, all of them brush/side-1, with 5388 of 16136 faces authored on the back of their
   plane"*. Those triangles are simultaneously drawn-by-the-counter and invisible-on-screen. That
   is why the cull A/B (§4) is the decisive experiment and why no counter number in §3 may be
   read as "nothing is wrong" on its own.
2. **A triangle drawn in a colour the eye reads as a hole** — black albedo (card-overview's
   (470,280)) or a black lightmap. The identity closes and the pixel is still black. The word
   "hole" must not swallow this class; it gets its own column in §4's taxonomy.
3. **A face whose polygon or fan degenerates at emission.** `skippedInvalid` covers the polygon
   build, but a fan that pushes zero triangles *after* that is in no counter at all — the face
   counts as rendered, the identity closes, and the screen gets nothing.

## 2. Which BSP the product actually reads (settled, one line)

`planMapLookup` (`native/src/map_lookup.cpp:64-79`) tries `maps/<name>.bsp`, then
`download/maps/<name>.bsp`, then the VPK entry. The demo headers name the nine maps above; two of
them (cp_sultry_b8a, koth_bagel_rc11) resolve from `download/maps`, the other seven from
`maps/`, and every one of the nine loads a real file. Wherever a `地图/` copy exists beside the
demos it is the same byte size as the product's copy, so the census and the shipped geometry are
the same bytes. **There is no wrong-version-BSP class in this report.**

The product states the resolution itself: `map: lookup map=pl_upward_f12 store=maps
selected=maps/pl_upward_f12.bsp bytes=25252306 reason=found`.

## 3. Mechanisms, scored

### M1 Triangle budget — DEAD

Budget capacity is 4,000,000 in the probe's default run and 400,000 in the product
(`map: geometry budget capacity=400000 requested=66737 dropped=0 facesDropped=0
brushFacesDropped=0 brushModelsDropped=0 firstFace=-1 firstBrush=- state=within` for
pl_upward_f12). Requested triangles are 23,276–121,780 across the nine maps. Nothing is dropped
on any map, and `state=within` on all nine. The 100M-triangle control run (`--triangle-limit
100000000`) produces byte-identical face counts, so no face is losing a race to the cap.

### M2 Displacement grid refused → flat fallback — DEAD as a big lever

The prime suspect for 楼梯斜坡 (stairs and slopes) does not survive the count. Across all nine
maps only **17** faces fall back (bagel 9, product 4, ashville 3, upward 1), every one with reason
`cell`, and a fallback still pushes a flat quad — it is drawn, not missing. The requested-vs-
buffer triangle delta closes exactly as those rollbacks: bagel 260, product 120, upward 30,
ashville 0. A face whose every cell is degenerate pushes nothing, so requested == buffer.
Stairs and slopes in these maps are brush geometry, not displacement, so this mechanism could not
have produced the complaint anyway.

### M3 Faces skipped from the lightmap atlas — NOT A HOLE

`native/src/bsp_map.cpp:684-688`: the atlas pass skips faces with `styles0 == 255` or a bad
`lightofs`, and `:791` sets `hasLightmap = false`. Emission never consults the style byte, so
those faces stay in `map.triangles` and are drawn **unlit**. This is a lighting difference, not a
missing face, and it stays available to the materials card as a cause of flat-looking surfaces.

### M4 Static brush-class whitelist — LIVE, and the only face loss a *counter* census can name

(see §1a for what that qualifier means: a counter cannot see a face that is counted and culled.)

`native/src/bsp_map.cpp:585-600`:

```cpp
const auto brushClassIsStatic = [](const std::string& name) {
  return name == "func_brush" || name == "func_wall" || name == "func_lod"
      || name == "func_illusionary";
};
```

Every other brush model is refused as `reason=moving` and its faces never reach the buffer. This
whitelist has already produced exactly this defect once, and the source says so (`:577-585`,
T88):

> func_illusionary is in the list, and its absence was a defect, not a policy. […] it was
> excluded only because the list was written from the three classes earlier tasks had needed, and
> a class that is missing from a whitelist fails silently.

T88's measurement: 2,892 of snakewater's 3,278 skipped brush faces were `func_illusionary`.

Product rows for pl_upward_f12, tick 0 (`.scratch/holes/out/upward-tick0.trace`):

    gpu: world brush models total=173 drawn=100 skippedMoving=73 skippedUnknown=0 \
         facesDrawn=1182 facesSkipped=476 worldFaces=13497 brushFaces=1658
    (check: 1182 + 476 = 1658)

Refused models by class, with the faces each withheld:

| class | models | faces | visible in the map? |
|---|---|---|---|
| func_door | 14 | 84 | **yes** — a whole door, 6 faces per model, is real geometry |
| func_nobuild | 17 | 102 | **yes** — boards/trim carrying the no-build volume |
| func_respawnroom | 4 | 89 | **yes** on some maps — the room's own walls |
| func_respawnroomvisualizer | 8 | 8 | **yes** — the spawn barrier quad |
| func_physbox_multiplayer | 4 | 24 | yes, but it moves (physics) |
| func_regenerate | 8 | 48 | no — the cabinet trigger |
| func_nogrenades | 2 | 22 | no — clip |
| trigger_multiple / _once / _hurt / _capture_area / dispenser_touch_trigger | 16 | 99 | no — triggers |

So on this map `skippedMoving` is NOT 73 invisible volumes: at least 34 models / 283 faces of
shape the player can see are deleted by a class test, not by the renderer. That is a floor-and-
structure class, and it is invisible in every pixel count that starts from the emitted buffer —
which is why the face census had to be the first instrument.

Whether these are the surfaces behind the specific black regions the complaint is about is the
next question and it is a pixel question, not a class question; the A/B gate for it is proposed
in §6, not asserted here.

### M5 Winding / PVS — the eye oracle is a tool disagreement; the pixel-proven class is W-1 (18 faces)

Two draw-time oracles were run. Both are described here with their controls, because a number
from either one would otherwise look like a defect.

**(a) Ray-down from above** (`--ray-down 512`, 262,144 cells per map). Fraction of cells whose
topmost emitted triangle is front-facing upwards:

| map | hits | misses | drawn | fraction |
|---|---|---|---|---|
| pl_upward_f12 | 195003 | 67141 | 165106 | 0.847 |
| cp_process_f12 | 28450 | 233694 | 20190 | 0.710 |
| cp_sunshine | 75156 | 186988 | 43421 | 0.578 |
| cp_sultry_b8a | 102424 | 159720 | 94215 | 0.920 |
| cp_gullywash_f9 | 93727 | 168417 | 75643 | 0.807 |
| cp_reckoner | 74446 | 187698 | 61895 | 0.831 |
| koth_bagel_rc11 | 167748 | 94396 | 141912 | 0.846 |
| koth_ashville_final1 | 142113 | 120031 | 113884 | 0.801 |
| koth_product_final | 179704 | 82440 | 178387 | 0.993 |

The shortfall is not a hole rate. It splits almost perfectly along the face's plane-side bit:
brush `planeSide=0` reads storedUp≈all / storedDown≈0, brush `planeSide=1` reads
storedUp≈0 / storedDown≈all (ashville 0/27016, sunshine 651/28316, bagel 692/25575, upward
692/22623, gullywash 0/14710). The shipped winding follows the side bit coherently on both
cells. The topmost **emitted** triangle at a roofed cell is the roof's underside — sky and nodraw
brushes are not in `map.triangles` — so "not visible from above" is the correct reading for
indoor cells, and the fraction tracks roofedness (open-skybox product 0.993 vs indoor-heavy
sunshine 0.578), not damage. The premise "in an open-air cell the topmost triangle is the floor
top" does not hold in this data without a roofedness filter, so this instrument cannot be quoted
as a hole count.

**(b) Ray-eye from spawn eyes** (`--eye` + `--ray-eye <eye> 48`, 4,608 directions per eye).
On pl_upward_f12 from eye (612,1512,596):

    dirs=4608 hits=4608 misses=0 shippedRuleKeeps=0 of 4608 (0.000)
    first-hit class=brush storedTowardEye=0 storedAway=4608
    classification drawn=0 visRejected=0 backRejected=4608

i.e. from a spawn inside a working map, 100% of first surfaces read "culled". That cannot be
true of a renderer whose frames are closed, and the probe's own `rule-table` says why the
question is malformed: the top-down oracle and the eye oracle pass opposite sign conventions to
the same `orientedFacing` lambda (top-down passes +Z, i.e. surface→sky; the eye passes the ray
direction, i.e. eye→surface), so no single orientation rule can satisfy both:

| map | best rule by top oracle | best rule by eye oracle | best rule that satisfies both (worst-of-two) |
|---|---|---|---|
| pl_upward_f12 | mask 05 top=0.959 | mask 00/04/08/12 eye=1.000 | mask 12, worst=0.797 |
| cp_sunshine | mask 05 top=0.943 | mask 00/04/08/12 eye=0.953 | mask 12, worst=0.759 |

The shipped rule does not have a row of its own: it keeps brush triangles and flips a
displacement triangle only when that triangle points against its face's outward, which is per
triangle, not per (class, side) cell. What matters is the two readings sit at opposite ends of the
same table — the ray-down reading (0.847) is the *keep everything* reading (mask 15) and the eye
reading (0.000) is the *flip everything* reading (mask 00) — i.e. the two oracles are not
measuring the same sign convention. A 100% back-face reading at the same pose where T23 measured 19.40%
back-face rejection of the finished frame is a tool disagreement, not a defect. **No ray-eye or
winding number is quoted as a hole rate in this document.** Calibrating one sign against a
product frame is the first item in §6.

### W-1 Cull-back removes present geometry - pixel-proven, and NOT a whole-world inversion

**Retraction, first, because an earlier draft of this card said the opposite.** "W-1 = the whole
world's winding is inverted" is withdrawn. What the pixels support is much smaller: at the pinned
pose, **5,501 pixels** lose a surface that is present in the map, and that surface is **18 BSP
faces**, all on the worldspawn model.

How it was established (`--no-world-cull` against the same pinned pose, everything else equal):

- normal vs no-cull differ on **9,380 px = 1.090% of the frame**, bbox (377,0)-(899,527); of the
  changed pixels **5,501 had nothing at all** in the culled frame (they were the clear colour) and
  9,237 carry real albedo. So the difference is geometry appearing, not shading drifting.
- the clear colour is exactly **(14,18,22)** and all **78,586** empty world-pass pixels are that one
  colour (9.13% of the frame), which is what makes "nothing drawn" measurable rather than eyeballed.
- the black-pixel taxonomy has three columns and it puts W-1 in its place: of the frame's **10,291**
  black px (RGB sum <= 24), **(a) nothing drawn = 0**, (b) albedo black = 6,072, (c) albedo fine but
  shaded dark = 4,219. The user's black floor is a **drawn** surface; W-1 does not explain it, and
  that class belongs to the material/lighting card.

Provenance of the 5,501 (decode of the face-id frame, `r=id&0xFF, g=(id>>8)&0xFF, b=(id>>16)&0xFF`,
`native/src/native_renderer_world.cpp:31-37`):

| bucket | faces | pixels |
|---|---|---|
| world face (BSP index < 13,497 = worldFaces) | 18 | 5,501 |
| brush-model face | 0 | 0 |
| other | 0 | 0 |

16 of the 18 are displacement faces (5,475 px); the two brush faces are 11220 (82 px) and 13241
(13 px). **A counter census cannot see this class at all - it is 0 models and 0 faces in every
refused-class histogram - which is why M4 and W-1 are two separate accounts.**

Three negative controls, all landing trace/BMP:

1. `--no-world-vis-clip` at the same pose: **0 pixels changed**, all 78,586 empty px stay empty. PVS
   and cluster clipping are not part of this mechanism.
2. `--world-geometry none`: all 78,586 empty px stay empty, so **no brush model and no prop** covers
   them - the loss is inside the world pass.
3. The capture eye is in BSP leaf **2447, contents=0 (EMPTY)** - the eye is not inside a solid, so
   "the camera was buried in geometry" is not the explanation for this pose either.

**What cull-off filling does NOT prove, and the per-triangle audit that settles it.** Drawing the
geometry with culling disabled shows that an away-facing triangle exists at those pixels; it does not
show that the triangle *should* have been drawn. The decisive number is per triangle: the sign of
`dot(emittedCrossNormal, eye - centroid)` read together with the emitted triangle's own reversal
flag. The probe (`world_geometry_probe --face-info ... --eye ... --face-triangles`) dumps every
emitted triangle of the 18 hole faces plus 7 faces this frame provably draws, and the control is
unambiguous: every drawn face reads **toward** the eye (12953 31/32 toward, 12954 31/32, 12982 32/32,
9218 6/6, 12038 3/3), so toward-eye is the drawn sign and away is the culled one.

Sampled every third hole pixel (1,802 of 5,419; attribution from the product's own `--world-hole-trace`
record, where `any=` is the emitted triangle the ray hits; 100% matched):

| class | samples | share |
|---|---|---|
| displacement, **not** reversed by the product, facing away | 1,786 | **99.1%** |
| displacement, reversed by the product, facing away (map order faced the eye) | 16 | 0.9% |
| facing the eye yet empty | 0 | 0.0% |

So **99.1% of W-1's pixels are triangles the map itself authored facing away from this observer**.
Cull-back is doing its job there, and the no-cull frame is drawing a surface the map authored as a
back face. At this pose the eye looks at the inside of a displacement pelt: a measurement artifact,
not a product defect. The 0.9% residue - all on face 13191, ~92 px at this pose - is the only part
the product caused: the mask-07 reversal turned a map-authored front into a culled back face.

**The candidate fix, measured rather than argued (`--world-keep-map-winding`).** The two scopes
collapse into one, because the reversal rule only ever touches displacement triangles
(`native/src/bsp_map.cpp:1114` carries `&& displacement`) and the buffer says so out loud:
`map: geometry winding keepMapWinding=0 flipped=2519 misfaced=15038 misfacedDisplaced=0` against
`keepMapWinding=1 flipped=0 misfaced=17557 misfacedDisplaced=2519`. Same pinned pose, captures
deterministic (two runs of the same command differ by 0 px):

| reading | value |
|---|---|
| hole px filled | **92 of 5,419 (1.70%)** |
| whole-frame changed | **365 px (0.042%)** |
| new holes (drawn before, clear after) | **67 px** |

Net +25 px. The hypothesis that a pelt should trust the lump's own vertex order is **refuted by
measurement**: the lump order is precisely what the 99.1% already use. Removing the reversal does not
open the hole set, and it costs almost as much geometry as it recovers.

**Verdict, and its boundary.** W-1 is not a winding bug in the product at this pose; it retires as a
measurement artifact. It explains nothing about the user's report: **the user's floor/stair report
is not yet explained by any pixel-proven mechanism on this card.** M4's class-level whitelist is the
remaining live candidate, and it is gated on its own pixel proof.

**Known residue, booked as a number rather than a bug.** 0.9% of the sampled hole pixels (16 of
1,802; all on face 13191, ≈92 px at this pose) are the product's own doing: they are triangles the
mask-07 reversal turned into back faces although the map's own order faced the eye. It is written
here as a **bounded residual**, not a defect, because removing the rule does not repair it - it
fills those 92 px and opens 67 px of other geometry (the 2,519 triangles the rule legitimately
reverses), so the rule alone is not a lever on it. Fixing that residue would need a per-triangle
decision the map cannot state for a warped pelt (the base plane's side bit is not a per-triangle
facing verdict), i.e. a different instrument than this card has.

**The rule that was ruled out before it was tried.** Applying "flip every emitted triangle whose
dot<0" would flip **150,940 of 363,649 brush triangles (41.5%)** across the nine maps - 2.7x the
15,126/99,312 order of magnitude of the known authored-back faces - and it is a no-op for
displacements, which are already reversed. That rule is void; it would trade 5,501 px for a
whole-frame inversion.
## 4. Per-frame accounting (in progress)

The product's own frames are the arbiter. Baseline capture, pl_upward_f12 tick 0, demo camera
(`eye=308.7,-1983.7,199.7 pitch=10.94 yaw=354.37 src=follow`, arm fired at `frame_tick=16`):

![upward tick0](/.scratch/holes/out/upward-tick0.png)

Raw: `.scratch/holes/out/upward-tick0.bmp`, trace `.scratch/holes/out/upward-tick0.trace`.
The black regions beside/under the rails at (700-900, 400-520) are the pixel class the user's
report is about — 用户报的缺面 is not an abstraction, it is these pixels. The materials card traced
(760,400) and (800,415) to `tri=none reason=no-drawn-triangle`.

One correction on what that sentence is allowed to mean: `tri=none` is a **CPU oracle**
statement, and the M5 section below is the proof that a CPU oracle can disagree with the
rasteriser (the two oracles there pass opposite sign conventions to the same lambda). So the doc
does not say "no geometry is drawn there" on oracle evidence alone. The rasteriser's own answer is
one capture away — the depth view — and it is what §4's taxonomy reports.

The lead's own read of the frame adds the discriminating detail: the wedge is **pure black**
while the sky at the top of the same frame is **dark navy**, so "the empty pixel's ray lands on a
BSP sky face" cannot be the whole story unless that sky face paints the pixel navy. The taxonomy
therefore reports the per-pixel COLOUR of the empty pixels next to the sky colour, not only the
face kind behind them.

The sky-vs-hole split for this pose is the T26 matrix (`world-geometry-hole-capture.sh` with
`CAPTURE_MAP/DEMO/TICK`, five extra frames: sky-faces, sky-depth, sky-ids, flat, depth) fed to
`world-geometry-hole-account.py --dir <cap> --out <ana>`, which reports missing pixels split into
sky-covered and unexplained. **Status: pending — see §6.**

## 5. The eye-inside-solid control - measured, the capture eye is not in solid

The world pass draws `D3D11_CULL_BACK` (`native/src/native_renderer.cpp:741`), so a spectator
camera that has passed through a wall or floor slab sees the back of every surface in front of
it, and nothing is drawn. **This pose is not one of those:** a point-in-leaf test (BSP node walk,
child[0] = front, leaf `contents`) puts the capture eye (308.7,-1983.7,199.7) in leaf 2447 with
`contents=0` - empty, not `CONTENTS_SOLID`. That looks exactly like "the floor lost its faces" and it is a class the
user can hit at will with a free camera. The product already has the control: `--no-world-cull`
(the diagnostic rasterizer state at `:758-761`, which the T26 capture script uses for the sky
frames). The experiment is: find a floor height with `--ray-down`, stand the camera 128 units
below it with `--debug-camera`, capture the same pose with and without `--no-world-cull`. A void
frame next to a full no-cull frame names the class as culling, not missing faces.

The class is still real for other poses (any camera that clips through a slab), and its repair is a
product decision (draw the world double-sided when the eye is inside a solid, or keep the spectator
eye out of solids) that belongs to the user. For **this** pin, the class is excluded by measurement,
not by argument.

## 6. What remains open

1. Calibrate one of the two oracle signs against a product frame; only then may winding numbers
   be quoted (M5).
2. Per-frame account for the captured poses: missing pixels → sky-covered vs unexplained (T26
   tooling, §4).
3. Eye-inside-solid control (§5), normal vs `--no-world-cull`.
4. A/B gate for M4: add a refused-but-visible class (`func_nobuild`, `func_door` at its
   compile-time pose) to the whitelist behind a flag, capture the same pose, and require the
   frame to change where the withheld faces were — plus a negative control that a truly moving
   class (physics) does not change. **Needs lead approval before any product edit.**

## 7. The user-view measurement (does the user see a hole?)

Method: the demo's own first-person camera at a tick (standing eye height by construction), metric =
pixels whose colour is exactly the clear colour (14,18,22) **and** whose depth is empty, grouped into
4-connected components; a component that touches the frame edge is sky/outside and is discarded. What
survives is "a patch of nothing, fully surrounded by drawn geometry" - the shape a missing surface
makes. Depth is required: colour alone over-counts by 3,542 px on one frame (dark geometry that
shades to exactly the clear colour, §4).

| pose | enclosed clear px | components | full-frame clear | drawn luma p1 / p5 / p50 |
|---|---|---|---|---|
| upward tick 0 (= tick 600, frames are 0 px apart) | **8,976** | 43 | 78,586 (9.13%) | 6.7 / 16.0 / 90.6 |
| upward tick 1800 | **2,072** | 15 | 2,072 (0.24%) | 11.9 / 15.5 / 45.4 |
| cp_process_f12 tick 600 | **985** | 25 | 169,689 (19.71%) | 11.1 / 29.0 / 131.1 |

The number is **not zero at any pose**, so "the user is seeing darkness, not holes" is not what the
frames say. At the tick-1800 pose the largest components sit in one place (x 167-250, y 148-233) and
the frame is 100% enclosed-clear (0 edge-connected px - the camera is indoors), so that region is a
single coherent patch, not noise.

**What that patch is.** 400 of the 2,072 px were traced with the product's `--world-hole-trace`:
**all 400 hit the same world face 13233 (material COALMINES/BLENDGROUNDTOWALL_COALMINES) and all 400
have `drawn=-`** - i.e. the geometry exists, the ray hits it, and the culler skips it; the sky face
behind it (9947, TOOLS/TOOLSSKYBOX) is not drawn there either, which is why the pixel falls through to
the clear colour. This is the W-1 signature again (face exists, is hit, is culled), but at a *user*
pose and on a *single* face rather than inside a pelt - it is the first candidate in this card that
looks like the user's report, and it is **not yet per-triangle audited** (that needs the demo eye at
tick 1800, which the trace does not print; the audit is the next measurement, not a conclusion).

## 8. M4 three-bucket gate (material evidence, not class names)

A class can only be STATIC-VISIBLE if its faces bind real materials; a tool texture is editor-only.
`world-holes-face-materials.py` resolves every refused brush face to its material (lumps 2/6/7/43/44,
LZMA-aware). On pl_upward_f12:

| class | faces | materials | bucket |
|---|---|---|---|
| func_nobuild | 102 | TOOLSPRO/TOOLSNOBUILD | INVISIBLE |
| func_respawnroom | 89 | TOOLSPRO/TOOLSRESPAWN | INVISIBLE |
| func_regenerate | 48 | TOOLSPRO/TOOLSREGENERATE | INVISIBLE |
| func_nogrenades / trigger_hurt / trigger_capture_area | 22 / 18 / 6 | TOOLS* tool textures | INVISIBLE |
| trigger_multiple / trigger_once / dispenser_touch_trigger | 63 / 6 / 6 | TOOLS/TOOLSTRIGGER | INVISIBLE |
| func_physbox_multiplayer | 24 | TOOLS/TOOLSTRIGGER | INVISIBLE (and animated) |
| func_door | 84 | METAL/IBEAM001B, PROPS/METALDOOR01_* | ANIMATED (visible) |
| func_respawnroomvisualizer | 8 | OVERLAYS/NO_ENTRY | STATIC-VISIBLE (overlay) |

Across the nine maps the same rule holds; the only visible static classes found are
func_respawnroomvisualizer (8 faces per map) and func_breakable (24 faces on sunshine);
func_rotating (32) and func_tracktrain (34) on bagel are visible but animated. So the *whole*
STATIC-VISIBLE population on upward is 8 overlay faces - an order of magnitude below the 5,501 px of
W-1, and it cannot explain a missing floor.

Gate mechanism: `--world-allow-class <classname>` (repeatable) extends the static whitelist for one
run; the default list and every default-behaviour path are untouched. Measured at the pinned pose:
allowing `func_respawnroomvisualizer` changed **0 px** and its face ids appear **0 px** in a
`--world-face-id` capture, and allowing `func_physbox_multiplayer` also changed 0 px. Both zeros are
honest but **inconclusive**: neither class has a face in that pose's view, so the frame cannot test
them. The gate needs a pose that frames the visualizer overlays; that pose is not yet chosen.

## 9. The 2,072 px at pl_upward tick 1800, characterised

**The pose, printed by the frame itself.** A new trace line
`camera: capture eye=x,y,z pitch=.. yaw=..` (emitted by the hole-trace pass, so it belongs to the
captured frame) reads `eye=-1945.346,-1016.639,243.072` at tick 1800.

**Attribution: enclosed-clear components to faces.** Decode note that matters: the clear colour
(14,18,22) decodes to id **1,446,414**, so a pixel that is *still* clear with culling off reads
1,446,414 and means "nothing drawn even unculled" - not a face.

| pose | enclosed px | on one face | nothing drawn | other faces |
|---|---|---|---|---|
| pl_upward tick 1800 | 2,072 | **13233: 2,063 (99.6%)** | 9 | - |
| cp_process tick 600 | 985 | - | 956 | 11008 17, 11007 12 |
| pl_upward tick 0 | 8,976 | - | 3,475 | 13151 2,406; 13180 1,741; 13152 698; 13181 139; 13007 97; 11220 82 (brush); 13157 72; 13013 57; 13183 56; 13191 50; 13147 28 |

Shape answer: tick 1800 is **one face**; process is almost entirely "no geometry at all"; tick 0 is a
dozen displacement faces plus one brush face. "3-5 faces with an orientation problem" fits only the
largest single user-visible hole.

**Face 13233 per-triangle audit at that eye.** 32 triangles, all `disp=1`, **all `flip=0`** (the
mask-07 reversal never fired on this face), and **31 of 32 face away** from the eye
(toEye -183.8 .. -691.6; one triangle +27.4). Since flip=0 the emitted order *is* the map's index
order, so the map authored 31/32 back-facing at this pose and the product culls them correctly.
Material `COALMINES/BLENDGROUNDTOWALL_COALMINES`, plane 4482 planeSide=0: the ground-to-wall blend
family, as suspected. (The probe does not print `faceOutward`; flip=0 proves the branch did not
engage, which is the operative fact.)

**keep-map-winding A/B at tick 1800.** Enclosed set filled **765 / 2,072 (36.9%)**, new holes
**75 px**, whole frame changed **840 px (0.098%)** - against 92/5,419 (1.7%), 67 and 365 at tick 0.
Attributing the change (cull-on face-id with the flag, and without it):

- all **765 filled px** are drawn by **face 13074** (same blend material, displacement, planeSide=1):
  geometry *behind* the hole that the reversal had culled;
- all **75 new holes** were drawn by five faces: 13056 27, 13048 22, 13081 16, 13051 9, 13079 1.

So the rule never touches face 13233: it trades 765 px of one blend face for 75 px across five
others. Net +690, and the user's hole is unchanged. **The flip rule is not this face's bug**, and
tick 0's "net +25" was not two poses cancelling - both poses have a hole whose owner the rule does
not affect.

**Consequence, not a fix.** Every face that loses pixels to culling at these poses is in the same
material family: `BLENDGROUNDTOWALL_COALMINES` displacement pelts authored with mixed orientation,
culled by one global world state. The shape a fix would take is a *material-level* cull decision
(Source's `$nocull`), and there is a concrete, testable asymmetry: `vmt_material.cpp:316` parses
`$nocull` and entity ranges consume it (`native_renderer_entity.cpp:584` sets `range.noCull` from
material flag bit 256), while **no world-render source mentions `noCull` at all**. Whether this
material actually sets `$nocull` is **not verified**: the VMT is not in the four `*_dir.vpk` files
present. That is the next single check, not a conclusion.

Status: the 2,072 px are one authored-back-facing blend pelt (13233) seen from inside, plus 9 px of
no geometry - the same class as W-1's 99.1%, but at a user pose and concentrated on one face. The
user's floor/stair report still has no pixel-proven mechanism.

## 10. Correction to 9, and the four follow-ups the lead ordered

### 10.1 Retraction: "the map authored 31/32 back-facing" is not supportable

`flip=0` says only that the extra mask-07 swap did not fire. It does **not** say the emitted order
equals the map's index order, and reading the code, a displacement face has no index triple to equal:

- the grid corners are taken from the face polygon in its own order,
  `quad[i] = polygon[(corner + i) % 4]` (`native/src/bsp_map.cpp:1225`);
- rows are `lerp(quad[0], quad[3])` and `lerp(quad[1], quad[2])` (`:1239-1240`), columns lerp
  between them (`:1243`), and the displacement vector is added per grid vertex (`:1248`);
- each cell emits exactly two triangles, `pushTriangle(p00, p11, p10)` and
  `pushTriangle(p00, p01, p11)` (`:1314`, `:1316`).

So the emitted winding is **generated by our assembly** from the polygon order; it is not a
permutation of a stored triangle, and the brush path is different again (the fan emits
`(polygon[0], polygon[i+1], polygon[i])`, `:1364` — a deliberate reversal). Both facts the lead
raised are confirmed. Section 9's pixel numbers stand; its attribution sentence is replaced by:
**at this eye our assembly emits 31 of face 13233's 32 triangles facing away from the camera, and
the product culls them.** Whether that agrees with what the map's author saw in TF2 is a separate
question, and it is not answered by any counter on disk yet: `windingGridKeepsPolygon` /
`windingGridReversesPolygon` exist (`native/include/bsp_map.h:354-355`, filled at
`native/src/bsp_map.cpp:1054-1055`) but **no trace line prints them**, so the aggregate
"does the grid keep the polygon's order" number is not yet measured.

### 10.2 The ceiling of a cull-side fix, measured with `--no-world-cull`

Three user-view poses, depth captures with and without culling, same settings files
(`.scratch/holes/nocull.sh`, analysis `.scratch/holes/nocull.py`; drawn = depth > 12,
enclosed = clear pixel not connected to the frame border):

| pose | enclosed clear under cull | filled by no-cull | % | new holes | whole frame changed |
|---|---|---|---|---|---|
| pl_upward tick 0 | 8,976 | 5,501 | 61.3% | 0 | 5,501 |
| pl_upward tick 1800 | 2,072 | 2,063 | 99.6% | 0 | 2,063 |
| cp_process tick 600 | 985 | 29 | 2.9% | 0 | 29 |

Two things are load-bearing. First, at both upward poses a two-sided/material-level cull rule fills
the holes almost completely, and **at tick 1800 it closes 2,063 of 2,072 px** - the single leftover
9 px is the "nothing drawn" bucket. Second, **new holes are 0 at every pose and the whole-frame
change equals the filled count exactly**, i.e. the extra geometry lands only in the pixels that were
already empty. A cull-side fix therefore has a real ceiling worth building; it explains nothing at
cp_process, where it recovers 29 of 985 px.

### 10.3 `$nocull` is NOT set on this material

Read out of the game's own VPK tree (`.scratch/holes/vpkcat.py`: `tf2_misc_dir.vpk`,
v2 header, 28-byte header offset, tree entries `extension/path/filename`):
`materials/coalmines/blendgroundtowall_coalmines.vmt`, 323 bytes:

```
"WorldVertexTransition"
{
    "$basetexture"  "coalmines/rockwall012"
    "$basetexture2" "coalmines/dirtground004"
    "%tooltexture"  "Nature/blendgroundtograss003_tooltexture"
    "%keywords"     "tf"
    "$surfaceprop"  "dirt"
    "$bumpmap"      "Nature/rockwall009_height-ssbump"
    "$ssbump"       "1"
}
```

No `$nocull`, no `$cull "0"`. So the asymmetry found in 9 (`vmt_material.cpp:316` sets
`material.noCull`, the entity path consumes it, the world path never reads it) is **not a bug this
material exercises**: it is an unsupported-feature gap and is **not** available as a fix path. The
cull-side fix, if it is built, must be a policy decision about displacement/WorldVertexTransition
geometry, not a material flag the map is already setting.

### 10.4 cp_process tick 600 is not a missing-geometry hole - it is the sky

Hole-trace of 328 sampled enclosed pixels on that frame (`--world-hole-trace`, captured
`.scratch/holes/userview/pr600-tr.trace`) hits a **sky surface on 328 of 328**:
`sky=face=7848 material=TOOLS/TOOLSSKYBOX tri=351 depth=0.9963`, with `drawn=-` and
`any=-` (no front-facing world triangle) and `frameDepth=1.0000` (nothing written to that pixel).
So those pixels look at the map's skybox, and the world pass leaves the sky region clear - it is a
sky-pass distinction, **not** missing world geometry, and it stays clear under `--no-world-cull`
(which recovers only 29 px there). It must not be combined with the displacement holes in any
aggregate: cp_process contributes 985 "enclosed" px of which 956 are sky and 29 are geometry.

### 10.5 Where this leaves the fix decision

Reordered after the lead's m01273 ruling. Only the first entry is live:

1. **the sky pass is not drawing** - the only live candidate. The pixel is empty for a legitimate
   reason (correct culling, §10.6) and should show **sky**; retail never shows clear colour there.
   The question is therefore the sky pass's gate, not the world geometry. Investigated in §11.
2. **cull rule change** (W-1 keep-map-winding) - **refuted / ineffective**. Measured 765 px filled
   against 75 px newly lost at tick 1800, and a net +25 px at tick 0; the rule never touches face
   13233, so it cannot move this hole at all.
3. **assembly re-orientation** - **refuted for this face**. §10.6: 32/32 emitted triangles agree
   with the map's own plane normal and side bit, so the emitted winding is not a reversal and
   re-emitting in polygon order would not fix it.
4. **two-sided / material-class rendering** - **probable deviation from retail**: it would paint
   the back side of a surface the map deliberately authored single-sided. Kept only as a fallback,
   to be revisited if a residual hole remains **after** the sky fix. `$nocull` is not set on this
   material (§10.3), so it would need a policy rule of our own.

The aggregate grid-keeps/reverses counter is still unprinted, but it no longer decides anything:
the per-face plane audit answered the branching question for 13233 directly.

### 10.6 Face 13233 against the map's own plane: the emitted winding is NOT reversed

The face's plane is 4482: normal **(0.000000, 0.049939, 0.998752)** (essentially world up),
`dist = 147.015137`, `planeSide = 0`, so the map's front for this face is **up**. Measured:

- the capture eye is on that plane's front side: `dot(n, eye) - dist = +44.98`;
- all **32/32** emitted triangles have a raw normal with **+Z** (up), i.e. **32/32 agree with the
  map's own plane normal** and, with `planeSide = 0`, with the map's own side bit;
- and yet 31/32 face away from the camera: the pelt is steep (triangle centroid z ranges
  **1.3 .. 438.7** against `eye z = 243.1`, 14/32 centroids above the eye), so the eye sits on the
  back side of those triangles even though their normals point up.

So the emitted winding is **not** a reversal introduced by the grid parametrisation: it matches the
map's plane/side data on every triangle. Falling back on the lead's decision rule: 13233 is in the
"keeps" class, therefore **the parametrisation is not the cause and re-emitting in polygon order
would not fix this face** - it would flip only the small set of faces whose grid order disagrees with
their polygon, which is a different population. The remaining candidate is the two-sided rendering
described in 10.2, understood for what it is: a deliberate deviation from the map's own side data,
made to survive cameras that sit behind a surface.

The aggregate counters (`windingGridKeepsPolygon` / `windingGridReversesPolygon`) are still
unprinted; the per-face plane audit above answers the branching question for 13233 more strongly
than an aggregate would, but the one-line print is still owed and costs one build.

TF2 live comparison: **not done, deliberately** (lead's decision). Retro maps do not show through
towards their floors from any playable camera, and this pose is behind a pelt, so a retail capture
would spend hours reproducing a phenomenon we are confident does not exist there. Recorded as an
未做对照 with its reason, not as a gap.


## 11. The sky pass gate: the pixel is black because the pass never runs

The question from §10.5 is "what should that pixel show". The answer, measured: it should show sky,
and the reason it shows clear colour is not an occlusion rule but a **quality preset**.

### 11.1 The gate, in code

```cpp
// native/src/main.cpp:8641-8643
const bool skyEnabled = (worldSkyGiven ? worldSkyRequested : persistent.render.skybox) &&
    worldGeometry == WorldGeometryFilter::All && worldDebugView != 2;
```

and the preset decides `persistent.render.skybox`:

```cpp
// native/src/settings_store.cpp:61-63, :82
const bool standardQuality = readInt(file, L"preset", 0) == 1;
result.render.preset = standardQuality ? QualityPreset::Standard : QualityPreset::Performance;
...
result.render.skybox = standardQuality;
```

So `preset = 0` (the **default** when the key is absent) means Performance, and Performance means
**no sky**. The disabled path returns the reason that all the earlier traces in this card carry:

```cpp
// native/src/world_material.cpp:702-705
if (!request.enabled) {
  reading.state = WorldSkyState::Unavailable;
  reading.reason = "skybox draw disabled by the caller";
  return reading;
}
```

Order and depth, **as round 2 left it** (kept because §11's measurements were taken then; round 3
reversed both halves, see the note after this paragraph): the box was drawn **first, before the world**,
under its own rasteriser state, and it wrote **no depth** (`skyDepthDescription.DepthWriteMask =
D3D11_DEPTH_WRITE_MASK_ZERO`). It was not a "run after the world, paint only where depth is still 1.0"
pass, and there is no CPU occlusion/mask buffer that decides sky visibility from world faces. When the
pass is off, nothing covers the pixels the world leaves empty, and they stay clear colour.

**Round 3 changed the order and the shader, not the observable result of §11.** The box is now drawn
**after** the world's opaque batches - deferred to flush points inside and after the batch loop, so a
`$translucent` sub-batch still composites over it - and it is anchored at the far plane with its own
discard-free pixel shader (§13.1-§13.2). "Writes no depth" survived the change
(`native_renderer.cpp:698-706`). §11.2's "of those, painted by the sky" counts are therefore still the
right attribution of *which pass* painted those pixels; what round 3 adds is proof that on the final
build no world geometry owns them at all (§13.3).

`--world-depth` additionally forces the sky off on purpose (T52 comment, `main.cpp:8632-8640`): a
depth frame is a scalar image and a sky drawn into it would read as geometry.

The settings file every measurement in this card used is `.scratch/holes/userview/settings.ini`,
and its line 2 is `preset=0`.

### 11.2 Measured: force the sky on and the holes are painted

Driver `.scratch/holes/skycap.sh` re-ran the same two captures with `--world-sky`:

| pose | sky trace | enclosed clear px | of those, painted by the sky |
| --- | --- | --- | --- |
| pl_upward tick 1800 | `state=active vertices=36 triangles=12 faces=6 side=512 draws=1` | 2,072 | **2,072 (100%)** |
| cp_process tick 600 | `state=active ... side=1024 draws=1` | 985 | **985 (100%)** |

Top colours after: upward `(106,138,162)` 157, `(107,139,163)` 154, `(105,137,161)` 127; process
`(117,122,150)` 334, `(131,134,169)` 73, `(166,188,216)` 51. Before the change **every one** of
those pixels was exactly `(14,18,22)`. The sky touches nothing else: whole-frame changed pixels
equal clear pixels exactly, at both poses (upward 2,072 of 860,784 = 0.24%; process 169,910 =
**19.74%**).

The off state is in the older traces and was visible all along: `pr600-tr.trace:101` and
`up1800-tr.trace:80` read
`world sky-upload state=unavailable vertices=0 triangles=0 faces=0 ... reason=skybox draw disabled by the caller`,
with the post-frame line `world sky state=unavailable ... draws=0`.

### 11.3 What this changes about every earlier number

Every clear-pixel count in §9 and §10 was taken with the sky off, so "empty" and "black" were the
same thing by construction. The geometry findings stand - the faces really are culled, and §10.6
shows the culling is correct - but the *visible symptom* the user reports cannot be attributed to
geometry alone while the pass that should fill those pixels is disabled.

The 19.74% figure on cp_process is the one to keep in view: at the default preset, a fifth of a
normal gameplay frame is clear colour, and the correct content there is sky.

### 11.4 The fix decision, stated for the lead

The candidate fix is to decouple the sky from the quality preset: draw it whenever the world pass
runs, unless `--no-world-sky`, a non-`All` `--world-geometry` filter, or a depth debug view says
otherwise. That is a **product default behaviour change** - it alters every frame a Performance
user sees - and it will move any gate that pins the current reason string
(`native/tests/fixtures/gate-settings.ini:19`), so it is not taken unilaterally. The alternative,
if the preset coupling is judged intentional, is to leave the default and treat the black blocks as
"preset 0 is not a rendering-quality mode" - which does not match the user's report, since they did
not choose a quality mode before reporting missing faces.

**DECIDED (same day): the coupling is removed - see §12.** The paragraph above is kept as the
pre-decision text.

## 12. The fix: the sky is not a quality item (decided, built, gated)

### 12.1 The ruling and its three reasons

The lead approved decoupling the sky from the quality preset. The reasons are recorded here because
they are what makes this a product defect rather than a taste call:

1. **the cost account is wrong.** The pass is 12 triangles, 6 faces, 1 draw. Charging that to
   Performance as a quality saving is not an account anyone can defend.
2. **it is correctness, not quality.** Without the pass the clear colour *is* what the user sees
   where the map intends sky. That is not "a lower preset", it is a frame whose holes are unpainted.
3. **the factory default was the broken frame.** `preset` absent reads as 0 (Performance), so a fresh
   install - no settings file at all - drew no sky. A product-level defect outranks everything else
   this card has looked at.

### 12.2 The change

`native/src/settings_store.cpp`, `loadSettings()`:

- the compiled default is now set before the early return, so "no settings file" and "file without
  the key" cannot disagree: `result.render.skybox = true;`
- `result.render.skybox = standardQuality;` is replaced by
  `result.render.skybox = readBool(file, L"skybox", true);`
- precedence, highest first: `--world-sky` / `--no-world-sky` (`main.cpp:8641`) > an explicit
  `skybox=` key in the file > compiled default `true`. The preset no longer touches the sky.
- deliberately **not** changed: every other preset flag (they change shading and each has to be
  measured on its own terms, §11.1/T17), and `saveSettings()` does not write `skybox`, so the key is
  a hand-edit escape hatch until a UI toggle exists.

`native/tests/fixtures/gate-settings.ini`: `skybox=1` added under `[renderer]`, and the D-41
comment block re-pinned with the old reading and the new one. **Re-pin record:** the pinned frames do
NOT move, because `preset=1` already drew the sky before this change and `skybox=1` asks for the same
sky-on; what does move is the capture cache identity (`settings=<sha256>`), so the next release run
recaptures the matrix once, deterministically.

New gate: `world-sky-gate-check.sh` with `world-sky-gate-account.py` (pure-stdlib BMP reader; the
clear colour is RGB(14,18,22) and "enclosed" means 4-connected to the frame border through clear
pixels).

### 12.3 The gate: the assertions, all pass

Captured on the pinned user-view pose (pl_upward tick 1800) with an **empty** settings file, i.e. the
fresh-install configuration:

| # | assertion | measured |
| --- | --- | --- |
| A | preset 0 draws the sky, at the far plane, with the dedicated shader | `gpu: world sky state=active ... draws=1 ... farplane=1 shader=dedicated` - PASS (four assertions) |
| B | enclosed clear px == 0, and no clear-colour pixel at all | `clear_total=0 enclosed_clear=0` - PASS |
| C | negative control: `--no-world-sky` reproduces the old frame | `enclosed_clear=2072`, `state=unavailable draws=0` - PASS |
| D | the depth view still draws no sky | `state=unavailable draws=0` - PASS |
| E | an explicit key still wins: `preset=1` **and** `skybox=0` must not draw the sky | `state=unavailable draws=0`, `enclosed_clear=2072` - PASS (both assertions) |

`W-1 SKY GATE: 11 passed, 0 failed` (A x4, B x2, C x2, D, E x2), re-run on the far-plane deferred build in
§13. A's two extra assertions were added in round 3 so that "the sky is anchored at the far plane" and
"the sky pass runs the discard-free shader" are readable from the trace and not merely implied by the
pixels: the trace prints `farplane=1 shader=dedicated` on every capture that draws the sky, and both
gates A and B would go red if either regressed. E exists because the precedence has
two directions: before W-1 `preset=1` switched the sky **on** by itself, so E proves the preset stopped
deciding it either way and that a user who writes `skybox=0` by hand is still obeyed. C is the control
that makes B mean something: the same pose, same camera, same demo, one flag apart. What C asserts was
re-derived in round 3 and is stated in §13.5: it used to pin the 2,072 px population, and that number
turned out to belong to a different defect.

### 12.4 Closing measurement: the three characterised poses after the fix

Every capture below uses an empty settings file (fresh install), so the sky is being asked for the
default way:

| pose | before (sky off, preset 0) | after (sky on, default) | trace |
| --- | --- | --- | --- |
| pl_upward tick 0 | 8,976 enclosed clear px | **0** (clear_total 0) | `state=active ... side=512 draws=1` |
| pl_upward tick 1800 | 2,072 enclosed clear px | **0** (clear_total 0) | `state=active ... side=512 draws=1` |
| cp_process tick 600 | 985 enclosed clear px | **0** (clear_total 0) | `state=active ... side=1024 draws=1` |

There is no residual clear pixel on any of the three poses, and the instrument says what kind of pixel
they are: with the sky on, the per-pass depth planes (§13.3) read far-plane at these coordinates, i.e.
no world geometry owns them. The lead's instruction was: if a residual remains after the sky fix, that
is a real geometry remainder and gets a new round.

**Correction (round 3).** The sentence that used to sit here - "the pixels were never missing geometry
that the renderer dropped, they were pixels the renderer left unpainted while the sky pass was switched
off by a preset" - is withdrawn. The P0 fix to the transposed displacement parameterisation adds
**2,072 geometry pixels at the up1800 pose and loses none** (§13.7), which is exactly this population:
every one of those pixels WAS a hole in the terrain, and the sky pass was painting behind it. Two
defects landed on the same pixels - the sky being off made them black, the transposition made them
holes - and §12's own claim (a fresh install drew no sky at all, §12.1) is unaffected. What is
withdrawn is only the attribution of the *pixels*.

Per-map census to check that this was not a three-pose coincidence: every demo this card holds, tick
0, empty settings file, driver `.scratch/holes/census-sky.sh`, artifacts
`.scratch/holes/census-sky/`:

| map | clear_total | enclosed_clear | sky trace |
| --- | --- | --- | --- |
| pl_upward_f12 | 0 | 0 | active, draws=1 |
| cp_process_f12 | 0 | 0 | active, draws=1 |
| cp_sunshine | 0 | 0 | active, draws=1 |
| cp_sultry_b8a | 0 | 0 | active, draws=1 |
| cp_gullywash_f9 | 0 | 0 | active, draws=1 |
| cp_reckoner | 0 | 0 | active, draws=1 |
| koth_bagel_rc11 | 0 | 0 | active, draws=1 |
| koth_ashville_final1 | 0 | 0 | active, draws=1 |
| koth_product_final | 2 | 2 | active, draws=1 |

**The 2 px on koth_product_final: recorded on this branch, resolved elsewhere in round 3.** On this
tree (pre-P0) they are two adjacent clear pixels at (997,125) and (998,125), 0.0002% of the frame, and
the shape reasoning below held - a two-pixel hairline is a seam, not a culled face. On the P0 tree
(work/disp-param) the same two pixels are **world geometry** (RGB 54,54,54 / 68,70,51): they were a
seam of the transposed displacement parameterisation, and P0 removes them. So the paragraph is
withdrawn as a *sky* residual and kept only as history - the shape diagnosis was right, the culprit was
the terrain. On this branch they remain visible; a merge should delete this paragraph's first sentence
and cite §13.7.

**Criterion for the next reader:** a thin-line light leak that survives P0 is still worth checking
against the sky cage first (six quads, hairline at a shared edge) before opening a geometry round.

### 12.5 Cost, measured and recorded

Same demo, same pose, same preset 0, median of the steady-state rows of `--metrics-file` (12 rows
with `tick >= 1000`):

| build | median fps | frame time |
| --- | --- | --- |
| before (sky off) | 45.115 | 22.17 ms |
| after (sky on) | 38.835 | 25.75 ms |

delta **-6.28 fps / +3.59 ms per frame (-13.9% fps)** on this host. The pass is twelve triangles, so
this is not geometry cost: it is the textured box covering the frame area the world leaves empty,
which is exactly the work that was being skipped.

**Superseded by §13.6.** That "after" build still shaded the sky with the *world* pixel shader, whose
`discard` forfeits early depth rejection, so the -6.28 fps is the price of that implementation and not
of the pass. Round 3 gave the pass its own shader and anchored it at the far plane (§13.1-§13.2);
re-measured with an alternating A/B protocol the per-frame cost is inside the machine's noise band.

### 12.6 What this changes about the earlier numbers

Everything measured before this section - the 8,976, the 2,072, the 985, the per-component
attribution, the no-cull ceiling of 5,501 px - was captured with `preset=0` and therefore **with the
sky pass off**. Their correct reading is "pixels that should have been sky" and not "geometry the
world lost". The geometry findings are unaffected: the culling is correct (§10.6), the cull rule and
the winding rule do not touch the population (§10.2/§10.5), and none of that needed to change to fix
what the user sees.

### 12.7 Left open, stated rather than hidden

- **No UI toggle** writes `skybox` yet; a user who wants the old frame edits the key by hand.
- ~~**verify-all.sh is not wired to the new gate.**~~ **Wired in round 3.** `verify-all.sh` step 28
  ("world geometry at the pinned pose") now runs `world-sky-gate-check.sh` right after the 53-assertion
  hole gate and reports `WORLD-SKY-GATE=PASS (11 assertions)`. The gate is self-sufficient: it writes
  its own empty settings/ui files and derives the camera from the demo plus `--capture-tick`, so its
  only external inputs are the product, the demo and a python. When one of those is missing it prints
  `SKIP <reason>` and exits **3** - not a FAIL, and not a silent no-op. The chain counts that in a
  separate skip counter and prints it beside the pass count, so a skipped gate can never be read as a
  passed one: see §13.4 for the summary line.
- **The T26 hole gate could not be re-run on this machine.** It was attempted after the fixture
  change and stopped at its identity step, on two independent inputs: (a) `native/` is uncommitted,
  which that gate refuses by design ("a gate must describe a committed tree") and is the expected
  state until this branch is committed; (b) the demo it pins,
  `autorecord_2026-07-02_13-26-46.dem` under `tf/demos`, is not present here - this machine carries
  only the `/e/demo测试` set - so the gate cannot capture its matrix at all. Its pinned frames should
  not move (12.2's argument) and its capture identity did change (`settings=<sha256>`), so its next
  run on a machine holding that demo recaptures once; a red there is a re-pin with its own
  attribution, not a reason to revert this section. The failed attempt rewrote
  `evidence/t26-world-holes/analysis/identity.txt` before stopping; that file was restored with
  `git checkout -- evidence/t26-world-holes/analysis/identity.txt` and is not part of this round's
  diff.

## 13. Round 3: why the pass cost what it cost, and what the far plane moved

Round 2 (§12) fixed the defect and left one number unexplained: the sky cost **-6.28 fps / +3.59 ms**
for twelve triangles. Round 3 asked what that cost actually was, what the far-plane anchor changes in
the picture, and what the trace can assert about it - and it had to re-derive one gate assertion when a
second defect (P0, §13.7) turned out to share the same pixels.

### 13.1 The cost was never the order; it was the shader

Same build, same pose, same preset 0, the sky drawn **first** vs drawn **last**:

| order | median fps | frame time |
| --- | --- | --- |
| sky first (round 2) | 38.8 | 25.8 ms |
| sky last (order swap only) | 37.3 | 26.8 ms |

Moving the pass bought nothing - it came out slightly *worse*, inside noise. Root cause: the sky was
being shaded by the **world** pixel shader, and that shader contains a `discard`. A pixel shader with a
discard cannot rely on early depth rejection, so the sky's `LESS_EQUAL` test against the freshly drawn
world could not save the twelve triangles' fill: every covered pixel was shaded and then overwritten.
Order is a lever only when the pass can be rejected *before* shading.

The fix has two halves and both are needed:

1. **A dedicated pixel shader** - `skyPixelSource`, `native/src/native_renderer_shaders.cpp` (declared
   `native/src/native_renderer_shaders.h:28`, compiled and created in `native/src/native_renderer.cpp`
   beside the world pair): same cubemap, same `safeNormalize`, the same world vertex shader input, and a
   body with no discard and no clip that returns only a colour,
   `float4(envCube.Sample(sampler0, safeNormalize(worldPosition)).rgb, 1.0)`.
2. **The far-plane anchor** (§13.2), so the depth test has a value that means "background" instead of
   the box's own position in the scene.

Nothing else moved: the pass still runs inside the world pass, flushed at the same points (blended
batches, after the batch loop, before the placeholder quad); the depth state is unchanged
(`native/src/native_renderer.cpp:698-706`: test on, `DepthWriteMask = ZERO`, `LESS_EQUAL`); and the
entity and viewmodel passes are untouched. The change is entirely inside `drawWorldSkyBox` and the
shader pair it binds. The pass writes **no depth at all** - not before, not now - so nothing later can
compare against its pixels; the "does the far plane write depth" question is answered by the draw's own
pixel shader, which has one `SV_Target` output and no `SV_Depth`.

### 13.2 The far-plane anchor: what it does, and why it sits a hair inside

Engine parity (Q6, external evidence: engine source, `tf2-source-notes/NOTES.md` Q6):
`Shader_WorldEnd` runs `R_DrawSkyBox` **after** the opaque world and draws at `zFar`. The box here
becomes that far plane. In `native/src/native_renderer_frame.cpp:644-646`, after
`skyView * projection`:

```cpp
const float farPlanePin = 1.0f - 1.0e-6f;
for (int row = 0; row < 4; ++row)
  skyConstants.mvp.m[row][2] = skyConstants.mvp.m[row][3] * farPlanePin;
```

`XMFLOAT4X4` is row-major and the shader declares `row_major float4x4 mvp` and multiplies a row vector
(`mul(float4(position, 1.0), mvp)`), so output component *j* is `sum_i p_i * m[i][j]`: column 3 is
clip.w, and copying it into column 2 makes **clip.z == clip.w**, i.e. z_ndc = 1 for every vertex. The
**w column is untouched**, so 1/w and perspective-correct interpolation are unchanged - which is also
the structural refutation of the "the UV became screen-space affine" candidate in §13.3.

Four pins measured, three poses each (empty settings, sky on; diff against the round-2 frames):

| pin | up0 | up1800 | pr600 | verdict |
| --- | --- | --- | --- | --- |
| none (box at its 40-unit half-extent) | 0 px | 0 px | 0 px | reference row: these ARE the pre-round frames, so 0 is by construction; the sky sits at the box's depth, not at the far plane |
| exactly 1.0 | 0 px | 0 px | 4 px ±1 | geometry lies on the clip plane; the boundary is float rounding |
| `1 - 1e-6` | 2 px ±1 | 0 px | 2 px ±1 | **chosen** |
| `1 - 1e-3` | 2 px ±1 **+ 681 px of world painted over** | 0 px | 2 px ±1 | rejected |

The wider inset fails in the direction that matters: at 1e-3 the box sits deep enough inside the depth
range that distant world surfaces stop rejecting it, and in the up0 pose the sky painted over **681 px
of terrain**. The pin has to be as close to the far plane as float arithmetic allows. No pin at all is
measured at zero differences, but it leaves the sky at the box's own half-extent, so a world surface
farther than the box would win the depth test against the background - the far plane is the
semantically correct anchor, and it is what the trace now asserts (§13.4).

### 13.3 The bounded semantic correction: 4 px

Against the round-2 frames the final build differs at four pixels, each 1 LSB in one channel (BMP is
BGR; values below are RGB):

| pose | pixel | before (RGB) | after (RGB) | delta |
| --- | --- | --- | --- | --- |
| pl_upward tick 0 | (45,52) | 92,127,154 | 93,127,154 | R +1 |
| pl_upward tick 0 | (285,196) | 98,131,157 | 98,132,157 | G +1 |
| pl_upward tick 1800 | - | - | - | **none (0 px)** |
| cp_process tick 600 | (639,21) | 153,178,210 | 154,178,210 | R +1 |
| cp_process tick 600 | (652,67) | 136,156,190 | 136,157,190 | G +1 |

Four pixels of 3 x 1264 x 681 = 2,582,352 compared (1.5e-6), none at the middle pose.

**Attribution (the load-bearing evidence).** With `--capture-pass-depth` on the sky-on frame, the
per-pass depth planes read `depth=1.000000000 d24=16777215 far=yes` at all four pixels, at the
`10-sky` dump (inside the deferred sky draw, world depth already present) and at `30-world-done`
alike. No world geometry ever wrote depth at any of them, so no world pass can be responsible for the
difference: the only pass that ever painted those pixels is the sky. Artifacts
`.scratch/holes/skyfix-final/*-depth-*-10-sky.pgm` and `-30-world-done.pgm`.

**Candidates ruled out.**

- *Perspective-correct UV became screen-space affine.* Structurally impossible here: only matrix
  column 2 - the z output - is overwritten; column 3, clip.w, which is what 1/w interpolation depends
  on, is untouched. The shader's sample input is the pre-projection vertex attribute
  (`output.worldPosition = input.position`), so changing the *direction* would require the
  interpolation weights to change, which requires w to change - and a w change would move thousands of
  pixels, not four.
- *z = 1 depth tie at the same pixel centre.* Impossible at these pixels: the depth is the cleared 1.0
  (no world geometry) and the sky writes no depth, so there is no second writer to tie with.
- *Cube-face seam tie at the same pixel centre.* Not supported by the neighbourhood probe: three of the
  four pixels sit in a flat sky gradient, the fourth beside a face diagonal whose two sides differ by
  far more than 1 LSB (`neighbours_equal = 1/9` in all four 16x16 patches,
  `.scratch/holes/skyfix-farplane/diff-patches.png`). A face-winner flip would be a visible colour
  step, not ±1.

**What remains, as the measured invariant rather than a story.** The difference appears **iff the pin
is applied**; it is identical for `1 - 1e-6` and `1 - 1e-3` (a thousandfold z difference, same four
pixels, same values); it is identical with the dedicated shader and with the world shader bound (probe
build `.scratch/holes/skyfix-probe/`, bit-identical to the sky-shader frames); it is deterministic (two
captures of the same pose with the same binary are bit-identical); and it is 1 LSB in one channel on
pixels no world geometry owns. With the shader input and the interpolation weights provably unchanged,
the residue is a rasteriser-side rounding difference of at most one ulp in primitive setup, landing on
a trilinear mip-weight boundary at four of 2.6M pixels.

**Conclusion, in the wording the ruling asked for: a recorded bounded semantic correction - 4 px,
±1 LSB per channel, all of them sky-owned pixels - and not "no difference".** If pixel-exactness
against the pre-round frames is ever required, the lever is the pin itself (0 px measured, table row 1),
at the cost of the far-plane semantics.

### 13.4 The trace now carries the claim

The sky line prints two fields that were previously true only by construction
(`native/src/main.cpp:9830`):

```
gpu: world sky state=active vertices=36 triangles=12 faces=6 draws=1 side=512|1024 resampled=1 farplane=1 shader=dedicated reason=six sides, 36 vertices
```

`farplane` is set where the pin is applied, and `shader` reports which pixel shader the draw bound
(`dedicated` / `world` / `none`); both are reset each frame and copied into
`WorldMaterialSampleState` (`native/include/native_renderer.h:597-604`), so the trace cannot describe
something the draw did not do. Gate A gained two assertions on them, because a small box drawn first -
or the world shader rebound - looks identical in the pinned poses and would fail no pixel test.

`verify-all.sh` runs this gate as step 28 and prints `WORLD-SKY-GATE=PASS (13 assertions)`; a gate
that cannot run prints `SKIP <reason>` and exits 3, which the chain counts in its own skip counter.
### 13.5 Assertion C had to be re-derived

Round 2's C read: "`--no-world-sky` reproduces 2,072 enclosed clear px." That number belongs to a
*different* defect: P0 adds 2,072 geometry pixels at this pose and loses none (§13.7), so on a merged
tree the same pose with the sky off is no longer black and the assertion would go red on a correct
build. The ruling was explicit - do not re-pin another constant, and do not read P0 as having broken
W-1 - so C is now stated in a form that holds under any world geometry:

- capture the sky-on and sky-off frames **and** the sky-on per-pass depth plane;
- `diff` = pixels clear with the sky off and not clear with the sky on. The world draws identically in
  both runs, so a pixel that changed owner can only have been painted by the sky;
- **C2** `diff > 0` - the pass painted something (a flag that stopped switching anything off fails);
- **C3** every pixel of `diff` is far-plane in the sky-on depth plane - the sky painted only pixels the
  world does not own, never one of the world's;
- **C4** `enclosed_clear(off) - enclosed_clear(on) == |diff intersect enclosed(off)|` - the old
  population statement as an identity that cannot go stale.

The tool is `world-sky-gate-reconcile.py` (pure stdlib: BMP clear-colour mask, 4-connected enclosure,
16-bit PGM depth read). E changed for the same reason: its pixel assertion used the 2,072 population
too, and now asserts that the frame produced by `preset=1 skybox=0` is the **same picture** as
`--no-world-sky` (`frame_diff=0`, raw RGB comparison via `world-sky-gate-account.py --diff`), so "the
key lost" and "the flag works" cannot both be true.

Count: A 4, B 2, C 4, D 1, E 2 = **13 assertions**.

**General rule recorded here: a gate may not pin a number that belongs to another defect.** Two
correct fixes then cannot merge - one of them always has to re-pin, and whoever merges second inherits
a red tree that is nobody's bug.

### 13.6 The pass costs less than this box can resolve: alternating A/B

Metric and protocol are §12.5's: median fps of the `--metrics-file` rows with `tick >= 1000`, one
build, empty settings, `--world-water off`, tick 1800. What is different is that the two arms
**alternate** and the statistic is the **per-pair difference between neighbours** (sky-on minus
sky-off inside one pair). Drift in machine load then cancels row by row instead of being baked into two
independently computed medians.

Four batches were needed, and the three that are void are recorded because voiding them is the
protocol:

| batch | pairs | what happened | verdict |
| --- | --- | --- | --- |
| 1, `.scratch/holes/skyfix-perf/` | 7 | within one arm 22.4 - 57.0 fps (2.5x); both arms ~10 fps under the 45.115 baseline; sky-on read *faster* than sky-off | **void by load**: physically impossible for a pass that only adds work. The script probed `/proc/loadavg`, which is empty under MSYS, so it could not see the load. |
| 2, `.scratch/holes/skyfix-perf2/` | 1 of 6 | started 12:52, inside the lead's 12:40-12:58 capture window on the merge tree | **void by overlap**: killed mid-batch. |
| 3, `.scratch/holes/skyfix-perf3/` | 6 | deltas -10.07 .. +4.88 fps, arm medians 48.7 / 48.0 | **void by self-overlap** - see below. |
| 4, `.scratch/holes/skyfix-perf4/` | 6 | quiesced protocol | **headline** |

**The finding that made batches 1-3 unusable is worth recording on its own**: a run's process does not
end when the run ends. Its metrics CSV is complete, its exit code is returned, and 1-2 seconds later the
box still reads 30-50% CPU load; `Get-Process tf2_demo_native` still shows the previous run (and
sometimes the one before it) while the next run is starting. Batch 3's own log shows the mechanism:
`load_after=33` / `load_before=45` across a pair boundary, and `foreign_procs_before=2` and `4`.
So the pairs of batch 3 partly ran on top of each other's teardown, which is why its arms spread over
33.9-52.4 fps.

**The noise band is measured, not assumed.** A null control (`.scratch/holes/skyfix-null/`, six pairs,
**both** arms `--no-world-sky`, same protocol and statistic) gives, after one cold first pair:
per-pair differences `-1.007, -1.180, -1.257, +0.800, +0.977` fps, standard deviation **1.1 fps**,
median **-0.10 fps**, arm medians 56.2 / 56.8 fps, arm range 55.8-57.6 fps. (The cold pair reads
46.9/40.1 and is reported rather than dropped: it is the first run of the batch and its numbers say so.)
That is the bar an on/off effect has to clear on this box: **±1.3 fps**.

Batch 4 therefore waits, before every run, for **zero** `tf2_demo_native` processes and a CPU load of
at most 10% (up to two minutes of waiting, logged per run), so no pair can contain another run's
teardown:

| pair | order | sky-on | sky-off | on-off | ms |
| --- | --- | --- | --- | --- | --- |
| 1 | on, off | 59.651 fps / 16.76 ms | 54.119 fps / 18.48 ms | **+5.531** | -1.71 |
| 2 | off, on | 59.280 / 16.87 | 57.671 / 17.34 | **+1.608** | -0.47 |
| 3 | on, off | 58.386 / 17.13 | 54.703 / 18.28 | **+3.683** | -1.15 |
| 4 | off, on | 58.060 / 17.22 | 59.112 / 16.92 | **-1.052** | +0.31 |
| 5 | on, off | 58.569 / 17.07 | 58.440 / 17.11 | **+0.129** | -0.04 |
| 6 | off, on | 54.568 / 18.33 | 60.097 / 16.64 | **-5.529** | +1.69 |

Arm medians, batch 4: sky-on **58.478 fps** (54.568 - 59.651) / 17.10 ms; sky-off **58.056 fps**
(54.119 - 60.097) / 17.23 ms. Per-pair difference: median **+0.869 fps**, mean **+0.729 fps**,
standard deviation **3.542 fps**, standard error 1.446 fps. Baseline for reference: 45.115 fps / 22.17 ms.

The pairs also show an **order effect**, and it is the reason the table is read the way it is. In pairs
1, 3 and 5 the sky-on run came first and the difference averages **+3.115 fps**; in pairs 2, 4 and 6 the
sky-off run came first and the difference averages **-1.657 fps**. So the second run of a pair is
systematically the slower one by about **2.4 fps** (thermal or teardown drift between neighbours), and
alternating the order is what cancels it: the order-balanced estimate of the sky pass's cost is
**(+3.115 - 1.657) / 2 = +0.73 fps**, i.e. indistinguishable from zero, and the sign of the raw
difference flips exactly with the order. A real cost of the kind §12.5 measured would have kept one sign
in all six pairs.

The arms are also what they claim to be, from their own traces: every sky-on run prints
`state=active ... farplane=1 shader=dedicated reason=six sides, 36 vertices`, every sky-off run prints
`state=unavailable ... draws=0 farplane=0 shader=none reason=skybox draw disabled by the caller`.

**Conclusion.** On this build the deferred, far-plane-pinned, discard-free sky pass **costs nothing the
protocol can resolve**. The paired difference is +0.7 fps order-balanced, the null control puts two
identical arms within ±1.3 fps of each other, and the per-pair scatter here (sd 3.5 fps) is the order
effect, not the pass. The mechanism predicts exactly this: with the sky drawn after the opaque world and
pinned to the far plane, early-z rejects every sky fragment whose pixel the world already wrote, so the
pass only shades the pixels the sky is actually visible in - and in these three poses the map covers the
large majority of the screen. What §12.5 recorded (-6.28 fps) was the world shader's `discard` defeating
early-z, and that is gone.

One caveat about absolute numbers, because it matters for anyone reading the table out of context: both
arms of batch 4 read ~58 fps, i.e. ~13 fps above the 45.115 fps baseline, and the null control on the
same day read 56.2-57.6 fps with the sky switched off. That is the state of the box (and of the branch,
which now also carries the 86f2fc6f diagnostics) rather than anything round 3 did - so **only the
within-batch paired difference is used above, never an arm-against-baseline subtraction**.

### 13.7 What P0 changes about the hole attribution

P0 is the separate fix (branch `work/disp-param`, commits `6bdbf421` + `d177282d`/`93affd2e`) for the
transposed displacement parameterisation: this loader walks a displacement grid with
`k = y*size + x` (+y along `quad[0]`->`quad[3]`, +x along `quad[0]`->`quad[1]`,
`native/src/bsp_map.cpp:1227-1243`) while the engine walks `k = i*size + j` (+i along
`polygon[0]`->`polygon[1]`, +j along `polygon[0]`->`polygon[3]`). The transposition makes adjacent
displacement faces not share boundary vertices: on the nine-map set only **3,220 of 7,855** seam edges
met within 0.01 map units before P0, and **7,502 of 7,855** after it (engine oracle 7,535); face 13233's
maximum vertex deviation falls from 341.53 to 0.0055.

Frame readings on P0's own branch, same captures as this card:

| pose | enclosed clear before P0 (sky off) | after P0 |
| --- | --- | --- |
| pl_upward tick 1800 | 2,072 | **0** (2,072 geometry px added, 0 lost) |
| pl_upward tick 0 | 8,976 | 3,262 |
| cp_process tick 600 | 985 | 453 |
| koth_product_final tick 0 | 9,453 | **0** |

Three consequences for this card's record:

1. **The "known residual 2 px" is withdrawn** (§12.4): on the P0 tree those two pixels are world
   geometry (RGB 54,54,54 / 68,70,51). The shape diagnosis - a two-pixel hairline is a seam - was
   right; the seam was the terrain's, not the sky cage's.
2. **The up1800 population was a terrain gap**, the sky pass was painting behind it. Two defects landed
   on the same pixels; §12.4's correction states what is withdrawn and what stands (a fresh install drew
   no sky at all - that defect was real and is fixed here).
3. **The three-way classification of a non-black hole collapses on the merged tree.** Before P0 it was:
   (i) black/clear colour = the sky never drew; (ii) sky colour with depth < far = world geometry culled
   or wound wrong; (iii) sky colour at depth = far and enclosed by world = culled *or* displacement seam,
   separable only by face identity. After P0 the seam class is gone, so (iii) again means culling. Until
   the merge, on this branch, (iii) is ambiguous exactly as stated - the discriminator is
   `--world-face-id`: a seam follows displacement face edges and reports two adjacent displacement face
   ids, a cull hole is whole triangles of one face.

Not fixed, recorded as a consistency gap: the engine triangulates a displacement face as a recursive
**fan** (checkerboard diagonals) while this loader uses a uniform diagonal
(`quad[i] = polygon[(corner+i)%4]` plus fixed `p00,p11,p10` / `p00,p01,p11`). Recomputed on face 13233:
16 of 32 undirected triangles coincide, 16 cells differ in diagonal direction - and both sides are
32/32 clockwise and 32/32 outward, so the fan cannot be the hole cause (the vertices were in the wrong
places, not the diagonals). If it is ever fixed it is a change to the displacement emission loop only,
and it moves vertices: every pinned frame and per-face census on this card would need re-pinning. The
"13233 culled from below is legitimate" conclusion (§10.6, with Q5 engine backing that displacements are
effectively one-sided) stands, but per-face facing tables measured here must be labelled *measured under
the transposed parameterisation* and recomputed after the merge.

### 13.8 `--capture-pass-depth`: the pass list is a counter, not an index space

The new sky dump point is the eleventh plane on a sky-on capture
(`native/src/native_renderer_capture.cpp:97-156`): the file name is
`<prefix>-<index>-<name>.pgm` and **`index` is a running counter over the dumps that actually
executed**, incremented on each one. A pass that does not run therefore shifts every later index: a
sky-off capture has no `10-sky` plane at all, and its subsequent planes sit one index lower. Every dump
also prints `pass-depth: index=N name=... depthStateLastBind=...` on stdout, so the mapping for a given
run is in that run's log.

Consequences, checked before relying on it:

- **No script or tool in the tree selects a plane by index** (searched `.sh`/`.py` for `pass-depth`
  and for `-<digit><digit>-` filename patterns): there was nothing to break when the sky point was
  inserted.
- Anything written from now on must select **by name suffix** (`*-10-sky.pgm`), never by index. The new
  gate does exactly that, with `*-30-world-done.pgm` as the fallback.
- The plane is the full target size, P5 with 16-bit samples, `depth = value / 65535`, so
  `d24 = round(depth * 16777215)`. `10-sky` is dumped *inside* the deferred sky draw, i.e. after the
  world has drawn, which is what makes "far here" mean "no world geometry owns this pixel" - and is why
  it cannot exist in a sky-off capture.

### 13.9 This round in numbers, and what it did not change

Measurements taken, so a later reader can tell what stands behind §13 (all poses `pl_upward` tick 1800
unless named, empty settings file, sky on):

- **Pin sweep**: four pins x three poses = 12 frames, compared against the round-2 reference set
  (`.scratch/holes/skyfix/`); the "none" row *is* that set. Two `--capture-pass-depth` runs (up0 and
  pr600, 11 planes each) for the attribution; three frames from the probe build that binds the world
  pixel shader with the pin kept.
- **Gate**: the reworked 13-assertion gate, five captures (two of them carrying pass-depth planes),
  13 passed / 0 failed on the final build (`.scratch/holes/gate-final2`). The gate's own INFO line
  reads `preset-0 steady fps=46.9326 (21.31 ms)` for the sky-on arm on a quiet box: it is a single
  capture, not the A/B headline (§13.6), and it is *above* the 45.115 baseline recorded in §12.5 -
  which is the clearest sign that the baseline and today's arms are not comparable across days.
- **Perf**: three alternating batches - 7 pairs (void: load), 2 pairs (void: the lead's capture window
  on the merge tree), 6 pairs (headline, §13.6) - plus a six-pair **null control** that runs the same
  statistic with no treatment difference, to measure the box's paired-difference noise instead of
  assuming it.
- **Builds**: five after round 2 (exact pin; `1 - 1e-3` pin; `1 - 1e-6` pin; probe binding the world
  shader; final, which adds the trace fields).

What this round did **not** change:

- §10's culling conclusions - the cull rule, the winding rule, and "face 13233 culled from below is
  legitimate" - are untouched in code and re-confirmed only indirectly. After P0 they have to be
  re-derived per face, and until then they are labelled as measured under the transposed
  parameterisation (§13.7).
- The hard hole condition: three poses at `enclosed_clear = 0` on the final build, same as round 2.
- The world pass's own shading, the entity and viewmodel passes, the UI and the settings contract:
  untouched. No new convar, no new UI toggle; `skybox=0` still wins (gate E).
- The displacement parameterisation and the fan/diagonal consistency gap: not this card (§13.7).

## 14. The gate has to be run on the merge tree: branch-green is not merge-green

### 14.1 What happened at the merge

`work/integrate` (HEAD `aff39957`: candidate/e5 + the material card + P0's displacement fix + the four
commits of this card + the map-overview card `09fc824c`) was built at 14:05 and the gate was run on that
build. Twelve of thirteen assertions passed; C failed:

```
FAIL C: sky-on and sky-off differ by 0 px (expected > 0)
```

Nothing in the renderer changed between the two runs. The cause is the **pose**. C asserted on
`pl_upward tick 1800`, the user-view pose, because on the branch that pose had sky-visible pixels to
count: the 2,072 px population of §9 lived there. P0 fills the cracks that population came from, and on
the merge tree the world covers that pose completely. Four numbers say it, one line apart:

| pose / tree | diff | far | enclosed off / on / filled |
| --- | --- | --- | --- |
| tick 1800, branch (`2957a6a0`) | 2,072 | 2,072 | 2,072 / 0 / 2,072 |
| tick 1800, merge (`aff39957`) | **0** | **0** | **0 / 0 / 0** |
| tick 0, merge | 77,131 | 77,131 | 6,814 / 0 / 6,814 |

`sky-on == sky-off` on the tick-1800 pose is the **correct** reading after P0: there is no sky in view, so
drawing it changes no pixel. The assertion was not wrong about the sky; it was wrong about which pixels
existed to be sky.

### 14.2 The mistake in the assertion, twice

§13.5 already recorded the first version of this mistake: C pinned the number `2,072`. When P0 made that
number 0, a correct merge would have turned the gate red. The rework at that time replaced the constant
with an account - and then kept `diff > 0` on the same pose, which is the same assumption in a thinner
disguise: **"this pose has sky pixels" was still an assertion about the state of another defect.** It
survived one merge because the number stayed 2,072 on the branch, and it was going to break at the first
merge that fixed the cracks. The lesson generalises:

> A gate may not assert a property that belongs to a defect it does not own. When an assertion is about
> a population, it also needs a pose where that population is non-zero, or it silently degenerates into
> `0 == 0`.

Both W-1 gate failures on the way to this merge were instances of that one rule (the first one pinned the
number, the second one thresholded it).

### 14.3 What C asserts now, and what C+ adds

`world-sky-gate-reconcile.py` now prints a fourth count next to `diff` and `far`:

- `owned` = pixels that are **clear in the sky-off frame AND world-free at the far plane** in the sky-on
  pass-depth plane: everything the sky pass is *entitled* to paint, independent of what it did.
- **completeness:** `diff == owned` - the sky left no entitled pixel unpainted;
- **soundness:** `far == diff` - the sky painted nothing the world owns;
- **account:** `enclosed(off) - enclosed(on) == enclosed_filled`.

All three hold at 0 when the pose shows no sky, which is exactly what makes them merge-safe. C is these
three plus the "no-world-sky draws nothing" trace assertion; **C is deliberately allowed to be 0.**

C+ then runs the same three statements on **tick 0 of the same demo**, a second pose whose world does not
cover the frame (spawn, open sky): `diff > 0` is asserted **there**, together with completeness, soundness
and the enclosed account. On the merge tree that pose paints **77,131 px** and answers
`6814 - 0 == 6814` for the enclosed account, so the account is exercised on non-trivial numbers rather
than on zeros. C+ is what stops "the sky stopped drawing entirely" from being a green gate: if the pass
disappears, C reads 0/0/0 and passes, C+ reads 0 and fails.

Two assertions, one rule: C is allowed to be vacuous on a covered pose **because** C+ proves the gate is
not vacuous in general.

### 14.4 A second defect this merge exposed: the field parser

The first run of the reworked gate on the merge tree read `diff=6814` on the tick-0 pose, while the same
three files re-measured by hand read `diff=77131`. The cause was `field()`, the helper that pulls a number
out of the tools' one-line output: it used a greedy `sed`, so asking for `diff` matched
`enclosed_diff=6814` (the line contains both keys and `enclosed_diff` comes last). On every pose the gate
had used so far the two numbers happened to be equal - which is exactly why nobody noticed - and on the
first pose where they differ the parser returned the wrong field. It is now an exact-key scan over
whitespace-separated `key=value` tokens.

The near-miss matters beyond the one bug: a green gate whose two independent numbers are numerically equal
on every pose it tests has not tested that they are independent.

### 14.5 Readings on the merge tree

`work/integrate` @ `aff39957`, binary `native/build-nmake/tf2_demo_native.exe` built 14:05
(no source change in this commit), output `.scratch/w1-fresh` (six fresh captures, no `--reuse`):

```
== W-1 sky gate: preset-0 isolate, pose pl_upward tick 1800 ==
PASS A: preset 0 (empty settings file) draws the sky (state=active)
PASS A: the sky pass recorded draws=1
PASS A: the sky box is pinned to the far plane (farplane=1)
PASS A: the sky pass uses the dedicated discard-free shader (shader=dedicated)
PASS B: enclosed clear px == 0 (the hard condition; the old 2,072 pin now belongs to P0)
PASS B: no clear-colour pixel remains in the frame
PASS C: --no-world-sky records state=unavailable draws=0
PASS C: the sky painted every px it owns on the tick 1800 pose (0 painted / 0 owned; a fully covered pose legally reads 0)
PASS C: every px the sky painted is world-free at the far plane (0/0)
PASS C: enclosed(off) - enclosed(on) == enclosed px the sky filled (0 - 0 == 0)
PASS C+: on the tick 0 pose the sky paints 77131 px the world does not (diff > 0: the gate can still see the pass)
PASS C+: the sky painted every px it owns on the tick 0 pose (77131 painted / 77131 owned)
PASS C+: every px the sky painted there is world-free at the far plane (77131/77131)
PASS C+: enclosed(off) - enclosed(on) == enclosed px the sky filled (6814 - 0 == 6814)
PASS D: --world-depth draws no sky (state=unavailable draws=0)
PASS E: preset=1 with skybox=0 draws no sky (the explicit key wins)
PASS E: the explicit-key frame is the same picture as --no-world-sky (frame_diff=0)
INFO A: preset-0 steady fps=56.8004 (frame time 17.61 ms)
W-1 SKY GATE: 17 passed, 0 failed
```

P0 does not move the hard condition (B: `enclosed_clear = 0` still holds on the merge tree), and it does
not move A, D or E: the pass is still drawn, still pinned to the far plane with the dedicated shader, still
absent from the depth view, and the explicit key still produces exactly the `--no-world-sky` picture.

### 14.6 The count, and the rule it now follows

`13 -> 17` on this commit: A x4 + B x2 + C x4 (the trace assertion plus the three account statements on
the default pose) + C+ x4 + D + E x2. The comment in `verify-all.sh` carries the arithmetic and the count
history (9 -> 11 -> 13 -> 17).

The rule this section exists to leave behind:

1. **Run the gate on the merge tree before calling the merge safe.** A branch-green gate is evidence about
   the branch. The merge tree is a *different world geometry*, and any assertion that counts pixels is a
   statement about that world.
2. **When a gate must survive other people's fixes, assert accounts, not populations** - equalities whose
   truth does not depend on how much of the screen a hole happens to cover.
3. **Pair every account with a sensitivity pose.** If the account can be satisfied by zeros, one pose where
   the quantity is non-zero must be asserted too, or the gate can go green with the feature switched off.
4. **Re-run the gate after merging, even if nothing you own changed.** The one merge in this card's history
   that did not touch the renderer is the one that turned the gate red - twice, for two different reasons.






