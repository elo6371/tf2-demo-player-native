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

### 4.2 `depthWrite=1` **已定案**：是运行时归一化，不是产品写深度

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
