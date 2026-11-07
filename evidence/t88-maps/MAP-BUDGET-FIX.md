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
restored geometry actually draw, and did anything else move.

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

Re-pinning a frame without saying which faces caused the change is a baseline
edit. --pixel-attr answers it with the product's own face-id channel:

- native/src/native_renderer_world.cpp:31-37 encodes the BSP face index into the
  vertex colour (r = id & 0xFF, g = id >> 8, b = id >> 16) under --world-face-id.
- The mode captures the same camera and tick twice: on the fixed build, and on a
  rebuild with BOTH defects put back (capacity 200000, whitelist without
  func_illusionary) -- i.e. the pre-fix behaviour.
- world-geometry-budget-pixels.py decodes the two id frames and the two textured
  frames and asserts the containment: every changed textured pixel must belong to
  a face id that did not exist in the earlier frame (outside=0). Adding opaque
  geometry can only replace what a pixel showed, never reveal something new, so a
  changed pixel on an already-drawn face would be an unexplained change.

The attribution tool has its own negative case, run on synthetic frames before it
touched a product capture (.scratch-t88/selftest-pixels.py, 16x16 BMPs): a changed
region owned by a newly drawn face reads changed=128 outside=0 and exits 0, while
the same scene with one extra changed pixel on an already drawn face reads
outside=1 and exits 1 with "FAIL 1 changed pixels belong to faces that were already
drawn". So the containment assertion is not one that can only pass.

**NOT RUN: attempted and cancelled, no frame exists.** --pixel-attr was started
at 17:53:xx on cp_snakewater_final1, but the t91 integration chain took the lock
first and this run's ticket queued behind it (hostlock status: owner=t91-integration,
queue=...-t88-budget-308447). Rather than queue a rebuild chain behind a lease
that owns the machine for the evening, the job was killed and the ticket removed
at 17:54; the lock was left to the chain. So evidence/t88-maps/pixels/ is empty,
no attribution frame was captured from this tree, and no pixel baseline was
re-pinned: the T62 pins in evidence/t62-map-matrix/ are untouched by this commit.
What exists is the tool and its negative case (above). Run it with the command in
section 9 (c) -- one map is enough to be evidence, four maps take roughly four
times as long because the mode rebuilds twice and captures four frames per map.
Expected: WORLD-GEOMETRY-PIXELS=PASS plus one "reading <map>: changed=...
outside=0" line per map.

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

- The pixel attribution proves *containment*, not full coverage: a restored face
  that is edge-on, occluded or out of view owns no pixels and is reported as a
  reading, not asserted.
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
