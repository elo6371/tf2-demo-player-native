# CHARACTER-MATRIX — focused visual matrix over the existing scope (T50–T61)

**HEAD this was written against:** `047bcfbdb89225c1c054535dc3e51bc44b9cf3b1` — branch `p0-entity-protocol`, subject *"Merge T59: world materials through VMT patch includes"*. Written 2026-10-25 for T62 (`NEXT-TASKS-2026-10-23.md` §11).

**Rule of this document: no gate was run to produce it.** Nothing was built, launched, captured or mutated, and the host lock was not touched. Every PASS / FAIL / NOT VERIFIED / UNSUPPORTED below quotes a line that already exists in a committed evidence file at the HEAD above. Where a cell rests on a gate *script* that was read but not executed here, the script path and line are given instead of a reading.

**Working tree at reading time:** dirty, and not by this document — `M NEXT-TASKS-2026-10-23.md`, `M evidence/t68-events/runs/*.txt`, `M evidence/t70-controls/gate/*`. No file was modified while writing this matrix except this one.

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
| Scout | PASS | PASS | UNSUPPORTED | PASS | NOT VERIFIED |
| Sniper | PASS | PASS | UNSUPPORTED | PASS | NOT VERIFIED |
| Soldier | PASS | PASS | UNSUPPORTED | PASS | NOT VERIFIED |
| Demoman | PASS | PASS | UNSUPPORTED | PASS | NOT VERIFIED |
| Medic | PASS | PASS | UNSUPPORTED | PASS | NOT VERIFIED |
| Heavy | PASS | PASS | UNSUPPORTED | PASS | NOT VERIFIED |
| Pyro | PASS | PASS (thinnest: 1 slot) | UNSUPPORTED | PASS | NOT VERIFIED |
| Spy | PASS | PASS | UNSUPPORTED | PASS | NOT VERIFIED |
| Engineer | PASS | PASS | UNSUPPORTED | PASS | NOT VERIFIED |

Nothing in the two measured columns is a rendered frame — see §4.2.

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

- Every drawn stem above is a **RED** stem. `redBluDistinctSlotsTotal=14` only proves the resolver *finds a different* stem for BLU in 14 slots; no BLU material was bound to a draw in any committed frame.
- Pyro has **one** material slot, hence `paintedSlots=2` and `headStem=-`: the weakest paint evidence of the nine, and no head material at all.
- Heavy's body stem is a weapon-skin family (`hvyweapon_red_sheen`) and Spy has `skinFamilies=26`. Both are asserted green and both sit on T57's own NOT VERIFIED list (`evidence/t57-player-model/REPORT.md:394` §8: *"Heavy/Spy head-paint look"*).

### 2.4 C — weapon body in third person. 9/9 UNSUPPORTED

Three weapon gates exist, and all three are first-person:

- `weapon-body-check.sh:102,108,112` — `POV_DEMO="$TF/demos/autorecord_2026-07-02_13-26-46.dem"`, `VM_MODEL="models/weapons/v_models/v_scattergun_scout.mdl"`, `ARM_MODEL="models/weapons/c_models/c_sniper_arms.mdl"`, `CHAIN_TICK=16306`.
- `weapon-material-check.sh:76,82,85,86` — same demo, `NATURAL_BODY="models/workshop/weapons/c_models/c_pro_rifle/c_pro_rifle.mdl"`, `OVERRIDE_BODY="models/weapons/c_models/c_tfc_sniperrifle/c_tfc_sniperrifle.mdl"`.
- Committed reading `evidence/weapon-material/natural-on.readings.txt:5`: `gpu: viewmodel weaponBody model=…/c_pro_rifle.mdl class=CTFSniperRifle entity=1008 worldModelIndex=533 … sequences=1 bones=1 vertices=12681 materials=2/2/2/2 …` and `:6` `armDrawCalls=26 weaponDrawCalls=26 … weaponVisible=1 weaponReason=drawn`.

That is one Sniper rifle, two materials, in the viewmodel pass, on one POV demo at `capture_tick=2000`. Nothing draws a weapon **attached to a third-person player body**, and no gate asserts a per-class weapon. The only weapon strings in the class audit are material families living inside the player `.mdl` itself (`hvyweapon_red_sheen`). Hence UNSUPPORTED for all nine — absence of scope, not failure.

### 2.5 D1 — animation data, pose and clock (counter level). 9/9 PASS, with a caveat

Gate `player-animation-check.sh` (T61), probe `native/build-nmake/model_pose_probe.exe`, per-class logs `evidence/t61-animation/runtime/class-<mdl>.txt`. Gate totals: 213 OK / 0 FAIL, mutations m1–m5, three named negatives at rc=2 (`tf-root-invalid`, `model-missing`, `sequence-out-of-range`).

Anchor, `class-scout.txt:1`:

> `animtable model=models/player/scout.mdl animSource=models/player/scout_animations.mdl separate=1 bones=78 bindBones=78 validBindBones=78 sequences=306 idleSeq=6 idleLabel=PRIMARY_aimmatrix_idle movingSeq=7 movingLabel=PRIMARY_aimmatrix_run poseSeq=24 poseLabel=PRIMARY_airwalk_reload_start poseFrames=21`

B — four sampled poses inside the bind radius, `class-scout.txt:2` and the four `animpose` lines:

> `boundDiag bindRadius=73.534996 boundDiag=148.069992 bones=76 composed=1 reason=-`
> `animpose cycle=0.0000 frame=0/21 checksum=1f7cb059b8f3968d maxDelta=69.670395 finite=1` … `cycle=0.7500 frame=15/21 checksum=3c6c1b9fbb24a32b maxDelta=68.860672 finite=1` (four distinct checksums)

C — continuity, `class-scout.txt`:

> `animcont sequence=208 steps=60 tickRate=60.000 speed=125.000 rate=0.500000 finalCycle=0.499999851 maxStepDelta=13.934902 worstBone=61 worstElement=12 worstTick=59 usable=1`

D — rate, printed for all nine and asserted on Scout only, e.g. `class-heavy.txt`:

> `animrate idleRate=0.000000 movingRate=1.000000 idleOneSecond=0.000000000 movingOneSecond=0.999999702 expected=1.000000000`

**The caveat that matters:** the sequence C advances is not a locomotion sequence. The probe prints the label it swept, and it is a swim, taunt or menu animation in every case:

| Class | idle seq / label | moving seq / label | continuity seq / label (frames) | sweep delta | boundDiag | B pose seq / label (frames) |
|---|---|---|---|---|---|---|
| Scout | 6 `PRIMARY_aimmatrix_idle` | 7 `PRIMARY_aimmatrix_run` | 208 `swim_ITEM2` (31) | 13.934902 | 148.069992 | 24 `PRIMARY_airwalk_reload_start` (21) |
| Sniper | 6 | 7 | 199 `s_swimAlign_ITEM1` (61) | 7.227989 | 168.444580 | 34 `ReloadAirwalk_SECONDARY` (41) |
| Soldier | 6 | 7 | 199 `s_swimAlign_LOSER` (61) | 7.921024 | 162.180664 | 22 `ReloadAirwalk_PRIMARY` (33) |
| Demoman | 6 | 7 | 138 `swim_MELEE` (31) | 11.252254 | 153.344421 | 24 `a_PRIMARY_reload_airwalk` (51) |
| Medic | 6 | 7 | 79 `swim_LOSER` (31) | 11.527103 | 158.545471 | 16 `ReloadAirwalk_PRIMARY` (46) |
| Heavy | 7 | 8 | 224 `selectionMenu_Anim01` (72) | 13.546520 | 160.262329 | 31 `a_SECONDARY_reload_airwalk` (46) |
| Pyro | 6 | 7 | 173 `layer_taunt_scorch_shot` (114) | 32.513805 | 144.647598 | 47 `layer_reload_flaregun_airwalk_ITEM1` (61) |
| Spy | 7 (`SECONDARY_aimmatrix_idle`) | 8 (`SECONDARY_aimmatrix_run`) | 144 `swim_LOSER` (31) | 12.674725 | 152.491623 | 16 `layer_reload_airwalk_arms_SECONDARY` (41) |
| Engineer | 6 | 7 | 252 `s_swimAlign_ITEM2` (61) | 7.102366 | 153.530396 | 24 `a_PRIMARY_reload_airwalk` (76) |

So D1 proves: the animation table loads and binds, four sampled poses are distinct and bounded, and a clock advances a cycle without exploding the skeleton — on swim/taunt/menu sequences. It does not prove any locomotion animation.

### 2.6 D2 — animation on a real demo tick and on screen, per class. 9/9 NOT VERIFIED

Both the demo-tick and pixel readings exist, and both are class-blind.

`evidence/t61-animation/runtime/demo-ticks.txt:33`:

> `animticks mode=demo window=60000,62000 transitions=39 entities=13 movingTicks=39 movingAdvanced=26 idleTicks=0 idleAdvanced=0 staleTicks=1997 maxMovingDelta=11.905984879 maxIdleDelta=0.000000000`

Its eight per-tick lines name entities only, e.g. `animtick mode=demo tick=60941 entity=1 … speed=7.090 state=moving` … `entity=8 … speed=92.541 state=moving`. Not one line names a model, a class or a team.

Pixels — the only rendered character evidence in the tree (`player-animation-pixels.sh:39,43,45`: demo `bb841c6d…cp_snakewater_final1.dem`, `TICK=64161`, `--debug-camera 5363.8 -1102.1 5.9 4.1 0.0`):

> `MASK pixels=6876 of 860784 (0.80%) sentinel=(255,0,255)` — `pixel-check.txt`
> `BODY over mask: distinct=1660 mean_lum=43.9 pure_black=0.148` — `pixel-check.txt`
> `animation-off vs animated over mask: differ=5665 (82.39%) differ_by_>=24=3580 (52.07%)` — `pixel-check.txt`
> `PIXEL-CHECK=PASS` — `pixel-check.txt` (19 OK / 0 FAIL)

and the frame's own identity, `pixels/animated.summary:5`:

> `scene: anim skinned=67 bindPose=29 matrices=1339 gpuSkin=66/1339 boneMismatch=71366 poseChanges=5345 targetInstances=2 targetPose=3 advances=4931 targetAdv=3 cycle=187675/174216 tick=74481 status=ok reason=ReloadAirwalk_PRIMARY`

Four separate reasons this cannot be attributed to a class:

1. The mask is the union of the whole entity pass (`sentinel=(255,0,255)`), 6876 px — 0.80% of the frame — with `targetInstances=2`, while the snapshot held 14 players (`animsum: tick=74481 players=14 moving=7 idle=7 advanced=7 skipped=0 maxDeltaCycle=0.856742`).
2. The drive-off and coverage runs record `status=missing reason=entity carried no m_nSequence`: the entity carried no sequence sendprop at all, so the animated label is the product's own pick.
3. `boneMismatch=71366` is a counter artefact, not a measured pose defect (T61 §7: it compares `skinBones` against `poseToBone.size()`, `main.cpp:9113-9117`); no player gate asserts it to be zero.
4. The mask was captured with the drive off — the bind-pose body — so the measured region is a subset of the animated one (T61 §6.1).

The one join that would name classes is an inference and is therefore not used: the demo-tick lines run at bagel tick `60941` on entities `1..8`, while the class-fallback audit rows are from the same recording at requested tick `60001`; if entity indices were stable across those 940 ticks, the eight entities would be {soldier, medic, medic, scout, soldier, demo, scout} plus one unresolved. T57 lists *"bagel 每 tick 的 CTFPlayer 实体数、team 分布"* as NOT VERIFIED (`evidence/t57-player-model/REPORT.md:265`), so the join stays an inference and the cells stay NOT VERIFIED.

---

## 3. The ACTION axis

Rows are the action names the animation gate actually reads, in its own words. No row below was measured on screen for a named class.

| # | Action / pose | Sequence(s) the gate reads | Classes | Demo / tick | What is actually measured | Status | Evidence |
|---|---|---|---|---|---|---|---|
| 1 | idle | `PRIMARY_aimmatrix_idle` (seq 6; 7 for Heavy/Spy) | all 9 | none — `idleTicks=0` | `idleRate=0.000000 idleOneSecond=0.000000000` at clock level; no idle tick ever occurs in a recording | **PASS (clock)** / **NOT VERIFIED (demo)** | `class-*.txt` `animrate`; `demo-ticks.txt:33` |
| 2 | run (moving) | `PRIMARY_aimmatrix_run` (seq 7; 8 for Heavy/Spy) | all 9 | bagel 60000–62000 (aggregate) | `movingRate=1.000000 movingOneSecond=0.999999702`; the sequence is a **1-frame aim pose** per `main.cpp:218-221` | **PASS (clock)** / **NOT VERIFIED (cycle)** | `animrate` lines; `main.cpp:191-247` |
| 3 | walk | needle `walk` in `kMovingNeedles` | — | — | nothing samples a walk cycle; the needle matches `airwalk` instead (§3.1) | **NOT VERIFIED** | `main.cpp:195` |
| 4 | swim / taunt / menu (the C sweep) | 208 `swim_ITEM2`, 199 `s_swimAlign_*`, 138 `swim_MELEE`, 79 `swim_LOSER`, 224 `selectionMenu_Anim01`, 173 `layer_taunt_scorch_shot`, 144 `swim_LOSER`, 252 `s_swimAlign_ITEM2` | all 9 (one sequence each) | none | `0<maxStepDelta<=boundDiag/4`, `usable=1`, `finalCycle=0.499999851` | **PASS** (pose stability only) | `class-*.txt` `animcontseq` / `animcont` |
| 5 | airwalk / reload pose | pose seq 24 / 34 / 22 / 24 / 16 / 31 / 47 / 16 / 24, `poseFrames` 21–76 | all 9 | none | 4 sampled cycles, 4 distinct checksums, `maxDelta <= boundDiag` | **PASS** (sampling only) | `class-*.txt` `animpose` |
| 6 | **what is drawn** | `ReloadAirwalk_PRIMARY` | not identified | snakewater 64161 (frame tick), scene tick 74481 | the animated frame's label plus 82.39% of mask pixels changing between drive on/off | **PASS (a body moves)** / class unknown | `pixels/animated.summary:5`, `pixel-check.txt` |
| 7 | crouch | labels exist: `PRIMARY_aimmatrix_crouch_idle`, `SECONDARY_aimmatrix_crouch_walk`, `ReloadCrouch_PRIMARY` | 0 of 9 sampled | none | nothing | **NOT VERIFIED** | label list, `class-scout.txt:1` |
| 8 | attack | labels exist: `AttackStand_PRIMARY`, `AttackCrouch_PRIMARY`, `attackstand_Primary_deployed_fire` | 0 of 9 sampled | none | nothing | **NOT VERIFIED** | same label list |
| 9 | flinch | label `a_flinch01` | 0 of 9 sampled | none | nothing | **NOT VERIFIED** | same label list |
| 10 | ragdoll / death | labels `ragdoll`, `ragdollspawn` in every table | 0 of 9 | none | nothing; excluded by task scope | **UNSUPPORTED** | T62 §11 |
| 11 | video cut/export (T55) | — | — | — | not implemented, not accepted | **UNSUPPORTED** | T62 §11 step 4 |

### 3.1 Why the drawn action is `ReloadAirwalk_PRIMARY` and not a run cycle

`D:/TF2_Native_Test/native/src/main.cpp:191-247` picks the sequence, and it matches substrings:

> `static const char* const kMovingNeedles[] = {"run", "walk", "move"};`
> `static const char* const kIdleNeedles[] = {"idle", "stand"};`

with a documented preference for a candidate that actually has frames (`main.cpp:218-221`):

> `// A label match can still be a one-frame pose: PRIMARY_aimmatrix_run is`
> `// a static aim pose with frameCount 1, and cycling through it would`

`main.cpp:9626` stores the choice — `runtime.sequenceLabel = runtime.moving ? choice.movingLabel : choice.idleLabel;` — and `main.cpp:9689` copies it into `animationStats.lastReason`, which is how `reason=ReloadAirwalk_PRIMARY` reached the pixel summary. "Air**walk**" contains the moving needle `walk` and it has frames, so a moving player is drawn playing an airwalk-reload pose. That is real, quotable product behaviour; it is not a measured claim that walk animation works.

---

## 4. How many cells are actually measured

### 4.1 Counts, computed from §2.1

**9 classes × 5 axes = 45 cells: 27 measured (PASS), 9 NOT VERIFIED, 9 UNSUPPORTED, 0 FAIL.**

| Axis | PASS | NOT VERIFIED | UNSUPPORTED |
|---|---|---|---|
| A model load | 9 | 0 | 0 |
| B materials/bodies | 9 | 0 | 0 |
| C weapon body (3rd person) | 0 | 0 | 9 |
| D1 anim data/clock | 9 | 0 | 0 |
| D2 demo tick + pixels | 0 | 9 | 0 |
| **Total** | **27** | **9** | **9** |

Against the four-axis shape the brief illustrated (model load / materials / weapon body / animations) the same evidence reads **36 cells: 18 measured, 9 NOT VERIFIED, 9 UNSUPPORTED**. The fifth axis only makes explicit that the animation claim splits into a counter-level part that exists (D1) and a demo/render part that does not (D2). Use whichever number matches the report you are comparing to; the underlying 27/9/9 does not change.

### 4.2 The number that actually matters: 0 of 27 measured cells come from a rendered frame

All 27 measured cells are table/probe readings — model files, animation tables, sampled matrices. The single rendered proof, `PIXEL-CHECK=PASS` with 19 assertions, is one camera pose at one tick on an unlabelled union mask, so it cannot fill any per-class cell. No committed capture draws a **named class** as a third-person player body, and T57's planned third-person capture (`evidence/t57-player-model/REPORT.md:388`: `--settings-file raw/thirdperson.ini`, `--capture-tick 64230,64261,64292`, `~150 s`) was never executed.

### 4.3 Cheapest next test for the largest NOT VERIFIED block (D2, 9 cells)

Root cause: the demo-tick and scene trace lines carry entity ids and no class/model. Two options, cheapest first.

**Option 1 — no rebuild, no product launch, ~5 minutes, one probe run.** Run the already-used `entity_overlay_probe` against the same snakewater recording at `64161` (the exact frame tick of the committed `animated.bmp` / `coverage.bmp`) and diff its class-fallback rows against the pixel runs. It cannot create a PASS; it converts "which classes were even in the frame" from unknown into a bounded statement ("the frame contained N players of classes X…"), which is precisely why the cells are NOT VERIFIED today. Same call shape as the T60 rows in `evidence/t57-player-model/runtime/t60/player-model-demo-snakewater.txt`.

**Option 2 — one rebuild plus one run, ~25–30 minutes, the only thing that can turn D2 green.** Add a `model=` (or class-fallback) field to the `animtick mode=demo` lines and to the `scene: anim` summary (`native/src/main.cpp`, the animtick demo writer and the `animationStats` summary), then rerun `player-animation-check.sh` (213 OK pinned; `class-scout.txt` demo tokens prove the writer already visits demo ticks) and `player-animation-pixels.sh` at `T61_PIXEL_TICK=64161`. Expected outcome: D2 becomes PASS for the classes the two recordings actually contain — **Scout, Soldier, Demoman, Medic, Heavy = 5 cells** — at aggregate-tick level. The remaining 4 (Sniper, Pyro, Spy, Engineer) need a **different recording**, not a different test: neither committed demo ever plays them.

**The honest per-class pixel test, if the matrix is meant to be pixel-level:** one product launch per class (~8–12 min each, capture plus the on/off compare), camera aimed so only that class is on screen and the mask restricted to that entity. Cost for the 5 demo-reachable classes: roughly 1 hour of host-lock time; the other 4 cannot be done at all without a new recording.

---

## 5. What this matrix cannot claim

1. **Not a release verdict.** T62 step 5: *"本任务不出最终 release verdict"*. A green grid here plus green gates still does not mean the picture is right.
2. **No per-class rendered cell exists.** 0 of 27 measured cells come from a frame (§4.2); the only rendered PASS is class-unlabelled.
3. **One map, one tick, one camera.** The pixel proof is snakewater `64161` with `--debug-camera 5363.8 -1102.1 5.9 4.1 0.0`. Nothing generalises to another tick, camera or map.
4. **The mask is a bind-pose subset.** Captured with the drive off (`status=missing`, `bindPose=43`), so the animated body extends past the measured region (T61 §6.1).
5. **The "time" proof is not in the committed gate.** The `64161 → 64251` difference (11,058 of 12,035 px, 91.88%) came from an earlier round with a different mask; the committed gate compares drive-on vs drive-off at a single tick.
6. **Only LOD0 is built, and only RED is bound.** `vvdLods=7 / vtxLods=7` are counted, not built; every drawn stem in §2.3 ends in `_red`.
7. **Four classes are static-only.** Sniper, Pyro, Spy and Engineer never appear as a played player in either recording; nothing here can be upgraded to "seen on screen".
8. **No animation matches the server.** Entities carry no `m_nSequence` (`status=missing reason=entity carried no m_nSequence`) and no `m_nModelIndex` (`missingModelIndex=39/39, 20/20`). The pose is the product's own pick from `m_vecVelocity` and the playback tick; fidelity is never claimed.
9. **What is drawn is not a run cycle** (§3.1). Airwalk-reload is what a moving player shows; no gate asserts otherwise, and none measures crouch, attack, flinch or death.
10. **Weapon coverage is first-person only, one weapon, one demo.** `c_pro_rifle` / `CTFSniperRifle` / 2 materials / `capture_tick=2000`.
11. **`boneMismatch` on players is explained, not fixed.** `71366` with the drive on vs `0` off; T61 §7 calls it counter arithmetic, and no player gate asserts zero.
12. **Ragdoll and video export are out of scope**, recorded here as absences, not as passes.

---

## 6. Evidence index used above

| Path (under `D:/TF2_Native_Test/`) | What it contains |
|---|---|
| `player-model-check.sh` (298 lines) | T60 static gate: 9-class model audit, 4 named negatives, demo reachability |
| `player-animation-check.sh` (308 lines) | T61 gate: A table, B pose, C continuity, D rate (Scout only), E ticks (Scout only) |
| `player-animation-pixels.sh` | T61 pixel gate: snakewater `64161`, debug camera, mask/body/A-B identity |
| `weapon-body-check.sh`, `weapon-material-check.sh` | First-person weapon body/material gates, POV demo, ticks `2000` / `16306` |
| `evidence/t57-player-model/runtime/t60/player-model-audit.txt` | 10 lines: per-class model audit plus summary at `:10` |
| `evidence/t57-player-model/runtime/t60/player-model-anim.txt` | 9 animation-source lines (`animSeqs`, `boundSeqs`, `bindBones`) |
| `evidence/t57-player-model/runtime/t60/player-model-demo-bagel.txt`, `player-model-demo-snakewater.txt` | Per-entity class fallback, tick fidelity, `playerclass` histogram |
| `evidence/t57-player-model/runtime/REPORT-T60.md` | T60 verdicts, negative controls, mutations, "static load only" caveat |
| `evidence/t57-player-model/REPORT.md` | T57: third-person draw path (`native_renderer.cpp:5994`, `:6096` `DrawInstanced`), §8 NOT VERIFIED list, planned third-person capture |
| `evidence/t61-animation/runtime/class-<mdl>.txt` (9 + repeat) | `animtable`, `boundDiag`, 4×`animpose`, `animcontseq`, `animcont`, `animrate`, `animticks` |
| `evidence/t61-animation/runtime/demo-ticks.txt` | 8 per-entity demo ticks plus aggregate window at `:33` |
| `evidence/t61-animation/pixels/pixel-check.txt`, `animated.summary`, `off.summary`, `coverage.summary` | Mask/body/diff readings, scene identity, drive-off traces |
| `evidence/t61-animation/T61-ANIMATION-REPORT.md` | 382 lines: stages, mutations, §8.4 limits, §9 NOT VERIFIED |
| `evidence/weapon-material/natural-on.readings.txt` | `weaponBody model=… class=CTFSniperRifle`, draw calls, materials `2/2/2/2` |
| `native/src/entity_model.cpp:589-602` | tfClass → model path |
| `native/src/main.cpp:191-247`, `:9626`, `:9689` | Sequence chooser, stored label, `lastReason` |
| `NEXT-TASKS-2026-10-23.md` §11 | T62 scope, acceptance, "no release verdict", T55 deferred |

**Missing from the tree, and blocking a stronger matrix:** a third-person camera capture of a named class (T57 §7 step 4, never run); a per-tick class/model field on the demo animation trace; any recording containing Sniper, Pyro, Spy or Engineer as a player; any measurement of crouch, attack, flinch, death or ragdoll; any BLU-team draw.
