# 完整任务报告与清单完成标记（2026-11-15 10:55，第二窗口收工）

仓库 `D:/TF2_Native_Worktrees/t105-e5`，分支 `candidate/e5`，HEAD `10f8e28`。
本报告回答两个问题：**清单上哪些任务真的做完了（且有证据）**、**哪些没做（以及为什么）**。
标记：✅ 完成（带证据边界）／◐ 部分完成／⛔ 阻塞在环境或依赖／○ 未开始。
配套：窗口过程报告 `docs/WINDOW-REPORT-2026-11-15B.md`、N13 实现 `docs/N13-STEP1-IMPL-2026-11-15.md`、
状态表 `docs/N-STATUS-2026-11-14.md`。

## 一、总表（按 `docs/PROJECT-NEXT-TASKS-2026-11-14.md` 的 N 编号）

| 任务 | 状态 | 证据与边界 |
|---|---|---|
| N00 唯一台账与当前身份 | ◐ | 台账（N-STATUS/EXEC-PLAN/本报告）已统一；构建身份闸 D-90（`gen-build-stamp.cmake:293`）在跑并拦过未跟踪头文件；`tools/audit-tree.sh` 每次提交前 `VERDICT: OK` |
| N01 缺失素材与独立参考 | ⛔ | 本机仍缺 `autorecord_2026-07-02_13-26-46.dem`，gate3 `playback-error-paths`／gate5 `viewmodel-animation` 因此只能记环境假红 |
| N02 探针 Unicode/路径与失败契约 | ○ | probe 的 CJK demo 路径与 `--scan` 两 sha 本轮被复用作 A8.9 基线（`8007b92b…`/`3ef091d5…` 逐字节相同），但 N02 本身没有取 verdict |
| N03 设置隔离与清理自检 | ○ | 本轮门禁全部写独立 OUT（`T84_STATES_OUT`、`N13-QA/a8`），但这是操作纪律，不是 N03 验收 |
| N04 安全/崩溃缺陷优先级复核 | ○ | 本窗口未动 |
| N05 Sultry 849/919 draw 闭环 | ◐ | 静态诊断否证两条候选解释（METALGRATE alpha、tiled colour=1），**根因未定位**；无实际 pass/triangle/UV/覆盖证据。D01 保持 OPEN |
| N06 七图世界帧异常分组 | ✅ | `docs/N06-GROUPING-2026-11-15.md`（提交 `ed419f6`，290 行）。边界：白噪/黑区/蓝面族群的**同机位 on/off 对照未做**，分组表不等于归因完成 |
| N07–N11 地图 | ◐ | 盘点/矩阵文档完成（N08–N10 以静态证据为主）；七图 14 格仅 2 格合格且证据跨四个 exe；推车 NOT VERIFIED、`sky_camera` UNSUPPORTED；产品签收未完成 |
| N12 POV/STV 相机 | ◐ **A ✅ / B ✅（有限样本） / C 三步落地** | 见 §2.1 |
| N13 HUD 身份与跟随路径 | ◐ **第一步 ✅** | 命令/状态层 + `follow:` 行落地并 A8 PASS；A1–A7 命令语义未验；第二步（相机优先级）未做 |
| N14 小地图底图与独立标尺 | ◐ | 设计文档 `docs/N14-MINIMAP-BASEMAP-2026-11-15.md` 就绪，产品未实现、未签收 |
| N15 DPI/resize/暂停 seek | ◐ | 设计文档 `docs/N15-DPI-LAYOUT-2026-11-15.md` 就绪；产品未过 |
| N16 多玩家分屏 | ◐ | 只有契约 `docs/N16-SPLITVIEW-CONTRACT-2026-11-16.md`，无产品实现 |
| N17–N20 角色/武器/容量/三效果 | ◐ | 覆盖报告 `docs/N17-N20-CHAR-COVERAGE-2026-11-15.md`；ViewModel 动画仍 bind pose/NOT VERIFIED；N20 三指定效果未实现 |
| N21 音频设备与恢复 | ○ | 未做；听感部分需人在场 |
| N22 CI 与工具链便携 | ○ | 本地未跑 CI，无远端运行 |
| N23 警告/静态检查/错误注入 | ○ | `/W4` 覆盖不足未推进 |
| N24 拆分主循环 | ○ | 只有方案 |
| N25 门禁与证据效率 | ◐ **大幅推进** | 见 §2.3：gate2/gate4 真绿、gate4 四条负控转可判、新增 `--controls-only`、修掉 dry-run 截断 evidence 的缺陷；gate1 沿用真绿；gate3/gate5 ⛔ 环境。12 道被算术清扫改过的门禁**真实整轮未复跑** |
| N26 构建/缓存/内存实测 | ○ | 未做 |
| N27 可选音视频导出 | ◐ | 范围与组件契约已裁决（`N27-SCOPE-DECISIONS`/`N27-OPTIONAL-COMPONENT-CONTRACT`），**产品实现未开始** |
| N28 集成批次与证据继承 | ◐ | 文档就绪，未集成冻结 |
| N29 干净环境走查与冻结 | ⛔ | 无干净环境 |
| N30 最终发布链与发布包 | ⛔ | 未跑 `verify-release.sh`；发布状态仍 NOT-YET |
| 嵌套算术清扫 | ✅ | 14 处 buggy 全修（12 个脚本）+ 新门禁 `tools/check-nested-arith.sh`（`--selftest` PASS、仓扫 `sites=0`、旧版回放 14/14 全抓）。边界：被改门禁未跑真实整轮 |
| `.gitattributes` 行尾策略 | ⛔ | 策略已裁决（只约束源码/构建定义/门禁工具为 LF），文件改动仍未提交，等待负责人确认 |

## 二、本窗口四件完成项

### 2.1 N12 覆盖率收复：A2/A3/B2 从 NOT VERIFIED 变 PASS

- 在与 A1 同一 exe（`90f781b5…`，3,003,392 B）上补采四帧：`pov-t40000`、`upward-pov-t39999`、`upward-pov-t20000`、`stv-t40000`，四帧 `rc=0`、`trace_state=ok`、bmp 均 2,582,406 B，耗时 09:41→09:52:36。
- `N12_SET=full90` 重跑 `accept.py`：**N12-A PASS**（A1/A2/A3 全 PASS，eyes 行 `offsetZ=75.0001/75.0001/68.0001 dz=+75.0000/+75.0000/+68.0000`）；**N12-B PASS**（B2 `stv-t40000` 45.000043，距 68.0 常量 23 u，有区分度；B1 `stv-t20000` 68.000065 与常量只差 0.0001 u ⇒ **COINCIDENT/NOT VERIFIED**，不计绿不计红）；**N12-C BOUNDED-STALE**；EX-3 死亡相机未抬升 PASS；六条 eye line 全 PASS 或 NOT APPLICABLE。
- 顺手修掉 `accept.py:1235` 的假红：非 pov 角色一律取 `ENTITY['stv_signon']`，death 角色（EX3，entity 10）也吃到 1 ⇒ `_expect = ENTITY[_ent] if _mode == 'death' else ENTITY['stv_signon']`。
- **仍未证**：STV 没有真实导播相机数据，眼位推导与导播真值只能分开验收；B1 样本没有区分力，需换帧。

### 2.2 N13 第一步：命令/状态模型落地，A8 PASS

- 纯增量四文件、零删除行：`native/include/target_identity.h` 127 行、`native/src/main.cpp` +640、`native/src/main_hud.cpp` +22、`native/src/main_hud.h` +20；四个相机函数（`haveObserverView`/`resolvePovEye`/`applyCameraPovEye`/`placeSpectatorView`）与既有 trace 格式串未动。
- 编译从零到绿：D-90 拒收未跟踪头文件（`git add` 解决）；C2065 前置 `extern`；**C1061 根因是 CLI else-if 梯子被四个 `--follow-*` 分支顶过 MSVC 上限**，改为 `followConsumeArgument()` 在梯子前消费，梯子恢复与 HEAD 逐字节相同（530 行/117 rung）。⇒ exe `6d15c566…`，3,010,048 B，`build-target.sh tf2_demo_native` exit 0。
- **A8 判据修正**：原「trace 逐字节不变」不可满足（同 demo 两次运行 75,799 vs 154,899 行、`povQueries` 1429 vs 2932，纯帧率效应），按 `docs/N12-ACCEPTANCE-2026-11-15.md:398` 改为按 tick 归键：eye row 387 共同 tick / eye 387 共同 key **0 不同行**；帧 md5 `334fe0a8…` 三态相同；行种类 85 vs 85；`follow:` 行 0 vs 5；`cameraEffect=pov-eye-wins` 具名；probe 两 sha 与 N12 §17.4 基线相同 ⇒ **N13-A8=PASS**。
- **未做**：A1–A7（select/next/prev/unfollow/back-to-pov 语义、队列上限、reject 计数）；第二步相机优先级裁决。POV demo 里选人仍不改画面。

### 2.3 门禁：gate2/gate4 真绿，gate4 四条负控首次可判

- gate2 `player-animation-check.sh`：`rc=0` / `PLAYER-ANIMATION-CHECK=PASS` / 67 s / 235 OK / 0 FAIL（前提：23:44:32 重建 `model_pose_probe.exe` 清 STALE-BINARY）。
- gate4 `animation-drive-states-check.sh`：`rc=0` / `ANIMATION-DRIVE-STATES=PASS` / 4002 s / 20 OK / 0 FAIL / 9 次产品运行 / `READINGS=PASS`（A: default==legacy md5 `6f6309c0…`）。
- 修掉两处脚本缺陷：`:445` `grep -m1E` 粘连 flag（负控原因串恒空）；`:153` `: > gate.txt` 在 dry-run 早退前，会把 tracked evidence（43 行）截成 22 行 ⇒ 现 dry-run 前后 md5 均 `814ba78c…`。
- 新增 `--controls-only`（复用已捕获的九次运行，独立 `gate-controls.txt`）：四条负控 `failures=0 controls=4/4`，每条给出具名原因；空 OUT 负控 `rc=2`。证据边界：controls-only 不启动产品，复用 trace 由 exe `90f781b5…` 于 11-14/15 产出。

### 2.4 算术清扫（本窗口前完成，本窗口取真实结论）

14 处「`$(( $(…) )` 内层含引号」全部改两步赋值并加非整数护栏，分布 12 个脚本；新门禁 `tools/check-nested-arith.sh` 三种验证（`--selftest` PASS、仓扫 `sites=0`、旧版回放 14/14 全抓）。**未跑这 12 道门禁的真实整轮**，所以「修完仍按原语义判绿」仍缺证据。

## 三、明确没做完的（按优先级）

1. **12 道被清扫门禁的真实整轮复跑**（gate1/2/3/4/5 之外的 `hud-live`、`map-geometry-lighting`、`player-model`、`t2-visual-matrix`、`release-rebuild`、`userinfo`、`world-material`、`world-material-effects`、`vmt-nesting`、`viewmodel-material`、`viewmodel-animation`、`viewmodel-companion`）。其中两道依赖缺失录制。
2. **N13 A1–A7 + 第二步相机优先级**。
3. **N14/N15/N16** 产品实现；**N17–N20** 人物/武器/三效果；**N27** 导出实现；**N22–N26** 工程项；**N28–N30** 集成与发布。
4. **N05 根因**（849/919）与 **N06 同机位对照**。
5. **B1 `stv-t20000` 样本无区分力**，需换帧。
6. `.gitattributes` 实施等负责人确认。

## 四、下一步（依赖顺序）

1. 12 道门禁复跑（可并行，按 hostlock 串行产品运行）；缺录制的两道如实标环境假红。
2. N13 A1–A7 语义验收 → 第二步相机优先级裁决。
3. N14/N15 → N16 → N27 → N17–N20 → N28/N29/N30。
4. N05/N06 归因（需 GPU 抓帧与同机位对照）。

## 五、现场

HEAD `10f8e28`；hostlock FREE；product_processes=0；工作树仅剩负责人未提交的 `.gitattributes` 与 `docs/PROJECT-*`（本报告未改动它们）。
本轮提交：`4bf830a`（N13 第一步 + gate4 修复 + N13 实现文档）、`10f8e28`（第二窗口报告 + N-STATUS 改判）。
