# N22 — CI 与工具链便携：草案（arch-ci，2026-11-14）

> 作者：arch-ci（角色 F）。写域：docs 新增。**未改任何源码、CMake、脚本、workflow，未提交 git。**
> 基线：candidate/e5 @ a901ece，工作树 D:/TF2_Native_Worktrees/t105-e5。

## 0. 一句话结论

**这个仓库今天没有 CI，而且已有的那条 CMake preset 在这台机器上是坏的**（`native/build/CMakeCache.txt` 里 `CMAKE_MT:FILEPATH=CMAKE_MT-NOTFOUND`，exe_count=0）。但做 CI 并不需要先解决它：`verify-fast.sh` 已经是天然的 tier-1 CI 作业 —— 它不读 demo、不开窗口、不碰音频设备（脚本头注释原话，我用 grep 复核为 0 命中 TF 资产路径）。真正的阻塞只有两个，而且都小：工具链发现写死在一台机器上，以及行尾会让某些静态检查假红。

## 1. 现状（全部实测，不是转述）

### 1.1 没有任何 CI 配置

- `ops_glob` 全树 `.github/**`、`*.sln`、`Directory.Build.props` → 唯一命中是 `native/CMakePresets.json`。**没有 `.github/`**。
- `.gitlab-ci.yml` / `.travis.yml` / `azure-pipelines.yml` / `Jenkinsfile` / `appveyor.yml` → 全部不存在（`ls` rc=2）。
- 远端**存在**：`origin git@github.com:elo6371/tf2-demo-player-native.git`（fetch+push）。所以「没有 workflow」不是「没有远端」，是仓库里真的没写。

### 1.2 现有的 preset 在本机跑不通（这是本轮新发现）

`native/CMakePresets.json`（606 B）只有一个 configurePreset `windows-release`：generator `Visual Studio 17 2022`、architecture `x64`、binaryDir `${sourceDir}/build`。

而 `native/build/CMakeCache.txt`（mtime 2026-11-11 23:03:35）记录了这个生成器的实际结局：

```
CMAKE_GENERATOR:INTERNAL=Visual Studio 17 2022
CMAKE_GENERATOR_INSTANCE:INTERNAL=C:/Program Files/Microsoft Visual Studio/2022/Community
CMAKE_MT:FILEPATH=CMAKE_MT-NOTFOUND
exe_count=0
```

`CMAKE_MT-NOTFOUND` = `mt.exe` 不在 PATH。**preset 指向的这条路径在这台机器上从没产出过一个 exe**，实际在用的构建树是 `native/build-nmake`（NMake 生成器 + 手工设 INCLUDE/LIB）。结论：**preset 不是「另一种可行配置」，它是一条从未验证通过的岔路** —— 谁先把它接进 CI，谁就先拿到一个必然红的流水线。

### 1.3 工具链硬编码：只有两处，四行常量

| 文件 | 行 | 内容 |
|---|---|---|
| `env-msvc.sh` | :4 / :5 | `MSVC_VER="14.44.35207"` / `SDK_VER="10.0.26100.0"` |
| `env-msvc.sh` | :6-9 | 写死 `C:/Program Files/Microsoft Visual Studio/2022/Community/...` 与 `C:/Program Files (x86)/Windows Kits/10` |
| `build-cmake.sh` | :30 / :31 | 同样两个版本常量（**不 source** `env-msvc.sh`，自己又写了一份） |

消费者只有一个：`build-target.sh:61` `source ./env-msvc.sh`。`build-cmake.sh` 独立。所以**要改的只有 2 个文件共 4 行常量 + 2 处根路径**。grep 硬编码版本有 155 命中，但绝大多数是 `evidence/*.log` 里的包含文件路径噪音（`...\14.44.35207\include\xutility`），不是配置——**不要把那个 155 当工作量**。

## 2. 必须先更正的一条既有诊断

`build-cmake.sh:8-9` 与 `env-msvc.sh:2` 都写着：**「`vcvars64.bat` 不能用，因为它调 `reg.exe`，而 `reg.exe` 被沙箱拦」**。

实测这条不成立（至少部分不成立）：

- `reg.exe` **存在且能运行**：`/c/windows/system32/reg.exe` 在 PATH 上，`reg query "HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion"` 能正常执行。
- 失败原因是**键不存在**，不是被拦：`reg query "HKLM\SOFTWARE\Microsoft\VisualStudio\SxS\VS7"` → rc=1，原文 `ERROR: The system was unable to find the specified registry key or value.`；`HKLM\SOFTWARE\WOW6432Node\Microsoft\VisualStudio\SxS\VS7` 同样 not found。VS2022 根本不再写这个 VS7 老键。
- 而 1.2 已经给出 VS 生成器真正的失败原因：`CMAKE_MT-NOTFOUND`。

**我没有验证「现在把 mt.exe 补进 PATH 后 VS 生成器是否可用」** —— 那是构建动作，本轮锁被占（见 §6）。所以报告只能写：「成因诊断需要更正，可用性未验证」。写进这里是因为，如果照旧文档的说法去设计 CI，会得出「MSVC 必须手工发现」的错误前提。

## 3. 工具链发现原型（已实测，四条路径全过）

原型 `.scratch/n22/env-locate.sh`（3,614 B，**未提交、不是仓库脚本**）三段式设计：

1. **显式覆盖优先**：`TF2_MSVC_VER` / `TF2_SDK_VER` / `TF2_MSVC_ROOT` / `TF2_SDK_ROOT`。给 CI 或另一台机器时不需要改文件。
2. **发现**：先 `vswhere.exe`（`-latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`），失败则枚举 `"/c/Program Files[/ (x86)]/Microsoft Visual Studio"/*/*/VC/Tools/MSVC`。
3. **取最新 + 断言**：`sort -V | tail -1` 选工具集/SDK，MSYS→Windows 路径转换后写 PATH/INCLUDE/LIB，然后**断言 cl.exe / rc.exe / mt.exe 都在 PATH、include 目录都存在**，最后打印一行机器可读摘要，任一缺失 `exit 1`。

实测四条路径：

| 路径 | 命令 | 结果 |
|---|---|---|
| A 默认发现 | 直接跑 | `status=OK` rc=0，解析出 `msvc=14.44.35207 sdk=10.0.26100.0` |
| B vswhere 不可用 → 枚举回退 | `TF2_NO_VSWHERE=1` | **与 A 完全相同**，证明回退分支真的能用 |
| C 显式覆盖 | `TF2_MSVC_VER=... TF2_SDK_VER=...` | `status=OK` rc=0 |
| D 故意喂错版本 | `TF2_MSVC_VER=99.99.99999` | `status=FAIL` **rc=1**，stderr 打出 `TF2-TOOLCHAIN: cl.exe not on PATH` + `MSVC include missing under …` |

**D 这条比 A 重要**：它证明「找不到工具链时会响」，而不是静默用一个半配置的环境继续编译（那正是 `build-cmake.sh` 那两个坑的成因）。

## 4. CI 该跑什么、不该跑什么

### 4.1 能进每提交 CI 的：`verify-fast.sh`（tier-1）

我把它的依赖实测了一遍：

- `grep -E "TF_ROOT|tf_root|STEAM|/d/SteamLibrary|--audio-disabled|TF2_ROOT" verify-fast.sh` → **0 命中**。
- 头注释 :14-16 原话：「This script answers that question in seconds. It builds the program and the self-checking probes, then runs their `--self-test` entry points. **Nothing here reads a demo, opens a window, or touches an audio device.**」
- 它需要的东西只有两样：**MSVC + 仓库**。这正是 N22 第③条要的「无私有 TF 资产的稳定快测」。

它做的事：`:89-96` 构建 7 个目标（`tf2_demo_native` + 6 个 probe）→ `:197-215` 跑 6 组自测共 43 条断言 → `:220-224` 跑 `gate-registry-check.sh`（纯静态）→ `:267-271` 要求 `bad==0 && ok==FAST_ASSERTIONS && reg_rc==0`。

**特别值得进 CI 的是它的 mutation 段（`:226-249`）**：`mutate_case` 先 `cp` 捕获文件、`sed -i` 扰动、然后 `if cmp -s "$file" "$copy"; then echo "MUTATION-SETUP=FAIL (... the real capture does not contain the needle)"` —— 它区分「红线因为断言对」与「红线因为针根本没找到」。这是 `docs/GATE-DESIGN-TRAPS.md` 第②条（红基线上的 always-green 镜像）在代码里的正解，CI 里应该默认开（`--mutation`）。

### 4.2 不能进每提交 CI 的

`verify-all.sh`（97,729 B）与 `verify-release.sh`（23,383 B）里的多数步骤需要：私有 TF 安装（`/d/SteamLibrary/...`）、demo 语料（`E:/demo测试/`）、真 GPU、音频设备、以及每次几十分钟的 mutation/抓帧。这些是**发布窗口**的事，不是每提交的事。把全链塞进每提交 CI 的结果是可预期的：一个长期红、被忽略的流水线。

### 4.3 一个诱人但会假红的静态检查：`bash -n` 全树

我试过：对 `*.sh native/**/*.sh`（**136 个**）跑 `bash -n` → **21 个报语法错**，形如：

```
verify-all.sh: line 369: syntax error: unexpected end of file from `if' command on line 368
history-coverage-check.sh: line 74: syntax error: unexpected end of file from `if' command on line 73
mutate.sh: line 310: syntax error: unexpected end of file from `if' command on line 309
```

**这 21 个是假的**，来自检出时的行尾，不是仓库缺陷。证据链：

1. `od -c` 显示工作树是 CRLF，且断在续行反斜杠之后：`... " \` `\r` `\n` `   && grep ...`（`verify-all.sh:368-370`）。bash 的续行是「反斜杠 + LF」，中间多一个 CR 就不续行了，`if` 于是永不闭合。
2. `git show HEAD:verify-all.sh` 的 **blob 是 LF**（同一位置是 `\` `\n`）。
3. 决定性对照：把 LF blob 写到临时目录再 `bash -n` → `verify-all.sh` / `history-coverage-check.sh` / `mutate.sh` **全部 rc=0**；同一批 CRLF 工作树副本 → 全部 rc=2 且报同样的错。

**但结论不能停在「这是行尾问题」**，因为它同时依赖 shell 实现：同一个 CRLF 文件，Git Bash 5.3.9（x86_64-pc-cygwin）`bash -n` 返回 **0**，而这个会话用的 niubash 5.3.20（x86_64-pc-msys）返回 **2**。极简复现 `if true \`+CRLF+`&& true; then ...` 在 **Git Bash 下 `bash -n`=0 但实际执行 rc=2**（同一二进制内部都不一致）。

还有个**未解释的矛盾**，我如实记下：`git config --list --show-origin | grep -iE "autocrlf|eol"` 无任何设置，`git check-attr text eol -- verify-all.sh` 是 `text: unspecified` / `eol: unspecified`，而 `.gitattributes`（44 行）只对 `evidence/` 若干目录标 `-text`、对 `evidence/gate-launch-hygiene/baseline.txt` 标 `text eol=lf`。**没有任何属性解释「按 LF 提交、按 CRLF 检出」这件事**，而 `.gitattributes:5` 和 `:13` 的注释却声称「core.autocrlf=true is on for this checkout」——实测那个配置为空（`git config --get core.autocrlf` rc=1）。我不知道 CRLF 是谁写进去的。

**给 CI 的直接取舍**：要么先把 `*.sh text eol=lf` 钉进 `.gitattributes`，要么在 CI 里显式 `git config core.autocrlf false` 再检出。**在有结论之前，不要把裸 `bash -n` 全树扫描当门禁** —— 它会稳定地把 21 个好文件判红，然后所有人学会忽略它。

## 5. CI 工作流草案（未提交，仅供 lead 决定）

下面这段**没有写进仓库**（`.github/workflows/` 不在我的写域）。它只描述一个我认为诚实的起点：

```yaml
# draft only -- NOT committed. Path would be .github/workflows/tier1.yml
name: tier1
on:
  push: { branches: [main, candidate/*] }
  pull_request:
jobs:
  fast:
    runs-on: windows-2022          # 自带 VS2022 + Windows SDK，mt.exe 在 PATH
    steps:
      - uses: actions/checkout@v4
        with:
          # 见 §4.3：不改行尾，否则 21 个脚本会被 bash -n 判红
          # （本步只是止住「进一步」的改写；工作树里已有的 CRLF 仍需单独处理）
          fetch-depth: 0
      - name: toolchain
        shell: bash
        run: bash tools/tf2-toolchain.sh      # = §3 原型的落地版，失败必须非零退出
      - name: tier1
        shell: bash
        run: bash verify-fast.sh --mutation
      - uses: actions/upload-artifact@v4
        if: always()
        with: { name: tier1-evidence, path: evidence/fast }
```

落地这个草案需要三件事，按顺序：**① 把 §3 原型变成 `tools/tf2-toolchain.sh`（或直接改 `env-msvc.sh`，四行常量）并让 `build-target.sh` 继续 source 它；② 决定 `bash -n` 这条要不要（我的建议：先不要）；③ 推一次真跑，把「本地绿」和「远端绿」分开记状态。**

注意 CI 上的 `windows-2022` runner 里 `mt.exe` 本来就在 PATH，所以 §1.2 的 `CMAKE_MT-NOTFOUND` 在 CI 上大概率不会复现——**但这不等于 preset 就能用**，我没验证过。

## 6. 本地验证状态 vs 远端实跑状态（分开记）

| 项 | 本地 | 远端 |
|---|---|---|
| 工具链发现原型四路径 | **已实测通过**（§3 表） | 未跑 |
| `verify-fast.sh` 的 CI 可移植性（无 TF 资产依赖） | **已实测**（grep 0 命中 + 读脚本） | 未跑 |
| `verify-fast.sh` 端到端在本机跑绿 | **未跑**（需持锁构建 7 个目标；本轮锁被占） | 未跑 |
| `bash -n` 全树 | 已实测：21 假红，已证伪（§4.3） | 未跑 |
| workflow 文件本身 | **不存在**（未写进仓库） | 不存在 |

**没有远端实跑，所以这份文档不能声明「CI 通过」。** 它声明的是：CI 需要的两个前提（工具链发现、tier-1 无需 TF 资产）里，第一个有原型并有失败路径的证明，第二个有静态证据；第三个前提（端到端跑绿）还没做。

本轮 hostlock 状态：`LOCKED owner=n12-qa`，队列 `n05-n06-maps` → `n12-camera`，`product_processes=1`。**我没有抢锁**，所以所有构建类动作都没做。

## 7. 这份文档不能证明什么

- **不能证明 CI 会绿**：workflow 没写、没推、没跑过。
- 不能证明 VS 生成器现在可用：`CMAKE_MT-NOTFOUND` 是 2026-11-11 的历史状态，本轮没重试。
- 不能证明原型在**别的机器**上正确：本机只有一版 MSVC（14.44.35207）和一版 SDK（10.0.26100.0），所以 `sort -V | tail -1` 的「取最新」分支实际上从未在多版本环境里被区分过——回退枚举分支（B 路径）证明了「能找到」，没证明「能选对」。
- 不能证明行尾成因：CRLF 从哪来、为什么 `core.autocrlf` 为空却有 CRLF，我没查出来（§4.3）。
- 不能证明 `verify-fast.sh` 43 条断言与六个数组的元素数一致：我数到 44 条字面量（10+8+7+8+4+7），脚本钉的是 43。**这个差我没有解释**，也没在绿跑里验证过哪个数是运行时真值；写在这里是因为它正好是「pin 一个数却不解释它从哪来」的形态，值得在跑通后核一次。

## 8. 证据

| 文件 | 内容 | 备注 |
|---|---|---|
| `.scratch/n22/env-locate.sh` | 工具链发现原型（3,614 B） | gitignored，未提交 |
| `.scratch/n22/diag-decisive.sh` | CRLF/LF 决定性对照脚本 | 产生 §4.3 的 rc=0 / rc=2 对照 |
| `.scratch/n22/diag-cont.sh` | 最小复现 `if true \`+CRLF | 记录 Git Bash `-n`=0 但执行 rc=2 |
| `native/CMakePresets.json` | 唯一的「CI 配置」，606 B | 指向的 `native/build` 里 `CMAKE_MT-NOTFOUND` |
| `native/build/CMakeCache.txt` | VS 生成器的失败现场（mtime 2026-11-11 23:03:35） | exe_count=0 |
| `verify-fast.sh` | tier-1 作业本体，13,912 B | 无 TF 资产依赖（grep 0 命中） |
| `env-msvc.sh` / `build-cmake.sh` | 各 4 行常量 + 2 处根路径 | 需要便携化的地方就这些 |
