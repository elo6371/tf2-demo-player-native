# T52 队伍色 X-ray 描边 —— 交付报告

**结论先说：这一版代码是对的，但门禁这一轮没有跑。** 21:12–21:18 的窗口把三层东西量清楚了：
(1) 采样侧的 class fallback 已修好并有读数；(2) 描边仍然一个像素都没画出来，原因是**世界渲染面在这条录制上从来不画玩家身体**；
(3) 据此新增了描边自己的网格源（world 优先 / bind 兜底）和 `poseSource` 读数。代码 + 夹具 + 门禁 + 变异已写完并语法验证，
**门禁与 11 条变异要在下一个窗口才跑**，所以本报告里凡未跑的都标 NOT VERIFIED，不写 PASS。

## 1. 分支与提交

- 工作树 `D:/TF2_Native_Worktrees/t52-outline`，分支 `task/t52-outline`，基线 `6b71aeb`。
- 契约提交 `b3ed5bd`；WIP `148bcfd`；本轮提交见 `git log --oneline -3`（下面第 2 节列出内容）。
- 只改本工作树；未合并、未推送、未动 `D:/TF2_Native_Test`、未碰任何别人的 ticket。

## 2. 改了什么

### 2.1 采样与连线（上一轮写的，本轮量到它对）

- 采样器在 `m_nModelIndex` 解析不到时回退到 `EntityModelResolver::defaultPlayerModelPath(m_iClass)`，并置 `modelPathFromClass`。
- 连线接受两种拼写（裸路径 / `<path>#class-fallback`），item 携带**真正匹配到的 key**。
  实测证据：同一 tick（74585）修好后 20/21 行解析出 `models/player/{soldier,scout,demo,medic}.mdl`，`classFallback=1`。

### 2.2 描边自己的网格源（本轮新增，按 lead 的 (a) + 约束 1–4）

- `main.cpp`：启动时准备 9 个 class 模型（bind pose），**单独一个列表** `overlayBindMeshes`，
  在世界面 59 个模型之后上传；上传成功的 key 才填进 `g_overlayBindMeshes`（被拒绝的 key 会变成
  '提交了、计数了、画不出来'——那正是 m8 注入的缺陷形状）。
- **不动世界渲染面的读数**：`gpu: models done N` 仍只统计世界面那一批（59），描边另打一行
  `gpu: overlay meshes prepared=9 uploaded=9`。
- `Renderer::uploadEntityModelMesh` 的上限 64 → 96 并写明原因：世界面 59 + 描边 9 = 68，
  64 会让 9 个里有 5 个被 `E_OUTOFMEMORY` 拒绝。
- 连线顺序按约束 1：**world 优先 → bind 兜底**；`OverlayDrawItem.poseSource` 记 World/Bind，
  `OverlayDrawList.poseWorld/poseBind` 计数（约束 2）。
- trace 行新增 `poseWorld=%zu poseBind=%zu`（读数，不是日志）。
- 契约：把'轮廓画在网格被画的那个姿势上'**改写成带条件的一句**（约束 3）：world 优先、无则 bind、
  并明确 bind 不跟随动画、任何断言不得声称它跟随。新增 v5 一节记下上面这些读数。
- 夹具：`states[4]` 的 class 由 1 改 0（保持'无 model 且无 fallback'语义）；新增 8 条用例覆盖
  fallback 解析、`#class-fallback` key、**world 优先于 bind**、以及'禁用列表不声明任何姿势来源'。
- 门禁：新增 `poseWorld/poseBind` 与 `submitted == poseWorld + poseBind` 断言；新增 **m9**
  （把 bind 放到 world 前面 → 探针 self-test 的排序用例变红）。

### 2.3 门禁自身的缺陷（本轮修的第 3 处）

`outline_value()` 用 `grep "^outline: draw"` 读 trace，而产品每行都带 `33234.9 ` 这样的耗时前缀 →
**锚定 grep 一条都读不到，所有字段读成空**。空值最恶毒：它同时让 `== 0` 和 `== 1` 都失败，
而对着一个'本该为空'的字段它会静静通过。现在改成不锚定，并加了一条用产品真实行形状做的自检
（读不到就红）。

## 3. 门禁与逐项读数

**本轮（21:12–21:18，owner t52-outline2-backup）实际跑到的：**

| 层 | 读数 |
|---|---|
| 构建 | 绿（含 class fallback 修复） |
| 探针 self-test | `checks=81 failures=4` ← **夹具过期**（`states[4]` 仍是 class 1，现在能解析了）；已在源码修正，本轮跑的是旧二进制 |
| 探针 fixtures | `cases=5 failures=0` |
| 探针覆盖 `--find-ticks 25` | `swept=8 withRows=8 step=25`；每 tick `rows=21 red=6 blue=7 unknown=8 posed=20 fidelity=exactTick stale=0` |
| tick 域映射 | 播放 0 处的产品 trace 自己打印 `scene: begin tick=0 sceneTick=10252` → `playbackTick = sceneTick - 10252` |
| 产品采集（5 个 tick） | playback 64229/64254/64279/64304/64329，各 33 s，BMP 2582406 B；**每个都** `samples=21 noMesh=20 noOrigin=1 submitted=0 drew=0` |
| 编排器判决 | `T52-LOCK-RUN=OVERLAY-DRAWS-NOTHING`（早退，不浪费后面 10 次采集） |

### 3.1 描边**确实画出来了**（21:36，`pick-64229.trace`，同一构建）

```text
gpu: overlay meshes prepared=9 uploaded=9 rejected=0
outline: draw ... drew=1 silhouettes=5 drawCalls=5 vertexCount=137040
             submitted=14 red=6 blue=6 unknown=2 noMesh=6 noOrigin=1 poseWorld=0 poseBind=14
```

`submitted=14`、`silhouettes=5`、`drew=1`：**这是这条任务第一次真的画出轮廓**。

> ⚠️ 读数更正（21:53 门禁跑出来才发现）：`samples=21` 不是 21 名玩家，而是 **20 名玩家 + 1 条 `CTFPlayerResource`**
> （队伍信息实体，不是玩家）。因此 `noMesh=6 / noOrigin=1` 里的那个 1 不是"某个玩家没有位置"，
> 而是**这条资源实体被 builder 正确拒掉**。错误读数保留在此，证据与更正见 §9.2。
`poseBind=14 / poseWorld=0` 同时说明了两件事：(a) 方案在工作；(b) 世界渲染面这一帧没有玩家网格可复用。
帧内的队伍色像素也是实测的（BMP 逐像素统计）：`red_total=128`、`blue_total=287`。

### 3.2 但"颜色落在哪个框里"这个断言在本 tick 上还断不了，原因是读数本身

门禁的逐实体像素断言依赖 trace 里每个 item 自己的屏幕框。实测这一帧的框是**退化的 1 像素**：

```text
outline: item entity=15 team=blue    x0=467 y0=354 x1=467 y1=354 depth=1.0000 onScreen=1
outline: item entity=16 team=unknown x0=564 y0=344 x1=564 y1=344 depth=1.0000 onScreen=1
```

而 BMP 里队伍色像素的真实范围是：红 **(502,333)-(519,342)（128 px）**、蓝 **(453,325)-(567,342)（287 px）**。
原因可读：`native_renderer.cpp:5442-5443` 用的是**常量包围盒**（±20 / ±36 地图单位）而不是网格自身的 AABB，
在这张地图的缩放比下 20 地图单位投影出来不到 2 像素；`depth=1.0000` 说明最近角点也没进远平面以内。
再加一条：这一帧红蓝两队**在画面上是挤在一起的**（蓝像素横跨 x=453..567，覆盖了红玩家框的左右两侧），
所以任何"框内不得出现另一队颜色"的断言在这个 tick 上都必然失败——不是描边错，是这一帧的几何不支持这条断言。

**最便宜的下一步（不需要新功能）**：把 `buildOverlayPreparedMesh` 的常量盒换成网格自身 AABB 的投影（约 25 行，
`EntityModelMesh` 上传时顺手存 min/max），门禁再挑一个"红蓝框中心距离 > 96 像素"的 tick（`--find-ticks` 的 8 个 tick 里可测）。

**当时没跑的**（21:36）：编排器在第 5 步判死退出，没有进入门禁。
**22:16 已经跑了**：正向门禁完整跑完，结果见 §4.0（49 OK / 10 FAIL）。
**仍然没跑的**：9 条变异、cap A/B 对照、`--playback-speed` 不变性——直接脚本在正向红处停（设计如此）。
上一轮（20:51–20:57）的门禁尝试留下的是 `NO-CAPTURE-TICK`；再上一轮（20:28–20:41）留下的是
`evidence/outline/pick-64332.trace` 里 **778 帧全部 `drew=0`** 的记录。

## 4. 变异（11 条，全部已写入门禁，本轮未跑）

| 编号 | 变异 | 必须移动的读数 | 状态 |
|---|---|---|---|
| m1 | 红蓝交换 | `red_wrong_team`/`blue_wrong_team` | NOT RUN |
| m2 | 只做深度测试 | `occluded_xray`、`depthTest` | NOT RUN |
| m3 | 开关失效且常开 | off 帧出现队伍像素 | NOT RUN |
| m4 | pass 移到 ViewModel 之后 | `orderOk`、`ordinal`、`vmOrdinal` | NOT RUN |
| m5 | 描边写深度 | `depthWrite` | NOT RUN |
| m6 | 捕获 tick 依赖 speed | speed=1 与默认 speed 帧不同 | NOT RUN |
| m7 | 去掉 class fallback | `submitted`→0、`noMesh`→`samples` | NOT RUN |
| m8 | item 只带裸路径 | `silhouettes`→0 而 `submitted`>0 | NOT RUN |
| m9 | bind 优先于 world | 探针 self-test 排序用例变红 | NOT RUN |

11 个补丁模式已用脚本验证**在源码里各出现且仅出现 1 次**（`MUTCHECK=ALL-PATTERNS-UNIQUE`，含 m9）。

### 4.1 上传上限 64→96：成对读数（不是变异）

lead 的要求：把"抬上限"这个改动本身变成可观测的，否则它和今天那些"我以为我修了"的改动没有区别。
做法是门禁里的 **cap A/B 一对读数**（同一个构建里只改这一个常量）：

| 上限 | 读数 | 期望 |
|---|---|---|
| 64（旧） | `gpu: overlay meshes prepared=9 uploaded=4 rejected=5` + 5 条 `gpu: overlay mesh refused key=... lastError=0x8007000E` | `rejected >= 1`（断言） |
| 96（现在） | `prepared=9 uploaded=9 rejected=0` | `rejected == 0` 且 `uploaded == prepared`（断言） |

被拒的 key 会"提交了、计数了、画不出来"——正是 m8 注入的缺陷形状，而它**在别的任何读数里都看不见**：
`submitted` 照样涨，`silhouettes` 照样是 0，门禁看起来只是"没画出来"。这就是为什么这一对必须单独量。

**代价（写清，不只写好处）**：`Renderer` 里每个网格是 `min(顶点数,12000)` 个 `EntityModelGpuVertex`（约 56–64 B/顶点），
所以 64→96 的上限把"最坏情况"从约 39 MB 抬到约 59 MB（+20 MB）；实际这条录制上 9 个 class 模型的 bind pose
每个约 2–4k 顶点（≈130–250 KB），**真实增量约 2 MB**。抬上限换的是"9 个职业的轮廓都能画"，
而不是内存优化；如果哪天内存紧张，正确的做法是给描边单独一个网格表，而不是把别人的预算切走。
每条变异都会先构建：**编译失败记为 MUTATION-INVALID，不算抓到**（与 MUTATION-RED 分开计）。
恢复用整文件字节复制 + `cmp` 校验，恢复后还要重跑一次正向读数确认二进制回到绿的。

## 5. NOT VERIFIED（每条带 owner 与最便宜的下一步）

| 项 | 为什么没验 | owner | 最便宜的下一步 |
|---|---|---|---|
| 门禁正向全套（像素、穿墙、非干扰、ordinal） | 本窗口没进到门禁 | t52-outline | 下一个窗口直接 `T52_TICK=64329 bash outline-check.sh`（tick 已由本轮映射确定） |
| 11 条变异 | 同上 | t52-outline | 同一窗口 `T52_MUTATION=1` |
| `--playback-speed` 不变性（speed=1 vs 默认） | 需要一次 ~16 min 的采集，本轮预算用在找'为什么画不出' | t52-outline | 已在门禁里；同窗口跑（`RUN_TIMEOUT=1800`） |
| 描边像素真的落在玩家身上 | 同上 | t52-outline | 门禁的 `outline-regions.py --team` + `outline-frame-check.py --team` 已就位 |
| bind 网格源是否真能画出轮廓 | 本轮写完代码后没有窗口 | t52-outline | 下一个窗口第一件事：`gpu: overlay meshes uploaded=` > 0 且 `submitted` > 0 |

## 6. 还没做完

1. 下一窗口：构建 → 探针 self-test 全绿 → 门禁正向 → 11 条变异 → 把实测 `TICK/TICKS/SPEED` 烘进门禁默认值。
2. T53（弹道叠加层）在 T52 全绿之后开始。

## 7. 保留窗口与逐 tick 分布

- 探针域（实体历史，SERVER tick）：`retainedFirst=74481 retainedLast=74671`，`checkpoints=2 archive=96 dropped=174 flushes=174 gap=0`；
  `--find-ticks 25` 扫出 8 个 tick，全部 `fidelity=exactTick stale=0`。
  每 tick `rows=21`，但**其中 1 行是 `CTFPlayerResource`**（§9.2）：真实分布是
  **20 名玩家 + 1 条资源实体**，玩家侧 `red=6 blue=6~7 unknown=7~8 posed=20`。
  6 个采集 tick 合计：`rows=126 withTeam=120 red=36 blue=40 unknown=50 withPose=120`（差掉的 6 条就是资源实体）。
- 产品域（播放游标）：`sceneTick = firstPacketTick + playbackTick`，`firstPacketTick=10252`（产品自己打印的 `scene: begin tick=0 sceneTick=10252`）；
  可播放窗口 playbackTick 64229..64352；本轮实测的 5 个 tick 见第 3 节。

## 8. 等待时间（原文照引）

**「三个多小时在等，不是在量。」**

同一晚、同一棵树、只是排队：

- 16:12:50 主票入队 → 19:35:19 超时被摘除：**202 分钟**，一次没拿到锁。
- ~17:39 备用票入队 → 20:28:21 拿到：**约 169 分钟**。拿到后 12 分钟做完构建+探针，然后卡在不可达 tick 上 10 分钟。
- 20:42 重排 → 20:51:35 拿到：**9 分钟**；5 分钟量出'采样 21 人、提交 0 人、778 帧全空'。
- 21:01:57 重排 → 21:12 前后拿到：**约 10 分钟**；6 分钟跑完第 3 节全部读数。
- 合计：排队约 **390 分钟**，真正在量约 **30 分钟**。
- 同期：t491-chain3 一人连续持有 15:46:33–20:14（**4 小时 28 分**），而它自报估算是 90 分钟。

**建议：按资源分锁，不要一台机器一把锁。** 今晚三类工作互不冲突却互相排队：

1. **构建锁**（nmake，CPU，1–4 分钟）：只在同树内需要串行。
2. **GPU 采集锁**（启动产品 + 抓帧，独占 GPU/窗口，15 秒–16 分钟）：这才是真正不能并行的资源。
3. **变异锁**（构建 + 采集交替，5–10 分钟/条）：可拆成'构建'与'采集'两次申请，让别人的采集插进变异构建的间隙。

分锁之后：今晚我 30 分钟的活不必等 390 分钟；而那条 4 小时 28 分的长任务也不会把五个任务堵在门外。
## 9. 单列：产品在 demo 回放里从来不画玩家身体（这不是描边的缺陷）

这是本轮最重要的产品发现，**原样留下读数**，作为下一个 owner 的起点：

```text
gpu: models done 59 uploaded=59            # 整个进程只准备了 59 个模型
scene: buildInstances=256                  # 实例请求，上限 256
scene: setInstances draws=96 markers=32    # 96 个画成网格，32 个退化成 marker（封顶）
grep -c models/player  pick-64229.trace -> 0     # 1.3 MB / 33 秒 / 0 次
outline: draw ... samples=21 noMesh=20 noOrigin=1 submitted=0 drew=0   # 每帧都这样
```

判据在 `native/src/main.cpp:2132-2147`（行号按本轮提交）：

```cpp
// 2130: 世界面的请求来自 demo 的 assetReferences
modelRenderRequests = ModelLoader::buildRenderRequests(assets, demoNetworkSummary.assetReferences, ...);
// 2132-2140: 又把 9 个 class 模型做成 classFallbacks 的请求
// 2141-2142: meshSource = 世界面请求 + class 请求
// 2145-2147: 但循环里要求 request.renderable —— 9 个 class 请求全部在这里被跳过
for (auto& request : meshSource) {
  if (!request.renderable || request.inspection.viewModelPathDetected) continue;
```

也就是说：玩家的 class 模型**被请求过**，但不是 `renderable`，于是从来没有被上传成网格；
没有网格的玩家在 `setInstances` 里落到 marker 通道（`markers=32`，且 32 是封顶数）。
**结论：这不是描边的问题——21 个玩家每帧都被采样，没有一个被画成身体。**

下一个 owner 的最便宜起点（三条，按代价排序）：
1. 打印 `buildRenderRequests` 的 `modelRequestStats`（本轮 trace 里没有这行），确认 9 个 class 请求是哪一步掉成非 renderable 的；
2. 检查 `request.inspection` 对这 9 个模型返回了什么（`metadata.bindPoseVertices.size()` 是否为 0 / companion 是否缺失）；
3. 若是材质/companion 缺失导致 `renderable=false`，那是实体模型管线的缺陷，修好后描边的 `poseSource=bind` 会自然降到 0（代码不用改）。

## 10. 门禁缺陷一节（今天这条家族的第三个实例）

`outline-check.sh` 里读 trace 的辅助函数原来是：

```bash
grep -a "^outline: draw" "$1" | tail -1 | tr " " "\\n" | grep -a "^$2=" | cut -d= -f2
```

产品 trace 每行都带耗时前缀（`33234.9 outline: draw ...`），**锚定 grep 一条都读不到**，
于是 `enabled/drew/submitted/...` 全部读成空。空值在这里比读错值更隐蔽：

- `assert_eq field 0` 会失败，`assert_eq field 1` 也会失败 → 看起来很吵，容易以为是断言写错；
- 但对着一个**本该为空**的字段（例如'off 帧不应该有队伍像素'），空值会让断言**静静通过**。

这就是 lead 说的'空值双向掩盖'：同一处缺陷既能伪造失败，也能伪造通过。
修法不是'把断言放宽'，而是三件事一起做：
1. 去掉行首锚点；
2. 在门禁开头加一条**用产品真实行形状**的自检（`12.3 outline: draw ... submitted=3` → 必须读出 3），读不到就红；
3. 让'读不到'变成 `<none>` 而不是空字符串，这样所有断言都能看出是'没读到'而不是'读到了空'。

## 4.0 门禁正向运行（第二次，修复后）：**57 OK / 1 FAIL**（23:00–23:22）

三处修复（采样器精确 `CTFPlayer`、item 框用网格自身 AABB、D3D11 状态读回打印）编译通过、
探针 `self-test checks=97 failures=0`、正向门禁从 **49 OK/10 FAIL → 57 OK/1 FAIL**。

**唯一还红的一条**：
```text
FAIL differing pixels outside every drawn region = 297, expected 0
```
同一 tick 的旧盒/新盒对照（lead 要求的那条）在同一行里：
```text
outline: item entity=2  team=red  x0=480 y0=335 x1=504 y1=342  legacy=497,339,497,339   ← 25x8 px，旧盒 1 px
outline: item entity=5  team=blue x0=607 y0=336 x1=627 y1=340  legacy=622,337,622,337   ← 21x5 px，旧盒 1 px
```
**框修对了**（旧盒确实是 1 像素），但"区域外不得有差异"仍不成立：我自己逐像素量了一遍这一帧，
`differing=484`，其中 `outside=424`，且这些像素到最近框的切比雪夫距离分布是 `1px:39, 2px:39, ..., 23px:1`
——**不是 1–2 像素的描边外扩，而是一圈最远 23 像素的光晕**。这与 shader 里的 `kOverlaySilhouetteExpand` 一致：
pass 画的是被**外扩过**的轮廓壳，而 item 框报的是**网格本身**的范围。所以要么框改报"实际绘制范围"，
要么这条断言的区域按外扩量放大——**两者必须同一个来源，否则框永远小于画**。
（门禁报 297、我量 424，差值本身也是读数：门禁的 region 取的是 trace **最后一帧**的 item，而捕获帧不一定是最后一帧，
这两个数不一致就说明"区域取自哪一帧"还没钉死。下个窗口一并钉。）

### 4.1 那条红的**下一轮排除**：两个假设都被量掉了

**(a) "区域取自最后一帧而不是被拍那一帧" —— 排除。** 改了 reader（`outline-regions.py` 现在默认取
`capture: armed` 之后的第一帧，`--frame captured|last|all` 三档），离线用同一批 BMP 重量：
```text
capture: armed requested=64229 frame_tick=64229 frames=316
regions captured=14  regions last=14        ← 两套框完全一样（这一跑捕获后没再动）
noninterference: diff_outside=297 diff_inside=187 regions=14   VERDICT FAIL   ← 一个像素都没变
```
所以 297 不是对齐问题。**这个改动本身仍然要留**：它把"区域取自哪一帧"从没说清变成写死的一条规则，
并写进 docstring——落在某天"悄悄给出正确样子"之前。

**(b) "画的是外扩壳，框是网格本体" —— 也排除。** shader 的外扩是：
```hlsl
mapped += unitNormal * cameraPositionAndExpand.w * mode.x;   // w = kOverlaySilhouetteExpand = 0.004
```
`0.004` 是**世界单位**，在 player ≈ 20 单位 ≈ 10 像素的比例下是 **0.002 像素**——外扩不是 23 像素光晕的来源。

**(c) 剩下的读数，逐连通域量的**（on/off 差集，8 连通）：
```text
4 个 blob:  (453,325)-(474,338) 22x14   (502,333)-(519,342) 18x10
            (557,335)-(569,341) 13x7    (480,336)-(488,342)  9x7
14 个报告的框，全部落在 y=340..351 这一带，例如 (506,340)-(513,342)、(559,341)-(565,343)
```
**框系统性地比 blob 低 8–14 像素、且纵向被压到 2–3 像素（blob 是 10–14 像素）。**
这不是"框小一点"，是**框和人不在同一个位置**——所以下一步不再是调框的大小，而是查这个偏移：
按 item 打印 `cacheKey / vertexCount / worldMin / worldMax / origin`，与 blob 的 bbox 对照，
就能判断是 (i) `boundsMin/Max` 对被蒙皮的网格不成立（模型空间 AABB × 骨骼变换 ≠ 蒙皮后范围），
还是 (ii) 送进框的 `mvpForOverlay` 与画的时候用的不是同一个矩阵（帧内先后次序），
还是 (iii) 框取到了别的实例的行。**三种都有测法，但都要再开一次窗口。**

### 4.2 `depthWrite=1` 的定案（**措辞已按 §7.2 更正**）：是运行时归一化，不是产品写深度

判读规则在跑之前写死（`stateWrite=0 ⇒ 缺陷在赋值路径`；`stateWrite=1 ⇒ 状态本身没带上掩码`），22:51 的读数：
```text
per-frame: depthTest=0 depthWrite=1      states: stateTest=0 stateWrite=1
tested control: depthTest=1 depthWrite=0   ← 同一个 desc，DepthEnable=TRUE 建出来的那个，掩码被保留了
```
代码在 `native_renderer.cpp:1327` 要的是 `D3D11_DEPTH_WRITE_MASK_ZERO`；同一段代码用 `DepthEnable=TRUE` 建的对照状态
读回来就是 `ZERO`。**同码同掩码、两种答案，差别只在 `DepthEnable`** ⇒ `DepthEnable=FALSE` 时 D3D11 不保留写掩码，
写掩码此时是惰性的，"不写深度"由 `DepthEnable=FALSE` 保证，不由掩码保证。

**因此**：门禁不再断言 `depthWrite==0`（那是在断言一个运行时会重写的字段），改为**记录**该读数并断言 `depthTest==0`；
契约里"depth write off"这句要改写成机制描述。**行为层面"这一帧深度缓冲没被改"仍未测**（NOT VERIFIED，owner：我，
最便宜下一步：在 overlay pass 前后各读一次深度缓冲并逐字节比较——渲染器已有 `captureDepthBuffer` 的 staging 读回路径）。

## 4.0-旧 第一次正向运行：`OUTLINE-CHECK=FAIL`（49 OK / 10 FAIL，21:52–22:16）

这是第一次真正跑完正向门禁（不是被编排器挡在门外）。**它不再是"什么都没画"**：

```text
on.drew=1 on.silhouettes=5 on.submitted=14 on.red=6 on.blue=6 on.unknown=2
on.poseWorld=0 on.poseBind=14        (submitted == poseWorld + poseBind ✓)
world models: prepared=59 uploaded=59   (worldModelUploads 断言 ✓ 通过)
on.ordinal=6                        ✓
vm.ordinal=6 vm.vmOrdinal=7 vm.orderOk=1 vm.drew=1   ✓✓ （带 --viewmodel 的那次采集）
off: enabled=0 drew=0 disabled=21，off 帧红蓝像素 0                      ✓
test mode: depthTested，穿墙像素 = 0，与 off 帧在该区域逐字节相同        ✓
```

**通过的关键项**：描边真的画（`drew=1/silhouettes=5`）、每个提交的轮廓都声明了姿势来源、
世界面模型数不被扰动（59/59，advisory→断言，见 §9.1）、覆层序号 6 在 ViewModel 之前（带 viewmodel 时 `vmOrdinal=7 orderOk=1`）、
关掉开关后一像素不画、深度测试模式下一像素不穿墙。

### 10 条 FAIL，按根因分四类（没有一条是"描边没画"）

| # | 读数 | 根因 | 谁的 | 最便宜的下一步 |
|---|---|---|---|---|
| 1-4 | `rows carrying a team slot = 120, expected 126`；`rows carrying a pose` 同；`rowsMissingTeam/Pose = 6` | 采样器把 `CTFPlayerResource` 当玩家（§9.2） | 我 | 过滤器精确匹配 `CTFPlayer`，重建；断言不用改 |
| 5 | `x-ray: depth write = 1, expected 0` | **未定案**：`native_renderer.cpp:1327` 用 `MASK_ZERO` 建状态、1351 读回，静态路径不可能得 1，而 exe(21:33:23) 比源码(21:32:35) 新，所以不是在跑旧二进制 | 我 | 在 1348 读回处打印两个 state 的 `(test,write)`，重建再读一次；两次运行定案 |
| 6-7 | `diff_outside=<none> diff_inside=<none>` | 门禁把 4701 个 region 塞进 argv → `Argument list too long`，python 根本没跑 | 我 | 已修：`regions_file=`（见下），并且 region 应只取**最后一帧**的 item，而不是整条 trace 的并集 |
| 8-9 | `red/blue pixels in the player region = 0`，`occluded region = 1x1` | item 框退化 1 像素（§3.2），于是"区域"里没有任何像素 | 我 | 常量盒 → 网格 AABB（lead 已批准） |
| 10 | `team-coloured pixels THROUGH the wall = 0, expected >=8` | 同上：occluded region 被 1×1 框裁成 1 像素 | 我 | 同 #8-9 |

### 已修并**离线验证**的一条：non-interference 的 argv 溢出
`regions_file=` 加进 `outline-frame-check.py`，用本轮真实产物复跑：
```text
diff_outside=391 diff_inside=93 regions=4701 | VERDICT FAIL
(坏 region 文件自检：region wants x0,y0,x1,y1: 1,2,3  ✓ 拒绝，不是静默跳过)
```
**`diff_outside=391` 不是"描边改了世界像素"**：`regions=4701` 是整条 trace 所有帧 item 的并集（998 帧 × ~5），
而这一帧只画了 14 个轮廓。

于是顺手从根上修掉它：`outline-regions.py` 现在默认只读**最后一帧**（BMP 拍的就是那一帧）的 item，
`--frame all` 保留旧行为。离线复测（同一份本轮产物）：
```text
--csv            → 14 个框 / 225 字节      （--frame all 仍是 4701 个 / 94 KB，就是它撑爆了 argv）
--team           → red=510,343 blue=565,343
noninterference  → diff_outside=478 diff_inside=6 regions=14   VERDICT FAIL
```
**这一组数字把问题钉死了**：本例覆层一共改变了 **484** 个像素（`478+6`），而帧内队伍色像素是 `128+287=415`，
两者同一量级 → **484 个差异像素全部是描边自己的轮廓**，不是世界像素被改。
但 14 个退化 1 像素的框只豁免了其中 6 个，所以"区域外必须逐字节相同"这条断言**现在只能报"框不够大"**。
**AABB 框修好之后它才会变成真正的 non-interference 断言**——这是同一个根因的两个症状，不是两个缺陷。

## 9.1 产品与离线互相矛盾：**运行时的 `renderable` 还没有定案**（lead 要求单列）

两边的读数都是别人测好的、都保留在这里，**结论只能是"矛盾未解"**：

| 侧 | 读数 | 出处 |
|---|---|---|
| 离线 | 9 个 class 模型全部 `resolution=found-vpk, vvd=true, vtx=true, meshBuilt=true`；`entity_model_probe` 给出 `uniqueRenderable=4 scoutVertices=12000` | `model_robustness_probe` / `entity_model_probe`（t50-camera2 的只读调查） |
| 产品 | `gpu: models done 59`（世界面只准备 59 个）；`entityMeshKeyByPath` 里没有玩家路径；`poseWorld=0 poseBind=14` | 本任务 21:36 的 `pick-64229.trace` |

**两边不能同时为真。** loader 层没有缺陷（离线能建出 scout 的 12000 个顶点），而运行时的 9 个 class 请求没有变成网格。
**本轮不追这条因果**，因为它不属于 T52，而且我手上没有能定案的读数。

⚠️ 报告里**不得**写成"因为世界面没画玩家，所以描边改用 bind"——那是因果断言，因果未证。
能说的是：`poseWorld=0 / poseBind=14` 是实测的姿势来源分布，它说明这一帧里描边**没有**从世界面拿到网格；
**为什么**没有，要等一次带 trace 的运行定案。

**最便宜的定案动作（t50 给的，两行 trace，属于另一个任务）**：
`main.cpp:2159` 之后加 `gpu: class requests=%zu renderable=%zu firstDiagnostic=%s`，并在 2164 的循环里按丢弃原因计数。
一次运行能区分三层：`requests=0`（构造）｜`renderable=0 + diagnostic`（inspection）｜`renderable=9 但 models done 仍 59`（合并/去重）。

**本窗口已经把这条不变量写成了门禁断言**：`assert world pass model uploads == 59`（并打印 prepared/uploaded 实测值）。
这条断言存在的理由是：`main.cpp:2159-2161` 把 9 个 class 请求并进了同一个 `meshSource`，
所以"世界面仍然是 59"**不是构造保证的**，只是这次运行恰好成立。读数会变的东西不是不变量；现在它变了门禁会红。

## 9.2 采样器把非玩家实体当成玩家（门禁抓到的真缺陷）

门禁 3/6 节的两条断言在 21:53 变红：

```text
FAIL rows carrying a team slot = 120, expected 126
FAIL rows carrying a pose      = 120, expected 126
FAIL sampled rows with no team value = 6, expected 0
FAIL sampled rows with no pose       = 6, expected 0
demo summary ticks=6 rows=126 distinctEntities=21 withTeam=120 withPose=120
tick requested=74482 ... entity=31 class=CTFPlayerResource player=1 team=unknown raw=0 hasTeam=0 hasOrigin=0
```

`CTFPlayerResource` 是**队伍信息实体**，每 tick 一条被 `player=1` 采进来（6 个 tick 共 6 条）。
于是 `rows=126` = 120 玩家行 + 6 资源实体行；`withTeam=120`、`withPose=120` 正好差这 6 条。

**这是"断言是对的、被测对象错了"的实例**，不是"断言写错了"：
修掉过滤器之后 `rows=120 / withTeam=120 / withPose=120`，**那两条断言一个字都不用改就会通过**。
判据（lead 的表述，写进报告以便复用）：**修完断言不动就绿 = 对象错了；必须改期望值 = 断言错了。**

**修法（下一个窗口，一行）**：玩家过滤器用**精确类名** `CTFPlayer`，不用"排除 `*Resource`"。
理由：白名单会漏掉新东西，黑名单会漏进新东西，而后者更难发现——它看起来已经修了。

帧不受影响：builder 因为 `hasOrigin=0` 拒掉这些行并计入 `noOrigin`。
所以 `noOrigin=1` 的正确读法是"这条资源实体被正确拒绝"，**不是**"某个玩家没有位置"。

## 10.1 门禁设计：三条可复用判据

1. **变异要按"能不能把断言弄红"判，不是按"能不能改一张它改不动的图"判。**
   m9（bind 优先于 world）在这条录制上改变不了产品帧——世界面根本没有玩家网格，两种顺序画出来的都是 bind。
   但它能弄红探针里那条"两源都有答案时 world 必须赢"的用例，所以它是一个有效的变异。
   反过来，一个"能让截图变化"但弄不红任何断言的变异，只是噪声。
2. **断言要能自己退休。** `submitted == poseWorld + poseBind` 证明"每个提交都有姿势来源"，
   但它**不要求 `bind > 0`**：等产品真的开始画玩家身体那天，`poseBind` 会自然归 0，这条断言仍然成立。
   如果写成 `assert poseBind > 0`，产品变好的那天门禁会变红——那是把缺陷固化成了规格。
3. **"读不到"必须和"读到空"分开。** `outline_value` 锚定 grep 读不到任何字段时，所有断言拿到的是空串：
   它同时让 `== 0` 和 `== 1` 失败（很吵），却对一个"本该为空"的字段静静通过（很危险）。
   现在读不到就是 `<none>`，并且门禁开头用产品真实行形状自检这个读取器本身。

## 11. 同族发现（读到了字，读错了字段）

今晚这条家族一共五个成员，全部是'有两个答案，而代码只信了其中一个'（第 5 个是"没跑"与"零差异"）：

1. **tick 域**：`firstPacketTick` 同时存在三个答案——`main.cpp` 注释里写 29368、探针打印 1、产品自己打印 10252。
   只有第三个是产品在说它自己。注释已改成不带数字的推导；探针把 `firstRecordedPacketTick` 与 `serverTickOrigin`
   分成两列；采集 tick 改为'在播放 0 处抓一帧，读产品自己打印的偏移'来算，而不是从探针 tick 猜。
2. **锁状态**：`hostlock.sh status` 里有 `LOCKED owner=`（持有者）与 `reservation=`（预约者），我把 `reservation=` 读成了持有者，
   以为自己在跑，其实别人在跑。lead 当场纠正。
3. **tick 列表**：探针只扫保留窗口内的 tick（`[retainedFirst..retainedLast]`），而门禁早期把播放 tick 直接当查询 tick，
   于是六个 tick 全部 `rows=0`——'描边坏了'和'这一帧没有玩家'读起来一模一样。
4. **网格 key**（本轮）：同一个事实'这个玩家用哪个模型'由两条路径生产，而连接只用了一条在用的 key。
5. **`Argument list too long`（本轮，22:16 门禁）**：14 个 item 框撑到 4701 个（整条 trace 的并集）后被塞进 argv，
   python 进程根本没起来，`diff_outside=<none>`——**"脚本没跑"和"差异为零"读起来一模一样**。
   这是"空值双向掩盖"的第三个实例，而且这次空值的来源不是 grep，是**进程没启动**：
   同一族的下一个变种是"脚本跑了但没读到输入"，再下一个是"退出码被 `2>&1` 吞掉"。
   修法：`regions_file=` 让区域不走 argv，并且 region 默认只取被拍那一帧（4701 → 14）。
   判据：**任何把自己的产物喂回自己作参数的断言，都要有一条"喂不进去时它会喊"的路径**。

## 11.1 下一窗口的补丁计划（行号级，未应用）

写成计划而不是先改：**没有锁就不能编译，未编译的改动进了树只会让下一个窗口多花时间查语法**。

### A. 采样器：精确匹配 `CTFPlayer`（§9.2）
- `native/src/entity_overlay.cpp` 的玩家判定（`isPlayer` 处）改成类名 == `"CTFPlayer"` 的精确比较；
- 断言不用改：修完 `rows=withTeam=withPose=120`。

### B. item 框：常量盒 → 网格 AABB（§3.2，lead 已批准）
1. `native/include/native_renderer.h:1269-1275` `EntityModelMesh` 加 `float boundsMin[3]; float boundsMax[3];`
2. `native/src/native_renderer.cpp:3485-3494` 上传时顺手求 min/max（顶点就在手上，6 行）
3. `native/include/native_renderer.h:1234-1238` `OverlayInstanceRecord` 加世界空间 `worldMin[3]/worldMax[3]`
4. `native/src/native_renderer.cpp:3310-3316` 用 `rows.positionRows` 变换 AABB 的 8 个角，存世界 min/max
5. `native/src/native_renderer.cpp:5437-5463` 把 `halfX/halfZ`+`center` 换成 record 的 8 个角，投影循环不动
6. **同 tick 对照读数**：`OverlayItemReading` 加 `legacy[4]`，仍用旧常量盒算一遍，item 行加 `legacy=x0,y0,x1,y1`
   —— 这样"盒子修对了"和"断言放宽了"在同一行里能分开读。

### C. `depthWrite=1` 定案（§4.0 第 5 条）
- `native_renderer.cpp:1348` 读回处加一行 trace：`gpu: overlay depth states xray(test=%d write=%d) tested(test=%d write=%d)`；
- 重建后**一次采集**就能定案：若打印 `xray(test=0 write=0)` 而 draw 行仍报 `depthWrite=1`，则缺陷在 `overlayPassReading_` 的赋值路径；
  若打印 `write=1`，则缺陷在状态创建（`MASK_ZERO` 没有生效），产品确实写了深度。

## 12. 这次给 lead 的建议

! 21:52–22:16 的门禁已经跑过一轮正向（§4.0）。下面是**下一窗口**的排法，已按 lead 的优先序调整：

1. **一个窗口、一次重建、三个修复 + 一条打印**：
   ① 采样器精确匹配 `CTFPlayer`；② 常量盒 → 网格 AABB（附同 tick 旧盒/新盒对照读数）；
   ③ `depthWrite` 在 `native_renderer.cpp:1348` 读回处打印两个 state 的 `(test,write)`；④ 重建。
2. **正向跑完先定案 `depthWrite=1`**，再进变异。它有两种相反解释（产品真写深度 / 读回路径坏），
   在定案之前 T52 **不能**声称"不改变世界几何或普通可见性"——那是验收契约里的句子，不是可选项。
3. 变异 9 条 + cap A/B + speed 不变性：窗口不够时按 lead 的次序砍——**先砍变异条数、最后砍 speed=1**，
   砍掉的每一条写成 NOT VERIFIED 并说明原因；**不许把"没跑"写成"不适用"**。
4. 预计纯机器时间：构建 2–4 min + 正向 7 次采集（含 16 min 的 speed=1）+ 变异 9 次重建与采集 ≈ 70–90 min。
2. 第 9 节那条产品缺陷另开任务，不要塞回 T52；它的读数和判据行号已经写在这里。
3. 分锁（第 8 节）。今晚我 30 分钟的活等了 390 分钟，这不是我一个人的损失。

## 13. 附：本轮产物

- `evidence/outline/OVERLAY-CONTRACT.md`（v4 连线、v5 网格源与姿势来源条件句）
- `evidence/outline/outline-offline-selftest.py`（离线自检，无 GPU）
- `evidence/outline/pick-64332.trace`（778 帧 `drew=0` 的原始证据，gitignored）
- 门禁 `outline-check.sh`：11 条变异、对齐表、pose 断言、`outline_value` 自检
- 探针 `native/tools/entity_overlay_probe.cpp`：`classFallback=`/`reason=`/`serverTickOrigin=` 三列 + 8 条 fallback 用例

## 7. 变异与配对读数（第一次全量，00:06 结束）

9 条变异全部**重建**，每条自带 `cmp` 还原：日志里 **27 条** `restored ... byte-identical (cmp)`，跑完 `git status` **0 行**。

| 变异 | 结果 | 读数（变异后 vs 声明的固定值） |
|---|---|---|
| m1 红蓝互换 | **RED** | 错队名额里的队伍像素 `95`（固定 `0`） |
| m2 pass 一律深度测试 | **RED** | 穿墙队伍像素 `0`（固定 `68`） |
| m2 子断言 x-ray depth test | **点不亮** | `0`（固定 `0`）——这条子断言没有可动的读数 |
| m3 无视开关恒开 | **RED** | `--outline off` 帧队伍像素 `415`（固定 `0`） |
| m5 overlay 写深度 | **INVALID，不是 RED** | 见 7.1 |
| m6 捕获 tick 依赖速度 | **RED** | speed=1 与 speed=64 捕获不同帧 |
| m7 取消 class 回退 | **RED** | 提交轮廓 `0`（固定 `14`）、noMesh `20`（固定 `6`） |
| m8 轮廓带裸路径 | **RED** | 画出轮廓 `0`（固定 `5`） |
| m9 bind mesh 优先 | **RED** | 探针 self-test 失败 `5`（固定 `0`） |
| m4 ViewModel 之后画 | **RED** | 次序 `0`（固定 `1`）、ordinal `7`（固定 `6`） |

### 7.1 m5 是**假红** —— 必须先说清楚

```bash
must_move "m5" "x-ray depth write state" "0" "$(outline_value "$OUT/m5.trace" depthWrite)"
```
期望值是**硬编码的 0**，而**固定构建自己**的正向 trace 三次都读 `depthWrite=1`。
固定值本来就是 1 时写死一个 0 当"固定值"，这条断言**不跑变异也会红**——没有区分力。
⇒ **m5 记 INVALID**。修法：期望值从正向 trace 取；而"它该是几"取决于 4.2 的定案。

### 7.2 4.2 的措辞更正：读数是读数，解释是解释

已测到的：同一份代码、同一个 `ZERO` 掩码，`DepthEnable=FALSE` 的状态读回 `ALL`（`stateWrite=1`、每帧 `depthWrite=1`），
`DepthEnable=TRUE` 的对照读回 `ZERO`（`depthWrite=0`）。**两个答案，只差一个字段。**
因此"掩码惰性、不写深度靠 `DepthEnable=FALSE`"是**解释**，不是直接测到的 D3D11 内部行为；
**行为层（这一帧深度缓冲有没有被改）仍未测**：NOT VERIFIED，owner 我，最便宜下一步 = overlay pass 前后各 staging 读回一次深度缓冲逐字节比。

### 7.3 cap A/B 配对读数：**没读出来**（3 条 FAIL，NOT VERIFIED）

```text
FAIL cap 64: the old limit REFUSES class meshes (this is what 96 buys) = 0, expected >= 1
FAIL cap 96: every prepared class mesh is uploaded = <none>, expected 0
FAIL cap 96: uploaded count matches prepared = <none>, expected 9
```
`<none>` 不是"读数为 0"，是**没有读数**：这两次 run 的 trace 里没有 `gpu: overlay meshes prepared=` 行。
其余 8 条变异都有读数，只有 cap 段是 `<none>` ⇒ 最可能是这几次 run 的 trace 没落在断言读的那条路径上。
⇒ cap A/B **NOT VERIFIED**，owner 我，下一步：先核对 cap 段用的 trace 文件名与 `run_frame` 实际写出的名字。

### 7.4 可重复性（被测的，不是假设的）

同一构建、同一参数、正向门禁**连跑两次**：`57 OK / 1 FAIL`，失败那条与读数完全一致（`diff_outside=297`）。
带变异的那一轮算上 cap 段是 `61 OK / 4 FAIL`，其中 3 条 FAIL 全在 cap 段（见 7.3）。

## 8.0 判读规则（**采数之前写死**，2026-10-23 09:3x）

在跑任何一次采集之前，把三种可能结果和各自意味着什么写下来。读数回来只做对照，不再解释。

新增的诊断读数（item 行旁，一条独立 `outline: itemdiag` 行）：
`key`（实例 cacheKey 的哈希）、`meshVerts`（框用的那个网格的 vertexCount）、`skinned`（该实例是否走骨骼）、
`bmin/bmax`（**模型空间**边界）、`origin`（该实例 origin 用同一个 mvp 投到屏幕的像素坐标）、
`range`（这一批 draw 的 `vertexCount/instanceCount`）。

| 观察 | 结论 | 下一步 |
|---|---|---|
| `origin` 投影 ≈ blob 中心，box 中心偏下且扁 | **形状问题**：框的范围不是这块几何的范围 | 查 `bmin/bmax` 是否 0 或退化；查 `skinned=1` 时"绑定姿态边界 × rows"是否等于"蒙皮后范围" |
| `origin` 投影也偏下 8–14 px（与 box 一起偏） | **位置问题**：这个实例的 origin/矩阵与画出来的不是同一份 | 查 `OverlayInstanceRecord` 与 GPU 实例是否同序（`range.instanceStart` 索引两张表） |
| `meshVerts` ≠ `range` 的 vertexCount | **框量的网格 ≠ 画的网格** | 查 bucket 分组（`bucketOf.emplace(item.cacheKey, ...)`）与实例落桶 |
| `skinned=1` 且 `bmin/bmax` 明显小于 blob 的竖直范围 | **绑定姿态边界被当成蒙皮后范围** | 修法：绑定时把边界也按骨骼变换（或用世界渲染面自己的 bounds） |
| 读数一条都没有 | 诊断打印没走到（item 未进入该分支） | 先确认 `outline: item` 行是否还在 |

**不许**在看到读数后改这套判据；如需新增判据，另起一节注明"采数之后追加"，并说明为什么原来的判据不够。

### 8.1 Q1 / Q2 的答案（2026-10-23 09:45 的采集）

**Q1：这个样本不是相机自己的玩家。** `scene: camera mode=first-person target=1 follows=N` —— 相机跟的是 **entity 1**，
而所有 item 是 **entity 2–17**。所以"模型包住相机"的退化假设不成立。
**同时更正我上一条的措辞**：`w=0.33` 是**归一化空间**的 w，不是 TF2 单位——把它换算成"8 毫米"是我把两套单位混了。

**Q2：框的像素尺寸与解析投影一致，门禁的尺子没坏。**
```text
box=25x8   ndc=0.0380,0.0193   0.0380/2*1264 = 24.0 px   ✓
box=57x15  ndc=0.0885,0.0409   0.0885/2*1264 = 55.9 px   ✓   0.0409/2*681 = 13.9 px ✓
box=25x10  ndc=0.0374,0.0263   0.0374/2*1264 = 23.6 px   ✓
```
按事先写死的判读表：**两者一致 ⇒ 不是"门禁自己的像素测量错了"**（那一行记 INVALID）；
而"blob 仍是 6 倍"这一行**今天没有复现**——今天这次采集的框是 **21–57 px 宽、5–15 px 高**，
和之前那帧的 blob（9–22 px）同量级。

**所以那条红目前的正确状态是：不可复现，原因是验收样本本身不稳定。** 证据：
```text
same tick 64229,  same arm:   on.trace  capture: armed ... frames=512   draws=513
                             diag.trace capture: armed ... frames=839   draws=840
```
同一个 tick、同一个 arm 帧，**进程在抓到帧之后又跑了 300+ 帧才退出**，而 `scene: camera ... follows=N` 每帧递增、
`sampledTicks` 944 vs 1296 ⇒ 同一 tick 的**相机状态在两次 run 之间不同**（之前那帧玩家在 w≈0.33，今天在 w≈0.06–0.13）。
**在相机状态不固定的样本上比较"框 vs 像素"没有意义**：框和像素虽然同帧，但两帧之间的玩家远近差 3–5 倍，
残差被相机拖着走，不是被框拖着走。

⇒ 下一步不是修框，是**把验收样本钉住**（`--capture-tick` 之外还要固定 arm 之后的帧数/相机收敛），
或者在**同一次 run 内**同时导出框和像素做比较。

### 8.2 两条报告口径（按 lead 要求单列）

- **在 w 很小（相机贴近玩家）的退化样本上得出的任何结论都不进验收。** 今天这次采集的 w=0.06–0.13 也属于"相机离得很近"，
  同样只作归因用，不作验收读数。
- **`grep -m1` 取到进度行是新的一族："第一个匹配不是那条读数"**，不是"缺席被读成良性"。
  （缺席族是 `Argument list too long` 那条：命令没跑，读数 `<none>` 被当成 0。）
  两族的共同点只是都表现为"读到了一个看起来合理的数"，判据不同：一族是**没有读数**，一族是**读错了那一条**。

### 8.3 那条红的重新定级：证据指向样本漂移，但 on/off 这一对是同一个场景

判据先写死（写在这批数之前）：

- 同场景 ⇒ 世界像素几乎全部一致，差异只出现在描边自己的像素上（几百个量级）；
- 不同场景 ⇒ 差异铺满整帧（几万到几十万），因为相机或插值变了。

读数（22:16 那一对 on/off）：
```text
484 differing pixels of 1264*681 = 861k     => 同一个场景
```
⇒ 22:16 的 1 FAIL 不是「跨运行比了不同场景」，它是真的：那一帧描边画在它自己公布的框之外。

但同一 tick 不同时刻的 run 之间不是同一个场景，这一点直接量到了：
```text
on.trace    (22:16)  frames=512   follows=945    玩家 w≈0.33      框 2-4 px
diag.trace  (09:45)  frames=839   follows=1298   玩家 w≈0.06-0.13  框 21-57 px
```
同一个 --capture-tick 64229、同一个 arm tick，渲染帧数差 300+、相机跟随计数差 350、玩家远近差 3-5 倍。
⇒ 跨小时的两次 run 不能互相引用；只有同一次门禁里背靠背的 on/off（以及 off/off2）才能比。

### 8.4 前置断言已落地（outline-check.sh，非干涉比较之前）

不满足时输出：
```text
FAIL precondition: the two runs did not see the same scene (N >= 17220 differing pixels)
     every region assertion below measures the camera, not the overlay, so this run proves nothing
```
判据是机械的：场景不同 ⇒ 差异铺满整帧；场景相同 ⇒ 只有描边像素差。不去猜「差多少算多」。
这是本轮唯一新增的前置断言：没有它，区域类断言在样本漂移时会给出看起来合理的错数。

## 8. 交接：唯一那条红 + 下个窗口三步

**读数**：4 个绘制 blob 与 14 个框，X 中心逐像素吻合（463/510/562/484 全中），Y 偏 2–18 px 且被压扁；
尺寸比：`blob 21x14 vs 框 17x4`、`18x10 vs 9x3`、`13x7 vs 7x3`、`9x7 vs 7x3` ⇒ **两轴都小、Y 更狠（1.2–3.5 倍）**。
已排除：帧对齐（captured 与 last 两套框完全相同、`diff_outside` 一个像素没变）、
shader 外扩（`0.004` 世界单位 = 0.002 像素）、两角点投影（代码是 8 角点）、串轴（换方向不改跨度）。

**候选**：`AABB 覆盖的顶点比画出来的少`——最像 `uploadEntityModelMesh(cacheKey, vertices)` 的 `vertices` 是一个子网格，
而绘制是 `drawCalls=5 vertexCount=137040` 的合并轮廓。

**下个窗口三步**（一次重建就能定因）：
1. item 行打 `cacheKey / mesh.vertexCount / boundsMin / boundsMax`，与 `drawCalls / vertexCount` 对照；
   同时打 `projectedOrigin`，用于判定"位置错"还是"形状错"。
2. 定因：若 key 相同而 `vertexCount` 不同 ⇒ 同名两对象（家族第三次现身）；若 key 不同 ⇒ 子网格选错。
3. 修 + 重建 + 正向（21 分钟）+ 变异全量（45 分钟）。

**还欠的 NOT VERIFIED**：行为层"不写深度"（owner 我）、cap A/B 配对读数（owner 我）、m5 的期望值（owner 我）。