# /analyze 全树重扫 — 2026-11-19

这份文件是 L4 的交付（`docs/TASK-ALLOCATION-2026-11-19.md` §2），也是
`docs/TASK-LIST-2026-11-19.md` 未解决项第 1 条的读数。它替换 2026-11-18 那份
只覆盖 `tf2_core` + `tf2_demo_native`（51+30 TU）的旧读数。

## 1. 怎么跑的（从仓库里就能复跑）

这次把分析器**收进仓库**，不再依赖 `/d/_shared` 里的临时脚本：

- `tools/analyze-one-target.sh`：对单个 target 的每个 TU 跑一次 `cl`，flags 取
  `native/build-nmake/CMakeFiles/<target>.dir/flags.make` 里的原样 flags，
  去掉 `/WX`（分析结论要读，不能变成构建失败）与 `/Brepro`，加
  `/analyze /analyze:external- /analyze:log`。
- `tools/analyze-full-tree.sh`：枚举 `build-nmake` 下所有 target 串行跑，写
  `summary.tsv`（target / tus / warnings / rc）。

复跑：

```bash
tools/analyze-full-tree.sh --stamp 2026-11-19      # 2026-11-19 实测约 10 分钟
```

本轮（13:47–13:57）用的是 hostlock 单锁：

```bash
bash ../_shared/hostlock.sh run analyze-full bash tools/analyze-full-tree.sh --stamp 2026-11-19
```

## 2. 总量

| 项 | 数 |
|---|---|
| target（有 `flags.make` 的） | **55** |
| TU 行（含 2 个跳过的生成源） | 138 |
| **真正分析的 TU** | **136** |
| **警告总数** | **19** |
| 分析本身失败（`rc!=0`） | **0** |
| 跳过 | 2（`tf2_build_stamp`，生成源，仓库里没有对应 .cpp） |

## 3. 有警告的 target（全部 19 条）

| target | 条数 | 代码 |
|---|---|---|
| `tf2_core` | 1 | C28020 |
| `tf2_demo_native` | 4 | C28251, C6011, C6262, C6553 |
| `viewmodel_fov_probe` | 5 | C6326 ×5 |
| `sound_timeline_census_probe` | 2 | C6239, C6326 |
| `winmm_sink_probe` | 2 | C6553 ×2 |
| `demo_open_probe` | 1 | C6262 |
| `entity_message_fixture_probe` | 1 | C6262 |
| `logstf_viewer_probe` | 1 | C6262 |
| `review_workspace_probe` | 1 | C6262 |
| `world_feature_probe` | 1 | C6262 |

按代码合计：C6326 ×6、C6262 ×6、C6553 ×3、C6239 ×1、C6011 ×1、C28251 ×1、C28020 ×1。

关键位置：

- `tf2_demo_native`：`native/src/main.cpp(2971)` 的 C28251 + **C6262 58,208 B**（`wWinMain`）、
  `native/src/main_hud.cpp(1301)` 的 C6011（`draw.row` 空指针）、
  `winreg.h(788)` 的 C6553（系统头，经 `RegOpenKeyExW` 触发）。
- `tf2_core`：`native/src/vtf_texture.cpp(200)` 的 C28020（`_Param_(1)<8` 对此调用无效），**旧读数里就有，未变化**。
- 4 个 probe 的 C6262 与 2 个 probe 的 C6553/C6239/C6326：这些 target **以前从没进过 /analyze**，
  所以是新读数而不是新回归；它们是工具/探针，不是产品 exe。

## 4. 与 2026-11-18（P9 之前）的差

只对当年有读数的两个 target 可比：

| target | P9 之前 | 现在 | 差 |
|---|---|---|---|
| `tf2_core` | 2（C6262 + C28020） | 1（C28020） | `native_ui.cpp:63` 的 C6262（131,152 B 栈帧）已随 P9 消失 |
| `tf2_demo_native` | 6（C28251 + C6262×3 + C6011 + C6553） | 4（C28251 + C6262 + C6011 + C6553） | 两处 `main.cpp` 的 32 KiB 栈缓冲 C6262 已消失；`wWinMain` 的 58,208 B 仍在 |

**净变化：`-4` 条**（2 → 1、6 → 4 合计少 3 条，但旧读数口径是 2 个 target；按同 target 计 −3）。
其余 53 个 target 当年没有读数，不能算"新增"。

## 5. 这份读数意味着什么

1. `docs/TASK-LIST-2026-11-19.md` 未解决项第 1 条（全树没有重扫）**关闭**：55 target / 136 TU
   全部有读数，分析失败 0。
2. `N23-WALL-LEVEL-2026-11-18.md` §8 里"其余 4 条（C28020 / C6553 / C6011 等）仍是旧读数"
   现在有当前值：C28020 未变、C6553 在系统头（外部）、C6011 在 `main_hud.cpp:1301`。
3. **唯一还压在产品 exe 上的大项**是 `wWinMain` 的 C6262 58,208 B —— 它需要把
   `wWinMain` 拆开（N24 phase 2）才能真正消失，不是再搬一个缓冲能解决的。
4. 19 条里 14 条在 probe/工具 target 上，产品 exe（`tf2_core` + `tf2_demo_native`）只占 5 条。
   `/analyze` 仍然**不在门禁里**，这份读数是手工读数，不是 CI 绿。

## 6. 证据与不宣称

- 入库：`evidence/analyze/2026-11-19-summary.tsv`（55 行，target/tus/warnings/rc）。
- 原始：`/d/_shared/n23-analyze-2026-11-19/<target>/{<tu>.log,<tu>.xml}`、
  驱动日志 `/d/_shared/analyze-full-2026-11-19.log`（本机路径，可重跑，不入库）。
- 本轮还修了驱动的一个统计 bug：`[ -s "$line" ]` 被用来判断"runner 有没有打印 SUMMARY"，
  但它测的是一个以 SUMMARY 文本命名的文件，永远为假 → 每个 target 都被记成 `rc=1`、
  驱动报 `ANALYZE-FULL-TREE=PARTIAL`。已改成 `[ -n "$line" ]`；上表的 rc 列是按各 target
  日志重新判定的（55 个全是 0），**警告数不受影响**。
- 不宣称：这些警告已经修好、或 `/analyze` 可以进 CI。它只提供当前读数。
