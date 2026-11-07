# T92 — 安装器 CLI 契约：dry-run 是一个模式，manifest 是 UTF-8

日期：2026-11-07　worktree：`D:/TF2_Native_Worktrees/t92-install`（分支 `task/t92-install`，基线 `e15577b`）
门禁脚本：`install-cli-contract-check.sh`（仓库根，487 行，sha256 见同目录 `gate-script.sha256`）

## 0. 状态（先读这一行）

代码与门禁已落盘并提交；**本 worktree 里 forward / mutation 没有跑绿**。第一次跑是红的，根因是门禁的前置条件（见 §5），不是产品缺陷；已在门禁里修掉。
绿不绿的实测由 lead 在合并窗口按 §3 的断言原文判读（本文件不主张任何未跑出来的 PASS）。

## 1. 两个真缺陷（缺陷本身的实测证据，来自 task-11，本 worktree 只读引用）

### 1.1 `--install-dry-run` 单独用不 dry-run，而是启动真实 GUI

- 旧代码：`native/src/main_lifecycle.cpp:28`（基线版本）只做 `request->options.dryRun = true;`，从不设置 `request->mode`；函数末尾 `return request->mode != LifecycleMode::None;` 返回 false，`main.cpp` 于是走正常产品启动路径。
- 实测 A（round1 C1，`timeout 300` 未触发、rc≠124）：同一行命令跑满 67.6 s（13:44:04.420→13:45:12.024）后以 **rc=1 自己退出**，stdout 0 字节、安装目录无变化。
- 实测 B（round2 R1）：起进程后 8 s 仍存活（`alive_after_8s=1`），`tasklist //v` 抓到真实窗口标题 `TF2 Demo Player - TF2 资源目录: D:\Stea…`，随后由脚本 kill，rc=143（=128+15）。
- 两条合起来＝单用该 flag 走的是 GUI 启动路径；B 不能推出「永久挂住」，A 不能推出「崩溃」。证据：`D:/_shared/t85-install/logs/c1-dryrun.*`、`logs/accept-results-r2.txt`、`logs/r1-*`；口径细节见 `D:/_shared/t85-install/INSTALL-ACCEPTANCE.md` §6.1。

### 1.2 manifest 的 installDirectory 写 ANSI/ACP，读侧却按 UTF-8

- 旧代码：`native/src/install_lifecycle.cpp:340`（基线版本）`manifest.installDirectory = installDir.generic_string();` —— MSVC 上 `generic_string()` 是**活动代码页字节**（本机 cp936）。
- 实测字节（中文安装目录 `D:/_shared/t85-install/work/验收 路径 空格/cjk-bytes`）：值 52 B，`matches_cp936=True`、`matches_utf8=False`；UTF-8 严格解码报 `can't decode byte 0xd1 in position 28: invalid continuation byte`，cp936 解码恰为原路径。
- 产品自己从不回读该字段（全树只有 3 处引用：写 :340、序列化 `install_manifest.cpp:189`、解析 `:255`），所以产品功能一直正常，只有外部工具/解析器看到乱码。证据：`D:/_shared/t85-install/logs/cjk-decode.txt`、`logs/cjk-installdir-hexdump.txt`、`logs/cjk-installdir-line.bin`。

## 2. 改了什么（文件:行，均为本 worktree 最终内容）

| 文件 | 行 | 改动 |
| --- | --- | --- |
| `native/src/main_lifecycle.cpp` | 42-44 | 读完整条参数表后：`dryRun && mode==None` → `mode = Install`（位置无关） |
| `native/src/main_lifecycle.cpp` | 48-52 | `dryRun` 与非 `Install` 的显式 mode 同时出现 → `dryRunConflict = true` 并 return true |
| `native/src/main_lifecycle.cpp` | 79-85 | 冲突分支：输出 `install=FAIL reason=dry-run-conflict mode=<uninstall\|install-status\|version> detail=--install-dry-run only modifies --install`，`exitCode = 2`，不碰磁盘 |
| `native/src/main_internal.h` | 61-64 | `LifecycleRequest` 新增 `bool dryRunConflict = false;` |
| `native/src/install_lifecycle.cpp` | 340-349 | 写侧改 `installDir.u8string()`（C++17 返回 UTF-8 字节） |
| `native/src/install_manifest.cpp` | 38-90 | 读侧新增 `validUtf8()` / `acpToUtf8()` / `installDirectoryAsUtf8()`：非法 UTF-8 → 按 ACP 重读，并打印 `manifest: legacy-acp installDirectory`（stdout + fflush） |
| `native/src/install_manifest.cpp` | 308 | 解析 `installDirectory` 时调用 `installDirectoryAsUtf8(value)` |
| `README.md` | 46-49 | 生命周期 flag 一段：dry-run 两种合法写法、冲突 exit 2、manifest UTF-8 与 legacy-acp 提示 |
| `install-cli-contract-check.sh` | 新增 487 行 | 门禁（见 §3），含 hermetic settings（§4.3） |

## 3. 门禁的四组断言（原文 + 期望 token，行号对应当前脚本）

判定读法：每行 `OK <claim> = <reading>` 或 `FAIL <claim> = <reading>, expected <token>`。**只看 rc 不够**——下表是每条断言的 claim 与它要求的 token。完整逐条清单（含 mutation 段）见 `assertions.txt`。

### A1 `--install-dry-run` 单独用 = dry run，不是产品启动（行 203-235）

| 行 | claim | 期望 |
| --- | --- | --- |
| 206 | `A1 the package staged` | `0` |
| 213 | `A1 the staged package carries a payload` | `yes` |
| 223 | `A1 exit code` | `0` |
| 224 | `A1 the answer is a dry run` | 输出含 `install=DRYRUN` |
| 225 | `A1 the plan lists the payload count` | 输出含 `files=4` |
| 227 | `A1 the directory did not change` | `same`（前后 `find` 清单逐字节相同） |
| 229 | `A1 no manifest was written` | `absent` |
| 231-235 | `A1 it answered inside 5 s` | 读数为毫秒数，必须 `< 5000` |

### A3 中文路径安装后 manifest 记录为 UTF-8（行 237-265）

| 行 | claim | 期望 |
| --- | --- | --- |
| 240 | `A3 the package staged` | `0` |
| 254 | `A3 install exit code` | `0` |
| 255 | `A3 the install succeeded` | 输出含 `install=PASS` |
| 256 | `A3 the payload was laid down` | 输出含 `files=5` |
| 258 | `A3 the manifest was written` | `yes` |
| 263 | `A3 the recorded value is valid UTF-8` | `valid_utf8=yes` |
| 264 | `A3 the recorded value is the installed path` | `equals_utf8=yes`（分隔符归一化后比较） |
| 265 | `A3 it is not ANSI bytes that only look like the path` | **不得**含 `cp936_reads_as_path=yes` |

### A2 / A2b dry-run 与别的 mode 并列 = 显式拒绝 exit 2（行 267-291）

| 行 | claim | 期望 |
| --- | --- | --- |
| 276 | `A2 exit code`（`--uninstall --install-dry-run`） | `2` |
| 277 | `A2 the refusal names the reason` | 含 `reason=dry-run-conflict` |
| 278 | `A2 the refusal names the mode it was given` | 含 `mode=uninstall` |
| 280 | `A2 nothing was uninstalled` | `same` |
| 289 | `A2b exit code`（`--install-status --install-dry-run`） | `2` |
| 290 | `A2b the refusal names the mode it was given` | 含 `mode=install-status` |
| 291 | `A2b no status answer was produced` | **不得**含 `status=VERIFIED` |

### A4 旧 ACP manifest 仍能读回并打出提示（行 293-321）

| 行 | claim | 期望 |
| --- | --- | --- |
| 301 | `A4 control: status exit code` | `0` |
| 302 | `A4 control: status is VERIFIED` | 含 `status=VERIFIED` |
| 303 | `A4 control: a UTF-8 manifest is not announced as legacy` | **不得**含 `manifest: legacy-acp installDirectory` |
| 307 | `A4 the fixture was rewritten to ANSI bytes` | `rewritten_to_cp936=yes` |
| 311 | `A4 the fixture really is not valid UTF-8` | **不得**含 `valid_utf8=yes` |
| 319 | `A4 legacy status exit code` | `0` |
| 320 | `A4 legacy status is still VERIFIED` | 含 `status=VERIFIED` |
| 321 | `A4 the ANSI fallback is announced` | 含 `manifest: legacy-acp installDirectory` |

### 两条变异（`--mutation`，行 379-483）

| 变异 | 做法 | 必须变红 |
| --- | --- | --- |
| M1 | 把 `request->mode = LifecycleMode::Install;` 换成 `(void)0;` 再重链 | A1：45 s deadline 内没有 `install=DRYRUN`（退化成 GUI 启动），gate 用 `taskkill //f` 收尾并记录残留产品数 |
| M2 | 把 `installDir.u8string()` 换回 `installDir.generic_string()` 再重链 | A3：manifest 值 `valid_utf8=no`（中文路径下必然） |

两条变异均要求「重链后 exe 哈希必须变」，否则报 `MUTATION-NOT-LINKED` 并 exit 90（与 `install-lifecycle-check.sh` 同语义）；变异结束把源文件按字节原样恢复并重链。

## 4. 怎么跑（给合并窗口照抄）

### 4.1 一行命令

```bash
bash install-cli-contract-check.sh --forward  "D:/_shared/t85-install/t92-gate"
bash install-cli-contract-check.sh --mutation "D:/_shared/t85-install/t92-gate"   # 必须在 --forward 之后、同一 OUT
```

在串行锁里跑（本机规矩）：

```bash
bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run <owner> \
  bash /d/_shared/t85-install/t92-run.sh
```

`t92-run.sh`（在 `D:/_shared/t85-install/`）一个租约内做：build 三个目标 → `--forward` → `--mutation` → 打 git status 与残留产品数；日志落 `D:/_shared/t85-install/t92-gate/logs/{prebuild,forward,mutation}.log`。

- **超时建议 1800 s**：干净构建约 4-5 分钟，forward 约 30 s，mutation 是 4 次重链（M1 那轮还要等 45 s deadline 再 taskkill）。
- **OUT 目录**：`--forward` 会自己 `rm -rf` 自己的 `cases/*`，复用目录没问题；但 mutation 会写 `$OUT/mutation/`，用它做证据时别在两个 gate 之间共用同一个 OUT。
- **前置**：门禁自己在 0/4 里 build `tf2_demo_native install_cli_probe native_install_smoke_probe` 三个目标（见 §5 为什么），所以不需要预构建。

### 4.2 退出码

`0` = 全部断言成立（forward）/ 两条变异都被抓住（mutation）；`1` = 有红读数；`2` = 用法错误；`90` = MUTATION-NOT-LINKED。

### 4.3 hermetic settings（门禁自身的卫生要求）

产品退出时无条件写设置与 UI 状态（D-22），会写真实 `%LOCALAPPDATA%\TF2 Demo Player\settings.ini` / `ui.ini`。门禁里 **8 处**产品启动全部带 `--settings-file` 与 `--ui-settings-file`，指向 `mktemp -d` 出来的目录；脚本开头 `trap cleanup_hermetic EXIT`（若已有 EXIT trap 则不覆盖），结束 `rm -rf`。日志第一行会打印 `hermetic settings: <dir>`。

## 5. 第一次跑为什么是红的（门禁前置，不是产品缺陷）

2026-11-07 15:24:03 拿到锁（owner=t92-install），BUILD_RC=0，exe sha256=`25018de73ea3a3bf2a2366f91d75edc04b451f33ee8ef8a297c2279f15a9f2b6`；`--forward` 结果 **31 checks / 14 FAIL**，mutation 因 forward 红未跑。

根因（`red-run-1/a1-stage.log` 原文）：

```text
-- Installing: D:\_shared\t85-install\t92-gate\cases\a1/tf2_demo_native.exe
CMake Error at native/build-nmake/cmake_install.cmake:40 (file):
  file INSTALL cannot find
  "D:/TF2_Native_Worktrees/t92-install/native/build-nmake/native_install_smoke_probe.exe":
  File exists.
```

阶段目录里只有产品本体（`red-run-1/a1-stage-inventory.txt`）：`tf2_demo_native.exe\t2766336`。
即：`native/cmake/tf2-package.cmake:30` 的 `install(TARGETS tf2_demo_native native_install_smoke_probe …)` 需要一个**尚未构建**的目标产物，`cmake --install` 装完 exe 就报错退出，`.uninstall/payload` 没生成 → A1/A3/A4 全部拿不到 payload 而红（A1 的 `install=FAIL reason=no_payload payload=…/.uninstall/payload` 就是这么来的）。
`install-lifecycle-check.sh` 不暴露这个问题，因为它的 stage 前置默认 build tree 里已经有该 exe；**fresh worktree 没有**。

修法（只改门禁，不改产品）：0/4 里自己 build 三个目标并断言 `build rc = 0`；A1/A3/M1/M2 的 stage 各加一条 `the package staged = 0`，失败时打印 cmake 原始报错行 + stage 文件清单（写 `logs/*-stage-inventory.txt`）。

这次红跑里**仍然成立**的读数（`red-run-1/forward.log` 的 OK 行）：A2/A2b 共 8 条全绿（`exit=2`、`reason=dry-run-conflict`、`mode=uninstall` / `mode=install-status`、目录未变、无 `status=VERIFIED` 输出）——即冲突拒绝逻辑在真实二进制上已经生效；A1 的「目录未变 / 未写 manifest / 451 ms 内返回」也成立（只是没进 dry-run 分支）。

## 6. 已验证 / 未验证

已验证（本 worktree，有实测输出）：

- `bash -n install-cli-contract-check.sh` 通过（487 行）。
- 门禁前置构建：`build-target.sh tf2_demo_native install_cli_probe native_install_smoke_probe` BUILD_RC=0，exe sha256=`25018de7…9f2b6`（见 `build-here.txt`）。
- helper 的字节判读在两种路径分隔符下都正确（`helper-offline-test.txt`）：UTF-8 + 反斜杠路径 → `valid_utf8=yes`、`equals_utf8=yes`、`cp936_reads_as_path=no`；同一行走完 `legacy` 重写 → `valid_utf8=no`、`cp936_reads_as_path=yes`。
- A2/A2b 冲突拒绝：真实二进制 8 条断言全绿（`red-run-1/forward.log`）。

**未验证**（必须在合并窗口的合并树上跑 `--forward` / `--mutation` 才算）：

- A1：单独 `--install-dry-run` → `exit 0` + `install=DRYRUN files=4` + <5 s。
- A3：中文路径安装后 manifest 值 `valid_utf8=yes` / `equals_utf8=yes`。
- A4：改写为 ACP 字节后 `--install-status` 仍 `status=VERIFIED` 且打出 `manifest: legacy-acp installDirectory`；控制组不打。
- M1 / M2 两条变异是否确实变红（以及重链哈希确实变化）。
- 读侧 `acpToUtf8` 在**非中文** ACP 机器上的行为（本机 cp936 只覆盖这一种代码页）。

## 7. 本目录清单

| 文件 | 内容 |
| --- | --- |
| `README.md` | 本文件 |
| `assertions.txt` | 脚本里全部断言逐行摘录（含 mutation 段），行号对应当前提交 |
| `gate-script.sha256` | 门禁脚本 sha256（`747ca276…1cb26` 若与当前文件不符，以当前文件重算为准） |
| `red-run-1/a1-stage.log` | 第一次红：`cmake --install` 原始报错 |
| `red-run-1/a1-stage-inventory.txt` | 第一次红：失败 stage 里实际只有 `tf2_demo_native.exe` |
| `red-run-1/a1-out.txt` | 第一次红：`install=FAIL reason=no_payload …` |
| `red-run-1/a3-stage.log` | 第一次红：中文路径的 stage 同样报错 |
| `red-run-1/forward.log` | 第一次红：31 checks / 14 FAIL 全量输出 |
| `red-run-1/build.log` | 第一次红的构建日志（BUILD_RC=0） |
| `helper-offline-test.py` / `.txt` | 字节判读 helper 的离线验证（两种分隔符、legacy 重写） |
| `build-here.txt` | 本 worktree 构建产物 sha256（修改后源码） |