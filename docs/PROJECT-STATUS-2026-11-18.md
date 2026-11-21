# 项目功能实现情况 — 2026-11-18

本文回答一个问题：**这个东西现在到底能做什么、哪些是已经验证过的、哪些还不能说**。
逐卡缺陷与验收细则仍以 [N-STATUS](N-STATUS-2026-11-14.md) 和
[HANDOVER-2026-11-21](HANDOVER-2026-11-21.md) 为准，本文是它们的摘要，不覆盖其结论。

## 一句话

Windows 原生 TF2 demo 播放器与渲染器（C++17 + Win32 + Direct3D 11）：能打开 demo、
按 tick 播放，渲染世界几何与材质、第三人称实体、第一人称武器、HUD 与音频，并有一套
142 个脚本的门禁体系。**它不是一个能替代 TF2 客户端的成品**，视觉正确性按子系统分别签收，
目前世界渲染接近收口、角色动画与部分 UI 仍未闭环。

11-18 续作：最新执行顺序、N00–N30接续步骤与架构任务以
[全项目连续执行清单（2026-11-21 已并入交接报告）](HANDOVER-2026-11-21.md) 为准；本轮已修 companion
循环位丢失并做九职业验证，默认动画/完整Scout外观仍未闭环。

## 代码规模

| 项目 | 数量 |
|---|---|
| 产品源码（`native/src` + `native/include`） | 90 个文件、约 75,294 行 C++ 与 HLSL 字符串 |
| `native/` 全部（含探针与测试） | 278 个文件、182,064 行 |
| 探针工具 `native/tools/` | 54 个 |
| 门禁脚本（仓库根） | 142 个 |
| 文档 | 123 篇 + 63 篇历史报告 |
| 证据 `evidence/` | 6,173 个文件、约 1.0 GB |

## 分支现状

| 分支 | 内容 |
|---|---|
| `work/integrate` | 当前开发线，11-18续作开工本地 `3e3f47f3`，远端实查 `30a2fcfb`；渲染修复 `daa607e0` 已含 |
| `main` | 11-18公开快照 `98ddaeba`，tag `snapshot-2026-11-18` 同指该提交；788文件、不含evidence，native与续作前integrate一致 |
| `p0-entity-protocol` | 远端11-13交接快照 `6f475caf`，与本机该名称工作区的推进状态分开记 |
| `archive/full-history-2026-11-13` | 历史全量归档 |

上述main与远端integrate身份已用GitHub refs/fetch复核；后续循环位修复不自动进入main快照。
保留开发历史，不强推重写；archive引用旧blob时完整clone仍包含历史大对象，需要轻量代码副本可浅克隆main。

## 已实现且能跑通的子系统

1. **demo 播放与协议层**：`.dem` 解析、entity protocol、packetentities/snapshot 解码、
   server class 表、PVS、logs（击杀/回合）、按 tick 定位与播放。
2. **世界几何与材质**：BSP 面与位移贴图、VMT/VTF 材质链、lightmap、detail、
   六面天空盒、雾、水体、`$alphatest` 镂空、多材质 atlas 合批。
3. **实体与角色**：MDL/VVD/VTX 模型加载、骨骼蒙皮与矩阵绑定、动画序列选择、
   LOD 与 `renderMode` 处理。
4. **第一人称 ViewModel 与武器**：手臂与武器绘制、材质、多 range 手部。
5. **UI**：HUD（血量/弹药/计分/时钟）、日志面板、多 pane、地图总览、DPI 布局。
6. **音频**：内容类别、真实设备输出、按 tick 调度。
7. **相机与观战**：POV/STV 眼高、跟随与自由视角、调试相机、tick 轨道。
8. **安装与资源**：安装生命周期、VPK 构建、资源发现与缺失时的明确失败路径。
9. **工程**：CMake/NMake 构建、门禁套件、变异测试、`verify-all.sh` 整链。

## 各子系统实现状态（据 N-STATUS 2026-11-17/18）

| 子系统 | 状态 | 未闭环的部分 |
|---|---|---|
| 世界画面（N06/N07） | 主要缺面根因已修 | 仍有 pr600 绕序残点、退化位移面、world-material 基线 |
| 天空/位移（N10） | 报告级局部 PASS | `sky_camera` 路径 UNSUPPORTED，3D skybox 未实现 |
| 相机（N12） | A/B与POV精确轨道有历史局部证据 | STV relay精确轨道消费、身份/包顺序和当前exe复验未闭环；COINCIDENT不算绿 |
| 角色外观（N17） | 投影、idle选序、helper与companion循环位已窄修 | **默认仍是 bind pose**；LOD/UV/material、完整pose/blend和同机位外观未签收；其它工作区WIP未合入 |
| 第一人称/武器（N18） | 独立未验 | 第三人称闭环不替代 arms/viewmodel 验收 |
| UI（N13–N16） | 侦察与契约为主 | 没有 follow/选人命令；DPI/HUD 遮挡未过；多视角只有契约 |
| 音频（N21） | 覆盖报告 | 听感需要人在场 |
| 工程（N22–N26） | 全树 `/W4` 与身份门禁已落地 | `/analyze` 只测过一轮且不在门禁/CI 里；x64 ASan 已装并取两条路径读数（2026-11-19）；五门禁里身份门禁与 animation-drive-states 已有真 PASS（后者 2026-11-19 复跑、负控 4/4）；CI 未远端跑；`verify-all` 未端到端跑完 |
| 导出/发布（N27–N30） | 契约已裁决 | 产品内导出未实现；干净环境走查与 release 链未跑 |

## 2026-11-18 修掉的四个渲染缺陷

| 缺陷 | 根因 | 修复 | 实测 |
|---|---|---|---|
| 全地图左右镜像，模型像被掏空 | 视图基把游戏左侧画到屏幕右侧；实体三角形是按左手裁剪空间绕序写的 | 投影 clip-X 取反 + `FrontCounterClockwise` 成对翻转；模型加载时对调三角形后两个索引 | `TF2_NATIVE_FLIP_X` A/B 两帧精确水平镜像，翻转后中位像素差 0.0 |
| 远距离世界贴图闪动 | 世界 atlas 没有 mip 链 | 新增 `Texture2D::createMipped`，`GENERATE_MIPS` 生成到 1x1 | — |
| 铁丝网逐像素椒盐/黑点 | 像素着色器把**插值后的** feature float 直接 `(uint)` 截断，mask 16 变成 15：`$alphatest` 位丢失、`detail` 位被点亮采样黑色填充 | 三处改为四舍五入 `(uint)(x + 0.5)` | 实测区域纯黑 15,595 px（39.9%）→ 0，行向高频 154.8 → 3.3，全帧纯黑 94,043 → 700 |

诊断开关保留在构建里（不设环境变量即完全惰性）：`TF2_NATIVE_WORLD_VIEW=11..18`、
`TF2_NATIVE_DUMP_ATLAS`、`TF2_NATIVE_HIDE_MATERIAL`、`TF2_NATIVE_ONLY_MATERIAL`、
`TF2_NATIVE_FLIP_X`、`TF2_NATIVE_MODEL_WINDING`。

## 明确**不能**说的

- 不能说「任意地图/材质/职业都渲染正确」：七图矩阵 14 格目前只有 2 格合格。
- 不能说「角色动画已闭环」：默认启动不开玩家动画，仍是 bind pose。
- 不能说「整链门禁全绿」：`verify-all.sh` 没有在当前树上端到端跑完；
  最近一次全链是 2026-11-02 在 `59d94a7` 上。
- 不能说「可以发布」：N29/N30 阻塞，没有干净环境走查。

## 怎么构建、怎么验

```bash
bash build-cmake.sh                    # 配置 + Release 构建 → native/build-nmake/
bash build-target.sh tf2_demo_native   # 增量构建产品
bash verify-all.sh --quick             # 整链门禁（约 2.7 小时）
```

## 下一步优先级

1. 角色默认动画（bind pose → 可验证的 idle/pose/blend），这是当前最大的功能缺口。
2. 世界残项：pr600 绕序残点、退化位移面、world-material 基线。
3. 架构效率与功能并行：N25证据/捕获复用→N26构建实测→N22/N23工具链/警告→N24主循环小片拆分；普通更新开发分支，保留历史，main快照不冒充发布。
