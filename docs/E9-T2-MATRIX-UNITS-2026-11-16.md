# E9 / t2-visual-matrix：三条读数红线是单位与方向的问题

日期 2026-11-16。分支 `work/card-ui-u4s3`（worktree `D:/TF2_Native_Worktrees/t105-e5-u4s3`）。
对象：仓库根 `t2-visual-matrix.sh`（**不是** `*-check.sh`，历次门禁普查用的 `*-check.sh` glob 漏掉了它）。
同批的另一张卡（minimap-hud 三条红）见 `docs/E9-MINIMAP-HUD-GATE-UNITS-2026-11-16.md`（提交 62aba085）。

## 0 结论

`t2-visual-matrix.sh` 长期带着 3 条红（本轮前 `assertions_ok=58`，另有 3 条 FAIL）。三条都不是产品缺陷，
而是**判据的单位/方向错了**：

| # | 断言 | 旧期望 | 实测 | 定性 | 现在的判据 |
|---|------|--------|------|------|------------|
| 1 | `vis on input` | `200000` | `242109` | 200000 是 T88 之前 parser 的三角形容量，**从未按地图测量过**；T88 量出 cp_snakewater_final1 要 242361，把容量提到 400000（`native/src/main.cpp:5973`） | 与**同一次 run** 的 `requested` 比，1% 残差 |
| 2 | `vis on hidden` | `15039` | `25995` | 15039 是兄弟门禁 `world-material-check.sh` **自己机位**的数（`:472`）；hidden 是**相机**的属性（本门 orbit 机位 25995） | 由 census 推导：`hidden = clustered - (drawn - withoutCluster)`，并要求 `0 < hidden < clustered` |
| 3 | `on vs vis-off` 像素差 | 至少 5% | `0.0000%`（逐字节相同） | 方向反了：PVS 裁掉的三角形按定义从本帧相机看不见，**正确的裁剪本来就应该像素中性**；旧断言实际在要求"开关必须改变画面" | `--max-delta 0`（必须完全一致） |

改后前向 `assertions_ok=68`、`T2-VISUAL-MATRIX=PASS`；新增的 `--mutation` 注入三个缺陷都能被抓到
（m1 5 红 / m2 1 红 / m3 3 红），恢复重建后 exe sha 回到 `43b59b72…`（72 条断言全绿）。

**没有把 242109 / 25995 / 0.0000% 换成新的钉死数字。** 换数字只是把当前算术钉住，对"算术对不对"一个字都不说；
下面每条断言都改成了"由同一次 run 自报的读数推导"的形式。

## 1 修前读数（复现原始三条红）

```
exe   native/build-nmake/tf2_demo_native.exe  sha256 43b59b7299386aba924ecfbc79a87c5edfe0fa00e3307c60fcd4658fc600df4e
demo  D:/TF2_Demo_Player/testdata/demos/bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem
tick  0（--start-paused），无 observer 样本 ⇒ 默认 orbit 机位
```

```
  FAIL vis on input = 242109, expected 200000
  FAIL vis on hidden = 25995, expected 15039
  FAIL on vs vis-off        : BMP-DIFF differing=0 total=2582352 percent=0.0000 max_delta=0 extent=1264x681x24 status=FAIL reason=whole frame percent=0.0000 is below 5.0000
```

同一次 run 的 trace（`readings/switches-tick0.txt`，按行原样）：

```
map: geometry budget capacity=400000 requested=242361 dropped=0 facesDropped=0 brushFacesDropped=0 brushModelsDropped=0 firstFace=-1 firstBrush=- state=within
gpu: world vis cameraCluster=2108 visibleFromCamera=858 clusters=2940 clusteredTriangles=34517 hiddenTriangles=25995 withoutCluster=207592 inputTriangles=242109 drawnTriangles=216114 clipApplied=1 state=active eyeMap=(1359.6,-538.8,73.9) reason=hid 25995 of 242109 triangles from cluster 2108 (609 kept by a cluster past the first)
gpu: world vis cameraCluster=-1 visibleFromCamera=-1 clusters=2940 clusteredTriangles=0 hiddenTriangles=0 withoutCluster=0 inputTriangles=0 drawnTriangles=242109 clipApplied=0 state=disabled eyeMap=(368.0,372.0,344.0) reason=vis clip disabled by the caller
```

旧的三条期望值来自 `world-material-check.sh:458-475`（同一份 trace 的另一套机位）：

```
# Re-pinned again 2026-11-10 (g3a): inputTriangles 200000 -> 242109 and
...
assert_eq "render vis hidden triangles" "$(vfield hiddenTriangles)" "15039"
assert_eq "render vis input triangles"  "$(vfield inputTriangles)"  "242109"
assert_eq "render vis drawn triangles"  "$(vfield drawnTriangles)"  "227070"
```

也就是说：**input 已经被兄弟门禁重钉过（200000→242109），t2 里还留着旧值；hidden 则两边都钉着各自相机
的数（15039 vs 25995），互相不能借用。**

## 2 三个数各自是什么

### 2.1 input = 242109，requested = 242361，差 252

- 需求（census）：`evidence/t88-maps/demand.txt`——`world_geometry_probe --triangle-limit 4000000` 在
  2026-11-07 量得 cp_snakewater_final1 `requested=242361 dropped=0 state=within`（`:35`、`:40`）。
- 容量：T88 把 `kWorldTriangleCapacity` 从 200000 抬到 400000（`native/src/main.cpp:5973`，parse 调用 `:5974`）。
  200000 下同一张地图是 `requested=242361 dropped=42643 state=truncated`（本轮 m1 变异复现了这条）。
- 为什么 input 比 requested 少 252：displacement grid 建到一半失败时，`pushTriangle` 已经先
  `++trianglesRequested`，随后 `map.triangles.resize(before)` 把三角形吐回（`native/src/bsp_map.cpp:1309-1313`，
  `if (!displacementComplete) { map.triangles.resize(before); emittedDisp = false; }`）。
  所以 **input 与 requested 精确相等不是产品属性**；input 偏小到内存不足才是属性问题。1% 残差就是这条界线
  （本轮 252/242361 = 0.104%）。

### 2.2 hidden 是相机属性，不是地图属性

`WorldVisClipReading clipWorldTrianglesByVis`（`native/src/world_material.cpp:769-858`）的规则：

- `triangle.cluster < 0`：一律保留并 `++trianglesWithoutCluster`（`:816`）——它无法被测，所以谈不上隐藏；
- 否则逐个 cluster 问 `BspParser::clusterVisible(map, request.cameraCluster, …)`，**任一可见就保留**
  （T82 的 any-leaf 规则，`:833-840`）；
- 全不可见才 `++reading.hiddenTriangles`（`:847`），reason 串把条数写进去（`:853`）。

而 `request.cameraCluster` 由**本帧自己的 eye** 算：`main.cpp:9682` `clusterAt(displayMap, eyeX, eyeY, eyeZ)`，
只在 cluster 变化时重裁（`:9683-9687`）。所以 hidden 随相机走：本门 orbit 机位 25995，兄弟门禁自己的机位 15039，
两个数都对，但只在各自的机位成立。

本轮 trace 的 reason 里那句 `(609 kept by a cluster past the first)` 就是 any-leaf 规则的直接证据：有 609 个
三角形是"第一个 cluster 看不见、后面的 cluster 看得见"才被留下来的。

### 2.3 像素对的方向

被裁剪掉 = PVS 认定从本帧相机所在 cluster **看不见**的三角形。既然如此，同机位开/关裁剪的两帧**必须逐字节相同**；
任何一像素不同都意味着裁掉了看得见的几何。实测 `differing=0`（两帧 BMP 相同）是**产品正确**的读数，
旧断言 `>= 5%` 把它当成了缺陷。

历史包袱：`>= 5%` 是在 T80/T84 之前写的——那时裁剪查询的相机与出帧的相机不是同一台（`on vs vis-off` 差 19.59%，
T83 之前 13.44%）。T80/T84 把查询搬到本帧 eye 上之后，差值自然归零。

## 3 现在的判据（`judge_vis <map-line> <vis-on-line> <vis-off-line>`）

脚本 `:191-260` 的 `judge_vis` 只吃 trace 行，不钉任何地图常量：

| 断言 | 边界 | 本轮实测 |
|------|------|----------|
| `map budget state` | `within` | within |
| `map budget dropped` | `0` | 0 |
| `the capacity covers the map's demand` | `capacity >= requested` | 400000 ≥ 242361 |
| `the buffer holds the map's demand` | `input <= requested` 且 `input*100 >= requested*99` | 242109 of 242361 |
| `vis census adds up` | `withoutCluster + clustered == input` | 207592+34517=242109 |
| `the clip keeps every triangle with no cluster` | `drawn >= withoutCluster` | 216114 ≥ 207592 |
| `vis hidden from the census` | `hidden == clustered - (drawn - withoutCluster)` | 25995 = 34517-(216114-207592) |
| `vis identity drawn+hidden=input` | 恒等式 | 216114+25995=242109 |
| `the clip hid some but not all clustered triangles` | `0 < hidden < clustered` | 25995 of 34517 |
| `vis on clip applied / state` | `1` / `active` | 1 / active |
| `vis off state / applied / drawn = input` | `disabled` / `0` / on 的 input | 242109 |
| `the clip changes the geometry` | `drawn(on) < drawn(off)` | 216114 < 242109 |
| `clip pixel neutrality` | `--max-delta 0` | differing=0，逐字节相同 |

配套的两处硬化：

- **数字护栏**：先 `case "$inp$drawn$hidden$clustered$no_cluster" in ''|*[!0-9]*)` 判非数字，缺字段直接红，
  不让空字符串以 0 的身份混进算术（旧脚本只对 hidden 做了这件事）。
- **`map:` 行也进了读数**：`switches-tick0.txt` 现在第一行是 `map: geometry budget`，截断的 walk 会让
  `state/dropped/capacity` 三条先红——比 vis census 更早指出"这一帧根本没有那批几何"。

## 4 `--mutation`：三个缺陷，三种红法

`--mutation`（`section_mutation`，`:493-527`）在一次绿跑之后逐个注入缺陷：备份源文件 → `sed` 替换 →
`grep -qF` 确认已生效 → `bash build-target.sh tf2_demo_native` → `T2V_QUICK=1` 重入本脚本（只跑 on/vis-off
两次启动，用**同一个** `judge_vis` 判定）→ 字节恢复源文件 → `cmp -s` 校验 → 要求 rc≠0 且 FAIL 行命中预期串。
最后重建 forward 树并要求 exe sha 回到原值。

| id | 注入 | 位置 | 期望红串 | 实测 |
|----|------|------|----------|------|
| m1-capacity | `kWorldTriangleCapacity = 400000` → `200000` | `native/src/main.cpp:5973` | `the map asked for` | red=5 |
| m2-invert | `if (visible) {` → `if (!visible) {` | `native/src/world_material.cpp` | `clip pixel neutrality` | red=1 |
| m3-clusterless | 无 cluster 的三角形也记 hidden | `world_material.cpp:816` | `hid triangles with no cluster` | red=3 |

读数（`mut-*-gate.log`）：

- **m1**（容量退回 200000）：`map: geometry budget capacity=200000 requested=242361 dropped=42643 … state=truncated`，
  vis 行 `inputTriangles=200000 withoutCluster=165483 drawnTriangles=174005 hiddenTriangles=25995`；5 条红：
  `map budget state = truncated`、`map budget dropped = 42643`、`the capacity is below the map's demand`、
  `the buffer holds 200000 of the 242361 triangles the map asked for`，外加
  `clip pixel neutrality: differing=239 percent=0.0093 max_delta=109`。
  **这条顺带证明像素对不是恒真**：容量截断让"被裁掉的那批"真的从画面上少了 239 个像素。
- **m2**（把可见集当隐藏集）：只有 1 条红 `clip pixel neutrality: differing=695342 percent=26.9267 max_delta=213`，
  census 与恒等式**全绿**——它们在新语义下依然自洽。像素对才是语义判据。
- **m3**（无 cluster 的三角形也藏）：3 条红——`the clip hid triangles with no cluster (drawn 8522 < withoutCluster 207592)`、
  `the clip hid 233587 of 34517 clustered triangles`、`clip pixel neutrality differing=447453 percent=17.3273`；
  注意 `vis hidden from the census = 233587` 和恒等式仍然绿（自洽），所以只有"不藏无 cluster 三角形"这条能抓到它。
- 恢复重建后 exe sha 回到 `43b59b72…` ✓，最终 `assertions_ok=72`、`T2-VISUAL-MATRIX=PASS`。

**首跑教训（已写进脚本注释的行为约定）**：变异期望串必须是 **FAIL 行独有**的文案。第一版把 OK 文案
（"the buffer holds the map's demand"、"the clip keeps every triangle with no cluster"）当期望串，而 FAIL 行印的是
`the buffer holds 200000 of the 242361 triangles the map asked for` / `the clip hid triangles with no cluster (…)`，
于是 m1/m3 被判成"红了但不是因为预期理由"（red=5 expected=0）。

## 5 证据

树外目录 `D:/_shared/EXEC-2026-11-16/e9-t2-matrix/`：

```
pre-fix/t2pre.log                     修前基线（assertions_ok=58，3 条红原文）
forward/t2new.log                     新判据前向（assertions_ok=68，0 红）
forward/readings-switches-tick0.txt   前向 trace 读数（map/sky/vis/water 八行）
forward/comparisons.txt               五个像素对
mutation/t2mut2.log                   --mutation 首次全绿（m1/m3 期望串写错的上一版）
mutation/t2final.log                  提交字节的 --mutation（assertions_ok=72）
mutation/mut-*-gate.log               三个变异的 quick 判定日志
mutation/mut-*-build.log              三个变异的构建日志
identity.txt                          脚本/exe/demo 路径与 sha256
```

标识：脚本 sha256 见 `identity.txt`；exe `43b59b7299386aba924ecfbc79a87c5edfe0fa00e3307c60fcd4658fc600df4e`。
复跑用 `T2V_SCRATCH`/`T2V_OUT` 把产物指到 `.scratch/`，否则默认写进被跟踪的 `evidence/t2-visual-<date>/`。

## 6 已知限制

- census 恒等式依赖 trace 的字段名（`inputTriangles/drawnTriangles/hiddenTriangles/clusteredTriangles/withoutCluster`）。
  改字段名要同步改 `judge_vis`，否则数字护栏先红（这是有意的：宁可红，不要静默跳过）。
- 1% 残差是 displacement grid 的 resize 粒度。将来若出现新的"注册后又吐回"路径且幅度超过 1%，断言会红——
  那时应当先定性，再决定是放宽残差还是修产品，**不要直接把 99 改成更小的数**。
- 像素中性只能证明"被裁的三角形在本帧不可见"，**不能证明 PVS 本身与 Source 一致**：face→cluster 仍是本仓库的
  first-leaf 近似，未与 Source 交叉验证（T1 报告 §8 item 5）。本门不判这件事。
- `on vs repeat` 的 0 差异、`sky`/`water` 的像素对与机位断言仍是钉值（它们没坏，且各自有明确语义：
  确定性、开关必须改变画面、相机确实是钉的那台）。
- `world-material-check.sh` 的 `hidden=15039` 保持不动：那是它自己机位的数，两门不能互钉。
- `--mutation` 约 10 分钟（3 次注入重建 + 3 次 quick 判定 + 1 次恢复重建），会拿串行主机锁。

## 7 复现

```bash
# 前向（产物写 .scratch，不碰 evidence/）
T2V_SCRATCH=.scratch/t2v-scratch T2V_OUT=.scratch/t2v-out bash t2-visual-matrix.sh

# 三变异（自带一次前向绿跑；结束时重建 forward 并校验 exe sha）
T2V_SCRATCH=.scratch/t2mut-scratch T2V_OUT=.scratch/t2mut-out bash t2-visual-matrix.sh --mutation

# 只跑 vis 那两次启动（变异内部用的就是它）
T2V_QUICK=1 bash t2-visual-matrix.sh
```
