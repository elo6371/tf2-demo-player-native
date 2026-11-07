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
