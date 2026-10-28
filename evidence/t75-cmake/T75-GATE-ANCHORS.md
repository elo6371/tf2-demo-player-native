# T75 Phase 2 准备：install-readiness-check.sh 的门禁锚点修复

> 状态：**静态完成**。只改文本：没有构建、没有跑门禁、没有取锁。
> 工作树 `D:/TF2_Native_Worktrees/t75-cmake`，分支 `task/t75-cmake`，基线提交 `6e6facf`
> （拆分提交，父 `db3316e`）。改动只落在 `install-readiness-check.sh` 与本文档。

## 1. 为什么要改

T49 留下的安装验收门禁 `install-readiness-check.sh` 把「构建描述」当成**一个文件**
`native/CMakeLists.txt` 来 grep。T75 把 53 行的入口拆成「入口 + 23 个 `native/cmake/*.cmake`
模块」之后，从入口 grep 这些内容一律读到 0，于是门禁出现两类误判：

- **硬失败**：M3 变异段把 `SRC_CMAKE` 指向入口。python 变异块在入口里找不到
  `install(TARGETS tf2_demo_native native_install_smoke_probe` 这一行，直接
  `sys.exit("M3 root anchor absent")`，于是 :789 的 `eq "M3 applied" "$?" "0"` FAIL——
  失败信息说的是"变异没应用"，根因是变异器瞄错了文件。
- **静默变弱**：陈旧检查 / CPack / rmdir 三处只扫入口，读到 0 就得到 "none" 并**通过**——
  门禁看起来是绿的，实际上根本没看过模块。

修法只有一条原则：**把「构建描述」显式定义成「入口 + 全部模块」，让每条搜索都覆盖它**，
而不是把期望值改成新的数字。脚本第 65 行 `cd "$REPO" || exit 2`，因此
`native/cmake/*.cmake` 这个 glob 在脚本执行时展开为 23 个模块文件。

## 2. 改动清单（旧行号 → 新行号）

新行号对应本提交后的文件；旧行号对应 `6e6facf`。文件 827 行 → 839 行。

| 新行号 | 旧行号 | 改动 | 类别 |
| --- | --- | --- | --- |
| 228 | 228 | `find` 列表加入 `native/cmake` | 变-wide |
| 253–256 | 253–255 | 注释折行 + 注记"T75: 现在位于 `native/cmake/tf2-package.cmake`" | 注释 |
| 265–266 | 264 | 同上，注记 install/payload 两条规则的新位置 | 注释 |
| 551–556 | 548 之后插入 | 新增 5 行理由注释 + `CMAKE_FILES="native/CMakeLists.txt native/cmake/*.cmake"` | 变-wide 的基础 |
| 559 | 551 | CPack 搜索改用 `$CMAKE_FILES` | 变-wide |
| 560 | 552 | 断言文案去掉文件名，改 "in the CMake build description" | 文案 |
| 565 | 557 | rmdir 搜索改用 `$CMAKE_FILES` | 变-wide |
| 566 | 558 | 同上，文案中性化 | 文案 |
| 568 | 560 | `install_lifecycle.cpp` 计数改用 `cat $CMAKE_FILES`（期望 3 未改） | 修锚点 |
| 573 | 565 | `TF2_PACKAGE_VERSION` 计数改用 `cat $CMAKE_FILES`（期望 2 未改） | 修锚点 |
| 574 | 566 | 文案中性化 | 文案 |
| 617–621 | 609 | 4 行注释（锚点在 :30 / :60）+ `SRC_CMAKE="$REPO/native/cmake/tf2-package.cmake"` | 修锚点 |
| 624 | 612 | 备份名 `$MUT/backup/tf2-package.cmake` | 修锚点 |
| 811 | 799 | 还原复制用新备份名 | 修锚点 |
| 812 | 800 | 还原校验 `cmp` 用新备份名 | 修锚点 |
| 827 | 815 | verdict 循环显示名 `"tf2-package.cmake\|$SRC_CMAKE"` | 修锚点 |

改动后的关键行原文（去 `\r`）：

```sh
# :228
stale="$(find native/src native/include native/CMakeLists.txt native/cmake -type f -newer "$EXE" 2>/dev/null | head -3)"

# :556
CMAKE_FILES="native/CMakeLists.txt native/cmake/*.cmake"

# :559-560
cpack_rules="$(grep -lE "include\(CPack\)|CPACK_" $CMAKE_FILES 2>/dev/null | head -1 || true)"
eq "CPack/package rules in the CMake build description" "${cpack_rules:-none}" "none"

# :565-566
uninstall_rules="$(grep -lEi "remove_directory|rmdir" $CMAKE_FILES 2>/dev/null | head -1 || true)"
eq "blind directory-removal rule in the CMake build description" "${uninstall_rules:-none}" "none"

# :568 / :573-574
  "$(cat $CMAKE_FILES | grep -c 'install_lifecycle.cpp' | tr -d ' ')" "3"
version_defs="$(cat $CMAKE_FILES 2>/dev/null | grep -cE "^set\(TF2_PACKAGE_VERSION" | tr -d ' ')"
eq "package version metadata lines in the CMake build description" "${version_defs:-0}" "2"

# :621 / :827
SRC_CMAKE="$REPO/native/cmake/tf2-package.cmake"
for pair in "map_lookup.cpp|$SRC_MAP" "vpk_archive.cpp|$SRC_VPK" "tf2-package.cmake|$SRC_CMAKE"; do
```

> 失败模式检查：`$CMAKE_FILES` 是 glob 而不是 `find`。如果将来 `native/cmake/` 被改名或清空，
> bash 会把字面量 `native/cmake/*.cmake` 传给 `cat`/`grep`，计数变成 `0` 而不是空串，
> 于是 `eq ... "3"` / `eq ... "2"` **大声 FAIL**。也就是说这里的弱化不可能再退化成"绿的但没看见"。

## 3. 三处静默变弱：改前 / 改后

### 3.1 产品陈旧检查（:228 → :228）

- 改前：`find native/src native/include native/CMakeLists.txt -type f -newer "$EXE"`
  —— 只盯入口。拆分后改 `native/cmake/tf2-product.cmake`（源文件列表）不会让 exe 变"stale"，
  `product build is fresh against its sources` 会拿旧的 exe 通过。
- 改后：把 `native/cmake` 加进查找列表。任一模块比 exe 新，就会打印它并让这条断言 FAIL。
- 这正是它想要的语义：门禁只在该重建的时候把板子打回，模块也是构建输入。

### 3.2 CPack（:551/:552 → :559/:560）

- 改前：`grep -lE "include\(CPack\)|CPACK_" native/CMakeLists.txt` —— 只看入口。
- 改后：扫入口 + 23 个模块。全树仍然 0 命中（见 §4），所以**期望仍是 `none`**，
  但现在是"全树确实没有"而不是"入口里没有"。

### 3.3 盲目删目录规则（:557/:558 → :565/:566）

- 改前：`grep -lEi "remove_directory|rmdir" native/CMakeLists.txt` —— 只看入口。
- 改后：扫入口 + 23 个模块。T37 之后卸载是产品模式 `--uninstall`（读清单逐项删），
  没有 CMake 的 `remove_directory`/`rmdir` 规则，全树 0 命中，**期望仍是 `none`**。
- 为什么重要：拆分最容易把"删目录"这类危险规则挪进某个模块；只扫入口正好会漏掉。

### 3.4 两处计数（:560 → :568，:565 → :573）

这两处不是"静默"，而是"只数入口 = 0 → 立刻硬失败"。现在改为数全树，期望值保持 3 和 2。

## 4. 期望值 3 / 2 / none / none 的来源（数字一个没改）

在本工作树（`6e6facf` + 本提交）实测：

```
$ grep -c install_lifecycle.cpp native/CMakeLists.txt
0
$ grep -l install_lifecycle.cpp native/CMakeLists.txt native/cmake/*.cmake
native/cmake/tf2-probes-install-cli.cmake
native/cmake/tf2-product.cmake
native/cmake/tf2-version.cmake
$ cat native/CMakeLists.txt native/cmake/*.cmake | grep -c install_lifecycle.cpp
3
```

- **3** = 探针（`tf2-probes-install-cli.cmake` 1 处）+ 产品（`tf2-product.cmake` 1 处）+
  版本（`tf2-version.cmake` 1 处）都编同一个生命周期源。语义与拆分前完全一致，
  所以数字不动，只是把搜索面扩大。
- **2** = `^set\(TF2_PACKAGE_VERSION` 两行，都在 `native/cmake/tf2-version.cmake`。
- **none**（CPack）= `include(CPack)` / `CPACK_` 在入口 + 23 模块里 0 命中（grep rc=1）。
- **none**（rmdir）= `remove_directory|rmdir` 全树 0 命中（grep rc=1）。

> 纪律：把期望值改成"拆分后的新数字"会让门禁失去原契约（"产品、探针、版本共用同一个
> 生命周期源"、"版本元数据就这两行"）。所以只改搜索范围，不改数字；本次也没有出现
> 实测值与期望值不符的情况。

## 5. M3 变异段（唯一真正"改文件再还原"的段落）

- **锚点缺失时的行为**：M3 的 python 块在 :773-774（根规则）与 :783-784（payload 行）
  找不到锚点时 `sys.exit("... anchor absent")`，rc=1，于是 :789 的
  `eq "M3 applied"` 直接 FAIL。这正是拆分后门禁硬失败的样子。
- **锚点位置**（已核实，全树仅此两处）：
  - `native/cmake/tf2-package.cmake:30` `install(TARGETS tf2_demo_native native_install_smoke_probe`
  - `native/cmake/tf2-package.cmake:60` `"$<TARGET_FILE:native_install_smoke_probe>"`
- **一致性**：`SRC_CMAKE`、备份名、还原 `cmp`、verdict 循环里的显示名四处统一为
  `tf2-package.cmake`，否则备份路径 `$MUT/backup/CMakeLists.txt` 与 `$SRC_CMAKE`
  对不上，还原校验会拿错文件比。
- **预演（不碰仓库）**：把门禁里的 python 变异块抽出来，在 `mktemp` 出来的模块副本上跑：
  rc=0、输出 `mutated: neither install rule names native_install_smoke_probe`、88 行 →
  87 行、`native_install_smoke_probe` 命中 2 → 0、CRLF 未被破坏（88 → 87 个 CR），
  再用备份覆盖回去 `cmp` 一致。说明变异块与新锚点匹配、且能干净还原。
- 真实验证仍然只能由 Phase 2 跑门禁给出（本次禁止执行门禁本体）。

## 6. 断言数量：没有增删

- 没有新增或删除任何 `eq` / `has` / `hasnt` / `wrong` 调用：用同一组 grep/awk 分别数
  `git show HEAD:install-readiness-check.sh` 与工作树，改动前后都是 **190 处（全文）/
  142 处（forward 段，旧 :211-591 区间）**。
- 本门禁历史运行记录里的断言数是 **cases=13 checks=145**
  （`evidence/logstf/readiness.log:248`、`evidence/t37-install/t47-cold-readiness/gate/forward.log:240`），
  本次改动后仍应是 145。
- 注：`verify-all.sh:1304` 注释里的 "88 assertions" 是 **MATERIAL-COVERAGE** 那一档的 pin，
  不是这个门禁的断言数（那一档在 `verify-all.sh:1322` 打印 `MATERIAL-COVERAGE-CHECK=PASS (88 assertions)`）。
  两者是不同门禁，本次都没有动它们的数字。
- 三条断言文案（:560 / :566 / :574）只改措辞。历史证据日志里的旧文案
  （如 `evidence/t37-install/*`、`evidence/logstf/readiness.log`）不会被任何脚本拿去比对，
  只作当时记录，因此重写文案是安全的。

## 7. 故意没有改的过期引用

### 7.1 `evidence/t49/fullscreen-locked-run.sh:50,56`

```sh
cmnew=$(find native -maxdepth 1 -name 'CMakeLists.txt' -newer "$EXE" ...)   # :50
echo "... nor native/CMakeLists.txt is newer than the exe"                    # :56
```

理由：这是 T49 **那一次验收**的取证脚本，它的输出是历史证据。改它等于用今天的树去重写
当时的观测；而且 `-maxdepth 1` 的写法本身就在断言"构建描述是一个文件"这个已经过时的模型。
T75 的职责只到当前门禁 `install-readiness-check.sh`。留给集成者/后续任务（可以把这类历史
脚本标成 historical，或另开一项统一修）。

### 7.2 `evidence/logstf/tools/t54-locked-run.sh:98`

`SRCS="... native/CMakeLists.txt"`，用于生成 `sources-before.sha256` 快照。

理由同上：它是 T54 锁定运行时的取证工具，快照的是"当时测过的输入集合"。改它会改变
历史证据的含义。同样交给集成者/后续任务。

### 7.3 `verify-all.sh:266,352`

只在**注释**里提到 `native/CMakeLists.txt`（记录历史测量点）。运行时的 pin
（`verify-all.sh:363-365`）读的是构建输出里的 `^exe_count=53$`，不 grep CMakeLists，
所以拆分不影响它，无需改动。

### 7.4 门禁内部的历史行号注释（:253–256、:265–266）

保留原有 `native/CMakeLists.txt:656-689` / `:670-676` 的引用（那是 T49 时的行号坐标），
只在后面加一句"T75: 现在位于 `native/cmake/tf2-package.cmake`"，方便读者按老行号回查历史、
同时不被误导。

## 8. Phase 2 风险：build-target.sh 与 `cmake_check_build_system`

这是本次**只记录、不修**的一条真风险。

- **现状（静态读代码）**：`build-target.sh` 只有两种情况下会 configure——
  ① `native/build-nmake/CMakeCache.txt` 不存在；② 请求的 target 在 `$BUILD/Makefile` 里
  找不到 `^target:` 规则。否则直接 `cmake --build "$BUILD" --target "$@"`（:136）。
  也就是说增量路径上"感知 .cmake 变更"这件事完全依赖 nmake 的
  `cmake_check_build_system` 规则。
- **静态证据（支持它会感知）**：`.scratch/t75-cmake/after/CMakeFiles/Makefile.cmake` 的
  `CMAKE_MAKEFILE_DEPENDS` 列了 23 个 `native/cmake/*.cmake`（拆分前的控制树里是 0 个），
  且 `.scratch/t75-cmake/after/Makefile` 里 `all`（:111）与 `tf2_demo_native`（:145）
  都依赖 `cmake_check_build_system`。=> 理论上 touch 任一模块 = 重跑 cmake = 重新生成
  `cmake_install.cmake`。
- **但这是推理，不是实测**。M3 是第一条真正走这条路的路：它改 `tf2-package.cmake`，
  然后只跑 `build-target.sh tf2_demo_native`，再数 `$BUILD/cmake_install.cmake` 里的 probe 命中。
- **失败症状**：如果重配没触发，`M3 reached the generated install script` 会停在改前的值
  （2）→ 该断言 FAIL；或 `M3_LOST` 为空 → `M3 caught` FAIL。
- **若真失败，T75 还要改 `build-target.sh`**：当任一 `native/cmake/*.cmake`（或
  `native/CMakeLists.txt`）比 `$BUILD/CMakeFiles/Makefile.cmake` 新时置
  `NEED_CONFIGURE=1`。这在本次范围之外。
- 附带提醒：本工作树当前**没有** `native/build-nmake`，Phase 2 第一次跑必然是冷配置；
  冷配置那次不构成对增量路径的检验，M3 才是。

## 9. 状态、复现与边界

静态检查（允许范围内）：

```
$ bash -n install-readiness-check.sh; echo rc=$?
rc=0
$ file install-readiness-check.sh
install-readiness-check.sh: Bourne-Again shell script, ASCII text executable, with very long lines (381), with CRLF line terminators
$ printf 'lines=%s CR=%s LF=%s\n' \
    "$(wc -l < install-readiness-check.sh)" \
    "$(tr -dc '\r' < install-readiness-check.sh | wc -c)" \
    "$(tr -dc '\n' < install-readiness-check.sh | wc -c)"
lines=839 CR=839 LF=839
```

CRLF 没有翻转：文件是 CRLF 进来、CRLF 出去（827 行 → 839 行，CR 与 LF 数量相同）。

**没有运行**：`install-readiness-check.sh` 本体、`build-cmake.sh`、`build-target.sh`、
任何 `cmake`（configure/build）、任何 `.exe`、`verify-all.sh`；没有碰 `hostlock.sh`；
没有动 `D:/TF2_Native_Worktrees/t61-airwalk`。

因此本文所有结论都是静态的 + 一次隔离副本上的 python 变异预演。M3 的端到端行为、
三条 grep 在真实构建产物上的结果，都要等 Phase 2 在锁内的那一次运行。
