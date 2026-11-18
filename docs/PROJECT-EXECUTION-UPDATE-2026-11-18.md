# 全项目任务清单与连续执行顺序（2026-11-18）

本文件是 11-18 当前增量裁决，与主清单 N00–N30 使用相同卡号；优先于旧快照中的“当前”“未提交”“未上传”。旧记录保留其日期与证据身份，不覆盖历史读数。功能、架构、验证三条线共同推进；子项通过只关闭该子项。

## 1. 已核事实与本轮交付

| 项目 | 验收与边界 |
|---|---|
| 本地开发基线 | `t105-integrate` / `work/integrate`，开工 HEAD `3e3f47f3`，工作树 clean |
| GitHub 现场 | 开工实查远端 main/tag `snapshot-2026-11-18` 均为 `98ddaeba`；远端 integrate 为 `30a2fcfb`，未包含本地最后一笔文档提交。后续推送结果须另记，不能把本地 tip 当远端事实 |
| main 快照 | fetch 后核实 788 个 tracked 文件、evidence 文件 0；`git diff origin/main 3e3f47f3 -- native` 无差异。仅证明该时点产品源码一致，不证明门禁可在缺 evidence 的 main 上直接运行 |
| 四项世界修复 | `daa607e0` 已进入产品源码；报告提供手性/绕序/铁丝网读数，atlas mip 行的实测栏仍为空。本轮未复跑这些帧，不认“四项全部有新 exe 运行证据” |
| 本轮实际修复 | 已提交 `61b0dbfc`。companion 合并丢循环标记：Scout 修前 75 mismatch、循环片段误停末帧；修后九职业 2516 条一致、18 跨周期样本及18反向变异通过，pose-family 40/40、定向产品构建通过。详见 [N17循环修复](N17-COMPANION-LOOP-2026-11-18.md) |
| 构建身份 | 本轮产品 `8b1ab1f0…677641`，probe `59d38b95…6f1a23`；均为提交前构建。完整哈希、命令和局限见修复报告，不叫发布 canonical |
| 未关闭 | 默认 bind pose、Scout完整外观、世界残项、14格七图签收、UI/多视角、3D天空、导出与发布链 |

## 2. 仓库与工作区决策

1. **保留现有历史，不重写公开分支**。只要 archive 分支仍引用旧 blob，完整 clone 仍会获取这些对象；“移到 archive 就降到20MB”不成立。普通删文件也不缩历史。
2. 代码阅读建议 `git clone --depth 1 --single-branch --branch main https://github.com/elo6371/tf2-demo-player-native.git`；该命令减少历史，不承诺网络包体恰等于文件树23MB。
3. 开发继续 integrate；普通代码/文档更新采用显式文件提交和正常 fast-forward push。main 是具名快照，不强推、不自动滚动 tag、不顺带发布。
4. 大帧、trace、变异产物继续放 scratch；签入紧凑结果、哈希、fixture身份与复现命令。新证据若不在 main 分发，门禁必须给出获取/生成方法，缺输入不能假 PASS。
5. 下列是**待接回线索**，不是新任务派发，也不是分支已验收；先看未提交 diff，保留原工作者改动，不整目录拷贝。

| 已有工作线 | 已查身份 | 接续步骤 |
|---|---|---|
| `work/n17-anim` / t105-n17anim | HEAD `f7b40844`；另有8项未提交改动 | 已提交部分自述为 parked、未编译；未提交报告称 idle时钟+序列间混合，有 `[[STATES]]` / `[[PIXELS]]` 占位。先收正式提交/日志，区分自定义混合与真正9/12-way pose；合入时保留本轮循环位修复 |
| `work/n17-draw` / t105-n17draw | `1f625c39` | 审 bodygroup、未贴图range、FOV自检/逐实体诊断，编译并跑仪表开关控制，再按最小依赖提交接回 |
| `work/world-cull` / t105-cull | `44d67276` | 核 authored normals/crack gate 与 integrate新手性变换的兼容性；以同机位和背面正负控重验，不直接宣称pr600已修 |
| `work/qa-gates` / t105-qa | `b922677c` | 逐项分离门禁修复、输入替换、环境结论；对应最终exe重跑受影响项，不整体继承旧 PASS |

## 3. 首发范围

| 地图 | 必需验收 |
|---|---|
| cp_process_f12 | POV + STV |
| cp_sunshine | POV + STV |
| cp_gullywash_f9 | POV + STV |
| cp_sultry_b8a | POV + STV |
| koth_product_final | POV + STV |
| koth_bagel_rc11 | POV + STV |
| pl_upward_f12 | POV + STV，含推车 |

`pl_upward` 与 `pl_upward_f12` 分开记资源身份；前者不是后者的等价pin。Odyssey不在首发范围，其旧几何洞门独立记历史阻塞。水面基础外观要正确，玻璃/水反射折射、光影/阴影一致性暂不做；粒子仅 Pyro 武器、Medic治疗枪、流血。小地图先研究 demos.tf 源码，可安全实际复用的优先；若另有更合适方案写出比较与选择依据。

## 4. 可一次分配的队列

下列 owner 是责任域，用户可交给不同执行实例；本轮未创建聊天或通知其他人。每位执行者同时只做一片实现；完成即接下一片，独立输入/源码阅读可并行，产品构建与GPU/设备/变异运行经共享hostlock串行。main、renderer、CMake的共同写域要先声明。

| 队列 | 顺序 | 第一片与完成条件 | 后续触发 |
|---|---|---|---|
| A 角色/武器 | N17现有WIP审验 → N17默认动画 → N18 → N19 | 先取anim/draw实际提交与日志；编译，复跑循环检查+固定帧/错误骨骼负控；记录Scout同实体相机/LOD/bodygroup/UV | 数值与最小像素证据成立再合入；缺TF2参考只阻塞视觉签收，不阻塞独立采样实现 |
| B 世界 | N05/N06 → N07 → N08/N09 → N10 → N11 | 接回cull最小片，先判新手性下pr600实际剩余像素；同时补atlas mip的远景A/B与材质边界串色检查 | 各缺陷定因并小修后复跑对应固定ROI；不靠关cull/改pin清红 |
| C 相机/交互 | N12relay → N13 → N14/N15 → N16 | 先精确tick relay与身份/包顺序样本；再选人/follow命令；双pane独立玩家与视角 | 双pane正确后PIP，再四pane；同步播放时钟，输入归选中pane，音频焦点需明确 |
| D 架构/效率 | N25 → N26 → N22/N23 → N24 | 两个代表gate验证捕获复用及失效；实测no-op/单TU/公共头依赖，再按测量修构建热点 | 功能各片推进时持续执行，不等地图或Scout完工；一次只抽一个有证据的主循环切口 |
| E 音频/导出/集成 | N21 → N27；就绪小片 → N28 → N29/N30 | 拆payload丢弃/解码失败计数并验证守恒；MF能力探针接产品入口，帧时钟与音频按tick离线推进 | 前置齐再冻结最终候选，只安排必要的一次完整链；失败返回owner最小修复 |

单人顺序：现场/锁 → 已复现小缺陷（本轮循环位已完成）→ 接回N17可验小片 → N25/N26一片 → 世界cull/mip → N12/N13 → 再循环推进；不能把架构永远排在功能全部完成之后。

## 5. N00–N30 全量接续表

“有限完成”保持原卡边界；新exe未复跑不抹掉旧证据，也不能算新exe通过。每行按“先做→再做→验收”执行。

| 卡 / 状态 | 详细接续与验收出口 |
|---|---|
| N00 有限完成，持续维护 | 登记唯一缺陷ID、demo/map/hash/tick域/产品与gate身份；每批追加累计台账和未完成项。禁止用报告篇数作产品完成率 |
| N01 部分 | 清点现存POV/STV及同机位TF2参考；缺失只阻塞依赖案例。输入替换须同类型/用途、重新测值+保留负控，样本覆盖缩水明确列出，不能用无关图替首发图 |
| N02 有限完成 | 保留Unicode/路径既有结果；新入口补中文空格路径、缺输入、不可写输出的具名非零失败；其余探针D44按触达面治理 |
| N03 有限完成 | 所有新运行双settings副本、scratch OUT、锁/进程清理；开关与失败分支不得改tracked evidence；新增writer先做污染负控 |
| N04 有限完成 | 八类安全检查结果保留；新解码/资源实现沿用fail-closed和边界夹具，不因门禁统计变绿容忍部分坏网格 |
| N05 未闭环 | 先重确认Sultry 849/873/919的pass/face/triangle来源；使用已落诊断与隔离，不重复旧假设。若VS差异仍可复现，SO/调试回读测真实GPU clip，再分裁剪/深度/覆盖；一次只关闭有证据的根因 |
| N06 部分 | pr600以新手性与world-cull候选重测；14退化位移面逐个列几何来源/回退原因，修合法面；白噪/黑区/蓝面按材质族同机位A/B。atlas mip需远景质量、atlas边缘和性能实测 |
| N07 部分 | 使用产品具名材质链，覆盖BSP pak、TF/HL2挂载顺序、IncludeReader；零缺VMT不等于画对。补water分类/损坏资产控制；按具名道具/材质做UV、贴图、noCull（实际需要时）可见签收 |
| N08 部分 | 控制点对象→状态netprop→skin/bodygroup→实际draw；选至少两种占领状态与切换，验证暂停/seek/恢复，禁止写成“控制台” |
| N09 部分 | Upward推车模型/轨道/动态brush，至少直道/弯道/暂停/seek；用实体身份、变换与帧证明移动，不能只列资源存在 |
| N10 部分，3D天空已实装未推广 | 保留六面天空有限PASS；真实sky_camera origin/scale 与 3D skybox 独立 pass 已实装（`--world-sky-3d`，默认 off，off 帧逐字节等于基准 `30315f8f`；on 与 off 差 94 像素）见 docs/WORLD-SKY3D-2026-11-18.md；剩：第二张图正例、远离天空相机机位的抓帧、blended 顺序逐像素归因、trace cluster 断言；不以普通天空覆盖代替3D天空 |
| N11 未达成 | 目前14格只有2格合格；先完成主要修复，再在一个最终候选补七图POV/STV。每格同资源/视角/tick身份、判词/rc/图像/剩余缺陷，缺输入明确BLOCKED |
| N12 部分 | 保留POV眼位独立oracle；relay消费精确tick轨道并记录resolved tick/age/entity生命周期；包内SetView/PacketEntities顺序须有可判别样本，不能用0样本判安全；站/蹲/死亡、seek各验 |
| N13 未达成 | 从已有UiCommand/相机优先级契约出发实现选人/follow/模式命令及可见入口；本地/被跟随/观察者身份明确；A1–A7逐项实测，不只加按钮 |
| N14 部分 | 研究demos.tf许可证、坐标/底图数据与依赖；写复用/自研决策；至少三个非共线独立地标验朝向/比例，玩家图标与总览切换，不能用单CP自证 |
| N15 部分 | DPI、resize、最小窗口、HUD遮挡按运行帧验；再暂停/seek/全屏切换，验证坐标输入和资源恢复；数学布局PASS不替代真实遮挡结果 |
| N16 契约完成，功能未完成 | 共享已解码场景/播放时钟，每pane独立camera/player/mode；先双pane点击焦点切第一/第三/自由，再PIP拖动/大小/DPI与四视角性能；不可每pane重解整个demo |
| N17 部分，本轮循环位已修 | 审anim/draw WIP→idle周期和姿态连续性→真实sequence内pose参数/blend→同机位模型层次验收→默认动画策略。自定义idle/walk/run组合不冒充Source9/12-way混合；固定首帧/错bone/错LOD/错UV负控验证探针能力 |
| N18 未闭环 | 按职业/槽位验证arms和weapon的模型、骨合并、skin/bodygroup、独立FOV/深度/镜像；POV没有v_*实体不代表没有应绘制武器；切换、开火、reload用可判别片段 |
| N19 未闭环 | 当前候选重跑网格/实例/纹理容量与key命名空间；OOM要具名失败/回退且资源清理，记录内存峰值与响应时间，不只读静态上限 |
| N20 未实现 | 仅实现Pyro武器、Medic治疗束、流血；从demo事件/实体属性驱动，以tick确定进度，暂停/seek可重建；各给存在/不存在/结束帧和身份，不扩其它粒子 |
| N21 部分 | payload上限导致的丢弃与真实解码失败分别记账，保留声明/保留/解码/匹配守恒；无设备/设备故障注入+恢复可自动验，真实听感留人工项，不把统计修复称恢复丢失音频 |
| N22 部分 | 代码已公开不等于CI已跑；检测MSVC/SDK与路径，移硬编码；本地代表构建→Windows CI代表目标→干净机复现；缺工具具名失败，保存远端run URL/commit。**tier-1 workflow 已落地**（`.github/workflows/tier1.yml`，5 步：checkout/eol guard/toolchain/`verify-fast.sh --mutation`/artifact；触发面 `main`+`candidate/*`+PR+dispatch，**不含 `work/*`**，所以推这一提交不会自触发）；草案遗留的 43 vs 44 已核清（10+8+7+7+4+7=43，草案把 `E_MODEL_POSE` 的注释行当成条目，`verify-fast.sh:80` 的 pin 是对的）；**远端仍未跑**：`gh` 未登录、匿名 API 403 限流，读不到 run 结论，见 `docs/N22-CI-TIER1-LANDING-2026-11-18.md` |
| N23 部分 | 先重新量当前/W4覆盖（旧6/54不是现值）；按目标清越界/生命周期/返回值警告，新增目标不得退化；D3D resize/device/WinMM失败注入；不全局禁警告换绿 |
| N24 方案，待实施 | 先量当前main调用/状态所有权；按相机/pane边界选一个现有可独立职责做原行为搬迁，功能修复另提交；两demo×三tick的相机/关键trace/图像不变才接下一片，不新增泛用框架 |
| N25 部分 | 列gate输入/输出/耗时/依赖；产品与gate脚本身份分开，capture key含exe/demo/map/资源/设置/tick/实际相机/viewport；先两个代表复用与失效负控，再推广；空判词/rc丢失/写脏必须红。**已授权的.gitattributes行尾治理已落地**（`07d8ca7d`：`native/{src,include,tools,cmake}`、根门禁脚本、`tools/` 钉 `text eol=lf`；502 个文件本来全 LF，fresh checkout 负控 1610/16313/106 CR → 0；见 `docs/LINE-ENDINGS-2026-11-18.md`），仍留：markdown 不动、`.ps1` 未裁、程序自写 CRLF 不管 |
| N26 待实测 | 同机测clean/no-op/单TU/公共头；记录编译/链接/IO/动作数与exe哈希，验证tf2_core实际复用及头依赖；先修实测瓶颈，再考虑缓存，不凭旧时间猜优化 |
| N27 契约/能力有证据，入口未完成 | MF SinkWriter接产品离线视频+音频，固定tick帧时钟、输出失败/取消/组件缺失契约；Finalize后若需byte-identical，正确解析容器时间字段归一化，再跨进程比mdat/解码帧/音频/容器，不写固定字节偏移；长片音画同步另验 |
| N28 部分 | 依赖最少的就绪小片逐个集成；每片记冲突/构建/影响门/身份，旧分支PASS保留历史不转移；本轮main是快照，不替代最终集成候选 |
| N29 阻塞，前置未齐 | 具名完整候选+首发矩阵+安装/资源清单→独立干净环境走查→冻结源码/资源/pins；缺环境精确列人工步骤，不伪造完成 |
| N30 未完成 | 前置全部齐才跑最终verify-all/verify-release，保存失败/修复/最终身份；发布需另有明确安排，功能快照tag不等于发布验收 |

## 6. 失败后的自动处理与效率规则

1. 先保存实际命令、rc、最小日志、输入及exe/gate身份；判断产品错误、仪表错误、输入缺失或环境错误，再决定下一项测量。
2. 仪表先做正负控；逐像素比对要先核pass/相机/FOV/viewport。CPU预测与另一工具共享的假设不得冒称独立证据。
3. 已复现逻辑错：按固定revision源码和本地资产查因→最小改动→原复现转绿→保留变异转红→受影响验证；不反复请求“能否再加字段”。
4. 同一环境连续两次失败停止该环境路线，保留两次错误原文/尝试/需人工事项，继续独立队列；不为绕环境改正式逻辑。缺同机位参考不阻塞其它可独立验证项。
5. 诊断60–90分钟无区分性新证据，做检查点并换测量或独立卡；不是超时就把缺陷改成“已知误差”。
6. 每个提交只跑影响门；头文件或共享renderer变更扩相关面。无需每步全量54目标/2小时整链；最终候选再跑完整链。
7. 每批交付：改了什么、原始证据、没证明什么、未完成/阻塞/接续队列；追加 `D:/_shared/TASK-COMPLETION-LOG.md`，同时向用户输出对应清单。

## 7. 必须查阅与记录的参考网站

开卡先按域上GitHub看**实际源码**，记录URL、revision、路径/符号、结论和本机差异；新方案至少与项目现有实现比较。只有经最小复现验证的可复用问题才写 coding problem；不得把猜测写成已解决。更多网站和限制见 [完整参考索引](TF2-SOURCE-REFERENCE-INDEX.md)。

| 参考 | 使用要求 |
|---|---|
| https://github.com/OthmanAba/TeamFortress2 | TF2专项优先参考之一，动画/相机/武器/材质先读；本轮固定 `1b81dded673d49adebf4d0958e52236ecc28a956`；不是“全球最全”或当前游戏真值 |
| https://github.com/ValveSoftware/source-sdk-2013 | 官方SDK交叉核引擎/TF2契约；本轮固定 `e1f1fd42271805072f052051c26c21caa2c87b04`，确有TF2代码 |
| https://github.com/sr2echa/TF2-Source-Code | 次级镜像定位调用链；同源镜像不算独立实现 |
| https://github.com/SteamTracking/GameTracking-TF2 | 核发布资产/脚本更新，再核本机实际文件 |
| https://github.com/demostf/parser 、 https://github.com/demostf/demo.js 、 https://github.com/PazerOP/DemoLib2 | 网络/眼位独立对照，expected不能由产品生成；明确tick与生命周期 |
| https://demos.tf/ 、 https://github.com/demostf | 小地图开卡先定位对应公开实现与许可证；安全可复用优先，另选方案必须写比较，不把整站依赖搬进native |
| https://github.com/pySourceSDK/ValveBSP 、 https://github.com/Galaco/bsp 、 https://github.com/tasbox-org/source-parsers | BSP/VMT/VTF/pak约定；产品展开face与BSP dface不能混用 |
| https://developer.valvesoftware.com/wiki/StudioMDL 、 https://developer.valvesoftware.com/wiki/Model_Viewer | 模型/骨骼/序列/资产查看，运行画面仍需配对验证 |
| https://developer.valvesoftware.com/wiki/Material 、 https://developer.valvesoftware.com/wiki/Valve_Texture_Format | 材质与贴图语义，先按实际shader/flags查，不照搬泛化修复 |
| https://developer.valvesoftware.com/wiki/Source_Multiplayer_Networking | 网络时钟/插值背景，不代替具体demo对照 |
| https://github.com/elo6371/coding-problem-playbook | 开发前pull并rg相关Markdown；验证解决后写 problems/日期条目、更新INDEX、独立commit/push；没有SCHEMA时如实用现有条目结构 |
| https://github.com/elo6371/tf2-demo-player-native | 开工核远端与本地，不把本地提交当已上传；正常提交/推送开发线，验证源码和文档链接，不重写公开历史 |

## 8. 本批没证明什么

本批完成一个真实采样修复和全项目重排；没有收口整张N17、没有合入其它工作区WIP、没有补齐14格七图、没有实测mip修复画面，也没有执行最终发布链。上文任务包是可交接队列，不是声称已启动多个执行者。
