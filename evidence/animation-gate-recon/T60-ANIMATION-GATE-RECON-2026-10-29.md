# T60 后的动画门禁对账（第 22 / 23 / 24 步）

**任务**：T60 的 `AnimationBindingCache` 规则（扫描 companion 候选、按序列名绑定数量选源、companion 无同名有效序列时拒绝采样）与 `verify-all.sh` 第 22/23 步的验收用例冲突。本轮保留产品语义，修正门禁；不改产品动画解析器。

**结论**：第 22 步换了正样本并把 `ref` 变成具名负控（57 → 62 断言）；第 23 步四项读数逐条归因后更新期望、并修好一个死锚点（pin 仍 154）；第 24 步三条快照 pin 换成派生不变量（pin 仍 125）。三个 gate 的 forward 与 mutation 全绿，T60/T61 focused gate 全绿。

## 1. 身份

| 项 | 值 |
|---|---|
| 树 | `D:/TF2_Native_Test`（主树，未建 worktree） |
| 分支 | `p0-entity-protocol` |
| 开工 HEAD | `a947e28`（T60 收口三提交之上） |
| 源码（未改动） | `main.cpp 57cc0ac3…`、`native_renderer.cpp 3cffe12d…`、`animation_binding.cpp 11d90e63…`、`viewmodel.cpp 232dc634…`（sha256 前缀） |
| 产品 exe | `0fe7ac99…`，2,657,280 B，链接于 12:54 |
| 探针 | `model_robustness_probe` / `model_pose_probe` / `entity_overlay_probe` / `weapon_animation_probe` / `viewmodel_*` 于 13:15 全量重建 |
| demo（第 22/24 步与第 23 步 pov） | `bb841c6d…cp_snakewater_final1.dem` 51,834,617 B `308cc145…`；POV `autorecord_2026-07-02_13-26-46.dem` `c8a58501…` |
| VPK | `tf2_misc_dir.vpk` 5,402,349 B `b1e36819…`，mtime 2026-10-17 09:48 |
| 主机锁 | 每条命令由 `hostlock run` 独占，结束时 `FREE`；`product_processes=0` |

## 2. 复核：产品规则与三个 gate

`native/src/animation_binding.cpp` 的现行规则（0819fce 起）：

* 候选 = 所有声明里名字含 `_animations.mdl` 的 include，加命名约定的 companion；
* 每个候选按“能按**序列名**绑定多少个基础序列”打分，取最高者；平手保留声明顺序；
* 采纳时**基础模型的序列表仍是索引空间**，逐条按名映射 `animDesc`；companion 没有同名有效序列的条目置 -1，采样时以 `sequence has no animation binding` 具名拒绝，不会错播邻近序列；
* `!baseDecoded` 时才整表采纳 companion。

实测（`model_robustness_probe`，重建后复核）：

```
models/player/scout.mdl            seq=306 animPath=models/player/scout_animations.mdl      separate=1 animSeqs=1012 boundSeqs=303
models/weapons/c_models/c_sniper_arms.mdl  seq=43  animPath=models/weapons/c_models/c_sniper_animations.mdl separate=1 animSeqs=88  boundSeqs=38
```

即 scout 有 3 条基础序列按名绑不上（`ref` 是其中之一），arms 有 5 条绑不上。这正是第 22 步 `ref` 变红的机制，也是第 23 步读数移动的机制。

## 3. 第 22 步：换正样本，`ref` 转负控

### 3.1 为什么必须换

旧正样本是 `models/player/scout.mdl --model-sequence 0`，即基础表索引 0 的 `ref`。T60 之后它**按名没有 companion 片段**，产品正确地拒绝：

```
$ tf2_demo_native --model models/player/scout.mdl --model-sequence 0 --model-tick 30
gpu: model skin source=identity bones=78 uploaded=78 skinning=1 seq=0 frame=0/0 reason=sequence has no animation binding
```

一个被正确拒绝的序列不能当“有效动画正样本”，否则门禁只能用放宽阈值或删断言变绿。

### 3.2 新正样本（有据可查）

`--model-sequence 212`，标签 `run_PRIMARY`：

```
model_pose_probe --player-animation-test: sequences=306 movingSeq=212 movingLabel=run_PRIMARY poseFrames=49
tf2_demo_native --model ... --model-sequence 212 --model-tick 30:
gpu: model skin source=animated bones=78 uploaded=78 skinning=1 seq=212 frame=30/49 reason=run_PRIMARY
```

### 3.3 改动（`weapon-animation-check.sh`）

* `MODEL_SEQUENCE` 0 → 212；新增 `MODEL_UNBOUND_SEQUENCE=0`、`MODEL_FRAMES=49`；
* 新增一次产品运行 `unbound`，与 identity/control/animated 同模型同 tick；
* 新增 5 条断言：正样本是**多帧 companion 片段**（`49`）、`ref` 不被播放（`source=identity`）、`ref` 采样零帧（`0/0`）、`ref` 的具名原因（`sequence has no animation binding`）、`ref` 仍上传全部骨骼（78）；
* 复审补丁（2026-10-29 第二轮）再加 3 条**样本身份**断言：正样本 `seq=212`、`reason=run_PRIMARY`，负控 `seq=0`。理由见 §3.5；
* WAVE 100 帧样本保留，仍是 tick 前进与循环的唯一见证（不拿单帧冒充时间动画）；
* 新增 `assert_contains` 辅助函数（与本仓其他 gate 同名同义）。

### 3.4 结果

| 运行 | 读数 | 结论 |
|---|---|---|
| forward（新二进制） | **65 OK / 0 FAIL** | `WEAPON-ANIMATION=PASS` |
| `--mutation` | 具名红：`no witness model at tick …`、`uploaded pose changes by the last tick = 0` | `MUTATION-CAUGHT=PASS` |
| `--mutation-model` | 具名红：`identity skinning changes nothing outside the measured edge mask` | `MUTATION-CAUGHT=PASS` |
| `--mutation-identity` | 同上（silhouette 规则本身抓住） | `MUTATION-CAUGHT=PASS` |

各节断言数 27 / 6 / 5 / **20** / 6 / 1 = 65（旧 12+4FAIL）。identity-vs-animated 图像断言现在真的会红：`the silhouette rule did not reject a real pose change` 由 FAIL 转 OK，`a sampled sequence moves the geometry` 同样转 OK。

### 3.5 复审缺口与补丁：样本身份

复审指出的问题成立：本轮第一版的 section 4 只断言了 `source=animated`、`uploaded == bones` 和分母 `49`，**没有任何断言证明产品读到的就是 `seq=212` / `run_PRIMARY`**；负控同样没有断言产品读到的是 `seq=0`。后果是：换成另一个 49 帧片段（或另一个恰好也按名绑上的序列），或负控换成另一个未绑定序列，照样能过。

补丁（`weapon-animation-check.sh`，仍是 3 条断言，期望值写**字面量**而不是 `$MODEL_SEQUENCE` / `$MODEL_FRAMES`，这样改常量不会把期望一起带走；产品没打印的字段读成空值，`assert_eq` 直接 FAIL）：

```
assert_eq "the animated run read the intended sequence index"   "$(field "$ANIMATED_LINE" seq)"    "212"
assert_eq "the animated run resolved the intended clip label"   "$(field "$ANIMATED_LINE" reason)" "run_PRIMARY"
assert_eq "the refusal is about the intended sequence index"    "$(field "$UNBOUND_LINE" seq)"     "0"
```

实测（新 run-id `.scratch/t60-s22-identity-20261029/`）：

```
  OK   the animated run read the intended sequence index = 212
  OK   the animated run resolved the intended clip label = run_PRIMARY
  OK   the refusal is about the intended sequence index = 0
65 OK / 0 FAIL / WEAPON-ANIMATION=PASS / rc=0
```

第 23、24 步未改动，pin 保持 154、125；本轮没有再运行它们（上一轮的 154 OK / 125 OK 仍是它们的最近读数）。

## 4. 第 23 步：四项读数逐条归因

| 断言 | 旧期望 | 实测 | 归因 | 处置 |
|---|---|---|---|---|
| `pov refusal sequence` | `29` | `28` | 该字段是 demo 实体自己的 `m_nSequence`；一次运行只采到 2 条观测、都是 28。旧 `29` 是“更长运行里最后一帧”的快照 | 换成不变量 `sequence == entitySequence`（两字段来源不同，能抓 override/remap） |
| `pov companion frame count` | `51` | `31` | T60 前后**采纳的表不同**：旧规则整表采纳 companion，索引 28 指向 companion 第 28 项（51 帧）；现规则保留基础索引空间，索引 28 = 基础标签 `draw`，按名绑到 companion 的 `draw`（31 帧）。同一 demo、同一实体、同一 companion 文件 | 更新为 31，并改为**取整段运行的帧数最大值**（末行会随运行长度漂移） |
| `pov history frame count` | `61` | `1`（末行） | 61 帧片段仍在（同一次运行里 486 条观测是 61 帧，1003 条 31 帧，248 条 1 帧），只是末行落在 1 帧片段上 | 改为最大值，期望仍 `61`——数值不变，去掉了采样脆弱性 |
| `matcha pairing says nothing was there` | 含 `the snapshot holds no ViewModel entity` | `no ViewModel entity names the view entity in m_hOwner` | `entity_model.cpp:400-403` 是二分支文案：扫描到 0 个 ViewModel 实体说前者，扫描到但都不归 view entity 所有说后者。本录制现在走第二支（`entities=0` 指“选中 0 个”，扫描计数为 0 之外的另一计数） | 断言改成在册的那一支；`reason` 与 `entityRule` 仍是两条不同规则的具名拒绝 |

`pov refusal sequence` 的旧值属于“把快照当语义”；`companion/history` 的帧数是**真实来源变化**（同一标签、不同表）。没有一处是把 `source=identity`、缺 pose 或像素无变化改写成 PASS。

### 4.1 顺带修好的死锚点

第 23 步 `--mutation` 原来锚定 `      if (nameBound > boundInBase(anims)) {`，T60 改成分支后该串出现 0 次，运行以 `FATAL: the mutation did not land` 退出（rc=1），根本没在变异。新锚点：

```
  } else if (!bestCompanion.empty() && bestNameBound > boundInBase(anims)) {
→ } else if (false && !bestCompanion.empty() && bestNameBound > boundInBase(anims)) {
```

语义不变：常量 false 使 companion 永不采纳。**第一次失败的那轮在报错前已改写 `native/src/viewmodel.cpp` 且未还原**，本轮用 `git checkout -- native/src/viewmodel.cpp` 从索引恢复并核对哈希（`e23e4d5c…` = `HEAD:…`）。这是本轮唯一一次 `git checkout --`，对象是一个已被变异脚本改写的文件，不含任何用户改动。

### 4.2 结果

| 运行 | 读数 | 结论 |
|---|---|---|
| forward | **154 OK / 0 FAIL** | `VIEWMODEL-ANIMATION=PASS` |
| `--mutation`（重锚后） | `MUTATION-CAUGHT=PASS`（模型证明与 companion 采纳被同时掐断，全部控制项保持） | 通过 |

## 5. 第 24 步：三条快照 pin 换成派生不变量

失败是 `witness-a/b/c sequence = 28/30/1, expected 29/1/30`。根因不在产品：**witness tick 是运行期推导的**，两次运行给出不同 tick：

```
本树      derived witness ticks: 0 507 1731
2026-10-22 derived witness ticks: 0 814 1698
```

tick 由运行中的序列变化时刻推导，tick 变了，落在 tick 上的序列自然变。三条绝对值 pin 钉的是“推导结果的快照”。

处置（计数不变，125）：

* `witness-a`：`sequence == entitySequence`（抓 override/remap）；
* `witness-b` / `witness-c`：与前一个 witness 的序列**不同**（证明三个抓帧是三个不同取样点）。

| 运行 | 读数 | 结论 |
|---|---|---|
| forward | **125 OK / 0 FAIL** | `VIEWMODEL-COMPANION=PASS` |
| `--mutation` | `MUTATION-CAUGHT=PASS`（6 个缺陷各自具名红，控制项保持） | 通过 |

## 6. T60 / T61 focused 回归

| gate | 首次 | 复跑（重建探针后） |
|---|---|---|
| `player-model-check.sh`（T60） | 165 OK + **2 FAIL**：`STALE-BINARY overlay/robust … older than material_slot_resolution.h` | **167 OK / 0 FAIL**，rc=0 |
| `player-animation-check.sh`（T61） | 231 OK + **1 FAIL**：`STALE-BINARY probe=model_pose_probe older than model_pose_probe.cpp` | **232 OK / 0 FAIL**，`PLAYER-ANIMATION-CHECK=PASS` |
| `player-animation-pixels.sh`（T61） | **28 OK / 0 FAIL** | 未复跑（已 PASS） |

三处失败全部是“探针二进制早于输入”的新鲜度断言，不是语义失败；重建 `entity_overlay_probe`、`model_robustness_probe`、`model_pose_probe` 后复跑即绿。这一条本身值得记住：**先 build-target 再跑 gate**，否则 gate 会用旧探针给出旧读数。

## 7. pin 变动

| 步骤 | 旧 pin | 新 pin | 依据 |
|---|---|---|---|
| 22 | 57 | **65** | 第一轮新增 5 条（62 OK，`.scratch/anim-recon-20261029/f22.log`，节计数 27/6/5/17/6/1），复审补丁再新增 3 条样本身份断言（65 OK，`.scratch/t60-s22-identity-20261029/forward.log`，节计数 27/6/5/20/6/1） |
| 23 | 154 | **154**（不变） | 只换提取方式与一处期望，断言数不变；实测 154 OK（`f23.log`） |
| 24 | 125 | **125**（不变） | 三条 pin 换成三条不变量；实测 125 OK（`f24.log`） |

`verify-all.sh` 的注释里写了每一步的数字来源与改动理由。

## 8. 本轮**没有**证明的事

* **没有跑 `verify-all.sh`**（按要求）。第 22/23/24 步的绿是单步 focused 运行，不是链结论；其余 27 步本轮没有读数。
* 第 23 步 `pov refusal sequence` 从绝对值改成不变量，是一种**收窄**：它不再钉住“这个 demo 在这一帧的序列是 28”。如果将来有人需要那个数字，它只存在于日志里。
* `matcha` 的两个分支文案取决于扫描到的 ViewModel 实体数；本轮只证明“当前走第二支且拒绝具名”，没有追溯是哪一轮把扫描计数从 0 变成非 0（不是 T60：T60 不碰实体查找）。这一条记为 **NOT VERIFIED**，需要单独一轮。
* 第 24 步的 witness tick 推导随运行变化，本轮把 pin 换成不变量而没有让推导稳定；**推导本身的不稳定性仍是 NOT VERIFIED 项**。
* 第 22 步的 `--mutation-model` 与 `--mutation-identity` 都落在同一条 silhouette 断言上；两条变异的区分度比注释描述的弱，未改。
* 未做 T62 矩阵刷新、未合并、未 push、未做 GitHub 清理、未跑完整链。

## 9. 交付块

```
task:      T60 后动画门禁对账（verify-all 第 22/23/24 步与 T60 序列来源规则）
status:    PASS（三个 gate forward + mutation；T60/T61 focused 回归）；含 1 处 INVALID 修复（第 23 步死锚点）；
           复审补丁后第 22 步 65 断言已复跑绿（第 23/24 步未改、未重跑）
owner:     anim-recon（s22-forward, s22s23-mut, s23-forward, s23m-s24f-2, s23m-s24m, t60-t61-rerun,
           final-forward, s22-identity）
branch:    p0-entity-protocol
head:      a947e28（开工）→ 本轮提交见 git log
worktree:  D:/TF2_Native_Test（主树）
files:     weapon-animation-check.sh, viewmodel-animation-check.sh,
           viewmodel-companion-check.sh, verify-all.sh, 本报告
commands:  weapon-animation-check.sh [--mutation|--mutation-model|--mutation-identity]
           viewmodel-animation-check.sh [--mutation]
           viewmodel-companion-check.sh [--mutation]
           player-model-check.sh / player-animation-check.sh / player-animation-pixels.sh
           build-target.sh（tf2_demo_native + 9 个探针）
tests:     22: 65 OK/0 FAIL；23: 154 OK/0 FAIL；24: 125 OK/0 FAIL
           mutation: 22 三种 + 23 + 24 全部 MUTATION-CAUGHT=PASS
           T60: 167 OK/0 FAIL；T61: 232 OK/0 FAIL；T61 pixels: 28 OK/0 FAIL
rc:        0（全部上述运行）；1（首轮 23 变异死锚点、首轮 T60/T61 探针过期——均已复跑转绿）
evidence:  .scratch/anim-recon-20261029/{f22,f23,f24,s22/forward,s23-forward,s24-forward,
           mut/22-*,mut/23-mutation,s24-mutation,t60,t61,t61p,probe-after-rebuild}.log
           .scratch/t60-s22-identity-20261029/{forward.log,out,traces}（复审补丁的 65 断言运行）
failures:  首轮 23 变异锚点 0 命中（INVALID，已重锚）；首轮 T60/T61 三处 STALE-BINARY（已重建探针复跑绿）
restore:   native/ 全净；animation_binding.cpp、viewmodel.cpp 哈希 = HEAD；viewmodel.cpp 一次
           git checkout -- 恢复（变异脚本残留，见 §4.1）
lock:      FREE；verify-all.lock 残留已 rmdir（空目录、无构建进程）
processes: product_processes=0，无 nmake/cmake 残留
next:      1) T62 矩阵刷新（用本轮确认的 gate 与身份）
           2) 决定 matcha 分支与 witness tick 推导的 NOT VERIFIED 项
           3) 全部冻结后从第 1/30 步跑一次完整链
```
