# 验收包 - 2026-11-14（G1 + G2）

给审阅方（Codex）：本文件是**可复核的交付清单**，不是自评。每条给出命令、期望输出、以及"这条不证明什么"。
完整叙述见 `docs/TASK-REPORT-2026-11-14.md`。

- 仓库：`D:/TF2_Native_Worktrees/t105-e5`，分支 `candidate/e5`
- HEAD：`94e7612` → `0aa6eef`（13 笔：12 笔代码/文档 + 本验收包）
- 产品 exe：`cfcdbe5d48c6e71e5853b26b89da1578d521146447338a379a25bb9ad16ae2f3`，2,988,544 B
- sidecar：`sourceTree=0aa6eef41606`、`dirtyFiles=2`（只有用户既有的两份文档；产品源码干净）

---

## 1 交付物

| 类型 | 文件 | 作用 |
|---|---|---|
| 产品修复 | `native/src/main.cpp` | 暂停后小地图不再消失（`a153030`） |
| 门禁修复 | `minimap-hud-check.sh` | A2b 实测 BBOX 断言；A12 m2/m3 期望红因（`e3e0b7b`） |
| 门禁修复 | `verify-iso.sh` | 哈希改 stdin 读，27 个门禁恢复可跑（`124cc3b`） |
| 探针修复 | `native/tools/animation_viewmodel_probe.cpp` | 合成夹具 2 骨（`68b2555`） |
| 文档 | `docs/TASK-REPORT-2026-11-14.md` | 完整报告 |
| 文档 | `docs/DEFECT-REGISTER-2026-11-14.md` | 40 条台账（D01–D40） |
| 文档 | `docs/D08-MINIMAP-SEVEN-MAPS-2026-11-14.md` | 七图小地图验收 |
| 文档 | `docs/G2-2026-11-14.md`、`docs/BATCH-G1-2026-11-14.md` | 分册与证据 |
| 文档 | `docs/TRIAGE-LOOPFALSE-2026-11-14.md`、`docs/BLACKPOINT-849-919-2026-11-14.md` | 两篇归因 |
| 脚本 | `.scratch/lockrun.sh`、`.scratch/g1-minimap-five/{run,check,paused-panel}.sh`、`.scratch/g2-{probe-matrix,world-frames}.sh` | **未跟踪**（`.scratch/` 不入库），复核时按报告 §12 的命令用 |

---

## 2 可复核的断言（按顺序跑，全部只读或需 host lock）

| # | 命令 | 期望 | 耗时 |
|---|---|---|---|
| 1 | `python .scratch/g1-minimap-five/check.py D:/_shared/EXEC-2026-11-14/G1/minimap-five` | `MINIMAP-FIVE checks=205 failed=0` | 秒级 |
| 2 | `bash .scratch/lockrun.sh review -- bash minimap-hud-check.sh --mutation --old-exe=D:/_shared/EXEC-2026-11-13/R0/bin/tf2_demo_native.exe` | `MINIMAP-HUD=PASS assertions=104`；m1/m2/m3 `red=3/5/4` | ~15 min |
| 3 | `TF_ROOT="…/tf" ./native/build-nmake/animation_viewmodel_probe.exe` | `selfTest=true loop=true`，15 个标志全 true | 秒级 |
| 4 | `bash .scratch/lockrun.sh review -- bash world-black-account-check.sh` | `WORLD-BLACK-ACCOUNT=PASS (…; 849/919 pinned open)` | ~3 min |
| 5 | `bash .scratch/lockrun.sh review -- bash .scratch/g2-probe-matrix.sh` | 14 次探针全 `rc=0`；七图 `budget within`、`bump missing=0` | ~3 min |
| 6 | `OUT=/tmp/neg EXE=D:/_shared/EXEC-2026-11-14/G2/parent-bin/tf2_demo_native.exe bash .scratch/lockrun.sh review -- bash .scratch/g1-minimap-five/paused-panel.sh` | `on.bmp` 与 `off.bmp` 逐字节相同（缺陷复现） | ~2 min |
| 7 | 同上但 `EXE=native/build-nmake/tf2_demo_native.exe` | 两帧相差 25600 像素（全在面板内） | ~2 min |
| 8 | `sha256sum native/build-nmake/tf2_demo_native.exe` | `cfcdbe5d…` | 秒级 |
| 9 | `git diff --stat a153030..HEAD -- native/src native/include native/CMakeLists.txt` | 空 | 秒级 |

---

## 3 明确不在交付范围内（不要按这些验收）

- 任何一张地图的**视觉验收**（材质/光照/天空/水面/控制点状态）。
- 音频（可听）、多视图、3D skybox、线稿、载荷车图标。
- POV 相机矩阵；`pl_upward_f12` 的 **POV demo 仍缺**（用户提供的 `autorecord_2026-11-13_13-59-18.dem` 地图是 `pl_upward`，已按真实地图名单独归档）。
- 15 步发布链、安装流程、CI。
- DPI 缩放（产品无设置入口）。

---

## 4 已知 OPEN（供验收方判断严重级）

| ID | 内容 |
|---|---|
| D01 | 七图渲染矩阵未做（只有探针账 + 世界帧筛查） |
| D03 | 849/919 黑点来源未闭合（门禁已可跑，读数已量化） |
| D33 | 面板无线稿，文档口径已改 |
| D35 | 小地图缩放比例未独立验证 |
| D38 | 探针路径/编码不一致 |
| D39 | 世界帧三族异常未归因（白噪/黑区/疑似水面） |
| D40 | 产品写回 `--settings-file`（脚本已规避） |
| D06 | 非小地图证据仍绑旧 exe `00ec8b99`，需一次全量重绑 + 作废清单 |

---

## 5 请验收方回答

1. 第 2 节的 9 条断言，哪些**必须**在看到实跑输出后才算验收通过？请指定你认为必须现场复跑的最小集合。
2. 第 4 节哪几条应当在下一批**优先关闭**，理由是什么？
3. 本批有没有你认为是"自证"（用产品自己的输出证明产品正确）的断言？如有，请指出条目号。
4. 产品修复 `a153030` 是否引入了你没预料到的行为变化（尤其暂停/seek/相机回退路径）？

## 6 请验收方产出

一份新的任务清单，要求：

- 每条含：目标、前置依赖（资产/授权/窗口）、可执行命令、**验收断言与期望值**、以及"这条不证明什么"。
- 按"能不能在无人值守窗口内完成"分组（可做 / 需人在场 / 需资产 / 需授权）。
- 不得把"探针 PASS"当渲染 PASS，不得用别的图替代 `pl_upward_f12`。
- 若认为本批某条结论过强，请在清单里直接给出反例实验。
