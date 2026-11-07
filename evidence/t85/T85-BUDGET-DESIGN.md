# T85 P0：世界模型预算 —— 设计、账目与门禁

分支 task/t85-budget（起点 e15577b），worktree D:/TF2_Native_Worktrees/t85-budget。
本文只写已经落盘的实现与已经跑出来的数；没跑的一律标"未验证"。

## 1. 缺陷与判据

三个"非目标武器的世界模型"上传被拒，报 E_OUTOFMEMORY：
- models/weapons/c_models/c_flamethrower/c_flamethrower.mdl
- models/weapons/c_models/c_sapper/c_sapper_xmas.mdl
- models/workshop_partner/weapons/c_models/c_shogun_warhorn/c_shogun_warhorn.mdl

失败行形状（改动前的基线二进制在 POV demo 上跑到 tick 55418 时产生）：
  attach: mesh result=refused stage=upload key=<cacheKey> lastError=0x8007000E

交接书明确否决"把 kEntityModelMeshLimit 从 96 抬到 160/192"。原因是它不是一条常数，
是四层预算串起来的链：哪一层先满，画面上就少东西，而每一层都被不同的门禁钉住。

## 2. 四层调用链（改动后仍在原来的位置，只是行为从"先到先得"变成"预算+优先级"）

A. 世界 pass 的"准备上限"—— native/src/main.cpp:3754
   constexpr std::size_t kPrepareCapacity = 64;   // 没有抬高
   收集候选（renderable、非 first-person、按 cacheKey 去重）→ 按优先级稳定排序 →
   前 64 个准入（admitted），其余记 deferred/deferredByPriority → 仍按原始顺序构建
   已准入的候选。构建失败记 prepareBuildFailed，不占名额。
   所以"排在第 65 个之后的世界模型完全没有 mesh"这件事现在有一个名字（deferred），
   而且这些名额是给高优先级资源的，不是"先来先占"。

B. 实体 mesh 驻留表 —— native/src/native_renderer_entity.cpp:33
   constexpr std::size_t kEntityModelMeshCapacity = 96;   // 也没有抬高
   上限处不再是"拒绝且永不重试"：
   - **先建后驱**：新 key 的 GPU buffer 先建出来；只有建成功了，表满时才去
     evictableEntityModelMeshKey() 选一个 victim（rank >= WorldProp、非 pinned、
     本帧没被实例更新画过，按 rank→最近触碰时间→key 排序），驱逐后再插入；
   - 建不出来（CreateBuffer 失败）→ 保留真实的 HRESULT 直接返回，**不动任何驻留者**
     （评审 B5：不能让一个新人的失败拖一个居民陪葬）；
   - 一个 victim 都选不出来 → E_OUTOFMEMORY + entityModelMeshCapacityRefusals_ + 返回 false
     （带原因，不是静默丢弃）；
   - 已存在的 key 再上传 → entityModelMeshReuploads_ 计数（纹理挂接的抖动证据）；
   - overlay 的 9 个 class mesh 进表时 pinned=true，永不成为 victim（评审 B1/M1）。

C. 每帧实例上限 —— Renderer::entityModelInstanceCapacityLimit = 96（native_renderer.h）
   setEntityModelInstances 不再在 96 处 break 掉整条循环：每条被拒的实例都按原因进四个
   计数器 missingMesh / badMesh / nonFinite / budget，循环继续走完。

D. 每帧绘制预算 —— native/src/main.cpp:9740 附近，kDrawBudget = 96
   先算 playerBudget = 可见玩家数（封顶 96），propBudget = 96 - playerBudget；
   attach 再从 prop 预算里预留实例（不挤玩家）。hasMesh 但没有绘制名额 → 记
   playerDroppedByBudget / propDroppedByBudget，不再是不留痕的空洞。

优先级（native/include/entity_mesh_budget.h）：
  PlayerBody=0 > HeldWeapon=1 > Overlay=2 > WorldProp=3 > Deferred=4
  分类器 classifyEntityMeshPath()：/v_models/ 或 *_arms.mdl → Deferred；
  models/player/<name>.mdl 恰好一层 → PlayerBody；models/player/items/... → WorldProp；
  其余含 /weapons/ → HeldWeapon；其它 → WorldProp。

## 3. 驱逐的安全约束（lead 评审第 1 条）

被驱逐的 key 的顶点缓冲会被 draw range 引用（entityModelDrawRanges_ / overlayDrawRanges_
里是裸 ID3D11Buffer*）。驱逐必须保证"不留空绘制、不永久丢件"，做法：
1. **先建后驱**（评审 B5）：新 mesh 的 vertices / material runs / CreateBuffer 全部成功之后
   才动表。CreateBuffer 失败时保留真实 HRESULT 并直接返回，不驱逐任何驻留者——一个失败
   的新人不能让一个居民陪葬，也不能把 CreateBuffer 失败写成 E_FAIL 冒充容量问题。
2. **谁可以当 victim**（评审 B1/M1，不是按 rank 简单比较）：必须 pinned==false、
   rank >= WorldProp（Overlay 及以上永不驱逐）、且 lastTouchedFrame < 当前 mesh 帧
   （上一帧实例更新真的画过的不驱逐，否则就是把 m8 的形态变成默认路径）。
3. **驱逐要完整失效**（评审 B1/B4）：同一个临界区里 (a) 清 entityModelDrawRanges_ 和
   textured 计数，(b) 清 overlayDrawRanges_（它只在上传/采样帧重建，留着就是悬空指针），
   (c) 把 key 记进 entityModelMeshEvictedKeys_。main.cpp 的 reconcileEvictedEntityMeshes()
   随后删掉 entityMeshKeyByPath / entityMeshKeyByNormalizedPath 里所有值等于它的映射、
   清 attachMeshTried 标记、删 attachEntityMeshes 与 rig/family 表，++attachMeshResupply，
   并打印 "budget: eviction key=<path> action=unpublish,resupply"。
   没有这一步，驱逐就是"枪永远消失、只剩计数器"。
4. 驱逐只在帧边界发生：上传发生在 setEntityModelInstances 之前（世界 pass / overlay /
   attach 各一次合成），draw ranges 在同一帧稍后由 setEntityModelInstances 重建。

抖动证据：同一 key 反复上传 → entityModelMeshReuploads_。attach 的路径是"一个 key 只上传
一次"（重传会丢纹理），所以 reuploads 的读数应当为 0；门禁把这个 0 钉住。

## 4. 观测线（四条 budget 行 + 两条 attach 行）

  budget: prepare capacity=.. candidates=.. admitted=.. built=.. deferred=.. failed=..
          admittedByPriority=player:..,weapon:..,overlay:..,prop:..,firstPersonOnly:..
          deferredByPriority=...
  budget: meshTable capacity=.. resident=.. evictions=.. capacityRefusals=.. reuploads=..
          resupply=.. residentByPriority=player:..,weapon:..,overlay:..,prop:..,firstPersonOnly:..
          （每帧在 settle 点打印一次，是这一帧的最后一条 meshTable 读数；启动时那条旧的保留）
  budget: eviction key=<models/...mdl> action=unpublish,resupply
          （每次驱逐一行，带被删模型的路径——门禁据此断言"驱逐有名字"而不是只有计数）
  budget: draw capacity=.. playerBudget=.. propBudgetAfterAttach=.. playerDrawn=.. propDrawn=..
          attachReserved=.. attachDrawn=.. droppedPlayers=.. droppedProps=..
  budget: instances capacity=.. accepted=.. slots=.. rejected=missingMesh:..,deferredNotResident:..,badMesh:..,nonFinite:..,budget:..
          （missingMesh 与 deferredNotResident 分开：前者是"该有却没有"，后者是"预算在 A 层就
           延后了、本来就没有 mesh"，混在一个数里会让 m8 类缺陷淹没在正常的延后里）
  attach: meshSource supplied=.. refused=.. resupply=.. meshes=..
  attach: mesh result=refused stage=upload key=.. lastError=0x%08lX evictions=.. capacityRefusals=..

## 5. 门禁改动（outline-check.sh）

逐条命令与判据见 evidence/t85/GATE-ASSERTION-INVENTORY.md（本轮新增/改写 61 条断言，
老断言退役 4 条）。计数命令与结果：

    grep -cE '^[[:space:]]*assert_(eq|ne|ge|le|lt|contains)\b' outline-check.sh
    HEAD = 102    当前 = 159

(1) **退役"baseline 59/59"**（HEAD:857-860 的 `assert_eq "world pass model uploads
    unchanged by the overlay" ... 59`）。59 是 snakewater 这段录像的偶然候选数，换成
    A/B 账目：candidates>=1、admitted+deferred==candidates、admitted<=capacity、
    built+failed==admitted、failed==0、uploaded==prepared。断言名里明写
    "a bound, not a moved cut"，防止有人用"把 64 抬上去"让账目变宽松。

(2) **新增世界模型具名交付段**（assert_world_fixture，outline-check.sh:259-331；主流程
    调用点 :1216，另有 --world-only 证据模式 :344-355）。它跑 Highlander STV pl_upward_f12 夹具
    （tick 30001，pyro，19 个身体在场上），因为原 outline 夹具（snakewater，2 个玩家）
    在构造上撑不满 96 张表——用它的 PASS 去证明"三个模型不再 OOM"是外推，不是读数。
    这一段的断言（:272-331）：夹具 demo sha256 / BSP 大小自证身份（缺失即红，不 skip）、
    run rc、抓到帧、三个模型各自 supplied>=1、没有 refused 行命名它们、
    0x8007000E 出现 0 次、层 A 账目（capacity 64 未抬、admitted+deferred==candidates、
    deferred>=1 证明预算确实紧、failed==0）、层 B（resident<=capacity、capacityRefusals==0、
    evictions==0）、层 C（accepted<=slots）。
    这一段只断言 mesh 上传读数，不对像素做任何声明（--capture-tick 是"播放过点再夹回游标"，
    不是 seek；trace 前段秒数是 PLAYBACK 域，见 lead m00789 与本节末尾的域声明）。

(3) **退役旧 cap64/cap96 成对读数**（HEAD:1842-1860）。旧块靠
    `kEntityModelMeshLimit = 96` 这个锚点做 patch，而该常量已改名
    kEntityModelMeshCapacity 并迁到 native_renderer_entity.cpp，原样保留会让 patch_in
    直接 MUTATION-INVALID 退出 3；而且"cap 64 会拒掉 class mesh"是把缺陷当对照，
    正是本轮要消灭的行为。换成**容量缩到 24 的边界控制**（outline-check.sh:2136-2168）：
    - 缩容版：capacity==24、resident<=24、evictions>=1、至少 1 行
      "budget: eviction key=<models/...>"（具名）、该行含 "key=models/" 和
      "action=unpublish,resupply"；
    - 恢复版（restore_all + 重编）：capacity==96、evictions==0、capacityRefusals==0、
      missingMesh==0、overlay rejected==0、overlay uploaded==prepared。
    这样"缩容会红、且红的是具名断言"是同一段代码里的读数，不用人去推。

(4) **新增 mutate.sh case m14（行为变异，长期防线）**。不是改容量，是改**排序**：
    把 prepare 准入的 std::stable_sort 比较体换成先到先得（main.cpp:3825-3828），重编
    tf2_demo_native，跑 `outline-check.sh --world-only`，要求
    OUTLINE-CHECK=FAIL + 至少 1 条具名 "FAIL named world models:" 行 + deferred>=1 仍成立。
    这条 case 存在的理由：把 96 抬到 160/192 的构建不改排序，目标集合照样被先到先得挤掉，
    所以它过不了 m14——这是"加常数"路线唯一的自动防线。

(5) 新增的第五次产品启动 `attach`（outline-check.sh:972，
    `--outline off --player-weapon-attach on --player-animation`）：
    `attach: meshSource` 只在 --player-weapon-attach 下打印，而原有 run_frame 不传它，
    所以旧门禁里读这一行会恒读成空（review B2）。outline off 是为了不动 overlay 像素夹具。
    这一行本身我把两个参数都写错过（裸开关 = attach 实际关闭；缺 --player-animation =
    没有握持点），见 §8——这也是本轮"门禁参数写错、产品静默按另一套配置跑"的现场证据。

### tick 域声明（lead m00789）
--capture-tick 不是 seek，是"播放越过目标 tick 再把游标夹回"，所以 trace 里 32979/34717/37773
这些 PLAYBACK 秒数上的拒绝不是"用户在该 tick 看到的东西"。本门禁只用 trace 做
**mesh 上传计数**（预算/准入/上传/拒绝/驱逐），不做像素声明；"同一帧两进程一致"是单独一条
证据（evidence/t85/determinism/），不是本段的推论。

## 6. 验收读数

（本节数字全部来自实际跑出来的日志；证据路径见 §7。凡是还没跑到的项，这里写"待回填"，不写预测值。）

### 6.1 复现：同一 fixture、同一 tick、同一 argv 的前后对照

fixture 身份：demo sha256 7e047aff1f7743915e355eac907a95dc3d981aa0fae5591b6b15346fb77dc4a3（75,663,754 B）；
地图 pl_upward_f12.bsp 25,252,306 B；class=pyro，ent=3，tick=30001；eye 1707.0 -125.1 152.8，pitch 4.1，yaw 0；
--playback-speed 32 --player-animation --player-weapon-attach on；完整 argv 见 §7。

BEFORE（冻结候选 t79 的 exe，sha256 a3c93d5755fd331351766dfb991fe17f90c4dfb753ad7ef9f53391f0303724e9，trace 7,629,535 B）：

- 层 A 读数 `28232.1 gpu: models done 64 uploaded=64`（trace 第 119 行）——世界 pass 在 64 处切断。
- attach 读数从 `attach: meshSource supplied=10 refused=0` 涨到 `supplied=23 refused=3`。
- 三次拒绝就是缺陷本身（trace 第 8887/13421/21668 行），全部 lastError=0x8007000E：
  `key=models/weapons/c_models/c_sapper/c_sapper_xmas.mdl#333942364`（32979.7）、
  `key=models/weapons/c_models/c_shogun_warhorn/c_shogun_warhorn.mdl#3753418525`（34717.1）、
  `key=models/weapons/c_models/c_flamethrower/c_flamethrower.mdl#1578078797`（37773.7）。
- 用户可见的失败：`gpu: pre-draw error hresult=0x8007000E drawn=178`（第 8924 行）、`drawn=179`（第 8971 行）；
  全文 601 行含 0x8007000E。
- 层 B/C/D 的读数在同一轮里的表现：`scene: entityRanges ranges=121 texturedRanges=121 texturedInstances=96 skinnedInstances=82 meshes=96 textures=280`（第 36518 行）——96 就是表容量，说明世界 64 + overlay 9 + attach 23 正好把预算吃满，最后三把枪必然落空。

- AFTER（本次构建的 exe，sha256 55efceb69cc7feae02ac8d5d8724cf92360ba2a284d2d3246664254fc8ce1650；
  trace evidence/t85/upward-pyro/coverage.trace 4,238,618 B，402 个 scene 帧）：

- 0 行含 0x8007000E；0 行 `attach: mesh result=refused`；0 行 `budget: pre-draw error`。
- 层 A（世界模型"准备"预算；容量仍是 64，**没有抬**）：`budget: prepare capacity=64 candidates=98
  admitted=64 built=64 deferred=34 failed=0
  admittedByPriority=player:9,weapon:43,overlay:0,prop:12,firstPersonOnly:0
  deferredByPriority=player:0,weapon:0,overlay:0,prop:25,firstPersonOnly:9`
  —— 98 个候选里 34 个按优先级被 defer，被 defer 的只有 prop 25 + 第一人称专用 9；玩家 9 与武器 43 一个没掉。
- 世界 pass 计数（BEFORE 里 outline 门禁钉住的正是这条）：`gpu: models done 64 uploaded=64` —— **没有变**。
- 层 B（实体 mesh 表；容量仍是 96）：`budget: meshTable capacity=96 resident=86 evictions=0
  capacityRefusals=0 reuploads=0 resupply=0
  residentByPriority=player:9,weapon:56,overlay:9,prop:12,firstPersonOnly:0`
  —— 这一行现在在每帧 attach 之后结算打印（§5 的 B3 修正）。
- 层 C（每帧实例表；容量仍是 96）：`budget: instances capacity=96 accepted=59 slots=170
  rejected=missingMesh:0,deferredNotResident:37,badMesh:0,nonFinite:0,budget:0`
  —— 37 全部是"层 A 明确推迟、未准备"的实例（deferredNotResident）；"该有却没有"的 missingMesh 是 0。
- 层 D（本帧绘制预算；容量仍是 96）：`budget: draw capacity=96 playerBudget=13 propBudgetAfterAttach=70
  playerDrawn=13 propDrawn=70 attachReserved=13 attachDrawn=13 droppedPlayers=0 droppedProps=82`
  —— 三个子预算仍严格加和到 96。
- 世界模型 attach 的请求账（每帧一行；数字是**整轮累计**，402 帧、每帧 13~17 次请求）：
  `attach: coverage requested=5988 resolvedFromWorld=5975 uploaded=13 refused=0 retrySkipped=0
  unresolvedPath=0 meshes=13`。逐行平衡（requested == resolved+uploaded+refused+retrySkipped+
  unresolvedPath）违例 = 0。**最后两帧相减就是捕获帧的读数**：requested 13、resolvedFromWorld 13、uploaded 0
  —— 捕获帧上 13 个持枪身体的武器世界模型全部由世界 pass 已准备的缓存解析，attach 一个都不必再建。
- `attach: meshSource supplied=13 refused=0 meshes=13 resupply=0`；13 条具名
  `attach: mesh result=supplied path=...`，其中 c_shogun_warhorn（verts=4017 runs=1）、
  c_flamethrower（verts=14880 runs=1）都在；c_sapper_xmas 在捕获帧没被持有，所以 supplied=0 是正确读数，
  而 BEFORE 里它正是第一个被 0x8007000E 拒掉的模型。
- 实体级覆盖（每帧）：`scene: weaponAttach on=1 held=13 drawn=13 poseMissing=0 overBudget=0 refused=0
  noHoldPoint=0 noWorldMesh=0 skippedStandalone=11 firstRefusal=-` —— 与 BEFORE **完全相同**。
- 没有抬任何常量：层 A 64、层 B/C/D 96 全部原值（§2 的四层约束原封不动）。

### 6.2 同帧像素

before.bmp 与 after.bmp 的 sha256 都是 d297340f6bbd1c9edfe9d5674fe989c0cd793e4f067df33b629fbf062476987c。
必须这样读：**"同一帧未变"**，不是"修复没有视觉效果"。三次拒绝发生在 32979.7/34717.1/37773.7 秒，
都晚于 30001 的捕获 tick；本 fixture 的像素只覆盖到捕获那一帧，所以它证明的是"改动没有动过既有帧"，
而不是"目标模型在像素上被画出来了"。

### 6.3 验收项状态

| # | 项 | 读数 | 状态 |
|---|---|---|---|
| 1 | 干净构建 | job bash-a769a10a74874246 rc=0，[100%]，exe_count=53 | 通过 |
| 2 | outline-check.sh | 断言数 102(HEAD) → **173**（口径：grep -cE '^[[:space:]]*assert_(eq|ne|ge|le|lt|contains)\b' outline-check.sh）。lease 1 整轮跑满 2700 s 被驱动超时杀掉：**rc=124、160 条 OK、9 条 FAIL、未打印终判**；9 条全部定位到三个根因并已修（§6.5），但改动**未编译、未复跑** | 分支上**未验证**（按 lead m01908 改为合并树验收） |
| 3 | third-person-weapon-check.sh | 未跑（lease 2 一直没轮到；命令、输入副本、设置隔离都已就绪） | 分支上**未验证** |
| 4 | player-body-appearance-check.sh | 未跑（同上） | 分支上**未验证** |
| 5 | 两进程同帧确定性 | 未跑（同上） | 分支上**未验证** |
| 6 | 三个模型不再 OOM | BEFORE 3 次 0x8007000E → AFTER 0 次（§6.1） | 通过（附带 §6.1 的 tick 域限定） |
| 7 | 预算/prepared/drawn/refusal 读数 | §6.1 全部四层都有实数 | 通过 |

**这张表的读法**：第 1、6、7 项是在**已经构建过的那一版源码**（exe sha256 55efceb6…4fc8ce1650，§6.1）上
实跑得到的；第 2–5 项所属的验收要在合并树上跑，因为 lead m01908 明令"分支上不再排整轮 outline 的租约"，
而且**本轮最后一组改动（--window-size、submitted、门禁补丁）还没有编译过**。凡是在未编译的源码上
"写通过"都是假证据，所以 2–5 一律写"未验证"。

### 6.4 「23 vs 13」的对账（lead m01475 / m01584 第 2 项）

**结论：是口径差，不是覆盖缩水。** BEFORE 的 `supplied=23` 与 AFTER 的 `supplied=13` 数的不是同一件事：
`attach: meshSource supplied` 只数 **attach 自己动手建的** mesh；世界 pass 已经准备过的武器，attach 只做一次
"取现成 key"的早退（native/src/main.cpp 的 supplyAttachMesh：先查 entityMeshKeyByPath /
entityMeshKeyByNormalizedPath，命中就直接返回、不花一次上传），这种命中不计入 supplied。

五条互相独立的证据：

1. 实体级覆盖：前后都是 `scene: weaponAttach held=13 drawn=13`（每帧、同一 tick）。
2. 世界 pass：前后都是 `gpu: models done 64 uploaded=64`。
3. 请求账：`requested=5988 = resolvedFromWorld 5975 + uploaded 13 + refused 0 + retrySkipped 0 +
   unresolvedPath 0`，逐行平衡违例 0 —— 5988 次请求每一次都有具名出口，没有"悄悄消失"的路径。
4. 逐帧差分：捕获帧 13 次请求全部 resolvedFromWorld，uploaded 0。
5. 模型级集合对账（对两份 trace 的 `attach: mesh result=supplied path=...` 取集合）：
   - BEFORE 现场建 23 个；AFTER 现场建 13 个；
   - 只在 BEFORE 出现、AFTER 不再现场建的 12 个：c_axtinguisher_pyro、c_frying_pan、c_medigun、
     c_minigun、c_sandwich_xmas、c_scattergun、c_crusaders_crossbow_xmas、c_fists_of_steel、
     c_riding_crop、c_scout_sword、c_tomislav、c_winger_pistol —— 它们现在在层 A 的 weapon:43 里，
     由世界 pass 准备，attach 只解析；
   - 只在 AFTER 出现、BEFORE 没有的 2 个：c_flamethrower、c_shogun_warhorn —— 正是 BEFORE 被 0x8007000E 拒的其中两个；
   - c_sapper_xmas（BEFORE 第 1 个被拒）在 AFTER 里由世界 pass 准备，所以不出现在 attach 的 supplied 里；
   - 合计：26 个不同武器模型 = BEFORE 的 23 建 + 3 拒 = AFTER 的 13 建 + 13 解析。

**附带把 rejected= 拆干净**（同日要求的第 3 项）：`rejected=missingMesh:0,deferredNotResident:37,...`。
37 是层 A 明确推迟、从未准备的实例；missingMesh（该有却没有，m8 缺陷类）为 0。两个桶在源码里就分开计数
（native_renderer_entity.cpp 的 setEntityModelInstances：实例带 budgetDeferred 标记进 deferredNotResident，
否则进 missingMesh），门禁两条都断言，所以"推迟"变"空洞"这种回归藏不进大数字里。

### 6.5 outline-check 整轮（lease 1）：跑满 45 分钟被超时杀掉，9 条 FAIL 全部归因并已修

驱动 evidence/t85/run-outline-full.sh，租约票据 1794035980925433300-t85-budget-129698，15:25:51 拿到锁，
日志 evidence/t85/acceptance-outline/lease.log（264 KB）。

**结果（必须照实读）**：timeout --kill-after=60 2700 到点杀死，**rc=124，整轮没有打印终判**；
被杀时现场 160 条  OK   、9 条  FAIL，最后进到 === 6/6 the toggle === 段的 OK 行之后。
所以这不是"通过了"，是"跑到一半被我们自己砍断"。本夹具整轮成本 **> 2700 s**（speed=1 单次实测 999 s），
与"约 25 分钟"的估计不符——合并窗口要按 > 45 min 设超时。

**9 条 FAIL = 1 + 7 + 1，三个根因，都不是产品缺陷：**

1. **1 条 speed 不变性**（--playback-speed CHANGES THE FRAME）：几何不同，不是内容不同。
   speed1.bmp 10,882,614 B = **2560x1417**，同轮其它帧 2,582,406 B = **1264x681**
   （BMP 头 od -An -tu4 -j18 -N8，两者 24 bpp）；主屏实测 2560x1440，2560x1417 = 屏幕减 23 px 标题栏。
   产品源码里唯一会动窗口的是 WM_DPICHANGED（native/src/main.cpp:1407 的 SetWindowPos），
   而 speed=1 恰好是这轮里最长的那次运行（999 s）。**修法不是放宽断言**（lead m01857 的排序）：
   产品加 --window-size WxH（钉外框，WM_DPICHANGED 见 pin 就跳过）、打印 capture: frame width/height/dpi
   与 window: created outer/client/dpi/pinned，门禁 4 条断言（§8.6）。
2. **7 条层 A**：trim_trace 只保留 outline/gpu/window/hud/logstf/scene: begin/item/title + 尾 2000 行，
   而 budget: prepare 是启动时打印一次 —— 实测 on.trace 里出现 **0 次**，于是 7 条断言读回 none/0 而红。
   修法：trim 追加 grep -aE "budget:|capture:|attach: |scene: weaponAttach"。
3. **1 条层 C**：我把 slots 当成"提交数"在比。实测最后一行
   accepted=95 slots=236 rejected=missingMesh:0,deferredNotResident:1 —— 95+1=96 正好是容量，**账是平的**；
   slots 是 instance ROW 数（每个 material run 一行，native_renderer_entity.cpp:588），不是实体数。
   修法：产品在 budget: instances 里加 submitted=draws.size()，门禁断言
   accepted+missingMesh+deferredNotResident+badMesh+nonFinite+budget == submitted 且 slots >= accepted。

另有一条**亲和性**读数（不是缺陷）：层 B 的 meshTable 行在这轮里读的是**启动时**那一条
（resident=68，因为启动时表还没满），所以 evictions/capacityRefusals 结构性为 0 —— 这正是 lead 评审 B3；
现在的代码在每帧结算处（renderer.setEntityMarkers 之前）再打一条，本轮未编译，故下一轮才可见。

**按 lead m01908 的处置**：分支上不再重跑整轮；这一跑的证据到此为止（rc=124），
改动后的"合并树绿"由 lead 在合并窗口用同一条门禁取得。

## 7. 证据清单

| 路径 | 内容 |
|---|---|
| evidence/t85/upward-pyro/before.trace（7,629,535 B） | 冻结 t79 exe 在同一 fixture 的完整 trace：三次拒绝、601 行 0x8007000E、`gpu: models done 64` |
| evidence/t85/upward-pyro/after.trace（8,267,305 B） | 本次构建的完整 trace（中间态构建；sha256 见 §6.1 的说明） |
| evidence/t85/upward-pyro/before.ledger.txt / after.ledger.txt | 上面两份 trace 里四层 budget/prepare/instances/draw 行的摘录 |
| evidence/t85/upward-pyro/before.bmp / after.bmp | 同帧像素对照，sha256 相同（§6.2） |
| evidence/t85/upward-heavy/before.ledger.txt | 第二个 fixture（heavy，ent=11，tick=24001）的 before 读数：refused=3、417 行 0x8007000E |
| evidence/build-baseline.log | 干净构建的完整输出（rc=0） |
| evidence/t85/outline-check-nomutation.log | 14:01 那次整轮 outline-check：被 launch-site 守卫在自检阶段判红（见 §8.3），0 条断言执行——这是守卫有效的现场证据，不是验收读数 |
| evidence/t85/GATE-ASSERTION-INVENTORY.md | 断言清单：HEAD 102 条 vs 现在 159 条，逐条列旧读数/新字段/谓词 |
| evidence/t85/run-acceptance-batch.sh | 验收批次驱动（六步串行、每步超时、结束杀进程树） |
| evidence/t85/run-upward-attach.sh / run-weapon-oom.sh / run-determinism.sh / run-budget-boundary.sh / run-budget-priority-mutation.sh | 复现与变异脚本（每个都打印 exe/demo/gate 的 sha256 与完整 argv） |
| evidence/t85/inputs/ | 交给产品的设置/UI 副本（绝不把被跟踪的 settings.json/ui.json 直接递给产品） |
| evidence/t85/upward-pyro/coverage.trace（4,238,618 B） | §6.1 AFTER 的完整 trace（含 §6.4 的请求账 `attach: coverage`，每帧一行，402 帧） |
| evidence/t85/upward-pyro/coverage.ledger.txt | 上面那份 trace 的四层读数 + coverage + 平衡检查摘录 |
| evidence/t85/run-coverage-after.sh | 打补丁（幂等）+ 构建 + 跑 upward 的驱动，打印 exe/demo/settings/gate 的 sha256（sha256 b8a54704362acfaa83e3cd204551ab454e39f0a8903039386757277560841076） |
| evidence/t85/run-outline-full.sh | 验收 lease 1：整轮 outline-check（timeout 2700 s），结束打印 lock-after 与 product_processes 断言（sha256 154ca8177b46921abe0ec6bb64c612ded9dde90d4a38fab3d25c78c0d00d3766） |
| evidence/t85/acceptance-outline/lease.log | lease 1 的现场日志（rc、断言计数、四层实测行、wall time） |
| evidence/t85/run-acceptance-batch.sh | 验收批次驱动：lease 2 用 T85_BATCH_SKIP 跳过 outline，determinism 排在 m14 变异之前（sha256 40a977152039c67b7434fc1421c767e2308a9e00c5cd7b688f4385a6c87cba8a） |
| 待回填 | evidence/t85/acceptance/{third-person,player-body}/、determinism 输出、m14 变异证据（commit 后真跑 mutate.sh m14） |

## 8. 我把参数写错过什么，怎么保证不再犯

这一节是本轮"门禁写错参数、产品静默按另一套配置跑"的现场记录。两次都是我写的，都是靠读数对不上才发现的。

### 8.1 裸写 `--player-weapon-attach`

第一次的 argv 是 `--player-weapon-attach`（没有值）。产品的解析器在 native/src/main.cpp:2795-2802 里
要求 `on|off`：`arguments[++i]` 取到的不是 on/off 就置 playerWeaponAttachArgumentRejected；
main.cpp:2803-2806 的注释写的是"裸开关不是 on：猜一个值会悄悄改掉调用者想保留为基线的帧"，
main.cpp:3099-3104 打印 `invalid --player-weapon-attach; it takes on|off` 并 `return 13`。

要点（比我最初的描述更准确）：裸开关不是"静默关掉 attach"，而是**产品直接退出 13**。所以门禁会红在 rc 断言上，
但那是"测到一个退出码"，不是"测到 attach 关着"——如果哪天门禁没有 rc 断言，这就变成静默按另一套配置跑。

修正后的 argv 原文（outline-check.sh:246-253 的世界夹具；run_frame 的通用 argv 在 :932-949）：

    --tf-root "$TF" --hidden-window --audio-disabled
    --settings-file "$T85_WORLD_SETTINGS" --ui-settings-file "$T85_WORLD_UI"
    --demo "$T85_WORLD_DEMO" --capture-tick "$T85_WORLD_TICK" --playback-speed 32
    --capture-frame "$prefix.bmp"
    --debug-camera $T85_WORLD_EYE
    --player-animation --player-weapon-attach on

### 8.2 写了产品不认识的 `--no-player-animation`

原脚本里有 `ANIM_ARGS=(--no-player-animation)`，但产品的 CLI 循环里没有这个开关
（grep 'L"--no-player-animation"' native/src/main.cpp = 无命中），于是它被**静默忽略**。
产品侧对应的是 `--player-animation`（main.cpp:2807），且默认是关的：main.cpp:2125
`bool playerAnimationEnabled = false;`。也就是说"不加就对了"，但我当时写了一个看起来在关、
其实什么都不做的开关——这比写错更危险，因为读脚本的人会以为动画被显式关掉了。

修正：删掉这个数组的内容，改成 `ANIM_ARGS=()` 并把原因写成注释（outline-check.sh:840 附近），
"关"靠**省略**表达，不靠一个不存在的开关。

对照证据：t83 在**同一个 demo、同一 tick 64161** 上用 `--player-animation --player-weapon-attach on`
拿到 `attach: meshSource supplied=1 refused=0 meshes=1`（evidence/t83-weapon/weapon-attach/*.summary）。
这说明本门禁缺的从来不是开关本身，而是**这两个参数一起给**——参数没给对时，读数会"结构性地为 0/为空"，
看起来却像产品没实现。

### 8.3 逐 flag 自查记录（argv 里每个 flag 都在产品解析器里存在）

方法：把 argv 里的每个 `--flag` token 拿出来，在 native/src/main.cpp 里 grep `L"--flag"`，取首个命中行号；
无命中的当场删除或改正。本轮实测（outline-check.sh 交给产品的全部 flag）：

| flag | 产品解析器命中行 |
|---|---|
| --tf-root | main.cpp:2737 |
| --hidden-window | main.cpp:3010 |
| --audio-disabled | main.cpp:3012 |
| --demo | main.cpp:2739 |
| --capture-tick | main.cpp:3026 |
| --playback-speed | main.cpp:2426 |
| --capture-frame | main.cpp:3020 |
| --settings-file | main.cpp:1900 |
| --ui-settings-file | main.cpp:1909 |
| --outline-vertex-extent | main.cpp:2453 |
| --debug-camera | main.cpp:2547 |
| --outline | main.cpp:2435 |
| --outline-depth | main.cpp:2444 |
| --player-weapon-attach | main.cpp:2795 |
| --player-animation | main.cpp:2807 |
| --capture-depth | main.cpp:3022 |
| --world-depth | main.cpp:2567 |
| --viewmodel | main.cpp:2769 |
| --no-player-animation | **MISSING（已删除）** |

脚本里其余 `--` token（`--mutation`、`--world-only`、`--skip-live`、`--fixtures`、`--ticks`、`--team`、
`--depth*`、`--self-test` 等）是门禁自己的 CLI 或 outline-regions.py 的参数，不进产品 argv，不在上表内。
third-person-weapon-check.sh 与 player-body-appearance-check.sh 用同样方法查过：无幽灵 flag
（`--dry-run`/`--logstf-*`/`--model-*`/`--viewmodel-*` 是门禁本地参数，`--ids` 属于 player-body-appearance-pixels.py）。

### 8.4 今后的四条硬规则

1. **argv 只在 helper 里构造**：`run_frame()` / `run_world_fixture()` 负责拼最终 argv，调用者只能追加，
   不能漏 `--audio-disabled`/`--hidden-window`/设置隔离。新增夹具也只能走这两个函数。
2. **全文件只允许一处启动产品**：`LAUNCH_SITES=$(grep -c 'timeout.*"$EXE"' "$0")`，`!= 1` 就 FATAL
   （outline-check.sh:584-588）。这不是形式主义：14:01 那次整轮 outline-check 正是被它挡下来的——
   日志 `FATAL: this gate has 2 product launch sites`、`LOCKED-COMMAND-EXIT rc=1`、0 条断言执行；
   修好（世界夹具改走 launch_product）之后同一行命令的计数实测为 1。一个人为加的第二启动点会漏掉
   audio/设置隔离，这条守卫是唯一能自动发现它的地方。
3. **带值开关一律写 on/off**，裸开关 = 退出 13；不要用"看起来像否定"的开关名表达关闭。
4. **加 flag 前先 grep 产品源码**：`grep -n 'L"--flag"' native/src/main.cpp` 必须有命中行，
   命中的行号写进本节的表；没有就当场删。
   （未知参数目前被产品静默忽略、没有 unrecognized 计数——这是产品侧的债，已被 lead 单列，不在本条范围内。）

### 8.5 我第三次写错：在门禁正在运行的那一刻改它

14:27:32 我把覆盖账本的补丁脚本跑在了 **真实的 outline-check.sh** 上（脚本里把目标路径硬编码成
`outline-check.sh`，本该在副本上试跑），14:27:38 用备份 `cp` 回去。前后只有 6 秒，但方向是错的：

- bash 是**边读边执行**的：被 `cp` 截断重写的脚本，可能让它在不同字节偏移上接着读，轻则语法错、
  重则执行到一半的旧文本；而且这时**验收批次已经在 14:26:48 拿到锁、正在跑 outline-check**
  （批次日志记录的门禁 sha256 = `15a6d869...c809bc767`，正是恢复后的内容，说明批次开跑时文件还是好的）。
- 幸好批次哈希的是好文件、校验用的也是好文件；但这属于"自己给自己制造不确定性"，不能靠运气。

三条硬规则（已并入 8.4 的执行面）：

1. **补丁脚本的第一个参数就是目标路径**，默认值只能是 `.scratch/` 下的副本；
   在真实文件上跑之前，先 `python patcher.py .scratch/copy.sh && bash -n .scratch/copy.sh`。
   （事故后的修法就是这一条：`P=sys.argv[1] if len(sys.argv)>1 else ...`。）
2. **有锁在跑、或批次在跑的时候，不改任何会被它读到的文件**（门禁脚本、驱动、native 源码）。
   要改就等批次结束；改完必须 `bash -n` + 逐字节恢复验证（`cmp`）。
3. 事故当天可复核的痕迹：`outline-check.sh` sha256 恢复为 `15a6d869db2df3aec1faf07e7b336e954116fd5a72bcf833664d826c809bc767`，
   `.scratch/oc-copy.sh`（补丁前副本）与它 `cmp` 相同；`grep -c 'attach: coverage' outline-check.sh` = 0，
   即那次误打的内容一个字都没留在门禁里。

### 8.6 本轮最后加的开关 --window-size WxH（以及它带的两个诚实话题）

**为什么加开关而不是放宽断言**：lead m01857 的排序是 (a) 有 pin 尺寸的开关就钉住它、(b) 没有就加一个、
(c) 只有在 A/B 证明冻结候选也一样的前提下才降级为具名 skip。产品的窗口在一轮里被桌面改成了 2560x1417
（§6.5 根因 1），这不是渲染结论，所以按 (a)→没有→(b) 做：

- native/src/main.cpp：wWinMain 预扫 argv，--window-size WxH（swscanf_s L"%ux%u"，
  320x240..8192x8192 之外直接 return 13），把 CreateWindowExW 的 1280,720 换成它；
  WM_DPICHANGED 里 if (lParam && !g_windowSizePinned) 才 SetWindowPos，并打印
  window: dpi-changed dpi=%u pinned=%d client=%ldx%ld；创建后打印
  window: created outer=%dx%d client=%ldx%ld dpi=%u pinned=%d。
- 捕获时打印 capture: frame width=%d height=%d dpi=%u（读 renderer 的 back buffer 尺寸，
  native/include/native_renderer.h 新增 frameWidth()/frameHeight()）。
- 门禁侧 4 条断言：speed=1 与 speed=SPEED 帧尺寸相同；本租约每张 capture 都打印了尺寸；
  本租约只有一个尺寸；捕获尺寸 == window: created 的 client 尺寸。**断言没有放宽、也没有删除**，
  原来是"两张图字节相同"，现在是"先证明两张图形状相同，再证明字节相同"。

两个诚实话题：

1. **冻结候选不认识这个开关**（它是本轮新加的），而产品对未知参数是**静默忽略**的（没有 unrecognized 计数）——
   这是产品侧的债，已被 lead 单列。所以 evidence/t85/run-speed-ab.sh 给冻结候选的 argv **不带**
   --window-size：给一个会被忽略的开关做 A/B，比的是空气。A/B 脚本必须显式传 --ab-speed 才会启动
   产品（4 次启动），默认只打印用法并退出 2。
2. **--window-size 是否真的能压住 DPI 变化，本轮未验证**：改动未编译，A/B 也没跑（lead m01908 把
   那一跑放进合并窗口）。能证明的只有：开关存在（源码行）、算术自洽（2560x1417 = 屏幕减标题栏）、
   修复路径符合 (a)(b) 顺序。**不能**在报告里说"speed 断言已经绿了"。

同样地，hostlock.sh **没有 extend 子命令**（子命令只有 run/status/break；grep 无 extend/help），
所以长租约只能一次给够超时，不能中途续约 —— 这也是把整轮 outline 放到合并窗口的一层理由。

### 8.7 顺手记一笔：行尾被我自己写坏，diff 一度变成"整文件重写"

提交前 numstat 里 outline-check.sh 是 **2575 增 / 2074 删**，而实际改动只有几百行。
原因不在门禁，在我几个补丁脚本：本轮最后两次补丁读文件时做了 CRLF→LF 归一，回写时又补回 CRLF，
于是有 44 行被写成双 CRLF（实测该字节序列计数为 44）。这个仓库 core.autocrlf=true，
工作区脚本本来就是 CRLF、blob 是 LF；一旦混进双 CR，git 就不再走归一化，整文件都算改动。

修法与验证：把双 CRLF 归一成 CRLF 回写后 bash -n 通过，git diff --numstat outline-check.sh
从 2575/2074 变成 **548/47**（真实改动），git diff 首块从"@@ -1,2074 +1,2575 @@"（整文件）变成
"@@ -104,6 +104,28 @@"（局部）。教训：**补丁脚本回写前先断言文件里双 CRLF 计数为 0**，
并在改完后立刻看 numstat 的量级——一个脚本的改动不该让一个 2600 行的文件"整篇重写"。
