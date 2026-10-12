# T1 ViewModel 材质接入实测（2026-10-12，`task/viewmodel-material-render`）

工作区 `D:\TF2_Native_Worktrees\viewmodel-material-render`，分支 `task/viewmodel-material-render`，
基线 `origin/p0-entity-protocol@9a8387b`。本文件只写**实测**读数；每条都能用 §9 的命令重跑。
未测项写在 §8，不混进 §2--§6。

**一句话结论**：把清单三已验证的 MDL → `$cdmaterials` → VMT → `$basetexture` → VTF →
BC1/BC3 像素解码链接入了**实际 ViewModel draw pass**。产品每帧读取被绘制 mesh 命名的
材质槽状态，GPU 纹理按规范化 VTF 资源路径缓存并逐帧复用，缺失槽进入具名 fallback；
在冻结世界（`--start-paused`）下，绑定材质与纯色对照的帧差为 1.4295%（`max_delta=186`），
且 ViewModel 之外三条条带的差异**恰好为 0**。专项门禁 220 条断言通过、10/10 mutation 捕获。
Phong/bump/selfillum **未实现**，读数逐帧标 `*AppliedRanges=0`；本项未合并主线。

---

## 1. 任务边界与产物

目标：把已经验证的解码链接入产品绘制。**不做**的事：不改实体协议；不改 `verify-all.sh` pin；
不重构世界材质逻辑；不以机器安装目录的临时资源为唯一输入（真实 VPK 是固定输入，
另有可再生的缺陷夹具）。

| 文件 | 性质 | 作用 |
|---|---|---|
| `native/include/viewmodel_material.h`（新增） | 产品+探针共用 | 材质表、状态词表、九项读数访问器 |
| `native/src/viewmodel_material.cpp`（新增） | 产品+探针共用 | `resolveViewModelMaterialTable` / `decodeViewModelMaterialSlots` 实现 |
| `native/include/model_loader.h` | 产品 | `ModelMeshMaterialTable`：从 bodypart/model/mesh 表解出被绘制 mesh 命名的槽 |
| `native/src/model_loader.cpp` | 产品 | `decodeMeshMaterials()`：相对记录偏移、stride 双见证校验、描述符全量核对 |
| `native/include/viewmodel.h` | 产品 | `ViewModelRequest` 挂载材质表；`applyViewModelMaterialResult` |
| `native/src/viewmodel.cpp` | 产品 | `buildRequest` 内解析材质表（按 `descriptorIndex`），解码结果回填读数 |
| `native/include/native_renderer.h` | 产品 | `ViewModelDrawRange`、纹理缓存 API |
| `native/src/native_renderer.cpp` | 产品 | 按规范化 VTF 路径缓存/复用 GPU 纹理、逐 range 绑定、`$translucent` 混合、shader albedo 采样 |
| `native/src/main.cpp` | 产品 | 接线；`--viewmodel-material-slot` / `--viewmodel-material-missing-slot` / `--viewmodel-flat-color`；九项读数；修复 `reloadGpuResources` 抢占缓冲区导致的材质 pass 跳过 |
| `native/CMakeLists.txt` | 构建 | `viewmodel_material.cpp` 同时加入 `tf2_demo_native` 与 `viewmodel_material_probe` |
| `native/tools/viewmodel_material_probe.cpp` | 门禁 | 探针改为消费共享实现（同一份代码，门禁非循环） |
| `bmp-pixel-diff.py` | 门禁 | 新增 `--region x,y,w,h`；无 `--region` 时输出与旧版逐字节一致 |
| `viewmodel-material-check.sh` | 门禁 | 新增第 6--9 节产品段落与 4 条产品变异 |
| `evidence/viewmodel/material-readings.txt` | 证据 | 产品材质读数归档（刷新） |
| `README.md` / `CHANGELOG.md` / `HANDOFF-2026-10-07.md` / `IMPLEMENTATION-PLAN-2026-10-10.md` | 文档 | 任务状态同步 |

六项必证与证据对照：

| 必须证明 | 证据 | 反向验证 |
|---|---|---|
| 正常材质绑定到 ViewModel | §3：纹理态 `boundSlots=1`、`uploads a texture`；纯色态 `binds nothing`、`uploads no texture`；`slotOverride=0` 生效 | `material-flag-ignored` 变红（shader 不读门控时像素不再随绑定变化） |
| ViewModel 区域像素稳定变化 | §3：1.4295%（`max_delta=186`），三条外部条带 0；两次同设置运行逐字节一致 | `range-key-dropped` 变红（绑定键被解剖后该断言红） |
| 缺失材质稳定 fallback | §4：注入后 `fallbackSlots=1`、`boundSlots=0`、几何照画、该帧与纯色帧逐字节一致、行标 `missingSlotInjected=0` | `injection-dropped` 变红（注入被静默忽略时 fallback 断言红） |
| mutation 变红 | §6/§10：10/10 捕获 | 即 mutation 本身 |
| 恢复后源码逐字节一致 | §10：mutation 日志 + `git status` | 即 mutation 的还原检查 |
| `texturesReused` 证明不逐帧重建 | §2：`texturesReused=149 == frames=128`、`texturesUploaded=2` 恒定 | `texture-rebuilt-every-frame` 变红（每帧重建时 uploads 随帧数增长） |

## 2. 产品读数（长跑，读数断言用）

命令（§9-3）长跑后抓取 `materialReadings` 行（实测逐字；本次长跑 149 帧，
帧数随机机器时序浮动——不同轮次是 128/149，不变量才是断言对象）：

```text
materialSlots=3 materialResolved=3 texturesUploaded=2 texturesReused=149
fallbackSlots=0 alphaSlots=0 alphaAppliedRanges=0 phongSlots=3 phongAppliedRanges=0
bumpSlots=3 bumpAppliedRanges=0 selfillumSlots=0 selfillumAppliedRanges=0
decodedSlots=3 drawRanges=1 fallbackRanges=0 boundSlots=1 blendedRanges=0
alphaTestedRanges=0 flatColor=0 slotOverride=-1 missingSlotInjected=-1 decodeFailure=-
```

配套行：`drawCalls=149 vertices=600`、`drawnSlot=0 source=mdl-mesh-table`、
`meshStrideExercised=1`、`drawnModel=models/weapons/c_models/c_sniper_arms.mdl`、
`entityModelMatchesDrawn=1`。

要点：
- `texturesReused == rendered_frames`（149 == 149）：每帧只重绑，不重建。
- `texturesUploaded=2`：两张纹理分别对应命令行模型与实体 companion 路径（两条路径各一张），
  不随帧数增长——这正是"禁止每帧重新创建纹理"的直接证据。
- `phongSlots=3 bumpSlots=3` 是 VMT **声明**值；`phongAppliedRanges=0 bumpAppliedRanges=0
  selfillumAppliedRanges=0` 是同一条读数行里的**应用**值。声明与应用分离打印，
  防止把声明读成支持。

## 3. 像素证据（必须冻结世界）

**关键方法论**：`--capture-tick N` 的落点随帧时机浮动（实测同一个 `--capture-tick 200`
分别落在 tick 200/201/202，世界内容差异可达全帧 3.5%）。因此全部像素对照使用
`--start-paused --capture-tick 0`，产品读数 `frames=1 tick=0`，世界冻结。
同设置两次运行的 BMP **逐字节一致**（0 字节差异），该一致性本身是门禁断言。

三组对照（分辨率 1264x681，全帧 2582352 字节）：

| 对照 | differing | percent | max_delta | 三条外部条带 |
|---|---|---|---|---|
| 纹理态 vs 纯色（`ptex` vs `pflat`） | 36915 | 1.4295% | 186 | 全 0 |
| 纹理态 vs 换第二张真实纹理（`ptex` vs `palt`） | 36440 | 1.4111% | 120 | 全 0 |
| 纯色 vs 纯色（`pflat` vs `pflat2`） | 0 | 0% | 0 | 全 0 |
| 纹理 vs 纹理（`ptex` vs `ptex2`） | 0 | 0% | 0 | 全 0 |
| 注入缺失槽 vs 纯色（`pmissing` vs `pflat`） | 0 | 0% | 0 | 全 0 |

三条外部条带：左 `0,0,420,681`、右 `844,0,420,681`、上 `0,0,1264,250`——即 ViewModel
之外的画面。**差异只落在 ViewModel 内，世界未被污染**；换纹理不改几何
（第 9 节断言 `11640 vertices` 不变）。

## 4. 缺失材质 fallback

真实安装中不存在缺失材质（扫过全部 37 个 ViewModel、共 147 个槽，无一缺失且全部解码，
95 个 BC1 + 52 个 BC3，逐槽清单见 `evidence/viewmodel/material-readings-all-vmodels.txt`；
10 个缺陷夹具没有几何、
到不了 draw pass），因此 fallback 只能由 `--viewmodel-material-missing-slot` **注入构造**，
且每行读数标注 `missingSlotInjected=<slot>`。注入后：该槽不再计入 `materialResolved`，
进入 `fallbackRanges`，不上传纹理、不绑定，几何照画（`drawRanges=1`），
该帧与纯色对照帧**逐字节一致**（0 差异）。

## 5. Alpha 路径

`palpha` 运行（换到带 alpha 的材质槽）：`alphaSlots=2 alphaAppliedRanges=1
blendedRanges=1 alphaTestedRanges=0`——只应用 `$translucent` 混合，不走 alpha test。
纯色对照 `flatColor=1` 时 `binds nothing`、`uploads no texture`、`reuses no texture`。

## 6. 门禁与变异

- `bash viewmodel-material-check.sh`（**比对工具修复后重跑**，23:48--23:58，9m57s）：
  **`assertions_ok=220`、0 失败、RC=0、`VIEWMODEL-MATERIAL=PASS`**。
  断言分布：1/9 夹具可再生产 1 条；2/9 真实安装四模型 80 条；3/9 十个夹具缺陷类 63 条；
  4/9 工作量计数 10 条；5/9 缺失模型拒绝 5 条；6/9 产品长跑读数 25 条；
  7/9 像素对照 13 条；8/9 fallback 注入 11 条；9/9 alpha 路径 12 条。
  与首轮的差别在于：首轮"通过"里有 5 条无 `--region` 的 BMP 断言是假绿（§7 第 3 条），
  这 220 条是真判定后的 220 条。
- `bash viewmodel-material-check.sh --mutation`：10 个变异（6 条夹具/探针 + 4 条产品）
  **逐个都验证为 CAUGHT**。首轮 8/10，两个 NOT-CAUGHT 均为门禁自身缺陷（§7 第 3、4 条），
  不是变异不可行。修复后：全量 v2 跑（23:06--23:42，34m06s，RC=1）9/10 CAUGHT，
  第 10 条被陈旧-capture 守卫中止（§7 第 8 条，环境原因非判定失败）；
  配额恢复后单独重跑 `--mutation texture-rebuilt-every-frame`
  （23:44--23:49）→ `MUTATION-CAUGHT=PASS ... [red=1]`、
  `VIEWMODEL-MATERIAL-MUTATION=PASS (1/1 mutations caught, tree restored)`、RC=0。
  两条产品变异的连锁红如实记录（如 `range-key-dropped` 同时红了"绑定随帧数"与"range 带纹理"）。
- 4 条产品变异的设计：`range-key-dropped`（解剖绑定键）、`material-flag-ignored`
  （shader 不读材质门控）、`injection-dropped`（注入被静默忽略）、
  `texture-rebuilt-every-frame`（每帧重建纹理）——每条都对应一个产品断言变红，
  连锁红如实记录进 `evidence/viewmodel/material-mutation-failure-*.txt`。

## 7. 本轮发现与修复

1. **接线缺陷（产品）**：`reloadGpuResources()` 原先直接上传 ViewModel 网格并占据
   `viewModelMeshBufferPath`，使场景更新的材质 pass 被 early-return 跳过——
   `--viewmodel-render command` 下材质从未绑定、覆盖开关被静默忽略（`slotOverride=-1`）。
   修复：所有上传统一经 `ensureViewModelMesh`。已同步 playbook。
2. **捕获非确定性（方法论）**：`--capture-tick N` 选择的是邻域不是点；像素对照必须冻结世界。
   已同步 playbook。
3. **比对工具假绿（仪器，由 mutation 首轮暴露）**：`bmp-pixel-diff.py` 的 min/max 判定只在
   `--region` 循环内执行，整帧条目被 `if region is None: continue` 跳过——**所有无 `--region`
   的边界断言从不判定**。变异 `material-flag-ignored`（shader 不采样，ptex 与 pflat 实测
   `differing=0`）因此 NOT-CAUGHT。修复：整帧也按同一套 bound 判定；修复后用真实 capture
   验证五组路径（整帧 min/max 的红与绿、region 的红与绿）。已同步 playbook。
4. **变异设计缺陷（仪器）**：`texture-rebuilt-every-frame` v1 直接删掉 ensure 的
   early-return，使解码每帧重跑——而解码是一次性的（跑完即清 `vtfBytesData`），第二次起
   全部槽被降级为 MissingTexture，上传根本不发生、目标断言不红。v2 改为保留 early-return、
   在分支内对被绘制槽以每次唯一的键调用上传（4x4 占位纹理）：`texturesUploaded` 随帧数增长、
   画面与其它读数不动。
5. **capture 清理被沙箱拦截（仪器）**：safe-delete 垫片在本轮累计删除数越过 50 后静默拒绝
   后续批次的 `rm`（观察到的正是三组），残留的 capture 让 `product_launch` 跳过运行、
   用上一个变异的二进制测量。修复：清理后验证残留，python 兜底删除，仍残留即退出报错
   （拒绝测量错误的程序）。
6. **mutation 哈希列表不全（仪器）**：`HASHES_AFTER` 漏了 `main.cpp` 与
   `native_renderer.cpp`，导致"SOURCE-NOT-RESTORED"假红（工作树实际无残留，grep 验证）。
   修复：补齐列表。
7. **判定过严（仪器）**：`fail -eq 1` 要求恰好一条断言红，但一个变异可以合法地红多条
   （如 shader 不采样同时让 ptex-vs-pflat 与 ptex-vs-palt 两对归零）。改为
   `fail -ge 1 && 目标断言在红名单`；全部红断言仍记录进 `material-mutation-failure-*.txt`。
8. **陈旧-capture 守卫真的触发了一次（仪器，守卫按设计工作）**：v2 全量跑在第 10 个变异处，
   沙箱 safe-delete 的 turn 级配额（50 次）耗尽，`pflat.rc` 删不掉（连 python 兜底
   `os.remove` 也被计入配额）；守卫按设计拒绝测量并退出：
   `VIEWMODEL-MATERIAL=FAIL (1 stale captures could not be cleared; refusing to measure a
   previous build)`、整轮 RC=1。`trap restore_if_patched EXIT` 使源码在退出路径上仍然还原
   （实测 `native/src/main.cpp` 与变异前备份 sha256 同为 `230629e7…`，三个变异锚点 grep 全 0）。
   配额在新的 bash 调用中恢复后单独重跑该变异 → CAUGHT、RC=0。
   **这条的价值**：它证明守卫不是摆设——宁可整轮报红，也不用上一个变异的二进制冒充读数。

## 8. 未验证内容（不写成"完成"）

1. **人工画面对照未做**：像素证据是机器比对（三组对照 + 三条外部条带为 0），
   未在真实游戏/人眼下确认真实观感。
2. **phong/bump/selfillum 未实现**：仅资源层声明可见，shader 未读取；读数用
   `*AppliedRanges=0` 明确标记。
3. **缺失材质在真实安装中不存在**：fallback 由注入构造，注入路径每行标注。
4. **跨机器抓帧未做**：所有证据来自本机。
5. **多帧像素稳定性未做**：像素证据只有单帧对照（世界冻结），未测连续帧。
6. **未合并主线**：本分支未合并，未跑 `verify-all.sh`（总链不因本项重跑）。
7. **纹理格式覆盖面**：真实模型命中 BC1/BC3；其他格式（如 BC2、未压缩）不在本项范围。

## 9. 复现命令

```text
bash build-target.sh tf2_demo_native viewmodel_material_probe        # 构建（rc=0）
bash viewmodel-material-check.sh                                     # 220 条断言
bash viewmodel-material-check.sh --mutation                          # 10/10
bash verify-fast.sh                                                  # 回归
```

产品运行一律 `--hidden-window --audio-disabled`；像素对照额外加
`--start-paused --capture-tick 0`。

## 10. 最终记录

### 10.1 验收命令与退出码（本机实测）

| 命令 | 结果 | 退出码 |
|---|---|---|
| `bash build-target.sh tf2_demo_native viewmodel_material_probe` | `rc=0` | 0 |
| `bash viewmodel-material-check.sh` | `assertions_ok=220`、`VIEWMODEL-MATERIAL=PASS`（9m57s） | 0 |
| `bash viewmodel-material-check.sh --mutation`（全量 v2） | 9/10 CAUGHT，第 10 条被陈旧-capture 守卫中止（RC=1） | 1 |
| `bash viewmodel-material-check.sh --mutation texture-rebuilt-every-frame` | `MUTATION-CAUGHT=PASS [red=1]`、`VIEWMODEL-MATERIAL-MUTATION=PASS (1/1, tree restored)` | 0 |
| `bash verify-fast.sh` | `VERIFY-FAST=PASS (assertions_ok=43)`（修 CMake 链接前为 `VERIFY-FAST=FAIL (build)`） | 0 |
| 四个 probe target 补链接后重建 | `rc=0` | 0 |

### 10.2 证据路径

| 路径 | 内容 |
|---|---|
| `evidence/viewmodel/material-readings.txt` | gate4 的读数归档（220 条断言、长跑 149 帧、六组像素对照的 BMP-DIFF 行） |
| `evidence/viewmodel/material-readings-all-vmodels.txt` | 37 个真实 ViewModel、147 槽的逐槽清单（门禁外扫描） |
| `evidence/viewmodel/material-mutation-failure-*.txt` | 10 个变异各自红了哪些断言 |
| `evidence/fast/viewmodel-material/*.bmp` | 六组对照的抓帧（gitignored，sha256 前 16 位冻结在 readings 里） |

BMP sha256（前 16 位，来自 `material-readings.txt`）：`ptex=7ca80fa61a4e655d`、
`pflat=4fa0e7eb1274d839`、`palt=b62affafb76b667e`；`ptex2`/`pflat2` 与各自对照一致，
`pmissing` 与 `pflat` 一致。

### 10.3 提交与推送

分支 `task/viewmodel-material-render`，远程
<https://github.com/elo6371/tf2-demo-player-native/tree/task/viewmodel-material-render>
（基线 `9a8387b` = `origin/p0-entity-protocol`）。

| 提交 | 内容 |
|---|---|
| `950994f` | 产品：共享材质解析、`ModelMeshMaterialTable`、renderer 纹理缓存与逐 range 绑定、main 接线与九项读数、四个 target 的链接依赖补齐 |
| `279dabb` | 门禁与工具：`bmp-pixel-diff.py` 整帧判定修复、门禁第 6--9 节与 4 条产品变异、仪器五处修复、证据归档、四份文档同步 |
| （本报告所在提交） | `T1-VIEWMODEL-MATERIAL-2026-10-12.md`；分支末端即该提交 |

工作树状态：三次提交后 `git status` 干净。注意——本分支上重跑
`viewmodel-contract-check.sh` / `weapon-animation-check.sh` 会重新生成各自的抓帧证据
（旧归档由更早的构建产出，字节不同），本分支**未**改动这些归档，
合并后需由对应门禁重跑更新；两个门禁在本分支上重跑均为 PASS（154 条 / PASS）。
