# Gemini 任务包 — TF2 demo player native（基线 work/integrate @ 3ef37e6c）

> 这是一份可以直接执行的委派书。你的产出只有两类：**(A) 只读设计文档**、**(B) 纯文本 unified diff**。
> 我们不接受「我跑过了」的声明，也不接受任何需要运行时才能成立的结论。

## 0 角色与硬约束

- 你是静态分析与补丁作者。你能读源码，**不能构建、不能跑 demo、不能看画面**。
- 仓库：`https://github.com/elo6371/tf2-demo-player-native`，分支 `work/integrate`。
- 只允许基于第 1 节钉死的 commit 工作。
- 不许改产品契约：退出码、`--` 开关语义、trace / frame-capture 文件格式、`.dem` 解析语义。
- 不许新增产品命令行开关（`wWinMain` 里 191 条 `L"--"` 比较已经撞过 MSVC `C1061`）。需要实验旋钮一律用**环境变量**，默认关闭。
- 代码风格：C++17、2 空格缩进、LF、`/W4 /WX` 下零新增告警、注释写「为什么」而不是「做了什么」。
- 不许碰 `.github/workflows/`（CI 至今零运行，改动无意义）。

## 1 基线（钉死，行号以此为准）

- commit `3ef37e6c30dfdbdbf7c41e144bab83b12bd9a482`（分支 `work/integrate`，远端同）。
- `native/src/main.cpp` 16,221 行；`wWinMain` 在 `main.cpp:3163`–`:16221`（13,251 行）。
- CLI：`L"--"` 比较 191 条、`wcscmp` 合计 212 条、`L"--"` 字面量 200 个。
- 构建：`bash build-target.sh tf2_demo_native` → `native/build-nmake/tf2_demo_native.exe`；139 个 TU。
- 已有门禁（我们本地跑，你不用管）：`--renderer-resize-selftest`、`tools/warn-check.sh`、`build-identity-check.sh`、`frame-capture-check.sh`。
- 你引用的行号与基线差超过 3 行，必须在交付里标注「行号可能漂移」。

## 2 交付格式（不遵守直接退回）

1. 每轮只交付 **1 个 patch**，≤400 行 diff、≤3 个文件。
2. 纯文本 `diff --git` unified diff：2 空格缩进、LF、**不要**用 markdown 代码块包裹整份补丁时可读性会坏，正文里只写 diff 与说明。
3. 每个 patch 必附：
   - 每个被改文件的**改前 sha256**；
   - `git apply --check` 的预期结果（你无法本地验证就明说）；
   - 增删行数；
   - **「静态无法定论」清单**：你判断不了、必须由我们跑产品才能定的部分。
4. 设计文档用 markdown，表格优先，每条结论带 `文件:行号`。
5. 每轮结尾给 ≤10 行进度小结：本轮做了什么、下一轮给什么、有没有卡住。

## 3 任务清单

### G1（最重，两阶段）CLI 表驱动化

**背景**：`main.cpp` 16,221 行，`wWinMain` 里 191 条 `wcscmp(arguments[i], L"--...")` 平铺，MSVC 已报过 `C1061`（编译器分支限制）。这是架构优化第一片，风险最低但要绝对保持行为。

**阶段 A（只读设计，先交这个）**
1. 完整 flag 清单表，覆盖**全部 191 条**，列：`flag | 参数个数 | 处理块起止行号 | 读写的共享状态 | 是否依赖先后顺序`。少一条即退回。
2. 给出目标结构（示例，可改但要说明理由）：
   ```cpp
   struct FlagSpec { const wchar_t* name; int arity; bool (*apply)(CliContext&, int argc, const wchar_t* const*); };
   ```
3. 标出哪些 flag 有顺序耦合（必须在渲染器初始化之前 / demo 加载之后 / selftest 之后生效），并给出你在分发循环里保序的方案。
4. 标出哪些 flag 会**短路**（解析完就 return / 跑完探针就 exit），逐条列退出码。

**阶段 B（分 3 轮给补丁）**
- 轮 1：表结构 + 分发循环骨架 + ≤30 条纯开关型 flag。骨架必须能在其余 flag 未迁移时原样回落到旧分支。
- 轮 2/3：剩余分批迁移，最终删掉旧链。
- 硬约束：**任何 flag 的副作用顺序、错误文本、退出码逐字保持**；未知 flag 的现有行为不变；`--help` 输出文本不变。

**验收（本地）**：build → `--renderer-resize-selftest` → `tools/warn-check.sh` → identity 门禁 → 30 条 flag 的退出码对比。

### G2（重）LOD 选择策略 + 逐实体选级

**背景**：`native/src/model_loader.cpp:1489` 注释写着「THE LOD TO DRAW IS THE LOWEST ONE THE FILE DECLARES」——现在**固定只画文件声明的最粗一级**。LOD 数据表其实已经解出来了：`:952-1046` 的 LOD-major 顶点池，`:1489-1543` 的选择与 `no valid LOD mesh descriptors` 报错。缺的是策略与管线。

**要求**
1. 设计：屏幕空间误差选级（距离 + FOV + 模型包围球半径 + 每级 switch 距离，来自模型文件），带迟滞避免切级抖动；说明每级 index buffer 怎么建、切换时几何数据放哪。
2. 补丁：`model_loader.cpp` 侧的选级函数 + 实体渲染侧传入选级参数（`native_renderer_frame.cpp` / `entity_model.cpp` 的调用面，先给设计再给最小补丁）。
3. 探针：**不新增开关**，用环境变量 `TF2_NATIVE_LOD_REPORT=<path>`，每帧输出 `tick,entity,model,distance,chosenLod,switchDistance` 的 CSV。
4. 明确列出你**没有**验证的：画面是否破面、性能变化、切级是否闪烁。

**验收（本地）**：跑 demo 拿 CSV 对比 + 帧捕获。

### G3（最重的新子系统）粒子数据层（不做绘制）

**背景**：`native/src/pcf_operators.cpp` 已有 operator 解析骨架（`kMaxFileBytes=8MB`、`kMaxStrings=8192`、`kMaxElements=8192`、`kMaxAttributes=512`、`kMaxArray=65536`、`kMaxStoredOperators=256`）；`native/src/effects_timeline.cpp` 是 temp effect 时间线。素材存在：`pcf_capability_probe` 在 `tf2_misc_dir.vpk` 里看到 135 个 pcf（`particles/flamethrower.pcf`、`medicgun_beam.pcf`、`blood_impact.pcf` 等）。但**产品没有粒子绘制 pass**，这是新子系统。

**要求（分 4 轮）**
- D3.1 设计：pcf operator 图 → 运行时可求值的 emitter/particle 状态机。必须写清：每帧确定性（禁止 time-based 随机源）、生命周期/发射率/颜色插值/尺寸曲线的求值顺序、数据放哪、与 `effects_timeline` 的边界。
- D3.2 解析硬化补丁：现有解析骨架的边界检查、错误串、上限说明。
- D3.3 离线仿真探针：环境变量 `TF2_NATIVE_PARTICLE_SIM=<path>`，读 3 个试点 pcf + 固定 tick 序列，输出每帧粒子 `x,y,z,size,color,rgba` 文本。
- D3.4 3 个试点的 golden 输出（`flamethrower`、`medicgun_beam`、`blood_impact`）。

**明确不做**：billboard/quad 绘制、GPU、深度排序、和现有 overlay 通道合成。这些留本地。

**验收（本地）**：dump 逐字节比对 + 与你给的 golden 对齐。

### G4（重）`wWinMain` seam map（只读）+ 第一片搬迁

**要求**
1. **只读设计先交**：把 `wWinMain`（`main.cpp:3163`–`:16221`）切成 6–8 个簇，每簇给：精确行号区间、进出共享状态（变量名 + 类型 + 谁写谁读）、建议的 context struct 字段、迁移顺序、风险等级。
2. 然后只搬**一个簇**（≤600 行移动）的补丁：函数签名显式化、参数替代全局捕获，**不许改语义、不许改调用顺序、不许顺手重构**。
3. 禁止一次给整包重构。

**验收（本地）**：build + selftest + identity。

### G5（中）`/analyze` 剩余告警

**背景**：P9 之后 `main.cpp` 仍有 `wWinMain` 的 `C6262 58,208 B` 与 `C28251`；另有 `C28020`、`C6553`、`C6011` 待定。

**要求**：逐条给「真问题 / 假阳性 + 证据行 + 修法」；真问题给补丁，假阳性给为什么。禁止为消警告而改语义。

### G6（中）文档-源码一致性第二遍

**背景**：上一份评审找出 3 处过时（`sky_camera` 说 UNSUPPORTED 但 `bsp_map.cpp:628-640` 已解析；COLOR0 回读已被 View16/17 推翻；`183` 开关是旧数、现为 191）。

**要求**：扫 `docs/STATUS-REPORT-2026-11-19.md`、`docs/PROJECT-NEXT-TASKS-2026-11-14.md`、`docs/N-STATUS-2026-11-14.md`、`docs/TASK-LIST-2026-11-19.md`、`docs/DEFECT-REGISTER-*`，找两类：① 文档说 UNSUPPORTED/没有 X，但源码已有；② 数字过时。**至少 8 处**，每条给文档行号 + 源码行号 + 谁对。

### G7（中）逐顶点 UV 取模的 PS 侧修复

**背景**：`native/src/world_tile_atlas.cpp:340-347` 的 `worldTileCoord` 在 CPU 逐顶点做 `texcoord - floor(texcoord/size)*size` 再映射 atlas 子矩形；调用点 `native/src/native_renderer_world.cpp:810-815`；PS 侧没有 `frac`；sampler 是 WRAP（`native/src/native_renderer.cpp:596-597`）。跨整数平铺边界的三角形会反向插值（UV 拉伸）。

**要求**：给 HLSL 侧改用 `frac` 的方案补丁，保留 CPU 路径作回退（环境变量 `TF2_NATIVE_UV_CPU=1`），并解释为什么不会破坏 untiled 路径与 detail 纹理分支（`native_renderer_shaders.cpp:388-391`）。

### G8（中）N18 viewmodel 门闩

**背景**：`main.cpp:3296` 的 `commandViewModel` 为空 → `:8601` `viewModelRequested=false` → `:8985/:10155/:14704` 跳过；`:3314` `viewModelApplyAnimation=false` → `:15024` else → `:15041` `clearViewModelBones`；绘制端 `native_renderer_frame.cpp:2031`、`native_renderer_viewmodel.cpp:648`、`entity_model.cpp:133-136` 跳过 `v_*.mdl`；LOD 侧 `model_loader.cpp:1496-1508` + `:195-246`。

**要求**：给最小补丁让 `v_*.mdl` 能进绘制，**默认关、环境变量开**；逐条列出你打开/关闭了哪道门闩、每道的开关位置。

### G9（轻）N11 七图采集脚本

**要求**：`scripts/map-matrix.sh`：对 7 张图各跑一次扫描与帧捕获，输出 JSON Lines `{map,status,tickFrames,entities,faces,bytes}`；不许改产品代码。我们负责跑。

## 4 明确不接的

- 任何要运行时 verdict 的：五门禁、像素/画面正确性、帧捕获比对、GPU 路径。
- 契约变更（退出码、开关语义、文件格式）。
- 一次性整包重构 `wWinMain` / `main.cpp`。
- 依赖截图的判断（你看不到图，别猜）。

## 5 批次与顺序

| 批次 | 内容 | 预计轮数 |
|---|---|---|
| 1 | G4 阶段 1（seam map）+ G6 一致性表 | 2 |
| 2 | G1 阶段 A（191 条 flag 清单） | 1 |
| 3 | G1 阶段 B 轮 1 + G5 | 2 |
| 4 | G2 LOD + G7 UV | 3 |
| 5 | G3 粒子 D3.1–D3.4 | 4 |
| 6 | G4 阶段 2 + G8 + G9 | 3 |

先只交批次 1。**不要**在没拿到上一批我们的反馈前往下跑。

## 6 我们本地怎么验（你不用做）

`bash build-target.sh tf2_demo_native` → `--renderer-resize-selftest`（PASS checks=30）→ `bash tools/warn-check.sh`（WARN-GATE tus=139）→ `bash build-identity-check.sh`（assertions=222）→ 画面相关再跑 `frame-capture-check.sh`。源码动过就重跑 identity，它会把 exe sha、HEAD、native 树 sha 一起钉住。

## 7 参考实现（只读对照，不是依赖）

对上引擎语义时，下面这些仓库可以直接对读。**只做交叉验证：不要抄进补丁，也不许声称你编译/读过整个库。**

### 7.1 游戏本体源码（前三行是泄露源）

> **⚠ 风险提醒**：Valve **从未正式开源 TF2**。下表前三行是 2020-04-22（或 2018 版）**泄露**的源码镜像，有版权与商用风险。本项目的要求：只允许用来核「某个字段/某条语义是什么意思」，**一行都不许复制**，也不要在提交信息、文档、交付物里引用这几个仓库；能用下面合法来源解释同一件事的，就不要碰泄露源。

| 参考库 | 星 | 说明 |
|---|---|---|
| `https://github.com/OthmanAba/TeamFortress2` | 321 | 2020-04-22 泄露原版，最完整 |
| `https://github.com/sr2echa/TF2-Source-Code` | 80 | 同一份的镜像 |
| `https://github.com/lua9520/source-engine-2018-hl2_src` | 324 | 2018 年版泄露 |
| `https://github.com/NicknineTheEagle/TF2-Base` | 286 | **合法替代**：把老版 TF2 移植到官方开源的 `source-sdk-2013` 上 |
| `https://github.com/ValveSoftware/source-sdk-2013` | 10140 | Valve 官方开源的同代 Source 引擎与游戏层代码 |
| `https://github.com/ValveSoftware/Source-1-Games` | — | Source 1 各游戏的游戏层代码（按分支发布） |
| `https://github.com/ValveSoftware/halflife` | — | HL1 SDK，只作老式粒子/精灵对照 |

### 7.2 Demo 解析器（对你项目最有用）

| 仓库 | 语言 | 说明 |
|---|---|---|
| `https://github.com/demostf/parser` | Rust | 事实标准，demo.tf 官网在用 |
| `https://github.com/demostf/demo.js` | TypeScript | 是解析规范的另一份实现，适合对拍字段语义 |
| `https://github.com/PazerOP/DemoLib2` | C++ | 完整重实现，最接近本仓技术栈 |
| `https://github.com/Nocrex/tf2-demo-player` | Rust | 同样是播放器，可对读播放语义 |
| `https://github.com/programmer1o1/demoscope` | Rust | 通用 demo 解析 + 3D 可视化 |

**用途**：本仓 `native/` 有自研 `.dem` 解析。核字段语义（tick 语义、`temp_entities`、`usercmd`、字符串表）时以 `demostf/parser` 与 `demostf/demo.js` 为准，不要凭猜测改解析器。

### 7.3 BSP 地图解析

| 仓库 | 语言 | 说明 |
|---|---|---|
| `https://github.com/Galaco/bsp` | Go | 轻量 BSP 读取，适合核 lump 结构与边界 |
| `https://github.com/tasbox-org/source-parsers` | C++ | 完整 Source 格式解析，含 BSP/VPK |
| `https://github.com/pySourceSDK/ValveBSP` | Python | 便于快速比对某个 lump |

**用途**：对读 `native/src/bsp_map.cpp` 的 lump 解析、`sky_camera` 字段（`:628-640`）、面/顶点索引的符号与范围。

### 7.4 物品 Schema（后续 HUD/装备显示用）

| 仓库 | 说明 |
|---|---|
| `https://github.com/SteamTracking/GameTracking-TF2` | 自动追踪每次更新，schema 与本地化字符串都在 |
| `https://github.com/tsuza/tf2-item-schema` | 持续更新的 item schema |

### 7.5 用在哪（任务 → 参考库）

| 任务 | 对读哪个 |
|---|---|
| G2 LOD 选级 | `source-sdk-2013` 的 `public/studio.h` 与 studio 渲染代码（switch 距离、LOD 记录布局） |
| G3 粒子 operator 求值 | `source-sdk-2013` 的 client 粒子代码；TF2 特有 operator 用 `TF2-Base` 核 |
| G7 逐顶点 UV / 纹理 | `source-sdk-2013` 的 materialsystem 相关代码 |
| G8 viewmodel 门闩 | `TF2-Base`（合法）优先，泄露源仅交叉检查 |
| `.dem` 字段语义 | `demostf/parser`、`demostf/demo.js`、`DemoLib2` |
| BSP lump / 实体 | `Galaco/bsp`、`tasbox-org/source-parsers`、`ValveBSP` |
| G4/G1/G5/G6 | 与本条无关，不必对读外部库 |

### 7.6 对读规则（违规即退回）

1. 引用任何外部源码结论，必须写「库 + 路径 + 文件 + 行号」，并说明它对应本仓哪一处（本仓行号）。
2. 外部源码只能解释**语义**，不能证明「本仓现在是什么样」——本仓的证据只认本仓行号与本仓文档里的字节读数。
3. 链接打不开或版本对不上，就以本仓 `docs/` 里已有的证据为准，并在交付里标注「未能对读外部源码」。
4. 外部库许可证与本仓不同，**一行代码都不许复制进来**；只许按语义重写。
5. 泄露源（7.1 前三行）只许用来核「某个字段是什么意思」，**不许复制、不许出现在提交信息/文档/交付物里**；能用 `TF2-Base`、`Source-1-Games` 解释的就不要碰泄露源。
## 8 你上次踩过的坑（这次别踩）

1. P9 补丁用 **4 空格**缩进、仓库是 2 空格 → 三个 hunk 全 `patch does not apply`。这次先确认缩进。
2. 静态评审把「CPU 边函数」引成 `main.cpp:8616`，那里实际是 viewmodel 的 lambda；真正的边函数在 `main.cpp:10940-10974`（负容差判断在 `:10955`）。**引用前二次确认符号上下文。**
3. `183` 是旧数字（现 191）。
4. 补丁被 markdown 污染过（行首多空格、代码块符号混入）。只给纯文本。
5. 行号漂移：基线是 `3ef37e6c`，不是 `86c9019e`。