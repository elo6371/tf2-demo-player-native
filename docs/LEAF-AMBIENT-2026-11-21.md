# BSP 叶子环境立方体（task-37 Half A，2026-11-21）

日期 2026-11-21 · 分支 `task/leaf37-ambient` · 基线 `82901898`（lighting: close the three world-lightmap atlas tails）

这张卡只做 GPU 侧之前的半边：解析 `LUMP_LEAF_AMBIENT_INDEX`（52，HDR 51）与 `LUMP_LEAF_AMBIENT_LIGHTING`（56，HDR 55），
给世界坐标一个「这个叶子的六向环境色是多少」的接口，并且在**答不出来的时候明确说不**。
把立方体绑进逐实例上传、让 shader 读它，是 task-36 那半边，不在本分支。
写这份文档的原因是：这些 lump 在本仓此前一行都没解析过
（`docs/HANDOVER-2026-11-21.md:261` 记着「全树没有 LUMP_LEAF_AMBIENT_INDEX(52) / LUMP_LEAF_AMBIENT_LIGHTING(56) 解析」），
格式、页面选择、六向顺序、量级四件事都必须先有实测数字，后半边才不会照着猜测接线。

## 0. 结论

1. **格式是「逐叶子索引 + 28 字节样本」。** 索引 lump 长度恒等于 `叶子数 * 4`，每条 4 字节是 `(uint16 sampleCount, uint16 firstSample)`；
   数据 lump 每 28 字节一条：6 个 `ColorRGBExp32`（各 4 字节 = 3 个无符号尾数 + 1 个有符号指数）
   再接样本位置 `x,y,z` 三个字节和 1 个填充字节。全语料 261 张图 `outOfRange` 全为 0、
   `数据字节 % 28 == 0`、`索引字节 == 叶子数*4` 三条同时成立才设 `BspLeafAmbientMode::RgbExp32`（`native/src/bsp_map.cpp:436-480`）。
2. **「没有立方体」是常态，不是异常。** 260 张带这个 lump 的图合计 1629178 张叶子，其中
   `leavesWith=934084 / leavesWithout=695094`，也就是 **42.7% 的叶子一个样本都没有**；
   整语料叶子总数是 1629874，差的 696 张属于那张两个 lump 都不带的 `cp_cloak.bsp`
   （它没有样本可数，两个计数都不会因为它的叶子加一，所以两个分母不是同一个）。
   所以接口返回 bool 而不是零值：零值立方体在渲染上表达的是「这个模型在黑暗里」，
   而事实是「这个叶子没有样本」。
3. **页面规则跟 brief 反着来，且是有意的。** brief 说「有 HDR 页就优先 HDR」，本仓
   `native/src/bsp_map.cpp:322`（`const Bytes& lightPage = lighting.empty() ? lightingHdr : lighting;`）
   与 `:327`（`const bool hdrLumpUsable = hdrDecoded && lightingHdr.size() == lightPage.size();`）
   ——task-38 落的光贴图页选择——是「**LDR 在就用 LDR**，HDR 只作为只有 HDR 的图的兜底」。
   两条光照路径若选到相反页，同一叶子会从两份不同的烘培里取色，所以这里跟仓里的先例走
   （`native/src/bsp_map.cpp:419-431`），并在本机语料上确认这个兜底分支**不可达**：`hdrOnly=0 / 261`。
4. **两页不是同一份数据，所以页面选择是真决定。** 逐叶比对
   （`evidence/leaf-ambient/tools/page_diff.py`，原始输出 `evidence/leaf-ambient/page-diff.txt`，
   解码后按 Rec.709 亮度比）：cp_process_f12 相同 885 叶 / 不同 **2589** 叶 / 样本数不一致 63 叶，
   maxAbsLumDiff 0.002912062；cp_badlands 794 / **2430** / 62，0.000765048；
   2koth_abbey 4303 / 208 / 3，0.004033659；koth_bagel_rc12 5930 / 325 / 1，1.9751e-05。
   没有一张图是「某叶只在一页里」（`onlyLdr=onlyHdr=0`），但多数叶在两页里颜色不同。
   `maxAbsLumDiff` 是**整次比较**里的最大值，所以每张图各跑一次脚本、只取它那一行——
   四个文件混在一次调用里跑，后面几行会继承前面那张图的最大值。
5. **解码出来的数是线性辐射度，量级 ~1e-3，不是 0..1 颜色。** 同一张图上光贴图页与叶子环境页的
   最大解码值（`evidence/leaf-ambient/tools/page_magnitude.py`，原始输出
   `evidence/leaf-ambient/page-magnitude.txt`）：cp_process_f12 7.686275 对 **0.004626**，
   cp_badlands 5.176471 对 **0.002344**，2koth_abbey 227.890196 对 **0.010049**，
   koth_bagel_rc12 2.854902 对 **0.002068**。公式是同一个（`scale = ldexp(1.0f, exponent) / 255.0f`），
   含义不是同一个：后半边必须自己乘系数，直接当 0..1 用会让环境项小三个数量级、等于没接。
6. **六向顺序 `(+X, -X, +Y, -Y, +Z, -Z)` 是推断，证据是向上的偏置。** SDK 的
   `mathlib/compressed_light_cube.h:17-21` 只声明 `ColorRGBExp32 m_Color[6]`，**没有说是哪六个方向**。
   本机 261 张图 7,868,947 个立方体的统计：最亮角是 +Z 的占 3,279,485 个（41.7%），
   −Z 平均亮度最低（0.000358352 对 +Z 的 0.000674501）。室内图天花板比地板亮，方向对的话
   就该这样，方向错的话两个数会对调。这是很强的旁证，不是证明；头文件里也是这么写的
   （`native/include/bsp_map.h:107-119`）。
7. **真 demo 上能落地。** cp_process_f12 的 STV demo、4 个 tick、每 tick 前 24 个实体：
   有世界坐标的 96 个实体全部解到叶子，`leafAmbientLookups hit=76 missed=20`；20 次 miss 全部是
   `CWorld` / `CMannVsMachineStats` / `CVoteController` 这类位于原点 `[0,0,0]` 的实体
   （落在 leaf 1843、cluster −1、一个全黑样本），不是解析失败。
8. **9 张图带「没有叶子认领的样本」**，合计 197554 条（pl_patagonia 69902 最多）。
   解析器把这批样本计数（`leafAmbientSamplesUnreferenced`）而不是静默丢弃——
   这是全语料上唯一一处与「索引正好覆盖全部数据」假设不符的地方。

## 1. 改动

### 1.1 数据与解析（`native/src/bsp_map.cpp`、`native/include/bsp_map.h`）

- `native/src/bsp_map.cpp:34-35`：四个 lump 常量 `kLumpLeafAmbientIndexHdr = 51`、
  `kLumpLeafAmbientIndex = 52`、`kLumpLeafAmbientLightingHdr = 55`、`kLumpLeafAmbientLighting = 56`。
  此前仓里只用到 53。
- `native/src/bsp_map.cpp:255`：`decodeColorRgbExp32(const std::uint8_t* sample, float rgb[3])`——
  把 ColorRGBExp32 解成线性 float 收成**唯一一份**。此前仓里有两处同样的写法（`writeLuxel` 与 T17 的
  HDR 色调通道），本任务不再写第三份；现在三个调用点：`:264`（writeLuxel）、`:472`（叶子环境）、
  `:1342`（HDR 光贴图色调通道）。
- `native/src/bsp_map.cpp:349-352`：四个 lump 走**软解码**（`tryDecode`），跟 entities/planes/leafs 一样：
  缺 lump 不是解析失败。这很关键——语料里真有图不带（`cp_cloak.bsp`）。
- `native/src/bsp_map.cpp:418-481`：解析块。页面选择 `:428-431`；门 `:436-437`（索引字节 == 叶子数*4
  且 数据字节 % 28 == 0，两者同时成立才认这份数据）；逐叶读数 `:444-445`；越界钳制并计数 `:449-455`；
  `leavesWith/Without` `:457-464`；无人认领样本计数 `:466`；样本解码 `:468-478`（六个方向各 4 字节，
  位置在 `+24/+25/+26`）。
- `native/include/bsp_map.h:80`：`enum class BspLeafAmbientMode { Unavailable, RgbExp32 };`。
  `:85-96` `BspLeaf` 补了 `short mins[3] / maxs[3]`（lump 10 的 `+8` 与 `+14`，此前只读 cluster
  与面范围）——样本位置是**叶包围盒内 0..255 的 8 位定点分数**（除以 255 得 0..1），
  没有包围盒就换算不成世界坐标。
  `:102-105` `BspLeafAmbientEntry`、`:120-123` `BspLeafAmbientSample`、`:130-138` `BspLeafAmbientCube`。
- `native/include/bsp_map.h:506-532`：`BspMap` 新增 `leafAmbientMode`、`leafAmbientHdrPage`、
  `leafAmbientEntries`、`leafAmbientSamples`、`leavesWithAmbient`、`leavesWithoutAmbient`、
  `leafAmbientRecordsOutOfRange`、`leafAmbientSamplesUnreferenced`、`ambientLookupsHit`、`ambientLookupsMissed`。

### 1.2 查询接口（Half B 的入口）

- `native/src/bsp_map.cpp:1937` `BspParser::leafAt(const BspMap&, float, float, float) -> int`——
  `clusterAt()` 的同构兄弟：同一套守卫（空树 / 非有限坐标 / 平面越界）、同一条 4096 步上限、
  同一遍走树，只是把「返回叶子的 cluster」换成「返回叶子编号」，解不出来返回 −1。
  两者都存在而不是互相推导，是因为 ambient 索引是**逐叶子**的、PVS 是**逐 cluster** 的，
  互相推导正是它们会走偏的方式（`native/include/bsp_map.h:552-558`）。
- `native/src/bsp_map.cpp:2018` `BspParser::leafAmbientAtLeaf(BspMap&, int leafIndex, BspLeafAmbientCube& out) -> bool`——
  已知叶子编号时的入口（静态道具在 lump 35 里存的正是叶子编号）。一个叶子有多个样本时，
  取**离该叶自身包围盒中心最近**的那个。
- `native/src/bsp_map.cpp:2029` `BspParser::leafAmbientAt(BspMap&, float x, float y, float z, BspLeafAmbientCube& out) -> bool`——
  世界坐标入口：`leafAt()` 之后走**同一个私有选择器**（`native/src/bsp_map.cpp:1967` `pickLeafAmbientCube`），
  但目标点换成调用者的点。一个选择器两个靶点，是两条重载不会对同一叶子给出不同立方体的原因。
- `native/src/bsp_map.cpp:2043` `leafAmbientModeName` 返回 `"source-equivalent"` 或 `"unavailable"`。

接口形态（Half B 直接照这个接，不碰 D3D、不 include 渲染器）：

```cpp
tf2::native::BspLeafAmbientCube cube;
if (tf2::native::BspParser::leafAmbientAt(map, x, y, z, cube)) {
  // cube.color[0..5] = (+X, -X, +Y, -Y, +Z, -Z) 的线性 RGB
  // 另有 leafIndex / cluster / sampleCount / sampleIndex / position
} else {
  // 回落：map.ambientLookupsMissed 已经自增。不要填零立方体。
}
```

`out` 在返回 false 时**原样不动**（不是清零），自检 `empty.missLeavesCallerValue` 钉的就是这条：
调用方预填的哨兵值 `12345` 在 miss 之后还在（`evidence/leaf-ambient/self-test.txt:18`）。

## 2. lump 里到底有什么（全语料，不是假设）

261 张 `.bsp`（`D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/maps`）逐张跑
`leaf_ambient_probe --corpus`，原始输出 `evidence/leaf-ambient/corpus-261-maps.txt`：

```json
{"summary":"corpus","maps":261,"usable":261,"ldr":260,"hdrOnly":0,"none":1,
 "unreadable":0,"unparsed":0,"leaves":1629874,"leavesWith":934084,
 "leavesWithout":695094,"samples":7868947,"outOfRange":0,"unreferenced":197554}
```

- 260 张走 LDR 页，0 张只有 HDR，1 张两 lump 都不带（`cp_cloak.bsp`，696 叶，
  `evidence/leaf-ambient/real-cp_cloak-no-lumps.txt` 是它的阴性对照）。
- `outOfRange=0`：全语料没有一个索引记录读到自己样本之外——逐叶 `(count, first)` 的布局在整个语料上成立。
- 每叶样本数上限 16。**最常见的不是满 16 个**：`evidence/leaf-ambient/samples-per-leaf.txt` 的直方图里
  cp_process_f12 单样本 1573 叶、16 样本 1392 叶，koth_bagel_rc12 单样本 3798 对 16 样本 1660，
  只有 2koth_abbey 反过来（16 样本 2156 > 单样本 1945）。「一个叶子一个立方体」是主力形态，
  4×4×4 只在一部分图上占多数——所以选择器是「离目标最近的那一个」而不是「取平均」：
  单样本的叶子上两者等价，16 个样本的叶子上才分得开。
- 带 lump 的 260 张图里 695094 / 1629178 的叶子没有样本（42.7%；不含既无索引也无数据的
  `cp_cloak.bsp`——它没有样本可数，加上它的 696 叶会把分母算大）。这既是「unavailable
  要走第一类路径」的理由，也是任何光照对比必须同时报 hit 与 missed 的理由。

## 3. 六向颜色长什么样（数字）

### 3.1 真实地图 + 叶子

cp_process_f12（`evidence/leaf-ambient/real-cp_process_f12.txt`），leaf 139、cluster 71、16 个样本，
按叶子中心选中的是第 14 个：

```
+X 0.00153186, 0.00196078, 0.00459559
-X 0.00111826, 0.00139400, 0.00323223
+Y 0.00153186, 0.00193015, 0.00453431
-Y 0.00105699, 0.00130208, 0.00294118
+Z 0.00153186, 0.00196078, 0.00459559
-Z 0.00087316, 0.00110294, 0.00257353
```

cp_badlands 最小的一例（leaf 5515、单样本）：最大分量 0.000181909、最小 1.81909e-05。
同一张图上光贴图页的解码值最大到 5.176471，叶子环境页最大 0.002344——三个数量级的差别，
这就是「必须缩放」的实测依据，不是修辞。

### 3.2 真实 demo + 真实 tick

cp_process_f12 STV demo，`--ticks 48850,49500,51000,54000 --entities 24`（原始输出
`evidence/leaf-ambient/demo-cp_process_f12.txt`）：

- `{"reading":"demoEntities","withOrigin":96,"withoutOrigin":72,"partialOrigin":0,"outsideTree":0}`
- `{"reading":"leafAmbientLookups","hit":76,"missed":20}`
- tick 48850 的实体历史回溯到 48845（demo tick 与 server tick 不同，打印 resolved 就是为了不藏这件事）。

一个玩家实体（entity 1，CTFPlayer，原点 `[660,1469,755]`）解到 leaf 1687 / cluster 918 /
16 个样本、选中第 14 个：

```json
{"entity":1,"class":"CTFPlayer","origin":[660,1469,755],"complete":1,"leaf":1687,
 "cluster":918,"hit":1,"samples":16,"picked":14,"cube":"0.00126379,0.00113358,0.00120251|
 0.0005553,0.000471048,0.000467218| ... |0.000434666,0.000371477,0.000346584"}
```

20 次 miss 的每一次都是同一类：`{"entity":0,"class":"CWorld","origin":[0,0,0],"leaf":1843,"cluster":-1,"hit":0,"samples":0,"picked":-1}`
——原点落在 cluster −1 的叶子，那个叶子只有一个全黑样本。也就是说 miss 的来源是
**世界 / 索引类实体站在地图原点**，不是叶查找失败。这条区别很重要：把 miss 当「查找坏了」
和当「实体不在光照里」是两种完全不同的结论。

## 4. 两页不一致（所以页面规则是决定，不是形式）

LDR 页（52/56）与 HDR 页（51/55）在同一次烘培里**不是同一份数据**。逐叶逐方向比对：

| 地图 | 叶子总数 | 带样本叶子 | 两页相同 | 两页不同 | 样本数不一致 | maxAbsLumDiff | meanRelDiff | 比较对数 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| cp_process_f12 | 5654 | 3537 | 885 | 2589 | 63 | 0.002912062 | 0.065174 | 40522 |
| cp_badlands | 5516 | 3286 | 794 | 2430 | 62 | 0.000765048 | 0.054747 | 43369 |
| 2koth_abbey | 7375 | 4514 | 4303 | 208 | 3 | 0.004033659 | 0.307518 | 3751 |
| koth_bagel_rc12 | 9472 | 6256 | 5930 | 325 | 1 | 1.9751e-05 | 0.005916 | 872 |

两列叶数是两个独立读者给的：`叶子总数` 取自 `evidence/leaf-ambient/samples-per-leaf.txt` 的 `leaves`，
`带样本叶子` 是 `page-diff.txt` 里 `两页相同 + 两页不同 + 样本数不一致` 三项之和，四张图分别是
3537 / 3286 / 4514 / 6256，与 `leaf_ambient_probe --map` 报的 `leavesWith` 逐个相等——
这也是两个独立实现互相确认的一次。
（上一版的表格把「叶子总数」和「带样本叶子」混在同一列：前两行填的是总数、后两行填的是带样本数，
而且 2koth_abbey 那格还多算了；这一版全部按提交的证据重算。）

没有任何叶子只存在于一页（`onlyLdr=0 onlyHdr=0`）。结论是：选页会改变多数叶子的颜色，
所以「按先例选 LDR」必须被写下来，而不是当成实现细节。

## 5. 回落计数（光照结论必须引用的那个数）

- 语料级：`leavesWithout=695094`（42.7% 的叶子没有样本）、`none=1`（1 张图完全没有这两个 lump）。
- 解析级：`outOfRange=0`（布局没有一次越界）、`unreferenced=197554`（9 张图带多出来的样本：
  pl_patagonia 69902、cp_premuda 35548、pl_redwood 33965、pl_citadel 21310、pl_phoenix 16907、
  mge_chillypunch_final4_fix2 7570、pl_odyssey 5121、pl_corruption 4736、pl_divulgence_b4b 2495）。
- 查询级：`ambientLookupsHit` / `ambientLookupsMissed`。**只有世界坐标入口**
  `BspParser::leafAmbientAt`（`native/src/bsp_map.cpp:2029`，自增在 `:2033` 与 `:2039`）计数，
  成功与失败都记；`leafAmbientAtLeaf`（`:2018`）**不计数**——它拿的是叶子编号，
  没有「点解不到叶子」这件事可记，把它的调用记进 miss 会让分母失去意义。
  头文件 `native/include/bsp_map.h:567-573` 一直就是这么写的；本节此前写成「两个公开重载每次调用都计数」，
  是文档错了，代码没错。

## 6. 自检（合成 fixture，不依赖任何地图）

`leaf_ambient_probe --self-test` 输出 28 条 `{"check":...}`、末行 `{"summary":"self-test","failures":0}`、退出码 0。
原始输出 `evidence/leaf-ambient/self-test.txt`。覆盖：

- 布局与计数：`fixture.parsed` / `fixture.mode` / `fixture.entries` / `fixture.samples` / `fixture.leavesWith` / `fixture.noOutOfRange`。
- 叶查找：`leafAt.right` / `leafAt.left` / `leafAt.agreesWithClusterAt`（与 `clusterAt` 在同一棵树上
  必须给出一致答案）/ `leafAt.refuses`（NaN 不许解出叶子）。
- 立方体选取：`cube.atLeaf` / `cube.atLeafPicksCentre` / `cube.atCornerPicksCorner`（同一个叶子，
  按叶子中心问取第 2 个样本、按角落问取第 0 个——两条重载靶点不同是可观测的）/ `cube.position`。
- 空叶子与计数：`empty.oneLeafWithoutCube` / `empty.missReturnsFalse` / `empty.missLeavesCallerValue` / 
  `empty.counters` / `empty.countersBothWays`。
- 页面规则：`page.ldrOnly` / `page.hdrOnly` / `page.ldrWins`（两页都在时必须读 LDR 环境页，
  与 `world_material_probe` 对光贴图页的钉法同形）/ `page.none` / `page.noneAccessorRefuses`。
- 坏输入：`page.shortIndexRefused`（索引短于 叶子数*4 → 整份拒绝，不是读一部分）/ 
  `page.raggedDataRefused`（数据字节非 28 的倍数 → 拒绝）/ `page.overrunClampedAndCounted`（越界样本被钳制**并且**计数）。

`BspFixture` 为此扩了字段（`native/include/world_material.h:842-861`）：叶子的 `mins/maxs`、四条页向量，
加上 `appendAmbientSample` / `appendAmbientEntry` 两个打包助手（`native/src/world_material.cpp:1761` / `:1780`），
lump 表补了 51/52/55/56（`native/src/world_material.cpp:1713-1725`）。

## 7. 对读记录（外部源码只解释语义，不抄一行）

```
库 / 路径 / 文件:行号  ->  本仓文件:行号

ValveSoftware/source-sdk-2013  src/public/bspfile.h:343-350
  LUMP_LEAF_AMBIENT_INDEX_HDR=51 / INDEX=52 / LIGHTING_HDR=55 / LIGHTING=56
  -> native/src/bsp_map.cpp:34-35

bspfile.h:366  LUMP_LEAF_AMBIENT_LIGHTING_VERSION = 1
  -> native/src/bsp_map.cpp:436-437（本仓不靠版本号判页，靠两条长度门）

bspfile.h:857-868  x,y,z 是叶包围盒内 0..255 的 8 位定点分数；
  dleafambientlighting_t = CompressedLightCube + byte x,y,z,pad
  -> native/include/bsp_map.h:120-123 与 native/src/bsp_map.cpp:468-478

bspfile.h:870-876  dleafambientindex_t { unsigned short ambientSampleCount;
  unsigned short firstAmbientSample; }
  -> native/include/bsp_map.h:102-105（确认没有 leaf 字段：索引是逐叶数组，按叶号寻址）

src/public/mathlib/compressed_light_cube.h:17-21
  CompressedLightCube { ColorRGBExp32 m_Color[6]; }
  -> 只证实「六个 ColorRGBExp32」。这六个方向在 SDK 里没有名字，
     native/include/bsp_map.h:107-119 自己声明顺序并给出语料旁证。
```

运行时那条「六向按表面法线加权混合」的公式**未能对读外部源码**：
`src/game/client/c_bsp.cpp` 与各 `lightcubemap.h` 路径均 404，且本机联网搜索返回
`HTTP 402 Insufficient Balance`。所以本仓的单向解码只保证「每个方向是什么」，
不主张引擎的混合权重。Half B 若按法线混合，那份权重得自己定并单独测。

## 8. 没做、和 Half B 必须知道的

1. **不缩放就等于没接。** 解码值是线性辐射度、量级 1e-3（见 §0.5）。系数要自己定，
   并且要在抓帧上验证，不能只写在 shader 常量里。
2. **miss 必须回落到原来那条路径。** 42.7% 的叶子、语料 1 张图、demo 上 20/96 次查询都会 miss。
   填零会把这些模型压暗，而「压暗」在画面上看起来像光照 bug，不像数据缺失。
3. **六向顺序是推断（见 §0.6）。** 若在画面上看到「天花板比地板暗」，第一步该怀疑这六个方向，
   而不是系数。`--map` / `--corpus` 的 `directionStats` 行就是为排查这件事留的。
4. **页面规则跟 brief 反着来（见 §0.3）。** 若另一半或其它光照路径按「HDR 优先」读，
   同一叶子会取到另一份烘培的颜色；本机语料上 HDR-only 图是 0 张，所以现在看不出来。
5. **本卡不做 GPU。** 逐实例上传、shader 采样、法线混合权重都不在本分支；
   `leaf_ambient_probe` 一行 GPU 代码都没有，也不写文件。

## 9. 复现

```sh
native/build-nmake/leaf_ambient_probe.exe --self-test
native/build-nmake/leaf_ambient_probe.exe --map <map.bsp> --leaves 4
native/build-nmake/leaf_ambient_probe.exe --corpus 'D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/maps'
native/build-nmake/leaf_ambient_probe.exe --demo <demo.dem> --ticks 48850,49500,51000,54000 --entities 24
```

四条命令的输出分别落在 `evidence/leaf-ambient/` 下的 `self-test.txt`、
`real-cp_process_f12.txt`、`real-cp_badlands.txt`、`real-cp_cloak-no-lumps.txt`、
`corpus-261-maps.txt`、`demo-cp_process_f12.txt`，都是原样 stdout、没有手工改动；
每行说明见 `evidence/leaf-ambient/README.md`。
