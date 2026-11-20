# 第 9 轮任务书（G12 第一人称可见性 / G13 门禁前提普查 / G14 B2-1 尾段搬运施工单）

写给 Gemini：这一轮**三件都只读**（不写代码、不改门禁、不改台账），产出是三个 markdown 文件。
全文中文，ASCII 标识符原样。每条结论后面必须跟 `文件:行号`。

## 0. 这一轮为什么不派源码补丁

- **N18-B（POV 自身身体隐藏）我本地刚刚验证通过**：`scene: cameraFilter removed=1 indices=9`、
  `anim: ent=9` 行消失、帧 md5 从 `5c5e874a…` 变成 `3bf37d4f…`。它的位置、条件、负控都在我手里，
  写补丁需要编译 + 抓帧（本机），不适合静态交付。
- **B2-1 的搬运是 325 行跨循环代码**（8 个跨循环局部 + 6 个文件作用域全局），直接给 diff 必错；
  「施工单」这种"读仓库就能读对"的活才是可交付的。
- 上一轮 G9/G10/G11 的三份交付已被我逐条核对：**锚点基本全对**，但有 5 处要改（见 §1 末）。
  这一轮的验收口径不变，锚点仍然逐条抽检。

## 1. 基线与环境

- 基线：`origin/work/integrate` @ `28aba482`（2026-11-20）。你看到的仓库以你能取到的最新提交为准；
  **行号以你实际读到的为准**，并在每节开头写一句「本节锚点对应提交 <sha>」。
- 我本地有一份**未提交**的工作树改动（N18-B + 门禁 m4 修复 + 三份新 docs），你看不到，不要假设它在。
  若你发现某行比本文写的位置整体偏移若干行，**如实写偏移量**，不要凑数。
- 仓库事实（我本机实测，HEAD 28aba482）：`native/src/main.cpp` **16,090 行**、`native/src/*.cpp` **83 个**、
  `*.sh` 门禁脚本在仓库根目录（+ `evidence/**` 里有一些取证专用脚本）。
- 你看不到：`.scratch/`（未入库的抓帧脚本与产物）、构建产物、实际画面、本机 TF2 安装与 demo 文件。
- 产品构建（本机，不在你的环境）：`bash build-target.sh tf2_demo_native`，约 2–3 分钟。

**上一轮已修正、这一轮不要再犯的 4 条**：
1. `state.enabled = viewModelMeshBufferPath == drawnRequest.path;` 在 **`main.cpp:14834`**（不是 :14812）；
2. `status=missing` 的真正早退是 **`native/src/viewmodel.cpp:464-467`** 的 `if (!input.entityPresent)`；
3. NDC 构图**已由 `7250df6c` 实现**（`native_renderer_viewmodel.cpp` 的 `kViewModelFramingNdcX/Y`），不要再建议；
4. `main.cpp:15202` 一带的相机决策块**在候选过滤之后**，写在那里的隐藏只影响下一个 scene tick（暂停 demo 永远等不到）。

## 2. 交付格式（硬规则）

三件各一个 markdown 全文（不打包、不 diff、不动其它文件）：

| 件 | 交付文件 | 上限 |
|---|---|---|
| G12 | `docs/N18-FIRSTPERSON-VISIBILITY-2026-11-20.md` | ≤130 行 |
| G13 | `docs/GATE-PREMISE-AUDIT-2026-11-20.md` | ≤130 行 |
| G14 | `docs/B2-1-SHUTDOWN-MOVE-SPEC-2026-11-20.md` | ≤130 行 |

纯 UTF-8、LF 行尾（CI 有行尾守卫）。每条结论给 `文件:行号` + 你读到的 1–3 行原文。

## 3. G12：第一人称下「谁能被看见」（最重要的一件）

背景（我本机实测的 trace，你可当已知输入）：POV 录制（pl_upward，狙击手）里
`scene: eye origin=375.1,-2871.0,213.9 offsetZ=75.0001 eye=375.1,-2871.0,288.9 viewEntity=9 … reason=applied`
——摄像机眼 = 玩家 9 的 origin + viewOffset(75)。世界 pass 把 ent 9 的身体画在同一 origin 上，
所以修前画面右侧被帽子/头/躯干包住；隐藏后画面干净，剩下的枪与双臂来自 viewmodel pass：
`gpu: viewmodel pass … armDrawCalls=1 weaponDrawCalls=1 … drawnModel=models/weapons/c_models/c_sniper_arms.mdl`
与 `gpu: viewmodel weaponBody … entity=734 … path=models/weapons/c_models/c_sniperrifle/c_sniperrifle.mdl status=ok meshReady=1 bones=6 vertices=23061`。

**要回答的问题**（答不出写「静态无法判定」并说明缺什么读数）：

1. **手里那支枪是谁画的、用的什么变换**：viewmodel pass 的 `weaponDrawCalls=1` 画的是 c_ 武器体；
   它的模型/世界矩阵从哪来（函数名 + 文件 + 行号）？和手臂（`c_sniper_arms.mdl`，`attachment=weapon_bone`）
   是不是同一个变换源？如果不是同一个源，静态推演会出现什么画面症状（错位/悬空/比例不符）。
2. **为什么用 c_ 体**：`models/weapons/v_models/v_sniperrifle.mdl` 在本机 VPK 里不存在
   （trace：`status=failed … reason=viewmodel was not found`）。仓库里选择「c_ 体当第一人称武器」的判据在哪
   （函数/行号）？这是 fallback 还是设计？给行号证明。
3. **归属**：第一人称下本地玩家的武器 ENTITY（ent 734）**会不会也被世界 pass 画**？
   请用静态路径回答：候选列表过滤（`g_spectateCameraHiddenEntity` 那段）是否覆盖它、
   `findViewModelWeaponBodyInput`（`entity_model.cpp:454`）与拒绝名单（`:436-449`）会不会拦它，
   `anim: ent=734` 这类行会不会出现。如果两个 pass 都可能画同一把枪，指出**当前是靠什么参数区分的**。
4. **判据该放哪**：如果要把「本地玩家在第一人称下不该被画的东西」收敛成一条规则（身体 / 武器实体 / 手臂），
   应该改哪一层（候选过滤 / viewmodel pass 的输入挑选 / entity_model 的拒绝），并给出**不破坏**
   spectate 跟随、第三人称、`--debug-camera`、`--pane-split` 双窗的理由。
5. **最小改动清单**：改哪个文件哪个函数，需要新增哪些 trace 字段来证明（字段名 + 期望值形态），
   以及**不该动什么**（NDC 构图已完成；不要改手臂蒙皮；不要靠猜 v_ 路径；不要动 `1250` 的 placeSpectatorView 语义）。

**必须覆盖的锚点**：`main.cpp:4358`/`:4367`（`--viewmodel-weapon-body` 解析）、`:3221`（该开关的说明）、
`:14687`（weapon body 被关掉时的文案）、`:1250`（placeSpectatorView 定义）、`:1327-1350`（隐藏被跟随者的写入点）、
`main.cpp` 候选过滤块的 `hiddenForCamera` 行、`entity_model.cpp:436-449`/`:454`、
`native_renderer_viewmodel.cpp` 里 viewmodel pass 的 draw 与形态（`drawViewModel` 及其调用者）。

## 4. G13：门禁前提的过期普查（续 m4 债）

背景：门禁脚本里有大量**写死的前提**，某次重构后前提失效但断言还在跑，只有等它红才发现。
已知两例（已处理/已知，作为校准样本，不要重复当新发现）：
- `viewmodel-apply-check.sh:717/718/724` 的 mutation 目标曾是 `native/src/native_renderer.cpp`，
  函数已搬到 `native_renderer_viewmodel.cpp` —— **已修**；
- `viewmodel-contract-check.sh:714` 的 `CAPTURE_SCENE_TICKS="10252"` 与实测 10320 不符
  （根因 = T63-F1 两套 tick 空间：signon entry tick 与 server tick），**:363-364 的注释也过时**。

**要做的事**：把仓库里所有 `*.sh`（根目录门禁 + `evidence/**` 里的取证脚本）的**写死前提**盘一遍，
按下面的分类找**新**问题，产出一张审计表 + 修复优先级：

| 类型 | 例子 | 证据要求 |
|---|---|---|
| A 硬编码行号 / patch 目标 | `patch_source` 指向的文件已拆分 | 给出 `grep -c` 命中 0 的结果 |
| B 硬编码常量（tick / 帧号 / 计数） | `CAPTURE_SCENE_TICKS`、期望帧数、期望实例数 | 给出实测值来源或推断理由 |
| C grep 探针已无命中 | 断言的 trace 行形状被改过（字段改名/前缀变了） | 给出该 trace 行的现行写法与出现位置 |
| D 断言恒真 | 前提被改后条件永远成立（例如把「不等于 0」改成「>= 0」） | 给出为什么恒真 |

规则：
- **至少给 8 条**新发现，最多 20 条；每条给 `脚本:行号`、前提原文、为什么可能过期、判定置信度（高/中/低）、
  修复动作与代价（一键改常量 / 要重抓 / 要改实现）。
- 置信度「高」必须附**可验证的仓库内证据**（grep 计数、文件里已无该符号、函数已搬家）。
- 不许跑门禁、不许改脚本、不许把「可能」写成「是」。
- 最后给一段「这一轮没有覆盖的门禁」：哪些脚本太慢/依赖本机 demo，只能我在本地跑。

## 5. G14：B2-1 尾段搬运施工单（`main.cpp` → `main_shutdown.cpp`）

背景：`wWinMain` 的 C6262（58,208 B 栈）只处理了前三处，尾段（关闭汇总块）还留在里面。
上一轮判定「盲写风险太高」，因为它依赖跨循环局部状态。这一轮只要你写**施工单**，不写代码。

1. **定位**：用内容锚点（不是固定行号）找出该块：从 `wWinMain` 里「关闭汇总」相关的注释块开始，
   到 `wWinMain` 返回之前结束。写出你读到的**起止行号**与首尾各 2 行原文。
2. **符号清单**：块内引用的
   - 跨循环局部（名字 + 声明行号 + 类型）
   - 文件作用域全局（名字 + 声明行号）
   - 命名空间/静态函数（含 `main.cpp` 内的 `static`/`namespace {}` 符号）
   逐条列表；标出哪些**只能**通过参数传入（因为声明在 `wWinMain` 内）。
3. **参数表设计**：给出新函数签名（`void …(…)`）与调用点建议（一行）。说明为什么这样分组
   （按用途分而不是按出现顺序分），以及哪些参数可以合并成结构体、为什么现在不合并。
4. **依赖与顺序风险**：撤离后哪些对象的生命周期/析构顺序会变？哪些 `return`/`exit` 早退路径会漏掉新函数？
   有没有 `goto`/`break`/`continue` 依赖块内作用域？逐条给行号。
5. **验证计划**：编译命令、应该跑哪几个门禁、应该对比哪些 trace 行（给出现行 trace 行形状），
   以及**怎么证明行为没变**（同输入两次运行、关键 md5/计数）。
6. **不做什么**：不改行为、不顺手重命名、不搬 `wWinMain` 里的主循环、不改 CLI 解析、不动 `main_cli.h`。

## 6. 验收与退回（我这边跑）

- 每件随机抽 8–12 处 `文件:行号` 逐条 `grep -n` 对照；对不上只退那一件。
- 另抽 3 处「引用的原文片段」逐字比对。
- 不接收：没有行号的结论、把静态推断写成事实、把「建议加日志」当交付、与 §1 修正 4 条冲突的建议。

## 7. 自报（每份结尾必须有）

≤6 行的「我不确定的地方」：哪几处是运行时行为你读不到、哪几条只是静态推断、最可能被我驳回的是哪条。
