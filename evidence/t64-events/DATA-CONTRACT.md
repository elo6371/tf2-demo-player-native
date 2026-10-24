# DATA-CONTRACT — 点击导航最小数据契约（T64）

**目标**：让"点一条事件 → 跳到该时刻的画面"这件事在数据层**可判定、可降级、不撒谎**。本契约只描述数据结构与不变量，不引入任何运行时实现；所有字段都标了来源，来源不存在时字段必须为 `null` + `reason`，**禁止默认 0**。

## 0. 三条硬原则

1. **事件 tick 与帧 tick 分开**：事件 tick 精确（包 tick），帧 tick 只能 ≤ 事件 tick（checkpoint），两者差值必须显式给出（`stalenessTicks`）。
2. **absent ≠ 0**：缺样本（cart）、缺解析（玩家名）、缺样本覆盖（相机 cap）都必须 `null` + `reason` 枚举，不能填 0 或空串。
3. **不承诺同 tick 帧**：任何 UI/接口文案不得写"跳到该 tick 的精确画面"；只能写"跳到 ≤ 该 tick 的最近帧（陈旧 N tick）"。

## 1. 顶层对象

```json
{
  "schema": "tf2.t64.nav/1",
  "demo": {
    "path": "D:/TF2_Demo_Player/testdata/demos/4a9bfb9276509d0ec5f5fdc722a95b17_match-20260927-0239-koth_bagel_rc13.dem",
    "md5": "4a9bfb9276509d0ec5f5fdc722a95b17",
    "recording": "SourceTV",
    "map": "koth_bagel_rc13",
    "ticks": 77466,
    "tickRate": 66.666667,
    "firstPacketTick": null,
    "firstPacketTickOrigin": "unresolved",
    "events": []
  }
}
```

| 字段 | 必填 | 类型 | 来源 | 说明 |
|---|---|---|---|---|
| `demo.md5` | 是 | string | 文件 | 样本身份（T63 清单未冻结，用 md5 固定） |
| `demo.ticks` | 是 | int | header（`demo_header.h:40`） | 总 tick |
| `demo.tickRate` | 是 | double | `demoTickRate`（`demo_header.cpp:2331-2340`） | 显示时间换算 |
| `demo.firstPacketTick` | 是 | int 或 null | `main.cpp:5064-5067` 或 `demo_header.cpp:2323-2329` | **必须记录用的哪个原点**；未测出时 null + origin=`unresolved`（G6） |
| `demo.recording` | 是 | enum | `classifyDemoRecording`（`demo_header.cpp:2805+`） | SourceTV/POV/Unknown |

## 2. 事件记录

```json
{
  "seq": 0,
  "name": "player_death",
  "eventId": 17,
  "tick": 12345,
  "packetOrdinal": 678,
  "messageBit": 1234,
  "displaySeconds": 185.175,
  "fields": { "userid": 12, "victim_entindex": 45, "attacker": 3, "weapon": "tf_projectile_rocket" },
  "actors": [
    { "role": "victim", "entindex": 45, "userid": null, "name": null, "nameReason": "no-userinfo" },
    { "role": "attacker", "entindex": 3, "userid": null, "name": null, "nameReason": "no-userinfo" }
  ],
  "frame": { "status": "checkpoint", "tick": 12100, "stalenessTicks": 245, "stalenessSeconds": 3.675, "source": "entity-history" },
  "camera": { "status": "stale", "tick": 12340, "stalenessTicks": 5, "lastSampleTick": 65535, "reason": null }
}
```

| 字段 | 必填 | 说明 |
|---|---|---|
| `seq` | 是 | 输出顺序（0 起），用于稳定排序 |
| `name` | 是 | **来自 demo 自己的 `svc_GameEventList`**（G4）；未知 id → `name=null` + `nameReason="unknown-event-id"` |
| `eventId` | 是 | 9-bit `GameEventTypeId`（demo 内有效，跨 demo 不可复用） |
| `tick` | 是 | **绝对 demo tick，精确**（包 tick；参考 `packet/mod.rs:42`、`analyser.rs:416`） |
| `packetOrdinal` | 是 | 包序号（实体历史同款，`demo_header.h:308-315`） |
| `messageBit` | 是 | 包内消息起始 bit（`demo_header.cpp:2192`），去重键的一部分 |
| `displaySeconds` | 是 | `(tick - firstPacketTick) / tickRate`；firstPacketTick 为 null 时该字段 null |
| `fields` | 是 | wire 字段原样（按定义）；**字段缺失 = null，不是 0** |
| `actors` | 否 | 由 entindex/userid 派生；`name` 永远 null + `nameReason="no-userinfo"`（G5）直到补 userinfo |
| `frame` | 是 | 见 §3；不能省略 |
| `camera` | 否 | 见 §4；没有视角样本时 `status="missing"` |

**去重键**：`(tick, packetOrdinal, messageBit)` 唯一。没有这个键就无法区分"两个真实事件"和"重复投递"（G9）。

## 3. 帧记录（实体快照）

```json
{ "status": "checkpoint", "tick": 12100, "stalenessTicks": 245, "stalenessSeconds": 3.675, "source": "entity-history" }
```

`status` 枚举（与 `demo_header.h:420-423` 对齐）：

| status | 含义 | tick | stalenessTicks |
|---|---|---|---|
| `exact` | 该 tick 有 live 快照（仅 bagel tick ≥ 129210 的最后 ~68 包） | = event.tick | 0 |
| `checkpoint` | 取到 ≤ event.tick 的归档 checkpoint | < event.tick | > 0（实测 bagel 中位 822、最坏 1180） |
| `gap` | 历史有缺口 | null | null |
| `before-history` | tick < 历史起点 | null | null |
| `unavailable` | 无历史/查询失败 | null | null |

不变量：
- `frame.tick <= event.tick`；
- `status == "exact"` ⇔ `frame.tick == event.tick && stalenessTicks == 0`；
- `status != "exact"` 时 `stalenessTicks = event.tick - frame.tick`（若 tick 非 null），**必须显示**；
- 实测依据：`evidence/history-coverage/bagel.txt`（exact=1/512、medianGap=822、maxGap=1180、dropped=172）；snakewater 未采样（G8）。

## 4. 相机记录

```json
{ "status": "stale", "tick": 12340, "stalenessTicks": 5, "lastSampleTick": 65535, "reason": null }
```

| status | 含义 |
|---|---|
| `exact` | `sample.tick == event.tick` |
| `stale` | `sample.tick < event.tick`（at-or-before 语义，`demo_header.cpp:2795-2803`） |
| `missing` | 无样本，或 `event.tick > lastSampleTick`（cap 65536 之后，G12） |

硬规则：
- `event.tick > lastSampleTick` → `status="missing"`、`stalenessTicks=null`、`reason="view-sample-cap"`；**不得**沿用最后一条样本冒充（当前 `findObserverViewAtOrBeforeTick` 正是这样静默返回的）。
- bagel 实测 `views=65536` = cap（`e2e.driver.txt:3`）；snakewater `views=64333` 未打满。
- FixAngle 样本的 tick 域未验证（G7）→ 若 `source="FixAngle"`，必须带 `tickDomain:"network"` 并禁止与 `entry.tick` 直接比较。

## 5. 显示时间

- `displaySeconds = (tick - firstPacketTick) / tickRate`（`main.cpp:5064-5067` + `demo_header.cpp:2331-2340`）。
- 绝对时间 `absoluteSeconds = tick / tickRate`（与 header duration 同源：bagel 77466/66.666667 = 1161.99 ✓）。
- `firstPacketTick` 的两个候选（`command==1||2` vs `command==2`）在 bagel 上是否相等**未测**（G6）→ demo 块必须记录 `firstPacketTickOrigin`，禁止隐式假设。

## 6. 字段来源映射（逐字段可溯源）

| 契约字段 | wire/源码来源 |
|---|---|
| event.name / eventId / fields | `svc_GameEventList`（`demo_header.cpp:807-815` 当前只跳过；参考 `gameevent.rs:163-185`）+ `svc_GameEvent`（`demo_header.cpp:790-796`；参考 `gameevent.rs:20-51`） |
| event.tick | `entry.tick`（`demo_header.cpp:2296`）；参考 `packet/mod.rs:42`、`analyser.rs:416` |
| frame.* | `queryEntitySnapshotAtOrBeforeTick`（`demo_header.cpp:2380`）、状态 `demo_header.h:420-423`、丢包计数 `demo_header.cpp:1146/1160/1186` |
| camera.* | `viewSamples`（`demo_header.h:455`）、cap `demo_header.cpp:2288/666`、查询 `demo_header.cpp:2795-2803` |
| actors.entindex/userid | `player_death` 等字段（`gameevent_gen.rs:1165` 等） |
| actors.name | **无来源**（G5）→ null + reason |
| cart progress | **无来源**（G2）→ null + reason，禁止用 `m_flTotalProgress` 假装已验证（该属性存在但无 payload 样本） |

## 7. 必须拒绝/降级的输出

| 场景 | 规则 |
|---|---|
| 未知 event id | `name=null`、`nameReason="unknown-event-id"`，事件仍保留（不能丢） |
| 无 userinfo | `name=null`、`nameReason="no-userinfo"`，只给 entindex/userid |
| 无 payload 样本 | cart 事件 `fields=null`、`reason="no-payload-sample"`；**不输出 progress 数字** |
| tick 早于历史起点 | `frame.status="before-history"`，不给帧 |
| tick 在 cap 之后 | `camera.status="missing"`、`reason="view-sample-cap"` |
| 历史缺口 | `frame.status="gap"`，不猜帧 |

## 8. 验收用例（P4 执行，不是现在）

在 P2/P3 产出上挑 3 个真实事件 tick：
1. **回合开始**（bagel 中段，tick < 129125）→ 预期 `frame.status="checkpoint"`，staleness 可能到 1180 tick（17.7 s）。
2. **击杀**（bagel 晚段，tick ≥ 129210）→ 预期 `frame.status="exact"`（live 窗口），staleness=0。
3. **控制点占领**（bagel 早段，tick < 56148）→ 预期 `frame.status="before-history"`。
4. **相机**：任意 tick > lastSampleTick → 预期 `camera.status="missing"`、reason=`view-sample-cap`（bagel）。
验收通过条件：不变量 §3/§4 全部成立；任何缺值都有 reason；没有一处把 null 渲染成 0。
