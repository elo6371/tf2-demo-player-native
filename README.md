# TF2 Demo Player

当前产品入口是 `native/`：Windows x64、C++17、Win32 + Direct3D 11 的独立渲染器。

## 构建

```powershell
cmake --preset windows-release -S native
cmake --build native/build --config Release --parallel 2
```

## 运行

```powershell
native/build/Release/tf2_demo_native.exe --tf-root "D:\SteamLibrary\steamapps\common\Team Fortress 2\tf" --quality performance --no-vsync
```

软件直接引用用户本地 TF2 `tf` 目录，不需要 Electron、浏览器、Node.js 或启动 TF2。当前已接入 VPK、VTF、VMT 和压缩 BSP lump 解析；世界网格绘制、Demo 回放、模型、音频、粒子、HUD、导出和安装发布仍按 [docs/TAKEOVER-2026-10-03.md](docs/TAKEOVER-2026-10-03.md) 的顺序开发。
