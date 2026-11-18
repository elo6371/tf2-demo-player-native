# T21 — World material coverage and visual correctness

2026-10-19 · owner: render specialist · branch `task/t21-material-visual`

## 1. 结论

T19 把 `pl_odyssey` 上 85 个材质全部送进了世界图集（`capped=0`），但**图集里的
uv 归一化用的是 tile 尺寸，不是源尺寸**。这不是「新覆盖的面变了」——它是「进了
图集的材质全部取错了纹素」。

`worldTileCoord(texcoord, sourceSize, offset, extent, atlasSize)` 的语义是
`(offset + (texcoord mod sourceSize) * extent / sourceSize + 0.5) / atlasSize`。
传进去的是 tile 尺寸，`extent / sourceSize` 因此恒等于 1：

- 一个 1024 的 VTF 被 box-downscale 成 512 的 tile 后，tile 里装的是**整张图的
  一次降采样**；uv 却按 512 归一化，于是同一张图在它所属的那个面上被铺了 **2×2 遍**；
- 2048 的源就是 **4×4 遍**，并且 wrap 边界也错（在 512 而不是 2048 处回绕）。

本轮实测：`vtfTooLarge=70`、`downscaled=70`——85 个进了图集的材质里有 **70 个**
比它的 tile 大，也就是 70 个全部取错。逐面追踪的 24 个面里 `oversized=24`，
**没有一个降采样材质在修复前是对的**。

| 读数 | T17 基线 `0c43e3e` | 当前树（修复前） | 当前树（修复后） |
|---|---|---|---|
| `material-fetch tiled` | — | 85 | 85 |
| `material-fetch downscaled` | — | 70 | 70 |
| 与 T17 基线的逐像素差异 | — | 1,063,297 px（41.18%），max Δ160 | 1,524,364 px（59.03%），max Δ157 |
| 与「修复前」的逐像素差异 | — | — | **1,459,130 px（56.50%），max Δ123** |
| 逐面比对（albedo，tol 3） | 无此仪器 | 21/23 不匹配 | **0/23 不匹配，worst Δ1** |
| 逐面比对（textured，tol 6） | 无此仪器 | — | **0/23 不匹配，worst Δ2** |

修复本身是一行的事，所以这一轮真正的交付是**能判定的仪器**：产品自己沿着
`点 → 最前的可见三角面 → 材质名 → VMT 仓库 → VTF 仓库 → tile 矩形 → atlas uv →
纹素 → 像素` 走一遍，并把「这个像素应该是什么颜色」写进 trace；脚本拿同一帧的
同一个像素去比。计数器说「85 个材质拿到了 tile」，这个仪器说「墙上那个像素是墙
自己那张图的第几个纹素」。

### 判别（不是自证）

改 uv 会改半个屏幕的像素，但「像素变了」不等于「改对了」——CPU 镜像若和 GPU 用
同一个公式，两个一起错也会一起对。所以 trace 对**同一个像素**发射**两个互斥的
预测**（`albedo` = 源尺寸归一化，`albedoTile` = tile 尺寸归一化），并且两个都跑
一遍两个构建：

| 构建 | 预测 | 结果 |
|---|---|---|
| 带修复 | `albedo`（源尺寸） | **PASS** 23/23，worst Δ1 |
| 带修复 | `albedoTile`（tile 尺寸） | FAIL 21/23 不匹配，worst Δ57 |
| 只有 uv 那一行回退 | `albedoTile`（tile 尺寸） | **PASS** 23/23，worst Δ1 |
| 只有 uv 那一行回退 | `albedo`（源尺寸） | FAIL 21/23 不匹配，worst Δ57 |

`discriminating=24`：24 个比较点里，两个预测**没有一个点给出同一个纹素**。所以
这不是「某个 uv 能复现这一帧」，而是「这一帧同意源尺寸、拒绝 tile 尺寸」——而且
反过来那个构建正好相反。

### 新旧覆盖的拆分

「变了」和「错了」必须分开说。24 个可比面按 `beyondOldCap`（T19 之前的上限会
丢掉、这一轮才进图集的面）分组，在修复帧上被旧 uv 预测判错的 21 个点：

- **14 个点在 `beyondOldCap=1` 的面上** —— 这些是 T19 新覆盖的面，T17 本来就没
  给它们纹理，相对 T17 变化是**预期的**；
- **7 个点在 `beyondOldCap=0` 的面上**（该组共 8 个点）—— 这些面 **T17 就已经
  画了**，它们的变化与覆盖率无关，是纯粹的 uv 缺陷。

也就是说：56.5% 的像素变化里，既有「新面第一次有了纹理」，也有「老面的纹理一直
在重复」。后者是这一轮修掉的东西。

## 2. 环境

| 项 | 值 |
|---|---|
| worktree | `D:/TF2_Native_Worktrees/t21-material-visual` |
| branch | `task/t21-material-visual`（从 `9591499` 拉出） |
| base commit | `9591499` Merge branch `task/t17-world-visual` into `p0-entity-protocol` |
| 对照 worktree | `D:/TF2_Native_Worktrees/t21-baseline`（`--detach 0c43e3e`，只用于取 T17 基线帧） |
| 主工作树 | `D:/TF2_Native_Test` —— 未触碰（本任务中途曾误写入，已 `git checkout --` 还原，改动经 `.scratch/t21-wrong-tree.patch` 搬入本 worktree） |
| pinned capture | `autorecord_2026-07-02_13-26-46.dem`，tick **2000**（重试到 draw 自报 2000 为止），1264×681，`--world-water off`，viewmodel `v_scattergun_scout` / companion |
| 集成 | **未合并、未推送**。任务排期规定 T21 的产品校验与集成等 T16 与 T20；push/merge 留给那时。 |

## 3. 改动

### 3.1 产品修复（一处，改渲染结果）

`native/src/native_renderer.cpp` — `uploadWorldGeometry()` 的顶点 uv：

```cpp
// 前：t.width / t.height 出现在 sourceSize 的位置，extent/sourceSize 塌成 1
a.u = worldTileCoord(triangle.au, t.width, t.x, t.width, batch.width);
// 后：sourceSize 是降采样之前的 VTF 尺寸
a.u = worldTileCoord(triangle.au, t.sourceWidth, t.x, t.width, batch.width);
```

`WorldTileReading` 增加 `x / y / sourceWidth / sourceHeight / atlasWidth / atlasHeight`，
`WorldTileSlot` 增加 `sourceWidth / sourceHeight`，`WorldTileSource` 增加
`modulateBytes` 与 `blendDeclared` —— 都是把渲染器已经算出来的数公开给 trace，
不改变渲染。

### 3.2 仪器（不改渲染结果）

| 文件 | 内容 |
|---|---|
| `native/src/main.cpp` | `--world-face-trace`：28 个屏幕点，投影 BSP 三角面，沿材质链走一遍并预测像素。`--world-albedo`（`worldDebugView=3`）：shader 直出 albedo，不乘 lightmap / lighting / self-illum，供逐像素比对。 |
| `native/src/native_renderer.cpp` | `captureDepthBuffer(bool finalPass)`：ViewModel 之前抓世界深度、颜色回读之后抓最终深度（原先在 ViewModel 之后抓，全部是清除值 1.0）；D24S8 按低 24 位解码（实测清除值 `0x00ffffff`）；albedo 视图走 `envFeatures.y`（走 `.x` 会被 flat/depth 分支先拦掉）。 |
| `native/src/main.cpp` `traceFmt` | 缓冲区 512 → 2048，并给仍然发生的截断加 `...[TRUNCATED]` 尾巴。 |
| `face-trace-verify.py` | 读 trace 与 BMP，逐点比 `albedo` / `albedoTile` / `expected`，输出 `FACE-CHECK status=PASS/FAIL compared=… mismatched=… depthSkipped=… discriminating=…`。 |
| `world-material-face-check.sh` | 本轮门禁：5 个正向小节 + 4 个 mutation。 |
| `t21-capture.sh` | 抓帧助手：重试到 draw 自报 tick 2000。 |

镜像必须和 shader 做同一件事，否则「不匹配」是镜像的错不是产品的错。这一轮踩到并修掉的三处：

1. **屏幕空间重心插值**没有透视校正 —— 在透视三角面上误差到 163 LSB。改成按
   `1/w` 加权后归一化。
2. **插值的是映射后的顶点 uv**，不是原始 texcoord —— GPU 在光栅化之前就把每个
   顶点的 texcoord 映射进 atlas 了。跨 wrap 缝的三角面两者不是一回事。
3. **采样的是已降采样的 tile buffer**，不是源纹理 —— 差到 40 LSB。
4. **lightmap 用双线性**，不是 `floor()` 取最近 —— shader 是
   `lightmapTexture.Sample(sampler0, lightUv)`，WRAP/linear。镜像取最近纹素在平
   滑地面上够用，在一道阴影边缘的墙上差 8%（点 1140,230，Δ7 / 容差 6），
   「像光照算错了」其实是镜像没跟上。

### 3.3 追踪行

```
gpu: world face-trace point=1140,230 tri=29587 material=METAL/WALL_ISLAND_HEAVY02C
  vmt=materials/METAL/WALL_ISLAND_HEAVY02C.vmt vmtStore=vpk-volume
  vtf=materials/Metal/wall_island_heavy02c.vtf vtfStore=vpk-volume
  src=1024x1024 tile=0/31 size=512x512 rect=3599,1543 atlas=4112x4112
  uv=0.90852,0.40509 uvInside=1 texel=160,153,144 blend=none
  luv=0.5645,0.3331 light=135,134,143 albedo=160,153,144 albedoTile=165,161,155
  expected=85,80,81 reason=tiled beyondOldCap=0 oldOrder=34
  depth=0.6365 frameDepth=0.6365 finalDepth=1.0000 depthMatch=1 frameRaw=00a2f200
```

`depthMatch=0`（帧深度与该面不符，或最终深度未被清除——被武器/更近的面挡住）的
点**跳过而不是算作缺陷**：那是在和另一个面比颜色。24 个命中面里 1 个因此跳过
（720,390，被 viewmodel 挡住），4 个点没有可见三角面（天空）。

## 4. 具名拒绝

不是「有几个没画上」，而是**每个理由带 PVS 可见三角面数**，因为 3 个材质的权重
和 30 个材质的权重不是一回事：

```
gpu: world visible-refusal reason=no-basetexture materials=20 visibleTriangles=758
gpu: world visible-refusal reason=vmt-missing     materials=3  visibleTriangles=144
```

- `no-basetexture`（20 个材质 / 758 个可见三角面）：VMT 在 pakfile 里解析到了，
  但没有 `$basetexture`。这些面走 flat 分支，`reason=vmtNoBase`。
- `vmt-missing`（3 个材质 / 144 个可见三角面）：VMT 从 loose / VPK / pakfile 都
  没解析出来。

`material-fetch` 计数是一张完整的划分：
`distinct=108 = tiled 85 + capped 0 + vmtMissing 3 + vmtNoBase 20 + vtfMissing 0 +
decodeEmpty 0`（门禁断言这一行）。blend 链路同样全解析：
`blend-fetch declared=10 resolved=10 refused=0 modulateDeclared=10 modulateResolved=10
modulateRefused=0`，10 个 `$basetexture2` 与 10 个 `$blendmodulatetexture` 都被
重采样到所属 tile 的网格上，镜像照做（lower=sat(g−r)、upper=sat(g+r)、
smoothstep）。

## 5. 门禁

`bash world-material-face-check.sh` 正向（rc=0）：

1. `material-fetch` 的划分闭合；
2. 图集表 85 行、85 行都是 `tiled`、每行都有矩形（没有材质在 fetch 和 plan 之间消失）；
3. 逐面链：28 行 = 28 个点，`hits=24 > 0`、`oversized=24 > 0`、
   `beyondOldCap=16 > 0`（两种情形都真的被走到），tiled 面的 uv 全在自己矩形内，
   没有 untiled 面冒充 tile，没有 tiled 面采不到纹素；
4. 像素：`textured` 与 `albedo` 都 PASS，**并且 `albedoTile`（旧 uv）必须被拒**，
   `discriminating > 0`（否则「拒绝旧 uv」没有任何依据）；
5. 重复抓帧逐字节一致；区域读数与拒绝行落盘。

`bash world-material-face-check.sh --mutation`（rc=0）：

| mutation | 要求 | 实测 |
|---|---|---|
| M1 uv 用 tile 尺寸归一化 | albedo 转红 **且** `albedoTile` 转绿 | 21/23 不匹配（worst Δ57）；帧也不同；**`albedoTile` PASS 23/23** |
| M2 atlas uv 去掉 tile 原点 | albedo 转红 | 21/23 不匹配（worst Δ101） |
| M3 预测去掉 lightmap 项 | textured 转红、**albedo 保持绿** | 23/23 不匹配（worst Δ198）；albedo 绿 |
| M4 丢失矩形的行仍自称 `tiled` | 表/名断言转红 | 2 行自称 tile 却没有 batch |

四个 mutation 之后 `native_renderer.cpp` 与 `main.cpp` 均 `RESTORE=OK`（逐字节）。
M1 的「另一半」是本轮新增：只有 uv 那一行回退的构建必须**同意** `albedoTile`，
否则这个仪器量的就不是归一化，只是「有东西变了」。

## 6. 已知未解决 / 交给谁

- **图集预算分支没被走到**：`atlas-plan` 报
  `85 of 85 materials tiled in 2 batch(es); refused=0 (malformed=0, duplicate=0,
  budget=0); layers=89+64+64 MiB`。85 个材质全放得下，所以
  `AtlasBudgetExceeded` / `Malformed` / `Duplicate` 三条拒绝在本帧没有任何证据，
  只有代码路径。更大的图才会真的走到。
- **光照语义**（`dynamicLighting` / specular / bump / `$bumpmap2` / `%detailtype` /
  `$ssbump`）：本轮的 textured 预测只复现了当前 shader 真正做的事
  （`albedo × pow(lightmap, 1/2.2) × lighting`）。未实现特性的具名与取舍归 **T24**。
- **两块全黑区域**（510,390 与 930,390 在 textured 帧上 `want=0,0,0`，预测与实际
  一致）：像素和预测**一致**，所以不是取错纹素；是这块地真的没有光。归 **T23**
  （几何/可见性）与 **T24**（光照）。
- **trace 行长**：`traceFmt` 的 512 字节缓冲把 per-face 行的尾巴（`depthMatch`、
  `frameRaw`）悄悄吃掉过——一个点因此没有深度判决却被当成「就是这个面」。已改成
  2048 并给残留截断加显式尾巴。**其他长 trace 行此前可能也被截过**，值得单独立项扫一遍。

## 7. 复现

```
cd D:/TF2_Native_Worktrees/t21-material-visual
bash build-target.sh tf2_demo_native
bash world-material-face-check.sh                 # 正向
bash world-material-face-check.sh --mutation      # 变异（约 13 分钟）
bash t21-capture.sh .scratch/t21/albedo.bmp .scratch/t21/albedo.trace \
     --world-albedo --world-face-trace --world-water off
python face-trace-verify.py --trace .scratch/t21/albedo.trace \
     --frame .scratch/t21/albedo.bmp --mode albedo --min-points 12
python face-trace-verify.py --trace .scratch/t21/albedo.trace \
     --frame .scratch/t21/albedo.bmp --mode albedo-old --min-points 12   # 必须 FAIL
```

证据：`evidence/world-material-face/`（`face-albedo.txt`、`face-textured.txt`、
`face-albedo-rival.txt`、`refusals.txt`、`regions.txt`、`identity.txt`、
`trace-*.txt`、`frame-diffs.txt`、`uv-discrimination.txt`，以及五张对照 PNG）。
