# 网页端验收记录（历史归档）

本文件保留 2026-09-29 以前 Electron/Vite/WebGL 版本的验收证据，仅供追溯，不能作为当前产品状态或发布依据。

当前产品已经转为 Windows 原生渲染器：源码入口为 `native/`，构建使用 CMake + MSVC，运行时使用 Win32 + Direct3D 11，直接读取用户本地 TF2 `tf` 目录。网页端源码、Node 依赖、网页构建产物和安装包副产物已从活树删除。

当前原生验收入口：

- `docs/TAKEOVER-2026-10-03.md`
- `docs/NATIVE-PRODUCT-CHECKLIST-2026-10-03.md`
- `native/README.md`

历史记录中的 `npm`、Electron、Vite、WebGL、`src/`、`scripts/` 和 `desktop-release/` 路径只表示旧产品当时的环境，不应按这些命令继续开发或验收。
