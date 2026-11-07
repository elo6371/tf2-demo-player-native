# T85 动画动作表（冻结候选 e15577b 实测口径）

分支 `task/t87-anim`，基线 `e15577b`（`integration/t79-final`）。本文回答一个问题：
**一个动作在 demo 里到底由什么可见信号驱动，产品（e15577b）现在做到哪一步，哪里只是假设。**

## 0. 一句话结论

- 产品动画层只有**五个**状态：`idle`、`moving`、`crouch`、`crouchwalk`、`flinch`（`native/include/animation_clock.h:43` 只有 `Idle/Moving` 两个驱动态，其余三层在 `native/src/main.cpp:9939-10023` 叠加）。
- **jump / air / fall / land / turn / attack / reload / deploy / taunt / death / respawn 全部未实现**，不是"做得差"，是代码里没有这条路径。
- demo 协议能给的是：每个玩家的 **server 属性**（血量、flags、蹲伏三件套、生命状态、原点、手持武器句柄）、**CTEPlayerAnimEvent**（一个 `m_hPlayer` 句柄 + 一个 `m_iEvent` 整数，没有别的）、以及 **game event**（`player_death`/`player_hurt`/`rocket_jump` 等）。协议**不给**的是：事件值到"攻击/受伤/死亡"的权威映射、每个 tick 的速度/着地保证、userid↔entindex 的身份表（userinfo 未解码）。
- **两套 tick 域不要混用**（本轮实测证据见 `docs/T85-ANIMATION-RECON.md` §3）：temp entity 的 tick 是 `svc_TickMsg` 的绝对服务器 tick（`native/src/demo_header.cpp:143,162` → `native/src/demo_header_events.cpp:312`），game event 的 tick 是 cmd=2 包的播放域 tick（`demo_header_events.cpp:139-140,162`）。换算实测：`server = playback + (serverTickOrigin - firstRecordedPacketTick)`，snakewater 10320-1=10319（74671-64352=10319 ✓）、upward 11057-1=11056（96260-85204=11056 ✓）。
- 本轮实测（3 个不同地图的 demo、`--player-animation-event-census`）：解析到 6321/4265/7819 条可用事件，`m_iEvent` 实测 18/20/18 种取值。**`player & 0x7ff` 本身不是假设**：11 位索引布局是 `native/src/entity_model.cpp:140-148,638` 已经在用的事实（解 `m_hActiveWeapon`）；实测 96 条样本的 `player` 原始值都是百万级（如 1886218 = 索引 10 + serial 921），`player%2048` 恒等于打印出的 entity（96/96），同一索引的 serial 跨 tick 不变（0 冲突，census2 全量 6321/4265/7819 条也是 0）——「player 字段是打包的 CBaseHandle」有实测支撑。**仍然无法用 `userid` 去判它错**（两个编号空间）。**没被证明的是最后一步**：解出的索引是否就是「正在发这个动画的那个玩家」——overlay 只列玩家、索引 1..20 连续，判别力弱（RECON §4.3）；本轮另加的全实体花名册检定见 RECON §4.9（36 个 tick 上 `entity ∈ 2..19` 全部是 CTFPlayer，0 反例），再加上 §4.8 的受害者/攻击者统计（0.58–0.80 vs 零假设 0.05–0.08），A2 已从「未证明」推进到「索引是某个具体玩家且与动作强相关」；仍不能分辨 `m_hPlayer` 是「动画作用于谁」还是「谁触发」。
- **v1 的跨域关联计数（exactDeath/exactHurt）无效，D1 已在 t87 副本中修正并重跑**：v1 拿 temp entity 的服务器 tick 直接和 game event 的播放域 tick 比相等（snakewater 差 10319、upward 差 11056），读数紧贴巧合期望（201 vs 210.6 等）。换算后重跑：**snakewater/upward 的 event 9–14 全部 `exactHurt == n`（100%）、hurt lift 6.2–7.7×，窗口内该实体是受伤者的比例 0.58–0.80；event 0 呈攻击侧（攻击者占比 0.52/0.58）**。但 **bagel13 一条都没复现**（同值 2.3–3.9%，贴基线），候选原因是常量偏移在 bagel13 不成立（应改用 `packetServerTicks` 查表，RECON §3.6）——所以「换算普适」未成立。e15577b 不拿 CTEPlayerAnimEvent 做枚举，结论仍成立，理由是**没有权威映射 + 值→动作的语义只能统计推断**，不是"userid 对不上"。
- 证据分级见 §6；本文所有数字都出自 §5 的 census 输出与 §5.3 的抽样核对，未实测的项目一律写"未验证"。

## 1. 口径

- "产品策略"只指第三人称角色驱动的 `--animation-drive states` 路径（`native/src/main.cpp:9939` 起）。第一人称 viewmodel 是另一条管线（`viewmodel-*` 门禁），不在本表。
- "可靠输入"必须是**协议上真的存在**的东西，且写明是从哪条消息/哪个属性读到的；凡是"我认为 TF2 应该是这样"的，一律归到 §7 假设清单。
- "证据状态"用 §6 的 A/B/C/D 四级。

## 2. 动作表

| 动作 | 产品策略（e15577b） | 可靠输入（wire 字段 / event / flags） | 缺失时的退化行为 | 证据状态 | 无法验证的边界 |
|---|---|---|---|---|---|
| **idle** | 默认态。时钟 `idleRate=0`，cycle **冻结**而不是减速（`animation_clock.h:22-24,62`）。序列用 `kIdleNeedles={"idle","stand"}` 的 **Substring** 匹配、取第一个 animated（帧数>1）候选（`animation_sequence_match.h:141,101-115`）。 | server 属性 `m_vecOrigin`：相邻 scene tick 的三维原点位移算出的 speed < `movingSpeedThreshold=0.5` u/s（`animation_clock.h:52`）。**没有任何 wire 字段直接说"idle"**。 | 找不到 idle 序列 → `chosen=-1` → `++playerAnimSkipped`，该实体这一帧不出动画请求（保留 bind pose 以外的既有姿态）（`main.cpp:10070-10083`）。reload 家族被 `sequenceLabelIsOneShot` 主动排除（`animation_sequence_match.h:161-163`）。 | C：代码路径；`animation-drive-states-check.sh` 有 states/legacy 同帧断言（本轮**未复跑**）；census 与 idle 无关，未直接测。 | idle 侧仍是**子串**匹配（T61.1 只改了 moving 侧），"stand" 会命中 `AttackStand_PRIMARY` 这类标签。`animation_sequence_match.h:14-19` 明确记录这个已知缺陷归另一个任务，本轮未测。 |
| **walk / run** | `moving` 态。rate = `clamp(speed/250, 0.05, 3.0)` cycles/s（`animation_clock.h:53-60`）。序列用 `kMovingNeedles={"run","walk","move"}` 的 **WholeToken** 匹配 + 首 token 优先（`animation_sequence_match.h:139,144-150`）；九职业里八类落 `run_PRIMARY`、Spy 落 `run_SECONDARY`（同文件 :36-38）。 | 同上：`m_vecOrigin` 相邻 tick 位移 → speed；位移含 z，所以**跑和跳在下落/上升里是同一个信号**（`main.cpp:9841-9850`）。 | 层级阶梯 animatedPrimary→animated→posedPrimary→posed→fallbackPrimary→fallback（`animation_sequence_match.h:167-173`）；全落空 → skipped。 | C：代码路径 + T61.1 离线实测（记录在头文件注释，非本轮复测）；门禁 `player-animation-check.sh` 未复跑。 | 传送/重生第一帧的原点跳变会被算成极高速度（rate 被 clamp 到 3.0，但状态是 moving）；本轮未测。 |
| **turn（原地转身）** | **未实现**。没有任何"转身"状态，也没有角度→cycle 的映射：原点不动 = idle，cycle 冻结。 | 无被使用的输入。动画层读的信号只有 flags / ducked / ducking / ducktime / health / lifeState 六个（`main.cpp:9360-9415` PlayerDriveSignals），**没有任何朝向字段**。 | 原地转身期间播 idle 并冻结 cycle，看起来像站桩。 | C：代码事实（信号结构 + 无 turn 分支）。 | TF2 玩家实体是否在快照里带 `m_angEyeAngles`/`m_angRotation`，本轮**未验证**（没有跑属性直方图探针）。 |
| **crouch / crouchwalk** | `crouched = (m_fFlags & 0x2) || m_bDucked || m_bDucking || m_flDucktime>0`（`main.cpp:9961-9965`）。moving+crouched → 报 `crouchwalk`；静止+crouched → `crouch`；两者都要求 `alive`（`:9987`）。 | `m_fFlags` bit1（FL_DUCKING，值 2）、`m_bDucked`、`m_bDucking`、`m_flDucktime` —— 全是 CTFPlayer 的 server 属性。OR 不是保险丝：P2-1 census 实测 `m_fFlags=131329`（bit1 为 0）时 `m_bDucking=1`、`m_flDucktime=940`（`main.cpp:9927-9930`）。 | 六职业（scout/soldier/demo/engineer/spy/sniper）没有 crouch locomotion 家族，只有 `AttackCrouch_*` 和单帧 `PRIMARY_aimmatrix_crouch_idle`：moving 时仍报 `crouchwalk`，但 reason=`no-crouch-locomotion`、实际播单帧蹲姿（cycle 不动）（`animation_sequence_match.h:329-338`；`main.cpp:9995-10023`）。整体无 crouch clip → `++playerAnimFallback`。 | C：代码路径；门禁 `animation-drive-states-check.sh` 有 crouch 与 same-state 同 tick 断言（未复跑）；四个字段的存在性来自 P2-1 census 报告（`evidence/t62-p2-server-anim/report.tsv`），本轮**未复跑**。 | 六职业的"蹲走"视觉上等于静止蹲姿：状态名 `crouchwalk` 是**服务器状态**的命名，不承诺有走路循环（`main.cpp:9988-9993` 注释已写死这一点）。 |
| **jump** | **未实现**。跳跃期间原点上升 → speed>0 → `moving` → 播跑的循环（"空中跑"）。 | `m_fFlags` 的 **bit0（FL_ONGROUND，值 1）** 就在已读取的同一个字段里，但产品只用 `& 0x2`（`main.cpp:9961`，全文件唯一一处 flags 位测试）。game event `rocket_jump`/`rocket_jump_landed`/`sticky_jump`/`sticky_jump_landed` 实测存在（snakewater 891/366/57/56 次），但动画层**不消费任何 game event**（`main.cpp:3310` 只把 events 交给 review workspace）。 | 无专门 clip：chooser 里没有 jump/air 谓词，即使给了信号也没有可播序列。 | C（代码）+ B（事件计数实测）。 | 没有任何 air/jump 序列被挑选过；`airwalk` 家族存在但 T61.1 明确把它排除在 moving 匹配之外（`animation_sequence_match.h:5-9`）。 |
| **air / fall** | **未实现**，且**上升与下落不可区分**：唯一的速度信号是三维位移的标量速度。 | 可用但未用：`m_fFlags` bit0（FL_ONGROUND）、game event `rocket_jump` 等。产品的 PlayerDriveSignals 不含速度/着地字段。 | 空中保持跑循环，落地前一帧仍是跑循环。 | C：代码事实。 | 着地状态如果不在 server 属性的 flags 里逐 tick 刷新，"离地"就不可知；本轮未验证 flags 的更新频率。 |
| **land** | **未实现**。落地没有任何事件驱动：speed 骤降 → 下一个 scene tick 变成 idle，cycle 从跑步循环直接跳回 idle 第 0 帧。 | 可用但未用：`rocket_jump_landed`=366、`sticky_jump_landed`=56（snakewater 实测）；或 FL_ONGROUND 由 0→1 的边沿。 | 无缓冲、无落地动作层。 | C + B。 | game event 的 tick 是**播放域**（`demo_header_events.cpp:139-140,162`），与 scene tick 同域（尚未逐帧验证）；temp entity 的 tick 是服务器绝对 tick，两者差 10319/11056（RECON §3）。 |
| **flinch / hurt** | 相邻两个**新** scene tick 上 `m_iHealth` **下降** 且 `m_lifeState==0`，且该模型有 flinch clip、且当前没有一次性层在跑 → 起一层，长度 `flinchTicks`（`a_flinch01`=13 帧），cycle = elapsed/oneShotTicks 线性推进，reason=`health-drop`（`main.cpp:9948-9985`）。 | `m_iHealth`（下降边沿）+ `m_lifeState`（==0 才算活体）。两者都是 server 属性。 | 五个职业（heavy/engineer/medic/sniper/spy）没有 flinch 家族：`choice.flinch<0` → 层根本不起（`:9950` 的条件含着 `choice.flinch>=0`），也不计 fallback，直接停在 idle/moving。 | C：代码路径；门禁 `animation-drive-states-check.sh` 有 flinch tick 断言（未复跑）；census 实测 `player_hurt` 2144/3494/2191 次，说明"若改用 game event 有足够的信号量"，但产品没用。**更贴近动画层的可用信号**：CTEPlayerAnimEvent 的 9–14 在域对齐后与受伤强耦合（2/3 demo exactHurt==n、lift 6.2–7.7×，RECON §3.5/§4.7），产品同样完全不消费。 | 同一 tick 多次掉血只算一次（`newTick` 门）；血量上升（重生/治疗）不触发；`m_iHealth` 是否每 damage tick 都进 delta 快照，本轮未验证。 |
| **death / respawn** | **不播死亡动画**。构建 drawCandidates 时先剔除相机所在玩家，再剔除 `!g_spectateShowCorpses && hidesModel(entityIndex)` 的尸体（`main.cpp:9522-9557`，注释 `:9523-9529` 说明"剔除而不是循环内 skip"，因为只有剔除可计数、才可作证据）。trace 行 `scene: corpseFilter ...`。 | `player_death` game event（实测 109/248/168 次，带 `userid`/`victim_entindex`）+ `m_lifeState!=0`。动画层**不消费**这两者；它只是不再画这个实体。 | 死亡 = 模型消失（无 ragdoll、无死亡姿势）；重生 = 实体重新出现，直接回到 idle/moving。 | C + B。 | **同 entityIndex 重生不清状态**：`playerAnimationRuntimes` 按 entityIndex 键（`main.cpp:3477`），只在倒带（`sceneTick < lastObservedTick`）时 clear（`:9337-9343`）。重生第一帧会继承旧 clock/lastOrigin/lastHealth → 原点跳变、大 speed、可能的误 moving。本轮**未实测**。 |
| **taunt** | **未实现**。 | 无。实测 snakewater 的 62 个 game event 名里**没有** taunt 事件（§5.2 清单）；temp entity 的 event 值也没有被映射。 | 嘲讽期间角色停在 idle/moving，无任何变化。 | B（事件名清单实测）+ C。 | TF2 服务器侧是否有嘲讽的具体属性/事件：未验证；本 demo 集合未出现。 |
| **attack** | **未实现**。武器挂载（`--player-weapon-attach on`）只画世界模型；`attachDraw` 从不设 `boneOffset/boneCount/skinningEnabled`（`main.cpp:10239-10264`）→ 手里的武器是 bind pose，既没有开火动画也没有换弹动画。 | 名义上最像的是 CTEPlayerAnimEvent 的 event 值，但 census 的关联计数**跨 tick 域**（temp entity=服务器 tick，game event=播放域 tick），按数值相等比出来的 exactHurt 最高 54/1296（4.2%）、exactDeath 最高 9/1296 只能当巧合量级的读数，**不能**用来证明或否证某个 event 值对应开火（RECON §3）。game event `player_hurt` 是"命中结果"不是"开火动作"。域对齐已在 t87 副本里做了（RECON §3.5）：event 0 在 2/3 demo 上呈攻击侧（窗口内 hurt 的 attacker 占比 0.52/0.58），9–14 呈受伤侧；但**没有任何值被证明是「开火/攻击动作」**，也没有 clip 可播。 | 不开火动画；第一人称 viewmodel 走另一条管线。 | A/B：本轮实测（census 3 demo）+ 代码。e15577b 注释 `main.cpp:9932-9938` 记录过同一结论（109 次死亡 vs 545 次"die-sized"候选），本轮**独立复现 109 与 545**；但那条注释里"player 字段从不等于 userid"的论证**不能否证该字段**（userid 与 entindex 本不同空间），结论要靠"没有权威映射 + 域对齐后仍无相关性"支撑。 | 要证明某个 event 值是 attack，需要动作侧的 ground truth（例如音频/第三人称 viewmodel 的逐 tick 对照），本轮没做。 |
| **reload / deploy** | **未实现**，而且被**主动排除**在循环选择之外：`sequenceLabelIsOneShot` 让两个 picker 都跳过 reload 家族（`animation_sequence_match.h:152-163`）。原因写在注释里：`ReloadStand_PRIMARY` 里 "stand" 既是整 token 又是子串，会把换弹当成 idle —— Pyro 曾因此全身 bind pose。 | 可用但未用：`post_inventory_application`=251、`weapon_equipped`=66（snakewater 实测）。 | 换弹期间角色继续 idle/moving，不冻结也不覆盖。 | C + B。 | 没有"动作层叠在循环之上"的概念，所以即使拿到换弹信号也没有合成方式；本轮未设计。 |
| **weapon switch** | 只影响第三人称武器挂载：`m_hActiveWeapon` 句柄 → `m_iWorldModelIndex` 解析到武器实体（`native/src/entity_model.cpp:628,641,695`）。**不改变角色动画状态**。 | `m_hActiveWeapon`（server 属性）；`weapon_equipped` game event（实测 66 次）未用。 | 切枪时角色不换持枪姿态，只有手里挂的模型变了。 | C + B；门禁 `third-person-weapon-check.sh`（未复跑）。 | 角色 clip 是否需要随武器切换（每把武器有自己的 stand/run 动画集）本轮未验证。 |

## 3. demo 协议能给什么、不能给什么

### 3.1 能给

| 来源 | 内容 | 位置 |
|---|---|---|
| server 快照属性（每实体 delta） | `m_iHealth`、`m_lifeState`、`m_fFlags`、`m_bDucked`、`m_bDucking`、`m_flDucktime`、`m_vecOrigin`、`m_hActiveWeapon` | 读取见 `native/src/main.cpp:9374-9415`、`native/src/entity_model.cpp:628` |
| `svc_TempEntities` / `CTEPlayerAnimEvent` | 只有两个字段被解析：`m_hPlayer`→`event.player`、`m_iEvent`→`event.event`（`native/src/demo_header_events.cpp:52-54`）。**没有 action 字符串、没有 player slot、没有 weapons** | `native/include/demo_header.h:162-203` |
| `svc_GameEvent` | **62 种**事件（snakewater）/ **72 种**（upward），带 tick（播放域）与全字段；死亡事件同时给 `userid` 和 `victim_entindex` | §5.2 清单；计数 `cut -f3 events-<demo>.log \| sort -u \| wc -l` |
| string table userdata | 只记录了字节数统计，**未解析成 userinfo**（`native/src/demo_header_strtab.cpp:277-290`） | — |

### 3.2 不能给

- **没有事件值→动作的权威映射**：协议只给一个整数 `m_iEvent`。同一整数在不同地图上频次差一个量级（event=6：1553/1296/2201 次），本轮只能给出统计推断（9–14=受伤反应、0=攻击侧，2/3 demo），**给不出 TF2 的 PLAYERANIMEVENT_* 枚举名**；die/taunt/reload/deploy 没有任何值被证明（RECON §4.7）。
- **没有每 tick 的速度/着地保证**：速度是产品自己对 `m_vecOrigin` 求差得到的，不是 wire 字段。
- **没有身份表**：`userid` 与实体索引是两个编号空间，userinfo 未解码 → 官方映射渠道在本代码库里不存在（`native/src/review_bookmarks.cpp:321` 在缺身份时写 `identityReason="no-userinfo"`）。
- **temp entity 的 tick 是服务器绝对 tick**：`demo_header.cpp:162` 把 `networkTick`（`svc_TickMsg` 解出的绝对 tick）传进去，`demo_header_events.cpp:312` 直接 `event.tick = packetTick`；延迟事件（`hasDelay`）不会修正 tick。**game event 的 tick 是播放域**（`demo_header_events.cpp:139-140,162`）。两者相差 `serverTickOrigin - firstRecordedPacketTick`（snakewater 10319、upward 11056，均实测对上 tickMax），所以任何"同一 tick"的比较都必须先换算。
- **缓存上限 16384，且是前缀截断**：`demo_header_events.cpp:310` 的 `if (tempEntityEvents.size() < 16384u)` 是"首次填满就不再 push"（不是淘汰最旧）。三个 demo 的 `cached` 全部等于 `cap=16384`，因此保留下来的是**每个 demo 最前面的 16384 条临时实体事件**；`parsedEvents`（`demo_header_events.cpp:295`，全 demo 计数）远大于 `decoded`（缓存内判读成功的 anim 事件数）就是截断的直接证据（17444 vs 6321 等）。

## 4. 本轮实测的 census 读数

见 `docs/T85-ANIMATION-RECON.md` §2（同一份表）。要点：

| demo | decoded | parsedEvents | cached/cap | fieldHits | player_death | player_hurt |
|---|---|---|---|---|---|---|
| cp_snakewater_final1 | 6321 | 17444 | 16384/16384 | 15654 | 109 | 2144 |
| pl_upward_f12 | 4265 | 46671 | 16384/16384 | 13386 | 248 | 3494 |
| koth_bagel_rc13 | 7819 | 16722 | 16384/16384 | 17776 | 168 | 2191 |

计数器的一句话定义（可复核，见 RECON §2.3）：

- `parsedEvents`：整个 demo 扫描中**遇到**的 `CTEPlayerAnimEvent` 总数（`demo_header_events.cpp:295`），与缓存无关。
- `cached`：`tempEntityEvents` 容器条目数；容器首次填满 16384 就停止 push（`demo_header_events.cpp:310`），是**前 16384 条临时实体事件的前缀**。
- `decoded`：缓存内 **className==CTEPlayerAnimEvent 且有 m_hPlayer 与 m_iEvent** 的条目数（`native/tools/model_pose_probe.cpp:794-806`），所有相关性只在这个集合上算。
- `fieldHits`：缓存内 anim event 的 `knownFieldCount` 之和（`demo_header_events.cpp:323`）；因为读取时对所有临时实体都先试 `m_vecOrigin`/`m_vecAngles`，它可以 > 2×decoded，**不是**字段命中率百分比。


### 4.1 域对齐重跑（census2，t87 副本 exe `41b37ece…`）

| demo | serverMinusPlayback | decoded | event 9 exactHurt / n | event 9 hurt lift | event 0 攻击者占比 | 判读 |
|---|---|---|---|---|---|---|
| cp_snakewater_final1 | 10319 | 6321 | **545 / 545 = 1.000** | **6.97×** | 0.52 | 强耦合 |
| pl_upward_f12 | 11056 | 4265 | **192 / 192 = 1.000** | **6.67×** | 0.58 | 强耦合 |
| koth_bagel_rc13 | 56147 | 7819 | 13 / 508 = 0.026 | 0.96× | 0.16 | **未复现**（贴基线 0.0283/tick） |

日志：`D:/_shared/t85-anim/census2-{snakewater,upward,bagel13}.log`（623/671/591 行）。bagel13 的候选原因与 D1b 待办见 RECON §3.6。

## 5. 引用清单（全部 e15577b）

- `native/include/animation_clock.h:43,52-62,65-80`
- `native/include/animation_sequence_match.h:5-52,75-163,167-173,329-370`
- `native/include/demo_header.h:162-203,593-598`
- `native/src/demo_header_events.cpp:52-54,139-140,162,252-336`
- `native/src/demo_header.cpp:143,162`（`svc_TickMsg` 的绝对 tick 传入 temp entity）
- `native/src/main.cpp:3310,3451-3477,9337-9343,9360-9415,9522-9557,9836-9850,9932-10023,10030-10083,10121-10264`
- `native/src/entity_model.cpp:628,641,695`
- `native/src/review_bookmarks.cpp:321`
- 探针：`native/tools/model_pose_probe.cpp:764-901`（census 实现，本仓库 t84 分支未提交）

## 6. 证据分级

| 级别 | 含义 |
|---|---|
| **A** | 门禁脚本断言过，且本轮**复跑通过**（本轮没有这一级，见 §7） |
| **B** | 本轮实测读数（census / events 输出），有日志与哈希 |
| **C** | 读代码确认路径存在，本轮未运行 |
| **D** | 未实现 / 协议侧无输入 |

## 7. 本轮没有做的事（别外推）

- 没有复跑任何门禁脚本（`animation-drive-states-check.sh`、`player-animation-check.sh`、`third-person-weapon-check.sh`）；它们存在，本轮只读了脚本，没有断言结果。
- 没有启动产品、没有抓帧；本文所有"产品会这样播"的表述都来自代码阅读，不是像素证据。
- 没有解析 userinfo，因此 userid↔entindex 的映射在本仓库里**仍是空白**；本轮只从 `player_death` 的 `userid`+`victim_entindex` 反推出一张 **demo 级经验表**（109/248 次死亡全一致，见 RECON §5），那不是协议事实。
- 域对齐重跑只覆盖 2/3 demo（bagel13 未复现，RECON §3.6）；全实体花名册检定只覆盖少量 tick（RECON §4.8）；census 仍用常量偏移而不是 `packetServerTicks` 查表。
- 没有跑属性直方图探针，因此 turn 行要的 `m_angEyeAngles` 是否存在，仍是空白。
