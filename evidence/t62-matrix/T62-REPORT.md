# T62 — focused visual matrix, evidence refresh

Original assessment: 2026-10-25 at `047bcfb`. Evidence refresh: 2026-10-29 against candidate `task/repo-cleanup @ 2f14873` (not frozen).
Deliverables: `MAP-MATRIX.md`, `CHARACTER-MATRIX.md`, this report. No gates were run during this refresh.

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

**Map matrix, 5 maps × 4 axes = 20 cells: 4 historical PASS (not re-tested on `2f14873`), 12 NOT VERIFIED, 4 UNSUPPORTED, 0 FAIL.**

| Map | materials | geometry | lighting | coverage table | 
| --- | --- | --- | --- | --- |
| cp_snakewater_final1 | PASS | NOT VERIFIED | NOT VERIFIED | PASS |
| pl_odyssey | PASS | PASS | NOT VERIFIED | NOT VERIFIED |
| cp_sunshine | NOT VERIFIED | NOT VERIFIED | NOT VERIFIED | NOT VERIFIED |
| koth_bagel_rc12 | NOT VERIFIED | NOT VERIFIED | NOT VERIFIED | NOT VERIFIED |
| koth_bagel_rc13 | UNSUPPORTED | UNSUPPORTED | UNSUPPORTED | UNSUPPORTED (no BSP in the install) |

**Character matrix, 9 classes × 5 axes = 45 cells: 28 PASS, 8 NOT VERIFIED, 9 UNSUPPORTED, 0 FAIL.**
The 28 PASS are model load (9), body materials (9), animation data/clock (9), and one rendered
Demoman D2 sample. The 8 NOT VERIFIED are D2 for the other classes; the 9 UNSUPPORTED are the
third-person weapon body. Against the brief's four axes (model / materials / weapon / animation),
the same evidence reads 36 cells: 19 measured, 8 NOT VERIFIED, 9 UNSUPPORTED.

## 4. What the matrix found that is not a cell

1. **One of the 28 PASS cells is tied to a rendered frame.** The T61.1 pixel evidence joins identity
   `id=95` to `models/player/demo.mdl`, the Demoman `run_PRIMARY` pick and its own sampled pixels at
   playback tick 64161 / scene tick 74481. The other 27 PASS cells are model, material or probe
   readings. This is one named entity sample, not general proof for all Demoman instances or classes.
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
7. **The old `ReloadAirwalk_PRIMARY` false match is fixed.** T61.1's whole-token matcher excludes
   `walk` inside `airwalk`, prefers a multi-frame run clip and pins the resolved sample identity;
   the current gate is 232/0 and its unit mutations include matcher regressions (T61 report §8.7).
8. **D2 remains narrow despite the named sample.** The pixel chain identifies one Demoman entity;
   the other eight D2 class cells remain NOT VERIFIED. Four classes (Sniper, Pyro, Spy and Engineer)
   do not appear as played players in the committed recordings. The product still has no server
   `m_nSequence`/`m_nModelIndex` for these rows, so its selected pose is not a claim of server fidelity.

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
| character D2 (8 cells remain) | capture and join per-entity identity, model, selected sequence and pixels for the four other classes present in recordings; four absent classes require a recording containing them | not estimated; requires additional evidence |

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

* The 4 map PASS cells are historical readings only; no map gate was re-run against candidate `2f14873`.
  Their source evidence and the Odyssey `99 + 3 + 2 + 4 = 108` arithmetic were re-read, but no fresh
  map verdict is claimed. Odyssey's `tiled=99` still has no committed tile table.
* The 12 map NOT VERIFIED cells are **queued, not excused**: §5 gives each a costed test. They stay NOT
  VERIFIED in this report; filling them is follow-up work, not a matrix edit.
* The map matrix's 4 UNSUPPORTED cells reflect the absent `koth_bagel_rc13` BSP. Separately, the
  character matrix's 9 C-axis cells are UNSUPPORTED because no gate renders a third-person weapon
  attached to a player body.
* The historical `ReloadAirwalk_PRIMARY` false match is closed by T61.1's token-boundary matcher;
  the nine current class probes select multi-frame run clips, while only the Demoman has a named D2
  pixel sample.

## 8. Evidence refresh on the candidate tree

This refresh reads committed T61.1 evidence available at `task/repo-cleanup @ 2f14873`, including the
nine `r10-after-demos/runtime/class-*.txt` records and
`r10-pixel-mutations/baseline.log`. The character matrix updates D1 and the single Demoman D2 cell;
the C axis remains UNSUPPORTED for all nine classes because weapon-world-model data is not a
third-person player render. No map gate, T75 install gate, or full `verify-all.sh` run was performed;
the map PASS cells remain historical and explicitly un-retested. T73's final frozen-tree run and the
release verdict remain separate work.
