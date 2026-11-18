# N13 状态：身份、读数与 UI 通路（证据 + 门槛，不写实现）

基线：`HEAD 6dd853ef24494b6066e4624970ac75620d4863ea`（分支 candidate/e5）。
本文件是**只读侦察**：没有跑产品 exe，没有跑渲染/抓帧，没有改 `native/` 一个字节，也没有新增 `tools/n13-*` 脚本。
因此下文的"存在/不存在"全部是**源码读出的事实**（行号当轮复核），不是运行结果。
凡是"看起来会这样"的地方，一律进 §5 / §6，不写成结论。

---

## 0 三小项的真实进度

| 卡片小项（`docs/PROJECT-NEXT-TASKS-2026-11-14.md:229-233`） | 机制是否存在 | 本文件能下的结论 | 状态 |
|---|---|---|---|
| 1. 广播者 / POV 主体 / STV 被跟随者三者不许混；缺 userinfo 必须显式 unknown，不许编名字 | 分类器 + 按 (slot, server tick) 查 userinfo 的名字解析都在 | 三者**没有混**，但"广播者"这个概念在产品里**根本不存在**（grep 0 命中）——不混是因为无此物；缺名字有 4 个 reason 字符串且不编造，**但两条早退路径的 reason 是空串**（§1.3） | 部分成立：机制有、缺实测 |
| 2. timer/round/team/class/health/weapon/事件流的来源与 stale/missing 策略；Unicode/缩放/DPI 分开测 | 读数分两条路径（标题栏读 4 个字段，HUD 三行读 timer/score/name），每个字段都带 `*Known`；事件流已解码 | 缺失→`?` / `TIME ?` 的显式策略**实测在源码里**；但 health/class/clip/team **只进标题栏**，不进 HUD；事件流只进缓存计数，**不显示** | 部分成立 |
| 3. 开 demo→选 TF 根→找地图→播放→跳转→跟随→设置重启→异常恢复，全都能在产品 UI 里做 | 5 个 screen、18 个命令 | 命令表里有 seek/step/scrub/reverse/settings/diagnostics/export，**没有 follow / 选人 / 下一个玩家**；跟随是引擎按 demo 视图目标自动做的，不是用户可选项 | **未达成**（不是"没做"，是"机制不在"） |

一句话：**N13 不是"还没开始"，是"其中一小半已经以另一种形式存在，但卡上最核心的那一条（选人跟随）在产品里没有入口"。**

---

## 1 身份三项：谁是谁，以及"缺名字"长什么样

### 1.1 录像是 STV 还是 POV：一个分类器，两个标签

`native/src/demo_header_track.cpp:430` `classifyDemoRecording(const DemoHeader&, const DemoNetworkSummary&)`：

- `:432-434` 注释直说"**从名字重新推导，而不是只信 header.recordingType**"；
- `:435` `result.headerName = header.recordingType == DemoRecordingType::SourceTv || namesIndicateSourceTv(header);`
- `:439-442` 命中 → `kind = SourceTv`、`label = "SourceTV"`；
- `:444-447` `PovHeuristic || !header.clientName.empty()` → `kind = Pov`，标签是 `summary.serverInfoCount > 0 ? "POV" : "POV (heuristic)"`；
- `:449-450` 都不命中 → `Unknown` / `"unknown"`。

原始来源：`native/src/demo_header_core.cpp:95` 置 `SourceTv`、`:97` 置 `PovHeuristic`。

**读法（这是 N13 的关键）：`"POV (heuristic)"` 这个标签自己承认是猜的。** 卡片要求"不许混"，源码给的答案是"混不了，因为我给猜的那一类单独打了 (heuristic)"——这是诚实的，但也意味着**这个标签不能当证据用**：一个 `POV` 标签不等于"这个 demo 就是第一人称录的"。

### 1.2 名字不是从 userid 来的，链路上每一跳都可查

`native/src/main_hud.cpp:726` `hudLiveResolveName(summary, viewEntity, playbackTick)`：

1. `:730` `viewEntity < 1` → 直接返回（reason 空）；
2. `:736-742` 在 `summary.userInfos` 里按 `entry.entityIndex == viewEntity` 找槽位，找不到直接返回（reason 空）；
3. `:754-762` 用 `packetServerTicks[playbackTick]` / `packetServerTickKnown[playbackTick]` 把播放 tick 换成**服务器 tick**，两个分支都 `reason = "no-tick"`；
4. `:765-768` `findUserInfoAtTick(summary, name.tick, slot->entryIndex, resolved)` 失败 → `"no-userinfo-tick"`；
5. `:769-772` 名字是空串 → `"no-userinfo-empty"`；
6. `:773-775` 成功 → `known = true`、`reason = "ok"`、`hudLiveProjectName()`。

`:719-725` 的注释说明了为什么不用 userid："**userid 是另一个命名空间，demo 无法确认它**"，所以按 `entityIndex` 读解码器已经推出的字段，而不是在这里再算一次 `entity - 1`。
"往回 seek 得到的是那个 tick 真的有的名字"靠 `findUserInfoAtTick` 是 `(summary, tick)` 的纯函数（同段注释）。

### 1.3 缺名字：有 reason、有计数器，但两条早退可能给出**空 reason**

- `:1196-1200` trace 打在 `hud: name nameEntity=%d nameSlot=%d nameKnown=%d nameFallback=%d nameReason=%s nameSlug=%s nameDisplay="%s" relay=%d relayValid=%d`；
- `:1202` `if (!name.known) ++g_hudLiveNameMissing;`——**每一帧**问不到名字就记一次，trace 汇总行（`:528-530`）带 `nameMissing=`。

但 §1.2 的第 1、2 步是**早退且不设 reason**。所以产品在"实体号无效""表里没有这个槽位"两种情况下，trace 打出来的是 `nameReason= `（空），而不是像 `no-userinfo-tick` 那样自解释的一串。
**这正是卡片第 1 条关心的形态**：缺失是"显式 unknown"没错（`nameKnown=0`），但**原因是空的**，读 trace 的人无法从字符串本身区分"没这张表"和"这个实体号本来就不该有名字"。

### 1.4 "广播者"在产品里不存在

- `grep -rn "broadcaster\\|Broadcaster" native/src native/include` → **0 命中**；
- `grep -n "clientName" native/src/main_hud.cpp native/src/main_ui.cpp native/src/native_ui.cpp` → **0 命中**。

也就是说：`header.clientName`（POV 录制者）**只被分类器用来判标签**（`demo_header_track.cpp:444`），**从未被 HUD 或 UI 画出来**。屏幕上的名字只有一个来源——被跟随实体的 userinfo 名字。
结论要说得精确：**"广播者/录制者/被跟随者"不会混，是因为产品只展示其中一项，另外两项根本没进入渲染路径**；这不是"设计上做了区分"，而是"少画了两个"。

---

## 2 读数：两条路径，两套字段

### 2.1 HUD 三行 vs 标题栏五字段

| 位置 | 内容 | 证据 |
|---|---|---|
| HUD 面板（3 行文本） | `TIME ...` / `RED n BLU m` / 目标名字 | `native/src/main_hud.cpp:1235` `const std::string lines[3] = { text.time, text.score, text.target };` |
| 标题栏 | `health= team= class= clip= ...` | `native/src/main.cpp:6396-6399`，未知打 `L"?"`（`:6396` `hudState.healthKnown ? ... : L"?"`） |

标题栏那 4 个字段的来源是 `readPlaybackHud`（`native/src/playback_hud.cpp:38-53`）：
`m_iHealth`→`healthKnown/health`、`m_iTeamNum`→`teamKnown/team`、`m_iClass`→`classKnown/playerClass`、`m_iClip1`→`clipKnown/clip`，外加 `m_flChargeLevel`→`uberKnown/uberCharge`（`:46-51`，只在 `Float` 且 finite 时才认）。
字段定义见 `native/include/playback_hud.h:12-23`。
**每一对都是 `bool known + value`**，也就是卡片要的"缺失必须显式"在这里是结构上就成立的。

### 2.2 时间：round-timer 与 playback-clock 是两个东西，且标签不同

`native/src/main_hud.cpp:783-800`：

- `reading.roundTimerKnown` → `source = "round-timer"`（`:784`）；
- 否则若 `tickRate > 0 && endTick > 0` → `source = "playback-clock"`（`:792`），并且 `:789-791` 的注释明说"**这个回落值不是回合计时器，它数的是录像还剩多长**"；
- 都没有 → `source = "none"`、`text.time = "TIME ?"`（`:797-800`）。

这是"读数来源必须可分辨"的正面例子：**两种时间不但分开算，还在 trace 里用不同的 source 字符串自报家门。**

### 2.3 但没有 gamerules 实体

`grep -rn "gamerules\\|GameRules\\|teamplay_round\\|roundState\\|m_iRoundState" native/src` → **0 命中**。
所以 `roundTimerKnown` 不是从"服务器游戏规则实体"读的，而是走 `hudLiveFind`（`main_hud.cpp:563-577`）的属性查找：先精确名，再退到"存储名的点号尾段"（`:555-562` 注释说明这条规则抄自 `playback_hud.cpp`，且门禁会把这里挑出的数和 `readPlaybackHud` 对同一帧的读数交叉比对，所以两份规则不会悄悄漂移）。
**"没有 gamerules 实体"是实测事实；"回合计时器因此可能永远走 playback-clock 回落"是 NOT VERIFIED（§5-3）。**

### 2.4 事件流：解码了，但没画出来

- `native/include/demo_header.h:162` `struct TempEntityEvent`；`:499` `struct DemoGameEvent`；`:888` `std::vector<DemoGameEvent> gameEvents`；`:883` `gameEventDefinitions`（`:483` 注释：`svc_GameEventList` 一次发整表，414 条定义、id 0..413）；
- `native/src/demo_header_events.cpp:23-74` `applyTempEntityFields`：先清零全部 `has*`，再按 `className` 分支取 `m_iPlayer`/`m_iWeaponID`/`m_iMode`/`m_iSeed`（`CTEFireBullets`，`:47-51`）、`m_hPlayer`/`m_iEvent`（`CTEPlayerAnimEvent`，`:52-54`）等，并累加 `knownFieldCount`；
- 消费端：`native/src/main.cpp:3513` 把 `demoNetworkSummary.gameEvents` 作为参数传出去；`:6053` 循环 `tempEntityEvents`；**标题栏只有一个计数** `:6157` `+ L" tecached=" + std::to_wstring(demoNetworkSummary.tempEntityEvents.size())`。

结论：**事件流有来源、有 known 标志、但没有 HUD 呈现**。卡片第 2 条问"事件流的来源与 stale/missing 策略"——来源答得出，**呈现与新鲜度策略在本轮没有证据**（§5-4）。

---

## 3 UI 通路：命令表里缺的那一项

`native/include/native_ui.h:16` `enum class UiScreen { Opening, ImportReview, Player, Settings, Error };`，名字由 `:355` `uiScreenName` 给出（`native/src/native_ui.cpp:483` `case UiScreen::Player: return "player";`）。

`UiCommand`（`native/include/native_ui.h`，18 项）：
`Open, Cancel, ConfirmImport, PlayPause, Stop, StepBackward, StepForward, StepSecondBackward, StepSecondForward, Reverse, Scrub, ToggleHud, ToggleMute, ToggleFullscreen, ResetCamera, OpenSettings, OpenDiagnostics, Export`。

**这张表里没有 Follow、没有 SelectPlayer、没有 NextPlayer。** `UiSettings` 也只有 `standardQuality/fov/vsync/volume/tfRoot/recentDemos`，没有"跟随目标"字段。

"跟随"确实存在，但是自动的：`native/src/main.cpp:430` `std::uint32_t g_cameraFollows = 0; // scene ticks that followed an observe target`，配合 T81 观战相机常量（`:458` `kSpectatorEyeHeight = 68.0f`、`:466` `kSpectatorBackUnits = 0.0f`）与 `:715` `placeSpectatorView()`。
所以卡片那条链要拆成两半：

| 链上一环 | 产品 UI 里的入口 | 证据 |
|---|---|---|
| 开 demo | `Open` / `ImportReview` / `ConfirmImport` | UiCommand 表 |
| 选 TF 根 | `UiSettings::tfRoot` 字段存在 | `native_ui.h` UiSettings |
| 找地图 | **未验证**（`ImportReview` 里画了什么我没有跑产品去看） | §5-5 |
| 播放 / 暂停 / 停止 | `PlayPause` `Stop` | 同上 |
| 跳转（按 tick / 按秒 / 单步） | `Scrub` `StepBackward` `StepForward` `StepSecondBackward` `StepSecondForward` `Reverse` | 同上 |
| **跟随某个玩家** | **没有任何命令**；相机跟的是 demo 自己的视图目标 | UiCommand 表 + `g_cameraFollows` |
| 设置 | `OpenSettings` | 同上 |
| 异常恢复 | `Error` screen（`UiScreen::Error`） | `native_ui.h:16` |

**这是 N13 第 3 条的硬结论：链子在"跟随"这一环断了，而且断在产品模型层（没有命令、没有设置项），不是"没接上 UI"。** 补它要动的不只是画一个按钮。

---

## 4 三个同类缺陷类别的自查（本卡）

| 类别 | 怎么查的 | 结果 |
|---|---|---|
| awk 数字 vs 字符串（D53 族） | 本卡**没有写任何脚本**（无 `tools/n13-*`），也没有 shell 比较 | 不适用 |
| 引用生产者不打印的字段（D56 族） | 本文引用的每个 trace 字段都回查了生产点：`nameKnown/nameReason/nameSlug/nameDisplay/relay` 在 `main_hud.cpp:1196-1200`，`health=/team=/class=/clip=` 在 `main.cpp:6396-6399`，`tecached=` 在 `:6157` | 无"永远红/永远绿"的断言；本文不含任何会红的断言，因为它不含任何断言，只含"读到什么" |
| 采样密度改变读数（D59 族） | 本轮没有跑任何测量 | 不适用 |

额外自查出的那一条（§1.3）：**`nameReason` 在某些缺失路径上是空串**。这不是脚本 bug，是产品 trace 的可读性缺口——但它的效果和 D56 同类：拿 `nameReason=` 做断言的门禁，在这两条路径上会看到一个空值而不是一个能区分原因的值。

---

## 5 未验证（不写进结论）

1. **没有在真实 demo 上观察过任何一次名字解析。** 全部是源码链路；`nameKnown=1` 的真实样本本轮没有（T100 的 `evidence/t100-userinfo/MEASURED.md:137-142` 是引用，不是本轮复现）。
2. **没有确认 STV demo 的被跟随者是否总能查到 userinfo。** 表是按 client slot 存的，STV 视角的视图目标是否一定在表里——源码没说，我也没跑。
3. **没有确认 round-timer 在实际 demo 里到底命中过没有。** `gamerules` 0 命中只说明"不是从那个实体读的"；`roundTimerKnown` 在真实录像上的取值分布我没有测。
4. **事件流的新鲜度/过期策略没有证据。** 我只证到"解码了、有 known 字段、标题栏只有一个计数"。
5. **UI 的实际外观与流程没有证据。** 我没跑产品（任务卡禁止），`ImportReview` 里画了什么、TF 根选择器长什么样，全部 NOT VERIFIED。
6. **Unicode / 缩放 / DPI 对这两处文本的影响没有量。** 只有 `main_hud.cpp:1244-1246` 的注释"只合成可打印 ASCII，所以这里字节数==码点数"——那是**宽度算术的前提**，不是"非 ASCII 被正确渲染"的证据。
7. **没有证明"三行 HUD"就是唯一的 HUD。** 我只读了 `main_hud.cpp` 这一条路径与标题栏；`playback_hud.cpp` 是数据层，它是否还有别的呈现路径，本轮没有穷举。

---

## 6 本文件没有证明什么

1. 没有实现任何东西。本文是证据清单，不是设计，也不是"建议这么改"。
2. 没有跑过产品，因此**没有任何一条"实际显示为…"的结论**。
3. 没有证明分类器在真实 demo 上的标签正确率——只证明它怎么算、以及 heuristic 分支自己承认是猜。
4. 没有证明"名字不会编造"到运行层面：源码里 `known` 门在，但**没有样本**证明它在缺名字时确实打 `?`/空而不是别的。
5. 没有证明事件流没被别处用到（我只扫了 `native/src/*.cpp` 的 `gameEvents/tempEntityEvents` 命中）。
6. 没有证明 §3 那张表的"未验证"以外还有隐藏通路：我查的是 `UiCommand` 枚举与 `UiSettings` 结构，没查 Win32 菜单/快捷键里是否另有一条跟随入口。
7. 没有对 N13 做任何"完成度%"。卡上三条，一条部分、一条部分、一条未达成——这不是进度，是我读到的形状。

---

## 7 复现（只读命令，全部在 HEAD 6dd853e 上跑过）

```bash
PY=C:/Users/Administrator/.dsh/dsh-runtimes/dsh-primary-runtime/dependencies/python/python.exe
# 0) 基线
git -C /d/TF2_Native_Worktrees/t105-e5 rev-parse HEAD
# 1) 身份：分类器与标签
grep -n "classifyDemoRecording" -A 22 native/src/demo_header_track.cpp
grep -rn "broadcaster\|Broadcaster" native/src native/include | wc -l      # 期望 0
grep -n "clientName" native/src/main_hud.cpp native/src/main_ui.cpp native/src/native_ui.cpp | wc -l  # 期望 0
# 2) 名字解析与 reason
sed -n '719,777p' native/src/main_hud.cpp
sed -n '1190,1240p' native/src/main_hud.cpp
# 3) 读数两条路径
sed -n '783,806p' native/src/main_hud.cpp
sed -n '6392,6404p' native/src/main.cpp
grep -rn "gamerules\|roundState\|teamplay_round" native/src | wc -l          # 期望 0
# 4) 事件流
sed -n '23,74p' native/src/demo_header_events.cpp
grep -n "tempEntityEvents\|gameEvents" native/src/main.cpp
# 5) UI 命令表
grep -n "enum class UiCommand" -A 20 native/include/native_ui.h
```

> D53 提醒：本文件里所有 shell 都只用 `grep`/`sed`/`wc`，不用 `awk` 取字段（worktree 的 awk 会留 CR，Git 的 awk 会剥掉，同一份字节在两种 PATH 下比出不同结果）。
