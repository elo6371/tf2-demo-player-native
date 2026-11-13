# TF2 Demo Player Native

An experimental Windows-native Team Fortress 2 demo player and renderer built with C++17, Win32, and Direct3D 11.

**Status:** active development; not a release-ready product. This repository is public, but no license is provided, so public visibility does not grant permission to reuse the code.

## Project status

The public default branch is `p0-entity-protocol`. This checkout is the release HEAD of the T8/T9/T10 source-split merge (branch `integration/t79-final`); when `git rev-parse p0-entity-protocol` equals `git rev-parse HEAD`, the public branch carries the current software: the Win32/Direct3D 11 demo player with entity-protocol, world and material rendering, viewmodel and player animation, HUD and logs panel, audio playback and INI settings persistence (there is no settings screen in the product: `UiCommand::OpenSettings` exists at `native/src/native_ui.cpp:377` but no product code sends it; only `native/tools/native_ui_probe.cpp:340` does), including the T75 install lifecycle and the T80/T82/T83/T84 camera, PVS, brush-geometry and gate work recorded in CHANGELOG.md. Older T50-T54 reports remain in `evidence/` as historical records.

World geometry/material rendering and third-person player models/animation are still under development. A passing focused gate is not a claim that every map, material, class, or animation is visually correct.

See [CHANGELOG.md](CHANGELOG.md) for the current hosted snapshot, [docs/KNOWN-ISSUES-AND-FIXES.md](docs/KNOWN-ISSUES-AND-FIXES.md) for verified failure modes and their status, and [docs/RELEASE-POLICY.md](docs/RELEASE-POLICY.md) for acceptance rules.

## Verification status

- The last full `verify-all.sh --quick` chain completed on 2026-11-02 on HEAD `59d94a7`: 30 steps, 29 of them PASS. Step 27 went red before it measured anything — step 8's mutation suite restores the sources it patches, which moves their mtimes, so the gate's stale-binary guard refused the probe step 1 had built for being "older" than that tree's own sources (`git status` was clean and the source hashes matched the frozen set). `verify-all.sh` now rebuilds that probe at step 27, the way step 25 already rebuilds `model_pose_probe`; step 27 was then re-run alone on the same product sources and read `SPECTATE-CHECK=PASS`, 95 assertions, 0 FAIL. The chain was not run twice — `evidence/t66-sourcetv/CHAIN-RUN-20261102.md` states what ran, what was red, and what the single-step re-run did and did not repeat. `CHANGELOG.md` records the run and the gate work behind it.
- Two things that were NOT VERIFIED on the 2026-11-01 tree are now measured: `outline-check.sh` reads 121 PASS / 0 FAIL with 14 caught mutations (the camera fixture, the published-region test and the scalar depth view are in `evidence/t52-a3/`), and `spectate-check.sh` reads 95 PASS / 0 FAIL with 8 caught mutations (`evidence/t66-sourcetv/T66-CLOSEOUT.md`). Those two reports still carry their own remaining limits, and a green focused gate is not a claim about every map, material, class or animation.
- Focused reports live in `evidence/`; each report keeps its own PASS, NOT VERIFIED, and UNSUPPORTED boundaries.
- The hosted tree is a development snapshot: a passing focused gate is not a claim that every map, material, class, or animation is visually correct.

## Build

Requirements: Windows 10 or 11, Visual Studio 2022 with the C++ workload and Windows SDK, and CMake 3.24 or newer.

From Git Bash (CMake, NMake, and MSVC on PATH):

```bash
bash build-cmake.sh                    # configure + clean Release build -> native/build-nmake/
bash build-target.sh tf2_demo_native   # incremental rebuild of one target
```

`build-cmake.sh` explains why the Visual Studio generator cannot run on every host. Where the generator does work, the preset remains available:

```powershell
cmake --preset windows-release -S native
cmake --build native/build --config Release
```

The application uses resources from a local Team Fortress 2 installation. Game assets are not included. To select a nonstandard `tf` directory, open a recorded demo, and see the remaining flags:

```powershell
native/build-nmake/tf2_demo_native.exe --tf-root "X:\SteamLibrary\steamapps\common\Team Fortress 2\tf" --demo "X:\SteamLibrary\steamapps\common\Team Fortress 2\tf\demos\sample.dem"
```

`--demo` opens a recorded demo instead of the normal startup path. There is **no `--help`**: the argument loop (`native/src/main.cpp:2523-3034`) has no help table, so flags the player does not recognise are counted in the trace (`args: unrecognized=N ...`) and otherwise ignored; `--strict-args` refuses them instead, naming them on stderr and exiting 2 before a window appears — see [native/docs/cli-arguments.md](native/docs/cli-arguments.md). The only meta flags that exist are `--version` and `--install-status`, handled before the window is created (`native/src/main_lifecycle.cpp:23` and `:26`).

One limitation worth knowing before scripting a run: `--audio-device` is validated against the device count (`native/src/main.cpp:3003-3009`), and a malformed or out-of-range value exits with status 13 while the reason goes only to `OutputDebugStringW` (`native/src/main.cpp:3051-3054`). Nothing is printed on stdout and no dialog appears, so a headless script sees a bare exit 13.

The installer lifecycle answers without starting the product: `--install`, `--uninstall`, `--install-status` and `--version`. `--install-dry-run` -- alone or beside `--install` -- is the same install stopped after the plan it prints as `install=DRYRUN`; given beside any other mode it is refused with exit 2 rather than answering one of the two questions. The install manifest records its install directory as UTF-8, and a manifest written before 2026-11-07 in the machine's ANSI code page is still read, and reported, as `manifest: legacy-acp installDirectory`.

## Verification

Focused checks live beside the repository root as `*-check.sh` scripts and build into `native/build-nmake/`. The native PowerShell gates default to `native/build/Release`, so pass `-InstallDir native/build-nmake` explicitly. Build and verification runs share machine resources and should be serialized. `bash verify-all.sh --quick` runs the shortened oracle sample; `bash verify-all.sh` runs the full chain on a frozen tree; see [docs/RELEASE-POLICY.md](docs/RELEASE-POLICY.md) before running it.

## Repository layout

- `native/` — Windows application, renderer, parsers, and probes.
- `build-cmake.sh`, `build-target.sh` — build entry points; output lands in `native/build-nmake/`.
- `verify-all.sh` and root `*-check.sh` scripts — the acceptance chain and focused gates.
- `evidence/` — selected captures and reports supporting specific findings.
- `docs/` — release policy, known issues, gate traps, and the playbook.
- Root `T*-*.md` files are historical task reports, not current user documentation; start with this README, CHANGELOG.md and `docs/`.

## 中文说明

这是一个使用 C++17、Win32 和 Direct3D 11 开发的 Windows 原生 TF2 Demo 播放与渲染实验项目。公开默认分支为 p0-entity-protocol，自 2026-10-30 起包含当前软件（截至 T75 的工作）。构建入口为 `bash build-cmake.sh`（输出 `native/build-nmake/`），验收入口为 `bash verify-all.sh`。地图几何/材质和第三人称人物模型/动作的验收边界见 `docs/` 与 `evidence/` 各报告；仓库公开不代表代码已授予再使用许可，当前没有附带许可证。
