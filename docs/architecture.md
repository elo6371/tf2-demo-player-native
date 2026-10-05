# 原生架构与扩展约定

当前产品唯一入口是 `native/`：C++17、Win32、Direct3D 11。旧 Electron/Vite/WebGL 架构已废弃并从活树删除；本文件不再描述网页端模块。

## 模块边界

`native/include/asset_root.h`：TF2 资源根与路径安全
`native/include/vpk_archive.h`：VPK v1/v2 索引和分卷读取
`native/include/vtf_texture.h`：VTF 头、mip、压缩纹理解码
`native/include/vmt_material.h`：VMT KeyValues 与材质声明
`native/include/bsp_map.h`：BSP lump、LZMA、世界三角形
`native/include/texture2d.h`：D3D11 Texture2D/SRV
`native/include/native_renderer.h`：Win32/D3D11 设备、交换链和绘制
`native/include/settings_store.h`：本地设置原子读写

依赖方向固定为：资源解析 → 渲染资源 → D3D11 绘制 → Win32 外壳。解析层不得依赖窗口或 UI；渲染器不得直接扫描 TF2 根目录，所有资源都经 `AssetRoot` 读取。

## 新功能落点

1. Demo 容器、协议和 tick 状态放在 `native/include`/`native/src` 的独立模块，不塞进 `main.cpp`。
2. BSP 世界材质先完成按材质分组，再接 lightmap、bumpmap、Phong、selfillum、envmap 和 Water。
3. 模型系统独立于世界绘制，按 MDL/VVD/VTX、骨骼蒙皮、动作和 ViewModel 分阶段接入。
4. 音频、粒子、HUD、导出分别建立窄接口；不要恢复网页端的 Node 或浏览器适配层。
5. 每项真实 TF2 资源验证记录样本路径、地图版本、文件哈希和运行参数；构建成功不等于视觉验收通过。

## 构建与清理

`cmake --preset windows-release -S native`
`cmake --build native/build --config Release --parallel 2`

`native/build/` 和 `native/build-*/` 是可再生产物，禁止提交。发布前在干净目录重新构建，不从旧网页 `dist/` 或历史安装包恢复文件。
