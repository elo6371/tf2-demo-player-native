# G1 完整任务报告 - 2026-11-14

- 会话窗口：2026-11-14 09:36 – 11:55（GMT+8）
- 依据：`docs/PROJECT-HANDOFF-2026-11-14.md`（§5 H0-H7、§6 立即指派顺序）、`docs/PROJECT-ROADMAP-HANDOFF-2026-11-12.md`（F1/F2 卡）
- Worktree：`D:/TF2_Native_Worktrees/t105-e5`，分支 `candidate/e5`
- HEAD：`94e7612`（进场）→ `11885d7`（代码/证据收尾）→ 本报告提交（文档收尾，其后无源码改动）
- 产品 exe：`00ec8b99…`（进场）→ `cfcdbe5d…`（收尾，2,988,544 B）
- 结论一句话：**修掉 2 个门禁/探针缺陷（A12 期望、探针夹具）+ 1 个用户可见产品缺陷（暂停丢小地图），完成七图小地图投影验收；地图渲染、音频、多视图、发布链未动。**

---

## 1 任务清单覆盖

| 接手文档的卡 | 本轮状态 | 说明 |
|---|---|---|
| **H0** 基线、资产、任务板 | **完成** | 分支/HEAD/状态/锁/进程/exe 身份 + 七图 BSP 与 13 条 demo 实测哈希 |
| **H1** 高风险缺陷闭环 | **部分** | 缺陷台账对齐 HEAD（主表 35 条，本轮新增 D33/D34/D35/D36）；只修了 D09/D10/D34，其余未复现 |
| **H2** 七图渲染 | **未开始** | 仅做资产清单与 849/919 取证（F1-5 仍 OPEN） |
| **H3** 播放/相机/HUD/用户流 | **部分** | 修了「暂停丢小地图」（相机→面板链路的用户可见缺陷）；POV/STV 矩阵、音频未动 |
| **H4** 角色/武器/特效 | **部分** | 只修了 `animation_viewmodel_probe` 合成夹具；九职业矩阵、ViewModel、特效未动 |
| **H5** 小地图 + 多视图 | **小地图大部分完成** | 七图面板/投影/目标点 PASS；resize/DPI、线稿、载荷车图标未完成；多视图未开始 |
| **H6** 架构/工程 | **未开始** | CI、`/W4`、`main.cpp` 拆分、缓存测量均未动 |
| **H7** 集成/冻结/发布 | **未开始** | 未冻结、未跑 15 步发布链 |

---

## 2 进场基线（H0）

| 项 | 值 |
|---|---|
| `git status --porcelain -uall` | `M docs/PROJECT-ROADMAP-HANDOFF-2026-11-12.md`、`?? docs/PROJECT-HANDOFF-2026-11-14.md`（**进场即存在，全程未回退、未提交**） |
| 产品 exe | `native/build-nmake/tf2_demo_native.exe`，`00ec8b99fc779327eb2cf0eb0cb9a0e59eca3a4d0413e1ea29b36c7bcbec2078`，2,988,544 B |
| identity sidecar | `sourceContent=1d9c0ead…`、`dirty=0`、`sourceTree=6d203db8e729`、`buildUtc=2026-11-13T16:49:55Z` |
| host lock | 无锁；产品进程 0 |
| 误导项 | `native/build/Release/tf2_demo_native.exe`（`de3d0927…`）是过时构建目录，门禁不用它；真正的产品 exe 在 `native/build-nmake/` |

七图资产（实测 SHA-256，完整表见 `docs/BATCH-G1-2026-11-14.md` §1.1）：7 个 BSP 全部可解析，其中 process/gullywash/sultry/product/bagel 在 `tf/download/maps/`，sunshine/upward 在 `tf/maps/`。demo 14 条：6 图 × POV+STV（12）+ Upward STV（1）+ 九职业动作素材（1）。

---

## 3 派工与并发

| 角色 | 执行者 | 任务 | 结果 |
|---|---|---|---|
| Integrator | 主 agent | H0 基线、构建/抓帧/变异窗口、提交 | 7 笔提交 |
| Animation QA | 只读子 agent | F2-2a `loop=false` 归因 | 定位为夹具缺陷（后被证实并修复） |
| Map QA | 只读子 agent | F1-5 849/919 归因 | 机制证清、来源未闭合，保持 OPEN |
| QA / 台账 | 只读子 agent | H1 缺陷台账合并 | 主表 35 条 + 本轮 4 条（D33-D36） |

并发规则执行：三个只读 agent 并行（不构建、不抓帧、不跑产品）；所有构建/抓帧/变异串行在 `.scratch/lockrun.sh`（内部走 `verify-lock.sh` 的 pid+heartbeat 锁）内，每轮结束 `VERIFY-LOCK=RELEASED`，产品进程 0。

---

## 4 逐项完成情况

### 4.1 H0 基线（完成）

产出：`docs/BATCH-G1-2026-11-14.md` §1、`D:/_shared/EXEC-2026-11-14/G1/bsp-inventory.tsv`、`demo-inventory.tsv`。

### 4.2 F2-1a 小地图 A12 变异判定（完成，PASS）

- **缺陷**：2026-11-14 首跑的 A12 三个变异都被抓到，但 m2/m3 的期望红因写错，判 FAIL（门禁缺陷，非产品回归）。
  - m2 翻转箭头符号把点移到**身后**，该红的是 `a heading dot 4.0px ahead`；旧期望写 `no heading dot`（那句恰好变绿）。
  - m3 把面板锚到左边，而 A2 的 PANEL 矩形由 `panel_rect(w,h)` 从帧尺寸算出，等于断言**规格**，永远绿；只有像素计数变红。
- **修法**（`e3e0b7b`）：给 A2 增 4 条**实测 BBOX** 位置断言（A2b：变化像素的包围盒必须等于规格矩形），m3 期望改为 `the changed pixels start at the panel's left edge`；m2 期望改为 ahead 断言。
- **证据**：`MINIMAP-HUD=PASS assertions=104 skipped_controls=0`，m1/m2/m3 `red=3/5/4`，还原构建逐字节复现 exe。
- **证明**：三个注入缺陷都被以「说得出缺陷名字」的断言判红；锚点变异现在由位置断言判红，不再靠像素计数副作用。
- **不证明**：其余五图的视觉正确性（当时只有 Upward/Sunshine 有帧）；也不证明 A2 能发现「同角落但尺寸错」以外的布局缺陷。

### 4.3 F2-2a `animation_viewmodel_probe` 合成 `loop=false`（完成，PASS）

- **根因**：`loopAndOnce()` 用 `studioShell(1,1,1)` 造了**只有一根骨骼、而那根就是 skeleton root** 的合成模型；产品 `readBoneFrame()` 对 `bone.parent < 0` 直接返回 bind 变换（T62 决策，`animation_decoder.cpp:289`），于是每帧 `modelPosition` 恒定，末断言 `!samePositions(frame0, frame3)` **结构上永不成立**。
- **修法**（`68b2555`）：骨架改 2 骨、动画块指向 child（bone 1），`bindSequence` 加默认 `sequenceAt` 参数；**断言一字未改**。
- **证据**：`selfTest=true loop=true`，15 个自检标志全 true；`realLoop=true`、`bones=76`、`anims=1012`、`motionSequence=AttackStand_PRIMARY` 全部未变。
- **证明**：失败原因是夹具，不是 loop/once 帧算术，也不是产品解码器。
- **不证明**：root 骨骼通道被忽略这一 T62 决策在视觉上完全正确；也不证明 loop 的可听/可见表现。

### 4.4 F1-5 Sultry 849/919（未完成，保持 OPEN）

- **849**：同姿态 `--world-texel` 读 84,83,81（α255）、`--world-albedo` 读 0，两条视图只差逐顶点乘子 ⇒ **机制**是 vertex colour 乘子，不是纹理/alpha。但 **0 的来源未闭合**（BSP 读入侧无顶点色字段，本树 6 个写入点均非 0），缺一次 COLOR0 回读。
- **919**：离线复算 ordinal→face 与 face-id 帧解码与 trace 一致（849→17804、873→17808、919→6343）⇒ **不是 face 选错**；分叉在归属/覆盖（trace 预测 texel 82，draw 的 texel 帧 210；同面邻点 902 两者一致）。
- **证据**：`docs/BLACKPOINT-849-919-2026-11-14.md`，含 4 帧最小实验设计（face / texel / face-id / entity-coverage，同相机同 tick）。
- **未做**：那 4 帧没跑（需要产品抓帧窗口，本轮排给了小地图）。

### 4.5 H1 缺陷台账（完成台账，未做复现）

`docs/DEFECT-REGISTER-2026-11-14.md`：进场 35 条（P0 0 / P1 7 / P2 19 / P3 9；产品 13 vs 门禁+证据 17），收尾追加 D33/D34/D35/D36。台账同时记录了文档本身的矛盾：路线图称 HEAD "clean" 而实际有 2 个改动、sidecar `sourceTree` 与文档所称 `f533293` 不一致、`world-material-semantics-check` 的 blocked 理由已过期、Phase F 报告 18 vs 15 笔提交。

### 4.6 H5 / D08 七图小地图（本轮主体，大部分完成）

**a) 抓帧方法**：每图固定相机 `cp0 + (1200, -1200, 300)`、`--start-paused`、各跑「带 `--hud-minimap`」与「不带」两次，共 14 次产品运行；判定完全离线读帧。

**b) 结果**：`MINIMAP-FIVE checks=205 failed=0`

| 地图 | 检查数 | 朝向对 | 目标点 | inside / outside |
|---|---:|---:|---:|---|
| cp_process_f12 | 39 | 20 | 5 | 25600 / 0 |
| cp_gullywash_f9 | 39 | 20 | 5 | 25600 / 0 |
| cp_sultry_b8a | 39 | 20 | 5 | 25600 / 0 |
| koth_product_final | 7 | 0 | 1 | 25600 / 0 |
| koth_bagel_rc11 | 7 | 0 | 1 | 25600 / 0 |
| pl_upward_f12 | 28 | 12 | 4 | 25600 / 0 |
| cp_sunshine | 39 | 20 | 5 | 25600 / 0 |

每图覆盖四件事：面板矩形（实测 BBOX = 规格）、每个目标点在**由 BSP 实体独立取出**的世界坐标投影处有对应队色环、逐对坐标序（x 同号 / y 反号）、trace 无 `no-camera-pose`。

**c) 发现并修复的产品缺陷（`a153030`）**

- 现象：`--start-paused` 且不带 `--debug-camera` 时，第 1 帧 `state=ready shapes=14`，**第 2 帧 `reason=no-camera-pose shapes=0`**，抓到的帧与「不加开关」**逐字节相同**。用户一暂停，小地图整块消失。
- 根因：`minimapFrame.valid = cameraTraceReady`，而 `cameraTraceReady` 只在「本帧有新相机读数」时为真；暂停 tick 不重算相机。
- 修法：面板记住**同一张图**最后一次位姿，在无读数帧复用；换图仍不画。复用显式跟踪的状态，不读 `cameraTrace*` 局部变量，原来「不读未初始化位姿」的约束保留。
- 影响面：修复前，本任务的五图抓帧**根本无法完成**——这也是它此前没被发现的原因：既有小地图门禁全部带 `--debug-camera`。

**d) 发现但未修的文档缺陷（D33）**

面板只由「背景 + 4 条边框 + 控制点环 + 观察者点 + 3 个航向点」组成，`main_hud.cpp` 里没有任何地图几何；形状计数与之一致（Upward 13、五图 14、单点图 10、边缘裁剪 11）。因此 `PHASE-F-REPORT:233`、`PROJECT-HANDOFF:334`、`PROJECT-ROADMAP:784` 说的 "BSP-derived linework" **在本构建里不成立**：没有线稿，只有标记点。未改产品（线稿属未实现功能，不是回归），登记为 D33 并要求文档改口径。

**e) 未覆盖**：resize/DPI、点击缩放、楼层选择（UNSUPPORTED）、载荷车图标（UNSUPPORTED）、声明 bounds 的独立验证（D35，BSP 世界包围盒本就与声明矩形不同）、两张 koth 的朝向（单点无对可比，PASS 但弱）。

### 4.7 Upward 资产更正

用户提供的 `a9630c…pl_upward_f12.dem` 头部 `client="SourceTV Demo"`（serveme.tf #1570108、85,204 ticks），与 testdata 里那份**字节相同**（`7e047aff…`）。所以 Upward **有 STV、缺 POV**，与初稿结论相反。已归档到 `E:/demo测试/pl_upward_f12/{STV,地图}`，并更正 `docs/BATCH-G1-2026-11-14.md` §1.1。七图现在 STV 齐全、POV 缺 1 条。

---

## 5 证据清单

| 内容 | 路径 | SHA-256 |
|---|---|---|
| A12 历史 FAIL（2026-11-14 首跑，门禁缺陷，已修） | `D:/_shared/EXEC-2026-11-13/F2/minimap-mutation/gate.log` | `962abae6…` |
| A12 门禁修复后（exe `00ec8b99`），F2-1a 证据 | `D:/_shared/EXEC-2026-11-14/G1/minimap-a12-gate.log` | `b5bf0d1b…` |
| A12 产品修复后重跑（exe `cfcdbe5d`），**当前身份** | `D:/_shared/EXEC-2026-11-14/G1/minimap-a12-post.log` | `79c39fea…` |
| 七图判定 | `D:/_shared/EXEC-2026-11-14/G1/minimap-five/check.log` | `d478c8e1…` |
| 七图帧（7×2 BMP + trace） | `D:/_shared/EXEC-2026-11-14/G1/minimap-five/` | 单帧哈希见 `docs/D08-MINIMAP-SEVEN-MAPS-2026-11-14.md` §1 |
| 暂停缺陷正控 | `D:/_shared/EXEC-2026-11-14/G1/paused-panel/on.trace` | `429793fe…` |
| 探针自检 | `D:/_shared/EXEC-2026-11-14/G1/probe/loopfalse.log` | `445ada6c…` |
| 资产清单 | `D:/_shared/EXEC-2026-11-14/G1/{bsp,demo}-inventory.tsv` | 内容即哈希 |

说明：两份 A12 日志是**不同 exe** 上的两次独立 PASS（`00ec8b99` 与 `cfcdbe5d`），文件名不同（`-gate` / `-post`），引用时必须带 exe 哈希；历史 FAIL 那份在 2026-11-13 的共享目录。

---

## 6 身份演化与作废面

| 阶段 | exe | 字节 | sidecar sourceTree | 后果 |
|---|---|---|---|---|
| 进场 | `00ec8b99…` | 2,988,544 | `6d203db8e729`（dirty=0） | Phase F 的 canonical 二进制；绑定它的地图渲染/音频/动画证据仍停在这里 |
| 本轮门禁修复后 | `00ec8b99…`（未重建） | 2,988,544 | 同上 | 门禁/探针改动不触碰产品源码，可复用同一二进制 |
| 产品修复后（`a153030`） | `cfcdbe5d…` | 2,988,544 | `a15303062d5e` | **全部绑定 `00ec8b99` 的小地图证据作废**，A12 与七图帧已在新 exe 上重跑 |
| 收尾（`11885d7` 及其后的本报告提交） | `cfcdbe5d…` | 2,988,544 | 收尾后刷新到最终 HEAD（dirty=2） | 收尾提交只动 `docs/`；`git diff a153030..HEAD -- native/src native/include native/CMakeLists.txt` 为空 |

`dirty=2` 只来自用户既有的两份文档改动，产品源码干净。

---

## 7 提交清单（`94e7612..11885d7` 共 7 笔，另有本报告 1 笔）

| 提交 | 类型 | 内容 |
|---|---|---|
| `e3e0b7b` | test(hud) | A12 m2/m3 期望修正 + A2b 实测 BBOX 断言 |
| `68b2555` | test(animation) | `loopAndOnce` 夹具改 2 骨 |
| `4ba137f` | docs | G1 批次记录、缺陷台账、两篇归因文档 |
| `c03f9fe` | docs | 身份区间说明 + D09/D10 结账 |
| `a153030` | fix(hud) | 暂停帧复用同图位姿，小地图不再消失 |
| `1eca496` | docs | 七图证据 + 暂停缺陷 + D33 + Upward 更正 |
| `11885d7` | docs | D08 身份边界与未做的负控 |
| 本报告 | docs | G1 完整任务报告（只动 `docs/`） |

未推送、未打 tag、未跑 15 步发布链。

---

## 8 未完成与建议下一步

| 优先级 | 事项 | 依赖 / 窗口 |
|---|---|---|
| 1 | **D36** 用父提交 `94e7612` 的二进制重做暂停缺陷负控（独立构建目录） | 需独占构建窗口，约 6 分钟 |
| 2 | **D08 剩余**：resize/DPI 两档抓帧；click/zoom | 与 UI 写窗口串行 |
| 3 | **D01 七图渲染矩阵**：每图 POV+STV 的几何/材质/天空/控制点 | 需先补 Upward POV；与 H2 单写者窗口 |
| 4 | **D03 849/919**：按 4 帧实验跑一次（同相机 `-600 -600 950 20 45`、tick 0） | host lock 抓帧窗口 |
| 5 | **D02 3D skybox**：先写 `sky_camera` 设计与像素 oracle，再动世界渲染 | 与 H2 同窗口 |
| 6 | **D33 文档口径**：把三处 "BSP-derived linework" 改成「面板只有标记点，无线稿」 | 纯文档，可随时 |
| 7 | **D06 重绑**：非小地图证据（渲染/音频/动画）仍绑 `00ec8b99`，需一次全量重绑 + 作废清单 | 需 release lead 授权 |
| 8 | H6 CI/`/W4`/`main.cpp` 拆分；H7 冻结与发布链 | 各自独立窗口 |

---

## 9 结论边界

**本轮 PASS 只覆盖**：小地图门禁自身的三变异判定（104 断言）、探针合成自检（15/15 标志）、七图小地图面板位置/投影朝向/控制点落位（205 检查）、暂停帧保留面板、基线与资产清单的可复现性。

**不是 PASS 的**：任何地图几何/材质/天空渲染、音频（含可听设备）、POV/STV 相机矩阵、九职业动画与武器、多视图、安装/用户流程、15 步发布链。

**未验证的**：缩放比例的世界正确性、resize/DPI、两张 koth 的朝向（弱证据）。

---

## 10 复核命令

```bash
# 门禁（A12，需 host lock，约 15 分钟，会重建 4 次）
bash .scratch/lockrun.sh review -- bash minimap-hud-check.sh --mutation \
  --old-exe=D:/_shared/EXEC-2026-11-13/R0/bin/tf2_demo_native.exe

# 七图判定（离线，不构建不抓帧）
python .scratch/g1-minimap-five/check.py D:/_shared/EXEC-2026-11-14/G1/minimap-five

# 七图重抓（需 host lock，14 次产品运行）
bash .scratch/lockrun.sh review -- bash .scratch/g1-minimap-five/run.sh

# 探针自检（需先 build-target.sh animation_viewmodel_probe）
TF_ROOT="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf" ./native/build-nmake/animation_viewmodel_probe.exe

# 身份
sha256sum native/build-nmake/tf2_demo_native.exe
cat native/build-nmake/tf2_demo_native.identity
git diff --stat a153030..HEAD -- native/src native/include native/CMakeLists.txt
```
