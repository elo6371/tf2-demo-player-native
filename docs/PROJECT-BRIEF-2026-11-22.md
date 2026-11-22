# TF2 录像播放器（原生实现）· 项目简报

> 面向不熟悉这个仓库的读者。所有数字都来自 2026-11-22 在 `D:/TF2_Native_Worktrees/t105-merge`（分支 `task/merge-all`）上的实测。
> 一句话：**一个不依赖 Source 引擎、自己解析 .dem 并用 D3D11 直接画出来的 TF2 录像播放器**，目标是"逐 tick 可复现、可截图、可被门禁校验"。

- 仓库：`git@github.com:elo6371/tf2-demo-player-native.git`
- 许可证：**GPL-3.0-only**（2026-11-22 起；根目录 `LICENSE` + `THIRD-PARTY-NOTICES.md`）
- 分支：`task/merge-all`，head `7f6818ef`；基线 `390a7c76`（`work/integrate`）
- 规模：89 个 `.cpp` 在 `native/src`、73 个 `.h` 在 `native/include`、55 个探针在 `native/tools`，约 8.6 万行 C++；`git ls-files` 7413 个文件

---

## 1. 它是什么，不是什么

**是**：一个 Win32 程序。读 TF2 的 `.dem`（网络包 + 快照 + 实体表），自己重建世界与实体，用 D3D11 画出 BSP 世界、实体模型、第一人称视图模型、天空、HUD，并支持逐 tick 冻结、抓帧、命令行驱动。

**不是**：不是引擎，不跑服务器，不承诺与 Source 像素级一致；不捆绑任何游戏资产（运行时用 `--tf-root` 指向你自己的 TF2 安装）。

---

## 2. 已实现（能跑、有门禁、有证据）

| 能力 | 说明 | 关键位置 |
| --- | --- | --- |
| 录像播放 | POV / STV；`--playback-speed`、`--start-paused`、`--capture-tick`、`--capture-frame` | `native/src/main.cpp` |
| 时钟与快照 | 入口 tick 与服务器 tick 两行冻结输出，可逐帧对齐 | `native/src/main.cpp` |
| 摄像机 | 第一/第三人称、自由视角、`--debug-camera`、观察者中继 | `placeSpectatorView` `native/src/main.cpp:1289` |
| 世界几何 | BSP 刷子面、位移面、`func_brush`/`func_wall`/`func_lod` 静态面 | `native/src/bsp_map.cpp` |
| 世界材质 | 瓦片图集、VMT/VTF 从 VPK 解出、雾 | `native/src/world_tile_atlas.cpp:340-347` |
| 光照 | 烘焙 lightmap（lump 8）+ 全局模型光照 + leaf ambient 立方体（CPU / GPU 两条臂） | `native/src/native_renderer_shaders.cpp`、`native/include/leaf_ambient_table.h` |
| 实体 | schema 类名、tint 修正、bind pose、服务器动画 | `native/src/entity_model.cpp` |
| 第一人称视图模型 | 58/58 骨骼蒙皮，NDC (0.40,-0.62) | `drawViewModel` `native_renderer_frame.cpp:2031` |
| HUD | `--hud-on/off/live`、`--hud-root/layout/scale`、`--overview-*`、`--hud-minimap` | `native/src/main_hud.cpp` |
| 音频 | 设备选择 `AUDIO_DEVICE=6`、录像音轨播放 | `native/src/*.cpp` |
| 命令行 | 表驱动开关（现 38 个），未知参数 `--strict-args` 直接 exit 2 | `native/src/main_cli.h` |
| 构建纪律 | /W4 零警告、七个 /WX 目标、222 断言身份门禁、tier 1 CI | `native/cmake/`、`verify-*.sh` |

**最近一轮（六张卡，全部合并）**：模型光照第一步、leaf ambient 光照（CPU+GPU）、lightmap 图集尾块、wearable 扫描门禁、wearable 归属、骨骼合并式 cosmetics。
合并规模：`390a7c76` → `7ec173a0`，253 个文件、+115174/-157 行，零文件删除。

---

## 3. 还没做 / 只做了一半（最该被人知道的）

### 3.1 明确没做
| 编号 | 内容 |
| --- | --- |
| N13 | 观察者方向：没有 broadcaster / follow / select 指令（**第一步已写**：`--follow-sticky`，跟随的人复活后自动继续跟随，默认关，未编译） |
| N16 | 分屏、画中画、四视图 |
| N20 | Pyro 火焰、Medic 治疗束、流血（**第 0 步已写**：`--temp-census` 特效事件截断计数，默认关，未编译） |
| N27 | 产品导出（选项已定，未开工） |
| N22/N26 | CI 只有 tier 1（**已加第二个 strict job**，未提交） |
| N28–N30 | 集成收口、干净机器首启/装卸载、发布链 |

### 3.2 做了但不完整
- **玩家动画**：只有 `kIdleNeedles`/`kMovingNeedles` 两个序列（`native/src/main.cpp:481-505`），没有 9/12 路混合、没有 aimmatrix、没有 LOD1+。
- **武器锚点**：AABB 中心已不再使用；默认 `--viewmodel-weapon-attach attachment`（`native/src/native_renderer_viewmodel.cpp:886-910`），`bone` 模式也已实现（`:911-931`）但非默认。剩下是跑一组 attachment/bone 的 A/B 再定默认。
- **3D 天空盒**：实现但默认关闭，没有人签收（缺第二个正样本、同机位 origin/scale、遮挡、2D/3D 叠放、负控）。
- **小地图**：原只有七张表内地图有底图；**新增** `--hud-minimap-bounds bsp` 用刷子几何推边界（默认关，未编译），面板与状态栏在 DPI 下最多重叠 17.1%。
- **HUD 面板贴图**：未做。**世界残差**：pr600 上 94 px 绕序剔除、14 个退化位移面。**cart / 动态刷子**：未验证。**音频**：4096 个负载丢弃与真解码错误没分开统计，换设备/seek 无恢复路径，**没有人实际听过**。
- **武器本体锚点**、**第 19 步计数器**、**世界残差重测** 都还挂在待办上。

---

## 4. 路上撞到的难题（按"差点把人骗过去"排序）

1. **同一个实体 ID 被读成第一具尸体**：变异体 28/0 全绿，其实读的是重复 ID 里的先到者。修法是**在采样之前**拒绝身份读数，不是采样后去重。复盘：`coding-problem-playbook/problems/2026-10-28-duplicate-entity-id-was-read-as-the-first-body.md`
2. **leaf ambient 表被"剪切"**（本轮最硬）：源码每立方体 72 字节（六个 float3），上传按 96 字节（六个 float4）拷，着色器也按 float4 索引。后果：每个 slot 被剪切 + 147 个立方体时 **越界读 3528 字节**。证据是同一个 exe 连拍五次得到**五个不同 md5**，而 basic 臂稳定（不绑该槽）、trace 臂稳定（在算色之前就 return）。修完连拍四次同一 md5。
3. **两台相隔 1500 单位的摄像机拍出字节相同的画面**：`--debug-camera` 第一版。修完差异 99.37%——画面没错，是取景根本没变。
4. **计数口径骗了三次**：抓帧 tick 从漂移的 origin 10252 变 10320，留下 11 个玩家 onScreen=0；在红砖墙上数"队伍色像素"数出 7607 个；`--world-depth` 只改世界 pass，天空盒是另一个 pass，于是读数从 27.61% 变 0.00%，看着"有效"其实测的是别的层。
5. **门禁基础设施自己烂了**：本机 `diff`/`comm`/`grep`/`sort` 等全是 WinuxCmd 垫片，打不开 bash 进程替换给的 `/dev/fd/63`（本 MSYS 没有 `/dev/fd`），有门禁因此可能一直"空通过"；变异套件用 `cp -p` 还原源码，把旧 mtime 写回去，nmake 不重编，**变异体一直留在二进制里**，后续 8 个步骤读的都是被污染的可执行文件。

---

## 5. 验收现状（诚实版）

### 5.1 能复现的
- 干净构建 /W4 **零警告**；verify6 那次产出的 exe `c6bebfcd…`，13 次抓帧与 4 个探针自检用的都是它。
- 核心像素引脚：bare `2ddade35…`、basic `d927c526…`、leaf `fa297fb9…`（连拍四次一致），都在 tick 600 复现。
- 探针自检：`world_material_probe` 75 项全真、`leaf_ambient_probe` 40 项 0 失败、`demo_open_probe` 16+10 全过。

### 5.2 30 步 verify-all 链（2026-11-22 14:01–15:35，94 分钟）
结论行：`VERIFY=FAIL (30 steps, 1 skipped: WORLD-GEOMETRY-CHECK)` —— **9 步 PASS / 20 步红 / 1 步部分跳过**。

逐条归因（A 机器/环境、B 门禁过期或脚本缺陷、C 真回归）：

- **A 15 步**（5、7、8、9、10、11、12、13、14、15、19、23、25、27、28）。主因是上面第 5 条：变异套件留下的污染二进制被后续 8 个步骤共用；其次是**这份 POV 录音与 14 份标定 demo 已不在本机**；再加一个 2026-11-21 遗留的空锁目录。
- **B 5 步**（1、3、22、24、29）。都是门禁自己的毛病：`exe_count` pin 写 53 而树里是 55；CRLF 文件配 `$` 锚定 grep；见证者取 `tail -1` 取到单帧 clip；`ROOT` 变量在用它的 41 行之后才赋值（这一步**常年没跑**）；选错 `gpu: world isolation` 行。
- **C 1 步**（26，player animation pixels）。掩码从提交时的 3156 px 涨到 262681 px（83 倍），on/off 差异从 68.00% 塌到 2.24%。同 tick 的 entity-id 帧证明这些像素确实由 entity pass 画出（白 ∪ 红 == 哨兵集合），所以不是哨兵误判。

**重要保留**：第 26 步是在链里那个 exe（`3c9a3756…`）上测的。干净重建**成功**并复现出引脚二进制 `c6bebfcd…`，但第 26 步还没在干净二进制上重跑完。**所以"唯一真回归"目前仍是待确认结论，不是已定案。**

完整 30 行表格、每条证据与要真修的六件事：`docs/CHAIN-RED-ATTRIBUTION-2026-11-22.md`。

---

## 6. 正在进行中、尚未验证的改动（工作区里，未提交）

这些**全部没有编译、没有跑门禁、没有提交**，任何"已修好"的说法都还不成立：

- **22 个门禁脚本**：`cp -p` 还原卫生、`false &&` 变异在 /WX 下编不过（C4127→C2220）、进程替换改成实体文件、构建失败后跳过还原、外加 4 个单点 bug（`exe_count` 53→55、CRLF 锚、`ROOT` 顺序、见证行选择、sultry 选行）。
- **两个防回归工具**：`tools/gate-lint.sh`（静态拦截上述几类写法，155 个文件、7 条带理由的例外）与 `tools/gate-lib.sh`（正确写法的公共函数）。gate-lint 现在能跑出 `GATE-LINT=PASS findings=0`，但它自身在扫描时还会向 stderr 吐若干 `unbound variable` 噪声（不影响判定，仍需清理）。
- **8 个 `native/` 文件**：`--temp-census`、`--hud-minimap-bounds bsp`、`--follow-sticky`，全部默认关闭。**当前 `tf2_demo_native.exe` 是 15:46 的，比这些源码（17:32–17:36）旧**，所以现有二进制里不含它们。
- **CI**：`.github/workflows/tier1.yml` 增加 `strict` job。
- **许可证**：`LICENSE`（GPL-3.0-only）、`THIRD-PARTY-NOTICES.md`、README 双语段；`docs/REFERENCE-REPOS-2026-11-21.md` §0.1 写入净室规则（官方 SDK 优先、只读参考、一行都不许抄）。
- **文档**：`docs/GATE-FIXES-2026-11-22.md`、`docs/UNFINISHED-AUDIT-2026-11-22.md`、`docs/WORKPLAN-2026-11-22.md`、`docs/STATUS-2026-11-22.md`。

改判的结论：第 19 步是二进制被污染、不是计数器 bug；第 26 步待定；总览里武器锚点那条**已过时并已改写**。

---

## 7. 下一步（按顺序）

1. 备份 8 个新代码文件 → 静态检查 → **干净构建** → 三个探针确认干净 → **单独跑第 26 步**（判据：≈3156 px 则链上无真回归；仍 ≈26 万 px 则查 entity pass 的 instance range）。
2. 第 24 步 + `mutate.sh` 复跑。
3. 放回新代码 → 编译新功能 → `warn-check` + 自检 + 默认输出逐字节比对 + 两个门禁。
4. 全绿后：先提交门禁/文档/许可证，再提交 `native/` 新功能，最后才考虑推送（只推 `task/merge-all`，不动公开 main）。

---

## 8. 工作区、工具、demo 在哪

### 8.1 机器上的位置
| 内容 | 路径 |
| --- | --- |
| 工作树（本轮） | `D:/TF2_Native_Worktrees/t105-merge`（36 个工作树，命名 `t<任务>-<用途>`） |
| 主仓 | `D:/TF2_Demo_Player` |
| 问题库 | `D:/TF2_Native_Worktrees/_playbook` |
| 同步脚本 | `D:/_shared/sync-2026-11-22.sh`（分三次提交，默认不推送，`--only-a` 只交门禁与文档） |
| 宿主锁 | `D:/_shared/tf2-hostlock/`（跨工作树独占的构建/抓帧窗口） |

### 8.2 仓库内
- 门禁：根目录 `*.sh`（142 个，其中 88 个 `*-check.sh`）；总链 `verify-all.sh`（30 步）；快速链 `verify-fast.sh`、`verify-*.sh`。
- 构建：`build-cmake.sh`（干净全量，约 4 分钟）、`build-target.sh <target>`（增量）、`tools/lockrun.sh`（工作树内锁）。
- 探针：`native/tools/` 55 个；证据：`evidence/` 138 个目录；`evidence/BUILDS.md` 是引脚出处索引。
- 文档：`docs/` 180+ 篇；总入口 `docs/HANDOVER-2026-11-21.md`。

### 8.3 Demo 与资产
| 内容 | 路径 | 规模 |
| --- | --- | --- |
| 仓库自带测试 demo | `D:/TF2_Demo_Player/testdata/demos` | 4 份 |
| 标定语料 | `E:/demo测试` | 11 个地图 18 份录像，1.30 GB |
| 用户 TF 安装的录像 | `D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/demos` | 992 份 autorecord（24 GB，**不是**引脚集） |
| TF2 本体（`--tf-root`） | `D:/SteamLibrary/steamapps/common/Team Fortress 2/tf` | 261 张 `.bsp` |

**已丢失**：POV 录音 `autorecord_2026-07-02_13-26-46.dem` 与 24 份标定 demo 里的 14 份（`73.dem` + 13 个 `autorecord_2026-05/06/07`）。这是链上 6 条红步的唯一原因，只能补文件或正式重钉。

---

## 9. 外部参考（语义基准，不是代码来源）

完整清单见 `docs/REFERENCE-REPOS-2026-11-21.md`。要点：
- **官方/合法优先**：[ValveSoftware/source-sdk-2013](https://github.com/ValveSoftware/source-sdk-2013)、[Source-1-Games](https://github.com/ValveSoftware/Source-1-Games)、[NicknineTheEagle/TF2-Base](https://github.com/NicknineTheEagle/TF2-Base)
- **游戏层语义**（泄露版，只读参考、不进代码）：[OthmanAba/TeamFortress2](https://github.com/OthmanAba/TeamFortress2)、[lua9520/source-engine-2018-hl2_src](https://github.com/lua9520/source-engine-2018-hl2_src)
- **.dem 解析**（事实标准）：[demostf/parser](https://github.com/demostf/parser)、[demostf/demo.js](https://github.com/demostf/demo.js)、[PazerOP/DemoLib2](https://github.com/PazerOP/DemoLib2)
- **同类播放器**：[Nocrex/tf2-demo-player](https://github.com/Nocrex/tf2-demo-player)、[programmer1o1/demoscope](https://github.com/programmer1o1/demoscope)
- **BSP 与资源**：[Galaco/bsp](https://github.com/Galaco/bsp)、[pySourceSDK/ValveBSP](https://github.com/pySourceSDK/ValveBSP)、[SteamTracking/GameTracking-TF2](https://github.com/SteamTracking/GameTracking-TF2)

规则（写在 `docs/REFERENCE-REPOS-2026-11-21.md` §0）：引用外部结论必须写"库 + 路径 + 文件 + 行号 + 本仓对应行号"；外部源码只解释**语义**，不证明本仓现状；**一行代码都不许复制进来**，只许按语义重写。

---

## 10. 这份简报不能证明什么

- 它不能证明分支代码是对的：它只说明 30 条红里 29 条不该按"代码坏了"去读。
- 第 26 步的 C 判定基于两帧颜色交叉验证，是**症状定位**不是根因；根因要等干净二进制重跑后的读数。
- 一次 `verify-all` 运行**永远**会让工作树变脏（本轮约 230 条，绝大部分是重新生成的 `evidence/`）。任何"工作区干净"的说法都必须先还原或刻意提交这些文件。
