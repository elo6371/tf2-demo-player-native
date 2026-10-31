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
