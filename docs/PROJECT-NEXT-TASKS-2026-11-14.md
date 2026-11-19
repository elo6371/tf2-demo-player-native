# 全项目验收结论与下一阶段任务清单

初版日期：2026-11-14（北京时间）；状态快照更新至 2026-11-15（北京时间）
依据：`TASK-REPORT-2026-11-14.md`、G1/G2 分册、现有缺陷登记册、实际 Git 历史与磁盘证据。
用途：交给下一位执行者的中文工作入口；配套详细交接为 `PROJECT-HANDOFF-2026-11-14.md`。
本文件中的安排是建议执行顺序，不代表已启动任务或已向其他聊天派发任务。

## 最新执行入口（2026-11-18，上传后续作）

完整N00–N30任务、按步接续、失败后自主解决、五个责任域队列、架构效率和源码参考要求已更新到[全项目连续执行清单](PROJECT-EXECUTION-UPDATE-2026-11-18.md)。该清单及[N-STATUS最新节](N-STATUS-2026-11-14.md)优先于下文旧身份记录；首发七图/粒子/多视角范围保持用户既定要求。

本轮实际修复companion序列循环位：九职业2516条元数据一致，跨周期样本与反向变异通过，pose-family40/40；已编进产品`8b1ab1f0…677641`，详见[N17修复与证据](N17-COMPANION-LOOP-2026-11-18.md)。默认动画和Scout视觉仍未闭环，不重复开发n17anim尚未提交的时钟/混合WIP；先审验再接回。

已核main/tag快照`98ddaeba`（无evidence）；开工远端integrate`30a2fcfb`与本地`3e3f47f3`不一致。决定保留历史，以浅克隆main提供轻量代码副本；archive仍引用旧blob时重做快照不会让完整clone变20MB。

## 历史执行快照（2026-11-18早期）

当前集成树 `work/integrate` 基线 `f25e954f`（产品 `5a599d1e`：世界 FOV 拆分、动画 helper skin 矩阵、idle token 选择、实体账本字段），上一基线 `2305215`、功能合并 tip `d5037e58`。本轮重建 exe SHA-256 `f341ab1e…0d15`（3,185,664 B，exe_count=54），并在该 exe 上复跑 camera_probe / camera-ui / pose-families 三门（全部 PASS，读数见各行），但未重跑完整视觉门禁与 verify-all。依据 `docs/WORLD-VISUAL-REPORT-2026-11-17-ZH.md`、`docs/WORLD-VISUAL-INTEGRATION-2026-11-17.md` 与本轮N17相机调查；下面2026-11-15 candidate/e5记录均为历史，不覆盖本快照。

已接受的局部修复：天空默认开启并改在opaque world后绘制（天空门17/17）；位移网格转置修复，位移边界吻合7,502/7,855（engine oracle 7,535），之前被回退平面的13个patch恢复；实体网格二次染色修复（379,038 px mask及mutation）；未知开关exit 13及`--window-size`；地图总览/HUD 213 checks。报告称四姿态帧与位移分支逐字一致，但不能据此推导人物模型外观通过。

未闭环：pr600仍报告94 px真几何被背面绕序测试剔除；14个退化位移patch仍平面回退；Odyssey几何洞门因无其专用demo pin未过，Odyssey不属于首发七图；world-material在candidate/e5基线上同样红；hud-live/fullscreen-quiet未在此exe重跑，verify-all未端到端跑（camera-ui已在f341ab1e上复跑PASS）。图 `analysis/integrated-up0.png` 的Scout外形明显可疑；N17报告测得屏外像素为0，已排除“被画面裁切”，但mesh/LOD、UV/材质、骨骼/权重、动画或投影仍未定因。本轮已提交世界相机错误复用ViewModel FOV的修复（5a599d1e），并以camera probe、定向构建及同demo/tick隔离抓帧验证；Scout本体仍需同帧对照与逐层诊断。

| 工作 | 当前判定 | 证据边界与下一步 |
|---|---|---|
| 集成世界修复 | **报告级局部验收** | 集成报告绑定exe `9d8333d8…bf7e86`；本轮基线HEAD `f25e954f`/产品`5a599d1e`，父`d5037e58`。world FOV修复已提交并集成；camera_probe/camera-ui/pose-families 在 exe `f341ab1e…0d15` 上 PASS；完整视觉门禁与 verify-all 未重跑 |
| Scout / N17-N18 | **FOV混用已修并窄验证；Scout本体仍OPEN** | 世界相机原先误用ViewModel垂直FOV；已拆Source水平worldFov与ViewModel FOV，90°@4:3转换为73.74°垂直并按pane aspect投影。idle 契约（旧 idleRate=0 冻结 cycle）已确认与 TF2 不符，修复在 work/n17-anim 进行中（task-7）；现有model-data、pose、body tint或entity tint门仍不证明玩家完整模型匹配游戏；继续同demo/tick/entity对照和模型分层诊断 |
| 世界残项 | **独立OPEN** | pr600绕序剔除与14个退化位移面在 work/world-cull 处理（task-9）；world-material红另行建卡；不可通过关闭cull或重钉错误pin隐藏 |
| 架构/验证 | **并行继续** | 审计模型probe与gate覆盖属于N25质量/效率任务，五个已修门禁取真实verdict+具名负控在 work/qa-gates（task-10）；构建/主循环按既有路线推进，不因视觉诊断暂停 |

相关源码和数据站点按任务域列于 [`TF2-SOURCE-REFERENCE-INDEX.md`](TF2-SOURCE-REFERENCE-INDEX.md)。后续代码/资产任务要记录参考仓库commit、文件/符号和适用边界；N17优先查OthmanAba TF2源码，Valve Source SDK用于共享引擎机制，最终以当前安装资产、demo独立oracle和产品读数裁决。

## 历史执行快照（2026-11-15，仅追溯candidate/e5）

当前树：`candidate/e5` @ `0974860`。本轮八个交付提交均在历史中；复核期间另有独立文档更正提交 `0974860`，只改执行计划与 Q5 证据措辞。`tools/audit-tree.sh` 已实跑 `VERDICT: OK`。工作树保留用户两份 handoff 文档、另一执行中的 `native/src/main.cpp` 与 `native/tools/demo_open_probe.cpp` 修改、未跟踪的 N05 draw 盘点草稿，以及本轮两份任务清单修改；本轮没有改动两个产品文件，也未将它们或草稿计入 N12-C/N05 验收。`.gitattributes` 当前与 HEAD 相同，行尾策略尚未落文件。

| 工作 | 当前判定 | 证据边界与下一步 |
|---|---|---|
| N05 849/919 | **OPEN：现象确证，机制未定** | 静态诊断否证了两条候选解释，但没有定位实际 GPU draw；补实际 pass/triangle/UV/覆盖证据后逐点修复。D01 不得关闭 |
| N06 世界帧异常分组 | **缺少交付** | `N05-N06-MAP-DEFECTS` 正文只有 N05 三卡；另开白噪、黑区、蓝面族群表和同机位 on/off，不把文件名误当 N06 完成 |
| N07–N11 地图 | **盘点/矩阵文档完成，产品签收未完成** | N08–N10 主要为静态证据；推车 NOT VERIFIED、`sky_camera` UNSUPPORTED；七图 14 格仅 2 格合格，证据跨四个 exe，D01 仍 OPEN |
| N12 相机 | **A/B PASS；C 第一步完成，第二步未做** | A/B 报告绑定 exe `1c3580e2…`。C 已加相机 tick 轨道和 Q5 哨兵；`demo-clock-check.sh` 一次 exit 0 / 62 断言。C 的精确 tick 消费端未改；SetView/PacketEntities 次序在 POV 样本为 0，stale 仍实测 1100/15 tick |
| N13–N16 UI | **侦察/契约为主，功能未闭环** | N13 没有 follow/选人命令；N14 底图与独立比例未签收；N15 DPI/HUD 遮挡未过；N16 只有多视角契约、没有产品实现 |
| N17–N21 角色/音频 | **覆盖报告，产品证据不足** | ViewModel 动画仍 bind pose/NOT VERIFIED；OOM 当前 HEAD 未跑；N20 三指定效果未实现；N21 听感需人在场 |
| N22–N26 工程 | **计划与部分门禁修复已落地，工程验收未完成** | N22 CI 未远端运行；N23 全树 `/W4` 已落地（55/55、138 TU 零告警、7 个目标 `/WX`，见 `docs/N23-WALL-LEVEL-2026-11-18.md`），身份门禁干净树 PASS（assertions=222），`/analyze` 测过 81 TU（8 条、修 1 条）；sanitizer 已在两条路径取读数（2026-11-19，见 `docs/N23-WALL-LEVEL-2026-11-18.md` §8）、故障注入未开始；N24 phase 1 已落地（`88ade848`，`main.cpp` 16,221 行）；N25 五门禁已有 2/5 真 PASS（class-pose + animation-drive-states，后者 2026-11-19 复跑且四条负控 4/4），其余仍是"修了没跑"；P9 三处 C6262 栈转堆已落地（`native_ui.cpp:70/76`、`settings_store.cpp:132`、`main.cpp:2287`；`/analyze` 逐 TU 复测 warnings=0/0/2，`main.cpp` 只剩 `wWinMain` 的 58,208 B 与 C28251）；warn/fault/frame 链 2026-11-19 在新鲜 exe 上复跑 PASS；未解决项与顺序见 `docs/TASK-LIST-2026-11-19.md`；N26 构建缓存实测待做 |
| N27 导出 | **范围和组件契约已裁决，产品实现未开始** | MF 探针支持方案 C；须实现产品内音视频导出、可选组件安装/卸载和缺组件的明确失败路径 |
| N28–N30 发布 | **N28 文档就绪；N29/N30 阻塞** | 还未集成冻结，未做干净环境走查，未跑 `verify-release.sh`；禁止写成发布验收完成 |
| `.gitattributes` 行尾规则 | **策略已定，文件改动待安全窗口** | 选定仅约束 `native/src`、`native/include`、`native/tools`、`native/cmake`、根目录门禁脚本及 `tools` 为 LF；证据目录保留 `-text`，不动 Markdown、不做全树 renormalize。本轮补丁在共享工作树并发写入时两次消失，当前 `.gitattributes` 与 HEAD 相同，不能宣称已生效 |

本轮提交：`d8b10a3`、`ed20a7e`、`b6d8e55`、`5931247`、`51e4421`、`9672e13`、`db0f384`、`3901ddf`。`b6d8e55` 曾误带 `native/src/demo_header_camera.cpp`，`db0f384` 已补齐声明、调用和 CMake 登记；历史未改写，当前文件参与 `tf2_core` 编译。门禁修复只做了局部语法/拒绝分支核验，**不等于五个门禁有真实 verdict 或全绿**。

### 下一批派工顺序

| 顺序 | 负责人 | 任务 | 可并行/必须串行 |
|---|---|---|---|
| 1 | D + B | 完成 N12-C 第二步：`resolvePovEye` / `resolveRelayEye` 消费精确 tick 相机轨道；补顺序未知时的明确定义和 counters | 相机生产代码由一人独占；QA 可并行设计包顺序夹具，但不可同时改 `main.cpp`/同一 gate |
| 2 | F + B | 先为门禁确认独立 OUT/scratch 和设置副本，不写 tracked evidence；再对五个修复门禁逐条取真实 verdict 与具名负控，不跑完整发布链 | 可与只读任务并行；产品构建、mutation、GPU 抓帧按 hostlock 串行 |
| 3 | C + B | 补 N06 分组交付，同时继续 N05 实际 draw 归因 | 静态资产盘点可并行；产品诊断字段和抓帧需同一产品写域预约 |
| 4 | D | 推进 N13 用户可选跟随，再做 N14/N15 UI 缺口 | 先产品命令/状态模型，再 UI 接线；N16 可先只读审查契约 |
| 5 | D/E + F | N27 导出实现；N17–N20 按用户首发范围实现人物/武器/指定效果 | Media Foundation 探针已不是阻塞；共享 renderer 和 CMake 按接口窗口集成 |
| 6 | A + 各 owner | N28 集成，确认受影响证据后再做 N29 干净环境；最后 N30 一次发布链 | 全员停写冻结候选后串行执行 |

行尾规则的技术边界：决策是仅对维护中的源码、构建定义和门禁工具设 `text eol=lf`；不对 Markdown、证据目录或全仓所有文本设规则，也不运行全树 renormalize。规则尚未保留在当前 `.gitattributes`，所以当前检出行为尚未改变。Git 属性约束 add/checkout 转换，不能阻止程序在工作目录生成 CRLF，因此门禁仍必须写独立 OUT/scratch 并由 `audit-tree.sh` 检查。证据目录的专用 `-text` 规则应保留且优先；其他 worktree 不会被本地编辑即时改写，只有拿到此规则并对匹配文件执行 checkout/add 时才会采用。`core.autocrlf` 在本次 PowerShell 环境读到 `true`，另一个执行 shell 读到 unset；因此不把它当作 CRLF 事故根因，只把 LF 属性作为一致性策略。

Q5 证据保留缺口：运行报告说明原始 7 份生成物已还原、26 个未跟踪产物已删除；本次只能核对报告和提交，不能重验那次运行的原始字节。下次受影响的窄门禁重跑时，把 rc、exe/sidecar、原始日志和 before-history 输出保存到独立 OUT 并附 SHA；62 是静态断言调用点数量，不冒充实跑断言计数。

## 一、验收结论

**本批按局部交付验收，项目整体继续开发，发布状态仍为 NOT-YET。**

| 交付 | 本次验收 | 证据与限制 |
|---|---|---|
| 暂停后小地图消失修复 | 接受这条产品修复 | `a153030` 已入当前分支；父提交负控与修复后对照可追溯；尚未覆盖同图重开/seek 后失效位姿 |
| 小地图 A12 | 接受门禁修复及历史运行结果 | 正向 104 断言；修复后日志哈希 `79c39fea…` 已复核；三变异的红因修正已落树；本次未再次执行会重建的变异套件 |
| 七图小地图抓帧 | 接受投影/位置的有限结论 | 本次重新运行现有离线检查器，rc=0、`MINIMAP-FIVE checks=205 failed=0`；未重抓产品；KOTH 单控制点朝向弱、独立世界缩放仍未证 |
| 800×600 resize | 接受一个新增尺寸的证据 | 报告记 39 检查；不等于多 DPI、连续拖拽或所有尺寸通过 |
| `animation_viewmodel_probe` 合成夹具 | 接受夹具修复 | `68b2555` 改成可观测的 child bone；日志哈希 `445ada6c…`、15 项标志全 true；不是新人物动作功能 |
| `verify-iso.sh` 哈希 | 接受所示机制修复 | `124cc3b` 改 stdin 哈希；黑点门禁从 seed 失败恢复；被引用的 27 个门禁没有在本次全部重跑 |
| 七图 14 次探针 | 接受输入/解析/预算测量 | rc=0 不证明材质、物件、天空或地形在画面中正确 |
| 七图世界帧 | 接受为初筛资料 | 每图单一 STV tick；白噪、黑区、浅蓝面均未完成同机位归因 |
| 黑点门禁 | 接受“已知点仍 OPEN”的回归检查 | 日志哈希 `261e4d09…`；该 PASS 不代表 849/919 已修 |
| 角色、武器、多视角、架构、发布 | 本批没有新增完整验收 | 仍按后续任务推进，不因小地图通过而自动关闭 |

### 本次独立核对的范围

- 当前分支 `candidate/e5`，HEAD `1eb84d7`；`94e7612..a92b1bd` 的提交数量实测为 12。
- 当前产品路径：`D:/TF2_Native_Worktrees/t105-e5/native/build-nmake/tf2_demo_native.exe`；不要使用另一个过期 Release 目录。
- exe SHA-256：`cfcdbe5d48c6e71e5853b26b89da1578d521146447338a379a25bb9ad16ae2f3`，2,988,544 B；实测与 sidecar 一致。
- sidecar：`sourceTree=1eb84d7909d8`、`contentBinding=carried`、`dirty=1`、`dirtyFiles=2`，两份既有文档；加入本清单后文件数会再变化，旧 sidecar 不能当作最终净树证明。
- `git diff a153030..HEAD -- native/src native/include native/CMakeLists.txt` 为空；没有以此次验收为由重建产品。
- 复核了相关修复 diff、关键日志哈希，重新执行七图既有帧的离线检查；没有重新运行全链、产品抓帧、所有 probe 或变异套件。

## 二、报告需要纠正的口径

1. **缺陷编号已冲突。** 原登记册 D33=提交计数，D34=LOD1+，D35=裁剪导出范围；后续报告又用 D33=小地图无线稿、D34=暂停丢面板、D35=缩放未证；“40 条”不能直接理解为 40 个唯一问题。
2. **主表 OPEN 不能脱离后面的结账表使用。** D08 的缺帧部分已被七图 205 检查补上，resize 又补一尺寸；DPI/独立缩放/底图仍未完成；应拆成子项，不能整体重复做或整体关闭。
3. **缺一条 Upward f12 POV 不阻塞全部地图工作。** 其余六图 POV、七图 STV、静态物件和材质归因可继续；`pl_upward` 的 POV 必须保留，但不能冒充 `pl_upward_f12`。
4. **没有 DPI 测试开关不等于做不了 DPI。** 先审查 DPI awareness、`WM_DPICHANGED`、布局计算；自动检查计算边界，真实桌面多 DPI 在有人在场的窗口验证，不擅改用户显示设置。
5. **没有推送不阻塞 CI 开发。** 可以先做 MSVC/SDK 发现、workflow 编写、配置与代表目标的本地验证；远端实际运行需要发布窗口，但不能把整个架构任务停在“等推送”。
6. **加诊断字段不意味着所有旧帧必须立刻重钉。** 先检查渲染是否改变，列出依赖和身份变化；只补受影响证据，最终冻结时再统一绑定。
7. **设置文件写回不自动构成产品损坏。** 报告没有发现原十键值改变；首先修测试直接使用 tracked fixture 的流程，再独立判断未知键丢失、保存失败或真实用户设置损坏。

本清单使用新的 `N00`–`N30` 任务编号；引用旧 D 编号时必须带问题名称或 G1/G2 上下文，暂不擅自重编号历史资料。

## 三、背景、目标和范围

产品是 C++17 + Win32 + D3D11 的原生 TF2 demo 播放器，支持 POV 和 SourceTV；追求用户能正常安装、打开、播放、切视角、看人物与地图的完整流程，不以大量门禁 PASS 代替产品可用性。

首发七图：

| 地图 | 要求 |
|---|---|
| `cp_process_f12` | POV + STV |
| `cp_sunshine` | POV + STV |
| `cp_gullywash_f9` | POV + STV |
| `cp_sultry_b8a` | POV + STV |
| `koth_product_final` | POV + STV |
| `koth_bagel_rc11` | POV + STV |
| `pl_upward_f12` | POV + STV；含推车链路 |

- Odyssey 不在首发清单；不再用它代替首发图验收。
- 不做布娃娃、水面反射/折射、玻璃反射；本阶段不做光影/阴影一致性。
- 粒子仅限 Pyro 武器效果、Medic 治疗束、流血；其余粒子不隐含纳入。
- 水面基础外观、几何位置、贴图仍需正确；排除反射不等于接受一块错误穿越地形的平面。
- 多玩家分屏和小窗已是待实现需求，每窗独立选择玩家与第一/第三/自由视角；先双视角，再 PIP，再四视角评估，不能悄悄删去后两项。
- LOD1+、裁剪导出不是完成项：LOD 先登记真实可见需求和实现缺口；导出保留为范围待定项，用户决定前既不宣称实现，也不自行排除。

## 四、当前进度和架构状态

| 工作域 | 目前状态 | 尚缺 |
|---|---|---|
| 基线/素材 | 七 BSP、14 demo 输入已有清点 | 缺 `pl_upward_f12` POV；精确输入与使用能力逐格绑定 |
| 地图 | 能解析、预算有账、能出帧；部分具名材质有证据 | 黑点/白噪归因、七图外观、静态/动态物件、控制点、推车、3D skybox |
| 小地图 | 可画标记，暂停已修，七图投影有限通过 | 实际底图/线稿、独立比例、KOTH 朝向、楼层、载荷车标记、DPI、正常 UI 入口与连续状态 |
| 相机/HUD/播放 | 有现有实现和历史门禁 | 当前 POV/STV 语义、玩家身份、seek/跟随/输入与用户流程闭环 |
| 人物/武器 | 有动画、蒙皮、挂接等实现与历史局部证据 | 九职业动作/槽位/ViewModel、可靠数据边界、OOM 与指定效果的当前验收 |
| 工程底座 | 已有 `tf2_core` 51 个共享编译单元、相机脚本一个抽取切口 | CI、工具链便携、统一警告覆盖、继续拆主循环、预算/缓存实测 |
| 发布 | 未进入最终冻结 | 干净环境用户走查、最终七图矩阵、净树身份与一次发布链 |

架构已有实质收益：历史测量编译动作 634→137、clean build 648s→221s；这些数字是已有测量，不是本轮重新计时。G1/G2 没有新增架构交付，不能再说“核心库未建立”，也不能说“架构优化完成”。

## 五、角色与调度

| 角色 | 负责 |
|---|---|
| 集成人 A | 保护现场、分支/身份、串行写入窗口、集成与发布 |
| QA B | 台账、复现、证据边界、负控与验收 |
| 地图 C | 材质/世界几何、物件、天空、控制点、推车 |
| UI/播放 D | 播放、相机/HUD、小地图、多视角的产品交互 |
| 角色/武器 E | 九职业/ViewModel/动作/槽位/指定特效 |
| 架构/工具 F | CMake/core、CI/工具链、probe、警告和主循环拆分 |
| 素材 G | 精确 BSP/demo/自定义资产、来源/哈希和参考画面 |

这些是责任角色，不表示已创建/联系相应 AI；执行前由集成人填真实负责人。

- 可并行：代码只读审查、素材下载核对、台账整理、相机/多视角契约设计、CI 草案、逐职业样本定位。
- 必须协调单写：同一 `main.cpp` 区域、共享 renderer/shader/model 接口、CMake 目标图、同一公共 gate helper。
- 本机产品构建、变异重建与 GPU 抓帧按现有 hostlock 排队；不要抢锁或因为排队删除别人的锁。
- 不共享写域的工作可以独立 worktree 进行；同一路径文件不靠“AI 各自改完再碰运气合并”。
- 每次集成先产品修复和依赖接口，再功能，再行为不变重构；不要混在一个不可审查提交中。

## 六、立即任务：先消除返工源，同时推进可见功能

### N00　整理唯一台账与当前身份（A + B，第一项）

- [ ] 合并登记册主表、G1/G2 追加结果和报告；保留历史，不删除相冲突的记录。
- [ ] 为重复的 D33/D34/D35 分配唯一 ID，列出“旧 ID + 文档/批次 → 新 ID + 问题名”映射；重新统计唯一问题数。
- [ ] 把 D08 拆为七图帧、resize、DPI、比例、底图等独立状态，已通过的子集不重做。
- [ ] 更新 D06：exe 内容与当前 sidecar 对得上，但文档脏改动和最终净树发布身份仍要区分；不要求每次 docs-only 提交全量重建。
- [ ] 输出一张含负责人、依赖、风险、最小测试、状态的主表；所有后续 N 任务都挂上对应问题。

验收：唯一 ID，无“同 ID 同时已关又未关”的歧义；本任务只改文档，不触发长回归。

### N01　缺失素材与独立参考（G + B，可与 N00 并行）

- [ ] 查 demos.tf 和用户本地集合里的 `pl_upward_f12` POV；先检查 demo header，随后检查有效 tick/职业/视角，保留来源 URL 和 SHA-256。
- [ ] 若 demos.tf 只有 STV，直说 POV 搜索未命中；从有该 BSP 的游戏录制 POV 或请用户补录，不能承诺网站一定有。
- [ ] 准备同 tick/同视角尽量对齐的 TF2 参考图：正常地形、天空、具名物件、控制点与推车；没有参考先标未证，不用猜测补素材。
- [ ] 盘点命名材质 miss、`pak01_dir.vpk` 缺失的实际影响；先找 BSP pak、VPK、原地图完整包，再问用户补齐。

验收：七图 POV/STV 矩阵逐格有路径、哈希、能力和限制；缺 f12 POV 只阻塞该格。

- **替换方案（2026-11-15 标注，待批准）**：gate5 的 pov pin 与 gate3 的 `AUDIO_DEMO` 指向已不存在的 `autorecord_2026-07-02_13-26-46.dem`（21,856,667 B / sha256 `c8a58501…`，Sniper POV）。已实测替代：`pl_upward_f12/POV/autorecord_2026-08-08_21-43-59.dem`（146,853,666 B，实体模型同为 `c_sniper_arms.mdl`，tick 28795 实体 685 `sequence=30`）；代价是 6 处「录像自身属性」读数重钉 + §2 `v_*=0` 断言处置（新录像默认全扫 2 条 `v_*`，原为 0）。备选：只换 gate3（方案 B）；或从 2056 条 Steam 录像筛一条 `vStarModels=0` 的 Sniper POV（方案 C）。逐项改动清单与验收判据见 `docs/TASK-REPORT-2026-11-15.md` §2.5 的「替换方案」表。

### N02　修探针 Unicode/路径与失败契约（F，G2-D38）

- [ ] 在相同小输入下对比 ASCII、中文、带空格路径、TF 相对/绝对路径及不存在路径，先记录 rc/stdout/stderr。
- [ ] 审查生产文件打开 helper 和 probe 参数入口，确定是编码转换、参数契约还是打开失败被吞。
- [ ] 复用已有宽字符/`std::filesystem` 路径处理；保持 map name 与文件路径语义清楚，错误必须非零退出且有具名原因。
- [ ] 只构建受影响 probe，真实中文路径有效输入为正控，不存在/损坏输入为负控。

验收：相同文件不同表示的结果一致；失败不再空输出假成功；不为过测把用户目录改成纯 ASCII。

### N03　设置隔离与清理自检（D + B + F，G2-D40）

- [ ] 所有本批运行脚本给 `--settings-file` 传临时副本，记录输入 hash；产品写回副本后 tracked fixture hash 不变。
- [ ] 暂停/重启后原设置值正确；如发现未知键丢失或保存失败，另建产品缺陷，不把正常持久化一概禁用。
- [ ] 把暂停回归、七图 checker 和运行参数从仅仓外 `.scratch` 记录变成可维护的测试入口或可复建资源清单；不要新增一套重复 runner。
- [ ] 用两个代表 gate 复验公共隔离 helper，分别覆盖正/负 seed；四个 `iso-trap-selftest` cleanup 红另行复现和分类。

验收：临时目录可追溯、源码/fixture 不被写脏、错误 rc 和恢复正确；27 个 gate 引用范围与实际运行范围分开报告。

### N04　安全/崩溃缺陷优先级复核（B 分派，各域所有者修）

- [ ] 从原 QA 确认项筛当前仍存在的 bounds/分配放大、网络畸形输入、路径穿越、安装假成功、WinMM/D3D 失败和生命周期问题。
- [ ] 每项先最小输入复现，再定位生产入口，修复后有效输入仍成功；高风险项优先于画面美化和重构。
- [ ] 不把静态疑点直接称运行缺陷；若确认为未复现，留下原因与最便宜的下一步验证。

验收：确认的 P0/P1 有修复/证据/负责人；其余为有依据的 OPEN 或范围处置，不能被大表 PASS 掩盖。

## 七、地图任务：先归因，再实现，最后逐图签收

### N05　Sultry 849/919 draw-path 闭环（C，legacy D03）

- [x] 静态诊断与候选机制筛查（2026-11-15）：`docs/N05-N06-MAP-DEFECTS-2026-11-15.md` 否证了 METALGRATE alpha 未消费解释 919 亮白、以及 tiled colour=1 解释 849 黑点；这只是排除候选，不是定位根因或修复。
- [ ] 固定原 demo/tick/相机和输出尺寸，重新取得同一产品身份下 texel、albedo、face-id 与实际覆盖证据；旧报告缺少 2026-11-15 运行帧/日志，不可据它声称当前行为已复现。
- [ ] 确认已有 ordinal→face 结果；919 不能再简单称“face 选错”，已知问题是预测与实际采样分叉。
- [ ] 若缺必要信息，最小增加实际 draw 的 face/triangle/pass、UV、atlas tile、COLOR0/乘子来源；先核查两个点是否落入同一真实覆盖路径。
- [ ] 849 追踪零乘子来源；919 追踪插值/UV/覆盖差异；一次只修一个证实根因。
- [ ] 维持 alpha=0 合理黑点的负控，记录诊断改动与视觉改动的证据失效范围，禁止把 OPEN pin 重钉成无条件 PASS。

验收：能从输入面/材质一路解释到 GPU 实际颜色；不能以“门禁仍 PASS”代替修复。

### N06　七图世界帧异常分组（C + B，G2-D39）

- [ ] 单独建立 N06 正文分组表；目前 `N05-N06-MAP-DEFECTS` 正文只有 N05 三卡，不能以文件名或 N11 静态表替代此交付。
- [ ] 白噪：Gullywash/Bagel/Sultry 各选具名材质区域，查 UV/alpha/filter/depth/重复几何，先验证是否真为同一原因。
- [ ] 黑区：Process/Gullywash/Product 分别查覆盖、sky、alpha、PVS、vertex color；不要合并为“所有洞已修”。
- [ ] Sunshine 蓝面：定位真实几何/材质，做水面有/无的同相机对照，比较游戏内基础外观；不扩展到反射/折射。
- [ ] 每族先一个最小可证例，产品改动只对应被证实的原因，其他地图以受影响测试扩展。

验收：每族归类为产品缺陷/正常资产效果/诊断缺陷/仍未证，并给同机位证据。

### N07　材质与静态/实时物件（C + G）

- [ ] 核对 Sultry/Bagel/Product 命名 live-model 缺材质的资产链；原报告 20 slot 读数可能重复，不能当 20 个独立缺失模型。
- [ ] 核对 VMT/VTF、alpha-test/translucent、skin/bodygroup、纹理变换与代理；九种 rescue 材质补具名可见例，已有玻璃窗对照保留。
- [ ] 静态 prop 与 live model 分别核对 parent/origin/angles/skin/PVS、提交/绘制/拒绝计数与屏幕位置。
- [ ] 如调查半透明排序/`$nocull`，先造实际 world-brush 正例，不用 static prop 或全局关 cull 的大像素差替代。

验收：缺资产和缺渲染能力分开；关键物件具名可见，无无依据材质 alias/fallback。

### N08　控制点状态（C + D）

- [ ] 每图选 neutral/capturing/RED/BLU 状态样本，核对模型、skin/bodygroup、代理时间、texture2/modulation/alpha。
- [ ] 绑定真实网络/游戏状态与 UI marker，暂停/seek 后不能用旧 team/占领状态。
- [ ] 与 TF2 参考画面对照，覆盖一次状态变更和缺数据回退。

验收：控制点模型和状态都正确；不是控制台模型。

### N09　Upward 推车与动态 brush（C + D）

- [ ] 推车 entity/模型/父链保持一致，选择直线/弯道、静止/移动两组 tick；检查暂停、seek、倍速、PVS。
- [ ] 七图盘点门/平台/可破坏物等实际存在的动态 BSP 实体，分解为字段解码、变换、插值、绘制、像素位移。
- [ ] 未出现的物件记不适用，未实现不能记 PASS。

验收：推车正确显示并跟时间移动；动态 brush 不停在错误原点或被静态世界重复绘制。

### N10　天空和 3D skybox（C）

- [ ] 普通六面天空单独检查缺面、方向、接缝与遮罩；它不等于 3D skybox。
- [ ] 从真实首发 BSP 找 `sky_camera`，验证 origin/scale 与主相机转换，明确 pass/depth/遮罩策略。
- [ ] 一个首发图先实现正例+关闭对照+不应受影响区域，再扩其余适用地图；变换正确后再审视觉一致性。

验收：天际几何合成位置正确、不会盖住前景；不纳入光影匹配。

### 集成分支世界残项（C + B；与首发范围分账）

- [x] 集成报告接受：默认天空/opaque后sky pass、位移格点方向修复、已绘制实体白色tint、未知开关拒绝、map overview/HUD；证据和当前限制见 `docs/WORLD-VISUAL-INTEGRATION-2026-11-17.md` §3–5。
- [ ] **W-PR600 背面绕序（首发相关）**：用报告固定的cp_process tick600与94px ROI复现`area > 0`剔除；以已知朝向的正面/背面三角形和不同屏幕Y方向做正负控，推导面绕序应结合投影/坐标手性而非简单翻符号。`--no-world-cull`只作诊断，不作为修复；修后前景正常遮挡不变且94px中可恢复部分有像素/face证据。
- [ ] **W-DISP-14 退化位移面**：列出14个patch的map/face/disp id、退化原因和回退前后顶点；区分地图输入本身退化、浮点epsilon/采样/网格构建拒绝。对每类选最小正控与坏输入负控；仅在能按引擎语义重建时修，不以无效面积强行生成NaN网格，也不将平面fallback计为几何吻合。
- [ ] **W-MATERIAL 基线红**：对照同一exe/输入/日志身份复现world-material差异1481→858、15039→25995、227070→216114，判断这是位移修复改变覆盖/聚类的合法更新还是产品回归；逐项核输入、cluster和面数不变量，未查清前保持红，不重钉遮盖。
- [ ] `material.noCull`是报告列明的未支持能力；先扫描首发七图实际使用的VMT是否存在此属性并找有判别力的world-brush样本。有实际使用才实现/验收，无实际使用则登记首发影响并延后；不把它误称为上述94px孔洞根因。
- [ ] `world-geometry-hole`的Odyssey pin不是首发验收前置；无录像保持UNVERIFIED，不换其他图冒充。N11最终只要求首发七图同候选十四格；其余gate按本exe实际改动影响补测。

验收：每项修复有固定输入、证据身份、有效对照与缺陷负控；任何一项的完成不自动清除其他红项。

### N11　七图地图最终矩阵（B + C）

- [ ] N05–N10 边做边填写：BSP 查找、几何、材质、alpha、prop/live、控制点、sky、dynamic brush、适用推车。
- [ ] 每图 POV/STV 有具名物件与同机位对照，不能仅 tick0 或当前一张 STV 帧。
- [ ] 为每个未验证格给缺口和下一步；最后在稳定产品身份上补受影响格，不把历史截图改名变成新证据。

## 八、相机、人物、UI 与新功能

### N12　POV/STV 相机与播放（D）

- [x] **历史独立相机眼位对照（修复前 exe `cfcdbe5d…`）**：所测 POV 样本 cmdinfo 与玩家 origin 一致，修复前缺 viewOffset；该发现已推动下列 A/B 修复，不能继续写成当前实现仍缺加法。STV 没有真实导播相机数据，跟随眼位推导与导播真值分开验收；archive 的 1100/15 tick 陈旧问题仍待 C 解决。原报告：`D:/_shared/EXEC-2026-11-15/N12-CAMERA-ORACLE-REPORT-2026-11-15.md`。
- [x] **N12-A POV 眼高修复，有限样本通过**：`docs/N12-ACCEPTANCE-2026-11-15.md` 的三个有效 POV 帧在 exe `1c3580e2…` 上通过；证据包括 offsetZ 四位读数与修前后约 +75 u 位移，死亡零偏移帧不计判别分母。以下扩展验收仍待补齐，不把历史样本当作当前 HEAD 全部通过。
- [ ] N12-A 验收：在报告登记的 POV-f12 与 POV-upward 样本及 stand/crouch/`viewOffset=0` 状态上，最终 eye 与独立 oracle 的 `m_vecOrigin + m_vecViewOffset` 在预注册 2.0 u 容差内；角度继续符合 direct oracle 的 0.5° 容差。保留 N1 错 tick、N2 POV/STV 互换、N3 全零负控，新增缺 viewOffset / 错 tick 不得误 PASS 的负控；检查 seek、暂停恢复和实体切换不会跨 tick 串值。
- [x] **N12-B STV 蹲伏眼高修复，有限样本通过**：同报告 B2 眼高由 609.0 改为 586.0（offset 68→45），B1 站立帧保持不变；B1 的一致不提供区分力，不计 PASS 分母。过渡帧、缺属性和实体切换仍需以下扩展验收。
- [ ] N12-B 验收：对报告中的 STV-f12 t40000 及独立 stand/crouch 样本，和同一 resolved tick 的 `D_follow` oracle 比较，位置差 ≤2.0 u、角度差 ≤0.5°；负控覆盖错误目标、错 tick、缺失 viewOffset fallback 与 `viewOffset=0`，不得把 fallback 伪装成 oracle 匹配。
- [x] **N12-C 第一步：相机 tick 轨道与 Q5 哨兵**：`db0f384` 已登记新 TU、采样/查询 API 和探针，轨道默认 opt-in，产品尚未消费。Q5 的单次 `demo-clock-check.sh` exit 0；before-history 负例由伪 stale=0 改为 -1，62 为脚本断言调用点数，不是运行日志统计。
- [ ] **N12-C 第二步：产品消费精确 tick 查询**（D 实现，B 验收）：改 `resolvePovEye` / `resolveRelayEye` 及 POV/relay 调用处，让实体身份、origin、offset 和角度来自一致的查询状态；先核实轨道记录的目标能否覆盖用户选择的 follow 目标，明确无精确 tick、无属性、死亡/观战、generation 变化与容量淘汰的行为，禁止退回陈旧 checkpoint 后自称新鲜。
- [ ] **N12-C 包顺序与轨道负控**：现有 pov_f12 的 SetView-before/after-PacketEntities 均为 0 样本；补能区分这两种顺序的 packet 夹具或独立语料，另测 Delete+Enter 复用槽、同 tick 多包、完整/不完整包、跨 demo 重置、容量上限与错 tick。零 mismatch 计数不能替代从未进入的身份重建分支验证。
- [ ] N12-C 验收：报告中的 t20000 stale=1100 负例应选到与请求时间最近的有效状态，fresh-window 正例保持位置/角度连续；可用时要求 resolved snapshot stale ≤1 tick。增加“只有旧 checkpoint”“目标消失/serial 改变”“未来样本存在”负控，验证不串目标、不向未来跳、不静默声称新鲜；若语料无法达到 ≤1 tick，明确标 NOT VERIFIED 并记录实际上界，不放宽阈值掩盖缺数据。
- [ ] N12-A/B/C 通过后再复跑 N12 的 viewEntity、observer target/mode、实体 serial、userdata、第一/第三/自由视角、目标切换、暂停/seek/逐帧/倍速矩阵；独立 oracle 能证明 POV 的录制眼位，**不能**证明 STV 的真实导播相机（demo 未记录该值）。
- [ ] 矩阵覆盖 viewEntity、observer target/mode、实体 serial、userdata；区分输入眼位与实际渲染眼位。
- [ ] 验证第一/第三/自由视角、跟随目标切换、失效目标回退、暂停后恢复、seek 前后状态。
- [ ] play/pause/seek/逐帧/倍速/结束/错误索引分别验证；reverse 仅在产品宣称支持时纳入，未实现需写明。
- [ ] `--start-paused` + 非零 `--capture-tick` 不会推进 tick：取中间帧按已有播放/seek能力设置，不让脚本无限等待。

调度：A/B 已有有限样本证据，继续由一名相机负责人串行完成 C 第二步，QA 可并行补独立 fixture/负控。记录构建、exe/sourceContent 与采集身份；先短 probe/oracle，再按改动影响跑 `timeline-check.sh`、`history-coverage-check.sh`、`spectate-check.sh` 与相机消费者，不因此跑完整发布链。A/B 的历史读数绑定 exe `1c3580e2…`；C 第一步/Q5 证据见 `docs/N12-C-STEP1-IMPL-2026-11-15.md`、`docs/N12-C-Q5-RUNTIME-EVIDENCE-2026-11-15.md`，不能互相改标签当作同一 exe 的完整验收。

### N13　HUD 身份与普通用户路径（D + B）

- [x] 只读通路调查已完成：`docs/N13-IDENTITY-HUD-UI-2026-11-15.md` 确认缺选人/follow/下一玩家命令及 broadcaster 概念，部分 `nameReason` 早退为空；这是产品缺口，不是等待按钮验收。
- [ ] 先在产品命令/相机目标状态层实现玩家选择与 follow，再接 UI，区分 broadcaster、POV 主体、被跟随者的身份来源；处理实体消失/serial 变化与无 userinfo。与 N12-C 的目标轨道范围保持一致，之后复用到 N16 每窗目标，避免双份不一致状态。
- [ ] 真实 broadcaster、POV 主体、STV followed player 的名称不混用；缺失 userinfo 显式未知，不伪造玩家名。
- [ ] timer/round/team/class/health/weapon/事件流核对 source 与 stale/缺失策略，Unicode/resize/DPI 分别检查。
- [ ] 打开 demo→选择 TF 根→找图→播放→seek→跟随→设置重启→异常恢复，必须能经产品 UI 完成，不能只有测试 CLI。

### N14　小地图底图与独立标尺（D + G）

- [ ] 先保留已通过的 104/205/39 检查；新增测试只覆盖真实缺口，不每天重跑三次变异。
- [ ] 核查 demos.tf 的已记录源码版本、许可证与复用结论；能安全实际复用则优先，否则用更适合 Win32/D3D 的实现并写明原因。
- [ ] 为七图决定授权 overview 图或 BSP 派生线稿；现在面板只有标记，不得称底图已实现。
- [ ] 独立核对可玩区比例/裁剪和至少三处不共线 landmark；KOTH 不能只靠一个控制点验证朝向；世界 bounds 大于可玩区不自动是错误。
- [ ] 增加 viewer/player/team/objective/cart 区分、楼层策略、缩放/布局策略及正常 UI 开关；缺数据不画猜测图标。

验收：玩家能识别地图与相对位置；投影 oracle 不只复制产品常量；牌子中有圆点不能替代可读底图。

### N15　DPI、resize 与暂停/seek 连续状态（D + B）

- [ ] 保留 800×600 正例，增加一窄窗、一大窗及连续 resize；区分客户区与外窗尺寸。
- [ ] 审查 DPI awareness 和 `WM_DPICHANGED`，先测布局计算；真实多 DPI 由人在场时验证，不擅改系统显示设置。
- [ ] 同地图重开 demo、后退 seek、目标切换、无位姿与跨图切换不能复用错误缓存；面板不消失，也不以旧位置冒充新位置。

### N16　多玩家分屏（D 牵头，C/F 审视图资源）

- [ ] 先写每窗 target/mode/FOV/camera/input focus/viewport/HUD 的状态契约；播放时间轴共享，相机状态独立。
- [ ] 第一片实现双玩家分屏：选择/删除玩家、激活 pane、每 pane 第一/第三/自由视角切换，鼠标键盘仅控制选中窗。
- [ ] 目标消失/死亡/无相机数据的回退，不伪造 POV demo 未包含的其他玩家数据；对每种 demo 标明可用目标范围。
- [ ] 测暂停/seek/倍速同步、单 pane 切换不影响另一个、resize/深度/裁剪/HUD/minimap 的隔离。
- [ ] 记录一视图和双视图的 CPU/GPU 帧时、显存、draw/instance 峰值，给限制与降级策略。
- [ ] 双视图通过后实现可调整小窗 PIP；再单独评估四视角容量，避免同时做三套布局。

验收：不仅截屏有两个框，而是真实不同玩家、独立模式/输入、共享时间同步；PIP/四窗仍有后续交付卡。

### N17　人物动作与服务器数据边界（E）

2026-11-18当前基线：`work/integrate` 已推进到 `4c620527`，其中 `fd25e93c` 把默认动画翻转为开（产品 `ef4aadd1…`）并给四个门禁补了显式 `--player-animation off`；Scout视觉仍OPEN；下列局部完成不替代本卡验收。最终四种采集、身份、证据和旧门禁归属见[N17报告](N17-SCOUT-CAMERA-ANIMATION-2026-11-17.md)。

**本项目统一参考与代码复用要求（执行每张卡前必读）**：

- TF2专用行为先查 **[OthmanAba/TeamFortress2](https://github.com/OthmanAba/TeamFortress2)**。目前它是本参考集里覆盖TF2客户端/共享游戏逻辑最广的代码树，动画状态、activity、pose参数、武器/职业行为优先从相关调用链查起；它是社区源码快照，不是官方运行时真值。每次记录固定commit、文件路径、符号、调用者/被调用者和本机游戏版本。
- 官方共享引擎对照：**[ValveSoftware/source-sdk-2013](https://github.com/ValveSoftware/source-sdk-2013)**，重点是`src/game/shared/tf`、`src/game/client/tf`、渲染/动画公共实现；SDK与已发布TF2可能不同，不能照抄常数。
- 当前发布资产：**[SteamTracking/GameTracking-TF2](https://github.com/SteamTracking/GameTracking-TF2)** + 本机`tf`目录；模型/材质格式另查[VDC StudioMDL](https://developer.valvesoftware.com/wiki/StudioMDL)、[Materials](https://developer.valvesoftware.com/wiki/Material)、[VTF](https://developer.valvesoftware.com/wiki/Valve_Texture_Format)、[Model Viewer](https://developer.valvesoftware.com/wiki/Model_Viewer)。
- Demo/网络字段至少交叉核验一个独立实现：[demostf/parser](https://github.com/demostf/parser)、[demostf/demo.js](https://github.com/demostf/demo.js)、[PazerOP/DemoLib2](https://github.com/PazerOP/DemoLib2)；网络/预测语义参考[Source Multiplayer Networking](https://developer.valvesoftware.com/wiki/Source_Multiplayer_Networking)。记录版本与适用语料，不把独立解析器输出冒充TF2像素oracle。
- BSP/地图格式可交叉查[Valve BSP](https://github.com/pySourceSDK/ValveBSP)、[Galaco/bsp](https://github.com/Galaco/bsp)、[tasbox-org/source-parsers](https://github.com/tasbox-org/source-parsers)；它们是格式工具，不是Source渲染oracle。
- **已验证问题必须回写 [coding-problem-playbook](https://github.com/elo6371/coding-problem-playbook)**：开工先同步GitHub并读相关条目；复现并修复后新增带症状、根因、尝试、修法、验证、边界、相关文件的条目，更新`problems/INDEX.md`，单独commit并push。只能记录真实复现且验证通过的问题；未解决项标未解决，不准包装成已解决。push失败时记录原始错误和本地commit，列入未完成任务。
- 统一程序：GitHub先读当前源码与revision → 查本机模型/动画/材质及demo字段 → 写预期和正负控 → 本项目最小修复 → 定向构建/门禁 → 同身份运行时证据 → 更新项目台账与playbook。只有授权且许可/依赖/API/行为都匹配才复用代码；否则移植规则并记录原因。

- [x] **N17-ENTITY-POSE / 同帧测量（未提交）**：实体账本含cycle、frame/localFrame/sectionFrames、skinBones/validBones；Scout ent14 ID91默认无序列，实验idle=175、cycle0、frame0/45；四帧均exit0。只证明产品实际选择/绘制，不证明TF2视觉等价。
- [x] **N17-PROBE-META / 仪表修正（未提交）**：companion blendCount复制正确（Scout stand=1/run=9/aim=12）；helper矩阵纳入写入数（78/78），同时保留无有效通道拒绝；helper自测新增计数断言。职业姿态门默认写scratch，锁内九职业40样本PASS且不污染tracked evidence。
- [ ] **N17-IDLE-CYCLE / 下一步E实施、B复核**：多 tick 下速度时钟已接入 `idleRate`（`main.cpp:13661-13675`），但首帧单 tick 仍因 `!state.hasPrevious` 停在 `cycle=0`（`animation_clock.cpp:90-143`）；先按固定revision TF2源码核idle与动作时钟语义，再以demo resolved tick而非墙钟做确定性采样；暂停、seek、重启应复现，补“强制首帧/使用墙钟”负控。不得仅因状态名是idle就判姿态正确。
- [ ] **N17-BLEND / 接续IDLE-CYCLE，可独立盘点**：读本机scout_animations的blend网格、pose参数范围、动画索引和bone权重；核OthmanAba/Valve的相同字段，再实现run/aim必要的参数混合与旋转插值。测试参数端点/中间值、缺失参数、不支持格式；错误blend索引必须红。只采第一animDesc不能声称支持九blend；无证据参数不得硬猜。
- [ ] **N17-DEFAULT-ACCEPT / 接续视觉验收**：取得同demo/实体/pose/相机/FOV/viewport的TF2参考，先比较silhouette/关键骨点，再分mesh/LOD、材质UV定位剩余差异；注：默认开关已于 `fd25e93c` 翻转为开（`main.cpp:3331`），但视觉验收未过前仍不得以“开关已开”关闭本卡。
- [ ] **N17-FINAL-GATES / 合入前**：本轮camera-ui和overview在旧`666a1ac2…`分别PASS/231 PASS；最终`f341ab1e…`只完成定向资产、自测和四帧。按影响补最终相机/UI/双窗/ViewModel/旧书签与七图验证，再审全链；禁止挪用旧exe的通过。

- [ ] **N17-VIS / Scout视觉疑点**：固定 `analysis/integrated-up0.png` 对应demo/tick/玩家实体/userid/team/武器状态、相机/FOV、viewport和分辨率；报告测得屏外像素为0，因此“Scout被边缘裁切”不是已观察事实。先锁定已修正的世界投影，再用同录制实体/姿势的TF2参考比较silhouette、mesh/LOD、材质区域和骨骼姿态。
- [ ] **N17-CAMERA / 世界投影与ViewModel分离**：世界投影读Source水平FOV（4:3参考），转为DirectX垂直角并按每个pane自己的aspect计算；ViewModel FOV只控制武器。确认默认/显式worldFov、ini往返、相机书签恢复和多pane；本轮数值probe与隔离帧通过，最终候选仍须重绑相机/UI及受影响视觉门。
- [ ] 将Scout实际绘制逐层记账：MDL/VVD/VTX所选LOD、bodygroup/skin、mesh/tri/vertex与bounds；VMT/VTF/UV来源；骨架层级/权重/bind pose/该tick骨矩阵；VS最终顶点及屏幕silhouette。加权骨骼索引已审，helper影响见下；保证trace里的实体ID映射到参考里的同一玩家。
- [x] **N17-HELPER / 前臂helper蒙皮（未提交）**：Scout 23,442条LOD0绘制顶点中，524条引用无动画通道的左右前臂helper；旧代码留单位蒙皮，现按已有父骨骼当前变换生成`current * inverseBind`。数值夹具bind/translation/rotation全1，恢复旧行为时平移/旋转全0（rc=1），还原重建后全1；最终exe `a917b76b…`下两次锁内捕获rc=0。只证明父变换继承，TF2程序骨行为与完整Scout外观未证。
- [x] **N17-METRIC / XYZ统一尺度局部修复（未提交）**：旧upward Z相对XY放大1.351815；已统一world/entity/overlay/camera/fog/CPU诊断尺度。当前exe `666a1ac2…`上传MVP对等长线段为22.699882/22.699903px（独立基准22.700000），拉伸变异负控被拒；天空17/17、雾58断言PASS。此项不等于Scout外观、全地图或发布验收。
- [ ] **N17-METRIC-INTEGRATION / 坐标修复集成验收**：旧归一化书签/preset需要重新验证和明确迁移策略；复核split-view相机隔离、ViewModel对齐、HUD/overview和七图pin。先将画面差异归因到有意的FOV/尺度修正或独立缺陷，再讨论重钉；不得用重钉跳过产品回归。
- [x] **N17-IDLE / idle误选局部修复（2026-11-17，未提交）**：旧选择器实测Scout选`AttackStand_PRIMARY`(9)，改为WholeToken与首词元优先后选`stand_PRIMARY`(175)；九职业均选通用`stand_*`，`--matcher-selftest`的`genericIdle=1 oneShot=1`、探针与产品定向构建通过。该项只证明通用idle的选序正确，不证明截图角色命中fallback、最终姿态或外观与TF2一致；实体级draw/pose轨迹与同帧视觉对照仍须执行。
- [ ] **N17-SOURCE / TF2源码对照**：参考OthmanAba/TeamFortress2 revision `1b81dded673d49adebf4d0958e52236ecc28a956` 的`CTFPlayerAnimState::CalcMainActivity`、`TranslateActivity`及客户端动画更新；按观测到的demo字段实现并验证必要activity映射/叠加层。源码只作实现参考，不代替当前TF2资产及运行时对照。详见`docs/TF2-SOURCE-REFERENCE-INDEX.md`与`docs/N17-SCOUT-CAMERA-ANIMATION-2026-11-17.md`。
- [ ] **N17-ANIM-DEFAULT / 默认姿态链**：当前产品默认已于 `fd25e93c` 改为 `playerAnimationEnabled=true`（`main.cpp:3331`，原记 `false`）；`--player-animation --animation-drive states`会驱动姿态，但不是完整TF2 activity/gesture/pose/layer实现。以游标1已抓双路径；游标不是server tick，必须分别绑定`entryTick/serverTick/resolved scene tick`，不使用固定偏移。先将trace的entity、model、sequence/cycle、skin矩阵覆盖与draw range绑定，再以TF2同帧参考比较idle/run等类别；未通过前不得以“开关已开”视为验收通过，也不把汇总`boneMismatch`直接解释为映射错误。详见N17报告。
- [ ] **N17-PROBE / 探测器覆盖审计**：逐项审`entity_model_probe`、`model_pose_probe`、`player-model-check.sh`、`class-pose-family-check.sh`、`player-body-appearance-check.sh`、`world-entity-tint-check.sh`的输入、断言、变异、盲区。解析成功/meshBuilt、序列可解/变化、队色mask分别不是游戏内外观等价；现有tint门的379,038px主要证明颜色路径，不证明玩家Scout轮廓、UV或骨骼正确。
- [ ] 加同帧角色ROI验收：游戏参考与产品相机/实体配准；比较外轮廓、可见mesh/遮挡、关键材质区域和姿态。用至少一种故意错LOD/隐藏mesh、错UV或错bone映射的变异证明相应视觉断言会红，未能检出的属性必须明确标为UNTESTED；容差由重复采集和配准误差测出，不凭肉眼调宽。
- [ ] 按观测分支定位修复：错误LOD/mesh→模型选择与绘制范围；材质/UV→资源或坐标解析；静态姿态对、动画错→骨绑定/序列/时钟；仅屏幕位置错→相机。一次改一个已证实因素，修复前后同帧A/B并保留变异负控；成功后以Scout为模板扩九职业/双队样本。
- [ ] 九职业按 idle/run/turn/crouch/jump/fall/land/flinch/death-respawn/taunt/attack 建行，分输入事件、选序、骨骼运动、像素四层。
- [ ] `loop=false` 夹具已修，不再重复做；扩大真实 reload 观测密度，先确定是否能观测到动作再改产品。
- [ ] STV 缺 `m_nSequence` 等字段时明确数据边界；产品推断动画与服务器原始姿态不混称。
- [ ] 检查 duplicate entity ID、`walk` 命中 `airwalk` 等修复是否仅在其他分支；读取原实现和负控后最小集成，不直接搬泛化补丁。
- [ ] 源码对照顺序：OthmanAba/TeamFortress2的TF2玩家动画状态、model/class选择、skin/bodygroup、camera/FOV与材质调用点；NicknineTheEagle/TF2-Base作可读移植结构交叉参考；ValveSoftware/source-sdk-2013用于通用Source机制。记录具体commit/文件/符号，与当前本机MDL/VVD/VTX/VMT/VTF及demo属性逐项比对，不能把快照常量直接当成当前资产参数。

### N18　第一人称与武器全槽位（E + D）

- [ ] N17 Scout第三人称视觉样本闭环后扩九职业/双队/LOD/skin/bodygroup可见模型；第一人称arms/viewmodel和武器各自验收，不能互相替代。
- [ ] 单职业基线再扩九职业主/副/近战：model/arms/companion/material/team/sequence/骨骼父链和世界/ViewModel 投影。
- [ ] idle/fire/reload/switch/jump 等真实可得样本；FOV、near clip、roll、蹲站眼位等先测现实现，不照外部建议盲改常数。
- [ ] 第三人称挂接和第一人称动画分开验收；人物身体像素不代替武器像素。
- [ ] LOD1+ 属 legacy D34 的未实现限制，另找真实可见需求，设计选择/切换/回退与测试；不能用“模型有多个 LOD”声称已渲染。

### N19　网格/实例/纹理容量（E + F，legacy D04）

- [ ] 捕获三处已知武器 OOM 的 prepared/uploaded/rejected/drawn、实例/draw/纹理内存；明确瓶颈是哪一个预算。
- [ ] 从实测定义 player/held/attach/props 优先级与可观测溢出，不能只提高 mesh table 让“传得上但画不出”。
- [ ] 核查 world/attach/overlay key 命名空间和重复注册；容量修复与大重构分提交。
- [ ] 边界/溢出/恢复负控；保住原有非目标物件，不以静默丢对象换通过。

### N20　仅三类指定效果（E）

- [ ] Pyro 武器火焰、Medic 治疗束、流血分别核事件/附着点/资源/生命周期与时间驱动。
- [ ] 一例先实现，暂停/seek/目标消失能重建或清除；颜色/位置/出现区与 absent-event 负控都可见。
- [ ] 实际已有哪段实现先登记，未实现补实现；不扩一般粒子引擎或所有武器特效。

### N21　音频设备与恢复（音频负责人 + B，需人参与听感部分）

- [ ] 无设备/音频禁用路径可无人值守；真实设备试听需在场，记录输出设备、内容与 mute/volume 响应。
- [ ] pause/seek/倍速的时间同步，设备断开/失败恢复、缺 WAV 的降级和可读错误分别核查。
- [ ] M/`[`/`]` 字节/配置通过只证明控制输入，不证明“听见了”。

## 九、架构与工程：与功能并进，按写域串行集成

### N22　CI 与工具链便携（F）

- [ ] 查当前分支实际 workflow/CMake/preset 与 `env-msvc.sh`；硬编码机器路径是实现任务，不是无限等待授权的理由。
- [ ] 复用现有工具链发现方式或平台工具，允许显式配置覆盖；用当前产品与代表 probe 验证配置/编译/链接。
- [ ] 写入 Windows CI：configure/build、脚本语法、无私有 TF 资产的稳定快测；不把长 GPU/变异/发布全链塞进每提交 CI。
- [ ] 本地验证与远端工作流实跑分别记状态；推送另走发布窗口，不因无远端实跑就宣称 CI 已通过。

### N23　警告、静态检查与错误注入（F + B）

- [x] 输出 product/core/probe 的 `/W4` 覆盖矩阵与现有告警分类；分目标补，清理后再局部 `/WX`。（2026-11-18：55/55 目标 /W4、138 TU 零告警、`_CRT_SECURE_NO_WARNINGS` 作为唯一方言定义、7 个目标 /WX；覆盖矩阵与逐条修法见 `docs/N23-WALL-LEVEL-2026-11-18.md`，产品 exe `2e44c7e3…`；2026-11-19：干净树 `BUILD-IDENTITY=PASS assertions=222` head `aa8b0df0438d`，`/analyze` 51+30 TU、8 条告警、修 1 条 C6326，见该文档 §7/§8）
- [x] sanitizer/静态分析只选当前 Windows 编译器支持的组合，先 parser/资源生命周期代表目标，不凭配置文件声称运行干净。（2026-11-19：`/analyze` 已测 51+30 TU，见 `docs/N23-WALL-LEVEL-2026-11-18.md` §8；ASan x64 已装并取两条路径读数——install CLI selftest `PASS cases=4 checks=21 fail=0`、`demo_open_probe --scan` 25,130 包 `malformed_packets=0`，均 0 条报告；边界：model/material 与资源生命周期无读数，本机 `tf/` 无 `models` 目录，ASan 构建不在门禁里）
- [ ] WinMM/D3D 故障注入、resize 与有效控制挂到对应域窄测；格式/许可证策略单独提交，不混产品修复。

### N24　继续拆主循环与核心边界（F，和功能单写协调）

- [ ] 当前 `tf2_core` 已落地；先看真实 include/依赖/目标专有宏，补剩余重复编译，不把 core 一次改成全新分层框架。
- [ ] 选择 next seam：一个有明确输入/所有权/生命周期的 main 阶段；先提文件/接口清单，再原行为搬迁。
- [ ] 固定两 demo × 三 tick 比较相机、关键 trace/计数和图像；任何差异先归因，不用重钉洗回归。
- [ ] 实现功能和搬迁代码分开提交，主循环 owner 给出预约写入窗口。

### N25　门禁与证据效率（F + B）

- [x] D53/D55/D56/D59 的五脚本修复与部分拒绝分支自测已提交；真实整门禁 verdict 未取，修复入库与运行通过分两列记录。
- [ ] 顺序核验 `class-pose-family-check.sh`、`player-animation-check.sh`、`playback-error-paths-check.sh`、`animation-drive-states-check.sh`、`viewmodel-animation-check.sh`：先检查脚本实际 OUT 参数和输入要求，再安排一个有判别力的正控/负控；没有支持隔离 OUT 的脚本先做最小路径修复，不把正常写脏后的 exit 97 忽略或改为 PASS。
- [ ] 保存原始日志、退出码、exe/gate/input SHA、必要输出到仓外独立运行目录；禁止先清掉仅有的失败证据再说可复核。共同 helper 修复后选代表验证，其他脚本按影响面扩展，不每日跑完整发布链。
- [ ] 修复 stale blocked 注册说明、OUT 路径契约、wall-clock 计数精确 pin 等 legacy D12/D31/D32；一次迁移小组，不一口气改 50 runner。
- [ ] 为可复用帧定义输入身份：产品/资源/设置/demo/map/tick/实际相机/viewport；不能只靠文件 mtime 或输出文件名。
- [ ] gate 与产品分开身份：仅文档/门禁变更不自动导致 exe 像素重抓；改公共 helper 时选代表正/负控，再按影响扩展。
- [ ] 记录耗时/缓存命中/失效原因，避免“每天八小时验证”；缩小重复工作不等于省略关键负控。

### N26　构建/缓存/内存实测（F）

- [ ] 重新测 clean、no-op、单 cpp 修改三种构建，产品与 probe 分开；51 TU、137 actions、221s 是历史参考。
- [ ] 分析 IO/编译/链接/资产准备热点，再决定是否加缓存；至少重复测量并验证依赖变更真的失效。
- [ ] 多视角与资源预算的帧时/显存计量复用同套 telemetry，不为漂亮数字牺牲结果一致性。

## 十、集成、最终验收与落地

### N27　已裁决的可选音视频导出组件（D/E + F，范围边界由 A 维护）

- [x] 范围与组件契约已记录：首发包含有音轨的视频导出，不依赖用户自备 ffmpeg；MF 方案 C + 离线音频；组件独立清单，核心卸载遇组件拒绝，缺组件具名 reason + exit 13，见 `docs/N27-SCOPE-DECISIONS-2026-11-15.md` 与 `docs/N27-OPTIONAL-COMPONENT-CONTRACT-2026-11-15.md`。
- [x] MF 探针实测：本机 H.264/AAC、MP4 双流、RGB32 直入与独立进程成立；仅为可行性，非产品导出功能。默认 profile/起点规则与容器时间戳例外已记录。
- [ ] 按契约实现产品固定步长渲染、离线音频、区间选择、导出取消与磁盘/编码器失败恢复；组件未安装/安装/升级/卸载分别正负控，不把探针程序改名当产品完成。
- [ ] 用真实 demo 验证至少 30 秒音画同步、帧数/音频采样数、读回/编码/写盘吞吐与内存；确定性先比编码输入帧/PCM，再审解码结果，容器时间戳差异不可当画面回归，音轨存在也不等于声音内容正确。
- [ ] PIP/四视角、LOD1+ 保持明确的实现/后续里程碑，不以本轮未做暗中排除。

### N28　集成批次与证据继承（A + 各 owner）

- [ ] 每批先列合并依赖/共享文件/受影响证据，审查 diff，再 build 相关目标与窄门禁。
- [ ] 产品行为不变的重构要求相同固定输入输出；产品修复则用根因、旧缺陷负控、新正控解释差异。
- [ ] 分段集成回归只覆盖实际改变的域；记录历史证据继承依据，不给所有旧 exe 证据改标签当“当前通过”。

### N29　干净环境走查与冻结（A + B）

- [ ] 在另一受支持环境/干净用户目录验证安装→找 TF 根→开 demo→找 BSP/资产→播放→seek/跟随→音频→错误恢复→卸载。
- [ ] 七图 POV/STV、物件/天空/材质、人物/武器、UI/小地图、多视角和设备测试逐格复核；数据缺口/功能缺口/排除项分开。
- [ ] 已确认 P0/P1 修复或由用户明确接受影响；docs dirty 不可直接冒充发布净树。
- [ ] 冻结源码、gate、pin、素材版本与可重建的构建身份；冻结后禁止并行落新功能。

### N30　一次最终发布链与发布包（A）

- [ ] 前置 N29 完成后，在冻结候选上运行一次 `verify-release.sh`；不是日常开发任务，本次验收没有启动。
- [ ] 保存首个失败；定因后修候选，先跑受影响测试；只有候选改变且发布负责人安排新最终尝试时再跑整链。
- [ ] 完整链通过且未决事项处置后，准备二进制、版本/哈希、安装说明、七图/数据限制、控制方式、已知问题与恢复文档。
- [ ] push/tag/release 是后续明确发布动作；本清单的编写不等于发布授权或已经发布。

## 十一、建议下一批派工与交付

| 优先级 | 责任人 | 当前交付 | 可并行/必须串行 |
|---|---|---|---|
| P0 | A+B | N00 唯一台账和身份边界、N04 严重项分诊 | 先保护现场；只读可并行 |
| P1 | G | N01 f12 POV 和缺材质资源 | 与其他只读任务并行；不替换错误地图 |
| P1 | F | N02 Unicode probe、N22 工具链/CI 草案 | 实现不抢 renderer 写域；构建窗口排队 |
| P1 | D+B | N03 设置副本和可维护回归入口 | 公共 helper 单写；不重复整链 |
| P1 | D+B | N12-C 第二步精确 tick 消费与包顺序/身份负控；N12-A/B 已有有限样本 PASS，需随 C 集成重验 | `main.cpp` 相机写域同一负责人；QA 并行准备 packet fixture，产品构建与 GPU 采集串行排 hostlock |
| P1 | C | N05 两黑点、N06 每族一个具名实验 | 与资产盘点并行，产品抓帧串行 |
| P2 | D | N14 独立底图/比例、N15 DPI 与连续状态 | 与 N16 设计可并行；共享 UI 写入串行 |
| P2 | E | N17/N18 真实动作与武器样本、N19 容量实测 | 盘点并行，shader/model 与地图分窗口 |
| P2 | D+F | N16 双视角契约与首片实现 | 先契约后共享渲染器，不同时做四窗 |
| P2 | F | N23–N26 警告、下一抽取切口、实测提速 | 一次一个行为不变切口；CMake 单写 |
| 最后 | A+B | N28–N30 集成、净环境、冻结与发布 | 全员停止改冻结候选后再跑全链 |

任何任务拿不到特定素材时，继续其他不依赖它的卡；不能把某一行的 NOT VERIFIED 升格成整个项目停工。

## 十二、每项完成必须交的记录

```text
任务 Nxx / 负责人 / 旧缺陷映射（含原文档批次）：
分支、开始/结束 HEAD、既有脏文件保留情况：
改动文件与用户可见变化：
复现输入、命令、原失败结果、有效控制：
最小 build/probe/gate 命令、rc、断言/变异数量：
exe/sourceContent/sidecar 与 BSP/demo/设置/相机身份：
证据路径、SHA-256、实际执行时间：
结论 PASS / OPEN / NOT VERIFIED / UNSUPPORTED：
本条证明什么；本条没有证明什么：
失效与继承的旧证据清单：
剩余项、下一责任人、hostlock/进程/源码还原状态：
```

本次仅更新交接/任务文档并离线复核七图已有帧；未修改产品代码，未构建，未全链，未提交或推送。

## 十三、第二窗口完成标记（2026-11-15 10:55，HEAD `10f8e28`）

本节只追加标记与证据，不改上文任何判定；详细报告见 `docs/TASK-REPORT-2026-11-15.md`。
图例：✅ 完成（带边界）／◐ 部分／⛔ 阻塞／○ 未做。

| 任务 | 本轮状态 | 关键证据 |
|---|---|---|
| N12-A POV eye = origin + viewOffset | ✅ | `full90` 三帧全 PASS（A1/A2/A3），eyes 行 `offsetZ=75.0001/75.0001/68.0001 dz=+75.0000/…`，exe `90f781b5…` |
| N12-B STV crouch eye | ✅（有限） | `stv-t40000` 45.000043（距 68.0 常量 23 u）；`stv-t20000` 68.000065 与常量只差 0.0001 u ⇒ COINCIDENT/NOT VERIFIED，不计绿不计红 |
| N12-C stale snapshot selection | ◐ | BOUNDED-STALE；三步骤（tick 轨道 → 消费端 → 解码扁平分量）落地且行为变更已被裁决接受；SetView/PacketEntities 次序仍 0 样本，STV 无导播真值 |
| N13 第一步（命令/状态 + `follow:` 行） | ✅ | 提交 `4bf830a`；exe `6d15c566…` 3,010,048 B；A8 按 tick 归键全绿（eye row 387 tick、eye 387 key 0 差分、帧 md5 三态同、`follow` 0 vs 5、probe 两 sha 与 N12 §17.4 同）；边界：A1–A7 未验、相机优先级未改 |
| N25 门禁 | ◐ | gate2 `PASS` 67 s / 235 OK；gate4 `PASS` 4002 s / 20 OK / `READINGS=PASS`，四条负控 `controls=4/4` 具名原因，空 OUT `rc=2`；gate1 沿用真绿；gate3/gate5 ⛔ 缺录制；**12 道被算术清扫改过的门禁真实整轮未复跑** |
| 嵌套算术清扫 | ✅ | 14/14 修复（12 个脚本）+ `tools/check-nested-arith.sh`（`--selftest` PASS、`sites=0`、旧版回放 14/14） |
| N06 分组交付 | ✅（边界：同机位对照未做） | `docs/N06-GROUPING-2026-11-15.md`（`ed419f6`） |
| `.gitattributes` | ⛔ | 策略已裁决，改动仍未提交，等负责人确认 |

本轮完成的两个「收口」事项：**N12 覆盖率不再是 NOT VERIFIED 的一整片**（A2/A3/B2 有样本了）、
**gate4 自带负控首次能按预期红并报出具名原因**。仍然 NOT-YET 的：N05 根因、N13 A1–A7、
N14–N20、N22–N24、N26、N27 实现、N28–N30。
