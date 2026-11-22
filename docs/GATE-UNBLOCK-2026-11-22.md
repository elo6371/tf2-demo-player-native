# GATE-UNBLOCK-2026-11-22 -- 死录像替换与闸门解锁

本文件记录 2026-11-22 的任务：用户的 POV 录像
`tf/demos/autorecord_2026-07-02_13-26-46.dem` 已从主机上消失，四个注册闸门
纯粹因为"读不到这个文件"而红。本文记录替换、每一条被改的固定值的来历、以及
**拒绝**改的固定值和拒绝的理由。

## 0. 结论表

| 闸门 | 状态 | 说明 |
| --- | --- | --- |
| weapon-world-model-check.sh | **绿**（重新固定） | POV 段计数 69/71/2 -> 21/21/0/1；bagel、fixture、oracle 见证一字未动（§3.1） |
| observer-focus-check.sh | **红 -- BLOCKED-INPUT-2** | 断言绑定的是旧录像的观战事件；替换录像里这些 tick 低于 archive 起点，且表与相机不符（§3.2、§5） |
| entity-property-lookup-check.sh | **红 -- BLOCKED-INPUT-1** | `sweep is cold` 读 1831 而非 0；重新固定会删除该节赖以存在的前提（§3.3、§5） |
| animation-availability-check.sh | **绿**（只换录像） | 15 条逐录像断言全部原值复现，一条未改（§3.4） |
| world-geometry-hole-check.sh | **具名 SKIP**（exit 3） | pl_odyssey 录像不存在；跳过不能读成绿（§4） |

四个注册闸门里**两个回到绿**（都是真复现出来的读数，不是放宽），两个保持红并各记
一条 BLOCKED-INPUT；几何闸门从"静默 exit 0 被 verify-all 记成失败"改成具名跳过。
没有删除任何断言、没有放宽任何上下界、没有把"观测不到"当成"观测通过"。

三条规则（任务单原文）在本文件里是这样落地的：

1. 每一次构建/抓取都从 worktree 根目录经宿主机锁：
   `bash /d/_shared/tf2-hostlock/hostlock.sh gatefix2 -- bash tools/lockrun.sh gatefix2 -- <cmd>`，
   exit 2 只表示锁被别人持有，循环重试，从不跳过。
2. 不弱化断言。不会复现的数字保持红色，记 BLOCKED-INPUT，并附上证明它为什么
   不会复现的测量（先例：`docs/GATE-UNBLOCK-2026-11-15.md` §2.2 的 D-first）。
3. 本文件里的每个数字都能在 `evidence/gate-unblock-2026-11-22/` 里找到原始日志。

## 1. 输入：死掉的那一个和替换它的是哪一个

| 角色 | 路径 | 字节 | sha256 | 状态 |
| --- | --- | --- | --- | --- |
| 原 POV（死） | `tf/demos/autorecord_2026-07-02_13-26-46.dem` | 21,856,667 | `c8a58501d222bdd9aec6db92d2782931a29a13d963a87364d603263e6dab091e` | **不存在**（2026-11-22 实测；证据里的旧日志引用同一组字节数和 sha） |
| 替换 POV | `E:/demo测试/cp_process_f12/POV/autorecord_2026-09-03_21-05-39.dem` | 100,302,450 | `b6ed3f24c4aa46ba22e66f6a38b3da129135f511995448e9109d27b0d4dc14ad` | 存在 |
| 原 SourceTV（死） | `tf/demos/73.dem` | -- | -- | **不存在**（同上） |
| 替换 SourceTV | `E:/demo测试/cp_process_f12/STV/1338f5de79ea343682c17dd514c980d9_matcha-20260927-1312-cp_process_f12.dem` | 90,995,077 | `81ceb1afa4c54ad5fdf14201b1f1a9049c183d11aa70c6ed431a69ebf9f73b94` | 存在 |

用户已授权替换（"不行就换一个"）。替换录像的头部是独立解码器读出来的，不是文件名推断：

```
demo_type HL2DEMO version 3 protocol 24 server "169.254.162.70:29104" nick "SpawN"
map "cp_process_f12" game tf duration 4151.895 ticks 276793 frames 273442 signon 340948
```

即：一场 4,152 秒、276,793 ticks 的 cp_process_f12 POV 录像，录制者是 SpawN，
和死掉那场（Sniper POV）是同一类录像（客户端 POV，带 cmdinfo 摄像机），不是
SourceTV。原始日志：`evidence/gate-unblock-2026-11-22/probes/d2-oracle-pov-200000.txt`
（330 字节，只有头部；同目录另有 `d2-props-all-200000.txt` 47,335 B、
`d4-oracle-199999.txt` 14,807 B 等六份探针原始转储）。

替换 SourceTV 与替换 POV **不是同一场比赛**：语料索引 `E:/demo测试/DEMO-INDEX.md`
记的 STV 是 2026-09-27 另一个服务器会话。因此"用同场 STV 做对照"只对
`instanceCount` 一类与场次无关的计数成立，对"这一局里发生了什么"不成立。

## 2. 共享覆盖：一个变量，五个脚本

四个注册闸门从同两个环境变量读录像，默认值就是替换后的文件。几何闸门**不**在这两个
变量里：它读自己的 `WORLD_CHECK_DEMO`，因为它的输入是另一盘录像（pl_odyssey，§4）：

| 脚本 | 变量 | 默认值 |
| --- | --- | --- |
| weapon-world-model-check.sh:70 | `TF2_POV_DEMO` | `E:/demo测试/cp_process_f12/POV/autorecord_2026-09-03_21-05-39.dem` |
| observer-focus-check.sh:139 | `TF2_POV_DEMO` | 同上 |
| entity-property-lookup-check.sh:74 | `TF2_POV_DEMO` | 同上 |
| animation-availability-check.sh:87 / :93 | `TF2_POV_DEMO` / `TF2_STV_DEMO` | 同上 / `…/cp_process_f12/STV/1338f5de…dem` |

模式沿用 `minimap-hud-check.sh:131`：变量只改"从哪里读字节"，不改任何断言、
阈值、镜头或段落列表。**与那个先例的唯一差别**：先例的默认值仍是原路径（找不到就
FATAL），这里的默认值是替换文件 —— 原文件在任何主机上都不存在，"默认值找不到就
FATAL"就不是默认值。这一点在四个脚本的注释里都写明了，理由记在这里。

## 3. 逐闸门：换什么、为什么、还剩什么

三句话总结：weapon 与 animation 是**重新固定**（数字是这盘录像的读数）；entity
与 observer 是 **BLOCKED-INPUT**（红的断言不是在说"构建坏了"，而是在说"这盘录像
不是那盘录像"）；几何闸门是**具名跳过**。

### 3.1 weapon-world-model-check.sh -- 绿（重新固定一处）

改的只有一处：`:224` `check_demo pov 21 21 0 1`（旧 `69 71 2`）加 `:218-223`
的注释。三个被固定的计数据替换录像自己的 `--history-stats
--dump-weapon-models --dump-precache "*"` 读数：

```
assetModelPathFromWorldModel 21   assetWorldModelKnown 21   assetWorldModelZero 0
assetModelPathWorldModelOnly 1    assetWorldModelResolved 21
assetWorldModelUnresolved 0       assetWorldModelOutOfRange 0
worldModelRequests 21             weaponViewModelDiffers 0   armsWeaponRefs 0
```

未动：bagel 的 `32 33 1 0`、fixture 的 12 条读数（11/10/6/2/1/1/2/6/6/2/1/1）、
oracle 见证段（用 bagel 的 enter 列表，与 POV 无关）、claim 3b 的加法恒等式。
替换录像上"每个非零 world index 都走了 world route"这条命题仍然成立，只是武器
少了一半（旧 69 -> 21），所以计数变了、命题没变。

**为什么不把共享替换换成 `pl_upward_f12` POV（仓库里现成的 archive POV）：**
它读 `weaponViewModelDiffers=2`（2026-11-22 12:26:58Z–12:29:15Z 在锁内实测：
`weapon-dump refs=641 withWorldIndex=48 … agrees=46 viewIndexZero=0 differs=2`，
原始转储 `probes/d5-upward-weapon.txt` 90,682 B），即两把武器的 view index 既不是 0
也不是 `m_nModelIndex`：`entity=471 class=243 CTFPistol` 与
`entity=475 class=298 CTFShotgun` 都是 `modelIndex=1063 viewModelIndex=936`。`:195-199` 的断言是 `differs==0`，`:187-194` 的注释说在这两盘
录像上出现冲突就是"新形状"；把 2 固定进去等于给"两个 slot 读错"留两个合法位置，
正是任务单禁止的弱化。所以共享变量 `TF2_POV_DEMO` 的默认值只能是 differs=0 的
这盘 cp_process_f12 POV。

### 3.2 observer-focus-check.sh -- 红，BLOCKED-INPUT-2

这个闸门的断言不是"probe 能读 m_iObserverMode"，而是三条**关于某一盘录像的具体
事件**的命题（`:17-43` 的注释自己写的）：

1. 实体 18 -- 该 demo 的 setview target、每个包里带 DT_LocalPlayerExclusive 的实体
   -- 在 archive checkpoint 53539/53976 声明 `mode 4`、handle `1775619 == index 3
   | serial 867 << 11`，并保持 437 ticks；
2. demo tick 37677 的相机 `x=-1112.031250 y=505.593719 z=459.031250`、pitch
   `13.764709` 与录制者自己的 `m_angEyeAngles[0]` 逐位相同；
3. 修好 rank rule 之后，若渲染器相信这张表，只会移动 `45` units（旧 2729）。

替换录像上三条全部不可观测：

* 它的实体 tick 轴是 server tick，archive 区间 `59,563..335,780`
  （`--history-stats`），而闸门的五个 observation tick
  **51,900 / 53,400 / 53,976 / 54,747 / 55,394** 和 census 的
  **1,000..55,000** 全部低于 archive 起点；
* 替换录像的实体表里根本没有 18 号。第 4 段（`:357-372`）用 ent-oracle 在
  demo tick 33,242 的 packet 里读**实体 18** 的 `m_iObserverMode` /
  `m_hObserverTarget`，读不到就 FATAL；`--props-at` 的全表是 378 个实体
  （紧凑区间 `0 2..13 34..38 40 43 46..48 50 52 54..56 61..66 … 950 952`，
  `analysis/an14.out`），没有 18，所以无论换哪个 tick 这一段都会 FATAL——
  它不是"tick 不对"，是"这个实体不存在"。
* 旧录像里有一条隐含前提："每个包里带 DT_LocalPlayerExclusive 的实体就是
  setview target，也就是实体 18"。在替换录像上这条前提不成立：server tick
  199,999 的 packet 里唯一带 `DT_LocalPlayerExclusive` /
  `DT_TFLocalPlayerExclusive` 块的是 **`EntityId(7)`**（12 行属性，
  `m_nTickBase=259576` 正是这个包自己的 resolve，`probes/d4-oracle-199999.txt:48-56`），
  而观战字段落在**实体 2** 上（`m_hObserverTarget=24587`、`m_iObserverMode=3`、
  `m_lifeState=2`，`probes/d4-props-e2.txt:47/:58/:59`；探针表里实体 2 另有
  `DT_TFNonLocalPlayerExclusive` 三条）。两块不在同一个实体上，所以"跟着
  LocalPlayerExclusive 找到的那个实体"不再是观战者。

更关键的是**即使挪到可观测的 tick，这张表也不描述这盘录像的相机**。以每行自己的
resolved checkpoint 为锚（resolved server tick − 59,567 = 对应 demo tick，59,567 由
oracle 的 `packet_entities tick=199999 delta=Some(ServerTick(259566))` 定出），把
1,004 条 in-eye 行的 targetOrigin 与该时刻相机比：

| 锚 | 窗口 | n | 中位距离 | <=50u | <=100u |
| --- | --- | --- | --- | --- | --- |
| resolved tick | +-100 | 1004 | 399.4 | 13.7% | 18.4% |
| resolved tick | +-200 | 1004 | 384.0 | 15.3% | 18.4% |
| query tick | +-200 | 1004 | 1,580.1 | 1.4% | 2.5% |

单个样本：resolved 145,589（-> demo 86,022）的 in-eye 行 target
`(2229.250000, 1314.375000, 498.750000)`，相机在 demo 86,000 是
`(2253.512939, 1068.675049, 634.643921)`。逐轴差
`(-24.262939, +245.699951, -135.893921)`，三轴平方和开方 = **281.8232**
（`analysis/an13.out`；Y 轴那 245.7 不是距离本身）。命题 3 要固定的是 45，
而闸门自己的 DIVERGENCE 就是三轴求和开方（`:254-261`，`:276` 固定 `45`），
所以同一口径下这里读 282：把 45 改成 282 不是重新固定，是把 "rank rule 是对的"
改成 "rank rule 是错的"，那是另一个断言。

另有一条独立的反证：12 个 CTFPlayer（实体 2..13）在 **1,369 个 tick 里的 1,369
个**上都有 `mode!=0`（每 tick 2 到 12 个，中位 9；实体 2 的 1,133 行里 mode 3 占
1,093 行），且 1,004 条 in-eye 行里有 384 条（38.2%）`targetOrigin` 与
`selfOrigin` 逐位相同（`analysis/an13.out`）-- 这两件事都不像一局活着的比赛，
说明这盘录像的 observer 表不能当作"某人在观战某人"的证人。

处置：五个固定 tick、census 区间、45、pitch 13.764709、实体 18 **全部保留原值**，
闸门保持红。测量日志：`evidence/gate-unblock-2026-11-22/observer-focus.txt` 与
`evidence/gate-unblock-2026-11-22/analysis/`（an6/an7/an10/an12/an13 原始脚本与输出；
本节每个数字都能用 `node analysis/an13.mjs` 从 `analysis/inputs/` 重算）。

### 3.3 entity-property-lookup-check.sh -- 红，BLOCKED-INPUT-1

唯一红的断言是 `:220` `sweep is cold (every player already has a request)` 期望
`0`：替换录像读 `resolverFallbackScanned=1831`、`resolverClassLookups=1`
（bagel 读 0）。同一节里 `instanceCount=6` 仍成立。

为什么不重新固定成 1,831：`:205-214` 说这条断言保护的是"section 1 的 fixture 是
这条路径唯一被测量的地方"。固定 1,831 等于一边承认"demo 会走这条路径"，一边继续
声称"demos 不走路" -- 抹掉前提，不是重新固定。所以 pin 保持 0、POV 段保持红。

同一套构建、同一张地图上的替换 STV（另一场会话，所以只比与对局无关的计数）读
`resolverFallbackScanned=0`、`resolverClassLookups=0`、`resolverTransforms=256`、
`resolverPropertyComparisons=124664`、`instanceCount=6`（2026-11-22 11:15 实测）。
两盘录像同一二进制、同一地图，一个扫 1,831 一个扫 0，所以 1,831 属于 POV 的字节
（客户端录制、带本地玩家），不属于构建。这条读数与 POV 的全套计数一起记在
`evidence/gate-unblock-2026-11-22/ep-lookup-pov.txt`。

旁证：仓库里同一计数在 `pl_upward_f12` POV 上读 `0`（也是 archive POV）；那盘录像
不能当共享替换（§3.1 的 `differs=2`），所以这条旁证不能把闸门变绿，只能说明红的
原因在字节里。

### 3.4 animation-availability-check.sh -- 绿（只换录像，一条断言不改）

`:87`/`:93` 两个变量加 `:76-92` 的注释，其余一字未动。替换录像上：

* `--anim-props` 的 wire class 表：`classesWithSequence=197 classesWithCycle=195
  classesWithRate=197`，`animprop class=CTFWeaponBase id=321 sequence=1 cycle=1
  rate=1 pose=0`（与旧固定逐字节一致）；
* `animprop-player` 行 `tfPlayerFound=1`，四个零槽位全 0（与旧固定一致）；
* positive control 在 bagel 上不变：`CTFWeaponBaseGrenadeProj id=322 sequence=1
  cycle=1 rate=1`。

这个闸门的 15 条逐录像断言全是"某个类有没有 sequence/cycle/rate"和"本地玩家是否
四槽皆 0"，没有一条绑定到某个 tick 的事件，所以换录像不需要改任何固定值 -- 前提
正是替换录像真的复现了这些读数。

## 4. world-geometry-hole-check.sh：具名跳过（选项 a）

### 4.1 事实

这个闸门 100% 的固定值都是 **pl_odyssey 地图、TICK=2000、在这盘录像里** 的读数：
97,453 个缺失像素、4,613 像素的封闭连通块、seam 面命名集合、两帧 SHA（`:68-72`
的注释自己就这么写）。录像不存在时它原本 `:71-79` 打印 SKIP 并 **exit 0**。

verify-all.sh 第 28 步（`:1434-1450`）的判据是：
`GEOMETRY_RC==0 && grep '^PASS: every empty pixel' && GEOMETRY_OK==53`。
exit 0 的跳过路径既不打印 `^PASS:` 也没有 53 条断言，所以**一次安静的空转被记成了
失败**：观测不到任何东西，却报告"像素不对"。

### 4.2 选 (a) 而不是 (b) 的理由

1. pl_odyssey 在支持范围之外：`docs/T85-SUPPORT-SCOPE.md:35` "出局，historical
   evidence rows only"，`:50` "历史，不在支持集"。把它搬到 launch map 上，就等于
   为一个明确不受支持的地图重新背 53 条数字。
2. 搬过去要重新抓 18 帧矩阵（`verify-all.sh:1415-1421` 的捕获，约 16 分钟）并把
   53 条断言和两帧 SHA 全部重固定，而且只有在**每个变化的像素都被解释**时才合法
   （`verify-all.sh:1423-1433` 的判据）；换地图意味着像素必然全变，"解释每个像素"
   变成"重新做一次 T26"，不是本任务的重新固定。
3. 跳过不能被算成绿：`world-sky-gate-check.sh:56-57` 与 `verify-all.sh:239-241`
   的约定是"打印一行 `SKIP <reason>` 并用自己的退出码"，调用方按具名跳过计数。
   本闸门现在 exit **3**，首行是 `SKIP no recording at <path> …`，后面 4 行
   `SKIP   …` 说明为什么没有替身，然后 SUMMARY/VERDICT。
   `verify-all.sh` 第 28 步新增 `GEOMETRY_RC==3` 分支：取首行 SKIP 的原因，
   打印 `WORLD-GEOMETRY-CHECK=SKIP (reason)`，`skip_all+1`、追加 `skip_names`，
   **不**置 `rc_all`。其余情况（rc 0 且 `^PASS` 且 53 条、rc 非 0/3）保持原判据。
4. 闸门没有被删除，53 这个数没有被放松：只要有人把录像放回
   `WORLD_CHECK_DEMO`（或原路径），它照旧跑满 53 条。

### 4.3 与 2026-11-15 先例的关系

`docs/GATE-UNBLOCK-2026-11-15.md` §2.2 的教训是"可观测的数字才能进断言"。这里的
问题是它的反面：**数字观测不到时不能记成失败**。跳过是诚实的第三种结论，前提是
调用链能把它和绿区分开——这就是 exit 3 + `SKIP` 首行 + verify-all 具名计数三件套。

## 5. BLOCKED-INPUT 表

| 编号 | 闸门 | 断言 | 期望 / 替换录像读数 | 为什么不重新固定 | 原始证据 |
| --- | --- | --- | --- | --- | --- |
| BLOCKED-INPUT-1 | entity-property-lookup-check.sh:220 | `sweep is cold (every player already has a request)` | 0 / **1831**（另 classLookups 1） | 固定 1831 = 承认 demo 会走这条路径，却继续声称 demos 不走路（§3.3） | `ep-lookup-pov.txt` |
| BLOCKED-INPUT-2 | observer-focus-check.sh **:195-198**（第一条红，先于后面所有 pin） | `FATAL: expected 5 observer rows for entity 18, got 1`（期望 5 行、实得 1 行） | 五个 observation tick（51,900/53,400/53,976/54,747/55,394）全部 `status=before-window resolved=0`，低于替换录像 archive 起点 59,563，探针只吐 1 行 | 后面 `:203`/`:211`/`:213`/`:221`/`:222`/`:223` 的 pin 根本没执行到；即使挪到可观测 tick，`:276` 的 45 在替换录像上是 282（§3.2），且 §3.2 那条"带 LocalPlayerExclusive 的就是观战者"的前提也不成立 | 改 45->282 会把"rank rule 正确"改成"rank rule 错误"，是另一个断言（§3.2） | `observer-focus.txt`、`analysis/an13.out` |

两条都不改固定值、不删断言、不放宽上下界。它们是"这盘录像不是那盘录像"的测量，
不是"构建坏了"的报告；要原样复现，只有把 2026-07-02 的那盘 POV 放回
`TF2_POV_DEMO`。

## 6. 仍指向死文件的脚本（只枚举，本任务不改）

grep `autorecord_2026-07-02_13-26-46` 命中 **27** 个 `*.sh`（生成器
`analysis/census.sh`，输出 `analysis/census.out`；已排除 `.scratch/` 与本任务新增的
注释副本）。其中五个是注册闸门：**四个**只在注释里提到它（记录"这个文件没了"），
变量已改成 §2 的覆盖 —— animation-availability-check.sh:79、
entity-property-lookup-check.sh:66、observer-focus-check.sh:131、
weapon-world-model-check.sh:62/:219；**第五个** world-geometry-hole-check.sh:73
仍然把它当**活的默认路径**（`WORLD_CHECK_DEMO` 的缺省值，§4），跳过分支正是靠
"这个路径不存在"触发的。其余 **22** 个仍把它当输入。
`注册` 列 = verify-all.sh 是否引用该脚本。

| 脚本:行 | 变量 | 作用 | 缺文件时 | 注册 |
| --- | --- | --- | --- | --- |
| audio: verify-audio.sh:1003 | `DEMO_POV` | 音频闸门的 player 段用它跑产品 | FAIL 一条（`:1005-1006`），其余照跑 | 否 |
| install-lifecycle-check.sh:144 | `REAL_DEMO` | 安装生命周期烟测的真实录像输入（`:351` 的 required 列表） | 硬失败 | 否 |
| install-readiness-check.sh:75 | `REAL_DEMO` | 同上（`:225` 的 required 列表） | 硬失败 | 否 |
| lighting-dark-account-check.sh:89 | `DEMO` | pl_odyssey 光照暗部账目（`:88` MAP=pl_odyssey） | 未验证 | 否（`docs/GATES-UNREGISTERED.md:53-54`） |
| lighting-semantics-check.sh:81 | `DEMO` | 同上（pl_odyssey 光照语义） | 未验证 | 否（同上） |
| material-coverage-check.sh:127 | `POV_DEMO` | 武器 pass 覆盖的第二帧来源（`:195` required 列表） | 硬失败 | 是（1 处引用） |
| mutate.sh:133 | `POV` | 变异脚本 m1..m10 的 POV 输入（`:289/:291/:449/:450/:478/:495/:620/:627/:849/:851`）；`:620-629` 还硬编码了旧 POV 的 69/71/2 | 跑不起来（m10 早已如此） | 是（6 处引用） |
| quality-audit-check.sh:66 | `DEMO` | 质量审计的产品运行输入 | 未验证 | 否 |
| slot-freshness-check.sh:54 | `POV` | 槽位新鲜度（entity 3 的 stale/fresh 规则） | 未验证 | 是（2 处引用） |
| t21-capture.sh:23 | `DEMO` | T21 抓取脚本的产品输入 | 硬失败（`:12` 的隔离设置守卫） | 否 |
| viewmodel-companion-check.sh:652 | （注释） | 记录旧固定值是在这盘录像上读的 | 不影响 | 是（3 处引用） |
| viewmodel-presentation-check.sh:848 | （报告字符串） | 只写进报告 `recording=`，不读文件 | 不影响 | 否 |
| viewmodel-range-check.sh:314 | `DEMO_POV` | 固定输入校验按文件名匹配后才赋值；缺文件在校验段就失败（`:318` 只处理"没匹配上"） | 硬失败（校验段） | 否 |
| viewmodel-robustness-check.sh:250 | `DEMO`（case） | "这盘就是固定那盘"的通过消息 | 不影响 | 否 |
| visual-baseline-check.sh:122 | `DEMO` | pl_odyssey 视觉基线（历史行，`docs/GATES-UNREGISTERED.md:77`） | 未验证 | 否 |
| weapon-material-check.sh:86 | `POV_DEMO` | 武器材质链的 POV 输入（`:331-332` 显式 FAIL） | 显式 FAIL | 否 |
| world-geometry-completeness-capture.sh:56 | `DEMO` | 完整性抓取的产品输入 | 未验证 | 否 |
| world-geometry-hole-capture.sh:96 | `DEMO`（默认继承闸门的 `WORLD_CHECK_DEMO`） | 几何洞的抓取子脚本 | 子脚本拒绝记身份 | 是（1 处引用） |
| world-material-face-check.sh:74 | `DEMO` | pl_odyssey 面材质 | 未验证 | 否 |
| world-material-semantics-check.sh:138 | `DEMO` | pl_odyssey 材质语义（MAP=pl_odyssey） | 未验证 | 否 |
| world-near-clip-check.sh:66 | `DEMO` | 近裁剪面 | 未验证 | 否 |
| evidence/t85/run-weapon-oom.sh:13 | `DEMO` | 证据目录里的历史 OOM 复现脚本（`:12` `[ -e ]` 硬守卫） | 硬失败 | 否 |

（"未验证"= 我没有为这个任务跑它，不是断言它能跑；本任务对它零改动。）

## 7. 其它 pl_odyssey 固定（只枚举，本任务不改）

| 脚本:行 | 内容 | 处置依据 |
| --- | --- | --- |
| lighting-semantics-check.sh:80 | `MAP=maps/pl_odyssey.bsp` | 不在支持集（`docs/T85-SUPPORT-SCOPE.md:35,:50`），未注册（`docs/GATES-UNREGISTERED.md:53-54`） |
| lighting-dark-account-check.sh:88 | 同上（`:14` 注释：在 pl_odyssey tick 2000 上测量） | 同上 |
| visual-baseline-check.sh:121 | 同上（`:548` 还提到 pl_odyssey 的 pakfile） | 历史行（`docs/GATES-UNREGISTERED.md:77`） |
| map-geometry-lighting-check.sh:207 | `MAPS=(cp_snakewater_final1 pl_odyssey cp_sunshine koth_bagel_rc12 koth_bagel_rc11)`，`:283` map→bsp | 分段测量；本任务不改 |
| map-material-matrix.sh:108 | 同一组 MAPS，`:125` map→bsp，`:172` map→demo；`:159` 记录 pl_odyssey 的 POV 录像 `autorecord_2026-05-26_23-14-06.dem`（27,718,172 B，55,294 ticks）同样已从 `tf/demos` 消失 | 同上 |
| world-geometry-hole-capture.sh:97 | `MAP=…CAPTURE_MAP:-maps/pl_odyssey.bsp` | 几何洞的抓取子脚本，随 §4 一起跳过 |
| world-geometry-budget-check.sh:94 | `READING_MAPS="pl_odyssey"`，`:49` 注释"只测量打印、从不断言" | **正确范式**：读数而非断言，其它脚本应照抄 |

## 8. 验证

五个闸门在**同一个**锁窗口里连跑（`.scratch/run-fivegates.sh`，由
`.scratch/run-fivegates-outer.sh` 循环取锁后调用）。仓库里只有这一份日志：

```
weapon-world-model     rc=0  139s
animation-availability rc=0  131s
entity-property-lookup rc=1  118s
observer-focus         rc=1   92s
world-geometry-hole    rc=3    0s
ALLDONE 2026-11-22T03:24:52Z
```

逐行时间戳在 `run-order.txt`；`locks/fivegates-attempts.log` 只有一行
`attempt=1 rc=0 11:24:52`，即这次连跑**第一次**就拿到锁，没有等待。锁等待发生在
准备阶段，日志都在 `locks/`：`d2-attempts.log` 13 次 `rc=2`（09:48:17–09:49:54）、
`d4-attempts.log` 8 次 `rc=2`（09:56:33–09:57:30）后第 9 次拿到 `rc=97`，即
`tools/lockrun.sh` 的 drift 检测器（`LOCKRUN=TRACKED-DIRTY`：
`evidence/gate-unblock-2026-11-22/` 在锁窗口内出现），`d5-progress.log` 与
`stv-cold.log` 记着后面两次读数。五个闸门的 `*_OUT` 因此一律重定向到
`.scratch/fivegates/`，锁释放后再复制进 `evidence/`。

各闸门的关键行（原文在对应的 `.txt` 里）：

| 闸门 | 关键行 |
| --- | --- |
| weapon-world-model | `WEAPON-WORLD-MODEL=PASS`；`pov pathFromWorld = 21`、`pov known = 21`、`pov zero = 0`、`pov worldModelRequests = 21`、`pov onlyWorld = 1`；bagel 与 oracle 见证段全部 OK |
| animation-availability | `ANIMATION-AVAILABILITY=PASS`；`classesWithSequence = 197`、`classesWithRate = 197`、`classesWithCycle = 195`；oracle 段 `DT_BaseAnimating slots=15` / `animation-property slots=0` |
| entity-property-lookup | `ENTITY-PROPERTY-LOOKUP=FAIL (assertions_bad=1)`；唯一红行 `FAIL pov sweep is cold (every player already has a request) = 1831, expected 0`；同节 `OK pov instanceCount (unchanged) = 6` |
| observer-focus | 前 9 条 fixture 断言全 OK；五个 tick 全是 `status=before-window resolved=0 states=0`；`FATAL: expected 5 observer rows for entity 18, got 1` |
| world-geometry-hole | `VERDICT world-geometry-hole: SKIP (0 assertions ran, 1 gate skipped)`，首行 `SKIP no recording at D:/SteamLibrary/.../demos/autorecord_2026-07-02_13-26-46.dem` |

目录：

* `inputs.txt` -- 运行时刻、HEAD、分支、四个输入文件的字节数与 sha256、两个工具的
  时间戳（替换 POV `b6ed3f24…` 100,302,450 B；替换 STV `81ceb1af…` 90,995,077 B；
  bagel `03529698…` 46,921,111 B；死文件那行是 `INPUT-MISSING`）。
* `run-order.txt`、`summary.txt` -- 上面那张表与五个日志的 PASS/FAIL/VERDICT/SKIP 汇总。
* `<gate>.txt` -- 五个闸门的完整 stdout。
* `evidence/<gate>/` -- 各闸门自己写盘的证据（`pov.json`、`fixture.json`、`pov-row.txt` …）。
* `analysis/` -- §3.2 的七个相关分析脚本和它们的输出（an3/an4/an6/an7/an10/an11/an12），
  外加 an13（§3.2 的种群与距离读数）与 an14（实体表、LocalPlayerExclusive 归属、
  observer 字段出现在哪些探针里）。脚本按 `import.meta.url` 相对读
  `analysis/inputs/`（`cam-rows.txt`、`newpov-cam.out`、`newpov-sweep2.out`、
  `sweep-rows.txt`），所以从任意 CWD 都能重算；an6/an7/an10 已复跑并与提交的
  `.out` 逐字节相同。
  `analysis/census.sh` → `census.out` 是 §6 的 27；`ctx.sh` → `ctx.out` 是 §6 那
  22 行的上下文。
* `probes/` -- 六份探针原始转储（`d2-oracle-pov-200000.txt`、
  `d2-props-all-200000.txt`、`d4-oracle-199999.txt`、`d4-props-e2.txt`、
  `newpov-hist.err`、`stv-hist.err`），§1 的头部、§3.2 的 59,567 偏移与实体表、
  §3.2 的 archive 区间 `59,563..335,780` 全部出自这里。
* `locks/` -- 取锁与 drift 检测的原始记录（见上）。

大文件说明：**两个**被 `.gitignore` 排除的 oracle 转储没有进仓库 --
`oracle-survey-bagel.txt` 8.6 MB 与 `oracle-enters-bagel.txt` 780 KB，命中
`.gitignore:10-11` 的 `evidence/**/oracle-survey-*.txt` /
`evidence/**/oracle-enters-*.txt`。它们是闸门运行时自己就能重算的中间产物，
被固定的数字已经逐条出现在对应 `<gate>.txt` 的 `OK/FAIL` 行里。
`oracle-all-classes.txt`（2,834,074 B）**在仓库里**：
`evidence/gate-unblock-2026-11-22/evidence/animation-availability/oracle-all-classes.txt`
-- 它不命中那两条规则，而且 §3.4 的 `classesWithSequence=197` 一类计数就是从它读的。
另有一个 `>1 MB` 的提交件 `analysis/inputs/newpov-sweep2.out`（2,157,391 B）：
§3.2 整张相关性表都由它重算，不提交就没法复现。见 `evidence/README.txt`。

判据核对：weapon 与 animation 退出 0 且打印各自的 PASS 行；entity 与 observer 退出 1
且失败的正是 §5 那两条（不是 `exit 2`、不是工具缺失）；geometry 退出 3 且首行
`SKIP `。与 §0 的结论表逐行一致。

## 9. 未处理的风险

1. §6 的 27 个脚本仍指向死文件。本任务只覆盖四个注册闸门与几何闸门；下一次全量
   `verify-all.sh` 仍会在 `verify-audio.sh`、`install-lifecycle-check.sh` 等
   缺文件处红（它们是"缺文件就硬失败"，不是跳过）。
2. §7 的 pl_odyssey 固件仍在。pl_odyssey 不在支持集，但那四五个未注册脚本读不到
   录像时的行为没有为这个任务验证过。
3. 替换 POV 与替换 STV 不同场（§1），任何"同场对照"的写法不能再用这对文件。
4. 共享变量只能有一个默认值。§3.1 的 `differs=2`（`probes/d5-upward-weapon.txt`）
   已经证明"一盘录像服务所有闸门"会在下一盘录像上分叉；如果以后要给某个闸门单独指定录像，必须新开一个变量并在
   文档里写清代价，而不是悄悄改默认值。
5. observer-focus 的红不是构建缺陷：probe 在这盘录像上读到的 observer 表与相机
   不符（§3.2 的表）。这需要录像层面的解释，不是代码层面的修。

## 10. 审计：本文件的每个数字落在哪个文件里

§0 的规则 3 是"每个数字都能在 `evidence/gate-unblock-2026-11-22/` 里找到原始日志"。
下表把它逐条兑现。`analysis/` 里的脚本全部按 `import.meta.url` 相对读
`analysis/inputs/`，把工作目录换到哪都一样；`an6`/`an7`/`an10` 已复跑并与提交的
`.out` 逐字节相同，其余脚本的 `.out` 就是提交的那一份。

### 10.1 数字 → 证据

| 数字 | 出处（本仓库内） |
| --- | --- |
| 死 POV 21,856,667 B / `c8a58501…` | `inputs.txt` 的 `INPUT-MISSING` 行给出路径；字节数与 sha 另有历史提交件互相印证：`evidence/audio/content-inputs.txt:37`、`evidence/audio/timeline-inputs.txt:30`、`evidence/audio/verify-audio-device.txt:45` |
| 替换 POV 100,302,450 B / `b6ed3f24…`，替换 STV 90,995,077 B / `81ceb1af…` | `inputs.txt`（`INPUT-EXISTS` + `sha256sum`） |
| 替换 POV 头部（HL2DEMO v3 / protocol 24 / ticks 276,793 / frames 273,442 / signon 340,948 / duration 4151.895） | `probes/d2-oracle-pov-200000.txt`（330 B） |
| archive 区间 `59,563..335,780`、live `335,909..336,297` | `probes/newpov-hist.err:1`、`:2` |
| 偏移 59,567 | `probes/d4-oracle-199999.txt:14`（`tick=199999 … delta=Some(ServerTick(259566))` → 259566 − 199999） |
| 实体表 378 个实体、无 18 号；LocalPlayerExclusive 属于 `EntityId(7)` | `analysis/an14.out`（读 `probes/d2-props-all-200000.txt` 与 `probes/d4-oracle-199999.txt`） |
| 实体 2 的 `m_hObserverTarget=24587` / `m_iObserverMode=3` / `m_lifeState=2` | `probes/d4-props-e2.txt:47/:58/:59` |
| 相关性表 399.4 / 384.0 / 1,580.1、13.7% / 15.3% / 1.4% / 18.4% / 2.5% | `analysis/an12.out`（读 `analysis/inputs/newpov-cam.out` + `newpov-sweep2.out`） |
| 样本 3-D 282（逐轴 24.262939 / 245.699951 / 135.893921）、1,004 / 384 / 38.2%、每 tick 2–12 中位 9、1,369/1,369 | `analysis/an13.out` |
| 相机 1,384 个采样、877 个不同原点、40 段保持 | `analysis/an3.out` |
| 1,004 条 in-eye 行里只有 4 条落在 targetOrigin 80 u 内、中位 2,322.21 | `analysis/an4.out` |
| 窗口扫描 200/400/800/1600 的 1,580.1 / 1,107.3 / 798.4 / 406.5 | `analysis/an11.out` |
| 65 个几何命中、178 对 selfOrigin 命中 | `analysis/an7.out` / `analysis/an10.out` |
| weapon：`pov refs=436 … known=21 zero=0 fromWorld=21 onlyWorld=1 differs=0` | `evidence/weapon-world-model/pov-dump.txt:24`、`weapon-world-model.txt` |
| weapon：`bagel refs=679 withWorldIndex=33 … onlyWorld=0`；fixture 11/10/6/2/1/1/2/6/6/2/1/1 | `evidence/weapon-world-model/bagel-dump.txt:46`、`fixture-line.txt` |
| pl_upward `differs=2` + 两把武器 entity 471/475 | `probes/d5-upward-weapon.txt`（`weapon-dump refs=641 … differs=2`） |
| animation：197 / 195 / 197、id=321、id=322、`DT_BaseAnimating slots=15` / `animation-property slots=0` | `animation-availability.txt:22-33`、`evidence/animation-availability/` |
| entity：`resolverFallbackScanned=1831`、`resolverClassLookups=1`、`instanceCount=6`；STV 侧 0/0/256/124664/6 | `entity-property-lookup.txt`、`ep-lookup-pov.txt`、`locks/stv-cold.log` |
| observer：五个 tick 全 `before-window resolved=0` | `observer-focus.txt:10-14`（该日志 20 行，止于 FATAL） |
| 几何：53 条、97,453 px、4,613 px、`SKIP` 五连 | `world-geometry-hole.txt`、`world-geometry-hole-check.sh:68-72/:188-192` |
| 运行时刻与逐闸门秒数 139/131/118/92/0、`ALLDONE 03:24:52Z` | `run-order.txt`（`<gate> rc=N seconds=M`）、`inputs.txt` 的 `RUN-UTC` |
| 取锁 13 次 / 8 次 `rc=2`、第 9 次 `rc=97` | `locks/d2-attempts.log`、`locks/d4-attempts.log`、`locks/d4-progress.log` |
| §6 的 27 个 `*.sh`、§6 表 22 行 | `analysis/census.out`（`analysis/census.sh`）、`analysis/ctx.out`（`analysis/ctx.sh`） |

### 10.2 两条红是不是"诚实的 BLOCKED-INPUT"

判据：**被固定的期望值一个都没改，失败的正是那条断言，且失败原因指向输入而非构建。**
拿 base `050b98e8` 逐字节比：

| 脚本 | 删掉的行（全部） | 新增断言？ |
| --- | --- | --- |
| entity-property-lookup-check.sh | 仅 `-POV="D:/…/tf/demos/autorecord_2026-07-02_13-26-46.dem"` | 无（`git diff … \| grep 'assert_eq'` 无输出） |
| observer-focus-check.sh | 仅 `-POV="D:/…/autorecord_2026-07-02_13-26-46.dem"` | 无（同上） |
| animation-availability-check.sh | 仅 `-POV=` 与 `-SOURCETV="…/tf/demos/73.dem"` | 无（同上） |

两个红闸门的**第一条**失败断言（原文，逐字）：

* entity-property-lookup-check.sh —— 断言本身未改动，仍是
  `assert_eq "$demo sweep is cold (every player already has a request)" "$d_scan" "0"`（`:220`），
  失败行是 `  FAIL pov sweep is cold (every player already has a request) = 1831, expected 0`；
  全局 `assertions_bad=1`，紧随的 `OK pov instanceCount (unchanged) = 6` 仍通过。
* observer-focus-check.sh —— 失败在**行数守卫**，不是任何 pin：
  `FATAL: expected 5 observer rows for entity 18, got 1`（`:195-198`）。
  这条守卫先于 `:203`/`:205`/`:211`/`:213`/`:221`/`:222`/`:223` 全部执行，
  所以那些 pin **从未运行过**。日志 `observer-focus.txt` 只有 20 行：`OK` 9 行全部是
  1/4 的 fixture 段（`:3-11`），`:14-18` 是五个 tick 各一行的
  `status=before-window resolved=0 states=0`，`:19` 是
  `observer-focus-summary ticks=5 examined=0 emitted=0 …`，`:20` 就是这条 FATAL。
  所以正确说法是"守卫先退出了，后面的断言一条都没跑"，不是"某些 pin 读数变了"。

两条都不是 `exit 2`（工具缺失），也不是构建失败：同一次运行里 weapon 与 animation
两个闸门在同一份二进制上是绿的（`OK=36`、`OK=23`，`FAIL=0`）。

### 10.3 rc=3 的 SKIP 分支不可能把真失败伪装成跳过

`verify-all.sh:1454-1464` 的新判据按退出码分三支。逐支走一遍，用
`world-geometry-hole-check.sh` 的全部退出点做输入：

**闸门的全部退出点**（`grep -nE 'exit [0-9]'` 的原样结果）：`:88 exit 3`、
`:98/:113/:117/:712 exit 2`、`:720/:759/:896/:1323/:1687 exit 1`、
`:1320/:1684 exit 0`。`set -uo pipefail`（`:56`）**没有** `-e`，所以不存在
"命令失败顺手带出别的退出码"的隐式路径；`verify-iso.sh:19-36` 的 EXIT trap 只追加
`rm -rf` 隔离目录、不改退出码，而且 `:108` 才挂上，晚于 `:88`。

| 情形 | 闸门行为 | verify-all 判定 | 结论 |
| --- | --- | --- | --- |
| A 输入缺席 | `:80-87` 先打印 6 行 `SKIP`，再 `exit 3` | 走 `:1454` 分支 → `SKIP (reason)`，`skip_all+1`，**不**置 `rc_all` | 具名跳过，摘要行写 `N skipped: WORLD-GEOMETRY-CHECK`，不会被读成绿 |
| B rc=0 但没有 `^PASS: every empty pixel` | `:1320` 之外无从 exit 0 | `elif` 的 `grep -q` 不成立 → `else` → `FAIL (rc=0 ok=… expected 53)`，`rc_all=1` | 抓得住 |
| C rc=0 且有 PASS 行但 `GEOMETRY_OK != 53` | 同上 | `elif` 的 `[ "$GEOMETRY_OK" = "53" ]` 不成立 → `else` → FAIL | 抓得住（断言数被动过就红） |
| D 任何其它 rc（1 / 2 / 其它） | 断言失败、工具缺失、变异没变红 | `else` → FAIL 并 `rc_all=1` | 抓得住 |

**必须一起写下的诚实限定**：SKIP 分支只按 `GEOMETRY_RC -eq 3` 判，链上**没有**
再复核"日志首行是否真以 `SKIP ` 开头"。今天这是安全的，因为上面那份退出点清单里
`exit 3` 只有一个产出者（`:88`），而它前面必然先打 6 行 `SKIP`；但这是"当前事实"，
不是"结构保证"——将来若有人加第二个 `exit 3`，这条链会把它吸收成一条
`SKIP ()` 空理由。因此本文件把不变量写成一句可检验的话：
**闸门 exit 3 当且仅当首行打印 `SKIP `**。`world-sky-gate-check.sh:56-57` 与
`verify-all.sh:1502-1506` 是同一形状，同一限定同样适用于它。

改造前后对照：旧判据只有 `elif` 一处（`-if [ "$GEOMETRY_RC" -eq 0 ] && grep -q …`），
所以 **A 情形**（旧闸门 `exit 0`）落进 `else` 被判 FAIL——"没看成"被记成"看错了"。
新增的只有 A 分支与它的计数，B/C/D 三个判据一个字未动。

### 10.4 仍然只能从 worktree 复现的东西

诚实清单（这些不是"数字没证据"，而是"证据的重算需要工作树里的输入"）：

1. 闸门本身：本任务按要求没有重跑 `verify-all.sh`，五个闸门的日志是
   `.scratch/run-fivegates.sh` 那一次连跑的产物，已被复制进 `evidence/`。
2. `analysis/inputs/` 的四份转储（`cam-rows.txt`、`sweep-rows.txt`、
   `newpov-cam.out`、`newpov-sweep2.out`）现在是提交件，所以 §3.2 的两张表可以
   只靠仓库重算；但生成它们的探针调用命令行只写在 `.scratch/diag*` 里，没有提交——
   复现"这些输入是怎么来的"需要重新拼那条命令（探针自身的 `--observer-focus-at` /
   `--camera-at` 用法在 `observer-focus-check.sh:191` 与 `:353` 有示例）。
3. 两个被 `.gitignore` 排除的大转储（`oracle-survey-bagel.txt` 8.6 MB、
   `oracle-enters-bagel.txt` 780 KB）在磁盘上、不在仓库里，重算
   `bash check-oracle.sh <oracle.exe> evidence/final` 即可。


