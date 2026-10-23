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
2. 第 9 节那条未归因的红另开任务，不要塞回 T52；它的读数和判据行号已经写在这里。
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

### 8.5 两条判据的措辞定版（lead 定，2026-10-23 10:00）

**① 场景相同与否是物理判据，2% 只是它的机械形式。**

- **物理判据**：场景不同 ⇒ 差异铺满全帧（相机/插值变了）；场景相同 ⇒ 差异只落在描边自己那几个像素上。
- **机械形式**：把上面这条写成可执行断言时，取帧面积的一个比例（2%）当阈值。

**理由永远是物理判据，不是阈值。** 阈值只是把物理判据变成一行代码的手段；
写报告、争论读数、判"这条红算不算"时，引用的必须是"两次运行看到的是不是同一个场景"，不是"有没有超过 2%"。
实测锚点：`484 differing pixels / 861k` ⇒ 同场景；跨小时的两次 run ⇒ 差异铺满整帧。

**② 引用任何旧 trace 之前，先跑一次前置断言。**
跨小时的两次 run 之间不能互相引用（同一 `--capture-tick` 下 `frames=512` vs `839`、`follows=945` vs `1298`、
玩家远近差 3-5 倍）。任何"用昨天的 trace 和今天的 trace 比"的论证，**先证明两次运行看的是同一个场景**，否则作废。

### 8.6 "同运行导出"的准确记法：NOT VERIFIED，不是"没做"，也不是"已解决"

> 跨运行比较已被前置断言约束；**单次运行内同时导出框与像素未实现**，
> 因此**"框与绘制同源"这一条 NOT VERIFIED**。

owner：我（t52-outline2）。最便宜下一步：渲染器在一次 run 内渲两遍（带/不带 overlay）到两个 target 再读回比较，
这样"框 vs 像素"不再依赖任何跨运行假设，前置断言也就不再需要。排在下一个窗口第一项。
### 8.7 那条红的距离分布：按 lead 的判据，RED 成立

判据先写死（测量之前）：

| 读数 | 判定 |
|---|---|
| max_outside <= 线宽/2+1 且集中在一条细环 | 断言前提错（要求描边不存在）→ INVALID |
| max_outside 远大于线宽，或差异是实心块而不是环 | 真的画到框外 → RED |
| 差异是环但也吞进框内一大片 | 描边朝内画 → 另一个缺陷 |

读数（484 个差异像素，对 14 个框一签有符号距离，框外记正）：
```text
outside=424   inside=60   （60 个框内像素的深度全部 <= 1 px）
max_outside = 23 px    p50 = 10 px    p95 = 20 px

 1-4 px  154   5-9 px  51   10-14 px 107   15-20 px 100   21-23 px 12
```
**不是窄峰，是铺开到 23 px 的双峰**；线宽对应的膨胀量是 0.004 世界单位 ≈ 0.002 px。
⇒ 落在判读表**第二行**：RED 成立。
更硬的一条：**484 个像素里只有 60 个落在任何框内，且都在边界 1 px 内**——框基本没盖住画出来的东西。

### 8.8 第 7 个家族成员：变异没应用，用例还是报了判定

```text
PATTERN NOT FOUND:           ? overlayXRayDepthState_.Get() : overlayDepthTestedState_.Ge
built (0 compiler errors)
MUTATION-GREEN m2: team-coloured pixels through the wall=68 (fixed=68)
```
为了发布 `stateBound`，我把绑定那一行重构成先读进 `boundDepthState`；**m2 的替换锚点于是对不上**。
`patch_in` 打了 `PATTERN NOT FOUND`，然后**照样 build、照样跑、照样给判定**——把「无效」报成了「绿」。
这比红危险：红会有人看，绿不会。

修法（已落地）：
- 锚点跟着代码改到 `boundDepthState` 的 ternary；
- 新增 `mut_applied`：`patch_in` 返回非零 ⇒ 该用例记 **MUTATION-INVALID** 并**不跑到断言**。

与已有家族的关系：m5、`stateBound`、`grep -m1` 那条是**同一个根**——**以为自己固定住了自变量**；
这一条是它的一个变体：**以为自己应用了变异**。

### 8.9 三条结论分开记（按 lead 的定级，2026-10-23 10:45）

```text
T52 验收（队伍色 / 遮挡 / 开关 / 实体消失 / 无玩家夹具）  := 由变异与正向覆盖，见 §7
姿态一致性（任务书 NEXT-TASKS:566-569 "prove pose at each sampled tick"）:= NOT VERIFIED（代理失败，原因未定）
产品公布的 AABB vs 绘制几何                                := 四数并列，未定因（见 8.9.2）
```

**不得把第 2 条写成"产品缺陷"，也不得写成"通过"。** 它是这一次没有证明的东西。

#### 8.9.1 更正一处口径

我先说过这条"不在任务书里"——不准确。任务书写的是 **"First prove player entity identity, team mapping, and pose at each sampled tick"**，
"框 vs 像素"正是为 prove pose 写的代理。**代理选错了**（用产品公布的 AABB 当代理），但要求本身在范围内。

#### 8.9.2 两个候选都能用已有数据判掉，剩下的确实未定因

| 候选 | 判定 | 依据 |
|---|---|---|
| 框来自顶点的**子集** | **排除** | `uploadEntityModelMesh` 的 bounds 循环是 `for (const auto& vertex : local)`，覆盖整个数组，无截断；`meshVerts=12000 == range=12000` |
| 框来自**另一个变换** | **排除** | `native_renderer.cpp:3291` 把 `rows.positionRows[row*4+column]` 逐字拷进 `gpu.position`，`:3343` 的 8 角用**同一个** `rows.positionRows` —— 绘制与框是同一个数组，不是"两个变换" |

顺带记一处可疑但**不影响结论**的写法（`:3336-3341`）：8 角的位选择是 `(corner&1)`, `(corner&4)`, `(corner&2)`——
位的次序与轴的次序不一致。**AABB 仍然完整**（8 种组合都枚举到了，只是组合顺序换了），所以它不是差异来源，但读代码的人会误以为少了一半。

⇒ 两个候选都判掉，按 lead 的指示**停在这里**：**未定因**，不为它开一次重建。

### 8.10 洞已堵：变异没应用时，用例不再给判定

改动（`outline-check.sh`）：

- `patch_in` 失败时**恢复源码、打印原因、`exit 3`**——不再继续跑到 build 和断言；
- 新增 `patch_in_self_test`：在临时文件上**故意用不存在的锚点**，断言三件事——返回非零、有 `PATTERN NOT FOUND`、文件字节未变；
- 自测在任何用例之前运行，**每次跑门禁都跑**。

人为打断的读数（standalone，10:47）：
```text
OK   patch_in stops the suite when an anchor no longer matches (rc=3, source untouched)
```

这条与 m5 / `stateBound` / `grep -m1` 同一个根：**以为自己固定住了自变量**。
这一支的变体是**以为自己应用了变异**，症状是"绿"，所以单独记一条。

### 7.1 m2 重跑：**两条读数都红了**（2026-10-23 10:37，`t52-outline18`）

m2 = 任务书点名的三条变异之一（"depth-tested-only outlines"）。上午那次 INVALID 是锚点对不上、变异没打上；
锚点改到 `boundDepthState` 之后重跑，一条 4 分钟的窗口：

```text
                              固定构型     m2 变异
stateBound（实际绑定的那个状态）   0            1        ← 红了
穿墙的队伍色像素 occluded_xray     68           0        ← 红了
armed                        requested=64229 frame_tick=64229
还原                         entity_overlay.cpp / main.cpp / native_renderer.cpp 三条 cmp 全 byte-identical
git status                   0 行
```

两条读数独立：一条来自**绑定的深度状态**（变异把 x-ray 列表绑到 depth-tested 状态，`stateBound` 0→1），
一条来自**像素**（穿墙的队伍色像素 68→0）。像素那条也顺带证明：`occluded_xray=68` 在固定构型上是**穿墙**读到的，
变异之后一个都没有——这正是"仅深度测试的描边"该有的红。

**上一次报的 `stateBound=0 (fixed=0)` 是我自己的读数错误**：脚本用 `grep -oE "T52_TICK:-[0-9]+"` 取 tick，
从 `T52_TICK` 这个变量名里抓到了 `52`，于是抓的是 **tick 52** 的帧，不是 64229。
两次读数都来自同一帧所以"看起来一致"，实际那一帧没有任何轮廓。已改回硬编码 64229（与全ticket所有捕获一致），重跑后如上。

### 8.11 顶点级代理的读数：框与像素**不是同一个对象**（2026-10-23 10:48，`t52-outline20`）

一次**同场景配对**（on 紧接 off，同一二进制）：
```text
pair-on  capture: armed requested=64229 frame_tick=64229 frames=4734
pair-off capture: armed requested=64229 frame_tick=64229 frames=4731
差异像素 484 / 860784 = 0.06%   ⇒ 前置断言通过（同场景）
```

顶点级读数（每帧只算一个 item，12000 个顶点全部投影）：
```text
entity=2  box=25x8  vertexbox=485,337,499,341  vverts=12000  origin=497,339  w=0.11
```
**顶点级框 ⊂ 公布的 AABB 框**（15x5 落在 25x8 内）⇒ AABB 是顶点的**超集**，这条上 AABB 推导没有偏小。

差异像素聚类（8 邻接）→ 4 块：
```text
comp 225 px  bbox=(453,325)-(474,338)   落在任何框内:   0/225   ← 这一块根本没有框
comp 128 px  bbox=(502,333)-(519,342)   落在任何框内:  32/128
comp  76 px  bbox=(557,335)-(569,341)   落在任何框内:   8/76
comp  55 px  bbox=(480,336)-(488,342)   落在任何框内:  20/55   其中 20 个落在 entity 2 的顶点框内
```

**判读（按事先写死的表）：**

- 顶点框与 AABB 框互相一致（⊂），**不是**"像素在顶点框内、在 AABB 外"那一行 ⇒ **不是 AABB 计算 bug**；
- 顶点框也**没有**把 blob 包住（entity 2 的顶点框只覆盖其中 20 个像素，blob 有 55 个）⇒ **也不是"姿态/变换"能解释的形状**；
- 真正的形状是：**4 块画出来的东西，14 个公布的框，只有 60/484 落在框内，且有一块（225 px，最大）一个框都没有**。
  框的中心都在各自 origin 附近（1-5 px），**框和 blob 各自都自洽，但它们不是同一个对象**。

⇒ 这不是姿态误差，是**框与绘制对象之间的对应关系**错了（CPU 记录 `overlayInstances_` 与 GPU 实例缓冲的 index 对应）。
姿态一致性因此仍是 **NOT VERIFIED**：代理从 AABB 换成顶点级之后，暴露的是**对应关系**问题，而对应关系正是"prove pose"必须先成立的东西。

**下一步（下一个窗口，一次构建即可）：** 在 `range.instanceStart` 与 `overlayInstances_` 之间加一条断言——
把 GPU 实例缓冲里的 `entityIndex` 读回（或把 CPU 记录的 entityIndex 同时写进 GPU 实例的 padding），
比较"画在第 i 个实例位置上的实体"与"第 i 条 CPU 记录的实体"是否相同。相同 ⇒ 对应关系没问题，回到几何；不同 ⇒ 定因。

## 9. D1：overlay 绘制对象与 CPU 记录的实体不对应（**一条未归因的红，尚未定性**）

### 9.1 定级

```text
D1  一条未归因的红：区域外 297 像素（同场景配对，0.06% 帧面积）
    状态：未归因。不是「已确认的产品缺陷」，也不是「通过」。
          已经测到的：overlay 画出来的对象与它自己记录的实体对不上。
          还没测到的：「是哪一侧错」。
    定性规则（用户 2026-10-23 定）：先归因、后定性。归因表只允许三种结论：
      (a) 质心在框内、描边在框外 -> 绘制矩阵/投影错（产品侧）
      (b) 质心本身在框外         -> 包围盒推导错（门禁侧）
      (c) 两者都在框内而判据有别 -> 测量方法错（我们错）
    影响：pose consistency = NOT VERIFIED（阻塞项就是 D1）
          正向门禁里那条 1 FAIL 就是它的像素面，登记在本条，不挂在六项验收上
```

### 9.1a 归因结果（2026-10-23 14:0x 补，**改了本节标题里的假设**）

readback 读数把本节标题那句话打掉了：**75292 个实例、0 个实体不匹配**，框与描边指向同一批实体。
归因表（第 16 节）给出的是：14 个发布框挤在一个 110x12 px 的区域里，画出来的墨迹是它们连成的 4 块，
框外那 297 个像素是 1-6 px 的窄边（76% <= 2 px）。

```text
结论：case (c) 测量方法错（我们错）——判据把「发布框」当成了「绘制范围」，而框比实际绘制的像素紧一圈。
      outsideDiff=297 仍然红、仍然记在本条名下；它是未归因红的归因已完成，不是产品画错对象。
      下一轮第一个测试：框来自网格静态 AABB 的投影（14 条里 13 条 vertexbox=0,0..0,0），
      而画的是当前姿态的蒙皮网格 + 0.004 世界单位的外扩。
```

### 9.2 读数（2026-10-23 10:48，同场景配对，`t52-outline20`）

前置断言先过：
```text
pair-on  capture: armed requested=64229 frame_tick=64229 frames=4734
pair-off capture: armed requested=64229 frame_tick=64229 frames=4731
差异 484 / 860784 = 0.06%   ⇒ 同一个场景（判据见 8.5）
```
差异像素按 8 邻接聚类 → **4 块**；公布的框 **14 个**：
```text
comp 225 px  bbox=(453,325)-(474,338)   落在任何框内:   0/225   ← 最大那块一个框都没有
comp 128 px  bbox=(502,333)-(519,342)   落在任何框内:  32/128
comp  76 px  bbox=(557,335)-(569,341)   落在任何框内:   8/76
comp  55 px  bbox=(480,336)-(488,342)   落在任何框内:  20/55
合计                                        60 / 484
```
顶点级对照（每帧一个 item，12000 顶点全部投影）：
```text
entity=2  box=25x8  vertexbox=485,337,499,341  vverts=12000  origin=497,339  w=0.11
```

### 9.3 为什么不是前两种解释

| 解释 | 判据 | 结论 |
|---|---|---|
| 框算小了（AABB 推导 bug） | 顶点框 ⊂ 公布的 AABB（15x5 在 25x8 内）⇒ AABB 是顶点**超集** | **排除** |
| 姿态/变换错 | 顶点框也只覆盖相邻 blob 的 20/55 像素，画出来的东西不在顶点框里 | **排除** |
| **对应关系错** | 4 块画的东西 / 14 个框 / 60-of-484 落在框内 / 最大一块无框；框中心都在各自 origin 附近（1-5 px），blob 自身也自洽 | **仅剩此解释** |

### 9.4 两个候选（未定因，需读回才能分）

```text
(a) GPU 实例缓冲未按帧清零  -> 幽灵轮廓（上一帧/上一批的实例还在缓冲里被画出来）
(b) CPU 列表与实例缓冲 index 错位 -> 画在第 i 位的实体 ≠ 第 i 条 CPU 记录的实体
```

### 9.5 给下一个人的第一行：读回测试（这是修复的第一步，不是验证）

```text
1. 把 entityIndex 同时写进 GPU 实例的 padding（EntityModelInstanceGpu 现有 padding[0] 被 boneCount 用，
   用 padding[1] 或新增字段），着色器不必读它，只为读回。
2. 一帧画完后，把实例缓冲读回（staging buffer / Map READ），逐位比较：
     读回第 i 个实例的 entityIndex  ==  overlayInstances_[range.instanceStart + i].entityIndex ?
3. 相同 -> (a) 幽灵轮廓：去查实例缓冲的写入/清零与 instanceCount；
   不同 -> (b) index 错位：去查 buckets 分组后 packed 的顺序与 overlayInstances_ 的 push 顺序。
```
**为什么必须先读回：** 两个候选的修法完全不同，而现在没有任何一条读数能区分它们；
先改代码等于在两个假设里挑一个改，改对了也不知道，改错了会同时破坏 8 条变异的锚点。

### 9.6 修 D1 的代价（已评估，本轮不做）

- D1 的修复要动**绘制路径**；8 条变异（m1 m3 m4 m6 m7 m8 m9 与 m2）**全部锚在那条路径上**，修完必须全量重跑；
- 一次全量变异 ≈ 45 分钟独占锁；
- 收益要等读回测试之后才明确。
⇒ **本轮冻结带着 D1**，进错题库，作为后续任务的第一件事。

## 14. 合并回归的三条定因（2026-10-23 12:xx，T52 门禁在合并树上 12 条 FAIL）

合并树 `3e4211f`（工作树 HEAD `ecb3a93`）上本门禁红 12 条，本分支报告记 1 条（`outsideDiff=297`）。
十二条分成三组，**没有一条是合并引入的行为缺陷**；其中两组是门禁自己的过期常数，一组是 D1 的像素面（未归因，见第 9 节）。

### 14.1 A 组六条：门禁的采样点不在录像的保留窗口里（**默认值过期**）

```text
FAIL probe demo exit = 1                     FAIL sampled player rows = 0
FAIL red player rows = 0                     FAIL blue player rows = 0
FAIL sampled ticks that produced player rows = 0
FAIL per-row lines carrying identity, team and pose = 0
```
**在分支上不带任何环境变量复现同一现象**（这是定性的关键读数）：
```text
分支 --ticks 1000,1200,...,2000（旧默认）  demo summary ticks=6 rows=0 unavailable=6  exit=1   ← 与合并树一致
分支 --ticks 64229,64267,...（窗口内）      demo summary ticks=6 rows=120 withTeam=120 unavailable=0  exit=0
```
⇒ **合并缺陷排除**：旧默认 `TICKS="${T52_TICKS:-1000,...,2000}"` 与这条录像的保留窗口
（`retainedFirst=74481 retainedLast=74671 serverTickOrigin=10252` ⇒ playback **64229..64419**）各说各话。
门禁此前每一条读数都是显式传 tick 跑出来的，**默认路径从没被走过**，所以这个红藏在门禁里。

**已改**（本分支）：采样点从探针自己打印的窗口**推导**，不写死；推导、窗口、步长、六个 tick 全部打印进日志；
`T52_TICKS` 保留为"钉死某一轮的诊断口"；捕获 tick 同样改为窗口内第一个 tick（原来是 `2000`，同一类过期常数，
它把帧拍到了录像开头、玩家近 3-5 倍，直接决定了 14.3 的像素数）。

两条判读同时保留：`unavailable=0`（历史答不出的 tick 数）与 `ticksWithRows==请求数`（答得出且有玩家的 tick 数）。

**推导实测**（分支，未占锁，只跑探针）：
```text
--ticks 0                    rc=1   （"没测到东西"裁决，发现运行按设计就是这个码）
  stderr: FAIL no player was sampled at any requested tick
          ← 用固定字符串断言，因为"exit 1"也可能是打印完就崩
  window_usable: history 三字段齐全 -> yes
  window: server 74481..74671 origin=10252 -> playback 64229..64419 step=31
  ticks=64229,64260,64291,64322,64353,64384
  自测：喂一行没有 retainedFirst= 的 history -> window_usable 判 no（SELFTEST-OK）
```
发现运行与测量运行打印的 `demo history` 行必须逐字节相同（cmp）——这条证明"窗口是同一扇窗"，
而不是两条相似但不同的代码路径。
一条说"我没取到答案"，一条说"我取到答案的数量"，删一条就是把一种失败换成另一种看不见。

### 14.2 B 组三条：绝对序号不是契约（**快照被当成了契约**）

```text
合并树: on.ordinal=7   vm.ordinal=7  vm.vmOrdinal=8  vm.orderOk=1
分支:   on.ordinal=6   vm.ordinal=6  vm.vmOrdinal=7  vm.orderOk=1
```
**`orderOk` 在两棵树上都是 1**——相对契约（描边在清深度的 pass 之前）没有坏；红的是三个绝对值。
T53 的弹道 pass 合并进了 outline pass 之前，整体 +1，**行为没变**。

**已改**：三件事同时断言，而不是把 6 改成 7——
```text
1 相对   orderOk=1 且 overlay ordinal < vmOrdinal（产品自比 + 门禁自比，门禁不只是复述产品）
2 自洽   ordinal 取自 owner=outline 且 drew=1 的那一行；同一构建两次捕获（on / vm）必须同号
3 可动   m4 移动 pass 之后 ordinal 必须 != 本构建的序号（must_move，期望值改为运行时读取）
```
**绝对序号 6/7 曾经是契约，2026-10-23 起不再是；当前注册表快照是 7/8。**
快照记成读数、不记成契约：下一次有人插 pass，红的是"快照变了"而不是"行为错了"。

**问：如果有人把 outline pass 挪到 ViewModel 之后，这套新断言会不会红？靠哪条红？**
会，两条独立地红：`orderOk`（产品自己的比较，m4 就是这个移动，实测 `orderOk=0`）
和 `assert_lt`（门禁自己比 `ordinal < vmOrdinal`，移动后是 8 > 7）。两条不共用同一来源，所以一条坏了另一条还在。

### 14.3 C 组一条：`outsideDiff` 6141 是 D1 换了镜头，不是新 pass（**不是独立缺陷**）

合并树 on/off 纯离线差分（不占锁、不开产品）：
```text
differing: 7644 / 860784 = 0.89%      regions: 7      outside every region: 7322
框外聚类 = 4 块大实心（恰好等于 silhouettes=4）:
  2745 px bbox=(765,334)-(853,382) 89x49 fill=0.63
  2178 px bbox=(605,333)-(679,375) 75x43 fill=0.68
  1577 px bbox=(351,350)-(414,382) 64x33 fill=0.75
   820 px bbox=(673,326)-(726,347) 54x22 fill=0.69
```
fill 0.63-0.75 是**人体形状**；轨迹线应当是 fill≈0.02-0.05 的细长条。而且：
```text
grep -c -i trajectory on.trace = 0
--projectile-trajectory 是显式开启的（默认关），--no-projectile-trajectory 才关
```
⇒ **不是 T53 的 pass，也不是 HUD/相机**。这是 **D1（框与画不对应）**：这一帧玩家近 3-5 倍，
blob 从 22x14 长到 89x49，"框外像素"因此从 297 涨到 6141；放大机制就是 14.1 的捕获 tick 过期。

**结论写成一句可复用的话：**
> `6141 = D1 × 近景放大 + A 的采样点`——**这条红从来不是独立缺陷，它一直是 D1 的另一个镜头。**
> A 修好后它回到 297，而 297 就是 D1 的当前读数：有 owner、有形状、有下一步（报告第 9 节）。

### 14.4 LOCK-DISCIPLINE：一轮被丢弃的 forward

```text
LOCK-DISCIPLINE: run t52-outline22 discarded, lock broken by a third party at 12:26
                 (hostlock.sh break has no ownership check: t53-merged released a lock it did not hold);
                 rerun under lock at 12:36:23 (t52-outline23), 68 OK / 1 FAIL, see 14.5
```

**这一行不是追责，是让下一个读的人知道为什么有一轮不算数。** 事实与影响范围：

```text
12:19:24  ACQUIRED owner=t52-outline22
12:19:25  探针段（1-3 段）：probe-window / probe-demo / selftest      ← 锁内，且不启产品
12:21:53  off.bmp  off.trace                                          ← 锁内
12:23:48  on.bmp   on.trace                                           ← 锁内
12:25:44  tested.bmp                                                  ← 锁内，12:26 之前的最后一帧
12:26     hostlock.sh break 把锁从我手里释放（非持有者可释放）        ← 此后 5-8 分钟无锁
12:29:54  speed1.trace                                                ← 无锁窗口内
12:31:55  regions.txt                                                 ← 无锁窗口内
12:36     我发现进程处于无锁状态，自行终止                            ← 这一轮作废
```

**判断**：`outsideDiff=297` 的**两个源帧写于 12:21:53 与 12:23:48，都在锁被破坏之前**，
而且同场景前置断言给出 484 像素——与 10:45 那次干净配对的数字**完全相同**。
但按纪律：**跨过 12:26 的一轮整体判为待验证，不作为读数**；`297` 作为"待重跑确认"记录，
最终数字以持锁重跑的读数为准。

### 14.5 持锁重跑的读数（**这是有效读数**：`t52-outline23`，12:36:23 取锁 → 12:59:59 结束）

```text
OUTLINE-CHECK=FAIL       68 OK / 1 FAIL
FAIL differing pixels outside every drawn region = 297, expected 0     ← 唯一一条，即 D1
OK   differing pixels INSIDE the drawn regions = 187 (>= 1)
```

**A 的读数**
```text
OK   window discovery reported a usable history line
OK   window discovery exit is the probe measured-nothing verdict = 1
OK   window self-test: a line without retainedFirst= is rejected
OK   the discovery run and the measuring run report the same history line = same
     window: server 74481..74671 origin=10252 -> playback 64229..64419 step=31
     ticks=64229,64260,64291,64322,64353,64384
     capture tick=64229 (first in-window playback tick; T52_TICK pins it)
demo summary ticks=6 rows=120 distinctEntities=20 withTeam=120 red=36 blue=36 withPose=120
             exact=0 checkpointFallback=6 unavailable=0 ticksWithRows=6 rowsMissingTeam=0 teamMappingMismatches=0
OK   sampled ticks the entity history could answer = 0
OK   sampled ticks that produced player rows (own reading) = 6
```

**B 的读数**
```text
on.ordinal=6
OK   overlay took a pass slot = 6 (>= 1)
OK   the ordinal is read from the overlay pass's own line = outline
OK   the pass that took the slot is the pass that drew = 1
vm.ordinal=6 vm.vmOrdinal=7 vm.orderOk=1 vm.drew=1 fixed.ordinal=6
OK   overlay ordinal is before the viewModel slot (gate's own comparison)
OK   overlay ordinal is the same slot in both captures of one build
```
注意：**分支上序号仍是 6**，所以本轮没有"因为改期望值而变绿"的东西——变的只有判据。
它在合并树（序号 7/8）上应当同样绿，在"pass 被挪到 ViewModel 之后"时两条独立地红（`orderOk` 与 `assert_lt`）。

**另外顺带清掉的一条**：`speed=1` 捕获 `rc=0 took=711s`，与 `speed=8` 帧**逐字节相同**（cmp）——
上一轮那条 `rc=127` 已确认是我 kill 进程造成的假红，不是缺陷。

## 15. 第 5 段：深度图从"但愿它出图"变成"不成立就红"

### 15.1 那条静默降级是怎么发生的

--world-depth 那次运行和别的捕获共用 RUN_TIMEOUT（600 s），而它要跑到 playback tick 64229。
一旦这次采集没有到达目标 tick，这一段的实际形态是：

```text
worlddepth.bmp 不存在  ->  --occluded 得到空区域  ->  [ -z "$OCCLUDED" ] 分支
                       ->  "NOT VERIFIED: the captured tick has no occluded player pixel"
                       ->  m2 走到 "NOT VERIFIED: no occluded region, so m2 has nothing to prove"
```

三行输出看上去都很有分寸，但它们说的都不是真的：**这一帧根本没被采集到**。
m2 不是"没得证明"，是"证明的舞台没搭起来"。这是六个形状里的第 1 个
（absent-read-as-benign）落在第 5 段上。

### 15.2 现在必须先过的四道门（按会失败的速度排）

派生 occluded 区域**之前**，outline-regions.py --depth-stats 给出四个读数，门禁逐条断言。
**本轮实测读数（2026-10-23 14:0x，tick 64229）**：

```text
world-depth run rc=0
depth image: bytes=2582406 nonGreyPct=0.00 nonzeroPct=16.71 uniqueSampled=4
OK   world-depth run rc = 0
OK   the depth capture reached the requested tick (64229)
OK   depth image bytes written = 2582406 (>= 100000)
OK   the depth image is a scalar image (0% non-grey)
OK   the depth image has geometry in it (16.71% of pixels are not black)
OK   the depth image carries more than one sampled depth value (4)
```

第 2 条是点名要求的那条，它读 capture: armed requested=... frame_tick=...，把"没到"和
"到了但没看见人"分开；失败时打印的就是那句：

```text
FAIL capture never reached tick N (last scene tick M; capture armed frame_tick=..., run rc=...)
```

第 3 条 nonGreyPct 的理由：世界像素着色器把 position.z/position.w 写进 R/G/B 三个通道，
所以真深度图**按构造是灰的**（实测 0.00% 非灰）；同一场景的彩色帧是 16.76% 非灰。
差一个数量级，替代品藏不住。

### 15.3 出图之后 m2 才允许有结论，而且只允许三种

```text
DEPTH_OK=1 且区域非空  ->  must_move "m2" ...（原有读数）
DEPTH_OK=1 且区域为空  ->  M2-UNDECIDABLE depth=valid region=empty
                          + 打出 trace 里的 outline: item 行作证（相机确实没看着两个人）
DEPTH_OK=0            ->  M2-UNDECIDABLE depth=unusable
                          + 明说"这个发现是上面那条红，不是这行"
```

第四种情形（DEPTH_OK=0 却落 NOT VERIFIED）现在不可能发生：深度证据不成立时第 5 段
自己就是红的，m2 没有资格再谈"没得证明"。

本轮第 5 段的实际走向是第一条：

```text
occluded region: 455,348,471,351
    occluded_xray=68 occluded_tested=0 occluded_tested_diff=0 region=17x4
OK   team-coloured pixels drawn THROUGH the wall (x-ray) = 68 (>= 8)
OK   team-coloured pixels through the wall when depth-tested = 0
OK   depth-tested frame vs overlay-off frame, in the occluded region = 0
```

### 15.4 变异 m10：把世界帧当成深度图喂进去

故意不重建：这条要证明的是**门禁的准入规则**有作用面，不是产品。

```text
cp on.bmp m10-depth.bmp;  depth-stats 读它
m10  stats: bytes=2582406 nonGreyPct=16.76 nonzeroPct=16.77 uniqueSampled=26
真深度图  : bytes=2582406 nonGreyPct=0.00  nonzeroPct=16.71 uniqueSampled=4
must_move "m10" "non-grey percent ... (section 5 requires 0)" 0 16.76  ->  红
```

**一条诚实的负面读数**：同一 tick 上，用彩色帧推出的 occluded 矩形与真深度推出的是
**同一个**（455,348,471,351）。所以"比较区域"这个断言在这张图上抓不到替代；
抓到它的是准入规则本身（0% 非灰）。这句话写进 m10 的注释，免得下一个人
以为区域比较也在守这道门。

m10 属于**门禁变异**（不重建、不启动产品）；m2/m11 属于需要重建的产品变异，
本轮 forward 没跑变异套件，所以那两条**本轮未验证**，不写 PASS。

## 16. D1 归因：从"297 个像素对不上"走到"是哪一侧错"

### 16.1 原来的假设，和它需要的读数

已有：14 个发布框、4 个色块、484 个差异像素里 187 个落在框内。
这只能说明"画的位置和发布的位置不是一回事"，**它甚至不能排除"画的是别的实体"**。
像素读数在结构上做不到这件事：一个像素不知道自己是哪个实体的。

### 16.2 让 GPU 说出它画的是谁（readback）

实例结构第 128 字节处本来就有 padding[1]（与骨计数 padding[0] 并列），
**不改 layout、不改 stride、四条 static_assert 一条没动**：

```cpp
gpu.padding[1] = item.entityIndex;      // 随实例一起上传
```

上传后一次 staging 拷贝，把**绘制调用真正消费的那个 buffer** 读回来，逐条打印
（渲染器叫不到 traceFmt——它在 main.cpp 的匿名命名空间里；所以读回表挂在
Renderer 上，由 main.cpp 打印）：

```text
overlay: readback i=<槽位> entityIndex=<GPU 里实际的> expected=<CPU 发布的> match=<0/1>
```

一个必须记下来的坑：CopyResource 要求源与目标**同尺寸**，而实例 buffer 按 capacity 分配。
用 packed.size() 建 staging 会让拷贝静默失败、读回为空——"测量没发生"伪装成"零个不匹配"。
代码按 overlayInstanceCapacity_ 建 staging。

**读数（2026-10-23 14:0x，正向门禁，tick 64229）**：

```text
readback: drawn instances=75292 identity mismatches=0
OK   drawn instances read back from the GPU buffer = 75292 (>= 1)
OK   each drawn instance carries the entity the CPU published for its slot = 0
```

**这条读数先把原假设打掉了**：框与描边指向同一批实体，不存在"画的是别的实体"。

### 16.3 归因表：发布框 vs 最近色块质心

```text
attr i=0  entity=2  box=506,340,513,342 centroid=510.2,342.2 blobPx=128 inside=0 dist=1.4
attr i=1  entity=3  box=504,343,509,345 centroid=510.2,342.2 blobPx=128 inside=0 dist=4.1
attr i=2  entity=4  box=506,341,515,342 centroid=510.2,342.2 blobPx=128 inside=0 dist=0.7
attr i=3  entity=5  box=559,341,565,343 centroid=562.9,342.0 blobPx=76  inside=1 dist=0.9
attr i=4  entity=6  box=455,348,471,351 centroid=462.8,348.8 blobPx=225 inside=1 dist=0.7
attr i=5  entity=7  box=505,341,512,343 centroid=510.2,342.2 blobPx=128 inside=1 dist=1.7
attr i=6  entity=8  box=481,340,487,342 centroid=484.3,341.0 blobPx=55  inside=1 dist=0.3
attr i=7  entity=9  box=559,341,565,343 centroid=562.9,342.0 blobPx=76  inside=1 dist=0.9
attr i=8  entity=10 box=456,348,470,350 centroid=462.8,348.8 blobPx=225 inside=1 dist=0.3
attr i=9  entity=11 box=506,340,514,342 centroid=510.2,342.2 blobPx=128 inside=0 dist=1.2
attr i=10 entity=13 box=509,341,517,343 centroid=510.2,342.2 blobPx=128 inside=1 dist=2.8
attr i=11 entity=15 box=456,348,470,350 centroid=462.8,348.8 blobPx=225 inside=1 dist=0.3
attr i=12 entity=16 box=558,342,565,344 centroid=562.9,342.0 blobPx=76  inside=1 dist=1.7
attr i=13 entity=17 box=562,341,568,342 centroid=562.9,342.0 blobPx=76  inside=0 dist=2.1
attribution verdict: 5 of 14 published boxes do NOT contain their nearest blob centroid
```

### 16.4 第三条读数：框外像素到最近框的距离

这张表只能说明"质心离框多远"，说明不了"框外那 297 个像素是什么"。补一条：

```text
differing 484 | inside 187 | outside 297
距离分布（切比雪夫，到最近发布框）: {1:137, 2:90, 3:32, 4:25, 5:12, 6:1}   最远 6 px
```

### 16.5 归因结论（按用户 2026-10-23 定的三条）

```text
(a) 质心在框内、描边在框外 -> 绘制矩阵/投影错      不成立：readback 0/75292 不匹配，
                                                   且框外像素是 1-6 px 的窄边，不是位移
(b) 质心本身在框外         -> 包围盒推导错         部分：5/14 质心在框外，但只差 0.7-4.1 px
(c) 两者都在框内而判据有别 -> 测量方法错（我们错） **成立：判据把"发布框"当成了"绘制范围"**
```

三条一起读，结论只有一句：**D1 不是"画错了对象"，而是"发布框比实际画出的像素小一圈"，
那一圈是 1-6 px（76% ≤2 px）、沿每个框的边界均匀分布的窄边。**
框是产品自己发布的（pass 的投影），它是 2-4 px 高的极小框；14 个玩家的框全部挤在
一个 110x12 px 的屏幕区域里，4 个色块是**多个相邻描边连成的一整块**——所以"4 个色块
对 14 个框"从一开始就不是"4 个对象对 14 个对象"，而是"4 块墨迹对 14 个框"。

机制上的候选（**下一轮的第一个测试**，不是结论）：

```text
1. 框 = 网格静态 AABB 的投影；画的 = 当前姿态的蒙皮网格 -> 姿态变形会跑到框外
   证据：14 条里 13 条的 vertexbox 是 0,0..0,0（CPU 侧没有投影顶点），框来自 AABB
2. 描边按 kOverlaySilhouetteExpand=0.004 沿法线外扩（entity_overlay.h:134），
   投影在框之后发生 -> 框天然不含这一圈
   ~~0.004 世界单位在这个距离上远小于 6 px，所以它最多解释 1 px~~
   **这一条当时算错了，见 §22.3 与 §24：0.004 加在世界位置上是每个 item 0.4–1.6 px，
   而它本来是模型空间的常数。改到模型空间后每侧 0.0002 px，outsideDiff 373 -> 0。**
```

**因此 outsideDiff=297 仍然是红的、仍然记在 D1 名下，但它现在有了归因**：判据（框）比产品
实际绘制范围紧，修法在测量侧（用产品实际绘制的范围，或让产品发布已外扩/已考虑姿态的框），
不是"产品把对象画错了地方"。定性到此为止：**case (c)，我们错**。

### 16.6 上一轮写下的 case (c)，被本轮读数否掉了（2026-10-23 15:0x）

上一轮的结论是"判据把发布框当成绘制范围，框比画出来的紧一圈"（case (c)，我们错）。
本轮把尺子换成"这一次 draw 的顶点"之后（第 18 节），outsideDiff **没有归零，而是 297 -> 373**：

<<<F>>>text
diff_outside=373 diff_inside=111 regions=14      VERDICT FAIL
<<<F>>>

同一轮里的像素证据说明了为什么：

<<<F>>>text
发布框（14 条）：x 505..516, y 340..351   宽 2..10 px、高 1..3 px
team 色墨迹：415 px，落在 453..567 x 325..342
同帧差异像素里另有 69 px 不是 team 色（on=(184,184,184) / off=(0,0,0)）
<<<F>>>

- 框与墨迹在 x 上重合，在 y 上**框比墨迹低 3-14 px**；
- 框量到的那块几何只有 1-3 px 高，画出来的是 10-14 px 高的站立人形；
- 两条尺子（AABB 与顶点）投影的是**同一块短网格**，所以换尺子不会改变结论。

结论：**病根在"框的世界->屏幕推导"（case b），不在"拿哪个来源当区域"（case c）。**
上一轮的 (c) 是个合理假设，被一次对撞实验否掉了——这正是"先归因、后定性"的价值：
它把下一轮的工作从"改测量口径"改成了"查这块比人形矮 4-5 倍的几何是从哪来的"。

下一轮第一个测试（一句话）：同一个 item，把 CPU 侧 rows*vertex 的世界坐标与 GPU 实例缓冲里
的 rows 逐元素比对，并打印该 draw range 的 vertexBuffer 与 instance.vertexPositions 是否来自
同一个 mesh 缓存；任何一处不同，4-5 倍的差异就有解释。


## 17. 顺序：第 5 段是整合前置条件，D1 归因不是

2026-10-23 13:2x 用户定：**等 T52 第 5 段完成，再一次性整合 T52 -> T53 -> T54**。
理由：我还会动共享渲染代码，现在合进去等于合两遍。

```text
第 5 段（深度感知）做完并跑出读数   -> 整合的前置条件，优先级高于 D1 归因
D1 归因                             -> 继续做，但不阻塞整合
分支状态                            -> 代码在 task/t52-outline、git status 干净、
                                       门禁自测能跑，就够资格进整合
```

整合之后（**现在不做，等用户/lead 点名**）：在集成树上重跑本线的 forward + mutation，
并证明新门禁、新断言、变异锚点**都来自集成树本身**，不是从本 worktree 借的脚本。
对 T53 是同样的要求。

这条顺序也改了 D1 的措辞：它是**一条未归因的红**，不是"已确认的产品缺陷"。
归因表（第 16 节）把手指向哪一侧，才配叫那一侧的缺陷——先归因，后定性。

## 18. 修尺子：区域改由"这次 draw 的顶点"推出——以及它测出的新读数

### 18.1 动作：不动那个 0，动"区域从哪来"

```text
assert_eq "differing pixels outside every drawn region" ... "0"     <- 一个字没动
region 的来历换了：同一个 mvp、同一个实例矩阵、同一次 draw 的顶点
```

以前是投影实体的**世界 AABB 八个角**（采样器给的实体外壳）；现在是把这个实例**自己的顶点**
（instance.vertexPositions，就是那次 draw 的顶点缓冲里的顶点）用**同一个实例矩阵**变换、用
**同一个 mvp** 投影，取屏幕包围盒。开关 --outline-vertex-extent 默认关（每帧每个描边都要过一遍
顶点，正常运行不需要），门禁在它测的所有捕获里都带。蒙皮实例被排除：CPU 手上是 bind 姿态的顶点，
投影它就是给一个没人画的姿态发框；它们落 extent=2，门禁读成"这个区域不是绘制范围"。

门禁新增一条，把尺子本身变成读数：
```text
region source: items=14 extent1=14 extent0=0 extent2=0 extentMissing=0
OK   items whose region is the drawn extent = 14
```
把 --outline-vertex-extent 从启动参数里去掉，这条立刻红——尺子退回旧规则不会再无声发生。

### 18.2 读数：**373**（上一轮 AABB 尺子是 297）

```text
diff_outside=373 diff_inside=111 regions=14      VERDICT FAIL
OK   precondition: the two runs saw the same scene (484 differing pixels < 2% of the frame)
FAIL differing pixels outside every drawn region = 373, expected 0
OK   differing pixels INSIDE the drawn regions = 111 (>= 1)
```

按你定的规矩：**它没有变成 0，那这个数就是一条新的真实读数，不抹掉。** 而且它比 297 更大，
说明"区域该从 AABB 换成顶点"这个方向**本身就不是病根**。同一轮里 attribution 与像素证据一起
说明了原因：

```text
发布框（14 条）：x 459..566, y 340..351 —— 宽 2..10 px、高 1..3 px
画出来的 team 色像素：415 个，落在 453..567 x 325..342
同帧差异像素里另有 69 个**不是 team 色**（on=(184,184,184) / off=(0,0,0)）
```

即：**框和墨迹在 x 上重合，在 y 上框比墨迹低 3-14 px**；框量到的那块几何只有 1-3 px 高，
而画出来的剪影是 10-14 px 高的站立人形。两条尺子（AABB 与顶点）投影的是**同一块短网格**，
所以换尺子当然没用——**错的是这块网格/它的变换，不是"用 AABB 还是用顶点"**。

这是本轮最有价值的一条：**尺子换到了对的位置，读数立刻把上一轮的归因否掉**。上一轮写的是
case (c)「框比绘制范围紧一圈，是测量方法错」；现在看是 **case (b)「框的世界→屏幕推导错」**，
框来自一块比人形矮 4-5 倍的几何。归因到此为止，定性下轮做。

### 18.3 位移变异 m12：只平移画的东西，不动框

```cpp
overlayConstants.mvp = frameConstants.mvp;
/* m12: 只平移 GPU 拿去画的矩阵；发布框仍用未平移的 mvpForOverlay */
for (int shiftRow = 0; shiftRow < 4; ++shiftRow) {
  overlayConstants.mvp.m[shiftRow][0] += 0.005f * frameConstants.mvp.m[shiftRow][3];
}
```

0.005 NDC x (1264/2) = 3.16 px。矩阵与框一起平移会让断言保持绿，那正是"软尺子"的样子；
所以变异只动常量缓冲。读数见 19 节（--mutation 同一轮）。
## 19. 变异套件读数（2026-10-23 15:0x，与 forward 同一次持锁运行）

```text
m1  RED      色块落到错误队伍的 region = 61 (fixed 0)
m2  INVALID  变异后的源码不编译 -> 这条什么都没证明（见 19.1）
m3  RED      --outline off 帧里的 team 色像素 = 415 (fixed 0)
m4  RED      通过顺序 overlay before viewModel = 0 (fixed 1)；ordinal 7 (fixed 6)
m5  GREEN    亮度写状态 1 -> 1，变异没有移动读数（见 19.2）
m6  RED      去掉 hold 后 speed=1 与 speed=8 采到的不是同一帧
m7  RED      x2：无 class 兜底时 submitted=0 (fixed 14)、noMesh=20 (fixed 6)
m8  RED      丢 upload key 后真正画出的剪影 = 0 (fixed 5)
m9  RED      bind 优先于 world 时 probe 自检失败 = 5 (fixed 0)
m10 RED      彩色帧当深度图的非灰比例 = 16.76 (fixed 0)
m11 RED      读回表里实体不匹配 = 127484 (fixed 0)
m12 RED      画的东西平移 3 px 后框外像素 = 488 (fixed 373)

MUTATION-SUITE=FAIL  (mutations that did not compile: 1)
```

九条红是九条有面的读数；两条不红/无效，都是本轮的真读数，不是"跑过了"。

### 19.1 m2 为什么无效：变异文本吞掉了分号

旧锚点：`? overlayXRayDepthState_.Get() : overlayDepthTestedState_.Get();`
替换：  `? overlayDepthTestedState_.Get() : overlayDepthTestedState_.Get()`  <- 少了结尾的 `;`
于是变异体是 `...Get()` 后面直接跟下一行语句，MSVC 在 5496 报 C2013。
**这比"没跑"更值得记**：它说明 m2 在过去某一版锚点改写后就再也没编译过，
而套件当时仍报了一个 GREEN——一个从未被真正变异的构建。已补回分号（本条已修，下轮复核）。

### 19.2 m5 为什么 GREEN：读数说 1，而代码创建的是 ZERO

```text
code : overlayDescription.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;   (1327，两个状态共用)
       overlayXRayDepthWriteEnabled_ 默认 false                        (native_renderer.h:1239)
trace: outline: draw ... depthTest=0 depthWrite=1 ... stateTest=0 stateWrite=1
```
两者互相矛盾：创建的是 ZERO，读回来的成员却是 1，所以变异体（改成 ALL）当然读 1。
这不是"变异没咬人"，是**读数没有接到那个状态上**——正是我们要找的第六种坏形状
（mechanism not wired to verdict）。下一个测试：在那条 draw 行里同时打印
`overlayDepthStencilState->GetDesc()` 的 DepthWriteMask 原始值，看 1 是从哪一路来的。
本轮不改这个机制：改了就没有读数可依据。


## 20. m5：读数接到设备上了，而且它接上之后说的话和原来不一样

### 20.1 先量，再定

在创建两个深度模板状态之后，把**送给 CreateDepthStencilState 的描述**与 **GetDesc 读回的**
两套值都发布，再加一个 `agree`（两套值必须一致，否则这条读数不是从设备来的）。smoke 读数：

```text
stateTest=0 stateWrite=0 stateBound=0 stateEnable=0 stateMask=1 stateCreated=0 agree=1
```

三件事一次说清：

- `stateCreated=0` / `stateMask=1`：我们要 `WRITE_MASK_ZERO`，设备回读 `WRITE_MASK_ALL`；
- 原因不是产品写错状态，而是**深度被关掉时 mask 没有意义**，运行时把规范化成 ALL；
- 旧读数 `depthWrite=1` 是"把 mask 当效果"的产物：断言名字说 overlay 不写深度，
  读数却来自一个与写深度无关的字段——**就是你说的"名字说着一件事、读数来自另一件事"**。

### 20.2 修法：发布效果，并让两套值互相制约

```cpp
// 效果，而不是 mask：DepthEnable=FALSE 的状态什么也不写，不管 mask 是什么
overlayXRayDepthWriteEnabled_ = enabled && mask != D3D11_DEPTH_WRITE_MASK_ZERO;
// 一致性只在有意义的地方比：enable 永远比，mask 只在深度打开时比
overlayStateReadbackAgrees_ = createdEnable == readEnable
  && (readEnable == 0 || createdMask == readMask);
```

门禁新增三条断言（forward 段）：

```text
OK  depth-stencil: enable=0 mask=1 created=0 agree=1 depthWrite=0 (enable&&mask = 0)
OK  the depth-stencil readback agrees with the description it was created from = 1
OK  the x-ray state does not write depth = 0
OK  the published depthWrite is the effect of the state (enable AND mask) = 0
```

### 20.3 m5 变异体也跟着改对了

旧变异只把 mask 改成 ALL，而深度是关的——**它改变不了任何效果**，所以无论读数接没接线都不该动。
现在变异把 enable 和 mask 一起改（一个真正会写深度的状态）：

```bash
patch_in "$MUT_RENDERER" \
"    overlayDescription.DepthEnable = FALSE;
    overlayDescription.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;" \
"    overlayDescription.DepthEnable = TRUE;   /* mutation m5 */
    overlayDescription.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;"
```

固定构建读 0、变异体读 1，这条断言才是第一次真的在测东西。判读见第 23 节。

## 21. m2 的历史：分号是哪一版吞掉的，"曾经 GREEN"能不能复原

### 21.1 溯源（git，可复核）

```text
git show 6c0b0c3:outline-check.sh   (2026-10-22 21:26)  m2 替换文本末尾：..., 0);
    -> 替换整段（含结尾 ", 0);"），变异体能编译
git show 4e795b4:outline-check.sh   (2026-10-23 10:33)  m2 替换文本末尾：...Get()
    -> 锚点含结尾分号、替换不含，变异体从此编译不过
```

4e795b4 的标题恰好是 "an unapplied mutation can no longer wear a pass"：
它修的是"锚点没匹配上"，同一次编辑里把"编译不过"这条弄坏了。

### 21.2 结论

`rebuild_product` 的 MUTATION-INVALID 计数**早于** 4e795b4（`6c0b0c3` 里已经是
`mut_invalid=$((mut_invalid + 1))` + `mut_fail=1`），所以 10:33 之后任何一次套件运行都会把 m2
报成 INVALID 而不是 GREEN；这个窗口里只有今天 14:26 那一次，它报的正是 INVALID。早于 10:33 的
判读（例如 `24b0067` 记的"m2 turns red on both readings"）来自 `6c0b0c3` 版本的锚点，那个锚点
能编译（上面的 git show 原文可查）。

**本分支历史上不存在"未编译成功的变异体被报成 GREEN"。** 但有两个过程漏洞，都已堵上：

```text
1. 4e795b4 之后 m2 没跑起来过（编译失败），而"这个 case 没有判读"只在末尾汇总里体现；
2. 更早版本里 rebuild 失败会跳过 case 主体（if rebuild_product m2; then ... fi），
   连 INVALID 那行都打不出来 —— 判读静默消失。
```

### 21.3 元自检 m-meta（每次跑套件都跑）

往渲染源里追加 `#error`，要求：构建失败、被分类成 MUTATION-INVALID、mut_invalid 递增（即让套件
最终失败）、源码字节级还原并重建成功。这条自身不计入失败（是预期的失败），但把 21.2 的漏洞 2
永久堵住。判读见第 23 节。

## 22. case (b)：两个怀疑都死了，第三个浮出来

你给的两个怀疑，各用一个读数打死：

### 22.1 "框取自碰撞几何而不是渲染网格" —— 否

```text
itemdiag ... box=10x3 ... bmin=-22.13,-0.03,-29.76 bmax=21.87,83.41,8.42 mh=83.45 wh=0.01
```

`mh=83.45` 就是 TF2 玩家模型的高度。**框量的是玩家渲染网格本身**，不是碰撞壳。

### 22.2 "draw 用的行与框用的行不同" —— 否

实例缓冲读回时顺手取出第一个实例 position 第 0 行，与 CPU 发布的行逐元素比：

```text
overlay: rowcheck gpu=0.000016,-0.000081,0.000009,0.745107
                  cpu=0.000016,-0.000081,0.000009,0.745107 worst=0.000000 match=1
```

同一个矩阵。框与像素指向同一处，残余不在变换上，也不在实体关联上（readback 0 处不一致）。

### 22.3 第三个：外扩常数用错了空间（本轮量到了每一像素）

采集帧的 14 个 item 逐条算（`box` 是公布的框，`wh` 是它的世界高度）：

```text
box    wh      world/px   外扩 0.004 -> 每侧像素
10x4   0.0100  0.00250    1.60 px
10x3   0.0100  0.00333    1.20 px
 7x3   0.0100  0.00333    1.20 px
 7x1   0.0100  0.01000    0.40 px
 5x2   0.0100  0.00500    0.80 px   (8 个这种)
 3x2   0.0100  0.00500    0.80 px
```

即：**着色器把每个轮廓向外扩了 0.4–1.6 px**，而公布的框是**未外扩**的网格范围。
`kOverlaySilhouetteExpand = 0.004` 是**世界单位**（`entity_overlay.h:134`，加在 `mapped` 上，
`native_renderer.cpp` 的 `mapped += unitNormal * cameraPositionAndExpand.w * mode.x`），
而玩家的世界高度只有 0.0067——**外扩量是玩家身高的 60%**。同样的 0.004 若按模型空间理解，
相对 83.45 units 的身高是 0.005%，等于没有。

门禁现在把两个高度并排打印（本轮实读）：

```text
heights: published boxes hmin=1 hmax=4 | team ink red=3 px blue=4 px
```

**框 1–4 px 高，墨迹 3–4 px 高——同一个对象，不是"框只有墨迹的五分之一"。**
上一轮 §18.2 里"框 1–3 px、墨迹 10–14 px"的对比是拿**一整团连通墨迹的外接框**（多玩家连成一片）
去比**单个玩家的框**，那个五倍是我自己量错了。撤回。
算一遍面积：14 个玩家、平均周长约 26 px、每侧 0.8–1.6 px ⇒ 预期框外像素 ~300–430；
实测 `outsideDiff=373`。**同一个量级，且这一次它有了名字。**

结论（按三种定性）：**case (c)，量法问题**——墨迹是对的（它就是外扩后的壳），框也是对的
（它就是网格范围），判据把"壳"和"网格"当成同一个对象在比。产品侧另有一件事：外扩常数用错了
空间，60% 身高不是"细描边"，是"放大一倍的色块"。

下一轮一次跑死的实验（两者都要）：
```text
1. 产品发布 expandPx（= 0.004 / world-units-per-pixel，按 item），
   判据改成 outside <= 周长*expandPx（或把框按 expandPx 长大后再要求 outside=0）；
2. 把外扩改到模型空间做（position + normal*expand 之后再 rows），
   要求 outsideDiff 归零、且 m12（位移）与 m10（深度当颜色）仍然红。
```

### 22.4 我自己那条"8/14 质心在框外"的归因，是坏的尺子

`attribution` 拿**每个玩家的框**去比**每团墨迹的质心**，但 14 个玩家只画出 4 团（玩家在画面里
挨得很近，壳互相连成一片）。**一团多个玩家的质心本来就不必落在任何单个框里**，所以那条读数
不能支持 case (b)，本轮撤回它作为证据的地位（只留作参考）。
要按玩家归因，需要**逐实例着色**：给每个实例一个唯一颜色画一帧，才能把"这个玩家的墨迹"和
"这个玩家的框"一一对上。这是下一轮的第二个实验。

## 23. 本轮读数（2026-10-23 16:08–17:17，一次加锁的 forward + mutation）

### 23.1 forward：92 OK / 1 FAIL

```text
FAIL differing pixels outside every drawn region = 373, expected 0      <- 唯一一条红（未变）
OK   differing pixels INSIDE the drawn regions = 111 (>= 1)
OK   drawn instances read back from the GPU buffer = 77252 (>= 1)
     readback: drawn instances=77252 identity mismatches=0
     region source: items=14 extent1=14 extent0=0 extent2=0 extentMissing=0 hmin=1 hmax=4
     heights: published boxes hmin=1 hmax=4 | team ink red=3 px blue=4 px
     depth-stencil: enable=0 mask=1 created=0 agree=1 depthWrite=0 (enable&&mask = 0)
OK   the depth-stencil readback agrees with the description it was created from = 1
OK   the x-ray state does not write depth = 0
OK   the published depthWrite is the effect of the state (enable AND mask) = 0
     overlay: rowcheck gpu=0.000016,-0.000081,0.000009,0.745107
                      cpu=0.000016,-0.000081,0.000009,0.745107 worst=0.000000 match=1
OK   the instance row the draw consumes equals the one the box was built from = match=1
```

### 23.2 mutation：15 条判读全红，0 GREEN，1 INVALID（那条 INVALID 是自检故意的）

```text
OK   m-meta: a source that does not compile reads MUTATION-INVALID and fails the run
MUTATION-RED m1  team pixels in the WRONG region=61 (fixed=0)
MUTATION-RED m2  team-coloured pixels through the wall=0 (fixed=40)
MUTATION-RED m2  x-ray depth test state actually bound=1 (fixed=0)
MUTATION-RED m3  team-coloured pixels in the --outline off frame=415 (fixed=0)
MUTATION-RED m5  x-ray depth write state=1 (fixed=0)              <- 本轮主修项
MUTATION-RED m6  without the hold, speed=1 and speed=8 capture different frames
MUTATION-RED m7  silhouettes submitted without the class fallback=0 (fixed=14)
MUTATION-RED m7  players rejected as noMesh without the class fallback=20 (fixed=6)
MUTATION-RED m8  silhouettes DRAWN when the item loses the upload key=0 (fixed=5)
MUTATION-RED m9  probe self-test failures when the bind mesh wins=5 (fixed=0)
MUTATION-RED m4  pass order (overlay before viewModel)=0 (fixed=1)
MUTATION-RED m4  overlay ordinal after the move=7 (fixed=6)
MUTATION-RED m11 identity mismatches in the readback table=71694 (fixed=0)
MUTATION-RED m12 pixels outside every drawn region with the draw shifted 3 px=488 (fixed=373)
MUTATION-RED m10 non-grey percent of a colour frame offered as depth=16.76 (fixed=0)
MUTATION-SUITE=ALL-RED-RESTORED   GATE-RC=1（只因 23.1 那条红）
```

判读分三列写死：**RED** = 变异体读数与固定构建不同；**GREEN** = 没动（本轮 0 条）；
**INVALID** = 编译不过或锚点没匹配（本轮 1 条，是 m-meta 故意造的）。

### 23.3 本轮三条结案的判读

| 项 | 结论 | 依据 |
| --- | --- | --- |
| ① m5 | **结案：读数已接到设备，变异体红** | `stateCreated=0` vs `stateMask=1` 暴露了"mask 当效果"；改成发布 `enable && mask` 后固定读 0、变异读 1 |
| ② m2 | **结案：真实红，且不再能假绿** | 补回分号后编译通过，两条读数都红（40→0、0→1）；m-meta 每次运行都验证"不编译 ⇒ INVALID ⇒ 套件失败" |
| ③ case (b) | **归因完成：是 case (c) 量法 + 一个用错空间的外扩常数** | 网格高 83.45（是渲染网格不是碰撞壳）、draw 行与框行逐元素相同（match=1）、框高 1–4 px 与墨迹 3–4 px 同尺寸、373 是每侧 0.4–1.6 px 的外扩 |

### 23.4 仍然红的 373，现在的身份

它不是"投影错了"，也不是"实体错位"：框和墨迹是同一个对象，只是**框量未外扩的网格、墨迹是外扩后的壳**。
要它归零有两条路，都排在下一轮，且都必须让 m12/m10 继续红：

```text
1. 判据侧：产品按 item 发布 expandPx = 0.004 / world-units-per-pixel，断言改成 outside <= 周长*expandPx；
2. 产品侧：把外扩放回模型空间（或乘上 rows 的缩放量），让 0.004 重新成为"细描边"。
```

## 24. 外扩回到模型空间，容差改由产品发布（2026-10-23 17:2x）

### 24.1 产品侧：同一常数，换回它本来属于的空间

```hlsl
// before: 世界空间（这一张图里玩家只有 0.0067 世界单位高，0.004 是 60% 身高）
mapped += unitNormal * cameraPositionAndExpand.w * mode.x;
// after:  模型空间（83 units 的玩家，0.004 是 0.005% 身高 = 发丝）
local.xyz += unitNormal * cameraPositionAndExpand.w * mode.x;
mapped = float3(dot(input.row0, local), dot(input.row1, local), dot(input.row2, local));
```

常数一个字没改：`kOverlaySilhouetteExpand = 0.004`。改的是它加在 `rows` 之前还是之后。

### 24.2 判据侧：容差是产品按 item 发布的数，门禁不许自己发明

产品现在每个 item 发布 `expand=`，算法就是它自己实际加的那个量除以该 item 的 world-per-pixel：

```cpp
expandWorld = kOverlaySilhouetteExpand * |rows row0|;        // 模型外扩 x 模型->世界缩放
expandPixels = expandWorld / min(spanX/boxW, spanY/boxH);     // 该 item 的 world-per-pixel
```

门禁只做两件事：读这个数、把它按周长加成像素预算：

```bash
TOLERANCE=$("$PY" outline-regions.py --trace "$OUT/on.trace" --expand-tolerance)
#   tolerance = sum over items of 2*(boxW+boxH) * expand
assert_eq "items whose shell expansion the product published" "$TOL_MISSING" "0"
assert_eq "painted pixels outside every drawn region, within the product own tolerance" "$WITHIN" "1"
```

门禁里**没有** 0.004、没有 1.6 px、没有任何乘法常数。产品不发布（`missing>0`）就是失败：
**"没说话"不等于"容差为零"**。

### 24.3 镜像变异 m13：产品声明 0 而像素仍然外扩 → 必须红

```text
m13 同时做两件事：着色器改回世界空间（像素照旧外扩 ~1.6 px/侧），发布的 expandPixels 改成 0。
如果容差是门禁自己猜的，这个变异体读绿；容差绑在产品的声明上，它必须读红。
```

### 24.4 smoke 读数（长跑之前先看一眼，17:25–17:28，持锁）

```text
outline: item entity=16 ... extent=1 expand=0.000 aabb=558,342,565,344
tolerance=0.080 items=14 published=14 missing=0
noninterference: diff_outside=0 diff_inside=41 regions=14   VERDICT PASS
```

**373 → 0。** 一条链跑完：

```text
373（每侧 0.4-1.6 px 的外扩，面积 300-430，与实测同量级）
  -> 外扩从世界空间改到模型空间（常数不变）
  -> 每侧外扩 0.0002 px（expand=0.000）
  -> outsideDiff = 0，inside 41（原 111）
```

顺带被钉住：`missing=0`（14 个 item 全部发布）、`extent1=14`（区域仍是 draw 实际消费的顶点投影）。

### 24.5 关于 16:05 那次被我杀掉的运行（lead 要求写进报告）

16:05 那次排队已经进了第 3 段，smoke 先一步暴露 `agree=0`：读数与创建描述对不上，说明
**旧语义下这次运行无论跑多久都只会产出解释不了的数**。它被终止，16:08 用改好的语义重新排队。

规则写下来：**一次运行的读数如果在运行过程中被证明语义是错的，跑完它只会得到一堆需要重新解释
的数；宁可白扔一次排队（80 分钟），也不要拿它去凑读数。** 判据是"语义是否已被证明错"，
不是"跑得顺不顺"。

## 25. 本轮读数（2026-10-23 17:30–18:38，一次持锁的 forward + mutation）

### 25.1 forward：93 OK / 1 FAIL，那条红换了位置

```text
OK   painted pixels outside every drawn region, within the product own tolerance = 1
     diff_outside=0 diff_inside=41 regions=14            <- 上一轮 373 / 111
     tolerance: tolerance=0.080 items=14 published=14 missing=0  (outside=0)
     region source: items=14 extent1=14 extent0=0 extent2=0 extentMissing=0 hmin=1 hmax=4
     depth-stencil: enable=0 mask=1 created=0 agree=1 depthWrite=0
     heights: published boxes hmin=1 hmax=4 | team ink red=3 px blue=4 px
     readback: drawn instances=68194 identity mismatches=0
FAIL red pixels in the red player is region = 6, expected >= 8        <- 新暴露的一条
OK   blue pixels in the blue player is region = 17 (>= 8)
OK   blue pixels in the RED player is region = 0
OK   red pixels in the BLUE player is region = 0
```

### 25.2 373 -> 0 的完整链（lead 要求能读出来）

```text
373 = 每侧 0.4-1.6 px 的外扩（面积 300-430，与实测同量级）
    -> 两次假设被否：网格高 83.45（是渲染网格）、draw 行与框行逐元素相同（match=1）
    -> 定位：kOverlaySilhouetteExpand=0.004 加在世界位置，而玩家世界高 0.0067（60% 身高）
    -> 改到模型空间（常数不变）：每侧 0.0002 px，expand=0.000
    -> outsideDiff = 0，inside 41
```

### 25.3 容差：产品发布，门禁只读

```text
tolerance=0.080 items=14 published=14 missing=0
#   tolerance 由 --expand-tolerance 按 sum(2*(boxW+boxH)*expand) 算，expand 是产品逐 item 发布的
#   门禁里没有 0.004、没有 1.6、没有任何自造的乘法常数；missing>0（产品没发布）就是失败
```

### 25.4 mutation：16 条判读全红，0 GREEN，1 INVALID（m-meta 故意）

```text
OK   m-meta: a source that does not compile reads MUTATION-INVALID and fails the run
MUTATION-RED m1  team pixels in the WRONG region=23 (fixed=0)
MUTATION-RED m2  team-coloured pixels through the wall=0 (fixed=17)
MUTATION-RED m2  x-ray depth test state actually bound=1 (fixed=0)
MUTATION-RED m3  team-coloured pixels in the --outline off frame=33 (fixed=0)
MUTATION-RED m5  x-ray depth write state=1 (fixed=0)
MUTATION-RED m6  without the hold, speed=1 and speed=8 capture different frames
MUTATION-RED m7  silhouettes submitted without the class fallback=0 (fixed=14)
MUTATION-RED m7  players rejected as noMesh without the class fallback=20 (fixed=6)
MUTATION-RED m8  silhouettes DRAWN when the item loses the upload key=0 (fixed=5)
MUTATION-RED m9  probe self-test failures when the bind mesh wins=5 (fixed=0)
MUTATION-RED m4  pass order (overlay before viewModel)=0 (fixed=1)
MUTATION-RED m4  overlay ordinal after the move=7 (fixed=6)
MUTATION-RED m11 identity mismatches in the readback table=75782 (fixed=0)
MUTATION-RED m12 pixels outside every drawn region with the draw shifted 3 px=41 (fixed=0)
MUTATION-RED m13 pixels outside every drawn region when the product declares no expansion=488 (fixed=0)
MUTATION-RED m10 non-grey percent of a colour frame offered as depth=16.71 (fixed=0)
MUTATION-SUITE=ALL-RED-RESTORED   GATE-RC=1（只因 25.1 那条红）
```

m13 是这一轮新加的镜像变异，也是最关键的一条：`m13 outsideDiff=488 declared tolerance=0.000`。
产品声明"我不外扩"，而像素仍按世界空间外扩 —— 如果容差是门禁自己猜的，它读绿；它读红，
证明容差确实绑在产品的声明上。m12（draw 平移 3 px）同轮读 41（固定 0）——也继续红。

### 25.5 新的那条红：`red = 6, expected >= 8`，我没有动它

它不是我改坏的，是**口径被产品修正之后露出来的**：

```text
采集帧 14 个 item：red 6 / blue 6 / unknown 2（逐 item 数出来的）
red 的 6 个框都只有约 4x2 px，整块 red 区域 12x4，里面画了 6 个红像素 = 每名红队玩家 1 px
blue 的 6 个框里有一个 10x4（更近），整块 blue 区域画了 17 px
外扩修好之前：整帧墨迹 484 px；修好之后：41 px（14 名玩家，平均 3 px/人）
```

`>= 8` 这个下限是在**外扩把每个剪影撑成 2-5 倍的年代**选定的。外扩修正后，同样这批玩家在自己
区域里只画 1 px/人，于是 6 < 8。**这不是产品退化，是下限的口径跟在了一个已经被修掉的缺陷后面。**

按纪律，本轮**不改这个数**。下一轮的建议（需要 lead 点头才动手）：把下限从产品自己的 item 数
推导出来——同一区域里每名被画的玩家至少 1 px（`red_pixels >= red_items`，本轮 6 >= 6），
而不是写死 8。这样它测的是"每名被画的玩家都留下了颜色"，而不是"当年那个胖剪影的面积"。

### 25.6 顺带记下的两组对照

```text
外扩修正前后的同一批读数（同一 tick、同一相机）：
  整帧差异像素 484 -> 41；team 像素 415 -> 33（m3 变异体读数）
  m12 变异体 outsideDiff 488 -> 41（平移 3 px 仍然立刻出框）
  m2 变异体 through-the-wall 40 -> 17（对照值变小，仍然 0 < 17 而判红）
```

这些数一起变小是同一个原因：剪影从"撑大的壳"回到了"网格本身的轮廓"。红/绿判读没有一条因此变绿。

## 26. 外扩搬到裁剪空间：像素才是像素（2026-10-23 18:5x）

### 26.1 为什么"常数放哪个空间"这个问题本身没有解

参考相机上，一个站立的玩家屏幕高度**约 4 px**（世界高 0.0067、网格高 83.45，模型->世界 ~8e-5）。
于是"每侧 1-2 px 的细描边"换算成世界/模型单位就是：

```text
世界空间 0.004  ->  每侧 0.4-1.6 px   墨迹 484 px（胖色块，373 px 出框）
模型空间 0.004  ->  每侧 0.0002 px    墨迹  41 px（几乎看不见）
想要 2 px/侧   ->  世界空间 0.0033 ≈ 身高 50%（又是胖色块）
```

**结论：在这个距离上"1-2 px 的描边"在世界/模型空间里根本不是一个固定量——它是身高的 25-50%。**
上一轮"改到模型空间"只是把 373 换成了 41，两边都不对。lead 的判断是对的：反推世界常数是错的方向。

### 26.2 做法：顶点投影之后，在 NDC 里按像素偏移

```hlsl
float4 clip = mul(float4(mapped, 1.0), mvp);
float3 clipNormal = float3(dot(rotated, mvp[0].xyz), dot(rotated, mvp[1].xyz), dot(rotated, mvp[2].xyz));
float2 normalPixels = clipNormal.xy;
float normalLength = length(normalPixels);
normalPixels = (normalLength > 1e-6) ? normalPixels / normalLength : float2(0.0, 0.0);
clip.xy += normalPixels * float2(mode.y, mode.z) * clip.w * cameraPositionAndExpand.w * mode.x;
```

`mode.yz` 是 CPU 每帧算好的 `2/width, 2/height`（NDC 里一个像素的尺寸），
`cameraPositionAndExpand.w` 现在是**像素数**（1.5），`mode.x` 仍是"这条路径要不要外扩"（线/标记为 0）。
方向取屏幕空间法线并归一化，所以壳的厚度与表面角度无关，不会被拉斜。

### 26.3 常数为什么是 1.5

```cpp
// 参考相机上一个玩家约 4 px 高。1.5 px/侧的壳 -> 剪影从 4 px 长到约 7 px，
// 一眼能看见是"描边"；4 px/侧会到 12 px，那时它读起来是"光晕/放大"而不是边。
constexpr float kOverlaySilhouetteExpandPixels = 1.5f;
```

它不再需要按距离换算（这正是像素单位的全部好处），所以注释写的是"为什么落在这个带里"，
而不是"怎么从别的常数推出来"——推不出，也不需要推。

### 26.4 带断言进 forward（不进变异）

```text
expand band: items=14 published=14 missing=0 below=0 above=0 min=1.500 max=1.500 median=1.500
OK   silhouettes whose shell the product did not publish = 0
OK   silhouettes with a shell of zero pixels (the outline is not drawn) = 0
OK   silhouettes with a shell above the outline band (> 4 px) = 0
```

上一轮缺的就是这条：壳是"身高 60%"的时候，门禁里没有一句话说"这已经不是描边了"。
带读的是产品自己发布的数，所以它同时守住常数本身（谁把常数改成 9，这里立刻红）。

### 26.5 容差变成精确量，`outside` 从 0 变成一个被解释的数

```text
tolerance=360.000 items=14 published=14 missing=0        （= sum 2*(w+h)*1.5）
diff_outside=48 diff_inside=105 regions=14               （smoke 读数）

outside=48 <= tolerance=360  ->  门禁接受；48 就是这一圈 1.5 px 的壳真正落在框外的部分。
```

`--expand-tolerance` 的每个乘数现在都是产品发布的像素数，不再有"世界单位除以世界每像素"的估算，
所以这个界是**精确的**而不是近似。为此 `outline-frame-check.py` 新增 `max_outside=`，让工具的 VERDICT
和门禁的断言用同一条规则——日志里出现"绿断言配 VERDICT FAIL"是这一项目付过代价的错误形状。

### 26.6 `>= 8` 那条红，没有改阈值就自己绿了（lead 预测正确）

```text
red_inside=20 blue_inside=35 red_wrong_team=0 blue_wrong_team=0   VERDICT PASS
（上一轮 0.0002 px/侧时是 red=6，触发了 >= 8；现在 20 与 35）
```

### 26.7 变异 m13 重写、m14 新增

- **m13 重写**：原来那条"改回世界空间 + 发布 0"依赖的着色器行已经不存在（裁剪空间改写把它们删了）。
  现在 m13 = **画的壳是声明的 10 倍**（发布仍是 1.5，着色器按 15 px 偏移）：带照旧绿，
  而 outside/tolerance 必须红——它证明"声明与画面对不上"能被抓住。
- **m14 新增**：**声明 0、画面照旧 1.5 px**。像素一个都不动，变的是门禁还接不接受它们：
  tolerance 变 0，于是同样那 48 个框外像素从"允许"变成"拒绝"。这是容差绑在声明上的正面证明。

## 27. 集成树上的验收运行（2026-10-23 19:46–20:57，一次持锁，forward + 全部变异）

### 27.1 这次运行跑在哪

```text
树     D:/TF2_Native_Test  HEAD 42024c6（d3523d8 T52 v1 / 508dc0e T53 / 0981b39 T54 之上）
门禁   outline-check.sh 来自本分支 1c58081（md5 cce823fcbcad），不是 worktree 里的临时版本
产品   sha256=0850467bafc63f6d… mtime 19:36（lead 全量构建的产物，我没有重建）
只写   evidence/outline/**（产物）；源码零改动。跑完 git status 里 evidence/ 之外的 6 条未跟踪项
       与跑之前完全一致（那是 lead 的 evidence/release-gates、evidence/trajectory）

这就是用户点名的验收条件：门禁、断言、锚点必须来自集成树。
```

### 27.2 forward：97 OK / 0 FAIL，GATE-RC=0

```text
ordinal: absolute numbers are a registry snapshot, recorded not asserted (fixed=7 vmOrdinal=8)
OK   overlay took a pass slot = 7 (>= 1)
OK   viewModel took a pass slot = 8 (>= 1)
OK   pass order (overlay before viewModel, with a viewmodel) = 1
OK   overlay ordinal is before the viewModel slot (gate own comparison) = 7 (< 8)

world models: prepared=59 uploaded=59          <- lead 让我先放着的那条，集成树上本来就是 59/59
tolerance: tolerance=360.000 items=14 published=14 missing=0
expand band: items=14 published=14 missing=0 below=0 above=0 min=1.500 max=1.500 median=1.500
diff_outside=48 diff_inside=105 regions=14
OK   precondition: the two runs saw the same scene (153 differing pixels < 2% of the frame)
readback: drawn instances=63938 identity mismatches=0
region source: items=14 extent1=14 extent0=0 extent2=0 extentMissing=0 hmin=1 hmax=4
heights: published boxes hmin=1 hmax=4 | team ink red=3 px blue=4 px
red_inside=20 blue_inside=35 red_wrong_team=0 blue_wrong_team=0     <- 不再触发 >= 8
负对照 exchanged-colour frame: red_inside=0 blue_inside=0 red_wrong_team=256 blue_wrong_team=256  FAIL
```

三条待定点全部落地：

1. **`WORLD_MODELS_UPLOADED = 59`** —— 集成树上就是 59/59，**没有红**。所以不需要改成"off 与 on 相等"的关系断言：
   这个数在两棵树上都是 59，它测的是"覆层没有多传模型"，不是树间差异。留原样，记录在案。
2. **`NI_LIMIT`** —— 集成树帧尺寸实测 **1264×681**（从 `evidence/outline/on.bmp` 头读的），与写死的算式一致，不动。
   trace 里没有 `client=` 字段，所以"打印 client 尺寸"这一步改成读 BMP 头，结论相同。
3. **`outside` 与 `tolerance`** —— `outside=48 <= tolerance=360`，判据是产品自己声明的 1.5 px/侧。牙口见 27.3 的 m13/m14。

### 27.3 mutation：18 条判读全红，0 GREEN，1 INVALID（m-meta 故意）

```text
OK            m-meta: a source that does not compile reads MUTATION-INVALID and fails the run
MUTATION-RED  m1   team pixels in the WRONG region=55 (fixed=0)
MUTATION-RED  m2   team-coloured pixels through the wall=0 (fixed=35)
MUTATION-RED  m2   x-ray depth test state actually bound=1 (fixed=0)
MUTATION-RED  m3   team-coloured pixels in the --outline off frame=121 (fixed=0)
MUTATION-RED  m5   x-ray depth write state=1 (fixed=0)
MUTATION-RED  m6   without the hold, speed=1 and speed=8 capture different frames
MUTATION-RED  m7   silhouettes submitted without the class fallback=0 (fixed=14)
MUTATION-RED  m7   players rejected as noMesh without the class fallback=20 (fixed=6)
MUTATION-RED  m8   silhouettes DRAWN when the item loses the upload key=0 (fixed=5)
MUTATION-RED  m9   probe self-test failures when the bind mesh wins=5 (fixed=0)
MUTATION-RED  m4   pass order (overlay before viewModel)=0 (fixed=1)
MUTATION-RED  m4   overlay ordinal after the move=8 (fixed=7)
MUTATION-RED  m11  identity mismatches in the readback table=69454 (fixed=0)
MUTATION-RED  m12  pixels outside every drawn region with the draw shifted 3 px=153 (fixed=48)
MUTATION-RED  m13  pixels outside every drawn region when the product declares no expansion=3358 (fixed=48)
MUTATION-RED  m14  silhouettes the band refuses because the product declared zero=14 (fixed=0)
MUTATION-RED  m14  out-of-region pixels the gate still accepts once the shell is declared zero=0 (fixed=1)
MUTATION-RED  m10  non-grey percent of a colour frame offered as depth=16.72 (fixed=0)
MUTATION-SUITE=ALL-RED-RESTORED
```

### 27.4 三件只有在这棵树上才能证明的事

**(a) 序号没有任何绝对依赖。** m4 的读数在集成树上自动变成 `8 (fixed=7)`，在我 worktree 上是 `7 (fixed=6)`。
同一个变异、同一份门禁脚本、两棵树两对数字——期望值是从运行时读的，不是写死的。
forward 里那条 `ordinal: ... (fixed=7 vmOrdinal=8)` 是**记录**，`assert_lt` 才是断言，两者分工清楚。

**(b) m13 把"声明与画面对不上"抓成 3358。** 它发布 1.5 px、按 15 px 画：带断言照旧绿（声明是 1.5），
outside 从 48 涨到 3358，容差 360 —— 判据红。上一轮同一位置读到 488（世界空间那一版），量级差约 7 倍，
与 15/1.5 的比例关系一致。

**(c) m14 的红是从"接受"变成"拒绝"，像素一个都没动。** 它只把发布的 `expandPixels` 改 0：
同样那 48 个框外像素，fixed 构建 within=1（接受），变异体 within=0（拒绝）。
**这是"容差绑在产品声明上"最直接的证据**：改的不是画面，是声明。

### 27.5 `>= 8` 那条红：集成树上也是绿的，阈值一个字没改

```text
red_inside=20 blue_inside=35        （上一轮 0.0002 px/侧时是 red=6，触发 >= 8）
负对照（两队颜色交换）：red_inside=0 blue_inside=0 red_wrong_team=256 blue_wrong_team=256  FAIL
```

负对照值得单独记：它证明 `red_inside=20` 不是"凡是红像素都算"，而是"红像素出现在红队自己的区域里"。

## 8. 交接

### 8.0 冻结状态（2026-10-23 10:55，本轮结束）

```text
T52 验收（队伍色 / 遮挡 / 开关 / 未知队伍回退 / 实体消失 / 无玩家夹具）  := PASS（8 红变异覆盖）
pose consistency                                                        := NOT VERIFIED，阻塞项 = D1（见第 9 节）
正向门禁                                                                := 67 OK / 1 FAIL
                                                                           唯一 FAIL 即 D1 的像素面，登记在 D1，不挂六项验收
变异                                                                    := 8 RED（m1 m2 m3 m4 m6 m7 m8 m9）+ m5 INVALID（读数点不亮：运行时归一化）
cap A/B                                                                 := OK（cap 64: prepared=9 uploaded=5 rejected=4 / 0x8007000E；cap 96: rejected=0、uploaded==prepared=9）
patch_in                                                                := 自测 rc=3、源码未动；锚点失效即中止（exit 3）
还原                                                                    := 27 + 3 条 cmp 全 byte-identical，git status 0 行
盘上的 exe                                                              := 2026-10-23 10:45 由干净源码重建（不是变异体）
```

**三件留给下一个人的东西：**
1. **`SUITE-ARTIFACT-IS-MUTANT`** —— 每轮变异结束自己打印"盘上是替身，先重建"；
2. **顶点级仪表** —— `cpuPositions` + 实例 `rows` + 每帧一个 item 全顶点投影（`vverts`/`vertexbox`），量过真东西的尺子，不删；
3. **同场景配对前置断言** —— on/off 紧邻两帧，差异超过帧面积 2% 即判定"两次运行没看到同一个场景"，并声明该次运行证明不了任何东西。

**下一个人从这里开始：** 第 9.5 节的读回测试（`entityIndex` 进 padding → 读回 → 逐位比较）。
那是 D1 修复的第一步，也是本轮唯一没有做完的定因。：唯一那条红 + 下个窗口三步

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