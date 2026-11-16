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

## Step 2b: two panes over one surface

2a replaced the single-surface predicate with a per-pane one: `kPaneMaxPanes = 2`,
`PaneBoundaryPaneReadings` per pane (x/y/width/height, camera, view hash, camera-input
fingerprint, camera slot), and three rules that only exist because there are two panes --
R8 `kPaneMismatchPaneLayout` (panes must tile the client and not overlap), R9
`kPaneMismatchSharedView` (two panes reporting one view hash from two different camera
fingerprints), R10 `kPaneMismatchSharedCamera` (two panes quoting one camera slot). Seven
rules became ten, and the probe's negative controls went from 8 to 13: each new rule is broken
by its own mutation, and R9/R10 are the two defects a lazy split (draw the same view twice)
would actually produce. This is the "改谓词必须配负控" condition, and the negatives are the
evidence, not the green run.

2b makes the renderer compose the frame from two panes. The structure:

* `Renderer::draw()` is now a thin wrapper: `paneCountThisDraw_ = 1`,
  `syncPrimaryPaneCamera()`, `drawPaneInternal(..., pane=0, count=1)`. The body moved into
  `drawPaneInternal` unchanged except where this table says otherwise.
* `Renderer::drawSplit()` calls `drawPaneInternal` twice with pane 0 and pane 1, then restores
  pane 0's frame camera (`frameView_` / `frameEye_` / `frameForward_` / `frameUp_` /
  `frameRight_`) so the camera the rest of the program reads after a frame is still the
  single-pane camera, not whichever pane was drawn last.
* Per-pane viewport: left pane gets `width_ / 2`, right pane `width_ - width_/2` (an odd width
  loses no column); both get the full `height_`. The viewport is the only thing that moves.
* Per-pane state: `PaneCameraState paneCameras_[kPaneMaxPanes]` (include/native_renderer.h,
  next to `paneDrawOrdinal_`), `paneIndexThisDraw_`, `paneCountThisDraw_`,
  `paneDepthClears_`. Pane 0 is refreshed from the orbit/observer members at the start of every
  frame by `syncPrimaryPaneCamera()` -- the existing camera setters stay the one source of truth
  -- and pane 1 is written only by `setPaneCameraOrbit()`, which refuses pane 0. Camera input
  therefore cannot reach pane 1 by accident, and pane 1 cannot overwrite pane 0's camera.
* Per-frame readings (`framePassOrdinal_`, `viewModelWorldPassOrdinal_`, HUD counters, sky/vis
  flags, clip-capture flags) are reset once, by pane 0. Later panes keep counting into the same
  frame totals, which is what makes `panes: passes=` a frame number.
* Only pane 0 clears the back buffer and the depth view: one surface, disjoint viewports, so a
  per-pane clear would erase the half already drawn. `paneDepthClears_` counts the clears that
  do happen (`++` at the frame-start clear and at the ViewModel pass's own full-surface clear).
* The camera block reads `paneCameras_[paneIndex]` instead of the global members; the aspect is
  `paneWidth / paneHeight` (a half-width viewport with the window's aspect would stretch every
  pane); `paneBoundary_.panes[paneIndex]` is filled with that pane's rectangle, camera source,
  view hash, fingerprint and slot.
* The HUD is a whole-window overlay: it runs once, from the last pane, after restoring the full
  client viewport. `drawHudPass()` never set a viewport, so without that restore it would draw
  through the last pane's half.
* The ViewModel pass is deferred in a split (`paneCount == 1`): it builds its projection from
  the window aspect and clears the whole depth surface, so drawing it twice would be the wrong
  fix. A split frame therefore shows `depthClears=1`, not 2 -- the budget reading says so.
* Capture (BMP + depth PGM) and `Present` run once, from the last pane.

Shared by design (same list as the audit table, now with a second pane proving it): the demo
playback clock (`entityModelTimeSeconds_`), one swap chain, one render target, one depth view,
the whole-window HUD, and the capture slot. The panes are two views of one frame; seeking one
pane is seeking the frame, and step 3 may not give them separate clocks.

### The budget line

```
pane:  draw=1 viewport=632x681 client=1264x681 targets=1 depth=1 viewports=2 camera=demo-view hud=1 scissor=1 mismatches=0x0
panes: count=2 viewports=2 depthTargets=1 depthClears=1 passes=15 hud=1
```

`viewport=` stays pane 0's extent (R4 pins it against `client` for a single-pane frame), so a
two-pane frame is recognisable by `viewports=2`. `panes:` is the additive budget line: panes
composed, viewports, surface count, whole-surface depth clears, draw passes handed out across
all panes, and HUD passes. Those five numbers are what a PIP -> four-window budget has to be
able to count before it can argue one.

### CLI and evidence

`--pane-split` turns the split on; `--pane1-orbit <yaw> <pitch> <distance>` writes pane 1's
camera (the pre-ladder block consumes both, same as the fog flags). Without it the two halves
are the same picture, which is the control frame for "the panes are independent".

* single pane, same window: old `75dae10e` vs new `5c357113`, BMP sha256 both
  `0e00f553e17b66176f8c4aef50a17ee0852abbf81218679c4fe9cc6a5dfe143f`, `cmp` 0 bytes. The
  single-pane path did not change pixels.
* two panes, `pl_upward_f12` and `cp_sunshine`: both halves non-empty (clear-colour fraction
  0.0000 in each half), halves differ, `mismatches=0x0`.
* independence negative control 1: change only pane 1's orbit -- left half byte-identical
  (0 of 1,291,176 bytes), right half 963,808 bytes differ.
* independence negative control 2: change only pane 0's debug camera -- left half 1,253,828
  bytes differ; right half 32 pixels differ, all inside the HUD's rectangle
  (`region=24,12,1228,160`; diff bbox x 1159..1168, y 120..127). The HUD is the one
  whole-window overlay and its camera indicator follows pane 0; the pane 1 world pixels did not
  move.
* `camera-ui-check.sh` = PASS, `tools/audit-tree.sh` = VERDICT: OK.

This section supersedes "no pane split" in `Deliberately not done` above: step 2b is the split.

## Step 3: interaction -- one pointer, two panes

Step 2b made the frame two cameras. Step 3 makes the input say which camera it is
addressing, without moving the single-pane path.

* `native/include/pane_input.h` is the only arithmetic the mouse needs:
  `paneForClientX(x, width, paneCount)` -- half-open `[0, width)`, split at
  `width / 2` (the left viewport's width), a click outside the client or a
  non-positive width selects nothing (`-1`), one pane owns every pixel, and a pane
  count above `kPaneMaxPanes` clamps. It is a pure header function, so the window
  procedure and the probe call the same rule; the probe pins 20 cases and 7
  negatives (both edges, both panes, the 1264-wide boundary at 631/632, an odd
  width, a one-pixel client, width 0 and -5, zero panes, an over-large count).
* `g_activePane` owns the keyboard and the mouse because it is the pane the pointer
  last clicked. `WM_LBUTTONDOWN` inside the client and not consumed by a panel
  selects the pane under the cursor and traces
  `paneActivate: pane=… x=… width=… height=… active=… source=mouse`. A single-pane
  program never enters that path: `g_paneSplit` is false and every routing branch
  falls through to the code that was already there.
* Keyboard, right drag and wheel follow `g_activePane`: space cycles the ACTIVE
  pane's camera through a second `CameraModeController` (`g_pane1CameraMode`), and
  leaving first-person clears that pane's demo view only; orbit deltas and zoom go
  to `orbitPaneCamera` / `zoomPaneCamera` for pane 1, the same 0.008 rad/px and
  `exp(-delta * 0.08)` clamps as pane 0.
* Follow commands (`Select` / `Unfollow` / `Next` / `Prev`) route too: pane 1 has
  its own `SpectateDirector`, its own pinned target and its own accept/reject
  counters, updated with `handle=0` immediately after pane 0's update so both panes
  read one snapshot and one scene tick. There is no second clock.
* Pane 1's camera block mirrors pane 0's per mode: first-person places the eye from
  its own director's snapshot, third-person focuses its director's target,
  free-look leaves the orbit pose alone. It traces
  `pane1: mode=… target=… eye=… focus=… src=… hidden=… tick=… active=…` only when
  something changes, and `panes:` gained
  `active= mode1= target1= applies1= commands1= rejected1=` for the same reason the
  pane-0 counters exist: a command that reached its pane and one that never ran are
  different readings.
* The CLI is the automation surface, and argv order is the semantics
  (`--pane-split` must precede the pane commands that act on it):
  `--pane-activate N`, `--pane-click X WIDTH`, `--pane-cycle`,
  `--pane-mode N first|third|free`, `--pane-follow N ENTITY`. Each calls the same
  function as the mouse/keyboard path and traces `source=cli`.

### Two defects this step found (and fixed)

* `--pane-split` set a LOCAL `paneSplit` only. The draw used the local, but every
  routing consumer read `g_paneSplit`, which was never assigned: activation, input
  routing, pane 1's director update and pane 1's camera block never ran, and the
  first `--pane-cycle` went to pane 0. What caught it was a control pair coming out
  IDENTICAL -- `--pane-activate 1 --pane-cycle` versus `--pane-mode 0 free` -- which
  cannot happen if the commands reach different panes.
* Pane 1's pending pick was consumed on the first director update, before the first
  follow snapshot existed, so the director rejected it and the pane kept its stable
  pick: `--pane-follow 1 1` ended with `target1=2`. The pick is now adopted only
  once `g_followSnapshotReady` is true, and `pane1Pick: entity=… accepted=…
  rejected=… target1=…` makes the rejection a number instead of an absence.

### The hidden body is one list, not two

A first-person relay camera stands in a player's eye, so that body must not be drawn
(T81). The model instance list is built once per frame and is frame-global; with two
first-person-capable panes there are two candidate bodies and one list, so the filter
removes the UNION: a body hidden for either pane is gone for both. Both camera blocks
publish their candidate (`g_spectateCameraHiddenEntity` and the new
`g_pane1CameraHiddenEntity`, one frame behind -- the same lag pane 0 has always had),
`scene: cameraFilter` prints `hidden0=`/`hidden1=`, and `panes:` prints them too.
The cost is measured, not argued away:

* pane 1 first-person with a follow target (body hidden): left half differs by 120
  pixels at (0,463)-(23,491) -- pane 0's view of the body pane 1 stands in, removed by
  the union -- while the right half changes 428,755 pixels and is no longer filled
  with the target's torso. That torso fill was the defect this section fixes.
* pane 0 leaving first-person: `--pane-mode 0 free` and `--pane-mode 0 third` give
  BYTE-IDENTICAL frames, both halves. The right half moves 13,411 pixels at
  (766,119)-(1221,440) purely because pane 0's body came back into the shared list.
  The camera mode did not move pane 1; the hide set did.

Per-pane instance lists are the correct fix and are NOT in this step: the panes share
one instance buffer, one constant buffer and one surface by design, so a per-pane body
list means a per-pane upload -- a budget decision (and its own measurement) rather
than a routing one.

### Evidence

Every capture: pinned `--capture-tick 2000` on the same demo, 1264x681, both halves
compared byte-wise (24 bpp rows de-padded), left = [0,632), right = [632,1264).
Each half is 1,291,176 bytes.

| claim | reading |
| --- | --- |
| single pane unchanged | baseline vs this build: left `de4a64b4…`, right `26dfb247…`, both halves 0 bytes differ |
| split at rest unchanged | step-2b build vs this build: left `295a4dac…`, right `34c66bce…`, both halves 0 bytes differ |
| explicit default is the default | `--pane-mode 0 first` = split baseline, both halves identical |
| a target alone moves no pixels | pane 1 free-look + `--pane-follow 1 1`: both halves identical to the split baseline (`mode1=free-look target1=2`) |
| pane 1 first-person + follow | left 120 px (union cost, above), right 428,755 px; `pane1: … target=2 … src=follow hidden=2`, `cameraFilter removed=2 indices=2,3` |
| pane 1 third-person | left 0 px, right 101,586 px; `pane1: … target=2 … src=focus hidden=-1` |
| pane 0 mode change | left 430,206 px, right 13,411 px = the hide set, not the camera (both modes byte-identical) |
| probe | rc=0, `paneInputCases=20 paneInputNegatives=7` beside the existing `paneBoundaryRules=10 paneBoundaryNegatives=13 paneCameraSources=3` |
| gates | `camera-ui-check.sh` = CAMERA-UI=PASS; `tools/audit-tree.sh` = VERDICT: OK |

Still deliberately not done, and now stated per item: the ViewModel pass in a split
(2b defers it; a first-person pane therefore shows no weapon, and the lead's verdict
-- one side, both sides with per-pane aspect, or hidden -- is still open); per-pane
seek and demo clocks (the clock is shared by design and step 3 did not change that);
per-pane instance lists (above).

