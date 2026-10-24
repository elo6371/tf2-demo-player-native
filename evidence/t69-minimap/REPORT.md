# T69 小地图纯逻辑模块 — 实现与门禁报告

| 字段 | 值 |
| --- | --- |
| 任务 | task-12「T69 基础：小地图纯逻辑模块（投影 / 命中 / 折叠状态）+ probe + 门禁」 |
| owner | t54-verify |
| 状态 | 完成：门禁 27/27 PASS，变异 4/4 CAUGHT，源码逐字节还原 |
| 工作区 | `D:/TF2_Native_Worktrees/t69-minimap`（git worktree） |
| 分支 / 基点 | `task/t69-minimap` @ `99eeea294cbe883e4c3e6b4b82c83de45d2ab926`（= 集成树 p0-entity-protocol 当前 HEAD） |
| 时间 | 2026-10-24 12:37 起编写；最终锁窗口 13:08:15 ACQUIRED，13:09:44 收尾 |
| 二进制 | `native/build-nmake/minimap_probe.exe` 175616 B，sha256 `288b44da039d5ea9dab1d46f8463f1fd8db704f5b43c6f77599ea1ecb0ea7a60`，mtime 2026-10-24 13:09:30 |
| 结论 | `T69-LOCKED-RUN=PASS`，`LOCKED-COMMAND-EXIT rc=0` |

## 1. 目标与非目标

**目标（本任务只做这些）**

1. 小地图的**纯逻辑**：世界坐标 ↔ 小地图像素投影、可逆往返、鼠标命中（hit test）、面板折叠/展开状态机、缩放与视图复位、不可用地图的具名原因。
2. 一个 test-only probe，直接链接 `src/minimap.cpp`（与产品将来链接的**同一个翻译单元**），用合成 bounds 打印全部可观测读数。
3. 一个能变红的门禁 `minimap-check.sh`：缺 probe 必须 NOT RUN/FAIL，不允许零断言误报 PASS。
4. 一个变异套件：每个变异都是"能编译的错"，必须让**具名断言**变红，然后逐字节还原并重建回基线。

**非目标（明确不做，等后续任务）**

- 绘制：小地图底图/圆点/文字渲染属于 T67 HUD 图元层，本任务不碰 `native_renderer.*`。
- 产品集成：不改 `native/src/main.cpp`、`native/src/demo_header.*`，不做 CLI/热键接线。
- 真实 BSP 加载：本模块只接收调用方给的 `MinimapBounds`；从 BSP 现算 bounds 是产品侧后续工作（T65 的 `bsp_geometry.py` 是算法参考，不是产品代码）。
- KOTH 真机：`koth_bagel_rc13.bsp` 本机不存在（T65 结论），本任务只用合成 bounds 覆盖 unavailable 路径，不伪造 KOTH 投影。

## 2. 交付物

| 文件 | 字节 | sha256 | 说明 |
| --- | --- | --- | --- |
| `native/include/minimap.h` | 7093 | `28c3e5037293aa23b66a5caaf28260a822c3b4f3181a34358e20eddae467e8bd` | 纯逻辑 API + `MinimapPanel` 状态机声明 |
| `native/src/minimap.cpp` | 8140 | `865550a4b16dfd7e2132ea6e75267b91a74487481fc8447d0b15e298cb6d5487` | 投影/逆投影/命中/面板实现 |
| `native/tools/minimap_probe.cpp` | 12677 | `bc6e32920fe21d94fce4e31f43cbb81f083aad5767a65030b8c549d87575e04d` | `--minimap-test` 探针，31 行读数 |
| `minimap-check.sh` | 13780 | `ed7e73811aa9f2e2c6d71783185ffbb0275e472ececcec5c58bae4439c2ec72f` | 门禁：10 段 / 27 断言 |
| `native/CMakeLists.txt` | — | `49559d2bd436dd3b6e0a6b0199f012193191b897ab08ef682b0cc787baa022ad` | 新增 `minimap_probe` target（`/W4 /permissive- /EHsc /utf-8`，与 hud_probe 同警示条） |
| `evidence/t69-minimap/run-locked.sh` | 2122 | `0e90373cf77d3bdb98fd02b13ab9d64ccb0b0b6f6dc465606a8325e0e914ea41` | 一个锁窗口内 build → gate → 变异 → 收尾基线 |
| `evidence/t69-minimap/run-mutations.sh` | 5502 | `e1e6f06d99360fb5e937dbb563a43f53f8a12f1a96ce53cbd078f9a0c1225eda` | 变异套件驱动 |
| `evidence/t69-minimap/mutation/patch_once.py` | — | `e4958a1a6648511ae7c1d4ce750c6e0e4e9df5f8bd9ad63ff7d91c4b375053c4` | 精确一次替换；needle 命中数 ≠ 1 即 MUTATION-INVALID |

`git status` 在本 worktree：`M native/CMakeLists.txt` + 5 个未跟踪新文件（`minimap-check.sh`、`native/include/minimap.h`、`native/src/minimap.cpp`、`native/tools/minimap_probe.cpp`、`evidence/t69-minimap/`）。无其它改动；未 stage、未 commit。

## 3. 设计与契约

### 3.1 数据结构（`minimap.h`）

- `MinimapBounds`：`float minX,minY,maxX,maxY; int pixelWidth,pixelHeight; bool valid=false`。
- `MinimapProjection`：`float scale,offsetX,offsetY; bool yFlip=true,valid=false`。
- `MinimapPoint`：`float px,py; bool inside=false`。
- `MinimapPlayerState`：`uint16 index; int team,health; bool alive,visible; float x,y; std::string name`。
- `MinimapPanel`：`setMap/available/unavailableReason/collapsed/expand/collapse/toggle/zoom/panX/panY/setZoom/setPan/resetView/projection/hitTest` + 可观测计数器（`expandCount/collapseCount/zoomChanges/hitTests/hits/misses/lastHitIndex`）与 `stateLine()`。

### 3.2 投影（唯一公式，`native/src/minimap.cpp`）

```
usable := valid && pixelW>0 && pixelH>0 && spanX>0 && spanY>0 && 全部有限
scale  = min(pixelW/spanX, pixelH/spanY) * zoom          // 等比，绝不拉伸
offsetX = (pixelW - spanX*scale)*0.5 - minX*scale + panX  // 先居中，再平移世界原点
offsetY = (pixelH - spanY*scale)*0.5 + maxY*scale + panY
px = worldX*scale + offsetX
py = yFlip ? offsetY - worldY*scale : offsetY + worldY*scale
inside := 0<=px<=pixelW && 0<=py<=pixelH
```

**为什么用 `min`**：小地图必须等比；用 `max` 会把地图裁掉，用非等比会扭曲方向感。**为什么先居中**：bounds 不保证以原点对称，居中后 pan=0 时地图自然落在画布中心。**为什么 yFlip**：BSP 世界 +Y 向北，屏幕 +Y 向下，翻转一次才符合"北在上"。

### 3.3 往返与命中

- `minimapToWorld`：逆变换；投影不可用时返回 `false` 且**不写**输出参数（三值读取：调用方必须检查返回值，不能把未写值当结果）。
- `minimapHitTest`：只看 `visible==true` 的玩家；`distance > radiusPx` 跳过；`kTieEpsilon=1e-4f`；替换条件 `bestIndex<0 || closer || (|dist-best|<=eps && player.index<bestIndex)` → **等距取小 index，与遍历顺序无关**（避免 hover 时因容器顺序变化而"误点"到另一个人）。

### 3.4 面板状态机

默认 `collapsed_=true`（T69：右上角默认折叠）；`expand/collapse/toggle` 各自计数；`setZoom` 改变 zoom 并计数；`resetView` 回 zoom=1/pan=0（不重置 expand/collapse）；`stateLine()` 一次性打印全部状态，供门禁做字符串级断言。

### 3.5 不可用地图（三值读取，不画空白）

`minimapUnavailableReason` 依次判：`valid==false` → `<map>:bounds-invalid`；span<=0 → `<map>:world-span-nonpositive`；pixel<=0 → `<map>:pixel-size-nonpositive`；空地图名用 `unnamed-map`。原因**必须具名**，对应 T69 "无地图/坐标不可用时显示原因，不留空白画布"。

## 4. 门禁 `minimap-check.sh`（10 段 / 27 断言）

| 段 | 断言 | 期望值来源 |
| --- | --- | --- |
| 0 | probe 存在、rc=0、输出 31 行、`probe done ok=1` | 主体存在性；缺 probe 直接 NOT RUN/FAIL RC=2 |
| 1 | 合成投影 `valid=1 scale=0.125000 offsetX=0 offsetY=512 yFlip=1` | 手算：bounds 0..4096 @ 512px → 512/4096=0.125 |
| 2 | 4 个点像素 `(0,512)/(512,0)/(128,128)/(256,384)` tol 1e-3 | 手算（yFlip 后的坐标） |
| 3 | 4 条 roundtrip errorPx < 1e-4 | 逆变换必须可逆 |
| 4 | yflip 方向：world y=4096 → py=0 在 world y=0 → py=512 之上 | 手算方向 |
| 5 | hitTest nearest=3 / tie=0 / outside=-1 / invisible=-1；forward/reversed 顺序一致（3/3、0/0） | 手算 + 顺序无关性 |
| 6 | collapsed 初值=1；expand 后 0、collapse 后 1；计数 1/1 | 状态机契约 |
| 7 | zoom=2 时点 3 距离 = 2×128=256、像素 (256,512)、roundtrip 0、zoom2 hitTest=0 | 手算缩放几何 |
| 8 | `koth_bagel_rc13` unavailable：available=0 projectionValid=0 reason 非空；degenerate span 的 reason 非空 | 合成 bounds 的不可用路径 |
| 9 | **T65 ledger 交叉核对**：`cp_snakewater_final1` 与 `pl_badwater` 的 canvas 矩形喂给产品公式，scale 必须等于 ledger 常量（tol 1e-6） | T65 静态审计：`evidence/t65-minimap/data/projection_ledger.json`，sha256 `e2d35aae52942cba14157b5f6fb7f798d161302c52c43b084a3c7427af4a67bc` |

**ledger 交叉核对为什么成立**：T65 ledger 的两张画布矩形在世界单位下恰好是正方形（snakewater span=13079.04，badwater span=7820.8），所以产品公式的 `min(pixelW/spanX, pixelH/spanY)` 退化为 `1024/span`，与 ledger 的 `scalePxPerUnit` 同源。实测（float32）：

| 地图 | 产品 scale（probe） | ledger 期望 | 差 | 结果 |
| --- | --- | --- | --- | --- |
| cp_snakewater_final1 | 0.078293204308 | 0.07829320806420043 | 3.76e-9 | OK |
| pl_badwater | 0.130932897329 | 0.1309328968903437 | 4.39e-10 | OK |

差值来自 float32 的 24-bit 尾数（远小于 1e-6 阈值，也远小于 T65 的 halfpx 误差预算 6.386/3.819 px）。**这些常量只出现在 probe 的测试 fixture 和门禁期望值里，产品代码没有任何硬编码**——产品从调用方给的 bounds 现算。

**NOT RUN 行为已验证**：把门禁拷到 `/tmp/t69-notrun`（probe 缺失）运行 → `NOT RUN: probe missing`、`MINIMAP-CHECK=FAIL`、`RC=2`，日志 `logs/gate-notrun-demo.log`（sha256 `b91b8c7896b70dda970c0ca4464ad6978d8770466ff2e1b6b2c762103e7e5480`）。

## 5. 变异套件（4 个变异，全部 CAUGHT）

每个变异先 `patch_once.py`（needle 必须恰好命中 1 次，否则 MUTATION-INVALID）→ `build-target.sh minimap_probe`（rc 必须 0，证明是"能编译的错"）→ 跑门禁 → 具名断言变红记 CAUGHT → `cmp` 还原 → 重建 → 基线 PASS。

| 变异 | 注入的缺陷（锚点） | 首个变红断言（原文） | 门禁读数 |
| --- | --- | --- | --- |
| m1 | `worldToMinimap` 的 `offsetY - worldY*scale` 改成 `+`（y 不翻转） | `FAIL point index=1 px=512.000000 py=1024.000000 inside=0 expected=512,0 tol=0.001` | 12 ok / 13 bad |
| m2 | 删掉 hitTest 的等距 tie-break（`if (bestIndex < 0 || closer || tieWithSmallerIndex)` → `if (bestIndex < 0)`） | `FAIL hittest nearest: result=0 (expected 3)` | 23 ok / 2 bad |
| m3 | `MinimapPanel` 默认 `collapsed_ = true` 改成 `false` | `FAIL panel collapsed initial: collapsed=0 (expected 1)` | 23 ok / 2 bad |
| m4 | `MinimapPanel::hitTest` 忽略 zoom（用 `makeMinimapProjection(bounds_,1.0f,...)`） | `FAIL zoom2 hittest: result=-1 (expected 0: the hit test must use the same zoomed projection it draws with)` | 24 ok / 1 bad |

其余红行（同一次变异内）：
- m1 还红：point index=2/3 像素、4 条 roundtrip、yflip、hitTest、zoom2 系列——y 翻转是全局变换，坏一处全坏，符合预期。
- m2 还红：`hittest order: forward/reversed disagree or wrong (nearest 0/3 expected 3/3, tie 0/3 expected 0/0)`——证明 tie-break 变异被顺序无关性断言抓住。
- m3 还红：`panel expand/collapse: collapsed 0/1 expandCount=0 collapseCount=1 (expected 0/1 1 1)`——初值错导致计数也错。
- m4 只红 1 条：单点缺陷只被单条断言抓住，其余 24 条仍绿——这是"断言足够细"的证据，不是漏测。

**还原证明**：变异前备份 `.scratch/mut/backup/{m1,m2,m4}.minimap.cpp` 与 `.scratch/mut/backup/m3.minimap.h` 的 sha256 与当前源码完全相同（`865550a4…` / `28c3e503…`），即逐字节还原；每个变异后都重建并跑基线，`BASELINE-RESTORED name=mX rebuild_rc=0 gate_rc=0`。

## 6. 锁窗口运行读数

**两次窗口，最终证据取 run2**：

- run1（2026-10-24 12:50:45 ACQUIRED，队列第 4 位）：25 断言全绿、变异 4/4，日志保留在 `logs/run1/`。该版本尚未包含第 9 段 ledger 交叉核对。
- run2（2026-10-24 12:57 排队，13:08:15 ACQUIRED，等待约 11 分钟）：加入 ledger 交叉核对后重跑，**27 断言全绿、变异 4/4、收尾基线 PASS**，为最终证据。

run2 读数（原文）：

```
=== step 1: build minimap_probe ===   BUILD_RC=0
=== step 2: gate baseline ===         GATE_RC=0
assertions_ok=27 assertions_bad=0
MINIMAP-CHECK=PASS
=== step 3: mutation suite ===        MUTATION_RC=0
mutations_caught=4/4
MUTATION-SUITE=PASS
=== step 4: final baseline ===        FINAL_BUILD_RC=0 FINAL_GATE_RC=0
assertions_ok=27 assertions_bad=0
MINIMAP-CHECK=PASS
T69-LOCKED-RUN=PASS
LOCKED-COMMAND-EXIT rc=0 owner=t69-minimap
```

probe 关键行（`logs/probe.txt`，31 行）：

```
minimap: projection valid=1 scale=0.125000 offsetX=0.000000 offsetY=512.000000 yFlip=1 zoom=1.000000
minimap: point index=0 ... px=0.000000 py=512.000000 inside=1
minimap: point index=1 ... px=512.000000 py=0.000000 inside=1
minimap: point index=2 ... px=128.000000 py=128.000000 inside=1
minimap: point index=3 ... px=256.000000 py=384.000000 inside=1
minimap: roundtrip index=0..3 errorPx=0.000000000
minimap: hittest case=nearest ... result=3 / tie ... result=0 / outside ... result=-1 / invisible ... result=-1
minimap: hittest order forward_nearest=3 reversed_nearest=3 forward_tie=0 reversed_tie=0
minimap: panel initial collapsed=1 zoom=1.000000 expandCount=0 collapseCount=0
minimap: panel after_expand collapsed=0 expandCount=1 collapseCount=0
minimap: panel after_collapse collapsed=1 expandCount=1 collapseCount=1
minimap: panel after_zoom zoom=2.000000 zoomChanges=1
minimap: zoom2 point=3 px=256.000000 py=512.000000 center=256.000000,256.000000 distance=256.000000
minimap: zoom2_hittest point=0 ... result=0
minimap: panel after_reset zoom=1.000000 panX=0.000000 panY=0.000000 zoomChanges=2
minimap: panel state collapsed=1 zoom=1.000000 panX=0.000000 panY=0.000000 expandCount=1 collapseCount=1 zoomChanges=2 hitTests=1 hits=1 misses=0 lastHitIndex=0 available=1 reason=-
minimap: unavailable map=koth_bagel_rc13 boundsValid=0 projectionValid=0 reason=koth_bagel_rc13:bounds-invalid available=0
minimap: unavailable map=degenerate boundsValid=1 projectionValid=0 reason=degenerate:world-span-nonpositive available=0
minimap: ledger map=cp_snakewater_final1 scale=0.078293204308 expected=0.078293208064 offsetX=470.661194 offsetY=528.285034 valid=1
minimap: ledger map=pl_badwater scale=0.130932897329 expected=0.130932896890 offsetX=405.158752 offsetY=527.842896 valid=1
minimap: probe done ok=1
```

构建：`build-target.sh minimap_probe` rc=0（`logs/build.log`、`logs/final-build.log`，内容 `=== build minimap_probe === / rc=0`；构建器只输出结论行，无告警行）。二进制 175616 B sha256 `288b44da…`（比 run1 的 174592 B / `2c2cba1f…` 大，因为新增 ledger 两行）。

**锁结束状态**：run2 结束后 `hostlock.sh status` 显示 owner 已切到 `t67-gate`（我释放后队列正常推进），无残留 `t69-minimap` 锁；`product_processes` 由后续 owner 持有。本任务未 break 任何人的锁。

## 7. 已知限制与 NOT VERIFIED

| 项 | 状态 | 说明 |
| --- | --- | --- |
| KOTH `koth_bagel_rc13` 真实投影 | **NOT VERIFIED** | BSP 本机不存在（T65 已确认；不得用 rc5/rc10/rc11/rc12 冒充）。门禁只用合成 bounds 覆盖 unavailable 路径，reason 具名 `koth_bagel_rc13:bounds-invalid`。 |
| 绘制层（底图/圆点/文字/悬停） | 未实现（非目标） | 等 T67 HUD 图元渲染器；本模块只保证"渲染器要消费的数字"正确。 |
| 真实 BSP bounds 集成 | 未实现（非目标） | 产品侧需从 BSP 现算 bounds；本模块只接收 bounds 输入。T65 的 `bsp_geometry.py` 是算法参考。 |
| float32 精度 | 已量化 | ledger 规模下 scale 误差 ~4e-9，远小于 1e-6 门禁阈值与 T65 halfpx 预算。 |
| hitTest 几何 | 圆形半径 | 不含图标矩形/重叠像素面积；重叠时取最近、等距取小 index，符合"不得因 hover 重排误点"，但最终 UI 层仍需人工验收（T69 IA 系列）。 |
| 面板计数 | 仅 `Panel::hitTest` | `hitTests/hits/misses` 只统计经面板的命中调用，直接调自由函数不计。 |

## 8. 结论与下一步

**结论**：小地图纯逻辑层已实现并通过门禁（27/27）、变异（4/4 CAUGHT）、逐字节还原验证。T65 ledger 的两个真实地图投影常量与产品公式一致（复算 diff=0；门禁 float32 tol 1e-6 通过），且这些常量只存在于测试 fixture/门禁期望值，产品代码零硬编码。`unavailable` 路径具名，不会画"看似正确的空白小地图"。

**下一步（建议）**：
1. T67 HUD 图元渲染器接入绘制层：用本模块的 `projection/worldToMinimap/hitTest` 出图，按 T69 IA-1..IA-12 做 UI 验收（默认折叠、展开、悬停名字、点击立即切镜头且不触发 0.3s 自动接力、缩放/复位/记忆、不可用原因、多层提示）。
2. 产品侧 BSP bounds 集成：加载地图时从 BSP 现算 `MinimapBounds`（参考 T65 `bsp_geometry.py` 的 z 过滤与组件过滤，避免"原始 min/max 不是可玩区"的坑）。
3. KOTH 资产决策：需 lead 决定是否获取 `koth_bagel_rc13.bsp`；在拿到之前 KOTH 保持 NOT VERIFIED 且具名。

## 附录 A. 证据清单（sha256）

| 文件 | sha256 |
| --- | --- |
| `logs/gate-baseline.log` | `352fb579a259b2bdab14a126b5f3d537fba328d157e2e78c5410e912a35c59fe` |
| `logs/gate-final.log` | 同上（逐字节相同） |
| `logs/build.log` | `bc1a23aa71812f9e2841d345bd92a4fb80990878252ee2b97572cdd24aa539be` |
| `logs/final-build.log` | 同上 |
| `logs/mutations.log` | `a6f9b00f23808b5f7c7b1d93b3cdec88154ccf24ac02c6881eb6f85fa734a8db` |
| `logs/probe.txt` | `980006f5d2343869781b02598c24e3a9cd05a1287957087dd8bb9c17ff793327` |
| `logs/identity.txt` | `8e42053a43d3cac265a781deaad1eb6ec2fc755172a59529287971143e0f384a` |
| `logs/gate-notrun-demo.log` | `b91b8c7896b70dda970c0ca4464ad6978d8770466ff2e1b6b2c762103e7e5480` |

其它：`logs/mutation-gates/{m1,m2,m3,m4}.log`（每次变异后的门禁原文）、`logs/mutation-builds/`（patch/build/rebuild 日志）、`logs/run1/`（第一次窗口，25 断言版本）、`mutation/needles/*`（变异 needle/replacement 原文）。
