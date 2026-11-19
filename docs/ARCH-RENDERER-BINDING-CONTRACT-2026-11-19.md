# T3：Renderer 共享着色器 / 绑定契约风险图（只读设计）

- **目标文件**：`docs/ARCH-RENDERER-BINDING-CONTRACT-2026-11-19.md`（只读设计，未改源码）
- **基线提交**：`work/integrate` @ `69804728f368099aeda7b2e5a8e89056521b3611`（所有行号均为该基线实测）

---

## 1. 绑定矩阵（`native/src/native_renderer_frame.cpp` 各 Draw 站点）

| Pass | 绑定/绘制行号 | VS | PS | InputLayout | 顶点结构体 / Stride | BlendState | DepthStencilState | 绘制内容 |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **2D Skybox** | `:381–394` | `worldVertexShader_` | `skyPixelShader_` | `worldInputLayout_` | `WorldVertex` (112 B) | 沿用前态 | `worldSkyDepthStencilState_` | 六面 cubemap 天空盒 (`Draw`) |
| **3D Skybox** | `:449–463` | `worldVertexShader_` | `worldPixelShader_` | `worldInputLayout_` | `WorldVertex` (112 B) | `nullptr` | `worldSkyDepthStencilState_` | 缩放 3D 天际几何 |
| **World Opaque** | `:995–1000, :1106, :1158` | `worldVertexShader_` | `worldPixelShader_` | `worldInputLayout_` | `WorldVertex` (112 B) | `nullptr` | `depthStencilState_` | 世界不透明与 alpha-test 批次 (`DrawIndexed`) |
| **World Translucent** | `:1096–1102, :1158` | `worldVertexShader_` | `worldPixelShader_` | `worldInputLayout_` | `WorldVertex` (112 B) | `worldAlphaBlendState_` | `worldBlendedDepthStencilState_` | `$translucent` 世界面 (`DrawIndexed`) |
| **World Fallback Quad** | `:1260–1266` | `vertexShader_` | `pixelShader_` | `nullptr` | `SV_VertexID` (无 VB) | 沿用前态 | 沿用前态 | 无地图时占位四边形 (`Draw(6,0)`) |
| **Water** | `:1322–1386` | `worldVertexShader_` | `worldPixelShader_` | `worldInputLayout_` | `WorldWaterVertex` (112 B) | 沿用 (`nullptr`) | 沿用 (`depthStencilState_`) | 世界水面 (`Draw`) |
| **Entity Markers** | `:1412–1433` | `worldVertexShader_` | `worldPixelShader_` | `worldInputLayout_` | `WorldVertex` (112 B) | 沿用 (`nullptr`) | 沿用 (`depthStencilState_`) | 实体坐标轴调试线 (`Draw`) |
| **Entity Model** | `:1445–1583` | `entityModelVertexShader_` | **`worldPixelShader_`** | `entityModelInputLayout_` | `EntityModelGpuVertex` | `materialBlend` (alpha/add/mod/null) | `worldBlendedDepthStencilState_` 或 `depthStencilState_` | 实例化玩家/道具/武器 (`DrawInstanced`) |
| **Overlay Silhouettes** | `:1663–1700` | `overlayVertexShader_` | `overlayPixelShader_` | `entityModelInputLayout_` | `EntityModelGpuVertex` | `nullptr` | `overlayXRayDepthState_` / `overlayDepthTestedState_` | 玩家轮廓 (`DrawInstanced`) |
| **Overlay Lines** | `:1902–1906` | `overlayLineVertexShader_` | `overlayPixelShader_` | `overlayLineInputLayout_` | Overlay line vertex | `nullptr` | 同上 | 投射物轨迹线 (`Draw`) |
| **Viewmodel** | `:2031` (`viewmodel.cpp:465,648`) | 见 `native_renderer_viewmodel.cpp` | 见 `native_renderer_viewmodel.cpp` | 见 `native_renderer_viewmodel.cpp` | `EntityModelGpuVertex` | 见 §5 静态无法定论 | 独立 `ClearDepthStencilView` (`:648`) | 第一人称手臂 + 武器 |
| **2D HUD** | `:2342–2372` | `hudVertexShader_` | `hudPixelShader_` | `hudInputLayout_` | HUD 顶点 (44 B, `native_renderer.cpp:346–352`) | `hudBlendState_` | `hudDepthStencilState_` | 2D HUD 面板与文字 (`Draw`) |

---

## 2. 共享资源清单（被 ≥2 个 Pass 复用）

| 共享资源 | 复用站点 (`native_renderer_frame.cpp`) | 复用性质 | 风险 |
| :--- | :--- | :--- | :--- |
| **`worldPixelShader_`** | 3D Skybox (`:460`)、World Opaque/Translucent (`:1000`)、Water (`:1326`)、Entity Markers (`:1417`)、**Entity Model (`:1450`)** | **历史遗留高危复用**（Entity Model 用独立 VS 喂同一 PS） | 新增任意世界 PS 输入语义若漏改 `entityModelVertexSource`，实体整帧损坏（已踩过 T61 与 G7 两次） |
| **`worldVertexShader_` + `worldInputLayout_`** | 2D Skybox (`:383,385`)、3D Skybox (`:451,454`)、World (`:995,997`)、Water (`:1322,1324`)、Entity Markers (`:1412,1414`) | 故意复用（靠 `static_assert(sizeof(WorldWaterVertex)==sizeof(WorldVertex))` 守门） | 改 `WorldVertex` 必须同步 `WorldWaterVertex`（位于 `native/include/world_material.h`）与 `worldElements[]` |
| **`entityModelInputLayout_`** | Entity Model (`:1445`)、Overlay Silhouettes (`:1684`) | 故意复用（同一网格 VB 画本体与轮廓） | 改 `EntityModelGpuVertex` 需同步两个 VS |
| **`worldBlendedDepthStencilState_`** | World Translucent (`:1102`)、Entity Model Translucent (`:1559`) | 故意复用（关深度写、开深度测） | 低；注意每次 pass 末尾已显式恢复 `depthStencilState_` |

---

## 3. 插值语义契约表（`native_renderer_shaders.cpp`）

本驱动对**PS 声明但 VS 未写出的插值语义不报错且读作 `1.0`**（实证见 `native_renderer_shaders.cpp:902–917` T61 事故与 `:922–929` G7 事故）：

| 语义 | `worldVertexSource` Output (`:53`) | `worldClipCaptureGeometrySource` (`:80–127`) | `entityModelVertexSource` Output (`:895–930`) | `worldPixelSource` Input (`:222`) | 事故面评估 |
| :--- | :---: | :---: | :---: | :---: | :--- |
| `SV_POSITION` / `COLOR0` / `TEXCOORD0` / `NORMAL0` / `TEXCOORD2` / `TEXCOORD3` | 全写 | 全透传 | 全写 (`lightUv=(-1,-1)`) | 全消费 | 安全 |
| `TEXCOORD4` (`blend`) / `TEXCOORD5` (`blendModulate`) | 全写 | 全透传 | 写中性值 `-1` / `0`（T61 修复） | 消费 | 曾触发 T61 实体全黑（10,845 px） |
| `TEXCOORD6` (`feature`) / `TEXCOORD7` (`tangent`) | 全写 | 全透传 | 写中性值 `0`（T61 修复） | 消费 | 曾触发 T61 误进 detail/bump |
| `TEXCOORD8` (`tileRect`) | 写 `input.tileRect` | 全透传 | 写中性值 `float4(0,0,0,0)`（G7 修复 `:929`） | 消费 (`:292+`) | **曾触发 G7 实体 68% 错帧**；任何未来 `TEXCOORD9` 必重演 |

---

## 4. 新增语义可脚本化检查清单
1. **枚举所有喂给目标 PS 的 VS**：
   `grep -n "PSSetShader(worldPixelShader_" native/src/native_renderer_frame.cpp` → 核对每个站点绑定的 VS，确认所有 VS `Output` 与 `worldClipCaptureGeometrySource` 均声明新语义并写入中性值（如 `0.0` 或 `-1.0`）。
2. **顶点结构体与输入布局同步**：
   若改 `WorldVertex`，必须同步 `WorldWaterVertex`（`native/include/world_material.h`）、`renderer_internal.h:96–106` 的 `static_assert(sizeof)` + `offsetof` 链、以及 `native_renderer.cpp:419–438` 的 `worldElements[]` 与 `std::size(worldElements)`。
3. **零回归帧校验**：
   默认 env 下跑 tick-0 抓帧，要求 `md5 == 0fcb6867c46d55d9ef9fc95bf65596c2`，且 `--world-entity-coverage` 区域像素差为 0。

---

## 5. 按风险排序的 5 条技术债

| 排序 | 位置（文件:行号） | 触发条件 | 建议动作 | 本轮是否做 |
| :-: | :--- | :--- | :--- | :---: |
| **1 (高)** | `native_renderer_frame.cpp:1450` + `native_renderer_shaders.cpp:895–930` | `worldPixelShader_` 被世界与 Entity Model 跨 Pass 共享，新增世界 `TEXCOORDn` 易把实体拉爆 | 为 Entity Model 拆出独立 `entityPixelShader_`（或把共享契约固化进编译期/脚本门禁） | 否（单独立项） |
| ~~2 (高)~~ **已修** | `native_renderer.cpp:419–438` | 原债为 `CreateInputLayout(worldElements, 9, ...)` 硬编码元素个数；**G7（a64b549f）已改成 `static_cast<UINT>(std::size(worldElements))`，本条结清**（整合时实测 `native_renderer.cpp:440`） | — | 已修 |
| **3 (中)** | `renderer_internal.h:47–106` vs `native/include/world_material.h` | `WorldVertex` 与 `WorldWaterVertex` 分散在两个头文件却共用 `worldInputLayout_` | 保留并扩充 `offsetof` 链，或在注释中显式标注 `WorldWaterVertex` 所在头文件 | 否（只读设计） |
| **4 (中)** | `native_renderer_shaders.cpp:79–128` | `worldClipCaptureGeometrySource` 手工拷贝全部 10 个插值字段，漏抄一个就污染 `--world-clip-capture` 帧 | 每次修改 `worldVertexSource::Output` 时把 GS 纳入 §4 必查清单 | 否（只读设计） |
| **5 (待核)** | `native_renderer_entity.cpp:154, :165` | 任务提示该处曾用 `sizeof(WorldVertex)` 写 48B `EntityModelGpuVertex` 缓冲；但 `69804728` 实测 `:145–167` 为 `entityMarkerVertexBuffer_`（`WorldVertex` 匹配） | 需本地 `grep -n "sizeof(WorldVertex)" native/src/native_renderer_entity.cpp` 复核是否在其他行仍存在 48B 越界 | 否（见 §6） |

---

## 6. 静态无法定论清单
1. `native_renderer_entity.cpp:154/:165` 在 `69804728` 上为 `entityMarkerVertexBuffer_`（使用 `WorldVertex`），未见 48B `EntityModelGpuVertex` 越界；若该债位于同文件其他行号，需全文件 grep 确认。
2. `drawViewModel`（`native_renderer_viewmodel.cpp:465–794`）内部绑定的具体 VS/PS/InputLayout/BlendState 变量名及 `drawWorldSkyGeometry3d` 在 `native_renderer_frame.cpp:482` 之后的 `DrawIndexed` 精确行号本轮未展开读完。
