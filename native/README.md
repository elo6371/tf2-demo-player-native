# TF2 Demo Player Native Renderer

The root [README](../README.md) is the project entry point. Build and release
acceptance rules live in [`docs/RELEASE-POLICY.md`](../docs/RELEASE-POLICY.md).

This is the new Windows-native rendering direction. It deliberately has no
Electron, Node.js runtime or WebGL path. The only web technology is the vendored WebView2 loader behind the logs panel (`native/cmake/tf2-product.cmake:107-111,145`); on a host without the WebView2 runtime the player still starts, the logs panel just cannot display imported URLs.

## Current stage

The current MVP creates a Win32 window, initializes a hardware Direct3D 11
device and swap chain with WARP fallback, indexes real TF2 resources, rebuilds
entity state from real Demos, draws the basic BSP world and diagnostic effects,
and exposes a tested UI import state controller. It is still an incremental
renderer: the controller exists, but the full button/timeline/HUD/settings
visual layer is the next UI slice.

## Initial quality profiles

`RenderSettings::fromPreset` starts with two explicit profiles based on the
local TF2 configuration and the supplied 2560x1440 screenshots:

- `Performance`: `mat_picmip 2`, one-tap anisotropic filtering, no dynamic
  lighting, shadows, bumpmaps, specular or water reflections, model LOD 4.
- `Standard`: full-resolution textures, 8x anisotropic filtering, dynamic
  lighting, shadows, bumpmaps, specular, skybox and water reflections, model
  LOD 0.

Both profiles start with VSync disabled so a 120 FPS display target is not
artificially capped at 60 Hz; `--vsync` enables it for a launch without changing the saved settings.

The performance profile mirrors `maxframes.cfg`; the standard profile is the
native renderer target for matching the screenshot appearance. These profiles
drive the implemented D3D11 passes; focused reports in `evidence/` record what
each gate actually verified.

The supplied `bestsoldiercfg (1).zip` identifies the default HUD source as
`custom/m0rehud-5.8`, with Soldier-specific configuration layered on top. The
native HUD loader uses that resource layout; HUD panels, the logs panel and the
minimap draw through the native HUD path (`src/hud_*.cpp`, `src/playback_hud.cpp`).

## Build

From Git Bash (CMake, NMake, and MSVC on PATH):

```bash
bash build-cmake.sh                    # configure + clean Release build -> native/build-nmake/
bash build-target.sh tf2_demo_native   # incremental rebuild of one target
```

Where the Visual Studio generator does work, the preset remains available:

```powershell
cmake --preset windows-release -S native
cmake --build native/build --config Release
```

The release preset uses the static MSVC runtime and enables ASLR/NX. A
Windows 10/11 SDK and the MSVC C++ workload are required on the build machine.

The migration order was: shared binary parsers, replay timeline, D3D11 world
pass and lightmaps, StudioMDL skinning, particles/audio, native UI/HUD, then
export. `AssetRoot` is the native boundary for the existing `tf`
directory; it validates loose `materials`, `models`, and `maps` directories or
standard `*_dir.vpk` archives, and rejects path escape attempts. The former WebGL/Electron tree was removed from this product worktree on 2026-10-03; native code and the documented real-resource smoke checks are now the source of truth. Current implementation status is tracked in the task reports.

The native shell now indexes a base VPK at startup using bounded directory-tree
reads and reports the indexed entry count in the window title. A local TF2
installation indexed 104,041 entries during the smoke test; payload decoding is
kept on demand for the later material/model passes.

For machines with a nonstandard Steam install, launch with
`tf2_demo_native.exe --tf-root "X:\\SteamLibrary\\steamapps\\common\\Team Fortress 2\\tf"`.
This explicit path takes priority over automatic discovery. Open a recorded demo
with `--demo <path>`; the entity/TempEntity regression gate at the end of this
file accepts comma-separated paths.

Quality can be overridden for diagnostics with `--quality performance` or
`--quality standard`; `--vsync` and `--no-vsync` override presentation sync for
that launch without changing the saved settings.

Audio diagnostics can use `--audio-device N` to select a WinMM output device;
the default is `WAVE_MAPPER`. Use `--start-paused` for silent startup checks.
For a machine-readable one-second performance sample, pass
`--metrics-file X:\\path\\metrics.csv`. The process writes CSV columns for
elapsed seconds, rendered frames, interval FPS, playback tick, working-set
bytes, and private bytes; metrics are opt-in and do not open an audio device.
The runtime caches up to 32 MiB of decoded PCM data (at most 512 resources) and
otherwise resolves WAV resources on demand.

For a diagnostic frame grab, pass `--capture-frame X:\path\frame.bmp` with an
optional `--capture-tick N`. The back buffer is read back inside the draw call,
*before* `Present()`, because that is the only moment at which "the frame this
call composed" is still defined; the request is consumed once, and the process
then exits on its own with code 0, or with code 16 if the capture itself failed
(a capture failure is a diagnostic failure and deliberately does not touch the
device-error path). `--capture-tick` is parsed rather than defaulted: a value
that is not a non-negative integer exits 13 instead of silently capturing the
first frame. The file is an uncompressed 24-bit BMP with no colour management,
chosen so a gate can hash and measure it without an image library.

Run `powershell -ExecutionPolicy Bypass -File native/tools/native_resilience_gate.ps1
-Demo X:\path\sample.dem` to verify normal indexing, rejection of a truncated
Demo, and safe startup with an explicitly missing TF2 root.

For the current acceptance gate, run:

```powershell
powershell -ExecutionPolicy Bypass -File native/tools/native_acceptance_gate.ps1 `
  -InstallDir native/build-nmake -MissingTfRoot -Seconds 5 -MinSamples 3
```

The native UI state-machine probe is `native/build-nmake/native_ui_probe.exe`.
It verifies opening, import review, player transition, cancel recovery, tick
clamping, HUD toggle, and corrupt-Demo recovery.

## Release install smoke

Install the Release targets with `cmake --install native/build-nmake --config Release
--prefix install`. The layout contains `tf2_demo_native.exe`,
`native_install_smoke_probe.exe`, and `install_smoke_probe.ps1`; TF2 VPKs and
other game assets are intentionally excluded. Run
`powershell -ExecutionPolicy Bypass -File install_smoke_probe.ps1` from that
directory to verify both executables, required system DLLs, a diagnostic
missing-TF-root state, and D3D11 WARP fallback without opening an audio device.

Demo 元数据和命令边界索引使用文件流式读取，避免启动时将整个录像文件载入内存。

For entity and TempEntity regression across real samples, run
`powershell -ExecutionPolicy Bypass -File native/tools/native_demo_regression_gate.ps1
-Demo X:\path\pov.dem,Y:\path\sourcetv.dem` with comma-separated paths.
