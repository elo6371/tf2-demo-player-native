# 更新日志

## 2026-10-28 — GitHub 页面与问题记录（未发布）

- 更新项目首页，注明公开默认分支的 T50-T54 状态，并区分未发布的后续本地开发。
- 新增已复现问题与修复状态页，链接到公共问题库中的重复 entity ID 读数案例。
- T61.1 的重复 entity ID 修复有 focused mutation 证据；其恢复门禁仍待复审，不作为验收结论。
- 清理旧任务/接手文档、未纳入 CMake 构建的旧 probe 源码及无当前引用的旧 P0 patch；产品行为未改，CMake 配置生成通过。
- 仓库仍处于开发阶段；本次没有创建发行版，也没有授予代码再使用许可。

## 2026-10-30 — release tree f23bf2a

> 发布时以实际合并后的 HEAD 为准（f23bf2a 是本提交所在树的基线；合并后的发布 HEAD 以 git rev-parse HEAD 为准）。

- p0-entity-protocol fast-forwarded to the release tree; the full `verify-all.sh` chain passed on the pre-split tree `ebc4716` (evidence in `evidence/verify/`). The T8/T9 source-split commits after it are refactors; re-run the chain on the final merged tree for the shipped record.
- README and native/README.md: build entry is `build-cmake.sh` -> `native/build-nmake/`; documented `--demo` and `verify-all.sh --quick`; corrected the HUD, VSync and WebView2 wording.
- Known: repository history still holds ~594 MiB of history-only blobs; deleting files in new commits does not shrink clone size, and no history rewrite was performed.
