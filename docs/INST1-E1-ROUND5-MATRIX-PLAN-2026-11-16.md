# E1 第 5 轮最终四门矩阵 — 计划（2026-11-16，只写不跑）

**卡片**：E1 round-5 final matrix 预备件（lead 派工）　**状态**：脚本与本文档已落盘；**矩阵未执行**，等 lead 发令一次跑完。
**脚本**：$native/tools/run-e1-round5-matrix.sh（`--dry-run` / `--run` / `--help`）
**前置身份**：exe 钉 sha $0bc3ed077aae404634a9f11a5453e6d8f5235be56cc2b818a1626a39ecbab3e4（3,070,464 B，2026-11-16 19:51 重建）；dry-run 时 head `07cb7bd0`。
**本轮 exe 身份变更**：216d7f98 → 06afc166，原因是 U4 第 3 步 + E9 修法并入（merge `4cca8d41`）；pin 随每次重建刷新。
**r1b 并入后再次重建**：06afc166 → 0bc3ed07，原因是 `07cb7bd0` 把 `--capture-range-depth/window/skip` 并入 candidate/e5；当前矩阵 pin 即 `0bc3ed07…`。

---

## 1 这份矩阵要回答什么

四门在同一窗口、同一 exe 身份、同一 host lock 规则下顺序跑完，产出一张可横向读的表：

| 门 | 脚本 | 本次范围 | 判据（verdict 行） |
| --- | --- | --- | --- |
| viewmodel-material | `viewmodel-material-check.sh` | 全部 §1–9 | `VIEWMODEL-MATERIAL=PASS` |
| minimap-hud | `minimap-hud-check.sh` | 默认范围（A12 mutation 跳过） | `MINIMAP-HUD=PASS assertions=N skipped_controls=1` |
| map-geometry | `map-geometry-lighting-check.sh` | `--only=cp_snakewater_final1 --poses=p1` | `MAP-GEOMETRY-LIGHTING=PASS maps=1 ... failed=0` |
| companion | `viewmodel-companion-check.sh` | 默认范围 | `VIEWMODEL-COMPANION=PASS` |

- **p2 不跑**：第 4/5 轮 p2 都是 WINDOW-DEADLINE（620 s 被 TERM、10/11 帧无裁决），本窗口按预算只跑 p1；p2 单列为已知未覆盖。
- **minimap-check.sh（2 s、27 断言）不是第五门**：它是 minimap-hud 的旁证读数，本矩阵不跑；需要时单独两秒补。
- 每门 PASS 定义 = rc=0 **且** 自己的 verdict 行 = PASS **且** exe sha 跑前=跑后=pin。OK/FAIL 行数是读数，不是判据。

## 2 已知红历史（这些红不是新红，矩阵的作用是看它们是否复现）

| 门 | 第 5 轮读数（triage §2/§3） | 本矩阵期望 |
| --- | --- | --- |
| viewmodel-material | 10 条 pin 已按 re-pin 窗口重钉（§3.1.2/§3.1.3），同 exe 全 9 节 **226 OK / 0 FAIL** | **保持绿**；若红，先查是产品还是 pin 漂移 |
| minimap-hud | 285 s、96 断言、3 红：A1 `shapes=1243 期望13`（13+1230，rects 并入、未钉，待 card-ui 重定）；A6/A7 vacuity 守卫实测 15/8 期望 >=20（U3a 基底层填充破坏前提，`nothing-drawn` 仍 0）；A12 需 `--mutation` 跳过 | **3 红复现即定性成立**；数值漂移要在报告里点名 |
| map-geometry p1 | ~11 min、24 OK / 0 FAIL；`cull removed=3467 px/16 faces`、attributable 0.9257% <=2% | 两份参考是 2026-11-05 的，本轮必红；按 lead 批准**正式 re-pin**（§10），改写只限这两个 pin 文件 |
| companion | 第 4/5 轮均 NOT RUN（WINDOW-DEADLINE）；上轮 36 FAIL / 43 OK 属结构性红，待从 8-08 POV 重导 witness（observations>=1000、ticks>=3） | 记录真实读数，**不预设绿**；结构性红复现即入册 |

## 3 本次要验的三件事

1. **viewmodel-material 保持绿**：226 OK / 0 FAIL 在 head 下复现；§7/§8 的 `ranges=3`/`rangeTextured=0,0,0`/`0,1,1`、§9 的 `uploadSlot=-2`/`rangeSlots=2,2,2` 与 re-pin 后的断言一致，**没有因新提交再漂一次**。
2. **minimap-hud 定性结论能复现**：三红仍是 A1(shapes=1243) + A6/A7(vacuity 15/8)，`nothing-drawn=0`，A12 `skipped_controls=1`；若 A1/A6/A7 变绿，要能指出是 card-ui 的哪次提交改了口径（本次矩阵不改其期望值）。
3. **CRLF 不再红**：camera-ui 门禁的 CRLF 自缺陷已由 `bde74d5d` 修（证据一律 LF、读侧忽略 CR）。矩阵两层验证：① 矩阵自己的 bookkeeping 目录跑 `camera-ui-check.sh --check-eol` 必须 GREEN；② 各门证据目录的 CR 计数只作为 `EOL-WARN` 记录（产品 trace 以 CRT 文本模式落盘是 D53 家族已知现象，未被本窗口修，**不得记成矩阵红**）。

## 4 脚本的硬约束（同时写在脚本头注释与 `--help`）

1. **不重建 exe**：脚本无 `build-target.sh` 调用；每门跑前/跑后 sha 与 pin 比，`before=after=pin` 才算数，漂移记 `EXE-SHA-DRIFT`（矩阵 rc=2）。
2. **不 stash / 不 clean / 不 checkout / 不 commit**：别人的脏文件（`.gitattributes`、4 个 N/PROJECT docs、`evidence/t62-map-matrix/*`）原样保留；矩阵只记录 `git-status-before/after` 的 delta。
3. **不跑 `--mutation`**：三个门都有会重建源码的 mutation 模式，一个都不传；因此 minimap-hud A12 必然 `skipped_controls=1`。
4. **某门 FAIL 不早退**：四门全跑，汇总表才是交付物。
5. **证据目录一律相对**：全部落在 `evidence/fast/e1-round5-matrix/<utc>/`（`evidence/fast/` 已 gitignore）。`TRACE_DIR`/`OUT` 绝对路径会被门禁二次拼 `ROOT`（triage §4.4 的坑）。
6. **超时显式**：`timeout -k 10 <cap>`，cap 写死在门表：viewmodel-material 2400 s、minimap-hud 900 s、map-geometry 1800 s、companion 1800 s（其自身每次 launch `RUN_TIMEOUT=400`，加上 history run，给 1800 这一档）。没有无限等待。

## 5 hostlock 归属（容易踩的一点）

- viewmodel-material / minimap-hud / companion：`hostlock.sh run card-arch …`（门自己不再取锁）。
- map-geometry：**必须** `hostlock.sh run t62-mapaxes … env HOLD_LOCK=1 …`。该门的 `HOLD_LOCK=1` 会校验锁确实由 `t62-mapaxes` 持有；若外层用别的 owner 再让它嵌套取锁会死锁，若直接裸跑则拒绝执行（它不允许无锁捕获）。
- hostlock 状态/残留 owner/队列的处理沿用 hostlock v2.2：stale owner 用 `hostlock.sh break <owner>`，死 waiter 票要清 `_shared/queue/<ticket>`（triage §4.2）。

## 6 已知副作用（矩阵只记录，不清理）

- map-geometry 会 `mkdir -p MATRIX/inputs` 并写入 `settings.ini`/`ui.ini`（`evidence/t62-map-matrix/inputs/`，untracked）。矩阵把它们作为 tree delta 打出；删不删、怎么还原由 lead 定（脚本不允许 clean/checkout）。
- map-geometry 不带 `--re-pin`：`frame-pins-p1.txt`、`cull-reference-p1.txt` 只读；pin 不匹配是红读数，不是重钉。
- 其余各门的 OUT/TRACE/SCRATCH 全在 `evidence/fast` 下，跑完不脏 tracked 路径。

## 7 dry-run 自检输出（2026-11-16 11:12Z，head 2a8396ad，未执行任何门）

```
E1 ROUND-5 MATRIX -- mode=dry
  head=2a8396ad
  exe=native/build-nmake/tf2_demo_native.exe
  exe_sha=OK
  hostlock=/d/TF2_Native_Worktrees/_shared/hostlock.sh
  run_dir=evidence/fast/e1-round5-matrix/20261116T111248Z (relative, gitignored)

gate                 hostlock     cap     preconditions
viewmodel-material   card-arch    2400s   OK
minimap-hud          card-arch    900s    OK
map-geometry         t62-mapaxes  1800s   OK
companion            card-arch    1800s   OK

--- resolved command lines (nothing is executed in this mode) ---
- viewmodel-material  (owner=card-arch cap=2400s)
    bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run card-arch timeout -k 10 2400 env VIEWMODEL_MATERIAL_OUT=evidence/fast/e1-round5-matrix/20261116T111248Z/gates/viewmodel-material/out VIEWMODEL_MATERIAL_TRACE_DIR=evidence/fast/e1-round5-matrix/20261116T111248Z/gates/viewmodel-material/trace bash viewmodel-material-check.sh 
- minimap-hud  (owner=card-arch cap=900s)
    bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run card-arch timeout -k 10 900 env MINIMAP_CHECK_OUT=evidence/fast/e1-round5-matrix/20261116T111248Z/gates/minimap-hud/out MINIMAP_CHECK_SCRATCH=evidence/fast/e1-round5-matrix/20261116T111248Z/gates/minimap-hud/scratch MINIMAP_CHECK_DEMO_SUNSHINE=E:/demo测试/cp_sunshine/STV/SUNSHINE.dem bash minimap-hud-check.sh 
- map-geometry  (owner=t62-mapaxes cap=1800s)
    bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run t62-mapaxes timeout -k 10 1800 env HOLD_LOCK=1 MAP_AXIS_OUT=evidence/fast/e1-round5-matrix/20261116T111248Z/gates/map-geometry/map-axes bash map-geometry-lighting-check.sh --only=cp_snakewater_final1 --poses=p1 
- companion  (owner=card-arch cap=1800s)
    bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run card-arch timeout -k 10 1800 env VIEWMODEL_COMPANION_TRACE_DIR=evidence/fast/e1-round5-matrix/20261116T111248Z/gates/companion/trace bash viewmodel-companion-check.sh 

--- preconditions ---
  OK   viewmodel-material
  OK   minimap-hud
  OK   map-geometry
  OK   companion

--- identity / safety ---
  exe pin: OK
  rebuilds: none (no build-target call in this script)
  git mutation: none (no stash/clean/checkout/commit)
  mutation modes: none passed (minimap-hud A12 reads skipped_controls=1)
  caps: viewmodel-material 2400 s, minimap-hud 900 s, map-geometry 1800 s, companion 1800 s
  evidence: evidence/fast/e1-round5-matrix/20261116T111248Z/ (relative, under the gitignored evidence/fast)

DRY-RUN=OK gates=4 (SKIP rows are listed above, never dropped silently)
```

**SKIP-PRECONDITION：本轮 dry-run 为 0 门** —— 四门前置全部解析通过（exe 与四个探针 exe 在、fixture 目录在、三条 demo 在、`animation-inputs.txt` 在、`frame-pins-p1.txt` 在、hostlock 在）。
脚本对"缺前置"的处理是显式打印 `SKIP-PRECONDITION <gate>: missing …` 并在汇总表单列 `SKIP`，不会静默跳过；真跑时缺前置 → 矩阵 rc=2（`E1-ROUND5-MATRIX=INCOMPLETE`）。

## 8 跑完要回答的问题清单

1. 四门 exe sha 是否全程 `0bc3ed07…`（r1b 并入后重建；身份史见 §10.2）？有没有门在内部触发 build（sha drift）？
2. viewmodel-material 是否 226 OK / 0 FAIL？§7/§8 的 3-range 读数与 §9 的 `uploadSlot=-2` 是否仍与 re-pin 文本一致？
3. minimap-hud 是否仍 3 红且就是 A1+A6/A7？`shapes`/vacuity 数值有没有漂（1243/15/8）？A12 是否仍 skip=1？
4. map-geometry p1 是否 24 OK / 0 FAIL？`frame-pins-p1`/`cull-reference-p1` 是否未被改写（`git diff --stat -- evidence/t62-map-matrix` 里不该出现这两个 pin 文件）？
5. companion 的真实读数是什么（OK/FAIL 行数、verdict、耗时）？witness `observations`/`ticks derived` 是否达到 >=1000 / >=3？结构性红是否复现？
6. 矩阵 bookkeeping 的 `--check-eol` 是否 GREEN？各门 `eol_cr` 是否都能归因到产品 trace（D53 家族），有没有**新**的非产品 CRLF 证据？
7. tree delta 是否为空？run 2 实测的 `?? evidence/t62-map-matrix/inputs/` 已由 5aee0885 修掉（输入改到 `.scratch/t62-map-axes/inputs`）；有没有门意外写到 tracked evidence（如 `evidence/t62-map-axes`）？
8. 总耗时与窗口预算：实际 vs cap 差多少？哪门需要更长窗口 / 是否该拆成两个窗口（p2、mutation 另排）？
9. 若 viewmodel-material 或 map-geometry 出现新红：先定性（产品 / 判据 / 窗口截断），**不许当场改期望值凑绿**。

## 9 复现

```bash
# 只解析、不跑门（本次验过的模式）
bash native/tools/run-e1-round5-matrix.sh --dry-run

# lead 发令后：
bash native/tools/run-e1-round5-matrix.sh --run
# 汇总：evidence/fast/e1-round5-matrix/<utc>/matrix/summary.tsv
# 每门日志：evidence/fast/e1-round5-matrix/<utc>/matrix/<gate>.log
```

dry-run 输出与本文档同步保存在 `/d/_shared/EXEC-2026-11-15/INST1/E1-round5/matrix/dry-run.log`。
## 10 p1 参考正式 re-pin（2026-11-16，B 工单，lead 已批准）

§2 里「保持 p1 PASS」这句被本轮证伪：p1 的两份参考还是 2026-11-05 的（`pinned=2026-11-05T05:48:47Z head=14c0f1c`、`reference=2026-11-05T05:48:56Z`），本轮跑出来就是 `THE PICTURE MOVED` + `THE CULL REFERENCE MOVED` 两条红（24 OK / 2 FAIL）。定性见 `docs/FOUR-CARD-REPORT-2026-11-16.md:111`：内容判据两版全绿，变的只是画面参考本身，所以按 lead 批准把参考正式重钉到本次身份。本节把旧值逐项并列，不留静默覆盖。

### 10.1 两份参考的旧值（2026-11-05）与新值（2026-11-16）

身份（旧 → 新）：

- `frame-pins-p1.txt`：旧 `pinned=2026-11-05T05:48:47Z head=14c0f1c product_sha256=2caafdbf7280694d9fafd88bb05dbac3e95cd522973b673fc5b5b45f3e39c783`；新 `pinned=2026-11-16T13:35:13Z head=5aee0885 product_sha256=0bc3ed077aae404634a9f11a5453e6d8f5235be56cc2b818a1626a39ecbab3e4`。旧头以 `# previous pin:` 行保留在文件里，不静默覆盖。
- `cull-reference-p1.txt`：旧 `reference=2026-11-05T05:48:56Z head=14c0f1c`；新 `reference=2026-11-16T13:36:19Z head=5aee0885`，reason 相同。

帧 sha（十帧 bytes 全为 2582406；「同」= 与旧值逐字节相同）：

| 帧 | 2026-11-05（旧） | 2026-11-16（新） |
| --- | --- | --- |
| textured | bdeecfd41a87451a1f243a43cb0822ca0040ba259f599b637a5b231eac810933 | 4ee8c864dcbbb6af134e7c7ad3e56a672838f92d813b563f8dc8bfd937164af7 |
| none | c04021f716da86dc61d30c8115703ce97ad037f762c3e8f7b291b9b056cfecfc | 同 |
| sky-faces | a7c576e17773464b50c3cb890f7d1fc76894013d53166f2b5540562023e5e403 | 同 |
| flat-base | 0e0d9eeb0a6c5b15e78cde97a5b4222dadcf16ce765aa9dc65b2e9f76c597569 | ab4bdb2dfd9d06dfb8818c8eb93c9fbd5b3ad83cfe4ba7e4c9874804cc19e236 |
| flat-novis | 6372d09d9b9ee92610512cf1b9d7b0f0795de70424f95e78d5fb77cac26b79f9 | 同 flat-base（新参考里与 vis-on 帧字节相同） |
| flat-nocull | 344cf9413949656664f803e0c533ff013cb96c072345000fe7e6ec71d4622076 | 8c975f6990dee2ee0876adc061ef759fb7db09368be1c9b132d4f9bf40b566d2 |
| flat-novis-nocull | 35c9c1efaff8586ea589be7457819c49c883f780430de307f63568d350942791 | 同 flat-nocull |
| ids | 21edac7184a895de66f227ff46a63608a0bd257a698c2976f1f9469022450776 | a3a2b2b19c0e415b7f7ae5b40e26fc79409a8ab6798a7cb2590f46c00d3db640 |
| ids-nocull | 1389055cd18dd79a06c55abc54d41aee868395d79cb1daae8b4b8a5865c91eff | 9cd6a3c77a72d1a0b560c97af859f8e457d7c9f7e5b815b57453687fbb513814 |
| lmoff | 79ce770b6a60394269d1c47ae4eac494f48f803b210fb2c4a3f7e8ac089c2b65 | 4f17fca0c50314ad8b9262c012c320e9d7c4336eb04bcf4e83c9cfb256b6e564 |

cull 参考（旧 → 新）：

| 读数 | 2026-11-05（旧） | 2026-11-16（新） |
| --- | --- | --- |
| cull kept | 617852 | 425899 |
| cull removed | 22520 | 3467 |
| cull onlyInCull | 2 | 0 |
| cull removedFront | 17017 | 710 |
| cull removedBack | 5503 | 2757 |
| cull removedAmbiguous | 0 | 0 |
| cull distinctRemovedFaces | 27 | 16 |
| 前 8 个 face | 9746=14068、9745=2664、10908=2494、10911=1502、5247=537、11043=340、9591=140、11237=98 | 10908=1369、12225=707、9619=466、12341=184、10911=179、9626=152、12406=127、12343=93 |

### 10.2 这段距离里到底变了什么

- **跨 11 天、3 个 exe 身份**：旧参考钉在 `2caafdbf…`（2026-11-05），本次身份 `0bc3ed07…`（2026-11-16 19:51 重建，3,070,464 B），中间经过 `a14ea967…` → `216d7f98…` → `06afc166…`。
- **不是 r1b 回退**：本轮 p1 的 24 条内容判据（G1–G8 的像素代数、点到 oracle、cull 侧别、暗 albedo 归因）全绿，`attributable=0.9257%` 仍在 `<=2%` 内；变的只有「钉住的画面」本身。
- **退化变体从「四者互不相同」变成「两两相等」**：旧 pin 时 `flat-base`/`flat-novis`/`flat-nocull`/`flat-novis-nocull` 四个 sha 互不相同；现在关掉 vis clip 不再改变这一帧（`flat-novis == flat-base`、`flat-novis-nocull == flat-nocull`）。这正是本次把退化关系升成断言（§10.3）的原因。
- `none` 与 `sky-faces` 与旧值逐字节相同：这两帧不经过 world 世界的 flag 组合，说明不是渲染链整体漂移。

### 10.3 退化变体变成断言（带负控）

`map-geometry-lighting-check.sh:573`（B2 块）把上面两条相等写成显式断言，而不是只留在 pin 里：

- `flat-novis` 必须与 `flat-base` 逐字节相同（`frames_same`，`map-geometry-lighting-check.sh:310`）；
- `flat-novis-nocull` 必须与 `flat-nocull` 逐字节相同；
- 每条相等断言都带一个**负控**：把 vis-on 帧复制到 `$CAP/<off>.control.bmp` 并翻转最后一个字节（离屏篡改，不重建产品），要求 `cmp` 看到差异；看不到就判 `the <off> equality has no control`。

### 10.4 可复现：连续两次逐项相同

同一 exe、同一 pose、同一 tick，hostlock `run t62-mapaxes` + `HOLD_LOCK=1`：

| 次 | 命令 | 结果 | 日志 |
| --- | --- | --- | --- |
| 1 | `--only=cp_snakewater_final1 --poses=p1 --re-pin=...` | `MAP-GEOMETRY-LIGHTING=PASS`，28 OK / 0 FAIL（两条参考比较被 RE-PIN 行替代），写出新 pin/cull 参考 | `/d/_shared/EXEC-2026-11-15/INST1/E1-round5/matrix/20261116T114858Z/B-run1/gate.log` |
| 2 | 同上但不带 `--re-pin` | `PASS`，30 OK / 0 FAIL；`the frames are the pinned picture (10 frames)` 通过，cull 对照新参考通过 | 同目录 `B-run2/gate.log` |

两次 `captures.txt` 的 10 行 `frame <name> sha256=…` 逐行相同（`diff` 为空，FRAMES-IDENTICAL-10）；两次 cull 读数也逐项相同（第二轮对第一轮参考通过）。读数：`cull removed=3467 px from 16 faces (front=710 back=2757)`、`attributable=0.9257%`。

### 10.5 阈值没有动

`attributable <= 2%`、`onlyInCull` 的 0.1% 上界等判据一字未改；本提交只换参考值、加 §10.3 的两条实体断言及其负控。

### 10.6 复跑

```bash
bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run t62-mapaxes timeout -k 10 1800 \
  env HOLD_LOCK=1 MAP_AXIS_OUT=<外部目录> \
  bash map-geometry-lighting-check.sh --only=cp_snakewater_final1 --poses=p1
```

Expected: `MAP-GEOMETRY-LIGHTING=PASS ... failed=0` and 30 OK / 0 FAIL. Re-pin again only with a new reason (`--re-pin=<why>`), and record old/new in this section.
