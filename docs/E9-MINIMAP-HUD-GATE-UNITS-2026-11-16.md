# E9 小地图门禁三条红的定性：两个单位错，产品行为正确 - 2026-11-16

- 任务：lead 布置的 A) minimap-hud 3 红定性（B) t2 判据修正是另一张卡）。
- 写域：`minimap-hud-check.sh`、`native/src/main_hud.cpp`、本文件。
  未碰主树、未碰 `docs/FOUR-CARD-REPORT-*` / `PROJECT-OVERVIEW*` / `PROJECT-EXECUTION-ROADMAP*`；
  主树脏 docs 未 stash、未 clean。
- 工作树：`D:/TF2_Native_Worktrees/t105-e5-u4s3`，分支 `work/card-ui-u4s3`，基线 HEAD `328bc44a`。
- 前情：三条红由 INST1 E1 第 3 轮发现、第 5 轮复现，两轮都判「口径待 card-ui 重定」并**未改脚本未钉数**
  （`docs/INST1-E1-ROUND3-TRIAGE-2026-11-16.md` §3.1-§3.3、`docs/INST1-E1-ROUND5-TRIAGE-2026-11-16.md` §3.2）。
  本卡就是那次「重定」。

## 0 结论摘要

三条红**全部是门禁的测量口径（单位）错误，产品行为正确**；没有产品缺陷，也没有回归。

| 红 | 断言原文 | 实测 | 定性 | 处置 |
|---|---|---|---|---|
| A1 | 面板交出 13 个形状（`shapes == 13`） | `shapes=1243` | 单位错：`shapes` 是**所有 primitive**，U3a 把底图的 1230 个矩形 push 进了同一个列表；13 是「标记数」 | 产品新增 `markers`（= shapes − 实际 push 的底图矩形数）与 `floorRects`；门禁改成三条独立断言（见 §2） |
| A6 | 未翻转位置在面板内部（vacuity 守卫，`bg >= 20`） | `bg=15`（另有 `floor=10`） | 单位错：「面板内部」被当成「背景像素数」；U3a 之后空白处是**背景+底图** | 分类器增 `floor` 色带，守卫改 `bg+floor >= 20`；加面板外负控（见 §3） |
| A7 | 镜像位置在面板内部（同一守卫） | `bg=8`（另有 `floor=17`） | 同上 | 同上 |

功能性断言本身三条全部成立：`nothing is drawn at the unflipped position = 0`、
`nothing is drawn at the mirrored position = 0`、`cap_1/cap_2 is drawn at the ... position = 25`。
所以红的是「证明非空洞的前提」，不是被证明的结论。

## 1 原始读数

三条红在修前 exe（`c698740f…`，U4 第 3 步构建）上的完整读数（`.scratch/mm-run3.log`，
抓帧 `.scratch/e9-minimap/20261116180307-67620/on.bmp`）：

```
minimap: hud state=ready map=pl_upward_f12 reason=ok panel=1092,12,160,160
  scale=0.022321 offsetX=68.571426 offsetY=68.571426 angle=57.00 yaw=33.00
  eye=0.0,-2048.0 viewerPx=68.57 viewerPy=114.29 viewerInside=1 arrowDots=3 cps=4
  drawn=4 shapes=1243 lw=ok floor=21566 faces=3480 kept=18061 fkept=2084 bands=6
  runs=2332 rects=1230 tick=0
```

```
FAIL the panel handed over thirteen shapes = 1243, expected 13
FAIL the unflipped position is inside the panel (so this is not vacuous) = 15, expected >= 20
FAIL the mirrored position is inside the panel (so this is not vacuous) = 8, expected >= 20
MINIMAP-HUD=FAIL assertions=20 failed=3
```

## 2 A1：`shapes` 是什么，13 和 1230 各是什么

### 2.1 13 的构成

`native/src/main_hud.cpp` 的 compose 按固定顺序 push primitive：

```
1 背景 + 4 边框条 + 4 控制点环 + 1 观察者 + 3 航向点 = 13
```

这正是 2026-11-14 写 `shapes == 13` 时（E9 补丁、还没有底图）面板交出的全部东西，
也是 `drawn=4`/`arrowDots=3`/`viewerInside=1` 之外唯一能抓到「静默少画一个 primitive」的计数。

### 2.2 1230 从哪来

N14/U3a（`6fa76eb8` 等）把底图（floor linework）的矩形**push 进同一个 `draw.primitives`**，
在边框之后、控制点环之前。pl_upward_f12 上 `rects=1230`，于是

```
shapes = markers + floorRects = 13 + 1230 = 1243
```

`rects=` 是 builder **造出**的矩形数，`floor=`/`faces=` 是几何阶段读数，都不等于「实际交给了渲染器的数」：
矩形预算 `kMinimapLineworkMaxRects=4000` 超限时 trace 写 `lw=rect-budget` 且**不画**，
此时 `rects` 仍很大而实际 push 是 0。所以「面板交出了什么」必须由 push 侧计数回答。

### 2.3 为什么不能把 13 改成 1243，也不能改钉 1230

- 把 13 改成 1243：等于把**底图矩形数**钉死。`rects` 随地图（Upward 1230 / Sunshine 815）、
  随几何阈值、随 N14 未完的模型差变化；N14 记录里 Upward/Sunshine 的现场转录与产品仍有 598/108 的
  **未证明模型差**，此时把现场数写进门禁就是固化一个未经解释的值（ROUND3 §3.1 已明确禁止）。
- 改钉 1230：同样问题，只是换个数。
- 只保留 `shapes >= 13`：会把「多画了 primitive」放过，也抓不到少画（若底图同时少画几百，总数仍 >=13）。

### 2.4 修法：两个计数 + 三条独立断言

产品（`native/src/main_hud.cpp`）：

- `HudMinimapDraw` 增 `std::size_t floorRectsPushed = 0;`，只在真正 push 底图矩形时 `++`
  （注意：`lineworkRects` 是 builder 造的，`floorRectsPushed` 是实际交出的，两者在 `lw=rect-budget` 时不同）。
- trace 增 `markers=%zu floorRects=%zu`，定义写在 `hudMinimapTrace` 上方注释里：
  `markers = primitives.size() - floorRectsPushed`，`floorRects = floorRectsPushed`。

门禁 A1（三条主张，互不替代）：

```
assert_eq "the panel handed over thirteen frame+marker shapes"  markers  13
assert_eq "the basemap pushed every rectangle it built"         floorRects  rects
assert_eq "shapes is exactly the markers plus the basemap rectangles"
          $(( markers + floorRects ))  shapes
```

第一条抓「少画/多画标记」，第二条抓「底图造了没交（预算拒绝）」，第三条抓住两者口径分裂。
三条都**不含具体底图数量**，所以换图、换几何、模型差修正都不会把它们变成假红。

## 3 A6/A7：vacuity 守卫的单位是「面板内部」，不是「背景像素数」

### 3.1 断言与实测

两条红同构（`minimap-hud-check.sh` A6/A7 的第三、四条断言）：

```
assert_eq "nothing is drawn at the unflipped position" neutral 0        # 成立：0
assert_ge "the unflipped position is inside the panel (not vacuous)" bg 20   # 红：15
```

像素实测（`.scratch/mm-probe2.py`，5×5 窗口，半径 2）：

| 窗口 | 位置(px,py) | bg | floor | 其它 |
|---|---|---|---|---|
| cap_1 未翻转 | 91.43, 34.29 | 15 | 10 | 0 |
| cap_2 镜像 | 57.14, 44.29 | 8 | 17 | 0 |

`bg` 的 15/8 不是「碰巧差一点」，而是**面板内部被底图填充后剩下的空白角落**：
采样点落在底图矩形之间的缝里，其余 10/17 个像素是底图色。

### 3.2 为什么不是产品问题

同一组的功能断言成立（`neutral = 0`，真位置 `neutral = 25`），说明投影与标记都对。
守卫要证明的只是「这个窗口确实落在画了东西的面板内部」，而 U3a 之后面板内部的正确单位是
`bg | floor`，不再是 `bg`。

### 3.3 修法：分类器增 floor 色带

`kMinimapFloorColour = {0.30, 0.34, 0.38}` → 8 位 `(76, 87, 97)`。分类器新增：

```python
if 60 <= r <= 95 and 70 <= g <= 105 and 80 <= b <= 115:
    return "floor"
```

放置位置：`neutral` 之后、`border` 之前。理由：

- `border` 带是 `100 <= r <= 195` 且三通道近灰，与 floor 带不重叠；放前面是为了不让某次
  色彩抖动把 floor 误判成 border（border 判据更宽，df 相近）。
- `bg` 带是 `r,g < 45, b < 55`，也不重叠。

守卫改为 `bg + floor >= 20`。20/25 的约定沿用门禁其余部分（5×5 窗口、4 像素半径圆盘覆盖整窗，
留一行给裁边）。

### 3.4 负控：同排面板外窗口

只把守卫放大是不够的——任何窗口只要落在面板内部都会满足，守卫仍可能变成恒真。
所以 `cmd_rings` 对每个监视点额外输出**同一行、面板左缘外 10 像素**的窗口：

```
OFFWINDOW name=cap_1 kind=noflip px=-10.0000 py=34.2857 bg=7 floor=0 other=18
OFFWINDOW name=cap_2 kind=mirror px=-10.0000 py=44.2857 bg=0 floor=0 other=25
```

门禁断言 `bg + floor <= 19`：实测 7 / 0（另两个 kind 的对照 0 / 0）。即
**同一守卫在面板内 = 25，在面板外 = 7 或 0**，两者的差距就是守卫的有效性。
窗口外的像素被判成 `other`（世界色），既不是 bg 也不是 floor。

### 3.5 为什么不改成几何判定

把守卫改成「`0 <= px <= pw`（在面板矩形内）」看似更直接，但那会让守卫**恒真**：
被采样的坐标由门禁自己按投影算出来，只要投影尺度正常就永远落在矩形内，守卫就再也不判任何东西。
保留像素计数，「面板内部」就同时要求「这里确实画了面板的东西」，这才是非空洞的含义。

## 4 负控与变异（证明新判据会红）

门禁自带的 A12 变异机制（`--mutation`：改一行源码 → 重建 → 只跑判该行的段 →
要求 rc≠0 且红行文案匹配 → 恢复 → 重建后 exe 字节必须回到原值）本轮扩到**四个**变异：

| 变异 | 改什么 | 期望红在 | 结论 |
|---|---|---|---|
| m1-yflip | `minimap.h` 的 `if (projection.yFlip)` → `if (false)` | A6「cap_1 is drawn at the flipped position」 | 抓到：rc=1，红 3 条，命中 1 |
| m2-heading | 航向点 forwardY 去负号 | A9「a heading dot 4.0px ahead」 | 抓到：rc=1，红 5 条，命中 1 |
| m3-anchor | `spec.anchor = 1` → `0` | A2「the changed pixels start at the panel's left edge」 | 抓到：rc=1，红 4 条，命中 1 |
| **m4-marker（新）** | 删一条边框条（`if (false) minimapPushRect(...top...)`） | **A1「the panel handed over thirteen frame+marker shapes」** | 抓到：rc=1，红 1 条，命中 1 |

m4 是这次新判据的负控，也是「markers 不是 A1 已有计数的复述」的证明：
它让 `markers` 从 13 掉到 12，而 `cps=4`/`drawn=4`/`arrowDots=3`/`viewerInside=1` **一个都不变**
（边框条不在这些计数里），所以只有新断言能抓到它。m4 的 trace 原文：

```
shapes=1242 markers=12 floorRects=1230 ... arrowDots=3 cps=4 drawn=4
FAIL the panel handed over thirteen frame+marker shapes = 12, expected 13
MINIMAP-HUD=FAIL assertions=16 failed=1
```

四次变异后恢复重建，exe 字节回到 `43b59b72…`（与变异前同一 sha），说明恢复是真的。
像素分析器另有 `--selftest-pixels`（无产品、无构建，合成帧）：本轮 14/14 通过。

**跑法坑（记下来免得重踩）**：`--mutation` 只在 `--sections` 含 `12` 时才真的变异。
第一次用 `--sections=1,2,6,9 --mutation` 跑出 49 条 PASS、却一条变异都没做（`section_a12` 的
`want 12` 直接 return 0）；正确是 `--sections=1,2,6,9,12 --mutation`（这次 49 条断言 + 4 次变异全过）。

## 5 证据

全部产物落在树外 `D:/_shared/EXEC-2026-11-16/e9-minimap-units/`（仓库内只有本文件与两个被改的源文件）。

| 项 | 身份 | 读数 | 路径 |
|---|---|---|---|
| 修前三红 | exe `c698740f…`，gate `f9a255fb…`，Upward demo | `MINIMAP-HUD=FAIL assertions=20 failed=3`（A1 1243≠13、A6 15<20、A7 8<20） | `pre-fix/mm-run3.log`、`pre-fix/20261116180307-67620/` |
| 修前全量 | 同上前情（E1 第 5 轮） | 96 断言 / 3 failed | `docs/INST1-E1-ROUND5-TRIAGE-2026-11-16.md` §3.2 |
| 修后分段 | exe `43b59b72…`，gate `bb42cecb…` | `MINIMAP-HUD=PASS assertions=24 failed=0`（sections 1,6,7） | `sections-1-6-7/readings.txt` |
| 修后全量 | 同上，sunshine demo = `/d/_shared/t85-assets/demos/demostf-1114998-cp_sunshine.dem` | `MINIMAP-HUD=PASS assertions=100 failed=0 skipped_controls=1`（skip 的是 A10 旧 exe 负控，需 `MINIMAP_CHECK_OLD_EXE`） | `gate-full.log`、`identity.txt`、`readings.txt` |
| 变异 | 同上，`--sections=1,2,6,9,12 --mutation` | 4/4 抓到；恢复重建 exe = `43b59b72…`；外层 49 断言 PASS | `mutation/mutation.log`、`mutation/out/` |
| 像素自检 | 无产品 | `SELFTEST-PIXELS=PASS checks=14` | `selftest.txt` |
| 树卫生 | — | `VERDICT: OK (no forbidden writes, line endings intact)` | `audit-tree.txt` |

关键读数（修后全量 `on` 帧的 minimap 行）：

```
minimap: hud state=ready map=pl_upward_f12 reason=ok panel=1092,12,160,160
  scale=0.022321 offsetX=68.571426 offsetY=68.571426 angle=57.00 yaw=33.00
  eye=0.0,-2048.0 viewerPx=68.57 viewerPy=114.29 viewerInside=1 arrowDots=3 cps=4
  drawn=4 shapes=1243 markers=13 floorRects=1230 lw=ok floor=21566 faces=3480
  kept=18061 fkept=2084 bands=6 runs=2332 rects=1230 tick=0
```

A6/A7 的像素窗口（同一份分析器输出）：

```
RING   name=cap_1 px=91.4286 py=102.8571 neutral=25
NOFLIP name=cap_1 px=91.4286 py=34.2857  neutral=0 bg=15 floor=10
OFFWINDOW name=cap_1 kind=noflip px=-10 py=34.2857 bg=7 floor=0 other=18
RING   name=cap_2 px=102.8571 py=44.2857 neutral=25
MIRROR name=cap_2 px=57.1429 py=44.2857  neutral=0 bg=8 floor=17
OFFWINDOW name=cap_2 kind=mirror px=-10 py=44.2857 bg=0 floor=0 other=25
```

## 6 已知限制与后续

- **floor 色带依赖当前配色**：换 theme/换 floor 颜色必须复测门禁。色带写在 `classify()` 的注释里，
  并注明来源（`kMinimapFloorColour`）。
- **markers 的 13 是面板组成**：以后加标记（例如车辆/运载点图标）要同步改 13 与注释；
  但底图数量永远不进这个数。
- **A1 不再钉底图数量**：底图矩形数的漂移不会让 A1 红；底图本身的正确性由 A2（面板像素差）与
  N14 oracle 守，A1 只守「面板交出的东西」的账。
- **MSYS bash 5.3.20 的算术展开**：`$(( $(cmd) + 1 ))` 在双引号里会被**原样保留**（不展开），
  必须先取出变量再 `$(( a + b ))`。已在门禁断言辅助函数上方写明；第一次写 A6/A7 时踩中，
  两条新守卫因此红成字面量，修法就是拆成变量。
- 三条红期间**没有产品行为变化**：修前修后的面板像素由 A2 的 25600 内部像素断言继续守；
  `markers`/`floorRects` 是纯读数追加，不改绘制。

## 7 复现

```bash
cd /d/TF2_Native_Worktrees/t105-e5-u4s3
# 构建（锁 card-ui）
bash tools/lockrun.sh card-ui -- bash build-target.sh tf2_demo_native
# 门禁（SUNSHINE.dem 本机缺，用 _shared/t85-assets 里的 cp_sunshine demo 代入 A5b）
MINIMAP_CHECK_DEMO_SUNSHINE='/d/_shared/t85-assets/demos/demostf-1114998-cp_sunshine.dem' \
MINIMAP_CHECK_OUT=/d/_shared/EXEC-2026-11-16/e9-minimap-units \
  bash minimap-hud-check.sh
# 变异（约 13 分钟、五次构建）
MINIMAP_CHECK_DEMO_SUNSHINE='...' MINIMAP_CHECK_OUT=/d/_shared/... \
  bash minimap-hud-check.sh --sections=1,2,6,9 --mutation
# 像素分析器自检
bash minimap-hud-check.sh --selftest-pixels
```
