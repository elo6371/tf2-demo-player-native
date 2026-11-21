# 非特效饰品（wearable）渲染计划：先证明录像里有饰品，再谈骨骼合并（2026-11-21）

状态：**计划，未开工**。首版只做"普通模型饰品"（帽子/徽章/背包一类纯模型，无粒子、无附加模型、无自定义渲染）；凡是资源缺失、客户端专有外观、粒子/附加模型，一律输出明确的"不支持/资源缺失"状态，不静默换成近似帽子。

## 0 结论：链路有四段已存在，缺的是"识别 + 归属 + 骨骼合并"

提案（2026-11-21）里的数据链，本仓已经有一半以上：

| 能力 | 本仓位置 | 状态 |
|---|---|---|
| 实体身份 `(entityIndex, generation)` | `native/include/demo_header.h:455-463`、`:950-951`（generation 每次 Enter/Delete 递增） | 已有 |
| 类名（classId → name） | `native/src/demo_header_strtab.cpp:766-771` 遍历 `serverClassSchemas` | 已有 |
| `m_nModelIndex` → modelprecache 路径 | 属性读取 `native/src/demo_header_strtab.cpp:798`；世界模型优先 `m_iWorldModelIndex`（`:824-880`，含 "view/index 两个槽位" 的取舍注释）；查表 `precachePathFor`（`:882-890`） | 已有 |
| `m_iItemDefinitionIndex` | `native/src/demo_header_strtab.cpp:917` | 已有（只读进来） |
| item schema 候选 | `native/src/item_schema.cpp`；`ItemSchema::modelCandidates`（`native/include/item_schema.h:49-55` 的 `modelCandidates`），入口 `native/src/main.cpp:5181`、命中计数 `:5186` | **半有**：见下方风险 1 |
| owner / parent handle | `m_hOwnerEntity` 被跟踪（`native/src/demo_header_track.cpp:150`），`AssetReference.ownerHandle`（`native/include/demo_header.h:384-390`）；事件里 `m_hOwner`（`native/src/demo_header_events.cpp:73`）；按槽读 `entity_model.cpp:355`；父链 `m_hMoveParent` `entity_model.cpp:886` | 读得到，但没用于饰品 |
| 父骨骼附着 | `native/src/entity_model.cpp:890-892`：`m_iParentAttachment > 0` 直接拒绝（"map model requires a parent bone attachment"） | 拒绝，不是实现 |
| 逐实例骨骼上传 | `native/src/main.cpp:14371` `draw.boneOffset`、`:14818` `setEntityBoneMatrices`；`native/include/native_renderer.h:1049` | 已有 |
| 模型骨骼名表 | `entityBoneNamesByCacheKey`（`native/src/main.cpp:5677-5684`），`ModelBone.name`（`native/include/model_loader.h`） | 已有 |
| 玩家每 tick 骨骼矩阵（模型空间） | `sampled.boneModelMatrices`（`native/src/main.cpp:14460-14530` 一带动画采样） | 已有 |
| 附着矩阵合成 | `findHoldPoint`（`native/src/main.cpp:13400-13440`，weapon_bone 附件→骨骼兜底）+ `multiplyTransforms`（`:14526`，T83 持枪） | 已有（**刚性**附着） |
| wearable 类识别 / 饰品归属 / 骨骼合并 | `grep -rni wearable native/src` 只有"目标类"注释；`grep boneMerge` 0 命中 | **缺** |

结论：**不用重新造 modelprecache / schema / 骨骼上传**；缺的第一块是"录像里到底有没有 wearable 实体、能不能认出来"，这决定后面全部工作是否值得做。

## 1 风险 1（必须先说清）：schema 候选目前不参与渲染

`ModelLoader::resolveAssetReference`（`native/src/model_loader.cpp:1281-1311`）：**有 `hasModelPath` 就在第一行 return**（`:1283`），item schema 只在"连接型录像连路径都没有"时才被查；而且查到了也只返回 `resolution = Unknown` + `diagnostic = "items_game candidate only"`（`:1294-1302`）。`buildRenderRequests` 又直接跳过没有 model path 的引用（`:1329`）。

所以提案里"item schema 的模型候选解析已经存在"是**对的一半**：候选表存在、能按 `itemDefIndex` 查到 `modelPlayer/modelWorld/modelPlayerPerClass`，但它今天不是渲染源，也**没有按职业/队伍选变体**的逻辑（`modelPlayerPerClass` 只被塞进诊断）。饰品如果想走 schema 兜底，需要单独一条路径，不能假设复用现成的 `resolveAssetReference` 就够了。

## 2 参考实现（只读；实施时必须按"库+路径+文件+行号 → 本仓文件:行号"复核）

| 用途 | 库 / 文件 / 行 | 读什么 |
|---|---|---|
| 饰品模型选择（按物品/职业/队伍） | `OthmanAba/TeamFortress2` `tf2_src/game/shared/econ/econ_entity.cpp` | `CEconEntity` 的模型设置与 `GetItem`/`GetOwner` 关系，`m_nModelIndex` 来源 |
| 玩家饰品可见性更新 | 同库 `tf2_src/game/client/tf/c_tf_player.cpp` | 视角/观战目标变化时如何刷新 wearable 的可见性与 owner |

- 行号本轮**未逐条打开核对**（本机对 GitHub 页面访问受限），落地报告里必须重新核对并写对读记录；核对不上就写"未能对读外部源码"，退回本仓证据。
- 仓库页面的 MIT 标记**不等于**可以复制代码：只读对读、只抄结论与数据格式，实现全部自己写。红线见 [REFERENCE-REPOS-2026-11-21.md](REFERENCE-REPOS-2026-11-21.md)。

## 3 分步计划

### Step 0（P0，S–M）：录像能力探针 —— 决定要不要做

扩 `demo_open_probe`（现有 `--scan` 系列：`--snapshot`/`--props`/`--events`，见 `native/tools/demo_open_probe.cpp:588-737`）加一个 `--wearable-scan`，对指定 tick 输出每个实体的：

`class`、`entityIndex/generation`、`m_nModelIndex`、modelprecache 路径、`m_iItemDefinitionIndex`、`m_hOwnerEntity`、`m_hMoveParent`、`m_iParentAttachment`、origin/angles。

同时给三条汇总：按类名直方图；类名含 `Wearable` 的实体数；"owner handle 能解析到同 tick 的 CTFPlayer"的饰品数。

样本：STV `cp_process_f12`（pr600）+ 一段第三人称 POV，各取 ≥3 个 tick（如 600/3000/6000）。

**门禁（过不了就停手）**：满足三条才进入 Step 1 ——
1. 至少一个 tick 里存在类名含 `Wearable` 的实体（真实类名以探针输出为准，可能是 `CTFWearable`/`CTFWearableItem` 等）；
2. 这些实体有可解析的 owner（→ 同 tick 存活的 `CTFPlayer`）；
3. 有 `m_nModelIndex` 且能从 modelprecache 取到路径的占比可统计（允许部分缺失，但缺失率要写出来）。

若录像只有玩家实体、没有任何 wearable 更新/关联，**就此结案**：写一份"录像不足以还原饰品"的结论与证据，不进入绘制实现。

### Step 1（P1，M）：饰品识别与归属

1. 用探针输出确定类名集合，做成显式允许表（子串匹配要能区分 `CTFWearable` 与别的含 Wearable 的类），不靠猜。
2. 归属解析：`m_hOwnerEntity` → 槽位 + generation，**必须与本 tick 的玩家实体身份对上**；父链 `m_hMoveParent` + `m_iParentAttachment` 一并记录。实体复用（generation 变化）时立刻断开旧归属，不做"同 index 继承"。
3. 产出"饰品实例表"：`(ownerEntity, generation, wearableEntity, wearableGeneration, modelPath 来源, itemDef, attachment)`，每帧重建，不跨 tick 缓存归属。

验收：探针两份录像的汇总数与逐条明细可复算；每条饰品都能指到它的 owner，或明确标 `noOwner`；`generation` 变化处有对拍。

### Step 2（P1，M）：模型路径与材质

1. **录像实际路径优先**：`m_nModelIndex`（必要时 `m_iWorldModelIndex`）→ modelprecache。不要把 schema 候选当第一来源。
2. 仅当路径缺失时走 schema 候选，并补上"按职业/队伍选 `modelPlayerPerClass`"这一步（今天没有，见风险 1）；命中 schema 的实体单独计数、单独 trace。
3. 复用现有 mesh/material 缓存（`entityMeshKeyByPath`/`entitySkinFamilyCountByKey` 一带），不走第二套加载器。

验收：每类饰品打印"路径来源"三分类计数（precache / schema / 未解析）；材质缺失单独计数；不得出现"路径猜出来但模型不存在"的静默替代。

### Step 3（P1，L）：骨骼合并绘制（本计划真正难的一块）

1. 实现 `buildWearableBoneMatrices`（新函数，放实体模型/动画模块）：对饰品模型的每根骨骼，**按名字**在 owner 玩家模型里找同名骨骼；
   - 找到 → 取玩家本 tick 的模型空间矩阵（`sampled.boneModelMatrices[i]`），即 `boneMerge` 的近似；
   - 找不到 → 用饰品自身的动画/绑定姿态，父级沿已合并骨骼链落位；
   - 玩家姿态不可用（未采样/超出预算）→ 整件饰品**不画**并计数 `poseUnavailable`，不允许回退到 origin/angles 硬摆。
2. 绘制归属：饰品进"玩家组"预算与玩家同一排序，不进 prop 预算；有 owner 的饰品从独立 prop 绘制里剔除（照 T83 持枪 `:13347-13400` 的 "先剔除、再绘制" 写法，别在循环里补）。
3. 用逐实例 `boneOffset/boneCount` 上传，与现有实体骨骼同一缓冲；不新增管线状态。
4. 关闭开关（`--cosmetics off|basic`，默认 off）时，路径与字节必须不变。

验收：
- 至少两职业、红蓝各一：饰品贴合到正确骨骼，蹲/走/转视角时不漂移、不脱离；
- 同 tick 内稳定：同一 tick 的两帧饰品矩阵一致；
- 无串挂：不存在"饰品的 owner 与所贴骨骼来自不同玩家"的对拍（拿 `entityIndex` 与 owner 身份同帧核对）；
- 无材质缺失、无"帽子在脚边"。

### Step 4（P2，M）：测试与画面验收

1. 解析夹具体（沿用现有 fixture 风格）：属性缺失、无效 handle、实体删除/复用（generation）、模型索引缺失、item schema 兜底。
2. 真录像验收：两职业 × 红蓝 × 走/蹲 × 视角切换 × STV 跟随；每项给 tick 与截图。
3. 报告写清"不支持"清单：粒子饰品、依赖附加模型的饰品、workshop 资源缺失、客户端专有外观，各自计数 + 一条样例。

## 4 抽查清单（每步都要能回答）

- 这件饰品属于谁？（owner 身份 = index + generation，与本 tick 玩家对上）
- 模型路径从哪来？（precache / schema / 未解析，三分类计数）
- 它为什么在这个位置？（boneMerge 骨骼名 + 玩家矩阵；还是明确 `poseUnavailable` 不画）
- 关闭开关时，字节是否真的没变？

## 5 排期与卡

| 卡 | 内容 | 依赖 |
|---|---|---|
| task-39 | Step 0：录像 wearable 探针 `--wearable-scan` + 两段录像汇总，出"可做/不可做"结论 | 无 |
| task-40 | Step 1+2：饰品识别、owner 归属、路径与 schema 兜底 | blocked_by task-39 |
| task-41 | Step 3+4：骨骼合并绘制、开关、夹具与画面验收 | blocked_by task-40 |

写域提示：`native/src/main.cpp` 属共享写域，与 task-3/task-36 串行；Step 3 会同时碰 `native/include/entity_model.h`、`native/src/entity_model.cpp`、`native/src/native_renderer_entity.cpp`。

## 6 复现模板

```bash
# Step 0 探针（开关名以落地实现为准）
native/build-nmake/demo_open_probe.exe --scan --wearable-scan 600 \
  "/e/demo测试/cp_process_f12/STV/1338f5de79ea343682c17dd514c980d9_matcha-20260927-1312-cp_process_f12.dem"
# 画面验收（沿用现有抓帧装置）
bash .scratch/n34-alpha/cap.sh cos-basic --cosmetics basic
```

（抓帧装置与基线 md5 见 [ALPHATEST-REFERENCE-2026-11-21.md](ALPHATEST-REFERENCE-2026-11-21.md)；`--capture-tick 600` 的 pr600 基线在默认参数下为 `2ddade35`。）
