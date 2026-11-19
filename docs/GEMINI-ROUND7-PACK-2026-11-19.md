# Gemini 第 7 轮任务包（多任务）— T1 G8 viewmodel 门闩（代码）+ T2/T3/T4 架构优化（只读设计）

> 基线：`work/integrate` @ `69804728f368099aeda7b2e5a8e89056521b3611`（源码与 `a64b549ffdfb99c9395384683ea42d2a4d791947` 相同，69804728 只加了本文）。本地 = 远端，2026-11-19 21:18 推。
> 本文所有行号都是**这个 commit 上实测**的，不是旧任务包里的行号。旧包里 `main.cpp:3296 / :8601 / :8985 / :10155 / :14704 / :3314 / :15024 / :15041` 那一套已经全部漂移（B1 把 CLI 开关表搬进 `native/src/main_cli.h`，main.cpp 从 16,221 行变成 **16,046** 行）。

## 总览：本轮一次交四件

| # | 任务 | 性质 | 产出 | 写作用域 | 优先级 |
| --- | --- | --- | --- | --- | --- |
| T1 | G8：N18 viewmodel 门闩 | 代码补丁 | unified diff ≤400 行 / ≤3 文件 + 门闩清单表 | `native/src/main.cpp`（新增头文件需报备） | P0，其余三件的前提 |
| T2 | main.cpp 第二批架构分解 | 只读设计 | `docs/ARCH-MAIN-SPLIT-B2-2026-11-19.md` | 只新增这一个文件 | P1 |
| T3 | renderer 共享着色器/绑定契约风险图 | 只读设计 | `docs/ARCH-RENDERER-BINDING-CONTRACT-2026-11-19.md` | 只新增这一个文件 | P1 |
| T4 | N24 phase 2 现状勘察 | 只读，小 | `docs/N24-PHASE2-SURVEY-2026-11-19.md` | 只新增这一个文件 | P2 |

交付顺序 T1 → T2 → T3 → T4，全部在这一轮给完（不要分轮次问我要下一步）。T1 是唯一动源码的任务，**先交 T1**；T2/T3/T4 共用同一基线行号，T1 落地后行号会漂移，但你的引用一律写基线行号并在各文档开头声明这一点。

### 写作用域冲突规则（硬）

- T1 之外**不许改任何源码**；T2/T3/T4 只许各自新建那一个 docs 文件。
- **不许**改既有台账与门禁产物：`docs/TASK-LIST-*.md`、`docs/STATUS-*.md`、`docs/ALLOC-*.md`、`docs/RELEASE-POLICY.md`、`docs/FRAME-CAPTURE.md`、`.github/`、`evidence/`、`native/build-nmake/`。台账由我回写。
- 四件的文件集合互不重叠，你可以一次全交，我并行核对。

### 我怎么验收这四件（你按这个标准写）

- T1：见第 5 节，构建 + 两种抓帧 + 既有 CLI 回退 + 门禁。
- T2/T3/T4：我**逐条抽查行号与数字**（每件抽 8-12 处 grep 对照），任一条对不上就退回该件重交（不整包退回）；文档里凡是写不出 `文件:行号` 的断言，必须在「静态无法定论」里列出，不许含糊过去。

## 0 角色与硬约束（沿用任务包第 0 节，另加三条）

- 你是静态分析与补丁作者：能读源码，**不能构建、不能跑 demo、不能看画面**。
- 不许新增产品 CLI 开关（`wWinMain` 里 190 条 `L"--"` 比较撞过 MSVC C1061）。实验旋钮一律**环境变量 + 默认关**。
- 不许改退出码、`--` 开关语义、trace / frame-capture 文件格式、`.dem` 解析语义。
- 不许碰 `.github/workflows/`、`native/src/main_cli.h`（那是 B1 刚落地的表，动它要重跑整条 CLI 门禁）。
- 代码风格：C++17、2 空格缩进、LF、`/W4 /WX` 下零新增告警、注释写「为什么」。
- **行号引用规则**：本文件给的行号是基线读数；你自己的补丁 hunk 头必须是基线行号，任何偏差超过 3 行的地方要在交付里标注。

## 1 本轮目标（一句话）

让 `v_*.mdl` 在**不传任何 CLI** 的情况下也能进第一人称绘制，驱动方式只允许是一个环境变量；默认（变量未设）必须与该 commit 的现有构建**逐字节一致**。

为什么这是「门闩」而不是「功能缺失」：绘制端早就接通了，卡住的是两个默认值。核实过的读数如下。

### 1.1 两道硬门闩（HEAD 实测）

| 门闩 | 位置 | 当前值 | 作用 |
| --- | --- | --- | --- |
| `commandViewModel` | `native/src/main.cpp:3279` | 空 `std::filesystem::path` | 只有 `--viewmodel <path>` 会在 `main.cpp:4235` 写它 |
| `viewModelApplyAnimation` | `native/src/main.cpp:3297` | `false` | 只有 `--viewmodel-apply-animation` 会在 `main.cpp:4259` 置 true |

派生读数：`main.cpp:8426` `const bool viewModelRequested = !commandViewModel.empty();`。它后面整条链都是 `if (viewModelRequested)` 或它的取反，所以两个默认值一空一假，链上所有东西都不发生：

| 站点 | 位置 | 被挡住的东西 |
| --- | --- | --- |
| 请求构建 | `main.cpp:8810`–`:8831` | 不建 `viewModelRequest`，trace 里就没有 `gpu: viewmodel request ...` |
| 网格缓存判定 | `main.cpp:8538` | `viewModelMeshBufferPath` 永远空 |
| 网格上传 | `main.cpp:8699` | `renderer.uploadViewModelMesh(...)` 不被调用 |
| HUD / 探针取值 | `main.cpp:9980`、`:11658`、`:11666`、`:11676` | 探针直接答 `none` |
| 动画输入查找 | `main.cpp:14529` | 不找 `findViewModelAnimationInput(...)` |
| 位姿应用 / 清表 | `main.cpp:14849` / `:14866` | 走 else，`renderer.clearViewModelBones()` 把模型钉在 bind pose |
| 绘制端契约读数 | `main.cpp:14812` | `state.enabled = viewModelMeshBufferPath == drawnRequest.path` 恒 false |

绘制端本身是通的，不需要你动：`native/src/native_renderer_frame.cpp:2028`–`:2032` 每帧调 `drawViewModel(paneWidth, paneHeight)`（单 pane 或 pane 0 处于玩家视角时），`drawViewModel` 定义在 `native/src/native_renderer_viewmodel.cpp:465`，深度清在 `:648`。世界 pass 跳过 `v_*` 的判定在 `native/src/entity_model.cpp:133`（`isViewModelPath`）与 `:747`（`instance.viewModelSkipped = isViewModelPath(request.modelPath)`），类名跳过在 `:318` / `native/src/entity_overlay.cpp:247`——这些是**正确的**，不要为了「让它出现」去关掉它们（否则世界里会多出一把浮空的枪）。

## 2 环境变量的口径（钉死，不许改名）

- 名字：`TF2_NATIVE_VIEWMODEL_PATH`。
- 值：一个模型路径，短名和全路径都接受，语义与 `--viewmodel <path>` **完全一致**（`normalizedModelPath` 会处理 `models/weapons/v_models/v_*.mdl` 这类前缀）。
- 非空：等价于「`--viewmodel <path>` **且** `--viewmodel-apply-animation`」。
- 空 / 未设：**当前行为逐字节不变**（这是硬验收，见第 5 节）。
- 读取方式照抄本仓先例：`native/src/renderer_internal.h:37` 的 `worldUvPsEnabled()`——`static const bool` + `_dupenv_s` 只读一次，接受 `1/on/true`。G7 刚用这个形状落地并逐字节回退，评审已经过。
- 显式 CLI 优先：`--viewmodel` 给了路径就以 CLI 为准，环境变量只在 `commandViewModel.empty()` 时生效。这条要在注释里写清楚，否则「A/B 两次跑的是同一个路径」会变成读不出来的事故。

## 3 交付物（两件，缺一退回）

### 3.1 门闩清单（只读，本轮的主要价值）

一张表，逐条列出你在 `viewModelRequested` 链上找到的**每一道**门闩（不只是上面那两道，包括你没改的）：

| 门闩 | 文件:行号 | 当前默认 | 它挡住什么 | 本轮怎么处理（开 / 不动 / 后续卡） |
| --- | --- | --- | --- | --- |

要求：
- 每条都要有 `文件:行号` 与实际代码片段（≤1 行）。
- 区分「默认值挡住的」（本轮要开）与「结构性缺失」（本轮不碰，例如队友涂装、LOD1+、武器→模型映射表）。
- 明确写出：`TF2_NATIVE_VIEWMODEL_PATH` 生效时，还有没有别的门闩会把它二次挡住（例如 `viewModelMeshBufferPath` 的缓存比较、`uploadViewModelMesh` 的前置条件、材质槽解析失败导致 meshReady=0）。这一条是**真正能杀死本轮的**，请重点查。

### 3.2 补丁

- 统一 diff（`diff --git`、LF、纯文本、2 空格缩进、**不要**用 markdown 代码块包整份补丁）。
- ≤400 行 diff、≤3 个文件；预计只需 `native/src/main.cpp` 与一个门闩头（若你判断必须新增文件，先说明理由）。
- 每个文件给**改前 sha256**、增删行数、`git apply --check` 的预期结果（不能本地验证就写「未能验证」）。
- **静态无法定论**清单：你判断不了、必须我们跑产品才能定的部分（例如「开了 env 之后 mesh 是否能上传」这类）。
- 结尾 ≤10 行进度小结。

## 4 明确不接（本轮做了也不收）

1. **武器/职业 → `v_*.mdl` 的自动映射表**。本仓 `native/` 里 **grep 不到任何 `v_scattergun` / `v_rocketlauncher` 之类的武器名**，映射只能从 demo 的 weapon precache / 实体 schema 推，那是下一张卡。本轮只接受「env 给路径」。
2. `viewmodel-contract-check.sh` 的两条既有红（详见第 6 节）。别试图修它们，如果补丁让这两个数字变了，必须在交付里报告。
3. LOD1+、`vvdFixups`、队友涂装、split ViewModel 的 pane 归属——都不是本轮。
4. 任何对世界 pass 跳过 `v_*` 逻辑的放宽。

## 5 我方验收（你不用做，但要知道会被怎么判）

1. `bash build-target.sh tf2_demo_native` rc=0，`/W4 /WX` 零新增告警。
2. **默认关逐字节**：不设任何 env，tick-0 抓帧 md5 必须仍是 `0fcb6867c46d55d9ef9fc95bf65596c2`（这是 HEAD 干净构建的规范帧，G7 的 PS=0 也是它）。
3. **开**：`TF2_NATIVE_VIEWMODEL_PATH=models/weapons/v_models/v_scattergun_scout.mdl`，帧必须与第 2 条不同，且画面右下出现武器；trace 里必须出现 `gpu: viewmodel request path=... status=ok`。
4. **既有 CLI 路径不回退**：`--viewmodel models/weapons/v_models/v_scattergun_scout.mdl --viewmodel-render command` 的读数与 `docs/N17-N20-CHAR-COVERAGE-2026-11-15.md:383` 记录的帧一致（`cls-scout` 行，md5 前 12 位 `2e6147c6d24c`）。
5. `BUILD-IDENTITY=PASS assertions=222`，且 argument-refusal / hud / map-overview / frame-capture 四门禁不回退。

## 6 门禁健康（既有红，别背锅）

`viewmodel-contract-check.sh` 当前 `assertions_ok=152`、`VIEWMODEL-CONTRACT=FAIL`，两条红在 N18 之前就存在（同一份门禁在 `t105-anim-audit` @ `979c6904` 与 N18 后的 `5bac7eaa` 上读数相同）：

| 断言 | 979c6904（N18 前） | 5bac7eaa（N18 后） |
| --- | --- | --- |
| all 7 compared frames came from a measured capture point | `0 ≠ 7` | `0 ≠ 7` |
| identity skinning does not deform the bind pose | `1 px / max_delta=1` | `1 px / max_delta=1` |

出处 `docs/N17-BRANCH-AUDIT-2026-11-18.md:199`–`:214`，摘录在 `evidence/n18-gate-health/`。第一条是产品 trace 的 capture tick 全为 0（`sceneTicks=10320`、measured points `10252`），第二条是恒等蒙皮负控在 2,582,352 像素里 1 个像素差 1 级。**这两条与 viewmodel 门闩无关**，是这条门禁自己欠的账。

## 7 参考资料（本仓内，不用外部）

- `docs/N17-N20-CHAR-COVERAGE-2026-11-15.md:236`–`:400`：N18 的覆盖矩阵；`:240` 是 `vpk_query <tf> models/weapons/v_models | grep '\.mdl$'` → 37 个第一人称模型；`:247` 记了短名会 `model did not resolve` 的坑（必须全路径 `models/weapons/v_models/<name>.mdl`，`v_hm_watch` 在子目录）；`:383`–`:384` 是 scout/soldier 两个已验证帧的 md5 与 trace 字段。
- `docs/GEMINI-REVIEW-TRIAGE-2026-11-19.md:30`–`:41`：N18 两道门闩的原始判断（行号已过时，结论仍成立）。
- `evidence/viewmodel/`：材质变异负控的读数文件名本身就说明了「什么会被判红」。

## 8 上一轮踩过的坑（第 6 轮 G7 实测，这次别踩）

1. **锚点缩进**：你上一轮 TSV 里有多条 anchor 写成 4 空格，仓库是 2 空格 → 6 条必须我手改。这次给 **unified diff**，别给整行替换表。
2. **目标文件写错**：上一轮 `WorldWaterVertex` 被指到 `native/include/native_renderer.h`，实际在 `native/include/world_material.h`。引用类型前先 `grep` 定义点。
3. **一处锚点命中多行**：`float4 tangent : TEXCOORD7;` 在 `native_renderer_shaders.cpp` 有 3 处（:90 / :102 / :908）。hunk 里要带足够上下文，别只给一行。
4. **数学口径**：上一轮给的 `(width-1)` 会整体缩放，正确是 `extent/atlasSize`。本轮虽然不是数学题，但「FOV / pane 宽高 / 视口 aspect」这类换算仍然要给推导，不要只给常数。
5. 补丁被 markdown 污染过一次（行首多空格、代码块符号混入）。只给纯文本。

## 9 T1 一句话验收口诀

「不设变量 → 帧还是 `0fcb6867`；设了变量 → 右下出枪且 trace 说 `status=ok`；不新增 CLI；不碰 main_cli.h。」

## 10 T2 — main.cpp 第二批架构分解（只读设计，不许改源码）

### 10.1 背景与边界

B1 已经把 190 条 CLI 开关比较里的一部分抽成表驱动（`native/src/main_cli.h`，376 行 / 34 条表项），`main.cpp` 从 16,221 行降到 16,046 行，六门全绿。接下来要回答的问题是：**第二批搬什么、按什么顺序搬、每批怎么证明是纯搬运**。这一轮不要你写补丁，只要一张能照着做的清单。

硬边界：

- 任何一步都不许改 `wWinMain` 里 `--` 开关的**比较顺序**（曾撞 MSVC C1061，且开关之间有先后依赖）。
- 任何一步都必须是**纯搬运**：不改逻辑、不改语义、不改 trace 字段、不改帧。
- 不许提出「顺便重构/顺便删死代码」——那是另一张卡，本轮只列搬运。

### 10.2 先做量化盘点（给出命令 + 结果，不要凭印象）

1. `wc -l native/src/main.cpp`、`ls native/src/*.cpp | wc -l`（TU 数）。
2. `git log --numstat -200 -- native/src/main.cpp` 聚合成「变更热点」：按函数归属给 top 15-20 个热点函数（名称 + 行区间 + 近 200 次提交的增删行数）。没有 git 历史的函数就标「无历史」。
3. `wWinMain` 的行区间、局部变量数、直接调用函数数、以及它内部最大的一段线性代码（行区间 + 做什么）。
4. **隐式状态清单**：`native/src/main.cpp` 里文件作用域的可变全局 + 函数内 `static` 变量，逐个给「行号 / 名字 / 谁读 / 谁写 / 跨不跨 TU」。这是能不能搬的**真正判据**——凡是靠隐式状态耦合的，必须先参数化才能搬，请在清单里标出来。
5. 当前构建读数：exe 字节数、编译时间（不能构建就写「未测」并说明你用的替代推断）。

### 10.3 交付表（`docs/ARCH-MAIN-SPLIT-B2-2026-11-19.md`，≤250 行）

| 批次 | 提取目标（函数/结构/块） | 起止行（基线） | 迁往新 TU | 预计 diff 行数 | 纯搬运判定依据 | 该批验收 | 回退方式 |
| --- | --- | --- | --- | --- | --- | --- | --- |

要求：

- 选 **3-5 批**，每批 ≤400 行移动、可独立回滚、彼此不重叠；按「依赖先解耦的先做」排序（先参数化隐式状态，再搬代码）。
- 「纯搬运判定依据」必须是可检验的：例如「该函数只读 3 个入参 + 2 个全局，全局已在第 N 批参数化」「移动后 obj 数 +1，头文件包含集合不新增 X」。
- 「该批验收」必须包含：构建 rc=0 / `BUILD-IDENTITY=PASS assertions=222` / argument-refusal + hud + map-overview + frame-capture 四门禁 / **tick-0 帧 md5 仍是 `0fcb6867c46d55d9ef9fc95bf65596c2`**（纯搬运的证据就是帧不变）。
- 单列一节「不建议搬的」（例如 `wWinMain` 主体、与 C1061 相关的比较链），说明原因。
- 末尾「静态无法定论」：哪些批必须我实际构建才能判断。

## 11 T3 — renderer 共享着色器 / 绑定契约风险图（只读设计，不许改源码）

### 11.1 为什么这张图现在必须有

G7（世界 UV 平铺）刚踩过一个坑：`worldPixelShader_` 是**共享**像素着色器，entity 模型 pass（`native_renderer_frame.cpp:1450`）也绑它，而 `entityModelVertexSource` 的 Output 从未声明 `TEXCOORD8`；本驱动对未写插值读作 1.0（`native_renderer_shaders.cpp:902-917` 记了 T61 那次事故），于是新语义在实体上变成垃圾，整帧 68% 错。这类事故的根因不是某一行代码，而是**没有一张「谁绑了什么、谁必须声明什么」的契约表**。

### 11.2 交付（`docs/ARCH-RENDERER-BINDING-CONTRACT-2026-11-19.md`，≤250 行）

1. **绑定矩阵**：`native/src/native_renderer_frame.cpp` 里每个 draw 站点一行，列 `{行号, VS 变量名, PS 变量名, InputLayout 变量名, 顶点 stride/结构体, 混合状态, 深度状态, 这个 pass 画什么}`。站点至少覆盖：sky / 世界 opaque / 世界 alpha-test / water / entity markers / entity model / viewmodel / overlay / 2D HUD 类。
2. **共享资源清单**：被 ≥2 个 pass 复用的 shader、state、input layout，逐个列出使用者行号与「复用是故意的还是历史遗留」。
3. **插值语义契约表**：每个 `TEXCOORDn` / `SV_*` 语义在哪些 VS 的 Output 里声明、哪些 PS 的 Input 里消费、未声明时本驱动的行为（引用 `native_renderer_shaders.cpp:902-917`）。特别标出「PS 消费但某个喂它的 VS 没声明」的组合——这是事故面。
4. **新增语义检查清单（要能脚本化）**：例如「① grep 所有绑该 PS 的 VS 站点 → 每个都在 Output 声明并写中性值；② 若改 `WorldVertex`/`WorldWaterVertex` → 同步 `offsetof` 链 + `static_assert(sizeof)` + `worldElements` 偏移 + `native_renderer_entity.cpp:154/:165` 的 48B 缓冲 memcpy（那里已经越界，见备注）；③ 跑 tick-0 帧 md5 对照」。每条给命令或判据。
5. **按风险排序的 5 条债**：每条给 `文件:行号` + 触发条件 + 建议动作 + 「本轮该不该做」。已知至少两条必须上榜：`native_renderer_entity.cpp:154/:165` 用 `sizeof(WorldVertex)` 给 48B 的 `EntityModelGpuVertex` 缓冲做 ByteWidth/memcpy；`native_renderer.cpp` 的 `worldElements` 条数与 `CreateInputLayout` count 曾经不一致（HEAD 8 条传 9）。

约束：只读。行号必须基线实测；矩阵里出现你**没实际读到**的组合一律写进「静态无法定论」，不要凭 plausibility 填表。

## 12 T4 — N24 phase 2 现状勘察（只读，≤80 行）

从 `docs/TASK-LIST-*.md` 里找到 N24 条目，读清楚 phase 2 到底要什么，然后产出 `docs/N24-PHASE2-SURVEY-2026-11-19.md`：

1. N24 phase 2 的原始定义（引用台账 `文件:行号` 与原文 ≤2 行）。
2. 现状：相关代码在哪（`文件:行号`）、现在能做什么、phase 1 已合入的部分是什么。
3. 缺口清单：逐条「缺什么 / 卡在哪一行 / 是结构性的还是缺个开关」。
4. 与 renderer/HUD seam 的关系：一句话说清它和 T3 那张矩阵有没有交叉。
5. 「如果要做，第一步是什么」（一句话，不展开设计）。

不做设计、不写补丁、不给工作量估计。如果台账里 N24 的描述不足以支撑上面 5 条，就把缺的信息列成「需要我补的问题清单」交回，不要自己编。

## 13 全包交付顺序与回复格式

1. T1 补丁（+ 门闩清单）——单独一段，纯文本 diff。
2. T2 / T3 / T4 三个文档全文（各自独立段落，标明文件名）。
3. 最后 ≤15 行总小结：四件各交了什么、哪一件你自己觉得最不可靠、需要我先跑什么。

如果 T1 你判断有两条以上实现路线，**不要**先问我：选一条你认为最稳的写完整补丁，另一条用 ≤5 行说明为什么不选。我要的是可构建的东西，不是选择题。
