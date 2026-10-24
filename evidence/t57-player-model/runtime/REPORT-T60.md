# T60 运行时修复报告 — LOD0 预算 / 材质槽 / 动画源

任务：T60（在 T57 运行时审计的三个已确认缺陷上做**产品源码修复**）。
T57 的审计报告见 `evidence/t57-player-model/runtime/REPORT.md`；本报告覆盖修复实现、前后读数、门禁与变异证明。

## 0. 身份与范围

| 项 | 值 |
| --- | --- |
| owner | t60-model |
| worktree | `D:/TF2_Native_Worktrees/t60-model` |
| branch | `task/t60-model` |
| base commit | `ca4de84`（从 p0-entity-protocol 切出，未变基） |
| 改动文件 | 见 §2：10 个已跟踪文件（+931/−283）+ 新增 `native/include/material_slot_resolution.h`、`player-model-mutations.sh`、`PROGRESS.txt`、`evidence/t57-player-model/runtime/t60/` |
| 产品源码改动 | 有：`native/src/` 5 个文件 + `native/include/` 4 个文件（A/B/C 三个缺陷都在产品链路里） |
| 构建 | `bash build-target.sh entity_model_probe entity_overlay_probe model_robustness_probe tf2_demo_native` → rc=0（§9 有哈希） |
| TF2 资产根 | `D:/SteamLibrary/steamapps/common/Team Fortress 2/tf` |
| demo | bagel `4a9bfb9276509d0ec5f5fdc722a95b17_match-20260927-0239-koth_bagel_rc13.dem`；snakewater `bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem` |

### 非目标
- 不做像素/抓帧对比（§7 说明 probe 能证明的边界）。
- 不跑 `verify-all.sh` 全链。
- 不实现 bodygroup 运行时选择（T57 已记录 `bodygroupStatus=Unknown`，且九职业每个 bodypart 只有一个 model，选择是空操作）。
- 不修改 `D:/TF2_Native_Test` 主树；不 push。

## 1. 结论摘要

**A. LOD0 预算（已修复，probe 可证）**
- 旧：固定 12000 顶点/渲染索引上限，9/9 职业被截断，`lod0Truncated=9`、`lod0LostTotal=107829`（单职业丢 603–20724）。
- 新：预算按模型自身 LOD0 描述符的 `renderIndexCount` 求和分配（`ModelLoader::buildBindPoseMeshLod0`），不再有常量上限，也不再静默丢顶点（越界索引/权重异常改为计数到 `lod0Skipped`）。
- 实测：9/9 职业 `lod0Budget == lod0RenderIndices == lod0Built`（23442/23595/26922/25755/24864/28944/12603/16980/32724），`lod0Skipped=0`、`lod0Lost=0`、`lod0Truncated=0`、`lod0LostTotal=0`。门禁断言 0。

**B. 材质槽（已修复，probe 可证）**
- 旧：只取 slot0（`main.cpp:3749-3750`），MDL 的 `numSkinRef/numSkinFamilies` 从未被引用；9/9 渲染 `_red`，Heavy/Spy 的 slot0 是头部贴图涂在身体上。
- 新：解析 MDL 220/224/228 的 skin family 表，按每个 mesh 自己的 material 槽 + skin family（红/蓝）解析 VMT→VTF；渲染器按 `(materialSlot, skinFamily)` 绑定纹理，一个 mesh 槽一段 draw range（`ModelMeshMaterialRun`）。
- 实测：`classesWithPaint=9`、`productPaintedSlots=78`、`paintedFamiliesMin=2`、`distinctPaintedStemsMin=1`、`headOnBodyClasses=0`、`redBluDistinctSlotsTotal=14`。Heavy 身体走 slot4 `hvyweapon_red_sheen`（slot0 的 `heavy_head_red` 不再涂身体），Spy 身体走 slot3 `spy_red`（slot0 的 `spy_head_red` 不再涂身体）。

**C. 动画源（已修复，probe 可证）**
- 旧：取第一个包含 `_animations.mdl` 的 include，命中 `<class>_user_animations.mdl`（69 骨骼/1 序列），`separateAnimSource=0 animSeqs=2`。
- 新：遍历全部 include 候选 + 约定路径，按 `nameBoundAgainst`（候选序列名能在模型基础序列里按名绑定且 animDesc 有效）打分，取最大者。
- 实测：9/9 `separateAnimSource=1`、`animPath=models/player/<class>_animations.mdl`、`animSeqs 671..1131`、`bindBones=full`（scout 303、soldier 353 绑定序列与 T57 静态表一致）。

**门禁**：`player-model-check.sh` 先跑 4 个负控制（各自具名失败），再跑 forward；A/B/C 全部断言，最终 `PASS`（0 FAIL 行）。变异套件 a/b/c/meta 四例全部"变红 + 具名原因 + 字节还原 + 重建 + 基线复绿"。

## 2. 改动清单

### 2.1 产品源码

| 文件 | 改动 |
| --- | --- |
| `native/include/model_loader.h` | 新增 `ModelMeshMaterialRun{vertexStart,vertexCount,materialSlot}`（:150）；`ModelMetadata` 新增 `SkinFamilyTable`（:322，含 `textureSlotFor`）、`bindPoseRanges`、`bindPoseLod0Budget`、`bindPoseLod0Skipped`；删除 `kMaxBindPoseLod0Vertices=34000` 常量，`buildBindPoseMeshLod0(metadata,error)` 改为按模型分配；新增 `buildMaterialRuns`（:450）。 |
| `native/src/model_loader.cpp` | `parseMdl` 读 MDL 220/224/228（:553-577），校验 skin 表边界与每个 entry < numTextures；`buildBindPoseMeshLod0`（:1246）预算=所有在界 LOD0 描述符 `renderIndexCount` 之和，无上限 break，记录 `bindPoseRanges` 与 skipped；`buildMaterialRuns`（:1320）合并相邻同槽 range 并要求完整铺满顶点列表。 |
| `native/include/material_slot_resolution.h`（新） | `resolveMaterialSlot(metadata, skinFamily, materialSlot, reader, out)`：skin family → texture slot → textureCandidates → VMT → `$basetexture` → VTF；绝不回落到默认槽。 |
| `native/src/vmt_material.cpp` / `native/include/vmt_material.h` | 新增 `fallbackBaseTexture`：EyeRefract 类 VMT 的 base texture 只写在嵌套 `Eyes_dx8` 块里（:109 / :24）。 |
| `native/include/native_renderer.h` | `EntityModelDrawInstance` 增加 `skinFamily`；`uploadEntityModelMesh(cacheKey, vertices, runs)` 与 `uploadEntityModelTexture(cacheKey, slot, family, rgba, w, h)`；`EntityModelMesh` 增加 runs 与按 `(slot<<32)|family` 索引的纹理表；新增 `entityModelMeshTruncatedCount()`/`entityModelTextureCount()`。 |
| `native/src/native_renderer.cpp` | `uploadEntityModelMesh`（:3808）不再 min(size,12000)，只校验 runs 铺满并计数截断（:3832）；`uploadEntityModelTexture`（:3907）按 `(slot,family)` 建纹理；`setEntityModelInstances` 每个 run 一段 range 并按 family 取纹理（:4016）；draw 循环用 `range.vertexStart` 偏移。 |
| `native/src/main.cpp` | `PreparedEntityMesh` 增加 runs/skinFamilies/textureCandidates/paintedSlots（:3699-3731）；构建处填 runs；paint 阶段按 family{0,1}×槽解析（:5062-5073）；上传阶段逐槽解码（:6464-6505）；`draw.skinFamily` 由队伍决定（:8543，team==3 → 蓝）；标题 `entityPaintedSlots`（:5508）；`--dump-entity-materials` 输出（:7793）。 |
| `native/src/animation_binding.cpp` | 候选=所有 include 里含 `_animations.mdl` 的 + 约定路径；`nameBoundAgainst`（:224）按序列名绑定打分；循环取最大 `nameBound`（:251）；`separateAnimSource` 仅在采纳时置 true（:273）。 |

### 2.2 探针与门禁

| 文件 | 改动 |
| --- | --- |
| `native/tools/entity_model_probe.cpp` | `--player-model-audit`（:1562）每职业一行新增 lod0Descriptors/lod0RenderIndices/lod0Budget/lod0Built/lod0Skipped/lod0Lost/materialRuns/lod0MaterialSlots/paintedSlots/paintedFamilies/redBluDistinctSlots/distinctPaintedStems/headOnBody/dominantSlot/dominantStem/bodySlot/bodyStem/headStem/skinRefs/skinFamilies/slot0…（:1770-1787）；summary 增加 `lod0Truncated/lod0LostTotal/lod0SkippedTotal/classesWithPaint/productPaintedSlots/paintedFamiliesMin/distinctPaintedStemsMin/headOnBodyClasses/redBluDistinctSlotsTotal`（:1793）。B 的指标定义见 §6.3。 |
| `player-model-check.sh` | 298 行。0/4 二进制新鲜度（按 CMake DependInfo 推导每个 exe 的输入 + 头文件）；1/4 四个负控制（默认先跑）；2/4 A+B；3/4 C；4/4 demo 可达性。 |
| `player-model-mutations.sh`（新） | 290 行。基线绿 → 逐例 patch → 重建 → 门禁必须变红且 FAIL 数上升且具名原因出现 → `cp -p` 字节还原（`cmp -s`）→ 重建 → 基线复绿。EXIT trap 兜底还原。 |

## 3. 命令、退出码与关键读数

| # | 命令 | rc | 证据 |
| --- | --- | --- | --- |
| 1 | `bash build-target.sh entity_model_probe entity_overlay_probe model_robustness_probe tf2_demo_native` | 0 | `.scratch/t60-build4.log` |
| 2 | `bash player-model-check.sh`（负控制 + forward） | 0 | `evidence/t57-player-model/runtime/t60/gate-forward.txt` |
| 3 | `bash player-model-mutations.sh` | 0 | `evidence/t57-player-model/runtime/t60/mutations.log` + `mutations/` |
| 4 | 还原后最终 `bash player-model-check.sh` | 0 | `evidence/t57-player-model/runtime/t60/gate-run.txt` |

### 3.1 A+B 每职业读数（`player-model-audit.txt`）

贴图列省略 `models/player/<dir>/` 前缀（探针输出为完整路径，如 `models/player/scout/scout_red`）。

| class | lod0Descriptors | lod0RenderIndices | lod0Budget | lod0Built | lod0Skipped | lod0Lost | lod0MaterialSlots | paintedSlots | paintedFamilies | redBluDistinct | distinctStems | headOnBody | dominantStem | bodyStem | headStem |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Scout | 9 | 23442 | 23442 | 23442 | 0 | 0 | 4 | 8 | 2 | 1 | 4 | 0 | scout_red | scout_red | scout_head_red |
| Sniper | 10 | 23595 | 23595 | 23595 | 0 | 0 | 6 | 12 | 2 | 1 | 6 | 0 | sniper_head_red | sniper_red | sniper_head_red |
| Soldier | 9 | 26922 | 26922 | 26922 | 0 | 0 | 6 | 12 | 2 | 2 | 6 | 0 | soldier_head_red | soldier_red | soldier_head_red |
| Demoman | 7 | 25755 | 25755 | 25755 | 0 | 0 | 4 | 8 | 2 | 1 | 4 | 0 | demoman_head_red | demoman_red | demoman_head_red |
| Medic | 6 | 24864 | 24864 | 24864 | 0 | 0 | 5 | 10 | 2 | 2 | 5 | 0 | medic_red | medic_red | medic_head_red |
| Heavy | 8 | 28944 | 28944 | 28944 | 0 | 0 | 5 | 10 | 2 | 2 | 5 | 0 | hvyweapon_red_sheen | hvyweapon_red_sheen | heavy_head_red |
| Pyro | 6 | 12603 | 12603 | 12603 | 0 | 0 | 1 | 2 | 2 | 1 | 1 | 0 | pyro_red | pyro_red | - |
| Spy | 7 | 16980 | 16980 | 16980 | 0 | 0 | 5 | 10 | 2 | 2 | 5 | 0 | spy_head_red | spy_red | spy_head_red |
| Engineer | 6 | 32724 | 32724 | 32724 | 0 | 0 | 3 | 6 | 2 | 2 | 3 | 0 | engineer_red | engineer_red | engineer_head_red |

summary：`classes=9 resolved=9 renderable=9 companionsComplete=9 lodCountKnown=9 vvdLodsMin=7 vtxLodsMin=7 lod0Truncated=0 lod0LostTotal=0 lod0SkippedTotal=0 classesWithPaint=9 productPaintedSlots=78 paintedFamiliesMin=2 distinctPaintedStemsMin=1 headOnBodyClasses=0 redBluDistinctSlotsTotal=14 slot0Resolved=9`

说明：`dominantStem` 是 LOD0 里顶点最多的那个 mesh 的 family-0 贴图——Sniper/Soldier/Demoman/Spy 的头部本来就是最大 mesh（T57 记录 sniper head 861 > body 789、spy head 1036 > body 361），所以 `dominantStem=*_head_red` 是**正确**的；判断"头涂身体"用 `headOnBody`（body 槽的贴图是否为 head），九职业全 0。

### 3.2 C 每职业读数（`player-model-anim.txt`）

| class | animPath | separateAnimSource | animSeqs | boundSeqs | bindBones |
| --- | --- | --- | --- | --- | --- |
| scout | models/player/scout_animations.mdl | 1 | 1012 | 303 | 78/78 |
| sniper | models/player/sniper_animations.mdl | 1 | 932 | 237 | 74/74 |
| soldier | models/player/soldier_animations.mdl | 1 | 858 | 353 | 86/86 |
| demo | models/player/demo_animations.mdl | 1 | 767 | 273 | 84/84 |
| medic | models/player/medic_animations.mdl | 1 | 671 | 210 | 92/92 |
| heavy | models/player/heavy_animations.mdl | 1 | 819 | 255 | 79/79 |
| pyro | models/player/pyro_animations.mdl | 1 | 842 | 266 | 76/76 |
| spy | models/player/spy_animations.mdl | 1 | 826 | 271 | 84/84 |
| engineer | models/player/engineer_animations.mdl | 1 | 1131 | 348 | 73/73 |

### 3.3 门禁最后一行与 demo

```
PASS player-model-check: negative controls named-failed; A lod0Truncated=0; B paintedSlots per slot x family; C separateAnimSource=1.
```
demo：bagel `rows=39 withModel=36 markerOnly=3`；snakewater `rows=20 withModel=14 markerOnly=6`（与 T57 基线一致，未退化）。

## 4. 前后对比（T57 基线 → T60 修复后）

| 读数 | 修复前（T57） | 修复后（T60） | 门禁 |
| --- | --- | --- | --- |
| A `lod0Truncated` | 9 | **0** | 断言 =0 |
| A `lod0LostTotal` | 107829 | **0** | 断言 =0 |
| A 单职业 `lod0Built` | 12000（全部） | 23442..32724 = 各自 `lod0RenderIndices` | 断言相等 |
| B `productPaintedSlots` | 1 | **78** | 断言 =各职业之和 且 ≥18 |
| B `paintedFamiliesMin` | 无 paint 阶段（首轮 T60 审计 7/9 为 0） | **2** | 断言 =2 |
| B `headOnBodyClasses` | 旧指标无效（把“最大 mesh 是头”当缺陷） | **0** | 断言 =0 |
| B Heavy/Spy 身体槽 | slot0 头贴图 | Heavy slot4 / Spy slot3 | 断言 headOnBody=0 |
| C `separateAnimSource` | 0（9/9） | **1**（9/9） | 断言 =1 |
| C `animSeqs` | 2 | 671..1131 | 断言 ≥200 |
| C `boundSeqs` scout/soldier | 2/2 | 303 / 353 | 记录 |

## 5. 根因（对应 problems/INDEX 规则）

1. **A：预算常量把"渲染索引数"当成可调上限**（规则 1：数值恰好落在 cap 上就是截断证据）。旧实现 `kMaxBindPoseLod0Vertices=12000` 与"顶点数"语义混用，9/9 职业 `lod0Built` 恰好 =12000；修复方式不是调大常量，而是让预算由模型自己的 LOD0 `renderIndexCount` 求和得出，并在同一行打印 `lod0RenderIndices/lod0Budget/lod0Built` 三个数，使"预算是否被截断"成为可比读数。
2. **B：常量 1 编码了未检查的假设**（规则 3）。`productPaintedSlots=1` 是硬编码常量，而 MDL 的 `numSkinRef/numSkinFamilies` 从未被读；修复是解析 skin 表 + 按 mesh 槽解析，并让 `productPaintedSlots` 由真实上传计数得出（`entityPaintedSlotCount`），门禁断言其等于各职业 `paintedSlots` 之和。B 的读数还修了两个"缺失值读成良性"的探针 bug（规则 4）：`paintedFamiliesMin` 初值 0 会让第一个职业把 0 写进 min、后续职业再覆盖成 2；改为 `size_t(-1)` 哨兵并在 summary 里转 0。`headOnBody` 原来用"最大 mesh 不能是 head"，但 TF2 头部本来就常是最大 mesh，改为"body 槽（按声明的非 head 槽里顶点最多者）解析出的贴图是否含 `_head`"。
3. **C：第一次匹配被当成唯一匹配**（规则 3/6）。include[0] 是 `<class>_user_animations.mdl`（69 骨骼/1 序列的 stub），旧代码遇到第一个含 `_animations.mdl` 的就停；修复遍历全部候选并按 `nameBound` 打分取最大，且 `separateAnimSource` 只有真正采纳时才为 true（缺值不读成良性）。

## 6. 门禁设计（为什么这些断言不是自证）

### 6.1 顺序：负控制先跑
`player-model-check.sh` 默认先跑 4 个负控制，全部**具名失败**才继续 forward：
```
OK wrong-tf-root failed rc=2 with 'player-model-audit FAIL tf-root is not a valid AssetRoot'
OK wrong-demo failed rc=2 with 'the demo header did not parse'
OK class-table-8 failed rc=2 with 'class-table entries=8 expected=9'
OK class-table-missing-model failed rc=1 with 'class models did not resolve'
```
每个负控制都要求 rc 与文本双匹配，所以"门禁跑绿"不是因为什么都没跑。

### 6.2 新鲜度
0/4 段按 CMake `DependInfo.cmake` 推导每个 exe 真正编译的源文件集合（overlay 不编译 `model_loader.cpp`，不能拿全树源文件比），并额外要求 exe 比所有头文件新（`build-target.sh` 在头文件变化时会 touch 全部源文件）。输出 `OK BINARY-FRESH=1 probe exe=...` 三行。

### 6.3 断言（A/B/C）
- A：summary `lod0Truncated=0 / lod0LostTotal=0 / lod0SkippedTotal=0`；每职业 `lod0Budget==lod0RenderIndices`、`lod0Built==lod0RenderIndices`、`lod0Lost=0`、`lod0Skipped=0`。
- B：`classesWithPaint=9`、`paintedFamiliesMin=2`、`distinctPaintedStemsMin≥1`、`headOnBodyClasses=0`；每职业 `lod0MaterialSlots≥1`、`paintedSlots==2*lod0MaterialSlots`、`paintedFamilies=2`、`headOnBody=0`、`redBluDistinctSlots≥1`、`distinctPaintedStems==lod0MaterialSlots`；`productPaintedSlots==ΣpaintedSlots` 且 ≥18。
- C：每职业 `separateAnimSource=1`、`animSeqs≥200`、`animPath` 含 `_animations.mdl` 且不含 `_user_animations.mdl`、`bindBones=bones/bones`。
- demo：`rows≥1`、`withModel≥1`（两份 demo）。

### 6.4 变异套件（`player-model-mutations.sh`，四例全过）

| case | 变异 | 门禁结果 | 具名 FAIL |
| --- | --- | --- | --- |
| a | 在 LOD0 顶点循环里重新插入 `>=12000` 跳过 | rc=1，FAIL-count 30（基线 0） | `A summary lod0Truncated = 9, expected 0`；`A Scout lod0Built = 12000, expected 23442` |
| b | `textureSlotFor(skinFamily,slot)` → `textureSlotFor(0,slot)` | rc=1，FAIL-count 9 | `B Scout redBluDistinctSlots = 0, expected >= 1`；`B Heavy ...` |
| c | 去掉 `nameBound > bestNameBound` 比较 | rc=1，FAIL-count 27 | `C scout separateAnimSource = 0, expected 1`；`C scout animSeqs = 2, expected >= 200` |
| meta | 门禁自身期望值 0→1（配置变异，非产品） | rc=1，FAIL-count 1 | `A summary lod0Truncated = 0, expected 1` |

每例之后 `cp -p` 还原并 `cmp -s` 验证字节一致，重建后基线复绿（FAIL-count=0）；EXIT trap 兜底。meta 例证明门禁自身能失败（规则 2：不是只数"预期失败"）。

## 7. probe 能证明 / 只有产品渲染能证明

**probe 能证明（本次实测）**：skin 表解码、每个 LOD0 槽的 VMT→VTF 解析（红/蓝两族）、每个 mesh 的 material 槽与 draw range、LOD0 预算与构建顶点数、动画源选择与绑定序列数。

**只有产品渲染能证明（未做）**：屏幕上的实际颜色（红/蓝是否正确显示）、头部是否真的没被身体贴图覆盖、截断修复后几何是否完整可见、动画播放的视觉效果。本任务没有抓帧（未启动产品窗口）。

## 8. NOT VERIFIED（明确未做）

1. **像素级验证**：没有截图/抓帧，"红/蓝正确、头不涂身体"是**贴图解析链**层面的正确（每槽解析出的 VTF 路径与 family 对应），不是屏幕像素。
2. **bodygroup 运行时选择**：`bodygroupStatus=Unknown` 是 T57 已有结论，本次未实现；但九职业每个 bodypart 只有一个 model，运行时选择对这些模型是空操作。
3. **队伍以外的 skin family**：只验证 family 0（红）与 family 1（蓝）；其他 6–24 个 family（节日/特殊皮肤）未验证。
4. **动画播放正确性**：只验证源选择与绑定数量（`boundSeqs`），未验证播放时的骨骼矩阵/循环。
5. **其他职业的 slot0 头贴图**：Heavy/Spy 的 `dominantStem` 仍是 head（因为头是最大 mesh），但 `headOnBody=0` 证明 body 槽不是 head；屏幕确认未做。
6. **demo 内实际渲染**：demo 探针只验证实体→模型可达性，未启动产品渲染 demo。

## 9. 构建 / 进程 / 锁结束状态

- 构建：`build-target.sh` rc=0；四个 exe 全部重建（§9.1 哈希）。
- 进程：探针进程全部正常退出；step4 结束后 `LOCKED-COMMAND-EXIT rc=0`，锁自动释放（`owner=t60-model` 释放后队列前进）。
- 锁：step4 于 2026-10-24 15:31:11 获得 `host.lock`（owner=t60-model），15:45:33 随命令退出释放；未跨阶段空持，未 break 任何人的锁。
- 窗口/音频：本任务只跑探针（CPU/文件解析），未启动产品窗口；产品构建不涉及 `--hidden-window --audio-disabled`。

### 9.1 二进制哈希（sha256）

| 文件 | sha256 |
| --- | --- |
| `native/build-nmake/entity_model_probe.exe` | `ea59320968951367144af594b940c83c46dbddd52fa5a34ca906838d6546f5f6` |
| `native/build-nmake/entity_overlay_probe.exe` | `8c70423cf639812a284cc12fe10519efcb5fc0379abb41d4248b836a9f76e0b3` |
| `native/build-nmake/model_robustness_probe.exe` | `5db79ff503305a42681728052a6c0085c6f4f52ce7c3dad34c9a70e4557c4d86` |
| `native/build-nmake/tf2_demo_native.exe` | `32504c5f81ac82bc7a57af2a0073aebe632180d186d14527d30d1988d4ef0b7b` |

## 10. 产物清单

```
evidence/t57-player-model/runtime/t60/
  gate-forward.txt               # 负控制 + forward 门禁（rc=0）
  gate-run.txt                   # 还原后最终门禁（rc=0）
  player-model-audit.txt         # A+B 每职业读数 + summary
  player-model-anim.txt          # C 每职业读数
  player-model-demo.txt          # demo 可达性
  player-model-demo-bagel.txt
  player-model-demo-snakewater.txt
  mutations.log                  # 变异套件汇总
  mutations/                     # 每例门禁日志 + 还原日志
  negative/                      # 四个负控制日志与夹具
```
