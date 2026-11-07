# T85-COVERAGE-MANIFEST — 支持声明 → 证据映射表

冻结基线：`e15577b4f0550870bb11672a04497c038c9d88b2`（t79-integration / integration/t79-final，工作树干净）。
产品二进制：`native/build-nmake/tf2_demo_native.exe`，2,763,264 B，sha256 `a3c93d5755fd331351766dfb991fe17f90c4dfb753ad7ef9f53391f0303724e9`。
配套：`docs/T85-SUPPORT-SCOPE.md`（冻结边界）、`docs/T85-DEBT-LIST.md`（欠账）、`evidence/t62-matrix/POSTFREEZE-INVENTORY-CORRECTED.md`（账本对账）。

## 0. 怎么读这张表

- **这张表不是重跑记录。** 阶段 0 只读、不构建、不启动产品、不抓帧（见 `D:/_shared/T85-CONSTRAINTS.md`）。表中的 PASS 全部是 `e15577b` 已提交证据的**引用**，不是本次执行结果。任何"我重跑过"的说法都不成立。
- **等级是"引用的可核性"，不是"结论的强度"**：
  - **A** = 断言行与判定行都逐行读过（`sed -n`），并且知道它断言了什么、不断言什么。
  - **B** = 读过文件头/接受边界或 grep 到判定行，但未逐行读每一条 assert。
  - **C** = 只有矩阵/报告的读数，没有门禁；这类"支持"实为文档结论。
- **门禁 PASS 只支持它断言过的东西。** 每一格的"不证明"列尽量写明；跨图/跨职业/跨 tick 的外推一律不成立（`docs/KNOWN-ISSUES-AND-FIXES.md:3`："A focused PASS is limited to the named gate"）。
- 产品路径一律相对仓库根；行号是 `e15577b` 上的真实行号。
- 夹具哈希：已实测的写 sha256；未实测的写 **未哈希（本次）**，不得当作已固定。

## 1. 角色静态外观与身份（C01–C03）

| ID | 支持声明 | 产品路径 | 输入夹具 | 门禁 + 断言/判定行 | 变异 / 负控 | 证据目录 | 级 |
|---|---|---|---|---|---|---|---|
| C01 | 九职业模型可解析：LOD0 未截断（`lod0Truncated=0`）、每材质槽红蓝两族涂装、独立 `<class>_animations.mdl` 动画源 | `native/src/entity_model.cpp:699`（`defaultPlayerModelPath`）、`native/src/model_loader.cpp:1568`（`buildBindPoseMeshLod0`） | bagel：`D:/TF2_Demo_Player/testdata/demos/4a9bfb92…-koth_bagel_rc13.dem` sha256 `03529698…b5436`；snakewater：`bb841c6d…-cp_snakewater_final1.dem` sha256 `308cc145…4607f` | `player-model-check.sh:213`（交代 `lod0Truncated` 断言）、`:294`（PASS 判定行） | 四个**先跑**的具名负控：错误 tf-root（rc=2）、坏 demo（rc=2）、8 条职业表（rc=2）、职业模型解析失败（rc=1），见 `:184-190` | `evidence/t57-player-model/`（由矩阵引用） | A |
| C02 | 采样到的玩家实体**不带** model index，模型来自职业回退：bagel 39 行/36 有模型/36 回退；snakewater 20/14/14 | `native/src/entity_model.cpp:782`（`defaultPlayerModelPath(transform.playerClass)`）、`native/src/entity_overlay.cpp:301` | 同上两 demo | 无专门门禁断言这两个计数；读数为探针/矩阵读数（`entity_model_probe.exe`） | — | `evidence/t62-matrix/CHARACTER-MATRIX.md:42` | C |
| C03 | 被绘制的第三人称身体带队伍色且红蓝在同一帧内可区分：九对（7 对 tick 24001 + Scout/Sniper tick 45001） | `native/src/main.cpp:4045-4141`（LOD0 bind-pose 身体绘制）、`native/include/material_slot_resolution.h:99`（`textureSlotFor`） | upward Highlander STV `a9630c65…-pl_upward_f12.dem`（**未哈希（本次）**）+ snakewater | `player-body-appearance-check.sh:50`（floor 200 px mask/15 tint/40 pair gap）、`:238-244`（pair gap 计算）、`:287`（PASS） | 同帧 identity mask + 两帧红/蓝差；单队/无身体为负向 | `evidence/t62-matrix/body-appearance/`、`body-appearance-upward/gate-two-team-2026-11-07.log` | B |

## 2. 动画（C04–C07）

| ID | 支持声明 | 产品路径 | 输入夹具 | 门禁 + 断言/判定行 | 变异 / 负控 | 证据目录 | 级 |
|---|---|---|---|---|---|---|---|
| C04 | 第三人称骨骼动画：序列表、骨骼绑定、四分周期姿势、连续性、demo 时钟驱动；233 断言 0 失败 | `native/src/animation_decoder.cpp`、`native/include/animation_sequence_match.h`、`native/src/animation_clock.cpp:110` | snakewater + bagel | `player-animation-check.sh:378`（`PLAYER-ANIMATION-CHECK=PASS`） | 门禁内含姿势/时钟变异断言（该文件 383 行） | `evidence/t61-animation/r10-after-demos/check.log` | B |
| C05 | 动画驱动**到达像素**且跨职业：每职业一具身体一次采样，移动像素 ≥200 且动画 share ≥5% | `native/src/main.cpp:2117-2141`（默认标志）、`native/src/entity_model.cpp:206` | snakewater + upward 各自 tick（64161/63000/64000、20001/24001/30001/30001…） | `class-pixels-check.sh:108`（`MIN_BODY_PIXELS=200`）、`:60-70`（相位钉原理）、`:401`（PASS） | 同帧 drive on/off 对比 + identity 双捕获（byte-identical） | `evidence/t62-matrix/class-pixels/`、`class-pixels-upward/` | A |
| C06 | crouch / crouchwalk / flinch 在 **opt-in** `--animation-drive` 下产生可读状态 trace；默认仍是 legacy | `native/src/main.cpp:2133`（`animationDriveStates=false`）、`:2794/:2809/:2816`（参数解析） | snakewater（tick 14765 等） | `animation-drive-states-check.sh:446`（PASS 长行，含 24 条 flinch trace、24/24 语义） | 默认 vs opt-in 双跑对照 | 该门禁自身日志 | B |
| C07 | 九职业红蓝同队对已完成（18 目标两批），队伍维度**已测** | 同 C03 | upward STV + snakewater | `RUNS-2026-11-04.md:1999`（§26.5 18 目标/九对）、`:2021`（证据）、`CHARACTER-MATRIX.md:178-179` | ent 号不跨 tick 携带；同 tick 两队 | `evidence/t62-matrix/body-appearance-upward/` | C |

## 3. 第三人称武器与第一人称 ViewModel（C08–C12）

| ID | 支持声明 | 产品路径 | 输入夹具 | 门禁 + 断言/判定行 | 变异 / 负控 | 证据目录 | 级 |
|---|---|---|---|---|---|---|---|
| C08 | 九职业每人手持本职业武器，挂点与骨骼重合（drift 0.00），attached≠off，off 等于该职业 class-pixels 基线 | `native/src/main.cpp:2139`（`playerWeaponAttachEnabled`）、`native/src/main.cpp:3695/:3730`（prepared meshes 上限 64） | snakewater yaw 45（Demoman/Scout/Soldier/Medic）、`pl_upward_f12` yaw 225（Pyro/Heavy/Spy/Engineer/Sniper） | `third-person-weapon-check.sh:82-102`（默认输入）、`:457-461`（武器像素）、`:580`（九职业计数行）、`:582`（PASS） | 相位钉住（`maxDeltaCycle` 0.84 vs 9.32）；off 行必须等于基线；refused-row 计数 | `evidence/t83-weapon/weapon-attach/`（51 MB） | A |
| C09 | 实体网格预算可饱和且拒绝有具名原因：`kEntityModelMeshLimit=96`，`supply=23 refused=3`，三把非目标武器被拒 | `native/src/native_renderer_entity.cpp:168`（96）、`native/src/main.cpp:3730`（reserve(64)） | 同上 | `outline-check.sh:1840-1857`（96 上限变异断言） | 三把非目标武器 `0x8007000E` 具名拒绝；`noWorldMesh` 9→0 | `RUNS-2026-11-04.md:1943`（§26.3）等 | A |
| C10 | 第一人称 ViewModel 绘制契约：必须画 ViewModel；缺资源时关闭该 pass 而非崩溃；缺动画输入时画 bind pose | `native/src/viewmodel.cpp:113`（`isViewModelPath`）、`native/src/native_renderer_viewmodel.cpp` | `c_pro_rifle` / `CTFSniperRifle`（2 材质，capture_tick=2000） | `viewmodel-contract-check.sh`（127 断言，变异判定 `:1194`） | 缺资源/缺动画两个具名负控 | 该门禁自身日志 | B |
| C11 | 应用姿态（`--viewmodel-apply-animation`）时**确实改变像素**，重复运行一致，且仍只落在 ViewModel 上；33 断言 | `native/src/main.cpp:2117`（默认 false）、`:10933`（`poseAvailable && viewModelApplyAnimation`）、`native/src/animation_binding.cpp:41` | 钉住的录制（见 `wire-animation-audit.md:100-102`） | `viewmodel-apply-check.sh:818`（`VIEWMODEL-APPLY=PASS`） | bind/applied/repeat/world-only/flat 五类比对 | 该门禁自身日志 | B |
| C12 | ViewModel 动画输入契约：只画实体自己命名的模型、只读该实体、stub 为负控 | `native/src/viewmodel.cpp`、`native/src/entity_model.cpp:714` | 同上 | `viewmodel-animation-check.sh:1169`（PASS） | "实体未命名的模型被拒绝而不是绘制"负控 | 该门禁自身日志 | B |

## 4. 世界与地图（C13–C18）

| ID | 支持声明 | 产品路径 | 输入夹具 | 门禁 + 断言/判定行 | 变异 / 负控 | 证据目录 | 级 |
|---|---|---|---|---|---|---|---|
| C13 | 官方四图可解析且 BSP 来源可追（`koth_bagel_rc11` **只在** `tf/download/maps`），名称/来源/回退写入 trace | `native/src/map_lookup.cpp:26`（`isAllowedMapNameCharacter`）、资源发现链 | `tf/maps/cp_snakewater_final1.bsp` 59,590,420 B `f09d406d…a1eb`；`cp_sunshine.bsp` 30,319,857 `ca9ad7d0…2474`；`koth_bagel_rc12.bsp` 26,486,963 `c991a61e…d680`；`tf/download/maps/koth_bagel_rc11.bsp` 26,370,688 `f06cfe4b…720a` | `map-material-matrix.sh:36`（`demo map=`/`server_map=` 必须等于该行地图） | 旧门禁用过的不可达图作负控（`resource-reachability-check.sh`） | `evidence/t62-map-matrix/<map>/camera.txt` | B |
| C14 | 地图材质/覆盖矩阵：fetch 不截断、distinct≥tiled、计划无畸形/重复、tile 行数与计划一致、atlas 已绑、捕获 tick 与请求一致 | `native/src/material_slot_resolution.h:90`、`native/src/vmt_material.cpp:104/:119/:199`、`native/src/vtf_texture.cpp:45/:190/:266` | 五图（含 Odyssey 历史行） | `map-material-matrix.sh:36`（不变量）、`:624`（PASS，maps=5 rows=10 unsupported=0 failed=0） | 像素列"只记录不断言"（`MAP-MATRIX.md:36`） | `evidence/t62-map-matrix/` | A |
| C15 | 几何面账：分区两次一致、天空 brush 多边形存在、每个被读 lightmap 样本都被写出、空集非空、四张 flat 捕获嵌套、cull/PVS 移除像素归因、点 oracle | `native/src/bsp_map.cpp:587`（lightmap 采样） | 五图两姿势 | `map-geometry-lighting-check.sh:38`（断言集合）、`:939`（PASS，maps=5 geometry=5 lighting=5 failed=0） | 四张 `--world-flat --no-world-sky` 捕获 + 点 oracle | `evidence/t62-map-matrix/` | A |
| C16 | 世界帧字节 pin（每图每姿势 10 个，共 100 个），tick-2000 与历史证据一致；tick-0 刻意不 pin | `native/src/main.cpp` 帧捕获路径 | 五图两姿势 | `world-geometry-hole-check.sh:178-179`（帧字节 pin，断言在 `:712-717`） | cull 控制（onlyInCull 0–4 px，门禁拒 >0.1%） | `evidence/t62-map-matrix/`（100 hash） | B |
| C17 | 世界材质与光照控制：亮度/暗象/材质类别语义可分辨 | `native/src/world_lighting.cpp:25/:33` | 五图 | `world-material-check.sh`（六控制）、`lighting-semantics-check.sh`（98 断言）、`lighting-dark-account-check.sh`（暗像素账） | 各项控制互为负向 | 各门禁日志 | B |
| C18 | 渲染可靠性：后缀顺序、方向顺序、路径解析、逐面；**不证明**与 Source 像素一致 | `native/src/bsp_map.cpp` 天空面路径 | sunshine 输入 + snakewater 控制 | `world-render-reliability-check.sh:48-52`（接受边界原文）、3/4 六面天空 | 变异 `fix_deleted`、`sky_suffixes_swapped` | 该门禁日志 | A |

## 5. 音频 / HUD / UI（C19–C22）

| ID | 支持声明 | 产品路径 | 输入夹具 | 门禁 + 断言/判定行 | 变异 / 负控 | 证据目录 | 级 |
|---|---|---|---|---|---|---|---|
| C19 | 音频：事件/音轨解析、时间线普查、内容图、null sink 下产品运行；**device 6 可听验收只有在 `--device` 时执行** | `native/src/audio_player.cpp:98`（`play`）/`:170`（`stop`）、`native/src/demo_header_sound.cpp:23`（`readSounds`）、`native/src/audio_resource.cpp:29` | 钉住的录制（`verify-audio.sh` 头） | `verify-audio.sh:1275`（MUTATION）、`:1280`（PASS）；`verify-audio-content.sh:566`（MP3 内容 PASS） | 期望列表全可红；负 fixture 具名拒绝 | 两门禁日志 | B |
| C20 | HUD：导入/拒绝/绘制、三热键、HUD 内外像素仅区域内变化、缩放规则、坏 HUD 拒绝不半应用。**产品只画彩色矩形，不画字也不画图**：`main_hud.cpp:247-250` 注释自述 "this build rasterises no font and no image"；唯一 Text 原语调用点在 `main_hud.cpp:210` 的 `--hud-text-test` 分支（开关 `main.cpp:2514-2517`），`hud-text-check.sh:284` 正是用这个 flag 跑 | `native/src/hud_state.cpp:16-20`（键）、`native/src/hud_import.cpp:447/:457`、`native/src/main_hud.cpp:243/:247-250`、`native_renderer_frame.cpp:1171`（渲染器文本能力存在，正常路径无调用者） | HUD fixture 文档 | `hud-check.sh:388`（`--hud-apply --hud-on`：矩形产品路径）、`:1392`（MUTATION）、`:1402`（PASS）；`hud-text-check.sh:658`（**测试路径** PASS，非产品 HUD） | 四个变异 + 缩放/坏文档负控 | 各门禁日志 | B |
| C21 | 小地图：**仅探针逻辑**（世界↔小地图像素映射、命中测试、不可用原因可读）。产品内无小地图通路：`src/minimap.cpp` 只链进 `minimap_probe`（`native/cmake/tf2-probes-minimap.cmake:23`），产品 target 源清单无此文件，`main.cpp` 无 `minimap` 字样；产品侧只有 `review_workspace.cpp:367-375` 的 `workspace.minimap` 状态持久化 | `native/src/minimap.cpp:13/:77/:89/:127`、`native/src/review_workspace.cpp:367-375`、`native/cmake/tf2-probes-minimap.cmake:23` | 纯逻辑夹具（探针） | `minimap-check.sh:36`（`PROBE=minimap_probe.exe`）、`:314`（`MINIMAP-CHECK=PASS`；该脚本无 `===` 段头、无 `assert_*`） | 自身负向分支 | 该门禁日志 | B |
| C22 | logs.tf 面板：内容/realtime 可用性判定；**面板 PASS ≠ 页面已显示** | `native/src/logstf_webview.cpp:18/:28/:45` | `logstf-check.sh` 夹具 | `logstf-check.sh:1081`（MUTATION）、`:1086`（PASS）、`:397`（边界警告原文） | 九个变异族（m1–m9） | 该门禁日志 | B |

## 6. 相机 / 播放 / 事件 / 安装（C23–C29）

| ID | 支持声明 | 产品路径 | 输入夹具 | 门禁 + 断言/判定行 | 变异 / 负控 | 证据目录 | 级 |
|---|---|---|---|---|---|---|---|
| C23 | 相机：T4 模式/观察者/HUD/布局 + 暂停冻结/书签/键位冲突/T50 | `native/src/camera_director.cpp:91/:120/:127/:148`、`native/src/camera_bookmarks.cpp` | `camera_probe` 夹具 + 产品跑 | `camera-ui-check.sh:1208`（T50 MUTATION）、`:1212`（PASS，90 断言） | 六捕获 + 两产品变异；四 T50 缺陷必须变红 | 该门禁日志 | B |
| C24 | SourceTV 死亡接力：三次死亡/三次交接、无空帧、尸体移除、暂停中交接继续、seek 只算一次、16x 是播放不是 seek | `native/src/spectate_target.cpp:64`（`update`）、`:277`（`refreshDead`） | STV demo | `spectate-check.sh:620`（PASS，91 断言）、`:227`（replay 段）、`:232`（demo 判定） | 11 段含具名负控与 pause/seek 对照 | 该门禁日志 | B |
| C25 | 播放控制：逐帧精确、1s/5s 跳、速度、seek 中断、不可达 tick 的落点映射、缺 demo 负控 | `native/src/playback_control.cpp:14`（`isTickMove`）/`:23`（`ticksForSeconds`）/`:28`（`setDemo`） | 两 demo | `playback-control-check.sh:507`（MUTATION 4/4）、`:516`（PASS，95 断言） | 4/4 变异变红并恢复字节一致 | 该门禁日志 | B |
| C26 | demo 时钟：同一 demo 两个 tick 空间、流起点两极性、中点快照、视角采样顺序、相机眼=入口 tick 视角、修复前原点负控 | `native/src/demo_header_strtab.cpp:449`（资产引用）等 | 两 demo | `demo-clock-check.sh`（65 断言，A0–A6 段；变异判定 `:513`，4/4） | A6 就是"修复前原点"负控 | 该门禁日志 | A |
| C27 | svc_GameEvent / svc_GameEventList 解码：body 解析、事件 tick 来自 signon、去重键 | `native/src/demo_header_events.cpp:47`、`native/src/effects_timeline.cpp:42` | `--events 1` 夹具 | `game-event-check.sh`（`MUTATION-MODE=PASS (3/3)`） | m1/m2/m3 三变异 | 该门禁日志 | B |
| C28 | 安装生命周期/就绪：安装、升级/降级、卸载保留用户数据、WebView2 缺失路径 | `native/src/install_manifest.cpp:47/:78/:263`、`native/src/install_lifecycle.cpp:58` | 安装夹具 | `install-lifecycle-check.sh:817`（PASS）、`:1092`（4 变异）、`:446`（**运行中镜像检查是具名递延，不是 PASS**）；`install-readiness-check.sh:596`（PASS）、`:836`（3 变异） | 4 + 3 个变异 | 各门禁日志 | B |
| C29 | 资源可达性：main.cpp 命名的归档都存在或有具名原因；demo 地图 BSP 可达；"可解析≠可绘制"，512 上限是被测量的 | `native/src/main.cpp` 归档表 | demo 地图 | `resource-reachability-check.sh`（5 段，含 512 cap 反事实） | 旧门禁用过的不可达图负控 | 该门禁日志 | B |

## 7. 没有任何门禁覆盖的支持声明（硬清单）

以下**不存在**可引用的门禁断言。它们要么按政策不做，要么是纯文档结论，要么完全没有实现。任何把它们写成"已支持/已验证"的文字都是越界。

| # | 声明 | 现实 | 出处 |
|---|---|---|---|
| N01 | ragdoll 渲染 | 明确不做；`ragdoll` 在 99 个脚本里只出现在 `observer-focus-check.sh:61/:90/:283/:289/:291`（`FL_TRANSRAGDOLL=0x40000000` 死亡相机标志，不是 ragdoll 渲染） | 支持边界 B1；`CHARACTER-MATRIX.md:343` |
| N02 | 水面反射/折射 | 明确不做；`world-material-effects-check.sh:16-21` 只有 warp 三属性 + 像素 on≠off | 支持边界 B3；handoff :136 |
| N03 | T55 视频切成/导出 | UNSUPPORTED | `CHARACTER-MATRIX.md:344` |
| N04 | LOD1+ 层 | 只建 LOD0；`lod[1-9]` 在 99 脚本里 **0 命中**；`vvdLods=7/vtxLods=7` 只是被计数 | `CHARACTER-MATRIX.md:131`；`model_loader.cpp:1568` |
| N05 | 动态 brush（门/移动平台/可破坏体）按 demo 时间更新 | **产品与门禁双侧 0 命中**：`func_door|func_movelinear|track_train|func_breakable|m_vecMoveDir` 在 `native/src`+`native/include` 与 99 脚本里都搜不到。这是 handoff 8.2 明列的"不能用'不支持'改名结案"的功能欠账 | handoff §8.2；本次 grep |
| N06 | 3D skybox 与天空分离 | `3d skybox|_skybox` 门禁 **0 命中**；只有六面 `--world-sky`/`--no-world-sky` | handoff §8.2；`world-render-reliability-check.sh` 3/4 |
| N07 | 解码光照值（LDR/HDR luxel/atlas 数值） | 无 pin，`MAP-MATRIX.md:183` 明写 OPEN | `MAP-MATRIX.md:183/:189-192` |
| N08 | 真实设备可听音频 | `verify-audio.sh:1280` 的 device-6 可听段**只有 `--device` 才跑**；默认 null sink | `verify-audio.sh` 第 6/6 段 |
| N09 | 每槽位（主/副/近战）武器覆盖 | 每职业只测一把（Spy 持 `c_knife`）；无槽位表 | `RUNS §26.6`；`CHARACTER-MATRIX.md:398` |
| N10 | 服务器 `m_nSequence`/`m_nModelIndex` 还原、driven attack | 采样实体不带这些字段；所有帧按 `missing` 拒绝画 bind pose | `CHARACTER-MATRIX.md:396`；`main.cpp:2119-2132` |
| N11 | D2 第二具身体/第二支队伍 | 每职业一具身体一次采样 | `T62-REPORT.md:102` |
| N12 | 玩家视角的第三人称采集 | 未采集；相机是 `--debug-camera` | `CHARACTER-MATRIX.md:390` |
| N13 | UI 能力表（支持/实验/不支持） | `grep` 无此文档；`logstf-check.sh:397` 只是警告"面板 PASS ≠ 页面显示" | handoff §8.6 |
| N14 | 安装后运行中镜像检查 | 具名递延，不是 PASS | `install-lifecycle-check.sh:446` |
| N15 | 一次可复现的"典型用户流"（装→demo→TF2 根→地图→播放/seek→设置→重启） | 无单一端到端门禁；`restart` 无播放/回放门禁命中 | handoff §8.6 |
| N16 | 与 Source 渲染器的像素一致性 | 明确不承诺 | 支持边界 B6；`world-render-reliability-check.sh:48-52` |
| N17 | 产品内小地图（绘制/投影/命中/交互） | **无通路**：`src/minimap.cpp` 只链 `minimap_probe`（`native/cmake/tf2-probes-minimap.cmake:23`），不在产品源清单（`tf2-product.cmake:16`），`main.cpp` 中 `minimap` 零命中；产品侧仅 `review_workspace.cpp:367-375` 持久化 `workspace.minimap` 状态 | 本次 grep + CMake 复核（workflow-auditor 结论成立）；C21 只覆盖探针 |
| N18 | 产品 HUD 的文本/字体/图像 | **无通路**：正常路径只推彩色矩形（`main_hud.cpp:247-250` 自述 "this build rasterises no font and no image"）；唯一 Text 原语调用点在 `main_hud.cpp:210` 的 `--hud-text-test` 分支（开关 `main.cpp:2514-2517`），`hud-text-check.sh:284` 正是用该 flag | 本次复核（workflow-auditor 结论成立）；渲染器文本能力在 `native_renderer_frame.cpp:1171` 存在但无默认调用者 |

| N19 | "官方四图都已验证角色轴/播放" | **无门禁**：角色轴只在 snakewater（默认 demo：`class-pixels-check.sh:102`、`player-body-appearance-check.sh:42`、`third-person-weapon-check.sh:85`）与 upward（队伍对夹具）取证；sunshine 无九职业齐样本（demos.tf 最好 8/9）、rc12 本地 0 条 demo（demos.tf 15 条候选、0 条 9/9，最好 6/9） | task-6 复核 task-3 索引：`D:/_shared/t85-assets/demo-candidates.csv`、`local-demo-index.csv`；支持边界 §3 表末列；DEBT D-17 |

## 8. 覆盖统计（据本表）

- 有门禁可引用的支持声明：**C01–C29 共 29 条**；其中等级 A 8 条（C01/C05/C08/C09/C14/C15/C18/C26）、B 19 条、C 2 条（C02 为探针计数、C07 为矩阵读数，均无门禁断言）。
- 明确无门禁的声明：**N01–N19 共 19 条**（N17/N18 是 task-6 复核 workflow-auditor 结论后新增的"产品内无通路"两条；N19 是 task-6 按地图夹具覆盖度新增的外推闸）。
- 关键结构性事实：**"45/45 PASS"是矩阵读数的合成**——45 格里只有 18 格绑定了具名渲染实体（`CHARACTER-MATRIX.md:375`），其余靠静态/结构断言与文档推理。引用 45/45 时必须同时引这一条，否则就是外推。

## 9. 复核用的哈希与命令（只读）

```
# 冻结树
cd /d/TF2_Native_Worktrees/t79-integration
git rev-parse HEAD            # e15577b4f0550870bb11672a04497c038c9d88b2
git status --porcelain | wc -l # 0
sha256sum native/build-nmake/tf2_demo_native.exe
# a3c93d5755fd331351766dfb991fe17f90c4dfb753ad7ef9f53391f0303724e9  2,763,264 B

# 文档（本仓库内的权威读数）
sha256sum evidence/t62-matrix/CHARACTER-MATRIX.md   # 81028863238a7bb5c1f000f1939694c73e719837c612b96648c730bbd3ac9ecc
sha256sum evidence/t62-matrix/MAP-MATRIX.md         # 1caa6829d8bd2b207d357f753bce65bdd74b3aec1ed8cd43c63a7469d5ceba17
sha256sum evidence/t62-matrix/T62-REPORT.md         # a4d52b530b1492dcd5dd8b482d106b60401f7017dee0488d0f5e9da5e3d217e4
sha256sum evidence/t62-matrix/RUNS-2026-11-04.md    # b5d4e100316d32c3eaa401c5dd3e5e4dd7c13a39d4402c8bb18742ef0bc8e1d5
sha256sum docs/RELEASE-POLICY.md                    # c1fdb92923992ccc21c1c0becf2a27ba712b8ffea81af8c313f73f60adb2ffa3
sha256sum docs/KNOWN-ISSUES-AND-FIXES.md            # 2e6413737601e75c90cd79449fb718c8ee00f12ec25ccfe3b8ee452b859d8cb9
```

> **哈希口径（2026-11-07 task-6 校正）**：上表一律是**工作树字节**的 sha256（这些文档在工作树里是 CRLF；`git show HEAD:…` 得到的是 LF blob，值不同）。task-6 重算后的变化：`CHARACTER-MATRIX.md` 与 `T62-REPORT.md` 因加"取代注"而变（cb5bfcd 时记录为 `6fa7df9f…` / `653b4952…`，均已作废）；`MAP-MATRIX.md` 在 cb5bfcd 记录为 `7fa92339…` 是 **git blob（LF）值**，与工作树字节 `1caa6829…` 不同，此前记录口径不一致，此处按工作树值改正。`RUNS-2026-11-04.md`/`RELEASE-POLICY.md`/`KNOWN-ISSUES-AND-FIXES.md` 本次复核未变。

门禁脚本 sha256（本次实测，`e15577b` 工作树干净状态下计算）：

```
a99d067bf9dd6315ddc26c80676cb7581aaf50c5307e2a17bb33b17f60dd3c9a  player-model-check.sh
8cc3e77c7b6c38a918bd7457a7b9acdbed78dfa0cb911c97e252ba90463111c3  player-animation-check.sh
15c65d0f5126a4dc7b6130ac00aba69731280b6dfb63e6dd9d60a911b5a2389b  class-pixels-check.sh
393a93fc69affc981a8fad2632751fb7b57cfee55cb3a7996cb0e8de75849f1a  player-body-appearance-check.sh
5de35d101f41e5bec08e2cf947820871e43b7414e2bb910c14844edc59453d94  third-person-weapon-check.sh
252ff1591cde1343bbd1527623429c93dff3d0cd9ec3090ecd76c398759921ec  animation-drive-states-check.sh
b97822f73192eac905449cdfe958581c37cbeadf1bcc186c7808f81ec64f1ea2  map-material-matrix.sh
73715dd9de6a4ce8f79e883d1e75cd8899fc4c3f6d3415d8a105e1654c0fb145  map-geometry-lighting-check.sh
e33e06004b0c835e7eb50472b22da988e247892a3feb7d71badae7ede0aed07f  bone-mismatch-semantics-check.sh
be9432a81662381fb7098d2f074ad4c2d897c6db21208a60f2aa99db274ace28  weapon-animation-check.sh
4ed94d0217dd0d3f3e8b872a6494ef80e45fbc871fa43d0deb7ddd5472f788bd  viewmodel-apply-check.sh
e15ba2789542545f83314f0c0c9083e32d0c5f641acc1139af62868daaafa7dc  viewmodel-animation-check.sh
d8c21ea2bbc5882265d4768b7603a9d02b2903a31dc0c6fdf0fbe4f2b0f65595  verify-audio.sh
d905c9aae0cf5eb7f471cfd98bd9fa97009325a5c61cd9ef0c776e4cd73fa0a2  hud-check.sh
b27404f1c15a2c60a6fa18ac9903556a8ac96120281996532b61fa8c67e5e977  camera-ui-check.sh
ada0674151c0d3f9153f5e0cc9a4a7b8c99c0565978339f999b324efdfe9d223  spectate-check.sh
5e52b63ced34bae8f7bd91064dcff0c5688e7bd48cbe57a2f79141f7605562f8  playback-control-check.sh
cfecbe670beff09c77578d0435cdaba055e33ea6b29b66d3eb66f25cdda632b5  demo-clock-check.sh
e99c9908102d4707102852359d2da2b7c42ff78a519ea54fd3ef0e72da0ff33f  game-event-check.sh
fdae6526bc474340ea140000f3841d462c7290d1b76b1e9d587a0b99c33e0566  minimap-check.sh
4d95315f6a28db915541835e7e35c99f67052148219319297470e089e7f60215  logstf-check.sh
5b39bcc6db4ae50b0be9b3b14859cd2579df4999526eb4b53294c0a9c18b7f07  install-lifecycle-check.sh
64c7046f26bd6fde68dc2c7374330fc376919d88647e22211cb9ea6f31835313  install-readiness-check.sh
a000188e7c7e5217b5b2c8d39d5f43ef8636a32c4978ea414a56a9a14bdf8fa3  world-material-effects-check.sh
d8a1c7e43534ca68daf8ce2ebc4fa7b80d21c4b1e43d08334b61aef913342b66  world-geometry-hole-check.sh
88d68ece4788e2c1924003147fc8f06d945328246a71d363202eba75a4e6f43c  outline-check.sh
69d2967b77ed0f1177454a066624048c58344c2a38145063f681ebde026fe3fd  resource-reachability-check.sh
180be8a64542432715655ad74d62bc78ba25c5cefefbfef3f07c963709805fc8  verify-fast.sh
7e28c987738a15fb32564311ed71ae0c675c0cd4d689e05e86b92f2945082d20  verify-all.sh
```

**未做（阶段 0 明确不做）**：重跑任何门禁、构建、启动产品、抓帧。上表 PASS 的重现需要持有 host lock 并按上表命令行执行；执行者必须自己记录 exe/gate/demo/BSP 的 sha256、rc、断言数、hostlock owner 与锁结束状态。

## 10. 已知的引用陷阱

1. `CHARACTER-MATRIX.md:438`（§6"Still missing"）与 `T62-REPORT.md:124` 原为**过期段落**（40/5、"缺 Scout/Sniper 对"）；已于 2026-11-07 task-6 各加一块取代注并**保留历史数字**。引用矩阵结论仍用 `:362`/`:375`/`:379`；文档历史与现状的权威口径见 `POSTFREEZE-INVENTORY-CORRECTED.md` §3。
2. `MAP-MATRIX.md:3` 的第五行是 rc13→rc11 的**文档换行**，"No gate was re-run for the swap"；该行的读数是既有的，换行本身不是重跑证据。
3. `minimap-check.sh` 与 `resource-discovery-check.sh` 没有 `===` 段头和 `assert_*`，不能用"grep assert_ 数量"评估其强度。
4. `logstf-check.sh:397`：面板内容 PASS 不等于"页面已显示"。
5. `install-lifecycle-check.sh:446`：运行中镜像检查是具名递延。
6. `verify-audio.sh` 的 device-6 可听段默认跳过——引用"音频可听"必须写清是否带 `--device`。
