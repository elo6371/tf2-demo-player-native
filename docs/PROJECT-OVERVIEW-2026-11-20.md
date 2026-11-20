# 项目总览 —— 已实现 / 部分实现 / 未实现（2026-11-20）

写给"这个东西现在能干什么、还差什么"。口径是**本机可复现的证据**，不是卡片的愿望清单；
每一条后面跟的是要在哪读到它（脚本、文档或源码行）。没跑过的门禁不写 PASS。
基线：`work/integrate` HEAD `892e0056`（本地比 origin 多 3 个提交），本轮另有门禁改写未提交。

## 1 这个项目是什么

一个不依赖 Source 引擎的 TF2 demo 回放器：自己解析 `.dem`（网络包 + 快照 + 实体表），
自己用 D3D11 画 BSP 世界、实体模型、第一人称 viewmodel、天空与 HUD，用 Win32 开窗口，
把"看录像"这件事做到可精确到 tick、可抓帧、可门禁化的程度。产品是一个 exe（`native/build-nmake/tf2_demo_native.exe`）
加一批诊断探针与 142 个门禁脚本。

## 2 已实现（有读数/证据支撑）

| 领域 | 能力 | 证据锚点 |
|---|---|---|
| demo 播放 | POV/STV 两种录制都能解析；`--playback-speed`、`--start-paused`、`--capture-tick N`（"到或过 N 的第一帧"）、`--capture-frame` 抓 BMP | `demo-clock-check.sh`、`frame-capture-check.sh`；抓帧口径见 `.scratch/g7/cap-pov2.sh` |
| 时钟与快照 | `scene: clock`/`scene: snapshot` 冻结行、entry tick 与 server tick 分开记录、signon 块携带 server tick | `viewmodel-contract-check.sh` §A1；两套 tick 空间的口径见 `docs/GATE-REDS-FORENSICS-2026-11-19.md` |
| 相机 | first-person / third-person / free-look 三模式；`--debug-camera x y z pitch yaw`；观战 relay（`placeSpectatorView`，`native/src/main.cpp:1250`，写在 `:1347`）；demo 眼解析（`g_scenePovEye`，偏移 `offsetApplied`） | 今日 N18-B 三条读数：POV 帧 `md5 3bf37d4f…`、`cameraFilter removed=1 indices=9`；两条负控见 `docs/N18-FIRSTPERSON-VISIBILITY-2026-11-20.md` §6 |
| 世界渲染 | BSP brush 几何、位移面、tiled atlas（`native/src/world_tile_atlas.cpp:340-347`）、世界 PS 采样、VMT/VTF 从 VPK 链解析、雾 | G7 逐顶点 UV 修复 `26280880`；读数 `docs/STATUS-REPORT-2026-11-19.md` 第 13 条（默认关逐字节回退、开差 99,117 px） |
| 实体 | 实体模型加载、按脚本 schema 解析类名、二次染色修复、bind pose 与服务器动画驱动 | `entity_model.cpp`、`world-entity-tint-check.sh`；动画驱动 `animation-drive-states` PASS（9 次运行、负控 4/4） |
| 第一人称 | viewmodel 绘制链接通（`native_renderer_frame.cpp:2031` `drawViewModel`）、手臂与武器共用同一条 hand 链（`native_renderer_viewmodel.cpp:528-529` / `:821-822`）、蒙皮 58/58、NDC 构图 (0.40,-0.62)，**今日起第一人称不再画玩家自己的身体** | G8 `4b08984a`、G9 `7250df6c`、N18-B `b41a74f5`；`docs/N18-FIRSTPERSON-VISIBILITY-2026-11-20.md` |
| HUD / 总览 | `--hud-on/off/live`、`--hud-root/layout/scale`、`--overview-open/root/image/hide`、`--hud-minimap`；overview 曾取 231 断言 PASS（历史 exe 身份，不转移到当前 exe） | `hud-check.sh`、`map-overview-check.sh`；历史读数见 `docs/N-STATUS-2026-11-14.md` 2026-11-18 节 |
| 音频 | 音频设备选择（默认设备在本机是错的，门禁显式 `AUDIO_DEVICE=6`）、demo 内音频播放 | `playback-error-paths-check.sh`；**听感从未有人验收**（N21） |
| 失败契约 / CLI | 表驱动 CLI 已迁入 34 个开关（`native/src/main_cli.h`，34 条 `L"--` 表项），其余仍是 `main.cpp` 内的逐条比较；未知开关拒绝、缺参拒绝 | `argument-refusal-check.sh`；B1 `3d5a782a` 读数 `BUILD-IDENTITY=PASS assertions=222` |
| 构建 / 质量 | 全树 `/W4` 零告警（`WARN-GATE tus=139`）、七个既有目标另加 `/WX`（`native/CMakeLists.txt:88`）、身份门禁 222 断言、tier 1 CI 实跑绿（run `44479356155`，3 m 14 s） | `warn-check.sh`、`build-identity` 探针、`docs/STATUS-REPORT-2026-11-19.md` 第 10 条 |

## 3 部分实现（已接通但有具名缺陷）

| 领域 | 现状 | 缺口 / 下一步 |
|---|---|---|
| 第一人称武器体 | 武器体是从实体自己的 `m_iWorldModelIndex` 解出来的**世界模型**（`c_sniperrifle.mdl`），被搬到与手臂同一个 target | 两半各自把**自身 AABB 中心**搬到 target（`native_renderer_viewmodel.cpp:536-538`+`:578-579` vs `:826-834`），长枪的几何中心不在握把，所以"枪离手"。残差 `Δ = M_wpn(b_wpn − c_wpn) − M_arm(b_arm − c_arm)`（Gemini 第 10 轮复核）。修法：把 `:832-833` 的平移锚点换成 weapon_bone，而不是重挂骨骼链 |
| 玩家动画 | 默认动画已翻转为开（`fd25e93c`，`main.cpp:3331` `playerAnimationEnabled=true`）；idle/moving 选序器修好 | 只有 `kIdleNeedles`/`kMovingNeedles` 两组（`main.cpp:481-505`）；9/12-way blend、aimmatrix、LOD1+ 都没有；Scout 同机位视觉未验收 |
| 3D 天空盒 | `sky_camera` 实体**已解析**（`native/src/bsp_map.cpp:628-640` 写 `map.skyCamera`）；六面天空 + 雾在画 | 第二遍渲染与深度清除未实现；前提是先去掉 z 钉远平面（`native_renderer_frame.cpp:961-963`）；远离中心未量级压缩（文档口径） |
| 分屏 / pane | `--pane-mode` 与 pane 级相机隐藏（`g_paneSplit`/`g_pane1CameraHiddenEntity`，`main.cpp:12884-12885`）已在代码里 | N16 只完成契约（双 pane/PIP/四视角边界），产品没有真正的多视角流程 |
| 小地图 | 在线底图与单 CP 标记能画 | 底图与独立标尺未完成（N14）；Bagel 单 CP 不能判朝向 |
| DPI / resize | 布局数学 42 组合无越界 | 实测 HUD panel 不随 DPI 缩放，与状态栏相撞（最大 17.1%） |
| 七图矩阵 | 14 格矩阵有逐格记录 | 只有 2 格合格；六图 POV 零帧；证据跨 4 个 exe，没有同一 exe 覆盖七图（N11） |
| 世界画面残项 | 主要缺面根因已修 | pr600 绕序剔除仍有 94 px；14 个退化位移 patch 仍平面 fallback；N05 的 849/919 像素缺陷现象确证、机制未归因 |
| 推车 / 动态 brush | 静态盘点有记录 | 随时间移动、弯道、暂停/seek 未验（N09 部分 / NOT VERIFIED） |

## 4 未实现

| 卡 | 缺口 |
|---|---|
| N13 | HUD 身份与普通用户路径：**没有 broadcaster / follow / 选人命令**，缺的是模型层命令，不是按钮 |
| N16 | 多玩家分屏：PIP、四视角、真正的双 pane 流程 |
| N20 | 三类指定效果：Pyro 火焰、Medic 治疗束、流血，产品侧零实现 |
| N27 | 范围裁决与导出：MF 方案 C 已裁决，**产品导出未开始**；未安装组件 exit 13、长片音画同步 |
| N22 / N26 | CI 只覆盖 tier 1，`verify-all`/`verify-release` 未进 CI；构建缓存未做 |
| N29 / N30 | 干净环境走查与最终发布链：`verify-release.sh` 未运行，候选功能未冻结 |
| N28 | 集成批次与证据继承：exe 身份与旧 A/B 证据不一致，集成仍未闭合 |

## 5 工程债 / 门禁现状（2026-11-20）

- **门禁前提债今天修掉三条**（`docs/GATE-PREMISE-AUDIT-2026-11-20.md` #12/#13/#14；#14 是修的过程中发现的平台缺陷）：
  `weapon-body-check.sh` 原来钉的是**已不存在的** `autorecord_2026-07-02_13-26-46.dem`，现改钉
  `E:/demo测试/pl_upward_f12/POV/autorecord_2026-08-08_21-43-59.dem`（146,853,666 B、`14b4943a…`），
  实体常量按该录制实测改为 ViewModel 685 / weapon 810 / worldModelIndex 201 / `CTFClub` + `c_machete.mdl`，
  并把 tick 拆成两组：`CAPTURE_TICK=3000`（播放游标）与 `PROBE_TICK=5202`（server tick，trace 里成对出现 `tick=3000 entryTick=3000 serverTick=5202`）；
  run4 → `WEAPON-BODY=PASS`；
  `viewmodel-apply-check.sh` 的第 0 段原来只认 `root = steam`，现按 `steam/archive-pov/testdata` 映射路径、
  断言两条录制都验过、被测 demo 只取 POV 行，tick 保持 0（该门禁自带 `--viewmodel` 模型，不依赖武器链）；
  另外三处红按机制修（`sha256sum` 吞 stdin / 写死 demo 序列号 / 蒙皮姿态越出 bind 掩膜需 1 px 膨胀），
  无变异读数 `VIEWMODEL-APPLY=PASS (42 assertions, 0 failures)`，`--mutation` 读数 `PASS (14 assertions, 0 failures)`
  （四个变异各自动到该门禁读的数，m4 因此完成整门禁验证；首轮 m2 变异目标随 `3218e9bd` 的 HLSL 搬迁失效，见审计 #15）。
- **还欠的两条红**（要改门禁语义，等批准）：`viewmodel-contract-check.sh` 的 capture point 0≠7
  （premise `CAPTURE_SCENE_TICKS=10252` vs 实测 10320，根因是 T63-F1 的两套 tick 空间）、
  identity 的 1 px LSB（2026-11-20 复跑未复现）。
- **m4 mutation 目标已修**（`viewmodel-apply-check.sh` 三处指向 `native_renderer_viewmodel.cpp`），前向整门禁已绿，
  `--mutation` 复验（四个变异、约 40 分钟）在跑。
- **`sha256sum` 吞 stdin 是平台缺陷**：本机 `sha256sum` 是 WinuxCmd 1.2.1，给了文件参数仍读标准输入，
  把 `while read … < 钉住表` 的剩余行吃光 → 只核对第一行、其余行静默消失；全仓 12 处同构造（10 个脚本），
  本批只修被测的两处，其余 8 处列在审计 #14 待排。
- 142 个根目录 `*.sh`；`GATE-REGISTRY` 88 个门禁 / 已登记 35 / 排除 53；e/sidecar 身份门禁是唯一能证明
  "二进制是这个源建的"的那一层，任何提交前必须干净树。

## 6 建议顺序（一次一个）

1. 第一人称武器体：把 `native_renderer_viewmodel.cpp:832-833` 的锚点换成 weapon_bone，用同一 POV 帧前后对比（预期只动枪，不动手臂）。
2. 玩家动画：blend/aimmatrix 参数化 + 同机位 TF2 参考帧验收（N17）。
3. 3D 天空盒：先拆 z 钉远平面，再加第二遍 pass + 深度清除（D02）。
4. N05 849/919：给实际 draw 加 face/triangle/pass/UV/atlas 诊断，同输入下归因。
5. 门禁：两条既有红改语义 + m4 整门禁跑绿；然后把 verify-all 接进 CI。
6. 发布线：N29 干净环境 → N30 `verify-release.sh`。

## 7 这份总览不宣称什么

- 没跑过的门禁不写 PASS；除 `weapon-body-check.sh`/`viewmodel-apply-check.sh` 今天的两轮之外，
  其余门禁读数来自它们各自的证据窗口，不是当前 exe 的整门 verdict。
- 七图、HUD、overview 的历史 PASS 绑定旧 exe 身份，不继承到 `892e0056`。
- "已实现"指代码路径与至少一条读数存在，不等于"与真 TF2 观感一致"；观感类项目（N21 听感、人物外观、雾/天空接缝）都还需要人眼/人耳验收。
