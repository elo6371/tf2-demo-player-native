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

### 1.4 t87 分支上的复现与扩展（不碰 t84）

任务 1 的 141 行只在 t84 工作树里；为了让它在**不受污染的分支**上可构建、可扩展，按 lead 决策在 `task/t87-anim` 自己的 worktree 里复现：

| 步骤 | 结果 |
|---|---|
| t87 HEAD blob | `git show HEAD:native/tools/model_pose_probe.cpp` = SHA256 `b7ed13b0e9dde06abd880718c857ae59f0f927e0a7544695f3426d2533ca4798`（与 t84 HEAD 副本一致） |
| 应用备份 patch | `git apply` 成功；`git diff --stat` = 141 insertions(+), 0 deletions(-)；应用后文件 SHA256 = `0eb9820c46845a66edad738cc4379c98d98a6b2f5cb8810aa2b68cda70a653d6`（与 t84 工作副本逐字节一致） |
| 来源声明 | **不是从 t84 直接拷**，是在 t87 HEAD blob 上应用 `model_pose_probe-141line.patch`（该 patch SHA256 `7868d280…` 已在 §1.1 记录） |
| 扩展 | 同一文件上扩了 tick 三域、反查表、follow 窗口、句柄解析、模型指纹（`--player-animation-event-census --follow-window N --sample-limit N`；`entity_model_probe --model-fingerprints`） |
| 构建 | `bash build-target.sh model_pose_probe` / `build-target.sh entity_model_probe`，hostlock owner=t87-anim，rc=0 |
| exe | `native/build-nmake/model_pose_probe.exe` SHA256 `f3c5388f61f4e3c3c9cd55db8b65b19aeabb95d086e27fb473793cecdeb26b27`；`entity_model_probe.exe` SHA256 `90bca4909af6e6f71b5d7da719d6fb8441d041540e0793fd711f0a386ec13bfe` |
| 负控（4 条，全部 rc=2） | `--bogus-flag`（event-census-argument）、`--follow-window 9999`、`--sample-limit -1`、缺 `--demo`（demo-missing-argument） |
| recon4 运行 | 三个 demo 全部 rc=0，`--sample-limit 20000 --follow-window 5`，日志 `census4-{snakewater,upward,bagel13}.log`（sha256 `cb0c2eb1…`/`ed742949…`/`6ba0048a…`），运行记录 `recon4-run.log` |
| recon4 指纹（有 bug，作废留证） | `fingerprints-{snakewater,upward,bagel13}.log`：sha256 与 bytes 正确，`mdlName/checksum/bones` 全为 `-/0/0`（用 `ModelRenderRequest` 反查 `modelPrecache` 路径查不到）。修复版（studiohdr 定偏移直读 + 只对 `models/player/` 走 `ModelLoader::inspect`）已在 recon5 里重跑，读数见 §9 |
| recon5 运行 | 同上两个 exe 重建（`model_pose_probe` sha256 **5b02ed3d…**、`entity_model_probe` sha256 **fe60a40d…**）后：负控 2 条 rc=2；三 demo census 全部 rc=0（`census5-*.log`，sha256 `2dce2674…`/`b46d7ef6…`/`c6313945…`）；三 demo 指纹 rc=0（`fingerprints2-*.log`，sha256 `d93b1406…`/`8da56099…`/`a0a6f062…`）。运行记录 `recon5-run.log`。hostlock owner=t87-anim，16:57:55 进锁、17:01:50 结束 |

**t84 现场复核（写入本文件时）**：HEAD 仍 `9233fcf2e6b059d867126f4e3d976b726f77a013`，`git status --porcelain -uall` 仍只有 ` M native/tools/model_pose_probe.cpp`，没有 commit、没有新未跟踪文件。

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

### 3.6 bagel13 为什么没复现（已定位：常量偏移在该 demo 上不成立）

同一套换算下 bagel13 的相关性为零（§3.5 第三行）。上一版本节列了两个候选原因，**recon3（hostlock owner=t87-anim，`events-bagel13.log`，rc=0）已经把它们判掉一个、证实一个**：

先看 demo 自己的 tick 表（demo_open_probe `--scan` 的 header 计数器，`native/tools/demo_open_probe.cpp:259-273`）：

| demo | first_data_net_tick | first_data_entry_tick | last_data_entry_tick | net_minus_entry（常量偏移） | data_packets | server_tick_held | gaps | stalls | first_data_packet_own_net_tick |
|---|---|---|---|---|---|---|---|---|---|
| cp_snakewater_final1 | 10320 | 1 | 64352 | 10319 | 64330 | 22 | 11 | 0 | 1 |
| pl_upward_f12 | 11057 | 1 | 85204 | 11056 | 85198 | 6 | 2 | 0 | 1 |
| koth_bagel_rc13 | 56148 | 1 | **77466** | 56147 | **73118** | **4348** | 4 | 0 | **1** |

**候选 2（第一条数据包没有自己的 NetTick，firstDataPacketOwnNetTick==0）被否证**：bagel13 的 `first_data_packet_own_net_tick=1`，第一条数据包就带 svc_NetTick，所以 `firstDataNetTick` 就是第一条数据包的服务器 tick，常量偏移的"起点"本身是对的。

**候选 1（常量偏移不成立）被证实，而且是 header 级别的直接证据**：如果偏移是常量，末尾数据包的 entry tick 必然等于 `last server tick − net_minus_entry`。

| demo | last server tick（packet_server_last5 末值） | 若常量偏移 → 预测 last entry | 实测 last_data_entry_tick | 漂移 |
|---|---|---|---|---|
| snakewater | 74671 | 64352 | 64352 | **0** |
| upward | 96260 | 85204 | 85204 | **0** |
| bagel13 | 129277 | 73130 | **77466** | **+4336** |

bagel13 有 **4348 个 server tick 被"held"**（相邻记录包复用同一 network tick），entry tick 因此在同一段时间里继续前进——两个空间的距离从头到尾变了 4336 个 tick。`view_first5=1,3,4,5,6` / `packet_server_first5=56148,56148,56150,56151,56152` 就是第一处错位的原文（第 2 个包 entry tick 跳过 2，服务器 tick 仍停在 56148）。**在这种 demo 上，"serverTick − 常量"根本不是那个包的播放 tick。**

反证也做了（离线，用 `events-bagel13.log` 的 player_hurt 与 `census3-bagel13-all.log` 的 event 9–14 全样本 1184 条）：

- 常量偏移下 exactHurt = 32/1184 = **2.7%**（hurt 密度 2191/77466 ≈ 2.8%/tick → 就是巧合）；
- 扫所有常量平移 c ∈ [−3000, +3000]，最好也只有 48/1184 = **4.1%**；
- 试线性漂移模型 α ∈ {0, 0.02, 0.04, 0.05, 0.055, 0.059, 0.065, 0.07, 0.08, 0.1}，全部 1.8–2.7%；
- 最近 hurt 距离：中位数 756 tick、p90 1829（snakewater 对照全部为 0）。

结论：**离线任何常数/线性修正都救不回来，必须换数据源**——改用 `DemoNetworkSummary::packetServerTicks[row]`（`native/include/demo_header.h:551-564`，行=播放 tick，`packetServerTickOwn[row]` 标记真的有包的行，源 `native/src/demo_header.cpp:269-293`）反查"这个服务器 tick 对应哪一行播放 tick"。bagel13 的 `server_tick_stalls=0` 保证了记录包之间服务器 tick 严格递增，反查唯一。

D1b 的探针实现与重跑见 §3.7。

**判读纪律（保留）**：D1 的换算只在 snakewater/upward 两个 demo 上被"100% 同 tick 受伤"复现；bagel13 证明它在 1/3 demo 上根本对不齐。凡引用"同 tick"结论必须写清用的是哪种映射（常量偏移 / packetServerTicks 反查）与哪个 demo。

### 3.7 反查表方案（D1b）：把"减法"换成"查表"

§3.6 的结论要求换数据源。D1b 的实现不再假设常量偏移，而是直接用 demo 自己记录的
`DemoNetworkSummary::packetServerTicks[row]`（`native/include/demo_header.h:551-564`；填充 `native/src/demo_header.cpp:269-293`，row = entry.tick − firstDataEntryTick，`packetServerTickOwn[row]` 标记这一行确实来自一个记录包）：

1. 扫 demo 时建立 `map<serverTick, row>`，只在 `packetServerTickOwn[row] == true` 的行上登记；同一个 serverTick 出现第二次时记 `tableAmbiguous=1`（本批三个 demo 都不该出现，因为 `server_tick_stalls=0`）；
2. 对每条 `CTEPlayerAnimEvent`，用它的 `tick`（服务器域，`native/src/demo_header_events.cpp:312`）查表得到播放域 tick；查不到记 `unmapped`；
3. 相关性统计全部改用查到的播放 tick（`animEventExactFollow`），与 game event 的 entry tick 同域比较。

探针新增的两行输出（`native/tools/model_pose_probe.cpp`，本分支未提交副本）：

    animEventTickMap samples= mapped= unmapped= tableRows= tableOwn= tableAmbiguous= derivedMismatch= derivedDiffMean= derivedDiffMax= serverTickHeld= serverTickGaps= serverTickStalls= firstOwnNetTick=
    animEventExactFollow event=9 n= mapped= exactHurt= hurtVictimHit= hurtAttackerHit=

`derivedMismatch / derivedDiffMean / derivedDiffMax` 是"常量偏移算法"与"查表"在同一批样本上的差：常数偏移成立时它们应为 0。这三个数给出的是**同一份 demo 上一段代码对另一段代码的自我核对**，不是外部真值。`animEventCorr` / `animEvent` 里旧的 exactHurt/exactDeath 仍然是常量偏移口径，**不要**混用；只有 `animEventExactFollow` 与 `animEventTickMap` 是反查口径。

读数见 §3.7.1（recon4 填）。判读口径先说死：如果 bagel13 在反查口径下 exactHurt 回到接近 n（与前两个 demo 同量级），说明 §3.6 的定位正确、只是映射错；**如果没有回到**，那问题不在域，必须另找（例如解析器在该 demo 上漏事件，或 hurt 字段解析差异），不能拿"域"当解释继续用。

### 3.7.1 recon4 读数与一个 off-by-one（已定位并修正）

recon4（`D:/_shared/t85-anim/recon4.sh` 的前身脚本，hostlock owner=t87-anim，16:16 完成，EXIT=0）在锁内重建并跑了三个 demo，反查表本身是健康的：

| demo | samples | mapped | unmapped | tableRows | tableOwn | tableAmbiguous | serverTickHeld | gaps | stalls |
|---|---|---|---|---|---|---|---|---|---|
| cp_snakewater_final1 | 6321 | 6321 | 0 | 64352 | 64330 | 0 | 22 | 11 | 0 |
| pl_upward_f12 | 4265 | 4265 | 0 | 85204 | 85198 | 0 | 6 | 2 | 0 |
| koth_bagel_rc13 | 7819 | 7819 | 0 | 77466 | 73118 | 0 | 4348 | 4 | 0 |

**每一条动画事件都能在表里查到唯一行，没有歧义**（`tableAmbiguous=0`，与 `server_tick_stalls=0` 自洽）——这才是 §3.6 要求的数据源。

但同一行里的 `derivedMismatch` 暴露了一个实现 bug：三个 demo 的 `derivedMismatch` 都等于全部样本数，`derivedDiffMean` 在 snakewater/upward 是 **1.00**、在 bagel13 是 **4298.97**（max 4335）。前两个 demo 常量偏移已知漂移为 0，却系统性地差 1 tick，说明不是域的问题，是**取错了列**。

根因（`native/include/demo_header.h:553-554`）：`packetServerTicks[i]` 对应的是「entry tick = `firstDataEntryTick` + i」的那个包，**行号 i 不是 entry tick**。三个 demo 的 `first_data_entry_tick` 都是 1（`events-*.log` 头部），所以查表返回的行号恰好比播放 tick 小 1，每条样本都被比到前一 tick 去了。

修法（t87 工作副本 `native/tools/model_pose_probe.cpp`）：

    exactPlaybackTick = row + summary.firstDataEntryTick;   // 不再直接用 row

并在 `animEventTickMap` 行尾补打 `firstDataEntryTick=%d`，让这个 +1 变成可复核的显式量而不是隐含常量。

**离线重算（不占锁，用同一批 census4 样本 + `events-*.log`）**：`D:/_shared/t85-anim/offline-exactfollow2.py`→`offline-exactfollow2.out`，把每条样本的播放 tick 换成 `row + 1` 后：

| m_iEvent | snakewater exactHurt / n | pl_upward exactHurt / n | koth_bagel13 exactHurt / n |
|---|---|---|---|
| 9 | **545 / 545 = 100%** | **192 / 192 = 100%** | **508 / 508 = 100%** |
| 10 | **109 / 109 = 100%** | **49 / 49 = 100%** | **129 / 129 = 100%** |
| 11 | **132 / 132 = 100%** | **93 / 93 = 100%** | **158 / 158 = 100%** |
| 12 | **79 / 79 = 100%** | **42 / 42 = 100%** | **107 / 107 = 100%** |
| 13 | **86 / 86 = 100%** | **38 / 38 = 100%** | **109 / 109 = 100%** |
| 14 | **141 / 141 = 100%** | **32 / 32 = 100%** | **173 / 173 = 100%** |
| 0 | 357 / 475 = 75.16% | 210 / 589 = 35.65% | 421 / 538 = 78.25% |

两个结论：

1. **§3.6 对 bagel13 的定位是对的**：换用 packetServerTicks 反查后，bagel13 的 9–14 从 2.6% 直接回到 **100%**，与前两个 demo 同量级。域换算的普适性问题不是「bagel13 数据坏」，而是「常量偏移不成立、必须按包反查」。
2. **event 0 与 9–14 的侧别不同**：event 0 在窗口内命中的是**攻击者**（250/207/427）远多于受害者（27/19/26）；9–14 命中的受害者数**恰好等于 exactHurt 数**。→ 9–14 的 `m_hPlayer` 是「受伤的那个人」，event 0 的 `m_hPlayer` 是「攻击的那个人」。这使 A2 的「解出的索引 = 发动画的玩家」获得**行为侧**支撑（实体参与了该事件，不是随机槽位），但仍不等于「客户端确实为这个实体播了动画」。

其余值（3/4/5/6/15/17/23/24/29/30）在三个 demo 上都在 **0–5.9%**，贴 hurt 基线（0.0333 / 0.0410 / 0.0283 每 tick），不构成耦合。upward 有一个可疑小样本：**event 20 n=5，exactHurt 5/5、exactDeath 5/5、攻击者命中 5/5**——样本太小，标「可疑未定」，不据此下结论。

**巧合基线怎么算（可复算）**：把 `events-<demo>.log` 里所有 `player_hurt` 的 tick 去重，除以 tick 跨度（`max−min+1`），得到「随便指一个播放 tick，那里正好有人受伤」的概率：snakewater 1908/64346 = **2.97%**、upward 3123/85197 = **3.67%**、bagel13 1903/77421 = **2.46%**。event 9–14 的同 tick 命中率是 **100%**，相对基线分别 **33.7× / 27.3× / 40.7×**。这不是「受伤很频繁所以总能对上」，恰恰相反。脚本：`D:/_shared/t85-anim/baseline.py` → `baseline.out`（`player_death` 基线顺带算了：0.168% / 0.285% / 0.216%）。

（注：§3.5 表里的 6.2–7.7× 是「某值样本的 hurt 计数 ÷ 全 demo hurt 密度」的旧口径，计算方式不同；本节 33.7× 等以**去重 tick 概率**为分母，两者不可混用。）

### 3.7.2 recon5：原生复跑（修复后的 exe）与离线表逐项一致

`recon5.sh` 在 hostlock（owner=t87-anim，ACQUIRED 16:57:55，脚本 rc=0，17:01:50 结束）里先重建两个探针：`model_pose_probe.exe` sha256 **5b02ed3dc07715a00816248719b950d86cdd7fbb154585de1609caf2948f4fab**（build rc=0）、`entity_model_probe.exe` sha256 **fe60a40d8a605a291a839bb3299927e948b80fe1d94377799e0eae8b2bc1e574**（rc=0）。负控两条都真的拒了：`--bogus-flag` → rc=2 `reason=event-census-argument`；`--follow-window 9999` → rc=2 `reason=event-census-window`。

| demo | rc | 秒 | samples | mapped | tableRows | tableOwn | tableAmbiguous | derivedMismatch | derivedDiffMean / Max | held | gaps | stalls | 日志 sha256 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| snakewater | 0 | 26 | 6321 | 6321 | 64352 | 64330 | 0 | 0 | 0.00 / 0 | 22 | 11 | 0 | 2dce2674… |
| upward | 0 | 38 | 4265 | 4265 | 85204 | 85198 | 0 | 0 | 0.00 / 0 | 6 | 2 | 0 | b46d7ef6… |
| bagel13 | 0 | 24 | 7819 | 7819 | 77466 | 73118 | 0 | **7754** | **4299.95 / 4336** | 4348 | 4 | 0 | c6313945… |

三件事被原生读数确认：① **反查表 100% 命中、零歧义、零 stall**（`tableAmbiguous=0`），即 D1b 这条路在三个 demo 上都走得通；② +1 修正之后 `derivedMismatch` 在 snakewater/upward 变 **0**——说明这两个 demo 上「常量偏移 == 查表结果」，而在 bagel13 上 7754/7819 条不一致、平均差 4299.95 tick、最大 4336，**这正是 §3.6 那次漂移的量化**（表跟常量公式是两个不同的东西，只有查表能覆盖它）；③ 原生 `animEventExactFollow` 与本节的离线表**逐项相同**：

| m_iEvent | snakewater exactHurt/n | upward | bagel13 |
|---|---|---|---|
| 9 | 545/545 | 192/192 | 508/508 |
| 10 | 109/109 | 49/49 | 129/129 |
| 11 | 132/132 | 93/93 | 158/158 |
| 12 | 79/79 | 42/42 | 107/107 |
| 13 | 86/86 | 38/38 | 109/109 |
| 14 | 141/141 | 32/32 | 173/173 |
| 0 | 357/475 | 210/589 | 421/538 |

→ **§3.7.1 表里的 100% 不再是"离线重算"，而是探针原生打印**（原生日志 `D:/_shared/t85-anim/census5-<demo>.log`，行 `animEventExactFollow event=9 n=545 mapped=545 exactHurt=545 …`）。此前的离线结论没有被推翻，但只有原生这一份可以作为证据引用。


## 4. player & 0x7ff 到底被验证到什么程度

### 4.1 先分清三件事

| # | 命题 | 状态 |
|---|---|---|
| P1 | Source 的 CBaseHandle 低 11 位是 entindex、高 10 位是 serial | **事实**（source-sdk-2013 public/basehandle.h；本仓库已实现：native/src/entity_model.cpp:140-148 kEntityHandleIndexBits=11 / kEntityHandleIndexMask=0x7ff，:638 用它解 m_hActiveWeapon） |
| P2 | CTEPlayerAnimEvent.m_hPlayer 是打包的 CBaseHandle | **强证据**（§4.4：96/96 条 player%2048==entity，serial 恒定） |
| P3 | 解出的索引指向**正在发这个动画的那个玩家** | **强证据（域对齐后精确到人，§4.8.1）**：event 9–14 在同 tick 上该索引是 player_hurt 的**受害者** 2621/2722 = 96.3%，101 条未命中全部因受害者 userid 不在身份表里（不是反例）；event 0 在同 tick 是**攻击者** 814/988 = 82.4%，49 条反例（5.0%）。语义仍是「动画作用于谁」，**不是**「谁触发了这条消息」 |

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

**先给最终口径**：下表是我在 **3.7.1 的 +1 修正之后**用同 tick 精确检定重算的（脚本 `a2-exact.py` → `a2-exact.out`，输入是 census4 的样本 + `events-*.log`）。早先那张只按"同 tick 有人受伤"统计的表已经被它取代；`bagel13` 在旧口径下"全贴基线"是**域换算错**造成的假象（§3.6），不是数据本身的性质。

| event 值 | n（sn/up/bag） | 同 tick 有人受伤（sn/up/bag） | 同 tick **是受害者**（sn/up/bag） | 同 tick **是攻击者**（sn/up/bag） | 推断 | 置信度 |
|---|---|---|---|---|---|---|
| **0** | 475/589/538 | 357/210/421 | 2/2/2 | **235/170/409** | **发起攻击侧**（该实体=该 tick 的 attacker；在"有人受伤"样本里 65.8%/81.0%/97.1% 命中） | 中高（3/3 demo 同向，反例 5.0%） |
| **9** | 545/192/508 | 545/192/508 | **477/192/508** | 195/44/222 | **受伤/受击反应**（同 tick 必有 hurt） | 高（3/3 demo） |
| **10** | 109/49/129 | 109/49/129 | **107/49/129** | 33/5/15 | 同上（受伤家族） | 高 |
| **11** | 132/93/158 | 132/93/158 | **129/93/158** | 35/8/36 | 同上 | 高 |
| **12** | 79/42/107 | 79/42/107 | **68/42/107** | 31/2/29 | 同上 | 高 |
| **13** | 86/38/109 | 86/38/109 | **73/38/109** | 20/6/38 | 同上 | 高 |
| **14** | 141/32/173 | 141/32/173 | **137/32/173** | 46/4/76 | 同上 | 高 |
| 6 | 1553/1296/2201 | 50/31/49 | 8/2/4 | 3/3/3 | 无耦合，**不能分类**（规模最大的一类，像循环/姿态类） | 低 |
| 3 / 4 / 5 / 15 / 23 / 24 / 29 / 30 | 59–2201 | ≤ 数个百分点 | ≈0 | ≈0 | 无耦合（贴基线），像循环/移动/其它客户端事件 | 低（只知"不是受伤"） |
| 1 / 2 / 17 / 20 | 4–85 | 0.000–0.048 | 0–5 | 0 | 样本过少，不下结论；其中 upward 的 event 20 只有 5 条（5/5 同时 hurt+death），**可疑未定** | — |

**9–14 合起来**：n=2722，同 tick 受害者命中 **2621（96.3%）**，101 条未命中**全部**是因为该 tick 的受害者 userid 不在身份表里（身份表只覆盖死过的玩家），**反例 0**（`a2-exact2.out`）。**event 0 合起来**：在 988 条"同 tick 有人受伤"的样本上攻击者命中 **814（82.4%）**，174 条未命中里 125 条同样可用覆盖缺口解释，**反例 49（5.0%）**（`a2-event0.out`）。

**能说的**：9–14 是"同 tick 受伤、且受伤者就是该实体"的强耦合家族（100% / 96.3%）；0 是攻击侧。**不能说的**：9–14 之间如何区分（六个值同签名，可能是 hitgroup/方向/武器类别的不同反应，本仓库无验证路径）；0 是否等于 TF2 的 `PLAYERANIMEVENT_ATTACK_PRIMARY`（名字相同只是类比，不是证据）；6 到底是什么（无耦合，只能排除"受伤"）。


### 4.8 A2 强化：索引与"某个具体玩家"的统计绑定（不依赖花名册）

§4.3 的 overlay 检定判别力弱（它只列玩家、索引 1..20 连续）。换一个不依赖花名册的判据：**如果是"任意玩家槽位"，窗口内受伤者命中率应当 ≈ 1/玩家数**。

| demo | 窗口内 distinct entity（census identity） | 零假设 1/N | event 9–14 实测 victimShare | 倍数 | event 0 实测 attackerShare | 倍数 |
|---|---|---|---|---|---|---|
| snakewater | 12 | 0.083 | 0.64–0.80 | 7.7–9.6× | 0.52 | 6.3× |
| upward | 18 | 0.056 | 0.58–0.73 | 10.4–13.1× | 0.58 | 10.4× |

（受害占比来自 §4.7 表；零假设用该 demo 的 distinct entity 数，最大 20 玩家槽位时 1/20=0.05，结论不变。）

**这条把 A2 从"未证明"推进到"有统计证据"**：解出的索引与**某个具体玩家**的受伤/攻击强相关，不是随便落在一个槽位上。**更硬的同 tick 精确到人检定见 §4.8.1（96.3%，0 反例）**，本节窗口数字可与之并列但不能替代它。**仍未被证明的两件事**：① 只在 9–14 与 0 上成立，3/4/5/6 等无耦合值上无法检验；② 「索引 = 发这个动画的实体」这一语义仍可能错——协议字段名是 `m_hPlayer`（"动画作用于谁"），不保证"谁触发了这条消息"，两者在多数动作上重合、在他人引发的动作（如被击中）上可能指向受害者而非攻击者，本轮无法分辨。

### 4.8.1 精确到人：同 tick 的受害者/攻击者身份（最硬的一条）

上面的窗口统计只能说"与某个具体玩家强相关"。域对齐 + off-by-one 修正（§3.7.1）后，可以做**同 tick 精确到人**的检定：对每条样本取它的播放 tick，直接看该 tick 的 `player_hurt`，用 §5 的身份表把 `userid`（受伤者）/ `attacker`（凶手）翻成 entindex，问"这个 entindex 是不是样本里那个 entity"。

脚本 `D:/_shared/t85-anim/a2-exact.py` / `a2-exact2.py`（输入 census4 的 `animEventSample` + `events-*.log`，**不依赖锁、可离线复算**），输出 `a2-exact.out` / `a2-exact2.out`。**口径**：本节先用离线脚本算（与 §3.7.1 同一套 +1 修正），随后 recon5 的原生复跑逐项验证了同一批数字（§3.7.2，三 demo exactHurt 全等于本节表）；引用时请引 `census5-*.log` 的原生行 `animEventExactFollow`，不要引离线脚本输出。

| demo | event | n | 同 tick 是受伤者 | 未命中 | 未命中里受害者 userid 不在身份表 | 反例 |
|---|---|---|---|---|---|---|
| snakewater | 9 | 545 | 477 | 68 | 68 | **0** |
| snakewater | 10 | 109 | 107 | 2 | 2 | **0** |
| snakewater | 11 | 132 | 129 | 3 | 3 | **0** |
| snakewater | 12 | 79 | 68 | 11 | 11 | **0** |
| snakewater | 13 | 86 | 73 | 13 | 13 | **0** |
| snakewater | 14 | 141 | 137 | 4 | 4 | **0** |
| upward | 9–14 | 446 | **446** | 0 | 0 | 0 |
| bagel13 | 9–14 | 1184 | **1184** | 0 | 0 | 0 |
| **合计** | 9–14 | **2722** | **2621（96.3%）** | 101 | **101（全部）** | **0** |

event 0 走攻击侧（`a2-event0.py` → `a2-event0.out`），只统计"同 tick 确实有人受伤"的样本：

| demo | 同 tick 有 hurt | 该 entity 是攻击者 | 未命中 | 未命中里攻击者 userid 不在身份表 | 反例 |
|---|---|---|---|---|---|
| snakewater | 357 | 235 | 122 | 119 | 3 |
| upward | 210 | 170 | 40 | 5 | 35 |
| bagel13 | 421 | 409 | 12 | 1 | 11 |
| **合计** | **988** | **814（82.4%）** | 174 | 125 | **49（5.0%）** |

判读：

- **event 9–14：entity 就是同 tick 那个受伤的玩家**（96.3% 精确命中，0 反例）。这不是"落在玩家槽位区间"，而是"点名到人"。
- 101 条未命中**全部**可用"该受伤者的 userid 从未在本 demo 的 player_death 里出现"解释（身份表只覆盖死过的玩家；迟入场/整场存活的玩家没有映射）。**没有一条反例**说明索引指向别人。
- **event 0：entity 是攻击者**（82.4%），但强度明显低一档：49 条反例（5.0%）——这些 tick 上有人受伤，但该实体既不是受害者也不是凶手（可能是开火没打中、或攻击者是环境/世界）。
- **语义边界**：9–14 上"动画作用于受害者"与"消息由受害者自己发出（客户端预测受伤反应）"无法区分；`m_hPlayer` 的字面义是"动画作用于谁"，与这批证据一致，但**不能**据此宣称"谁触发了消息"。

**更新 A2 的结论**：A2（解出的索引 = 发动画的玩家）从"弱证据"升为**强证据（对 9–14 与 0 这两个有耦合的事件家族）**；仍不能外推到 3/4/5/6 等无耦合值（那里没有可对照的动作，检验不到）。

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

判读：**在抽样的 36 个 tick 上，「entity ∈ 2..19 ⇒ 该索引是一个玩家」成立**（0 反例）；但这只是"是某个玩家"，仍不回答"是哪个玩家"——后半问由 §4.8 的受害者/攻击者统计回答（0.58–0.80 vs 零假设 0.05–0.08）。两条合起来（叠上 §4.8.1 的同 tick 精确检定 96.3%/0 反例）：**索引就是那个具体玩家，且在 9–14 上是'受伤者'、在 0 上是'攻击者'；但「m_hPlayer 的语义是'动画作用于谁'还是'谁触发'」仍无法从 demo 分辨**——9–14 的受害者自己完全可能是消息的发出方（客户端预测受击反应）。

边界（别外推）：① 花名册只包含"该 tick 有 origin 属性"的实体（`entity_model_probe.cpp:959` 只打 `isOriginProperty`），没有 origin 的实体不会出现，所以"非玩家不占 1..19"只在**有 origin 的实体集合**内成立；② 只抽了 36 个 tick，不是全场；③ 每个 tick 只打一次快照（checkpoint），不是逐 tick 连续验证。

## 5. 身份：demo 级 userid → entindex 表（经验表，不是协议事实）

从 player_death 自带的 userid（死者）与 victim_entindex 反推，两个 demo 内**完全一致**（每次死亡都落在同一映射上）：

- snakewater（109 次死亡，11 个 userid）：3→2、4→3、6→4、7→5、9→6、10→7、12→9、13→10、14→11、17→8、18→13
- upward（248 次死亡，18 个 userid）：3→2、5→4、6→5、7→6、8→7、9→8、10→9、11→10、12→3、13→11、14→12、15→13、16→14、18→16、19→17、20→18、21→19、22→15

能说的：本 demo 内这张表稳定，说明 victim_entindex 是真实实体索引、userid 是另一编号空间。不能说的：它不是协议规定映射（userinfo 未解码，native/src/review_bookmarks.cpp:321 缺身份写 no-userinfo），跨 demo 也不通用（两表不同）。

### 5.1 表的独立交叉印证：拿 entity_killed 对照（离线，`a2-check.py` → `a2-check.out`）

`player_death` 只有 userid 形式，但同一 tick 还有 `entity_killed` 事件，它直接给 `entindex_killed` / `entindex_attacker`（实测字段名，见 `events-*.log`）。用它做两条独立检验：

| demo | 死亡 tick | victim_entindex == entindex_killed | 攻击者 userid→entindex 可映射并对上 entindex_attacker | 映射不上（攻击者没死过） |
|---|---|---|---|---|
| snakewater | 109 | 108/109 | 86/87（99%） | 22 |
| upward | 247 | 242/247 | 242/245（99%） | 2 |
| bagel13 | 168 | 167/168 | 168/168（100%） | 0 |
| **合计** | **524** | **517/524（98.7%）** | **496/500（99.2%）** | 24 |

7 条 entindex_killed 不一致全部出现在**同一 tick 有多次死亡**的行上（脚本按 tick 配对，多对多无法一一对应）；upward 有 1 行死亡没有对应的 entity_killed 行。这不影响结论：**§5 的 userid→entindex 表被两种互相独立的事件类型同时印证**（98.7% / 99.2%），因此 §4.8.1 里用它做的"姓名到人"判定有可信基础。

仍不能说：这张表仍然是**本 demo 的经验表**（要靠死亡事件才能建立，没死过的玩家缺失——这正是 §4.8.1 那 101 条未命中的来源）。

## 6. 假设清单（每条写着"要什么才能升级"）

| # | 假设 | 现状 | 升级需要什么 |
|---|---|---|---|
| A1 | player 字段是 CBaseHandle | 强证据（§4.4） | 无需再验；若要更强，用槽位复用导致 serial 变化的样本 |
| A2 | 解出的索引 = 发动画的玩家 | **强证据（域对齐后）**：§4.8.1 同 tick 精确到人——9–14 该索引是受伤者 2621/2722=96.3%（101 条未命中全部因身份表缺 userid，**0 反例**）；event 0 是攻击者 814/988=82.4%（49 反例=5.0%）。§4.9 花名册 44/44 命中 CTFPlayer。§4.3 的 overlay 检定判别力弱，只作旁证 | 只剩语义问题：无法区分「动画作用于谁」与「谁触发消息」；要分辨需第三人称画面逐帧或引擎侧知识，本仓库无验证路径 |
| A3 | 低 11 位掩码 | **事实**（仓库既有，P1） | — |
| A4 | m_iEvent 的值语义（哪个是 attack/hurt/die） | **实证更强**：9–14 = 受伤反应家族（**3/3 demo** 上 exactHurt==n；§3.7.1），实体是受伤者；0 = 攻击侧（实体是攻击者，§3.7.1）；**die/taunt/reload 仍无任何值被证明**；无权威枚举名；upward event 20（n=5）可疑未定 | 需外部真值（客户端 CRC / 开火音频 / 逐 tick 画面）逐值标注；本仓库内无路径 |
| A5 | event tick 能对齐到动画帧 | 域换算已建立，并在 2/3 demo 上被「100% 同 tick hurt」反证；**未做动画帧级对齐** | 用已知受伤/死亡时刻与 trace 的 anim: 行逐帧对照 |
| A11 | 常量偏移不成立，必须用 packetServerTicks 反查（bagel13 实证） | **已验证（含原生复跑）**：§3.6 定位漂移；§3.7.1 修 +1；§3.7.2 recon5 原生：三 demo 表 100% 命中、`tableAmbiguous=0`、`stalls=0`，bagel13 `derivedMismatch=7754/7819`、平均差 4299.95 tick。9–14 的 exactHurt 在原生读数上仍全等于 n | 剩余：把「常量偏移」从代码/文档彻底移除（本分支已无此路径） |
| A6 | 同 entityIndex 重生继承旧动画状态 | 代码显示会（native/src/main.cpp:3477 按 entityIndex 键，只在倒带时 clear :9337-9343） | 跑含重生的 demo，抓 trace anim: state 看第一帧 speed/state |
| A7 | 缓存前缀覆盖的播放时长 | **未知**（只知前 16384 条临时实体事件） | 探针输出缓存首/末事件 tick |
| A8 | 跨 demo 相同 raw 句柄值 | 异常（§4.4） | 拉第三个 demo 对照；若也相同更像确定性重连/固定 bot 名册 |
| A9 | m_iHealth 每个 damage tick 都进 delta 快照 | 未验证（flinch 依赖它） | 属性直方图探针 + 对齐 player_hurt tick |
| A10 | userinfo 可由 string table 解出 | 未实现（native/src/demo_header_strtab.cpp:277-290 只记字节数） | 单独任务：解 svc_CreateStringTable 的 userinfo 结构 |

## 7. 对动作表的影响

- **能升级的格子**：death/respawn、attack 的"协议侧证据"从"未验证"升到"event 小枚举 + 值分布实测"；但两条都**不是**"能驱动动画"。
- **能升级但仍须标推断**：flinch/hurt 行可写「CTEPlayerAnimEvent 9–14 在反查表口径下与该实体受伤强耦合（统计推断，**3/3 demo** 同 tick 100%，§3.7.1），实体即受伤者」，但**不得**写成 TF2 的 PLAYERANIMEVENT_* 枚举名；attack 行可写 event 0 呈攻击侧（实体=攻击者，3/3 demo）。
- **仍不能升级**：这些值只说明「服务器给客户端发了这个实体的一条动画事件、且同 tick 该实体在受伤/攻击」，**没有**证明产品会据此播哪个 clip（产品当前根本不消费临时实体事件）。
- **不能升级的格子**：die/taunt/reload/deploy 仍没有任何 event 值被证明，任何「值 X = 动作 Y」的断言在 A4 获得外部真值前都不许写。
- **新增可写事实**：两套 tick 域与换算；缓存前缀截断；句柄结构实测；demo 级身份表。

## 8. 未验证边界（别外推）

- 没有跑任何门禁脚本（animation-drive-states-check.sh 等），没有启动产品、没有抓帧。
- 没有解析 userinfo。
- 常量偏移式域换算**在所有 demo 上都不可靠**（bagel13 已证），只能按 `packetServerTicks` 反查；v1/v2 日志里的 exactHurt/exactDeath 都是常量偏移口径，**不得引用**。
- §3.7.1 的 100% 已被 recon5 原生复跑确认（§3.7.2）；本文件其余仍由常量偏移口径生成的表（§2、§3.5 第三行）保留作历史对照，**不可引用**。
- 全实体花名册检定（§4.8）只覆盖少量 tick，不是全量。
- fieldHits 是字段个数总和，不是命中率。
- 三个 demo 的 cached 都满，因此**全部结论只覆盖每个 demo 的前缀**，不能外推为整场统计。

## 9. 8.3-2 UNANCHORED：模型指纹锚点（给 debt-claim）

需求：产品侧动画/武器挂载的欠账要有一个**可复查的锚点**，不能只写「sniper 模型」。做法是让探针把 `modelPrecache` 的每一项算内容哈希，并（修复版）顺带解 studiohdr 的 `mdlName`/`checksum`/`numbones`/`sequences`/`rootBone`。

已落地（`entity_model_probe --model-fingerprints [filter]`，t87 副本）：遍历 `summary.modelPrecache`，用 `readAssetBytes` 取原始 VPK 字节算 SHA256；`mdlName`/`checksum`/`bones` 按 studiohdr 定偏移直读（name@12、checksum@8、numbones@156），只有 `models/player/` 前缀的条目才走 `ModelLoader::resolveAsset + inspect` 补 `sequences/rootBone`（全量 inspect 太慢）。输出行：`model-fingerprint idx= path= bytes= sha256= mdlName= checksum= bones= sequences= rootBone=`。

**七个职业模型的内容锚点（三 demo 实测一致，可作 debt-claim 的锚）**：

| 模型（`mdlName`） | bytes | SHA256（内容哈希，跨 demo 相同） | checksum（studiohdr@8） | bones（@156） | precache idx（snakewater / upward / bagel13） |
|---|---|---|---|---|---|
| player/sniper.mdl | 1194680 | be405f065d9db840fb1de1f35fbfac3f33921d0acf2d576529b014bd632edb19 | 3495254885 | 74 | 880 / 889 / 1039 |
| player/scout.mdl | 611668 | 603c8fc56450b383ccb5952420d850044a6eff98e0df810171ffa5182d76f478 | 1098120898 | 78 | 973 / 982 / 1150 |
| player/soldier.mdl | 1053984 | 62bc48c0fa4ac4166151633087feadef103a2d86d4412639fe1adddba91de219 | 3748736120 | 86 | 993 / 1002 / 1187 |
| player/demo.mdl | 1244388 | b8fe45619b062197a975798310d703ec59f8c1ab53f901c462eed5e0fd34ef93 | 1505337062 | 84 | 1003 / 1012 / 1205 |
| player/medic.mdl | 964852 | 485dd564325839863d9d9593e9beb1d64ca24bb59576240ac5ce6ccf05196d04 | 1029183977 | 92 | 1011 / 1020 / 1219 |
| player/heavy.mdl | 901264 | 503258d6009595bcdfa6bf518956b7a518b916f98d9447ffe540d7e84741db54 | 1641026357 | 79 | 1021 / 1030 / 1237 |
| player/pyro.mdl | 102420 | bcd17a24d213e7946deb3a0773bc626774aca295310d822971426b8843ddde89 | 3846592064 | 76 | 1030 / 1039 / 1253 |
| player/spy.mdl | 977520 | 772b9e751f09f7bbc576e3193050f2ee5d72b13c67050fe4ced7ab1720029c17 | 4261541971 | 84 | 1040 / 1049 / 1271 |
| player/engineer.mdl | 922680 | 533049f35bf1e7310e55ac9b0d8e60f689d0ddd671befdad4021fe6863f283e7 | 1880291295 | 73 | 1049 / 1058 / 1287 |

要点：**precache 索引每个 demo 不同（同一模型可差 150+），SHA256/checksum/bones 三列相同** —— 所以欠账/回归必须锚在内容哈希（或 `mdlName`+`checksum`），不能锚在索引上。数据来源：`fingerprints2-{snakewater,upward,bagel13}.log`（recon5 修复版；recon4 的 `fingerprints-*.log` 只作留证，其 `mdlName=-/bones=0` 是 lookup bug）。

**已拿到（recon5，`fingerprints2-*.log`）**：每个玩家模型的 `mdlName`/`checksum`/`bones`/SHA256 全部有值且三 demo 一致，例：`idx=880 path=models/player/sniper.mdl bytes=1194680 sha256=be405f06… mdlName=player/sniper.mdl checksum=3495254885 bones=74`。同一次运行里 9 个职业模型都齐了（sniper/scout/soldier/demo/medic/heavy/pyro/spy/engineer），`filter=models/player/` 的 summary：snakewater `entries=1193 shown=124 hashed=124 missing=0`、upward `1211/125/125/0`、bagel13 `1430/194/194/0`。

**仍未拿到（诚实标注）**：`sequences` 一律为 0、`rootBone` 一律为 `-`。原因在代码里看得到：`ModelLoader::inspect(const ModelResourcePaths&)`（`native/src/model_loader.cpp:1393-1397`）用 `readFile(paths.mdl)` 从**文件系统**读字节，而玩家模型只存在于 VPK（早前已实测 `tf/models/player/` 下没有散文件、`pak01_dir.vpk` 里 grep 不到该路径），所以 inspect 拿不到字节、metadata 为空，代码退回 studiohdr 直读的名字/checksum/骨数（`native/tools/entity_model_probe.cpp:1642-1663`）。要补 rig 名有两条路：改用 `ModelLoader::inspectVpk`（`native/src/model_loader.cpp:1399-1424`，需要 VpkArchive 句柄），或直接按 studiohdr 的 `boneindex`（偏移 160）读第 0 根骨的 `sznameindex`。**本轮没做**，所以模型锚点只到「SHA256 + mdlName + checksum + 骨数」这一层；`rootBone` 不作为证据使用。

一个旁证：TF2 的 player 模型不在 `tf/*.vpk` 的目录树里能搜到（`grep -a models/player/sniper.mdl *_dir.vpk` 为空、`tf/models/player/` 无散文件），说明它们由 `ModelLoader::resolveAsset` 从自定义搜索路径/其他包解析；**这也解释了为什么不能用「文件系统路径」当锚点，只能用探针读到的字节哈希**。

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

    6) t87 分支复现 141 行 + 扩展（D1/D1b/指纹，全部在 hostlock 下）
    cd /d/TF2_Native_Worktrees/t87-anim
    git show HEAD:native/tools/model_pose_probe.cpp > /tmp/blob.cpp      # sha256 b7ed13b0…
    git apply /d/_shared/t85-anim/model_pose_probe-141line.patch         # -> 0eb9820c…
    bash build-target.sh model_pose_probe
    bash build-target.sh entity_model_probe
    bash /d/_shared/t85-anim/recon5.sh        # 本文件 §3.7.1/§9 的读数来源

    7) 离线重算（不占锁）
    python offline-exactfollow2.py            # row+firstDataEntryTick 后的 exactHurt
    python baseline.py                        # 每 tick 受伤/死亡基线
    python model-anchor.py                    # 职业模型 bytes/sha256/idx

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
| recon3.sh / recon3-run.log / events-bagel13.log / census3-*.log | bagel13 域漂移定位（§3.6） |
| recon4.sh / recon4-run.log / census4-*.log / fingerprints-*.log | 反查表重跑（§3.7.1 表的上游数据）与作废的指纹读数 |
| recon5.sh / recon5-run.log / census5-*.log / fingerprints2-*.log | 修复 row→entry 与指纹后的**权威读数**（§3.7.2、§9 引用的就是这些） |
| offline-exactfollow2.py / .out | 离线 +firstDataEntryTick 重算（§3.7.1 表） |
| baseline.py / baseline.out | 每 tick 受伤/死亡基线（§3.7.1 巧合基线） |
| model-anchor.py / model-anchor.out | 职业模型 bytes/SHA256/precache idx（§9） |
| vpk.py / vpk-hdr.py / vpk-dbg.py / vpk-bytes.py | 尝试离线解 VPK 取模型字节（未走通：player 模型不在 tf/*_dir.vpk 目录树里），仅留证 |

探针 exe：model_pose_probe.exe d93022ad…；demo_open_probe.exe 2d56a61d…（脚本运行时打印）；entity_overlay_probe.exe（t79 预构建）7baacb60…。
