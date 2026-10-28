# T75 Phase 2 —— 真实构建与门禁实测（clean build / 增量 / 三条门禁）

Status: **PHASE2-OK**。拆分后的构建树在 NMake + MSVC 19.44 上完成了一次从零配置的真实
全量构建（53/53 目标、errors=0），两条安装门禁按 T75 修正后的锚点实测通过
（readiness 两个锚点 3/2 全绿，M3 参考值 2/2 实测通过，变异体 3/3 被捕获）。
唯一红项是 install-lifecycle 第 4 节的 3 项断言，**根因已定位并复现：产品把两份 WebView2
许可证写进了安装清单，与门禁第 4 节注释声明的契约不符；与本次拆分无关，未修改**（源码
不在本任务授权范围）。

基准提交 5412f91（"T75 WIP: point the install gate at native/cmake modules"），分支
task/t75-cmake，worktree D:/TF2_Native_Worktrees/t75-cmake。Phase 1（静态等价）见
T75-SPLIT-PHASE1.md，锚点修复见 T75-GATE-ANCHORS.md。

## 0. 一句话结论

模块化本身在真实构建和真实门禁上是等价的：53 个目标全部产出、0 错误、20 条警告全部来自
未改动的源码（db3316e..HEAD 对 native/src、native/include、native/tools 的 diff 是 0 个文件），
增量路径在某个 native/cmake/*.cmake 变化时会真的重新 configure。T75 之前"会挂"的两处
计数锚点（install_lifecycle.cpp 出现次数、TF2_PACKAGE_VERSION 行数）在真实构建树上分别
实测 3 和 2，与门禁 pin 的值一致。

## 1. 环境与前置

| 项 | 值 |
|---|---|
| 分支 / HEAD | task/t75-cmake / 5412f9188e1642899cc508d7d1ec5803752d306c |
| 起点工作树 | git status --porcelain 为空 |
| 构建树 | native/build-nmake（本次之前不存在：这是宿主机上该 worktree 的第一次构建） |
| 编译器 | MSVC 19.44.35227.0（cl 14.44.35207） |
| 生成器 | NMake Makefiles（NMake 无并行，构建脚本会自己提示并忽略 -j） |
| 机时锁 | t75-cmake-r1：ACQUIRED 2026-10-28 12:19:02 → RELEASED 13:00:53（持锁 41m51s，全程单构建；13:00:53 status 显示 FREE） |
| 本任务改动 | 只有本文件（evidence/t75-cmake/T75-PHASE2.md）；native/src、native/include、native/tools、任何 .sh 均未改动 |

## 2. 步骤总表

| # | 步骤 | 命令 | rc | 用时 | 判定 |
|---|---|---|---|---|---|
| 1 | 前置核对 | git log / git status | 0 | <1s | HEAD、分支、干净工作树均符合预期 |
| 2 | 取锁 | hostlock.sh acquire t75-cmake-r1 1800 | 0 | <1s | ACQUIRED 12:19:02 |
| 3 | 清构建 | build-cmake.sh clean | **1** | 0.4s | 新工作树无 native/build-nmake，clean 无从执行——非 T75 缺陷，见 3.1 |
| 4 | 全量构建 | build-cmake.sh | 0 | **645.0s** | errors=0 / warnings=20 / exe_count=53 |
| 5 | 目标构建 | build-target.sh tf2_demo_native vpk_query | 0 / 0 | 94.1s / **3s** | 首次重建（缺 header stamp）+ 稳态增量 |
| 6 | 生命周期门禁 | install-lifecycle-check.sh --forward … | **1** | 267.6s | cases=15 checks=213 fail=3（既有产品漂移，见 3.4/第 5 节） |
| 7 | 安装就绪门禁 | install-readiness-check.sh --forward … | 0 | **698.1s** | cases=13 checks=145，INSTALL-READINESS=PASS，两个锚点 3/2 实测通过 |
| 8 | 增量重配置 | touch native/cmake/tf2-product.cmake + build-target.sh | 0 | 91s | **会重新 configure**（Makefile.cmake 重写、flags.make 重写、全目标重编） |
| 7b | 就绪门禁变异体（附加） | install-readiness-check.sh --mutation … | 0 | 232s | cases=4 checks=53 mutations=3，M1/M2/M3 全捕获，源码逐个字节复原 |
| 8b | 收尾稳态 | build-target.sh tf2_demo_native | 0 | 2s | rc=0，无编译动作 |
| 9 | 放锁 | hostlock.sh release t75-cmake-r1 | 0 | <1s | RELEASED，status=FREE |

## 3. 详细结果

### 3.1 第 3 步：clean 返回 1 不是缺陷

这个 worktree 是新建的，native/ 下此前没有任何 native/build* 目录，于是脚本里的
`cmake --build native/build-nmake --target clean` 必然失败：

```
Error: D:/TF2_Native_Worktrees/t75-cmake/native/build-nmake is not a directory
clean FAILED (rc=1)
```

clean 需要一个已存在的构建树。紧随其后的第 4 步是从零配置（日志里有
"-- The CXX compiler identification is MSVC 19.44.35227.0"、"Configuring done (2.3s)"），
所以第 4 步本身就是一次干净的、从空目录开始的配置加构建——第 3 步的 rc=1 不削弱
"clean build"这个结论。

### 3.2 第 4 步：全量构建

脚本自报的汇总块，逐字：

```
=== summary ===
cmake_build_rc=0
errors=0
warnings=20
exe_count=53
```

* 用时 645.0s（10m45s），比预期的 4~8 分钟长。原因是 NMake 无并行，而且这是一台
  冷构建（首次 configure + 53 个目标链接）。
* 独立复核：`ls native/build-nmake/*.exe | wc -l` = **53**，与脚本自报一致，等于预期的
  53 个目标。
* 完整日志：evidence/cmake-build.log（1097 行；按 .gitignore:27 属于 churn 文件，不入库）。

**20 条警告不是回归。** `git diff --name-only db3316e..HEAD -- native/src native/include
native/tools` 是 0 个文件——少一个字节的事实比任何警告分析都强：源码没变，警告就只能是
既有诊断。19 条不同的诊断（install_manifest.cpp(51) 因为同一个 .cpp 编进两个目标而出现两次）：

| 位置 | 诊断 |
|---|---|
| main.cpp(3508)(3509)(3510)(3511)(3512) | C4244 double→float |
| main.cpp(9841) | C4457 "instance" 隐藏函数参数 |
| xutility(4813)（MSVC 头，实例化点 main.cpp(6459)） | C4244 wchar_t→char |
| native_renderer.cpp(1711) | C4456 samplerDescription 隐藏上一个本地声明 |
| native_renderer.cpp(3492) | C4100 'tick' 未引用参数 |
| native_renderer.cpp(3750) | C4456 world |
| native_renderer.cpp(4623)(4624) | C4267 size_t→uint32_t |
| native_renderer.cpp(4931)(5101) | C4456 view |
| camera_bookmarks.cpp(134)(191) | C4456 index |
| logstf_panel.cpp(376) | C4100 'window' 未引用参数 |
| install_manifest.cpp(51) ×2 | C4189 dataSize 已初始化但未引用 |
| spectate_target.cpp(286) | C4127 条件表达式是常量 |

也就是说，任务书里"0 错误 0 警告"的期望在"警告"这一半不成立；errors=0 成立。

### 3.3 第 5 步：目标构建，两次

第一次：rc=0，94.1s，打印 `note: header stamp is missing; forcing requested source rebuild`
——新构建树没有 header stamp，脚本按设计强制重建了目标源码，属于首次使用的预期行为。
第二次紧接着再跑：rc=0，**3s**，输出只有标题行加 rc=0——稳态增量路径正常。

两次都没有触发 configure。两次之后 `git status --porcelain` 依旧为空：脚本只动了
mtime/obj，没动源码内容。

### 3.4 第 6 步：install-lifecycle 3 项红（既有问题，本次不改）

脚本必须带参数，裸跑打印 `usage: install-lifecycle-check.sh --forward <out-dir>` 并退出 2。
正式运行：`bash install-lifecycle-check.sh --forward .scratch/t75-cmake/lifecycle-forward`
→ rc=1，267.6s。末尾逐字：

```
cases=15 checks=213 mutations=0
INSTALL-LIFECYCLE=FAIL fail=3
```

历史基线（仓库根 T42-INSTALLER-VPK-2026-10-21.md:27）逐字记录着
`install-lifecycle-check.sh --forward: cases=15 checks=213, PASS`——同样的用例数与检查数，
所以不是"门禁少跑了节"，只是这 3 个检查点红。3 项 FAIL 全部落在第 4 节
（把一个只含 payload 的目录当作发布包形状去 --install），逐字：

```
FAIL no file was already there to preserve = 2, expected 0
FAIL the manifest lists exactly the installed files, each once = 7, expected 5
FAIL every manifest record is a state word the product knows = new preserved unchanged, expected new unchanged
```

其余 14 节（VPK 矩阵、首启、重启、迁移、升降级、非管理员、缺文件）全绿。

### 3.5 第 7 步：install-readiness 前向通过，两个锚点实测

`bash install-readiness-check.sh --forward .scratch/t75-cmake/readiness-forward` → rc=0，
698.1s（11m38s），末尾逐字：

```
cases=13 checks=145 mutations=0
INSTALL-READINESS=PASS
```

1/13..13/13 全部跑过（含 8b 正向对照 TITLE-CAPTURE-TIMEOUT）。末尾还有两条 BLOCKER 记录
（no_shortcuts_or_file_association=1 owner=T28、tf2_assets_not_packaged_by_design=1
owner=product），是登记在案的产品/任务归属，不参与判定。

因为是通过，T75 修好的两个锚点必然被断言过并成立：

* install-readiness-check.sh:568 —— "the product and the probe link the same lifecycle
  source"，对 CMAKE_FILES 做 `grep -c install_lifecycle.cpp`，pin 值 3。
* install-readiness-check.sh:574 —— "package version metadata lines in the CMake build
  description"，`grep -cE '^set(TF2_PACKAGE_VERSION'`，pin 值 2。

对应代码：:556 `CMAKE_FILES="native/CMakeLists.txt native/cmake/*.cmake"`，
:621 `SRC_CMAKE="$REPO/native/cmake/tf2-package.cmake"`（:617-620 的 T75 注释说明 M3 必须
指向模块而不是入口文件）。这两个数字在拆分前会挂，因为生命周期链接行和版本行搬进了
native/cmake/tf2-product.cmake 与 tf2-version.cmake。

### 3.6 第 8 步：模块改动确实会重新 configure（本阶段的关键风险点）

做法：先记录 native/build-nmake/CMakeFiles/Makefile.cmake 的 mtime，然后
`touch native/cmake/tf2-product.cmake`（只动 mtime，内容一字节未变），再跑
`bash build-target.sh tf2_demo_native`。

* mtime：Makefile.cmake **12:19:50.716746600** → **12:52:13.848468100**（epoch
  1793161190 → 1793163133）；生成的 Makefile 同时刻 12:52:13.797781600；CMakeCache.txt
  没有变（12:19:50.129567400），说明不是整树重配，是按需要重跑。
* 原始日志 .scratch/build-target.log 头三行逐字：
  `-- Configuring done (0.1s)` / `-- Generating done (2.3s)` /
  `-- Build files have been written to: D:/TF2_Native_Worktrees/t75-cmake/native/build-nmake`。
  （build-target.sh 的控制台输出只筛编译器诊断，所以这三行不在它的 stdout 里，而在它自己
  写的原始日志里。）
* 机制：CMakeFiles/Makefile.cmake 把模块登记成 configure 依赖——`grep -o
  native/cmake/tf2-product.cmake` 命中，该文件里共有 23 处 native/cmake/ 引用。

**代价**：reconfigure 会重写 flags.make（12:52:11.666724500），而 NMake 的每个 obj 都依赖
flags.make，于是这个目标的所有对象失效重编、重新链接——本次 91s（rc=0，同样 19 条既有警告）。
结论：改一个模块是正确性安全但**不免费**的，纯注释改动也要付整个目标的编译时间。

### 3.7 附加：就绪门禁的变异体（M1/M2/M3）跑通了

M3 只在 `--mutation` 模式里存在（forward 不跑），所以补跑了一次，复用 forward 的产物：
`bash install-readiness-check.sh --mutation .scratch/t75-cmake/readiness-forward` → rc=0，
232s，末尾逐字：

```
cases=4 checks=53 mutations=3
INSTALL-READINESS-MUTATION=PASS (3 mutations caught, sources restored)
```

* M1（map_lookup.cpp 里把 VPK 候选排到松散地图前面）：`store=vpk bsp=invalid/BSP header is
  invalid` —— 捕获。
* M2（vpk_archive.cpp 里把 64 位内联偏移截成 32 位）：`store=vpk refused=0` —— 捕获。
* M3（tf2-package.cmake 的两条安装规则都不再提 native_install_smoke_probe）：
  参考脚本命中 **2**（= pin 值，即 "M3 参考值 2/2"）；变异后生成的 cmake_install.cmake
  命中 **0**；包清单少了且只少了两个 probe 副本
  （`.uninstall/payload/native_install_smoke_probe.exe` 与 `native_install_smoke_probe.exe`）；
  复原后重新生成的安装脚本又回到 2，包清单回到 11 项。
* 三个源码（map_lookup.cpp、vpk_archive.cpp、tf2-package.cmake）复原后逐个
  `cmp` 字节相同；我另外用运行前的 sha256 备份独立复核：三个都 OK；`git status` 为空。

变异体跑完后当前的 tf2_demo_native.exe 是复原源码的重新链接产物
（sha256 f630e714…；变异前那次是 bf0e8d3e…）。PE 里有时间戳/调试路径，源码相同也不保证
字节相同——这不是问题，判据是源码与 HEAD 一致（git status 空）。

## 4. 两处"会挂"的锚点：实测位置

| 锚点 | 门禁位置 | 目标文件（拆分后） | pin | 实测 | 结论 |
|---|---|---|---|---|---|
| install_lifecycle.cpp 计数 | install-readiness-check.sh:568 | native/cmake/tf2-product.cmake | 3 | 3（forward PASS 即断言通过） | 修复有效 |
| TF2_PACKAGE_VERSION 行数 | install-readiness-check.sh:574 | native/cmake/tf2-version.cmake | 2 | 2（同上） | 修复有效 |
| M3 参考命中 | install-readiness-check.sh:760 | native/cmake/tf2-package.cmake | 2 | 2（变异体里直接打印：`OK M3 reference install script names the smoke probe twice (root and payload) = 2`） | 修复有效 |

## 5. lifecycle 3 项红的根因与判定

**根因（一个原因，可复现）**：产品把两份 WebView2 许可证当成安装记录写进了 manifest，
状态词是 `preserved`。独立最小复现：`cmake --install` 到一份暂存目录、整体拷成"包"、
直接跑 `tf2_demo_native.exe --install`，输出

```
install=PASS files=5 new=1 changed=0 unchanged=4 preserved=2 backups=0 copied=4
```

manifest 记录依次是：new/160/VERSION.txt、unchanged/{WebView2Loader.dll,
install_smoke_probe.ps1, native_install_smoke_probe.exe, tf2_demo_native.exe}、
preserved/1513/third_party/webview2/LICENSE.txt、preserved/3979/third_party/webview2/NOTICE.txt。
于是三个断言同时红：要保留的既有文件是 2（期望 0）、记录数 7（期望 5）、状态词集合多了
一个 preserved（期望 new unchanged）。

**为什么不是 T75 造成的**：

* `git diff --name-only db3316e..HEAD` 不含 install-lifecycle-check.sh（这份 diff 里只改了
  install-readiness-check.sh），也不含 native/src/install_lifecycle.cpp（它最后一次变更是
  b1d8dba，2026-10-21，早于基线提交）。
* 门禁第 4 节的注释自己写着那两份许可证"are not install records and the manifest below does
  not claim them"（install-lifecycle-check.sh:307-310）——现在产品声称了，是产品侧与门禁契约漂移。
* 装出来的包清单与该门禁文档化的 8 项（4 个 payload + VERSION.txt + manifest + 2 份许可证）
  完全一致，包形状没变，拆分也没有改变安装规则。

同一份基线里变异体是 `cases=3 checks=25 mutations=3`，现在是 cases=4 checks=53
mutations=3——多出来的一例正是 M3（3.7），检查数从 25 涨到 53，说明 T75 确实把 M3 接进了门禁。

**处置**：未改（源码不在授权范围）。两条修法都成立，需要产品侧拍板，我不替产品定：

(a) 收紧产品：许可证不是安装记录，就不该进 manifest——这与门禁第 4 节注释的契约一致，但要在
install_lifecycle.cpp 的 "preserved = 目录里已有、不是我们写的" 语义里开一个例外；
(b) 放宽门禁：承认 `preserved` 是合法状态词，把三条断言改成"计数 2 / 记录 7 / 状态词集合
含 preserved"——更贴近产品现在的语义（kStatePreserved 在 install_lifecycle.cpp:343-359 是
一条通用路径，不是为许可证特设的），代价是把"许可证算安装记录"写成契约。

关键是同一个事实（产品会记录 preserved）只能有一处被承认：现在是产品记录了、门禁说没有。

## 6. 本次没有做

* 没有跑 verify-all.sh（任务禁止），没有 merge / push / rebase。
* 没有改动 native/src、native/include、native/tools 或任何 .sh；第 5 节的漂移留给授权方。
* 没有做"只改注释的模块不触发重编"的优化——现状是重编整个目标（3.6），属于已知代价而非缺陷。

## 7. 复现命令

```bash
# 0) 机时锁（单构建）
bash /d/TF2_Native_Worktrees/_shared/hostlock.sh acquire t75-cmake-r1 1800

# 1) 全量（新工作树里 clean 会 rc=1，属预期）
bash build-cmake.sh clean
bash build-cmake.sh

# 2) 目标级 + 增量重配置验证
bash build-target.sh tf2_demo_native vpk_query
touch native/cmake/tf2-product.cmake
bash build-target.sh tf2_demo_native          # 原始日志里应有 Configuring/Generating done

# 3) 两条安装门禁（都必须带 out-dir；mutation 复用 forward 的目录，且要在 forward 之后）
bash install-lifecycle-check.sh --forward .scratch/t75-cmake/lifecycle-forward
bash install-readiness-check.sh --forward  .scratch/t75-cmake/readiness-forward
bash install-readiness-check.sh --mutation .scratch/t75-cmake/readiness-forward

bash /d/TF2_Native_Worktrees/_shared/hostlock.sh release t75-cmake-r1
```

## 8. 证据文件

| 文件 | 内容 | 是否入库 |
|---|---|---|
| evidence/t75-cmake/T75-PHASE2.md | 本文件 | 是 |
| evidence/cmake-build.log | 第 4 步全量构建日志（1097 行） | 否（.gitignore:27，每次构建重写） |
| .scratch/t75-cmake/step8-build.log | 第 8 步 touch 后的构建控制台 | 否（.scratch/） |
| .scratch/build-target.log | 第 8 步的原始 cmake 输出（含 Configuring/Generating 三行） | 否 |
| .scratch/t75-cmake/lifecycle-forward/ | 第 6 步门禁产物 | 否 |
| .scratch/t75-cmake/readiness-forward/ | 第 7 步门禁产物，也是第 7b 步的输入 | 否 |
| .scratch/t75-cmake/readiness-mutation.log | 第 7b 步变异体日志（112 行） | 否 |
| .scratch/t75-cmake/pre-mutation-backup/ | 变异前三个源码的副本与 SHA256SUMS | 否 |
