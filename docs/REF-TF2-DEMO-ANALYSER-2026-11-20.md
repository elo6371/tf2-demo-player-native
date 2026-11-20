# TF2-Demo-Analyser 参考评估（2026-11-20）

* 评估对象：用户下载的 `C:\Users\Administrator\Downloads\TF2-Demo-Analyser-build-4\`（本地 build 4 源码 + 冻结 exe），以及 GitHub 上的 `wombo14/tf2-demo-analyser` main 分支（2026-11-16 build 8 上游）。
* 方法：本地源码逐文件读（70 个 `.rs`，55,978 行）；GitHub raw/codeload 拉取 upstream main 对照（128 个 `.rs`，63,234 行，含 build 7 新增的完整性检测）；`releases.atom` 读 build 3–8 发布说明；shipped exe 用 `grep -a` 做字符串取证。
* 没有做的事：没有运行该工具的 GUI/CLI，没有跑它的 3D player，也没有用我们自己的 demo 喂它。所有"观感"判断来自 `docs/images` 截图与源码行为，不作为像素级证据。

## 0. 结论表

| 问题 | 结论 | 对我们要不要参考 |
|---|---|---|
| 渲染模式 | OpenGL 3.3 core（eframe/egui 的 glow 后端），先把场景画进离屏图像再贴进 egui；世界 pass 做 `base`/`base2` 混合 + lightmap + 曝光 tonemap；模型 pass 自带骨骼蒙皮；另有 glow、trace 线、屏幕后期 | **部分参考**：资产加载（vbsp/vmdl/vtf）、蒙皮、viewmodel 组合可借；它的采样/插值/启发式相机不适合我们"逐 tick 取证"的定位 |
| 代码结构 | 分析层（algorithms/base/lib）+ 回放层（player：recording → view → renderer）+ 双 bin（gui/cli）。tick 模型是"整份 demo 读一遍、按采样 tick 存快照"，长 demo 会抽稀（`MAX_FRAMES=150_000`） | **结构可参考，代码不可搬**（GPLv3） |
| "异常 tick 检测" | 本地 build 4 **没有**这个功能（exe 里连 `anomal` 字样都没有）；build 7+ 新增的是 **demo 完整性/篡改检测**（`src/util/integrity.rs`），不是渲染异常 tick 检测；另一个可借的是 `util/nocrex/jankguard.rs` 的 spawn/teleport tick 窗口 | **方法论可借**：对我们的 N11 异常归因直接有用；功能本身与我们的"异常 tick"不是一回事 |
| 许可证 | GPLv3（`LICENSE.md`），README 自述"大部分是 vibecode 出来的"；我们仓库根没有 LICENSE 文件 | **不能复制代码/整段移植**；读思路、重写实现可以做，需在文档里记理由 |

## 1. 这是什么项目

* 血统：`wombo14/tf2-demo-analyser` fork 自 `Nocrex/demo-analysis`，后者基于 `MegaAntiCheat/analysis-template-rust`；本地 build 4 基于 `Fidoo001/tf2-demo-analyser @ a89f7e1`（2026-11-09），GPLv3。
* 定位：TF2 demo 的**作弊检测分析器**（CLI 批量扫 / GUI 逐个看 + 3D 回放）。不是渲染器，也不是 demo 工具库。
* 依赖：`tf-demo-parser 0.6.4`（demo 解析）、`vbsp 0.9.1` / `vmdl 0.2.0` / `vtf 0.4.1` / `tf-asset-loader 0.2.0`（直接读 TF2 安装目录里的地图/模型/贴图）、`glam 0.30`、`eframe/egui 0.31.1`（glow/OpenGL）、`cpal 0.15.3` + `symphonia`(mp3) + `unsafe-libopus`（声音）、`lzma-rs`/`snap`、`image`、`rayon`、`sha2`；features：gui / player / parallel / rcon / tf2bd / experimental。
* 版本：build 4（用户手里的）是 2026-11-09 代码；build 7（11-16 晚）才加入 integrity check；build 8（同日更晚）只是主窗口联动。**用户手上这份不含完整性检测。**

## 2. 渲染模式（它怎么把 demo 画成 3D）

### 2.1 管线

* `src/player/renderer.rs`：GLSL `#version 330 core`，MSAA 4x。注释原文：把世界画进一张**离屏图像**，再拷进 egui 窗口；渲染全在 UI 线程。
* 着色器：`WORLD`（基础色 `base` 与叠层 `base2` 按 `weight` 混合、lightmap、`EXPOSURE=2.2` 的 tonemap）、`MODEL`、`SKINNED`（骨骼蒙皮）、`GLOW_MASK`、`SCREEN`、`GLOW`、`TRACE`。
* 场景内容：真地图（BSP + lightmap + props + skybox）、职业模型与武器、火箭/黏弹/建筑/门/推车/拾取物、弹道线与命中点、伤害数字、透视 glow、瞄准线、真实 hitbox、backtrack ghosts；**有意不含 cosmetics/hats**。
* 相机三种：被观察玩家的第一人称眼（含手/武器、开火/换弹/切换动作、TF2 准星、狙击开镜 `SCOPED_FOV=20`）、第三人称追尾（拖拽环绕、滚轮缩放）、自由相机（WASD/QE/Shift）。
* 声音：读 TF2 音频文件，枪声/脚步/语音/语音聊天，`cpal` 输出。

### 2.2 与我们最相关的：第一人称怎么拼

`src/player/view.rs` 的 `view_model()`（upstream :3086 起）顺序是：

1. `first_person_slot()` 取被观察槽位；**门禁**：`!player.is_alive() || player.is_zoomed() || player.is_out_of_view()` 直接返回空（不画手/武器）。
2. 录制者本人：手模型与动画**直接从 demo 读**（`view.arms` / `view.sequence` / `view.sequence_start`，来自 demo 的 `ViewModel` 实体）；其他人：用职业的 `class.arms` + `first_person_sets()/first_person_actions()` 回放出 idle/拔枪/开火/挥砍/换弹。
3. 摆姿势：`skeleton.world_matrices(&pose, root)`，`root = 相机 eye + sway` 与相机旋转；`sway` 由 `view_model_sway()` 用最近 1 秒视角历史算（`SWAY_HISTORY_SECONDS=1.0`、`MAX_SWAY=1.5`，视角落后越多回正越快）。
4. `fix_grip()`：**如果 `bip_hand_r` 与 `weapon_bone` 的世界距离 > `GRIP_DISTANCE=12`，就把武器整体放到视图空间里的固定位置** `HELD_GUN_OFFSET=(16,-17,-19)`（枪管方向 `(1.0,0.16,0.06)`）。这是给"握点对不上"的兜底。
5. 武器挂接：`weapon_skeleton.attached_world(arms_skeleton, world, root)`；只画"拿在手里的"——`weapon_slot != WEAPON_UNKNOWN || recorded.is_some()` 且武器骨架必须有 `weapon_bone`（否则像 Pyro 喷气背包这种穿戴物会贴到镜头上）；顺带取 `muzzle` 挂点给枪口焰。

### 2.3 它渲染得"像"的代价

* 相机是**重建**不是复现：玩家眼 = 录像里的 view origin + 按职业/下蹲查表的眼高（`recording.rs:157-167`：下蹲 45、Scout 65、Sniper/Medic/Heavy/Spy 75、其余 68），而且只在"活着且身体位置与 view origin 距离 < 4"时才加眼高；spectate 时 demo 记的就是相机本身，不加。
* 相邻 tick 位移超过 `MAX_STEP_PER_FRAME=300` 就不插值（防传送穿模）。
* `Recording` 对长 demo 抽稀采样（`MAX_FRAMES=150_000`），`frame_at()` 二分找最近帧再插值。**这是"看得过去"的设计，不是"逐帧可取证"的设计**——和我们的目标相反。

## 3. 代码结构（值得参考的部分）

| 模块 | 作用 | 备注 |
|---|---|---|
| `src/player/recording.rs`（84 KB） | 整份 demo 读一遍，按 tick 存 `PlayerFrame`/`ViewFrame`/`VisionFrame` 快照 | 字段：position/yaw/pitch/health/team/class/flags（alive、out-of-view、ducking、on-ground、in-water、cloaked、zoomed、aiming）/weapon_slot/weapon_model/travelled/ping |
| `src/player/view.rs`（246 KB） | 所有"看"的逻辑：相机、第一人称、覆盖层、回放/审阅 | `first_person_slot`、`view_model`、`fix_grip`、`view_model_sway`、`eyes_or_free_camera`、`scoped()` |
| `src/player/renderer.rs`（58 KB） | GL 管线与 pass | MSAA 4x；离屏→egui |
| `src/player/skeleton.rs` / `map.rs` / `characters.rs` / `items.rs` / `ragdoll.rs` | 骨架与蒙皮、BSP/lightmap/sky、职业与动画、武器表、布娃娃 | `map.rs` 57 KB |
| `src/util/helpers.rs`（88 KB） | demo 修补与工具 | 见 §5.3 |
| `src/util/integrity.rs`（build 7+，634 行） | demo 完整性检测 | 见 §4 |
| `src/algorithms/**` | 作弊检测算法（视角/弹道/输入/ML） | 与渲染无关 |

值得抄的**设计**（不是代码）：录制层只产出"纯数据帧"，`view` 层只做"把帧变成 Actor 列表"，`renderer` 只认 Actor；三层的边界干净，单元测试直接测前两层。我们 `native/src` 的 `scene:` trace 行其实已经在往这个方向长，但"数据/呈现/GPU"还没有硬边界。

## 4. "异常 tick 检测"到底是什么

### 4.1 本地 build 4：没有

* 源码里没有 anomaly/corrupt-tick 检测器；`grep -a -i anomal "Demo Analysis.exe"` 零命中（21 MB 的 exe 连这个词都没有）。
* 它有的只是**解析容错**：坏命令行跳过并报 `Corrupted or unsupported demo command {cmd} at offset {frame_start}`（`src/util/helpers.rs:1319`）、语音包坏了跳过（`voice.rs:100-101`）、CLI 统计 partial demo（`cli.rs:1211-1213`）；`datatable_validator.rs` 防的是 datatable DAG 炸弹（`MAX_TOTAL_DATATABLE_PROPS=250_000` 等上限），不是 tick。
* 它语境里的 "tick" 是**作弊检测的标注点**：每条 detection 带 tick，UI 上画时间轴、review mode 跳到该 tick 慢放。不是"检测异常 tick"。

### 4.2 build 7+ 的 Demo integrity check（最接近用户问的功能）

`src/util/integrity.rs`，build 7 发布说明：主窗口 demo 名字旁显示 ✓ No signs of editing / ⚠ Damaged or unusual / ✗ Looks edited，并可开窗说明"发现了什么、在哪"。自述在 57 份真实 demo 上零误报，对人工编辑副本全部命中（最小 3 秒剪切）。设计是"流式量一遍 + 判一次"：

* **量**（`measure()`，:127）：header 的帧数/tick 数/秒数、实际包数、首个/最后 tick、tick 间隔、相邻 gameplay 包 tick 步长分布、包序号步长、`net_Tick`（服务器时钟）与 demo tick 的偏移序列、计数器回退位置。
* **判**（`judge()`，:227）：
  * 包数 ≠ header 帧数 → Edited（"Packets were added or removed"）；少了且没有 demo 结束标记 → Warning（"The end of the file is missing"）；
  * 实际 tick 超出 header 结尾 5 个以上 → Edited；比 header 早 1000 个以上（`HEADER_TICK_SLACK`）→ Warning（结尾可能被剪）；
  * header 秒数与 tick×interval 差 > 1.0 s（`HEADER_SECONDS_SLACK`）→ Edited；
  * 任何计数器回退 → Edited（真实 demo 从不回退）；
  * **关键**：`net_Tick − demo_tick` 的偏移在 2000 tick 记忆窗口（`CLOCK_MEMORY_TICKS`）里向前跳 > 66（`CLOCK_SKIP_TICKS`）→ 说明中间被剪掉一段（前 200 tick 的 `WARMUP_TICKS` 不算，因为服务器时钟会先乱后稳）；简单的 tick 跳跃不算证据，因为卡顿能让 tick 跳最多 10 秒。
  * 严重级三档：Info/Warning/Edited，`Report::worst()/verdict()` 出判词。
* **自测**（:447 起）：`forgeries` 测试真的就地把 demo 剪开、重编号、改 header，再断言每一种改法都被抓到；`survey` 测试对 57 份真 demo 量基线。这是"带标签语料 + 篡改生成器"的验证范式。

**判断：这是 demo 篡改/损坏检测，不是渲染异常 tick 检测。** 但它的"量-判-分级-语料回归"四件套，正是我们 N11 需要的。

### 4.3 JankGuard：spawn/teleport tick 窗口

`src/util/nocrex/jankguard.rs`（6 KB）：逐 SteamID 记 `last_spawn`/`last_teleport`/`last_fire`；位移 > `TELEPORT_DIST=256` 记一次传送。上层算法对这些窗口做豁免——`src/lib/live_scan.rs` 注释写明"JankGuard 算法在 spawn 或 teleport 后忽略 60 tick"，`DEFAULT_LIVE_WARMUP_TICKS=130`、`LIVE_SCAN_OVERLAP_TICKS=64`。这是"异常窗口先标记、再决定信不信"的成熟做法。

## 5. 对我们项目的参考价值（具体到 N18/N11）

### 5.1 N18：武器体握点（最大的一条）

* 我们的读数：`armsGrip=weaponGrip=(-72.98,-26.10,-65.42) residual=0.0000`，但画面里武器体立在脚边（body-rifle 帧的 4447 px 差异就落在那里）。**residual=0 是矩阵空间残差，不是"手到武器的世界距离"**；它的兜底判据恰好补上这个盲点：
  * 世界距离 `|world[bip_hand_r] − world[weapon_bone]| > 12` → 不信任骨架摆位，改放到视图空间固定偏移。
* 可落地的验证：在 `native/src/viewmodel.cpp` 的 `weaponBodyReading` 里加一个 `weaponHandDistance=` 字段（解算后的世界距离），tick 2000 对比 `body-rifle` 的 4447 px 基线；如果距离 > 我们把门禁阈值，就加同样的"视图空间兜底"分支，再出像素差。
* 另一个可借的门禁：`is_out_of_view()` 时不画 viewmodel；我们的 N18 里"相机自藏"（entity 9）与它同源——它用 demo 的"录像者看不到"标志，我们用 hidden 集合，语义一致，可以参考它的降级策略（隐藏而不是插值）。
* 不要照抄：它把武器挂到 `weapon_bone` 后靠 `attached_world` 重算；我们是"伴生手模型 + 实体世界模型武器体"两条路都画（N18 已验），它的做法只覆盖第一人称，不覆盖世界实体。

### 5.2 N11：异常 tick 归因

* **眼高交叉验证**：N11 里 `offsetZ=68.0001`。它的 `eye_height()` 对"非 Scout、非 75 组"的职业正好返回 68（`recording.rs:157-167`）——与我们"这是正常眼高、异常在 origin 行而不是 offset"的结论一致（趋势一致，不是判决性证据）。
* **有效性门禁**：它的规则"活着且 `|body.position − view.origin| < 4` 才加眼高"，说明"view origin 可能是垃圾"是这类工具已知的失效模式；我们 N11 的 (0,0,-427) 行正是这种垃圾。可直接借"结构化无效 → 标脏 → 用上一有效行/隐藏"的策略，而不是继续插值。
* **完整性检测的量化阈值**：`CLOCK_SKIP_TICKS=66/2000`、`WARMUP_TICKS=200`、`HEADER_TICK_SLACK=1000` 是我们做"异常 tick 检测"时最好的起点参数（需在我们自己的 demo 语料上重新标定；它自述真实 demo 卡顿可跳 10 秒，这与我们 `REC tick 2000` 的退化/回跳经验吻合）。
* **带标签语料**：我们可以照它的 `forgeries` 思路，把 14 格矩阵 + pl_upward 的已知异常做成"注入式回归"（人工改 tick/offset 后断言检测器命中），而不是只靠人工看 trace。

### 5.3 解析层可借的冷知识

* `sanitize_demo_signon_ticks`（`helpers.rs:936-951`）：Source 1 demo 在 `dem_synctick` 之前的包带的是服务器 host tick（可能几万），会让 `CDemoPlayer::SkipToTick` 的相对 tick 直接越过目标；它把这些包的 tick 清零。我们若遇到"replay 快进失效/首包 tick 巨大"，这是已知原因。
* `reconstruct_simtick`（`helpers.rs:364-372`）：实体动画的 8 位网络值要按"每实体 base、每 100 tick 前进一格"的规则、用快照的 `NetTick` 还原成完整 sim tick（±128 回绕循环）。我们 `offset-out-of-range` 那一族如果碰到"动画 tick 对不上"，这是引擎侧的编码规则参考。

## 6. 不能照搬的

1. **许可证**：GPLv3。我们仓库没有 LICENSE，默认版权保留；把它的 `.rs` 整段搬进来会让我们的分发受 GPL 约束。**只读思路、自己重写**，并在提交信息/文档里注明参考来源。
2. **tick 模型**：它抽稀采样 + 插值，为"看得顺"服务；我们要"该 tick 是什么就是什么"，不能引入它的 `MAX_FRAMES` / `frame_at` 近似。
3. **相机启发式**：眼高查表、4 单位容差、300 单位不插值——都是 plausibility 取舍，不是取证口径。可以借"无效就降级"的思想，不能借具体常数当真相。
4. **资产栈**：它用四个 crate 直接读 VPK/BSP/MDL/VTF；我们是 C++ 原生管线，没有可移植性，只能对照"它怎么处理某类资产"。

## 7. 建议的最小验证（按性价比排序）

1. **N18 握点距离**（半天）：`weaponHandDistance` 读数 + 视图空间兜底 → `body-rifle` tick 2000 像素差；成功判据：4447 px 里至少大部分消失，且 `body-rifle-off` 仍只剩手。
2. **N11 异常注入回归**（1–2 天）：写一个只读检测器（包数/tick 单调/offset 窗口），先用 14 格矩阵 + pl_upward 标定阈值，再用注入式改动验证命中。
3. **完整性检测独立工具**（可选）：如果确实需要判"demo 是否被剪"，把 §4.2 的检查移植成我们自己的 `tools/` 脚本；先只做只读报告，不改 demo。
4. **文档化**：把"参考来源 + 只借方法不借代码"写进 `docs/TF2-SOURCE-REFERENCE-INDEX.md` 的同类条目，避免以后有人误拷。

## 8. 引用

* 本地 build 4：`C:\Users\Administrator\Downloads\TF2-Demo-Analyser-build-4\Demo Analysis\source\`（`view.rs` 240,529 B、`recording.rs` 76,146 B、`renderer.rs` 59,852 B）。
* 上游 main：`https://github.com/wombo14/tf2-demo-analyser`（`src/util/integrity.rs` 634 行；`src/integrity_view.rs`）。
* 发布说明：`https://github.com/wombo14/tf2-demo-analyser/releases.atom`（build 3–8；build 7 的 Demo integrity 段落在 2026-11-16 条目）。
* 本地 exe 取证：`grep -a -i anomal "Demo Analysis.exe"` 无命中（2026-11-20 本机）。
