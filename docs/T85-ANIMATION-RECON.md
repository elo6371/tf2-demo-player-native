# T85 动画重建：实测、假设与待决（RECON）

分支 task/t87-anim（基线 e15577b，integration/t79-final）。本文是 docs/T85-ANIMATION-ACTION-TABLE.md 的证据底稿：**哪些是实测、哪些只是假设、每项待定要什么决策。** 所有数字来自附录 B 的日志，未实测的一律标"未验证"。

## 0. 结论摘要

1. t84 的 141 行未提交改动**已备份、已哈希、未提交、未丢弃**：patch 与工作文件两份副本，git status --porcelain -uall 全程只有那一个 M（§1.1）。
2. census 探针**负控有效**（不存在的参数 → rc=2），三个不同地图的 demo 全部 rc=0（§1.3）。
3. **v1 census 的死亡/受伤关联计数在结构上无效**：它拿 temp entity 的服务器 tick 直接和 game event 的播放域 tick 比相等，两者差一个每 demo 不同的偏移（snakewater 10319、upward 11056，均与 tickMax 精确对上）。v1 的 exactHurt 合计（201 / 161）紧贴纯巧合期望（210.6 / 174.9）——读数就是巧合本身（§3）。**D1 已实现并重跑**：t87 副本的样本现在同时带 serverTick / playbackTick，换算后再比。snakewater/upward 的 event 9–14 变成 `exactHurt == n`（100%）与 6.2–7.7× lift；event 0 呈攻击侧（§3.5）。**但 bagel13 一条都没复现**（§3.6），换算普适性未成立。
4. player & 0x7ff **不是本轮发明的假设**：11 位索引是 basehandle.h 的既有布局，产品早就用它对 m_hActiveWeapon 解码（native/src/entity_model.cpp:140-148,638）。本轮另外证明：m_hPlayer 的 player 原始值是**百万级的打包句柄**（如 1886218 = 索引 10 + serial 921），96/96 条样本满足 player % 2048 == entity、player / 2048 在同一索引上恒定（0 冲突）——这排除了"player 字段是 userid"（§4.4）。
5. 仍然**未被证明**的是最后一步："player & 0x7ff 解出的索引就是**正在发这个动画的那个玩家**"。(b) 命中率很高（29/29 次查询该索引都是 CTFPlayer player=1），但判别力弱——overlay 每 tick 只列玩家，索引 1..20 全都是玩家（§4.3）。
6. useridEqEntity=0 **不能**用来否证掩码：userid 与 entindex 是两个编号空间（player_death 反推表把 userid 3..18 映到 entindex 2..13，§5）。census 的 animEventDeathPair 行里 userid_hit 看着 100%，那是因为该行的 userid 本来就从同一 tick 的死亡事件里抄来——**同义反复**（§4.5）。
7. 缓存是**前缀截断**：cached 恒等于 cap=16384，parsedEvents（全 demo 计数）远大于 decoded（缓存内可判读数）（§2.3-2.4）。
8. 产品侧（e15577b）本轮**没有新增任何代码**：动画层仍只有 idle/moving/crouch/crouchwalk/flinch 五态；凡"产品会怎样"都来自代码阅读，不是像素证据。
9. **A4（事件值语义）已有实证推断，仍无权威映射**：9–14 = 受伤反应家族（同 tick 必 hurt），0 = 攻击侧；3/4/5/15/23/24/29/30 贴基线，无法分类（§4.7）。这是"值→行为"的统计推断，不是协议枚举名。
10. **交叉域冗余检查不是独立证据**：census2 的 crossDomain* 是把 game event +offset 映射到服务器域再比（model_pose_probe.cpp:920,945,994-995），与 exact* 数学恒等；它只能证明换算自洽。
11. **A2 的判别力仍弱**：overlay 每 tick 只列玩家、索引 1..20 连续；本轮补了全实体花名册检定（entity_model_probe --props-at，§4.8），但"该索引=发动画的玩家"仍未被独立证明。

## 1. 对象与可复核性

### 1.1 那 141 行改动的备份（任务 1）

| 项 | 值 |
|---|---|
| worktree / 分支 / HEAD | D:/TF2_Native_Worktrees/t84-animdrive / task/t84-animdrive / 9233fcf2e6b059d867126f4e3d976b726f77a013 |
| git status --porcelain -uall | 全程只有 " M native/tools/model_pose_probe.cpp"（无新增未跟踪文件） |
| diff stat | 141 insertions(+), 0 deletions(-) |
| patch 备份 | D:/_shared/t85-anim/model_pose_probe-141line.patch，7868 B，SHA256 7868d280b1ea64c2b94bde3302533c2635ecabdab4f97aade6e13785f415cd5a |
| 工作文件副本 | D:/_shared/t85-anim/model_pose_probe.cpp.working，67502 B / 1290 行，SHA256 0eb9820c46845a66edad738cc4379c98d98a6b2f5cb8810aa2b68cda70a653d6 |
| HEAD 版本副本 | D:/_shared/t85-anim/model_pose_probe.cpp.head9233fcf，59420 B / 1149 行，SHA256 b7ed13b0e9dde06abd880718c857ae59f0f927e0a7544695f3426d2533ca4798 |
| 自洽检查 | 1290 − 1149 = 141 行；git apply --numstat 对 patch 复算 = "141 0" |
| status 快照 | D:/_shared/t85-anim/t84-git-status-snapshot.txt |

**明确结论：没有 commit、没有 checkout/restore/stash、没有丢弃。** 改动仍在 t84-animdrive 工作树里；构建只产生 native/build-nmake/ 下的产物（git status 未新增未跟踪文件，因为 build 目录被忽略）。

### 1.2 构建与负控（任务 2 前半）

| 项 | 值 |
|---|---|
| 构建命令 | cd /d/TF2_Native_Worktrees/t84-animdrive && bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run t87-anim bash build-target.sh model_pose_probe |
| 返回码 / 日志 | rc=0 / D:/_shared/t85-anim/build-model_pose_probe.log |
| build-target.sh SHA256 | d0f72766e29606fa28ce3e05c8263edafc683f13cff485989ed85ba0c1aacdd4 |
| exe | native/build-nmake/model_pose_probe.exe，917504 B，SHA256 d93022ad13d8f0961b607f87aebe29a3f4b085c57373013c7bb4f45a078ac87b，mtime 2026-11-05 13:44:33 |
| 二进制自检 | grep -a 确认含 player-animation-event-census、animEventCensus decoded=、animEventSample event=、animEventCorr event=、gameEventDeathUserids distinct= |
| 负控 A | --player-animation-event-census --bogus-flag → rc=2，输出 FAIL player-animation-test reason=event-census-argument detail=--bogus-flag（census-badarg.log） |
| 负控 B | --player-animation-event-census（无 --demo）→ rc=2，FAIL player-animation-test reason=demo-missing-argument（census-nodemo.log） |

CLI 参数校验确实存在：未知参数与缺输入都非零退出。

### 1.3 census 运行（任务 2 后半）

全部经 hostlock.sh run t87-anim，同一 exe（d93022ad…），rc=0；探针不启产品、不碰 audio，因此无需 --audio-device。

| demo | demo SHA256 | BSP | 用时 | 输出行数 | 日志 |
|---|---|---|---|---|---|
| cp_snakewater_final1（51.8 MB） | 308cc1453672b26dfdcc106bcb62eac4a7eea97899adc8830f51711be834607f | cp_snakewater_final1.bsp f09d406d2bbe397c8f9df9ca17f11b36b53d40fcf57e3f1532ec9af811b2a1eb | 26 s | 163 | D:/_shared/t85-anim/census-snakewater.log |
| pl_upward_f12（75.7 MB） | 7e047aff1f7743915e355eac907a95dc3d981aa0fae5591b6b15346fb77dc4a3 | pl_upward_f12.bsp 0d9fe5486023df49f0e6d4da43ac3e777baee87f1c588933dc16750cb7c51677 | 43 s | 196 | D:/_shared/t85-anim/census-upward.log |
| koth_bagel_rc13（46.9 MB） | 03529698db749e77a1df1c476c1c0cc06847e1173b5366a0602f1e15161b5436 | koth_bagel_rc12.bsp c991a61edb0346e57ad4bfd256693322afb6a522d8005492418b12c094f1d680 | 29 s | 154 | D:/_shared/t85-anim/census-bagel13.log |

完整命令：./native/build-nmake/model_pose_probe.exe --player-animation-event-census --demo "<demo 绝对路径>"（demo 来自 D:/TF2_Demo_Player/testdata/demos/，文件名含空格，路径已加引号）。

## 2. census 读数

### 2.1 总表

| demo | decoded | cached / cap | parsedEvents | fieldHits | player_death | player_hurt | 事件值种数 |
|---|---|---|---|---|---|---|---|
| cp_snakewater_final1 | 6321 | 16384 / 16384 | 17444 | 15654 | 109 | 2144 | 18 |
| pl_upward_f12 | 4265 | 16384 / 16384 | 46671 | 13386 | 248 | 3494 | 20 |
| koth_bagel_rc13 | 7819 | 16384 / 16384 | 16722 | 17776 | 168 | 2191 | 18 |

### 2.2 m_iEvent 取值分布（实测，18/20/18 种）

- snakewater：0:475、1:19、3:798、4:804、5:328、6:1553、9:545、10:109、11:132、12:79、13:86、14:141、15:293、17:59、23:224、24:447、29:110、30:119
- upward：0:589、1:63、2:5、3:294、4:336、5:142、6:1296、9:192、10:49、11:93、12:42、13:38、14:32、15:49、17:61、20:5、23:382、24:485、29:108、30:4
- bagel13：0:538、1:25、3:938、4:940、5:425、6:2201、9:508、10:129、11:158、12:107、13:109、14:173、15:325、17:85、23:281、24:463、29:284、30:130

能说的：这是小枚举（18–20 种取值，范围 0..30），不是任意整数。不能说的：**没有**任何一个值到动作的映射是本轮验证过的（§6 A4）。

### 2.3 四个计数器的一句话定义（可复核）

- parsedEvents = summary.tempPlayerAnimEventCount：**整个 demo 扫描中遇到的所有** CTEPlayerAnimEvent 数（native/src/demo_header_events.cpp:295），与缓存无关。
- cached = tempEntityEvents.size()：临时实体事件缓存条目数；demo_header_events.cpp:310 是 if (summary.tempEntityEvents.size() < 16384u) —— **首次填满即停止 push**，不是淘汰最旧。
- decoded = 缓存内同时满足 className=="CTEPlayerAnimEvent" && hasPlayer && hasEvent 的条目数（native/tools/model_pose_probe.cpp:794-806）；所有相关性只在这个集合上算。
- fieldHits = 缓存内 anim event 的 knownFieldCount 之和（demo_header_events.cpp:323）；applyTempEntityFields 对所有临时实体先试 m_vecOrigin/m_vecAngles（demo_header_events.cpp:45-46），故可大于 2×decoded（15654 > 2×6321），**不是**命中率。
- cap=16384 是常量上限，不是观测值。

### 2.4 decoded < cap 为什么仍是前缀截断

- 被截断的量是 cached（cached == cap == 16384，三个 demo 全中），decoded 只是它的**子集**（另一部分是非 anim 的临时实体事件）。
- 独立证明：parsedEvents ≫ decoded（snakewater 17444 vs 6321；upward 46671 vs 4265）；若缓存是全量，两者应相等或同量级。
- 结论：census 看到的是**每个 demo 最前面的 16384 条临时实体事件**及其中的 anim 子集，**不是全量**；decoded 不能外推成"该 demo 一共有多少动画事件"。

## 3. tick 域：本轮最重要的发现（原 census 的相关性计数无效）

### 3.1 代码事实（两套域）

| 消息 | tick 来源 | 位置 |
|---|---|---|
| svc_TempEntities（含 CTEPlayerAnimEvent） | svc_TickMsg 的**绝对服务器 tick**：networkTick 由 type==3 消息更新（native/src/demo_header.cpp:143），再传给 readTempEntities(bits, summary, networkTick)（:162），最后 event.tick = packetTick（native/src/demo_header_events.cpp:312） | 代码链已读通 |
| svc_GameEvent | **播放域** tick：event.tick = entryTick（native/src/demo_header_events.cpp:162），注释 :139-140 写明 "The tick is the cmd=2 packet tick (playback domain), NOT the signon entry's tick field" | 代码链已读通 |

### 3.2 换算关系（两个 demo 数值验证）

entity_overlay_probe 打印 serverTickOrigin=summary.firstDataNetTick、firstRecordedPacketTick=1、实体历史窗口 retainedLast：

| demo | serverTickOrigin | firstRecordedPacketTick | retainedLast | 播放域 tickMax | retainedLast − tickMax |
|---|---|---|---|---|---|
| snakewater | 10320 | 1 | 74671 | 64352 | **10319** = 10320 − 1 ✓ |
| upward | 11057 | 1 | 96260 | 85204 | **11056** = 11057 − 1 ✓ |

即 server_tick = playback_tick + (serverTickOrigin − firstRecordedPacketTick)，两个独立 demo 精确成立。含义：census 里 player_death 在 tick 71（播放域），anim 事件在 tick 10324（服务器域）——**不是相差 10253 个 tick，而是两把尺子**。

### 3.3 后果：相关性读数就是巧合本身

| demo | decoded | 死亡数 | 播放域 tickMax | 巧合期望（decoded×死亡/tickMax） | 实测 exactDeath 合计 |
|---|---|---|---|---|---|
| snakewater | 6321 | 109 | 64352 | **10.7** | **8** |
| upward | 4265 | 248 | 85204 | **12.4** | **20** |

| demo | decoded | player_hurt 数 | 巧合期望 | 实测 exactHurt 合计 |
|---|---|---|---|---|
| snakewater | 6321 | 2144 | **210.6** | **201** |
| upward | 4265 | 3494 | **174.9** | **161** |

受伤两行尤其干净：实测紧贴巧合期望（−4.6% / −8.0%）。**所以"没有任何 event 值与死亡/受伤对齐"这个观察是对的，但它证明的是"当前比较方法无效"，不是"event 值不携带动作信息"。**

### 3.4 要修什么（决策项 D1）

- **D1**：探针要同时保留 entryTick（播放域）与 networkTick（服务器域），或直接输出换算后的播放域 tick，再重跑关联。
- 偏移**每 demo 不同**（10319 / 11056），不能写死，必须按 serverTickOrigin 校准。
- D1 完成前，census 的 exactDeath/nearDeath/exactHurt/nearHurt/animEventDeathPair **不得作为结论引用**。产品决策（不用 event 枚举）不受影响，其真正依据是"没有权威映射 + 域对齐后仍未测出相关性"。

### 3.5 域对齐重跑（D1 实现版，census2）

t87 副本已把换算做进探针：每个样本同时落 `serverTick`（temp entity 原始 tick）与 `playbackTick = serverTick - serverMinusPlayback`，并保留旧比对作交叉域冗余检查（`native/tools/model_pose_probe.cpp:851,987-995`）。

**一个必须先说清的实现细节**：`crossDomainExactDeath/crossDomainExactHurt` 不是"旧读数"，而是把 game event 的 tick **加上** offset 映射到服务器域后再比（`:920,945,994-995`）。它与 `exact*` 在数学上恒等（实测 357=357、545=545），只能当**换算自洽性检查**用，不是独立证据。真正的旧读数在 v1 日志（`census-*.log`）里。

| demo | serverMinusPlayback | event 9 exactHurt / n | event 9 hurt lift | event 9 窗口内受害者占比 | event 0 exactHurt / n | event 0 lift | event 0 攻击者占比 |
|---|---|---|---|---|---|---|---|
| cp_snakewater_final1 | 10319 | **545 / 545 = 1.000** | **6.97×** | 0.69 | 357 / 475 = 0.752 | 5.45× | 0.52 |
| pl_upward_f12 | 11056 | **192 / 192 = 1.000** | **6.67×** | 0.73 | 210 / 589 = 0.357 | 2.64× | 0.58 |
| koth_bagel_rc13 | 56147 | 13 / 508 = 0.026 | 0.96× | 0.08 | 13 / 538 = 0.024 | 0.84× | 0.16 |

event 10–14 在 snakewater/upward 上全部是 `exactHurt == n`（109/109、49/49、93/93、42/42、38/38、32/32 等），lift 6.2–7.7×；bagel13 上同一批值全部贴基线（0.023–0.039，基线 0.0283/tick）。

**结论（2/3 demo 成立）**：域对齐后，`m_iEvent` 的 **9–14 这六个值在同 tick 必有 player_hurt**，且窗口内该实体多为受伤者（0.58–0.80）；**event 0** 的受伤 lift 也高但攻击者占比更高（0.52/0.58），更像"发起攻击/开火"侧。这条把"事件值完全无信息"推翻了一半——但它**仍不是动作名**（见 §4.7 的推断表与置信度）。

### 3.6 bagel13 为什么没复现（未解释，已定位到候选原因）

同一套换算下 bagel13 的相关性为零（§3.5 第三行）。两个候选原因，本轮**未定论**：

1. **常量偏移可能不成立**。`demo_header.h:551-569` 已经提供了正确的逐播放 tick 表 `packetServerTicks[i]`（行 i = 播放 tick i 的服务器 tick，丢包行沿用前值），注释明确说 "not the i-th packet: S1 drops 22 packets and S5 drops 4348"。census 用的是 `firstDataNetTick - firstRecordedPacketTick` 常量偏移，只在"第一条数据包自带 svc_NetTick"时严格成立（`demo_header.h:545-550` 的 `firstDataPacketOwnNetTick` 就是记这个的）。
2. 若 bagel13 的 `firstDataPacketOwnNetTick == 0`（第一条数据包没有自己的 NetTick），`firstDataNetTick` 属于更晚的包，常量偏移会**高估**，播放域整体错位几十到几百 tick——错位后 exactHurt 应回落到基线附近，与实测 2.6% 一致。

待办（D1b）：census 改用 `packetServerTicks` 查表（而不是常量偏移），并在输出里打印 `firstDataPacketOwnNetTick / serverTickHeldCount / serverTickGaps / serverTickStalls`；这四个计数器 demo_open_probe 已经会打印（`native/tools/demo_open_probe.cpp:259-273`），本轮用 `recon3.sh` 对 bagel13 取数（hostlock 排队中；拿到后补 §3.7，未拿到前本节维持「未解释」）。

**判读纪律：D1 的换算只在 2/3 demo 上被"100% 同 tick 受伤"复现验证；在 bagel13 解决前，不许写"换算普适"。**

## 4. player & 0x7ff 到底被验证到什么程度

### 4.1 先分清三件事

| # | 命题 | 状态 |
|---|---|---|
| P1 | Source 的 CBaseHandle 低 11 位是 entindex、高 10 位是 serial | **事实**（source-sdk-2013 public/basehandle.h；本仓库已实现：native/src/entity_model.cpp:140-148 kEntityHandleIndexBits=11 / kEntityHandleIndexMask=0x7ff，:638 用它解 m_hActiveWeapon） |
| P2 | CTEPlayerAnimEvent.m_hPlayer 是打包的 CBaseHandle | **强证据**（§4.4：96/96 条 player%2048==entity，serial 恒定） |
| P3 | 解出的索引指向**正在发这个动画的那个玩家** | **未证明**（§4.3 检定判别力弱） |

### 4.2 (a) 值域

| demo | 抽样条数 | entity 最小 | 最大 | 去重 | 全部 ∈ [1,2047] |
|---|---|---|---|---|---|
| snakewater | 40（32 samples + 8 pairs） | 2 | 15 | 11 | 是 |
| upward | 52（32 + 20） | 2 | 19 | 18 | 是 |
| bagel13 | 40 | 2 | 13 | 11 | 是 |

### 4.3 (b) 同一 tick 的快照里该索引是不是玩家

用 entity_overlay_probe（t79 预构建，SHA256 7baacb60…）对 census 的 tick 取快照：

- 第一轮（早期样本 tick）：snakewater 8 个 tick 下 9 条样本行全部 entity_present=1；upward 1 条。
- 第二轮（死亡对 tick，overlay2，rc=0，15 s / 24 s）：snakewater **8/8**、upward **11/11** 的 pair tick 上，该索引都是 class=CTFPlayer player=1（其余 9 个 upward tick 未进入查询列表，**不算缺席**）。
- 合计 **29/29 次 (tick, entity) 查询，索引处都有玩家实体**。

**这条检定必须连带写清强度**：

1. overlay 每 tick 只列**玩家**（OverlayEntityFilter::Players，native/tools/entity_overlay_probe.cpp:602-603），实测每 tick 19–20 行、索引 1..20 连续；census 取值 2..19 天生落在集合内——命中率高**不能**证明"这个索引是发动画的玩家"，只能证明"它落在玩家槽位区间内"。
2. **分辨率是降级的**：所有查询 fidelity=checkpointFallback，stale 从 5 到 485 tick（≈0–7.5 s）；--ticks 是**相对第一包的偏移**（serverTick = firstRecordedPacketTick + relative，native/tools/entity_overlay_probe.cpp:586），传的是服务器 tick，实际查询点晚 1 tick 再回退到最近 checkpoint。**"同一 tick"实际是"同一时间窗内"**。

### 4.4 (c) 时间一致性：句柄结构与 serial 稳定性

对 census 打印的 player=<raw> 解包（idx = raw % 2048、serial = raw / 2048），三个 demo 共 96 条：

| demo | 样本数 | idx == 打印的 entity | 不同索引 | 同索引 serial 冲突 |
|---|---|---|---|---|
| snakewater | 32 | 32/32 | 11 | **0** |
| upward | 32 | 32/32 | 13 | **0** |
| bagel13 | 32 | 32/32 | 9 | **0** |

示例：snakewater raw=1886218 → 1886218 = 2048×921 + 10 → entity 10、serial 921，该索引所有样本恒为 921。**这立住了 P2**：userid（3..22 的小整数）不可能呈现 raw/2048 非零且按索引稳定的结构。

未解异常（假设 A8）：同一 raw 值（如 1886218）同时出现在间隔一个月的两个 demo 里；真 CBaseHandle 的 serial 是服务器侧递增计数，跨比赛撞值需要相同槽位复用次数——本轮不解释，标未验证。

### 4.5 哪些说法不能用（防误读）

- ❌ "useridEqEntity 几乎全 0，所以掩码解错了"。userid 与 entindex 本不同空间；0 只说明 player 字段不是 userid（§5）。
- ❌ "pair 行 userid_hit=8/8 / 20/20，说明关联上了"。**同义反复**：该行 userid 本就从同一 tick 的 player_death 抄来。
- ❌ "死亡 tick 上那条 anim event 既不是死者也不是凶手"。**撤回**：该结论用服务器域 anim tick 去匹配播放域 death tick，前提不成立。
- ✅ 可以说：m_hPlayer 是打包句柄；索引落在玩家槽位区间且时间上稳定；**但"索引属于发动画的玩家"仍需域对齐后的独立验证**。

### 4.6 建议措辞

> player 字段是 CBaseHandle（索引 + serial，产品已用同一布局解 m_hActiveWeapon），& 0x7ff 取低 11 位得索引是既有协议事实。已实测：索引全部落在 1..2047，96/96 与句柄解包一致，serial 在同一索引上恒定。**未实测**：该索引一定是"正在发这个动画的玩家"——overlay 只列玩家，索引 1..20 全是玩家，该检定无法区分"正确的玩家"与"任意玩家槽位"。

### 4.7 A4：m_iEvent 值 → 行为 的实证推断（D1 之后，不是权威映射）

做法：对每个 event 值，把它所有样本的**换算后** tick 当锚点，统计其后 `followWindow=5` 个 tick（含两端）内 game event 的分布，并与「该 demo 全局每 tick 基线」求 lift；同时看窗口内 `player_hurt` 的 victim/attacker 有多少落在该实体索引上（`native/tools/model_pose_probe.cpp:977-1064`）。

**两个口径必须先说清**：① `exactHurt` 只表示「该样本的播放 tick 上**有人**受伤」，不表示是本人；本人受害只在 `hurtVictimHit`（窗口内）里统计。② `fail-fast` 的受害者映射来自 §5 的 demo 级 userid→entindex 经验表，不是协议身份（userinfo 未解码），所以序号上的 victim/attacker 占比带有该表的误差。

| event 值 | n（sn/up/bag） | exactHurt 比例（sn/up/bag） | hurt lift（sn/up/bag） | 受害者占比（sn/up） | 攻击者占比（sn/up） | 推断 | 置信度 |
|---|---|---|---|---|---|---|---|
| **0** | 475/589/538 | 0.752/0.357/0.024 | 5.45/2.64/0.84 | 0.06/0.06 | **0.52/0.58** | **发起攻击/开火侧**（窗口内的 hurt 多由该实体造成） | 中（2 demo 一致） |
| **9** | 545/192/508 | **1.000/1.000**/0.026 | **6.97/6.67**/0.96 | **0.69/0.73** | 0.28/0.23 | **受伤/受击反应**（同 tick 必受伤；窗口内多为受害者） | 高（2 demo 精确 100%） |
| **10** | 109/49/129 | **1.000/1.000**/0.039 | 6.61/6.30/0.91 | 0.80/0.68 | 0.26/0.07 | 同上（受伤家族） | 高 |
| **11** | 132/93/158 | **1.000/1.000**/0.025 | 6.78/6.60/0.82 | 0.75/0.68 | 0.20/0.07 | 同上 | 高 |
| **12** | 79/42/107 | **1.000/1.000**/0.028 | 6.65/6.39/1.21 | 0.67/0.71 | 0.31/0.06 | 同上 | 高 |
| **13** | 86/38/109 | **1.000/1.000**/0.028 | 7.56/7.70/0.92 | 0.64/0.58 | 0.20/0.11 | 同上 | 高 |
| **14** | 141/32/173 | **1.000/1.000**/0.023 | 7.45/6.22/0.99 | 0.68/0.69 | 0.23/0.12 | 同上 | 高 |
| 6 | 1553/1296/2201 | 0.032/0.024/0.024 | 1.40/0.61/0.89 | 0.29/0.20 | 0.28/0.18 | 无耦合；受害/攻击各约 1/4，**不能分类** | 低 |
| 3 / 4 / 5 / 15 / 23 / 24 / 29 / 30 | 59–2201 | 0.015–0.047 | 0.33–1.21 | 0.00–0.18 | 0.00–0.10 | 无耦合（贴基线），更像循环/移动/其它客户端事件 | 低（只知"不是受伤"） |
| 1 / 2 / 17 / 20 | 4–85 | 0.000–0.048 | 0.25–2.03 | 0 | 0 | 样本过少，不下结论 | — |

**能说的**：9–14 是"同 tick 有 player_hurt"的强耦合家族；0 是攻击侧。**不能说的**：9–14 之间如何区分（六个值同签名，可能是 hitgroup/方向/武器类别的不同反应，本仓库无验证路径）；0 是否等于 TF2 的 PLAYERANIMEVENT_ATTACK_PRIMARY（名字相同只是巧合级的类比，不是证据）。`bagel13` 一行全贴基线，**不能**用来支持或反驳任何值（§3.6）。


### 4.8 A2 强化：索引与"某个具体玩家"的统计绑定（不依赖花名册）

§4.3 的 overlay 检定判别力弱（它只列玩家、索引 1..20 连续）。换一个不依赖花名册的判据：**如果是"任意玩家槽位"，窗口内受伤者命中率应当 ≈ 1/玩家数**。

| demo | 窗口内 distinct entity（census identity） | 零假设 1/N | event 9–14 实测 victimShare | 倍数 | event 0 实测 attackerShare | 倍数 |
|---|---|---|---|---|---|---|
| snakewater | 12 | 0.083 | 0.64–0.80 | 7.7–9.6× | 0.52 | 6.3× |
| upward | 18 | 0.056 | 0.58–0.73 | 10.4–13.1× | 0.58 | 10.4× |

（受害占比来自 §4.7 表；零假设用该 demo 的 distinct entity 数，最大 20 玩家槽位时 1/20=0.05，结论不变。）

**这条把 A2 从"未证明"推进到"有统计证据"**：解出的索引与**某个具体玩家**的受伤/攻击强相关，不是随便落在一个槽位上。**仍未被证明的两件事**：① 只在 9–14 与 0 上成立，3/4/5/6 等无耦合值上无法检验；② 「索引 = 发这个动画的实体」这一语义仍可能错——协议字段名是 `m_hPlayer`（"动画作用于谁"），不保证"谁触发了这条消息"，两者在多数动作上重合、在他人引发的动作（如被击中）上可能指向受害者而非攻击者，本轮无法分辨。

花名册检定（entity_model_probe `--props-at`，全实体而非仅玩家）目的：确认这些 tick 上该索引是 `CTFPlayer`、且同 tick 有多少非玩家实体共用同一索引空间。结果见 §4.9（若未取到则写"未完成"）。


### 4.9 全实体花名册检定（entity_model_probe --props-at，A2 的 ①层）

工具：`entity_model_probe.exe`（t87 构建，SHA256 `88adee9dbbe616db2e6884929ca9d453e04fd5d17b74cd739b3059e446ca3a67`），命令 `--tf-root <TF2 tf> --demo <dem> --props-at <逗号 tick 列表>`；不给 `--entity` 时它打印**该 tick 全部有 origin 属性的实体**（`native/tools/entity_model_probe.cpp:938-968`，只有 `isOriginProperty` 的属性）。日志：`props2-<demo>-srv.log`（snakewater 7417 行、upward 7057 行、bagel13 7645 行），`props2-<demo>-play.log`（各 13 行）。

**先验的域证据**：服务器域 tick 全部 `status=checkpoint`（snakewater 12/12、upward 12/12、bagel13 12/12），播放域 tick（snakewater 5,7,8…；upward 1,8,17…；bagel13 39,53,56…）全部 `status=before-window`（各 12 行 + 1 行汇总）。即 `--props-at` 与 census 的 `serverTick` 同域——这独立于 entity_overlay_probe 再确认了一次换算方向。

**每个 tick 的花名册规模与索引块**：

| demo | tick 数 | 每 tick 实体行 | 玩家数 | 玩家索引块 | 非玩家实体索引 | 玩家索引块内的非玩家实体 |
|---|---|---|---|---|---|---|
| snakewater | 12 | 560 | 19 | 1..19 | 0, 20..790 | **仅 index 0 的 CWorld**（12 次） |
| upward | 12 | 530 | 19 | 1..19 | 0, 20..1024 | 同上（12 次） |
| bagel13 | 12 | 597 | 13 | 1..13 | 0, 14..837 | 同上（12 次） |

**与 census 样本的连接**：把 census2 打印的 `animEventSample serverTick / entity` 与同 tick 花名册 join，**44/44 次命中 `CTFPlayer`**（snakewater 14、upward 14、bagel13 16；0 次 miss）。且 `index<=32` 的位置里只有 `CTFPlayer`（228/228/156）与 `CWorld`（12/12/12），**没有任何非玩家实体占用 1..19**。

判读：**在抽样的 36 个 tick 上，「entity ∈ 2..19 ⇒ 该索引是一个玩家」成立**（0 反例）；但这只是"是某个玩家"，仍不回答"是哪个玩家"——后半问由 §4.8 的受害者/攻击者统计回答（0.58–0.80 vs 零假设 0.05–0.08）。两条合起来：**索引是某个具体玩家，且与事件对应的受伤/攻击动作强相关；但「m_hPlayer 的语义是'动画作用于谁'还是'谁触发'」仍无法从 demo 分辨。**

边界（别外推）：① 花名册只包含"该 tick 有 origin 属性"的实体（`entity_model_probe.cpp:959` 只打 `isOriginProperty`），没有 origin 的实体不会出现，所以"非玩家不占 1..19"只在**有 origin 的实体集合**内成立；② 只抽了 36 个 tick，不是全场；③ 每个 tick 只打一次快照（checkpoint），不是逐 tick 连续验证。

## 5. 身份：demo 级 userid → entindex 表（经验表，不是协议事实）

从 player_death 自带的 userid（死者）与 victim_entindex 反推，两个 demo 内**完全一致**（每次死亡都落在同一映射上）：

- snakewater（109 次死亡，11 个 userid）：3→2、4→3、6→4、7→5、9→6、10→7、12→9、13→10、14→11、17→8、18→13
- upward（248 次死亡，18 个 userid）：3→2、5→4、6→5、7→6、8→7、9→8、10→9、11→10、12→3、13→11、14→12、15→13、16→14、18→16、19→17、20→18、21→19、22→15

能说的：本 demo 内这张表稳定，说明 victim_entindex 是真实实体索引、userid 是另一编号空间。不能说的：它不是协议规定映射（userinfo 未解码，native/src/review_bookmarks.cpp:321 缺身份写 no-userinfo），跨 demo 也不通用（两表不同）。

## 6. 假设清单（每条写着"要什么才能升级"）

| # | 假设 | 现状 | 升级需要什么 |
|---|---|---|---|
| A1 | player 字段是 CBaseHandle | 强证据（§4.4） | 无需再验；若要更强，用槽位复用导致 serial 变化的样本 |
| A2 | 解出的索引 = 发动画的玩家 | 弱证据（§4.3）；D1 后新增：event 9–14 窗口内该实体是受害者的比例 0.58–0.80（§3.5），但仍不能排除同 tick 他人受伤 | 需要「玩家数 > 20」的 tick，或玩家入场/离场边界处索引跳变的样本（§4.8）；或与第三人称画面逐帧比对 |
| A3 | 低 11 位掩码 | **事实**（仓库既有，P1） | — |
| A4 | m_iEvent 的值语义（哪个是 attack/hurt/die） | **部分实证**：9–14 = 受伤反应家族（2/3 demo 上 exactHurt==n、lift 6.2–7.7×）、0 = 攻击侧（§4.7）；**die/taunt/reload 无任何值被证明**；无权威枚举名 | 需外部真值（客户端 CRC / 开火音频 / 逐 tick 画面）逐值标注；本仓库内无路径。bagel13 先按 §3.6 解决换算 |
| A5 | event tick 能对齐到动画帧 | 域换算已建立，并在 2/3 demo 上被「100% 同 tick hurt」反证；**未做动画帧级对齐** | 用已知受伤/死亡时刻与 trace 的 anim: 行逐帧对照 |
| A11 | bagel13 的常量偏移不成立（应用 packetServerTicks 查表） | **未验证**（§3.6）；需先打印 firstDataPacketOwnNetTick / serverTickHeldCount / serverTickGaps | 改用逐播放 tick 表后重跑 bagel13，看 9–14 是否恢复 100% 同 tick |
| A6 | 同 entityIndex 重生继承旧动画状态 | 代码显示会（native/src/main.cpp:3477 按 entityIndex 键，只在倒带时 clear :9337-9343） | 跑含重生的 demo，抓 trace anim: state 看第一帧 speed/state |
| A7 | 缓存前缀覆盖的播放时长 | **未知**（只知前 16384 条临时实体事件） | 探针输出缓存首/末事件 tick |
| A8 | 跨 demo 相同 raw 句柄值 | 异常（§4.4） | 拉第三个 demo 对照；若也相同更像确定性重连/固定 bot 名册 |
| A9 | m_iHealth 每个 damage tick 都进 delta 快照 | 未验证（flinch 依赖它） | 属性直方图探针 + 对齐 player_hurt tick |
| A10 | userinfo 可由 string table 解出 | 未实现（native/src/demo_header_strtab.cpp:277-290 只记字节数） | 单独任务：解 svc_CreateStringTable 的 userinfo 结构 |

## 7. 对动作表的影响

- **能升级的格子**：death/respawn、attack 的"协议侧证据"从"未验证"升到"event 小枚举 + 值分布实测"；但两条都**不是**"能驱动动画"。
- **能升级但仍须标推断**：flinch/hurt 行可写「CTEPlayerAnimEvent 9–14 在域对齐后与该实体受伤强耦合（统计推断，2/3 demo）」，但**不得**写成 TF2 的 PLAYERANIMEVENT_* 枚举名；attack 行可写 event 0 呈攻击侧。
- **不能升级的格子**：die/taunt/reload/deploy 仍没有任何 event 值被证明，任何「值 X = 动作 Y」的断言在 A4 获得外部真值前都不许写。
- **新增可写事实**：两套 tick 域与换算；缓存前缀截断；句柄结构实测；demo 级身份表。

## 8. 未验证边界（别外推）

- 没有跑任何门禁脚本（animation-drive-states-check.sh 等），没有启动产品、没有抓帧。
- 没有解析 userinfo。
- bagel13 的域对齐未复现（§3.6），换算普适性未成立；census 仍用常量偏移而不是 packetServerTicks 查表。
- 全实体花名册检定（§4.8）只覆盖少量 tick，不是全量。
- fieldHits 是字段个数总和，不是命中率。
- 三个 demo 的 cached 都满，因此**全部结论只覆盖每个 demo 的前缀**，不能外推为整场统计。

## 附录 A 复现命令

    1) 备份 141 行改动（只读 t84，不写仓库）
    cd /d/TF2_Native_Worktrees/t84-animdrive
    git diff -- native/tools/model_pose_probe.cpp > /d/_shared/t85-anim/model_pose_probe-141line.patch
    cp native/tools/model_pose_probe.cpp /d/_shared/t85-anim/model_pose_probe.cpp.working
    git show HEAD:native/tools/model_pose_probe.cpp > /d/_shared/t85-anim/model_pose_probe.cpp.head9233fcf
    git status --porcelain -uall | tee /d/_shared/t85-anim/t84-git-status-snapshot.txt

    2) 构建（串行锁）
    bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run t87-anim bash build-target.sh model_pose_probe

    3) 负控
    ./native/build-nmake/model_pose_probe.exe --player-animation-event-census --bogus-flag   # 期望 rc=2

    4) census（三个不同地图；DEMOS=/d/TF2_Demo_Player/testdata/demos）
    bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run t87-anim ./native/build-nmake/model_pose_probe.exe --player-animation-event-census --demo "<demo 绝对路径>"

    5) 抽样核对
    bash /d/_shared/t85-anim/crosscheck2.sh   # demo_open_probe --scan --events 0 + entity_overlay_probe
    bash /d/_shared/t85-anim/overlay2.sh      # 第二轮：死亡对 tick 的玩家快照
    bash /d/_shared/t85-anim/analyze2.sh      # A/B/C 三段离线 join

## 附录 B 证据文件清单（全部在 D:/_shared/t85-anim/）

| 文件 | 内容 |
|---|---|
| model_pose_probe-141line.patch | 141 行 patch（SHA256 7868d280…） |
| model_pose_probe.cpp.working / .head9233fcf | 工作副本（1290 行）/ HEAD 副本（1149 行） |
| t84-git-status-snapshot.txt | 改动前后 git status --porcelain -uall |
| build-model_pose_probe.log | 构建日志（rc=0） |
| census-badarg.log / census-nodemo.log | 负控（rc=2） |
| census-snakewater.log / census-upward.log / census-bagel13.log | 三次 census 输出 |
| crosscheck2.sh / crosscheck2.log | 主抽样脚本与运行日志（rc=0，含两个探针 SHA256） |
| events-snakewater.log / events-upward.log | demo_open_probe --scan --events 0 的逐事件输出 |
| overlay-snakewater.log / overlay-upward.log | 第一轮 entity_overlay_probe（早期样本 tick） |
| overlay2.sh / overlay2-snakewater.log / overlay2-upward.log | 第二轮（死亡对 tick），38510 B / 45002 B |
| analyze2.sh / analyze2.log | A/B/C 三段离线 join |
| analyze-map.sh / analyze-map.log | userid→entindex 表（该文件里的 attacker 检验结论因跨域而作废，仅留表） |

探针 exe：model_pose_probe.exe d93022ad…；demo_open_probe.exe 2d56a61d…（脚本运行时打印）；entity_overlay_probe.exe（t79 预构建）7baacb60…。
