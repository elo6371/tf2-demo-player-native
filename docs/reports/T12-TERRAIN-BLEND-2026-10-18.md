# T12 地形混合材质渲染 — 交付报告

- 任务：T12 地形混合材质渲染（对应 release blocker F2）
- 分支：`task/t12-terrain-blend`
- worktree：`D:\TF2_Native_Worktrees\t12-terrain-blend`
- 基线：`p0-entity-protocol@d3ab18a`
- 本轮提交：`7954527`（产品）、`0e77c49`（门禁 + 探针 + 证据）、本文件
- 日期：2026-10-18

---

## 0. 结论摘要

**根因**。`NATURE/BLEND*` 之所以不像地形，不是因为资源找不到，而是因为
`main.cpp` 把第二层用 **第一层贴图的 alpha** 当遮罩、在 CPU 上折进了第一层。
参考图的两层都是不透明 DXT1，遮罩恒为 1，于是每个置换面都被画成第二层平铺，
地图作者画在置换里的权重一个像素都没参与。而 Source 的
`WorldVertexTransition` 用的权重是 `CDispVert::m_flAlpha`——**逐顶点**的值，
它根本不可能折进贴图。

**现在**。解析器读出 `+16` 处的 alpha；第二层按第一层的 tile 网格重采样后单独
上传；像素着色器用顶点权重在两层之间 lerp，材质声明了 `$blendmodulatetexture`
时再套 `smoothstep(g - r, g + r, w)`。

**像素级证据**。固定帧上混合开启/关闭相差 **1930 个像素，1930 个全部落在地形
区域内**（区域由 `terrain` 与 `none` 两帧的差集**测出**，不是硬编码的矩形）；
`all-on` 与 `all-repeat` 逐字节相同。四个变异（不读 alpha / 不解析第二层 /
权重写常数 / modulate 不落到顶点）全部转红，源码逐字节还原。

**未完成的**。"地形看起来像 TF2 地形"这一句在本基线上**不可宣称**：F1（世界
几何）未修，T11 未合并，本 worktree 的帧仍是破碎几何 + 近乎全黑，地形只占画面
0.22%。本轮证明的是**混合链路正确且变化落在地形上**，不是**地形好看**。见 §7。

---

## 1. 任务步骤对照

| T12 步骤 | 本轮做了什么 | 证据 |
|---|---|---|
| 1 枚举 `NATURE/BLEND*` 实际用到的 VMT 字段 | 用 `vpk_query --dump` 读出三个 `WorldVertexTransition` 材质的全部字段（§4.1） | `evidence/terrain-blend/probe-cp_snakewater_final1.txt` |
| 2 按 loose → VPK → pakfile 解析全部引用资源 | `main.cpp` 抽出一个 `resolveTexture`，第一层/第二层/modulate 走**同一条**策略；探针用同一策略读 map 侧 | `blend.*.layers` = `vpk/vpk/vpk`；`materials.pakfileOnly=32` |
| 3 用当前着色器接口实现最小正确的混合输入 | 顶点加 `blend`/`blendModulate`，PS 采样 t6/t7，`lerp` + `smoothstep` | `native_renderer.cpp` 世界 PS |
| 4 下采样确定、记录源与目标尺寸 | 每次重采样打印 `source=WxH tile=WxH` | `source=1024x1024 tile=512x512`，`resampled=2` |
| 5 采地形 on/off/repeat，证明变化在地形区域 | `--world-blend on\|off`、`--world-geometry terrain\|none` 两个开关，区域由两帧差集测出 | `region.txt`、`frames/`、`frames.sha256` |
| 6 正向 + 变异检查 | 4 个变异，各自转红并逐字节还原 | `gate-mutation.log`、`mutated-m1..m4` |

---

## 2. 改动文件

| 文件 | 改动 | 原因 |
|---|---|---|
| `native/include/bsp_map.h` | `BspTriangle` 加 `aAlpha/bAlpha/cAlpha`（默认 -1） | 权重必须从解析层带到上传层；-1 是"这个顶点只有一层"的哨兵，缺失时退化成单层，而不是编一个混合 |
| `native/src/bsp_map.cpp` | 置换顶点读 `+16` 处 alpha，非有限值保持哨兵 | 20 字节/顶点：向量、距离、alpha；只读前 16 字节的实现读不到权重 |
| `native/include/vmt_material.h`、`src/vmt_material.cpp` | 解析 `$blendmodulatetexture` | Source 用 `smoothstep(g-r, g+r, w)` 重塑权重，缺了它边缘是硬切 |
| `native/src/main.cpp` | ① 统一 `resolveTexture`；② 第二层按第一层网格重采样；③ `$blendmodulatetexture` 同网格；④ 每个失败给具名原因；⑤ 两个诊断开关 | 旧代码用第一层 alpha 折图；且三种资源各自一条查找路径，材质可能从别的存储里凑出来 |
| `native/include/native_renderer.h`、`src/native_renderer.cpp` | 第二/第三张 atlas 复用第一张的 tile 矩形；顶点加两个字段；PS 分支；`WorldBlendState` 读数；`--world-geometry` 的隔离 | 一个 uv 采样三张图的前提是三张图 tile 矩形相同；计数必须能区分"权重到了"和"常数到了" |
| `native/include/texture2d.h`、`src/texture2d.cpp` | 字节上限改为参数 | atlas 按设计就是 67.6 MiB，64 MiB 的护栏在静默拒绝它，随后调用方回落到单材质贴图——一次没有任何读数附着的降级 |
| `native/tools/terrain_blend_probe.cpp`（新增） | 读 map 侧的混合资源与权重分布 | 产品计数看不到"VMT 声明了什么"和"解析器读到了什么" |
| `native/tools/vpk_query.cpp` | 加 `--dump <path>` | 列举名字不等于读到字节；只存在于归档里的 VMT 无法按路径打开 |
| `native/CMakeLists.txt` | 新增 `terrain_blend_probe` 目标 | 与产品共用 `bsp_map/world_material/vmt_material/vpk_archive/vtf_texture` |
| `terrain-blend-check.sh`（新增） | 5 段门禁 + 4 个变异 | 见 §5 |
| `evidence/terrain-blend/**` | 探针读数、产品读数、区域读数、10 张帧与哈希、四份变异读数、两份门禁日志 | 交付物 |

---

## 3. 输入、命令、退出码、可执行文件身份

| 项 | 值 |
|---|---|
| 地图 | `D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/maps/cp_snakewater_final1.bsp` |
| TF 根 | `D:/SteamLibrary/steamapps/common/Team Fortress 2/tf` |
| demo | `D:/TF2_Demo_Player/testdata/demos/bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem` |
| 抓帧点 | `--start-paused --capture-tick 0`（暂停才让 repeat 帧逐字节相同） |
| 音频设备 | `--audio-device 6 --audio-disabled` |
| 构建 | `bash build-target.sh terrain_blend_probe tf2_demo_native` → `rc=0` |
| 前向门禁 | `bash terrain-blend-check.sh` → `EXIT=0`（`TERRAIN-BLEND=PASS`） |
| 变异门禁 | `bash terrain-blend-check.sh --mutation` → `EXIT=0`（四个 `MUTATION-CAUGHT=PASS`） |
| `tf2_demo_native.exe` | 1,624,064 字节，2026-10-18 10:05，`6760a26d56000547ac916bb3563ec11cce026838c3821fdd5fb08a7768194c75` |
| `terrain_blend_probe.exe` | 433,664 字节，2026-10-18 10:12，`9ec0c22602040995f3a3232d0ab563d9259af0afab5c6205c2b86ed085ac6de9` |

两个 exe 都晚于各自依赖的最后一次源码改动（`bsp_map.cpp` 10:02、`native_renderer.cpp` 10:05）。
门禁日志：`evidence/terrain-blend/gate-forward.log`、`evidence/terrain-blend/gate-mutation.log`。

---

## 4. 读数

### 4.1 map 侧：这三个材质到底声明了什么

`cp_snakewater_final1` 的置换面用到 29 个材质，其中 3 个是 `WorldVertexTransition`：

| 材质 | `$basetexture` | `$basetexture2` | `$blendmodulatetexture` | 三层来源 | 尺寸 |
|---|---|---|---|---|---|
| `NATURE/BLENDGROUNDTOGRASS008` | `Nature/dirtground010` | `Nature/grass_08` | `Nature/grass_blendmask` | vpk/vpk/vpk | 1024²/1024²/1024² |
| `NATURE/BLENDGROUNDTOGRASS008_NODETAIL` | 同上 | 同上 | 同上 | vpk/vpk/vpk | 1024²/1024²/1024² |
| `COALMINES/BLENDGROUNDTOGRASS_COALMINES` | `coalmines/dirtground003` | `coalmines/dirtground004` | 无 | vpk/vpk/none | 1024²/1024²/none |

另外声明的字段：`%tooltexture`、`$bumpmap`（`Nature/dirtground001_height-ssbump`）、
`$bumpmap2`（仅 COALMINES）、`%detailtype`（`tf_forest_grass`）、`$ssbump 1`、
`%keywords`、`$surfaceprop`。**`$bumpmap2`/`%detailtype`/`$ssbump` 本轮不消费**，见 §7。

汇总读数（探针）：`blend.declared=3`、`blend.resolved=3`、`blend.modulateDeclared=2`、
`blend.modulateResolved=2`、`materials.resolved=144`、`materials.pakfileOnly=32`、
`disp.blendTriangles=11300`、`disp.weightedVertices=496128`、`disp.sentinelVertices=0`、
`disp.distinctWeights=10`、`disp.weightRange=0.000..1.000`。

### 4.2 GPU 侧：到达顶点的东西

```
gpu: world blend enabled=1 terrainOnly=0 textures=64 layers=2 accepted=64 acceptedLayers=2
     tiles=2 modulateTiles=2 malformedLayers=0 weightedVertices=30816 modulatedVertices=30816
     weightMin=0.000 weightMax=1.000 distinctWeights=9
gpu: world atlas size=4112 bytes=67634176 state=bound
gpu: world blend-fetch declared=2 resolved=2 refused=0 disabled=0 resampled=2
     modulateDeclared=2 modulateResolved=2 modulateRefused=0
gpu: world material-fetch distinct=148 tiled=64 capped=51 vmtMissing=1 vmtNoBase=31 ...
```

- `state=bound` 是"atlas 就是世界 pass 采样的那张"的唯一取值。此前 64 MiB 护栏
  拒绝 67.6 MiB 的 atlas，`uploadWorldGeometry` 返回 false，调用方回落到单材质贴图，
  **所有计数仍然是绿的**。
- `distinctWeights=9`、`weightMin/Max=0/1` 是"地图画的权重到了"与"常数到了"的
  唯一区别；m3 就是靠这三项转红的。
- 关闭态：`tiles=0`、`weightedVertices=0`、`fetch.disabled=2`、`fetch.resolved=0`
  ——被开关跳过的层**不算解析失败**，否则摘要行会把"我关了它"读成"资源没有"。

### 4.3 像素：变化在哪里

固定帧 1264×681（860,784 像素）：

| 读数 | 值 |
|---|---|
| 地形区域（`terrain-off` ⊕ `none`） | 13,714 像素（1.6%） |
| `all-on` ⊕ `all-off` | **1,930** 像素（0.22%），最大通道差 59/255，变化像素均差 7.9 |
| 其中落在地形区域内 | **1,930（100%）** |
| 区域外 | **0** |
| `all-on` ⊕ `all-repeat` | **0** |
| `terrain-on` ⊕ `terrain-off` | 3,535 像素，全部在区域内 |

变化幅度小（均差 7.9/255）不是链路问题，是画面问题：本基线的世界帧近乎全黑
（F1），地形只占 0.22%，两层贴图的差异被压在这条带子里。§7 第 1、5 条。

### 4.4 帧哈希

```
a059c39744582bc70ab9ce5b74b830b1d46c2786e69e5ea0ba21e04786840bbd  all-off.bmp
ba9676def721320c267ee1eefaee25fe55f0385586da611f1c9f49d5342e0d57  all-on.bmp
ba9676def721320c267ee1eefaee25fe55f0385586da611f1c9f49d5342e0d57  all-repeat.bmp
c04021f716da86dc61d30c8115703ce97ad037f762c3e8f7b291b9b056cfecfc  none.bmp
fccee3b78c0dca03616a16d17d6f32548b7dfe42546573563543ec55b62f6f31  terrain-off.bmp
56033aebb3986363c3d42ff63bb64e4f4746773ca9266c12da9db630c99ff688  terrain-on.bmp
```

`all-on` 与 `all-repeat` 哈希相同即 repeat 逐字节相同。这六个哈希在三次独立运行
（前向两次、变异一次）中完全一致，跨构建的确定性成立。

---

## 5. 变异

`bash terrain-blend-check.sh --mutation`，四个缺陷各自必须转红：

| 变异 | 缺陷 | 转红依据 |
|---|---|---|
| m1 | 置换顶点不读 `+16` 处的 alpha | `weightedVertices=0`、`sentinelVertices=496128` |
| m2 | 不解析 `$basetexture2` | `blend.declared=0`，而 shader 仍是 `WorldVertexTransition` |
| m3 | 上传时把每个顶点权重写成 1 | `distinctWeights=1`、`weightMin=1`，而 `weightedVertices=30828` 不变 |
| m4 | modulate 取到了但 flag 不落到顶点 | `modulateTiles=2` 保持、`modulatedVertices=0`、`weightedVertices=30816` |

m3 的读数是本轮自己改出来的：第一版在**三角形**上数权重，所以"顶点权重写常数"
一个读数都动不了（`distinctWeights` 仍是 9）——计数器读错了对象，等于没有计数器。
现在按**顶点**数，因为到着色器的是顶点。

每个变异都在断言之后 `cp` 回备份、重编、`cmp` 逐字节比对、删备份；任何一步失败
直接 `TERRAIN-BLEND-MUTATION=FAIL` 退出。四次还原全部 `cmp` 通过，备份已删净
（`ls .scratch/terrain-blend/*.backup` 为空）。

---

## 6. 证据、工作区、残留进程

- 证据：`evidence/terrain-blend/`
  - `probe-cp_snakewater_final1.txt`（93 条读数）、`product-readings.txt`、`region.txt`
  - `frames/*.bmp` + `frames.sha256`
  - `mutated-m1..m4-*.txt`
  - `gate-forward.log`、`gate-mutation.log`
- 工作区：提交后 `git status --short` 为空（`.scratch/` 被忽略）。
- 残留进程：**本 worktree 无残留**。宿主上有一个 `tf2_demo_native.exe`（PID 42508，
  10:18 启动），路径是 `D:\TF2_Native_Worktrees\t11-world-geometry\...`，属于 T11
  的 worktree，**未触碰**。

---

## 7. 仍未关闭的限制

1. **视觉验收等 T11。** F1 未修：本 worktree 的基线是 `d3ab18a`，不含
   `task/t11-world-geometry`（`a6e0b74`）。世界帧仍是破碎几何 + 近乎全黑，
   地形只占 1.6% 的画面。本轮能证"变化在地形上"，不能证"地形看起来像 TF2 地形"。
   合并 T11 后必须重采 `evidence/terrain-blend/frames/` 并重跑本门禁。
2. **近景相机在基线上拍不出东西（已实测，未进产品）。** 本轮试过让探针输出
   "最大混合三角形的质心 + 朝上法线"，用它生成 `--debug-camera` 拍地形近景。
   实测：该位置渲染出**纯清屏色**（单色帧），`--no-world-vis-clip`、
   `--no-world-cull`、`--world-geometry terrain` 三种组合都是单色；
   而 `--debug-camera 0 0 2000 85 0` 俯拍能出图，图上是堆叠的碎片。
   即：几何的**空间位置**本身不可信（F1），任何"按地图坐标站过去看地形"的方案
   在这条基线上都不成立。改动已完整回退，探针读数与回退前逐字节一致。
3. **拒绝路径有名字，但没有被执行。** 参考图上 `refused=0`
   （`blend-fetch refused=0 modulateRefused=0`），所以"第一层没解析出来""第二层
   从三个存储里都找不到""不是可读的 VTF""解出空""重采样失败""modulate 无第二层
   可调制"这六条具名原因**只存在于代码里，没有任何一次运行走到**。
   下一步应加一个畸形混合资源夹具（合成 VMT/VTF），把它变成读数。
4. **64 tile 上限截掉了三个混合材质中的一个。** `distinct=148 tiled=64 capped=51`，
   `COALMINES/BLENDGROUNDTOGRASS_COALMINES` 在 51 个被截掉的材质里，
   所以它在画面里不是地形而是哈希色。这不是本轮引入的，但 F2 的"地形材质正确"
   在整图范围内仍不成立。
5. **`$bumpmap2`、`%detailtype`、`$ssbump` 未消费。** 第一层的 `$bumpmap` 走既有
   的 bump 路径；第二层的 bump、细节贴图（`tf_forest_grass`）和自阴影 bump
   都没有实现。地形在近处会缺细节与法线层次。
6. **混合权重只有顶点 alpha 一条输入。** 没有实现 `$blendmodulate` 的
   `$blendmodulate` 之外的 Source 变体（例如三/四层 `WorldVertexTransition` 的
   扩展、`$color2` 调制）。

---

## 8. 复核方式

```bash
cd /d/TF2_Native_Worktrees/t12-terrain-blend
bash build-target.sh terrain_blend_probe tf2_demo_native
bash terrain-blend-check.sh              # 期望 TERRAIN-BLEND=PASS, exit 0
bash terrain-blend-check.sh --mutation   # 期望四个 MUTATION-CAUGHT=PASS, exit 0
```

单看一帧：

```bash
./native/build-nmake/tf2_demo_native.exe --tf-root "<tf>" --audio-disabled \
  --hidden-window --demo "<demo>" --start-paused --capture-tick 0 \
  --capture-frame out.bmp --world-geometry terrain --world-blend on
```

`--world-geometry terrain` 只上传混合材质的面，`none` 什么都不上传；
两帧之差就是地形区域，`--world-blend off` 只抽掉第二层。三个开关互不替代。
