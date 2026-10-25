# T62 — focused visual matrix, final report

Date: 2026-10-25. Tree: `D:/TF2_Native_Test`, branch `p0-entity-protocol`.
Deliverables: `MAP-MATRIX.md` (106 lines), `CHARACTER-MATRIX.md` (341 lines), this report.

## 1. What T62 asks for, and what it forbids

T62 (NEXT-TASKS-2026-10-23.md:265-278): build a **focused** matrix over the existing T50–T61 scope,
run the affected focused gates once with their identity, produce a map matrix (the maps T56 selected
× fixed materials/geometry axes) and a character matrix (the T57 classes × T61 action axes), with
every cell marked PASS / FAIL / NOT VERIFIED / UNSUPPORTED plus an owner and a disposition.
T55 (clip/export) is explicitly out of scope. The task **must not produce a release verdict**, and
`bash verify-all.sh --quick` stays in T73, run exactly once by the user on the frozen tree.

## 2. Method, and what it is worth

Both matrices were produced against **committed evidence only**: each cell quotes a file path with a
line number and a literal excerpt, or names the directory listing that proves no such file exists.
Nothing was inferred from a report's prose, nothing was estimated, and no gate was re-run to write a
cell. That is deliberate: a matrix is a *reading of the record*, and the record is what a later
reviewer will have.

Legend: **PASS** = a gate exists, a committed run exists, and the committed reading supports the
claim. **FAIL** = the committed evidence contradicts the pin. **NOT VERIFIED** = the axis is
measurable today but no committed reading covers this map/class at this tree. **UNSUPPORTED** = the
input the axis needs is absent, so no run can produce it.

## 3. The counts

**Map matrix, 5 maps × 4 axes = 20 cells: 4 PASS, 12 NOT VERIFIED, 4 UNSUPPORTED, 0 FAIL.**

| Map | materials | geometry | lighting | coverage table | 
| --- | --- | --- | --- | --- |
| cp_snakewater_final1 | PASS | NOT VERIFIED | NOT VERIFIED | PASS |
| pl_odyssey | PASS | PASS | NOT VERIFIED | NOT VERIFIED |
| cp_sunshine | NOT VERIFIED | NOT VERIFIED | NOT VERIFIED | NOT VERIFIED |
| koth_bagel_rc12 | NOT VERIFIED | NOT VERIFIED | NOT VERIFIED | NOT VERIFIED |
| koth_bagel_rc13 | UNSUPPORTED | UNSUPPORTED | UNSUPPORTED | UNSUPPORTED (no BSP in the install) |

**Character matrix, 9 classes × 5 axes = 45 cells: 27 PASS, 9 NOT VERIFIED, 9 UNSUPPORTED, 0 FAIL.**
The 27 PASS are model load (9), body materials (9) and animation data/clock (9); the 9 NOT VERIFIED
are all "animation on a real demo tick, per class"; the 9 UNSUPPORTED are the third-person weapon
body. Measured against the four axes the brief illustrated (model / materials / weapon / animation)
the same evidence reads 36 cells: 18 measured, 9 NOT VERIFIED, 9 UNSUPPORTED.

## 4. What the matrix found that is not a cell

1. **0 of the 27 measured cells come from a rendered frame.** They are tables, model headers and
   sampled matrices. The only rendered proof in the character axis is one snakewater frame at tick
   64161, one camera, one unlabelled union mask (`6876 px = 0.80 %` of the frame,
   `targetInstances=2`). "The model loads and its tables drive the matrices" is proven; "a class is
   drawn on screen with a named animation" is not.
2. **The lighting evidence is still the pre-clock-fix pose.** `evidence/t36-lighting-dark-account/frames.txt:3`
   reads `eye=-2294.8,348.8,383.4` and `evidence/lighting-semantics/frames.txt:4` reads
   `A sha256=6bb4d9a7...`, i.e. the camera the T63-F1 clock defect produced, while
   `world-geometry-hole-check.sh:167-197` now pins `PIN_CAPTURE_EYE="815.5,-147.8,-194.3"` for the
   same demo and tick. Worse, `lighting-dark-account-check.sh:189` re-derives its own eye from its
   trace, so a green lighting gate today cannot notice that the camera moved.
3. **Only one gate pins stored world frame bytes**: `world-geometry-hole-check.sh:178-179`
   (`PIN_TEXTURED_SHA=1b0ab468...`, `PIN_REF_SHA=e9218c42...`), asserted at `:712-717`. Every other
   gate compares frames it just captured, so a picture that changed for the wrong reason stays green.
4. **cp_sunshine is gated after all, but not for the matrix**: `world-render-reliability-check.sh:71`
   runs `SUNSHINE.dem` and asserts reload errors plus six sky-face lines at tick 0 — which
   contradicts `evidence/t56-map-audit/gate_matrix.md:152` ("covered by no gate").
5. **koth_bagel_rc12 has no gate at all**, although its BSP is present (`tf/download/maps/`,
   26,486,963 B) and 31 POV demos sit next to it.
6. **T56's own report quotes counters its files no longer contain**: `REPORT.md:41` says
   `tiled=112 ... vmtNoBase=31`, the live `evidence/material-coverage/product-readings.txt` says
   `tiled=123 ... vmtNoBase=4 ... patchDeclared=30 patchResolved=30 transformRefused=16`; `REPORT.md:40`
   says refusals `materials=20 ... materials=3`, the live file says `materials=2 ... materials=4 ...
   materials=3`. T59 re-measured this map; the report was not re-pinned.
7. **A product defect with a mechanism, found from the label list**: a *moving* player is drawn
   playing `ReloadAirwalk_PRIMARY`. `native/src/main.cpp:191-247` matches substrings —
   `kMovingNeedles[] = {"run","walk","move"}` — so "air**walk**" satisfies the moving needle and,
   because it has frames while `PRIMARY_aimmatrix_run` has one (the comment at `main.cpp:218-221`
   says so), it wins. `main.cpp:9626` stores it and `main.cpp:9689` copies it into
   `animationStats.lastReason`, which is how `reason=ReloadAirwalk_PRIMARY` reached the pixel
   summary. This is product behaviour, quotable, and currently unasserted.
8. **The class axis is thinner than nine**: the two committed demos only ever play Scout, Soldier,
   Demoman, Medic and Heavy as movers (class histograms stop at 1/3/4/5 plus 6 once), every player row
   carries `missingModelIndex` (39/39 and 20/20), and only one LOD and one team's stems are ever
   built. Sniper, Pyro, Spy and Engineer are table-level PASS and cannot become pixel-level evidence
   from these recordings.

## 5. The cheapest way to settle each open cell

Cost basis: a measured capture is 42–60 s (`evidence/visual-baseline/trace-ref.txt` "59903.2 capture:
armed requested=2000 frame_tick=2000 frames=2056" and `evidence/world-material-face/trace-albedo.txt`
"42482.7 capture ... frames=1415"), so a 3-capture gate is 2.5–3 min and an 18-frame matrix 15–18 min.

| open cell | cheapest test | cost |
| --- | --- | --- |
| snakewater geometry | `world-geometry-hole-check.sh` with this demo, `TICK=0`, 3 frames | ~3 min |
| snakewater lighting | `lighting-dark-account-check.sh` with `MAP=maps/cp_snakewater_final1.bsp` | ~5 min |
| odyssey lighting (stale pose) | re-run `lighting-dark-account-check.sh` unchanged, diff `account.txt` | 8–10 min |
| odyssey coverage table | `material-coverage-check.sh` with `MAP=pl_odyssey`, tick 2000 | 6–8 min |
| cp_sunshine (all four) | coverage → geometry (3 frames) → lighting, in that order | 4 + 3 + 6 min |
| koth_bagel_rc12 | `world-material-face-check.sh` with its own POV demo (needs no `MAP`) | ~3 min |
| koth_bagel_rc13 | nothing: no BSP; keep it as the negative control | — |
| character D2 (5 reachable classes) | add model=/class to the animation tick lines + a scene animation summary in the product, then re-run `player-animation-check.sh` and `player-animation-pixels.sh` | ~25–30 min |

## 6. The gates this matrix rests on, and the map/tick each one pins

| gate | map | tick | what it pins |
| --- | --- | --- | --- |
| `world-geometry-hole-check.sh` | pl_odyssey (`:930`) | 2000 (`:58`) | 18 frames + two stored frame hashes |
| `world-material-face-check.sh` | whatever the demo loads (`:57`) | 2000 | 3 frames, fetch partition, face chain |
| `material-coverage-check.sh` | cp_snakewater_final1 | 0 | tile table, batches, control frame |
| `lighting-dark-account-check.sh` | pl_odyssey (`:88`) | 2000 | 5 captures, re-derives its own eye |
| `world-render-reliability-check.sh` | cp_sunshine (`:71`) | 0 | reload errors, six sky lines |
| `visual-baseline-check.sh` | pl_odyssey | 2000 | stored reference trace |

No axis is measured on two maps. No axis is measured on two ticks of one map. Posture is split:
snakewater gates run paused at tick 0, odyssey gates at tick 2000. That is the structural reason
twelve of the twenty map cells cannot be filled from the record.

## 7. Disposition

* The 4 PASS cells stand as they are; the two materials cells are the ones T59/T59b changed, and their
  readings were refreshed on this tree.
* The 12 NOT VERIFIED cells are **queued, not excused**: §5 gives each a costed test. They stay NOT
  VERIFIED in this report; filling them is follow-up work, not a matrix edit.
* The 4 UNSUPPORTED cells are a property of the install (koth_bagel_rc13 has no BSP anywhere in
  `tf/maps` or `tf/download/maps`) and of scope (T55, the third-person weapon body).
* The `ReloadAirwalk_PRIMARY` needle defect (finding 7) is a product bug with a mechanism and a fix
  site; it is recorded here and belongs to the animation wave, not to T62's matrix.

## 8. Readings taken on the final tree

Pending: T62 explicitly does not produce a release verdict, and the matrix's own cell list above is a
reading of the record. The final-tree gate wave (see the T59b / T71-T72 reports) refreshes the four
PASS cells and the lighting/coverage evidence; when it lands, its identity and assertion counts are
appended here so that the matrix and the tree cannot drift apart the way T56's quotes did (finding 6).
