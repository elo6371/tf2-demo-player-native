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

软件直接引用用户本地 TF2 `tf` 目录，不需要 Electron、浏览器、Node.js 或启动 TF2。当前 native MVP 已接入 VPK、VTF、VMT、压缩 BSP lump、PacketEntities 状态重建、基础地图/实体/投射物回放、Win32 UI 导入状态机和稳定性门禁；当前完整产品状态以 [native/docs/TAKEOVER-2026-10-05.md](native/docs/TAKEOVER-2026-10-05.md) 为准。

当前 UI 仍处于第一切片：支持 `.dem` 打开/拖放、导入审核状态、播放命令状态、tick 边界、HUD/静音/全屏状态和可恢复错误探针；实际 Win32 按钮、时间轴绘制、HUD 绘制、主题导入和设置页面仍由 `handoff-ui` 分支继续实现。
