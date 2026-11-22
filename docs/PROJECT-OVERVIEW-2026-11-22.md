# 项目总览 · 2026-11-22

> 这份文档回答五件事：这个项目做了什么、没做什么、路上撞到了什么、外部参考了什么、东西都在哪。
> 数字全部来自 D:/TF2_Native_Worktrees/t105-merge 上 task/merge-all 分支（head 7ec173a0，基线 work/integrate 390a7c76）的实测。

## 1. 一句话定义

一个不依赖 Source 引擎的 **TF2 录像播放器**：自己解析 .dem（网络包 + 快照 + 实体表），用 D3D11 画出 BSP 世界 / 实体模型 / 第一人称视图模型 / 天空 / HUD，开一个 Win32 窗口，把看录像这件事做成**逐 tick 可复现、可截图、可被门禁校验**。

**不是**：不是引擎，不跑服务器，不承诺与 Source 像素级一致。导出（MF 选项 C）尚未开工，CI 只有 tier 1。

仓库：git@github.com:elo6371/tf2-demo-player-native.git（公开但**未授予任何许可证**；公开 main 是 work/integrate 的快照，evidence/ 因为一个 GB 的截图没有推）。

## 2. 已实现（能跑、有门禁、有证据）

| 能力 | 说明 | 关键位置 |
| --- | --- | --- |
| 录像播放 | POV / STV 两种，--playback-speed、--start-paused、--capture-tick、--capture-frame | native/src/main.cpp |
| 时钟与快照 | 入口 tick 与服务器 tick 两行冻结输出，可逐帧对齐 | native/src/main.cpp |
| 摄像机 | 第一/第三人称、自由视角、--debug-camera、观察者中继 | placeSpectatorView native/src/main.cpp:1289 |
| 世界几何 | BSP 刷子面、位移面、func_brush/func_wall/func_lod 静态面 | native/src/bsp_map.cpp |
| 世界材质 | 瓦片图集、VMT/VTF 从 VPK 解出、雾 | native/src/world_tile_atlas.cpp:340-347 |
| 光照 | 烘焙 lightmap（lump 8，styles[0]）+ 全局模型光照 + leaf ambient 立方体（CPU 与 GPU 两条臂） | native/src/native_renderer_shaders.cpp、native/include/leaf_ambient_table.h |
| 实体 | schema 类名、tint 修正、bind pose、服务器动画 | native/src/entity_model.cpp |
| 第一人称视图模型 | 58/58 骨骼蒙皮，NDC (0.40,-0.62) | drawViewModel native_renderer_frame.cpp:2031 |
| HUD | --hud-on/off/live、--hud-root/layout/scale、--overview-open/root/image/hide、--hud-minimap | native/src/main_hud.cpp |
| 音频 | 设备选择 AUDIO_DEVICE=6、录像音轨播放 | native/src/*.cpp |
| 命令行 | 表驱动 37 个开关，未知参数 --strict-args 直接 exit 2 | native/src/main_cli.h |
| 构建纪律 | /W4 零警告、七个 /WX 目标、222 断言身份门禁、tier 1 CI 绿 | native/cmake/、verify-*.sh |

### 本轮六张卡（全部落地并已合并进 task/merge-all）

| 卡 | 内容 | 结果 |
| --- | --- | --- |
| task-36 | 模型光照第一步（basic 臂） | 已合并，引脚复现 |
| task-37 | leaf ambient 光照，CPU 半 + GPU 半 | **发现并修掉一个真实缺陷**（见 4.2） |
| task-38 | lightmap 图集尾块 | 已合并，补了 docs/LIGHTMAP-ATLAS-2026-11-22.md |
| task-39 | wearable 扫描门禁 | 已合并 |
| task-40 | wearable 归属（谁被谁画） | 已合并 |
| task-41 | 骨骼合并式 cosmetics | 已合并，off 臂与 bare 字节一致 |

合并规模：基线 390a7c76 到 head 7ec173a0 共 **253 个文件、+115174/-157 行**，其中 native/ 下 29 个文件、+7041/-138 行，零文件删除。

## 3. 未实现 / 只做了一半

### 3.1 明确没做（handover 表里的 N 项）

| 编号 | 内容 | 状态 |
| --- | --- | --- |
| N13 | 观察者方向：没有 broadcaster / follow / select 指令 | 未实现 |
| N16 | 分屏、画中画、四视图 | 未实现（只有 pane/split-screen 契约） |
| N20 | Pyro 火焰、Medic 治疗束、流血 | 未实现 |
| N27 | 产品导出（MF 选项 C 已定，未开工） | 未实现 |
| N22/N26 | CI 只有 tier 1 | 未做 |
| N28 | 集成收口（冻结候选的 exe/证据/门禁/录像/资产身份表） | 未做 |
| N29/N30 | 干净机器首启/装卸载、发布链 | 未做 |

### 3.2 做了但不完整

- **武器本体锚点**：现在用 AABB 中心，应为 weapon_bone（native/src/native_renderer_viewmodel.cpp:832-833，而非 :536-538 + :578-579）。
- **玩家动画**：只有 kIdleNeedles/kMovingNeedles 两个序列（native/src/main.cpp:481-505），没有 9/12 路混合、没有 aimmatrix、没有 LOD1+。
- **3D 天空盒**：实现但默认关闭，且**没有人签收**（N10 需要第二个正样本、同机位 origin/scale、主世界遮挡、2D/3D 叠放顺序、sky-cluster 负控）。
- **小地图**：只有七张表内地图有底图和比例，其它地图报 bounds-not-tabulated（例如 cp_snakewater）。
- **HUD 面板贴图**：未做；DPI 下面板最多重叠 17.1%。
- **世界残差**：pr600 上 94 px 的绕序剔除、14 个退化成平面回退的位移面、N05 的 849/919 未归属像素。
- **cart / 动态刷子**：未验证（N09）。
- **音频**：4096 个负载丢弃与真正的解码错误没有分开统计；换设备和 seek 没有恢复路径；**没有人实际听过声道分离和时序**。

## 4. 遇到的难题（按“差点把人骗过去”的程度排序）

### 4.1 同一个实体 ID 被读成了第一具尸体
m12 变异体一开始 28/0 全绿，实际上是把重复实体 ID 当成了先到的那一个。修法是**在采样之前**拒绝身份读数，不是采样之后去重。
变化过程：28/0 → 27/3 → 28/0。公开复盘：https://github.com/elo6371/coding-problem-playbook/blob/main/problems/2026-10-28-duplicate-entity-id-was-read-as-the-first-body.md

### 4.2 leaf ambient 表被“剪切”了，而且没人看得出来
这是本轮最硬的一个。源码里每个立方体是 72 字节（六个 float3），上传时却按 96 字节（六个 float4）拷贝，着色器也按 float4 行索引。后果有两个：

1. 每个 slot 都被**剪切**——着色器在 slot*24 + f*4 处读第 f 个面，源数据却写在 slot*18 + f*3；
2. 147 个立方体时 memcpy 从 10584 字节的 vector 里搬 14112 字节，**越界读 3528 字节**（约 36.75 个立方体），尾部 slot 拿的是堆上残留数据。

这就是为什么同一个 exe 连拍五次会给出**五个不同的 md5**，而 basic 臂稳定（根本不绑 t12）、trace 臂稳定（在 modelLightingRgb 之前就 return 了）、计数器稳定（数的是立方体身份，不是表里的字节）。

修法：结构体改成 std::array<std::array<float,4>, 6>，加 static_assert 把 sizeof 钉在 96，上传长度由同一个 kCubeBytes 常量推导。修完 leaf 臂连拍四次同一个 md5（fa297fb989bd4e2b3100bbd95cecb666），计数器一字不变，探针自检从 38 项变成 40 项仍然 0 失败。

**教训写进了 evidence/BUILDS.md**：先是一次构建差异被读成了代码差异，然后是同一支臂根本不可复现——因为那张表是剪切的。因此现在的 leaf 引脚绑的是**源码提交 + 复现次数**，而不是一个二进制。

### 4.3 两个相隔 1500 单位的摄像机拍出了字节相同的画面
--debug-camera 的第一版：两台摄像机相距 1500 单位，帧字节完全一样。修完差异 99.37%。这是“门禁本身在骗人”的典型——画面没错，是取景根本没变。

### 4.4 计数口径把人骗了三次
- 抓帧 tick 从漂移的 serverTickOrigin 10252 变成 10320，留下 11 个玩家 onScreen=0；
- 在一面红砖墙上数“队伍色像素”：开/关两帧字节完全相同，却数出 7607 个 team 像素；
- --world-depth 只改世界 pass，天空盒是另一个 pass，于是 same-camera nonGreyPct 从 27.61% 变成 0.00%，看起来“有效”其实测的是别的层。

### 4.5 门禁基础设施自己在坏（这一轮代价最大的一类）
- **sha256sum 是 WinuxCmd 1.2.1 的**，它会吃掉 stdin，12 个调用点全部补 </dev/null；
- **diff 也是 WinuxCmd**，它**打不开 /dev/fd**（这台 MSYS 根本没有 /dev/fd 这个目录），于是 bash 的进程替换在 diff 眼里是 0 字节，输出 0a1,18 加十八行 > ——第 7 步“探针输出可加性”因此长期假红，而冻结基线本身是对的（7437 字节、18 行、无 CR），基线在 390a7c76 和 7ec173a0 上提交的转录都是 PASS；
- **CRLF 引脚**：探针自己写 CRLF，而 verify-all.sh:403 用整行匹配去比 fixture_failures=0，于是第 3 步假红（git ls-files --eol 显示 i/lf w/crlf，core.autocrlf 现在其实没设）；
- **verify-all.sh 会自己抢 .scratch/verify-all.lock**，所以用 tools/lockrun.sh 去包它会死锁（VERIFY-LOCK=FAIL / RC=2），正确形状是 hostlock.sh 加 owner 再 bash inner；
- **变异体没被还原**：weapon-animation-check.sh 的两条 model 变异把 native/src/main.cpp 改脏之后没有立刻恢复，链路上后续任何一次 rebuild 都会去构建被变异的源码。这条必须在链跑完之后手工核对 git status --porcelain -- native/。

### 4.6 永远对不上的引脚
--audio-pov-fire 卡的前提**根本不成立**：那段录像是 SourceTV，实体 1 是广播摄像机，服务器从来没发过开枪者自己的声音。结论不是“没做到”，是“这件事不存在”。

## 5. 外部参考（docs/REFERENCE-REPOS-2026-11-21.md 是唯一权威清单）

规矩：每条结论必须写清 库 + 路径 + 文件 + 行号，再对到本地 file:line；**外部代码一行都不许抄**（许可证不同，只允许语义重写）；链接打不开就回退到仓库文档，并写“未能对读外部源码”。

| 用途 | 仓库 |
| --- | --- |
| 游戏源码（首选） | https://github.com/ValveSoftware/source-sdk-2013 （10140 星） |
| 游戏源码（合法替代移植） | https://github.com/NicknineTheEagle/TF2-Base （286） |
| 泄漏基线 / 镜像 | https://github.com/OthmanAba/TeamFortress2 （321）、https://github.com/sr2echa/TF2-Source-Code （80）、https://github.com/lua9520/source-engine-2018-hl2_src （324） |
| 其它 Valve | https://github.com/ValveSoftware/Source-1-Games 、https://github.com/ValveSoftware/halflife |
| 录像解析（事实标准） | https://github.com/demostf/parser （Rust）、https://github.com/demostf/demo.js （TS） |
| 录像解析（C++） | https://github.com/PazerOP/DemoLib2 |
| 其它播放器 | https://github.com/Nocrex/tf2-demo-player 、https://github.com/programmer1o1/demoscope 、https://github.com/wombo14/tf2-demo-analyser （build-4 有 3D 玩家渲染，与本项目最直接可比） |
| BSP | https://github.com/Galaco/bsp （Go）、https://github.com/tasbox-org/source-parsers （C++，位移面）、https://github.com/pySourceSDK/ValveBSP （Python） |
| schema / 本地化 | https://github.com/SteamTracking/GameTracking-TF2 、https://github.com/tsuza/tf2-item-schema |

## 6. 工作区、工具、Demo 在哪

### 6.1 工作区
- **主仓库**：D:/TF2_Native_Worktrees/t105-integrate（分支 work/integrate，050b98e8）——日常开发。
- **本轮合并树**：D:/TF2_Native_Worktrees/t105-merge（分支 task/merge-all，7ec173a0）——六张卡汇合处，验收在这里跑。
- 一共 **36 个 worktree**（D:/TF2_Native_Worktrees/t105-* 三十个、D:/TF2_Native_Test、D:/TF2_Native_Animation_Integration、t79/t91-integration、D:/tmp-b22-* 四个）。
- 规模：根目录 142 个 .sh（其中 88 个 *-check.sh）+ 56 个 .py + 12 个 .ps1；native/src 89 个 .cpp、native/include 73 个 .h、native/tools 55 个探针 .cpp；C++ 共 **85920 行**；evidence/ 下 **138 个目录**；docs/ 180 篇，其中 docs/reports/ 64 篇；git 跟踪 7413 个文件。
- 构建：bash build-cmake.sh（或 build-target.sh 加目标名），产物在 native/build-nmake/，需要 VS2022 + Windows SDK + CMake 3.24+。
- 四条铁律：新源文件**先 git add 再构建**（身份戳读 git index）；改过的源文件要 touch（nmake 看 mtime）；/W4 零警告且七个 /WX 目标不许抑制；构建和抓帧一律走 tools/lockrun.sh。

### 6.2 工具（tools/，独立工具不是门禁）
analyze-full-tree.sh、analyze-one-target.sh、asan-build.sh、asan-model-material.sh、audit-tree.sh、bsp_face.py、check-nested-arith.sh、d01-colour0-readback.sh、d01-compose-probe.sh、display-mode-probe.ps1、fault-seam-check.sh、key-dispatch-probe.ps1、lockrun.sh、n11-seven-map-matrix.sh、n17_camera_metric.py、tf2-toolchain.sh、vpk_dump.py、vpk_lookup.py、warn-baseline.tsv、warn-check.sh。

单跑一条门禁就用根目录的 *-check.sh；全套是 verify-all.sh（30 步）/ verify-fast.sh；发布另有 verify-release.sh。

### 6.3 Demo 位置（**最容易搞错的地方**）

| 位置 | 内容 |
| --- | --- |
| **E:/demo测试** | 11 个地图目录、**18 个 demo**、1.30 GB。cp_gullywash_f9 2、cp_process_f12 3、cp_reckoner 1、cp_sultry_b8a 2、cp_sunshine 2、koth_ashville_final1 1、koth_bagel_rc11 2、koth_product_final 2、pl_upward 1、pl_upward_f12 2；每图分 POV/ 与 STV/。**这是抓帧引脚用的那一套。** |
| **D:/TF2_Demo_Player** | 门禁夹具根：testdata/demos 只有 **4 个** demo（bagel_rc13、upward_f12、snakewater、bagel_rc11），.scratch/tf2-demo-parser/test_data 下 **11 个小夹具**（comp、decal、protocol23、saytext2、small 等）。 |
| **D:/SteamLibrary/.../tf/demos** | **992 个** autorecord_2026-08-*.dem，24 GB，全是 8 月的自动录制——**不是**引脚要的那一套。 |

**引脚用的那一个**：E:/demo测试/cp_process_f12/STV/1338f5de79ea343682c17dd514c980d9_matcha-20260927-1312-cp_process_f12.dem（273442 ticks，100302450 字节），抓 tick 600、1264x681、2582406 字节。

注：有 14 个被引用的 demo（73.dem 和 13 个 autorecord_2026-05/06/07 的）**在这台机器上已经不存在了**，这是 verify-all 第 5 步和第 9 步假红的唯一原因。

### 6.4 资产
TF2 本体：D:/SteamLibrary/steamapps/common/Team Fortress 2/tf（tf/maps 下 261 个 .bsp）。程序**不捆绑任何资产**，必须用 --tf-root 指过去。

## 7. 验收现状（诚实版）

- **构建**：合并树 /W4 **零警告**，exe sha256 c6bebfcd…，13 次抓帧与 4 个探针自检用的都是这一个可执行文件。
- **核心引脚**：bare 2ddade35…、basic d927c526…、leaf fa297fb9…（四次一致）全部在 tick 600 复现。
- **探针自检**：world_material_probe 75 项全真、leaf_ambient_probe 40 项 0 失败、demo_open_probe 16+10 全过。
- **30 步链**：本轮跑出一批红步，绝大多数**不是**回归，而是这台机器上输入缺失或门禁自身失效（第 5/9 步缺 demo、第 3 步 CRLF 整行匹配、第 7 步 diff 打不开 /dev/fd、第 8 步变异套件在 14:16 之后编译失败）。逐条判定（A 机器漂移 / B 门禁过期 / C 真回归）由只读取证代理给出。
- **引脚能证明什么、不能证明什么**：evidence/BUILDS.md 把每个引脚绑到具体提交和出处（sidecar / 门禁打印 / 运行打印），并写明“像素引脚不等于代码正确”。

## 8. 想看细节去哪

- 总入口：docs/HANDOVER-2026-11-21.md（0 概览 / 1 定义 / 2 构建与门禁 / 3 代码与证据地图 / 4 提交与看板 / 5 交付 / 6 未完成 / 7 计划 / 8 交接顺序 / 9 规矩 / 10 索引）。
- 六张卡：docs/COSMETICS-2026-11-22.md、docs/LEAF-AMBIENT-2026-11-21.md、docs/LEAF-AMBIENT-GPU-2026-11-22.md、docs/LIGHTMAP-ATLAS-2026-11-22.md、docs/WEARABLE-ATTRIBUTION-2026-11-21.md、docs/WEARABLE-SCAN-2026-11-21.md、docs/GATE-UNBLOCK-2026-11-22.md。
- 引脚出处：evidence/BUILDS.md。历史引脚：evidence/leaf-ambient-gpu/retired-pins.txt。
- 踩坑复盘：docs/KNOWN-ISSUES-AND-FIXES.md。
