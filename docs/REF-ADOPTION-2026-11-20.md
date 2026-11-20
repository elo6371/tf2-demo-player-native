# TF2-Demo-Analyser 六项参考：逐卡采纳评估（2026-11-20）

- 卡片：跨卡评估（N17 / N18 / N21 / N12-N19 / N16 / N27），只读调研 + 计划，不改产品代码
- 上游对象：wombo14/tf2-demo-analyser @ main（提交见 docs/REF-TF2-DEMO-ANALYSER-2026-11-20.md），GPLv3
- 本地对照：D:/TF2_Native_Worktrees/t105-integrate @ fb6236df（work/integrate）
- 边界：本文件不含任何新实测读数；所有"我方现状"均来自当前工作树源码阅读（行号为写稿时读取）

## 0 结论表

| # | 上游机制 | 对应我方卡 | 现状差距 | 采纳 |
|---|---|---|---|---|
| 1 | 2D pose 网格双线性 + 逐骨骼 weight_list + SEQUENCE_DELTA | N17 | 我方只有一维 locomotion 权重 + 单一 aim 层（自述"approximation, not TF2 parity"） | **采纳（实现）** |
| 2 | 同名骨骼合并（Skeleton::attaches_to / attached_world），Attachment 只作枪口 | N18 | P2 已实现 bone 放置（arms 世界矩阵 × weapon_bone 帧）：body 位移 46.2226 模型单位、像素差 946 px，默认 attachment 逐字节不变 | **采纳（已实现并实测，32f78ed9）** |
| 3 | demo_sounds 增量解码、96 声部、WAV cue loop、60 dB/36 u 衰减、POV 开火音补播 | N21 | 我方原 computeSpatialMix 是线性 (1-d/max)^rolloff（max=2048, rolloff=1.0），无 cue loop、无 POV 补播；(a) 审计发现听者用了归一化盒坐标（见 §3） | **部分采纳（(a) 听者已修 + 公式开关 1f00b759，(b) cue/smpl 解析 6d9d9ea6，(c) 未做）** |
| 4 | 整段快照 + MAX_FRAMES=150_000 + 稀疏采样；8 位 simtick 重建 | N12/N19 | 我方有 seek，但未见帧快照上限 | **采纳（先审计再定）** |
| 5 | 多标签页关闭时把 GPU 资源推入 closing 队列，下一帧统一释放 | N16 | 我方单窗无 pane，契约未涉及多窗资源生命周期 | **延后（记入契约）** |
| 6 | clip.rs：后台线程编码 5/10/20s 片段，无 ffmpeg 降级 800px GIF | N27 | 我方 MF 探针 H.264/AAC 可用；产品导出未实现 | **延后（兜底参考）** |

## 1 N17：pose 网格与逐骨骼权重（采纳，最高优先）

上游 `src/player/skeleton.rs`：直接解 mdl 的 SEQUENCE、POSE_PARAM、AUTOLAYER 与最多 16 个 include_paths 外部动画文件；用 demo 归一化参数在 width × height 序列网格上做 2D 双线性插值，再按每根骨骼的 weight_list 叠加 SEQUENCE_DELTA（区分 SEQUENCE_POST 前/后乘）与非 delta 层。

我方现状：
- `native/include/animation_blend.h:14` 自述 "THE MODEL (first version; an approximation, not TF2 parity)"；权重只区分 stand/walk/run 两段线性 + 按朝向夹角给 aim 层权重。
- 全目录 grep `SEQUENCE_DELTA`/`poseParam`/`numblends` 在 `native/src` 零命中：产品侧没有网格采样、没有逐骨骼权重、没有 delta 层。
- 离线结构已盘点：`docs/N17-BLEND-INVENTORY-2026-11-18.md`（Scout companion 377 序列中 74 条 numblends!=1：39 条 3×3 移动网格、28 条 3(yaw)×4(pitch) 瞄准矩阵、6 条一维；基础 mdl 306 条 numblends 全 0，必须读 companion）。

落地建议：先做只读判定——从 demo 取 move_x/move_y 与 aim yaw/pitch 的归一化参数，检查我方当前权重与网格坐标是否可映射；再分两步实现（网格采样 → 逐骨骼 weight_list/delta），每步带 probe 读数与 trace 权重。
已实施（2026-11-20，提交 `4236fad0`、`f0d82aa9`、`03da539c`；默认路径不变，要开关才切采样器）：
- Step 1 `4236fad0`：解 mdl 的 POSE_PARAM / WEIGHT_LIST 与网格单元，`model_pose_probe` 打印每模型 `GRID-POSE-PARAMS` / `GRID-CELLS` / `WEIGHTLIST` 读数（离线判定轴角色与网格形状）。
- Step 2/3 `f0d82aa9`：按参数名解析轴（`move_x`/`move_y` 在槽 4/5 或 5/4，随职业换）、角点同 cycle 对相位、在 width×height 上做 2D 双线性采样、按每骨骼 `weight_list` gate（某层对某骨骼权重为 0 就排除该骨骼；全排除时回退 dominant 并计 `poseMaskIsolatedBones`），并统计每序列 delta 通道；`--posegrid-selftest` 8 项断言全过（centre/clamp/swap/aim12/grip/gate/weight/sweep）。跨层 `SEQUENCE_DELTA` 仍未实现。
- Phase B `03da539c`：产品开关 `--animation-blend posegrid|legacy`（从 `native/src/main_cli.h` 的 flag 表解析，因为 `main.cpp` 的 argv 链已到 MSVC C1061 块嵌套上限）；`posegrid` 用归一化速度喂 `move_x`/`move_y`（网格轴范围就是 [-1..1]）、用 view−body 的 yaw/pitch 喂瞄准网格、权重掩码打开；每帧（上限 8 行）打 `anim: posegrid tick= ent= model= applied= layers= idx= frac= cells= w= roles= clamp= gated= isolated= reason=`。
- 仿真实测（matcha demo，`--capture-tick 3000`）：不开关与 `--animation-blend legacy` 两种跑法抓帧 md5 都是 `9681f12c9e672ae896105f7756b3bf5f`，与改动前 exe 逐字节相同；`--animation-blend posegrid` rc=0、无 UNHANDLED，全程 16136 次采样 `applied=1`，同 tick 画面与 legacy 差 71.4% 像素（网格采样确实在驱动姿态）；未知值/裸开关 rc=13。
- 未验证：`move_y` / `aim_pitch` 的符号、网格带来的根位移量级是否合理、与源引擎的视觉保真；这些留给 N17 的视觉抽检卡，不阻塞开关合入。

## 2 N18：同名骨骼合并 vs attachment（采纳为判定任务）

上游 `src/player/skeleton.rs`：`Skeleton::attaches_to` 检查武器与载体是否有同名骨骼，`attached_world` 直接拷贝同名骨骼世界矩阵；Attachment 只用于枪口出弹点。上游 `view.rs:3152` 的 fix_grip 是兜底：`bip_hand_r` 与 `weapon_bone` 世界距离 > GRIP_DISTANCE=12 才按视图空间重放。

我方现状：
- `native/src/native_renderer_viewmodel.cpp:518-595`、`:811-870` 全走 attachment 链：attachment local → 手部骨骼 → placement；实测 `grip ... residual=0.0000`、`alignment=weapon_bone`。
- 也就是说：**当前读数支持 attachment 已对准**；要判断"长枪离手"是否另有原因，应先回答 c_sniperrifle.mdl / c_sniper_arms.mdl 与玩家骨骼是否存在同名骨骼。若不存在，骨骼合并不适用，残差假设应转向蒙皮/姿态；若存在，才用同 tick 像素差（已知基线 4447 px）对比两种挂法。

P2（提交 `32f78ed9`）把这个问题落成了同帧 A/B 开关 `--viewmodel-weapon-attach=attachment|bone`：
- `bone` 时 `weaponWorld = armsWorld × gripBoneFrame`（帧取 `boneModelMatrices`，rest 帧由 `poseToBone` 逆求），实测 body 原点相对 attachment 放置位移 **46.2226** 模型单位（P1 离线预测 46.22234），同 tick 像素差 **946 px**、bbox 139×203；
- 默认 `attachment` 的帧与改动前**逐字节相同**（md5 911768d4…），`viewmodel-contract-check.sh` 154 断言 PASS；`--viewmodel-weapon-attach=lol` 与裸开关均以 rc=13 拒绝；
- 未实测：`bone-missing` 回退（本机没有缺 `weapon_bone` 的便利模型）与 posed（非 rest）帧路径。

## 3 N21：空间音频常量、loop 点与 POV 补播（部分采纳）

上游 `src/player/audio.rs`：独立线程 96 声部混音、读 WAV cue point 作 loop_start、`REFERENCE_DB=60.0` / `REFERENCE_DISTANCE=36.0` 距离衰减、`LIMITER_KNEE=0.6` 软限幅；`demo_sounds.rs` 按 SoundInfo_t::ReadDelta 增量解 svc_Sounds。GUIDE 指出 POV 录制者自己的开火/挥砍音效在 demo 里常缺失，需从第一人称动画补播。

我方现状：
- `native/src/audio_timeline.cpp:123-135` `computeSpatialMix(source, listener, maxDistance=2048.0f, rolloff=1.0f)`，gain = clamp((1 - d/max)^rolloff)，pan 另算；`native/src/audio_player.cpp:406-410` volume = event.volume × gain × master。
- 没有 60 dB 参考距离模型、没有 cue loop_start、没有 POV 开火补播；`audio: seek reset` 已有（main.cpp:12117）。

落地建议：先做三问审计——(a) 同一 tick 我方 gain 与 60 dB/36 u 公式差多少（用既有 trace 的 origin/listener 数值离线算）；(b) WAV cue 点解析是否存在；(c) recorded-owner 的开火音效在 trace 里是否真的缺失。

(a) 审计结果（2026-11-20，报告 `.scratch/n21a-review/spatial-audit.md`，只读，产品行为未改）：
- 两边是两个模型：上游 `scale=10^((60-level)/20)/36`，`relative=d*scale`，`falloff=relative>0.1 ? min(1/relative,1) : 1`，永不静音；我方 `(1-d/2048)^1.0`，d>=2048 硬静音。60 dB 档下 d=100/512/1024 我方响 +8.44/+20.56/+23.06 dB，d=2048 上游仍 -35.1 dB 而我方 0；90 dB 档下 d=512/1024/1600 我方反而轻 -2.50/-6.02/-10.24 dB。两条曲线在 60 dB 于 36.66 u 与 2011.34 u 相交。
- **更严重的是听者缺陷**：`native/src/main.cpp:12256-12259` 把 `renderer.observerFocusWorld()` 喂给 `soundScheduler.setListenerPosition`，而 `observerFocusWorld`（`native/src/native_renderer_camera.cpp:75-79`）返回的是归一化盒坐标（`:51-62` 的 world→box），声源 origin 却是地图单位。实测 500 条事件听众恒为 `(-0.6,-0.3,0.4)` 且不随相机 eye 移动；后果 260/500（52%）被判 inaudible，其中 51 条真实距离 <500 u（最近 144.8 u 的 `machete_swing`），同时 94 条真实 >=2048 u 仍在响；`oursGain==0` 与 inaudible 260/260 一一对应。T10 为 PVS 修过同类问题：`observerFocusMapWorld` 精确逆（`native_renderer_camera.cpp:81-96`）已在 `native/src/main.cpp:9663` 使用。
- 真实分布（500 条，tick 8..1568，24.5 s）：真实距离中位 1621.42 u（产品打印中位 2065.51 u），300 条 <2048、200 条 >=2048；按真实距离重算 dB 差（300 条可闻，level=60）：中位 +20.95，296/300 >+6 dB。
- 建议（未实施）：先把监听点换成 `observerFocusMapWorld`（取不到时 clearListener 或跳过衰减，不沿用盒坐标），再换上分段反比公式（level 未知按 60 dB，maxDistance 只作可选静音），并加"每 N 条事件 trace 一次 listener"的回归。
- (b) 结论：WAV 解析只有 fmt(106)/data(117)（`native/src/audio_resource.cpp:88-140`），不解析 cue 点 / loop_start；上游 `audio.rs:75-78` 会读成 `Clip.loop_start`。
- (c) 尚未审计（本轮只做 (a)(b)），留待批准后再查。
- 已知限制：每 sound 的 level 未解析（`native/include/demo_header.h:124-138`、`native/src/demo_header_sound.cpp:125-130` 丢弃 9 bit），dB 三档只能作敏感度带；只一次 run 一张图、前 500 条事件；未做双耳试听与 pan/master 对比。
- 已实施（2026-11-20，提交 `1f00b759`、`6d9d9ea6`；默认路径不变，要开关才切公式）：
  - 听者修复：音频块改为优先取 `g_scenePovEye.eye`（地图单位，`src=pov`），取不到再退 `renderer.cameraEyeMapWorld()`（`src=camera`），两者都失败则喂非有限坐标（`src=none`，调度器读作"不衰减"而不是"听者在原点"）；每 64 tick 打一行 `audio: listener tick= src= model= pos=` 作回归。同一 POV demo（tick 3000）：32 个采样点全部与前一帧 `scene: camera ... eye=` 一致（误差 <0.05 u）；修复前数百 tick 恒定不变。
  - 参考公式开关：`--audio-spatial linear|reference`，默认 `linear`（出厂混音不变），裸开关或未知值 rc=13 拒绝；`reference` 实现上游曲线——`scale=1/36`（60 dB）、`relative=d*scale`、`falloff = relative>0.1 ? min(1/relative,1) : 1`，**永不静音**（与线性版 2048 u 硬静音的差别是刻意的）。`audio_effects_probe` 断言 36 u=1、72 u=0.5（-6 dB）、360 u=0.1（-20 dB）、3600 u=0.01（-40 dB，且不静音）、NaN→静音。
  - A/B（同 build、同 demo、tick 3000、`--audio-device 6`）：`linear` played=485 / inaudible=393 / missing=716；`reference` played=878 / inaudible=0 / missing=323。差额 393 恰是线性版被 2048 u 截断消音的那批事件（485+393=878，716-393=323）；两版 `duplicateTicks` 不同（0 vs 368，帧节奏差异），不影响计数解释，但说明这不是逐帧严格对齐的 A/B。
  - (b) 已实现：`WavLoopInfo`（hasLoop / loopStart / loopEnd 排他、cuePoint、smplLoopCount）解析 RIFF `cue `（count@0、首点 dwSampleOffset）与 `smpl`（numLoops@28、首 loop dwStart@36+8、dwEnd@36+12）；畸形块忽略不拒播，cue-only 回退 loopStart、loopEnd=0；播放层零改动（还没有人消费 loop 区间）。
  - 崩溃记录：本轮 bisect 中的 0xC0000005 与音频无关——残留 `main.cpp.obj` 对上放大后的动画结构体（`AnimationSampleResult` 280→424 字节）砸调用方栈；纯 HEAD 构建同 demo 同 tick 抓帧 rc=0。
  - 仍未做：(c) POV 开火/挥砍补播（另见 `.scratch/n21c-review/pov-fire-audit.md`：该 demo 只解到 5127/140863 条 `svc_Sounds`、覆盖 tick 0..12187，而录制者的 223 次 player_hurt / 82 次 player_death 全在 13876..304763，不能用它回答"computed-owner 开火无声"）；公式尚未设为默认；未做双耳试听；`level` 仍未解析。

## 4 N12/N19：长片内存与快照上限（采纳为审计）

上游 `recording.rs`：整份 demo 一次解包成紧凑帧快照数组，硬限 MAX_FRAMES=150_000，超长自动稀疏采样防 OOM；`cheat_analyser_base.rs` 有 8 位 m_flSimulationTime 重建 sim_tick。

我方现状：`native/src/main.cpp:2094` 起有 seek 实现，`main.cpp` 内 snapshot/MAX_ 相关 115 处；是否对整段帧做无上限缓存、超长 demo 内存曲线如何，尚无读数。

落地建议：用两份长度差 3 倍以上的 demo 量进程 RSS 与帧缓存条目数，确认是否存在上限；若无，按 150k 帧 + 稀疏采样给一个可判定的上限与 trace 读数（拒绝要具名，符合 N19 预算口径）。

## 5 N16：closing 队列（延后，记入契约）

上游 `tabs.rs`：标签页关闭不立即销毁 GPU 资源，推入 closing 队列，等下一帧持有图形上下文时统一释放。我方 N16 契约是单窗切两窗、无 tab 概念；该模式只对"未来多窗/多 pass 资源生命周期"有意义，先写入 `docs/N16-SPLITVIEW-CONTRACT-2026-11-16.md` 的后续风险小节，不实现。

## 6 N27：clip 导出兜底（延后）

上游 `clip.rs`：后台线程经通道编码 5/10/20s 片段，检测到 ffmpeg 出 MP4，否则降级 800px GIF。我方 MF 探针在 Windows 构建机上 H.264/AAC 可用（`docs/N27-EXPORT-PROBE-RERUN-2026-11-16.md`），产品固定步导出仍未实现；GIF 兜底只对"缺 MF 组件"的机器有意义，等导出主路径落地后再评估，不进本轮计划。

## 7 计划落点

已登记共享任务（见任务板）：
- N17-posegrid：网格采样 + 逐骨骼权重 + delta 层（先只读判定，再实现）
- N18-bonemerge：同名骨骼离线判定（有/无同名骨骼 → 两条分支）
- N21-spatial：60 dB/36 u 对照 + cue loop + POV 补播审计
- N12-limit：长片帧缓存上限审计（含 150k 参考值）

## 8 不采纳的理由（避免误读）

- 上游渲染的**采样/插值/启发式相机**（MAX_STEP_PER_FRAME=300 不插值、fix_grip 距离阈值）与其"录制回放"定位绑定，不能替代我方逐 tick 取证；N18 若走骨骼合并，仍需我方自己的像素证据。
- GPLv3 代码不可搬（我方仓库根无 LICENSE）：本计划只采纳**机制与判据**，实现按我方代码风格重写。
