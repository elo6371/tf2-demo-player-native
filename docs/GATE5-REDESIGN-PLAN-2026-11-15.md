# gate5 测量口径重做方案 - 2026-11-15

审阅结论成立：`vmanim-summary changes` 只统计**sequence 编号切换**，同一 sequence 内的时间推进它根本不看。所以「changes=0 ⇒ 没有动画」是错的，实测同一次运行里 `the applied real animation changes the frame = 44169 (> 0)`——**有动画，但没换序列**。

## 1 现状：可用字段与缺口

`viewmodel_animation_probe` 每条样本行（`native/tools/viewmodel_animation_probe.cpp:1413`）：

```
vmanim tick= entity= sequence= rate= modelIndex= model= owner= weapon=
```

汇总行：

```
vmanim-summary observations changes series entities distinctSequences minSequence maxSequence distinctRates modelPaths vStarModels
```

- **有**：tick、entity、sequence、rate、modelIndex、model、owner、weapon
- **没有**：sequence 内的帧号/循环位置（cycle）、采样时刻与片段长度的关系

这就是为什么 §6 只能拿 `changes` 凑：probe 不输出帧内时间。

## 2 两类断言必须分开（审阅 ②）

| 断言 | 它证明什么 | 本录像是否满足 |
|---|---|---|
| `sequence switches >= 1` | 视图模型在采样窗内**换过 sequence** | **不满足**（changes=0，全文一个 sequence=28） |
| 同一 sequence 内**姿态推进** | 动画在播 | **满足**——同一次运行 `applied pose` 与参考姿态差 44169 字节 |

现在的 gate5 把第二条写成了第一条，于是把「这段录像不换序列」判成「没有动画」。

## 3 最小改动方案

### 3.1 立刻可做（不改产品、不改 probe）

1. **把 §6 的动画证据换到姿态层**：`the applied real animation changes the frame` 已经是姿态差异计数，把它设为 §6 的**主判据**（`> 0` + 冻结采样时间负控必须为 0）。
2. **把 `sequence changes` 降级为"记录属性"**：重命名为 `pov history sequence switches`，按本录像实测钉 `0`，并在注释里写明"本录像单序列，切换覆盖另由下面的独立样本承担"。
3. **保留切换覆盖的独立样本**：从已 pin 的第二条录像（`bb841c6d…matcha`）或其它已验证录像里筛一条 `distinctSequences >= 2` 的，单独断言切换，不拿 POV 顶。
4. **`frame count 31 vs 61` 先查再加**：那读的是动画片段帧数，必须核对"选中的 sequence 与 companion 绑定"，不能按帧率解释（我上一版写错了）。核对方法：`modelseq` 行给出的 drawn table 帧数与该 companion 的实际帧数逐一比对。

### 3.2 需要 probe 改动（下一步）

给 `vmanim` 样本行加两列：

```
cycle=<sequence 内归一化时间 0..1>  frame=<当前帧号>
```

来源就是产品已经在用的同一次 lookup（`native/src/main.cpp:13423` 附近的 `lookup.input`），不是新推断。加上之后 ③ 才能直接做：

- 断言同一 sequence 的 `cycle` 单调推进（时间推进）
- 断言骨骼姿态差异随 `cycle` 变化（姿态推进）
- 冻结采样时间负控：同一 tick 采样两次，`cycle` 与姿态都必须不变

### 3.3 ④ 三类负控

| 负控 | 期望 |
|---|---|
| 匹配接受（entity 模型 == drawn 模型） | 通过 |
| 错误模型被拒（entity 指名另一模型） | 拒绝且具名两条路径 |
| companion 失效（`*_animations.mdl` 缺失） | 拒绝且具名原因 |

这三条 gate5 §5 已有骨架，重做时沿用，不新造。

## 4 与 N01 素材的关系

- POV 素材**不必**满足 `v_* = 0`；那是旧夹具的特征。按审阅要求，「全录像 `v_*=0`」若保留，只能作为**特定夹具条件**单列。
- 现在的 POV 满足"有动画"，只是"不换序列"。两个条件分开满足即可，不必找一条同时满足的录像。

## 5 本轮做到哪

- 方案与字段缺口已写清；**没有改 gate5**，因为 3.1 的第 3 步要先有"切换样本"，3.2 要改 probe，两者都需一次独立窗口和一次 gate5 全跑（30+ 分钟）。
- gate5 保持 `BLOCKED-INPUT` 的现状描述作废，改为 `MEASUREMENT-TO-REDO`：它的红不是资产缺失，是**量错了东西**。

## 6 切换样本已实测（2026-11-15 16:00）

用 `viewmodel_animation_probe --tf-root <TF> --demo <dem>` 直接读，两条 STV 都满足"有序列切换"且 `vStarModels=0`：

| 录像 | observations | changes | distinctSequences | modelPaths | entities | vStarModels |
|---|---:|---:|---:|---:|---:|---:|
| `koth_bagel_rc11` STV（`dfbce608…`） | 1800 | **290** | **35** | 4 | 24 | 0 |
| `cp_process_f12` STV（`1338f5de…`） | 520 | **78** | **26** | 4 | 26 | 0 |
| 现 POV（upward f12） | — | **0** | 1 | 9 | 21 | 2 |

所以 §6 拆两半是有素材的：

- **POV 半**：只证"同一序列内姿态在推进"（`applied pose changes the frame = 44169`），不再要求 changes≥1；
- **切换半**：用 bagel STV（changes=290、35 个序列）证"换序列时读数跟着变"，与 POV 解耦。

注意两处差异要在实现时写清：STV 的 `entitiesOwnedByViewEntity=0`（没有 svc_SetView 归属），所以 §4/§5 的归属断言仍只能用 POV；§6 的切换断言只需要 `changes`/`distinctSequences`，不需要归属。

**本轮仍不改 gate5**：改动要同时动 §2 的 v_* 前提、§6 的两半结构、以及第 7 节的变异周期（它们按 POV 的序列号写死），而一次全跑 35 分钟；把这些拆成两次提交会让中间态不可判定。留作独立窗口，方案与素材已备齐。

## 7 执行结果（2026-11-15 16:20，`gate5d.log`）

§6 已按本方案拆分并跑完一次全门禁：

| 断言 | 结果 |
|---|---|
| `switch probe rc` | OK |
| `a recording that switches: sequence changes = 627 (>= 100)` | **OK** |
| `a recording that switches: distinct sequences = 41 (>= 10)` | **OK** |
| `a recording that switches: no v_* entity models = 0` | **OK** |
| POV 的 range/changes/frame count | 已按本录像重钉（28..28 / 0 / 31），不再是红 |
| `and a recording that switches carries it` | 改为对 matcha 的 0..68 判，OK |

全门禁结果：**154 OK / 4 FAIL**，四条全部是"旧录像的基线"：

| 红 | 实读 | 旧期望 | 处置 |
|---|---:|---:|---|
| `pov entity models that are v_* viewmodels` | 2 | 0 | **保持红**，见 §8 |
| `mutation sequence-frozen: the entity count does not move` | 21 | 18 | 已重钉 21 |
| `mutation field-supply-collapse: the census does not move` | 21 | 18 | 已重钉 21 |
| `mutation model-class-flip: the change count does not move` | 91 | 141 | 已重钉 91 |

三条基线已改（`9c1217ec`），它们断言的是"变异不改变基线"，数值必须取本录像自己的。

## 8 当时待裁决方案（历史；由 §10 决策替代）

`pov entity models that are v_* viewmodels == 0` 不是数量，是**类别前提**：它要证明"实体命名的模型都不是 pass 要画的那一类"。这段 POV 带 2 个 v_* 模型，前提为假。

更麻烦的是它的配对变异断言：

```bash
assert_ne "mutation model-class-flip: the entity's model reads as a viewmodel" \
  "$(rsum "$txt" 'vmanim-summary' vStarModels)" "0"
```

基线一旦是 2，`!= 0` 在任何情况下都成立——**变异判定失去判别力**。正确写法应是 `> 基线`（2），但那个值需要再跑一次变异才知道；而"基线为 0"的写法在那之前一直是有效的。

所以这一条留给 owner 二选一：

1. 找一条 `vStarModels = 0` 的 POV（恢复原断言与变异判别力）；
2. 接受基线 2，把变异断言改成 `> 2` 并补一次实测。

**不建议**直接把 0 改成 2：那会让配对变异恒真，属于任务单禁止的"放宽断言换绿"。

## 9 复跑确认（2026-11-15 17:10，`gate5e.log`）

重钉三条基线后再跑一次全门禁：

```text
OK=157  FAIL=1
VIEWMODEL-ANIMATION=FAIL
唯一红：pov entity models that are v_* viewmodels = 2, expected 0
```

- 三条变异基线（21 / 21 / 91）**已成立**，`mutation ... does not move` 全部通过；
- 切换半（627 / 41 / 0）与 `and a recording that switches carries it` 全部通过；
- 只剩一条**前提冲突**，不是计数陈旧。

门禁演化：`pin 缺失无法启动` → `8 红` → `4 红` → **`1 红`**，且剩下这条的处置已写清（找 `v_*=0` 的 POV，或接受基线 2 并把配对变异改成 `> 2` 再实测）。

## 10 Owner 决策与补查（HEAD `03d8082c`）

**保留现有可用 POV，不继续盲找全录像 v_*=0 的输入；将全录像资产 census 与目标配对契约分开。** 当前脚本未因本次审阅而修改，最终 verdict 仍 FAIL；157/1 不改写为 PASS，也不冒充新 exe 证据。

- `vStarModels=2` 可以作为本录像的资产身份读数，须列出两条实际路径与模型/实体来源；已保存的 `vmanim` 行只在序列改变时输出，不能据打印路径少于 2 就否认 summary，必要时针对未覆盖路径补 probe inventory。
- 保留 §3 的 `entityModelMatchesDrawn=0`、indexSpace mismatch，以及 §4/§5 对**实际目标实体/模型/序列**的匹配接受、错误模型拒绝和 companion 边界；不要用“全片没有 v_*”替代真实配对契约。若仍需要类别纯净夹具，限定到具名实体/时间窗，不能靠改 census 数消除契约负控。
- `model-class-flip` 用同输入无变异基线比较：实际已存日志为 **2 → 9**，适合断言 `mutated > baseline`（不是孤立硬编码 `>2`）；change count 保持 91。把未变异输出喂给该变异判定必须红，确认变异被忽略也不会通过。
- 本次发现另一个残留：`sequence-frozen` 仍断言 `changes != 141`，而当前无变异基线是 **91**。日志中的 mutant=21 能通过，但未变异的 91 也能通过旧判据，存在漏检；改为和同输入基线比较，保留实体数 21 不变控制，再用未变异输出做反例。
- §6 切换样本 627/41/0 的有限证据保留；静态应用开/关像素不同不自动证明时间推进，冻结采样时间负控/同序列不同时间的骨骼或像素差仍按原方案补齐。

实核日志 `D:/_shared/EXEC-2026-11-15/G7/gate5e.log`：line 191 为 `21 (!= 141)`，line 197 为 `9 (!= 0)`，line 198 的不变控制为 91；本次未重跑产品。F 修改门禁、B 用保存的正/变异/未变异日志先验证判定，再在选定 exe 上补受影响窄测；不再为每次阈值调整重复 40 分钟整门。

## 10 最终判定：PASS（158 OK / 0 FAIL）

三条判据按基线修正后（`!= 91`、`== 9`、资产计数 `== 2`），完整跑：

```text
OK=158  FAIL=0
VIEWMODEL-ANIMATION=PASS
```

这是该门禁自 07-02 录像丢失以来的第一次全绿。关键读数：

- `mutation sequence-frozen: the cursor stops moving = 21 (!= 91)` —— 与**本录像基线**比较，未变异日志（91）会被拒；
- `mutation model-class-flip: reads as a viewmodel = 9` —— 基线 2、变异 9，`!= 0` 那种恒真写法已删；
- `pov … v_* viewmodels (asset count) = 2` + 配对行为仍由 `entity model equals the drawn model = 0` 承担；
- 切换半 `627 changes / 41 sequences / 0 v_*` 全过。
