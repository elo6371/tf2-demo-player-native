# U4 step 1 — pane ownership audit (2026-11-16)

Scope, in the roadmap's words (`docs/PROJECT-EXECUTION-ROADMAP-2026-11-15.md:109`, N16):

> U4 / N16: 双 pane 共享时间轴，每窗 target/mode 独立；点击激活、第一/第三/自由切换，seek 同步；
> 单视角旧功能不退化。出口 = PIP → 四窗资源预算评估。注意：查全局相机/viewport/深度/状态串用，
> 先补确证的所有权边界，不复制 demo 解析器或全资源库。

This step delivers the boundary and nothing above it: **no second pane, no UI, no second
renderer, no demo-parser copy**. What it delivers is the audit table below, a predicate that
turns the current single-surface contract into a testable statement, a product trace line that
prints what the last drawn frame actually bound, and a probe section that proves each rule is
separately reachable. Step 2 (two panes on one clock) is the consumer of all of this.

## What shipped

| file | what |
|---|---|
| `native/include/pane_boundary.h` | `PaneCameraSource`, `PaneBoundaryReadings`, `paneBoundaryMismatches()` (7 bits), `paneCameraSourceName()`. Header-only so a probe can test the predicate without linking a renderer. |
| `native/include/native_renderer.h` | new members `paneBoundary_` / `paneDrawOrdinal_` (around :2441) and read-only accessors `paneBoundary()` / `paneBoundaryMismatches()` (:1326-1333). |
| `native/src/native_renderer_frame.cpp` | `draw()` records the readings where the bindings are made (:42-62), the camera it actually built the view matrix from (:162-165), and the HUD half after the pass (:1303-1306). |
| `native/src/main.cpp` | additive `pane:` trace line beside the existing `capture:`/`hud:` lines (:14279-14291). Old lines untouched. |
| `native/tools/native_ui_probe.cpp` | section "pane boundary contract" (:301-364, called at :376): a clean reading, eight negative controls (one per rule plus the second extent case), the all-seven-bits-reachable check, the HUD-off-must-stay-clean case, and the three camera-source names. |
| this document | the table and the reasoning. |

The probe's failures already gate: `camera-ui-check.sh:589` asserts the probe exit is 0 and
:590 fails the gate when any expectation is missing, so this section is enforced without
editing a gate script or its thresholds.

## The table

"Owner today" is the object one pane currently shares with everything else; "verdict" says
whether a two-pane frame keeps it shared or has to make it per-pane. Line numbers are this
worktree at the step-1 commit.

| # | state | owner today | written by | read by | reset point | what a second pane does to it | verdict |
|---|---|---|---|---|---|---|---|
| 1 | swap chain | `Renderer::swapChain_` | created `native_renderer.cpp:140`, resized `:939` | `Present` `native_renderer_frame.cpp:1323` | never (only resize) | one HWND owns one chain; a second pane either shares this surface through scissored viewports or needs its own chain + HWND | **shared (single surface)** |
| 2 | back-buffer RTV | `Renderer::target_` | created `native_renderer_entity.cpp:105`, released `:123` | bound `native_renderer_frame.cpp:30`, cleared `:31` | per resize | both panes would bind the same RTV; no per-pane colour target exists | **shared** |
| 3 | depth view | `Renderer::depthStencilView_` | created `native_renderer_entity.cpp:115`, released `:123` | bound `native_renderer_frame.cpp:30`, cleared `:32` and `native_renderer_viewmodel.cpp:638` | per resize | two panes sharing one depth buffer means pane B's geometry occludes pane A's; needs per-pane depth or a depth-range split | **shared** |
| 4 | depth-stencil state | `Renderer::depthStencilState_` / `viewModelDepthStencilState_` | `native_renderer_frame.cpp:33`; viewmodel switch `native_renderer_viewmodel.cpp:640` | D3D draws | per pass | state is global to the immediate context; a second pane must re-set it after every swap | **shared** |
| 5 | viewport | computed in `draw()` from `width_`/`height_` | `native_renderer_frame.cpp:36-41`; members `native_renderer.h:1698-1699` | every draw call | once per draw start | nothing re-sets it per pass (the HUD pass only sets a scissor, `native_renderer_frame.cpp:1554`), so a second pane must set its own viewport or the HUD inherits the wrong one | **per-pane** |
| 6 | client size | `Renderer::window_` + `width_`/`height_` | `GetClientRect` `native_renderer.cpp:118`; `resize` `:926-939` | `draw()` fills the pane reading `native_renderer_frame.cpp:44-61` | per resize | a pane smaller than the client is exactly R4's mismatch, on purpose: it proves the frame is not yet pane-aware | **shared** |
| 7 | world view matrix | `frameView_` / `frameEye_` | `native_renderer_frame.cpp:166-176`, defaults `:89-95`; members `native_renderer.h:2101-2102` | viewmodel pass, overlay silhouettes (:924), world trace | once per draw | two panes need two view matrices; one member cannot hold both | **per-pane** |
| 8 | orbit camera | `cameraYaw_` / `cameraPitch_` / `cameraDistance_` (`native_renderer.h:2366-2368`) | `native_renderer_camera.cpp:27` (orbit), `:37` (zoom), `:153` (reset), `:188` (set pose) | `draw()` orbit branch `native_renderer_frame.cpp:135-163` | never (setters only) | one orbit pose for both panes; the pane that did not receive the mouse would move too | **per-pane** |
| 9 | observer focus | `observerFocusValid_` + X/Y/Z (`native_renderer.h:2369`) | `native_renderer_camera.cpp:43`, `:51`, `:130` | `native_renderer_frame.cpp:136-139` | never | shared look-at target means one pane's spectate target drags the other | **per-pane** |
| 10 | demo camera (first/third person) | `observerDemoView_`, eye/pitch/yaw (`native_renderer.h:2373-2378`) | `native_renderer_camera.cpp:98` (clear), `:100` (set); product entry `native/src/main.cpp:14220`, clears `:210`, `:1833`, `:2405`, `:11444` | `native_renderer_frame.cpp:137-163` | per setter | this is the "first / third / free" state U4 wants per window; shared today, so one window's mode change moves both | **per-pane** |
| 11 | projection FOV | `settings_.viewModelFov` (`native_renderer.h:75`, clamp :34-36) | inline setter `native_renderer.h:1417`, called `native/src/main.cpp:1832` and `native/src/main_camera.cpp:123` | `native_renderer_frame.cpp:202` | per settings change | two panes cannot have different FOV; the projection is built from one settings field | **shared (must split in step 2)** |
| 12 | pass ordinal | `framePassOrdinal_` (`native_renderer.h:2108`) | reset `native_renderer_frame.cpp:60`, incremented by each pass (:1581 for HUD) | HUD readings and traces | once per draw | a two-pane frame that draws twice per draw must decide whether the ordinal is per draw or per pane | **shared** |
| 13 | HUD primitives + enabled | `hudPrimitives_` / `hudRectVertexCount_` / `hudEnabled_` | `setHudPrimitives` `native_renderer_frame.cpp:1330`, `setHudRects` `:1338`, `setHudEnabled` inline `native_renderer.h:1303`; published `native/src/main_hud.cpp:305`, `:362`, `:364`, `:1418` | `drawHudPass` `native_renderer_frame.cpp:1299+` | list replaced on every publish | one HUD list for one window; dual pane needs either per-pane HUD state or one overlay drawn after both | **shared** |
| 14 | HUD pass/scissor readings | `hudPassOrdinal_`, `hudScissorApplied_`, `hudLastRegion_` (`native_renderer.h:2438-2440`) | `native_renderer_frame.cpp:1554`, `:1581`; reset `:67-73` | product `pane:` line (`native/src/main.cpp:14288`), `hud:` line | once per draw | two HUD passes in one draw would break R6 on purpose | **shared** |
| 15 | frame capture | `capturePath_` (`native_renderer.h:2410`) | `requestFrameCapture` `native_renderer.h:1643`, called `native/src/main.cpp:14174` | `native_renderer_frame.cpp:1287`, `:1310` | cleared when the one-shot capture completes | one capture slot: two panes cannot both write a frame | **shared** |
| 16 | world clip capture readings | `worldClipCaptureValid_` / `Attempted_` (`native_renderer.h:1742-1743`) | reset `native_renderer_frame.cpp:85-86`, written by the world pass | `worldClipCaptureStatus` (`native_renderer.h:1236`) and the clip trace | once per draw | single-slot: the second pane would overwrite the first pane's capture | **shared** |
| 17 | demo clock | `entityModelTimeSeconds_` (`native_renderer.h:1987`) | `setEntityModelTime` inline `native_renderer.h:915`, called `native/src/main.cpp:13185` | entity proxies `native_renderer_entity.cpp:593-607` | once per frame | **this one is shared on purpose**: U4 requires one time axis across panes, and seek must move both | **shared by design** |

Totals: 6 per-pane (5, 7, 8, 9, 10, 11), 11 shared. The six are the whole of step 2's work on
the renderer side; the eleven are what a PIP / four-window budget has to price.

## The contract, as seven rules

`paneBoundaryMismatches()` returns one bit per rule; the product trace prints it as `0x0` when
the frame still describes one surface.

| bit | rule | why it is the boundary |
|---|---|---|
| R1 `kPaneMismatchRenderTargets` | exactly one render target bound | a second target is a second surface; the swap chain owns exactly one |
| R2 `kPaneMismatchDepthViews` | exactly one depth view bound | panes cannot share a depth buffer without occluding each other |
| R3 `kPaneMismatchViewportCount` | exactly one viewport | two viewports is already a two-pane frame; the renderer must know that explicitly |
| R4 `kPaneMismatchViewportExtent` | viewport extent equals the client rect, and is non-zero | the current code derives the viewport from one `width_`/`height_`; a pane-wide viewport would silently render the wrong region |
| R5 `kPaneMismatchCamera` | a camera source exists | distinguishes "the frame was built from the orbit camera / the demo camera" from "no world camera this frame" |
| R6 `kPaneMismatchHudPasses` | at most one HUD pass per draw | the HUD draws into the same surface; two passes means one pane's HUD would cover the other's |
| R7 `kPaneMismatchHudScissor` | if a HUD pass ran, it scissored | a HUD pass without a scissor can paint outside its rectangles, which is the claim `hud-check.sh` rests on |

A frame with the HUD off is clean (R7 only constrains a pass that ran) — the probe pins that case
so the rule cannot be "fixed" by requiring the HUD.

## Reading it in the product

The `pane:` line is printed next to the existing capture readings, so any captured frame carries
the evidence:

```
pane: draw=1 viewport=1280x720 client=1280x720 targets=1 depth=1 viewports=1 camera=demo-view hud=1 scissor=1 mismatches=0x0
```

`camera=` is the camera the view matrix was built from (not the `scene: camera` request), and
`draw=` is a monotonic counter: a reading that did not refresh cannot masquerade as clean.
`mismatches=0x0` is the claim step 2 will have to replace with an explicit pane-aware
predicate — that replacement, not a silent change here, is what "确证边界" means.

## Deliberately not done

* no second renderer, HWND, swap chain or target; no pane split.
* no UI, no click activation, no per-pane camera mode plumbing (step 3).
* no copy of the demo parser or the resource library (the roadmap forbids both).
* no gate script or threshold touched. `camera-ui-check.sh` is unchanged; the probe's exit code
  already gates the new section.
* no attempt to make the trace's older `scene: camera` line honest — that is the t2 remediation,
  which is a separate commit after U4's three steps.

## Verification

```
bash tools/lockrun.sh card-ui -- bash build-target.sh native_ui_probe
bash tools/lockrun.sh card-ui -- bash build-target.sh tf2_demo_native
native/build-nmake/native_ui_probe.exe        # expect rc=0 and paneBoundary* readings
```

Product capture: the same `--capture-frame` launch used by `capture-u3a.sh` (`--hidden-window
--hud-live --hud-minimap`, one map) and read the `pane:` line out of the trace. Expected on an
unmodified single-pane frame: `mismatches=0x0` with `viewport` equal to `client`.

Build-identity note: the first build of this step was REFUSED by `gen-build-stamp.cmake` (D-90,
untracked compile input) until `native/include/pane_boundary.h` was `git add`ed. That is the
generator working as intended: a new header the compiler reads must be in the content hash, and
the refusal is recorded here so nobody "fixes" it by allowing untracked inputs.
