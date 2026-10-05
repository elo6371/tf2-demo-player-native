# TF2 Demo Player Native Renderer

Current handoff: [`docs/TAKEOVER-2026-10-05.md`](docs/TAKEOVER-2026-10-05.md).
Use that file as the native branch entry point; older root-level handoffs are
historical records.

This is the new Windows-native rendering direction. It deliberately has no
Electron, browser, WebGL, Node.js runtime, or web UI dependency.

## Current stage

The first slice creates a Win32 window, initializes a hardware Direct3D 11
device and swap chain, resizes its render target, and presents a stable frame.
It is still an incremental renderer, but the native resource boundary now includes VPK, VTF, VMT, and compressed BSP geometry metadata parsing.

## Initial quality profiles

`RenderSettings::fromPreset` starts with two explicit profiles based on the
local TF2 configuration and the supplied 2560x1440 screenshots:

- `Performance`: `mat_picmip 2`, one-tap anisotropic filtering, no dynamic
  lighting, shadows, bumpmaps, specular or water reflections, model LOD 4.
- `Standard`: full-resolution textures, 8x anisotropic filtering, dynamic
  lighting, shadows, bumpmaps, specular, skybox and water reflections, model
  LOD 0.

Both profiles start with VSync disabled so a 120 FPS display target is not
artificially capped at 60 Hz; a future presentation option can enable VSync.

The performance profile mirrors `maxframes.cfg`; the standard profile is the
native renderer target for matching the screenshot appearance. These flags
are configuration inputs for the future D3D11 passes; the current shell only
clears and presents a swap chain.

The supplied `bestsoldiercfg (1).zip` identifies the default HUD source as
`custom/m0rehud-5.8`, with Soldier-specific configuration layered on top. The
native HUD loader will use that resource layout when the HUD pass is migrated;
the current renderer shell does not yet draw HUD panels.

## Build

From a Visual Studio Developer PowerShell:

```powershell
cmake --preset windows-release -S native
cmake --build native/build --config Release
```

The release preset uses the static MSVC runtime and enables ASLR/NX. A
Windows 10/11 SDK and the MSVC C++ workload are required on the build machine.

The migration order is: shared binary parsers, replay timeline, D3D11 world
pass and lightmaps, StudioMDL skinning, particles/audio, then native HUD and
export. `AssetRoot` is the native boundary for the user's existing `tf`
directory; it validates loose `materials`, `models`, and `maps` directories or
standard `*_dir.vpk` archives, and rejects path escape attempts. The former WebGL/Electron tree was removed from this product worktree on 2026-10-03; native code and the documented real-resource smoke checks are now the source of truth.

The native shell now indexes a base VPK at startup using bounded directory-tree
reads and reports the indexed entry count in the window title. A local TF2
installation indexed 104,041 entries during the smoke test; payload decoding is
kept on demand for the later material/model passes.

For machines with a nonstandard Steam install, launch with
`tf2_demo_native.exe --tf-root "X:\\SteamLibrary\\steamapps\\common\\Team Fortress 2\\tf"`.
This explicit path takes priority over automatic discovery.

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

Run `powershell -ExecutionPolicy Bypass -File native/tools/native_resilience_gate.ps1
-Demo X:\path\sample.dem` to verify normal indexing, rejection of a truncated
Demo, and safe startup with an explicitly missing TF2 root.

## Release install smoke

Install the Release targets with `cmake --install native/build --config Release
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
