# 更新日志

## 2026-10-12

### T2 第二笔：静态武器本体独立 draw request（`task/weapon-body-render`，待合并）

- 分支 `task/weapon-body-render`，基线 `origin/p0-entity-protocol@9a8387b`，工作区 `D:\TF2_Native_Worktrees\weapon-body-render`。产品在手臂之后新增第二笔独立 draw request：武器实体的 `m_iWorldModelIndex` → `c_*` 武器本体模型，独立 mesh/radius/near-far/projection，与手臂分别计数；拒绝时清空旧武器槽位，不留过期 mesh。
- 定向验收：`bash build-target.sh tf2_demo_native weapon_body_probe` rc=0；`bash weapon-body-check.sh` = `WEAPON-BODY=PASS`（61 条断言、0 FAIL）；`bash weapon-body-check.sh --mutation` = `MUTATION-CAUGHT=PASS (4 defects)`（104 条断言、0 FAIL）；`bash verify-fast.sh` = `VERIFY-FAST=PASS (assertions_ok=43)`、rc=0。
- 关键读数（POV `autorecord_2026-07-02_13-26-46.dem`，tick 2000）：武器实体 1008 `CTFSniperRifle` → `m_iWorldModelIndex=533` → `c_pro_rifle.mdl`；`armDrawCalls=weaponDrawCalls=1379`、`weaponMaterials=2/2/2/2`、`weaponVisible=1`、`weaponVertices=12357`、`weaponFallback=-`；`byte_diff(on, off)=42209`、`byte_diff(on, on-repeat)=0`、`byte_diff(on, visible)=0`；`weaponBody model=` 与 `viewmodel request path=` 各 1 次（非每帧重建）。
- 变异（每个都有具名读数与具名 control）：m1 读 `m_nModelIndex` 代替 `m_iWorldModelIndex` → 读数报 1032、拒绝为 `worldModelIsArms`、不绘制、帧与 `body-off` 逐字节相同；m2 删除 arms 比较 → 探针 8 条命名断言变红而产品读数不变（control，正常路径不经过被删检查）；m3 删除第二笔提交连同其计数 → 计数分离（0 vs 1370）且帧与 `body-off` 逐字节相同；m4 材质链短路 → 8 条断言变红。四个变异全部捕获，源码逐字节恢复并重建成功。
- 像素级闭合：`body-off.bmp`、`mutation-m1.bmp`、`mutation-m3.bmp` 的 sha256 相同（`1d2cab7d…`）——"拒绝"、"关闭"、"提交消失"画出同一画面。
- 顺带修复的产品缺陷：arms 比较原先按原始字符串比较，反斜杠与大小写变体可绕过同一模型的判定；`normalizeModelPath` 提升为 `model_loader.h` 的共享 inline，三个翻译单元共用一条规则（该缺陷由探针自检首次运行抓出）。
- 未验证：人工画面对照、武器切换/多武器场景、`verify-render.sh` 未在本分支重跑、总链未跑、真实 GPU 性能。抓帧落点按请求 tick 对齐：`--capture-tick N` 的"≥ N"语义会让落点漂移，本次两次运行共 5 次漂移到 2001 并全部由重试纠正。

### 清单四：音频 tick 调度与 null sink 契约已整合

- 整合提交：`28007af`，来源 `task/audio-tick-scheduler@9b4b41a21d4546efe85087e9fea72873a58069f3`；保留清单二的 `OutcomeSink`、`eventsSkippedDisabled` 和 `waveOutOpenCalls()`，并加入设备尝试/成功计数与 tick 调度实现。
- 定向验收：`verify-audio.sh` = `VERIFY-AUDIO=PASS (assertions_ok=28)`；`verify-audio.sh --mutation` 的效果、调度器和 sink 三组变异全部捕获；`verify-fast.sh` = `VERIFY-FAST=PASS (assertions_ok=43)`。
- 关键读数：`eventsSeen=10`、`eventsPlayed=6`、`eventsSkippedVoice=3`、`eventsMissingResource=1`、`repeatedSeen=2`、`recoveredSeen=2`、`device_open_attempts=0`、`device_opened=0`。
- 未验证：真实 TF2 demo 端到端音频回放、真实 MP3/ADPCM 在 WinMM 设备上的播放、长时间压力和非 Windows 平台；既有 `firstPacketTick=INT_MIN` 批量 flush 问题仍未修复。总链未重跑。

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
