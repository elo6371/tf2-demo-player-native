# 未完成项现状审计 · 2026-11-22

> 范围：`docs/PROJECT-OVERVIEW-2026-11-22.md` §3.1（7 项）与 §3.2（除"武器本体锚点"外 7 项），对照 `docs/HANDOVER-2026-11-21.md` §6/§7 与 `docs/REFERENCE-REPOS-2026-11-21.md` §0.1。
> 方法：只读（ripgrep / fdfind / file_read），**没有构建、没有运行任何 exe、没有改动 native/ 或任何 *.sh**。本文是本次唯一写入的文件。
> 树：`D:/TF2_Native_Worktrees/t105-merge`（task/merge-all）。下文行号是本次读到的当前行号，不沿用总览里的旧行号。

## 0. 对读记录（REFERENCE-REPOS §0 第 5 条要求）

- **未对读外部源码。** 本次审计只读本仓；下文所有外部入口（`ValveSoftware/source-sdk-2013` 为主，`OthmanAba/TeamFortress2` 只作行为参考）都只给出**路径 + 一句话语义**，**行号一律未核实**，开工时按 §0.1 补齐"库 + 路径 + 文件 + 行号 → 本仓 file:line"。
- 没有引用或改写任何外部代码；按 §0.1 净室做法，落地前先在 `docs/` 写语义说明，再依据该说明实现。

## 1. 总结论（先看这个）

总览 §3 有**一半以上的描述已经过时**——不是"没做"，而是"做了一部分、文档没跟上"。逐项结论：

| 项 | 总览说法 | 现状（本仓行号为证） | 总览是否准确 |
|---|---|---|---|
| N13 观察者方向 | 未实现 | step 1 已落地：`--follow-target/--follow-action select\|next\|prev\|unfollow\|back-to-pov`、`follow:` trace、拒绝分桶；pane1 另有一套。缺的是 step 2（pin 优先级/跨死亡保持/POV 眼位）和"broadcaster（HLTV 自动导播）" | **不准确**（部分实现） |
| N16 分屏/PIP/四视图 | 未实现（只有契约） | 两 pane 左右等分已实现（`--pane-split` 等 7 个开关、独立相机/跟随/ViewModel）；PIP 与四视图确无 | **不准确**（两 pane 已有） |
| N20 火焰/治疗束/流血 | 未实现 | 确实没有任何绘制；数据面只有计数，且 temp entity 保留上限 16384 仍在 | 准确 |
| N27 产品导出 | 未开工 | 产品内 MF 代码 0；只有独立探针 `mf_export_probe` 与设计文档 | 准确 |
| N22/N26 CI | 只有 tier 1 | `.github/workflows/tier1.yml` 一个 job：`verify-fast.sh --mutation` | 准确 |
| N28 集成收口 | 未做 | 只有清单文档；身份 sidecar 机制已具备但没有"冻结候选表" | 准确 |
| N29/N30 | 未做 | 安装/卸载门禁与 `verify-release.sh` 都在，但无第二台机器、无冻结候选 | 准确 |
| 玩家动画 | 只有 kIdle/kMovingNeedles，无 9/12 路、无 aimmatrix、无 LOD1+ | 默认路径已是 idle/walk/run/aim **四层加权混合**；`--animation-blend posegrid` 有 move_x/move_y 网格双线性采样；aimmatrix 作为普通（非 delta）层、权重封顶 0.5；**确无** STUDIO_DELTA/12 路 aim 网格、**确无** LOD1+ | **不准确**（混合与 aimmatrix 近似已有） |
| 3D 天空盒 | 默认关，N10 未签收 | 准确；另确认 sky3d pass **强制关雾**、只画天空簇的世界三角形 | 准确 |
| 小地图 | 只有 7 张表内地图 | 准确；地板几何其实对每张图都已推送，只是被表格门挡住 | 准确 |
| HUD 面板贴图 + DPI 17.1% | 未做 | 贴图：`image` 字段只解析不绘制，渲染层只有 1 个图像槽（被 overview 底图占用）。**17.1% 是"小地图面板与 Win32 状态栏"的重叠，不是贴图问题** | 半准确（两件事被并成一句） |
| 世界残差 | 94 px / 14 退化位移面 / 849/919 未归属像素 | 94 px 是旧读数（未在新绕序判据下重测）；"14 个"已被 task-9 实测为 4 个并修到 fallback 0；**849/919 是两个屏幕点坐标，不是像素数**，且 2026-11-19 COLOR0 回读在当前树上未复现 0,0,0 | **不准确** |
| cart/动态刷子 N09 | 未验证 | 会动的 brush 模型一律拒画（只计数+列名）；没有任何"按 demo 实体 origin/angles 变换 brush 模型"的代码 | 准确，且比"未验证"更严重：是**未实现** |
| 音频 | 4096 丢弃与解码错误未分开；换设备/seek 无恢复 | **分开计数已做**（`MessageCap`/`PayloadTooLarge` 与 `decodedSoundEventFailures` 三个计数）；**seek 恢复已做**（T93-C）；**换设备恢复确无**；人耳验收确无 | **不准确**（三条里两条已完成） |

---

## 2. 逐项审计

约定：
- 构建统一：`bash tools/lockrun.sh <owner> -- bash build-target.sh <target...>`。新源文件**先 `git add` 再构建**（`native/cmake/gen-build-stamp.cmake` 的 D-90 身份拒绝，见 `docs/N13-STEP1-IMPL-2026-11-15.md` §2）；改过的文件 `touch`。
- `/W4` 零警告，`/WX` 目标为 `tf2_core tf2_demo_native entity_protocol_probe entity_message_fixture_probe hud_probe minimap_probe review_workspace_probe`（`native/CMakeLists.txt:96-103`，`:130-131`）。新代码**不要写常量条件**（C4127）：开关一律是运行时 `bool`，不要 `if (kSomeConstexpr)`。
- **新 CLI 开关不能加进 main.cpp 的 else-if 长链**：那条链已在 MSVC C1061 嵌套上限（`docs/N13-STEP1-IMPL-2026-11-15.md` §2 第 3 条；`native/src/main.cpp:4040-4044` 注释）。现行做法是放在长链之前的"ladder arm"（`if (wcscmp(...)) { ...; continue; }`，例：`native/src/main.cpp:3945-3967` `--world-sky-3d`、`:3978-4004` `--lighting`、`:4022-4038` `--cosmetics`、`:4050-4080` `--viewmodel-weapon-attach`），或只改 `CliParseEnv` 字段的放进 `native/src/main_cli.h` 的 `kFeatureFlagTable`（例：`main_cli.h:167-187` `--animation-blend`）。未知值**拒绝**而不是回落默认（现有所有 A/B 开关都这么做）。
- `verify-all.sh` 自带锁，**不要**再套 `tools/lockrun.sh`（会死锁，见总览 §4.5）；跑完任何带变异的门禁后必须 `git status --porcelain -- native/` 手工确认源码已还原。

### 2.1 N13 观察者方向（broadcaster / follow / select）

**(a) 代码现状**
- 导播状态机（纯逻辑）：`native/include/spectate_target.h:9-36`（五条规则：死人不画、死亡交接、0.3 s 墙钟交接、手选即刻生效、全灭保持焦点）、`:77` `class SpectateDirector`、`:80` `kHandoverSeconds`、`:102` `update(...)`、`:118` `pickManual(...)`；实现 `native/src/spectate_target.cpp:299`。
- 全局实例 `native/src/main.cpp:1455`（`g_spectateDirector`），`--spectate-target` / `--spectate-pause-at` / `--spectate-pause-seconds` / `--spectate-show-corpses` 在 `:4581-4617`。
- **N13 step 1（2026-11-15）已落地**：状态 `main.cpp:1479-1580`；命令队列（上限 16，每帧一条）`:1492-1535`；`followConsumeArgument` 解析 `--follow-target <0..2047>` 与 `--follow-action select|next|prev|unfollow|back-to-pov` `:1589-1643`（在长链之前调用，`:4216-4218`）；四道校验 `followValidate` `:1712-`；`followReconcile` `:1958-2009`；每帧消费与 `pickManual` 交接 `:16330-16346`；`cameraEffect=pov-eye-wins|relay|no-snapshot|unknown` `:1697-1704`；导播拒绝 pin 的计数 `g_followFallbacks` `:16401-16416`。
- pane1 的跟随命令 `handlePane1FollowAction` `:1883-1946`（select/unfollow/next/prev）。
- **缺口（代码确认）**：
  1. pin 不能压过导播：`main.cpp:16394-16400` 注释明说"step 1 cannot make a pin outrank the relay's own decision -- that precedence is step 2"。
  2. 目标一死即解除 pin：`main.cpp:2000-2006`（`g_followTarget.pinned = false; state=Dead`），重生后不会自动回到该玩家。
  3. POV demo 中选人不改变画面：`cameraEffect=pov-eye-wins`（`docs/N13-STEP1-IMPL-2026-11-15.md` §4）。
  4. `serial` 无数据源，`gone-index-reused` 分支编码但不可达（`main.cpp:1982-1989`）。
  5. "broadcaster"（SourceTV 自动导播镜头）：全树 `hltv_chase|hltv_fixed|hltv_status` 0 命中。但 game event 是**全量按名保留**的（`native/src/demo_header_events.cpp:162-206`，无上限），所以若录像里有这些事件，数据已经在 `summary.gameEvents` 里，只是没人读（是否存在于样本 demo：**未核实**）。
  6. UI 按钮 `kUiFollowNext/Prev/Unfollow/BackToPov`：0 命中（设计在 `docs/N13-COMMAND-STATE-MODEL-2026-11-15.md:185`）。

**(b) 总览"未实现"不准确**：select/next/prev/unfollow 命令、trace 与拒绝分桶都已存在；未实现的是 step 2 的优先级/跨死亡保持、broadcaster 与 UI。

**(c) 最小下一步（可默认关）**
- 新开关 `--follow-sticky`（ladder arm，放在 `followConsumeArgument` 里与另两个 follow 开关并列，`main.cpp:1589` 起，避免 C1061）：只改 `followReconcile`（`main.cpp:2000-2006`）——开关打开时死亡不清 `pinned`，状态记 `Dead`；在 `:16343` 附近增加"pin 处于 Dead→Ok 转换时再发一次 `g_followPendingEntity = g_followTarget.entityIndex`"。默认关时两处代码不进入，`follow:` 行与相机行不变。
- 只读前置（零代码）：先跑 `demo_open_probe --scan --events 0 <STV.dem>` 数 `hltv_*` 事件，决定 broadcaster 是否有数据可用。
- broadcaster 本体（M）：新纯函数文件 `native/src/hltv_director_events.cpp`（+ `native/include/…h`，**git add**），把 `hltv_chase`（target1/target2/distance/theta/phi/inertia/ineye）转为 `SpectateDirector` 的 director handle 序列；开关 `--follow-broadcaster on|off` 默认 off。

**(d) 验证**
- 现有门禁：`spectate-check.sh`（A 段 `spectate_probe --self-test` 49 项、B 段 demo 回放、C 段产品 8 倍速、D 段交接形状）；`spectate-mutations.sh`。A8 回归脚本 `a8-check.sh` 在仓外 `D:/_shared/EXEC-2026-11-15/N13-QA/a8/`（**不随仓**）。
- 人要跑的命令：
  ```bash
  bash tools/lockrun.sh n13 -- bash build-target.sh tf2_demo_native spectate_probe demo_open_probe
  native/build-nmake/demo_open_probe.exe --scan --events 0 "<STV.dem>" | grep -c hltv_
  bash tools/lockrun.sh n13 -- bash spectate-check.sh
  git status --porcelain -- native/
  ```
  `--follow-sticky` 本身需要在 `spectate-check.sh` 之外加一条 A/B（无开关 vs 有开关：无开关的 `follow:` 行与 frame md5 必须逐字节一致）。

**(f) 参考入口**：`ValveSoftware/source-sdk-2013` `src/game/client/hltvcamera.cpp`（客户端按 HLTV 导播事件切换追踪/固定机位与观察目标）、`src/game/server/hltvdirector.cpp`（服务器端挑镜头并广播 `hltv_chase/hltv_fixed` 事件）、`src/game/shared/shareddefs.h`（`OBS_MODE_*` 观察者模式枚举）。行号未核实。

### 2.2 N16 分屏 / PIP / 四视图

**(a) 代码现状**
- 上限常量 `native/include/pane_boundary.h:57` `kPaneMaxPanes = 2u`；自检 `:199-254`（含两两不相交检查 `:253-254`）。
- 状态 `native/src/main.cpp:611-640`（`g_paneSplit`、`g_activePane`、pane1 相机模式与独立 `g_pane1Director`）。
- 开关 `main.cpp:4107-4194`：`--pane-split`、`--pane1-orbit y p d`、`--pane-activate`、`--pane-click x w`、`--pane-cycle`、`--pane-mode <pane> <mode>`、`--pane-follow <pane> <entity>`。
- 渲染：`native/src/native_renderer_frame.cpp:178-183` `draw()`（单 pane），`:189-223` `drawSplit()`（同一 surface 两次 `drawPaneInternal`），视口固定左右各半 `:340-347`；产品调用点 `main.cpp:16822`。
- 已完成的子项（`docs/CAMERA-MULTIVIEW-GAPS-2026-11-18.md`）：C1 split 下 ViewModel、C2 `--world-fov`、C3 pane1 的 next/prev/unfollow（即上面的 `handlePane1FollowAction`）。
- **缺口**：PIP / 四视图没有任何代码（`kPaneMaxPanes` 是 2，视口算式只认"左/右"）；`paneForClientX`（`native/include/pane_input.h:32`）只按 x 命中；pane1 右半残留 1203 px 未定位（同上文档 C1"残留"）。

**(b) 总览"只有契约"不准确**：两 pane 已可用并有自证 trace；PIP/quad 确无。

**(c) 最小下一步**：开关 `--pane-layout side|pip`（ladder arm，紧挨 `--pane-split` `main.cpp:4107`），默认 `side`。
- `native/include/pane_boundary.h`：加 `enum class PaneLayout { SideBySide, Pip }`，自检里 PIP 允许 pane1 被 pane0 包含（现在 `:253-254` 会报重叠）。
- `native/src/native_renderer_frame.cpp:340-347`：`paneLayout_ == Pip` 时 pane0 全屏、pane1 = 右上 1/4（宽高取偶数）；`:353-360` 的深度清除逻辑不用改（pane0 先画完）。
- `native/include/native_renderer.h`：加 `setPaneLayout(...)`；`pane_input.h` 加二维命中 `paneForClientPoint(x,y,…)`。
- 四视图需要把 `kPaneMaxPanes` 提到 4，会改 `PaneBoundaryReadings` 数组尺寸（`pane_boundary.h:164-169`、`native_renderer.h:3081`），风险高于 PIP，放第二步。
默认 `side` 时视口算式不变，单 pane 路径（`draw()`）完全不经过新代码。

**(d) 验证**：仓内**没有**任何门禁脚本使用 `--pane-split`（全树 ripgrep 只在 docs 与 main.cpp 命中），是一个门禁空洞。现可用：`viewmodel-contract-check.sh`（单 pane 不变）、`native_ui_probe.exe`、`camera_probe.exe`、`spectate_probe.exe --self-test`（纯逻辑）。命令：
```bash
bash tools/lockrun.sh n16 -- bash build-target.sh tf2_demo_native native_ui_probe camera_probe
native/build-nmake/native_ui_probe.exe ; native/build-nmake/camera_probe.exe
bash tools/lockrun.sh n16 -- bash viewmodel-contract-check.sh
```
建议同时新增 `pane-split-check.sh`（register 进 `gate-registry-check.sh`），把 CAMERA-MULTIVIEW-GAPS §4 的三条命令固化。

**(f) 参考**：source-sdk-2013 `src/game/client/viewrender.cpp`（每个视图单独 setup 视口/投影后 render，PIP 即多一次带小视口的 RenderView）。行号未核实。

### 2.3 N20 Pyro 火焰 / Medic 治疗束 / 流血

**(a) 代码现状**
- 数据面：`native/include/effects_timeline.h:11-15` 只有 `FireBullets / Particle / Explosion` 三类；转换 `addFireBulletsEvent/addExplosionEvent/addParticleEvent` `:58-64`。
- 保留上限仍在：`native/src/demo_header_events.cpp:318` `if (summary.tempEntityEvents.size() < 16384u)`，`:333` 投射物同上限；超出部分**没有丢弃计数**（只有 task-35 的 tick 审计能看到全量，`:312-317`）。`docs/N20-EFFECTS-AVAILABILITY-2026-11-16.md` §7 实测 68133 个 temp 事件只留 16384。
- 唯一消费者是 HUD 计数：`native/src/main.cpp:13157` `effectTimeline.countThrough(sceneTick)`。
- 火焰/治疗束所需的实体属性：`m_iWeaponState|m_bHealing|m_hHealingTarget` 全树 0 命中；`CTEBloodSprite|CTETFBlood|EffectDispatch` 0 命中。
- 无粒子绘制 pass（`native/src/native_renderer_overlay.cpp:252-279` 的 `particles` 是投射物轨迹叠层，不是 PCF 粒子）。

**(b) 总览准确。** 补充：即使只做计数，16384 截断也会让后 3/4 的 demo 没有事件。

**(c) 最小下一步（不动产品像素）**
1. `native/include/demo_header.h` 的 `DemoNetworkSummary` 加 `tempEntityEventsDroppedCap`，在 `demo_header_events.cpp:318` 的 else 分支自增；`native/tools/demo_open_probe.cpp` 在 `--scan` 行加打印 `temp_dropped_cap=`。产品帧不变（只多一个字段）。
2. 同一探针加 `--temp-class-census`：按 `className` 直方图打印（回答"有没有 CTETFBlood/哪些粒子名是火焰与治疗束"）。
3. 之后才是产品：治疗束不需要粒子系统——按 `CWeaponMedigun` 的 `m_bHealing` + `m_hHealingTarget` 两端点画一条队伍色 beam（overlay pass 已有画线能力），开关 `--effects-beam on|off` 默认 off。火焰与流血需要 PCF 粒子渲染，是 L。

**(d) 验证**：`native/tools/temp_effect_probe.cpp`、`temp_entities_fixture_probe.cpp`（单测）；`build-identity-check.sh`；产品帧不变用任一 md5 pin（pr600 tick 600 `2ddade35…`，见 HANDOVER §5.5）。
```bash
bash tools/lockrun.sh n20 -- bash build-target.sh tf2_core demo_open_probe temp_effect_probe
native/build-nmake/demo_open_probe.exe --scan "<demo.dem>"        # 读 temp_events= 与新 temp_dropped_cap=
native/build-nmake/demo_open_probe.exe --scan --temps 0 "<demo.dem>" | grep -c CTETFBlood
bash build-identity-check.sh
```

**(f) 参考**：source-sdk-2013 `src/game/shared/tf/tf_weapon_flamethrower.cpp`（网络化的武器状态决定客户端火焰粒子开/关）、`src/game/shared/tf/tf_weapon_medigun.cpp`（网络化治疗目标与是否在治疗，客户端据此连 beam）、`src/game/client/tf/c_tf_player.cpp`（客户端受击流血效果）；流血是否通过 `CTETFBlood` 类 temp entity 下发：**未核实**。

### 2.4 N27 产品导出（MF 方案 C）

**(a) 代码现状**：`native/src` 内 `IMFSinkWriter|MFCreateSinkWriter|mfplat|mfreadwrite|--export` 0 命中。整条编码链只在 `native/tools/mf_export_probe.cpp`（`:247-254` `createSinkWriterMp4`，`:15-17` 三种模式），由 `native/tools/mf_export_probe.sh:32` 直接 `cl.exe` 链接（不在 CMake 里）；`native/tools/mf_export_verify.sh` 是断言脚本。设计 `docs/N27-PRODUCT-EXPORT-ENTRY-DESIGN-2026-11-16.md` §2（`--export-video` 等 7 个旗标草案）、§1 第 9 条指出结构问题：**draw 节奏由墙钟决定，复用它导出的帧集合依赖机器负载**。

**(b) 总览准确。**

**(c) 最小下一步**：先不碰 MF。新开关 `--capture-sequence <dir> <startTick> <count>`（ladder arm），复用 `--capture-tick` 已有的"tick hold"（`main.cpp:12648-12652`）按 tick 逐帧落 BMP：每帧把 `captureTick` 前进 1 并重新 arm。产物喂给已有的 `mf_export_probe --encode-from-disk`。默认不传即不进入，现有 `--capture-frame` 路径不变。第二步才把 SinkWriter 移进产品（新文件 `native/src/export_mf.cpp`，git add，`tf2-product.cmake` 加 `mfplat/mfreadwrite/mfuuid`）。

**(d) 验证**：`bash native/tools/mf_export_probe.sh` + `bash native/tools/mf_export_verify.sh <mf_export_probe.exe> <work-dir>`；产品不变用 md5 pin。序列确定性：同参数跑两次，所有 BMP sha256 逐个相同。

**(f) 参考**：无 SDK 对应（Source 的 `startmovie` 在闭源引擎里）——**未能对读外部源码**；行为参考仅可写"引擎按固定 host_framerate 推进并逐帧落盘"。

### 2.5 N22 / N26 CI 只有 tier 1

**(a) 代码现状**：`.github/workflows/tier1.yml` 只有一个 job（`:53` `jobs:`），步骤是 LF 检查、`bash tools/tf2-toolchain.sh`（`:94`）、`bash verify-fast.sh --mutation`（`:99`）。`verify-fast.sh:97-98` 只构建 `tf2_demo_native presentation_probe entity_model_probe world_material_probe model_pose_probe item_schema_probe viewmodel_fov_probe`，`:229` 跑 `gate-registry-check.sh`。`tools/warn-check.sh`（新警告对比 `tools/warn-baseline.tsv`）**不在 CI**。文件头 `:33` 自己写着"STATUS: NOT YET OBSERVED ON THE REMOTE"。

**(b) 准确。**

**(c) 最小下一步**：在 `tier1.yml` 加第二个 job `tier1b`（不是 .sh、不在 native/，允许改）：构建 7 个 /WX 目标 + 不需要资产的纯逻辑探针（`spectate_probe camera_probe native_ui_probe hud_probe minimap_probe`），跑 `spectate_probe --self-test`、`native_ui_probe`、`camera_probe`，再跑 `bash tools/warn-check.sh`。不影响任何 pin。

**(d) 验证**：本地先跑同一组命令；远端只能 `gh workflow run tier1 --ref <branch>` 后看结果。
```bash
bash tools/lockrun.sh ci -- bash build-target.sh tf2_core tf2_demo_native entity_protocol_probe entity_message_fixture_probe hud_probe minimap_probe review_workspace_probe spectate_probe camera_probe native_ui_probe
bash tools/warn-check.sh && bash tools/warn-check.sh --selftest
native/build-nmake/spectate_probe.exe --self-test
```

**(f)** 无外部对应。

### 2.6 N28 集成收口

**(a) 现状**：只有清单 `docs/N28-N30-INTEGRATION-RELEASE-2026-11-15.md` §1（明确"本轮未执行"）。可用积木：每个可执行文件旁的身份 sidecar（`native/cmake/tf2-identity.cmake:43`、`write-identity-sidecar.cmake`，含 `sourceContent`）、`build-identity-check.sh`、`release-rebuild.sh`（D-54 身份刷新）、`evidence/BUILDS.md`（pin 出处）。没有"冻结候选表"（exe/sidecar/门禁/录像/资产五列）。

**(b) 准确。**

**(c) 最小下一步**：不改代码，写一份 `docs/FREEZE-CANDIDATE-<date>.md` 模板：exe sha256 + `tf2_demo_native.identity` 全文 + 当次各门禁最后一行 verdict + 用到的 demo sha256（总览 §6.3 指出 14 个 demo 已不在本机，表里必须写"缺失"而不是省略）。可选再加只读脚本 `tools/freeze-manifest.sh`（新文件，git add），只读 sidecar 与 `evidence/` 产出表格。

**(d) 验证**：`bash build-identity-check.sh`；`bash release-rebuild.sh`（会重建，必须在锁内）。

### 2.7 N29 / N30 干净机器首启、安装卸载、发布链

**(a) 现状**：安装/卸载 CLI 与门禁存在：`native/src/install_lifecycle.cpp`、`install-lifecycle-check.sh`（七种 TF 根 fixture、manifest 记录）、`install-cli-contract-check.sh`、`install-readiness-check.sh`、`native/tools/native_install_smoke_probe.cpp`；发布链 `verify-release.sh`（15 步，锁外拒绝运行，`VERIFY_RELEASE_DRY_RUN=1` 只打印计划）。阻塞（`docs/N28-N30-…md` §2.3）：A 无第二台受支持机器；B 全链一整天量级；C 音频人耳只有 2026-10-15 一条记录。

**(b) 准确。**

**(c) 最小下一步**：在本机用"干净用户目录 + 非默认 TF 根"做 §2.2 的 1/2/3/7/8/9 六步（不需要第二台机器），结果写进 N28 的冻结候选表。零产品代码。

**(d) 命令**：
```bash
VERIFY_RELEASE_DRY_RUN=1 bash verify-release.sh      # 只看计划
bash tools/lockrun.sh n29 -- bash install-lifecycle-check.sh
```

### 2.8 玩家动画

**(a) 代码现状**
- `native/src/main.cpp:485-509` `choosePlayerAnimationSequence` 仍是 `kIdleNeedles/kMovingNeedles` 选序列（总览行号 481-505 偏了 4 行），但**它只负责选"idle/moving"两档的基序列**。
- 默认路径已是四层加权混合：`main.cpp:14339-14380` 计算 `locomotionBlendWeights`（`native/include/animation_blend.h` 的 stand/walk/run 两段线性 ramp，和恒为 1）+ aim 层；`:14554-14563` 把 idle/walk/run/aim 四层交给采样器。
- aimmatrix：`native/include/animation_sequence_match.h:419-435` 按 `aimmatrix` token 选一帧姿态序列；以**普通加权层**混入，权重封顶 `kAimLayerWeightCap = 0.5`（`animation_blend.h:117`，`main.cpp:14359-14362`）。**不是** Source 的 delta 叠加层：全树 `STUDIO_DELTA|autolayer` 0 命中。
- 9 路网格：`--animation-blend posegrid`（`native/src/main_cli.h:167-187`，默认 legacy）→ `main.cpp:14571-14602` 喂 move_x/move_y，`native/src/animation_binding.cpp:557-613` `samplePoseGrid` 做四角双线性。`aimYaw` 被**强制 0**（`main.cpp:14599`，注释 `:14585-14598` 解释 CTFPlayer 不发 `m_angRotation`）。
- 不一致点（新发现）：legacy 默认路径的 aim 权重仍用 `viewYaw - bodyHeadingDegrees`（速度方向，`main.cpp:14351-14354`），而 posegrid 路径的注释（`:14585-14598`）已判定"速度方向不是身体朝向、会注入恒定扭腰"。两条路径对同一问题给出相反做法。
- LOD：VVD 的 LOD 顶点数已解析（`native/src/model_loader.cpp:206-211`），绘制只用 LOD0（`main.cpp:6053`、`:8959`）。
- 当前链上唯一真回归就是"第 26 步 player animation pixels"（总览 §7：掩码 3156 → 262681 px）。

**(b) 不准确**：有混合、有 aimmatrix 近似、有 9 路网格（开关后）；无 delta 叠加、无 12 路 aim 网格、无 LOD1+。

**(c) 最小下一步**：
1. 先查第 26 步回归（哪段 instance range 覆盖了 26 万像素），否则任何动画改动都没有可信基线。
2. 开关 `--aim-layer-heading view|velocity`（`kFeatureFlagTable` 一行 + `CliParseEnv` 一个字段），默认 `velocity`（现状，字节不变）；`view` 时 `main.cpp:14351` 改用 `viewYaw` 对 `viewYaw`（即 aim 权重 0，与 posegrid 结论一致）。
3. LOD：探针级先做——`model_pose_probe` 打印每模型 `vvdLodCount` 与各 LOD 顶点数；产品 `--model-lod <n>` 后置。

**(d) 验证**：`player-animation-check.sh`、`player-animation-pixels.sh`（第 26 步）、`player-animation-mutations.sh`（**会改 main.cpp，跑完必须核对 `git status --porcelain -- native/`**）、`animation-drive-states-check.sh`、`native/tools/model_pose_probe.cpp`。
```bash
bash tools/lockrun.sh anim -- bash build-target.sh tf2_demo_native model_pose_probe
bash tools/lockrun.sh anim -- bash player-animation-check.sh
bash tools/lockrun.sh anim -- bash player-animation-pixels.sh
git status --porcelain -- native/
```

**(f) 参考**：source-sdk-2013 `src/game/shared/multiplayer/multiplayer_animstate.cpp`（由速度算 move_x/move_y、由视角与身体朝向差算 body_yaw/aim 并有身体朝向追赶逻辑）、`src/game/shared/tf/tf_playeranimstate.cpp`（TF 对上述的特化）、`src/public/studio.h`（序列 blend 网格与 delta/autolayer 标志的定义）、`src/public/optimize.h`（VTX 的多 LOD 结构）。行号未核实。

### 2.9 3D 天空盒

**(a) 代码现状**：开关 `--world-sky-3d on|off` ladder arm `native/src/main.cpp:3945-3967`，默认 `native/include/native_renderer.h:93` `worldSky3d = false`，应用 `main.cpp:5099`；状态组装 `:9519-9533`（只在 `--world-geometry` 为 All 时启用）；trace `:9571`；上传只取天空簇三角形 `native/src/native_renderer_world.cpp:238-242`、`:1038-1042`、`:1107-1120`；绘制常量 `native/src/native_renderer_frame.cpp:1159-1198`——**`fogParams` 全 0（`:1192`）即关雾**、光照方向硬编码（`:1187`）。`sky_camera` 解析只取 origin/scale（`native/src/bsp_map.cpp:766-777`，结构 `native/include/bsp_map.h:237-242`），没有 `fogenable/fogcolor/fogstart/fogend`。只有天空簇内的世界面，天空盒里的 static prop 是否绘制：**未核实**。
- 签收缺项（`docs/WORLD-SKY3D-2026-11-18.md` §7/§7.1）：只有 pl_upward_f12 一张图、无 Source 参考帧、无逐像素归因、2D/3D 叠放顺序未测、无独立 skybox 雾、cluster 无断言。

**(b) 准确。**

**(c) 最小下一步**：`BspSkyCamera` 加 4 个雾字段，`bsp_map.cpp:766-777` 解析；`native_renderer_frame.cpp:1192-1194` 在 `worldSky3d_` 带雾时填入（只在 `--world-sky-3d on` 生效；为了 A/B 再加 `--world-sky-3d-fog on|off`，默认 off，使 on 帧也可与旧 on 帧逐字节比）。另在 trace `main.cpp:9571` 后加一行 `sky3d: fog enable=… start=… end=…`。默认 off 的帧完全不经过这些分支。

**(d) 验证**：`world-sky-gate-check.sh`（不含 `--world-sky-3d`，默认帧须不变）、`world_geometry_probe`（解析读数）。签收所需的第二张正样本与同机位对照必须由人跑产品抓帧。
```bash
bash tools/lockrun.sh sky -- bash build-target.sh tf2_core tf2_demo_native world_geometry_probe
bash tools/lockrun.sh sky -- bash world-sky-gate-check.sh
```

**(f) 参考**：source-sdk-2013 `src/game/server/sky_camera.cpp`（sky_camera 实体的 scale 与独立雾参数键）、`src/game/client/viewrender.cpp`（先以缩放后的天空相机画 3D 天空盒并用其独立雾，再画主世界）。行号未核实。

### 2.10 小地图

**(a) 代码现状**：硬编码表 `native/include/minimap.h:273-280`（7 行：cp_process_f12、cp_sunshine、cp_gullywash_f9、cp_sultry_b8a、koth_product_final、koth_bagel_rc11、pl_upward_f12，含控制点坐标 `:218-271`）；查表 `:305`；`native/src/main_hud.cpp:1213` `hudMinimapCompose`，`:1229-1232` 未命中即 `reason=bounds-not-tabulated` 返回；`:1030-1039` 注释说明"七张图都没有 radar overview（T135 查过 pakfile/VPK/散文件）、BspMap 不带实体表所以表格编进二进制"。
- 但：**地板几何对每张图都已推送**——`main.cpp:6920` `minimapHudSetFloorGeometry(...)` 不看表格；`main_hud.cpp:1273-1279` 才用"已上表"来挡。另外 bsp_map.cpp 实际**会**读实体 lump（`sky_camera`、`light_environment`，`bsp_map.cpp:764-780`），所以"BspMap 不带实体表"已不完全成立。
- 第二套"大地图 overview"走 demos.tf 底图包（`native/include/map_basemap.h:3-36`，`E:/demo测试/demos_tf_maps` 的 `boundaries.json`），与 `--hud-minimap` 不是同一条路。
- **(e) TF2 自身数据能否替代硬编码表**：
  - `resource/overviews/<map>.txt`（`pos_x/pos_y/scale/rotate`）：全树 `overviews|pos_x` 0 命中，**没有读取代码**；而 T135 结论是这七张竞技图不带该文件（`main_hud.cpp:1030-1032`）。TF2 本体对官方图是否随 VPK 提供该文件：**未核实**（本次未读 VPK）。可以接，但只能作为"有则用"的第一优先级，不能指望覆盖社区图（cp_snakewater 未核实）。
  - **更稳的两路 TF2 自身数据**：①边界——直接取已推送地板三角形的 xy 包围盒（`main.cpp:6920` 的同一份数据，零新解析）；②控制点——demo 里的 `CTFObjectiveResource` 网络化了控制点位置数组 `m_vCPPositions` 与 `m_iNumControlPoints`（全树 0 命中，未用），这比 BSP 实体 lump 更贴合录像时刻。
  - 读文件应走已有资产链 `AssetStoreChain::lookup(path)`（`native/include/asset_lookup.h:47-59`）。

**(b) 准确。**

**(c) 最小下一步**：开关 `--hud-minimap-bounds table|bsp`（`kFeatureFlagTable` 或 ladder arm），默认 `table`。
- `native/include/minimap.h`：加 `inline bool minimapBoundsFromFloor(const std::vector<MinimapFloorTriangle>&, MinimapMapRow& out, ...)`（纯函数，加 margin，控制点数 0）。
- `native/src/main_hud.cpp:1229-1232`：`row == nullptr && boundsMode == Bsp` 时用上面的派生行，reason 记 `bounds-from-floor`；`:1273-1279` 的准入同步放宽到同一模式。
- 第二步：`demo_header_*` 读 `CTFObjectiveResource.m_vCPPositions`，填派生行的控制点。
默认 `table` 下 A11（未上表地图逐字节不变 + `bounds-not-tabulated`）保持成立。

**(d) 验证**：`minimap-hud-check.sh`（A1–A11，A11 是未上表地图负控）、`minimap-check.sh`（`:294-310` 有 cp_snakewater_final1 的 canvas 读数）、`native/tools/minimap_probe.cpp`（/WX 目标，纯函数可直接加自检）。
```bash
bash tools/lockrun.sh mm -- bash build-target.sh tf2_demo_native minimap_probe
native/build-nmake/minimap_probe.exe
bash tools/lockrun.sh mm -- bash minimap-hud-check.sh
bash tools/lockrun.sh mm -- bash minimap-check.sh
```

**(f) 参考**：source-sdk-2013 `src/game/client/mapoverview.cpp`（按地图名读 `resource/overviews/<map>.txt`，用 pos_x/pos_y/scale/rotate 做世界→图像变换）、`src/game/server/team_objectiveresource.cpp` / `src/game/client/c_team_objectiveresource.cpp`（服务器把控制点位置/归属等网络化给客户端）。行号未核实。

### 2.11 HUD 面板贴图 + DPI 17.1%

**(a) 代码现状**
- 贴图：`native/src/hud_import.cpp:634` 解析 `image`，`native/include/hud_import.h:175` 注释"declared only: this build rasterises no image"；面板路径只发 Rect + Text（`main_hud.cpp:335-418` `hudPanelExpandLabel`）。渲染层 `native/include/native_renderer.h:1606` 已有 `HudPrimitiveKind::Image`，但 `:1614-1618`、`:1641-1662` 说明**只有一个常驻图像纹理**（`setHudImageTexture`），目前被 map overview 底图占用。
- DPI 17.1%：**与贴图无关**。它是小地图面板（`native/include/hud_panel.h:196-213`，160×160，y=12）与 Win32 状态栏（`native/src/native_ui.cpp:653-657` 一带，y=12 高 28）起点相同导致的重叠 156×28 = 4368 px（`docs/N15-DPI-LAYOUT-2026-11-15.md` §2.1）。布局函数 `hudLayoutPanels` `hud_panel.h:104-185` 不知道状态栏；调用点 `native/src/main_hud.cpp:1162-1175`。另有 `hud_panel.h:175` 只判 y 不判 x 的逻辑漏洞（不可由真实窗口触发）。视觉是否真的被状态栏盖住：门禁读的是后备缓冲，看不见 Win32 子控件——**未核实**。

**(b) 半准确**：两件独立的事。

**(c) 最小下一步**
- DPI（S）：开关 `--hud-panel-top <px>` 或 `--hud-panel-avoid-status`，默认关；开时 `hudMinimapPanelRect`（`main_hud.cpp:1162-1175`）把 `safeInset` 的顶部替换为"状态栏底 + 4"。默认关时矩形不变，`minimap-hud-check.sh` A3 的独立重算矩形仍一致。
- 贴图（M–L）：`native/src/main_hud.cpp` 新增面板图集：把面板 `image`（`materials/vgui/<image>.vmt` → VTF，走现有 VPK 链）打包进一张 RGBA，`Image` 原语用 uv 裁切；与 overview 底图互斥（overview 打开时不上传面板图集）。开关 `--hud-panel-images on|off` 默认 off。

**(d) 验证**：`hud-check.sh`、`hud-text-check.sh`、`hud-live-check.sh`、`minimap-hud-check.sh`；纯布局 `native_ui_probe.exe`（42 组 DPI×尺寸）、`hud_probe.exe`（/WX）。
```bash
bash tools/lockrun.sh hud -- bash build-target.sh tf2_demo_native hud_probe native_ui_probe
native/build-nmake/hud_probe.exe ; native/build-nmake/native_ui_probe.exe
bash tools/lockrun.sh hud -- bash hud-check.sh
bash tools/lockrun.sh hud -- bash minimap-hud-check.sh
```

**(f) 参考**：source-sdk-2013 `src/vgui2/vgui_controls/ImagePanel.cpp`（按 `image` 键解析成 `vgui/` 下材质并按面板尺寸/缩放绘制）、`src/game/client/tf/vgui/` 下 TF HUD 面板（未核实具体文件）。

### 2.12 世界残差（94 px 绕序 / 退化位移面 / N05 849,919）

**(a) 代码现状**
- 绕序：判据 `windingFacesViewer` 在诊断 trace 里（`native/src/main.cpp:11094-11108`），A/B 开关 `--no-world-cull` / `--world-no-alpha-test` / `--world-show-own-body` 在 `main.cpp:4251-4256`；另有 `--world-keep-map-winding` / `--world-flip-disp-winding` 预扫 `:3714-3718`。HANDOVER §5.1：pr600 的 313 点残余在 alpha-off 下全部填回 ⇒ 真几何丢失 0 px；**94 px 是更早的读数，未在新判据下重测**（`docs/GEMINI-QUALITY-TASKS-2026-11-20.md` G7 同样写明）。
- 位移面：`native/src/bsp_map.cpp:1745-1775` 已把 cell 上限改成 `max(262144, 16*flatEdge2Max)`；按原因分桶的计数 `native/include/bsp_map.h:411-420`，探针打印 `native/tools/world_geometry_probe.cpp:593-600`。HANDOVER §5.1：台账"14 个"实测为 4 个，改后 fallback 0（5671/5671）。
- N05：849,396 与 919,379 是**屏幕点坐标**（`docs/BLACKPOINT-849-919-2026-11-14.md`）；`docs/D01-COLOUR0-READBACK-2026-11-19.md:7,34-39`：同 recipe 当前树在 849 读到 70,70,70，919 COLOR0=255，归档里的 0,0,0 **今天拍不到**。门禁 `world-black-account-check.sh` 仍把两点 pin 为 OPEN。

**(b) 不准确**：14→0 已修；94 px 待重测；849/919 是点而非像素数且当前不复现。

**(c) 最小下一步**：**零代码**。按顺序：world_geometry_probe 七图确认 `fallback=0`；pr600 tick 600 默认/`--no-world-cull` 两帧逐像素差（同一 exe）重测"94 px"；`world-black-account-check.sh` 在当前 exe 上重跑，若 849 仍非 0，则把 D01/N05 改成"不复现，关闭或改 pin"。若重测后仍有残差，再谈代码。

**(d) 命令**：
```bash
bash tools/lockrun.sh world -- bash build-target.sh tf2_demo_native world_geometry_probe
native/build-nmake/world_geometry_probe.exe --tf-root "D:/SteamLibrary/steamapps/common/Team Fortress 2/tf" --map maps/cp_process_f12.bsp | grep "displacement coverage"
bash tools/lockrun.sh world -- bash world-black-account-check.sh
bash tools/lockrun.sh world -- bash world-geometry-hole-check.sh
```

**(f) 参考**：`tasbox-org/source-parsers`（位移面 dispinfo/dispvert/power）、source-sdk-2013 `src/public/bspfile.h`（ddispinfo_t 布局、面/边结构）。行号未核实。

### 2.13 cart / 动态刷子（N09）

**(a) 代码现状**：`native/src/bsp_map.cpp:941-958` 对非 `func_brush/func_wall/func_lod` 的 brush 模型一律拒画，只计数 `brushModelsSkippedMoving/Unknown` 并记名单 `brushModelsRefused`（`native/include/bsp_map.h:266-280`）；`--world-allow-class <cls>`（`main.cpp:3719-3728`）只能把某类按**编译期姿态**加画。全树 `func_tracktrain|CFuncTrackTrain` 0 命中；**没有任何"按 demo 实体 origin/angles 变换 brush 模型"的代码**。推车模型（prop）是否随父实体移动：`entity_model.cpp` 对 `m_iParentAttachment > 0` 直接拒绝（HANDOVER §7.2 表，`native/src/entity_model.cpp:890-892`），父链只有 attachment 0 的情况——推车具体走哪条路：**未核实**。

**(b) "未验证"偏轻**：动态 brush 是**未实现**。

**(c) 最小下一步**：探针先行——`native/tools/brush_entity_probe.cpp` 加 `--demo <dem> --tick N`：列出 modelprecache 中 `*N` 形式的模型、对应的 demo 实体类名与该 tick 的 origin/angles，和 `brushModelsRefused` 求交（"哪些被拒的 brush 在录像里有实体且在动"）。产品第二步：开关 `--world-dynamic-brush on|off` 默认 off，把命中的 brush 模型面单独建一个 VB，按实体变换每帧画。

**(d) 验证**：`native/tools/brush_entity_probe.cpp`；默认帧 md5 不变；正样本需人跑 pl_upward_f12 直道/弯道/暂停/seek 四帧。

**(f) 参考**：source-sdk-2013 `src/game/server/trains.cpp`（func_tracktrain 沿 path_track 移动并网络化 origin/angles）、`src/game/server/team_train_watcher.cpp`（推车进度与轨道）、`src/game/client/c_baseentity.cpp`（`*N` brush 模型按实体变换绘制）。行号未核实。

### 2.14 音频

**(a) 代码现状**
- **分开计数已完成**：`native/include/demo_header.h:847-861`（`SoundDropReason{PayloadTooLarge, MessageCap}`、两个丢弃计数、`decodedSoundEventFailures`）；`native/src/demo_header_sound.cpp:28-36` `recordDroppedSoundMessage`，`:61-103` 超保留上限（4096 条）**仍从 scratch 解码**、只丢保留副本（N21(c)），真正的解码失败在 `:120-168` 逐字段 `++decodedSoundEventFailures`。打印：`native/tools/demo_open_probe.cpp:1160-1171`（`sound_failures=`、`sound_dropped_payload_too_large=`、`sound_dropped_message_cap=`），产品标题行 `main.cpp:8412` `sounddecodefail=`。守恒校验脚本 `native/tools/sound_accounting_verify.sh`（I1：总数 = 各原因之和）。
- **(e) 丢弃计数位置**：结构体字段在 `DemoNetworkSummary`（`demo_header.h:852-853`），写入点只有 `demo_header_sound.cpp:92`（MessageCap）与 `:101`（PayloadTooLarge）。
- **seek 恢复已完成**：`main.cpp:12615-12676`（暂停 stop/恢复 reset、反向 reset、T93-C 前向 seek 按 ledger `seeks` 计数触发 `soundScheduler.reset`，`audio: seek reset from=… to=…` trace）；调度器 `native/src/audio_player.cpp:323-347` 记 `reverseRecoveries`。
- **换设备恢复确无**：`audio_player.cpp:271-306` 遇 `MMSYSERR_INVALHANDLE` 只计 `g_deadHandleCount`，不重开；`WM_DEVICECHANGE` 全树 0 命中；`--audio-device` 只在启动解析（`main.cpp:4755-4760`）。
- 人耳验收：只有 `evidence/audio/listening.txt` 一条 2026-10-15 记录（`docs/N28-N30-…md` §2.3 C）。

**(b) 不准确**：三条里"分开统计""seek"两条已完成。

**(c) 最小下一步**：开关 `--audio-device-recover on|off` 默认 off。`native/src/audio_player.cpp` 在 `deadHandle` 计数处（`:271-276`、`:305-306`）设置"需要重开"标志；`main.cpp` 主循环在 `soundScheduler` 每帧处（`:12605` 一带）发现标志且开关开时，按同一 `--audio-device` 索引（失败则 WAVE_MAPPER）重开并 `soundScheduler.reset(g_playback.tick)`，打印 `audio: device reopen attempt=… result=…`。窗口过程加 `WM_DEVICECHANGE` 只置标志。默认 off 时 trace 与行为不变。

**(d) 验证**：`verify-audio.sh`、`verify-audio-content.sh`、`audio-keys-check.sh`、`native/tools/sound_accounting_verify.sh`、`audio_scheduler_probe` / `winmm_sink_probe`（后者可用注入的 backend 模拟 INVALHANDLE，`native/include/audio_player.h:64` 的函数指针表即注入缝）。门禁需 `AUDIO_DEVICE=6`。设备热插拔与听感必须人做。
```bash
bash tools/lockrun.sh audio -- bash build-target.sh tf2_demo_native demo_open_probe audio_scheduler_probe winmm_sink_probe
bash native/tools/sound_accounting_verify.sh      # 参数见脚本头
AUDIO_DEVICE=6 bash tools/lockrun.sh audio -- bash verify-audio.sh
```

**(f) 参考**：source-sdk-2013 不含引擎声音层（`snd_*` 在闭源引擎）——**未能对读外部源码**；设备恢复语义只能按 Windows 文档（设备移除后句柄失效需重开）自研。

---

## 3. 排名表

排序原则：小、可放在默认关开关后、能被现有门禁验证的排前面。"不运行程序即可完成"指**代码改动本身**可以只读+编辑完成（编译与门禁验证仍需人跑）；"零代码"项本身就是跑程序，标"否"。

| # | 项 | 工作量 | 不运行程序即可完成 | 对现有门禁的风险 | 建议顺序 / 理由 |
|---|---|---|---|---|---|
| 1 | 文档纠偏：总览 §3 按本文 §1 改写（N13/N16/动画/世界残差/音频） | S | 是 | 无 | 1：避免下一轮按过时描述重复开工 |
| 2 | N20 step 0：`tempEntityEventsDroppedCap` + 探针 `temp_dropped_cap=` / 类名直方图 | S | 是 | 极低（产品帧不变，只多字段） | 2：先把 16384 截断变成读数，也回答 CTETFBlood/hltv 是否存在 |
| 3 | 小地图 `--hud-minimap-bounds bsp`（地板包围盒） | S | 是 | 低（默认 table，A11 不变） | 3：数据已在内存、纯函数可进 /WX 的 minimap_probe 自检 |
| 4 | DPI：小地图面板避开状态栏 `--hud-panel-avoid-status` | S | 是 | 低（默认关，A3 不变） | 4：一处函数，native_ui_probe/hud_probe 可断言 |
| 5 | N13 `--follow-sticky` | S–M | 是 | 低（默认关；spectate-check 不变） | 5：只动 followReconcile 与交接点 |
| 6 | 3D 天空盒雾 `--world-sky-3d-fog`（+ trace） | S | 是 | 低（仅 sky3d on 下生效） | 6：签收所需的第二图正样本仍要人抓帧 |
| 7 | CI tier1b（7 个 /WX 目标 + 纯逻辑探针 + warn-check） | S | 是 | 无（不碰 pin） | 7：远端结果要推分支后才能看 |
| 8 | 世界残差重测（94 px / fallback / 849,919） | S | **否**（本身就是跑） | 无 | 8：可能直接关掉三条旧账 |
| 9 | 音频 `--audio-device-recover` | M | 是 | 低（默认关） | 9：注入 backend 可单测；热插拔与听感需人 |
| 10 | 动画：先查第 26 步回归，再 `--aim-layer-heading` | M | 否（回归归因要跑） | 中（player-animation 门禁本就红） | 10：基线不稳前不要动动画 |
| 11 | ~~N16 `--pane-layout pip`~~ **已完成（2026-11-23）**：`PaneLayout` 枚举 + 按布局区分的 R8、pane 1 右下角 3/10 内嵌、`pane-pip-check.sh` | M | 是 | 低–中（自检规则需按布局区分） | 11：且需新增 pane-split 门禁 |
| 12 | N27 `--capture-sequence`（先不接 MF） | M | 是 | 低（默认不进入） | 12：解决"帧集合依赖负载"后再做编码 |
| 13 | N09 探针：被拒 brush ∩ 录像实体 | S–M | 是 | 无（只改探针） | 13：产品侧动态 brush 是 M–L |
| 14 | HUD 面板贴图图集 | M–L | 是 | 中（与 overview 共用唯一图像槽） | 14 |
| 15 | N13 broadcaster（hltv_chase → director） | M | 是 | 低（默认关） | 15：取决于第 2 项是否证明数据存在 |
| 16 | N20 治疗束 / 火焰 / 流血绘制 | L | 是 | 中 | 16：治疗束可先于粒子单独做 |
| 17 | N28 冻结候选表 → N29 本机干净目录六步 → N30 | M / L | 否 | 无 | 17：需冻结窗口、第二台机器与一整天全链 |

## 4. 未核实清单

- 样本 STV demo 是否含 `hltv_chase/hltv_fixed` 事件；是否含 `CTETFBlood` 类 temp entity。
- TF2 VPK 是否随官方图提供 `resource/overviews/<map>.txt`（尤其 cp_snakewater_final1）。
- 3D 天空盒内 static prop 是否被绘制。
- 推车 prop 的父链/attachment 走哪条路径。
- 小地图面板是否在可见窗口里真的被 Win32 状态栏盖住。
- 所有外部仓库路径的行号（本次未对读外部源码）。
