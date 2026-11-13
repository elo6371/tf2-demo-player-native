# Stability acceptance run (module 6) 2026-10-06

Local measurements only. This file does not claim a clean Windows install or a
sustained 120 FPS.

## Build

`cmake --preset windows-release -S native` failed:

```text
Generator Visual Studio 17 2022 could not find any instance of Visual Studio.
```

There is no Windows SDK and no `cl.exe` on this machine. The two gate binaries
were produced with llvm-mingw Clang 23.1.2 (`llvm-mingw-20260922-ucrt-x86_64`)
and CMake MinGW Makefiles, `CMAKE_BUILD_TYPE=Release`, into the gitignored
directory `native/build-mingw-app`.

The mingw `DirectXMath.h` declares no `XMVECTOR`. The compile used the
Microsoft DirectXMath headers at `E:\008\third-party\DirectXMath` commit
`e2f2b9bddbbc4fd0f6f63586f86b2279213b991d` on the include path. `WINVER` and
`_WIN32_WINNT` were set to `0x0A00` so the existing
`SetProcessDpiAwarenessContext` declaration in mingw's `winuser.h` is visible
(`WINVER >= 0x0605`). The link line added `-municode` because the process
entry is `wWinMain`. `main.cpp`, `native_renderer.cpp`, and `audio_timeline.cpp`
were not edited.

`llvm-readobj --needed-libs` on `tf2_demo_native.exe` includes `d3d11.dll`,
`D3DCOMPILER_47.dll`, `WINMM.dll`, `libc++.dll`, and `libunwind.dll`. The last
two come from the llvm-mingw `bin` directory. `cmake --install` does not copy
them.

## Gate

`native/tools/native_acceptance_gate.ps1` now quotes any argument that contains
a space before `Start-Process`. Windows PowerShell 5.1 otherwise joins the
argument array with spaces, and `CommandLineToArgvW` splits
`Team Fortress 2`. An unquoted launch left the window title at
`Demo error: demo file cannot be opened`, loaded the fallback map
`maps/2koth_abbey.bsp` (180555 triangles), and kept the playback tick at 0.
Those earlier process samples are not demo playback. `play_mode` in the gate
JSON only means `-Play` was passed.

`-Play` waits up to 60 seconds for `WaitForInputIdle`. This demo reached the
message loop in 36857 ms. A paused smoke run still uses 10 seconds. A working-set
failure now also prints `average_fps` and `samples`.

## Machine

Video controllers reported by Windows:

- AMD Radeon(TM) Graphics, driver 32.0.21043.5001
- NVIDIA GeForce RTX 5070 Ti, driver 32.0.16.1714

A separate process called `D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE)`
and got feature level `0xb100` on `NVIDIA GeForce RTX 5070 Ti` (vendor
`0x10de`, device `0x2c05`). The player asks for `D3D_DRIVER_TYPE_HARDWARE`
before `D3D_DRIVER_TYPE_WARP`. The player does not record which path it took.
The acceptance process is started with `--no-vsync`. The frame loop still
targets `1/120` second.

TF2 root: `D:\Steam\steamapps\common\Team Fortress 2\tf`. The demo named in
`stability-acceptance-2026-10-05.md` is not on this disk. The run used
`tf\demos\2026-04-19_12-58-48.dem` (`HL2DEMO`, map `pl_upward`, 301.7 seconds,
20116 ticks).

## Commands and results

Paused WARP smoke, no demo:

```text
{"acceptance":"pass","samples":6,"average_fps":115.03,"peak_working_set_bytes":198492160,"play_mode":false,"missing_tf_root":false,"warp_probe":true}
```

Missing TF root, paused:

```text
{"acceptance":"pass","samples":6,"average_fps":114.43,"peak_working_set_bytes":197566464,"play_mode":false,"missing_tf_root":true,"warp_probe":true}
```

Real playback for 120 seconds with `-MaxWorkingSetBytes 268435456` (256 MiB),
`-MinSamples 60`, `-Play`, `-RequireWarp`. Wall clock 157.9 seconds, including
startup. Exit 1:

```text
working_set_above_threshold peak=369389568 threshold=268435456 average_fps=78.81 samples=118
```

The same demo for 15 seconds with `-MinFps 120`. Exit 1:

```text
fps_below_threshold average=44.92 threshold=120.00
```

A direct quoted launch, not the gate, kept its metrics file. After input idle
it sampled `tick=16` then `tick=230` (`playback=229/20116`, `playing`). The
window title included `Demo: pl_upward`, `BSP: 69109 tris`,
`state=source-equivalent`, and `entityModels=4`. The last kept interval was
`fps=14.3193` at `working_set_bytes=383004672`. The 15-second gate average and
the 120-second gate average are both below 120. They are not the same number
because each run's first intervals are slower.

`cmake --install native/build-mingw-app --prefix native/build-mingw-app/install`
copied `tf2_demo_native.exe`, `native_install_smoke_probe.exe`, and
`install_smoke_probe.ps1`. With the llvm-mingw `bin` directory on `PATH`,
`install_smoke_probe.ps1` printed:

```text
install_executable=1 tf_root_missing_prompt=1 warp_fallback=1 feature_level=b100
install_layout=ok tf2_assets_packaged=0
```

The same script with that directory removed from `PATH` failed:

```text
install_probe_failed exit=-1073741515
```

`-1073741515` is `0xC0000135`, a missing DLL. No clean Windows image was booted.

## Not claimed

- `-MinFps 120` failed on both real playback invocations above.
- The 256 MiB working-set ceiling failed. Peak on the 120-second run was 369389568 bytes.
- Clean-machine packaged startup was not run. This mingw build does not start from the install directory alone.
- `tf2_assets_packaged=0` means the install script did not ship TF2 assets.
- `source-equivalent` here is the existing lightmap label in the window title. No screenshot was compared with the game.
- The player process did not print its D3D driver type. Hardware versus WARP inside that process was not logged.
- Visual Studio was not used. The `windows-release` preset did not configure.
