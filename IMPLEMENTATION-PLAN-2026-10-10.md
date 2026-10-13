# 当前整合覆盖（2026-10-13）

T3 世界材质与光照已合入主线 `b7d8035`，共享抓帧修复已合入 `1c69f08`，T4 相机/HUD/UI 已合入 `bf0d268`。世界材质、ViewModel contract、frame-capture、`verify-fast.sh` 和 camera-ui 定向验收通过；animation 吞吐阈值与 camera-ui 标题编码假红均已修复。完整总链非必要不运行；天空盒/水面绘制、VIS 裁剪和人工窗口验收仍是后续任务。

# TF2 Demo Player Native 实现任务与验收计划

更新时间：2026-10-13
产品整合基线：`origin/integration/entity-material@1a96adb`（实体材质已整合）
当前验收与诊断基线：`p0-entity-protocol@8d5c8cd`（已整合 T0、实体材质、T1 动画接线、T2 ViewModel、T2.5 输入契约、清单一 companion 绘制、五层映射调查、静默抓帧测试、ViewModel 材质资源链与产品接入、T2 静态武器本体、清单四音频调度契约和 T3 音频主程序接线；当前完整总链尚未重跑，不是产品发布分支）

## 2026-10-14 T3 人工验收与真实输入（`task/manual-followup`，未合并）

基线 `origin/p0-entity-protocol@3c92f7d`，工作区 `D:\TF2_Native_Worktrees\manual-followup`；只新增报告 `T3-MANUAL-FOLLOWUP-2026-10-14.md` 与 `evidence/manual-followup/`（45 文件），未改产品源码与门禁。要点：POV 录制 `autorecord_2026-07-02_13-26-46.dem` 确有 ViewModel 实体（实体 1077、武器实体 1008 → `c_pro_rifle.mdl`）；开关矩阵 11 用例钉 tick 2000（`--capture-tick` 落点漂移必须重试钉住）；真实键盘三键（SPACE 单击/双击、P）经 `PostMessage` 消息投递，三重证据（修正读法 HUD + 产品计数 + trace 停滞段）；窗口缩放帧头==客户区；声音真实设备与 null sink 分开记录。未确认：武器切换、ViewModel 动画画面（无绑定）、水面、天空盒纹理、声音可听性、其它 DPI 档。**仪器读数规则（后续任务是硬约束）**：`win32gui.GetWindowText` 跨进程截断 511 字符——读 HUD 标题必须用长度感知读法（`GetWindowTextLengthW` + 精确缓冲区），字段抽取锚定 `HUD[tick=`；`freezeFrames` 是 free-look 场景计数不是暂停计数。验收：`verify-fast.sh` = `VERIFY-FAST=PASS (assertions_ok=43)`、`camera-ui-check.sh` = `CAMERA-UI=PASS`，rc=0。

## 2026-10-13 T1 当前渲染档复核

`task/render-current@8ddab34` 已整合，新增报告和 `evidence/render-current/`，未改产品源码、`verify-render.sh` 或 `verify-all.sh`。来源 worktree 的正向结果保留为 `VERIFY-RENDER=FAIL (1 red)`、8/9：`viewmodel-contract` 的测量抓帧点断言因 T3 音频接线改变首帧速度而落入 10253--10267 标定盲区；复采 3/3 稳定。变异结果为 `VERIFY-RENDER=PASS (9/9 gates, mutation=1)`。当前 HEAD 的正向渲染档仍需后续单独复核，不能把 mutation PASS 或旧基线 9/9 当作当前正向 PASS；完整总链不因本项重跑。

### T2 人工验收（2026-10-13，已整合）

`task/manual-acceptance@ae7bd09` 仅新增人工验收报告和证据。冻结 ViewModel 矩阵确认模型输入、FOV、手性会影响画面；真实设备运行确认设备 6 打开且播放 19 个事件，用户已确认声音内容。动画/材质正确性、武器本体可见性和人工画面观看仍未由该矩阵证明：选定 tick 没有 ViewModel 实体，武器本体 on/off 逐字节相同；tick 2000 与 4000 逐字节相同。该项不修改自动门禁，也不构成 `verify-render.sh` 或完整总链通过。

渲染档补跑（2026-10-12，工作区 `D:\TF2_Native_Worktrees\render-gate-recheck`，基线 `9a8387b`）：
9 个门禁的 `verify-render.sh` 已在该基线上跑过正向与 `--mutation` 各一次，两次都是
`VERIFY-RENDER=PASS (9/9 gates)`、退出码 0，记录 `T0-RENDER-RECHECK-2026-10-12.md`。
总链仍按上一段记为未重跑；渲染档的补跑不构成总链结论。
主程序目录：`D:\TF2_Native_Test`
只读源目录：`D:\TF2_Demo_Player`

这份文件是交给其他 AI 的执行说明。接手者必须先读本文件和
`HANDOFF-2026-10-07.md`，再创建自己的工作区；不得直接修改主线工作区或
`D:\TF2_Demo_Player`。

## 一、项目现状

### 已完成并可复用

| 项目 | 事实证据 | 结论 |
|---|---|---|
| SourceTV PacketEntities 恢复 | 9 份固定 demo 与 Rust oracle 的包数、实体更新数逐值一致；`entity_failures=0` | 协议修复可复用；新协议改动仍需完整回归 |
| 实体历史保留 | `history-coverage-check.sh` 已有按最大 tick 间隙的门禁 | 窗口内精确，窗口外仍可能返回 Checkpoint |
| 实体模型材质 | tick 3001：`entityMaterials=95/95`、`resolved=59 uploaded=59`、`blockDiff=20 frameDiff=20` | 实体使用自有材质已接通 |
| 主循环等待 | `verify-fast.sh` 当前 `36/36`；idle CSV 使用截断写入 | 空转修复已完成；T0 总链已在 `98df09b` 通过 |
| 动画数学诊断 | `skeleton_skin_probe`：`bones=78 animBones=76 mappedBones=76`，矩阵反序变异可红 | 只证明数学契约，不证明产品 GPU 动画已接线 |
| 玩家动画供给诊断 | `CTFPlayer sequence=0 cycle=0 rate=0 pose=0`；正对照 class 计数 197/195/197 | POV demo 不能直接驱动玩家序列动画 |
| VPK 音频资源分类 | weapon/footstep/Uber 资源探针通过，`device_opened=0` | 资源可用，不等于 demo 音频时序完成 |

### 当前未完成

1. 玩家动画需要客户端预测或外部输入，不能从当前 demo 猜测；不能把这一项伪装成已完成。
2. ViewModel 第一人称 draw pass 和 T2.5 动画输入拒绝契约已接入；材质/VMT/VTF 已在分支 `task/viewmodel-material-render`（AI-B / T1）接入产品 draw pass（未合并主线，见下），真实游戏画面对照、实体序列到 `v_*.mdl` 的真实映射和 phong/bump/selfillum 仍未闭环。
3. 世界逐面 lightmap、cubemap 六面采样与 displacement 已在 `task/world-material-lighting`（未合并，见下）闭环到"有读数、有状态"；skybox 与水面**未绘制**、水面 RT 未实现、VIS clipping 未裁剪。
4. Demo 音频的 sound index 到 WAV 的 tick 对齐、TempEntity/PCF、过滤规则未闭环。
5. 相机模式、观察者切换、基础 HUD/最终 UI、DPI/主题和导出状态未完成验收。
6. 10 分钟 soak、损坏 demo、移动资源目录、安装启动、真实 GPU/FPS 和发布包未完成。
7. PaintKit、Phong、bump/selfillum、完整粒子和高级水面效果未完成。

### 清单四交付状态（2026-10-12）

`task/audio-tick-scheduler@9b4b41a21d4546efe85087e9fea72873a58069f3` 已整合为 `28007af`。音频时间线现在按 tick 去重、支持倒放恢复、延时取整/下限、声音类别过滤和 sound index 到 VPK WAV 的失败原因；`AudioEventScheduler` 同时支持普通播放 sink 与 `--audio-disabled` 的 `OutcomeSink`。定向 `verify-audio.sh` 通过 28 条断言，效果/调度器/sink 变异全部捕获，`verify-fast.sh` 通过 43 条断言。

本交付只闭合调度与 null sink 契约。真实 demo 端到端声音播放、真实 MP3/ADPCM 解码和设备输出、长时间压力、非 Windows 平台及主程序完整音频接线仍未完成；总链不因本项重跑。

### 清单三交付状态（2026-10-12）

`task/viewmodel-materials@f795992` 已整合为 `e584052`。`viewmodel_material_probe` 复用产品材质解析链，门禁覆盖真实 VPK 模型和可再生缺陷夹具；整合主线正常门禁为 159 条断言通过，来源分支的 6/6 mutation 和 `verify-render.sh` 8/8 已通过。资源解析与 BC1/BC3 像素解码闭合；产品 draw pass 尚未读取这些材质的缺口已由下方 T1 分支补上（未合并主线），人工画面对照和高级 shader 效果仍未完成。

### ViewModel 材质接入交付状态（AI-B / T1，2026-10-13，已合并）

`task/viewmodel-material-render`（worktree `D:\TF2_Native_Worktrees\viewmodel-material-render`，基线 `origin/p0-entity-protocol@9a8387b`）把清单三的解码链接入实际 ViewModel draw pass：每个材质槽有运行时状态，GPU 纹理按规范化 VTF 资源路径缓存并在每帧复用（长跑 149 帧 `texturesUploaded=2`、`texturesReused=149`），缺失槽进入具名 fallback；新增九项稳定读数与 `--viewmodel-material-slot` / `--viewmodel-material-missing-slot` / `--viewmodel-flat-color` 覆盖开关。像素对照必须在 `--start-paused --capture-tick 0` 冻结世界下取得——实测 `--capture-tick` 的落点随帧时机浮动（同一个值落在 tick 200/201/202，世界内容差异可达全帧 3.5%），长跑不可作为像素对照。专项门禁 220 条断言、10/10 mutation；phong/bump/selfillum 未实现，读数逐帧标 `*AppliedRanges=0`，防止把 VMT 声明当支持。

剩余：人工画面对照、跨机器抓帧、phong/bump/selfillum、多帧像素稳定性；本项已合并主线，但 `verify-render.sh` 和总链尚未在当前 HEAD 重跑。

### 世界材质与地图光照交付状态（2026-10-13，未合并；任务书 `TASK-PLAN-2026-10-13-4-WAY.md` 中编号为 T3，即本计划 §T4）

`task/world-material-lighting`（worktree `D:\TF2_Native_Worktrees\world-material-lighting`，基线 `origin/p0-entity-protocol@0f023bb`）按任务书顺序实现六项能力，每项先做合成 BSP fixture 再接真实 BSP，各自输出 state/reason/counters，不以整帧变化当证据。实测：逐面 RGBExp32 lightmap `state=active`（snakewater 2048x2047 图集装 11512/11527 面、`trianglesWithLightUv=199263/200000`、15 面被装包器丢弃）；cubemap 33/33 六面全解、198 面、全部来自 BSP pakfile（旧读数 fallback 0/33；面数按图像段长度实测）；sky 6 个 VMT 解出像素（512x256..1x1）；displacement 1316 面/165376 三角（渲染侧计数相等）；water 18 面/`approximate-warp`（反射折射 unavailable 具名）；VIS `cluster-pvs`/2940 簇/`clipApplied=0`。产品 trace 与解析侧相等：`lightmapped=597789/600000`、`displacedTriangles=165376`、`waterTriangles=36`。`world-material-check.sh` = `WORLD-MATERIAL=PASS`（8 节）+ `--mutation`；`verify-fast.sh` = `VERIFY-FAST=PASS`（43 条断言）。

同轮更新共享门禁 `frame-capture-check.sh` 的一条判据前提（颜色多样性上限 → 同次运行回退帧的字节比较，单独提交 `3ba4803`，需整合者确认）：lightmap 把世界帧 563→15909 色，旧上限 1000 由"能分辨"变成"必然红"。

未完成：skybox 与水面**未绘制**（只有资源/状态读数）；VIS **未裁剪**；大图 lightmap 图集丢弃（abbey 4137/22991 面）；人工画面对照、真实 GPU 性能；总链未跑。详见 `T3-WORLD-MATERIAL-LIGHTING-2026-10-13.md`。

### T1 交付与验收状态（2026-10-10）

`task/weapon-animation@91a0de6` 已实现实体实例的 GPU skinning 适配，使用每实例
`StructuredBuffer<float4x4>` 和 `boneOffset/boneCount`，不是旧的单副 `b1` 常量缓冲。
真实 `cp_snakewater_final1` 读数为 `gpuSkin=62/237`、`boneMismatch=0`、`advances=48→283`、
`poseChanges=48→283`；`weapon-animation-check.sh` 和其 mutation、实体档、渲染档均通过。
T1 已合并主线并通过专项门禁：正常 `WEAPON-ANIMATION=PASS (6/6)`，实体 bind-pose 变异和
`--model` 逆绑定变异均被捕获；实体档与渲染档均通过。产品实体路径真实读数为
`gpuSkin=62/237`、`boneMismatch=0`、`advances=48→283`、`poseChanges=48→283`。

限制必须保留：当前见证帧只有 2/3 像素帧不同，部分武器/道具序列本身只有单一姿态块；
游标推进不等于可见动作。玩家动画仍因 demo 没有 `CTFPlayer` 动画输入而未验证，真实人工画面确认也未完成。
合并节点的总链是 1–21 步端到端通过，第 22 步随后以相同环境独立重跑通过；仓库没有把两段拼成一次虚假的 22/22 结论。

## 二、统一工作区规则

### 创建工作区

每个任务使用独立路径和分支。先创建工作区根目录，再从包含最新验收与诊断的分支创建任务 worktree；不设固定修改文件数量上限，只要求修改文件都属于当前任务边界并在报告中逐文件说明：

```powershell
New-Item -ItemType Directory -Force D:\TF2_Native_Worktrees
git worktree add -b task/<task-name> D:\TF2_Native_Worktrees\<task-name> origin/p0-entity-protocol
```

只有任务明确针对旧的实体材质产品整合树做比较时，才从该基线建 worktree：

```powershell
git worktree add -b task/<task-name> D:\TF2_Native_Worktrees\<task-name> origin/integration/entity-material
```

禁止：

- 修改 `D:\TF2_Demo_Player`。
- 直接在 `native-mvp` 或共享整合目录改 `native/`。
- 把 `ai-continuation` 整体覆盖当前代码。
- 一个任务同时修改实体协议、渲染器、BSP、音频和 UI。
- 在另一个 AI 的工作区运行会修改源码或重编译的脚本。

### 并发与构建

- `build-cmake.sh`、`build-target.sh`、`mutate.sh`、`verify-all.sh` 会重写二进制或源码，必须独占。
- `verify-all.sh` 和直接运行的 `mutate.sh` 共用 `.scratch/verify-all.lock`；锁存在时先确认进程，再决定是否清除。
- 不要在总链运行期间编辑脚本、源码、`evidence/` 或启动第二个 mutation。
- `native/` 头文件改动后必须全量 `bash build-cmake.sh`，不能相信增量 obj。
- 有声音的程序一律传 `--audio-device 6`；没有设备验证时使用 null sink。
- 非必要不运行 `verify-all.sh --quick`：它只用于合并主线、发版或核心协议变更；日常按分层门禁执行。
- `verify-fast.sh`、`verify-entity.sh`、`verify-audio.sh` 的探针不建窗口且不打开音频；`verify-render.sh` 及武器/ViewModel 抓帧门禁仍启动窗口并使用 `--audio-device 6`，直到测试静默模式任务完成。
- 清单二已完成：`--hidden-window --audio-disabled` 通过专项门禁，抓帧可无窗口、无设备；默认交互模式不变。现有其他抓帧脚本尚未统一传入这两个参数。清单一如修改 `main.cpp`，必须基于 `4dd79a9` 手动整合。

### 任务提交合同

每个任务必须返回：

1. 分支名、基线 commit、最终 commit hash。
2. 实际修改文件列表和每个文件的职责。
3. 真实 TF2 root、真实 demo、地图和 VPK 输入路径。
4. 可复制的构建与运行命令。
5. 原始输出中的具名读数和工作量计数。
6. 正向结果、负向/变异结果、失败原文和未验证部分。
7. 目标 exe 绝对路径、退出码、音频设备状态。
8. 明确“可以运行”还是“被什么输入阻塞”。

## 三、任务清单

### T0：验收链恢复与基线冻结（已完成）

**负责人**：整合 AI。
**工作区**：`D:\TF2_Native_Worktrees\acceptance-recovery`。
**边界**：只改验收脚本和文档，不改 `native/src`。

实施步骤：

1. 从 `origin/p0-entity-protocol@080f0fe` 创建工作区，确认 `git status` 干净。
2. 运行 `bash verify-fast.sh`，保存 `VERIFY-FAST=PASS` 和 `assertions_ok`。
3. 预置 `.scratch/verify-all.lock`，确认 `verify-all.sh` 和 `mutate.sh` 都以 2 拒绝；再确认锁可清理。
4. 清除锁后只启动一个 `bash verify-all.sh --quick`；整个过程不启动其他构建或 mutation。
5. 已完成：`VERIFY=PASS`、23/23、退出码 0、2h17m23s；当前证据基线为 `e4c4d41`，日志 sha256 `aff5fe6b40a4bea334feecd3f40df61a922b301204d2536927be68af85e4abd9`。第 8 步 `46/6/0/0/6`，第 23 步 111 条断言。

验收：

```bash
bash -n verify-all.sh
bash -n mutate.sh
bash verify-fast.sh
bash verify-all.sh --quick
```

变异要求：完整链第 8 步必须固定 `red=46 hold=6 green=0 broke=0 restored_identical=6`；任何数字变化都先修 pin 或查明用例变化，不能放宽断言。

### T1：武器/投射物骨骼动画接线

**负责人**：动画/实体 AI。
**工作区**：`D:\TF2_Native_Worktrees\weapon-animation`。
**允许文件**：动画解码模块、实体模型请求、`main.cpp` 的动画接线、renderer 骨骼上传、对应 probe/CMake；禁止改 BSP、音频、UI。

与 T2 有共享文件风险：动画 AI 与 ViewModel AI 可并行做资源调查、独立 probe 和 fixture；任何一方开始改 `main.cpp`、renderer 或共享模型请求类型前，先做小提交并通知整合者。两个分支不得各自复制相同接线后再整体覆盖。

#### T1 接入路线（保留现有 decoder，不重写）

当前已有 `native/include/animation_decoder.h`、`native/src/animation_decoder.cpp` 和
`animation_viewmodel_probe`。它们已经验证 RAWROT/RAWROT2、局部采样、父链位置和循环边界；
它们不是产品接线。按下面的边界接入：

1. **先接入构建**：把 `src/animation_decoder.cpp` 加入 `tf2_demo_native`，只增加编译依赖；先跑
   `build-target.sh tf2_demo_native` 和 `verify-fast.sh`，确认未改行为。
2. **建立模型缓存**：按 `modelPath` 缓存 `AnimationModel`、序列标签索引、渲染骨骼名到动画骨骼名的映射；
   不在每个渲染帧从 VPK 重新读取。模型包含的 `*_animations.mdl` 通过 decoder 的 include 名称解析，找不到时记录
   `animationSource=missing` 并保持 bind pose。
3. **建立骨骼映射**：以骨骼名称匹配 76 个动画骨骼到渲染模型骨骼；检查 parent 名称和顺序。渲染模型多出的
   helper bone 使用 bind transform；名称缺失、parent 不一致或骨骼数超过 128 时整实例拒绝动画，不部分猜测。
4. **从实体快照读取播放状态**：在 `EntityModelResolver` 的 `ModelInstance` 增加
   `hasSequence/sequence/cycle/playbackRate/poseParameters` 和诊断字段；读取真实发送属性时保留
   `lastWriteTick`。缺字段必须是 `animationInput=missing`，不能把 sequence=0 当作“正在播放 idle”。
5. **按 demo tick 采样**：将场景 tick 转换为统一的 demo tick rate，使用已有
   `sampleAnimation(model, sequenceIndex, tick, tickRate)`；`cycle`/`playbackRate` 若存在则作为采样时间，
   否则使用 decoder 的确定性 tick 采样。序列越界、frameCount/fps 非法和外部 anim block 都返回 bind pose。
6. **生成产品矩阵**：补一个与现有矩阵约定一致的 `composeLocalToModelMatrices`，输出完整旋转+平移的
   model-space 矩阵；按骨骼逐项计算 `skin[i] = animatedWorld[i] * poseToBone[i]`。不得用只组合位置的
   `modelPosition` 直接驱动 GPU，也不得重新发明转置约定；必须用 `skeleton_skin_probe` 的 identity/反序变异作门禁。
7. **先做单模型 MVP**：第一阶段只给当前 `--model` 路径上传一套矩阵，替换
   `main.cpp` 中 `uploadBoneMatrices(boneMatrices, false)` 为“有效动画时 true、无输入时 false”。标题增加
   `animSource/animSequence/animFrame/animBones/uploadedBones`，这样可以先证明产品 GPU 路径确实改变。
8. **再做实体实例**：当前 `EntityModelDrawInstance` 只有 origin/angles，不能承载每实例骨骼。新增
   `skeletonKey` 和矩阵范围，或新增独立的 `SkinnedEntityDrawInstance`；renderer 为每个模型/骨骼集合建立
   独立 constant-buffer/structured-buffer，不能用一套全局骨骼矩阵误画所有实体。没有动画输入的实例继续走
   bind-pose entity shader。
9. **武器/投射物优先**：从真实 demo 找到带非零序列或可解释静态序列的实体，记录 entity index、model path、
   sequence、cycle、sampled frame、bone count 和上传数。玩家 `CTFPlayer` 的动画属性在现有 POV/SourceTV
   语料中为空，必须保持 `animationInput=missing`，另列外部客户端预测任务。

T1 的最小完成判据是：一个真实武器或投射物实例的 `sampled frame` 随 tick 变化，GPU 上传数等于有效骨骼数，
   bind pose/动画矩阵变异可使门禁变红，并有前中后三张真实帧。只有探针通过而 `main.cpp` 仍上传 `false`，不能算完成。

前提：先读取 `D:\TF2_Native_Animation_Integration` 的 `2296e59`。该提交只提供诊断，不代表功能完成。骨骼数学探针证明矩阵契约，不证明主程序已接线或画面对。

实施步骤：

1. 从实体快照读取 `m_nSequence`、`m_flCycle`、`m_flPlaybackRate` 和 pose 参数；没有字段时返回明确的 bind pose 状态。
2. 用 `animation_decoder` 按 sequence 查找动画，执行帧号、循环、fps 和 section 边界处理。
3. 将动画局部位置/旋转按父骨骼顺序组合为 model/world 矩阵；未映射的 helper bone 保持 bind transform。
4. 计算 `skin = animWorld * poseToBone`，调用 `uploadBoneMatrices(..., true)`；禁止把数学探针的自写矩阵当成产品接线证据。
5. 对武器、投射物、可穿戴模型分别记录模型路径、骨骼数、序列、采样帧和上传数。
6. 增加一个错误输入门禁：序列越界、动画文件缺失、骨骼名称/parent 不匹配时拒绝播放并保持稳定 bind pose。

验收必须包含：

- `skeleton_skin_probe` 正常、`--mutation`、`--mutation-offset` 都按预期变红。
- 至少一个真实 demo 中 weapon/projectile 的 `sequence` 随 tick 改变或有可解释的静态序列。
- 标题或探针显示 `sampled frame` 递增；矩阵上传数等于有效骨骼数。
- 绑定姿势误差、矩阵刚性、循环首尾误差和缺序列负向测试。
- 真实帧抓取：动作前/中/后各一帧；不得只凭“探针通过”宣称画面正确。

> 本节保留 T2 实施时的原始步骤作为历史记录；当前状态以本计划下方的 T2.5 和文件顶部基线为准。第一人称绘制与动画输入拒绝契约已完成，真实 sequence 映射另列为未完成任务。

### T2：ViewModel 第一人称绘制（已完成接入，2026-10-11）

工作区：`D:\TF2_Native_Worktrees\viewmodel-render`；分支：`task/viewmodel-render`；产品基线：
`origin/p0-entity-protocol@5b58760`（T2 首个 probe 提交为 `2c87438`）。只允许修改 ViewModel、第一人称投影和对应探针；不得回写共享主线，
不得整体覆盖 T1 动画接线。

整合结果：来源提交 `9894c5b`，整合提交 `1b765d5`。`v_scattergun_scout.mdl` 已在主程序独立 pass
绘制，实测 `drawn=1`、`viewModelPass=6`、`afterWorld=1`、`vertices=9723`、`depthWrite=1`、
`depthFunc=less_equal`、默认 `fov=90`；缺资源、缺 companion、无 placement attachment 和短骨表均有稳定回退。
专项门禁 `viewmodel-contract-check.sh` 为 `154` 条断言通过，`--mutation` 为 `5/5` 捕获。
本段后续只保留材质、真实人工画面对照和 T3 动画输入接入，不得重复重写 ViewModel 模块。

执行顺序：

1. **资源契约**：将现有 `viewmodel_contract_probe.cpp` 接入 CMake，用真实 `v_*.mdl`、VVD、VTX 和材质 companion
   验证可加载数量；缺任一 companion 的 fixture 必须失败，不能把世界模型当 ViewModel。
2. **请求缓存**：在 demo/TF root 切换时调用一次 `buildViewModelRequest`，缓存 mesh、材质、attachment 和动画绑定；
   输出 `viewModelPath/companions/status/reason`，禁止每帧重新解析 VPK。
3. **独立绘制接口**：在 `native_renderer` 增加 ViewModel mesh、骨骼、投影状态和 draw pass；不得直接复用世界实体的
   `worldConstants_`、位置归一化、材质回退或深度状态。先以 flat color 证明 draw pass，再接 VMT/VTF。
4. **第一人称变换**：默认 FOV 为 **90**，并实现 40--120 夹取、attachment 到相机空间变换和最终左右手镜像；绘制顺序固定为世界 pass
   之后、HUD 之前，并输出 `drawn/depthWrite`。
5. **动画状态**：从同一实体快照读取 sequence/cycle/playback rate；有输入时复用 T1 动画绑定和矩阵，缺输入时明确
   `viewmodelAnimationInput=missing` 并稳定回退 bind pose，不按文件名猜动作。
6. **专项验收**：默认 FOV、40/120 边界、左右手、attachment 各抓真实帧；缺 companion、非 `v_` 路径、越界 FOV 做
   负向 fixture。正常门禁与 mutation 必须分别变红，`drawn=1` 必须来自实际 draw pass。

T2 交付后仍需在整合提交上运行 `bash verify-fast.sh`、ViewModel 专项门禁（正常与 `--mutation`）和 `bash verify-render.sh`；只有合并
主线时运行 `bash verify-all.sh --quick`，期间禁止其他构建或 mutation。

**负责人**：ViewModel AI。
**工作区**：`D:\TF2_Native_Worktrees\viewmodel-render`。
**允许文件**：`viewmodel.*`、模型材质请求、独立第一人称 draw pass、FOV/attachment、对应 probe；必须与世界实体材质路径分开。

与 T1 共用主程序和 renderer 边界；可并行完成接口设计、独立 probe 和真实资源盘点，主程序 draw pass 的共同文件按先 T1 后 T2 的顺序逐个整合、构建和回归。

#### T2 接入路线（保留现有 viewmodel 模块）

当前 `native/src/viewmodel.cpp` 只构造 renderer-neutral `ViewModelRequest`，不会绘制；
`native/include/native_renderer.h` 也只有世界模型的 `uploadBindPoseModel`/`uploadBoneMatrices`。
因此 T2 必须新增独立 pass，不能把 ViewModel 塞进实体世界实例路径：

1. **先补 probe target**：将已有 `native/tools/viewmodel_contract_probe.cpp` 加入 CMake，使用真实
   `v_*.mdl`、VVD、VTX companion 验证路径、attachment、FOV、左右手矩阵；缺 companion 的 fixture 必须失败。
2. **建立资源生命周期**：启动或换 demo 时调用 `buildViewModelRequest` 一次，缓存模型 mesh、材质、attachment
   和动画模型；禁止每帧解析 VPK。缺资源只禁用 ViewModel，不影响世界实体绘制。
3. **补独立渲染接口**：新增 `uploadViewModelMesh`、`uploadViewModelBones`、`setViewModelState` 和
   `drawViewModel`（名称可按本地风格调整）。ViewModel 使用独立 vertex/index buffer、skin constant buffer、
   projection constant buffer 和 material SRV；不要复用世界 `worldConstants_` 的位置归一化。
4. **建立第一人称投影**：ViewModel 矩阵使用窗口宽高和 `clampViewModelFov` 的 40--120 范围，应用 attachment
   到相机空间；左手只在最终 hand transform 镜像，不能修改世界实体骨骼。draw 顺序为世界 pass 后、HUD 前，
   深度状态使用独立配置并记录是否写深度。
5. **接当前武器状态**：从同一实体快照读取 sequence/cycle/playback rate；有状态时用 T1 的采样/矩阵缓存，
   没有状态时明确显示 bind pose 和 `viewmodelAnimationInput=missing`。不要根据文件名猜当前武器动作。
6. **保留材质隔离**：ViewModel 使用自己的 VMT/VTF 和材质常量，不能把世界 lightmap/cubemap fallback 当作
   ViewModel 成功；材质缺失时只回退到明确的 flat color，并计数。
7. **添加运行态读数**：标题或 trace 输出 `viewModelPath/companions/attachment/fov/hand/sequence/frame/
   bones/drawn`；`drawn=0` 时必须说明是缺资源、无实体状态还是 shader/上传失败。

T2 的最小完成判据是：真实 `v_*.mdl` 在默认 FOV、FOV 边界和左右手下各有一张帧，缺 companion 负向 fixture
   稳定回退；标题显示 `drawn=1`，并确认 ViewModel draw pass 实际发生在世界 pass 之后。

实施步骤：

1. 解析真实 `v_*.mdl`、VVD、VTX 和材质 companion；缺任一文件时返回失败状态并保留世界渲染。
2. 建立独立 viewmodel projection，不复用世界实体的深度/裁剪假设。
3. 应用 FOV 范围 40--120、左右手镜像和 attachment 变换，记录最终矩阵。
4. 按当前武器实体的 sequence/cycle 选择换弹/攻击动作；没有 demo 属性时显示 bind pose 并记录输入缺失。
5. 增加真实 v_* 模型的见证帧和错误 companion 的负向 fixture。

验收：

```bash
bash verify-fast.sh
native/build-nmake/viewmodel_fov_probe.exe
```

`native/tools/viewmodel_contract_probe.cpp` 当前存在，但尚未作为 CMake 可执行目标；本任务需将它接入构建，并由 probe 验证真实 `v_*.mdl` companion 链。不能把源码文件存在当作已构建或已验收。

另需真实截图：默认 FOV、FOV 边界、左右手、attachment 对齐、缺资源回退；截图标题必须包含 demo tick 和 model path。

#### T2 第二笔：静态武器本体（已完成并合并，2026-10-13）

工作区：`D:\TF2_Native_Worktrees\weapon-body-render`；分支：`task/weapon-body-render`；基线
`origin/p0-entity-protocol@9a8387b`。

现代 TF2 第一人称是合成体：实体 `m_nModelIndex` 给出手臂 `c_*_arms.mdl`，武器实体的
`DT_BaseCombatWeapon.m_iWorldModelIndex` 给出静态武器本体。本轮把它接成第二笔独立 draw request，
与手臂共用 attachment/FOV 参数但拥有自己的 mesh、radius、near/far 和 projection：

1. **输入**：`findViewModelWeaponBodyInput`（`native/src/entity_model.cpp`）从 ViewModel 实体经
   `m_hWeapon` 走到武器实体，读 `m_iWorldModelIndex`，12 个具名拒绝分支
   （`NoViewModelEntity` … `WorldModelIsArms`）；不变式为 `worldModelPath 非空 ⟺ 成功`，
   失败时不回传任何真实路径。
2. **绘制**：`native/src/native_renderer.cpp` 的 `drawViewModel` 内第二笔 `Draw`，独立顶点缓冲、
   独立 projection、skin 常量清零、独立计数 `armDrawCalls`/`weaponDrawCalls`。
3. **读数**：`weaponModelIndex`/`weaponModelPath`/`weaponMaterials`/`weaponFallback`/`weaponVisible`/
   `weaponReason`/`weaponEntity`/`weaponClass`/`weaponMeshRadius`/`weaponVertices`/`weaponClip`；
   拒绝时清空上一把武器的槽位。
4. **开关**：`--viewmodel-weapon-body on|off`（默认 on；off 是 A/B 对照）。
5. **门禁**：`weapon_body_probe`（自检 57 项 + 真实资源 117 项）与 `weapon-body-check.sh`
   （61 条断言；`--mutation` 4/4 捕获，104 条断言）。

验收（2026-10-12 本机）：构建 rc=0、`WEAPON-BODY=PASS`、`MUTATION-CAUGHT=PASS (4 defects)`、
`verify-fast.sh` = `VERIFY-FAST=PASS (assertions_ok=43)` rc=0。关键读数：
`armDrawCalls=weaponDrawCalls=1379`、`weaponModelIndex=533` → `c_pro_rifle.mdl`、
`byte_diff(on, off)=42209`、`byte_diff(on, on-repeat)=0`；`body-off`/`m1`/`m3` 三帧 sha256 相同
（`1d2cab7d…`）。本轮未跑 `verify-render.sh` 和总链；人工画面对照、武器切换场景、真实 GPU 性能
未验证。

### T2.5：ViewModel 动画输入契约（拒绝路径已完成，真实映射未完成）

主线整合：`p0-entity-protocol@150f0c7`；产品提交 `447ca74`；门禁/交付提交 `2a92967`。专项门禁读取 `CTFViewModel.m_nSequence`、`m_flPlaybackRate`，不可信序列回退 bind pose；五层调查新增 `vmselfsum`/`vmself-outlier`，证明按实体自身模型比较才是有效读数。主线 `verify-fast.sh` 43/43、`verify-render.sh` 8/8；正常门禁与独立/父锁 mutation 均通过。总链已在 `e4c4d41` 上 23/23、rc=0、`VERIFY=PASS`。动态 `c_*_arms.mdl` 产品绘制仍未实现。

真实 `v_*` 映射调查已完成：demo、items_game 和加密 weapon ctx 没有可靠的狙击枪 `v_*` 数据源，实体 `c_*_arms.mdl` 也没有可连接的 activity 名称。下一项改为同伴证明和动态手臂绘制：实体序列只对实体自身模型表生效；禁止把 `m_nSequence` 直接当作 `v_*.mdl` 索引，也禁止把 `--viewmodel-apply-animation` 当作正式能力。

### 当前接手步骤

1. ~~在 `D:\TF2_Native_Worktrees\viewmodel-sequence-mapping` 开展 T2.5 映射调查~~：已完成（主线 `150f0c7`），结论见 `T2.5-MAPPING-RECON-2026-10-12.md`。
2. ~~为每层建立合成 fixture 和反向 mutation，先证明错误映射会红，再接入产品~~：已完成；结论为"实体序列只对实体自身模型表生效，禁止按文件名猜动作"，缺输入保留 bind pose 回退。
3. 定向通过后更新 `README.md`、`CHANGELOG.md`、本计划和 `HANDOFF-2026-10-07.md`；只有合并节点、发版或核心协议变更才重跑总链。
4. 测试专用 hidden-window/null-sink 已由 `4dd79a9` 完成；后续抓帧门禁优先使用该模式，真实设备验收仍单独传 `--audio-device 6`。
5. 2026-10-13 批次：T1 ViewModel 材质接入、T2 静态武器本体和 T3 音频主程序接线均已合并到 `69baff8`；当前下一步先补跑渲染档，再安排 T4 真实画面对照。真实设备音频、人工画面和完整总链状态必须分开报告。抓帧门禁若断言同一 tick 的多张帧，必须按请求 tick 重试对齐（`--capture-tick N` 是"≥ N"，落点会随机器负载漂移）。
### T3：相机、观察者模式与基础 HUD/UI

**负责人**：UI/相机 AI。
**工作区**：`D:\TF2_Native_Worktrees\camera-ui`。
**允许文件**：`native_ui.*`、相机输入/状态、HUD 绘制和设置存储；禁止改 PacketEntities 解码。

实施步骤：

1. 保持 TF2 demo 相机的 tick 驱动：第一人称、第三人称、观察者自由视角分别保存状态。
2. 空格单击切换第一/第三人称；双击空格进入观察者自由视角；P 键切换暂停。双击判定必须有明确时间窗口并可测试。
3. 观察者目标无有效实体时不搬移相机，保留当前视角并显示状态；不能从单一死亡相机 demo 推断目标接线正确。
4. 补基础 HUD：播放/暂停、tick、demo 名、相机模式、资源缺失和错误状态；窗口缩放/DPI 不重叠。
5. 所有控制路径增加键盘 fixture 或可重复输入脚本。

验收：

- `native_ui_probe.exe` 返回 0。
- 记录单击、双击、P 键事件序列和最终状态。
- 1280x720、1920x1080、2560x1440 截图无文字重叠；暂停时 tick 不推进。
- 缺观察目标、缺地图和无 demo 三种负向状态都稳定回退。

### T4：世界材质与地图光照

**负责人**：BSP/材质 AI。
**工作区**：`D:\TF2_Native_Worktrees\world-materials`。
**允许文件**：`bsp_map.*`、`vmt_material.*`、`vtf_texture.*`、world shader/probe；禁止改实体协议和音频。

实施步骤：

1. 先固定 BSP/VPK 输入，记录 BSP 字节数、面数、材质数、lightmap 数量和可解像素数。
2. 把 RGBExp32 lightmap 接到逐面 shader，曝光和缺 lightmap 回退必须显式记录。
3. 实现 cubemap 六面发现与采样，先用仓库内合成六面 fixture 验证顺序。
4. 分别实现 skybox、displacement、water capability；水面 RT 和 VIS clipping 未完成前不得宣称 Source 等效。
5. 1024+ 材质与缺失 `pak01_dir.vpk` 必须有资源可达性读数，不能用全屏 fallback 的彩色多样性冒充地图画面。

验收：

```bash
bash verify-render.sh
bash resource-reachability-check.sh
native/build-nmake/world_material_probe.exe --self-test
```

必须同时提交合成 fixture、真实 BSP 输出、缺资源输出和帧抓取差异；没有 BSP 的 demo 只能标“资源不可达”，不能标渲染通过。

### T5：Demo 音频与临时效果

**负责人**：音频 AI。
**工作区**：`D:\TF2_Native_Worktrees\audio-effects`。
**允许文件**：音频资源、时间线、TempEntity/PCF、audio probe；禁止改协议 bit reader 和 renderer。

实施步骤：

1. 从 demo 的 sound index/string table 建立到 VPK WAV 的可复核映射；每条记录 name、tick、来源表和解析状态。
2. 按 network tick 调度 weapon、footstep、Uber、TempEntity 和 PCF；去重必须保留原因。
3. 明确过滤 voice、announcer、music 的规则，不按文件名猜类型。
4. 使用 null sink 或 `--audio-device 6`，禁止测试默认 `WAVE_MAPPER`。
5. 缺 WAV、坏 WAV、超长 WAV 和重复事件都必须稳定失败/丢弃并计数。

验收：

```bash
bash verify-audio.sh
native/build-nmake/audio_effects_probe.exe --self-test
native/build-nmake/audio_scheduler_probe.exe --self-test
```

真实 demo 至少覆盖一段开火、脚步、Uber、临时效果；输出必须含 `events_seen`、`resolved`、`played`、`filtered`、`failures` 和 `device_opened`。

### T6：稳定性与发布

**负责人**：稳定性 AI。
**工作区**：`D:\TF2_Native_Worktrees\stability-release`。
**允许文件**：门禁脚本、启动包装、安装/发布文档；正式解析和渲染逻辑只有复现 bug 时才允许修改。

实施步骤：

1. 运行 10 分钟以上真实 demo soak，记录 tick、帧数、working set、崩溃和进程退出。
2. 对截断、空文件、损坏 header、缺失 TF root、移动 TF root、缺 BSP/VPK 逐项测试。
3. 测试 WARP、真实 GPU、无音频设备和指定设备；记录设备打开状态。
4. 验证安装包双击启动、资源路径、日志目录、退出后无残留进程。
5. 所有缺输入必须 FAIL 并保留原始错误，不允许 SKIP 变绿。

验收：

```bash
bash verify-fast.sh
bash verify-entity.sh
bash verify-render.sh
bash verify-audio.sh
```

长时和安装验收须提供开始/结束时间、exe 路径、退出码、峰值内存、帧/tick 统计和失败原文。

### T7：PaintKit 与高级画质

**负责人**：材质/效果 AI。
**工作区**：`D:\TF2_Native_Worktrees\advanced-materials`。
**允许文件**：PaintKit/schema/material shader/particle/water；禁止重写实体协议。

实施步骤：

1. 由 items schema 和真实实体属性确定 PaintKit，不得按文件名猜材质。
2. 逐项接 Phong、bump、selfillum、cubemap、水面和粒子；每项独立 capability state。
3. 固定纹理夹具和真实 VPK 对照，缺 shader/纹理保留基础材质回退。
4. 每项改动提供像素差异和负向变异；不能只检查 shader 编译成功。

验收：

- 真实模型/地图输入的材质链解析计数完整。
- 每项效果都有合成 fixture、真实帧和禁用效果对照帧。
- 缺资源、越界参数和错误 PaintKit 不崩溃。

### T8：主线整合与发布门禁

**负责人**：主线整合 AI。
**工作区**：`D:\TF2_Native_Worktrees\release-integration`。
**职责**：不直接开发功能，只合并通过验收的专项提交。

整合顺序：先整合协议/实体专项并跑实体档，再整合渲染/BSP 并跑渲染档，随后整合音频和 UI；动画与 ViewModel 的共同主程序改动必须逐提交审查，禁止一次性覆盖整棵 `native/`。任何专项缺少真实输入、负向结果或“是否可运行”结论时，保持在专项分支，不进入产品基线。

步骤：

1. 检查专项交接清单是否包含本文件要求的 8 项交付信息。
2. 用 `git merge-tree` 检查目标基线与专项提交；协议、渲染、BSP、音频、UI 分开 cherry-pick。
3. 在已提交干净树运行 `bash verify-fast.sh` 和对应二级门禁。
4. 只有合并主线、发版或修改核心协议时运行 `bash verify-all.sh --quick`；运行期间禁止其他构建/变异。
5. 检查 `git diff --check`、工作树干净、证据文件时间一致，再推送 GitHub。
6. 更新 `README.md`、`HANDOFF-2026-10-07.md`、本文件和变更日志；通过结论必须来自实际命令输出。

## 四、统一验收分层

### 第 1 档：每次小改动

```bash
bash verify-fast.sh
```

目标：秒级构建和 36 条核心自检。失败时先修本任务，不启动总链。

### 第 2 档：专项完成

```bash
bash verify-entity.sh
bash verify-render.sh
bash verify-audio.sh
```

只运行与改动相关的脚本；`--mutation` 必须单独运行，不能和另一个 mutation 并发。门禁输出放到 `evidence/fast/` 或脚本指定的 OUT 目录，不得覆盖已提交的 `evidence/`。

### 第 3 档：合并/发版/核心协议修改

```bash
git status --porcelain
bash verify-all.sh --quick
```

要求：工作树干净、锁不存在、没有 `bash`/`nmake`/探针进程、输入 demo 名单固定、最后出现 `VERIFY=PASS` 且退出码为 0。完整链中任何一步失败都保留 `VERIFY=FAIL`，不得手工修改证据后重写结论。

## 五、统一变异和证据标准

每个新门禁必须先做反向验证：

1. 正常代码得到具名读数和工作量计数。
2. 变异只破坏一个行为，门禁必须变红。
3. 变异恢复后源码、二进制输入和正常读数必须恢复。
4. 空输入、错路径、缺资源、越界值必须失败，不得打印 SKIP/PASS。
5. 变异断言数量写死；新增用例同时修改 pin 和文档。
6. 读数必须绑定真实输入样本、源码 commit、命令和输出哈希；exe 哈希因 MSVC 时间戳不可作为唯一锚点。

## 六、接手者最终回复模板

```text
任务：
工作区/分支：
基线 commit：
最终 commit：
修改文件：
真实输入：
构建命令与退出码：
运行命令与退出码：
已验证读数：
变异/负向结果：
未验证与外部依赖：
已知失败原文：
目标 exe 绝对路径：
是否可运行：是/否；若否，阻塞原因：
```

没有填写完整模板、没有真实输入和反向验证的提交，只能留在专项分支，不能进入 `native-mvp` 或 `integration/entity-material`。
