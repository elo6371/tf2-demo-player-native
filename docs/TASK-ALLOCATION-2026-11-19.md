# 任务分派与进度 — 2026-11-19

基线与当前 HEAD：`work/integrate`（本文件所在提交之前是 `6bd73350`）。
给 Gemini 的完整任务书在 `docs/GEMINI-TASK-PACK-2026-11-19.md`（G1–G9，含基线、交付格式、参考库、验收链）。
本文件只回答两件事：**谁做什么**、**现在做到哪**。

## 0 约束（决定分工的物理原因）

- Gemini 只看得到 GitHub 上的仓库内容：它不知道本地 `/d/_shared`、`.scratch`、构建产物、画面。
- 本地机器能构建（139 TU）、能跑 demo、能跑五门禁；Gemini 不能。
- 所以：**静态可判定的给它**（写代码、写设计、找文档矛盾），**必须运行才能定的留在本地**。

## 1 给 Gemini 的（详见任务包）

| 编号 | 任务 | 难度 | 交付 |
|---|---|---|---|
| G1 | CLI 表驱动化（191 条开关全量清单 + 分批迁移） | 最重 | 阶段 A 清单 + 3 轮补丁 |
| G2 | LOD 选择策略 + 逐实体选级 + 环境变量报告 | 重 | 设计 + 补丁 |
| G3 | 粒子数据层（PCF operator 求值 + 离线仿真探针） | 最重 | 4 轮（设计/解析/探针/golden） |
| G4 | `wWinMain` seam map + 第一片搬迁 | 重 | 只读设计先交 |
| G5 | `/analyze` 剩余告警定性 | 中 | 逐条判定 |
| G6 | 文档-源码一致性第二遍（≥8 处） | 中 | 表格 + 文档补丁 |
| G7 | 逐顶点 UV 的 PS 侧修复 | 中 | HLSL 补丁 |
| G8 | N18 viewmodel 门闩 | 中 | 最小补丁（默认关） |
| G9 | N11 七图采集脚本 | 轻 | 脚本 |

批次 1 只要求它交 G4 阶段 1 + G6，拿到反馈前不许往下跑（见任务包 §5）。

## 2 我负责的（必须本机运行）

| 编号 | 任务 | 状态 | 验收 |
|---|---|---|---|
| L1 | 清理旧产物与仓库卫生 | **今天完成第一轮** | 见 §3 |
| L2 | N11 七图矩阵：脚本 + 真跑一次 | **完成（2026-11-19 13:32）`N11-SEVEN-MAP-MATRIX=PASS rows=14/14 identical_pairs=0`** | 读数入库 `evidence/n11-matrix/readings.tsv`；§9 两条待查读数（sunshine POV 相机、tiles=0） |
| L3 | D01 COLOR0 取证（849/919 顶点色归因） | 待做 | 用现有 View16/17 回读 + 新增 dump，给出 texel 级结论 |
| L4 | `/analyze` 全树重扫（139 TU） | 待做 | 逐 TU 读数 + 与 2026-11-18 基线 diff |
| L5 | ASan 两条路径（model / material） | 待做 | 两次带 ASan 运行，0 报告或逐条定位 |
| L6 | CI 首跑排查（`.github/workflows/tier1.yml` 在仓但零运行） | **已推 `candidate/ci-smoke`（`b83a9c2e`）触发；本机到 api.github.com 不可达，结果只能远端看** | 要么首次真跑绿灯，要么写清被什么挡住 |
| L7 | T84 成本：3848 s → 目标 < 2400 s | 待做 | 同读数的前提下压缩时长，仍 4/4 负控 |
| L8 | 每轮进度回写（本文件 + STATUS-REPORT + TASK-LIST） | 持续 | 每次提交带读数 |

优先级：L2 → L3 → L4（后台跑）→ L5 → L7 → L6。

## 3 L1 今天做了什么（读数）

清理前 `/d/_shared` = **36,062 MB**、`t105-integrate/.scratch` = **4,257 MB**。

删掉的东西（全部是 docs 未引用的旧产物）：

- `/d/_shared/EXEC-2026-11-07 / -09 / -10 / -11` 四个执行目录（35 + 451 + 1,819 + 132 MB）。文档引用过的是 `-08 / -12 / -13 / -14 / -15 / -16`，这些**保留**。
- `.scratch` 下 44 个实验目录：`speckle*`（24 个）、`n17-sweep`、`n17-world-fov`、`n17-overview-current`、`w1-merged`、`w1-pos`、`integrate-frames*`、`up11057`、`up11356`、`quality-audit.*`、`lighting-semantics.*`、`sultry-translucent-atlas.*`、`viewmodel-apply/presentation-*`、`world-material-face.*`、`world-near-clip.*`。这些目录在 `docs/` 里零引用，且都是可重跑的捕获输出。

清理后 `/d/_shared` = **33,627 MB**（−2,435 MB）、`.scratch` = **2,288 MB**（−1,969 MB），合计释放 **约 4.4 GB**。`git status` 干净（被删的都在 `.gitignore` 里）。

明确保留：

- `/d/_shared/n25-drive`（8.1 GB，被 `docs/GATE-VERDICTS-ROUND2-2026-11-15.md` 与 `STATUS-REPORT` 引用，是 animation-drive-states PASS 的原始证据）。
- 被文档引用的 `.scratch` 目录（`warn-check`、`build-identity-run`、`t62-map-axes` 等）。
- 9 个实验 worktree（`t105-b1..b3/base/p1..p3/vt-pre/vt-post`）：2026-11-19 判定完毕 —— 脏文件全是 `evidence/*` 再生成产物加两三个 check 脚本的本地改动；已把文字 diff 归档成 `/d/_shared/worktree-archive/<name>.patch` + `<name>.status.txt`（共 47 MB，排除 bmp/csv/raw），底提交 `ae0b47cc`/`917332d7`/`10d13559` 均已在 `work/integrate` 祖先链上，随后 `git worktree remove --force` 全部删除。D 盘可用 44,656 → **56,914 MB**（+12.2 GB），`git worktree list` 剩 20 个。
- 33 个本地分支**不推**（无工程必要性，远端分支已够用）；CI 只推 `candidate/ci-smoke` 一条，不改产品。

## 4 同步规则（保证 Gemini 看到的和你我一致）

1. 任何完成项必须在仓库里有落盘：`docs/` 里的读数 + 一次推送，不能只活在我这边的对话里。
2. 本地证据若在 `/d/_shared` 或 `.scratch`，文档里必须写清「这是本机路径、可再生」，并给出重跑命令；不能当作唯一证据。
3. 清理前先确认文档引用（本轮就是按这条筛的）。
4. 每轮结束 HEAD 单一、工作区干净；Gemini 以远端 `work/integrate` 为唯一事实来源。
