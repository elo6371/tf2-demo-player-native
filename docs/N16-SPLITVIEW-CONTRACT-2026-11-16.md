# N16 双视角分屏：状态与资源契约（2026-11-16）

**性质**：设计契约，**零产品代码**。本文不修改任何 `native/` 源文件，不新增构建目标，不运行产品，不含任何实测数字。
**读者**：N16 的实现者（本卡之后另派）、D 线验收人、C/F 资源审阅人。
**范围**：两窗等分（split）。PIP 与四窗**只做评估**，见 §9，不在本轮实现。
**引用口径**：本文所有 `file:line` 均在本次写稿时逐条读取当前工作树核对过（HEAD a901ece 之后的工作树，含他人未提交改动）。行号会漂，实现者落到文件时以**函数名/注释原文**为准，行号只作定位起点。

---

## 0 这份契约要解决的三件事

现有产品是**单窗、单相机、单 HUD、单小地图、单视口**。`native/src/main.cpp:1743` 只创建**一个** `CreateWindowExW`，没有子 HWND、没有 pane 概念（对 `native/src` 全目录 grep `pane` / `splitView` 只命中小地图面板、相机 resetView 计数与 HUD 面板拒绝等无关处）。

而播放时间轴**必须**只有一份：`native/src/playback_state.h:21-35` 的 `PlaybackState` 是全局 `extern PlaybackState g_playback`（`:34`），其头注释 `playback_state.h:8-12` 明说渲染器、音频调度器和相机导演都读它，它是主循环对 transport 的唯一视图。

所以 N16 的全部难度不在"再画一次"，而在：**把已经全局化的东西切成按窗的，同时保证不能被切开的那一份真的没被切开**。本文按 §1–§7 给出可判定的边界。

---

## 1 每窗状态契约

建议新增头文件 `native/include/split_view_state.h`（**本卡不创建**）。所有字段名、类型、默认值、取值范围如下表；实现必须逐字采用，因为 §6 的断言直接读这些字段的 trace 名。

### 1.1 视图矩形与窗格标识

    enum class PaneId : std::uint8_t { PaneA = 0, PaneB = 1 };
    constexpr std::size_t kPaneCount = 2;   // 第一片固定两窗

    struct PaneViewport {
      std::int32_t x = 0;        // 客户区像素，含 x
      std::int32_t y = 0;
      std::int32_t width = 0;    // >=1 才 valid
      std::int32_t height = 0;
      bool valid = false;        // == (width >= 1 && height >= 1)
    };

| 字段 | 类型 | 默认 | 范围 / 约束 | 归属 |
|---|---|---|---|---|
| `x`,`y` | int32 | 0 | `>= 0`，且 `x+width <= clientWidth` | 每窗 |
| `width`,`height` | int32 | 0 | `>= 0`；等分布局下 `abs(A.width + B.width - clientWidth) <= 1` | 每窗 |
| `valid` | bool | false | 派生，不独立赋值 | 每窗 |

**等分布局的唯一算法**（写死，避免两人算出不同结果）：

    client   = GetClientRect(window)          // 客户区，不含边框
    splitX   = client.right / 2               // 整数除法，余数一律给左窗
    A = { 0,       0, splitX,                 client.bottom }
    B = { splitX,  0, client.right - splitX,  client.bottom }

余数给左窗这一条是**必须写死**的：DPI 缩放后客户区宽度可能是奇数，两种取整方式都能自圆其说，验收断言需要一个确定值。断言：`A.width >= B.width` 且 `A.width - B.width <= 1`。

### 1.2 目标

    struct PaneTarget {
      std::int32_t entityIndex = -1;                              // -1 = 无目标
      std::uint32_t serial = 0;                                   // 见 EntityHandle (spectate_target.h:43)
      SpectateSource source = SpectateSource::None;               // None/Director/StableOrder/Manual
      bool pinned = false;                                        // 用户手选，跨死亡不被 handover 覆盖
      bool valid = false;                                         // == (entityIndex >= 0 && entityIndex <= 2047)
    };

| 字段 | 默认 | 范围 | 说明 |
|---|---|---|---|
| `entityIndex` | -1 | `-1` 或 `0..2047` | 与 `--spectate-target` 同口径：解析在 `native/src/main.cpp:2866-2876`，守卫在 `:2872`（`parsed >= 0 && parsed <= 2047`，否则 `++g_spectateArgumentRejected`）。**解析失败只计数、不静默取默认**。上下界写死在这里，两处必须一致。 |
| `serial` | 0 | u32 | 来自 `SpectateSlot` / `EntityHandle.serial`（`native/include/spectate_target.h:43`、`:53`）；slot 复用时 serial 变化即视为**目标已消失**，见 §4.1。 |
| `pinned` | false | — | 只有 §5.2 的显式选择会置 true；`Delete`（§5.3）清回 false。 |

### 1.3 相机状态（每窗独立）

    struct PaneCameraState {
      CameraMode mode = CameraMode::FirstPerson;   // native_ui.h:40
      CameraPose pose{};                           // camera_director.h:36-44
      bool poseValid = false;
      float fovDegrees = 90.0f;                    // clampViewModelFov 保持 40..120
      bool haveObserverView = false;               // 本 tick 该 target 有 origin+angles
      CameraMoveSource lastSource = CameraMoveSource::Player;
    };

| 字段 | 默认 | 范围 | 依据 |
|---|---|---|---|
| `mode` | `FirstPerson` | 三值枚举 | `native/include/native_ui.h:40` 定义 `enum class CameraMode { FirstPerson = 0, ThirdPerson, FreeLook }`；`:74` 成员默认即 FirstPerson；`:85-93` 注释给出 3×2 决策矩阵 |
| `pose` | `CameraPose{}` | focusX/Y/Z 归一化世界盒、yaw/pitch **弧度**、distance 盒单位 | `native/include/camera_director.h:36-44`，单位注释 `:30-35` |
| `poseValid` | false | — | 与 `CameraPose::valid` 同义；本字段是快照冗余，写时两者必须相等 |
| `fovDegrees` | 90.0f | `[40, 120]` | `native/include/native_renderer.h:34-36` `clampViewModelFov`；默认值取 `native_renderer.h:71` `viewModelFov = 90.0f` |
| `haveObserverView` | false | — | 派生自 `ModelInstanceTransform::hasOrigin && hasAngles`（`native/src/entity_model.cpp:206` 起的变换链） |

**注意 §8 的依赖**：`native/src/main_camera.cpp` 与 `native/include/main_camera.h` 本轮**禁止修改**（另一执行者正在串行编辑相机区）。因此 `PaneCameraState` 必须落在**新头文件**里，而 `syncDirectorPoseToRenderer`（`native/src/main_camera.cpp:43-57`，导演与渲染器的唯一接合点，Reset 分支调 `resetCamera()` 再 `setOrbitPose(focusX,focusY,focusZ,yaw,pitch,distance)`）的改造由该文件的属主承接。本文只登记（§8 第 22 行），不动它。

### 1.4 输入焦点

    struct PaneFocusState {
      PaneId focused = PaneId::PaneA;   // 启动即 A，不随鼠标悬停改变
      bool mouseCaptured = false;       // 右键拖拽期间为 true，见 §5.4
    };

规则（全部可断言）：

1. **只有"按下去"能改焦点**。`WM_MOUSEMOVE` 悬停**绝不**改 `focused`——否则鼠标划过另一窗就会偷走相机控制，这正是验收要禁的"切一个窗影响了另一个"。
2. 键盘一律路由到 `focused`：共享热键表在 `native/src/main.cpp:1284-1289`，历史按键处理在其下方（`:1373` 的 VK_SPACE 暂停等）。
3. 鼠标左键在 `PaneViewport` 内按下（`main.cpp:1040-1045`）→ 先 `focused = 该窗`，再按 §5.2 处理选择。
4. 右键拖拽（`main.cpp:1046-1050` 置 `g_orbiting = true` 并 `SetCapture(window)`；`:1055-1063` 的 WM_MOUSEMOVE 调 `orbitCamera`）与滚轮（`:1064-1066` 的 `zoomCamera`）作用于 `focused`，**不是**光标所在窗。理由同第 1 条：滚轮悬停改另一个窗的相机，是同一个越界行为的另一副面孔。

### 1.5 HUD 与小地图归属

    struct PaneHudState {
      bool hudEnabled = false;     // 每窗开关，默认关（与 hud.enabled 默认 0 一致）
      bool ownsMinimap = false;    // 第一片：只允许一个 pane 为 true
    };

- `hudEnabled` 默认 false：`native/include/hud_state.h:24` 写明 `[hud] hud.enabled int 0/1 default 0`（"a capture-only run draws no HUD"）。
- `ownsMinimap` 是**互斥**的：`std::count(ownsMinimap) <= 1`。理由见 §3.5——小地图帧在进程里只有一个。
- **归属已裁决（Q3）**：`ownsMinimap` **跟焦点窗**。唯一一句写死的表述：

  > 只有焦点窗拥有小地图和 HUD 主体，非焦点窗只画世界和最小标识。

  断言：任一帧 `count(ownsMinimap) <= 1`，且 `ownsMinimap` 的那个窗恒等于 `focus.focused`；切换焦点时整帧原子切换（不得出现同帧两窗都画或都不画）。

### 1.6 合成结构

    struct PaneState {
      PaneId id = PaneId::PaneA;
      PaneViewport viewport{};
      PaneTarget target{};
      PaneCameraState camera{};
      PaneHudState hud{};
    };

    struct SplitViewState {
      bool enabled = false;                        // false = 单窗，行为与今天逐位一致
      SplitLayout layout = SplitLayout::Single;    // Single / SplitVertical（左右等分；本轮不做拖拽，见 §11 Q1）
      PaneState panes[kPaneCount];
      PaneFocusState focus{};
      // 共享时间轴**不在**这里，见 §2
    };

`enabled == false` 时 `panes[0].viewport` 必须是整个客户区、`focused` 恒为 `PaneA`，且 `PaneB` 不参与任何读数的分母——**这样单窗路径的 trace 与今天逐字相同**，历史门禁不会因为新增字段而误判。

---

## 2 共享与独立的边界

每一个字段都必须落进下面两张表之一。表里没有的字段，实现者必须先问，不能自行归类。

### 2.1 共享（进程内唯一实例）

| 字段 | 现有位置 | 为什么必须共享 | 断言方式 |
|---|---|---|---|
| `tick` | `native/src/playback_state.h:21-35` `g_playback.tick` | 两窗画的是同一帧画面 | 每帧两窗的 `renderedTick` 相等 |
| `paused` / `reverse` / `speed` / `tickRate` / `endTick` / `anchor*` | 同上 | 同上；且 `speed` 已经是 `SpectateDirector::update` 的显式入参（`native/include/spectate_target.h:102-106`，**故意不给默认值**，见同文件 `:98-101` 的说明），分叉会让 seek 判定错乱 | `playback:` trace 每帧至多一行 |
| `PlaybackControl` 请求账本 | `playback_state.h:35` `g_playbackControl`、`:40` `applyPlaybackRequest` | 一帧只允许一个请求落地 | `PlaybackResult.id`（`native/include/playback_control.h:100`）每帧至多递增 1 |
| 解码后的场景快照 | `native/src/main.cpp:10082` `queryEntitySnapshotAtOrBeforeTick` | **这是分屏唯一的性能理由**：逐窗解码会把 CPU 翻倍 | `FrameLanding.resolvedSceneTick`（`playback_control.h:116-131`）两窗相同 |
| `soundScheduler` | `native/src/main.cpp:9962` | 一份音频，不可能两条时间轴 | 静音/非静音两条运行都不得出现第二组 `audio:` 时间轴行 |
| 世界几何 / 材质 / BSP 与 PVS 的**输入** | `native/src/main.cpp:8106-8155` | 数据源相同；但**裁剪结果不是**，见 2.2 | — |
| `--camera-script` 的步进节奏 | `native/src/main.cpp:12613`、`:732` `kTargetFrameSeconds = 1.0/120.0` | 脚本按时间推进，不能按窗推进 | `step` 序号单调且全局唯一 |

### 2.2 独立（每窗一份）

| 字段 | 今天的位置（单例证据） | N16 后归属 |
|---|---|---|
| 相机注视点 / 轨道角 / 距离 | `native/include/native_renderer.h:2271` `cameraYaw_`、`:2273` `cameraDistance_`、`:2278` `observerDemoView_`、`:2008` `frameView_` | 每窗 `PaneCameraState` |
| 观察者眼睛与角度 | `native_renderer.h:789` `setObserverDemoViewWorld` 写单一成员 | 每窗；**入口函数需带 PaneId** |
| FOV / aspect | `native_renderer.h:71` `viewModelFov`；`native/src/native_renderer_frame.cpp:149` aspect 由 `width_/height_` 推出 | 每窗；aspect 由**窗格**尺寸推出 |
| 视口 / 裁剪矩形 | `native_renderer_frame.cpp:36-41` 唯一 `RSSetViewports(1, &viewport)` | 每窗，每帧显式设置 |
| 目标玩家 | `native/src/main.cpp:697` 单个 `SpectateDirector g_spectateDirector` | 每窗一个实例 |
| HUD 图元与矩形 | `native_renderer.h:2336` 单个 `hudPrimitives_`；`native/src/main_hud.cpp:289` `hudApplyRects(HWND)` | 每窗（带 `paneOwner`） |
| 小地图帧 | `main_hud.cpp:46-57` 文件级 `g_minimapHudFrame` | 每窗（但只允许一窗 `ownsMinimap`） |
| 输入焦点 / 拖拽状态 | `native/src/main.cpp:174` `g_textFieldFocused`、`:349` `g_orbiting` | 焦点每进程一份（§1.4），**作用对象**每窗 |
| PVS 裁剪结果 | `native_renderer.h:606-613` `worldVisClipState` / `worldVisCameraCluster` / `worldVisClipReason` | 每窗重算，见 §3.3 |

### 2.3 一句话判据

> 只要一个量会改变"屏幕上的像素属于谁的视点"，它必须按窗；只要一个量会改变"现在是第几 tick"，它必须共享。

---

## 3 多窗资源约定

每条都点出现有代码位置，实现者照单改，不要重新发明。

### 3.1 视口（viewport）

- 今天：`native/src/native_renderer_frame.cpp:27-45` `Renderer::draw(clearR,clearG,clearB)` 在 `:36-41` 构造一个 `D3D11_VIEWPORT`（`Width=width_`、`Height=height_`），然后 `RSSetViewports(1, &viewport)`（`:41`）。**全客户区，无 scissor。**
- 约定：`draw` 必须能对**每个 pane** 设置一次 `RSSetViewports`。签名建议 `draw(const PaneRenderItem* panes, std::size_t count, ...)`，`PaneRenderItem` 携带 `PaneViewport` + `PaneCameraState` + 该窗的 vis draw list 句柄。
- **不变量**：一个 pane 的 pass 序列开始前必须显式设 viewport；不得依赖上一窗留下的值。理由不是洁癖——`native_renderer_frame.cpp:1457-1466` 的注释已经吃过这个亏：遗留 scissor "would clip the NEXT frame's world draw"。

### 3.2 裁剪（scissor）与深度缓冲

- 深度缓冲：`native/src/native_renderer_entity.cpp:101` `createTarget(UINT,UINT)` 创建**一个** `target_`(RTV) 与**一个** `depthStencilView_`（`DXGI_FORMAT_D24_UNORM_S8_UINT`，`MipLevels=1`，`ArraySize=1`，无 MSAA），尺寸 = 客户区；`:123` `releaseTarget()` 释放；调用点 `native/src/native_renderer.cpp:166`（init）与 `:875`/`:878`/`:885`（resize 路径的 release/create）。
- 清屏：`native_renderer_frame.cpp:30-32` 每帧一次 `OMSetRenderTargets(1, target_, depthStencilView_)` + `ClearRenderTargetView` + `ClearDepthStencilView(DEPTH|STENCIL, 1.0f, 0)`。
- **等分双窗的决定（关键，先想清楚再改）**：两个窗格的屏幕矩形**互不相交**，所以**一次全屏深度清除 + 逐窗 viewport** 就是正确的，不需要两个 DSV。代价是 0 额外显存。**但**必须显式说明为什么它不是错的：深度值只在同一视口内的像素之间比较，两个不相交矩形之间不存在比较。
- **反过来，PIP 会破坏这条**：PIP 的矩形**相交**，全屏一次清除之后，小窗的深度仍留在被它覆盖的位置上，母窗在那块区域的像素会与之竞争。所以 §9 明确记录：PIP 要么给每窗独立 DSV，要么改成"先画母窗全屏 → 再画小窗带自己的深度清除（需要 `ID3D11DeviceContext1::ClearView` 或离屏 RT + 合成）"。**这是 PIP 的主要成本，不是多画一次。**
- scissor 约定：D3D11 的 `Clear*` 系列**不吃 scissor rect**，且 `ClearDepthStencilView` 不受 viewport 限制，因此**禁止**用 scissor 去"只清一半深度"。当前唯一的 scissor 使用点是 HUD pass（`native_renderer_frame.cpp:1424-1430` 设、`:1457-1466` 恢复全屏），HUD 之后必须无条件恢复全屏 rect，否则下一窗的世界绘制会被切掉。

### 3.3 PVS 裁剪（真正的隐藏成本在这里）

- 今天：`native/src/main.cpp:8106-8155` `applyWorldVisClip` lambda。眼睛 → map 空间（`renderer.cameraEyeMapWorld`，`:8123`），`BspParser::clusterAt`（`:8126`），**若新 cluster == 上次已应用的 cluster 就提前返回**（`:8127`），否则 `clipWorldTrianglesByVis`（`:8133`）+ `renderer.setWorldVisDrawList(list)`（`:8154`）。开关在 `:8114` `setWorldVisClipEnabled`。调用点 `main.cpp:12656`，在 `renderer.draw`（`:12699`）之前。
- 后果：**两窗若在同一 cluster，这层几乎免费；在不同 cluster，每帧重建两次**。这个"cluster 相同"判断不是优化细节，它是 §7 成本模型里唯一可能让双窗帧时接近单窗的因素。约定：
  1. vis draw list 是**按相机**的，所以按 pane 重建，且必须在**该 pane 的 draw 之前**完成；
  2. 两窗 cluster 相同时允许复用同一份 list（读数的 `worldVisCameraCluster` 必须打印出来，声明是复用而非重算）；
  3. 禁止把"A 窗的 list"喂给 B 窗——`setWorldVisDrawList`（`native_renderer.h:1109`）是渲染器单例状态，顺序错了不会报错，只会画出错的墙。
- **可断言**：`worldVisApplied` / `worldVisCameraCluster` / `worldVisClipReason`（`native_renderer.h:606-613`）已经能区分"重算/复用"；N16 只需在 trace 里加 pane 前缀。

### 3.4 HUD 隔离

- 今天：`native/src/main_hud.cpp:289` `hudApplyRects(HWND window)` 里 `GetClientRect` → `dpiScale = GetDpiForWindow/96.0`（`:294`）→ `g_hud.layoutFor(client.right, client.bottom, dpiScale)`（`:295`）→ 构造 `Renderer::HudDrawRect`（固定 4 色调色板在 `:300-305`；`:311` 让隐藏面板仍推进颜色索引，使"面板颜色取决于文档而非本帧哪个兄弟被隐藏"）。`g_hudLiveActive` 时再叠加实时读数（`:323-344`）。
- 问题：`layoutFor(w, h)`（`native/include/hud_state.h:74`）用的是**整个客户区**。两窗下必须改成 `layoutFor(pane.width, pane.height)`，否则 HUD 会跨过分隔线画到另一窗里去。
- 图元容器：`native/include/native_renderer.h:2336` `std::vector<HudPrimitive> hudPrimitives_` 只有一份。约定：`HudPrimitive`（`native_renderer.h:1234`，种类枚举 `:1233`）增加 `std::uint8_t paneId`；绘制时按 pane 过滤并在该 pane 的 scissor 内画。
- 现有读数可直接复用为断言源：`hudRectCount()`/`hudDrawnRects()`（`native_renderer.h:1251`/`:1256`）、`hudScissorApplied()`（`:1266`）、`hudLastRegion()`（`:1269`）、`hudPassOrdinal()`（`:1262`）；产品侧已有 `hud: draw drawn=.. rects=.. ordinal=.. scissor=.. region=.. union=..` 与 `hudtext: ...`（`main.cpp:12751-12757`、`:12759-12765`）。
- **主体归属（Q3）**：HUD 主体只画在焦点窗（§1.5）。非焦点窗的 `hudEnabled` 必须为 false，且其 HUD 读数必须是"未绘制"（`hudDrawnRects() == 0`），不是"画了 0 个矩形所以看起来一样"。

### 3.5 小地图隔离

- 今天：`native/src/main_hud.cpp:46-57` 的文件级 `MinimapHudFrame g_minimapHudFrame` + `bool g_minimapHudFramePushed` 与三个访问函数，结构在 `native/include/minimap.h:381-388`（`valid`、`mapName`、`eyeX`、`eyeY`、`yawDegrees`、`tick`），API 声明 `minimap.h:392`/`:397`/`:401`。帧在 `main.cpp:12575-12602` 由 `cameraTraceEye`/`cameraTraceYaw`/`g_playback.tick` 构造（`:12601` `minimapHudSetFrame`），并带一个上次位姿缓存（`:12587-12589` 的 `reusePose`，配合 `:9855` 的 `minimapPoseValid` 与同处的 pose 缓存变量）。
- 结论：**它现在是每进程一个**。两窗同时画两个小地图需要把它改成按 pane，成本不小且收益可疑。
- 第一片约定（Q3 已裁决）：**只有焦点窗拥有小地图和 HUD 主体，非焦点窗只画世界和最小标识**。非拥有窗必须打印"未拥有"，而不是打印旧帧。`minimap.h:394-396` 的注释已经定下这个原则：没调用过 `setFrame` 的进程 "cannot print a minimap reading that a gate might mistake for evidence"。按窗沿用同一原则。
- 断言：任一帧 `count(ownsMinimap) <= 1`；且未拥有窗的 minimap 读数为 `unowned`（一个独立取值，不是 `valid=false` 的复用）。

---

## 4 回退与降级

### 4.0 硬约束（写进契约正文，不可协商）

> **不得伪造 POV demo 中不存在的数据。**
> 一段 POV demo 只描述被录制者本人的视点。其它玩家在该 demo 里的 origin 与 `m_angEyeAngles` **不是可依赖的相机数据**：它们可能缺失、可能是最后一次已知值、可能整段不变。因此当第二窗的目标是一个非录制玩家、且该 demo 类型为 POV 时，**禁止**用这些行去构造第一人称视角。允许的是：改用可证明存在的姿态（本体第三视角/自由视角），或如实降级（§4.3）。

这条约束不是文风要求，它有代码依据：`native/src/entity_model.cpp:206` 起的 `extractTransform` 会依次尝试 `m_vecOrigin`、`m_angEyeAngles`、`m_angEyeAngles[0]/[1]`、`m_angRotation` 四个来源并逐级回退；`hasAngles == false` 是一个**真实可观测状态**，不是异常。`native/src/main.cpp:638-643` 的 `placeSpectatorView` 正是这么判的：`:651` `if (!transform.hasOrigin || !transform.hasAngles) return false;`（索引守卫在 `:647`，调用点在 `:12478`）。

### 4.1 目标消失（slot 被复用 / 下标越界）

| 触发 | 判定 | 动作 | 读数 |
|---|---|---|---|
| `entityIndex` 超出 `[0, 2047]` | 直接 | 清空 target，`pinned=false` | `paneTargetFallback=index-out-of-range` |
| `serial` 变化（slot 复用） | 按 `EntityHandle` 取 entry index，serial 比对 | 清空 target | `paneTargetFallback=index-reused` |
| 该行不再是可观测玩家 | `slotObservable`（`native/include/spectate_target.h:72`，观战者永远不是目标） | 清空 target | `paneTargetFallback=not-observable` |

共同要求：**相机停在最后一个有效位姿，不跳到世界原点、不回到出厂轨道、不随机挑一个玩家**。这不是新规矩，`native/include/spectate_target.h:9-38` 第 5 条已经这么写了（"there is deliberately no fallback to the world origin, to the shipped orbit or to a random player"），N16 只是把它从"一个相机"推广到"每窗一个相机"。

### 4.2 目标死亡

复用现有 handover 语义，**按窗各跑一份**：

- `SpectateDirector` 是普通对象（`spectate_target.h:77`），天然可实例化两份。**不要**给它加 pane 参数——那会把"一个已有的、被门禁覆盖的类"变成两套语义。
- 死亡 → 导演句柄接手（若指到活人）→ 否则按 entity index 稳定挑选（`spectate_target.h:9-38` 第 2 条）→ 手动选择优先于两者。
- handover 用 **wall time** `kHandoverSeconds = 0.3`（`spectate_target.h:80`），暂停时仍推进。`update` 的 `nowSeconds` 必须是 `main.cpp:9879` 那个由 `QueryPerformanceCounter`（`:9871-9872`）算出的 wall 时钟，不是 demo tick。
- `pinned == true` 的窗**不参与 handover**：用户点了这个玩家，他死了就停在尸体视角（`hidesModel`，`main.cpp:10607` 与 `:11842`），并打印 `paneTargetFallback=pinned-dead`。理由是"我点了看这个人"比"系统替我换人"更符合意图；而 `Delete`（§5.3）就是退出口。
- 读数：每窗各自 `spectate: target=.. source=.. deaths=.. handovers=..`（`traceFields()`，`spectate_target.h:145`），**必须带 pane 前缀**，否则两个窗的读数在 trace 里无法区分——这是 N16 最容易犯的"读数看起来对、其实不可判"的错。
- 现有单例接线位置（对照用）：`pickManual` 在 `main.cpp:12444-12446`，`update` 在 `:12453`（传 `g_playback.speed`），`refreshDead` 在 `:10579`。每窗一份后这三处都必须按窗调用，且 `speed` 仍是同一个值。

### 4.3 无相机数据

三层，从便宜到贵：

1. `hasAngles == false` → 第一人称**不成立**。第一人称 + 无目标 = `HoldCurrent`（`native/include/native_ui.h:85-95` 的矩阵已定：first-person + no → HoldCurrent，"keep the camera; never teleport"）。**不**退到原点。
2. 有 origin 无 angles → 只允许第三人称/自由视角，并以 origin 为焦点。断言：`paneCameraFallback=angles-missing`。
3. 两者都没有 → 保留最后有效位姿；若从未有过有效位姿（窗刚建立），用 `native/include/camera_director.h:139-142` 的出厂值（`kDefaultCameraYaw=0.0f`、`kDefaultCameraPitch=0.36f`、`kDefaultCameraDistance=1.65f`、`kDefaultFocusZ=0.25f`，注释 `:136-138` 说明 `Renderer::resetCamera` 用同一组数）并打印 `poseSource=default`。**"默认值"与"上次有效值"必须是两个不同的读数**，否则验收无法区分"它正确地停住了"和"它从来就没动过"。

### 4.4 每种 demo 的可用目标范围

判据是 demo 类型，不是样本名：`summary.sourceTv`（`native/include/demo_header.h:978` `bool sourceTv = false`，赋值于 `native/src/demo_header_net.cpp:697`），录制者实体是 `summary.lastViewEntity`（`demo_header_net.cpp:647`）。

| demo 类型 | 判据 | 第一人称可用范围 | 第三/自由视角可用范围 | 契约要求 |
|---|---|---|---|---|
| **POV** | `summary.sourceTv == false` | **仅录制者本人**（`lastViewEntity` 指向的那个实体） | 全部可观测玩家（用 origin 做焦点即可，`hasAngles` 不参与） | 第二窗若指向非录制玩家并请求第一人称 → **拒绝并如实降级**（§4.3），不构造伪视角 |
| **STV** | `summary.sourceTv == true` | 所有活着的可观测玩家（导演句柄会指名） | 同上 | 无特殊限制；但"STV 录的是观察者"这一情形（`native/include/native_ui.h:97` 起 T80 注释：`svc_SetView` 指到录制者自己的 STV，eye 恒为 `(0,0,0)`，3312/3312 行如此）仍按无眼睛处理 |

**样本清单：待实测，本卡不写。** 现有 gate demo 与随附样本各自是 POV 还是 STV，必须由 `source-tv-coverage-check.sh`（已在 `verify-all.sh:655` 注册，`:672` 另有 `--mutation`）在**真实 demo 上**跑出来读，不在契约里猜一个数。理由是：一个猜错的数会被后续实现当成契约消费掉，而一次实跑的输出不会。

---

## 5 交互契约

### 5.1 谁能改什么

| 输入 | 现有位置 | 作用对象 | 拒绝条件 |
|---|---|---|---|
| 键盘热键 | `main.cpp:1261` WM_KEYDOWN；共享表 `:1284-1289` | `focused` 窗 | `shouldDispatchHotkey(textFieldFocused, keyIsCharacter)` 为 false（`native/include/keybinds.h:73`）；文本框有焦点时计数 `noteSuppressedByFocus()`，trace `hud: keys suppressed focus=1 key=%u`（`main.cpp:1287-1289`） |
| 右键拖拽 | `main.cpp:1046-1050` 起，`:1055-1063` 移动 | `focused` 窗 | 光标不在任何 pane 内也允许（拖拽开始时的焦点才是权威，拖到窗外不该中断） |
| 滚轮 | `main.cpp:1064-1066` | `focused` 窗 | 同上 |
| 左键单击 | `main.cpp:1040-1045` | 先改焦点，再选玩家 | 命中 logs 面板分隔条时**优先给分隔条**（`native/src/logstf_panel.cpp:280` 的 `logsPanelMouse`，现在就在 `main.cpp:1041` 被**第一个**调用；`:276-280` 的注释说明它是父客户区上的**命中测试**，不是子窗口） |

**优先级必须写死为一条有序表**，否则两个都"先看"的命中测试会互相吞掉消息：

    1. 分隔条 logsPanelMouse(WM_LBUTTONDOWN)   —— 现在就是这个位置，保持不动
    2. pane 命中：pane = 包含 (x,y) 的那个 PaneViewport
    3. 世界拾取：在 pane 内做玩家拾取（§5.2）
    4. 都不命中 -> DefWindowProcW

（原第 2 项"分隔线拖拽"已被 Q1 裁决移除：本轮只做等分，不做拖拽，因此不存在可拖的分隔线。）

### 5.2 选择一个玩家（鼠标）

- **不要新写世界射线求交**：产品里没有可用的 world-pick helper。`--world-face-trace-points`（`main.cpp:2207` 的开关、`:2618-2646` 的解析、`:8642`/`:8749-8753` 的实现）只 trace 面，不选实体。
- **用已有的屏上读数**：`native/src/native_renderer_frame.cpp:858-885` 每个被画出的实例都发布一个 `OverlayItemReading`，其注释明说该框 "is the projection of the instance's own world-space extent through the SAME mvp the shader was handed"——即它与帧上的像素同源，不存在"读数与画面各算一遍"的偏差。字段含 `entityIndex`、`team`、`depthMode` 与屏幕范围。
- 约定：拾取 = 在 **pane 局部像素** 内，按 `depthMode` 从近到远，取第一个包含点的 `OverlayItemReading`，且其 `team != 1`。命中 → `target.entityIndex = reading.entityIndex`、`pinned = true`、`source = Manual`。
- 未命中 → **不改目标**（点空地不等于清空；清空是 §5.3 的显式动作）。
- 读数：`pane: pick pane=A hit=1 entity=7 depth=..` / `hit=0`。

### 5.3 删除一个玩家

"删除"是**从窗格解除绑定**，不是杀玩家、不是改播放：

- `Delete` 键（`VK_DELETE`，`native/src/keybinds.cpp:36` 有名字映射，但 `defaultKeyBindings()`（`:392-403`）里**没有绑定**，是空位）：清 `target`、`pinned=false`、`source=None`，相机按 §4.3 第 3 层保持。
- 必须可绑定：按 `native/include/keybinds.h:155-163` `namespace camera_actions` 的命名惯例，新动作 id 建议 `pane.clearTarget` / `pane.activateNext` / `pane.cycleTargetNext`；且**必须注册进 `defaultKeyBindings()` 而不是散在 `main.cpp`**——`main.cpp:958-968` 已经为 `hud.*` 记过这个教训（`:965` 先取 `defaultKeyBindings()`，`:966-968` 才追加三个 hud 动作；`:958-964` 的注释说明第二个只含部分动作的表会通过"hud actions 存在"的检查而丢掉全部相机绑定）。
- 断言：`Delete` 前后 `g_playback` 逐字段不变（尤其 `tick`/`paused`/`speed`）。

### 5.4 激活窗格与切换视角

- 激活：左键在 pane 内按下 → `focused`；`Tab` → `pane.activateNext` 在 A/B 间切换（`VK_TAB` 在 `keybinds.cpp:34` 有名字、无默认绑定，同样空位）。
- 每窗视角切换：沿用现有 `CameraModeController`（`native/include/native_ui.h:44`）：`SPACE` 逐级循环（`:62` `spacePress(nowMs)`）、`kDoubleClickWindowMs = 300`（`:48`）内第二次按直接跳 `FreeLook`、无 observe 目标时按下不移动相机（`native_ui.h:19-39` 头注释 + `:95` `decideCameraSceneAction`）。**每窗一个 controller 实例**，`spacePress(nowMs)` 的时钟仍是同一个 wall 时钟。
- 注意 `WM_KEYDOWN` 的重复抑制表（`main.cpp:1267-1270`）现在只挡 `VK_SPACE`、`'R'`、`'P'`、`'M'`、`VK_HOME`、`VK_END`、`VK_F12`、`VK_F1..VK_F8`。`Tab` 与 `Delete` 若做成**切换类**动作，必须加入这张表，否则按住不放会连翻多窗。
- 自由视角停位姿：每窗一份 `CameraPose`，沿用 `poseEqual` 的**精确比较**（`native/include/camera_director.h:46-49`，注释说得很清楚：刻意不用 epsilon，因为 settings 往返是逐位一致的）。验收里"另一窗没变"因此可以是**逐位相等**断言，不需要容差——这是这条契约能"机器可判"的关键。

### 5.5 输入路由的总不变量

> 一帧之内，任何输入事件最多改变**一个** pane 的相机状态。

断言：对任意输入序列，比较操作前后两窗的 `(pose.focusX, focusY, focusZ, yaw, pitch, distance, mode, fovDegrees)` 八元组，变化的不超过一个窗。

---

## 6 验收清单（机器可判）

判定约定（沿用 `docs/GATE-DESIGN-TRAPS.md` 的四个陷阱）：

- 读数**不存在**时必须输出 `NOT-VERIFIED`，**不得** PASS 也不得 FAIL（陷阱 1：对象在成功路径上不存在）。注意这是一条**新约定**：`spectate-check.sh` 里有 `BINARY-FRESH=1`/`STALE-BINARY=1`（`:186`/`:188`），但 grep `NOT-VERIFIED` 在该脚本中**零命中**，所以不能声称"沿用既有做法"，必须在本轮门禁里新写。
- "有该行但值为空" 与 "没有该行" 必须分开断言（陷阱 3）。
- 断言的事实来自独立读数，不来自工具的输出格式（陷阱 4：例如 MSYS `sha256sum` 打印 `<hash> *<path>`）。
- 门禁必须在**红基线**上先证明自己会红（陷阱 2），否则输出 `MUTATION-SUITE=REFUSED`。

每一条都必须落到"退出码"或"数值阈值"，不接受"看起来正确"。表中 `F` = 需要产品帧捕获；`O` = 完全离线可判（trace / metrics / 探针读数）。

| # | 断言 | 判定形式（阈值 / 退出码） | 证据 | F/O |
|---|---|---|---|---|
| A1 | 两窗同一帧渲染同一 tick | 整段运行中 `renderedTick(A) != renderedTick(B)` 的帧数 `== 0` | 每窗 `draw: pane=.. tick=..` | O |
| A2 | 时间轴请求一帧最多落地一个 | `PlaybackResult.id` 严格递增且每帧增量 `<= 1`；`playback:` 落地行每帧 `<= 1` | `native/include/playback_control.h:100` 的 `id` | O |
| A3 | 暂停/变速时两窗同步 | 暂停段内 `tickDelta == 0` 且 `pauseViolations == 0`；2× 段内每帧两窗 `tickDelta` 相等 | `camera_director.h:82` `pauseViolations` + `FrameLanding.frameDelta` | O |
| A4 | seek 后两窗落到同一帧 | `resolvedSceneTick(A) == resolvedSceneTick(B)` 且 `frameStatus` 相同，失败计数 `== 0` | `FrameLanding`（`playback_control.h:116-131`，读取 `:220` `lastFrame()`） | O |
| A5 | **真实不同玩家** | 同帧 `targetEntityIndex(A) != targetEntityIndex(B)` 且两者均 `>= 0`；两窗 `OverlayItemReading.entityIndex` 集合**交集非空**（同一场景），且两窗的居中实体不同 | `native_renderer_frame.cpp:858-885` | O |
| A6 | **不是"两个框"** | 同一捕获帧内 A/B 区域逐像素平均绝对差 `> T`，且两区域不做逐像素全等判定（`equal == false`） | 帧捕获 + 逐像素比较 | **F** |
| A7 | **模式独立** | A 第一人称、B 第三人称时 `mode=` 两者不等，且 `orbitPose()` 八元组不等；退出码非 0 即失败 | `native_renderer.h:1046` `orbitPose()` | O |
| A8 | **输入独立** | 对 B 发一次 `orbit`：A 的八元组**逐位**不变（`poseEqual` 精确比较，`camera_director.h:49`），B 变化；不符则 `exit 1` | trace | O |
| A9 | **焦点不被悬停偷走** | 在 A 内注入 `WM_MOUSEMOVE` 序列后 `focus==A`；`focusChangeByHover` 计数 `== 0` | `pane: focus=..` | O |
| A10 | **resize 隔离** | `WM_SIZE` 后 `abs(A.width + B.width - clientWidth) <= 1` 且 `A.width >= B.width`；两窗 `hudLastRegion()` 均不越过自己的 rect（越界计数 `== 0`） | `main_hud.cpp:289` + `hud: draw region=` | O |
| A11 | **深度/裁剪隔离** | 同一相机下单窗与双窗该相机的 `worldVisDrawnTriangles` **差值 == 0**；每窗 HUD pass 后 `hudScissorApplied() == true` 且已恢复全屏 | `native_renderer.h:606-613` | O |
| A12 | **裁剪不留痕** | 相机相同 ⇒ 两窗三角数相等；相机不同 ⇒ 允许不等但 `worldVisCameraCluster` 必须不同（不等却同 cluster 即失败）；`vis_reuse` 出现在 trace 时必须有对应 `cluster` 值 | `worldVisCameraCluster` + `worldVisClipReason` | O |
| A13 | **HUD 隔离** | 非焦点窗 `hudDrawnRects() == 0`，且其区域像素与"A/B 都关 HUD"的对照帧**逐位相同**（差异像素数 `== 0`） | `hudDrawnRects()` + 帧捕获 | **F** |
| A14 | **小地图互斥且跟焦点** | 任一帧 `count(ownsMinimap) <= 1` 且拥有者 `== focus`；未拥有窗打印 `unowned`（该取值出现次数 `> 0`）而非旧帧 | `minimap.h:394-396` 原则 | O |
| A15 | **无目标不伪造**（负向对照，必须先红） | 把 B 的 target 设成不存在的 entityIndex → B 打印 `index-out-of-range` 且 B 区域与设之前帧**逐位相同**（差异像素 `== 0`）；若该负向对照在红基线上不产生失败 → `MUTATION-SUITE=REFUSED` | trace + 帧捕获 | **F** |
| A16 | **POV 不伪造**（负向对照） | POV demo 下 B 指向非录制玩家并请求第一人称 → trace 必须出现 `angles-missing` 或 `pov-not-recorded`，且 B 的 `camera: mode=` **不等于** FirstPerson。**没有 POV 样本时输出 `NOT-VERIFIED` 并 `exit 1`**（不通过） | trace | O |
| A17 | 二进制新鲜度 | 门禁自检 `BINARY-FRESH=1`，否则 `STALE-BINARY=1` 并失败 | 沿用 `spectate-check.sh:186`/`:188` | O |
| A18 | **双窗帧时 / 单窗帧时 <= 1.7**（**交付门槛**，不是降级触发器） | 同一 demo、同一 tick 区间、同一相机集合下的 p95 帧时比值 `<= 1.7` 才算这一片可交付；超了如实报出并判不合格，不得用降级掩盖。**当前产品无法测量这个比值**：`native/` 下 `ID3D11Query` / `TIMESTAMP` / `QueryVideoMemoryInfo` / `DXGI_QUERY_VIDEO_MEMORY_INFO` 命中全 0，故本项现为 `NOT-VERIFIED（无测量能力）`，且**修 A18 的第一步是新增测量能力，不是降级** | §7.1/§7.2 的 telemetry，§7.5.1 | O（须先实现测量） |

**A6 的 `T` 怎么定（不许拍脑袋）**：跑两次**同相机双窗**（A、B 盯同一玩家、同一模式），逐像素比较两窗区域，取该差值的 p99 作为噪声底；再跑真实双玩家，要求差值 `> 噪声底 × 3`。这样 `T` 是从测量里长出来的，不是发明出来的。

**"不是两个框"的可判定形式**，一句话：**A5 + A6 + A8 三条同时成立**——不同实体（结构）、像素不同（画面）、互不影响（独立性）。任一为 `NOT-VERIFIED` 则整条不通过。

### 6.1 需要新增的 gate 脚本（本轮不写）

- `split-view-check.sh`：A1–A4、A7–A12、A14、A16–A18，全部走 trace，负向对照先行（A18 在 §7.1/§7.2 的计时读数落地前只能输出 `NOT-VERIFIED（无测量能力）`，不得当作 PASS）。
- `split-view-frame-check.sh`：A6、A13、A15（要帧捕获与逐像素比较）。
- `split-view-pov-refusal-check.sh`：A16 的 POV 分支，必须能在**没有 POV 样本**时输出 `NOT-VERIFIED` 并失败。

### 6.2 需要新增的 camera-script 步骤

现有命令表（`native/src/camera_script.cpp`，每条一个 `if (step.command == "...")`）里**没有任何 pane 命令**。第一片最小集合：

| 命令 | 语义 | 断言用 |
|---|---|---|
| `pane <A|B>` | 设定后续命令的作用窗 | 后续所有相机的读数归属 |
| `pane-target <A|B> <index>` | 等价于 §5.2 的选择，可直接注入 | A5、A15、A16 |
| `pane-clear <A|B>` | §5.3 的 `Delete` | A8（清目标不该动相机） |
| `pane-mode <A|B> <mode>` | 三值 | A7 |
| `pane-focus <A|B>` | 绕过鼠标直接设焦点 | A9 |
| `expect-panes <n>` | 断言启用窗数，不符即失败 | 全部 |

必须满足现有约定：`CameraScriptStep{command, args, text}`（`native/src/camera_script.h:35-41`），失败计入 `g_cameraScriptStepsFailed`，退出码走 `g_cameraScriptExitCode`；步进由 `main.cpp:12613` 按 `kTargetFrameSeconds` 驱动。

---

## 7 容量与成本估算方法

**本节不写任何数字。** 理由是：`native/` 下对 `ID3D11Query` / `TIMESTAMP` / `QueryVideoMemoryInfo` / `DXGI_QUERY_VIDEO_MEMORY_INFO` 的 grep 命中为 **0**。今天产品没有任何 GPU 计时或显存统计，现有 `--metrics-file`（列头在 `main.cpp:9440`）只有 `elapsed_seconds,rendered_frames,fps,tick,working_set_bytes,private_bytes,main_loop_iterations,ui_update_calls,title_update_calls,metrics_write_calls,wait_calls,wait_ms_total`。**在这里填任何"双窗大约 1.6×"都是发明数字**，禁止。

### 7.1 测量方案（必须先实现，再谈容量）

1. **CPU 帧时**：已有 QPC 包围（`main.cpp:9871-9872` 取 `now`，`renderer.draw` 在 `:12699`）。把 `(draw_start - frame_start)` 记为 `frame_cpu_ms`、`(after_draw - draw_start)` 记为 `draw_call_ms`。两者都是现成的时钟区间，不需要新依赖。
2. **GPU 帧时**：需新增。方案：`ID3D11Query` 的 `D3D11_QUERY_TIMESTAMP_DISJOINT` + 两枚 `D3D11_QUERY_TIMESTAMP`，帧首帧尾各一次，`GetData` 用"上一帧的结果"（异步，不阻塞）。**这是新增能力，契约只登记需求，不假装它已存在。**
3. **显存**：需新增。`IDXGIAdapter3::QueryVideoMemoryInfo(DXGI_MEMORY_SEGMENT_GROUP_LOCAL, ...)` 的 `CurrentUsage`/`Budget`。设备已在 `native/include/native_renderer.h:1639` `ComPtr<ID3D11Device> device_`，取 adapter 需 `QueryInterface`。
4. **draw / instance 峰值**：`native/include/entity_model.h:73` `kMaxInstances = 2048` 是硬上限；世界绘制计数已由 `main.cpp:8160` 的 `traceWorldDraw` 打印（`gpu: world geometry budget capacity=.. requested=.. dropped=.. drawTriangles=..` 等），但**只打一次**（首帧，`:12711` 调用）。需要改成每帧累积 max 并进 metrics。
5. **协议**：同一 demo、同一 tick 区间、同一相机集合；`N = 3` 次运行；丢弃前 2 s 预热；报 `p50/p95` 而非均值（长尾才是卡顿）；单窗基线与双窗用**完全相同的相机**，否则比较的是两个场景。

### 7.2 必须新增的 telemetry 字段

    frame_cpu_ms, frame_gpu_ms, draw_call_ms, present_ms,
    vram_used_bytes, vram_budget_bytes,
    draw_calls_world, draw_calls_entities, draw_calls_hud,
    instances_peak, triangles_world,
    pane_a_triangles, pane_b_triangles, vis_rebuilds, vis_reuse,

`vis_rebuilds` / `vis_reuse` 是 N16 特有的：它直接量化 §3.3 那个"两窗同 cluster 就几乎免费"的判断，没有它就无法解释双窗帧时为什么有时接近单窗。

### 7.3 成本模型应该长成什么形状（用测得量表达）

不是"1.6×"，而是：

    frame(cost) = decode(cost, 共享) + Σ_pane [ vis_clip(cost) + world_draw(cost) + entity_submit(cost) + hud(cost) ]
    其中 vis_clip 在 vis_reuse=1 时为 0；decode 不随窗数变化。

### 7.4 代价控制手段（第一片只允许不改变「两窗都真实渲染」这一前提的那些）

**第一片禁止渲染降级**（Q5，见 §7.5.1）。下表按「是否改变两窗都真实渲染的前提」分栏，标「否」的不进第一片。

| 手段 | 第一片 | 说明 |
|---|---|---|
| 两窗同 cluster 时复用 vis list（§3.3） | 是 | 不减少任一窗的绘制，只省重复的裁剪计算 |
| 非焦点窗不画 HUD 主体（§1.5 / §3.4） | 是 | 这是 Q3 的归属决定，不是降级；两窗的世界照画 |
| 非焦点窗降分辨率渲染 | **否** | 改变「真实渲染」的前提，且需离屏 RT |
| 非焦点窗的实体预算减半 | **否** | 同上；且 `kMaxInstances`（`native/include/entity_model.h:73`）逐窗还是共享必须先实测——共享会让焦点窗把预算吃光，副窗没人 |
| 单窗兜底（`SplitViewState::enabled = false`） | 是 | 只是「不启用分屏」，与今天逐位一致；不是「启用后偷偷降级」 |

### 7.5 代价天花板 = **交付门槛**（不是降级开关）

> 若 §7 的实测显示**双窗帧时 > 单窗帧时的 1.7 倍**，则**这一片不合格**：如实报出该倍率，不得用降级掩盖，先修的是帧时本身。

这是**交付门槛**，不是「性能不够就偷偷降级」的开关（Q5，§7.5.1）。它必须在测量**之前**写进实现的任务书（本契约即为其登记处），测完才引用。否则任何倍率都可以被事后解释成「在预期内」，§7 的全部工作就失去判定力。同时记录：本轮**不**引入按帧降频调度——它会与 A1（两窗同帧同 tick）耦合，`kTargetFrameSeconds = 1.0/120.0`（`native/src/main.cpp:732`）保持不变。对应的验收项是 §6 的 **A18**。

#### 7.5.1 已裁决：第一片不做降级（Q5）

**裁决：分屏第一片不做渲染降级。**

理由：降级机制（非焦点窗复用上一帧）与 §6 A1 的 `renderedTick` 相等断言**在逻辑上互斥**——降级一启用就必然让非焦点窗的 `renderedTick` 落后，A1 会立刻自己打穿；而 A1 是我们唯一能自动判定的「两窗同帧」证据。

所以第一片的边界是：**要么两窗都真实渲染且 tick 相等，要么这一片不交付**。不做「性能不够就偷偷降级」这种会让验收自相矛盾的设计。

直接后果三条，都必须照办：

1. **1.7× 是交付门槛，不是降级开关**：超了就是这一片不合格，如实报出（A18），不许用降级掩盖。
2. **§7.4 里真正的渲染降级项（降分辨率、实体预算减半）不进第一片**；只保留不改变「两窗都真实渲染」前提的手段（vis list 复用；非焦点窗不画 HUD 主体本就是 Q3 的归属决定）。
3. **将来若要改这个决定，那是契约变更，不是实现细节**：必须同时改 A1 的断言口径——引入 `drawnTick`，并明确 A1 断言的是 `renderedTick` 而非「绘制时刻」，否则 A1 会被静默改写含义。

---

## 8 需要的代码改动（本卡**不**实施，只登记）

**本卡不实施。** 下表只是把"契约要成立必须有人动哪里"登记清楚，供派工时逐个认领；本卡没有、也不会改动其中任何一行。

「需要谁批准」列的字母沿用派工表口径（`docs/PROJECT-NEXT-TASKS-2026-11-14.md:360-372` 责任人列、`:251`「D 牵头，C/F 审视图资源」）：**D = N16 属主**，**C = 渲染器/视图资源审阅人**，**F = HUD 与 UI 属主**。仓库里没有一张写下来的 A–G 角色表（对 `docs/` grep `[A-G] ?线` 除本文外零命中），所以本文不发明新字母，只用能与上述两处对上的三个角色；第 22 行按 `docs/EXEC-PLAN-2026-11-15.md:43` 的写域表直接点名到执行者。

| # | 文件 | 行区 | 为什么避不开 | 需要谁批准 |
|---|---|---|---|---|
| 1 | 新 `native/include/split_view_state.h` | 新文件 | §1 的结构无处安放；放进 `main_camera.h` 会碰禁区 | D（N16 属主） |
| 2 | `native/include/native_renderer.h` | `:1055` `bool draw(float,float,float)` | 单视口签名无法表达两个 pane；需携带 viewport + 相机 | C（渲染器资源）+ D（N16 属主） |
| 3 | `native/src/native_renderer_frame.cpp` | `:36-41` | 唯一的 `RSSetViewports(1,&viewport)`，必须逐 pane | C（渲染器资源） |
| 4 | `native/src/native_renderer_frame.cpp` | `:76-176`（`WorldConstantsData` 构造与上传） | 相机常量每帧一份（`static_assert(sizeof==192)` 在 `:95-96`），双窗需两次上传 | C（渲染器资源） |
| 5 | `native/src/native_renderer_frame.cpp` | `:149`、`:173` | aspect / 投影矩阵今天用 `width_/height_`，必须改用**窗格**尺寸 | C（渲染器资源） |
| 6 | `native/src/native_renderer_frame.cpp` | `:1424-1466` | HUD pass 的 scissor 需按 pane，且恢复逻辑必须仍保证全屏 | C（渲染器资源）+ F（HUD 与 UI） |
| 7 | `native/include/native_renderer.h` | `:2336` `hudPrimitives_`、`:1245` `setHudPrimitives` | 单容器无法按窗过滤 | F（HUD 与 UI） |
| 8 | `native/src/main_hud.cpp` | `:289-345` `hudApplyRects` | `layoutFor` 用整客户区，双窗会跨过分隔线；DPI 仍按窗口取 | F（HUD 与 UI） |
| 9 | `native/src/main_hud.cpp` + `native/include/minimap.h` | `main_hud.cpp:46-57`；`minimap.h:381-401` | 小地图是进程级单例；按 Q3 只允许焦点窗拥有，需要"未拥有"读数 | F（HUD 与 UI） |
| 10 | `native/src/main.cpp` | `:1046-1066`（右键/滚轮） | 现在直接作用于唯一的 renderer，需路由到 `focused` 的 pane | D（N16 属主） |
| 11 | `native/src/main.cpp` | `:1040-1045`（左键） | pane 命中测试必须插在 `logsPanelMouse` 之后 | D（N16 属主） |
| 12 | `native/src/main.cpp` | `:697` `g_spectateDirector` | 需两份实例；**不要**给类加 pane 参数 | D（N16 属主） |
| 13 | `native/src/main.cpp` | `:12444-12453`、`:10579`、`:10607`、`:11842` | `pickManual`/`update`/`refreshDead`/`hidesModel` 逐窗调用，且每窗传同一个 `g_playback.speed` | D（N16 属主） |
| 14 | `native/src/main.cpp` | `:12575-12605` | 小地图帧按拥有窗构造，未拥有窗不得推旧帧 | D（N16 属主）+ F（HUD 与 UI） |
| 15 | `native/src/main.cpp` | `:1007-1037`（`WM_SIZE`/`WM_DPICHANGED`） | 尺寸变化后必须重算两窗 rect 与 `hudApplyRects` | D（N16 属主） |
| 16 | `native/src/main.cpp` | `:1267-1270` | 重复抑制表要加 `Tab`/`Delete`，否则按住会连翻多窗 | D（N16 属主） |
| 17 | `native/src/keybinds.cpp` | `:392-403` `defaultKeyBindings()` | `pane.*` 必须注册进这张表（`main.cpp:958-968` 的 hud 教训） | D（N16 属主） |
| 18 | `native/src/camera_script.cpp` | 现有命令分发处 | §6.2 的六个命令；不改既有命令语义 | D（N16 属主） |
| 19 | `native/src/main.cpp` | `:8106-8155` `applyWorldVisClip` | vis clip 需逐 pane 重建 + 复用判定，并打印 `vis_rebuilds`/`vis_reuse` | C（渲染器资源）+ D（N16 属主） |
| 20 | `native/src/main.cpp` | `:9440` metrics 列头 | 只追加 §7.2 的列，不改既有列序（历史门禁按列解析） | D（N16 属主） |
| 21 | `native/src/main.cpp` | `:12699` 附近的帧计时 | §7.1 的 CPU 区间；GPU 计时需新建 query 对象 | D（N16 属主）+ C（渲染器资源） |
| 22 | **`native/src/main_camera.cpp` / `native/include/main_camera.h`** | `main_camera.cpp:43-57` `syncDirectorPoseToRenderer` | 导演与渲染器的唯一接合点，必须能按 pane 应用 | **n12-camera**：`docs/EXEC-PLAN-2026-11-15.md:43` 写域表独占该两文件，本轮禁改，由 n12-camera 承接，不在本卡范围 |

---

## 9 PIP 与四窗（评估，不在本轮）

- **PIP 的真实成本不在"多画一次"，在深度与遮挡**：等分双窗的矩形互不相交，一次全屏深度清除就够（§3.2）；PIP 的矩形相交，必须换成"每窗独立深度目标 + 合成"，或"先母窗后小窗带自己的清除"。这是**架构级**差别，也是它必须单独立卡的原因。
- **四窗**：屏幕矩形两两不相交，深度方案与双窗同构；真正的约束是 §7 的预算（四个相机 ⇒ 最多四次 vis 重建、四次世界绘制）。在 §7.2 的字段落地之前，评估四窗的任何数字都不成立。
- **本轮不同时做三套布局**。`docs/PROJECT-NEXT-TASKS-2026-11-14.md:370` 的派工口径就是"先契约后共享渲染器，不同时做四窗"。
- **布局可变性（Q1 已裁决）**：第一片**只做等分，不做拖拽**。理由是可判定性——等分的正确性是一条可机械检查的不等式（`abs(A.width + B.width - clientWidth) <= 1` 且 `A.width >= B.width`），而拖拽要求"在任意分割比例下都正确"，验收对象从一条不等式变成一整条连续区间；连带 `hudApplyRects` 每帧重算与 §7 成本模型都要按可变 rect 重做。若后续要开拖拽，那是另一张卡，且必须先补"每个分割比例下的 rect 一致性"断言。

---

## 10 与既有文档的一致性

| 文档 | 要求 | 本文如何满足 |
|---|---|---|
| `docs/PROJECT-ROADMAP-HANDOFF-2026-11-12.md:731` | "Every pane keeps its own player target, camera mode and free-look pose while playback time and decoded demo state remain shared" | §1 + §2 逐字段落地 |
| `docs/PROJECT-ROADMAP-HANDOFF-2026-11-12.md:739` (R8-3) | "Render one shared scene snapshot through N camera views using per-view camera constants, viewport/scissor rectangles and independent depth clears; do not decode/seek the demo once per pane" | §2.1 共享解码 + §3.1/3.2 |
| `docs/PROJECT-ROADMAP-HANDOFF-2026-11-12.md:743` | "accepted only when switching one pane leaves every other pane's target and camera state unchanged" | §5.5 不变量 + A8 断言（逐位） |
| `docs/PROJECT-NEXT-TASKS-2026-11-14.md:253-258` | N16 的六条勾选 | §1/§2/§4/§5/§6/§7 一一对应；`:258`（PIP/四窗）见 §9 |
| `docs/PROJECT-NEXT-TASKS-2026-11-14.md:260` | "不仅截屏有两个框，而是真实不同玩家、独立模式/输入、共享时间同步" | §6 A5+A6+A8 三条合取，并给出可判定的形式 |
| `docs/T85-COVERAGE-MANIFEST.md:98` | N16 与 Source 渲染器的像素一致性"明确不承诺" | 本文的像素断言只做**窗格之间**的比较（A6/A13/A15），从不对 Source 做比较 |
| `docs/DEFECT-REGISTER-MASTER-2026-11-14.md:49` (D21) | 多视图未实现，PIP/四视图为后续里程碑 | §9 |
| `docs/GATE-DESIGN-TRAPS.md` | 读数不存在 ≠ PASS | §6 开头的判定约定 |
| `docs/N-STATUS-2026-11-14.md:24` | N16 状态"未开始 / 未实现" | 本文不改变该状态（见 §12） |

---

## 11 裁决记录（已由牵头人定死，不再是开放问题）

| # | 问题 | 裁决 | 落在本文哪里 |
|---|---|---|---|
| Q1 | 分割线是否可拖拽 | **第一片不做拖拽，只做等分**。等分的正确性是一条不等式，拖拽会把验收变成"任意分割比例下都正确"，连带 `hudApplyRects` 与 §7 成本模型都要重做 | §1.1 唯一算法、§5.1 有序表移除拖拽项、§9 末段 |
| Q2 | 非焦点窗是否继续渲染 | **两窗同帧渲染，先量代价**。`kTargetFrameSeconds = 1.0/120.0` 不变，本轮不引入按帧降频（会与 A1 的"两窗同 tick"耦合）。代价以 §7.5 的**预先登记门槛**约束：双窗帧时 > 单窗 1.7× 即这一片不合格（A18），**不降级**（Q5） | §7.5、§2.1 |
| Q3 | `ownsMinimap` 跟焦点还是固定 | **跟焦点窗**。写死一句：只有焦点窗拥有小地图和 HUD 主体，非焦点窗只画世界和最小标识 | §1.5、§3.4 末段、§3.5、A14 |
| Q4 | gate demo 的 `sourceTv` 实测值 | **本轮不填**。样本清单须由 `source-tv-coverage-check.sh`（`verify-all.sh:655`）在真实 demo 上产出，本卡不臆测 | §4.4 末段 |
| Q5 | 双窗帧时超 1.7× 时是否降级 | **不降级**。降级（非焦点窗复用上一帧）与 A1 的 `renderedTick` 相等断言逻辑互斥，一启用就自己打穿验收；第一片边界是「要么两窗都真实渲染且 tick 相等，要么这一片不交付」，1.7× 因此是**交付门槛**而非降级开关 | §7.4、§7.5、§7.5.1、A18、§12 |

---

## 12 这份契约没证明什么

本文是一份**设计契约**，不是实现，不是实验报告，也不是验收证据。

- **不含任何实现**：§1 的结构体、§6.2 的命令、§7.2 的字段都是"应当长这样"，没有一行被编译过。
- **不含任何实测**：§7 全节没有一个数字，因为今天产品里没有任何 GPU 计时或显存读数（`native/` 下 `ID3D11Query`/`TIMESTAMP`/`QueryVideoMemoryInfo` 命中为 0）。任何"双窗约 N 倍"的说法若引用了本文，都是对本文的误读。
- **不含任何产品改动**：本卡没有修改 `native/` 下任何文件，没有新增构建目标，没有运行产品，没有改动 `evidence/`。
- **排除了「用降级换性能」的路线**：第一片不做渲染降级（Q5，§7.5.1）。若将来实测双窗帧时超过单窗 1.7×（A18），本文的答案是**这一片不交付**，而不是「加降级」。产品方若要改这个决定，必须同时改 A1 的断言口径（引入 `drawnTick` 并明确 A1 断言的是 `renderedTick` 而非绘制时刻），那是**契约变更**，不是实现细节。
- **不构成 N16 通过**：`docs/N-STATUS-2026-11-14.md:24` 的"未开始 / 未实现"在本卡之后**依然成立**。§6 的 A1–A18 在没有对应门禁脚本与实测跑动之前，全部处于 `NOT-VERIFIED`。
- **不承诺与 Source 渲染器像素一致**（`docs/T85-COVERAGE-MANIFEST.md:98`）：本文所有像素断言都是窗格与窗格之间、帧与帧之间的自比较。