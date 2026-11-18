# 相机与多视角：缺口盘点（2026-11-18）

只读盘点，基准是 `work/integrate` 的相机与分屏代码。目的不是列全所有代码，
而是把「已经能用」「已知限制」「真缺陷」分开，并给每一项一条能跑的验收命令。
设计意图见 `docs/N16-SPLITVIEW-CONTRACT-2026-11-16.md`（其中 part 已实现、
part 只有契约没有代码，本文标出）。

## 1. 现在能用的

| 能力 | 入口与证据 |
| --- | --- |
| 三档相机模式 | `CameraMode{FirstPerson,ThirdPerson,FreeLook}` `native/include/native_ui.h:40`；SPACE 循环、双击进 free-look `native_ui.h:44-83` |
| 每 tick 相机决策是纯函数 | `decideCameraSceneAction` → SetDemoView/SetOrbitFocus/FreezeCurrent/HoldCurrent `native_ui.h:93-95`，执行点 `native/src/main.cpp:12595-12622` |
| STV/follow | `SpectateDirector` `native/include/spectate_target.h:77-150`；`--follow-target/--follow-action(select\|next\|prev\|unfollow\|back-to-pov)` `main.cpp:1563-1606`，trace `follow:` `main.cpp:1840` |
| 分屏（2 pane，左右等分） | `kPaneMaxPanes=2` `native/include/pane_boundary.h:57`；每 pane 独立相机槽 `PaneCameraState` `pane_boundary.h:67-77`；pane1 每帧按 mode 取 follow/focus/free-look `main.cpp:15308-15397` |
| 每 pane 独立 aspect | 世界投影用 pane 宽高而非窗口 `native/src/native_renderer_frame.cpp:463-466`、`:500` |
| 同屏不同相机 | split 下两 pane 可各自 POV / 第三人称 / free-look + 各自 target（`main.cpp:15308-15397`） |
| 相机自证 | `pane:` `main.cpp:15699`、`panes:` `main.cpp:15722`（mismatches 计数）、`capture: fov` `native_renderer_frame.cpp:495-499` |

## 2. 缺口（按画面影响排序）

### C1（缺陷，最高）split 帧完全不画 ViewModel

`if (paneCount == 1u) drawViewModel();` `native_renderer_frame.cpp:1731`。
代码注释与 `docs/FOUR-CARD-REPORT-2026-11-16.md:258` 都写明这是**有意的 defer**：
VM pass 用整窗 aspect 建投影、并清整张深度面，当年判定「在 split 画需要先有
per-pane 投影 FOV/aspect 与每 pane viewmodel pass 的资源账」。

现在这两条前置有一半已经具备：VM pass 用的是**当前 pane 的** `frameView_`
（`native_renderer_viewmodel.cpp:564`，每 pane 在 `native_renderer_frame.cpp:440` 重写），
视口也是当前 pane 的（`native_renderer_frame.cpp:218-224`，VM 自己不改视口）。
真正缺的只有两件：**投影 aspect 仍取 `width_/height_`**（`native_renderer_viewmodel.cpp:565`），
以及**「哪些 pane 该画武器」的产品判断**（建议：只有 `PaneCameraSource::DemoView`
的 pane 画，orbit/free-look pane 不画——武器不属于那种视角）。

验收：同 tick 跑 split 与非 split 两帧，split 帧里 pane0 应出现武器且不被拉伸。

### C2 world FOV 只能改 settings.ini

世界 FOV 只有 `settings_.worldFov`（`native_renderer_frame.cpp:490`），CLI 只有
`--viewmodel-fov`（`main.cpp:4509-4520`）；worldFov 入口是 settings.ini
`[renderer] worldFov`（`native/src/settings_store.cpp:108`，回读 `main.cpp:2162`），
clamp 40..120 复用 `native/include/native_renderer.h:1492`。

注意 `native/include/world_fov.h:7-17` 的语义：**垂直** FOV 恒定、**水平** FOV 随
aspect 变（0.75 = 4:3 参照）。所以两个 pane 现在已经有不同的水平 FOV，缺的是
「给某个 pane 单独的缩放」。

验收：`--world-fov 75` 跑一帧，`capture: fov` 行应打印 horizontal=75、vertical=59.8404
（`native/tools/camera_probe.cpp:525-530` 有 pin）。

### C3 follow 只在 pane0 上平权

`--follow-action` 只推 pane0 的 `g_spectateDirector`（`main.cpp:1584-1606`）；pane1 只有
`--pane-follow`(select)（`main.cpp:4171-4184`），没有 next/prev/unfollow；
`--camera-script` 也没有 follow/spectate/pane 命令（`native/src/camera_script.cpp:156-595`）。

验收：`--pane-follow 0 5 --pane-follow 1 7 --follow-action next` → `follow:` 只动 pane0，
`pane1:` 的 target 不变。

### C4 第三人称是纯 orbit：不能越肩、不收缩

`native/src/native_renderer_camera.cpp:34,40`：pitch 固定 0.25..1.45 rad（只能俯视）、
distance 0.8..12；相机文件里没有 collision/occlusion/ray。契约同样只声明 orbit
（`native/docs/camera-ui-contract.md:38-39`、`docs/N16-SPLITVIEW-CONTRACT-2026-11-16.md`）。

验收：`--pane-mode 0 third-person --capture-frame tp.pgm`，pitch 取 0.25 / 1.45 两帧，
画面应只有俯视两档。

### C5 debug-camera 与 pane1 互斥

`g_debugCamera` 为真时 pane1 的 FirstPerson 分支被跳过（`main.cpp:15323`），自由机位只写
pane0——调试机位与第二 pane 不能同时观察。

### C6 FOV 的两条语义路径（不是缺陷，是容易误读）

- bookmark/快照把 `settings_.worldFov`（**水平**值）存进 `reading.fovDegrees`
  （`native/src/native_renderer_camera.cpp:184` → `main.cpp:2148` → `setWorldFov`
  `main.cpp:2162`）；`camera: snapshot ... fov=` 与 `verify ... fovMatch=` 比的是这个水平值，
  **从不校验实际投影出来的垂直角**（`native/src/camera_script.cpp:123,289-298`）。
- ViewModel 把 `viewModelState_.fov` 当**垂直**角、aspect 用整窗
  （`native_renderer_viewmodel.cpp:565-572`）。两套语义各有出处，但放在一起看像矛盾。

## 3. 建议的动手顺序

1. **C1 split ViewModel**（有前置分析、可验收、玩家直接可见）：VM 投影 aspect 改用 pane
   尺寸、只在 DemoView pane 画、trace 里报每个 pane 是否画了武器；资源账只动
   「每 pane 一次 VM pass」这一句，不碰 PIP/四窗预算。
2. **C2 `--world-fov`**（小改动，顺手）：CLI 写 `settings_.worldFov`，同一行 clamp；
   验收靠 `capture: fov` 的 vertical=59.8404 pin。
3. **C3 pane1 的 follow 平权**（命令行与状态机改动，不需要动渲染）。
4. **C4 越肩与遮挡收缩**（要新相机解算，另开卡，先量「有多少场景会被墙挡住」）。
5. C5/C6 记入文档即可，暂不动。

## 4. 复现与验收命令（都可直接跑）

```
# FOV 自证（单 pane，期望 vertical=59.8404）
tf2_demo_native.exe --demo X --capture-frame f.pgm --capture-tick N --window-size 1280x720
# split 后 aspect 变化（期望 aspect=0.888889、水平 FOV 不变、垂直 FOV 不变）
tf2_demo_native.exe ... --pane-split --capture-frame split.pgm --capture-tick N
# 双相机独立
tf2_demo_native.exe ... --pane-split --pane1-orbit 1.57 0.36 3 --pane-mode 1 first-person \
  --pane-follow 1 <entity>    → 看 pane1: 与 pane:/panes:
# 纯逻辑单测（不需要 demo）
native_ui_probe.exe ; camera_probe.exe ; spectate_probe.exe --self-test
```
