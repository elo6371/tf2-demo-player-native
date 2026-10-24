# EVENT-LIST — T64 事件清单（名称、字段、tick、状态）

**读法**：事件 id（9-bit `GameEventTypeId`）由每个 demo 自己的 `svc_GameEventList` 定义（每 demo 仅 1 条，见 `evidence/final/bagel.txt` 的 `svc_GameEventList=1`），**不是固定常量**。下表按事件名组织；字段名/类型来自参考解析器的生成定义（`D:/TF2_Demo_Player/work/_refs_demostf/src/demo/gameevent_gen.rs`）。"状态"一列全部是 **NOT VERIFIED**，因为产品没有解码过任何一条事件内容（`native/src/demo_header.cpp:790-796`）。

状态图例：`COUNT-ONLY` = 只有总数读数；`NOT VERIFIED` = 无解码样本；`ABSENT-IN-SAMPLE` = 样本里确实没有（≠不存在）。

---

## 1. 击杀 / 死亡族

| 事件 | 定义行 | 字段（wire key） | tick | 身份 | 同 tick 帧 | 状态 |
|---|---|---|---|---|---|---|
| `player_death` | 18165（结构 1165） | userid u16、victim_entindex u32、inflictor_entindex u32、attacker u16、weapon string、weapon_id u16、damage_bits u32、custom_kill u16、assister u16、weapon_log_class_name string、stun_flags u16、death_flags u16、silent_kill bool、player_penetrate_count u16、assister_fallback string、kill_streak_* u16、duck_streak_* u16、rocket_jump bool、weapon_def_index u32、crit_type u16 | 包 tick（精确） | userid（无名字，G5）；victim/attacker/inflictor 是 entindex（可关联实体状态） | 只能取 ≤tick 的 checkpoint（G8/G11） | **NOT VERIFIED** |
| `player_team` | 18161（1064） | userid u16、team u8、oldteam u8、disconnect bool、autoteam bool、silent bool、name string | 包 tick | userid + name（name 在事件内，但产品不解析） | 同上 | **NOT VERIFIED** |
| `player_spawn` | 18173（1537） | userid u16、team u16、class u16 | 包 tick | userid | 同上 | **NOT VERIFIED** |

真实计数（只到消息级）：bagel `events=12982`、snakewater `events=10703`（`evidence/camera-ui/e2e.driver.txt:3`、`evidence/entity-material/gate.txt:41`）。**无法从计数推出各事件名数量。**

## 2. 回合开始/结束族

| 事件 | 定义行 | 字段 | tick | 身份 | 状态 |
|---|---|---|---|---|---|
| `teamplay_round_start` | 18295（4113） | full_reset bool | 包 tick | 无（全局） | **NOT VERIFIED** |
| `teamplay_round_win` | 18313（4429） | team u8、winreason u8、flagcaplimit u16、full_round u16、round_time f32、losing_team_num_caps u16、was_sudden_death u8 | 包 tick | team（数字队伍） | **NOT VERIFIED** |
| `teamplay_game_over` | 18327 | 见生成定义 | 包 tick | 无 | **NOT VERIFIED** |

实体侧替代/补充（可读，但产品未与事件绑定）：`DT_TeamplayRoundBasedRules.m_iRoundState` Int（`oracle-all-classes.txt:22475`）、`m_iWinningTeam`（`:22477`）、`CTeamRoundTimer`（`:14922`）。

## 3. 控制点变化族

| 事件 | 定义行 | 字段 | tick | 身份 | 状态 |
|---|---|---|---|---|---|
| `controlpoint_updateowner` | 18281（3835） | index u16 | 包 tick | index（控制点序号） | **NOT VERIFIED** |
| `teamplay_point_startcapture` | 18335 | 见生成定义 | 包 tick | cp/team（按定义） | **NOT VERIFIED** |
| `teamplay_point_captured` | 18337（4936） | cp u8、cpname string、team u8、cappers string | 包 tick | cp + team + cappers | **NOT VERIFIED** |
| `teamplay_point_locked` | 18339 | 见生成定义 | 包 tick | cp | **NOT VERIFIED** |
| `teamplay_point_unlocked` | 18341 | 见生成定义 | 包 tick | cp | **NOT VERIFIED** |
| `controlpoint_starttouch` / `controlpoint_endtouch` | 18283 / 18285 | 见生成定义 | 包 tick | cp + player | **NOT VERIFIED** |

实体侧：`CBaseTeamObjectiveResource.m_iOwner.000`（`oracle-all-classes.txt:4255`）、`m_iNumControlPoints`（`:4273`）。snakewater 的 BSP 有 5 个 `team_control_point`（`evidence/t65-minimap/data/cp_snakewater_final1.bsp.probe.json`）。

## 4. 推车进度/检查点族（**缺样本**）

| 事件 | 定义行 | 字段 | tick | 状态 |
|---|---|---|---|---|
| `cart_updated` | 18249（3244） | **空结构（无字段）** | 包 tick | **NOT VERIFIED（G2 缺 payload demo）** |
| `payload_pushed` | 18823（15711） | pusher u8、distance u16 | 包 tick | **NOT VERIFIED（G2）** |

实体侧：`CTeamTrainWatcher.m_flTotalProgress` Float [44]（`oracle-all-classes.txt:14984`）、`m_iTrainSpeedLevel`、`m_nNumCappers`。**snakewater（5CP）BSP 无 `team_train_watcher`**，即使拿 snakewater 也验证不了 → 必须 `pl_*` demo。

## 5. 导演相机 / 视角变化族

| 来源 | 位置 | 内容 | tick | 状态 |
|---|---|---|---|---|
| `svc_SetView`（type 18） | `demo_header.cpp:1731-1737`、调用 `:2209` | 11-bit entityIndex；只保留 `lastViewEntity` | 包 tick | **NOT VERIFIED（无历史）**；样本 `setview=1` → **ABSENT-IN-SAMPLE**（只有 1 条，无切换） |
| `svc_FixAngle`（type 19） | `demo_header.cpp:644-676`、调用 `:2211` | 角度样本进 `viewSamples`；tick 用 `networkTick` | 包/网络 tick（域未验证，G7） | **NOT VERIFIED**；bagel `fixangle=0` |
| 每包 cmdinfo | `demo_header.cpp:2280-2292` | origin/angles 样本；tick=entry.tick | 包 tick | `VERIFIED-VALUE`（真实读数 `views=65536`/64333）但 **cap 65536 打满**（G12） |
| observer 实体属性 | `entity_model.cpp:270-295`；`oracle-all-classes.txt:3255/3256` | `m_iObserverMode`、`m_hObserverTarget` | 查询 tick 的实体状态 | `VERIFIED-VALUE`（`observer-focus/bagel-row.txt`） |

**结论**：这两个 SourceTV 样本里"导演切镜"作为 SetView 事件流 **不存在**（每 demo 1 条，且无历史）；可观察的相机变化信号是 observer target（mode 4/5）与每包 cmdinfo 姿态，不是 SetView 序列。

---

## 6. 排序 / 重复 / 缺失（能否稳定提供）

静态可得的结构事实：
1. **事件在包内按消息顺序出现**；一个包可含多条 `svc_GameEvent`（消息循环 `demo_header.cpp:2191-2219`），因此同一 tick 多事件是常态。
2. **demo 包 tick 单调递增**（命令索引 entry.tick），但同一 tick 可有多条命令/消息 → 事件时间戳会重复，**不能只按 tick 排序**。
3. `svc_GameEventList` 每 demo 仅 1 条（`svc_GameEventList=1`）→ 定义表可一次性解析并缓存；事件 id 只在 demo 内有效。
4. **重复/缺失/乱序无法验证**（G9）：没有解码样本；产品只有 `gameEventCount`（`demo_header.cpp:793`）。
5. 建议去重键：`(demoTick, packetOrdinal, messageBitOffset)`（后两者在解码时可取：`messageBit` `demo_header.cpp:2192`，packetOrdinal 见 `EntityHistoryPacket` `demo_header.h:308-315`）。没有这个键就无法区分"两个真实事件"与"同一事件重复投递"。

**能稳定提供的**（若 P2/P3 落地）：事件名、绝对 demo tick、字段值、包内顺序、每 tick 多条。
**不能稳定提供的**：玩家名（G5）、cart（G2）、SetView 变化列表（无历史）、同 tick 精确帧（G8/G11）、相机晚段姿态（G12）。
