# Gemini 静态评审（86c9019e）逐条核实

作者：lead，2026-11-19。写盘范围：本文档。被核对象是外部模型对
`86c9019e354fd28a0af6b7cf4bf4b4978e08b1dc` 的一份 1,418 行静态评审（含 P1–P11、
排序清单、静态无法定论清单）。核实基准是 `work/integrate` 的 `a9336de9`；相关文件
与 86c9019e 同源，个别行号有小幅漂移（文中给的都是当前树行号）。

## 0. 一句话结论

**约三分之二的条目可核实且成立**，其中 4 条值得立刻排期；**1 处核心引文是错的**
（边函数行号）、**1 个数字过时**（183 开关）、**1 组补丁不可直接 apply**（markdown
把代码吃掉了）、**1 个根因假说没有源码支持**（欧拉角轴序）。三处「文档与源码矛盾」
里有两处确为文档过时，第三处是仓库自己的文档自相矛盾——都成立，且都能零风险修。

## 1. 成立且可用

### 1.1 逐顶点 UV 取模（视觉最高价值）

- `native/src/world_tile_atlas.cpp:340-347` 的 `worldTileCoord` 在 CPU 端逐顶点做
  `texcoord - floor(texcoord / size) * size`，再映射进 atlas 子矩形。
- 调用点是 `native/src/native_renderer_world.cpp:810-815`（a/b/c 三个顶点各一次），
  **PS 侧没有等价 `frac`**；世界 sampler 是 `D3D11_TEXTURE_ADDRESS_WRAP`
  （`native_renderer.cpp:596-597`），atlas 有 1 px 边界复制（`world_tile_atlas.cpp:325-337`）。
- 后果：UV 跨度跨过整数平铺边界的三角形，两个顶点被折到 tile 的两端，光栅器在
  atlas 空间里反向插值 → 整面拉伸/镜像。机制成立。
- 待补的一步是运行时对照（同机位开关帧），本文档不宣称它就是截图那面砖墙的根因。
- 修复方向：把 tileRect 传给 PS、在采样前 `frac`；或上传阶段按边界切分三角形。前者改动小，
  但要动 shader 契约与像素门禁。

### 1.2 N18：POV 无 viewmodel 的两道硬门闩（行号全部核实）

- `native/src/main.cpp:3296` `commandViewModel` 默认为空 → `:8601`
  `viewModelRequested = !commandViewModel.empty()` = false → `:8985`、`:10155`、
  `:14704` 三处全部跳过；`:3314` `viewModelApplyAnimation = false` → `:15024` 的
  else 分支 → `:15041` `renderer.clearViewModelBones()` 强制 bind pose。
- 绘制端是接通的：`native/src/native_renderer_frame.cpp:2031` 调 `drawViewModel`；
  `native_renderer_viewmodel.cpp:648` 独立清深度；`entity_model.cpp:133-136` 的世界
  pass 跳过 `v_*.mdl`。
- LOD：`native/src/model_loader.cpp:1496-1508` 把 `targetLod` 锁到 LOD0，`parseVvd`
  （`:195-246`）只读 `numLod0` 顶点，没有对 LOD1+ 应用 `vvdFixups`。
- 结论：「POV 帧没有 viewmodel」是**默认关闭**，不是资产缺失；修复点就是那两个默认值。

### 1.3 P9 四条 C6262 的元凶

- 四处栈缓冲已核实：`native/src/main.cpp:2287`、`native/src/native_ui.cpp:70` 与
  `:76`、`native/src/settings_store.cpp:132`（各 `wchar_t[32768]`，64 KiB）；
  第四条 `main.cpp:3163` `wWinMain` 是累积栈帧。
- 栈转堆的方向成立；但**它给出的 diff 不能被 apply**：markdown 把 `buffer.data()`
  这类片段替换成了 `[invalid URL]()`，必须手写。

### 1.4 P10 CI 零运行

- 远端 `main`（`98ddaeba`）的树里没有 `.github/workflows/tier1.yml`
  （`git ls-tree -r origin/main` 只有 `tools/`），远端也没有 `candidate/*` 分支；
  `tier1.yml:36-42` 只监听 `main` / `candidate/*` / PR / dispatch，文件头自己写着
  `STATUS: NOT YET OBSERVED ON THE REMOTE`，N22 文档 §0 是同一结论。
- 方案 A（推 `candidate/ci-smoke` 或 `gh workflow run`）可行，但会在公开仓库产生
  Actions run，属于要用户拍板的动作。

### 1.5 P6 UiCommand / 选人

- `native/include/native_ui.h:191-210` 的 `UiCommand` 确实只有 18 项（`Open` 到
  `Export`，加 `Count`），没有 follow/选人；`native/src/settings_store.cpp:126-127`
  已有 `selectedPlayerEntity`；`native/src/spectate_target.cpp` 存在但没接进命令层。
- 追加 4 个命令、独立 `spectate:` trace 前缀（不碰旧行格式）的方向成立。

### 1.6 P3 的骨架事实（不含它的根因假说）

- 成立：`native/src/animation_decoder.cpp:240-242`（外部 anim block 拒绝）、
  `:572`（`cursor = tick * anim.fps / tickRate`）、`:613-616`（骨骼索引越界）、
  `:539-652`（`sampleAnimation` 整体）。
- **新线索（有用）**：`:503-516` 在 `blendCount > 0` 时只读第一个 `animDesc`，
  9-way / 12-way blend grid 没有实现——这解释了「run/aim 姿态参数不生效」。
- `native/src/animation_clock.cpp:113-121` 首帧 `!hasPrevious` 不推进 cycle，这是
  设计，不是缺陷。

### 1.7 P11 的 C1061 论证

- `native/src/native_renderer_world.cpp:734` 的注释确实写着「命令行不能再加开关
  （main.cpp 的链撞 MSVC C1061）」。把 CLI 扁平化提前以解除 C1061 的论证成立；
  但数字要用本节 §2.2 的实测。

### 1.8 三处「文档与源码矛盾」都成立

1. `docs/BLACKPOINT-849-919-2026-11-14.md:150` 说「缺 COLOR0 回读、最好新增
   `--world-vertex-colour`」——`native_renderer_shaders.cpp:341-342` 已有 View16
   （`colour.r/g/a + blendWeight`）与 View17（`albedo/sampled/sampledTexel.a`）；
   `:80` 的「本树无法产出 tiled 三角形的 0 顶点色」同样过时。
2. `docs/N-STATUS-2026-11-14.md:63` 与 `docs/PROJECT-NEXT-TASKS-2026-11-14.md:41`
   仍写 `sky_camera` 路径 UNSUPPORTED——`native/src/bsp_map.cpp:628-640` 已解析
   origin+scale 进 `map.skyCamera`；缺的是渲染侧 3D skybox pass（N10）。
3. `docs/N-STATUS-2026-11-14.md:17` 与 `:40` 仍写「默认仍 bind pose」，而同一文件
   `:9` 已写「默认动画开」，`native/src/main.cpp:3331` 的
   `playerAnimationEnabled` 也确实是 `true`——仓库文档自相矛盾，成立。

## 2. 需要纠正

### 2.1 边函数引文是错的

它把 CPU trace 的边函数指到 `main.cpp:8616/8625-8634`，那是 ViewModel 的
`viewModelDrawStateFor`。真实位置是 `native/src/main.cpp:10940-10974` 的
`consider` lambda：`:10943` 跳过水面、`:10950` 背面剔除、`:10945` 投影、
`:10955` 负容差 `if (w0 < -1.0e-4f || w1 < -1.0e-4f || w2 < -1.0e-4f) return;`。
「负容差选错边缘片元」这个机制仍可测，但引文不能沿用。

### 2.2 「183 开关链」是旧文档数

当前 `native/src/main.cpp`：`wcscmp(arguments[i], L"--` **191** 条、
`wcscmp(arguments[i]` 全部 **212** 条、`L"--` 字面量 **200** 个。183 已过时。

### 2.3 补丁不可直接 apply

P9 的三段 diff 里 `buffer.data()` / `root.data()` 等被替换成 `[invalid URL]()`；
P1 的 `world_color0_probe.cpp` 所依赖的字段存在（`native/include/bsp_map.h:27-29`
`aAlpha/bAlpha/cAlpha`、`:70` `faceIndex`），但探针只打印 alpha、不打印 r/g/b，
判定力并不比已有 View16 强，还要新增 CMake 目标。

### 2.4 欧拉角轴序假说没有源码支持

`native/src/animation_decoder.cpp:95-109` 的 `radianEulerToQuat` 与 Source
`AngleQuaternion` 的 Rz(yaw)Ry(pitch)Rx(roll) 逐项一致；且 2026-11-19 的
animation-drive-states 门禁已经证明 crouch/flinch 姿态被应用并改变像素。
「第二帧起错乱」需要运行时读数，不能归给轴序。

### 2.5 P2 的验收行号

它引用 `docs/PROJECT-NEXT-TASKS-2026-11-14.md:195-201` 作为 N10 验收；N10 章节
实际在 `:259`。

## 3. 静态无法定论（保持 OPEN）

- 三张截图的现象本身：评审文件里没有嵌入图片，只有文件名。
- 919,379 的 texel 82 vs draw 210、849,396 的 0 来源（entity 覆盖 vs 越界零读）。
- `METALGRATE013A2` 是否 `$translucent`。
- POV 帧是否真的一帧都没画（要跑 `viewmodel_contract_probe` 与 trace）。
- 「第二帧起错乱」的 frame 0 / frame 1 骨骼矩阵对照。

## 4. 建议顺序（对应本仓库编号）

1. **N18 viewmodel 两道门闩**：行号已核实，先跑 `viewmodel_contract_probe` 取基线，
   再把 `--viewmodel` 的自动触发与 `viewModelApplyAnimation` 默认值分开讨论。
2. **文档修正 3 处**（BLACKPOINT 的 COLOR0 回读、两处 `sky_camera` UNSUPPORTED、
   N-STATUS 的 bind pose 自相矛盾）：零风险，可在下一次文档批次里做掉。
3. **worldTileCoord 的 PS 侧 frac**：视觉收益最大，但要 A/B 像素 oracle 与门禁，
   按渲染变更走。
4. **P9 三处栈转堆**：30 行以内、过 `/W4 /WX`，可与 N24 后续 phase 同窗口。
5. **CI 首次实测**：推 `candidate/ci-smoke` 或 `gh workflow run`——需要用户同意。
6. **D01/D02 的运行时证据**：与缺陷登记一致，等柜面窗口。

**不建议先做**：P7 三类特效（工作量大、无近期验收面）；CLI 扁平化（N24 phase 1 刚
落地，phase 2 的 seam 计划里已经包含，现在大改会与拆分互相踩）。
