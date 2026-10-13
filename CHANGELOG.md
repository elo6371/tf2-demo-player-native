# 更新日志

## 2026-10-13

### T1：当前渲染档复核（已整合，正向 8/9）

- 整合 `task/render-current@8ddab34` 的报告与 `evidence/render-current/`；本项没有产品源码或门禁判据改动。
- `bash verify-render.sh` 实测 `VERIFY-RENDER=FAIL (1 red)`、`gates_ok=8 gates_bad=1`；唯一红项是 `viewmodel-contract` 的测量抓帧点断言。
- 复采 3/3 确认 T3 音频接线改变首帧速度，使部分探针落入 10253--10267 标定盲区；未修改判据掩盖失败。
- `bash verify-render.sh --mutation` 实测 `VERIFY-RENDER=PASS (9/9 gates, mutation=1)`；变异源码已恢复，完整 `verify-all.sh` 按约束未运行。

### 主线整合状态

T1、T2、T3 已整合并推送至 `p0-entity-protocol@69baff8`。T3 来源为 `task/audio-main-wiring`，产品提交 `baf93e7`、门禁/证据提交 `61260a0`；合并后 `verify-fast.sh` = `VERIFY-FAST=PASS (assertions_ok=43)`、rc=0。T3 专项 48 条断言和 12/12 mutation 在来源 worktree 通过。当前 HEAD 的渲染档及完整总链未验证；真实设备声音输出未验证。下方「尚未整合」字样均为来源分支交付时的历史状态。

### T3：音频主程序接线（`task/audio-main-wiring`，**尚未整合**）

- 产品提交 `baf93e7`，门禁/证据提交 `61260a0`；基线 `origin/p0-entity-protocol@9a8387b`；工作区 `D:\TF2_Native_Worktrees\audio-main-wiring`。
- **声音事件现在落在录制自己的 tick 原点。** `readSounds()` 按事件到达的 entry 打 tick（`demo_header.cpp:2184`），而一份 demo 有两套 tick 空间：signon entry（command 1）带服务器绝对 tick（POV 29368/29438/29456/29488），packet entry（command 2）带 demo 自己的 0 基计数（POV 0..39122；38558 条里 28932 条低于 29368）。旧原点取第一条 command-1/2 entry —— 也就是 signon —— 于是 `max(0, tick - origin)` 把整段录制折到相对 tick 0：POV `clampedToZero=5555/5563`。这就是清单四遗留的批量 flush。
- 修复形状：新增 `firstRecordedPacketTick()`（第一条 command-2 entry）与 `soundEventRelativeTick()`；后者把差算成 `int64`，**负值或超出 int32 一律拒绝并计数**，不再夹取。POV 换原点后 `clampedToZeroPacketOrigin=2`、`unplaceablePacketOrigin=0`；matcha 为 0/0。`firstPacketTick` 保留给实体历史查询（`sceneTick`），声音时间线改用新的 `soundOriginTick` —— 两者混用曾把 scene 清空（`scene: snapshot status=3 stale=0 states=0`）。
- 新增产品 trace：`audio: timeline`（read/scheduled/unresolved/unplaceable/refused/kinds）、`audio: summary`（`soundEventsRead/scheduled/played/skippedVoice/missingResource`、`firstPacketTick`、`soundOriginTick`、`reverseRecoveries`、`duplicateTicks`）、逐 tick `audio: advance tick= cursor= seen= dup=`。`scheduleSoundEvent` 让 voice/announcer/music 事件**留在时间线里**并计数，`soundEventsSkippedVoice` 因此能读到 34，而不是一个恒 0 的计数器。
- 定向验收：`verify-audio.sh` = `VERIFY-AUDIO=PASS (assertions_ok=48)`（含构建，`build_rc=0`）；`verify-audio.sh --mutation` = `VERIFY-AUDIO-MUTATION=PASS`（12/12 捕获）。新增 `sound_timeline_census_probe` 与 `evidence/audio/timeline-inputs.txt`（两份录制按 `size + sha256` pin 住）。
- 未验证：`applySoundDelayTicks` 的负延时夹取（POV `delayMin=-39.72s`、2694 条负延时 → 原点修好后仍有 746 个事件落在相对 tick 0）是**既有解码行为**，本轮没有改（SDK 参考 `soundinfo.h` 不在公开 `source-sdk-2013` 里）；真实设备播放仍需人工执行，不在门禁内。

## 2026-10-12

### T0：渲染档 9/9 已在整合主线补跑（正向与变异各一次）

- 补上 README 记载的缺口「`verify-render.sh` 已扩展为 9/9，尚未在主线上重跑」。工作区 `D:\TF2_Native_Worktrees\render-gate-recheck`，基线 `9a8387b`。
- 正向 `bash verify-render.sh` = `VERIFY-RENDER=PASS (9/9 gates, mutation=0)`，退出码 0，1h11m29s（全新 worktree，从零 configure + 全量构建，没有构建缓存）。
- 变异 `bash verify-render.sh --mutation` = `VERIFY-RENDER=PASS (9/9 gates, mutation=1)`，退出码 0，1h34m12s：gates 1–4 各自扰动被捕获，contract 5/5、animation `mutation_red=3/3 hold=4/4`、companion `caught=6 of 6`。
- 九个门禁正向：resource-reachability / frame-capture / entity-material / weapon-animation / viewmodel-contract(154) / viewmodel-animation(139) / viewmodel-companion / material-chain(12/12) / evidence-tree。
- 证据新鲜度：本轮全部抓帧与已提交见证帧逐字节一致（paused `a61a3e4d…`、bagel `fa1bb194…`、snakewater `d086695e…`、entity-material `86b680bb…`、weapon-animation `7bc06bc7…`/`2ed0add0…`）；两次运行前后 `git status --porcelain` 均为空、未产生未跟踪文件、锁全部释放、变异源码已恢复。
- 未做：`verify-all.sh`（24 步，本任务边界明令不跑）。`verify-all.sh:876` 的 `VM_ANIM_OK` 仍钉 111、渲染档实测 139；第 23 步 mutation 的 hold 由 3/3 变为 4/4（`4f43560` 把猜测值改为实测配对）。两条 pin留给总链重跑者更新。
- 本轮不改产品源码、不改门禁判据；提交只含证据与文档。记录 `T0-RENDER-RECHECK-2026-10-12.md`，证据 `evidence/render-gate-recheck/`。

### T2 第二笔：静态武器本体独立 draw request（已整合）

- 分支 `task/weapon-body-render`，基线 `origin/p0-entity-protocol@9a8387b`，工作区 `D:\TF2_Native_Worktrees\weapon-body-render`。产品在手臂之后新增第二笔独立 draw request：武器实体的 `m_iWorldModelIndex` → `c_*` 武器本体模型，独立 mesh/radius/near-far/projection，与手臂分别计数；拒绝时清空旧武器槽位，不留过期 mesh。
- 定向验收：`bash build-target.sh tf2_demo_native weapon_body_probe` rc=0；`bash weapon-body-check.sh` = `WEAPON-BODY=PASS`（61 条断言、0 FAIL）；`bash weapon-body-check.sh --mutation` = `MUTATION-CAUGHT=PASS (4 defects)`（104 条断言、0 FAIL）；`bash verify-fast.sh` = `VERIFY-FAST=PASS (assertions_ok=43)`、rc=0。
- 关键读数（POV `autorecord_2026-07-02_13-26-46.dem`，tick 2000）：武器实体 1008 `CTFSniperRifle` → `m_iWorldModelIndex=533` → `c_pro_rifle.mdl`；`armDrawCalls=weaponDrawCalls=1379`、`weaponMaterials=2/2/2/2`、`weaponVisible=1`、`weaponVertices=12357`、`weaponFallback=-`；`byte_diff(on, off)=42209`、`byte_diff(on, on-repeat)=0`、`byte_diff(on, visible)=0`；`weaponBody model=` 与 `viewmodel request path=` 各 1 次（非每帧重建）。
- 变异（每个都有具名读数与具名 control）：m1 读 `m_nModelIndex` 代替 `m_iWorldModelIndex` → 读数报 1032、拒绝为 `worldModelIsArms`、不绘制、帧与 `body-off` 逐字节相同；m2 删除 arms 比较 → 探针 8 条命名断言变红而产品读数不变（control，正常路径不经过被删检查）；m3 删除第二笔提交连同其计数 → 计数分离（0 vs 1370）且帧与 `body-off` 逐字节相同；m4 材质链短路 → 8 条断言变红。四个变异全部捕获，源码逐字节恢复并重建成功。
- 像素级闭合：`body-off.bmp`、`mutation-m1.bmp`、`mutation-m3.bmp` 的 sha256 相同（`1d2cab7d…`）——"拒绝"、"关闭"、"提交消失"画出同一画面。
- 顺带修复的产品缺陷：arms 比较原先按原始字符串比较，反斜杠与大小写变体可绕过同一模型的判定；`normalizeModelPath` 提升为 `model_loader.h` 的共享 inline，三个翻译单元共用一条规则（该缺陷由探针自检首次运行抓出）。
- 未验证：人工画面对照、武器切换/多武器场景、`verify-render.sh` 未在本分支重跑、总链未跑、真实 GPU 性能。抓帧落点按请求 tick 对齐：`--capture-tick N` 的"≥ N"语义会让落点漂移，本次两次运行共 5 次漂移到 2001 并全部由重试纠正。

### T1：ViewModel 材质接入产品 draw pass（已整合，待定向验收）

- 来源分支 `task/viewmodel-material-render`，基线 `origin/p0-entity-protocol@9a8387b`，已整合到当前主线。把已验证的 MDL → `$cdmaterials` → VMT → `$basetexture` → VTF → BC1/BC3 像素解码接进实际 ViewModel draw pass。
- 新增 `native/include/viewmodel_material.h`、`native/src/viewmodel_material.cpp`（材质表、状态词表、解码遍历，探针与产品共用）；`native/include/model_loader.h`、`native/src/model_loader.cpp` 增加 `ModelMeshMaterialTable` 与 `decodeMeshMaterials()`（从 bodypart/model/mesh 表解出被绘制 mesh 命名的槽）；`native/include/viewmodel.h`、`native/src/viewmodel.cpp` 把材质表挂到 `ViewModelRequest`；`native/include/native_renderer.h`、`native/src/native_renderer.cpp` 增加 `ViewModelDrawRange`、按规范化 VTF 路径索引的纹理缓存、每帧 range 循环与 `$translucent` 混合状态；`native/src/main.cpp` 接线、新增 `--viewmodel-material-slot` / `--viewmodel-material-missing-slot` / `--viewmodel-flat-color` 与九项稳定读数；`bmp-pixel-diff.py` 新增 `--region`；`viewmodel-material-check.sh` 新增产品段落 6--9 与 4 条产品变异。
- 修复接线缺陷一处：`reloadGpuResources()` 原先直接上传 ViewModel 网格并占据缓冲区归属，使场景更新里的材质 pass 被 early-return 跳过；因此 `--viewmodel-render command` 路径下材质从未绑定，覆盖开关被静默忽略（`slotOverride=-1`）。改为经同一材质 pass 上传。
- 关键读数（长跑 149 帧）：`materialSlots=3 materialResolved=3 fallbackSlots=0 texturesUploaded=2 texturesReused=149 boundSlots=1 drawRanges=1 fallbackRanges=0 phongSlots=3 bumpSlots=3 selfillumSlots=0`，`drawnSlot=0 source=mdl-mesh-table`、`drawnModel=c_sniper_arms entityModelMatchesDrawn=1`。`texturesReused == rendered_frames`，`texturesUploaded` 不随帧数增长。
- 像素证据：`--start-paused --capture-tick 0` 冻结世界（同设置两次运行逐字节一致），纹理态与纯色对照相差 `1.4295%`（`max_delta=186`），ViewModel 之外三条条带差异为 0；同几何换第二张真实纹理相差 `1.4111%`（`max_delta=120`）且同样只改 ViewModel 区域；注入缺失槽后该帧与纯色对照帧逐字节一致。
- 门禁：`viewmodel-material-check.sh` 220 条断言通过（原 159 条资源断言 + 61 条产品断言）；这是在修掉比对工具的整帧假绿后重跑的结果，真判定与首轮的"220"不是同一件事。`--mutation` 十个变异**逐个**捕获（全量重跑 9/10，第 10 条 `texture-rebuilt-every-frame` 被新加的陈旧-capture 守卫中止——沙箱删除配额耗尽、宁红勿假——配额恢复后单独重跑捕获），源码与夹具均还原。
- 本轮修掉的仪器缺陷（都是 mutation 首轮暴露出来的，不是产品缺陷）：`bmp-pixel-diff.py` 的 min/max 判定原先只在 `--region` 循环内执行，整帧条目被跳过，故**所有无 `--region` 的边界断言从不判定**（三个门禁共用此工具）；变异 `texture-rebuilt-every-frame` v1 删 early-return 反而让一次性解码重跑并降级所有槽；`clear_product_captures` 被沙箱安全删除垫片静默拦截后残留 capture 会让门禁测量上一个变异的二进制；mutation 还原哈希列表漏两个文件导致假红；判定 `fail -eq 1` 拒掉合法连锁红。
- 修复链接缺陷一处：`viewmodel.cpp` 调用新的共享材质解析，但 `viewmodel_material.cpp` / `vmt_material.cpp` / `vtf_texture.cpp` 只加进了 `tf2_demo_native` 与 `viewmodel_material_probe`，`viewmodel_fov_probe`、`viewmodel_contract_probe`、`viewmodel_animation_probe`、`animation_viewmodel_probe` 链接失败（NMAKE 停在第一个，其后 target 静默不构建）。由 `verify-fast.sh` 抓到，四个 target 已补齐。
- 未完成：phong/bump/selfillum 未实现（读数逐帧给出 `*AppliedRanges=0`，`*Slots` 仅为 VMT 声明值）；缺失材质在真实安装中不存在（扫过全部 37 个 ViewModel 共 147 个槽，无一缺失且全部解码，95 个 BC1 + 52 个 BC3，逐槽清单见 `evidence/viewmodel/material-readings-all-vmodels.txt`），fallback 由带标注的注入构造；未做人工画面对照、跨机器抓帧、多帧像素稳定性。总链未重跑。

### 清单四：音频 tick 调度与 null sink 契约已整合

- 整合提交：`28007af`，来源 `task/audio-tick-scheduler@9b4b41a21d4546efe85087e9fea72873a58069f3`；保留清单二的 `OutcomeSink`、`eventsSkippedDisabled` 和 `waveOutOpenCalls()`，并加入设备尝试/成功计数与 tick 调度实现。
- 定向验收：`verify-audio.sh` = `VERIFY-AUDIO=PASS (assertions_ok=28)`；`verify-audio.sh --mutation` 的效果、调度器和 sink 三组变异全部捕获；`verify-fast.sh` = `VERIFY-FAST=PASS (assertions_ok=43)`。
- 关键读数：`eventsSeen=10`、`eventsPlayed=6`、`eventsSkippedVoice=3`、`eventsMissingResource=1`、`repeatedSeen=2`、`recoveredSeen=2`、`device_open_attempts=0`、`device_opened=0`。
- 未验证：真实 TF2 demo 端到端音频回放、真实 MP3/ADPCM 在 WinMM 设备上的播放、长时间压力和非 Windows 平台；既有 `firstPacketTick=INT_MIN` 批量 flush 问题仍未修复（**该问题已由 2026-10-13 的 T3 修复，见上**）。总链未重跑。

### 清单三：ViewModel 材质资源链已整合

- 整合提交：`e584052`，来源 `task/viewmodel-materials@f795992`；新增 `viewmodel_material_probe`、材质链门禁和十类可再生夹具，仅扩展 CMake 目标注册，未修改 renderer/main 产品绘制。
- 真实安装读数：`c_sniper_arms`、`c_scattergun`、`v_scattergun_scout`、`c_pro_rifle` 共 10/10 槽完成 VMT/VTF 定位和像素解码，覆盖 BC1/BC3、反斜杠 `$basetexture`、空 `$cdmaterials` 和 2048² 边界。
- 验收：整合主线运行 `viewmodel-material-check.sh` 为 159 条断言通过；来源分支 `--mutation` 6/6 捕获并恢复夹具，`verify-render.sh` 8/8 通过。总链未重跑。
- 未完成：产品 ViewModel draw pass 材质绑定、真实画面人工确认、Phong/bump/selfillum 的 GPU 视觉效果。

### 清单一：ViewModel companion 动态手臂绘制已整合

- 整合提交：`9cfc913`、`4f43560`，来源 `task/viewmodel-companion-render@df0ce1e`；产品默认路径从 ViewModel owner 的 `m_nModelIndex` 解析并绘制实体自己的 `c_*_arms.mdl`，命令行 `v_*` 路径保留为明确的 mismatch 对照。
- 运行时证明：owner 与 view entity 匹配、实体模型路径与上传 mesh 一致、sequence 按实体自身模型表检查、accepted pose 的骨骼覆盖为 `58/58`；POV 自然序列为 `1..30`，实体模型只有 sequence `0` 有绑定，所以自然运行 `ok=0`，构造 `--viewmodel-sequence 0` 才进入 skinning 正例。
- 验收：`viewmodel-companion-check.sh` 108 条断言通过，`--mutation` 6/6 捕获；`viewmodel-animation-check.sh` 139 条断言通过，`verify-fast.sh` 43 条断言通过。`verify-render.sh` 尚未在整合主线上记录 9/9；总链已扩展为 24 步但尚未重跑。
- 未完成：静态武器本体第二笔、材质绑定到产品 draw pass、自然 demo 中可见的手臂动画、人工画面对照。

### 清单二：静默抓帧测试已整合

- 整合提交：`4dd79a9`，来源 `task/quiet-capture-tests@cf46caf`。
- 新增 `--hidden-window` 与 `--audio-disabled` 测试参数；新增 `quiet_capture_probe` 和 `quiet-capture-check.sh`，并注册 CMake 目标。
- 定向验收：`QUIET-CAPTURE=PASS`（63 条断言）、`QUIET-CAPTURE-MUTATION=PASS`（4 个 mutation 全部变红）、`verify-fast.sh` 43 条断言通过；正常与静默模式 BMP 均为 2,582,406 字节且逐字节一致。
- 实测静默模式 `window_visible=0`、`audio_device_opened=0`、`drawn=1`、capture/metrics 均写出；音频事件仍计数但 `events_played=0`，改记为 skipped。
- 未跑总链；只使用 bagel demo，地图缺失导致画面是回退四边形。基线已有 `firstPacketTick=INT_MIN` 使 4610 个音频事件在首次调度批量 flush，本提交只验证禁用设备契约，没有修复 tick 回绕。
- 清单一若修改 `native/src/main.cpp`，必须基于 `4dd79a9` 手动整合，不能直接把旧基线分支覆盖主线。

### T2.5 五层序列映射调查完成

- 调查提交：`83920eb`，主线整合为 `150f0c7`；探针新增按实体自身模型查 sequence 范围的 `vmselfsum` 与具名 `vmself-outlier`，stdout 单行 JSON 契约不变。
- POV 固定录制：view entity 18 → ViewModel 1077 → 武器实体 1008 `CTFSniperRifle`；武器给出手臂 `c_sniper_arms.mdl` 和工坊世界模型 `c_pro_rifle.mdl`，不给狙击枪 `v_*` 路径。
- 同 141 次 POV 观测：默认 `v_scattergun_scout.mdl` 为 37 in-range / 104 out-of-range；按实体自身模型比较为 141/141 in-range。matcha 从单一模型比较的 121 次越界降至按实体比较仅 1 次；该越界为 entity 569 / tick 11355 / sequence 68 / `c_soldier_arms.mdl` 54 条，产品须拒绝而非截断。
- `modelprecache`、`items_game.txt` 和加密 weapon ctx 均未提供狙击枪 `v_*` companion；`c_sniper_arms.mdl` 没有 activity 名称，无法测量得到 `v_*` 的 `ACT_VM_*` join。产品下一步采用“同伴证明”，本轮没有改产品绘制。
- 调查报告 `T2.5-MAPPING-RECON-2026-10-12.md` 已入库；总链不重跑，日常按分层门禁执行。

### T2.5 ViewModel 动画输入契约已整合

- 主线整合提交：`f5e427f`；来源产品提交 `447ca74`，门禁/交付提交 `2a92967`。
- 产品读取 `CTFViewModel.m_nSequence` 与 `m_flPlaybackRate`，区分 `missing`、`ok`、`sequenceOutOfRange`、`boneMismatch`；越界序列稳定回退 bind pose。
- 真实 POV 读数确认实体序列空间与绘制的 `v_scattergun_scout.mdl` 序列表不同（绘制模型 7 序列，实体模型 43 序列；观察到 0--34，104 次越界）。没有把数值落在范围内误认为映射。
- 门禁：主线 `verify-fast.sh` 43/43、`verify-render.sh` 8/8、T2.5 正常 PASS；独立和父锁 mutation 均 `red=3/3 hold=3/3`，源码恢复和恢复构建通过。
- 修复的问题：父级持构建锁时 mutation 恢复责任被错误跳过；pin 文件 CRLF 使 Git Bash 读到 `name\r` 并误报 demo 缺失；变异模式仍按正常版断言拒绝状态。
- 当时未完成：真实序列映射、ViewModel 材质与人工画面对照、完整 23 步总链、玩家动画外部输入。真实映射调查与总链随后分别在本日志后续条目闭合；产品动态手臂绘制和外部玩家动画仍未完成。

### 23 步总链复核与验收修复

- `b9f000e` 上的完整总链实际结果为 `VERIFY=FAIL`、退出码 1；第 1–21 步通过，第 22 步 mutation-model 锚点失效，第 23 步断言数 pin 过期（门禁实际 111，pin 为 110）。
- 在隔离分支修复第 22 步锚点，使其限定 standalone `--model` identity lambda；更新第 23 步断言 pin 为 111。
- 定向复核：`weapon-animation-check.sh --mutation-model` → `MUTATION-CAUGHT=PASS`（变异及恢复构建均 rc=0）；`viewmodel-animation-check.sh` → 111 条断言、`VIEWMODEL-ANIMATION=PASS`。
- **完整总链仍待在修复提交上重跑**；不把定向门禁结果替代 23/23 总链结果。
- 2026-10-12 按用户要求中止了一次修复后总链：运行到第 2 步，未生成 `VERIFY` 结论；锁已清理，机器产物放在本地 stash，不作为验收证据。
- 当前主线为 `1848814`。下一步必须先在干净、无并发、单实例环境重跑 23 步总链，再开始 T2.5 真实序列映射调查。

### 23 步总链正式闭合

- 在 `e4c4d41` 的干净、单实例主线上运行 `bash verify-all.sh --quick`：23/23、退出码 0、末行 `VERIFY=PASS`，耗时 2h17m23s。
- 关键计数：27 个目标构建、0 error、1 个既有 C4457；mutation `46 red / 6 hold / 0 green / 0 broke / 6 restored_identical`；第 23 步 111 条断言。
- 原始日志：`.scratch/obs/verify-all-23-quick.log`，45293 字节，sha256 `aff5fe6b40a4bea334feecd3f40df61a922b301204d2536927be68af85e4abd9`；第 23 步 verdict 已纳入 `evidence/verify/`，大型 trace 目录不入库。
- 第 20 步本次 `blockDiff=30`、`frameDiff=434510` 是接线后构建和 HUD 时间变化造成的整帧差异；门禁仍只钉块内至少 10 个像素变化，判定没有放宽。
- 总链只在合并主线、发版或核心协议变更时运行；日常改动按 `verify-fast.sh` 或对应 `verify-entity.sh`/`verify-render.sh`/`verify-audio.sh` 分层验收。
- 静默边界：fast、entity、audio 契约探针不建窗口且不打开音频；需要抓帧的 render/weapon/ViewModel 脚本尚未统一传入 `--hidden-window --audio-disabled`，未传参数时仍会启动可见窗口并使用 `--audio-device 6`。
## 2026-10-11

### T2 ViewModel 第一人称绘制已整合

- 整合提交：`1b765d5`（合并 `task/viewmodel-render@9894c5b`）；主线功能基线仍需在本次整合分支验收后更新。
- 产品已接入独立 ViewModel draw pass：默认 FOV 90，范围 40--120，attachment `weapon_bone`、左右手镜像、独立深度状态和世界 pass 后绘制均有运行时读数。
- `v_scattergun_scout.mdl` 实测 `drawn=1`、`vertices=9723`、`viewModelPass=6`、`afterWorld=1`、`depthWrite=1`、`depthFunc=less_equal`；缺模型/缺 companion/短骨表稳定回退且不改变场景帧。
- 验收：`viewmodel-contract-check.sh` 正常 `154` 条断言通过；`--mutation` `5/5` 捕获；`verify-fast.sh` 与 `verify-render.sh` 需在本整合提交上继续执行并记录最终结果。
- 本轮修复两个验收缺陷：门禁缺输入早退不清理锁目录；夹具可再生比较受 `core.autocrlf=true` 造成 CRLF 假红。比较改为 `diff --strip-trailing-cr`，锁使用 `trap` 清理。
- 未验证：完整 `verify-all.sh --quick`、真实游戏人工画面对照、ViewModel 材质/VMT/VTF、ViewModel 动画输入和不同机器的抓帧点集合。

### 下一项

- T3：把 T1 动画输入接入 ViewModel（有输入复用骨骼矩阵，无输入保持 `animationInput=missing`），只在新的 worktree 修改并先跑 `verify-fast.sh`。
- T4：世界材质与光照；T5：demo 音频时序；T6：相机/HUD/UI；T7：soak、损坏输入和发布验收。每项独立门禁，合并或核心协议变更才跑总链。

## 2026-10-10

### T1 武器/投射物骨骼动画接入

- 已将现有 `animation_decoder` 接入主程序实体路径，使用每实例骨骼矩阵缓冲和 GPU skinning。
- 关键提交：`ef3961b`、`b25ab60`、`fd5d661`、`d696c74`、`6419429`、`1f11b8c`。
- 实际读数：`gpuSkin=62/237`、`boneMismatch=0`、`advances=48→283`、`poseChanges=48→283`。
- `weapon-animation-check.sh` 正常 `6/6`；实体 bind-pose 变异和 `--model` 逆绑定变异均被捕获；实体档、渲染档通过。
- 限制：部分资产只有单一姿态块，三张见证帧中只有 2/3 张像素不同；游标变化不等于可见动作。玩家 `CTFPlayer` 动画输入在现有 demo 中为空，仍需外部输入。

### 验收链状态

- T0 的 21 步总链在 `98df09b` 上 `VERIFY=PASS`，rc=0，48m56s。
- T1 合并节点的第 22 步首次被环境事件中断，随后按同一环境独立重跑：`WEAPON-ANIMATION=PASS`，正常 52 条断言，实体变异 9 条红，模型变异 1 条红。
- 当前不声称存在一次未中断的 22/22 `VERIFY=PASS`；需要完整发布验收时在干净提交树单实例重跑总链。

### T2 ViewModel 起点

- 已创建 `D:\TF2_Native_Worktrees\viewmodel-render` / `task/viewmodel-render`，产品基线 `origin/p0-entity-protocol@5b58760`；首个 probe 提交为 `2c87438`。
- 已有 `viewmodel.cpp`、`viewmodel.h` 和诊断 probe，但尚未进入主程序第一人称 draw pass。
- 实施顺序：资源 companion 契约 → 请求缓存 → 独立 ViewModel draw pass → FOV/attachment/左右手 → T1 动画状态复用 → 真实帧和缺资源负向验收。

## 2026-10-09

- 完成实体材质接线、资源可达性判据、分级验收脚本和主循环空转修复；详见 `HANDOFF-2026-10-07.md` 与 `IMPLEMENTATION-PLAN-2026-10-10.md`。
