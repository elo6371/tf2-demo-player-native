# T62 — focused visual matrix, evidence refresh

Original assessment: 2026-10-25 at `047bcfb`. First evidence refresh: 2026-10-29 against candidate `task/repo-cleanup @ 2f14873` (not frozen). Second refresh: 2026-11-04 at `integration/t79-final @ 046f4cd52a6eae5eb952488bb0095bcce86500b1`; third pass the same day at `65315d27b10d416a46680123c6719416c11d6541`, which adds the team-colour gate and turns the map matrix into a gate; fourth pass the same day at `931588b1dc8fc14ec0775be53dc47ba217e1a94f`, which adds the five-map geometry/lighting gate, the per-class Highlander chain and the Pyro animation fix (none of these frozen).
Deliverables: `MAP-MATRIX.md`, `CHARACTER-MATRIX.md`, this report. **Eight product gates were run at `65315d2` and all eight pass** — the six character gates, the team-colour gate (`BODY-APPEARANCE`) and the five-map material matrix (`MAP-MATERIAL-MATRIX`). A ninth, the T66 relay gate, was run at `74e946b` and passes as well (`SPECTATE-CHECK`, 95 assertions), which is what backs the death clause. The fourth pass at `931588b` adds two more runs: the five-map geometry/lighting gate (`MAP-GEOMETRY-LIGHTING`, 50 captures) and the probe-only pose-family gate (`POSE-FAMILIES`, 40 sampled clips). With those, no map cell is NOT VERIFIED and no D2 cell is either. A fifth pass at the same commit then re-ran everything from one driver: ten gates plus the build, all green, no assertion relaxed (`RUNS-2026-11-04.md` §14). Commands, verdicts, tree identity and log/frame hashes: `RUNS-2026-11-04.md`.

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

**Map matrix, 5 maps × 4 axes = 20 cells: 16 PASS, 0 NOT VERIFIED, 4 UNSUPPORTED, 0 FAIL.** Nine of the sixteen were backed by the `map-material-matrix.sh` run at `65315d2` (three already PASS and reproduced unchanged, five upgraded from NOT VERIFIED, plus the odyssey geometry cell which keeps its historical reading). The other seven — the lighting cells of snakewater, sunshine and bagel_rc12, and the geometry cells of those same three maps — were measured at `931588b` by `map-geometry-lighting-check.sh`. `MAP-MATRIX.md` marks the material re-reads ‡‡ and the geometry/lighting measurements §§, and states exactly what each mark asserts.

| Map | materials | geometry | lighting | coverage table | 
| --- | --- | --- | --- | --- |
| cp_snakewater_final1 | PASS ‡‡ | PASS §§ | PASS §§ | PASS ‡‡ |
| pl_odyssey | PASS ‡‡ | PASS | PASS §§ | PASS ‡‡ |
| cp_sunshine | PASS ‡‡ | PASS §§ | PASS §§ | PASS ‡‡ |
| koth_bagel_rc12 | PASS ‡‡ | PASS §§ | PASS §§ | PASS ‡‡ |
| koth_bagel_rc13 | UNSUPPORTED | UNSUPPORTED | UNSUPPORTED | UNSUPPORTED (no BSP in the install) |

**Character matrix, 9 classes × 5 axes = 45 cells: 36 PASS, 0 NOT VERIFIED, 9 UNSUPPORTED, 0 FAIL.** The 36 PASS are model load (9), body materials (9), animation data/clock (9), and nine rendered D2 samples — one body per class, four on snakewater frames 63000/64161 and five on the Highlander STV of `pl_upward_f12` at ticks 20001/24001/30001 (`CHARACTER-MATRIX.md` §2.6). The 9 UNSUPPORTED are the third-person weapon body. Against the brief's four axes (model / materials / weapon / animation), the same evidence reads 36 cells: 27 measured, 0 NOT VERIFIED, 9 UNSUPPORTED.

## 4. What the matrix found that is not a cell

1. **Nine of the 36 PASS cells are tied to a rendered frame.** The pixel chains name one body per class: Demoman `id=95` `run_PRIMARY` seq 147, 1,767 px, 78.72 % moved (snakewater 64161); Scout `id=96` 1,161 px, 43.07 % (64161); Soldier `id=86` 1,140 px, 93.68 % (63000); Medic `id=95` 1,400 px, 64.57 % (64000); and on `pl_upward_f12`, Pyro `id=85` 3,152 px, 37.44 % (30001), Heavy `id=89` 5,072 px, 66.58 % (24001), Spy `id=95` 2,730 px, 59.56 % (24001), Engineer `id=86` 3,033 px, 86.75 % (24001) and Sniper `id=93` 3,681 px, 60.12 % (20001). The other 27 PASS cells are model, material or probe readings. These are nine named bodies at one tick each — not general proof for other instances, other ticks, or the other team's body of the same class.
2. **The lighting evidence is no longer the pre-clock-fix pose.** The old reading was taken at the camera the T63-F1 clock defect produced — `evidence/t36-lighting-dark-account/frames.txt:3` says `eye=-2294.8,348.8,383.4` — while `world-geometry-hole-check.sh:167-197` pins `PIN_CAPTURE_EYE="815.5,-147.8,-194.3"` for the same demo and tick. The `931588b` gate does not re-derive its eye from its own trace: it reads the demo and the camera per map from `evidence/t62-map-matrix/<map>/camera.txt` and asserts that all ten frames armed at tick 2000 with one identical `scene: camera` eye, so a moved camera now fails the gate instead of quietly changing the number. The five cameras are snakewater `1185.5,-813.5,207.1`, odyssey `-1221.2,462.2,650.3`, sunshine `-3760.0,376.0,-112.0`, bagel rc12 `533.2,-803.3,320.0` and bagel rc11 `914.0,-1000.0,563.8`.
3. **Only one gate pins stored world frame bytes**: `world-geometry-hole-check.sh:178-179`
   (`PIN_TEXTURED_SHA=1b0ab468...`, `PIN_REF_SHA=e9218c42...`), asserted at `:712-717`. Every other
   gate compares frames it just captured, so a picture that changed for the wrong reason stays green.
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
8. **D2 is complete across nine classes, one body each.** The pixel chain names one body per class — four on snakewater, five on the Highlander STV — so every class has a rendered sample with its own pick, its own per-entity mask and its own motion reading. What it still does not prove: a second body or tick per class; the other team's body of the same class (the snakewater D2 samples are all BLU, `team=3 family=1`; the team-colour gate is what covers RED against BLU); and the runtime crouch/flinch poses the product never selects — the pose-family gate covers their *data* only (`RUNS-2026-11-04.md` §13). Where a row has no server `m_nSequence`/`m_nModelIndex`, the selected pose remains the product's choice, not a claim of server fidelity.

## 5. What is still open, and the cheapest way to settle it

Cost basis: a measured capture is 42–60 s (`evidence/visual-baseline/trace-ref.txt` "59903.2 capture:
armed requested=2000 frame_tick=2000 frames=2056" and `evidence/world-material-face/trace-albedo.txt`
"42482.7 capture ... frames=1415"), so a 3-capture gate is 2.5–3 min and an 18-frame matrix 15–18 min.

| open item | cheapest test | cost |
| --- | --- | --- |
| cull control, four maps with a hole account | the gate already captures `--no-world-cull` frames per map; asserting them needs a per-map pin that does not exist yet | pinning work, no capture |
| bagel rc11 as a matrix row | both map gates already read it (materials at `65315d2`, geometry/lighting at `931588b`); placing it in the table is a decision, not a run | editing only |
| a second posture per map | one more camera per map in `map-geometry-lighting-check.sh`; ten captures per map | ~1 min per capture |
| no lighting pixel pin | a stored frame hash per map, as `world-geometry-hole-check.sh:178-179` does for odyssey | pinning work |
| crouch / flinch at runtime | a product change: the driver only chooses idle/moving (`native/src/main.cpp:170-236`); the data itself is decoded and gated by `class-pose-family-check.sh` | feature work |
| character D2 (a second body or the second team) | the Highlander STV proved the instrument; a second recording with the same nine classes, or the same recording at a second tick, gives two bodies per class. Once a target exists, `class-pixels-check.sh` gives three captures per class | ~1 min per class at `speed 32`, plus the recording |
| koth_bagel_rc13 | nothing: no BSP; keep it as the negative control | — |

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
| `map-geometry-lighting-check.sh` | the five maps the matrix names, camera from `evidence/t62-map-matrix/<map>/camera.txt` | 2000 | face partition, sky-brush coverage, empty-pixel population, dark-account partition |

Geometry and lighting are now measured on five maps each, materials and coverage on five. The hole account's sky-brush mask localises the sky on only two of them (odyssey 100 %, snakewater 98.2 %), so on the other three the verdict rests on the face partition and the empty-pixel population rather than on the unexplained count. Cull control still reads odyssey only, and no geometry or lighting cell pins a frame hash.

## 7. Disposition

* The 16 map PASS cells: nine backed by `map-material-matrix.sh` at `65315d2` (five upgraded from NOT VERIFIED, three reproduced unchanged, one historical odyssey geometry) and seven measured at `931588b` by `map-geometry-lighting-check.sh`. The Odyssey `99 + 3 + 2 + 4 = 108` arithmetic is superseded: the run commits `distinct=127 tiled=118 capped=0` and a 118-row tile table.
* No map cell is NOT VERIFIED. What remains open is not a cell: cull control on four maps, a second posture per map, a lighting pixel pin, and bagel rc11's place in the table (§5). The map matrix's 4 UNSUPPORTED cells reflect the absent `koth_bagel_rc13` BSP; separately, the character matrix's 9 C-axis cells are UNSUPPORTED because no gate renders a third-person weapon attached to a player body.
* The map matrix's 4 UNSUPPORTED cells reflect the absent `koth_bagel_rc13` BSP; separately, the character matrix's 9 C-axis cells are UNSUPPORTED because no gate renders a third-person weapon attached to a player body.
* The historical `ReloadAirwalk_PRIMARY` false match is closed by T61.1's token-boundary matcher. The Pyro defect the Highlander chain exposed — an empty `ReloadStand` bone chain, plus an idle pick that chose that reload clip over the 1-frame aimmatrix pose — is fixed at `931588b` (a 0xFF chain terminator in `native/src/animation_decoder.cpp`, a one-shot rule in `native/include/animation_sequence_match.h`), and the animation gate reads 233 assertions / 0 failures at that tree.
* Crouch and flinch stay NOT VERIFIED *at runtime*: the clips decode (40 sampled crouch/flinch clips, 14 flinch sequences, every one finite) but the driver chooses only idle and moving, so no frame this product draws can show them. That is a recorded product gap, not a matrix hole.
* Every PASS in this report rests on a committed run listed in `RUNS-2026-11-04.md` §3–§13; T73's frozen-tree sweep and the release verdict stay separate work.

## 8. Evidence refreshes on the candidate trees

The 2026-10-29 refresh read committed T61.1 evidence available at
`task/repo-cleanup @ 2f14873`, including the nine `r10-after-demos/runtime/class-*.txt` records and
`r10-pixel-mutations/baseline.log`; it updated D1 and the single Demoman D2 cell.

The 2026-11-04 refresh at `integration/t79-final @ 046f4cd52a6eae5eb952488bb0095bcce86500b1`
re-ran the six character gates at that commit (`RUNS-2026-11-04.md` §3 and §3b) and added the
per-class pixel chain that makes D2 measured for Demoman, Scout, Soldier and Medic
(`CHARACTER-MATRIX.md` §2.6, `RUNS-2026-11-04.md` §5). The same day's third pass at `65315d2` added
the team-colour gate and the map gate and ran all eight gates green (`RUNS-2026-11-04.md` §9 and §9b);
that pass is what filled five map cells. The C axis remains UNSUPPORTED for all nine classes because
weapon-world-model data is not a third-person player render. No T75 install gate or full
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