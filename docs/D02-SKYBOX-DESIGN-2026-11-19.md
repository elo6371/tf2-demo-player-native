# D02 3D skybox 设计（第 8 轮 G10 交付，已核对落盘）

* 基线：`work/integrate @ 28aba482`
* 来源：Gemini 第 8 轮交付 `gemini_round8_deliverable_2026_11_19.md` 第二部分（G10）
* 核对结论：**锚点全部正确**（`native_renderer_frame.cpp:435/448/449-450/468-487/488/492-493/498-499/515-524/942-950`、
  `native/include/bsp_map.h:176-181`、`native/src/bsp_map.cpp:628-638`、`main.cpp:3790-3794` 的 `--world-sky-3d on|off`、
  `main.cpp:4186-4192` 的 `--world-geometry all|terrain|sky|none`），但**方案漏了一条会让改动白做的硬约束**（§2.0）。

## 1. 现状

* `BspSkyCamera`：`native/include/bsp_map.h:176-181`（`present`、`origin[3]`、`scale = 16.0f`、`entityIndex`），挂 `BspMap::skyCamera`；解析在 `native/src/bsp_map.cpp:628-638`（扫 `sky_camera` 实体）。
* 有人读：`native/src/native_renderer_frame.cpp:942-950` 读 `worldSky3d_.enabled/present/scale/origin` 建 `skyOriginBox`。`hd` 下不是死字段。
* 绘制：`drawWorldSkyGeometry3d` 在 `native/src/native_renderer_frame.cpp:435-511`，由 `flushSkyPasses`（`:515-524`）在 `:1100`、`:1227`、`:1246` 三处触发。
* 深度：帧首 `:220` 清 depth=1.0；`:449-450` 绑 `worldSkyDepthStencilState_` 并 `noteDepthState("sky3d:write-off")`——
  即**在不透明世界之后画、且关深度写入**。这正是「天空永远盖不住前景」的实现，也是「天空内部互不遮挡」的根源。

## 2. 方案

### 2.0 先决条件：必须去掉远平面 pin（交付漏写）

`native/src/native_renderer_frame.cpp:961-963`：

```cpp
const float sky3dFarPlanePin = 1.0f - 1.0e-6f;
for (int row = 0; row < 4; ++row)
  sky3dConstants.mvp.m[row][2] = sky3dConstants.mvp.m[row][3] * sky3dFarPlanePin;
```

这一行把所有 3D 天空顶点的 `z` 都钉在远平面（同一深度）。只要它在，**即使把这一遍改绑成开深度写入的
`depthStencilState_`，也不会产生任何内部自遮挡**：先画的占用深度，后来的同深度片元要么被 `less` 判失败，
要么在 `less_equal` 下与先画的纠缠，结果仍然不是「按真实深度排序」。
所以 D02 的第一步不是换状态，而是**在这一遍里去掉 pin（或为天空另建一份不 pin 的常量块）**，
让天空几何带着自己的真实 NDC 深度进光栅化；未 pin 之后才轮到 §2.1 的深度状态。

### 2.1 变换：缩放发生在「视点」，不在顶点

* 几何顶点**未预缩放**：`native/src/native_renderer_world.cpp:1082-1094` 用与主世界同一份值建 VB（注释就写明 "Built after the world's own vertex buffer from the same values"）。
* 缩放靠移动视点实现：`native/src/native_renderer_frame.cpp:951-959`
  `eyeSky = skyOriginBox + (eyeBox - skyOriginBox) / sky3dScale`，然后
  `sky3dMvp = XMMatrixTranslation(eye - eyeSky) * mvp`。
  也就是说「mvp 乘 scale(1/scale)」只是这条式子的说法，真正乘在视点位移上；照字面去改 mvp 会双重缩放。
* 常量内容（`:964-979`）是本设计的可取处：无日光、无高光、无雾、无自发光、lightmap 因子 1，
  只保留地图自带的 albedo/uv——天空盒里除了这些，世界光照的假设都不成立。

### 2.2 一遍 + 深度清除时机

1. 清屏（Color+Depth，`:220`）
2. 2D cubemap（`:381-394`，`write-off`）
3. **3D skybox 改绑 `depthStencilState_`（测试开 + 写入开）**，去掉 §2.0 的 pin → 内部自遮挡成立
4. **只清深度**（`ClearDepthStencilView(..., D3D11_CLEAR_DEPTH, 1.0f, 0)`，保留 Color）
5. 主世界不透明 / 水面 / 实体 / 半透明照常画 → 前景无条件覆盖天空

第 4 步是可选的稳妥做法（不清也可以，只要天空的 NDC 深度确实在远平面附近）；但既然 §2.0 去掉了 pin，
天空内部就有了真实前后关系，清深度把「天空 depth 会不会挡到主世界」这个问题从设计里删掉，建议保留。

### 2.3 面剔除

3D 刷面是外表面建筑/地形，剔除态应与主世界一致：
`worldBackfaceCull_ ? rasterizerState_ : worldNoCullRasterizerState_`（`:498-499`），
而不是 2D 盒的 `worldSkyRasterizerState_`（`:448`）——除非实测绕序确实与 2D 盒一致。

## 3. 验证计划

* 同机位 `--world-sky-3d on` vs `off`，看 `worldSky3dDrewThisFrame_` / `worldSky3dDrawnTriangles_`（`:492-493`）与 `11-sky3d` 深度捕获（`:488`）。
* 正控：`--world-geometry sky` 掩码下，非天空前景 `diff(on, off) == 0 px`，天空窗 `diff > 0 px`。
* 负控：把 `:946` 的 `sky3dScale` 临时改 1.0f，天空窗像素必须显著位移（证明缩放真的起作用）。
* 内部自遮挡的正控（§2.0 的验收）：给一块自带前后层次的天空（两层建筑），
  去掉 pin 且开写入后，近层必须遮住远层；留着 pin 时应该能看到远层「漏」出来——这条要把两种配置都拍下来，
  否则没人能证明 §2.0 是必要的。

## 4. 本版不做

* 不补 `sky_camera` 的朝向字段与 3D 天空雾效。
* 不动 2D 天空渐变与 `skyPixelShader_`。
* 不纳入光照匹配。

## 5. 未决

* `worldSky3dBatches_` 的 PVS 切分本轮未通读，需确认 3D 刷面是否已从 `worldDrawBatches_` 里剔除（否则会被画两遍）。
* `:448` 用 `worldSkyRasterizerState_` 的绕序假设需抓帧确认，再决定是否并入 §2.3。
