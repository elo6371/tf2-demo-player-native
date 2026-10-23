# TF2 Demo Player Native

An experimental Windows-native Team Fortress 2 demo player and renderer built with C++17, Win32, and Direct3D 11.

**Status:** active development; not a release-ready product. This repository is public, but no license is provided, so public visibility does not grant permission to reuse the code.

## Project status

The default branch is `p0-entity-protocol`. The integration branch contains the native playback/rendering work and the T50 camera and T51 HUD features, whose focused gates passed in the 2026-10-23 integration run. Follow-up work for team outlines, projectile trajectories, and the logs panel is still being reviewed and is not fully represented by the default branch.

World geometry/material rendering and third-person player models/animation are still under development. A passing focused gate is not a claim that every map, material, class, or animation is visually correct.

See [CHANGELOG.md](CHANGELOG.md) for the current integration snapshot and [docs/RELEASE-POLICY.md](docs/RELEASE-POLICY.md) for acceptance rules.

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

这是一个使用 C++、Win32 和 Direct3D 11 开发的 Windows 原生 TF2 Demo 播放与渲染实验项目。项目仍在开发中：地图几何/材质和第三人称人物模型/动作尚未完成全面验收，T52/T53/T54 的后续改动也尚未完整进入默认分支。仓库公开不代表代码已授予再使用许可；当前没有附带许可证。
