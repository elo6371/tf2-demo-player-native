# N24 — Splitting `native/src/main.cpp` (plan)

Status: **plan only — nothing applied.** No source file was edited, no build was run, no
product was executed to produce this document.
Tree: `D:/TF2_Native_Worktrees/t105-e5`, HEAD `50c8a235`, 2026-11-16.
Method: every number below was read off the working tree with `grep -n`, `sed -n` and `awk`
over `native/src/main.cpp` (15,324 lines, `wc -l`). Because line numbers drift, every phase
also names the **anchor text** it moves — the anchors, not the numbers, are the contract.

## 1. Shape of the problem

| file | lines | note |
|---|---|---|
| `native/src/main.cpp` | **15,324** | 81% of it is one function |
| `native/src/main_hud.cpp` | 1,476 | T76 split |
| `native/src/camera_script.cpp` | 633 | T76 stage 4 |
| `native/src/main_ui.cpp` | 347 | T76 stage 3 |
| `native/src/main_helpers.cpp` | 198 | T76 stage 2 |
| `native/src/main_internal.h` | 180 | the split's shared declarations |
| `native/src/main_camera.cpp` | 139 | T76 |
| `native/src/main_lifecycle.cpp` | 116 | T76 |
| `native/src/main_trace.cpp` | 104 | T76 |

`int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)` opens at **2933** and closes at **15324**:
**12,392 lines inside a single function**. The seven existing split TUs moved everything *around*
it (helpers, HUD layer, UI layer, camera, trace, lifecycle) and never touched that function.

Five facts constrain any split more than style does:

1. **12,392 lines, one frame.** There is no cleanup label (`grep -c 'goto '` = **0**), so
   teardown order is linear control flow and every early `return` carries its own partial
   teardown. Cutting a block out of the middle is a control-flow edit, not a cut-and-paste.
2. **183 command-line switches** are compared in one `for` loop (`grep -c 'wcscmp(arguments\[i\]'`
   = 183, lines 3748-4534), each backed by a local variable declared in 3073-3745.
3. **MSVC C1061 was already hit.** The comment at 2898-2901 records that the argument chain had
   to be split into `parseCaptureRangeDepthSwitch` because the block nesting limit was reached.
   That is a measured compiler ceiling on this file, not a preference.
4. **306 `traceFmt` calls** (51 of them in the scene block alone). The trace text is what the
   gates grep; the trace is part of the product's observable contract.
5. **54 by-reference lambdas and 4 function-local `static`s inside wWinMain** — hidden state
   that a naive cut would turn into dangling references or silent cross-iteration carry-over
   (details in `2.3).

## 2. Inventory of wWinMain

Ranges are inclusive and brace-verified. "exits" = early returns / PostQuitMessage in that block.

### 2.1 Whole-function phases

| # | phase | lines | n | what it does | exits |
|---|---|---|---|---|---|
| 1 | process plumbing | 2933-2953 | 21 | `openTrace()` 2934, DPI awareness 2936-2938, window class `L"TF2DemoNativeRenderer"` 2939, `RegisterClassW` 2940-2945, default 1280x720 2952-2953 | 10 (class) |
| 2 | window-size pre-scan | 2954-2974 | 21 | its **own** `CommandLineToArgvW` pass for `--window-size %ux%u`; clamps 320/240 … 8192; refuses rather than guesses; `LocalFree` in-block | — |
| 3 | window creation | 2976-2994 | 19 | `CreateWindowExW` 2976-2988, `tf2::native::Renderer renderer;` 2989, second `CommandLineToArgvW` 2995-2996 | — |
| 4 | lifecycle early exit | 3003-3008 | 6 | `parseLifecycleRequest(...)` → install/uninstall/version answer and exit before any device work | yes |
| 5 | settings-file pre-scan | 3009-3038 | 30 | second argv walk for `--settings-file`/`--ui-settings-file` | 13 (missing value) |
| 6 | renderer self-test | 3037-3055 | 19 | `--renderer-resize-selftest` → `runRendererSelftest` 3050, `DestroyWindow` 3052, return result | 0/14/15 |
| 7 | settings load + UI host | 3056-3072 | 17 | `loadSettings()` 3056, `NativeUiController nativeUi` 3063, comment 3068-3072 (logs panel attaches after the arg loop) | — |
| 8 | flag declarations | 3073-3745 | 673 | ~183 locals, one per accepted switch (capture 3094-3112, HUD 3124-3141, world 3406-3559, unrecognized 3745, strict 3746) | — |
| 9 | argument chain | 3748-4534 | 787 | `for (int i = 1; ...)` 3748; 183 `wcscmp` arms 3754-4487; unrecognized collection 4493-4518; `args: unrecognized=...` 4519-4522; `--strict-args` → stderr + return 2 at 4523-4534 | 2 |
| 10 | post-parse validation | 4535-4718 | 184 | cross-flag refusals: capture-depth/pass/range need `--capture-frame` (4561/4571/4580), `--world-geometry` 4703, `--world-hole-trace` 4707, `--world-face-trace-points` 4711, `--viewmodel-*` 4714-4717; then `renderer.setSettings(persistent.render)` 4719 | 13 |
| 11 | preload | 4722-4839 | 118 | tf-root candidate list 4722-4759, `LocalFree(arguments)` 4760, demo open 4761-4783 (`demo: open failed` 4776 → return 2), `AssetRoot::findInstalled` 4784, `resources: ...` 4790, item schema 4816, demo header/index/net summary 4820-4822, camera track limits 4828, parse 4829-4855 | 2 |
| 12 | startup resource build | 4840-11671 | **6,832** | straight-line device/asset construction (see 2.2) | 12, 13, 15 |
| 13 | window show | 11217-11234 | 18 | `ShowWindow(SW_HIDE)` 11217-11223 or `ShowWindow+UpdateWindow` 11224-11227; `mode: window_created=...` 11231 | — |
| 14 | loop setup | 11235-11671 | 437 | QPC guard 11235-11242 (→12/13), metrics CSV 11243-11256, counters 11257-11278, `viewModelResourceStatusText` lambda 11285, `MSG` 11610, `running` 11611, `tickAccumulator` 11612, entity states 11618, `spectateSlots` 11624, `captureArmed/captureRequested` 11632-11633, camera script load 11641-11650 (→15), `minimapPose*` 11667-11671 | 12,13,15 |
| 15 | main loop | 11672-14999 | 3,328 | `while (running)` — decomposed in 2.2 | 3,4,16,17 |
| 16 | shutdown | 15000-15323 | **324** | straight-line summary/save block — decomposed in 2.2 | 0..n |

### 2.2 The two big blocks

**Startup resource build (4840-11671)** — no branches that leave, one long sequence:
entity-mesh prepare/eviction budget 5312-5604 · overlay bind meshes 5539-5588 · water surface
5628 · minimap floor geometry 6063-6244 (gated on `hudMinimapCli` 6078) · world materials
6531-7299 · static props + model bank 7380-7470 · audio timeline/mp3 7570-7641 · HUD state
load 8027-8095 · **`renderer.initialize(window)` 8019** (→ `gpu: initialize failed` 8022,
return 12) · viewmodel 8164+ · `gpu: reload` begin/end 8578/9691 · pvs clip 9714 · map→screen
projection 10212 · per-face material chain 10265 · empty-pixel trace 10840 · clip-space capture
10857 · material dump 11190-11216.

**Main loop (11672-14999)** — eight sub-phases, in execution order:

| sub-phase | lines | n | contents |
|---|---|---|---|
| a. pump | 11673-11686 | 14 | `++mainLoopIterations`, `loop n=...`, `PeekMessageW`, WM_QUIT → break |
| b. timing | 11688-11695 | 8 | `elapsed = clamp(measured, 0, 0.25)`, accumulator reset on a long frame |
| c. per-tick update | 11700-11860 | 161 | audio master, pause→`stop()/reset`, **`if (g_playback.enabled && !g_playback.paused) {` 11713**, T70 ledger 11725-11729, `--capture-tick` hold 11741-11745, seek reset 11758-11766, `audio: advance` 11781 |
| d. frame prelude | 11797-11866 | 70 | `g_cameraDirector.setPaused` 11797, `noteDemoTick` 11798, `sceneRequestId` 11801, per-frame camera-mode apply 11827, `noteSceneUnavailable` 11861 |
| e. scene update | 11867-14597 | **2,731** | guarded by `g_renderer && g_playback.enabled && g_playback.tick != lastSceneTick`; snapshot/eye/instances/animation/viewmodel/overlay + spectate & follow drivers 14339-14597 |
| f. per-frame tail | 14600-14700 | 101 | camera trace 14604, `nativeUi.setPlaybackState` 14638, `logsPanelPump` 14649, `updateNativeUiControls` 14650, `refreshWindowTitle` 14651, minimap frame 14668-14694, `hudApplyLive` 14696, `writeMetrics` 14699 |
| g. draw frame | 14705-14954 | 250 | `elapsed >= kTargetFrameSeconds` 14709, `hudApplyRects` 14730, **capture arming 14731-14753**, world-clip/debug switches 14754-14758, `renderer.draw/drawSplit` 14804-14805, frame+pane readings 14844-14889, HUD draw 14900-14915, viewmodel reading 14917, world traces 14920-14928, `draw: end` 14929, **capture exit decision 14931-14951** |
| h. idle wait + device policy | 14956-14998 | 43 | `MsgWaitForMultipleObjectsEx` 14970 (wait clamped 1-16 ms 14967), device-error policy 14973-14998 (`PostQuitMessage(3/4)`) |

**Shutdown (15000-15323)** — `metricsEnd` · quiet-capture summary 15012-15042 · `audio:`
summary 15053 · `camera:` 15072 · `spectate:` 15085 · `follow:` 15098/15107/15116 · director
15125 · bookmarks 15134 · `playback:` 15144 · `keys: table` 15182 + per-action 15200 · logs
panel 15228 · `hud: keys` 15246 · `hud: save` 15265 · `hud: summary` 15269 · `camera: eye`
15290-15317 · `g_renderer = nullptr;` **15319** · `return` 15322-15323.

## 3. Coupling census

Legend: **FREE** = only its arguments; **STRUCT** = needs fields on an explicit context;
**ENTANGLED** = shares mutable state with another phase and cannot be moved alone.

| phase (lines) | reads | writes | verdict |
|---|---|---|---|
| self-test 2707-2894 | `Renderer&`, `HWND`, `bool` only; `traceFmt` | renderer device state only | **FREE** — moves today |
| argv pre-scans 2954-2974, 3009-3038 | `arguments`, `argumentCount` | 2 out-params / 2 path strings | **FREE** (out-params) |
| flag declarations 3073-3745 | nothing | ~183 locals | **STRUCT** (`CommandLine`) |
| argument chain 3748-4534 | `arguments`, flags | flags, `unrecognizedArguments` | **STRUCT** |
| validation 4535-4718 | flags | — (returns) | **STRUCT** |
| preload 4722-4839 | `nativeUi`, `persistent`, tf-root flags | `nativeUi`, ~25 asset locals | **ENTANGLED** → becomes `AppContext` fields |
| startup 4840-11671 | ~80 locals, `renderer` | `renderer`, `nativeUi`, `g_hud`, attach meshes | **ENTANGLED** (straight-line, but every write is consumed later) |
| per-tick 11700-11860 | `g_playback`, `g_playbackControl`, `soundScheduler`, `captureTick` | `g_playback.tick` (3 writers, `3.1), `soundScheduler` | **ENTANGLED** |
| scene 11867-14597 | `g_renderer`, `g_playback`, `g_camera*`, `g_pane1*`, `g_spectate*`, `g_hud`, 4 statics | renderer scene buffers, 20+ `g_*` flags, 4 function-local statics | **ENTANGLED** (last) |
| tail 14600-14700 | `g_hud`, `g_playback`, `nativeUi`, `minimapPose*`, 3 lambdas | HUD/minimap/title state | **STRUCT** |
| draw + capture 14705-14954 | `captureArmed/captureRequested`, 6 capture path locals, `g_hud` | renderer capture state, `PostQuitMessage` | **STRUCT** (`CapturePlan`, `3.2) |
| shutdown 15000-15323 | ~40 locals + `renderer` counters | settings files, `g_hud`, `g_reviewSession`, `g_renderer=nullptr` | **STRUCT**; order is the contract |

### 3.1 Riskiest state: `g_playback.tick` has three owners

It is not a variable a context struct can own; it has three writers and a wide read fan-out:

- **T70 ledger** — `const std::int32_t advanced = g_playbackControl.advance(elapsed);` 11727 then
  `g_playback.tick += advanced;` **11729**.
- **`--capture-tick` hold** — 11741-11745 writes it **backwards** to a requested tick
  (`g_playback.tick = static_cast<std::int32_t>(captureTick);` 11742), re-notes the cursor and
  resets the accumulator.
- **seek/pause** — `soundScheduler.reset(g_playback.tick)` 11710 / 11765.

Readers: the scene guard 11867, capture arming 14731, the capture exit decision 14947, the
`--capture-tick` reading, and ~12 trace lines. **A phase that changes when the tick is written
changes the composition of every subsequent frame** — this is the one value whose owner cannot
be reassigned without a product decision, so it is explicitly out of scope for every phase below
(it stays in the loop owner and is passed by reference).

### 3.2 Second entangling cluster: the draw block's 40 locals

Within 14709-14954 one scope holds the camera-script step, HUD rect push, capture arming, the
world-visibility switches, the single draw call, the frame/pane/capture readings, the HUD text
pass and the exit-code decision — all reading locals declared at 11243-11671. Extracting the
capture arm/read/decide triple (as P3 does) is only sound because it is *the last thing that
touches* those capture locals in the frame; the arming must stay textually before the draw and
the decision after it, so the extracted function is called twice, not once.

### 3.3 Hidden state a cut would break

| kind | count | the ones that matter |
|---|---|---|
| file-scope `g_*` in two anonymous namespaces | ~90, lines 128-557 | 24 are already declared in `main_internal.h`/`main_hud.h`/`main_camera.h`; the other ~66 are internal-linkage and must become `extern` to cross a TU |
| `static` file-scope inside main.cpp | `static bool g_windowSizePinned` 2175, read by `windowProc` 2177 | if `windowProc` ever moves, this flag moves with it or becomes `extern` |
| function-local `static` inside wWinMain | 4 | `lastCameraKey` **12516**, `lastCorpseSignature` **12526**, `lastTracedDriveState` **13093** — all three suppress repeat trace lines *across frames*, so they must keep their identity or the pinned trace readings change (`kPoints` 10292 is `constexpr`, harmless) |
| by-reference lambdas in wWinMain | **54** | `refreshWindowTitle` **7856** (called 14651), `writeMetrics` **11579** (called 14699 and 15001), `viewModelResourceStatusText` **11285**, `applyWorldVisClip` **9739** (called 14754), `reloadGpuResources` **8577** (called 14992), `resetAttachMeshes` **5911**, `describe` **11081**, `parseFeatureArgument` **3566** (inside the arg chain) |

## 4. Target translation units and phase order

Convention is the existing T76 one, so nothing new is invented: body in
`namespace tf2::native::main_internal { ... }`, declaration in `src/main_internal.h`,
unit added to the `add_executable(tf2_demo_native WIN32 ...)` list in
`native/cmake/tf2-product.cmake:16` (the `/W4 /permissive- /utf-8` flags come from
`native/cmake/tf2-package.cmake:88`, so a new TU inherits them automatically), and **no call
site is renamed** — `main.cpp` includes the header and has one `using namespace
tf2::native::main_internal;` (documented at `native/src/main_internal.h:24-26`).

| P | new TU | moved symbols (anchor text) | ranges | needs | wWinMain after |
|---|---|---|---|---|---|
| **P1** | `main_selftest.cpp` | `runRendererSelftest` + its F0-c comment block | 2706-2894 | `Renderer&`, `HWND`, `bool`; nothing else | 12,392 |
| **P2** | `main_cli.cpp` (part 1) | `parseCaptureRangeDepthSwitch`; window-size pre-scan | 2902-2931, 2954-2974 | out-params only | 12,371 |
| **P3** | `main_capture.cpp` | capture arming; capture readings; capture exit decision | 14731-14753, 14844-14848, 14931-14951 | `CapturePlan` + `Renderer&` + `g_playback.tick` | 12,322 |
| **P4** | `main_shutdown.cpp` | the whole summary/save block | 15000-15323 | `ShutdownInputs` struct (~40 fields); order fixed | 11,998 |
| **P5** | `main_hud_loop.cpp` | logs/UI/title/minimap/live-HUD tail + `hudApplyRects` call | 14649-14700, 14730 | `g_hud`, `nativeUi`, `minimapPose*`, `hudApplyRects` | 11,946 |
| **P6** | `main_cli.cpp` (part 2) | all 183 switches: declarations + chain + validation | 3073-3745, 3748-4534, 4535-4718 | `struct CommandLine` (183 fields), `parseCommandLine` | 10,302 |
| **P7** | `main_spectate.cpp` | spectate / follow / pane-1 driver inside the scene block | 14339-14597 | `SpectateFrame` struct; `g_pane1*`, `g_spectate*` | 10,043 |
| **P8** | `main_scene.cpp` | the scene body | 11867-14597 (2,731) | `SceneFrame` + 20 globals; keeps 3 statics | 7,312 |
| **P9** | `main_startup.cpp`, `main_loop.cpp` | startup resource build; then loop shell + tail + wait + device policy | 4840-11671; 11672-14999 | `AppContext` (owns renderer, nativeUi, assets, settings) | ~350 |

Anchor text for the risky ones (these exact lines are also gate mutation targets — `6):
P3 `if (g_renderer && renderer.lastFrameCaptureSucceeded()` / `PostQuitMessage(entityIdOverflow ? 17 :`;
P6 `for (int i = 1; i < argumentCount; ++i) {`;
P8 `if (g_renderer && g_playback.enabled && g_playback.tick != lastSceneTick) {`.

**Why this order.** P1-P2 are pure motion with one or two call sites and no shared state: they
prove the seam (header + CMake + gate pass) while the blast radius is a compiler error at worst.
P3-P5 cut the *tail* of the loop, which is where state is most nearly single-owner and where a
mistake is a wrong exit code or a lost trace line — both loud. P6 is the first wide edit: 1,644
lines but purely mechanical (`flag` → `cli.flag`), and it is deliberately *after* the capture
and HUD phases so those phases do not have to rewrite their own field access twice. P7-P8 touch
state that every earlier phase reads; P9 is last because the startup block builds the ~80 locals
that all of them consume.

Note on the brief's suggestion: the argument cluster **cannot** be phase 1. It cannot move
before `struct CommandLine` exists (673 declaration lines whose values are read across all 12,392
lines below them), and it cannot move alone without simultaneously rewriting every read site —
that is P6, and it is a rewrite, not motion. P1-P2 give the same seam proof in ~50 lines of diff.

## 5. Acceptance criteria

### 5.1 Every phase, in this order

| # | command | what it proves |
|---|---|---|
| 1 | `bash build-target.sh tf2_demo_native` | the TU compiles and links; exits non-zero on compiler error |
| 2 | `bash tools/warn-check.sh` | no new `/W4` warning vs `tools/warn-baseline.tsv` (`--emit-baseline` rewrites it; see R8) |
| 3 | `bash frame-capture-check.sh` | the program still hands back the composed frame as a checkable BMP with the same exit code |
| 4 | `bash quiet-capture-check.sh` | `--hidden-window --audio-disabled` produces the **same** frame |
| 5 | `bash build-identity-check.sh` | `--version` describes the tree the binary was linked from (D-54/D-90) |
| 6 | `bash quality-audit-check.sh` | T33 footprint/steady-state readings unchanged |
| 7 | `bash tools/audit-tree.sh` | the diff is the reviewed one, not a quiet whole-file rewrite |
| 8 | `bash verify-all.sh` (30 steps) | the full chain, once per phase batch; ~4 min for step 1 alone |

"Byte-identical frames" is defined narrowly, because the trace is not byte-stable: the **capture
BMP sha256 before and after the phase must be equal**, and the pinned *reading* lines
(`frame: width=… height=…`, `capture: summary`, `draw: end`, `mode: summary`, `hud: …`,
`camera: …`) must be textually equal. Comparative elapsed times and frame counts legitimately
differ and are not compared.

### 5.2 Phase-specific proof

| P | gate that must stay green | the reading that would catch a bad cut |
|---|---|---|
| P1 | `bash tools/fault-seam-check.sh`; `tf2_demo_native.exe --renderer-resize-selftest` → exit 0; `--renderer-fault <spec>` → exit 15 | `renderer-selftest: RESULT=PASS checks=N` unchanged (no gate greps this string — it is a manual reading) |
| P2 | `build-identity-check.sh` (its M3 mutation lives in the arg chain and must still apply) | `mode: window_created=…` width/height for `--window-size` |
| P3 | `frame-capture-check.sh`, `quiet-capture-check.sh`, `quality-audit-check.sh` | capture exit code must remain 0 on success / 16 on a failed capture / 17 on entity-id overflow |
| P4 | `hud-check.sh`, `camera-ui-check.sh`, `quality-audit-check.sh` | `keys: table`, `hud: save`, `playback: summary`, `camera: eye` line order and count |
| P5 | `hud-check.sh`, `minimap` gate, `camera-ui-check.sh` | `hud: draw`/`hud: rects`/`minimap: frame` counters |
| P6 | `build-identity-check.sh`, `hud-check.sh --mutation`, `camera-ui-check.sh`, `demo-clock-check.sh` | `args: unrecognized=N`; `--strict-args` still exits 2 |
| P7 | `spectate` gate, `camera-ui-check.sh`, `demo-clock-check.sh` | `spectate:`, `pane1:`, `follow:` lines |
| P8 | `visual-baseline-check.sh`, `demo-clock-check.sh`, `viewmodel-apply-check.sh`, `viewmodel-presentation-check.sh` | `scene: begin/clock/observer done/instances set/done`, `anim*`, `entity:` readings |
| P9 | all of the above plus `quality-audit-check.sh` | `loop n=`, `gpu: initialize failed`, `device-error-policy` |

## 6. Risk register

**R1 — the gates patch main.cpp by exact source text.** 11 patch sites in 4 gates
(`camera-ui-check.sh` 1, `demo-clock-check.sh` 2, `viewmodel-apply-check.sh` 2,
`viewmodel-presentation-check.sh` 6), plus `hud-check.sh --mutation` (3 python anchors that
exit with `mutation-N anchor not found`) and `build-identity-check.sh` (M3 rewrites the
unrecognized-argument branch and expects `args: unrecognized=1`).
`camera-ui-check.sh:1537` patches the literal `"    if (g_playback.enabled && !g_playback.paused) {"`
— **four leading spaces included** — which is main.cpp:11713; `demo-clock-check.sh:407-408`
patches `'? firstPacketTick + g_playback.tick : -1;'` (11874). Mitigation: before moving any
line, `grep -rn "<line text>" --include=*-check.sh .`; if a gate owns the anchor, either keep the
anchor in main.cpp or update the gate in the same commit and run it with its mutation mode.
**Lead verification (2026-11-16 22:15, before any phase runs).** The four-gate list above is
incomplete — I re-ran the census and the coupling is wider:

- `grep -rl 'main\.cpp' --include='*-check.sh' .` → **11** gate scripts touch the file at all:
  `camera-ui` (15 refs), `world-render-reliability` (13), `world-material` (12),
  `world-material-effects` (9), `viewmodel-apply` (6), `hud-check` (4), `world-geometry-hole` (3),
  `attach-weapon-budget` (2), `viewmodel-range` (2), `viewmodel-companion` (1), `minimap-hud` (1).
- **More than the four named gates patch source text.** Verified beyond the doc's list:
  `world-render-reliability-check.sh:388` — `sed -i 's|^\( *\)renderer\.clearLastError();|\1// MUTATION (D-98): the clear taken back|'`;
  `world-material-check.sh:807-818` — `main.cpp.clip.backup` + anchor rewrite;
  `camera-ui-check.sh:1330-1347` — register/restore driver around `native/src/main.cpp`.
- **Consequence for every phase:** run the anchor grep across **all** `*-check.sh` (not the four),
  and treat any hit inside the phase's moved range as a blocker: either leave the anchor in
  `main.cpp` or land the gate change in the same commit and run its `--mutation` mode.
  Line numbers in this document are snapshots; the anchor **text** is the contract.

**R2 — build identity changes by construction.** `build-identity-content.sh` recomputes a
digest over `native/CMakeLists.txt`, `native/src`, `native/include`, `native/third_party`,
`native/cmake`; `release-rebuild.sh` records `inputs_sha256` + `binary_sha256` per target in a
manifest. A split rewrites `native/src` and `native/cmake/tf2-product.cmake`, so both the source
digest and the linked binary change: the sidecar/stamp must be regenerated deliberately, and
every *other* target's `binary_sha256` must be unchanged. Silently omitting a new TU from the
identity input set is exactly the failure mode D-90 refuses.

**R3 — internal linkage.** ~90 `g_*` definitions are in anonymous namespaces; only 24 are
declared in headers today. A moved symbol needs external linkage inside
`tf2::native::main_internal` plus a declaration in `main_internal.h`. P1 needs none (it touches
no global); P3 onward do.

**R4 — by-reference lambdas (54).** `refreshWindowTitle` (7856), `writeMetrics` (11579),
`viewModelResourceStatusText` (11285), `applyWorldVisClip` (9739), `reloadGpuResources` (8577)
capture locals by reference and are called from the loop. Handing one to a function that outlives
the frame compiles fine and crashes later. Mitigation: any lambda that crosses a new TU boundary
becomes a function whose state is an explicit struct field, and its call sites move with it.

**R5 — function-local statics.** `lastCameraKey` 12516, `lastCorpseSignature` 12526,
`lastTracedDriveState` 13093 suppress *repeated* trace lines across frames. If they become scene
context fields that are reconstructed per frame, the trace silently gains duplicate lines and the
pinned gates go red for a reason that looks unrelated. Keep them `static` when P8 moves them.

**R6 — device lifetime.** `renderer.initialize` 8019, `renderer.shutdown` 15223 and the recovery
path `shutdown(); resetAttachMeshes(); initialize(); reloadGpuResources()` 14990-14992, with
`g_renderer = nullptr` only at 15319. Summary lines in 15012-15290 read renderer counters, so the
shutdown extraction (P4) must keep `renderer.shutdown()` *after* those readings and before
`g_renderer = nullptr`.

**R7 — no `goto`.** Teardown is linear; every early `return` does its own cleanup (3052, 4781,
4810, 8024, 11650…). Extracting a block must keep the partial-teardown order inside it byte for
byte — this is why P4/P5 are specified as "call a function at the same point", not "hoist".

**R8 — `/W4` and a new baseline entry.** `tools/warn-check.sh` compares against
`tools/warn-baseline.tsv` (13 lines; 3 for `native/src/main.cpp`: C4456 x1, C4244 x5, C4457 x1).
A new TU inherits `/W4` from `tf2-package.cmake:88` but has **no baseline entries**, so any
warning in it is new. Conversely, if moved code carried one of main.cpp's baseline warnings, the
count drops and the gate fails until `--emit-baseline` is re-run in the same commit. P1 moves
code that currently compiles clean, so its expectation is "13 lines unchanged".

**R9 — C1061 nesting ceiling.** 2898-2901 documents that the argument chain already exceeded
MSVC's block-nesting limit. P6 must *flatten* (one `if` per switch, no new nested blocks) and
must not re-nest the 183 arms.

**R10 — stale line references in gate comments are not a contract.** `build-identity-check.sh:221`
cites main.cpp:11613-11625 for the settings save (that range is inside the loop now) and
`camera-ui-check.sh:214-229` cites main.cpp:12626-12631 for the `target=` camera line (it is at
14609). Numbers in comments already rot; update them opportunistically, never rely on them.

**R11 — trace text is the contract.** 306 `traceFmt` calls; 51 in the scene block. Moving code
must not change a format string, its argument order, or the order of lines. Re-indenting a moved
line is only safe for lines no mutation patches.

**R12 — audit trail.** `tools/audit-tree.sh` compares the live tree against a frozen lead
baseline; a split is a large, legitimate diff and must be one reviewed change with the phase's
verification output attached, or it reads as an ownership violation.

## 7. Phase 1, fully specified: the renderer self-test

**Why first.** It is the only ~190-line unit that is both self-contained (no globals, no captured
locals, no `static`s) and already outside every gate's text anchors: no script greps
`renderer-selftest` (only `tools/fault-seam-check.sh` mentions the flags that reach it). It does
not shrink wWinMain — it proves the seam at the lowest possible cost.

**Boundaries (verified).** Function 2728-2894; signature
`int runRendererSelftest(tf2::native::Renderer& renderer, HWND window, bool faultSpecIgnored)`;
doc comment block 2706-2727 beginning `// ---- F0-c (2026-11-11): the renderer resize/render self-test ----`;
it sits alone inside `namespace { ` **2705** / `}  // namespace` **2896**, i.e. that anonymous
namespace exists only for this function. Body uses only the three parameters, `traceFmt`,
`std::snprintf/std::fprintf/std::string`, and two local lambdas (`hex` 2731, `expect` 2739 that
captures `checks`/`failures` by reference within the same function).

**Edits.**
1. New file `native/src/main_selftest.cpp`: includes `"main_internal.h"` (for `traceFmt`) and
   `"native_renderer.h"`; body copied verbatim into
   `namespace tf2::native::main_internal { int runRendererSelftest(...) { ... } }`.
2. `native/src/main_internal.h`: one declaration after the trace section —
   `int runRendererSelftest(tf2::native::Renderer& renderer, HWND window, bool faultSpecIgnored);`
   plus a forward `class Renderer;` if the header does not already have one (it currently only
   forward-declares `ReviewSession`, line 41).
3. `native/cmake/tf2-product.cmake`: one line `src/main_selftest.cpp` in the
   `add_executable(tf2_demo_native WIN32 ...)` list (after `src/main.cpp`, with a T76-style
   comment). `/W4` is inherited from `tf2-package.cmake:88`.
4. `native/src/main.cpp`: delete 2706-2894 and the now-empty `namespace {` 2705 / `}  // namespace`
   2896 pair.
5. **No call-site edit.** 3050 `const int selftestResult = runRendererSelftest(renderer, window, !faultSpec.empty());`
   compiles unchanged through the `using namespace tf2::native::main_internal;` in main.cpp.

**Expected diffstat (estimate).** `main.cpp` −190/+0 · `main_internal.h` +6 · new
`main_selftest.cpp` +193 · `tf2-product.cmake` +1. `wc -l main.cpp` ≈ **15,134**.

**Verification, cheapest first.**
```sh
wc -l native/src/main.cpp                                  # expect ~15,134
grep -c 'wcscmp(arguments\[i\]' native/src/main.cpp        # expect still 183
grep -rn 'renderer-selftest' --include='*.sh' .            # expect unchanged hit list
bash build-target.sh tf2_demo_native                       # exit 0
bash tools/warn-check.sh                                   # 13 baseline lines, main.cpp count unchanged
native/build-nmake/tf2_demo_native.exe --renderer-resize-selftest; echo "exit=$?"   # 0
native/build-nmake/tf2_demo_native.exe --renderer-resize-selftest --renderer-fault <spec>; echo "exit=$?"  # 15
bash tools/fault-seam-check.sh                             # PASS
bash frame-capture-check.sh && bash quiet-capture-check.sh  # BMP sha256 unchanged
bash build-identity-check.sh                               # M3 mutation still applies
bash tools/audit-tree.sh                                   # only the 4 intended files differ
```
Rollback: `git checkout -- native/src/main.cpp native/src/main_internal.h native/cmake/tf2-product.cmake && rm native/src/main_selftest.cpp`.

## 8. What cannot be determined without building

- Whether any of main.cpp's three baseline `/W4` warnings originate in code that a later phase
  moves (only `warn-check.sh` after the move answers this).
- Whether `tools/warn-check.sh` enumerates TUs from the CMake target (so a new TU is picked up
  automatically) or from a fixed list (so it must be added). Its header says it recompiles "the
  TUs that carry /W4 with CMake's exact flags" — that implies the target, but it was not proven.
- Whether `tools/audit-tree.sh`'s frozen baseline records a line count or a hash for
  `main.cpp`; if it records a count, every phase needs a lead-visible baseline refresh rather
  than a silent diff.
- The real wall-clock cost of each gate batch (the only measured figure available is
  `verify-all.sh`'s own header: ~4 min for the clean build step, ~20 min for step 5).
- Whether `tf2_demo_native` currently links `main_internal.h` symbols that P1 would need
  re-including (`native_renderer.h` inclusion from that header is unverified).
- Whether extracting 1,644 lines of argument chain (P6) stays under MSVC's per-function limits
  and the `/W4` C4456/C4457 shadowing baseline, given the existing 673-line declaration block.
