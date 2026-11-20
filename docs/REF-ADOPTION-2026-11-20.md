# TF2-Demo-Analyser 六项参考：逐卡采纳评估（2026-11-20）

- 卡片：跨卡评估（N17 / N18 / N21 / N12-N19 / N16 / N27），只读调研 + 计划，不改产品代码
- 上游对象：wombo14/tf2-demo-analyser @ main（提交见 docs/REF-TF2-DEMO-ANALYSER-2026-11-20.md），GPLv3
- 本地对照：D:/TF2_Native_Worktrees/t105-integrate @ fb6236df（work/integrate）
- 边界：本文件不含任何新实测读数；所有"我方现状"均来自当前工作树源码阅读（行号为写稿时读取）

## 0 结论表

| # | 上游机制 | 对应我方卡 | 现状差距 | 采纳 |
|---|---|---|---|---|
| 1 | 2D pose 网格双线性 + 逐骨骼 weight_list + SEQUENCE_DELTA | N17 | 我方只有一维 locomotion 权重 + 单一 aim 层（自述"approximation, not TF2 parity"） | **采纳（实现）** |
| 2 | 同名骨骼合并（Skeleton::attaches_to / attached_world），Attachment 只作枪口 | N18 | 我方走 attachment 链（weapon_bone 挂点），读数 residual=0.0000；未验证是否该用骨骼合并 | **采纳（先离线判定再定）** |
| 3 | demo_sounds 增量解码、96 声部、WAV cue loop、60 dB/36 u 衰减、POV 开火音补播 | N21 | 我方 computeSpatialMix 是线性 (1-d/max)^rolloff（max=2048, rolloff=1.0），无 cue loop、无 POV 补播 | **部分采纳** |
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

## 2 N18：同名骨骼合并 vs attachment（采纳为判定任务）

上游 `src/player/skeleton.rs`：`Skeleton::attaches_to` 检查武器与载体是否有同名骨骼，`attached_world` 直接拷贝同名骨骼世界矩阵；Attachment 只用于枪口出弹点。上游 `view.rs:3152` 的 fix_grip 是兜底：`bip_hand_r` 与 `weapon_bone` 世界距离 > GRIP_DISTANCE=12 才按视图空间重放。

我方现状：
- `native/src/native_renderer_viewmodel.cpp:518-595`、`:811-870` 全走 attachment 链：attachment local → 手部骨骼 → placement；实测 `grip ... residual=0.0000`、`alignment=weapon_bone`。
- 也就是说：**当前读数支持 attachment 已对准**；要判断"长枪离手"是否另有原因，应先回答 c_sniperrifle.mdl / c_sniper_arms.mdl 与玩家骨骼是否存在同名骨骼。若不存在，骨骼合并不适用，残差假设应转向蒙皮/姿态；若存在，才用同 tick 像素差（已知基线 4447 px）对比两种挂法。

## 3 N21：空间音频常量、loop 点与 POV 补播（部分采纳）

上游 `src/player/audio.rs`：独立线程 96 声部混音、读 WAV cue point 作 loop_start、`REFERENCE_DB=60.0` / `REFERENCE_DISTANCE=36.0` 距离衰减、`LIMITER_KNEE=0.6` 软限幅；`demo_sounds.rs` 按 SoundInfo_t::ReadDelta 增量解 svc_Sounds。GUIDE 指出 POV 录制者自己的开火/挥砍音效在 demo 里常缺失，需从第一人称动画补播。

我方现状：
- `native/src/audio_timeline.cpp:123-135` `computeSpatialMix(source, listener, maxDistance=2048.0f, rolloff=1.0f)`，gain = clamp((1 - d/max)^rolloff)，pan 另算；`native/src/audio_player.cpp:406-410` volume = event.volume × gain × master。
- 没有 60 dB 参考距离模型、没有 cue loop_start、没有 POV 开火补播；`audio: seek reset` 已有（main.cpp:12117）。

落地建议：先做三问审计——(a) 同一 tick 我方 gain 与 60 dB/36 u 公式差多少（用既有 trace 的 origin/listener 数值离线算）；(b) WAV cue 点解析是否存在；(c) recorded-owner 的开火音效在 trace 里是否真的缺失。

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
