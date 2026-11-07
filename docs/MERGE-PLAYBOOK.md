# MERGE-PLAYBOOK — 把 7 条分支并进 `t91-integration`，并在合并树上重跑整链

> 作者：task-22 / ledger-recon（t86-phase0 分支）。只读实测，无构建、无启动产品、无锁。
> 冻结基线：`e15577b`（分支 `integration/t79-final`，工作树 `D:/TF2_Native_Worktrees/t79-integration`）。
> 目标合并树：`D:/TF2_Native_Worktrees/t91-integration`，分支 `integration/t91`，当前 = `e15577b`，clean。
> 所有分支读数的时间戳：**2026-11-07T07:39Z**（§0.2 快照表）。分支仍在被人改动：**合并前必须重测，不得拿本文的快照当合并依据**。

---

## §0 这份文档说清什么、不说清什么

**说清**：合并顺序与依据、每条分支触碰的文件与 `main.cpp` 行区间、预判冲突点、合并后必须重做/重冻的清单、门禁红了的三种处置及其证据要求、一页可复制的 checklist、5 条 UNANCHORED 债的分工表。

**不说清**：本次没有构建、没有启动产品、没有跑任何一个门禁（纯读任务，不占锁）。所以本文里"合并后某门禁会怎样"全部是**预判**，不是实测。凡标 **[预判]** 的都必须由合并执行者在合并树上用真实运行替换成读数。

**铁律复述**（合并期间同样生效）：`t79-integration` 只读；禁 `push`/`fetch`/`stash`/`reset`/`clean`/改写历史；任何构建、启动产品、抓帧走
`bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run <owner> <命令...>`，产品启动带 `--audio-disabled`。

### §0.1 合并前的前置条件（不满足就不要开始）

1. **7 条分支里 5 条是未提交的工作树脏改动**（t85/t88/t90/t92/t93）。`git merge` 只能合提交，合不了脏工作树。
   每条分支的 owner 先在自己的 worktree 里提交，并把 **HEAD sha** 报给 Lead。提交信息建议前缀：`t85: …` / `t88: …`（t87 已是这样）。
2. 提交后 **重跑本文 §2 的测量**：uncommitted 快照的 hunk 坐标会随提交漂移，尤其 t93-playback 在本文侦察窗口内（15:4x–15:39Z）
   从 dirty=5 变成 dirty=6、并新出现 `playback-error-paths-check.sh`——**测量是快照，不是承诺**。
3. `t91-integration` 起点必须是 `e15577b` 且 `git status --porcelain` 为空；否则先查清是谁写脏了它，不要合。
4. `hostlock.sh status` 必须是 FREE；合并期间的构建与门禁各排一次票，一次租约包一批。

### §0.2 快照（2026-11-07T07:39Z，只读 `git -C <wt> …`）

| worktree | 分支 | HEAD | ahead of e15577b | 脏文件 | 形态 |
|---|---|---|---|---|---|
| t85-budget | task/t85-budget | e15577b | 0 | 13 | **未提交**（8 tracked + 89 untracked 证据） |
| t86-phase0 | task/t86-phase0 | 344ab87 | 9 | 0 | 已提交，14 files +1743/−8 |
| t87-anim | task/t87-anim | e042bcf | 1 | 2 | 已提交 1 个 + 2 个未提交 |
| t88-maps | task/t88-maps | e15577b | 0 | 11 | **未提交**（5 tracked + 16 untracked） |
| t90-identity | task/t90-identity | e15577b | 0 | 14 | **未提交**（10 tracked + 4 untracked） |
| t92-install | task/t92-install | e15577b | 0 | 6 | **未提交**（5 tracked + 1 untracked） |
| t93-playback | task/t93-playback | e15577b | 0 | 6 | **未提交**（4 tracked + untracked 门禁/补丁） |
| t91-integration | integration/t91 | e15577b | 0 | 0 | 合并目标，clean |

测量命令（合前/合后各跑一次，把输出存 `evidence/t91-merge/pre-merge-snapshot.txt`）：

```bash
for w in t85-budget t86-phase0 t87-anim t88-maps t90-identity t92-install t93-playback; do
  git -C /d/TF2_Native_Worktrees/$w rev-parse --abbrev-ref HEAD
  git -C /d/TF2_Native_Worktrees/$w rev-parse HEAD
  git -C /d/TF2_Native_Worktrees/$w status --porcelain | wc -l
  git -C /d/TF2_Native_Worktrees/$w rev-list --count e15577b..HEAD
done
```

---

## §1 合并顺序（推荐）与依据

**推荐顺序**：t87-anim → t88-maps → t90-identity → t92-install → t93-playback → t85-budget → **t86-phase0（最后）** → **t94-hygiene（从 t86 tip 起的卫生收尾，必须紧跟 t86）**。

| # | 分支 | 为什么在这个位置 |
|---|---|---|
| 1 | **t87-anim** | 只加 `docs/T85-ANIMATION-ACTION-TABLE.md`、`docs/T85-ANIMATION-RECON.md`、`native/tools/model_pose_probe.cpp`；**不碰 `main.cpp`**、无共享文件 → 最干净，先清场。它带来的 `model_pose_probe` 是后续角色轴门禁的输入工具，早合早可用。 |
| 2 | **t88-maps** | `main.cpp` 只有 2 个孤立 hunk（base 4269、7471），改 `bsp_map.*` 与探针；与其余线的 `main.cpp` 区间零重叠。**必须早于 t86**：`verify-release.sh` 第 8 步 `world-geometry-budget` 调用的 `world-geometry-budget-check.sh` 只存在于 t88；也早于 t85，因为 t85 的预算叙事/证据要吸收 t88 的 `demand.txt` 读数。 |
| 3 | **t90-identity** | 与 t92 共享 `install_lifecycle.cpp`/`install_manifest.cpp`，**必须相邻且 t90 在前**：t90 引入 `tf2_build_identity::*` 编译期身份与 `sourceDirty=` 行，t92 的 CLI 契约要针对"身份行已经存在"的版本来断（t92 触碰同一函数区 338–343，t90 触碰 337–340）。先 t90 后 t92 = 冲突只解一次。**也必须早于 t86**：`verify-release.sh` 第 7 步 `build-identity` 调用的 `build-identity-check.sh` 只存在于 t90；debt D-20 的锚点也指向它。 |
| 4 | **t92-install** | 紧跟 t90（见上）。只改安装/CLI 契约与 README，不碰 `main.cpp`。 |
| 5 | **t93-playback** | `main.cpp` 9 个 hunk（312/953/3241/3244/8920/9022/9070/11501/11507），与其他四线零重叠；但它改的是**播放/错误路径**，t85 的录制窗口读取这些 trace，所以排在 t85 前。 |
| 6 | **t85-budget** | 改动最大：`main.cpp` 28 个 hunk + 新增 `entity_mesh_budget.h` + 重写 `outline-check.sh`（4590 行 diff，2516 行新文件）+ 三个门禁的 hygiene 修正。它的门禁要吸收 t88（世界预算）与 t93（播放 trace）的读数，**必须最后于产品线**。合并后 `gate-launch-hygiene` 的基线预计变小（它修了 3 个脚本的 `--settings-file`/`--ui-settings-file` 传参）。 |
| 7 | **t86-phase0** | **本文建议把 Lead 原方案里的"t86 最先"改成"t86 最后"**，依据三条硬理由：① `gate-launch-hygiene-check.sh` 的 `evidence/gate-launch-hygiene/baseline.txt` 是**棘轮基线**，必须在最终树上重跑/重冻，它记录的是"当前树有哪些未隔离启动"——先合它、后合别线，基线立刻 stale；② `verify-release.sh` 的 STEPS 显式包含 `build-identity` 与 `world-geometry-budget`，先合 t86 会让这两个步骤在合并中期稳定地 `STUB-ABSENT=FAIL`（见 `verify-release.sh:42-44` 自述），谁看到都会误判；③ `docs/T85-COVERAGE-MANIFEST.md` 的哈希/行数表引用的是"合并后的最终 .md/.sh"，先合就要改两遍。 |
| 8 | **t94-hygiene** | 从 `task/t86-phase0` tip（`d889376`）切出、只做 D-22 卫生收尾：修 9 个 `verify-all.sh` 脚本的隔离启动、把 `baseline.txt` 85→67 行、给棘轮补 CRLF 归一化（`.gitattributes` 钉 baseline 为 LF、gate 读基线时 `tr -d '\r'`）。它改的正是 t86 的门禁与基线，**必须紧跟 t86**；它还给债表加 D-27（CAN-RED-UNANCHORED），不影响 t86 的判红计数。 |

**如果你选择维持"t86 最先"**：可以接受，但必须显式记录"合并中期整链必红、且 t86 的文件在合并末尾还要再改一次（基线重冻 + MANIFEST 哈希 + STEPS 实测）"。**不许**在中期把 `STUB-ABSENT=FAIL` 当成"还没轮到"而忽略，也不许把 `verify-release.sh` 的步骤临时注释掉。

**合并命令形态**（每条分支一次，保留来源）：

```bash
cd /d/TF2_Native_Worktrees/t91-integration
git merge --no-ff task/t87-anim -m "merge t87-anim: animation action table + census probe"
# 冲突 -> 按 §2 预判逐个解；解完 git add <file> && git commit --no-edit
```

---

## §2 每条分支触碰什么、会撞在哪里

### §2.1 文件清单（`git diff --stat e15577b..task/xxx` 实测；未提交的分支用工作树 diff + untracked）

**t85-budget**（8 files +3215/−2121，另 89 个 untracked 证据）

```
.gitignore                            |   18 +
mutate.sh                             |   79 +-
native/include/native_renderer.h      |  105 +-
native/src/main.cpp                   |  339 +-
native/src/native_renderer_entity.cpp |  181 +-
outline-check.sh                      | 4590 ++++++++++++++++++---------------
player-body-appearance-check.sh       |   11 +-
third-person-weapon-check.sh          |   13 +-
```
+ 新增（untracked）：`native/include/entity_mesh_budget.h`、`attach-weapon-budget-check.sh`(143 行)、`evidence/t85/**`（acceptance/outline、upward-pyro、smoke-skiplive 等）。

**t86-phase0**（已提交，14 files +1743/−8）

```
README.md                                  |   6 +-
debt-claim-check.sh                        | 168 new
docs/RELEASE-POLICY.md                     |  58 +-
docs/T85-COVERAGE-MANIFEST.md              | 202 new
docs/T85-DEBT-LIST.md                      | 142 new
docs/T85-SUPPORT-SCOPE.md                  |  95 new
evidence/gate-launch-hygiene/baseline.txt  |  85 new
evidence/t62-matrix/CHARACTER-MATRIX.md    |   8 +-
evidence/t62-matrix/POSTFREEZE-INVENTORY-CORRECTED.md | 138 new
evidence/t62-matrix/T62-REPORT.md          |   4 +-
gate-launch-hygiene-check.sh               | 385 new
native/docs/camera-ui-contract.md          |  25 +-
native/docs/hud-supported-formats.md       |   2 +-
verify-release.sh                          | 433 (mod)
```

> **task-24 之后（t94-hygiene 分支）**：`evidence/gate-launch-hygiene/baseline.txt` 85 → **67 行 / 38 脚本**，`gate-launch-hygiene-check.sh` 385 → **394 行**（CRLF 归一化 + 注释），`debt-claim-check.sh` 未变（167 行，`claims=51 anchored=24 cannot=21 unanchored=6`），新增 D-27 行。上表是 **t86-phase0 相对 e15577b** 的旧读数，t94 合并后这些行数以 t94 为准。

**t87-anim**（1 commit + 2 dirty，合计 3 files +816）

```
docs/T85-ANIMATION-ACTION-TABLE.md | 114 +
docs/T85-ANIMATION-RECON.md        | 356 +
native/tools/model_pose_probe.cpp  | 346 +
```

**t88-maps**（5 files +173/−8，另 16 untracked）

```
map-geometry-lighting-check.sh        |  28 +-
native/include/bsp_map.h              |  17 +
native/src/bsp_map.cpp                |  85 +-
native/src/main.cpp                   |  39 +-
native/tools/world_geometry_probe.cpp |  12 +
```
+ untracked：`world-geometry-budget-check.sh`(713 行)、`world-geometry-budget-pixels.py`、`t88-window.sh`(73)、`t88-mutation-window.sh`(31)、`evidence/t88-maps/{MAP-BUDGET-FIX.md,demand.txt,window-*.log}`、`.scratch-t88/**`（**不得入库**）。

**t90-identity**（10 files +266/−32，另 4 untracked）

```
.gitignore                                |   4 +
README.md                                 |   2 +-
native/cmake/tf2-probes-install-cli.cmake |   9 +-
native/cmake/tf2-product.cmake            |   8 +
native/cmake/tf2-version.cmake            | 103 +++++-
native/include/install_lifecycle.h        |  16 +-
native/include/install_manifest.h         |   6 +
native/src/install_lifecycle.cpp          |  42 +-
native/src/install_manifest.cpp           |   8 +
native/src/main.cpp                       | 100 +-
```
+ untracked：`build-identity-check.sh`(704 行)、`native/cmake/gen-build-stamp.cmake`、`native/docs/cli-arguments.md`、`evidence/t90-identity/docs/BUILD-IDENTITY.md`。

**t92-install**（5 files +106/−9，另 1 untracked）

```
README.md                        |   2 +
native/src/install_lifecycle.cpp |  11 +-
native/src/install_manifest.cpp  |  55 +-
native/src/main_internal.h       |   4 +
native/src/main_lifecycle.cpp    |  43 +-
```
+ untracked：`install-cli-contract-check.sh`(438 行)。

**t93-playback**（4 files +104/−7，另 untracked 门禁与补丁包）

```
native/include/playback_control.h |   6 +
native/src/audio_player.cpp       |  14 +-
native/src/main.cpp               |  88 +-
native/src/playback_control.cpp   |   3 +
```
+ untracked：`playback-error-paths-check.sh`(201 行，末行 `GATE-RESULT=PASS|FAIL`)、`.t93patches/{a,b1-b5,c1-c4,d1}.txt`（**补丁包不是产物，合并前决定：要么删，要么进 evidence 并注明"历史补丁，不参与门禁"**）。

### §2.2 `main.cpp` 行区间（`git diff -U0 … -- native/src/main.cpp | grep -E '^@@'`，base 坐标 = e15577b 行号）

| 分支 | hunks 数 | base 行号（括号内为删除行数） |
|---|---|---|
| t85-budget | 28 | 3641(0) 3682(0) 3704(0) 3728(0) 3730(3) 3736(1) 3739(0) 3747(0) 3777(1) 3778(0) 3818(0) 4008(3) 4045(0) 4046(0) 4048(1) 4050(1) 4052(2) 4122(0) 4170(0) 7014(1) 7061(1) 7219(0) 9629(0) 9772(1) 9775(0) 10413(0) 10430(2) 10434(0) |
| t88-maps | 2 | 4269(+1) 7471(插入) |
| t90-identity | 6 | 2521(0) 2524(0) 2533(4) 3010(6) 3032(0) 3036(0) |
| t93-playback | 9 | 312(0) 953(1) 3241(2) 3244(0) 8920(0) 9022(1) 9070(0) 11501(1) 11507(1) |
| t87 / t92 / t86 | 0 | — |

**结论：`main.cpp` 四条线两两精确重叠 0 处。** 最近的一对是 t85 的 3728/3730/3736/3739/3747 与 t90 的 3010/3032/3036（相距 700+ 行）以及 t93 的 3241/3244——都不相交。
**但**：git 的三行上下文合并仍有**上下文相邻**风险。已实测相邻/重叠的共享文件见 §2.3。

### §2.3 预判冲突点（按"必然 / 可能 / 语义"分三档）

**必然文本冲突（同一行被两条线改写）**

1. `README.md` 第 46 行：**t86 与 t90 都替换同一行**（t86 改成"没有 `--help`，参数循环见 `main.cpp:2523-3034`"；t90 改成"未识别参数被忽略并计入 trace，`--strict-args` 拒绝"）。t92 在第 47 行后追加 2 行。
   → 解：保留 t90 的 `--strict-args` 语义 + t86 的"无 --help"事实，两者不矛盾（可以用 t90 的措辞再补一句"没有 --help"）。**不得只留一边。**

**可能文本冲突（hunk 相隔 2–3 行，git 可能自己合上，也可能红）**

2. `native/src/install_lifecycle.cpp`：t90 base 337–340 插 `sourceDirty/sourceDirtyFiles`；t92 base 338–343 在同一函数 `installInto` 里插 CLI 契约字段。→ 解：两段都留，注意字段写入顺序（t90 注释写明"写在 sourceTree 之后以保持 pre-T90 键序"，t92 的插入不得打乱）。
3. `native/src/install_manifest.cpp`：t90 base 253(0)；t92 base 255(1)。→ 同上，两段都留。
4. `native/src/install_lifecycle.cpp` 另一处：t90 base 13 (namespace 声明块) 与 t92 base 6/36——t92 只改 6/36 附近（`install_manifest.cpp` 另有 6/36），**t92 的 install_lifecycle 只有 340 一处**，故第 2 条是唯一近邻。

**语义耦合（不冲突但必须一起验）**

5. **预算叙事撞车**：t88 把 `BspParser::parse(mapBytes, displayMap, 200000)` 改成 `constexpr std::size_t kWorldTriangleCapacity=400000` 并新增 `map: geometry budget …`/`gpu: world geometry budget …` trace（`main.cpp:4269/7471` 区）；t85 新增 `entity_mesh_budget.h` 并改 `native_renderer_entity.cpp:181`（实体网格预算）。两者对象不同（世界三角 vs 实体网格），但**都写 "budget" 行**：`world-geometry-budget-check.sh` 若按裸 `budget` 匹配 trace，会被 t85 的新行污染。→ 合并后**必须同时跑** `world-geometry-budget-check.sh` 与 `attach-weapon-budget-check.sh`，并检查前者的 grep 是否用了足够具体的锚（`map: geometry budget` / `gpu: world geometry budget`）。**[预判]**
6. **t85 重写 `outline-check.sh` 会动债表锚点**：t86 的 `docs/T85-DEBT-LIST.md` 有 `gate:outline-check.sh#OUTLINE-CHECK=PASS`。已核对 **t85 的工作树版本仍含该 token**（新文件 `:2512` 有 `OUTLINE-CHECK=PASS`，`:2514` FAIL，`:690` PARTIAL）→ 合并后 `debt-claim-check.sh` 应仍绿，但必须实跑确认。
7. **t85 的 hygiene 修正会减小 launch-hygiene 违规面**：它给 `third-person-weapon-check.sh`/`player-body-appearance-check.sh` 加 `--settings-file`/`--ui-settings-file`（指向 `$OUT/inputs` 副本）。所以 t86 的 85 行基线在合并树上**应变小**；变大 = 新脚本违规，必须修，不许重冻。
8. **t90 的 `build-identity` 与 t92 的 `install-cli-contract` 相互断言**：两条线都改 `--version`/`--install-status` 的输出形状（t90 加第 3 行 `dirty=…`，t92 直接改 install 命令行契约）。合并后**先跑 t90 的门禁再跑 t92 的门禁**；若 t92 的契约断言里写死了 t90 之前的行数/键序，按"t90 优先、t92 适配"的原则改 t92 的期望值，并在证据里注明原因（不许静默改绿）。
9. **t93 的错误路径与 t90 的 exit code 语义**：t90 的 CLI 解析改动（`main.cpp:2521-3036` 区、`--audio-device` 退出码 13 的文档）与 t93 的 `playback-error-paths-check.sh`（`GATE-RESULT=PASS`）都涉及退出码。合并后跑 t93 门禁时若退出码断言漂移，先查 t90 的 `--strict-args` 是否把原先"静默忽略"的参数变成拒绝。

---

## §3 合并后必须重做/重冻（逐条可勾；每条给命令与期望 token）

> 记录目录：`evidence/t91-merge/`。每条完成后写一行"做了什么 / 读数 / 谁做的 / 时间 / 日志路径"。
> **原则：旧 exe 的 sha、旧帧 pin、旧基线、旧哈希表一律作废；不许把"应该没变"写成"没变"。**

### R01 重新构建（旧 exe sha 全部作废）

```bash
bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run t91-merge \
  bash -lc 'cd /d/TF2_Native_Worktrees/t91-integration && bash build-cmake.sh'
sha256sum native/build-nmake/tf2_demo_native.exe
```
- 期望：构建 rc=0，`native/build-nmake/tf2_demo_native.exe` 存在。
- 记录：新 exe sha256、字节数、构建命令、`git rev-parse HEAD`、hostlock owner、开始/结束时间 → `evidence/t91-merge/BUILD.md`。
- **作废清单**：冻结 exe `a3c93d5755fd331351766dfb991fe17f90c4dfb753ad7ef9f53391f0303724e9`（2,763,264 B）。
  引用点实测：`docs/T85-COVERAGE-MANIFEST.md`、`docs/T85-SUPPORT-SCOPE.md`（`grep -rIl a3c93d5755fd3313` 仅这两处）+ 各分支 evidence 头里的 exe sha。
  刷新命令：`grep -rn 'a3c93d5755fd3313' --include='*.md' .` → 只改"当前 exe"的引用，**历史证据头不要改**（那是历史 exe，改了就是伪造）。

### R02 四个 P0 门禁重跑（outline / third-person-weapon / body-appearance / class-pixels）

```bash
bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run t91-merge \
  bash -lc 'cd /d/TF2_Native_Worktrees/t91-integration && \
    bash outline-check.sh && bash third-person-weapon-check.sh && \
    bash player-body-appearance-check.sh && bash class-pixels-check.sh'
```
- 期望 token：`OUTLINE-CHECK=PASS`、`THIRD-PERSON-WEAPON=PASS`、`BODY-APPEARANCE=PASS`、`CLASS-PIXELS=PASS`（`class-pixels-check.sh:401`）。
- 任一红 → 按 §4 处置，不许拆成两次运行拼一个绿。

### R03 新增门禁全部进链并重跑

| 门禁 | 分支 | 期望 token | 现在是否已在 `verify-release.sh` |
|---|---|---|---|
| `gate-launch-hygiene-check.sh` | t86 | `GATE-LAUNCH-HYGIENE=PASS violations=N baseline=M` | 第 1 步（120s） |
| `debt-claim-check.sh` | t86 | `DEBT-CLAIM=PASS claims=50 anchored=24 cannot=21 unanchored=5` | 第 2 步（120s） |
| `build-identity-check.sh` | t90 | `BUILD-IDENTITY=PASS`（`:55`/`:492`）+ `BUILD-IDENTITY-MUTATION=PASS` | 第 7 步（现存 STUB-ABSENT=FAIL，合并后变实） |
| `world-geometry-budget-check.sh` | t88 | `WORLD-GEOMETRY-BUDGET=PASS`；像素段 `WORLD-GEOMETRY-PIXELS=FAIL` 单独出现 | 第 8 步（同上） |
| `install-cli-contract-check.sh` | t92 | `INSTALL-CLI-CONTRACT=PASS` + `INSTALL-CLI-CONTRACT-MUTATION=PASS` | **否 —— 需要 t86 owner 加第 10 步** |
| `playback-error-paths-check.sh` | t93 | `GATE-RESULT=PASS` | **否 —— 需要 t86 owner 加第 11 步** |
| `attach-weapon-budget-check.sh` | t85 | **无机器 token**（只有 `=== … assertions, … failed ===` + rc） | **否** |
| `timeline-check.sh` | 无（全仓未找到） | — | **不存在**：Lead 要求"timeline 重跑"前必须先有人写它，否则这一步只能记 ABSENT，不能记 PASS |

→ **动作**：合并后由 t86 owner 把后 4 行里已存在的脚本注册进 `verify-release.sh` 的 STEPS（`VERIFY_RELEASE_TIMEOUT_*` 同步加），把 `steps_total` 从 9 改成实际值，并实测 dry-run：

```bash
VERIFY_RELEASE_DRY_RUN=1 bash verify-release.sh | tail -1
# 期望形如：VERIFY-RELEASE=DRY-RUN steps_total=13
```

（**顺带修一个欠账**：`attach-weapon-budget-check.sh` 无末行 token，导致它无法被 `debt-claim` 锚定、也不好在整链里判定；建议加末行 `ATTACH-WEAPON-BUDGET=PASS|FAIL`——这同时是 8.5-5 的补锚点路径，见 §6。）

### R04 像素基线与帧 pin：逐条重新确认，**禁止静默重冻**

哪些断言真的会"因为别线行为变化而合法改变"，必须逐条写原因：

| 门禁 | pin 的是什么（文件） | 刷新机制 | 合并后怎么判 |
|---|---|---|---|
| `map-geometry-lighting-check.sh` | 每图每姿势 `evidence/t62-map-matrix/<map>/frame-pins-<pose>.txt`（10 个世界帧的 sha256）+ `cull-reference-<pose>.txt` | `--re-pin[="why"]`；pin 头记录 head/time/reason；脚本自述"**Never re-pin to make a run green**"（`map-geometry-lighting-check.sh:154-164`，用法见 `:169-177`） | 先**不 re-pin** 跑一遍：pin 不匹配 = 画面确实变了。变化若可归因到 t88（世界三角容量 200000→400000，snakewater 请求 242,361）或 t85（实体预算导致少画/多画），写 `evidence/t91-merge/PIXEL-DELTA.md`（门禁/夹具/旧 sha/新 sha/原因/哪条分支），再 `--re-pin="t91 merge: <branch> changed <what>"`。**不能归因的漂移 = 红，不许 re-pin。** |
| `class-pixels-check.sh` | `evidence/t62-matrix/class-pixels/<class>/{animated,identity}.bmp` | 基线缺失即 FAIL；刷新 = 重新生成 BMP 并重算 sha 入证据 | 同上；`third-person-weapon-check.sh:371/:379` 用同一 `BASELINE` 目录，**两边必须同时更**，只改一边 = 自欺。 |
| `third-person-weapon-check.sh` | 同 `BASELINE`（`:82`） | 同上 | 同上。 |
| `outline-check.sh`（t85 重写） | fixture 像素（t79 版 `:583` 注释 "THE FIXTURE IS PINNED NOW (T52 A3)"；t85 版 2516 行需重读头注确认机制） | **[未验证]** 重写后 pin 的文件与刷新 flag 未逐行确认 | 合并后**先读 t85 版头注**（`sed -n '1,120p' outline-check.sh`），把它 pin 的文件/刷新命令补进本表；在此之前不许用 `--update*` 之类未知 flag。 |
| `world-render-reliability-check.sh` | 帧 pin（403 行，机制未逐行读） | **[未验证]** | 同上：不匹配先归因，再决定。 |
| `world-geometry-budget-check.sh`（t88 新） | 预算读数 + 像素（脚本内 16 处 re-pin/REPIN 字样） | 按脚本头注的 re-pin 流程 | 首次跑用**现状**建 pin 必须在证据里写"初次建立"，不能假装是"复核通过"。 |

**"两个 exe 逐字节不变"型断言**（`third-person-weapon-check.sh` 的 "off is unchanged"、`class-pixels` 的 identity 差分、outline 的 fixture 重复）在合并后**必须重新确认**：这类断言比较的是"本次 exe 产物 vs 基线"，基线来自旧 exe，所以合并本身就让它们处于"可能合法改变"状态。逐条记录：断言名 / 旧基线 sha / 新产物 sha / 是否一致 / 若不一致的归因。**没有归因的不一致 = FAIL。**

### R05 `baseline.txt` 重冻（棘轮：只能变小，不能变大）

```bash
cd /d/TF2_Native_Worktrees/t91-integration
bash gate-launch-hygiene-check.sh            # 先看 now.txt 与旧基线的差集
# 只允许：STALE（旧基线行不再违规）被删；NEW（新违规）必须修脚本，不许进基线
bash gate-launch-hygiene-check.sh --update-baseline
bash gate-launch-hygiene-check.sh            # 再跑必须 PASS
```
- 严格按 `docs/RELEASE-POLICY.md:46-51` 的合并步骤：**先在冻结/合并树上重跑复核，再 `--update-baseline`**；基线是 worktree-local evidence，不许把某分支的基线直接搬过来。
- 旧基线：85 行 = 32 缺 `--settings-file` + 42 缺 `--ui-settings-file` + 6 shared settings + 5 shared ui，涉及 **47 个不同脚本**（`evidence/gate-launch-hygiene/baseline.txt`）。
- 期望：新基线行数 **≤ 85**（t85 修了 3 个脚本，应减少）。**若 > 85，停：这是新违规，修源码/脚本，不是重冻。**
- 记录：旧行数/新行数、增删行、`GATE-LAUNCH-HYGIENE=PASS violations=N baseline=M` 一行。
- **task-24 之后**：9 个 `verify-all.sh` 脚本已实际修复（D-22），基线在 `t94-hygiene` 分支重算为 **67 行 / 38 脚本**（85 − 9×2）。合并 t86 时若还没合 t94，期望 85 行；合完 t94 后应在最终树重跑到 **≤67 行**再 `--update-baseline`——不许把 67 行当“已经绿”而不复跑。

### R06 `debt-claim` 重跑与锚点更新

```bash
bash debt-claim-check.sh   # 期望 DEBT-CLAIM=PASS claims=50 anchored=24 cannot=21 unanchored=5
```
- 合并后**必须更新两条**（t86 owner 改 `docs/T85-DEBT-LIST.md`）：
  - **D-20**：t90 的 `build-identity-check.sh#BUILD-IDENTITY=PASS` 落地后，把 `CAN-RED-UNANCHORED …` 改成 `CAN-RED gate:build-identity-check.sh#BUILD-IDENTITY=PASS` → 期望 `anchored=25 unanchored=4`。
  - **8.5-5**：若 §6 的建议被采纳（给 `attach-weapon-budget-check.sh` 加 token），改成 `CAN-RED gate:attach-weapon-budget-check.sh#ATTACH-WEAPON-BUDGET=PASS` → `anchored=26 unanchored=3`。
- 规则：**anchored 只能增、unanchored 只能减或持平**；任何"脚本改名导致锚点失效"必须当场修锚点，不许把行删掉。

### R07 `docs/T85-COVERAGE-MANIFEST.md` 刷新

- 刷新对象：所有 `.md`/`.sh` 的 sha256 与行数（MANIFEST 里"门禁哈希"段）、`verify-release.sh` 的 `steps_total`、baseline 行数/脚本数、新门禁 6 条（`build-identity`、`world-geometry-budget`、`install-cli-contract`、`playback-error-paths`、`attach-weapon-budget`、`timeline`(ABSENT)）的"有/无门禁"归属。
- 口径：`docs/*.md` 取 `git show HEAD:<path>` 的字节（不是工作树，行尾可能不同）；`*.sh` 取工作树 LF 字节（MANIFEST 末尾有注）。复核命令写在 MANIFEST 里，照跑一遍。
- 期望：新加的 C/N 行与实测 token 一致；**每一行都能 `grep -F` 命中**。

### R08 证据入库与 `.scratch` 清理边界

- 入库：`evidence/t85/**`(89)、`evidence/t88-maps/**`(3+)、`evidence/t90-identity/**`(1)、t92/t93 的证据目录（如有）、`evidence/t91-merge/**`（本次合并产物）。
- **不入库**：`.scratch-*`、`.t93patches/`、`*.exe` 的构建中间物、`outline/inputs` 里从 tracked 证据复制出来的副本（若它们只是副本，用 `.gitignore` 或 `$OUT` 规避）。
- 检查：`git status --porcelain` 在合并 + 构建 + 跑门禁之后仍应只剩**预期**的改动（门禁跑完后不该有 tracked 文件被改——这是 D-18 的本体，t85 修了 3 个脚本，但其他脚本可能仍会写 tracked `ui.json`；若出现，记进 DEBT-LIST，不许 `git checkout --` 掩盖）。

### R09 冻结树签收（**只有 Lead 能做**）

- 前提：`t91-integration` 上 `VERIFY-RELEASE=PASS` 且 `git status` 干净。
- 谁：Lead（或 Lead 书面指定的 owner）。
- 步骤：
  ```bash
  git -C /d/TF2_Native_Worktrees/t79-integration status --porcelain   # 必须空
  git -C /d/TF2_Native_Worktrees/t79-integration rev-parse HEAD        # 必须仍是 e15577b
  git -C /d/TF2_Native_Worktrees/t91-integration diff --stat e15577b..HEAD
  ```
  然后由 Lead **二次确认**（第二条消息里明确"确认提升"），再在冻结树里 `git merge --ff-only integration/t91`（或由 Lead 决定的其他方式），并立即打 tag `pre-t91-<date>` 之前先建 `t91-signoff-<date>`。
- 记录：`evidence/t91-merge/FROZEN-SIGNOFF.md`：谁、何时、两条确认消息的引用、`git rev-parse` 前后值、exe sha、`VERIFY-RELEASE=PASS` 原文行。
- **注意**：合并结果如果最终不回冻结树，也要在 t91 上留一份 `t91-signoff` tag，否则下次没人知道"这份 PASS 对应哪棵树"。

---

## §4 门禁红了怎么办（三种处置 + 各自的证据要求）

**先统一报告口径**：`VERIFY-RELEASE=FAIL steps_ok=N steps_total=M` 必须原样贴出，附失败步骤日志路径、该步 rc、失败断言原文。**禁止**："基本通过 / 只剩一个小问题 / 已修复待复跑" 这类改写。红就是红。

### 处置 A：回退该分支（默认首选，当失败可归因到单条分支且不阻塞其他线）

- 条件：能指出失败断言由哪条分支的哪个改动引入（bisect 或门禁日志足够定位）。
- 动作：在 `t91-integration` 上 `git revert -m 1 <merge-commit>`（保留历史，不用 `reset`/`rebase`）。
- 证据：失败日志、`git log --oneline -3`、revert commit sha、回退后**整链重跑**的 `VERIFY-RELEASE=PASS`（不是"预计会绿"）。
- 记录：`evidence/t91-merge/REVERTS.md`：分支、merge sha、revert sha、失败断言、回退理由、重跑结果。

### 处置 B：修（当失败是合并语义冲突/接口错配，且改动小）

- 条件：能写出最小 patch，并解释为什么这不掩盖缺陷（例如"t92 的期望行号按 t90 的新输出 +1"）。
- 动作：修代码/期望值 → **必须重跑该门禁 + 它的变异/负控**（若门禁自带）→ 再跑整链。
- 证据：patch 的 `git diff`、门禁前后 token、变异数、整链 `VERIFY-RELEASE=PASS`。
- 额外：若失败暴露的是**门禁本身的盲区**（旧门禁没覆盖到），必须同时加断言并把它写进 `docs/T85-DEBT-LIST.md` 的新行（带 `CAN-RED gate:…#token`）。

### 处置 C：降级为已知缺陷（最后手段，需 Lead 签字）

- 条件：缺陷用户可见但不阻塞发布（如 D-26 后退 seek 相位复位），且**有** `docs/T85-SUPPORT-SCOPE.md` 的"已知限制"段落 + `DEBT-LIST` 行（`CANNOT-RED <理由>` 或 `CAN-RED-UNANCHORED <理由>`）。
- 动作：不改门禁期望值去"绕过"；把门禁该步标 `KNOWN-DEFECT=<id>` 而不是 PASS 只有在 Lead 明确同意后才可以，且 `verify-release.sh` 必须把它计入 `steps_ok` 之外（即整链仍 PASS 但摘要里列出 known-defect=N）。
- 证据：缺陷复现命令、BMP/日志路径、影响面、用户可见描述、Lead 签字（消息引用）。
- **禁止**：为了让链绿而删断言、注释步骤、放宽阈值 —— 这三种等价于伪造证据。

---

## §5 一页 checklist（复制执行；每步给期望 token）

```text
[ ] 0  前置：5 条脏分支已提交；记录 7 个 HEAD sha；t91 仍 e15577b 且 clean；hostlock FREE
[ ] 1  快照：§0.2 命令 → evidence/t91-merge/pre-merge-snapshot.txt（含 git diff --stat 每分支）
[ ] 2  merge t87-anim  （期望：无冲突；或仅 docs 冲突）
[ ] 3  merge t88-maps   （期望：main.cpp 2 hunk 自动合；bsp_map 无冲突）
[ ] 4  merge t90-identity（期望：README 冲突 —— 与 t86 的第 46 行；此时先按 t90 措辞解）
[ ] 5  merge t92-install （期望：README:47 追加；install_* 两处近邻冲突 → 两段都留）
[ ] 6  merge t93-playback（期望：main.cpp 9 hunk 自动合；.t93patches 决定去留）
[ ] 7  merge t85-budget  （期望：outline/third-person/body 脚本整体替换；main.cpp 28 hunk 自动合）
[ ] 8  merge t86-phase0  （期望：README 最终解；verify-release/MANIFEST/DEBT-LIST 进来）
       然后 merge t94-hygiene（基于 t86 tip；baseline 85→67、gate 394 行；不改 README/main.cpp）
[ ] 9  冲突清单落盘：git diff --name-only --diff-filter=U 必须为空 → evidence/t91-merge/CONFLICTS.md
[ ] 10 构建：hostlock run … bash build-cmake.sh → rc=0；sha256sum exe → BUILD.md
[ ] 11 静态门禁：gate-launch-hygiene（首跑看差集）→ debt-claim（期望 PASS claims=51 anchored≥24；t93-timeline 合入后 D-27 变 anchored=25）
[ ] 12 四 P0：outline / third-person / body / class-pixels → 4 个 PASS token 全出
[ ] 13 新门禁：build-identity / world-geometry-budget / install-cli-contract / playback-error-paths
        → BUILD-IDENTITY=PASS / WORLD-GEOMETRY-BUDGET=PASS / INSTALL-CLI-CONTRACT=PASS / GATE-RESULT=PASS
[ ] 14 像素与 pin：先不 re-pin 跑 map-geometry / class-pixels / third-person / outline / world-render
        → 任何不匹配先写 PIXEL-DELTA.md 归因；只有可归因项才 --re-pin="…"
[ ] 15 基线重冻：gate-launch-hygiene --update-baseline（行数 ≤67，t94 已修 9 脚本；未合 t94 时 ≤85）；再跑必须 PASS
[ ] 16 债表更新：D-20 锚点 → anchored+1；若采纳 8.5-5 → anchored+2（见 §6）
[ ] 17 verify-release 注册新步骤 + steps_total 改写；VERIFY_RELEASE_DRY_RUN=1 → steps_total 实测
[ ] 18 整链：hostlock run … env VERIFY_RELEASE_OWNER=… bash verify-release.sh
        → 末行 VERIFY-RELEASE=PASS steps_ok=N steps_total=N；机器隔离 MACHINE-MUTATED 不得出现
[ ] 19 MANIFEST 哈希/行数刷新 + 复核命令自跑一遍 → 每条 grep -F 命中
[ ] 20 冻结树签收（Lead 二次确认）：t91-signoff tag + FROZEN-SIGNOFF.md
```

**任何一步红：停下，按 §4 处理，不要继续往下堆。** 整链的语义是"从干净树到 PASS 一次走完"，不是"每一步都曾经绿过"。

---

## §6 6 条 UNANCHORED 债的分工表（Lead 派活用）

现状：`DEBT-CLAIM=PASS claims=51 anchored=24 cannot=21 unanchored=6`。这 6 条是"声称能判红但没有可执行锚点"，在补锚点之前**不能**被 `debt-claim-check.sh` 判定，只能人工读证据。

| id | 声明（缺什么断言） | 最该补的分支/owner | 门禁类型 | 建议的具体锚点（脚本 + token） | 依赖 / 备注 |
|---|---|---|---|---|---|
| **8.2-7** | 光照数值 oracle：LDR/HDR、边界、UV clamp、已知源对比、显式 oracle 与残差。现状只有 100 个世界帧 pin 与 `lighting-semantics-check.sh`(98 断言)，**解码数值无 pin**（`MAP-MATRIX.md:18` 一带） | **t88-maps**（它拥有 `map-geometry-lighting-check.sh` 与 `--re-pin` 基建） | GPU 抓帧 + 数值采样 | 新脚本 `lighting-luxel-oracle-check.sh`，末行 `LUXEL-ORACLE=PASS|FAIL residual=<float>`；断言"每样本数值 ∈ 期望区间 ± 残差"，锚点写 `gate:lighting-luxel-oracle-check.sh#LUXEL-ORACLE=PASS` | 需要产品 trace 输出每样本 luxel/atlas 数值；可能要 t85 的渲染改动配合。工作量大，建议单开 t94 |
| **8.3-2** | 玩家实体身份 vs 职业回退没分开断言（checksum/path/骨骼 rig）；`entity_model.cpp:699/:782`、`entity_overlay.cpp:301` 有代码，`CHARACTER-MATRIX.md:42` 只有 36/36、14/14 计数 | **t87-anim**（`model_pose_probe` + 骨骼 rig 最接近） | GPU 抓帧 / 探针 | 扩展 `model_pose_probe` 输出"实体自带 model index → 模型 sha256 / rig 名"表；门禁断言"有 model index 时不得走职业回退"，token `ENTITY-IDENTITY=PASS`；锚点 `gate:entity-identity-check.sh#ENTITY-IDENTITY=PASS` | 采样实体当前不携带 model index（`CHARACTER-MATRIX.md:42` 的已知限制），需产品侧补 trace 或在探针里读协议字段 |
| **8.5-5** | 预算拒绝优先级：`T62-REPORT.md:10` 有 supply=23 refused=3 / `0x8007000E` 具名拒绝，但**没有"非目标预载不得挤占可见目标"的断言**；三把 OOM 武器是 task-1 的 P0（未修） | **t85-budget** | 静态 / trace 断言（已有脚本，缺 token） | 给现有 `attach-weapon-budget-check.sh` 加末行 token `ATTACH-WEAPON-BUDGET=PASS|FAIL`，并把"优先级"写成显式断言（当前是 resident/refusals=0 的读数，不是优先级证明）；锚点 `gate:attach-weapon-budget-check.sh#ATTACH-WEAPON-BUDGET=PASS` | 该脚本 143 行、rc 判定，**无 machine token**，所以现在连"能判红"都无法被 debt-claim 表达；补 token 是最便宜的一步 |
| **D-17** | 官方地图夹具覆盖不均：角色轴门禁默认跑 snakewater（`class-pixels-check.sh:102`、`player-body-appearance-check.sh:42`、`third-person-weapon-check.sh:85`），队伍对夹具在 upward；sunshine demos.tf 最好 8/9、rc12 本地 0 条 | **无人认领 → 建议新开 t94-fixtures**（或并入 t87-anim） | 静态（矩阵断言） | `fixture-coverage-check.sh`：读 `D:/_shared/t85-assets/{demo-candidates.csv,local-demo-index.csv}`，断言"四官方图每图 ≥N 条九职业 demo"或显式 `UNSUPPORTED`；token `FIXTURE-COVERAGE=PASS maps=4 full=N partial=M`；锚点 `gate:fixture-coverage-check.sh#FIXTURE-COVERAGE=PASS` | 这是**资产问题**不是代码问题：rc12 本地 0 条 demo 只能靠下载/录制；补门禁会立刻红，所以要先决定"允许 partial 到什么程度"（写进 SUPPORT-SCOPE） |
| **D-20** | 构建身份错误：`--version` 的 sourceTree/buildUtc 不属于该 exe（`native/cmake/tf2-version.cmake:25` configure 期求值） | **t90-identity（已实现）** | lifecycle / CLI 静态 + 产品 | 直接把债表行改成 `CAN-RED gate:build-identity-check.sh#BUILD-IDENTITY=PASS`（token 实测在 `build-identity-check.sh:55/:492`，另有 `BUILD-IDENTITY-MUTATION=PASS`） | **只要 t90 合并即可清零**；合并顺序里 t90 在 t86 之前，所以 t86 的 debt-claim 不会因为"脚本不存在"而红 |
| **D-27** | `--capture-tick` 不是 seek：只把越过 N 的游标按回 N（`native/src/main.cpp:9005-9019`），真 seek 走 `main.cpp:1156-1165` → `playback_state.cpp:13-31`；拿它当"seek 到 N"会写出假的跨 seek 证据 | **workflow-auditor / t93-timeline 分支**（`timeline-check.sh` 只在 `D:/TF2_Native_Worktrees/t93-timeline/`） | 产品 trace 断言 | 合入后把债表行改成 `CAN-RED gate:timeline-check.sh#captured frame drawn at the requested tick`（该字面串实测在 `timeline-check.sh:262`） | 分支合入前 `debt-claim-check.sh` 会报 missing-script，故现记 `CAN-RED-UNANCHORED`；合入即 anchored+1 |

**派活顺序建议**：D-20（合并即解）→ D-27（合并 t93-timeline 即解）→ 8.5-5（补 token，半天）→ D-17（先定政策再写门禁）→ 8.3-2（要产品 trace）→ 8.2-7（最大，独立 task）。
每条补完都要：改 `DEBT-LIST` 的机器判红列、跑 `debt-claim-check.sh` 看 `anchored` 增、`unanchored` 减，并在交付里贴 `DEBT-CLAIM=PASS …` 原文。

---

## §7 本文的测量方法与局限（复核用）

- 全部读数来自 `git -C /d/TF2_Native_Worktrees/<wt> {rev-parse,status,rev-list,diff --stat,diff -U0}`，只读，无写操作，无构建/启动/锁。
- `main.cpp` 行区间用`git diff -U0 -- native/src/main.cpp | grep -E '^@@'` 取 base 侧行号；括号内数字为删除行数，0 = 纯插入。
- 冲突判定只做了**行区间相交**与**共享文件**两层；没有做 `git merge --no-commit --no-ff` 的试合（那会写 `t91-integration` 的索引与工作树，属于合并动作，留给合并执行者）。**"精确重叠 0 处"不等于"合并不冲突"**——§2.3 的"必然冲突"是逐行读 diff 得出的，其余是预判。
- 未验证项已在正文标 **[未验证]** / **[预判]**：outline-check.sh（t85 重写版）的 pin 机制、world-render-reliability-check.sh 的 pin 机制、world-geometry-budget-check.sh 的 re-pin 流程、t92 契约是否依赖 t90 的行序、以及合并后任何门禁的实际颜色。
- 分支仍在被改动：t93-playback 在侦察窗口内 dirty 5→6、新出现 `playback-error-paths-check.sh`。**合并前重跑 §0.2 与 §2.2 的测量**，本文的数字只对 2026-11-07T07:39Z 的快照负责。
