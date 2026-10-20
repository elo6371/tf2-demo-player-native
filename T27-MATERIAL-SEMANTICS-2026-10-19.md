# T27 — 影响可见输出的材质语义

2026-10-19 起，2026-10-20 收尾（正向 163 断言 / 变异 5-5 全红） · owner: rendering specialist · branch `task/t27-material-semantics`

## 1. 结论

T27 要回答的是「VMT 里写的材质键，有没有真的走到像素上」。这句话包含四段不同的证明，
计数器只能给出其中一段：

1. **声明**：VMT 里有这个键（probe 在真实 BSP 上逐材质读，不读渲染器的缓存）；
2. **查找**：键命名的 VTF 在资源链里解析、解码；
3. **着色器输入**：解出来的纹素进到着色器，按正确的 uv、切线基、权重被采样；
4. **像素差**：同一个相机、同一个 tick，只动一个语义，画面变化**且有方向**。

改动之前，世界 pass 只有一个 bump 采样槽、一套图集 uv、没有切线基，于是 `$bumpmap2`、
`%detailtype`、`$ssbump` 只能整行记为 `refused`——它们和「没有声明」在同一行里无法区分。
本轮给三个材质特性各配了一条独立的图集与开关，一次抓帧只动一个语义，并给每一项一条
可重算的账本句：

| 特性 | 声明 / 可见解析 / 绘制 | 开关 | 实测帧差（同一 tick，只动这一项） |
|---|---|---|---|
| `$bumpmap` | 45 / 9（其余 36 是 `$ssbump`）/ 9 材质，19,305 顶点 | `--world-material-bump on` | C→D **624** px（355 亮 / 269 暗），max Δ21 |
| `$detail`（本安装无资源） | 15 / 0（15 个 VMT 全缺）/ 0 | `--world-detail on` | A→E **0** px |
| `$detail` + 诊断纹理 | 15 / 15 / 15 材质，73,770 顶点 | `--world-detail on --world-detail-texture` | A→F **82,213** px，全暗，max Δ60 |
| `$bumpmap2`（产品路径：4 个无 layer 1，1 个被遮挡） | 5 / 1（4 个 `$ssbump` 没有 layer-1 法线）/ 1 材质，384 顶点 | `--world-bump2 on` | R1→R3 **0** px |
| `$bumpmap2` + 诊断纹理（4 个用 legacy base，1 个用 layer 1） | 5 / 5 / 5 材质，41,118 顶点 | `+ --world-bump2-texture` | R1→R2 **15,953** px（8,790 亮 / 7,163 暗），max Δ26 |
| 法线的第二个消费者（specular） | 与第一行同一批 9 材质 | `--world-specular on` | S1→S2 **135** px（123 亮 / 12 暗），max Δ3 |
| `$ssbump` | 36 / 36 / 0 | — | 具名拒绝：bumpBasis 权重不是切线空间法线 |
| `%detailtype` | 键可读：1 个材质（`tf_forest_grass`）；它命名的 15 个 detail VMT 全缺 | — | 具名拒绝：消费者是 VBSP 的编译期，不是运行时着色器 |

确定性（不动任何语义开关，抓两次）：A vs A2 = **0 px / max Δ0**；R1 vs R1b（tick 1200）
= **0 px / max Δ0**。

**counter-only 的反证（B 帧）**：单独打开 `--world-material-bump` 时，45 个声明里 9 个材质
的瓦片被 blit 进图集，`feature-apply bump enabled=1 tiles=9 materials=9 vertices=19305`，
图集绑定、常量上传、顶点位全部就位——画面与基线**逐字节相同（0 px）**。这是本轮最重要
的负读数：lightmap 项与 specular 项都关着，扰动后的法线没有任何消费者，于是「bump 已生效」
这句话在计数器上成立、在像素上不成立。所以本轮的判据是**两条腿**——`shader input` 读数
与**有方向**的像素差必须同时出现；缺一条，B 帧就是它的样子。

方向读数是第二条防线，而且每项的预期方向是先验的，不是事后从像素里读出来的：

* 法线扰动应当同时把像素推向光和背光：C→D 355/269、S1→S2 123/12。只亮或只暗，说明那是
  一个缩放系数，不是被扰动的法线。
* `$detail` 走 mode 0（`base * 2 * detail`），参考纹理的纹素普遍低于 0.5 探针，所以只能
  变暗：82,213/0。
* `$bumpmap2` 的诊断 lerp 在 tick 1200 把 legacy base 法线拉向诊断法线，两个方向都出现：
  8,790 亮 / 7,163 暗。只亮或只暗会说明它只是一个缩放系数；双向是这个臂真的在做 lerp 的
  证据。

**一个无效状态，以及它伪装成特征效果的方式**：第一版把 `featureMask` 的 bit 2 也设在
layer 1 被拒的 4 个 `$ssbump` 材质上。读数看起来很好：`bump2 tiles=5 materials=5
vertices=41118`，R1→R2 38,538 px。但把整个 layer-2 臂删掉、并确认重建后的 exe 里不再有
`bumpAtlas2.Sample` 字符串之后，帧**仍然是** 38,538 px 全暗；把 layer-1 分支的写入改成
`?:` 选择（不再有 predicated write）之后也仍然如此。所以这 38,538 px 不是 layer-2 的 lerp，
也不是某个分支写入泄漏——**是「bit 2 置位、bit 1 未置位」这个顶点状态本身**：着色器没有为它
写任何消费者，画面却变了。本轮没有把这条路径再往下追（需要反汇编/逐条关 `envFeatures.z`、
t9 绑定与顶点位做二分），只把它记录为一个**实测存在、机制未定位**的帧差，并用下面两步修复
让它不可能再发生。
修复分两步：产品路径只在 bit 0 置位时置 bit 2（4 个 `$ssbump` 材质因此具名拒绝 layer 2）；
诊断资格不再借 bit 2，而是放在 `feature.w`（`featureDiag`），由 shader 的 `feature.w` +
`envFeatures.z` 走独立的诊断臂。修复后：删除该臂 = **0 px**（与基线逐字节相同），
R1→R2 = **15,953 px 双向**，m2 = **25,908 px**（R1→M2）。旧的 38,538 读数保留为 superseded，
不进入 pin；这正是「计数器成立而像素不成立」的第二种形状——这次不是没有消费者，而是
消费者读到了一个非法状态。

**一个被这条链揪出来的读数错误**：`%detailtype` 之前读的是 detail 材质的 VMT（`$detail`
命名的那个文件），而这张地图的 15 个 detail VMT 全部缺失，于是读成 0——「读不到」和「没有
声明」在读数上无法区分。按 Valve 的 Detail props 文档，这个键写在**表面材质自己的** VMT 里、
由 VBSP 在编译地图时消费；实测全安装 57 个 VMT 声明它，本图 `NATURE/BLENDGROUNDTOGRASS_ODYSSEY`
写着 `tf_forest_grass`。修正后产品与 probe 都读到 1，账本句从「读不到」变成「读到了，消费者
是编译器，不是着色器」（§3.1）。这条错误的形状和 T24 的悬垂引用一样：一个读数不成立，而它
看起来只是一个 0。

**抓帧的确定性也修了**：`--capture-tick N` 原来只保证「第一个 tick >= N 的绘制」，
绘制节奏（墙钟 120 Hz）与 tick 节奏（demo 的 66.7 Hz）不一致，机器一忙就落在 N+10，
重试同一个数字永远不中。现在光标到 N 后停住直到那一帧画完，本轮 13 次抓帧全部第一次命中
（§7）。

## 2. 环境

| 项 | 值 |
|---|---|
| worktree | `D:/TF2_Native_Worktrees/t27-semantics` |
| branch | `task/t27-material-semantics`（从 `36d68b0` 拉出） |
| base commit | `36d68b0` Record T20 focused regression and keep captures hidden |
| 参考地图 | `maps/pl_odyssey.bsp`（108 个 distinct 材质） |
| 参考 demo | `autorecord_2026-07-02_13-26-46.dem` |
| 抓帧 tick | A/A2/B/C/D/E/F/S1/S2 = 2000；R1/R1b/R2/R3 = 1200（原因见 §5） |
| 诊断 detail 纹理 | `materials/brick/brickwall001.vtf` |
| 诊断 layer-2 法线 | `materials/brick/brickwall001_normal.vtf` |
| 画面 | 1264×681×24bpp，`preset=1`，`vsync=0` |
| 主工作树 | `D:/TF2_Native_Test` — 开发期**未触碰**；合并与收尾验证在其上进行 |
| 本分支提交 | `fa1cf8d` feat(t27) per-material feature atlases… / `ffeffff` evidence(t27) the gate on the final tree… |
| T24 依赖验证 | `lighting-semantics-check.sh --mutation` 在本分支上重跑通过（4 pin 随 `$bumpmap1` 别名更新，见 §3.2）；日志 `evidence/material-semantics/t24-lighting-semantics-dependency.log` |

## 3. 改动文件与理由

新增：

| 文件 | 理由 |
|---|---|
| `native/tools/world_feature_probe.cpp` | 独立可执行：`--self-test`（49 条断言；fixture 用两个不同的 `%detailtype` 值——detail 文件里 `9`、表面材质里 `2`——把键的位置钉住）、`--map`（真实 BSP 上的四组读数）、`--resource ... --stats`（解码单张 VTF 并给出 `meanDeviation` / `strongFraction`）、`--camera <material>`（按材质法线反解相机 pitch/yaw）。产品与 probe 链接同一份 `world_material_features.cpp`，所以「probe 的读数」与「产品的读数」不可能成为两套定义。 |
| `world-material-semantics-check.sh` | 本轮门禁：2 个读数小节 + 13 次 tick 固定的抓帧 + 5 个 mutation。 |
| `evidence/material-semantics/` | 当前树的证据：probe 输出、三份 trace 摘录、`deltas.txt`、`sign.txt`、13 帧 sha256，以及两份完整门禁日志 `gate-forward.log`（163 条断言全过）与 `gate-mutation.log`（5/5 变红）。 |
| `evidence/material-semantics-superseded-invalid-state/` | 被替换掉的 38,538 px 那一轮证据原样归档，`SUPERSEDED.md` 说明它为什么不是 PASS、修正后的 pin 是什么。原样保留、标注，而不是改写成本轮通过。 |

修改：

| 文件 | 理由 |
|---|---|
| `native/include/world_material_features.h`、`native/src/world_material_features.cpp` | 四组纯读数（bump / bump2 / ssbump / detail）＋每组的具名 `reason`。顶部注释重写，说明三个开关打开后**还剩哪些**拒绝——避免「detail 实现了」被读成「detail 键都实现了」。 |
| `native/include/world_tile_atlas.h`、`native/src/world_tile_atlas.cpp` | 图集计划扩展出三个特性层：`bumpBytes` / `bump2Bytes` 按 `featureScale=2` 计价，瓦片矩形与 albedo **共用同一组归一化矩形**，所以一个 uv 能同时寻址 albedo 与三层特性；`WorldTileSlot::bump/bump2` 记录「这个材质的瓦片被 blit 了吗」，顶点位由它决定。 |
| `native/include/native_renderer.h`、`native/src/native_renderer.cpp` | 第二张/第三张特性图集（`t9`/`t10`）、顶点属性 `featureMask` / `featureDiag`（`feature.w`）/ `detailMode` / `detailFactor` / `tangent`、`tangentNormal()`、`combineDetail()`、`$bumpmap2` 的 lerp、specular 读同一个 `n`。产品路径要求 bit 0 才置 bit 2；诊断路径用 `featureDiag` + `envFeatures.z`，在「帧已经算出的 base normal」上量 lerp。normal 的三种候选（per-material / legacy / geometric）用 `?:` 选择而不是分支写入——见 §1 的无效状态。 |
| `native/include/vmt_material.h`、`native/src/vmt_material.cpp` | 解析 `$bumpmap` / `$bumpmap1` 别名优先级、`$bumpmap2`、`$ssbump`、`$detail`、`$detailscale`、`$detailblendmode`、`$detailblendfactor`，以及 `%detailtype`（表面材质自己的键，见 §3.1；非数字的 `$detailblendmode` 记 `malformed`，不猜）。 |
| `native/src/main.cpp` | 五个新参数（§4）、三张图集的上传、`feature-fetch` / `feature-apply` 与六行 `lighting-control` 的装配；`worldBump2Diagnostic` 在 upload 之前传入 renderer（它决定 `featureDiag` 和 `envFeatures.z`）；抓帧 tick 的确定性（§7）。 |
| `native/src/world_lighting.cpp` | 六行账本里三行的句子重写：`bump` 行区分「单槽诊断」与「per-material 图集」两条路径，`$bumpmap2` 行承认「其中 N 个采的是诊断纹理」并点名「其中 4 个没有 layer-1 法线、lerp 从帧已算出的 base 开始」，`%detailtype` 行先给读到的组名、再指名消费者是编译器。 |
| `native/src/bsp_map.cpp`、`native/include/bsp_map.h` | 每个三角形带出 texinfo 的 u/v 轴（切线基的来源）。 |
| `native/CMakeLists.txt` | 新目标 `world_feature_probe`；两个目标都链接 `world_material_features.cpp` 与 `world_tile_atlas.cpp`。 |
| `lighting-semantics-check.sh` | T24 门禁的两个 mutation 锚点随重构移动（`%detailtype` 的解析在 `vmt_material.cpp`，bump2 的状态分支改写）。判据未变。 |

### 3.1 顺带修掉的一个读取错误：`%detailtype` 读错了文件

T19/T24 的实现从 **detail 材质**（`$detail` 命名的那个 VMT）里读 `%detailtype`，于是这张
地图上读到 0。这不是安装缺失造成的：Valve 的 Detail props 文档写的是「材质在自己的 VMT
里写 `%detailtype`，VBSP 在**编译地图时**按它从 `detail.vbsp` 生成 prop_detail
精灵与模型」。T27 用三个读数把这句话落到这台机器上：

| 读数 | 值 | 来源 |
|---|---|---|
| 全安装声明该键的 VMT | **57 / 26,060** | 直接遍历 `tf2_misc_dir.vpk` 的目录树并读每个 VMT 的字节 |
| 本图材质中声明该键的 | **1 / 212**（`NATURE/BLENDGROUNDTOGRASS_ODYSSEY` → `tf_forest_grass`） | BSP 的 texdata 字符串表 ＋ VPK ＋ 地图 pakfile（236 个 VMT） |
| 该材质 `$detail` 指向的 VMT | 缺失 | probe：`15/0/15` 声明/解析/缺失 |
| 世界坐标的该键（map + `detail.vbsp`） | `detail_trainyard.vbsp`（worldspawn） | BSP 实体 lump |

所以「0 可读」是**读错了文件**，不是没有声明。修正后读路径回到表面材质，产品与 probe 的
读数从 `0` 变成 `1`（`feature.detail.typesRead=1`、`firstType=tf_forest_grass`），账本句
也从「读不到」改成「读到了，消费者是编译器」：

```
1 material names a %detailtype group (first=tf_forest_grass); %detailtype names a
detail-prop group that VBSP consumes at map compile time (it emits prop_detail
sprites and models there), so the key never reaches a runtime shader and has no
consumer here
```

probe 的 fixture 用两个不同的值把位置钉住：detail VMT 里写 `"9"`、表面材质里写 `"2"`，
自检要求读到 `"2"`——一个把键读回 detail 文件的改动会立刻变红。这条修复也解释了为什么
「detail VMT 全缺」与「键可读」可以同时成立：它们不是同一个文件。

### 3.2 连带修 pin 的一件事：`$bumpmap1` 别名让 T24 的 bump 读数 +1

本轮给 VMT 解析补了 `$bumpmap1` 别名：`WorldVertexTransition` 的 layer 1 用的就是这个键，
而它与 `$bumpmap` 是同一个采样槽（`$bumpmap` 优先，`$bumpmap1` 兜底）。这张地图上恰好有
一个材质只写 `$bumpmap1`、没写 `$bumpmap`——`GREEK/GREEK_BLEND_COBBLE_FLOOR_01_TO_SAND`，
它同时也是唯一一个非 SSBump 的 `$bumpmap2` 材质（§5）。于是它现在被算作「声明了 `$bumpmap`」，
T24 门禁里四条 pin 随之变化：

| T24 pin | 旧 | 新 | 原因 |
|---|---|---|---|
| `bump declared` / `resolved` | 44 | 45 | 新增的那个材质 |
| `bump.chainMissing` / `pakfileOnly` | 9 | 10 | 它的 VTF 只在地图 pakfile 里 |
| `A bump counts`（declared/visible/resolved） | 44/43/44 | 45/44/45 | 同上 |
| `A bump scope`（materials/triangles） | 43/53,334 | 44/53,462 | 它的 1,128 个三角形 |

这是**读数变得更正确**，不是回归：`$bumpmap1` 是真实的 Source 语法，忽略它等于漏掉一个
声明。按项目规则，pin 跟着读数走、并把原因写在 pin 旁边（`lighting-semantics-check.sh`
里四处注释都点名了这个材质），而不是留着让它红着或者悄悄改数字。T24 门禁在更新后的 pin 上
重跑通过：正向全绿、4 个 mutation 全红、源码逐字节还原（日志在
`evidence/material-semantics/t24-lighting-semantics-dependency.log`）。

## 4. 五个参数与一条 uv

```
--world-material-bump on|off          per-material $bumpmap（图集 + 切线基）
--world-bump2 on|off                  $bumpmap2（需要前一项同时开）
--world-bump2-texture <path>          layer 2 的诊断法线图（替换声明的 VTF，不改权重）
--world-detail on|off                 $detail 层
--world-detail-texture <path>         detail 层的诊断纹理（替换声明的 detail VMT）
```

一次抓帧只加一个参数。三个「层」的开关互相独立，但数值上有依赖，且依赖在 trace 里是
可读的：`$bumpmap2` 是 lerp 到第 1 层法线上的，第 1 层没画时它没有可以混合的目标，
所以 `--world-bump2 on` 单独打开时 `feature-fetch bump2Switch=1` 而 `bump2 tiles=0`，
账本句写的正是这个原因，而不是「资源缺失」（5 个声明的 VTF 全部解析成功）。

一张 uv 同时寻址四条层：特性图集与 albedo 图集共用瓦片矩形（`rect.*` 自检钉住
`256×256`、中心 `0.562500`、相邻瓦片 `257 <= 257` 不重叠），区别只在分辨率——
`featureScale=2` 意味着每个特性层是 albedo 的 1/4 面积。这既是显存账本（§6），也是
「采样位置正确」这句话能被一个 uv 检查的原因。

## 5. `$bumpmap2`：机制用诊断 base 量，声明资源在本 demo 下 NOT VERIFIED

`$bumpmap2` 有 5 个声明、全部解析成功（`5/5/0`）。按**能不能消费**分，它们不是一类东西：

* **4 个同时声明 `$ssbump`**：它们的 `$bumpmap` 是 bumpBasis 权重，layer 1 已经按名拒绝；
  Source 的 `BUMPMAP2` 块编译在 `BUMPMAP` 之下，layer 2 的 lerp 需要一个 layer-1 法线作为
  起点，所以产品路径也拒绝它们的 layer 2。不是资源缺失（5/5 都解析），是**没有 base**。
* **1 个不是**（`GREEK/GREEK_BLEND_COBBLE_FLOOR_01_TO_SAND`）：它的 `$bumpmap` 是
  `greek_cobble_floor01_ssbump.vtf`，`$bumpmap2` 指向
  `materials/nature/dirtground001_height-ssbump.vtf`，128 个三角形（384 顶点）。probe 解码
  该纹理：`meanDeviation=46.2`、`strongFraction=1.000000`（1024²）——它**不是**平法线图。

**R3 是产品路径的诚实读数**：`5/1/0` 声明/解析/缺失、`ssBump=4`、
`tiles=1 materials=1 vertices=384`，R1→R3 **0 px**。唯一被画的 layer-2 材质不是没有资源、
也不是没有权重（`weights=1(1.000..1.000)`），而是**不在屏幕上**：tick 1200、tick 2000、以及
probe 用自己的 `--camera` 反解出的材质相机都是 0 px。所以「声明资源在像素上成立」这句话
本轮**不能**说；它是一个具名 `NOT VERIFIED`，不是被 0 px 洗掉的失败。

**R2 是 shader 臂的证明，base 是诊断的**：加 `--world-bump2-texture` 后，5 个材质的 layer 2
纹素换成诊断法线；4 个没有 layer-1 法线的材质用 `featureDiag`（`feature.w`）标记，shader 把
它们 lerp 到**帧已经算出的 legacy base 法线**上。权重仍是材质自己的顶点 alpha
（`weights=9(0.000..1.000)`）：`bump2Forced=5`、`tiles=5 materials=5 vertices=41118`，
R1→R2 **15,953 px（8,790 亮 / 7,163 暗）**，max Δ26。**删除整个 layer-2 臂后 R1 vs X = 0 px**
（exe 字符串确认 `bumpAtlas2.Sample` 不在重建产物里），说明这 15,953 px 全部来自该臂；
`m2`（layer 2 改采 layer 1 的图集）把 R1→M2 变成 **25,908 px**，说明采到的纹素确实参与
了结果。这是本轮 `$bumpmap2` 的 shader input→pixel delta 证明。

结论必须分成三句，而不是一句：

1. **layer 2 的 lerp 到达像素**（R2，15,953 px 双向；删除 0；m2 R1→M2 25,908）；
2. **这 4 个材质的 base 不是 Source 的 bumpBasis**，而是 legacy 法线（`feature.w` +
   `envFeatures.z` 的诊断臂）；
3. **声明的 layer-2 资源在本 demo 的相机下没有可见像素**：4 个没有 layer-1 base，1 个被遮挡
   （R3 = 0），所以声明资源的像素链是 `NOT VERIFIED`，不是 PASS。

账本句在 forced 时同时写两件事：「5 个采的是 `--world-bump2-texture` 诊断纹理，不是它们
`$bumpmap2` 命名的 VTF」和「4 个设置 `$ssbump`，layer-1 法线被拒，lerp 从帧已算出的 base
开始——诊断量的是 shader 臂，不是 Source 的 bumpBasis」。两句话都在 gate 里被 pin 成文本。

**旧 38,538 px 读数为什么被替换**：第一版把 bit 2 也设在 layer 1 被拒的 4 个材质上。读数
看起来是 `tiles=5 materials=5 vertices=41118`，帧差 38,538 px 全暗；但删除 layer-2 臂后
仍然是 38,538 px，把 layer-1 的写入改成 `?:` 选择之后**仍然**是。所以它不是 layer 2，
也不是某一个分支写入——是「bit 2 置位、bit 1 未置位」这个顶点状态本身。本轮没有继续定位
机制（需要对 `envFeatures.z`、t9 绑定、顶点位做二分，或反汇编），只把它记为**实测存在、
机制未定位**的帧差，并让这个状态不可能再产生：bit 2 只在 bit 0 置位时出现，诊断资格改走
`feature.w`。修复后删除臂 = 0 px，真实臂 = 15,953 px。旧读数保留在本报告、gate 注释与
`evidence/material-semantics-superseded-invalid-state/` 里，作为 superseded，不作为 pin。

## 6. 显存账本

特性层不是免费的，而「加了三个层」这句话必须带上数字才可评审：

| 量 | 读数 |
|---|---|
| albedo + blend 图集 | `atlas size=4112 bytes=67634176 atlases=2 totalBytes=94053776 state=bound` |
| 计划层字节（bump 开） | `layerBytes=252835572`，`layers=89+64+64+22+0 MiB` |
| 计划层字节（bump + bump2 诊断开，R2 帧） | `layerBytes=269744116`，`layers=89+64+64+22+16 MiB` |
| 计划层字节（detail 开、bump 关） | `layerBytes=229322128`，`layers=89+64+64+0+0 MiB` |
| 全分辨率特性层的代价（反事实） | 三个满分辨率层 = **+282 MiB**，会把计划推过 384 MiB 预算 |
| `featureScale=2` 后 | 每个特性层 1/4 面积：bump 层 22 MiB（满分辨率是它的 4 倍），三层合计 **≈70 MiB** |

`layerBytes` 的定义是 `albedo + blend + modulate + bump + bump2`，所以上面两行的差（22 MiB）
就是 bump 层；`featureScale=2` 是让这笔账可付的原因，也是「法线细节不与源纹理逐纹素等价」
这句边界的技术来源。

**一个读数缺口，写在这里而不是补一个数字**：detail 图集由渲染器单独创建
（`worldDetailAtlasTextures_`），不在 `WorldAtlasPlan` 的计价范围内，所以它**不进入**
`layerBytes`，也不进入 `layers=`。也就是说这张表里的 70 MiB 只含两个法线层。缺口是
读数的缺口，不是显存的缺口；未测量就不写数。

## 7. 门禁

`bash world-material-semantics-check.sh [--mutation]`

正向：**163 条断言全过，0 失败**（含 49 条 probe 自检、真实地图读数、13 次抓帧的状态
与像素 pin、4 组方向读数）。13 次抓帧**全部第一次命中**目标 tick（`gate-forward.log` 里
`attempt` 出现 0 次）——`--capture-tick` 现在把光标停在目标 tick 上等那一帧画完。
`frames.txt` 还给出一个免费读数：`R3 == R1 == R1b` 逐字节相同，即「layer 2 开着但采不到
任何纹素」与「layer 2 关着」在这台机器上是同一帧。负向：5 个源码变异各自必须把某条正向 pin 变红，
并且源码逐字节还原：

| mutation | 注入的缺陷 | 变红的表现 |
|---|---|---|
| m1 | `tangentNormal()` 丢弃解码结果，直接返回几何法线 | C→D 从 624 px 变成 **0** px，而账本仍读 `applied` |
| m2 | layer 2 采 layer 1 的图集（`bumpAtlas2` → `bumpAtlas`） | R1→M2 从 15,953 px 变成 **25,908** px，`bump2Forced` 仍为 5 |
| m3 | mode 0 按 mode 3 画（`base*2*d` 变成 `lerp(base,d)`） | A→M3 从 82,213 px 变成 **78,791** px，直方图仍读 `0x15` |
| m4 | 顶点位取自 VMT 声明而不是 blit（45 个材质都置位） | C→M4 从 624 px 变成 **416,188** px，而 `feature-apply bump tiles=9 materials=45 vertices=187437`：9 个瓦片、45 个材质、187,437 个顶点，其中 36 个材质采的是空瓦片 |
| m5 | 特性瓦片矩形按全分辨率算（忽略 `featureScale=2`） | probe 的 `rect.size` 从 256×256 变成 **512×256** |

**变异判词：5/5 全部变红，源码逐字节还原（`gate-mutation.log`）。** 五个注入里最值得记的是 m4：
计数器这一侧现在跟着**顶点位**走（`materials` 是带着 bit 的材质数，不是 blit 数），所以 m4 的读数是
`bump tiles=9 materials=45 vertices=187437`——9 个瓦片被 blit，45 个材质声称采样，36 个采的是空瓦片。
这正是 counter-only 的形状：每个数字都是真的，合起来是错的；只有帧差（624 → 416,188）能暴露它。
m2 的 25,908 px 与正向 15,953 px 不同，说明改采 layer 1 的图集确实改变了结果，而不是碰巧不动。

**抓帧的确定性是产品行为，不是脚本重试。** 原实现里 `--capture-tick N` 的含义是
「第一个 tick >= N 的绘制」，而绘制节奏是墙钟（120 Hz 目标）、tick 节奏是 demo 的
`tickRate`（≈66.7 Hz），两者读同一个 `elapsed`——机器一忙，一次绘制跨 10 个 tick，
抓帧就落在 N+10，重试同一个数字永远不中（实测 target 2000 连续落在 2001/2002/2010）。
本轮把语义收紧为「**在 N 上绘制**」：光标到达 N 后停住直到那一帧画完（一帧的停顿，
抓帧随即解除武装），于是 `capture` 的第一次尝试就命中，pinned 像素差才有意义。

## 8. 边界：本轮没有声称什么

* **没有声称 `$ssbump` 被渲染。** 36/45 个 `$bumpmap` 是 SSBump，本轮仍然是具名拒绝：
  `BUMPMAP == 2` 下纹素不是法线而是 bumpBasis 权重，需要三张 lightmap 加权，本 pass
  没有那三张图。账本句给的是减法（`36 of 45`），不是形容词。
* **没有声称 `%detailtype` 被实现。** 它是 VBSP 的编译期输入（detail prop 组名），不是
  运行时着色器输入；本 pass 读到了它（1 个材质），但没有消费者可以给它。句子里写的是
  「读到了，消费者是编译器」，而不是「缺失」——这两句话在 T27 之前是一样的 0。
* **`$detail` 的链路是用诊断纹理证明的。** 本安装里 15 个被引用的 detail VMT 全部缺失
  （`detailResolved=0`），所以 A→E 是 0 px；诊断纹理把「着色器腿」单独量出来（A→F），
  这不等于「地图作者写的那 15 个 detail 材质画对了」。
* **`$bumpmap2` 的 15,953 px 来自诊断纹理 + 诊断 base。** 4 个材质没有 layer-1 法线，
  诊断臂把 layer 2 lerp 到 legacy base 上；所以量到的是「法线进 lerp 会不会改变像素」，
  不是「Source 的 BUMPMAP2 在这 4 个材质上画对了」（那需要 bumpBasis 与三张 lightmap）。
  R3 的 0 px 是第 5 个材质（非 SSBump、纹理很强、三个相机下都被遮挡）。**声明资源的
  layer-2 像素链是 NOT VERIFIED**，不是被诊断读数替代成 PASS。
* **没有把「bit 2 置位、bit 1 未置位」读成特征生效。** 这个非法状态在本轮第一版里确实
  产生了 38,538 px 全暗帧差，删掉 layer-2 臂后仍在；它被保留为 superseded 读数，
  修复后的 pin 是 15,953 px 双向、删除 0 px。
* **没有声称法线贴图在视觉上「正确」**：钉住的是位置（同一 uv、同一矩形、同一权重）、
  方向（双向/单向符合先验）与确定性（两次抓帧逐字节相同）。观感验收不在本轮。
