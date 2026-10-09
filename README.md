# TF2_Native_Test

P0（SourceTV PacketEntities 状态重建）与 P1 第一项（实体模型引用接线 + 逐 tick 位移）
的隔离测试树。

P1 共八个提交：模型引用接线（`8c6f06e`）、接线暴露出的 VPK 目录重复解析性能悬崖
（`4868e7b`）、补上该修复漏掉的链接依赖（`9316416`）、两次记录与判据加固
（`f38bcf6` / `a2c584f`），以及逐 tick 位移的 z 合并与确定性属性选择（`6cb0ecf`）、
普查判据改按增量判定（`cc17ebe`）、把逐值门禁接成验收链第 10 步（`dab6ce1`）。
逐 tick 位移那一条见 `ACCEPTANCE-P1-2026-10-06.md` §10。

2026-10-07 续接会话又落两个提交：`9842a9f`（文档）与 `aa93926`——
实体历史保留修复（按最大 tick 间隙抽稀 + 去掉重复检查点 + `resolvedTick` 读数），
把原 §3.1 阻塞项闭合，验收链扩到 11 步（新增历史覆盖门禁）。见 HANDOFF §1.1 / §3.1。

2026-10-07 第二轮：**`cd36db1`（当前代码基线）** —— 武器世界模型接线。
武器的 `DT_BaseEntity.m_nModelIndex` 是第一人称手臂合成模型（`c_*_arms`），
世界渲染此前按它取路径；错得能解析成功（一双手臂是真实资产），
所以每一个计数门禁都绿着也看不见。修复后 POV `armsWeapon=0
worldModelRequests=69 known=71`、bagel `armsWeapon=0 worldModelRequests=32 known=33`，
oracle 同实体同包见证（实体 822 的 Enter 包 `Integer(363)` ↔ `c_rocketlauncher.mdl`）。
新增门禁为验收链第 12 步、变异用例 `m10`。见 `ACCEPTANCE-P1-2026-10-06.md` §11。

2026-10-07 第三轮：**`c0ff710`** —— 观察目标 `m_hObserverTarget`。
玩家的观看对象写在**观看者自己**的实体上（`m_iObserverMode` 槽 835 / `m_hObserverTarget`
槽 836，21 位 `CBaseHandle`），旧管线一处未读。本轮解出 mode、句柄、索引与序列号，
并与 oracle 逐值对照（33242 包 `mode=1 / 606217` ↔ `idx=9 / serial=296`）；
**但相机与听者刻意不跟随**：POV 实测 `dem_cmdinfo` 录下的相机贴在录像者自己的眼位
（俯仰逐位相同、位置差 0.012），而管线优先槽给目标解出的坐标离它 **2729 单位**、
该槽自己落后 **2379 tick**。交付 = 解析 + 仪表 + 把这笔账钉进验收链第 13 步。
见 `ACCEPTANCE-P1-2026-10-06.md` §12 与 HANDOFF §1.3 / §3.3.2。

2026-10-08 第四轮：**`66cd97f`** —— 实体属性选槽的新鲜度。
上一轮留下的「2729 单位」不是坐标算错，是**读错了槽**：玩家的 `m_vecOrigin` 在
`DT_TFLocalPlayerExclusive`（全精度）和 `DT_TFNonLocalPlayerExclusive`（量化）里各有一份，
而 rank 规则看不见**哪一份还在被写**。bagel 上 Local 就是真值（NonLocal 已陈旧 73129 tick），
POV 实体 3 上 Local **冻在 51596 后再未更新**、NonLocal 每检查点重写。
修法分两步提交：`d61b3d6` 先只加仪表（`EntityPropertyValue::lastWriteTick`，在
`readEntityPropUpdates` 唯一写入点盖章；零行为变化，`PROBE-OUTPUT-ADDITIVE=PASS` 9/9）；
`66cd97f` 再让 `preferCandidate` 先比 tick、**平局回落到原 rank＋字典序**。
读数：POV `chosenStale 3→0`、选中槽从 Local 变 NonLocal、`worstChosenAge 3798→124`；
**bagel 保持 0（对照组）**；第 13 步 `camera-to-target 2729→45`（逐轴 `-0.031,44.344,3.781`）。
新增门禁为验收链第 14 步、变异用例 `m11`。见 `ACCEPTANCE-P1-2026-10-06.md` §14。

2026-10-08 第五轮：**真实 SourceTV 覆盖**（整合审查「仍未解决」第 2 项）。
这项是个覆盖洞，不是读数错：钉住的 24 份校准语料（`evidence/corpus-calib/demos.txt`）
里**只有 1 份 SourceTV**（`73.dem`），所以它 1361392 个包里有 112175 个是 SourceTV，
其余 SourceTV 侧证据全靠 oracle 九份里的 bagel 与 snakewater。全量语料的头扫描
（1643 份 `.dem`）说本机**总共 9 份 SourceTV**，所以「扩大覆盖」是有界的：
把九份全解一遍（664 MB / 917543 包 / ~3.5 min），而不是花 4 小时普查 1634 份 POV
—— 那些和 oracle 里已有的五份 POV 走同一条代码路径。九份**全部 `entity_failures=0`**、
`malformed_packets=0`、`unknown_message_packets=0`、`delta_base_unavailable=0`；
每份还断言 `index_tail_bytes` **等于文件字节数**，否则「只读了一个包就报零失败」
也能打印同样的零。新增门禁为验收链第 15 步（47 条断言 + 两个变异，各 9 行红）。
见 `ACCEPTANCE-P0-2026-10-06.md` §0.8。

2026-10-08 第六轮：**45 单位的归因**（整合审查「仍未解决」第 1 项的前置，**无 `native/` 改动**）。
上一轮把第 13 步的 `camera-to-target` 从 2729 降到 45，但**没归因**。本轮把那 45 拆开：
用 `--camera-at` 逐 tick 采样发现**录制的相机原点在 demo 里是「保持」的** ——
demo tick 37640..37678 这 39 个 tick 的 origin 逐字节相同
（`-1112.031250,505.593719,459.031250`），而角度每 tick 都在变。**直接读原始
`democmdinfo` 字节**确认：37640/37650/37655/37677/37679 五个包的 `viewOrigin`
逐字节相同（前 16 字节 sha1 都是 `ef7a26d97762`）。该值等于**录像者自己**在
demo tick 37639 / server tick 53968 的 `DT_TFLocalPlayerExclusive.m_vecOrigin`（x,y）
与最后一次写下的 `m_vecOrigin[2]`（z）；而录像者**此刻已经死了**
（37633 起 `m_fFlags` 带 `FL_TRANSRAGDOLL`、37634 起 `m_flFallVelocity` 掉到 0）。
所以 **45 = 一台被冻结的死亡相机 − 同一具身体 12 tick 之后的位置**：44.28 在 y、3.78 在 z
是身体死后自己掉的，剩下每轴 ≤0.061 是 NonLocal 槽的 1/8 量化。
更有分量的是 **541 点普查**（server tick 1000..55000 步长 100）：`follows=1` 只在
**13 个不同检查点**出现，其中 **11 个**目标解出的坐标**就落在录像者自己身上（≤0.15 单位，
同一快照）** —— 即**这一份 demo 的观察对从不指向别的玩家**，它**无法**回答「跟随另一个玩家对不对」。
结论：整合审查「不接相机」**结论不变**，依据换成「这一份 demo 无法验证接线」。
判据 39 → **46 条断言**，`--mutation` 2 → **3 个扰动且要求恰好 3 条红行**。
见 `ACCEPTANCE-P1-2026-10-06.md` §15 与 HANDOFF §1.7。

2026-10-08 第七轮：**帧抓取**（整合审查「仍未解决」第 3–5 项的第一步，**回归结果仍是先补仪表**）。
这一步解决的是**结构性**缺口，不是漏了一条断言：第 1–15 步全是计数与解码值，
**没有一条能说画面对不对**；而「画面错」可以是「画面格式完好」——
上一轮武器画成手臂就是这样的缺陷，它**解析成功、绘制成功**，所有计数保持绿色。
本轮做**仪表，不下结论**：渲染器可按 `--capture-frame` 抓**一帧**
（可选 `--capture-tick N`），读回点在 `draw()` 内、`Present()` **之前**
（Present 之后 back buffer 内容未定义，这是唯一能读到「刚刚合成的这一帧」的时刻）。
请求**一次性消费**，抓完程序自己退出，门禁不必猜何时杀进程。
抓取失败走**独立退出码 16**，且**刻意不写 `lastError_`** —— 那个成员是主循环设备恢复路径的输入，
诊断失败不能装成设备失败。产物是**手写的未压缩 24 位 BMP**（不引编码器、不做色彩管理），
选它就是为了让门禁能直接哈希与度量。

新增门禁 `frame-capture-check.sh`（验收链第 16 步）断言四件事，正是后续任何
「画面变了」结论的前提：① 文件是 BMP 且**头部与自身字节数自洽**；
② 同一静态场景抓两次**逐字节相同**（确定性）；③ demo 场景与暂停场景
**逐字节不同**（抓的是真画面，不是恒定缓冲）；④ 非法 `--capture-tick` **退出 13 而不是静默抓第 0 帧**。
实测：暂停场景 sha256 `ab0bc11e…`（两次相同）、demo 场景 `fa1bb194…`（不同）、
`1264×681×24bpp`、`fileBytes 2582406 == 54 + 2582352`（磁盘实字节）。
**缺输入是 FAIL 不是 SKIP**。`--mutation` 翻转图像一个字节，要求内容哈希能察觉。

> 2026-10-08 第八轮已覆盖上文的一部分说法，留在这里以便对照：
> **① 第 16 步的「demo 场景」当时是假绿** —— 它唯一命名的 demo 的地图
> `koth_bagel_rc13` 在本机没有安装，抓到的是**回退全屏四边形贴的喷漆图**，
> 不是场景。第 16 步现在加了第二个（有安装地图的）demo 与
> 「色彩多样性 < 1000」判据，断言 16 → **18**。
> **② `native/` 又多了两个探针**（`resource_reachability_probe` / `vpk_query`），
> 所以 `exe_count` 的 pin 从 21 变成 **23**，链条 16 → **17 步**。
> 判断代码有无被动过请改用最新的基线标签，别照抄下面这条
> `git diff --stat 3d03719..HEAD -- native/`（它现在非空，是探针的加法）。
> 见 `ACCEPTANCE-P1-2026-10-06.md` §17。

⚠️ **从第七轮起「`native/` 一个字节未改」不再成立**：第七轮 `native/` 三文件 +156 行
（帧抓取，**全部加法**，渲染行为未变），第八轮又 +293 行（两个资源探针）。
**判断代码有无被动过请用当前基线**：`git diff --stat fe6cb3b..HEAD -- native/`（应为空，
`fe6cb3b` 是最后一次动 `native/` 的提交；更早的 `3d03719..HEAD` **现在非空**，是上面两次加法）。
另：门禁里**每条启动都写了 `--audio-device 6`** —— 本机默认设备不是该出声的那个，
且 `--start-paused` **不豁免**（音频设备在启动阶段就打开）。
见 `ACCEPTANCE-P1-2026-10-06.md` §16 与 HANDOFF §1.8。

**`VERIFY=PASS` 已在 `c84e32d` 上取得**（**17 步** `bash verify-all.sh`，**1h16m16s**，
RC=0，17 步全绿，读数在 `evidence/verify/1..17-*.txt`）。本轮相对上一次（`cfd6637`，
1h08m36s）**只改了第 17 步的探针**（加反事实读数）与门禁断言数（9 → 12），
17 步链在新基线上**重跑复核通过**。
逐步：`BUILD (errors=0 warnings=1 exe_count=23)` / `CENSUS 9/9` / `COVERAGE 9/9` /
`FIXTURE 58/58` / `ORACLE` / `ORACLE-CORPUS (pinned_sample=24 of 24 pinned) + selftest` /
`RECORDING-TYPES` / `PROBE-ADDITIVE` /
`MUTATION (red=40 hold=4)` / `CENSUS-NEGATIVE (pinned_reports=24)` /
`TRAJECTORY-ORACLE (compared=40)` / `HISTORY-COVERAGE (worst 1180 ≤ 2×761)` /
`WEAPON-WORLD-MODEL (fixture_ok=12, live_window_packets=68)` /
`OBSERVER-FOCUS (46 断言, camera-to-target=45u)` + 变异（3/3） /
`SLOT-FRESHNESS (24 断言, POV chosenStale=0 worstAge=124)` + 变异（2/2） /
`SOURCE-TV-COVERAGE (9 份 / 917543 包 / 47 断言)` + 变异（2/2） /
`FRAME-CAPTURE (18 断言)` + 变异（一字节改动移动内容哈希） /
**`RESOURCE-REACHABILITY (12 断言, cap 1024 → 193875/200000)` + 变异（错档案数变红）**。
**接材质的第一刀已量**（`c84e32d`）：把 `main.cpp:775` 的 512 世界图集上限抬到 1024，
`cp_snakewater_final1` 的三角形覆盖从 **30731/200000（15.4%）** 跳到
**193875/200000（96.9%）** —— **瓶颈就是 512 这个整数，不是接线**；
无上限只到 199227，剩下 773 个三角形引用的材质根本没解到像素（真实安装缺口）。
⚠️ **第 5 步的输入集已改成钉住的**（`cfd6637`）：它原本和已修过的第 9 步
**共用同一个 `sorted(glob())` 抽样活目录**的写法，语料 1656 → 1658 让八份抽样
整体移位 —— 但第 5 步只比较**计数**，所以**继续报绿**的同时产出了一份不可复现的证据。
详见 `ACCEPTANCE-P1-2026-10-06.md` §13.9。
上一次 **`VERIFY=PASS` 在 `cfd6637` 上取得**（17 步，1h08m36s，**17 步链第一次跑完**；
其前一次跑到 01:39 因机器 fork 耗尽死在第 8 步中间，非判据红）。
更早 **`VERIFY=PASS` 在 `e43a3f3` 上取得**（16 步，38m27s）。
上一次 **`VERIFY=PASS` 在 `290db52` 上取得**（15 步，36m20s）。
更早 **`VERIFY=PASS` 在 `18fe7ea` 上取得**（15 步，**33m09s**）。
上一轮 **`VERIFY=PASS` 在 `fefc216` 上取得**（14 步，30m35s，读数
`evidence/verify/1..14-*.txt`）。**第一次跑（`3ca75ae`）是 `VERIFY=FAIL`**：
唯一红的是第 8 步 —— 新增变异用例 `m11` 把 `red/hold` 从 `37/3` 抬到 `40/4`，
而第 8 步的期望是写死的；**这是验收机器按设计抓住了我自己的改动**，显式改计数后转绿。
更早一次 **`VERIFY=PASS` 在 `a1d5e8d` 上取得**（13 步，32m30s）。
而**更早的第一次跑（`c0ff710` + `0eca5ab`）是 `VERIFY=FAIL`**：红在第 9 步，
原因在验收机器而非被测代码 —— `--sample 24` 抽的是**活的** Steam 语料目录，
当晚新增两份 demo 让 24 个抽样落点整体移位，变异会全部空转（`native/` 一个字节没动）。
`a1d5e8d` 把输入集钉成 `evidence/corpus-calib/demos.txt` 后转绿。见 §13。
武器世界模型那轮基线 `cd36db1`（12 步，mutation red=37 hold=3）的通过记录保留作对照；
`aa93926`（11 步）与 `a2c584f`（9 步）两次运行同为对照。
重跑前请先确认 `git status --porcelain -- native/` 为空。

2026-10-09 第十轮：**主循环空转**（`5ae6c10` 代码 + 门禁，读数在 `79c18ee` 上）。
缺陷是一行：`const DWORD waitMs = static_cast<DWORD>(std::max(0.0, std::min(remainingMs, 1.0)));`
—— 等一个**还没到点**的帧时，等的是 1 ms 的切片而不是「到点为止」，每个切片都把整个循环体
对**没有变化的状态**重跑一遍。此前 17 步全部读的都是程序**产出**了什么（计数、解码值、
抓到的像素、解到的资源），**没有一步能看见循环在烧 CPU**，因为热循环产出的是同样的东西。
判据读的是**等待本身**（`mean_wait_ms`，修复后实测 8.97 ms、变异版 1.00 ms），
不是派生的速率：`main_loop_iterations / rendered_frames` 在**本机没有判别力** ——
帧目标是 120 Hz（8.33 ms）而循环体自己就要几毫秒，两个版本都落在 ~2 迭代/帧，
所以它只作为诊断打印、**刻意不断言**（是 `--mutation` 把这件事抓出来的）。
另外两条断言独立于等待路径：状态驱动的两个更新器在无变化时必须远低于循环计数、metrics 节流必须成立。
门禁 `idle-spin-check.sh`，**11 条断言**，`--mutation` 把那一行改回去并要求 `mean_wait_ms` 变红；
它需要**已提交的树**，因为还原机制是 `git checkout`。链扩到 **18 步**，
`VERIFY=PASS` 在 `79c18ee` 上取得（**1h19m40s**，证据 `6fe6cdc`）。

2026-10-09 第十一轮：**实体属性查找（P1 第二条的性能面）**，`665c0e9`（代码）+ `3a890ce`（门禁）。
`EntityModelResolver::buildInstances` 的**类回退扫描**原本对每个名字像玩家的实体都先
`extractTransform()`，再问它有没有可用的玩家职业；而 `extractTransform` 会按每个后缀把
整张属性表扫一遍，所以一个注定被丢弃的实体也要付全部代价。改法是**先问便宜的问题**：
一次 `findProperty(state, "m_iClass")`（后缀与 1..9 取值区间都和 `extractTransform` 一致），
通过的才付 transform。**实例集合必须一点不动**——省的是工，不是结果。
读数：夹具 `fixtureTransforms 7→6`、`fixturePropertyComparisons 24→19`、
`fixtureClassLookups 0→1`，而 `fixtureInstanceCount` **6→6**。
新增门禁 `entity-property-lookup-check.sh` 为验收链**第 19 步**（10 条断言 + 变异，
变异删掉前置判断后要求夹具读回 7/24/0）。

⚠️ **这一处在真实 demo 上的收益是 0，而且是被判据钉住的 0**：九份 demo 里每个玩家实体
**都已经带渲染请求**，扫描全部被 `covered` 判断挡在门口，`resolverFallbackScanned=0`。
第 19 步因此把「demo 上必须读到 0」也写成断言 —— 哪天某份 demo 开始走到这条路径，
是这个文件先说话，而不是收益悄悄变了。所以这轮的正确表述是
**「在夹具上省了恰好一次完整 transform 和五次属性比较」**，不是「更快了」。

2026-10-09 第十二轮：**验收流程分级**（本文件新增「分级验收」一节）。
见下节。

- 源目录 `D:\TF2_Demo_Player` **本次未改动**（`work/native-mvp-source` 仍是
  `d3e2b7c`，`git status` 干净；该提交由**另一路 AI 会话**写入，只改 `native/docs/`
  3 个 markdown，**一个代码文件都没动** —— 合并补丁对它也干净，见 `MERGE-MANIFEST` §6.1）。
- 本目录由 `git archive d585af8 native | tar -x` 建立，独立 git 仓库，
  分支 `p0-entity-protocol`。

## 分级验收

**一小时链是合并/发布门禁，不是日常门禁。** 19 步里有 12 步要整份解码 demo、跑独立
oracle、扫全部 SourceTV、抓帧或做变异；它们**看不见**「刚改的这个文件还编不编得过」，
而为一个一文件改动付一小时，结果是这一轮里只有两次验收跑、中间什么都没有。

按改动能波及多大范围选档：

| 档 | 命令 | 耗时 | 什么时候跑 |
|---|---|---|---|
| 1 | `bash verify-fast.sh` | 33–37 s（两次实测） | **每次小改动**：增量构建主程序 + 五个自检探针，跑各自 `--self-test`（36 条断言） |
| 2 实体 | `bash verify-entity.sh` | 10m59s（实测） | 实体解码 / 摆放这一块做完（5 个门禁 + 58 真相干 + 单份真 demo 的 oracle 对照 = 8 项） |
| 2 渲染 | `bash verify-render.sh` | 3m37s（实测） | 材质 / VTF / BSP / 画面这一块做完（4 项） |
| 2 音频 | `bash verify-audio.sh` | 约 10 s | 音频这一块做完（28 条断言，全走注入 sink，不开真设备） |
| 3 | `bash verify-all.sh` | ~80 min | **合并主线、发版、改核心协议**，且必须在**已提交的干净树**上 |

单项门禁也可以直接跑（改哪儿跑哪儿）：

| 改了什么 | 跑什么 |
|---|---|
| `main.cpp`、窗口循环、标题/UI | `verify-fast.sh` + `bash idle-spin-check.sh` |
| `demo_header.cpp`、实体协议 | `verify-entity.sh` |
| `entity_model.cpp` | `verify-entity.sh`（含历史覆盖 / 武器世界模型 / 观察目标 / 选槽新鲜度 / 属性查找） |
| 材质、VTF、BSP、渲染 | `verify-render.sh` |
| 音频 | `verify-audio.sh` |
| 只有文档、脚本、证据 | `bash -n <改过的脚本>` + 跑受影响的那个脚本，不必重跑真 demo |

**变异不必每次都跑。** 它回答的是「这条判据能不能变红」，是关于**判据**的问题；
只有改了解码边界、判据脚本或验收条件时才必须跑。四个分层脚本各自带 `--mutation`。

**每个提交保留的证据仍然只有完整链的那一份**（`evidence/verify/`）。分层脚本的输出写在
`evidence/fast/`（已 gitignore）：它们每轮要跑很多次，是该轮的工作读数，不是该提交的证据。

这条**不是靠自觉，是靠断言**：门禁默认把自己的原始读数写进 `evidence/<门禁名>/`，
而那些目录正是**链**提交的地方 —— 直接调用会把提交过的记录用一次临时读数覆盖掉。
两个第 2 档脚本因此显式把每个门禁的 `*_OUT` 变量指到 `evidence/fast/`（`mutate.sh` 一直
就是这么用的），并在末尾**断言 `git status --porcelain evidence/` 前后一模一样**。
2026-10-09 实测过这个缺陷：重定向存在之前，跑一次实体档改写了 **7 个已跟踪文件**，
并把 `evidence/weapon-world-model/pov-dump.txt` 截掉了 **1327 行**。

分层不改变判据纪律：每一档都断言**具名键值与工作量计数**，不只断言退出码 ——
「rc=0 但什么都没查」是这个仓库命中过四次的缺陷类，见下面「注意」第一条。

## 入口

| 文件 | 用途 |
|---|---|
| `HANDOFF-2026-10-07.md` | **接手入口（先读这个）**：已完成 / 未完成 / 困难 / 试过但没用的办法 / 复核命令 / 红线 |
| `ACCEPTANCE-P0-2026-10-06.md` | **P0 主验收文档**，按清单的强制验收清单逐项填写 |
| `HANDOFF-P0-2026-10-06.md` | P0 的可并入主线的交接条目 |
| `ACCEPTANCE-P1-2026-10-06.md` | **P1 第 1 项验收**：§1–§9 模型引用（`m_nModelIndex` → `modelprecache` → 可加载路径），§10 逐 tick 位移 |
| `HANDOFF-P1-2026-10-06.md` | P1 第 1 项的交接条目（含对旧文档 `requests=0` 归因的更正） |
| `oracle-trajectory-check.py` | **逐值**门禁：与独立实现（demostf）按 tick 对照实体位置。tick 对齐按值推导，不硬编码 |
| `fix-P0-entity-messages.patch` | 只含 `native/` 的协议修复补丁（**不含** `226d119` 的分类器修正） |
| `evidence/probe-baseline/` | **冻结**的 9 份探针报告（`recording_stream=` / `index_state=` 两行加入之前的二进制产出），供 `check-probe-output-additive.sh` 当基线。不要用 `run-demos.sh` 覆盖它 |
| `evidence/probe-baseline/added-lines.txt` | 同上，冻结的是 P1 新增的两行（`sound_precache_entries=` / `asset_refs=`）。剥离的行也要有基线，否则「additive」等于「没人验证」 |
| `evidence/weapon-world-model/` | 武器世界模型门禁的原始读数：fixture 断言行、bagel/POV 逐实体武器列表（`--dump-weapon-models`）、oracle 见证块、两份 JSON 计数器 |
| `evidence/observer-focus/` | 观察目标门禁的原始读数与两份门禁输出：fixture 断言行、POV 五档 observer 行（含 `resolved=`）、录制相机对照（三个 tick 一行一个）、录像者死亡见证（`pov-e18-dead.txt`）、541 点普查（`pov-census.json` / `.txt`）、实体 3 两槽读数、oracle 见证块、`gate.txt` / `gate-mutation.txt` |
| `slot-freshness-check.sh` | **选槽新鲜度门禁**（验收链第 14 步）：断言规则取**后写的那个槽**、平局/无 tick 时仍回落到原 rank＋字典序、且排序与渲染器真正调用的 `extractTransform` 一致。`--mutation` 扰动两份 demo 的 `chosenStale` 并要求恰好 2 条红行 |
| `evidence/slot-freshness/` | 上述门禁的原始读数：fixture 断言行、POV 实体 3 三次查询的逐候选 `rank/name/lastWrite/age`、bagel 对照 |
| `source-tv-coverage-check.sh` | **真实 SourceTV 覆盖门禁**（验收链第 15 步）：把本机**全部 9 份** SourceTV 录制整份解码，逐份断言录制类型（头字段＋流内位）、`index_tail_bytes` 等于文件字节数（证明读完了整份）、钉住的包数/地图、以及 `entity_failures`/`malformed`/`unknown`/`delta_base_unavailable` 全零。`--mutation` 两个扰动各要求恰好 9 行红 |
| `evidence/source-tv-coverage/` | 上述门禁的原始读数：9 份 demo 各自的完整探针输出（`<name>.txt`）。不重跑就复核时读这里 |
| `frame-capture-check.sh` | **帧抓取门禁**（验收链第 16 步）：从运行中的程序抓一帧（`draw()` 内、`Present()` 之前读 back buffer，写未压缩 24 位 BMP），断言 BMP 头与自身字节数自洽、同场景两次**逐字节相同**、**两个** demo 场景与暂停场景**不同**、非法 `--capture-tick` **退出 13**；**缺输入记 FAIL 不记 SKIP**。`--mutation` 翻转图像一字节，要求内容哈希察觉。**18 条断言**。⚠️ 2026-10-08 起加了第二个 demo（`cp_snakewater_final1`，本机有装地图）与「色彩多样性 < 1000」判据 —— 原先唯一那个 demo 的地图本机没装，抓到的「demo 帧」其实是回退四边形贴的喷漆图 |
| `evidence/frame-capture/` | 上述门禁的原始产物：`paused.bmp` / `paused-again.bmp` / `demo.bmp` / `demo-installed-map.bmp` 及各自 `.log` / `.csv`（实测暂停 sha256 `ab0bc11e…`、bagel sha256 `fa1bb194…`、snakewater sha256 `18447d16…`；色彩多样性 69 / 162 / 139121）。**这是「接材质前后画面变没变」的唯一对照基线**（`mutated.bmp` 由脚本从 `paused.bmp` 现生成，已 gitignore） |
| `resource-reachability-check.sh` | **资源可达性门禁**（验收链第 17 步）：断言主程序硬编码的 5 个 VPK 里**实际打开了几个**（`pak01_dir.vpk` 本机缺失，`main.cpp:647` 会**静默跳过**）、demo 地图的 BSP 取不取得到、它的材质有多少能解到像素、多少能过 `main.cpp:775` 的 **512×512 世界图集上限**、覆盖多少三角形，外加**负面对照**（旧门禁那个 demo 的地图必须取不到），以及**反事实读数**（`triangleCoverageByCap`：把上限抬到 1024/2048/无上限各覆盖多少三角形，用于**归因**是上限还是接线丢了画面）。**12 条断言 + 变异**。`--mutation` 把期望档案数改成 5，要求变红 |
| `evidence/resource-reachability/` | 上述门禁的原始 JSON：`snakewater.json`（有装地图，`bspSource=loose`，148 材质 / 111 可解 / 7 过上限 / 30731 三角形）与 `bagel.json`（`bspBytes=0`，负面对照） |
| `native/tools/resource_reachability_probe.cpp` | 上条门禁的仪表：不建窗口、不载 demo，直接报资源可达性。用法 `resource_reachability_probe <tfRoot> <mapStem> [materialName]`，stdout 单行 JSON |
| `native/tools/vpk_query.cpp` | 辅助仪表：按 `list(prefix, ext)` 统计每个档案下的条目数。⚠️ `VpkArchive::list` 是**路径前缀**匹配，`list("concrete")` 恒为 0，必须写 `list("materials/concrete")` |
| `evidence/p1/` | P1 的原始读数：`protocol-*.fixed.txt` / `model-*.fixed.txt`（探针）、`p0-baseline.csv`（P0 对照主程序）、`bagel-after-fix.csv`（修复后主程序）、`m3.csv` / `m5.csv`（对照运行） |
| `verify-fast.sh` | **第 1 档**（33–37 s，两次实测）：增量构建主程序 + 五个自检探针，跑各自 `--self-test`，**36 条断言**（具名键值 + 工作量计数，不只退出码）。`--mutation` 篡改捕获输出，要求每份期望列表变红 |
| `verify-entity.sh` | **第 2 档 · 实体**（实测 10m59s）：58 真相干 + 历史覆盖 / 武器世界模型 / 观察目标 / 选槽新鲜度 / 属性查找五个门禁 + **单份真 demo**（bagel）与 oracle 逐计数对照 + **证据树不变断言**。**不打印 `VERIFY=PASS`**，不是合并门禁 |
| `verify-render.sh` | **第 2 档 · 渲染**（实测 3m37s）：资源可达性门禁 + 帧抓取门禁 + `material_chain_probe` 的合成层（10 个合成拒绝键 + 3 个模式键）+ 证据树不变断言。`texture_quad_probe` **刻意未接线**，理由写在脚本头部（缺一份提交进仓库的 VTF 夹具） |
| `verify-audio.sh` | **第 2 档 · 音频**（约 10 s）：三个音频探针的免设备契约（`device":"sink"` / `device=0 playbackCalls=0` / `mode=dry_run device_opened=0`）+ 工作量计数 + 设备枚举的自洽性（`device_count` 与逐设备行数相等；**不钉具体台数**，那是机器的属性），**28 条断言** |
| `verify-all.sh` | **第 3 档**，一条命令跑完整证据链（**19 步**：构建（23 exe）→ 9 份普查 → fixture → oracle → **oracle 语料抽样** → 录制类型 → 探针增量性 → 变异 → 普查判据可证伪 → 逐值对照 → **历史覆盖门禁** → **武器世界模型门禁** → **观察目标门禁** → **选槽新鲜度门禁** → **真实 SourceTV 覆盖门禁** → **帧抓取门禁** → **资源可达性门禁** → **空转门禁** → **属性查找门禁**）；`--quick` 把语料抽样降到 8 份。**只在合并/发版/改核心协议时跑**，见「分级验收」 |

## 命令

```bash
cd /d/TF2_Native_Test

# 分级验收：按改动能波及多大范围选档（见「分级验收」一节）
bash verify-fast.sh          # 每次小改动，两次实测 32.8 / 36.6 s
bash verify-fast.sh --mutation        # 证明第 1 档的判据确实能变红
bash verify-entity.sh        # 实体这一块做完，实测 10m59s
bash verify-entity.sh --mutation      # 五个门禁里四个各带自己的扰动（history-coverage 没有变异分支）
bash verify-render.sh        # 材质/渲染这一块做完，实测 3m37s
bash verify-audio.sh         # 音频这一块做完，约 10s（全走注入 sink，不开真设备）
bash verify-all.sh           # 合并/发版前的完整 19 步链约 80 min（必须在已提交的干净树上）

bash build-cmake.sh          # 干净全量 Release 构建（NMake，23 个 exe）
bash build-target.sh entity_protocol_probe entity_model_probe   # 只重建指定目标（失败即非零退出）
bash run-demos.sh evidence/final          # 9 份 demo 协议普查
bash check-oracle.sh "<oracle.exe>" evidence/final   # 与 Rust oracle 逐值对照
bash oracle-recording-types.sh            # 钉住 9 份 demo 的 POV/SourceTV 判定（头字段 + 流内 STV 位）
bash check-probe-output-additive.sh       # 证明探针新增输出行没动旧计数器，且新增行本身未漂移
bash check-probe-output-additive.sh --refresh-added   # 只在刻意改过探针输出后刷新新增行基线
bash mutate.sh               # 变异验证（证明判据能变红）；11 个用例，40 条红断言 + 4 条 hold
bash census-negative-test.sh # 证明普查判据能变红（10 个变异）
bash history-coverage-check.sh # 历史覆盖门禁：Checkpoint 答案最多能多旧（fixture + bagel）
bash weapon-world-model-check.sh # 武器世界模型门禁：武器不能被画成手臂（fixture + bagel/POV + oracle 见证）
bash observer-focus-check.sh  # 观察目标门禁：解析 + oracle 对照 + 45 单位的归因 + 541 点普查（fixture + POV + bagel）
bash observer-focus-check.sh --mutation   # 同一门禁的变异：三处比较各挪 1，必须恰好 3 条红行
bash slot-freshness-check.sh  # 选槽新鲜度门禁：规则必须取后写的那个槽（fixture + POV/bagel 对照 + 影响面）
bash slot-freshness-check.sh --mutation   # 同一门禁的变异：两份 demo 的 chosenStale 各挪 1，必须恰好红 2 条
bash source-tv-coverage-check.sh          # 真实 SourceTV 覆盖门禁：本机全部 9 份整份解码，逐份零失败
bash source-tv-coverage-check.sh --mutation  # 同一门禁的变异：两个扰动各要求恰好 9 行红
bash frame-capture-check.sh   # 帧抓取门禁：从运行中的程序抓一帧，断言产物格式与场景敏感
bash frame-capture-check.sh --mutation   # 同一门禁的变异：翻转图像一个字节，要求内容哈希察觉
bash resource-reachability-check.sh       # 资源可达性门禁：5 个 VPK 开了几个、demo 地图的 BSP/材质取不取得到
bash resource-reachability-check.sh --mutation  # 同一门禁的变异：期望档案数改成 5，必须变红
bash verify-all.sh --quick   # 上面全部串起来（19 步）；完整跑约 80 min，只用于合并/发版
# 单独问一份地图的资源可达性（不建窗口、不载 demo）：
./native/build-nmake/resource_reachability_probe.exe "$TF" cp_snakewater_final1

# P1：模型引用读数的单点复现
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
POV="$TF/demos/autorecord_2026-07-02_13-26-46.dem"
./native/build-nmake/entity_protocol_probe.exe "$POV" | grep -E 'precache_entries|asset_refs='
./native/build-nmake/entity_model_probe.exe --tf-root "$TF" --demo "$POV"

# P1：选槽新鲜度的单点复现（逐候选打印 rank / 最后写入 tick / 年龄）
./native/build-nmake/entity_model_probe.exe --tf-root "$TF" --demo "$POV" \
  --prop-candidates-at 53976,54747,55394 --entity 3 --candidate-suffix m_vecOrigin

# P1：主程序读数（会出声，必须 --audio-device 6）
BAGEL="D:/TF2_Demo_Player/testdata/demos/4a9bfb9276509d0ec5f5fdc722a95b17_match-20260927-0239-koth_bagel_rc13.dem"
timeout 60 ./native/build-nmake/tf2_demo_native.exe --tf-root "$TF" --demo "$BAGEL" \
  --audio-device 6 --metrics-file "D:/TF2_Native_Test/evidence/p1/bagel-after-fix.csv"
head -3 evidence/p1/bagel-after-fix.csv    # 首帧应在 ~18 s 出现

# P1 逐 tick 位移与 z 读数（详见验收文档 §4.2）
./native/build-nmake/entity_model_probe.exe --tf-root "$TF" --demo "$BAGEL" \
  --trajectory 24 2>/dev/null | tr ',' '\n' | grep trajectory
./native/build-nmake/entity_model_probe.exe --tf-root "$TF" --demo "$BAGEL" \
  --trajectory-at 129211 --rendered 2>&1 >/dev/null | grep 'entity=1 '
./native/build-nmake/entity_model_probe.exe --tf-root "$TF" --demo "$BAGEL" \
  --history-stats 2>&1 >/dev/null | head -2
./native/build-nmake/entity_model_probe.exe --tf-root "$TF" --demo "$BAGEL" \
  --history-stats --history-coverage 512 2>&1 >/dev/null | grep -E 'history (gap|staleness|retained)'

# 逐值门禁（约 1.5 分钟）
PY=C:/Users/Administrator/.workbuddy-ai/binaries/python/versions/3.13.12/python.exe
$PY oracle-trajectory-check.py --ticks 4              # GATE=PASS, compared=40
$PY oracle-trajectory-check.py --ticks 2 --mutation   # MUTATION-CAUGHT=PASS

# 只读头 1072 字节的独立 Python 头解析器，可覆盖全量语料（秒级）
python corpus-header-scan.py --demos-dir "D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/demos"
# 语料抽样与 Rust oracle 逐值对照（自带 --selftest）。
# ⚠️ 2026-10-09 起输入集必须钉住：--demos-list 指向冻结名单（否则抽样活目录，
# 语料一涨读数就不可复现）。链条第 5 步就是这么调的。
python oracle-corpus-check.py \
  --demos-list evidence/corpus-calib/demos.txt --sample 8 --workers 2

# 全语料普查（1644 份 / 40.2 GB，3 并发约 4 小时；--resume 可断点续跑）
python corpus-census.py \
  --demos-dir "D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/demos" \
  --outdir evidence/corpus-full --workers 3 --timeout 1800 --resume

# 上面两步串起来（先 oracle 抽样再全量，避免内存撞车）
bash run-corpus-evidence.sh
```

## 探针

| exe | 说明 |
|---|---|
| `native/build-nmake/entity_protocol_probe.exe` | 协议普查：包数、消息类型直方图、实体失败坐标、实例基线、precache 表（`soundprecache` / `modelprecache`）、资产引用分解、录制类型（头字段 + 流内 STV 位） |
| `native/build-nmake/entity_message_fixture_probe.exe` | 58 个 bit-exact 合成 wire fixture，覆盖 baseline/delta/Preserve/Leave/Delete |
| `native/build-nmake/presentation_probe.exe` | 视图数学 / 录制类型分类 / 投射物字段 / 全跨度 seek / 历史覆盖（合成均匀供给，`historyWorstGap` vs `historySlotFloor`）自检 |
| `native/build-nmake/entity_model_probe.exe` | 资产引用 → 渲染请求；`--tf-root` + `--demo` 给出 `requests` / `demoRenderable` / `vpkExtracts`；`--self-test` 校验实例矩阵；诊断开关见下 |
| `native/build-nmake/tf2_demo_native.exe` | 主程序 |

`entity_model_probe` 的诊断开关（**全部输出走 stderr**，stdout 仍是单个可解析 JSON；
不带这些开关时输出与 `evidence/probe-baseline/` 冻结内容逐字节一致）：

| 开关 | 作用 |
|---|---|
| `--trajectory N` | 沿 demo 等距取 N 个 tick，报告位移 / Z 分布 / 摘要（`trajectoryRawZZero` 是「解码器原始 z」的对照读数） |
| `--trajectory-dump` | 位移最大的实体的逐 tick 轨迹 |
| `--trajectory-at t1,t2` | 每个实体在指定 tick 的**原始** origin 属性 |
| `--rendered` | 与 `--trajectory-at` 合用：改印 `extractTransform` 的输出，即渲染器拿到的值 |
| `--props-at t1,t2 [--entity N]` | 指定 tick 的属性；带 `--entity` 时打印该实体全部属性（含 `m_nTickBase`），用于与 oracle 对齐 |
| `--dump-class-props <substr>` | 扁平发送表：槽位 / owner / 名字 / 类型 / `flags` / `bits` / `range`。形状对 `ent-oracle 3` |
| `--history-stats` | 归档与实时窗口的 tick 列表和间距（含 `flushes=` / `gapDropped=` 分拆） |
| `--history-coverage N` | 均匀抽 N 个 tick 查询一遍：`exact` / `checkpoint` / `unavailable`、最坏/中位 tick 间隙 vs 鸽笼下界、最坏陈旧度、内存字节数 |

## 注意

- 会出声的测试**必须**带 `--audio-device 6`（`耳机 (xduoo audio)`）。
  不传会落到系统默认设备（`WAVE_MAPPER`）。
- 本机 `-G "Visual Studio 17 2022"` 生成器不可用（`vcvars64.bat` 要调被沙箱
  拦掉的 `reg.exe`）。`build-cmake.sh` 顶部注释写了 NMake 走通的两个前置条件。
- 本机 exe 哈希**不可复现**（两次强制重链接差 2 字节）。
  复核锚点用源码哈希 + 编译命令，见验收文档 §8。
- `evidence/final/oracle-*.txt` 约 94 MB，已 gitignore，可随时用
  `check-oracle.sh` 重新生成。
- **`mutate.sh` / 任何会重编译的脚本必须独占运行。** 它们会重写
  `native/build-nmake/*.exe`，并发的探针脚本会读到变异版二进制。
  2026-10-06 已经因此产生过一次「读数不可比」的现场（`oracle-recording-types.sh`
  与 `mutate.sh` 撞车），两边的结果都作废重跑。
- **判据必须写「确实查了 N 件」，不能只写「没有失败」。** 后者在输入缺失时恒真。
  2026-10-06 踩过：`check-probe-output-additive.sh` 用仓库相对路径，
  在只有 `native/` 的隔离树里 9 份全部 `SKIP` 却仍报 PASS；fixture 步只断言
  `fixture_failures=0` 而不断言跑过几个；构建步只断言 `rc=0` 而不断言产出几个 exe；
  变异步只断言 `MUTATION-SUITE=PASS` 而不断言跑了几个用例。
  现在四处都加了工作量计数（`compared=` / `^PASS` 行数 / `exe_count=` / `MUTATION-RED` 行数）。
- **「退出码 0」不等于「自检跑了」。** 2026-10-09 接线第 1 档时发现的第五例：
  `item_schema_probe --self-test` **不是自检** —— 那个探针把 `argv[1]` 当 `items_game.txt`
  的路径，没有 `--self-test` 分支，于是它把参数当文件名、打印
  `error: cannot open items_game.txt: --self-test`、**返回 0**。真正的入口是
  `--wire-self-test`。凡是「顺手把 flag 名统一一下」的清理都会把它重新引进来，
  所以 `verify-fast.sh` 头部写明了这件事。
- **`compared == requested` 单独用是恒真的**（2026-10-09 自己踩到）。
  给 `check-oracle.sh` 加 `only` 过滤时，第一版只断言这两个计数相等 ——
  而**拼错 demo 名**时 `requested=0 compared=0`，相等，照样打印 `ORACLE-GATE=PASS`：
  正是那条断言要防的缺陷。现在过滤名不在名单里就直接 `FATAL` 并把可用名字列出来。
  **教训：给「子集化」加计数断言时，要先跑一遍反面（名字打错、集合为空）。**
- **`build-target.sh` 曾经永远返回 0**，所以 `entity-property-lookup-check.sh` 里
  `bash build-target.sh ... || { echo "mutation: rebuild failed"; exit 1; }` 那个守卫
  **从来没有生效过** —— 构建失败会留下旧二进制，后面的门禁就读到一个和源码不符的探针。
  现在构建失败即非零退出；还原路径的失败也改成**大声告警**而不是静默（源码修好了、
  二进制还是变异版，是这一类里最危险的一种）。
- **`texture_quad_probe` 刻意没有接进第 2 档**：它要一个真实的 `.vtf` 路径，而本机唯一的
  候选是安装目录里的松散文件。**输入集是机器的属性**这个缺陷在本仓库已经发作过两次
  （第 9 步真红、第 5 步假绿），所以接线的前提是**先往仓库里提交一份 VTF 夹具**。
  在那之前诚实的表述是「探针存在且能跑」，不是「纹理路径已验证」。
- **分层脚本必须把门禁的原始读数重定向出去。** 每个门禁默认写 `evidence/<门禁名>/`，
  而那正是**链**提交本轮证据的地方；`*_OUT` 变量就是为这件事留的（`mutate.sh` 一直在用）。
  第 2 档的两个脚本因此显式重定向，并**断言 `evidence/` 前后不变** ——
  2026-10-09 在重定向存在之前跑一次实体档，改写了 **7 个已跟踪文件**、
  把 `evidence/weapon-world-model/pov-dump.txt` 截掉了 **1327 行**。
  同理：`check-oracle.sh` 会**清空重写**它被指向的目录里的 `oracle-gate-summary.txt`，
  所以分层脚本**绝不能**把 oracle 目录指向 `evidence/final`。
- **被「剥离」的行等于没人验证的行。** 增量性检查剥掉新输出行才能比旧计数器，
  但剥掉之后那几行就再没有判据了。现在它们有独立的冻结基线
  （`evidence/probe-baseline/added-lines.txt`）。
- **VPK 目录只能解析一次。** `VpkArchive::open()` 会把整个 `_dir.vpk` 目录树读进内存建哈希表
  （`tf2_misc_dir.vpk` 几十 MB、约 5 万条目）。曾经 `ModelLoader::resolveAsset` 每查一个模型
  就重开一遍所有 `_dir.vpk`，还要为 5 个伴生后缀各再来一遍 —— 505 条路径下光重复解析就 90 秒。
  归档现在挂在 `AssetRoot` 上（`VpkArchiveSet`），只解析一次。
  **改 `resolveAsset` / `hasCompanion` / `buildRenderRequests` 时不要退回去逐次 `VpkArchive::open`。**
- **`asset_root.cpp` 与 `vpk_archive.cpp` 是一对。** `AssetRoot::archives()` 调用
  `VpkArchiveSet::openDirectory`，所以任何编译 `asset_root.cpp` 的目标都必须同时链接
  `vpk_archive.cpp`。2026-10-06 漏了 `native_install_smoke_probe` → 干净构建 LNK2019 →
  NMAKE `Stop.` → 后面 4 个目标没建 → 门禁 `exe_count=17`（期望 21）。
  **加新目标时用 `grep asset_root.cpp CMakeLists.txt` 核对一遍。**
- **读数变红先怀疑仪器，不要先怀疑刚改的代码。** 2026-10-06 我把 `entity_model_probe`
  在 bagel 上的 117 秒读成了「卡死」（其实是我给的 `timeout` 比它短），
  差点去查一个不存在的回归。给足时间后它正常返回。同类事故还有 `mutate.sh` 的
  `PATTERN NOT FOUND`：真因是重构让补丁模式过期，不是代码出错。
  同一天更隐蔽的一次：`--trajectory-at` 用**后缀匹配** `name` 以 `m_vecOrigin` 结尾，
  于是 `m_vecOrigin[2]` 被结构性排除在视野外，我差点把「仪器看不见」报成
  「解码器丢了属性」。**写过滤器时先问：如果缺陷就是「我要找的那个东西不在」，
  这个过滤器能看见它吗？**
- **玩家的 `m_vecOrigin` 是 VectorXY，它的 `z` 恒为 0 是设计如此。** 真实 Z 在
  同名的兄弟属性 `m_vecOrigin[2]`（Float）里，值落在 `EntityPropertyValue.x`。
  任何读玩家位置的地方都要走 `EntityModelResolver::extractTransform`
  （内部是 `readVectorProperty`），不要直接取 `m_vecOrigin` 的 `z`。
  同理 `DT_TFLocalPlayerExclusive` 与 `DT_TFNonLocalPlayerExclusive` 两份 origin
  在同一 tick 上可以相距约 4000 单位，选择必须确定（优先 Local）。
- **实体历史只在尾部窗口内是 tick 精确的（已缓解，未消除）。** 直到 2026-10-07，
  归档快照按**索引**抽稀，在 bagel 上有 54392 tick（约 13.7 分钟）的空洞，
  而 `main.cpp:1260` 把 `Checkpoint` 当作可绘制。`aa93926` 改为按**最大 tick 间隙**
  抽稀后：worst gap 1180 / 鸽笼下界 761，重复检查点清零，陈旧度成为可打印读数
  （主程序标题 `stale=`、探针 `--history-coverage`）。窗口外的答案仍是 `Checkpoint`，
  只是上了界；要 tick 精确需重塑保留表示（全量事件链实测 856 MB 事件 / 4.77 GB 常驻，
  已否决）。复核用 `bash history-coverage-check.sh`，不要假定任意 tick 都能拿到准确位置。
- **语料是活的。** `tf/demos` 是用户正在录的目录，会话中途就从 1643 涨到 1645 份。
  任何按 `--sample` 抽样再与「上一次的报告目录」比对的脚本都会被这个漂移误伤；
  比较前先对**本次实际用到的文件清单**取快照再取哈希。
