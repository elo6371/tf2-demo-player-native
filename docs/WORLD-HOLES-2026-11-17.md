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

Order and depth: the box is drawn **first, before the world**, under its own rasteriser state
(`native_renderer_frame.cpp:518-525`), and it writes **no depth**
(`native_renderer.cpp:697-698`, `skyDepthDescription.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO`).
It is not a "run after the world, paint only where depth is still 1.0" pass, and there is no CPU
occlusion/mask buffer that decides sky visibility from world faces. When the pass is off, nothing
covers the pixels the world leaves empty, and they stay clear colour.

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

### 12.3 The gate: four assertions, all pass

Captured on the pinned user-view pose (pl_upward tick 1800) with an **empty** settings file, i.e. the
fresh-install configuration:

| # | assertion | measured |
| --- | --- | --- |
| A | preset 0 draws the sky | `gpu: world sky state=active ... draws=1` - PASS (both assertions) |
| B | enclosed clear px == 0, and no clear-colour pixel at all | `clear_total=0 enclosed_clear=0` - PASS |
| C | negative control: `--no-world-sky` reproduces the old frame | `enclosed_clear=2072`, `state=unavailable draws=0` - PASS |
| D | the depth view still draws no sky | `state=unavailable draws=0` - PASS |
| E | an explicit key still wins: `preset=1` **and** `skybox=0` must not draw the sky | `state=unavailable draws=0`, `enclosed_clear=2072` - PASS (both assertions) |

`W-1 SKY GATE: 9 passed, 0 failed` (A x2, B x2, C x2, D, E x2). E exists because the precedence has
two directions: before W-1 `preset=1` switched the sky **on** by itself, so E proves the preset stopped
deciding it either way and that a user who writes `skybox=0` by hand is still obeyed. C is the control that makes B mean something: the same pose, same
camera, same demo, one flag apart, still yields exactly the 2,072 px the card spent its first half
attributing. The fix, not the measurement, is what moved them.

### 12.4 Closing measurement: the three characterised poses after the fix

Every capture below uses an empty settings file (fresh install), so the sky is being asked for the
default way:

| pose | before (sky off, preset 0) | after (sky on, default) | trace |
| --- | --- | --- | --- |
| pl_upward tick 0 | 8,976 enclosed clear px | **0** (clear_total 0) | `state=active ... side=512 draws=1` |
| pl_upward tick 1800 | 2,072 enclosed clear px | **0** (clear_total 0) | `state=active ... side=512 draws=1` |
| cp_process tick 600 | 985 enclosed clear px | **0** (clear_total 0) | `state=active ... side=1024 draws=1` |

There is no residual. The lead's instruction was: if a residual remains after the sky fix, that is a
real geometry remainder and gets a new round; if not, the answer to the user's "the floor and stairs
are missing faces" is this section. The pixels were never missing geometry that the renderer dropped -
they were pixels the renderer left unpainted while the sky pass was switched off by a preset.

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

**Known residual (decided: recorded, not chased): 2 px, and only on koth_product_final.** They are
two adjacent pixels at (997,125) and (998,125), i.e. 0.0002% of the frame, and they are not on any
pose this card has characterised. Two adjacent pixels is the shape of a seam between two faces of the
sky cage (the box is six quads, so a hairline where two of them meet is the ordinary way this fails)
and not the shape of a culled face, which would be a block. **Criterion for the next reader:** if
anyone later reports thin-line light leaks in the minimap or in spectator views, check this residual
first - same shape, same cause candidate - before opening a world-geometry round.

### 12.5 Cost, measured and recorded

Same demo, same pose, same preset 0, median of the steady-state rows of `--metrics-file` (12 rows
with `tick >= 1000`):

| build | median fps | frame time |
| --- | --- | --- |
| before (sky off) | 45.115 | 22.17 ms |
| after (sky on) | 38.835 | 25.75 ms |

delta **-6.28 fps / +3.59 ms per frame (-13.9% fps)** on this host. The pass is twelve triangles, so
this is not geometry cost: it is the textured box covering the frame area the world leaves empty,
which is exactly the work that was being skipped. It is recorded rather than hidden because it is the
price of correctness; it is also the number to watch if the sky box ever gets a cheaper path (a
full-screen gradient or a cubemap fetch per empty pixel would be the alternatives, not this card's
scope).

### 12.6 What this changes about the earlier numbers

Everything measured before this section - the 8,976, the 2,072, the 985, the per-component
attribution, the no-cull ceiling of 5,501 px - was captured with `preset=0` and therefore **with the
sky pass off**. Their correct reading is "pixels that should have been sky" and not "geometry the
world lost". The geometry findings are unaffected: the culling is correct (§10.6), the cull rule and
the winding rule do not touch the population (§10.2/§10.5), and none of that needed to change to fix
what the user sees.

### 12.7 Left open, stated rather than hidden

- **No UI toggle** writes `skybox` yet; a user who wants the old frame edits the key by hand.
- **verify-all.sh is not wired to the new gate.** `world-sky-gate-check.sh` is standalone and costs
  four captures; the release chain's geometry step still runs only the hole gate. Registering it is a
  chain change (the geometry step pins its own forward assertion count, 53) and is left to the lead.
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


