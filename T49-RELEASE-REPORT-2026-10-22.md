# T49 发布验收报告 — 2026-10-21/22

验收提交：`783931ab9ff1c08067bfdee1e6286c9dbcff46fb`
分支：`p0-entity-protocol`（集成工作树 `D:\TF2_Native_Test`）
冻结基线：`8f73d0f`（T44-T48 合并日志），其上依次叠加
T26 缓存修复 `3e32b3f`、step-22 证据刷新 `0d6abf1`、第 24 步宿主竞态修复 `783931a`。

## 结论

**PASS。**

- **focused 门禁**：全部 PASS（T26 forward/mutation、安装 lifecycle/readiness、step 22、
  ViewModel 23/24、音频 content/subsystem，含各自的变异套件）。
- **完整链**：`verify-all.sh --quick` **PASS** — `VERIFY=PASS`、`BUILD=PASS`、
  **24/24 步通过、零 FAIL 行**，`rc=0`。
- **第 24 步**：forward `114 OK / 0 FAIL`；mutation `caught=6 of 6`、
  `VIEWMODEL-COMPANION-MUTATION=PASS`、`rc=126/127 = 0`。

PASS 的依据是链本身：这条链在**修复后的冻结树**上一次性跑完 24 步。第一次链的红
（第 24 步 5/6）没有被掩盖或删除，它作为证据保留在下文与
`.scratch/t49/notpass-preserved/`，并被诊断为一个可复现的宿主竞态后由
`783931a` 修复；本报告同时给出两次链的对照。

## 这一轮改了什么，没改什么

T49 是 T43 报告要求的发布验收跟进。**产品没有变**：自 `8f73d0f` 以来的全部差异是
三个门禁脚本、一个测试包装脚本，加上重新生成的证据。

```
git diff --stat 8f73d0f..HEAD -- native/     ->  空
```

| 文件 | 提交 | 变更 |
|---|---|---|
| `world-geometry-hole-capture.sh` | `3e32b3f` | 抓帧缓存从"存在性测试"改为"身份+出处测试" |
| `world-geometry-hole-check.sh` | `3e32b3f` | 第 2 节：先跑矩阵，再用副本证明规则会拒绝缺位姿的 trace 与变更的身份 |
| `viewmodel-companion-check.sh` | `783931a` | `run_product` 对"启动器无法执行 exe"做一次有界重试（第 24 步宿主竞态） |
| `verify-all.sh` | `783931a` | 第 24 步 forward 与 mutation 之间有界等待，跨过映像释放窗口 |
| `evidence/t26-world-holes/analysis/{identity,point-trace,seam-eye}.txt` | `3e32b3f` | 刷新为产出它的那棵树 |
| `evidence/weapon-animation/*` | `0d6abf1` | focused-e 自身产生的见证帧与读数 |
| `_shared/t49/focused-d.sh`（仓库外） | — | 每个门禁一个输出目录、两种模式共用（见第 2 节） |

## 1. T26 缓存校验缺陷与修复（`3e32b3f`）

### 缺陷

T43/T49 的发布运行复用了 18 帧矩阵，而那些 trace 早于 `scene: camera ... eye=... src=demo` 这一行。抓帧路径接受"trace 里有 `capture_written=1` 且 tick 正确"的帧，这不是**出处**：语义门禁随后红了，因为它无法说出自己证据背后的相机位姿（"the world-ids trace does not name a pose at tick 2000"）。

### 修复

一个帧现在只有在下列**全部**成立时才可复用，且每项都在抓帧时写入 `<name>.identity`：

- 身份 schema 版本、`HEAD`、产品 exe sha256、demo sha256、BSP sha256、armed tick、窗口模式、精确 flags、抓帧脚本自身 sha256；
- trace 必须带 `capture: armed ... frame_tick=2000`，**并且**在该 armed 行之前有一条含 `eye=` 与 `src=` 的 `scene: camera`。

被拒绝的帧会重抓，且原因按名打印（`trace-no-camera-pose`、`trace-camera-no-eye`、`identity-mismatch`…），所以"重抓"不会再是静默行为。`--recapture` 强制重抓整个矩阵；`--validate <dir>` 不启动产品，逐帧报告原因。

### 门禁不相信规则，它验证规则

第 2 节先跑矩阵，再在**副本**上跑规则：

| 控制 | 读数 |
|---|---|
| 从 `world-ids.trace` 删除位姿行 | `reason=trace-no-camera-pose` |
| 改一处身份字段（`exe=`） | `reason=identity-mismatch` |
| 本次运行写出的矩阵 | 下一轮 18/18 可复用 |

这个控制的**第一次尝试是红的**：它当时跑在矩阵之前，评判的是一个在 identity 文件出现之前抓的矩阵。控制现在跑在它所评判的抓帧之后——这一点保留在提交信息里，不隐瞒。

### T26 实测数字

- **24/24** 方向探针解析成功（6 个 seam 点 × 4 方向）。任务简报里的"32/32"仍以实测总数为准，属文档错误。
- 该相机位姿下，8 个具名 seam 面中 **0 个** derived-back。
- 正控：产品剔除移除了 **81,218** 个 face-id 像素、跨 19 个面、其中 18 个非 seam，最大面 **17605**（disp/side-0、derived-back、33,167 px）。
- 缓存：第二轮 18/18 帧被复用，两个缓存控制均转红。
- 变异 M1–M6 全部被抓；M6（剔除永不运行）得到 `removed=0` 而 8/8 seam 面仍 derived-front，证明 seam 结论来自几何而非剔除。

## 2. focused 门禁（在 `0d6abf1`/`783931a` 上各跑一次，主机锁下串行）

### 先修掉一个包装脚本缺陷

第一次 `focused-d` 失败在**包装脚本**，不在产品：它把 `.scratch/t49/lifecycle-forward` 传给 forward，把 `.scratch/t49/lifecycle-mutation` 传给 mutation，但两个门禁的 mutation 模式都从 **forward 的目录**读前置产物。结果 lifecycle 报 3 条 `forward artifact ... = absent`，readiness 丢 20 项检查（没有 forward fixtures 可供抓帧，于是每个变异/还原运行都启动失败）。变异本身是被抓到的。每个门禁一个目录、两种模式共用，是这两个门禁自己写明的契约；包装脚本已修正并在干净树上重跑。

### 结果

| 门禁 | verdict | 日志 |
|---|---|---|
| T26 world holes（forward） | **PASS** | `.scratch/t49/t26.log` |
| T26 mutation（M1–M6） | **PASS**，6/6 被抓，全部逐字节还原 | `.scratch/t49/t26-mutation.log` |
| T37/T42 安装生命周期（forward） | **PASS** `cases=15 checks=213` | `.scratch/t49/lifecycle-forward.log` |
| T37/T42 安装生命周期（mutation） | **PASS** `cases=3 checks=25 mutations=3` | `.scratch/t49/lifecycle-mutation.log` |
| T47 安装就绪（forward） | **PASS** `cases=13 checks=145` | `.scratch/t49/readiness-forward.log` |
| T47 安装就绪（mutation） | **PASS** `cases=4 checks=53 mutations=3` | `.scratch/t49/readiness-mutation.log` |
| step 22 武器动画 | **PASS** 57 断言 / 0 FAIL | `.scratch/t49/step22.log` |
| ViewModel 23 动画 | **PASS** 154 / 0 | `.scratch/t49/step23.log` |
| ViewModel 24 伴生证明 | **PASS** 114 / 0 | `.scratch/t49/step24.log` |
| 音频内容 | **PASS** 39 / 0，变异 25/25 | `.scratch/t49/audio-content.log` |
| 音频子系统 | **PASS** 240 / 0 | `.scratch/t49/audio-subsystem.log` |

同一基线树上本轮较早阶段已完成、因源/钉/可执行文件均未变而**未重跑**的门禁：

| 门禁 | verdict | 日志 |
|---|---|---|
| T25 视觉基线 | PASS 70 / 0 | `.scratch/t49/t25.log` |
| T40 材质 forward | PASS 209 / 0（3 项 NOT VERIFIED） | `.scratch/t49/material-forward.log` |
| T40 材质 mutation | PASS 5/5 被抓并还原 | `.scratch/t49/material-mutation.log` |
| T36/T40 光照 forward | PASS 96 / 0 | `.scratch/t49/lighting-forward.log` |
| T41/T46 暗部账目 mutation | PASS 4/4 被抓并还原 | `.scratch/t49/dark-account-mutation.log` |
| T38 质量审计 | PASS 28 / 0 | `.scratch/t49/t38.log` |

### step 22：哪一行是断言，哪一行是反证控制

step 22 日志里有**两行** `BMP-SILHOUETTE`，只有一行是断言：

- 断言（同一性/直通对）：`changed=1 outside=0 interior_max=0` → **PASS**，这是 T39/T48 的轮廓规则；
- 反证控制（真实姿态改变）：`changed=14064 outside=11568 interior_max=172` → **FAIL**，门禁**要求**这个规则拒绝真实姿态变化，它确实拒绝了。把控制行读成失败是已知陷阱；门禁下一行就写着 `OK the silhouette rule rejects a real pose change`。

三个 tick 的帧彼此逐字节相同（`distinct=1`），因为该 demo 把自己的 eye 解析为 `0,0,0`：静止相机配动画世界必须是同一张图。这是该门禁赖以成立的 T34 结论，不是抓帧失败，且门禁断言该帧是一张图（547 种采样颜色）而非纯色填充。

### 安装就绪在本机上的读法

forward 为 13 cases / 145 checks、`titleTimeouts=0`；强制超时控制仍精确产出 `TITLE-CAPTURE-TIMEOUT`，同时 trace 记录 `valid=1`；两个冷启动循环的根选择决策完全一致。真正的系统级冷缓存行为仍为 **NOT VERIFIED**（见下）。

## 3. 完整链

这条链跑了**两次**，两次都在同一台主机、同一把锁下串行执行，日志都保留：

| | 第一次 | 第二次（验收） |
|---|---|---|
| 提交 | `0d6abf1` | `783931a` |
| 起止 | 00:01:24 → 01:33:20 | 09:59:39 → 11:29:31 |
| 结果 | `VERIFY=FAIL`（23/24） | **`VERIFY=PASS`（24/24）** |
| 红 | 第 24 步 mutation 5/6，8 次 `rc=126` | 无 |
| 日志 | `.scratch/t49/notpass-preserved/verify-all-quick.NOTPASS.log`（sha256 `7cd1fa86…`） | `.scratch/t49/verify-all-quick.log`（48,088 B，sha256 `51eb3c78…`） |

第一次的红**没有被覆盖或删除**：原文、第 24 步两份日志、以及它的 trace 目录都在
`.scratch/t49/notpass-preserved/`；下面第 3.3 节给出它的诊断。

### 3.1 第二次链（验收运行）的 24 步

命令：`bash verify-all.sh --quick`，主机锁 owner `t49-chain2`，2026-10-22 09:59:39 启动，
11:29:31 结束（约 1 小时 30 分）。

| 步骤 | 结果 |
|---|---|
| 1/24 干净全量 Release 构建 | PASS，`exe_count=44`，`BUILD=PASS` |
| 2/24 九 demo 普查 | PASS（entity_failures=0、malformed_packets=0） |
| 3/24 wire fixtures | PASS |
| 4/24 oracle 交叉校验（九个 demo） | PASS（`compared=9/9`） |
| 5/24 语料抽样 oracle 交叉校验 | PASS（`pinned_sample=8 of 24`） |
| 6/24 录制类型 | PASS |
| 7/24 探针输出增量性 | PASS（9/9 IDENTICAL） |
| 8/24 C++ 变异套件 | PASS（35 条 MUTATION-RED 全部按预期转红） |
| 9/24 普查判定可证伪 | PASS |
| 10/24 重建位置 vs oracle | PASS |
| 11/24 实体历史覆盖 | PASS |
| 12/24 武器世界模型 | PASS（`fixture_ok=12`） |
| 13/24 观察者焦点 | PASS（46 断言） |
| 14/24 属性槽新鲜度 | PASS（24 断言） |
| 15/24 SourceTV 覆盖 | PASS（`demos=9 packets=917543`） |
| 16/24 帧抓取 | PASS（18 断言） |
| 17/24 资源可达性 | PASS（12 断言） |
| 18/24 空闲自旋 | PASS（11 断言） |
| 19/24 属性查找 | PASS（10 断言） |
| 20/24 实体材质 | PASS（24 断言） |
| 21/24 动画可用性 | PASS（23 断言） |
| 22/24 武器动画 | PASS（57 断言；三个变异全部抓到） |
| 23/24 ViewModel 动画 | PASS（154 断言；变异抓到，4/4 红 4/4 控制成立） |
| 24/24 ViewModel 伴生证明（forward） | PASS（114 断言 / 0 FAIL） |
| 24/24 ViewModel 伴生证明（mutation） | PASS（`caught=6 of 6`，`rc=126/127 = 0`） |

**最终读出：`BUILD=PASS`、`VERIFY=PASS`、`rc=0`、24/24 步、零 FAIL 行。**

第 24 步的修复在链内实际生效了，日志原文：

```
VIEWMODEL-COMPANION=PASS (assertions_ok=114)
  settling the product image before the mutation half
  product processes=0 for 2s; the mutation half may start
MUTATION-CAUGHT=PASS (6 defects, each with its own named reading and control)
VIEWMODEL-COMPANION-MUTATION=PASS (all six proof mutations were caught)
```

日志中出现的 "must FAIL" 是变异用例的标题，不是失败读数。

### 3.2 第 24 步的修复（`783931a`）

第一次链的红是一个**宿主竞态**，不是产品缺陷。修复分两处，都在门禁侧，产品零改动：

**`viewmodel-companion-check.sh` · `run_product`**
- 最多**一次**重试，且只有当启动器自己报告无法执行该文件时才重试。判据读取日志文本
  `failed to run command ...: Permission denied|No such file or directory`，
  **不看退出码**——126/127 也是 `timeout(1)` 拒绝启动的返回值，而一个自己退出 126 的
  产品不会写下那一行。真实产品失败、错误退出码、崩溃仍然在第一次就失败。
- 重试前 `wait_for_exe` 有界等待（30 秒上限、0.5 秒轮询）直到进程表里没有产品映像。
- 两个更便宜的探测在本机实测**被否决**并写进注释：`cp "$EXE" /dev/null` 在产品运行时
  仍然成功；`"$EXE" --version` 在产品运行时也成功——两者都复现不了这个竞态。

**`verify-all.sh` 第 24 步**
- `wait_for_companion_image()` 在 forward 与 mutation 两半之间运行，要求进程表**连续四次**
  0.5 秒轮询（2 秒）保持为空，让交接跨过释放窗口；上限 60 秒并打印，永久占用仍然失败
  而不是挂死。

**修复后的实测**（先单独验证，再进链）：
- 单独 `viewmodel-companion-check.sh`：forward 114 OK / 0 FAIL，mutation `caught=6 of 6`，
  `rc=126/127 = 0`；
- 链内：同一读数，见上面日志原文。

### 3.3 第一次链的红：完整诊断（保留为证据）

1. **失败形态**：mutation 模式下 8 次产品启动返回 `rc=126`（另一次 `rc=127`），错误原文是
   `timeout: failed to run command 'native/build-nmake/tf2_demo_native.exe': Permission denied`
   与 `No such file or directory`。这些启动**根本没执行到产品**。
2. **时间窗**：第 24 步 forward 门禁的最后一次启动结束于 01:30:25；mutation 门禁的第一次
   启动在 **01:30:29**，相隔 **4 秒**。Windows 在进程完全退出前持有映像句柄，4 秒落在
   释放窗口内。
3. **同一门禁、同一棵树，空闲后单独跑 = 6/6 通过**：01:36 在主机锁下单独执行
   `viewmodel-companion-check.sh --mutation`，`caught=6 of 6`、`MUTATION-CAUGHT=PASS`、
   **零 rc=126**。
4. **恢复正确**：三次 `source restored byte-for-byte` 全 OK，`native/` 干净，无残留变异。
5. **不是"没抓到"**：5/6 中那一个"未抓到"是 `owner-cleared` 的控制断言因 `bmp=0`
   （没启动成功）读不到值而失败——被计成未抓，而不是缺陷漏网。

**结论**：这是**可复现的宿主竞态**（同一 exe 在两个门禁之间 4 秒内被重新执行），
不是产品回归，也不是断言被放宽。第一次链的红**按 T43 规则原样保留**，没有用改断言、
放宽阈值或删掉变异的方式消除；修复的是宿主状态的处理方式，6 个变异一个没少，断言
语义一字未动。

这一条对新竞态的暴露是 T49 唯一一次改动门禁**行为**（而非证据/缓存）的修复，因此
单独成一个提交，并在提交信息里写清了判据、被否决的两个探测、以及重试上限。

## 4. 仍未验证项（NOT VERIFIED）与 owner

以下各项沿用 T43/T44-T48 的既有记录，**没有任何一项被称为通过**：

1. **T40 tick-2000 参考相机上的高光**是实测的零（只有 28 px 材质法线覆盖）；WALL013 宽视角是第二个视角，不是替代。Owner T40/T45。最省的下一步：取第三个材质法线覆盖更大的位姿。
2. **T40 细节残差**（27 个非位移 WOOD/WALL007A 面上 2,812 px）：帧级原因是近裁剪面（已验证可逐字节复现修复前 A/F），像素级光栅化机制未隔离。Owner T40/T45。
3. **T41/T46 逐面 authored-side 拆分**与逐面移除读数仍为 superseded；202 px 的 edge-on 是**上界**而非通过。Owner T46。
4. **T37/T47 真正的系统级冷缓存**在本机不可测；20.4–24.0 s 的暖缓存稳定时间与 5 s 强制超时控制是现有证据。Owner T47。
5. **T48 逐采样 clip/NDC/viewport/depth 归属**不可得；结论被界定在 0.00022 px，非证明。Owner T39。
6. **T44 逐像素 derived side**与 0.1 单位精度的 eye 打印。Owner T44。
7. **T49 新增**：T26 缓存身份把抓帧脚本自身的 sha256 作为键，因此编辑该脚本会按设计使矩阵失效；它没有跨机器故事——不同 demo/BSP 路径的克隆会记录不同哈希并重抓，这是预期行为，但不构成可移植性测试。Owner T26。

## 5. 权威日志

| 用途 | 路径 | 字节 | sha256 |
|---|---|---|---|
| 验收链（PASS） | `.scratch/t49/verify-all-quick.log` | 48,088 | `51eb3c78ca9b4363c3a702d43a54464f507dca41ca2351df39a91a9504c70c76` |
| 第一次链（FAIL，保留） | `.scratch/t49/notpass-preserved/verify-all-quick.NOTPASS.log` | 48,075 | `7cd1fa86feb534940427393f1130149de76f0bd68c1a8a2564af92204d72b7ab` |

主机状态在两次链结束后均为：`product_processes=0`、无编译器残留、主机锁
`FREE`、`git status --porcelain native/` 为空。

## 6. 仓库状态与推送

- 验收提交 `783931a`；`native/` 自 `8f73d0f` 起零改动（产品源码、着色器、探针
  全部未动）。
- 完整链会把 `evidence/` 下的派生读数写脏（`evidence/verify/*`、
  `evidence/frame-capture/*`、`evidence/idle-spin/*` 等），这是链的既有行为；
  这些派生读数按既有做法不混入验收提交。
- 推送后核对：`git ls-remote`、ahead/behind、工作树干净、锁 `FREE`、零残留
  进程，读数写在本报告末尾的推送记录里。

## 7. 已知局限与下一步

1. **T26 缓存身份**依赖本机的 demo/BSP 绝对路径哈希：不同机器上首次运行会重抓
   整个矩阵。这是有意的（宁可重抓也不复用无法证明出处的证据），但没有跨机器
   可移植性测试。Owner T26。
2. **第 24 步的等待是 2 秒**。它覆盖了实测的 4 秒窗口（四次 0.5 秒轮询），但不是
   对所有可能的主机负载都成立的证明；重试上限 30 秒是第二道保护。Owner T24/T49。
3. 其余 NOT VERIFIED 见第 4 节，均有 owner 与"最省的下一步"。


## 8. 推送记录（2026-10-22）

**验收提交**是 `f3de4b7`——完整链跑在它上面，报告正文与第 5 节的所有读数都
描述它。**推送的 HEAD** 是 `6b71aeb`，即在验收提交之上加一个只改本文档的提交
（把本节的读数写进来）。这两个号都不是笔误，区别在此：

| 项 | 值 |
|---|---|
| 验收提交（链跑在它上面） | `f3de4b737b797b30801035085b0525d83389c87a` |
| 推送的命令 | `git push origin p0-entity-protocol`，先 `bc31533..f3de4b7`，后 `f3de4b7..6b71aeb` |
| `git ls-remote`（最终） | `6b71aeb6777a7cf85a2644d6e8ffa46ab473d7f2  refs/heads/p0-entity-protocol` |
| 本地 HEAD | `6b71aeb`（与远端一致） |
| ahead/behind | **0 / 0** |
| 工作树 | 干净（`git status --porcelain` 为空） |
| `native/` 自 `8f73d0f` | **零改动** |
| 主机锁 | `FREE`，`product_processes=0` |
| 残留进程 | 0（产品/编译器/构建工具） |
| 验收链日志 sha256 | `51eb3c78ca9b4363c3a702d43a54464f507dca41ca2351df39a91a9504c70c76` |

本报告的 sha256 不写死在这里：写下它就会立刻改掉它。要核对就用
`git show <推送 HEAD>:T49-RELEASE-REPORT-2026-10-22.md | sha256sum`。

推送的提交（在 T44-T48 的合并/证据提交之上）：

- `3e32b3f` gate(t49): require provenance, not existence, for the T26 capture cache
- `0d6abf1` evidence(t49): refresh the T49 step-22 witness frames and readings
- `783931a` gate(t49): retry a product image that is still busy, and settle it between step-24 halves
- `13645ca` evidence(t49): the acceptance chain — VERIFY=PASS, 24/24, on 783931a
- `f3de4b7` chore: ignore the trace directories and mutation frame T49 exercised for the first time

