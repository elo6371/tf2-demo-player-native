# B2-1 尾段搬运施工单：`main.cpp` 退出段 → `main_shutdown.cpp`（2026-11-20）

状态：核对并改写自 Gemini 第 9 轮 G14 交付（第三部分）。基线 `28aba482`，`native/src/main.cpp` 16,068 行。
**行号口径**：本工作树有未提交的 N18-B 补丁（+22 行，全在 12.4k 之前），这段在工作树里是 `15766–16088`。
施工以符号为准（先提交 N18-B，或 `git show HEAD:native/src/main.cpp` 取基线），不要拿行号当锚点硬改。

## §1 搬运块（已逐行核对）

- 范围 `native/src/main.cpp:15744–16066`，**共 323 行**。
- 上界：主循环以 `PostQuitMessage(3);`（`:15740`）+ `}`×3（`:15741-15743`）闭合，`:15744` 起退出段。
- 首 2 行（`:15744-15745`）：`LARGE_INTEGER metricsEnd{};` / `if (QueryPerformanceCounter(&metricsEnd)) writeMetrics(metricsEnd, true);`
- 尾：`:16063` `g_renderer = nullptr;`、`:16066` `if (g_cameraScriptExitCode >= 0) return g_cameraScriptExitCode;`；
  `:16067` `return static_cast<int>(message.wParam);` 与 `:16068` `}` **留守 wWinMain**。
- 内部汇总子块：`:15756` 的 `{` 到 `:15958` 的 `}`（一条长 `traceFmt` + 键位/书签逐条输出），整块随搬运、不要拆：
  它读关闭前的活对象（`soundScheduler.stats()` `:15758`、`renderer.lastFrameCaptureSucceeded()` `:15779`）。

已核对的局部符号（HEAD 行号）：`renderedFrames`(:15778)、`captureRequested`(:15779)、`captureDepthPath`(:15780)、
`renderer.lastDepthCaptureBytes()`(:15781)、`renderer.passDepthWritten()`(:15782)、`metricsPath`/`metricsFile`/`metricsWriteCalls`(:15783)、
`message.wParam`(:15784)、`g_nativeUi`(:15785-15786)、`soundScheduler`(:15758)、`window`(:15757)。

## §2 顺序红线（一步都不能重排）

1. `writeMetrics(metricsEnd, true)` — `:15745`
2. 汇总 `traceFmt` 子块（含 `keys: bind` 与 `camera: bookmark` 逐条）— `:15756-15958`
3. `g_cameraBookmarks.saveToSettings(...)` / `g_keyBinds.saveToSettings(...)` — `:15963-15966`（`settingsPath()` 非空才写）
4. `renderer.shutdown()` — `:15967`
5. `logsPanelFrameLine/SummaryLine/RuntimeLine` + `logsPanelShutdown()` — `:15970-15976`
6. `persistent.render = renderer.settings();` + `assets.valid()` 回写 `tfRoot` + `saveSettings(persistent)` — `:15977-15979`
7. `g_hud.saveTo(hudSettingsFile)`（`hudTouched` 才写）— `:16003-16012`
8. `g_reviewSession->finish(); g_reviewSession.reset();` — `:16024-16027`
9. `g_renderer = nullptr;` — `:16063`，**必须晚于 4**（否则 `WndProc` 解引用已 shutdown 的 renderer），**早于** `wWinMain` 栈上 renderer 析构。

第 8 步之后还有 N12-C 的眼计数汇总（`:16029-16062`），同样读关闭前的统计对象，随块搬。
第 6 步读 `renderer.settings()`，位置不能挪到第 4 步之后。

## §3 控制流

- 块内无 `goto`/`break`；唯一 `continue` 在 `:15951`（书签 `for` 内），搬走安全。
- 两条早退只剩 `return`：`:16066` 脚本退出码、`:16067` `message.wParam`。搬运后的入口函数必须原样返回这两个值（不统一成 0、不吞脚本码）。
- 主循环之前 CLI 校验的早退（`return 13` / `return 2`）不进该块，保持原样。

## §4 签名与放置

推荐扁平结构，类型名从声明处读（交付的示意类型未核实；`soundScheduler` 的声明是 `tf2::native::AudioEventScheduler soundScheduler;`，HEAD `:7851`）：

```cpp
// native/src/main_internal.h
namespace tf2::native::main_internal {
struct ShutdownContext {
  HWND window;
  Renderer& renderer;
  const MSG& message;
  const std::function<void(LARGE_INTEGER, bool)>& writeMetrics;  // 引用传 wWinMain 里的 lambda，无分配
  std::size_t renderedFrames;
  bool captureRequested;
  const std::filesystem::path& captureDepthPath;
  const std::filesystem::path& metricsPath;
  const std::ofstream& metricsFile;
  std::size_t metricsWriteCalls;
  const AudioEventScheduler& soundScheduler;
  bool audioDisabled;
  std::size_t soundEventsRead;
  const AudioTimeline& soundTimeline;
  std::int32_t firstPacketTick;
  std::int32_t soundOriginTick;
  const DemoNetworkSummary& demoNetworkSummary;
  PersistentSettings& persistent;
  const AssetBundle& assets;
  bool hudConfigRequested;
  const std::filesystem::path& hudRoot;
  const std::string& hudLayout;
};
int runMainShutdown(ShutdownContext& ctx);   // 返回值 = 原 :16066/:16067 的两个 return
}  // namespace
```

调用点：`main.cpp:15744` 处换成构造 + 一行调用。**35 个 `g_*` 本轮不并入结构体**——已在 `main.cpp` 文件作用域，
跨 TU 走 `main_internal.h` 的 `extern` 声明（`g_renderer`/`g_nativeUi`/`g_cameraMode`/`g_cameraBookmarks`/`g_keyBinds`/`g_hud`/`g_reviewSession`/`g_cameraScriptExitCode` 等），
一次动一个，避免把 323 行搬运扩散成接口手术。

`uiSettingsFilePath`（`:16004` 调用）在匿名命名空间里：搬出后要么移进 `main_internal.h`，要么改成 `tf2::native::` 公开声明——
这是本单唯一的非机械改动，先确认它有没有别的调用者。

## §5 验证

1. `native/cmake/tf2-product.cmake` 加 `src/main_shutdown.cpp` → `touch native/CMakeLists.txt` → `bash build-target.sh tf2_demo_native`。
   `touch` 必须做：N24 的教训是编辑工具保留 mtime，NMake 既不重编也不重跑 CMake，会链接旧 obj 直到 LNK2019。
2. `tools/warn-check.sh`：TU 139 → **140**，零新告警（全树 `/W4`）。
3. `build-identity-check.sh`：`assertions=222` 不降。
4. 行为等价：搬运前后两 exe 同输入，`TF2_NATIVE_TRACE` 末尾关闭汇总行逐字段一致；tick-0 帧 md5 不变。
5. 受影响功能门禁：`frame-capture-check.sh`、`hud-check.sh`、`map-overview-check.sh`、`playback-error-paths-check.sh`。

## §6 不做 / 不确定

- 不做：不改逻辑/顺序/变量名；不搬主循环；不碰 CLI 与 `main_cli.h`；不顺手再拆 `wWinMain`。
- 不确定：`C6262`（`wWinMain` 栈 58,208 B）能降多少只能搬完实测；`writeMetrics` lambda 捕获未逐项回溯，先用 `const std::function<>&` 引用传，实测再决定是否模板化。
- 与 N18-B 交叉：+22 行落在 12.4k 一带，不影响本块内容；但两个改动**不要同一次提交**，否则 `build-identity-check` 的 `compiled-input` 探针会把两个 TU 的变化混进同一条身份。
