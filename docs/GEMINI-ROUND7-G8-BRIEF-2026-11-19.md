# Gemini 第 7 轮任务书 — G8：N18 viewmodel 门闩（默认关、环境变量开）

> 基线：`work/integrate` @ `a64b549ffdfb99c9395384683ea42d2a4d791947`（本地 = 远端，2026-11-19 21:12 推）。
> 本文所有行号都是**这个 commit 上实测**的，不是旧任务包里的行号。旧包里 `main.cpp:3296 / :8601 / :8985 / :10155 / :14704 / :3314 / :15024 / :15041` 那一套已经全部漂移（B1 把 CLI 开关表搬进 `native/src/main_cli.h`，main.cpp 从 16,221 行变成 **16,046** 行）。

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

## 9 一句话验收口诀

「不设变量 → 帧还是 `0fcb6867`；设了变量 → 右下出枪且 trace 说 `status=ok`；不新增 CLI；不碰 main_cli.h。」
