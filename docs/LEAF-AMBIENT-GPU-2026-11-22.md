# BSP 叶子环境立方体，Half B：GPU 逐实例接线（task-37）

日期 2026-11-22 · 分支 `task/leaf37b-gpu` · 前置：`docs/LEAF-AMBIENT-2026-11-21.md`
（Half A：lump 解析、六向颜色、两页不一致、回落计数、自检 fixture）

本文件只讲 Half B 做的事：把 Half A 离线量出来的每 leaf 六向颜色**接到 GPU 的实体/道具通路上**，
让每个实例用它自己所在 leaf 的立方体，并把"到底接上了多少"变成捕获行上的可断言计数。
所有数字的原件在 `evidence/leaf-ambient-gpu/`（见该目录 README）。

## 0. 结论

1. **接线是逐实例的，回落也是逐实例的**。玩家、静态道具、地图模型实体走同一个 `resolveLeafCube()`
   （`native/src/main.cpp:13084`；玩家 :13772 与静态道具 :14930 用 `draw.origin`。
   地图模型实体 :13179 用矩阵平移 `matrix[3],[7],[11]`——那个 draw 的 origin 故意留在零，
   见 :13173-13178 的注释），命中后
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
   **普通运行**里设备错误策略把它变成 `PostQuitMessage(3)`（`native/src/main.cpp:16509-16533`）；
   但**捕获帧**走另一条出口（`native/src/main.cpp:16467-16486`）：那里只看装置/深度面有没有写成，
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
   0.323（语料 131 方向），但落屏后只有 0.43% 的像素有任一通道 255、0.096% 全白
   （`evidence/leaf-ambient-gpu/clip-measurement.txt`）——方向空间饱和不等于屏幕过曝。
6. **引脚**：`bare`/`off`/`basic` 三个捕获与前任 exe 的 md5 逐字节相同
   （`2ddade356e85dd77b795a7380c254bcb` / `2ddade356e85dd77b795a7380c254bcb` /
   `d927c52639fb10c62a50b3018806a2d3`），说明本轮新增的相机/计数打印没有动渲染。
   `leaf` 与前任差 0.225% 像素（1934 px，meanAbs 0.362），而且全部挤在 (368,110)-(642,273)
   这 275×164 一块（bbox 的复算记录见 `evidence/leaf-ambient-gpu/bbox-diff-run.txt`）。
   这是**前任的 exe 是旧的**造成的，见 §8，不是本轮的改动。
   `leaf-trace` 逐字节相同，但它是"法线当颜色"的调试视图
   （`native/src/native_renderer_shaders.cpp:623-629`），只能证明几何/法线通路没变，
   不能证明立方体数据。
7. **验收（同 tick 室内 vs 室外）**：tick 600 打印的 10 个玩家实例里，+Z 面（朝上=能看到多少天光）
   最高 `0.002354`（index 2/7，leaf 2269）与最低 `0.000105`（index 16，leaf 1511）相差
   **22.42 倍**（6 位小数相除就是 22.42；工具早先按更高精度报 22.35，差在第三位）；画面上能对上的两个玩家做 1:1 裁切（同窗口、两臂）：院子里的（室外叶）
   leaf 比 basic 亮 `(R,G,B)=+11.5/+13.9/+5.8`，门洞里的（室内叶）`-1.7/-0.6/-1.5`（≈0）。
   这是"烘焙确实按实体所在 leaf 走了"的像素证据。

## 1. 改动

四个文件、+221/-16 行，全部是**计数、断言、打印与自检**，光照数学没有改：

| 文件 | 改了什么 |
| --- | --- |
| `native/include/native_renderer.h` | 新增 `leafAmbientWithCubeEmpty()` / `leafAmbientRequests()` / `leafAmbientIdentityChecked()` / `leafAmbientIdentityOk()` 与对应成员；注释写清"空立方体是 withCube 的子集，永远 ≤ withCube" |
| `native/src/native_renderer_entity.cpp` | 每次更新重置计数（:500-517，随后 :518-521 是提前返回）、按 18 通道全 0 计 `withCubeEmpty_`、收尾求和断言（:937-960，两条不变式：`withCubeEmpty ≤ withCube` 与 `sum == accepted`） |
| `native/src/native_renderer_frame.cpp` | 相机捕获行补 `worldCenter/worldMinZ/worldScale` 与 `forward`（:793-798），leaf-ambient 捕获行补 `withCubeEmpty/requests/identity`（:1003-1006） |
| `native/tools/leaf_ambient_probe.cpp` | 自检扩到 38 项：槽表三条 + 空立方体必须是"命中"而不是"未命中"（`cube.blackCubeIsAHit`） |

### 1.1 数据通路（按执行顺序）

1. `resolveLeafCube(x,y,z, draw)`（`native/src/main.cpp:13084`）在**实体自己的位置**上取 leaf
   （源码注释的原话是"origin 是实体的，不是眼睛的"——模型在它站的地方被照亮，眼睛可能高出
   一层楼、身在另一个 leaf）；命中后写 `draw.leafAmbient*[18]` 与 `hasLeafCube`。
   三个调用点：玩家 :13772 与静态道具 :14930 用 `draw.origin`，地图模型实体 :13179 用矩阵平移。
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
  量像素（`evidence/leaf-ambient-gpu/clip-measurement.txt`，1264×681=860784 px）：
  leaf 全白(三通道 255) 830 px = 0.0964%，任一通道 255 的 3725 px = 0.4327%，
  basic 对应 2610 px = 0.3032%，leaf 独占饱和像素 1954 个；leaf 减 basic 的逐通道均值
  `-0.445/-0.408/-1.377`（leaf 整体略暗，因为均值匹配把亮部推上去的同时把暗部压下去）。

## 5. 两个地图（不是一张图碰巧）

| 地图 | instances | withCube | withCubeEmpty | noCubeInLeaf | slots | identity |
| --- | --- | --- | --- | --- | --- | --- |
| cp_process_f12 | 406 | 337 | 3 | 69 | 147 | ok |
| cp_sunshine | 420 | 346 | 3 | 74 | 162 | ok |

两张图的 leaf/立方体总量不同（3537 vs 3286 个带烘焙 leaf），槽数、命中率、空立方体数各自独立，
恒等式都成立。第二张图的捕获行与日志见 `evidence/leaf-ambient-gpu/pin-cp_sunshine-leaf-trace.log`。

## 6. 验收：同一 tick 的室内与室外

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
| 院子里（底部，室外叶） | (709,481)-(829,681) | (134.6,124.7,110.4) | (123.1,110.8,104.6) | **+11.5,+13.9,+5.8** |
| 门洞里（右侧，室内叶） | (1052,481)-(1172,681) | (90.4,76.8,67.2) | (92.1,77.4,68.6) | **−1.7,−0.6,−1.5** |

院子里的玩家在 leaf arm 明显更亮更饱和（军装的红、裤子的绿都抬起来），门洞里的几乎不动——
与两个 leaf 的 +Z 亮度（0.002354 vs 0.000105）方向一致。

**投影的诚实说明**：引擎不打印屏幕投影。本轮把 `worldCenter/worldMinZ/worldScale` 与
`forward` 加进捕获行，就是为了能自己重建（`map→box` 变换是引擎自己的：
`native/src/native_renderer_camera.cpp:57-59`，相机也在同一 box 空间，Z 朝上：up 在 :685、
`LookAtLH` 在 :715、投影在 :800，相机那一行打印在 :793-798）。重建结果在
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

| run | 臂 | 本轮 md5 | 前任 md5 | |
| --- | --- | --- | --- | --- |
| bare | （无） | 2ddade356e85dd77b795a7380c254bcb | 同 | 逐字节相同 |
| off | `--lighting off` | 2ddade356e85dd77b795a7380c254bcb | 同 | 逐字节相同 |
| basic | `--lighting basic` | d927c52639fb10c62a50b3018806a2d3 | 同 | 逐字节相同 |
| leaf | `--lighting leaf` | 83a85fd73d6dabe166bf38042c2f923f | a3f4229c48c054b8b4bf15e5860c3e43 | 0.225% 像素，见 §8 |
| leaf-trace | `--lighting leaf --lighting-trace` | 3f39236ee189629f9a5ed42c170a80da | 同 | 逐字节相同（调试视图） |
| leaf-outline | `--lighting leaf --outline on` | 本轮未采 | b4608170e6e7e6fe8262e1c831e94246 | 本轮批处理去掉了 `--outline on`（exe 不认这个参数、只是容忍），所以只有前任基线 |
| sunshine-leaf | 第二张地图，leaf+trace | 73550075d26879a40f0241824ce7bbab | 01afa412794b6a1dd83a90b05cb6b099 | 同 §8 的新旧差 |

三个"不带 leaf"的臂逐字节不变，是"新打印/新计数没改渲染"的硬证据；两条 leaf 臂的差异全部
来自前任 exe 的陈旧（§8）。

## 8. 前任的 exe 是旧的（为什么 leaf 会差 0.225%）

本轮第一次构建（09:38）的日志里**只**重编了 `generated/tf2_build_stamp.cpp` 并重链接，四个被改过的
源文件一个都没进 exe。实测原因是 `ops_edit_file` 保留 mtime：源文件的时间戳比它们的 `.obj` 更**旧**，
nmake 判定无需重编，于是一个"成功"的构建其实链接了旧对象。这条链是在 09:38 那次构建里用 `stat` 量的；
09:42 的重建把 exe 和 `.obj` 都刷新了，所以原始时间戳已不可复算——但同一轮的批处理日志
`evidence/leaf-ambient-gpu/batch-37d.log` 留下了 touch（09:41:43）与两次构建的原始记录，
`build-warnings.txt` 是它的构建段节选。

那批没进旧 exe 的改动是什么（git 可查）：`964e0d79`（Half B 本体，12 文件 +1277）里的**逐 leaf
立方体**部分——槽表、`t12` 的绑定与解绑、像素分支、三个 `resolveLeafCube()` 调用点、探针与捕获行。
前任 00:29 的 exe 早于这些编辑，所以它拍的 `leaf` 不可能含逐 leaf 替换；它 00:48-00:55 拍的 `leaf`
与 `sunshine-leaf` 用的都是那个旧 exe。

但 0.225% **不是"特性缺失"的证据**：前任 `leaf`（`a3f4229c…`）与本轮 `leaf`（`83a85fd7…`）只差
0.225%，而 `leaf` 与 `basic` 差 12.831%（`arm-diffs.txt`）——前任的 leaf 画面本来就和 basic 大不同
（那是 task-36 的全局六面光照臂，00:29 的 exe 里已有），本轮重建只是把它细化。逐 leaf 立方体与
全局立方体本来就接近（k 按均值匹配定标），所以 Half B 的净像素差只有 0.225%，且集中在一块
275×164 里，其它三个臂纹丝不动。

教训：**改完源文件必须 touch 再构建**，并且要看构建日志里到底重编了哪几个文件；
"构建成功"不等于"新代码进了 exe"。从本轮起批处理脚本在构建前统一 touch 四个源文件
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
- **clip 是方向空间的现象**：0.54 的方向均值 >1，但屏幕像素级只有 0.43% 有通道 255；
  如果以后换更亮的太阳/更暗的地图，需要重新量像素级 clip。
- 没有改光照数学、没有动 Half A 的解析/两页规则/回落计数语义。

## 10. 复现

```bash
# 1) 构建（必须先 touch：见 §8）
touch native/src/native_renderer_frame.cpp native/src/native_renderer_entity.cpp \
      native/include/native_renderer.h native/tools/leaf_ambient_probe.cpp
bash build-target.sh tf2_demo_native leaf_ambient_probe
native/build-nmake/leaf_ambient_probe.exe --self-test   # 38 项，failures=0（见 probe-rebuild.log；
#    build-warnings.txt 里那次的 34 是旧探针的读数，不是当前探针）

# 2) 捕获（全在 hostlock + verify-all 锁内，见 tools/lock-retry.sh）
EXE=native/build-nmake/tf2_demo_native.exe
PROC="E:/demo测试/cp_process_f12/STV/1338f5de79ea343682c17dd514c980d9_matcha-20260927-1312-cp_process_f12.dem"
"$EXE" --demo "$PROC" --capture-tick 600 --capture-frame .scratch/leafgpu/leaf.bmp \
       --audio-disabled --hidden-window --lighting leaf
# 同样命令换 --lighting off|basic|leaf --lighting-trace；第二张地图把 --demo 换成 SUNSHINE.dem

# 3) 表格、裁切、投影（脚本已提交在 evidence/leaf-ambient-gpu/tools/，路径按 worktree 根解析；
#    读的是第 2 步写进 .scratch/leafgpu/ 的捕获与日志）
python evidence/leaf-ambient-gpu/tools/make-tables.py        # pins-and-counters / entity-cubes / player-crops / arm-diffs
python evidence/leaf-ambient-gpu/tools/player-crop-pair.py    # accept-players-1to1.png
python evidence/leaf-ambient-gpu/tools/projection-accept.py   # 投影重建（projection-caveat.txt 的数字）
python evidence/leaf-ambient-gpu/tools/arm-cross-diff-bbox.py # bbox-diff-run.txt 的那一行
```
