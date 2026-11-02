# T66 — SourceTV 死亡接力镜头

日期：2026-10-25　分支：`task/t66-sourcetv`　基线：`p0-entity-protocol` 587aebb

---

## 1. 任务要求（原任务清单 §14 T66）

> 该清单（`NEXT-TASKS-2026-10-23.md`）已于 2026-10-29 随仓库清理删除。下列 5 条与下面的“验收”是按删除前原文转写的摘要，不是新写的要求；原文见删除前的 `§14 — T66 — SourceTV 死亡接力镜头`。

1. 被观察玩家死亡时，在死亡 tick 移除其模型，无 ragdoll；
2. 优先使用 demo 自带的 director/observer 目标，否则按稳定可复现的顺序挑活着的玩家，过滤无效/死亡/复用实体；
3. 自动交接 = 约 0.3 s 连续的相机位置+朝向过渡，其时钟独立于 demo 时钟（暂停时仍继续）；手动选人立即生效、无过渡；
4. 没有可用目标时保持上一个有效机位（绝不跳回世界原点、默认轨道或随机玩家）；seek/回退/变速要显式处理；
5. 覆盖：目标死亡、同 tick 多死、目标复活/复用、暂停、seek、无活人、手动打断；重复运行稳定。

验收：死亡 / 自动交接 / 手动切换 / 无目标 各自要有 trace + 帧证据；mutation 必须能抓到瞬时硬切、暂停冻结、死模型残留、无目标跳变。

---

## 2. 先量清"谁在被观察"：S1 样本的数据事实

S1 = `bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem`（SourceTV，64352 header ticks，first_server_tick 10321，last_server_tick 74668）。

任务第 2 条要求"优先用 demo 自带的 director 目标"，所以第一件事是把那个目标读出来，而不是假设它有用。用的是 `entity_model_probe --props-at <tick> --entity <n>`（注意 `--tf-root` 是必需的，否则 `native/tools/entity_model_probe.cpp:2176` 的加载分支 `if (!demoPath.empty() && !tfRoot.empty())` 直接跳过，输出 inspections=0 而不报错）。

| 读数 | 值 | 采样 tick |
| --- | --- | --- |
| `svc_SetView` 的目标 | entity **1**（SourceTV 玩家，class 247 CTFPlayer） | 全程一次 |
| entity 1 `m_hObserverTarget` | **301073**（常量） | 15000…70000 全部相同 |
| 解码 | index 17，serial 147（`NUM_ENT_ENTRY_BITS` = 11） | — |
| entity 1 `m_iObserverMode` | 7（常量） | 15000…70000 |
| entity **17**（demo 自己指定的目标） | team 1、health 200、`m_lifeState` = 2、class 3，位置 (4157.5, -2219.5, 0) | 15000…70000 全部相同 |
| 真实玩家 @30319 | entity 3 (team 2, hp 181)、5 (team 3, hp 186)、7 (team 2, hp 125) | 30319 |

结论，并且这是本任务最重要的一条前提：

> **在这份 SourceTV 录像里，demo 自带的 observer 目标是一个永久死亡的观察者槽位（entity 17）。**
> 它不是"偶尔失效"，而是从 15000 到 70000 每个采样点都一样。因此"优先用 director"必须实现成
> "director 指向活人时才用"，而不是"director 说谁就是谁"——否则相机永远停在一具尸体上，
> 而这正是任务第 4 条禁止的跳变的一种。

同一批测量还确认了三个字段都真的能读：`m_lifeState`（SendPropType::Int，T63 记为缺口 G6，属于产品没读、不是数据没有）、`m_iHealth`、`m_iTeamNum`。T63 的缺陷 F3（health/team/class 读的是 `lastViewEntity`，即 entity 1，而不是被观察玩家）在接力镜头里不再适用：这里的焦点**就是**被观察玩家本身；HUD 侧数据仍按 T63 的账本记录。

---

## 3. 决策本身（`native/include/spectate_target.h`）

接力决策被抽成一个不碰 GPU、不碰窗口、不读文件的类，理由是可测：它的每一条规则都能用一段数值快照序列断言，而不是靠人看标题栏。

声明见 `native/include/spectate_target.h:77-160`，实现在 `native/src/spectate_target.cpp`。每帧按顺序执行：

| 步 | 规则 | 代码位置 | 读数 |
| --- | --- | --- | --- |
| 1 | 尸体不进绘制集：`m_lifeState != 0` 或 `m_iHealth <= 0` 才算确定死亡 | `native/src/spectate_target.cpp:279-282`（`hidesModel`） | `deadIndices`、`hiddenDead` |
| 2 | director 句柄只在指向**活着的非观察者**时可用 | `native/src/spectate_target.cpp:107-120` | `directorPicks` |
| 3 | 已锁定目标死了 → 计一次死亡（同一次死亡只计一次） | `native/src/spectate_target.cpp:122-142` | `deaths`、`lastDeathTick` |
| 4 | 手动选人本帧生效，并刷新 alive/present 快照 | `native/src/spectate_target.cpp:144-178` | `manualPicks`、`manualRejected` |
| 5 | 目标选择：director → 仍活着就留 → 稳定顺序 | `native/src/spectate_target.cpp:180-192` | `source` |
| 6 | 全灭：保持上一机位，没有原点/轨道/随机回退 | `native/src/spectate_target.cpp:194-206` | `noTargetFrames` |
| 7 | 换人 → 开一个 0.3 s 墙钟窗口 | `native/src/spectate_target.cpp:208-239` | `handovers` |
| 8 | 每帧推进焦点（窗口内 smoothstep，端点跟随目标移动） | `native/src/spectate_target.cpp:241-271` | `transitionProgress`、`focus` |

"稳定顺序"的定义：**entity index 升序，从死者之后绕回**；跳过无效、死亡、观察者和没有 origin 的槽位。同一段录像重跑两次必须给出同一个序列（self-test S10 + probe 的 REPEAT-IDENTICAL）。

"活着"的定义（`slotAlive`）：`m_lifeState == 0` 且 `m_iHealth > 0`；没有 `m_lifeState` 的帧退回只看 health，**没有读数不等于死亡**。这一条是被实测逼出来的：S1 开场有 7 行玩家槽位还没解析出状态，若把"读不到"当死亡，第一帧就有 7 具假尸体被隐藏（早期版本确实这样，self-test 的 S2b 现在专门断言这一点）。

---

## 4. 交接窗口：0.3 s 墙钟，与 demo 时钟完全分开

任务第 3 条要求"过渡时钟独立于 demo 时钟（暂停时仍继续）"。实现上不是一个开关，而是接口形状决定的：

- `update(double nowSeconds, ...)`（`native/include/spectate_target.h:91-94`）的 `nowSeconds` 只接受单调墙钟，**从不**与 tick 比较、也从不从 tick 推导；
- 产品侧传的是 `native/src/main.cpp:8914` 的 `QueryPerformanceCounter / QueryPerformanceFrequency`；
- `kHandoverSeconds = 0.3`（`native/include/spectate_target.h:80`）只在这里拼写一次，进度、结束判定和 `transitionProgress()` 都从它算。

因此"暂停时交接仍然走完"不是靠特判，而是靠**同一 tick 传入两次、只推进墙钟**就能断言。self-test S2 的四条断言就是这件事：

- `S2 the handover starts from where the camera was instead of cutting to the new player`
- `S2 the focus keeps moving toward the new target while the tick is frozen`
- `S2 the window is still open before 0.3 s`
- `S2 the window closes once 0.3 s of wall time have passed`

`kSeekGapTicks = 200`（`native/include/spectate_target.h:85`）区分 seek 与普通帧：66 tick/s 下正常帧只动几个 tick，200 远高于 8× 播放的帧距，所以变速不会被误判为 seek（实测：8× 下 `resync=0`；16× 下曾出现 `resync=2`，见 §9）。

---

## 5. 产品接线（`native/src/main.cpp`）

| 位置 | 内容 |
| --- | --- |
| `:493-503` | 全局：`g_spectateDirector`、`g_spectateManualTarget`、`g_spectateArgumentRejected`、暂停三件套 |
| `:3455-3491` | 四个开关：`--spectate-target <n>`（0..2047，越界计 `rejectedFlags`）、`--spectate-pause-at <playback tick>`、`--spectate-pause-seconds <s>`、`--spectate-show-corpses`（后三个只为门禁与诊断存在） |
| `:8912-8914` | 接力状态提到 tick 块之外（`spectateSlots` / `spectateDirectorHandle` / `spectateSceneTick`）——这是"暂停不冻结接管"的结构前提 |
| `:8955` | `wallSeconds`（`QueryPerformanceCounter / QueryPerformanceFrequency`；与 tick 无关） |
| `:9270-9280` | 绘制候选集过滤：按 `hidesModel` 剔除尸体，集合变化时打印 `scene: corpseFilter removed=<n> indices=<list> candidates=<n> submitted=<n>` |
| `:9347-9404` | 每帧构造 `SpectateSlot` 向量、读 `m_hObserverTarget` 句柄、记录 `spectateSceneTick` |
| `:10331-10378` | **每帧**决策：`update(wallSeconds, …)`、把 `focus` 交给渲染器、按变化打印 `spectate: tick=…`；暂停/恢复的 trace 也在这里 |
| `:9408` | `bool focused = g_spectateDirector.focusValid();`——接力机位有效时禁用旧的"始终看 lastViewEntity"回退，否则回退会覆盖接力机位 |
| `:10645` | `spectate: summary rejectedFlags=… hiddenInstances=…` + `traceFields()` |

一处结构性修正：尸体过滤原来写在两个绘制循环内部（两处 `continue`），导致"隐藏了几个实例"只能靠计数推断；现在改成对**候选集**先过滤再进入预算/焦点/绘制三步，于是 trace 能直接说出"哪几个 entity index 离开了提交集"。这一改动也让 §7 的负控变成可能（规则关掉时 `corpseFilter` 行数必须为 0）。

---

## 6. 实测读数

### 6.1 探针重放（`spectate_probe --demo ... --from-tick 10321 --to-tick 20000 --step 5`）

```
demo: frames=1936 deaths=3 handovers=3 director=0 stable=4 manual=0 rejected=0 notarget=0
      resync=0 hidden=15 transitions_open=3 transitions_closed=3 origin_focus_hits=0
      final_target=6 focus_valid=1
SPECTATE-DEMO=PASS frames=1936
REPEAT-IDENTICAL=1
```

- 3 次死亡、3 次交接、3 个窗口全部闭合；
- `director=0`：demo 自带的 observer 目标在这段录像里一次都没被采用（见 §2，它指向永久死亡的槽位）；
- `notarget=0`：任何一帧都没有出现"无目标"；
- `origin_focus_hits=0`：焦点从未落在世界原点；
- 同样的窗口重跑一次，输出逐字节相同。

### 6.2 产品（8× 播放，capture-tick 19700，`--hidden-window --audio-disabled`）

```
spectate: summary rejectedFlags=0 corpses=0 hiddenInstances=8045 target=6 src=stable tr=0
          prog=0.000 focus=3435.5,-1305.4,67.6 deaths=3 handovers=3 notarget=0 resync=0
          manual=0 rejected=0 director=0 stable=4 hidden=8 lastdeath=19505
scene: corpseFilter removed=2 indices=16,17 candidates=466 submitted=468
scene: corpseFilter removed=3 indices=4,16,17 ...
```

目标和探针一致（2 → 3 → 4 → 6），死亡 3 次，无 seek，无空目标。`hiddenInstances` 在 8000–8100 之间随帧数浮动，因为产品跑到录像结束才退出（`--capture-tick` 只决定截图时刻，不决定退出时刻——这是本轮踩到的一个坑，写进了 gate 的注释里）。

### 6.3 交接形状（`spectate-transitions.py`）

| 交接 | tick 区间 | 采样点 | 总位移 | 单步最大占比 | 单调 | 起点/终点间隙 |
| --- | --- | --- | --- | --- | --- | --- |
| n=1 | 13621→13787（166 tick ≈ 0.31 s） | 14 | 4495.3 | 0.134 | 1 | 0.0 / 0.0 |
| n=2 | 15908→16073（165 tick） | 16 | 983.1 | 0.135 | 1 | 0.0 / 0.0 |
| n=3 | 19500→19667（167 tick） | 17 | 776.8 | 0.133 | 1 | 0.0 / 0.0 |

0.31 s 与任务要求的 0.3 s 吻合；单步最大占比 ≈ 0.13，说明它是连续插值而不是一次跳变；`startgap=endgap=0` 说明起点确实在旧位置、终点确实落在新目标上。

一个测量工具自身的坑：第一版分析器把"交接起点"取成"交接开始前的最后一行 trace"，但产品只在变化时打印，前一行可能是几千 tick 之前的旧位置，于是算出 `maxstep=0.939 startgap=2182.3` 这种"产品硬切"的假象。正确起点是同一条 `tr=1 prog=0` 行（它就在死亡 tick 打印）。修正后才有上表的读数。

### 6.4 手动选人

`--spectate-target 6` 的第一帧：

```
spectate: tick=10320 dead=1,12,14,16,17,18,19 corpses=0 target=6 src=manual tr=0 prog=0.000
          focus=755.2,269.0,64.0 deaths=0 handovers=0 notarget=0 resync=0 manual=1 ...
```

同一帧到位、`src=manual`、没有打开过渡窗口；该次运行里之后的 3 次交接全部来自真实死亡（n=1@13102、n=2@19499、n=3@19955）。

这个行为是本轮改出来的，改动前它是错的：手动块只改了 `target_`，没有刷新第 3 步算出的 `targetAlive` / `targetPresent`，于是第 5 步看到"目标是死的"，转去 `pickStable` 选了别人，第 7 步发现目标变了就开了一个 0.3 s 过渡——**手动选人被自动规则抢回去并动画了一次**。修复是在手动块末尾补 `targetPresent/targetAlive/targetSlot` 三行刷新（`native/src/spectate_target.cpp:164-172`），mutation m3 就是把这处退回修复前，见 §8.2。

### 6.5 暂停中的接管（本轮发现并修掉的真缺陷）

决策原先写在 `if (g_renderer && g_playback.enabled && g_playback.tick != lastSceneTick)` 里，即"tick 变了才决策"。播放暂停时 tick 不动，接管就停在原地——0.3 s 的过渡窗口被冻住，恢复播放后才继续走。这个缺陷在别处读不出来：所有随 tick 变化的读数（死亡数、接管数、目标、seek）在暂停时本来就不该变，**八段门禁全绿**。

修复是把决策从 tick 块里提出来，每帧跑一次、时钟用墙钟（`wallSeconds` 来自 `QueryPerformanceCounter`，与 tick 无关）。直接证据是同一 scene tick 上的多条 trace：

```
spectate: pause-at tick=3320 wall=20896.364504 seconds=2.000
spectate: tick=13640 ... tr=1 prog=0.130 focus=...
spectate: tick=13640 ... tr=1 prog=0.212 focus=...
spectate: tick=13640 ... tr=1 prog=0.278 focus=...
...                                    （共 14 行，14 个不同的 prog）
spectate: tick=13640 ... tr=0 prog=1.000
spectate: resume-after tick=3320 wall=20898.365381
```

这里的 `tick` 是 scene tick（不是 playback tick），14 行完全相同；`prog` 从 0.130 涨到 1.000 并在暂停窗口内闭合。2.0 s 的冻结对应墙钟 20896.364 → 20898.365。`--spectate-pause-at` / `--spectate-pause-seconds` 这两个开关只为这条断言存在，不是产品功能；门禁段 8/8 用的就是它，变异 m6 把墙钟换回 `sceneTick / 66.0` 来验证这一段确实在测这件事。

---

## 7. 画面证据：为什么"像素差 = 0"是对的

任务的死亡条款是"在死亡 tick 移除其模型"，最直接的证据看起来是"尸体从画面消失"。实测下来这条路走不通，而**走不通本身是应当记录的结论**：

- 在死亡帧（playback 3300 / 3681）分别用规则开、规则关各拍一张，`PIXELS=0`；
- 但同一帧 `hiddenInstances=1062 / 1277`（规则开）对 `0`（`--spectate-show-corpses`）——规则确实每帧都在移除实例；
- 原因：相机这时在 target 3 (3593.1, -1184.6, 8.0)，而死者在 (-626.7, 364.6, -24.0)，相距约 4200 单位，根本不在 1264×681 画面里。**看不见的东西被移除，像素不会变。**

所以证据换成了能说出名字的读数：`scene: corpseFilter removed=<n> indices=<list>`，它直接列出离开提交集的 entity index。死亡玩家的 index 出现、复活后消失，这就是"模型在死亡 tick 离开"的直接证据，而不是间接推断。

像素工具没有被丢掉，它改作负控：**没人死亡的帧上，规则开与关必须逐像素相同**（`PIXELS=0`，尺寸 1264×681×24bpp）。这条同时证明了两件事：规则没有误伤活人，以及"PIXELS=0"这个读数在当前相机下不是必然为 0（否则它证明不了任何东西）。

---

## 8. 门禁

门禁是一条命令：`bash spectate-check.sh`。它先在开头验新鲜度（`BINARY-FRESH=1`：两个 exe 都必须比四个源码文件新），再跑负控，然后九段正测；末行打印 `SPECTATE-CHECK=PASS|FAIL`，退出码即结论。`verify-all.sh` 的 27/27 步调用的就是它，并把断言总数钉死在 71。

| 段 | 覆盖的条款 | 断言数 | 关键读数 |
|---|---|---|---|
| 0/8 | 二进制新鲜度 | 1 | `BINARY-FRESH=1` |
| 1/8 | 负控：缺 demo / 反向窗口 | 2 | 退出非 0 且打印 `FAIL demo header`；退出 2 且打印范围句子 |
| 2/8 | 自测：八步决策的四个手工场景 | 4 | `SPECTATE-SELF-TEST=PASS checks=49 failures=0` |
| 3/8 | demo 回放：死亡 / 接管 / 切换 / 无目标 / seek | 11 | `frames=1936 deaths=3 handovers=3 notarget=0 resync=0 director=0`，重复运行逐字节相同 |
| 4/8 | 产品：死亡 tick 移除模型 | 12 | `scene: corpseFilter removed=2 indices=16,17 …`，`hiddenInstances=8100` |
| 5/8 | 接管形状：约 0.3 s 的连续移动而非硬切 | 14 | 三次接管 `samples=14/16/17 maxstep=0.134/0.135/0.133 monotone=1 endgap=0.0` |
| 6/8 | 手动选人：立即、无过渡 | 10 | 首帧即 `target=6 src=manual tr=0`，首次死亡前 `handovers=0`；越界 `--spectate-target 5000` 被拒 |
| 7/8 | 画面负控：规则不碰活人帧 | 6 | `PIXELS=0 WORST=0 SIZE=1264x681 BPP=24`，关规则时 `hiddenInstances=0` |
| 8/8 | 暂停冻结：接管时钟独立于 demo 时钟 | 11 | 暂停 2.0 s 内同一 scene tick 14 行、14 个不同 `prog=`、其中 1 个 `tr=0` |
| 合计 | | **71** | `SPECTATE-CHECK=PASS`（2026-10-25 15:59:02，owner `t66-baseline`） |

段 5 之所以单独成段：一次硬切的两端和一次正常接管的两端完全相同，只有中间样本能把它们区分开，所以"瞬时切换"这个缺陷在第 0-4 段全绿，只在第 5 段变红。段 8 同理：其余七段读的都是随 tick 变化的量，接管的时钟一旦换回 demo 时钟，那七段全部照绿——这一条是这一轮实际发现的缺陷（见 §6.5）。

### 8.1 负控

负控不通过时不是"少一个断言"，而是明确的红：

- `spectate_probe --demo /nonexistent.dem` → 退出非 0 且打印 `FAIL demo header`；
- `spectate_probe --from-tick 20000 --to-tick 10000` → 退出 2 且打印 `FAIL range --from-tick 20000 is past --to-tick 10000: …`（这个守卫是本轮新加的：之前退 1 打 `SPECTATE-DEMO=FAIL frames=0`，不说是谁的错）；
- `--skip-negative-controls` 时脚本打印 "a skipped control is NOT RUN, not a pass"。

### 8.2 变异

六个用例，每个都必须让一条**点名的**断言变红；变异体编译不过记 INVALID（编译不过的变异体不证明任何事）。

| 用例 | 变异 | 条款 | 期望变红的点名断言 | 结果 |
|---|---|---|---|---|
| m1 | `native/include/spectate_target.h:80` 交接窗口 0.3 → 0.0（瞬时硬切） | 3（自动交接必须连续过渡） | 段 5 `handovers analysed` | **红**：`FAIL handovers analysed = 0, expected 3`（段 4、5 同时红） |
| m2 | `native/src/spectate_target.cpp:281` `hidesModel` 恒 false（尸体不离开绘制集） | 1（死亡 tick 移除模型） | 段 4 `instances the corpse rule removed` | **红**：`= 0, expected >= 100`，另 3 条（发过筛选行的帧数、首/末尸体名单） |
| m3 | `native/src/spectate_target.cpp:170-172` 退回旧接线（手动选的人被自动规则抢回） | 3（手动选人立即生效、无过渡） | 段 6 `handovers that happened before the first death in the manual run` | **红**：`= 3, expected 0`；首帧就是 `target=7 src=stable tr=1` |
| m4 | `native/src/spectate_target.cpp:118` 句柄不校验（`directorUsable = true`） | 2（过滤无效/死亡的实体） | 段 3 `the director handle was never usable on this demo` ＋ 段 4 产品侧 `the product never used the demo director handle either` | **红**：`= 1, expected 0`；连带 `replayed deaths = 1, expected 3`、`product handovers = 0, expected 3`——信任句柄会把镜头停在一具尸体上 |
| m5 | `native/src/spectate_target.cpp` 无目标时把焦点设成世界原点 | 4（没有可用目标就保持上一个有效位置） | 段 2 `S4 the camera never falls back to the world origin` | **红**（第二轮）：首轮「红了但没点名」，门禁只打印 `self-test failures = 2`；补上自测失败行回显后点名 |
| m6 | `native/src/main.cpp:10351` 墙钟 → `sceneTick / 66.0` | 3（交接时钟独立于 demo 时钟） | 段 8 `handover lines the product printed while the clock was frozen` | **红**：`= 1, expected >= 8`；另 `distinct handover progress values = 1, expected >= 5` |

六例全部点名变红并逐字节还原（`cmp -s` 与 `spectate-patchlib.py verify` 双证），每个变异体都重新编译过：编译不过记 `MUTATION-INVALID`，不证明任何事。

这套变异跑了两遍，第一遍（15:22–15:38）是 4/4 抓到 + m4 GREEN + m5 MISS + 收尾基线假红。三处红都是**门禁/变异体自己的错**，不是产品的错，值得写下来：

- **m4 是「什么都没改的变异体」**：它只删 `slotAlive`，保留了 `slotObservable`；而这份录像的导演句柄指向观察者队（`team=1`），`slotObservable` 里 `team < 2` 的守卫独立拦住了它（`native/src/spectate_target.cpp:45-52`）。行为没变，自然全绿。改成 `directorUsable = true`（彻底不校验）后立刻点名变红。
- **m5 是「红了但没点名」**：世界原点规则只有自测能触达（这份录像里没有「无目标」帧），而门禁在自测失败时只报 `failures=2` 这个计数、不回显 `FAIL S4 …` 那一行。门禁现在失败了就把自测的 `^FAIL ` 行全部回显。
- **收尾基线是假红**：新鲜度规则原先要求每个源文件都比两个 exe 新，但 `main.cpp` 只喂产品、不喂探针，产品重链接后探针合法地更旧，于是 `STALE-BINARY=1` 误报。改成每个 exe 只跟它真正编译的源比（探针：`spectate_target.cpp/.h` + `tools/spectate_probe.cpp`；产品：`spectate_target.cpp/.h` + `src/main.cpp`）。

三处修好后门禁断言数 70 → 71（段 4 增加产品侧「从未使用 demo 导演句柄」一条），第二遍 m4/m5 都点名变红，收尾基线 **71 OK / 0 FAIL**。

### 8.3 复现

```
bash spectate-check.sh                      # 九段，约 7 分钟，8x 回放
SPECTATE_SECTIONS=8 bash spectate-check.sh  # 只跑暂停段，约 90 秒
bash spectate-mutations.sh                  # 六个变异：重建 + 跑对应段 + 逐字节恢复
```

提交进仓库的证据只有文本（共 414 KB，没有 trace/BMP）：

- `evidence/t66-sourcetv/forward-baseline.log` —— 71 断言的权威前向读数（2026-10-25 15:59:02）；
- `evidence/t66-sourcetv/forward-70pre-repair.log` —— 三处门禁修复之前的 70 断言读数（保留作历史，报告 §8.2 引用）；
- `evidence/t66-sourcetv/mutations/run1-suite.log` / `run2-suite.log` —— 两遍变异套件的完整输出；
- `evidence/t66-sourcetv/mutations/<case>/` —— 每个变异体的门禁输出、自测输出、接管分析与被变红段落的切分。

单次运行的 trace 约 9 MB、BMP 2.5 MB，只保留其中的读数（已写进报告与日志的 `OK` / `FAIL` 行），文件本身不入库。

### 8.4 这些门禁不覆盖什么

- 只在一份录像上跑（snakewater S1，窗口内含 3 次死亡）；别的 SourceTV 录像死亡更密或更稀时的行为没测；
- 死亡用的是正向证据（`m_lifeState != 0` 或 `m_iHealth <= 0`）。一个两列都没有的实体行不会被当成尸体（这是故意的，见 §3 步 3），代价是"某个不写这两列的死亡"不会被处理；
- 8x 以外的高倍速见 §9 第 1 条；
- 画面只作负控，不承担"尸体离开画面"的证明（见 §7）。

---

## 9. 边界与未验证项

**已测（2026-11-02 收口后）**：一份 SourceTV 录像的 **8x 与 16x** 回放；3 次死亡、3 次接管、1 次手动选人（含越界拒绝）、**1 次暂停中的手动选人**、**1 次真实倒退 seek**、1 次 2.0 s 暂停；探针回放 1936 帧且重复运行逐字节相同。

**七条的处置**：三条变成测量（1、2、6），其中一条同时是产品缺陷的修复；四条带着写明的理由接受（3、4、5、7）。逐条依据在 [`T66-NV-MATRIX.md`](T66-NV-MATRIX.md)，读数、命令与时间线在 [`T66-CLOSEOUT.md`](T66-CLOSEOUT.md)。**没有一条留成"未证明、也不知道能不能用"。**

1. **高倍速 → 已关闭，且是产品缺陷。** 旧判据是固定的 `kSeekGapTicks=200`（只看 tick 增量），16x 下"一帧耗时够久"与"用户 seek 了"无法区分，于是 `resync=2`。现在 seek 上限按当前倍速与墙钟帧时长算：`kTickRate=66`、`kSeekFloorTicks=32`、窗口 2 倍余量（`native/src/spectate_target.cpp` 的 `update()`）；`update()` 新增**必填**参数 `playbackSpeed`——不给默认值，因为默认值会把这个缺陷原样带回来（`native/src/main.cpp:10412-10415` 传 `g_playback.speed`）。实测 16x：`resync=0`，deaths=3、handovers=3 与 8x 一致（门禁 §11）。探针自测补 S7b 四条（49 → **53** 断言）。
2. **真实 seek / rewind → 已关闭。** 门禁 §10 用 `--camera-script`（播到 tick 3400 → `seek 2500` → 跑 24 帧）驱动产品自己的 `PlaybackControl` 请求，实测 `before_tick=13748 before_resync=0`、`after_tick=12820 after_resync=1`，整个 run 的 `resync` 恰好 1 次，且 seek 后第一帧就已经在新位置（回退约 930 tick），没有从旧位置滑过去的过渡。夹具原先带 `pause`，第三轮变异实测它不确定（暂停足够长时交接按墙钟已经走完，`before_tr` 读到 0；跳转有时还没落地脚本就退出了，`after_resync=<none>`，恢复后的基线因此自己判红），所以去掉了暂停；去掉后同一段连跑三次读数逐字一致（`/d/_shared/t66-nv/seekdet1..3.log`）。"清掉进行中的交接"那一行本身无法用变异证明（保留交接的补丁在这份录像上不可见，下一帧换目标就结束了），这条限制写在 `T66-CLOSEOUT.md` §2。
3. **demo 自带导演目标 → 接受（语料限制）。** S1 的 `m_hObserverTarget=301073`（index 17 / serial 147）指向一个**永久的死亡观察者槽位**（team 1 / health 200 / lifeState 2，tick 15000..70000 全同），所以"优先用 demo 自己的目标"这条在本录像上只能断言为 `director=0`（从未可用），不能断言为"跟随了导演"。要关闭需要一份该句柄指向活人的 SourceTV 录像；夹具生成器 `stability-make-inputs.ps1` 只截断真 demo，造不出来。
4. **特殊死亡 → 接受（在契约外）。** 接管只读 `m_lifeState` / `m_iHealth` 两个字段（`native/src/spectate_target.cpp:94-98`），从不问死因：demo 报成死亡的都覆盖，报不出来的死亡换任何死因分类也覆盖不了。"两列都没有的行不算尸体"这条禁路已被断言（§3 步 3）。
5. **附加模型（帽子/可穿戴）→ 接受，并留一条明确的后续。** 尸体规则按实体索引只隐玩家模型本身；产品目前没有 `m_hMyWearables` 读取路径（`grep -r wearable native/` 只命中绘制分类与探针注释）。要关闭需要（a）世界实体的 owner handle 解析、（b）一份戴帽玩家在画面里死亡的录像，两者都不在本语料里。
6. **暂停中的手动选人 → 已关闭。** 门禁 §9 在 §8 的暂停夹具上加 `--spectate-target 6`（选 2500 这个 tick 是真有理由的：被点的 6 号在 server tick 13097 死亡，晚于约 2750 就测不到"暂停保住了选人"）。实测：冻结窗内零重发（安静形态），冻结开始前最后一行 `target=6 src=manual`，恢复后第一次换人带 `deaths>=1`——换人来自死亡，不是来自暂停。
7. **多显示器 / 独占全屏 / 音频 → 接受（沿用 T49.1）**：本机只有一个 64 位 D3D11 窗口、一块屏，环境上测不到；清单留在 T49.1，不在这里复制第二份。

**已知取舍**（不变）：`native/src/spectate_target.cpp` 的 `kHandoverSeconds > 0.0` 判据会触发 `warning C4127`（条件表达式是常量，构建日志里报在 :303）。它存在的原因是让 m1 变异体（把 0.3 改成 0.0）不除零崩溃，从而把变红的原因说成"接管消失了"而不是"崩了"。这是门禁侧需要它，不是产品需要它，故保留并在报告里点名。

---

## 10. 变更文件

本轮改动的文件（`git status --porcelain` 的实测行数）：

| 文件 | 行数 | 内容 |
|---|---|---|
| `native/include/spectate_target.h` | 173（新） | 接管导演的契约：`SpectateSlot`、死亡判据、稳定顺序、导演句柄解码、接管时钟、seek 规则、`traceFields()` |
| `native/src/spectate_target.cpp` | 345（新） | 八步决策；手动选人后刷新存活状态（本轮修的真实缺陷）；正向死亡证据；"无目标不跳原点"守卫 |
| `native/tools/spectate_probe.cpp` | 460（新） | 自测 49 断言（S1-S8，含 S2b"未解析行不是尸体"）+ 真实录像回放 |
| `native/src/main.cpp` | +282 / -11 | 接线（`--spectate-target` / `--spectate-show-corpses` / `--spectate-pause-at` / `--spectate-pause-seconds`）、每帧决策块、绘制候选集重构、`spectate:` 追踪与总结 |
| `native/CMakeLists.txt` | +28 | 产品源 + `spectate_probe` 目标（exe 51 → 52） |
| `verify-all.sh` | +41 / -26 | 27 步（新增 27/27）、exe 钉 52、断言地板 71（收口轮提高到 94，见 §10.1） |
| `spectate-check.sh` | 406（新） | 九段门禁；每段可用 `SPECTATE_SECTIONS` 单独跑，变异套件靠这一点只跑携带该断言的那一段 |
| `spectate-mutations.sh` + `spectate-patchlib.py` | 172 + 200（新） | 六个变异、逐字节恢复、`error C` 计数、恢复后重跑基线 |
| `spectate-pixels.py` / `spectate-transitions.py` | 75 / 110（新） | BMP 对比（24bpp）；接管形状分析（段 5 的另一半） |
| `evidence/t66-sourcetv/` | — | 本报告 + 门禁日志 + 变异日志 |

### 10.1 收口轮（2026-11-02）的改动

`git diff --numstat` 实测，相对 `660ff40`：

| 文件 | 增 / 删 | 内容 |
|---|---|---|
| `native/include/spectate_target.h` | +24 / -7 | 删 `kSeekGapTicks`；加 `kSeekFloorTicks=32`、`kTickRate=66.0`；`update()` 新增必填 `playbackSpeed` 与 `lastNow_`/`hasLastNow_` |
| `native/src/spectate_target.cpp` | +23 / -6 | seek 上限 = 2 × 66 × speed × 帧时长，带 32 tick 地板；命中时清交接 |
| `native/src/main.cpp` | +4 / -1 | 每帧把 `g_playback.speed` 传给 `update()` |
| `native/tools/spectate_probe.cpp` | +51 / -20 | 20 处调用补 `kReplaySpeed`；S7b 四条（16x 卡顿不算 seek、一帧跳 240 tick 算 seek、零耗时不算、1x 半秒算）；自测 53 断言 |
| `spectate-check.sh` | +176 / -10 | 段数 11；§9 暂停中的选人、§10 真实 seek、§11 16x 回放；`assert_contains` 传文本不传路径的坑写进注释 |
| `spectate-mutations.sh` | +20 / -4 | `caught/4` 写死改为按文件数出 `TOTAL_CASES`（否则 8 例全红仍判 FAIL） |
| `spectate-patchlib.py` | +25 / -2 | m6 needle 跟上新调用；m7/m8 换成能红 §9/§10 的变异 |
| `verify-all.sh` | +13 / -10 | 27/27 的断言地板 71 → 94（12 段合计 96，starved host 94），注释列出每段计数 |
| `evidence/t66-sourcetv/T66-NV-MATRIX.md` | 新 | 七条 NV 的逐条处置与要关闭它需要什么 |
| `evidence/t66-sourcetv/T66-CLOSEOUT.md` | 新 | 本轮读数、命令、时间线、变异证据与那次被打断的重跑 |
| `evidence/t66-sourcetv/T66-SOURCETV-REPORT.md` | 改 | §9 重写为"逐条处置"，§10 增本节 |
