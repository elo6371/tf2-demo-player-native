# 任务完整报告 - 2026-11-14（G1 + G2）

- 窗口：2026-11-14 09:36 – 13:00（GMT+8）
- 依据：`docs/PROJECT-HANDOFF-2026-11-14.md`（§5 H0-H7、§6 指派顺序）、`docs/PROJECT-ROADMAP-HANDOFF-2026-11-12.md`（F1/F2）
- Worktree：`D:/TF2_Native_Worktrees/t105-e5`，分支 `candidate/e5`
- HEAD：`94e7612` → `a92b1bd`（12 笔提交）
- 产品 exe：`00ec8b99…`（2,988,544 B）→ `cfcdbe5d…`（2,988,544 B）
- 分册：`docs/BATCH-G1-2026-11-14.md`、`docs/G1-COMPLETE-REPORT-2026-11-14.md`、`docs/G2-2026-11-14.md`、`docs/D08-MINIMAP-SEVEN-MAPS-2026-11-14.md`、`docs/TRIAGE-LOOPFALSE-2026-11-14.md`、`docs/BLACKPOINT-849-919-2026-11-14.md`、`docs/DEFECT-REGISTER-2026-11-14.md`

---

## 1 一页摘要

| 类别 | 数量 | 内容 |
|---|---:|---|
| **产品缺陷修复** | 1 | 暂停后小地图整块消失（`a153030`） |
| **门禁/探针缺陷修复** | 3 | A12 期望红因写反（`e3e0b7b`）、探针合成夹具不可观测（`68b2555`）、`verify-iso.sh` 让 27 个门禁在本机跑不起来（`124cc3b`） |
| **证据缺口关闭** | 2 | 父提交负控（`d8bbaa05…` 复现缺陷）、小地图 resize（800×600） |
| **新登记条目** | 7 | 5 条 OPEN：D33 线稿口径、D35 缩放未独立验证、D38 探针路径/编码、D39 世界帧异常族、D40 设置写回；2 条当场闭合：D34、D36 |
| **验收通过** | 5 | 小地图门禁 104 断言 + 3 变异；七图小地图 205 检查；七图探针矩阵 14 次全 rc=0；黑点门禁 PASS；800×600 resize 39 检查 |
| **仍未做** | 8 类 | 见 §8（多数是缺资产/缺授权/需人在场，不是代码没写） |

一句话：**把一条"只能靠 --debug-camera 才成立"的小地图链路修成了用户真能用的链路，顺手挖出并修掉一个让 27 个门禁在本机全部失效的公共缺陷；地图渲染只做到"能出画面 + 七图探针账"，远不是验收。**

---

## 2 任务清单覆盖（接手文档 H0-H7）

| 卡 | 状态 | 依据 |
|---|---|---|
| **H0** 基线/资产/任务板 | **完成** | §3；七图 BSP + 14 条 demo 实测哈希 |
| **H1** 高风险缺陷闭环 | **部分** | 台账 39→40 条；只闭了 D09/D10/D34/D37/D36，其余未复现 |
| **H2** 七图渲染 | **仅输入层** | 七图探针矩阵（几何/材质/预算）+ 七图世界帧筛查；**不是**渲染验收 |
| **H3** 播放/相机/HUD/用户流 | **部分** | 修了暂停丢面板；POV/STV 矩阵、seek、音频未做 |
| **H4** 角色/武器/特效 | **部分** | 只修探针合成夹具；九职业矩阵、ViewModel、特效未做 |
| **H5** 小地图 + 多视图 | **小地图接近完成** | 七图投影/目标点/朝向 + resize；DPI、线稿、载荷车图标未做；多视图未开始 |
| **H6** 架构/工程 | **未开始** | 有阻塞（§8） |
| **H7** 集成/冻结/发布 | **未开始** | 未冻结、未跑发布链 |

---

## 3 进场基线

| 项 | 值 |
|---|---|
| `git status` | `M docs/PROJECT-ROADMAP-HANDOFF-2026-11-12.md`、`?? docs/PROJECT-HANDOFF-2026-11-14.md`（**进场即存在，全程保留，未提交**） |
| exe | `00ec8b99fc779327…`，2,988,544 B |
| sidecar | `sourceTree=6d203db8e729`、`dirty=0`、`buildUtc=2026-11-13T16:49:55Z` |
| 锁 / 进程 | 无锁 / 0 |
| 陷阱 | `native/build/Release/` 里的 exe（`de3d0927…`）是过时构建，门禁不用；产品 exe 在 `native/build-nmake/` |

七图资产：7 个 BSP 全部可读（5 个在 `tf/download/maps/`，sunshine/upward 在 `tf/maps/`）；demo 14 条 = 6 图 × POV+STV + Upward STV + 1 条九职业素材。

---

## 4 派工与并发

| 角色 | 执行者 | 产出 |
|---|---|---|
| Integrator | 主 agent | 基线、构建/抓帧/变异窗口、全部提交 |
| Animation QA | 只读子 agent | F2-2a 归因（夹具缺陷）→ 后由主 agent 修 |
| Map QA | 只读子 agent | F1-5 849/919 归因（机制清楚、来源未闭） |
| QA / 台账 | 只读子 agent | 缺陷台账 35 条主表 + 后续 5 条 |

规则执行：三个只读 agent 并行且**不跑产品**；所有构建/抓帧/变异串行在 `.scratch/lockrun.sh`（`verify-lock.sh` 的 pid+心跳锁）内；每轮 `VERIFY-LOCK=RELEASED`、产品进程 0。

---

## 5 缺陷处理总表

| ID | 内容 | 处理 | 证据 |
|---|---|---|---|
| **D34** | 暂停后小地图消失 | **修复** `a153030` | 父提交负控 `d8bbaa05…` 复现，修复后 25600/0 |
| **D09** | A12 m2/m3 期望红因写反 | **修复** `e3e0b7b` | 104 断言 PASS，m1/m2/m3 red=3/5/4 |
| **D10** | 探针合成 `loop=false` | **修复** `68b2555` | `selfTest=true loop=true`，15/15 标志 |
| **D37** | `verify-iso.sh` 让 27 个门禁 `iso-seed` 失败 | **修复** `124cc3b` | 黑点门禁 FAIL→PASS |
| **D36** | 暂停缺陷缺父提交负控 | **关闭** | 独立目录构建父提交，逐字节复现 |
| **D08**（resize 部分） | 单尺寸验证 | **关闭** | 800×600：39 检查 0 失败 |
| **D33** | 文档称 "BSP-derived linework"，实际无线稿 | **文档更正**（产品未改） | 形状计数 13/14/10/11 |
| **D35** | 缩放比例未独立验证 | 新登记 OPEN | 声明矩形 ≠ BSP 世界包围盒 |
| **D38** | 探针路径/编码要求不一致，且不认非 ASCII | 新登记 OPEN | `E:/demo测试/...` 空输出 |
| **D39** | 世界帧三族异常未归因 | 新登记 OPEN | 七图世界帧 |
| **D40** | 产品写回 `--settings-file` | 新登记 OPEN（已恢复） | 126 键改写，原 10 键值未变 |
| **D03** 849/919 | 黑点来源 | **仍 OPEN** | 门禁现已可跑，读数量化 |

---

## 6 逐项详述

### 6.1 小地图 A12：从"抓到了但判 FAIL"到"抓到了且说得出为什么"（`e3e0b7b`）

- **现象**：三个变异都被捕获，但 m2/m3 期望的红因不可能出现 → `failed=2`。
- **m2**：翻转箭头符号把点移到身后，该红的是 `a heading dot 4.0px ahead`，旧期望写的是 `no heading dot`（那句恰好变绿）。
- **m3**：A2 的面板矩形由 `panel_rect(w,h)` 从帧尺寸算出，等于断言**规格**；面板画到左边它仍然绿。新增 A2b：把**实测 BBOX** 与规格矩形比较。
- **结果**：`MINIMAP-HUD=PASS assertions=104`，m1/m2/m3 `red=3/5/4`，还原构建逐字节复现。
- **边界**：不证明五图视觉，也不证明"同角落但尺寸错"以外的布局缺陷。

### 6.2 探针合成 `loop=false`：断言本身不可能成立（`68b2555`）

- `loopAndOnce()` 用 `studioShell(1,1,1)`：唯一骨骼就是 skeleton root，而 `readBoneFrame()` 对 `parent<0` 直接返回 bind 变换（T62 决策）→ 每帧 `modelPosition` 恒定 → `!samePositions(frame0, frame3)` 永不成立。
- 修法：2 骨、动画指向 child，**断言一字未改**。
- 结果：`selfTest=true loop=true`；`realLoop`、`bones=76`、`anims=1012`、`motionSequence` 全未变。

### 6.3 暂停丢小地图：本轮唯一的产品修复（`a153030`）

- **现象**：`--start-paused` 且不加 `--debug-camera` 时，第 1 帧 `state=ready shapes=14`，第 2 帧 `reason=no-camera-pose shapes=0`，抓到的帧与「不加开关」**逐字节相同**。用户一暂停，地图就没了。
- **根因**：`minimapFrame.valid = cameraTraceReady`；暂停的 tick 不重算相机，读数为空。
- **修法**：面板记住**同一张图**最后位姿，在无读数帧复用；换图仍不画。复用显式跟踪状态，不读 `cameraTrace*` 局部变量，原「不读未初始化位姿」的约束保留。
- **负控**：父提交 `94e7612` 独立构建 `d8bbaa05…`（sidecar `sourceTree=94e7612dc07e`），同命令复现，on/off 同为 `df05b7e3…`。
- **正控**：同图加 `--debug-camera` 时每帧都有读数，修复前后都画。
- **影响面**：修复前，五图抓帧**做不出来**——既有门禁全带 `--debug-camera`，这条路径从未被覆盖。

### 6.4 `verify-iso.sh`：27 个门禁在本机全部失效（`124cc3b`）

- **现象**：`world-black-account-check.sh` 直接以 `reason=iso-seed` 退出，报 `\b85f044f7… != b85f044f7…`。
- **根因**：`sha256sum "$file"` 在**文件名含反斜杠**时给整行加 `\` 前缀；本机 `mktemp -d` 返回 Windows 路径 → 隔离副本带 `\`、fixture 不带 → 恒不相等。
- **影响**：`verify-iso.sh` 被 27 个门禁引用，凡是 seed 隔离目录的全部在本机失败。
- **修法**：改从 stdin 读（`sha256sum < "$file"`），输出里不再有文件名。
- **对照**：HEAD 版跑 `iso-trap-selftest` 同样 13 断言 / 4 失败，4 项全是"目录没被删除"（沙箱 safe-delete 拒绝删 Windows 路径），与本改动无关。

### 6.5 七图小地图：205 检查（`1eca496`）

- 每图固定相机（控制点 + (−1200, −1200, +300)）、开关/对照两次运行，共 14 次。
- 每图四项：面板矩形（实测 BBOX = 规格）、每个目标点在 **BSP 实体独立取出的坐标**投影处有队色环、逐对坐标序（x 同号 / y 反号）、trace 无 `no-camera-pose`。
- `MINIMAP-FIVE checks=205 failed=0`，七图全部 `inside=25600 / outside=0`。
- 两张 koth 只有 1 个控制点 → 朝向对数 0，弱证据，已在文档注明。

### 6.6 七图探针矩阵（D01 输入）

14 次探针全 rc=0：预算 `within`、`dropped=0`、材质 67–263、bumpmap `missing=0`。**不证明**任何一帧长什么样。

### 6.7 七图世界帧筛查（D01 输入，`1dff78e`）

7 帧（STV 相机、tick 30000、`--playback-speed 8`、`--hud-off`）全部能渲染出可辨内容。记录三族**未归因**异常：高频白噪（Gullywash/Bagel/Sultry）、黑区（Process/Gullywash/Product）、Sunshine 疑似水面的大片浅蓝平面。**不称缺陷**。

### 6.8 849/919（仍 OPEN）

- 849：逐顶点乘子机制成立（texel 84,83,81 vs albedo 0），**0 的来源未闭合**。
- 919：**不是 face 选错**；门禁读数 `predicted albedo=82,81,79` vs `texel/albedo frame=210,208,197`，量化了 trace 与 draw 的分叉。
- 需给 trace 加 `face=` 字段才能闭环——又一次产品改动，本轮未做。

---

## 7 证据与身份

### 7.1 关键证据

| 内容 | 路径 | SHA-256 |
|---|---|---|
| A12 门禁（`00ec8b99`） | `D:/_shared/EXEC-2026-11-14/G1/minimap-a12-gate.log` | `b5bf0d1b…` |
| A12 门禁（`cfcdbe5d`） | `D:/_shared/EXEC-2026-11-14/G1/minimap-a12-post.log` | `79c39fea…` |
| 七图小地图判定 | `D:/_shared/EXEC-2026-11-14/G1/minimap-five/check.log` | `d478c8e1…` |
| 暂停缺陷正控 | `D:/_shared/EXEC-2026-11-14/G1/paused-panel/on.trace` | `429793fe…` |
| 暂停缺陷负控（父提交） | `D:/_shared/EXEC-2026-11-14/G2/parent-paused/` | on/off 同为 `df05b7e3…` |
| 探针自检 | `D:/_shared/EXEC-2026-11-14/G1/probe/loopfalse.log` | `445ada6c…` |
| 黑点门禁 | `D:/_shared/EXEC-2026-11-14/G2/black/check.log` | `261e4d09…` |
| resize | `D:/_shared/EXEC-2026-11-14/G2/minimap-resize/` | 见 `docs/G2-2026-11-14.md` §2 |
| 探针矩阵 | `D:/_shared/EXEC-2026-11-14/G2/probe-matrix/` | 含探针 exe 哈希 |
| 世界帧 | `D:/_shared/EXEC-2026-11-14/G2/world-frames/` | 7 BMP + trace |

### 7.2 身份演化

| 阶段 | exe | sourceTree | 后果 |
|---|---|---|---|
| 进场 | `00ec8b99…` | `6d203db8e729` | Phase F 的 canonical；渲染/音频/动画证据仍绑这里 |
| 门禁/探针修复 | `00ec8b99…`（未重建） | 同上 | 未触产品源码，二进制可复用 |
| 产品修复 `a153030` | `cfcdbe5d…` | `a15303062d5e` | 全部绑 `00ec8b99` 的小地图证据作废，已在新 exe 重跑 |
| 收尾 `a92b1bd` | `cfcdbe5d…` | `a92b1bd65b19`（dirty=2） | 收尾提交只动 `docs/`/门禁；`git diff a153030..HEAD -- native/src native/include` 为空 |

`dirty=2` 只来自用户既有的两份文档；`native/src`、`native/include`、CMake 全干净。

---

## 8 未完成与阻塞（按"为什么做不了"分类）

| 缺口 | 类型 | 具体阻塞 |
|---|---|---|
| `pl_upward_f12` 的 **POV** | **缺资产** | 用户给的 `autorecord_2026-11-13_13-59-18.dem` 地图是 `pl_upward`（已归档到 `E:/demo测试/pl_upward/POV/`），不能替代 f12 |
| 七图渲染**验收**（POV+STV、具名物件、控制点、对照区） | **工作量大 + 缺资产** | 每图两路相机 × 多物件；先要有 f12 POV |
| 可听音频 | **需人在场** | `--audio-device` 真机试听，无人值守窗口无法判"听得见" |
| DPI 缩放 | **缺开关** | 产品无设置 DPI 的入口，改显示设置不属于本窗口 |
| 线稿/楼层/载荷车图标 | **未实现** | 面板只有标记点（D33） |
| 3D skybox、多视图 | **未实现** | 属 H2/H5 的实现工作量 |
| D03 849/919 闭环 | **需产品诊断字段** | trace 缺 `face=`，需改产品 + 重建 + 重钉证据 |
| CI | **不能验证** | 远程存在但未推送（不该由我推）；`env-msvc.sh` 硬编码本机 MSVC/SDK |
| 全量重绑 `00ec8b99` 的旧证据 | **需授权** | 重绑要重建+列作废清单，属 release lead |

---

## 9 结论边界

**可以说的**：小地图门禁三变异判定有效（104 断言）；七图小地图面板位置/投影朝向/控制点落位成立（205 检查）；暂停帧保留面板（前后对照二分）；探针合成自检 15/15；七图几何/材质/预算账目齐全（探针级）。

**不能说的**：任何一张地图"渲染通过"；材质/光照/天空/水面正确；音频可用；POV/STV 相机矩阵正确；安装与用户流程可用；发布就绪。

**未验证的**：缩放比例的世界正确性（D35）、DPI、两张 koth 的朝向（弱证据）、世界帧三族异常的性质（D39）。

---

## 10 提交清单（`94e7612..a92b1bd`，12 笔）

| 提交 | 类型 | 内容 |
|---|---|---|
| `e3e0b7b` | test(hud) | A12 期望修正 + A2b 实测 BBOX |
| `68b2555` | test(animation) | `loopAndOnce` 夹具 2 骨 |
| `4ba137f` | docs | G1 批次记录、台账、两篇归因 |
| `c03f9fe` | docs | 身份区间 + D09/D10 结账 |
| `a153030` | **fix(hud)** | **暂停帧复用同图位姿** |
| `1eca496` | docs | 七图证据 + D33 + Upward 更正 |
| `11885d7` | docs | D08 身份边界 |
| `2e590f5` | docs | G1 完整报告 |
| `124cc3b` | **test(gate)** | **`verify-iso.sh` 改 stdin 哈希** |
| `1dfefb1` | docs | G2 结账 + 线稿口径更正 |
| `1dff78e` | docs | 七图世界帧筛查 |
| `a92b1bd` | docs | 设置写回事故与脚本修复 |

未推送、未打 tag、未跑 15 步发布链。

---

## 11 过程教训（写给下一位）

1. **门禁全带 `--debug-camera`，等于没测过暂停**。新增抓帧脚本要么带固定相机、要么显式验证"无读数帧"。
2. **`--start-paused` 会钉死 tick 0**：`--capture-tick N`（N>0）永不触发（实测 14,062 帧仍 `tick=0`）。抓中间帧要用 `--playback-speed`。
3. **跑产品永远给设置副本**：`--settings-file` 指向 tracked fixture 会被写回（D40）。
4. **`sha256sum "$file"` 在 Windows 路径下会多一个 `\`**：比对哈希一律走 stdin。
5. **探针路径口径不统一**：`world_geometry_probe` 要 TF 相对、`world_feature_probe` 要绝对且不认非 ASCII（D38）。

---

## 12 复核命令

```bash
# A12 门禁（需 host lock，约 15 分钟，重建 4 次）
bash .scratch/lockrun.sh review -- bash minimap-hud-check.sh --mutation \
  --old-exe=D:/_shared/EXEC-2026-11-13/R0/bin/tf2_demo_native.exe

# 七图判定（离线）
python .scratch/g1-minimap-five/check.py D:/_shared/EXEC-2026-11-14/G1/minimap-five

# 七图重抓（需 host lock，14 次产品运行）
bash .scratch/lockrun.sh review -- bash .scratch/g1-minimap-five/run.sh

# 暂停缺陷 正控 / 负控
bash .scratch/lockrun.sh review -- bash .scratch/g1-minimap-five/paused-panel.sh
OUT=/tmp/neg EXE=D:/_shared/EXEC-2026-11-14/G2/parent-bin/tf2_demo_native.exe \
  bash .scratch/lockrun.sh review -- bash .scratch/g1-minimap-five/paused-panel.sh

# 七图探针矩阵 / 世界帧
bash .scratch/lockrun.sh review -- bash .scratch/g2-probe-matrix.sh
bash .scratch/lockrun.sh review -- bash .scratch/g2-world-frames.sh

# 黑点门禁
bash .scratch/lockrun.sh review -- bash world-black-account-check.sh

# 身份
sha256sum native/build-nmake/tf2_demo_native.exe
cat native/build-nmake/tf2_demo_native.identity
git diff --stat a153030..HEAD -- native/src native/include native/CMakeLists.txt
```
