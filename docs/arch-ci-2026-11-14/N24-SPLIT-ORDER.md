# N24 — `main.cpp` 行为不变切口：评估与排序（arch-ci，2026-11-14）

> 作者：arch-ci（角色 F）。**只做评估，未改一行代码、未提交 git。**
> 基线：`git show HEAD:native/src/main.cpp` = **HEAD a901ece 的 12,912 行版本**，sha256 `3485e87f2ef35b74e9d91ad6f4c65b7e903bda3c8edeb6618b8697b5c40cb703`，已导出为 `.scratch/head_main.cpp`（gitignored）用于分析。
> **重要前提**：工作树里的 `native/src/main.cpp` 此刻是 13,093 行（sha256 `045f5e3de79ae964b5eefb04f260513b62d75cdcde416697a958aa10f35ff9c4`，mtime 2026-11-14 18:39:35），正在被 n12-camera 修改。**本报告所有行号都指向 HEAD 版本**；工作树对不上是预期的。

## 0. 一句话结论

`main.cpp` 的问题不是「文件太大」，是**一个函数 11,361 行**（`wWinMain` 从 :1551 到 :12911，占全文 88%）。按「行为不变」这个标准排，最该先做的是**切口 1：把 :2984–:3134 那段「参数值被拒 → 打印 → return 13」的 26 个拒绝分支搬成函数** —— 它只有 151 行，只碰 37 个局部量 + 2 个全局，**段内没有任何一条 `break`/`continue`/`goto`**，而且它的行为已经被 `build-identity-check.sh:785` 这条断言钉住了（`eq "arg surface: an unknown argument is still ignored, rc=13 is the value refusal"`），搬完立刻能验证。

最不该先做的是**帧循环（:9711–:12900）**：3,190 行、要喂 120 个局部量 + 49 个全局量，而且它的退出方式（`running = false`）和 `message` 循环是**共享状态**，不是纯 dispatch。

## 1. 判据：什么叫「搬了但不改行为」

搬一段代码出去，行为会变的**唯一**真正原因是 **splice trap**：段内的 `return` / `break` / `continue` 原本跳出的是整个 `wWinMain` 或外层循环，搬进新函数后只跳出新函数。所以排序不该看「段多少行」，该看「这种语句有几个」。

我写了个括号/字符串/注释感知的扫描器（`.scratch/n24/analyze2.py`、`exits.py`、`seamcost.py`），把「在 lambda 里的 return」和「在 `wWinMain` 顶层的 return」分开数——因为**仓库自己已经证明过 lambda 里的 return 不是陷阱**：

`:2152` `const auto parseFeatureArgument = [&](int& i) -> bool {` … `:2314` `};`，跨 162 行，里面每个分支的 `return true;` 意思是「这个参数归我」。调用点 `:2335` `if (parseFeatureArgument(i)) continue;`。**仓库自己写下的理由（:2146-2151 注释，可直接引用）**：

> 「The flags T50, T51, T52, T53 and T54 added. They are not in the chain below for one measured reason: an else-if chain this long stops compiling (MSVC C1061, hit the first time all five branches were merged -- 128 arms). The chain is a pure dispatch over mutually exclusive flags, so **moving a group of arms into a function changes no behaviour**: order never decided anything, and "this argument was mine" is now the return value.」

这就是本报告用的判据：**纯 dispatch + 顺序不决定结果 + 归属用返回值表达** ⇒ 可以搬。反之，靠 `return <退出码>` 退出进程的段，搬的时候必须把退出码也搬（返回 `std::optional<int>` 或 `int` + 哨兵），否则就是把行为改了。

## 2. 全文基线（实测，`.scratch/head_main.cpp`）

| 区域 | 行范围 | 行数 | `wWinMain` 顶层 `return` | 顶层 `break`/`continue` | 段内 lambda |
|---|---|---|---|---|---|
| G 前置（顶层函数） | :1–:1550 | 1,550 | 67 | 0 | 8 |
| A 参数解析 | :1613–:2970 | 1,358 | 5 | 21 | 1 |
| B 参数校验/拒绝 | :2968–:3241 | 274 | 24 | 0 | 0 |
| C 资源准备 | :3242–:6328 | 3,087 | 1 | 40 | 30 |
| D 帧前准备 | :6329–:9711 | 3,383 | 4 | 2 | 38 |
| E 帧循环 | :9711–:12900 | 3,190 | **0** | 53 | 15 |
| F 退出落盘 | :12837–:12911 | 75 | 2 | 0 | 0 |

**全文 `wWinMain` 顶层 `return` 一共 38 个**，按表达式分：`return 13` × 26、`return 2` × 2、`return 15` × 2、`return 10` / `11` / `12` / `14` 各 1、`return runLifecycle(lifecycle)` 1、`return selftestResult` 1、`return g_cameraScriptExitCode` 1、`return static_cast<int>(message.wParam)` 1。`goto` 全文 **0** 个、`goto` 标签 0 个（4 个 `label` 命中全是 `public:` / `private:` / `default:`），所以**没有任何段会因为 goto 而搬不动**。

## 3. 候选切口（按可证性排序）

### 切口 1（建议先做）：参数拒绝块 `:2984–:3134`

- **是什么**：`if (audioDeviceRejected) { OutputDebugStringW(...); return 13; }` 这样的 26 个同形分支，每个读一个 `bool xxxRejected` 局部量，打一行诊断，`return 13`。首例 `:2984-2987`（`--audio-device`），末例 `:3130-3134`（`--viewmodel-phong/--viewmodel-bump/--viewmodel-selfillum`）。中间夹着 `:3063` `tf2::native::logsPanelAttach(window, instance, g_logsPanelOptions);` 和 `:3105-3114` 六条 `if (xxxGiven) persistent.render.yyy = xxxRequested;`。
- **量纲**：151 行 / 37 个局部量 / 2 个全局量（`g_logsPanelArgumentRejected`、`g_logsPanelOptions`）/ **0 个 `break`/`continue`/`goto`**。
- **splice trap 形态**：段内 26 个 `return 13` 都是**退出进程**，不是「这一支归我」。所以接口必须是 `std::optional<int>`（或 `int` + `-1` 哨兵）：`if (const auto refusal = checkArgumentRefusals(...)) return *refusal;`。这一步**必须显式做**，否则就是悄悄改了行为（拒绝路径变成继续执行 GPU 初始化）。
- **为什么它最可信**：行为已经被门禁钉住 —— `build-identity-check.sh:785` `eq "arg surface: an unknown argument is still ignored, rc=13 is the value refusal" "$UNKNOWN_RC" "13"`，用的是 `--playback-speed 0` 这条拒绝（`main.cpp:3159-3165`，注释在 :3018-3023）。搬完这条断言立即能验。
- **风险点（如实写）**：这 26 个分支之间**不是纯 dispatch** —— `:3063` 的 `logsPanelAttach` 是把选项变「实」的那一步（:3053-3062 注释说明它必须在 `g_controller` 为空时放在这里），而 `:3105-3114` 六条是「最后一句说」的覆盖。所以切口要么把这 3 处留在原地、只搬纯拒绝分支（推荐），要么把它们一起搬并连带 `window`/`instance` 两个参数。**只搬纯拒绝分支时，参数表只需 `const std::function<void()>`-free 的 37 个 bool/路径引用**，仍是可证的。
- **提交形态**：先只搬 `:2984–:3052`（前 15 个分支，纯拒绝、无副作用），绿；再搬 `:3064–:3134`（后 11 个）。**一次一个切口**，别一次搬 26 个。

### 切口 2：退出落盘 `:12837–:12911`

- **是什么**：书签与键表落盘 → 面板浏览器销毁 → `[hud]` 段落盘 → `g_reviewSession->finish()` → `g_renderer = nullptr;` → `return g_cameraScriptExitCode >= 0 ? g_cameraScriptExitCode : (int)message.wParam;`。
- **量纲**：75 行 / 6 个局部量 / 8 个全局量 / 0 陷阱 / **2 个 `return` 恰好都在最后两行**（`:12910`、`:12911`）。
- **为什么可信度第二**：尾部两个 `return` 天然在函数末尾，搬出去后变成新函数的返回值，**没有一个能被新函数「吃掉」的位置**。而它做的全是副作用（保存、关闭），没有分支逻辑。
- **为什么不是第一**：它没有门禁直接钉「退出时保存了书签」以外的语义，而书签保存的断言在 review-workspace 系（`review-workspace-check.sh`）里，需要真跑产品才能验 —— 比切口 1 的纯 rc 断言贵。

### 切口 3：渲染器初始化 `:6246–:6328`

- **量纲**：83 行 / 8 个局部量 / 1 个全局量 / 0 陷阱 / 1 个 `return 12`（`:6252`，`renderer.initialize` 失败）。
- **为什么排第三**：它要 `renderer`、`window`、`persistent` 三个实参（都是 `wWinMain` 的局部量），且 `:6246` 失败路径要 `DestroyWindow(window)` —— 搬出去后这个调用顺序（trace → DestroyWindow → return 12）必须原样保留，是可证的，但**验证需要真 GPU**（没有 GPU 走不到这条路径）。

### 切口 4（**不建议此时做**）：帧循环 `:9711–:12900`

- **量纲**：3,190 行 / 120 个局部量 / 49 个全局量 / 顶层语句 146 条 / `traceFmt` 81 次 / `traceCheckpoint` 3 次。
- **好消息（实测，可能反直觉）**：帧循环里**根本没有指向 `while (running)` 的 `break`/`continue`**。58 条 `break`/`continue` 的括号深度分布是 `{2:1, 3:3, 4:6, 5:17, 6:17, 7:8, 8:5, 9:1}`，深度 2（= 直接属于 `while` 体）只有一条：`:9720` `if (!running) break;`。退出循环的唯一状态写入是 `:9716` `if (message.message == WM_QUIT) { running = false; break; }` —— 全文 `running = false` 只有这一处。所以**帧循环的 splice trap 只有一个，就是那个 `break`**，把它改成 `return`/标志位即可。
- **坏消息**：它需要 169 个状态项（120 局部 + 49 全局）参与。这不是「搬代码」，是「先造一个 169 字段的上下文结构，再搬」。那一步的正确做法是先做**结构**（把 120 个局部量收进一个 `struct FrameContext`，仍然是纯改名，仍可逐个绿），而不是直接搬 3,190 行。
- **所以**：帧循环不是「不能做」，是「它的第一步不是切口而是前置重构」，不该和切口 1/2/3 并列排序。

### 其它已量化的段（供后续排期，本轮不推荐）

| 段 | 行数 | 局部量 | 全局量 | 陷阱 | 备注 |
|---|---|---|---|---|---|
| A 参数解析 `:1613–:2970` | 1,358 | 131 | 15 | 5 ret / 21 brk | 已知可搬先例：`parseFeatureArgument`（162 行，:2152–:2314）**已经是 lambda**，说明这条路走过；但那 5 个 `return` 里 2 个是 `return 13`（`--settings-file`/`--ui-settings-file` 缺值，:1634/:1644），3 个是早退（:1623 `runLifecycle`、:1671 `selftestResult`、:2968 strict-args 的 `return 2`） |
| C 资源准备 `:3242–:6328` | 3,087 | 156 | 9 | 1 ret / 40 brk | 只有 1 个顶层 `return`（`:6252`，即切口 3 的那个），40 个 `break`/`continue` 全在段内自己的循环里 ⇒ **这块其实很「干净」**，但要喂 165 个状态项 |
| D 帧前准备 `:6329–:9711` | 3,383 | 64 | 9 | 4 ret / 2 brk | 4 个 `return`：`:7920` return 14（`reloadGpuResources` 失败）、`:9280` return 13（`QueryPerformanceFrequency` 失败）、`:9289` return 15（`--metrics-file` 打不开）、`:9686` return 15（camera script 读不了）。都是硬失败，可搬 |

## 4. 排序与理由

| 顺序 | 切口 | 行数 | 状态项 | 陷阱 | 能否立即验证 | 
|---|---|---|---|---|---|
| **1** | 参数拒绝块 `:2984–:3134`（分两次） | 151 | 39 | 0 brk/cont；26 个 `return 13` 需显式外带 | ✅ `build-identity-check.sh:785` 已钉 rc=13 |
| **2** | 退出落盘 `:12837–:12911` | 75 | 14 | 0；2 个 `return` 都在末尾 | ⚠️ 需真跑产品（review-workspace 系） |
| **3** | 渲染器初始化 `:6246–:6328` | 83 | 9 | 0；1 个 `return 12` | ⚠️ 需真 GPU 才能走到失败路径 |
| — | 帧循环 `:9711–:12900` | 3,190 | 169 | 1（`:9720` 的 `break`） | ❌ 先要做 `FrameContext` 前置重构 |

判据不是「哪个大就先拆哪个」，是「**哪个搬完能立刻证明没变**」。切口 1 之所以第一，是因为它的可观测契约（rc=13）已经被一条现存断言钉住，搬完跑一次 `build-identity-check.sh` 就有答案；切口 2/3 要真跑产品/真 GPU。

## 5. 落成切口 1 的接口清单（供 lead 判断要不要做）

**不动的东西**：`:3063` `logsPanelAttach(...)` 与 `:3105-3114` 六条 `if (xxxGiven) persistent.render.yyy = ...` 留在 `wWinMain` 原位（它们是「把选项变实」的副作用，不是拒绝判定）。

**新增文件**：`native/src/main_arguments.cpp`（沿用既有 `main_*.cpp` 模式）+ 在 `native/src/main_internal.h` 追加一个声明块。既有 6 个 TU 的规模参考：`main_camera.cpp` 139 行 / 5 个函数、`main_helpers.cpp` 198 行 / 7 个、`main_hud.cpp` 1,308 行、`main_lifecycle.cpp` 116 行 / 3 个、`main_trace.cpp` 104 行 / 4 个、`main_ui.cpp` 347 行 / 5 个。**照这个模式，切口 1 是这批里最小的一块。**

**接口形状（草案）**：

```cpp
// main_internal.h（追加，沿用 T76 那套「不是 public header」的定位）
struct ArgumentRefusals {
  bool audioDevice = false;
  bool captureTick = false;
  // ... 26 个，与 main.cpp 里的 xxxRejected 同名去掉后缀
};
// 返回 std::nullopt = 没有一个被拒；否则是应当返回的退出码。
std::optional<int> refuseRejectedArguments(const ArgumentRefusals& refusals,
                                           const std::filesystem::path& captureDepthPath,
                                           const std::filesystem::path& captureFramePath,
                                           bool worldDynamicLightCombined);
```

**为什么不用 `if (...) return 13` 的宏**：宏会把 26 个分支的**文本行**也搬走，diff 看起来一样小，但 `OutputDebugStringW` 的字面量（其中 13 条带 `refusing to guess` 的解释）会留在宏参数里，反而更难审。函数 + `std::optional<int>` 让「拒绝 → 退出码」这条契约**出现在类型里**，这是可比文本搬移更强的东西。

**注意 `:2996-3005` 不是同形**：`if (!captureDepthPath.empty() && captureFramePath.empty()) captureDepthRejected = true;` 是**先判后拒**两段式，不是「读一个 bool」。搬的时候要连判定一起搬，否则拒绝原因会消失。

## 6. 建议的分步提交（一次一个切口）

1. **切口 1a**：只搬 `:2984–:3052`（前 15 个纯拒绝分支）。新增 `main_arguments.cpp` + `main_internal.h` 声明。验：`build-identity-check.sh` 绿（尤其 `:785` 的 rc=13 断言）。
2. **切口 1b**：搬 `:3064–:3134`（后 11 个 + `:2996-3005` 那段两段式）。同上验。
3. （停下报 lead）之后才是切口 2/3。

**每一步都必须单独绿**。分批不是为了省事，是因为「一次搬 26 个拒绝分支」一旦红了，你不知道是哪 1 个；分开搬，红的那个自己会指出来。

## 7. 这份文档不能证明什么

- **没有改过一行代码，所以没有任何「搬完是绿的」的证据。** 上面所有量纲都是对 HEAD 文本的静态统计，不是编译或运行结果。
- 没有验证「1613–2970 之外没有第三方 include 依赖这些被搬的符号」：`main.cpp` 只 include `main_internal.h` / `main_hud.h` / `main_camera.h` 三个（:74/:76/:81），但 `main_analysis` 的**符号级**依赖没有做闭包计算。
- 没有验证 `std::optional<int>` 方案在当前 `/W4 /permissive-` 下无新告警（`tf2_demo_native` 目标就是 `/W4 /permissive- /utf-8`）。
- 帧循环那 120 个局部量是按「`wWinMain` 内 indent=2 的声明 + 循环体内被引用」算的**上界**，可能包含循环内 shadow 掉的重名项（例如 `entitySnapshotStatus` 之类），真做 `FrameContext` 时这个数只会变小，不会变大。
- 没有判断「切口 1 之后 `main.cpp` 会小多少值得做」—— 151 行对 12,912 行是 1.2%。**这个切口的价值不在减行数，在建立第一个「带退出码的可搬段」的先例**，以及给后面 3,000 行级的段提供模板。如果 lead 只关心文件大小，这个切口看起来会不值得。

## 8. 证据

| 文件 | 内容 |
|---|---|
| `.scratch/head_main.cpp` | `git show HEAD:native/src/main.cpp`，12,912 行，sha256 `3485e87f…cb703` |
| `.scratch/n24/analyze2.py` | 括号/字符串/注释感知扫描，分「lambda 内 return」与「`wWinMain` 顶层 return」 |
| `.scratch/n24/exits.py` | 38 个顶层 `return` 站点枚举（表达式 + indent） |
| `.scratch/n24/seamcost.py` | 每个候选段的「局部量 + 全局量」状态项计数 |
| `.scratch/n24/frameloop.py` | 帧循环 `break`/`continue` 的括号深度分布 |
| `.scratch/n24/blocks.py` | 帧循环顶层语句 146 条、`traceFmt` 计数 |
| `build-identity-check.sh:785` | 现存 rc=13 契约断言（切口 1 的验证入口） |
| `native/src/main_internal.h` | 180 行，:3 注明「T76 (2026-10-29): declarations for the code split out of main.cpp」 |
| `.scratch/head_main.cpp:2146-2151` | 仓库自己写下的「搬 dispatch 分支不改行为」的理由 |
