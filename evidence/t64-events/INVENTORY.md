# INVENTORY — T64 事件数据来源逐条清单

本文件只做一件事：把"事件/时间轴数据从哪来"逐条钉到源码行或既有 evidence 行。**没有解码后的真实事件样本**，所以"内容级"结论一律 NOT VERIFIED（见 REPORT §5）。

---

## A. 产品侧（native/）现状：事件只计数，不解析

### A1. 消息循环（svc_* 类型 → 处理函数）

`native/src/demo_header.cpp:2209-2218`（原文）：

```cpp
2209:     else if (type == 18) { if (!readSetView(bits, summary)) result.packetValid = false; else { result.decodedAny = true; } }
2210:     else if (type == 17) { if (!readSounds(bits, summary, entryTick)) result.packetValid = false; else { result.decodedAny = true; } }
2211:     else if (type == 19) { if (!readFixAngle(bits, summary, networkTick)) result.packetValid = false; else { result.decodedAny = true; } }
2212:     else if (type == 23) { if (!readUserMessage(bits, summary)) result.packetValid = false; else { result.decodedAny = true; } }
2213:     else if (type == 24) { if (!readEntityMessage(bits, summary)) result.packetValid = false; else { result.decodedAny = true; } }
2214:     else if (type == 25) { if (!readGameEvent(bits, summary)) result.packetValid = false; else { result.decodedAny = true; } }
2215:     else if (type == 26) { ... readPacketEntities(bits, summary, networkTick) ... }
2216:     else if (type == 27) { ... readTempEntities(bits, summary, networkTick) ... }
2217:     else if (type == 28) { ... readPrefetch(bits, summary) ... }
2218:     else if (type == 30) { if (!readGameEventList(bits, summary)) result.packetValid = false; else { result.decodedAny = true; } }
```

### A2. svc_GameEvent（type 25）：只读长度、整段跳过

`native/src/demo_header.cpp:790-796`（原文）：

```cpp
790: bool readGameEvent(MessageBits& bits, DemoNetworkSummary& summary) {
791:   std::uint32_t payloadBits = 0;
792:   if (!bits.read(11, payloadBits) || payloadBits > bits.remaining() || !bits.skip(payloadBits)) return false;
793:   ++summary.gameEventCount;
794:   summary.gameEventPayloadBits += payloadBits;
795:   return true;
796: }
```

→ 没有 event id、没有 name、没有字段、没有 tick。**事件内容 = 不可见**。

### A3. svc_GameEventList（type 30）：只读 count/length、整段跳过

`native/src/demo_header.cpp:807-815`（原文）：

```cpp
807: bool readGameEventList(MessageBits& bits, DemoNetworkSummary& summary) {
808:   std::uint32_t eventCount = 0;
809:   std::uint32_t payloadBits = 0;
810:   if (!bits.read(9, eventCount) || !bits.read(20, payloadBits)) return false;
811:   if (payloadBits > bits.remaining() || !bits.skip(payloadBits)) return false;
812:   ++summary.gameEventListCount;
813:   summary.gameEventDefinitionCount += eventCount;
814:   return true;
815: }
```

→ 定义（id→name→字段类型）没有解码，所以即使有 GameEvent 也无法映射。参考解析器的定义格式见 B2。

### A4. svc_SetView（type 18）：只有最后值，没有历史

`native/src/demo_header.cpp:1731-1737`（原文）：

```cpp
1731: bool readSetView(MessageBits& bits, DemoNetworkSummary& summary) {
1732:   std::uint32_t entityIndex = 0;
1733:   if (!bits.read(11, entityIndex)) return false;
1734:   ++summary.setViewCount;
1735:   summary.lastViewEntity = entityIndex;
1736:   return true;
1737: }
```

→ `setViewCount` 只有总数；`lastViewEntity` 只保留最后一条。**相机变化列表不存在**。

### A5. svc_FixAngle（type 19）：解码角度，但 tick 用 networkTick

`native/src/demo_header.cpp:644-676`（关键行原文）：

```cpp
644: bool readFixAngle(MessageBits& bits, DemoNetworkSummary& summary, std::int32_t packetTick) {
...
664:   ++summary.fixAngleCount;
665:   ++summary.fixAngleDecoded;
666:   if (summary.viewSamples.size() >= 65536u) ++summary.viewSamplesDropped;
667:   else {
668:     DemoViewSample sample;
669:     sample.tick = packetTick;
670:     sample.angles[0] = angles[0]; sample.angles[1] = angles[1]; sample.angles[2] = angles[2];
671:     sample.hasAngles = true;
672:     sample.source = DemoViewSource::FixAngle;
673:     summary.viewSamples.push_back(sample);
674:   }
```

调用点 `demo_header.cpp:2211` 传 `networkTick`（`summary.lastNetworkTick`，由 type 3 Tick 消息更新，见 `:2197`），而 cmdinfo 样本用 `entry.tick`（`:2287`）→ **两个 tick 域**（G7）。

### A6. 每包 cmdinfo 视角：sample.tick = entry.tick；cap 65536

`native/src/demo_header.cpp:2280-2292`（原文）：

```cpp
2280:     if (entry.command != 1 && entry.command != 2) continue;
2281:     if (entry.payloadOffset >= 88) {
2282:       std::uint8_t cmdInfo[76] = {};
2283:       file.seekg(static_cast<std::streamoff>(entry.payloadOffset - 88), std::ios::beg);
2284:       if (readExact(file, cmdInfo, sizeof(cmdInfo))) {
2285:         DemoViewSample sample;
2286:         if (parseDemoCmdInfo(cmdInfo, sizeof(cmdInfo), sample)) {
2287:           sample.tick = entry.tick;
2288:           if (summary.viewSamples.size() >= 65536u) ++summary.viewSamplesDropped;
2289:           else summary.viewSamples.push_back(sample);
2290:         }
2291:       }
2292:     }
```

### A7. 解码入口：entry.tick 已传入

`native/src/demo_header.cpp:2174-2179`（原文）：

```cpp
2174: bool decodeDemoMessageStream(const std::vector<std::uint8_t>& payload,
2175:                              std::size_t payloadBits,
2176:                              std::int32_t entryTick,
2177:                              std::int32_t initialNetworkTick,
2178:                              DemoNetworkSummary& summary,
2179:                              DemoMessageStreamResult& result) {
```

调用点 `demo_header.cpp:2296`：`decodeDemoMessageStream(payload, payload.size() * 8u, entry.tick, summary.lastNetworkTick, summary, stream);`
→ **事件 tick 在接入点已经可用**（entry.tick = 该包/命令的 demo tick）。

### A8. 视角查询：at-or-before，无 staleness

`native/src/demo_header.cpp:2795-2803`（原文）：

```cpp
2795: bool findObserverViewAtOrBeforeTick(const DemoNetworkSummary& summary, std::int32_t tick, DemoViewSample& sample) {
2796:   if (summary.viewSamples.empty()) return false;
2797:   const auto match = std::upper_bound(
2798:       summary.viewSamples.begin(), summary.viewSamples.end(), tick,
2799:       [](std::int32_t value, const DemoViewSample& item) { return value < item.tick; });
2800:   if (match == summary.viewSamples.begin()) return false;
2801:   sample = *std::prev(match);
2802:   return true;
2803: }
```

→ 返回"≤ tick 的最后一条"，**不返回 staleness**；cap 打满后（bagel `views=65536`）晚段查询会静默拿到冻结的旧样本。

### A9. 实体历史：只保留 checkpoint（不是逐 tick 帧）

- `EntityHistoryEvent` / `EntityHistoryCheckpoint` / `EntityHistoryPacket` / `EntityHistoryLimits`：`native/include/demo_header.h:285-322`（原文见文件；limits = `maxEvents=8192, maxCheckpoints=8, checkpointStride=128, archiveMax=96`）。
- `appendEntityHistory`：`demo_header.cpp:1138`；丢包计数 `:1146`、`:1160`（`entityHistoryDroppedPackets`），delta base 缺失 `:1186`（`entityHistoryDeltaBaseMisses`）。
- 查询 `queryEntitySnapshotAtOrBeforeTick`：`demo_header.cpp:2380`；`findEntitySnapshotAtOrBeforeTick`：`:2445`；状态枚举 `TickBeforeHistory/Gap/DeltaBaseMissing`：`demo_header.h:420-423`。
- 头文件注释 `demo_header.h:333`：查询用"newest retained CHECKPOINT at or before tick"回答 → **同 tick 帧不保证**。

### A10. 其他相关产品代码

| 位置 | 内容 |
|---|---|
| `native/src/entity_model.cpp:270-295` | `resolveObserverFocus` 读 `m_iObserverMode`/`m_hObserverTarget`，只跟随 mode 4/5；声明 `entity_model.h:121` |
| `native/src/playback_hud.cpp:38-53` | `readPlaybackHud` 读 `m_iHealth`/`m_iTeamNum`/`m_iClass`/`m_iClip1`/`m_flChargeLevel` |
| `native/src/main.cpp:8226` | `sceneTick = firstPacketTick + g_playback.tick` |
| `native/src/main.cpp:8230-8232` | 实体快照查询 + staleness |
| `native/src/main.cpp:8242` | observer view 查询 |
| `native/src/main.cpp:5064-5067` | `firstPacketTick` = 第一条 command 1 或 2 |
| `native/src/demo_header.cpp:2323-2329` | `firstRecordedPacketTick` = 只认 command==2 |
| `native/src/demo_header.cpp:2331-2340` | `demoTickRate` = clamp(ticks/playbackTime,30,128)，回退 66.6666667 |
| `native/include/demo_header.h:32-45` | `DemoHeader` 字段（无 userinfo） |
| `native/include/demo_header.h:427-434` | `DemoViewSample` {tick, origin, angles, hasOrigin, hasAngles, source} |
| `native/include/demo_header.h:455-456` | `viewSamples`、`viewSamplesDropped` |

---

## B. 参考解析器（demostf `tf_demo_parser`，外部对照，非产品）

路径 `D:/TF2_Demo_Player/work/_refs_demostf`。

### B1. GameEvent 消息解析（`src/demo/message/gameevent.rs:20-51`）
11-bit length → read_bits → 9-bit `GameEventTypeId` → 按 `state.event_definitions` 定义读字段；定义表为空时返回 Unknown。

### B2. GameEventList 定义（`gameevent.rs:163-185`、`:217-226`）
`GameEventDefinition`：9-bit event_type id、String name、循环 {3-bit `GameEventValueType`, String entry_name} 直到 None。`GameEventListMessage`：9-bit count、20-bit length、count 个定义。→ **事件 id/名字/字段来自 demo 自身**，不需要硬编码 TF2 表。

### B3. 值类型（`src/demo/gamevent.rs:54-63`）
`None=0, String=1, Float=2, Long=3, Short=4, Byte=5, Boolean=6, Local=7`；读取 `:128-139`；`RawGameEvent` `:223-240`。

### B4. tick 归属（关键）
- `src/demo/parser/analyser.rs:405`：`handle_message(&mut self, message: &Message, tick: DemoTick, ...)`；
- `:416`：`Message::GameEvent(message) => self.handle_event(&message.event, tick)`；
- `src/demo/packet/mod.rs:42`：`Packet::tick()` 返回包自身 tick 字段。
→ **事件 tick = 包 tick**，与 demo command tick 同域，精确。

### B5. 下游使用（`src/demo/parser/gamestateanalyser.rs:56-72`）
把 `(self.tick, event)` 入队，Kill 记在 `self.tick`；`RoundStart`/`TeamPlayRoundStart` 清 building/projectile。`DemoTick(u32)` 定义 `src/demo/data/mod.rs:245`。

---

## C. TF2 事件定义（`src/demo/gameevent_gen.rs`，22973 行）

### C1. 事件名 → 行号
| 事件 | 行 |
|---|---|
| `player_team` | 18161 |
| `player_death` | 18165 |
| `player_spawn` | 18173 |
| `cart_updated` | 18249 |
| `controlpoint_updateowner` | 18281 |
| `controlpoint_starttouch` | 18283 |
| `controlpoint_endtouch` | 18285 |
| `teamplay_round_start` | 18295 |
| `teamplay_round_win` | 18313 |
| `teamplay_game_over` | 18327 |
| `teamplay_point_startcapture` | 18335 |
| `teamplay_point_captured` | 18337 |
| `teamplay_point_locked` | 18339 |
| `teamplay_point_unlocked` | 18341 |
| `payload_pushed` | 18823 |

### C2. 字段结构（行号 + 字段）
| 结构 | 行 | 字段 |
|---|---|---|
| `PlayerTeamEvent` | 1064 | userid u16, team u8, oldteam u8, disconnect bool, autoteam bool, silent bool, name string |
| `PlayerDeathEvent` | 1165 | userid u16, victim_entindex u32, inflictor_entindex u32, attacker u16, weapon string, weapon_id u16, damage_bits u32, custom_kill u16, assister u16, weapon_log_class_name string, stun_flags u16, death_flags u16, silent_kill bool, player_penetrate_count u16, assister_fallback string, kill_streak_* u16, duck_streak_* u16, rocket_jump bool, weapon_def_index u32, crit_type u16 |
| `PlayerSpawnEvent` | 1537 | userid u16, team u16, class u16 |
| `ControlPointUpdateOwnerEvent` | 3835 | index u16 |
| `TeamPlayRoundStartEvent` | 4113 | full_reset bool |
| `TeamPlayRoundWinEvent` | 4429 | team u8, winreason u8, flagcaplimit u16, full_round u16, round_time f32, losing_team_num_caps u16, was_sudden_death u8 |
| `TeamPlayPointCapturedEvent` | 4936 | cp u8, cpname string, team u8, cappers string |
| `PayloadPushedEvent` | 15711 | pusher u8, distance u16 |
| `CartUpdatedEvent` | 3244 | **空结构（无字段）** |

---

## D. 发送表属性（真实 bagel demo，`evidence/animation-availability/oracle-all-classes.txt`）

文件头 `:1-14` 是 header dump（`map: "koth_bagel_rc13"`, `ticks: 77466`, `duration: 1161.99`, `frames: 73118`, `signon: 1046987`）。

| 类/属性 | 行 | 备注 |
|---|---|---|
| class id=16 `CBaseTeamObjectiveResource` table=`DT_BaseTeamObjectiveResource` flat=831 | 3450 | 控制点资源 |
| `m_iOwner.000` [804] | 4255 | 逐点 owner（.000–.007） |
| `m_iNumControlPoints` [822] | 4273 | 控制点数量 |
| `m_iTimerToShowInHUD` [892] | 27254 段 | 同表 |
| `m_vCPPositions` [898] | 27258 段 | 控制点坐标 |
| class id=127 `CTeamRoundTimer` flat=16 | 14922 | 回合计时器 |
| class id=128 `CTeamTrainWatcher` table=`DT_TeamTrainWatcher` flat=49 | 14939 | 推车 watcher |
| `m_flTotalProgress` Float [44] | 14984 | 推车总进度 |
| `m_iTrainSpeedLevel` [45] / `m_flRecedeTime` [46] / `m_nNumCappers` [47] / `m_hGlowEnt` [48] | 14985-14988 段 | 同表 |
| class id=211 `CTFGameRulesProxy` flat=368 | 22276 | 规则代理 |
| `DT_TeamplayRoundBasedRules.m_iRoundState` Int [198] | 22475 | 回合状态 |
| `m_iWinningTeam` [200] | 22477 | 胜方 |
| class id=249 `CTFPlayerResource` flat=3675 | 30942 | 玩家资源（分数等） |
| class id=318 `CTFTeam` flat=9 | 46101 | 队伍实体（m_iScore 等） |
| `DT_BasePlayer.m_lifeState` | 3250 | 生命状态 |
| `m_iObserverMode` | 3255 | 观察模式 |
| `m_hObserverTarget` | 3256 | 观察目标 |

---

## E. 既有真实读数（evidence 文件:行）

| 读数 | 来源 |
|---|---|
| bagel 全字段行（ticks=77466 commands=73125 packets=73121 setview=1 viewent=1 fixangle=0 views=65536 events=12982 histEvents=1155 histCheckpoints=1 histDropped=172 rate=66.666667） | `evidence/camera-ui/e2e.driver.txt:3` |
| snakewater 全字段行（ticks=64352 commands=64337 packets=64333 setview=1 viewent=1 views=64333 events=10703 histEvents=3718 histCheckpoints=2 histDropped=174 rate=66.666669） | `evidence/entity-material/gate.txt:41` |
| bagel 消息直方图（svc_SetView=1, svc_UserMessage=137, svc_GameEvent=12982, svc_GameEventList=1, svc_EntityMessage=376, svc_TempEntities=14879） | `evidence/final/bagel.txt` |
| snakewater 消息直方图（svc_SetView=1, svc_UserMessage=100, svc_GameEvent=10703, svc_GameEventList=1, svc_EntityMessage=240） | `evidence/baseline/snakewater.txt` |
| 9 个其他 SourceTV demo 均 `svc_SetView=1`、`svc_GameEventList=1` | `evidence/source-tv-coverage/*.dem.txt` |
| 历史窗口：archive=96、ticks=[56148..129125]、medianGap=822、maxGap=1180；live=1 tick=129210 packets=68；采样 512：exact=1 checkpoint=511；staleness worst=1139 mean=407.5 | `evidence/history-coverage/bagel.txt`（另有 `bagel-summary.txt`） |
| observer 焦点：tick=129277 status=available resolved=129277 mode=4 targetIndex=3 follows=1 | `evidence/observer-focus/bagel-row.txt` |
| POV 对照：tick 51900/53400 → checkpoint，resolved 51742/53075（stale 158/325） | `evidence/observer-focus/pov-row.txt` |
| snakewater BSP：`team_control_point` x5、`team_control_point_master` x1、`tf_gamerules` x1、`team_round_timer` x1、**无** `team_train_watcher` | `evidence/t65-minimap/data/cp_snakewater_final1.bsp.probe.json` |

---

## F. 已检查且不存在的（negative results，防止"默认 0"）

| 检查 | 结果 |
|---|---|
| `native/` 内 `player_death`/`teamplay_round`/`teamplay_point`/`teamplay_cart` | **0 命中** |
| `native/` 内 `team_control_point`/`team_train_watcher`/`m_iRoundState` | **0 命中** |
| `native/` 内 userinfo/player_info 解码 | **无**（唯一 `UserInfoRefused` 是无关 logstf 文本） |
| `evidence/` 内 `player_death` | **0 命中**（没有任何事件内容证据） |
| `evidence/t63-sourcetv` | **不存在**（共享样本清单未冻结） |
| 磁盘 `pl_*` demo | **无**（cart 无样本） |
| `ent-oracle` 事件 dump 模式 | **无**（`src/main.rs` 213 行，仅实体 Dump；examples 只有 `allprops.rs`） |

---

## G. 工具与可执行物（静态事实，未运行）

| 工具 | 能力/限制 |
|---|---|
| `D:/TF2_Native_Test/native/build-nmake/demo_open_probe.exe`（Oct 24 10:21 构建） | 打印 `setview_count=`（`tools/demo_open_probe.cpp:90`）、`camera_track=`（`:172`）、`camera_track_dropped=`（`:173`）；**未运行** |
| `D:/TF2_Demo_Player_Deliverable/tools/ent-oracle` | 实体 dump（Dump analyser），无事件模式 |
| 参考 crate `D:/TF2_Demo_Player/work/_refs_demostf` | 有完整 GameEvent/GameEventList 解析实现；examples 只有 `allprops.rs` → P2 需新增 `dump_events.rs` |
