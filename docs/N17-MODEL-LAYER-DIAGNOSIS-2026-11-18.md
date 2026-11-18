# N17 模型层诊断卡 —「玩家模型（Scout 代表）外观可疑」五层定因

| 项 | 值 |
|---|---|
| 卡片 | n17-model（只读诊断卡，不改产品源码） |
| 日期 | 2026-11-18 |
| 工作树 | `D:/TF2_Native_Worktrees/t105-integrate`，HEAD **f25e954f**（其上 7aae441d 文档快照），working tree 仅 `.build-mapui.log`/`.build-mapui2.log` 未跟踪 |
| 边界 | 不构建、不运行产品、不抓帧、不 commit、`native/` 只读；本卡只产出本文件与 `.scratch/n17-model-scout/*` 证据 |
| 被测产物 | 产品 `sha256 f341ab1e…`（3,185,664 B；probe `09f6565d…`）；抓帧 trace 在 `.scratch/n17-scout-final/`（原始 GBK+CRLF，用 `decode_trace.py` 转 UTF-8 后再读） |
| 资产真值 | `D:/SteamLibrary/steamapps/common/Team Fortress 2/tf` — **磁盘上没有 `models/` 目录**，全部资源在 `tf2_misc_dir.vpk`（104,343 条目）+ `tf2_misc_000..028.vpk` 分卷；抽取副本 `.scratch/n17-model-scout/assets/`（scout.mdl 611,668 B / scout.vvd 529,152 B / scout.dx90.vtx 311,924 B） |
| 复现脚本 | `.scratch/n17-model-scout/`：`vpk_scan.py` `vpk_extract.py` `vpk_vtx.py` `mdl_scan.py` `mdl_meshes.py` `vvd_hdr.py` `vvd_blocks.py` `vvd_pool2.py` `vtx_idx.py` `vtx_dump.py` `vtx_map2.py` `idmap.py` `decode_trace.py` |

判词只有三种：**已证**（源码 + 资产读数/抓帧读数双向闭合） / **已排除**（有反证） / **无法判定**（现有证据不足以裁决，附能裁决它的探针）。不写「已修复」——源码改动算证据，不算验收。

## 0. 结论先给

| # | 假设 | 判词 | 关键证据（一句话） | 对「外观可疑」的解释力 |
|---|---|---|---|---|
| H1 | LOD 选太粗导致变形 | **已排除** | 全仓 `modelLod` 仅 3 处、无消费者（`native/include/native_renderer.h:74`、`native/src/native_renderer_settings.cpp:42`、`:52`）；实际恒取文件最小 LOD=0（`native/src/model_loader.cpp:1476-1500`） | 无（只会「过精细」，不会变形） |
| H2 | VTX→VVD 索引映射错位导致网格散架 | **已排除** | 离线复现 `applyVvdPoolMapping` 得 **31 runs / cursor=8261**，与 `scout.vvd` 的 `numFixups=31`、`numLODVertexes[0]=8261` 精确吻合；按此映射逐 mesh 还原 bbox 解剖全部合理 | 无（产品走的是正确路径） |
| H3 | UV/贴图错导致错色、错采样 | **部分排除** | `overlay uv key=models/player/scout.mdl#class-fallback verts=23442 uvMin=0.0038,0.0028 uvMax=0.9967,0.9995 nonFinite=0`；材质按 (slot,family) 分离且有 RED/BLU 两套实数 | 剩 `312/313` 那条缺 `textureView` 的 range（`native/src/native_renderer_frame.cpp:1183` 静默跳过） |
| H4 | 权重未归一/丢骨导致拉扯 | **已排除** | VVD LOD0 权重和 mean=min=max=**1.0000**、零权重 0%；17/17 玩家 `skinBones==validBones` | 无 |
| H5 | bodygroup 叠画（帽子/耳机重复） | **已证缺陷，当前可见影响 0** | `native/src/native_renderer_entity.cpp:571` 只有 `mapStaticProp` 才按 `m_nBody` 过滤；但 Scout 各 bodygroup 的 model1 顶点数=0，叠画不可见 | Scout 无；对有变体模型的职业待验 |
| H6 | 动画相位不推进（角色冻在第 0 帧） | **无法判定** | 抓帧冻结在单一 `tick=11057`（全 trace 只有 2 行 animsum、同 tick），而 `native/include/animation_clock.h:99` 明确「重复 tick 不推进」→ `advanced=0` 是抓帧方式的结果，不是缺陷证据 | 需动态抓帧 E4 |
| H7 | `stand_PRIMARY` frame 0 就是大幅弓步 | **未归因（最高优先）** | 动画帧截图 = 大跨步前倾弓步；同帧其它职业站姿正常；`stand_PRIMARY frames=45 activity=0 flags=2048 animDesc=386 blends=1` | 高 |
| H8 | 躯干斜跨的深色条状物 | **未归因** | 与 Scout 同实体 ID `(91,0,0)`；bind pose 与动画帧都在；**不在** scout.mdl LOD0 池几何（8261 顶点）的任何 mesh bbox 内 → 是绘制阶段产生的几何 | 高 |
| H9 | 尺度偏大 1.33×（世界 FOV 混用） | **源码已改，运行未验** | `5a599d1e fix(camera): the world camera FOV is Source horizontal-at-4:3…`；但 trace 里 **没有任何 fov 行**可自证该二进制含此改动 | 中（若旧帧则整幅画面尺度不可信） |
| H10 | 武器/挂件缺失（手上无枪、无面具挂件） | **已证** | `attachReserved=0 attachDrawn=0 droppedProps=42`、HUD `attachment=unknown modelSkin=none`、`deferredNotResident=35` | 中-高（用户说的「不对」很可能包含「没枪」） |

## 1. 证据形态与可复现性

1. **资产真值（A 级，可离线复现）**：VPK 树解析 → 抽 `.mdl/.vvd/.dx90.vtx` → 逐结构解码。全部字段用「字节算术闭合」验过，不是照教科书偏移硬套。踩过的坑写在 §6 附录 C。
2. **产品读数（B 级）**：抓帧 trace 的实体台账 + 预算/HUD 行 + GPU overlay 行 + ID 调色板帧（`(colorID,0,0)` 即实体 ID 着色）。
3. **源码引用（A 级）**：每条都带 `文件:行`，行号对 f25e954f。
4. **离线复现产品算法（A- 级）**：把 `applyVvdPoolMapping` 逐字用 Python 重写并与其自带的收尾校验比对（`cursor != vvdLodVertexCounts[0]` 就 fallback，`native/src/model_loader.cpp:1031`）。这一步是 H2 能被判死的唯一办法。

## 2. 五层定因表

### L0 — LOD 与 bodygroup 选择

| 项 | 内容 |
|---|---|
| 现象 | 担心「画的是粗 LOD / 子模型被全画」 |
| 机制假设 | `RenderSettings.modelLod=4` 生效 → 画 LOD4；`preserveBodygroups` 打开后所有 bodygroup 的 model0+model1 都画 |
| 证据（已证） | ① `modelLod` 全仓 3 处、**无消费者**（死设置）。`native/src/model_loader.cpp:1476-1500` 注释原文 "THE LOD TO DRAW IS THE LOWEST ONE THE FILE DECLARES."，`targetLod = metadata.indices.front().lod` 再对全部 descriptor 取 min → 恒 LOD0。② `native/src/native_renderer_entity.cpp:571` 原文 `if (bucket.mesh->mapStaticProp && !run.selectedForBody(bucket.body)) continue;` → 实体模型（玩家）带的 `bodyBase/bodyCount/bodyModel` 从不被消费；`native/src/main.cpp:7781` 是唯一 `preserveBodygroups=true` 的调用点，`native/src/main.cpp:12664` 是唯一读 `m_nBody` 处。 |
| 反证 Scout 无可见影响 | `scout.mdl`：bodypart base 是位标志 hat=1 / headphones=2 / shoes_socks=4 / dogtags=8；bp1..bp4 的 **model1 顶点数=0**（`bp1 model1 nv=0 vi=312192`、`bp2 model1 nv=0`、`bp3 model1 nv=0`、`bp4 model1 nv=0 vi=396528`），VTX 侧同为空 → 即使全画也没有第二份几何。`m_nBody=0` 恰好等价于「每组选 model0」= 参数本体正确。 |
| 探针 | E5（bodygroup 位与几何联动）、E6（LOD 消费者） |
| 修复成本 | bodygroup 消费：把 `:571` 的 `mapStaticProp &&` 去掉即可，**1 行级**，但必须先有 E5 证明它确实改变画面（否则是「修一个不影响画面的东西」）。LOD：给 `modelLod` 接消费者 + 选 LOD 后重建 range，**天级**，且当前无收益。 |

### L1 — mesh / VTX·VVD 索引与几何

| 项 | 内容 |
|---|---|
| 现象 | 怀疑网格被拆错（丢材质组、少 draw、索引连错顶点 → 平板/长刺） |
| 机制假设 | ① stripGroup 的 `origMeshVertID` 被当模型全局索引用（漏 mesh 顶点块起点）；② pool mapping 失败走 `applyModelVertexBaseFallback` 而无人发现；③ strip 未被膨胀成三角列表 |
| 资产真值 | `scout.mdl` v48：bones=78 textures=17 skinfamilies=8 bodyparts=5；bp0 `scout_morphs_low.dmx` 4 mesh 6358 顶点（材质 0/1/2/3 = scout_red / scout_head_red / eyeball_l / eyeball_r），帽子 146、耳机 474、鞋袜 1047、狗牌 236 → **Σ=8261 == VVD LOD0 顶点数**。`scout.dx90.vtx` LOD0：**9 个 stripGroup / 5291 stripGroup 顶点 / 23442 索引**（与抓帧 `gpu overlay verts=23442` 一致）；`origMeshVertID` 实测是 **mesh 局部**（mesh0 0..4478、hat 0..145、shoes 0..1046…）。 |
| 决定性验证（已排除本层） | 离线复现 pool 映射（lod 降序、bp/model/mesh 升序、`origMeshVertID` 升序压池）→ **derived runs=31、cursor=8261**；与 `numFixups=31`、`numLODVertexes[0]=8261` 三项对齐。按该映射还原逐 mesh bbox：身体 x[−16.22,15.40] y[14.17,70.48]、头 x[±4.05] y[67.77,81.12]、眼球 y[75.37,76.10]、帽 y[75.89,81.27]、耳机 y[70.99,81.61]、鞋袜 y[−0.08,14.66]、狗牌 y[65.28,71.69] — **全部落在解剖学正确位置**。 |
| 反证（量化） | 走 fallback（modelBase+stripGroup 局部索引）→ 帽子被画到 x[−14.39,−7.64] y[33.03,65.62]（大腿/手臂高度），长边(>15u)占 **18.48%**、max 80.5u；若只漏 mesh 块起点 → 长边 **41.38%**。产品实拍是完整人形 → 产品实走 pool 路径。 |
| 探针 | E1（每 range 的 vertexStart/vertexCount 与 23442/9 对账）、E2（逐 range 隔离） |
| 修复成本 | 0（本层无缺陷）。**风险登记**：fallback 分支一旦被触发会立刻画出「散架人形」，而它只写一条诊断不改变画面严重性 → 建议 E1 里顺手断言 fallback 计数=0。 |

### L2 — 材质 / UV / alpha

| 项 | 内容 |
|---|---|
| 现象 | 担心整块错贴图、UV 全 0、alpha 全穿 |
| 证据（已排除主要项） | ① UV：`uvMin=0.0038,0.0028 uvMax=0.9967,0.9995 nonFinite=0`（值域健康，无退化到 0 的通道）。② 纹理实数：slot0 family0 2048×1024 meanRGBA=110.1,64.5,58.6 = `scout_red.vtf`；slot1 1024×1024 meanRGBA=117.2,75.0,58.5 = `scout_head.vtf`；slot2/3 64×32 eyeball；family=1 slot0 = `scout_blue.vtf` meanRGBA=79.7,80.6,84.0 → **两套皮肤都在**。③ family 由 team 派生（t2→f0 RED、t3→f1 BLU），`skinFamily→textureSlotFor` 失败有独立 reason 字符串（`native/include/material_slot_resolution.h:89-110`）。④ `$basetexturetransform` 已应用（`transformApplied=3`）。 |
| 唯一已证缺口 | `entityRanges ranges=313 texturedRanges=312` → 1 条 range 无 `textureView`，被 `native/src/native_renderer_frame.cpp:1183` `if (!range.textureView) continue;` **静默跳过**（不画白，也不报错）；归属未定位。 |
| 探针 | E3（带 `textured=0/1` 的 entityDraw 行 → 直接读出那 1 条的 model/materialSlot） |
| 修复成本 | 定位 **小时级**；修复=把「材质缺失」提升为一次性显式诊断（现在只有计数差） |

### L3 — 骨骼 / 权重 / 动画绑定

| 项 | 内容 |
|---|---|
| 现象 | 姿态可疑（弓步、身体拉长、手臂位置怪） |
| 已排除 | 权重：VVD 权重和精确 1.0、零权重 0%；骨骼规模：17/17 玩家 `skinBones==validBones`（Scout 78/78）；蒙皮链路：`animSkinned=49/96`、`animBones=1459`、`gpuSkin=32/1459` 说明 skin 走 GPU。 |
| 已证（源码层） | ① 动画来自**独立**模型 `models/player/scout_animations.mdl`（`separate=1 bones=78 bindBones=78 sequences=306 idleSeq=175 idleLabel=stand_PRIMARY`）；② 未提交 diff（`native/src/animation_binding.cpp`）三处改动：companion 合并补 `blendCount`、`currentBoneModel*poseToBone` 注释、helper bone（`hlp_forearm_L/R`，MDL 骨索引 76/77，无直接通道）改为发布 parent-relative 变换并以 `acceptedChannels` 判空。③ **采样器只取第一个 anim descriptor**：Scout `stand_PRIMARY blends=1`（单 blend，影响小），但 `run_PRIMARY blends=9`、`PRIMARY_aimmatrix_idle blends=12` → 移动/瞄准类序列必然只取到 1/9、1/12 的混合权重。 |
| 无法判定 | 动画相位是否真不推进：trace 全篇同 `tick=11057`，`animsum: … advanced=0 skipped=0 maxDeltaCycle=0.000000`、实体行 `frame=0/45 cycle=0.000000` 都是**冻结抓帧**的必然结果（见 `native/include/animation_clock.h:99`）。**不能用它判缺陷**。 |
| 探针 | E4（动态抓帧：至少 3 个 tick 的 animsum，判据 `advanced>0` 且 `cycle` 单调增）；E7（离线 ANI 解码，见 §3-U2） |
| 修复成本 | 多 blend：**天级**（要按 seqdesc 的 blend 数组做加权，且要处理 aim matrix 的 quaternion 加权）；helper bone 判据改动的风险：它让「无通道骨」也发布矩阵，若矩阵基变换错，会**放大**姿态错误（这是 H7 的头号嫌疑，见 §3-U2） |

### L4 — 投影 / 尺度

| 项 | 内容 |
|---|---|
| 现象 | 早期怀疑「人物偏大/被推远」 |
| 已核实（源码） | 旧实现把竖直 90° 用在世界投影（`native/src/native_renderer_frame.cpp:488/490` 无水平↔竖直换算）；TF2 的 `fov_desired 90` 是 **4:3 水平 90°**，1264×681 下折算竖直 **73.74°**，同视口差 16.26° → 同距离身体高差 **1.33×**。现在世界 FOV 独立（`worldFov`，存 Source 4:3 水平值再转竖直，默认 90→73.7398°），见 `5a599d1e`。`settings.ini` 实测 `worldFov=90`。 |
| 无法判定 | 抓帧产物是否含该改动：trace 里**没有任何 fov 行**（`grep -i fov` 为空），无法自证。因此「1.33× 尺度差」在**已拍画面**里是否仍存在，取决于产物构建提交，不能靠源码判。 |
| 探针 | E8（在日志/HUD 增加一行生效 FOV，或在同机位用已知尺寸物体反解）；以及必须的 A 级 TF2 同机位参考帧 |
| 修复成本 | 0（源码已改）；验证成本=一次带 FOV 自证行的抓帧 |

## 3. 未归因疑点（按对可见异常的解释力排序）

**U1 — 躯干斜跨的「深色条状物」（最强、最具体）**
- 事实链：① ID 帧里它与 Scout 同为 `(91,0,0)`（`identity.bmp` 采样 (91,0,0) 4530 px；`animated-id.bmp` 4164 px），BLU Engineer 是 `(80,0,0)`，世界是 `(89,158,97)`；② 它在 `skinBones=0` 的 bind pose 帧与 `skinBones=78` 的动画帧**都存在**；③ scout.mdl LOD0 全部 8261 顶点的 7 个 mesh bbox **没有任何条状几何**（见 L1 表）。
- 已排除：蒙皮拉伸（bind pose 帧已有）、独立武器实体错挂（`attachDrawn=0 droppedProps=42`，且 ID 同色）、别的实体（ID 不同色）、资产自带几何（点云干净）。
- 剩余可能：实体绘制路径**多发了一份几何**（多余 draw / instance buffer 里混进别的 slot / 索引偏移用错 base / 某个 range 的 vertexStart 落到了其他模型的数据上）。
- 能定死的探针：**E2**（`rangeDepthSkipIndex_` 已在 `native/src/native_renderer_frame.cpp:1189-1193` 实现，逐 range 关一条看条状物是否消失）→ 直接定位到 (model, materialSlot, range)，再回查该 range 的 `vertexStart/vertexCount` 是否落在 Scout 的 23442 索引内。

**U2 — `stand_PRIMARY` frame 0 呈大幅弓步**
- 事实：动画路径截图是「大跨步、躯干前倾、一腿后蹬」；同帧同场其它职业（Engineer/pyro/soldier/heavy/demo/medic/spy/sniper）是正常站姿 → 不是全局姿态管线坏，是**单序列/单模型**层面。
- 两个互斥假设：**U2a** TF2 的 stand_PRIMARY frame 0 本来就不是直立（那产品对）；**U2b** 采样/绑定错（helper bone 矩阵基变换、blend 权重只取 1 个 descriptor、动画模型骨骼映射错）。
- 裁决探针（离线、不跑产品）：**E7** — 用 MDL/ANI 解码器把 `scout_animations.mdl` 的 `stand_PRIMARY` frame 0 解出来，算与 bind pose 的逐骨偏差；正确实现应给出**小幅度**近直立姿态（TF2 idle 是微幅呼吸），若离线解出的是小幅、而产品画的是大弓步 → U2b 成立。这条不需要 A 级参考帧，成本最低，建议**优先做**。
- 另需登记：`scout-sequences.txt:177 sequence index=175 label=stand_PRIMARY frames=45 activity=0 flags=2048 animDesc=386 blends=1`。

**U3 — 模型选择走 classFallback**
- `scene: PLAYER-MODEL-ASSERT=PASS tick=11057 checked=17 indexResolved=0 noIndex=17 classFallback=17 mismatched=0 unresolved=0 firstMismatch=-` → 17/17 玩家都不是从实体数据解析 model index，而是按类名回退猜模型；且 `entity: id=91 ent=14 model=models/player/scout.mdl` 里 `#class-fallback` 也出现在 GPU overlay 的 uv key 上。
- 后果：任何「模型与实体数据不一致」的情形（Spy 伪装成敌方职业是 TF2 的正面特性）都会被画成真实职业模型；素材/换装/变体同理。**是否算缺陷取决于产品是否宣称支持 disguise/换装**——需产品侧表态，故只登记为「已证行为，待定性」。

## 4. 最小运行实验清单（做完这 8 条，本卡所有「无法判定」都能收敛）

前置：每次实验固定同一 demo/机位/tick（`pl_upward_f12`，eye (500,−2240,240)，pitch 17.8，yaw 127.97，1264×681 客户区），并记录产物 `sha256`。

| 编号 | 实验 | 命令/开关 | 判据（pass 条件） |
|---|---|---|---|
| E1 | 实体绘制明细 | 带 `entityDraw:` 埋点的构建 + `TF2_NATIVE_ENTITY_DRAWS=1` | Scout 恰好 **9** 个 LOD0 range、`textured=9`、`vertexCount` 合计 **23442**、`vertexStart` 全部落在 [0, 8261]；出现第 10 个 range 或越界 → U1 定位成功 |
| E2 | 逐 range 隔离 | `rangeDepthSkipIndex_` 探针（`native/src/native_renderer_frame.cpp:1189-1193`），对 0..8 各关一次 | 存在某个 i 使「深色条状物消失且人形其余部分不变」→ 条状物=该 range；若关任何一条都不消失 → 它不在 range 列表里（改查 instance buffer/多实例） |
| E3 | 缺纹理 range 定位 | 同 E1 的 `textured=0` 行 | 读出该 range 的 model+materialSlot，回查其 VMT 是否存在/能否解码（当前只有 `312/313` 这个差数） |
| E4 | 动画相位动态验证 | 让 demo 连续推进 ≥3 tick 再抓 | `animsum` 出现 `advanced>0`、`maxDeltaCycle>0`、实体行 `cycle` 单调增、`animFrame` 分母>1；两 tick 间 Scout 的 skin 矩阵哈希不同 |
| E5 | bodygroup 消费 | 同帧改 `m_nBody`（Scout 0/1/2/4/8；另选一个「model1 非空」的职业） | HUD `bodygroup` 从 `unknown` 变已知；Scout 几何不变（预期），另一职业几何随位变化（证明选择器接通） |
| E6 | LOD 消费者（可选/性能向） | 临时代码强制 `targetLod=2` | `gpu overlay verts` 从 23442 → 4369（VVD `numLODVertexes[2]`），画面仍完整 |
| E7 | 离线 ANI 解码（**已执行 → 结论见 §7.2，U2a**） | 扩展 `mdl_scan.py`：解 `scout_animations.mdl` 的 `stand_PRIMARY` frame 0 逐骨 delta，与 bind pose 合成后投影成点云 | 离线姿态≈直立小幅 → 产品的大弓步=U2b（采样/绑定错）；离线姿态本身就是大弓步 → U2a（产品对，别改） |
| E8 | FOV 自证 | 日志加一行生效 `worldFov`（水平/竖直/换算来源）或画面里放已知尺寸标尺 | 抓帧日志出现该行且竖直=73.7398°，同时与 A 级 TF2 同机位参考帧比对同距离身高比 ≈1.00 |

## 5. 交付给 lead 的接口

**产品侧需要做的（按性价比排序）**
1. 合 E1/E3 的 `entityDraw:` 埋点（`work/model-visual` 上已有实现，未编译未合并）——它是 U1/E3 的唯一出口。
2. 跑 E2（隔离探针已存在，不需要写代码）。
3. 跑 E4（动态抓帧，改抓帧脚本即可）——它决定 L3「动画不推进」是否真缺陷（现在**不能**判）。
4. 给 FOV 加自证行（E8），否则任何「尺度可疑」都无法归因。
5. 表态 U3（disguise/换装是否要支持），决定 classFallback 是缺陷还是设计。

**必须等同机位 TF2 参考帧才能判的项**：所有「姿态是否与 TF2 一致」的判断（U2 的最终裁决）、以及尺度（E8 的像素比对）。现状：A 级参考仍拿不到（回放第 0 帧 TF2 无相机 `getpos=0,0,0`；回放锁相机 `setpos/setang/spec_player` 无效；tick 空间不同），见 `docs/N17-SESSION-REPORT-2026-11-17.md`。E7 是绕开该阻塞的离线替代。

## 6. 附录

### A. 资产读数台账（可直接引用）
- `scout.vvd`（529,152 B）：version=4、numLODs=7、`numLODVertexes=[8261,6188,4369,2427,1536,1464,1208,1208]`（**LOD 主序累计池**）、numFixups=31、fixupTableStart=64、vertexDataStart=448、tangentDataStart=396,976；闭合：`396,976−448 = 396,528 = 8261×48`、文件 = `396,976 + 8261×16`。
- 顶点 stride 48：weight[3]@0..12、boneIndex[3]+numBones@12..16、position@16..28、normal@28..40、uv@40..48（与 `native/src/model_loader.cpp` 读法一致）。
- `scout.dx90.vtx`（311,924 B）：version=7、numLODs=7、numBodyParts=5、bodyPartOffset=36；stripGroup **stride 25**（numVerts@0、vertOffset@4、numIndices@8、indexOffset@12、numStrips@16、stripOffset@20，**全部相对 stripGroup 自身**）；vertex stride 9（boneWeightIndex[3]@0..3、numBones@3、**origMeshVertID u16@4**、boneID[3]@6..9）。
- 各 bodypart LOD0 索引：bp0 16,572（4 mesh: 8,490 / 5,574+2,388 / 60 / 60）、bp1 帽子 582、bp2 耳机 1,542、bp3 鞋袜 4,176、bp4 狗牌 570；LOD1..6 分别 14,301 / 10,527 / 4,284 / 2,253 / 1,779 / 2,253（仅 bp0）。
- `scout.mdl` 材质槽：0 scout_red、1 scout_head_red、2 eyeball_l、3 eyeball_r、4 scout_blue、5 scout_head_blue，6..16 为 invun/zombie 变体；skeleton 末两根 `hlp_forearm_L/R`（索引 76/77）。

### B. 抓帧台账（原文，`.scratch/n17-scout-final/animated-id.trace.utf8.txt`）
- `:80970 scene: entityRanges ranges=313 texturedRanges=312 texturedInstances=61 skinnedInstances=32 meshes=73 textures=1043`
- `:81041 scene: PLAYER-MODEL-ASSERT=PASS tick=11057 checked=17 indexResolved=0 noIndex=17 classFallback=17 mismatched=0 unresolved=0 firstMismatch=-`
- `:81041 animsum: tick=11057 players=17 moving=0 idle=17 advanced=0 skipped=0 maxDeltaCycle=0.000000`
- `:81041 animdsum: tick=11057 crouch=1 crouchwalk=0 flinch=0 flinchStarts=0 fallback=16`
- `:81041 scene: setInstances draws=506 markers=32 playerDraws=17 propDraws=79 playerBudget=17 propBudget=79`
- `:81041 budget: draw capacity=96 playerBudget=17 propBudgetAfterAttach=79 playerDrawn=17 propDrawn=79 attachReserved=0 attachDrawn=0 droppedPlayers=0 droppedProps=42`
- `:81042 budget: instances capacity=96 submitted=96 accepted=61 slots=684 rejected=missingMesh:0,deferredNotResident:35,badMesh:0,nonFinite:0,budget:0`
- 实体行：`entity: id=91 ent=14 model=models/player/scout.mdl team=2 family=0 state=idle seq=175 label=stand_PRIMARY skinned=1 origin=365.7,-2068.3,129.4 frame_tick=1 cycle=0.000000 frame=0/45 localFrame=0 sectionFrames=0 skinBones=78 validBones=78`
- HUD 能力位：`attachment=unknown bodygroup=unknown viewmodel=unknown modelSkin=none modelSeq=-1`（产品自述未决能力）。

### C. 坑与警告（给下一个人）
1. **trace 是 GBK+CRLF**，`read` 工具会报 invalid UTF-8；先 `iconv -f GBK -t UTF-8//TRANSLIT`。
2. **不要按 `model.vertexindex` 切 VVD 顶点池**判几何——VVD 是 LOD 主序累计池，切错了会看到「帽子在大腿高度」这种**假异常**（本卡一开始就踩了，已推翻）。
3. MDL 的 `sznameindex` 是**相对所属结构体**的偏移，不是相对文件头。
4. `mstudiobone_t` 步长实测 216；`mstudiobodyparts_t` 16；`mstudiomesh_t` 116；`mstudiomodel_t` 148（候选 {148,140}，见 `native/src/model_loader.cpp:52-56`）。
5. 冻结抓帧的 `advanced=0/cycle=0` **不能当缺陷证据**（`native/include/animation_clock.h:99`：重复 tick 不推进）。
6. ID 着色是 `(colorID,0,0)`：Scout 91、BLU Engineer 80；同色=同实体，这是排除「别实体错挂」的关键手段。
7. 本卡所有「源码已改」都只是**源码层证据**，没有一条断言它们在**已拍画面**里生效；凡涉及可见观感的结论都以「待运行验证」标记。

## 7. E7 离线裁决与两项只读补充（2026-11-18 执行，只读）

脚本：`.scratch/n17-model-scout/anim_frame0.py`（逐字复现 `native/src/animation_decoder.cpp` 的采样）、`anim_all.py`（9 职业同帧对照）、`anim_eulercheck.py`（euler 变体自检）、`mdl_bg.py`（bodygroup 顶点表）、`vtx_list.py`（pool 映射 + 逐 range 三角形审计）。

### 7.1 解码器可信度自检（先证明解码器不是错的那个）
- 用**已知会动**的 `scout run_PRIMARY`（seq 188 / animDesc 499 / 49 帧 / 28 根 euler 骨）：逐帧 footZ 4.77→28.01 交替、13/25 帧有脚贴地（<8u）、pelvisZ 稳定 42.70–44.57 → RLE 通道、quat64、composeLocalToModel 全链路正确。
- 因此下面对 `stand_PRIMARY` 的读数是可信的。

### 7.2 裁决 U2a：`stand_PRIMARY` 本身就是一个「大跨步 + 前倾」的**静态**姿势
- `scout stand_PRIMARY` = seq 151 / animDesc 386 / name `@stand_PRIMARY` / fps 30 / frames 45 / flags 1（looping）/ animIndex 559684 / sectionFrames 0。
- **flags 直方图 = `0x20:54` + `0x21:7`**：61 根骨**全部**只有 RAWROT2（常量 quat64），其中 7 根另有 RAWPOS；**没有任何逐帧通道** → frame 0..44 逐字节同姿。抓帧里的 `frame=0/45 cycle=0` 不是「卡住」，是这条 animdesc 里只有一帧内容。
- 姿态读数（模型空间，up=Z，身高 ext=58.6）：pelvis `(-2.39,4.51,38.31)`、head `(10.72,0.28,59.45)`、foot_L `(20.55,10.87,5.15)`、foot_R `(-19.53,-4.98,6.08)` → 两脚 X 相距 **40.1u**、且**都贴地**（z≈5–6），头在骨盆前 **13.1u**。与截图的大跨步/前倾同向。
- 逐骨 delta vs bind（deg，前 10）：`weapon_bone_1 179.10`、`bip_dogtag_0 172.55`、`bip_dogtag_1 171.59`、`bip_pelvis 116.51`、`bip_lowerArm_R 83.11`、`bip_thumb_2_R 76.51`、`bip_hip_L 66.38`、`bip_lowerArm_L 65.27`、`bip_middle_1_R 61.17`、`bip_hand_L 56.55`；其余 ≤55°。
- 对照职业（同 `stand_PRIMARY` 同帧；spread=两脚 X 间距，head/pelvis 为 Z，leanX=head.x−pelvis.x，maxMove=frame0↔中间帧最大骨位移）：

| 职业 | bones | seq/desc | frames | eulerBones | spread | head | pelvis | leanX | maxMove |
|---|---|---|---|---|---|---|---|---|---|
| scout | 76 | 151/386 | 45 | **0** | **40.1** | 59.5 | 38.3 | 13.1 | **0.00** |
| engineer | 73 | 141/454 | 47 | 22 | 13.8 | 63.2 | 37.6 | 7.8 | 0.93 |
| soldier | 86 | 147/297 | 51 | 19 | 20.2 | 61.9 | 36.5 | 17.0 | 1.34 |
| pyro | 76 | 101/312 | 61 | 21 | 9.4 | 64.6 | 41.5 | 2.9 | 0.85 |
| demo | 82 | 91/263 | 34 | 22 | 22.7 | 67.2 | 42.7 | 10.0 | 0.74 |
| heavy | 77 | 62/263 | 61 | 22 | 13.8 | 75.6 | 38.2 | −4.0 | 2.42 |
| medic | 92 | 57/211 | 49 | 37 | 20.4 | 73.6 | 47.4 | 5.9 | 0.44 |
| sniper | 74 | 119/379 | 41 | 23 | 15.5 | 71.8 | 45.1 | −1.6 | 0.56 |
| spy | — | 无 `stand_PRIMARY`（用 `stand_SECONDARY`） | — | — | — | — | — | — | — |

→ Scout 的 40.1u 是全场最宽（第二 demo 22.7），且是**唯一 0 根 euler 骨、maxMove=0.00** 的职业。
- **结论（U2a）**：产品画出的弓步/前倾随资产数据，采样层无错，**不要按「姿态错」去改采样器**。反过来，「idle 不动」在这条 animdesc 上是数据属性（零逐帧通道）；TF2 真实 idle 是否靠 aim-matrix/程序化叠加，以及这条 desc 是否就是 TF2 实际播放的那一条，仍无法离线判定（§5 「必须 TF2 参考帧」项不变）。

### 7.3 顺带反驳一条候选缺陷（euler 顺序，原 C1）——**不要改**
- 产品 `radianEulerToQuat` 用 AngleQuaternion 布局（channel0→roll 槽），Source studiomdl 用 `RadianEulerQuaternion`；同一组通道值喂两式，逐骨 local 差可达 **175°**、模型空间位置差可达 **58u**。
- 但用已知会动的 `run_PRIMARY` 裁决：**产品式** → footZ 4.77–28.01、**13/25** 帧有脚贴地（合理步态）；**Source 式** → footZ 17.49–54.96、**0/25** 帧贴地（整体悬空）。产品式才与该数据自洽 → 原「euler 顺序缺陷 C1」**不成立，已撤销**。

### 7.4 补充 1：9 职业 bodygroup 的 model1/model2 顶点数（脚本 `mdl_bg.py`）
| 职业 | 有额外 model 的 bodypart（各 model 顶点数） |
|---|---|
| scout | hat(146,0) headphones(474,0) shoes_socks(1047,0) dogtags(236,0) → **全部 model1=0** |
| engineer | **rightarm(833, 8391, 0)** |
| soldier | **rocket(0, 628)**、**medal(0, 334)**、grenades(1288) |
| demo | **smile(0, 100)** |
| heavy | **hands(2876, 0, 4882)**（三套 model）、bullets(2069) |
| spy | **spyMask(0, 314)** |
| sniper | **arrows(0, 2382)**、**darts(0, 436)** |
| pyro / medic | 全部 bodypart 只有 1 个 model |

→ **Scout 的「bodygroup 叠加不可见」是职业个案，不是通例**：model 里 model1 恰好为空。engineer 的 `rightarm` model1 有 **8,391** 顶点（是 model0 的 10 倍）、heavy 有第三套手 **4,882**、sniper `arrows` **2,382** —— 这些职业一旦走同一条绘制路径，缺少 m_nBody 过滤会**可见地多画一整套肢体**。E5 的判据成立，且影响面比 Scout 大得多（建议用 engineer rightarm 或 heavy hands 做正对照）。

### 7.5 补充 2：深色条状物与 range 对齐
- **前提更正**：既有 trace **没有逐 range 行**——只有 `scene: entityRanges ranges=313 texturedRanges=312 ... meshes=73` 这类汇总，没有 `vertexCount/firstVertex`。因此「用既有 trace 做 range 级对齐」在这份数据上**不可行**（要 range 级明细必须先合 §4 的 E1 埋点）。
- 离线替代（`vtx_list.py`）：重建 pool 映射（cursor=**8261** ✓）后逐 (bp,model,mesh) 解出 LOD0 的 range 与全部三角形——8 条 range、**7,814 个三角形**、**最长边 5.69u（身高的 9.7%），>10u 的三角形 0 条**。
- LOD0 range 表（材质槽 / sg 顶点 / 索引 / 模型空间 bbox）：

| range | mat | sgVerts | idx | x | y | z |
|---|---|---|---|---|---|---|
| bp0 mesh0（身体） | 0 | 2157 | 8490 | −15.40..15.40 | 14.17..70.56 | −15.59..4.00 |
| bp0 mesh1（头/手材质组） | 1 | 1531 | 7962 | −4.05..4.05 | 67.77..81.12 | −5.97..4.29 |
| bp0 mesh2/3（眼球 L/R） | 2/3 | 18 / 18 | 60 / 60 | ±(0.74..1.90) | 75.37..76.10 | 2.31..2.87 |
| bp1 帽子 | 0 | 140 | 582 | −3.43..3.40 | 75.89..81.27 | −5.20..6.60 |
| bp2 耳机 | 0 | 425 | 1542 | −3.51..5.51 | 70.99..81.61 | −3.03..5.40 |
| bp3 鞋袜 | 0 | 830 | 4176 | −10.89..10.89 | −0.08..14.66 | −7.89..3.45 |
| bp4 狗牌 | 0 | 172 | 570 | −2.98..2.98 | 65.28..71.69 | −5.21..4.32 |

- 推论：**没有任何一条 range 的静态几何斜穿躯干**（唯一覆盖 y≈45..70 的是 body mesh0 自身），且**不存在长三角形** → 排除「多发的 buffer / 索引接错 / 少解一条 range / strip 当 list 解」这四类。
- 剩下的候选只有两个：(a) mesh0 的**表面着色**（贴图暗区/UV 采样）——它在**未蒙皮 bind 帧就已存在**，最支持这一条；(b) mesh0 顶点被骨矩阵位移——但 bind 帧无动画绑定，可能性低。
- 单点先试：`bp4 狗牌` 是唯一 z 伸出胸前的 bodygroup（+4.32），其骨 `bip_dogtag_0/1` 恰好是 delta 第二/第三大（172.5°/171.6°）→ E2 隔离时先关 bp4，再关 bp0mesh1。

### 7.6 交接给 n17-draw
§4 的 E2/E3/E4/E8 判据不变，另加三条：① E2 优先顺序 bp4 → bp0mesh1 → bp0mesh0；② E5 正对照改用 engineer `rightarm`（model1=8391）或 heavy `hands`（model2=4882）；③ 姿态不再按缺陷追（§7.2 已判 U2a），只追「idle 是否有逐帧运动」这条数据属性。
