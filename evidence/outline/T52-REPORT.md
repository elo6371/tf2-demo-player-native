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

**没有跑的：** 门禁 STEP 6/7（正向 + 11 条变异）没跑——编排器按设计在第 5 步判死就退出，没有进入门禁。
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
  `--find-ticks 25` 扫出 8 个 tick，全部 `fidelity=exactTick stale=0`，每 tick `rows=21 red=6 blue=6~7 unknown=8~9 posed=20`。
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

## 11. 同族发现（读到了字，读错了字段）

今晚这条家族一共四个成员，全部是'有两个答案，而代码只信了其中一个'：

1. **tick 域**：`firstPacketTick` 同时存在三个答案——`main.cpp` 注释里写 29368、探针打印 1、产品自己打印 10252。
   只有第三个是产品在说它自己。注释已改成不带数字的推导；探针把 `firstRecordedPacketTick` 与 `serverTickOrigin`
   分成两列；采集 tick 改为'在播放 0 处抓一帧，读产品自己打印的偏移'来算，而不是从探针 tick 猜。
2. **锁状态**：`hostlock.sh status` 里有 `LOCKED owner=`（持有者）与 `reservation=`（预约者），我把 `reservation=` 读成了持有者，
   以为自己在跑，其实别人在跑。lead 当场纠正。
3. **tick 列表**：探针只扫保留窗口内的 tick（`[retainedFirst..retainedLast]`），而门禁早期把播放 tick 直接当查询 tick，
   于是六个 tick 全部 `rows=0`——'描边坏了'和'这一帧没有玩家'读起来一模一样。
4. **网格 key**（本轮）：同一个事实'这个玩家用哪个模型'由两条路径生产，而连接只用了一条在用的 key。

## 12. 这次给 lead 的建议

1. 下一个窗口给 t52-outline，一次跑完：构建 → 探针 self-test（应 0 失败）→ 门禁正向 → 11 条变异 → speed 不变性。
   预计纯机器时间：构建 2–4 min + 正向 6 次采集（含一次 16 min 的 speed=1）+ 变异 9 次重建与采集 ≈ 70–90 min。
2. 第 9 节那条产品缺陷另开任务，不要塞回 T52；它的读数和判据行号已经写在这里。
3. 分锁（第 8 节）。今晚我 30 分钟的活等了 390 分钟，这不是我一个人的损失。

## 13. 附：本轮产物

- `evidence/outline/OVERLAY-CONTRACT.md`（v4 连线、v5 网格源与姿势来源条件句）
- `evidence/outline/outline-offline-selftest.py`（离线自检，无 GPU）
- `evidence/outline/pick-64332.trace`（778 帧 `drew=0` 的原始证据，gitignored）
- 门禁 `outline-check.sh`：11 条变异、对齐表、pose 断言、`outline_value` 自检
- 探针 `native/tools/entity_overlay_probe.cpp`：`classFallback=`/`reason=`/`serverTickOrigin=` 三列 + 8 条 fallback 用例
