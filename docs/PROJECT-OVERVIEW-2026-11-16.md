# TF2 Demo Player Native — 项目总览（2026-11-16）

> 用途：给"刚接手 / 想了解全貌"的人一份可独立读懂的说明：这是什么项目、现在在做什么、哪些没做完、工作区/素材/工具分别在哪。
> 口径：只写能追到实跑读数或仓库事实的内容；推断与未验证一律标注。本文件由 lead 在 T105-E5 合并窗口期间写于 `.scratch/`，合并窗口收工后落 `docs/` 并提交。

---

## 1. 这是什么项目

**TF2 Demo Player Native**：一个 Windows 原生的 Team Fortress 2 **demo 播放器与渲染器**，用 C++17 + Win32 + Direct3D 11 自己实现，**不依赖 Source 引擎**。它直接读 `.dem` 录像文件，自己解析网络包与实体协议，自己画世界几何、材质、人物模型与动画、HUD、音频，并自带安装生命周期（T75）。

- 仓库自述：`README.md`——"experimental Windows-native Team Fortress 2 demo player and renderer"；**开发中，不是可发布产品**；仓库公开但未附许可证，公开可见不等于授权复用。
- 公开默认分支 `p0-entity-protocol`；当前主开发工作树在 `candidate/e5` 分支上。
- 代码体量：`native/src` 87 个源文件、`native/include` 62 个头文件、`native/tools` 62 个文件（其中 54 个探针 `.cpp`，跑完构建产出 55 个 exe）。`docs/` 101 篇文档、`evidence/` 149 条证据目录。
- 产品能力现状（能用的部分）：demo 解析与实体协议、世界几何/材质渲染、viewmodel 与第三人称玩家模型/动画、HUD 与小地图、音频播放、INI 设置持久化、安装/卸载生命周期。**没有设置界面**（`UiCommand::OpenSettings` 存在于 `native/src/native_ui.cpp:377`，但产品里无人发送它，只有探针发送）。
- 明确的能力缺口：世界几何/材质的**完整视觉正确性**仍未验收；第三人称模型/动画同理。README 的硬口径：**"一个聚焦门禁通过，不是对所有地图、材质、职业、动画都正确的声明"**。

## 2. 现在在做什么

当前阶段是 **T105-E5**：把四条工作线并行推进，最后由 lead 交一份四卡总报告。四张卡各有 owner（独立 agent）与独立 worktree，写集不重叠，靠 hostlock 串行化对机器的使用。

| 卡 | owner | 主题 | 2026-11-16 当日在做什么 |
| --- | --- | --- | --- |
| task-1 | card-arch | 工程与集成 | 工具链唯一入口、/W4 告警门、故障注入门、E1 门禁矩阵（第 4/5 轮）、**合并窗口**（把三条分支并进主树并重跑门禁） |
| task-2 | card-maps | 地图 | M2a 顶点变换一致、M2b 否证"109 px 边界是遮挡"、R1 per-pass 深度回读仪（改码）、R2 常规渲染路径复现方案 |
| task-3 | card-ui | 交互 UI | U1/U2 语义验证、U3a 小地图线框、U3b 窄窗面板、D77 大小写缺陷、oracle stride 修复、**U4 双 pane**（第 1/2 步已落）、t2 判据失效定性 |
| task-4 | card-char | 角色/音频/导出 | P2 像素预算、P3a 效果可用性、P3b 音频账务、P4 导出能力、N27 导出先验链、D75 世界 FOV 基线重拍 |
| lead | — | 裁决 + 交付 | 语料普查与裁决（录像替换/BLOCKED-INPUT）、门禁改写、合并窗口裁定、四卡报告 |

工作方式有三条硬纪律，读任何证据前都该知道：

1. **门禁驱动**：仓库根目录有 **136 个 shell 脚本**，绝大多数是 `*-check.sh` 聚焦门禁；每个结论要来自本次实跑日志，不引用"上次跑过"。
2. **身份绑定**：每次门禁必须能追到"哪一个 exe"（sha256 + size + buildUtc + identity sidecar），产品二进制当前是 `native/build-nmake/tf2_demo_native.exe`。
3. **串行化**：构建与门禁共享机器资源，必须走 hostlock（`tools/lockrun.sh` / `hostlock.sh`）；提交前必须 `tools/audit-tree.sh` 判 OK。

## 3. 当前状态快照（2026-11-16 16:5x）

- **主树**：`D:/TF2_Native_Worktrees/t105-e5`，分支 `candidate/e5`，HEAD `014da358`（16:32:17），工作树干净。
- **当前产品 exe**：`a14ea9678be54e07…`，3,057,152 B，16:37:08，构建树 `014da358`，dirty=0——这是**合并窗口的新构建**（把 card-ui U4 第 1/2 步、card-char N27/4096 账务、card-maps R1 代码三条分支并入后的产物）。
- **上一个 exe**：`75dae10e60740cd2…`，3,044,864 B，13:28:50——第 3/4/5 轮门禁全部跑在它上面；身份链 `26bff436… → 81af3a4a… → cfa1ef77… → 75dae10e… → a14ea967…`。
- **合并提交**：`1c89aff5`（card-ui u3b，16:29:51）、`7706acaf`（card-char n27a，16:29:58）、`014da358`（card-maps r1，16:32:17）。
- **门禁矩阵现状**：
  - 第 4 轮（exe 75dae10e）：6 门 PASS（player-model / vmt-nesting / minimap-check / userinfo / release-rebuild / hud-live），t2-visual-matrix FAIL（3 红，已定性为**判据失效**），world-material-effects 被我掐（调用缺陷，非产品红），map-geometry 与 viewmodel-material 只跑到一半，companion / minimap-hud 未跑。
  - 第 5 轮（exe 75dae10e）：viewmodel-material **FAIL**（10 条红，未定性）、minimap-hud **FAIL**（3 红，含 A1 shapes 口径问题）、map-geometry `cp_snakewater_final1` **p1 PASS**（24 OK/0 FAIL，re-pin 后 git diff 为空）、p2 撞窗口上限被 TERM、viewmodel-companion 未开跑。
  - 合并窗口（exe a14ea967）正在跑：release-rebuild、CAMERA-UI、minimap-check、minimap-hud、hud-live、player-model、vmt-nesting，并验 `paneCount` 读数与单 pane same-sha。
- **四卡任务板**：task-1..task-4 全部 `in_progress`；**没有一张卡可以宣布完成**。

## 4. 哪些还没做完（诚实清单）

### 4.1 task-1 工程与集成
- 合并窗口的**门禁读数尚在跑**（合并后四门 + paneCount / same-sha 验证未收尾）。
- **mtime-only 新鲜度判据**要改成"源码内容哈希 + per-target 依赖集 + 二进制哈希"，并配负控（改内容必红、只复原字节必绿）——这是已定性的**架构性自缺陷**，只做了本轮取证（五探针 same-bytes 证明误报），未改判据。
- 第 5 轮遗留：viewmodel-material 的 10 条红、minimap-hud 的 A1 shapes 口径均**未定性**，不许先改期望值凑绿。
- N24 `main.cpp` 拆分未开工；N26 的 clean-build 测量是历史值（编译动作 634→137、clean build 648 s→221 s），本轮未按口径重测。

### 4.2 task-2 地图
- **R1 per-pass 深度回读仪**代码已提交（分支 `work/card-maps-r1`，4 文件 +189/−16，10 个深度 dump 点 + `--capture-pass-depth`），但**从未构建成功**（锁一直被占）⇒ 构建、三组对照、A 帧 `757f5aa2…` 复现全部待验。
- **R2 常规渲染路径是否复现该边界**：只有设计文档（`docs/N05-R2-NORMAL-PATH-DESIGN-2026-11-16.md`，85 行），未实现。
- **map-geometry-lighting**：cp_snakewater p1 已带理由 re-pin 并 PASS；p2 无裁决（窗口上限）；koth_bagel_rc12 的钉住 POV 录像已丢失（几何门只用 BSP，所以几何仍可跑）。
- **map-material 语料**：三份钉住的 POV 全部丢失；cp_sunshine 已按替换件登记（`acc13409…`，219,069 包）；`pl_odyssey` 与 `koth_bagel_rc12` 全机无录像 ⇒ **BLOCKED-INPUT**（保留行、不算失败）。语料登记已提交，但**完整重跑未做**，`readings.tsv` 仍是旧读数。
- M2b 的结论是**否证**（"遮挡"不成立），不是修复；现在指向绘制顺序/深度写状态，仍未证死。

### 4.3 task-3 交互 UI
- **U4 双 pane**：第 1 步（所有权边界 + pane 谓词 + 负控）、第 2a/2b 步（双 pane 渲染骨架）已提交；**第 3 步交互未做**——点击激活、第一/第三/自由切换、seek 同步、单视角不退化，以及"出口 PIP→四窗资源预算"。split 时是否画 ViewModel 尚未裁决。
- 5 条未证项：split 无 ViewModel 的呈现、window-size/DPR 矩阵、HUD split 变体、四窗预算、构建未逐次计时。
- **A1 面集**仍 BLOCKED：单位错配已解释（产品三角表 = fan(n−2) + 位移面 2·(2^power)²），但 floor faces 3480/3999（−13%）、同单位 fkept 2084/2802（−26%）的纳入规则未查清。
- **t2 判据修正**未做：input 期望 200000 已陈旧 21%（实测 242109）、vis 开关像素中性是正确行为；修正必须配负控并单独提交。

### 4.4 task-4 角色/音频/导出
- **N27 产品导出入口**：只有设计（`docs/N27-PRODUCT-EXPORT-ENTRY-DESIGN-2026-11-16.md`），未实现。导出**先验链**已可复跑（`native/tools/mf_export_verify.sh`，60/60 帧 sha、pcm、normalize 40 字节、decode 判据），但**产品仍无导出入口**。
- **4096 音频账务**：修已提交并进合并树（丢弃与解码失败分开计），但**尚未在新合并 exe 上跑产品验证**（期望 `sounddecodefail=0/2/2/2`）；恒等式部分只能由探针证明，而合并树的 `demo_open_probe` 尚未构建。
- **D75 世界 FOV**：基线已在新构建上重拍并**复现**（ini 40/120 让 99.92%/96.46% 像素变化；`--viewmodel-fov` 旗标与基线逐字节相同 ⇒ 旗标不碰世界相机），修法（给世界相机独立 FOV 字段）**未实施**。

### 4.5 横切缺陷登记
- **D72**（已修 `69992e0c`）：锁的 winpid+nonce 修复，误杀活锁变成正确拒绝。
- **D75**（未修）：世界相机 FOV 字段缺失，见上。
- **D76**（已登记 `f2b3cdb`）：三个 16384 静默截断 cap，违反本仓自己的计数标准。
- **D77**（已修 `0a1aad40`）：小地图排除材质的大小写失效（`rejectedTool` 0→829/208）。
- **source-tv 覆盖门禁**：九份钉住录像 5 份原位、4 份替换、review12 由用户补件恢复 ⇒ 活动集 9/9、pin 955,845；正负控均绿。口径：**九槽全覆盖 ≠ 原始九份都回来了**。
- **合并窗口未收尾**：新 exe 的门禁读数、`docs/INST1-MERGE-WINDOW-2026-11-16.md`、以及我 stash 起来的未跟踪 docs 的恢复。

### 4.6 交付物
- **四卡报告**：已写到 v0.1 并在持续补丁（`docs/FOUR-CARD-REPORT-2026-11-16.md`，当前副本暂存在 `.scratch/report-four-cards-recovered.md`）。未定稿、未提交——要等四卡收口与合并窗口读数。

## 5. 工作区在哪里

**主工作树（当前开发）**：`D:/TF2_Native_Worktrees/t105-e5`，分支 `candidate/e5`。
- 构建产物：`native/build-nmake/`（NMake Release，产品 exe + 55 个探针 exe）。
- 门禁脚本：仓库根 136 个 `.sh`，例如 `source-tv-coverage-check.sh`、`map-geometry-lighting-check.sh`、`map-material-matrix.sh`、`minimap-check.sh`、`minimap-hud-check.sh`、`camera-ui-check.sh`、`hud-live-check.sh`、`player-model-check.sh`、`userinfo-check.sh`、`release-rebuild-check.sh`、`vmt-nesting-check.sh`、`verify-all.sh`。

**各卡的隔离工作树**（互不污染，主树 exe 不被别人重建）：

| 工作树 | 分支 | 用途 |
| --- | --- | --- |
| `D:/TF2_Native_Worktrees/t105-e5-u3b` | `work/card-ui-u3b` | task-3 U3b / U4 |
| `D:/TF2_Native_Worktrees/t105-e5-n27a` | `work/card-char-n27a` | task-4 N27 / 4096 / D75 |
| `D:/TF2_Native_Worktrees/t105-e5-maps-r1` | `work/card-maps-r1` | task-2 R1 |
| `D:/TF2_Native_Worktrees/t62-matrix` | — | 地图矩阵抓图 |
| `D:/TF2_Native_Test` | `p0-entity-protocol` | 公开分支/历史测试树 |
| `D:/TF2_Native_Worktrees/t79-integration`、`t91-integration`、`t59-review`、`D:/TF2_Native_Animation_Integration` | 各自分支 | 历史集成线 |

**证据池（不在 git 里，体积大）**：`D:/_shared/`（约 25 GB），按天分 `EXEC-YYYY-MM-DD/`，再按 `INSTn`/主题分子目录。今日相关的四个：
- `D:/_shared/EXEC-2026-11-16/INST1/` — E1 门禁矩阵与合并窗口读数
- `D:/_shared/EXEC-2026-11-16/INST2/` — 地图矩阵（t62-map-matrix）
- `D:/_shared/EXEC-2026-11-16/INST3/` — card-char 任务 A/B（`taskA/`、`taskB/`、`taskB-merged/`）
- `D:/_shared/EXEC-2026-11-16/INST5/source-tv/` — source-tv 覆盖门禁正负控日志
- `D:/_shared/EXEC-2026-11-16/D75/rebaseline-75dae/` — D75 基线重拍（约 330 MB，含 harness 与全部 BMP）

仓库内的 `evidence/`（149 条）只放**选定的**抓图与报告；大块证据按纪律落 `D:/_shared`。

## 6. 素材在哪里

- **TF2 游戏资源（不进仓库）**：`D:/SteamLibrary/steamapps/common/Team Fortress 2/tf`。
  - 地图 BSP：`tf/download/maps/` 共 **83** 个 `.bsp`。
  - 录像：`tf/demos/` 共 **2063** 个 `.dem`（最早到 2026-08-02）。
- **额外录像语料**：`E:/demo测试/`——按地图分目录，含 POV/STV 备份。例：`E:/demo测试/cp_sunshine/POV/autorecord_2026-05-17_16-32-36.dem`（75,905,225 B，sha256 `acc134094ef2ab1d69b8fdf606346a331ae8dc630d4ba634c682e2b448562b3a`，219,069 包）就是 cp_sunshine 语料缺口时启用的替换件。
- **运行产品需要 `--tf-root`**：`tf2_demo_native.exe --tf-root "D:\SteamLibrary\steamapps\common\Team Fortress 2\tf" --demo "<...>\tf\demos\x.dem"`。没有 `--help`；未识别参数记进 trace（`--strict-args` 才拒）。
- **语料纪律**：录像丢失时按"替换件路径 + sha256 + 地图 + 录制类型 + 实测包数 + 替换原因"登记，断言一条不删；确实无件就记 **BLOCKED-INPUT**，不拿"没跑"当"通过"。

## 7. 工具在哪里

- **构建**：`build-cmake.sh`（configure + clean Release → `native/build-nmake/`）、`build-target.sh <target>`（增量）。工具链唯一入口是 `tools/tf2-toolchain.sh`（`native/env-msvc.sh` 只是 10 行 shim），它先断言 cl/nmake/rc/mt 存在再 export 同一套 env。
- **验收入口**：`verify-all.sh`（`--quick` / 全链）；全链要在冻结树上跑，见 `docs/RELEASE-POLICY.md`。
- **串行化与审计**：`tools/lockrun.sh`（带 owner 的锁；`hostlock.sh run <owner> ...`）、`tools/audit-tree.sh`（提交前树审计，证据目录脏会判 FORBIDDEN-WRITE）。
- **质量门**：`tools/warn-check.sh`（/W4 告警基线，只减不增）、`tools/fault-seam-check.sh`（故障注入 9 条）、`tools/check-nested-arith.sh`（嵌套算术）。
- **探针**：`native/tools/*.cpp`（54 个：`demo_open_probe`、`entity_model_probe`、`hud_probe`、`native_ui_probe`、`mf_export_probe`、`minimap_probe` 等），构建后落在 `native/build-nmake/`；`native/tools/mf_export_verify.sh` 是导出先验链的一键判据脚本。
- **一个坑**：`--window-size` 下限是 320×240（`native/src/main.cpp:2757-2766`），所以某些窄窗用例在产品路径上**不可达**，只能用离屏探针覆盖——写门禁时别把它当成用户可见缺陷。

## 8. 怎么跑一次（最短路径）

```bash
# 1) 构建（Git Bash，CMake/NMake/MSVC 在 PATH）
bash build-cmake.sh
bash build-target.sh tf2_demo_native

# 2) 跑一次 demo
native/build-nmake/tf2_demo_native.exe \
  --tf-root "D:/SteamLibrary/steamapps/common/Team Fortress 2/tf" \
  --demo "D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/demos/45process.dem"

# 3) 跑一个聚焦门禁（构建与门禁共享机器，必须串行）
bash tools/lockrun.sh <owner> -- bash hud-check.sh

# 4) 提交前审计
bash tools/audit-tree.sh     # 期望 VERDICT: OK
```

## 9. 文档地图（先读哪几篇）

- `README.md`：项目定位、构建、参数、验证现状。
- `CHANGELOG.md`：发布快照式更新日志。
- `docs/RELEASE-POLICY.md`：验收规则（什么算通过）。
- `docs/KNOWN-ISSUES-AND-FIXES.md`：已复现问题与修复状态。
- `docs/GATE-DESIGN-TRAPS.md`：门禁设计陷阱（写门禁/读门禁前必读）。
- `docs/DEFECT-REGISTER-MASTER-2026-11-14.md` 及 addendum：缺陷总账（D72/D75/D76/D77 等）。
- `docs/EXEC-PLAN-2026-11-15.md`、`docs/PROJECT-EXECUTION-ROADMAP-2026-11-15.md`：执行计划与 N16 双 pane 等定义来源。
- `docs/GATE-INPUT-REPLACEMENT-2026-11-16.md`：录像替换登记与 source-tv 口径（§9–§11）。
- 当日卡内文档：`docs/INST1-E1-ROUND4/5-TRIAGE-*.md`、`docs/N05-M2B-DEPTH-ATTRIBUTION-2026-11-16.md`、`docs/U4-PANE-OWNERSHIP-2026-11-16.md`、`docs/P3B-SOUND-ACCOUNTING-SPLIT-2026-11-16.md`、`docs/N27-EXPORT-PROBE-RERUN-2026-11-16.md`、`docs/D75-WORLD-FOV-FIX-DESIGN-2026-11-16.md`。
- 交付物：`docs/FOUR-CARD-REPORT-2026-11-16.md`（四卡总报告，定稿中）。

## 10. 诚实边界（读结论前请先读这一段）

1. **门禁绿 ≠ 视觉正确**：每个聚焦门禁只对它测的那些输入与断言负责；这是 README 与所有报告的共同口径。
2. **有些红是判据问题，不是产品问题**：mtime-only 新鲜度判据会误报（第 4 轮五探针 same-bytes 已证）；t2 的 input 期望值已陈旧 21%。这两类都单独登记、单独修，**不许顺手改期望值凑绿**。
3. **有些绿是"能力已证、功能未接"**：导出链、音频账务修正都属于这种——能力/修法可复跑，但产品入口/产品读数还没到位。
4. **语料不全**：地图矩阵有 2 份 BLOCKED-INPUT；source-tv 九槽是"全覆盖"而不是"原始九份都回来了"。
5. **本文件是项目总览，不是验收报告**；逐条证据与读数看 `docs/FOUR-CARD-REPORT-2026-11-16.md` 与各卡文档。
