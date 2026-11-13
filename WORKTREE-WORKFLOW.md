# 并行任务工作流

## 当前活动目录

| 任务 | 目录 | 状态 |
|---|---|---|
| 主线 | `D:\TF2_Native_Test` | 只做整合和文档，不运行活动任务 |
| T2 | `D:\TF2_Native_Worktrees\world-material-effects` | 已停止，待证据核对和推送 |
| T3 人工证据 | `D:\TF2_Native_Worktrees\manual-followup` | 已交付，保留证据 |
| T4 | `D:\TF2_Native_Worktrees\stability-release` | 已停止，待处理未提交源码和证据 |
| T2.5 参考 | `D:\TF2_Native_Worktrees\viewmodel-companion-render` | 保留未提交报告 |
| 世界渲染参考 | `D:\TF2_Native_Worktrees\world-render-followup` | 保留未提交证据 |

## 硬隔离规则

1. 每个任务只能读写自己的 worktree、`native/build-nmake`、`.scratch/<task>` 和 `evidence/<task>`。
2. 任务不得读写另一个任务的 `.scratch`、`evidence`、构建目录、脚本或 exe；需要历史证据时只能在交付后复制并记录来源哈希。
3. 任务启动前必须确认当前目录和 Git 分支：

   ```bash
   pwd
   git branch --show-current
   git status --short
   ```

4. 脚本必须使用任务根目录计算相对路径，禁止硬编码其他 worktree 或主线路径。
5. 每个任务使用唯一运行标签和唯一日志目录；禁止读取旧 trace、旧 BMP 或其他任务的输出。
6. 任务结束时先停止产品进程和脚本，再提交；交付报告必须列出未提交文件和证据来源。
7. 整合者只从已提交 commit 合并；未提交内容必须先由原任务负责人确认归属，不能直接捡取。

## 并行规则

- 编码可以并行，但必须使用独立 worktree。
- 构建、抓帧、mutation 若会争用 GPU、设备、全局资源或固定路径，应串行运行。
- T2 和 T4 不共享任何路径；T4 的 `main.cpp`、exe、稳定性脚本和 `evidence/stability-release` 对 T2 不可用。
- 总链只在所有任务合并后由主线整合者单实例运行。

## 清理规则

- 只删除工作树，不删除分支或提交：`git worktree remove <path>`。
- 只有工作树 clean、任务已结束且无需继续取证时才可删除。
- 有未提交源码、报告或证据的目录必须保留或先由负责人提交。
- 构建产物和 scratch 可以在任务交付、哈希归档后单独清理；已提交证据不得删除。
