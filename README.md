# TF2 Demo Player (native)

A Windows-native Team Fortress 2 demo (`.dem`) player and renderer written in C++17 with Win32 and Direct3D 11. It parses demos on its own (network packets, snapshots, entity tables) and draws the BSP world, entity and player models, the first-person view model, the sky and a HUD. It does not use the Source engine.

**Status:** experimental, for learning and research. Not a release-ready product.

## Requirements

- Windows 10/11, x64, a Direct3D 11 GPU
- Visual Studio 2022 (C++ desktop workload) and a Windows 10/11 SDK
- CMake 3.24 or newer
- Git for Windows (Git Bash); the build scripts are bash
- Your own Team Fortress 2 installation. **No game files are included**: the player reads maps, models, materials and sounds from the installation you point it at.

## Build

```bash
bash build-cmake.sh                      # configure (first time) + build everything
bash build-target.sh tf2_demo_native     # or build just the player
```

The player is written to `native/build-nmake/tf2_demo_native.exe`. The other executables built next to it are development probes.

## Run

```bash
native/build-nmake/tf2_demo_native.exe \
  --tf-root "C:/Program Files (x86)/Steam/steamapps/common/Team Fortress 2/tf" \
  --demo "path/to/your.dem"
```

Command-line options: [native/docs/cli-arguments.md](native/docs/cli-arguments.md). HUD formats: [native/docs/hud-supported-formats.md](native/docs/hud-supported-formats.md).

## License

Copyright (C) 2026 elo6371 and contributors.

This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3 (GPL-3.0-only). It is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See [LICENSE](LICENSE).

Third-party components: [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md). No Valve source code and no Team Fortress 2 game content are included. "Team Fortress", "TF2", "Source" and "Steam" are trademarks of Valve Corporation; this project is not affiliated with or endorsed by Valve.

---

## 中文说明

一个不依赖 Source 引擎的 Windows 原生 TF2 录像（`.dem`）播放器，使用 C++17、Win32 和 Direct3D 11 开发。它自己解析录像，并绘制地图、实体与玩家模型、第一人称视图模型、天空和 HUD。**仅供学习研究**，尚未达到可发布状态。

- **构建**：需要 VS2022（含 C++ 桌面开发）、Windows SDK、CMake 3.24+ 和 Git Bash。运行 `bash build-cmake.sh`，播放器输出在 `native/build-nmake/tf2_demo_native.exe`。
- **运行**：`tf2_demo_native.exe --tf-root "<你的 TF2 安装>/tf" --demo "<录像.dem>"`。**仓库不含任何游戏文件**，地图、模型、材质和声音都从你自己的 TF2 安装里读取。
- **许可证**：GPL-3.0（仅第 3 版），见 [LICENSE](LICENSE)。第三方组件见 [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md)。不含任何 Valve 源码，与 Valve 无关联。
