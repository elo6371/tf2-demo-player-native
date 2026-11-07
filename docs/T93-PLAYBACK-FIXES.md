# T93 四个播放侧缺陷：修前/修后读数（一页）

分支 `task/t93-playback`（基于冻结基线 `integration/t79-final` @ e15577b）。
**修前**=冻结 t79-integration 二进制（`tf2_demo_native.exe`，sha256 a3c93d57…3724e9，2,763,264 B）；**修后**=本分支构建的同一目标。
所有读数来自 `evidence/t93-playback/`（门禁产物）与 T85 审计的既存 trace，不是手抄。

## 0. 一表看完

| # | 缺陷 | 修前实测 | 修后实测 | 用户看见什么 |
|---|---|---|---|---|
| A | `--demo` 打不开时静默继续 | rc=0/6 s，trace 无 error，`no-such.dem` 连报错都不出现，地图回落 `2koth_abbey` | **rc=2**，首行 stderr `demo open failed: …`，trace `demo: open failed path=… reason=…`，不建窗、不查地图（0 s） | 双击/命令行传错路径：以前开着「别人的地图」装没事，现在立刻报错退出 |
| B | 相机模式切换要等下一个 demo tick 才生效 | `freefly` 返回 ok，但 `freeFlySteps=0`、589 条 `scene: camera` 全 `mode=first-person`、`modeChanges=2` | `camera: mode applied=free-look tick=413 paused=1`、`modeApplies=1`、两张帧 BMP sha 不同 | 暂停时按自由视角键没反应；现在当场切 |
| C | 前向 seek 把整段跳过的音效在一个帧里全放 | tick 22(seen=4) → tick 3000 单次 `seen=706`；门禁全 trace 峰值 **1494** | `audio: seek reset from=34 to=3000`，其后 advance 峰值 **28** | 一拖进度条就「哗」一声几十个音效叠放 |
| D | 恢复播放时重入的那个 tick 被当成已消费 | `--start-paused` 恢复后首条 advance = `tick=16 seen=4`（tick0 的 746 条全丢） | 首条 advance = `tick=16 seen=750 dup=0` | 从暂停继续时开场音效全丢 |

## 1. A：打不开的 demo 现在是一条硬错误

- **根因**：`nativeUi.openDemo()` 的返回值在 main.cpp:3245 被丢弃（`nativeUi.openDemo(commandDemo);` 无接收者），失败只落到 UI 的 `setError()`（native_ui.cpp:162-166），而产品在 headless/无窗路径下从不显示它。
  于是 `demoHeader` 保持默认值，地图名走 `main.cpp:4185` 的 `demoHeader.mapName : "2koth_abbey"` 回落。
- **修法**（patch A，main.cpp:3257-3277）：接住返回值 → 失败时 `traceFmt("demo: open failed path=%s reason=%s")`（:3271）+ `fprintf(stderr, "demo open failed: %s: %s")` + `fflush` + `return 2`（:3276），位置在**开窗与 VPK 装载之前**。
- **读数对照**：
  - 修前（T85 审计 c3d，t79）：`no-such.dem` → rc=0、6 s、trace 无 error、照常 `map: lookup` 到 2koth_abbey。
  - 门禁 pre A1：rc=**124**（挂到 300 s 上限才被杀，正是「静默继续」的可观测后果）。
  - 门禁 green A1：rc=**2**、**0 s**、stderr 命中 `demo open failed`、trace 命中 `demo: open failed`、`mode: window_created=1` **不出现**、`map: lookup` **不出现**。

## 2. B：相机模式在「这一帧」就应用，而不是等下一个 tick

- **根因**：模式变化只在 `if (g_renderer && g_playback.enabled && g_playback.tick != lastSceneTick)` 这个「tick 变了」的块里被消费；暂停时 tick 不动 ⇒ 模式永远不落地。
  而这正是 T85 审计 f1 里的假绿：`camera: accept step=3 cmd=freefly result=ok detail=on` 打印成功，`camera: director … freeFlySteps=0`，589 条 `scene: camera` 全部 `mode=first-person`——**脚本说成功了，画面没变**。
  额外坑：不能复用 `g_lastAppliedCameraMode`，因为 `handleCameraAction` 在**按键那一刻**就写了它（main.cpp:635/:646），复用会把假绿原样搬进来。
- **修法**（patch B）：新增 `g_cameraModeApplies` / `g_frameAppliedCameraMode`（main.cpp:313-321），在每帧块**之前**（main.cpp:9126-9153）比对真正的「上一帧应用值」，变了就（非第一人称且非 debug 时）`clearObserverDemoView()`、更新变量、`++g_cameraModeApplies`、打 `camera: mode applied=%s tick=%d paused=%d`（:9147）；`camera: summary` 增 `modeApplies=%u`（main.cpp:11582/11588）；脚本 `freefly` 步改成「设置后必须真的等于目标状态，否则 fail("unreachable")」，detail 带 `mode=`（main.cpp:962-970）。
- **读数对照**（门禁 B 用例：resume → tick 400 → pause → 停 4 帧 → freefly on → 抓两帧）：
  - 修前：`camera: mode applied=…` 行**不存在**（`fnum` 返回 `-`）、`modeApplies=-`、脚本 `failures=1`、rc=1。
  - 修后：`applied=free-look tick=413 paused=1`、`modeApplies=1`、两张 snapshot tick 都是 413 且 `expect-same-tick match=1`（**时钟没动**）、`failures=0`、rc=0。
  - **关于像素（更正，实测）**：两张 BMP 确实不同（2,582,406 B，before aeb0b5f0…dffe8 / after 3a848650…3a3393a2），但**修前那轮的同名两张 BMP 是逐字节相同的这两个 sha**——即像素差来自 `freefly` 之外的东西（同一份 trace 里 focus z 从 0.570319 变 0.539210，pre 与 green 完全一致），**这条断言不区分修前/修后**。B 的判别力在 `camera: mode applied=… paused=1` / `modeApplies` / 三者同 tick 这三条上。

## 3. C：seek 之后先把调度器复位，再前进

- **根因**：音频 advance 只在 `g_playback.tick < soundScheduler.lastTick()` 时 `reset()`（main.cpp:9076 区）。前向跳转 tick 变大 ⇒ 走 `forEachRange(t1+1, t2)`，**整段被跳过的音效在一帧内全部解析并播放**。
  T85 审计实测：a2 `tick 22 seen=4 → tick 3000 seen=706`（+702/2978 tick = **0.2358 条/tick**）；f1 `tick 714 seen=70 → tick 5000 seen=2360`（+2290/4286 = **0.5343 条/tick**）；重跑 f1r 复现 0.5342。
- **修法**：`PlaybackControl::readings()` 增 `seeks` 计数（native/include/playback_control.h:162、native/src/playback_control.cpp:277，在 `cursorTick_ = requestedTick;` 前 `++readings_.seeks;`）；main.cpp:9056-9079 里检测 `seeks` 变化且 `g_playback.tick != soundScheduler.lastTick()` → `traceFmt("audio: seek reset from=%d to=%d")`（:9070）+ `reset(tick)`（:9073），原「tick 变小」分支降级为 else-if。
- **读数对照**：
  - 修前：全 trace 最大 `seen` = **1494**（门禁 pre C，`audio: seek reset` 行不存在，helper 回落全 trace）。
  - 修后：`audio: seek reset from=34 to=3000` 之后 advance 峰值 **28**（≤60 断言）。
- **附带效果**：seek 之后 `audio: summary` 的累计会被 reset 清零，所以门禁与 FIXES 一律**逐条累加 `audio: advance`**，不读退出时的 summary。

## 4. D：resume 不重入它进入的那个 tick

- **根因**：`AudioEventScheduler::reset` 原为 `lastTick_ = initialized_ ? tick : tick - 1;`——`initialized_` 一旦为真就永远走排他分支，于是「暂停在 T、恢复在 T」时，恢复时 `reset(T)` 让 `lastTick_=T`，而 T 上的事件从未被消费过 ⇒ 永久丢失。
  T85 审计实测：`--start-paused` 恢复后首条 advance `tick=16 seen=4`，整跑 `events_seen=10`；开场 tick 0 的 **746 条（14.01%）** 全丢。
- **修法**（native/src/audio_player.cpp:211）：`lastTick_ = lastTick_ >= 0 ? tick : tick - 1;`——判据从「初始化过」改成「**是否已经消费过区间**」：没消费过（含启动/首次）仍含起点，已消费过则保持排他。
- **读数对照**：首条 advance 修前 `tick=16 seen=4 dup=0` → 修后 `tick=16 seen=750 dup=0`；且 `dup=0` 证明没有变成「重播一帧」。

## 5. 门禁如何证明（每条都有变异位）

四个缺陷各留了一个**可被一行代码打穿**的判定点，对应 `playback-error-paths-check.sh` 的断言组与 `mutations/m1..m4.txt`：

| 变异 | 打穿哪一行 | 预期由谁抓到 |
|---|---|---|
| M1 | `if (!demoOpened) {` → `if (false) {` | A1（rc=2 + `demo: open failed`） |
| M2 | `if (pendingCameraMode != g_frameAppliedCameraMode) {` → `if (false) {` | B（`camera: mode applied` + `modeApplies`） |
| M3 | `if (seekRequests != lastAudioSeeks) {` → `if (false) {` | C（seek 后峰值 ≤60） |
| M4 | `lastTick_ = lastTick_ >= 0 ? …` → `lastTick_ = initialized_ ? tick : tick - 1;` | D（首条 advance `seen>=700 && dup=0`） |

**状态**：M1..M4 尚未跑（门禁整链先由合并窗口代打，见 §7）。跑完后本表会补上 `MUTATION-KILLED/SURVIVED` 实测行。

## 6. 修前/修后整轮数字

| 轮次 | 二进制 | 结果 | 用时 |
|---|---|---|---|
| pre | t79-integration @ e15577b | 13 pass / 19 red | 556 s |
| green | 本分支（bbf3559 + fa34382 断言校准后） | 25 pass / **0 red** | 225 s（校准前那轮 7 红全是断言自身写错，见 docs/T93-GATE-CALIBRATION.md） |

E 用例（不变量，修前修后都必须是这组数）：`scheduled=5323`、`mode: summary events_seen=759`、`audio_device_opened=0`、`audio_device_open_attempts=0`、无 `audio: seek reset`、`camera: summary modeApplies=0`（默认第一人称不变）。

## 7. 未验证 / 别过度引用

- **没有在真实声卡上播放过**：所有用例一律 `--audio-disabled`，`waveOutOpen` 调用次数恒 0。C/D 证明的是「调度器喂给 sink 的事件数/顺序对不对」，不是「声音好不好听」。
- **变异证据待补**（见 §5 状态）。
- **像素断言不区分修前/修后（实测更正）**：B 的 before/after BMP 在 pre 与 green 两轮**逐字节相同**（aeb0b5f0… / 3a848650…），所以「两帧不同」不能当作修复证据；它只证明抓帧链路在动。B 的判据以 trace 为准。
- **跨 seek 的总量口径**：seek 会 reset 调度器，只有逐条累加 `audio: advance` 才是用户实际听到的事件数。
- A 的 `return 2` 是**新的退出码契约**（此前该路径 rc=0）：任何下游脚本若把「非 0」当「可忽略」需要改。