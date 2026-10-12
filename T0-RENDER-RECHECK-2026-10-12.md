# T0 渲染门禁补跑（2026-10-12）

本文件是 T0「渲染档 9/9 补跑」的交付记录。9 个门禁的读数来自两次真实运行：
正向 `bash verify-render.sh` 与变异 `bash verify-render.sh --mutation`，命令见 §2。
本轮没有修改任何产品源码或门禁判据；提交只包含验收证据与文档（§7）。

---

## 0. 任务与工作区

| 项 | 值 |
|---|---|
| 任务 | T0 渲染门禁补跑（关闭 README 记载的缺口「`verify-render.sh` 已扩展为 9/9，尚未在主线上重跑」） |
| 工作区 | `D:\TF2_Native_Worktrees\render-gate-recheck` |
| 分支 | `task/render-gate-recheck` |
| 基线 commit | `origin/p0-entity-protocol@9a8387b`（`feat: integrate ViewModel companion rendering`） |
| 目标 exe | `native/build-nmake/tf2_demo_native.exe` |
| 是否可运行 | 是。构建 `rc=0`；两次 tier 运行退出码均为 0 |

边界：没有改 `native/`（对基线 `git diff --stat 9a8387b -- native/` 为空）、没有改任何
`*-check.sh` 判据、没有运行 `verify-all.sh`、没有杀进程或删锁。

---

## 1. 前置检查（只读）

运行前快照 `evidence/render-gate-recheck/01-pre-run-state.txt`
（sha256 `c77750b1…`）：

- **进程**：`tasklist` 过滤 `tf2|probe|verify|python` 无匹配。没有并发门禁、探针或主程序。
- **锁**：本树、全部 worktree、主树均无 `.scratch/*.lock`。
- **工作树**：`git status --porcelain` 为空；`git status --porcelain evidence/` 为空；
  `HEAD == origin/p0-entity-protocol == 9a8387b`。
- **唯一偏差**：新建 worktree 没有 build tree（`CMakeCache.txt` 不存在），所以 build 步
  从零 configure 并全量构建 9 个目标。这是本轮耗时（1h11m）远超 README 历史读数
  （5m27s，有构建缓存）的直接原因。

---

## 2. 命令与退出码

| 运行 | 命令 | 退出码 | 末行 | 耗时 |
|---|---|---|---|---|
| 正向 | `bash verify-render.sh` | **0** | `VERIFY-RENDER=PASS (9/9 gates, mutation=0)` | 20:08:27 → 21:19:56（1h11m29s） |
| 变异 | `VERIFY_RENDER_OUT=evidence/fast/render-mutation bash verify-render.sh --mutation` | **0** | `VERIFY-RENDER=PASS (9/9 gates, mutation=1)` | 21:20:56 → 22:55:08（1h34m12s） |

变异那次显式改写了 `VERIFY_RENDER_OUT`，使两次运行的机器产物落在两个目录，互为对照，
绝不会互相覆盖。原始日志分别为
`02-verify-render-straight.txt`（sha256 `a9483814…`）与
`05-verify-render-mutation.txt`（sha256 `9d60efca…`）。

---

## 3. 九个门禁逐项（正向）

| # | 门禁 | 判定 | 关键读数 |
|---|---|---|---|
| 1 | resource-reachability | `RESOURCE-REACHABILITY=PASS` | `archivesOpened=4`、`missingNames=pak01_dir.vpk`；BSP 59,590,420 B、材质 148 → 可解像素 111；图集合格 7 / 超限 104、三角覆盖 `30731/200000`；反事实 cap 1024 → 193875 |
| 2 | frame-capture | `FRAME-CAPTURE=PASS` | 暂停帧 `a61a3e4d…`（两次抓帧逐字节相同）；bagel `fa1bb194…`、snakewater `d086695e…`；非法 tick rc=13 |
| 3 | entity-material | `ENTITY-MATERIAL=PASS` | tick 3001 `entityMaterials=95/95`、`ranges=29/29`、`models=59/59`；台账 `resolved=59 uploaded=59`；`blockDiff=30 frameDiff=434510` |
| 4 | weapon-animation | `WEAPON-ANIMATION=PASS` | tick 2000/4000/6000 `gpuSkin=62/177→62/237→62/237`、`poseChanges=48/146/283`；`--model` identity 不变形（BMP-DIFF=0）、序列 8.90%；probe bindpose 变异 `motionWorst 1.24223→0`；order 变异 `skinLands=35.9878` |
| 5 | viewmodel-contract | `VIEWMODEL-CONTRACT=PASS` | `assertions_ok=154`；真实资产 9,723 顶点 / 5 attachments / 45 骨；缺资源、损坏 attachment、无锚点、缺 companion 四类负向均 `drawn=0` |
| 6 | viewmodel-animation | `VIEWMODEL-ANIMATION=PASS` | 139 条 OK；运行时 5 变异 + 5 控制；构造正例 `coverage=58/58`；`pov-pose-bind vs pov-pose-apply: 31709` 字节位置不同 |
| 7 | viewmodel-companion | `VIEWMODEL-COMPANION=PASS` | 见证 tick 0/814/1698 携带序列 29/1/30，三帧两两不同（1879841 / 1948416 / 2107822）；自然运行 `accepted=0`、refused `1482`；构造正例帧差 101408 |
| 8 | material-chain | `12/12 synthetic keys held` | VTF 四类拒绝、cubemap 顺序与解析、BSP 默认、VMT/water 特性映射、两种"诚实近似"模式 |
| 9 | evidence-tree | `nothing under evidence/ moved` | 运行前后 `git status --porcelain evidence/` 一致 |

`gates_ok=9 gates_bad=0`。

---

## 4. 变异结果（`--mutation`）

九项同样全绿：`gates_ok=9 gates_bad=0`，`VERIFY-RENDER=PASS (9/9 gates, mutation=1)`。

| # | 门禁 | 变异判定 | 被注入的缺陷与它变红的读数 |
|---|---|---|---|
| 1 | resource-reachability | `MUTATION-CAUGHT=PASS` | 预期归档数被故意改错 → 变红 |
| 2 | frame-capture | `MUTATION-CAUGHT=PASS` | 图像翻转一个字节 → 内容哈希变化 |
| 3 | entity-material | `MUTATION-CAUGHT=PASS` | 用 atlas 见证帧替换新抓帧 → 第 3 层把 atlas 像素与模型自身材质分开 |
| 4 | weapon-animation | `MUTATION-CAUGHT=PASS` | 强制 bind pose（`motionWorst 1.24223→0`）+ 矩阵顺序（`skinLands=35.9878`） |
| 5 | viewmodel-contract | `MUTATION-CAUGHT=PASS`（5/5） | `attachment-bone`、`fov-default`、`hand-matrix`、`companion-missing`、`draw-skipped`，各自变红在一条具名读数上 |
| 6 | viewmodel-animation | `MUTATION-CAUGHT=PASS` | `mutation_red=3/3 hold=4/4`（每个变异移动一条读数、配对一条不可移动的控制） |
| 7 | viewmodel-companion | `MUTATION-CAUGHT=PASS` | `caught=6 of 6`：`mismatch-off`、`range-off`、`bone-count-off`、`model-index-zero-off`、`force-v-model`、`owner-cleared` |
| 8 | material-chain | `12/12 synthetic keys held` | 合成层不受 `--mutation` 影响（它本来就是"拒绝类"断言） |
| 9 | evidence-tree | `nothing under evidence/ moved` | 变异运行同样没有写进已提交的证据树 |

**源码恢复**：变异运行后 `git status --porcelain` 为空（`06-mutation-post-state.txt`），
即每个被临时改写的源文件都回到了变异前的字节（门禁自己也会断言这一点）。

### 与历史记录的计数差异（两处，都是门禁演进造成的，不是本轮改动）

| 计数 | 本轮 | 历史记录 | 出处 |
|---|---|---|---|
| animation 断言数 | 139 | 111（`evidence/verify/23-viewmodel-animation.txt`） | 与主线 README「动画专项 139 条断言」一致；`verify-all.sh:876` 的 `VM_ANIM_OK` 仍钉 111 |
| animation 变异 hold | 4/4 | 3/3（`23-viewmodel-animation-mutation.txt`） | `4f43560` 提交信息明确记载第 23 步有 4 条 mutation 断言曾被钉在猜测值上，已改为按实测配对 |

两处都在总链（`verify-all.sh`）的 pin 上，不在渲染档内。本轮不改动它们（边界：不修改门禁判据），
按 §8 列为总链重跑者需要更新的项。

---

## 5. 证据新鲜度

本轮新抓帧与已提交见证帧逐字节比对：

| 新抓帧 | sha256 | 对照 | 一致 |
|---|---|---|---|
| `paused.bmp` / `paused-again.bmp` | `a61a3e4d…` | `evidence/frame-capture/*.bmp` | 是 |
| `demo.bmp`（bagel） | `fa1bb194…` | 同上 | 是 |
| `demo-installed-map.bmp`（snakewater） | `d086695e…` | 同上 | 是 |
| `fresh-tick3001.bmp` | `86b680bb…` | `evidence/entity-material/fresh-tick3001.bmp` | 是 |
| `tick-2000/4000/6000.bmp` | `7bc06bc7…`/`7bc06bc7…`/`2ed0add0…` | `evidence/verify/22-weapon-animation.txt` | 是 |

同时，三份历史总链记录与本轮输出逐项相同：第 16 步三个帧哈希、第 20 步
`blockDiff=30 frameDiff=434510`、第 22 步三个 tick 哈希。渲染输出没有时间漂移。

`entity-material` 的 `blockDiff=30` 是门禁第 3 节的正例：模型块 6×5 共 30 像素全部与
atlas 见证帧不同，说明"模型用自身材质绘制"这条修复仍然成立。

---

## 6. 运行期间是否产生未跟踪文件

两次运行都是**没有**。

- 正向后（`03-post-run-state.txt`）：`git status --porcelain` 空、`evidence/` 空、
  `git status --porcelain --untracked-files=all | grep '^??'` 为 `(none)`、锁全部释放。
- 变异后（`06-mutation-post-state.txt`）：同上，且源码已恢复。

tier 的全部机器产物落在 `evidence/fast/render/` 与 `evidence/fast/render-mutation/`
（`.gitignore` 第 37 行覆盖 `evidence/fast/`）。

---

## 7. 本轮提交的文件与原因

| 文件 | 原因 |
|---|---|
| `evidence/render-gate-recheck/01 … 06` | 两次运行的前后状态快照与 tier 原始日志，是本轮结论的唯一来源 |
| `evidence/render-gate-recheck/straight/*.txt`（9） | 正向九个门禁各自的完整输出（含每一条断言） |
| `evidence/render-gate-recheck/mutation/*.txt`（14） | 变异九个门禁各自的完整输出，加上 contract 五个变异与 weapon-animation 的失败原文（记录每个缺陷到底变红了哪一条） |
| `evidence/render-gate-recheck/SHA256SUMS` | 上述文件的 sha256，便于复核归档未被改动 |
| `T0-RENDER-RECHECK-2026-10-12.md` | 本文件 |
| `README.md` | 第 9 行的「9/9 尚未在主线上重跑」已不成立，改为实测结果 |
| `CHANGELOG.md` | 按仓库惯例补一条 2026-10-12 的验收记录 |
| `HANDOFF-2026-10-07.md` | 「当前状态覆盖」段补上渲染档已补跑的事实与证据路径 |
| `IMPLEMENTATION-PLAN-2026-10-10.md` | 顶部基线区补一句渲染档补跑结果，避免接手者按旧状态判断 |

未提交：BMP（`evidence/fast/` 下，与已提交见证帧逐字节相同，重复提交没有新信息）、
每次抓帧的 metrics CSV（wall-clock 每次运行都不同）。

---

## 8. 未验证

1. **`verify-all.sh`（24 步总链）**：本任务边界明令不跑。它需要在合并/发版节点、
   干净且单实例的树上跑；届时 §4 的两处 pin（`VM_ANIM_OK` 111 vs 139、mutation hold 3/3 vs 4/4）
   必须一并核实更新。
2. **跨机器抓帧点集合、真实 GPU（本轮为 WARP/默认设备）、人工画面对照**：本机环境外，未验证。
3. **T1/T2/T3/T4/T5**：本轮完全未触碰。
4. 本轮**不构成**"渲染子系统完成"的结论。它只补上"9 个门禁在整合主线上重跑过一次，
   且正向与变异都通过"这一条记录。ViewModel 材质接入产品 draw pass、世界光照、
   音频端到端等仍按 README 记为未完成。
