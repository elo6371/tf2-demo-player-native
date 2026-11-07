# POSTFREEZE-INVENTORY-CORRECTED — 冻结后欠账账本对账（取代版）

> ## 取代声明（先读这一段）
>
> **本文件自 2026-11-07 起取代 `D:/_shared/postfreeze-inventory.md`（82 行，mtime 2026-11-07 00:40，标题 "Post-freeze gap inventory (E1/E2)"）。后者不得再作为权威引用。**
>
> 原因：它记录的是 **pass-7 快照**（40 PASS / 5 UNSUPPORTED、18 步清扫），而 `e15577b` 上的 pass-8 已经把五格武器与两条队伍对被关闭；它还有两处 `CHARACTER-MATRIX.md` 行号漂移（`:297`/`:422`）。下面逐行对账，保留它的原始出处与日期，给出 resolved / still-valid / stale / insufficient-evidence 四种判定。
>
> 本文件不修改 `D:/_shared/postfreeze-inventory.md`（外部文件，只读）；也不改产品、不改门禁。

## 0. 对账方法与四个判据

- 对账锚点：`e15577b4f055…`（t79-integration / integration/t79-final），产品 exe sha256 `a3c93d57…724e9`（本次对 t79-integration 内二进制实测，与 `RUNS-2026-11-04.md` §26.7 一致）。
- 权威读数：`T62-REPORT.md:10`（pass-8）、`CHARACTER-MATRIX.md:202/:206/:294/:313/:362/:375/:379`、`MAP-MATRIX.md:26-34/:40/:183`、`RUNS-2026-11-04.md:1959-2029`（§26）。
- 四个判据：
  - **resolved**：旧条目描述的矛盾/缺口已被后续已提交证据解释或关闭，且能在本仓读到读数。
  - **still-valid**：旧条目的判断在 e15577b 上仍然成立（证据位置可能变了，但结论没变）。
  - **stale**：旧条目描述的是已被取代的快照或已被修正的文档段落。
  - **insufficient-evidence**：既有证据不足以判真或判假（本次不替它下结论）。
- 判定不改变旧文件的历史地位：它的"gate 存在 ≠ 覆盖"原则仍然有效（原 `:5`）。

## 1. 为什么必须取代：六个硬矛盾

| # | 旧文件说 | e15577b 上的现状 | 判定 |
|---|---|---|---|
| 1 | `40 character PASS / 5 UNSUPPORTED`（原 `:7`，引 `T62-REPORT.md:8`） | `T62-REPORT.md:10`：pass-8 "Nine C cells now read PASS…"；`CHARACTER-MATRIX.md:362`："45 PASS, 0 NOT VERIFIED, 0 UNSUPPORTED, 0 FAIL"。40/5 是 pass-7 快照 | **stale** |
| 2 | Scout/Sniper 无同队对，upward "16 bodies and seven pairs"（原 `:18`/`:52`） | 18 目标两批（batches）已补齐九对：`CHARACTER-MATRIX.md:178-179`，`RUNS-2026-11-04.md:1999`（§26.5）、`:2021`（证据目录） | **stale**（缺口已关） |
| 3 | Heavy 身份"文档内部矛盾"（原 `:53`，引 `CHARACTER-MATRIX.md:297` 与 `:422`） | 真值在 `:294`/`:313`："Heavy now uses a real Heavy player body (ent=11, blue, AttackStand_SECONDARY)"；`:438` 是过期段落；`:297` 现在是 Sniper 的 D2 行、`:422` 是证据索引行 | **stale**（矛盾源自引用漂移） |
| 4 | C 轴证据分裂：旧矩阵只有四个 snakewater、upward 225° 未索引（原 `:50`） | `CHARACTER-MATRIX.md:202`（9/9 PASS）、`:206`（snakewater cases=4 phase-pinned=4 …；upward 同形计数）；`T62-REPORT.md:10` 记录九格由修产品关闭 | **stale** |
| 5 | 冻结清扫覆盖 "18 T62-dependent steps, not all 66"（原 `:11`，引 handoff :185） | 这句本身作为"那次清扫"的说明成立，但被当成"当前覆盖"就会误导：矩阵数字已在 `65315d2`/`931588b`/`b859dd8` 后重算，`MAP-MATRIX.md:4`/`:40` 有 20/20 的新计数 | **stale（作为当前结论）/ 有效（作为历史）** |
| 6 | —（旧文件没有这条） | 本仓自身原有两处过期文档行：`T62-REPORT.md:124` 的 "character count is 40 PASS / 5 UNSUPPORTED"、`CHARACTER-MATRIX.md:438` 的 "only the class-fallback body appears as Heavy / 16 bodies and seven pairs"。2026-11-07 task-6 已在两段后各加 `Superseded, annotated` 引用块（历史数字保留、未删除）；引用时以本文件 §3 为准 | **resolved（文档缺陷已加注，未删历史）**，见 DEBT-LIST D-13 |

## 2. 逐行对账

### 2.1 原 §0「gate 覆盖 vs 纯文字声明」十行（原 `:15-24`）

| 原行 | 旧声明（摘要） | 判定 | 依据（e15577b） |
|---|---|---|---|
| `:15` | Character A + static B 由 `player-model-check.sh` 审九模型/负控/LOD0/涂装槽/动画源；非 LOD0、被绘制 BLU stem、Heavy/Spy 头部不在其内 | **still-valid** | `player-model-check.sh:213`（`lod0Truncated`）、`:294`（PASS 行）、`:184-190`（四项具名负控）；residue 仍在 `CHARACTER-MATRIX.md:131/:149-153` |
| `:16` | D1 由 `player-animation-check.sh` 断言序列表/绑定/四分周期姿势/连续性/时钟；服务器序列、屏幕 walk/reload/airwalk、driven attack 不在内 | **still-valid** | `player-animation-check.sh:378` PASS（233 asserts / 0 fail，`evidence/t61-animation/r10-after-demos/check.log`）；非承诺项 `CHARACTER-MATRIX.md:387-400` |
| `:17` | D2 由 `class-pixels-check.sh` 绑身体/tick/identity 双进程；一样本≠全体；聚合日志仍 class-blind | **still-valid** | `class-pixels-check.sh:108`（`MIN_BODY_PIXELS=200`）、`:401`；`T62-REPORT.md:102`（D2 第二身体仍 Open） |
| `:18` | 队伍外观七对不推九对，Scout/Sniper 开放 | **stale** | 九对已测：`CHARACTER-MATRIX.md:178-179`；`RUNS §26.5` `:1999`/`:2021`；`player-body-appearance-check.sh:287` |
| `:19` | TP 武器只测四个 snakewater 45°，upward 225° 需另行索引 | **stale** | 九格：`CHARACTER-MATRIX.md:202/:206`；`third-person-weapon-check.sh:580`（九项计数）/`:582`（PASS）；`:82-102` 仅列默认输入 |
| `:20` | crouch/crouchwalk/flinch 只证 legacy/default 等价与 state trace；flinch 身体不可见 | **still-valid** | `animation-drive-states-check.sh:446`（PASS 长行）；默认 legacy `main.cpp:2133` |
| `:21` | 地图材质/覆盖：身份、fetch/atlas/tile 不变量、tick、非相同帧对；像素差只记录 | **still-valid** | `map-material-matrix.sh:624`；不变量 `:36`；像素差语义非 oracle `MAP-MATRIX.md:36` |
| `:22` | 地图几何/光照：面账/空/暗、拒绝非法姿势、两姿势帧 pin + cull 账；不证明每次 lightmap 解码、每个视角 | **still-valid** | `map-geometry-lighting-check.sh:939`；解码缺口 `MAP-MATRIX.md:183` |
| `:23` | boneMismatch 语义门禁只固定含义，不断言普适正确或稳定绝对值 | **still-valid** | `bone-mismatch-semantics-check.sh:229`（PASS） |
| `:24` | 第一人称自然动画：门禁没有 `--viewmodel-apply-animation` | **still-valid** | `grep -n 'viewmodel-apply' weapon-animation-check.sh` → **0 命中**（本次实测）；默认 `main.cpp:2117` |

### 2.2 原 §0「有用排除」三条（原 `:30-32`）

| 原行 | 旧声明 | 判定 | 依据 |
|---|---|---|---|
| `:30` | boneMismatch 不是渲染缺陷，不得靠"强制归零"修 | **still-valid** | `bone-mismatch-semantics-check.sh:5-25`；语义而非绝对值 |
| `:31` | 不同动画相位不是像素回归；class-pixel 用 `maxDeltaCycle` 区分姿势变化与同相位渲染变化 | **still-valid**（且已成常规做法） | `class-pixels-check.sh:60-70`（相位钉住）；`third-person-weapon-check.sh:460-461`（phase-pinned 计数） |
| `:32` | 基础 TP attach 不再是开放 E1 | **resolved**（进一步关闭） | `T62-REPORT.md:10`：`supplyAttachMesh` 在 attach 时补齐被预算丢掉的 mesh，`noWorldMesh` 9→0，"supply=23 refused=3"；`docs/THIRD-PERSON-WEAPON.md:1-6` |

### 2.3 原 §1「产品缺口 E1」六行（原 `:38-43`）

| 原行 | 旧声明（摘要） | 判定 | 依据 / 变化 |
|---|---|---|---|
| `:38` | 没有服务器写入的玩家动画/模型还原：reader 能读 sequence/cycle/model-index，但采样到的 CTFPlayer 不带这些字段 → 职业回退 + 合成运动 | **still-valid** | `CHARACTER-MATRIX.md:396`；`main.cpp:2119-2132` 注释实测：pinned recordings 上 entity 的 sequence 1/29/30 都不落到该模型 descriptor，每帧按 `missing` 拒绝 → 画 bind pose（这正是`main.cpp:2117` 默认 false 的原因） |
| `:39` | 默认驱动只有 idle/moving；crouch/flinch 是 opt-in；无 Air/Jump、无 attack 驱动 | **still-valid**（T84 增加了 opt-in 状态层，默认未变） | `native/include/animation_clock.h:42`：`enum class AnimationDriveState : std::uint8_t { Idle = 0, Moving = 1 };`；`main.cpp:2133` `animationDriveStates=false` |
| `:40` | `CTEFireBullets` 有武器 id/方向，但时间线硬编码一枪，无 weapon-id → 序列映射；idle `AttackStand_PRIMARY` 不是 driven attack | **still-valid** | `native/src/demo_header_events.cpp:47`、`native/src/effects_timeline.cpp:42`（两处都只认 `CTEFireBullets`） |
| `:41` | 只构建 LOD0；VVD/VTX LOD 计数被读但不构建；无门禁切 LOD | **still-valid** | `native/src/model_loader.cpp:1568`（`buildBindPoseMeshLod0`）；`CHARACTER-MATRIX.md:131`；99 脚本无 LOD1+ 命中 |
| `:42` | 职业回退 ≠ 逐玩家模型身份（无 cosmetics/override 证明） | **still-valid** | `entity_model.cpp:699`（`defaultPlayerModelPath`）、`:782`；`CHARACTER-MATRIX.md:42`（bagel rows=39 withModel=36 classFallback=36；snakewater 20/14/14） |
| `:43` | 第一人称自然动画不是默认合成路径（apply 默认关，骨骼表被清） | **still-valid** | `main.cpp:2117` `viewModelApplyAnimation=false`；`:10933` 分支；`viewmodel-apply-check.sh:818` 只断言 opt-in 路径 |

### 2.4 原 §2「证据缺口 E2」十行（原 `:49-58`）

| 原行 | 旧声明（摘要） | 判定 | 依据 / 变化 |
|---|---|---|---|
| `:49` | 解码光照值无 pin（LDR/HDR 解码路径无按 face/luxel/channel 的数值样本） | **still-valid** | `MAP-MATRIX.md:183`（明确 OPEN）、`:189-192`；解码点 `native/src/bsp_map.cpp:587`/`:621`（本次核对为 lightmap 采样与 HDR exponent 行） |
| `:50` | C 轴证据旧矩阵/新 attach 结果分裂 | **resolved** | `CHARACTER-MATRIX.md:202/:206`；`T62-REPORT.md:10`；`third-person-weapon-check.sh:580` |
| `:51` | D2 每职业只有一具身体一个 tick | **still-valid** | `T62-REPORT.md:102`；`CHARACTER-MATRIX.md:390` §5(2) |
| `:52` | Scout/Sniper 缺同队对 | **resolved** | `CHARACTER-MATRIX.md:178-179`；`RUNS §26.5:1999/:2021` |
| `:53` | Heavy 身份在两处文档矛盾 | **resolved**（矛盾本身是引用漂移）；"真实 Heavy 存在"已确立 | `CHARACTER-MATRIX.md:294/:313`；旧文件 `:297`/`:422` 的引用在 e15577b 上是 Sniper 行与证据索引行 |
| `:54` | 静态材质证据不全：无 BLU stem 被某次 draw 命名；Heavy/Spy 头部外观在 T57 NOT VERIFIED | **still-valid** | `CHARACTER-MATRIX.md:149-153`（只 RED stem 被命名、`redBluDistinctSlotsTotal=14` 只证解析器能找 BLU）；Heavy/Spy 头仍在 T57 的 NV 清单 |
| `:55` | ViewModel apply 只有强制/部分证据，没有自然运行的 applied=1/outOfRange=0/单调相位/像素稳定性 | **still-valid** | `viewmodel-apply-check.sh:818`（33 asserts，opt-in 路径）；`viewmodel-animation-check.sh:1169`；默认 `main.cpp:2117` |
| `:56` | boneMismatch 解释不是九职业视觉正确性证明；明细分析只覆盖 Demo/Heavy/Scout | **still-valid** | `bone-mismatch-semantics-check.sh:229`；分析范围 `D:/_shared/t62-p1-bonemismatch/REPORT.md:244-339`（未重读，承接旧文件） |
| `:57` | 两姿势不是视角扫描；Odyssey pose 2 只是同眼位 90° yaw | **still-valid** | `MAP-MATRIX.md:102-104`（原文："a different viewpoint, not a sweep"） |
| `:58` | 玩家视角第三人称、更广的第一人称武器覆盖未采集 | **still-valid** | 本次未发现新证据；`CHARACTER-MATRIX.md:398`（一职业一武器一姿势） |

### 2.5 原 §3「按政策关闭」九行（原 `:64-71`）

| 原行 | 旧声明 | 判定 | 依据 |
|---|---|---|---|
| `:64` | Ragdoll 明确不做 | **still-valid** | `CHARACTER-MATRIX.md:343`（0/9）；支持边界 B1 |
| `:65` | T55 视频导出 UNSUPPORTED | **still-valid** | `CHARACTER-MATRIX.md:344` |
| `:66` | 水面反射/折射不做 | **still-valid** | 支持边界 B3；`world-material-effects-check.sh:16-21` 只有 warp+像素 |
| `:67` | koth_bagel_rc13 是缺 BSP 负控，不是矩阵行 | **still-valid**（本次补充：rc11 有自己的录音 `dfbce608…koth_bagel_rc11.dem`，rc13 没有对应 BSP） | `T62-REPORT.md:103`；`MAP-MATRIX.md:26-34` 第五行为 rc11 |
| `:68` | tick-0 帧哈希刻意不 pin | **still-valid** | `MAP-MATRIX.md:14` |
| `:69` | T73 全链 / T75 安装生命周期是独立交付 | **still-valid** | `T62-REPORT.md:127`；`docs/RELEASE-POLICY.md:6-9`（全链只在冻结树跑一次） |
| `:70` | 发布判定与 push 在 T62 之外 | **still-valid** | `T62-REPORT.md:127` |
| `:71` | 未受命的 UI 观察不转成验收任务 | **still-valid** | 本次未见对应门禁；`logstf-check.sh:397` 的"面板 PASS ≠ 页面显示"是同类边界的活例 |

### 2.6 原 §4「建议顺序」八条（原 `:75-82`）

| 原条 | 事项 | 判定 | 现状 |
|---|---|---|---|
| 1 | 先归档武器证据（4/9 vs 五 pass 歧义） | **resolved** | `RUNS §26.4`/`§26.5` + `evidence/t83-weapon/weapon-attach/`（51 MB）、`evidence/t62-matrix/body-appearance-upward/` |
| 2 | 加解码光照数值 pin | **still-valid / open** | `MAP-MATRIX.md:183` 仍 OPEN |
| 3 | 跑 Scout/Sniper 两个队伍对候选 | **resolved** | `CHARACTER-MATRIX.md:178-179` |
| 4 | 先解决 Heavy 身份，再加第二具 D2 身体 | **半 resolved**：身份已解；第二身体 **仍 open** | `CHARACTER-MATRIX.md:313` vs `T62-REPORT.md:102` |
| 5 | 默认改前先采自然 ViewModel apply 证据 | **still-valid / open** | `viewmodel-apply-check.sh` 仍只覆盖 opt-in |
| 6 | 先做 server-animation/attack 侦察再实现 | **still-valid / open** | `main.cpp:2119-2132` 注释已给出"这些序列不落 descriptor"这一侦察结论的一半 |
| 7 | 把 helper-bone 结构检查扩到余下六职业（若需要更宽保证） | **still-valid / open** | `bone-mismatch-semantics-check.sh` 范围未扩 |
| 8 | 最后再加真正不同的地图视角 | **still-valid / open** | `MAP-MATRIX.md:102-104` |

## 3. 当前权威数字（引用时用这一套，不要用旧文件）

| 量 | 值 | 出处 |
|---|---|---|
| 角色矩阵 | 9×5 = 45 格，**45 PASS / 0 NV / 0 UNSUPPORTED / 0 FAIL** | `CHARACTER-MATRIX.md:362` |
| 有具名渲染实体的格 | 18 / 45 | `CHARACTER-MATRIX.md:375` |
| 同职业红蓝对 | 9 对（7 对 tick 24001 + Scout/Sniper tick 45001） | `CHARACTER-MATRIX.md:379` |
| 地图矩阵 | 5 行 × 4 轴 = 20 格全 PASS（其中 4 格为 Odyssey 历史行，官方四图 = 16 格） | `MAP-MATRIX.md:26-34`、`:40` |
| 世界帧 pin | 100 个（每图每姿势 10 个） | `T62-REPORT.md:99-101` |
| 冻结 exe | 2,763,264 B，sha256 `a3c93d57…724e9` | 本次实测 + `RUNS §26.7` |
| D2 像素门槛 | ≥200 px、动画移动 share ≥5% | `class-pixels-check.sh:108`（`MIN_BODY_PIXELS`） |
| 队伍外观门槛 | floor 200 px mask / 15 tint / 40 pair gap | `player-body-appearance-check.sh:50` |

## 4. 仍然开放（与 DEBT-LIST 的对应）

- 解码光照数值 pin（E2-1）→ `docs/T85-DEBT-LIST.md` 8.2-7。
- D2 第二具身体/第二队（E2-3）→ 8.4-1。
- 服务器 m_nSequence/m_nModelIndex 还原、driven attack（E1）→ 8.4-2/3。
- 非 LOD0 层（E1）→ 8.2-8。
- 被绘制的 BLU stem、Heavy/Spy 头部外观（E2-6）→ 8.3-3。
- 自然 ViewModel apply、默认不变（E1/E2）→ 8.5-4。
- 每槽位（主/副/近战）武器、持枪动画（E2）→ 8.5-2/3。
- 两姿势之外的地图视角（E2-9）→ 8.2-9。
- **本次对账新增/上升的两条**：(a) 动态 brush 按 demo 时间位移无任何门禁（8.2-3）；(b) 本仓 `T62-REPORT.md:124` 与 `CHARACTER-MATRIX.md:438` 两处过期段落已修（D-13 于 task-6 关闭：加取代注、保留历史数字）。

## 5. 保留的原始出处与日期（供追溯）

- 被取代文件：`D:/_shared/postfreeze-inventory.md`，82 行，mtime 2026-11-07 00:40，标题 "Post-freeze gap inventory (E1/E2)"；其引用的外部文件：`D:/_shared/handoff/HANDOFF-2026-11-06-POST-FREEZE.md`（`:114-115`、`:118-120`、`:122-125`、`:128-132`、`:138-142`、`:144`、`:146`、`:155-160`、`:185`）、`D:/_shared/wire-animation-audit.md`（`:100-102`、`:323-352`）、`D:/_shared/t62-b2/candidate-scan.md`（`:91-104`、`:114-128`）、`D:/_shared/t62-p1-bonemismatch/REPORT.md`（`:244-339`）、`D:/_shared/t62-class-pixels3.log`。这些外部引用本次**未重读**，以旧文件记载转入，不新增断言。
- 本文件不复制旧文件的任何 PASS 结论；只保留"它当时说了什么、现在还算不算"。
