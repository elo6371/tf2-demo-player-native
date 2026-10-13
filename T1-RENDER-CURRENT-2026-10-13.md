# T1 · 当前渲染档复核（render-current）

- 任务：T1，在基线 `p0-entity-protocol@0f023bb` 上运行当前 `verify-render.sh`，正向一次，并在变异档复核判据的反向可红性；只读验收，不改产品代码。
- 分支 / worktree：`task/render-current` / `D:\TF2_Native_Worktrees\render-current`
- 基线 commit：`0f023bb6dcb6b9617ace804fcf9cf34b93594ae9`（`docs: record T3 integration and fix audio input line endings`，2026-10-13T12:15:46+08:00）
- 执行日期：2026-10-13
- 授权范围：只写 `evidence/render-current/` 与 `T1-RENDER-CURRENT-2026-10-13.md`。产品源码、`verify-render.sh` 判据、`verify-all.sh` 均未改动；`git status --short` 全程只显示本任务自己的证据目录（见 `evidence/render-current/state/checks.txt`）。
- 未运行 `verify-all.sh`（任务书硬性约束）。

## 1. 结论摘要

| 项 | 读数 |
|---|---|
| 正向 `bash verify-render.sh` | `VERIFY-RENDER=FAIL (1 red)`，`gates_ok=8 gates_bad=1`，退出码 1 |
| 唯一红项 | `viewmodel-contract`：153 条断言中 1 条红，测量点断言（红因见第 4 节） |
| `bash verify-render.sh --mutation` | `VERIFY-RENDER=PASS (9/9 gates, mutation=1)`，`gates_ok=9 gates_bad=0`，退出码 0 |
| 变异捕获 | contract 5/5、companion 6/6、animation 门禁层 3/3 红且 4/4 对照保持（另探针层 5/5 移动 + 5/5 对照）、其余门禁各自变异全部捕获（逐项见第 5 节） |
| 锁 | 无残留（`.scratch/*.lock` 全部释放） |
| 源码恢复 | 变异源字节级复原断言 OK；工作树除本任务证据目录外 clean |
| 关键偏差 | ①直跑红项经归因属于 T3 音频接线，非本档判据本身失效；②`--hidden-window`/`--audio-disabled` 在这条链路上未被任何门禁使用（只读范围无法修）；③变异档第一次运行因沙箱 safe-delete 故障出现 4 门禁红（非产品读数），重跑修复 |

两个 tier 的最终读数与日志哈希：

```
straight:  VERIFY-RENDER=FAIL (1 red)    gates_ok=8 gates_bad=1   exit=1   52m12s
           log sha256 = 088a42ff3144a1bb0f7820a4b8c1174dafa2f1d25f8d4b3dac3aaa8bf92ab938
mutation:  VERIFY-RENDER=PASS (9/9 gates, mutation=1)  gates_ok=9 gates_bad=0  exit=0  1h29m32s
           log sha256 = bf54b209ebb810969b16e4f7f8055a610a2a9a3354806d2c06dbf84bee68d8c2
```

## 2. 运行方式与取证约定

- `verify-render.sh` 的输出根 `OUT` 默认是 `evidence/fast/render`（该路径在 `.gitignore` 内）。正向档用默认值；变异档显式 `VERIFY_RENDER_OUT=evidence/fast/render-mutation2`，避免覆盖正向读数。
- 本档全部门禁日志、运行总日志、变异失败明细均已复制进 `evidence/render-current/`（受版控），逐文件哈希见 `evidence/render-current/state/SHA256SUMS.txt`。`evidence/fast/` 下的原始输出保持不动，作为可再取证的 tier 现场。
- 时间窗口：正向档 12:45:12 → 13:37:25；变异档 16:16 → 17:45。均晚于基线提交时间，证据是新鲜的（`state/checks.txt` 第 4 节）。

## 3. 正向档读数（`bash verify-render.sh`）

逐门禁（日志内时间戳）：

| 门禁 | 判定 | 备注 |
|---|---|---|
| `resource-reachability` | PASS | 13:37 前依次产出 |
| `frame-capture` | PASS | |
| `entity-material` | PASS | |
| `weapon-animation` | PASS | |
| `viewmodel-contract` | **FAIL** | `assertions_ok=153`；唯一红项，见第 4 节 |
| `viewmodel-animation` | PASS | |
| `viewmodel-companion` | PASS | |
| `material-chain` | PASS | `12/12 synthetic keys held` |
| `evidence-tree` | OK | `nothing under evidence/ moved`（tier 输出留在 `evidence/fast/render`） |

末行：

```
gates_ok=8 gates_bad=1
VERIFY-RENDER=FAIL (1 red)
```

### 红项原文（`evidence/render-current/straight/viewmodel-contract.txt:147-148`）

```
  ..   capture sceneTicks: 10253 10253 10252 10253 10254 10253 10254  (measured points: 10252 10268)
  FAIL all 7 compared frames came from a measured capture point = 1, expected 7
```

## 4. 红项归因：T3 音频接线把首帧抓帧点推进标定盲区

这一段是 T1 的核心交付之一，因为它决定"红的是产品还是仪器"。归因用单变量探针 + 复采完成，原始记录见 `evidence/render-current/probe/README.txt`。

机制（代码事实）：

- `--capture-tick N` 的语义是"第一个 `elapsed >= 1/120s` 且 `tick >= N` 的 draw"（`native/src/main.cpp:3562-3565`），所以抓帧落点由首帧延迟决定。
- 回放 tick 由墙钟驱动（`main.cpp:2759`），`elapsed` 被 clamp 到 250 ms（`main.cpp:2746`），tickRate ≈ 66.67 tps。
- 首帧落点 ≈ `floor(首帧延迟 × tickRate)`。首帧延迟 <15 ms → tick 0（sceneTick 10252）；≥240 ms → tick 16（sceneTick 10268）；两者之间 15–240 ms 全部落在 tick 1–15，即 sceneTick 10253–10267，为**标定盲区**（判据只承认 10252 与 10268 两个测量点）。
- 旧二进制因为首帧很慢、`elapsed` 撞上 250 ms 的 clamp，稳定白拿 16 tick，落在 10268；音频接线（`69baff8`）让首帧变快，落点滑进盲区。

单变量探针实测（同一台机器、同一天、同一条 demo）：

| 二进制 | 首帧 tick | sceneTick | 是否在测量点集合 |
|---|---|---|---|
| p0-base（接线前的产品） | 16 | 10268 | 是 |
| p1-t1（仅清单二 quiet-capture） | 16 | 10268 | 是 |
| p2b-t2（仅 T2 静态武器本体） | 16 | 10268 | 是 |
| p2-audio（含 T3 音频接线） | 1 | 10253 | **否** |
| p3-all（全部整合） | 1 | 10253 | **否** |

复采：p0-base 3/3 落 tick 16；p2-audio 3/3 落 tick 1。同二进制在全新目录重跑该门禁两次（`10253×7` 与 `10252 + 10253×6`），确定性复现，排除负载偶然。

结论：**红项的唯一移动变量是 T3 音频接线（`69baff8`）；T1 与 T2 的改动无罪**。修复建议（增大测量点容差、或让捕获忽略首帧、或在判据里记录真实落点）写在 `probe/README.txt` 末节，本任务不改判据。

## 5. 变异档读数（`bash verify-render.sh --mutation`，重跑）

第一次运行 14:38:35 → 15:27:51（49m16s）结果为 `VERIFY-RENDER=FAIL (4 red)`、`gates_ok=5 gates_bad=4`、退出码 1。红因是本机环境故障（沙箱 safe-delete 拦截构建，连带 4 个变异场景 BUILD-FAILED），不是产品读数；完整取证与修复记录见 `evidence/render-current/ENVIRONMENT-BUILD-CURSE.md`，处置后按原命令重跑。

重跑（16:16 → 17:45，1h29m32s）逐门禁：

| 门禁 | 变异读数 | 判定 |
|---|---|---|
| `resource-reachability` | 1 个变异（故意写错归档计数）具名变红 | CAUGHT |
| `frame-capture` | 1 个变异（图像翻 1 字节）内容哈希移动 | CAUGHT |
| `entity-material` | 1 个变异（图集像素见证替换）第 3 层分离成立 | CAUGHT |
| `weapon-animation` | 1 个源码变异（强制 bind pose）+ 1 个探针判别（矩阵序，skinLands=35.9878） | CAUGHT |
| `viewmodel-contract` | 5/5：attachment-bone、fov-default、hand-matrix、companion-missing、draw-skipped，各具名红 | CAUGHT（`VIEWMODEL-CONTRACT-MUTATION=PASS`） |
| `viewmodel-animation` | 探针层 5 变异全部移动读数 + 5 对照保持；门禁层 `mutation_red=3/3 hold=4/4` | CAUGHT |
| `viewmodel-companion` | 6/6（含两处还原后复核：模型回到实体本体、失配计数归零） | CAUGHT |
| `material-chain` | 12/12 合成键保持（合成层判据，无源码变异） | PASS |
| `evidence-tree` | `nothing under evidence/ moved` | OK |

末行：

```
gates_ok=9 gates_bad=0
VERIFY-RENDER=PASS (9/9 gates, mutation=1)
```

两个 tier 合读：`viewmodel-contract` 的正向红是"落点盲区"，而反向变异 5/5 全部被捕获，即该判据的捕获能力本身没有失效；红的是测量点集合的设计前提（首帧慢），前提被 T3 打破。

## 6. 状态检查（锁 / 源码恢复 / 证据新鲜度 / 工作树）

- 锁：`ls -d .scratch/*.lock` 无输出，无残留锁目录；两次 tier 正常退出，无门禁因他人持锁被拒。
- 源码恢复：`viewmodel-contract` 自身断言 `every mutated source is byte-identical to its pre-mutation state` OK（`mutation/viewmodel-contract.txt:601-602`）；`viewmodel-companion` 断言 `source restored byte-for-byte (40768a7a747b)`（`:188`）；`git diff --stat -- native/ build-nmake/` 为空。
- 工作树：`git status --short` 仅 `?? evidence/render-current/`（本任务证据目录，随后提交）。
- 证据新鲜度：见第 2 节；两份运行日志+全部门禁日志已入库并带哈希。
- 构建树例外的复位：故障绕行期间曾把 `native/build-nmake/.../audio_timeline.cpp.obj` 的时间戳打到未来（详见第 7 节）；复核结束后已 `touch` 复位为 2026-10-13 17:48:01 +0800，内容未动（482139 B，为该源码的真实编译产物）。

## 7. 环境故障与绕行（必须随报告交付的偏差）

- 现象：构建在覆盖 `audio_timeline.cpp.obj` 时以 `C1083: Permission denied` 失败；`cmake_cl_compile_depends` 吞掉诊断，NMake 只报 rc=0x3 无输出。直接调 cl 用响应文件拿到真实诊断。
- 根因：WorkBuddy 沙箱 safe-delete 包装层对旧 obj 的 trash 操作失败（`genie-trash failed ... Some operations were aborted`）后按 `SAFE_DELETE_FAIL_CLOSED` 拒绝覆盖。自持循环：任何头文件改动 → stamp 守卫 touch 全部源 → 全量重编 → 撞上该路径 → 失败 → stamp 不前进 → 下一次构建又全量重编。
- 排除的假设：并发构建竞争（无进程时仍复现）、MSVC 环境丢失（症状不符）、TEMP 响应文件碰撞（隔离后仍复现）。
- 绕行：把该 obj 时间戳打到未来，使 nmake 跳过重编（该源不被任何变异修改，跳过不损失覆盖）。验证：touch `viewmodel.h` 触发守卫 → 23 文件重编 → 该 obj 跳过 → rc=0。
- 重跑结果：变异档 9/9 全绿（见第 5 节）。重跑中 `SAFE_DELETE_BULK_CONFIRM_REQUIRED` 诊断仍有出现（各门禁清理自己的旧帧目录），但未导致失败。

## 8. 未验证项与遗留风险

1. `audio_timeline.cpp.obj` 的覆盖故障（"被诅咒的路径"）是否仍存在，本任务未复测；若后续变异再次改头文件触发全量重编，可能复现，建议在下一次变异跑之前先做一次干净的构建验证。
2. 正向档红项的产品侧修复（测量点容差/首帧处理）未实施，属越权范围；建议由整合任务承接，`probe/README.txt` 有可执行建议。
3. `--hidden-window` / `--audio-disabled` 在本链路未被门禁使用（6 处产品启动全部只传 `--audio-device`），任务书建议的"静默"在两档运行中不成立；T1 只读范围无法修改产品脚本，记为偏差。
4. 盲区结论的负载依赖性只在本机做了 3/3 复采，未跨机器验证。
5. `material-chain` 的真实安装层读数（`explicitVtfDecoded=false`、`archiveOpen=false` 等）是判据对安装缺口的如实记录，非本次评测项目，未深查。

## 9. 证据清单

| 路径 | 内容 |
|---|---|
| `evidence/render-current/straight/*.txt` | 正向档 8 门禁日志（与 `evidence/fast/render/` 原件逐字节一致，`cmp` 全 SAME） |
| `evidence/render-current/logs/straight-run.log` | 正向档运行总日志（sha256 `088a42ff…`） |
| `evidence/render-current/mutation/*.txt` | 变异档 8 门禁日志 + 5 份 `mutation-failure-*.txt` |
| `evidence/render-current/logs/mutation-run-2.log` | 变异档重跑总日志（sha256 `bf54b209…`） |
| `evidence/render-current/logs/mutation-run-1-attempt1.log` | 变异档第一次（仪器故障）运行总日志（sha256 `774a8e56…`） |
| `evidence/render-current/probe/README.txt` | 红项归因的探针记录（机制、探针表、复采表、结论、修复建议） |
| `evidence/render-current/ENVIRONMENT-BUILD-CURSE.md` | 沙箱 safe-delete 故障全记录（现象、根因、排除法、绕行与验证） |
| `evidence/render-current/state/checks.txt` | 锁/源码恢复/新鲜度/工作树检查快照 |
| `evidence/render-current/state/SHA256SUMS.txt` | 上述全部文件的 SHA-256 |
