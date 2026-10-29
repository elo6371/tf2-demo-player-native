# T70 / T72 设计规格：精细回放控制与复盘书签

- 文档性质：**纯静态设计（DESIGN ONLY / NOT IMPLEMENTED）**。本文没有 build、没有启动产品、没有获取共享锁、没有修改产品源码。
- 日期：2026-10-24（Asia/Shanghai）。
- 仓库：`D:\TF2_Native_Test`，分支 `p0-entity-protocol`，基线 HEAD `99eeea294cbe883e4c3e6b4b82c83de45d2ab926`。
- 任务来源：原任务清单 §15（该清单已于 2026-10-29 随仓库清理删除），T70 与 T72；T71 只作为格式依赖。
- 相关规格：`evidence/t64-events/DATA-CONTRACT.md`、`evidence/t68-events/GAME-EVENT-SPEC.md`、`evidence/t68-events/SAME-TICK.md`。

> **断言等级声明。** 本文所有 `应读到` / `期望` 都是**设计断言**，不是运行结果。没有执行任何产品进程、脚本或探针，因此不存在任何 T70/T72 的 PASS；把本文的表格当成“已验收”就是 §1 规则所说的“零断言判 PASS”。真正的验收必须在实现后按每行的“读数 + mutation”重跑。

### 0.1 结论摘要（先看这五条）

1. **现有产品没有“实际帧 tick”读数。** 标题只有 `playback=<tick>/<endTick>`、`stale=<entitySnapshotStaleness>`、`anchorAbs=...`；`stale` 是 server tick 域的实体快照滞后，不是“请求 tick 与实际帧 tick 的差”。T70 必须新增 `req=`、`act=`、`delta=`、`frame=` 四个字段，并且 `act` 只能来自 `queryEntitySnapshotAtOrBeforeTick` 的 `resolvedSceneTick`，不能拿 `g_playback.tick` 冒充。
2. **现有“seek”只是对 `g_playback.tick` 赋值，不是原子 seek。** `native_ui.cpp:374-406` 在 UI 控制器里改自己的 `tick_`，`main.cpp:1955-1958` 再把 snapshot 抄回 `g_playback`；播放循环 `main.cpp:8161-8193` 在同一个 `elapsed` 里可以前进多个 tick。请求值与实际落点之间没有事务，也没有 pending/superseded 概念。T70 要把所有入口收敛到一个 `PlaybackControl::request()`，并让“落地”成为显式状态。
3. **逐帧在多数 tick 上不会得到“帧内容 +1 tick”。** T64 实测 bagel 的 512 个事件采样里只有 1 个 exact，median gap 822 tick、max gap 1180 tick；`queryEntitySnapshotAtOrBeforeTick` 会回退到 checkpoint。因此 T70 的验收不能写“画面帧号 +1”，只能写“cursor +1，且 `act`/`delta`/`frame` 如实显示回退”。这正是任务里“不伪装精确命中”的核心。
4. **1 秒不是 66 tick。** 现有 `native_ui.cpp:17` 的 `kSecondStepTicks = 66` 只在 `StepSecond*` 分支使用，且产品里没有任何调用者（只有枚举、switch 和探针）。按 demo 的 `demoTickRate`（`native/src/demo_header.cpp:2331-2340`，66.6667 或 header 的 ticks/playbackTime）计算，1 秒应为 `llround(rate)` = 67 tick（66.6667 时），5 秒为 333 tick。66 是 0.99 秒，必须改掉并把 `appliedSeconds`/`deltaSeconds` 打出来。
5. **T72 的玩家身份只能是数值，不能是名字。** 仓库没有 userinfo/player name 解码器（T64 G5；`logstf_viewer.h:69` 的 `UserInfoRefused` 是 logs.tf URL 的用户名/密码拒绝，不是玩家名）。T72 只能存事件里的 `userid`/`entindex`，名字留空并写 `no-userinfo`；任何默认名字、`unknown`、0 都是发明字段。

### 0.2 非目标

- 不实现片段剪辑、视频导出、编码（T55 延后）。
- 不实现 T71 工作区本身；本文只给 T72 需要的记录 schema 和 T71 必须预留的字段。
- 不实现 T68 事件解码；本文只消费 T64/T68 的事件记录契约。
- 不改变现有相机书签（T50 的 8 槽 `[camera]`）的语义；T72 是另一套复盘书签集合。
- 不做像素级画面比较；T70 的画面证据只要求“同一 tick 的 trace + 标题读数 + 一帧截图”，像素结论属于后续 gate。

### 0.3 已读的真实文件（基线）

| 文件 | 用途 |
| --- | --- |
| `native/include/native_ui.h` | `UiCommand`、`UiSnapshot`、`PlaybackHudLine`、`UiControlId` |
| `native/src/native_ui.cpp` | `setPlaybackState`、`command`、`kSecondStepTicks` |
| `native/src/main.cpp` | `g_playback`、播放循环、scene 解析、键盘、标题、WM_COMMAND/WM_HSCROLL |
| `native/include/demo_header.h` | `DemoHeader`、`EntitySnapshotQueryStatus`、`DemoViewSample`、查询函数 |
| `native/src/demo_header.cpp` | `demoTickRate` |
| `native/include/camera_director.h` | `CameraPose`、`formatPose`、`noteDemoTick` |
| `native/include/camera_bookmarks.h` + `native/src/camera_bookmarks.cpp` | 现有 8 槽相机书签 |
| `native/src/settings_store.cpp` + `native/include/settings_store.h` | `settings.ini` 路径与原子写 |
| `native/include/ini_section.h` | INI section 读写设施 |
| `native/include/keybinds.h` + `native/src/keybinds.cpp` | `shouldDispatchHotkey`、冲突检测、默认表 |
| `native/docs/camera-ui-contract.md` | 键盘焦点契约、trace 格式 |
| `evidence/t64-events/DATA-CONTRACT.md` | tick/时间/帧/相机/身份的实测契约 |
| `evidence/t68-events/GAME-EVENT-SPEC.md` | 事件解码规格、same-tick 去重键 |
| `evidence/t68-events/SAME-TICK.md` | tick 334 的 209/213 条同 tick 事件 |
| `D:\TF2_Native_Worktrees\_playbook\problems\INDEX.md` | 已知失败模式 |

---

## 1. 现有代码事实（T70）

### 1.1 播放状态只有一份数据，但没有一份接口

`native/src/main.cpp:462-474`：

```cpp
struct PlaybackState {
  bool enabled = false;
  bool paused = false;
  bool reverse = false;
  double speed = 1.0;
  double tickRate = 66.6666667;
  std::int32_t tick = 0;
  std::int32_t endTick = 0;
  std::int32_t anchorLookupTick = -1;
  bool anchorValid = false;
  DemoIndexEntry anchor{};
};
PlaybackState g_playback{};
```

打开 demo 时 `main.cpp:5027-5038` 初始化：`enabled = demoHeader.valid`、`endTick = max(0, demoHeader.ticks)`、`tick = 0`、`paused = startPaused`、`speed = clamp(playbackSpeed, 0.125, 64.0)`、`reverse = false`、`tickRate = demoTickRate(demoHeader)`。

`DemoHeader`（`native/include/demo_header.h:32-45`）的 `ticks` 是 `int32`，`playbackTime` 是 `float`。`demoTickRate`（`native/src/demo_header.cpp:2331-2340`）优先 `ticks / playbackTime` 并 clamp 到 `[30.0, 128.0]`，否则回退 `66.6666667`。

**有效范围是 `[0, endTick]` 闭区间**：循环在 `tick >= endTick` 时停止前进（`main.cpp:8174`），`StepForward` 用 `min(endTick, tick+1)`（`native_ui.cpp:398`），所以 `endTick` 本身是一个可停留的 tick。`endTick` 不是“最后一个事件 tick”；bagel 的 header ticks=77,466，而事件最大 tick=77,465。

### 1.2 播放循环：一个 accumulator，按帧跨多个 tick

`main.cpp:8150-8193` 的真实顺序：

1. `measuredElapsed` 来自 `QueryPerformanceCounter`；`elapsed = clamp(measuredElapsed, 0.0, 0.25)`；`measuredElapsed > 0.25` 时 `tickAccumulator = 0.0`（调试器/系统停顿保护）。
2. 暂停切换：暂停时 `soundScheduler.stop()`，恢复时 `soundScheduler.reset(g_playback.tick)`。
3. 只有 `enabled && !paused` 才累加：`tickAccumulator += elapsed * tickRate * speed`。
4. `while (tickAccumulator >= 1.0)`：正向 `++tick` 直到 `endTick`；反向 `--tick` 直到 0；`captureArmed && captureTick > 0 && tick > captureTick` 时把 `tick` 钉回 `captureTick` 并 `break`。
5. 每个推进的 tick 走 `soundScheduler.advance`；`main.cpp:8207` 打 `audio: advance tick=...`。

关键事实：**一个绘制帧可以前进多个 tick**（速度 8x、帧率 120Hz 时约 4.4 tick/帧；速度 64x 时约 35 tick/帧）。因此“请求 tick”和“画出来的帧”不是同一个东西；T70 必须把两者分开显示，不能把 `g_playback.tick` 同时当作两者。

### 1.3 scene 解析：`resolvedSceneTick` 是唯一的 actual 来源

`main.cpp:8225-8232`：

```cpp
if (g_renderer && g_playback.enabled && g_playback.tick != lastSceneTick) {
  const std::int32_t sceneTick = firstPacketTick + g_playback.tick;
  traceFmt("scene: begin tick=%d sceneTick=%d", g_playback.tick, sceneTick);
  std::int32_t resolvedSceneTick = sceneTick;
  entitySnapshotStatus = queryEntitySnapshotAtOrBeforeTick(
      demoNetworkSummary, sceneTick, currentEntityStates, &resolvedSceneTick);
  entitySnapshotStaleness = sceneTick - resolvedSceneTick;
  traceFmt("scene: snapshot status=%d stale=%d states=%zu", ...);
}
```

`queryEntitySnapshotAtOrBeforeTick` 的返回值在 `native/include/demo_header.h:416-423`：`Available`、`Checkpoint`、`NoHistory`、`TickBeforeHistory`、`Gap`、`DeltaBaseMissing`。`main.cpp:1148-1158` 的名字映射是 `available` / `checkpoint` / `no-history` / `before-window` / `gap` / `delta-base-missing` / `unknown`。

`firstPacketTick` 在 `main.cpp:5064-5067`：取 demo index 中第一个 `command == 1 || command == 2` 的 entry 的 `tick`；没有则 0。注释 `main.cpp:5040-5063` 明确它是 **SERVER tick origin**，而 `soundOriginTick` 是另一个 origin；两者曾经是同一个变量，修声音时把 scene 清空过。T70 只能用它换算 playback 域的 frame tick，不能把 `sceneTick` 当 playback tick。

### 1.4 UI 控制器：第二份 tick/playing/reverse/speed

`native/src/native_ui.cpp:365-372`：

```cpp
void NativeUiController::setPlaybackState(std::int32_t tick, bool playing,
    bool reverse, double speed) {
  if (!hasLoadedDemo() || screen_ != UiScreen::Player) return;
  tick_ = clamp(tick, 0, max(0, header_.ticks));
  playing_ = playing;
  reverse_ = reverse;
  speed_ = clamp(speed, 0.125, 8.0);
}
```

`command()`（`native_ui.cpp:374-406`）先调用 `callbacks_.command(value)`，再改自己的 `tick_`/`playing_`/`reverse_`/`speed_`。`StepBackward`/`StepForward` 改 `tick_`；`StepSecondBackward`/`StepSecondForward` 用 `kSecondStepTicks=66`；`Scrub` clamp 后改 `tick_`。`main.cpp:1955-1958` 在 WM_COMMAND 之后把 snapshot 抄回 `g_playback`；`main.cpp:1962-1982` 的 WM_HSCROLL 也一样，并把 `paused=true`。

**这是两份状态。** 任何只在 `NativeUiController` 内改 `tick_` 的路径，如果 WM_COMMAND 没有走完同步，就会出现 UI 显示与 `g_playback` 不一致；T70 的“所有操作使用同一个播放状态接口”必须消灭这份副本。

另外：`setPlaybackState` 把 speed clamp 到 `[0.125, 8.0]`，而 `g_playback.speed` 允许到 64（`main.cpp:5035`）。`--playback-speed 32` 时标题会打 `speed=32`，但 `UiSnapshot.speed` 是 8；这是现成的读数矛盾，T70 必须修。

### 1.5 键盘：transport 绕过焦点规则

`main.cpp:1993-2123` 的 WM_KEYDOWN：

- `:1998-2001` 对 `VK_SPACE`、`R`、`P`、`VK_HOME`、`VK_END`、`VK_F12`、`VK_F1..VK_F8` 的重复位直接 `return 0`。
- `:2015-2044` HUD 热键路径：`textFieldFocused = isTextFieldFocused()`；`characterKey = (A-Z or 0-9)`；`if (!shouldDispatchHotkey(textFieldFocused, characterKey))` 则 `++g_hotkeySuppressedByFocus` 并打 `hud: keys suppressed focus=1 key=%u`；否则只处理 `hud.*`。
- `:2063-2084` 相机热键路径：算 `control/alt/shift`，`character = (A-Z/0-9/SPACE/OEM_PLUS/OEM_MINUS)`，`dispatchHotkey`；`if handled or result == "suppressed-by-focus"` 更新 UI 并 return；`if (control || alt) return 0`。
- `:2085-2122` transport if 分支：HOME、END、SPACE、P、R、LEFT、RIGHT、OEM_PLUS/ADD、OEM_MINUS/SUBTRACT。**这些分支没有检查 `g_textFieldFocused`，也没有走 `dispatchHotkey`。**

`shouldDispatchHotkey` 的真实实现是 `native/src/keybinds.cpp:419-430`：

```cpp
bool shouldDispatchHotkey(bool textFieldFocused, bool keyIsCharacter) {
  if (!textFieldFocused) return true;
  (void)keyIsCharacter;
  return false;
}
```

也就是说：**输入框有焦点时，所有键都被抑制**，不只是字符键。`main.cpp:2010-2011` 的注释却写“only CHARACTER keys are withheld: an F-key or Home is a command even with a field focused”，与实现矛盾；T70 必须按实现（全部抑制）统一，并把 transport 也接进这条规则。

`isTextFieldFocused`（`main.cpp:1510-1517`）用 `GetFocus()` + `GetClassNameW`，识别 `EDIT`、`RichEdit20W`、`RichEdit50W`。产品还有合成钩子：`g_textFieldFocused`（`main.cpp:398`）、`--camera-script` 的 `focus on|off`（`main.cpp:1025-1026`）、`EN_SETFOCUS/EN_KILLFOCUS`（`main.cpp:1868-1869`，针对 `kUiBookmarkName`）。

### 1.6 标题/HUD：没有 requested vs actual

`main.cpp:5367-5412` 的标题由以下片段拼成：

- `anchorAbs=<tick>@<offset>` 或 `anchor=none`；
- `playback=<tick>/<endTick> speed=<speed> rate=<tickRate>` + `reverse|forward` + `paused|playing`；
- `HUD[` + `composePlaybackHudLine({tick, demoName, state, camera, error})` + `]`；
- 再次 `speed=<speed>`；
- `entity=<status>`、`stale=<entitySnapshotStaleness>`、`audio=<played>/<missing>`、health/team/class/clip/uber、`snd=...`。

`PlaybackHudLine`（`native/include/native_ui.h:166-175`）只有 `tick/demoName/state/camera/error` 五个字段，输出格式是 `tick=<n> demo=<name> state=<playing|paused|reverse> camera=<mode> error=<none|text>`。没有 `req`、`act`、`delta`、`frame`。

### 1.7 现有读数能证明什么、不能证明什么

| 读数 | 能证明 | 不能证明 |
| --- | --- | --- |
| `playback=<tick>` | 当前 cursor | 画面来自哪个 tick |
| `stale=<n>` | server 域实体快照滞后 | playback 域 requested-vs-actual 差（虽然数值相等，但域不同） |
| `anchorAbs=<tick>@<offset>` | 当前 cursor 对应的 demo 文件 anchor | 画面帧 tick |
| `entity=<status>` | 快照查询状态 | 具体 `resolvedSceneTick` |
| `audio: advance tick=<n>` | 声音调度推进到 n | 画面帧 tick |
| `scene: snapshot status=<n> stale=<n>` | 服务端快照状态 | 用户看到的 requested tick |

**缺口：** 没有 `frameTick`、没有 `frameDelta`、没有 `requestId`、没有 `pending/superseded`、没有 `speedSource`、没有 `appliedSeconds`、没有 `refused-*` 具名状态。T70 的设计就是补这些，而不是另造时钟。

### 1.8 现有速度/跳转/边界的真实行为

| 操作 | 代码 | 真实行为 | 问题 |
| --- | --- | --- | --- |
| 键盘 `+`/`-` | `main.cpp:2119-2120` | `speed = min(8, speed*2)` / `max(0.125, speed*0.5)` | 无 `speedSource`，无档位，无 `applied` 读数 |
| 键盘 `Shift+Left/Right` | `main.cpp:2112-2117` | ±66 tick | 0.99 秒，不是 1 秒；无 `appliedSeconds` |
| UI `StepSecond*` | `native_ui.cpp:399-400` | ±66 tick | 产品无调用者；不可达 |
| UI `Scrub` | `native_ui.cpp:402` | clamp 到 `[0,endTick]`，`playing_=false` | clamp 不是拒绝；无 `requested` 记录 |
| 键盘 `Home/End` | `main.cpp:2087-2098` | tick=0/end；Home 还 reset camera | 无 `requested` 记录；无焦点门禁 |
| `--capture-tick` | `main.cpp:8186-8190` | 超过目标时把 cursor 钉回 captureTick | 与交互 seek 共用 cursor，可能被误读为 seek |
| `--playback-speed` | `main.cpp:2716-2723`、`:5035` | `[0.125,64]` | UI snapshot clamp 8，读数矛盾 |

---

## 2. T70 设计：一个播放状态接口 + 显式落地

### 2.1 唯一接口：`PlaybackControl`

新增 `native/include/playback_control.h` 与 `native/src/playback_control.cpp`。它是**纯逻辑**：不包含 Win32、renderer、demo parser 头文件，因此可以被产品循环和 `native/tools/playback_probe.cpp` 同时使用。`g_playback` 保留为它的唯一实例（或把字段搬进类内，二者只能选一个；本文选“类拥有字段，`g_playback` 是 `PlaybackControl` 的实例”）。

```cpp
enum class PlaybackPhase { Disabled, Paused, Playing, ReversePlaying, SeekPending };

enum class PlaybackRequestKind {
  StepTicks, JumpTicks, JumpSeconds, Scrub, SetSpeed, SetPaused, SetReverse, Stop, RefreshFrame
};

enum class PlaybackRequestStatus {
  Applied, ClampedStart, ClampedEnd, SameTick, ClampedSpeed,
  RefusedDisabled, RefusedInvalidTick, RefusedOutOfRange, RefusedTextFocus, RefusedSpeedRange,
  Superseded
};

enum class FrameStatus {
  Pending, Exact, Checkpoint, BeforeWindow, Gap, NoHistory, DeltaBaseMissing,
  Invalid, NoRenderer, OriginUnknown, Superseded
};

struct PlaybackRequest {
  PlaybackRequestKind kind = PlaybackRequestKind::StepTicks;
  std::int32_t ticks = 0;        // StepTicks / JumpTicks：带符号
  double seconds = 0.0;          // JumpSeconds：带符号
  std::int32_t tick = 0;         // Scrub：绝对 tick
  double speed = 1.0;            // SetSpeed
  bool strict = false;           // true：越界拒绝；false：clamp
  bool fromKeyboard = false;
  bool textFieldFocused = false;
  const char* source = "ui";     // ui | key | script | slider | cli
};

struct PlaybackResult {
  std::uint64_t id = 0;
  PlaybackRequestStatus status = PlaybackRequestStatus::RefusedDisabled;
  std::int32_t requestedTick = 0;
  std::int32_t appliedTick = 0;
  std::int32_t deltaTicks = 0;
  double requestedSeconds = 0.0;
  double appliedSeconds = 0.0;
  double deltaSeconds = 0.0;
  double speedBefore = 1.0;
  double speedAfter = 1.0;
  const char* reason = "";
};

struct FrameLanding {
  std::uint64_t requestId = 0;       // 0 = 播放推进/刷新，不是用户请求
  bool valid = false;
  bool superseded = false;
  std::int32_t requestedTick = 0;
  std::int32_t landedCursorTick = -1;
  std::int32_t sceneTick = 0;
  std::int32_t resolvedSceneTick = -1;
  std::int32_t frameTick = -1;       // playback 域；-1 = 不可用
  std::int32_t frameDelta = -1;      // landedCursorTick - frameTick
  FrameStatus frameStatus = FrameStatus::Pending;
  std::int32_t viewTick = -1;
  std::int32_t viewDelta = -1;
  bool viewValid = false;
  const char* reason = "";
};

struct PlaybackView {
  PlaybackPhase phase = PlaybackPhase::Disabled;
  std::int32_t cursorTick = 0;
  std::int32_t endTick = 0;
  std::int32_t firstPacketTick = 0;
  bool firstPacketOriginKnown = false;
  bool paused = true;
  bool reverse = false;
  double speed = 1.0;
  double tickRate = 66.6666667;
  std::uint64_t pendingRequestId = 0;
  std::uint64_t lastRequestId = 0;
  PlaybackRequestStatus lastStatus = PlaybackRequestStatus::RefusedDisabled;
  std::uint64_t lastFrameRequest = 0;
  const char* boundary = "none";  // none | start | end
};
```

`PlaybackControl` 的公开方法（语义固定，命名可随实现微调）：

```cpp
class PlaybackControl {
 public:
  void setDemo(std::int32_t endTick, double tickRate, std::int32_t firstPacketTick,
               bool firstPacketOriginKnown, bool startPaused);
  void clear();
  PlaybackResult request(const PlaybackRequest& request);
  int advance(double elapsedSeconds);   // 返回本帧推进的 tick 数；elapsed 由调用方 clamp
  bool needsSceneResolve() const;
  std::uint64_t pendingRequestId() const;  // 0 = 无 pending
  void noteSceneResolved(std::uint64_t requestId, FrameStatus status, std::int32_t sceneTick,
                         std::int32_t resolvedSceneTick, std::int32_t viewTick, bool viewValid);
  void noteSceneUnavailable(std::uint64_t requestId, FrameStatus status, const char* reason);
  void noteSceneSuperseded(std::uint64_t requestId, const char* reason);
  void forceFrameRefresh();
  PlaybackView view() const;
  PlaybackPhase phase() const;
  const FrameLanding& lastFrame() const;
};
```

**约束：** 只有 `request()` 和 `advance()` 能改 `tick`；只有 `request()` 能改 `paused/reverse/speed`。`NativeUiController` 不再拥有可变的 `tick_/playing_/reverse_/speed_`；它的 `setPlaybackState` 改名为 `applyPlaybackView(const PlaybackView&)`，只拷贝显示字段。这样“所有操作使用同一个播放状态接口”是可检查的，而不是口号。

### 2.2 具名状态机

五个状态，没有隐藏的“半播放”状态。`atStart`/`atEnd` 是派生条件，不是状态，避免状态爆炸。

```
Disabled --openDemo(valid)--> Paused
Disabled --openDemo(invalid)--> Disabled
Paused --Step/Jump/Scrub(move)--> SeekPending
Paused --Step/Jump/Scrub(no move)--> Paused
Paused --Play--> Playing
Playing --Step/Jump/Scrub--> SeekPending
Playing --Pause--> Paused
Playing --Reverse--> ReversePlaying
Playing --advance--> Playing
ReversePlaying --Step/Jump/Scrub--> SeekPending
ReversePlaying --Pause--> Paused
ReversePlaying --Reverse--> Playing
ReversePlaying --advance--> ReversePlaying
SeekPending --sceneResolved--> Paused
SeekPending --new request--> SeekPending (old landing superseded)
SeekPending --Play--> Playing/ReversePlaying (old landing superseded)
SeekPending --Stop--> Paused
Any --Stop--> Paused
Any --closeDemo--> Disabled
```

| 状态 | 进入条件 | 不变量 | 允许的事件 |
| --- | --- | --- | --- |
| `Disabled` | 无 demo / `enabled=false` | `tick=0`、无 pending、无 landing | `openDemo`；其它一律 `RefusedDisabled` |
| `Paused` | `paused=true`、无 pending | cursor 可停在任意 `[0,endTick]`；`advance` 不改变 cursor | 所有 request；`Play`；`closeDemo` |
| `Playing` | `paused=false`、`reverse=false` | cursor 单调增；到 `endTick` 后 `advance=0` 并置 `boundary=end` | `Pause`、`Step/Jump/Scrub`、`SetSpeed`、`Reverse`、`Stop` |
| `ReversePlaying` | `paused=false`、`reverse=true` | cursor 单调减；到 0 后 `advance=0` 并置 `boundary=start` | 同 `Playing`，方向相反 |
| `SeekPending` | 有 pending 请求且 cursor 已应用 | `paused=true`；`pending=true`；等待下一次 scene resolve | 新 request（旧 landing `Superseded`）、`Play`、`Reverse`、`SetSpeed`、`Stop`、`closeDemo` |

**转移表（每个转移都写清副作用和 status）：**

| From | 事件 | 守卫 | To | 副作用 | status |
| --- | --- | --- | --- | --- | --- |
| Disabled | openDemo(valid) | header.valid | Paused | `tick=0`、`endTick=max(0,ticks)`、`tickRate=demoTickRate`、`firstPacketTick`、`lastResolvedCursor=-1`、`accumulator=0`、`paused=startPaused` | Applied |
| Disabled | openDemo(invalid) | !header.valid | Disabled | `error=header.error`；不建 cursor | RefusedDisabled |
| Disabled | 任意 request | - | Disabled | 无 | RefusedDisabled |
| Paused | Step/Jump/Scrub | 目标 != cursor | SeekPending | `paused=true`、`accumulator=0`、`requestId++`、`pending=id`、cursor=applied | Applied/ClampedStart/ClampedEnd |
| Paused | Step/Jump/Scrub | 目标 == cursor | Paused | 无 pending、无 landing | SameTick/ClampedStart/ClampedEnd |
| Paused | Play | - | Playing | `paused=false`、`accumulator=0`、pending 若存在则 `Superseded reason=resume` | Applied |
| Paused | Reverse | - | Paused | `reverse=!reverse`；若 `!reverse && tick==0` 则 `tick=endTick`（保留现有 R 行为）并 `reverseStartJump=1`；`accumulator=0` | Applied |
| Paused | SetSpeed | 在范围内 | Paused | `speed=new`、cursor 不变、`accumulator` 不变 | Applied/ClampedSpeed |
| Playing | Step/Jump/Scrub | 目标 != cursor | SeekPending | `paused=true`、`accumulator=0`、`requestId++`、`pending=id`、cursor=applied、`wasPlaying=1` | Applied/ClampedStart/ClampedEnd |
| Playing | Pause | - | Paused | `paused=true`、`accumulator=0`、pending 若存在则 `Superseded reason=pause` | Applied |
| Playing | SetSpeed | 在范围内 | Playing | `speed=new`、cursor 不变 | Applied/ClampedSpeed |
| Playing | Reverse | - | ReversePlaying | `reverse=true`；若 `tick==0` 则 `tick=endTick`；`accumulator=0`；pending 若存在则 `Superseded reason=reverse` | Applied |
| Playing | advance | - | Playing | cursor 增加，直到 `endTick`；`accumulator` 保留小数 | - |
| ReversePlaying | Step/Jump/Scrub | 目标 != cursor | SeekPending | `paused=true`、`accumulator=0`、`requestId++`、`pending=id`、cursor=applied、`wasPlaying=1` | Applied/ClampedStart/ClampedEnd |
| ReversePlaying | Pause | - | Paused | `paused=true`、`accumulator=0` | Applied |
| ReversePlaying | Reverse | - | Playing | `reverse=false`、`accumulator=0` | Applied |
| ReversePlaying | advance | - | ReversePlaying | cursor 减少，直到 0 | - |
| SeekPending | 新 request | - | SeekPending | 旧 pending `Superseded reason=new-request`；新 cursor/status | Applied/... |
| SeekPending | sceneResolved | landing id == pending id | Paused | 填 `lastFrame`；`pending=false`；`landingCount++` | - |
| SeekPending | Reverse | - | Paused | `reverse=!reverse`；pending `Superseded reason=reverse`；`accumulator=0` | Applied |
| SeekPending | SetSpeed | 在范围内 | SeekPending | `speed=new`；pending 不变；cursor 不变 | Applied/ClampedSpeed |
| SeekPending | Play | - | Playing/ReversePlaying | `paused=false`、`accumulator=0`、pending `Superseded reason=resume` | Applied |
| SeekPending | Stop | - | Paused | `tick=0`、`reverse=false`、`accumulator=0`、pending `Superseded reason=stop` | Applied/SameTick |
| Any | closeDemo | - | Disabled | `clear()`；`generation++`；所有 pending 作废 | Applied |

**七条不变量（每条都对应一个 gate 断言）：**

1. `phase==SeekPending` 蕴含 `paused==true && pending==true`。
2. `pending` 只能由 landing 或 supersede 清除；不允许静默丢弃。
3. `tick` 始终在 `[0,endTick]`；严格拒绝路径不改变 cursor。
4. `tickAccumulator` 在 `[0,1)`；`Pause`/`Seek`/`Reverse`/`Stop` 都置 0；恢复播放的第一帧不允许携带暂停前的余数。
5. cursor 变化只发生在 `request()` 或 `advance()` 内；两者都打行。
6. `lastFrame` 只对 `lastResolvedCursor == tick` 有效；否则 `frameStatus=Pending` 且 `frameTick=-1`，不允许把旧帧冒充当前帧。
7. `frameTick <= landedCursorTick` 且 `frameDelta == landedCursorTick - frameTick`；`frameStatus==Exact` 当且仅当 `resolvedSceneTick==sceneTick`。

### 2.3 请求与落地：requested / actual 的唯一算法

**请求阶段**（`request()`）：

1. 记录 `requestedTick`（或 `requestedSeconds`）与 `speedBefore`；分配 `id=++requestId`。
2. 计算目标：`StepTicks` 用 `tick + ticks`；`JumpTicks` 用绝对 `tick`；`JumpSeconds` 用 `tick + llround(seconds * tickRate)`；`Scrub` 用绝对 `tick`。
3. 越界策略：`strict=true` 且目标在 `[0,endTick]` 外 -> `RefusedOutOfRange`，**不改 cursor、不改 paused**；`strict=false` -> clamp 到边界并返回 `ClampedStart/ClampedEnd`。
4. 目标等于 cursor -> `SameTick`（`StepTicks` 在边界则为 `ClampedStart/End`），不建 pending。
5. 目标不同 -> 写 cursor，`paused=true`，`accumulator=0`，建 pending，返回 `Applied`。

**落地阶段**（`noteSceneResolved(requestId, ...)`）：

1. 只接受 `pending && requestId==pending.id` 的 landing；否则 `staleLanding++`，打 `playback: landing-stale id=<requestId> ignored=1 pending=<pendingId>`。`requestId` 必须由发起 query 的调用方在请求时捕获并原样传回；**不允许**在 landing 时用当前 pending id 补，否则迟到 landing 无法识别。
2. `landedCursorTick = tick`（此时 cursor 未变，因为 seek 已暂停）。
3. `sceneTick = firstPacketTick + landedCursorTick`。
4. 映射 `EntitySnapshotQueryStatus`：

| 查询状态 | `FrameStatus` | `frameTick` | `frameDelta` | 说明 |
| --- | --- | --- | --- | --- |
| `Available` 且 `resolvedSceneTick==sceneTick` | `Exact` | `resolvedSceneTick - firstPacketTick` | 0 | 精确帧 |
| `Checkpoint` 且 `resolvedSceneTick<sceneTick` | `Checkpoint` | 同上 | `sceneTick-resolvedSceneTick` | 回退帧，必须显示 delta |
| `TickBeforeHistory` | `BeforeWindow` | -1 | -1 | 无帧；不得猜 0 |
| `Gap` | `Gap` | -1 | -1 | 无帧；不得回退到最近 checkpoint |
| `NoHistory` | `NoHistory` | -1 | -1 | 无帧 |
| `DeltaBaseMissing` | `DeltaBaseMissing` | -1 | -1 | 无帧 |
| 其它 / `resolvedSceneTick>sceneTick` / `frameTick<0` | `Invalid` | -1 | -1 | 防御性 |

5. `frameDelta` 与现有 `entitySnapshotStaleness` 数值相等，因为 origin 相消：`(sceneTick-firstPacketTick)-(resolvedSceneTick-firstPacketTick)=sceneTick-resolvedSceneTick`。这是可断言的恒等式，也是“没有第二个 origin”的证明。
6. `viewTick = sample.tick - firstPacketTick`；`viewDelta = sceneTick - sample.tick`；`viewStatus=exact|stale|missing`。观察者视图有 65,536 样本上限（T64：bagel 65,536、snakewater 64,333），超出后早期 tick 没有 view，必须显示 `view=none`，不能复用最后一个样本。
7. `lastResolvedCursor = landedCursorTick`；`pending=false`；打 `playback: landing`。

**无 renderer**：`noteSceneUnavailable(requestId, NoRenderer, "no-renderer")`；`act=none frame=no-renderer`；cursor 仍然可动，但不声称有画面。

**origin 不可用**：`firstPacketOriginKnown=false` 时 `frameStatus=OriginUnknown`、`frameTick=-1`；不得用 `firstPacketTick=0` 硬减。

### 2.4 逐帧、1 秒/5 秒、速度档

#### 2.4.1 逐帧

- `StepForward`：`request({kind=StepTicks, ticks=+1, strict=false})`；目标越界时 `ClampedEnd`，cursor 停在 `endTick`，不建 pending。
- `StepBackward`：`ticks=-1`；在 0 时 `ClampedStart`。
- 逐帧**总是先暂停**（`paused=true`），因此落地时 cursor 不会继续跑；这是“暂停下逐帧”和“播放中逐帧”共用一条路径的原因。
- 逐帧只保证 cursor 增量 ±1；`act` 可能不变（同一 checkpoint 覆盖多个 tick），必须显示 `delta`。**验收断言 cursor 增量，不断言画面帧号增量。**

#### 2.4.2 1 秒 / 5 秒

```cpp
std::int32_t playbackTicksForSeconds(double seconds, double tickRate) {
  if (!(tickRate > 0.0) || !std::isfinite(seconds)) return 0;
  return static_cast<std::int32_t>(std::llround(seconds * tickRate));
}
```

- 1 秒：`llround(1.0 * rate)`；66.666667 -> 67；66.0 -> 66；128.0 -> 128。
- 5 秒：66.666667 -> 333；66.0 -> 330；128.0 -> 640。
- `appliedSeconds = appliedTicks / tickRate`；`deltaSeconds = appliedSeconds - requestedSeconds`。66.666667 的 1 秒：`appliedSeconds=1.005`、`deltaSeconds=+0.005`；5 秒：`4.995`、`-0.005`。
- `StepSecondBackward/Forward` 保留枚举名但改为调用该函数；新增 `StepFiveSecondsBackward/Forward`。旧的 `kSecondStepTicks=66` 删除。
- 短跳转在边界用 clamp（`ClampedEnd/Start`），因为按钮语义是“尽量走 1 秒”，不是“拒绝”。显式 `JumpTicks strict=true` 才拒绝。

#### 2.4.3 速度档

交互档位固定为 `0.125, 0.25, 0.5, 1, 2, 4, 8`。`speedUp/speedDown` 在档位间移动；到 8/0.125 返回 `ClampedSpeed`。`speedReset` 回 1.0。

`--playback-speed` 仍是自动化入口，范围 `[0.125,64]`；它不是交互档位。`UiSnapshot.speed` 必须显示真实值（修 `native_ui.cpp:371` 的 8 上限），并加 `speedSource=cli|key|ui`。从 CLI 的 32 按一次 `speedDown`：`ClampedSpeed` 到 8，trace 记录 `speedBefore=32 speedAfter=8`。

纯 `advance(1.0)` 的期望值（rate=66.666667，end 足够大）：

| speed | ticks | 计算 |
| --- | --- | --- |
| 0.125 | 8 | floor(8.333) |
| 0.25 | 16 | floor(16.667) |
| 0.5 | 33 | floor(33.333) |
| 1 | 66 | floor(66.667) |
| 2 | 133 | floor(133.333) |
| 4 | 266 | floor(266.667) |
| 8 | 533 | floor(533.333) |

`advance` 不负责 0.25 秒的 stall clamp；`main.cpp` 在调用前已经做了 `elapsed = clamp(measured,0,0.25)` 和 `measured>0.25 -> accumulator=0`。把 stall 策略留在循环里，`advance` 只做算术，这样纯探针可以传 1.0 秒而不被 0.25 截断。

### 2.5 边界、不可达、拒绝

| 情况 | 判定 | 行为 | status / FrameStatus |
| --- | --- | --- | --- |
| `tick < 0` 或 `> endTick`，`strict=true` | 无效目标 | 不改 cursor/paused | `RefusedOutOfRange`，`reason=tick-out-of-range` |
| `tick` 非整数 / NaN / 超 int32 | 无效目标 | 不改 | `RefusedInvalidTick`，`reason=invalid-tick` |
| `tick < 0` 或 `> endTick`，`strict=false` | clamp | 移到边界 | `ClampedStart/End` |
| 目标 == cursor | 同 tick | 不建 pending | `SameTick` |
| 在范围内但 `BeforeWindow/Gap/NoHistory/DeltaBaseMissing` | 不可达帧 | cursor 可到，`act=none` | 对应 `FrameStatus`，不是拒绝 |
| 无 demo | 不可操作 | 无 cursor | `RefusedDisabled` |
| 输入框有焦点 | 焦点优先 | 不改 | `RefusedTextFocus`，`reason=text-focus` |
| speed 不在 `[0.125,64]` | 无效速度 | 不改 | `RefusedSpeedRange` |

**“不可达 tick”要分两类说：** `endTick` 之外是**拒绝**（strict）或 clamp；`endTick` 之内但实体历史没有 exact 帧是**正常回退**，不是拒绝。把两者混成一个 `error` 就是 T70 要避免的“无效目标没有具名拒绝”。

### 2.6 快速连按与 seek 中断

**快速连按：** 每个 request 分配单调 `id`；cursor 立即应用；pending 只保留最后一个。前面的 pending 在下一 request 到来时补打 `playback: request id=<old> ... status=superseded reason=new-request`（保留原请求字段），并 `superseded++`。最终 cursor = 最后一个目标；最终 landing id = 最后一个 request。

**中断：**

- `SeekPending + Play`：pending 变 `Superseded reason=resume`；播放从当前 cursor 继续；旧 landing 即使稍后到达也因 `id != pending.id` 被丢弃。
- `SeekPending + Stop`：pending `Superseded reason=stop`；cursor=0。
- `SeekPending + Reverse`：pending `Superseded reason=reverse`；方向切换。
- `closeDemo/openDemo`：`generation++`；旧 generation 的 landing 一律 `staleLanding`，不能写进新 demo。
- 同一个绘制帧内多个 request：只有最后一个 pending；`requestId` 连续；`superseded` 计数等于 request 数减 1（若每个都移动）。

**“落地晚到”处理是验收重点：** `noteSceneResolved` 必须先比对 `pending.id`，再写 `lastFrame`；没有这个比对，快速连按的最后画面可能被前一个请求覆盖。

### 2.7 键盘与焦点

#### 2.7.1 现有默认表（不能破坏）

`native/src/keybinds.cpp:392-403` 的真实默认：F1..F8 = `camera.bookmark.jumpN`；Ctrl+F1..F8 = `camera.bookmark.saveN`；Ctrl+Shift+F1..F8 = `camera.bookmark.deleteN`；F9=`camera.freeFly`；F10=`camera.resetToPlayer`；F11=`camera.resetView`。

#### 2.7.2 新增 playback actions

把 transport 从 `main.cpp:2085-2122` 的 if 分支搬进同一张 `KeyBindTable`。建议默认（实现时必须用 `conflicts()` 验证，冲突则取下一个空闲 chord，并把实际 chord 打进 trace；不得静默覆盖）：

| action | 建议默认 | 允许重复 | 行为 |
| --- | --- | --- | --- |
| `playback.pause` | P | 否 | `SetPaused !paused` |
| `playback.reverse` | R | 否 | `SetReverse` |
| `playback.home` | Home | 否 | `Scrub 0` + `renderer.resetCamera()`（保留现有 Home 的相机复位） |
| `playback.end` | End | 否 | `Scrub endTick` |
| `playback.stepForward` | Right | 是 | `StepTicks +1` |
| `playback.stepBackward` | Left | 是 | `StepTicks -1` |
| `playback.jump1sForward` | Shift+Right | 是 | `JumpSeconds +1` |
| `playback.jump1sBackward` | Shift+Left | 是 | `JumpSeconds -1` |
| `playback.jump5sForward` | Ctrl+Right | 是 | `JumpSeconds +5` |
| `playback.jump5sBackward` | Ctrl+Left | 是 | `JumpSeconds -5` |
| `playback.speedUp` | OEM_PLUS | 是 | 下一档 |
| `playback.speedUpNumpad` | ADD | 是 | 同 `speedUp` |
| `playback.speedDown` | OEM_MINUS | 是 | 上一档 |
| `playback.speedDownNumpad` | SUBTRACT | 是 | 同 `speedDown` |

`F12` 仍是 overlay 切换，`SPACE` 仍是相机模式循环；它们不是 playback action，留在原路径。

#### 2.7.3 焦点规则

- `dispatchHotkey` 已经是所有表 action 的唯一入口（`main.cpp:642-675`）：先 `shouldDispatchHotkey(g_textFieldFocused, character)`；抑制则 `g_keyBinds.noteSuppressedByFocus()`、`++g_hotkeySuppressedByFocus`、返回 `suppressed-by-focus`。
- 把 transport 搬进表后，`Right/Left/P/R/Home/End/+/-` 全部走这条谓词；**输入框有焦点时一个都不触发**。这修正了 `main.cpp:2010-2011` 注释与 `keybinds.cpp:419-430` 实现的矛盾。
- `isTextFieldFocused()` 仍用 `GetFocus()` 类名判断；合成的 `focus on|off` 仍可强制该布尔，供脚本在无真实 EDIT 焦点时验证谓词。
- `WM_KEYDOWN` 的重复位过滤只保留给非表路径（F12、SPACE）；表 action 的重复由 `handlePlaybackAction(action, repeated)` 的 `repeatAllowed` 决定。`P/R/Home/End` 忽略重复；`Right/Left/Shift/Ctrl 箭头/+/-` 允许重复。
- 冲突：`KeyBindTable::conflicts()` 覆盖 camera + playback；`defaultKeyBindings()` 注册后断言 `conflicts().empty()`。HUD 的 `hudActionForKey` 是另一张表（T67/T68 要统一），T70 至少要在启动时把 HUD 的 chord 与 playback 的 chord 做一次冲突检查，冲突则 `conflictCount++` 并拒绝启动绑定（具名错误，不静默）。

### 2.8 UI 控件与布局

`UiControlId`（`native/include/native_ui.h:122-134`）按 T51 规则**只在 `Count` 前追加**，不得移动已有 rect：

```cpp
PlaybackStepBack, PlaybackStepForward,
PlaybackJump1Back, PlaybackJump1Forward,
PlaybackJump5Back, PlaybackJump5Forward,
PlaybackSpeedDown, PlaybackSpeedUp, PlaybackSpeedReset,
PlaybackStatus,
```

`planUiLayout` 扩展这些 rect；已有 `Status/Open/.../KeybindReset` 的 rect 必须逐字段不变（gate 用 rect 表 hash 断言；mutation：在 `Count` 前插入导致后续 rect 移位 -> red）。

`UiCommand` 追加：`StepFiveSecondsBackward`、`StepFiveSecondsForward`、`SpeedSlower`、`SpeedFaster`、`SpeedReset`。`StepSecond*` 保留但改语义为 1 秒计算。`Scrub` 仍是 slider 的绝对 tick 请求。

`UiSnapshot` 追加（放在末尾，默认值保证旧探针可编译）：

```cpp
std::int32_t frameTick = -1;
std::int32_t frameDelta = -1;
std::string frameStatus;      // exact|checkpoint|before-window|gap|no-history|delta-base-missing|pending|invalid|no-renderer|origin-unknown
std::int32_t viewTick = -1;
std::int32_t viewDelta = -1;
std::string viewStatus;       // exact|stale|missing
std::string playbackPhase;    // disabled|paused|playing|reverse-playing|seek-pending
std::uint64_t requestId = 0;
std::string requestStatus;    // applied|clamped-start|clamped-end|same-tick|clamped-speed|refused-*
std::string requestReason;
std::string speedSource;      // cli|key|ui
```

`NativeUiController::command()` 不再改自己的 `tick_/playing_/reverse_/speed_`；它只调用 `callbacks_.command(value)`（main 的 `applyPlaybackRequest`），然后等待下一帧 `applyPlaybackView`。slider 的 WM_HSCROLL 也只发 `Scrub` 请求，不直接写 `g_playback.tick`。

`setPlaybackState` 的 speed clamp 改为 `[0.125,64]`，与 `g_playback` 一致；交互 request 自己在 `[0.125,8]` 档位内 clamp。`UiSnapshot.durationSeconds` 仍来自 `header_.playbackTime`，T70 另外显示 `time=tick/rate`（两者可能差几毫秒，不能混用）。

### 2.9 标题与 trace 的精确格式

#### 2.9.1 标题

在现有 `playback=<cursor>/<endTick>` 后追加：

```
cursor=<n> req=<n> act=<n|none> delta=<n|na> frame=<status> view=<n|none> vdelta=<n|na> phase=<phase> speed=<x> rate=<r>
```

示例（1 秒跳转后落在 checkpoint）：

```
playback=1067/77466 cursor=1067 req=1067 act=1000 delta=67 frame=checkpoint view=1067 vdelta=0 phase=paused speed=1.000 rate=66.666667
```

- `cursor`：当前 cursor；`req`：最近一次请求的 requested tick（无请求时等于 cursor）；`act`：`lastFrame.frameTick`，仅当 `lastFrame.valid && lastResolvedCursor==cursor`，否则 `none`；`delta=cursor-act`，`act=none` 时为 `na`。
- `frame`：`FrameStatus` 的小写名；`act=none` 时仍必须有 `frame`（`before-window`/`gap`/`pending`/`no-renderer`/`origin-unknown`）。
- `view`：观察者视图的 playback 域 tick；没有则 `none`。
- `phase`：状态机名；`speed`：真实值；`rate`：`demoTickRate`。
- 一行，CR/LF/TAB 全部压成空格；用户可控的 demo 名/错误文本先 sanitize。

`composePlaybackHudLine` 增加一个重载，接受 `PlaybackFrameFields` 并把上述字段追加到 `HUD[...]` 内；旧的五字段重载保留给现有探针，避免一次性改所有调用者。

#### 2.9.2 trace 行

```
playback: state phase=%s cursor=%d end=%d paused=%d reverse=%d speed=%.6f rate=%.6f firstPacketTick=%d origin=%s pending=%llu lastRequest=%llu lastStatus=%s lastFrameRequest=%llu boundary=%s
playback: request id=%llu kind=%s source=%s requestedTick=%d requestedSeconds=%.6f appliedTick=%d cursor=%d appliedSeconds=%.6f deltaTicks=%d deltaSeconds=%.6f speedBefore=%.6f speedAfter=%.6f strict=%d status=%s reason=%s
playback: landing id=%llu requestedTick=%d landedCursor=%d sceneTick=%d resolvedSceneTick=%d frameTick=%d frameDelta=%d frameStatus=%s viewTick=%d viewDelta=%d viewValid=%d
playback: landing-stale id=%llu ignored=1 pending=%llu
playback: summary requests=%zu applied=%zu clampedStart=%zu clampedEnd=%zu sameTick=%zu refused=%zu superseded=%zu landings=%zu exact=%zu checkpoint=%zu unavailable=%zu stepsForward=%zu stepsBackward=%zu jumps1s=%zu jumps5s=%zu speedChanges=%zu advanceTicks=%zu elapsedSeconds=%.6f maxFrameDelta=%d focusSuppressed=%zu staleLandings=%zu
playback: key vk=%d ctrl=%d alt=%d shift=%d action=%s result=%s
```

- `status` 用小写 kebab：`applied|clamped-start|clamped-end|same-tick|clamped-speed|refused-disabled|refused-invalid-tick|refused-out-of-range|refused-text-focus|refused-speed-range|superseded`。
- `frameStatus` 用 `FrameStatus` 小写名；`frameTick=-1` 表示不可用，不用 0。
- `playback: state` 只在 phase 变化或 request 后打，避免每帧刷屏；`playback: summary` 在退出时打。
- `playback: key` 的 `action` 是脚本驱动在调用 `dispatchHotkey` **之前**用 `g_keyBinds.actionForKey(chord)` 解析出的 action；`dispatchHotkey` 在焦点抑制时会清空 action，因此不能用清空后的值做断言。`result` 取 `dispatchHotkey` 的返回值/原因（`suppressed-by-focus`、`unbound`、`ok:<reason>`）。
- 计数器的 `refused` 是所有 `Refused*` 之和；`superseded` 是所有 `Superseded` 之和；`unavailable` 是 `BeforeWindow+Gap+NoHistory+DeltaBaseMissing+Invalid+NoRenderer+OriginUnknown` 之和。

### 2.10 纯探针与脚本驱动

#### 2.10.1 `native/tools/playback_probe.cpp`（纯逻辑）

不建窗口、不读 demo；只测 `PlaybackControl`：

1. `advance` 表：rate 66.666667、speed 0.125..8、1 秒的期望 tick 数；边界 start/end；pause 不推进；reverse 递减；accumulator 余数。
2. `request` 表：Step/Jump/JumpSeconds/Scrub/SetSpeed/SetPaused/SetReverse/Stop；strict/clamp/same-tick/refused；每个 status。
3. `landing` 表：6 种 `EntitySnapshotQueryStatus` 到 `FrameStatus` 的映射；`frameTick/frameDelta`；`act=none`；origin unknown；`resolved>scene` 防御。
4. 状态机矩阵：5 状态 × 12 事件，断言 phase/status/不变量。
5. 标题 compose：固定 fixture 的完整字符串逐字节比较；字段缺失/换行注入必须失败。
6. 退出码：`checks=<n> failures=<n>`；`failures>0` -> 1。

#### 2.10.2 `--playback-script`（产品集成）

仿 `--camera-script`（`main.cpp:735-1104`）：逐行命令，空白/# 跳过，每步打 `playback: accept step=%zu cmd=%s result=%s detail=%s`，失败 `stepsFailed++`，`quit` 设 `stepsFailed==0?0:1`。命令：

```
step <+n|-n>
jump <tick> [strict]
jump-seconds <sec>
scrub <tick>
speed <x>
pause | resume | reverse | stop
frames <n>
wait-ms <n>
settle <maxFrames>
burst-step <n>
interrupt <tick>
expect-cursor <n>
expect-cursor-gt <n>
expect-phase <name>
expect-frame <tick|none>
expect-frame-status <name>
expect-delta <n|na>
expect-last-frame-request <id|none>
expect-title <field>=<value>
mark-tick
expect-same-tick
focus on|off
key <vk> [ctrl] [alt] [shift]
capture <path> [label]
summary
quit
```

`settle` 等 `pending==false`，超时 `fail("landing-timeout")`；这是把“请求发了但没落地”判成 INVALID 的关键，不能只看 request 行就 PASS。`wait-ms` 让产品 loop 真实走 N 毫秒（`frames 60` 不是 1 秒，禁止用它近似时间）；`burst-step N` 在同一脚本步内发 N 个 `StepTicks +1` 请求，中间不 `advance`，用于快速连按；`interrupt <tick>` 在同一脚本步内先 `Scrub` 再 `SetPaused(false)`，用于 seek 中断；`mark-tick`/`expect-same-tick` 复用 camera-script 的精确 tick 比较语义。

---

## 3. T70 验收：读数与 mutation

**运行时验收要求（本文未执行）**：所有 T70 行设计为在同一台机器、同一份 `bagel` SourceTV demo（`4a9bfb...dem`，sha256 `03529698...`）上执行；`snakewater` 作为第二样本复跑 A1/A4/A5/A7/A11。命令统一为：

```
tf2_demo_native.exe --demo <path> --hidden-window --audio-disabled --start-paused --playback-script evidence/t70-t72/scripts/t70-accept.txt
```

`--playback-script` 的 `quit` 退出码 0 只表示脚本步骤都执行，不表示验收通过；验收看每一行的 `playback:` 读数。**缺字段、`settle` 超时、`steps=0` 一律 INVALID，不是 PASS。**

**每行独立运行：** 每个 A 行使用独立脚本文件/独立进程，`playback: summary` 计数从 0 开始；禁止把多行拼进一个脚本后断言累计 `summary` 值（否则 A3/A11/A12 的计数不可解释）。

**字段级断言：** 表中 trace 行只列必须断言的字段；实现可以多打字段，但列出的字段必须存在且精确匹配；缺字段 = INVALID，不是 PASS。

**demo 数据对账 fixture：** A1/A2 的 `frameTick/frameStatus` 期望值必须来自 T64 oracle 的 `frame` 记录（按 cursor tick 选取有 `frame` 的事件记录，见 `evidence/t64-events/DATA-CONTRACT.md` 的 `frame.tick/status/stalenessTicks`），不能只靠 `frameDelta == cursor-frameTick` 的算术恒等式。gate 读取 `evidence/t70-t72/fixtures/t70-frame-map.tsv`（列：`cursorTick\texpectedFrameTick\texpectedFrameStatus\texpectedFrameDelta`，由 T64 oracle 生成）。**域转换：** T64 `frame.tick` 是绝对 scene tick，fixture 的 `expectedFrameTick` 必须是 `frame.tick - firstPacketTick`（T70 playback 域），不能直接抄绝对 tick；`firstPacketTick` 取 T64/T63 记录的 origin，缺失则该行 INVALID。`frame.status` 的 `exact/checkpoint/before-history/unavailable` 映射到 T70 的 `exact/checkpoint/before-window/...`，映射表随 fixture 一起提交。该 tick 无 fixture 行时，A1/A2 判 INVALID，不得用恒等式冒充“与 demo 数据一致”。

### 3.1 正向验收表

| ID | 验收点（NEXT-TASKS T70） | 必须断言的读数（精确字段） | 通过条件 | 能让它变红的 mutation |
| --- | --- | --- | --- | --- |
| T70-A1 | 逐帧前进，actual tick 增量与 demo 数据一致 | `step +1`；`settle`；`playback: request kind=StepTicks requestedTick=<n+1> appliedTick=<n+1> status=applied`；`playback: landing id=<id> landedCursor=<n+1> sceneTick=<firstPacketTick+n+1> resolvedSceneTick=<r> frameTick=<f> frameDelta=<n+1-f> frameStatus=<exact 或 checkpoint>`；标题 `cursor=<n+1> req=<n+1> act=<f> delta=<n+1-f> frame=<status>`；fixture 行 `cursorTick=<n+1> expectedFrameTick=<f> expectedFrameStatus=<status> expectedFrameDelta=<n+1-f>` | `appliedTick == cursor_before+1`；`frameTick == fixture.expectedFrameTick`；`frameStatus == fixture.expectedFrameStatus`；`frameDelta == landedCursor-frameTick == fixture.expectedFrameDelta`；`frameStatus==exact` 时 `frameDelta==0`，`checkpoint` 时 `frameDelta>0`；fixture 缺失 = INVALID | `StepTicks` 的 `ticks` 改成 +2：`appliedTick=<n+2>`，A1 的 `cursor_before+1` 断言红；或把 `frameDelta` 强制 0 而 `frameStatus=checkpoint`，delta 恒等式红；或选一个 fixture 期望 `exact` 的 tick 却报 `checkpoint`，fixture 比对红 |
| T70-A2 | 逐帧后退，actual tick 增量 -1 | `step -1`；`settle`；`request kind=StepTicks requestedTick=<n-1> appliedTick=<n-1>`；`landing landedCursor=<n-1> frameDelta=<n-1-f> frameTick=<f> frameStatus=<status>`；fixture 行 `cursorTick=<n-1> expectedFrameTick=<f> expectedFrameStatus=<status>` | `appliedTick == cursor_before-1`；`frameTick/frameStatus` 与 fixture 一致；`frameDelta` 恒等式 | `ticks=-2` -> `appliedTick=<n-2>` 红；或 `StepBackward` 写成 no-op -> `status=same-tick`、cursor 不变红；或 fixture 期望 `checkpoint` 却报 `exact`，红 |
| T70-A3 | 暂停下逐帧后，等待 1 秒 cursor 不动 | 先 `expect-cursor <n>`；`step +1`；`wait-ms 1000`（真实 1 秒，不用 `frames 60` 近似）；`expect-cursor <n+1>`；`playback: summary advanceTicks=0` | 1 秒后 cursor 仍 `n+1`；`advanceTicks` 在 step 后为 0 | 让 `advance()` 忽略 `paused`：1 秒后 cursor 到 `n+67`，`expect-cursor` 红 |
| T70-A4 | 1 秒短跳，tick 增量正确 | `request kind=JumpSeconds requestedSeconds=1.000000 requestedTick=<n+67> appliedTick=<n+67> deltaTicks=67 appliedSeconds=1.005000 deltaSeconds=0.005000`（rate=66.666667） | `deltaTicks == llround(1.0*rate) == 67`；`appliedSeconds == 67/rate == 1.005`；标题 `delta=67` | 恢复 `kSecondStepTicks=66`：`deltaTicks=66`、`appliedSeconds=0.990`，A4 的 67 断言红；或只改标题 `delta=67` 而 cursor +66，`expect-cursor` 红 |
| T70-A5 | 5 秒短跳，tick 增量正确 | `request kind=JumpSeconds requestedSeconds=5.000000 appliedTick=<n+333> deltaTicks=333 appliedSeconds=4.995000 deltaSeconds=-0.005000` | `deltaTicks == llround(5.0*rate) == 333` | `5*66=330` -> `deltaTicks=330` 红；或 clamp 到 `endTick` 时把 `deltaTicks` 写成 0 而 cursor 到边界，恒等式红 |
| T70-A6 | 1s/5s 跳转与速度档有独立读数 | `request kind=SetSpeed speedBefore=1.000000 speedAfter=2.000000 status=applied`；`playback: state speed=2.000000`；纯探针 `advance(1.000000)` 在 1x/2x 分别 `advanceTicks=66/133`；`UiSnapshot.speed=2.000000`；标题 `speed=2.000000` | 1x 与 2x 各有独立 `advance(1.000000)` 读数（66/133），不是从 1s 跳转的 67 推断；`speed` 字段等于真实值 | 只改 UI 标签：`speedAfter=2` 但 `advanceTicks=66`，A6 的 133 断言红；或 `native_ui.cpp:371` 仍 clamp 8，CLI `--playback-speed 32` 显示 `8.000000`，标签读数红 |
| T70-A7 | 播放中 seek，落地后暂停且不继续 | 播放 1x 到 `<n>`；`scrub <m>`；`request kind=Scrub appliedTick=<m> status=applied`；`playback: state phase=seek-pending paused=1`；`settle` 后 `landing landedCursor=<m> frameStatus=<...>`；再 `wait-ms 1000`；`expect-cursor <m>` | `phase=seek-pending` 时 `paused=1`；settle 后 `landedCursor==m`；1 秒后 cursor 仍 `m` | 去掉 `Scrub` 的 `paused=true`：settle 后 cursor 继续 `m+66`，`expect-cursor` 红；或 landing 后不暂停，同样红 |
| T70-A8 | 首/末边界 | 起点 cursor 在中间：`key 0x24`（VK_HOME）-> `request kind=Scrub requestedTick=0 appliedTick=0 status=applied`；`key 0x23`（VK_END）-> `request kind=Scrub requestedTick=<endTick> appliedTick=<endTick> status=applied`；在 0 按 `key 0x25`（Left）-> `status=clamped-start appliedTick=0`；在 end 按 `key 0x27`（Right）-> `status=clamped-end appliedTick=<endTick>`；再在 0 按 `key 0x24` -> `status=same-tick appliedTick=0`，在 end 按 `key 0x23` -> `status=same-tick`；标题 `cursor=0` / `cursor=<endTick>` | `Home/End` 在 cursor 不同时为 `applied`、已在该 tick 时为 `same-tick`；`clamped-start/clamped-end` 精确；cursor 不越界 | 去掉 clamp：在 0 按 Left 得 `appliedTick=-1`，范围断言红；或 `End` 写成 `endTick-1`，边界读数红；或已在 0 的 `Home` 仍报 `applied`，same-tick 断言红 |
| T70-A9 | 不可达 tick 有具名拒绝，且不移动 | `jump 999999 strict` -> `status=refused-out-of-range reason=tick-out-of-range requestedTick=999999 appliedTick=<before> cursor=<before>`；`jump -1 strict` 同样；`expect-cursor <before>` | status/reason 精确；cursor 前后相同；**不是**通用 `error` | 把拒绝改成 clamp：`appliedTick=endTick`、cursor 移动，A9 红；或只打 `error=bad` 没有 `refused-out-of-range`，具名断言红 |
| T70-A10 | 范围内但无帧是正常回退，不是拒绝 | 用 fixture 确认的 `tickBeforeHistory`（由 `t70-frame-map.tsv` 的 `expectedFrameStatus=before-window` 行给出；**不要硬编码 45**，除非该行确认）`jump <tickBeforeHistory>`；`settle`；`status=applied appliedTick=<tick>`；`landing frameStatus=before-window frameTick=-1 frameDelta=-1`；标题 `act=none delta=na frame=before-window` | cursor 到该 tick；`frameStatus` 是 `before-window`；`act=none`；**没有** `refused-*` | 把 `before-window` 当拒绝：`status=refused-out-of-range` 且 cursor 不动，A10 红；或把 `frameTick` 填 0 冒充帧，`act=0` 且 `frameStatus` 矛盾，红；或硬编码 45 但 fixture 该 tick 实际有帧，期望状态红 |
| T70-A11 | 快速连按：最终 cursor 与最终 landing 一致 | `burst-step 10`（同一脚本步内连续 10 次 `StepTicks +1`，中间不 `advance`）；`settle`：`request id=1..10`；`superseded=9`；`summary requests=10 applied=10 superseded=9`；最后一次 `landing id=10 landedCursor=<n+10> frameDelta=<n+10-f>`；`expect-cursor <n+10>`；`expect-last-frame-request 10` | 最终 cursor = n+10；最终 landing id=10；`superseded=9`；`lastFrame.requestId==10` | 去掉 pending id 比对：第 9 个 landing 覆盖第 10 个，`expect-last-frame-request 10` 红；或 pending 只保留第一个，cursor 只 +1，红；或把 10 次拆到 10 帧导致每次都落地，`superseded=0`，A11 红 |
| T70-A12 | seek 被播放中断：旧 landing 不覆盖新状态 | `interrupt 3000`（同一脚本步内先 `Scrub 3000` 再 `SetPaused(false)`）；`wait-ms 100`；`request id=1 kind=Scrub status=superseded reason=resume`；`request id=2 kind=SetPaused status=applied`；`playback: landing-stale id=1 ignored=1 pending=0`（`SetPaused` 不是 seek，不建 pending）；`expect-cursor-gt 3000`；`summary superseded=1 staleLandings=1` | 旧 id 被标记 superseded；cursor 从 3000 继续增大；stale landing 不写 `lastFrame`；`lastFrame.requestId` 不是 1；pending 已清 0 | 删除 id 检查：旧 landing 把 cursor 写回 3000 或 `lastFrame` 变成旧请求，A12 的 `cursor-gt`/`lastFrame` 断言红；或 `SetPaused` 错误地保留 pending，`pending=0` 断言红 |
| T70-A13 | 输入框焦点不触发逐帧/跳转 | `focus on`；`key 0x27`（Right）-> `playback: key action=playback.stepForward result=suppressed-by-focus`；`expect-cursor <n>`；`focus off`；`key 0x27` -> `result=ok:applied`、cursor `<n+1>` | focus on 时 cursor 不变且 `result=suppressed-by-focus`；focus off 后 +1 | 保留 `main.cpp:2085-2122` 的 transport if 分支（绕过 `shouldDispatchHotkey`）：focus on 时 Right 仍 +1，A13 红 |
| T70-A14 | 不伪造精度：无帧时 `act=none` | 在 `before-window`/`gap`/`no-history` 区域：标题 `act=none delta=na frame=<status>`；`UiSnapshot.frameTick=-1`；`frameDelta=-1` | `act` 不是 cursor；`frameTick=-1`；`frameStatus` 非 `exact` | 把 `act` 填成 cursor：`act=<n>`、`frameStatus=before-window`，A14 红；或 `frameDelta=0`，红 |
| T70-A15 | 速度档位表与 0.25 秒 stall 策略分离 | 纯探针 `playback_probe`：speed 0.125/0.25/0.5/1/2/4/8 的 `advance(1.0)` 分别 `8/16/33/66/133/266/533`；产品脚本 `--playback-speed 32` 的 `UiSnapshot.speed=32.000000`，`summary speedChanges` 记录 | 七个数精确；CLI 32 不被 8 clamp；`advance` 不自行做 0.25 秒截断（纯探针传 1.0 仍得全量） | 把 `advance` 里加 0.25 秒 clamp：纯探针 1.0 秒只推进 16 tick，A15 红；或 speed 表用 66 固定，0.125 得 8、8x 得 528（不是 533），红 |
| T70-A16 | 不新增剪辑/导出路径 | 静态：`git diff --stat` 只含 `playback_control.*`、`main.cpp`、`native_ui.*`、探针/脚本；`grep -R "clip\|export" native/include/playback_control.h native/src/playback_control.cpp` 为空；`UiCommand::Export` 仍只走原导出路径 | 无剪辑/导出新增符号；`UiCommand` 没有 `Clip*` | 在 `playback_control.h` 加 `ExportClip` 或 main 加 `clip` 分支，A16 静态断言红 |

### 3.2 T70 负控（每条都必须能让 A 行变红）

| 负控 | 注入 | 预期红行 | 为什么不是 PASS |
| --- | --- | --- | --- |
| 逐帧被跳过 | `StepTicks` 改成 no-op，只打 request 行 | A1/A2 | request 行仍 `applied`，但 `appliedTick==cursor`，`appliedTick==before+1` 断言红；只看到 request 不能 PASS |
| 速度只改标签 | `SetSpeed` 改 `speed` 字段，`advance` 仍用旧速度 | A6/A15 | `advanceTicks` 不变，133/66 断言红；标签读数单独看会骗过 |
| seek 不落地 | `Scrub` 只改 cursor，不调用 `noteSceneResolved` | A7 | `settle` 超时 -> `landing-timeout` INVALID；没有 landing 就不能 PASS |
| seek 不暂停 | `Scrub` 不置 `paused` | A7/A8 | 1 秒后 cursor 继续走，`expect-cursor` 红 |
| 迟到 landing 覆盖 | 去掉 `pending.id` 比对 | A11/A12 | 最终 `lastFrame.requestId` 不是最后请求，或 cursor 回退，红 |
| 暂停仍推进 | `advance` 不检查 `paused` | A3/A8 | `expect-cursor` 红；`advanceTicks` 非 0 |
| 边界不 clamp | 去掉 `max(0,...)`/`min(endTick,...)` | A8/A9 | cursor 越界，范围断言红 |
| 拒绝无具名 | 越界只打 `error=bad` | A9 | `status=refused-out-of-range` 缺失，具名断言红 |
| 伪造 exact | `frameStatus=exact` 但 `frameDelta>0` | A1/A14 | 恒等式红；`exact` 的 `frameDelta==0` 断言红 |
| 焦点被绕过 | transport if 分支仍在 | A13 | focus on 时 cursor 变化，`suppressed-by-focus` 断言红 |
| 字段缺失 | 标题不输出 `act/frame/delta` | A1/A14 | 缺字段 = INVALID，不是 PASS；gate 必须把缺失当失败 |

### 3.3 T70 静态断言

以下断言在**实现后**执行；当前基线尚未满足（`kSecondStepTicks` 仍在），不能把本节当现状。

1. `grep -R "kSecondStepTicks" native` 为空（旧 66 常量删除；只允许设计文档提及）。
2. `grep -R "g_playback\.tick *=" native/src/main.cpp` 只剩 `PlaybackControl` 的 `advance`/`request` 内部（或 `g_playback` 是 `PlaybackControl` 实例，外部无赋值）。
3. `native_ui.cpp` 不再有 `tick_ =`、`playing_ =`、`reverse_ =`、`speed_ =` 的 transport 写入；`setPlaybackState` 只拷贝 view。
4. `defaultKeyBindings()` 注册后 `conflicts().empty()`；`playback.*` 与 `camera.*`、HUD 的 chord 无交集。
5. `UiControlId` 新值都在 `Count` 之前追加；旧 rect 表 hash 不变（mutation：在中间插入 -> hash 红）。
6. `UiSnapshot` 新字段都有默认值；旧 `native_ui_probe` 不改也能编译。
7. `playback_probe` 的 `checks>0 && failures==0`；`checks==0` 视为 INVALID。

---

## 4. T72 设计：复盘书签与注释

### 4.1 数据结构

新增 `native/include/review_bookmarks.h` / `native/src/review_bookmarks.cpp`。它不依赖 Win32；只依赖 `camera_director.h` 的 `CameraPose` 和 `workspace_types.h` 的原始记录。

```cpp
struct ReviewEventRef {
  bool present = false;
  std::string name;              // T68 定义名；manual 为空
  std::uint32_t eventId = 0;
  std::int32_t tick = -1;        // 事件真实 tick
  std::uint64_t packetOrdinal = 0;
  std::uint32_t messageBit = 0;
};

struct ReviewPlayerRef {
  std::string role;              // T64 actors[].role；不猜
  std::int32_t entindex = -1;
  std::int32_t userid = -1;
  std::string name;              // 只有 T64 提供 name 时才非空
  std::string nameReason;        // no-userinfo | not-an-actor | ...
  bool valid = false;
};

struct ReviewCameraRef {
  bool valid = false;
  CameraPose pose;
  float fov = 90.0f;
  std::string mode;              // first-person | third-person | free
  std::int32_t cameraTick = -1;  // 拍摄时的真实 cursor tick
  std::int32_t bookmarkSlot = 0; // 0 = 非相机书签；1..8 = 来源槽位（仅溯源）
  std::string cameraReason;      // no-renderer | captured | ...；valid=false 时必填
};

struct ReviewBookmark {
  std::uint64_t id = 0;          // 稳定唯一；跨 reopen 不变
  std::int32_t tick = -1;        // 真实 cursor tick，不是 requested tick
  std::string title;             // 必填，UTF-8
  std::string note;              // 可空，保留换行
  ReviewEventRef event;
  std::vector<ReviewPlayerRef> players;
  std::string identityReason;    // no-player-context | no-userinfo | ...
  ReviewCameraRef camera;
  std::int64_t createdAtUtc = 0;
  std::int64_t updatedAtUtc = 0;
  std::string demoHash;          // 创建时 demo 内容 hash，供迁移校验
  std::uint32_t recordVersion = 1;
};
```

**字段来源纪律：** `role/entindex/userid` 只能来自 T64 `actors[]`；`name` 只有 T64 真的给出时才写，否则为空并写 `nameReason=no-userinfo`。**禁止**写 `steamId`、`team`、`class`、`avatar`、默认 `Player N` 等 T64 未证明的字段。手动书签没有玩家上下文时 `players=[]`、`identityReason=no-player-context`，不填默认玩家。

`eventKey()` 是 `(event.tick, event.packetOrdinal, event.messageBit)` 的字符串 `tick:packet:bit`；这是 T68 的去重/导航键，**不是 tick 单键**。同一 tick 的 209/213 个事件靠它区分。

### 4.2 Store 与查询

```cpp
enum class ReviewLoadState { NotLoaded, Loaded, Missing, Corrupt, HashMismatch, OldSchema };

enum class ReviewAddStatus {
  Added, AlreadyExists, RefusedEmptyTitle, RefusedSeekPending, RefusedNoDemo,
  RefusedNotLoaded, RefusedUnknownEvent, RefusedHashMismatch, RefusedInvalidTick
};

struct ReviewAddResult {
  ReviewAddStatus status = ReviewAddStatus::RefusedInvalidTick;
  std::uint64_t id = 0;          // Added/AlreadyExists 时为稳定 id，否则 0
  std::string reason;            // 具名原因，如 seek-pending / unknown-event
};

enum class ReviewJumpStatus {
  Applied, RefusedNotFound, RefusedHashMismatch, RefusedOutOfRange,
  RefusedCamera, RefusedPlayback, RefusedNoDemo, RefusedNotLoaded
};

struct ReviewJumpResult {
  ReviewJumpStatus status = ReviewJumpStatus::RefusedNotFound;
  std::uint64_t id = 0;
  std::int32_t tick = -1;
  bool cameraApplied = false;
  std::string reason;
};

struct ReviewQuery {
  std::string text;          // title/note/event.name/player.name/userid 的子串
  std::string eventName;     // 空 = 全部；"manual" = 手动
  std::string player;        // userid:<n> / entindex:<n> / name 子串
  std::int32_t tickMin = -1;
  std::int32_t tickMax = -1;
};

enum class ReviewSort { Tick, Created, Title };

class ReviewBookmarkStore {
 public:
  ReviewAddResult add(const ReviewBookmark& candidate);
  ReviewAddResult addFromEvent(const DemoEventSample& event,
                               const std::vector<DemoEventActor>& actors,
                               const ReviewCameraRef& camera);
  bool update(std::uint64_t id, const std::string& title, const std::string& note,
              std::string& reason);
  bool remove(std::uint64_t id, std::string& reason);
  void clear();
  const ReviewBookmark* at(std::uint64_t id) const;
  std::vector<ReviewBookmark> query(const ReviewQuery& query,
                                    ReviewSort sort, bool descending) const;
  std::size_t count() const;
  ReviewLoadState load(const WorkspaceDocument& workspace, std::string& reason);
  bool save(WorkspaceDocument& workspace, std::string& reason) const;
  bool migrate(const WorkspaceDocument& from, WorkspaceDocument& to, std::string& reason);
  // counters: add/update/delete/refused/loadFailures/saveFailures/jumpFailures/duplicateEvent
};

`jump()` 不在 Store 内实现：它需要 `PlaybackControl` 和 renderer，放在 `main.cpp` 的 `ReviewController`，返回上面的 `ReviewJumpResult`；Store 只提供 `at(id)` 和 `demoHash` 校验所需数据。
```

**排序：** 默认 `Tick` 升序；同一 tick 依次按 `event.packetOrdinal`、`event.messageBit`、`id`，保证 same-tick 多事件顺序稳定且可区分。`Created` 按 `createdAtUtc`，`Title` 按解码后的 UTF-8 字节序（不做 locale 排序，避免机器差异）；两者同值时再按 `id` 升序，保证排序全序、可逐行断言。

**搜索：** 对 title/note/event.name/player.name/userid 做 ASCII 大小写不敏感子串；UTF-8 非 ASCII 按字节精确匹配（不做 Unicode case fold，文档写明限制）。空查询返回全部。

**过滤：** `eventName` 精确匹配（manual 用 `manual`）；`player` 支持 `userid:<n>`、`entindex:<n>`、name 子串；tick 范围闭区间。多个条件 AND。

### 4.3 添加、编辑、删除、跳转

#### 4.3.1 添加

- `add` 的 `tick` 必须由调用方传入 **`g_playback.tick`（当前 cursor）**，不是 slider 的 requested 值，也不是 `lastFrame.frameTick`。`main.cpp` 在 UI 按钮/快捷键处理时读 `PlaybackControl::view().cursorTick`；如果 `phase==SeekPending`，先 `settle`，否则拒绝 `seek-pending`（具名），不把 requested 当 actual；`phase==Disabled` 拒绝 `no-demo`，不创建“tick=-1”的书签。**workspace 未加载 -> `RefusedNotLoaded`，不静默创建空 workspace；`loadState==HashMismatch` -> `RefusedHashMismatch`，必须先显式 `migrate` 才能 add。**
- `title` 非空；空 -> `RefusedEmptyTitle`，不自动生成默认标题。`note` 可空。
- `id = workspace.nextBookmarkId++`；`createdAtUtc=updatedAtUtc=now`；`demoHash=current`；`recordVersion=1`。
- 手动添加：`event.present=false`、`players=[]`、`identityReason=no-player-context`、`camera` 捕获当前 `g_cameraDirector.pose()` + renderer fov + `cameraModeToken(g_cameraMode.mode())` + `tick`；没有 renderer 时 `camera.valid=false`，`cameraReason=no-renderer`，仍可保存书签（跳转时只跳 tick）。
- 从事件添加：`event` 填 T64/T68 的 `(tick, packetOrdinal, messageBit, eventId, name)`；`players` 从 `actors[]` 拷贝，只填 T64 字段；`camera` 捕获当前相机；`tick=event.tick`（事件真实 tick），不是当前 cursor。若事件不在 T64 索引 -> `RefusedUnknownEvent`，不创建。
- 完全相同的 `eventKey` 已存在 -> `AlreadyExists`，返回已有 id；**同一 tick 不同 eventKey 允许并存**。这是“同一 tick 多事件可区分”的正面路径，也是“重复 tick”负控的边界。**只对 `event.present==true` 做 eventKey 去重；手动书签 `eventKey=none`，同一 tick 的多条手动注释必须允许**（否则“同一 tick 多注释”会被误判重复）。

#### 4.3.2 编辑

只允许改 `title` 和 `note`；`tick/event/players/camera/demoHash` 不可变。修改 `tick` 的请求返回 `RefusedImmutableField`，不静默改。`updatedAtUtc` 更新，`createdAtUtc` 不变。编辑空标题 -> `RefusedEmptyTitle`，旧值保留。

#### 4.3.3 删除

按 id 删除；不存在 -> `NotFound`，不改变列表；删除最后一个后 `count=0`，但 `loadState` 仍是 `Loaded`（不是 `NotLoaded`）。删除只改内存并置 `dirty=1`；T71 的保存/自动保存负责落盘。

#### 4.3.4 跳转（事务）

```
jump(id):
  if !loaded -> RefusedNotLoaded
  r = find(id); if !r -> RefusedNotFound
  if r.demoHash != currentHash -> RefusedHashMismatch (先提示迁移)
  if r.tick < 0 || r.tick > endTick -> RefusedOutOfRange, cursor/camera unchanged
  beforeCursor = view().cursorTick; beforePose = camera.pose(); beforeFov = renderer.fov()
  if r.camera.valid:
     if !applyCamera(r.camera) -> RefusedCamera, restore camera, cursor unchanged
  result = playback.request({Scrub, tick=r.tick, strict=true})
  if result.status is Refused*:
     restore camera to beforePose/beforeFov; return RefusedPlayback(reason), cursor unchanged
  return Applied(tick=r.tick, paused=1, camera=applied|none, frameStatus=pending)
```

**关键：** 相机先应用，播放请求同步拒绝时回滚相机；播放请求一旦 `Applied`，跳转即成功，后续 `landing` 即使 `frameStatus=gap/before-window` 也只是“无帧”，不是“跳转失败”。失败路径必须保持 cursor 和相机都不动；gate 用 `expect-same-tick` + `expect-camera` 双断言。

### 4.4 T71 工作区格式

T71 尚未实现；以下是 T72 对 T71 的**最小契约**。T71 可以用别的序列化（JSON/二进制），但字段集合和语义必须一致；T72 只通过 `WorkspaceDocument` 读写，不直接碰文件。

#### 4.4.1 共享类型（`native/include/workspace_types.h`，T71 拥有）

```cpp
struct WorkspacePlayerRef {
  std::string role; std::int32_t entindex=-1; std::int32_t userid=-1;
  std::string name; std::string nameReason;
};
struct WorkspaceCameraRef {
  bool valid=false; CameraPose pose; float fov=90.0f; std::string mode;
  std::int32_t cameraTick=-1; std::int32_t bookmarkSlot=0;
  std::string cameraReason;  // no-renderer | captured | ...
};
struct WorkspaceBookmarkRecord {
  std::uint64_t id=0; std::int32_t tick=-1; std::string title,note;
  bool eventPresent=false; std::string eventName; std::uint32_t eventId=0;
  std::int32_t eventTick=-1; std::uint64_t eventPacketOrdinal=0; std::uint32_t eventMessageBit=0;
  std::vector<WorkspacePlayerRef> players; std::string identityReason;
  WorkspaceCameraRef camera; std::int64_t createdAtUtc=0, updatedAtUtc=0;
  std::string demoHash; std::uint32_t recordVersion=1;
};
struct WorkspaceDocument {
  std::uint32_t schemaVersion=1;
  std::string demoHashAlgo;   // sha256
  std::string demoHash;       // lowercase hex
  std::string demoPathHint;   // 仅提示，不是身份
  std::uint64_t nextBookmarkId=1;
  std::vector<WorkspaceBookmarkRecord> bookmarks;
  // T71 其它：camera/hud/minimap/eventFilter/logsTf/playback
};
```

T71 的 `WorkspaceStore` 必须提供：`open(demoPath, actualHash)` 返回 `Opened/Missing/Corrupt/HashMismatch/OldSchema`；`save(document)` 原子写（temp + replace，失败不覆盖旧文件）；`migrate(document, actualHash)` 显式迁移。T72 不调用 `writeIniSection` 直接写 live 文件。

**hash 权威：** `[workspace] demoHash` 是权威；每条 `WorkspaceBookmarkRecord.demoHash` 必须等于 header，否则整个 workspace `Corrupt`（reason 指到记录 id）。`migrate` 同时重写 header 和每条记录；只改 header 不改记录 = Corrupt，不能静默通过。

#### 4.4.2 建议的 INI 序列化（T71 最终决定）

仓库没有 JSON 库，`ini_section.h` 已提供 `readIniSection/writeIniSection`，因此建议 T71 用 UTF-8 INI：

```
[workspace]
schema=1
demoHashAlgo=sha256
demoHash=<64 hex>
demoPathHint=<percent-encoded absolute path>
updatedUtc=<seconds>
nextBookmarkId=<n>

[review.bookmarks]
count=<n>

[review.bookmark.<id>]
id=<n>
tick=<n>
title=<percent-encoded UTF-8>
note=<percent-encoded UTF-8>
eventPresent=0|1
eventName=<percent-encoded>
eventId=<n>
eventTick=<n>
eventPacketOrdinal=<n>
eventMessageBit=<n>
playerCount=<n>
player.0.role=<percent-encoded>
player.0.entindex=<n>
player.0.userid=<n>
player.0.name=<percent-encoded>
player.0.nameReason=<percent-encoded>
identityReason=<percent-encoded>
cameraValid=0|1
cameraFocusX=<float>
cameraFocusY=<float>
cameraFocusZ=<float>
cameraYaw=<float>
cameraPitch=<float>
cameraDistance=<float>
cameraFov=<float>
cameraMode=<percent-encoded>
cameraTick=<n>
cameraSlot=<n>
cameraReason=<percent-encoded>
createdUtc=<seconds>
updatedUtc=<seconds>
demoHash=<64 hex>
recordVersion=1
```

- 字符串 percent-encode：`%` -> `%25`，除 `[A-Za-z0-9._~-]` 外的字节 -> `%XX` 大写；空串写空值。`readIniSection` 不 trim、值可含 `=`，所以 `note` 中的 `=` 安全；换行/`[`/`]` 必须编码。
- 解析时缺 key、`playerCount` 与实际 `player.N.*` 不一致、percent 解码失败、UTF-8 非法、`tick` 越界、`id` 重复 -> 整个 workspace `Corrupt`，`loaded=0`，**不覆盖旧文件、不静默丢弃**。
- 文件路径由 T71 决定；建议 `%LOCALAPPDATA%/TF2 Demo Player/workspaces/<algo>-<hash>.ini`。`demoPathHint` 只用于显示“上次路径”，移动 demo 后仍按 hash 找到同一 workspace。
- `settings.ini` 仍只放 `[renderer]/[camera]/[keys]`；T72 书签**不写入 settings.ini**。

### 4.5 同一 tick 多事件与身份显示

- 列表行必须显示 `id`、`tick`、`eventName`、`eventKey`（或 `packet:bit` 短码）、`players`。同一 tick 的两行不能合并；排序按 `(tick, packetOrdinal, messageBit, id)`。
- 事件书签的 `eventKey` 与 T68 的 `(tick,packetOrdinal,messageBit)` 完全一致；跳转后用 `eventKey` 再查 T64/T68 索引，必须命中同一条事件，而不是“该 tick 的第一条”。
- 玩家显示：`name` 非空 -> `name (userid=<n>, entindex=<n>)`；`name` 空 -> `userid=<n>, entindex=<n>, name unavailable (no-userinfo)`。**不显示 `Player <entindex>` 这种伪造名。** 手动书签无玩家 -> `no player context`。

### 4.6 UI 与交互

`UiControlId` 在 T70 新控件之后、`Count` 之前追加（T70 与 T72 都不得移动旧 rect）：`ReviewBookmarkAdd`、`ReviewBookmarkEdit`、`ReviewBookmarkDelete`、`ReviewBookmarkJump`、`ReviewBookmarkSearch`、`ReviewBookmarkFilter`、`ReviewBookmarkList`、`ReviewBookmarkStatus`。

- 列表：按 tick 排序；搜索框/过滤下拉有焦点时，全局快捷键（播放、相机、事件）全部被 `shouldDispatchHotkey` 抑制；`Escape` 只关面板/清搜索，不跳转。
- 添加按钮在 `SeekPending` 时禁用并显示 `seek-pending`；在 `Disabled` 时显示 `no-demo`；workspace 未加载时显示 `not-loaded`；不把禁用当“成功添加 0 条”。
- 空状态必须区分：`workspace missing`、`workspace loaded, 0 bookmarks`、`workspace corrupt: <reason>`。三者不能都显示空白列表。
- 删除最后一个后仍显示 `loaded, 0`，不是 `missing`。

#### 4.6.1 T72 trace 行（验收的唯一读数）

自由文本字段（title/note/name/eventName/reason）一律 percent-encode 后写入，避免空格/制表符破坏解析；数值/布尔用十进制。所有行一行一条，`bookmarks: ` 前缀。

```
bookmarks: add id=%llu tick=%d event=%s players=%zu identityReason=%s camera=%s status=%s
bookmarks: add-event id=%llu name=%s eventId=%u eventTick=%d packet=%llu bit=%u players=%zu status=%s
bookmarks: update id=%llu changed=%s tickUnchanged=%d
bookmarks: delete id=%llu removed=%d count=%zu reason=%s
bookmarks: query text=%s matches=%zu ids=%s
bookmarks: filter event=%s player=%s tickMin=%d tickMax=%d matches=%zu ids=%s
bookmarks: list count=%zu order=%s ids=%s
bookmarks: list-item id=%llu tick=%d title=%s eventKey=%s players=%s nameReason=%s camera=%s createdUtc=%lld updatedUtc=%lld
bookmarks: jump id=%llu ok=%d reason=%s tick=%d paused=%d camera=%s cursor=%d
bookmarks: load state=%s loaded=%zu nextId=%llu storedHash=%s actualHash=%s autoApply=%d reason=%s
bookmarks: save ok=%d count=%zu nextId=%llu reason=%s
bookmarks: migrate from=%s to=%s migrated=%zu ok=%d reason=%s
bookmarks: verify id=%llu tick=%d title=%s note=%s eventKey=%s players=%s camera=%s demoHash=%s createdUtc=%lld updatedUtc=%lld match=%d
bookmarks: trace id=%llu tick=%d eventKey=%s userids=%s entindexes=%s nameReason=%s
bookmarks: expect what=%s expected=%s actual=%s match=%d
bookmarks: summary add=%zu update=%zu delete=%zu refused=%zu loadFailures=%zu saveFailures=%zu jumpFailures=%zu duplicateEvent=%zu
```

- `eventKey` 固定 `<tick>:<packetOrdinal>:<messageBit>`；manual 为 `none`。`players` 是分号分隔的 `role,entindex,userid,name`（name 为空写空），无玩家写 `none`。
- `list` 每行一条 `bookmarks: list-item`，顺序与 `ids` 一致；空列表只打 `list count=0`，没有 `list-item`（空状态必须可区分）。`list-item.nameReason` 是分号分隔的每个玩家原因，无玩家写 `none`。
- `add/add-event/verify` 的 `camera` 是 `valid,pose(6 floats),fov,mode,cameraTick` 的逗号串；无相机写 `none`。`jump` 的 `camera` 是 `applied|none|refused`（是否成功应用相机），不要与相机引用混淆。
- `add/add-event.status` 用 `added|already-exists|refused-empty-title|refused-seek-pending|refused-no-demo|refused-not-loaded|refused-unknown-event|refused-hash-mismatch|refused-invalid-tick`；`already-exists` 必须带已有 id，不能静默覆盖。
- `bookmarks: expect` 用于 `expect-cursor`、`expect-phase`、`expect-camera`、`expect-same-tick`、`expect-bookmark-count`、`expect-list`、`expect-load-state`、`expect-add-status`、`expect-jump`；`what` 是命令名，`expected/actual` 是 percent-encode 后的值。缺 `bookmarks: expect` 行 = 该断言未执行 = INVALID。

#### 4.6.2 `--review-script`（产品集成驱动）

仿 `--camera-script`，测试专用；每步打 `bookmarks: accept step=%zu cmd=%s result=%s detail=%s`，失败 `stepsFailed++`，`quit` 退出码 `stepsFailed==0?0:1`（退出码 0 只表示脚本跑完，不表示验收通过）。

```
add <title> [note]              # 使用当前 cursor tick，不接受调用方 tick
add-at <tick> <title> [note]     # 测试用：显式 tick（仍走 add 的范围校验）
add-event <tick> <packetOrdinal> <messageBit> [title]
inject-bookmark <tick> <title>  # 仅负控：绕过范围校验注入记录，产品路径不可达
fail-next-save                  # 仅负控：下一次 save 注入失败
update <id> <title> [note]
delete <id>
query <text>
filter event=<name>|player=<userid:n|entindex:n|text>|tick=<a>..<b>
list [order=tick|created|title] [desc]
jump <id>
verify <id>
load <workspacePath>
save
migrate
mark-camera <label>
expect-camera <label>
expect-cursor <n>
expect-same-tick
expect-phase <name>
expect-bookmark-count <n>
expect-list <id,id,...>
expect-load-state <state>
expect-add-status <status>
expect-jump <ok|fail> <reason>
focus on|off
key <vk> [ctrl] [alt] [shift]
settle <maxFrames>
summary
quit
```

- `--review-workspace <path>` 是 T71 workspace 的测试 override（生产路径仍由 T71 按 demo hash 决定）；`--review-script` 必须先 `load` 再操作，未 `load` 的 add 返回 `refused-not-loaded`，不得静默创建空 workspace。
- `mark-camera <label>` 保存当前 `pose/fov`；`expect-camera <label>` 用 `formatPose` 精确比较并打 `bookmarks: expect what=camera ...`；没有 mark 就 expect = INVALID。
- `add-event` 的 `(tick,packetOrdinal,messageBit)` 必须查 T64/T68 索引；查不到 -> `refused-unknown-event`，不创建。
- `settle` 等 `PlaybackControl::pending==false`，超时 `landing-timeout` INVALID；T72 的 jump 断言必须 settle 后再 `expect-cursor`。

### 4.7 T72 验收：读数与 mutation

**事件 fixture：** T68 golden TSV 只有 `tick/name/fields`，没有 `packetOrdinal/messageBit`；A2/A3/A7 需要的 `eventKey` 必须从 T64 oracle 的 event 记录取。gate 读取 `evidence/t70-t72/fixtures/t72-event-map.tsv`（列：`eventName\ttick\tpacketOrdinal\tmessageBit\teventId\tplayers`），由 T68 golden 与 T64 oracle 按 `(tick,name)` + 字段元组连接生成；连接不唯一/缺行 = INVALID，**禁止取该 tick 的第一条**。fixture 行必须带 oracle 的 `eventKey`，A2/A3 只读 fixture，不现场猜。

**字段级断言：** 表中 trace 行只列必须断言的字段；实现可以多打字段，但列出的字段必须存在且精确匹配；缺字段 = INVALID，不是 PASS。

| ID | 验收点 | 必须断言的读数 | 通过条件 | 能让它变红的 mutation |
| --- | --- | --- | --- | --- |
| T72-A1 | 在当前 actual tick 添加手动书签 | `bookmarks: add id=1 tick=<cursor> event=none players=0 identityReason=no-player-context camera=<valid,pose,fov,mode,cameraTick> status=added`；`bookmarks: list count=1 order=tick`；`bookmarks: list-item id=1 tick=<cursor>` | 存的是 cursor，不是 slider requested；`event.present=false`；`players=[]` | 移除 `seek-pending` 拒绝并改存 `requestedTick`：在 `scrub 3000` 未 settle 时添加，存 3000 而 cursor 仍 2000，A1 红；或存 `lastFrame.frameTick`（<cursor），tick 断言红 |
| T72-A2 | 从 T64/T68 事件添加，返回同一事件/tick | 从 `t72-event-map.tsv` 的 `player_death` 行选一条（tick/packet/bit 全取 fixture，**不硬编码 334**）：`bookmarks: add-event id=1 name=player_death eventId=<fixtureEventId> eventTick=<fixtureTick> packet=<fixturePacket> bit=<fixtureBit> players=<n> status=added`；`bookmarks: jump id=1 ok=1 tick=<fixtureTick>`；`bookmarks: verify id=1 eventKey=<fixtureTick:fixturePacket:fixtureBit> match=1` | `eventKey` 与 T64 oracle 完全一致；跳转 tick=eventTick；不是“tick 的第一条” | 只用 tick 查事件：若该 tick 有多条，选到第一条（可能是 `ammo_pickup`），A2 的 `name=player_death`/`eventKey` 红；或 fixture 连接取第一条而非唯一匹配，`eventKey` 红 |
| T72-A3 | 同一 tick 多事件可区分 | 在 `t72-event-map.tsv` 已证实的 tick 334 添加 `ammo_pickup` 与 `teamplay_round_start` 两条（均取 fixture 的 packet/bit）：`list count=2`，两行 `id` 不同、`eventKey` 不同、`packet/bit` 不同；分别 `jump` 后 `verify eventKey` 各自匹配 | 两行都在；排序稳定；jump 命中各自事件；`eventKey` 精确到 packet/bit | 以 tick 为 map key：第二条覆盖第一条，`count=1`，A3 红；或 jump 总选第一条，`verify` 红 |
| T72-A4 | 编辑标题/正文 | `bookmarks: update id=1 changed=title,note tickUnchanged=1`；`bookmarks: verify id=1 tick=<unchanged> title=<new> note=<new> eventKey=<unchanged> players=<unchanged> camera=<unchanged> createdUtc=<old> updatedUtc=<new> match=1` | 只有 title/note 变；tick/event/player/camera 不变 | 编辑时改 tick：`tickUnchanged=0` 或 `RefusedImmutableField` 缺失，A4 红；或 update no-op，旧标题仍在，红 |
| T72-A5 | 删除 | `bookmarks: delete id=1 removed=1 count=0`；再 `delete id=1 removed=0 reason=not-found`；`list count=0` | 第一条删除；重复删除具名失败；列表 0 | delete no-op：`count=1`，A5 红；或删除后 `loadState=not-loaded`（把空当未加载），红 |
| T72-A6 | 搜索 | fixture 3 条：title 命中 1、note 命中 1、event 命中 1；`query text=<s> matches=<n> ids=<...>` | 三个来源都能命中；空查询返回 3；大小写 ASCII 不敏感 | 只搜 title：note/event 的命中丢失，`matches` 少于期望，A6 红 |
| T72-A7 | 过滤 | `filter event=player_death` -> 2 条；`filter player=userid:2` -> 1 条；`filter tick=300..400` -> 1 条；组合 AND | 每个过滤的 ids 精确；未知 event -> 0 且 `state=loaded` | 过滤用 title 子串代替 eventName：`player_death` 命中标题含该词的 manual 书签，ids 多一条，A7 红 |
| T72-A8 | 按时间排序 | fixture 创建顺序与 tick 顺序相反：`list order=tick` -> ids 按 tick 升序；`order=created` -> 按 created 升序；`order=title` -> 字节序 | 三种排序的 id 序列都精确；同 tick 用 `(packet,bit,id)` 稳定 | 永远按 id 排序：`order=tick` 的 id 序列与期望不同，A8 红 |
| T72-A9 | 跳转到书签 | `bookmarks: jump id=1 ok=1 tick=<record.tick> paused=1 camera=applied`；`settle` 后 `expect-cursor <record.tick>`；`expect-phase paused` | cursor=record.tick；暂停；相机 applied | 跳转不暂停：1 秒后 cursor 前进，`expect-cursor` 红；或相机应用失败仍返回 ok=1，`camera=applied` 断言红 |
| T72-A10 | 失败跳转保持当前位置 | `mark-camera before`；`inject-bookmark <endTick+1> ...`（仅负控）；`jump <id>` -> `bookmarks: jump ok=0 reason=tick-out-of-range cursor=<before>`；`expect-same-tick`；`expect-camera before` | cursor 和相机都不动；reason 具名 | 失败时 clamp 到 endTick：cursor 变了，`expect-same-tick` 红；或相机已应用不回滚，`expect-camera before` 红 |
| T72-A11 | 关闭/重开后逐项还原 | 添加 3 条（不同 tick/事件/身份/相机）-> `save ok=1 count=3 nextId=<n+3>` -> 关闭 demo -> 重开同 hash -> `load state=loaded loaded=3 nextId=<n+3>`；逐项 `verify id/tick/title/note/eventKey/players/camera/createdUtc` | 3 条逐字段一致；`nextId` 不回退；`createdUtc` 不变 | 跳过 save：重开 `count=0`，A11 红；只写 count 不写记录：`count=3` 但 `verify` 缺字段，红 |
| T72-A12 | demo hash 不匹配不自动应用，可显式迁移 | 用 demo A 的 workspace 打开 demo B：`load state=hash-mismatch autoApply=0 storedHash=<A> actualHash=<B>`；不迁移时 `list count=0`；`migrate` -> `migrated=3 ok=1`，每条 `verify demoHash=<B>`；旧文件保留 | 默认不应用；显式迁移成功且每条记录 hash=B；旧文件保留 | 加载时静默改 hash：`state=loaded` 且 B 上显示 A 的记录，A12 红；或 migrate 只改 header，`verify demoHash=<A>`，红 |
| T72-A13 | 损坏 note 被具名拒绝，不静默丢 | fixture `note=%ZZ` / 非法 UTF-8 / 超长：`load state=corrupt loaded=0 reason=review.bookmark.1:invalid-percent`；`list` 不显示半条；`save` 被拒绝且旧文件字节不变 | `state=corrupt`、`loaded=0`、reason 指到具体记录/字段；旧文件 sha256 不变 | 解析时跳过坏记录并 `state=loaded count=2`：A13 红；或覆盖旧文件，sha256 变，红 |
| T72-A14 | 无玩家身份不伪造 | 手动：`bookmarks: add ... players=0 identityReason=no-player-context`；事件 actor 无 name：`bookmarks: verify id=<id> players=<role>,5,2,`（`role,entindex,userid,name`，name 为空）；`bookmarks: trace id=<id> userids=2 entindexes=5 nameReason=no-userinfo` | 没有默认名；没有 `Player 5`；`nameReason` 保留；`userid/entindex` 可追溯 | 默认填 `Player 5`：A14 的 `name=""`/`no-userinfo` 断言红；或省略 userid/entindex，traceability 红 |
| T72-A15 | 无书签是真实空状态，不是未执行 | 先 `load` 一个已保存的空 workspace：`load state=loaded loaded=0 nextId=1`；再 `save ok=1 count=0 nextId=1`；重开 `load state=loaded loaded=0 nextId=1`；UI `workspace loaded, 0 bookmarks` | `state=loaded` 且 `loaded=0`；不是 `not-loaded`/`missing`；`save` 不因 0 条而拒绝 | 不调用 load：`state=not-loaded`，A15 红；或把 missing 当 loaded 0，状态断言红；或 `save` 在 0 条时返回 `ok=0`，A15 红 |
| T72-A16 | 写入失败可见，不覆盖旧文件 | `fail-next-save`（或只读文件）后 `save`：`bookmarks: save ok=0 count=3 reason=write-failed`；`list` 内存仍 3 条；重开旧文件仍 3 条且 sha256 不变 | `ok=0`；旧文件字节不变；内存不被清空 | 失败仍返回 `ok=1`：A16 红；或失败时先 truncate 旧文件，sha256 变，红 |
| T72-A17 | 删除最后一条后保存/重开 | `delete` 最后一条 -> `save ok=1 count=0 nextId=<n>` -> 重开 `load state=loaded loaded=0 nextId=<n>` | 空列表持久化；`nextId` 不回退 | 删除只改内存不写盘：重开 `count=1`，A17 红 |
| T72-A18 | tick 与可选身份可追溯 | 每条列表行：`bookmarks: list-item id=<id> tick=<n> eventKey=<...> players=<...> nameReason=<...>`；每条详情：`bookmarks: trace id=<id> tick=<n> eventKey=<...> userids=<...> entindexes=<...> nameReason=<...>` | 每个书签都能回答“哪个 tick、哪个事件、哪个玩家（或为什么没有）”；字段不缺失 | 列表只显示标题：`list-item` 无 `tick`/`players`，A18 红（缺字段 = INVALID）；或 trace 省略 `nameReason`，红 |

### 4.8 T72 负控

| 负控 | 注入 | 预期红行 | 说明 |
| --- | --- | --- | --- |
| 无书签 | 空 workspace + 不调用 load | A15 | `not-loaded` 不能冒充 `loaded,0` |
| 重复 tick | 以 tick 为 map key | A3 | 同 tick 不同事件被合并，count/eventKey 红 |
| 无身份 | 默认 `Player <entindex>` | A14/A18 | 伪造名/缺 `nameReason` 红 |
| 损坏 note | 跳过坏记录继续 load | A13 | `state=loaded` 但记录丢失，count/字段红；且旧文件可能被覆盖 |
| 不写入 | save no-op 但返回 ok | A11/A16/A17 | 重开旧状态，count/字节红 |
| 跳转失败仍移动 | 越界 clamp | A10 | `expect-same-tick` 红 |
| 相机失败不回滚 | 先跳 playback 再 apply camera | A9/A10 | 相机/位置至少一个动，红 |
| hash 静默迁移 | 加载时改写 demoHash | A12 | `state=loaded` 而非 `hash-mismatch`，红 |
| 事件只按 tick | `addFromEvent` 取 tick 第一条 | A2/A3 | eventName/eventKey 错，红 |

---

## 5. 依赖矩阵与 NOT VERIFIED

### 5.1 依赖

| 任务 | 依赖 | 当前状态 | 对设计的影响 |
| --- | --- | --- | --- |
| T70 | T64 tick/time 换算已测量 | 部分：T64 DATA-CONTRACT 有 `displaySeconds=(tick-firstPacketTick)/tickRate`、`tickRate`；`demoTickRate` 用 `ticks/playbackTime` | T70 的 `rate` 和 `appliedSeconds` 以 `demoTickRate` 为准；`durationSeconds` 仍用 `header.playbackTime`，两者分开 |
| T70 | T68 事件跳转 + 基础 seek 已验证 | **未满足**：T68 只有静态 spec，无 `DemoGameEvent` 产品代码；当前“seek”只是 `g_playback.tick=` 赋值，无落地确认 | T70 的 `PlaybackControl` 是新的唯一接口；T68 事件跳转验收不能替代 T70 seek 验收 |
| T70 | T66/T67 键盘优先级/冲突规则 | **未满足**：T66/T67 未落地；`shouldDispatchHotkey` 当前对所有键返回 false（焦点时），与 main.cpp 注释矛盾 | T70 的键盘表必须调用最终 `shouldDispatchHotkey`；T70-A13 在 T67 落地前 NOT VERIFIED |
| T70 | T71 播放位置/速度持久化 | T71 未实现 | T71 需要 T70 `PlaybackView` 的 `cursor/paused/reverse/speed/phase`；T70 不依赖 T71 |
| T72 | T64 事件索引（含 actors） | T64 DATA-CONTRACT 有 event `actors[]`，但产品事件解码未实现 | T72 的事件书签/玩家身份只能接 T64/T68 的真实 actors；不能自造 |
| T72 | T70 seek/actual tick 读数 | **未满足**：T70 只有设计 | T72 的 `tick` 必须是 `PlaybackControl::view().cursorTick`；`SeekPending` 时拒绝添加 |
| T72 | T71 工作区格式 | **未满足**：无 `WorkspaceStore`、无 workspace 文件、无 hash 绑定 | 4.4 是提案；T71 定稿前 T72 reopen/hash/migrate 验收 NOT VERIFIED |
| T72 | T68 事件解码（`DemoEventSample`） | **未满足**：T68 spec 的 `DemoEventSample` 没有 `actors` 字段；T64 contract 有 actors | T72 需要 T68 样本携带 actors 或 T64 side table；否则只能存 `eventKey`，玩家身份为空并标 `no-userinfo` |
| T72 | T67 焦点/列表键盘规则 | **未满足**：T67 未落地 | 搜索框焦点时全局快捷键必须被抑制；T72 的键盘验收依赖 T67 |

### 5.2 NOT VERIFIED（按 NEXT-TASKS §1，未执行/无读数的一律列出）

1. **T70 全部运行时验收未执行。** 本文是静态设计；没有 build、没有启动、没有 lock、没有真实 demo 运行；T70-A1..A16 全部 NOT VERIFIED。
2. **没有 actual frame 读数。** 产品当前只显示 `anchorAbs`、`entitySnapshotStaleness`，没有 `frameTick/frameDelta/frameStatus`；T70 设计字段未实现。
3. **`frameDelta == entitySnapshotStaleness` 只是静态恒等式。** 未在真实 demo 上对账；`firstPacketTick` 双 origin（main.cpp:5040-5063）仍是风险点。
4. **T64 bagel exact=1/512、medianGap=822、maxGap=1180** 是 T64 的读数；T70 逐帧的“actual tick 增量与 demo 数据一致”尚未用这些 fixture 跑过。
5. **1 秒 = 67 tick 未在产品中验证。** 当前 `kSecondStepTicks=66`；`playbackTicksForSeconds` 是设计，未编译。
6. **5 秒 = 333 tick、速度表 8/16/33/66/133/266/533 未跑纯探针。**
7. **T68 事件解码未实现。** 仓库无 `DemoGameEvent` 符号；T72 的 `addFromEvent`、same-tick 209/213、eventKey 均无法在产品中验收。
8. **T71 workspace 未实现。** 4.4 的 INI schema 是提案，不是 T71 已接受格式；T72 的 save/reopen/hash/migrate/corrupt 全部 NOT VERIFIED。
9. **玩家姓名不可得。** 产品没有 userinfo/string-table 玩家名解码（T64 G5）；只能存 `userid/entindex/role`，`name` 为空 + `no-userinfo`。任何“真实姓名”验收都不能通过。
10. **T68 `DemoEventSample` 无 actors。** T72 事件书签的玩家身份需要 T68 扩展或 T64 side table；当前 spec 不足。
11. **同一 tick 多事件的真实 UI 未验证。** T68 SAME-TICK 有 209/213 的 fixture 读数，但产品没有事件列表；T72 只能设计，不能跑。
12. **INI percent-encoding / UTF-8 校验未实现。** `ini_section.h` 只保证 `key=value` 与 section 保留；编码器/解码器是 T72 新代码，未编译。
13. **`writeIniSection` 的原子性不等于 T71 workspace 原子性。** T71 必须自己 temp+replace；T72 不直接写 live 文件。该契约未实现。
14. **UI 控件/布局未实现。** T70/T72 的 `UiControlId` 追加、rect 稳定性、空状态文案都未编译/未截图。
15. **焦点测试只有合成 `focus on|off`。** 没有真实 EDIT/RichEdit 焦点下的 T70/T72 键盘读数；T67 落地前不能 PASS。
16. **T66/T67 键盘优先级未落地。** `shouldDispatchHotkey` 当前“焦点时所有键都抑制”，与 main.cpp 注释的“F-key/Home 仍是命令”冲突；T70 设计采用“全部抑制”，但最终策略由 T67 定。
17. **T71 demo hash 算法未定。** T64 用 md5，T68 证据用 sha256；4.4 提案 `sha256`，T71 可能选别的；T72 的 `demoHash` 字段必须跟 T71 一致。
18. **没有 pl_* 事件 payload demo。** 只有 KOTH/5CP 两份 SourceTV；T72 的事件类型覆盖不能声称完整。
19. **没有真实“无书签/损坏/只读”workspace 文件。** 负控需要 fixture；当前未创建。
20. **没有 build/launch/lock 结束状态可报告。** 因为从未开始；这不是 PASS，是 NOT RUN。

### 5.3 静态检查结果（本次实际执行）

| 检查 | 命令/方法 | 结果 |
| --- | --- | --- |
| 产品是否有 `DemoGameEvent` | `grep -R "DemoGameEvent" native` | 0 命中（T68 仅 spec） |
| 是否有 JSON 库 | `grep -R "nlohmann\|rapidjson\|json.hpp" native` | 0 命中 |
| 是否有 workspace 实现 | `grep -R "WorkspaceStore\|workspace" native/include native/src` | 只有 `native/docs/TAKEOVER-2026-10-05.md` 提及，无实现 |
| 66 tick 常量 | `grep -R "kSecondStepTicks\|66" native/src/native_ui.cpp` | 只有 `kSecondStepTicks=66` 与其使用；无其它 66 pin |
| 当前 seek 写法 | `grep -n "g_playback\.tick" native/src/main.cpp` | 多处直接赋值；无 `PlaybackControl` |
| 当前 actual-frame 显示 | `grep -n "frameTick\|frameDelta\|frameStatus" native` | 0 命中 |
| 当前 review bookmark | `grep -R "ReviewBookmark\|review.bookmark" native` | 0 命中 |
| 现有 camera bookmark | `native/include/camera_bookmarks.h` | 8 槽、settings `[camera]`，与 T72 无关 |
| 现有 settings 写入 | `native/src/settings_store.cpp` | 只写 `[renderer]`，保留 `[camera]/[keys]`；无 workspace |

---

## 6. 静态合规报告（NEXT-TASKS §1 字段）

| 字段 | 内容 |
| --- | --- |
| 目标 | 产出 T70/T72 设计规格：T70 请求/实际 tick、逐帧、1s/5s、速度档、边界/不可达/连按/中断状态机与验收 mutation；T72 书签数据结构、T71 工作区字段、CRUD/搜索/过滤/跳转、same-tick、负控与验收 |
| 非目标 | 不实现产品代码；不 build；不启动；不获取 lock；不修改 `native/`；不导出/剪辑；不声称运行时 PASS |
| Owner | delegated design agent（本会话） |
| Baseline commit | `99eeea294cbe883e4c3e6b4b82c83de45d2ab926`（branch `p0-entity-protocol`） |
| Worktree | `D:/TF2_Native_Test` |
| Changed files | 仅 `evidence/t70-t72/DESIGN.md`（新建）；`native/` 0 改动 |
| Commands + exit | 只读 `read/grep/glob`；`sha256sum` 读取文件；无 build/launch/lock 命令。所有读取成功；无退出码非零 |
| Actual assertion/readout | 本文的读数都是**设计中的期望读数**，不是已执行读数；没有一行是运行时 PASS |
| Input hashes | `main.cpp` 87883d332d22ec4a7faf66653349fde0ec7f0f76bcbbb89ae654da909629fd36；`native_ui.cpp` 6fbff21f47bf6f7f3d54a52616b50218b787b2e81ebab607bd798e6f961f4770；`native_ui.h` 8d7f981d861f611e517f333caeb20979246bee1c9ed796c7b7fb4424638341be；`camera_bookmarks.h` eaad83b030ec0dc71dffffcd0cd2a7d4f19c05e861decbc5a819ee3bf7616cf2；`camera_bookmarks.cpp` ced9d1b5e85221a2f8854eac7fa8a208df2f401f2c7adc53c4cb97f0fcb32eb7；`settings_store.h` cdeb895abb12f134abd6ed7b0cd7f2e70b260e1e8b34bb6b5202c3cca04a291e；`settings_store.cpp` 0db0933da5a1d98e0926c6dbe0a9b5fc69766e6def551c44c59c74a7a0940830；`demo_header.h` cb9f00087d894cbe4c9e53886ed88c703c13fcde24ab9defb43319352284ae63；`keybinds.h` a47876ac55edfe62ead43103a37d0083f74e08d13d36afb998f93f5cca072184；`keybinds.cpp` 632b5fca64a40e3d3b9155ff9d4715c3eea592edde92c5fce95d4e572a6f5661；`ini_section.h` 49f173d6c308026e4f67b87ac2b002f3e70423c95a37557163faae06364aea9b；`camera_director.h` fe3e4792c350967bf3107740a975c1abd50c0408e241d7a0c6b88a618587db2c；T64 DATA-CONTRACT e8619d3bed564b8f741e0a79a31bd4d0a8a1fd06c6f768a95c8e1c8cfe7d43ff；T68 GAME-EVENT-SPEC 6b7763043672d43fc7d6e2c7a1961ad3f463ba4571778f1a5965eee04d2f790f；T68 SAME-TICK 4aeab6f64c054fbebb493ccb704a7cfea3cd74be05ffd7a8441122701da06ee7；T68 REPORT 8645b71e66fd7e5599ebcbea590202914e1bfa5d6351181e04304abc2e647547 |
| Evidence paths | `evidence/t70-t72/DESIGN.md`；无 frame/log（静态任务） |
| FAIL / INVALID / NOT VERIFIED | 全部 T70/T72 运行时验收 NOT VERIFIED；无 FAIL 声称；无 PASS 声称；`kSecondStepTicks=66` 与设计 67 的差异是已知设计变更，不是运行失败 |
| Mutation capture + byte-restore | 未执行产品 mutation；本文只定义 mutation。没有“已注入并恢复”的声称 |
| Build/process/lock end state | 未 build、未启动进程、未获取/释放 lock；`native/build-nmake` 未触碰；结束状态与开始一致 |
| Next steps | 1) T71 owner 确认 4.4 `WorkspaceDocument`/INI schema；2) T68 实现 `DemoGameEvent` 并补 actors；3) T70 实现 `PlaybackControl` + `playback_probe` + `--playback-script`；4) T72 实现 `ReviewBookmarkStore` + workspace adapter；5) 按 3.1/4.7 建 fixture 与 mutation gate；6) T67 落地后补焦点验收；7) 全部运行时读数出来前保持 NOT VERIFIED |

## 7. 建议实施顺序

1. **T71 workspace 契约先行**（只定 `WorkspaceDocument`/`WorkspaceStore` 接口和 INI schema），否则 T72 的持久化无法落地。
2. **T68 事件解码 + actors**：没有真实 `DemoEventSample.actors`，T72 的玩家身份只能为空。
3. **T70 `PlaybackControl` 纯逻辑 + `playback_probe`**：先让 1s/5s、速度表、状态机、landing 映射在无窗口探针中全绿。
4. **T70 产品接线**：替换 `g_playback` 直接赋值、UI controller 本地状态、transport if 分支；加标题/trace。
5. **T72 `ReviewBookmarkStore` 纯逻辑 + fixture**：先测 add/update/delete/query/sort/jump 事务，再接 workspace。
6. **T72 UI + 持久化**：列表/搜索/过滤/空状态/损坏状态；最后跑 reopen/hash/migrate/no-write 负控。
7. **T66/T67 焦点最终策略**：T70/T72 的键盘行必须用最终 `shouldDispatchHotkey`，不能各自维护第二套规则。

## 8. 结论

- T70 的核心不是“再加几个按钮”，而是把**请求 tick、实际 cursor、实际帧 tick** 三个数分开，并让 `SeekPending` 成为显式状态。没有这个状态，播放中的 seek 和快速连按无法证明“最后一次请求赢”。
- 1 秒不是 66 tick：在 `demoTickRate=66.666667` 下是 67；5 秒是 333。保留 66 会把“1 秒”变成 0.99 秒，且与 `durationSeconds` 的秒单位不一致。
- 逐帧的 `act` 可能不变（checkpoint 覆盖多个 tick）；验收必须断言 cursor 增量与 `frameDelta` 恒等式，不能断言画面帧号 +1。
- T72 的唯一身份键是 `(tick, packetOrdinal, messageBit)`；tick 单键在同一 tick 209/213 条事件下必然丢数据。
- 玩家身份只能到 `role/entindex/userid`；姓名不可得时留空并写 `no-userinfo`。任何默认名都是伪造。
- T71 尚未实现，4.4 是提案；T72 的持久化/reopen/hash 验收在 T71 定稿前一律 NOT VERIFIED。


