# 第 8 轮任务书（G9 N18 pose 勘察 / G10 D02 skybox 设计 / G11 两条门禁红取证）

写给 Gemini：这一轮**三件都只读**（不写代码、不改门禁），产出是三个 markdown 文件。
基线、锚点、验收口径都在下面钉死。全文用中文，ASCII 标识符保持原样。

## 0. 这一轮为什么没有源码补丁（实测数字，不是偏好）

- **B2-1（关闭汇总块搬运）盲写风险太高**：块本身是 `native/src/main.cpp:15744–16068`（325 行），
  直接引用 8 个跨循环局部（`renderer`、`persistent`、`assets`、`demoNetworkSummary`、
  `hudConfigRequested`、`hudRoot`、`hudLayout`、`message.wParam`）和 6 个文件作用域全局
  （`g_cameraBookmarks`、`g_keyBinds`、`g_keybindsLoaded`、`g_hud`、`g_reviewSession`、
  `g_cameraScriptExitCode`）。参数列表只能靠读代码重构，编译一次才知道对错 —— 这种做法留给我在本地做。
- **B2-4（`main_cli.h` 第二批扩表）不够一件**：T2 设想的「40 条纯布尔开关」实测只剩 7 条单行臂
  （`main.cpp:3715`/`:3716`/`:3717`/`:3718`/`:3739`/`:3839`/`:3947`，形如
  `if (wcscmp(arguments[i], L"--x") == 0) { y = true; continue; }`）。
- 所以这一轮的分配原则是：**你只做"读仓库就能读对"的活**，需要编译/抓帧/看画面的活我本地做。

## 1. 基线与环境

- 基线：`origin/work/integrate` @ `8053f34215ed3448c588919d3aa521ed11f293d1`（2026-11-19）。
- 仓库事实（我在本机实测）：`native/src/main.cpp` **16,068 行**、`native/src/*.cpp` **83 个**、
  `native/src/main_cli.h` **376 行**、`native/src/main.cpp` 里 `L"--` 字面量 **159 个**。
- 你能看到：GitHub 上的仓库内容（含 `evidence/`、`docs/`、门禁脚本）。你看不到：
  `.scratch/`（未入库的抓帧脚本与产物）、构建产物、实际画面、本机 TF2 安装。
- 产品构建（本机，不在你的环境）：`bash build-target.sh tf2_demo_native`，约 2–3 分钟。

## 2. 交付格式（硬规则）

三件各交付**一个 markdown 全文**（不要打包 zip、不要 unified diff、不要动其它文件）：

| 件 | 交付文件 | 上限 |
|---|---|---|
| G9 | `docs/N18-VIEWMODEL-POSE-SURVEY-2026-11-19.md` | ≤130 行 |
| G10 | `docs/D02-SKYBOX-DESIGN-2026-11-19.md` | ≤130 行 |
| G11 | `docs/GATE-REDS-FORENSICS-2026-11-19.md` | ≤130 行 |

- 只新建这三个文件；**不改**任何代码、台账、`evidence/`、门禁脚本；不碰 `native/src/main_cli.h`。
- 纯 UTF-8、LF 行尾；不要写 CRLF（CI 有行尾守卫）。
- 每条结论后面跟 `文件:行号`；没有行号的判断句不接受。行号以 **HEAD `8053f342`** 为准（G8 之后
  `main.cpp` 比 T2 文档里的基线多 22 行：环境变量门闩插在 `persistent.render.normalize();` 之后）。

## 3. G9：N18 viewmodel pose 勘察（最重要的一件）

**要回答的问题**（按顺序，答不出就写「静态无法判定」并说明需要什么读数）：

1. `poseAvailable=0` 的直接原因链：`main.cpp:14860` 是
   `const bool poseAvailable = !outcome.bindPose && !outcome.skinMatrices.empty();` ——
   `outcome` 是谁产生的（函数名 + 文件 + 行号）？`bindPose` 在什么条件下为 true？
   `skinMatrices` 在什么条件下为空？两者哪个是当前 trace 的命中者？
2. `scene: vmAnim … status=missing`（`main.cpp:14964` 的 trace，summary 在 `:14976`）：
   `status` 字段的取值来自哪个变量/枚举，`missing` 的判定条件是什么（没有 sequence？没有 anim desc？
   实体不是 `*_animations.mdl`？），它和问题 1 是同一个根因还是两条独立路径？
3. `weaponBody` 的拒绝 `noViewModelEntity`（`native/src/entity_model.cpp:439` 是这个原因名的字符串化）：
   判定发生在哪个函数（给行号），输入是什么，为什么 viewmodel 实体没有被当成 weapon body？
4. **要把 viewmodel 画成「右下第一人称构图」，最小改动清单**：现值 trace 是
   `gpu: viewmodel centre viewSpace=(7.49,-5.40,56.00)`（`main.cpp:11845` 一带的 pass 打印）。
   placement 变换现在在哪算（文件/函数/行）？应当乘什么（viewmodel 专用 fov/偏移）？
   动画输入该从哪条通道接（v_*.mdl 的 sequence / `vmAnimApplied` 现成路径能不能复用）？
   需要新增哪些 trace 读数来证明「位置对了」？
5. **不需要改什么**：明确列出哪些看起来相关但**不该动**的地方（例如世界 pass 对 `v_*` 模型的跳过，
   `entity_model.cpp:133/:318/:747` 与 `entity_overlay.cpp:247`）。

**必须包含的锚点（≥12 个 `文件:行号`，每条附你读到的 1–3 行原文）**：
`main.cpp:14859`（`vmAnimApplied` 注释）、`:14860`（poseAvailable）、`:14871`（`poseAvailable && viewModelApplyAnimation`）、
`:14964`、`:14976`、`:11845`、`:14812`/`:14849`/`:14866`（viewmodel 请求与绘制链）、
`entity_model.cpp:439`、`entity_model.cpp:133/:318/:747`、`entity_overlay.cpp:247`、
`native_renderer_viewmodel.cpp:465`（drawViewModel）、`:648`（深度清除）。

**不要做**：不要写补丁、不要改代码、不要建议「先加一堆日志」——我这边随时能抓帧，缺的是代码事实。

## 4. G10：D02 三维 skybox 的设计

`sky_camera` **已经解析**：`native/src/bsp_map.cpp:628–638`（照 `entity.hasOrigin` 与
`entity.scale` 写进 `map.skyCamera`）。缺的是**渲染侧第二遍与深度清除**。要写三节：

1. **现状**：`BspSkyCamera` 结构（`native/include/bsp_map.h`）有哪些字段、现在谁读它（给行号，读不到就写「无人读取」）；
   世界 pass 的绘制顺序与深度状态（`native/src/native_renderer_frame.cpp` 的 world pass 与 `:648` 一带的
   `ClearDepthStencilView` —— 以你实际读到的行号为准）。
2. **方案**：3D skybox 面如何用 `skyCamera.origin` + `scale` 构造变换（缩放矩阵该乘在哪个阶段：模型矩阵还是视图矩阵）；
   为什么要单独一遍；深度清除的时机（先清后画还是后画覆盖）；与现有天空盒/背景的绘制顺序关系；
   面剔除与 `map.world` 的 brush 分类怎么区分（给现有代码里可复用的判据）。
3. **验证计划**：用哪类地图能证明（含 `sky_camera` 实体）、看什么 trace 读数、什么负控（改了必须变、不改必须不变）。
明确写「这一版不做什么」（例如不补 `sky_camera` 的朝向字段、不动现有天空渐变）。

## 5. G11：两条既有门禁红的取证

输入全部已入库：`evidence/n18-gate-baseline-2026-11-19/README.md`（83 行）与
`evidence/n18-gate-baseline-2026-11-19/n18-contract-baseline.log.txt`（290 行），门禁脚本 `viewmodel-contract-check.sh`。

1. **capture point 0/7**：前提常量在 `viewmodel-contract-check.sh:714`（`CAPTURE_SCENE_TICKS="10252"`，
   注释说取自 pinned demo 的 `firstPacketTick=10252`），提取器在 `:373`（对每个 trace 取
   `draw: begin` 之前最后一条 `scene: begin`），断言在 `:740`。实测 trace 是
   `draw: begin tick=0` + `scene: begin sceneTick=10320`。请回答：10320 与 10252 的差从哪来
   （启动期 tick 推进？`--start-paused` 的 hold？demo 头部 vs 第一包？），**该改前提值还是改实现**，
   以及改前提会不会让这条断言变成永远为真。
2. **identity skinning 1 px**：断言在 `:828`（`--max-delta 0`，比 `vmident.bmp` 与 `vm90.bmp`），
   实测差 1 个像素、`(658,416)` G 通道差 1 个 LSB（其余 2,582,351 像素相同）。
   请回答：一个 LSB 的可能来源（浮点→8bit 舍入、图像读回路径、GPU 采样精度），
   断言该不该是 `<= 1` 并给出理由，以及**为什么另两组 compare 也用了 `--max-delta 0` 却没红**（`:752`/`:754`/`:772`/`:851`）。
3. 每节最后给一句裁决：**改前提 / 改实现 / 改断言 / 保持并记录为已知**，并说清代价。

**不许**：改门禁脚本、改 `evidence/`、把「重跑更宽的容差看还红不红」当结论。

## 6. 验收与退回（我这边跑）

- 抽检：每件随机 8–12 处 `文件:行号`，逐条 `grep -n` 对照；对不上只退那一件，不影响其它两件。
- 另外抽 3 处「你引用的原文片段」与文件实际内容逐字比对。
- 不接收：没有行号的结论、把静态推断写成事实的句子、「建议加日志」式的交付。

## 7. 自报（每份结尾必须有）

一段 ≤6 行的「我不确定的地方」：哪几处是运行时行为我读不到、哪几条结论只是静态推断、
以及最可能被我驳回的那条。
