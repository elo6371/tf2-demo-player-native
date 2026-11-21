# $AlphaTestReference 接入与 pr600 像素验证（task-34，2026-11-21）

## 0. 结论

1. 世界材质的 alpha cutout 阈值不再硬编码 0.5：每个材质从自己的 VMT 读
   `$alphatestreference`（缺省 0.5），经 Makefile 顶点通道送进 shader。
   旧代码的两处 `sampledTexel.a < 0.5`（HEAD 的
   `native/src/native_renderer_shaders.cpp:393` 与 `:613`）现在用材质自己的值。
2. cp_process_f12 的 `materials/METAL/METALTRUSS001.vmt` 声明 `0.2`，
   本产品的 trace 也读到了 `reference=0.2000`（见 §2.2）。pr600、tick 600 上
   修复后有 **1140** 个像素从“镂空/透出背景”变成“金属桁架”：
   685 个修前是天空色 (117,122,150)，455 个修前透出的是后面别的几何；
   这 1140 个像素的采样 alpha 全部落在 **[0.20, 0.50)**（§2.4），
   正是旧硬编码 0.5 会切掉、VMT 的 0.2 会保留的区间。
3. 声明 0.5 的材质逐像素不变：在新二进制上强制全场景 0.5 抓出的帧与修改前
   旧二进制的帧 **md5 完全相同**（`d9544cd67773552f0b948a98bf47458c`）；
   强制全场景 0.20 抓出的帧与默认帧 **md5 完全相同**
   （`2ddade356e85dd77b795a7380c254bcb`）。见 §2.5。
4. 诊断臂 `TF2_NATIVE_WORLD_ALPHA_TEST_REFERENCE` 只用于测量，产品默认路径
   不读它；构建 rc=0，默认抓帧不引入新参数、不改退出码。

## 1. 改动

### 1.1 解析 VMT

- `native/include/vmt_material.h:102`：`VmtMaterial::alphaTestReference = 0.5f`
  （Source 在键缺失时的默认值，也是旧硬编码的数字）。
- `native/src/vmt_material.cpp:316-326`：`$alphatest` 之后读
  `$alphatestreference`；`strtod` 必须吃掉整串、值必须有限，结果
  clamp 到 [0,1]。读不成数字（空串、`"0.2foo"`、NaN）就保留 0.5，
  按“读不懂的值不猜阈值”处理。

### 1.2 每顶点打包（世界 pass 没有 per-material 常量）

- `native/include/native_renderer.h:198-202`：`WorldTexture::alphaTestReference`
  与 `:506` `WorldTileReading::alphaTestReference`。
- `native/src/native_renderer_world.cpp:326-327`：建 tile 表时把材质的阈值
  带进 reading（这是 trace `stage=consumed reference=` 的来源）。
- `native/src/native_renderer_world.cpp:911-925`：走顶点时把阈值写进
  `featureMask`：bit 12..19 = `round(reference * 255)`，bit 20 = “有阈值”。
  **reference 恰好 0.5 时不写任何位**，shader 回落到字面量 0.5——这样
  旧硬编码路径的帧是逐像素复现，而不是用 128/255 重新推导（采样是双线性，
  texel alpha 不一定是 1/255 的整数倍，两者切点不同）。0.2 恰好是 51/255；
  cp_process_f12 里其它值（如 0.55 → 140/255）误差在 1/255 内。
- featureMask 的 bit 12..20 在该 shader 里原先没有任何用途（已用位只有
  1/2/4/8/16/32/64/128/512/1024）；顶点在三角形内阈值一致，插值不会
  把位串拼坏。

### 1.3 shader

- `native/src/native_renderer_shaders.cpp:385-386`：解码
  `alphaTestReference = bit20 ? ((featureMask >> 12) & 255) / 255 : 0.5`。
- `:401`（unlit/模型分支）与 `:621`（世界分支）两处 discard 都改成
  `sampledTexel.a < alphaTestReference`。
- 若 shader 用了低精度或采样改了滤波方式，回到 0.5 的那条路径仍与旧帧一致
  （§2.5 已用整帧 md5 钉住）。

### 1.4 主程序：每材质值、trace 与控制臂

- `native/src/main.cpp:6947-6960`：`TF2_NATIVE_WORLD_ALPHA_TEST_REFERENCE`
  存在时解析成 `forcedAlphaTestReference`（clamp [0,1]），否则 -1（不覆盖）。
- `:7228-7232`：`alphaTestReference = forced >= 0 ? forced : material.alphaTestReference`；
  `tiled.alphaTestReference` 在 `:7735` 写入。
- `stage=declared`（`:7237`）与 `stage=consumed`（`:10510` 附近）的
  trace 行增加 ` reference=%.4f`，因此“VMT 声明了什么”和“世界 pass 实际
  收到了什么”各有一行可核对。

## 2. 证据

### 2.1 环境与命令

- 构建：`bash tools/lockrun.sh lead -- bash build-target.sh tf2_demo_native` → **rc=0**。
- demo：`/e/demo测试/cp_process_f12/STV/1338f5de79ea343682c17dd514c980d9_matcha-20260927-1312-cp_process_f12.dem`
  （demoProtocol=3, networkProtocol=24, ticks=120936, frames=120917）。
- 抓帧脚本 `.scratch/n34-alpha/cap.sh <tag> [额外参数]`：`--tf-root` 指向
  TF2 安装、`--demo` 指向上面的 demo、`--capture-tick 600`、
  `--capture-frame <tag>/pr600.bmp`、空 settings/ui、`--audio-disabled
  --hidden-window`，`TF2_NATIVE_TRACE=<tag>/<tag>.trace`。
- 阈值扫描：`TF2_NATIVE_WORLD_ALPHA_TEST_REFERENCE=<v> bash .scratch/n34-alpha/cap.sh ref<vv>`。

### 2.2 trace 里的声明值（pr600 tick 600）

`grep "gpu: world alpha" <trace>`，同帧共 23 行（declared + consumed 各半），
两个 cutout 材质：

| 材质 | 模式 | VMT | 声明 reference |
| --- | --- | --- | --- |
| `METAL/METALTRUSS001` | cutout (`$alphatest`) | `materials/METAL/METALTRUSS001.vmt` | **0.2000** |
| `METAL/METALGRATE011A` | cutout (`$alphatest`) | `materials/METAL/METALGRATE011A.vmt` | **0.5500** |

其余 9 个材质（玻璃、`TOOLS/TOOLSINVISIBLEDISPLACEMENT`、`OVERLAYS/DUST_GRADIENT0*`、
`METAL/METALGRATE013A2`）都是 `$translucent` 的 blended，reference=0.5000，
不走 cutout 分支。

### 2.3 阈值扫描（同一个 tick，只改 shader 的参考值）

对修改前的帧（旧二进制，硬编码 0.5，下面称 before）与修改后的默认帧
（after2）逐像素比较，得到 **1140** 个差异像素。再用强制参考值臂逐档抓帧，
统计这 1140 个像素各自“画出来（等于 after2）”还是“被切掉（等于 before）”：

| 强制 reference | 抓帧 md5 | 画出来 | 切掉 |
| --- | --- | --- | --- |
| 0.20 | `2ddade356e85dd77b795a7380c254bcb` | **1140** | 0 |
| 0.30 | `33914a5cbf60b231437c2cc836c3a8be` | 711 | 429 |
| 0.40 | `252526edf00cd3dbffa11f3a4ca7694b` | 313 | 827 |
| 0.45 | `4eda2230edf6ff7694e8f8a26e0e573b` | 160 | 980 |
| 0.50 | `d9544cd67773552f0b948a98bf47458c` | 0 | **1140** |
| 0.55 | `c9dd62f5b856a8a0d6c1840d1de06080` | 0 | 1140 |

默认帧（after2）`2ddade356e85dd77b795a7380c254bcb` 与强制 0.20 行**整帧相同**：
本帧实际生效的 cutout 阈值就是 0.2（truss）与 0.55（grate），而 0.55 的材质
在本帧没有 alpha 落在 [0.2,0.55) 的可见 texel，所以两个阈值臂在本帧等价。
强制 0.50 行与修改前帧整帧相同，说明旧行为被逐像素复现。

### 2.4 每个差异像素的采样 alpha 区间

规则：强制参考值 r 下“画出来”当且仅当采样 alpha >= r（`discard` 是
`alpha < r`）。把 1140 个像素在 0.20/0.30/0.40/0.45/0.50 五档下的结果排序，
每个像素得到区间 [最高“画出来”档, 最低“切掉”档)。**没有任何像素不单调**
（0 例），分布：

| alpha 区间 | 像素数 | 例（x,y 修前 RGB → 修后 RGB） |
| --- | --- | --- |
| [0.20, 0.30) | 429 | (325,158) (117,122,150) → (20,19,22)；(367,174) (117,122,150) → (19,19,22)；(324,239) (62,67,49) → (23,22,24)；(401,248) (71,73,54) → (22,21,24) |
| [0.30, 0.40) | 398 | (323,157) (117,122,150) → (24,23,26)；(384,171) (117,122,150) → (29,28,33)；(327,240) (43,48,43) → (26,25,28)；(404,248) (66,71,56) → (27,26,29) |
| [0.40, 0.45) | 153 | (325,157) (117,122,150) → (33,32,36)；(398,172) (117,122,150) → (36,35,40)；(432,189) (125,105,95) → (34,33,38)；(335,249) (117,122,150) → (33,32,37) |
| [0.45, 0.50) | 160 | (331,159) (117,122,150) → (38,37,42)；(403,173) (117,122,150) → (38,37,43)；(328,239) (117,122,150) → (38,36,40)；(419,250) (117,122,150) → (38,36,41) |

全 1140 个像素都在 [0.20,0.50) 内，且全部在强制 0.50 档被切掉——这正是
“旧硬编码 0.5 切掉、VMT 的 0.2 保留”的集合。差异像素包围盒
x∈[320,437]、y∈[157,261]，全部落在 pr600 画面里的金属桁架区域。

### 2.5 默认路径与控制臂的一致性

- 修改前旧二进制 before = `d9544cd67773552f0b948a98bf47458c`；
  新二进制强制 0.50（legacy2）= `d9544cd67773552f0b948a98bf47458c`：
  **整帧相同**，即 0.5 路径没有被动过。
- 新二进制默认 after2 = 强制 0.20 = `2ddade356e85dd77b795a7380c254bcb`：
  默认帧就是“每材质阈值”生效的结果，而不是某个折中值。
- 同帧同参数的重复抓帧在旧流程里已多次得到同一 md5（legacy2 与 before 就是
  两次不同二进制的抓帧），扫描各档之间也只差阈值，没有随机成分。

## 3. 与 task-9 的关系

task-9 第 1 部分在 pr600 上留下 313 px“残余”，并证明全部是 `$alphatest`
镂空（`docs/WORLD-CULL-ORACLE-2026-11-21.md` §6）。本次扫描里
“强制 0.40 档画出来”的像素数正好也是 **313**（= [0.40,0.45) 的 153 +
[0.45,0.50) 的 160）。这不能当作两组像素同一的证明（task-9 的 313 是另一套
比较口径），但方向和量级一致：旧 0.5 阈值切掉的正是这批 0.4~0.5 的桁架像素。
修复后它们全部由镂空变为不透明。

## 4. 限制

- **没有直接读到 GPU 的 alpha 值**：`--world-face-id`/调试视图的路子抓到的
  B 通道是常数 255（`.scratch/n34-alpha/probe12/pr600.bmp`），所以本文的
  alpha 是从“阈值档位翻转”反推的采样 alpha，分辨率 0.05（扫描步长）。
  它是 shader 实际比较的那个双线性/多级渐远采样值，不是基础 texel 的
  8-bit alpha——这正是要证明的量，但不是逐 texel 直读。
- **只测了 pr600 的 tick 600**：其它地图、其它帧、其它 cutout 材质未测；
  `METAL/METALGRATE011A` 的 0.55 已解析并送进 shader（trace 可查），但本帧
  没有可见 texel 落在 [0.2,0.55)，所以“0.55 会多切一点”这个方向只有代码与
  trace 证据，没有像素证据。
- `--world-face-trace` 报的 `texelA` 是基础 mip 的 texel alpha
  （truss 采样点上是 255 或 0），无法预测过滤后的采样 alpha，因此不能用它
  做逐像素归属；归属依据是 §2.2 的声明表（全帧只有 truss 声明 0.2）和
  task-9 §6 的既有结论。
- 诊断臂 `TF2_NATIVE_WORLD_ALPHA_TEST_REFERENCE` 是测量工具，不是产品开关；
  产品默认不读它，也不改变 CLI、退出码和默认抓帧以外的任何路径。
- featureMask 的阈值只有 8 bit（1/255 量化）：0.2 精确，0.55 量化到 140/255
  = 0.54902，差 0.001，小于一个量化步长。

## 5. 触及文件与复现

代码：

- `native/include/native_renderer.h`（WorldTexture / WorldTileReading）
- `native/include/vmt_material.h`
- `native/src/vmt_material.cpp`
- `native/src/native_renderer_world.cpp`
- `native/src/native_renderer_shaders.cpp`
- `native/src/main.cpp`

证据（`.scratch/n34-alpha/`，未入库）：

- `before/pr600.bmp`（旧二进制 0.5）、`legacy2/pr600.bmp`（新二进制强制 0.5）、
  `after2/pr600.bmp`（新二进制默认）、`ref020/030/040/045/055/pr600.bmp`；
  同名 `.trace` 里有 §2.2 的声明/消费行。
- `cap.sh`（抓帧）、`band.py`（§2.4 的区间统计）、`diff.py`（差异像素分类）。

复现：

```bash
bash tools/lockrun.sh lead -- bash build-target.sh tf2_demo_native
bash .scratch/n34-alpha/cap.sh default                      # 默认帧
TF2_NATIVE_WORLD_ALPHA_TEST_REFERENCE=0.20 bash .scratch/n34-alpha/cap.sh ref020
TF2_NATIVE_WORLD_ALPHA_TEST_REFERENCE=0.50 bash .scratch/n34-alpha/cap.sh legacy2
python .scratch/n34-alpha/band.py                           # 区间表
```
