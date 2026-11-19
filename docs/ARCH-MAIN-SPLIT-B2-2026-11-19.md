# T2：`main.cpp` 第二批架构分解（只读设计）

- **目标文件**：`docs/ARCH-MAIN-SPLIT-B2-2026-11-19.md`（只读设计，未改源码）
- **基线提交**：`work/integrate` @ `69804728f368099aeda7b2e5a8e89056521b3611`（行号均为该提交实测；T1 落地后 `main.cpp` `:4554` 之后行号将整体 `+22`）
- **硬边界**：只做纯搬运，不改任何 `--` 开关比较顺序，不改逻辑/语义/trace/帧，不顺手重构或删死代码。

---

## 1. 量化盘点（`69804728` 实测）

| 指标 | 命令 / 来源 | 实测结果 |
| :--- | :--- | :--- |
| `main.cpp` 总行数 | `wc -l native/src/main.cpp`（GitHub Code 实测） | **16,046 行**（15,933 loc，931 KB） |
| `native/src/*.cpp` TU 数 | `ls native/src/*.cpp \| wc -l`（GitHub 目录实测） | **83 个 `.cpp`**（全树 warn-gate 139 TU） |
| `wWinMain` 行区间 | `native/src/main.cpp:2954–16046` | **13,093 行**单个函数 |
| `wWinMain` 最大线性代码段 | `main.cpp:4840–11588`（约，受 B1 `-175` 偏移） | 启动期资产根探测 + Demo 预加载 + D3D11/BSP/材质/HUD 初始化（约 **6,748 行**） |
| CLI 开关比较数 | `docs/TASK-LIST-2026-11-19.md:48–49` | `main.cpp` 156 条 `wcscmp(..., L"--"` + `main_cli.h` 34 表项 = **190 条** |
| 近 200 次提交变更热点 | `git log --numstat -200 -- native/src/main.cpp` | **未测**（见 §4 静态无法定论） |
| exe 字节数与编译耗时 | 本地构建产物 | **未测**（见 §4；旧台账 `TASK-LIST:17` 曾记 `3,266,560 B`） |

---

## 2. 隐式状态清单（`native/src/main.cpp:134–204` 实测 35 个文件作用域可变全局）

全部位于 `namespace tf2::native::main_internal`（`:134–299`）：

| 行号区间 | 变量名与类型 | 读写归属 | 跨 TU | 搬运前提 |
| :--- | :--- | :--- | :---: | :--- |
| `:135` | `Renderer* g_renderer` | `wWinMain` 写；WndProc/主循环/绘制/关闭读 | 是 | 先参数化为 `Renderer&` |
| `:136–138` | `HWND g_uiHudPreview/Scale/Status` | UI 创建写；HUD/WM_COMMAND 读 | 否 | 归入 `UiControls` 结构体 |
| `:139, :195` | `KeyBindTable g_keybinds` / `g_keyBinds` | `registerKeybinds` 写；按键派发读 | 否 | 统一注入引用 |
| `:145` | `NativeUiController* g_nativeUi` | `wWinMain` 写；WndProc/UI 循环读 | 是 | 传入 `NativeUiController&` |
| `:146–169` | `HWND g_uiStatus`..`g_uiHudReset`（20 个控件句柄） | `createNativeUiControls` 写；WndProc/UI 更新读 | 否 | 打包为 `struct UiHandles` |
| `:172` | `std::size_t g_uiBookmarkSlot` | UI 列表选择写；书签保存/删除读 | 否 | 归入 `UiRuntimeState` |
| `:180, :183` | `bool g_keybindsLoaded`, `g_textFieldFocused` | 启动/焦点事件写；热键分发读 | 否 | 归入 `InputState` |
| `:194` | `PauseKeyController g_pauseKey` | 键盘事件写；主循环读 | 否 | 归入 `InputState` |
| `:199–200` | `std::string g_lastDispatchedAction`, `uint32_t g_hotkeySuppressedByFocus` | 热键派发写；trace/HUD 读 | 否 | 归入 `InputState` |
| `:204` | `std::unique_ptr<ReviewSession> g_reviewSession` | 评审工作区写；UI/关闭读 | 否 | 按引用传给评审/关闭块 |

*(注：`main.cpp:205–2953` 中其余文件作用域 `g_*` 如 `g_playback`、`g_windowSizePinned`、`g_paneSplit`、`g_spectate*` 及函数局部 `static` 共 4 处，详见 `docs/N24-MAIN-SPLIT-PLAN-2026-11-16.md` §3.3，搬运对应簇前需逐一参数化。)*

---

## 3. 第二批纯搬运清单（4 批，每批 ≤400 行、互不重叠、可独立回滚）

| 批次 | 提取目标 | 基线起止行 (`69804728`，约) | 迁往新 TU | 预计 diff | 纯搬运判定依据 | 该批验收 | 回退方式 |
| :--- | :--- | :--- | :--- | :---: | :--- | :--- | :--- |
| **B2-1** | 关闭汇总与配置保存块（Shutdown） | `main.cpp:15722–16045`（324 行） | `native/src/main_shutdown.cpp` | ~350 行 | 纯单向尾段，零循环反馈；用显式 `ShutdownInputs` 替代 40 个局部变量捕获；obj 数 `+1` | 构建 rc=0；`BUILD-IDENTITY=PASS 222`；四门禁全绿；tick-0 md5 = `0fcb6867c46d55d9ef9fc95bf65596c2` | `git revert` 单提交 + `touch native/CMakeLists.txt` |
| **B2-2** | 抓帧装弹 / 读数 / 退出判定（Capture） | `main.cpp:15409–15676` 局部块（~120 行） | `native/src/main_capture.cpp` | ~180 行 | 仅读写 `CapturePlan`、`Renderer&` 与 `const PlaybackState&`（不写 `g_playback.tick`）；`traceFmt` 零改动 | 同上 + `frame-capture-check.sh` 4/4 PASS | `git revert` 单提交 |
| **B2-3** | HUD / Minimap / Title 循环尾段 + `hudApplyRects` | `main.cpp:15375–15408` 等（~160 行） | `native/src/main_hud_loop.cpp` | ~220 行 | 先将 `:136–169` 控件句柄收口为 `UiHandles&`；不新增 D3D11 头依赖 | 同上 + `hud-check.sh` 全绿 | `git revert` 单提交 |
| **B2-4** | `main_cli.h` 扩表：迁入主循环 40 条纯布尔 & `on\|off` 开关 | `main.cpp:3715–4195`（子集） | `native/src/main_cli.h` | ~320 行 | 保持 `argv` 单遍顺序执行；所有 `OutputDebugStringW`/`traceFmt` 零改动；`wcscmp L"--"` 再降 ~45 条 | 同上 + `argument-refusal-check.sh` + B1 差异矩阵 | `git revert` 单提交 |

### 不建议搬的（本阶段冻结）
1. **`wWinMain` 主循环核心与 `g_playback.tick`**：`g_playback.tick` 有 3 处写入者（T70 ledger、`--capture-tick` hold、seek/pause）且扇出极广，必须留在主循环所有者中按引用传递。
2. **`C1061` 敏感的剩余顺序依赖多参开关链**（如 `--quality` 重置 `persistent.render`、`--pane-split` 影响后续 `--pane-activate`）：在 B2-4 未完成纯布尔减负前不要整体改写。
3. **启动期 6,748 行资源构建块**：生产约 80 个跨循环局部变量，必须在消费端（B2-1/2/3）全部收口后最后拆。

---

## 4. 静态无法定论清单
1. **`git log --numstat -200` 变更热点 Top 15–20**：本环境无本地 git 仓库，无法执行 `git log --numstat` 统计，需你在本地跑 `git log --numstat -200 -- native/src/main.cpp` 复核是否调整 B2-1..B2-3 优先级。
2. **`wWinMain` 精确局部变量数与直接调用函数数**：需本地 AST/clang-query 或脚本扫描 `main.cpp:2954–16046` 给出精确计数。
3. **当前构建时间与 exe 精确字节数**、以及 B2-1 迁出后 `wWinMain` 的 `C6262`（58,208 B）实际下降字节数，均需本地 NMake + `/analyze` 实测。
4. **B2-1/B2-2/B2-3 精确起止行号**：`main.cpp:15375–16045` 为按 B1 `-175` 行偏移推算，实操前须在 `69804728` 上 `grep -n "metricsEnd\|captureArmed\|hudApplyRects"` 核定精确行号。
