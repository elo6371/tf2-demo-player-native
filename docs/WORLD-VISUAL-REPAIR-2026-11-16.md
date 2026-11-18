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
| `world-entity-tint-check.sh` | PASS（本机复跑：379038 个 tint 像素全部等于 raw texel，mutation 对照红） | `.scratch/worldfix/tint.log` |
| `argument-refusal-check.sh` | PASS（本机复跑：未知 `--switch` 按名字拒绝并非零退出，已知开关与位置参数行为不变） | `.scratch/worldfix/argref.log` |
| `frame-capture-check.sh` | PASS | `.scratch/worldfix/tally3.log` |
| `world-material-check.sh` | PASS（重定标后；因果见 6.3） | `.scratch/worldfix/mat.log` |
| `world-geometry-hole-check.sh` | 跑起来了但不算绿：地图无关断言全过，pl_odyssey 的像素 pin 不成立（见 6.4） | `.scratch/worldfix/geo2.log` |
| `world-render-reliability-check.sh` | PASS（48 项，0 失败；用本机 SUNSHINE 副本，见 6.4） | `.scratch/worldfix/rel2.log` |

- 本机在这棵已提交的树上把五道快门分别复跑（`tally3.log` 加各自单独复跑 `rel2.log` / `tint.log` / `argref.log`）：`world-material`、`frame-capture`、`world-render-reliability`、`world-entity-tint`、`argument-refusal` 全 PASS；`world-geometry-hole` 只能跑到「地图无关断言全过」为止（6.4）。
- 汇总时可靠性门的控制输入有一次 `exit_code=-1`（`tally3.log`），当时同机另一个产品实例在跑；同一棵树单独复跑 `rel2.log` 是 `checks=48 fail=0`，所以那次读数作废、以单独复跑为准。
- 汇总顺序也踩了一个坑：`world-entity-tint-check.sh` 与 `argument-refusal-check.sh` 自己会去抢主机锁（`LOCK=`），把它们塞进外层 `hostlock run` 会互相等死；这两道门必须单独跑，只有 `world-material` / `frame-capture` / `world-render-reliability` 能共用一把锁。
- 先红的是三条 pvs 派生断言：`render vis visible from camera = 858, expected 1481`、`hidden triangles = 25995, expected 15039`、`drawn triangles = 216114, expected 227070`；同一段 trace 里 `cameraCluster=2108`、`clusteredTriangles=34517`、`inputTriangles=242109`、`clusters=2940` 仍在 pin 上。
- 这两次红都不是本轮代码：`cd04373f` 干净 worktree 的读数逐字相同（`.scratch/basecheck/{base,branch}.trace`，两次 rc=0），两构建差分把它们指到 `10d13559` 的 vis 解码修复；重定标后本轮复跑 `WORLD-MATERIAL=PASS`（`.scratch/worldfix/mat.log`，含 `OK render vis drawn + hidden = input (216114 + 25995 = 242109)`）。完整因果见 6.3。
- 代码侧旁证：`git diff cd04373f -- native/` 只有 `native/src/main.cpp`，新增行只出现 `entityTint*`、`tileFill*`、`unknownSwitch*` 三族标识符，世界 vis / cull / pvs 一行没改。
- 两个缺 demo 的失败是环境：`world-geometry-hole-check.sh:60` 要 `$TF/demos/autorecord_2026-07-02_13-26-46.dem`，`world-render-reliability-check.sh:83` 要 `.../tf/demos/SUNSHINE.dem`，本机 `tf/demos` 下都没有；两次运行的 trace 里 `args: unrecognized=0 first=- all=-`，不是本轮新加的参数拒绝把它们打死的。两道门现在都有显式覆盖口、缺文件时打印 SKIP 而不是 FAIL，并且各自用本机存在的 demo 跑过（见 6.4）。

### 6.2 读数的适用范围（交叉引用）

- 6.1 里 `world-material-check.sh` 的天空读数（`vertices=36 triangles=12 faces=6 draws=1 side=512`）是产品在 `--quality standard` 下画出来的：这道门自己就用这个开关启动（`world-material-check.sh:536`、`:539`）。产品默认安装不画天空——`native/src/settings_store.cpp:61` 把 `preset` 默认读成 0，`:62-63` 落成 `QualityPreset::Performance`，`:82` 只有 standard 才 `result.render.skybox = standardQuality`（=true）。所以这条 pin 描述的是「standard 预设下天空作为 36 顶点/12 三角形几何被画」，**不能读成「默认安装会画天空」**；默认预设不画天空这一条属于几何卡/渲染预设的账，本卡只写交叉引用，不下结论。外部证据（6.5）进一步表明原版**没有任何画质预设或 convar 会关天空盒**，所以「默认预设不画天空」是我方缺陷，不是还原原版行为。
- 同一节里三条 pvs 派生读数也是这道门自己的 demo 与它自己的开关组合下的读数（见 6.3 的重定标说明），不是任意 demo 的通用常数。

### 6.3 world-material 的三条 pvs 读数重定标（10d13559 的因果）

6.1 里那三条失败断言（`visible from camera = 858, expected 1481`、`hidden triangles = 25995, expected 15039`、`drawn triangles = 216114, expected 227070`）不是本轮代码引起的，也不是「读数飘了」，而是 `10d13559` `fix(bsp): the vis table is offset pairs and rows are Source zero-run RLE` 之后的正确读数——门里的 pin 描述的是改之前的规则。用两次构建证明：

| 读数 | `10d13559^`（t105-vt-pre） | `10d13559` = HEAD（t105-vt-post / t105-worldfix） | 是否动 |
| --- | --- | --- | --- |
| `cameraCluster` | 2108 | 2108 | 没动 |
| `visibleFromCamera` | 1481 | 858 | 动了 |
| `clusters` | 2940 | 2940 | 没动 |
| `clusteredTriangles` | 34517 | 34517 | 没动 |
| `inputTriangles` | 242109 | 242109 | 没动 |
| `hiddenTriangles` | 15039 | 25995 | 动了 |
| `drawnTriangles` | 227070 | 216114 | 动了 |
| `reason` 里「kept by a cluster past the first」 | 2276 | 609 | 动了 |
| `eyeMap` | (1359.6,-538.8,73.9) | 同左 | 没动 |

- 命令与输入：同一份 snakewater control demo、同一串开关（`--world-sky --quality standard --capture-tick 0 --capture-frame`）、各自隔离的 settings；三个 exe 分别是 `10d13559^` 与 `10d13559` 两个干净 worktree 的构建（`t105-vt-pre` 2,900,480 B / `t105-vt-post` 2,908,160 B）和本 worktree 的 HEAD 构建。原始 trace 在 `.scratch/vtvis/{pre,post,head}.trace`，脚本 `.scratch/vt-vis.sh`。HEAD 与 `10d13559` 逐字相同。
- 机制：vis lump 6 是「每 cluster 一对 (pvs,pas) offset」，行是 Source 的 zero-run RLE。旧解码把 offset 表当成了行开始（或把 RLE 走错），于是把不该可见的 cluster 也当成可见：`reason` 里「被第二个及之后的 cluster 保住」的三角形从 2,276 掉到 609，相机 PVS 从 1,481 个 cluster 降到 858 个，多藏起 10,956 个三角形。
- 为什么这是「clip 处读数变了」，不是「丢了面」也不是「相机动了」：两边都满足 `drawn + hidden = input`（227070+15039 = 216114+25995 = 242109），且 `cameraCluster`、`clusters`、`clusteredTriangles`、`inputTriangles`、`eyeMap` 五个量两边逐字相同。
- 6.1 那条「base `cd04373f` 上同样红」与此一致：`10d13559` 是 `cd04373f` 的祖先，所以 base 也是新读数。那条差分当时只能证明「不是本轮引入」，这次的两构建差分把它指到了具体提交。
- 门禁本身已重定标（`world-material-check.sh` 的 vis 段，注释里带上面这张表和 identity 论证），不是把数字改绿了事。流程上的教训也写在这里：一个 pin 描述旧规则、而规则改了、门又没人跑，就会连着几天红着而没人知道；pin 必须带提交级因果，而不是只带数字。

### 6.4 两道「缺 demo」的门：覆盖口、SKIP、本机实跑

- 改动（断言一条没删，默认路径一个字没改）：
  - `world-geometry-hole-check.sh:60`：`DEMO="${WORLD_CHECK_DEMO:-$TF/demos/autorecord_2026-07-02_13-26-46.dem}"`，素材不存在时打印四行 SKIP、`SUMMARY checks=0 fail=0 skip=1` 与 `VERDICT world-geometry-hole: SKIP (0 assertions ran, 1 gate skipped)` 后 `exit 0`——跳过计入 skip 计数，不会被读成「全部通过」。
  - `world-render-reliability-check.sh:83`：`SUNSHINE="${WORLD_CHECK_SUNSHINE_DEMO:-D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/demos/SUNSHINE.dem}"`，同样缺文件即 SKIP（`exit 0`），`--mutation` 分支不变。
- SKIP 路径实测（不启动产品，RC=0）：
```
SKIP world-geometry-hole-check: the recording this gate measures is absent
SKIP   wanted: /nonexistent/demo.dem
SKIP   every pin (missing 97,453 px / enclosed 4,613 px / the named seam faces) is pl_odyssey at TICK=2000 in that recording; another recording is another pose on another map
SKIP   set WORLD_CHECK_DEMO=<path> to run it
SUMMARY checks=0 fail=0 skip=1
VERDICT world-geometry-hole: SKIP (0 assertions ran, 1 gate skipped)

SKIP world-render-reliability-check: the T5 baseline recording is absent
SKIP   wanted: /nonexistent/SUNSHINE.dem
SKIP   section 1 (the T5 ending) and section 3 (the six sky sides) are readings of that recording; the control in section 2 is a different map and cannot stand in for them
SKIP   set WORLD_CHECK_SUNSHINE_DEMO=<path> to run it
SUMMARY checks=0 fail=0 skip=1
VERDICT world-render-reliability: SKIP (0 assertions ran, 1 gate skipped)
```
- 可靠性门实跑（`.scratch/worldfix/rel.log`）：`WORLD_CHECK_SUNSHINE_DEMO='E:/demo测试/cp_sunshine/STV/SUNSHINE.dem'`（82,759,239 B，sha256 `7b8db6b4d5f375761723ededb89c232c842d1998f61d6134d9ad4ddc712e6a08`），`checks=48 fail=0`，`WORLD-RENDER-RELIABILITY=PASS`。SUNSHINE.dem 就是 cp_sunshine 的录像，换的只是文件位置，门要验的 48 条（启动、帧、trace、退出码）一条没少。
- 几何洞门实跑两次（同一份 `WORLD_CHECK_DEMO='E:/demo测试/pl_upward_f12/STV/a9630c6557468b24f0b8c2e7dab38ed8_match-20261030-2145-pl_upward_f12.dem'`，地图 pl_upward_f12，`TICK=2000` 不变）：
  - 第一次 `.scratch/worldfix/geo.log`：门跑起来了，但缓存自洽性两条是假红——`--validate` 探针与 capture 子进程没继承 `WORLD_CHECK_DEMO`，`compute_inputs()` 按写死默认 demo 报 `identity input is missing`，于是 `FAIL ... (reason=none)`。修法：gate `export DEMO`，capture 侧改成 `DEMO=${CAPTURE_DEMO:-${DEMO:-写死默认}}`。
  - 第二次 `.scratch/worldfix/geo2.log`：三条缓存断言全过（`PASS a trace that lost its "scene: camera" pose line is refused, not reused`、`PASS a frame whose executable identity changed is refused, not reused`、`PASS every frame this run wrote carries an identity and a pose-bearing trace`）。**成立的地图无关断言**：`PASS build`、`PASS native/ is clean`、`PASS sky face census`、`PASS face account unchanged`（`geometry faces total=21119 rendered=19845 sky=731 skippedNoDraw=0 skippedInvalid=0`）、`PASS the shipped rule keeps 0.771 of the eye's first hits; the two cells a flip would win read 0.325 and 0.283`、`PASS the binary this run built reproduces the pinned textured frame byte for byte`（这一条比的是本轮新构建与本次矩阵自己写的帧，是自洽性检查）。**失败的全是 pl_odyssey 专属 pin**：`stored textured/ref frame identity`（`f2b6aff5…`/`de610799…`）、`missing pixels 9 != 95892`、`missing pixels covered by a sky face 9 != 95892`、`enclosed missing pixels 9 != 4634`、`T23 95892`、`back-face population 4 != 49077`、`sky hull closed 0 != 1`、`enclosed components 0 != 1`、`capture-pose eye -1945.3,-1016.6,243.1 != 815.5,-147.8,-194.3`、seam 方向/成员 `238,4173,9189` 等；收尾 `FAIL: 1 assertion groups did not hold.`。
  - 读法：换 demo 能让门跑起来并逐条指出「哪些是地图专属」，**不能读成绿**；同一份日志里这张图的空像素账是 9 个、全部被天空面覆盖、`unexplained=0`，不是渲染崩了。要让几何洞门真正复活，需要 pl_odyssey 录像的机器，或把 pin 连同 demo/map/camera 按地图重钉。缓存 key 里含 capture 脚本 sha（`cache rejected: identity-mismatch, fields= captureScript`），所以改脚本后第一次必然 18 帧全采，这是设计如此，不是缓存坏了。
- 顺带修的一个真 bug：`mktemp -d` 在本机 TMPDIR 是 Windows 形态时返回 `C:/...`，而 `world-geometry-hole-capture.sh` 的绝对路径判定只认 `/*`，于是把 `$PWD/C:/...` 当相对路径又拼了一遍，`rm`/`mkdir` 报 `Invalid argument`。现在 capture 的 `case` 认 `[A-Za-z]:*`，check 在 `mktemp` 之后立刻 `ISO_DIR=$(cygpath -u "$ISO_DIR")`。这个坑只有换成需要 capture matrix 的 demo 才会露出来。

### 6.5 外部证据（研究卡 research-source；Source 引擎源码，非本机实测）

`D:/_shared/research/tf2-source-notes/NOTES.md` 是调研卡的引擎源码证据（官方 wiki 四页与 Wayback 副本都被 Anubis 反爬挡住，所以本文没有 B 级 wiki 引用），与本卡相关的三条：

- **背面剔除**：引擎世界 pass 在 CPU 侧对每个面做 `Dot(normal, viewOrigin) - dist < -0.01` 的背面剔除，只有材质 `IsTwoSided` 才跳过；GPU 默认 `D3DCULL_CCW`、`MATERIAL_CULLMODE_CCW → D3DCULL_CCW`，屏幕空间顺时针为正面——与本产品的 `FrontCounterClockwise=FALSE` 一致。本文的读数仍是本机实测，这条只作交叉引用。
- **天空不能被预设关掉**：TF2 里没有任何画质预设或 convar 会关天空盒（全量 string scan：只有 cheat 的 `r_drawskybox` 默认 1、`mat_loadtextures`；`r_draw3dskybox`/`mat_drawskybox` 不存在于引擎 dll；`tf/cfg` 里没有天空 convar）。⇒ 6.2 里「默认预设 `render.skybox=false` 不画天空」是**本产品的缺陷**，不是还原原版行为。
- **vis 编码**：vis 表是 `dvis_t`（numclusters + 每 cluster 的 PVS/PAS 偏移）后跟零游程编码位向量（非零字节原样拷贝，零字节+计数=零游程）——这正是 `10d13559` 修的东西。6.3 的 pin 因果仍以本文那两次构建的实测数据为准，外部证据只说明「修的是编码不是数值」。
- 一条**未验证**（NOTES 的 Unresolved）：「天空材质是否写深度」——本文不拿它做任何结论。

## 7. 没做的

- 4,219 / 6,072 / 10,291 这组绝对计数来自几何卡自己的那一帧（相机与阈值不同），本文的分拨是本帧自己的定义与读数；比例相同（约 55/45），绝对数不同是帧不同，不是分类不一致。
- 静态道具 vs 刷模型的命名：`--capture-pass-depth` 把它归到「实体 pass」，entity-id 给 `(1,1,1)`、染色负控不动它，据此命名为**静态道具**；逐像素的模型名 trace 不存在（也没有 `--static-prop-budget` 这个开关），具体是哪一个 prop 模型本文没有命名。
- 七张地图的逐图材质普查（`.scratch/worldfix/materials-maps.sh`，7 次产品启动）没有跑：第 3 节的 B 读数只来自 pl_upward_f12。
- world-material 的三条 pvs pin 已按 6.3 的两构建差分重定标并复跑 PASS；但这道门只在本机那一份 snakewater control demo 上读过，换 demo 读数会变，pin 与 6.3 的表是绑定的。
- 几何洞门与渲染可靠性门已加覆盖口（6.4），判定基准仍绑原 demo：可靠性门换文件位置后 48 项全过；几何洞门的 pl_odyssey 专属 pin（`PIN_MISSING=95892` 等）在别的 demo 上不可能重现，本机只验到「不崩、不丢帧、洞统计自洽」，地图专属数值没有重置。
- `evidence/t8-render-reliability-20261117101825-20636-forward/` 是失败那次的生成物，未提交，留着给 6.1 的引用落地。
