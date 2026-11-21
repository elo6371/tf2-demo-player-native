# 参考实现库清单（强制优先对读）

本文件是"外部参考库"的唯一清单。**从 2026-11-21 起，本仓所有任务开工前先对读对应参考库**，交付里必须出现
"库 + 路径 + 文件 + 行号"以及它对应的本仓行号；没有对读的结论要显式写"未对读外部源码"。

## 0. 规则（violation = 退回）

1. 引用任何外部源码结论，必须写 **库 + 路径 + 文件 + 行号**，并指出它对应本仓哪一处（本仓文件 + 行号）。
2. 外部源码只能解释**语义**，不能证明"本仓现在是什么样"——本仓现状只认本仓行号与本仓文档里的字节读数。
3. 链接打不开、版本对不上（例如泄露源 branch 已删），就退回本仓 `docs/` 已有证据，并在交付里标注"未能对读外部源码"。
4. 外部库许可证与本仓不同，**一行代码都不许复制进来**；只许按语义重写。
5. 每个子任务的 prompt 里必须带上本文件路径，并要求在报告开头写"对读记录"小节。

## 1. 游戏本体源码（语义基准）

| 参考库 | 星 | 说明 |
|---|---|---|
| `https://github.com/OthmanAba/TeamFortress2` | 321 | 2020-04-22 泄露原版，最完整 |
| `https://github.com/sr2echa/TF2-Source-Code` | 80 | 同一份的镜像 |
| `https://github.com/lua9520/source-engine-2018-hl2_src` | 324 | 2018 年版泄露 |
| `https://github.com/NicknineTheEagle/TF2-Base` | 286 | **合法替代**：把老版 TF2 移植到官方开源的 `source-sdk-2013` 上 |
| `https://github.com/ValveSoftware/source-sdk-2013` | 10140 | Valve 官方开源的同代 Source 引擎与游戏层代码（首选） |
| `https://github.com/ValveSoftware/Source-1-Games` | — | Source 1 各游戏的游戏层代码（按分支发布） |
| `https://github.com/ValveSoftware/halflife` | — | HL1 SDK，只作老式粒子/精灵对照 |

## 2. Demo 解析与播放（`.dem` 语义）

| 仓库 | 语言 | 说明 |
|---|---|---|
| `https://github.com/demostf/parser` | Rust | 事实标准，demo.tf 官网在用 |
| `https://github.com/demostf/demo.js` | TypeScript | 解析规范的另一份实现，适合对拍字段语义 |
| `https://github.com/PazerOP/DemoLib2` | C++ | 完整重实现，最接近本仓技术栈 |
| `https://github.com/Nocrex/tf2-demo-player` | Rust | 播放器语义 |
| `https://github.com/programmer1o1/demoscope` | Rust | 通用 demo 解析 + 3D 可视化 |
| `https://github.com/wombo14/tf2-demo-analyser` | Rust | **2026-11-21 新增**，`Nocrex/demo-analysis` 的分叉、基于 `MegaAntiCheat/analysis-template-rust`；**build-4 带 3D 玩家渲染**（release 资产 `Demo-Analysis-with-3D-Player.zip` 17.1 MB / `Demo-Analysis.exe` 20.8 MB，2026-11-11），是本仓"demo → 世界 + 玩家模型渲染"这条路径最直接的对读对象 |

## 3. BSP / 地图解析

| 仓库 | 语言 | 说明 |
|---|---|---|
| `https://github.com/Galaco/bsp` | Go | 轻量 BSP 读取，适合核 lump 结构与边界 |
| `https://github.com/tasbox-org/source-parsers` | C++ | 完整 Source 格式解析，含 BSP/VPK；**位移面（dispinfo/dispvert/power）对读首选** |
| `https://github.com/pySourceSDK/ValveBSP` | Python | 便于快速比对某个 lump |

## 4. 物品 Schema / 本地化（HUD、装备显示）

| 仓库 | 说明 |
|---|---|
| `https://github.com/SteamTracking/GameTracking-TF2` | 自动追踪每次更新，schema 与本地化字符串都在 |
| `https://github.com/tsuza/tf2-item-schema` | 持续更新的 item schema |

## 5. 任务 → 参考库（当前在办）

| 任务 | 对读哪个 | 具体读什么 |
|---|---|---|
| task-9 退化位移面 / 绕序 | `tasbox-org/source-parsers`、`Galaco/bsp`、`source-sdk-2013` | vbsp 侧位移三角形发射顺序与 `bumpbasis/bump_t` 约定；对照本仓 `native/src/bsp_map.cpp:1242-1300`（绕序决策）与 `:1435-1546`（cell 判据与回滚） |
| task-30 N17 相机/自身身体 | `source-sdk-2013`（`c_baseplayer`/camera）、`TF2-Base` | 第一人称相机偏移与"自身实体不进世界通道"的语义；对照本仓 `native/src/main.cpp:695-703`、`:1347-1348`、`:13072-13084`、`native/src/native_renderer_frame.cpp:607-617` |
| task-31 HUD 面板/文字 | `source-sdk-2013`（`vgui`、`hud`）、`SteamTracking/GameTracking-TF2` | `%token%` 展开与面板绘制语义；对照本仓 `native/src/main_hud.cpp`、`native/include/hud_import.h:168-226` |
| task-29 音频 | `source-sdk-2013`（`sound`、`snd_`） | 设备枚举/恢复语义；对照本仓音频层 |
| `.dem` 字段语义 | `demostf/parser`、`demostf/demo.js`、`DemoLib2` | tick 语义、`temp_entities`、`usercmd`、字符串表 |
| 3D 玩家/世界一致性 | `wombo14/tf2-demo-analyser`（build-4 的 3D 模式） | demo 驱动世界 + 玩家模型的取景/骨骼/相机约定 |

## 6. 本仓证据文件（外部库之外的第二顺位）

- `docs/WORLD-HOLES-2026-11-17.md`、`docs/WORLD-DISP-PARAM-2026-11-17.md`：世界洞/位移面历史读数（**旧 pin 已过期，读数必须重测**）
- `docs/N17-SESSION-REPORT-2026-11-17.md`、`.scratch/n30-diag/report.md`：N17 相机与目视缺陷归因
- `.scratch/hudfix/FINDINGS.md`：HUD 面板/minimap 实证
- `analysis/rule-decision.txt`、`analysis/first-hit-oracle.txt`：绕序规则的 oracle 推导
