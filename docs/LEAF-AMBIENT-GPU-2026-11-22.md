# BSP 叶子环境立方体，Half B：GPU 逐实例接线（task-37）

日期 2026-11-22 · 分支 `task/leaf37b-gpu` · 前置：`docs/LEAF-AMBIENT-2026-11-21.md`
（Half A：lump 解析、六向颜色、两页不一致、回落计数、自检 fixture）

本文件只讲 Half B 做的事：把 Half A 离线量出来的每 leaf 六向颜色**接到 GPU 的实体/道具通路上**，
让每个实例用它自己所在 leaf 的立方体，并把"到底接上了多少"变成捕获行上的可断言计数。
所有数字的原件在 `evidence/leaf-ambient-gpu/`（见该目录 README）。

## 0. 结论

1. **接线是逐实例的，回落也是逐实例的**。玩家、静态道具、地图模型实体走同一个 `resolveLeafCube()`
   （`native/src/main.cpp:13203`；玩家 :14045 与静态道具 :15349 用 `draw.origin`。
   地图模型实体 :13298 用矩阵平移 `matrix[3],[7],[11]`——那个 draw 的 origin 故意留在零，
   见 :13292-13297 的注释），命中后
   18 个通道写进实例记录、由槽表折成一个 `uint` 槽号塞进实例记录的 `gpu.normal[0][3]`
   （`native/src/native_renderer_entity.cpp:656-702`），顶点着色器按 `nointerpolation uint
   ambientSlot : TEXCOORD9` 传下去（`native/src/native_renderer_shaders.cpp:92,129`），像素着色器
   从 `StructuredBuffer<float4> leafAmbientCubeFaces : register(t12)`
   （`native/src/native_renderer_shaders.cpp:206`）取六面并按法线平方加权
   （`modelLightingRgb`，`native/src/native_renderer_shaders.cpp:221-229`）。
   没取到立方体的实例继续画 basic arm 的 sky/ground/side 常数——不是整帧开关。
2. **计数恒等式成立并且被断言**。cp_process_f12 tick 600：
   `slots=147 instances=406 withCube=337 withCubeEmpty=3 noCubeInLeaf=69 outsideLeaf=0 noLump=0
   notAsked=0 requests=406 identity=ok`；337+69=406。cp_sunshine tick 600：
   `slots=162 instances=420 withCube=346 withCubeEmpty=3 noCubeInLeaf=74 requests=420 identity=ok`。
   求和式印在捕获行上（`native/src/native_renderer_frame.cpp:1003`），并在
   `setEntityModelInstances()` 收尾时断言（`native/src/native_renderer_entity.cpp:937-960`），
   不等就打 `[renderer-invariant] leaf-ambient identity ...`、把 `lastError_` 置 `E_UNEXPECTED`。
   **普通运行**里设备错误策略把它变成 `PostQuitMessage(3)`（`native/src/main.cpp:16991-17016`，其中 `:17014` 那一行）；
   但**捕获帧**走另一条出口（`native/src/main.cpp:16949-16968`）：那里只看帧/装置/深度面有没有写成，
   **不读 `lastError_`**。所以在正好写 BMP 的那一帧触发不变式，进程仍会退出 0、BMP 照样落盘，
   唯一的证据是 stderr 那行与捕获行上的 `identity=MISMATCH`——读捕获日志必须看这两处，不能只看退出码。
3. **空立方体落在 `withCube`，不落在 `noCubeInLeaf`**。它有一份烘焙、18 个通道全 0，
   语义是"这个 leaf 收不到天光"，与"没有烘焙"是相反的事实。运行时两个地图各 3 个实例属于这类
   （占 withCube 的 3/337、3/346），全语料 2.18%（cp_process_f12：77/3537）与 2.16%
   （cp_badlands：71/3286）。这类实例环境项为 0，只剩太阳项（`modelLightingRgb` 返回
   `ambient + sun.w * halfLambert²`），所以是"晒不到天光"，不是黑成一片。
4. **槽表是精确去重，不是量化**：`+0` 与 `-0` 合一个槽，差 1 个 mantissa LSB 就分两个槽，
   16 个相同实例只吃 1 槽（自检 `table.signedZeroIsOneCube` / `table.oneMantissaLsbIsANewSlot`
   / `table.sixteenEqualInstancesOneSlot`）。所以 `slots` 是"可见不同立方体数"的**上界**：
   147 槽承载 337 个带立方体实例（43.6%），162 槽承载 346 个（46.8%）。
5. **尺度 k 是测量值，不是拍脑袋**：k=738.229 让 leaf arm 的平均亮度等于 basic arm 的全局平均
   （1.195807，`evidence/leaf-ambient-gpu/scale-measurement.txt` 的 `MEAN-MATCHED` 行）；
   发货常量是 `kLeafAmbientScale = 738.0f`（`native/include/leaf_ambient_table.h:84`，
   文件头 :60-67 说明根与发货 float 的关系）。方向空间里 clip(>1.0)=0.540（demo 立方体）/
   0.323（语料 131 方向），但落屏后只有 **0.651%** 的像素有任一通道 255、**0.137%** 全白
   （`evidence/leaf-ambient-gpu/clip-measurement.txt`，剪切修复后重算；basic 臂对应
   0.303% / 0.093%）——方向空间饱和不等于屏幕过曝。修复前那一版量到 0.433% / 0.096%，
   偏小是因为被剪切过的表把亮面挪到了别处；这两个数也要一起看，别单独引用旧的。
6. **引脚与可复现性**：`--lighting leaf` 臂**现在可复现**——同一个 exe 连续 5 次捕获是同一个
   md5 `fa297fb989bd4e2b3100bbd95cecb666`（`pins-and-counters.txt` 末行原话
   `leaf arm byte-identical across all 5 runs: YES`）。这个 md5 绑定的是**源码**提交
   `b1293105`；同一份源码编出过两个 exe，两个都留了证据：
   `94375d7f109f4a00e16fc8af9ecebde71f5c97120dde3f209980b2d5634a0478`（3434496 字节，
   13:09:34 构建，那 5 次捕获在这里），以及改完一行注释后重编的
   `54abaeebdc681d08cf8d6eb2bd80d7ed154bfef6b4cf321251a674b8a13a7bfd`（同尺寸，13:32:04）。
   第二个 exe 上把每个臂都重采了一遍，8 个 md5 与第一个 exe **逐字节相同**
   （`evidence/leaf-ambient-gpu/rebuild-reverify.log`）——所以引脚不是某一次链接的产物。
   第二张地图两次同一 md5（`b9ed23d8c865ed7e2a19f6e80f271dc0`）。
   其它臂与修复前逐字节相同：`bare`/`off` = `2ddade356e85dd77b795a7380c254bcb`、
   `basic` = `d927c52639fb10c62a50b3018806a2d3`、`leaf-trace` =
   `3f39236ee189629f9a5ed42c170a80da`、`sunshine-leaf-trace` =
   `73550075d26879a40f0241824ce7bbab`——**修复只动了 leaf 光照臂，一个别的像素都没碰**。
   **修复之前发布过的每一个 leaf 光照臂 md5 都是一次抽样，与 `b1293105` 之后不可比**：
   `a3f4229c` / `83a85fd7` / `868f42e7` / `5e489226` / `d3823da4` / `91867bf7` /
   `56f8e20e` / `8b0aa348` / `b9233d87` / `33631a40` 全部作废，原样列在 §7 作历史，
   作废理由只有一条：它们都是从**被剪切过的槽表**里读出来的像素，而同一台机器同一条命令
   每次读出来的都不一样（§8）。两个 `*-trace` md5 **不作废**，它们是"法线当颜色"的调试视图，
   着色器在 `native/src/native_renderer_shaders.cpp:627-629` 就返回了，从来不读立方体
   ——它们修复前后逐字节相同，正是这件事的证据。`basic` 与 `leaf` 两个 `--lighting-trace`
   捕获的 md5 之所以也相同（都是 `3f39236e…`），原因同在这一条：
   像素分支在 :621 先算完 `modelLightingRgb`，随后 :627-628 的调试返回直接把画面换成法线色，
   光照结果根本没进最终颜色——两条 trace 相同并不说明两个光照模式相同，只说明 trace 视图在
   `modelLightingRgb` 之后覆盖了它。这条相等只关于 trace 视图，与 leaf 立方体数据本身无关；
   leaf 臂的画面读数归 §7 的 pin 表，不由这条推断。
7. **验收（同 tick 室内 vs 室外）**：tick 600 打印的 10 个玩家实例里，+Z 面（朝上=能看到多少天光）
   最高 `0.002354`（index 2/7，leaf 2269）与最低 `0.000105`（index 16，leaf 1511）相差
   **22.42 倍**（6 位小数相除就是 22.42；工具早先按更高精度报 22.35，差在第三位）。
   这三个数是 CPU 侧解析出来的立方体值、在进 GPU 之前就打出来了，所以剪切修复**没有动它们**，
   一次复算仍是 `0.002354 / 0.000105 / 22.42`。
   画面上能对上的两个玩家做 1:1 裁切（同窗口、两臂），修复后：院子里的（室外叶）
   leaf 比 basic 亮 `(R,G,B)=+3.2/+3.5/+12.6`，门洞里的（室内叶）`-5.8/-3.1/-3.2`。
   方向**成立而且更干净**：室外那一块三通道全正、门洞那一块三通道全负，两个玩家的差
   相差 10.5 个灰阶；修复前那一版是 `+11.5/+13.9/+5.8` 与 `-1.7/-0.6/-1.5`，
   差别只剩 1 个灰阶。逐通道的量变了、**符号与会话方向没变**——因为剪切把六张面挪位了，
   修复前"亮在哪个通道"本身是错的。

## 1. 改动

四个文件、+221/-16 行，全部是**计数、断言、打印与自检**，光照数学没有改
（这是 Half B 本体那一笔；leaf37f 的剪切修复是另立的一笔，三个文件 +88/-5，见 §8.3）：

| 文件 | 改了什么 |
| --- | --- |
| `native/include/native_renderer.h` | 新增 `leafAmbientWithCubeEmpty()` / `leafAmbientRequests()` / `leafAmbientIdentityChecked()` / `leafAmbientIdentityOk()` 与对应成员；注释写清"空立方体是 withCube 的子集，永远 ≤ withCube" |
| `native/src/native_renderer_entity.cpp` | 每次更新重置计数（:500-517，随后 :518-521 是提前返回）、按 18 通道全 0 计 `withCubeEmpty_`、收尾求和断言（:937-960，两条不变式：`withCubeEmpty ≤ withCube` 与 `sum == accepted`） |
| `native/src/native_renderer_frame.cpp` | 相机捕获行补 `worldCenter/worldMinZ/worldScale` 与 `forward`（:793-798），leaf-ambient 捕获行补 `withCubeEmpty/requests/identity`（:1003-1006） |
| `native/tools/leaf_ambient_probe.cpp` | 自检扩到 38 项：槽表三条 + 空立方体必须是"命中"而不是"未命中"（`cube.blackCubeIsAHit`） |

**leaf37f 那一轮又加了两项，自检现在是 40 项**（`evidence/leaf-ambient-gpu/self-test-40-checks.txt`，
`SELFTEST-RC=0`、`"summary":{"self-test","failures":0}`）：`table.rowIsOneFloat4PerFace`
钉住"CPU 一行 = 着色器一行的 float4"（detail 原话 `sizeof=96 faces=6 rowBytes=16 faceSpan=16`），
`table.paddingLaneIsNotIdentity` 钉住第四条 padding lane 不参与立方体身份
（`slotPacked=0 slotPadded=0 distinct=1`）。

### 1.1 数据通路（按执行顺序）

1. `resolveLeafCube(x,y,z, draw)`（`native/src/main.cpp:13203`）在**实体自己的位置**上取 leaf
   （源码注释的原话是"origin 是实体的，不是眼睛的"——模型在它站的地方被照亮，眼睛可能高出
   一层楼、身在另一个 leaf）；命中后写 `draw.leafAmbient*[18]` 与 `hasLeafCube`。
   三个调用点：玩家 :14045 与静态道具 :15349 用 `draw.origin`，地图模型实体 :13298 用矩阵平移。
2. `setEntityModelInstances()` 逐实例：命中 → 18 个 float 进槽表（首次出现分配新槽），
   槽号写 `gpu.normal[0][3]`；未命中 → 写哨兵 `kLeafAmbientNoSlot`（-1.0f，
   `native/src/native_renderer_entity.cpp:667`），**不是 0 号槽**（0 号槽是别人的真立方体，
   0 会被读成"全黑"）。
3. 槽表整体上传为 `StructuredBuffer`（着色器侧 `leafAmbientCubeFaces : register(t12)`，
   `native/src/native_renderer_shaders.cpp:206`）。这棵树**没有根签名**，是直接绑 D3D11 寄存器：
   实体 pass 之前 `PSSetShaderResources(12, 1, &leafView)`（`native/src/native_renderer_frame.cpp:1699-1708`），
   这个 pass 一结束就解绑（:1843-1847），免得后面的 pass 继承一张它没要过的表；
   `native/src/native_renderer_shaders.cpp:198-206` 只是解释这段约定的注释，不是机制。
4. 像素着色器：先把 `ambientCube[6]` 拷进 `leafCube[6]`，`ambientSlot != 0xFFFFFFFF` 时再用
   槽里的六面覆盖，然后进同一条 `modelLightingRgb`（`native/src/native_renderer_shaders.cpp:611-621`）。

## 2. 槽表不变量（为什么它必须是精确相等）

`slots` 只有在"相同立方体必然合并、不同立方体必然分开"时才等于可见立方体数。自检把它钉成四条：

- `table.equalCubesEqualSlots` —— 同一份 18 通道插两次 → 一个槽。注意这条曾经**误报**：
  原来的写法是"插两次再插一份不同的、然后数 distinct"，第三次插入把两张表都变成 2，
  `distinctAfterEqual` 永远是 1，测试恒真。现在先读 `distinctAfterEqual` 再插第三份，
  detail 里能直接看到 `distinctAfterEqual=1`（不是靠顺序碰巧）。
- `table.sixteenEqualInstancesOneSlot` —— 16 个相同实例仍只有 1 槽（排除"每 N 个实例强制新槽"）。
- `table.oneMantissaLsbIsANewSlot` —— 只有 1 个 mantissa LSB 的差别也分成两个槽
  （排除"带 epsilon 的近似比较"；近似比较会让相邻 leaf 的细微差别被吃掉）。
- `table.signedZeroIsOneCube` —— `+0.0` 与 `-0.0` 合成一个槽（排除"按位比较"：
  `-0.0` 是合法烘焙值，按位比较会浪费槽，而 18 个通道都是 `-0.0` 的 leaf 与 `+0.0` 视觉上同一份）。

结论：槽表按 **IEEE 数值相等**（`==`）合并，所以 `slots` 是上界；147/337 与 162/346 说明
上界很紧（同一 leaf 的多个实例大量合并，主要是同一 leaf 的 16 个 prop 实例）。

## 3. 计数恒等式与空立方体

捕获行（`native/src/native_renderer_frame.cpp:1003`）一次把五个桶、和、身份都印出来：

```
cp_process_f12 tick 600: slots=147 instances=406 withCube=337 withCubeEmpty=3 noCubeInLeaf=69 identity=ok
cp_sunshine    tick 600: slots=162 instances=420 withCube=346 withCubeEmpty=3 noCubeInLeaf=74  identity=ok
```

- `instances` = 这一帧走通的实例数（实体 + 静态道具）；`requests` = 五个桶之和。
  两者相等才算"一个实例都没被桶吃掉"，否则 `identity=MISMATCH`、断言往 stderr 打一行并把
  `lastError_` 置 `E_UNEXPECTED`（普通运行因此退出 3；捕获帧的出口见 §0 第 2 条）。
- `withCubeEmpty` 是 `withCube` 的**子集**（不是第六个桶），断言保证
  `withCubeEmpty ≤ withCube`；这两个数是"有一份烘焙"与"烘焙是黑的"两个不同事实。
- 空立方体在**全语料**里的占比（Half A 的 dump 重算，`evidence/leaf-ambient-gpu/empty-cube-census.txt`）：
  cp_process_f12 `77/3537 = 2.177%`，cp_badlands `71/3286 = 2.161%`。
  运行时每帧只有 3 个实例踩中，因为一帧只画视锥内的一小部分实例。
- 自检 `cube.blackCubeIsAHit`（detail：`available=true sampleIndex=0 brightest=0 hit=1 missed=0`）
  钉住"全 0 立方体必须算命中"：如果它算未命中，那么近 2.2% 的 leaf 会静默退回 basic 常数，
  而计数看起来完全正常——这正是本 Half 要防的"看不见的空操作"。

## 4. 尺度 738：为什么是常数而不是每图归一化

递归上采样的能量随迭代次数指数增长，Half A 用"均值匹配"定标：找一个 k 让 leaf arm 的
平均亮度等于 basic arm 的全局平均（1.195807）。`evidence/leaf-ambient-gpu/scale-measurement.txt`
记录了全部依据：

- `MEAN-MATCHED k=738.229`（二分根）；`kLeafAmbientScale = 738.0f` 是离它最近的 float32
  （`native/include/leaf_ambient_table.h:60-84`）。
- 半/双倍扫描（369.1 / 738.2 / 1476.5）：均值 0.747897 / 1.195807 / 2.091627，
  `dark(<0.25)` 0.1194 / 0.0335 / 0.0097，`clip(>1.0)` 0.2522 / 0.5402 / 0.7894。
  两个方向都会离开 basic arm（`VERDICT MEAN-MATCHED k=738.2 leaves the basic arm`）。
- 这个 k 是**全语料一个数**，不是按地图/按 leaf 归一化：每 leaf 的亮度差是烘焙本身的信息，
  归一化会把它抹掉。
- 代价是方向空间里 0.54 的方向均值 >1（会 clip）。但那是**方向空间**的统计：在真实捕获上
  量像素（`evidence/leaf-ambient-gpu/clip-measurement.txt`，1264×681=860784 px，剪切修复后重算）：
  leaf 全白(三通道 255) 1179 px = 0.1370%，任一通道 255 的 5604 px = 0.6510%，
  basic 对应 801 px = 0.0931% / 2610 px = 0.3032%，leaf 独占饱和像素 2994 个；
  leaf 减 basic 的整帧逐通道均值 `-1.340/-1.033/+3.318`（red/green 略暗、blue 明显亮，
  这是天空光偏蓝的直接体现；修复前量到的是 `-0.445/-0.408/-1.377`，通道方向是错的）。

## 5. 两个地图（不是一张图碰巧）

| 地图 | instances | withCube | withCubeEmpty | noCubeInLeaf | slots | identity |
| --- | --- | --- | --- | --- | --- | --- |
| cp_process_f12 | 406 | 337 | 3 | 69 | 147 | ok |
| cp_sunshine | 420 | 346 | 3 | 74 | 162 | ok |

两张图的 leaf/立方体总量不同（3537 vs 3286 个带烘焙 leaf），槽数、命中率、空立方体数各自独立，
恒等式都成立。第二张图的捕获行与日志见 `evidence/leaf-ambient-gpu/pin-cp_sunshine-leaf-trace.log`。

第二张图的**光照**臂（不是 trace）在剪切修复后连续两次同一 md5 `b9ed23d8c865ed7e2a19f6e80f271dc0`
（`sunshine-leaf` 与 `sunshine-leaf2`）；它的 trace 臂 `73550075d26879a40f0241824ce7bbab`
与修复前逐字节相同。也就是说两张地图上"修复只动 leaf 光照、不动别的臂"都成立，不是单图巧合。

## 6. 验收：同一 tick 的室内与室外

**先看整体**：`--lighting leaf` 与 `--lighting basic` 在同一 tick 的整帧差是
**110386 px = 12.824%**，`changed>=16` 105859、meanAbs 4.468、max 177，bbox 是整幅画面
（`evidence/leaf-ambient-gpu/arm-diffs.txt`）。修复前这个比例是 12.831%、但 `changed>=16` 只有
54182——份额几乎没变而"差异显著的像素"翻了一倍，正是"亮部原来被放进了错误通道"的样子：
总量在，分布错了。所以下面是同一份 12.8% 里挑出来的两个可对号入座的玩家。

数据：`evidence/leaf-ambient-gpu/entity-cubes-tick600.txt`（10 个被打印的玩家实例，
按 +Z 面亮度排序）。+Z 是朝上的面，法线朝上的表面权重几乎全给它，所以 +Z 亮度就是
"这个 leaf 能看到多少天光"的度量：

| index | leaf | slot | mean | +Z (up) | -Z (down) |
| --- | --- | --- | --- | --- | --- |
| 2, 7 | 2269 | 23 | 0.001556 | **0.002354** | 0.001348 |
| 5, 13 | 1827 | 16 | 0.001556 | 0.002344 | 0.001284 |
| 10 | 2307 | 27 | 0.001358 | 0.002231 | 0.000980 |
| 4 | 504 | 24 | 0.000880 | 0.001838 | 0.000007 |
| 9 | 2673 | 26 | 0.001233 | 0.001532 | 0.001208 |
| 15 | 2635 | 28 | 0.000522 | 0.000297 | 0.000632 |
| 8 | 462 | 25 | 0.000346 | 0.000116 | 0.000542 |
| 16 | 1511 | 10 | 0.000238 | **0.000105** | 0.000359 |

室外/室内 = 0.002354 / 0.000105 = **22.42 倍**。

像素证据（`evidence/leaf-ambient-gpu/accept-players-1to1.png`，同一窗口、同 tick、两臂 1:1）：

| 玩家 | 窗 | leaf 均值 | basic 均值 | leaf−basic |
| --- | --- | --- | --- | --- |
| 院子里（底部，室外叶） | (709,481)-(829,681) | (126.3,114.3,117.2) | (123.1,110.8,104.6) | **+3.2,+3.5,+12.6** |
| 门洞里（右侧，室内叶） | (1052,481)-(1172,681) | (86.3,74.3,65.4) | (92.1,77.4,68.6) | **−5.8,−3.1,−3.2** |

院子里的玩家在 leaf arm 变亮、而且**主要亮在蓝通道**（+12.6，红绿只 +3 左右），门洞里的变暗——
与两个 leaf 的 +Z 亮度（0.002354 vs 0.000105）方向一致：室外叶拿到天空光，天空光是偏蓝的，
所以抬起来的正好是蓝通道；室内叶的环境项接近 0，反倒比 basic 的常数项暗一点。
**修复前的同一张表是 `+11.5,+13.9,+5.8` 与 `−1.7,−0.6,−1.5`**：符号方向当时也对，
但"亮在哪个通道"是错的（红绿比蓝抬得多，符合剪切把六张面整体挪了一格的结果），
而且门洞那位只差 1 个灰阶、几乎量不到。修复后两个玩家相差 10.5 个灰阶，方向一样、幅度和通道都变干净了。

**投影的诚实说明**：引擎不打印屏幕投影。本轮把 `worldCenter/worldMinZ/worldScale` 与
`forward` 加进捕获行，就是为了能自己重建（`map→box` 变换是引擎自己的：
`native/src/native_renderer_camera.cpp:53-58` 的 `setObserverFocus`；相机也在同一 box 空间，Z 朝上：
up 在 `native/src/native_renderer_frame.cpp:685`、`LookAtLH` 在 :715、投影在 :800，相机那一行
打印在 :793-798）。重建结果在
`evidence/leaf-ambient-gpu/projection-caveat.txt`：10 个实例里只有 3 个落在画面内，
其中 index 16（门洞士兵）的投影点与实际红色 blob 底边差 55 px，index 5/13（画面底部的两个）
脚在画面下沿以下 80-230 px——正好解释为什么底部两个被切了腿。但**投影到的是脚（trace 的
origin），不是可见躯干**：实体绘制枢轴与模型原点之间有一个引擎不打印的偏移
（`native/src/native_renderer_entity.cpp:650-662` 的模型行）。所以两张裁切按**测到的红色军装
blob** 取窗，立方体数字按"最近的被追踪实例"引用，并在 `projection-caveat.txt` 里写清这条界线，
不假装有一个引擎并不打印的对应关系。

## 7. 引脚（引脚协议 + 结果）

固定命令行（与前任逐字节相同，`proc`=cp_process_f12，`sun`=cp_sunshine）：

```
EXE --demo "$PROC" --capture-tick 600 --capture-frame OUT/name.bmp --audio-disabled --hidden-window [臂]
```

**当前引脚**（源码 = 提交 `b1293105`；两个同源码的 exe 都验过：
`94375d7f109f4a00e16fc8af9ecebde71f5c97120dde3f209980b2d5634a0478` 采了下表全部 md5，
`54abaeebdc681d08cf8d6eb2bd80d7ed154bfef6b4cf321251a674b8a13a7bfd` 重采一遍得到同样的 md5
（`rebuild-reverify.log`）——**下表 md5 只绑定源码，不绑定其中某一次链接**。
捕获日志 `evidence/leaf-ambient-gpu/pin-*.log`，表格 `pins-and-counters.txt`）：

| run | 臂 | 当前 md5 | 可复现性 |
| --- | --- | --- | --- |
| bare | （无） | 2ddade356e85dd77b795a7380c254bcb | 与前任逐字节相同 |
| off | `--lighting off` | 2ddade356e85dd77b795a7380c254bcb | 与 bare 逐字节相同 |
| basic | `--lighting basic` | d927c52639fb10c62a50b3018806a2d3 | 与前任逐字节相同 |
| leaf | `--lighting leaf` | **fa297fb989bd4e2b3100bbd95cecb666** | **连跑 5 次全同**（§8）；重编的新 exe 上再连跑 3 次仍同 |
| leaf-trace | `--lighting leaf --lighting-trace` | 3f39236ee189629f9a5ed42c170a80da | 与前任逐字节相同（调试视图，不读立方体） |
| sunshine-leaf | 第二张地图，`--lighting leaf` | **b9ed23d8c865ed7e2a19f6e80f271dc0** | 连跑 2 次全同（重编后再 2 次仍同） |
| sunshine-leaf-trace | 第二张地图，leaf+trace | 73550075d26879a40f0241824ce7bbab | 与前任逐字节相同 |
| leaf-outline | `--lighting leaf --outline on` | 本轮未采 | `--outline on\|off` 是这 exe 真接受的开关（`native/src/main_cli.h:188-204`，状态与用法注释在 `native/src/main.cpp:3230-3234`，生效在 `:4931-4932`，非法值在 `:5017-5023` 被拒并 exit 13），只是本轮批处理没有排这一臂；要补齐就跟上表其它行一样跑同一条命令加 `--outline on`，再和前任基线 `b4608170e6e7e6fe8262e1c831e94246` 比 |

**历史（作废，不要引用）**：`a3f4229c48c054b8b4bf15e5860c3e43`（前任 leaf）、
`83a85fd73d6dabe166bf38042c2f923f`（本轮修复前 leaf）、`868f42e7551023ac433d4e73995851de` /
`5e489226ff9aa9956399a3f1a43a6e46` / `d3823da4d4a7be27c31c76329590ae3a`（同一条命令连跑三次
得到的三个不同 md5）、`91867bf76e73547445476587eb3faffe` / `56f8e20ec4adb5f255a6971a2b231d0c` /
`8b0aa34852e1aac68ec03856d1c2199b`、
`b9233d878a06774b81bb99ec5d9bd5f0` / `33631a4042101506243e3e5748d71c86`、
`73550075d26879a40f0241824ce7bbab` 作为**光照臂**引脚（它作为 trace 引脚仍然有效）、
`01afa412794b6a1dd83a90b05cb6b099`。作废理由见 §8：它们全是从被剪切的槽表里读出的像素，
同机同命令每次都不同，**任何一个单值都不能当基线**。原始清单也留在
`evidence/leaf-ambient-gpu/retired-pins.txt`。

三个"不带 leaf"的臂逐字节不变，是"修复只动了 leaf 光照"的硬证据。

## 8. 剪切过的槽表：`--lighting leaf` 为什么逐次不同（已修）

### 8.1 症状

`--lighting leaf` 在**同一个 exe、同一条命令**下每次捕获都不一样。leaf37f 用
`d5efd2c814ac39cdd7789b457eb4df524e87fc9756ed9f90bd5fd3d7ac325a9c` 那个 exe 连跑三次，
得到三个不同 md5（`868f42e7…` / `5e489226…` / `d3823da4…`，脚本 `.scratch/leaf37f/det-batch.sh`，
原始日志提交为 `evidence/leaf-ambient-gpu/det-3run-prefix.log`）；同一批里 `basic` 三次全同
（`d927c526…`），`leaf-trace` 三次全同（`3f39236e…`）。
两次 leaf 之间约 1600–3000 个像素不同，delta 最大 255，bbox 永远在画面同一块里，
**只有亮度变、几何和法线不动**——是"某个实例拿到了别的立方体"，不是渲染管线抖动。

`leaf-trace` 的确定性把范围切得很死：着色器在
`native/src/native_renderer_shaders.cpp:627-629` 的调试分支里就返回了，
**从来不读 `leafAmbientCubeFaces`**（真正的光照调用在 :621）。所以顶点阶段、实例数据、
插值全都字节稳定，唯一能变的就是写进 `gpu.normal[0][3]` 的那个 slot 值
（`native/src/native_renderer_entity.cpp:694-702`）。计数行也证明"聚合层面"稳定：
每次都是 `slots=147 instances=406 withCube=337 …identity=ok`——聚合看得见总量，看不见排列。

### 8.2 机制：CPU 一行 72 字节，着色器一行 96 字节

`LeafAmbientCubeFace` 原本是**六个 `float3`**（72 字节），而上传循环按
`kLeafAmbientFaces * sizeof(float) * 4` = **每立方体 96 字节**去 `memcpy`
（旧 `native/src/native_renderer_entity.cpp:1125` 与 :1065）：

- 每个 slot 在 GPU 端是六个 `float4`（着色器 `native/src/native_renderer_shaders.cpp:611-621`
  就是这么索引的），CPU 却按 72 字节一格的间距往里填 96 字节一格的内容 → **整张表被剪切
  （shear）**：第 n 个立方体的六张面读到的其实是源数据里错位的浮点数，面 i 的通道会串到
  面 i+1 上。
- 更糟的是最后一次 `memcpy` 在 147 个立方体时读了 `147*96 = 14112` 字节，
  而源 vector 只有 `147*72 = 10584` 字节——**越过 vector 末尾读了 3528 字节**，
  读进来的是 capacity 之外的堆内存。这段内存的内容取决于前面分配了什么，
  于是同一个 slot 每次跑可能读到不同的垃圾，屏幕上的亮度就跟着变。

两件事都能解释观测：剪切解释"哪个通道变"（修复前院子里的玩家红绿抬得比蓝多，
修复后蓝抬得最多——通道回到了正确位置），越界读解释"为什么同机同命令每次不同"。
它同时把"+Z 那三个数为什么没变"解释清楚：那是 CPU 侧解析出来的原值，
在进 GPU 之前就打进 trace 了，根本没经过这张表。

### 8.3 修复：一行就是六个 `float4`

- `native/include/leaf_ambient_table.h`：`LeafAmbientCubeFace` 改成
  `std::array<std::array<float, 4>, kLeafAmbientFaces>`，并加
  `static_assert(sizeof(LeafAmbientCubeFace) == kLeafAmbientFaces * sizeof(float) * 4, …)`
  ——**尺寸不再靠注释约定，编译器看着**。
- `native/src/native_renderer_entity.cpp` 的 `uploadLeafAmbientCubes()`：引入
  `kCubeBytes = kLeafAmbientFaces * sizeof(float) * 4`，`ByteWidth` 与 `memcpy` 都用它，
  再 `static_assert(sizeof(LeafAmbientCubeFace) == kCubeBytes)`。上传量与源对象尺寸
  在编译期绑死，越界读不可能再出现。
- `native/tools/leaf_ambient_probe.cpp`：自检加两项——
  `table.rowIsOneFloat4PerFace`（`sizeof=96 faces=6 rowBytes=16 faceSpan=16`，
  连"相邻两张面之间正好隔一个 float4"都钉住）与 `table.paddingLaneIsNotIdentity`
  （只在第四条 lane 上不同的两个立方体必须共享 slot，`slotPacked=0 slotPadded=0 distinct=1`）。
  自检从 38 项变 40 项。

填表那一侧**一行没改**：`native/src/native_renderer_entity.cpp:667-702` 只写通道 0..2，
第四条 lane 由 `LeafAmbientCubeFace face;`（:671 无括号 = 默认初始化 → 成员默认初始化
`rgb{}`）零填；空立方体普查在 :686-692 只测通道 0..2，**永远不看 padding lane**，
所以"全零立方体算命中"这条语义不会因为加了 lane 而变。同理槽表的相等判定
（`native/include/leaf_ambient_table.h:133`）也只比通道 0..2，加宽一行不会让两个
同色立方体变成两个 slot——这正是 `table.paddingLaneIsNotIdentity` 钉的东西。

### 8.4 修复后的状态

同一个 exe 连跑 5 次 `--lighting leaf` 全部 `fa297fb989bd4e2b3100bbd95cecb666`；
第二张地图两次全部 `b9ed23d8…`；`bare/off/basic/leaf-trace/sunshine-leaf-trace` 五个臂
与修复前逐字节相同。逐次不同的现象消失，§7 那张历史表里的每一个 leaf 光照臂 md5
都作废（它们是不同次数的抽样，不是同一个基线）。

**换一遍链接也是同一张图**：上面那批捕获之后，`native/src/native_renderer_entity.cpp` 里
槽表上传的那段注释又改了一次（原文说 ByteWidth、NumElements、memcpy 三处都写成
`kCubeBytes`，实际 NumElements 仍是 `capacity * kLeafAmbientFaces`，只是同一个行数换用
float4 计量）。改注释也要重编，重编出来的 exe 指纹自然变了：
`94375d7f…`（3434496 字节）→
`54abaeebdc681d08cf8d6eb2bd80d7ed154bfef6b4cf321251a674b8a13a7bfd`（同尺寸）。
于是**把每个臂在第二个 exe 上重采了一遍**：8 次捕获、8 个 md5 与第一个 exe 逐字节相同
（`evidence/leaf-ambient-gpu/rebuild-reverify.log`）。这条不只是自证——它把"引脚钉的是
源码"和"引脚钉的是某一次链接"两件事分开了，后者才是叶臂修复前那种不可复现的病。

### 8.5 附带的教训：改完源文件必须 touch 再构建

本轮第一次构建（09:38）的日志里**只**重编了 `generated/tf2_build_stamp.cpp` 并重链接，
四个被改过的源文件一个都没进 exe。原因是 `ops_edit_file` 保留 mtime：源文件时间戳比
`.obj` 更**旧**，nmake 判定无需重编，于是一个"成功"的构建其实链接了旧对象。原始记录在
`evidence/leaf-ambient-gpu/batch-37d.log`（touch 在 09:41:43），`build-warnings.txt`
是它的构建段节选。教训：**看构建日志里到底重编了哪几个文件**——"构建成功"不等于
"新代码进了 exe"。批处理脚本因此统一在构建前 touch
（`evidence/leaf-ambient-gpu/tools/capture-batch.sh`）。

## 9. 没做 / 已知限制

- **trace 是采样，不是全量**：`tracePrinted` 有上限（本帧 10/11 条，其余 396/409 个实例不打印），
  且打印的 10 条全是玩家模型。所以 §6 的立方体表只覆盖玩家，不是 406 个实例的 census。
- **trace 捕获不是光照捕获**：`--lighting-trace` 把插值法线当颜色画（shader 的调试分支），
  两条 trace 捕获逐字节相同只说明几何/法线通路没变。要量光照看 `leaf` 臂。
- **投影**：见 §6 末段，引擎不打印屏幕投影，重建只能到脚、不能到躯干；裁切按 blob 取窗。
- **空立方体实例是太阳-only**：环境项为 0，视觉上是"只有太阳"的暗面；这是烘焙的事实，
  不是缺省值；如果产品上希望空立方体也有一点环境项，那是另一个决定（Half A 的 42.65%
  无烘焙 leaf 继续走 basic 常数是**继承来的**规则，出处 `native/include/native_renderer.h:324`
  与 `:1183`，本轮没有重测、也没有改）。
- **clip 是方向空间的现象**：0.54 的方向均值 >1，但屏幕像素级只有 0.651% 有通道 255、
  0.137% 全白（basic 臂 0.303% / 0.093%）；
  如果以后换更亮的太阳/更暗的地图，需要重新量像素级 clip。
- **槽表次序依赖调用次序**：`leafAmbientSlotFor()` 是"先来先占号"的线性搜索
  （`native/include/leaf_ambient_table.h:126-140`），给同一个世界喂同一个实例序列时是
  确定的（本轮连跑 5 次 `slots=147` 且画面逐字节相同），但**如果有人改了实例遍历次序，
  slot 号会整体重排**、md5 会变，而画面不一定会变。所以 §7 的 leaf 引脚绑定的是
  "这个 exe + 这张地图 + 这个 tick + 这个实例次序"，不是"任何情况下都该出现的图"。
- **SRV 的容量余量仍是旧的写法**：`view.Buffer.NumElements = capacity * kLeafAmbientFaces`
  而只写 `distinct.size() * kLeafAmbientFaces` 行，`capacity > distinct.size()` 时尾部那些行
  是没写过的（现在 `capacity == max(distinct.size(), 16)`，147 个立方体时两者相等，不触发）。
  留着是因为它不影响任何已测量的帧，但它是一颗同样的种子，写在这里备查。
- 没有改光照数学、没有动 Half A 的解析/两页规则/回落计数语义。

## 10. 复现

```bash
# 1) 构建（必须先 touch：见 §8）
touch native/src/native_renderer_frame.cpp native/src/native_renderer_entity.cpp \
      native/include/native_renderer.h native/tools/leaf_ambient_probe.cpp
bash build-target.sh tf2_demo_native leaf_ambient_probe
native/build-nmake/leaf_ambient_probe.exe --self-test   # 40 项，failures=0
#    原始输出提交为 evidence/leaf-ambient-gpu/self-test-40-checks.txt
#    （38 项那一版是 probe-rebuild.log；build-warnings.txt 里那次的 34 是更早的旧探针读数）

# 2) 捕获（全在 hostlock + verify-all 锁内，见 tools/lock-retry.sh）
EXE=native/build-nmake/tf2_demo_native.exe
PROC="E:/demo测试/cp_process_f12/STV/1338f5de79ea343682c17dd514c980d9_matcha-20260927-1312-cp_process_f12.dem"
"$EXE" --demo "$PROC" --capture-tick 600 --capture-frame .scratch/leafgpu/leaf.bmp \
       --audio-disabled --hidden-window --lighting leaf
# 同样命令换 --lighting off|basic|leaf --lighting-trace；第二张地图把 --demo 换成 SUNSHINE.dem

# 2b) 可复现性：leaf 臂连跑 5 次，5 个 md5 必须全同（这是 §7 那个引脚的依据）
for i in 1 2 3 4 5; do
  "$EXE" --demo "$PROC" --capture-tick 600 --capture-frame .scratch/leafgpu/leaf$i.bmp \
         --audio-disabled --hidden-window --lighting leaf > .scratch/leafgpu/leaf$i.log 2>&1
done
md5sum .scratch/leafgpu/leaf[1-5].bmp | awk '{print $1}' | sort -u | wc -l   # 必须是 1

# 3) 表格、裁切、投影（脚本已提交在 evidence/leaf-ambient-gpu/tools/，路径按 worktree 根解析；
#    第一个参数是第 2 步写捕获的目录，缺省 .scratch/leafgpu/）
python evidence/leaf-ambient-gpu/tools/make-tables.py .scratch/leafgpu
#    产出 pins-and-counters.txt / entity-cubes-tick600.txt / player-crops-stats.txt /
#         arm-diffs.txt / clip-measurement.txt（末行会打印 leaf 臂 distinct-md5 与是否全同）
python evidence/leaf-ambient-gpu/tools/player-crop-pair.py    # accept-players-1to1.png
python evidence/leaf-ambient-gpu/tools/projection-accept.py   # 投影重建（projection-caveat.txt 的数字）
python evidence/leaf-ambient-gpu/tools/arm-cross-diff-bbox.py # 差异 bbox 的复算
#    注意：它复算的是 bbox-diff-run.txt 那一行（历史记录，修复前的两次抽样）；
#    修复后的两两差与 bbox 已经在 make-tables.py 的 arm-diffs.txt 里，别混用。
```

```bash
# 4) 换一遍链接再验一次（这一步不是走过场：叶臂修复前正是"同一条命令不同次结果"，
#    所以要证明引脚钉的是源码而不是某一次链接）
#    改一行注释 → 重编 → exe 指纹必然变 → 每个臂重采一遍 → md5 必须逐个相同
touch native/src/native_renderer_entity.cpp && bash build-target.sh tf2_demo_native
sha256sum native/build-nmake/tf2_demo_native.exe   # 与 94375d7f… 不同是正常的
#    重采 bare/off/basic/leaf×3/leaf-trace/sunshine-leaf×2/sunshine-leaf-trace
#    比对结果提交为 evidence/leaf-ambient-gpu/rebuild-reverify.log
#    （本轮记录：94375d7f… → 54abaeeb… 两个 exe，8 个 md5 全同）
```
