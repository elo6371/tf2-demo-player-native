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

当前 UI 已有第一版 Win32 操作层：支持 `.dem` 打开对话框/拖放、导入审核、播放/暂停、停止、逐 tick、反向和时间轴拖动；HUD 主题、完整 HUD 绘制、设置页和导出仍未完成。大 Demo 的导入扫描目前在窗口线程同步执行，导入取消和后台进度属于下一 UI 增量。

## 接手入口

接手任何任务前必须先阅读 [`HANDOFF-2026-10-09.md`](HANDOFF-2026-10-09.md)。所有修改必须在隔离工作区完成，禁止直接编辑主线工作区；提交必须附真实资源、构建命令、运行命令、原始输出和未验证项。

## 分层验收

小任务使用 `verify-fast.ps1`、`verify-entity.ps1`、`verify-render.ps1` 或 `verify-audio.ps1` 中与改动对应的一项。完整协议/发布验收仍在独立测试仓库 `D:\TF2_Native_Test\verify-all.sh`，不要每个小任务重复运行完整链。

当前已整合 P0 主循环等待和 UI/标题缓存；实体属性预检查已经在隔离测试树验证，但因主线实体语义不同暂未直接整合。动画、ViewModel、地图高级材质、音频时序、相机行为和发布验收仍未完成。
