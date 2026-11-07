# T62 — focused visual matrix, evidence refresh

Original assessment: 2026-10-25 at `047bcfb`. First evidence refresh: 2026-10-29 against candidate `task/repo-cleanup @ 2f14873` (not frozen). Second refresh: 2026-11-04 at `integration/t79-final @ 046f4cd52a6eae5eb952488bb0095bcce86500b1`; third pass the same day at `65315d27b10d416a46680123c6719416c11d6541`, which adds the team-colour gate and turns the map matrix into a gate; fourth pass the same day at `931588b1dc8fc14ec0775be53dc47ba217e1a94f`, which adds the five-map geometry/lighting gate, the per-class Highlander chain and the Pyro animation fix (none of these frozen).
Deliverables: `MAP-MATRIX.md`, `CHARACTER-MATRIX.md`, this report. **Eight product gates were run at `65315d2` and all eight pass** — the six character gates, the team-colour gate (`BODY-APPEARANCE`) and the five-map material matrix (`MAP-MATERIAL-MATRIX`). A ninth, the T66 relay gate, was run at `74e946b` and passes as well (`SPECTATE-CHECK`, 95 assertions), which is what backs the death clause. The fourth pass at `931588b` adds two more runs: the five-map geometry/lighting gate (`MAP-GEOMETRY-LIGHTING`, 50 captures) and the probe-only pose-family gate (`POSE-FAMILIES`, 40 sampled clips). With those, no map cell is NOT VERIFIED and no D2 cell is either. A fifth pass at the same commit then re-ran everything from one driver: ten gates plus the build, all green, no assertion relaxed (`RUNS-2026-11-04.md` §14). Commands, verdicts, tree identity and log/frame hashes: `RUNS-2026-11-04.md`.
A sixth pass (the worktree tip is `72d42b8`; 2026-11-04 evening) closed instrument questions rather than adding cells: the geometry/lighting gate was rebuilt around a four-capture flat-colour lattice and a point oracle after the sky-brush mask was shown to measure the map construction instead of the renderer (`RUNS-2026-11-04.md` §15, §19); the per-class pixel chain now takes each identity frame twice and asserts the two are byte-identical (§17); `bone-mismatch-semantics-check.sh` pins what the `boneMismatch` counter counts rather than a value (§16); and the team-colour gate was re-measured on a Highlander recording where both teams are on screen for all nine classes -- 16 bodies, seven same-class pairs (§18, `CHARACTER-MATRIX.md` §2.3).
No cell changes status in that pass: the map cells were already PASS at `931588b` and stay PASS under the stricter criterion, the nine third-person weapon cells stay UNSUPPORTED and the open list in `CHARACTER-MATRIX.md` is unchanged (those nine cells are closed by the eighth pass below).

A seventh pass (2026-11-06, `b859dd8`, product sha256 `891bfd7a0e7cc412fe3912c6f4deb4ca57e36618bfa31d59d06f5a5370b4f11c`) is not a new sweep of readings but the repair of the one red §24.6 left behind: the spectate corpse filter read the *previous* frame's dead set, which made the demo target's picture depend on the wall clock (2218 px apart at one tick). The filter now reads the frame's own slot snapshot before the draw, and the per-class gate's two captures are byte-identical again — four new identity hashes, all four BODY readings still pass (`RUNS-2026-11-04.md` §25.2). An eighteen-step freeze on that commit re-ran the whole set: sixteen green, and the two reds were the animation-drive gate's own two defects and the freeze script handing the spectate gate an absolute output path; both were repaired and re-run to `ANIMATION-DRIVE-STATES=PASS` (15:49:19 local) and `SPECTATE-CHECK=PASS` (`RUNS-2026-11-04.md` §25.3-§25.4). After that pass the grids stood where §24.8 left them — character 40 PASS / 5 UNSUPPORTED, map 20 PASS / 0 UNSUPPORTED, no known red gate — and both are superseded by the eighth pass below.

An eighth pass (2026-11-07, product sha256 `a3c93d5755fd331351766dfb991fe17f90c4dfb753ad7ef9f53391f0303724e9`) closes the five C cells §24.8 left open, and it does so by fixing the product rather than by widening an assertion. The cause was a hard budget: the prepared entity meshes stop at 64 (`native/src/main.cpp:3695,3730`), so a weapon whose entity first appears late in the recording never got a mesh and the attach path counted `noWorldMesh` silently. `supplyAttachMesh` now resolves, paints and uploads the missing weapon mesh at attach time, in its own list so the world-pass model counters other gates assert do not move (`noWorldMesh` 9 → 0; `attach: meshSource supplied=23 refused=3`). The same pass found the pixel gates had been asserting a frame the trace did not describe: the animation clock integrates wall-clock chord length, so two runs of one request can land one of two phases 11x apart in `maxDeltaCycle` and 2,129 px apart on the body. Both pixel gates now pin the phase from the `identity` capture and re-run the rest until it matches, treating equal-phase byte differences as the only real regression. Nine C cells now read PASS and the two team pairs `CHARACTER-MATRIX.md` §2.3 could not complete are closed by an 18-target two-team batch (`RUNS-2026-11-04.md` §26).

## 0. 任务定义（内联存档）

The task list that defined T62 (`NEXT-TASKS-2026-10-23.md` §11) was deleted from the repository by
the 2026-10-29 cleanup. Its text is reproduced verbatim below, so that this report remains the
authoritative definition of what T62 asked for and what it forbade; the "§11" citations elsewhere
in this matrix set point here.

### 原文（逐字，删除前版本）

> ## 11. T62 — 现有范围视觉矩阵与验收准备
>
> **前置：**T49.1–T61 的 blocker 已关闭或由用户明确接受为限制；T55 若按原范围实施，则必须在本任务之前完成。所有功能分支由用户整合并冻结，禁止验收过程中继续 merge/edit。
>
> **步骤**
>
> 1. 记录当前候选 HEAD；确认干净 worktree、用户证据处置状态、远端目标、host lock FREE、无产品/探针/编译进程。
> 2. 在冻结树先运行 T50–T61 直接受影响的 focused gates，一次性记录实际 assertion 与证据身份；只补缺口，不重跑已证实且身份匹配的无关 gate。
> 3. 跑地图矩阵（T56 选定地图 × 已修材料/几何）和人物矩阵（T57 可达职业 × T61 可达动作）；逐格记 PASS / FAIL / NOT VERIFIED / UNSUPPORTED 与原因。
> 4. T55 视频剪辑/导出当前明确延后；本任务不验收编码器或 MP4。报告需把该项列作未实现范围，不得据此声称完整产品功能全部完成。
> 5. 确认所有 FAIL/INVALID/NOT VERIFIED 都有 owner 和处置，不把 release gate 全绿等同“画面正确”；本任务不出最终 release verdict。
> 6. 最终 `bash verify-all.sh --quick` 移至 T73，在 SourceTV 与复盘工作区任务完成、由用户整合并冻结后恰好运行一次。
>
> **验收：**当前范围矩阵单元有证据或显式未验证理由；focused 与 full-chain verdict 分开报告；不得把本任务称为全软件最终验收。

## 1. What T62 asks for, and what it forbids

T62 (§0, the task definition inlined above): build a **focused** matrix over the existing T50–T61 scope,
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

**Map matrix, 5 maps × 4 axes = 20 cells: 20 PASS, 0 NOT VERIFIED, 0 UNSUPPORTED, 0 FAIL.** The fifth row changed map on 2026-11-05: `koth_bagel_rc13` (absent BSP, four UNSUPPORTED cells) was replaced by `koth_bagel_rc11`, which both map gates had already rendered — materials and coverage at `65315d2`, geometry and lighting at `931588b`, all committed in `RUNS-2026-11-04.md` §9-§10; no gate was re-run for the swap. Ten of the twenty cells are the materials and coverage cells of the five rows, asserted by `map-material-matrix.sh` at `65315d2`; nine are the five lighting cells and the geometry cells of snakewater, sunshine, bagel_rc12 and bagel_rc11, measured at `931588b` by `map-geometry-lighting-check.sh`; the twentieth is odyssey's geometry cell, which keeps its T26/T58 reading. `MAP-MATRIX.md` marks the material re-reads ‡‡ and the geometry/lighting measurements §§, and states exactly what each mark asserts.

| Map | materials | geometry | lighting | coverage table | 
| --- | --- | --- | --- | --- |
| cp_snakewater_final1 | PASS ‡‡ | PASS §§ | PASS §§ | PASS ‡‡ |
| pl_odyssey | PASS ‡‡ | PASS | PASS §§ | PASS ‡‡ |
| cp_sunshine | PASS ‡‡ | PASS §§ | PASS §§ | PASS ‡‡ |
| koth_bagel_rc12 | PASS ‡‡ | PASS §§ | PASS §§ | PASS ‡‡ |
| koth_bagel_rc11 | PASS ‡‡ | PASS §§ | PASS §§ | PASS ‡‡ |

**Character matrix, 9 classes × 5 axes = 45 cells: 45 PASS, 0 NOT VERIFIED, 0 UNSUPPORTED, 0 FAIL.** The 45 PASS are model load (9), body materials (9), third-person weapon body (9), animation data/clock (9), and nine rendered D2 samples — one body per class, four on snakewater frames 63000/64161 and five on the Highlander STV of `pl_upward_f12` at ticks 20001/24001/30001 (`CHARACTER-MATRIX.md` §2.4 and §2.6). All nine classes have measured third-person weapon attachments: Demoman, Scout, Soldier and Medic on snakewater at side yaw 45, and Pyro, Heavy, Spy, Engineer and Sniper on the Highlander STV of `pl_upward_f12` at side yaw 225 (`RUNS-2026-11-04.md` §26.4). The nine had been UNSUPPORTED because the prepared-mesh budget dropped their weapons, not because the gate lacked a target table; the fix is product-side and the evidence is one captured frame per class, not a sweep of poses. Against the brief's four axes (model / materials / weapon / animation), the same evidence reads 36 cells: 36 measured, 0 NOT VERIFIED, 0 UNSUPPORTED.

## 4. What the matrix found that is not a cell

1. **Eighteen of the 45 PASS cells are tied to a rendered player frame.** Nine are third-person weapon-on-body samples, one per class (`CHARACTER-MATRIX.md` §2.4; `RUNS-2026-11-04.md` §24.8 and §26.4). The other nine are D2 samples, one body per class: four on snakewater and five on `pl_upward_f12`, with entity IDs, ticks and current pixel readings in `CHARACTER-MATRIX.md` §4.2; the freeze6 Snakewater hashes and BODY readings are in `RUNS-2026-11-04.md` §25.2. The remaining 27 PASS cells are model, material or probe readings. The D2 evidence is nine named bodies at one tick each — not general proof for other instances, other ticks, or the other team's body of the same class.
2. **The lighting evidence is no longer the pre-clock-fix pose.** The old reading was taken at the camera the T63-F1 clock defect produced — `evidence/t36-lighting-dark-account/frames.txt:3` says `eye=-2294.8,348.8,383.4` — while `world-geometry-hole-check.sh:167-197` pins `PIN_CAPTURE_EYE="815.5,-147.8,-194.3"` for the same demo and tick. The `931588b` gate does not re-derive its eye from its own trace: it reads the demo and the camera per map from `evidence/t62-map-matrix/<map>/camera.txt` and asserts that all ten frames armed at tick 2000 with one identical `scene: camera` eye, so a moved camera now fails the gate instead of quietly changing the number. The five cameras are snakewater `1185.5,-813.5,207.1`, odyssey `-1221.2,462.2,650.3`, sunshine `-3760.0,376.0,-112.0`, bagel rc12 `533.2,-803.3,320.0` and bagel rc11 `914.0,-1000.0,563.8`.
3. **Stored world-frame pins now cover five maps and two postures.** The older Odyssey pin remains `world-geometry-hole-check.sh:178-179` (`PIN_TEXTURED_SHA=1b0ab468...`, `PIN_REF_SHA=e9218c42...`), asserted at `:712-717`. P1-2b adds ten pinned world frames per map and posture, recorded in `evidence/t62-map-matrix/<map>/frame-pins-<pose>.txt`; `RUNS-2026-11-04.md` §23 and §24.2 record 100 pins, with 100 same / 0 changed / 0 missing across the merged-product comparison. Freeze6 reconfirmed the tick-2000 rows (§25.4). Tick-0 captures are deliberately not frame-pinned, and decoded lightmap values still have no pin.
4. **cp_sunshine is gated twice now, and all four axes are covered.** `world-render-reliability-check.sh:71` runs `SUNSHINE.dem` and asserts reload errors plus six sky-face lines at tick 0 — which contradicts `evidence/t56-map-audit/gate_matrix.md:152` ("covered by no gate"). Since `65315d2` `map-material-matrix.sh` renders it at tick 2000 and asserts the material pipeline (`distinct=169 tiled=128 capped=0`, 128-row tile table), and since `931588b` the geometry/lighting gate adds a face partition of 20,449 and a dark account at eye `-3760.0,376.0,-112.0`. No sunshine axis is unmeasured any more; its sky-brush mask covers 0.4 % of the empty pixels, so the empty-pixel part of that cell is a reading, not an assertion (`MAP-MATRIX.md` §4).
5. **koth_bagel_rc12 is no longer ungated**: since `65315d2` the map gate names `tf/download/maps/koth_bagel_rc12.bsp` (26,486,963 B) and asserts its material axes, and since `931588b` the geometry/lighting gate asserts 18,235 faces, the dark-account partition and a 0.1320 % product-attributable share at eye `533.2,-803.3,320.0`. The same gate also reads bagel rc11 as a fifth map, whose BSP lives under `tf/download/maps/` too.
6. **T56's own report quotes counters its files no longer contain**: `REPORT.md:41` says
   `tiled=112 ... vmtNoBase=31`, the live `evidence/material-coverage/product-readings.txt` says
   `tiled=123 ... vmtNoBase=4 ... patchDeclared=30 patchResolved=30 transformRefused=16`; `REPORT.md:40`
   says refusals `materials=20 ... materials=3`, the live file says `materials=2 ... materials=4 ...
   materials=3`. T59 re-measured this map; the report was not re-pinned.
7. **The old `ReloadAirwalk_PRIMARY` false match is fixed.** T61.1's whole-token matcher excludes
   `walk` inside `airwalk`, prefers a multi-frame run clip and pins the resolved sample identity;
   the gate reads 233 assertions at `931588b` (232 at the T61 commit plus the new `matcher oneShot` field) and its unit mutations include matcher regressions (T61 report §8.7).
8. **D2 is complete across nine classes, one body each.** The pixel chain names one body per class — four on snakewater, five on the Highlander STV — so every class has a rendered sample with its own pick, its own per-entity mask and its own motion reading. What it still does not prove: a second body or tick per class; the other team's body of the same class (the snakewater D2 samples are all BLU, `team=3 family=1`; the team-colour gate is what covers RED against BLU); or server animation fidelity. The **default remains legacy**, selecting idle/moving; explicit `--animation-drive states` now selects crouch/crouchwalk/flinch and is covered by `ANIMATION-DRIVE-STATES=PASS` (`RUNS-2026-11-04.md` §24.1 and §25.3). Attack still has no driven state. Where a row has no server `m_nSequence`/`m_nModelIndex`, the selected pose remains the product's choice, not a claim of server fidelity.

## 5. What is still open, and the cheapest way to settle it

Cost basis: a measured capture is 42–60 s (`evidence/visual-baseline/trace-ref.txt` "59903.2 capture:
armed requested=2000 frame_tick=2000 frames=2056" and `evidence/world-material-face/trace-albedo.txt`
"42482.7 capture ... frames=1415"), so a 3-capture gate is 2.5–3 min and an 18-frame matrix 15–18 min.

| open item | disposition and evidence | remaining work / cost |
| --- | --- | --- |
| cull control, five maps with a hole account | **Closed** — P1-2a reads product face-id frames on all five maps and both postures, with a negative control; `MAP-MATRIX.md` §4 and `RUNS-2026-11-04.md` §23, §24.2, §25.4. | No additional map is needed for this control. `onlyInCull` and the cull/lattice difference remain readings, not claims of exact pixel equivalence. |
| bagel rc11 as a matrix row | **Closed** — `koth_bagel_rc11` is the fifth row and all four of its cells are PASS: `MAP-MATRIX.md` §1 and §2; `RUNS-2026-11-04.md` §21 and §25.4 steps 5/16. | No run needed for the row. Its earlier §10-only readings are historical. |
| a second posture per map | **Closed** — P1-1 supplies two postures per map; `MAP-MATRIX.md` §4 “Two postures, one tree” and `RUNS-2026-11-04.md` §23, §24.2, §25.4. | Odyssey posture 2 is a 90-degree yaw change at the same eye, not a second position; that limitation remains explicit in §23. |
| no lighting pixel pin | **Closed for captured frame pixels** — P1-2b stores ten world-frame hashes per map/posture (100 total), revalidated by `RUNS-2026-11-04.md` §23, §24.2 and §25.4. | **Open: decoded-lighting values are not pinned** (`MAP-MATRIX.md` §4). A probe/gate must expose and pin lightmap samples. Tick-0 frames remain intentionally unpinned because they capture the first armed frame. |
| crouch / flinch at runtime | **Closed for the opt-in states path** — `--animation-drive states` selects crouch/crouchwalk/flinch; `ANIMATION-DRIVE-STATES=PASS` with trace and crouch-pixel evidence in `RUNS-2026-11-04.md` §24.1 and §25.3. | The default is still legacy idle/moving. Attack remains NOT VERIFIED with no driven state, and no server `m_nSequence`/`m_nModelIndex` fidelity is claimed. |
| character D2 (a second body or the second team) | **Open** — the Highlander STV proved the instrument, but D2 still has one body and one tick per class. Team colour has 16 upward bodies and seven same-class pairs; Scout and Sniper remain single-team (`CHARACTER-MATRIX.md` §2.3). | A second recording or tick can supply another body; once a target exists, `class-pixels-check.sh` gives three captures per class. Do not infer nine two-team pairs from the 16-body census. |
| koth_bagel_rc13 | **Open by design / negative control** — no BSP is installed, so it is not a matrix row and remains unreachable (`MAP-MATRIX.md` §4). | Nothing to run unless a BSP is supplied; keep it as the “not installed” negative control. |

## 6. The gates this matrix rests on, and the map/tick each one pins

| gate | map | tick | what it pins |
| --- | --- | --- | --- |
| `world-geometry-hole-check.sh` | pl_odyssey (`:930`) | 2000 (`:58`) | 18 frames + two stored frame hashes |
| `world-material-face-check.sh` | whatever the demo loads (`:57`) | 2000 | 3 frames, fetch partition, face chain |
| `material-coverage-check.sh` | cp_snakewater_final1 | 0 | tile table, batches, control frame |
| `lighting-dark-account-check.sh` | pl_odyssey (`:88`) | 2000 | 5 captures, re-derives its own eye |
| `world-render-reliability-check.sh` | cp_sunshine (`:71`) | 0 | reload errors, six sky lines |
| `visual-baseline-check.sh` | pl_odyssey | 2000 | stored reference trace |
| `map-material-matrix.sh` | the five maps the matrix names | 0 and 2000 | fetch plan, tile table, atlas, two captured frames per map |
| `map-geometry-lighting-check.sh` | the five maps the matrix names, cameras from `evidence/t62-map-matrix/<map>/camera.txt` and `camera2.txt` | tick 2000 plus the second recorded/fallback posture | face partition, sky-brush coverage, empty-pixel population, dark-account partition, cull control, ten stored world-frame hashes per posture |

Geometry and lighting are measured on five maps each, materials and coverage on five. The hole account's sky-brush mask localises the sky on only two of them (odyssey 100 %, snakewater 98.2 %), so on the other three the verdict rests on the face partition and the empty-pixel population rather than on the unexplained count. P1-2a extends cull control to all five maps and both postures; P1-2b pins ten world frames per map/posture (`RUNS-2026-11-04.md` §23 and §24.2). Tick-0 captures are not frame-pinned, and no decoded-lighting value pin exists.

## 7. Disposition

* The 20 map PASS cells: ten are the materials and coverage cells of the five rows, backed by `map-material-matrix.sh` at `65315d2`; nine are the five lighting cells plus the geometry cells of snakewater, sunshine, bagel_rc12 and bagel_rc11, measured at `931588b` by `map-geometry-lighting-check.sh`; the twentieth is odyssey's geometry cell, kept from T26/T58. The Odyssey `99 + 3 + 2 + 4 = 108` arithmetic is superseded: the run commits `distinct=127 tiled=118 capped=0` and a 118-row tile table.
* No map cell is NOT VERIFIED and none is UNSUPPORTED. Cull control on five maps, a second posture per map, and stored world-frame pins are **Closed** by P1-1/P1-2a/P1-2b (`RUNS-2026-11-04.md` §23 and §24.2). Decoded-lighting values remain unpinned, and tick-0 frames are deliberately not frame pins. Separately, four character C-axis cells are PASS and five remain UNSUPPORTED (`CHARACTER-MATRIX.md` §2.4; `RUNS-2026-11-04.md` §24.8).
* The map matrix has no UNSUPPORTED cell: `koth_bagel_rc13`, whose absent BSP produced them, is not a row any more — `koth_bagel_rc11` took the fifth row on 2026-11-05. The character count is 40 PASS / 5 UNSUPPORTED, not the historical 36 / 9; the four measured third-person weapon cases are Demoman, Scout, Soldier and Medic.

> **Superseded, annotated 2026-11-07 (task-6). History kept, not deleted.** Two sentences in this §7 are pass-7 residue and are superseded by pass-8: the previous bullet's "four character C-axis cells are PASS and five remain UNSUPPORTED", and this bullet's "40 PASS / 5 UNSUPPORTED". Current authority: `CHARACTER-MATRIX.md:362` (9 x 5 = 45 cells, 45 PASS / 0 NOT VERIFIED / 0 UNSUPPORTED / 0 FAIL), `:375` (18 of the 45 cells carry a named rendered entity), `:379` (nine same-class red/BLU pairs); this file's `:10` records that the nine C cells were closed by fixing the product, and the nine-class weapon counts live in `third-person-weapon-check.sh:580`.
>
> **Line-number correction:** the retired `/d/_shared/postfreeze-inventory.md` cites `CHARACTER-MATRIX.md:297` for "a real Heavy" and `:422` for "only a class-fallback Heavy". At this tree `:297` is the Sniper D2 row (the real-Heavy statement is `:313`) and `:422` is an evidence-index row (the "Still missing" paragraph is `:438`). Doc numbers moved; no claim changed.
* The historical `ReloadAirwalk_PRIMARY` false match is closed by T61.1's token-boundary matcher. The Pyro defect the Highlander chain exposed — an empty `ReloadStand` bone chain, plus an idle pick that chose that reload clip over the 1-frame aimmatrix pose — is fixed at `931588b` (a 0xFF chain terminator in `native/src/animation_decoder.cpp`, a one-shot rule in `native/include/animation_sequence_match.h`), and the animation gate reads 233 assertions / 0 failures at that tree.
* Crouch and flinch are measured on the explicit `--animation-drive states` path: `ANIMATION-DRIVE-STATES=PASS`, with 24 flinch-state trace lines and a distinct states/legacy crouch frame at tick 14765 in the delivered run (`RUNS-2026-11-04.md` §25.3). The default legacy path still chooses idle/moving; attack remains without a driven state, and ragdoll remains out of scope. This is not server-animation fidelity.
* Every PASS in this report rests on committed evidence indexed by `RUNS-2026-11-04.md`, including the later closures in §22–§25; T73's frozen-tree full-chain sweep and the release verdict stay separate work.

## 8. Evidence refreshes on the candidate trees

The 2026-10-29 refresh read committed T61.1 evidence available at
`task/repo-cleanup @ 2f14873`, including the nine `r10-after-demos/runtime/class-*.txt` records and
`r10-pixel-mutations/baseline.log`; it updated D1 and the single Demoman D2 cell.

The 2026-11-04 refresh at `integration/t79-final @ 046f4cd52a6eae5eb952488bb0095bcce86500b1`
re-ran the six character gates at that commit (`RUNS-2026-11-04.md` §3 and §3b) and added the
per-class pixel chain that makes D2 measured for Demoman, Scout, Soldier and Medic
(`CHARACTER-MATRIX.md` §2.6, `RUNS-2026-11-04.md` §5). The same day's third pass at `65315d2` added
the team-colour gate and the map gate and ran all eight gates green (`RUNS-2026-11-04.md` §9 and §9b);
that pass is what filled five map cells. At that point the C axis remained UNSUPPORTED because
weapon-world-model data was not a third-person player render; §24.8 later closed four C cells and left
five unmeasured. No T75 install gate or full
`verify-all.sh` run was performed in any refresh; T73's final frozen-tree run and the release verdict
remain separate work.

A fourth pass at `74e946b` (docs only; `native/` unchanged since `65315d2`) ran the T66 relay
gate: 95 assertions, `SPECTATE-CHECK=PASS` (`RUNS-2026-11-04.md` §9c). That run is what turns the
death clause — a dead player leaving the drawn set — from an argument into a measurement; the C
axis, the map geometry and lighting cells, and the crouch/flinch gaps are untouched by it.

A fifth pass at `931588b` re-ran the whole set from a single driver: `build` plus ten gates,
each in its own host-lock acquisition, every step logging to `/d/_shared/t62-freeze2/`. Ten
steps passed first time. The two that did not, `appearance` and `spectate`, failed on driver
addressing rather than on product behaviour — `T62_BODY_OUT` was pointed at an empty directory
although `targets.txt`, `settings.json` and `ui.json` are read from it as inputs, and
`SPECTATE_OUT` was absolute where the product requires a repository-relative path — and both
passed unchanged once addressed correctly. No threshold moved and no gate script was edited for
this sweep. The pixel gate reproduced the §4 frame set byte for byte after the relink, and the
map axis gate reproduced every §10 reading. The full table, the twelve log hashes and the
build hashes are `RUNS-2026-11-04.md` §14. This pass does not furnish a release verdict, does
not run T75 or `verify-all.sh`, and does not close the open cells listed in §5.
A sixth pass — the nine-item closeout — merged the third-person weapon attach and the animation
drive into `integration/t79-final` (`4b34f1e`, `7c5c761`), re-ran the map matrix and its 100
frame pins on the merged binary (all 100 unchanged), and swept 18 steps at `4b34f1e` +
`2747e909…` (`RUNS-2026-11-04.md` §24). Fourteen of the eighteen steps passed. The four reds were
three gate bugs (`drives`, `weapon`, `water`), one driver-path error (`spectate`, rc=2), and one
product defect (`class-snakewater`); the gates were repaired, `spectate` and `weapon` re-ran green on
the unchanged product, and water had already passed in the sweep. The one red that stayed red is
`class-snakewater`'s demo identity: the same capture command draws the medic's body on some runs
and hides it on others, because the corpse filter at `native/src/main.cpp:9123` asks a dead set
filled by the per-frame director update at `native/src/main.cpp:10866` — i.e. from the previous
frame's snapshot — and the previous resolved tick differs per process. Those two limitations were
true at the end of §24, not at the frozen tip: §25.2 fixes the corpse-filter snapshot and restores
four byte-identical identity pairs, while §25.3 re-runs `ANIMATION-DRIVE-STATES=PASS` with zero
failures. The final freeze6 sweep is §25.4; it furnishes no release verdict.
