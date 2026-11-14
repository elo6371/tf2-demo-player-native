# D08 小地图七图帧与投影验收 - 2026-11-14

- 任务：路线图 F2 "五图固定相机帧" + D08（七图小地图视觉验收）。
- 基线：`candidate/e5`，HEAD `a153030`（含本轮 `fix(hud): keep the minimap drawn while the clock is stopped`）。
- 二进制：`native/build-nmake/tf2_demo_native.exe`，SHA-256 `cfcdbe5d48c6e71e5853b26b89da1578d521146447338a379a25bb9ad16ae2f3`，2,988,544 B；
  sidecar `sourceTree=a15303062d5e`、`dirty=1`（脏文件只有 `docs/PROJECT-ROADMAP-HANDOFF-2026-11-12.md` 与未跟踪的 `docs/PROJECT-HANDOFF-2026-11-14.md`，**产品源码干净**）。
- 七图 BSP 全部可解析：process/gullywash/sultry/product/bagel 在 `tf/download/maps/`，sunshine/upward 在 `tf/maps/`（哈希见 `docs/BATCH-G1-2026-11-14.md` §1.1）。

## 1 结果

`MINIMAP-FIVE checks=205 failed=0`（七图，全部 `inside=25600 outside=0`）

| 地图 | 结论 | 检查数 | 朝向对数 | 目标点 | 帧 SHA-256 前缀 |
|---|---|---:|---:|---:|---|
| cp_process_f12 | PASS | 39 | 20 | 5 | `6080c191fe5f1fb9` |
| cp_gullywash_f9 | PASS | 39 | 20 | 5 | `0eacda20fcdd88bd` |
| cp_sultry_b8a | PASS | 39 | 20 | 5 | `99aebe29004f6261` |
| koth_product_final | PASS | 7 | 0 | 1 | `3292f8c66dc58343` |
| koth_bagel_rc11 | PASS | 7 | 0 | 1 | `3540a80719fbc1a1` |
| pl_upward_f12 | PASS | 28 | 12 | 4 | `ad749888ec4a7db5` |
| cp_sunshine | PASS | 39 | 20 | 5 | `4a107ad34c97461b` |

证据：`D:/_shared/EXEC-2026-11-14/G1/minimap-five/`（`check.log` sha256 `d478c8e1…`、`<map>.bmp`、`<map>.off.bmp`、`<map>.trace`、`run.log`）。

每个 PASS 覆盖四件事：

1. **面板就是那块矩形**：`--hud-minimap` 与同参数无开关的两帧相比，面板内 25600/25600 像素变化、面板外 0；变化像素的实测 BBOX 等于规格矩形 `(W-172, 12, 160, 160)`。
2. **每个目标点都画了环**：控制点投影位置 5×5 窗口 ≥20 像素是对应队色（红/蓝/中立），且点不在面板边缘 2 px 内。
3. **朝向没翻**：逐对比较面板坐标序与世界坐标序——x 同号、y 反号，共 20 对（Upward 12 对，两张 koth 各 1 个点无对可比）。
4. **暂停帧不再丢面板**：每图 trace 里所有 `minimap: hud state=` 行都是 `reason=ok`，没有 `no-camera-pose`（见 §2）。

## 2 本轮发现并修复的产品缺陷：暂停后小地图消失

- **缺陷**：`--start-paused`（或用户按暂停）后，第 2 帧起小地图面板不再绘制。第 1 帧发布 `state=ready shapes=14`，第 2 帧发布 `reason=no-camera-pose shapes=0`，抓到的帧与「不加开关」的对照**逐字节相同**。
- **复现**：`tf2_demo_native.exe --tf-root <TF> --demo <cp_process_f12 STV> --start-paused --hud-minimap --capture-frame <bmp>`，不加 `--debug-camera`。trace 里能看到上面两行。
- **根因**：`minimapFrame.valid = cameraTraceReady`（`native/src/main.cpp`），而 `cameraTraceReady` 只在「本帧有新相机读数」时为真；暂停的 tick 不重算相机，于是面板拿到「无效」。
- **修复**（`a153030`）：面板记住**同一张图**最后一次拿到的位姿，在无读数的帧上复用；换图则仍不画。复用的是显式跟踪的状态，不是 `cameraTrace*` 局部变量，所以原来「不读未初始化位姿」的约束仍然成立。
- **正控**：同图同相机加 `--debug-camera` 时每帧都有读数，修复前后都画（门禁 A2 的 25600 像素即此路径）。
- **负控**：修复前的行为由当时的 trace 直接记录（`loop n=1 state=ready shapes=14` → `loop n=2 reason=no-camera-pose shapes=0`，抓帧与对照逐字节相同），已写进提交 `a153030` 的说明。
  **本轮没有**再拿父提交的二进制重建做第二次负控（要另开构建目录，与门禁写窗口冲突），这一点登记为 D36，不当作已完成。
- **影响面**：用户暂停即丢面板；所有 `--start-paused` 抓帧都拿不到小地图。修复前，本任务的五图抓帧**无法完成**——这也是该缺陷此前没被发现的原因：既有门禁全部带 `--debug-camera`。

## 3 本轮发现的文档缺陷：面板里没有 BSP 线稿

- 事实：面板只由「背景 + 4 条边框 + 控制点环 + 观察者点 + 3 个航向点」组成，`main_hud.cpp` 的形状推送里没有任何地图几何；形状计数与之一致（Upward 13、五图 14、单点图 10、边缘裁剪的 Gullywash 11）。
- 因此 `docs/PHASE-F-REPORT-2026-11-13.md:233`、`docs/PROJECT-HANDOFF-2026-11-14.md:334`、`docs/PROJECT-ROADMAP-HANDOFF-2026-11-12.md:784` 说的「BSP-derived linework / 第一版是 BSP 派生线稿」**在本构建里不成立**：没有线稿，只有标记点。
- 处置：不改产品（线稿属未实现功能，不是回归）；登记为缺陷台账 D33，并要求文档改口径。面板视觉（`panel-cp_process_f12.bmp`）可直接看出：深色底 + 5 个目标点 + 绿色观察者/航向点，无地图轮廓。

## 4 未覆盖（保持 NOT VERIFIED / UNSUPPORTED）

| 项 | 状态 | 原因 |
|---|---|---|
| resize / DPI | NOT VERIFIED | 本轮全部 1280×720 单尺寸；`hudMinimapPanelRect` 已按客户区算，但未在 2 个 DPI/尺寸下抓帧 |
| 点击/缩放 | NOT VERIFIED | 产品小地图无点击交互；`minimap-check.sh` 只覆盖纯逻辑投影 |
| 楼层选择 / 地图几何 | UNSUPPORTED | 没有线稿实现（§3） |
| 载荷车图标 | UNSUPPORTED | 面板只有 `team_control_point` 行；Upward 载荷车不是控制点 |
| 缩放比例（scale） | 部分 | 用的是产品声明的可玩区矩形（`minimap.h`）；未用 BSP 世界包围盒独立验证——两者本来就不同（Upward 声明 ±3072/±4096，世界包围盒 -3328..9344） |
| 两张 koth 的朝向 | PASS 但弱 | 单控制点无对可比，朝向只由「点在面板内的位置 + 无翻转」间接支持 |

## 5 修复后的门禁回归（同一二进制）

产品改动落在 `main.cpp` 的小地图推送路径上，所以受影响门禁是 `minimap-hud-check.sh`，必须整段重跑：

```text
MINIMAP-HUD=PASS assertions=104 skipped_controls=0
m1-yflip   red=3 expected=1  -> "cap_1 is drawn at the flipped position"
m2-heading red=5 expected=1  -> "a heading dot 4.0px ahead"
m3-anchor  red=4 expected=1  -> "the changed pixels start at the panel's left edge"
the restored build reproduces the forward exe (cfcdbe5d…)
```

- 命令：`bash .scratch/lockrun.sh g1-a12b -- bash minimap-hud-check.sh --mutation --old-exe=D:/_shared/EXEC-2026-11-13/R0/bin/tf2_demo_native.exe`
- 证据：`D:/_shared/EXEC-2026-11-14/G1/minimap-a12-post.log`，sha256 `79c39feafb190e92ce12b27f89103bd73aada8b6b597cfd97f64feb7f273c353`
- 结论：暂停位姿复用没有改变门禁覆盖的任何一条路径（门禁全程带 `--debug-camera`，那里每帧都有读数）；三个变异仍在各自预期的断言上变红；还原构建逐字节回到 `cfcdbe5d`。

## 6 身份与后续

- 本批证据全部绑定 `cfcdbe5d…`。该 exe 链接自 `a153030` 的产品源码；其后的收尾提交只动 `docs/`，可用
  `git diff --stat a153030..HEAD -- native/src native/include native/CMakeLists.txt`（预期为空）复核。
  sidecar 在收尾时用 `bash build-target.sh tf2_identity` 刷新到最终 HEAD，`dirty=1` 只来自两份用户文档；`native/src`、`native/include`、CMake 全干净。
- 绑定旧 exe `00ec8b99` 的小地图证据到此作废，不得与新证据混用。
- 下一步（UI owner）：resize/DPI 两档抓帧；若要线稿，先写 `sky_camera`/floor 选择的设计与像素 oracle，再动 `main_hud.cpp`（与 H2 的世界渲染同属单写者窗口）。

## 7 附录：命令与完成记录

```text
# 抓帧（host lock 内，14 次产品运行 = 7 图 × 开关/对照）
bash .scratch/lockrun.sh g1-minimap5 -- bash .scratch/g1-minimap-five/run.sh
# 判定（纯离线读帧）
python .scratch/g1-minimap-five/check.py D:/_shared/EXEC-2026-11-14/G1/minimap-five
# 暂停缺陷的正控/负控
bash .scratch/lockrun.sh g1-paused -- bash .scratch/g1-minimap-five/paused-panel.sh
```

相机：每图固定 `--debug-camera <cp0.x+1200> <cp0.y-1200> <cp0.z+300> 0 0`，避免观察者点压在控制点环上（首轮把相机放在控制点上，5×5 窗口读到 16 环 + 9 观察者，属于采样污染而不是产品缺陷；已改用偏移相机重拍）。

```text
Task ID / owner: G1-6 / UI + playback owner
Source branch + HEAD before/after: candidate/e5 94e7612 -> a153030
Files changed: native/src/main.cpp（暂停位姿复用）；新增 docs/D08-MINIMAP-SEVEN-MAPS-2026-11-14.md
Requirement or defect: D08 五图无帧 + 暂停丢面板
Minimal reproduction + valid control: 见 §2
Build/test/gate commands and exit codes: run.sh rc=0；check.py rc=0（205/205）
Assertion/mutation totals: 205 checks 0 failed；每图 inside=25600 outside=0
Evidence path + SHA-256: D:/_shared/EXEC-2026-11-14/G1/minimap-five/check.log d478c8e13742e30caeaaa7089a1669d44ab89d102fb902872bd8f4e3e6dcaa4c
Executable/source identity: exe cfcdbe5d… / 2,988,544 B；sourceTree a15303062d5e；产品源码干净
Result: PASS（七图投影/目标点/朝向）；resize/DPI 与线稿仍 NOT VERIFIED/UNSUPPORTED
What this result proves: 七图上面板位置、投影朝向、控制点落位与该图 BSP 实体一致；暂停帧保留面板
What this result does not prove: 缩放比例的世界正确性（声明矩形未独立验证）；resize/DPI；没有地图几何可看
Evidence invalidated by this change: 全部绑定 00ec8b99 的小地图证据（改为 cfcdbe5d）；A12 已在 cfcdbe5d 上重跑，见 §5
Remaining risks / follow-up owner: 线稿/楼层未实现（D33）；resize/DPI（UI owner）
Lock and product-process state at completion: 每轮 VERIFY-LOCK=RELEASED，产品进程 0
```
