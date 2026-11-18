# INST1 E3a — 工具链入口去硬编码（N22）

Date: 2026-11-15  Author: arch-ci  片: E3a/N22  前置: R0 / R1 / E1 / E2 已落盘并提交

---

## 0. 一句话

**入口硬编码有 4 类、散布在 6 个脚本里；本机实测存在第二个被硬编码忽略的 Visual Studio（BuildTools），
而新脚本 `tools/tf2-toolchain.sh`（已写、已测，本片未提交）用「显式覆盖 → vswhere/目录扫描发现 → 断言后才 export」三段把它换掉，
在本机给出与旧 `env-msvc.sh` **逐字节相同**的 PATH/INCLUDE/LIB（外部参照 diff 为空），
并在六条负控下都以具名原因返回非零。

本片的边界：新脚本**已写、已测，但未提交**（arch-ci 的写域是 docs/tools/CI 草案、且 EXEC-PLAN 标注默认只读）；
把 `env-msvc.sh` / `build-cmake.sh` / `resource-discovery-check.sh:226-230` 改成 source 新脚本的动作
**没有做**，原因见 §7：这三个文件在别的实例正在使用的路径上，改它们会让不相关的构建在别人手里换行为。

---

## 1. 本轮硬约束

| 约束 | 出处 | 本片怎么遵守 |
| --- | --- | --- |
| 不改 CMake 目标图 | `docs/EXEC-PLAN-2026-11-15.md:14` | 未动任何 `CMakeLists.txt` / `native/cmake/*.cmake` |
| arch-ci 默认只读 | `docs/EXEC-PLAN-2026-11-15.md:38` | 仓库内只新增 `tools/tf2-toolchain.sh`（未提交）；`env-msvc.sh` 等既有脚本一个字节未改 |
| 提交硬闸 | lead 的 CRLF-DAMAGE 闸 | `bash tools/audit-tree.sh` → `VERDICT: OK (no forbidden writes, line endings intact)` |
| 构建/产品要宿主锁 | `verify-lock.sh` + 实测 | 唯一的构建动作（no-op 构建）走 `tools/lockrun.sh arch-ci -- …`；代表目标编译**只调 cl.exe、不进构建树、不碰 exe** |

---

## 2. 现状：入口硬编码的四类

全树普查（`E3a-out/survey*.sh`）得到四类入口，不是一类：

| 类 | 内容 | 站点 |
| --- | --- | --- |
| ① MSVC/SDK 绝对路径 | `MSVC_VER="14.44.35207"` / `SDK_VER="10.0.26100.0"` + 本机 VS Community 路径 | `env-msvc.sh:4-7`、`build-cmake.sh:30-33`、`resource-discovery-check.sh:226-230`、`install-readiness-check.sh:285` |
| ② python 绝对路径 | `.workbuddy-ai/…/python.exe` 等 4 种路径、4 种写法 | 根 `*.sh` 里 31 处（其中 24 个脚本纯硬编码无覆盖） |
| ③ 读 ① 的消费者 | `source ./env-msvc.sh` | `build-target.sh:61`、`logstf-syntax-check.sh:27`、`native/tools/mf_export_probe.sh:19`、`evidence/t75-cmake/verify-split.sh:23` |
| ④ 自己跑 cmake 的 | `cmake -S native -B …` | `build-cmake.sh`、`build-target.sh:113-114`、`resource-discovery-check.sh`、`evidence/t75-cmake/verify-split.sh`（自洽：跑 cmake 的四个都自己处理工具链） |

**本片只处理 ①（+ ③ 的迁移提案）。② 是另一条线**：它同样硬编码，但换它要动 24 个门禁脚本，风险面与 ① 不同族，不该塞进同一张卡。

### 2.1 硬编码的实际代价（本机可证，不是假设）

本机装了**两个** Visual Studio：

- `C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools` —— **没有** `VC/Tools/MSVC`（只有 Common7 / Licenses / MSBuild）；
- `C:/Program Files/Microsoft Visual Studio/2022/Community` —— 真正带 MSVC 14.44.35207 的那个。

`vswhere.exe -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath` 返回 Community。
而硬编码的 `env-msvc.sh:6` 只认识 Community —— **换一台只装 BuildTools 的机器，或者 Community 装在 D: 盘，这套脚本就整体失效，且失效信息是 `cl.exe: not found` 而不是「找不到 VS」**。

---

## 3. 「缺工具具名失败」今天做不到（E3a 验收第 1 条的反面证据）

实测 `cl.exe` / `rc.exe` / `mt.exe` / `nmake.exe` 四个**全都不在 PATH 上**；
source `env-msvc.sh` 之后六个（含 link/lib）才出现。`cmake` 一直在 PATH（`/c/Program Files/CMake/bin/cmake.exe`，4.4.3）。

所以旧脚本在工具链不匹配时的失败形态是**裸的 `cl.exe: not found`** —— 没有一句话说明「TF2 的哪一段以为工具链在哪、实际在哪」。
这正是新脚本第 4/5 段断言存在的理由。

---

## 4. 交付物：`tools/tf2-toolchain.sh`

**9,576 B / 206 行 / sha256 前 32 位 `f5c066033c49895e82e20839eacd75af` / 状态：untracked（未提交）**。

| 段 | 行 | 做什么 |
| --- | --- | --- |
| 覆盖 | `:33-38` | `TF2_MSVC_ROOT` / `TF2_MSVC_VER` / `TF2_SDK_ROOT` / `TF2_SDK_VER`；另 `TF2_NO_VSWHERE`（强制走扫描，让回退分支可测）、`TF2_TOOLCHAIN_QUIET` |
| 发现 | `:93-121` | vswhere → 失败则扫 `/c/Program Files{, (x86)}/Microsoft Visual Studio/*/*` 找 `VC/Tools/MSVC` |
| 取版本 | `:136-149` | 除非 pin，`ls -1 | grep -E '^[0-9]+[.]' | sort -V | tail -1` 取最新（`sort -V` 让未来的 14.9 排在 14.44 下面而不是上面） |
| 断言（导出前） | `:157-168` | 七条目录断言：toolset bin / SDK bin / MSVC include / SDK ucrt,shared,um / MSVC lib\x64 / SDK ucrt lib\x64。任一缺 → `TF2-TOOLCHAIN: …` 到 stderr，**环境一个字节都不动** |
| 导出 | `:170-172` | `PATH` 用 `/c/…` 形式；`INCLUDE` / `LIB` 用 `C:/…` 形式（旧 `env-msvc.sh` 就是这形状，cl.exe 接受） |
| 工具断言 | `:177-184` | `cl.exe nmake.exe rc.exe mt.exe` 四个 `command -v`，缺哪个具名报哪个 |
| 汇总行 | `:191-194` | `TF2-TOOLCHAIN: vsroot=… msvc=… sdk=… cl=… rc_exe=… status=OK`（`TF2_TOOLCHAIN_QUIET` 可静音） |
| 两种用法 | `:198-205` | `BASH_SOURCE[0]` 与 `$0` 比较：被 source 时 `return 1`，被直接执行时 `exit 1` —— 两种形态都报失败，不会一端静默 |

### 4.1 一处必须解释的路径转换（v1 bug，已修）

`tf2_toolchain_to_win()`（`:53-71`）第一版是手写的 `cut -c2 -c4` 映射。它把 `/tmp/tf2-fake-vs/…` 变成 `T:/p/tf2-fake-vs/…`，
于是断言报 `TF2-TOOLCHAIN: MSVC include missing: T:/p/tf2-fake-vs/...` —— **这是假阴性，读起来跟真实工具链故障一模一样**。
现版：先判 drive-letter 形式，是则**优先 `cygpath -m`**，没有 cygpath 才退回 `cut`；不是 drive-letter 形式就原样返回、**不发明盘符**。
同一假 VS 根现在的失败信息是真因：`TF2-TOOLCHAIN: cl.exe is not on PATH after adding /tmp/tf2-fake-vs/VC/Tools/MSVC/14.00.00000/bin/Hostx64/x64 and /c/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64`。

`cygpath` 实测存在（niubash 自带 `winuxcmd/usr/bin/cygpath.exe`；`cygpath -u 'C:\Program Files\Microsoft Visual Studio\2022\Community'` → `/c/Program Files/Microsoft Visual Studio/2022/Community`，rc=0；bash 是 GNU bash 5.3.20 MSYS）。

### 4.2 `tr -d \r` 是刻意的，别当脏字节删掉（lead 判据 c）

`:82-92` 有 11 行注释专门说这件事：vswhere 回的是 CRLF，路径里混进 CR 会让 `"$msvc_root/VC/Tools/MSVC"` 永远不匹配、失败信息指向错误的东西，所以要剥；
但**必须写成反斜杠 + r 两个字符**。写成字面 0x0D 就是 CRLF 损坏：`tools/audit-tree.sh` 会整树判 `VERDICT: CRLF-DAMAGE` 并指名 `tools/tf2-toolchain.sh has 1 CR`。
**本文件真的这样被提交硬闸拦下过一次**（见 §6）。

---

## 5. 验收矩阵（每条都有实测输出）

### 5.1 六路径套件（`E3a-out/test-toolchain.sh`）

| 路径 | 期望 | 实测 |
| --- | --- | --- |
| `bash -n` | rc=0 | **rc=0** |
| A 默认发现（vswhere） | rc=0，msvc/sdk 正确 | **rc=0**，`msvc=14.44.35207 sdk=10.0.26100.0` |
| B `TF2_NO_VSWHERE=1`（扫描回退） | 与 A 同结果 | **rc=0**，路径逐字与 A 相同 |
| C pin 版本 | rc=0 | **rc=0** |
| D 负控 `TF2_MSVC_VER=99.99.99999` | rc≠0 + 具名 | **rc=1**，三条具名：toolset bin / MSVC include / MSVC x64 lib |
| E 负控 `TF2_MSVC_ROOT=/c/NoSuchVS` | rc≠0 + 具名 | **rc=1**，`TF2-TOOLCHAIN: TF2_MSVC_ROOT=/c/NoSuchVS has no VC/Tools/MSVC` |
| F 负控 `TF2_SDK_VER=0.0.0.0` | rc≠0 + 具名 | **rc=1** |
| G 与 `env-msvc.sh` 的环境等价 | 逐字相同 | **IDENTICAL**（diff 为空） |

### 5.2 外部参照 diff（lead 判据 a：不能只有自证）

`E3a-out/test-external-env.sh` 取**已提交的** `git show HEAD:env-msvc.sh`（10 行，sha 前 16 位 `cc3b642e285b645c`），
在**两个独立子 shell** 里各自 `unset INCLUDE LIB` 后 source，再 `env | grep -E '^(PATH|INCLUDE|LIB)=' | sort`：

- A（旧 `env-msvc.sh`）rc=0，3 行；
- B（新 `tools/tf2-toolchain.sh`）rc=0，3 行；
- `diff -u` **完全空** → `EXTERNAL-ENV-IDENTICAL=YES`；
- `INCLUDE` + `LIB` 单独 diff：**IDENTICAL**；PATH 两侧均 2,229 B 且开头逐字相同。

两边打印的 `INCLUDE` 都是：
`C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207/include;C:/Program Files (x86)/Windows Kits/10/Include/10.0.26100.0/ucrt;…/shared;…/um;…/winrt`，
`LIB` 都是 `…/lib/x64;…/Lib/10.0.26100.0/ucrt/x64;…/um/x64`。

### 5.3 路径带空格 / 中文（lead 判据 b）

**正控**（`E3a-out/test-path-negatives.sh:N4`）：在 `/tmp/tf2 gap 中文 dir` 下 source rc=0，
`cl.exe /nologo /c /std:c++17 /EHsc "ok 中文.cpp" /Fo:"ok 中文.obj"` → **rc=0，obj 630 B**；另一轮 `"gap test 中文.cpp"` → rc=0，obj 1,104 B。

**负控**（同脚本）：

| 编号 | 场景 | rc | 具名原因 |
| --- | --- | --- | --- |
| N1 | cd 到不存在的 space+中文目录 | 3 | `cd failed: no such directory` |
| N2b | `/Fo:"Z:/no such drive/t.obj"` | **1** | `fatal error C1083: 无法打开编译器生成的文件: "Z:\no such drive\t.obj"` |
| N2c | `/Fo:"不存在的子目录/t.obj"` | **1** | 同类 C1083 |
| N3 | 源文件不存在 | **2** | `c1xx: fatal error C1083: 无法打开源文件: "no such file.cpp"` |
| N4 | 同目录正控 | 0 | obj 630 B |

#### 一条负控失败，但结论比它本身重要

最初想用 `chmod 555` 造「无写权限」目录，实测 **`WRITE SUCCEEDED despite 555`** ——
**MSYS 的 POSIX mode bits 在 NTFS 上只是建议性的，不构成写拒绝。** 结论：「无写权限」这条反控在 MSYS 下**不能用 chmod 造**，
只能用不存在的盘 / 不存在的子目录（N2b / N2c）。附带：`/Fo:C:/Windows/System32/tf2-probe.obj` 也 rc=0（Administrator 有写权限），同样不构成负控。

#### 两条 cl 行为（踩过）

1. 成功路径也会把源文件名 echo 到 stdout —— 判成功要看 rc 和 obj，不要看有没有输出；
2. 失败时 `PIPESTATUS[0]` 才是 cl 的 rc，管道到 `head` 会把 rc 吃掉。

### 5.4 代表目标本地构建（验收「本地代表目标可构建」）

`E3a-out/test-representative-build.sh`：不跑 cmake、不动构建树、不碰 exe，直接用新脚本导出的环境 + **`tf2_demo_native` 的真实编译旗标**编 4 个真实 TU，obj 落在树外（`C:/Users/ADMINI~1/AppData/Local/Temp/tf2-rep-build 中文`）：

| TU | rc | 耗时 | obj | warnings |
| --- | --- | --- | --- | --- |
| `native/src/hud_state.cpp` | 0 | 1,647 ms | 526,107 B | 0 |
| `native/src/world_material.cpp` | 0 | 2,329 ms | 1,256,568 B | 0 |
| `native/src/vpk_archive.cpp` | 0 | 2,403 ms | 1,084,137 B | 0 |
| `native/src/hud_import.cpp` | 0 | 2,107 ms | 983,197 B | 0 |

用的旗标取自 `native/build-nmake/CMakeFiles/tf2_demo_native.dir/flags.make`：`/DWIN32 /D_WINDOWS /EHsc /O2 /Ob2 /DNDEBUG -std:c++17 -MT /Brepro /W4 /permissive- /utf-8`，
include 两段 `native/include` 与 `native/third_party/lzma`。**四个 TU 全部零警告**（与 E2 的 21 条全量警告不矛盾：那 21 条没有一条落在这四个文件里）。

同一脚本的三条负控：

- 不存在的 include 目录 → **rc=2**，`fatal error C1083: 无法打开包括文件: "hud_state.h"`；
- 不 source 工具链 → **`cl.exe` 根本不在 PATH 上**（脚本断言「NOT on PATH until sourced」，不静默成功）；
- 工具链脚本指向不存在的 VS 根 → **rc=1**，`TF2-TOOLCHAIN: TF2_MSVC_ROOT=/c/NoSuchVS has no VC/Tools/MSVC`。

#### 一个踩坑记录（会误导下一个人，所以写下来）

第一版代表构建 4 个 TU **全 rc=2 报「找不到 hud_state.h」**，看起来像工具链坏了。真因是 **`-I/d/TF2_Native_Worktrees/…`（MSYS 形式）被 cl.exe 当成了 `/D` 宏定义**，include 路径根本没生效。
改成 `-I"D:/TF2_Native_Worktrees/…"`（`cygpath -m`）后全绿。同理 `/Fo:` 也必须用 Windows 形式，否则 obj 被写到 `D:\tmp\…` 这种不存在的位置。**「MSYS 路径可以直接喂给 cl.exe」是错的。**

### 5.5 no-op 构建（承 E2/N26 的「no-op 不误构建」）

`E3a-out/noop-build.sh`，跑法 `bash tools/lockrun.sh arch-ci -- bash ./build-target.sh tf2_demo_native`：

- 锁：`VERIFY-LOCK=ACQUIRED owner=arch-ci` → `VERIFY-LOCK=RELEASED`，**rc=0 / 5 s**；
- exe：`e61c7fd9e5dba5167463f493937b23be799c96bcdd35bee7e0970d747a3bb078` / mtime 1794754711 / 3,025,408 B，跑前跑后**完全相同** → `NOOP=EXE-UNCHANGED sha-equal` + `NOOP=mtime-unchanged`；
- 日志只有 `[ 2%] Built target tf2_build_stamp` / `[ 65%] Built target tf2_core` / `[100%] Built target tf2_demo_native`，**Building CXX object = 0、Linking = 0**；
- `git status --porcelain -- native/tests/fixtures evidence` **空**。

> 首次跑 rc=127 是我自己写错路径：`tools/build-target.sh` 不存在，真正的路径是**仓库根的 `./build-target.sh`**（8,930 B / Nov 10 18:37）。记在这里免得下一个人重踩。

---

## 6. 事故记录：一个 CR 字节卡住了全仓提交

lead 报：`tools/tf2-toolchain.sh` 第 64 行有一个**字面 CR 字节**而不是两字符转义，`cat -A` 显示为 `tr -d '^M'`；后果是 `bash tools/audit-tree.sh` 整树判 `VERDICT: CRLF-DAMAGE`（`VIOLATION: tools/tf2-toolchain.sh has 1 CR (HEAD had 0)`），这是 lead 给所有人定的提交硬闸。

- **根因**：我用程序生成脚本正文时把转义写成了真字节。
- **修法**：CAS 编辑把字面 CR 换成反斜杠 + r 两个字符。
- **回证**：`tr -cd '\r' < tools/tf2-toolchain.sh | wc -c` → **0**；`bash tools/audit-tree.sh` → `VERDICT: OK (no forbidden writes, line endings intact)`；`bash -n` rc=0、直接执行 rc=0、三条负控 rc=1。lead 独立复测一致。
- **教训（写进 §4.2 的注释里了）**：脚本正文里任何要出现反斜杠的地方，都要拼成两字符序列；写进真字节就是脏字节。

---

## 7. 迁移提案（**没有做**，需要 lead 点头）

把三个消费者改成 source 新脚本，每个都是等价的单行替换：

```sh
# env-msvc.sh -- 整体替换为：
source "$(dirname "${BASH_SOURCE[0]}")/tools/tf2-toolchain.sh"

# build-cmake.sh:30-33 -- 删掉四个常量与两段 export，换成：
source "$(dirname "$0")/tools/tf2-toolchain.sh" || exit 1

# resource-discovery-check.sh:226-231 -- 同上，并把 nmake 断言留给脚本自己的第 5 段
```

**为什么不在本片做**：

1. `env-msvc.sh` 在**别的实例的**构建路径上（`build-target.sh:61` 是四个实例共用的入口）。改它等于在别人跑门禁时换工具链实现；
2. arch-ci 的写域是 docs/tools/CI 草案，`env-msvc.sh` 与 `build-cmake.sh` 都在仓库根、不在写域内；
3. 换实现的可观测差异只有一处：旧脚本在工具链不匹配时**静默导出半个环境**，新脚本**具名失败**。这是想要的改进，但它会让「以前能跑但一直在用错工具链」的脚本第一次跑就红 —— 应该由一个能盯住四个实例的窗口来做，不是现在。

**建议的落地方式**：先只改 `env-msvc.sh`（消费者最多，改一个覆盖 `build-target.sh` / `logstf-syntax-check.sh` / `mf_export_probe.sh` / `verify-split.sh` 四个），跑一轮 `bash tools/audit-tree.sh` + 一次 no-op 构建 + 一次 `tf2_demo_native` 真构建，全绿后再动 `build-cmake.sh` 与 `resource-discovery-check.sh`。

---

## 8. 未证明项（NOT VERIFIED）

1. **远端 CI 一行都没跑**。`.github/` 不存在，本片没有创建它（前一轮 N22 草案 `docs/arch-ci-2026-11-14/N22-CI-DRAFT.md` §5 有 `tier1.yml` 骨架，但那是草案不是事实）。按卡片：「远端没有跑即 NOT VERIFIED」。
2. **迁移没有落地**：`env-msvc.sh` / `build-cmake.sh` / `resource-discovery-check.sh:226-230` 三处仍是旧实现，`tools/tf2-toolchain.sh` 还没有任何消费者 —— 所以「入口硬编码被移除」这句话在本片**只对新增脚本成立，对既有入口不成立**。
3. **没在第二台机器上验证过发现逻辑**。BuildTools 那条分支（`Program Files (x86)` 扫描）在本机只走到「有目录但没有 VC/Tools/MSVC」这一种情形；一个真的只有 BuildTools 的机器没测过。
4. **没在非 C: 盘装 VS 的机器上测过**。扫描分支的 `/[A-Za-z]/*)` 与 `cygpath -m` 在 D: 根下未实测。
5. **没测「pin 的版本号存在但目录残缺」**。负控只给了不存在的版本号（D / F）与不存在的根（E）。
6. **`sort -V` 在多 toolset 共存时的选择未实测**：本机只有一个 toolset（14.44.35207），「取最新」这条规则没有反例可测。
7. **没有产品级运行**。本片全程没起 `tf2_demo_native.exe`（宿主锁在别的实例手上，产品进程常驻 1 个）。代表目标只编译、不链接、不运行。
8. **python 那 31 处硬编码（② 类）一个字没动**，见 §2。

---

## 9. 复现

```sh
cd /d/TF2_Native_Worktrees/t105-e5

# 1. 六路径套件（默认发现 / 扫描回退 / pin / 三条负控 / 环境等价）
bash /d/_shared/EXEC-2026-11-15/INST1/E3a-out/test-toolchain.sh

# 2. 外部参照 diff（用 HEAD 版 env-msvc.sh 做参照，不是工作树）
bash /d/_shared/EXEC-2026-11-15/INST1/E3a-out/test-external-env.sh

# 3. 空格 + 中文路径的正负控
bash /d/_shared/EXEC-2026-11-15/INST1/E3a-out/test-path-negatives.sh

# 4. 代表目标编译（4 个真实 TU，obj 在树外，不碰构建树）
bash /d/_shared/EXEC-2026-11-15/INST1/E3a-out/test-representative-build.sh

# 5. no-op 构建（要走宿主锁；锁被占就别跑）
bash tools/lockrun.sh arch-ci -- bash ./build-target.sh tf2_demo_native

# 6. 提交硬闸
bash tools/audit-tree.sh          # 期望 VERDICT: OK
tr -cd '\r' < tools/tf2-toolchain.sh | wc -c   # 期望 0
```

证据目录：`D:/_shared/EXEC-2026-11-15/INST1/E3a-out/`（21 个脚本，见索引）。

---

## 10. 证据索引

| 文件 | 覆盖什么 |
| --- | --- |
| `E3a-out/survey.sh` / `survey2.sh` / `survey3.sh` / `survey4.sh` | 四类入口普查 |
| `E3a-out/discovery.sh` / `discovery2.sh` / `discovery3.sh` | vswhere、两个 VS、MSVC/SDK 版本、PATH 实况 |
| `E3a-out/test-toolchain.sh` | 六路径套件（含 A–F + G 环境等价） |
| `E3a-out/test-external-env.sh` | 与 `git show HEAD:env-msvc.sh` 的外部参照 diff |
| `E3a-out/test-path-negatives.sh` | 空格 + 中文的正控 N4 与负控 N1/N2/N2b/N2c/N3 |
| `E3a-out/test-write-denial.sh` | chmod 555 不构成写拒绝的证据 + N2b/N2c/N2d |
| `E3a-out/test-representative-build.sh` | 4 个真实 TU 编译 + 三条负控 |
| `E3a-out/noop-build.sh` | no-op 构建（exe sha / mtime / Building=0 / Linking=0 / tracked 不脏） |
| `E3a-out/test-missing-tool.sh` | 缺工具路径（历史） |
| `E3a-out/test-spaces.sh` / `test-toolchain2.sh` | 早期空格路径与控制变体 |
| `E3a-out/verify-source.sh` / `check1.sh` | 断言自检 |
| `E3a-out/d72-pidspace.sh` | D72 的 pid 空间实测（`$$` ≠ 被 fork 的 pid） |
| `E3a-out/debug-inc.sh` | 定位 `-I/d/…` 被当成 `/D` 宏的那次调试 |
