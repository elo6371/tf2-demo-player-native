# 当前任务清单 — 2026-10-23

## 0. 状态口径与负责人

- 主线快照：p0-entity-protocol，HEAD 3e4211f。当前提交历史已经包含 T50、T51、T52、T53、T54 的本地 merge commit；此前“这些分支都未合并”的进度表已过时。这里的“已合并”只表示进入本地集成树，不代表功能验收完成或已经推送。
- T49 的完整链报告记录 24/24 通过。T49.1 的全屏非干扰门禁仍按最新运行报告列为未接受；完整链通过不能替代这项专门验收。
- 当前工作树存在用户的 HUD 证据改动和未跟踪 evidence/release-gates/。所有任务必须保留这些内容，禁止清理、覆盖、暂存或提交它们。
- 当前报告之间有冲突：T52 的已提交报告为 57 OK / 1 FAIL，用户最新摘要为 67 OK / 1 FAIL；T54 的已提交报告称滚动与历史导航通过，用户最新运行状态又报告这两项有运行时缺陷。T53 的旧进度说性能读数未齐，而主线证据已包含 append/build 成本。冲突项先对账来源、commit、exe、trace 与时间，再决定是否补跑。
- 用户负责创建/切换任务 worktree、实现、运行门禁、提交与整合。本助手负责代码审查、疑难问题定位、任务拆解和指导；不替用户合并功能分支。
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
9. 不运行完整链作为调试工具。每项任务只跑受影响的 focused gate；最终冻结树上由用户安排一次完整链。若完整链后改动触及已测模块，重跑对应 focused gates；影响多个模块或链断言时重新冻结再跑整链。
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
| T49.1 全屏非干扰 | 未接受；最新运行摘要为三轮 FAIL，且旧报告/本地证据需要核对 | 按下面 T49.1 步骤恢复可信的隔离运行 |
| T50 相机书签 | 本地已合并；分支报告曾记录 CAMERA-UI PASS | 集成树复核一次 camera-ui-check；只在源码身份不匹配时重跑 |
| T51 HUD 导入 | 本地已合并；本地 forward/mutation 日志存在，用户 HUD 证据有未提交修改 | 先只读审查现有日志和 diff，保留 HUD 改动；确需补测时跑 hud-check |
| T52 队伍色 X-ray | 本地已合并，但仍有一条区域外像素断言红；报告读数与最新摘要不一致 | 对账实际帧/包围盒/外扩轮廓，修复后重新 forward 与 mutation |
| T53 投射物轨迹 | 本地已合并；主线有 gate 与 append/build 性能读数，旧进度报告未齐 | 核对性能证据所用 commit/exe；补齐交付报告和未验证项，不重复全链 |
| T54 logs.tf | 本地已合并；已提交报告称滚动/历史通过，最新运行摘要称出现缺陷 | 以最新运行的源码、exe、trace 和页面状态为准，复现后关闭冲突 |
| T55 时间轴/剪辑/导出 | 按原计划延后 | 在相机和 review UI 验收后按 T55 步骤实施 |
| T56–T62 | 新增计划 | 先做 T56/T57 审计，再按依赖实现和验收 |

## 3. T49.1 — 全屏非干扰门禁关闭

**目标：**可信证明自动测试不会抢占或覆盖用户当前操作窗口。
**非目标：**不能以只运行隐藏窗口替代 foreground/control 阶段，也不能为了让测试通过而改变产品窗口行为。
**前置：**停止所有其他运行；确认当前桌面隔离方案确实不会把可见 control 窗口显示在用户活动窗口之上。

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

### T51 — 自定义 HUD 复核与用户文件保护

**前置：**当前 evidence/hud 有用户未提交改动，任务执行者先保存其状态信息但不得 stash、还原、清理或覆盖。
1. 只读检查 hud-check.sh、当前 diff 与 forward/mutation 日志，确认用户文件 fixture 的目录项和哈希清单均可解释。
2. 只有日志身份不匹配或缺失时才运行 bash hud-check.sh；正向全绿后再运行 bash hud-check.sh --mutation。
3. 核对有效 HUD、缺失/畸形/越界输入的具名拒绝、预览/apply/reset、分辨率缩放、reset/uninstall 后用户文件逐字节不变，以及 HUD 开关不改变区域外世界像素。
4. 变异 source 必须逐字节恢复，恢复构建的 probe/frame 应复现 baseline。不要把现有用户证据一起 stage 或提交。

**完成：**所有 gate 断言有非零实测，变异全由断言捕获，用户文件清单前后完全相同。任何被删改的用户文件立即停止并报告，不自行恢复。

### T52 — 队伍色 X-ray 描边红项关闭

**前置：**先对账已提交报告 57/1 与最新摘要 67/1；使用实际失败帧和其同次 trace，不能混用两次运行。
1. 核实失败断言的两个运行是否真的同地图、同实体、同 pose、同 exe；检查 outline 区域取自当前 frame，且红/蓝双方都在同帧出现。
2. 追踪实体世界模型网格 AABB 与 silhouette expansion 的关系。当前证据把区域外差异归因到被扩张的轮廓壳，旧 item AABB 不等于实际绘制范围；修正两边读数使用同一来源。
3. 先运行一次代表性 forward：bash outline-check.sh。只有区域外像素红项已闭合、截图身份与 trace 匹配且其余 assertions 非零时，才运行 bash outline-check.sh --mutation。
4. mutation 必须覆盖换队色、普通深度测试导致遮挡、overlay 恒开/泄漏、无玩家或未知队伍等适用路径；INVALID 单列，不能作为已捕获。
5. 恢复源码 byte-identical 并重建；记录实际 assertion 数、每条 mutation 结果和 mask 外/内像素。

**完成：**forward 0 FAIL，适用 mutation 全 RED、0 INVALID，恢复后 baseline 可复现。
**停止：**帧外差异无法用同一渲染范围解释、红蓝玩家主体不在帧中、正控不成立或两轮输入身份不一致。

### T53 — 投射物轨迹 gate 与性能证据收尾

**前置：**主线已有 trajectory-overlay-check.sh 和 evidence/trajectory/demo.txt；这些读数能复用的条件是 commit、exe、demo、sampleRows 和配置身份相同。
1. 对照当前 HEAD 和 evidence/trajectory/demo.txt、evidence/verify/10-trajectory.txt、evidence/verify/10-trajectory-mutation.txt 的 hash/来源；无需为重复数字重新跑 24 步链。
2. 若 forward 来源匹配，复用；否则运行 bash trajectory-overlay-check.sh。核验 13 项 gate 规则、真实 projectile 输入、owner/slot lifetime、全体与单玩家过滤、消失与约 0.5s linger、无投射物输入。
3. 只在 forward 全绿时运行 bash trajectory-overlay-check.sh --mutation；每个 mutation 必须有不同的具名读数，不编译的 mutation 记 INVALID。
4. 报告 append median/p90/max 与发生最大值的 packet，build median/p95/max、240Hz 的 4.1667ms 预算占比、query 比较上界、slot reuse 对筛选的影响。均值或中位数不能掩盖最大值。
5. 核对 overlay 的开关和采样间隔在真实产品设置中可操作；若性能 gate PASS 但用户 UI 控件未接线，要拆成明确未完成项。

**完成：**实际 gate PASS、mutation PASS/恢复完整、性能样本数非零且所有关键百分位可追溯到原始数据；旧“缺三个读数”的状态被证据明确关闭或仍保留。

### T54 — logs.tf 最新运行状态对账

**前置：**旧 evidence/logstf/T54-REPORT.md 是 PASS；最新运行摘要称 scroll/history 曾失败，两者必须在同一提交上复核。
1. 先从最近一次实际运行日志确认运行时间、HEAD、exe hash、WebView2 runtime 来源、页面 URL 和 browser state；不以旧报告覆盖更新的失败。
2. 对可疑路径单独复现 scroll 和 back/forward：断言 scrollOffset 实际改变、canBack/canForward 随导航改变、脚本错误为空；页面不可达/脚本未执行不能读作 offset=0 或历史成功。
3. 在当前集成树运行 bash logstf-check.sh；只有 forward 全绿才运行 bash logstf-check.sh --mutation。
4. 保留已提交 34 assertions/旧 mutation 日志，新增复核日志到独立 run-id 目录，不覆写旧证据；把旧通过与新失败按身份逐项对照。
5. 核验 empty state、合法/非法 host、resize、scroll、history、reload、close/reopen、播放响应；无 runtime 的机器需标记实际未验证，不能用 fake host 代替真实 runtime 结论。

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

## 11. T62 — 全软件视觉矩阵与最终验收

**前置：**T49.1–T61 的 blocker 已关闭或由用户明确接受为限制；T55 若按原范围实施，则必须在本任务之前完成。所有功能分支由用户整合并冻结，禁止验收过程中继续 merge/edit。

**步骤**

1. 冻结最终 HEAD；确认干净 worktree、用户证据处置状态、远端目标、host lock FREE、无产品/探针/编译进程、无待合并分支。
2. 在冻结树先运行 T50–T61 直接受影响的 focused gates，一次性记录实际 assertion 与证据身份；只补缺口，不重跑已证实且身份匹配的无关 gate。
3. 跑地图矩阵（T56 选定地图 × 已修材料/几何）和人物矩阵（T57 可达职业 × T61 可达动作）；逐格记 PASS / FAIL / NOT VERIFIED / UNSUPPORTED 与原因。
4. 核验 T55 短片段导出的容器可解码、时间边界和音画同步；若 T55 未完成，不得声称全软件完成，也不得提前跑最终整链。
5. 确认所有 FAIL/INVALID 都有 owner 和处置，不把 release gate 全绿等同“画面正确”；冻结后由用户运行 bash verify-all.sh --quick 恰好一次。
6. 如 full chain 后有任何改动，废弃该 freeze 结论；按变更范围重跑 focused gates，并在必要时重新冻结后再跑完整链。

**验收：**所有矩阵单元有证据或显式未验证理由；focused 与 full-chain verdict 分开报告；最终 HEAD、日志/帧 hash、远端哈希、锁和进程状态齐全。

## 12. 顺序与并行表

1. 首先按 T49.1、T52、T54 当前冲突优先修复/对账；T50/T51 做只读审查并复用身份匹配的证据；T53 先审计已有 gate/artifacts。所有运行串行排队。
2. T56 与 T57 的静态源码/资源盘点可以并行；所有它们的构建、启动、参考图抓取和 mutation 仍必须串行。
3. T58 必须等 T56 的几何归因；T59 必须基于 T56 的材质账本。两项会触碰共享 renderer，默认串行。
4. T60 必须等 T57；T61 必须等 T60 的模型显示稳定。禁止把模型绘制与动画缺陷在同一提交里一起改，除非先证明两者不可分并记录理由。
5. T55 只有在 T50/T54 review UI 通过后才实现。纯 UI/数据设计可与 T56/T57 静态盘点并行；产品改动先检查 main/settings/render file ownership，确认没有共享文件冲突再排期。
6. T62 只能在上述任务关闭、用户整合完成并冻结后开始。最终 full chain 与任何其他操作、编辑、merge、构建、抓帧或 mutation 均不可并行。

## 13. 每个任务交给审查时

- 给出分支/HEAD/base 与干净状态；标明工作树里原先已存在的用户文件。
- 给出 diff 文件列表和每处改动对应的验收点；超出任务范围的变化单独解释，不顺手清理。
- 先报 FAIL、INVALID、NOT VERIFIED，再给 PASS；贴出实际 gate 最终摘要和退出码。
- 给出参考/产品帧和 trace 的身份 hash，截图必须来自报告所指的同一构建和输入。
- 展示 mutation changed → assertion red → source restored byte-identical → restored build baseline 的闭环。
- 说明哪些结论不能从当前证据推出，并提出最便宜的下一次验证。
- 交付后由用户整合；整合树上由用户安排受影响的回归。本助手收到分支或 diff 后做代码审查和问题定位，不代替用户合并。
