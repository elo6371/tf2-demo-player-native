# N05 — 919 亮白 / 849 黑点：draw 路径清单与可检验预测

日期：2026-11-15　工作树：D:/TF2_Native_Worktrees/t105-e5　分支：candidate/e5　HEAD：0974860
角色：N05 静态归因分析者（只读）。**本文没有构建、没有启动产品、没有抓帧、没有跑门禁、没有 commit。**

**锚点复核（2026-11-15 22:0x，第二轮）**：初版里 main.cpp 的行号来自被并行修改前的文件。本版**全部 main.cpp 行号在当前工作树上重算过**；native_renderer_*.cpp 的行号也逐条重读。复核方式：对每条引用 grep 出「那句代码」的当前行号并与引用比对。若 main.cpp 继续被改，行号会再漂移——**以 grep 结果为准**。

---

## 0. 范围、读法与判定口径

### 0.1 本轮实际读了什么（全部只读）

- 渲染 TU：native_renderer_frame.cpp、native_renderer_world.cpp、native_renderer_shaders.cpp、native_renderer_viewmodel.cpp、native_renderer.cpp，以及 main.cpp 的 trace 与参数段。
- 旧报告 docs/N05-N06-MAP-DEFECTS-2026-11-15.md（392 行）的已确证结论与 NOT VERIFIED 清单。
- 本文所有「文件:行」都可由 grep 命中；本轮无法在该行确认的，一律标 NOT VERIFIED 或降级成「文件 + 函数名」。

### 0.2 判定口径

一条 draw 路径「可能是成因」需同时满足三件事：(a) 它在帧序里覆盖该像素；(b) 它写入的颜色能在现有测试里留下与缺陷相符的读数；(c) 它的颜色来源（材质、常量、状态）与缺陷的颜色/形状相容。只满足 (a) 不算候选。

### 0.3 五个判据（本文的分析骨架，全部可用现有 trace 或现有开关读出）

**J1 三视图恒等判据。** debug 视图覆写只写进 world draw 的常量（native_renderer_frame.cpp:377-398）和 entity draw 的常量（native_renderer_frame.cpp:697-708）。因此只有 world 与 entity 两条路径会因为换视图而改颜色。sky（envFeatures 固定 (3,0,0,0)，native_renderer_frame.cpp:308）、water（强制 envFeatures.x=0，native_renderer_frame.cpp:585）、markers、projectile lines/track、legacy model、overlay（自有 PS 与常量）、viewmodel（自有 PS）、HUD（自有 PS）在 face-id / albedo / texel 三个视图里给出**同一个值**。
　⇒ 一个像素在三视图里同值，是「它不是 world/entity 画的」的强证据（前提是三帧同一 tick，见 §4）。

**J2 面号可解码判据。** face-id 视图把 BSP 面号写进 colour.rgb，每通道 8 bit（native_renderer_world.cpp:31-37：r=id&0xFF, g=(id>>8)&0xFF, b=(id>>16)&0xFF，a=1）。所以 face-id 帧里任何 world 像素的值必须解码成一个 **0 <= id < 该 map 面数** 的整数；不等通道的值只有在 id 不是 0 时才会出现，而 id 很小时 b 通道必然很小。
　919 读到 (210,208,197) ⇒ id = 210 + 208*256 + 197*65536 = **12,964,050**，超出任何 TF2 map 的面数量级 ⇒ 该像素在 face-id 帧里不是 world 面。
　849 读到 (0,0,0) ⇒ id = 0，是一个**合法**面号（faceIndex 0），也可能是一条不写 colour 的路径写了黑；两者都能通过 J2。

**J3 亮度上限判据。** world PS 的输出 = albedo * lighting（native_renderer_shaders.cpp:409），albedo = lerp(colour.rgb, sampled*colour.rgb, colour.a)（:234）再乘 lightColour（:290-307），lighting = lerp(1, 0.62+0.38*diffuse, lightDirectionAndMode.w)（:371）再加 specular 0.12*pow32（:375）并被 clamp 到 [0,2]（:377）。world 模式下 lighting <= 1，所以输出 <= ~1.12 × albedo，而 albedo <= 该材质纹理的最大 RGB（tiled 材质 colour=(1,1,1,1)、a=1，native_renderer_world.cpp:664-665）。
　旧报告卡 B 实测 METALGRATE013A2 的 VTF **全部 mip 的 max RGB <= 111**。⇒ 一个正确采样的 METALGRATE 像素最高约 124/255，**210 不可能**。919 的亮白不可能是「METALGRATE 被正确采样后变亮」。

**J4 清屏色判据。** albedo/flat/face-id/texel 等视图的清屏色是 (0.055, 0.07, 0.085)（main.cpp:13201-13203：clearR/G/B 三元组，只有 depth 视图 worldDebugView==2 时清 0），8 bit 约 (14,18,22)；**只有 depth 视图清成 (0,0,0)**。
　⇒ 在 albedo 视图里读到 (0,0,0)，不是「这个像素什么都没画」，而是某个路径写了精确 0。849 的 (0,0,0) 因此是一条正面读数，不是「空洞」。

**J5 阶段切片判据（本文最有用的一个）。** debug 视图在 PS 里切在四个固定位置：

| 视图 | 返回点 | 该点之前已发生的阶段 |
| --- | --- | --- |
| texel（开关 main.cpp:3029） | native_renderer_shaders.cpp:193-197，:196 返回 sampled = t0.Sample(uv) | 只有 t0 采样；**blend / detail / lightmap / lighting / fog 都还没发生** |
| albedo（开关 main.cpp:3018） | native_renderer_shaders.cpp:277 返回 albedo | blend 层（:218-232）、unlit/modulate 臂（:248-265）、detail（:266-269）已发生；lightmap（:289-307）、lighting、fog 未发生 |
| face-id（开关 main.cpp:3024） | native_renderer_shaders.cpp:181 返回 colour.rgb | 即面号，与材质无关 |
| uv（开关 main.cpp:3035）/ depth（main.cpp:3016）/ flat（main.cpp:3014）/ entity-*（main.cpp:3040/3046/3051） | native_renderer_shaders.cpp 对应臂（:165 / :183 / :184） | — |

　所以「同一像素 texel=X、albedo=Y」(X != Y) 直接把成因夹在 native_renderer_shaders.cpp:218-269 这四段之间：混合层 lerp、unlit/modulate 臂、detail combine。对 849 而言旧报告给的是 texel=(84,83,81)、albedo=(0,0,0) —— 若两帧同一 tick，这就是一把精确的刀；若不同 tick（见 §4），它只是「两次覆盖不同」的提示。

### 0.4 一个必须先说清的事实：debug 覆写只覆盖两个 pass

world 与 entity 之外的所有 pass 都不消费 debug 视图。这不是推断，是读出来的：world 的覆写在 native_renderer_frame.cpp:377-398（写完立刻用 frameConstants 恢复），entity 的在 native_renderer_frame.cpp:697-708；water 显式把 envFeatures.x 写成 0（native_renderer_frame.cpp:585）；overlay 与 viewmodel 用的是自己的 PS 与常量块（overlay 常量上传在 native_renderer_frame.cpp:810；viewmodel 常量在 native_renderer_viewmodel.cpp:648）。
因此「三视图同值」这条读数能一次排除 world/entity 两条路径，并把剩下的候选压到不消费 debug 通道的那一半。

---

## 1. 实际存在的 draw 路径（逐条，文件:行）

### 1.0 每帧固定的三条

**P1 清屏（颜色缓冲）** — native_renderer_frame.cpp:29-32（ClearRenderTargetView :31、ClearDepthStencilView :32）。颜色来自调用方 main.cpp:13201-13203（depth 视图 (0,0,0)，其他 (0.055,0.07,0.085)）。几何类别：背景常量。材质：无。混合：无（直接写）。深度：不涉及。debug 通道：不消费，但值本身就是常量。

**P2 天空（sky）** — native_renderer_frame.cpp:293-329：skyConstants 在 :305-313 组装（envFeatures=(3,0,0,0) :308、fogParams 置 0 :311、fogColor 白 :312、fogScale :313），常量在 :314 上传，:318-326 绑 sky 常量缓冲，**:329 Draw(worldSkyVertexCount_, 0)**。几何：BSP 的 sky 三角形（native_renderer_world.cpp:49：worldSkyFaceOnly_ 时取 map.skyTriangles）。材质参数：只有 t5 envCube，**不采样 t0 albedo、不读 lightmap、不读 colour**；PS 在 native_renderer_shaders.cpp:168 返回 envCube.Sample(normalize(worldPosition))。混合：无。深度：worldSkyDepthStencilState_ = 深度测试开、**写掩码 ZERO**（native_renderer.cpp:635-637）⇒ 天空只写颜色、不写深度，先画，后面 world 直接覆盖。debug 通道：**不消费**（常量在 sky 自己的块里）。

**P3 世界不透明 batch（world opaque）** — native_renderer_frame.cpp:408-447：逐 batch 循环 :411，半透明分支 :419-421，不透明分支 :424 清 blending，t0/t6/t7 等在 :426-434 绑，**:447 DrawIndexed(batch.indexCount, batch.firstIndex, 0)**；无索引时 :466 Draw(worldVertexCount_, 0)；完全没有世界几何时 :483 Draw(6, 0) 占位 quad。顶点来自 native_renderer_world.cpp:648-720（atlas 重载）：tiled 材质 colour=(1,1,1,1)、a=1（:664-665）、uv 经 worldTileCoord 归一（:715-720）、$basetexturetransform 在 :673-714 先做。材质参数/寄存器：t0 albedo（native_renderer_shaders.cpp:191）、t1 bump、t2 bump2、t3 lightmap、t5 env cube、t6/t7 blend 与 blendModulate（:433-434 每 batch 绑）、t8-t10 detail/其他、b1 proxy 常量。albedo 公式 native_renderer_shaders.cpp:234；lightmap 与 gamma :290-307（唯一的 gamma 编码点，pow(light,1/2.2)）；无 tone-map、无曝光、无独立 gamma 输出级（:409 直接返回 albedo*lighting）。混合：不透明分支关。深度：LESS_EQUAL、写。debug 通道：**消费**（native_renderer_frame.cpp:377-398）。

**P4 世界半透明 batch（world blended）** — 同一个 batch 循环，native_renderer_frame.cpp:419-421：batch.alphaMode==1 时绑 worldAlphaBlendState_（SrcAlpha/InvSrcAlpha，native_renderer.cpp:661 创建）与 worldBlendedDepthStencilState_（深度测试开、**写掩码 ZERO**，native_renderer.cpp:646-649）。顶点 featureMask bit8（native_renderer_world.cpp:780，bit 8 = $translucent、bit 16 = $alphatest，注释在 :767-777）由 VMT 的 $translucent 置位（:778-781）。alphaMode 判定在 native_renderer_world.cpp:306-322：材质声明 translucent 但**没拿到 atlas tile** ⇒ 标 refused（:309-310），drawn 时走 untiled flat 分支且 alpha=0（native_renderer_world.cpp:771-773 的注释说明这条路径），也就是不混合、直接写不透明色。debug 通道：消费（同一个 PS）。

**P5 世界 cutout** — 同一 batch 内，PS 在 native_renderer_shaders.cpp:386 discard（featureMask bit16 = $alphatest，native_renderer_world.cpp:779-781）。注意 **discard 在 albedo 返回（:277）之后**：albedo/texel 视图会看到本应被裁掉的像素。

**P6 世界单材质 fallback** — native_renderer_frame.cpp:466 Draw(worldVertexCount_, 0)；顶点由单材质重载 native_renderer_world.cpp:109-147 生成：每面 shade = 0.78 + (hash%23)/100（:114，写到 r/g/b :124-126）、a = textured?1:0、`--world-refused-mask` 把未贴图的面涂洋红 (1,0,1)（:132-134）、face-id 模式写面号（:140-144）、uv 按纹理尺寸归一（:145-147）。debug 通道：消费。

### 1.1 水

**P7 水面（water）** — native_renderer_frame.cpp:503-600，**:588 Draw(worldWaterVertexCount_, 0)**。状态：worldWaterRasterizerState_（native_renderer.cpp:710 创建）在 :523 绑定。材质：t0 waterTexture、t1 waterBump，t2/t3/t5/t6-t10 全绑 nullptr（:530-542）。常量：waterConstants 复用 world 布局，**envFeatures.x 被强制写成 0**（:585），lightmapFeatures.x=1、lightmapFeatures.y=waterBumpFlag（:583-584），:586 上传，:588 之后在 :590-600 恢复光栅态与空纹理槽。⇒ 水像素走的是产品路径：PS 里 flat/depth/texel/albedo 的调试臂一个都不触发。混合：无（沿用 world 不透明态）。深度：world 深度态。debug 通道：**不消费**。

### 1.2 实体

**P8 实体标记（entity markers）** — native_renderer_frame.cpp:609-635：LINELIST（:633），**:634 Draw(entityMarkerVertexCount_, 0)**，:635 切回 TRIANGLELIST。常量：frameConstants（产品值）。混合：关。深度：world 态。debug 通道：**不消费**。

**P9 实体模型（entity models）** — native_renderer_frame.cpp:647-757，**:749 DrawInstanced(range.vertexCount, range.instanceCount, 0, range.instanceStart)**；两趟外层循环 blended=0/1，range.translucent 为真时用 worldAlphaBlendState_ 与 worldBlendedDepthStencilState_（:721/:726），否则 :753 清 blending。材质：t0 range.textureView、t11（secondTextureView）在 :729、t1 proxy 常量。常量：entity 自己的 lightmapFeatures.x=1.0（:696），debug 覆写在 :697-708（x 映射 :698-707、y 在 :708）。debug 通道：**消费**（但只覆盖 entity 自己的 draw）。

### 1.3 overlay（屏幕空间标记）

**P10 overlay 剪影（silhouette）** — native_renderer_frame.cpp:767-857（overlay lambda：:767 重置 reading、:788-793 可用性判定）：:816 overlayRasterizerState_、:824 overlayPixelShader_、:825 清 blending、:810 上传 overlayConstants_、:834-835 取常量缓冲，**:855 DrawInstanced(range.vertexCount, range.instanceCount, 0, range.instanceStart)**。深度态：overlayXRayDepthState_ / overlayDepthTestedState_（native_renderer.cpp:556/571 创建；XRay 的写掩码是 ZERO，:548）。几何：entityModelInputLayout_ + 实体模型顶点。材质：无 t0（PS 用纯色）。debug 通道：**不消费**（自有 PS）。

**P11 overlay 线** — 同一 lambda 内的线分支（native_renderer_frame.cpp:1057-1065）：:1057 overlayLineInputLayout_、:1060 LINELIST、**:1061 Draw(overlayLineVertexCountByDepth_[slot], ...)**、:1063-1064 计数、:1065 回 TRIANGLELIST。同一个 overlay PS。debug 通道：不消费。

### 1.4 弹道与遗留模型

**P12 projectile lines** — native_renderer_frame.cpp:1080-1105，**:1105 Draw(projectileVertexCount_, 0)**，LINELIST（:1104）；worldVS/worldPS + worldConstants（产品常量），t0 用 worldAtlasTextures_.front()（:1095-1097）。debug 通道：不消费。

**P13 projectile track** — native_renderer_frame.cpp:1114-1135，**:1135 Draw(projectileTrackVertexCount_, 0)**，LINELIST（:1134）；同样 worldConstants + atlas tile 0（:1125-1127）。debug 通道：不消费。

**P14 legacy model** — native_renderer_frame.cpp:1139-1154，**:1154 Draw(modelVertexCount_, 0)**；modelVS/modelPS + 独立纹理。debug 通道：不消费。

### 1.5 视图模型与 HUD

**P15 viewmodel（手臂 + 武器 body）** — native_renderer_viewmodel.cpp：**:638 ClearDepthStencilView(DEPTH, 1.0f, 0)**（viewmodel 自己的深度清，与 world 分离）、:640 viewModelDepthStencilState_、:641 viewModelRasterizerState_（cull none）、:648 常量缓冲、:655 t4 bone；无 range 时整缓冲 :686 Draw(viewModelVertexCount_, 0)；每 range :732 Draw(range.vertexCount, range.firstVertex)（blended 用 viewModelAlphaBlendState_，:701）；武器 body 第二次独立绘制在 :839-898 段（:839/:866 更新 body 常量、**:898 Draw**）。材质：自有 PS，材质常量只有 material=(textured, alphaTest)。深度：LESS_EQUAL、写。debug 通道：**不消费**。

**P16 HUD** — native_renderer_frame.cpp:1424-1455：scissor :1424-1429、hudDepthStencilState_ / hudRasterizerState_ / hudBlendState_ :1434-1437、fontView 绑定 :1451-1453，**:1455 Draw(hudRectVertexCount_, 0)**。debug 通道：不消费。

### 1.6 汇总：谁会被 debug 视图改变颜色

| 路径 | 会被 debug 视图改色 | 依据 |
| --- | --- | --- |
| P3/P4/P5/P6 world | 会 | native_renderer_frame.cpp:377-398 |
| P9 entity models | 会 | native_renderer_frame.cpp:697-708 |
| P1 清屏 | 只随视图换清屏色 | main.cpp:13201-13203 |
| P2 sky / P7 water / P8 markers / P10-11 overlay / P12-14 弹道与遗留 / P15 viewmodel / P16 HUD | **不会** | native_renderer_frame.cpp:308（sky 常量）、:585（water 强制 0）、自有 PS/常量 |

---

## 2. 每条路径的可检验预测

下面每条都写成一个可判真假的三元组：**若它是成因 → 现有 trace 应读到什么 → 现有字段够不够**。

### 2.1 世界路径（P3-P6）

预测：919 若由 world 画出，则 (i) face-id 视图的值必须解码成一个合法面号（J2）；(ii) albedo 视图的值必须 <= 1.12 × 该材质纹理最大 RGB（J3）；(iii) texel 视图必须等于 CPU 在 trace 里报的 texel。现有读数三条全不成立（12,964,050 越界；(210,208,197) 远超 111；view 的 210 与 trace 的 82 差 128）。**因此 world 路径对 919 已被现有读数否证**（前提是三帧同 tick，见 §4）。
　对 849：albedo 视图 (0,0,0) 在 J4 下是「某条路径写了精确 0」；face-id (0,0,0) 解码为 face 0，**合法** ⇒ world 路径对 849 仍活着。若它是成因，trace 应看见：
- tiled 材质 colour=(1,1,1,1)、a=1（native_renderer_world.cpp:664-665）⇒ albedo 恒 = sampled（native_renderer_shaders.cpp:234）；因此 albedo=0 只能来自 sampled=0（tile 本身全黑）或 blend/detail 把它抹成 0。
- 区分两者的字段：main.cpp:9516 的逐点 face-trace 会打印 texel/texelA/albedo/blend（汇总行在 main.cpp:9589）。若 trace 的 albedo=0 ⇒ tile 是黑的（不是 shader 阶段问题）；若 trace 的 albedo≈84 而帧是 0 ⇒ 成因在 native_renderer_shaders.cpp:218-269（blend 层 lerp / detail / unlit 臂）。**现有 trace 足够做这个区分，不需要新字段。**

### 2.2 天空（P2）

预测：若 919 是天空，则 (i) 三视图恒等（J1，sky 常量固定 envFeatures=(3,0,0,0)，native_renderer_frame.cpp:308）；(ii) face-id 视图的值解码越界（J2，因为天空写的是 cubemap 而不是面号）；(iii) 值可以远超任何材质的上限（J3 不适用：天空不乘 albedo）；(iv) hole-trace 的 sky= 必须命中，drawn= 必须为空。
　现有读数 (i)(ii)(iii) 全部成立；只差 (iv) 未做。**这是 919 目前唯一一个与全部既有读数相容、且能用现有开关直接判定的候选。**
　drawn/any/sky 三个 ordinal 在 main.cpp:9671 声明、:9702/:9707/:9716 计算、:9807 打印；汇总在 :9818。含义（打印行 main.cpp:9671 之前的注释原文）：drawn=按出厂开关会画的最前世界三角、any=关掉 cull 与 pvs 后的最前世界三角、sky=最前 BSP 天空三角。
　现有 trace 能否区分：**能**，用 --world-hole-trace（main.cpp:3091 取点、:3117 无值分支、:9807 打印），不需要新字段。缺的只有一次抓帧。

### 2.3 水（P7）

预测：若 919/849 是水，(i) 三视图恒等（J1，envFeatures.x 被强制 0，native_renderer_frame.cpp:585）；(ii) 值由 waterTexture 与 waterBump 决定，与 METALGRATE 无关；(iii) 水的几何在水面位置，屏幕坐标会落在该关卡的液面区域。
　现有 trace 能否区分：**半能**。:585 的强制 0 只能从代码读出，trace 里没有「这一像素由 water pass 画」的字段；但 world 的 face-trace 跳过 water（旧报告卡 A 记的 trace 会跳过 water，**该行号本轮未复核**），所以 trace 报 METALGRATE 本身就说明「不是水」。⇒ (ii)(iii) 可用坐标与关卡几何否证，(i) 无法单靠 trace 区分。缺的字段：一个 per-pass 覆盖计数（例如 water 的绘制像素数），或把 water 的 envFeatures.x 也接 debug 通道。

### 2.4 实体（P8）

预测：若 919/849 是实体标记，(i) 标记是 LINELIST，宽度 1 px，5x5 邻域会出现 15 个唯一色的平滑梯度**与线的形状不符**；(ii) 实体 pass 消费 debug 覆写（native_renderer_frame.cpp:697-708），三视图会不同值。
　⇒ (ii) 可直接否证 919（三视图同值）；(i) 与旧报告的 5x5 统计（15 唯一色、平滑）也否证。现有 trace 能否区分：**能**（--world-entity-coverage 视图 8 = main.cpp:3046、--world-entity-id 视图 9 = main.cpp:3051；视图名表在 main.cpp:8687-8694 的 debug view 行里）。

### 2.5 overlay（P10-11）

预测：若 919 是 overlay 剪影/线，(i) 三视图恒等（自有 PS）；(ii) 颜色是纯色（剪影）或 1 px 线，不会有 5x5 平滑梯度；(iii) overlay 的深度态是 XRay 或 depth-tested，会在前后景之间跳变。
　⇒ (ii) 与 5x5 统计不符。现有 trace 能否区分：**不能**——没有 per-pixel 的 pass 归属字段（overlayPassReading_ 在 native_renderer_frame.cpp:1052 只累计 vertexCount，按 pass 统计，不按像素）。缺的字段：overlay 的像素覆盖掩码视图。

### 2.6 弹道与遗留模型（P12-14）

预测：若 919 是这类路径，(i) 三视图恒等（worldConstants 的产品值）；(ii) projectile lines 是 LINELIST，track 是带 atlas 的细条，legacy model 是独立纹理模型；(iii) 都需要对应实体/子弹存在。
　现有 trace 能否区分：**不能**（没有 pass 归属）。缺的字段同 2.5。

### 2.7 viewmodel / HUD（P15-16）

预测：若 919/849 在这两条路径上，(i) 三视图恒等（自有 PS）；(ii) 位置固定：viewmodel 在屏幕下部/右下（它自己清深度，native_renderer_viewmodel.cpp:638），HUD 在边缘与十字准星。
　现有 trace 能否区分：**能（几何上）**。919,379 在 1264x681 的 73% 宽、56% 高处，不落在典型 viewmodel 区域；849,396 同理。⇒ 除非该帧的武器/贴纸布局特殊，这两条可先按位置排除。

### 2.8 一个横切预测（对所有「非 world」候选都成立）

若 919 属于 P2/P7/P8/P10-P16 中的任何一条，则：(i) 三视图同值；(ii) 它的值**不受** --world-albedo/--world-texel 影响；(iii) 用 `--world-refused-mask` 抓一帧，该像素**不会**变洋红（mask 只改 world 顶点，native_renderer_world.cpp:132-134）；(iv) 用 --world-cull / --no-world-cull 抓两帧，该像素**不变**（cull 只改 world）。第 (iii)(iv) 两条是现成的、零成本的排除实验。

---

## 3. 最小判别实验清单（全部只用现有开关与现有 trace 字段，不改产品行为）

排序原则：先用一次抓帧把 919 的归属定下来（它已经有三条互相独立的读数指向「非 world」），再用现有 face-trace 把 849 夹进 native_renderer_shaders.cpp:218-269 或否证。

**E1（最高优先，一次抓帧判别 919）**：抓帧命令加 --world-hole-trace 919,379;849,396。
- 读 main.cpp:9807 的 **drawn= / any= / sky=** 与 :9818 的汇总行：
　- drawn=none 且 sky=hit ⇒ 该像素由天空覆盖，919 是「世界几何没盖住天空」的显示结果，不是材质变亮；成因转向「该三角为何没被光栅化」（cull/裁剪/深度）。
　- drawn=none 且 sky=none 且 any=三角 ⇒ 有几何但没有被画（cull 或 pvs 或 clip 造成），可用 E4/E5 继续二分。
　- drawn=三角 ⇒ world 真的画了它，J1-J3 的否证作废，回头查材质/常量。
- 同一行还带 frameDepth 与 lightluv/lighttexel/lightrgb/lightfactor/lightbranch（既有字段）。
- 一次能排除的路径：E1 一旦给 drawn=none，直接排除 P3-P6 全部 world 路径与 P9 实体；若 sky=hit 则把嫌疑压到 P2，剩下只差「为何没盖住」。

**E2（同时做，判别 919 是否 world）**：用 --world-face-id 抓同一相机，读 919,379 的 8 bit 值并按 native_renderer_world.cpp:31-37 解码。
- 若解码越界（预计 12,964,050）⇒ 该像素在 face-id 帧不是 world 面，与 J2 一致；
- 若解码成合法面号 ⇒ 我从 (210,208,197) 推出的越界结论作废，需要先复核通道顺序（native_renderer_world.cpp:33-35 写的是 r=id&0xFF 低位）。
- 一次能排除：world 路径（与 E1 互为交叉验证，两条独立读数一致才下结论）。

**E3（判别 849，现有 face-trace 就够）**：--world-face-trace --world-face-trace-points 849,396（及 849 邻域 5 个点；开关 main.cpp:3060/3062）。
- 读逐点打印（main.cpp:9516）的 texel / texelA / albedo / blend / luv / light：
　- trace 的 albedo=0 ⇒ tile 本身是黑，成因在材质/atlas 采样（mip、tile 归属）；
　- trace 的 albedo≈84 而帧是 0 ⇒ 成因在 native_renderer_shaders.cpp:218-269（混合层 lerp、detail、unlit 臂），J5 的刀口成立。
- 注意：face-trace 是 CPU 预测，且旧报告已实测 6/6 depthMatch=0；所以它只能用来比「采样/材质」，不能用来定「哪个三角覆盖」。

**E4（二分 cull）**：--world-cull（main.cpp:3008）与 --no-world-cull（main.cpp:3010）各抓一帧同一相机，比 919,379 与 849,396。
- 像素变/不变 ⇒ 该像素是否由那条受 cull 影响的 world 三角提供；不变则 cull 无关。现有 trace 有 main.cpp:8682 的 cull backfaces/mode 行可对照。

**E5（二分 pvs 裁剪）**：--no-world-vis-clip（main.cpp:3006）抓一帧，比同两像素；hole-trace 的 main.cpp:9818 汇总行同时会报 clipApplied。

**E6（一次性问「材质是不是根本没拿到 tile」）**：--world-refused-mask 抓一帧。
- 919/849 变洋红 ⇒ 该材质被 atlas 计划拒绝（native_renderer_world.cpp:132-134 的 mask 只改 world 顶点），这本身就是一个可解释的「亮/黑」来源；
- 不变 ⇒ 与 refused 无关，同时排除 P4 里 refused-translucent 的 untiled-flat 分支。

**E7（读状态，不看像素）**：抓帧日志里读 main.cpp:8747 的 gpu: world alpha frame blendedTriangles/cutoutTriangles/blendedBatches/blendState/depthWriteOff，以及 main.cpp:8730/:5587 的 per-material alpha 行（stage=consumed/declared）。
- 这些行能回答旧报告 N-2（METALGRATE 运行时 alphaMode 与 featureMask 位）——但它只按材质统计，不能回答「919 这个像素是哪个材质」。

**E8（排除实体）**：--world-entity-coverage（main.cpp:3046）与 --world-entity-id（main.cpp:3051）各抓一帧，看 919/849 是否被实体覆盖。

**E9（判 uv 属性是否到达 PS）**：--world-uv（main.cpp:3035）抓一帧。919 若是 world 且 uv 平坦 ⇒ 该三角的 uv 属性异常（会把 texel/albedo 的读数解释成「采到了别的 tile」）。

**E10（把 849 的 0 归到混合层）**：--world-blend off（main.cpp:3196/3205）与默认各抓一帧 albedo 视图。
- 若 849 在 off 下不再是 0 ⇒ 成因在混合层（native_renderer_shaders.cpp:218-232 或 blend atlas 的 filler，native_renderer_world.cpp:372/398/412 的 worldAtlasFill 区）。
- 若仍是 0 ⇒ 混合层无关，转 detail/unlit。

**E11（把 849 的 0 归到 detail）**：--world-detail off 抓一帧 albedo（开关 main.cpp:3163/3167）。同理，若 0 消失 ⇒ detail combine（native_renderer_shaders.cpp:266-269）。

**E12（只读对照，判断 METALGRATE 的 tile 是否走了 translucent rescue）**：--world-atlas-no-translucent-rescue（main.cpp:2944）抓一帧，配合 main.cpp:8701 的 atlas-plan 行（batches/blendBatches/tiled/refused/translucentRefused）。

**实验设计红线（本轮遵守）**：以上全部只用现有 CLI 开关、现有 trace 行、现有 debug 视图；没有一条要求改产品行为、改 shader、改上传、改状态。新增字段的建议只在 §2 各条的「缺什么字段」里提出，本轮不实现。

---

## 4. 本文件没有证明什么（NOT PROVEN / NOT VERIFIED）

1. **没有证明 919/849 由哪条路径画出。** 本轮没有抓帧（G5/n05 目录不存在），也没有运行产品。本文给的是「哪些路径仍可能 + 用什么读数能判死」的清单，不是归因结论。
2. **没有证明三份 BMP 是同一 tick。** 旧报告记的三个 trace 时间戳 47854.9 / 48593.6 / 48879.1 ms 相差约 0.7 s；若来自同一进程的三次抓帧，则是三个 tick，跨视图逐像素比较只对静止场景成立（旧报告实测 albedo==texel 95.9%、face==albedo 18.1%，4% 的差异已经足以让 849 这种单点读数落到差异集里）。**J1/J2/J4 的结论都带这个前提。**
3. **没有证明 919 的真材质是 METALGRATE。** face-trace 是 CPU 预测，旧报告实测 6/6 depthMatch=0（深度差 4e-4~9e-4）；它报的 tri/material 只是「CPU 认为该像素最前面是什么」，不是帧里的实际绘制者。
4. **没有定位 worldAtlasFill 的取值。** 本轮 grep 到它在 native_renderer_world.cpp 只有三处使用（:372 基础 atlas、:398 混合层、:412 modulate 层），但定义不在 native/src 内，值（是否 0）未读到。E10 的实验设计依赖「未铺 tile 的区域是一个常量」，但不依赖这个常量正好是 0。
5. **没有证明 849 的 albedo=0 来自 blend/detail。** 那需要 texel 与 albedo 两帧同一 tick；E3/E10/E11 才是判定实验。
6. **旧报告及其它会话留下的 main.cpp 行号一律未复核**（例如 face-trace 主体 8712/8741/8775/8862/8882/8889/8903-8911、hole-trace 跳过 water 的 8868、faceid 解码 17804/17808/6343、hole-trace 定义注释的旧行号）。本版 main.cpp 行号全部在本轮工作树上重算；上述旧数字**不随本文件继承**。
7. **没有证明「三视图同值」在 919 上是三次独立运行的同值。** 旧报告只给了「三视图都读到 (210,208,197)」，没给每次的运行号/命令行；若三次用了不同开关（例如其中一次开了 --world-cull），同值的解释就要改。
8. **没有证明 919 的 (210,208,197) 就是天空色。** 天空候选与「其他非 world 路径」在这一条上无法区分；需要 E1 的 sky= 或一张 --world-geometry sky 的掩码帧。
9. **没有读 viewmodel 武器 body 的后半段与 overlay lambda 的收尾**（native_renderer_viewmodel.cpp:900 之后、native_renderer_frame.cpp:1071 之后），本文对这两段的描述只到 grep 到的 :839/:866/:898 与 :1061/:1071 为止。
10. **没有评价这些路径的正确性**（是否该被混合、该不该写深度、alpha 该不该消费），本文只回答「谁能产生这个像素」。