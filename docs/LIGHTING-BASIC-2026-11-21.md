# 基础光照计划：世界烘焙光已就绪，补人物/武器的 6 向环境光与 Half-Lambert（2026-11-21）

状态：**计划，未开工**。目标是把"一眼有 TF2 味道"的两件轻量光照接进来，不做实时阴影、法线/凹凸光照、Phong/Rim、Cubemap 反射。每一步默认关，开与关都能同机位抓帧对拍。

## 0 先更正现状：世界 Lightmap 不是缺口

提案（2026-11-21 群内）假设"项目完全没有光照"，与代码不符。先量清楚再排期：

- **世界 baked lightmap 已实现并在用**：BSP lump 8 按 ColorRGBExp32 解析（`native/src/bsp_map.cpp:263`），只取 `styles[0]`（`native/src/bsp_map.cpp:966` 的 `const bool hasLightmap = styles0 != 255 && lightOffset >= 0 && lightBytes > 0`），逐面 (size+1)×(size+1) 解码后装进 shelf 图集（`native/src/bsp_map.cpp:833-881` 选宽、`:1077-1104` 打包，宽可超过历史 2048），逐三角形写 lightmap UV（`:1151-1152`），掉面单独计数（`lightmapAtlasDroppedFaces`，`:1652`）。模式名 `RgbExp32` / `"source-equivalent"`（`:1635`、`:1669-1671`）。
- **世界 PS 真的采样了图集**：`native/src/native_renderer_shaders.cpp:436-451`，`lightmapFeatures.w > 0.5 && lightUv.x >= 0` 时 `lightColour = pow(max(lightmapTexture.Sample(...), 0), 1/2.2)`，再 `albedo *= lightColour`（`:453`）。gamma 的推导写在 `:437-450`。
- **地图灯（N21）也已接**：`light_environment` 的 `_light`/`_ambient` 解析在 `native/src/bsp_map.cpp:654-657`，结构在 `native/include/bsp_map.h:153-165`；世界 PS 的 sun/ambient/lamp 三段在 `native/src/native_renderer_shaders.cpp:531-590`（lightmapped 面只吃烘焙，无 lightmap 的几何吃 sun+ambient，16 盏灯对所有面生效）。开关常量在 `native/src/native_renderer_frame.cpp:703-705`、`:848-851`。
- **A/B 开关已有**：`--world-lightmap on|off`，默认 on（`native/src/main.cpp:4267-4271`，默认值 `:9173`，非法值报错 `:4975`）。
- **HDR lump 53 只用了一半**：只做 tone 统计（`native/include/bsp_map.h` 的 `hasHdrLightmap/hdrLightmapSampleCount`，写入处 `native/src/bsp_map.cpp:1614-1621`）；判定要求 HDR 与 LDR **等长**才可用（`native/src/bsp_map.cpp:272-273`），所以 LDR 为空的地图连 HDR 都判不可用，图集只从 LDR 打包（样本只来自 `lighting`，`:1090`），HDR-only 地图退到 `AverageIntensity`（`:1640-1644`）。

结论：**提案里的 Step 2（世界 lightmap）实际已经是完成态**；真正的缺口在人物/武器侧，以及世界图集的三个小尾巴（HDR-only 回退、面片间 1px clamp border、掉面清单）。

## 1 实体侧现状（要改的就是这两处）

- 实体/道具模型 PS：`native/src/native_renderer_shaders.cpp:720-728`（`modelPixelSource`）
  ```hlsl
  float diffuse = saturate(dot(normal, normalize(float3(0.35, 0.55, 0.75))));
  float lighting = 0.62 + 0.38 * diffuse;
  return float4(input.colour.rgb * lighting, input.colour.a);
  ```
  方向写死、没有地图太阳、没有 ambient cube、不是 Half-Lambert；这个 PS 连 cbuffer 都没有。
- 第一人称武器 PS：`native/src/native_renderer_shaders.cpp:1146-1176`（`viewModelPixelSource`），方向写死 `(-0.30, 0.42, 0.86)`、`0.55 + 0.45 * diffuse`，另有独立的 bump/phong/selfillum 分支（bump 用的是近似 TBN，见 `:1160-1176` 注释）。
- 全树**没有** `LUMP_LEAF_AMBIENT_INDEX(52)` / `LUMP_LEAF_AMBIENT_LIGHTING(56)` 的解析（`grep -n "LEAF_AMBIENT" native/` 0 命中），所以 Step 2 的 leaf ambient cube 是从零接。

## 2 参考实现（只读；实施时必须按"库+路径+文件+行号 → 本仓文件:行号"复核）

以下行号来自提案的实读摘录，本轮未逐条打开核对（本机对 GitHub 页面访问受限）；落地报告里必须重新核对并写对读记录，核对不上就写"未能对读外部源码"，退回本仓证据。

| 用途 | 库 / 文件 / 行 | 读什么 |
|---|---|---|
| 世界 lightmap lump | `OthmanAba/TeamFortress2` `tf2_src/public/bspfile.h:289, :347, :703-734` | `LUMP_LIGHTING=8`（LDR 优先）、`LUMP_LIGHTING_HDR=53`；`dface_t` 的 `styles[0]`、`lightofs`、`m_LightmapTextureMinsInLuxels`、`SizeInLuxels`（采样 `(size+1)`） |
| 世界 PS 合成 | `lightmappedgeneric_ps2_3_x.h:237-241, :465-482` | `diffuseLighting = lightmapColor1 * g_TintValuesAndLightmapScale.rgb`，再乘 albedo |
| leaf ambient 数据 | `bspfile.h:343-350, :859-875` + `compressed_light_cube.h:17-21` | `dleafambientindex_t`（count/first）→ `dleafambientlighting_t` 的 `CompressedLightCube`：6 个 ColorRGBExp32，顺序 +X,-X,+Y,-Y,+Z,-Z |
| 环境光立方体 PS | `common_vertexlitgeneric_dx9.h:41-50` | `PixelShaderAmbientLight`：`nSq = N*N`，按各轴正负加权 6 色 |
| Half-Lambert | `common_vertexlitgeneric_dx9.h:86-106` | `f = saturate(dot(N,L)*0.5+0.5); diffuse = f*f`，背光面不死黑 |

引用规则与"一行代码都不许复制"的红线见 [REFERENCE-REPOS-2026-11-21.md](REFERENCE-REPOS-2026-11-21.md)。

## 3 分步计划（按投入产出比排序）

### Step 1（P0，S–M）：人物/武器 ambient cube + Half-Lambert，不用改 BSP

目标：一张固定方向光的平面感消失；背光面不死黑；人物走进亮处/暗处有响应（先只按地图太阳与 ambient 给默认立方体，leaf 数据留到 Step 2）。

改动：
1. 新增常量块 `ModelLightingConstants`（PS `register(b1)`，与 VS 的骨骼 b1 不冲突）：`float4 ambientCube[6]`、`float4 sunDirectionAndIntensity`（xyz 指向光、w 强度）、`float4 lightingParams`（x=开关）。
2. `modelPixelSource` 与 `viewModelPixelSource` 各加一个 `lightingParams.x > 0.5` 分支：`halfLambert` + 立方体权重；**关闭时走原公式，算术一字不改**，保证旧抓帧字节不变。
3. 常量填充：`native_renderer_frame.cpp` 里从 `worldLights_`（地图 `light_environment`）推导默认立方体：`+Z` ≈ sky（sun*0.6 + ambient）、`-Z` ≈ ground（ambient*0.6）、`±X/±Y` ≈ ambient*0.8 + sun*0.25；无 `light_environment` 时退一组暖天冷地常量。所有 6 色与开关都进 trace，便于复算。
4. CLI：按 `--world-lightmap` 的既有写法（`native/src/main.cpp:4267-4271` 的 `wcscmp` 链 + 用法表 + 非法值报错）加 `--lighting off|basic`，**默认 off**。
5. 实体法线空间先自证：加一条只读 trace，打印一个已知朝向实体的 `N·L` 符号与三轴分量；若法线不在世界空间（骨骼矩阵是否含实体旋转），先修再谈观感。

验收：
- `--lighting off` 的 pr600（tick 600）抓帧 md5 必须等于当前基线 `2ddade35`（说明关路径零改动）。
- `--lighting basic` 同 tick 抓帧：人物/武器背面不再是"贴纸"，侧光扫过时明暗连续；给 1:1 局部裁图（人物躯干 + 武器）+ 像素均值/极值读数。
- `/W4` 零告警；trace 出现 `gpu: lighting mode=basic cube=...`。

### Step 2（P1，M）：BSP leaf ambient cube，让人物融入环境

1. 解析 `LUMP_LEAF_AMBIENT_INDEX(52)` + `LUMP_LEAF_AMBIENT_LIGHTING(56)`（HDR 对应 51/55）：index 是 4 字节（`count`/`first`），lighting 记录 28 字节（6×ColorRGBExp32=24 + `x,y,z,pad`）。
2. 叶子查询：实体包围盒中心走 BSP 节点树（lump 1 planes + lump 0 nodes/leaves）；取该 leaf 的第 0 个采样（或在同 leaf 多采样里按 z 取最近）。**先确认本仓是否已有可用的 node/leaf 遍历**；没有就补一个只读遍历，不改现有世界路径。
3. 每个 entity instance 上传 6 色（替换 Step 1 的默认立方体），PS 用同一权重公式。查不到 leaf 时退回默认立方体并计数 + trace。
4. 验收：pr600 户外亮处与室内暗处各站/放一个角色，trace 打两组 cube RGB（应有明显差异）；两张同机位截图；`fallback` 计数为 0（或解释来源）。

### Step 3（P2，S–M）：世界图集三个尾巴（可选，独立于 Step 1/2）

1. HDR-only 地图：`lighting`（lump 8）为空时用 lump 53 打包（同一 RGBExp32 解码），而不是退平均强度；放宽 `:272-273` 的等长判定。
2. 1px clamp border：现在 shelf 按 `cursorX/cursorY` 紧贴打包（`native/src/bsp_map.cpp:1077-1104`），双线性采样会在面片接缝串色；每面片四周留 1 luxel 边。
3. 掉面清单：`lightmapAtlasDroppedFaces > 0` 的地图与面号进 trace/报告，七图验收为 0 或列明例外。

## 4 开关、回归与证据规程

- 总开关 `--lighting off|basic`，默认 off；**off 必须字节不变**（先抓基线再动手）。
- 现有门禁（七图、N06、N11、N18）全部用默认 off 跑，不因本计划改 pin；开光照的抓帧单独进 `.scratch/n41-light/`，报告引用时给完整命令。
- 构建规矩照旧：改过的源文件先 `touch`、新源文件先 `git add`、走 `bash tools/lockrun.sh lead -- bash build-target.sh tf2_demo_native demo_open_probe`、`/W4` 零告警 + 七目标 `/WX`。
- 每步交付：代码 + 一份 `docs/` 报告（现状/改动/证据表/取舍/未做）+ 一条可复现命令；像素证据必须带 demo/tick/exe 身份。

## 5 明确不做（避免范围膨胀）

- 实时阴影（Shadow Map）、`LUMP_WORLDLIGHTS` 动态局部多光源。
- `$bumpmap` 的 3 向 Radiosity Normal Mapping、Phong 高光、Rim Light、LightWarp 1D LUT、`$envmap` 反射。
- `styles[1..3]` 闪烁/可开关灯光的多通道叠加（只读 `styles[0]`）。
- 曝光重做：世界输出仍是"线性光 + pow(1/2.2)"，不引入 HDR tonemap。

## 6 排期与卡

| 卡 | 内容 | 依赖 |
|---|---|---|
| task-36 | Step 1：实体/武器 ambient cube + Half-Lambert（`--lighting` 开关） | 无 |
| task-37 | Step 2：BSP leaf ambient cube 接入 | blocked_by task-36（共用 PS 常量与开关） |
| task-38 | Step 3：世界图集 HDR-only / clamp border / 掉面清单 | 无（只碰 bsp_map） |

写域提示：Step 1/2 都碰 `native/src/native_renderer_shaders.cpp`、`native/src/native_renderer_frame.cpp`、`native/src/main.cpp`（`main.cpp` 属共享写域，与 task-3 协调串行）。

## 7 复现模板

```bash
# 基线（关）：应等于 2ddade35（pr600 @ tick 600）
bash .scratch/n34-alpha/cap.sh light-off
# 开光照
TF2_NATIVE_LIGHTING=basic bash .scratch/n34-alpha/cap.sh light-basic   # 开关名以落地实现为准
# 像素差
python .scratch/n34-alpha/diff.py .scratch/n34-alpha/light-off/pr600.bmp   .scratch/n34-alpha/light-basic/pr600.bmp
```

（`cap.sh` 的抓帧装置、ini 与 demo 见 `.scratch/n34-alpha/` 与 [ALPHATEST-REFERENCE-2026-11-21.md](ALPHATEST-REFERENCE-2026-11-21.md)；开关最终以落地提交为准，本文件随实现更新。）
