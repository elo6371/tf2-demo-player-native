# 更新日志

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

- 已创建 `D:\TF2_Native_Worktrees\viewmodel-render` / `task/viewmodel-render`，基线 `origin/p0-entity-protocol@bd48e42`。
- 已有 `viewmodel.cpp`、`viewmodel.h` 和诊断 probe，但尚未进入主程序第一人称 draw pass。
- 实施顺序：资源 companion 契约 → 请求缓存 → 独立 ViewModel draw pass → FOV/attachment/左右手 → T1 动画状态复用 → 真实帧和缺资源负向验收。

## 2026-10-09

- 完成实体材质接线、资源可达性判据、分级验收脚本和主循环空转修复；详见 `HANDOFF-2026-10-07.md` 与 `IMPLEMENTATION-PLAN-2026-10-10.md`。
