# TF2 Demo Player 产品与构建清单

## 1. 软件定位

Windows x64 独立 TF2 Demo 播放器。目标是直接打开 `.dem`，从用户本地的
Team Fortress 2 `tf` 目录读取地图、材质、模型、粒子和音频，并用原生
Direct3D 11 渲染，不依赖浏览器、Electron、Node.js 或 TF2 进程。

产品功能目标：暂停、快进、回退、时间轴跳转、自由视角、第一/第三人称、
摄像机预设、SourceTV/POV 识别、投射物和 HUD、片段导出，以及用户要求的
TF2/UGC 风格界面和可换主题。

## 2. 当前已验证

### 原生基础

- Win32 窗口和 D3D11 硬件设备初始化。
- D3D11.1 不可用时降级到 11.0，设备创建失败时回退 WARP。
- 双缓冲交换链、窗口缩放、每显示器 DPI 感知、120 FPS 主循环。
- 设备移除后最多自动恢复一次，并重新上传当前纹理与 BSP 世界顶点；恢复后的视觉连续性仍需真实设备移除场景验收。
- Performance/Standard 画质档位、FOV 40-120 归一化，默认 FOV 80。
- 设置原子写入到 `%LOCALAPPDATA%\TF2 Demo Player\settings.ini`。
- Steam 路径发现和 `--tf-root` 覆盖。
- Release 构建无动态 MSVC 运行库依赖；启动、资源发现、正常退出和设置保存已实测。

### 原生资源层

- `AssetRoot` 支持散装 `materials/models/maps` 和标准 `*_dir.vpk`。
- VPK v1/v2 目录树、预载数据、分卷数据和路径安全读取已接入。
- 本机 TF2 `tf2_misc_dir.vpk` 启动索引实测 `104041` 个条目。
- VTF 头、资源表、mip 链和高分辨率数据偏移解析已加入并通过 Release 编译。
- 原生 VMT 解析已接入真实 VPK/散装资源，支持常用 shader 字段。
- 原生 BSP 已支持 Valve LZMA lump 解压，并在 `2koth_abbey.bsp` 上解析出 68,271 个三角形。
- BSP 世界三角形已接入 D3D11 顶点缓冲和基础俯视绘制；当前使用调试灰度材质，未宣称真实光照。真实地图材质链已读通：`SOHO/FLOORTILE_001` VMT 对应 VTF 为 `1024x1024`。

### 已移除的旧网页端

- 2026-10-03 已删除 Electron/Vite/TypeScript/WebGL 源码、Node 依赖、网页测试、网页构建产物和历史 release 探针副产物。
- 删除前的网页验收记录仍保留在 Git 历史和本目录的交接文档中；它们不再是当前产品构建入口。

## 3. 当前进行中

1. 将 BSP 世界网格从调试灰度绘制升级为按 VMT 材质分组的纹理绘制。
2. 接入 lightmap、bumpmap、selfillum、Phong、envmap 和 Water 基础 shader。
3. 迁移 BSP 静态道具、天空盒和可见性裁剪。
4. 迁移 StudioMDL 顶点、骨骼、蒙皮、动作和 v_/w_ ViewModel。
5. 接入 Demo 解码时间线、玩家状态、相机和 SourceTV/POV 判定。

## 4. 尚未完成

- 原生 `.dem` 命令索引、网络消息扫描、实体快照和 TempEntity 结构化读取已接入窗口；完整 Source 状态重建、精确随机跳转和所有消息类型仍未完成。
- 原生 BSP 世界基础绘制、VMT/VTF atlas 和几何法线光照已接入；人物/武器 GPU 网格、蒙皮、动作、投射物轨迹、粒子和 HUD 尚未绘制。
- 原生 WAV 事件播放已接入，过滤语音/播报/音乐并支持设备选择；MP3 解码、空间衰减、变速同步和视频导出仍未完成。
- 原生时间轴已支持暂停、快慢速、倒放和 tick 快捷跳转；实体状态已可驱动观察焦点，但自由视角编辑、摄像机预设和 POV/SourceTV 完整切换仍未完成。
- 原生视频导出、安装包、自动更新、主题编辑器和 logs.tf 面板尚未迁移。
- 光照逐像素一致性、真实水面反射/折射和完整 PCF 算子仍需专项实现与实机验收。

## 5. 推荐构建顺序

1. **资源读取**：VPK + VTF/VMT + BSP pakfile，所有读取都限制在 TF2 根目录。
2. **纹理上传**：创建 D3D11 纹理、SRV、mipmap 和压缩格式路径。
3. **世界渲染**：BSP 面、lightmap、静态道具、天空盒和基础雾。
4. **模型渲染**：MDL/VVD/VTX、骨骼蒙皮、动作采样、ViewModel FOV 80。
5. **回放状态**：Demo parser、tick 采样、玩家/武器/投射物和视角控制。
6. **声音与粒子**：VPK WAV 解码、tick 同步、脚步/Uber/武器音效和常用 PCF。
7. **HUD 与导出**：UGC 风格 HUD、隐藏开关、主题系统、片段导出和封装。
8. **发布验收**：干净 Windows 安装、资源移动、缺失分卷、GPU/WARP、长时间播放、
   导出取消/失败和多 DPI 窗口测试。

## 6. 构建与验证命令

```powershell
cmake --preset windows-release -S native
cmake --build native/build --config Release --parallel 2
native/build/Release/tf2_demo_native.exe --tf-root "D:\SteamLibrary\steamapps\common\Team Fortress 2\tf"

```

## 7. 交付门槛

只有同时满足以下条件才能称为发布候选：原生 Demo 播放器能在无开发环境的
Windows 上打开真实 `.dem`；地图、材质、模型、HUD、声音和时间轴均使用真实资源；
导出片段首/中/末帧、尺寸、时长、音画同步通过；安装、卸载、资源目录移动和
GPU/WARP 回退均有实测记录。当前项目尚未达到该门槛。

## 材质采样回归记录

本轮尝试把地图 VTF 接入世界像素 shader 时，Release 启动出现进程退出；已回退到稳定的灰度世界 shader，真实 VMT/VTF 读取保留在资源层。下一轮必须先独立验证 D3D11 世界 shader 的输入布局、SRV 和初始化失败保护，再恢复纹理采样。


## 8. 本轮原生验证记录（2026-10-03）

- CMake + MSVC Release 构建通过，使用 Windows SDK 10.0.26100.0。
- 真实 TF2 资源启动通过：窗口保持响应，VPK 索引 104090 entries，BSP 解析 68271 tris，地图材质 `SOHO/FLOORTILE_001` 的 VMT/VTF 读取为 1024x1024。
- 世界顶点现在按已加载材质名选择纹理面，并按实际 VTF 宽高归一化 BSP texinfo UV；未匹配材质回退为稳定的灰度面。
- 该改动只证明资源绑定和启动稳定性，仍不等于完整地图材质、lightmap 或最终画质通过。


### Demo 文件头入口（已完成第一步）

- 原生支持 `--demo <path>`，校验 `HL2DEMO` 签名并读取地图名、协议、tick、帧数和播放时长。
- 自动录制样本和推车样本均已验证；带空格的 Windows 路径也已验证。
- 当前仍未解析 Demo packet、`svc_ServerInfo`、`PacketEntities`、`svc_TempEntities` 或时间轴，因此不能称为完整回放。


### Demo 命令流索引（已完成基础层）

- 支持协议 3/4 的命令前缀差异，建立命令 offset、tick 和 payload 边界。
- 两份真实 Demo 均已扫描通过；协议错位会被测试样本暴露，不能静默报成功。
- 下一步是把 packet payload 交给 Source 网络消息解码器，再建立可采样的玩家/相机状态。


### 低内存启动扫描

- Demo 头和命令索引已改为文件流式读取，避免按 Demo 文件大小增长的常驻缓冲。
- 本地 47.8 MB Demo 启动烟测工作集约 69.3 MB；完整模型、粒子和回放状态接入后仍需单独建立预算和长时稳定性测试。


### 网络消息扫描边界

统一消息长度探针已撤回，因为它不能覆盖 Source 的逐消息 schema；当前不宣称 svc、实体或 TempEntities 已解析。后续实现必须以专用消息读取器和真实 Demo 的变异验收为准。


### 专用网络读取器

- `net_Tick` 已按专用字段读取并在真实样本中验证；未知消息会安全停止当前 packet。
- `svc_ServerInfo` 的字段读取代码已建立，但尚未完成跨 Demo 的消息定位和完整字段验收。


### ServerInfo 当前边界

Print 已可消费；ServerInfo 消息边界已验证，但地图/服务器字符串字段仍需按 TF2 实际 bitbuf 字符串编码复核，暂不宣称 SourceTV 判定完成。


### ServerInfo 字符串验收

固定长度字符串语义已修正；两份真实 Demo 都能读出 ServerInfo 地图名，分别为 `cp_sunshine` 和 `pl_upward_f12`。

