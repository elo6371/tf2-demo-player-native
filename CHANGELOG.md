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
