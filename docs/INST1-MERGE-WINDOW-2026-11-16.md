# INST1 合并窗口（2026-11-16）—— 三支合并、一次性重建、七门读数

执行人：E1 合并窗口（candidate/e5）。窗口开始 HEAD 5549398e，结束时 HEAD 014da358。
本文件只记录**实测**：每个读数都来自新 exe `a14ea967…` 的本次运行，未跑的项列在 §8，不当作通过。

## 1. 结论摘要

* 三条分支按序合并（不 rebase、不 squash，原作者提交原样保留）：`1c89aff5`（card-ui-u3b）、`7706acaf`（card-char-n27a）、`014da358`（card-maps-r1，唯一冲突）。
* 只重建一次（clean 全量，193 s），新 exe `a14ea9678be54e079a448541afe1ddad25ce6f47a709d95ad094d415e329f29f` / 3,057,152 B / mtime 2026-11-16 16:37:08。
* 七门：**PASS 5**（player-model、minimap-check、camera-ui、hud-live、vmt-nesting），**FAIL 2**（release-rebuild、minimap-hud）。两条红的定性见 §6，均**不是合并引入**。
* 未吞功能已验证：`paneBoundaryRules=10 paneBoundaryNegatives=13`；单 pane `viewport==client mismatches=0x0` 且同参数 A/B 帧同 sha；双 pane 预算行 `count=2 viewports=2 depthTargets=1 depthClears=1 passes=15 hud=1`；split 下 ViewModel 仍只在 `paneCount == 1` 时画。
* 合并前的工作树脏文件（含 lead 交付物 docs 与 .gitattributes）在 `stash@{0}`，**未 pop**（§3）；构建与读数基线因此**不含**这些 docs。

## 2. 合并表

| 序 | 分支 | tip | merge commit | 变更 | 冲突 |
|---|---|---|---|---|---|
| 0 | candidate/e5（起点） | 5549398e | — | — | — |
| 1 | work/card-ui-u3b | 3fb33728 | `1c89aff5` | 10 files +1099/−81（新增 native/include/pane_boundary.h、docs/U4-PANE-OWNERSHIP-2026-11-16.md） | 无 |
| 2 | work/card-char-n27a | 19abe673 | `7706acaf` | 16 files +1589/−303 | 无 |
| 3 | work/card-maps-r1 | e6cf1f61 | `014da358` | per-pass 深度回读仪（10 个 dump 点 + `--capture-pass-depth`） | **native/src/native_renderer_frame.cpp，2 处 hunk** |

`native/include/native_renderer.h` 与 `native/src/main.cpp` 自动合并（无冲突标记）。合并后 `git status` 0 项，`bash tools/audit-tree.sh` = `VERDICT: OK`，native/ 无未跟踪编译输入（D-90：`pane_boundary.h` 已被 gen-build-stamp 纳入内容哈希）。

## 3. stash 与构建基线（重要）

合并前工作树有 5 个 tracked 脏文件（.gitattributes +14 条 eol=lf 规则；docs/N-STATUS、docs/N17-N20-CHAR-COVERAGE、docs/PROJECT-NEXT-TASKS、docs/PROJECT-ROADMAP-HANDOFF）与 3 个 untracked docs。为让合并与构建有确定的树，先：

```
git stash push -u -m 'merge-window: preserve pre-merge dirty docs/.gitattributes'
```

stash **至今未 pop**（`git stash list` 第一条仍在），里面有 lead 的交付物 `docs/FOUR-CARD-REPORT-2026-11-16.md`、`docs/PROJECT-EXECUTION-ROADMAP-2026-11-15.md`、`.gitattributes` 与四份 N/PROJECT docs；**按 lead 要求不许 drop、不许 clean，由 lead 自己 pop**。

因此：新 exe 的 `dirty=0 dirtyFiles=0` 是相对于「不含这些 docs 的树」而言，不是「工作树一直干净」。文档与读数都在这个基线上。

## 4. 构建与身份

```
bash build-cmake.sh            # clean 全量；08:35:20Z → 08:38:33Z ≈ 193 s，exe_count=55
```

* exe：`a14ea9678be54e079a448541afe1ddad25ce6f47a709d95ad094d415e329f29f`，3,057,152 B，mtime 2026-11-16 16:37:08。
* 侧车 `native/build-nmake/tf2_demo_native.identity`：`sourceTree=014da3588791`、`sourceContent=fb63f9fdb1712fd7cdbf05600edfade04c3397a9ab50091bf5ded5b3d74e5d12`、`contentBinding=carried`、`contentMethod=git-clean-v1`、`sourceDateEpoch=1794817937`、`dirty=0 dirtyFiles=0`、`buildUtc=2026-11-16T08:35:23Z`。
* HEAD 与绑定一致：构建与全部读数期间 HEAD 均为 014da358（candidate/e5），与侧车 `sourceTree=014da3588791` 一致；工作树除 `stash@{0}` 里的 docs 外干净。`37a872b3`（card-char 的 P3B §9 准备提交，`git show --stat` 只改 docs/P3B-SOUND-ACCOUNTING-SPLIT-2026-11-16.md +159/−1）**只在 `work/card-char-n27a` 分支上**，未进主树，与本窗口 exe 绑定无关。

## 5. 门表（全部在新 exe a14ea967 上实跑，同一 hostlock 批次）

| 门 | rc | 秒 | 关键读数 | 结论 |
|---|---|---|---|---|
| release-rebuild | 1 | 19 | `REBUILD=FAIL reason=source-newer-than-binary targets=5`；entity_overlay_probe/entity_model_probe/entity_protocol_probe **真的重建了**（sha 分别 c3b5006c… / 28d8e6a9… / 9578fafb…，inputs 均变化）；tf2_demo_native `state=identity-agrees sha=a14ea967…`；仅 world_geometry_probe 报 `REBUILD-FRESH=FAIL reason=inputs-changed-binary-not-rebuilt inputs_sha256_was=ba733a4ecdf1 now=937eae0002b6 binary_sha256=dfbd95dbc98f` | FAIL（判据类别，见 §6.1） |
| player-model | 0 | 42 | `PASS player-model-check: negative controls named-failed; A lod0Truncated=0; B paintedSlots per slot x family; C separateAnimSource=1.` | PASS |
| minimap-check | 0 | 2 | `assertions_ok=27 assertions_bad=0 MINIMAP-CHECK=PASS` | PASS |
| camera-ui（含 T50） | 0（首跑 1，门禁自身缺陷，见 §6.3） | 111 | 113 条 OK/0 FAIL；probe 行 `paneBoundaryRules=10 paneBoundaryNegatives=13 paneCameraSources=3`；`cameraDecisionMatrix=6`、`layoutCases=42 layoutOverlap=0 layoutOutOfRange=0 layoutControls=19`、`hudFields=5`；T50 关键读数：比较帧 2,582,406 B、冲突绑定拒绝=2、未绑定键派发=0 条→`an unbound key dispatched nothing = 1`、restore-defaults 回默认=1、聚焦文本框吞两键=2、paused 帧=271、paused 中相机移动=21、书签应用=10、两张比较帧同一 tick=244、脚本运行隐藏且未开音频设备 | PASS |
| hud-live | 0 | 234 | `HUD-LIVE-CHECK=PASS` | PASS |
| minimap-hud | 1 | 260 | `MINIMAP-HUD=FAIL assertions=96 failed=3 skipped_controls=1`；三条红见 §6.2 | FAIL（非合并引入） |
| vmt-nesting | 0 | 3 | `VMT-NESTING=PASS (36 assertions)`（在 hostlock 内以 `VMT_NESTING_OWNER=e1-merge` 跑） | PASS |

## 6. 红分流

### 6.1 release-rebuild：world_geometry_probe 的「输入变了但二进制没重建」

* 现象：只有 `world_geometry_probe` 一条 `REBUILD-FRESH=FAIL`，其 inputs 摘要 `ba733a4ecdf1 → 937eae0002b6`，而二进制 sha `dfbd95dbc98f` 与第 4/5 轮**完全相同**。
* 事实：本次是 clean 全量构建，55 个 exe 全部重出（合并让输入确实变了：三个 entity 探针的 inputs 与新 sha 都变了）；world_geometry 的「两遍 build 均 rc=0 但 nmake no-op、二进制同字节」说明它的源码输入在内容上未变，只是内容摘要集合（依赖集/时间戳参与）变了。
* 定性：与第 4 轮已入册的 D93 兄弟缺陷同类——**mtime/content 新鲜度判据的类别缺陷，不是合并缺陷、不是产品缺陷**。判据修正（content hash + per-target 依赖集 + 正负控）仍留独立提交，本窗口未改判据、未 touch 源码。

### 6.2 minimap-hud：三条红与第 5 轮逐字相同

```
FAIL the panel handed over thirteen shapes = 1243, expected 13
FAIL the unflipped position is inside the panel (so this is not vacuous) = 15, expected >= 20
FAIL the mirrored position is inside the panel (so this is not vacuous) = 8, expected >= 20
```

* 三条读数与第 5 轮（exe 75dae10e）**逐字相同**，不是合并引入。
* A1：1243 = 13 + 1230（基层地物 rects 计入 shapes），与 lead 已确认口径一致，**不钉**，待 card-ui 重定口径。
* A6/A7 是 vacuity 守卫：U3a 基底层填充破坏了「未翻转/镜像位置没有环」的前提（15/8 < 20），与 A1 同源，一并交 card-ui/语义 owner。
* 本窗口未改阈值、未改判据。

### 6.3 camera-ui 首跑 rc=1：门禁脚本自身缺陷（已修复到可运行，不是产品红）

* 首跑 5 s 退在 `camera-ui-check.sh: line 415: $3: unbound variable`，当时已完成 18 条断言全 OK。
* 成因：该工作树文件是 CRLF 而索引 blob 是 LF（`git ls-files --eol` 显示 `i/lf w/crlf`，其余门 `w/lf`）。CRLF 让 :618/:620 的续行反斜杠后面多了 \r，续行失效，`assert_eq` 只收到 2 个参数，`set -u` 下取 `$3` 报错。
* 处理（不改逻辑、不改阈值）：把工作树字节恢复成已提交的 LF blob（`git cat-file blob HEAD:camera-ui-check.sh > camera-ui-check.sh`，index/HEAD 未动），重跑 rc=0 111 s、113 OK/0 FAIL；收工 `git checkout -- camera-ui-check.sh` 后 `git status` 干净、该文件 `w/lf`。
* 记录在此，避免下次把它当产品红。
* **一句话修法（不必现在改）**：门禁起手先自检自己的字节 —— `case "$(tr -cd '\r' < "$0")" in *$'\r'*) echo "FATAL: $0 is CRLF (worktree checkout rewrote it; see .gitattributes)"; exit 1;; esac` —— 或把 `*.sh` 的 `eol=lf` 写进 `.gitattributes` 并在门禁里断言 `git ls-files --eol -- "$0"` 为 `w/lf`。这样 :618/:620 这类续行在任何检出形态下都不会静默失效（本次 CRLF 命中正是：续行反斜杠后多了 \r，`assert_eq` 只收到 2 参，`set -u` 取 `$3` 报错）。

## 7. 「功能没被吞」的 pane 实证（新 exe 实拍）

同一产品捕获口径（`--tf-root … --hidden-window --audio-disabled --settings-file … --ui-settings-file … --demo pl_upward_f12 --capture-frame … --capture-tick 600 --hud-live --hud-minimap --debug-camera 0 -2048 512 0 33`），脚本 `/d/_shared/EXEC-2026-11-15/INST1/E1-merge/pane-evidence.sh`，证据目录 `…/E1-merge/pane/`：

```
singleA rc=0 51 s bmp_sha256=2ba52781872005ec2639cf558fcbb5ec69a102dd1791e0d6a332c1391297b07d bytes=2582406
singleB rc=0 49 s bmp_sha256=2ba52781872005ec2639cf558fcbb5ec69a102dd1791e0d6a332c1391297b07d bytes=2582406
split   rc=0 51 s bmp_sha256=283b00cd2b425061656c304993cd4ad9ba9e21ff18aa4b00d8e6d1fc0c9151f4 bytes=2582406
```

* 单 pane 预算行：`pane: draw=438 viewport=1264x681 client=1264x681 targets=1 depth=1 viewports=1 camera=demo-view hud=1 scissor=1 mismatches=0x0` ⇒ `viewport == client`、无 mismatch；**同参数两跑帧 sha 完全相同**（A/B 同 sha，单 pane 路径稳定）。
* 双 pane 预算行：`pane: draw=426 viewport=632x681 client=1264x681 targets=1 depth=1 viewports=2 camera=demo-view hud=1 scissor=1 mismatches=0x0` 与 `panes: count=2 viewports=2 depthTargets=1 depthClears=1 passes=15 hud=1` ⇒ 两个 pane 合成、一块 surface、depth 整面只清 1 次、HUD 1 次、mismatch 0，与 U4 文档记录的预算行一致。
* split 与 single 的帧 sha 不同（283b00cd… vs 2ba52781…），说明 split 确实改变了画面而不是把单 pane 重画一遍。
* 代码层保留项复核：`if (paneCount == 1u) drawViewModel();`（split 不画 ViewModel）、HUD 由最后一个 pane 画、成品 capture（BMP→depth PGM→Present）只在最后一个 pane 执行、`capturePassDepth("70-hud-pre")` 每 pane 都 dump、`passDepthIndex_` 只增不清零（per-pass 文件名跨 pane/跨帧天然唯一，无需加 pane 索引）。
* paneBoundary 语义读数：`paneBoundaryRules=10 paneBoundaryNegatives=13`（7→10 规则 + 8→13 负控全部在，未因合并退回单 pane 谓词）。

## 8. 未跑 / 未证清单（不当作通过）

本窗口没有跑的项：t2-visual-matrix、userinfo、fixtures-backup/fixtures-guard、world-material-effects、map-geometry（四图 + snakewater p2 未完成）、viewmodel-material（第 5 轮整门读数仍是旧 exe 75dae10e 的）、viewmodel-companion witness、material-coverage、其余矩阵门。

* 这些门的最近读数是旧 exe 的，**不能**代表合并后的产品；新 exe 只覆盖上表七门 + §7 的 pane 实证。
* 合并树虽未改渲染主路径（三支分别动 pane 边界/HUD、demo 声音分账、per-pass 深度 dump），但「未跑」不等于「无回归」。
* 收工状态：hostlock FREE、无 `.scratch/*.lock`、`git status` 0 项、exe 仍是 a14ea967、stash@{0} 仍在。

## 9. 复现

```
cd /d/TF2_Native_Worktrees/t105-e5
bash tools/audit-tree.sh                      # VERDICT: OK
bash build-cmake.sh                           # 新 exe，本次 193 s
bash tools/lockrun.sh e1-merge -- bash release-rebuild-check.sh
bash tools/lockrun.sh e1-merge -- bash player-model-check.sh
bash tools/lockrun.sh e1-merge -- bash minimap-check.sh
bash tools/lockrun.sh e1-merge -- bash camera-ui-check.sh
bash tools/lockrun.sh e1-merge -- bash hud-live-check.sh
bash tools/lockrun.sh e1-merge -- bash minimap-hud-check.sh
bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run e1-merge env VMT_NESTING_OWNER=e1-merge bash vmt-nesting-check.sh
bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run e1-merge bash /d/_shared/EXEC-2026-11-15/INST1/E1-merge/pane-evidence.sh
```

## 10. 纪律

* 未改任何门禁阈值/判据；未 touch 产品源码；未 revert 任何他人提交。
* 证据只写 `/d/_shared/EXEC-2026-11-15/INST1/E1-merge/`；主树 `evidence/` 被门禁改写过的文件已 `git checkout` 还原。
* stash 未 pop、未 drop；`.gitattributes` 与 lead 交付物仍原样保存在 stash@{0}。
