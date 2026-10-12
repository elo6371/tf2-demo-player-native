# 更新日志

## 2026-10-12

### T2.5 ViewModel 动画输入契约已整合

- 主线整合提交：`f5e427f`；来源产品提交 `447ca74`，门禁/交付提交 `2a92967`。
- 产品读取 `CTFViewModel.m_nSequence` 与 `m_flPlaybackRate`，区分 `missing`、`ok`、`sequenceOutOfRange`、`boneMismatch`；越界序列稳定回退 bind pose。
- 真实 POV 读数确认实体序列空间与绘制的 `v_scattergun_scout.mdl` 序列表不同（绘制模型 7 序列，实体模型 43 序列；观察到 0--34，104 次越界）。没有把数值落在范围内误认为映射。
- 门禁：主线 `verify-fast.sh` 43/43、`verify-render.sh` 8/8、T2.5 正常 PASS；独立和父锁 mutation 均 `red=3/3 hold=3/3`，源码恢复和恢复构建通过。
- 修复的问题：父级持构建锁时 mutation 恢复责任被错误跳过；pin 文件 CRLF 使 Git Bash 读到 `name\r` 并误报 demo 缺失；变异模式仍按正常版断言拒绝状态。
- 未完成：真实序列映射、ViewModel 材质与人工画面对照、完整 23 步总链、玩家动画外部输入。下一步先跑干净主线总链，再开独立 worktree 做映射调查。

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
- 静默边界：fast、entity、audio 契约探针不建窗口且不打开音频；需要抓帧的 render/weapon/ViewModel 门禁目前仍启动主程序并使用 `--audio-device 6`，尚未实现隐藏窗口和 null sink 产品模式。
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
