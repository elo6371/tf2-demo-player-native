# T88 -- the world triangle budget and the func_illusionary whitelist

Scope: two defects in the map axis that the T85 audit found and did not fix.
Product tree: D:/TF2_Native_Worktrees/t88-maps, branch task/t88-maps, cut from
e15577b4f0550870bb11672a04497c038c9d88b2 (integration/t79-final).
Every number below was produced by a command in this tree; the command is printed
next to it so it can be run again.

## 0. One page

**Defect 1 -- the triangle budget truncated silently.** native/src/bsp_map.cpp
took a triangle limit (default 200000 in native/src/main.cpp) and stopped emitting
world triangles when it was reached. Nothing counted the refusal, nothing printed
it, and the first refused face was not named. cp_snakewater_final1 needs
**242,361** triangles (measured, below), so the shipped binary drew **200,000** of
them and silently lost 42,361 -- 17.5% of its triangles, 3,513 faces -- with no
trace line, no counter and no reading in any gate.

**Defect 2 -- func_illusionary was not in the world brush whitelist.**
native/src/bsp_map.cpp decided which brush entities are world geometry with a
name list: func_brush, func_wall, func_lod. func_illusionary -- the entity whose
entire purpose is "not solid, still drawn" -- was missing, so its faces were
parsed and then dropped. On cp_snakewater_final1 that is **2,892 faces** (61
entities), 7,662 triangles, invisible in every frame and in every gate.

**The fix.** The whitelist gains func_illusionary (bsp_map.cpp:330-331). The
budget becomes a *measured* capacity of 400,000 (main.cpp:4270-4292) with a
refusal that is *readable*: every dropped face and brush model is counted, the
first refusal is named, and one trace line carries capacity/requested/dropped/
droppedFaces/droppedBrushes/firstFace/firstBrush/state on both the parse path
(map: geometry budget) and the draw path (gpu: world geometry budget).

**The gate.** world-geometry-budget-check.sh (new) asserts the budget line exists,
the numbers agree with the probe, the capacity is the measured one, nothing is
dropped on the four authorised maps, and the two named degradations (capacity
shrunk, whitelist rolled back) each turn it red at a *named* assertion. Its
--pixel-attr mode answers the question a re-pin must answer: which faces did the
restored geometry actually draw, and did anything else move. It was measured on
cp_snakewater_final1 (section 6) and the first assertion shape had to be replaced:
"pixels only increase" is unsatisfiable when a fix substitutes a curved displacement
grid for a flat fallback quad.

## 1. What was wrong, in the old code

| where | old behaviour |
|---|---|
| native/src/bsp_map.cpp (pre-fix parse loop) | the triangle loop exited when the limit was reached; no counter, no name, no trace |
| native/src/bsp_map.cpp (pre-fix whitelist) | "func_brush" / "func_wall" / "func_lod" only; func_illusionary fell through to "not world geometry" |

The consequence is not cosmetic. A truncated parse is a *silent* change of the
thing every picture is made of: the frame is a valid frame of a map that is
missing its far half, and nothing in the product or in the gate suite said so.

## 2. The change

| file:line | change |
|---|---|
| native/include/bsp_map.h:116-131 | eight budget fields: triangleCapacity, triangleRequested, triangleDropped, facesDropped, brushModelsDropped, firstDroppedFace, firstDroppedBrush, budgetState |
| native/src/bsp_map.cpp:320-331 | world brush whitelist gains func_illusionary, with the note that "not solid" is not "not drawn" |
| native/src/bsp_map.cpp:333-337 | per-model bookkeeping (modelDrawn / modelLostToBudget / faceModel) so a refusal can name the model |
| native/src/bsp_map.cpp:676-716 | faceLostToBudget + noteBudgetDrop: one place that records a refusal and names the first one |
| native/src/bsp_map.cpp:932-1001 | displacement grid asks the budget *before* building a grid; a refused grid still adds its triangles to the request |
| native/src/bsp_map.cpp:1030-1043 | the parse loop no longer breaks early; it writes map.triangleCapacity |
| native/src/main.cpp:4270-4292 | kWorldTriangleCapacity = 400000, sized from the demand readings, plus the "map: geometry budget" trace line |
| native/src/main.cpp:7492-7505 | "gpu: world geometry budget" trace line (draw path, with drawTriangles) |
| native/tools/world_geometry_probe.cpp:294-305 | the probe prints the same budget line, so a gate can compare parser and product without a screen |

## 3. Sizing: 200,000 was a default, not a measurement

Demand = the triangles the map asks for when the budget cannot refuse anything
(probe --triangle-limit 4000000). It is a property of the map: the same number
comes back at 100k, 200k and 4M, only the drop count moves.

| map | demand (probe @4M) | capacity 400000 |
|---|---|---|
| cp_snakewater_final1 | 242,361 | within, 65% headroom |
| cp_sunshine | 82,859 | within |
| koth_bagel_rc12 | 98,066 | within |
| koth_bagel_rc11 | 98,029 | within |
| pl_odyssey (not an authorised map, reading only) | 105,486 | within |

At the old 200,000 the snakewater reading is triangles=200000 requested=242361
**dropped=42643** facesDropped=3513 brushFacesDropped=3246 brushModelsDropped=86
firstFace=12204 firstBrush=worldspawn state=truncated. dropped is *not* the
arithmetic gap (42,361): a refused displacement grid still emits a fallback fan
that occupies slots, which is why the gate asserts dropped >= requested - limit
and not equality.

Evidence: evidence/t88-maps/demand.txt (why 400000 and not 242361+epsilon: the
budget is a refusal threshold, and a threshold a map sits exactly on turns every
map edit into a silent truncation; 400000 is the smallest round capacity with
room for the authorised set).

## 4. What the gate asserts

Gate: world-geometry-budget-check.sh (new, 99 -> 100 gate scripts in this tree).

| id | assertion | the reading it consumes |
|---|---|---|
| B1 (gate:246,257,261,263) | both budget lines exist in the capture trace (map: and gpu:) and agree on requested/dropped | the two traceFmt lines above |
| B2 (gate:267-272) | capacity=400000, requested<=capacity, dropped=0, state=within | map: geometry budget line |
| B3 (gate:279-284) | requested and capacity equal the pins | evidence/t88-maps/budget-pins.tsv |
| B4 (gate:288-293) | facesDrawn/facesSkipped equal the pins (this is the whitelist-rollback detector); B4b/B4c add the face partition (world+brush faces and the unattributed gap) | gpu: world brush models line |
| B5 (gate:316-320) | probe @4M requested == product requested and probe dropped=0 | probe geometry budget line |
| B6 (gate:326-340) | at 200k and 100k: requested unchanged, dropped >= requested-limit (not equality: refused displacement grids still emit a fallback fan), facesDropped>0, firstFace present | probe geometry budget lines |
| H1 (gate:~660) | the run did not touch the operator's real settings.ini / ui.ini (a gate that edits the profile is not reproducible) | real_state before/after |
| PIXELS (gate:561,583) | the pre-fix rebuild is rolled back to the fixed sha, and every changed textured pixel belongs to a newly drawn face id | --pixel-attr |

Every product launch inside the gate passes --settings-file and
--ui-settings-file pointing into a throwaway directory created with mktemp -d and
removed on exit; the two files are seeded once, read only, from the real profile.
The product saves settings and UI state on every exit whether or not the caller
changed anything (main.cpp:11613-11625, native_ui.cpp:137-139), so without this a
gate would edit the operator's profile and the next run would start from
different input (H1 checks that the real profile did not move).

## 5. Mutation proof

--mutation applies each degradation, rebuilds, and requires the gate to go red at
a *named* assertion -- not merely red:

- capacity-shrink: main.cpp kWorldTriangleCapacity 400000 -> 200000, expected
  red at "B2 ... capacity=200000" (the capacity pin fires before the dropped
  reading; expecting "dropped=" called a correct red the wrong red).
- illusionary-whitelist: bsp_map.cpp world whitelist loses func_illusionary,
  expected red at "B4 ... facesDrawn=".

After each mutation the source is restored from a byte copy, compared with cmp
and sha256, rebuilt, and the gate must be green again. A mutation whose patch
does not match exactly once exits 3 with PATTERN NOT FOUND instead of silently
testing nothing (docs/GATE-DESIGN-TRAPS.md trap 5).

**RUN on this branch: WORLD-GEOMETRY-BUDGET-MUTATION=PASS**
(evidence/t88-maps/mutation-window-20261107-174856.log, 17:48:55-17:52:11, 196 s
inside the held lock, product rebuilt twice per mutation):

    mutation capacity-shrink: gate red at the named assertion --
      FAIL B2 cp_snakewater_final1: capacity=200000, expected 400000
    mutation capacity-shrink: main.cpp restored byte-identical (sha256 9914fa02...)
    mutation capacity-shrink: gate green again after restore
    mutation illusionary-whitelist: gate red at the named assertion --
      FAIL B4 cp_snakewater_final1: facesDrawn=354, pin=3246
    mutation illusionary-whitelist: bsp_map.cpp restored byte-identical (sha256 363d2dc7...)
    mutation illusionary-whitelist: gate green again after restore

The 354 is arithmetic, not a coincidence: 3,246 pinned drawn faces minus the
2,892 func_illusionary faces the rollback deletes. So both degradations are
caught by the assertion that names them, and neither mutation stays in the tree
(git diff --stat on the two files is empty at the end of the window).

The first execution of this mode (mutation-window-20261107-174521.log) FAILED on
two gate bugs that only a real run could reveal, and both are fixed here:

1. The whitelist anchor spans two source lines, and native/src/*.cpp are CRLF.
   The pattern was a literal-LF string, so edit_source counted 0 occurrences and
   the mutation reported "could not apply the mutation" -- a correct gate reading
   as broken. Patterns are now $'...' with an explicit \r\n (50662d1).
2. The pin row starts with the map name (map capacity requested dropped
   facesDrawn facesSkipped worldFaces brushFaces) but was read into seven words,
   shifting every field: B3 compared requested=242361 against pin=400000 and B4
   compared facesDrawn=3246 against pin=0, so a correctly restored build read as
   red. Fixed by consuming the name (223ec8a).

Both are recorded rather than quietly repaired because they are the failure mode
this doc warns about: a mutation window that cannot produce a red/green pair is
indistinguishable from a gate that does not fire.

## 6. Pixel attribution

Re-pinning a frame without saying which faces caused the change is a baseline edit.
--pixel-attr answers it with the product's own face-id channel: native/src/native_renderer_world.cpp:27-37
writes the BSP face index into the vertex colour under --world-face-id (r = id & 0xFF,
g = (id >> 8) & 0xFF, b = (id >> 16) & 0xFF). The mode captures the same camera and tick
twice -- the fixed build, then a rebuild with BOTH defects put back (capacity 200000,
whitelist without func_illusionary, i.e. the pre-fix behaviour) -- and hands the four
frames to world-geometry-budget-pixels.py.

### 6.1 The run (measured 2026-11-07 18:35, cp_snakewater_final1)

Command: HOLD_LOCK=1 bash world-geometry-budget-check.sh --pixel-attr
--only=cp_snakewater_final1. Wall time 3 min 12 s. Log:
evidence/t88-maps/pixel-window-20261107-181526.log. Frames, traces and CSV:
.scratch/t88-budget/pix/cp_snakewater_final1/ (4 BMPs, 4 traces, 4 metrics files).
Camera: evidence/t62-map-matrix/cp_snakewater_final1/camera.txt, tick 2000.

Result: **WORLD-GEOMETRY-PIXELS=FAIL (2 assertions)**, and both failures are real
measurements, not noise:

1. FAIL PIXELS: the rebuilt product is not the fixed build (sha f949ff55..., wanted
   7fb62a4d...). This branch has no /Brepro (grep -r Brepro over the build scripts and
   native/ finds nothing), so a rebuild of identical sources has a different PE
   timestamp. The assertion was wrong, not the rollback: the sources were restored
   byte-identically (checked with cmp). Fixed in the same commit -- see 6.4.
2. FAIL 2516 changed pixels belong to faces that were already drawn: the change is not
   contained in the restored geometry. This is the important one; section 6.2 explains
   the 2516 to the pixel.

### 6.2 What the two frame pairs say

    changed                                   15,722   bbox (0,0,1263,678)
      owned by an added face id               13,207   25 added ids, all of them visible
      outside                                  2,516
        same face id, small delta                942   856 of them are delta 1
        lost to a different face id            1,574   before ids: 12225 -> 1,565, 12227 -> 9
    pixels owned by the restored faces       before 0 / after 51  (one face, 14362)

### 6.3 Why "pixels only increase" is unsatisfiable for THIS fix

The truncated build does not draw a prefix of the face list. bsp_map.cpp:941 refuses a
displacement GRID when the remaining room is smaller than the grid, but bsp_map.cpp:1007-1043
still pushes that face's flat fallback fan, which usually fits. So faces past firstFace
reach the screen as FLAT quads until the budget is genuinely full:

- pre-fix trace: capacity=200000 requested=234951 dropped=35233 facesDropped=621
  firstFace=12204 state=truncated, displacedTriangles=165376;
- fixed trace: capacity=400000 requested=242361 dropped=0 drawTriangles=242109,
  displacedTriangles=199360;
- both frames report hiddenTriangles=5350, i.e. the visibility set did not change --
  what changed is which triangles exist;
- the pre-fix id frame owns pixels on faces 12225, 12227 and 12228, all >= firstFace
  and all displacement faces -- they can only be there as flat fallback quads.

Faces 12225 and 12227 are displacement surfaces. The fixed build draws their real
(curved) grids, and a curved grid does not cover the same screen area as the flat quad
it replaces. At this camera nothing is behind that patch -- the sky pass draws a 6-face
cube and writes no face id, so the after frame decodes those pixels to noise values
(9002075, 9001817, ...) -- and 1,574 pixels go wall -> sky. The loss is the fixed
picture being right, not geometry disappearing.

The restored func_illusionary faces are NOT what moved the picture: the after frame
shows 51 pixels of exactly one of their 2,892 faces (14362, WOOD/WOOD_BEAM02) and the
before frame shows none. They are real occluders, but at this camera almost all of them
sit behind nearer geometry. So the occluder explanation for the 2516 is excluded by
measurement; the flat-quad-versus-grid substitution is what is left, and it accounts
for 1,574 of the 2,516 (the rest is 942 same-face pixels: 856 at delta 1 = lightmap
sampling, and 20 above delta 8 that all touch an id edge, i.e. antialiased silhouettes
of face 12228 as its grid shifts).

### 6.4 The assertion that replaced it

world-geometry-budget-pixels.py no longer asks for outside = 0. It partitions and fails
on a gap:

1. every changed pixel belongs to an added id, or to a face drawn before and after with
   a channel delta <= 8 (--same-face-delta); a pixel above that limit is allowed only if
   it touches a pixel whose id changed (silhouette antialiasing) -- interior drift is a
   shading change and fails;
2. every other changed pixel ("lost") must have been owned before by a face in the map's
   dispinfo list AND with an index >= firstFace read from the pre-fix trace -- i.e. a
   face whose grid the pre-fix build refused and drew flat instead. Anything else fails
   as "not a displacement-grid substitution";
3. the faces the whitelist restored must own pixels in the after frame and none in the
   before frame (positive signature that the fix reached the screen).

The face lists come from the map, not from a build: world-geometry-budget-faces.py reads
lump 26 (dispinfo mapFace at +36) and lump 0/lump 14 (func_illusionary models, dmodel_t
firstface at +40, numfaces at +44). If those lists are not passed, the tool falls back to
the old strict rule -- nothing is weakened silently.

Offline re-run on the saved frames with the face lists (no product, no lock):
rc 0, outside_partition same_face=942 lost=1574, lost_grid_substitution=1574,
unattributed_lost absent, restored_face_pixels before=0 after=51.

The rolled-back-sha assertion from 6.1(1) was rewritten too: byte equality is only
asserted when the branch can produce it; otherwise the check asserts that the restore
rebuilt the product and that its bytes are not the pre-fix build's.

### 6.5 Negative cases (the assertions are not one-way)

world-geometry-budget-pixels-selftest.py edits a copy of the real frames once each
(command in run (c) of section 9) and re-runs the tool:

- case A: one interior pixel of a face drawn before and after moved by +40 in blue ->
  rc 1, "FAIL ... away from any id edge (max channel delta 40 > 8): that is a shading
  change";
- case B: one lost pixel's before id rewritten to a face that is not a displacement face
  -> rc 1, "FAIL 1 lost pixels are not a displacement-grid substitution".

The tool's first version was tested the same way with 16x16 synthetic frames before it
ever read a product capture (changed=128 outside=0 passes; one changed pixel on an
already drawn face reads outside=1 and fails), which is how the assertion was known to
be falsifiable in the first place.

### 6.6 Status of the baseline

No pin was re-pinned and evidence/t62-map-matrix/ is untouched by this commit: --pixel-attr
ran on one camera of one map, and re-pinning needs the t62-mapaxes owner lease. The
numbers above are therefore evidence for the assertion shape and for this camera only.
The T62 frame pins still have to be re-pinned with a reason string in that window (section
9c), and the four-map run is four times the captures.

## 7. Cost

Readings, not a cost claim. The CSVs below come from the 16:16 capture run of the
fixed build on this host, one capture per map, and nothing here isolates the cost
of the 200,000 -> 400,000 capacity change from the rest of the frame:

    map                   peak working_set   private     frames   fps range
    cp_snakewater_final1  3,139 MiB         3,439 MiB   44        1.0 - 15.9
    cp_sunshine           2,611 MiB         2,871 MiB   163      20.9 - 58.1
    koth_bagel_rc11       2,015 MiB         2,950 MiB   140       3.5 - 49.9
    koth_bagel_rc12       1,757 MiB         2,704 MiB   61       10.8 - 19.8

Columns read by name from the product's metrics CSV (working_set_bytes,
private_bytes, fps, rendered_frames); the first row is the first second of the
process, so the peak includes startup. Build cost: full build rc=0 in 105 s
(evidence/t88-maps/window-20261107-152000.log), incremental rebuild rc=0 in 4 s
(window-20261107-161615.log) -- the capacity is a constructor argument, so it adds
no build time. The measurement that would isolate the delta is a paired run (same
camera and flags) at 200,000 and at 400,000 comparing working_set_bytes and fps;
that pair has not been run, so the table is a reading of the fixed build, not a
before/after.

## 8. What this does not prove

- The pixel attribution proves a *partition with no unowned gap*, not full coverage: a
  restored face that is edge-on, occluded or out of view owns no pixels and is reported
  as a reading, not asserted. It also runs on one camera of one map so far (section 6).
- The budget is sized for the authorised map set. A future map above 400,000
  triangles will truncate -- but now it prints capacity/requested/dropped and
  names the first refused face, and the gate's B2 fails instead of the picture
  quietly losing geometry.
- Raising the capacity does not make dynamic brush entities (func_door,
  func_movelinear, func_tracktrain, func_breakable) render with demo time; they
  are still 0-hit in this source (T85 audit, section 8.2-3). See GAPS.md.
- No frame-time claim is made beyond the readings in section 7; they are single
  captures on one host.

## 9. How to re-run (three runs, merge window)

The gate refuses HOLD_LOCK=1 unless the shared lock is held by owner t88-budget
(gate:130-136), so every command below goes through one lease. `<tree>` is the
merged checkout; nothing here writes outside it.

**(a) the assertions, reusing the captures committed here**

    for m in cp_snakewater_final1 cp_sunshine koth_bagel_rc12 koth_bagel_rc11; do
      mkdir -p .scratch/t88-budget/$m
      cp evidence/t88-maps/trace-$m-forward.trace .scratch/t88-budget/$m/forward.trace
      sha256sum native/build-nmake/tf2_demo_native.exe | cut -d' ' -f1 \
        > .scratch/t88-budget/$m/.build.forward
    done
    bash hostlock.sh run t88-budget bash -c 'cd <tree> && HOLD_LOCK=1 \
      build-target.sh tf2_demo_native world_geometry_probe'
    bash hostlock.sh run t88-budget bash -c 'cd <tree> && HOLD_LOCK=1 \
      world-geometry-budget-check.sh --reuse \
      --re-pin="T88: capacity 400000 sized from measured demand (242361 worst map); func_illusionary restored to the world whitelist"'
    bash hostlock.sh run t88-budget bash -c 'cd <tree> && HOLD_LOCK=1 \
      world-geometry-budget-check.sh --reuse'

--reuse only skips the capture when `.build.forward` names the sha256 of the
binary in the tree (gate:189-193); the first command writes the pins, the second
proves they still hold. Expected: WORLD-GEOMETRY-BUDGET=PASS twice, ~2 min total.
(Without --reuse the same two runs work and recapture four frames each, ~6 min.)

**(b) the two mutations**

    bash hostlock.sh run t88-budget bash -c 'cd <tree> && HOLD_LOCK=1 \
      world-geometry-budget-check.sh --mutation'

Rebuilds four times (mutate + restore, per mutation) and leaves the tree clean.
Expected: WORLD-GEOMETRY-BUDGET-MUTATION=PASS; logs under
evidence/t88-maps/mutation-*.log. Budget 20 min (timeout 1500).

**(c) pixel attribution**

    bash hostlock.sh run t88-budget bash -c 'cd <tree> && HOLD_LOCK=1 \
      world-geometry-budget-check.sh --pixel-attr'

Two rebuilds and eight captures (four maps x after/before, textured + face-id
frames). Expected: WORLD-GEOMETRY-PIXELS=PASS plus one
"reading <map>: changed=... outside=0" line per map; frames land in
evidence/t88-maps/pixels/. Budget 25 min (timeout 2400).

**(d) Why t88-window.sh is not the merge command.** It chains all three behind
one build and was written for a single held window; the merge window runs them
separately so one failure does not hide the others. Its logic is the same three
calls in the same order.

**Tokens and the assertion that decides them.** Grep for the token, not for a
line number: the numbers drift when the gate is edited, the strings do not.

    WORLD-GEOMETRY-BUDGET=PASS              all of the below held
    WORLD-GEOMETRY-BUDGET=FAIL (N assertions)
    "B1 <map>: no map: geometry budget line in the capture trace"      (gate:246)
    "B2 <map>: dropped=$dropped state=$state firstFace=$first_face -- the map lost geometry to the budget"   (gate:269)
    "B2 <map>: capacity=$cap, expected 400000"                           (gate:267)
    "B4 <map>: facesDrawn=$drawn, pin=$pdrawn"   <- whitelist regression (gate:288)
    "B4c <map>: $part_gap brush faces are accounted for by neither pass (pin $pin_gap) -- a face was dropped without being counted"   (gate:307)
    "B5 <map>: probe requested=$huge_req, product requested=$req"        (gate:319)
    "B6 <map>: at $limit requested=$s_req, demand is $req (the ask must not move with the capacity)"   (gate:331)
    "H1 real profile untouched by this run: ..."                         (gate:~660)

    WORLD-GEOMETRY-BUDGET-MUTATION=PASS / =FAIL (N)   (--mutation)
    WORLD-GEOMETRY-PIXELS=PASS / =FAIL (N assertions) (--pixel-attr)

A green run prints, per map, "reading load: ...", "reading brush: ..." and
"reading <map>: face partition world=.. brush=.. drawn=.. skipped=..
unattributed=.."; those lines are the numbers the assertions compared.
