# T3 · 世界材质与光照（world-material-lighting）

- 任务：`TASK-PLAN-2026-10-13-4-WAY.md` §T3（= `IMPLEMENTATION-PLAN-2026-10-10.md` §T4「世界材质与地图光照」；两份文档编号不同，本文件按任务书称 T3）。
- 分支 / worktree：`task/world-material-lighting` / `D:\TF2_Native_Worktrees\world-material-lighting`
- 基线 commit：`0f023bb6dcb6b9617ace804fcf9cf34b93594ae9`（`docs: record T3 integration and fix audio input line endings`）
- 最终 commit：`f7eea49`（代码/门禁/证据/四份文档；其后为判定运行的证据提交与交付报告，见 §7）
- 执行日期：2026-10-13
- 授权范围：产品与门禁改动见 §2 逐文件表；**`D:\TF2_Demo_Player` 全程只读**；未运行完整 `verify-all.sh`（任务书硬性约束）。

## 交付格式（12 项）

| 项 | 值 |
|---|---|
| 任务 | T3 世界材质与光照 |
| 分支与 worktree | `task/world-material-lighting` / `D:\TF2_Native_Worktrees\world-material-lighting` |
| 基线 commit | `0f023bb` |
| 最终 commit | `f7eea49`（+ 判定运行证据与交付报告，见 §7） |
| 修改文件及理由 | §2 逐文件表；其中 5 个文件超出任务书允许清单，逐条给理由 |
| 构建命令与 rc | `bash build-target.sh world_material_probe tf2_demo_native` → rc=0 |
| 专项验收命令与 rc | `world-material-check.sh` → `WORLD-MATERIAL=PASS` rc=0；`--mutation` → `MUTATION-CAUGHT=PASS` rc=0；`verify-fast.sh` → `VERIFY-FAST=PASS (assertions_ok=43)` rc=0；`verify-render.sh` → `gates_ok=8 gates_bad=1` rc=1（唯一红 = `viewmodel-contract`：T1 已归因、`task/capture-determinism@a259b52` 已修的落点盲区，**不在本分支**；§3.5） |
| 正向读数 | §4，六项能力逐项 state / reason / counters，外加渲染侧对照读数 |
| mutation 结果 | §5：7 个 fixture 变异 120 断言、5 条错误期望守卫、frame-capture 2/2 |
| 证据路径与哈希 | §8 |
| 未验证项 | §10 |
| 工作树是否 clean | 是（§9；`git status --porcelain` 空） |
| GitHub 分支地址 | https://github.com/elo6371/tf2-demo-player-native/tree/task/world-material-lighting |

## 1. 结论摘要

| 项 | 读数 |
|---|---|
| 六项能力 | 逐项独立读数（§4）：lightmap/cubemap/sky/vis 为 `active`，displacement/water 为具名 `fallback`（原因见 §4） |
| 合成 fixture | 9 个，正向 90 条断言、变异档 120 条断言 7 个变异，全 0 bad |
| 真实地图 | `cp_snakewater_final1`：lightmap 装 11512/11527 面（15 面被图集丢弃）、cubemap 33/33 六面全解、sky 6 纹理全解、displacement 165376 三角、water 18 面、vis 2940 簇 |
| 负向控制 | `koth_bagel_rc13`（未安装）：六项全部 `unavailable`，各自具名原因 |
| 渲染侧 | `lightmapped=597789/600000`、`displacedTriangles=165376`、`waterTriangles=36` 与解析侧相等；cubemap `status=six-face mode=0 faces=6 size=32` |
| `world-material-check.sh` | `WORLD-MATERIAL=PASS`（8 节，rc=0） |
| `--mutation` | `MUTATION-CAUGHT=PASS`（rc=0） |
| `verify-fast.sh` | `VERIFY-FAST=PASS (assertions_ok=43)`（rc=0） |
| `verify-render.sh` | `VERIFY-RENDER=FAIL (1 red)`、rc=1（attempt 2：`gates_ok=8 gates_bad=1`）；唯一红 = `viewmodel-contract` 的落点盲区（T1 已归因、`a259b52` 已修，不在本分支）；`frame-capture` 以新判据 PASS |
| 共享门禁更新 | `frame-capture-check.sh` 颜色上限判据被本轮特性证伪，改为回退帧字节比较；单独提交 `3ba4803`，**需整合者确认**（§6） |

## 2. 修改文件及理由（逐文件）

相对 `0f023bb` 共 17 个提交（11 产品/首批证据 + 门禁判据 + 三笔证据/留档 + 四份文档 + 交付报告）；文件与理由如下（行数取自 `git diff --stat 0f023bb..HEAD`）。

| 文件 | 规模 | 为什么改 |
|---|---|---|
| `native/include/world_material.h` | +485（新） | 六项能力的读数结构（state/reason/counters）、判定函数与 fixture API；探针与门禁共用一套词表。 |
| `native/src/world_material.cpp` | +1119（新） | 六项能力的测量实现（`measureLightmap/measureCubemaps/measureSky/measureDisplacement/measureWater/measureVis` 与 `judge*`）、fixture 的存储式 ZIP 构造、BSP pakfile 读取（method 0 stored / method 14 LZMA，复用仓库 `LzmaDec`）。 |
| `native/tools/world_material_probe.cpp` | +1279 | 9 个合成 BSP fixture（逐 lump 构造真实 BSP 字节再走 `BspParser::parse` 回读）、正向与变异自检、真实地图/负向地图输出。 |
| `native/src/native_renderer.cpp` | +176 | 世界 PS/US 常量缓冲加 `envFeatures`（offset 128，ByteWidth 128→160）、`TextureCube`（t5）采样分支、世界 cubemap 上传与状态词表、逐能力顶点计数（lightmapped/displaced/water）。 |
| `world-material-check.sh` | +390（新） | 8 节门禁：self-test / fixture 正向（90 断言）/ fixture 变异（120 断言、7 变异）/ 真实地图 pin / 负向 bagel / `--mutation` / 渲染侧 lightmap / 渲染侧 cubemap。 |
| `frame-capture-check.sh` | +127/−25 | **共享门禁判据更新**（§6）：颜色多样性上限 → 同次运行回退帧的字节比较；paused 场景注释按实测修正。单独提交 `3ba4803`。 |
| `evidence/world-material/*` | 13 文件 + 3 子目录 | 本轮全部门禁读数的入库件（`before/` 对照、三张帧 PNG、`frame-capture-gate/` 与 `verify-render/` 的判定日志），`README.md` 逐文件说明。 |
| `evidence/frame-capture/*` | 7 文件 | 见证帧刷新（snakewater `019a2899`、paused `dd65787f`；bagel 不变且成为新判据的参照物）。 |
| `README.md` / `CHANGELOG.md` / `HANDOFF-2026-10-07.md` / `IMPLEMENTATION-PLAN-2026-10-10.md` | 文档 | 按工作规则同步；本文件即交付报告。 |

**超出任务书允许清单的 5 个文件，逐条理由**（任务书允许：`native/include/world_material*.h`、`native/src/world_material*.cpp`、`native/src/native_renderer.cpp`、`native/tools/world_material_probe.cpp`、`world-material-check.sh`、`evidence/world-material/`）：

1. `native/include/vtf_texture.h` / `native/src/vtf_texture.cpp`（+50/+82）：cubemap 单面解码必须先知道"图像段怎么切、到底几个面"。这些是 VTF 格式知识，放进既有共享解析器是唯一不制造第二份格式实现的做法；改动是增量的（新头字段 + `decodeCubeFaceRgba`），既有 2D 路径未动（`verify-fast.sh`、`world-material-check.sh` 与 `material-chain` 均复跑通过）。
2. `native/include/native_renderer.h`（+61）：`WorldCubemapStatus` 状态词表、采样状态新字段、`uploadWorldCubemap` 声明——读数要能从渲染器读出来，才谈得上"能力单独输出状态"。
3. `native/src/main.cpp`（+146/−1）：**唯一同时持有 D3D 设备、BSP 与 pakfile 解码的调用点**（探针没有 device）。端到端接线（地图 cubemap 解码→上传→trace）、`--world-env-force` 诊断开关、`envMap` 语义改为"材质声明了 envmap"。**这是共享文件（任务书 §T4 也提示 main.cpp 最后整合），整合时不得覆盖。**
4. `native/CMakeLists.txt`（+12/−1）：把 `vtf_texture.cpp` 链进 `world_material_probe`、`world_material.cpp` 链进 `tf2_demo_native`；无此两项则目标不构建。
5. `frame-capture-check.sh`：见 §6——不改它，本任务的验收命令 `verify-render.sh` 会因为一条被本特性证伪的旧判据而必然变红，且该红会转移到整合者的合并后复核。已按"共享接口先小提交"的规则单独提交。

## 3. 验收：命令、rc 与读数

### 3.1 构建

```
bash build-target.sh world_material_probe tf2_demo_native   → rc=0
```

### 3.2 `bash world-material-check.sh`（正向）

`WORLD-MATERIAL=PASS`，rc=0。8 节构成与读数：

| 节 | 内容 | 读数 |
|---|---|---|
| 1/8 | `world_material_probe --self-test` | 两行；第一行与旧自检逐字节一致（`verify-fast.sh` 钉了其中 7 个子串） |
| 2/8 | 9 个合成 fixture 正向 | 90 条断言、0 bad |
| 3/8 | 9 个 fixture 变异档 | 120 条断言、7 个变异、0 bad；`mutationRan=false` 的两个具名（`lightmap-triangle-limit`、`vis-camera-cluster`） |
| 4/8 | 真实地图 pin（`cp_snakewater_final1`） | 逐条 `{"reading": …}` 断言，见 §4 |
| 5/8 | 负向 bagel | 六项全 `unavailable`，各自具名原因 |
| 6/8 | `--mutation` 守卫 | 5 条"错误期望值"不得出现（§5） |
| 7/8 | 渲染侧 lightmap | `status=active intensity=0.479 … lightmapped=597789`（= 3 × 解析侧 `trianglesWithLightUv` 199263） |
| 8/8 | 渲染侧 cubemap | 默认 `mode=0 declared=0` / `--world-env-force` `mode=2 declared=1`；帧差 62699 字节 |

### 3.3 `bash world-material-check.sh --mutation`

`MUTATION-CAUGHT=PASS`，rc=0（§5）。

### 3.4 `bash verify-fast.sh`

`VERIFY-FAST=PASS (assertions_ok=43)`，rc=0。

### 3.5 `bash verify-render.sh`

**结论：8/9 门禁通过；唯一红项是 `viewmodel-contract` 的落点盲区——由 T1 归因、已在 `task/capture-determinism@a259b52` 修复（不在本分支）。本任务的判据更新与产品改动没有引入任何红项**：`frame-capture` 以新判据 PASS，`evidence-tree` 干净。

同一棵树、同一脚本共跑两次（第一次运行期间本轮的证据文件还在写入，被 `evidence-tree` 快照抓到——那是操作者编辑，不是 tier 泄漏，故重跑）：

| 运行 | 时间 | 读数 | 说明 |
|---|---|---|---|
| attempt 1 | 19:02:06–19:46:56（44m56s） | `gates_ok=7 gates_bad=2`、rc=1 | 红：`viewmodel-contract`（见下）+ `evidence-tree`（运行期写入 `evidence/world-material/{README.md,frame-capture-gate/}`；运行开始时 `git status --porcelain evidence/` 为空，tier 自身输出全部落在 `evidence/fast/render`） |
| attempt 2（判定运行） | 19:48:56–20:32:34（43m41s） | `gates_ok=8 gates_bad=1`、rc=1、`VERIFY-RENDER=FAIL (1 red)` | 证据树冻结（`evidence/ before: []`）后重跑；唯一红 = `viewmodel-contract` |

attempt 2 逐门禁：`resource-reachability` PASS / `frame-capture` PASS（新判据）/ `entity-material` PASS / `weapon-animation` PASS / **`viewmodel-contract` FAIL（唯一红）** / `viewmodel-animation` PASS / `viewmodel-companion` PASS / `material-chain` 12/12 / `evidence-tree` OK。

红项读数（attempt 2，与 T1 在 `0f023bb` 上实测同一条）：`assertions_ok=153`、`VIEWMODEL-CONTRACT=FAIL`，失败断言为"7 张比较帧全部落在测量点"（实测落在 10253，测量点集合为 10252/10268）。归因见 `T1-RENDER-CURRENT-2026-10-13.md` §4（单变量探针表）：落点由首帧延迟决定，音频接线（`69baff8`，早于本分支基线）让首帧变快、落点滑入 10253–10267 标定盲区。**T3 改动不碰这条链路**（它不读世界材质/渲染器状态）；修复 `a259b52` 把落点固定为暂停 `playback_tick=0 / sceneTick=10252`（154 条断言、7 张比较帧同一测量点、跨工作树 BMP SHA-256 一致），按"先合 T3、再合 capture-determinism"的顺序，合并后重跑应 9/9。

日志与哈希：`evidence/world-material/verify-render/run.log`（sha256 `a526774fbd39ac779906bdd29bb7692926e1d40d20d0950bd8e60b42c07d96e9`，1794 B）、`run-attempt1.log`（sha256 `2f7b3831b1dd8660acdda9b7a36e343348029325981707730c67773d519c7232`）；八个门禁日志逐份在 `verify-render/` 目录内。

## 4. 六项能力：状态、读数与失败原因（真实地图 `cp_snakewater_final1`）

每项都先有合成 fixture（§5 的 7 个变异覆盖全部六项），再接真实 BSP。以下为入库原文（`evidence/world-material/real-cp_snakewater_final1.txt` 摘录），负向为 `koth_bagel_rc13.bsp`（未安装）。

| # | 能力 | state | 关键读数 | reason（原文摘） |
|---|---|---|---|---|
| 1 | 逐面 RGBExp32 lightmap | `active` | 图集 2048x2047；`facesPacked=11512/facesWithLightmap=11527`；`trianglesWithLightUv=199263/200000`；`uvAllInRange=true` | "…packs 11512 of 11527 faces; 199263 of 200000 triangles carry a per-face light uv; 15 faces dropped by the packer" |
| 2 | cubemap 六面 | `active` | `samplesComplete=33/33`、`facesDecoded=198`、`fromPakfile=33`；首样本 7 个面槽位 vs 头部声明 6；`faceMeans=62,45,56,58,51,54` | "33 of 33 env_cubemap samples decoded all six faces; 33 face slots beyond the six a cube needs" |
| 3 | skybox | `active` | `vmtFound=6`、`texturesDecoded=6`、`firstSize=512x256`、`smallestSize=1x1`、`decodedBytes=3145732`；`skyFaces=690` | "all six skybox vmts resolve for sky_well_01 and their 512x256..1x1 textures decode to 3145732 bytes" |
| 4 | displacement | `fallback` | `faces=1316`、`triangles=165376`、`cornerMatches=1316/1316` | "the triangle limit truncated the walk after 165376 triangles"（显式：三角形上限截断） |
| 5 | water capability | `fallback` | `faces=18`、`trianglesTagged=36`、`surface=approximate-warp`；`$normalmap` 解析到 `materials/water/tfwater001_normal.vtf` | "18 water faces resolve to a texture; reflection=off refraction=off, neither render target is implemented so the surface draws unlit" |
| 6 | VIS clipping 状态 | `active` | `mode=cluster-pvs`、`clusters=2940`、`visibleFromCluster0=384`、`trianglesWithCluster=34517` | "pvs hides 2556 of 2940 clusters; 34517 of 200000 triangles carry a cluster" |

负向（`koth_bagel_rc13.bsp` 未安装）：六项全部 `state=unavailable`，原因分别为 "BSP header is invalid"（总）/ "the bsp declares no lightmap faces" / "the bsp declares no env_cubemap samples" / "the entity lump names no skyname" / "no displacement face emitted geometry" / "the bsp declares no water faces" / "the bsp carries no usable pvs lump"。`{"bsp":…,"valid":false}` 而不是崩溃或零值。

渲染侧（产品 trace，与解析侧可对照的量逐项相等）：

```
gpu: world lightmap status=active intensity=0.479 atlas=1 bump=0 env=0 vertices=600000 lightmapped=597789
gpu: world geometry displacedTriangles=165376 waterTriangles=36
gpu: world cubemap status=six-face mode=0 preset=standard declared=0 faces=6 size=32
  source=materials/maps/cp_snakewater_final1/c-5135_2022_-15.vtf reason=all six faces decoded
gpu: world vis cameraCluster=563 visibleFromCamera=850 clusters=2940 clusteredTriangles=34517 hiddenTriangles=26936 clipApplied=0
```

**能力边界，必须随读数一起读**：③ skybox 只有**资源与状态**读数，天空盒几何**未绘制**；⑤ 水面为无光照近似（water RT 未实现，反射/折射均 unavailable）；⑥ `clipApplied=0`——**未做任何裁剪**，"状态"到此为止，不得读成 Source 等效。① 大图受图集容量约束：abbey `2koth_abbey.bsp` 实测装 18854/22991 面、4137 面被丢弃（`trianglesWithLightUv=56476/180555`），渲染侧 `lightmapped=169428` = 3 × 56476，两侧同样相等。

## 5. mutation 结果

1. **fixture 变异（7 个，覆盖全部六项能力）**：`fixture-mutation.txt` 逐条 `mutationBad=0`；总计 120 条断言 0 bad。两个直测-only fixture（`lightmap-triangle-limit`、`vis-camera-cluster`）在 §10 具名。
2. **第 6 节守卫（5 条错误期望值不得出现）**：packer drop=0、sky.state=unavailable、`--world-env-force` 前 mode=0、fixture 变异数=99、**修复前渲染顶点数 92193**。最后一条的"可红性"有本轮实证：把逐面 light uv 的写入回退到旧行为时，渲染侧读数从 597789 回落到 92193，门禁第 7 节变红（`evidence/world-material/before/` 保留修复前读数）。
3. **frame-capture 门禁变异（2/2）**：见 §6——mutation 1 翻 1 字节移动哈希；mutation 2 把回退贴片（2% 字节扰动）喂给新判据，读数 0.0200 < 10%，被拒。
4. 固定装置：每次变异后源码逐字节恢复、重建成功；`git diff --stat -- native/` 在门禁跑完后为空。

## 6. 共享门禁更新：`frame-capture-check.sh`（**需要整合者确认**）

**为什么改**：该门禁对安装地图帧的判据是 `distinct colours < 1000`，前提写在它自己的注释里——"a shaded world with no textures is a handful of flat greys"。本轮 lightmap 恰好把这个前提推翻：

| 读数 | 2026-10-12（前轮） | 2026-10-13（本轮） |
|---|---|---|
| 安装地图帧（snakewater） | 563 色 | **15909 色** |
| 回退贴片帧（bagel，地图未安装） | 139121 色 | 139121 色（逐字节不变，sha `fa1bb194…`） |
| 判据余量 | 247x | **8.7x**（且下一次 realism 迭代还会继续吃掉） |

**改成什么**：安装地图帧必须与**同一次运行**抓到的回退帧在有效载荷字节上相差 ≥ 10%。实测 0.9840（本轮）/ 0.9899（前轮）；回退形状喂进判据读 0.0200 → 拒绝。参照帧每次现抓，判据不把本机的回退贴片资产写进阈值。颜色计数保留为**只读数**（不再断言）。同时修正了 paused 场景的陈旧注释：无 demo 时程序加载的是 koth_abbey 世界（trace 读 541665 顶点），不是"全屏四边形"。

**证据**：`evidence/world-material/render-frame-shape.txt`（两轮六帧、两两字节差、paused 变化的归因、fix 前后对照 PNG）；判定日志 `evidence/world-material/frame-capture-gate/{straight-final,mutation-final}.txt`。

**为什么不能留给整合者顺手处理**：判据红与不红的开关是本轮特性本身（563→15909），合并后 `verify-render.sh`（任务书 Wave 2 第 4 步）必然红在这一条；本任务把"判据随产品演进"这件事在自己轮内结算，并留下可反向验证的替换判据。**这是共享验收面，请整合者复核后决定是否随 T3 合并。**

## 7. 提交清单（相对 `0f023bb`）

```
f4c6549 feat(world-material): capability layer for the six world material claims
67ead84 feat(world-material): expose lightmapped/total world vertex counts on the sample state
35805ba fix(world-material): light every face that carries lightmap data, and count it from the vertex
8835620 feat(vtf): decode one face of a cubemap as a 2D image through the existing decoder
8ad7be6 feat(world-material): find the cubemaps TF2 actually ships, and decode their faces
7f6f75f feat(renderer): cube texture upload and a cubemap status a runtime can read
972cd5c feat(world-material): sample the map's six-face cubemap, and read back which pass ran
7377acc feat(renderer): declare the uploaded displacement and water triangle counts
9e1b021 feat(world-material): read the sky's textures, and what the renderer draws per capability
e411733 fix(world-material): a cubemap the device refuses degrades the capability, not the program
73220b1 chore(evidence): world-material readings, forward and mutated, with the pre-fix set kept
3ba4803 test(frame-capture): the fallback comparison replaces a colour cap the lightmapped world falsified
3857078 chore(evidence): frame-capture's witness frames for the lightmapped world, and the frame-shape record
2e83e27 chore(evidence): the frame-capture verdicts, and the first tier attempt kept with its cause
f7eea49 docs: sync the four handover documents for the T3 world-material round
72b6a57 chore(evidence): the render tier's verdict run on the frozen tree, with the eight gate logs
```

本轮共 18 笔：上列 16 笔 + 交付报告 `bcd32d6` + 链尾一笔计数校正（本段数字由它钉住）。§6 复核锚点（HANDOFF §6 表）以 `f7eea49` 计算；`72b6a57` 及其后的两笔只增加证据与文档字节，不含 native 或判据字节。

## 8. 证据路径与哈希

入库证据（sha256 取前 16 位；完整值可用 `sha256sum evidence/world-material/*` 复算）：

| 路径 | 内容 | sha256（前 16） |
|---|---|---|
| `evidence/world-material/self-test.txt` | 双层自检 | `4069284a4f38bf28` |
| `evidence/world-material/fixture-straight.txt` | 9 fixture 正向 90 断言 | `1c4d27a86b358e7c` |
| `evidence/world-material/fixture-mutation.txt` | 变异档 120 断言 7 变异 | `5de2553069b1375e` |
| `evidence/world-material/real-cp_snakewater_final1.txt` | 真实地图六项读数 | `518f01032bd97563` |
| `evidence/world-material/negative-bagel.txt` | 负向控制 | `a207e917cf67b54c` |
| `evidence/world-material/render-lightmap.txt` / `render-cubemap.txt` | 渲染侧 trace | `b3ec217e2e0e74cc` / `a68ec2ce34efcd17` |
| `evidence/world-material/render-frame-shape.txt` | 帧形记录（§6） | `d3b4d3d1e1be700a` |
| `evidence/world-material/render-frame-*.png` | 三张对照帧 | `7a60d575…` / `1b768038…` / `94573e2e…` |
| `evidence/world-material/frame-capture-gate/{straight,mutation}-final.txt` | 门禁两模式判定 | `6694d7a3f0abb081` / `dde662052e72bae5` |
| `evidence/frame-capture/{paused,demo,demo-installed-map}.bmp` | 见证帧（刷新后） | `dd65787f…` / `fa1bb194…` / `019a2899…` |
| `evidence/world-material/verify-render/run-attempt1.log` | 第一次 tier 运行（含两条红的成因） | `2f7b3831b1dd8660` |
| `evidence/world-material/verify-render/{run.log,8 份门禁日志}` | 冻结树的判定运行（attempt 2，8/9） | `a526774fbd39ac77`（run.log） |

## 9. 工作树与锁

- `git status --porcelain`：`f7eea49` 提交后只剩本报告未跟踪（`?? T3-WORLD-MATERIAL-LIGHTING-2026-10-13.md`），`evidence/` 为空；两次 tier 运行的起始状态记录在各自日志头部（attempt 1 `status before: []`；attempt 2 `status before: [?? 本报告]`，两者 `evidence/ before: []`）。
- 锁：`.scratch/*.lock` 全程无残留；两次 `verify-render.sh` 均跑完并给出判定（attempt 1 rc=1 因两条红、attempt 2 rc=1 因唯一判定红，均非中断或超时）。
- 变异恢复：`git diff --stat -- native/` 为空；fixture 再生比较逐字节一致。
- `D:\TF2_Demo_Player` 只读约束：2026-10-13 全天 `find … -newermt` 计数：文件 0、目录 0；顶层 mtime 2026-10-05。

## 10. 未验证项与遗留风险

1. **skybox 几何未绘制**：能力 3 只到"资源可达 + 六面全解 + 状态词表"，天空盒不参与画面。
2. **水面未实现 RT**：反射/折射 unavailable 具名；水面以无光照近似绘制（`waterTriangles=36` 有计数）。
3. **VIS 未裁剪**：`clipApplied=0` 是读数不是省略——隐藏三角只被计数（`hiddenTriangles`），没有进裁剪实现。
4. **大图 lightmap 图集容量**：abbey 丢弃 4137/22991 面（具名）；容量策略（P1c 配置化的 512/1024 上限）未做。
5. **两个直测-only fixture**：`lightmap-triangle-limit`、`vis-camera-cluster` 没有注册变异（六项能力各有至少一个变异 fixture）。
6. **人工画面对照与真实 GPU 性能未做**：帧形记录（§6）是机检读数，不是人眼对标。
7. **帧抓取落点漂移**：`--capture-tick N` 为"≥ N"语义（T1 已归因），本轮的帧形对照都在 `--capture-tick 0` 固定落点下取得，跨负载不做断言。
8. **总链未跑**：`verify-all.sh` 按任务书硬性约束未运行。`verify-render.sh` 两次运行见 §3.5；唯一红项（`viewmodel-contract` 落点盲区）已由 `task/capture-determinism@a259b52` 承接，合并后按 Wave 2 第 4 步在合并 HEAD 上重跑。
9. **共享文档冲突面**：README/CHANGELOG/HANDOFF/IMPLEMENTATION-PLAN 在本分支与主线（T1/T2 的整合提交）都有改动，整合时需按语义合并。

## 11. 给整合者的建议

1. **合并顺序（协调者已定）**：先合 T3（`f7eea49`），再合 `task/capture-determinism@a259b52`；合后只重跑受影响的定向门禁：`verify-render.sh`（9 门禁）、`verify-fast.sh`、`world-material-check.sh`，另加 capture 的专项。合并 HEAD 上的 `verify-render.sh` 应把本分支的两条读数变化吸收掉：`viewmodel-contract` 应变绿（落点修复），`frame-capture` 应保持绿（新判据不 pin 落点，实测 98% 对阈值 10% 有 10 倍余量）。
2. **main.cpp 是共享文件**：本轮的接线都在 `reloadGpuResources`/trace 段，T4（相机/HUD）整合时按语义合并，不得整体覆盖。
3. **两处编号**：本分支的 `T3` 是四任务分工计划编号；`IMPLEMENTATION-PLAN §T4` 是另一套编号，两份文档都已互相注明。
4. **`evidence/frame-capture/` 的刷新**：见证帧随本轮特性变了（snakewater/paused），这属于"证据随产品走"，若整合者对见证帧的刷新策略有异议，请连同 §6 一起裁定。
