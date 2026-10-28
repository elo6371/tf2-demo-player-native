# TF2 Demo Player Native

An experimental Windows-native Team Fortress 2 demo player and renderer built with C++17, Win32, and Direct3D 11.

**Status:** active development; not a release-ready product. This repository is public, but no license is provided, so public visibility does not grant permission to reuse the code.

## Project status

The public default branch is `p0-entity-protocol` and contains the T50-T54 camera, HUD, team-outline, trajectory, and logs-panel work. Later development (including the T56+ audits and review-workspace work) exists in the local integration tree but has not been published to the default branch. Focused-gate results from unpublished work must not be treated as acceptance of the hosted tree.

World geometry/material rendering and third-person player models/animation are still under development. A passing focused gate is not a claim that every map, material, class, or animation is visually correct.

See [CHANGELOG.md](CHANGELOG.md) for the current hosted snapshot, [docs/KNOWN-ISSUES-AND-FIXES.md](docs/KNOWN-ISSUES-AND-FIXES.md) for verified failure modes and their status, and [docs/RELEASE-POLICY.md](docs/RELEASE-POLICY.md) for acceptance rules.

## Verification status

- T50-T54 have focused reports in `evidence/`; each report keeps its own PASS, NOT VERIFIED, and UNSUPPORTED boundaries.
- Later local work has focused reports, but its code and reports are not all present on the public default branch.
- T61.1 player animation has a focused implementation and mutation evidence in an unpublished task branch, but its recovery harness is still under review. It is not an accepted release gate.
- The full `verify-all.sh` chain is reserved for the final frozen tree and is not a substitute for feature-specific gates.

## Build

Requirements: Windows 10 or 11, Visual Studio 2022 with the C++ workload and Windows SDK, and CMake 3.24 or newer.

From a Visual Studio Developer PowerShell:

```powershell
cmake --preset windows-release -S native
cmake --build native/build --config Release
```

The application uses resources from a local Team Fortress 2 installation. Game assets are not included. To select a nonstandard `tf` directory, launch with:

```powershell
native/build/Release/tf2_demo_native.exe --tf-root "X:\SteamLibrary\steamapps\common\Team Fortress 2\tf"
```

## Verification

Focused checks live beside the repository root as `*-check.sh` scripts. Build and verification runs share machine resources and should be serialized. The full `verify-all.sh --quick` chain is reserved for the final frozen tree; see the release policy before running it.

## Repository layout

- `native/` — Windows application, renderer, parsers, and probes.
- Root `*-check.sh` scripts — focused acceptance and mutation gates.
- `evidence/` — selected captures and reports supporting specific findings.

## 中文说明

这是一个使用 C++、Win32 和 Direct3D 11 开发的 Windows 原生 TF2 Demo 播放与渲染实验项目。公开默认分支包含 T50-T54；更晚的本地开发尚未全部发布。地图几何/材质和第三人称人物模型/动作尚未完成全面验收。仓库公开不代表代码已授予再使用许可；当前没有附带许可证。
