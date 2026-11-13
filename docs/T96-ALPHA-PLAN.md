# T96 · 世界 alpha 三断点 —— 设计 + 补丁草稿

> **状态：DRAFT / 本批未编译、未运行。**
> 本批（task-39）只做设计、补丁草稿、trace 契约与验收设计；编译、像素证据、
> 变异测试留给下一批 hostlock 锁窗。本文所有「预计耗时」都是推断，不是读数。
>
> - worktree：`D:/TF2_Native_Worktrees/t96-alpha`
> - branch：`task/t96-alpha`（从 `t91-integration` 的 `1a835d5` 起）
> - 基线只读参考：`D:/TF2_Native_Worktrees/t79-integration`（HEAD `e15577b`）
> - 起点证据：`D:/_shared/EXEC-2026-11-07/T3-ALPHA-GEOMETRY.md`（三断点原文）

---

## 1. 要修的三个断点（起点原文）

| # | 断点 | file:line（基线 t96 = 1a835d5） |
|---|------|----------------------------------|
| ① | VMT 解析器读出 `$translucent` / `$alphatest`，写进 `VmtMaterial`，但世界参数结构没有对应字段——**解析了，没人接** | `native/src/vmt_material.cpp:266-267`；`native/include/vmt_material.h:84-85`；`native/include/native_renderer.h:357-365`（WorldMaterialParams） |
| ② | 世界 PS 硬编码输出 alpha=1 | `native/src/native_renderer_shaders.cpp:335` `return float4(albedo * lighting, 1.0);` |
| ③ | 世界通路从不设混合态，世界 PS 也没有 `discard`（viewmodel PS 有） | 全仓 `OMSetBlendState` 只在 `native_renderer_viewmodel.cpp:685/:701/:723/:740/:843/:875/:895/:904` 与 HUD `native_renderer_frame.cpp:715/:1284/:1314`；viewmodel 的 discard 在 `native_renderer_shaders.cpp:801-802` |

关键既有事实（让补丁可以很薄）：

- 图集里 **alpha 数据本来就在**：`worldTileBlit` 每 texel memcpy 4 字节
  （`native/src/world_tile_atlas.cpp:262-286`，逐 texel 在 `:271`），纹理格式
  `R8G8B8A8_UNORM`（`native/src/texture2d.cpp:27`）。缺的只是消费者。
- 世界 PS 的每材质特征**已经走顶点**：`float4 feature : TEXCOORD6`
  （`native_renderer_shaders.cpp:34/:35/:110`），PS 里
  `uint featureMask = (uint)feature.x;`（`:222`），低位已被
  `$bumpmap/$bumpmap2/$detail` 用掉（bit 1/2/4）。
- 世界 pass **没有 per-material 常量缓冲**；`WorldShaderConstants`
  （`native_renderer.h:422-441` 基线）是每帧一组。所以 alpha 不能走常量，
  只能走顶点或拆 draw。

结论：不新增材质常量、不改 vertex stride，把两个键放在 `featureMask` 的
**bit 8 / bit 16**，再加一个「透明子批次 + 两个设备状态 + PS 的 discard/alpha 输出」。
没有任何新资源格式。

---

## 2. 设计

### 2.1 每材质真值走顶点位（bit 8 = `$translucent`，bit 16 = `$alphatest`）

- `WorldTexture` 新增 `translucent` / `alphaTest`（`native/include/native_renderer.h:168-176`），
  在 main.cpp 的 VMT 循环里从 `material` 直接抄（`native/src/main.cpp:5631-5636`）。
- 渲染器上传顶点时按 tile 所属材质写位：
  `native/src/native_renderer_world.cpp:691-712`（`mask |= 8/16`，计
  `worldBlendedTriangleCount_` / `worldCutoutTriangleCount_`）。
- `feature.x` 是 float：bit ≤ 2^24 都精确；本批最大 mask =
  `1+2+4+8+16 = 31`，远低于 2^24，无精度问题。
- 未在此通道的顶点保持 0：**不透明帧不受影响**。

### 2.2 按 (atlas batch, alphaMode) 拆子批次

混合态是 per-draw 的，而一个 atlas batch 会同时装不透明材质与透明材质
（plan 按预算排布，不按 alpha 分组），所以索引缓冲做二级分组：

- `worldTriangleBatch_[tri] = batch * 2 + (translucent ? 1 : 0)`
  （`native/src/native_renderer_world.cpp:748-751`）。
- 索引按 (batch, mode) 建：opaque 在前、blended 在后
  （`native_renderer_world.cpp:877-925`；顺序见 `:892-897` 注释）。
- **表必须保持 dense**：`worldDrawBatches_` 每个 (batch, mode) 都占一个槽，
  即使该子批次为空。这是本批自查时修掉的一个真实缺陷：`setWorldVisDrawList()`
  用 `worldTriangleBatch_` 的值直接索引 `worldDrawBatches_`
  （`native_renderer_world.cpp:1112-1114`），若空子批次被 compact 掉，
  常见地图（没有透明材质）里第二批之后的所有三角形都会**错位或越界被静默丢弃**。
  修复见 `native_renderer_world.cpp:921-928`（dense push + 空批次不计数）。

### 2.3 两个设备对象（一次创建，不在绘制路径懒建）

`native/src/native_renderer.cpp:575-598`：

- `worldBlendedDepthStencilState_`：复制世界 pass 的 `depthDescription`
  （less-equal、测试开），`DepthWriteMask = ZERO`——透明面被前面的不透明面遮挡，
  但不遮挡它后面的另一个透明面。
- `worldAlphaBlendState_`：与 viewmodel 同配方
  （`SrcBlend=SRC_ALPHA`、`DestBlend=INV_SRC_ALPHA`、`SrcBlendAlpha=ONE`、
  `DestBlendAlpha=INV_SRC_ALPHA`、写掩码 ALL；对照
  `native_renderer.cpp:444-458`）。
- 创建失败 `return false`，`initStage_` 分别为
  `createDepthState.worldBlended` / `createBlendState.world`。

### 2.4 绘制：状态切换与恢复

`native/src/native_renderer_frame.cpp:346-363`：

- `batch.alphaMode == 1`：`OMSetBlendState(worldAlphaBlendState_)` +
  `OMSetDepthStencilState(worldBlendedDepthStencilState_)`。
- 否则显式 `OMSetBlendState(nullptr)`——不能让上一 pass 留下的混合态把地图叠一遍。
- 循环后恢复 `OMSetBlendState(nullptr)`，若切过 mixed 再恢复
  `depthStencilState_`（`native_renderer_frame.cpp:387-390`）。
  循环内没有 `return`，所以不存在中途退出把状态漏给 water/entity pass 的路径。

### 2.5 PS：opaque / blended / cutout 三模式

`native/src/native_renderer_shaders.cpp:171-177`（采样保留 alpha）与
`:341-358`（输出）：

```hlsl
if ((featureMask & 16u) != 0u && sampledTexel.a < 0.5) discard;   // cutout
const float outputAlpha = (featureMask & 8u) != 0u ? sampledTexel.a : 1.0f;
return float4(albedo * lighting, outputAlpha);                    // blended / opaque
```

- cutout 不需要混合态，留在 opaque 子批次里，靠 `discard` 打洞。
- 未置 bit 8 的顶点输出 1.0：opaque 帧与改前逐位一致，这是最强的负控。
- 阈值 0.5 是硬编码（`$alphatestreference` 未解析，见 §8）。

### 2.6 pass 级聚合读数（不参与绘制）

`WorldMaterialParams` 新增 `translucent`/`alphaTest`
（`native_renderer.h:371-379`），在 main.cpp 材质循环里对**所有**已解析材质做 OR
（`main.cpp:5173-5175`）。它只是 trace 用的「这张图到底声明了没有」，
绘制决策永远看每顶点位；注释里已写明，避免被误读成每材质开关。

---

## 3. 改动点（逐条 file:line，新行号）

| 文件 | 位置 | 内容 |
|------|------|------|
| `native/include/native_renderer.h` | `:168-176` | `WorldTexture::translucent/alphaTest` |
| 同上 | `:371-379` | `WorldMaterialParams` 聚合字段 + 注释 |
| 同上 | `:428-439` | `WorldTileReading::translucent/alphaTest/alphaMode/alphaReason` |
| 同上 | `:592-602` | `WorldMaterialSampleState` 五个 alpha 读数 |
| 同上 | `:1905-1911` | `WorldDrawBatch::alphaMode`（0 opaque / 1 blended） |
| 同上 | `:2107-2116` | 三个设备/计数成员 |
| `native/src/native_renderer.cpp` | `:575-598` | 创建 blended 深度态与混合态 |
| `native/src/native_renderer_shaders.cpp` | `:171-177`, `:341-358` | 保留 texel alpha；discard + 输出 alpha |
| `native/src/native_renderer_world.cpp` | `:203-206` | 每次 upload 重置三个 alpha 计数 |
| 同上 | `:274-292` | 每材质消费判决（opaque/cutout/blended/refused + 原因） |
| 同上 | `:691-712` | 顶点位 + 三角计数（按顶点，不按声明） |
| 同上 | `:748-751` | 三角→子批次索引 |
| 同上 | `:877-925` | 索引按 (batch, mode) 分组；opaque 先、blended 后；dense 表 |
| `native/src/native_renderer_world_material.cpp` | `:396-402` | 采样状态填 alpha 读数 |
| `native/src/native_renderer_frame.cpp` | `:346-363`, `:387-390` | 绘制内状态切换与循环后恢复 |
| `native/src/main.cpp` | `:4867-4873` | 五个 alpha 计数器 |
| 同上 | `:5161-5170` | 声明侧 trace（每材质） |
| 同上 | `:5173-5175` | 聚合 OR |
| 同上 | `:5631-5636` | 把两键写进 `WorldTexture` |
| 同上 | `:5645-5652` | `gpu: world alpha-fetch ...` 聚合行 |
| 同上 | `:8028-8036` | 消费侧 trace（每材质） |
| 同上 | `:8046-8050` | 帧侧 trace |

补丁规模：7 文件，+258 / −25；`git diff` 存为
`D:/_shared/EXEC-2026-11-08/T96-ALPHA.patch`。

---

## 4. trace 契约（门禁 grep 的精确文本）

1. 声明侧（每个声明了任一键的 VMT，在 base-texture 检查之前打印，
   所以被后续拒绝的材质也留痕）：

   ```
   gpu: world alpha material=<name> stage=declared translucent=<0|1> alphatest=<0|1> source=<vmt path>
   ```

2. 聚合（每张图一行，`zu` 是 size_t，`%d` 是 0/1）：

   ```
   gpu: world alpha-fetch declared=<n> translucent=<n> alphatest=<n> tiledTranslucent=<n> tiledAlphatest=<n> aggregateTranslucent=<0|1> aggregateAlphatest=<0|1>
   ```

3. 消费侧（每个声明了键的材质；`mode` 是词不是标志位）：

   ```
   gpu: world alpha material=<name> stage=consumed mode=opaque|blended|cutout|refused reason=<...>
   ```

   reason 词表：`no alpha key`、`$translucent`、`$alphatest`、
   `the material declares an alpha key but got no atlas tile`、
   `the world pass has no alpha blend state`。

4. 帧侧（来自上一次 upload 的顶点计数与设备对象存在性）：

   ```
   gpu: world alpha frame blendedTriangles=<u> cutoutTriangles=<u> blendedBatches=<u> blendState=<0|1> depthWriteOff=<0|1>
   ```

判读规则：`stage=declared` 只有声明；`mode=` 才是这个 pass 实际会怎么画。
**「有 declared 行、没有对应 mode=blended/cutout 行」正是旧 bug 的形状**。
`mode=refused` 必须带能自解释的 reason，不许静默退化。

---

## 5. 正控 / 负控

> 一条通行的前提：先跑一次带 trace 的运行，grep
> `stage=declared` 找到**本机安装地图里真的有透明材质**的那张图/那个材质。
> 5 个本地 demo 里若一张都没有，正控就是 **未验证**，如实记录，不许拿
> 「代码看起来对」冒充。搜索命令见 §6.2。

### 正控（必须变）

- **P1 像素**：同一次构建的两次 capture——
  A = 补丁原样；B = 单点变异版（PS 末行改回
  `return float4(albedo * lighting, 1.0);` 并删掉 discard，其他一字不改）。
  在含 `$translucent` 材质的地图上，A 与 B 的 BMP payload 必须不同；
  用 `frame-capture-check.sh` 的逐字节 diff 方法，阈值 > 0（更稳：差异字节
  > 0.1%，对照该脚本 2026-10-13 的 0.9840 读数）。
  同一 A/B 在**无 alpha 键**的地图上必须逐位相同（这就是最强的负控 N1）。
- **P2 计数**：A 运行里 `blendedTriangles > 0` 且至少一条
  `mode=blended`；含 `$alphatest` 的材质同理 `cutoutTriangles > 0` 且
  `mode=cutout`。
- **P3 不透明不变**：无 alpha 键地图的 capture 与补丁前构建逐位相同
  （补丁前构建 = 同一 worktree 的父提交 `1a835d5`，或 B 变异版）。

### 负控（必须为零/必须拒绝）

- **N1**：无 alpha 键地图 → `blendedTriangles=0 cutoutTriangles=0`，
  没有 `stage=consumed` 行（只在声明了键的材质上打印），且像素不变。
- **N2（本题点名的那条）**：把 `$translucent` 键清掉/关掉（探针 VMT 或
  单点变异 `mask &= ~(8|16)`）后，`blendedTriangles == 0`，
  且 `mode` 不再是 blended。最省事的做法是用 B 变异版跑同一张图：
  `grep -c 'blendedTriangles=0' ` 必须命中，且 `mode=blended` 行数为 0。
- **N3**：声明了键但没拿到 tile 的材质 → `mode=refused` 且 reason 含
  `no atlas tile`（构造：把世界材质预算调小，或用一张超预算的大材质图）。
- **N4（设备态负控）**：`blendState=0` 或 `depthWriteOff=0` 时，
  任何 `mode=blended` 行都必须是 `refused`——即「材质要求了、pass 做不到」
  不许读成 blended。

### 变异测试（证明读数不是自证）

下一批（有余量时）做三条单点变异，每条要求对应读数变红：
`mask` 不置位 → `blendedTriangles=0`；PS 输出恒定 1.0 → 像素回到 B 的样子；
删掉 `discard` → cutout 像素不再打洞。

---

## 6. 验收命令与预计耗时（全部为**推断**，本批未执行）

### 6.1 构建（锁窗内，必须先 hostlock）

```bash
bash /d/TF2_Native_Worktrees/_shared/hostlock.sh status
bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run t85-av "cd /d/TF2_Native_Worktrees/t96-alpha && bash build-cmake.sh"
```

- `build-cmake.sh` 是仓库自己的可复现构建（NMake + 显式 INCLUDE/LIB，
  见脚本头注释），输出 `native/build-nmake/tf2_demo_native.exe`。
- 预计 3–6 分钟（推断；本机首次全量链接的常见量级）。构建后记下 exe 的
  sha256 与 `native/generated/tf2_build_stamp.identity` 的 buildUtc/dirty。

### 6.2 跑产品（安全红线：必须 `--audio-disabled --hidden-window`）

```bash
DEMO=D:/TF2_Demo_Player/testdata/demos/bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem
OUT=/d/_shared/EXEC-2026-11-08/t96-run1
mkdir -p "$OUT"
bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run t85-av \
  "cd /d/TF2_Native_Worktrees/t96-alpha && TF2_NATIVE_TRACE='$OUT/alpha.trace' \
   native/build-nmake/tf2_demo_native.exe \
   --tf-root 'D:/SteamLibrary/steamapps/common/Team Fortress 2/tf' \
   --demo '$DEMO' --capture-tick 0 --capture-frame '$OUT/world.bmp' \
   --audio-disabled --hidden-window"
```

- 注意：现成的 `frame-capture-check.sh:144` 用的是 `--audio-device 6`，
  **本线不允许照抄**（安全规则：绝不真实音频设备）。alpha 验收一律
  `--audio-disabled`，语义是 `waveOutOpen` 从不调用（`main.cpp:2015`、
  `:5868`）。
- `TF2_NATIVE_TRACE` 是 trace 的落盘开关（`native/src/main_trace.cpp:79-87`
  读环境变量后 fopen；没有它 traceFmt 直接 return，一行都不会有）。
- 预计每次 20–60 秒（推断），每次前 `hostlock.sh status`，每次后确认锁释放、
  `product_processes=0`。

### 6.3 断言（一次 grep 一组）

```bash
T=$OUT/alpha.trace
grep -c 'stage=declared'             "$T"
grep -c 'mode=blended'               "$T"
grep  'gpu: world alpha-fetch'       "$T"
grep  'gpu: world alpha frame'       "$T"
# N1：无 alpha 键地图必须都是 0
grep -q 'gpu: world alpha frame blendedTriangles=0 cutoutTriangles=0' "$T"
```

### 6.4 最小验证集（回信给 lead 的那三行）

1. `bash build-cmake.sh`（1 条，3–6 分钟）。
2. 跑 1 张含声明材质的地图 + 1 张不含量（2 条，各 20–60 秒），
   trace + BMP 都落盘。
3. 4 条 grep + BMP 字节 diff（1 条脚本，<5 秒）。
   **合计约 6–8 分钟**；若做 A/B 变异版再加 1 次构建 + 2 次运行（约 +5 分钟）。

---

## 7. 不变量（验收时优先于任何读数）

1. `--audio-disabled`：`waveOutOpen` 调用次数必须为 0。trace 里有现成读数：
   `mode: summary ... audio_device_opened=<n> audio_device_open_attempts=<n> audio_device_opens_ok=<n>`
   （`native/src/main.cpp:12076-12079`），三者都必须为 0。
2. 无 alpha 键的地图：帧逐位不变（BMP payload sha256 相同）。
3. 有 alpha 键的地图：`blendedTriangles + cutoutTriangles > 0` 且像素变了。
4. `declared > 0` 的每个材质必须有一条 `stage=consumed` 行（不静默）。
5. `mode=blended` 的行数 > 0 时，帧侧 `blendState=1 depthWriteOff=1`。

---

## 8. 风险与已知限制（写清楚，不藏）

1. **混合顺序**：本批只保证「所有 opaque 先于所有 blended」，**不做逐三角
   back-to-front 排序**（`native_renderer_world.cpp:892-897` 注释已声明）。
   两层透明材质重叠时，结果会依赖三角形在索引缓冲里的原始顺序，与 TF2 的
   `$translucent` 排序不同。这是本批的已知限制，不是回归。
2. **深度写关闭**：blended 批次 `DepthWriteMask=ZERO`，所以两个透明面之间
   不会互相遮挡；opaque 先画保证正确的前后遮挡。若某个地图把透明面画在
   opaque 面之前（本补丁不会），会看到错误的合成。
3. **cutout 阈值**：硬编码 `a < 0.5`，`$alphatestreference` 未解析；
   与 TF2 的 cutout 边界可能差一点，但打洞这件事本身成立。
4. **单纹理路径不支持**：`worldDrawBatches_` 为空的单纹理上传路径
   （`native_renderer_frame.cpp:394-411`）没有顶点位/子批次，alpha 键在那里
   不会生效；读数会显示 `mode=refused`（无 atlas tile）或 `opaque`，
   不会假装支持。
5. **dense 子批次表**：它是 PVS 重组的索引正确性前提
   （`native_renderer_world.cpp:1112-1114`）。后续若有人把 `worldDrawBatches_`
   改回 compact，会静默丢/错配三角形；注释已写在 `:921-928`。
6. **shader 字面量上限**：世界 PS 两段相邻字面量基线 8,050 / 9,860 字符，
   本批加约 1 KB；MSVC 单字面量上限 ~16,384（C2026），仍有余量。编译时若
   触发 C2026，需拆成两个字面量拼接（PS 本来就是多段拼接结构）。
7. **状态泄漏**：混合态/深度态在循环后恢复；world 之后是 water/entity pass，
   验收时看 `mode=blended` 的帧是否把 sky/water 也叠了（P1 的 A/B 会暴露）。
8. **性能**：draw call 最多翻倍（每个 atlas batch 最多 2 个），实际只多出
   有透明材质的 batch；顶点/索引内存不变（索引总量不变，只是分组）。
9. **未验证项**（本批结束时）：
   - 编译（未编译，可能有语法/类型错误；已做人工自查：变量作用域、
     `%zu/%u` 格式与实参类型、`featureMask` 作用域、dense 表索引）。
   - 像素/读数（未跑产品）。
   - 本机 5 个 demo 是否真的有声明 `$translucent` 的材质（未知，§5 前提）。
   - `WorldTexture` 的 alpha 键是否只来自 VMT 而非 patch 包装后的 include
     （解析器现状，T59 已处理 patch；本批未新增行为）。

---

## 9. 下一批（锁窗）交接清单

1. 构建 + 修编译错误（预计 1–2 轮）。
2. 跑 §6 的命令，拿到 4 组 trace 与 2 张 BMP；按 §7 断言。
3. 找一张真有 `$translucent` / `$alphatest` 的地图；找不到就把正控标为
   **未验证**，不得用「代码看起来对」替代。
4. 有余量再做 §5 的三条变异（各需一次重建）。
5. 与 T3 的断点编号 ①②③ 对齐；**不新造 8.x-y 编号**，缺口登记留给
   T85-DEBT-LIST 的维护者（T3 报告已给出三处独立断点）。
6. 结论回填 `D:/_shared/EXEC-2026-11-08/`（本目录），并 update task-39。

---

*本文件是草稿：补丁尚未提交编译，所有「预计」都是推断；任何一个数字在
下一批实测出来之前都不能当作结论。*
