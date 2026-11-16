# N14/U3a 独立底图（地面线稿）：落地、构建与产品读数

- 角色：card-ui（task-3 实例3，main 相机/UI 唯一写入者；本文件作者接手被环境中断的前任实例）
- 日期：2026-11-15（夜班跨到 11-16 凌晨）
- 仓库：D:/TF2_Native_Worktrees/t105-e5，分支 candidate/e5
- 开工 HEAD：3e9ff50ac125797ebda2f5967c91f430e5960e28（docs(defects): D75 ...）
- 被测 exe：`native/build-nmake/tf2_demo_native.exe`，sha256 `26bff436…f11966`，3,042,816 B，mtime 2026-11-16 10:40:49（详见 §3）
- 证据根目录：D:/_shared/EXEC-2026-11-15/u3a/

本文件只覆盖 U3a 这一片：把 **地面线稿（basemap）** 接进小地图面板，并按 `face` 而不是按三角形做阈值判定。
它不重测 N14 的朝向/比例结论（那份在 docs/N14-MINIMAP-BASEMAP-2026-11-15.md），也不改 gate。

## 0. 复跑方式

~~~bash
cd /d/TF2_Native_Worktrees/t105-e5
bash tools/lockrun.sh card-ui -- bash build-target.sh tf2_demo_native
bash tools/lockrun.sh card-ui -- bash D:/_shared/EXEC-2026-11-15/u3a/run/capture-u3a.sh
PY=C:/Users/Administrator/.dsh/dsh-runtimes/dsh-primary-runtime/dependencies/python/python.exe
$PY D:/_shared/EXEC-2026-11-15/u3a/compare_trace.py \
    D:/_shared/EXEC-2026-11-15/u3a/run/pl_upward_f12.trace pl_upward_f12 \
    D:/_shared/EXEC-2026-11-15/u3a/expected-counters.json
~~~

抓帧参数与 tools/n14-oracle/capture-cp-traces.sh 一致（hidden-window、audio-disabled、fixture settings 的副本），
只跑两张表内地图：pl_upward_f12、cp_sunshine。

## 1. 卡级结论

| 断言 | 判定 | 依据 |
|---|---|---|
| A. 编译与链接（四文件进 exe） | **PASS**（rc=0；两个 TU 真重编；我的区域零警告） | §3 |
| B. 产品读数 = 离线转录（线稿计数） | **FAIL**（底图已画出且 shapes 自洽，但 5 个计数全部大于转录，根因待定） | §4 |
| C. 未列表地图不画底图（gate A11 的形状） | PASS（结构性，读代码） | §5：`minimapFindMapRow()==nullptr` 在 :1037 早退，早于线稿块 :1086；且 floor 几何只在 `--hud-minimap` 下推送 |

一句话：**编译通过、产品读数与离线转录一致**是两件事，本卡分别记；**像素观感**不在本卡判据内（N14 已有朝向/比例口径，见 §6）。

## 2. 改了什么（四文件）

### 2.1 native/include/minimap.h（+43）

新增跨模块搬运用的平凡数据与三个访问器：

~~~cpp
struct MinimapFloorTriangle {
  float ax, ay, az, bx, by, bz, cx, cy, cz;
  std::int32_t face = -1;
};
void minimapHudSetFloorGeometry(std::vector<MinimapFloorTriangle> triangles);
std::size_t minimapHudFloorGeometryRevision();
const std::vector<MinimapFloorTriangle>& minimapHudFloorGeometryRef();
~~~

**为什么搬数据而不是搬 BspMap**：displayMap 是 main.cpp 里那一条路径的局部量，main_hud.cpp 没有通路读到它
（与相机姿态必须"推"过去是同一个理由）。只搬三角形，不搬材质字符串、不搬平面表 —— 因为"这个面到底是不是地面"
必须在平面表与材质名都还在手边的那一层判定。

### 2.2 native/include/minimap_linework.h（+154/−24）

- 新增 `struct MinimapFloorFace { std::int32_t face; double area; double meanZ; int band; }` 与 `minimapBuildFaceTable()`；
  删掉 `minimapTriangleMeanZ`（按三角形求均值正是要被替换掉的东西）。
- `minimapFillMask()` 增加 `faces` / `triangleFace` / `facesKept` 参数，**按面判阈值**：面积、z 带、band 面积份额都改成按 face 累计。
- 阈值具名（:117-120）：`kMinimapBandHeight=256.0`（世界单位/带）、`kMinimapBandMinShare=0.05`（带占地面面积份额）、
  `kMinimapMinFaceArea=1024.0`（u²）、`kMinimapBandPad=16.0`（带自身 z 窗口）、`kMinimapUpwardNormalZ=0.7`。

**为什么必须按 face**（这三条是阈值能站住的前提，写在这里以便以后改数的人知道自己在改什么）：

1. 同一个面被拆成很多三角形，按三角形判阈值等于按"碎片"判：一个 32..512 三角形的位移面会被拆散、边缘判丢。
2. 位移面自身高差可达 498..1024 u（= 整个 4 个 256 u 的带），按三角形分带会把**一个连续坡面**撕到 4 个带里。
3. 位移面在数量上不到地面面的 9 %，但在**面积**上占 12.8 %..73.8 % —— 所以这不是边角情形，而是主要面积来源。

### 2.3 native/src/main_hud.cpp（+128/−2）

- 匿名命名空间里持有 `g_minimapFloorGeometry` 与 revision（推送时 ++），三个访问器。
- `:1004` `hudMinimapLineworkFor(projection, width, height)`：缓存键 = **geometry revision + 面板 w/h**，
  而不是 bool —— 换地图、换面板尺寸都必须重建；只缓存 bool 会把第一张地图的线稿画到第二张上。
- compose（:1086 起）在边框之后、控制点环之前画线稿矩形；只在 `lineworkReady` 时画。
- 计数器进 trace：`lw=%s floor=%zu faces=%zu kept=%zu fkept=%zu bands=%zu runs=%zu rects=%zu`，
  与 `shapes=` 一起给出 —— "没有底图"和"有底图但里面是空的"是两种读数，trace 必须能区分。
- 矩形预算 `kMinimapLineworkMaxRects=4000`：超了**报而不画**（`lw=rect-budget`），不是截断后照画。

### 2.4 native/src/main.cpp（+77）

在 `map: geometry budget` trace 之后（:5687，`if (hudMinimapCli)` 内）从 `displayMap.triangles` 挑地面三角：

- `planeIndex` 有效（越界计入 `rejectedPlane`）；
- `sideSign * plane.normal[2] > 0.7`，`sideSign = (planeSide == 0) ? 1 : -1`（朝下的面是天花板，计入 `rejectedSide`）；
- 材质前缀非 `tools/`、`editor/`（计入 `rejectedTool`），且不含 `skybox`（计入 `rejectedSky`）；
- `entry.face = triangle.faceIndex` 随三角形一起过去。

三条例外都是**载重的**：`tools/` 是游戏里看不见的 clip brush，skybox 是 16× 缩放的天空盒（N14 4.1：pl_upward 外框恰是
可玩面积的 16² 倍），朝下的面是天花板。任何一条漏掉，面板上就会在地面位置画出一个玩家站不上去的面。

## 3. 构建

## 3. 构建（2026-11-16 10:38–10:41，锁 card-ui）

- 命令 `bash tools/lockrun.sh card-ui -- bash build-target.sh tf2_demo_native`，**rc=0**（log 末行 `LOCKRUN_RC=0`，
  锁自愈行 `VERIFY-LOCK=ACQUIRED` / `VERIFY-LOCK=RELEASED`）。这是四文件**第一次被编译器看到**：昨晚 23:58 的构建
  止于 main.cpp 的语法错误（traceFmt 被截断），main_hud.cpp 与 minimap_linework.h 从未进过编译器。
- 重编范围：日志首行 `note: a header is newer than native/build-nmake/.headers-stamp; touching sources`
  （头文件依赖未被 NMAKE 跟踪，脚本 touch 全部 cpp 后全量重编）。`main.cpp.obj` mtime 10:40:10、
  `main_hud.cpp.obj` 10:40:17 —— 我的两个 TU 确实重编，不是只重新链接。
- **我的区域零警告**：全 log 15 条 warning 全是既有的（main.cpp:3590–3594 / 12191 / 12499、xutility:4813、
  native_renderer.cpp:871、native_renderer_overlay.cpp:161 / 452、native_renderer_viewmodel.cpp:390 / 391 / 698 / 868），
  没有一条落在 minimap_linework.h / main_hud.cpp / main.cpp 的 U3a 区域。
- **exe 三元组**（E1 门禁要跑的就是这个二进制）：
  - sha256 `26bff4361874a5f17b13990b1c29dd89874a56084afeed7884ee77227df11966`
  - bytes `3042816`
  - mtime `2026-11-16 10:40:49.949703100 +0800`
- identity sidecar（`native/build-nmake/tf2_demo_native.identity`，逐字）：
  `contentBinding=recompiled`、`sourceContent=9fd2b1d9679d6d8b6c2ca13c6f757c60213c9b5a31f615c401cc305664e04209`、
  `contentMethod=git-clean-v1`、`sourceTree=3c0ef8e0a475`、`sourceDateEpoch=1794795886`、
  `dirty=1 dirtyFiles=8 dirtyTracked=5 dirtyUntracked=3`、`buildUtc=2026-11-16T02:38:38Z`；
  抓帧时 HEAD=`3c0ef8e0a4756ddcf481b5f9744821acec5d0735`。
- 这个 exe 同时包含别人三件改动（card-maps 的 M2a 仪器 e5fe2219、card-arch 的 /W4 清理 363aed88 与工具链入口 ecbc9b51），
  所以"build rc=0"是**四份改动一起通过**，不是"我的改动单独通过"。

## 4. 产品读数（两张图，2026-11-16 10:41–10:43，锁 card-ui）

命令：`OUT=…/INST3/u3a-run bash tools/lockrun.sh card-ui -- bash …/capture-u3a.sh`（hidden-window、audio-disabled、
settings 用 fixture 副本，不碰 tracked fixture），两图 **rc=0**。产物：`identity.txt`、`*.trace`、`*.bmp`（1264×681，
每张 2,582,406 B，帧非空）、`*.csv`。

trace 原文（逐字，未编辑；为省宽度只省略了与本节判定无关的 eye/angle 段）：

- pl_upward_f12：`… panel=1092,12,160,160 scale=0.022321 offsetX=68.571426 offsetY=68.571426 … cps=4 drawn=4 shapes=1007 lw=ok floor=22395 faces=3869 kept=18826 fkept=2441 bands=6 runs=2080 rects=994 tick=0`
- cp_sunshine：`… panel=1092,12,160,160 scale=0.014881 offsetX=163.809525 offsetY=158.095245 … cps=5 drawn=5 shapes=825 lw=ok floor=24766 faces=5338 kept=20333 fkept=3596 bands=5 runs=1866 rects=815 tick=0`

| 判据 | pl_upward_f12（产品 vs 转录） | cp_sunshine（产品 vs 转录） | 结论 |
|---|---|---|---|
| 底图存在（非空、非 rect-budget） | lw=ok，rects=994 | lw=ok，rects=815 | **PASS** |
| shapes 自洽（=13+rects） | 1007 = 13 + 994 | 825 = 13 + 815 | **PASS** |
| 投影未受影响 | scale/offset 与 N14 表一致 | 一致 | **PASS** |
| faces（面数） | 3869 vs 3097 | 5338 vs 4326 | **FAIL** |
| kept（保留三角形） | 18826 vs 2802 | 20333 vs 4177 | **FAIL** |
| fkept（保留面） | 2441 vs 680 | 3596 vs 1215 | **FAIL** |
| runs | 2080 vs 1258 | 1866 vs 1501 | **FAIL** |
| rects（矩形数） | 994 vs 570 | 815 vs 489 | **FAIL** |

**这不是"差一点"，是模型不同**：产品在每一项上都大于转录（faces 1.249× / 1.234×），kept 相差 6.7× / 4.9×。
也就是说产品把**同一个面的更多三角形**算进了 kept，而面的集合本身比转录多约四分之一。

已排除的解释（有读数支撑，不是猜）：

- **不是材质过滤没生效**。trace 里有 `floor triangles=22395 of 66707 … rejectedTool=0 rejectedSky=0`；这两个 0 是
  **预期**的：天空面本来就不在 `displayMap.triangles` 里（native/include/bsp_map.h:259-260 的 T26 注释：sky faces 作为
  geometry 单独保留、"The product must not draw them"），tools/ 面被 `faceIsDrawable` 挡在外面。所以 0 不表示材质名为空，
  也就不解释多出来的面。
- **不是面板尺寸或缓存残留**。两张图的 scale / offsetX / offsetY 与 N14 表逐位一致，`lw=ok` 且不是 `rect-budget`；
  缓存键含 geometry revision + 面板 w/h（§2.3），第一次推送的就是本次地图，不存在上一张地图的线稿。

剩余最可能的根因（**是下一步要验证的假设，不是结论**）：转录与产品对"一个面有多少三角形"的模型不同。
`bsp_oracle.read_bsp()['floors']` 每个 dface 只给**一条多边形**，转录 `build_faces()` 用边界顶点做扇形三角化
（cpp_parity_lib.py:22-31），位移面只贡献 1..N 个扇三角形；产品用的是 BSP 里真实的三角形表（位移面 32..512 个）。
按面判面积/均值时两者可能接近，但 `kept`（三角形数）必然差一个量级；而转录的"面面积"= 扇形面积、产品的"面面积"=
三角形面积之和，对非平面的位移面不相等，足以把 `fkept` 拉开 3.6×。

**口径（不许含糊）**：
- **编译通过、链接通过、产品跑起来、底图非空** = 已证明（§3 与本表前三行）。
- **"产品线稿 = 离线转录那张图"** = **未证明**，读数显示**不是**。五个失败计数不许写成"已验收"。
- 下一步（不在本窗口）：把转录改成"按 BSP 真实三角形表覆盖每个 dface"，重算后再比 —— 若产品与重算一致，则转录是模型误差；
  若仍不一致，则产品实现有 bug，按"读代码→最小复现"定位。**无论哪条，都不是调预期。**

## 4. 产品读数

## 4. 产品读数（两张图，2026-11-16 10:41–10:43，锁 card-ui）

命令：`OUT=…/INST3/u3a-run bash tools/lockrun.sh card-ui -- bash …/capture-u3a.sh`（hidden-window、audio-disabled、
settings 用 fixture 副本，不碰 tracked fixture），两图 **rc=0**。产物：`identity.txt`、`*.trace`、`*.bmp`（1264×681，
每张 2,582,406 B，帧非空）、`*.csv`。

trace 原文（逐字，未编辑；只省略了与本节判定无关的 eye/angle 段）：

- pl_upward_f12：`… panel=1092,12,160,160 scale=0.022321 offsetX=68.571426 offsetY=68.571426 … cps=4 drawn=4 shapes=1007 lw=ok floor=22395 faces=3869 kept=18826 fkept=2441 bands=6 runs=2080 rects=994 tick=0`
- cp_sunshine：`… panel=1092,12,160,160 scale=0.014881 offsetX=163.809525 offsetY=158.095245 … cps=5 drawn=5 shapes=825 lw=ok floor=24766 faces=5338 kept=20333 fkept=3596 bands=5 runs=1866 rects=815 tick=0`

| 判据 | pl_upward_f12（产品 vs 转录） | cp_sunshine（产品 vs 转录） | 结论 |
|---|---|---|---|
| 底图存在（非空、非 rect-budget） | lw=ok，rects=994 | lw=ok，rects=815 | **PASS** |
| shapes 自洽（=13+rects） | 1007 = 13 + 994 | 825 = 13 + 815 | **PASS** |
| 投影未受影响 | scale/offset 与 N14 表一致 | 一致 | **PASS** |
| faces（面数） | 3869 vs 3097 | 5338 vs 4326 | **FAIL** |
| kept（保留三角形） | 18826 vs 2802 | 20333 vs 4177 | **FAIL** |
| fkept（保留面） | 2441 vs 680 | 3596 vs 1215 | **FAIL** |
| runs | 2080 vs 1258 | 1866 vs 1501 | **FAIL** |
| rects（矩形数） | 994 vs 570 | 815 vs 489 | **FAIL** |

**这不是"差一点"，是模型不同**：产品在每一项上都大于转录（faces 1.249× / 1.234×），kept 相差 6.7× / 4.9×。
即产品把**同一个面的更多三角形**算进了 kept，而面的集合本身比转录多约四分之一。

已排除的解释（有读数支撑，不是猜）：

- **不是材质过滤没生效**。trace 里有 `floor triangles=22395 of 66707 … rejectedTool=0 rejectedSky=0`；这两个 0 是
  **预期**的：天空面本来就不在 `displayMap.triangles` 里（native/include/bsp_map.h:259-260 的 T26 注释：sky faces 作为
  geometry 单独保留、"The product must not draw them"），tools/ 面被 `faceIsDrawable` 挡在外面。所以 0 不表示材质名为空，
  也就不解释多出来的面。
- **不是面板尺寸或缓存残留**。两图 scale / offsetX / offsetY 与 N14 表逐位一致，`lw=ok` 且非 `rect-budget`；
  缓存键含 geometry revision + 面板 w/h（§2.3），第一次推送的就是本次地图。

剩余最可能的根因（**是下一步要验证的假设，不是结论**）：转录与产品对"一个面有多少三角形"的模型不同。
`bsp_oracle.read_bsp()['floors']` 每个 dface 只给**一条多边形**，转录 `build_faces()` 用边界顶点做扇形三角化
（cpp_parity_lib.py:22-31），位移面只贡献 1..N 个扇三角形；产品用的是 BSP 里真实的三角形表（位移面 32..512 个）。
按面判面积/均值时两者可能接近，但 `kept`（三角形数）必然差一个量级；而转录的"面面积"= 扇形面积、产品的"面面积"=
三角形面积之和，对非平面位移面不相等，足以把 `fkept` 拉开 3.6×。

**口径（不许含糊）**：
- **编译通过、链接通过、产品跑起来、底图非空** = 已证明（§3 与本表前三行）。
- **"产品线稿 = 离线转录那张图"** = **未证明**，读数显示**不是**；五个失败计数不许写成"已验收"。
- 下一步（不在本窗口）：把转录改成"按 BSP 真实三角形表覆盖每个 dface"再比 —— 若产品与重算一致，则转录是模型误差；
  若仍不一致，则产品实现有 bug，按"读代码→最小复现"定位。**无论哪条，都不是调预期。**

## 5. 对 gate 的影响（gate 本体本卡未改）

| gate | 位置 | 影响 |
|---|---|---|
| A1 | `minimap-hud-check.sh:567` | `shapes == 13` **必红**。本次 exe 实测：Upward `shapes=1007`（=13+994）、Sunshine `shapes=825`（=13+815）。**但不要现在钉 1007/583**：rects 与离线转录不符（§4），根因判明前把 1007 写进门禁等于固化一个未经解释的数。给 card-arch 的输入应为「13 + 产品实测 rects」，rects 确切值待 §4 根因判明后复测。 |
| A2 | `:609-610` | inside 像素差区间 `20000..25600` 是**按无底图**的帧测的，底图必然推高它，下界/上界都要重测。:613 outside==0 不受影响（矩形按面板裁剪）。 |
| A11 | `:830` | 未列表地图仍须 `shapes==0`、帧逐字节相同 —— 由 §1.C 的早退 + 只在开关下推送几何保证。 |

N14 doc §8 要求「gate 的期望数必须与代码同一次提交」；本卡只被要求提交四文件与本文，**gate 改动不在本卡**，
所以 A1 在合并后到 gate 更新前是红的，这是已知且被点名的红，不是回归。

## 6. 本卡明确没证明的（防止被下次引用成结论）

1. **没有重测朝向/比例**。N14 的口径不变：抓帧 7/7、名字对 BSP 7/7、**朝向钉住 6/7**（koth_bagel_rc11 的 X 镜像位移
   0.000 px，不可判 = NOT VERIFIED，不是 PASS）、**轴拉伸检查非空 5/7**。判据 TOL_PX=0.05 px 出自
   mirror-sensitivity.json。本卡的两张图不改变这些分母。
2. **没有判观感**：线稿"好不好看"、"密不密"不是判据。本卡只判计数一致（§4）。
3. **没有窄窗口/DPI 用例**：面板 160×160 是后备缓冲像素、A2/A3 只在 1280×720 下跑 —— 这正是 N15（U3b）的缝。
4. **没有负控**：未构造"故意把地面判定写错、应当读出不匹配"的对照运行。§4 的一致性是正控；负控留待 gate 侧补。

## 7. 证据路径

- 离线转录与期望计数：D:/_shared/EXEC-2026-11-15/u3a/{cpp_parity_lib.py, expected-counters.json, expected_u3a.py}
- 比对脚本：D:/_shared/EXEC-2026-11-15/u3a/compare_trace.py
- 抓帧脚本：D:/_shared/EXEC-2026-11-15/u3a/run/capture-u3a.sh（产物不在这个目录）
- 本次窗口产物：D:/_shared/EXEC-2026-11-15/INST3/u3a-run/{identity.txt, *.trace, *.bmp, *.csv}；构建日志 D:/_shared/EXEC-2026-11-15/INST3/build-u3a.out、抓帧日志 capture-u3a.out
- 证据格式化脚本：D:/_shared/EXEC-2026-11-15/u3a/u3a_evidence.py（读 identity+trace+expected-counters，输出实测 vs 转录表与逐字 trace）
- 锁注记：D:/_shared/EXEC-2026-11-15/LOCK-NOTES/card-ui.txt（START/END 各一行）
