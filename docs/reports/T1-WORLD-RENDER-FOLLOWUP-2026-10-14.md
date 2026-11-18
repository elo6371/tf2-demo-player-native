# T1 世界渲染能力补全 — 交付报告

- 任务：T1 世界渲染能力补全（skybox 几何绘制 + VIS 实际三角形裁剪 + atlas 丢面修复）
- 分支：`task/world-render-followup`
- worktree：`D:\TF2_Native_Worktrees\world-render-followup`
- 基线：`p0-entity-protocol@3c92f7d`
- 日期：2026-10-13 ~ 2026-10-14
- 上游文档：`T3-WORLD-MATERIAL-LIGHTING-2026-10-13.md`

## 0.1 接手方式与提交链

本轮由 **Ada** 接手（上一轮做到「产品改动完成、接口已冻结、门禁已扩，但 §5.2/§6/§7 是占位符」）。
提交链：

| 提交 | 内容 | 状态 |
|---|---|---|
| `731032d` | 接口冻结（只含 2 个头文件声明，+105 / −0） | 上一轮已提交 |
| 本轮产品提交 | `native/` 全部改动（skybox、VIS 裁剪、atlas 选择）+ 证据文件 | 本轮提交 |
| 本轮门禁提交 | `world-material-check.sh`（含三个门禁缺陷修复）+ 变异日志 | 本轮提交 |
| 本文件 | §5.2 / §6 / §7 / §8 由本轮运行填写 | 本轮提交 |

上一轮的原文保留在第 1–4 节、§8 第 1–8 条；本轮补充的结论集中在 §7.2/§7.3 和 §8 第 9–12 条。

---

## 1. 本轮之前的状态（基线事实）

T3 那轮把世界材质的六项能力做成了「状态 + 原因 + 计数」，但其中两项只到「资源读数」为止：

| 能力 | T3 结束时 | 证据 |
| --- | --- | --- |
| lightmap atlas | 货架宽度硬编码 2048；装不下的面**静默丢弃** | snakewater 丢 15 / 11527 面 |
| skybox | 只有资源读数（6 vmt、6 贴图、3145732 字节），**一个顶点都没建** | `sky.state=active` 但 `skyboxVertices` 这个字段根本不存在 |
| VIS | 只统计「若裁剪能藏多少」，实际不裁 | main.cpp 打印里的 `clipApplied=0` 是字面量 |
| abbey 等大图 | 丢 4137 / 22991 个 lightmap 面，无具名原因 | `lightmapFaceCount - lightmapPackedFaces` 的差值，无人打印 |

本轮要证的三句话：**天空盒真的被画出来了**、**PVS 真的裁掉了三角形**、**atlas 真的不再丢面（丢的时候有名字）**。

---

## 2. 接口冻结（先提交，后实现）

`731032d feat(world-render): freeze the skybox draw and VIS clip interface`

只含 `native/include/bsp_map.h` 与 `native/include/world_material.h` 的**声明与读数结构**，不含任何实现：

- skybox：`WorldSkyVertex` / `WorldSkyFace` / `WorldSkyState{Unavailable,Fallback,Active}` / `WorldSkyDrawRequest{enabled, halfExtent, faces[6]}` / `WorldSkyDrawReading{state, skyboxVertices, skyboxTriangles, skyboxFaces, skyboxDraws, reason}` / `evaluateSkyDrawRequest()`
- VIS：`WorldVisClipState{Unavailable,Disabled,Active}` / `WorldVisClipRequest{enabled, cameraCluster}` / `WorldVisClipReading{state, clipApplied, inputTriangles, outputTriangles, hiddenTriangles, trianglesWithoutCluster, cameraCluster, reason}` / `clipWorldTrianglesByVis()`
- atlas：`BspMap` 新增 `lightmapAtlasMaxWidth / lightmapAtlasDroppedFaces / lightmapAtlasGrown / lightmapAtlasReason`

两个入口都是**纯函数**（不碰 GPU、不读文件、不解析路径），所以探针断言的就是渲染器执行的那份代码——一个「测试专用第二实现」的门禁可以在裁剪是错的时候变绿，这份不会。

冻结提交后 `bash build-target.sh world_material_probe` rc=0（只加声明、还没实现）。

实现期在冻结接口上追加了两处**向后兼容**的扩展（不改动既有签名）：

- `captureSkyFaces(map, read)`：把六面**像素**交给上传者，与 `measureSky` 共用同一条解析/解码路径，计数器与像素不可能来自两组文件。
- `clipWorldTrianglesByVis(..., keptIndices = nullptr)`：可选出参，回填存活三角形的**输入下标**。渲染器因此能把存活集合直接变成索引缓冲，而不需要第二次实现裁剪逻辑去重算可见性；不传该参数时行为与冻结时完全一致。

---

## 3. 产品改动

### 3.1 lightmap atlas：宽度变成「选出来的」（`bsp_map.cpp` / `bsp_map.h`）

原来是 `constexpr int kAtlas = 2048;`，装不下的面直接丢，字面上没有任何读数。

现在 `BspParser::parse(bytes, map, triangleLimit = 200000, atlasMaxWidth = 4096)`：
货架打包前先用**同一套算法**空跑一遍候选宽度 `{2048, 4096}`，返回第一个装得下的；2048 装得下就地保持 2048（所有已 pin 的小图读数不变）。打包结束后无条件写：

- `lightmapAtlasMaxWidth`：最终采用的宽度
- `lightmapAtlasDroppedFaces = lightmapFaceCount - lightmapPackedFaces`
- `lightmapAtlasGrown = (width > 2048)`
- `lightmapAtlasReason`：`"all 11527 faces fit the 4096-wide atlas"` / `"N of M faces did not fit the 2048-wide atlas"` / `"the bsp declares no lightmap faces"`

**这不是「保留具名回退」，是本轮真修好了**：

| 地图 | 修前 | 修后 |
| --- | --- | --- |
| cp_snakewater_final1 | 2048×2047，11512/11527，丢 15，lightmapped 三角形 199263 | **4096×1156，11527/11527，丢 0**，199922 |
| 2koth_abbey | 2048 宽，22991 面丢 4137 | **4096×1654，22991/22991，丢 0** |

uv 归一化与 v 重缩放都跟着 `kAtlas` 走，所以「atlas 变大」没有引入坐标口径变化。

### 3.2 skybox：从「文件在哪」到「36 个顶点被画出来」

- **解码**：`captureSkyFaces()` 用与 `measureSky()` 相同的 `VmtParser` / `VtfTexture` 路径取六面 RGBA。
- **建盒**：`evaluateSkyDrawRequest()` 按 D3D11 立方体面序（+X,−X,+Y,−Y,+Z,−Z）生成 6 面 × 6 顶点，uv 角点固定为 (0,0)(1,0)(1,1)。
- **上传**：`Renderer::uploadWorldSky()`。本机 `sky_well_01dn.vtf` 是 **1×1** 而它的五个兄弟是 512×256 / 512×512——立方体贴图要求六面等大方块，因此取「覆盖最大边的最小 2 的幂」（上限 1024）做最近邻重采样，并把 `resampled=1`、`side=512` 作为读数打印。少于六面直接拒绝，不用黑面补齐（黑面会采样成「那个方向是黑的」，是对内容的谎报）。
- **绘制**：世界像素着色器新增 **mode 3 分支**（`envFeatures.x > 2.5`）→ `envCube.Sample(sampler0, safeNormalize(worldPosition))` 直接返回，不参与光照/光照贴图。常量缓冲用**独立的** `worldSkyConstants_`，其 mvp 把视图矩阵平移列清零，盒子因此跟着相机；光栅状态 `worldSkyRasterizerState_` 关掉背面剔除（盒子是从内侧看的，背面剔除会把六面全剔掉，且会安静地表现成「没画」）。
- **读数**：`gpu: world sky state=... vertices=36 triangles=12 faces=6 draws=1 side=512 resampled=1 reason=...`

### 3.3 VIS：真的裁，而且是逐帧跟着相机裁

一次性裁剪（加载时按当时的观察者簇裁死）会让相机走到别的叶子后再也看不到应该可见的几何——那是把「不裁」换成「裁错」。因此：

- `Renderer` 持有 world 顶点缓冲对应的**索引缓冲**（DYNAMIC，R32_UINT），世界 pass 在裁剪生效时走 `DrawIndexed`，被 PVS 藏掉的三角形**离开光栅化器**，而不只是离开读数。
- 集群只由 `clipWorldTrianglesByVis()` 决定（探针断言的是同一个函数）；main.cpp 只在**相机簇变化时**重建索引表（`applyWorldVisClip`），并且绘制的那一帧就是取证的那一帧（trace 行从帧循环里发，不再从加载期发）。
- 传进去的是 `map.triangles` 的**簇投影向量**（索引对齐、只填 `cluster`）：裁剪只读这一个字段，直接把 20 万个 `BspTriangle` 连同其 `std::string material` 拷一遍会让每次跨叶子都卡一下。
- 无簇三角形（PVS 无话可说）保留并计数，不用「没数据」冒充「不可见」。

### 3.4 开关

| 参数 | 默认 | 说明 |
| --- | --- | --- |
| `--world-sky` / `--no-world-sky` | 跟随 preset（`standard` 开、`performance` 关） | 天空盒绘制 |
| `--world-vis-clip` / `--no-world-vis-clip` | 开 | PVS 逐三角形裁剪 |

门禁两个状态都跑，因为「裁剪生效」只有在「裁剪不生效时读数不同」的前提下才是读数。

---

## 4. 真实资源读数

**输入清单**（门禁第 4/6/7 节读的同一批）：

| 输入 | 作用 |
|---|---|
| `.../tf/maps/cp_snakewater_final1.bsp` | 全图六能力 pin |
| `.../tf/maps/2koth_abbey.bsp`（74658895 B） | cubemap 源；本轮补跑 atlas 读数 |
| `.../tf/maps/koth_bagel_rc13.bsp` | 负对照（本机未安装 → 空图，六能力全部 unavailable） |
| `D:/TF2_Demo_Player/testdata/demos/bb841c6d379f…matcha-20260927-1347-cp_snakewater_final1.dem` | 产品侧真实录制 |

本轮补跑产物 `evidence/world-material/t1-followup/real-2koth_abbey.txt` —— §3.1 的 abbey 数字原文只有文字、无产物，
本轮从产品自己的解析器重新取了一遍：`atlas 4096x1654` / `facesPacked 22991 of 22991` /
`facesDropped 0` / `atlasMaxWidth 4096` / `atlasGrown true` /
`reason "all 22991 faces fit the 4096-wide atlas"`，与 §3.1 一致。

`evidence/world-material/` 是提交进仓库的证据目录（门禁默认写它，`WORLD_MATERIAL_OUT` 可改）。
本轮改动的四个文件：`negative-bagel.txt`(+3) / `real-cp_snakewater_final1.txt`(+9 −6) /
`render-lightmap.txt`(+3 −2) / `self-test.txt`(+1)；
本轮新增：`render-off.txt`、`mutated-sky_draw_deleted.txt`、`mutated-vis_clip_deleted.txt`；
每轮运行的 stdout/stderr 全量日志在 `evidence/world-material/t1-followup/`。

### 4.1 skybox（cp_snakewater_final1 + 真实 demo，`--world-sky`）

```
gpu: world sky state=active vertices=36 triangles=12 faces=6 draws=1 side=512 resampled=1 reason=six sides, 36 vertices
```

`--no-world-sky` 时：

```
gpu: world sky state=unavailable vertices=0 triangles=0 faces=0 draws=0 side=512 resampled=1 reason=skybox draw disabled by the caller
```

天空盒确实改变了画面：同一 tick 0 的两帧 BMP 逐字节比较 **2010951 字节不同**，整幅均值 RGB 从 (26,28,31)（清屏色）变为 (101,95,118)（天空贴图）。

### 4.2 VIS（同一次运行）

```
gpu: world vis cameraCluster=2287 visibleFromCamera=585 clusters=2940 clusteredTriangles=34517 hiddenTriangles=29756 withoutCluster=165483 inputTriangles=200000 drawnTriangles=170244 clipApplied=1 state=active reason=hid 29756 of 200000 triangles from cluster 2287
```

`--no-world-vis-clip` 时：

```
gpu: world vis cameraCluster=-1 visibleFromCamera=-1 clusters=2940 clusteredTriangles=0 hiddenTriangles=0 withoutCluster=0 inputTriangles=0 drawnTriangles=200000 clipApplied=0 state=disabled reason=vis clip disabled by the caller
```

恒等式 `drawn + hidden = input`（170244 + 29756 = 200000）在门禁里被断言：只把三角形「不上传」来实现的裁剪会满足 `drawn` 与 `hidden` 两个数字而在这条上变红。

### 4.3 atlas

```
{"reading":"lightmap.atlas","value":"4096x1156"}
{"reading":"lightmap.atlasGrown","value":"true"}
{"reading":"lightmap.atlasReason","value":"all 11527 faces fit the 4096-wide atlas"}
{"reading":"lightmap.facesDropped","value":"0"}
{"reading":"lightmap.trianglesWithLightUv","value":"199922"}
```

### 4.4 未回归的部分（同一次探针运行）

- sky 读数逐字未变：`sky.name=sky_well_01`、`vmtFound=6`、`texturesResolved=6`、`texturesDecoded=6`、`firstSize=512x256`、`smallestSize=1x1`、`decodedBytes=3145732`。
- cubemap 全部读数未变（33 样本、198 面、`fromPakfile=33`、首样本面均值与校验和）。
- displacement / water / vis 计数未变（1316 面、165376 三角形、18 水面、36 三角形、2940 簇）。
- 夹具：9 个夹具、90 条断言、7 条变异、变异后 120 条断言——数字与 T3 完全一致。
- GPU 侧：`vertices=600000`、`lightmapped=3 × 解析三角形`、`displaced/water` 与解析侧一致。

---

## 5. 门禁与变异

### 5.1 新增/更新的断言（`world-material-check.sh`）

第 1 节（探针自测）新增 `worldDrawSelfTest` 第三行共 15 个标志位：盒子的 36 顶点/12 三角形/6 面/1 次绘制、六面各自落在 ±halfExtent 平面上、uv 角点、缺面时 `4 of 6` 具名回退、缺 skyname/无面时的具名原因、被调用方关闭时的原因、裁剪藏 2/6 且索引正确、换簇后藏 4/6、关闭时不裁、无相机簇/无 PVS 时具名不可用、以及 **同一批 17 个 512×512 面在 2048 下丢 1、在 4096 下全装下**。

第 4 节全图 pin 更新为修后的值，并新增 `lightmap.atlasMaxWidth / atlasGrown / atlasReason`。

第 6 节新增 skybox 六条与 VIS 四条（含 `drawn + hidden = input` 恒等式）。

第 8 节「两个开关关掉」：独立跑一次 `--no-world-sky --no-world-vis-clip`，断言天空 `vertices=0 draws=0` 且原因是「调用方关掉了它」，裁剪 `state=disabled clipApplied=0 drawnTriangles=200000`，并断言**开/关两个状态下的绘制三角形数不同**（170244 < 200000）。

> 节号说明：接手前脚本打印的顺序是 1,2,3,4,5,**7,8,6** —— 标签和实际执行顺序不一致。
> 本轮按实际顺序重排为 1..8（原第 7 节改第 6 节，原第 8 节改第 7 节，原第 6 节改第 8 节）。
> 上面三行的「第 N 节」用的是**重排后**的编号。原因见 §7.2 第 3 条。

### 5.2 变异

`bash world-material-check.sh --mutation`（本轮最终一次，见 §6）：

```
=== mutation: wrong expectations first, then two product mutations ===
  MUTATION-CAUGHT=PASS (sky_draw_deleted: draws=0 while the state still reads active)
  MUTATION-CAUGHT=PASS (vis_clip_deleted: clipApplied=0, drawn=200000 = the whole buffer)
MUTATION-CAUGHT=PASS (six wrong expectations, the deleted sky draw and the
  deleted pvs clip each go red; both sources came back byte-identical)
```

八节正向断言 117 条全绿，一个 FAIL 都没有；两个源码变异从 sed 落地（`cmp` 证明改过）到重建、跑产品、捕获取证、还原、`cmp` 证明逐字节还原，全链路都走到了。
探针层另有 7 个夹具变异（探针输出）与 `--self-test` 的 21 个自检断言。

**两个变异各自捕到的「具名读数」**：

| 变异 | 改的是什么 | 变红的读数 |
|---|---|---|
| `sky_draw_deleted` | `native_renderer.cpp` 的 `hasSkyBox` 守卫 AND 上 `false`（盒子仍上传、状态仍 active） | `draws=1 → 0`（**不是** state） |
| `vis_clip_deleted` | `main.cpp` 的 `applyWorldVisClip` 里 `enabled = false` | `clipApplied=1 → 0`，`drawnTriangles 170244 → 200000` |

后者有一个**已知的特异性限制**，写在这里而不是藏起来：它改的是「关掉裁剪」而不是「删掉裁剪代码」，
所以变异后产品的读数和用户主动传 `--no-world-vis-clip` **逐字节相同**（`evidence/world-material/render-off.txt`
与 `mutated-vis_clip_deleted.txt` 的 vis 行 `cmp` 结果相同）。
门禁能抓到它，靠的是正向运行 `clipApplied=1`；但它**不能**证明产品能区分「用户关的」和「裁剪代码没了」。

---

## 6. 命令、退出码

全部由 **Ada 接手后重跑**（2026-10-14，产物哈希与输入样本见 §4 和 `evidence/world-material/`）：

| 命令 | 退出码 | 末行 / 关键行 |
|---|---|---|
| `bash build-target.sh world_material_probe tf2_demo_native` | **0** | `rc=0` / `[100%] Built target tf2_demo_native` |
| `bash world-material-check.sh` | **0** | `WORLD-MATERIAL=PASS`，**117 OK / 0 FAIL**，3m11s |
| `bash world-material-check.sh --mutation` | **0** | `MUTATION-CAUGHT=PASS`（八节 117 OK / 0 FAIL + 两个源码变异全捕） |
| `bash verify-fast.sh` | **0** | `VERIFY-FAST=PASS (assertions_ok=43)`，37s |
| `world_material_probe.exe --bsp <abbey> --tf-root <tf>` | **0** | `atlas 4096x1654` / `22991 of 22991` / `dropped 0` |

> 关于 `--mutation` 的退出码：它 rc=0 是在**修完两个门禁缺陷之后**（§7.2）。
> 修完之前那次运行打印了一条 `FAIL` 却仍然 rc=0 —— 那个 bug 本身见 §7.2 第 2 条。

## 7. 结果

### 7.1 Ringo 要求证明的六件事

| 要求 | 读数 | 成立 |
|---|---|---|
| `skyboxVertices > 0` 或明确报告不可用原因 | 正向 `gpu: world sky state=active vertices=36 triangles=12 faces=6 draws=1 side=512 resampled=1 reason=six sides, 36 vertices`；关掉时 `state=unavailable vertices=0 draws=0 reason=skybox draw disabled by the caller`；bagel（无 BSP）时六个能力全部 unavailable 且各带原因 | 是 |
| `clipApplied=1` | `gpu: world vis ... clipApplied=1 state=active reason=hid 29756 of 200000 triangles from cluster 2287` | 是 |
| VIS 开关前后几何数量不同 | 开 `drawnTriangles=170244` / 关 `drawnTriangles=200000`，门禁断言 `170244 < 200000` | 是 |
| 缺 skybox、缺 VIS 时有具名 fallback | 缺面 `4 of 6`；缺 skyname / 无 PVS / 无相机簇各有专门原因串；被调用方关闭 → `disabled by the caller`（与「这张图没有天空」是不同原因） | 是 |
| 删除 skybox draw 能被 mutation 捕获 | 捕获：`draws=0 while the state still reads active` | 是 |
| 删除 VIS 裁剪能被 mutation 捕获 | 捕获：`clipApplied=0, drawn=200000`（特异性限制见 §5.2） | 是 |

天空盒确实改变画面，不只是增加一个读数：同一 tick 0 两帧 BMP 逐字节 **2010951 字节不同**，
整幅均值 RGB 从 (26,28,31)（清屏色）变为 (101,95,118)（天空贴图）。

### 7.2 接手时门禁自身的两个缺陷（都是「判据会自己骗人」）

上一轮的产品改动是完整的，但门禁有一处会给出**假的绿**。两个缺陷都是我在跑验收时撞出来的，不是读代码读出来的。

**① `rm -f` 之后直接读，旧证据会被当成新证据。**

第一次 `--mutation` 运行（2026-10-14 上午）打出：

```
=== 6/8 the two switches, off ===
[safe-delete][SAFE_DELETE_BULK_GUARD_ERROR] EPERM: operation not permitted, rename ...
world-material-check.sh: line 419: .scratch/world-material/render-off.log: Permission denied
  FAIL switches-off run rc = 1, expected 0
  OK   sky off state = unavailable          <- 这一行和后面 7 行全是假的绿
  ...
```

产品运行**失败**了（日志文件的 shell 重定向被拒），但同节后面 8 条断言仍然打印 OK。
原因：脚本第 413 行 `rm -f "$OFF_TRACE"` 被同一套 delete guard 拒掉，**上一次运行的 trace 留在了原处**
（实测时间戳 10:19，而这一轮是 10:30），于是 `grep "$OFF_TRACE"` 读的是**另一次运行**的字节。
`rm -f` 在「文件已不存在」和「删不掉」两种情况下都返回 0，所以「先删再读」在本机不是新鲜证据的保证。

同一个形状还埋伏在另外三节（`render-lightmap.*`、`render-cubemap-*`、`run_mutated_product`）里，
其中前两个的 `$OUT/*.txt` 是**提交进仓库**的证据文件：运行失败时 `echo "" > "$OUT/render-lightmap.txt"`
会把上一次成功的证据覆盖成空行。

**修法**：给本轮写的每个文件加运行标签 `RUN_TAG="$(date +%Y%m%d%H%M%S)-$$"`。
一个带着本次运行自己启动时间和 pid 的路径不可能被读成别人的证据，判据因此从「假设删成功」变成**结构性新鲜**。
证据文件改成**只在真的拿到读数时才写**，拿不到就 FAIL 并保留原文件。
两个源码备份的删除也从「假设删成功」变成删完 `cmp` 检查，删不掉即 `RESTORE=FAIL`。

**② `--mutation` 无条件 `exit 0`。**

同一个运行里，`fail=1` 已经置上（因为它打印了 FAIL），但变异分支的最后是 `exit 0`。
于是「门禁打印了一条 FAIL，然后返回成功」——和上一轮 T2.5 踩过的那个形状完全一样
（**`fail` 没有成为裁决的一部分**）。修法：变异分支的退出码现在要求**两个裁决同时成立** ——
两个变异都变红，且八节正向断言一条都没红。

**③ 节号和执行顺序不一致（顺手修）。**

脚本实际打印顺序是 1,2,3,4,5,7,8,6。任何按编号读日志的人都会读错位置。
已按实际顺序重排为 1..8，并同步脚本头部说明和注释里的交叉引用。脚本外部没有任何地方依赖这些标签（已 grep 确认）。

### 7.3 我做了什么

- 读完上一轮的交接（`T1-WORLD-RENDER-FOLLOWUP-2026-10-14.md`）后，先核实接口冻结提交 `731032d` 确实是
  2 个头文件 / +105 / −0（纯接口），再核实源码时间戳与 `.obj` 时间戳一致（`main.cpp.obj` 00:46:38 >
  `main.cpp` 00:46:27），确认二进制是最新的。
- 独立补跑了 abbey 的 atlas 读数（§4.3 之前只有文字，没有捕获产物）：
  `4096x1654 / 22991 of 22991 / facesDropped 0 / reason "all 22991 faces fit the 4096-wide atlas"`，
  存为 `evidence/world-material/t1-followup/real-2koth_abbey.txt`。
- 修掉 §7.2 的三个门禁缺陷，然后**在修好的脚本上重跑** 构建 + 正向门禁 + 变异门禁 + `verify-fast.sh`，
  四次的退出码与末行都在 §6。

---

## 8. 未完成项与已知限制（**不是 PASS，需要单独看**）

1. **索引表重建是全量扫描**：相机每换一个簇，`clipWorldTrianglesByVis` 会在 CPU 上扫一遍 20 万个三角形的簇位。聚类移动频繁的 demo 会因此出现周期性卡顿；本轮没有做空间索引或分块，也没有测量帧时影响。**未验证**。
2. **天空盒六面尺寸不一致时靠最近邻重采样**得到等大方块（本机 `sky_well_01dn.vtf` 是 1×1）。重采样用的是顶点级最近邻（按面比例取整像素），没有做线性过滤；缩放后上下两面会有可见的块状。**未验证**。
3. **天空盒不写深度策略未改**：天空先画、世界后画，靠「世界几何比盒子近」保证遮挡。相机若离开 40 单位半程（世界被缩放进约 1.8 单位盒，本轮不会发生）或远平面小于 69（当前 100），行为未定义。**未复现、未验证**。
4. **`--world-sky` / `--world-vis-clip` 没有进入持久化设置**：只作为命令行开关存在，配置文件里仍只有 preset 的 `skybox` 位。**未实现**。
5. **VIS 只做「三角形—簇」一步**：没有 portal 递归、没有按簇合并表面、没有把 `Sphere`/`Box` 面的簇映射做特殊处理。面映射取叶子表的第一条记录（解析器规则），与 Source 的 leaf→face 语义未做交叉验证。**未验证**。
6. **本次只实测了两张图**（cp_snakewater_final1、2koth_abbey）。4096 上限对 luxel 面积更大的地图仍会丢面——那种情况下 `lightmapAtlasReason` 会打印 `"N of M faces did not fit the 4096-wide atlas"`，本机没有这类地图可测。**未覆盖**。
7. 每帧 `captureSkyFaces` 只在加载期调用一次；切图（`reloadGpuResources`）路径未做端到端验证（本轮没有触发切图的用例）。**未验证**。
8. 本轮**未改动**（属其它任务）：ViewModel、音频、相机/HUD、水面高级材质、`verify-all.sh`、README/HANDOFF/CHANGELOG/IMPLEMENTATION-PLAN。
9. **`vis_clip_deleted` 变异不是特异性变异**：它的读数和用户主动传 `--no-world-vis-clip` **逐字节相同**
   （`render-off.txt` 与 `mutated-vis_clip_deleted.txt` 的 vis 行 `cmp` 一致）。
   门禁靠正向运行 `clipApplied=1` 抓到它，但**不能**证明产品能区分「用户关的」和「裁剪代码没了」。
   要补这条特异性，需要第三个状态（例如裁剪被请求却未能应用时给出第三种 reason），本轮没做。
10. **`RUN_TAG` 让证据结构性新鲜，但没让目录变干净**：每轮运行在 `.scratch/world-material/` 留下
    带时间戳的一套文件。本机沙箱的 delete guard 偶发拒绝删除（这次就拒了一次），
    所以这些文件**只能手工清、不能指望脚本清**。`.scratch/` 已 gitignore，不进提交。
11. **`--no-build` 未测**：本轮门禁都带构建。`world-material-check.sh` 没有 `--no-build` 参数（只有 `--mutation`），
    所以「不重新构建直接跑门禁」这条路径没有验证过。
12. **变异门禁只在修复缺陷后跑通过一次**。修复前那次同时暴露了 A/B 两个缺陷，但它本身是**红的**，
    不能算作一次通过的正向证据；§6 表里记录的是修复后的那次。

**能力边界（本机做不到的，不写成已验证）**：真实人工画面对照、真实 GPU 上的 FPS/soak、
干净 Windows 上的端到端、跨机器抓帧点集合 —— 本机都不具备，全部未验证。
本轮只证明了「几何被建出来、裁剪被执行、读数与源码提交绑定、判据能变红」。
