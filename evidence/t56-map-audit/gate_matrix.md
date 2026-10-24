# T56 gate matrix (static reading of the six existing gates)

Phase: static only. Nothing in this table was executed. Every row is a reading of the
script text plus the evidence files the script writes; durations are **estimates**
derived from the measured per-capture cost, not recorded gate runtimes (no gate records
its own wall clock — see "Duration basis").

Repo: D:/TF2_Native_Test @ p0-entity-protocol, HEAD 99eeea294cbe883e4c3e6b4b82c83de45d2ab926.
TF root (all gates hardcode it): D:/SteamLibrary/steamapps/common/Team Fortress 2/tf.

## Summary

| # | Gate | lines | map | demo | tick | camera | forward-only cost (est.) | recommended for |
|---|------|-------|-----|------|------|--------|--------------------------|-----------------|
| 1 | visual-baseline-check.sh | 571 | pl_odyssey (74) | tf/demos/autorecord_2026-07-02_13-26-46.dem (75) | 2000 (120) | demo camera; probe eye fixed at -2423.6,5566.0,-1280.0 (143) | ~2-3 min (1 capture + 1 probe) | pl_odyssey |
| 2 | world-material-check.sh | 758 | cp_snakewater_final1 (60) | testdata/bb84...cp_snakewater_final1.dem (61) | 0 (316) | demo camera | ~3-5 min (5 captures at tick 0 + probe) | cp_snakewater_final1 (secondary) |
| 3 | material-coverage-check.sh | 700 | cp_snakewater_final1 (66) + POV odyssey (70,71) | testdata/bb84... (67); odyssey POV (70) | 0 (80); POV 2000 (71) | demo camera | ~6-10 min (6-8 product runs) | cp_snakewater_final1 (primary) |
| 4 | world-material-face-check.sh | 456 | **none** (renders the demo's map = pl_odyssey) | tf/demos/autorecord... (64) | 2000 (92) | demo camera | ~2-3 min (3 captures) | pl_odyssey (per-face ledger) |
| 5 | world-geometry-hole-check.sh | 1417 | **none** (renders the demo's map = pl_odyssey) | tf/demos/autorecord... (57) | 2000 (58) | demo camera; pinned eye -2294.8,348.8,383.4 (144) | ~15-20 min (18-frame capture matrix + probes) | pl_odyssey (heaviest; run alone) |
| 6 | lighting-semantics-check.sh | 614 | pl_odyssey (80) | tf/demos/autorecord... (81) | 2000 (captures 261,310-317) | demo camera | ~7-9 min (9 captures) | pl_odyssey |

**No gate covers cp_sunshine (the third selected map).** Four of the six hardcode a map
path or map name (1,2,3,6); the two that do not (4,5) still pin odyssey numbers (odyssey
face ids, odyssey seam ids, odyssey eye), so pointing them at a sunshine demo would render
the right map and then fail every pin. The third map therefore needs a **new baseline
measurement**, not a gate pass — see runtime_proposal.md. (koth_bagel_rc12 and
koth_bagel_rc13 are kept only as alternatives/negative control.)

## Per gate

### 1. visual-baseline-check.sh (571 lines)
- Purpose (header 1-67): T10/T11 visual correctness baseline: geometry probe census of the
  reference map, the product's own reference capture, four measured world regions, and the
  T17 three populations (two blend layers weighted, sky six faces, lightmap term addressing
  its own texels, two lightmap pages agreeing on darkness).
- Asserts: probe census pinned (142-144: `--ray-down 96`, eye `-2423.6,5566.0,-1280.0`);
  capture must be frame_tick==2000 exactly, retried up to 6 times (114, 125-127); four
  regions on a 1264x681 frame with pinned margins (evidence/visual-baseline/regions-ref.txt);
  five mutations M1-M5 (412+, 439, 461-462, 483-484, 499) each of which must turn the gate red.
- Map/demo/tick/camera: MAP=maps/pl_odyssey.bsp (74); DEMO=tf/demos/autorecord_2026-07-02_13-26-46.dem (75);
  `--capture-tick 2000` (120); camera is the demo's, probe eye is a fixed odyssey map-space point (143).
- Map limitation: the region rectangles, the probe eye and the census pins are odyssey
  specific. Not parameterised; changing the map invalidates every pin.
- Duration: forward-only = 1 product capture (measured 59.9 s to tick 2000 on odyssey,
  evidence/visual-baseline/trace-ref.txt) + 1 probe run + region analysis ≈ 2-3 min.
  `--mutation` (66) adds five rebuilds and five captures — not part of a forward run.
- Recommended for: pl_odyssey (it is the only gate that pins *regions* of the frame rather
  than counts, so it is the closest thing to a visual baseline).

### 2. world-material-check.sh (758 lines)
- Purpose (header 1-53): six world material/lighting capabilities, each with a state and a
  reason, each able to go red. Sections 1-8: probe self-tests, synthetic fixtures, fixture
  mutations, real-map states + counters, negative control, product trace tying GPU-side
  lightmap count to parse side, cubemap build + env pass selection, and the two switches off.
- Asserts: probe `--bsp $TF/maps/$MAP.bsp --tf-root $TF --map-name $MAP` (182); cubemap first
  path pinned `materials/maps/cp_snakewater_final1/c-5135_2022_-15.vtf` (214); five product
  captures (316 `--capture-tick 0 --world-sky`, 431, 434, 494, 625).
- Map/demo/tick/camera: MAP=cp_snakewater_final1 (60); DEMO=testdata snakewater (61);
  tick 0 (316); camera = demo camera.
- Negative control (278-283) reads `$TF/maps/koth_bagel_rc13.bsp`; because that BSP is **not
  installed** the gate prints "OK koth_bagel_rc13.bsp is not installed here, so the run reads
  an empty map". The control therefore currently tests the absent-map case by design. If
  bagel_rc13 is ever installed the control changes meaning (the gate says so itself at 283).
- Map limitation: pinned to snakewater (map name, cubemap path, counter values).
- Duration: 5 captures at tick 0 + probe runs ≈ 3-5 min (tick-0 captures are cheaper than
  tick-2000; no measured tick-0 trace cost was found, hence a range).
- Recommended for: cp_snakewater_final1, as the material/lighting **state** gate.

### 3. material-coverage-check.sh (700 lines)
- Purpose (header 1-59): the pre-T19 fixed 64-tile cap dropped materials with no counter
  (51 of 148 on snakewater, 44 pvs-visible, 106983 triangles, one of them 80256 triangles of
  wall). Asserts: plan table (every material tiled or refused by name), the picture as a
  control (budget cut reproduces the pre-T19 frame byte for byte), and the draw-time state.
- Asserts: probe plan (174, 400); product runs through one helper (220-236); 8 sections
  including `tiled=112 capped=0 refused=0 batches=2`, two runs byte-identical, blend switch
  off, weapon frame isolation counters 0, and the one-batch control frame sha
  `65934ede...` (359).
- Map/demo/tick/camera: MAP=cp_snakewater_final1 (66); DEMO=testdata snakewater (67);
  **TICK=0** (80) with PAUSE=1 → `--start-paused` (217-219, 229); POV_DEMO=odyssey (70),
  POV_TICK=2000 (71), and the POV run explicitly sets PAUSE=0 (337-339).
- Map limitation: BEYOND_CAP_MATERIAL=MEDIEVAL/PLASTERWALL002A (78) is snakewater's largest
  surface; the pre-T19 frame sha is snakewater-specific; the POV weapon frame is odyssey.
- Duration: 10 run_product call sites in the file (6-8 execute in a forward run) ≈ 6-10 min.
- Recommended for: cp_snakewater_final1 (primary) — it is the only gate that accounts for
  *every* material with a name from a closed set of refusals.

### 4. world-material-face-check.sh (456 lines)
- Purpose (header 1-58): T21 measures the world material chain **at a pixel**: point →
  front-most drawn triangle → material → VMT store → VTF store → tile rectangle → texel the
  shader fetches, then compares the prediction against a captured frame.
- Asserts: fetch counters add up; every atlas material has a row; every traced tiled face's
  uv lands inside its own tile; albedo frame matches trace (tol 3) and textured frame (tol 6);
  repeat capture byte-identical; refusal counts weighted by pvs-visible triangles.
- Map/demo/tick/camera: **no MAP variable**; DEMO=tf/demos/autorecord_2026-07-02_13-26-46.dem
  (64); 3 forward captures at `--capture-tick 2000` (92, 130-132), up to 8 attempts; camera = demo camera.
- Map limitation: renders whatever map the demo carries (odyssey); the traced points, the
  oversized-tile case and the old-cap material are odyssey's.
- Duration: 3 captures × ~42.5 s measured (evidence/world-material-face/trace-albedo.txt) ≈ 2-3 min.
- Recommended for: pl_odyssey — this is the gate that produces per-face, per-material,
  per-pixel rows, i.e. the natural feeder for the §5 step-5 ledger.

### 5. world-geometry-hole-check.sh (1417 lines)
- Purpose (header 1-44): is an empty pixel a hole or the sky. Asserts the missing population
  is covered by BSP sky faces, enclosed components are sky through brush seams, point oracle
  and rasterizer agree, the winding repair is refused by measurement, and the seam
  classification is not a cull artefact (positive control + mutation M6).
- Asserts: pins 124-135 (PIN_MISSING=1648, PIN_SKY_MISSING=1648, PIN_UNEXPLAINED=0,
  PIN_ENCLOSED=1648, PIN_SKY_FACES=731, PIN_SKY_TRIANGLES=1842, PIN_WORLD_TRIANGLES=99312,
  PIN_BACKFACE=80917, PIN_TEXTURED_SHA=ac2cfe9..., PIN_REF_SHA=6bb4d9a...); T44 seam pins
  144-149 (eye -2294.8,348.8,383.4; seam ids; culled=0; derived-back=0).
- Map/demo/tick/camera: **no MAP variable**; DEMO=tf/demos/autorecord... (57); TICK=2000 (58);
  camera = demo camera, eye pinned at 144.
- Map limitation: every pin is odyssey's; the capture matrix is 18 frames
  (world-geometry-hole-capture.sh: 250-283: ref, textured, none, terrain, lightmap-off,
  4 vis/cull combos × flat+depth, sky-faces, sky-depth, sky-ids, world-ids, world-ids-nocull).
- Duration: 18 captures × ~45-60 s ≈ 15-20 min, plus probe runs. Heaviest gate; run alone.
- Recommended for: pl_odyssey (only when the question is "hole or sky").

### 6. lighting-semantics-check.sh (614 lines)
- Purpose (header 1-73): six lighting/shader controls — three pass switches (dynamicLighting,
  specular, bump) and three material keys ($bumpmap2, %detailtype, $ssbump) that are parsed
  and refused. Compares one field at a time; A→B is the dynamic-lighting semantic, A→C
  specular, A→D bump with no consumer (0 px is the finding), B→E bump with a consumer.
- Asserts: pins at 124-135 region and the T40 re-pin (header 41-57: A->B 1776259 differing,
  A->C 390361, B->E 910776; direction claims brighter=0 / darker=0 hold).
- Map/demo/tick/camera: MAP=maps/pl_odyssey.bsp (80); DEMO=odyssey (81);
  BUMP_TEXTURE=materials/brick/brickwall001_normal.vtf (82); 9 captures at tick 2000 (261, 310-317).
- Map limitation: odyssey pins; the bump texture is a fixed path; deltas are per-frame, not absolute.
- Duration: 9 captures × ~45 s ≈ 7-9 min.
- Recommended for: pl_odyssey.

## Duration basis (why these are estimates)

No gate records its own wall clock. `grep -nE 'SECONDS|date \+|elapsed'` over the six
scripts finds only `RUN_TAG=$(date +%Y%m%d%H%M%S)-$$` in five of them
(visual-baseline:78, world-material:75, material-coverage:85, world-material-face:67,
lighting-semantics:85) and nothing in world-geometry-hole-check.sh.

The only measured per-capture costs are trace timestamps at the `capture: armed` line
(process start → frame at the requested tick):
- evidence/visual-baseline/trace-ref.txt: `59903.2 capture: armed requested=2000 frame_tick=2000 frames=2056` → 59.9 s (odyssey, tick 2000).
- evidence/world-material-face/trace-albedo.txt: `42482.7 capture: armed requested=2000 frame_tick=2000 frames=1415` → 42.5 s (odyssey, tick 2000; process exits 42578.9 → 42.6 s).

Tick-0 captures (snakewater) and any bagel capture have **no measured cost** in the evidence
available to this phase, so those ranges are wider. A runtime phase that wants real numbers
should wrap each gate in `/usr/bin/time -f %e` (the gates themselves are unchanged).

## Map coverage summary

- cp_snakewater_final1: covered by gates 2 and 3 (both hardcode it).
- pl_odyssey: covered by gates 1, 4, 5, 6 (1 and 6 hardcode the path; 4 and 5 render the demo's map).
- cp_sunshine: covered by **no gate** — no script names it. It has a SourceTV demo
  (SUNSHINE.dem) and its BSP is in `tf/maps/`, so the recipe is the snakewater
  `--start-paused` + tick 0 shape (unproven for this map); see runtime_proposal.md §C.
- koth_bagel_rc12 (alternative, not in the three-map set): covered by **no gate** — no
  script names it, and its only installed BSP is under `tf/download/maps/` (not
  `tf/maps/`), so the two gates that take a `--bsp` path would need an absolute path anyway.
- koth_bagel_rc13: named by world-material-check.sh:278-283 as a negative control only; its
  BSP is not installed (see REPORT.md gaps), and its only demo is in testdata.
