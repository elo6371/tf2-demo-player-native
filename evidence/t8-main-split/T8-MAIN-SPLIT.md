# T8 -- splitting native/src/main.cpp

Branch `task/t8-main-split`, base `d5839b5` (the published HEAD), worktree
`D:/TF2_Native_Worktrees/t8-main-split`. Nothing here is merged or pushed.

## What the file looked like when this started

- 10,935 lines.
- `wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)` alone spans 2,315-10,935: **8,621 lines
  in one function**, including seven large lambdas (reloadGpuResources 754 lines,
  traceWorldFaces 466, traceWorldDraw 276, writeViewModelReading 276,
  refreshWindowTitle 162, parseFeatureArgument 150, traceOutlineDraw 102,
  traceWorldHoles 88).
- 19 depth-2 functions in the anonymous namespace, 77 `g_` globals,
  `g_playback` 142 references, HUD state 273 references.
- Existing T76 units next to it: main_trace.cpp (104), main_lifecycle.cpp (87),
  main_hud.cpp (370), main_camera.cpp (139), main_internal.h (58). The build
  manifest `native/cmake/tf2-product.cmake` lists sources explicitly, no GLOB.

## Candidate clusters, ranked by what they cost to lift

`D:/_shared/t8/lift.py` counted, per cluster, how many globals it reads and how
many of its members something outside the cluster calls:

| cluster | lines | functions | globals | called from outside |
|---|---|---|---|---|
| D pure helpers (paths, bytes, registry, snapshot names) | 184-2294 | 7 | 0 | 0 |
| C Win32 control layer (applyUiLayout, setUiControlVisible, uiControlsUnchanged, update/create/updateNativeUiControls) | 460-511, 1463-1715 | 4 | 30 | 0 |
| B window: windowProc + isTextFieldFocused | 1778-2246 | 2 | 20 | 4 |
| A camera script: splitWords, loadCameraScript, cameraSnapshot, runCameraScriptStep (+ handleCameraAction) | 824-1385 | 4 | 24 | 1 |

Order taken: D, then C. Both are leaves -- nothing outside them is called, so a
move is a relocation, not a redesign. B drags the message pump, the text field
that hud-check.sh mutates, and four callers. A shares 24 globals with everything
and is the cluster the T50 camera gate exercises hardest, so it goes last.

## What moved, and what the compiler said

### Stage 1 -- e42034c: the file, path and byte helpers (`native/src/main_helpers.cpp`, 158 lines)

narrowAscii, normalizedModelPath, entitySnapshotStatusName, addSteamRegistryRoots,
readFile, sha256File, localCacheDirectory, with their leading comments, verbatim.
Declarations into `main_internal.h`, `src/main_helpers.cpp` into the manifest.

First build failed with `error C2065: "BCRYPT_ALG_HANDLE": undeclared identifier`;
the two Windows headers the helpers had been borrowing from main.cpp's include
block (`<bcrypt.h>`, `<shlobj.h>`) went with them. main.cpp 10,935 -> 10,783.

### Stage 3 -- ace0ac5: the Win32 control layer (`native/src/main_ui.cpp`, 347 lines)

applyUiLayout, setUiControlVisible, uiControlsUnchanged, updateNativeUiControls,
createNativeUiControls. The 25 control handles and 2 flags (g_textFieldFocused,
g_keybindsLoaded) moved from the anonymous namespace into the
`tf2::native::main_internal` definitions in main.cpp -- linkage only, definitions
stayed in the order they had, so nothing about initialisation changes. The 21
`kUi*` ids became `inline constexpr` in the header so both units name one number.

main.cpp 10,783 -> **10,459**; build rc=0 with only the warnings this file already
emitted (C4456/C4100/C4267/C4189/C4127). The build manifest gained
`src/main_ui.cpp`; no target, no command line, no probe output and no gate
expectation changed.

## How it was checked

`bash camera-ui-check.sh` -- the T4 modes/HUD/layout gate plus the T50 camera
director contract. It builds the product, native_ui_probe and camera_probe at the
top (so all sections describe one tree), then asserts the layout planner over 42
window-size/dpi combinations, the probe reading lines, and one scripted product
run that pauses, orbits, saves bookmarks and drives the settings file. The UI
layer is what that run pushes at the window, so a silent change in what the
controls get told shows up as a moved reading, not as a compile error.

### The gate is red on the base commit too, on the same line and nothing else

Run twice, same machine, host lock held both times:

| | this branch (ace0ac5) | base d5839b5 (detached, clean) |
|---|---|---|
| CAMERA-UI | FAIL | FAIL |
| failing claim | `trace camera followed observe targets = 0, expected >= 1` | identical |
| every other assertion | OK | OK |
| `camera: summary` | `follows=0 holdsNoTarget=426` | `follows=0 holdsNoTarget=445` |

The two readings differ only in the frame counter that depends on how long the
run took (426 vs 445), which is the host, not the tree. So this move changed no
reading the gate takes.

The last committed pass of this gate is `evidence/camera-ui/gate-run.log` from
ca21a50 (2026-10-13), where the same line read `follows=575 holdsNoTarget=0`.
The claim drifted at some point in the following three weeks. `camera-ui-check.sh`
is not a step in `verify-all.sh` (`grep -n camera verify-all.sh` matches only the
observer-focus step and the T84 frame comments), so the 30-step chain never
covered it and could not have reported it.

Recorded here rather than repaired: whether the product should still follow the
demo's observe targets is a product question, and the answer changes which side
of the gate gets edited. Not in this branch's scope.

Logs: `/d/_shared/t8/camera-ui.log` (branch), `/d/_shared/t8/camera-ui-base.log`
(base).

## Still in main.cpp, and why

- windowProc and dispatchHotkey: the message pump that calls the UI layer back.
- The camera-script cluster: 24 globals shared with the rest of main.cpp.
- wWinMain itself, 8,621 lines: not touched. Splitting it is a second piece of
  work (the lambdas capture a large local set); doing it in the same branch would
  make one review carry a relocation and a redesign.
