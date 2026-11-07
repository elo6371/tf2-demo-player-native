# T85-DEBT-LIST — 交接书 §8 欠账清单（阶段 0 冻结版）

基线：`e15577b4f055…`；产品 exe sha256 `a3c93d57…724e9`。
源清单：`D:/_shared/PROJECT-HANDOFF-2026-11-07.md` §8（`:188-250`），共 **4+9+5+6+5+6+4 = 39** 个复选框。
配套：`docs/T85-SUPPORT-SCOPE.md`（边界）、`docs/T85-COVERAGE-MANIFEST.md`（C01–C29 / N01–N16）、`evidence/t62-matrix/POSTFREEZE-INVENTORY-CORRECTED.md`（账本对账）。

## 0. 状态判据（先说清，否则表没法读）

| 状态 | 判据 |
|---|---|
| **已覆盖** | 有门禁/文档对**这一条要求的全部内容**给出可引用断言或可引用冻结文本；不是"相关领域有门禁"。 |
| **部分覆盖** | 有门禁覆盖了其中一部分，缺的那部分必须在本表"缺什么"列点名；只有"部分"两字不算。 |
| **无覆盖** | 没有任何门禁或证据触及；产品侧也可能根本没实现。 |
| **明确不做** | 已在 `T85-SUPPORT-SCOPE.md` 冻结为支持边界之外，且**不再声称**。 |

统计：**已覆盖 4 / 部分覆盖 30 / 无覆盖 4 / 明确不做 1**（合计 39）。
判据的依据是"读过的门禁与文档"，不是重跑结果；阶段 0 不构建、不启动、不抓帧。

## 1. §8.1 播放核心与鲁棒性（4 条）

| 条款 | 要求（压缩） | 状态 | 门禁 / 证据 | 缺什么 |
|---|---|---|---|---|
| 8.1-1 | POV 与 SourceTV 解析，且对 header / string tables / net messages / snapshots+delta / events / tick 时间线做**畸形输入测试** | 部分覆盖 | `demo-clock-check.sh`（65 断言，A0–A6：两个 tick 空间、流起点两极性、中点快照、视角采样顺序、相机眼=入口 tick；A6 是修复前原点负控）；`game-event-check.sh`（3/3 变异）；`spectate-check.sh:620`（STV 三次死亡/交接）；`playback-control-check.sh` 的缺 demo 负控 | **没有系统化的畸形输入矩阵**：截断 string table、坏 net message 类型、未知快照 delta 的具名错误路径，未被任一脚本枚举。见 N-类第 3 条 |
| 8.1-2 | play/pause/resume/speed/jump-tick/scrub/replay+restart 一致，且 seek 时有**显式复位规则**（相机、实体插值、骨骼、音频、HUD） | 部分覆盖 | `playback-control-check.sh:507/:516`（95 断言：A 逐帧精确、B 1s/5s 跳不改变 transport、C 速度、D seek 中断、E 不可达 tick 落点、F 缺 demo 负控、G 产品接线；4/4 变异）；`camera-ui-check.sh`（暂停冻结）；`spectate-check.sh`（seek 只算一次、16x 是播放不是 seek） | **restart/replay 无门禁**；**seek 复位规则对音频与 HUD 无断言**（相机有、骨骼相位靠相位钉间接、音频无）；`restart` 在 99 脚本里无播放/回放命中 |
| 8.1-3 | 截断/损坏/未知协议/不完整 demo → **定位明确、可理解的错误**，不崩、不挂、不静默画错帧 | 部分覆盖 | `frame-capture-check.sh`（坏 tick 被拒绝而不是猜）；`playback-control-check.sh` F 段；`player-model-check.sh:186`（坏 demo：`player-model-audit FAIL the demo header did not parse`，rc=2） | 未找到"损坏/截断 demo 全集"的负控矩阵；"不静默画错帧"没有直接断言（只有帧字节 pin 间接） |
| 8.1-4 | 固定播放的确定性，且动画相位/时钟/线程差异被**量化解释**；禁止用 "PHASE VARIANCE" 当一般豁免 | 部分覆盖 | 相位钉住：`class-pixels-check.sh:60-70`、`third-person-weapon-check.sh:457-461`（`maxDeltaCycle` 0.84 vs 9.32、2,129 px 跨相位 vs 169 px 同相位）；identity 双捕获 byte-identical（`CHARACTER-MATRIX.md:317-321`） | **线程/长期运行方差无量化**；没有"相位以外的不确定输出"清单；4x/64x 速度被判"不是合法证据速度"（`RUNS §26.2`）但未进门禁 |

## 2. §8.2 地图与世界（9 条）

| 条款 | 要求（压缩） | 状态 | 门禁 / 证据 | 缺什么 |
|---|---|---|---|---|
| 8.2-1 | 四张官方图 + Upward 夹具 + Odyssey 只作历史 | 已覆盖 | `docs/T85-SUPPORT-SCOPE.md` §3 地图表（含实测大小与 sha256）；`MAP-MATRIX.md:26-34` 五行（第五行 rc11） | — |
| 8.2-2 | 每图 BSP 查找路径 + 哈希（尤其 RC11 来自 `tf/download/maps`），trace 里写 name/source/fallback | 部分覆盖 | `MAP-MATRIX.md:26-34` 记录 rc11 只在 `tf/download/maps`（26,370,688 B / `f06cfe4b…`）；`map-material-matrix.sh:36` 断言 `demo map=`/`server_map=` 等于该行地图；`resource-discovery-check.sh`（查找顺序） | **trace 里 source/fallback 字段无断言**（只断 map 名相同）；BSP sha256 未写入 trace，是文档侧记录 |
| 8.2-3 | 世界几何（含 displacement、sky 面、负控）**+ 正式图可见动态 brush 必须按 demo 时间更新位置/角度**；"不支持"改名不算结案 | 部分覆盖（动态 brush **无覆盖**） | `map-geometry-lighting-check.sh:38/:939`（面账分区、天空 brush 多边形、lightmap 样本、flat 嵌套、cull 归因、点 oracle）；`world-geometry-hole-check.sh:178-179`（帧字节 pin） | **动态 brush 完全没有实现也没有门禁**：`func_door`/`func_movelinear`/`track_train`/`func_breakable`/`m_vecMoveDir` 在产品源码与 99 脚本中 **0 命中**（本次 grep）。这是 §8.2 明文禁止用"不支持"结案的条目 → 见 N05 |
| 8.2-4 | 天空 vs 3D skybox **分开**测（边界、开口、视角变化、PVS、sky 关闭对照） | 部分覆盖 | `world-render-reliability-check.sh` 3/4 六面天空 + `--world-sky`/`--no-world-sky` 对照；`--world-flat --no-world-sky` 四张捕获（`MAP-MATRIX.md:22`） | **3D skybox 分离无覆盖**：`3d skybox|_skybox` 在门禁中 0 命中 → N06 |
| 8.2-5 | 材质：VMT proxy/inherit、VTF+mip、slot、alpha、blend、mask、emissive、detail、bump/normal、lightmap 逐类登记 + 确定性回退与诊断 | 部分覆盖 | `map-material-matrix.sh:36/:624`（fetch/atlas/tile 不变量）；`vmt_material.cpp:104/:119/:199`、`vtf_texture.cpp:45/:190/:266`；`world-material-check.sh`（六控制）、`world-material-effects-check.sh`（warp+三效果）、`material-coverage-check.sh` | **没有"逐材质类别 × 逐特性"的登记矩阵**（哪些 proxy 被支持、哪些回退）没有一份可核对表；emissive/detail/bump 的**逐类**断言散在各门禁，未汇总；确定性回退的具名诊断只覆盖部分路径 |
| 8.2-6 | 水 = 只做当前纹理/颜色，**不做反射/折射** | 已覆盖（范围声明） | `T85-SUPPORT-SCOPE.md` B3；`world-material-effects-check.sh:16-21`（warp 三属性 + 像素 on≠off） | 此为"边界已冻结"，不是"水面正确性已验证"；不承诺 |
| 8.2-7 | 光照需独立 luxel/atlas **数值**采样：LDR/HDR、边界、UV clamp、已知源对比、显式 oracle 与残差 | 部分覆盖 | 帧级：100 个世界帧 pin（`T62-REPORT.md:99-101`）、`lighting-semantics-check.sh`（98 断言）、`lighting-dark-account-check.sh`（暗像素账）、`map-geometry-lighting-check.sh` 的"每个被读样本都写出" | **解码数值无 pin**：`MAP-MATRIX.md:183` 明写 OPEN；没有独立 oracle 与残差数字；HDR exponent（`bsp_map.cpp:621`）无单元级断言 → N07 |
| 8.2-8 | LOD 层可读可建，切换/材质一致/过渡/预算；无 LOD1+ 时**不得**宣称 LOD 完整 | 无覆盖 | 只有"不宣称"这一半做到了：`CHARACTER-MATRIX.md:131`（只建 LOD0）、`T85-SUPPORT-SCOPE.md` B5 | **LOD1+ 未实现、无门禁**（`lod[1-9]` 0 命中）→ N04 |
| 8.2-9 | 逐图矩阵按轴，历史 Odyssey 格**不计入**总数 | 已覆盖 | `MAP-MATRIX.md:26-34/:40`（20 格含 4 格 Odyssey 历史；官方四图 16 格）；`T85-SUPPORT-SCOPE.md` §6 数字口径 | —（引用时必须写"20 含 4 历史格"，本表已固定该口径） |

## 3. §8.3 角色外观（5 条）

| 条款 | 要求（压缩） | 状态 | 门禁 / 证据 | 缺什么 |
|---|---|---|---|---|
| 8.3-1 | 九职业红蓝加载/蒙皮/明暗/绘制；45/45 与 18 身体/9 对是**基线证据，不是回归覆盖** | 部分覆盖 | `player-model-check.sh:213/:294/:184-190`；`player-body-appearance-check.sh:287`；矩阵 `CHARACTER-MATRIX.md:362/:375/:379` | 45/45 是**一次快照**；没有把"18 身体/9 对"变成每次改动的回归门禁；矩阵合成中有 27 格无渲染实体（只有 18 格有） |
| 8.3-2 | 玩家实体身份 vs 职业回退要**分开**（checksum/path/骨骼 rig） | 部分覆盖 | `entity_model.cpp:699/:782`、`entity_overlay.cpp:301`；`CHARACTER-MATRIX.md:42`（36/36、14/14 回退计数）、`:313`（真 Heavy ent=11） | **没有"实体自带 model index → 精确模型"的身份断言**（采样实体不携带该字段）；checksum 比对无门禁 |
| 8.3-3 | 外观范围由 catalogue 界定（基础模型必须；cosmetic 只有被编目且可用才算） | 部分覆盖 | `T85-SUPPORT-SCOPE.md` §4（范围冻结）；`item_schema.cpp`/`pcf_operators.cpp` 存在（文件级，未断言行为） | **没有 cosmetic catalogue 门禁**；没有"被编目且可用"的可核对清单 |
| 8.3-4 | 骨骼映射 / helper-bone / bind pose / 动画普查 + 负控；helper-bone 不匹配不算骨骼失败，但**主要可见骨骼不得静默停在 bind pose** | 部分覆盖 | `bone-mismatch-semantics-check.sh:229`（语义固定）；`animation-availability-check.sh`；`class-pose-family-check.sh`（crouch/flinch 数据解码）；`main.cpp:2119-2132`（bind pose 是**已知默认**） | **helper-bone 普查只覆盖 Demo/Heavy/Scout**（`D:/_shared/t62-p1-bonemismatch/REPORT.md:244-339`，承接旧账）；九职业的 helper-bone 结构检查未做；"主要骨骼静默 bind pose"在产品默认路径上**恰好发生**（`viewModelApplyAnimation=false` + 实体无 sequence），这是设计决定，但未写成每职业清单 |
| 8.3-5 | 容量覆盖目标角色/活动武器/必要 overlay，单一优先级，拒绝诊断；回退三把已知 OOM 武器，并确认提高单一上限没有造成"上传成功但未绘制" | 部分覆盖 | `native_renderer_entity.cpp:168`（96）；`main.cpp:3730`（reserve(64)）；`outline-check.sh:1840-1857`（96 上限变异）；"supply=23 refused=3"（`T62-REPORT.md:10`）；`resource-reachability-check.sh`（512 cap 是测量的） | **P0 已知缺陷仍在**：`c_sapper_xmas`/`c_shogun_warhorn`/`c_flamethrower` 走模型/实例预算链失败，交接书明确"不能只提高单个 `kEntityModelMeshLimit`"——属 **task-1（t85-budget）** 范围；"上传成功但未绘制"的检查只有 outline-check 的 59=59 个例 |

## 4. §8.4 动作矩阵（6 条）

| 条款 | 要求（压缩） | 状态 | 门禁 / 证据 | 缺什么 |
|---|---|---|---|---|
| 8.4-1 | 九职业动作矩阵 ≥ idle/walk-run/turn/crouch/crouchwalk/jump/air-fall/land/flinch/death-respawn/taunt/attack，每格写来源/规则/可见证据/限制 | 部分覆盖 | `CHARACTER-MATRIX.md` §3 动作轴表（idle/run PASS，walk/airwalk/reload NV，attack 数据 PASS/驱动 NV，crouch+flinch opt-in，ragdoll UNSUPPORTED） | 矩阵里 **walk、turn、jump、air-fall、land、death-respawn 大面积 NOT VERIFIED**；每职业一具身体一次采样 → N11 |
| 8.4-2 | 选输入前先验证 demo 可用字段与实体关联；**有限样本缺 sequence/cycle/model-index 不能证明所有 Source demo 都没有** | 部分覆盖 | `CHARACTER-MATRIX.md:396`（`status=missing`）；`main.cpp:2119-2132`（pinned recordings 上 sequence 1/29/30 不落 descriptor） | 只有**少量录音**的观测；没有跨更多 demo 类型的字段普查，因此"所有 demo 都没有"这一更强结论**不成立**（本表不写它） |
| 8.4-3 | 区分"事件推断动画"与"服务器姿态还原"，每种回退给可靠性 | 部分覆盖 | `demo_header_events.cpp:47`、`effects_timeline.cpp:42`（事件→固定一枪）；`T62-REPORT.md:10`（C 轴挂点 0.00 drift） | 无可靠性分级表；攻击事件不选序列（`main.cpp` 无映射） |
| 8.4-4 | 时钟可重复：暂停/seek/后退/变速/长播/快速切换；固定 tick 复现要控制 seek 历史与相位 | 部分覆盖 | `playback-control-check.sh`（95 断言）；`camera-ui-check.sh`（暂停冻结）；`spectate-check.sh`（seek/16x）；`class-pixels-check.sh:60-70`（相位钉） | **后退播放**与**长播**无门禁；"固定 tick 复现控制 seek 历史"只在部分门禁里隐式做到 |
| 8.4-5 | 每个动作都要可见证据：同相机 idle/动作对比证明像素或骨骼变化 + 一个**必须不变**的对照；parser 解码/开关接通不算验收 | 部分覆盖 | `class-pixels-check.sh`（每职业 drive on/off 同帧 + identity 双捕获）；`player-animation-pixels.sh`（F 段）；`animation-drive-states-check.sh:446`（crouch/flinch） | 有可见证据的动作只有 **D2 九格 + crouch/crouchwalk/flinch**；walk/jump/land/attack 没有 → 与 8.4-1 同缺口 |
| 8.4-6 | 不可靠动作要先有**约定政策**才能变成已知限制 | 已覆盖（政策侧） | `docs/T85-SUPPORT-SCOPE.md` §4/§5：`walk/airwalk/reload` NOT VERIFIED、`attack` 只有数据 PASS、`ragdoll` 不做、`T55 导出` UNSUPPORTED 均写成边界而不是"支持" | 政策已冻结；动作本身仍未验证（不冲突） |

## 5. §8.5 武器（5 条）

| 条款 | 要求（压缩） | 状态 | 门禁 / 证据 | 缺什么 |
|---|---|---|---|---|
| 8.5-1 | 九职业第三人称基础武器与持枪点可追（模型、归属、骨骼/挂点变换、队/皮肤、PVS、遮挡、动画）；活动武器不得消失/漂浮/错位 | 部分覆盖 | `third-person-weapon-check.sh:457-461/:580/:582`（每职业一帧、drift 0.00、attached≠off、off=基线）；`docs/THIRD-PERSON-WEAPON.md:1-6`（链路文档） | **PVS/遮挡**未断言；每职业只一把、一个姿势（`CHARACTER-MATRIX.md:398`） |
| 8.5-2 | 每职业每槽位库存武器（主/副/近战）覆盖表；**不能以每职业一种武器代表所有槽位** | 无覆盖 | 现状：Spy 持 `c_knife`（`RUNS §26.6`），其余职业各一把 | 没有槽位轴；没有覆盖表 → N09 |
| 8.5-3 | 有动画序列就用，否则记录资源事实与回退；**不以人物身体动画 PASS 代替武器动画 PASS** | 部分覆盖 | `weapon-animation-check.sh:1122`（GPU 蒙皮/光标+姿势随 tick 前进/矩阵序变异）；`T85-SUPPORT-SCOPE.md` §5 明确"身体 PASS ≠ 武器动画 PASS" | 第三人称武器**序列**是否被使用未登记；"资源事实与回退"没有逐武器记录 |
| 8.5-4 | 第一人称 ViewModel 手臂/武器 idle/move/fire/reload/switch/jump；**opt-in 与默认分开记录，默认不能因加门禁而改变** | 部分覆盖 | `viewmodel-apply-check.sh:818`（33 断言，opt-in）；`viewmodel-contract-check.sh`（127 断言）；`viewmodel-animation-check.sh:1169`；默认 `main.cpp:2117`（false，未变） | fire/reload/switch/jump 的 **ViewModel 动作**都没有门禁；只有 applied pose 一条路径 |
| 8.5-5 | 身体+武器同帧挂接；越界/缺模型给明确原因；预算不得为了非目标预载牺牲可见目标 | 部分覆盖 | `T62-REPORT.md:10`（supply=23 refused=3，`0x8007000E` 具名拒绝）；`outline-check.sh:1840-1857`；`main.cpp:3695/:3730` | 预算"非目标预载挤占可见目标"的**优先级断言**没有；三把 OOM 武器是 task-1 的 P0（未修） |

## 6. §8.6 音频 / HUD / 相机 / UI / 安装流（6 条）

| 条款 | 要求（压缩） | 状态 | 门禁 / 证据 | 缺什么 |
|---|---|---|---|---|
| 8.6-1 | 音频：解析、play/pause/seek 同步、音量与设备失败、资源释放、**枚举声明的格式**并点名未列出的 | 部分覆盖 | `verify-audio.sh:1280`（六段 + 变异）、`verify-audio-content.sh:566`（MP3 内容）；`audio_player.cpp:98/:170`、`demo_header_sound.cpp:23` | **真实设备可听**只有带 `--device` 才测（默认跳过，N08）；**格式表**没有（哪些格式支持、哪些不）；seek 与音频同步无断言 |
| 8.6-2 | HUD：计时/比分/玩家状态/事件，按 POV/STV 数据源，缺字段有明确状态，不显示过期帧 | 部分覆盖 | `hud-check.sh:388`（`--hud-apply --hud-on`，产品矩形路径）、`:1402`（导入/拒绝/绘制/区域外不变/缩放/坏文档 PASS）；`hud-text-check.sh:658`（**测试路径**：`:284` 用 `--hud-text-test`）；`hud_state.cpp:16-20` | **产品 HUD 不画字**：`main_hud.cpp:247-250` 自述 "this build rasterises no font and no image"，唯一 Text 原语在 `main_hud.cpp:210` 的 `--hud-text-test` 分支（开关 `main.cpp:2514-2517`）。因此计时/比分/状态/事件的**字段显示本身不存在**，逐字段断言无从谈起；"不显示过期帧"只有区域外像素不变这一条间接证据 → N18 |
| 8.6-3 | 相机：默认轨道/SourceTV 观察者/跟随/切目标/debug/越界与无观察者回退；**trace 要描述真正喂给绘制的相机**，姿势↔像素可连 | 部分覆盖 | `camera-ui-check.sh:1208/:1212`（T4+T50，90 断言）；`spectate-check.sh:620`（三次交接、暂停/seek）；`camera_director.cpp:91/:120/:127/:148` | **"trace 描述真正喂给绘制的相机"没有专门断言**（`demo-clock-check.sh` A5 只覆盖 POV 入口 tick 视角）；越界/无观察者回退无具名负控 |
| 8.6-4 | UI 能力表（打开 demo/播放/速度/跳 tick/相机/目标/HUD/书签/小地图/logstf+WebView2）标注支持/实验/不支持；**可见控件 ≠ 可用功能** | 无覆盖 | 只有零散能力：`minimap-check.sh:314`（**仅探针**，`native/cmake/tf2-probes-minimap.cmake:23`）、`logstf-check.sh:1086`、`camera-ui-check.sh:1212`、`hud-check.sh:1402` | **没有能力表文档**；也没有"控件 ↔ 功能"一致性的门禁 → N13。且两条被点名的 UI 能力比表格更弱：**小地图在产品内无通路**（N17）、**产品 HUD 不渲染文字**（N18） |
| 8.6-5 | 典型流：安装→demo→TF2 根→地图→播放/暂停/seek→设置→重启，加上坏 demo/缺地图/缺模型/坏路径/WebView2/权限失败 | 部分覆盖 | `install-readiness-check.sh:596`、`install-lifecycle-check.sh:817`（含 WebView2 缺失路径）；坏 demo/坏 tick 负控散在 `player-model-check.sh:184-190`、`frame-capture-check.sh` | **单一端到端流无门禁**；**重启**没有播放侧断言（只有安装侧） → N15 |
| 8.6-6 | 导出/截图/日志——只有承诺了才做验收（真实输出、路径、覆盖、可读、失败信息） | 明确不做 | `T85-SUPPORT-SCOPE.md` B4：视频导出 UNSUPPORTED，不承诺；截图/日志不在冻结承诺内 | —（若将来承诺，需重新立项） |

## 7. §8.7 环境 / 打包 / CI / 文档（4 条）

| 条款 | 要求（压缩） | 状态 | 门禁 / 证据 | 缺什么 |
|---|---|---|---|---|
| 8.7-1 | 支持环境：Windows 版本、GPU/驱动下限、D3D11、内容发现、WebView2、磁盘、权限 | 无覆盖 | 无文档；只有门禁运行环境事实（`env-msvc.sh`：MSVC 14.44.35207 + SDK 10.0.26100.0） | 没有"支持环境"文档，也没有环境探测门禁 → 无覆盖 |
| 8.7-2 | 干净配置构建 + 安装/便携包（manifest、依赖来源、逐文件 SHA256、卸载/升级、用户数据保留） | 部分覆盖 | `install_manifest.cpp:47/:78/:263`（sha256 + 写 manifest）；`install-lifecycle-check.sh:817/:1092`（4 变异，升级/降级/卸载保留数据）；`install-readiness-check.sh:596` | **便携包**未验证；运行中镜像检查是具名递延（`:446`）→ N14；依赖来源清单无门禁 |
| 8.7-3 | 轻量持续检查（干净 configure/build + 可用的 `verify-fast.sh` 子集 + 脚本语法），`verify-all.sh` 全链按 RELEASE-POLICY | 部分覆盖 | `docs/RELEASE-POLICY.md:1-25`（全链只在冻结树跑一次）；`verify-fast.sh`（257 行，tier 1）；`logstf-syntax-check.sh`（`cl /Zs` 语法检查，**不是构建**） | 没有 CI 配置或"每次改动跑哪个子集"的可执行判定；脚本语法检查只覆盖 logstf 一个 |
| 8.7-4 | 用户文档/README/已知限制/地图与模型目录与矩阵一致；License 保持"本轮不加" | 部分覆盖 | `README.md`、`CHANGELOG.md`、`docs/` 5 篇、`evidence/*/CHARACTER-MATRIX.md` 等存在；`docs/KNOWN-ISSUES-AND-FIXES.md:3` | **一致性未验证**；两处**过期段落**已于 2026-11-07 task-6 加"取代注"（`T62-REPORT.md:124` 的 40/5、`CHARACTER-MATRIX.md:438` 的旧表述，历史数字保留、未删改）→ D-13 关闭；但"文档与矩阵一致"仍无门禁。License 按政策不加 |

## 8. 本阶段新增的欠账（不属于 §8 原始 39 条，但必须记）

| ID | 内容 | 状态 | 说明 |
|---|---|---|---|
| D-13 | 修 `T62-REPORT.md:124` 与 `CHARACTER-MATRIX.md:438` 两处过期段落（正文与同文件 `:10`/`:362` 矛盾） | 已覆盖（2026-11-07 task-6） | task-6 提交（父提交 `cb5bfcd`）在两段后各加一块 `> **Superseded, annotated 2026-11-07 (task-6). History kept, not deleted.**`：保留历史数字，指向 `CHARACTER-MATRIX.md:362/:375/:379` 与 `POSTFREEZE-INVENTORY-CORRECTED.md` §3，并说明行号漂移（旧账本 `:297` 真值 `:313`、`:422` 真值 `:438`）。原文未删除 |
| D-14 | 动态 brush 按 demo 时间位移（8.2-3 的硬缺口） | 无覆盖 | 产品侧 0 实现、门禁侧 0 命中 → N05；交接书明确不许用"不支持"改名结案，因此保留为**功能欠账**而不是边界 |
| D-15 | 产品 HUD 不渲染文字/字体（8.6-2 的硬缺口） | 无覆盖 | `main_hud.cpp:247-250` 自述 "this build rasterises no font and no image"；唯一 Text 原语在 `main_hud.cpp:210`，只在 `--hud-text-test` 下跑（`main.cpp:2514-2517`），`hud-text-check.sh` 全用该 flag → N18。属**功能欠账**：用户看到的计时/比分/状态全是彩色矩形 |
| D-16 | 产品内小地图通路（8.6-4 的范围收窄） | 明确不做（范围收窄） | `src/minimap.cpp` 只链 `minimap_probe`（`native/cmake/tf2-probes-minimap.cmake:23`），不在产品源清单（`tf2-product.cmake:16`）；产品侧仅 `review_workspace.cpp:367-375` 持久化 `workspace.minimap` 状态 → N17。按支持边界记 UNSUPPORTED，不得写成"部分支持" |

| D-17 | 官方地图的**夹具覆盖不均**（角色轴跨图外推风险） | 部分覆盖 | 角色轴门禁默认跑 snakewater（`class-pixels-check.sh:102`、`player-body-appearance-check.sh:42`、`third-person-weapon-check.sh:85`），队伍对夹具在 upward；sunshine 的 demos.tf 候选最好 8/9、rc12 本地 0 条 demo（demos.tf 15 条候选中 0 条九职业齐，最好 6/9）。因此"官方四图"不能被写成"四图角色轴已验证"。数据：`D:/_shared/t85-assets/demo-candidates.csv`、`local-demo-index.csv`（2026-11-07） |

## 9. 跨任务移交（避免重复劳动）

| 事项 | 归属 | 依据 |
|---|---|---|
| 三把 OOM 武器（`c_sapper_xmas`/`c_shogun_warhorn`/`c_flamethrower`）预算链缺陷 | **task-1 / t85-budget** | 交接书 §1 P0；`main.cpp:3730` reserve(64) 与 `native_renderer_entity.cpp:168` 96 的关系是它的主题 |
| opt-in 动画驱动状态层（crouch/crouchwalk/flinch 扩到 walk/jump/attack） | **task-4 / t87-anim** | `native/tools/model_pose_probe.cpp` 补丁（9233fcf + 141 行）冻结待用 |
| 资产/材质逐类登记矩阵（8.2-5） | **task-3 / t85-assets** | `D:/_shared/t85-assets/` |
| 证据封装与门禁模板（8.7-3） | **task-5 / t85-workflow** | `D:/_shared/t85-workflow/` |

## 10. 引用口径（防止下一轮再写出 40/5）

- 角色矩阵：**45 PASS / 0 NV / 0 UNSUPPORTED / 0 FAIL**（`CHARACTER-MATRIX.md:362`），其中 **18 格**有具名渲染实体（`:375`），**9 对**队伍对（`:379`）。
- 地图矩阵：**20 格全 PASS**，含 **4 格 Odyssey 历史行**（`MAP-MATRIX.md:26-34/:40`），官方四图为 16 格。
- 武器：九职业各一格 PASS（`third-person-weapon-check.sh:580-582`），**不是**"九职业全槽位"。
- "40 PASS / 5 UNSUPPORTED" 只属于 pass-7 历史（`T62-REPORT.md:8`），不得作为当前结论；`T62-REPORT.md:124` 的重复是过期文字。
- 任何"已覆盖"必须能指到本表 8.x 行的门禁行号；指不到就降级为"部分覆盖"。
- 地图：授权集 5 行（4 官方 + upward 夹具）；世界轴 20 格只覆盖矩阵里的 5 行（含 4 格 Odyssey 历史行），**角色轴只覆盖 snakewater（默认 demo）与 upward（队伍对）**。引用"官方四图"必须同时给每图的证据级别（`T85-SUPPORT-SCOPE.md` §3 表末列），否则就是把几何/材质证据外推成角色/播放证据（D-17）。
