# T105 — 世界视觉收尾：实体二次染色 / 玻璃白噪 / 近黑面归属 / 暗像素定性

- 日期：2026-11-16（读数时间 2026-11-17 上午）
- worktree：`D:/TF2_Native_Worktrees/t105-worldfix`，分支 `work/world-visual-fix`，base `cd04373f`
- 状态：**已实现、已测量**。三条提交（A 修复 / 参数拒绝 / 本文件与 B、C、D 读数）见第 6 节；闸门日志路径逐条列在对应小节。
- 证据目录：`.scratch/worldfix/`（git 忽略），三条主日志
  `.scratch/worldfix/final-gates.log`（A 的两道门 + 参数门，本次复跑）、
  `.scratch/worldfix/cd/report.txt`（C 的 pass 归属 + D 的逐点光图读数）、
  `.scratch/worldfix/grid/report.txt`（实体颜色探针）、
  `.scratch/worldfix/d2/report.txt`（D 的同材质别处采样，19 暗 / 157 亮）。

## 0. 一页结论

| 项 | 问题 | 读数（一句话） | 结论 | 状态 |
| --- | --- | --- | --- | --- |
| A | 实体模型被上第二层队伍色（皮肤族已自带队伍色） | 正常帧 379,038 个被染色像素与 raw texel 逐通道 `withinTol=379038 exceed=0 maxDiff=0` | 修好：painted 网格画白色，队伍色只留给无贴图的兜底网格 | 已修，三道门绿 |
| — | 未知 `--flag=value` 静默忽略 | `--world-flat=1` 现在 `exit=13`，trace 打 `args: refused switch count=1 first=--world-flat=1`，且不出帧 | 改成按名字拒绝 + 非零退出；裸开关与位置参数行为不变 | 已修，门绿 |
| B | 玻璃窗白噪 | 玻璃 tile 字节 == 同 VTF mip0 重解字节（`agreeDecode=1 agreeTile=1`，校验和相同），选中 mip 级别 `decodeMip=0`、`mipEquiv=0`、`downscale=1x1` | **负结果**：白噪不在取图/解码/降采样链路上；不再投入修复时间 | 已定论 |
| C | (470,280) 近黑像素 `depthMatched=0`（不是世界 BSP） | pass-depth：世界批次与 `30-world-done` 在该点 depth=0.992294，`50-entities` depth=0.991516 == 帧 depth；entity-id 给 `(255,255,255)`；`--entity-tint-mutation` 不改变该点，而玩家像素 597,190 会变 | 是**实体 pass 里的静态道具**（不走队伍色染色），自身 albedo 就是 `13,13,13`；不是 BSP，也不是世界光照变暗 | 已定论 |
| D | 画了但暗的像素里「贴图正常、输出暗」那一拨 | 本帧 12,211 个暗像素中 6,798 是 albedo 黑、5,413 是 albedo 正常；后者的光图采样 `lightrgb` 2..10、`lightfactor` 0.09..0.19，且 `lightbranch=texture`（不是零纹理/不是 fallback）；同一材质别处 `lightrgb` 到 113 | 是**烘焙光图的暗采样（阴影/衰减）**，不是解码或绑定缺陷 | 已定论 |

## 1. A — 实体二次染色

### 1.1 机制

- 画实体的地方（`native/src/main.cpp`，T60 注释上方那段）以前对**每一个**实体实例都写一个队伍色：红 `0.86,0.28,0.22`、蓝 `0.28,0.52,0.86`、其它 `0.72,0.74,0.70`。
- 像素着色器把 `colour.a == 1` 的顶点色当作乘法因子（`albedo = sampled * colour.rgb`）。而皮肤族本身已经编码了队伍色（family 0 = 红，family 1 = 蓝，见同处 T60 注释），所以 painted 网格被乘了两次：整只玩家变成单一色调的剪影。
- 实测确认：正常帧的像素 = raw texel × 队伍色（比值就是那三个通道）。

### 1.2 修法

- 给每个实例算 `entityIsPainted`：准备阶段每画进一个贴图槽就记一次数（`entityPaintedSlotCountByKey`，按 cacheKey；查不到再按归一化模型路径查 `entityPaintedSlotCountByNormalizedPath`，因为 demo 的拼写与准备后的拼写两边都会到这里）。
- painted 且不是负控时：`draw.color = (1,1,1,1)`，并把 sampled albedo 原样放过去。
- 只有 painted 槽为 0（真没有贴图）的网格才保留队伍色兜底；预算延迟（`budgetDeferred`）的实例也算「这帧什么都没画」，照样写白，并单独计数 `deferred`，不让它污染 white/fallback 两个读数。
- 负控 `--entity-tint-mutation`：把旧行为装回去（painted 网格也乘队伍色），并让 trace 打 `mutation=1`，这样「绿」不可能来自一个负控根本没跑过的运行。
- 账本：`gpu: entity tint white=N fallback=N deferred=N mutation=0|1`，另有每个 key 的 fallback 行（按实例数排序，最多 8 行），让非零 fallback 说出是哪个模型。

### 1.3 三道门

闸门 `world-entity-tint-check.sh`（像素判据在 `entity-tint-pixels.py`），日志 `.scratch/worldfix/final-gates.log`。

1. **像素**（整条玩家轮廓，不是三个点）：normal 帧与 raw texel 视图逐通道比较，mask = 被染色触碰过的像素（`|normal-mutation| > 2`）——
   `mask pixels=379038 bbox=0,0,1126,681`、
   `normal-vs-texel pixels=379038 withinTol=379038 exceed=0 maxDiff=0 mean=(87,87,91)`。
   对照的 sentinel 集（全帧）另有 `normalExceed=5506`，那是静态道具（不走实体染色 claim），只作参考，不是本 claim 的判据。
2. **计数**：正常运行 `gpu: entity tint white=61 fallback=0 deferred=35 mutation=0`——white>0、fallback=0、mutation=0。
3. **负控**：`--entity-tint-mutation` 运行必须 `white=0 fallback>0 mutation=1`，并且像素断言必须变红——
   `mutation-vs-texel pixels=379038 withinTol=0 exceed=379038 maxDiff=184`，
   举例 `offender x=982 y=609 normal=(255,174,74) texel=(255,174,74) mutation=(71,90,64) diff=184`。

### 1.4 复跑

```sh
bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run t105-worldtint \
  bash -c 'export HOLD_LOCK=1 EXPECT_OWNER=t105-worldtint; bash world-entity-tint-check.sh'
```

## 2. 参数拒绝（未知 `--flag=value` 不再静默忽略）

- 缺陷：产品对多余参数一律「记下来、继续画」（D-23）。T105 的缺陷排查里就踩过：`--world-flat=1`（给无值开关加了值）跑了四次「不同诊断模式」，四帧逐字节相同，产品一句话都没说。
- 修法：参数循环结束后，所有 `--` 开头且没被认出的参数**按名字拒绝**，退出码 `13`，trace 打一行，并且在出帧之前返回：
  `args: refused switch count=%zu first=%s`。
- 不动的部分：裸开关（`--world-flat`）、位置参数（`randomword`）照旧记录并继续，后者仍是 D-23 行为。
- 为什么不用新的 `else if`：那条 else-if 链已经到了 MSVC 的块嵌套上限（C1061），所以两个无值开关用 `claimValueLessSwitch` 在 fall-through 分支里按名字认领（`native/src/main.cpp:308`）。
- 门：`argument-refusal-check.sh`（日志同上）。读数：
  - `--world-flat=1` → exit=13，无帧，`args: refused switch count=1 first=--world-flat=1`；
  - `--not-a-real-switch` → exit=13，`first=--not-a-real-switch`；
  - `--world-flat`（裸）→ exit=0 且出帧；
  - 位置参数 `randomword` → exit=0。

## 3. B — 玻璃白噪：tile 字节 vs 同 VTF mip0 重解（负结果）

### 3.1 仪器

`--world-tile-fill`：每取到一个世界材质，打印一行（`native/src/main.cpp` 的 `traceWorldTileFill`）：
材质名、vmt/vtf 路径、VTF 存储、格式（数字 + Source 枚举名）、`mips`、`decodeMip`（=0，`decodeRgba` 总是解顶层 mip）、`mipEquiv`（tile 相当于第几级 mip；-1 = 非 2 的幂的重采样）、`downscale`（src/tile 的整数比）、`src` 尺寸、`decodedBytes`/`decodedChecksum`、同字节**重解**的 `redecodeChecksum`、`tile` 尺寸/`tileBytes`/`tileChecksum`、同一次下采样**重跑**得到的 `retileChecksum`、`agreeDecode`/`agreeTile`，以及两侧的近黑/近白像素数。

- 重解走的是产品自己的解码器（`texture.decodeRgba(bytes)`）与产品自己的下采样 lambda（传进 `traceWorldTileFill` 的那个 `std::function`），所以这是「同一份代码再跑一遍」的对照，不是第二个实现的意见。
- 玻璃是这个问题的对象，因为玻璃材质的 uv 密度高（`$transform` 缩放 + 旋转），最容易在 mip/降采样上先出问题。

### 3.2 读数（upward，默认相机，tick 0；`.scratch/worldfix/verify/up0-normal.trace`）

玻璃五条，全部一致：

```
GLASS/GLASSWINDOW005A          format=15(DXT5) mips=9 decodeMip=0 mipEquiv=0 downscale=1x1 src=256x256 decodedBytes=262144 decodedChecksum=ea09d875 redecodeChecksum=ea09d875 tileBytes=262144 tileChecksum=ea09d875 retileChecksum=ea09d875 agreeDecode=1 agreeTile=1
maps/pl_upward_f12/glass/glasswindow001a_*  format=15(DXT5) mips=8 decodeMip=0 mipEquiv=0 downscale=1x1 src=128x128 decodedChecksum=563104a5 = redecode; tileChecksum=563104a5; decodedDark=1008
maps/pl_upward_f12/glass/glasswindow001b_*  format=15(DXT5) mips=8 decodeMip=0 mipEquiv=0 downscale=1x1 src=128x128 decodedChecksum=8273fdc5 = redecode; decodedDark=112
maps/pl_upward_f12/icarus/glasschrome003_*  format=15(DXT5) mips=9 decodeMip=0 mipEquiv=0 downscale=1x1 src=256x256 decodedChecksum=a6e89dc5 = redecode; tileChecksum=a6e89dc5
```

全帧统计：84 条 tile-fill 行，`agreeDecode=1` 84 条、`agreeTile=1` 84 条，无一条不同。mip 分布：`mipEquiv` 0 有 19 条、1 有 64 条、2 有 1 条（`downscale` 对应 19 条 1x1、64 条 2x2、1 条 4x4）。即：**每一块 tile 都等于同 VTF mip0 解出来的字节（或在需要时由它们按整数比降采样一次），没有一条来自更粗的 mip、没有一条与重解不一致**。

### 3.3 结论

- 选中的 mip 级别是 **0**（`decodeMip=0`），玻璃 tile 的 `mipEquiv` 也是 **0**，`downscale=1x1`：玻璃窗上采样到的就是资产自己的 mip0，字节数、校验和与重解逐字节一致。
- 所以「玻璃白噪」**不在取图 → 解码 → 降采样这条链路上**；它是地图资产自己的像素（或者着色端的反射/高光），不是本任务的取图缺陷。按负结果处理，不再花修复时间。

## 4. C — (470,280) 近黑像素是谁画的：实体 pass 的模型面

### 4.1 仪器

`--capture-pass-depth <prefix>`：每过一个 pass 就写一张 16 位深度 PGM（`native/src/native_renderer_capture.cpp:97` 起，调用点 `native_renderer_frame.cpp:213/575/734-739`），命名 `<prefix>-<index>-<name>.pgm`。后写的平面**继承**前面的深度缓冲，所以「哪个 pass 画的」= 该平面与前一平面相比深度**第一次变化**的那个 pass。

### 4.2 读数（默认相机，tick 0；`.scratch/worldfix/cd/report.txt`）

点 (470,280)：帧像素 `13,13,13`。

| 平面 | 该点 depth |
| --- | --- |
| 0 `00-cleared` | 1.0 |
| 1 `10-sky` | 1.0 |
| 2..5 `20-world-batch0{0..3}` | 0.992294 |
| 6 `30-world-done` | 0.992294 |
| **7 `50-entities`** | **0.991516**（== 帧 depth 0.991516） |
| 8 `60-overlay` | 0.991516 |
| 9/10 `70-hud-pre`/`80-post-viewmodel` | 0.991516 |

世界批次在该点画到 depth=0.992294 的面是 `WOOD/WOOD_FLOOR002` tri=43696；实体 pass 在它**前面**（更近）画了一层，深度 0.991516 与最终帧一致。

### 4.3 进一步：这个实体面是静态道具，不是玩家，也不是世界面

同一相机、同一 tick 下把各诊断视图摊开（`python world-dark-attribution.py modes .scratch/worldfix`）：

```
points   normal          texel           mutation        coverage        entityid        albedo
470,280  (13, 13, 13)    (13, 13, 13)    (13, 13, 13)    (255, 0, 255)   (255, 255, 255) (13, 13, 13)
597,190  (51, 12, 10)    (51, 12, 10)    (14, 6, 9)      (255, 0, 255)   (255, 255, 255) (51, 12, 10)
512,178  (14, 8, 6)      (60, 45, 41)    (14, 8, 6)      (89, 158, 97)   (89, 158, 97)   (60, 45, 41)
447,173  (15, 11, 8)     (97, 82, 70)    (15, 11, 8)     (89, 158, 97)   (89, 158, 97)   (97, 82, 70)
899,2    (19, 12, 11)    (116, 75, 66)   (19, 12, 11)    (89, 158, 97)   (89, 158, 97)   (116, 75, 66)
877,100  (8, 5, 5)       (80, 68, 62)    (8, 5, 5)       (89, 158, 97)   (89, 158, 97)   (80, 68, 62)
```

- 判据一（哪些像素归实体 pass）：`--world-entity-coverage` 给品红 `255,0,255` 的只有 470,280 与 597,190；其余四点是世界调试的平色 `89,158,97`，不是实体像素。
- 判据二（玩家还是道具）：`--entity-tint-mutation` 会把旧行为（每个实例乘队伍色）装回去。597,190 由 `51,12,10` 变 `14,6,9`——这是玩家，染色路径确实到达它；470,280 在 normal / texel / mutation 三份里**完全相同** `(13,13,13)`——同一个 pass 里另一条不走队伍色的绘制路径，也就是静态道具。
- 判据三（暗的原因）：`--world-albedo` 在 470,280 仍是 `13,13,13`，即该模型面自己的贴图就是这个亮度；它挡住的木地板光图是 `lightrgb=113,101,73`、`lightfactor=0.6378`、`lightbranch=texture`（亮的）。

### 4.4 结论

(470,280) 是**实体 pass 画的静态道具**，不是世界 BSP（与 lead 的 `depthMatched=0` 一致），也不是玩家（染色负控不动它）。它近黑是自身贴图就黑，世界面在它身后是亮的——这里没有需要修的世界光照缺陷。

同一次测量的其它点（447,173；512,178；877,100；899,2）在 world-batch 与 entity 平面深度相同（都是世界面，前面没有更近的实体），它们的暗属于第 5 节的烘焙光图。

## 5. D — 「画了但暗」的两拨像素

### 5.1 分拨

定义（本帧、默认相机、tick 0）：暗 = 帧亮度 < 16；albedo 黑 = `--world-albedo` 帧同像素亮度 < 16；albedo 正常但输出暗 = 帧暗且 albedo 亮度 ≥ 40。

本帧（1264x681，默认相机，tick 0，同一份 seeded settings；normal = `.scratch/worldfix/verify/up0-normal.bmp`，albedo = `.scratch/worldfix/cd/albedo.bmp`）：

```
dark luma<16        12,211
albedoBlack          6,798   (同像素 albedo 亮度 < 16)
albedoNormalDark     5,413   (帧暗且 albedo 亮度 >= 40)
```

比例 6,798 : 5,413 ≈ 56 : 44，与几何卡那一帧的 6,072 : 4,219（≈ 59 : 41）同形；绝对数不同是因为帧不同（相机与阈值不同），不是分类不一致——见第 7 节。

采样：`--world-hole-trace` 在 64px 网格上取 231 点（每格取中位像素），其中 176 点真打在世界面上；按上面的定义，19 点是「albedo 正常但输出暗」，157 点亮。19/176 ≈ 10.8%，与 5,413/861,489 ≈ 0.6% 的整帧占比不同是采样方式造成的（网格点优先落在有面的格子，暗区在画面下半部更密），本文只用它做同材质对照，不当整帧比例。

### 5.2 光图读数

19 个暗点与 157 个亮点的光图采样（`.scratch/worldfix/d2/report.txt`；`lightfactor` 就是 `constants.lightmapFeatures.x` → `worldShaderConstants_.lightmapFactor`，`native_renderer_frame.cpp:437/501`）：

```
DARK lightLuma min=0.0 med=4.5 max=27.0 factor min=0.0000 med=0.1523 max=0.3561 branch=texture:17,texture-zero:1,texture-zero-nbr:1
LIT  lightLuma min=0.0 med=130.6 max=255.0 factor min=0.0000 med=0.7294 max=1.0000 branch=texture:138,texture-zero:15,texture-zero-nbr:4
```

- 暗点的 `lightbranch` 17/19 是 `texture`——光图纹理取到了、值本身就是接近 0；另外 2 条是 `texture-zero` / `texture-zero-nbr`（该 texel 及其邻居就是 0），仍然是烘焙数据，不是「没找到光图 / 没绑定」的 `no-atlas` 或 `factor` 分支。
- 亮点的中位 `lightfactor` 是 0.7294，暗点是 0.1523：同一张光图、同一个采样函数，差别只在采样位置。

### 5.3 同一材质在别处的采样

`python world-dark-attribution.py join ...` 把 trace 里的「实际画到的那张面」（`drawn=face=<id> material=<name> tri=<n>`）与暗/亮分拨按材质名对齐，**同一材质两端都采到**的有 5 种（共 9 种材质、176 个面）：

| 材质 | 暗点 | 暗点光图 | 亮点 | 亮点光图 |
| --- | --- | --- | --- | --- |
| COALMINES/RED_BEAM1 | 831,246 (2,2,2) albedo=(93,80,74) | lightrgb=(0,0,0) luma=0.0 factor=0.0000 branch=texture-zero-nbr | 718,215 (164,102,79) albedo=(164,102,79) | lightrgb=(56,62,67) luma=61.1 factor=0.5242 branch=texture |
| COALMINES/RED_BEAM2_MIDDLE | 749,179 (13,9,7) albedo=(67,53,50) | lightrgb=(7,5,4) luma=5.4 factor=0.1713 | 1088,288 (109,90,82) albedo=(109,90,82) | lightrgb=(255,255,255) luma=255.0 factor=1.0000 |
| WOOD/WOOD_FLOOR002 | 447,175 (14,11,8) albedo=(95,80,70) | lightrgb=(4,3,2) luma=3.1 factor=0.1315 | 437,167 (98,82,70) albedo=(98,82,70) | lightrgb=(255,255,255) luma=255.0 factor=1.0000 |
| COALMINES/BLENDGROUNDTOWALL_COALMINES | 840,245 (16,10,7) albedo=(117,90,71) | lightrgb=(3,2,1) luma=2.1 factor=0.1079 | 204,477 (71,62,54) albedo=(71,62,54) | lightrgb=(168,183,181) luma=179.7 factor=0.8477 |
| COALMINES/RED_WALL4_MIDDLE | 899,3 (18,12,10) albedo=(119,76,65) | lightrgb=(4,4,4) luma=4.0 factor=0.1513 | 876,23 (96,65,53) albedo=(120,84,74) | lightrgb=(145,137,114) luma=137.0 factor=0.7404 |

最直观的一组是 `WOOD_FLOOR002`：447,175 与 437,167 相距 8 个像素、同一个三角形族（tri 20428 vs 4643）、albedo 几乎相同（95,80,70 vs 98,82,70），输出却是 `14,11,8` vs `98,82,70`——差别只在光图：`lightrgb=(4,3,2) factor=0.1315` vs `(255,255,255) factor=1.0`。

### 5.4 结论

「贴图正常、输出暗」那一拨 = **烘焙光图的暗采样**：`frame ≈ albedo × lightmap(lightrgb, lightfactor)`，例如 119,76,65 × 0.1513 → 18,11,10（实测 18,12,10），93,80,74 × 0.0 → 0（实测 2,2,2，光图恰好为 0）。

- 不是材质/albedo 缺陷：同一材质同一 texel 在别处输出正常（5.3 的五行）。
- 不是解码/格式/绑定缺陷：`lightbranch` 在 19 个暗点里 17 个是 `texture`（取到纹理、值为 0..10），另 2 个是光图数据本身为零；没有 `no-atlas` 或 `factor` 兜底分支。
- 是阴影/衰减本身：这些点位于梁下、墙角、地板接缝，光图在那里烘焙出 0.0..0.36 的强度。按「地图资产/光照烘焙」定性，本轮不改。

这两拨像素（6,798 albedo 黑 / 5,413 光图暗）与 C 的 (470,280) 是同一族「画面黑但链路正常」的读数：C 是模型贴图黑，D 是烘焙光图黑。

## 6. 提交与复跑

分支 `work/world-visual-fix`，base `cd04373f`：

1. `adce1061` `fix(world-entity-tint): painted meshes draw white; the team colour is only an untextured fallback`——A 的修复，含 `entity-tint-pixels.py`、`world-entity-tint-check.sh`；tile-fill 读数用的匿名命名空间辅助函数因为插入点与参数循环相邻（MSVC C1061）一并落在这里，`--world-tile-fill` 不开时是死代码，提交消息里写明了。
2. `97ad8917` `fix(cli): refuse an unrecognised --switch by name instead of drawing on`——参数拒绝 + `argument-refusal-check.sh`。
3. 本文件 + 第 3 节的 tile-fill 调用点 + `world-dark-attribution.py`（C/D 读数的复跑仪器）。

复跑：

```sh
# A 的两道门 + 参数门（同一把主机锁，约 3 分钟）
bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run t105-worldtint \
  bash -c 'export HOLD_LOCK=1 EXPECT_OWNER=t105-worldtint; bash world-entity-tint-check.sh; bash argument-refusal-check.sh'

# C/D 的读数后处理（纯离线，不占锁）
python world-dark-attribution.py modes .scratch/worldfix
python world-dark-attribution.py join .scratch/worldfix/verify/up0-normal.bmp \
  .scratch/worldfix/cd/albedo.bmp .scratch/worldfix/d2/d2.trace
```

C/D 的原始采集命令（需要主机锁，产品要跑两趟）：

```sh
bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run t105-worldcd bash .scratch/worldfix/cd-attribution.sh
bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run t105-worldd2 bash .scratch/worldfix/cd-d2.sh
```

### 6.1 本轮门禁汇总（2026-11-17）

| 门禁 | 结论 | 证据 |
| --- | --- | --- |
| `world-entity-tint-check.sh` | PASS（A 的三道门） | `.scratch/worldfix/final-gates.log` |
| `argument-refusal-check.sh` | PASS | `.scratch/worldfix/final-gates.log` |
| `frame-capture-check.sh` | PASS | `.scratch/worldfix/tally2.log` |
| `world-material-check.sh` | FAIL，**base 上同样红，不是本轮引入** | `.scratch/worldfix/tally2.log` |
| `world-geometry-hole-check.sh` | FAIL，本机缺 demo | `.scratch/worldfix/tally2.log` |
| `world-render-reliability-check.sh` | FAIL，本机缺 `SUNSHINE.dem` | `evidence/t8-render-reliability-20261117101825-20636-forward/sunshine.log` |

- world-material 的三条失败断言都是 pvs 派生读数：`render vis visible from camera = 858, expected 1481`、`hidden triangles = 25995, expected 15039`、`drawn triangles = 216114, expected 227070`；同一段 trace 里 `cameraCluster=2108`、`clusteredTriangles=34517`、`inputTriangles=242109`、`clusters=2940` 仍在 pin 上。
- 差分：在干净的 `cd04373f` 临时 worktree（验后已删）重建产品，跑同一段第 6 节命令，读数是逐字相同的一行 `cameraCluster=2108 visibleFromCamera=858 hiddenTriangles=25995 inputTriangles=242109 drawnTriangles=216114`（`.scratch/basecheck/base.trace` 与 `branch.trace`，两次 rc=0）。所以这三条 pin 在 base 上就是红的，本轮没有碰它。
- 代码侧旁证：`git diff cd04373f -- native/` 只有 `native/src/main.cpp`，新增行只出现 `entityTint*`、`tileFill*`、`unknownSwitch*` 三族标识符，世界 vis / cull / pvs 一行没改。
- 两个缺 demo 的失败是环境：`world-geometry-hole-check.sh:60` 要 `$TF/demos/autorecord_2026-07-02_13-26-46.dem`，`world-render-reliability-check.sh:83` 要 `.../tf/demos/SUNSHINE.dem`，本机 `tf/demos` 下都没有；两次运行的 trace 里 `args: unrecognized=0 first=- all=-`，不是本轮新加的参数拒绝把它们打死的。

## 7. 没做的

- 4,219 / 6,072 / 10,291 这组绝对计数来自几何卡自己的那一帧（相机与阈值不同），本文的分拨是本帧自己的定义与读数；比例相同（约 55/45），绝对数不同是帧不同，不是分类不一致。
- 静态道具 vs 刷模型的命名：`--capture-pass-depth` 把它归到「实体 pass」，entity-id 给 `(1,1,1)`、染色负控不动它，据此命名为**静态道具**；逐像素的模型名 trace 不存在（也没有 `--static-prop-budget` 这个开关），具体是哪一个 prop 模型本文没有命名。
- 七张地图的逐图材质普查（`.scratch/worldfix/materials-maps.sh`，7 次产品启动）没有跑：第 3 节的 B 读数只来自 pl_upward_f12。
- world-material 的三条 pvs 派生 pin 需要单独重定标（读数见 6.1），不在本轮范围；几何洞门与渲染可靠性门要在有对应 demo 的机器上跑，或把门里写死的 demo 路径指到本地存在的素材（本机 `E:/demo测试/cp_sunshine` 下有本地 cp_sunshine 素材）。
- `evidence/t8-render-reliability-20261117101825-20636-forward/` 是失败那次的生成物，未提交，留着给 6.1 的引用落地。
