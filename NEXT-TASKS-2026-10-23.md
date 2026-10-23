# 当前任务清单 — 2026-10-23

## 0. 状态口径与负责人

- 主线快照：p0-entity-protocol，HEAD `ecb3a93`，与 `origin/p0-entity-protocol` 一致。提交历史已经包含 T50–T54 的本地 merge；“已合并”只表示进入集成树，不代表功能验收完成。
- T49 完整链报告记录 24/24 通过。T49.1 于 11:52 得到 53 断言、0 失败和 mutation PASS，但可见 control 阶段占用了用户桌面，违反窗口隔离前提，故为 NOT VERIFIED。当前选择先保留此状态，不投入 2–3 小时尝试隔离桌面；隔离桌面方案单独排期。
- `evidence/hud/**` 的 52 个工作树差异来自 11:56–12:00 的 `hud-check.sh`（20 个内容差异、其余为行尾差异），不是用户编辑；已恢复这些已跟踪文件到 HEAD。未跟踪 `evidence/release-gates/`、`evidence/trajectory/demo.txt`、`evidence/trajectory/trace-cli.txt` 和 `native/src/main.cpp` 改动仍保留，不暂存、不覆盖。
- 截至 12:15 的同树结果：T50 18 断言 PASS，T51 PASS，T52 12 FAIL 已分为 A/B/C，T54 34 断言 PASS 但仍需证明脚本未执行时会失败，T53 正在验证 `parseFeatureArgument` 搬迁的 CLI 路径。后续状态必须注明实际 HEAD、exe 和 trace 身份。
- 用户负责功能分支整合；本批现有 merge 已发生，不再创建新的合并动作。当前 T52 无锁诊断和 T53 gate 由用户继续，助手不启动竞争运行；助手负责审查与定因，并在所有任务完成、用户交付冻结树后执行最终冻结核验和唯一一次完整链。
- 本清单是当前任务入口。T50–T55 的原始产品要求仍见 NEXT-TASKS-2026-10-21.md；本清单补充当前状态、收尾步骤和地图/材质/第三人称人物任务。

## 1. 全局执行规范

### AI 必须遵守

1. 开始每项任务先核对分支、HEAD、工作树、远端、共享锁、产品/探针/编译进程；把结果写入任务报告。只在独立 worktree 改代码。
2. 先读取实际调用点、类型和现有 gate，再设计修改；不凭任务名猜接口、字段或资源格式。先找仓库已有探针和门禁，能复用就不另建一套。
3. 先做便宜的静态检查和一次目标构建，再跑一个代表性输入。只有发现真实缺陷后才扩展样本；同一源码/配置只构建一次。
4. 所有产品启动、构建共享输出、抓帧、变异、安装 staging 和音频设备测试持共享锁单实例串行。测试窗口不得显示在用户当前窗口之上；渲染命令必须带 --hidden-window --audio-disabled。T49.1 的可见控制阶段只能在已验证的隔离桌面/会话执行；不能隔离就停止并记 NOT VERIFIED。
5. 在每个像素结论中固定并记录地图、demo、tick、相机/位姿、设置、源码 commit、exe 哈希和 trace 身份。tick 请求值不等于实际帧身份；缺相机行、缺实体或缺产物时不得继续判像素。
6. 断言必须先证明被测主体存在。零断言、未启动、缺字段、空文件、跳过步骤、变异未应用、变异构建失败均为 NOT RUN/INVALID，不是 PASS，也不算变异捕获。
7. 每个产品修复要有同一输入的修复前后证据、具名正向读数和至少一个能使对应验收变红的变异。变异后逐字节恢复源码、重建恢复版本并确认产物；不能只看计数器证明画面正确。
8. 不放宽阈值、删掉失败变异、改成宽松 pin 或把未测项目改写成“通过”。遇到首个无法解释的红先停下归因；相同环境错误连续两次后停止该环境路径，提交两次原始错误和人工所需事项。
9. 不运行完整链作为调试工具。每项任务只跑受影响的 focused gate；最终冻结树由用户交付后，助手只执行一次 verify-all.sh --quick。若完整链后改动触及已测模块，重跑对应 focused gates；影响多个模块或链断言时重新冻结再跑整链。
10. 证据只保留支持结论的帧、短日志、报告和哈希清单。大型原始 trace 按仓库既有规则忽略并记录 SHA-256；不提交 exe、临时 build、重复帧、空日志或备份。删除任何既有或归属不明的证据前必须停下，不得把清理当效率优化。

### 提效方法

- 先按当前提交和证据清单找出已在同一源码树上完成的步骤，复用有效结果；只有身份字段不匹配或读数冲突时才补跑。
- 静态代码审查、资源盘点、报告草拟可并行；占用共享构建树、GPU、窗口、安装目录或音频设备的工作必须串行。
- 先测最大可见问题和最短可达输入，不在多个地图/职业上同时盲目抓帧。首个代表样本能区分机制后，再扩到矩阵。
- 每任务固定顺序：身份核对 → 静态定位 → 单目标构建 → 一次基线 → 一次修复/对照 → forward → mutation → byte-restore → 受影响回归 → 报告。避免反复全量跑同一门禁。
- 开始前确认 demo tick 与目标实体确实可达。运行前检查锁和进程，运行后确认锁释放、源码恢复、日志非空、frame/trace 对齐。
- 等锁队列必须记录 owner、预计耗时、开始/结束和下一项；不要因为队列空就把运行中的任务视为完成。

### 每个交付报告必填

任务目标与非目标、owner、基线/最终 commit、worktree、改动文件及原因、运行命令和退出码、实际 assertion/readout、输入和二进制哈希、证据路径、所有 FAIL/INVALID/NOT VERIFIED、变异捕获与还原证明、构建/进程/锁的结束状态、建议下一步。报告措辞必须与日志一致。

## 2. 当前状态矩阵

| 任务 | 当前状态 | 下一步 |
|---|---|---|
| T49 完整链 | 报告记录 24/24 PASS | 保留为当前基线；不要因局部问题重跑整链 |
| T49.1 全屏非干扰 | 11:52 实测 53/0 + mutation PASS；活动桌面正控不满足验收前提，NOT VERIFIED | 本波不启动隔离桌面研发；保留限制，后续单独排期 |
| T50 相机书签 | 集成树 PASS，18 断言 | 复用 evidence/release-gates/camera-ui.log；不重复运行，除非身份对账失败 |
| T51 HUD 导入 | 集成树 PASS；门禁写脏的 52 个 tracked HUD 产物已还原 | 复用 `evidence/release-gates/hud.log`；除非身份对账失败，不重跑 |
| T52 队伍色 X-ray | 集成树 12 FAIL；A tick 越界、B 绝对 ordinal 假设、C 被 A 影响的 D1 采样 | 用户已批准在 `task/t52-outline` 修 A/B；先证明重排 pass 会被相对顺序断言捕获；修后预期只留 D1 `outsideDiff=297` |
| T53 投射物轨迹 | 截至 12:15 正在验证 `parseFeatureArgument` 搬迁的 CLI 路径；当前结果未知 | 等现有 gate 结束，记录同次 HEAD/source/exe/demo/trace；不启动第二个 gate |
| T54 logs.tf | 同树 34 断言 PASS，`runtime_available=1`；scrollOffset=480、height=4200、viewport=720 | 对账旧失败的 commit/exe/trace；增加或证明脚本未执行时的具名失败控制，不能依赖 `0 == 0` |
| T55 时间轴/剪辑/导出 | 按原计划延后 | 在相机和 review UI 验收后按 T55 步骤实施 |
| T56–T61 | 地图与第三人称人物审计/修复计划 | T56/T57 静态审计可并行，修复按文件所有权串行 |
| T62 | 现有范围视觉矩阵 | 做当前范围 focused 验收准备；不跑最终完整链 |
| T63–T65 | SourceTV HUD 数据、事件、小地图审计；下一波可并行任务 | 先冻结共享 demo 样本清单，再并行静态审计；共享 demo 运行和抓帧串行 |
| T66–T69 | SourceTV 观战镜头、比赛面板、事件导航、小地图选人 | 审计接口冻结后分配；实现并行须先确认文件所有权不重叠 |
| T70 | 最终验收 | 全部功能由用户整合并冻结后，再运行一次完整链 |

## 3. T49.1 — 全屏非干扰门禁关闭

**目标：**可信证明自动测试不会抢占或覆盖用户当前操作窗口。
**非目标：**不能以只运行隐藏窗口替代 foreground/control 阶段，也不能为了让测试通过而改变产品窗口行为。
**状态/决定：**11:52 运行虽有 53/0 和 mutation PASS，但可见 control 覆盖了活动桌面，故 NOT VERIFIED。本轮不投入 2–3 小时构建隔离桌面；该工作另立任务。不得用活动桌面重跑来补验收。
**前置：**未来重新排期时，先停止所有其他运行；确认隔离桌面不会把可见 control 窗口显示在用户活动窗口之上。

**步骤**

1. 对账 evidence/t49/T49-FULLSCREEN-REPORT.md、fullscreen-quiet-check.sh 的源码版本与最近三轮日志；给每轮标记源码 hash、exe hash、运行时长、窗口创建时间和是否执行到断言。
2. 静态检查守卫是否在产品窗口创建时持有前台、观测区间是否覆盖窗口创建前后、baseline/forward/control 产物是否都存在且非空。
3. 确认隔离桌面/会话的正控能被守卫识别。无法证明隔离或正控不可达时立即停止，不在用户当前桌面试跑。
4. 在隔离桌面和共享锁内运行 bash fullscreen-quiet-check.sh；核对 FULLSCREEN-QUIET=PASS、断言数大于零、startup/window 时间区间覆盖。再运行 bash fullscreen-quiet-check.sh --mutation。
5. 每条 mutation 都必须先证明补丁生效、源文件确实变化、恢复字节一致；无效变异单列 INVALID，不算捕获。
6. 如果正控或窗口交接失败，只修测试器缺陷并增加可本地重复的 self-test；不要改产品实现来迁就一个无效守卫。

**验收：**forward 与 mutation 均 rc=0；正控证明 guard 的检测灵敏度；用户活动窗口未被覆盖；守卫观测覆盖窗口创建/显示时间；源码恢复逐字节一致。
**停止：**测试会显示在当前用户窗口之上、守卫未真正成为前台、产物缺失、断言为零、连续两次相同环境失败。
**证据：**evidence/t49/，包括三轮历史和修复后正反向日志、窗口事件时间线、hashes。
**并行：**只允许静态审阅与其他任务的静态工作并行；运行必须独占桌面和共享锁。

## 4. T50–T55 — 已有功能收尾

### T50 — 相机书签集成树复核

**前置：**本地集成树已含 T50 merge；不得再次合并分支。
1. 核对 camera-ui-check.sh 的有效证据是否对应当前集成 HEAD；旧证据身份相同则复用，不同才重跑。
2. 必要时运行 bash camera-ui-check.sh 和 bash camera-ui-check.sh --mutation，确认 pause 时 tick 不动而相机可动、resume 后 tick 前进、至少三个书签可精确恢复、快捷键冲突拒绝、焦点在文本框时不误触发。
3. 确认 mutation 是被具名断言捕获，而非编译失败；还原后恢复帧和源码哈希匹配 baseline。
4. 用户完成整合记录；本助手只 review diff 与证据。

**完成：**集成树正向/变异通过或旧证据身份完全吻合；没有把单次相机帧误称为完整书签验收。

### T51 — 自定义 HUD 复核与运行产物恢复

**状态：**集成树 gate PASS。11:56–12:00 的 `hud-check.sh` 改写了 52 个 tracked evidence 文件；mtime、运行窗口和内容/行尾差异已对上。因为这些不是用户编辑且原文件都在 HEAD，已恢复至 HEAD。未跟踪 `evidence/release-gates/` 保留。
1. 复用 `evidence/release-gates/hud.log`，核对其 HEAD、exe 和报告身份；身份匹配时不重跑。
2. 若证据缺失或身份不匹配，先把门禁输出改到独立 run-id 目录，再运行 `bash hud-check.sh`；正向全绿后才运行 `bash hud-check.sh --mutation`，避免覆盖已跟踪 fixture/evidence。
3. 核对有效 HUD、缺失/畸形/越界输入的具名拒绝、预览/apply/reset、分辨率缩放、reset/uninstall 后用户文件逐字节不变，以及 HUD 开关不改变区域外世界像素。
4. 变异 source 必须逐字节恢复，恢复构建的 probe/frame 应复现 baseline。不得暂存未跟踪的 `evidence/release-gates/`，除非后续交付明确需要并逐项审查。

**完成：**所有 gate 断言有非零实测，变异全由断言捕获，用户文件清单前后完全相同。任何被删改的用户文件立即停止并报告，不自行恢复。

### T52 — 队伍色 X-ray 描边红项关闭

**前置：**集成树 12 条红已由同次诊断拆为三类；后续代码改动只在用户维护的 `task/t52-outline` 分支进行。
1. A（探针 tick）：默认请求 1000–2000 落在 demo 保留历史窗口 64229–64419 之前，因此真实录像段 rows=0；分支上同样 rows=0，属于过期默认值，不是 merge 缺陷。用窗口内 tick 64329 验证 rows=120，并记录 `serverTickOrigin=10252` 与 `74481-10252=64229` 的换算。
2. B（pass 顺序）：三处 ordinal 整体 +1 是 T53 pass 插入后的序列变化。代码契约 `overlayPassOrderValid()` 是相对谓词（两者非零且 overlay < viewModel），不是固定 ordinal=6。更新断言为相对契约；必须给出一个把 overlay 移到 ViewModel 之后的负控，并指出该负控会触发哪条具名断言。不得把期望值 6 改成 7。
3. C（区域外像素）：6141 由 A 的无效 tick 让 D1 采到更近玩家帧造成（4 个实心区块、silhouettes=4），不是新 pass。A 改用可达 tick 后回到 D1 原读数 `outsideDiff=297`；不得提高阈值或隐藏该残余红。
4. 在 `task/t52-outline` 完成 A/B 后运行一次 forward 与 mutation，验证 A 有非零 rows、B 的 pass 重排负控被相对顺序断言捕获；每条 mutation 需证明已应用且逐字节恢复。
5. 将唯一残余 D1 红单独交付：把 `entityIndex` 写入 GPU instance padding 并读回核对绘制归属。先静态确认 padding 生命周期/布局和 shader ABI，再做最小构建及单个代表输入；不得和 A/B 混在同一诊断结论里。

**完成：**A/B 的 forward 与负控闭环；D1 有独立 owner 和像素归属证据，或明确保持 NOT VERIFIED。整体验收不能因旧分支 1 FAIL 覆盖集成树实测。
**停止：**tick 仍无可达快照、pass 重排负控不转红、帧外差异归属未明、红蓝玩家主体不在帧中，或输入身份不一致。

### T53 — 投射物轨迹 gate 与性能证据收尾

**前置：**截至 12:15，集成树正在对 `parseFeatureArgument` 搬迁做 CLI 验收；`--trajectory-*` 属于搬迁参数集合。`native/src/main.cpp` 尚有 7 行 `traceFmt("title: …")` 未提交改动，`evidence/trajectory/demo.txt` 与 `evidence/trajectory/trace-cli.txt` 也有未提交产物；已知产品 exe `3076E5B1…` 构建早于这些改动。不要把旧 exe 的结果说成新源码结果。
1. 等现有 gate 结束，记录最终 exit、stdout、HEAD、工作树 diff、exe hash、demo hash 和 trace；不得并行启动第二个 gate 或构建。
2. 静态核对参数搬迁的调用链与原有解析行为；用一次 focused CLI 输入验证 `--trajectory-*` 可达且未知/缺值参数仍被拒绝。区分 parse-feature 变更与标题 trace 改动的身份。
3. gate 身份与源码一致时复用结果；否则先由用户决定如何处理未提交源码/证据，再做最小 focused 验证。不得用完整链验证 CLI 变更。
4. 单独核验 trajectory gate 的 13 项规则、真实 projectile 输入、owner/slot lifetime、全体与单玩家过滤、消失与约 0.5s linger、无投射物输入。
5. 报告 append median/p90/max 及最大值对应 packet、build median/p95/max 与 240 Hz 的 4.1667 ms 预算占比、query 比较上界和 slot reuse 对筛选的影响；中位数不得掩盖最大值。
6. 核对 overlay 开关和采样间隔在真实产品设置中的可操作性；若 gate PASS 但 UI 控件未接线，拆成明确未完成项。仅在 forward 有效且全绿后运行 mutation；每个变异须有具名读数，未应用/未构建记 INVALID。

**完成：**实际 gate PASS、mutation PASS/恢复完整、性能样本数非零且所有关键百分位可追溯到原始数据；旧“缺三个读数”的状态被证据明确关闭或仍保留。

### T54 — logs.tf 最新运行状态对账

**状态：**集成树 forward 日志 `evidence/release-gates/logstf.log` 报告 34 断言 PASS、`runtime_available=1`、`state=content`、host alive；scroll 读数为 offset=480、clamped=0、height=4200、viewport=720。该摘要未给出 mutation 结果；旧失败摘要的 commit/exe/trace 身份也仍待对账。
1. 按同一运行记录 HEAD、exe hash、WebView2 runtime、URL、browser state 和实际脚本执行确认；不得只凭非零或零数值推断 JS 成功。
2. 证明脚本未执行时哪条具名断言会红。若当前 gate 没有脚本执行 ack，补一条明确 ack/执行状态断言；另用 JS 未执行或滚动调用未发送的负控，确认失败发生在该断言，而不是 `scrollOffset` 的数值比较。
3. 对有效内容页确认 `scrollHeight > viewportHeight`、请求的 scroll 非零、offset 实际变化且 `scrollClamped` 符合预期；页面脚本错误和 history back/forward 另有独立读数。
4. 对账旧失败与本次 PASS 的 commit/exe/trace；只在身份匹配后解释为同一路径的状态变化。若需重跑，输出写入独立 run-id，保留现有日志。
5. 核验 empty state、合法/非法 host、resize、scroll、history、reload、close/reopen 和播放响应；无 runtime 时标 NOT VERIFIED，不用 fake host 代替。

**完成：**最新 same-tree forward 与 mutation 通过，scroll/history 各有独立可读读数，旧报告与新运行差异得到解释。
**停止：**WebView 脚本没有执行、日志为空、没有成功内容页面、测试窗口会覆盖用户当前窗口。

### T55 — 时间轴、剪辑和视频导出（延后）

**前置：**T50 相机工作流、T54 review UI 已验收；不得在前置条件未满足时开始编码器集成。
1. 先定义 timeline 的唯一时间基准（demo tick 与显示时间的映射）、seek 完成读数和 seek 失败状态；用已有 demo 验证往返 seek 的 tick/pose 可复现。
2. 实现时间轴、拖拽 in/out、数值输入和可访问键盘操作；测试边界、反向区间、单 tick 区间和 seek 中断。
3. 分开实现导出队列：选定区间、输出路径、帧率/分辨率、音频同步、进度、取消和失败保留/清理规则。先盘点系统与仓库现有编码能力，再决定 MP4/H.264 或明确标记的兼容回退；未经测量不新增重型依赖。
4. 用短片段做可重复导出；验证起止帧、音频同步、容器可被至少两个可用解码器读取、取消后文件状态明确，再扩大时长。
5. 为 seek 和导出各建 focused gate 与故障注入；导出必须有非空、可解码产物，取消和错误不能报告成功。

**状态：**Deferred；实现阶段与 T56–T61 分开排期。全链安排在本任务及当前视觉任务都关闭并冻结之后。

## 5. T56 — 多地图画面与材质审计

**目标：**回答地图/材质整体哪些已正常、哪些只是局部修复、哪些仍明显错误。
**非目标：**本任务先审计和定责，不以盲目调色或翻 winding 快速改变画面。
**前置：**保存当前集成 HEAD 和证据清单；用户提供/安装中确有可运行的 TF2 参考资源与 demo。

**步骤**

1. 静态盘点地图、demo、BSP、VPK/VTF/VMT 输入；选 cp_snakewater_final1、pl_odyssey，再从现有资源中选一张几何/材质分布不同且 demo 可达的地图。若第三张不存在，记录资源缺口，不伪造输入。
2. 每张图先选一个代表性 tick 和相机。验证该 tick 范围、实际 scene camera、world draw 与目标地图身份；优先用已证明稳定的 pause/freeze 机制。不能只凭请求的 capture tick 认定同帧。
3. 对同一地图/视角获取播放器帧和 TF2 参考帧；记录画面设置、分辨率、FOV、相机位置/朝向、HUD 状态。没有可比 TF2 参考时，把绝对“像 TF2”结论记 NOT VERIFIED，只做内部诊断。
4. 每张图先运行最相关的既有 gate，不要全套门禁一次性重复：bash visual-baseline-check.sh、bash world-material-check.sh、bash material-coverage-check.sh、bash world-material-face-check.sh、bash world-geometry-hole-check.sh、bash lighting-semantics-check.sh。选择依据写进报告；跨图路径若不受既有 gate 覆盖，明确该 gate 的地图局限。
5. 建立逐项问题账本：像素区域、face id、material 名、VMT/VTF 来源、UV 尺寸、atlas tile、lightmap 页/坐标、可见性/剔除状态、产品实际拒绝原因、与 reference 的差值。每类问题至少给一张放大图和一份相同帧 trace。
6. 以像素影响和可复现性排优先级，分成产品缺陷、地图内容本身、尚不支持的材质语义、证据不足四类；不得把最后两类写成修复完成。
7. 记录最少证据集并复跑分析脚本，确认 hashes 和账本统计稳定。审计若新增判定脚本，给“缺 trace/错地图/错 pose”负控，证明它会拒收不合格样本。

**验收：**三张可用地图各有同身份画面、参考来源说明、逐面/材质归因账本与哈希；每个大面积异常都有 owner 和下一步。无参考图、实体未出现、帧来源不明均不得记 PASS。
**Focused commands：**按选定地图执行上列既有 gate；具体组合需在运行前锁定，不能测试后挑更好看的帧。
**并行：**资源静态盘点可与 T57 静态盘点并行；全部构建/渲染/参考捕获共用一把锁串行。
**证据：**evidence/t56-map-audit/，保留少量原始 BMP、裁剪图、trace hash、map/demo/pose manifest、账本和报告。

## 6. T57 — 第三人称人物模型与动作审计

**目标：**独立检查 demo 中的玩家实体身体、职业模型、材质、骨骼和动作；ViewModel/手臂通过不算玩家身体通过。
**非目标：**不把单个模型探针通过写成九职业完整支持；不以 HUD/轮廓效果替代人物网格验收。

**步骤**

1. 从现有代码找玩家实体分类、职业/模型索引来源、模型资源解析、VVD/VTX/MDL 装载、材质槽、sequence/cycle 和骨骼矩阵的真实入口；先确认当前产品是否有第三人称实体 draw path。
2. 扫描已安装资源和现有 demo：逐职业列出可用 player 模型、LOD、材质、骨骼数、动画集与缺失 companion；同时列出 demo 实际出现的实体、team/class/model state 和可达 tick。
3. 选至少一个有可见第三人称玩家身体的 demo；找静止、移动、攻击/动作、死亡等实际可达样本。每种缺席情况按“输入没有该状态”记录，不能造数据充数。
4. 捕获本产品和 TF2 参考的同类 pose；读回 entity id、模型 key、sequence、cycle、root transform、骨骼矩阵摘要、bounds、draw ranges 和材质 key，并把这些读数与实际像素对应。
5. 用现有模型探针先查静态装载；viewmodel-robustness-check.sh 只覆盖其声明范围，禁止将它当第三人称验收。如果当前无第三人称诊断路径，增加最小 player-model-probe 与 player-model-check gate，并固定名字、身份及失败理由。
6. 产出问题分类：没有实现 draw path、实体到模型选择错、网格/LOD 缺失、材质槽错、骨骼绑定/姿态错、动作时间推进错、相机/近裁剪问题或输入本身不含目标状态。

**验收：**九职业静态资源清单完成；demo 中可达职业均有实体到像素链路或具名不支持原因；动作样本只对真实可达序列给结论；每个“乱成一团”样例可定位到具体链路阶段。
**Focused commands：**优先运行已有 model/viewmodel 静态 probe；如需产品渲染，按任务新增 gate 的 --help 与仓库调用点确认命令后记录，不猜参数。
**并行：**静态资源与代码盘点可同 T56 并行；所有构建、扫描启动、抓帧与变异串行。
**证据：**evidence/t57-player-audit/，包括 class/model/animation inventory、同 pose 参考图、entity-to-pixel trace 和未验证表。

## 7. T58 — 地图几何与可见性缺陷修复

**前置：**T56 至少已确认一个可复现的产品缺陷并提供可定位 face/像素；如果问题属于 BSP 内容本身，T58 只补具名诊断，不改产品几何。

**步骤**

1. 选 T56 中影响最大的一个缺陷，写明单一假设与可反证观测；同时保存 baseline source/exe/frame/trace hashes。
2. 先用 face-id/depth/vis 证据分离缺失几何、背面剔除、PVS、近远裁剪、z-fighting 和 sky 覆盖；不要把黑像素自动归为缺几何。
3. 用同一地图、demo、pose 和设置复现；证明某个产品数据流与错误像素对应，再修改最小代码范围。
4. 构建一次，采修复帧并与 baseline 对比；核对非目标区域、天空、武器/人物等隔离控制没有额外变化。
5. 加一条能回退该行为并让 named assertion 变红的 mutation；门禁本身先自测缺位姿、错 face 或 stale capture 会拒收。
6. mutation 完成后逐字节恢复、重建并重拍；确认恢复帧复现 baseline，再运行受影响的 T25/T26/world geometry focused gates。

**验收：**根因能由独立观测推翻或支持；目标像素改善且控制区域无非预期变化；forward 0 FAIL，mutation 有效捕获并 byte-identical restore。
**并行：**不得与 T59 并行改 renderer/culling；静态 review 可并行。

## 8. T59 — 多地图材质与光照修复

**前置：**T56 已给出跨图账本；T58 若改过共享 renderer，先冻结并交接其源文件身份。

**步骤**

1. 只选一个大面积、可见、能在参考帧上确认的材质问题作为第一修复；记录支持该材质的真实 VMT/VTF 与 VPK/loose 来源。
2. 分别测 VMT 声明、资源解析、实际 blit/upload、mesh range 绑定、shader 消费和最终像素。上游声明数或“纹理已绑定”不能单独证明视觉生效。
3. 先运行单特性/单材质对照：albedo、UV、blend、lightmap、detail/bump/specular 一次只改一项；不把没有消费者的 feature 标 applied。
4. 改最小产品代码并保留未支持材质的具名拒绝。对于不同 map 用到的同一路径，再加第二张地图作回归；没有第二输入时如实限定范围。
5. 跑对应 forward gate 和 mutation gate：bash world-material-face-check.sh、bash world-material-semantics-check.sh、bash material-coverage-check.sh、bash lighting-semantics-check.sh。只运行受改动影响的组合并说明选择理由。
6. mutation 后确认读数/像素向预期方向改变、恢复源码逐字节一致、恢复构建回到 baseline；对新 pin 记录同树 frame、素材身份、population 和阈值依据。

**验收：**至少一项真实可见材质缺陷完成端到端修复；第二张地图回归通过或明确不可测原因；未实现材质语义继续具名拒绝；无像素改善仅靠计数器报绿。

## 9. T60 — 第三人称人物模型正确显示

**前置：**T57 已证实实体/资源输入并给出至少一个真实可达职业样本。若没有第三人称渲染路径，本任务先实现最小可见模型路径，再扩展覆盖；不能跳到动画微调。

**步骤**

1. 选 T57 中有资源和可见实体的一个职业作为竖切样本，明确实体模型 index/key → MDL/VVD/VTX → LOD mesh/ranges → material slots → world transform → draw call → mask。
2. 先做现有代码静态 review 和最小 target build，加入必要的 trace/probe 字段；不先实现九职业特例。
3. 实现或修复通用的玩家模型解析与绘制；加载失败、缺 companion、无 model index 必须有可见的具名 fallback，不静默渲染错模型。
4. 对样本拍同 pose 产品/参考图，量 vertex/index/range/material key/bounds 和可见像素；再增第二职业验证没有硬编码首例。
5. 建立 player-model-check forward + mutation：mutation 至少攻击 model-key 选择、material-slot/range 绑定或 bounds 中实际修复的一项；确保缺实体控制不会空集报绿。
6. 恢复 mutation 后同帧同 pose 与 forward 对照；再运行现有 model robustness gate，不用 ViewModel gate 替代 third-person gate。

**验收：**样本玩家身体完整出现、部位与材质归属正确、bounds 覆盖实际 draw、第二职业无回归；forward/mutation 与源码恢复证据齐全。

## 10. T61 — 第三人称动作与骨骼连续性

**前置：**T60 模型静态显示稳定；T57 已定位真实动画资源和可达 sequence。必须与 T60 分开，避免模型选择错误被误诊为骨骼错误。

**步骤**

1. 固定同一实体，收集多个连续 tick 的 sequence、cycle、playback rate、活动状态、动画 source、骨骼矩阵摘要和实际截图。
2. 对照 TF2 相同实体/动作的参考帧；先验证 demo 输入确实在改变 sequence/cycle，再判断产品 pose。
3. 从 sequence lookup、companion binding、cycle 更新、blend/interpolation、parent/bone transform 的调用链向下定位；每次只改一类行为。
4. 增加连续帧检查：cycle 单调/按状态转变，关节变换有限且有界，root 与 mesh bounds 一致；装备/手持物附件另记，不把 ViewModel 接触当人物动作结论。
5. 跑 player-animation-check forward 和针对根因的 mutation；mutation 要让姿态/矩阵断言变红，不能只让编译失败或删除 trace 行。
6. 字节级恢复并重建；同输入重复输出 pose digest/截图一致，再跑 T60 player-model gate 和相关 animation availability focused gate。

**验收：**至少覆盖真实存在的静止与移动动作，另覆盖 demo 中确实存在的攻击/死亡等状态；无模型飞散、错 sequence、明显跳 pose；缺失数据明确标注，不伪称九职业所有动作已通过。

## 11. T62 — 现有范围视觉矩阵与验收准备

**前置：**T49.1–T61 的 blocker 已关闭或由用户明确接受为限制；T55 若按原范围实施，则必须在本任务之前完成。所有功能分支由用户整合并冻结，禁止验收过程中继续 merge/edit。

**步骤**

1. 记录当前候选 HEAD；确认干净 worktree、用户证据处置状态、远端目标、host lock FREE、无产品/探针/编译进程。
2. 在冻结树先运行 T50–T61 直接受影响的 focused gates，一次性记录实际 assertion 与证据身份；只补缺口，不重跑已证实且身份匹配的无关 gate。
3. 跑地图矩阵（T56 选定地图 × 已修材料/几何）和人物矩阵（T57 可达职业 × T61 可达动作）；逐格记 PASS / FAIL / NOT VERIFIED / UNSUPPORTED 与原因。
4. 核验 T55 短片段导出的容器可解码、时间边界和音画同步；若 T55 未完成，不得声称全软件完成，也不得提前跑最终整链。
5. 确认所有 FAIL/INVALID/NOT VERIFIED 都有 owner 和处置，不把 release gate 全绿等同“画面正确”；本任务不出最终 release verdict。
6. 最终 `bash verify-all.sh --quick` 移至 T70，在 SourceTV HUD 任务完成、由用户整合并冻结后恰好运行一次。

**验收：**当前范围矩阵单元有证据或显式未验证理由；focused 与 full-chain verdict 分开报告；不得把本任务称为全软件最终验收。

## 12. 顺序与并行表

1. 先等待当前 T53 gate 结束；不启动竞争运行。用户在 `task/t52-outline` 完成已批准的 A/B 门禁修正并交付 diff 后，助手只做审查与定因；T54 补脚本未执行负控。T49.1 保留 NOT VERIFIED，不阻塞这三项的静态工作。
2. T50/T51 只读审查并复用身份匹配证据；HUD 产物已恢复到 HEAD，不再把它们归属为用户改动。运行时所有共享资源仍串行排队。
3. T56 与 T57 的静态源码/资源盘点可以并行；所有构建、启动、参考图抓取和 mutation 串行。
4. T58 必须等 T56 几何归因；T59 必须基于 T56 材质账本。两项会碰共享 renderer，默认串行。
5. T60 必须等 T57；T61 必须等 T60 模型显示稳定。模型绘制与动画缺陷分开改，除非先证明不可分并记录理由。
6. T55 只有在 T50/T54 review UI 通过后才实现。纯 UI/数据设计可与 T56/T57 静态盘点并行；产品改动先检查 main/settings/render 文件所有权。
7. T62 只做现有范围的 focused 矩阵；SourceTV 审计 T63–T65 可与 T56/T57 的静态盘点并行。
8. T66 依赖 T63 的死亡/玩家状态数据结论；T67 依赖 T63 的比分、血量、弹药、Uber 与目标数据结论；T68 依赖 T64；T69 依赖 T65。UI 实现只有在审计冻结接口且文件所有权不重叠后才允许并行。
9. T70 在 T49.1–T69 的 blocker 关闭或由用户明确接受、所有分支由用户整合并冻结后开始。最终 full chain 与任何编辑、merge、构建、抓帧或 mutation 均不可并行。

## 13. 每个任务交给审查时

- 给出分支/HEAD/base 与干净状态；标明工作树里原先已存在的用户文件。
- 给出 diff 文件列表和每处改动对应的验收点；超出任务范围的变化单独解释，不顺手清理。
- 先报 FAIL、INVALID、NOT VERIFIED，再给 PASS；贴出实际 gate 最终摘要和退出码。
- 给出参考/产品帧和 trace 的身份 hash，截图必须来自报告所指的同一构建和输入。
- 展示 mutation changed → assertion red → source restored byte-identical → restored build baseline 的闭环。
- 说明哪些结论不能从当前证据推出，并提出最便宜的下一次验证。
- 交付后由用户整合；整合树上由用户安排受影响的回归。本助手收到分支或 diff 后做代码审查和问题定位，不代替用户合并。

## 14. T63–T70 — SourceTV 观战 HUD 波次

**范围：**优先支持 SourceTV 等全局录制 demo；POV 延后。布娃娃不在本波范围内。玩家死亡时模型立即消失；自动接力到下一名可观战玩家时摄像机约 0.3 秒飞行过渡，暂停播放时过渡仍继续；手动切换玩家立即切换；无可用目标时保持当前镜头。目标选择优先采用 demo 明确记录的 SourceTV 导演/观战目标，否则使用可复现的存活玩家顺序。

### T63 — SourceTV HUD 数据覆盖审计

**目标：**实测比赛 HUD 所需字段在真实 SourceTV demo 中是否存在、多久更新一次、哪些字段只能对部分玩家读取。
**步骤：**

1. 从本机可用资源中选至少一张 KOTH 与一张 Payload 地图的 SourceTV demo；记录路径、地图、demo hash、tick 范围和录制类型。缺少任一类型时如实记录，不用 POV 或合成数据冒充。
2. 选有玩家存活、死亡、医疗兵持有医疗枪等真实状态的 tick，逐项检查玩家名/队伍/职业/存活/血量、当前弹药、Uber、队名/比分/比赛时间、目标进度和观战目标字段。
3. 每项记录来源属性、样本数、更新时间间隔、缺失比例、解析置信度，并区分“可直接读取”“可从有效数据推导”“缺失/不支持”；缺失值不得默认为 0。
4. 检查死亡时玩家模型消失所需的状态是否可靠；记录目标被移除、重生和 demo seek 后的身份变化，不实现布娃娃。
5. 仅提交审计报告或本任务专属探针；若需要运行、抓帧或构建，取得共享锁后串行执行，不抢占/覆盖用户活动窗口。

**验收：**KOTH/Payload 各有身份明确的真实样本；HUD 每个字段有覆盖率与缺失策略；不支持的数据明确显示为 unavailable，不伪造数值。

### T64 — SourceTV 事件与时间轴数据审计

**目标：**确定事件导航可稳定提供哪些事件和准确 tick。
**步骤：**

1. 复用 T63 的 demo 身份，分别盘点击杀/死亡、回合开始结束、控制点变化、推车进度/检查点和导演镜头变化的数据来源。
2. 对每个事件记录原始消息/实体属性、demo tick、显示时间换算、关联玩家身份和同 tick 可取到的画面；不能只依据 HUD 文本或默认值。
3. 在至少一段包含事件的区间验证事件排序、重复/缺失处理、seek 到目标 tick 后画面身份和事件一致；记录 tick 偏移和边界误差。
4. 输出事件清单、字段覆盖率和可点击导航所需的最小数据契约；没有真实样本的事件标为 NOT VERIFIED。

**验收：**每类事件有可追溯来源与 tick 精度；失败/缺失能被具名报告；不把 T55 剪辑导出混入本任务。

### T65 — SourceTV 小地图数据与投影审计

**目标：**验证 BSP 世界坐标到简洁俯视地图以及玩家点位的映射。
**步骤：**

1. 对 T63 的 KOTH 与 Payload 地图检查 BSP 可用性、world bounds、地图方向、可玩区域和多层结构；保存地图身份与输入 hash。
2. 验证玩家实体位置在 SourceTV 快照中的覆盖率、更新间隔、死亡/重生时点和 entity handle 复用；建立世界坐标到小地图像素的可逆投影误差账本。
3. 产出不遮挡画面的简洁俯视预览，标出地图边界、代表性已知坐标与两队样本点；多个高度层的歧义要显式标出。
4. 定义默认折叠、展开、悬停名字和点击选择玩家的交互验收点；点击属于手动切换，必须立即切镜头。

**验收：**至少两种目标类型各有可验证投影；坐标误差和缺失点有上限/原因；不得把不可用地图绘成看似正确的空白小地图。

### T66 — SourceTV 死亡接力镜头

**前置：**T63 已确认死亡、存活和可观战目标数据；先冻结目标选择规则与镜头输入接口。
**步骤：**

1. SourceTV 专用地识别当前目标死亡；其玩家模型在死亡 tick 按规则立即消失，不创建 ragdoll。
2. 先采用 demo 明确的导演/观战目标；没有时按稳定、可复现的存活玩家顺序选择下一人，过滤无效、死亡或身份已复用的实体。
3. 自动接力时对镜头位置和朝向做约 0.3 秒连续过渡；过渡时钟独立于 demo 播放时钟，播放暂停也继续。手动选人直接设置目标，不播放过渡。
4. 没有可用目标时保持最后有效镜头；不得跳到世界原点、默认 orbit 或随机玩家。seek、倒放/变速能力（若存在）要有明确状态处理，不可重放旧过渡。
5. 覆盖目标死亡、同 tick 多人死亡、目标重生/复用、暂停、seek、无存活玩家和手动打断；使用同输入重复验证结果稳定。

**验收：**death/auto-transition/manual-switch/no-target 四种行为分别有 trace 与帧证据；暂停时动画继续，手动切换无动画；mutation 能抓住瞬切、暂停冻结、死亡模型残留和无目标跳位。

### T67 — 比赛 HUD 面板

**前置：**T63 的数据契约已冻结；与导入自定义 HUD 的组件开关兼容。
**步骤：**

1. 顶部呈现比赛时间、红蓝队名与比分；左右呈现两队玩家列表，显示有证据的数据字段和当前观战目标高亮。点击玩家行属于手动切换，立即切镜头。
2. 玩家行显示血量、弹药和医疗兵 Uber；只有 T63 证明可读的字段才显示数值，缺失时用明确的 unavailable 状态，不显示成 0。
3. 底部按地图目标只显示对应进度：KOTH 控制点或 Payload 推车；其他地图显示其已支持目标或具名未支持状态。
4. 保持中央画面无遮挡；缩小窗口时提供紧凑/可折叠队伍面板；加载和数据更新不能让控件跳动。
5. 为内置面板、导入 HUD 的重叠组件提供单独开关，并验证布局状态可恢复；队伍面板、事件栏和小地图的显示/折叠行为保持一致。
6. 键盘焦点顺序可预测；方向键在玩家/事件列表内移动，Enter 执行当前选择，Escape 关闭浮层或折叠小地图。可配置快捷键须检测冲突；文本输入、搜索框和设置控件有焦点时不得触发播放器全局快捷键。

**验收：**KOTH 与 Payload 各一帧，全部可见字段逐项对账；窄窗/全屏布局不遮核心画面；未提供的字段不误导。

### T68 — 事件导航栏

**前置：**T64 已交付可定位事件索引，现有 seek 对目标 tick 的行为已验证。
**步骤：**

1. 提供按时间排序的事件列表/时间轴标记，至少覆盖 T64 实测通过的死亡/击杀和地图目标事件；支持筛选事件类型与玩家。
2. 点击事件跳到准确 tick，并显示事件参与者；加载中、事件不可 seek、时间映射缺失都要反馈真实状态。
3. 将事件导航与播放时间轴分层，不能占用底部目标进度条；键盘上下选择事件、Enter 跳转，输入控件有焦点时不触发全局快捷键。事件栏与 T67 面板共用焦点/快捷键优先级规则，并允许用户配置事件导航快捷键且检测冲突。
4. 用真实录像核对前后顺序、tick、玩家名和跳转画面；为无事件、同 tick 多事件和越界 tick 加负控。

**验收：**每个展示事件都有实际来源；点击跳转同 tick；脚本未执行或事件为空不得以“0 == 0”判 PASS。

### T69 — 可折叠小地图选人

**前置：**T65 坐标投影和玩家点位通过；T66 手动选择目标接口稳定。
**步骤：**

1. 右上角默认只露出折叠页入口；展开后显示简洁地图底图和红/蓝小圆点，不默认显示复杂纹理、标签或装饰。
2. 悬停点位显示玩家名、队伍和有数据支持的简要状态；点击立即切换该玩家，不触发 0.3 秒死亡自动接力动画。
3. 提供缩放/重置与展开关闭操作，记住上次面板状态；无地图或坐标不可用时显示原因，不留一块空白画布。
4. 检查密集点位命中、重叠提示、地图边缘点、窗口缩放和不同地图投影；不得因 hover 或选中重排点位导致误点。

**验收：**折叠/展开、hover、click 和立即切换均有可观察断言；KOTH/Payload 点位与世界坐标对应。

### T70 — 整合后最终验收

**前置：**T49.1–T69 的 blocker 已关闭或由用户明确接受；用户整合并冻结代码，期间不再 merge/edit。
**步骤：**先跑受影响的 focused gates 与 SourceTV HUD 矩阵，再由助手在冻结树运行 `bash verify-all.sh --quick` 恰好一次；记录 HEAD、构建、demo/地图/帧/trace hashes、各门 verdict、远端同步、锁与进程状态。任何冻结后改动都废弃该次最终结论。
**验收：**SourceTV KOTH/Payload 数据与画面各有证据；死亡接力、手动切换、暂停动画、无目标、事件导航、小地图和布局恢复均有覆盖；POV 与 ragdoll 明确记为本波未实现。

**本波并行与执行规则：**

- 先由 T63 第 1 步产出 KOTH/Payload 的共同 demo 样本清单（路径、地图、录制类型、hash、tick 范围）；这一步完成后，T63 数据字段、T64 事件、T65 小地图三个审计可并行推进。没有对应真实 demo 时停止依赖该样本的结论，不拿 POV 或合成数据替代。
- T56/T57 与 T63–T65 的静态源码/资源盘点也可并行。任何会启动产品/探针、扫描 demo、构建、抓帧或运行 mutation 的阶段都必须取得共享锁后单实例串行；并行只指互不覆盖的静态审查和各自报告编写。
- T66–T69 只能在 T63–T65 的数据契约冻结后启动。先由用户/审查者按实际代码确定文件所有权；只有互不重叠的模块才分支并行，`main.cpp`、同一 UI 容器、demo parser 或 renderer 的并发编辑禁止。
- T67 与 T68 的面板/键盘交互必须先共用并冻结一份输入优先级约定；小地图点选、玩家行点选和手动切换一律即时生效，不得误走死亡接力动画。HUD 视觉布局、键盘焦点与自定义 HUD 组件冲突检查应在同一窗口尺寸矩阵中验收。
- 执行优先级：当前 T52/T53/T54 在跑的工作先完成或交接；T56/T57 与 T63–T65 的静态审计并行；之后优先做 SourceTV T66–T69，再继续 T58–T61 地图/人物产品修复；T62 做现有范围 focused 矩阵，T70 才是包含 SourceTV 的最终验收。
- 每个任务跑 focused gate 即可；T70 是本波唯一完整链。所有测试窗口隐藏或置于隔离桌面，不得覆盖用户当前窗口、抢焦点或播放声音。
