# T57 人物模型 / 动作审计 — 静态阶段报告

owner: t57-playermodel (shared task task-2)
date: 2026-10-24
phase: static only

## 0. 身份与范围

| 项 | 值 |
|---|---|
| repo | D:\TF2_Native_Test |
| branch | p0-entity-protocol |
| HEAD | 99eeea294cbe883e4c3e6b4b82c83de45d2ab926 (2026-10-24 09:27:48 +0800) |
| worktree | D:/TF2_Native_Test (main) |
| TF2 install | D:/SteamLibrary/steamapps/common/Team Fortress 2/tf |
| demo root | D:/TF2_Demo_Player/testdata/demos |
| evidence | evidence/t57-player-model/ (this dir) |

本阶段**只做静态审计**：未修改任何产品源码 / gate / 配置，未 build，未启动任何产品或探针二进制，
未抓帧，未碰 host lock (/d/TF2_Native_Worktrees/_shared/hostlock.sh)，未在 D:\TF2_Native_Test 暂存或提交。
git status 中 evidence/t57-player-model/ 是本任务新建的未跟踪目录；其余未跟踪项为其他任务所有，未触碰。

### demo 实际内容（ls 实测，2026-10-24）

    -rw-r--r-- 1 Administrator 197121 46921111 Sep 30 13:08 4a9bfb9276509d0ec5f5fdc722a95b17_match-20260927-0239-koth_bagel_rc13.dem
    -rw-r--r-- 1 Administrator 197121 51834617 Sep 30 13:08 bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem
    drwxr-xr-x 1 Administrator 197121        0 Sep 30 22:55 验收 路径 空格

即只有两份 .dem（koth_bagel_rc13、cp_snakewater_final1），与任务说明一致；第三个条目是空目录。

### TF2 资源实测

    tf2_misc_dir.vpk        5402349 bytes  (目录树)
    tf2_textures_dir.vpk    1974706 bytes  (目录树)
    tf2_misc_000.vpk .. tf2_misc_028.vpk   共 29 个数据块
    tf2_textures_000.vpk ..                数据块

所有 MDL/VVD/VTX/VMT/VTF 字节都从这些 VPK 的真实数据块里解出（脚本见 §9）。

## 1. 结论摘要

1. **第三人称实体 draw path 存在**（§2，逐段 file:line）。它不是"只有 viewmodel"：
   世界 pass 有 entity model 实例化绘制，第三人称相机模式也接在这条链上。
2. **九职业静态清单完整**（§3）：9/9 模型、7/7 LOD、5/5 companion 齐全，材质 slot0 全部可解析。
   但 9/9 的 LOD0 render index 都超过 12000 上限 → **每个职业的玩家网格都被截断**（§3.3、§5 类别 3）。
3. **材质只有 slot 0、没有 skin family**（§3.4）：整模型一张贴图，且九职业 slot0 全是 _red；
   Heavy / Spy 的 slot0 还是**头部**贴图 → 身体被涂成头。→ **wrong material slot**。
4. **两份 demo 的玩家实体没有 m_nModelIndex 槽**，模型只能靠 m_iClass 职业 fallback；
   snakewater 每 tick 20 个 CTFPlayer 里 12 个有 fallback 模型、8 个连 class 都没有 → 只有 marker（§4）。
5. **玩家实体的动画属性在 demo 里是空的**：CTFPlayer 的 m_nSequence / m_flCycle / m_flPlaybackRate /
   m_flPoseParameter 槽位数为 0（正控制：同类统计里 197 个类有 sequence 槽）→ 属于 **input lacks the state**。
6. **没有任何 gate 覆盖九职业玩家模型清单、第三人称玩家绘制或玩家 pose** → 提出最小
   player_model_probe + player-model-check（§6）。

## 2. 第三人称实体 draw path：存在（含证据）

### 2.1 调用链（实体列表 → 模型查找 → 网格上传 → 绘制）

| # | 阶段 | 位置 | 说明 |
|---|---|---|---|
| 1 | 实体分类 | native/src/entity_model.cpp:129-131 | isPlayerClassName = className 含 "TFPlayer" 或 "Player" |
| 2 | 变换提取 | native/src/entity_model.cpp:206-268 | m_vecOrigin / m_angEyeAngles 或 m_angRotation / m_iTeamNum / m_nSkin / m_iClass(必须 Int 1..9) |
| 3 | 模型索引 | native/src/entity_model.cpp:368-375 | 读 m_nModelIndex 槽，0..0xffff 才查 modelPrecache |
| 4 | 职业→模型 | native/src/entity_model.cpp:589-602 | 1 scout / 2 sniper / 3 soldier / 4 **demo.mdl** / 5 medic / 6 heavy / 7 pyro / 8 spy / 9 engineer |
| 5 | 实例构建 | native/src/entity_model.cpp:604-651 | 先按 requests 建实例；:637 viewModel 路径 renderable=false 且 "world pass skips viewmodel" |
| 6 | 职业 fallback | native/src/entity_model.cpp:653-686 | 无模型时用 m_iClass 兜底，cacheKey=path+"#class-fallback"，playerClassFallback=true |
| 7 | 实例数量 | native/src/main.cpp:8349-8363 | buildInstances(..., 256)；trace "scene: buildInstances=%zu" |
| 8 | 提交 draw 实例 | native/src/main.cpp:8427-8459 | :8435 skip viewModelSkipped；:8441-8442 hasMesh = !cacheKey.empty() && !viewModelSkipped && (renderable 或 playerClassFallback)；:8443 draws<96 |
| 9 | 队伍着色 | native/src/main.cpp:8443-8459 | team 2 红 0.86/0.28/0.22、team 3 蓝 0.28/0.52/0.86、否则灰 |
| 10 | 送入渲染器 | native/src/main.cpp:8606-8608 | setEntityBoneMatrices / setEntityModelInstances / setEntityMarkers |
| 11 | 实例缓冲 | native/src/native_renderer.cpp:3893-4010 | :3910 maxInstances=96；:3925-3927 找不到 mesh 就 continue；:3951-3959 skinned 需 boneCount>0 且 boneOffset+boneCount<=总数 |
| 12 | **真正 draw** | native/src/native_renderer.cpp:5732-5773 | 逐 entityModelDrawRanges_ DrawInstanced；**:5766 if (!range.textureView) continue;** |
| 13 | 网格上传 | native/src/main.cpp:6406-6437 | uploadEntityModelMesh；无 materialVtfPath 则 continue |
| 14 | 纹理上传 | native/src/native_renderer.cpp:3880-3891 | uploadEntityModelTexture 只对已存在 cacheKey 生效 |

结论：**存在**。只要实体的 m_iClass ∈ 1..9（或 m_nModelIndex 命中），它就会拿到
models/player/<class>.mdl 的 mesh key，被 setEntityModelInstances 收进实例缓冲，
再由 5732-5773 的 DrawInstanced 画出来。没有"只有 viewmodel"这回事。

### 2.2 第三人称相机也在这条链上

- native/include/native_ui.h:40  enum class CameraMode { FirstPerson = 0, ThirdPerson, FreeLook };
- native/src/native_ui.cpp:534-545  decideCameraSceneAction：ThirdPerson → haveObserverView ? SetOrbitFocus : HoldCurrent
- native/src/main.cpp:8240-8334  相机块：SetOrbitFocus → setObserverFocusWorld(origin)；trace "scene: camera mode=%s target=%d ..."
- native/src/main.cpp:2090-2104  VK_SPACE 单击 first→third→free-look 循环
- native/src/main.cpp:554-580    jumpToCameraBookmark 恢复 bookmark.mode，可直接进 third-person
- native/src/native_renderer.cpp:5027-5041 setOrbitPose：pitch clamp 0.25..1.45，distance clamp 0.8..12.0（box 单位）

### 2.3 这条链对玩家身体做不到的事（静态可证）

1. **只画 LOD0 且被 12000 截断**：model_loader.cpp:1215 maxVertices=0→12000；
   1229-1230 一旦到上限就整组 break；native_renderer.cpp:3823 上传时再 min(size,12000)。
   → 每个职业 LOD0 都超（最小 Pyro 12603，最大 Engineer 32724），丢 603..20724 个 render index。
2. **整模型一张贴图**：main.cpp:3749-3750 prepared.materialCandidates = textureCandidates.front()（只 slot 0）；
   MDL 的 numSkinRef/numSkinFamilies 在 native/src 全库零引用 → 没有 skin family，队伍色/多 bodypart 材质无从谈起。
3. **无 m_iClass 的玩家只有 marker**：entity_model.cpp:653-686 只在 1..9 时给模型；
   main.cpp:8596-8603 无 mesh 才画 EntityMarker（≤32 个，0.95/0.75/0.15）。
4. **动画 witness 明确排除玩家身体**：main.cpp:8558-8561 targetClass 白名单只有
   models/weapons/、models/workshop/weapons/、models/player/items/、models/workshop/player/items/，
   **models/player/<class>.mdl 不在其中** → 玩家身体永远不是 animation witness 的 target。

### 2.4 运行时旁证（已有 trace，非本次产生）

evidence/world-material-face/trace-textured.txt:32246

    scene: setInstances draws=70 markers=32

:32247

    scene: anim skinned=35 bindPose=35 matrices=147 gpuSkin=35/147 boneMismatch=0 poseChanges=62 targetInstances=6 targetPose=0 advances=62 targetAdv=0 cycle=5951/0 tick=31065 status=ok reason=idle

即 70 个 draw 实例里 35 蒙皮 + 35 bind pose；targetPose=0 说明白名单 target 没有一个改姿势。
这条 trace 证明世界 pass 确实在提交实体 draw，但它**没有**按职业区分玩家，也没有玩家 pose 读数。

## 3. 九职业静态资源清单

### 3.1 方法（读真实文件头，不信任文件名）

- tools/vpk_inventory.py：解析 VPK v2 目录树（header 28 字节，treeSize 在 offset 8；
  entry 18 字节 crc/preload/arch/off/len/0xFFFF），数据块路径 "%s_%03d.vpk"（:69-70）。
- MDL studiohdr_t 字段偏移：0 id / 4 version / 8 checksum / 12 name / 76 length / 152 flags /
  156 numBones / 160 boneIndex / 188 numSequences / 192 sequenceIndex / 204 numTextures /
  208 textureIndex / 212 numCdTextures / 216 cdTextureIndex / 220 numSkinRef / 224 numSkinFamilies /
  232 numBodyParts / 236 bodyPartIndex / 240 numLocalAttachments / 244 attachmentIndex。
- VVD：IDSV magic / 4 version / 8 checksum / 12 numLODs / 16 numLOD0Vertices / 48 numFixups。
- VTX：version-first（0 version / 16 checksum / 20 numLODs）。
- tools/vtx_counts.py：把 native/src/model_loader.cpp:244-311 parseVtxHierarchyWithStrides 逐行复刻
  （bodyPart 8 → model 8 → lod 12 → mesh 9 → stripGroup 25；indexStride 2，vertexStride 9，
  originalIndex 在 groupOff+vertexRel+localIndex*9+4）。
- tools/materials_resolve.py：按 native/src/model_loader.cpp:147-170 textureCandidatesFor 与
  native/src/vmt_material.cpp:165-174 VmtParser::resourcePath 的规则解析 VMT→VTF，
  并且**只读 VMT 根块（brace depth 1）的 $basetexture**——与产品一致。
- 交叉验证：脚本给出的 Scout 15/17、Medic 19/21 与 entity-material-check.sh:137-138
  和 evidence/entity-material/probe-models_player_scout.mdl.txt 完全一致；scout 顶点数 12000
  与产品 probe 的 "scoutVertices":12000 一致。

### 3.2 主表（raw/nine_class_table.tsv，33 列）

| class | tfClass | mdl | checksum | bones | seqs | tex | cdtex | bodyParts | attach | VVD LOD0 verts | VTX LOD0 idx |
|---|---|---|---|---|---|---|---|---|---|---|---|
| Scout | 1 | models/player/scout.mdl | 1098120898 | 78 | 306 | 17 | 2 | 5 | 29 | 8261 | 23442 |
| Sniper | 2 | models/player/sniper.mdl | 3495254885 | 74 | 240 | 20 | 2 | 5 | 20 | 9854 | 23595 |
| Soldier | 3 | models/player/soldier.mdl | 3748736120 | 86 | 361 | 20 | 3 | 5 | 24 | 9626 | 26922 |
| Demoman | 4 | **models/player/demo.mdl** | 1505337062 | 84 | 276 | 17 | 2 | 4 | 25 | 9914 | 25755 |
| Medic | 5 | models/player/medic.mdl | 1029183977 | 92 | 213 | 21 | 2 | 2 | 20 | 10332 | 24864 |
| Heavy | 6 | models/player/heavy.mdl | 1641026357 | 79 | 259 | 21 | 2 | 3 | 20 | 13847 | 28944 |
| Pyro | 7 | models/player/pyro.mdl | 3846592064 | 76 | 270 | 8 | 3 | 5 | 24 | 6337 | 12603 |
| Spy | 8 | models/player/spy.mdl | 4261541971 | 84 | 274 | 31 | 2 | 2 | 27 | 5183 | 16980 |
| Engineer | 9 | models/player/engineer.mdl | 1880291295 | 73 | 351 | 15 | 2 | 3 | 13 | 16829 | 32724 |

- 全部 mdlMagic=IDST、mdlVersion=48；VVD magic IDSV、version 4；VTX version 7；每职业 VVD numLODs=7。
- Demoman 的模型文件名是 **demo.mdl**，不是 demoman.mdl —— 与 entity_model.cpp:589-602 一致，
  也与 TF2 资产命名一致；按 "demoman.mdl" 去找会得到 gap，这是文件名陷阱。
- companions：9/9 都有 .vvd、.dx90.vtx、.dx80.vtx、.sw.vtx、.phy（裸 .vtx 为 0，产品不需要裸 .vtx）。
  → **没有任何职业缺 companion**。

### 3.3 逐 LOD render index 数（raw/vtx_index_counts.json）

| class | LOD0 | LOD1 | LOD2 | LOD3 | LOD4 | LOD5 | LOD6 | total | descriptors | 12000 截掉 |
|---|---|---|---|---|---|---|---|---|---|---|
| Scout | 23442 | 17817 | 13005 | 6342 | 4263 | 3729 | 4263 | 72861 | 50 | 11442 |
| Sniper | 23595 | 16836 | 11850 | 8535 | 6336 | 5079 | 6336 | 78567 | 52 | 11595 |
| Soldier | 26922 | 15741 | 10137 | 6252 | 4521 | 3477 | 6252 | 73302 | 50 | 14922 |
| Demoman | 25755 | 14805 | 9189 | 6870 | 3663 | 1716 | 3663 | 65661 | 40 | 13755 |
| Medic | 24864 | 17640 | 11682 | 7578 | 5061 | 2322 | 7578 | 76725 | 29 | 12864 |
| Heavy | 28944 | 21633 | 16752 | 13212 | 10509 | 9117 | 14550 | 114717 | 43 | 16944 |
| Pyro | 12603 | 10143 | 7197 | 4941 | 3114 | 1632 | 3114 | 42744 | 41 | 603 |
| Spy | 16980 | 10326 | 7566 | 4698 | 3489 | 2595 | 3489 | 49143 | 31 | 4980 |
| Engineer | 32724 | 19497 | 14340 | 12090 | 10422 | 9861 | 10422 | 109356 | 37 | 20724 |

- 全部 outOfBounds=0、rejectedGroups=0 → 不是解析失败，是解析成功后按
  bodyPart→model→lod→mesh→group 顺序被 12000 上限**整组截断**，丢的是靠后的 bodypart/组（几何缺块），
  不是均匀降采样。LOD1..6 数据齐全但产品只取 LOD0（model_loader.cpp:1224 if (anyLod0 && descriptor.lod != 0) continue;）。

### 3.4 材质（raw/materials_resolved.json）

| class | slots | resolved | slot0 texture | slot0 VMT → VTF | incomplete |
|---|---|---|---|---|---|
| Scout | 17 | 15 | models/player/scout/scout_red | 存在 | eyeball_l/r |
| Sniper | 20 | 18 | models/player/sniper/sniper_red | 存在 | eyeball_l/r |
| Soldier | 20 | 18 | models/player/soldier/soldier_red | 存在 | eyeball_l/r |
| Demoman | 17 | 16 | models/player/demo/demoman_red | 存在 | eyeball_r |
| Medic | 21 | 19 | models/player/medic/medic_red | 存在 | eyeball_l/r |
| Heavy | 21 | 19 | models/player/hvyweapon/heavy_head_red | 存在（base=heavy_head） | eyeball_l/r |
| Pyro | 8 | 8 | models/player/pyro/pyro_red | 存在 | - |
| Spy | 31 | 29 | models/player/spy/spy_head_red | 存在 | eyeball_l/r |
| Engineer | 15 | 15 | models/player/engineer/engineer_red | 存在 | - |

- incomplete 全部是眼球：materials/models/player/<class>/eyeball_l.vmt 是 EyeRefract，
  $basetexture 嵌在 "Eyes_dx8" { } 里，产品只读根块 → complete=0。这是资产属性，不是缺 companion。
  证据：evidence/entity-material/probe-models_player_scout.mdl.txt:22-23、40-41
  "model-material name=models/player/scout/eyeball_l vmt=... vmtBytes=1498 parsed=1 complete=0"。
- **九职业 slot0 全是 _red**：蓝队贴图在 texture[4]/[5]，产品只取 slot0 → 蓝队必然红皮。
- **Heavy slot0 = heavy_head_red、Spy slot0 = spy_head_red**：单贴图整模型绘制时，
  身体会被涂成头部贴图（heavy 的 VMT $basetexture 甚至指向 heavy_head 而非 heavy_head_red）。

### 3.5 独立动画模型（raw/anim_models.json，18 个真实头解析）

| 模型 | bones | sequences | textures | bytes |
|---|---|---|---|---|
| scout_animations.mdl | 76 | 377 | 0 | 5017436 |
| scout_user_animations.mdl | 69 | 1 | 2 | 29512 |
| sniper_animations.mdl | 74 | 330 | 0 | 4332020 |
| sniper_user_animations.mdl | 67 | 1 | 5 | 28996 |
| soldier_animations.mdl | 86 | 419 | 0 | 5375964 |
| soldier_user_animations.mdl | 79 | 1 | 4 | 33600 |
| demo_animations.mdl | 82 | 326 | 0 | 4412588 |
| demo_user_animations.mdl | 75 | 1 | 3 | 32064 |
| medic_animations.mdl | 92 | 265 | 0 | 5003812 |
| medic_user_animations.mdl | 84 | 1 | 4 | 35668 |
| heavy_animations.mdl | 77 | 317 | 0 | 4083444 |
| heavy_user_animations.mdl | 70 | 1 | 4 | 30332 |
| pyro_animations.mdl | 76 | 351 | 0 | 4573600 |
| pyro_user_animations.mdl | 68 | 1 | 1 | 29112 |
| spy_animations.mdl | 84 | 328 | 0 | 4726416 |
| spy_user_animations.mdl | 77 | 1 | 4 | 33784 |
| engineer_animations.mdl | 73 | 402 | 0 | 5225668 |
| engineer_user_animations.mdl | 65 | 1 | 2 | 27968 |

- render 模型骨骼数（78/74/86/84/92/79/76/84/73）与 <class>_animations.mdl 骨骼数
  （76/74/86/82/92/77/76/84/73）不完全相同（scout 78 vs 76、demo 84 vs 82、heavy 79 vs 77），
  与 animation_binding.h:20-24 的说明一致（多出的 helper 骨保持 bind transform）。
- 产品确实会去加载这个独立动画源：animation_binding.cpp:64 companionPath = dir + stem + "_animations.mdl"；
  :167-215 三条路由（模型自带 / include 列表声明 / 命名约定），:190 从 includes 里找 *_animations.mdl，
  :206/:275 separateAnimSource=true，:341-347 骨骼映射失败时回退。
  :178-183 的实测注释：scout.mdl 306 个 sequence 里只有 2 个有 animdesc（route 1 形同虚设）。
- 动画集规模：每个职业 265..419 个 sequence，全部来自独立动画模型；主模型自带的 sequence
  是 stub（见上）。

## 4. demo 侧可达性

### 4.1 snakewater（bb841c6d...，51834617 bytes）— 已有实测

来源 evidence/outline/probe-demo.txt（entity_overlay_probe 产物，128 行，
6 个 tick：64230 / 64261 / 64292 / 64323 / 64354 / 64385）。生成命令见 outline-check.sh:79-83、:301。

- 每个 tick 20 行 CTFPlayer；tick 汇总 "tick summary requested=<tick> rows=20 red=6 blue=6 unknown=8 posed=20"。
- 末行："demo summary ticks=6 rows=120 distinctEntities=20 withTeam=120 red=36 blue=36
  unknown=48 withPose=120 exact=0 checkpointFallback=6 unavailable=0 appeared=20 vanished=0
  ticksWithRows=6 rowsMissingTeam=0 rowsMissingPose=0 teamMappingMismatches=0"。
- 行样例（probe-demo.txt:3）：
  "tick requested=64230 resolved=63721 fidelity=checkpointFallback stale=509 entity=2
   class=CTFPlayer player=1 team=red raw=2 hasTeam=1 origin=4395.17,-2326.22,84.01 hasOrigin=1
   model=models/player/soldier.mdl classFallback=1 reason=no m_nModelIndex slot; class fallback model path"
- **关键事实**：每个 tick 里 12/20 有 class fallback 模型（soldier/scout/demo/medic/heavy 等），
  8/20 是 "model=- classFallback=0"（team=unknown raw=1，m_iClass 缺失或越界）
  → 这些实体永远只有 marker 没有身体。
- **reason="no m_nModelIndex slot"**：该 demo 的玩家实体没有 m_nModelIndex 槽，
  模型只能来自 m_iClass 职业 fallback。产品自己的注释也记录了这一测量：
  native/tools/entity_overlay_probe.cpp:290-296 —— "NO player carries m_nModelIndex:
  21 of 21 sampled rows had identity, team and pose and none had a model, so the join rejected
  every row and the pass drew nothing in 778 of 778 frames."

### 4.2 bagel（4a9bfb92...，46921111 bytes）— 部分未知

已知（evidence/animation-availability/bagel.txt:102）：

    animprop-player class=CTFPlayer id=247 sequence=0 cycle=0 rate=0 pose=0

以及 animation-availability-check.sh:102-129 的断言：CTFPlayer 在三个录像里
m_nSequence=0、m_flCycle=0、m_flPlaybackRate=0、m_flPoseParameter=0 个 slot，且 tfPlayerFound=1
（类存在但为空）。正控制：同一次运行 classesWithSequence=197、classesWithRate=197、
classesWithCycle=195；CTFWeaponBase 有 1 个 sequence slot（脚本 205-218 的 assert_eq 钉住）。

**未知（NOT VERIFIED）**：bagel 每 tick 的 CTFPlayer 实体数、team 分布、有/无 class 的比例、
可达 tick 范围。→ 见 §7 第 1 条 proposal（跑 entity_overlay_probe）。

### 4.3 建议的 demo 侧探针命令（精确，交 Lead 串行）

    # 1) 实体/职业/队伍/模型可达性（无需窗口，纯 demo 解析）
    native/build-nmake/entity_overlay_probe.exe \
      --demo "D:/TF2_Demo_Player/testdata/demos/4a9bfb9276509d0ec5f5fdc722a95b17_match-20260927-0239-koth_bagel_rc13.dem" \
      --ticks 1000,3000,6000,9000,12000,15000,18000,21000 > evidence/t57-player-model/raw/probe-bagel.txt

    # 2) 同上，带 JSON（便于 gate 断言）
    native/build-nmake/entity_overlay_probe.exe --demo "<bagel.dem>" --ticks 1000,6000,12000 --json \
      > evidence/t57-player-model/raw/probe-bagel.json

    # 3) 动画属性（确认玩家没有 sequence/cycle/rate/pose 槽）
    native/build-nmake/entity_model_probe.exe --tf-root "D:/SteamLibrary/steamapps/common/Team Fortress 2/tf" \
      --demo "<bagel.dem>" --anim-props > evidence/t57-player-model/raw/animprops-bagel.txt

    # 4) 实体属性快照（看 m_iClass/m_iTeamNum/m_vecOrigin 是否在）
    native/build-nmake/entity_model_probe.exe --tf-root "D:/SteamLibrary/steamapps/common/Team Fortress 2/tf" \
      --demo "<bagel.dem>" --props-at 6000 --dump-class-props CTFPlayer

flags 出处：entity_overlay_probe --self-test|--fixtures|--demo <path> --ticks a,b,c [--json]
（native/tools/entity_overlay_probe.cpp:683）；entity_model_probe flags 见
native/tools/entity_model_probe.cpp:1406-1449。

## 5. gap 分类（8 类）

| # | 类别 | 判定 | 证据 | 严重度 |
|---|---|---|---|---|
| 1 | no draw path | **否** — 路径存在 | §2.1 全链；native_renderer.cpp:5732-5773 DrawInstanced | - |
| 2 | wrong entity→model | **是** | 8/20 实体无 m_iClass → 只有 marker（probe-demo.txt tick 汇总 unknown=8）；全部 20 个都没有 m_nModelIndex 槽（entity_overlay_probe.cpp:290-296） | 高 |
| 3 | mesh or LOD missing | **是** | model_loader.cpp:1215/1229-1230 12000 上限；九职业 LOD0 12603..32724 全超，丢 603..20724 index；native_renderer.cpp:3823 二次截断 | 高 |
| 4 | wrong material slot | **是** | main.cpp:3749-3750 只取 slot0；MDL numSkinRef/numSkinFamilies 零引用；九职业 slot0 全 _red；Heavy/Spy slot0 是头贴图 | 高 |
| 5 | bone binding or pose | **未验证（运行时）** | 静态：animation_binding.cpp:167-215 支持独立动画源、:341-347 骨骼映射失败回退；无玩家 pose 读数 | 中 |
| 6 | animation time advance | **未验证（运行时）**；静态倾向"输入没有" | main.cpp:8558-8561 witness 白名单不含 models/player/<class>.mdl；demo 里 CTFPlayer 的 sequence/cycle/rate 槽数为 0 | 高 |
| 7 | camera or near-clip | **未验证** | native_renderer.cpp:5376-5387 世界近平面 4 map units；:5027-5041 orbit distance 0.8..12 box 单位（最小 0.8 可能贴脸/穿模） | 中 |
| 8 | input lacks the state | **是（动画）** | animation-availability bagel.txt:102 sequence=0 cycle=0 rate=0 pose=0；正控制 197 类有 sequence 槽 | 高 |

补充：第 2 类与第 8 类叠加的后果是——**在 snakewater 上，8/20 的玩家实体连"有身体"都不成立，
12/20 有身体但没有动画状态**。因此"人物乱成一团"的样例若出现在这两份 demo 上，
首先应怀疑的是 2/3/4（模型与材质），而不是骨骼或动画时间推进。

## 6. 最小 player-model-probe + player-model-check 设计

现有 gate 无一覆盖：九职业玩家模型清单、第三人称玩家绘制、玩家 pose（§8）。
因此提出下面两个固定名字的产物，交 Lead 在 worktree 里实现。

### 6.1 native/tools/player_model_probe.cpp → native/build-nmake/player_model_probe.exe

固定 CLI：

    player_model_probe --tf-root <tf> [--class 1..9]... [--json] [--fixture-root <dir>]
                       [--expect-fail <reason>]

每个职业输出一行（字段顺序固定，gate 按行 grep）：

    player-model class=Scout tfClass=1 mdl=models/player/scout.mdl checksum=1098120898
      mdlVersion=48 bytes=611668 bones=78 sequences=306 textures=17 bodyParts=5 attachments=29
      vvd=1 vvdVersion=4 vvdLods=7 vvdLod0Verts=8261
      dx90=1 dx80=1 sw=1 phy=1 bareVtx=0
      vtxVersion=7 vtxLod0=23442 vtxDescriptors=50 cap=12000 lost=11442
      slots=17 resolved=15 slot0=models/player/scout/scout_red slot0Vmt=1 slot0Vtf=1
      animModel=models/player/scout_animations.mdl animBones=76 animSequences=377 animToModel=76
      bindPoseVerts=12000 bindPoseTruncated=1 status=ok

末尾一行：

    player-model summary classes=9 companionsComplete=9 slot0Resolved=9 animModelFound=9
      animChannelMapped=9 lod0Truncated=9 bindPoseTruncated=9 failures=0

失败理由（枚举，必须逐条可区分，不许合并）：

- missing-mdl / missing-vvd / missing-vtx / missing-phy
- vtx-version-unsupported
- checksum-mismatch（MDL/VVD/VTX 三者 checksum 不一致）
- slot0-vmt-missing / slot0-vtf-missing / slot0-unresolved
- anim-model-missing / anim-channel-unmapped（poseToBone 全 -1）
- no-bind-pose-mesh（buildBindPoseMeshLod0 返回 false）
- not-nine-classes

实现要点（全部复用现有代码，不新写解析器）：
- ModelLoader::resolveAsset + inspectFile + inspectBuffers（model_loader.cpp:351-384/775-846/858-906）
- ModelLoader::buildRenderRequests（:940-1012）取 cacheKey/renderable
- ModelLoader::buildBindPoseMeshLod0（:1208-1253）取 bindPoseVertices.size() 与截断标志
- AnimationBindingCache::resolve（animation_binding.cpp）+ binding.animToModel 非 -1 计数
- 材质：复用 main.cpp:5003-5025 的解析规则（VMT 根块 $basetexture + VTF 头）

身份行（gate 必须钉住）：

    player-model identity tfRoot=<tf> miscVpk=<bytes> texturesVpk=<bytes> probe=<exe sha256>

### 6.2 player-model-check.sh（固定名字，仓库根目录，与其他 *-check.sh 并列）

断言（每条都要有"能变红"的证明）：

1. 九职业全部出现：grep -c '^player-model class=' == 9
2. companion 齐全：每行 vvd=1 dx90=1 dx80=1 sw=1 phy=1
3. slot0 可解析：每行 slot0Vmt=1 slot0Vtf=1 且 resolved>0
4. 动画源可加载且通道映射：每行 animModelFound=1、animToModel>0
5. bind pose 网格可构建：每行 bindPoseVerts>=3
6. 截断必须被**显式报告**（不是 pass/fail 阈值放宽）：每行 cap=12000，
   且 lod0Truncated=1 与 lost 数值必须与 probe 的 vtxLod0-cap 一致
7. 变异捕获：--fixture-root 指向一份把某个 .vvd 改名/删除的临时副本时，
   probe 必须报 missing-vvd、exit!=0、gate 必须变红（这是"能变红"的证据）
8. 任何一行 status!=ok → gate 失败并打印该行

gate 证据落 evidence/player-model/<name>.txt，含 identity 行 + 九行 + summary。

注意：这个 gate **只证明静态装载**。它不能替代第三人称验收（任务 §6 明确
viewmodel-robustness-check.sh 只覆盖其声明范围）。第三人称像素验收需要 §7 的
player-model-visual 步骤，二者分开命名，不许互相顶替。

## 7. 运行时 proposal（需 build/launch/capture，全部交 Lead 串行排期）

前提：每次运行前 hostlock.sh 加锁，结束后释放；产品必须 --hidden-window --audio-disabled；
测试窗口不得覆盖用户窗口；每步记录 exe sha256、demo sha256、tick、camera、settings。

| # | 目的 | 精确命令 | 预计时长 | 锁 |
|---|---|---|---|---|
| 1 | bagel 实体可达性 | native/build-nmake/entity_overlay_probe.exe --demo "<bagel.dem>" --ticks 1000,3000,6000,9000,12000,15000,18000,21000 > evidence/t57-player-model/raw/probe-bagel.txt | ~40 s | 需要（探针二进制） |
| 2 | bagel 动画属性 | native/build-nmake/entity_model_probe.exe --tf-root "<tf>" --demo "<bagel.dem>" --anim-props > raw/animprops-bagel.txt | ~60 s | 需要 |
| 3 | snakewater 材质实测 | native/build-nmake/tf2_demo_native.exe --tf-root "<tf>" --demo "<snakewater.dem>" --hidden-window --audio-disabled --start-paused --capture-tick 64230 --capture-frame raw/sw-64230.png --dump-entity-materials raw/materials-sw.tsv | ~90 s | 需要 |
| 4 | 第三人称可见玩家身体 | 同上，但加 --settings-file raw/thirdperson.ini（bookmark1.mode=third-person, saved=1, focusX/Y/Z 取实体 2 的 origin, yaw/pitch/distance 自定）与 --camera-script raw/camera.txt（内容 "jump 1"），--capture-tick 64230,64261,64292 | ~150 s | 需要 |
| 5 | 玩家 pose 读数 | 需要先实现 §6 的 probe 扩展（--entity/--pose-at），或在 #4 的 trace 里加玩家 skin 计数 | ~120 s | 需要 |
| 6 | 变异：companion 缺失 | 复制一份 tf 目录的子集到临时 fixture（或 --fixture-root），删 scout.vvd，跑 player_model_probe，期望 exit!=0 且 missing-vvd | ~30 s | 不需要（纯静态） |

预计总时长约 8 分钟（不含 build）。若 #5 需要新增 C++ 代码，另加 build 时间。

## 8. NOT VERIFIED 清单（明确未测，不许当 pass）

1. bagel demo 的每 tick CTFPlayer 实体数 / team / class 分布 / 可达 tick —— 未跑探针（§7 #1）。
2. 两份 demo 里玩家实体**是否真的被画出来**（第三人称像素）—— 没有任何第三人称截图；
   现有 trace 只有 draws=70 markers=32 的总数，不按职业分解。
3. main.cpp:3733-3734 meshSource = modelRenderRequests + classRequests 与运行时
   "gpu: models done 59 uploaded=59" + "gpu: overlay meshes prepared=9 uploaded=9" 的关系
   （59 是否已含 9 个职业模型，还是 classRequests 被 3738 的 !renderable 跳过）。
   两种解释对"玩家身体是否有世界 pass mesh"的结论不同 → player_model_probe 首要读数之一。
4. 玩家模型的 AnimationBinding 在运行时是否真的 resolve 成功（静态代码支持，无运行时读数）。
5. 第三人称 orbit 最小距离 0.8 box 单位是否导致近平面/穿模 —— 需实测（§7 #4）。
6. 12000 截断在画面上的具体表现（缺哪些 bodypart）—— 需要 #4 的截图与 §6 的 descriptor 报告对照。
7. Heavy/Spy 头贴图涂全身的实际观感 —— 需要 #4。

## 9. 产物与复现

| 文件 | 内容 |
|---|---|
| evidence/t57-player-model/REPORT.md | 本报告 |
| evidence/t57-player-model/raw/nine_class_table.tsv | 九职业 33 列合并表（真实头） |
| evidence/t57-player-model/raw/player_models.json | VPK 扫描 + 九职业 MDL/VVD/VTX 头 |
| evidence/t57-player-model/raw/vtx_index_counts.json | 逐 LOD render index 计数 |
| evidence/t57-player-model/raw/materials_resolved.json | 材质 slot 解析结果 |
| evidence/t57-player-model/raw/anim_models.json | 18 个独立动画模型头 |
| evidence/t57-player-model/tools/vpk_inventory.py | VPK v2 目录树 + MDL/VVD/VTX 解析 |
| evidence/t57-player-model/tools/vtx_counts.py | parseVtxHierarchyWithStrides 的 Python 复刻 |
| evidence/t57-player-model/tools/materials_resolve.py | VMT→VTF 解析（镜像产品规则） |
| evidence/t57-player-model/tools/make_table.py | 合并生成 nine_class_table.tsv |

复现：

    cd /d/TF2_Native_Test
    python evidence/t57-player-model/tools/vpk_inventory.py
    python evidence/t57-player-model/tools/vtx_counts.py
    python evidence/t57-player-model/tools/materials_resolve.py
    python evidence/t57-player-model/tools/make_table.py

## 10. 建议的下一步（按优先级）

1. 先跑 §7 #1（bagel 实体可达性，40 s）——决定 bagel 能不能作为第三人称验收输入。
2. 实现 §6 的 player_model_probe + player-model-check（静态，无锁，可并行）。
3. 跑 §7 #3/#4 拿第三人称截图，按 §5 的 2/3/4 类逐项定位。
4. 只有在 1-3 完成后，才谈骨骼/动画时间推进（类别 5/6），否则会把"没画出来"误诊成"pose 错"。
