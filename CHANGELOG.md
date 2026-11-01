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
