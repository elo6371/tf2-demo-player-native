# 更新日志

## 2026-10-28 — GitHub 页面与问题记录（未发布）

- 更新项目首页，注明公开默认分支的 T50-T54 状态，并区分未发布的后续本地开发。
- 新增已复现问题与修复状态页，链接到公共问题库中的重复 entity ID 读数案例。
- T61.1 的重复 entity ID 修复有 focused mutation 证据；其恢复门禁仍待复审，不作为验收结论。
- 清理旧任务/接手文档、未纳入 CMake 构建的旧 probe 源码及无当前引用的旧 P0 patch；产品行为未改，CMake 配置生成通过。
- 仓库仍处于开发阶段；本次没有创建发行版，也没有授予代码再使用许可。

## 2026-10-31 — release tree: this branch HEAD (integration/t79-final)

> 发布 HEAD 以本分支实际 HEAD 为准：`git rev-parse HEAD`；推送后 `git rev-parse p0-entity-protocol` 应与它相等。

- Release HEAD is the commit checked out here (T8/T9/T10 source-split merge on top of the `ebc4716` candidate). If `git rev-parse p0-entity-protocol` equals `git rev-parse HEAD`, the public branch has been fast-forwarded to this tree; if the two differ, this checkout is ahead of the public branch. The full `verify-all.sh` chain passed on the pre-split candidate `ebc4716` (evidence in `evidence/verify/`); the run on this merged tree is the shipped record and is appended below.
- README and native/README.md: build entry is `build-cmake.sh` -> `native/build-nmake/`; documented `--demo` and `verify-all.sh --quick`; corrected the HUD, VSync and WebView2 wording.
- Known: repository history still holds ~594 MiB of history-only blobs; deleting files in new commits does not shrink clone size, and no history rewrite was performed.

## 2026-11-01 — 验收链在合并后的树上跑完

- `bash verify-all.sh --quick` 在这棵合并树上端到端跑完（树 = `55560c3`，2026-10-31 23:12 → 2026-11-01 01:55，9,765 秒）：30 步逐步执行，**29 步 PASS**，第 30 步的红是 runner 自己的计数，不是测量结果。
- 那一步是第 27 步（SourceTV relay camera）。它的门禁打印 `SPECTATE-CHECK=PASS`、73 条断言全 OK（冻结窗口读到 14 行 handover、14 个不同 progress 值），但 `verify-all.sh` 拿它跟硬编码的 71 比。修好的门禁在"窗口内至少 8 帧"的主机上打 73、在饿帧主机上打 71，所以任何精确总数都会在另一种主机上假红；`fa1233f` 改成用**下限 71** 比较。
- `fa1233f` 只动 `verify-all.sh`：不动产品源码、不动门禁脚本、不动任何断言值、不动变异覆盖。改后在 `fa1233f` 上重测第 27 步（2026-11-01 11:00，整段 9 节门禁）：`SPECTATE-CHECK=PASS`、73 条断言、产品退出码 0。**因此 30 步在这个 HEAD 上都判 PASS。**
- 同窗口内还有两个同样不改变行为的门禁侧修正：`f25e5f4` 让冻结窗口的 handover 按契约读、而不是按主机帧率读；`55560c3` 把剩余的变异锚点跟到 renderer 拆分后的文件（修好了三个材质门禁的多点补丁，变异仍全部变红、源文件仍逐字节复原）。
- 原始日志、逐步 verdict 和 run 目录留在仓库外。`evidence/verify/` 保留它原有的旧快照；上面的数字就是那些日志里的读数。

## 2026-11-01 — 调试相机开关恢复生效（诊断路径，产品画面默认行为不变）

- `--debug-camera` 此前不起作用：调试机位在场景 tick 写入渲染器，随后被导演/relay 的姿势同步覆盖，画面落回轨道相机，而 trace 仍打印 `src=debug` 与请求的角度。实测两个相距 1500 单位的机位在同一 tick 产生**逐字节相同**的帧；把调试机位的 pitch 从 -80 改到 +80 也一样。
- 修复：在 `renderer.draw` 之前重述调试视图，并让 `scene: camera` 那一行打印**帧实际使用的眼位**。修复后两个机位差 99.37%，t2 的天空/水面两组对照由"空过"变回真测量（天空 100.0000%、水面 1.3930%）。
- 只影响显式传 `--debug-camera` 的诊断/门禁路径；不给这个开关时渲染分支与之前相同。受影响门禁已按开关真生效后的读数重测：t2-visual-matrix、hud-check、hud-text-check、player-animation-pixels、material-coverage、world-material-semantics。

## 2026-11-01 — 世界几何、PVS 与相机：T80/T82/T83 及相关门禁重钉

- **T80 相机**（`7762e67`、`0d41244`）：SourceTV 录制没有可用的观察者样本时，播放器不再从世界原点取景。`observerEyeUsable` 拒绝非有限与全零眼位；relay 路径把相机放进被观看玩家的眼里（eye = origin + 68 单位）并把该实体从自己的画面里剔除，只影响 relay 相机实例的计数，不动 T66 的 corpse 契约。实测 koth_ashville 抓帧的平均亮度 10.9 → 77.4、pl_upward 39.6 → 95.4。
- **T82 PVS**（`db652e9`）：面的 cluster 不再只取 BSP 里第一个提到的叶子，而是收全部包含它的叶子，绘制条件改成"任一叶子可见"（Source 的规则）。koth_ashville 在固定机位的精确 (0,0,0) 像素 47,103 → 24,182，画面变化 113,290 px。
- **T83 brush 几何**（`38d44f9`）：解析 lump-0 实体表，绘制 classname 为 `func_brush`/`func_wall`/`func_lod` 的静态模型面（门、可切换与无法分类的模型跳过并分别计数）。`pl_odyssey` 的面数 18,052 → 21,119、三角形 99,312 → 105,310；koth_ashville 的 (0,0,0) 像素进一步降到 14,162。
- **门禁按归因重钉，未放宽任何阈值**：visual-baseline（`f8b510c`，70/0）、world-material（`f544362`，144/0）、t2-visual-matrix（`7db4220`，61/0，天空/水面改为固定机位并断言相机身份）、material-coverage（`7ff6da7`，88/0）、world-material-semantics（`65d4206`，200/0，把"像素对塌成 0"改为诊断读数并在 T45 宽视角上重测）、hud 与 player-animation 证据（`6e31d06`）。
- **两个门禁在终树上必须改语义，均已写明理由**：`f6b961b` 让 weapon-animation 的三次抓帧钉死 `--debug-camera` 并断言相机身份，把"静止相机 ⟹ 同一张图"（只在 T80 之前的零眼位空镜下成立）换成"相机不动而画面前进"，断言数 65 → 66；`e5643cc` 按 T35/T58 的程序重钉 world-geometry-hole（missing 97,453 → 62,394 px，全部仍由天空面解释、unexplained 0；seam 探针从"每条都解析"改为钉住 10/16，因为它们现在由天空面围合而 face-id 帧不画天空）。
- **终树分步验证**（HEAD `44c8bad` + 上面两个门禁提交）：按 2026-11-01 的裁决不跑完整 30 步，只跑受影响的 20–26、28、29 九步，逐个 **PASS**：entity-material 24、animation-availability 23、weapon-animation 66、viewmodel-animation 154、viewmodel-companion 125、player-animation 232、animation-pixels 28、world-geometry 53、material-coverage 88。
- **仍未验证/已知限制**：整链没有在这棵终树上重跑（最后一次 30 步 PASS 在 `ebc4716`）；T52 的 8 个产品缺陷读数与 T66 的 7 项仍为 NOT VERIFIED；`src=held` 时 trace 仍打印 demo 请求的零眼位而不是帧实际使用的相机（T84 只修了 `--debug-camera` 那条路径），已作为已知限制另排一轮。

## 2026-11-02 — T52 轮廓门禁重钉、T66 七项收口、终树全链

- **T52 轮廓门禁：7 红 → 121 OK / 0 FAIL，变异 m1–m14 全红**（`cc11860` 深度视图标量化、`87ecadc` 夹具重钉、`660ff40` 记录该轮读数）。三条根因都不是"数字没调对"：① 抓帧 tick 由 probe 的 `serverTickOrigin` 推导，那个值从 10252 漂到 10320，派生出的 64161 帧上 11 个玩家**全部 onScreen=0**；② ⑦ 数"全帧队色像素"，而该机位正对红砖墙，on/off 两帧**逐字节相同**也有 7607 个"队色"像素；③ `--world-depth` 只改世界 pass 的着色器，天空盒是独立 pass，彩色天空盖在标量图上（同机位对照：关天空盒 nonGreyPct 27.61% → 0.00%）。
- **深度视图与遮挡**：深度视图不再画天空、clear 用黑，读数 nonGreyPct=0.00、nonzeroPct=73.66；另加 `--capture-depth`（16 位 D24 回读，distinct=3896、min=61090、max=65535），遮挡判断改用它而不是那张 8 位饱和图——8 位视图仍是饱和图，T26 的 `saturated=1` 不受影响。
- **A3 剪影**：draw→item 的 join 改为按 `entityIndex`（代码原注释自己写着 "the entity index is the join"，实现却用 cacheKey + 原点浮点比较），动画开启时 `poseWorld=2` 与 `scene: setInstances playerDraws=2` 相等；动画开/关同 tick 差 85 px，同配置复跑 0 px。
- **夹具**：抓帧 tick 与机位成对钉死（64250 / `5699 -1611 150 14.4 -143.3`），并固定 `--no-player-animation`（绑定姿势夹具）；⑦ 的判据改为"**发布区矩形内**的队色像素"（off 帧 0，m3 变异 1132）。
- **T66 七项 NOT VERIFIED 全部关闭**（`71911cc`，70 文件）：删除 `kSeekGapTicks=200` 的固定阈值，按播放速度算一帧该走多少 tick（16x 下 `resync=0`，8x/16x 同样 deaths=3 handovers=3）；真实 seek 接进门禁第 10 节（连跑三次读数一致：`before_tick=13748 → after_tick=12820`、`after_resync=1`、summary `resync=1`）；新增暂停中手动选人（第 9 节，`manual=1 rejected=0 notarget=0`）与 16x（第 11 节）两节；变异 8/8 全红、每例源文件逐字节还原，断言地板 71→93（健康宿主 95）。
- **终树全链**（HEAD `59d94a7`，16:41:24 → 19:34:41，`bash verify-all.sh --quick`）：30 步里 **29 步 PASS**，第 27 步在测量前被自己的 `BINARY-FRESH` 守卫拦下——第 8 步的变异套件恢复文件时刷新了源码 mtime（17:12），而探针是第 1 步编的（16:48），`git status` 干净、源码哈希与冻结值一致。修复是在第 27 步前重建 `spectate_probe`（与第 25 步重建 `model_pose_probe` 同一先例，守卫本身不放宽）；按用户决定**只重跑第 27 步**，读到 `SPECTATE-CHECK=PASS`、95 断言、0 FAIL，全链没有跑第二次。记录见 `evidence/t66-sourcetv/CHAIN-RUN-20261102.md`。
- 仓库仍处于开发阶段；本次没有创建发行版。


## 2026-11-03 — T79 跟拍相机：trace 改为记录"帧实际使用的镜头"

- **症状**：SourceTV 录制没有可用的 demo 眼位时，相机由 relay 自己放置（`native/src/main.cpp:10047`），但 `scene: camera` 那行在**决定处**就打印了，且 `eye=` 只由 demo 眼位或 `--debug-camera` 填充（`cameraEyeX/Y/Z` 初值 `0.0f`）。于是 KOTH 样本上 trace 恒为 `follows=0 holdsNoTarget=445 ... src=held eye=0.0,0.0,0.0`，而画面早已在被跟拍玩家的头里——与 T84 修 `--debug-camera` 时同一类缺陷：trace 记录的是**请求**，不是绘制。
- **修复**：`placeSpectatorView()` 抽出为返回实际姿势的函数（`native/src/main.cpp:360-399`，`outEye`/`outAngles` 可为 nullptr）；relay 守卫加 `!g_debugCamera`（`native/src/main.cpp:10043`），调试机位拥有自己的帧；trace 记账在 relay 之后、`renderer.draw()` 之前统一打印（`native/src/main.cpp:10085-10089`），relay 用实际应用的姿势覆盖决定处的种子（`native/src/main.cpp:8711-8713` → `:10062-10067`）；relay 即将放置的帧不再计入 holds（`native/src/main.cpp:8696`，`holdsNoTarget` 445 → 1）。
- **像素证明**（同 tick 2000、snakewater、1x）：R（relay 跟拍）vs D（`--debug-camera` 用 R 打印的 eye）只有 **638 / 860,784** 像素差 >8（0.074%，max 195，散在 77 个 64x64 格、最大格 54 px，与视图模型/远处实体的动画相位一致）；R vs D2（世界原点机位）**799,512** 像素差 >8（92.882%，max 247）。**修复前 R 打印的正是 D2 的姿势。** 产物与读数在 `evidence/t79-camera/`。
- **门禁**：`camera-ui-check.sh` **108 OK / 0 FAIL**（新增三条断言：511 帧带 `src=follow`、0 帧声称 `eye=0.0,0.0,0.0`、最后一条 `src=follow` 带 `target>=1`；修复前第一条读 0 并直接红）；同一把 host lock 下 spectate / frame-capture / demo-clock / outline 依次 **PASS**（合计 293 OK / 0 FAIL）。
- **边界**：KOTH 样本的地图 `koth_bagel_rc13` 本机未安装（空世界），所以 camera-ui 的 e2e 只证明 trace、计数与 HUD，画面结论来自 snakewater；本轮未跑 outline 的 `--mutation`，它重新引入的 8 个缺陷仍为 NOT VERIFIED。T79 关闭了 2026-11-01 那节记下的 `src=held` 已知限制。

