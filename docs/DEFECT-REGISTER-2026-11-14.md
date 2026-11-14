> **历史附录（2026-11-14 起冻结）。** 本文件的唯一台账职责已移交
> `docs/DEFECT-REGISTER-MASTER-2026-11-14.md`。原因：本批 G1/G2 追加结果时复用了
> D33/D34/D35 三个编号，与本文主表已有的同名号冲突（同一号既"已关"又"未关"）。
> 主表已把冲突项改号为 D41/D42/D43 并给出映射；**本文原文保留、不再更新**，
> 两者冲突时以主表为准。

# 缺陷台账（合并版）- 2026-11-14

本文把 `KNOWN-ISSUES-AND-FIXES.md`、`PROJECT-ROADMAP-HANDOFF-2026-11-12.md`（P0-P7、
Open rows、Batch 4、Batch 5）、`PHASE-F-REPORT-2026-11-13.md`（§7 诚实清单 / §11）、
`PROJECT-HANDOFF-2026-11-14.md` §2、`GATES-UNREGISTERED.md`（blocked 类）、
`F2-2-PROBE-SWEEP-2026-11-13.md`、`R2-9-NOCULL-CENSUS-2026-11-13.md` 中的**未闭项**
合并成一份与当前 HEAD 对齐的排期台账。只登记未闭项；已闭项见文末一行说明。

## 基线

| 项 | 值 |
|---|---|
| Worktree | `D:/TF2_Native_Worktrees/t105-e5` |
| 分支 / HEAD | `candidate/e5` / `94e7612` |
| 工作区 | 1 个已修改文件（`docs/PROJECT-ROADMAP-HANDOFF-2026-11-12.md`）+ 1 个未跟踪文件（`docs/PROJECT-HANDOFF-2026-11-14.md`），二者均须保留；**不是 clean** |
| 产品 exe | `native/build-nmake/tf2_demo_native.exe`，SHA-256 `00ec8b99…`（本次只读复算前缀 `00ec8b99fc779327eb2cf0eb` 一致），2,988,544 B |
| identity sidecar | `sourceContent=1d9c0ead…`，`dirty=0`，`sourceTree=6d203db8e729`，`buildUtc=2026-11-13T16:49:55Z` |
| 身份绑定边界 | exe 是 Phase F 报告（终点 `68a206a`）的 canonical build，**未**在 HEAD `94e7612` 重新构建并重绑；本台账所有证据的成立范围以此为准 |
| 本次动作 | 纯只读 + 只新建本文件；未构建、未抓帧、未运行产品 exe、未运行任何 `*-check.sh`、未改动任何已有文件、未 git add/commit/reset/clean |

严重级口径：**P0** 崩溃/数据丢失/安全；**P1** 阻断发布，或首图用户可见缺陷；**P2** 单图/单功能缺陷、
用户可见性未证、或门禁无法表述结论；**P3** 仅工程卫生或证据缺口，不影响当期判定。

类型口径：`产品` = 产品行为/功能缺陷；`门禁` = 门禁脚本或期望值缺陷；`证据` = 产品可能正常但缺少绑定
证据；`工程/文档/范围` = 构建与流程基础。**探针 PASS 不等于渲染 PASS；tick-0 smoke 不等于地图验收。**

## 台账

| ID | 严重级 | 一句话缺陷/缺口 | 状态 | 复现或最小命令 | 现有证据位置 | 建议 owner 角色 | 依赖/阻塞 |
|---|---|---|---|---|---|---|---|
| D01 | P1（阻断发布） | `产品/证据`：七图 POV+STV 的几何/材质/天空/控制点/BSP-道具矩阵未在同一 exe 上完成，仅零散单图单相机证据 | NOT VERIFIED | 每图各一次固定相机抓帧：`tf2_demo_native.exe --tf-root <TF> --demo <dem> --audio-disabled --hidden-window --start-paused --capture-tick <N> --debug-camera <x y z pitch yaw> --capture-frame <bmp>`（host lock 内） | `PROJECT-HANDOFF-2026-11-14.md` §5 H2 Exit；`PHASE-F-REPORT-2026-11-13.md` §7.2 | Map renderer owner + QA | 依赖 D06（身份）；须先有 D01 的 BSP/demo 配对清单（H0 步 4） |
| D02 | P1（用户可见） | `产品`：3D skybox（`sky_camera` 解析/缩放/合成/第二遍绘制）未实现；六面 cubemap 加载只是纹理读取，不算 3D sky | OPEN | 需求卡 F2-5：解析 `sky_camera` 后以命名相机 on/off 抓帧，加 `--no-sky3d` 回退对照 | 路线图 F2-5、Batch 3「E3 3D-skybox design 未落地」；`PHASE-F-REPORT` §7.7 | Map renderer owner | 与 D01 同一 world/frame 写入窗口，单写者串行 |
| D03 | P1（用户可见） | `产品(疑似)`：Sultry 黑区 849,396（vertex colour 0，texel 非黑）与 919,379（face-trace UV 与实际 pass 不一致）未归因 | OPEN | `bash world-black-account-check.sh`；产品侧 `--world-face-trace --world-face-trace-points "849,396;919,379"` + `--world-texel`，相机 `-600 -600 950 20 45`、tick 0 | `PHASE-F-REPORT` §4 F1-5/§6；`world-black-account-check.sh` | Map renderer + QA | 依赖 D06；须一次只改一个已证原因并保留同姿态对照 |
| D04 | P1（用户可见/稳定性） | `产品`：三处已知 weapon OOM/预算用例未按实测 prepared/uploaded/drawn 解决，优先级与溢出语义未定义 | OPEN | 需求卡 H4 步 5 / P4 卡：先取 prepared/uploaded/drawn、纹理内存、实例与 draw call 峰值，再定拒绝策略 | 路线图 P4「Resolve the three known weapon OOM cases」；`GATES-UNREGISTERED.md` `attach-weapon-budget-check.sh` | Character/weapon owner + Architecture | 与 D18 共享 model/renderer 写入窗口 |
| D05 | P1（阻断发布） | `门禁`：15 步 `verify-release.sh` 在冻结候选上从未跑过；所有绿色都是窄门禁 | NOT VERIFIED | `bash verify-release.sh`（仅在冻结候选 + host lock 下，全项目只允许一次授权尝试） | `PROJECT-HANDOFF-2026-11-14.md` §2；路线图 Batch 5 末条 | Integrator / release lead | 依赖 D01-D04 处置与 D06；D05 是最后一步，不得当日常状态检查 |
| D06 | P1（阻断发布） | `证据`：交付 exe 未在 HEAD `94e7612` 重建重绑；sidecar `sourceTree=6d203db8e729` 与文档所称 canonical `f533293` 不一致 | NOT VERIFIED | 重建后比对 `sha256sum native/build-nmake/tf2_demo_native.exe`、`cat native/build-nmake/tf2_demo_native.identity`、`bash build-identity-content.sh hash`（host lock 内） | 本文基线表；`PROJECT-HANDOFF` §2；`PROJECT-ROADMAP` 基线段 | Integrator | 阻塞全部身份敏感证据（D01/D03/D08/D14/D15/D20/D22）；无上游依赖，应最先做 |
| D07 | P1（用户可见） | `证据`：干净受支持环境上的 install → open → play → seek → follow → audio → recovery 全流程未验收，仅在开发机跑过 | NOT VERIFIED | 按 H7 步 5 在干净机走 `cmake --install` 产物路径，记录每步截图/日志与失败恢复 | `PROJECT-HANDOFF` §5 H7.5；`GATES-UNREGISTERED.md` `install-lifecycle-check.sh` / `install-readiness-check.sh` | Integrator + Asset/install owner | 依赖 D06；打包与安装门禁为 segmented，需单独窗口 |
| D08 | P2（证据缺口） | `证据`：小地图七图中仅 Upward、Sunshine 有产品帧；Process/Gullywash/Sultry/Product/Bagel 五图无帧，且既有两帧未与当前身份重绑 | NOT VERIFIED | `bash minimap-hud-check.sh --old-exe=<pre-E9 exe>` 复跑前向 96 断言；五图各补固定相机 `--hud-minimap --capture-frame` | `PHASE-F-REPORT` §4 F2-1；`minimap-hud-check.sh` | UI/HUD owner | 依赖 D06 与 D09（同门禁文件，避免同窗口改） |
| D09 | P2（门禁不可表述） | `门禁`：小地图 A12 变异 m2-heading / m3-anchor 的**期望红因写反**，三个变异实际都被抓到，但 A12 判 FAIL（属门禁缺陷，非产品回归） | OPEN | `bash minimap-hud-check.sh --mutation --old-exe=<pre-E9 exe>`（A12 段会改源并重建，须 host lock） | `PHASE-F-REPORT` §4 F2-1（`MINIMAP-HUD=FAIL assertions=100 failed=2`）；Batch 4 F2-1 条 | UI owner + gate owner | 无上游依赖，gate-only 可并行；解锁 D08 的完整验收 |
| D10 | P2（证据缺口） | `门禁/探针`：`animation_viewmodel_probe` 合成路径 `loop=false`，同 run `realLoop=true`、其余标志全 true；是唯一 OPEN 的探针自检 | OPEN | `animation_viewmodel_probe.exe`（带 `TF_ROOT`）复跑，比对 `loopAndOnce()` 合成断言与真实模型读数 | `F2-2-PROBE-SWEEP` §3/§5；`PHASE-F-REPORT` §4 F2-2 | Animation QA | 无上游依赖，可立即做；先证断言是否过期，禁止为变绿而放宽 |
| D11 | P2（用户可见未证） | `产品`：world brush pass 不带 per-material cull 标志，`$nocull` 仅用于 entity-model range；可见影响未证 | OPEN | 先造 Bagel world-brush 面带 `$nocull` 的正例，再 `--no-world-cull` on/off 同相机抓帧；现 oak 是 static prop，不够 | `R2-9-NOCULL-CENSUS` §"What this does and does not decide"；`PHASE-F-REPORT` §4 F2-3；Batch 4 F2-3 | Map renderer + QA | 与 D01/D02 同一 world 写入窗口；无正例前禁止改 renderer |
| D12 | P2（门禁不可表述） | `门禁`：`world-material-semantics-check.sh` 登记为 blocked，理由仍写"直到 F1-3 重钉"，但 F1-3/I-24 已 CLOSED 且 `material-coverage` 100 断言 PASS | OPEN | `bash world-material-semantics-check.sh`（blocked 类，非注册链；跑前须重新判定其 red 是否仍成立） | `GATES-UNREGISTERED.md` blocked 行；`PHASE-F-REPORT` §4 F1-3 | Map/QA owner + gate owner | 无上游代码依赖；需先解释 6 个读数重钉是否已覆盖该门禁的 pin |
| D13 | P2（用户可见未证） | `产品(疑似)`：world 半透明面在 opaque 之后分组但**未按面 back-to-front 排序**；可见排序错误未量化（R2-10 / F2-4） | NOT VERIFIED | Sultry 玻璃相机（`--debug-camera 949 -1016 784 15 180`）固定 tick 抓帧，与源排序/相机深度/atlas-PVS 约束比对 | 路线图「Candidate-fix review」末条、F2-4；`PHASE-F-REPORT` §7.7 | Map renderer owner | 依赖 D06；无像素 oracle 前禁止写通用排序补丁 |
| D14 | P2（证据缺口） | `证据`：控制点 neutral/capturing/owned、队伍 skin/bodygroup/proxy/texture2/modulation/alpha 未逐态验收 | NOT VERIFIED | 按 P2 顺序：VMT 字段盘点 → parser 语义 → proxy 时间输入 → bodygroup/skin → 三态各抓一帧 → 与 Source 客户帧对比 | 路线图 P2 控制点条目；H2 步 5 | Map renderer owner | 依赖 D01 的每图固定相机与 D06 |
| D15 | P2（证据缺口） | `证据`：Upward 拖车仅 tick 5611/6160 初始证据，未覆盖直线/弯道、静/动、pause/seek/speed 与 PVS | NOT VERIFIED | 固定相机在 5611/6160 之外补直/弯道 tick，记录 model/skin/body/parent/origin | 路线图 P2 Upward 条目；H2 步 6 | Map renderer owner + QA | 依赖 D06；与 D14 同图窗口 |
| D16 | P2（用户可见未证） | `产品`：七图移动 BSP 实体（门/平台/列车/可破坏物）未盘点，解码变换/插值/绘制/像素位移未分层断言 | NOT VERIFIED | `brush_entity_probe`（真实参数）+ 一张图一个移动 brush 夹具与一个 static brush 对照 | 路线图 P2「Inventory moving BSP entities」；H2 步 6 | Map renderer owner | 依赖 D01；实现与 D11 串行 |
| D17 | P2（用户可见） | `产品(资产)`：Sultry 13 / Bagel RC11 4 / Product Final 3 处命名 live-model 材质 miss，20 个未解 slot 读数未收口 | OPEN | `world_feature_probe --tf-root <TF> --map <bsp>`、`resource_reachability_probe <TF> <mapStem>`；先向作者取原始包并记 SHA-256，再查 VPK/BSP pak | 路线图 P2 命名材质条目；`docs/MAP-MATERIAL-PROGRESS-2026-11-12.md` | Asset provider + Map renderer | 依赖外部资产；禁止用猜测 fallback 掩盖缺失 |
| D18 | P2（证据缺口） | `产品`：九类动画按行（idle/run/turn/crouch/jump/flinch/death/taunt/attack）与主/副/近战槽位矩阵未完成；attack 仅在有可靠事件映射处实现 | NOT VERIFIED | 逐类逐行比对输入字段/事件 → 选中 sequence → tick/cycle → pose 输出 → 可见像素；缺字段记 NOT VERIFIED | 路线图 P4 条目；H4 步 3-4；F3-2 | Character/weapon owner | 与 D04 同一 model/shader 窗口；与 D02/D11 争用 renderer 时串行 |
| D19 | P2（用户可见） | `产品`：HUD 字段语义（broadcaster vs POV 主体 vs STV 跟随者）、timer/round、stale/missing、Unicode、DPI 未验收；I-16 玩家名语义仍开 | OPEN | 真播放态下逐字段断言 + 改名/缺 userinfo/过期 string-table 用例；禁止把 broadcaster 显示为跟随者 | 路线图 P3 两条 HUD/玩家名条目；`GATES-UNREGISTERED.md` `userinfo-check.sh` | UI/playback owner | 依赖可用 POV/STV 语料（D01 的子集） |
| D20 | P2（证据缺口） | `证据`：transport（play/pause/seek/reverse/frame-step/speed/end/malformed index/seek-after-pause）全格式 sweep 与普通用户流程未完成 | NOT VERIFIED | `playback-control-check.sh` + `playback-error-paths-check.sh` + `timeline-check.sh` 分段跑；再走无测试开关的用户流程 | `GATES-UNREGISTERED.md` `playback-control-check.sh`；H3 步 3/5 | Playback/camera owner + UI | 依赖 D06；与 D21 共用 playback 状态文件 |
| D21 | P2（功能未实现） | `产品`：多视图（先两路 split，per-pane target/mode/free-look，pane focus 与输入路由）未实现；PIP/四视图为后续里程碑 | OPEN | 契约 → per-view 状态 solver → 共享快照 N 视图渲染 → 双人同 tick 正例与 pane 隔离断言 | 路线图 F3-1 / R8-1..R8-5；H5 多视图步骤 | Playback/camera + Renderer + UI（三方串行） | 依赖 D20 的相机状态契约；与 D01/D02/D11 争用 frame/world 文件 |
| D22 | P2（证据缺口） | `证据`：reload 在 30-60 tick 窗口观测密度不足，未先证"现有 playback 已能观测"就改产品（F2-6） | OPEN | 提高该窗口 checkpoint 密度，记录 sequence/tick/pose；先证现状，再决定是否改产品 | 路线图 F2-6；`PHASE-F-REPORT` §7.7 | Animation QA | 依赖 D18 的逐行动画夹具 |
| D23 | P2（工程） | `工程`：仓库无 CI workflow（无 `.github/`，本次只读确认），本地 `verify-fast.sh` 不是 CI | OPEN | 新增 Windows CI：clean configure/build + 脚本语法 + 小快层子集；并用一个故意失败负控证明会红 | 路线图 P6 CI 条目；Batch 5「no checked-in CI workflow」；H6 步 2 | Architecture/build owner | 无上游依赖，可与其他只读工作并行；CMake 落地需独占窗口 |
| D24 | P2（用户可见/门禁） | `产品`：重复 entity ID 拒绝、mutation anchor 失效检测的修复只存在于未发布任务分支，本树无此防护 | NOT VERIFIED | 依据 `KNOWN-ISSUES-AND-FIXES.md` 的 m12 变异：交替 body ID 后检查是否具名拒绝而非静默取首行 | `KNOWN-ISSUES-AND-FIXES.md` 前两节（明示"not part of this public default-branch snapshot"） | QA/parser owner | 修复需从任务分支挑选并重绑当前 HEAD；与 D06 同窗口 |
| D25 | P2（证据缺口） | `证据`：可听设备（device-6）smoke 未做；M/`[`/`]` 只证了字节与 ui.ini 持久化，不代表有声输出 | NOT VERIFIED | `bash quiet-capture-check.sh --device`（须人工在场，禁止无人值守） | `PHASE-F-REPORT` §4 F1-4；`GATES-UNREGISTERED.md` `quiet-capture-check.sh` / `fullscreen-quiet-check.sh` | Audio owner + QA（人工） | 需用户在席；阻塞 H3 的音频退出条件 |
| D26 | P2（证据缺口） | `证据`：I-23 仅一个具名玻璃材质（`glasswindow001a_649_-1016_704`，84,965 px）有像素证明，另外九个 rescue 材质仍只有结构/计数证据 | NOT VERIFIED | 复用相机 `--debug-camera 949 -1016 784 15 180`，对每个材质做 rescue on/off + hole-trace 归因 | `PHASE-F-REPORT` §4 F1-5；`sultry-translucent-atlas-check.sh` 第 4 节 | Map renderer + QA | 依赖 D06；与 D03 共用 Sultry 场景 |
| D27 | P3（工程卫生/证据） | `产品`：air-walk 全词匹配修复同样只在任务分支，本树选择器可能仍会匹配 `walk` inside `airwalk` | NOT VERIFIED | 用含 `airwalk`/`walk` 的序列名跑选择器，检查是否整词匹配并保留 leading-token 优先 | `KNOWN-ISSUES-AND-FIXES.md`「Air-walk substring matching」 | Animation QA | 与 D24 同一挑拣窗口；完整 T61.1 验收还需独立产品门禁 |
| D28 | P3（具名输入缺口） | `证据/外部`：`pak01_dir.vpk` 缺失被两个探针具名（4/5 archives），archive 覆盖主张受限 | OPEN | `resource_reachability_probe <TF> cp_sunshine`、`vpk_integrity_probe --real <TF> --real-limit 50` | `F2-2-PROBE-SWEEP` §2（两处均报同一缺失行） | Asset provider | 属环境/资产，不改代码；影响 D17 的"资产缺失"判定 |
| D29 | P3（工程卫生） | `工程`：`/W4` 未覆盖全部目标（本次只读统计：58 个 `add_executable`，9 处 `/W4`）；`/WX` 未启用 | OPEN | 建 `/W4` 目标矩阵，逐 target 提交并分类既有告警，再决定 `/WX` | 路线图 P6 `/W4` 条目；`native/cmake/tf2-libraries.cmake:111`、`tf2-package.cmake:88-97` | Architecture/build owner | 与 D23 同 CMake 窗口；禁止与产品修复混提交 |
| D30 | P3（工程卫生） | `工程`：`native/src/main.cpp` 仍 12,883 行；只完成一次 camera-script 抽取 | OPEN | 选下一个内聚 phase 搬移，保持行为不变，前后比对固定 demo/tick 相机、计数、trace、像素 | 本文只读统计；`PROJECT-HANDOFF` §5 H6 引言、H6 步 5 | Architecture/build owner | 依赖 D18/D21 的稳定行为基线；单写者窗口 |
| D31 | P3（门禁规则） | `门禁`：I-28 绝对/相对 OUT 路径影子路径只修了 quiet/audio-keys 两个门禁，全树契约未建立 | OPEN | 按 F1-6：抽一个 repo-wide 路径规范化断言，先在所有 gate 上只告警，再收紧 | `PHASE-F-REPORT` §6 I-28 行；路线图 F1-6 | Gate owner | gate-only，可与其他只读工作并行；改共享 `verify-*.sh` 需单写者 |
| D32 | P3（门禁规则） | `门禁`：I-30 规则（`renderedFrames` 等 wall-clock 计数不可作精确 pin）未落成通用检查 | OPEN | 检索门禁中的 wall-clock 字段精确断言，改为范围/不变量断言 | `PHASE-F-REPORT` §6 I-30 行；Batch 3 I-30 | Gate owner | 与 D31 同一 gate 窗口 |
| D33 | P3（文档） | `文档`：Phase F 报告正文称 18 笔提交，其提交表与 `git rev-list --count 9deed76..68a206a` 为 15 | OPEN | `git rev-list --count 9deed76..68a206a`；以表格 15 为准，除非作者指明遗漏 3 笔 | `PROJECT-HANDOFF` §2 末条；Batch 5 第二条 | QA/文档 owner | 无；只影响证据计数口径，勿改历史报告结论 |
| D34 | P3（产品限制） | `产品`：LOD1+ 不被当前 renderer 支持；首个可见需要它的模型尚未登记 | UNSUPPORTED | 在模型/地图矩阵中登记首个确实需要 LOD1+ 的可见用例，再单独立项 | 路线图「LOD1+ is not supported」；H4 步 3 | Character/weapon owner | 不得用 LOD-count 探针单独声称支持或缺陷 |
| D35 | P3（范围未决） | `范围`：T55 视频裁剪/导出既未完成也未排除，产品 owner 未决定是否进首发 | OPEN | 先做播放/导出设计估算，再由产品 owner 裁量；未决期间不得暗示已完成或已排除 | 路线图 scope limits 末条 | Product owner + Playback | 与 D20 播放路径相关；不阻塞其余项 |

## 已闭项与闭项依据（不列入上表）

I-31（门禁字段口径错配，`VERIFY-AUDIO` 235 断言 + 17/17 变异）、I-32（门禁注册表
`gates=84 registered=34 excluded=50` + 5 个自检控制）、I-24（`MATERIAL-COVERAGE=PASS` 100 断言）、
I-29 的 headless 部分（`QUIET-CAPTURE=PASS`，device-pair=SKIPPED）、F1-1..F1-4 已闭；
其中 F1-4 的**可听**部分仍是开项，已单列为 D25，F1-5/I-23 只闭了"一个材质像素"，余量列为 D26。
`KNOWN-ISSUES-AND-FIXES.md` 里的 T61.1 修复（重复 entity ID、mutation anchor、air-walk 全词匹配）
明确写明不在本默认分支快照内，故**不算已闭**，已列为 D24/D27。

## 统计

| 维度 | 数量 |
|---|---:|
| 总条数 | 35 |
| P0 | 0 |
| P1 | 7（D01-D07） |
| P2 | 19（D08-D26） |
| P3 | 9（D27-D35） |
| 产品缺陷 | 13（D02/D03/D04/D11/D13/D16/D17/D18/D19/D21/D24/D27/D34，其中 D03/D13 为"疑似"，D34 为已声明限制） |
| 门禁缺陷 | 6（D05/D09/D10/D12/D31/D32） |
| 证据缺陷 | 11（D01/D06/D07/D08/D14/D15/D20/D22/D25/D26/D28） |
| 工程/文档/范围 | 5（D23/D29/D30/D33/D35） |

结论口径：**产品缺陷 13 条 vs 门禁+证据缺陷 17 条**。台账里 17 条属于"产品可能已可用，但结论无法
成立"，不能与产品回归混为一谈；A12（D09）与 blocked 门禁（D12）是典型门禁缺陷，不是渲染缺陷。

## 下一步按依赖排序的前 5 条

1. **D06 身份重绑**（Integrator）：在 host lock 内以 HEAD `94e7612` 重建并重绑 exe/sourceContent/
   sourceTree，解释 sidecar 的 `6d203db8e729` 与文档 `f533293` 差异。它是其余 7 条身份敏感证据的前提。
2. **D09 A12 期望修正 + 重跑**（UI + gate owner）：只改 m2/m3 期望红因，三个变异都必须以**预期理由**变红；
   不改阈值、不放松断言。
3. **D10 `loop=false` triage**（Animation QA）：先证合成断言是否过期；`realLoop=true` 不得当作修复。
4. **D08 五图小地图帧**（UI）：在 D06 完成后补 Process/Gullywash/Sultry/Product/Bagel，并把既有两帧重绑身份。
5. **D03 849/919 归因**（Map renderer）：工作量最长，应与 D08 并行启动，但共用 host lock 时串行。

可并行且不占产品写入窗口：D23（CI 草案）、D31/D32（gate-only）、D17/D28（只读资产盘点）。

## 文档互相矛盾（本次只读发现）

1. **"clean" 与实际不符**：路线图基线段写 HEAD `94e7612` "clean"，但 `git status --porcelain` 是 1 个
   已修改 + 1 个未跟踪（且交接要求保留）。
2. **sourceTree 值不一致**：文档统一称 canonical `sourceTree=f533293 dirty=0`，磁盘 identity sidecar 写
   `sourceTree=6d203db8e729 dirty=0`；`exeSha256`/`exeBytes`/`sourceContent` 三方一致，只有 sourceTree 不同。
3. **blocked 理由已过期**：`GATES-UNREGISTERED.md` 仍称 `world-material-semantics-check.sh` 红到 F1-3
   重钉为止，而 F1-3/I-24 已 CLOSED 且 `material-coverage` PASS；该 blocked 归类需要重新判定（D12）。
4. **提交数 18 vs 15**：报告正文与提交表/`git rev-list --count` 不一致（文档已注明以 15 为准，但正文未改）。
5. **A12 运行日期**：报告标题为 2026-11-13，A12 变异实际在 2026-11-14 跑（Batch 5 已注明）。
6. **小地图状态**：路线图 P5 仍写"只存在 BSP 派生源数据"，与 F2-1"产品前向路径 PASS 96 断言"看似冲突；
   该矛盾已由 Batch 2 快照声明 + Batch 4/5 取代说明消解，不算未声明的矛盾。

## 本轮结账（2026-11-14，批次 G1 结束时追加）

| 条目 | 变化 | 依据 |
|---|---|---|
| D09 小地图 A12 期望 | **CLOSED** | `e3e0b7b`；`MINIMAP-HUD=PASS assertions=104`，m1/m2/m3 分别 red=3/5/4 且各命中预期断言；还原构建复现 exe `00ec8b99` |
| D10 探针 `loop=false` | **CLOSED**（夹具缺陷） | `68b2555`；`selfTest=true loop=true`，`realLoop/bones=76/anims=1012/motionSequence` 未变 |
| D06 exe 身份重绑 | **仍 OPEN，但已量化** | `git diff --stat 6d203db..HEAD -- native/src native/include native/CMakeLists.txt` 为空 ⇒ 该 exe 的产品源码内容与当前 HEAD 一致，只有内嵌 `sourceTree` 字符串过期。重建会改哈希并作废全部绑定 `00ec8b99` 的证据，需独立授权窗口 |
| 基线 HEAD | `94e7612` → `4ba137f`（3 笔：门禁、探针、文档），外加一笔只含文档的收尾提交 | 本轮提交 |
| D03 849/919 | **仍 OPEN**，机制已证、来源未闭合 | `docs/BLACKPOINT-849-919-2026-11-14.md` |

台账其余条目未变；本表不改变任何未列条目的状态。

## 第二批（G1-6，同日）：七图小地图帧

| 条目 | 变化 | 依据 |
|---|---|---|
| **D34 暂停丢面板** | 新增并 **CLOSED** | 缺陷：`--start-paused` 第 2 帧起 `reason=no-camera-pose`、`shapes=0`，抓帧与对照逐字节相同；修复 `a153030`（复用同图最后位姿）。七图 205 项检查 0 失败，`docs/D08-MINIMAP-SEVEN-MAPS-2026-11-14.md` |
| **D33 文档称 BSP 线稿、实际无线稿** | 新增 **OPEN**（文档/口径缺陷，非产品回归） | 面板只推 背景+边框+控制点环+观察者+航向点；形状计数 13/14/10/11 与之一致。`PHASE-F-REPORT:233`、`PROJECT-HANDOFF:334`、`PROJECT-ROADMAP:784` 的 "BSP-derived linework" 不成立 |
| **D35 小地图缩放比例** | 新增 **NOT VERIFIED** | 投影用的是 `minimap.h` 声明的可玩区矩形；未独立验证（BSP 世界包围盒本就不同：Upward 声明 ±3072/±4096 vs 世界 -3328..9344） |
| D08 七图帧 | **PARTIAL→大部分 CLOSED** | 七图面板位置/投影朝向/控制点落位 PASS；resize/DPI、线稿、载荷车图标仍 NOT VERIFIED/UNSUPPORTED |
| D06 身份 | **00ec8b99 由 cfcdbe5d 取代** | 产品修复后重建：exe `cfcdbe5d…`、2,988,544 B、`sourceTree=a15303062d5e`、`dirty=1`（脏文件仅两份文档）。绑定 00ec8b99 的小地图证据已全部作废并在新 exe 上重跑 |
| 七图 demo 配对 | **更正：Upward 缺的是 POV，不是 STV** | `a9630c…pl_upward_f12.dem` 头部 `client="SourceTV Demo"`（serveme.tf #1570108，85,204 ticks），已归档 `E:/demo测试/pl_upward_f12/STV/`；Upward POV 仍缺 |
| **D36 暂停缺陷的父提交负控** | 新增 **OPEN**（证据缺口） | 修复前后都有 trace 记录，但未在独立构建目录重建父提交 `94e7612` 的二进制做二次负控（与门禁写窗口冲突）。做一次即可关闭 |

## 第三批（G2，同日）：负控、resize、探针矩阵

| 条目 | 变化 | 依据 |
|---|---|---|
| **D37 `verify-iso.sh` 哈希读取（门禁在本机跑不起来）** | 新增并 **CLOSED** | `sha256sum "$file"` 在文件名含反斜杠时给整行加 `\` 前缀，而 `mktemp -d` 在本机给 Windows 路径；改了 `sha256sum < "$file"`。27 个门禁受影响，`world-black-account-check.sh` 由 `iso-seed` FAIL 变 PASS。对照：HEAD 版跑 `iso-trap-selftest` 同样 4 项失败（沙箱 safe-delete，与本改动无关） |
| **D36 暂停缺陷父提交负控** | **CLOSED** | 父提交 `94e7612` 在独立目录构建 `d8bbaa05…`，同命令复现 `reason=no-camera-pose` 且 on/off 逐字节相同（`df05b7e3…`）；修复后 `cfcdbe5d` 差 25600/0 |
| **D08 resize** | **CLOSED（两种尺寸）** | `--window-size 800x600`：面板 `612,12,160,160`、`inside=25600 outside=0`、39 项检查 0 失败。**DPI 仍 NOT VERIFIED**（无设置开关，需改显示设置） |
| **D03 849/919** | **仍 OPEN，但门禁可跑了** | `WORLD-BLACK-ACCOUNT=PASS`，919 读数 `predicted albedo=82,81,79` vs `texel/albedo frame=210,208,197`，量化了 trace 与 draw 的分叉；849 的 0 来源仍未闭合 |
| **D38 探针路径/编码缺口** | 新增 **OPEN**（工具缺口） | `world_geometry_probe` 要 TF 相对路径、`world_feature_probe` 要绝对路径，且后者无法打开非 ASCII 路径（`E:/demo测试/...` 空输出） |
| **D01 输入** | 新增七图探针矩阵 | 七图全部 rc=0、预算 `within`、`dropped=0`、bump `missing=0`。**不是**渲染验收 |
| **Upward POV** | **仍缺（用户提供的不是 f12）** | 新 demo 头部 `map="pl_upward"`（125,508 ticks，`708683e1…`），归档到 `E:/demo测试/pl_upward/POV/`；`pl_upward_f12` 的 POV 仍缺，不得替代 |
| **D39 世界帧里的未归因异常族** | 新增 **OPEN**（证据缺口） | 七图世界帧（tick 30000，STV 相机）显示：Gullywash/Bagel/Sultry 有成片高频白噪、Process/Gullywash/Product 有黑区、Sunshine 有疑似水面的浅蓝平面。均未做具名材质归因与 on/off 对照，不得称为缺陷。见 `docs/G2-2026-11-14.md` §7 |
| **D40 产品写回 `--settings-file`** | 新增 **OPEN**（流程/卫生） | 本轮世界帧脚本把 tracked 的 `gate-settings.ini` 直接当 `--settings-file`，产品退出时写回 126 键（原 10 键值未变）。已恢复并改为副本。建议：脚本层统一走 `iso_seed_gate_settings`，并在 `--settings-file` 指向 tracked 路径时由产品在 trace 里告警 |
