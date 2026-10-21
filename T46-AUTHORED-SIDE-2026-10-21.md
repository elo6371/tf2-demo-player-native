# T46 — the authored side of the drawn pixel population (2026-10-21)

Task: T46, "resolve T41's authored-side pixel population". Branch
`task/t46-authored-side` off `6888e6d`. Worktree
`D:\TF2_Native_Worktrees\t46-authored-side`.

Status: the instrument, the gate pins and mutation m4 are written and measured
against the committed T36 frame.

* Forward T36 gate PASS, run in this worktree: `bash lighting-dark-account-check.sh
  --reuse` exits 0, every pin holds, including the 43 new 3d assertions (§6). A
  `--reuse` run copies the committed frames and launches neither the product
  nor a build, so it needed no host lock and no host-lock slot.
* Mutation m4 verified by running the gate's own m4 block, extracted verbatim,
  against the real tree (§5): it turns the ledger red with the exact swap and
  `raster-rule.py` comes back byte-identical (`RESTORE=OK`).
* The full `--mutation` suite (m1-m4) has NOT been run here: m1 and m3 need a
  product build, another task holds the shared host lock, and the Lead has not
  sent "priority clear". That is the remaining step, not a result.

The readings below are the tool's own, reproducible with the command in §10.

## 1. What T41 left open

T41's raster-rule.py derives a front/back rule from the three things that decide
it (the raster state, the winding the product emits, the eye the trace used) and
then tests the face-id frame against it. At the T43 pose it finds 51,890 id
pixels sitting on faces the derived rule calls BACK. T41 could not say what
those pixels are: its face-level rule hands a displacement face one normal (the
face's outward) while the product hands the rasterizer the grid's triangles,
each flipped onto that outward, so a "back" displacement face can still ship
front triangles. T41 reported the residual, the two product-trace exceptions and
the 163 over-bound id regions as bounds and moved on.

T46 answers it with a second instrument rather than a better label, and it
separates the question T41 conflated:

* which side the MAP authored a surface on — the face-level rule, unchanged;
* which TRIANGLE the RASTERIZER painted a given pixel with — the new ledger.

## 2. What was added

All in `raster-rule.py` (additive; every T41 key and reading is untouched, so
T41's evidence stays comparable):

* `emit_world` recreates `native/src/bsp_map.cpp:314-843`'s emission triangle
  for triangle, in `map.triangles` order, from the lumps: the same 99,312
  triangles the product's own probe reports. It reads `dplane_t` and
  `CDispVert` at their real 20-byte strides (5 floats each; reading either as
  4 floats was the bug that made the first version of this rule count 80,082
  triangles instead of 99,312).
* `triangle_sides` classifies each TRIANGLE by `dot(cross(b-a,c-a), eye-a)`,
  which is the raster's winding test and equals the product's own `area > 0`
  oracle (`main.cpp:5255`); near-plane clipping preserves the sign.
* `build_camera` + `project_map_points` rebuild the product's transform
  (LH look-at with the basis in COLUMNS, 90° fovY, aspect 1264/681) and are
  reconciled against the product's own 28-point face trace BEFORE they are
  used (25 hits project onto the traced pixel, 25 reproduce the traced depth,
  24 carry the emitted face's id, max depth delta 4.85e-05).
* `cover_pixels` casts one ray per face-id pixel through the face's emitted
  triangles — rays, not a screen-space test, because a third of the map's
  triangles straddle the near plane, where an unclipped projection mirrors
  vertices across the screen (face 17605 alone has 35 of its 96 vertices
  behind the camera). Exact coverage and coverage within half a pixel are
  kept apart everywhere.

The gate pins the ledger in section 3d and mutation m4 flips one side
classification (§6).

## 3. The readings (eye -2294.8, 348.8, 383.4; 1264x681; tick 2000)

### 3.1 Emission — what the rasterizer receives

| reading | value |
| --- | --- |
| world triangles | 99,312 |
| brush-fan triangles | 52,400 |
| displacement-grid triangles | 46,912 |
| displacement faces that emitted a grid | 1,382 |
| displacement faces that fell back to the quad fan | 10 |
| water triangles | 300 |
| triangles flipped onto the face's outward | 8,514 |
| distinct faces emitted | 17,321 |

The 10 quad-fan fallbacks are faces 17034, 17817, 17818, 17819, 17821, 18038,
18039, 18043, 18044, 18045 — the product's `usable()` rejects one of their
cells (area or edge length) and rolls the whole face back, exactly as
`bsp_map.cpp` does.

### 3.2 Per-triangle side

| reading | value |
| --- | --- |
| displacement triangles facing the eye | 24,058 |
| displacement triangles facing away | 22,874 |
| brush triangles facing the eye | 24,383 |
| brush triangles facing away | 23,218 |
| degenerate emitted triangles (dot = 0) | 4,779 |
| displacement faces shipping BOTH sides | 343 |
| derived-BACK displacement faces that still ship FRONT triangles | 126 |

That last pair is the measured form of T41's "conservative on displacement":
126 faces the face-level rule calls back are still represented on screen by
triangles whose own winding faces the eye.

### 3.3 The product's own trace, reconciled

25 points, 25 hits, 25 covered by the traced triangle, 25 depth matches, 24 id
matches. The one id mismatch is (720,390): the trace names tri 27780 (face
8555) and reports `depthMatch=0` at that very point — the triangle it names
was culled there and the pixel shows the companion model. The two exceptions
T41 named, (930,390) and (1140,390), are both front grid triangles of face
17605 under this rule, which is why the face-level rule called them
exceptions and the per-triangle rule does not.

### 3.4 The id frame's populations

| population | pixels |
| --- | --- |
| in-range id pixels | 750,391 |
| ViewModel overwrites (id bytes == textured bytes, no geometry of the decoded face covers them) | 512 |
| — of those, on derived-back faces | 266 |
| — of those, covered by their own face's geometry | 0 |
| world pixels | 749,879 |

The ViewModel pass runs after the world with depth cleared and alpha blending
(`native_renderer.cpp:4968-4971`, `1139-1150`) and writes the weapon's
COLOUR, not a face id. An opaque weapon pixel therefore carries its own colour
in the id frame, and its dark channels can decode as an in-range face id. The
discriminator used here is byte equality with the textured frame at the same
pixel plus zero coverage by the decoded face's geometry — not a colour guess.

### 3.5 The residual, resolved

```
51,890 id pixels on derived-back faces
  266  ViewModel overwrites (3.4)                      not world pixels
51,624 world pixels on derived-back faces
  51,620  disp/side-0      51,356 exact front + 64 sub-pixel + 49 back-only + 30 back-only sub + 121 none
       2  disp/side-1           1 exact front +  1 sub-pixel
       2  brush/side-0          2 back-only within half a pixel (zero-area fan sliver of face 15334)
```

So T41's arithmetic closes exactly: 51,890 = 51,620 + 2 + 268, where the 268
"brush" pixels are 266 ViewModel overwrites plus 2 zero-area fan slivers.
T41's own stated split (51,620 + 2 + 334) did not add up to its own total; the
measured split does. Of the 51,624 world pixels, 51,357 sit exactly on a
triangle whose own side is FRONT and 65 more are within half a pixel of one
(99.61%).

### 3.6 The named face, 17605

Face 17605 is the disp/side-0 face T41 named: plane 196 = [0,0,1] at distance
384, side 0, dispIndex 945, power 2, a 4x4 grid (5x5 vertices, 32 triangles).
The eye sits at z = 383.4, i.e. 0.600 units BELOW its base plane and
horizontally inside its footprint: the camera is inside the displaced surface,
and the grid's silhouette passes through it.

| reading | value |
| --- | --- |
| pixels carrying its id | 51,618 |
| emitted triangles | 32 (17 front, 15 back) |
| pixels exactly on a front triangle | 51,354 |
| pixels within half a pixel of a front triangle | 64 |
| front triangles that carry pixels | 12 |
| plane-side distance | -0.600 |
| face-level rule | BACK |
| trace exceptions (930,390) and (1140,390) | both frontExact = true |

The 12 front triangles and their pixel counts: 83285:6858, 83286:4930,
83288:9869, 83289:4092, 83290:6230, 83291:7452, 83292:2354, 83293:5829,
83280:819, 83282:31, 83283:1431, 83284:1459.

### 3.7 The cull, measured with the same instrument

The `ids` and `ids-nocull` frames are one build and one pose, so "removed by
the cull" is `in-range in nocull, off-range in culled`: 81,141 pixels,
19 faces.

| reading | value |
| --- | --- |
| removed pixels | 81,141 |
| covered only by back triangles | 81,089 exact + 34 sub-pixel |
| covered by a front triangle | 14 exact + 4 sub-pixel (all face 17605) |
| no triangle at all | 0 |

81,123 of 81,141 (99.98%) are removed because a BACK triangle covered them —
the cull doing what the rule says. The 18 front-covered ones are the grazing
family: the ray hits a triangle whose 3-D dot sign is front while the raster's
float32 screen area treated it as back (the culled frame shows sky there), all
on face 17605's silhouette where the eye is inside the grid. Reported as a
bound, not as a pass.

## 4. The bound

Everything not explained by a front triangle on derived-back faces is 202
pixels: 49 back-only + 32 back-only within half a pixel + 121 with no triangle
at all (0.39% of 51,624). All of them are the edge-on silhouette of a surface
the camera is inside; the 121 are pixels whose id decodes to face 17605 but
whose ray misses every emitted triangle within the sampled grid resolution.
This is bounded and named, not explained away, and the gate pins the 202 with
`assert_le`.

## 5. Mutation m4 — the ledger must observe the side it is given

Forward anchor in the tool: `sides_used = sides`. m4 replaces it with

```python
sides_used = np.where((tri_kind == 1) & (tri_sidev == 0), -sides, sides)  # mutation m4
```

i.e. every disp/side-0 triangle's classification is inverted (38,548 of the
99,312 triangles; face 17605 among them), and the tool is re-run on the SAME
forward frame with no rebuild — so the only thing that changes is the side the
ledger believes each triangle has. Measured, unmutated -> mutated:

| reading | forward | mutated |
| --- | --- | --- |
| face 17605 pixels exactly on a "front" triangle | 51,354 | 51,413 |
| face 17605 pixels only on back triangles | 49 | 0 |
| face 17605 triangles called front | 17 | 15 |
| displacement front triangles | 24,058 | 22,958 |

The gate requires the exact swap (and fails if the ledger does not move, or
moves but not by the swap the flip implies — a crashed tool is reported as
MUTATION-BROKEN, not red). A global percentage could not tell 51,354 from
51,413 or 49 from 0, which is why the gate pins counts.

Verified: the gate's m4 block, extracted verbatim from
`lighting-dark-account-check.sh` and run against the real tree with the real
helpers (`patch_in`, `restore_source`), printed `RED m4 as required: face-17605
front pixels 51354 -> 51413, back-only 49 -> 0, front triangles 17 -> 15,
displacement front triangles 24058 -> 22958` and then `RESTORE=OK (m4 source
byte-identical)`, with `git hash-object raster-rule.py` equal before and after
(da6daa7…). So the wiring, not only the arithmetic, is exercised.

## 6. Gate changes

`lighting-dark-account-check.sh`:

* new section 3d pins 43 exact counts from §3 (emission, sides, trace,
  populations, the named face, the removed ledger) plus the 202 bound;
* the T41 residual comment and the forward verdict now say what the residual
  is and keep T36's plane-side pixel split explicitly SUPERSEDED / NOT VERIFIED
  (that reading was taken at the vis-line eye, not the capture pose);
* mutation mode gains m4 (python-only, same frame, exact swap, restore_source
  and blob-hash check like m1-m3) and the mutation verdict says four
  mutations;
* `evidence/t36-lighting-dark-account/raster-rule.json|txt` regenerated by the
  same run (new keys additive, T41 keys unchanged).

Gate command (when the lock and the Lead allow it):

```
bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run t46 \
  bash /d/TF2_Native_Worktrees/_shared/t46/t46-t36-gate.sh
```

where that script runs `bash lighting-dark-account-check.sh --mutation --reuse`
from this worktree. Cost: the forward ledger is ~9 s twice (forward + m4) on
top of the gate's existing work; m4 needs no build.

What has run here, and what has not:

* `bash lighting-dark-account-check.sh --reuse` — exit 0, forward PASS. It
  copied the committed frames, so it ran no build and launched no product.
  Every new 3d pin printed OK, including the 202-pixel bound and the named
  face, and the forward verdict printed the T46 resolution text.
* the extracted m4 block (§5) — RED as required, RESTORE=OK, tree clean.
* `bash lighting-dark-account-check.sh --mutation --reuse` — NOT run: its m1
  and m3 rebuild the product and recapture, which needs the shared host lock
  and a build directory that is not present in this worktree yet.

## 7. What this does NOT claim

* It is not a full gate PASS. The forward gate passes in this worktree, but
  the mutation suite (m1-m4) has not run; see §status and §6.
* The per-triangle rule is a measurement, not a proof that the raster is
  exactly modelable: 202 pixels are bounded, 18 removed pixels disagree on a
  grazing edge, and the 2 zero-area brush-fan pixels are drawn by a float32
  screen-area test a 3-D ray cannot reproduce.
* T36's per-face plane-side split of the removed pixels stays NOT VERIFIED; the
  new removed reading is per emitted triangle and is reported separately.
* The 163 over-bound id regions remain unexplained and bounded, as T41 left
  them.

## 8. Determinism and repetition

Two consecutive runs over the committed frames produce byte-identical JSON
(compared, not assumed). The tool reads only the committed BSP, the committed
capture frames and their trace.

## 9. Merge risk

* Files: `raster-rule.py`, `lighting-dark-account-check.sh`,
  `evidence/t36-lighting-dark-account/raster-rule.json|txt`. No native source,
  no build input, no other task's evidence.
* The T41 keys in the JSON are untouched, so anything reading them (the T36
  gate's 3c pins) is unaffected; the gate's forward run regenerates the same
  numbers from the same frames.
* The gate gains ~18 s and one python-only mutation; it needs `numpy` (already
  a T41 dependency) and `python` on PATH.
* Interaction with T49's chain: this gate is the T36 gate, which T49 runs as
  part of verify-all; the new pins only add assertions to the same script.

## 10. Reproduce

```
cd D:\TF2_Native_Worktrees\t46-authored-side
python raster-rule.py \
  --bsp "D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/maps/pl_odyssey.bsp" \
  --frames evidence/t36-lighting-dark-account/captures \
  --trace evidence/t36-lighting-dark-account/captures/ids.trace \
  --json evidence/t36-lighting-dark-account/raster-rule.json
```

The tool prints its readings and writes the JSON the gate pins; exit 0 means
every reading was produced.
