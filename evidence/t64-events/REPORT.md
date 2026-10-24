# T64 — SourceTV 事件与时间轴数据审计（静态阶段）

**结论先行**：事件族的 **wire 来源、字段、tick 归属**全部可溯源到源码行；但产品当前**不解析任何事件内容**（只计数），且没有 payload 样本，因此所有事件类型的**内容级状态 = NOT VERIFIED**。计数、tick 域、发送表属性、实体历史/相机采样的真实读数已在既有 evidence 中核实（见下）。需要运行时才能闭环的 4 项写在 §6 proposal；缺样本的 1 项升级（§5 G2）。

- 基线：HEAD `99eeea2`（`git log --oneline -1`），跑前 `git status --porcelain` = 9 行。
- 边界：静态阶段。**未构建、未启动产品/探针、未抓包、未持主机锁、未改任何源码/门禁/配置**。写范围仅 `evidence/t64-events/**`。
- 与 T55 的关系：本审计只读 `.dem` 字节与既有日志，**无编辑/导出代码路径**，不混入 T55 的编辑/导出工作。
- 读数分级（全文统一）：
  - `VERIFIED-COUNT` = 真实 demo 上解析器打印的计数；
  - `VERIFIED-VALUE` = 真实 demo 的发送表/实体属性/历史读数；
  - `NOT VERIFIED` = 没有解码后的真实事件样本，禁止当通过；
  - `ABSENT-IN-SAMPLE` = 样本里确实没有该消息（计数为 0 或 1），不等于"不存在"。

---

## 1. 样本身份

`evidence/t63-sourcetv` **不存在** → T63 的共享样本清单**未冻结**。本审计以磁盘上两个 SourceTV demo 为准，并用 md5 固定身份：

| 项 | bagel | snakewater |
|---|---|---|
| 路径 | `D:/TF2_Demo_Player/testdata/demos/4a9bfb9276509d0ec5f5fdc722a95b17_match-20260927-0239-koth_bagel_rc13.dem` | `D:/TF2_Demo_Player/testdata/demos/bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem` |
| 大小 | 46,921,111 B | 51,834,617 B |
| md5 | `4a9bfb9276509d0ec5f5fdc722a95b17` | `bb841c6d379ff7c40d0c8baf99f59d8d` |
| 类型/协议 | SourceTV / 24 | SourceTV / 24 |
| ticks / frames | 77,466 / 73,118 | 64,352 / —（未在读数行打印 frames） |
| duration | 1161.99 s | —（读数行未打印；rate 66.666669 与 ticks 一致） |
| commands / packets | 73,125 / 73,121 | 64,337 / 64,333 |
| map / 模式 | `koth_bagel_rc13` / KOTH | `cp_snakewater_final1` / 5CP |
| server | `na.serveme.tf #633053` | — |
| classes | 363 | 363 |

读数来源：`evidence/camera-ui/e2e.driver.txt:3`（bagel 全字段行）、`evidence/entity-material/gate.txt:41`（snakewater 全字段行）、`evidence/animation-availability/oracle-all-classes.txt:1-14`（bagel header dump：`map: "koth_bagel_rc13"`、`ticks: 77466`、`duration: 1161.99`、`frames: 73118`、`signon: 1046987`）。

**缺失**：磁盘上没有 `pl_*`（payload）demo → cart progress/checkpoint 无真实样本（G2）。POV 样本 `evidence/observer-focus/pov-row.txt` 只用于 observer 对照，不属本审计样本集。

---

## 2. 事件族 → 原始来源映射

产品侧的公共事实：`decodeDemoMessageStream(payload, payloadBits, entry.tick, lastNetworkTick, summary, stream)`（`native/src/demo_header.cpp:2174-2179`，调用点 `demo_header.cpp:2296` 传 `entry.tick`）——**事件 tick 在解码点已经拿得到**，接入事件内容不需要新数据源。

### 2.1 击杀/死亡

| 项 | 内容 |
|---|---|
| wire | `svc_GameEvent`（type 25，`demo_header.cpp:2214`）→ 11-bit payloadBits + 9-bit event id（参考解析器 `src/demo/message/gameevent.rs:20-51`） |
| 事件名 | `player_death`（`src/demo/gameevent_gen.rs:18165`）；`player_team`（18161）、`player_spawn`（18173）提供队伍/职业上下文 |
| 字段 | `PlayerDeathEvent`（`gameevent_gen.rs:1165`）：userid u16、victim_entindex u32、inflictor_entindex u32、attacker u16、weapon string、weapon_id u16、damage_bits u32、custom_kill u16、assister u16、weapon_log_class_name string、stun_flags u16、death_flags u16、silent_kill bool、player_penetrate_count u16、assister_fallback string、kill_streak_* u16、duck_streak_* u16、rocket_jump bool、weapon_def_index u32、crit_type u16 |
| 产品现状 | **只计数**：`readGameEvent` 读 11-bit 长度后整段 skip，仅 `++summary.gameEventCount` 与 payloadBits 累计（`demo_header.cpp:790-796`）。没有 event id/名字/字段/tick。 |
| 身份 | 事件内是 userid / entindex；产品**不解析 userinfo**（全仓库无 player_info 解码，G5）→ 无玩家名，只能用 entindex 关联实体状态（`m_iTeamNum`/`m_iClass`/`m_iHealth` 可读，`playback_hud.cpp:38-53`）。 |
| tick | 包 tick（`entry.tick`），精确；参考解析器同一结论：`Packet::tick()`（`src/demo/packet/mod.rs:42`）、`handle_message(..., tick, ...)`（`src/demo/parser/analyser.rs:405`、`:416`）。 |
| 状态 | **NOT VERIFIED**（无解码样本）。真实计数：bagel `events=12982`、snakewater `events=10703`（`e2e.driver.txt:3` / `gate.txt:41`）。 |

### 2.2 回合开始/结束

| 项 | 内容 |
|---|---|
| wire | `svc_GameEvent`：`teamplay_round_start`（`gameevent_gen.rs:18295`，字段 `TeamPlayRoundStartEvent` `:4113` = full_reset bool）、`teamplay_round_win`（`:18313`，`TeamPlayRoundWinEvent` `:4429` = team u8、winreason u8、flagcaplimit u16、full_round u16、round_time f32、losing_team_num_caps u16、was_sudden_death u8）、`teamplay_game_over`（`:18327`） |
| 实体属性 | `DT_TeamplayRoundBasedRules.m_iRoundState` Int（class id=211 `CTFGameRulesProxy`，`oracle-all-classes.txt:22475`）、`m_iWinningTeam`（`:22477`）；`CTeamRoundTimer` class id=127（`:14922`，flat=16） |
| 产品现状 | 事件内容不解析（同上）；实体属性可读（通用 key/value），但产品没有事件↔实体状态的绑定。 |
| 状态 | **NOT VERIFIED**（事件内容）；实体属性 `VERIFIED-VALUE`（发送表存在，真实 bagel demo）。 |

### 2.3 控制点变化

| 项 | 内容 |
|---|---|
| wire | `controlpoint_updateowner`（`gameevent_gen.rs:18281`，`ControlPointUpdateOwnerEvent` `:3835` = index u16）、`teamplay_point_startcapture`（`:18335`）、`teamplay_point_captured`（`:18337`，`TeamPlayPointCapturedEvent` `:4936` = cp u8、cpname string、team u8、cappers string）、`teamplay_point_locked`（`:18339`）、`teamplay_point_unlocked`（`:18341`） |
| 实体属性 | `DT_BaseTeamObjectiveResource.m_iOwner.000`（class id=16 `CBaseTeamObjectiveResource`，`oracle-all-classes.txt:4255`，[804]）、`m_iNumControlPoints`（`:4273`，[822]）、`m_iTimerToShowInHUD`（`:27254` 的 [892] 段内）、`m_vCPPositions`（`:27258` [898]） |
| 产品现状 | 事件内容不解析；实体属性可读（`m_iOwner.000`…`.007` 是逐点 owner）。 |
| 状态 | **NOT VERIFIED**（事件内容）；属性 `VERIFIED-VALUE`（真实 bagel 发送表）。snakewater 的 BSP 侧有 `team_control_point` x5（`evidence/t65-minimap/data/cp_snakewater_final1.bsp.probe.json`），说明 5CP 地图确实有 5 个控制点实体。 |

### 2.4 推车进度/检查点（payload）

| 项 | 内容 |
|---|---|
| wire | `cart_updated`（`gameevent_gen.rs:18249`；`CartUpdatedEvent` `:3244` = **空结构，无字段**）、`payload_pushed`（`:18823`；`PayloadPushedEvent` `:15711` = pusher u8、distance u16） |
| 实体属性 | `DT_TeamTrainWatcher.m_flTotalProgress` Float（class id=128 `CTeamTrainWatcher`，`oracle-all-classes.txt:14984`，[44]）、`m_iTrainSpeedLevel`（[45]）、`m_flRecedeTime`（[46]）、`m_nNumCappers`（[47]）、`m_hGlowEnt`（[48]） |
| 样本 | **无 payload demo**；snakewater（5CP）的 BSP 里 **没有** `team_train_watcher`（T65 BSP 读数）→ 即使有 snakewater 也验证不了。 |
| 状态 | **NOT VERIFIED（缺样本，G2）**；不能把"事件类型存在"当成已验证。 |

### 2.5 导演相机变化

| 项 | 内容 |
|---|---|
| wire | `svc_SetView`（type 18，`demo_header.cpp:2209`）→ `readSetView` 只读 11-bit entityIndex，`++setViewCount`，只保留 `lastViewEntity`（`demo_header.cpp:1731-1737`）；`svc_FixAngle`（type 19，`readFixAngle` `demo_header.cpp:644-676`）；每包 cmdinfo 视角（`demo_header.cpp:2280-2292`） |
| 真实读数 | bagel `setview=1 viewent=1 fixangle=0 views=65536`；snakewater `setview=1 viewent=1 views=64333`（`e2e.driver.txt:3`、`gate.txt:41`）。**两个 SourceTV 样本全程只有 1 条 SetView** → "导演切镜"作为 SetView 事件流在样本里 **ABSENT-IN-SAMPLE**（不是 0 条，是 1 条，且无历史）。 |
| 实体属性 | `m_iObserverMode`（`oracle-all-classes.txt:3255`）、`m_hObserverTarget`（`:3256`）；产品 `resolveObserverFocus` 只跟随 mode 4/5（`native/src/entity_model.cpp:270-295`，声明 `entity_model.h:121`） |
| 真实样本 | `evidence/observer-focus/bagel-row.txt`：`tick=129277 status=available resolved=129277 ... mode=4 targetHandle=1718275 targetIndex=3 targetSerial=839 targetPresent=1 follows=1`；POV 对照 `pov-row.txt`：tick 51900/53400 → `status=checkpoint`，resolved 51742/53075（stale 158/325） |
| 状态 | SetView 历史 **NOT VERIFIED**（无历史数组）；observer 焦点 `VERIFIED-VALUE`（真实 bagel 读数）。 |

---

## 3. tick 与显示时间

1. **事件 tick = 包 tick**（精确，同一 tick 域）：参考解析器 `src/demo/packet/mod.rs:42`（`Packet::tick()` 返回包自身 tick 字段）、`src/demo/parser/analyser.rs:405`（`handle_message(&mut self, message: &Message, tick: DemoTick, ...)`）、`:416`（`Message::GameEvent(message) => self.handle_event(&message.event, tick)`）。产品解码点 `demo_header.cpp:2296` 已把 `entry.tick` 传进去。
2. **tickRate**：`demoTickRate` = `clamp(ticks / playbackTime, 30.0, 128.0)`，不可用时回退 `66.6666667`（`demo_header.cpp:2331-2340`）。真实读数：bagel `rate=66.666667`、snakewater `rate=66.666669`（`e2e.driver.txt:3`、`gate.txt:41`）。
3. **两个原点（必须选一个）**：
   - `firstPacketTick` = 第一条 `command == 1 || command == 2` 的 entry.tick（`main.cpp:5064-5067`，播放器用这个）；
   - `firstRecordedPacketTick` = 只认 `command == 2`（`demo_header.cpp:2323-2329`，声音用这个）。
   - 两者在 bagel 上是否相等 **未测**（G6）。显示时间建议统一用 `(eventTick - firstPacketTick) / tickRate`，并在数据里同时保留绝对 tick。
4. **FixAngle 的 tick 域**：`readFixAngle` 的 sample.tick = 传入的 `packetTick`（`demo_header.cpp:669`），调用点 `demo_header.cpp:2211` 传的是 `networkTick`（`summary.lastNetworkTick`），而 cmdinfo 用 `entry.tick`（`demo_header.cpp:2287`）→ **两套 tick 域可能不一致**（G7，未验证）。bagel `fixangle=0`，本样本无 FixAngle 样本，无法对照。
5. **显示时间公式**：`seconds = (tick - firstPacketTick) / tickRate`；绝对时间 `tick / tickRate` 与 header duration 同源（bagel 77466/66.666667 = 1161.99 ✓）。

---

## 4. 同 tick 帧可用性（实测，不是推测）

实体快照查询 `queryEntitySnapshotAtOrBeforeTick`（`demo_header.cpp:2380`；`findEntitySnapshotAtOrBeforeTick` `:2445`）的语义（头文件注释 `demo_header.h:333`）：**返回 tick 之前最新的保留 CHECKPOINT**，不是精确帧。真实读数（`evidence/history-coverage/bagel.txt`）：

- archive=96，checkpoints ticks=`[56148..129125]`，medianGap=**822** tick，maxGap=**1180** tick；
- liveCheckpoints=1，ticks=`[129210..129210]`，packets=68，events=1155，dropped=**172**；
- 512 个采样 tick：exact=**1**，checkpoint=**511**；staleness worst=**1139**（at 87460），mean=**407.5**；
- 66.67 Hz 换算：中位 **12.3 s**、最坏 **17.1 s** 陈旧。

结论：
- 只有 tick ≥ 129210（bagel 最后 ~68 包 ≈ 1.0 s）才是精确同 tick；其余全部是 checkpoint 陈旧帧，且 172 个包因 delta base 缺失被丢（`entityHistoryDroppedPackets` / `entityHistoryDeltaBaseMisses`，`demo_header.cpp:1146/1160/1186`）。
- snakewater 只有 gate 行计数（`histEvents=3718 histCheckpoints=2 histDropped=174`，`gate.txt:41`），**没有做 history-coverage 采样** → 其陈旧度 **未测**（G8）。
- 相机姿态：`viewSamples` 上限 65536（`demo_header.cpp:2288`；FixAngle 同 cap `demo_header.cpp:666`）。bagel 真实 `views=65536` = **打满**，而 packets=73121 → **最后 ~7585 个包没有相机样本**（≈ 末尾 10.4% / ~121 s），且 `findObserverViewAtOrBeforeTick`（`demo_header.cpp:2795-2803`）用 `upper_bound` 取"≤tick 的最后一条"，**不返回 staleness**（mask-vs-effect：晚段点开画面会静默显示冻结的旧视角）。snakewater `views=64333`（= packets，未打满）。
- 因此 contract 必须把 `event.tick` 与 `frame.tick` 分开，并显式给 `frame.stalenessTicks`；**不能承诺同 tick 帧**。

---

## 5. 缺口清单（G1–G12，全部命名）

| # | 缺口 | 证据 | 影响 | 补救 |
|---|---|---|---|---|
| G1 | T63 共享样本清单未冻结（`evidence/t63-sourcetv` 不存在） | 目录不存在 | 样本身份只能按磁盘文件+md5 固定 | 等 T63 产出或 Lead 确认 |
| G2 | 无 payload demo | 磁盘无 `pl_*`；snakewater BSP 无 `team_train_watcher` | cart progress/checkpoint 无法验证 | P5 升级 |
| G3 | 产品不解析事件内容（只计数） | `demo_header.cpp:790-796` | 事件名/字段/tick 都拿不到 | P3 |
| G4 | 产品不解析 GameEventList 定义 | `demo_header.cpp:807-815`（9-bit count + 20-bit payloadBits 后整段 skip，只 `gameEventDefinitionCount += eventCount`） | 无事件名↔id 映射 | P3（定义从 demo 内解析，非硬编码） |
| G5 | 无 userinfo/player_info 解析 | 全 `native/` grep 无 player_info 解码（唯一命中是无关的 logstf UserInfoRefused） | 无玩家名，只有 userid/entindex | P3 可扩展 userinfo；否则导航只显示 entindex |
| G6 | 两个 tick 原点未对照 | `main.cpp:5064-5067` vs `demo_header.cpp:2323-2329` | 显示时间可能整体偏移 | P2/P3 输出首包 tick 对照 |
| G7 | FixAngle tick 域未验证 | `demo_header.cpp:669`（packetTick）vs `:2287`（entry.tick） | 若用 FixAngle 姿态，时间轴可能错位 | 需要含 FixAngle 的样本（bagel fixangle=0） |
| G8 | snakewater 历史陈旧度未采样；bagel 172 包丢失 | `history-coverage/bagel.txt`；`gate.txt:41` | 任意事件 tick 的帧可能陈旧/缺失 | P4 |
| G9 | 事件顺序/重复/缺失未验证 | 无解码样本 | 导航列表可能有重复/乱序 | P2/P3 |
| G10 | 无事件 dump 工具 | `ent-oracle` 仅实体模式（`src/main.rs` 213 行；examples 只有 `allprops.rs`） | 无法独立对照产品事件输出 | P2 |
| G11 | 事件↔帧没有绑定 | 产品只有 `lastViewEntity`，无事件→实体历史映射 | 点击事件只能取该 tick 的 checkpoint 帧 | P4 |
| G12 | 相机样本 cap 65536 打满（bagel） | `views=65536`（`e2e.driver.txt:3`）；cap `demo_header.cpp:2288` | 末尾 ~121 s 相机姿态冻结且无 staleness | P1（先量化 dropped 数） |

---

## 6. 运行时提案（静态阶段不能执行；每条给出确切命令/耗时/产出）

> 前置：这些都需要主机锁与构建/启动，**本次未做**。按 T64 规则，运行时项只写 proposal。

### P1 — 用现有探针读相机样本丢失数（零代码）
- 目的：把 G12 从"cap 打满"变成确切 dropped 数（bagel 期望 ≈ 7585）。
- 命令：`cd /d/TF2_Native_Test && native/build-nmake/demo_open_probe.exe D:/TF2_Demo_Player/testdata/demos/4a9bfb9276509d0ec5f5fdc722a95b17_match-20260927-0239-koth_bagel_rc13.dem`（snakewater 同）
- 耗时：每个 ~10 s。
- 产出：`setview_count=`、`camera_track=`、`camera_track_dropped=`（`tools/demo_open_probe.cpp:90,172,173`）。
- 为什么必须运行时：这些计数只在解析真实 demo 时产生，源码里没有常量。

### P2 — 参考解析器加事件 dump example（独立于产品）
- 目的：拿到真实事件的 name/tick/字段，建立 oracle（G9/G10）。
- 命令：在 `D:/TF2_Demo_Player/work/_refs_demostf` 加 `examples/dump_events.rs`（~40 行，用 `GameEventListMessage` + `GameEventMessage` + `Packet::tick()`），然后 `cargo run --release --example dump_events -- <demo>` 对两个 demo。
- 耗时：构建 1–3 min，运行 10–30 s/demo。
- 产出：`evidence/t64-events/dump-events-bagel.txt` / `-snakewater.txt`（事件名、tick、字段）。
- 为什么必须运行时：事件是 demo 字节流内容，静态无法得出。

### P3 — 产品侧事件解码（最小改动）
- 目的：产品自己能出事件列表（G3/G4/G6）。
- 命令：新增 probe（或 `demo_open_probe` 加 `--events` 标志），在 `readGameEvent` 里按 `svc_GameEventList` 的定义解 9-bit id + 字段，tick 用 `entry.tick`；`build-target.sh` 重建，跑两个 demo。
- 耗时：构建 2–4 min，运行 ~10 s/demo。
- 产出：`evidence/t64-events/product-events-*.txt`。
- 为什么必须运行时：需要真实 demo 的 GameEventList（每 demo 一份，id 是 demo 自定义，不是固定常量）。

### P4 — 事件 tick 的 seek/帧一致性
- 目的：量化"点击事件 → 帧"的误差（G8/G11）。
- 命令：在 P2/P3 产出里挑 3 个真实事件 tick（如 round_start / player_death / point_captured），用 `queryEntitySnapshotAtOrBeforeTick` 与 `findObserverViewAtOrBeforeTick` 查帧，记录 `status`（Available/Checkpoint/Gap/TickBeforeHistory）与 staleness。
- 耗时：~1–2 min。
- 产出：`evidence/t64-events/seek-identity.txt`。
- 为什么必须运行时：依赖真实事件 tick 与真实实体历史窗口。

### P5 — payload demo 获取（不能伪造）
- 目的：G2 的唯一解法。
- 命令：由用户/Lead 提供任一 `pl_*` SourceTV demo（例如 `pl_upward`/`pl_badwater`），再跑 P2/P3。
- 耗时：取决于获取。
- 产出：cart 事件与 `m_flTotalProgress` 的真实读数。
- 为什么不能静态做：磁盘上没有该样本；**不伪造**。

---

## 7. 验收对照（T64 §14）

1. **每个事件类型可溯源**：§2 覆盖 6 族（击杀、回合、控制点、推车、相机）+ 15 个事件名 + 6 组实体属性，全部给到 `file:line`；缺样本的族明确标 NOT VERIFIED（G2）。
2. **tick 精度**：事件 tick = 包 tick（参考解析器证明），产品接入点已有 `entry.tick`；显示时间公式与两个原点差异写清（G6）。
3. **失败/缺失命名**：G1–G12，无"静默通过"；`views=65536` 打满这种"mask-vs-effect"也单列（G12）。
4. **未混入 T55 编辑/导出**：本审计只读 demo 字节与既有日志；写范围仅 `evidence/t64-events/**`；未触碰编辑/导出代码或数据。

---

## 8. 文件与方法

- 本目录：`REPORT.md`（本文）、`INVENTORY.md`（源码/参考/发送表逐条清单与原文引用）、`EVENT-LIST.md`（事件清单+字段+状态）、`DATA-CONTRACT.md`（点击导航最小数据契约）。
- 方法：只读 `read`/`grep` 仓库文件；引用既有 evidence 文件（`e2e.driver.txt`、`gate.txt`、`history-coverage/bagel.txt`、`observer-focus/*`、`oracle-all-classes.txt`、T65 BSP JSON）；未构建、未启动、未抓包。
- 自检：HEAD `99eeea2`；跑前 `git status --porcelain` = 9 行；跑后见 §9（只新增本目录）。

## 9. 状态自检

- HEAD 仍为 `99eeea2`（`git log --oneline -1`）。
- `git status --porcelain | wc -l`：跑前 **9** 行 → 跑后 **10** 行；唯一新增是 `?? evidence/t64-events/`（本目录 4 个文件，共 724 行）。
- `git status --porcelain -- native` = **0 行**（native/ 零改动）。
- 无构建产物变化（未运行构建）；未启动任何产品/探针；未持主机锁；未写 `evidence/` 之外路径。
- 本目录文件：`REPORT.md`(201)、`INVENTORY.md`(299)、`EVENT-LIST.md`(74)、`DATA-CONTRACT.md`(150)。
