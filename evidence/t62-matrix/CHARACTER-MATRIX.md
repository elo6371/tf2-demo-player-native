# CHARACTER-MATRIX — focused visual matrix over the existing scope (T50–T61)

**Evidence snapshot for this refresh:** commit `integration/t79-final @ 65315d27b10d416a46680123c6719416c11d6541` on 2026-11-04 (not frozen); the first pass of this refresh was read at `046f4cd52a6eae5eb952488bb0095bcce86500b1` and is unchanged by the second. Two later additions postdate it: the nine C cells come from `third-person-weapon-check.sh` at product sha256 `a3c93d5755fd331351766dfb991fe17f90c4dfb753ad7ef9f53391f0303724e9` (§2.4, `RUNS-2026-11-04.md` §26.4), and the two team pairs that were single-team here are completed by the upward two-team batch at that same product sha (§2.3, §26.5). The gates behind this refresh were run at that commit and are recorded in [RUNS-2026-11-04.md](RUNS-2026-11-04.md): tree and binary identity, per-gate command and verdict, log and frame hashes. The original 2026-10-25 matrix was written against `047bcfbdb89225c1c054535dc3e51bc44b9cf3b1`; T62's task definition is retained in `T62-REPORT.md` §0.

**Every gate this refresh added now passes.** The eight of the `65315d2` sweep: — `PLAYER-ANIMATION-CHECK`, `player-model-check`, `ANIMATION-AVAILABILITY`, `ENTITY-MATERIAL`, `ENTITY-PROPERTY-LOOKUP`, and `PLAYER-ANIMATION-PIXELS` / `PIXEL-CHECK` / `ENTITY-ID` (readings: `RUNS-2026-11-04.md` §3, §3b, §4), plus the two gates `65315d2` added: `player-body-appearance-check.sh` (`BODY-APPEARANCE`, §2.3 and `RUNS` §9a) and `map-material-matrix.sh` (`MAP-MATERIAL-MATRIX`, `MAP-MATRIX.md`). The per-class pixel chain in §2.6 is new in this refresh and is recorded there in §5. What those runs do **not** establish is listed in `RUNS-2026-11-04.md` §6. A ninth later gate, `third-person-weapon-check.sh` (`THIRD-PERSON-WEAPON`), closes all nine C cells (§2.4). A tenth, the T66 relay gate (`spectate-check.sh`, `SPECTATE-CHECK`), was run at `74e946b41f8720f14578d4124aa296c4de47983e` and passes; it is what backs the death clause now recorded in §5 item 9 (`RUNS` §9c).

**Working tree at reading time:** the gates in §3/§4 were run at `046f4cd`; `65315d2` then added two gates and put `team`, `family` and `origin` on the entity-ledger line (trace only — no draw, decode or material path changed), and the eight gates were re-run at that commit (`RUNS` §9). Neither commit changes product renderer/decoder behaviour. The commits and what each touches are listed in `RUNS-2026-11-04.md` §1 and §8.

**Out of scope by task:** ragdoll, and T55 video cut/export (T62 §11 step 4: *"T55 视频剪辑/导出当前明确延后；本任务不验收编码器或 MP4"*). **This document is not a release verdict** (T62 step 5: *"本任务不出最终 release verdict"*), and it is not a full-chain acceptance either.

## 0. Vocabulary used below

| Cell | Means |
|---|---|
| **PASS** | A committed reading makes the claim, and the claim is exactly what that reading measures — quoted with `path:line`. |
| **FAIL** | A committed reading contradicts the claim. No cell in this matrix is FAIL. |
| **NOT VERIFIED** | The capability is exercised by some gate, but no committed reading ties it to this class/action. Missing, not broken. |
| **UNSUPPORTED** | No gate in the tree measures it at all, or the task scope excludes it. An absence of coverage, not a defect. |

**Evidence roots:** `D:/TF2_Native_Test/evidence/`. The brief's shorthand `evidence/player-model`, `evidence/player-animation` and `evidence/weapon-body` do **not** exist; the real roots are `evidence/t57-player-model/` (T57 + T60), `evidence/t61-animation/` (T61), and `evidence/weapon-material/`, `evidence/weapon-animation/`, `evidence/weapon-world-model/`.

---

## 1. The class axis comes from the code, not from the nine classes being famous

Two gates sweep a hard-coded list and the resolver has its own table; all three agree on the same nine names:

- `D:/TF2_Native_Test/player-animation-check.sh:172` — `CLASSES="scout sniper soldier demo medic heavy pyro spy engineer"`
- `D:/TF2_Native_Test/player-model-check.sh:253` — `for mdl in scout sniper soldier demo medic heavy pyro spy engineer; do`
- `D:/TF2_Native_Test/native/src/entity_model.cpp:589-602` — `case 1: return "models/player/scout.mdl";` … `case 9: return "models/player/engineer.mdl";`, where `case 4` is `models/player/demo.mdl` (Demoman's file is `demo.mdl`, not `demoman.mdl`).

Matrix classes: Scout(1), Sniper(2), Soldier(3), Demoman(4), Medic(5), Heavy(6), Pyro(7), Spy(8), Engineer(9).

### 1.1 Which classes a recording actually plays — this decides what can ever be pixel-tested

> `D:/TF2_Native_Test/evidence/t57-player-model/runtime/t60/player-model-demo-bagel.txt:51`
> `demo models rows=39 withModel=36 markerOnly=3 missingModelIndex=39 classFallback=36`

> `D:/TF2_Native_Test/evidence/t57-player-model/runtime/t60/player-model-demo-snakewater.txt:25`
> `demo models rows=20 withModel=14 markerOnly=6 missingModelIndex=20 classFallback=14`

Per-entity class-fallback models at the ticks those files resolved (bagel: 3 ticks with rows, requested `60001` resolved `59145`; snakewater: requested `64231` resolved `63721`):

| Class | tfClass | Played in a committed recording? | Where (demo / requested tick / entity) | Evidence shape |
|---|---|---|---|---|
| Scout | 1 | **yes** | bagel 60001 e5,8,10,11 · snakewater 64231 e3,10,15 | `model=models/player/scout.mdl classFallback=1` |
| Soldier | 3 | **yes** | bagel 60001 e2,6,9,12 · snakewater 64231 e2,8,9,11,17 | e.g. bagel `:6` `entity=2 … model=models/player/soldier.mdl` |
| Demoman | 4 | **yes** | bagel 60001 e7,13 · snakewater 64231 e4,5 | `model=models/player/demo.mdl` |
| Medic | 5 | **yes** | bagel 60001 e3,4 · snakewater 64231 e6,13,16 | `model=models/player/medic.mdl` |
| Heavy | 6 | **yes, one row** | snakewater 64231 e7 only | `playerclass` `tfClass=6 rows=1` |
| Sniper | 2 | **no** | never a played player in either recording | absent from both `playerclass` listings |
| Pyro | 7 | **no** | — | absent |
| Spy | 8 | **no** | — | absent |
| Engineer | 9 | **no** | — | absent |

Two hard limits follow, and they are the spine of §2 and §4:

1. Every player row in both recordings has `missingModelIndex` (`39/39` and `20/20`): the model comes from the tfClass fallback, never from the demo's `m_nModelIndex`.
2. Sniper, Pyro, Spy and Engineer can only ever be claimed from a **static sweep of their model files**. No committed frame, from any recording, contains one of them as a player.

---

## 2. The matrix: 9 classes × 5 axes

Axes, deliberately split because "animation" has two very different evidence levels in this tree:

- **A — model load (static).** T60 audit: companions, LOD0 budget, animation source.
- **B — materials / bodies (static).** T60 paint assertions per class model.
- **C — weapon body, third person.** A weapon drawn on a player, per class.
- **D1 — animation data, pose and clock (counter level).** T61 stages A/B/C plus the printed D reading.
- **D2 — animation on a real demo tick and on screen (per class).** T61 stage E plus the pixel gate.

### 2.1 Status grid

| Class | A model load | B materials | C weapon body | D1 anim data/clock | D2 demo tick + pixels |
|---|---|---|---|---|---|
| Scout | PASS | PASS | **PASS** | PASS | **PASS** |
| Sniper | PASS | PASS | **PASS** | PASS | **PASS** |
| Soldier | PASS | PASS | **PASS** | PASS | **PASS** |
| Demoman | PASS | PASS | **PASS** | PASS | **PASS** |
| Medic | PASS | PASS | **PASS** | PASS | **PASS** |
| Heavy | PASS | PASS | **PASS** | PASS | **PASS** |
| Pyro | PASS | PASS (thinnest: 1 slot) | **PASS** | PASS | **PASS** |
| Spy | PASS | PASS | **PASS** | PASS | **PASS** |
| Engineer | PASS | PASS | **PASS** | PASS | **PASS** |

All nine D2 cells are per-class rendered frame readings, each on its own playback tick, and all nine C cells are rendered third-person weapon readings from `third-person-weapon-check.sh` (§2.4): Demoman, Scout, Soldier and Medic on snakewater, Pyro, Heavy, Spy, Engineer and Sniper on the Highlander STV `pl_upward_f12`. The remaining PASS cells are static or probe-level evidence (§4.2).

### 2.2 A — model load (static). 9/9 PASS, one gate, one sweep

Gate `D:/TF2_Native_Test/player-model-check.sh` (T60): probe `entity_model_probe --player-model-audit` walks the nine class `.mdl` files. Four named negative controls run first (`wrong-tf-root`, `wrong-demo`, `class-table-8`, `class-table-missing-model`), so a green run is not a vacuous one. `evidence/t57-player-model/runtime/REPORT-T60.md:114`:

> `PASS player-model-check: negative controls named-failed; A lod0Truncated=0; B paintedSlots per slot x family; C separateAnimSource=1.`

Summary, `evidence/t57-player-model/runtime/t60/player-model-audit.txt:10`:

> `player-model-audit summary classes=9 resolved=9 renderable=9 companionsComplete=9 lodCountKnown=9 vvdLodsMin=7 vvdLodsMax=7 vtxLodsMin=7 vtxLodsMax=7 lod0Truncated=0 lod0LostTotal=0 lod0SkippedTotal=0 classesWithPaint=9 productPaintedSlots=78 paintedFamiliesMin=2 distinctPaintedStemsMin=1 headOnBodyClasses=0 redBluDistinctSlotsTotal=14 slot0Resolved=9`

Per class, same file lines 1–9 (LOD0 geometry fully built: budget = built = render indices, nothing skipped or lost):

| Class | bones | sequences | lod0RenderIndices | vvdLods/vtxLods | companions |
|---|---|---|---|---|---|
| Scout | 78 | 306 | 23442 | 7/7 | `vvd=1,dx90=1,dx80=1,sw=1,phy=1` |
| Sniper | 74 | 240 | 23595 | 7/7 | same 5 |
| Soldier | 86 | 361 | 26922 | 7/7 | same 5 |
| Demoman | 84 | 276 | 25755 | 7/7 | same 5 |
| Medic | 92 | 213 | 24864 | 7/7 | same 5 |
| Heavy | 79 | 259 | 28944 | 7/7 | same 5 |
| Pyro | 76 | 270 | 12603 | 7/7 | same 5 |
| Spy | 84 | 274 | 16980 | 7/7 | same 5 |
| Engineer | 73 | 351 | 32724 | 7/7 | same 5 |

One literal class line (`player-model-audit.txt:1`), elided in the middle with `…` only:

> `player-model class=Scout tfClass=1 path=models/player/scout.mdl resolution=FoundVpk companions=vvd=1,dx90=1,dx80=1,sw=1,phy=1 companionComplete=1 renderable=1 mdlVersion=48 … bones=78 sequences=306 textures=17 bodyParts=5 attachments=29 vvdLods=7 vtxLods=7 vvdVertices=8261 lod0Descriptors=9 lod0RenderIndices=23442 lod0Budget=23442 lod0Built=23442 lod0Skipped=0 lod0Lost=0 …`

Animation-source half, `evidence/t57-player-model/runtime/t60/player-model-anim.txt` (9 lines, each `separateAnimSource=1 animationInput=ok materialTable=available`):

| Class | animPath | animSeqs | boundSeqs | bindBones |
|---|---|---|---|---|
| Scout | `models/player/scout_animations.mdl` | 1012 | 303 | 78/78 |
| Sniper | `models/player/sniper_animations.mdl` | 932 | 237 | 74/74 |
| Soldier | `models/player/soldier_animations.mdl` | 858 | 353 | 86/86 |
| Demoman | `models/player/demo_animations.mdl` | 767 | 273 | 84/84 |
| Medic | `models/player/medic_animations.mdl` | 671 | 210 | 92/92 |
| Heavy | `models/player/heavy_animations.mdl` | 819 | 255 | 79/79 |
| Pyro | `models/player/pyro_animations.mdl` | 842 | 266 | 76/76 |
| Spy | `models/player/spy_animations.mdl` | 826 | 271 | 84/84 |
| Engineer | `models/player/engineer_animations.mdl` | 1131 | 348 | 73/73 |

**What A does not say:** only LOD0 is built — `lod0Built` is the field the gate asserts. `vvdLods=7 / vtxLods=7` are *counted*, not built, and no gate switches LOD. T60 says so itself (`REPORT-T60.md:374-375`): *"注意：这个 gate 只证明静态装载"*.

### 2.3 B — materials / bodies (static paint). 9/9 PASS, thinnest on Pyro

Same T60 run; assertions `paintedSlots == 2*lod0MaterialSlots`, `paintedFamilies=2`, `distinctPaintedStems == lod0MaterialSlots`, `headOnBody=0`, `redBluDistinctSlots>=1`, `productPaintedSlots>=18`. Mutation `b` forces this red (`REPORT-T60.md:165`): `B Scout redBluDistinctSlots = 0, expected >= 1`.

| Class | material slots | painted slots | families | red/blu distinct | distinct stems | body stem (slot) | head stem |
|---|---|---|---|---|---|---|---|
| Scout | 4 | 8 | 2 | 1 | 4 | `models/player/scout/scout_red` (0) | `scout_head_red` |
| Sniper | 6 | 12 | 2 | 1 | 6 | `sniper_red` (0) | `sniper_head_red` |
| Soldier | 6 | 12 | 2 | 2 | 6 | `soldier_red` (0) | `soldier_head_red` |
| Demoman | 4 | 8 | 2 | 1 | 4 | `demoman_red` (0) | `demoman_head_red` |
| Medic | 5 | 10 | 2 | 2 | 5 | `medic_red` (0) | `medic_head_red` |
| Heavy | 5 | 10 | 2 | 2 | 5 | `models/player/hvyweapon/hvyweapon_red_sheen` (4) | `heavy_head_red` (slot0) |
| Pyro | **1** | **2** | 2 | 1 | 1 | `pyro_red` (0) | **`-`** |
| Spy | 5 | 10 | 2 | 2 | 5 | `spy_red` (3) | `spy_head_red` (slot0) |
| Engineer | 3 | 6 | 2 | 2 | 3 | `engineer_red` (0) | `engineer_head_red` |

Three things this axis honestly says, and does not say:

- Every drawn stem above is a **RED** stem. `redBluDistinctSlotsTotal=14` only proves the resolver *finds a different* stem for BLU in 14 slots; no BLU *stem* was named by a draw in any of these runs. (What a BLU body is actually painted with is measured in pixels below, not by stem name.)
- Pyro has **one** material slot, hence `paintedSlots=2` and `headStem=-`: the weakest paint evidence of the nine, and no head material at all.
- Heavy's body stem is a weapon-skin family (`hvyweapon_red_sheen`) and Spy has `skinFamilies=26`. Both are asserted green and both sit on T57's own NOT VERIFIED list (`evidence/t57-player-model/REPORT.md:394` §8: *"Heavy/Spy head-paint look"*).

**The team dimension, measured on screen (2026-11-04).** The stems above are
material-slot readings; they say nothing about which family reached a drawn body.
`player-body-appearance-check.sh` (introduced at `65315d2`, extended in this pass)
closes that with pixels: for each body it captures an entity-id frame (which names
the id of every pixel) and a painted frame of the same tick, from a camera derived
from the ledger origin of that body (`eye = origin + (-250, 0, +58)`, pitch 4.1,
yaw 0), and measures the paint over exactly the id mask of that body. The pass at
`65315d2` read two same-model pairs on snakewater tick 63000 (Demoman and Scout,
gap 69.7 and 133.1); this pass repeated it on a Highlander recording
(`pl_upward_f12`) where both teams are on screen for every class, and read
**18 bodies, nine same-class pairs** — seven pairs on tick 24001 (Heavy's red body on
20001) and the Scout and Sniper pairs on tick 45001, where those two classes are
unoccluded in both teams:

| Class | red body | blu body | red-minus-blu gap |
|---|---|---|---|
| Demoman | ent 19, 2,077 px, r-b 38.8 | ent 6, 2,692 px, b-r 19.8 | **58.6** |
| Heavy | ent 5 (tick 20001), 5,134 px, r-b 58.8 | ent 11, 4,977 px, b-r 20.7 | **79.5** |
| Pyro | ent 9, 3,287 px, r-b 53.5 | ent 3, 622 px, b-r 33.3 | **86.8** |
| Spy | ent 18, 2,729 px, r-b 50.1 | ent 13, 1,422 px, b-r 37.1 | **87.2** |
| Soldier | ent 16, 3,105 px, r-b 55.8 | ent 4, 2,711 px, b-r 35.4 | **91.2** |
| Engineer | ent 8, 3,040 px, r-b 72.0 | ent 2, 1,786 px, b-r 34.6 | **106.6** |
| Medic | ent 10, 2,891 px, r-b 74.8 | ent 7, 5,054 px, b-r 44.7 | **119.5** |
| Scout | ent 12 (tick 45001), 3,010 px, r-b 87.7 | ent 14 (tick 45001), 1,548 px, b-r 51.3 | **139.0** |
| Sniper | ent 15 (tick 45001), 2,339 px, r-b 44.4 | ent 17 (tick 45001), 3,405 px, b-r 21.2 | **65.6** |

Every ledger line wrote the family its team asks for, 18 of 18 (`team=2 -> family=0`,
`team=3 -> family=1`). The floors are a 200 px mask, 15 counts for the tint sign of a
body and 40 for the pair gap; the smallest measured gap is 58.6 and the per-body
tints run 19.8 to 87.7. The mask sizes move a little between runs of the same target
because the captured pose depends on the animation phase (§2.4); the sign of the tint
and the pair gap are what this axis asserts, and the readings above are one batch at
product sha256 `a3c93d57…` (`RUNS-2026-11-04.md` §26.5). The pair is the hinge: one red body could read red on a
red-lit map by accident, a same-model pair cannot, which is why the claim is made on
the gap and not on a per-body constant. A static audit agrees at stem level: all 39
LOD0 material pairs of the nine class models resolve, 14 (family, slot) pairs are
distinct images -- exactly the per-class `redBluDistinctSlots` the model probe reports
(scout 1, sniper 1, soldier 2, demo 1, medic 2, heavy 2, spy 2, engineer 2, pyro 1) --
and its 72 assertions pass. Evidence `evidence/t62-matrix/body-appearance/` and
`evidence/t62-matrix/body-appearance-upward/` (frames as lossless PNG, with the
sha256 of each source BMP in `frame-hashes.txt`); readings `RUNS-2026-11-04.md` §9a
and §18. What it still does not do: every class is paired, but each body is one
tick, so this says the two team paints separate on those frames and not that they
separate on every frame of the recording; and the measurement reads paint, never a
BLU stem name.


### 2.4 C — weapon body in third person. 9/9 PASS

Gate `third-person-weapon-check.sh` (T83). With `--player-weapon-attach` the renderer composes the weapon's hold bone through the player's bone chain and draws that weapon's world model at the hold point. Per class the gate captures the same tick twice attached and once with the flag off, and asserts: attached differs from off; the off run prints no attach line and stays byte-identical to that class's `class-pixels` baseline frame; no weapon entity is drawn both attached and standalone; and the attached instance's own pixels sit on the weapon at that body. Determinism is asserted on the **pose phase**, not naively: the animation clock is advanced by wall-clock frame pacing (`delta = |delta origin| / strideUnits`, `native/src/animation_clock.cpp:110`, `native/include/animation_clock.h:52-62`), so two processes can land on different phases of the same walk cycle — measured `maxDeltaCycle` 0.838676 vs 9.315151, 11x apart, with a 169 px weapon-only delta at equal phase and a 2,129 px whole-body delta across phases. The gate therefore captures `identity` first to pin the phase and re-runs every other capture until it lands on the same one; a byte difference at equal phase is a real regression, a byte difference across phases is recorded as phase variance (`RUNS-2026-11-04.md` §26.2). Committed readings, product sha256 `a3c93d5755fd331351766dfb991fe17f90c4dfb753ad7ef9f53391f0303724e9`:

Snakewater recording, side yaw 45 (the gate default): `cases=4 phase-pinned=4 attached-deterministic=4 off-is-baseline=4 attach-row=4 refused-row=0 weapon-pixels=4`, `evidence/t83-weapon/weapon-attach/`.

| Class | body, own pixels | tick | hold source | hold point | drift | weapon px |
|---|---|---|---|---|---|---|
| Demoman | ent 5 `demo.mdl`, 1599 | 64161 | bone 21 | `-13.5,55.6,4.1` | 0.00 | 181 |
| Scout | ent 15 `scout.mdl`, 1178 | 64161 | attachment 23 | `-3.1,49.9,4.1` | 0.00 | 69 |
| Soldier | ent 2 `soldier.mdl`, 1502 | 63000 | attachment 24 | `23.8,53.3,7.8` | 0.00 | 207 |
| Medic | ent 13 `medic.mdl`, 1791 | 64000 | bone 19 | `-6.5,41.5,2.2` | 0.00 | 182 |

The Highlander STV, side yaw 225 (this recording's geometry puts the camera side on the other bearing): `cases=5 phase-pinned=5 attached-deterministic=5 off-is-baseline=5 attach-row=5 refused-row=0 weapon-pixels=5`.

| Class | body, own pixels | tick | hold source | hold point | drift | weapon px |
|---|---|---|---|---|---|---|
| Pyro | ent 3 `pyro.mdl`, 3865 | 30001 | attachment 25 | `23.0,55.7,-8.4` | 0.00 | 265 |
| Heavy | ent 11 `heavy.mdl`, 5377 | 24001 | bone 19 | `35.2,52.8,-1.8` | 0.00 | 1211 |
| Spy | ent 18 `spy.mdl`, 2669 | 24001 | attachment 22 | `27.5,61.3,-0.2` | 0.00 | 54 |
| Engineer | ent 8 `engineer.mdl`, 2511 | 24001 | bone 39 | `21.3,51.4,0.3` | 0.00 | 56 |
| Sniper | ent 15 `sniper.mdl`, 2951 | 20001 | bone 24 | `21.6,62.1,-5.5` | 0.00 | 599 |

Each row is a frame in which a weapon entity the run names is drawn at a hold point that coincides with its bone (`drift 0.00`), with the weapon's own pixel mask measured on the entity-id frame and no standalone twin of that weapon entity in the same frame. The five classes that read UNSUPPORTED before this pass were not a rendering defect: the 64-entry `preparedEntityMeshes` budget (`native/src/main.cpp:3695,3730`) truncated the world-model list before their target weapons, and the attach path silently counted `noWorldMesh` without naming the path. The fix supplies the missing mesh at attach time (`supplyAttachMesh`, `native/src/main.cpp`), which took `noWorldMesh` from 9 to 0; the truncation itself is unchanged and is still observable.

What those nine cells claim, and what they do not: each class has one third-person frame in which a weapon entity the run names is drawn at a hold point that coincides with its bone, with the weapon's own pixel mask measured. They do **not** claim that the hold point is the hand for every weapon in every pose, nor that the weapon's own reload or draw animation plays (`m_nSequence` is not in the recorded protocol). Two further limits are recorded rather than hidden: the upward rows are measured at side yaw 225 because that recording puts the camera side on the other bearing (each frame's `frame: request ... camera=...,225.0` line records it), and the renderer's 96-entry `entityModelMeshes_` cap (`native/src/native_renderer_entity.cpp:168`) is now saturable by the supplied attach meshes — three non-target weapons (`c_sapper_xmas`, `c_shogun_warhorn`, `c_flamethrower`) logged `attach: mesh result=refused stage=upload ... lastError=0x8007000E` in the upward runs, while every targeted weapon is inside the first 23 supplied meshes and draws (`RUNS-2026-11-04.md` §26.3).

What was true before T83, and why those cells were empty:

The render gates then in the tree exercised POV/viewmodel weapons only — none rendered a weapon attached to a third-person player body:

- `weapon-body-check.sh:102,108,112` — `POV_DEMO="$TF/demos/autorecord_2026-07-02_13-26-46.dem"`, `VM_MODEL="models/weapons/v_models/v_scattergun_scout.mdl"`, `ARM_MODEL="models/weapons/c_models/c_sniper_arms.mdl"`, `CHAIN_TICK=16306`.
- `weapon-material-check.sh:76,82,85,86` — same demo, `NATURAL_BODY="models/workshop/weapons/c_models/c_pro_rifle/c_pro_rifle.mdl"`, `OVERRIDE_BODY="models/weapons/c_models/c_tfc_sniperrifle/c_tfc_sniperrifle.mdl"`.
- Committed reading `evidence/weapon-material/natural-on.readings.txt:5`: `gpu: viewmodel weaponBody model=…/c_pro_rifle.mdl class=CTFSniperRifle entity=1008 worldModelIndex=533 … sequences=1 bones=1 vertices=12681 materials=2/2/2/2 …` and `:6` `armDrawCalls=26 weaponDrawCalls=26 … weaponVisible=1 weaponReason=drawn`.
- `weapon-world-model-check.sh` reads `m_iWorldModelIndex` and resolves model-precache paths from POV/bagel demos; it does not capture a third-person player render. This adds weapon identity evidence, not a C-axis render.

That is one Sniper rifle, two materials, in the viewmodel pass, on one POV demo at `capture_tick=2000`. At that point nothing drew a weapon **attached to a third-person player body**, and no gate asserted a per-class weapon. The only weapon strings in the class audit are material families living inside the player `.mdl` itself (`hvyweapon_red_sheen`). Hence UNSUPPORTED for all nine before T83 — absence of scope, not failure. The five later cells (Pyro, Heavy, Spy, Engineer, Sniper) were unmeasured for a second reason, the mesh budget, which §2.4 above now records.

### 2.5 D1 — animation data, pose and clock (counter level). 9/9 PASS

Gate `player-animation-check.sh`; its committed forward reading is
`evidence/t61-animation/r10-after-demos/check.log` (232 OK / 0 FAIL). Current per-class probe records
are under `evidence/t61-animation/r10-after-demos/runtime/`; the unit mutation suite exercises u1–u10
and returns to a 232/0 baseline. Each class now resolves a
multi-frame run clip and samples four distinct cycles; Spy uses its secondary run clip. The separate
continuity sweep remains a swim, taunt or menu sequence, so it is not evidence of in-demo behavior.
At `931588b` the same gate reads 233 assertions / 0 failures; the extra one is the new
`matcher oneShot` field that pins the reload rule (§5 item 9, `RUNS-2026-11-04.md` §11). The
committed T61 log quoted above predates that assertion, which is why it still reads 232.

| Class | moving / sampled sequence | frames | continuity sweep |
|---|---|---:|---|
| Scout | 212 `run_PRIMARY` | 49 | 208 `swim_ITEM2` (31) |
| Sniper | 213 `run_PRIMARY` | 19 | 199 `s_swimAlign_ITEM1` (61) |
| Soldier | 201 `run_PRIMARY` | 22 | 199 `s_swimAlign_LOSER` (61) |
| Demoman | 147 `run_PRIMARY` | 19 | 138 `swim_MELEE` (31) |
| Medic | 80 `run_PRIMARY` | 19 | 79 `swim_LOSER` (31) |
| Heavy | 115 `run_PRIMARY` | 17 | 224 `selectionMenu_Anim01` (72) |
| Pyro | 141 `run_PRIMARY` | 19 | 173 `layer_taunt_scorch_shot` (114) |
| Spy | 145 `run_SECONDARY` | 19 | 144 `swim_LOSER` (31) |
| Engineer | 265 `run_PRIMARY` | 19 | 252 `s_swimAlign_ITEM2` (61) |

The run sequences are asset/probe-level evidence: they load, produce bounded distinct sampled poses, and advance the reported cycle. Every class in §2.6 also has a named D2 pixel chain in a real demo frame.

### 2.6 D2 — animation on a real demo tick and on screen, per class. 9/9 PASS, 0/9 NOT VERIFIED

The T61.1 pixel run links one rendered entity instead of inferring a class from the class-blind
aggregate tick log. `evidence/t61-animation/r10-pixel-mutations/baseline.log:5-24` pins playback
frame 64161 / scene tick 74481, the product capture, and the selected run-cycle picks. Its per-entity
chain at `:26-38` reads:

> `SUBJECT id=95 ent=5 model=models/player/demo.mdl state=moving seq=147 label=run_PRIMARY pixels=4383`
> `JOIN subject matches its own pick: models/player/demo.mdl moving seq=147 label=run_PRIMARY`
> `MOTION subject=95 over-mask=4383 differ=3794 share=86.56%`
> `DECODED id=95 ent=5 model=models/player/demo.mdl state=moving seq=147 label=run_PRIMARY`

The model path maps to Demoman (`entity_model.cpp:589-602`). The same log records
`ENTITY-ID=PASS`, a shaded body, and `PIXEL-CHECK=PASS`. Separately, `:44-58` reports the whole-entity
coverage-view mask (3,497 pixels) and 2,268 changed pixels (64.86%) between drive-on and drive-off;
that coverage mask is distinct from the subject's 4,383-pixel ID mask used for the per-entity motion
measurement above.
That (the 2026-10-29 reading) is one Demoman entity's product-selected run pose, not a claim about the server's sequence, every Demoman instance, other classes, other actions or other ticks; the first refresh below adds three more classes on the same terms, and the second adds the last five.

**Refresh (2026-11-04): the chain covers nine classes on two recordings.** `class-pixels-check.sh` (repo root) drives one playback tick per class, captures `animated` / `off` / `identity` / `identity-2` frames from a camera derived from that body's own origin, asserts that the two `identity` captures are byte-identical, and grades the `BODY` line the pixel gate prints for every drawn player body. Readings are from [RUNS-2026-11-04.md](RUNS-2026-11-04.md) §5 and §12; frames are in `evidence/t62-matrix/class-pixels/<class>/` and `evidence/t62-matrix/class-pixels-upward/<class>/`.

| Class | Recording | Frame | Body | Own pick | Pixels | Moved | Status |
|---|---|---|---|---|---|---|---|
| Demoman | snakewater | 64161 | `id=95 ent=5 demo.mdl` | `run_PRIMARY` seq 147 (moving) | 1,767 | 1,391 (78.72 %) | **PASS** |
| Scout | snakewater | 64161 | `id=96 ent=15 scout.mdl` | `run_PRIMARY` seq 212 (moving) | 1,161 | 500 (43.07 %) | **PASS** |
| Soldier | snakewater | 63000 | `id=86 ent=2 soldier.mdl` | `AttackStand_PRIMARY` (idle) | 1,140 | 1,068 (93.68 %) | **PASS** |
| Medic | snakewater | 64000 | `id=95 ent=13 medic.mdl` | `AttackStand_PRIMARY` (idle) | 1,400 | 904 (64.57 %) | **PASS** |
| Pyro | pl_upward_f12 | 30001 | `id=85 ent=3 pyro.mdl` | `AttackStand_PRIMARY` seq 19 (idle) | 3,152 | 1,180 (37.44 %) | **PASS** |
| Heavy | pl_upward_f12 | 24001 | `id=89 ent=11 heavy.mdl` | `AttackStand_SECONDARY` seq 16 (idle) | 5,072 | 3,377 (66.58 %) | **PASS** |
| Spy | pl_upward_f12 | 24001 | `id=95 ent=18 spy.mdl` | `AttackStand_SECONDARY` seq 10 (idle) | 2,730 | 1,626 (59.56 %) | **PASS** |
| Engineer | pl_upward_f12 | 24001 | `id=86 ent=8 engineer.mdl` | `AttackStand_PRIMARY` seq 9 (idle) | 3,033 | 2,631 (86.75 %) | **PASS** |
| Sniper | pl_upward_f12 | 20001 | `id=93 ent=15 sniper.mdl` | `AttackStand_PRIMARY` seq 11 (idle) | 3,681 | 2,213 (60.12 %) | **PASS** |

Why each class uses the tick it uses (this is tick selection, not threshold tuning):

- The class has to be drawn at that tick as a body the chain can name. Snakewater frame 64161 draws
  only Demoman and Scout; Medic's last sampled origin at 64161 is 2.5 s stale and a camera built on
  it lands on a props-only frame, so Medic is read at 64000 and Soldier at 63000, where the body is
  drawn and its origin is from the captured frame.
- The near body must not sit in the new camera's cone. First attempts read the right body well but
  failed because a *closer* body became the subject: Soldier's camera at 62000 had Medic `e13` 118
  units ahead (6,629 px); Medic's at 64161 had Demoman `e4` 153 units ahead (4,046 px). Moving each
  class to a frame where the nearest neighbour is >400 units off-axis (Soldier `e2` at
  3640.0,-1764.5,-312.0; Medic `e13` at 4779.4,-2001.4,-88.0) fixed both. `MIN_BODY_PIXELS=200`
  and `MIN_BODY_MOTION_SHARE=5 %` were not changed.
- The second recording is the Highlander STV `a9630c65…match-20261030-2145-pl_upward_f12.dem`, which draws 19 player bodies per sampled tick — both teams, every class. Its per-body origin comes from the product ledger line of the captured frame (`entity: id=… origin=`), not from `entity_overlay_probe`: that probe falls back to checkpoint snapshots and its origins were up to 1,135 ticks (~17 s) stale, which aimed the first upward run's cameras at empty ground. Each target also had to pass a clearance test — with `eye = origin + (-250, 0, +58)` and a ±55° cone, no other body within 300 units in front — because a nearer body takes the subject slot even when the intended body reads well (Heavy's first upward frame had Engineer `e2` 4,274 px in front of it).

- Heavy now uses a real Heavy player body (`ent=11`, blue, `AttackStand_SECONDARY`); the old `ent=7` note applies to snakewater only — that entity is a class-fallback body whose model cycles scout→heavy→scout→engineer→spy because it carries no model index, and it is not used here.

- Pyro's row was blocked by a real defect, not by cast: every Pyro body drew the bind pose because the reload family's animation blocks are empty 0xFF-terminated chains (`skinned=0`), and the idle picker had selected one of those clips. Both were fixed in `native/src/animation_decoder.cpp` and `native/include/animation_sequence_match.h`; Pyro now selects `AttackStand_PRIMARY` seq 19 and is skinned (`RUNS-2026-11-04.md` §11).

Pixel counts are byte-stable across processes at a pinned tick and camera: the Scout `identity`
frame was captured six times over ten hours and hashed `7317b7a2...f0e423` every time, reading
1,161 px each time. The single 1,211 px reading came from the superseded per-target driver
generation (`/d/_shared/t62-class-pixels3.log`), which also mis-framed Soldier and Medic in that run;
it is not a rendering variance (`RUNS-2026-11-04.md` §5, §6). The chain now asserts that stability
itself: every class captures its `identity` frame twice in two processes and fails unless the two
BMPs are byte-identical -- the same construction as the official gate's `animated` = `animated2`.

The old `demo-ticks.txt` aggregate remains class-blind and is not used to upgrade any other row.

---

## 3. The ACTION axis

Rows separate the probe-level action evidence from the named rendered samples. D2 now has nine named class samples, one per class (§2.6).

| # | Action / pose | Sequence(s) the gate reads | Classes | Demo / tick | What is actually measured | Status | Evidence |
|---|---|---|---|---|---|---|---|
| 1 | idle | `PRIMARY_aimmatrix_idle` / `AttackStand_*` | all 9 | none — `idleTicks=0` | idle clock rate is zero at probe level; no idle demo tick is identified | **PASS (clock)** / **NOT VERIFIED (demo)** | `r10-after-demos/runtime/class-*.txt`; `demo-ticks.txt:33` |
| 2 | run (moving) | per-class `run_PRIMARY`; Spy `run_SECONDARY` (indices/frames in §2.5) | all 9 at probe level | Demoman + Scout at snakewater 64161; Soldier 63000; Medic 64000 (scene tick = frame); Pyro/Heavy/Spy/Engineer/Sniper are drawn idle on pl_upward_f12 | multi-frame poses and rate at probe level; rendered bodies change 78.72 % / 43.07 % / 93.68 % / 64.57 % of their own measured pixels on drive on/off (Demoman / Scout / Soldier / Medic) | **PASS (D1 all 9; D2 nine classes)** | `r10-after-demos/runtime/class-*.txt`; `r10-pixel-mutations/baseline.log:26-58`; `RUNS-2026-11-04.md` §5, §12 |
| 3 | walk | whole-token `walk` needle | — | — | no walk cycle is selected; `walk` no longer matches inside `airwalk` | **NOT VERIFIED** | matcher tests in `player-animation-check.sh`; T61 report §8.7 |
| 4 | swim / taunt / menu (continuity sweep) | per-class continuity sequences in §2.5 | all 9 (one sequence each) | none | bounded pose continuity, `usable=1`; this remains a separate sweep from the run pick | **PASS** (pose stability only) | `r10-after-demos/runtime/class-*.txt` `animcontseq` / `animcont` |
| 5 | airwalk / reload pose | labels remain in model tables; they are not the selected moving pose | all 9 tables | none | no current per-class gate asserts these actions on screen | **NOT VERIFIED** | `class-*.txt` label lists; matcher mutation m8 |
| 6 | **what is drawn** | nine per-class picks: Demoman `run_PRIMARY` (147), Scout `run_PRIMARY` (212), Pyro/Engineer/Sniper/Soldier/Medic `AttackStand_PRIMARY`, Heavy/Spy `AttackStand_SECONDARY` | `id=95` demo, `id=96` scout, `id=86` soldier, `id=95` medic (snakewater); `id=85` pyro, `id=89` heavy, `id=95` spy, `id=86` engineer, `id=93` sniper (upward) | 64161 / 64161 / 63000 / 64000 / 30001 / 24001 / 24001 / 24001 / 20001 | own identity, pick, sample and pixel motion chain, per body | **PASS (nine samples)** | `r10-pixel-mutations/baseline.log:26-58`; `evidence/t62-matrix/class-pixels/`; `evidence/t62-matrix/class-pixels-upward/` |
| 8 | attack | standing attack labels are the idle pick for six classes (`AttackStand_PRIMARY` / `AttackStand_SECONDARY`); crouched attack labels (`AttackCrouch_PRIMARY` etc.) exist and decode | 6 of 9 drawn (as idle); crouch clips 9 of 9 decode | the same D2 frames | the drawn pose is an attack-stance idle, not a driven attack; crouched attack data is decoded by the pose-family gate but never selected | **PASS (data) / NOT VERIFIED (driven attack)** | `RUNS-2026-11-04.md` §13; label lists in `class-*.txt` |
| 9 | flinch | `a_flinch01` in four tables (medic, pyro, sniper, spy), 14 flinch sequences across the nine tables | 0 of 9 drawn; all sampled flinch clips decode | none | data decodes (`finite=1`, label echo, moving poses); the runtime never selects one | **PASS (data) / NOT VERIFIED (runtime)** | `RUNS-2026-11-04.md` §13 |
| 10 | ragdoll / death | labels `ragdoll`, `ragdollspawn` in every table | 0 of 9 | none | nothing; excluded by task scope | **UNSUPPORTED** | T62 §11 |
| 11 | video cut/export (T55) | — | — | — | not implemented, not accepted | **UNSUPPORTED** | T62 §11 step 4 |

### 3.1 The false `airwalk` match is closed

The old substring matcher treated `walk` inside `airwalk` as a moving run candidate and could select
`ReloadAirwalk_PRIMARY`. T61.1 replaced it with a whole-token matcher and a preference for the
first-token, multi-frame run clip. The nine `r10-after-demos` class records now select `run_PRIMARY`
(Spy: `run_SECONDARY`); the pixel run independently names Demoman's `run_PRIMARY` at seq 147. The
matcher regression is exercised by unit mutations and the pixel mutation that substitutes a
substring-only pick. This closes the old selection defect; it does not establish server animation
fidelity or prove every action on screen.

---

## 4. How many cells are actually measured

### 4.1 Counts, computed from §2.1

**9 classes × 5 axes = 45 cells: 45 PASS, 0 NOT VERIFIED, 0 UNSUPPORTED, 0 FAIL.**

| Axis | PASS | NOT VERIFIED | UNSUPPORTED |
|---|---|---|---|
| A model load | 9 | 0 | 0 |
| B materials/bodies | 9 | 0 | 0 |
| C weapon body (3rd person) | 9 | 0 | 0 |
| D1 anim data/clock | 9 | 0 | 0 |
| D2 demo tick + pixels | 9 | 0 | 0 |
| **Total** | **45** | **0** | **0** |

Against the four-axis shape the brief illustrated (model load / materials / weapon body / animations) the same evidence reads **36 cells: 36 measured, 0 NOT VERIFIED, 0 UNSUPPORTED**. D2 now has one sample per class.

### 4.2 Eighteen of 45 PASS cells are backed by a named rendered entity

Twenty-seven PASS cells are table/probe readings; nine more are the C cells, which are rendered weapon-on-body frames for all nine classes (§2.4). The nine rendered D2 cells are Demoman `id=95` and Scout `id=96` at snakewater frame 64161, Soldier `id=86` at 63000, Medic `id=95` at 64000, Pyro `id=85` at upward 30001, Heavy `id=89`, Spy `id=95` and Engineer `id=86` at 24001, and Sniper `id=93` at 20001, each joined to its model, its own pick, its samples and a per-entity pixel mask (§2.6). This is not a third-person player camera; T57's planned third-person capture (`evidence/t57-player-model/REPORT.md:388`) remains unrun, but C no longer depends on it: the weapon gate renders the attached weapon in the same frame as the body, so all nine C cells are rendered readings.

A separate, non-cell team-colour reading covers 18 bodies on the Highlander STV: nine same-class red/BLU pairs, one per class — seven on tick 24001 and the Scout and Sniper pairs on tick 45001, where those two are unoccluded in both teams (§2.3). This is a team-dimension criterion, not a second D2 body/tick for every class, so the 45-cell count is unchanged.

### 4.3 Remaining D2 evidence

The class-blindness blocker is closed: all nine classes have a rendered D2 cell from a recording that draws them as a nameable player body (§2.6). What remains is breadth rather than a missing class — one tick and one body per class, two maps. Crouch/crouchwalk/flinch are measured only on the opt-in `--animation-drive states` path; the default remains legacy, and standing attack still has no driven state (§5 items 2, 3, 7, 9; `RUNS-2026-11-04.md` §24.1 and §25.3). Do not infer any further cell from aggregate ticks.

---

## 5. What this matrix cannot claim

1. **Not a release verdict.** T62 step 5: *"本任务不出最终 release verdict"*. A green grid here plus green gates still does not mean the picture is right.
2. **Every class has a rendered D2 cell, but one body on one tick each.** Demoman, Scout, Soldier and Medic are read on snakewater; Pyro, Heavy, Spy, Engineer and Sniper on `pl_upward_f12`. A second instance of the same class is not measured.
3. **Two maps, six ticks, a camera derived per body.** Four D2 frames are snakewater (`64161`, `63000`, `64000`) and five are `pl_upward_f12` (`20001`, `24001`, `30001`). The reference camera is `--debug-camera 5363.8 -1102.1 5.9 4.1 0.0`; each per-class camera is that eye position minus the body's own origin, i.e. `eye = origin + (-250, 0, +58)`. Nothing generalises to another tick, camera or map.
4. **The pixel comparison is same-tick drive on/off.** It demonstrates the selected product pose moves the measured body's own pixels; it does not prove the server's animation sequence or a multi-tick run cycle.
5. **The identity mask measures one body per run.** The F5 chain associates the subject body with its own ID-coloured pixels (4,383 px in the historical Demoman reading; 1,767 px for Demoman at this tree); it makes no claim about the other bodies drawn on the same screen.
6. **Only LOD0 is built, and only RED is bound.** `vvdLods=7 / vtxLods=7` are counted, not built; every drawn stem in §2.3 ends in `_red`.
7. **Each D2 sample is one body at one tick.** The nine classes are covered by one body each; a second instance of the same class is not measured (snakewater frame 62000 alone draws two Scouts and two Demomans; upward draws both teams), and no other tick or action is covered.
8. **No animation matches the server.** Entities carry no `m_nSequence` (`status=missing reason=entity carried no m_nSequence`) and no `m_nModelIndex` (`missingModelIndex=39/39, 20/20`). The pose is the product's own pick from `m_vecVelocity` and the playback tick; fidelity is never claimed.
9. **Default run selection is legacy; crouch/flinch are measured only in opt-in states mode.** The default `--animation-drive legacy` path still chooses idle or moving through `choosePlayerAnimationSequence`, preserving the old behaviour. Explicit `--animation-drive states` selects crouch/crouchwalk from `m_fFlags`/`m_bDucked`/`m_bDucking`/`m_flDucktime` and flinch from an `m_iHealth` drop through `native/include/animation_sequence_match.h` (`RUNS-2026-11-04.md` §24.1). The repaired live gate reads `ANIMATION-DRIVE-STATES=PASS` / `failures=0`, 24 `state=flinch` lines, and distinct states/legacy crouch pixels at tick 14765; the flinch tick's identical pixels are expected because that body is off screen (§25.3). Attack remains unverified: `AttackStand_PRIMARY` can appear as an idle pick, but no driven attack state exists. Ragdoll remains out of scope. The death clause is measured separately by T66 and the corpse-filter repair is recorded in §25.2. None of this claims server `m_nSequence`/`m_nModelIndex` fidelity.
10. **Third-person weapon attachment now covers all nine classes, still one weapon and one pose per class.** Every class has one measured weapon-on-body frame (§2.4; `THIRD-PERSON-WEAPON=PASS` on both recordings, `RUNS-2026-11-04.md` §26). What it does not cover: other weapons in the same class, other poses, and reload/draw animation; and the renderer's 96-mesh entity cap is now saturable by the attach meshes, so three non-target weapons are refused (`RUNS-2026-11-04.md` §26.3). First-person viewmodel coverage is still `c_pro_rifle` / `CTFSniperRifle` / 2 materials / `capture_tick=2000`.
11. **`boneMismatch` on players is explained, not fixed.** `24155` with the drive on (`evidence/t61-animation/r10-after-demos/pixels/animated.summary:44`) vs `0` off (`evidence/t61-animation/r10-after-demos/pixels/off.summary:4`); T61 §7 (`evidence/t61-animation/T61-ANIMATION-REPORT.md:229-231`, quoting an older run's `34800`) calls it counter arithmetic, and no player gate asserts zero.
12. **Ragdoll and video export are out of scope**, recorded here as absences, not as passes.

---

## 6. Evidence index used above

Paths are relative to the worktree this refresh read (`D:/TF2_Native_Worktrees/t79-integration`); entries that predate 2026-11-04 were written from the 2026-10-29 checkout at `D:/TF2_Native_Test/` and are quoted as committed.

| Path | What it contains |
|---|---|
| `player-model-check.sh` (298 lines) | T60 static gate: 9-class model audit, 4 named negatives, demo reachability |
| `player-animation-check.sh` | T61 gate: animation table, multi-frame pose, continuity, clock, demo ticks, matcher and entity-id checks |
| `player-animation-pixels.sh` | T61 pixel gate: snakewater `64161`, debug camera, mask/body/A-B identity |
| `weapon-body-check.sh`, `weapon-material-check.sh` | First-person weapon body/material gates, POV demo, ticks `2000` / `16306` |
| `evidence/t57-player-model/runtime/t60/player-model-audit.txt` | 10 lines: per-class model audit plus summary at `:10` |
| `evidence/t57-player-model/runtime/t60/player-model-anim.txt` | 9 animation-source lines (`animSeqs`, `boundSeqs`, `bindBones`) |
| `evidence/t57-player-model/runtime/t60/player-model-demo-bagel.txt`, `player-model-demo-snakewater.txt` | Per-entity class fallback, tick fidelity, `playerclass` histogram |
| `evidence/t57-player-model/runtime/REPORT-T60.md` | T60 verdicts, negative controls, mutations, "static load only" caveat |
| `evidence/t57-player-model/REPORT.md` | T57: third-person draw path (`native_renderer.cpp:5994`, `:6096` `DrawInstanced`), §8 NOT VERIFIED list, planned third-person capture |
| `evidence/t61-animation/r10-after-demos/runtime/class-<mdl>.txt` (9 + repeat) | Current per-class run pick, four pose samples, continuity sweep and clock rate |
| `evidence/t61-animation/r10-after-demos/check.log` | Forward animation gate result, 232 OK / 0 FAIL |
| `evidence/t61-animation/runtime/demo-ticks.txt` | 8 per-entity demo ticks plus aggregate window at `:33` |
| `evidence/t61-animation/r10-pixel-mutations/baseline.log` | Demoman `id=95` identity/pick/sample/pixel chain, frame and scene ticks, 28/0 pixel gate |
| `evidence/t61-animation/T61-ANIMATION-REPORT.md` | T61.1 stages, matcher and entity identity, mutation and recovery evidence, residual limits |
| `evidence/weapon-material/natural-on.readings.txt` | `weaponBody model=… class=CTFSniperRifle`, draw calls, materials `2/2/2/2` |
| `native/src/entity_model.cpp:589-602` | tfClass → model path |
| `native/src/main.cpp:191-247`, `:9626`, `:9689` | Sequence chooser, stored label, `lastReason` |
| `T62-REPORT.md` §0 | T62 scope, acceptance, "no release verdict", T55 deferred (inlined verbatim there; the task list was deleted in the 2026-10-29 cleanup) |
| `class-pixels-check.sh` | Per-class D2 driver: one playback tick per class, camera derived from that body's origin, grades the `BODY` line of the pixel gate, and asserts that two captures of the same `identity` frame are byte-identical |
| `evidence/t62-matrix/class-pixels/<class>/` | Per class: `animated` / `off` / `identity` / `identity-2` frames, four summaries, `entity-id.txt`; the last block of each summary and of `entity-id.txt` is a `frame:` provenance record (frame sha256s, product sha256, exact request); `targets.txt` records each class's tick and origin and why it changed |
| `evidence/t62-matrix/RUNS-2026-11-04.md` | This refresh: tree and binary identity, six gate runs with commands/rc/log hashes, pixel frame hashes, per-class chain, what the runs do not show |
| `native/tools/t61-entityid.py` | Pixel-gate grader; the `BODIES` block prints one line per drawn player body and is what the per-class chain grades |
| `player-body-appearance-check.sh`, `player-body-appearance-pixels.py` | Team-colour gate: two captures per body (entity-id mask, then painted), asserts team/family, mask floor, tint direction and the same-model red/blu gap |
| `evidence/t62-matrix/body-appearance/` | Per target: `identity.bmp` (id view), `painted.bmp`; `targets.txt` records tick, ent and origin |
| `map-material-matrix.sh`, `evidence/t62-map-matrix/` | Five maps × two ticks of the world-material pipeline, asserted per row; see `MAP-MATRIX.md` |
| `evidence/t62-matrix/RUNS-2026-11-04.md` §9, §9c | The second refresh: eight gate runs at `65315d2` with log hashes, the team-colour readings, and the T66 relay gate run at `74e946b` |
| `evidence/t62-matrix/RUNS-2026-11-04.md` §10 to §14 | The fourth and fifth passes: the five-map geometry/lighting gate, the Highlander per-class chain, the Pyro fix, and the freeze sweep that re-ran build plus ten gates at `931588b` with every log hashed |

**Still missing, and limiting broader claims:** a class/model field on every aggregate demo-animation tick (the entity chain covers only the named samples); a real Heavy player in any recording (only the class-fallback body appears as Heavy); a driven attack action rather than an idle `AttackStand_PRIMARY` pose; ragdoll, which remains out of scope; a BLU material *stem* named by a draw (the BLU body is measured in pixels, not by stem); and a team-colour pair for Scout and Sniper (both are single-team: their red bodies exist in the ledger and are fully occluded). Crouch/crouchwalk/flinch are no longer unmeasured, but only under explicit `--animation-drive states`; the default remains legacy (`RUNS-2026-11-04.md` §24.1 and §25.3). The upward team-colour run has 16 bodies and seven pairs, not nine two-team completions (§2.3).

> **Superseded, annotated 2026-11-07 (task-6). History kept, not deleted.** This paragraph was written on the pass-7 tree; three of its items were closed by later committed evidence, while the rest still hold:
> 1. "a real Heavy player in any recording" — `:313` records a real Heavy body (ent=11, blue, `AttackStand_SECONDARY`); the old ent=7 note is snakewater-only and is a class-fallback body.
> 2. "a team-colour pair for Scout and Sniper" — `:178-179` and `RUNS-2026-11-04.md` §26.5 give nine pairs; the Scout pair is tick 45001 (red ent=12 vs blu ent=14) and the Sniper pair is tick 45001 (red ent=15 vs blu ent=17).
> 3. "16 bodies and seven pairs, not nine" — superseded by the 18-target two-batch run (§26.5; evidence `evidence/t62-matrix/body-appearance-upward/gate-two-team-2026-11-07.log`).
> Still true: no class/model field on every aggregate demo tick, no driven attack, no BLU stem named by a draw, ragdoll out of scope.
>
> **Line-number correction:** the retired `/d/_shared/postfreeze-inventory.md` cites this paragraph as `:422`; at this tree `:422` is an evidence-index row and this paragraph is `:438`. Numeric canon for citations: `evidence/t62-matrix/POSTFREEZE-INVENTORY-CORRECTED.md` §3.

The pass after `931588b` re-ran the per-class pixel chain with the identity capture taken twice per class, so the chain now also asserts determinism: the two identity frames are byte-identical on every class (`demo e346a723..., scout 7317b7a2...`) even though the pixel counts the chain reports come from runs that earlier disagreed (`RUNS-2026-11-04.md` §17). It also added `bone-mismatch-semantics-check.sh` (`BONE-MISMATCH-SEMANTICS`, §16), which pins what the trace counter counts rather than a value, and re-ran the five-map geometry/lighting gate under the corrected hole criterion (§15, §19).
