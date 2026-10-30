# TF2 Demo Player Native

An experimental Windows-native Team Fortress 2 demo player and renderer built with C++17, Win32, and Direct3D 11.

**Status:** active development; not a release-ready product. This repository is public, but no license is provided, so public visibility does not grant permission to reuse the code.

## Project status

The public default branch is `p0-entity-protocol`. Since 2026-10-30 it carries the current software: the Win32/Direct3D 11 demo player with entity-protocol, world and material rendering, viewmodel and player animation, HUD and logs panel, audio and settings persistence, through the T75 work recorded in CHANGELOG.md. Older T50-T54 reports remain in `evidence/` as historical records.

World geometry/material rendering and third-person player models/animation are still under development. A passing focused gate is not a claim that every map, material, class, or animation is visually correct.

See [CHANGELOG.md](CHANGELOG.md) for the current hosted snapshot, [docs/KNOWN-ISSUES-AND-FIXES.md](docs/KNOWN-ISSUES-AND-FIXES.md) for verified failure modes and their status, and [docs/RELEASE-POLICY.md](docs/RELEASE-POLICY.md) for acceptance rules.

## Verification status

- The full `verify-all.sh` acceptance chain passed on the `ebc4716` release candidate (2026-10-30). The commits after it are source-split refactors, so re-run the chain on the final merged tree before treating it as the shipped acceptance record. `--quick` runs an 8-demo oracle sample while the full run walks the whole corpus.
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

`--demo` opens a recorded demo instead of the normal startup path; `--help` lists the other flags.

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
