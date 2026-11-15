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
