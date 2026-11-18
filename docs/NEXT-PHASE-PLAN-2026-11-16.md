# 下一阶段任务清单（E1 收尾之后，2026-11-16 22:20 立）

前一阶段（四卡 task-1..task-4）的交付物是 `docs/FOUR-CARD-REPORT-2026-11-16.md`；本文件接替它，
写明**接下来做什么、谁做、什么算做完**。原则沿用四卡纪律：只认实跑读数、身份可追、判据与产品缺陷分开、
阈值不偷改、提交前 `tools/audit-tree.sh` OK。每项完成时把读数与 commit 回填到本文件对应行。

## E. E1 第 5 轮收尾（必须先做完，其他阶段才谈得上定稿）

| ID | 任务 | 完成判据 | owner |
| --- | --- | --- | --- |
| E-1 | companion 11 红按 8-08 录制重钉 + 分类文档 | 门 `124 OK / 0 FAIL`；`docs/VIEWMODEL-COMPANION-RED-PIN-2026-11-16.md` 11 条逐条旧→新 + 交接指名 `viewmodel-animation-check.sh:574-577`；负控证明重钉仍能咬人 | card-arch |
| E-2 | 把 `37a872b3`(P3B §9) 与 `b0abac7d`(Phase A/B) 并入 candidate/e5 | 合并 0 冲突；主树 P3B 文档出现 §9；exe 身份不变（文档类提交不触发重建） | lead |
| E-3 | 四门矩阵 run 3（同一 exe、同一 HEAD） | 四门全 PASS，读数：viewmodel 226/0、minimap 100/0、map-geometry 30/0、companion 124/0；exe 跑前=跑后 | lead |
| E-4 | 报告 v0.4 定稿 | 身份声明 = `0bc3ed07…`（或 E-3 时的最新 exe）；§0 摘要按四门绿改写；§7.3 清理留档在；audit OK | lead |

## F. 产品缺口（报告里点名的「未完成」转成实现）

| ID | 任务 | 完成判据 | owner |
| --- | --- | --- | --- |
| F-1 | D75 世界 FOV 实现（设计 §4 全表） | worktree 分支提交；`--world-fov` CLI + ini 新键 + legacy 规则 + 目标实体 per-entity 缓存；trace `world: fov source=…` | card-char |
| F-2 | D75 验证（§5 五臂） | 基线重拍三组 sha；正控 1/2/3 逐像素相同；负控 1/2 必须红；回滚后行为 = 现状 | card-char → lead 放行构建 |
| F-3 | N27 产品导出入口 | 产品路径能按先验链（dump/encode/normalize/decode）导出并通过 `mf_export_verify.sh`；F-1 之后做，避免同文件冲突 | 待派 |
| F-4 | N26 按新口径重测 | 复测读数与口径文档逐项对齐 | 待派 |

## G. 结构（N24 拆分，方案见 `docs/N24-MAIN-SPLIT-PLAN-2026-11-16.md`）

| 阶段 | 内容 | 验收 |
| --- | --- | --- |
| P1 | `runRendererSelftest` → `main_selftest.cpp`（无全局、无捕获） | 构建 + 同一帧逐字节相同 + 自检门读数不变 + audit OK |
| P2 | 范围深度开关 + 窗口尺寸预扫 → `main_cli.cpp`（仅出参） | 同上 + `--help` 文本不变 |
| P3 | capture 布防/读数/退出 → `main_capture.cpp` | 三组 capture 门读数不变 |
| P4 | 收尾/保存块 → `main_shutdown.cpp` | `playback: summary` 行序与计数不变 |
| P5 | HUD/标题/小地图尾段 → `main_hud_loop.cpp` | `hud-check.sh`（含 --mutation）不变 |
| P6 | 183 个开关 → `struct CommandLine` + `main_cli.cpp` | `install-cli-contract-check.sh` 全绿 + 未识别参数退出码不变 |
| P7-P9 | spectate 驱动 / scene 体 / 启动与主循环 | 每阶段一提交、一重建、一次门复跑 |

**硬约束**：任何阶段移动的源码行若被门按文本钉住（见 N24 方案 §6 R1 的普查：11 个 `*-check.sh` 引用 该文件，≥6 个按锚点改写），要么把锚点留在 `main.cpp`，要么门改在同一提交并跑 `--mutation`。拆分改变源码内容 ⇒ `release-rebuild` 的身份链必须**显式**重盖（不能撞上）。

## H. 门禁与证据卫生

| ID | 任务 | 完成判据 |
| --- | --- | --- |
| H-1 | R1b 四连跑（新 exe 上探针未坏） | 4 次连续 pass，平面读数逐项相同 |
| H-2 | `D:/_shared` 27 GB 清理口径 | **已裁定（用户 2026-11-16 22:19，m08020）：全留，不删证据。** 本条从此是「决定」不是「待办」；若将来盘满，先在本文件改口径并列出被删池子与失效的正文引用，再动手 |
| H-3 | 门禁证据体积治理 | 新增门禁产出不再写进 tracked 目录（A 项已修）；单门证据 >200 MB 要登记 |
| H-4 | mtime 判据剩余扩展 | 全仓 `*-check.sh` 的 mtime 依赖清零或显式豁免 |

## 排期约束

- 产品构建与产品运行**串行**：hostlock 同一时刻只允许一个产品占用；任何阶段不做「边构建边跑门」。
- 每个阶段结束必须：构建 → 跑该阶段验收门 → `audit-tree.sh` → 一提交；不攒批。
- 我在主线（lead）持有 E-2/E-3/E-4 与放行权；F/G/H 可以并行派卡，但**同一时刻只有一张卡动 `native/src/main.cpp`**（D75、N27、N24-P1 都碰它，必须排开）。