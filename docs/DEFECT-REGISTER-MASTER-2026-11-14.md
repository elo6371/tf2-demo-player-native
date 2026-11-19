# 缺陷台账（唯一主表）- 2026-11-14

**本文件是唯一台账。** `docs/DEFECT-REGISTER-2026-11-14.md` 转为历史附录（保留原文，不再更新），两者冲突时以本文件为准。

- 生成依据：旧登记册主表 35 行（D01–D35）+ 本批 G1/G2 追加 8 条（D36–D43）+ N02 追加 D44、N03 追加 D45、N04 追加 D46–D48、N25 追加 D49，合并去重后 **49 个唯一问题**。
- 编号规则：**D01–D35 保持旧登记册原义不变**；本批新增从 **D36** 起连续分配；与旧号冲突的三条已改号（见 §1）。
- `级别` 即风险等级（P0 崩溃/数据丢失/安全；P1 阻断发布或首图可见；P2 单图/单功能或门禁无法表述结论；P3 工程卫生或证据缺口）。

## 1 编号冲突与映射（N00 要求）

发现：本批 G1/G2 追加结果时，直接沿用了 **D33/D34/D35**，而旧登记册这三个号已分别代表「Phase F 报告 18 vs 15 笔提交」「LOD1+ 不支持」「T55 视频裁剪未决」。同一号同时「已关」与「未关」，必须分开。

| 旧编号 + 出现处 | 旧含义 | 新编号 | 新含义 | 状态 |
|---|---|---|---|---|
| D33（旧主表） | Phase F 报告称 18 笔提交，实际 15 | **D33** | 不变 | OPEN（N00 已复核，见 §5） |
| D34（旧主表） | LOD1+ 不被 renderer 支持 | **D34** | 不变 | UNSUPPORTED |
| D35（旧主表） | T55 视频裁剪/导出未决 | **D35** | 不变 | OPEN |
| D33（G1 第二批追加） | 文档称 "BSP-derived linework"，实际无线稿 | **D41** | 小地图面板无线稿 | OPEN（文档已更正，功能未实现） |
| D34（G1 第二批追加） | 暂停后小地图消失 | **D42** | 同前 | **CLOSED**（`a153030`） |
| D35（G1 第二批追加） | 小地图缩放比例未独立验证 | **D43** | 同前 | NOT VERIFIED |
| D36（G2 追加，两行） | 暂停缺陷缺父提交负控 | **D36** | 同前（同一问题，非重复） | **CLOSED** |

未冲突、保持原号：D36–D40。

## 2 唯一主表

| ID | 级别 | 问题 | 状态 | 负责人 | 依赖 | 最小测试 | 证据/位置 | N 卡 |
|---|---|---|---|---|---|---|---|---|
| D01 | P1（阻断发布） | `产品/证据`：七图 POV+STV 的几何/材质/天空/控制点/BSP-道具矩阵未在同一 exe 上完成，仅零散单图单相机证据（**Upward 的 f12 POV 已于 2026-11-14 补入** `E:/demo测试/pl_upward_f12/POV/autorecord_2026-08-08_21-43-59.dem`，client=Sinister Mark、306,131 ticks；七图 POV 至此齐全，矩阵仍未做） | NOT VERIFIED | Map renderer owner + QA | 依赖 D06（身份）；须先有 D01 的 BSP/demo 配对清单（H0 步 4） | 每图各一次固定相机抓帧：`tf2_demo_native.exe --tf-root <TF> --demo <dem> --audio-disabled --hidden-window --start-paused --capture-tick <N> --debug-camera <x y z pitch yaw> --capture-frame <bmp>`（host lock 内） | `PROJECT-HANDOFF-2026-11-14.md` §5 H2 Exit；`PHASE-F-REPORT-2026-11-13.md` §7.2 | N11 |
| D02 | P1（用户可见） | `产品`：3D skybox 渲染 pass（缩放/合成/第二遍绘制与深度清除）未实现（`sky_camera` 实体已解析于 `native/src/bsp_map.cpp:629-638` 写 `map.skyCamera`）；六面 cubemap 加载只是纹理读取，不算 3D sky | OPEN | Map renderer owner | 与 D01 同一 world/frame 写入窗口，单写者串行 | 需求卡 F2-5：基于已解析的 `sky_camera` 以命名相机 on/off 抓帧，加 `--no-sky3d` 回退对照 | 路线图 F2-5、Batch 3「E3 3D-skybox design 未落地」；`PHASE-F-REPORT` §7.7 | N10 |
| D03 | P1（用户可见） | `诊断`：Sultry 三个点（849/873/919）经 COLOR0 回读后收敛成**同一个诊断缺陷**——CPU 点选把像素归给光栅器没画/不是覆盖它的三角形；顶点色全为 255，849 的 CPU 链完全正常，873 的 texel 本身是 0,0,0。**在修 oracle 之前不得改渲染器**（`docs/N05-ATTRIBUTION-2026-11-15.md` §14）。原记：`产品`：Sultry 两个点已分叉归因（**2026-11-15**：849 的 face-id = 17804 与预测一致；`texel frame=84,83,81` 而 `albedo frame=0,0,0` → 归零在 texel 之后、albedo 之前（顶点色/乘子段）；**2026-11-15 追加：同材质 `METALGRATE013A2` 的另一张面 873,366 也黑（3,0,0）→ 材质级而非单面级；translucent rescue 路径已排除**；919 的 face-id = 16777215（sky），与预测面 6343 无关 → oracle/光栅分叉，非材质。见 `docs/N05-ATTRIBUTION-2026-11-15.md`）。原文：Sultry 黑区 849,396（vertex colour 0，texel 非黑）与 919,379（face-trace UV 与实际 pass 不一致）未归因 | OPEN | Map renderer + QA | 依赖 D06；须一次只改一个已证原因并保留同姿态对照 | `bash world-black-account-check.sh`；产品侧 `--world-face-trace --world-face-trace-points "849,396;919,379"` + `--world-texel`，相机 `-600 -600 950 20 45`、tick 0 | `PHASE-F-REPORT` §4 F1-5/§6；`world-black-account-check.sh` | N05 |
| D04 | P1（用户可见/稳定性） | `产品`：三处已知 weapon OOM/预算用例未按实测 prepared/uploaded/drawn 解决，优先级与溢出语义未定义 | OPEN | Character/weapon owner + Architecture | 与 D18 共享 model/renderer 写入窗口 | 需求卡 H4 步 5 / P4 卡：先取 prepared/uploaded/drawn、纹理内存、实例与 draw call 峰值，再定拒绝策略 | 路线图 P4「Resolve the three known weapon OOM cases」；`GATES-UNREGISTERED.md` `attach-weapon-budget-check.sh` | N19 |
| D05 | P1（阻断发布） | `门禁`：15 步 `verify-release.sh` 在冻结候选上从未跑过；所有绿色都是窄门禁 | NOT VERIFIED | Integrator / release lead | 依赖 D01-D04 处置与 D06；D05 是最后一步，不得当日常状态检查 | `bash verify-release.sh`（仅在冻结候选 + host lock 下，全项目只允许一次授权尝试） | `PROJECT-HANDOFF-2026-11-14.md` §2；路线图 Batch 5 末条 | N30 |
| D06 | P1（阻断发布） | `证据`：交付 exe 未在 HEAD `94e7612` 重建重绑；sidecar `sourceTree=6d203db8e729` 与文档所称 canonical `f533293` 不一致 | OPEN（已量化，见 §4） | Integrator | 阻塞全部身份敏感证据（D01/D03/D08/D14/D15/D20/D22）；无上游依赖，应最先做 | 重建后比对 `sha256sum native/build-nmake/tf2_demo_native.exe`、`cat native/build-nmake/tf2_demo_native.identity`、`bash build-identity-content.sh hash`（host lock 内） | 本文基线表；`PROJECT-HANDOFF` §2；`PROJECT-ROADMAP` 基线段 | N28 |
| D07 | P1（用户可见） | `证据`：干净受支持环境上的 install → open → play → seek → follow → audio → recovery 全流程未验收，仅在开发机跑过 | NOT VERIFIED | Integrator + Asset/install owner | 依赖 D06；打包与安装门禁为 segmented，需单独窗口 | 按 H7 步 5 在干净机走 `cmake --install` 产物路径，记录每步截图/日志与失败恢复 | `PROJECT-HANDOFF` §5 H7.5；`GATES-UNREGISTERED.md` `install-lifecycle-check.sh` / `install-readiness-check.sh` | N29 |
| D08 | P2（证据缺口） | `证据`：小地图七图中仅 Upward、Sunshine 有产品帧；Process/Gullywash/Sultry/Product/Bagel 五图无帧，且既有两帧未与当前身份重绑 | PARTIAL（拆分见 §3） | UI/HUD owner | 依赖 D06 与 D09（同门禁文件，避免同窗口改） | `bash minimap-hud-check.sh --old-exe=<pre-E9 exe>` 复跑前向 96 断言；五图各补固定相机 `--hud-minimap --capture-frame` | `PHASE-F-REPORT` §4 F2-1；`minimap-hud-check.sh` | N15 + N14 |
| D09 | P2（门禁不可表述） | `门禁`：小地图 A12 变异 m2-heading / m3-anchor 的**期望红因写反**，三个变异实际都被抓到，但 A12 判 FAIL（属门禁缺陷，非产品回归） | CLOSED（本批 `e3e0b7b`） | UI owner + gate owner | 无上游依赖，gate-only 可并行；解锁 D08 的完整验收 | `bash minimap-hud-check.sh --mutation --old-exe=<pre-E9 exe>`（A12 段会改源并重建，须 host lock） | `PHASE-F-REPORT` §4 F2-1（`MINIMAP-HUD=FAIL assertions=100 failed=2`）；Batch 4 F2-1 条 | 已闭（N00 记录） |
| D10 | P2（证据缺口） | `门禁/探针`：`animation_viewmodel_probe` 合成路径 `loop=false`，同 run `realLoop=true`、其余标志全 true；是唯一 OPEN 的探针自检 | CLOSED（本批 `68b2555`） | Animation QA | 无上游依赖，可立即做；先证断言是否过期，禁止为变绿而放宽 | `animation_viewmodel_probe.exe`（带 `TF_ROOT`）复跑，比对 `loopAndOnce()` 合成断言与真实模型读数 | `F2-2-PROBE-SWEEP` §3/§5；`PHASE-F-REPORT` §4 F2-2 | 已闭（N00 记录） |
| D11 | P2（用户可见未证） | `产品`：world brush pass 不带 per-material cull 标志，`$nocull` 仅用于 entity-model range；可见影响未证 | OPEN | Map renderer + QA | 与 D01/D02 同一 world 写入窗口；无正例前禁止改 renderer | 先造 Bagel world-brush 面带 `$nocull` 的正例，再 `--no-world-cull` on/off 同相机抓帧；现 oak 是 static prop，不够 | `R2-9-NOCULL-CENSUS` §"What this does and does not decide"；`PHASE-F-REPORT` §4 F2-3；Batch 4 F2-3 | N07 |
| D12 | P2（门禁不可表述） | `门禁`：`world-material-semantics-check.sh` 登记为 blocked（**2026-11-14 更正**：真实原因不是 I-24 pin drift，而是它钉的 `tf/demos/autorecord_2026-07-02_13-26-46.dem` 已不在磁盘上，capture A rc=2 重试 12 次全空），理由仍写"直到 F1-3 重钉"，但 F1-3/I-24 已 CLOSED 且 `material-coverage` 100 断言 PASS | OPEN | Map/QA owner + gate owner | 无上游代码依赖；需先解释 6 个读数重钉是否已覆盖该门禁的 pin | `bash world-material-semantics-check.sh`（blocked 类，非注册链；跑前须重新判定其 red 是否仍成立） | `GATES-UNREGISTERED.md` blocked 行；`PHASE-F-REPORT` §4 F1-3 | N25 |
| D13 | P2（用户可见未证） | `产品(疑似)`：world 半透明面在 opaque 之后分组但**未按面 back-to-front 排序**；可见排序错误未量化（R2-10 / F2-4） | NOT VERIFIED | Map renderer owner | 依赖 D06；无像素 oracle 前禁止写通用排序补丁 | Sultry 玻璃相机（`--debug-camera 949 -1016 784 15 180`）固定 tick 抓帧，与源排序/相机深度/atlas-PVS 约束比对 | 路线图「Candidate-fix review」末条、F2-4；`PHASE-F-REPORT` §7.7 | N07 |
| D14 | P2（证据缺口） | `证据`：控制点 neutral/capturing/owned、队伍 skin/bodygroup/proxy/texture2/modulation/alpha 未逐态验收 | NOT VERIFIED | Map renderer owner | 依赖 D01 的每图固定相机与 D06 | 按 P2 顺序：VMT 字段盘点 → parser 语义 → proxy 时间输入 → bodygroup/skin → 三态各抓一帧 → 与 Source 客户帧对比 | 路线图 P2 控制点条目；H2 步 5 | N08 |
| D15 | P2（证据缺口） | `证据`：Upward 拖车仅 tick 5611/6160 初始证据，未覆盖直线/弯道、静/动、pause/seek/speed 与 PVS | NOT VERIFIED | Map renderer owner + QA | 依赖 D06；与 D14 同图窗口 | 固定相机在 5611/6160 之外补直/弯道 tick，记录 model/skin/body/parent/origin | 路线图 P2 Upward 条目；H2 步 6 | N09 |
| D16 | P2（用户可见未证） | `产品`：七图移动 BSP 实体（门/平台/列车/可破坏物）未盘点，解码变换/插值/绘制/像素位移未分层断言 | NOT VERIFIED | Map renderer owner | 依赖 D01；实现与 D11 串行 | `brush_entity_probe`（真实参数）+ 一张图一个移动 brush 夹具与一个 static brush 对照 | 路线图 P2「Inventory moving BSP entities」；H2 步 6 | N09 |
| D17 | P2（用户可见） | `产品(资产)`：Sultry 13 / Bagel RC11 4 / Product Final 3 处命名 live-model 材质 miss，20 个未解 slot 读数未收口 | OPEN | Asset provider + Map renderer | 依赖外部资产；禁止用猜测 fallback 掩盖缺失 | `world_feature_probe --tf-root <TF> --map <bsp>`、`resource_reachability_probe <TF> <mapStem>`；先向作者取原始包并记 SHA-256，再查 VPK/BSP pak | 路线图 P2 命名材质条目；`docs/MAP-MATERIAL-PROGRESS-2026-11-12.md` | N07 |
| D18 | P2（证据缺口） | `产品`：九类动画按行（idle/run/turn/crouch/jump/flinch/death/taunt/attack）与主/副/近战槽位矩阵未完成；attack 仅在有可靠事件映射处实现 | NOT VERIFIED | Character/weapon owner | 与 D04 同一 model/shader 窗口；与 D02/D11 争用 renderer 时串行 | 逐类逐行比对输入字段/事件 → 选中 sequence → tick/cycle → pose 输出 → 可见像素；缺字段记 NOT VERIFIED | 路线图 P4 条目；H4 步 3-4；F3-2 | N17 |
| D19 | P2（用户可见） | `产品`：HUD 字段语义（broadcaster vs POV 主体 vs STV 跟随者）、timer/round、stale/missing、Unicode、DPI 未验收；I-16 玩家名语义仍开 | OPEN | UI/playback owner | 依赖可用 POV/STV 语料（D01 的子集） | 真播放态下逐字段断言 + 改名/缺 userinfo/过期 string-table 用例；禁止把 broadcaster 显示为跟随者 | 路线图 P3 两条 HUD/玩家名条目；`GATES-UNREGISTERED.md` `userinfo-check.sh` | N13 |
| D20 | P2（证据缺口） | `证据`：transport（play/pause/seek/reverse/frame-step/speed/end/malformed index/seek-after-pause）全格式 sweep 与普通用户流程未完成 | NOT VERIFIED | Playback/camera owner + UI | 依赖 D06；与 D21 共用 playback 状态文件 | `playback-control-check.sh` + `playback-error-paths-check.sh` + `timeline-check.sh` 分段跑；再走无测试开关的用户流程 | `GATES-UNREGISTERED.md` `playback-control-check.sh`；H3 步 3/5 | N12 |
| D21 | P2（功能未实现） | `产品`：多视图（先两路 split，per-pane target/mode/free-look，pane focus 与输入路由）未实现；PIP/四视图为后续里程碑 | OPEN | Playback/camera + Renderer + UI（三方串行） | 依赖 D20 的相机状态契约；与 D01/D02/D11 争用 frame/world 文件 | 契约 → per-view 状态 solver → 共享快照 N 视图渲染 → 双人同 tick 正例与 pane 隔离断言 | 路线图 F3-1 / R8-1..R8-5；H5 多视图步骤 | N16 |
| D22 | P2（证据缺口） | `证据`：reload 在 30-60 tick 窗口观测密度不足，未先证"现有 playback 已能观测"就改产品（F2-6） | OPEN | Animation QA | 依赖 D18 的逐行动画夹具 | 提高该窗口 checkpoint 密度，记录 sequence/tick/pose；先证现状，再决定是否改产品 | 路线图 F2-6；`PHASE-F-REPORT` §7.7 | N17 |
| D23 | P2（工程） | `工程`：仓库已有 `.github/workflows/tier1.yml`（`work/integrate`）但零运行（触发不含 `work/*`，原记无 `.github/`），本地 `verify-fast.sh` 不是 CI | OPEN | Architecture/build owner | 无上游依赖，可与其他只读工作并行；CMake 落地需独占窗口 | 在远端触发首次运行（推 `candidate/*` 或 dispatch）并用故意失败负控证明会红 | 路线图 P6 CI 条目；Batch 5「no checked-in CI workflow」；H6 步 2 | N22 |
| D24 | P2（用户可见/门禁） | `产品`：重复 entity ID 拒绝、mutation anchor 失效检测的修复只存在于未发布任务分支，本树无此防护 | NOT VERIFIED | QA/parser owner | 修复需从任务分支挑选并重绑当前 HEAD；与 D06 同窗口 | 依据 `KNOWN-ISSUES-AND-FIXES.md` 的 m12 变异：交替 body ID 后检查是否具名拒绝而非静默取首行 | `KNOWN-ISSUES-AND-FIXES.md` 前两节（明示"not part of this public default-branch snapshot"） | N23 |
| D25 | P2（证据缺口） | `证据`：可听设备（device-6）smoke 未做；M/`[`/`]` 只证了字节与 ui.ini 持久化，不代表有声输出 | NOT VERIFIED | Audio owner + QA（人工） | 需用户在席；阻塞 H3 的音频退出条件 | `bash quiet-capture-check.sh --device`（须人工在场，禁止无人值守） | `PHASE-F-REPORT` §4 F1-4；`GATES-UNREGISTERED.md` `quiet-capture-check.sh` / `fullscreen-quiet-check.sh` | N21 |
| D26 | P2（证据缺口） | `证据`：I-23 仅一个具名玻璃材质（`glasswindow001a_649_-1016_704`，84,965 px）有像素证明，另外九个 rescue 材质仍只有结构/计数证据 | NOT VERIFIED | Map renderer + QA | 依赖 D06；与 D03 共用 Sultry 场景 | 复用相机 `--debug-camera 949 -1016 784 15 180`，对每个材质做 rescue on/off + hole-trace 归因 | `PHASE-F-REPORT` §4 F1-5；`sultry-translucent-atlas-check.sh` 第 4 节 | N07 |
| D27 | P3（工程卫生/证据） | `产品`：air-walk 全词匹配修复同样只在任务分支，本树选择器可能仍会匹配 `walk` inside `airwalk` | NOT VERIFIED | Animation QA | 与 D24 同一挑拣窗口；完整 T61.1 验收还需独立产品门禁 | 用含 `airwalk`/`walk` 的序列名跑选择器，检查是否整词匹配并保留 leading-token 优先 | `KNOWN-ISSUES-AND-FIXES.md`「Air-walk substring matching」 | N23 |
| D28 | P3（具名输入缺口） | `证据/外部`：`pak01_dir.vpk` 缺失被两个探针具名（4/5 archives），archive 覆盖主张受限 | OPEN | Asset provider | 属环境/资产，不改代码；影响 D17 的"资产缺失"判定 | `resource_reachability_probe <TF> cp_sunshine`、`vpk_integrity_probe --real <TF> --real-limit 50` | `F2-2-PROBE-SWEEP` §2（两处均报同一缺失行） | N01 |
| D29 | P3（工程卫生） | `工程`：`/W4` 与 `/WX`（**2026-11-19 更新：全树已达 55/55 目标 `/W4`、139 TU 零告警、7 个目标 `/WX`**；2026-11-14 旧复核为 54 目标中 6 个 `/W4`、`/WX` 未启用，见 `docs/N23-W4-COVERAGE-2026-11-14.md`） | OPEN | Architecture/build owner | 与 D23 同 CMake 窗口；禁止与产品修复混提交 | 建 `/W4` 目标矩阵，逐 target 提交并分类既有警告，再决定 `/WX` | 路线图 P6 `/W4` 条目；`native/cmake/tf2-libraries.cmake:111`、`tf2-package.cmake:88-97` | N23 |
| D30 | P3（工程卫生） | `工程`：`native/src/main.cpp` 现为 16,221 行（`wWinMain` `:2971–16221`）；已拆出 `main_selftest.cpp` 等 7 个周边 TU（原记 12,883 行、仅 camera-script 抽取） | OPEN | Architecture/build owner | 依赖 D18/D21 的稳定行为基线；单写者窗口 | 选下一个内聚 phase 搬移，保持行为不变，前后比对固定 demo/tick 相机、计数、trace、像素 | 本文只读统计；`PROJECT-HANDOFF` §5 H6 引言、H6 步 5 | N24 |
| D31 | P3（门禁规则） | `门禁`：I-28 绝对/相对 OUT 路径影子路径只修了 quiet/audio-keys 两个门禁，全树契约未建立 | OPEN | Gate owner | gate-only，可与其他只读工作并行；改共享 `verify-*.sh` 需单写者 | 按 F1-6：抽一个 repo-wide 路径规范化断言，先在所有 gate 上只告警，再收紧 | `PHASE-F-REPORT` §6 I-28 行；路线图 F1-6 | N25 |
| D32 | P3（门禁规则） | `门禁`：I-30 规则（`renderedFrames` 等 wall-clock 计数不可作精确 pin）未落成通用检查（**2026-11-14 审计：14 个门禁提到帧计数，4 个有等值断言形态，逐条核对后违规实例 0**；政策缺口保留，见 `docs/G4-N04-2026-11-14.md` §10） | OPEN | Gate owner | 与 D31 同一 gate 窗口 | 检索门禁中的 wall-clock 字段精确断言，改为范围/不变量断言 | `PHASE-F-REPORT` §6 I-30 行；Batch 3 I-30 | N25 |
| D33 | P3（文档） | `文档`：Phase F 报告正文称 18 笔提交，其提交表与 `git rev-list --count 9deed76..68a206a` 为 15 | OPEN | QA/文档 owner | 无；只影响证据计数口径，勿改历史报告结论 | `git rev-list --count 9deed76..68a206a`；以表格 15 为准，除非作者指明遗漏 3 笔 | `PROJECT-HANDOFF` §2 末条；Batch 5 第二条 | N00 |
| D34 | P3（产品限制） | `产品`：LOD1+ 不被当前 renderer 支持；首个可见需要它的模型尚未登记 | UNSUPPORTED | Character/weapon owner | 不得用 LOD-count 探针单独声称支持或缺陷 | 在模型/地图矩阵中登记首个确实需要 LOD1+ 的可见用例，再单独立项 | 路线图「LOD1+ is not supported」；H4 步 3 | N27 |
| D35 | P3（范围已裁决/未实现） | `范围`：T55/N27 视频导出已裁决首发含 MF 方案 C + 离线音频（`PROJECT-NEXT-TASKS` N27、`native/tools/mf_export_probe.cpp`），产品实现未落地（原记范围未决） | OPEN | Product owner + Playback | 与 D20 播放路径相关；不阻塞其余项 | 按 N27 契约实现产品导出并补缺组件 `exit 13` 与 30s 音画同步验证 | 路线图 scope limits 末条；`PROJECT-NEXT-TASKS` N27 | N27 |
| D36 | P2（证据缺口） | `证据`：暂停缺陷的父提交二进制负控 | CLOSED | Playback owner | — | 父提交 `94e7612` 独立目录构建 `d8bbaa05…`，同命令复现 `reason=no-camera-pose` 且 on/off 逐字节同 `df05b7e3…` | `D:/_shared/EXEC-2026-11-14/G2/parent-paused/` | 已闭（N00 记录） |
| D37 | P2（门禁） | `门禁`：`verify-iso.sh` 用 `sha256sum "$file"`，文件名含反斜杠时整行加 `\` 前缀，27 个门禁在本机 `iso-seed` 失败 | CLOSED | Gate owner | — | `bash world-black-account-check.sh`：修前 `FAIL reason=iso-seed`，修后 `PASS` | `docs/G2-2026-11-14.md` §5 | 已闭（N03） |
| D38 | P2（工具缺口） | `探针`：`world_geometry_probe` 要 TF 相对路径、`world_feature_probe` 要绝对路径，且后者无法打开非 ASCII 路径（实测为 0xC0000409 崩溃） | CLOSED（N02） | Probe owner | — | 同一 BSP 用相对/绝对/中文路径各跑一次，比较 rc 与读数 | `docs/G2-2026-11-14.md` §3 | N02 |
| D39 | P2（证据缺口） | `产品(未归因)`：七图世界帧出现三族异常——高频白噪（Gullywash/Bagel/Sultry）、黑区（Process/Gullywash/Product）、Sunshine 疑似水面浅蓝平面 | OPEN | Map renderer owner | D03/N05 的归因方法 | 每族一个具名材质/几何的最小例 + 同机位 on/off | `docs/G2-2026-11-14.md` §7 | N06 |
| D40 | P3（流程/卫生） | `流程`：产品退出时写回 `--settings-file`，脚本把 tracked fixture 直接传入会改脏仓库 | CLOSED（N03，`tools/lockrun.sh` 守卫） | Gate owner | — | 跑一次产品后 `git status --porcelain native/tests/fixtures` 必须为空 | `docs/G2-2026-11-14.md` §8 | N03 |
| D41 | P2（文档→功能） | `产品`：小地图面板没有地图线稿/楼层，只有背景+边框+控制点环+观察者+航向点；三处文档曾称 "BSP-derived linework" | OPEN | UI owner | D43 | 面板形状计数 13/14/10/11 与代码推送列表一致；无任何几何 primitive | `docs/D08-MINIMAP-SEVEN-MAPS-2026-11-14.md` §3 | N14 |
| D42 | P1（用户可见） | `产品`：`--start-paused` 后第 2 帧起小地图消失（`reason=no-camera-pose`），抓帧与对照逐字节相同 | CLOSED | Playback owner | — | 同图同相机 on/off 两帧：修前相同，修后差 25600 像素全在面板内 | `docs/G2-2026-11-14.md` §1 | 已闭（N15 回归） |
| D43 | P2（证据缺口） | `证据`：小地图投影用的是 `minimap.h` 声明的可玩区矩形，未用独立来源验证缩放比例 | NOT VERIFIED | UI owner | — | 用 BSP 中两个已知世界点与面板像素反算 scale，与声明矩形比较 | `docs/D08-MINIMAP-SEVEN-MAPS-2026-11-14.md` §4 | N14 |
| D44 | P2（工具缺口） | `探针`：仍有 30 处 `std::filesystem::u8path(argv[...])` 分布在 16 个探针，遇到非 UTF-8 参数会走同一条未捕获异常路径（0xC0000409） | OPEN | Probe owner | D38 的修法 | `grep -rn "u8path(argv" native/tools/*.cpp`；每个探针用非 ASCII 路径跑一次，rc 必须非零且带具名原因，不能是 0xC0000409 | `D:/_shared/EXEC-2026-11-14/G3/n02-matrix.txt` | N02 后续 |
| D45 | P3（数据丢失） | `产品`：设置保存会丢弃未知键——`--settings-file` 里加 `zzz_future_key=42`，跑一次产品后该键消失（已知键 `preset`/`viewModelFov` 保留） | OPEN | Settings owner | — | 复制 fixture 加一个未知键跑一次产品，检查该键是否仍在；再检查降级场景（新版本写的键被旧版本读过后是否丢失） | `D:/_shared/EXEC-2026-11-14/G3/rt.ini`（跑前/跑后） | N03 后续 |
| D46 | P2（门禁/探针） | `探针`：`demo_open_probe` 对"打不开/解析失败"也返回 0，只看退出码的门禁会把缺失文件读成成功 | CLOSED（N04） | Probe owner | — | valid=0 / truncated=1 / badmagic=2 / missing=2，输出行与修前逐字相同 | `docs/G4-N04-2026-11-14.md` §2 | N04 |
| D47 | P2（用户可见） | `产品`：启动期参数拒绝（`--audio-device`/`--capture-tick`/`--window-size`/`--settings-file`/`--ui-settings-file`/`--animation-drive`）只写 `OutputDebugStringW`，rc=13 但 stdout+stderr 为空、trace 无原因 | OPEN | Playback owner | D46 的契约口径 | 六条命令各跑一次，要求 stderr 有一行具名原因且 trace 有对应行 | `docs/G4-N04-2026-11-14.md` §3 | N04 后续 |
| D48 | P3（证据缺口） | `门禁`：`--renderer-fault hud.createbuffer:0` 注入确实触发（trace 有 refuse 行），但 `--renderer-resize-selftest` 仍 `RESULT=PASS refused=1`，没有断言画面缺了什么 | OPEN | Renderer owner | — | 故障注入运行时额外断言受影响区域的像素/形状，否则该用例只证明"没崩" | `docs/G4-N04-2026-11-14.md` §4 | N23 |
| D49 | P2（证据完整性） | `门禁`：`world-material-semantics-check.sh` 在判定失败**之前**就重写了 `evidence/material-semantics/` 下 11 个已提交文件，一次红跑把仓库里签入的读数改成空值 | OPEN（守卫已加） | Gate owner | D40 的守卫 | 红跑后 `git status --porcelain evidence/` 必须为空；门禁应写临时目录，判定后再落盘 | `docs/G4-N04-2026-11-14.md` §11 | N25 |

## 3 D08 拆分（N00 要求：已通过的子集不重做）

| 子项 | 状态 | 证据 / 说明 |
|---|---|---|
| D08a 七图固定相机帧 + 面板位置/投影朝向/控制点落位 | **CLOSED** | `MINIMAP-FIVE checks=205 failed=0`；每图 `inside=25600 outside=0`；`docs/D08-MINIMAP-SEVEN-MAPS-2026-11-14.md` §1 |
| D08b resize（1280×720 ↔ 800×600） | **CLOSED** | 面板 `612,12,160,160`，39 项检查 0 失败；`docs/G2-2026-11-14.md` §2 |
| D08c DPI 缩放 | NOT VERIFIED | 产品无设置 DPI 的入口；改显示设置不属无人值守窗口 |
| D08d 缩放比例/标尺 | NOT VERIFIED → **D43** | 用声明矩形投影，未用独立来源 |
| D08e 底图/线稿/楼层 | UNSUPPORTED → **D41** | 面板只推 背景+边框+环+观察者+航向点 |
| D08f click/zoom | UNSUPPORTED | 产品无小地图交互 |
| D08g 暂停/seek 下连续状态 | 暂停已闭（**D42**），seek 未测 | `a153030` + 正负控；seek 归 N15 |

## 4 D06 现状（N00 要求：区分"内容对得上"和"净树发布身份"）

| 项 | 值 | 判定 |
|---|---|---|
| exe 内容 vs sidecar | `cfcdbe5d…` 与 sidecar `exeSha256` 一致 | **对得上** |
| 产品源码 vs 证据 | `git diff --stat a153030..HEAD -- native/src native/include native/CMakeLists.txt` 为空 | 产品源码自 `a153030` 未变 |
| 工作区脏改动 | `dirtyFiles=2`：`docs/PROJECT-ROADMAP-HANDOFF-2026-11-12.md`（用户改动）+ 未跟踪的 `PROJECT-HANDOFF-2026-11-14.md` | 只影响文档，不影响 exe |
| 旧证据 | 地图渲染/音频/动画证据仍绑 `00ec8b99` | **未重绑** |
| 发布身份 | 净树 + 冻结 + 全链一次通过 | **未达成**（N28/N29/N30） |

口径：docs-only 提交**不重建产品**，只用 `bash build-target.sh tf2_identity` 刷新 sidecar 到当前 HEAD；只有产品源码变化才重建并作废绑定旧 exe 的证据。

## 5 统计（唯一问题数）

| 状态 | 数量 | 编号 |
|---|---:|---|
| OPEN | 25 | D02 D03 D04 D06 D11 D12 D17 D19 D21 D22 D23 D28 D29 D30 D31 D32 D33 D35 D39 D41 D44 D45 D47 D48 D49 |
| NOT VERIFIED | 14 | D01 D05 D07 D13 D14 D15 D16 D18 D20 D24 D25 D26 D27 D43 |
| CLOSED | 8 | D09 D10 D36 D37 D38 D40 D42 D46 |
| PARTIAL | 1 | D08（见 §3） |
| UNSUPPORTED | 1 | D34 |
| **合计** | **49** | D01–D49，无重号、无"同号既关又未关" |

D33（18 vs 15 笔提交）复核：`git rev-list --count 9deed76..68a206a` = **15**，与旧登记册判断一致；报告正文的 18 应视为笔误，历史报告不改写。

## 6 N 卡挂接

每个 N 卡对应的问题见主表最后一列。汇总：

| N 卡 | 关联问题 |
|---|---|
| N00 | D33（编号与台账本身） |
| N01 | D01、D28 |
| N02 | D38 |
| N03 | D37（已闭）、D40 |
| N04 | D04、D24、D27（按 P0/P1 复核结论再挂） |
| N05 | D03 |
| N06 | D39 |
| N07 | D11、D13、D17、D26 |
| N08 | D14 |
| N09 | D15、D16 |
| N10 | D02 |
| N11 | D01（最终矩阵由 N05–N10 填充） |
| N12 | D20 |
| N13 | D19 |
| N14 | D41、D43、D08d、D08e |
| N15 | D08c、D08f、D08g、D42（回归） |
| N16 | D21 |
| N17 | D18、D22 |
| N18 | D04（与 N19 共享容量口径） |
| N19 | D04 |
| N20 | D11（特效与 cull/管线口径） |
| N21 | D25 |
| N22 | D23 |
| N23 | D24、D27、D29 |
| N24 | D30 |
| N25 | D12、D31、D32 |
| N26 | D28、D29（构建/缓存口径） |
| N27 | D34、D35 |
| N28 | D06 |
| N29 | D07 |
| N30 | D05 |

说明：N04/N18/N20/N26 的挂接来自 N 文本的关键词，未在 N 文本中具名；若 owner 认为挂错，改这一列即可，主表 ID 不变。
