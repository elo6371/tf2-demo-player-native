# 更新日志

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
