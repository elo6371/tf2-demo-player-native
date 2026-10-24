# T57 运行时审计报告 — 玩家模型 / 动画源 / demo 可达性

任务：task-9（T57 运行时阶段）。静态阶段报告见 `evidence/t57-player-model/REPORT.md`。
本报告只覆盖**运行时探针与门禁**：不改产品渲染行为，只增加只读探针输出与一个审计型门禁。

## 0. 身份与范围

| 项 | 值 |
| --- | --- |
| owner | t57-playermodel |
| worktree | `D:/TF2_Native_Worktrees/t57-model` |
| branch | `task/t57-model` |
| base commit | `99eeea294cbe883e4c3e6b4b82c83de45d2ab926`（= p0-entity-protocol HEAD，未变基） |
| 改动文件 | `native/tools/entity_model_probe.cpp`（+304）、`native/tools/entity_overlay_probe.cpp`（+76）、新增 `player-model-check.sh`、新增 `evidence/t57-player-model/runtime/` |
| 产品源码改动 | **无**。只动两个 `native/tools/` 探针与新增门禁脚本；`native/src/` 一个字节没改 |
| 构建 | `bash build-target.sh entity_model_probe entity_overlay_probe model_robustness_probe` → rc=0 |
| TF2 资产根 | `D:/SteamLibrary/steamapps/common/Team Fortress 2/tf` |
| demo | bagel `4a9bfb9276509d0ec5f5fdc722a95b17_match-20260927-0239-koth_bagel_rc13.dem`（46921111 B，112396 ticks）、snakewater `bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem`（51834617 B） |

### 非目标
- 不实现第三人称修复、不调相机、不改 `native/src/` 的模型/材质/骨骼链路。
- 不做像素对比（需要产品渲染，见 §8）。
- 不跑完整 gate 链；只跑本任务新增的 `player-model-check.sh` 与其 4 个负控制。
- 不覆盖 viewmodel（`viewmodel-robustness-check.sh` 的声明范围与本任务无关）。

## 1. 结论摘要（probe 能证明 / 只有产品渲染能证明）

**probe 能证明（本次实测）**
1. 九职业 player 模型全部**静态可装载**：9/9 `resolution=FoundVpk`、companion 五件套齐全（`vvd/dx90/dx80/sw/phy` 全 1）、`renderable=1`、`mdlVersion=48`、`vvdLods=7`、`vtxLods=7`。
2. **九职业 LOD0 全部被 12000 顶点/索引预算截断**：`lod0Built=12000`，`lod0Truncated=9`，`lod0LostTotal=107829`（单职业 603–20724 个 render index 丢失）。
3. **产品只画材质 slot 0**：`productPaintedSlots=1`（常量，见 `native/src/main.cpp:3749-3750`、`:5003-5025`）；九职业 mesh 实际用到 1–6 个 slot（`slotsUsedByMeshes`），MDL 声明 8–26 个 skin family。Heavy 与 Spy 的 slot0 是**头贴图**（`heavy_head_red` / `spy_head_red`），其余 7 职业是 `*_red` 身体贴图。
4. **玩家身体没有任何可用的动画源**：9/9 `animationInput=ok` 但 `animSeqs=2`、`boundSeqs=2`、`separateAnimSource=0`——即玩家模型自带的 `ref`/`ragdoll` 两个单帧 stub，真正的 `<class>_animations.mdl`（scout 377 条全绑定）从未被采纳。根因见 §5。
5. **demo 里的玩家实体没有 `m_nModelIndex`**：`missingModelIndex` 等于全部行数（bagel 39/39、snakewater 20/20），模型只能靠 `m_iClass` 职业 fallback（`classFallback` bagel 36、snakewater 14）。
6. **两份 demo 里 4 个职业 0 行**：Sniper(2)、Pyro(7)、Spy(8)、Engineer(9) 在两份 demo 的任何采样 tick 里都不出现；实际可达只有 Scout(1)、Soldier(3)、Demoman(4)、Medic(5)、Heavy(6，仅 snakewater 1 行)。
7. **有实体但无模型 = 只有 marker**：bagel `markerOnly=3`、snakewater `markerOnly=6`，这些行 `m_iClass` 缺失或越界（playerclass 0 行数 = bagel 3、snakewater 6），永远画不出身体。

**只有产品渲染能证明（未做，见 §8）**
- 截断后的 12000 个 render index 在屏幕上到底缺哪块几何（缺的是靠后 bodypart，静态可推，但视觉确认需要抓帧）。
- 单 slot 上色在屏幕上的具体表现（整身红/头被涂成身体色）。
- 第三人称相机能否真正框住玩家身体、近裁剪是否切掉身体。
- 骨骼矩阵/pose 在屏幕上的正确性。

## 2. 改动清单（全部在 worktree 内，未提交前状态）

### 2.1 `native/tools/entity_model_probe.cpp`（+304 行）
新增 `--player-model-audit` 与 `--class-table <path>`：
- 九职业默认表来自产品自己的 `EntityModelResolver::defaultPlayerModelPath`（`native/src/entity_model.cpp:589-602`），保证探针与产品同源，不复制魔法表。
- 每职业一行固定字段（门禁 grep 用），字段全部来自真实解析：`ModelLoader::resolveAsset` + `inspectVpk`（MDL/VVD/VTX header）、`buildBindPoseMeshLod0`（12000 上限）、`ModelMeshMaterialTable`、`textureCandidates`、VMT→VTF 解析。
- 缺模型分支：`candidate.resolution == ModelAssetResolution::Missing` 直接算未解析，打印 `resolved=0 renderable=0 missing=1`。
- 退出码：0 = 9/9 解析且 renderable；1 = 有职业未解析/不可渲染；2 = tf-root 或 class-table 输入错误。

### 2.2 `native/tools/entity_overlay_probe.cpp`（+76 行）
demo 模式新增**纯增量**行（不改任何既有行，既有 gate 的 grep 不受影响）：
```
tick models requested=%d rows=%zu withModel=%zu markerOnly=%zu missingModelIndex=%zu classFallback=%zu
demo models rows=%zu withModel=%zu markerOnly=%zu missingModelIndex=%zu classFallback=%zu
demo class name=%s rows=%zu withModel=%zu markerOnly=%zu
demo playerclass tfClass=%d rows=%zu withModel=%zu markerOnly=%zu
demo team name=%s rows=%zu withModel=%zu markerOnly=%zu
```

### 2.3 `player-model-check.sh`（新增，审计型）
四段：1/4 九职业静态审计；2/4 九职业动画源；3/4 demo 实体→模型可达性；4/4 四个负控制（默认跑，`--skip-negative-controls` 可跳过）。
设计原则：**断言主体存在**（9 行、rc、summary 计数、demo rows/withModel），**缺陷只发布不判死**（lod0Lost、slot0、markerOnly 打印出来但不让门禁失败）——因为这些都是产品当前的真实缺陷，门禁的职责是让它们可见且不可悄悄变化。

## 3. 命令、退出码与关键读数

| # | 命令 | rc | 证据 |
| --- | --- | --- | --- |
| 1 | `bash build-target.sh entity_model_probe entity_overlay_probe model_robustness_probe` | 0 | configure+build 全绿，三个 target 首编通过 |
| 2 | `entity_model_probe.exe --tf-root <tf> --player-model-audit` | 0 | `runtime/player-model-audit.txt`（10 行） |
| 3 | `model_robustness_probe.exe --tf-root <tf> --model models/player/<class>.mdl` ×9 | 0 | `runtime/player-model-anim.txt`（9 行） |
| 4 | `entity_overlay_probe.exe --demo <bagel> --ticks 5000,20000,40000,60000,80000,100000` | 0 | `runtime/player-model-demo-bagel.txt`（60 行） |
| 5 | `entity_overlay_probe.exe --demo <snakewater> --ticks 64230` | 0 | `runtime/player-model-demo-snakewater.txt`（35 行） |
| 6 | `bash player-model-check.sh` | 0（GATE_RC=0） | `runtime/gate-run.txt`（61 行） |

### 3.1 九职业静态审计（`player-model-audit.txt`）

| class | tfClass | mdl | bones | seqs | tex | bodyParts | attach | lod0Idx | lod0Lost | slotsUsed | skinRefs | skinFam | slot0 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Scout | 1 | scout.mdl | 78 | 306 | 17 | 5 | 29 | 23442 | 11442 | 4 | 17 | 8 | scout_red |
| Sniper | 2 | sniper.mdl | 74 | 240 | 20 | 5 | 20 | 23595 | 11595 | 6 | 20 | 8 | sniper_red |
| Soldier | 3 | soldier.mdl | 86 | 361 | 20 | 5 | 24 | 26922 | 14922 | 6 | 20 | 8 | soldier_red |
| Demoman | 4 | demo.mdl | 84 | 276 | 17 | 4 | 25 | 25755 | 13755 | 4 | 17 | 8 | demoman_red |
| Medic | 5 | medic.mdl | 92 | 213 | 21 | 2 | 20 | 24864 | 12864 | 5 | 21 | 8 | medic_red |
| Heavy | 6 | heavy.mdl | 79 | 259 | 21 | 3 | 20 | 28944 | 16944 | 5 | 21 | 8 | **heavy_head_red** |
| Pyro | 7 | pyro.mdl | 76 | 270 | 8 | 5 | 24 | 12603 | 603 | 1 | 8 | 8 | pyro_red |
| Spy | 8 | spy.mdl | 84 | 274 | 31 | 2 | 27 | 16980 | 4980 | 5 | 31 | 26 | **spy_head_red** |
| Engineer | 9 | engineer.mdl | 73 | 351 | 15 | 3 | 13 | 32724 | 20724 | 3 | 15 | 8 | engineer_red |

汇总行（原文）：
```
player-model-audit summary classes=9 resolved=9 renderable=9 companionsComplete=9 lodCountKnown=9 vvdLodsMin=7 vvdLodsMax=7 vtxLodsMin=7 vtxLodsMax=7 lod0Truncated=9 lod0LostTotal=107829 slot0Resolved=9 productPaintedSlots=1
```

字段语义（避免误读）：
- `lod0Built=12000` 是**产品 API `buildBindPoseMeshLod0` 的默认上限**（`native/src/model_loader.cpp:1215`），不是资产顶点数；`lod0Lost` = `lod0RenderIndices - 12000`。
- `productPaintedSlots=1` 是**代码常量**（`main.cpp:3749-3750` 只取 `textureCandidates.front()`，`main.cpp:5003-5025` 只解析 slot 0），不是探针测量值；探针把它打出来是为了让"产品只画一个槽"这件事在读数里可见。
- `skinRefs/skinFamilies` 来自 MDL header（offset 220/224），产品**完全没有使用**（`native/src/` 全库无 skinref/skinfamily 读取）。

### 3.2 九职业动画源（`player-model-anim.txt`，全部 rc=0）

九行全部是：`animationInput=ok animPath=models/player/<class>.mdl separateAnimSource=0 animSeqs=2 boundSeqs=2 bindBones=<n>/<n>`。
即：**动画"解析成功"但只有 2 条自带 stub 序列**，真正的 companion 未被采纳。根因见 §5。

### 3.3 demo 可达性（`player-model-demo-*.txt`）

**bagel**（6 个采样 tick 中 3 个可用）：
```
demo models rows=39 withModel=36 markerOnly=3 missingModelIndex=39 classFallback=36
demo playerclass tfClass=0 rows=3 / 1 rows=12 / 3 rows=12 / 4 rows=6 / 5 rows=6
demo team blue rows=18 / red rows=18 / unknown rows=3
demo summary ticks=6 rows=39 distinctEntities=13 withTeam=39 red=18 blue=18 unknown=3 withPose=39 exact=0 checkpointFallback=3 unavailable=3 appeared=13 vanished=0 ticksWithRows=3 rowsMissingTeam=0 rowsMissingPose=0 teamMappingMismatches=0
```
采样点 `5001/20001/40001` 全部 `fidelity=unavailable reason=the tick precedes every retained snapshot`（demo 的保留快照从更晚开始）；`60001/80001/100001` 有行，`fidelity=checkpointFallback stale=856`。

**snakewater**（1 个 tick）：
```
demo models rows=20 withModel=14 markerOnly=6 missingModelIndex=20 classFallback=14
demo playerclass tfClass=0 rows=6 / 1 rows=3 / 3 rows=5 / 4 rows=2 / 5 rows=3 / 6 rows=1
demo team blue rows=6 / red rows=6 / unknown rows=8
demo summary ticks=1 rows=20 distinctEntities=20 withTeam=20 red=6 blue=6 unknown=8 withPose=20 exact=0 checkpointFallback=1 unavailable=0 appeared=20 vanished=0 ticksWithRows=1 rowsMissingTeam=0 rowsMissingPose=0 teamMappingMismatches=0
```

**可达职业结论**：两份 demo 合计只出现 tfClass 1/3/4/5/6；**2(Sniper)、7(Pyro)、8(Spy)、9(Engineer) 为 0 行**——不是"没测"，是采样到的每一行都不含这些职业（`demo playerclass` 行是逐职业全表，缺的职业根本没有该行）。这直接限制了动作样本的结论范围。

### 3.4 门禁与负控制（`gate-run.txt` 末 6 行原文）
```
  OK   wrong-tf-root failed rc=2 with 'player-model-audit FAIL tf-root is not a valid AssetRoot'
  OK   wrong-demo failed rc=2 with 'the demo header did not parse'
  OK   class-table-8 failed rc=2 with 'class-table entries=8 expected=9'
  OK   class-table-missing-model failed rc=1 with 'class models did not resolve'

PASS player-model-check: 9/9 class models loaded and renderable; sampled players resolved; defects published above.
```
四个负控制都**以命名原因**失败，说明断言不是恒真。

## 4. demo 可达性对动作样本的含义

- 可做动作样本的职业：**Scout / Soldier / Demoman / Medic**（bagel + snakewater 都有），Heavy 只有 snakewater 1 行（1 个 tick，且 `checkpointFallback`）。
- 不可做：Sniper / Pyro / Spy / Engineer——两份 demo 0 行，任何"这四个职业动作正常/异常"的结论都是**没有主体**的，按规则记为 NOT RUN。
- 所有 demo 行都是 `fidelity=checkpointFallback`（`exact=0`），stale 856/510 ticks：**位置是回退到 checkpoint 的，不是逐 tick 精确值**。pose 相关结论必须带这个前缀。

## 5. 玩家动画源断裂 — 根因（本次最重要的代码级发现）

### 5.1 现象
9/9 职业：`animationInput=ok` 但 `animSeqs=2 separateAnimSource=0`；而单独探测 companion：
```
models/player/scout_animations.mdl      animSeqs=1012 boundSeqs=377 bindBones=76/76
models/player/soldier_animations.mdl    animSeqs=858  boundSeqs=419 bindBones=86/86
```

### 5.2 代码路径（`native/src/animation_binding.cpp`）
- `:189-191`：`for (const auto& include : model.includes) { if (include.find("_animations.mdl") != npos) { companionCandidate = include; break; } }` —— **取第一个**名字里含 `_animations.mdl` 的 include。
- `:222-277`：模型自带动画非空时，只有 `nameBound > boundInBase(anims)` 才采纳 companion（`:252`）。
- `:193-221`：自带动画为空时直接加载 companion。

### 5.3 TF2 九职业的真实 include 顺序（python 直接读 MDL header 336/340，stride 8）
```
scout:    models/player/scout_user_animations.mdl | models/player/scout_animations.mdl | models/workshop/player/animations/scout_workshop_animations.mdl
sniper:   models/player/sniper_user_animations.mdl | ...
soldier / demo / medic / heavy / pyro / spy / engineer 同构，全部 3 个 include，_user_animations 排第一
```
九职业**全部**如此（`evidence/t57-player-model/runtime/tools/include_list_all.py` 的输出）。

### 5.4 为什么这会让采纳失败
`_user_animations.mdl` 的名字里含 `_animations.mdl` 子串 → `:190` 选中它：
```
models/player/scout_user_animations.mdl  bones=69 seq=1 bound=1   ← 被选中
models/player/scout_animations.mdl       bones=76 seq=377 全绑定  ← 从未被读
```
`nameBound` 对 `_user_animations` 最多 1，而 `boundInBase(anims)=2` → `1 > 2` 为假 → **不采纳**，回落到自带的 2 条 stub。

### 5.5 复刻验证（python，按产品 decoder 的同一套 offset 语义）
`runtime/tools/sequence_label_join.py`：
```
models/player/scout.mdl   modelSequences=306 modelBound=2  companion(scout_animations)=377 全绑定  nameBound=303
models/player/soldier.mdl modelSequences=361 modelBound=2  companion(soldier_animations)=419 全绑定 nameBound=353
```
即：如果 `:190` 选中的是 `<class>_animations.mdl`，`nameBound=303 > 2` 会立刻采纳，玩家身体将拥有 303 条按名字绑定的真实动画。**选中 `_user_animations` 是唯一的断点。**

### 5.6 影响
- 玩家实体 `m_nSequence` 若 ≥ 2，采样返回 `SequenceOutOfRange`/失败 → 走 bind pose。
- 现有 trace 的 `targetPose=0 targetAdv=0` 不能归因于"玩家不会动"，因为动画 witness 白名单本身排除了玩家（`main.cpp:8558-8561` 只含 weapons/items 路径）。
- 这是**独立于 demo 属性缺失**的第二条断链：即使给玩家实体补上 `m_nSequence`，当前代码也放不出动作。

### 5.7 建议修复（不在本任务范围，交 Lead 排期）
`animation_binding.cpp:189-191` 改为遍历**所有** include + 命名约定候选，逐个算 `nameBound`，取最大者（> `boundInBase` 才采纳）。预期读数：9/9 `separateAnimSource=1`、`animSeqs` 从 2 变为 300+、`boundSeqs` 从 2 变为 300+。

## 6. 8 类 gap 更新（静态报告 §5 的运行时补充）

| 类别 | 状态 | 运行时证据 |
| --- | --- | --- |
| no draw path | **有 draw path，但玩家身体永远走 marker 或 bind pose** | 探针：`missingModelIndex`=全部行；`classFallback` 生效；`markerOnly` bagel 3 / snakewater 6 |
| wrong entity->model | 部分（fallback 正确，但 8/20 实体无 class） | `demo playerclass tfClass=0` bagel 3 / snakewater 6 |
| mesh or LOD missing | **确认** | `lod0Truncated=9 lod0LostTotal=107829`；`native/src/model_loader.cpp:1215/1229-1230` |
| wrong material slot | **确认** | `productPaintedSlots=1`，`slotsUsedByMeshes` 1–6，`skinFamilies` 8–26 未使用；Heavy/Spy slot0 是头贴图 |
| bone binding or pose | 未测（需要产品渲染；静态 9/9 bindBones 匹配） | `bindBones=<n>/<n>` 全匹配 |
| animation time advance | **确认断链** | §5：`animSeqs=2 separateAnimSource=0`，9/9 |
| camera or near-clip | 未测（需要产品渲染） | 静态已知 `kWorldNearMapUnits=4.0`、第三人称 `setOrbitPose` |
| input lacks the state | **确认** | demo 玩家实体无 `m_nModelIndex`（39/39、20/20），且无 `m_nSequence`（`evidence/animation-availability/bagel.txt:102`） |

## 7. probe 与产品渲染的边界（避免过度声明）

probe 读的是**资产 + 产品解析代码**，不是屏幕。以下必须由产品渲染证明，本报告**不下结论**：
- 截断的几何在屏幕上具体缺哪一块（静态可推"靠后 bodypart 丢"，但未目视）。
- 单 slot 上色后的实际颜色分布（红/蓝队、头/身体错位）。
- 第三人称相机能否框住身体、近裁剪是否切掉。
- 骨骼矩阵在 GPU 上的最终姿态。

## 8. NOT VERIFIED（明确未做）

1. **没有任何像素/帧证据**：未启动 `tf2_demo_native.exe`，未抓帧（本阶段无 GPU/窗口授权）。
2. **第三人称相机实际构图**未验证（静态代码链见 `evidence/t57-player-model/REPORT.md` §2）。
3. **Heavy 只有 1 行、且是 checkpointFallback**：Heavy 的 demo 侧结论强度低于其余四个职业。
4. **bagel 的 3 个早期采样点不可用**（`tick precedes every retained snapshot`），bagel 的实际可观测窗口比文件 tick 范围窄。
5. **`m_nSequence` 是否真的出现在任何 demo 玩家实体上**：沿用 `animation-availability-check.sh` 的既有结论（0 个 slot），本任务未重新测。
6. **修复 `:190` 后的效果**未验证（需要改产品源码 + 重建 + 抓帧，本任务禁改 `native/src/`）。
7. **`lod0Lost` 的几何影响**未目视。

## 9. 构建 / 进程 / 锁结束状态

- 构建：`build-target.sh` rc=0，产出 `native/build-nmake/{entity_model_probe,entity_overlay_probe,model_robustness_probe}.exe`。
- 进程：探针进程全部正常退出；`hostlock status` 显示 `product_processes=0`。
- 锁：我于 12:34 获得 `host.lock`（owner=t57-model），**12:5x 已 release**（`RELEASED owner=t57-model`），release 后 `FREE`，队列 head 交给 `t66-camera`。未跨阶段空持。
- 窗口/音频：本任务全程未启动产品，因此不涉及 `--hidden-window --audio-disabled`；探针为纯 CPU/文件解析。


### 9.1 二进制与输入哈希（sha256）

| 文件 | sha256 |
| --- | --- |
| `native/build-nmake/entity_model_probe.exe` | `2a3a25977bffb8824de296f30e34a0ec5f1bc0f88e4aa6ca41560034927be300` |
| `native/build-nmake/entity_overlay_probe.exe` | `5d02237fef587966d931e180060ea48eb70ef4a95df0b727b538f2f20a8af8da` |
| `native/build-nmake/model_robustness_probe.exe` | `48fd87fca8bcc94617694db8d96ffb87c0ed0e7ef1b64f2e35247be7d487e270` |

demo 与 TF2 资产根路径见 §0；demo 文件大小已记录（46921111 / 51834617 B）。

## 10. 下一步建议（交 Lead 排期）

1. **P0 修复动画源选择**（`animation_binding.cpp:189-191`）：候选全部 include + 约定，取 `nameBound` 最大。预期 9/9 `separateAnimSource=1`。改动小、收益大，且能被现有 `player-model-check.sh` 2/4 段直接读出前后差异。
2. **P0 提高 mesh 预算或按 LOD 分块上传**：12000 上限对九个职业全部不够（最小 Pyro 也超 603）。需产品改动 + 抓帧验证。
3. **P1 材质按 skin family 上色**：读 MDL 220/224 + `ModelMeshMaterialTable`，按 bodypart 的 `material` 槽取 VMT，而不是只取 slot0。
4. **P1 给玩家实体补 `m_nModelIndex` 的替代路径**：既然 demo 里没有该属性，职业 fallback 是唯一路径；需要保证 `m_iClass` 缺失的 8/20 实体有可诊断的 reason（当前已是 `no m_nModelIndex slot; class fallback model path`）。
5. **P2 第三人称抓帧**：用 `--camera-script` + `bookmark.mode=third-person` 固定身份，对 bagel 60001 拍一张，验证身体是否入画。

## 11. 产物清单

| 路径 | 内容 |
| --- | --- |
| `runtime/player-model-audit.txt` | 九职业静态审计 10 行（9 职业 + summary） |
| `runtime/player-model-anim.txt` | 九职业动画源 9 行 |
| `runtime/player-model-demo-bagel.txt` | bagel 6 tick 全量 |
| `runtime/player-model-demo-snakewater.txt` | snakewater 1 tick 全量 |
| `runtime/player-model-demo.txt` | 两 demo 合并（gate 3/4 段） |
| `runtime/gate-run.txt` | `player-model-check.sh` 完整输出（含 4 负控制） |
| `runtime/negative/` | 4 个负控制的输入与日志 |
| `runtime/tools/sequence_label_join.py` | 复刻产品 sequence 名字连接（预测 nameBound） |
| `runtime/tools/include_list_all.py` | 九职业 include 顺序（根因证据） |
| `runtime/tools/include_list.py` | 单模型 include 明细 |
