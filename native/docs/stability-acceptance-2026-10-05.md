# Native Stability Acceptance Gate

`native/tools/native_acceptance_gate.ps1` is the repeatable gate for the
Native MVP. It combines the WARP smoke probe with a real process lifetime,
metrics sampling, optional Demo playback, missing-root fallback, minimum FPS,
and a working-set ceiling. It does not claim clean-machine validation.

## Examples

Build first:

```powershell
cmake --preset windows-release -S native
cmake --build native/build --config Release --parallel 2
```

Silent startup and WARP check:

```powershell
powershell -ExecutionPolicy Bypass -File native/tools/native_acceptance_gate.ps1 `
  -InstallDir native/build/Release -Seconds 5 -MinSamples 3 -RequireWarp
```

Real playback with a local TF2 root:

```powershell
powershell -ExecutionPolicy Bypass -File native/tools/native_acceptance_gate.ps1 `
  -InstallDir native/build/Release `
  -TfRoot 'D:/SteamLibrary/steamapps/common/Team Fortress 2/tf' `
  -Demo 'D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/demos/autorecord_2026-09-30_23-06-57.dem' `
  -Seconds 30 -MinSamples 20 -Play -RequireWarp
```

Missing resources must remain recoverable:

```powershell
powershell -ExecutionPolicy Bypass -File native/tools/native_acceptance_gate.ps1 `
  -InstallDir native/build/Release -Seconds 5 -MinSamples 3 -MissingTfRoot
```

To enforce a product target, add `-MinFps 120`. To enforce a local memory
budget, add for example `-MaxWorkingSetBytes 268435456` (256 MiB). These
thresholds are explicit inputs; a passing smoke run with no threshold does not
prove 120 FPS or a memory ceiling.

## Evidence boundary

The gate proves that the process survives the requested interval, writes valid
finite metrics, and respects any thresholds supplied to that invocation. The
current local evidence shows real playback around 103--107 FPS over short
runs, so `-MinFps 120` is expected to fail on this machine until the renderer
is optimized or a faster validation GPU is used. Clean Windows packaged startup
and sustained 120 FPS still require an external Windows validation host.

The 2026-10-06 run is recorded in `stability-acceptance-2026-10-06.md`.
