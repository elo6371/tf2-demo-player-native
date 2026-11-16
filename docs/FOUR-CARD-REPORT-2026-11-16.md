# 四卡交付报告（task-1..task-4，2026-11-16）

版本：v0.3（19:35 快照）。**当前门禁身份 = `06afc166…`（3,066,368 B / 19:33:25）**，由 `release-rebuild.sh` 在 merge `4cca8d41` 上重建（`basis=identity-agrees`、`sourceContent=a22779f7…`），取代此前 `a14ea967` → `216d7f98` 两代身份；正文中 `216d7f98` 的读数一律是**该身份下的历史读数**。v0.2（16:59 快照：并入 E1 第 5 轮结果与合并窗口读数；本文件只写**已实跑且有读数**的结论，未跑/未定性一律标出）
用户要求（m04773）：task-1 工程与集成 / task-2 地图 / task-3 交互 UI / task-4 角色·音频·导出 四张卡全部完成后给完整报告。

## 0. 一页摘要

| 卡 | 任务 | 已落地（有证据） | 仍欠/阻塞 | 当前判定 |
| --- | --- | --- | --- | --- |
| task-1 | 工程与集成（card-arch） | 工具链唯一入口、/W4 告警门、故障注入门、E1 第 3/4 轮门禁矩阵、E4 复用、五探针 same-bytes 复核、合并窗口（三支并入 → 全量重建 `a14ea967` → 6 门读数）、**D-93 判据重做（b233bdae：内容哈希+per-target 依赖集+产物哈希，负控 C9–C12、selftest 47/47、真实门两次绿）、viewmodel-material 十红定性为 stale pins（56dee328）、camera-ui CRLF 修法（2cbe87e5）** | viewmodel-material re-pin 窗口（定性已完成）、companion witness、t2/userinfo/map-geometry/world-material-effects 未跑、A1/A6/A7 口径、exe 身份改钉 `216d7f98` | 未完成（判据已修，re-pin 待窗口） |
| task-2 | 地图（card-maps） | M2a 顶点变换一致（e5fe2219）；M2b **否证**遮挡假设（cec27931）；**R1 已构建并定论：109 px 边界 = 深度写掩码边界/覆盖率补集，只有 50-entities 写过 interior 1181 px（1b762057）**；像素口径闭环更正 | R2 常规路径复现（只有设计）；map-geometry p2 未裁决、rc12 语料缺失；map-material 完整重跑未做（2 份 BLOCKED-INPUT）；50-entities 内逐 range 归属未证 | 未完成（R1 定了 M2b 的边界归属） |
| task-3 | 交互 UI（card-ui） | U1（f1365199）、U2（76d55078）、U3a 小地图线稿（6fa76eb8）、D77 大小写修复（0a1aad40）、oracle 错位修复（8eb6c085）、U3b 窄窗面板（0d2746c1）、**U4 第 1/2/3 步（1e9e18f8 / 2b / c1734a58，含两条真缺陷：g_paneSplit 未赋值、pane1 pick 早消费；单 pane same-sha）**、A1 单位错配解释 | A1 面集纳入规则、t2 判据修正（须负控）、PIP→四窗预算、split ViewModel（已裁决 defer） | 未完成（交互已通，A1 面集仍 BLOCKED） |
| task-4 | 角色/音频/导出（card-char） | P2 关闭（28 断言 0 失败）；P3a 效果可用性（78e58234）；P3b 音频账务根因+修正（d25798ad）；P4 导出能力六项全中；N27 导出先验链（ca3fa348）；**合并树 Phase A/B 全 PASS（产品 decodefail 0/2/2/2、I1–I4 四 demo PASS，b0abac7d）** | N27 产品导出入口（只有设计）；D75 世界 FOV（只复现+设计）；4096 cap 本身仍按设计丢 | 未完成（能力已证，功能未接） |

一句话：**没有一张卡可以宣布"完成"**；task-1 与 task-4 的证据链最完整，task-2 的核心进展是"否证了一个错误假设"，task-3 修掉了两条真缺陷但几何口径仍 BLOCKED。合并窗口已把 task-2/3/4 的四条分支并进主树并全量重建（exe `a14ea967`），同 exe 上 5 门 PASS、minimap-hud 3 红与合并前逐字相同、release-rebuild 1 红是已定性的判据缺陷——**合并本身没有引入新的产品红**，但也**没有让任何一张卡变成完成**。下面按卡给证据。

## 1. 方法与证据纪律（本轮四卡共同遵守的六条）

1. **只认实跑读数**：门禁结论一律来自本次跑出的日志/读数，不引用"上次跑过"或推断；每门给命令、输入、读数、日志路径。
2. **期望值过期与产品缺陷分开判**：期望值随实现变化（几何/材质/口径）时先定性为"期望值过期"，只有能证明产品违反自身不变量时才登记产品缺陷；本报告里两类都标了名。
3. **身份绑定**：每次门禁跑在哪个 exe 上必须可查（sha256 + size + buildUtc + identity sidecar）；文档写的读数必须能追到那一个二进制。
4. **语料替换必须登记**：录像丢失时按"替换件路径 + sha256 + 地图 + 录制类型 + 实测包数 + 替换原因"登记，解析质量断言一条不删；不把替换说成"原文件找回"。
5. **不偷改阈值**：门红了要么修产品、要么带证据改判据（独立提交 + 正负控）；本轮所有门禁阈值保持原值。
6. **审计协议**：提交前 `tools/audit-tree.sh` 必须 OK；门禁自己写脏 `evidence/` 时它会判 FORBIDDEN-WRITE（"lockrun.sh will make every gate exit 97"），此时按"只 add 本次范围路径 → 提交 → 干净树上补跑 audit"执行，并在 commit message 写明原因。

## 2. task-1 工程与集成（card-arch）

### 2.1 工具链唯一入口（已提交 ecbc9b51）
- `native/env-msvc.sh` 变成 10 行 shim → `tools/tf2-toolchain.sh`：先断言 cl/nmake/rc/mt 存在，再 export 同一套 env；`build-cmake.sh` 删掉硬编码常量；`build-target.sh` 失败即 rc=1。
- 证据：**ENV-DIFF=IDENTICAL claims=4**（PATH 2223 B / INCLUDE 350 B / LIB 217 B 逐字节一致；成功时 0 B 输出；坏工具链 rc=1 且 env 不变；HEAD 对照只带半套 env）。identity `git-clean-v1 content=9fd2b1d9…`。
- 边界（要写清）：入口脚本不在 hashed roots 内，**不能凭 content hash 认工具链**，只能凭 ENV-DIFF。

### 2.2 编译告警门与故障注入门（已提交 363aed88）
- `tools/warn-check.sh` + `tools/warn-baseline.tsv`：/W4 读数 **tus=86 new=0 fixed=0 same=9**（基线只减不增，本卡首批清理没有新增告警）。
- `tools/fault-seam-check.sh`：**FAULT-SEAM=PASS seams=winmm injected=9**（9 条注入全部被捕获）。

### 2.3 E1 门禁矩阵（第 3 轮 → 第 4 轮）
- 第 3 轮（`D:/_shared/EXEC-2026-11-15/INST1/E1-round3/summary.tsv`，13 行）：2 绿（release-rebuild PASS-TOKEN 0/31、hud-live PASS-TOKEN 0/234），其余红/UNKNOWN，逐条归因：三份 viewmodel 录像缺（后按用户 8-08 裁决换钉）、userinfo 缺 review1/prodcutscout/73、vmt-nesting 调用没带 owner 锁（`lock=not-held reason=owner-not-set`）、player-model 探针 `STALE-BINARY=1`。
- 第 4 轮（exe `75dae10e60740cd2c3e780ef68c72eb100e6f4221f8c234227a907a99d0607ab` / 3,044,864 B / 13:28:50）：**6 门 PASS** —— player-model（42 s）、vmt-nesting（1 s，36 断言，须用 `hostlock.sh run card-arch env VMT_NESTING_OWNER=card-arch`，`tools/lockrun.sh` 的锁它不认）、minimap-check（2 s）、userinfo（272 s，ASSERTIONS=191 PASS）、release-rebuild（18 s，targets=5 rebuilt=0 fresh=5 basis=identity-agrees）、hud-live（222 s）；辅助 minimap-probe-build / fixtures-backup / fixtures-guard rc=0；**E4 REUSE-OK**（pre/post2 sha 全 `75dae10e…`、`E4-NOOP-RELINK=NONE`、树 `661a4bc5…` 稳定）。
- 第 4 轮两条"新红"被判定为**判据缺陷（非树脏）**：release-rebuild 的 REBUILD-FRESH=FAIL 与 userinfo 的 STALE-BINARY，同因于 **mtime-only 新鲜度判据**——`native/src/main.cpp` 的 mtime 被一次 mutation restore 刷新，内容与 HEAD 一致，判据无法区分"字节复原"与"内容真变"。裁定：本轮不许改判据、不许 touch 源码；用"真重建"求证——删除二进制 + 对应 .obj（或按 target clean）后重建，前后记 sha256：五探针与产品 exe **全部 same-bytes**（entity_overlay `6410e4bf…`、entity_model `5042f555…`、entity_protocol `7e12f982…`、world_geometry `dfbd95db…`、demo_open `54340139…`），误报成立。
- 由此登记一条**架构性自缺陷**：新鲜度判据要改成"源码树哈希 + per-target 依赖集 + 二进制哈希"，并配负控（改内容必红、只复原字节必绿）——独立提交，本轮未做。

### 2.4 exe 身份链（报告必须可追）
- `26bff4361874a5f1…`（3,042,816 B / 10:40，第 3 轮及之前）→ `81af3a4aa35352a8…`（3,045,376 B / 13:20:24，读音 instrument）→ `cfa1ef77b5971340…`（3,044,864 B / 13:24:11，triplet dump）→ `75dae10e60740cd2c3e780ef68c72eb100e6f4221f8c234227a907a99d0607ab`（3,044,864 B / 13:28:50，大小写修复；第 3/4/5 轮门禁全部跑在它上面）→ `a14ea9678be54e079a448541afe1ddad25ce6f47a709d95ad094d415e329f29f`（3,057,152 B / 16:37:08，合并窗口全量重建；**当前**）。前两个已作废。
- identity：`contentBinding=recompiled dirtyTracked=8 dirtyUntracked=2 buildUtc=05:28:27Z`，sourceContent `661a4bc5…`、sourceTree `95d9fe19`。这是**脏树构建**（含未提交的 docs/source-tv 改动），报告照实写。
- 事故入册：删 `native/build-nmake/CMakeFiles/<target>.dir` 会连带删掉 cmake 生成的 build.make，`build-cmake.sh` clean 会静默 rc=2 并删产品 exe；正确做法是只删 .obj 或走 `cmake --build --target clean`。已按"configure + build-target 重建五个探针（74 s rc=0）"恢复，产品逐字节回到 `75dae10e…`。

### 2.5 第 4 轮未跑完（WINDOW-DEADLINE，照实记）
- map-geometry-lighting：1470 s 只到 31 captures / 2 个 pose 组（cp_snakewater p1+p2、koth_bagel_rc11 进行中）。
- viewmodel-material：480 s 只到 §7/9（OK=185 FAIL=0；`:689`/`:691` 两条 pin 本次 OK）。
- viewmodel-companion、minimap-hud：未跑。
- world-material-effects：被我掐掉——`SHARED` 传成 `D:/` 使 capture 路径拼成 `/d/…/D:/…`，属**门禁调用缺陷，不是产品红**，须以 `/d/` 形式重跑。

### 2.6 第 5 轮（15:10–16:21，四门尝试；triage `f8315bc1` + 追补 `5549398e`）
- 主树 exe 全程 `75dae10e…`（未重建），四门结果：**viewmodel-material FAIL**（§1–8 1100 s 被上限截断 + §9 单跑约 2 min：run A 202 OK/7 FAIL、§9 9 OK/3 FAIL——红为 §7 `fallback=3 期望1`、flat 上传纹理=2 期望0；§8 nothing-bound=2、上传=5、绑定=2、仍画几何=3 期望1、BMP-DIFF 0.2816% 期望0；§9 `alpha blend=3 期望1` ×2、`glass slot uploadSlot=-2 期望2`；`:689`/`:691` 两条 pin 仍 OK）、**minimap-hud FAIL**（285 s、96 断言 3 红：A1 `shapes=1243 期望13`（=13+1230，rects 并入、未钉）、A6/A7 vacuity 守卫 15/8 期望≥20（U3a 基底层填充破坏前提，`nothing-drawn` 仍 0）、A12 需构建 skipped；旁证 minimap-check PASS 2 s/27 断言）、**map-geometry cp_snakewater_final1 p1 PASS / p2 WINDOW-DEADLINE**（p1 约 11 min、24 OK/0 FAIL，`cull removed=3467 px/16 faces`、attributable 0.9257% ≤2%，re-pin 重写 pins 与 cull-reference 后 `git diff` 为空；p2 620 s 被 TERM、到 10/11 帧无裁决，其改写的 `frame-pins-p2` 11 条 sha 已还原）、**viewmodel-companion WINDOW-DEADLINE**（0 s 未开跑，待从 8-08 POV 重导，要求 observations≥1000、ticks≥3）。
- **中途树移动已入册**：窗口内 HEAD 从 `997e945e` 走到 `f71ff277`（card-maps 的 `d325ff0f`/`35f8856a` 16:11:52、`f71ff277` 16:18:49），`git show --name-only` 核实只碰 `map-material-matrix.sh`、`source-tv-coverage-check.sh`、`evidence/source-tv*`、`docs/*`，**无 `native/` 或构建输入**；侧车实测 sourceTree `e8cabb154d47`、sourceContent `661a4bc5…`、contentBinding `carried`、dirtyFiles=9（7 tracked + 2 untracked）。本轮未跑 release-rebuild/userinfo，也没有门禁报 identity 不一致；文档已写「若见 identity/STALE 红先按窗口中途提交定性，不动 exe、不 revert 别人提交」。
- 收工核对：exe sha/大小/mtime 未变（3,044,864 B / 14:14:35）、hostlock FREE、无 `.scratch/*.lock`、audit-tree 两次提交前均 `VERDICT: OK`。
- **未定性**：viewmodel-material 的 10 条红与 minimap-hud 的 A1 shapes 口径都**没有**判定为产品缺陷或判据缺陷——需要下一窗口逐条查期望值的出处（哪次提交钉的）与当前行为是否可辩护；不许先改期望值凑绿。

### 2.7 合并窗口（16:29–16:58，新 exe `a14ea967…`）
- **合并**：`1c89aff5`（work/card-ui-u3b）、`7706acaf`（work/card-char-n27a）干净并入 `candidate/e5`；`014da358`（work/card-maps-r1）唯一冲突 `native/src/native_renderer_frame.cpp` 两处 hunk，按口径解决——每 pane 都 dump（保留 `capturePassDepth("70-hud-pre")` + `captureDepthBuffer` 前置条件）、成品 capture（BMP→depth PGM→Present）保留 `if (paneIndex + 1u == paneCount)` 包裹、split 仍 `paneCount==1` 才画 ViewModel、`passDepthIndex_` 只增不清零（文件名天然唯一，未加 pane 索引）。合并后 audit-tree OK。
- **一次全量重建**：`bash build-cmake.sh clean` rc=0 / 193 s / `exe_count=55`。新 exe `a14ea9678be54e079a448541afe1ddad25ce6f47a709d95ad094d415e329f29f` / 3,057,152 B / 2026-11-16 16:37:08；sidecar `sourceTree=014da3588791`、`sourceContent=fb63f9fdb171…`、`contentBinding=carried`、`dirty=0 dirtyFiles=0`、`buildUtc=2026-11-16T08:35:23Z`（dirty=0 是相对「不含 stash 里 docs」的树）。
- **同一新 exe 的门表**（`docs/INST1-MERGE-WINDOW-2026-11-16.md`，提交 `0ada1d81`）：
  - `release-rebuild` rc=1 / 19 s：`REBUILD=FAIL targets=5`。entity_overlay/entity_model/entity_protocol **三探针这次真重建**（sha `c3b5006c`/`28d8e6a9`/`9578fafb`，inputs 都变）；`tf2_demo_native` `state=identity-agrees a14ea967`；**只有 world_geometry_probe REBUILD-FRESH=false**（inputs `ba733a4e→937eae00`、产物仍 `dfbd95db`，与第 4/5 轮同字节）⇒ 与 §2.3 同属 **mtime/content 新鲜度判据类别缺陷**（D93 兄弟），非合并引入、非产品缺陷。
  - PASS：player-model 42 s、minimap-check 2 s（27 断言）、`camera-ui` 111 s（113 OK / 0 FAIL，`paneBoundaryRules=10 paneBoundaryNegatives=13`，T50 全 OK：比较帧 2,582,406 B、paused 帧 271、同一 tick=244）、hud-live 234 s、vmt-nesting 3 s（36 断言）。
  - FAIL：`minimap-hud` 260 s，3 红与第 5 轮**逐字相同**（`shapes=1243 期望13`、`unflipped 15<20`、`mirrored 8<20`）⇒ 非合并引入；阈值未动，仍归 A1 shapes 口径待定性。
  - `camera-ui` 首跑 rc=1 / 5 s 是**门禁自身缺陷**：worktree CRLF 让续行失效、`set -u` 报 `$3 unbound`；恢复成已提交的 LF blob（不改逻辑/索引）后重跑 PASS，收工已 checkout 回、status 干净。修法（门禁自检 CRLF 或改 printf 续行）待入册。
- **未吞功能实证**：单 pane 两跑同 sha `2ba52781…`（`pane: viewport=1264x681 client=1264x681 viewports=1 mismatches=0x0`）；双 pane `--pane-split` 出 `pane: viewport=632x681 client=1264x681 viewports=2 mismatches=0x0` + `panes: count=2 viewports=2 depthTargets=1 depthClears=1 passes=15 hud=1`，split 帧 sha `283b00cd…` 与单 pane 不同 ⇒ `--pane-split` 缺省不改变单 pane 路径。
- 收工核对：audit-tree `VERDICT: OK`、dirty=0、hostlock FREE、无 `.scratch/*.lock`、exe 仍 `a14ea967`；stash@{0} 未 pop（由 lead 收工后 pop 并核对）。
- **本轮未跑（不当作通过）**：t2-visual-matrix、userinfo、world-material-effects、map-geometry、viewmodel-material、companion witness。
- **合并后主树继续前进（17:00–17:33）**：`b233bdae` 把 release-rebuild 判据改成「哈希构建真正读的那些输入」（`release-rebuild.sh` +146/−20、新增 `release-rebuild-selftest.sh` +116），`2cbe87e5` 给 camera-ui 的 CRLF 自缺陷落一行修法。主树 exe 因该自检**重建**而被刷新：`216d7f986574dc350113ad8b08902bc43754915c3ac0868c8c90e8405981d1eb` / 3,057,152 B / 17:33；sidecar `sourceTree=1b762057f47a`、`sourceContent=a73cb80a…`、**`dirty=1 dirtyFiles=52 dirtyTracked=51 dirtyUntracked=1`**、`buildUtc=2026-11-16T09:33:05Z`。
  ⇒ 关键更正：这个 exe 的 **`native/` 源码是干净的**（`git diff 014da358..HEAD -- native/` 为空、工作树 `native/` 无改动），`dirty=52` **全部来自非源码目录**（`evidence/t62-map-matrix/` 被地图门禁刷新 + 我的 docs）。因此它**可以当门禁身份**（viewmodel-material 复测就用它，读数与 `75dae10e` 逐字节同值），但文档必须写清「dirty 计数来自非源码证据目录；exe 身份只能钉 sha，不能靠推导」。
  ⇒ 重建本身是 D-93 判据修正的副作用：首次真实门因 manifest 覆盖面从 `src/` 扩到**全输入**，把 `tf2_demo_native.exe` 一次性重链 `a14ea967`→`216d7f98`（源码未变）。**「主树 exe 保持 a14ea967」这条约束在 17:33 之后不再成立**，后续门禁身份一律用 `216d7f98…`。

### 2.8 D-93 判据重做 + viewmodel-material 十条红定性（b233bdae / 56dee328 / 2cbe87e5）
- **release-rebuild 判据已重做**（commit `b233bdae`，`release-rebuild.sh` +146/−20、新增 `release-rebuild-selftest.sh` +116）：判据 = **源码内容哈希 + per-target 依赖集（`cmake compiler_depend.internal`）+ 产物二进制哈希**；负控 **C9/C10/C11/C12 全过**，selftest **47/47**；两次真实门（同一 hostlock 窗口）run1 `basis=deps-rule-first-run rebuilt=1`、run2 `basis=identity-agrees rebuilt=0`，**world_geometry 不再假红**。日志 `D:/_shared/EXEC-2026-11-15/INST1/E1-merge/rebuild-criterion/real-run-{1,2}.log`。
- **viewmodel-material 十条红已定性为「期望漂移（stale pins）」，不是产品回退**（commit `56dee328`，只改 docs，不动阈值/代码）：复测 sections 7–9 only、exe `216d7f98`、**26 OK / 10 FAIL**，十条红逐条同位；`§8 differing=7272/2582352=0.2816%` 与第 5 轮 `75dae10e` **逐字节同值** ⇒ 读数稳定、可钉。
- **硬证据**：这 10 条断言文本自 `279dabb5`（2026-10-13）起**逐字未改**；`435b4d5c` 换录制只 re-pin 了 §6（`:689/:691`）。机制：`v_sapper_spy` 现在 **3 descriptor**（`ranges=3`、11640/9447/96 vertices、`rangeSlots=-1,-1,-1 / 2,2,2 / -1,1,2`）；`uploadSlot` 在 `952ddf20`（2026-10-17）从「override/drawnSlot」改成常量 −1/−2（全上传，`main.cpp:7969`）；`texturesUploaded` 是**渲染器全局单调计数**且 body pass 也上传（`main.cpp:8170`），§7 的 flat 2 次上传不是本 pass 计数；§8「fallback 帧 == flat 帧」在 3 range 下**必然不成立**（`rangeTextured 0,0,0` vs `0,1,1`）。
- **反证（说明不是产品坏了）**：§7 因果像素控制全绿（1.1447%，世界条带 0，重复跑 byte-identical）；§9 另 9 条全绿（resolved 4/4、alphaSlots=2、11640 vertices）；§8 fallback 机制在位（resolved 4→3、fallbackSlots=1）。文档含 **re-pin 提案**（精确新值 + 退役 §8 整帧相等断言，换成 pmissing 稳定性 + rangeTextured 断言）与 `TRACE_DIR` 绝对路径陷阱（门禁会拼 `$ROOT`，产品 rc=16）。
- **我的裁决**：§7 `:786` 的 uploads 断言**不按全局计数器 re-pin**，改断言对象（换成 pass-local 的 ranges/rangeSlots；`texturesUploaded` 只留「单调不减」sanity 并注明是全局计数）——不需要产品先加 pass-local 计数器。**re-pin 窗口由 card-arch 在 hostlock 释放后独立开**，条件：只动 §7/§8、§9 九绿不动、§8 机制断言保留、**必须配负控**（要能判出「3 range 却只有 1 次 rangeTextured」「descriptor 数量回退」两类真回退）、贴新旧值对照与断言文本 diff。
- **camera-ui 门禁 CRLF 自缺陷已修（提交 `bde74d5d`，`camera-ui-check.sh` +232/−15 + E1 文档 §4.5）**：根因=产品 trace 走 MSVC CRT 文本模式（`native/src/main_trace.cpp:85` `_wfopen_s(..., L"w")`、`:40` `fprintf("...\n")`）、门禁 python driver stdout 也是文本模式 ⇒ 任何 `> file 2>&1` 都写 CRLF，而 `CAMERA_UI_OUT=evidence/camera-ui` 是 tracked 路径，CRLF 即整文件重写 ⇒ `tools/audit-tree.sh` 判 CRLF-DAMAGE（索引侧一直 `i/lf`，CR 只来自检出侧 autocrlf）。修法：新增 `lf_write`/`lf_normalize`/`evidence_normalize_all`/`eol_violations`/`--selftest-eol`/`--check-eol <dir>` 六件 helper，写侧 6 处 + 跑后归一 3 处，读侧 `pvalue` 改 `[^[:space:]]`、i18 awk 先 `sub(/\r$/, "")`，终检只扫本次运行写出的文件（任一 CR ⇒ FAIL）。
- **CRLF 修复的证据**：负控三组日志 `D:/_shared/EXEC-2026-11-15/INST1/E1-round5/eol-nc/` —— `pre-fix.log`（真 e2e.trace 6300 CR / 文本模式 driver 4 CR / 整文件 cp 的 mutated 6300 CR ⇒ 逐条 FAIL、verdict=RED、rc=1）、`post-fix.log`（本次 8 个文本证据 ⇒ GREEN、rc=0）、`selftest-eol.log`（`pre-fix=RED / post-fix=GREEN / EOL-SELFTEST=PASS`）。真实 camera-ui 门（exe `216d7f98…` 跑前跑后同值、head `4c4f2e43`）：**114 OK / 0 FAIL**、`CAMERA-UI=PASS`、rc=0（114 比 113 多的那条就是新增终检「OK no CR in the evidence this run wrote」），跑后 `e2e.driver.txt`/`e2e.trace`/`probe.txt` 全 `i/lf w/lf`，新增 5 个证据文件 CR 全 0。
- **诚实边界**：`--mutation` 分支没跑（要重建 exe，本窗口禁止），其约 30 处重定向只经 `evidence_normalize_all` sweep，文档已标「未实测」。
- **re-pin 已执行（提交 `61fec89`，只动 §7/§8）**：新值 vs 旧 pin —— §7 `fallbackRanges 1→3`；§8 `boundSlots 0→2`、`texturesReused 0→2`、`drawRanges 1→3`；新增 range 表断言（§7 `ranges=3 / rangeTextured=0,0,0`；§8 `rangeTextured=0,1,1 / rangeSlots=-1,1,2`）。两个全局 uploads 数字断言退役，换成 `uploads_monotonic`（只判不倒退，注释写明全局计数 + `main.cpp:8170`/`native_renderer.cpp:202` 出处）。§8 整帧相等退役（实测 `differing=7272/2582352=0.2816%`，3 descriptor 里 2 个仍带纹理故必然不同），换成 `pmissing vs pmissing2` **byte-identical**（differing=0，新增同参捕获）；机制断言（rc=0、missingSlotInjected=0、materialResolved 4→3、fallbackSlots=1、fallbackRanges=1、frozen）全保留。
- **跑门读数**：`SECTIONS="7 8"`、`TRACE_DIR=evidence/fast/viewmodel-material/repin`（相对）⇒ **28 OK / 0 FAIL**、`VIEWMODEL-MATERIAL=PASS`、rc=0；exe 跑前跑后同 sha `216d7f98…`；§7 像素控制与上轮一致（palt 1.1447%、ptex2 1.1264%、两次 pflat byte-identical）。日志 `D:/_shared/EXEC-2026-11-15/INST1/E1-round5/repin-1810/repin-run.log`。
- **负控 c1–c4（必须能判出真回退）**：c1 descriptor 数回退（pflat ranges 3→1、rangeTextured 0,0,0→1,0,0）⇒ **2 红**（draws all three descriptors / leaves every range untextured），26 OK；c2 三个 range 只 1 个带纹理（pmissing 0,1,1→0,0,1）⇒ **1 红**（only the injected range lost its texture），27 OK；c3 上传计数器倒退（readings 2→1）⇒ **1 红**（never went backwards），27 OK；c4 还原 ⇒ 28 OK / 0 FAIL。
- **§9 re-pin 已执行（提交 `3fd6b86`，只动 §9）**：机制先落文档 §3.1.3 再改数 —— `uploadSlot=-2` 与 `--viewmodel-material-slot 2` 同源（`main.cpp:7968-7969` 同一常量既传 `decodeViewModelMaterialSlots` 也写 readings；`viewmodel_material.cpp:344-375` 在 `-2` 时上传全部 resolved 槽；语义出自 `952ddf20`，旧 pin 2 出自 `279dabb5` 的 `flat ? -1 : (slotOverride>=0 ? slotOverride : drawnSlot)`）；`blendedRanges=3`/`alphaAppliedRanges=3` ↔ 3 descriptor（`main.cpp:7917-7931` 的 `slot = slotOverride>=0 ? slotOverride : source.materialSlot`，门禁以槽 2 跑，3 个 range 全取槽 2 玻璃态）；palpha trace 实测 `ranges=3 firstRange=(slot 2, 11640 vertices, textured=1 translucent=1 alphatest=0)`、`rangeSlots=2,2,2`、`rangeTextured=1,1,1`、`boundSlots=1`（去重槽数，与 §8 的 `boundSlots=2` 不矛盾）、`alphaTestedRanges=0`。
- **§9 断言改法（不钉字面量，避免下次 descriptor 数变再漂）**：`blendedRanges == drawRanges`、`alphaAppliedRanges == drawRanges`（判「声明几个 range 就 apply 几份」）；`uploadSlot 2 → -2`；新增 `rangeSlots=2,2,2`；`alphaTestedRanges=0`、`translucent=1`、11640 vertices、`boundSlots=1` 不动。负控 `nc9/n1–n4`：n1 全槽上传回退单槽 1 红、n2 blend 3→1 红（`= 1, expected 3`）、n3 alpha 3→1 红、n4 还原 **13 OK / 0 FAIL**。
- **欠的 full PASS 已还（同窗口，`SECTIONS` 未设）**：**226 OK / 0 FAIL**、`VIEWMODEL-MATERIAL=PASS`、rc=0；§1–§6 共 185 条先全绿再进 §7（`TRACE_DIR=evidence/fast/viewmodel-material/full-1830`，相对）；exe 跑前跑后同 `216d7f98…`；§7/§8 之外无新红。日志 `D:/_shared/EXEC-2026-11-15/INST1/E1-round5/full-1830/full-run.log`。⇒ **task-1 的 viewmodel-material 门从「10 红」到「全绿」，且每次 re-pin 都有负控。**
- **四门矩阵预备件已落（`74307641`）**：`native/tools/run-e1-round5-matrix.sh`（320 行）+ `docs/INST1-E1-ROUND5-MATRIX-PLAN-2026-11-16.md`（130 行），**未执行任何门**。`--dry-run` rc=0：四门命令全解析、`SKIP-PRECONDITION=0`（exe/四探针/fixture/三条 demo/animation-inputs/frame-pins-p1/hostlock 全在）、exe pin `216d7f98…` 命中且每门跑前跑后比 sha（漂移记 `EXE-SHA-DRIFT`）、超时 cap 2400/900/1800/1800 s、某门 FAIL 不早退、证据全相对路径（`evidence/fast/e1-round5-matrix/<utc>/`，gitignored）。hostlock 分派：viewmodel/minimap/companion 用 `hostlock.sh run card-arch`，map-geometry 用 `run t62-mapaxes + HOLD_LOCK=1`（该门自校验 owner，嵌套会死锁）。EOL 两层：矩阵 bookkeeping 走 `camera-ui-check.sh --check-eol` 必须 GREEN，各门证据 CR 只记 `EOL-WARN`（产品 trace 是 CRT 文本模式，D53 家族）。已知副作用只记录不清理：map-geometry 写 `evidence/t62-map-matrix/inputs/{settings.ini,ui.ini}`（untracked）。发令一条 `bash native/tools/run-e1-round5-matrix.sh --run`。

## 3. task-2 地图（card-maps）

### 3.1 M2a：顶点变换一致（已提交 e5fe2219）
- 7 文件 +443/−0，纯增量、全部由 debug 开关守卫；默认路径不跑。
- 读数：view10/view8 顶点级 clip **agree worstRelative=3.955e-07**，阈值 1.0e-05。
- 它当时给出的"109 px 边界是遮挡"结论**已被 M2b 否证，不得再用**。

### 3.2 M2b：遮挡假设被否证（已提交 cec27931 / docs/N05-M2B-DEPTH-ATTRIBUTION-2026-11-16.md）
- 方法：零代码改动，用 `--capture-depth` 回读同一帧。A 帧（掩码+深度）与 B 帧（同参去掉 depth）**逐字节相同**（sha `757f5aa25a9ffded…`）——"开深度"没动一个产品像素；A 与 M2a 参考帧也逐字节相同。
- 深度证据：缎带 109 px 上缓冲是清除值 1.000000，**0/109** 落在 2 quanta 内（这次绘制在这 109 px 上一个深度样本都没写）；interior 1181 px 为 0.992508/0.993225/0.993790，比三角形**更远** 4..112 quanta，**更近 0/1181**。在 LESS_EQUAL + 写 ALL 下更远的表面不可能赢 ⇒ **遮挡假设不成立**。
- 现在指向绘制顺序 / 深度写状态：候选是那批三角形进了 blended batch（`batch.alphaMode==1 -> worldBlendedDepthStencilState_`，write mask ZERO），trace 里 `blendedBatches=1 depthWriteOff=1` 与它一致，但**仍未证死**。
- 界限：只覆盖 capture 隔离路径，常规渲染路径未测。
- 像素口径更正（已接受）：中心采样 view10 内部 1290 / 缎带 109 / flat green 1181，**109+1181==1290 闭环**；整数角点 1288/85/1152 不闭合（差 51）。文档两套口径并列 + 闭环断言。

### 3.3 剩余工作改名（M3/M4a/b/c 定义已随上下文丢失，作废不追认）
- **R1**：per-pass 深度回读仪，判别"世界更晚批次"与"水面 pass"。代码已提交 `e6cf1f61`（隔离 worktree 分支 `work/card-maps-r1`，4 文件 +189/−16：header `setPassDepthPrefix`/`capturePassDepth`、`capture.cpp` `writeDepthPlanePgm` 拆分 + per-pass readback（P5 口径同 `--capture-depth`）、`frame.cpp` **10 个 dump 点** `00-cleared`…`80-post-viewmodel` + 每个深度绑定点 `noteDepthState` 标签、`main.cpp` `--capture-pass-depth`）。**从未构建成功**（verify 锁被 card-arch 第 5 轮连续持有，`lockrun` 重试 60+10 次全 FAIL）⇒ 构建、三组对照、dump 点齐全性、逐 pass 读数、A 帧 `757f5aa2…` 复现**全部待验**。下一窗口第一件事：拿到独占锁 → build → 三组对照（旧 exe `e61c7fd9…` / 新 exe 无开关 / 新 exe 带 `--capture-pass-depth`，三组 BMP 必须逐字节复现 `757f5aa2…`）→ `70-hud-pre` 与 `--capture-depth` 平面逐字节相等的自检 → 逐 pass footprint 判读。
- **R2**：常规渲染路径是否复现该边界。设计已提交 `35f8856a`（`docs/N05-R2-NORMAL-PATH-DESIGN-2026-11-16.md`，85 行：N1/N2/N3 三组、ribbon 109/interior 1181 闭合、判据 A/B/C、失败模式、输出目录 `D:/_shared/EXEC-2026-11-15/INST2/m2d-r2/runs/`），未实现。

### 3.4 map-geometry-lighting 的两类红（第 4 轮已见）
- cp_snakewater：帧 sha 与 `cull kept=617852 removed=22` 参考发生移动。**第 5 轮 p1 已带理由 re-pin 完成**：24 OK/0 FAIL，新读数 `cull removed=3467 px/16 faces`、attributable 0.9257% ≤2%，pins 与 cull-reference 重写后 `git diff` 为空；p2 620 s 被 TERM、10/11 帧无裁决（其 frame-pins 已还原）。
- koth_bagel_rc12：钉住的 POV 录像 `autorecord_2026-05-14_21-28-01.dem` 已不在 `tf/demos`；但 BSP `tf/download/maps/koth_bagel_rc12.bsp` 在，几何门只用 BSP ⇒ 几何照跑。

### 3.5 map-material 语料普查（15:09，lead 实测）
- `map-material-matrix.sh:126-130` 钉的三份 POV **全部丢失**：rc12（153,502 ticks / 49.36 MB）、cp_sunshine（144,583 / 48.96 MB）、pl_odyssey（55,294 / 26.43 MB）；census `evidence/header-scan/header-scan.csv:10/:14/:131` 留着当时身份。
- cp_sunshine 有 POV 替补 `E:/demo测试/cp_sunshine/POV/autorecord_2026-05-17_16-32-36.dem`（75,905,225 B、sha `acc13409…`、POV、219,069 包、四类失败 0）；pl_odyssey 与 koth_bagel_rc12 全机无任何录像 ⇒ 只能按 m06794 先例登记 BLOCKED-INPUT（保留行、写清依据）。
- **已登记 `d325ff0f`**（`map-material-matrix.sh` +78/−2 + `docs/T62-MAP-MATRIX-CORPUS-REGISTRY-2026-11-16.md`）：cp_sunshine 按 REPLACED 接入替换件并钉 sha；pl_odyssey / koth_bagel_rc12 记 BLOCKED-INPUT（保留行、48 字段、不 probe、不算失败），启动逐条打印 override，未登记缺件仍记 failure，汇总行带 `blocked=%d`。`--only=pl_odyssey,koth_bagel_rc12` 自测 **PASS（blocked=2 failed=0）**。**完整重跑未做**（host 锁被 map-geometry 占用）⇒ `readings.tsv` 未刷新，矩阵仍是旧读数。
- **完整重跑已做并 PASS（`05ece883` + `dce98de7` + `66f5017f`）**：`MAP-MATERIAL-MATRIX=PASS maps=5 rows=4 unsupported=0 blocked=4 failed=0`。cp_snakewater_final1 0/2000 ok、相机 orient `1185.5 -813.5 207.1`、probe 148 材质、diff 15140 px（1.7589%）；koth_bagel_rc11 0/2000 ok、orient `914.0 -1000.0 563.8`、99 材质、diff 173212 px（max 255）；**cp_sunshine 替补件实测 BLOCKED-CAMERA**（header 合格 `map=cp_sunshine server_map=cp_sunshine packets=219069`，但相机样本 `0,0,-427`、orient 复跑 `0,0,-359` ⇒ 录像有相机、没有可用视点），只记 probe 读数（170 材质 / 165 tile），不再输出两张一模一样帧；pl_odyssey / koth_bagel_rc12 保持 BLOCKED-INPUT（钉住的录像在 `tf/demos` 已不存在）。`evidence/t62-map-matrix/readings.tsv` 按 8 行重建。
- 门禁本轮修四处：MSYS argv 重写（探针改用 `cygpath -m`）、CRLF 落盘（三处 `sed -i 's/\r$//'`）、退化姿势判定、`0,0,*` 误匹配 `0,0,0`；并清掉 BLOCKED 行残留的旧 tile 文件。

### 3.6 R1 已构建并给出定论（提交 `1b762057`，`docs/N05-R1-PASS-DEPTH-RESULT-2026-11-16.md` +149，另 R2 设计 +54）
- **仪器**：`--capture-pass-depth <prefix>` 在渲染帧 10 个固定点各回读一次 D24S8（mask `& 0x00FFFFFF`、归一化后写 P5 16-bit big-endian PGM），并打 `pass-depth: index=… name=… depthStateLastBind=… wrote=…`。dump 点：`00-cleared` / `10-sky` / `20-world-batchNN-alphaM` / `30-world-done` / `40-water`（该帧未绘水面故缺席）/ `50-entities` / `60-overlay` / `70-hud-pre` / `80-post-viewmodel`。缺 `--capture-frame`、0 张平面或回读失败 ⇒ **exit 16**，不静默产「看起来正常」的帧。代码 4 文件（header +27 / capture +98 / frame +35 / main +45−16），分支 `work/card-maps-r1b`（基于 `0ada1d81`，代码与已合并的 `e6cf1f61` 相同），产物 `bf76cdae…` / 3,057,152 B。
- **三组对照（仪器零渲染影响）**：g1 旧 exe `e61c7fd9`+depth / g2 新 exe 无开关 / g3 新 exe 带 pass-depth —— **三组 BMP 与深度平面逐字节相同**（BMP `757f5aa2…b8b40`、depth `07f513c4…aa1834`）；常规路径 g4/g5/g6 同样不动帧（`b194973c…e707a`、`2babadb1…7632f`）⇒ 旧参考帧在三种配置下都复现。
- **逐 pass 读数（g3）**：足迹 1290 px = ribbon 109 + interior 1181。**深度写入只发生在 `50-entities` 一次**（`d_interior=1181`、`d_ribbon=0`；非 far px 0→223,766），其余 7 个 dump 点变化全 0。结论行 `R1-FIRST-WRITE ribbon=never interior=4-50-entities(1181 px)`、`R1-DEPTH-PLANE-EQUAL TRUE`。
- **对 M2b 的回答**：109 px **从头到尾没有任何一个 pass 写过深度**（8 个点上始终是清除值）⇒ 那条边界是**深度写覆盖率补集 / 写掩码边界**，不是「深度测试谁赢」的边界；pass 粒度上等于 `50-entities` 写掩码在足迹上的补集。M2b 的 `INCONCLUSIVE` 由此变成读数。
- **常规渲染路径（g6，14 张平面）**：3 个 `alpha=0` 批次写深度（773,685→784,479 px，另 +93,086/+17,061/+2,791 变化）、4 个 `alpha=1` 批次不写（`world-blended:write-off`），`50-entities` 再 +11,458 到 795,937；`--capture-depth` 平面与 10/11/12/13 号平面 `ndiff=0` ⇒ 50-entities 之后没有任何 pass 再写深度。与 g3 的 `world-blended:write-off` 互为印证。
- **写入者形态（不依赖 pass 命名）**：interior 存值 vs 三角形自身插值深度 —— 相关 **0.9583**、整体**远约 +20.8 quanta**（min+4 / med+21 / max+112，1 quantum=1.526e-05）⇒ 不是「更近的遮挡物」，更像同一面/共面面的第二次绘制；**具体是 50-entities 里哪一次 range 绘制，未证**（需逐 range trace，不影响边界结论）。
- 未证/下一步：水面 pass 该帧缺席（`40-water` 无平面，需换 `--capture-tick` 另跑）；「9–11 张平面」上界本帧用不到；逐 range 归属未做。证据 `D:/_shared/EXEC-2026-11-15/INST2/m2c-r1/`（runs/、r1-analysis-g3.txt、groups-*.log）。
- **R2 已跑，判据 C 有结论**：N2=g3 复现 A 帧（`757f5aa2…`）；N1=g5、N3=g4 同帧（`b194973c…`）。在 N2 足迹上量 N1 深度：interior nearer/farther/equal = **1/1164/16**、ribbon = **0/103/6** ⇒ **常规路径不复制 109/1181 这个二值边界**（判据 C 成立），但「三角形在 1290 px 内不进深度缓冲」在常规路径同样成立（1267 px 严格更远）。最小区分实验设计已写进 `docs/N05-R2-NORMAL-PATH-DESIGN-2026-11-16.md §8.1`。
- **audit 已回 OK**：`(every changed text file matches HEAD's line endings)` + `VERDICT: OK (no forbidden writes, line endings intact)` ⇒ 之前那条 `CRLF-DAMAGE  FORBIDDEN-WRITE` 的两条来源（card-maps 的矩阵证据 CRLF 写入端、我的 docs 检出侧行尾）都已消解。

## 4. task-3 交互 UI（card-ui）

### 4.1 已提交的里程碑
- `f1365199` U1（N13）：A1–A7 命令语义验证文档，含两条结构性不可达的判定（文档型交付，不是代码改动）。
- `76d55078` U2（N12-U2）：relay 重新指向、队列边界双向、以及"包顺序为何没有自然样本"的论证（文档型交付）。
- `6fa76eb8` U3a 小地图 basemap 线稿：`minimap.h` +43、`minimap_linework.h` +154/−24、`main.cpp` +77、`main_hud.cpp` +128/−2。

### 4.2 两条判定被推翻/修正（重要，报告必须写）
1. **"faces 3869 超过过阈值面 3116 ⇒ 产品面身份缺陷"不成立**——错在离线 oracle：`tools/n14-oracle/bsp_oracle.py:127-128` 把平面表按 16 B/面摊平，而 Source `dplane_t` 是 20 B/面（normal[3]+dist+type），`:157` 的 `pb = planenum*4` 于是除 plane 0 外全错位。例：`pl_upward_f12` face 10020 planenum=539 被读成 nz=169.7056（法线分量不可能），按 20 B stride 读同一面 nz=1.0000。修于 `8eb6c085`，同提交加单位法线自检：旧代码前 8192 平面 **5419** 个不过、首个坏面 |n|=256.000；修复后 **0**。
2. **产品面身份正确**：`minimap: floor triplets` 抽样 **167/167** 与 BSP dface 的 (planenum, side) 一致；`idsOutOfRange=0 idsNegative=0`；idMax 在 mapFaces 内（15147<15155、20440<20449）。按正确 stride 重算期望：Upward pass=4504/floors 3999/**rects 632**、Sunshine 6342/5738/**707**；旧值 3097/4326、570/489 及转录里的 570/3097/2802/583 **全部作废**。

### 4.3 D77 大小写缺陷（真缺陷，已修）
- 根因：小地图地面集的 `tools/`/`editor/`/`skybox` 排除用全小写字面量，BSP 存大写（`TOOLS/TOOLSSKYBOX` 760 次、`TOOLS/TOOLSBLACK` 116、`TOOLS/TOOLSTRIGGER` 465、`SKYBOX/CLOUD001A..D` 各 3），中间没有任何大小写折叠 ⇒ 三条排除一次都没生效。
- 修：`0a1aad40`，只折叠比较副本、存储名不动（+10/−3）。判别读数：`rejectedTool` **0→829**（Upward）/ **0→208**（Sunshine）；faces 3869→**3480**、5338→**5258**；身份行仍干净（idsOutOfRange=0、idMax 同上）。
- `rejectedSky` 仍 0 但有归因：sky 面名 `TOOLS/TOOLSSKYBOX` 先在 tool 前缀被拦下；`skyLower` 普查 Sunshine 只有 24 个三角形、原样 0 ⇒ sky 分支在真实地图上冗余。
- 连带：`1266600a` 更正 N14 文档（旧值/重算值/产品值三列并排 + 作废横幅，不偷改数，点名取代 `a00a32eb`/`a6e85f98`）；lead 的 `f7fa7d3a` 把 D77 标为已修并从缺陷条目里撤回"faces 另有一条独立缺陷"。

### 4.4 A1：单位错配已解释，面集缺口未解（仍 BLOCKED）
- 产品三角表 = **fan(n−2) + 位移面 2·(2^power)²**。按 BSP 重算：水三角 **170/410 全等**、位移三角 Sunshine **24000=24000**、Upward 18560 vs 18528（−0.17%），全量 68364/85939 vs 产品 66707/82859（差 = sky + 0.5~1.5% 残差）。转录是**多边形**口径（非 tool fan 三角 49392/59961、floor 面 3999/5738）。
- 因此早先"kept 6.45×、rects 2×"是**三角形 vs 多边形单位错配**，不是面身份缺陷——`native/include/minimap_linework.h:79-80,208-210` 自己就写明 kept 数三角形、facesKept 才是面数。
- **未解只剩面集**：floor faces 3480/3999（−13%）、同单位 fkept 2084/2802（−26%）。下一步查 floor 表纳入规则（世界几何 vs brush model、water、位移检查失败面）。证据 `D:/_shared/EXEC-2026-11-15/INST3/u3b-a1-evidence.md`。

### 4.5 U3b 小地图面板窄窗（已提交 0d2746c1，worktree 分支 work/card-ui-u3b）
- 缺陷：面板布局假设正方形——`hud_panel.h:83` 只钳宽度，窄窗 `rect.w<rect.h`；`main_hud.cpp:1055/:1113-1115/:1129/:1174` 用 `rect.w` 当高度。
- 修法：`hudLayoutPanels` 宽/高各自钳制（高度在 marginY 之后钳，margin 吃掉的高度不挤进 inset）；新增 `hudPanelFrame()`/`hudPanelPointInside()`，五条边框、viewer 点、控制点、箭头 cull 全按各自轴取盒。3 文件 +157/−22。
- 读数：`hud_probe` rc=0、`failures=0`；新 section 8 `narrow.panel x=8 y=12 w=104 h=160`（(52,120) 内、(52,160)/(105,120) 外；矮客户端 104 高不 drop）；corpus `importedPartial=55 panelsKept=800 panelsRefused=511`；audit VERDICT OK。
- **不染红证明**：同尺寸 A/B，旧 `75dae10e` vs 新 `1a41a495`，1280×720 两侧 `panel=1092,12,160,160`、`drawTriangles=66737 dropped=0`，两张 BMP sha256 同为 `59fbc35f…`、`cmp` 0 字节差。
- **可达性限制（如实写）**：`--window-size` 下限 320×240（`native/src/main.cpp:2757-2766`），触发窄钳制需 client < 172 ⇒ **产品路径不可达**，用例只能放离屏 `hud_probe`（`hud-check.sh:299` 断言 `probe.failures=0`）。修的是布局数学与探针覆盖，不是可复现的用户缺陷。

### 4.6 t2-visual-matrix 三红：单位/方向错，不是产品缺陷（已修，`7ac22f9d`）
- **三条红逐条重定性**：① `vis on input 242109 vs expected 200000` —— 200000 是 **T88 之前的 parser 容量**（从未按地图测过），T88 量得 cp_snakewater_final1 需求 **242361**、容量抬到 400000（`main.cpp:5973`）；改为「与同一次 run 的 requested 比、1% 残差」（252 的差来自 displacement grid 中途失败后 resize 回吐，`bsp_map.cpp:1309-1313`）。② `vis on hidden 25995 vs expected 15039` —— 15039 是 `world-material-check.sh` **自己机位**的数（`:472`），hidden 是相机属性；本门 orbit 机位 25995（trace 自述 `609 kept by a cluster past the first`，即 any-leaf）；改为 census 推导 `hidden = clustered − drawn_withoutCluster`，并要求 `0<hidden<clustered`、`drawn>=withoutCluster`、两向恒等式。③ `on vs vis-off ≥5%` 实测 **0.0000%** —— **方向反了**：PVS 裁掉的按定义从本帧相机看不见，正确裁剪必须像素中性；旧的 19.59% 是 T80/T84 之前 clip 查了别的相机。改为 `--max-delta 0`（两帧逐字节相同）。
- **读数**：前向 58 OK/3 FAIL → **assertions_ok=68 PASS**；`--mutation` 注入三缺陷：m1 旧容量 **5 红**（附带 differing=239 px，证明像素对不是恒真）、m2 可见集反号 **1 红**（census 全绿，只有像素抓得住）、m3 无 cluster 也算 hidden **3 红**；恢复重建后 exe sha 回 `43b59b72…`，最终 **assertions_ok=72 PASS**。证据 `D:/_shared/EXEC-2026-11-16/e9-t2-matrix/`（pre-fix/ forward/ mutation/ final/ identity.txt），文档 `docs/E9-T2-MATRIX-UNITS-2026-11-16.md`。
- **四条对后续有用的发现（都进了文档）**：① **门禁普查漏洞**——`t2-visual-matrix.sh` 在仓库根、不叫 `*-check.sh`，历次门禁 glob 普查**漏掉它**，它才长期带着旧钉值；建议把这类根目录脚本纳入 registry 普查。② **243397 不可复现**：全树与 `D:/_shared/EXEC-2026-11-16` 各子目录都 grep 不到；仓库权威 census 是 `evidence/t88-maps/demand.txt` 的 **242361**（probe 显式 200000 上限截断过，别拿它的 200000 当 census）。③ hidden 数**不能跨门借用**（它是相机属性），world-material 的 15039 保持不动。④ **容量截断会连带破坏像素中性**（m1 的 239 px）⇒「旧 200000」是真的会丢可见几何，这条现在有变异证据。

### 4.7 U4 第 1 步：所有权边界（已提交 1e9e18f8，worktree 分支 work/card-ui-u3b）
- 交付：`native/include/pane_boundary.h`（新，103 行）——`PaneCameraSource{None,Orbit,DemoView}`、`PaneBoundaryReadings`、`paneBoundaryMismatches()` **7 个位**（R1 targets==1、R2 depth==1、R3 viewports==1、R4 viewport==client 且非零、R5 camera!=None、R6 hudPasses<=1、R7 跑 HUD 就必须 scissor）；纯头文件谓词，probe 不链渲染器也能测。
- 读数采集：`Renderer::draw()` 在绑定点记读数（`native/src/native_renderer_frame.cpp:42-62`）、建 view 矩阵处记相机来源（`:162-165`）、HUD pass 后补两项（`:1303-1306`）；`drawOrdinal` 单调 ⇒ 陈旧读数不能冒充干净；访问器 `native/include/native_renderer.h:1326-1333`。
- 产品 trace **只增一行**（`native/src/main.cpp:14279-14291`，旧 `capture:`/`hud:` 行一字未动）：`pane: draw=1 viewport=1264x681 client=1264x681 targets=1 depth=1 viewports=1 camera=demo-view hud=1 scissor=1 mismatches=0x0`；`camera=` 是画帧真正用的相机，不是 `scene: camera` 那个请求。
- 负控：`native_ui_probe` 新 section（`:301-364`）晴朗读数=0，**8 个负控逐条覆盖全部 7 位**，HUD 关闭帧必须仍干净；probe 失败由 `camera-ui-check.sh:589` 的 exit 断言自动拦，未改任何 gate 脚本/阈值。旧行逐字保留。
- 证据：`CAMERA-UI=PASS`（含 T50 产品脚本）、`audit-tree VERDICT: OK`；同尺寸 A/B（old `75dae10e` vs new `076823d6`）两张 BMP sha 均为 `0e00f553…`、`cmp` 0 字节，old trace 无 `pane:` 行、new 有 ⇒ **像素零变化、trace 只增不改**；两图（pl_upward_f12 / cp_sunshine）产品 capture 均 `camera=demo-view hud=1 scissor=1 mismatches=0x0`。证据 `D:/_shared/EXEC-2026-11-15/INST3/u4-step1/`。
- **所有权审计表**（`docs/U4-PANE-OWNERSHIP-2026-11-16.md`，17 行：现所有者 / 写者 file:line / 读者 / reset 点 / 第二 pane 会怎样 / 裁决）：**6 项必须 per-pane**——viewport、世界 view 矩阵、orbit 相机、observer focus、demo 相机（即「第一/第三/自由」）、投影 FOV；**11 项 shared**；demo 时钟是**显式 shared-by-design**（seek 同步就靠它）。
- 教训入册：新头文件未 `git add` 时 `gen-build-stamp.cmake` 直接 **D-90 REFUSED**（untracked compile input），不是随机失败；不许用 `TF2_IDENTITY_ALLOW_UNTRACKED` 绕过。
- 第 2 步口径（已批 4 条件）：谓词形态可改但**改谓词/改 gate 必须单独提交并配负控**（负控须抓出两 pane 共用 viewport / 共用 view 矩阵 / 共用相机三类真错）；单 pane 路径每提交 same-sha A/B + 现有门禁全绿；只做渲染侧最小骨架（同一 demo、共享时钟、每 pane 独立 viewport/相机/mode），先不接交互、不动 N16 的点击激活与 seek 语义；两 pane 时 depth target / 绘制 pass / HUD pass 数必须能在 trace 里数出来（为 PIP→四窗预算做账）。

### 4.8 U4 第 3 步：交互与每 pane 相机（已提交 c1734a58，worktree 分支 work/card-ui-u4s3，6 文件 +768/−24）
- **交付**：`native/include/pane_input.h` 新增 `paneForClientX(x,width,panes)`（半开区间 [0,width)、split at width/2、越界 −1、单 pane 独占、count 钳到 kPaneMaxPanes）；`main.cpp` 加 `g_activePane`，`WM_LBUTTONDOWN` 激活光标下 pane（`paneActivate` trace），space/右拖/滚轮/follow 都路由到 active pane；pane 1 拥有独立 `CameraModeController` + `SpectateDirector` + pinned target + accept/reject 计数，director 紧跟 pane 0 更新（同一快照、同一 scene tick，**没有第二个时钟**）；renderer 加 `setPaneCameraFocusWorld`/`setPaneCameraDemoViewWorld`/`clearPaneCameraDemoView`/`orbitPaneCamera`/`zoomPaneCamera`；CLI `--pane-activate N`、`--pane-click X WIDTH`、`--pane-cycle`、`--pane-mode N first|third|free`、`--pane-follow N ENTITY`（argv 顺序即语义，`--pane-split` 须在前）；probe 覆盖 `paneForClientX` 20 cases / 7 negatives。
- **抓到并修掉两条真缺陷**：① `--pane-split` 只设了局部 `paneSplit`，`g_paneSplit` 从未赋值 ⇒ 激活/路由/pane1 director/相机块全不执行、第一次 `--pane-cycle` 落到 pane 0（靠一对「本该不同却逐字节相同」的控制组抓出）；② pane 1 的 pending pick 在第一个 follow 快照前就被消费 ⇒ 被拒、pane 停在 target 2；改为 `g_followSnapshotReady` 才采纳，trace 增 `pane1Pick: accepted=/rejected=`。
- **像素 A/B**（pinned `--capture-tick 2000`、同一 demo、1264×681、左右半区逐字节、1,291,176 B/半区）：单 pane 与本步构建**逐字节相同**（左 `de4a64b4…`、右 `26dfb247…`，0 B diff）；split 静止态 2b vs 本步相同（`295a4dac…`/`34c66bce…`）；`--pane-mode 0 first` 与 split 基线两半都相同；pane1 free-look + `--pane-follow 1 1` 两半相同 ⇒ **仅 follow 不动像素、只有 mode 变才动**。pane1 first+follow：右半 **428,755 px** 变化（原是目标躯干填满 pane，靠隐藏该 body 修掉）、左半 120 px @(0,463)-(23,491)（trace `pane1 target=2 src=follow hidden=2`、`cameraFilter removed=2 indices=2,3`）；pane1 third：左 0 / 右 101,586（hidden=-1）。pane 0 mode 变：左 430,206 / 右 13,411 @(766,119)-(1221,440)，且 free vs third 两半**逐字节相同** ⇒ 右半变化来自 frame-global hidden-body 集合，不是相机。
- 探针 rc=0（`paneBoundaryRules=10 paneBoundaryNegatives=13 paneCameraSources=3 paneInputCases=20 paneInputNegatives=7`），`camera-ui-check` = CAMERA-UI=PASS，audit-tree VERDICT: OK。
- **已知代价（我的裁决：本周期接受，记入文档）**：hidden bodies 是**整帧全局 instance 列表的并集**（两 pane 共用 instance buffer/常量/surface）⇒ 为任一 pane 隐藏的 body 在两边都消失（实测 120 px 与 13,411 px 两处）；正解是 per-pane instance list / per-pane 上传，明确**不在本步**。
- **split 下 ViewModel：维持不画（我的裁决）**：单 pane 路径不变；在 split 画需要先有 per-pane 投影 FOV/aspect 与每 pane viewmodel pass 的资源账（PIP→四窗预算的前置）⇒ 记为**已知限制**与将来做法，不是缺陷。
- 时钟/seek 仍 shared-by-design（第 3 步不拆时间），文档写明「split 下 seek 同步是设计不是巧合」。

### 4.9 minimap-hud 三条红：判据单位错（已修，`62aba085`）
- 改动：`minimap-hud-check.sh` + `native/src/main_hud.cpp` + `docs/E9-MINIMAP-HUD-GATE-UNITS-2026-11-16.md`（371+/23−）：**marker 与 interior 按各自单位测量**，不再拿同一个字面量套两种量。
- 读数：门禁全量 **assertions=100 / failed=0  PASS**；`--mutation` **4/4 抓到**并恢复后同 sha；前向 exe 不变（`43b59b72…`，只改门禁与一处读数）。证据 `D:/_shared/EXEC-2026-11-16/e9-minimap-units/`。
- 这条与 §4.6 是**同一类病**：门禁把「某个历史构建的读数」当常量钉住，产品换了单位/机位后仍按旧值判红 —— 不是产品回退。

### 4.10 未完成
- **U4 第 1/2/3 步已完成**（1e9e18f8 / 2b 步 / c1734a58）；仍欠：**PIP→四窗资源预算**（要真资源账，不是估算）、split ViewModel（明确 defer，需 per-pane 投影）、hidden-body 并集的正解（per-pane instance list）。定义取 `docs/PROJECT-EXECUTION-ROADMAP-2026-11-15.md:109`。
- **A1 最终处置**：面集纳入规则查清后重钉，或保留 BLOCKED 并写清缺口。
- ~~t2 判据修正~~ **已完成（`7ac22f9d`）**，三条红均为单位/方向错、配 m1–m3 变异负控；minimap-hud 三条红同样已修（`62aba085`）。
- **两条门禁脚本已在 `4cca8d41` 并入 `candidate/e5`**（merge `work/card-ui-u4s3`：11 文件 +1615/−103）⇒ 主树源码已含 U4 第 3 步 + E9 两处修法；**主树 exe 已重建为 `06afc166…`（3,066,368 B / 19:33:25，`basis=identity-agrees`、`sourceContent=a22779f7…`、sidecar `sourceTree=b935e052`、dirty 全为非源码），矩阵 `PINNED_EXE_SHA` 已更新并重跑 dry-run（`DRY-RUN=OK gates=4`、无 SKIP-PRECONDITION，提交 `16b04592`）**。

## 5. task-4 角色/音频/导出（card-char）

### 5.1 P2 已 PASS 关闭（不再重跑）
- 28 断言 0 失败；ATTACH-WEAPON-BUDGET=PASS；frame=yes；trace 10,100,518 B；frame.bmp 2,582,406 B sha `dfaf5500…b36053`；attach supplied=13 refused=0 E_OUTOFMEMORY=0；layer A cap=64 cand=98 admitted=64 built=64 deferred=34 failed=0；layer B cap=96 resident=86 evictions=0；layer C missingMesh=0；layer D droppedPlayers=0；world prepared=64 uploaded=64。日志 `D:/_shared/EXEC-2026-11-15/INST4/p2-n19/attach-budget/`。
- 事故根因（已修）：`attach-weapon-budget-check.sh` 在仓库根、整文件 196 个 CRLF 使反斜杠续行失效，`--demo` 等被当命令执行、末行纯重定向把 rc 变成 0；修复 + 加 D93 EOL 守卫（第 33 行 grep CR 即 FATAL）。

### 5.2 P3a / N20 效果可用性（已提交 78e58234）
- 数据面 `--scan`：temp_events=68133 temp_failures=0 particles=986 explosions=1873 firebullets=4307 projectiles=1538。
- 产品跑 tick 84000 speed 32：tecached=16384，933 条 fxDue 后 692 条恒 1538。结论两条都不软化：**没有粒子绘制 pass**、**计数本身被截断**。
- 据此登记 **D76**（`f2b3cdb`）：三个 16384 静默 cap 违反本仓 `kFollowQueueMax`/`g_followQueueOverflows` 的计数标准。

### 5.3 P3b 音频账务（4096 上限是设计；旧计数把丢弃混进解码失败，已修正）
- 逐字段命中：temp_events=68133 particles=986 explosions=1873 firebullets=4307 projectiles=1538；sounds=35278 failures=20923 matches=5482 misses=46 messages=25017 dropped=20921 decoded=5528。
- 四条算术断言全中：25017−20921=4096（cap 打满）、20923−20921=2（真正提前中止只有 2 条）、5482+46=5528、68133>16384。
- **根因（任务 B 查实）**：旧代码把 payload 丢弃后仍拿空 payload 进解码循环，首读失败即 ++failures，所以 failures = drops + 真失败。修法是在解码循环加 captured && 守卫（native/src/demo_header_sound.cpp:71/:100），并把总数拆成 rawSoundMessagesDropped + …PayloadTooLarge + …MessageCap（native/include/demo_header.h:823-829）。**这是修 bug，不是放宽断言**。
- 读数（提交 d25798ad，worktree work/card-char-n27a，四个 demo 同构建树）：failures 11018→**0**（bagel_rc13）/ 20923→**2**（upward）/ 8308→**2**（snakewater）/ 18288→**2**（bagel_rc11）；dropped 与 decoded/matches/misses 前后全同。恒等式 dropped == payload_too_large + message_cap、messages − dropped == 4096、failures <= 4096、有丢弃时 failures < dropped，四 demo 全 PASS。
- payload_too_large **结构上不可达**：payloadBits 可靠路径 8 bit、非可靠路径 16 bit ⇒ 上限 65535 < 65536，恒 0；能被输入推动的只有 message_cap（4096 条上限）。
- 产品侧：代码路径确实走这条受限 payload 链（native/src/main.cpp:4439 parseDemoHeaderFile → native/src/demo_header.cpp:180 type 17 → readSounds；:7168 buildSoundTimeline；:7309-7312 打印），既有产品日志与 probe 改前逐项相同（bagel_rc13 sounddecodefail=11018、snakewater 8308、bagel_rc11 18288）。但**产品本次未重建未运行**，所以「产品账务已修正」还不成立。
- 边界不变：**未证明没有丢音频**——4096 条上限仍按设计丢，每条丢消息仍记一次 failure（现在记在正确的原因字段）。
- **合并树 Phase A（产品读音，提交 `b0abac7d`）**：worktree `t105-e5-n27a` 执行 `git merge candidate/e5`（基线 `0ada1d81`，含 `014da358`）→ merge commit `d5336cb9`；`build-target.sh tf2_demo_native demo_open_probe` rc=0。被测产品 exe = worktree 构建 `eda6889bc45f97e4166b6652d4d269f554be46135788d2ab1a4c439ad7f407f2`（3,057,152 B / 17:02，跑前跑后 same-sha）；**主树门禁 exe 未动，仍是 `a14ea967…`（构建前后各核一次）**——两者大小相同、sha 不同（构建路径/调试串差异）。产品 TITLE 四行原始读数：
  `koth_bagel_rc13 sounds=15114 soundevents=19289 sounddecoded=5234 sounddecodefail=0` / `pl_upward_f12 25017 35278 5528 2` / `cp_snakewater_final1 12402 15942 5292 2` / `koth_bagel_rc11 22382 28637 5244 2` ⇒ 与 §9 期望表**逐项相同**，`VERDICT: PASS (product prints split sound accounting on merged build)`。
- **Phase B（同树 probe 恒等式）**：probe `314ab6b2…`（663,552 B / 17:02，由上述合并树现场构建；与修复 commit 的构建 sha 相同 ⇒ 可复现），四 demo `pass_verdicts=4 (expect 4)`、`PROBE_VERDICT: PASS`：I1 `dropped==payload_too_large+message_cap` = 11018/20921/8306/18286（payload_too_large 恒 0）；I2 `messages−dropped==4096` 四个全 4096（cap 仍按设计打满）；I3 `failures<=4096` = 0/2/2/2；I4 `failures<dropped` = 0<11018、2<20921、2<8306、2<18286。
- 证据口径（重要）：产品 TITLE 行只打 sounds/soundevents/sounddecoded/sounddecodefail 四个数，**不打 sound_dropped/message_cap** ⇒「产品账务字段已拆」这一层只有 probe 能证；且被测产品是 worktree 合并树构建（`eda6889b`），**不是**门禁矩阵 exe（`a14ea967`）本人。证据 `D:/_shared/EXEC-2026-11-16/INST3/taskB-merged/`（merged-run.log、merged-run/manifest.txt、product/*.driver.txt、probe.txt、build-merged.log、sound_product_check.sh、probe_identity_check.sh）。

### 5.4 P4 导出能力六项全中 + 一条口径缺陷
- Q1 h264=yes(filtered 4)、Q2 aac=yes(1)、Q3 bothStreamsAdded=yes、Q4 rgb32Accepted=yes needsOwnNv12Converter=no、Q6 streamCount=2 firstFrameDecoded=yes、throughput ok=yes（100 帧 220 ms / 419,212 B）。
- 口径缺陷：同进程 det_a==det_b（`fcde8895…`），但与 13:00 那次 `57e3b37d…` 差 20 字节；逐字节定位全是容器时间戳（moov/mvhd、视频 trak tkhd+mdhd、音频 trak tkhd+mdhd 的 creation/modification_time），mdat 完全相同，mvhd 差 2427 s = 运行间隔 ⇒ **媒体编码确定、容器时间戳不确定**。若要声称 byte-identical 导出，需 Finalize 后清 12 个时间字段再比。已写进 `docs/N27-EXPORT-PROBE-RERUN-2026-11-16.md` §9（`e8cabb15`）。
- 重申：产品**仍没有导出入口**，P4 是能力证明，不是导出功能通过。

### 5.5 N27 导出先验链（任务 A，15:22 收口，worktree 分支 work/card-char-n27a，提交 ca3fa348）
- 一条可复跑的链：`native/tools/mf_export_verify.sh <exe> <workdir>`，全绿才 PASS（证据 `D:/_shared/EXEC-2026-11-16/INST3/taskA/`）。
- 输入先验：`--dump-inputs` 落 frames.rgb24（60 帧共 2,764,800 B，逐帧 sha256）+ pcm.raw（384,000 B = 96,000 sample frames，48k stereo 16bit 2.0 s，sha256）；**外部 sha256sum 与探针自算一致**，不是自证。
- 编码只从 artifact 走：`--encode-dump` 得 frameShaMatches=60/60、pcmShaMatch=yes、videoSamplesWritten=60、audioSamplesWritten=94、bytes=207,556、全 S_OK。
- 确定性分两层：负控（间隔 2 s 两次编码）RAW_DIFF_BYTES=10；`--normalize` 独立步骤清 5 个 box（`fieldsZeroed=5 / bytesZeroed=40 / traks=2 / verify_nonzero_fields=0`），`cmp` 恰好 40 字节且两个文件都是，正控 norm_a==norm_b sha256 `428abe60…` 与上一次运行相同。⇒ **媒体确定、容器时间戳不确定**有了可执行判据（也把 §9 的「12 个时间戳」更正为 10 个 = 5 box × 2 字段）。
- 回读：`--decode` 视频 60/60（meanAbsDiff=1.958 / max=2.129）、音频 96,256 vs 96,000（delta 256 ≤ 2048）、pcmSimilar=yes。
- 两条实现事实（修 bug 不是放宽断言）：单 `IMFSourceReader` 不能同时解码两轨（视频 EOS 后音频读 0 样本，须另开 position 0 reader）；AAC 有损，解码 PCM 断言改为「样本数差 ≤2048 + 平均绝对样本差 ≤600」。
- 边界不变：**产品仍无导出入口**，这是导出能力的可验证链，不是导出功能通过。docs `N27-EXPORT-PROBE-RERUN-2026-11-16.md` §10。

### 5.6 两份设计（19abe673，全是方案、未实现）
- `docs/N27-PRODUCT-EXPORT-ENTRY-DESIGN-2026-11-16.md`（124 行）：入口选 **CLI `--export-video`**（不选菜单，理由：现有证据全走 CLI、参数解析器已有硬拒绝语义）；**单线程同步导出**，逐帧「tick 固定时间基 → playback.evaluate → renderer.draw → readback → SinkWriter」，绕开 `native/src/main.cpp:14151` 的墙钟节流（现状 draw 8.3 ms / tick 66.7 Hz 两套时钟，负载一重就每十 tick 才画一次）。状态归属：tick/相机归现有 playback，readback 归 Renderer，writer 归新 `demo_export.cpp`，混音复用 `native/include/audio_player.h:18-21` 的离线 mixer + `main.cpp:7201` 的 outcome sink 缝，**导出期间不开音频设备**。9 条失败模式（含 1264×681 奇数高必须拒绝或补边、先写临时文件成功后 rename）。验证只在 worktree 构建，`--export-dump-inputs` 落 frames/pcm 后套 task A 的 `mf_export_verify.sh` 判据 + 负控（删 1 帧 / 截 512 样本必须红）。改动面：`native_renderer_capture.cpp:88` 拆 readback、`main.cpp` 四处选项、`tf2-product.cmake:16-17` 加源、`:157` 追加 `mfplat mfreadwrite mfuuid`。
- `docs/D75-WORLD-FOV-FIX-DESIGN-2026-11-16.md`（114 行）：先补三条读数——① `viewmodel-material-check.sh` 真正比较的是 trace `gpu: viewmodel materialReadings` 的九项材质计数器（`:626` 解析、`:688` fallbackSlots、`:692` fallbackRanges，fallback 只有注入故障才可达），世界 FOV 变宽变窄**不改这些计数器** ⇒ 该闸门对 D75 两方向都不敏感，拿它当 D75 证据是误用；② `player-model-check.sh` 里 `[Ff][Oo][Vv]` 命中 0 且**不启动产品**（只跑 `entity_model_probe.exe` / `entity_overlay_probe`，两个探针无 fov 字段）⇒ 不消费产品 world FOV；③ demo 观测（`autorecord_2026-07-02_13-26-46.dem` tick 53976、`mode=4 follows=1 targetIndex=3`）：`m_iFOV=0`、`m_iDefaultFOV=90`、`m_iFOVStart=20`、`m_flFOVTime=809.505005`，oracle 只有这四个字段、**没有 `m_fFOV`**。
- D75 源选择（口径）：`worldFov = m_iFOV>0 ? m_iFOV : (m_iDefaultFOV>0 ? m_iDefaultFOV : 90)`，取**被观看目标**实体（不是观察者），不乘 `weapon_fov`（本仓无此 convar、Source 世界投影也不乘），`m_iFOV` 只在变化时网络化 ⇒ per-entity 缓存 + 目标切换（`targetSerial`）重置。legacy 优先级：`--world-fov` > ini `worldFov` > ini `viewModelFov`(legacy) > demo 值 > 90，否则旧闸门静默落回 90。
- **基线已按该口径重拍（16:29–16:36，主树 exe `75dae10e…` / 3,044,864 B / 14:14:35，hostlock 独占，demo bb841c6d… tick 2000、ViewModel 关）**：每级 2 次全一致——C90 ini=90 无 CLI `442489af…`、A40 ini=40 `5825a98e…`、B120 ini=120 `57b77baa…`；像素差 A40 vs C90 99.9207%（max_delta 241）、B120 vs C90 96.4585%（231）、A40 vs B120 98.4553%（252，与旧 RESULT.md 同值）⇒ **D75 在新构建上复现**。旧 `e61c7fd9…` 的 C90/A40/B120 sha 与新拍完全一致，但按协议只算跨构建像素稳定旁证、不当正控。两条诚实现：① 第一版 harness 把 D40/E120 两臂的 ini 也改成了 40/120（旧实验是 ini90+旗标），两臂与 A40/B120 同 sha、答不了旗标问题；修正后的 F40flag/G120flag（ini90 + `--viewmodel-fov` 40/120）均 `442489af…`，与 C90 逐字节相同（0/860,784 像素不同）⇒ 旗标不碰世界相机。错误臂与正确臂都留档在 `runs/` 与 `manifest2.txt`，未删。② 16:37 E1 合并窗口把主树 exe 换成 `a14ea967…`（3,057,152 B / sourceTree 014da358 / dirty=0），这 12 次运行全在替换前结束，harness 自记跑前/跑后 sha 均 75dae…。证据 `D:/_shared/EXEC-2026-11-16/D75/rebaseline-75dae/`，文档 `docs/D75-WORLD-FOV-FIX-DESIGN-2026-11-16.md` §7。

### 5.7 未完成
- N27 **产品导出入口实现**（方案已定，排 E1 第 5 轮收工后的合并窗口；硬约束：奇数尺寸入口处拒绝或补边、同步单线程、导出期间不开音频设备、临时文件成功后才 rename）。
- 4096 账务修正已提交但**未合并未重建**（d25798ad 在 work/card-char-n27a，等 E1 收工后批量合并→重建→重跑）。
- D75 世界 FOV 实现（同排合并窗口；实现必须带负控：旧 ini 只有 `viewModelFov` 时世界投影与改动前逐字节相同）。

## 6. 横切：门禁与缺陷登记

- **D72 锁修复**（`69992e0c`）：`tools/lockrun.sh` 用 winpid+nonce，误杀活锁变成正确拒绝；selftest L8/L9、Git Bash PASS 30。
- **D75**：世界相机 FOV 字段缺失——在新构建 `75dae10e…` 上重拍复现（A40 vs C90 99.9207%、B120 vs C90 96.4585%、A40 vs B120 98.4553% / max_delta 252；同级两次逐字节一致）；负控 `--viewmodel-fov` 40/120 与 ini90 基线逐字节相同（0/860,784 px）⇒ 旗标不碰世界投影、ini 键在转世界相机；旧 `e61c7fd9…` 的 BMP sha 与新拍一致只当跨构建稳定旁证。**修法（给世界相机独立 FOV 字段）未实施**。
- **D76**：三个 16384 静默 cap（`f2b3cdb`）。
- **D77**：材质排除大小写失效（`6fdd2043` 登记、`0a1aad40` 修复、`f7fa7d3a` 标已修）。
- **source-tv 覆盖门禁**：九份钉住录像在读性上 5 份原位、4 份替换（review1/gullyscout/processdemo/review44）、review12 由用户补件恢复 ⇒ 活动集 **9/9、pin 955,845**；正负控均 rc=0（正向 55 ok / 0 fail，负控 per_demo_reds=9、red_rows=18）。口径：九槽全覆盖 ≠ 原始九份都回来了（`71c983cb`、`997e945e`，文档 `docs/GATE-INPUT-REPLACEMENT-2026-11-16.md` §9–§11）。
- **架构三件**：N22 工具链唯一入口（`ecbc9b51`）；N23 /W4 + 故障注入（`363aed88`）；N25 第 4 轮（捕获复用已证）；N26 历史测量（编译动作 634→137、clean build 648 s→221 s，**本轮未按口径重测**）；N24 main 拆分**未开工**。

## 7. 附录

### 7.1 本轮相关提交（时间序，最新在前）
```
d25798ad p3b: count designed sound-message drops apart from decode failures (worktree branch)
ca3fa348 n27: verifiable export chain (dump/encode/normalize/decode) + verify script + readings (worktree branch)
0d2746c1 Clamp panel height as well as width; measure both axes in the minimap (worktree branch)
997e945e source-tv: accept cp_reckoner recovery; nine-input active set 9/9 (pin 955,845)
71c983cb source-tv: audit eight active recordings; register three replacements + review12 BLOCKED-INPUT
8bb99c34 docs: round-4 E1 triage (75dae10e identity, probe rebuild evidence, window outcome)
e8cabb15 T105 E5 N27/P4: the export rerun is deterministic in the media and not in the timestamps
0ada1d81 docs: merge-window record (three branches, single rebuild a14ea967, seven gates, pane evidence)
b0abac7d p3b: record merged-tree Phase A/B sound accounting PASS (product 0/2/2/2, I1-I4)
50cacb37 docs: four-card report v0.2 + project overview
b5f9fa7f docs: four-card report — merged-tree sound accounting Phase A/B PASS
1b762057 docs(N05): R1 per-pass depth readout — 109 px boundary is a depth-write coverage boundary
b233bdae gate(rebuild): hash the inputs the build actually reads (D-93 criterion fix)
2cbe87e5 docs(e1): the camera-ui gate defect gets a one-line fix
c1734a58 u4: pane activation, per-pane camera/mode routing, pane-input boundary (step 3)
56dee328 t1: the viewmodel-material reds are stale pins, not a product regression
```

### 7.2 证据目录
- E1 第 3/4 轮：`D:/_shared/EXEC-2026-11-15/INST1/E1-round3/`、`D:/_shared/EXEC-2026-11-16/INST1/…`
- source-tv 门禁：`D:/_shared/EXEC-2026-11-16/INST5/source-tv/gate-positive5.log`、`gate-mutation5.log`
- P2/P3/P4：`D:/_shared/EXEC-2026-11-15/INST4/p2-n19/attach-budget/`、`docs/N20-EFFECTS-AVAILABILITY-2026-11-16.md`、`docs/N27-EXPORT-PROBE-RERUN-2026-11-16.md`

### 7.3 未完成清单（诚实版）
1. task-1：**viewmodel-material 已全绿**（§7/§8 28/0 `61fec89` + §9 re-pin `3fd6b86` + 9 节 **226 OK/0 FAIL** full PASS，每步配负控）；仍欠 companion witness、t2/userinfo/map-geometry/world-material-effects 未跑、A1/A6/A7 口径、camera-ui 门禁 CRLF 修法落地；exe 身份**已再次变更**：`216d7f98` → **`06afc166`**（U4 第 3 步 + E9 修法并入后重建，`basis=identity-agrees`，3,066,368 B / 19:33:25；矩阵 pin 已更新 `16b04592`，dry-run `DRY-RUN=OK gates=4` 无 SKIP）。
2. task-2：**R1/R2 均已出结论、map-material 完整重跑 PASS**（`1b762057`/`05ece883`/`66f5017f`，audit 回 OK）；仍欠 50-entities 内逐 range 归属、水面 pass（需 `--capture-tick` 另跑）、map-geometry p2 裁决、cp_sunshine 需一段带正常视点的 POV（现在的替补件是 BLOCKED-CAMERA）、odyssey/rc12 需补录像。
3. task-3：A1 面集规则、t2 判据修正（配负控）、PIP→四窗预算、hidden-body 并集正解、split ViewModel（已裁决 defer）。
4. task-4：N27 导出先验链已完成（ca3fa348），仍欠产品导出入口（只有设计）；4096 账务修正已并入合并树（014da358），合并树 Phase A 产品读数 0/2/2/2 与 Phase B 四条恒等式**已跑通**（b0abac7d；被测产品是 worktree 合并树构建 eda6889b，主树门禁 exe 未重建）。
5. 横切：D75 世界 FOV 修法、N24 main 拆分、N26 按口径重测。
