# N17 / N18 / N19 / N20 覆盖矩阵（2026-11-15）

- 执行：`e17-e20-char`（N17/N18/N19/N20 责任子智能体）
- 工作树：`D:/TF2_Native_Worktrees/t105-e5`　分支：`candidate/e5`　HEAD：`a901ece`
- 证据目录：`D:/_shared/EXEC-2026-11-16/e17-e20-char/`
- 状态词汇：**PASS / NOT VERIFIED / UNSUPPORTED**（`docs/T85-SUPPORT-SCOPE.md` 只认这三种），外加本批 N20 裁决指定的 **OPEN（未实现）**。PASS 只对具名门禁成立，不等于可发布。

**阅读约定（这是本文件的硬规则）**：每一格要么给出「文件:行号」或「探针命令 + 实测输出」，要么写「无」/「未证」。**全文不出现「应该有」。**
行号一律取自 `git show HEAD:<file>`：工作树的 `native/src/main.cpp` 正被 n12-camera 修改（现 13093 行 / 789666 B，HEAD 为 12912 行），工作树行号会漂，HEAD 行号稳定。

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
| **N18** 第一人称与武器全槽位 | 有：`models/weapons/v_models/` **37 个**第一人称 `.mdl`（`v_hm_watch` 在子目录）+ `models/weapons/c_models/` **319 个**第三人称 `.mdl`（含 9 个 `c_<class>_arms.mdl`） | 有：`viewmodel_range_probe --all-lods` 37 个全部 `resolution=found-vpk` / `vtxOutOfBounds=0` / `meshBuilt=1`；材质解析 `VmtParser::resourcePath`（`native/src/vmt_material.cpp:435`）对 `v_medigun_medic` 实测 **7/7 槽 status=ok** | **部分**：网格可建、材质可解析；**动画层 refused**（`--viewmodel-apply-animation` 在两份钉住录制上 changes nothing measurable，`native/src/main.cpp:2018-2026`）；LOD1+ 未实现且资产本身只有 LOD0 | 见 §3 |
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
| airwalk | `airwalk` | 13 | 27 | 16 | 15 | 11 | 18 | 7 | 9 | 9 |
| turn | `turn` | **0** | **0** | **0** | **0** | **0** | **0** | **0** | **0** | **0** |
| crouch（首 token = crouch） | `crouch_*` | 有（crouch_PRIMARY 46 帧 / crouch_walk_PRIMARY 27 帧） | 有（51/27） | 有（61/27） | 有（34/27） | 有（61/31） | 有（47/27） | 有（31/27） | 有（41/27） | 有（crouch_SECONDARY 41 / crouch_MELEE 41） |
| jump_start | `jump_start` | **0** | 3 | **0** | **0** | **0** | **0** | 5 | **0** | **0** |
| jump_float | `jump_float` | 1 | 4 | **0** | **0** | **0** | **0** | 6 | **0** | **0** |
| fall | `fall` | **0** | **0** | **0** | **0** | **0** | **0** | **0** | **0** | **0** |
| land | `land` | 8 | 9 | 12 | 12 | 7 | 20 | 5 | 14 | 14 |
| flinch | `flinch` | 1 | 1 | 2 | 1 | 4 | 3 | 2 | 2 | 2 |
| death | `death` | 3 | 3 | 2 | 3 | 3 | 3 | 3 | 3 | 3 |
| ragdoll | `ragdoll` | 2 | 2 | 2 | 2 | 2 | 2 | 2 | 2 | 2 |
| taunt | `taunt` | 59 | 78 | 68 | 70 | 66 | 71 | 64 | 23 | 60 |
| attack | `attack` | 16 | 23 | 26 | 10 | 33 | 14 | 21 | 12 | 4 |

**从这张表能直接读出、且必须写进结论的三件事：**

1. **`turn` 与 `fall` 在九职业里都是 0**。TF2 的角色表没有转身序列，也没有名为 fall 的下落循环；N17 的 turn 行只能用「yaw 变化 + run/idle 切换」间接表达，fall 行只能用 `jump_float` 替代。**任何「转身动画」「下落动画」的说法都是编的。**
2. **`jump` 只有 soldier(3+4) 与 medic(5+6) 有完整家族**；scout 只有 1 条 `jump_float`；pyro / demo / heavy / engineer / sniper / spy **一条跳跃序列都没有**。所以 jump 行**必须按职业分列**，不能写「九职业 jump 通过」。
3. **`land` 九职业全部有数据（5–20 条），但产品侧没有任何 land 逻辑**（HEAD `main.cpp` 里 `\bland\b` 只命中注释）。数据有、产品无 —— 这是「数据齐 ≠ 已实现」的典型格。

### 2.2 产品侧：到底读了哪些服务器字段（四层拆开）

| 层 | idle / moving | crouch | flinch | death | jump/fall/land/turn/taunt/attack |
|---|---|---|---|---|---|
| **① 输入事件** | `m_vecVelocity` 推导的速度经 `advanceAnimationClock`（`native/include/animation_clock.h`；状态只有 `Idle=0` / `Moving=1`，:43） | `m_fFlags` bit1（FL_DUCKING）**或** `m_bDucked`/`m_bDucking`/`m_flDucktime>0`（HEAD `main.cpp:10936-10940`）。注释记了实测反例：mid-duck 身体 `m_fFlags=131329`（bit1 已清）但 `m_bDucking=1`、`m_flDucktime=940`，只用 flags 会漏 | `m_iHealth` 在相邻两个 scene tick 上下降且身体存活（HEAD `main.cpp:10907-10931`）。**刻意不用 CTEPlayerAnimEvent**：census 发现该事件没有可映射值（109 个 `player_death` vs 唯一候选值 545 次出现，且事件 player 字段从不等于 game event 的 userid） | `m_lifeState` 仅用于 spectate/可绘制判定（HEAD `main.cpp:474/10232/10641`），**没有接成死亡动画** | **无输入** |
| **② 选序** | `choosePlayerAnimationSequence`（`main.cpp:315`）→ `chooseSequenceByNeedles` | `chooseCrouchLocomotionSequence` → 失败退 `chooseCrouchPoseSequence`（`main.cpp:10859/10863`） | `chooseFlinchSequence`（`main.cpp:10871`，实现在 `animation_sequence_match.h:439`） | 无 | 无 |
| **③ 骨骼运动** | 有：`skeleton_skin_probe`（scout）`bones=78 animBones=76 mappedBones=76 conventionError=3.05176e-05 conventionBone=37 identityWorst=2.47955e-05 skinLands=3.8147e-06 skinReversed=24.942 sampledSequences=331 motionDelta=185.935 motionSequence=layer_taunt_chicken` | 未单独测（同一采样路径） | 同左 | 未测 | 无 |
| **④ 像素** | **未证**（本环境无产品运行窗口） | **未证** | **未证** | **未证** | **未证** |

### 2.3 逐动作结论

| 动作 | 数据 | 选序 | 骨骼 | 像素 | 卡片结论 |
|---|---|---|---|---|---|
| idle | 有 | 有 | 有 | 未证 | **NOT VERIFIED**（数据+逻辑成立，像素未测） |
| run | 有 | 有 | 有 | 未证 | **NOT VERIFIED**（同上） |
| crouch | 有 | 有 | 未单测 | 未证 | **NOT VERIFIED** |
| flinch | 有 | 有 | 未单测 | 未证 | **NOT VERIFIED**（T84 门禁在本仓库存在：`animation-drive-states-check.sh`，但本 HEAD 未跑） |
| land | **有** | **无** | 无 | 无 | **OPEN（未实现）** |
| jump | 仅 2 职业有 | **无** | 无 | 无 | **OPEN（未实现）** |
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
   - 这两条会实际影响判据：`main.cpp:10982` 给「没有 crouch locomotion 的职业」记 `no-crouch-locomotion` 并把 `fallback` 计数加一；按实测数据这个 fallback **可能永远不会触发**。这需要一次产品运行才能坐实，本轮记 **未证**。
3. **`walk` 家族九职业全 0**：移动只能靠 `run_`（与 `airwalk`）。`kMovingNeedles = {"run","walk","move"}` 里的 `walk` 在本资产上只可能匹配到 `crouch_walk_*`（而那不是移动主选）或 `airwalk`（T61.1 已用 whole-token 排除）。

### 2.5 N17 的两项历史核查（都已在本分支）

- `duplicate entity ID` 与 `walk/airwalk` 相关提交 **c85fdfc**（T85 T51: 8.3-2 player-model assert + precedence fixture + walk/airwalk/reload gap evidence）、**29e27ca**（Merge branch 'task/t61-airwalk' into p0-entity-protocol）、**a7b3196**（T61: the animation gate proves its own restoration）。
- `git merge-base --is-ancestor <c> HEAD` 三者**全部 YES**；`git branch --contains 29e27ca` 含 `* candidate/e5`。**结论：不需要从其他分支搬补丁。**

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

### 3.5 动画层：这是 N18 真正的缺口

- `--viewmodel-apply-animation` 在两份钉住录制上「changes nothing measurable」（`native/src/main.cpp:2018-2026` 原文），每帧仍被 refused、画 bind pose。
- `--viewmodel-sequence <n>` 是**输入覆盖，不是功能**（`main.cpp:2004-2012` 注释）：钉住录制上 ViewModel 实体只带 sequence 1/29/30，而它指的模型只能摆 sequence 0。
- 产品**确实读** `m_nSequence`：`native/src/entity_model.cpp:409` `readIntSlot(state, "m_nSequence", hasSequence)`；`native/src/animation_binding.cpp:444` 无序列 → `MissingSequence` 拒绝，`:486` 用序列号采样；`native/src/viewmodel.cpp:504/508/531` 三处诊断文本。
- 而**玩家身体**走本地标签匹配，因为「the demos this product can play carry no per-player m_nSequence or m_flCycle」（`native/src/main.cpp:2029-2031` 原文）。**两条路并存：ViewModel 走服务器序列号，玩家身体走本地标签。**

### 3.6 结论

- 槽位（model / arms / companion / team）：**数据层 PASS**（37 个第一人称 + 319 个第三人称全部可读、可建网格；九职业手臂复合模型齐）。
- material：**解析器 PASS（7/7）**；**GPU 上传未证**。
- sequence（第一人称动画）：**NOT VERIFIED** —— 通道被 refused，画的是 bind pose。
- LOD1+：**UNSUPPORTED** —— 资产里只有 LOD0（D34 记为 legacy 限制）。
- 像素：**未证**。身体像素不代替武器像素，本轮两者都没测。

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
2. 九职业序列表的实际家族分布（哪些动作有数据、哪些 0 条）。
3. 产品侧四个动作（idle/moving/crouch/flinch）有真实的输入→选序→骨骼链路，其余七个动作没有。
4. 37 个第一人称模型 + 9 个手臂复合模型的逐条读数；材质解析对真实资产 7/7 成功。
5. N19 四层预算的常量、读数行、优先级枚举、三例 OOM 的复现口径与判据。
6. N20 只有记账、零绘制的代码事实与 PCF 能力读数。

**不证明：**

> **总纲（本文件的边界，引用本文件前必须读这一条）**
>
> **本矩阵的「资产存在」列全部来自静态读取 VPK，与「产品能把这些资产画出来」是两件不同的事。** 本文件回答了「能不能修」，没有回答「修了没有」；产品在这些格上的实际表现**一条都没有被本文件证明**。任何引用本文件的结论都必须同时引用像素证据，否则只能写 NOT VERIFIED。

1. **任何像素层结论** —— 本轮没有跑产品（host 锁被 n12-camera 占用），没有任何帧被渲染或比对。
2. **本 HEAD 的容量读数** —— T85 的 `0x8007000E` 601→0 是别的分支/别的 exe 的读数。
3. **death-respawn** —— 本语料 POV demo 里死亡样本为 0（`spectate_probe` 实测）。
4. **jump/fall/land/turn/taunt/attack 的任何实现** —— 全部没有驱动逻辑。
5. **key 命名空间 / 重复注册** —— 未测。
6. **LOD1+** —— 资产本身只有 LOD0。

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

**需要产品窗口的（等 host 锁）**：

```bash
bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run e17-e20-char \
  bash attach-weapon-budget-check.sh
bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run e17-e20-char \
  bash animation-drive-states-check.sh
```

---

## 8. 本轮不落盘的东西与本文件的关系

本文件是本批四卡的**唯一汇总交付**。证据原件在 `D:/_shared/EXEC-2026-11-16/e17-e20-char/`（目前只有 `n08-n10-maps` 旁目录）。探针输出都是标准输出，未逐条落文件；需要时可照 §7 原样重跑，读数是确定性的（同一 exe + 同一资产）。

### 8.1 一条必须先说的门禁读数：`class-pose-family-check.sh` 在本机 **RED**，但原因不是模型数据

本轮尝试跑这个门禁（它**只读模型文件、不占 host 锁**，所以在上锁期间也能跑），结果是：

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

**下一步（等 lead 通知锁空）**：

1. 跑 `attach-weapon-budget-check.sh` → 补 §4.5 的「本 HEAD」读数与 exe sha256；
2. 跑 `animation-drive-states-check.sh` → 补 §2.3 四个 NOT VERIFIED 格的像素层，并顺带核实 §2.4 第 2 条（`no-crouch-locomotion` fallback 是否真的不触发）；
3. 若拿到 STV 语料，补 death-respawn 的观测窗口。

**排期状态（lead 裁决）**：上面两个门禁**都要 host 锁**，当前锁很紧（n12-qa 在跑 N12-A 六帧，n05 与 ui-d 在排队）。lead 已明确「**先不排，等 N12-A 提交完统一放锁**」。所以本文件在产品侧的三格（N17 像素层、N19 本 HEAD 读数、N20 可复现格）**在本轮保持 NOT VERIFIED / 无**，不因为等待而改动结论。

