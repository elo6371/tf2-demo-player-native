# N17 / N18 / N19 / N20 覆盖矩阵（2026-11-15）

- 执行：`e17-e20-char`（N17/N18/N19/N20 责任子智能体）
- 工作树：`D:/TF2_Native_Worktrees/t105-e5`　分支：`candidate/e5`　HEAD：`a901ece`（初稿）→ `0d0747c3`（2026-11-15 23:00 复核）→ **`32aa54de`（§3.7 产品帧的采集时点）→ `e5fe2219`（2026-11-16 00:05 当前，n05 提交 N05 step 3 后）**。§3 的全部行号已在 `e5fe2219` 上逐条复核。
- 证据目录：**`D:/_shared/EXEC-2026-11-15/INST4/`**（`p1a-n17/` = N17 普查与诊断脚本；`p1b-n18/` = N18 的 37 项只读普查产物 + `draw/` 三条产品帧与 trace）；早期一次性诊断包装在 `D:/_shared/EXEC-2026-11-16/e17-e20-char/tools/`
- 状态词汇：**PASS / NOT VERIFIED / UNSUPPORTED**（`docs/T85-SUPPORT-SCOPE.md` 只认这三种），外加本批 N20 裁决指定的 **OPEN（未实现）**。PASS 只对具名门禁成立，不等于可发布。

**阅读约定（这是本文件的硬规则）**：每一格要么给出「文件:行号」或「探针命令 + 实测输出」，要么写「无」/「未证」。**全文不出现「应该有」。**
行号一律取自 `git show HEAD:<file>`：初稿时工作树的 `native/src/main.cpp` 正被 n12-camera 修改（13093 行 vs HEAD 12912 行），HEAD 行号稳定。**2026-11-15 23:00 复核：HEAD 已漂到 `0d0747c3`（14517 行），工作树与 HEAD 一致；`git diff --stat cf625b42..HEAD -- native/src/main.cpp native/include/ native/tools/model_pose_probe.cpp` 为空，即动画接线本身没被人动过，但本文件里所有 `main.cpp` 行号都已按 `0d0747c3` 重取（见 §2.6.1）。**

---

## 0. 资产前提（先纠正一个我自己判错过的事实）

我第一轮静态侦察时用 loose 文件 glob 判定「本机没有 `models/**/*.mdl`」，**那个结论是错的**，写在这里以免后来者复述我的错误。

| 项 | 实测 |
|---|---|
| TF 安装根 | `D:/SteamLibrary/steamapps/common/Team Fortress 2/tf` |
| 该目录的 VPK | 4 个：`tf2_misc_dir.vpk`、`tf2_textures_dir.vpk`、`tf2_sound_misc_dir.vpk`、`tf2_sound_vo_english_dir.vpk` |
| 缺失 | `pak01_dir.vpk`（`resource_reachability_probe` 实测 `archivesOpened=4 archivesExpected=5 missingNames=pak01_dir.vpk totalEntries=149970`） |
| 模型资产实际所在 | **`D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/tf2_misc_dir.vpk`** |
| 直读证据 | `vpk_query <tf> --dump models/player/scout.mdl` → `source=tf2_misc_dir.vpk bytes=611668`，IDST 头正常 |
| 材质直读证据 | `--dump materials/models/weapons/v_medigun/v_medigun.vmt` → `source=tf2_misc_dir.vpk bytes=1408`；`materials/models/player/medic/medic_hands_red.vmt` → `bytes=736` |
| 粒子直读证据 | `pcf_capability_probe <tf>` → `vpk_status=opened vpk_pcf_count=135 first_entry=particles/bigboom.pcf` |

**结论（照 lead m00300 A1 的要求写死）**：`pak01_dir.vpk` 的缺失只影响该档案内的条目（主要是地图 BSP 的预载副本与部分音频），**不影响 `tf2_misc_dir.vpk` 里的 `models/`、`materials/`、`particles/`**。这同时解释了 N07 材质复核里「7 图 `vtfMissing=0` 但 `pak01` 缺失」的悬案：那些材质根本不在 `pak01` 里。

语料 `E:/demo测试/` 只有 `.bsp` 与 `.dem`，没有 TF 资产。中文路径一律走 ASCII 孪生 `D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/demotest231312314444.dem`（sha256 `0c33e5ef868f00f21f55f8b87b7094ce3d58a0fcdb80768b920dd23f625c524c`，6283371 B），因为旧版探针对非 ASCII 路径会崩 `0xC0000409`。

---

## 1. 四列总表

| 卡 | 资产存在 | 解析器存在 | 绘制/驱动实现存在 | 可复现格 |
|---|---|---|---|---|
| **N17** 人物动作与服务器数据边界 | 有：九职业 `models/player/<class>.mdl` + `<class>_animations.mdl` 伴生（`model_pose_probe --player-animation-test` 九职业全通，rc=0） | 有：本地序列标签选择器 `native/src/main.cpp:315` `choosePlayerAnimationSequence`，谓词与匹配本体在 `native/include/animation_sequence_match.h`（探针与产品共用同一份） | **部分**：idle / moving / crouch / flinch 四种有选择逻辑；jump / fall / land / turn / taunt / attack / death 无 | 见 §2 |
| **N18** 第一人称与武器全槽位 | 有：`models/weapons/v_models/` **37 个**第一人称 `.mdl`（`v_hm_watch` 在子目录）+ `models/weapons/c_models/` **319 个**第三人称 `.mdl`（含 9 个 `c_<class>_arms.mdl`） | 有：`viewmodel_range_probe --all-lods` 37 个全部 `resolution=found-vpk` / `vtxOutOfBounds=0` / `meshBuilt=1`；材质解析 37/37 全 `status=ok`（147 槽零失败）；契约 `meshReady 37/37`、`contractOk 25/37`（12 个 false 全是**右手放置锚缺失**，`--hand left` 可救回 9 个） | **部分**：网格可建、材质可解析、**材质 GPU 上传已证**、**像素已证（2 职业）**；**动画层 refused**（`--viewmodel-apply-animation` changes nothing measurable，`native/src/main.cpp:3044`）；**世界 FOV 被 `--viewmodel-fov` 污染**（`native/src/native_renderer_frame.cpp:179`）；**团队涂装无输入**；**手臂骨合并未实现**；LOD：ViewModel 无 LOD 数据（101 descriptor 全 lod=0），玩家侧无选择器 | 见 §3，产品帧见 §3.7 |
| **N19** 网格/实例/纹理容量 | 有：三只 OOM 武器 `.mdl` 在 VPK 内可读（`c_flamethrower` 4452 B、`c_sapper_xmas` 2892 B、`c_shogun_warhorn` 2052 B） | 有：四层预算账目 + 优先级分类（见 §4.1 / §4.2） | **部分**：T85 分支实测 `0x8007000E` 601 行 → 0 行；**但那不是本 HEAD 的读数** | **无**（等 host 锁） |
| **N20** 三指定效果 | 有：`particles/flamethrower.pcf`、`medicgun_beam.pcf`、`medicgun_attrib.pcf`、`blood_impact.pcf`、`blood_trail.pcf`、`burningplayer.pcf`（pcf 总 135） | **只有事件记账**：`native/src/effects_timeline.cpp:41/57/70` 三个 `add*Event` + `:35` `countThrough` | **无**：全仓 grep `drawEffect|renderEffect|particleSystem|effectPass` 命中 **0**；产品唯一相关输出是 `native/src/main.cpp:10111` 的计数行 | **无**（OPEN） |

**N20 定性（照 lead m00300 A2 裁决）**：`effects_timeline` 记账 + trace 计数但无绘制 = **未实现**，记 **OPEN**，不是缺陷、不是 NOT VERIFIED。**资产齐全只说明能修，不说明修了。**

---

## 2. N17 人物动作与服务器数据边界

### 2.1 数据层：九职业 × 11 动作（模型序列表实测）

命令（对每个 `<class>` 各跑一次，均 rc=0）：

```
native/build-nmake/model_pose_probe.exe --player-animation-test \
  --tf-root "D:/SteamLibrary/steamapps/common/Team Fortress 2/tf" \
  --model models/player/<class>.mdl --dump-sequences
```

下表计数是 `grep -c` 的 **label 命中行数**，不是可用动作数（一个动作可能有多条序列）。`0` 表示该家族在这份表里一条都没有。

| 动作 | 序列族关键词 | scout | soldier | pyro | demo | heavy | engineer | medic | sniper | spy |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| idle | `idle`/`stand` | 24 | 19 | 18 | 19 | 18 | 27 | 17 | 20 | 20 |
| run | `run` | 11 | 10 | 10 | 10 | 10 | 14 | 9 | 11 | 11 |
| walk | `walk`（作移动主选） | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| walk（**不区分大小写的原始命中**，2026-11-15 23:00 补测） | 子串 `walk` | 19 | 33 | 23 | 20 | 20 | 29 | 13 | 18 | 16 |
| ↳ 其中有几类会被误读成「站着走」 | 排除 `airwalk`/`crouch_walk` 后剩下的裸 `walk` | 0 | 0 | **2**（全是 `*_aimmatrix_walk` 1 帧） | 0 | 0 | **2**（`MELEE_aimmatrix_walk`/`MELEE_ALLCLASS_aimmatrix_walk` 各 1 帧） | 0 | **1**（`ITEM2_aimmatrix_deployed_walk` 1 帧） | 0 |
| airwalk | `airwalk` | 13 | 27 | 16 | 15 | 11 | 18 | 7 | 9 | 9 |
| turn | `turn` | **0** | **0** | **0** | **0** | **0** | **0** | **0** | **0** | **0** |
| crouch（首 token = crouch） | `crouch_*` | 有（crouch_PRIMARY 46 帧 / crouch_walk_PRIMARY 27 帧） | 有（51/27） | 有（61/27） | 有（34/27） | 有（61/31） | 有（47/27） | 有（31/27） | 有（41/27） | 有（crouch_SECONDARY 41 / crouch_MELEE 41） |
| jump_start | `jump_?start`（不区分大小写） | **7** | 6 | 6 | 6 | 6 | 10 | 5 | 7 | 7 |
| jump_float | `jump_?float`（不区分大小写） | **8** | 7 | 6 | 6 | 6 | 10 | 6 | 7 | 7 |
| fall | `fall` | **0** | **0** | **0** | **0** | **0** | **0** | **0** | **0** | **0** |
| land | `land` | 8 | 9 | 12 | 12 | 7 | 20 | 5 | 14 | 14 |
| flinch | `flinch` | 1 | 1 | 2 | 1 | 4 | 3 | 2 | 2 | 2 |
| death | `death` | 3 | 3 | 2 | 3 | 3 | 3 | 3 | 3 | 3 |
| ragdoll | `ragdoll` | 2 | 2 | 2 | 2 | 2 | 2 | 2 | 2 | 2 |
| taunt | `taunt` | 59 | 78 | 68 | 70 | 66 | 71 | 64 | 23 | 60 |
| attack | `attack` | 16 | 23 | 26 | 10 | 33 | 14 | 21 | 12 | 4 |

**从这张表能直接读出、且必须写进结论的三件事：**

1. **`turn` 与 `fall` 在九职业里都是 0**。TF2 的角色表没有转身序列，也没有名为 fall 的下落循环；N17 的 turn 行只能用「yaw 变化 + run/idle 切换」间接表达，fall 行只能用 `jump_float` 替代。**任何「转身动画」「下落动画」的说法都是编的。**
2. **`jump` 九职业全有家族（这一格在初稿里判错了，2026-11-15 23:00 重测推翻）**。初稿的 `jump_start`/`jump_float` 计数用 `grep -c` 默认大小写敏感，而家族拼写有**三套**：`jump_start_PRIMARY`（soldier/medic）、`Jump_Start_primary`（scout，大驼峰）、`a_jumpstart_PRIMARY`（pyro/demo/engineer/sniper）、`jumpstart_PRIMARY`（heavy）、`a_jumpstart_SECONDARY`（spy）；float 侧同理 `jump_float_PRIMARY` / `Jump_Float_primary` / `a_jumpfloat_PRIMARY` / `jumpfloat_PRIMARY`。改成不区分大小写并放宽到下划线可选后，九职业 `jump_start` 各 5–10 条、`jump_float` 各 6–10 条。**所以「jump 只有两个职业有」是错的，正确的是「数据齐、产品侧没有 jump 逻辑」**（仍记 OPEN，不是 NOT VERIFIED）。scout 的 `Jump_Float_primary` 是 2 帧且四姿态 checksum 全同（§2.6.3），**帧数不能当「会动」的证据**。
3. **`land` 九职业全部有数据（5–20 条），但产品侧没有任何 land 逻辑**（HEAD `main.cpp` 里 `\bland\b` 只命中注释）。数据有、产品无 —— 这是「数据齐 ≠ 已实现」的典型格。

### 2.2 产品侧：到底读了哪些服务器字段（四层拆开）

| 层 | idle / moving | crouch | flinch | death | jump/fall/land/turn/taunt/attack |
|---|---|---|---|---|---|
| **① 输入事件** | `m_vecVelocity` 推导的速度经 `advanceAnimationClock`（`native/include/animation_clock.h`；状态只有 `Idle=0` / `Moving=1`，:43） | `m_fFlags` bit1（FL_DUCKING）**或** `m_bDucked`/`m_bDucking`/`m_flDucktime>0`（HEAD `main.cpp:10936-10940`）。注释记了实测反例：mid-duck 身体 `m_fFlags=131329`（bit1 已清）但 `m_bDucking=1`、`m_flDucktime=940`，只用 flags 会漏 | `m_iHealth` 在相邻两个 scene tick 上下降且身体存活（HEAD `main.cpp:10907-10931`）。**刻意不用 CTEPlayerAnimEvent**：census 发现该事件没有可映射值（109 个 `player_death` vs 唯一候选值 545 次出现，且事件 player 字段从不等于 game event 的 userid） | `m_lifeState` 仅用于 spectate/可绘制判定（HEAD `main.cpp:474/10232/10641`），**没有接成死亡动画** | **无输入** |
| **② 选序** | `choosePlayerAnimationSequence`（`main.cpp:315`）→ `chooseSequenceByNeedles` | `chooseCrouchLocomotionSequence` → 失败退 `chooseCrouchPoseSequence`（`main.cpp:10859/10863`） | `chooseFlinchSequence`（`main.cpp:10871`，实现在 `animation_sequence_match.h:439`） | 无 | 无 |
| **③ 骨骼运动** | 有：`skeleton_skin_probe`（scout）`bones=78 animBones=76 mappedBones=76 conventionError=3.05176e-05 conventionBone=37 identityWorst=2.47955e-05 skinLands=3.8147e-06 skinReversed=24.942 sampledSequences=331 motionDelta=185.935 motionSequence=layer_taunt_chicken` | 未单独测（同一采样路径） | 同左 | 未测 | 无 |
| **④ 像素** | **已证**：`states-crouch` vs `legacy-crouch` 677 px（身体盒内 1.48%）；`default`==`legacy` 0 px（§2.6.5） | **已证**（同上，crouch 那一对比就是蹲层上屏） | **trace 已证、像素不可证**：该身体在第一人称视野外，`states-flinch` 与 `legacy` 同帧 0 px 是预期的 | **未证**（产品无死亡动画） | **未证**（产品无这些状态） |

### 2.3 逐动作结论

| 动作 | 数据 | 选序 | 骨骼 | 像素 | 卡片结论 |
|---|---|---|---|---|---|
| idle | 有 | 有 | 有 | **有**（§2.6.2 九职业 18 条 stand/run clip 四姿态全 distinct=4） | **PASS（数据+选序+解码；画面级见 §2.6.5 行 1）** |
| run | 有 | 有 | 有 | **有**（同上） | **PASS（同上）** |
| crouch | 有 | 有 | 有（`boundDiag composed=1`） | **有**（`states-crouch` vs `legacy-crouch` 677 px，bbox 落在身体上） | **PASS（状态层）**，但 **medic 的 crouch locomotion 是冻结 clip（缺陷候选，§2.6.3）** |
| flinch | 有 | 有 | 有 | **有**（状态行 + 计数器 `flinch=7 flinchStarts=7`；同帧像素不变是因为该身体不在相机视野内，见 §2.6.5 行 4） | **PASS（trace 级 + 选序）**；像素级**该身体不可见，无法证** |
| land | **有**（5–20 条） | **无** | 无 | 无 | **OPEN（未实现）** |
| jump | **有（九职业全有，§2.1 第 2 条已更正）** | **无** | 无 | 无 | **OPEN（未实现）**；`jump_float` 数据本身 Scout/medic 有单姿态块（§2.6.3） |
| fall | **无**（0 条） | 无 | 无 | 无 | **UNSUPPORTED**（数据与逻辑都没有） |
| turn | **无**（0 条） | 无 | 无 | 无 | **UNSUPPORTED** |
| taunt | 有（23–78 条） | **无**（HEAD `main.cpp` 里 `taunt` 命中 0 次） | 无 | 无 | **OPEN（未实现）** |
| attack | 有（4–33 条） | **无**（身体无 attack 状态；只有 ViewModel 走服务器序列号） | 无 | 无 | **OPEN（未实现）** |
| death-respawn | 有（death 2–3 / ragdoll 2） | 无 | 无 | 无 | **无法验证**：本语料 POV demo 里 `spectate_probe --demo` 回 `source available=0 checkpoint=0 skipped=5 frames=0 deaths=0 handovers=0 director=0` → **死亡样本为 0**。数据在，观测窗口不在 |

### 2.4 必须随结论一起报的三处偏差（不是缺陷，但会让「九职业一致」的说法不成立）

1. **heavy 与 spy 的 idle/moving 选择落在 SECONDARY 槽位**，其余七个职业落在 PRIMARY。实测（`--player-animation-test` 一行的 `idleSeq/idleLabel/movingSeq/movingLabel`）：
   - scout `idleSeq=9 idleLabel=AttackStand_PRIMARY` movingSeq=212 `run_PRIMARY`
   - sniper 11 / 213、soldier 10 / 201、demo 9 / 147、medic 9 / 80、pyro 19 / 141、engineer 9 / 265 均 PRIMARY
   - **heavy `idleSeq=16 idleLabel=AttackStand_SECONDARY` movingSeq=115**、**spy `idleSeq=10 idleLabel=AttackStand_SECONDARY` movingSeq=145 `run_SECONDARY`**
2. **`animation_sequence_match.h` 的注释与实测不符两处（注释需要修正，代码行为按实测走）**：
   - 注释称「only pyro/heavy/medic carry the crouch locomotion family」。实测九职业**全部**有首 token 为 `crouch` 的多帧循环（见 §2.1 crouch 行）。其中 spy 没有 `crouch_PRIMARY`，用 `crouch_SECONDARY`（与它整套走 SECONDARY 一致）。
   - 注释称 flinch「only scout / soldier / pyro / demo carry it」。实测 **8 个职业有 `a_flinch01`，heavy 用的是 `PRIMARY_flinch` / `SECONDARY_flinch` / `MELEE_flinch` / `MELEE_ALLCLASS_flinch`**，engineer 有 `a_flinch01/02/04`。`sequenceLabelIsFlinch` 用子串匹配，所以九职业**都能**选到 flinch。
   - 这两条会实际影响判据：`main.cpp:12454-12462`（HEAD `0d0747c3`）给「没有 crouch locomotion 的职业」记 `no-crouch-locomotion` 并用 crouch pose 顶上。**本轮已用产品运行坐实：`no-crouch-locomotion` 在我 7 条捕获共 168 条 state 行里出现 0 次**（`reason=` 分布只有 `flags-duck=96` / `duck-fields=6` / `health-drop=66`），与该注释「六职业没有 crouch locomotion 所以会走 fallback」的预言相反 —— 因为九职业其实都有。**这条 fallback 在当前资产上不可达**，是「注释驱动的死分支」：不是缺陷（它没有错，只是没有对象），但必须记账，且不能拿它当「六职业 crouch 已降级」的证据。
3. **`walk` 家族不是「九职业全 0」**：`grep -c walk` 默认区分大小写时是 0，不区分大小写后是每类 13–33 条，但**排除 `airwalk` 与 `crouch_walk` 之后只剩 0–2 条，而且全是 1 帧的 `*_aimmatrix_walk` 姿态、不是走路循环**（pyro `PRIMARY_aimmatrix_walk`、engineer `MELEE_aimmatrix_walk`/`MELEE_ALLCLASS_aimmatrix_walk`、sniper `ITEM2_aimmatrix_deployed_walk`）。所以「站着走的 walk 循环在资产里不存在、移动只能靠 `run_`」这个**结论不变**，但**理由必须改**：不是「walk 一条都没有」，是「带 walk 字样的全是 airwalk / crouch_walk / 单帧 aimmatrix 姿态」。`kMovingNeedles = {"run","walk","move"}` 里的 `walk` 在这个资产上选不到任何循环。

### 2.5 N17 的两项历史核查（都已在本分支）

- `duplicate entity ID` 与 `walk/airwalk` 相关提交 **c85fdfc**（T85 T51: 8.3-2 player-model assert + precedence fixture + walk/airwalk/reload gap evidence）、**29e27ca**（Merge branch 'task/t61-airwalk' into p0-entity-protocol）、**a7b3196**（T61: the animation gate proves its own restoration）。
- `git merge-base --is-ancestor <c> HEAD` 三者**全部 YES**；`git branch --contains 29e27ca` 含 `* candidate/e5`。**结论：不需要从其他分支搬补丁。**

### 2.6 2026-11-15 23:00 的产品级复测（本节取代 §2.2/§2.3 里所有「未证」格）

**先说结论**：N17 的「站 / 跑 / 蹲」三态本轮拿到了**产品级（渲染帧 + trace）**读数，「空中」在数据层与 trace 层都有证据但**产品代码没有这一态**，仍记 **NOT VERIFIED / 未实现**。所有读数只存在于**同一次捕获内部**，跨日期不可比（§2.6.5）。

#### 2.6.1 输入身份（跑门禁前后各取一次，判据「没被重建」）

| 项 | 值 |
|---|---|
| HEAD | `0d0747c3`（`docs(INST1): the candidate build and the gate-efficiency survey...`）；`native/src/main.cpp` = 14517 行，工作树与 HEAD 一致 |
| 动画代码是否被动过 | **没有**：`git diff --stat cf625b42..HEAD -- native/src/main.cpp native/include/ native/tools/model_pose_probe.cpp` 输出为空 |
| 产品 exe | `native/build-nmake/tf2_demo_native.exe`；sha256 `7a8da164de67553281065ef76638c135a49baf537cb5027865bca4f39f3992ff`，3017216 B，mtime 2026-11-15 21:34:58 |
| 探针 exe | `native/build-nmake/model_pose_probe.exe`；sha256 `e490d8b9389a8cd2b072380be3f92cc943617ec2a329c537b299bbfcd7f32e73`，966144 B，mtime 2026-11-15 21:50:00（本轮用 `tools/lockrun.sh e17-e20-char -- bash build-target.sh model_pose_probe` 重建，因为头文件依赖不被跟踪） |
| 录制（states） | `bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem`，sha256 `308cc1453672b26dfdcc106bcb62eac4a7eea97899adc8830f51711be834607f`（与门禁常量一致） |
| 所有产品运行的共同参数 | `--tf-root <tf> --settings-file <iso> --ui-settings-file <iso> --audio-device 6 --demo <dem> --capture-tick <T> --capture-frame <bmp> --metrics-file <csv> --player-animation [--animation-drive legacy\|states]`；**不含 `--playback-speed`**（这是 states 门禁的本意） |
| 帧尺寸 | 1264x681（BMP 2582406 B），7 条运行全部一致 |

#### 2.6.2 九职业 × 六种 stance 的四姿态 checksum 实测（探针层，`--player-animation-test --sequence N`）

判据是**四个 cycle（0 / .25 / .5 / .75）的 pose checksum 互不相同**（distinct=4 才算「这条 clip 会动」）。`poseChanges` / 帧游标前进都**不能**当证据（§2.6.3）。

| 职业 | stand（产品 idle） | run（产品 moving） | crouch locomotion | crouch_walk | jump float | flinch |
|---|---|---|---|---|---|---|
| scout | 9 AttackStand_PRIMARY **d=4** | 212 run_PRIMARY d=4 | 184 crouch_PRIMARY **d=4** | 213 d=4 | 229 Jump_Float_primary **d=1**（2 帧） | 69 a_flinch01 d=4 |
| soldier | 10 d=4 | 201 d=4 | 156 crouch_PRIMARY d=4 | 202 d=4 | 164 jump_float_PRIMARY **d=3**（0 与 .75 同） | 75 d=4 |
| pyro | 19 d=4 | 141 d=4 | 95 d=4 | 145 d=4 | 102 a_jumpfloat_PRIMARY d=4 | 13 d=4 |
| demo | 9 d=4 | 147 d=4 | 100 d=4 | 148 d=4 | 107 a_jumpfloat_PRIMARY d=4 | 28 d=4 |
| heavy | 16 AttackStand_SECONDARY d=4 | 115 d=4 | 71 d=4 | 121 d=4 | 78 jumpfloat_PRIMARY d=4 | 151 PRIMARY_flinch d=4 |
| engineer | 9 d=4 | 265 d=4 | 186 d=4 | 266 d=4 | 197 a_jumpfloat_PRIMARY d=4 | 297 d=4 |
| medic | 9 d=4 | 80 d=4 | **65 crouch_PRIMARY d=1（冻结）** | 81 **d=4** | 91 jump_float_PRIMARY **d=2**（2 帧） | 17 d=4 |
| sniper | 11 d=4 | 213 d=4 | 150 d=4 | 214 d=4 | 159 a_jumpfloat_PRIMARY d=4 | 23 d=4 |
| spy | 10 AttackStand_SECONDARY d=4 | 145 run_SECONDARY d=4 | 90 crouch_SECONDARY d=4 | 146 d=4 | 98 a_jumpfloat_SECONDARY d=4（164 帧） | 21 d=4 |

九职业 `finite=1` 全部成立、`boundDiag` 全部通过（`composed=1`）、`usable=1`。**产品实际会播的 18 条 stand/run clip 全部 distinct=4**（逐条复算，无一例外），所以「站」「跑」两态在解码 / 骨链 / 蒙皮 / 时间推进四层对九职业**全部成立**。

#### 2.6.3 三条会改变结论的负面读数（每条都做过对照）

1. **medic 蹲姿冻结，且冻的是产品实际会播的那一条。** `chooseCrouchLocomotionSequence` 取「第一个 frames>1 的 `crouch*` clip」，对 medic 命中 seq 65 `crouch_PRIMARY`（31 帧）；它的四个 cycle checksum **全是 `7af1c3641faccfd1`**，而 frame 游标确实在走（0→7→15→23，`localFrame` 同步，`sectionFrames=0`）。同一族的 `crouch_SECONDARY`(66) 四姿态**互不相同**（`25d5b9ee… / 4d49f534… / 7ce55bf1… / ea3db11b…`）→ **族里有会动的替代，pick 落在静态那条**。产品 trace 坐实这条就是它：`anim: staterefs model=models/player/medic.mdl crouchMoveSeq=65 crouchMoveLabel=crouch_PRIMARY`。**这是本轮唯一一条「画面级可复现的产品缺陷候选」。**
2. **scout 的 `stand_PRIMARY`(175) 与 `Jump_Float_primary`(229) 都是单姿态数据块（d=1）**，但产品 idle 选的是 `AttackStand_PRIMARY`(9)（d=4），所以 stand 的静态项**没有上屏**；jump float 则因为产品根本没有空中态（§2.6.4）也没有上屏。→ **「帧数多」「帧游标在走」都不能当「会动」的证据**：scout 的 jump float 只有 2 帧、soldier 的 jump float 有 24 帧但 cycle 0 与 .75 姿态相同（d=3）。
3. **对照证明解码器本身没坏**：medic `crouch_walk_PRIMARY`(81) 四姿态 `0967a7cb52072dca / 59b9f20bbc392cbf / 4cff7e2dc02ed243 / 11ea65f46d25bea5` 全不同。所以 1、2 两条是**资产数据块本身只有一个姿态**，不是解码路径坏。

#### 2.6.4 「空中」态：数据齐、wire 有信号、产品不读（这是本轮最硬的一条）

- 产品侧：`grep -n "m_hGroundEntity\|m_bOnGround" native/src/main.cpp` → **0 命中**；`PlayerDriveSignals`（HEAD `main.cpp:11713-11726`）只有 `hasFlags/flags`、`hasDucked/ducked`、`hasDucking/ducking`、`hasDucktime/ducktime`、`hasHealth/health`、`hasLifeState/lifeState` 六个字段；`animation_sequence_match.h` 里 grep `jump\|air\|ground\|land` → 只命中注释。**没有 air 信号、没有 air picker、没有 air 状态名。**
- 但 **wire 的 `m_fFlags` 里确实带着「不在 ground」的位**：我 7 条捕获里 168 条 `anim: state` 行的 `flags=` 分布是 **131328×8 / 131329×50 / 131330×103 / 131331×2**；`131328 & 1 = 0`，即那 8 条采样的 `FL_ONGROUND` 是清的，例如 `tick=26155 ent=2 state=flinch speed=290.08`、`tick=26155 ent=10 state=crouchwalk speed=140.52`。→ **产品只是从没读过这一位**，补实现的门在那里。
- 数据侧：九职业 jump 家族齐全（§2.1 第 2 条），但 `fall` 与 `turn` 在九职业里**真的是 0 条**，所以「下落动画」「转身动画」**任何声称都是编的**，只能用 `jump_float` 与「yaw 变化 + run/idle 切换」间接表达。
- 陷阱（沿用 T61 的记录）：`ReloadAirwalk_*` / `a_SECONDARY_reload_airwalk` / `airwalk_*` 里带 `air`，但属 reload 家族，`sequenceLabelIsOneShot` 按子串 `reload` 排除；`heavy` 的 `airwalk_PRIMARY`(8) 同族。**不能拿它们当空中动画**，`evidence/t61-animation/T61-ANIMATION-REPORT.md:328` 记过这条回归。

#### 2.6.5 产品级（渲染帧）读数：7 条捕获、BMP 逐像素比对

`animation-drive-states-check.sh` 的 9 条运行我只跑完 7 条（第 8/9 条被中止，见 §2.6.6「锁」），产物在 `D:/_shared/EXEC-2026-11-15/INST4/p1a-n17/t84-states/`，7 条**全部 `rc=0 bytes=2582406`**。

| 比较 | 像素差 | 判读 |
|---|---|---|
| `default` vs `legacy`（同 tick 16780） | **0**（bbox=None, maxDelta=0） | **A1 成立**：缺省路径与 `--animation-drive legacy` 逐字节相同 |
| `states-flinch` vs `states-flinch-repeat`（同 tick 16780） | **0** | **B2 成立**：同帧重放确定 |
| `states-crouch`(14765) vs `legacy-crouch`(14765) | **677 px**（0.08% 全图；bbox `[530,256,1032,347]`，maxDelta=169；在身体盒内 677/45682 = **1.48%**） | **B3 成立**：states 层真的改变了那一帧，且改变落在屏幕上那个玩家身体上（crop 复核过：画面左下的蓝兵姿态不同，不是 UI/噪点） |
| `states-flinch` vs `legacy`（同 tick 16780） | **0** | **不是失败**：受击的那个身体在第一人称相机视野外，不贡献像素。门禁 B1 的注释就是这么写的（`animation-drive-states-check.sh:243-250`，freeze6 2026-11-06 11:29 已实测 seq 9→17 而帧不变） |
| `states-crouch`(14765) vs `states-crouch-b`(14800) | **3 px**（bbox `[723,299,739,300]`，maxDelta=19） | **弱得可疑**：两个不同 tick 的「同名状态」姿态几乎完全静止。门禁自己的 B3 注释（`:288-298`）解释了原因：crouch 用的是 aim-matrix 单帧姿态，`advanced=0`，**两个 tick 本来就该一样**；历史上的那次 `gate.txt:24` 更是**两个 tick 字节完全相同**（`f814baec…` 两次） |

**跨日期不可比（重要）**：同名的、已提交的 `evidence/t84-animdrive/animation-drive-states/default.bmp` 与我这份 `default.bmp` 差 **594921 px（69.11%）**，bbox 全屏 —— crop 看过去是**完全不同的视角与场景内容**（提交版那个位置是木箱与斜坡，没有蓝兵）。CSV 显示我这次 run 内存 4.5 GB / fps 与提交版（2.1 GB）明显不同，exe 也在 11-14/11-15 被重建多次。→ **绝对 md5 只能在同一批 run 内部比；「现在这个 md5 是不是当年那个」这种断言不成立**，这也顺带解释了为什么不该把 md5 常量写死进门禁断言（现有门禁确实只在同一次运行内部比，是对的）。

#### 2.6.6 锁、残留进程与这次运行的边界（必须随读数一起报）

- 我的跑法**包了锁**：`retry-states-gate.sh` → `"/c/Program Files/Git/bin/bash.exe" tools/lockrun.sh e17-e20-char -- "/c/Program Files/Git/bin/bash.exe" animation-drive-states-check.sh`，开局日志 `STALE-LOCK=RECLAIMED owner=inst3-n13 pid=4216 age=4s reason=owner-dead` + `VERIFY-LOCK=ACQUIRED owner=e17-e20-char pid=2017`。
- **但 22:30 起 `.scratch/verify-all.lock` 整个消失，而我的产品还在跑**，lead 两次抓到这个窗口（PID 9868 起于 22:30:32、PID 55400 起于 22:35:04，命令行都是 states-cover 的 capture）。**根因已实测**：lockrun 跑在 **Git Bash** 下，锁里写的是 **MSYS pid 2017**；而 `verify-lock.sh` 的 `vlock_pid_alive()`（仓库根 `verify-lock.sh:33-38`）只做 `kill -0 "$p"`。MSYS pid 与 niubash bash 的 pid 空间**不互通**：同一 msys pid 1728 在默认 niubash bash 里 `kill -0` 判 **DEAD**、在 Git Bash 里判 **ALIVE**，Windows 层 `tasklist` 显示 `sleep.exe 43448` 活着。→ **别人在 niubash bash 里检查我的锁，会得到「owner 已死」，于是 `STALE-LOCK=RECLAIMED` 把活锁 rm 掉并在我的产品还在跑时拿锁。** 这是 `verify-lock.sh` 的缺陷（应比对 winpid 而不是 msys pid），**不是我没包 lockrun**。
- **处置**：收到 lead 通知后立即 `job_kill`，复核 `NO-PRODUCT-RUNNING` / `hostlock.sh status` = `product_processes=0` / `.scratch/verify-all.lock` 不存在。**第 8、9 条 run（states-cover 20000、early 12000）没有跑完，本轮不采信任何来自它们的读数**；7 条完成的 run 的产物时间戳在 22:05:43–22:35:01 之间。
- 另有一套互不知情的锁：`/d/TF2_Native_Worktrees/_shared/hostlock.sh`（用 `$ROOT/host.lock` + `queue` + `reserved`），与 `tools/lockrun.sh`（用 `.scratch/verify-all.lock`）**互不感知**。本轮期间 `hostlock.sh status` 反复报 `ANOMALY product_running_without_lock=1`，PID 不断更换，**是别人在无锁跑产品，不是我的**。

#### 2.6.7 逐状态 trace 级读数（我的 7 条捕获，自己重算的非门禁读数）

| 运行 | animsum 行 | 其中 `advanced>0` | `maxAdvanced` | scene: anim 行 | `poseChanges` 峰值 | `boneMismatch` 峰值 | `anim: state` 行 |
|---|---:|---:|---:|---:|---:|---:|---:|
| default | 5352 | 26 | 11 | 5352 | 1250 | 25000 | **0** |
| legacy | 5471 | 26 | 11 | 5471 | 1250 | 25548 | **0** |
| legacy-crouch | 4861 | 23 | 11 | 4861 | 1114 | 22273 | **0** |
| states-flinch | 4541 | 26 | 11 | 4541 | 1182 | 20953 | **62** |
| states-crouch | 5625 | 23 | 11 | 5625 | 1054 | 25664 | **53** |
| states-crouch-b | 4827 | 23 | 11 | 4827 | 1054 | 22152 | **53** |

- **`animdsum` 只在 states 三条里出现**（default / legacy / legacy-crouch 各 0 行）→ A2「legacy 仍走 pre-T84 路径」成立。
- **三个计数器峰值**（我重算，与门禁 `peak_of` 同口径）：states-flinch / states-crouch / states-crouch-b 全部 `crouch=2 crouchwalk=2 flinch=7 flinchStarts=7 fallback=10`，**五条全 ≥1** → C2 成立。注意 `fallback=10` 是**回落到移动层**的帧数（`main.cpp:12477` `if (!layered) ++playerAnimFallback;`），不是「六职业 crouch 降级」。
- **`state=` 分布**（states-crouch）：`crouch=17 flinch=21 crouchwalk=15`；**`reason=` 只有三种**：`flags-duck=30 / health-drop=21 / duck-fields=2`，**`no-crouch-locomotion` 出现 0 次**（见 §2.4 第 2 条）。
- **`label=` 只有三种**：`a_flinch01 / crouch_PRIMARY / PRIMARY_aimmatrix_crouch_idle` —— **没有任何 `layer_*` 手臂序列被当成动作层选中**（N17 卡点名的「arm-layer 误选」在这批捕获里**复现不出来**，按「只在复现后修」的规则不动代码）。逐条转录也证实 motion 层 label 与 staterefs 的 pick 一一对应：`ent=9 seq=156 label=crouch_PRIMARY`（soldier）、`ent=13 seq=65 label=crouch_PRIMARY`（medic，即 §2.6.3 那条冻结 clip）。
- **`boneMismatch` 每一行都 >0（峰值 2 万+）**，但它是**累计计数器**（`main.cpp:12583-12586` 的 `boneCountMismatches` 只在 skinBones≠validBones 时 ++），跨进程随帧数线性增长，**不是「每帧骨骼错配」**。class-pixels-check.sh 的头注释已记过同一现象（`boneMismatch = 4.860469 * frames - 89.862`，R²=0.999731），所以**门禁不拿它做逐帧断言是对的**。
- **`advanced>0` 的帧只占 0.4–0.6%（26/5352 等）**，`maxAdvanced=11`（每帧最多 11 个玩家推进）——这说明这批录制的绝大多数帧里玩家是 idle（`idleRate=0` 时 cycle 冻结是设计），**不是「动画没跑」**。C 段连续性由探针的 60 步测量负责（§2.6.2 引用的 `animcont` 行），不靠这条。

#### 2.6.8 N17 卡片判据逐条对账（这是本节要回答的那张卡）

| N17 要求 | 本轮证据 | 判级 |
|---|---|---|
| 先读当前实现再评估外部 fixes | §2.2 四层表 + §2.6.1「动画代码未被动过」的 git 证明 | **DONE** |
| 一个职业做站/跑/蹲/空中 | 站/跑：九职业全部（超出要求）；蹲：九职业全部选中，**medic 那条冻结**（§2.6.3）；空中：产品无此态（§2.6.4） | 站 **PASS（数据+选序）** / 蹲 **PASS（数据）但 medic 画面冻结（缺陷候选）** / 空中 **UNSUPPORTED（产品未实现）** |
| 同序列时间推进 | 探针 B 段四 cycle 四姿态（§2.6.2，九职业 × 6 stance × 4 姿态全 `finite=1`）；C 段 60 步连续性 `maxStepDelta=12.663803`（soldier，探针固定选 seq 228）；D 段一秒 delta 期望值从 CONFIG CONTRACT 推导 | **PASS（探针层）** |
| GPU 姿态正负控 | 正控：`states-crouch` ≠ `legacy-crouch`（677 px，bbox 落在身体上）；负控：`default` == `legacy`（0 px）、`states-flinch` 重复跑 == 自己（0 px）、`states-flinch` vs `legacy` 同帧为 0 px **是预期的**（身体不在视野内） | **PASS（7 条 run 内部自比）** |
| 重复 ID | `--world-entity-id`（`main.cpp:3640-3649` 解析，view 9）的 `entity: id=` 台账**在我这 7 条 run 里 0 行** —— 因为门禁没带 `--world-entity-id`。**未证，不是通过** | **NOT VERIFIED** |
| arm-layer 误选 | `label=` 只有 `a_flinch01 / crouch_PRIMARY / PRIMARY_aimmatrix_crouch_idle` 三种，**无 `layer_*`** → 这批捕获里**复现不出来** | **未复现（按规则不修）** |
| 序列选择/解码/骨链/上传/蒙皮逐层核 | 选序 §2.6.2（产品 trace 的 `staterefs` 坐实 pick 值）；解码 四姿态 checksum；骨链 `boundDiag composed=1` 九职业全通；上传 `scene: anim skinned=` 行 + `poseChanges` 峰值；蒙皮 `skeleton_skin_probe`（scout：`bones=78 animBones=76 mappedBones=76 conventionError=3.05176e-05 identityWorst=2.47955e-05 skinReversed=24.942`） | **PASS（逐层都有具名读数）** |
| C++17 语法通过 ≠ 产品可用 | 本轮所有结论都来自**跑起来的产品**（7 条 rc=0）或**探针实跑**，没有一条只靠编译 | **满足** |

#### 2.6.9 本节明确不声称的事

1. **不声称 medic 蹲姿缺陷已在画面上定位到肢体**：我只证明「四个 cycle 的 pose checksum 相同」，没有做肢体级像素归属（那属于 class-pixels-check.sh）。
2. **不声称空中态「差一点就能上」**：产品缺信号、缺 picker、缺状态名三样，wire 有 `FL_ONGROUND` 只是必要条件。
3. **不声称 T84 门禁在本 HEAD 是 PASS**：我只跑了 9 条里的 7 条，且**没有运行门禁自己的 `check_artifacts`**（它需要全部 9 条产物）。我重算的是同一批产物上的 C2 计数与 BMP 比对，**门禁自身的 PASS/FAIL 未出**。
4. **不声称 cross-date 的 md5 相等**（§2.6.5 已证不成立）。
5. **不声称 `no-crouch-locomotion` 分支是 bug**：它是不可达，不是错。

---
## 3. N18 第一人称与武器全槽位

### 3.1 第一人称 ViewModel 全集 = 37 个 `.mdl`（不是 12 个）

`vpk_query <tf> models/weapons/v_models | grep '\.mdl$'` → 37 条。命令模板：

```
native/build-nmake/viewmodel_range_probe.exe --tf-root "D:/.../tf" \
  --model models/weapons/v_models/<name>.mdl --all-lods
```

**坑（会白跑）**：短名 `v_<x>.mdl` 一律 `range: model did not resolve`，必须给全路径 `models/weapons/v_models/<x>.mdl`；`v_hm_watch` 的唯一正确路径是 `models/weapons/v_models/v_hm_watch/v_hm_watch.mdl`（在子目录里）。

**37 个全部**：`resolution=found-vpk`、`vtxOutOfBounds=0`、`targetLod=0`、`materialTable=available`、`meshBuilt=1`，且**每个 descriptor 都是 lod=0 —— 资产里根本没有 LOD1**（D34 的「LOD1+ 未实现」在这里是资产边界，不是代码遗漏）。

| 模型 | bones | vvd 顶点 | target 索引 | 段数 | 段内容（材质名 / 顶点·索引） |
|---|---:|---:|---:|---:|---|
| `v_baseball` | 1 | 1764 | 2532 | 1 | `models/weapons/v_baseball/baseball_sheet`（**只有球，无手臂**） |
| `v_bat_scout` | 27 | 4538 | 11379 | 2 | scout_hands(1348/6723) + v_bat(3190/4656) |
| `v_bonesaw_medic` | 21 | 5818 | 9189 | 3 | medic_red(128/414) + medic_hands_red(821/3906) + v_bonesaw(4869/4869) |
| `v_bottle_demoman` | 25 | 18236 | 26589 | 4（**bodyParts=2**） | bp0: demoman_red(160/468)+demoman_hands(1372/6297)；bp1: 两段同名 v_bottle(6480/9600 与 10224/10224) |
| `v_builder_engineer` | 51 | 5128 | 15174 | 4 | engineer_handR_red(1396/4392)+engineer_handL(1342/6576)+engineer_red(230/768)+v_builder(2160/3438)（**左右手分开建模**） |
| `v_fireaxe_pyro` | 41 | 7710 | 17160 | 3 | pyro_hands_red(1742/7572)+pyro_red(212/840)+v_fireaxe(5756/8748) |
| `v_fist_heavy` | 40 | 3424 | 15732 | 1 | hvyweapon_hands（**只有手，无身体段**） |
| `v_grenadelauncher_demo` | 55 | 5833 | 20844 | 3 | demoman_hands(1827/9510)+demoman_red(152/576)+v_grenadelauncher(3854/10758) |
| `v_hm_watch` | 26 | 4436 | 14082 | 2 | c_hitman_watch(3195/8262)+spy_hands_red(1241/5820) |
| `v_knife_spy` | 23 | 2564 | 8652 | 2 | spy_hands_red(1235/5820)+v_knife(1329/2832) |
| `v_medigun_medic` | 42 | 5460 | 18027 | 3 | medic_red(193/828)+medic_hands_red(1604/7812)+v_medigun(3663/9387) |
| `v_minigun_heavy` | 18 | 7206 | 21552 | 2 | v_minigun(6601/18588)+hvyweapon_hands(605/2964) |
| `v_pda_engineer` | 57 | 6444 | 16968 | 5 | 含 v_pda_engineer(3324/4992) 与 v_pda_engineer_screen(152/240) |
| `v_pda_spy` | 26 | 5022 | 11652 | 2 | spy_hands(1332/5820)+v_cigarette_case(3690/5832) |
| `v_pistol_engineer` | 42 | 8152 | 16920 | 4 | engineer_red+handL+handR_red+v_pistol(5184/5184) |
| `v_pistol_scout` | 40 | 7881 | 18630 | 2 | scout_hands(2697/13446)+v_pistol(5184/5184) |
| `v_revolver_spy` | 48 | 15987 | 31620 | 2 | spy_hands_red(2676/11640)+v_revolver(13311/19980)（**最大**） |
| `v_rocketlauncher_soldier` | 29 | 4199 | 13428 | 4 | soldier_sleeves_red(153/684)+soldier_hands(1030/5439)+v_rocketlauncher01(2499/6453)+**v_rocket(517/852)（唯一带独立弹体材料段）** |
| `v_sapper_spy` | 47 | 8837 | 21183 | 3 | spy_hands_red+v_sapper(6097/9447)+v_sapper_glass(64/96) |
| `v_scattergun_scout` | 45 | 6159 | 23169 | 2 | v_scattergun(3479/9723)+scout_hands(2680/13446) |
| `v_sd_sapper_spy` | 47 | 4876 | 18138 | 2 | spy_hands_red(2476/11640)+w_sd_sapper(2400/6498) |
| `v_shotgun_engineer` | 53 | 9724 | 18492 | 4 | engineer_handR_red+handL+engineer_red+v_shotgun(6756/6756) |
| `v_shotgun_heavy` | 43 | 9993 | 18891 | 2 | hvyweapon_hands(3237/12135)+v_shotgun(6756/6756) |
| `v_shotgun_pyro` | 25 | 7733 | 10962 | 3 | v_shotgun(6756/6756)+pyro_red(106/420)+pyro_hands_red(871/3786) |
| `v_shotgun_soldier` | 51 | 9139 | 18618 | 3 | soldier_hands(2098/10878)+soldier_sleeves_red(285/984)+v_shotgun(6756/6756) |
| `v_shovel_soldier` | 26 | 5326 | 12354 | 3 | soldier_sleeves_red+soldier_hands(1049/5439)+v_shovel(4127/6255) |
| `v_smg_sniper` | 43 | 4815 | 17847 | 3 | sniper_red(90/360)+sniper_handL_red(2075/9396)+v_smg(2650/8091) |
| `v_sniperrifle_sniper` | 44 | 5923 | 22524 | 4 | sniper_handL_red(2075/9396)+sniper_red(90/360)+v_sniperrifle(3693/12432)+v_sniperrifle_scope(65/336) |
| `v_stickybomb_defender` | 1 | 4900 | 15189 | 1 | v_stickybomb_defender（**独立弹体，无手臂**） |
| `v_stickybomb_launcher_demo` | 51 | 5157 | 19665 | 3 | demoman_hands(1827/9510)+demoman_red(152/576)+v_stickybomb_launcher(3178/9579) |
| `v_syringegun_medic` | 44 | 13391 | 24765 | 4 | medic_red(296/828)+medic_hands_red(1642/7812)+v_syringegun_glass(96/144)+v_syringegun(11357/15981) |
| `v_toolbox_engineer` | 27 | 6477 | 8418 | 3 | engineer_handR_red(845/2754)+engineer_handL(82/114)+v_toolbox_red(5550/5550) |
| `v_ttg_watch_spy` | 26 | 3975 | 12891 | 2 | spy_hands_red(1241/5820)+ttg_watch(2734/7071) |
| `v_watch_leather_spy` | 26 | 5443 | 13302 | 2 | spy_hands_red(1241/5820)+c_leather_watch(4202/7482) |
| `v_watch_pocket_spy` | 50 | 8325 | 29079 | 2 | spy_hands_red(2476/11640)+c_pocket_watch(5849/17439) |
| `v_watch_spy` | 26 | 3551 | 10707 | 2 | spy_hands_red(1241/5820)+v_watch(2310/4887) |
| `v_wrench_engineer` | 50 | 4738 | 17436 | 4 | engineer_handR_red(1086/4392)+engineer_handL(1276/6576)+engineer_red(170/768)+v_wrench_sheet(2206/5700) |

**结构结论：第一人称手臂与武器在同一个 `.mdl` 内**（每个 `v_<weapon>_<class>.mdl` 自带 `<class>_hands` / `<class>_red` 段），不是靠 companion 拼接。差异：Engineer 左右手分开建模；Soldier 有独立 sleeves 段；Sniper 只有 handL；Heavy 只有 `hvyweapon_hands`。

**资产边界（不是解析失败）**：`v_flamethrower` 与 `v_kukri` 在 VPK 里 **0 命中** —— Pyro 主武器与 Sniper 近战**没有第一人称模型**。

### 3.2 第三人称手臂复合模型（N18 的另一半）

| 模型 | bodyParts | bones | vvd 顶点 | 索引 |
|---|---:|---:|---:|---:|
| `c_scout_arms` | 1 | 58 | 2680 | 13446 |
| `c_sniper_arms` | 1 | 58 | 2215 | 9996 |
| `c_medic_arms` | 1 | 54 | 1797 | 8640 |
| `c_heavy_arms` | 1 | 60 | 3424 | 15732 |
| `c_pyro_arms` | **2** | 58 | 1880 | 8412 |

九个 `c_<class>_arms.mdl` 全部在 VPK 内（c_demo/c_engineer/c_heavy/c_medic/c_pyro/c_scout/c_sniper/c_soldier/c_spy），全部 `resolution=found-vpk` / `vtxOutOfBounds=0` / `targetLod=0` / `meshBuilt=1`。**只有 pyro 是 bodyParts=2**。骨骼 54–60，与第一人称 `v_models` 的 18–57 是两套骨架。`c_models` 下 `.mdl` 共 **319** 个，全部是第三人称世界模型。

### 3.3 材质槽位：解析器是好的，`materialResolved=0` 不是解析失败

这是我上一轮判错、必须在这里纠正的一点：

- `VmtParser::resourcePath`（`native/src/vmt_material.cpp:435`）会统一反斜杠、去前导斜杠、**自动补 `materials/` 前缀**（:439 `const std::string prefix = "materials/";`）、补扩展名。
- `viewmodel_material_probe --tf-root <tf> --model models/weapons/v_models/v_medigun_medic.mdl` 实测 **7/7 槽 `status=ok`**，逐槽带 `vmt_source=vpk` / `vtf_source=vpk`：slot0 medic_red 2048x1024 DXT1 decoded=8388608 bumpmap=1；slot1 medic_hands_red 1024x1024 DXT1；slot2 v_medigun 1024x1024 DXT5 alpha=1；slot3 medic_blue / slot4 medic_hands_blue / slot5 v_medigun_blue / slot6 v_medigun_gold 同样 ok。另有 `vm-mesh-material ... meshStride=116(exercised=1) meshes=3 descriptorsValidated=3`。
- `viewmodel_contract_probe` 的 `materialResolved=0 / flatColorFallback=true` 是**解码前的快照**：`native/src/viewmodel.cpp:391-392` 在 buildRequest 里写死 0/true，真正的值由设备侧 `applyViewModelMaterialResult`（`:411 request.resources.materialResolved = request.materials.materialResolved();`）填。`native/include/viewmodel_material.h:119-122` 明说 `materialSlots/materialResolved` 是**模型声明槽位口径**，`texturesUploaded/texturesReused` 是 **GPU 缓存口径**，两个分母故意不同。
- **准确结论**：解析器存在，且对真实资产 7/7 成功；contract probe 的 0 是「尚未上传」的诚实读数。写「解析器没生效」是错的。

### 3.4 FOV / near clip / roll / 蹲站眼位（先测现实现，不盲改常数）

| 项 | 实测 | 出处 |
|---|---|---|
| ViewModel FOV 常数 | `defaultFov=90 fovLow=40 fovHigh=120 legacyFovDefault=80`；自测 `constants/clampsAgree/boundaries/nanPreserved/legacyEightyMigrates/defaultIsNinety/explicitClamped` 全绿，`contractOk=true`，`probeValues=9` | `viewmodel_fov_probe`（无参自测） |
| 真实模型上的 FOV | `v_medigun_medic`：`defaultFov=90 fovLow=40 fovHigh=120 legacyFovDefault=80`，`contractOk=false` | `viewmodel_contract_probe` |
| 挂接点 | `v_medigun_medic` 有 3 个 attachment（`weapon_bone_L`,`muzzle`,`muzzle`），`validAttachments=3`，`alignmentReason="no placement attachment present; the pass uses the model transform"` | 同上 |
| near clip / roll / 蹲站眼位 | **未测**（属相机域；蹲站眼位归 N12-B，另一 agent 正在改 `native/src/main.cpp` 的相机区） | — |

按 N18 要求：**蹲站眼位只记录、不改**。FOV 常数在自测层是自洽的（90/40/120、legacy 80 迁移），但在真实模型上 `contractOk=false` —— 这是**待查**项，不是可以直接改常数的证据。

#### 3.4.1 世界相机 FOV 就是 ViewModel FOV（N18 的「独立投影」槽 —— 结构性缺陷，不是未测）

这是本轮新增的决定性读数，**推翻了初稿把「独立 viewmodel 投影」当作已实现**的写法。

- `native/src/native_renderer_frame.cpp:179`：`const XMMATRIX projection = XMMatrixPerspectiveFovLH(XMConvertToRadians(settings_.viewModelFov), aspect, worldNearPlane, worldFarPlane);` → `:180 const XMMATRIX mvp = view * projection;` → `:182 XMStoreFloat4x4(&constants.mvp, mvp);`。**世界 pass 的投影矩阵用的就是 ViewModel 的 FOV。**
- 全仓 `XMMatrixPerspectiveFovLH` 只有三处：`native/src/native_renderer_frame.cpp:179`（世界）、`native/src/native_renderer_viewmodel.cpp:571`（手臂，near=`max(radius*0.05,0.01)`、far=`distance+radius*4`）、`native/src/native_renderer_viewmodel.cpp:804`（weapon body，near=`max(weaponRadius*0.05,0.01)`、far=`distance+weaponRadius*4`）。**手臂与武器各有自己的投影，唯独世界复用 ViewModel FOV。**
- `settings_.viewModelFov` 全仓只有两个读者：`native/src/native_renderer_camera.cpp:184`（`reading.fovDegrees = settings_.viewModelFov;`，只读数）与上面 `native_renderer_frame.cpp:179`。
- `RenderSettings`（`native/include/native_renderer.h:60-75`）只有 `float viewModelFov = 90.0f;`（`:71`）与 `int modelLod = 4;`（`:69`），**没有独立的 camera / world FOV 字段**；`constexpr float clampViewModelFov(float value)`（`:34`）夹 40..120。
- 两个写入口共享同一字段：`native/src/main.cpp:1832`（`applyCamera` 里 `g_renderer->setViewModelFov(camera.fov);`）与 `native/src/main_camera.cpp:123`（bookmark.fov）→ bookmark / director 的镜头 FOV 也会改世界投影。
- 世界 near/far 是独立的：`native/src/native_renderer_frame.cpp:172-177`，`const float kWorldNearMapUnits = 4.0f;`、worldFarPlane=100.0f —— **只有 FOV 被耦合，near/far 没有**。

**像素证据（同一次运行内部比较，历史已提交帧）**：`evidence/t1-viewmodel-acceptance/frames/fov-40.png` vs `fov-120.png` → **156670 / 860784 像素不同（18.20%）**，8x8 网格几乎全格（左半 x<372 为 0，只有最左条带不动）；对照 `vm-off.png` vs `vm-command.png` 只差 **24907 px（2.89%）**且 8x8 网格**集中在一列竖直条带**（x 约 632–704）。→ 换 FOV 动的是整个画面（含世界几何），不是只动武器。

**门禁覆盖缺口**：`grep -ho "viewmodel-fov [0-9]*" *.sh` 全仓 **34 次 90 / 3 次 40 / 2 次 120 / 2 次 10 / 1 次 200 / 2 次无值**；**唯一会变 FOV 的门禁是 `viewmodel-contract-check.sh`（distinct=6）**，而它的断言全是读数与 clamp（`:581-582` probe FOV low/high=40/120；`:586-593` fov 90 kept、fov 10→40 fovClamped=true、fov 200→120；`:632-638` `assert_eq "product vm90 fov" "$(field "$LAST_READING" fov)" "90.0"`；`:659-707` vmdefault/vmstored(60)/vmlegacy(80→90)/vm40/vm120/vmleft/vmfov10/vmfov200）。**没有任何一条比对两个 FOV 下的世界像素** —— 这条缺口仍在。

落点（不在本卡修）：`native/src/native_renderer_frame.cpp:179` 应读一个独立的 camera/world FOV；`RenderSettings`（`native/include/native_renderer.h:59-74`，`int modelLod = 4;` 在 `:69`、`float viewModelFov = 90.0f;` 在 `:71`）需要新字段。**注意 `--viewmodel-fov` 的文档语义**（`native/src/main.cpp:2961` `clamped to 40..120; omitted means the stored`）是 ViewModel 专用，`native/src/main.cpp:7987-7988` / `:13300-13301` / `:13421-13422` 三处只把它传给 `buildViewModelRequest` / `buildViewModelCompanionRequest` —— **flag 的意图是 ViewModel 专用，实现却污染了世界**。

### 3.5 动画层：这是 N18 真正的缺口

- `--viewmodel-apply-animation` 在两份钉住录制上「changes nothing measurable」（`native/src/main.cpp:3040-3049`，原句在 `:3044`：`On both pinned recordings, turning it on changes nothing measurable`），每帧仍被 refused、画 bind pose。（初稿此处写 `main.cpp:2018-2026`，行号已随 HEAD 漂移，本轮更正。）
- `--viewmodel-sequence <n>` 是**输入覆盖，不是功能**（`native/src/main.cpp:3026-3034` 注释：`an input OVERRIDE, not a feature`）：钉住录制上 ViewModel 实体带 sequence 1/29/30，而它指的模型只能摆 sequence 0。覆盖点在 `native/src/main.cpp:13284-13287`（`if (viewModelSequenceOverride >= 0) { animationInput.hasSequence = true; animationInput.sequence = viewModelSequenceOverride; }`，注释说明它**REPLACES a value rather than supplying one**）。（初稿此处写 `main.cpp:2004-2012`，已更正。）
- 产品**确实读** `m_nSequence`：`native/src/entity_model.cpp:409` 起（`lookup.input.entityIndex = bestIndex; lookup.input.entityPresent = true; bool hasSequence …`，`readIntSlot(state, "m_nSequence", hasSequence)`）；`native/src/animation_binding.cpp:444-446`（`if (request.sequence < 0) return refuse(AnimationInputStatus::MissingSequence, "entity carried no m_nSequence");`），`:447` 越界分支、`:486` 用序列号采样；`native/src/viewmodel.cpp:504-508`（`"the viewmodel entity at index … carries no m_nSequence"`）与 `:531`（`m_nSequence` out of range）。
- 而**玩家身体**走本地标签匹配，因为「the demos this product can play carry no per-player m_nSequence or m_flCycle」（`native/src/main.cpp:3050-3057`，T61 注释；初稿写 `main.cpp:2029-2031`，已更正）。**两条路并存：ViewModel 走服务器序列号，玩家身体走本地标签。**

### 3.6 结论（本轮修订）

- 槽位（model / arms / companion / team）：**数据层 PASS**（37 个第一人称 + 319 个第三人称全部可读、可建网格；九职业手臂复合模型齐）。**但「真正被画过」的槽位原本只有 1 个**（门禁历史只用过 `v_scattergun_scout.mdl`），本轮补了 2 个（见 §3.7）。
- material：**解析器 PASS（7/7）**；**GPU 上传本轮首次证到**（见 §3.7：`materialResolved=3 uploads=3 phongAppliedRanges=2`，cls-soldier `6/6/4`）。历史 t1 证据里没有 `gpu: viewmodel materials` 行（那条 trace 是后来才加的），所以旧证据**不能**当上传证据 —— 这条判级本轮从「未证」改为「已证」。
- sequence（第一人称动画）：**NOT VERIFIED** —— 通道被 refused，画 bind pose。本轮的产品帧进一步坐实：`skinning=0 boneTableCoverage=0/0 bindPose=1`。
- **独立投影**：**不存在** —— 世界相机 FOV 被 `--viewmodel-fov` 控制（§3.4.1）。这是结构缺陷，不是未测。
- LOD1+：**两边要分开说**。① 事实：`native/src/model_loader.cpp:1483-1495` 取文件声明的**最低 LOD** 并只挑该 LOD 的 descriptor（`:1531-1537` 范围数不符则 refuse）；37 个 `v_models` 共 101 个 VTX descriptor **全部 lod=0、零 LOD1** —— 对 ViewModel 而言**没有可选 LOD 数据，LOD 切换无对象**。② 缺失能力：玩家/世界模型侧**才是选择策略缺失** —— `grep -rn "modelLod"` 只命中 `native/src/native_renderer_settings.cpp:42/52` 与 `native/include/native_renderer.h:69`，**没有任何读者把 modelLod 传给绘制**，没有距离/屏幕尺寸/预算判据；而玩家模型确实有 LOD（scout 50 desc/9 target、soldier 50/9、heavy 43/8、spy 31/7、medic 29/6；lod=0 计 39、lod=1 计 39、lod=2 计 33、lod=3..6 各 23）。
- **团队涂装**：**结构缺失** —— ViewModel 材质解析完全不经 MDL 皮肤家族表（`native/src/viewmodel_material.cpp:318` 只按 `metadata.textureCandidates[slot]` 取槽；`grep -n skinFamil\|teamNumber\|m_iTeamNum\|paintIndex` 对四个 viewmodel 文件 **0 命中**；`grep -n "viewmodel-team\|--team"` 对 `main.cpp` **0 命中**），上传键只有 `slot.vtfPath`（`native/src/viewmodel_material.cpp:367`）。数据侧：9 个 `c_<class>_arms.mdl` **全部 skinFamilies=10**，**7/9 的 family1≠family0**（红→蓝；例外 `c_scout_arms` textures=1、`c_heavy_arms` textures=2）；37 个 `v_models` 里 **29/37** family1≠family0，family0 恒等 37/37。→ **两队 ViewModel 手臂/袖子都画 family0（红）**。团队信号在产品里**已可用**（`native/include/playback_hud.h:15-16 bool teamKnown; std::int32_t team;`，`native/src/main.cpp:7347` 已写进 trace），缺的只是 ViewModel 材质侧不消费。
- **手臂骨合并**：**没有对象** —— `grep -rni "bonemerge\|bone_merge\|mergeBone\|LayerBone\|arm layer\|armslayer"` 在 `native/src/*.cpp` `native/include/*.h` 里除 `animation_sequence_match.h` 三处讲 `armslayer_*` 序列名（`:36 / :118 / :344`）外**无任何骨合并代码**。属于**未实现**，不是未测。已实现的挂点只有世界 pass 的持握点 `findHoldPoint`（`native/src/main.cpp:12014` 定义、`:12067` 调用，只认 `attachment.name == "weapon_bone"` 或同名 BONE，两者都没有就**不挂**，注释明写 no guessed bone, no weapon at the origin）。
- **像素**：**本轮首次证到**（§3.7：vmoff 负控 vs 两个职业帧，差异 8952 px / 5926 px，集中在画面中下偏右）。但只覆盖 2 个职业的 1 个槽位，**不代替全 37 槽位**。


### 3.7 产品实跑：3 条真帧（12 条计划里 9 条 BLOCKED-by-D72）

这一节是 N18 的**像素级实跑证据**。初稿写「本环境无产品运行窗口」，本轮拿到了窗口，但**只跑成 3 条**：锁被 D72（MSYS pid 命名空间缺陷）反复判死，三次事故让 12 条计划里的 9 条必须留空。

#### 3.7.1 共同身份（每一帧都成立）

| 项 | 值 |
|---|---|
| 产品 exe | `native/build-nmake/tf2_demo_native.exe`，`sha256=e61c7fd9e5dba5167463f493937b23be799c96bcdd35bee7e0970d747a3bb078`，3,025,408 B，mtime 2026-11-15 22:58:31 |
| 录制 | `D:/TF2_Demo_Player/testdata/demos/bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem`，51,834,617 B，`sha256=308cc1453672b26dfdcc106bcb62eac4a7eea97899adc8830f51711be834607f` |
| HEAD | **采集时 `32aa54de`**；本文件全部行号已在 **`e5fe2219`**（n05 的 N05-step-3 提交之后）上逐条复核过，`native_renderer_frame.cpp:179` 的世界 FOV 行未移动 |
| 共同参数 | `--start-paused --capture-tick 0 --viewmodel-fov 90 --hidden-window --audio-disabled` + 每次一个 `mktemp -d` 隔离的 `--settings-file` / `--ui-settings-file` |
| 帧尺寸 | **1264x681**，BMP 2,582,406 B |
| 产物目录 | `D:/_shared/EXEC-2026-11-15/INST4/p1b-n18/draw/` |

#### 3.7.2 三条帧（全部 rc=0、全部带 trace）

| 帧 | 命令差异 | BMP sha256 | trace 字节 |
|---|---|---|---:|
| `vmoff` | （无 `--viewmodel`）**负控：没有第一人称模型** | `8a40433c5bc18ce6ab97903a35a2377fb6ef2ac8280c4e82409324f067cf4284` | 124894 |
| `cls-scout` | `--viewmodel models/weapons/v_models/v_scattergun_scout.mdl --viewmodel-render command` | `2e6147c6d24c5adc57b2bb2906c116f820f1521a466631dd3f75db9370443f49` | 132953 |
| `cls-soldier` | `--viewmodel models/weapons/v_models/v_rocketlauncher_soldier.mdl --viewmodel-render command` | `d6c7940e3b820039f226566f663fc9454feb1815430a047dd7e8ee1f34b8aa2d` | 133214 |

- `vmoff.trace` 里 `gpu: viewmodel` 行 **0 条** —— 负控的行为正确：不加 `--viewmodel` 就没有这条 pass。
- 每次 `--metrics-file` 都只有一行数据：`0.564881,1,1.77028,0,…` / `0.619234,1,1.6149,0,…`（暂停态单帧渲染，符合预期）。

#### 3.7.3 `cls-scout` 的完整 viewmodel 读数（trace 原文）

```
gpu: viewmodel pass order worldPass=1 viewModelPass=8 afterWorld=1 drawCalls=1 vertices=23169 depthWrite=1 depthFunc=less_equal skinning=0 boneTableCoverage=0/0 animationInput=missing meshRadius=24.98 alignment=weapon_bone vmAnimInput=missing vmAnimApplied=0 drawnModel=models/weapons/v_models/v_scattergun_scout.mdl entityModelMatchesDrawn=0 sequenceInRange=0 sequenceAccepted=0 sequenceOverride=-1 modelMismatch=1 boneMismatch=0 bindPose=1 ranges=2 boundSlots=2
gpu: viewmodel materials path=models/weapons/v_models/v_scattergun_scout.mdl slots=3 resolved=3 decoded=3 decodeFailed=0 uploadSlot=-2 uploads=3 cache=3 drawnSlot=0 source=mdl-mesh-table meshTable=1 meshStride=116 meshStrideExercised=1 meshCount=2 descriptorsValidated=2 strideSource=unexercised reason=-
gpu: viewmodel materialReadings passRan=1 materialSlots=3 materialResolved=3 texturesUploaded=3 texturesReused=2 fallbackSlots=0 alphaSlots=2 alphaAppliedRanges=0 phongSlots=3 phongAppliedRanges=2 bumpSlots=1 bumpAppliedRanges=0 selfillumSlots=0 selfillumAppliedRanges=0 decodedSlots=3 drawRanges=2 fallbackRanges=0 boundSlots=2 blendedRanges=0 alphaTestedRanges=0 flatColor=0 slotOverride=-1 missingSlotInjected=-1 decodeFailure=-
gpu: viewmodel ranges path=models/weapons/v_models/v_scattergun_scout.mdl ranges=2 firstRange=(slot 0, 9723 vertices, textured=1 translucent=0 alphatest=0) uploaded=1 rangeSlots=0,1 rangeVertices=9723,13446 rangeTextured=1,1
gpu: viewmodel request path=models/weapons/v_models/v_scattergun_scout.mdl status=ok resolution=3 companions=1 meshReady=1 attachments=5 valid=5 partial=0 invalid=0 alignment=weapon_bone fov=90.0 hand=right reason=-
gpu: viewmodel upload mesh=1 vertices=23169 bones=45 alignment=weapon_bone enabled=1 reason=-
gpu: viewmodel centre viewSpace=(7.49,-5.36,56.17) clip=(4.038,-5.361,55.367,56.170) ndc=(0.072,-0.095) onScreen=1
gpu: viewmodel frame eye=(0.45,-0.48,0.85) forward=(-0.043,0.974,-0.220) placement=(-9.42,52.74,-16.74) fov=90.00 near=1.2491 far=154.8857 distance=54.9594 radius=24.9816 hand=(1,0,0,0,0,1,0,0,0,0,1,0) attachment=weapon_bone
gpu: viewmodel weaponBodyReading armDrawCalls=1 weaponDrawCalls=0 weaponFallback=noViewModelEntity weaponVisible=0 weaponReason=no ViewModel entity was selected, so no weapon entity can be named weaponEntity=-1 weaponVertices=0
scene: vmAnim entity=-1 sequence=-1 ... status=missing poseAvailable=0 applied=0 ... entityRule=no ViewModel entity names the view entity in m_hOwner reason=no viewmodel entity owned by the view entity drawnModel=models/weapons/v_models/v_scattergun_scout.mdl entityModelMatchesDrawn=0 sequenceInRange=0 bindPose=1
```

读法：**`--viewmodel` 在 `--viewmodel-render command` 下画的确实是 `v_scattergun_scout`，但实体自己的模型（`c_scout_arms`）不在，所以 `entityModelMatchesDrawn=0 modelMismatch=1`、序列永远 `missing`、`bindPose=1 skinning=0`。** 这解释了 §3.5 的 refused —— **不是「动画没实现」，而是 command 通道下根本没有实体模型提供序列。**

#### 3.7.4 `cls-soldier` 的完整 viewmodel 读数（**`alignment=-` 是真实回退现场**）

```
gpu: viewmodel pass order ... vertices=13428 ... meshRadius=40.73 alignment=- vmAnimInput=missing vmAnimApplied=0 drawnModel=models/weapons/v_models/v_rocketlauncher_soldier.mdl entityModelMatchesDrawn=0 modelMismatch=1 bindPose=1 ranges=4 boundSlots=4
gpu: viewmodel materials ... slots=6 resolved=6 decoded=6 decodeFailed=0 uploadSlot=-2 uploads=6 cache=6 drawnSlot=0 meshStride=116 meshCount=4 descriptorsValidated=4
gpu: viewmodel materialReadings ... materialSlots=6 materialResolved=6 texturesUploaded=6 texturesReused=4 fallbackSlots=0 alphaSlots=4 phongSlots=6 phongAppliedRanges=4 bumpSlots=2 bumpAppliedRanges=4 selfillumSlots=0 drawRanges=4 fallbackRanges=0 boundSlots=4
gpu: viewmodel ranges ... ranges=4 rangeSlots=0,1,2,3 rangeVertices=684,5439,6453,852 rangeTextured=1,1,1,1
gpu: viewmodel request ... meshReady=1 attachments=0 valid=0 partial=0 invalid=0 alignment=- fov=90.0 hand=right reason=-
gpu: viewmodel upload mesh=1 vertices=13428 bones=29 alignment=- enabled=1 reason=-
gpu: viewmodel centre viewSpace=(12.22,-8.74,91.57) clip=(6.582,-8.740,90.260,91.569) ndc=(0.072,-0.095) onScreen=1
gpu: viewmodel frame eye=(0.45,-0.48,0.85) forward=(-0.043,0.974,-0.220) placement=(-15.64,86.28,-27.83) fov=90.00 near=2.0363 far=252.4971 distance=89.5958 radius=40.7253 hand=(1,0,0,0,0,1,0,0,0,0,1,0) attachment=-
```

- `rangeVertices=684,5439,6453,852` 与 §3.1 表格的 `soldier_sleeves_red(153/684)+soldier_hands(1030/5439)+v_rocketlauncher01(2499/6453)+v_rocket(517/852)` **逐个对上** —— 包括最后那段 **852 顶点的独立弹体**。→ **4 个 range 全部真画（`boundSlots=4 fallbackRanges=0`），材质也是 4 个各自绑定。**
- `attachments=0 alignment=-` 与只读探查的 `contractOk=false`（§3.1 的 12 个 false 之一：v_rocketlauncher_soldier 模型**真没有附着点**）在**产品画面**上互为对照：没有锚 → 用模型自身变换放置 → 但**照样画全部 4 段**。这条是「偏好集顺序/锚缺失是放置问题不是绘制问题」的直接证据。

#### 3.7.5 像素证据（vmoff 是「没有第一人称模型」的负控）

| 对比 | 差异像素 | 占比 | bbox |
|---|---:|---:|---|
| vmoff vs cls-scout | 8952 / 860784 | **1.040%** | (580,283)-(789,477) |
| vmoff vs cls-soldier | 5926 / 860784 | **0.688%** | (624,331)-(777,490) |
| cls-scout vs cls-soldier | 11847 / 860784 | 1.376% | (580,283)-(789,490) |

8x8 网格（每格差异像素数）：差异**只落在 grid[3..5][3..4] 这 4–5 格**，其余 **56 格全 0**。逐格读数（vmoff vs cls-scout）：`grid[3][4]=1720`、`grid[4][3]=697`、`grid[4][4]=3564`、`grid[5][3]=1116`、`grid[5][4]=1855`。→ 与手臂/武器在画面中下偏右的位置一致，**不是全画布抖动**。

肉眼（`draw/vmoff-crop.png`、`draw/cls-scout-crop.png`、`draw/cls-soldier-crop.png`，ROI (500,200)-(900,560) 再 NEAREST ×2）：
- `vmoff` = 一双空手（红袖子 + 露指手套），**没有枪**。
- `cls-scout` = 双手举一把**黑色散弹枪**。
- `cls-soldier` = 右手一只红色拳头 + 一把**黑色火箭筒**（可见扳机护圈与灰色扳机，右侧）。

→ **这是「ViewModel 真的被画到屏幕上了」的像素证据，不是计数器**。也是本轮首次把 §3.6 的「像素未证」变成「已证（2 个职业 / 1 个槽位/职业）」。

#### 3.7.6 9 条没跑成的 + 锁与残留进程（原样照抄，不抹）

计划 12 条，落地 3 条，**剩 9 条标 `BLOCKED-by-D72`**：pyro / demo / heavy / engineer / medic / sniper / spy 七个职业，加 `cls-scout-flat`（`--viewmodel-flat-color` 正控）与 `cls-scout-repeat`（可重复性控制）。**没有一条是在我持锁期间被别人打断的，打断发生在取锁/释放本身。**

`LOCK-NOTES/e17-e20-char.txt` 全量（「锁与残留进程」栏原文）：
```
2026-11-15 23:33 e17-e20-char P1b N18: 37-slot draw sweep, 12 paused captures (--start-paused --capture-tick 0), est 12-25 min
2026-11-15 23:54 e17-e20-char P1b N18: batched draw sweep (12 paused captures + vmoff baseline), est 12-25 min. Lock was FREE at start, product_processes=0.
2026-11-15 23:57 START P1b N18 sweep v2
2026-11-15 23:58 END P1b N18 sweep v2: STOPPED at vmoff+1. First line was STALE-LOCK=RECLAIMED owner=ui-d-n14-basemap pid=59684 age=22s reason=owner-dead -- I did not own that lock and did not run on it.
2026-11-15 23:59 START P1b N18 sweep v3
2026-11-16 00:00 END P1b N18 sweep v3 rc=9 (LOCK-OWNER-CHANGED after cls-soldier: n05-n06-maps reclaimed my live lock, age=18s)
```

三次 D72 现场（**这条缺陷的实际受害者序列**，D72 条目见缺陷台账）：
1. **23:53 我回收 n05**：`VERIFY-LOCK=FAIL (… owner=n05-n06-maps pid=58372 age=5s heartbeat_limit=900s)` → 同一次调用内 `STALE-LOCK=RECLAIMED owner=n05-n06-maps pid=59700 age=3s reason=owner-dead` → `VERIFY-LOCK=ACQUIRED owner=e17-e20-char pid=1949`。
2. **23:57 我回收 ui-d**：`sweep-v2.log` 首行 `STALE-LOCK=RECLAIMED owner=ui-d-n14-basemap pid=59684 age=22s reason=owner-dead` → `VERIFY-LOCK=ACQUIRED owner=e17-e20-char pid=925`。`LOCK-NOTES/ui-d-n14-basemap.txt` **不存在**。
3. **00:00 n05 回收我**：`VERIFY-LOCK=ACQUIRED owner=e17-e20-char pid=1570`（23:59:35）→ 跑完 `cls-soldier rc=0` → `LOCK-OWNER-CHANGED cls-soldier owner=n05-n06-maps` → `VERIFY-LOCK=RELEASE-SKIPPED … (owned by pid 54920, not 1570)` → `LOCKRUN-RC=9`。n05 笔记里同一条：`STALE-LOCK=RECLAIMED owner=e17-e20-char pid=1570 age=18s reason=owner-dead (not mine; I did not run on it)`。

机制（`tools/verify-lock.sh:87-98`）：`vlock_pid_alive "$old_pid" && alive=yes`（`:88`，`kill -0`）→ `:89` 要求 `alive=yes` 才打 FAIL → alive=no 就 `:94 reason=owner-dead`、`:98 rm -rf "$d"`。**锁里存的是 MSYS pid，跨 bash 安装 `kill -0` 必失手，于是 age=3s/18s/22s 的活心跳全被判死。** 这是执行基础设施缺陷，不是捕获内容问题。

**残留进程状态**：sweep 结束后 `tasklist | grep -ci tf2_demo_native` = **0**；锁目录 `.scratch/verify-all.lock` = **不存在**（00:02:27 复核）。跑之前我误启动过一次**没包 lockrun** 的裸跑，3 秒内就 `job_kill` 掉，**没有产出任何帧**（那次产物只有 0 字节的 `vmoff.log`）。

#### 3.7.7 一条必须记下的自伤：D-40 路径 bug

前两条 sweep（v1/v2）的 12 次调用全部 `rc=15 bmp=0`，**是我自己的 bug**：脚本里 `TF2_NATIVE_TRACE="$ROOTW/$OUT/<name>.trace"`，而 `$OUT` 是 POSIX 绝对路径 `/d/_shared/…`，拼成了 `D:/TF2_Native_Worktrees/t105-e5//d/_shared/…`，产品打不开 metrics/trace 就 `return 15`（`native/src/main.cpp:10676`）。先例与解法都在 `material-coverage-check.sh:541`（D-40 注释）+ `:545-550`（`abs_win_path()` 用 `cygpath -m`）。v3 起 `OUTW=$(cygpath -m "$OUT")`、`ISOW=$(cygpath -m "$ISO")`。**那 12 次读数全部作废，不进任何分母。**

#### 3.7.8 本节证明什么 / 不证明什么

**证明**：① 产品确实把 `v_<weapon>_<class>.mdl` 画到了屏幕上（两条独立证据：trace 的 `vertices=23169/13428 drawCalls=1`，以及负控 vmoff 的像素差 1.04% / 0.688% 且集中在手臂区域）；② 多 range 多材质槽各自绑定的路径在真实资产上走通（scout 2 range 2 槽、soldier 4 range 4 槽，`fallbackRanges=0`）；③ 材质解码→GPU 上传确实发生（`texturesUploaded=3/6`、`phongAppliedRanges=2/4`、`bumpAppliedRanges=0/4`）；④ 无放置附着点的模型（soldier）在 `alignment=-` 下**仍然画全部 range**，说明锚缺失只影响摆放不影响绘制。

**不证明**：① 不证明 37 个槽位都能画 —— 只测了 2 个职业各 1 个槽位，剩 35 个未画；② 不证明任何动画 —— 三条帧全是 `skinning=0 bindPose=1`，command 通道下实体模型不在，序列必然 missing；③ 不证明团队涂装 —— `cls-scout` 的 `scout_hands` 只有 1 张纹理（`c_scout_arms` family1 也与 family0 相同），要证涂装必须用有红蓝差异的模型（如 medic / soldier），那 7 个都在这 9 条 BLOCKED 里；④ 不证明 LOD 选择；⑤ 不证明 weapon body（`weaponDrawCalls=0 weaponFallback=noViewModelEntity`，因为 command 通道下没有 ViewModel 实体可命名）；⑥ 不证明可重复性（`cls-scout-repeat` 没跑成，**只有 1 次观测**）。
#### 3.7.9 N18 卡片判据逐条对账

| 卡上要求 | 本轮状态 | 证据或缺口 |
|---|---|---|
| 先一个职业一个槽位**实际绘制** | **部分达成：2 / 9** | `cls-scout`（`v_scattergun_scout`）、`cls-soldier`（`v_rocketlauncher_soldier`）有 trace + 像素（§3.7.2/3.7.5）；其余 7 个职业 `BLOCKED-by-D72` |
| 再扩**九职业 / 槽位** | **未达** | 37 槽位里画过 2 个；门禁历史只画过 `v_scattergun_scout` |
| **手臂骨合并** | **未实现（不是未测）** | 全仓 0 处骨合并代码（§3.6）；已实现的只有世界 pass 的持握点 `findHoldPoint` |
| **团队 skin** | **结构缺失** | 材质解析 4 个文件里 `skinFamil/teamNumber/m_iTeamNum/paintIndex` **0 命中**；数据侧 7/9 个 `c_<class>_arms` 有红蓝 family1（§3.6） |
| **独立 viewmodel 投影** | **不成立：被耦合** | 世界 pass 投影读 `settings_.viewModelFov`（`native/src/native_renderer_frame.cpp:179`）；换 FOV 画面差 18.20%（§3.4.1） |
| **LOD1 明确补实现** | **ViewModel 侧无对象；玩家侧缺选择器** | 37 个 `v_models` 的 101 个 descriptor 全 `lod=0`；`modelLod` 全仓无绘制读者（§3.6） |
| **配对 probe 成功 ≠ GPU 有模型；要用实际 draw / 材质 / 骨骼输出定位** | **本轮照做并给出对照** | 正是这条把「37 槽位 probe 全绿」和「只有 1 个槽位被画过」分开：`contractOk 25/37`、`meshReady 37/37` 都是 probe 结论，而 `drawCalls=1 vertices=23169` + 像素差 1.04% 才是「画上去了」；反过来 `materialResolved=3` 单独看也不能当上传证据，必须配 `texturesUploaded=3` 与 `phongAppliedRanges=2` |
| **高光 / 光影暂不扩范围** | **本轮不声称** | `phongAppliedRanges=2/4` 是**读数**，不构成「光影质量」结论 |

---

## 4. N19 网格 / 实例 / 纹理容量

### 4.1 四层预算（capacity 与读数行，全部 HEAD 行号）

| 层 | 常量 / capacity | 读数行（trace 关键字） | 位置 |
|---|---|---|---|
| A prepare | **64**（`kPrepareCapacity`） | `budget: prepare capacity= candidates= admitted= built= deferred= failed= admittedByPriority= deferredByPriority=` | `native/src/main.cpp:3775`（常量）、:3809（准入）、:3823（reserve）、:3917→实际 trace 行 4063 |
| B meshTable | **96**（`kEntityModelMeshCapacity`） | `budget: meshTable capacity= resident= evictions= capacityRefusals= reuploads= resupply=` | `native/src/native_renderer_entity.cpp:33`、:36 `capacity()`、:330 表满分支、:7895（trace） |
| C instances | **96**（`entityModelInstanceCapacityLimit = 96`，`native/include/native_renderer.h:941`）+ 静态道具 **4096**（`mapStaticPropInstanceCapacityLimit`，同文件 :945） | `budget: instances capacity= submitted= accepted= slots= rejected=missingMesh:…,deferredNotResident:…,badMesh:…,nonFinite:…,budget:…` | `native_renderer_entity.cpp:427`、`main.cpp:11727`（trace） |
| D draw | draw 预算按帧分配 | `budget: draw capacity= playerBudget= propBudgetAfterAttach= playerDrawn= propDrawn= attachReserved= attachDrawn= droppedPlayers= droppedProps=` | `main.cpp:11671`（trace）；`propBudget -= attachBudget` 在 :9929-9930 区段 |

另有 `budget: mapModels` 与 `budget: eviction key=%s action=unpublish,resupply`。

### 4.2 优先级（`native/include/entity_mesh_budget.h`）

- 枚举：`PlayerBody(0)` > `HeldWeapon(1)` > `Overlay(2)` > `WorldProp(3)` > `Deferred(4)`；`entityMeshPriorityName` 返回 `player` / `weapon` / `overlay` / `prop` / `firstPersonOnly`。
- 分类入口 `classifyEntityMeshPath` 在 `:81`；`kPlayerPrefix = "models/player/"` 在 `:95`；前缀判定在 `:100-102`。注意 `models/player/items/...`（帽子）被判为 **WorldProp 而非 PlayerBody**。
- overlay mesh **pinned 不可驱逐**；驱逐是连坐的（清 drawRanges/textured 计数、推 `evictedKeys`、`reconcileEvictedEntityMeshes` 下线重供）。
- 顺序是「先建后逐」。

### 4.3 三处武器 OOM（复现口径）

| 项 | 值 |
|---|---|
| 门禁脚本 | `attach-weapon-budget-check.sh`（末行固定 token `ATTACH-WEAPON-BUDGET=PASS|FAIL`，`ledger-recon` 的 8.5-5 等这一行） |
| demo | `D:/TF2_Demo_Player/testdata/demos/a9630c6557468b24f0b8c2e7dab38ed8_match-20261030-2145-pl_upward_f12.dem` |
| demo sha256 | `7e047aff1f7743915e355eac907a95dc3d981aa0fae5591b6b15346fb77dc4a3` |
| 关键参数 | `--capture-tick 30001 --playback-speed 32 --window-size 1280x720 --debug-camera 1707.0 -125.1 152.8 4.1 0.0 --player-animation --player-weapon-attach on` |
| 注意 | `--capture-tick` **不是 seek**（`main.cpp:9015-9019` 播过就夹回），所以 supplied 是**播放域事件** |
| 三只模型 | `models/weapons/c_models/c_flamethrower/c_flamethrower.mdl`(4452 B)、`models/weapons/c_models/c_sapper/c_sapper_xmas.mdl`(2892 B)、`models/workshop_partner/weapons/c_models/c_shogun_warhorn/c_shogun_warhorn.mdl`(2052 B)，均在 `tf2_misc_dir.vpk` 内可读 |
| 冻结 exe | `native/build-nmake/tf2_demo_native.exe`，本轮前缀 sha256 `423ed9a8d6fa2fc62a2f`（**跑之前必须重取**） |

**断言骨架（脚本里 20+ 条，摘关键的）**：A 层 capacity 必须仍是 **64**；B 层必须仍是 **96**；A 层 `admitted+deferred==candidates`；`built+failed==admitted`；B 层 `resident<=capacity`；C 层 `missingMesh==0`；D 层 `droppedPlayers==0`；`weaponAttach overBudget==0`；三只模型 **named refused 必须 0**（`c_flamethrower` / `c_shogun_warhorn` 必须 `supplied>=1`）；`0x8007000E` 行数必须 **0**；`deferred>=1`（证明预算真的在起约束）。

**负控（脚本头注释原文）**：把 prepare 的优先级准入改回先来先服务，这批 named supplied 断言就会 **RED** —— 证明机制是**优先级准入**，不是放大常数。

**历史读数（不是本 HEAD 的）**：`docs/T85-BUDGET-SUMMARY.md` 记 `pl_upward_f12`、`ent=3`、`tick 30001`、`speed=32`、`--player-animation --player-weapon-attach on`：t79 exe `a3c93d57…724e9` → 改后，`gpu models 64/64→64/64`、`attach supplied/refused 23/3→13/0`、**`0x8007000E` 行数 601→0**、同帧像素两边都是 `d297340f6bbd1c9e…476987c`。**这条只能当基线，不能当本 HEAD 的证据。**

### 4.4 key 命名空间与重复注册（N19 要求核查项）

**未证。** 本轮只确认了优先级分类按路径前缀做（`classifyEntityMeshPath`）、overlay mesh pinned、驱逐会推 `evictedKeys` 并触发 `reconcileEvictedEntityMeshes`。**world / attach / overlay 三个 key 命名空间是否真的一致、有没有重复注册，我没有测**，不写结论。

### 4.5 结论

- 预算账目四层都在，读数行都在，优先级分类在。**框架 PASS**。
- OOM 三例的**复现口径与判据已写死**（§4.3），但**本 HEAD 未跑** → **NOT VERIFIED**。
- 命名空间/重复注册 → **未证**。

---

## 5. N20 三指定效果（**OPEN / 未实现**）

### 5.1 四列逐效果表（lead 要求的格式）

| 效果 | ① 资产存在（pcf 路径） | ② 解析器存在 | ③ 绘制存在 | ④ 可复现格 |
|---|---|---|---|---|
| **Pyro 武器火焰** | 有：`particles/flamethrower.pcf`（+ `_dx80` / `_dx90_slow` / `_high` / `_mvm` / `_test`）、`particles/burningplayer.pcf`（+ `_dx80`） | **无**（PCF 未解析，见 §5.3） | **无** | **无** |
| **Medic 治疗束** | 有：`particles/medicgun_beam.pcf`（+ `_dx80`）、`particles/medicgun_attrib.pcf`、`particles/passtime_beam.pcf` | **无** | **无**（且产品里**没有任何 beam 代码**；`materials/effects/beam001_*.vmt` 在档案里查不到） | **无** |
| **流血** | 有：`particles/blood_impact.pcf`、`particles/blood_trail.pcf`（+ `_dx80`） | **无** | **无** | **无** |

### 5.2 已经存在的部分（只有「事件记账」，没有「画」）

- `native/src/effects_timeline.cpp`（82 行）只有 `add` / `sortByTick` / `range` / `countThrough`（`:35`）与 `addFireBulletsEvent`（`:41`）、`addExplosionEvent`（`:57`）、`addParticleEvent`（`:70`）。
- 产品侧唯一消费点：`main.cpp:10110` `hudEffectsDue = sceneTickKnown ? effectTimeline.countThrough(sceneTick) : 0;` 与 `:10111` `traceFmt("scene: hud fxDue=%d drawable=%d", ...)` —— **只打计数**。
- `applyTempEntityFields`（`native/src/demo_header_events.cpp`）能解的事件：`CTEFireBullets`（`m_iPlayer/m_iWeaponID/m_iMode/m_iSeed`）、`CTEPlayerAnimEvent`（`m_hPlayer/m_iEvent`）、`CTETFParticleEffect`（`m_iEffectIndex` + `m_szParticleName`）、`CTETFExplosion|CTEExplosion`（`m_iMagnitude/m_iScale/m_iRadius`）、`CTEClientProjectile`（`m_vecVelocity/m_nModelIndex/m_hOwner/m_nLifeTime`）。**没有 `CTEBleed`，也没有任何 beam 类解析。**
- `addFireBulletsEvent` 里写死 `event.shots = 1`，注释：`One CTEFireBullets event; protocol shot count is not decoded.`；校验规则是 tick<0、`fireDelaySeconds` 非有限或 <0 或 >60、`shots==0` 一律 `return false`。

### 5.3 PCF 能力实测（「解析器不存在」的硬证据）

```
native/build-nmake/pcf_capability_probe.exe "D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
```

```
pcf loose_count=0 vpk_status=opened vpk_pcf_count=135 first_entry=particles/bigboom.pcf
first_prefix_hex=3c 21 2d 2d 20 64 6d 78 20 65 6e 63 6f 64 65 64
dmx_header_candidate=1 system_markers=1 operator_markers=6
header_validated=false parsed=false rendered=false mapping=false
```

读法：**PCF 能读到（135 个）、DMX 头候选成立（`<!-- dmx encoded -->`）、但未校验、未解析、未渲染、未映射。** 这比 grep 更有力，因为它直接证明「能拿到字节，但没有解析器」。

### 5.4 结论

**OPEN（未实现）**。按 lead 裁决 A2：不因为资产齐全给 PASS。要推进这件事的最小切口是「选一例先实现」（建议流血或治疗束，事件源与生命周期都最简单），而不是扩一般粒子引擎。

---

## 6. 本文件证明什么、不证明什么

**证明：**

1. 模型/材质/粒子资产在 `tf2_misc_dir.vpk` 内可直读，`pak01_dir.vpk` 缺失不影响它们（附带解释掉 N07 的 `vtfMissing=0` 悬案）。
2. 九职业序列表的实际家族分布（哪些动作有数据、哪些 0 条），**且已用不区分大小写的重算更正了初稿里 jump/walk 两行的错误**（§2.1 第 2 条、§2.4 第 3 条）。
3. 产品侧四个动作（idle/moving/crouch/flinch）有真实的输入→选序→骨骼链路，其余七个动作没有。**其中 idle/run/crouch 已有画面级证据（§2.6.5 的 677 px 与 0 px 两组），flinch 只有 trace 级**。
3b. **九职业 × 6 stance × 4 姿态的 checksum 普查（§2.6.2）**：产品实际会播的 18 条 stand/run clip 全部 distinct=4；同时给出两条**反例**（medic crouch 冻结、scout/medic jump float 单姿态）与一条**对照**（medic crouch_walk 四姿态全不同，证明解码路径没坏）。
3c. **「空中态」的三层分界（§2.6.4）**：资产齐（九职业 jump 家族）+ wire 有 `FL_ONGROUND` 位（168 条 state 行里 8 条该位为清）+ 产品代码 0 处读取。这是「数据齐 ≠ 已实现」最硬的一条证据。
4. 37 个第一人称模型 + 9 个手臂复合模型的逐条读数；材质解析 37/37（147 槽零失败）；契约 `contractOk 25/37` 且 12 个 false **全部**是右手放置锚缺失（`--hand left` 救回 9 个，剩 5 个是模型真没有可用锚）。
5. **N18 的三条产品帧（§3.7）**：`v_scattergun_scout` 与 `v_rocketlauncher_soldier` 确实被画到屏幕上 —— trace 侧 `drawCalls=1 vertices=23169/13428 ranges=2/4 boundSlots=2/4 fallbackRanges=0`，像素侧对 vmoff 负控差 1.04% / 0.688% 且**只落在 8x8 网格的 4–5 格里**。材质解码→GPU 上传（`texturesUploaded=3/6`）与 phong 应用（`phongAppliedRanges=2/4`）也是首次证到。
6. **N18 的三条结构性缺口**（全部有代码行号，不是「未测」）：① **世界相机 FOV 就是 ViewModel FOV**（`native/src/native_renderer_frame.cpp:179` 世界投影读 `settings_.viewModelFov`；`settings_.viewModelFov` 全仓只有 2 个读者），像素后果 18.20% 全画面差；② **团队涂装无输入**（viewmodel 材质 4 个文件里 `skinFamil/teamNumber/m_iTeamNum/paintIndex` **0 命中**，9 个 `c_<class>_arms` 里 7 个有红蓝 family1）；③ **手臂骨合并未实现**（全仓 0 处骨合并代码）。
7. N19 四层预算的常量、读数行、优先级枚举、三例 OOM 的复现口径与判据。
8. N20 只有记账、零绘制的代码事实与 PCF 能力读数。

**不证明：**

> **总纲（本文件的边界，引用本文件前必须读这一条）**
>
> **本矩阵的「资产存在」列全部来自静态读取 VPK，与「产品能把这些资产画出来」是两件不同的事。** 本文件回答了「能不能修」，没有回答「修了没有」；产品在这些格上的实际表现**一条都没有被本文件证明**。任何引用本文件的结论都必须同时引用像素证据，否则只能写 NOT VERIFIED。

1. ~~**任何像素层结论** —— 本轮没有跑产品。~~ **已作废：§2.6.5 用 7 条自己的产品捕获做了 BMP 逐像素比对（A1/B2/B3 三组）。但它们只是「同一次捕获内部」的比较**，没有覆盖 idle/run 那一对，也没有覆盖死亡/跳跃/攻击。
2. **本 HEAD 的容量读数** —— T85 的 `0x8007000E` 601→0 是别的分支/别的 exe 的读数。
3. **death-respawn** —— 本语料 POV demo 里死亡样本为 0（`spectate_probe` 实测）。
4. **jump/fall/land/turn/taunt/attack 的任何实现** —— 全部没有驱动逻辑。
5. **key 命名空间 / 重复注册** —— 未测。
6. **LOD1+** —— ViewModel 侧 101 个 descriptor 全是 lod=0（**无对象**）；玩家/世界侧才是**选择器缺失**（`modelLod` 无读者）。这两句必须分开说。
7. **37 个槽位都能画** —— 产品帧只覆盖 2 个职业各 1 个槽位，剩 35 个未画；那 9 条 `BLOCKED-by-D72` 里包含全部 7 个「有红蓝 family1」的职业模型所需的一半。
8. **任何动画 / 蒙皮** —— 三条帧全是 `skinning=0 boneTableCoverage=0/0 bindPose=1`。
9. **可重复性** —— `cls-scout-repeat` 没跑成，每条帧只有 **1 次观测**。
10. **weapon body 通道** —— 三条帧都是 `weaponDrawCalls=0 weaponFallback=noViewModelEntity`（command 通道下没有 ViewModel 实体可命名），N18 卡里那一条**完全未测**。

**方法学（§2.4 那两处注释冲突的教训，适用于全项目）**：当代码注释与探针实测冲突时，**以实测为准**，并把注释当作待修的文档缺陷登记，不要反过来用注释去解释读数。具体到本批：`native/include/animation_sequence_match.h` 声称 crouch locomotion 只有 pyro/heavy/medic 三个职业、flinch 只有 scout/soldier/pyro/demo 四个职业，实测分别是九个职业全有（spy 走 `crouch_SECONDARY`）与八个职业有 `a_flinch01`（heavy 用 `PRIMARY_flinch` 等四个）。用注释去推断就会漏掉 6 个职业的 crouch 与 5 个职业的 flinch。

---

## 7. 复现命令汇总（全部只读，不需要 host 锁）

```bash
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
DEMO="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/demotest231312314444.dem"
B=native/build-nmake

# 资产清单
$B/vpk_query.exe "$TF" models/weapons/v_models | grep '\.mdl$'      # 37
$B/vpk_query.exe "$TF" models/weapons/c_models | grep '\.mdl$'      # 319
$B/vpk_query.exe "$TF" particles | grep '\.pcf$'                    # 135
$B/vpk_query.exe "$TF" --dump models/player/scout.mdl

# 九职业序列表
for c in scout soldier pyro demo heavy engineer medic sniper spy; do
  $B/model_pose_probe.exe --player-animation-test --tf-root "$TF" \
    --model models/player/$c.mdl --dump-sequences;
done

# 第一人称全槽位
$B/viewmodel_range_probe.exe --tf-root "$TF" \
  --model models/weapons/v_models/v_medigun_medic.mdl --all-lods
$B/viewmodel_material_probe.exe --tf-root "$TF" \
  --model models/weapons/v_models/v_medigun_medic.mdl

# 粒子能力
$B/pcf_capability_probe.exe "$TF"
```

**产品捕获（必须包 `tools/lockrun.sh`，仓库根执行）**：

```bash
# 单条 ViewModel 绘制捕获（§3.7 用的就是这个形态；注意所有交给产品的路径
# 必须是 Windows 形式，POSIX /d/... 会被拼成 "D:/<cwd>//d/..." 并让产品 rc=15 —— D-40）
EXE=native/build-nmake/tf2_demo_native.exe
TF="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
DEMO="D:/TF2_Demo_Player/testdata/demos/bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem"
OUT="D:/_shared/EXEC-2026-11-15/INST4/p1b-n18/draw"
ISO=$(cygpath -m "$(mktemp -d)")
bash tools/lockrun.sh e17-e20-char -- \
  "$EXE" --tf-root "$TF" --audio-device "" --demo "$DEMO" --hidden-window --audio-disabled \
    --settings-file "$ISO/x-settings.ini" --ui-settings-file "$ISO/ui.ini" \
    --capture-frame "$OUT/cls-scout.bmp" --capture-tick 0 --metrics-file "$OUT/cls-scout.csv" \
    --viewmodel-fov 90 --start-paused \
    --viewmodel models/weapons/v_models/v_scattergun_scout.mdl --viewmodel-render command
```

**门禁（同样要锁）**：`bash tools/lockrun.sh e17-e20-char -- bash animation-drive-states-check.sh`、`… -- bash attach-weapon-budget-check.sh`。

---

## 8. 本轮不落盘的东西与本文件的关系

本文件是本批四卡的**唯一汇总交付**。证据原件在 `D:/_shared/EXEC-2026-11-15/INST4/`：`p1a-n17/`（N17 的 census.awk、bmpdiff/regiondiff/crop 三个诊断脚本）与 `p1b-n18/`（37 项普查产物 `vmodels.list` `all-lods.txt` `player-lods.txt` `contract-37.tsv` `contract-handleft.txt` `material-37.txt` `sequences-37.txt` `skinfamily-37.txt` `armsfamily-fixed.txt`，以及 `draw/` 下三条产品帧 + trace + `diff-readings.json`）。探针输出若未逐条落文件，可照 §7 原样重跑，读数是确定性的（同一 exe + 同一资产）。

### 8.1 `class-pose-family-check.sh`：**D53 已修，本机复跑 GREEN**（本节先给当前状态，再保留初稿那次 RED 的完整诊断作为 D53 的立案材料）

**当前状态（2026-11-15 22:20，本人复跑）**：

```
POSE-FAMILIES classes=9 crouchClasses=9 flinchSequences=14 sampled=40 decoded=40 failed=0
POSE-FAMILIES=PASS
rc=0
```

- 跑法：`T62_POSEFAMILY_OUT=/d/_shared/EXEC-2026-11-15/INST4/p1a-n17/t62-posefamily bash class-pose-family-check.sh`（Git Bash，只读模型，**不需要 host 锁**），日志 `D:/_shared/EXEC-2026-11-15/INST4/p1a-n17/posefamily.log`。
- 修在哪：`class-pose-family-check.sh:137` 现在是 `stats=$(tr -d '\r' < "$frame_log" | awk '...')`，**且这个修复已在 HEAD 里**（`git show HEAD:class-pose-family-check.sh` 同样命中 :137；工作树对该文件无改动）。提交是 **`51e44216` `gates(fix) + probe(mf) + docs: D53/D55/D56/D59 门禁修复...`** —— 不是我这个实例改的，本实例**一行代码都没改**。
- 结论：**`evidence/t62-matrix/pose-families/` 的 PASS 可以重新被引用为 crouch/flinch 数据的证据**（D53 的禁令随修复解除），而且它是**首尾一致**的：当年 `decoded=40 failed=0`、今天同样 `decoded=40 failed=0`。差异确实出在「读的一侧」，被 `tr -d '\r'` 消除。
- 每类的家族计数（本轮 PASS 的 CLASS 行）：`scout sequences=306 crouchCandidates=30 flinchCandidates=1`、`soldier 361/23/1`、`pyro 270/25/1`、`demo 276/27/1`、`heavy 259/29/4`、`engineer 351/47/3`、`medic 213/24/1`、`sniper 240/39/1`、`spy 274/32/1`。抽样例（含 arm-layer 命名的样本，证明门禁也会采到它们）：`SAMPLE spy crouch seq=30 label=armslayer_melee_Crouch_swing distinctPoses=4`、`SAMPLE spy crouch seq=31 label=bodylayer_Melee_Crouch_Swing distinctPoses=4`、`SAMPLE demo flinch seq=28 label=a_flinch01 distinctPoses=4 maxDelta=129.368073`。

以下是**初稿那次 RED 的完整诊断**，保留作为 D53 的立案材料与「注释/换行不可信」的通用教训：

初稿尝试跑这个门禁（它**只读模型文件、不占 host 锁**，所以在上锁期间也能跑），结果是：

```
POSE-FAMILIES classes=9 crouchClasses=9 flinchSequences=14 sampled=40 decoded=0 failed=49
POSE-FAMILIES=FAIL (see the FAIL lines above)
rc=1
```

**这个 RED 不能读成「crouch/flinch 数据坏了」。** 逐帧追下去，失败点是门禁自己的 `finite` 判定：

- 门禁在 `class-pose-family-check.sh:130` 用 `awk` 统计每个采样帧，判据是 `if ($i ~ /^finite=/) { split($i, a, "="); if (a[2] != "1") bad = 1 }`。
- `model_pose_probe` 的输出是 **CRLF**：`grep -m1 '^animpose ' pyro-crouch-seq15.txt | od -c` 的结尾是 `f i n i t e = 1 \r \n`，整个日志 24 个 `\r`。
- 于是 `a[2]` 实际是 `"1\r"`，永远不等于 `"1"`，`bad` 恒为 1，**每一帧都被判成非有限**。
- 把同一个文件先 `tr -d '\r'` 再跑同一段 awk：`0 107.405968 4` → `1 107.405968 4`，即 `finite=1` 且 4 个不同 checksum。**模型数据是好的。**
- 抽查样本（CR 剥离后）：pyro `AttackCrouch_PRIMARY` seq15 四帧 `finite=1`，checksum `5569d8ca81eb428a` / `f527d48c5aef9073` / `86f1d8ffe1de69f8` / `30445b74466c0fdd`（四帧四个姿态，不是静态参考姿势）；pyro `a_flinch01` seq13 四帧同样 `finite=1`、四个 checksum。

**D53（lead 裁决的正式命名：门禁在 CRLF 输入上静默判假红）**：仓库里的历史证据 `evidence/t62-matrix/pose-families/` 记的是 `decoded=40 failed=0` / `POSE-FAMILIES=PASS`，而它的采样日志 `scout-crouch-seq10.txt` 同样**含 24 个 `\r`**。也就是同一份门禁、同一种换行、当年 PASS、现在 RED。我没有查出差异来源（可能是当年的 awk 实现或执行环境不同），**不下结论**。给后来者的建议：要么让门禁在 awk 之前统一 strip CR，要么让探针按二进制写出 LF；在那之前，这个门禁的 RED 与 GREEN **都不能**作为 crouch/flinch 数据的证据。
**D53 的完整定义（不是「一个 if 写错了」）**：

> **同一门禁对同一份输入在不同时间给出相反结论，且仓库里没有任何记录说明门禁改过。** 在查清「当年那版判定长什么样」之前，`evidence/t62-matrix/pose-families/` 的 PASS **不可再被引用为 crouch/flinch 数据的证据**。

它与 D52 同族：D52 是「`> file` 启动即截断，读到合法的空文件」，D53 是「awk 拿 `"1\r"` 跟 `"1"` 比，永远不等」。共同点是**读的一侧对环境里的换行/存在性做了未经证实的假设**。

**归档事实（本轮实测，供 D53 立案）**：

- `evidence/t62-matrix/pose-families/` **被 git 跟踪**（不是被忽略的产物目录）：`git ls-files` 命中 **50 个文件**，`git check-ignore -v` 无输出。
- 该证据路径的提交史 **1 次**：`0efd844`（2026-11-04 18:34:06 +0800，`test(t62): gate the crouch and flinch families and bank the freeze-run evidence`）。
- 门禁脚本 `class-pose-family-check.sh` 的提交史 **也是 1 次，且是同一次提交** `0efd844`。
- **结论：在本仓库内，门禁脚本自引入以来没有被改过。**「门禁被人改过却没留下记录」这一支，从文件历史看不成立；差异来源更可能在执行环境一侧。
- 相关旁证：探针 `native/tools/model_pose_probe.cpp` 共 7 次提交（2026-10-06、10-11、10-25、10-28、11-04、11-07 两次），**门禁提交之后还有两次**（`931588b` 2026-11-04 16:54、`e042bcf` 2026-11-07 14:33、`624114f` 2026-11-07 17:04 中的后两次）。本机当前 awk 是 GNU Awk 5.4.1；**当年捕获那 50 份日志时的 awk 版本无从查证**，「版本差异」只是候选解释，**没有证据能指名，登记 OPEN**。

**修复归属（lead 裁决）**：修法很简单（门禁里先 `tr -d '\r'`，或探针统一写 LF），但它是**共享 gate helper**，动了会让所有跑过它的证据都要重跑。按任务清单的串行规则属「同一公共 gate helper」独占写域，**等 N12-A 提交完、放锁后单独派**；本轮不改任何代码。
**写入侧已定位（本轮新增，把 OPEN 的范围缩小了一半）**：`native/tools/model_pose_probe.cpp` 里 `grep -nE 'fopen|_setmode|O_BINARY|fwrite|freopen'` **0 命中** —— 探针只把结果 `std::printf` 到 stdout，由门禁的 shell 重定向写成 `.txt`；MSVC 的 stdout 默认是文本模式，会把 `\n` 翻成 `\r\n`。也就是说 **CR 来自探针的 stdout，不是日志文件被谁改过**，这同时解释了为什么 50 份历史日志和今天新跑的日志都带 `\r`。

门禁提交 `0efd844` 之后，探针又改过两次（`e042bcf` 2026-11-07 14:33、`624114f` 2026-11-07 17:04），**两次都没有碰换行或输出模式**（diff 里只增改了 census 相关的 `std::printf` 行）。因此「当年产物不同」这一支也被排除：**当年那份日志与今天这份日志在换行字节层面同源**，差异只能出在**读的一侧**（当时那次运行用的 awk 实现或调用方式）。这一支仍然**没有证据能指名，登记 OPEN**。

（我在证据目录里放了一个只用于诊断的一次性包装 `D:/_shared/EXEC-2026-11-16/e17-e20-char/tools/awk`，功能是先把文件参数里的 CR 去掉再交给 `/usr/bin/awk`。**它不是产品的一部分，也没有被门禁真正调用成功过**——把它记在这里只是因为上面的对照读数是用它复现的。）

**下一步（当前状态：N17 / N18 已推进，N19 / N20 待锁）**：

1. ~~跑 `class-pose-family-check.sh`~~ **已完成（§8.1，PASS）**。
2. ~~跑 `animation-drive-states-check.sh`~~ **跑了 7/9 条后因锁被抢而中止（§2.6.6）**。要拿到门禁自身的 PASS/FAIL，需要**在锁真正独占的条件下重跑全部 9 条**（约 40 分钟），并先修 D72（`verify-lock.sh` 的 pid 命名空间缺陷），否则同样的事会再发生一次。**本轮不出这个 PASS。**
3. ~~N18 产品帧~~ **3 条已落（§3.7），9 条 `BLOCKED-by-D72`**。补齐这 9 条后可把 §3.6 的「像素已证 2/37」推进到 9/37，并首次覆盖有红蓝 family1 的模型（medic / soldier / pyro 等），从而把「团队涂装画的是红队」从**代码结构缺失**升级成**像素级实锤**。
4. 跑 `attach-weapon-budget-check.sh` → 补 §4.5 的「本 HEAD」读数与 exe sha256（**仍未跑**）。
5. 若拿到 STV 语料，补 death-respawn 的观测窗口（**仍未**）。
6. **`medic crouch` 冻结（§2.6.3）是本轮唯一一条画面级人物动画缺陷候选**，按卡的「只在复现后修」规则已经复现，但修复要动 `chooseCrouchLocomotionSequence`（共享 `animation_sequence_match.h`），属公共写域，**本轮不动代码，交给派单**（已登记 D73）。
7. **本轮新登记的三条 N18 结构缺口**（§3.4.1 / §3.6）都要动别人正在改的文件，**一律不动代码、只交派单**：① 世界 FOV 与 ViewModel FOV 解耦（`native_renderer_frame.cpp:179` + `RenderSettings` 新字段，属 renderer 写域）；② ViewModel 团队涂装（`viewmodel_material.cpp` 需要 skin-family 参数，属动画/model 写域）；③ 手臂骨合并（全新功能，未实现）。

**排期状态（lead 裁决，仍未变）**：`animation-drive-states-check.sh` 与 `attach-weapon-budget-check.sh` **都要 host 锁**。lead 已明确「**先不排，等 N12-A 提交完统一放锁**」，且在 D72 修好前由 lead **手工分时**方可跑产品。所以 N19 本 HEAD 读数与 N20 可复现格**保持 NOT VERIFIED / 无**；N17 因本轮已用自持锁跑了 7 条，**像素格不再是「未证」**（§2.6.5），N18 有 3 条真帧（§3.7），但两者的门禁级 PASS 仍缺。

