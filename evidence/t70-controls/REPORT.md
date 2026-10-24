# T70 精细回放控制 — 交付报告

## 1. 目标 / 非目标

**目标**（任务清单 §12 T70）：帧步进、1 秒 / 5 秒短跳、速度档位；每次请求必须有一个可命名的落点，
被取代的请求不能被静默丢弃；边界（越界 / 不可达）必须有明确状态；键盘步进与文本输入焦点冲突要有规则。

**非目标**：T55 视频导出；T71 工作区布局持久化；T72 书签；T66 死亡接力镜头。本任务不改变任何画面像素，
只改变"看哪一帧"的控制器语义。

## 2. 归属 / 基线

| 项 | 值 |
| --- | --- |
| 负责人 | lead（T70 的探针修复、门禁修复、报告由 lead 完成） |
| 工作树 | `D:/TF2_Native_Worktrees/t70-controls` |
| 分支 | `task/t70-controls` |
| 基线 commit | `7b447a3`（Merge T60） |
| 代码 commit | `bf06eea` |
| 推送 | 无（按规则不 push） |
| 集成 | 待合并进 `p0-entity-protocol` |

## 3. 改动文件与理由

| 文件 | 改动 | 理由 |
| --- | --- | --- |
| `native/include/playback_control.h` | 新增，178 行 | 控制器契约：`PlaybackPhase`(Idle/Paused/Playing/Seeking/Ended)、`PlaybackRequestKind`(SetPaused/StepTicks/JumpTicks/JumpSeconds/SetSpeed)、`PlaybackRequestStatus`、`FrameStatus`、`SceneQueryStatus`；`PlaybackRequest` 带单调 id；`PlaybackControl` 单一入口。 |
| `native/src/playback_control.cpp` | 新增，约 578 行 | 状态机与落点逻辑；请求 id 与落点 id 的配对；被取代请求计数；不可达 / 钳制两种边界语义。 |
| `native/tools/playback_control_probe.cpp` | 新增，345 行 | 无窗口探针：直接驱动控制器，产出 A–E 段的全部读数。 |
| `native/src/main.cpp` | +429 / −48 | 产品接线：`g_playbackControl`（492 行）、请求入口（498–509）、标题栏 `playbackSuffix`（5737）、`fullTitle`（5851）、时钟推进 `advance()`（8576）、场景解析 `pendingRequestId/needsSceneResolve/noteSceneResolved`（8629–8724）、`playback: state ...` 追踪（9960–9991）。 |
| `native/src/native_ui.cpp` | +1 / −1 | `kSecondStepTicks` 66 → 67（66.6667 ticks/s 下 1 秒四舍五入是 67，DESIGN 写明 1 s = 67）。 |
| `native/CMakeLists.txt` | +11 | `playback_control_probe` 目标；`playback_control.cpp` 加入 `tf2_demo_native` 与探针。 |
| `playback-control-check.sh` | 新增，511 行 | 门禁：A–E 段 + 焦点/落点映射 + 缺 demo 负控 + G 段产品接线；`--mutation` 跑 m1–m4。 |

## 4. 命令与退出码

```
# 构建（门禁内部调用 build-target.sh，见 evidence/t70-controls/gate/build-baseline.log）
bash playback-control-check.sh              # FORWARD_RC=0
bash playback-control-check.sh --mutation   # MUT_RC=0
```

| 命令 | 退出码 | 日志 |
| --- | --- | --- |
| `bash playback-control-check.sh` | 0 | `evidence/t70-controls/forward4.log` |
| `bash playback-control-check.sh --mutation` | 0 | `evidence/t70-controls/mutation1.log` |
| 各次变异体重建 | 0（m1–m4 及 4 次恢复构建） | `evidence/t70-controls/gate/build-*.log` |

## 5. 实际断言 / 读数（forward4.log，22 checks，0 failures，39 steps）

- **A 帧步进精确**：`step-forward #1 requested=101 landed=101 status=ok playing=0`；`#2 102/102`；`#4 104/104`。
- **B 短跳不改传输状态**：`jump-1s requested=167 landed=167`，`playing (paused by the seek before it)=0`；
  `resume playing=1`；`jump-5s requested=500 landed=500 playing (unchanged)=1`。
- **C 速度档位**：0.125x=8、0.25x=16、0.5x=33、1.0x=66、2.0x=133、4.0x=266、8.0x=533 ticks/s；
  `2.0 vs 2x1.0 delta = 1 (<= 2)`；`speed-change does not move tick: checks ok=7 failed=0`。
- **D 被打断的 seek**：`superseded request id=1 (!=0)`、`pending request id=2`、`superseded is the displaced
  request, not the pending one = 1 (!=2)`、`interruptedSeeks=1`、`final landing id is the pending request=2`、
  `final landing cursor=4000 status=exact`。
- **E 不可达**：strict 模式 `requested=1001 landed (cursor unchanged)=100 status=unreachable`、
  `requested=-1 landed=100 status=unreachable`；clamp 模式 `requested=1050 landed=1000 status=clamped`、
  `requested=-50 landed=0 status=clamped`。
- **焦点**：`keyboard step refused while a text field has focus = rejected`，`focus-suppressed count = 1`。
- **落点映射**：`landing checkpoint status=1 delta=1`、`landing before-window status=1`。
- **F 缺 demo 负控**：`exit code = 1`、`error=demo-not-found`、`step status = rejected`。
- **G 产品接线（真实产品 + 相机脚本通道，读产品自己的 trace）**：
  `cmd=seek result=ok`、`kind=jump-ticks source=camera-script requestedTick=100`、
  `kind=step-ticks ... requestedTick=101`、`kind=jump-seconds ... requestedTick=168`、
  `kind=set-speed ... speedAfter=2.000000`、`playback: state phase=paused cursor=168`、
  `title: ... req=168 act=168 frame=`（最后一行与第一行都命中）。

## 6. 变异捕获与恢复证明（mutation1.log）

| 变异体 | 改动 | 读数（被点名的断言） |
| --- | --- | --- |
| m1 | 步进从"落点游标"改成"当前时钟" | `FAIL A step-forward #1 requested = 102, expected 101` |
| m2 | `setSpeed` 只改显示不改速率 | `FAIL C speed 0.125 ticks/s = 66, expected 8` |
| m3 | 去掉边界钳制 | `FAIL E strict end landed (cursor unchanged) = 1001, expected 100` |
| m4 | 忽略 seek 打断 | `FAIL D superseded request id = 0, expected != 0` |

每个变异体：`restored playback_control.cpp byte-identical (cmp)` + `mNr suite failures = 0`（恢复后基线重新变绿）。
末行：`MUTATION-CAUGHT=PASS (4/4 mutations red their named assertion and restore byte-identical)`，`MUT_RC=0`。

**注意**：m3、m4 的探针退出码是 1（探针在断言失败时返回非零），这不是"没跑起来"：门禁先从
`$OUT/mN-checks.txt` 读出被点名的 FAIL 行再判定，见 `suite_after_mutation`（`playback-control-check.sh:398-412`）。

## 7. FAIL / INVALID / NOT VERIFIED

- **FAIL**：无（forward 0 failures；mutation 0 green）。
- **INVALID**：无。
- **NOT VERIFIED**：
  1. 真实鼠标 / 键盘事件（`WM_KEYDOWN`、`WM_LBUTTONDOWN`）经由窗口消息循环的路径未验证——G 段走的是
     产品自带的相机脚本通道（`source=camera-script`），焦点规则只在探针层面验证。
  2. 反向播放（`g_playback.reverse`）与速度档位的交互未验证；`PlaybackControl::advance` 的正负号只由
     `syncTransport` 传入，本门禁没有反向用例。
  3. 8x 以上的速度档位、以及速度档位与 T66 接力镜头 / T67 HUD 面板同时开启的交互未验证。
  4. 只有 D3D11 隐藏窗口路径（`--hidden-window`）；独占全屏、多显示器、音频开启状态未验证。
  5. 只在一个 demo 上跑（`koth_bagel_rc13`，44.7 MB）；没有在 cp_snakewater / POV demo 上重复。

## 8. 输入与二进制哈希

| 项 | 值 |
| --- | --- |
| demo | `D:/TF2_Demo_Player/testdata/demos/4a9bfb9276509d0ec5f5fdc722a95b17_match-20260927-0239-koth_bagel_rc13.dem` |
| `tf2_demo_native.exe` | `490267cb4b6ec5dc27573a05ab6a264b2a0f6ab987b983d2ee7c0120eeb98ae7` |
| `playback_control_probe.exe` | `bad84bf7de3f09c40219148fb0afd47b51a0bcd1ecddefae48a9cc5211535d41` |
| 源码树状态 | `git status --porcelain -- native` 只有本任务自己的改动（提交后为空） |

## 9. 证据路径

```
evidence/t70-controls/
  REPORT.md              （本文件）
  forward4.log           forward 22 checks / 0 failures
  mutation1.log          m1-m4 全红 + 字节级恢复
  build-forward2.log     基线构建
  gate/                  每次变异体与恢复的构建日志、G 段产品 trace / settings / 截图输入
```

## 10. 构建 / 进程 / 锁 结束状态

- 构建：`build-target.sh playback_control_probe tf2_demo_native` → rc=0，0 errors。
- 进程：门禁结束后无 `tf2_demo_native.exe` / 探针 / 编译器进程残留（每次运行由 `hostlock.sh run` 包裹，
  退出即释放锁）。
- 锁：最后一次 mutation 运行由 `lead-t70m` 持有，退出后 `status` 显示 FREE。
- 工作树：提交后 `git status --porcelain` 为空。

## 11. 下一步

1. 合并 `task/t70-controls` → `p0-entity-protocol`（预计冲突点：`native/src/main.cpp`、`native/CMakeLists.txt`，
   因为 p0 已经合入 T67/T68/T63-F1 的同文件改动）。
2. 合并后重建（`build-cmake.sh`，`exe_count` 应为 51：T70 新增 `playback_control_probe`），
   更新 `verify-all.sh` 的 `^exe_count=51$` 钉并写明理由。
3. 在集成树上重跑 `playback-control-check.sh` 的 forward 与 mutation（合并可能改变 main.cpp 的接线顺序）。
4. T70 的 NOT VERIFIED 第 1 项（真实窗口输入）留给 T73 前的统一验收，或单独补一个 `--input-test` 通道。
