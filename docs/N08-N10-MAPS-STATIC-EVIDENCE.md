# N08 / N09 / N10 地图实体卡 —— 静态证据报告

- 负责人：n08-n10-maps（角色 C）
- 工作树：D:/TF2_Native_Worktrees/t105-e5（分支 candidate/e5）
- 开始时 HEAD：a901ece；本报告写就时 HEAD：83657f1；抓帧复核时 HEAD：6dd853e（**HEAD 在我工作期间被别的 agent 推进过；产品证据绑定 exe sha256，不绑 HEAD**）
- 改动文件：**仅本文件**（docs/N08-N10-MAPS-STATIC-EVIDENCE.md，新增）。未改产品源码、未改 CMake、未重建 exe、未 commit/push/stash。
- 证据目录：D:/_shared/EXEC-2026-11-16/n08-n10-maps/
- 结论（lead 已定判，见第九节）：**N08 = 部分**：模型侧有静态证据（N08-a 静态侧 PASS）、状态侧未观测（N08-b 未实现 / NOT VERIFIED）；**N09 = 实体链/资产/闸门/普查 静态成立，推车跟时间移动 NOT VERIFIED，平台与可破坏物 不适用（七图计数 0）**；**N10 = 部分完成**：可判项 PASS（不盖住前景、六面方向，限 upward / t40000 / 一图一机位）、接缝与 Fallback NOT VERIFIED、sky_camera 正例 UNSUPPORTED（功能不存在）、3D skybox 位置 未实现；剩余缺口并入 N11（七图最终矩阵），不新开卡；4.1(f) 的 $basetexturetransform 未消费属 D51，单列，不作 N10 反证。

---

## 零、结论边界（避免误读）

本报告全部建立在两类输入上：

1. **真实首发 BSP 的静态解码**（E:/demo测试/<map>/地图/<map>.bsp，七张：cp_gullywash_f9 / cp_process_f12 / cp_sultry_b8a / cp_sunshine / koth_bagel_rc11 / koth_product_final / pl_upward_f12）。
2. **产品源码逐行阅读 + 既有只读探针**（native/build-nmake/*.exe）。

**没有** TF2 参考画面对照、**没有**参考图对位。产品抓帧方面：第一次尝试**一帧都没写出**（6.1）；本轮按修正跑法取得了 N10 的**一对开/关帧**（4.3）。因此：

- 凡是「BSP 里有什么」「产品源码消费了什么 / 没消费什么」——可以下结论。
- 凡是「屏幕上画出来对不对」——除 4.3 明确列出的两条像素判据外，**一律不下结论**，标 NOT VERIFIED 或 UNSUPPORTED。
- 4.3 的两帧是**产品自比**（开天空 / 关天空），不是与 TF2 官方画面的对照；「像不像 TF2」本报告不表态。

本报告**没有**证明 D01 / D03–D09 / D40–D50 任何一条的完成或失效。

---

## 一、身份与复现基线

| 项 | 值 |
|---|---|
| 产品 exe | native/build-nmake/tf2_demo_native.exe，2,991,616 B，mtime Nov 14 19:22 |
| 产品 exe sha256 | 1c3580e278a160194783ec2c96d3bd622a5fe10ee2b5b70bafbea3295af0797e |
| 说明 | 该 exe 晚于本报告大部分静态结论产出（19:22 由别的 agent 重建）。凡引用产品源码行为的条目，均已按当前工作树逐行复核并附行号；**行为读数仍需在该 exe 上重跑才算绑定**。 |
| world_geometry_probe.exe | 1ac817626d2b1b06caad5a84c1441dd5c744dedf81e20dc005199c3b7c8e593f |
| world_material_probe.exe | 2724b44b5696772980e83009bf7ebf5b1368fdb5bb4ec6a3719983a6b877feda |
| world_feature_probe.exe | b6aa865c95e581ded7fca48301d72805a2054a50cb0643ed339ca446b49ed069 |
| brush_entity_probe.exe | 607b62d11d325acf9ab4d7f92be0c1dfa52cb6051212cb124668aba6294350a3 |
| entity_model_probe.exe | 2b51619e80df858fd948cbf6c3e2a7f5cc2f82275884469950bd4654e78df42f |
| demo_open_probe.exe | 6f2dddc9c5ffc8ac5b6a49c087106b33112ab756f0a04972b7322919bb265542 |
| minimap_probe.exe | ffcf9ed7d134acbb858187489c273f77443580a988604a07edfe6939d095748a |
| TF2 根 | D:/SteamLibrary/steamapps/common/Team Fortress 2/tf（**pak01_dir.vpk 缺失**，N01 已登记；只有 tf2_misc / tf2_textures / tf2_sound 系列） |

demo 身份（**实测 sha256，用以取代 DEMO-INDEX**）：

| demo | 字节 | sha256 |
|---|---|---|
| cp_process_f12 POV autorecord_2026-09-03_21-05-39.dem | 100,302,450 | b6ed3f24c4aa46ba22e66f6a38b3da129135f511995448e9109d27b0d4dc14ad |
| cp_process_f12 STV 1338f5de…matcha-20260927-1312-cp_process_f12.dem | 90,995,077 | 81ceb1afa4c54ad5fdf14201b1f1a9049c183d11aa70c6ed431a69ebf9f73b94 |
| pl_upward_f12 POV autorecord_2026-08-08_21-43-59.dem | 146,853,666 | 14b4943a8d7e866677091bb5a4843882bfed1105bc5ef27d955bbc4cd3ad00e8 |
| pl_upward_f12 STV a9630c65…match-20261030-2145-pl_upward_f12.dem | 75,663,754 | 7e047aff1f7743915e355eac907a95dc3d981aa0fae5591b6b15346fb77dc4a3 |

---

## 二、N08 —— 控制点状态

### 2.1 静态解码证明了什么

**(a) 七图控制点实体链完整，且可视化实体不是 team_control_point 自己。**

七图 team_control_point 的键集合固定为：angles, classname, hammerid, origin, point_default_owner, point_index, point_printname, point_warn_on_cap, point_warn_sound, spawnflags, targetname, team_bodygroup_0/2/3, team_icon_0/2/3, team_model_0/2/3 —— **没有 team_model_1、没有 team_skin_*、没有 model/modelindex**。控制点本体不挂模型。

真正的外观是独立的 prop_dynamic（models/props_gameplay/cap_point_base.mdl），skin 字段直接写明阵营。以 cp_process_f12 为例：

| CP 实体 | origin | index | default_owner | 对应 prop_dynamic | prop skin |
|---|---|---|---|---|---|
| cp_blu1 | -3540 -1280 580 | 0 | 3 | cp_blu1_prop | 2 (BLU) |
| cp_blu2 | -1664 -2176 772 | 1 | 3 | cp_blu2_prop | 2 |
| cp_3 | 0 0 489.75 | 2 | （无） | prop_cap_C | 0 (neutral) |
| cp_red2 | 1664 2176 772 | 3 | 2 | cp_red2_prop | 1 (RED) |
| cp_red1 | 3540 1280 580.25 | 4 | 2 | cp_red1_prop | 1 |

CP 与 prop 的 origin 差 4 单位（z 580 到 prop 576），因为 cap_point_base.mdl 原点在地面以下。

其余六图 prop 布局：

- cp_gullywash_f9：prop_cap_red_1(skin1) / prop_cap_blue_1(skin2) / prop_cap_blue_2(skin2) / prop_cap_red_2(skin1) / prop_cap_center(skin 缺省=0)。
- cp_sultry_b8a：**混用两种模型** —— cp_blu2_prop 与 cp_red2_prop 用 cap_point_base.mdl；cp_red1_prop / cp_blu1_prop 用 cap_point_small.mdl（skin 1 / 2），cp_mid_prop 用 cap_point_small.mdl（skin 0）。cp_red1_prop 与 cp_blu1_prop 还带 parentname=hatch_main / blueside_hatch_main。
- cp_sunshine：五个全用 cap_point_base.mdl，skin 2/2/1/1/0。
- koth_bagel_rc11：cp_koth_solid(skin0) 与 cp_koth_prop(skin0, parentname=towermove)。
- koth_product_final：prop_cap_1(skin0)。
- pl_upward_f12：**四个控制点没有 cap_point_base prop**，只有 cap1_platform_model / cap2_platform_model / cap3_platform_model（models/props_trainyard/track_midcap.mdl, skin 0）。

**(b) 控制点模型是真的三套皮肤，不是控制台模型（本卡「不是控制台模型」条款的反证）。**

models/props_gameplay/cap_point_base.mdl（ver 45，1904 B）解析（脚本 mdl_clean.py，先剥 vpk_query --dump 的 source=… bytes=N 头行，再按 offset 76 的 dataLength 截断）：

- numtextures=3：cap_point_base / cap_point_base_red / cap_point_base_blue
- numskinref=3、numskinfamilies=3
- skin 表原值：**family0 = [0,1,2]（neutral）、family1 = [1,1,2]（RED）、family2 = [2,1,2]（BLU）**
- numcdtextures=1，cd[0]='models\props_gameplay/'
- 配套资产真实存在：cap_point_base.vvd 96,832 B、cap_point_base.dx90.vtx 43,594 B（cap_point_base.vtx 不存在 —— 产品按 .dx90.vtx 找，native/src/model_loader.cpp:1212，所以这不是问题）
- VMT 三件（vpk_query --dump 实测）：cap_point_base.vmt 245 B / _red.vmt 249 B / _blue.vmt 250 B，都是 VertexLitGeneric；$basetexture 指向各自贴图、$bumpmap ..._normal、$envmap env_cubemap、$normalmapalphaenvmapmask 1；**base 的 $selfillum 是 0，red/blue 是 1** —— 三套皮肤不只是 tint，自发光也不同。
- VTF：base 699,304 B；red / blue 各 1,398,368 B。

**(c) cappoint_hologram 是另一条独立视觉通路，且只有一套皮肤。**

models/effects/cappoint_hologram.mdl（ver 45，4164 B）：numtextures=9、numskinref=9、**numskinfamilies=1**（[0,0,0,4147,0,0,1,2,3]）。九张纹理名字不在 texture 表里（解出来是空串/越界）—— 名字走别处，本报告解析器没解出来，**这一点算未解**，但不影响「只有一套 skin」这个结论。配套 cappoint_hologram.vvd 86,720 B、cappoint_hologram.dx90.vtx 22,755 B。

**(d) 阵营切换的实体网络在 BSP 里是完整的，但产品没有能执行它的东西。**

trigger_capture_area 的输出（cp_process_f12 原文）：

    OnCapTeam1   cp_blu1_prop  Skin  1  0  -1
    OnCapTeam2   cp_blu1_prop  Skin  2  0  -1

即「BLU 占领则把 prop 的 Skin 输入置 1(RED) / 2(BLU)」。这是 TF2 真正的状态机。

**产品侧双关键词复验（tools.ops_grep，逐子目录分别跑，均 status=completed, complete=true）：**

| 关键词组 | native/src | native/include |
|---|---|---|
| team_control_point / OnCapTeam / AcceptInput / entityInput / control_point_master | 0 | 0 |
| FireOutput / InputName / inputName / EntityIO / entity_io / DispatchInput / m_iszValueName / OutputName | 0 | 0 |

**结论：产品没有任何 entity I/O 分发器。BSP 里写好的 Skin 切换永远不会被触发。**

**(e) 小地图的阵营颜色是编译期常量，与 demo 状态零连接。**

native/include/minimap.h:200-206 定义 MinimapControlPointRow { const char* name; int index; int team; float x; float y; }，:203 注释写「2=RED, 3=BLU, 0=neutral/undetermined (the entity values)」。七张表全是 inline const 硬编码（minimap.h:219 起）。例如 kMinimapProcessPoints[] = {{"cp_blu1",0,3,-3540,-1280},{"cp_blu2",1,3,-1664,-2176},{"cp_3",2,0,0,0},{"cp_red2",3,2,1664,2176},{"cp_red1",4,2,3540,1280}}。

绘制侧 native/src/main_hud.cpp:1014-1022 直接 minimapTeamColour(point.team) 取这个常量。main_hud.cpp:829-834 注释自陈理由：**Bounds and control points are the map's design, not a row the BSP exposes -- BspMap carries no entity list -- so at load time there is nothing to read a control point's origin from.**

minimap_probe.exe --minimap-test 自测通过（synthetic bounds 4096²、roundtrip errorPx=0、hittest、zoom、panel、unavailable 分支 koth_bagel_rc13:bounds-invalid / degenerate:world-span-nonpositive、ledger cp_snakewater_final1 scale=0.078293204308 expected=0.078293208064）：**只验证投影数学，不验证七图 CP 表与实时状态**。

**(f) skin 上传侧不会被 2 家族上限截断（风险已排除，附行号）。**

native/include/material_slot_resolution.h:75 定义 constexpr std::uint32_t kMaxSelectedSkinFamilies = 2;（注释 red and blu）；:88-92 selectableSkinFamilyCount() 会夹到 2。

但控制点走 WorldProp 分支，不走 attach 分支：

- native/src/main.cpp:5916-5917 用 std::max(1u, prepared.skinFamilies.skinFamilyCount)，注释 :5913-5914 明写「control points have a third」。
- native/src/main.cpp:4516 才是用 selectableSkinFamilyCount 的 attach 路径（玩家挂件）。

所以 cap_point_base.mdl 的三套 skin 全部上传。绘制侧 main.cpp:10471-10473 用同一个 effectiveSkinFamily(std::max(1u, …skinFamilyCount), m_nSkin)，两侧一致。

**(g) 控制点模型在网络模型白名单里被专门放行。**

native/src/main.cpp:5841-5843：

    if (path.rfind("models/props", 0) == 0 || path.rfind("models/models/props", 0) == 0
        || path.rfind("models/effects/cappoint", 0) == 0) {
      mapNetworkModelPaths.insert(path);

cappoint_hologram.mdl 靠第三个条件进来；cap_point_base.mdl 靠第一个。main.cpp:10440 的 if (!mapNetworkModelPaths.count(path)) continue; 是第二道闸。两者都过。

### 2.2 静态解码不能证明什么（N08 的硬缺口）

| 缺口 | 为什么静态证明不了 |
|---|---|
| **状态根本不切换** | 没有 entity I/O 分发器（2.1d）。BSP 里 OnCapTeam 到 Skin 这条链在产品里**一次也不会执行**。这不是「画错颜色」，是**未实现**；皮肤在整段播放里恒等于 BSP 的 skin 初值。 |
| **小地图 marker 不可能是真实状态** | marker 颜色来自编译期表（2.1e），与 demo 实体零连接。「绑定真实网络/游戏状态与 UI marker」在产品里**没有可绑定的通路**。 |
| **neutral / capturing / RED / BLU 四种样本** | 需要 demo 某 tick 的 team_control_point 实体状态与同 tick 帧。**本卡一张都没取到。** |
| **capturing 态** | TF2 占领过程是 trigger_capture_area 内部计时，不进实体网络快照（area_time_to_cap 是编译期常量）。即使有快照也读不到「正在被占领 37%」。 |
| **bodygroup** | team_bodygroup_* 在 BSP 里有（gullywash 3/1/0，process 3/1/1），但产品 native/src/bsp_map.cpp:466-495 只解析 classname / model / origin / fog*，**完全不读 bodygroup / skin / parentname / spawnflags**。网络实体侧 m_nBody 会进 draw.body（main.cpp:10468-10470），但那是 demo 值，不是 BSP 的 team_bodygroup。两条通路本报告没打通。 |
| **代理时间（texture2 / modulation / alpha）** | main.cpp:5939-5940 有专属 trace map: material path=%s slot=%u family=%u shaderFlags=%u second=%s scroll=%d rate=%.3f angle=%.3f sine=%d period=%.3f min=%.3f max=%.3f，main.cpp:7762-7766 有 map: material-upload path=%s slot=%u family=%u flags=%u secondBytes=%zu —— 说明 scroll / sine / second-texture 这些代理字段确实被读进来了；但**没有跑过产品、没抓到这两行 trace**，所以代理时间对不对判 NOT VERIFIED。 |
| **暂停 / seek 后不用旧状态** | 唯一入口是 native/src/main.cpp:10182 queryEntitySnapshotAtOrBeforeTick(...)，:10194 trace scene: snapshot status=%d stale=%d states=%zu。staleness 会被如实记录 —— **这行 trace 就是判据，但本卡没跑到它**。 |
| **与 TF2 参考画面对照** | 需要参考来源 + 产品帧。**两样都没有。** |
| **覆盖一次状态变更** | 与「状态根本不切换」直接冲突。**在产品补上 entity I/O 之前，这一格无法被满足。** |

### 2.3 N08 结论

**卡级：部分** —— **模型侧有静态证据，状态侧未观测**（lead 裁决 1，见第九节；不使用 PASS 概括整卡）。

- **N08-a 数据模型（BSP 侧四状态的存在与字段）** → **PASS（静态侧）**：「控制点**模型**正确、不是控制台模型」静态侧成立（2.1b / 2.1c / 2.1g：三套真 skinfamily、真 VVD/VTX、真 VMT/VTF、白名单放行）。
- **N08-b 产品状态机（切换 / 暂停 / seek 后的新鲜度）** → **未实现 / NOT VERIFIED**：「控制点**状态**正确」不成立，且不是证据不足，是产品没有实现（2.1d 无 entity I/O；2.1e marker 是常量）。**缺的样本**：① 中立 / RED / BLU / 争夺中四种状态各一的 trace 读数；② 一次状态变更（OnCapTeam1 ↔ OnCapTeam2 触发 prop Skin 1↔2）前后的同机位对照；③ 暂停与 seek 之后同一 CP 的 skin / marker 新鲜度读数。在产品补上 entity I/O 之前，这一半无法被满足。

---

## 三、N09 —— Upward 推车与动态 brush

### 3.1 静态解码证明了什么

**(a) 推车实体链在 BSP 里完整且自洽。**

pl_upward_f12（1106 实体）：

- func_tracktrain（唯一，targetname=minecart_tracktrain）：model=*110、target=minecart_path_1、origin "-1674 -1536 58"、spawnflags=514、startspeed=90、speed=0、orientationtype=1、ManualSpeedChanges=1、ManualAccelSpeed=70、ManualDecelSpeed=150、wheels=20、dmg=10000、height=1、MoveSound=Cart.Roll / StartSound=Cart.RollStart / StopSound=Cart.RollStop。
- team_train_watcher（origin "-1696 -1536 136"）：train=minecart_tracktrain、start_node=minecart_path_1、goal_node=minecart_path_174、handle_train_movement=1、train_can_recede=1、speed_forward_modifier=1、linked_pathtrack_1..4 = minecart_path_44 / _79 / _135 / _174、linked_cp_1..4 = cap_1 / cap_2 / cap_3 / cap_4、env_spark_name=cart_sparks、hud_min_speed_level_1/2/3 = 1 / 60 / 80。
- 175 个 path_track（minecart_path_1..176，缺 _0）。从 _1 沿 target 走到 _175（该节点无 target），**链长 175，无环**。
- 路径几何（本报告计算，用于「直线/弯道两组样本」的选取）：转角 n=173，min 0.0°，max 25.1°，mean 12.6°，**>15° 的 63 处（弯道候选）**，**<1° 的 13 处（直线候选）**；段长 min 20.0 / max 527.8 / mean 83.6 地图单位；z 56..632（爬升 576）；x -1664..1984、y -2048..1536。所有 path_track 的 orientationtype=1；只有 _172/_173/_174 带 spawnflags（0/32/32）。
- 挂在 minecart_tracktrain 下的实体（parentname 精确匹配）：mapobj_cart_dispenser cart_dispenser、info_particle_system cart_particles、trigger_capture_area cap_area（model=*109）、trigger_hurt train2_hurt（*111）、env_spark cart_sparks ×2、dispenser_touch_trigger dispenser_trigger（*113）。

**(b) *110 是个空 brush 模型 —— 推车外观不来自它。这是本卡最容易踩错的一格。**

pl_upward_f12 的 dmodel 表 175 条里，**只有 model 110 的 faces=0**（firstface=14759、bbox (-34,-25,-52)..(54,25,16) = 88×50×68）。model 0（worldspawn）13497 面，其余 173 条非空。

真正的推车外观是 **prop_physics_override minecart_physprop**：model=models/props_trainyard/bomb_cart.mdl、origin "-1664 -1536 32"、angles "0 180 0"、spawnflags=260、disableshadows=1、modelscale=1.0、physdamagescale=0.1、shadowcastdist=0。该模型资产齐全：bomb_cart.mdl 2108 B / .vvd 1,148,912 B / .dx90.vtx 373,768 B / .phy 17,645 B（均在 tf2_misc_dir.vpk）。

实体输出原文里有 attach1 = minecart_tracktrain、attach2 = minecart_physprop、OnPass -> minecart_tracktrain Stop 0.1 -1 等，但**没有任何一条输出指向 Skin / BodyGroup**。

**(c) 产品侧的两道闸，逐条有行号。**

| 闸 | 位置 | 内容 | 对推车的影响 |
|---|---|---|---|
| 一：BSP brush 白名单 | native/src/bsp_map.cpp:585-588 | func_brush / func_wall / func_lod / func_illusionary 四个类放行 | func_tracktrain 不在内，进 BspRefusedBrushModel |
| 同上，面级 | bsp_map.cpp:606 / 649-654 / 675 / 740 | modelDrawn[model] != 0 才产生三角形 | 被拒模型的面**根本不生成三角形**（不是停在原点） |
| 同上，计数语义 | bsp_map.cpp:640 | refused.moving = !className.empty(); | **moving 等价于「类名非空」，不是「真的会动」**。相关计数字段名有误导。 |
| 二：网络模型路径前缀 | native/src/main.cpp:5841-5843 | 只收 models/props、models/models/props、models/effects/cappoint 前缀 | *110 不以 models/ 开头，被挡；**但 models/props_trainyard/bomb_cart.mdl 的前 12 字符正好是 models/props，所以它进得来** |

→ **推车本体（*110 的 brush）不会被画；推车外观（bomb_cart.mdl）走网络实体通路，有资格被画。** 此处我先前写错过一次（曾称「两道闸都过不去」），以本节为准。

**(d) demo 侧确实带来了 brush 模型 id，但探针拿不到状态。**

entity_model_probe.exe --dump-precache "*" 在 upward POV 上：**precache entries=1197，其中 174 条是 *N 形式**（idx=2..175 到 *1..*174）。所以 brush_entity_probe 的候选规则（brush_entity_probe.cpp:207-209 只在 precache->second[0] != '*' 时跳过）**本该命中**，但它报：

    brush_entity_probe summary map=pl_upward_f12 snapshots=99 candidates=0 samples=0
      missing=0 moved=0 max_displacement=0 verdict=no-brush-entities

根因在快照窗口，不在 precache。demo_open_probe --scan 实测：

| demo | ticks | entity_history_first | last | checkpoints | archive | delta_misses |
|---|---|---|---|---|---|---|
| upward POV | 306,131 | 335,909 | 336,297 | 4 | 96 | 2,783 |
| upward STV | 85,204 | 95,879 | 96,135 | 3 | 96 | — |

→ 上表的 first/last 是**扫描期留存策略**的结果，不是产品在任意 tick 的实体可用性。机制（lead 定位，native/src/main.cpp:3489-3492 是产品唯一扫描入口：parseDemoHeaderFile → indexDemoFile → scanKnownDemoMessages，播放前一次性扫完整份 demo）：

- native/include/demo_header.h:318-323 的 limits：maxEvents=8192、maxCheckpoints=8、checkpointStride=128、archiveMax=96。
- 每积累一批就 flush（native/src/demo_header_entity.cpp:183-199）：**只有 checkpoints 进 archive**（:193-195），再 `thinHistoryArchive(archive, archiveMax)` 抽稀（:196），然后 events / packets / checkpoints 全部 `clear()`（:197-199）。
- 所以正确的表述是：**扫描结束时缓冲里只剩最后 ≤8 个检查点，其余被 thinHistoryArchive 抽稀成 96 个稀疏快照，而 events/packets 已丢弃** —— 不是「档案只留了尾部」。这解释了为什么 gap_max 能到 4169（upward POV）而中位数只有 822（demo_header.h:336 的 T53 注记）。

对 N08 / N09 的后果（这是本节真正的结论）：**这是探针在同一留存策略下读到的档案，不是产品在 tick 40000 的实体可用性。** 要测「t40000 的推车/控制点状态」必须用能边播边采的快照路径，或补一条能吃长实体历史的探针 —— 与本报告 6.1 的抓帧结论同源：**播放期取状态与扫描期留存是两条路，不能互相顶替。**

`--snapshot 2000` 一律 snapshot_status=before-history snapshot_states=0。快照查询语义见 native/src/demo_header_entity.cpp:425-488：历史窗口外命中 archive 时返回 **Checkpoint（stale by construction）**，并在 resolvedTick 里如实给出实际 tick 与 staleness。

**(e) 七图动态 brush 普查（真实 BSP，census.py）。**

| classname | gullywash | process | sultry | sunshine | bagel | product | upward |
|---|---|---|---|---|---|---|---|
| func_door | 14 | 18 | 10 | 10 | 10 | 4 | 14 |
| func_door_rotating | 0 | 0 | 0 | 0 | 4 | 0 | 0 |
| func_movelinear | 4 | 0 | 0 | 0 | 0 | 0 | 0 |
| func_tracktrain | 0 | 0 | 0 | 0 | 2 | 1 | 1 |
| func_rotating | 3 | 0 | 0 | 0 | 1 | 0 | 0 |
| func_button | 0 | 0 | 0 | 0 | 2 | 0 | 0 |
| func_breakable | 0 | 0 | 0 | 2 | 0 | 0 | 0 |
| **func_plat** | **0** | **0** | **0** | **0** | **0** | **0** | **0** |
| **func_breakable_surf** | **0** | **0** | **0** | **0** | **0** | **0** | **0** |
| **func_water_analog** | **0** | **0** | **0** | **0** | **0** | **0** | **0** |
| **func_trackautochange** | **0** | **0** | **0** | **0** | **0** | **0** | **0** |
| prop_dynamic | 25 | 15 | 21 | 17 | 23 | 18 | 50 |
| prop_physics_multiplayer | 20 | 2 | 0 | 6 | 7 | 0 | 0 |
| func_areaportal | 62 | 32 | 36 | 58 | 41 | 6 | 72 |
| func_occluder | 0 | 0 | 4 | 0 | 9 | 8 | 54 |
| func_brush（静态放行） | 5 | 44 | 135 | 92 | 202 | 2 | 100 |
| func_illusionary（静态放行） | 81 | 80 | 164 | 43 | 4 | 146 | 0 |
| func_lod（静态放行） | 0 | 0 | 47 | 4 | 53 | 0 | 0 |
| **被拒实例合计** | 128 | 67 | 71 | 93 | 99 | 37 | **191** |

**关键：任务卡点名的「平台」(func_plat) 与「可破坏物」(func_breakable_surf) 在七张首发图里出现次数均为 0。** 按卡面规则 —— **记「不适用」，不记未实现，也不记 PASS**。func_breakable 只有 sunshine 有 2 个。

**「不适用」的可证伪定义（lead 裁决 2）**：「不适用」= 七图 entity lump 与 BSP brush 实体字典里**计数为 0**（上表逐图给数：func_plat / func_breakable_surf / func_water_analog / func_trackautochange 四类 × 七图共 28 格全为 0），因此这不是「漏检」，而是这些地图里没有该实体类。**给了逐图计数才算「不适用」，没给就是「未普查」。**

### 3.2 静态解码不能证明什么（N09 的硬缺口）

| 缺口 | 说明 |
|---|---|
| **推车跟时间移动** | **完全没有证据。** 三个独立原因：① *110 零面，brush 通路不产生几何；② 产品不读 parentname / path_track 链，也不做 func_tracktrain 的曲线插值（bsp_map.cpp:466-495 只解析 classname/model/origin/fog*）；③ 本可用 demo 实体快照验证 bomb_cart 的 origin 序列，但实体历史在扫描结束时只剩最后 ≤8 个检查点（其余被 thinHistoryArchive 抽稀成 96 个稀疏快照，events/packets 已丢弃 —— native/src/demo_header_entity.cpp:183-199、native/include/demo_header.h:318-323），brush_entity_probe 判 no-brush-entities。**bomb_cart 有资格被画 ≠ 它被画在正确位置上。** |
| **直线 / 弯道 × 静止 / 移动 四组 tick** | 一组都没取到。路径几何（63 处弯道 / 13 处直线、段长、转角）已算出，**样本选择的输入齐了，样本本身没有**。 |
| **暂停 / seek / 倍速 / PVS** | 未跑产品。判据存在（main.cpp:10194 的 scene: snapshot status=%d stale=%d states=%zu；main.cpp:11734 的 scene: staticProps … pvsRejected=%zu），但没跑到。**注：4.3 的 trace 里 10194 那条已经在跑**（`stale=` 有读数），可直接拿来当初次判据，只是尚未做「暂停/seek 前后」的对照。 |
| **像素位移** | **仍未取。** 但抓帧通路本身已经打通（4.3a）：`--playback-speed 64 --capture-tick <T>` 在 53 秒内精确停在指定 tick（`capture: armed requested=40000 frame_tick=40000`）。**阻塞从「抓不到帧」变成了「还没排到四组 tick」。** |
| **门的绘制** | func_door 七图共 66 个，全部进 BspRefusedBrushModel。它们连带的 prop_dynamic（upward 的 door_grate002/003_top/bottom、door_slide_door、windowed_door，以 parentname 指向 func_door 名字）会走网络实体通路，但 resolveEntityWorldMatrix 要能沿 moveparent 上溯（native/src/entity_model.cpp:872-892，深度上限 32、m_iParentAttachment > 0 直接拒绝），而父实体本身不被产品建模为可动实体 —— **门的开合在本卡里既没证明也没证伪**。 |

### 3.3 N09 结论

1. **推车显示并跟时间移动** → **NOT VERIFIED**（附一条负向发现：*110 零面 + 产品无 path_track 插值，意味着「跟时间移动」在产品里**没有任何实现路径**）。
2. **动态 brush 不停在错误原点或被静态世界重复绘制** → **NOT VERIFIED**。产品对被拒 brush 模型的做法是**根本不生成三角形**（bsp_map.cpp:649-654），所以「停在错误原点」这个失败模式在产品里**结构上不可能发生** —— 但也因此**不能算通过**，因为对应物件同样没有显示。
3. **七图盘点「门 / 平台 / 可破坏物」** → **平台与可破坏物（surf）在七图出现次数为 0，记「不适用」**（可证伪口径见 3.1e：四类 × 七图 28 格逐图计数全为 0，故不是「漏检」而是这些地图里没有该实体类）；门与推车有实体、有被拒记录、有资产，**未实现的部分如实记未实现**。

---

## 四、N10 —— 天空与 3D skybox

### 4.1 静态解码证明了什么

**(a) 七图各有一个 sky_camera，scale 全部为 16。**

| 图 | skyname | sky_camera origin | scale | angles |
|---|---|---|---|---|
| cp_gullywash_f9 | sky_dustbowl_01 | (10556, -238.21, 4204) | 16 | — |
| cp_process_f12 | sky_trainyard_01 | (-2484, 4550.25, 2568) | 16 | — |
| cp_sultry_b8a | sky_well_02_hdr | (5920, 4764, 3588.44) | 16 | — |
| cp_sunshine | sky_tf2_04 | (-1376, 6840, 138) | 16 | — |
| koth_bagel_rc11 | sky_goldrush_01 | (0, 4352, 270) | 16 | (0,129,0) use_angles=1 |
| koth_product_final | sky_trainyard_01 | (-8192, 0, 238.286) | 16 | (0,310,0) use_angles=1 |
| pl_upward_f12 | sky_upward | (7168, -2048, 0) | 16 | — |

每图恰好 1 个 sky_camera、1 个 light_environment、1 个 shadow_control、1 个 env_fog_controller、1 个 env_tonemap_controller；env_sun 只在 gullywash / sunshine / bagel / upward 有。

**(b) 七图的 worldspawn 里都带着「绕 sky_camera 的 3D skybox 几何」。**

按 model 0 的 faces→surfedges→edges→vertices 求 bbox（sky_cluster.py）：

| 图 | model0 bbox | 可玩 XY（minimap.h） | 超出量 | sky_camera 落在哪 |
|---|---|---|---|---|
| cp_gullywash_f9 | (-5202,-3992,-529)-(12056.4,3992,4904.1) | x ≤ 5202 | +6854 | x=10556（在超出部分内） |
| cp_process_f12 | (-14892,-4608,-14672.8)-(5056,15420,3152) | x ≥ -5056, y ≤ 4608 | +9836 / +10812 | (-2484,4550) |
| cp_sultry_b8a | (-4832,-6016,-128)-(6925,5984,4096) | x ≤ 4768 | +2157 | x=5920 |
| cp_sunshine | (-14892,128,-14672.8)-(-256,15420,1904) | x ≥ -11008, y ≤ 10368 | +3884 / +5052 | (-1376,6840) |
| koth_bagel_rc11 | (-4256,-2816,0)-(4416,5120,1792) | y ≤ 3072 | +2048 | y=4352 |
| koth_product_final | (-9216,-4864,-128)-(2304,4864,1920) | x ≥ -5426 | +3790 | x=-8192 |
| pl_upward_f12 | (-3072,-4096,-2048)-(9216,3072,2048) | x ≤ 4096 | +5120 | x=7168 |

**(c) 但这些超出量的成因必须分开归因，否则会写错。**

按「质心落在可玩 XY 外的非天空、非 nodraw 面」计数，并看它们的 z 与材质（sky_detail.py / zspan.py / sky_mats.py）：

| 图 | 可玩区外三角数 | 该部分 z 范围 | 该部分主材质（按三角数） | 归因 |
|---|---|---|---|---|
| pl_upward_f12 | **1797** (3.8%) | [-288, 424] | TOOLSBLACK 338、METAL/WALL011D 238、CONCRETEFLOOR007 198、BRICKWALL004D 164、BLUE_WALL2_BOTTOM 154、RED_BEAM1 140、REFLECTIVITY_10B 103、BLENDBEACH_TO_BEACH 96（19 种） | **真实 3D skybox detail**（贴地缩略世界）+ 一部分贴地 BLACK 刷 |
| cp_gullywash_f9 | 604 (1.3%) | [4174, 4442] | BLENDGROUNDTOGRASS001 290、ROCKWALL004 122、CONCRETE_TRACK002 70、CONCRETEWALL001E 56、CONCRETEFLOOR003 46、WOOD_BRIDGE001 20（6 种） | **真实 3D skybox detail**（高悬的缩略地形，材质就是地面草地/岩壁） |
| cp_sunshine | 356 (0.7%) | [-14673, -14235] | **TOOLSBLACK 326**、DUST_GRADIENT01 26、DUST_GRADIENT02 4 | **虚空刷（BLACK）**，不是天空盒 |
| cp_process_f12 | 362 (1.0%) | [-14673, -14235] | **TOOLSBLACK 332**、DUST_GRADIENT01 26、DUST_GRADIENT02 4 | **虚空刷（BLACK）**，不是天空盒 |
| koth_bagel_rc11 | 60 (0.2%) | [276, 292] | water_2fort_beneath.vmt 18、water_bagel_0_4352_304 18、BLENDGRASSTOGRASS_BAGEL_NODETAIL 18、BLENDGRAVELTOGRASS_BAGEL_2_NODETAIL 4、CONCRETEFLOOR003 2 | detail + 一点点水面，但未把 span 撑大 |
| cp_sultry_b8a | 16 (0.0%) | [3520, 3960] | WATERGATE/ILLUSION_SEA 8、ROCKWALL014 6、BLENDGROUNDTOBEACHGRASS001 2 | detail（海面 + 岩壁），量极小 |
| koth_product_final | **0** | — | — | 无 |

（这张表的材质列由 outside_mats.py 重算，口径与 sky_detail.py 的 outside 计数逐图对齐：604/362/16/356/60/0/1797 全部一致。**注意 process 与 sunshine 的「虚空刷」结论现在有材质背书：95% 以上是 TOOLSBLACK + OVERLAYS/DUST_GRADIENT，没有任何一个 NATURE/METAL 类 detail 材质**；而 gullywash 的 604 全部是草地/岩壁/水泥类，**是真 detail**。）

**「sky_camera 邻域材质」是判定哪几张图真有 3D skybox detail 的最硬指标**（距 sky_camera ≤2048 的非天空面，sky_mats.py）：

| 图 | near-sky_camera 面数 | 主材质 |
|---|---|---|
| pl_upward_f12 | **690** | METAL/WALL011D 116、TOOLSBLACK 115、COALMINES/RED_BEAM1 69、BLUE_WALL2_BOTTOM 63、DEV/REFLECTIVITY_10B 51、BRICKWALL004D 45 |
| cp_sunshine | 497 | TOOLSSKYBOX 112、METAL/WALL027 90、TILE/SLATEROOF001 84、COASTAL/OCEAN_FADE 54、WOOD_WALL006Z 41、water_well_beneath 40 |
| cp_gullywash_f9 | 324 | NATURE/BLENDGROUNDTOGRASS001 145、NATURE/ROCKWALL004 61、TOOLSSKYBOX2D 42、CONCRETE_TRACK002 28、CONCRETEWALL001E 22、CONCRETEFLOOR003 18 |
| koth_bagel_rc11 | 72 | TOOLSSKYBOX 43、water_2fort_beneath 9 |
| cp_sultry_b8a | 30 | TOOLSSKYBOX 22、WATERGATE/ILLUSION_SEA 4、ROCKWALL014 3 |
| cp_process_f12 | 22 | **只有 TOOLSSKYBOX 22**（纯壳，无 detail） |
| koth_product_final | 14 | **只有 TOOLSSKYBOX**（纯壳） |

→ **真有 3D skybox 缩略几何的：upward（最强）、sunshine、gullywash、bagel**。process / product 只有壳，sultry 只有一点点。

**(d) 产品对天空面的处理（普通六面天空这一半，可以判）。**

- **BSP 解析**：native/src/bsp_map.cpp:768-774 读 texinfo flags，const bool skyFace = (flags & (2 | 4)) != 0;（2=SURF_SKY，4=SURF_SKY2D）。sky 面**不再被丢弃**，而是 build polygon 后逐三角 push 进 map.skyTriangles 并 ++map.skyFaces（:803-831），**然后 continue** —— 明确不进常规 triangles（:769-773 注释自陈）。
- **六面材质路径**：BspParser::skyMaterialPath（bsp_map.cpp:1432）suffix 表 {"rt","lf","bk","ft","up","dn"}，路径 materials/skybox/<skyname><suffix>.vmt。
- **面序与 D3D11 cube 一致**：native/src/world_material.cpp:674-683 的 kSkyFaceBasis[6]（struct SkyFaceBasis 在 :674、六项表在 :676-683），注释（:660-673）记录 T8 修正：+X="rt"、-X="lf"、+Y="bk"、-Y="ft"、+Z="up"、-Z="dn"，并说明它必须与 worldCubemapFaceSuffix 和 BspParser::skyMaterialPath 同序。
- **天空是一个以相机为心、halfExtent = 40 box units 的贴无限远立方体**：native/include/world_material.h:572 的 float halfExtent = 40.0f;（**固定值，全仓没有任何地方按 sky_camera 改写它**）。world_material.cpp:698-758 evaluateSkyDrawRequest：decoded == 0 时 Unavailable（reason no sky side decoded, so there is no box to draw）；**部分面缺失不拒绝**（:725-727 注释 five sides of a skybox still cover five sixths…），state = decoded == 6 ? Active : Fallback（:752）；顶点坐标 halfExtent * (basis.d + a*basis.u + b*basis.v)（:741-743），uv = (0..1) 面内四角双三角 —— **六面齐全时 6 面 × 6 顶点 = 36 顶点 / 12 三角形**（:728-748 每面 6 顶点，:749-750 skyboxVertices / skyboxTriangles = vertices/3）。
- **绘制 pass**：native/src/native_renderer_frame.cpp:275-337 —— **最先画**；XMMATRIX skyView = view; skyView.r[3] = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f); XMStoreFloat4x4(&skyConstants.mvp, skyView * projection);（:303-305）—— 视图矩阵的平移被清零，立方体永远跟着相机走；此处**没有** BSP sky_camera 的 origin / scale=16 转换（4.2a 关键词双目录 0 命中）；skyConstants.envFeatures = XMFLOAT4(3.0f, 0, 0, 0)（:308，mode 3 = 像素着色器 sky 分支，注释 the box is lit by nothing and carries no lightmap uv）；显式 no-fog 哨兵（:309-313，T101）；static_assert(sizeof(SkyConstantsData) == 192, ...)（:301-302）；PSSetShaderResources(5, 1, &skyCube) —— **六面走 texturecube 采样**。
- **深度与遮罩策略**：native/src/native_renderer.cpp:634-635 skyDepthDescription.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;（**测深度、不写深度**）；:687-688 skyRasterDescription.CullMode = D3D11_CULL_NONE;（注释：盒从内部看，world 的 back-face culling 会把六面全剔掉而读数还报 drawn）。相机 kWorldNearMapUnits = 4.0f、far = 100 box units（native_renderer_frame.cpp:166-170 注释 also contains the sky box (halfExtent 40 box units), so it stays）。
- **开关**：native/src/main.cpp:7095 const bool skyEnabled = (worldSkyGiven ? worldSkyRequested : …)；CLI --world-sky / --no-world-sky 在 :2647-2650；--world-geometry sky 会把 skyTriangles 单独上传（native_renderer_world.cpp:49 的 worldSkyFaceOnly_ 分支）。

**这一半（六面天空作为一个面内 UV 无关的立方体 pass）在代码层完整且自洽。**

**(e) 六面 VMT 实测：D51 的定量底稿。**

sky_matrix2.py（逐面 vpk_query --dump，sultry 从 BSP pakfile 读）：

| 图 | skyname | 六面 basetexture | $basetexturetransform | $hdrcompressedTexture |
|---|---|---|---|---|
| gullywash | sky_dustbowl_01 | rt/lf/bk/ft **全指 side**，up/dn 独立 | **有**（四面 scale 1 2） | **有**（_hdrside / _hdrup / _hdrdn） |
| sunshine | sky_tf2_04 | rt/lf/bk/ft **全指 side**，up/dn 独立 | **有** | **有** |
| upward | sky_upward | rt/lf/bk/ft **全指 side**，up/dn 独立 | **有**（rt/lf/bk/ft/dn **五个面**都是 scale 1 2；up 面那句被 `//` 注释掉） | **有** |
| process / product | sky_trainyard_01 | 六面各自独立 | **有**（六面都有） | **无** |
| bagel | sky_goldrush_01 | 六面各自独立 | **有**（六面都有） | **无** |
| sultry | sky_well_02_hdr | **VPK 里没有**，只在 BSP pakfile 里；且六面互相引用 sky_well_01*（bk→01rt、rt→01ft、ft→01lf、lf→01bk、up/dn→01up/dn，侧四面做了一次旋转） | **有**（六面都有） | **有** |

upward sky_upwardrt.vmt 原文（vpk_query --dump 逐字，235 B）：

    "sky"
    {
        "$hdrcompressedTexture"  "skybox/sky_upward_hdrside"
        "$nofog"                 "1"
        "$ignorez"               "1"
        "$basetexture"           "skybox/sky_upwardside"
        "$basetexturetransform"  "center 0 0 scale 1 2 rotate 0 translate 0 0"
        "%keywords"              "tf"
    }

配套 VTF 实测（存在性）：sky_upwardside.vtf 524,457 B、sky_upward_hdrside.vtf 699,220 B、sky_upwardup.vtf 1,048,807 B、sky_upwarddn.vtf **1,255 B**（几乎全黑，符合 dn 面）。注意 **materials/skybox/sky_upwardside.vmt 不存在** —— side 只是 VTF 名，不是 VMT。

**upward 六面 VMT 逐面实测**（`native/build-nmake/vpk_query.exe --dump`，lead 亲跑；本节直接引用，未重跑）：

| 面 | VMT 体积 | `$basetexturetransform` | `$hdrcompressedTexture` | `$nofog` / `$ignorez` |
|---|---|---|---|---|
| rt | 235 B | `center 0 0 scale 1 2 rotate 0 translate 0 0` | 有 | 1 / 1 |
| lf | 235 B | 同上 | 有 | 1 / 1 |
| bk | 235 B | 同上 | 有 | 1 / 1 |
| ft | 235 B | 同上 | 有 | 1 / 1 |
| up | 233 B | **整行被 `//` 注释掉** | 有 | 1 / 1 |
| dn | 231 B | `center 0 0 scale 1 2 rotate 0 translate 0 0` | 有 | 1 / 1 |

六个文件全部在 `tf2_misc_dir.vpk`。

**(f) D51 的行级证据（照 lead 口径）。**

native/src/world_material.cpp:480-520 loadSkyFace 全流程：

    skyMaterialPath -> probe.reachable -> probe.read(vmt) -> VmtParser::parse
      -> :498 if (material.baseTexture.empty()) -> "vmt declares no $basetexture: <path>"
      -> :502 VmtParser::resourcePath(material.baseTexture, ".vtf")
      -> :509 VtfTexture::parse(...) || texture.header().isCubemap() -> "is a cubemap, which a sky face is not"
      -> :514 decodeRgba

**全程只消费 material.baseTexture，不消费：**

- $basetexturetransform —— **它已经被解析了**（native/include/vmt_material.h:107-110：:107-109 注释、:109 `bool baseTextureTransform`、:110 `std::string baseTextureTransformText`；解析函数 parseTextureTransform 在 native/src/vmt_material.cpp:184-192，口径注释 :353-365，赋值 :372），只是 loadSkyFace **不消费**。
- $hdrcompressedTexture —— **全仓 hdrcompressed 真 0 命中**（native/src 与 native/include 分别复验，均 complete=true matched=0）。
- $ignorez / $nofog —— 六面 VMT 全部声明，**也都不被读**（D51 可扩充项；本卡不主张这是视觉缺陷，只登记为字段未被消费）。

**受影响的图与面（客观陈述，不含观感判断）：**

| 图 | 受 $basetexturetransform 未消费影响的面 | 受 $hdrcompressedTexture 未消费影响的面 |
|---|---|---|
| gullywash | rt / lf / bk / ft（四面共用一张 side 图，声明 scale 1 2） | 六面全受影响 |
| sunshine | rt / lf / bk / ft | 六面全受影响 |
| upward | rt / lf / bk / ft / dn（声明了 scale 1 2 的五个面；up 面那句被注释掉） | 六面全受影响 |
| process / product | 六面（各自独立图，仍声明 transform） | 无（VMT 无该字段） |
| bagel | 六面 | 无 |
| sultry | 六面 | 六面全受影响 |

**五个面在产品里按 1:1 铺满，原设计是纵向 2 倍。** loadSkyFace 产出的立方体顶点 uv 是面内 (0..1) 四角（native/src/world_material.cpp:733-745），纹理按 1:1 覆盖整面；VMT 声明的 `center 0 0 scale 1 2 rotate 0 translate 0 0` 一次也没落到 uv 上 —— 字段在 native/include/vmt_material.h:107-110（:109 为 `bool baseTextureTransform`、:110 为 `std::string baseTextureTransformText`），赋值在 native/src/vmt_material.cpp:372，**产品别处有消费**（world pass：native/src/main.cpp:5270 / :5282 打出该文本），**只有 loadSkyFace 不消费**。

**这条是 D51 的可视后果，单列：它不是 N10 任一判据栏的 PASS 反证，也不是 N10 的缺口。**

**有没有同机位对照能判定这个差异？** —— **没有。** 本轮 4.3 的两帧，变量是 `draws` 由 0 变 1（关天空组六面 VTF 照样解码、rgb 逐值相同，见 4.3b），不是「套用 / 不套用 transform」；要判定缺了这次 scale 1 2 之后画面具体错成什么样，必须另抓一对「同 exe、同机位，只改 loadSkyFace 是否套用 transform」的帧 —— 那属改动，本卡没做。**如实记：静态侧确认未消费，像素侧未测。**

### 4.2 3D skybox：产品完全没有实现（N10 的核心判定）

**(a) 双关键词复验，两个目录分别跑，均 complete=true matched=0：**

关键词组 sky_camera / skyCamera / skyboxScale / sky_origin / skyOrigin / 3dskybox / 3DSkybox（caseInsensitive）：

- native/src → **0**
- native/include → **0**

→ **D02「3D skybox（sky_camera 解析 / 缩放 / 合成 / 第二遍绘制）未实现」成立。**

**(b) 后果不是「少了几个远景小房子」，而是两件更硬的事。**

**后果一：3D skybox 的 detail 面参与世界归一化，把整张地图的尺度改掉了。**

native/src/native_renderer_world.cpp:82-94 只对 map.triangles 求 min/max：

    const float spanX = std::max(maxX - minX, 1.0f), spanY = ..., spanZ = std::max(maxZ - minZ, 1.0f);
    const float horizontalSpan = std::max(spanX, spanY), horizontalScale = 1.8f / horizontalSpan;
    const float centerX = (minX + maxX) * 0.5f, centerY = (minY + maxY) * 0.5f;

convert（:100-109，U-A1 注释 the box is anisotropic）：(x-centerX)*horizontalScale, (y-centerY)*horizontalScale, (z-minZ)/spanZ*0.8f+0.1f。

map.triangles **包含 3D skybox 的 detail 面**（它们不是 sky 面，走普通世界面分支），所以：

| 图 | span_all | span_playableXY | 比值 | horizontalScale_all | horizontalScale_playable | 水平缩水 | 主因 |
|---|---|---|---|---|---|---|---|
| pl_upward_f12 | 12288 | 7168 | 1.71× | 0.000146 | 0.000251 | **-71.4%** | **3D skybox detail** |
| cp_gullywash_f9 | 17002 | 10404 | 1.63× | 0.000106 | 0.000173 | **-63.4%** | **3D skybox detail** |
| cp_process_f12 | 19948 | 10112 | 1.97× | 0.000090 | 0.000178 | -97.3% | **TOOLSBLACK 虚空** |
| cp_sunshine | 15292 | 10752 | 1.42× | 0.000118 | 0.000167 | -42.2% | **TOOLSBLACK 虚空** |
| koth_bagel_rc11 | 8512 | 8512 | 1.00× | — | — | 0% | 无 |
| koth_product_final | 9408 | 9408 | 1.00× | — | — | 0% | 无 |
| cp_sultry_b8a | 12000 | 12000 | 1.00× | — | — | 0% | 无 |

Z 跨度同样被污染：

| 图 | spanZ_all | spanZ_playableXY | 比值 | z 低于 -2000 三角数 | zmin |
|---|---|---|---|---|---|
| cp_process_f12 | 16401 | 1552 | **10.57×** | 628 | -14673 |
| cp_sunshine | 16049 | 2392 | **6.71×** | 618 | -14673 |
| cp_gullywash_f9 | 4971 | 1840 | 2.70× | 0 | — |
| cp_sultry_b8a | 4088 | 1894 | 2.16× | 0 | — |
| bagel / product / upward | 1.00× | | | | |

**注意：process 与 sunshine 的 -14892 / -14672.8 极端值不是天空盒，是 TOOLS/TOOLSBLACK 虚空刷（568 / 558 个三角）加 OVERLAYS/DUST_GRADIENT01/02 尘埃面。** 我一度怀疑它们是退化 / 哨兵顶点，已用材质归因推翻该怀疑 —— 但「它们仍然污染归一化」这个结论不变。

所以准确说法是：**产品把「不属于可玩区域、本应由 3D skybox pass 以 1/16 缩放处理的几何」与「虚空刷」一起算进了世界 bbox，导致水平尺度被压到应有值的 28%–37%（upward / gullywash）。**

**后果二：这些面在屏幕上基本看不见 —— 不是因为它们被正确处理，而是被 PVS 剔了。**

pvs_sky3.py：观察点 = 所有 info_player_teamspawn + info_player_start 的 origin 过 BSP 树求出的 cluster；被测面 = 质心距 sky_camera ≤2048 的非天空、非 nodraw 面；判定 = clusterVisible 任一簇可见即保留（与产品 clipWorldTrianglesByVis 同规则）：

| 图 | near-sky_camera 面 | PVS 保留 | PVS 剔除 | 无 cluster | spawn clusters |
|---|---|---|---|---|---|
| pl_upward_f12 | 679 | **0** | 637 | 42 | 9 |
| cp_sunshine | 385 | **0** | 385 | 0 | 9 |
| cp_gullywash_f9 | 282 | **0** | 76 | 206 | 8 |
| koth_bagel_rc11 | 29 | 2 | 15 | 12 | 3 |
| cp_sultry_b8a | 8 | 0 | 4 | 4 | 10 |
| cp_process_f12 | 0 | — | — | — | 16 |
| koth_product_final | 0 | — | — | — | 3 |

→ **从任一出生点看，3D skybox detail 面 100% 被判不可见。** 这解释了「为什么屏幕上看不到远处多出的小房子」，但**不能**解释成「正确处理了」：它们仍然活在 map.triangles 里，仍然参与 bbox 求解，只是被 PVS 挡住了绘制。

**（附：dleaf 字段偏移曾被我误判，此处更正）** 我一度判定 native/src/bsp_map.cpp:322-329 的 leaf 偏移读错（称 firstFace 应为 22、faceCount 应为 24）。用**精确划分检验**（每个 leaffaces 槽被所有 leaf 的范围恰好覆盖 1 次）在四张图上推翻了自己：

| 图 | 20/22 | 22/24 | 24/26 |
|---|---|---|---|
| cp_gullywash_f9 | **oob=0 gaps=0 dups=0（精确）** | gaps=6697 dups=17230 | gaps=6698 dups=0 |
| pl_upward_f12 | **精确** | gaps=7565 dups=15539 | gaps=7566 dups=0 |
| cp_sunshine | **精确** | gaps=8953 dups=16488 | gaps=8954 dups=0 |
| cp_sultry_b8a | **精确** | oob=11758 | — |

→ **产品的 20/22 是对的。** 方法学教训：单看「sum 相等 / oob=0」不足以定偏移，必须做覆盖划分检验。本节的 PVS 结论用的是修正后的偏移。

### 4.3 N10 抓帧：产品像素层证据（本轮完成）

上一版报告在 4.3 处记的是「三件套一件都没做、一帧都没写出」（第六节 6.1）。本轮拿到 hostlock 的 150 秒窗口，按 6.1 修正后的跑法重跑，**两帧全部写出**。

**(a) 抓帧身份（两条命令除天空开关外逐字相同）**

| 项 | sky_on | sky_off |
|---|---|---|
| 文件 | D:/_shared/EXEC-2026-11-15/n10-capture/sky_on/sky_on.bmp | D:/_shared/EXEC-2026-11-15/n10-capture/sky_off/sky_off.bmp |
| 字节 | 2,582,406 | 2,582,406 |
| sha256 | 450806898cfdacacfaeba2f725d2a7526b4d6e78c3101b949ae51dcb53809753 | dacf9488d364e51460825f0e8e9125f7d796132e9a79d80e12b72e789e6d1d9c |
| 尺寸 | 1264x681（24bpp） | 1264x681（24bpp） |
| 耗时 | 52,891 ms | 52,041 ms |
| 退出码 | 0（capture_written=1） | 0（capture_written=1） |
| trace 字节 | 2,950,743 | 3,988,858 |

命令（`<TF>` 与 demo 身份见第一节；两条只差 `--world-sky` / `--no-world-sky`）：

    tf2_demo_native.exe --tf-root <TF> --hidden-window --audio-disabled \
      --settings-file <copy> --ui-settings-file <copy> \
      --demo <a9630c65…pl_upward_f12 STV> --capture-tick 40000 --playback-speed 64 \
      --world-sky --hud-off --window-size 1280x720 \
      --capture-frame D:/_shared/EXEC-2026-11-15/n10-capture/sky_on/sky_on.bmp

两条都报 `capture: armed requested=40000 frame_tick=40000 frames=254`（on）/ `frames=346`（off），**精确命中 tick 40000**。

两条要点，都会影响复核：

1. **帧尺寸是 1264x681，不是请求的 1280x720。** `--hidden-window` 下窗口有边框/DPI 扣减。两帧尺寸相同，故逐像素可比；但任何按 1280x720 预算坐标的复核都会错位。
2. **实测总耗时 107 秒**（19:43:53 ACQUIRED → 19:45:40 done），在 150 秒预算内。跑完 hostlock 自动 FREE，product_processes=0，`ps -W` 里无残留进程。

**(b) 关闭对照是有效的：同材质、同解码，只差绘制**

sky_on 的 trace（六面逐条）：

    gpu: world sky-upload state=active vertices=36 triangles=12 faces=6 side=512 resampled=1 reason=six sides, 36 vertices
    index=0 +X rt vtf=materials/skybox/sky_upwardside.vtf rgba=107,138,162,255
    index=1 -X lf 同 vtf，同 rgba
    index=2 +Y bk 同 vtf，同 rgba
    index=3 -Y ft 同 vtf，同 rgba
    index=4 +Z up vtf=materials/skybox/sky_upwardup.vtf rgba=92,127,154,255
    index=5 -Z dn vtf=materials/skybox/sky_upwarddn.vtf rgba=173,194,203,255
    gpu: world sky state=active … draws=1

sky_off 的 trace：

    gpu: world sky state=unavailable vertices=0 triangles=0 faces=0 reason=skybox draw disabled by the caller
    gpu: world sky state=unavailable … draws=0

关键点：**关闭组里六面 VTF 照样被解析出来**（同样是 107,138,162 / 92,127,154 / 173,194,203），只是 draws=0。所以两次跑动的差别被限定在「画不画」这一件事上，不是「有没有读到资产」的差别 —— 这是本轮最有价值的控制条件。

**(c) 判据一：天空只填洞、一个像素都不盖前景 → 通过**

脚本 verify.py：以 sky_off 的最常见色（14,18,22，即清屏色）为 clear-colour 逐像素判定。

| 量 | 值 |
|---|---|
| 全图像素 | 860,784（1264x681） |
| sky_off 的 clear-colour 像素 | 29,054（3.38%） |
| 两帧有差异的像素 | 29,054（3.375%） |
| 差异像素里 sky_off 是 CLEAR 的（= 天空填了一个洞） | **29,054（100.00%）** |
| 差异像素里 sky_off 是 GEOMETRY 的（= 天空盖住了前景） | **0（0.00%）** |
| clear-colour 像素中被天空漏掉的 | **0（0.00%）** |

→ **天空精确填满了每一个没有被几何覆盖的像素；既没有越界盖到前景，也没有漏掉任何一个洞。** 卡面那句「不会盖住前景」在产品侧有了正面读数。

空间分布（同一次实测）：差异像素 bbox = x[548,1263] y[0,426]，有差异的行 325/681；按行带统计天空覆盖率 rows 0–84 = 13.2%、85–169 = 9.8%、170–254 = 0.7%、255–339 = 0.5%、340–424 = 2.9%、**425 以下 = 0.0%**。天空只出现在画面上半部，下半屏一个像素都没有 —— 与相机 `mode=first-person eye=-425.2,-816.3,622.2 pitch=6.00 yaw=12.32 src=follow`（略仰）一致。

**(d) 判据二：六面方向与颜色 → 与 VTF 解码值逐值吻合**

faces.py（容差 10）：

| 面 | trace 基准色 | 成片主色 | 像素数 | 不同色值数 | 范围 | std |
|---|---|---|---|---|---|---|
| up (+Z) | 92,127,154 | (92,127,154) | 11,694（40.2%） | 39 | [92,126,153]–[102,136,161] | 2.96 / 2.49 / 1.78 |
| side（rt/lf/bk/ft） | 107,138,162 | (107,138,162) | 13,032（44.9%） | 127 | [98,131,157]–[117,148,170] | 4.97 / 4.17 / 3.03 |
| dn (-Z) | 173,194,203 | (173,194,203) | 210（0.7%） | 10 | [163,186,197]–[173,194,203] | — |

三条事实：

1. **颜色与 VTF 解码值逐值吻合** —— 产品画的就是这六张 VTF，没有串面、没有把 up 当 side。
2. **存在 std 2–5 的渐变**（up 39 个色值、side 127 个色值），说明采样的是真实纹理，不是纯色填充。
3. **dn 面明显偏亮（173,194,203，近白），且只在 y≈390–440 出现 210 个像素** —— 符合 upward 是贴地地图、向下看时露出 dn 面的几何。

**(e) 目视复核（三张放大裁剪）**

| 图 | 内容 | 判读 |
|---|---|---|
| crop_top.png（780x390，3x） | 顶部蓝天 + 右侧谷仓墙面 | 天空与墙面边界锐利，**无渗色、无 z-fighting 条纹** |
| crop_mid.png（640x560，2x） | 地形缺口 | 天空边界**沿几何轮廓走**，只出现在缺口里 |
| crop_dn.png（960x240，4x） | 地形边缘下方 | 一条近白亮带 = dn 面，与 (d) 的像素统计一致 |

**(f) 这一节仍然没有证明什么**

- **没有与 TF2 官方画面对照**：「像不像 TF2」本报告不表态。这两帧是**产品自比**（开天空 / 关天空）。
- **缺面 / Fallback 状态机未被触发**：七图六面 VMT 全部可解析（4.1e），要测 Fallback 得人工删一面，属改动，本轮未做。
- **接缝没有量化**：三张裁剪图上没看到接缝，但那是目视，不是逐像素接缝检测；六面交界处的采样连续性没有测。
- **「不应受影响区域」没有单独指定**：只做了同机位开/关两帧。副产品是 97% 的像素确实一模一样（860,784 − 29,054），但这不等于事先指定了一块对照区。

---
### 4.4 静态解码与两帧对照合起来仍不能证明什么（N10 的剩余缺口）

| 缺口 | 现状 | 判法 |
|---|---|---|
| **不盖住前景** | 代码层证据齐全（天空最先画、DEPTH_WRITE_MASK_ZERO、CULL_NONE、去平移的 skyView、halfExtent 40 小于 far 100），**且 4.3(c) 给了像素读数：29,054 个差异像素（占全帧 3.375%）100% 落在 sky_off 的 clear-colour 上，盖住前景的 0 个、漏掉的 0 个。** | **PASS（本轮）** |
| **方向正确（六面朝向）** | 4.3(d) 把成片主色与 trace 的六面基准色、静态 VTF 解码值逐值对上（side 107,138,162 / up 92,127,154 / dn 173,194,203），且 dn 只在贴地视角的窄带出现。**面序错误会立刻表现为上下颠倒或左右互换，实测没有。** | **PASS（本轮，upward 一图一机位）** |
| **接缝 / 缺面** | 三张放大裁剪上没看到接缝（4.3(e)），但那是目视；六面交界处的采样连续性没有逐像素量化。Fallback 状态机在 world_material.cpp:752 —— 七图六面 **VMT 全部可解析**（4.1e），「缺面」在真实地图上不可自然触发，要测必须人工删面（属改动，未做）。 | **NOT VERIFIED**（接缝）；**Fallback 未触发**（不记 PASS） |
| **sky_camera 正例 + 关闭对照 + 不应受影响区域** | 关闭对照**已有**（4.3b：同材质同解码，只差 draws）。但这一格问的是 **3D skybox（sky_camera）**，而该功能不存在 —— 没有正例可抓。 | **UNSUPPORTED**（功能不存在，不是没测） |
| **不纳入光影匹配** | bsp_map.cpp:811-830 的 sky 三角只赋 au/av/bu/bv/cu/cv 与 material/planeSide/planeIndex/cluster/faceIndex，**从不赋 alu/alv**（BspTriangle 默认 -1，见 native/include/bsp_map.h）；native_renderer_world.cpp:150-151 的 lightmappedVertices 只数 a.lu >= 0。→ **代码层成立**。但它证明的是「天空不进 lightmap 统计」，不是「屏幕上光影正确」；本轮两帧也没有测光影匹配。 | **PASS（代码层）** / 像素未测 |
| **3D skybox 位置正确** | 无从谈起 —— 功能不存在（4.2a）。 | **未实现**，不记 PASS |
| **与 TF2 官方画面一致** | 两帧是产品自比，没有参考帧对位。 | **NOT VERIFIED**（本报告不表态） |

### 4.5 N10 结论

1. **普通六面天空** → 代码层完整（4.1d），**且本轮拿到了 upward 一图一机位的开/关两帧：遮罩（不盖前景）与六面方向两项都有像素读数并通过**（4.3c / 4.3d）。剩余未闭合的是**接缝量化**与**Fallback 状态机**，两者在真实地图上都不自然可触发。**卡级记「部分完成」，法定判法见第九节裁决 3。**
2. **3D skybox** → **UNSUPPORTED（未实现）**。sky_camera 三关键词在 native/src 与 native/include 均 0 命中；七图的 sky_camera origin + scale=16 **只是输入**，产品一处也没读。附带两条可量化的连带损伤：归一化尺度被 3D skybox detail / 虚空刷污染（upward -71.4%、gullywash -63.4%、process -97.3%、sunshine -42.2%），以及这些面靠 PVS 才没出现在屏幕上。

**结论的可复用边界**：4.3 的两帧只覆盖 `pl_upward_f12` / tick 40000 / 一个相机位姿。七图其余六张、其余时刻、以及任何 map 的 sky_camera 行为都没有像素证据。引用时请写全「upward、t40000、first-person pitch=6.00 yaw=12.32」。

**与判据无关的单列项**：4.1(f) 的 $basetexturetransform 未消费是 D51 的可视后果，单独登记 —— 它既不构成 N10 任一判据栏的 PASS 反证，也不计入本卡的缺口清单。

**卡级（lead 定判）：部分完成** —— 可判项 PASS（不盖住前景、六面方向；限 upward / t40000 / 一图一机位）/ 接缝与 Fallback NOT VERIFIED / sky_camera 正例 UNSUPPORTED（功能不存在）/ 3D skybox 位置 未实现。**不写「N10 PASS」，也不把四栏合成一个卡级结论。**

**剩余缺口去哪**：「其余六图 + 接缝量化 + Fallback 触发」**不新开卡**，并入 **N11（七图最终矩阵）**，由 lead 在 N11 里当缺口列。本卡不再挂悬空后续卡。

---

## 五、三张卡共用的负向证据汇总

| 编号 | 事实 | 证据位置 |
|---|---|---|
| N-1 | 产品没有 entity I/O 分发器 | native/src 与 native/include 双关键词均 0 命中 |
| N-2 | 产品不认 sky_camera / skyboxScale / skyOrigin | 同上，双关键词均 0 命中 |
| N-3 | **天空面加载器（loadSkyFace）**不消费 $basetexturetransform（已解析；world pass 消费，见 native/src/main.cpp:5270 / :5282）、全仓不识别 $hdrcompressedTexture | native/src/world_material.cpp:480-520；native/include/vmt_material.h:107-110；native/src/vmt_material.cpp:372；hdrcompressed 全仓 0 命中 |
| N-4 | 产品不解析 BSP 实体的 parentname / skin / body / spawnflags / m_nModelIndex | native/src/bsp_map.cpp:466-495 只取 classname/model/origin/fog* |
| N-5 | 非白名单 brush 类**不产生任何三角形**（不是画在原点） | native/src/bsp_map.cpp:585-588 / 606 / 628-647 / 649-654 |
| N-6 | BspRefusedBrushModel::moving 语义是「类名非空」，不是「会动」 | native/src/bsp_map.cpp:640 |
| N-7 | 小地图 CP marker 的 team 是编译期常量 | native/include/minimap.h:200-206 / 219 / 273；native/src/main_hud.cpp:1014-1022 |
| N-8 | func_tracktrain 的 *110 在 pl_upward_f12 里是 175 个 dmodel 中唯一 0 面的 | dmodel 表，faces=0 |
| N-9 | demo 实体历史可用范围由**扫描期留存策略**决定（flush 只留 ≤8 检查点 → 抽稀成 96 个稀疏快照，events/packets 已丢弃），不是产品在指定 tick 的实体可用性 | native/src/main.cpp:3489-3492；native/include/demo_header.h:318-323；native/src/demo_header_entity.cpp:183-199；demo_open_probe --scan 实测 |
| N-10 | 七图的 3D skybox detail 面从出生点看 100% 被 PVS 剔除 | pvs_sky3.py |
| N-11 | func_plat / func_breakable_surf / func_water_analog / func_trackautochange 在七图出现次数**均为 0** | census.py |

---

## 六、本轮失败的尝试与它们留下的东西

### 6.1 产品抓帧：第一次一帧都没写出（根因是系统性缺陷；第二次已成功，见 4.3）

- 时间：19:15:38 acquire（owner n08-n10-maps）→ 19:22:10 到点 kill 并 release（RELEASED，随后 FREE，product_processes=0，无残留）。
- 命令：tf2_demo_native.exe --tf-root <TF> --demo <pl_upward_f12 STV> --capture-frame cap/sky_on.bmp --capture-tick 40000 --world-sky --hud-off --audio-disabled，随后同参数 --no-world-sky。
- 结果：cap/sky_on.bmp 与 sky_off.bmp **一个字节都没有**；日志只有 ACQUIRED 一行。
- **根因**：我**没有传 --playback-speed**。产品按 demo 实时 tick 率播放（native/src/main.cpp:2458 解析，:5977-5981 g_playback.speed = std::clamp(playbackSpeed, 0.125, 64.0)），upward STV 到 --capture-tick 40000 需要约 10 分钟，而窗口只有 6 分半 —— **物理上装不下**。
- lead 判定：kill + release 正确（「不许 kill」的语境是别杀别人的）；但 --playback-speed 根因已升格为**系统性缺陷** —— animation-drive-states-check.sh 里 playback-speed 命中 **0 次**，9 次运行共 143,450 ticks ÷ 66.65 = **36.3 分钟**，贴着单次持锁 40 分钟上限；attach-weapon-budget-check.sh 安全（SPEED=32 在 :42、TICK=30001 约 14 秒）。**门禁不钉播放速度则耗时不可预算、随 demo 长度线性膨胀、超时不报错只失败。**
- **下次的准确跑法（lead 给定，逐字）**：两条命令除 --world-sky / --no-world-sky 外**完全相同**：

      tf2_demo_native.exe --tf-root <TF> --demo <demo> --playback-speed 64 --capture-tick 40000 \
        --hud-off --audio-disabled --hidden-window --world-sky --capture-frame cap/sky_on.bmp
      tf2_demo_native.exe --tf-root <TF> --demo <demo> --playback-speed 64 --capture-tick 40000 \
        --hud-off --audio-disabled --hidden-window --no-world-sky --capture-frame cap/sky_off.bmp

  预算 **150 秒**；出不来第一张就立刻 release 并报「窗口不够」。留 trace 必须设环境变量 TF2_NATIVE_TRACE=<path>（native/src/main_trace.cpp:79-102，不设则整个 trace 通道静默关闭）。

- **第二次（本轮，按上面跑法）成功**：19:43:53 ACQUIRED → 19:45:40 done，总耗时 **107 秒**，两帧均写出（身份与读数见 4.3）。同一台机器、同一 demo、同一 exe，**唯一变量就是 `--playback-speed 64`**：52.9 秒跑到 tick 40000，而第一次 6 分半都没跑到。这直接证实了上面的根因判定，不是推测。
- 两条命令的完整形态与逐字参数见 4.3(a)；脚本 D:/_shared/EXEC-2026-11-15/n10-capture/n10-sky.sh 串起「acquire → 跑两条 → release」，跑完自动 FREE 并确认 `ps -W` 无残留。

### 6.2 两次「0 命中」假阴性 / 一次自证伪，方法学教训

1. **tools.ops_grep 传目录会静默返回 status:error / complete:false / matched:0。** 我曾据此写「整仓 basetexturetransform 0 命中」，是**假阴性**；逐子目录跑就有命中（vmt_material.h:107 / vmt_material.cpp:184,353,365）。**规则：凡「0 命中」当证据，必须附实际命令 + 路径范围 + 第二种复验。**
2. **dleaf 偏移的自我推翻**（4.2 末）：oob=0 不足以证明偏移正确，必须做覆盖划分检验。
3. **vpk_query --dump 会把 source=<vpk> bytes=N 头行混进 stdout** —— 直接重定向写 .mdl 会把文件写坏（我第一版写出的两个 mdl 都多了 2 字节头，解析出 numtextures=1096 这种垃圾值）。必须先剥首行再写。
4. **raw grep demo 文件数模型名不可靠**：upward POV 里 bomb_cart 计数 **0**，而 entity_model_probe --dump-precache 报它在 idx=1172。precache 是压缩 / 分片的。**必须用探针的 dump-precache，不能用 raw grep。**

### 6.3 DEMO-INDEX.md 与磁盘不一致（索引缺陷）

E:/demo测试/DEMO-INDEX.md 记 pl_upward_f12 的 POV 为 autorecord_2026-09-10_21-01-05.dem。**该文件不存在。** 磁盘上只有：

- autorecord_2026-08-08_21-43-59.dem（146,853,666 B）
- STV 侧 a9630c6557468b24f0b8c2e7dab38ed8_match-20261030-2145-pl_upward_f12.dem（75,663,754 B）

索引里那个名字报 header=0 error=demo file cannot be opened（rc=1）。**后续任何用 demo 的工作都必须先 ls 实时确认文件名，不要相信索引。**（cp_process_f12 的 POV / STV 文件名与索引一致，已核对。）

---

## 七、按第十二节模板的交付记录

    任务：N08 / N09 / N10（三卡合并交付一份静态证据报告）
    负责人：n08-n10-maps（角色 C）
    旧缺陷映射：D51（天空面加载器不消费已解析的 $basetexturetransform、全仓不识别 $hdrcompressedTexture）；
      D02（3D skybox 未实现，本报告独立复验成立）；
      DEMO-INDEX 索引缺陷（新，未分配号）；
      门禁不钉 --playback-speed（lead 已定级为系统性缺陷，编号待分配）
    分支、开始/结束 HEAD：candidate/e5；开始 a901ece，报告写就时 83657f1，交稿复核时 6dd853e
      （HEAD 在本卡执行期间被别的 agent 推进三次；产品 exe sha256 1c3580e2… 全程未变，故行为读数应绑该 exe 而非 HEAD）
    既有脏文件保留情况：未触碰。工作树原有 M docs/N-STATUS-2026-11-14.md、M docs/PROJECT-NEXT-TASKS-2026-11-14.md、
      M docs/PROJECT-ROADMAP-HANDOFF-2026-11-12.md、M native/src/main.cpp、?? colour、?? docs/*.md 等，全部原样保留。
    改动文件与用户可见变化：仅新增 docs/N08-N10-MAPS-STATIC-EVIDENCE.md。无产品代码改动、无用户可见变化。
      抓帧产物落在 D:/_shared/EXEC-2026-11-15/n10-capture/（工作树之外，不污染 git）。
    复现输入：E:/demo测试/<map>/地图/<map>.bsp 七张（真实首发 BSP）；
      demo：pl_upward_f12 POV/STV、cp_process_f12 POV/STV（sha256 见第一节）。
    原失败结果：第一次抓帧两条命令未产出，cap/sky_on.bmp 与 cap/sky_off.bmp 均不存在。
      根因 --playback-speed 缺省 = 实时 1x，窗口物理上装不下。
      第二次按修正跑法（--playback-speed 64）两条均成功，见 4.3 与 6.1。
    有效控制：静态侧负控为：1) 关键词复验（每组两个关键词、两个目录）；
      2) dleaf 偏移四种候选布局的划分检验互斥；3) PVS 用「任一 spawn 可见」正面规则而非「都不可见」。
      抓帧侧的控制组就是 --no-world-sky 那一帧：同 exe、同 demo、同 tick、同机位、
      六面 VTF 同样被解析（trace 六条 rgba 一致），唯一差别是 draws 由 1 变 0。
    最小命令、rc、断言/变异数量：
      python bsp_static_dump.py <bsp>                          rc=0（七图各一次）
      python census.py / props.py / pak_sky.py / zspan.py      rc=0
      python norm_impact.py / sky_detail.py / sky_mats.py      rc=0
      python sky_cluster.py / pvs_sky3.py                      rc=0
      python leaf_settle.py                                    rc=0（四图 x 四候选布局）
      python cp_props7.py / sky_matrix.py / sky_matrix2.py     rc=0
      python sky_raw.py / mdl_clean.py / mdl_parse3.py         rc=0
      world_geometry_probe.exe --tf-root <TF> --map <abs bsp>  rc=0（七图；rc 不可信，须看 stdout 的 {"reading":）
      world_material_probe.exe（无参自测）                     rc=0
      minimap_probe.exe --minimap-test                         rc=0
      entity_model_probe.exe --demo <upward POV> --dump-precache "*"  rc=0（entries=1197）
      brush_entity_probe.exe --demo <upward POV>               rc=0（verdict=no-brush-entities）
      demo_open_probe.exe --scan --snapshot 2000 <demo>        rc=0
      n10-sky.sh（hostlock run 包裹，两条命令 + 自动 release）    rc=0（107 秒，两帧均写出）
        第一条 --world-sky    sky_on.bmp  2,582,406 B  52,891 ms  capture frame_tick=40000
        第二条 --no-world-sky sky_off.bmp 2,582,406 B  52,041 ms  capture frame_tick=40000
      python verify.py / faces.py / analyze.py / crop.py         rc=0（4.3 的像素判据）
      变异数量：本报告不涉及门禁变异。
    exe/sourceContent/sidecar 与 BSP/demo/设置/相机身份：
      产品 exe sha256 1c3580e2…（2,991,616 B, mtime Nov 14 19:22）；七个探针 exe 的 sha256 见第一节；
      证据脚本 sha256 见第八节；BSP 与 demo sha256 见第一、八节。
      抓帧用了 --settings-file / --ui-settings-file 的**副本**（避免污染真实配置），副本路径与内容哈希见 4.3(a) 同目录；
      相机身份由 trace 记录：scene: camera mode=first-person target=1 followed_steamid=255/256
      eye=-425.2,-816.3,622.2 pitch=6.00 yaw=12.32 src=follow（两帧同机位）。
    证据路径、SHA-256、实际执行时间：D:/_shared/EXEC-2026-11-16/n08-n10-maps/；
      静态解码执行窗口 2026-11-14 18:27-19:32（Asia/Shanghai）；
      产品抓帧第一次尝试 19:15:38-19:22:10（失败）；第二次 19:43:53-19:45:40（成功，107 秒）。
      抓帧产物目录 D:/_shared/EXEC-2026-11-15/n10-capture/。
    结论（lead 已定判，见第九节）：
      N08 = 部分：模型侧有静态证据（N08-a 静态侧 PASS），状态侧未观测（N08-b 未实现 / NOT VERIFIED）；
      N09 = 实体链/资产/闸门/普查 静态成立，推车跟时间移动 NOT VERIFIED，平台与可破坏物 不适用（七图计数 0，3.1e）；
      N10 = 部分完成：可判项 PASS（不盖住前景、六面方向；限 upward / t40000 / 一图一机位）、
        接缝与 Fallback NOT VERIFIED、sky_camera 正例 UNSUPPORTED（功能不存在）、3D skybox 位置 未实现；
        剩余缺口并入 N11（七图最终矩阵），不新开卡；4.1f 的 $basetexturetransform 未消费属 D51，单列，不作 N10 反证。
    本条证明什么：
      - 七图 BSP 里控制点/推车/sky_camera/动态 brush 的实体与资产全貌（含精确计数与字段值）。
      - 产品源码在这些链路上消费了什么、没消费什么（逐行行号）。
      - D51 的定量底稿（哪张图哪几面受影响）。
      - 3D skybox detail 几何对世界归一化的污染量级（-42% 至 -97%）。
    本条没有证明什么：
      - 除 4.3 的 upward / t40000 / 一机位开-关两帧之外的任何像素层事实；没有 TF2 参考帧对位。
      - N08 的四种状态样本、状态变更、暂停/seek 后的新鲜度。
      - N09 的四组 tick、推车位移、门开合、倍速/PVS 行为。
      - N10 的 sky_camera 正例（功能不存在）、接缝量化、Fallback 触发、其余六图像素；D51 的 transform 像素后果。
      - D01/D03-D09/D40-D50 任何一条的完成或失效。
    失效与继承的旧证据清单：
      - DEMO-INDEX.md 中 pl_upward_f12 的 POV 文件名条目失效（文件不存在）。
      - 我此前关于「产品 dleaf 偏移有 bug（读 20/22 应为 22/24）」的结论作废，正确为 20/22。
      - 我此前关于「推车两道闸都过不去」的结论部分作废：对 bomb_cart.mdl 不成立（前缀命中），仅对 *110 成立。
      - 我此前关于「process/sunshine 极端坐标可能是退化/哨兵顶点」的怀疑作废：是 TOOLSBLACK 虚空刷 + DUST_GRADIENT。
      - 我此前关于「整仓 basetexturetransform 0 命中」的结论作废：已解析，loadSkyFace 不消费（D51 口径）。
    剩余项、下一责任人、hostlock/进程/源码还原状态：
      - 已闭合：N10 两帧对照（sky_on/sky_off）—— 19:43:53-19:45:40，107 秒，两帧均写出，读数见 4.3。
      - 剩余 1：N10 接缝量化 + Fallback 触发（真实地图上不可自然触发，需人工删面，属改动）—— 待卡面裁决。
      - 剩余 2：N10「其余六图」的像素复核 —— 抓帧通路已通，每图约 110 秒。
      - 剩余 3：N08 状态切换 —— 依赖产品补 entity I/O 分发器，非本卡能闭合。
      - 剩余 4：N09 推车位移 —— **阻塞已解开**（抓帧可任意 tick 停 + trace 打 origin），可开卡按四组 tick 抓帧量位移。
      - hostlock：已 release（19:45:40），product_processes=0，`ps -W` 无残留。
      - 源码还原：未改动任何源码，无需还原。

---

## 八、证据文件与 SHA-256

证据目录 D:/_shared/EXEC-2026-11-16/n08-n10-maps/。

### 8.1 脚本（现场 sha256sum 可复算）

| 脚本 | 作用 | sha256 |
|---|---|---|
| bsp_static_dump.py | VBSP 头 + 64 lump 表 + LZMA 解压 + entity 解析 + dmodel 表 | f8a7ce001d27697f89e08ec8a0331554d136a9acc4cd31a98afc0225f79e046b |
| leaf_settle.py | dleaf 四种候选布局的覆盖划分检验 | 6d474822d9046bc110698f3beb4729336ddf624438509b2bea31f60ad5e6d7e2 |
| pvs_sky3.py | PVS：spawn cluster × sky_camera 邻域面 | 11e31e39cdfae342a8d0a2a9515b8bd920f9c1eaa1ebe962b7645a4df4b18c0d |
| sky_cluster.py | model0 bbox / sky_camera 邻域 bbox | f5463998ecd58b263dc72875a7c93b5d88841c5026abcacf6b67a32a0ce3fd73 |
| sky_mats.py | sky_camera 邻域面按材质计数 | f3d40d6a9007de0d5836186bd3cea7a80b067e5cf754c3fbb327b688c8832792 |
| sky_detail.py | 可玩区外非天空面计数与 z 范围 | 681c39e1141e435f2245320dc32ff2bddbc38340b234b5592a96e68da0c71e51 |
| zspan.py | Z 跨度污染归因 | 25a007c8fc6ed83eec33d7bbb301564eea5e5cf65d49c6a7f731bdece5795976 |
| norm_impact.py | 含/不含 skybox 的 span 与 horizontalScale 对比 | 55dec999912c27a7ad4881f67c7218b19399a6ec5854baf91e50e634682d1832 |
| census.py | 七图动态 brush 普查 | 701c4c70f5b761d18448aec451da76610541d0143e6abb9b1831f43dc9acc025 |
| props.py | 静态道具列表 | 7164a0fcba976fe1e240c9476c0ff0fed6a04d4efa1f4f800d0c68f576234865 |
| pak_sky.py | 逐图扫 BSP pakfile 的 skybox 条目 | 9f1f3be1df07a442bc6c7588179ea4e64931400e9bb5cc5889e823e8650f22de |
| cp_props7.py | 七图 CP 实体与 CP prop 全量导出 | 94a7d70a1abb923cc5c50a1b08658fdaa0d961169bcf9b16f3a826704b59bd2c |
| sky_matrix.py | 六面 VMT 首版（kv 大小写 bug 版，保留以证过程） | ab0f3460b49b30a85fce4f383430cdab11619fa498da9bb31f2e45fde8466a44 |
| sky_matrix2.py | 六面 VMT 定量矩阵（修正版，**报告引用的是这一份**） | f1d96be1f8328b77887a91ae658eaee2fdde9f0348cdd40c6c7c1e3e5f299882 |
| sky_raw.py | 六面 VMT 原文导出 | c7fca516fe24dc6011daf66333cef240c6880cecfc8b969faee96b6314f59fce |
| mdl_clean.py | MDL v45 头/皮肤/bodypart 解析（剥头行后） | b6948dab74595a27be33b6f736b3881cc1eb178481cb1bbb62973672bf99edde |
| mdl_parse3.py | MDL 解析首版（偏移验证） | 546c60eb23fb38853eedbd50aafb8856fa9c0d4b37216871af2f902d8499b4b9 |
| outside_mats.py | 可玩区外非天空面按材质归因（4.1c 的材质列） | b42745639a7e74785050cde08ac6af9220ea9737717c4d10cedf28e98d272910 |
| n10-sky.sh | 抓帧驱动：acquire → 两条命令 → 自动 release；内置 identity.txt 记录 | e77ba24d46a9839bea62890aea44c32bfcc83bf962c9d91bd1f9be5407f3e15d |
| diff.py | 初版逐像素差 + BMP→PNG | ab2d024c5d53173a3b36e2bbe7c3afa93eb756b628698549d9920d596f5aaa8a |
| analyze.py | 差异 bbox / 逐行剖面 / 颜色 top-N | 02a14ffb9c663bced25f0c7eea8cca7fa91457e083ae3ac22e63ec6d6e265fb3 |
| verify.py | **核心判据**：以 sky_off 清屏色为基准判定「填洞 vs 盖前景」 | 6c6b9d91005704280391b6dd270e7a148de0f62f313a0260b61424cabdcbc8ca |
| faces.py | 六面颜色族统计（容差 10） | 681cf622a3ff02ab4d89570886668f3bceebde2837f9f23cfa89fa8ebd591fcd |
| crop.py | 三块放大裁剪 | 29e91e3acd5630eca91b7aa233ea8fc3290a56a23db21bc616eb810493f3d525 |

### 8.2 数据产物

| 文件 | 内容 |
|---|---|
| dump7.json（253,688 B） | 七图完整静态转储：lump 表、entityCount、classCounts、worldspawn、sky_camera、env_sun、env_fog_controller |
| sky_matrix2.txt | 六面 VMT 定量矩阵（报告 4.1e 的来源） |
| sky_raw.txt | 六面 VMT 原文 |
| sky_matrix.txt | 首版（含 kv 大小写 bug 的空值输出，保留以证失败路径） |
| cap_point_base_clean.mdl（1,906 B） | 剥头行后的真 MDL（dataLength=1904） |
| cappoint_hologram_clean.mdl（4,176 B） | 剥头行后的真 MDL（dataLength=4164） |
| cap_point_base.mdl / cappoint_hologram.mdl | 首版（**含 2 字节头行，解析出垃圾值，不要用**） |
| demoopen_scan.txt / demoopen_upward_stv.txt / demoopen_process_pov.txt | demo_open_probe --scan 输出（实体历史窗口） |
| precache_upward.txt（80,452 B） | entity_model_probe --dump-precache "*" 输出（1,197 条） |
| brush_upward.txt / brush_upward_stv.txt | brush_entity_probe 输出（verdict=no-brush-entities） |
| deep_cp_process_f12.json / deep_pl_upward_f12.json | 两张图的实体全量导出 |
| cap/ | 第一次抓帧尝试的输出目录（**空**，见 6.1） |
| cap_sky.log | 第一次抓帧的日志（只有 ACQUIRED 一行） |

抓帧产物（D:/_shared/EXEC-2026-11-15/n10-capture/，工作树之外）：

| 文件 | 字节 | sha256 |
|---|---|---|
| sky_on/sky_on.bmp | 2,582,406 | 450806898cfdacacfaeba2f725d2a7526b4d6e78c3101b949ae51dcb53809753 |
| sky_off/sky_off.bmp | 2,582,406 | dacf9488d364e51460825f0e8e9125f7d796132e9a79d80e12b72e789e6d1d9c |
| sky_on/sky_on.trace | 2,950,743 | （trace 内容随运行变化，不作为判据；判据见 4.3 引用的原文行） |
| sky_off/sky_off.trace | 3,988,858 | 同上 |
| crop_top.png | 46,663 | 28bde6d61b751d6ea992efbd703e74000c1885f212f3aa01ff8ab74d1a5eaf7a |
| crop_mid.png | 153,997 | ef2454260afe4274f8990451e24e634b05914755d66b47ccd64dd60d3480776e |
| crop_dn.png | 24,462 | 12ff7d5b3992a370dbf67d9db166e9ceb0f4cd66b4cf490cea7f69488fd7d177 |
| identity.txt | 616 | f97f0b9624d109474ab07e5700460d0839d8d56d583aac390bfff29cc8add692 |
| sky_on/sky_on.settings.ini | 2,707 | 206d2469ef054f0f809fa8825d74fb14e913a9654c809c6f1f15c8c43b749478 |
| sky_on/sky_on.ui.ini | 482 | d45528bf1e84d099b4a8d26c0a5b8c6cc30ce117a762fe44bdb9bc51f09093ae |

identity.txt 逐字记录（抓帧自证）：head=6dd853e；exe_bytes=2991616；exe_sha256=1c3580e278a160194783ec2c96d3bd622a5fe10ee2b5b70bafbea3295af0797e；exe_mtime=2026-11-14 19:22:35 +0800；demo_bytes=75663754；demo_sha256=7e047aff1f7743915e355eac907a95dc3d981aa0fae5591b6b15346fb77dc4a3；tick=40000 playback_speed=64。**与第一节的 exe / demo 身份逐值一致** —— 抓帧用的就是报告通篇引用的那个 exe。

---

## 九、裁决已下（lead，2026-11-14）

四条原请求（保留原问题与理由），裁决附于每条之下。

1. **N08 的判法**（原请求）：「模型对了、状态没实现」这一半该记 NOT VERIFIED，还是拆成两条（模型 PASS / 状态 未实现）？我倾向**拆**，因为两者证据成熟度差一个数量级。
   **裁决：批准拆两条，但不许把卡级写成 PASS。**
   - N08-a 数据模型（BSP 侧四状态的存在与字段）→ 按证据成熟度判：**PASS（静态侧）**（2.1b / 2.1c / 2.1g）。
   - N08-b 产品状态机（切换 / 暂停 / seek 后的新鲜度）→ **未实现 / NOT VERIFIED**；缺的样本写在 2.3：四种状态各一、一次状态变更前后对照、暂停与 seek 后的新鲜度读数。
   - 卡级一行：**部分：模型侧有静态证据，状态侧未观测**。

2. **func_plat / func_breakable_surf 七图计数为 0**（原请求）：按卡面规则记「不适用」。请确认这个口径能进 N09 的结论行，而不是被判成「漏检」。
   **裁决：确认「不适用」口径，但必须可证伪。**
   - 不适用 = 七图 entity lump 与 BSP brush 实体字典里**计数为 0**（逐图给数：3.1e 表，func_plat / func_breakable_surf / func_water_analog / func_trackautochange 四类 × 七图 28 格全 0），因此不是「漏检」，而是这些地图里没有该实体类。
   - 给了逐图计数才算「不适用」，没给就是「未普查」。

3. **N10 的六面天空现在怎么判（本轮已抓到帧，请求升级为定判）**（原请求）：4.3 的两帧把「不盖住前景」（100% 填洞 / 0 越界 / 0 漏洞）与「六面方向」（成片主色与 VTF 逐值吻合）两项都做成了正面读数，但**只覆盖 upward 一图、t40000、一个机位**。三种口径请择一：(i) 记 **PASS** 并把「一图一机位」写进可复用边界；(ii) 记 **PASS（upward / t40000）**，另立「其余六图 + 接缝量化 + Fallback 触发」后续卡；(iii) 仍记 **NOT VERIFIED**，把 4.3 降为「部分证据」。我倾向 (ii)。
   **裁决：三条都不选，按判据分栏判，卡级记「部分完成」。**
   - 理由：卡面验收里「不会盖住前景」是**可判且已 PASS**；而「从真实 BSP 找 sky_camera 验证 origin/scale=16」是**功能不存在**（UNSUPPORTED），不是「没测」—— 两者不能合成一个卡级结论。
   - 4.4 / 4.5 的分栏即最终判决；卡级写「**部分完成：可判项 PASS / 接缝与 Fallback NOT VERIFIED / sky_camera UNSUPPORTED / 3D skybox 未实现**」，**不许写「N10 PASS」**。
   - 「其余六图 + 接缝量化 + Fallback 触发」**不新开卡**，并入 **N11（七图最终矩阵）**，由 lead 在 N11 里当缺口列。

4. **N09 推车前的位置**（原请求）：抓帧证明了 `--playback-speed 64 --capture-tick <T>` 能在 53 秒内精确停在任意 tick（4.3a 的 frame_tick=40000），这把原先「要等长实体历史探针」的阻塞解开了：可按四组 tick 各抓一帧，量 `map: model … origin=…` 的位移（trace 在 main.cpp:10475-10477 打出 origin）。是否现在就开？
   **裁决：批准解锁，但本轮不跑**（需要 hostlock，且 P1 相机链优先）。**待窗口的可执行步骤**：
   1. hostlock acquire（owner 记 n08-n10-maps），看到 ACQUIRED 再起进程；跑完 `release`。
   2. 命令形状（四组 tick 各一帧）：
      `tf2_demo_native.exe --tf-root <TF> --demo <upward POV> --playback-speed 64 --capture-tick <T> --capture-frame <out>.bmp --hud-off --hidden-window --window-size 1280x720 --audio-disabled`
      T 取四组：直线/静止、直线/移动、弯道/静止、弯道/移动 各一（段输入见 3.2 第 2 行：63 处弯道 / 13 处直线）。
   3. 三条硬要求：
      - `--audio-disabled`（或显式 `--audio-device` 并在日志里写理由）；
      - 显式 `--playback-speed`，并把**实际值**与**预计耗时**写进抓帧日志；
      - 跑完 `release`，并核 `ps -W` 计 `tf2_demo_native` = 0。
   4. 每帧要读的 trace 行与它要答的问题：
      - `map: model entity=… path=models/props_trainyard/bomb_cart.mdl origin=…`（main.cpp:10475-10477）→ 推车 origin 是否随 tick 变化，位移方向与 path_track 段是否一致；
      - `capture: armed requested=<T> frame_tick=<T>` → 是否精确停在目标 tick（否则位移不可比）；
      - `scene: camera mode=… eye=… pitch=… yaw=…` → 四帧机位是否同一（机位不同则 origin 不可直接相减）。
   5. 预计耗时：单帧帧内 ≈ 53 s（4.3a 实测 52,891 / 52,041 ms）；四帧 ≈ 212 s，加每帧启动/加载（本轮两帧 wall 107 s 中约 2 s）≈ 每帧 55 s、**四帧约 3.7 分钟**；再加 hostlock acquire/release 与资源加载，窗口按 **5 分钟**备。**本轮没跑 —— 以上是待执行计划，不是读数。**

---

*本报告的主体是静态解码与源码复核。像素层证据只有 4.3 那一对开/关帧（upward / t40000 / 一机位），**没有 TF2 参考帧对位**。除 4.3 明确列出的判据外，所有「画出来对不对」的问题仍未闭合。*

---

## 十、本文件没有证明什么

- **没有任何 TF2 官方画面的对位。** 全仓库唯一的产品像素证据是 4.3 那一对开/关帧（upward / t40000 / 一个机位），它是产品自比，不是与 TF2 的比对；本文件不对「画出来像不像 TF2」表态。
- **像素层只覆盖 upward 一张图的一个时刻。** 其余六图、其余 tick、其余机位、任意 map 的 sky_camera 行为都没有像素证据；这些一律 **NOT VERIFIED**，不是「测了不合格」。
- **N08 的四种状态样本、状态变更、暂停 / seek 后的新鲜度** 未观测（2.2）。
- **N09 的推车跟时间位移、门开合、四组 tick、倍速与 PVS 行为** 未观测（3.2）；func_plat / func_breakable_surf 七图计数为 0，是「不适用」而非「通过」。
- **N10 的接缝连续性没有逐像素量化；Fallback 状态机在真实地图上未触发**（4.4）；sky_camera 正例不可得，因为功能不存在（4.2）。
- **4.1(f) 的 $basetexturetransform 未消费只证到代码层**：像素后果未测（本轮两帧的变量是 draws 0↔1），属 D51，单列，不作 N10 反证。
- **D01 / D03–D09 / D40–D50 任何一条的完成或失效**，本文件都不证明。

