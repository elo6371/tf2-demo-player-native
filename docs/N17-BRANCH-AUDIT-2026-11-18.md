# N17 两条分支审验记录（2026-11-18）

审验对象是 `work/n17-anim` 与 `work/n17-draw` 两条并行分支。做法：各起一个审验
worktree，把分支与 `work/integrate`（c609e9f8）合并、全量构建、跑门禁与探针、跑真实
录制的产品帧，然后把两条分支按同样方式合回 `work/integrate` 再复跑一遍。本文记录
看到的数字、发现的问题和处置，不重复分支自己的设计文档
（`docs/N17-ANIMATION-2026-11-18.md`、`docs/N17-DRAW-STAGE-2026-11-18.md`）。

## 0. 结论摘要

| 分支 | 起点 HEAD | 内容 | 审验结论 |
| --- | --- | --- | --- |
| `work/n17-anim` | f7b40844（**实现未提交**） | 时钟速率、locomotion blend、capped aim layer | 通过，但实现是审验时才补提交的（§1） |
| `work/n17-draw` | 1f625c39 | bodygroup 选择器、具名 untextured range、FOV 自证 | 通过（§4） |

合并后 `work/integrate` = **24d3b686**。合并树增量构建 rc=0，三道门禁全绿：

```
PLAYER-ANIMATION-CHECK=PASS
COMPANION-LOOP=PASS compared=303 mismatches=0 loopSample=1 clampSample=1 loopMutation=1 clampMutation=1
POSE-FAMILIES=PASS classes=9 crouchClasses=9 flinchSequences=14 sampled=40 decoded=40 failed=0
```

## 1. 发现一：anim 分支的实现根本没在提交里

`work/n17-anim` 的 HEAD 是 f7b40844「park idle-rate and blend scaffolding, not
compiled, not verified」——这句话是字面意思：**提交里只有脚手架和头文件，真正让时钟
跑起来的改动还躺在工作树里**（8 个文件 +689/−37 未提交，另有一份未被跟踪的设计文档）。

第一次合并只拿到 f7b40844，构建出来的探针 `idleRate=0.000000`，idle 仍是冻结的，而
门禁照样全绿——因为门禁断言的正是「idle 一秒前进 0」，**冻结的 idle 也能满足断言**。
这就是「假绿」：门禁没有锚在任何只有新代码才成立的数字上（真正的锚是 §2 的
`idleRate=0.666667` 与 `idle tick delta == idleRate/tickRate`）。

处置：在 `D:/TF2_Native_Worktrees/t105-n17anim` 用 `git diff --binary` 导出
`.scratch/anim-impl.patch`，在审验树 `git apply -3`（7 文件全部 clean 应用），
提交为 **a3472baa**。**教训：审验一条分支，先看 `git status`，别只看 HEAD。**

## 2. anim：门禁与探针（审验树，已含实现）

`bash player-animation-check.sh` → `PLAYER-ANIMATION-CHECK=PASS`，且
`BINARY-FRESH=1`（探针比它读的源码新）：

| 段 | 断言 | 实测 |
| --- | --- | --- |
| A | 每职业序列表 ≥200，bindBones == bones | 九职业全部满足 |
| B | 四分之一周期的四个姿态互异、元素有限、不超出骨架 bound sphere | 通过 |
| C | 125 u/s 下每 1/60 秒任一矩阵元素变化 ≤ boundDiag/4 | engineer 14.595675 ≤ 38.382599 |
| D | movingRate > idleRate；moving 一秒 +1% 周期；idle 一秒恰好 0 | `idleRate=0.666667`（= idle 片段自身 cycleRate）、`idle tick delta=0.011111111 == idleRate/tickRate`、连续四个 tick 都前进 |
| E | 有速度前进、无速度冻结、周期永不倒退 | synthetic 61 ticks：movingAdvanced=30 / idleAdvanced=0；demo 窗口 60000,62000：transitions=39 entities=13 movingAdvanced=26 staleTicks=1997 |

可复现性：`clock sequence repeatable = b8b2728794fbe3a90aa37512332e1289`。
三个命名负控（tf-root-invalid / model-missing / sequence-out-of-range）都按名字失败。

```
model_pose_probe.exe --player-animation-test --tf-root <tf> \
  --model models/player/scout.mdl --companion-loop-test
⇒ COMPANION-LOOP=PASS compared=303 mismatches=0 loopSample=1 clampSample=1 \
  loopMutation=1 clampMutation=1
T62_POSEFAMILY_OUT=.scratch/anim-audit/families bash class-pose-family-check.sh
⇒ POSE-FAMILIES classes=9 crouchClasses=9 flinchSequences=14 sampled=40 decoded=40 failed=0
```

即 61b0dbfc 的 companion 循环标记修复经过 anim 合并后仍然成立，两类反向变异都 KILLED。

## 3. anim：产品级 trace（真实录制，审验树）

命令（单次 5–8 分钟，trace 93 MB / 529,329 行）：

```
TF2_NATIVE_TRACE=<out> tf2_demo_native.exe \
  --tf-root "D:/SteamLibrary/steamapps/common/Team Fortress 2/tf" \
  --demo "E:/demo测试/pl_upward_f12/STV/a9630c65...match-20261030-2145-pl_upward_f12.dem" \
  --player-animation --capture-tick 16773 --capture-frame <out> \
  --audio-disabled --hidden-window --world-water off
```

| 指标 | 值 |
| --- | --- |
| `anim: pick` | 9（九职业各一条） |
| `anim: blend` / `anim: frame` | 45960 / 45960 |
| 每条 blend 的 `sum` 与 `appliedSum` | 全部 1.000000（0 例外） |
| 同 tick 确定性 | 160 对重复 (tick, ent) 全部逐字节相同 |
| 姿态是否在采样 | 15 个身体各 20 个互异 poseHash / cycle |
| aim 层 | >0 有 43960/45960 行，`maxAim=0.500000` == `kAimLayerWeightCap` |
| 层数 / 丢弃 | layers=3:43061 / 1:2000 / 2:899；dropped=2:2899 / 1:43061 |

### 发现二：walk 档在九职业都没有解析出来

每一行 `anim: pick` 都是 `walkSeq=-1`、`walkLabel` 空（engineer/pyro/soldier/
heavy/demo/medic/spy/scout/sniper 全部），因此 45960 条 blend 行**全部**
`walkFallback=1`，seqs 形如 `60,80,80,7`（idle,walk,run,aim 里 walk==run）。

所以「三档 locomotion blend」在这份数据上实际是 **idle/run 两档 + aim 一档**。
分支文档只写了 Scout 没有 stand `walk_PRIMARY`，实测是九职业全都没有。代码行为是
正确的（显式回退到 run 并在 trace 里报 `walkFallback=1`），属于**能力局限而非缺陷**，
但「三档」的说法要按实测改口径；要真的做出 walk 档，得先确认这批 `_animations.mdl`
里到底有没有可用的 stand walk 动作。

### 发现三：门禁 E 段标签与断言相反

`player-animation-check.sh:431` 原标签是「E demo printed tick lines whose advanced=
field parsed」，而断言是 `bad_parse == 0`——标签读起来像「必须 ≥1 条」。已改为
「…could not be parsed (bad_parse, must be 0)」，提交 **979c6904**。

### 发现四：`--player-animation` 仍是产品默认关闭的开关

合并这条分支**不改变默认帧**：不带该开关的跑法仍然走默认 oste 路径。动画要在产品里
默认可见，得先让它过一遍默认路径的验收（那是另一张卡）。

## 4. draw：合并冲突与抓帧正控

`work/n17-draw` 与 c609e9f8 只在 `native/src/native_renderer_frame.cpp` 冲突：draw 的
`capture: fov` 自证行正好落在我为世界手性修复加的 clip-X 取反旁边。两边都保留——
FOV printf 在前，`XMMATRIX projection`（去掉 const）随后，clip-X 取反在后
（merge commit 9634b56e）。全量构建 rc=0，exe_count=54。

同一 worktree 抓两帧（`--debug-camera 500 -2240 240 17.8 127.97 --capture-tick 1
--player-animation --animation-drive states --hidden-window --hud-off`）：

| 帧 | sha256 | 说明 |
| --- | --- | --- |
| p9-default | d2d7e8808eb2169292362b0076465c9c7911bd34a4f22d5e313f4c8c8e4f22ed | 默认 body |
| p9-body2 | 09f89b4ef5c4413a4b2b48242d0bfc058cca70989de54ad38e01b5f3060573bf | `TF2_NATIVE_BODY_MUTATION=2` |

像素差：**441 px（>8 的阈值）、maxDelta 187**，全部落在 bbox (544,109)-(662,298) 的
118×189 人物区域内，占全帧 860,784 px 的 0.05%——bodygroup 选择器只换人物身上的部件，
不是整帧噪声。分支文档报的「488 px」用的是非零阈值，两个数不矛盾。

配套证据：trace `gpu: entity bodyMutation forced=192 body=2`；`scene: entityRanges
untextured=1 first=models/weapons/c_models/c_ubersaw/c_ubersaw_xmas.mdl#2112110215#0#body=0#slot=2#verts=2676#entity`
（把过去口头传的「312/313 条」坐实为 1 条已具名 range）；日志首行

```
capture: fov source=settings.worldFov horizontal=90.0000 vertical=73.7398 \
  rule=sourceHorizontalFovToVertical aspect=1.856094 near=0.0006 far=100.0000
```

注意这行是 `std::printf` 到 stdout/日志的，**不在 TF2_NATIVE_TRACE 文件里**；
`range: ordinal=` 默认 0 行，因为 `TF2_NATIVE_RANGE_LIST` 是 opt-in。

## 5. 合入 `work/integrate`

```
git merge --ff-only audit/n17-anim     # c609e9f8 → 979c6904（10 files +1096/−50）
git merge audit/n17-draw               # 冲突：native/src/main.cpp:13905-13953
```

`main.cpp` 的冲突是「两段代码粘在同一个 if 上」：anim 把 `anim: blend` trace 块插在
`if (!sampled.bindPose && !sampled.skinMatrices.empty())` 之前，draw 把这个 if 改成了
带 `entityBindPoseMutation` 的分支。解决为——先打 trace，再走变异分支：

```cpp
if (request.blendLayerCount > 0 && blendTraceLines < 8) { ...trace... }
if (!sampled.bindPose && !sampled.skinMatrices.empty() && entityBindPoseMutation) {
  ++entityBindPoseMutationInstances;
} else if (!sampled.bindPose && !sampled.skinMatrices.empty()) { ...skinning... }
```

（第一次解决时漏掉了 trace 块的收尾大括号，构建立刻 C2181/C1075 报出来——增量构建
在合并后必须实跑，不能只看 diff。）

合并树（24d3b686）增量构建 rc=0，三道门禁复跑全绿（见 §0）：两支共存没有回归。

## 6. draw 的算术正控：在合并树上复跑，精确复现

§4 的像素正控之外，draw 分支还有一份**算术**正控（逐条 range 账本按职业聚合）。
它在合并树（exe sha256 `f77e4294…53a557`）上重跑，两次抓帧 rc=0：

| 职业 | 文档值（单实例） | 合并树实测 | 一致 |
| --- | --- | --- | --- |
| engineer | 4→4 / 22314→31662（**+9348**） | 4→4 / 22314→31662（+9348） | ✅ |
| scout | 8→7 / 23442→21900（−1542） | 8→7 / 23442→21900（−1542） | ✅ |
| pyro | −792 | 5→4 / 12603→11811（−792） | ✅ |
| demo | −1488 | 5→4 / 25326→23838（−1488） | ✅ |
| sniper | −690 | 7→6 / 19902→19212（−690） | ✅ |
| heavy / medic | 0 | 6→6 / 21564→21564、5→5 / 24864→24864 | ✅ |
| soldier | 文档漏列 | 6→5 / 24930→21834（−3096） | 文档待补 |
| spy | 文档漏列 | 4→4 / 16080（0） | 文档待补 |

- 全场景 **318 → 309** ranges（trace `scene: entityRanges` default `ranges=318`、
  body2 `ranges=309`；账本 317→308，差的 1 条是 `ubersaw_xmas#slot=2` 无
  textureView、dump 前被 `continue`，与 draw 文档 §4 的解释一致）。
- **账本逐字节相同**：default.log 与 body2.log 的 sha256 等于 n17draw 原证据的
  `p3/range.log` / `p6/body2list.log`——同一个 bodygroup 选择器在合并树上给出同一份账。
- trace：body2 有且仅有 1 行 `gpu: entity bodyMutation forced=192 body=2`，default 0 行。
- 差异：文档表是**单实例**值，engineer 场景有 #0/#1 两个实例、各 +9348（按职业汇总
  +18696）；soldier 的 −3096 在 n17draw 原树上同样存在，是文档表漏列，不是合并引入。
- 合并树的像素差是 530 px（原 488 px）：底图含世界手性/材质等其他改动、sha 不同，
  两侧不要求相等。

## 7. 这次审验没做的事

- walk 档缺失（§3）没有修——九职业的 animation 表里只有 `PRIMARY_aimmatrix_walk` /
  `ReloadAirwalk` 一类，没有 stand walk 循环，属素材能力，另开卡。
- `--player-animation` 默认开启（§3 发现四）没动，需要先过默认路径验收。
- draw 文档的算术正控表缺 soldier / spy 两行（上表已补齐），按需回填原文。
- 大 trace 与抓帧仍留在 `.scratch/` 下，不签入；本文只留数字与复现命令。
