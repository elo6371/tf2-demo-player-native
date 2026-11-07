# T85 结项说明：世界模型四层预算（P0）

分支 task/t85-budget，基线 e15577b。本文只写实测过的东西；没跑的一律标「未验证」。
读数行、命令与逐条读数见 [evidence/t85/T85-BUDGET-DESIGN.md](../evidence/t85/T85-BUDGET-DESIGN.md)，
断言清单见 [evidence/t85/GATE-ASSERTION-INVENTORY.md](../evidence/t85/GATE-ASSERTION-INVENTORY.md)。

## 1. 缺陷

三个非目标武器的世界模型上传失败，报 E_OUTOFMEMORY（0x8007000E）：

    models/weapons/c_models/c_flamethrower/c_flamethrower.mdl
    models/weapons/c_models/c_sapper/c_sapper_xmas.mdl
    models/workshop_partner/weapons/c_models/c_shogun_warhorn/c_shogun_warhorn.mdl

交接书明确否决「只把 96 改大」。原因是四层预算连成一条链，改大任何一层都只是把墙往后推一格。

## 2. 四层预算与各自的读数行

| 层 | 语义 | 代码位置（本分支） | 读数行 |
|---|---|---|---|
| A | 世界模型「准备」预算：每帧最多准备多少个模型 | native/src/main.cpp:3799-3888（kPrepareCapacity = 64，未提高） | budget: prepare capacity= candidates= admitted= built= deferred= failed= admittedByPriority= deferredByPriority= |
| B | 实体 mesh 表容量：GPU 上常驻多少张 mesh | native/src/native_renderer_entity.cpp:33（kEntityModelMeshCapacity = 96，未提高）、:198 uploadEntityModelMesh | budget: meshTable capacity= resident= evictions= capacityRefusals= reuploads= resupply= residentByPriority= |
| C | 实例表容量：每帧提交多少条实例 | native/src/native_renderer_entity.cpp（setEntityModelInstances，entityModelInstanceCapacityLimit = 96） | budget: instances capacity= submitted= accepted= slots= rejected=missingMesh: deferredNotResident: badMesh: nonFinite: budget: |
| D | 本帧绘制预算：实际画多少 | native/src/main.cpp:9929-9930（propBudget -= attachBudget）、:10626 | budget: draw capacity= playerBudget= propBudgetAfterAttach= playerDrawn= propDrawn= attachReserved= attachDrawn= droppedPlayers= droppedProps= |

关键点：**A/B/C/D 是四个不同的数**，且 B 与 D 加起来不再是同一个 96——B 是常驻缓存，D 是本帧绘制。
交接书要求把这四个概念分开建模、分开观测、分别设上限，这就是四行读数各自的使命。

## 3. 策略

1. **优先级**（native/include/entity_mesh_budget.h）：PlayerBody(0) > HeldWeapon(1) > Overlay(2) > WorldProp(3) > Deferred(4)。
   分类由 classifyEntityMeshPath 按路径判定（models/player/<名字>.mdl = 玩家身体；含 /weapons/ = 手持武器世界模型；
   /v_models/ 与 _arms = 第一人称专用，标 Deferred）。A 层按 (优先级, 原始顺序) stable_sort 后再切 64 个名额。
2. **overlay 不可驱逐**：EntityModelMesh 有 pinned 标志，Overlay 优先级上来的 mesh 一律 pinned=true；
   evictableEntityModelMeshKey 只从 rank >= WorldProp 且 lastTouchedFrame 早于当前帧、且未 pinned 的里面挑
   （rank 降序 → 触达时间升序 → key 字典序）。不能用「rank 比较」当判据：那会把九个必须的 class 轮廓先选成受害者。
3. **先建后逐**（review B5）：先把顶点/材质 run/CreateBuffer 都建成功，再考虑容量；建失败直接返回并保留真实 HRESULT，
   **绝不为一个新来者赶走常驻者**。
4. **驱逐要连坐失效**（review B1+B4）：驱逐时清 entityModelDrawRanges_、overlayDrawRanges_、两个 textured 计数，
   把 key 推入 evictedKeys；main.cpp 的 reconcileEvictedEntityMeshes 再把 path->key 映射、attachMeshTried 标记、
   attachEntityMeshes 与 rig 表里所有指向该 key 的条目删掉，并打 budget: eviction key=… action=unpublish,resupply，
   下一帧重新供给（++attachMeshResupply）。不做这一步，驱逐 = 永久丢失，只剩计数器好看。
5. **拒绝必须有名有数**：capacityRefusals（表满无可驱逐）、evictions（被驱逐）、reuploads（同 key 重传）、
   missingMesh（应该存在却找不到）与 deferredNotResident（A 层延后、从未准备）分开计数——混在一起就无法判断是不是回归。

## 4. 实测读数（pl_upward_f12，ent=3，tick 30001，speed=32，--player-animation --player-weapon-attach on）

### 4.1 before（冻结候选 t79，exe a3c93d57…724e9）→ after（本分支）

| 读数 | before | after |
|---|---|---|
| gpu: models done N uploaded=M | 64 / 64 | 64 / 64（A 层没被放大） |
| overlay prepared/uploaded | 9 / 9 | 9 / 9 |
| attach: meshSource supplied/refused | 23 / 3 | 13 / 0 |
| 0x8007000E 出现行数 | 601 | 0 |
| 同帧像素 sha256 | d297340f6bbd1c9e…476987c | d297340f6bbd1c9e…476987c（同 tick 不变） |

### 4.2 口径：supplied 23 → 13 不是覆盖缩水

两个 supplied 数的不是同一件事。after 的 attach 覆盖账：

    attach: coverage requested=5988 resolvedFromWorld=5975 uploaded=13 refused=0 retrySkipped=0 unresolvedPath=0 meshes=13
    attach: meshSource supplied=13 refused=0 meshes=13 resupply=0
    scene: weaponAttach on=1 held=13 drawn=13 poseMissing=0 overBudget=0 refused=0 noHoldPoint=0 noWorldMesh=0

- supplied/uploaded = 这一帧**现建**的 mesh 数；resolvedFromWorld = 已经在 A/B 层准备好的、直接复用的请求数。
- 逐帧实体覆盖两边完全相同（held=13 drawn=13）；before 之所以要现建 23 个，是因为它的 A 层只准备了 64 个世界模型，
  武器世界模型基本排在切点之后；after 的 A 层按优先级把 43 个武器模型收进来（residentByPriority=weapon:56），
  attach 于是只剩 13 个要现建。账户平衡：requested == resolvedFromWorld + uploaded + refused + retrySkipped + unresolvedPath。
- 另 12 个 before-only 模型（c_axtinguisher_pyro、c_frying_pan、c_medigun、c_minigun、c_sandwich_xmas、c_scattergun、
  c_crusaders_crossbow_xmas、c_fists_of_steel、c_riding_crop、c_scout_sword、c_tomislav、c_winger_pistol）在 after 里
  全部由 A 层预先供给；c_sapper_xmas 是第 13 个 resolvedFromWorld。

### 4.3 四层读数（同一帧）

    budget: prepare capacity=64 candidates=98 admitted=64 built=64 deferred=34 failed=0 admittedByPriority=player:9,weapon:43,overlay:0,prop:12,firstPersonOnly:0 deferredByPriority=player:0,weapon:0,overlay:0,prop:25,firstPersonOnly:9
    budget: meshTable capacity=96 resident=86 evictions=0 capacityRefusals=0 reuploads=0 resupply=0 residentByPriority=player:9,weapon:56,overlay:9,prop:12,firstPersonOnly:0
    budget: instances capacity=96 accepted=59 slots=170 rejected=missingMesh:0,deferredNotResident:37,badMesh:0,nonFinite:0,budget:0
    budget: draw capacity=96 playerBudget=13 propBudgetAfterAttach=70 playerDrawn=13 propDrawn=70 attachReserved=13 attachDrawn=13 droppedPlayers=0 droppedProps=82

读法：
- A 层确实在起约束作用（candidates 98 > 64，deferred 34），但被延后的全是 prop:25 与 firstPersonOnly:9，玩家/武器一条没延后。
- C 层的 slots=170 是「按材质 run 拆开的实例行数」，与 accepted=59 不是同一单位（代码 native_renderer_entity.cpp:585-588 有说明）；
  所以账目要拿 submitted（实体数）对 accepted+四种拒绝，slots 只需 >= accepted。
- deferredNotResident:37 = A 层延后导致根本没进表，**不是** 37 个坏实例；missingMesh:0 说明没有「应该有却没有」的。

## 5. 门禁改动与断言

- outline-check.sh 断言数：e15577b = **102**，本分支 = **173**（口径 grep -cE '^[[:space:]]*assert_(eq|ne|ge|le|lt|contains)' outline-check.sh）。
- 其中 assert_world_fixture（outline-check.sh:260-375）自带 **26** 条具名断言，覆盖：fixture 身份（demo sha256 + BSP 字节数）、
  三个模型各自 supplied >= 1、无具名拒绝、无 0x8007000E、A 层账目、B 层容量/驱逐/拒绝、C 层 submitted 账目、
  D 层丢弃计数、attach 覆盖平衡、以及「本帧有持枪的实体」。
- 退役的旧断言（都是把偶然常量钉死）：
  - HEAD:860 世界上传数 == 59（baseline 59/59 那对）
  - HEAD:1850 cap 64 拒绝 class mesh >= 1
  - HEAD:1859 cap 96 每个 prepared class mesh 都上传 == 0
  - HEAD:1860 uploaded 数 == prepared 数
  它们被「容量/占用/按原因拒绝/驱逐各自可观测 + 越界时具名变红」替代；新的边界变异是把表容量 96 改成 24，
  要求具名驱逐行出现，再还原并确认驱逐/拒绝/缺失全为 0。
- mutate.sh 新增 m14：把 A 层的优先级排序改回先到先得，要求 outline-check --world-only 出现具名红
  （FAIL named world models:）。m14 是长期防线：把 96 改成 160/192 不会让 m14 变绿。

## 6. 验证状态

| 项 | 状态 |
|---|---|
| 干净构建（build-cmake.sh） | 通过：job bash-a769a10a74874246 rc=0，[100%]，exe_count=53（旧源码）；最终源码另做只编验证 rc=0、0 error |
| 三个模型不再 OOM | 通过：0x8007000E 601 行 → 0 行（同一 demo/tick/argv，§4.1） |
| 四层读数可发布 | 通过：§4.3 四行都是实测 |
| outline-check.sh 整轮 | **未验证**：分支上被租约超时杀掉（rc=124，160 OK / 9 FAIL，三个根因已修但未复跑）；改由合并树整链付这 45 分钟 |
| third-person-weapon-check.sh | **未验证**（合并树） |
| player-body-appearance-check.sh | **未验证**（合并树） |
| 两进程同帧确定性 | **未验证**（合并树 extras） |
| m14 变异 | 变异体已单独编译验证（rc=0、还原字节一致）；**跑起来是否变红未验证** |
| attach-weapon-budget-check.sh | 已写、未跑（等合并树） |
| speed A/B | 未跑；lead m02191 决定用 timeline 门禁的 1x/8x/32x 同 tick 帧 XOR=0 覆盖，省掉 45 分钟 |

## 7. 前提与局限（assumptions）

1. A 层的 64 是**故意不提**的：提高它会改变 gpu: models done 那条 outline 门禁读的数，也会把「谁该被准备」的问题藏起来。
   本方案回答的是「64 个名额给谁」，不是「给更多名额」。
2. B 层的 96 同理保留：修的是准入顺序、驱逐安全与账目，不是常数。
3. 计数的语义边界：slots 按材质 run 计，submitted/accepted 按实体计；两者不可直接相加相等，只能比对。
4. capture-tick 不是 seek：--capture-tick 是先播过去再回夹光标，所以 trace 里的 tick 是播放域，不是「用户在那个 tick 看到的画面」。
   任何「像素在某 tick 正确」的结论都要 seek 式抓帧，本说明不含这种结论。
5. exe sha256 **不是源码身份**：同源码连续三次构建得到三个 sha（PE TimeDateStamp 相差 14 s，见设计文档 §8.9）。
   行为归因一律靠读数，不靠 exe sha。
6. 结论只覆盖真正断言过的东西：模型「被供给/被准备」有读数；「在像素里被画出来」只有 attach 实体覆盖与逐帧账目，没有逐模型像素证据。

## 8. 移交

- 待跑（合并树，短租约）：m14 变异 + attach-weapon-budget-check.sh + determinism extras。
- 合并树 outline 绿后，可删除 attach-weapon-budget-check.sh（其断言已等价落在 outline-check 的 assert_world_fixture 里）。
- 证据目录：evidence/t85/（设计文档、断言清单、run-*.sh 驱动、before/after/coverage 读数、合并窗口清单）。