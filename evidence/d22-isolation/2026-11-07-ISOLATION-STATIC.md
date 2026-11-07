# D-22 隔离启动 —— 静态证据与门禁自身控制（2026-11-07 task-24）

分支 `task/t94-hygiene`，基线 `d889376`（t86-phase0 tip，即 e15577b + task-18/20/22 文档与门禁），
本文件对应提交 `480d664`。**本文件只证明静态事实**：脚本层面的启动点、棘轮读数、门禁自身的负向/正向控制。
产品是否真的不再改写 `%LOCALAPPDATA%` 需要真实启动，见本目录的动态抽验文件（若尚未生成，则动态抽验未完成）。

## 1. 为什么做（D-22 的机制）

产品每次正常退出都写 `settings.ini`（`native/src/main.cpp:11613-11616`、`:11627-11629`），每个 `--demo` 启动都写 `ui.ini`
（`native/src/main.cpp:3240-3241` → `native/src/native_ui.cpp:196-197`，`:128-143` 维护 `recentDemo0..7`）。
因此门禁只要没传 `--settings-file` / `--ui-settings-file`，跑门禁就等于改用户配置。task-18 做了静态棘轮（能枚举），
task-24 做的是把 `verify-all.sh` 真正会跑的那批脚本改对，并把棘轮在 fresh checkout 上的假红修掉。

## 2. 改了哪 9 个脚本、20 个启动点

每个脚本只加一个 **ISO 块**（`ISO_DIR=$(mktemp -d ...)` + 两个 `ISO_SETTINGS`/`ISO_UI` 变量 + 把 `rm -rf "$ISO_DIR"`
**追加**到已有 EXIT trap 之后，不覆盖原 trap），并在每个产品启动点字面插入
`--settings-file "$ISO_SETTINGS" --ui-settings-file "$ISO_UI"`（字面量而非数组，保证棘轮静态可见）。

| 脚本 | 启动点（补的 flag 对） | ISO 块 | 追加 trap |
|---|---|---|---|
| `frame-capture-check.sh` | 2 | 1 | 1 |
| `idle-spin-check.sh` | 1 | 1 | 1 |
| `material-coverage-check.sh` | 1 | 1 | 1 |
| `player-animation-pixels.sh` | 1（原来是 shared：指向 `$OUT/settings.json`） | 1 | 1 |
| `review-workspace-check.sh` | 1 | 1 | 1 |
| `spectate-check.sh` | 9 | 1 | 1 |
| `viewmodel-animation-check.sh` | 1（原来是 shared：指向 `$TRACE_DIR/$name-settings.ini`） | 1 | 1 |
| `weapon-animation-check.sh` | 2 | 1 | 1 |
| `world-geometry-hole-check.sh` | 2（含 `:1490` 的 restored 运行） | 1 | 1 |
| **合计** | **20** | 9 | 9 |

复核命令：`git show 480d664 | grep -c '^+.*--settings-file "\$ISO_SETTINGS"'` → **20**；ui 同。
`bash -n` 对 9 个脚本 + 两个门禁 + `verify-release.sh` 全绿。

## 3. 棘轮读数（权威值）

- 基线前（task-18，t86 树，101 脚本）：`violations=85 行 / 47 脚本`。
- 基线后（本树，102 脚本）：`evidence/gate-launch-hygiene/baseline.txt` = **67 行 / 38 脚本**（85 − 9×2）。
- 实测末行：`GATE-LAUNCH-HYGIENE=PASS violations=67 baseline=67 warnings=23 lifecycle=1`（rc=0）。
- 哈希（仓库字节，`git show 480d664:<path> | sha256sum`）：
  - `gate-launch-hygiene-check.sh` = `06dee8f14d1fb94c7e51d255ec5663ac948b37898f5dc9fd12c1e06362d5d88b`（394 行）
  - `evidence/gate-launch-hygiene/baseline.txt` = `2f5b79cf19e5eb953e33d207b0a7171074865042dfb481a5109fdf78264c092f`（67 行）

## 4. 门禁自身 bug：fresh checkout 的假红（已修）

`t94-hygiene` 是 fresh checkout，`core.autocrlf=true` 把 `baseline.txt` 落成 CRLF，而扫描输出是 LF，
`comm` 集合完全不相交 → 一次跑出 **85 stale + 85 new**（rc=1）。这不是真违规，是门禁自己的行尾假设错误。

修法（`gate-launch-hygiene-check.sh`，两处）：
1. 读基线时 `tr -d '\r' < "$BASELINE" | sort > "$TMP/base.txt"`（含 6 行注释说明为什么 fresh checkout 必红是假红）；
2. `.gitattributes` 增加 `evidence/gate-launch-hygiene/baseline.txt text eol=lf`，让落盘、写盘、比较三处字节一致。

## 5. 控制（同一次会话实测，rc 与原文）

| 控制 | 做法 | 期望 | 实测 |
|---|---|---|---|
| NC1（CRLF 假红回归） | 把基线就地转成 CRLF 再跑 | PASS（不再假红） | `GATE-LAUNCH-HYGIENE=PASS violations=67 baseline=67 warnings=23 lifecycle=1`，rc=0；恢复 LF 后再跑仍 PASS |
| NC2（棘轮必须能红） | `--root .scratch/nc-root`：1 个脚本 + 空基线，脚本用 `EXE=...tf2_demo_native.exe` 后 `"$EXE" --demo x --audio-disabled` | FAIL，点名脚本与 flag | `FAIL: new unisolated launch: bad-launch.sh missing --settings-file` / `... missing --ui-settings-file`，`GATE-LAUNCH-HYGIENE=FAIL violations=2 baseline=0`，rc=1 |
| NC3（正控） | `--root .scratch/nc-pos`：同一脚本补两个隔离 flag + `--hidden-window --audio-disabled`，空基线 | PASS | `GATE-LAUNCH-HYGIENE=PASS violations=0 baseline=0`，rc=0 |

> NC2 第一次写坏：假脚本直接写字面 `"./.../tf2_demo_native.exe"`，扫描器的 `pvars` 为空（它只把**赋值过产品名**的变量认作启动者），
> 于是 `launchers=0` 直接 PASS——负控没红就说明负控写法不对，不是门禁对。改成 `EXE=...; "$EXE"` 后才得到期望的红。

## 6. 未证明的事（不要外推）

- 本文件**没有**启动产品，因此不证明“产品不再写 AppData”；动态抽验另记。
- 棘轮只覆盖静态可见的命令位启动（直写 / 数组调用 / 数组定义）；python heredoc、`.ps1`、函数包装器、
  `bash -c` 字符串仍不可见（`docs/T85-COVERAGE-MANIFEST.md` N27），本任务没有改变这个盲区。
- baseline 仍记着 **67 行未修**（38 个脚本），其中 29 个是 `verify-all.sh` 之外的脚本；清零是后续任务，不是本任务。

## 7. AppData 参考快照（本文件写作时，只读）

```
C:/Users/Administrator/AppData/Local/TF2 Demo Player/settings.ini
  sha256=dcd30ffe7b1aa60cd230edd3747a622206d3b2bb3bff3da55e984570ae69b827  bytes=2786  mtime=2026-11-07 14:03:07
C:/Users/Administrator/AppData/Local/TF2 Demo Player/ui.ini
  sha256=3e085d6be5b0a41555dce9f39c28ff92d730b219ba8b90435a2c8899e585aa88  bytes=978   mtime=2026-11-07 14:02:43
```

（动态抽验会在启动前后各记一次并断言 `APPDATA-UNCHANGED=YES`。）

## 8. 动态抽验：`DEFERRED-TO-INTEGRATION`（由 Lead 在合并树执行，本文件不含动态结论）

状态：**未执行**。本分支上排队过 `hostlock`（队位 7/7，锁主 `t85-budget` 长时间持锁），
Lead 决定改在**合并后的树**（`t91-integration`）上跑，结论由合并窗口产出。在那之前，
“产品真的不写 `%LOCALAPPDATA%`”这件事**未验证**，不要引用本文件去主张它。

脚本已提交：`evidence/d22-isolation/dyn-iso.sh`（133 行；`--plan` / `--skip-build` / `--tree DIR` / `--out DIR` / `--lease NAME`）。

先看计划（不构建、不启动、不需要锁）：

```bash
bash /d/TF2_Native_Worktrees/t94-hygiene/evidence/d22-isolation/dyn-iso.sh --plan --tree /d/TF2_Native_Worktrees/t91-integration
```

真正跑（必须包在锁里；`timeout` 由执行的 Lead 自己给）：

```bash
bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run t94-hygiene bash /d/TF2_Native_Worktrees/t94-hygiene/evidence/d22-isolation/dyn-iso.sh --tree /d/TF2_Native_Worktrees/t91-integration
```

（`--tree` 默认取脚本所在仓库根，所以先 `cd` 进目标树再省略也可以。）

### 8.1 它会 build 什么

`bash build-target.sh tf2_demo_native review_workspace_probe spectate_probe viewmodel_animation_probe weapon_animation_probe`

= 产品 exe + 4 个探针；后三个被三条像素/工作区门禁使用，`review_workspace_probe` 给 review-workspace 用。
build 失败打印 `BUILD rc=<n>` 与 `BUILD-ERROR <最后 3 行 error>`，然后 `DYNAMIC-ISO=FAIL reason=build`，退出码 **3**
——**这是构建/环境问题，不是隔离结论**。

### 8.2 它跑哪 6 条门禁（按顺序）

| # | 脚本 | 期望 token | 传给门禁的 OUT 环境变量 |
|---|---|---|---|
| 1 | `frame-capture-check.sh` | `FRAME-CAPTURE=` | `FRAME_CAPTURE_OUT` |
| 2 | `idle-spin-check.sh` | `IDLE-SPIN=` | `IDLE_SPIN_OUT` |
| 3 | `review-workspace-check.sh` | `REVIEW-WORKSPACE-CHECK=` | `REVIEW_WORKSPACE_OUT` |
| 4 | `spectate-check.sh` | `SPECTATE-CHECK=` | `SPECTATE_OUT` |
| 5 | `viewmodel-animation-check.sh` | `VIEWMODEL-ANIMATION=` | `VIEWMODEL_ANIMATION_TRACE_DIR` |
| 6 | `weapon-animation-check.sh` | `WEAPON-ANIMATION=` | `WEAPON_ANIMATION_OUT` + `WEAPON_ANIMATION_TRACE_DIR` |

每条输出 `GATE <script> rc=<n> token=<TOKEN=...> log=<path>`；红时紧跟一行 `GATE-FAIL-DETAIL <日志里最后 2 行 FAIL/ERROR/assert>`。
判定：`rc=0` **且** token 行存在 → 计入通过（脚本不校验 token 的值，只保证门禁确实打了收尾行）。

### 8.3 AppData 断言怎么写的

1. 开跑前对两个真实文件各记 `sha256` 与 `size:mtime`：`<LOCALAPPDATA>/TF2 Demo Player/settings.ini`、`ui.ini`
   （`cygpath -u` 归一）。打印 `APPDATA-BEFORE settings=… (bytes:mtime) ui=… (…)`。
2. 六条门禁跑完后各记一次，打印 `APPDATA-AFTER`。
3. 四项（两文件各自的 sha256 与 size:mtime）任意一项变化 → `APPDATA-UNCHANGED=NO` 并打印 `APPDATA-DIFF <before -> after>`；全同 → `YES`。
4. 收尾：`DYNAMIC-ISO=PASS gates=<n>/6` 仅当 6 条全过 **且** AppData 未变；否则 `DYNAMIC-ISO=FAIL gates_passed=<n>/6 appdata_touched=YES|NO`，退出码 **1**。

### 8.4 红了怎么一眼判断责任

- `DYNAMIC-ISO=FAIL reason=build|exe-missing`（rc=3）→ 构建/环境，不是隔离结论。
- `DYNAMIC-ISO=FAIL reason=tree-missing|not-a-repo`（rc=2）→ 传错树。
- `GATE <script> rc!=0` 且 `APPDATA-UNCHANGED=YES` → 该门禁自身红，与隔离无关。
- `APPDATA-UNCHANGED=NO` 且 6 条门禁全绿 → **D-22 未修好或产品另有写点**，看 `APPDATA-DIFF` 与对应门禁日志里的启动命令行。
