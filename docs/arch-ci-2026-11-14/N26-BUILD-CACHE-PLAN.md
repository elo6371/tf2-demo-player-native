# N26 — 构建缓存与头文件依赖：测量计划（arch-ci, 2026-11-14）

> 作者：执行卡 E5（替换实例）。写盘范围：`docs/arch-ci-2026-11-14/N26-BUILD-CACHE-PLAN.md` 一个文件。
> 不提交：本卡不产生 git commit，改动留在工作树。
> 状态：测量计划。本文件不执行任何构建，也不改任何头文件；§3–§5 是留给下一个窗口的动作，全部按"先记录、再动手、后恢复"写死。

## 0. 一句话结论

派工给的前提是"144 个 `.obj.d` 全空、`--filter-prefix` 与本地化 `cl` 输出不匹配，所以改头文件可能不触发重编"。本会话实测该前提在**当前这棵树**上不成立：`native/build-nmake` 下 134 个 `.obj.d` 里 132 个非空（两个空的是 `generated/tf2_build_stamp.cpp`，该 TU 本来就没有头文件），样本有 14,946 B、含 `native/include/animation_decoder.h`；`CMakeFiles/tf2_demo_native.dir/compiler_depend.make` 有 1,297,720 B / 9454 行 / 3470 条 `include` 行，项目头的绝对路径就在里面，且被 `build.make:53` include。
但"文件里有依赖"不等于"nmake 会据此重编"——这一步从未被观测。所以 N26 的结论不是"缓存好/坏"，而是：**当前没有任何一次受控测量回答过"改一个头，重编了几个 TU"；本文件给出这次测量怎么做、怎么判、预计多久，并且明确 `build-target.sh` 的 touch-all 保护会把任何头文件改动放大成全量重编。**

## 1. 现状实测（本会话，全部只读）

| 观测点 | 命令 | 实测 |
| --- | --- | --- |
| 依赖文件数量与空文件 | `find native/build-nmake -name "*.obj.d"` + `-size 0` | 134 个；2 个 0 字节，均为 `CMakeFiles/*/generated/tf2_build_stamp.cpp.obj.d`；132 个非空 |
| 样本内容 | `head native/build-nmake/CMakeFiles/animation_viewmodel_probe.dir/tools/animation_viewmodel_probe.cpp.obj.d` | 14,946 B；85 处 `.h`；首行是 `native/include/animation_decoder.h`，其后是 MSVC 的 `array`/`yvals_core.h`/`vcruntime.h` |
| 旧构建树 | `find native/build -name "*.obj.d"` | 无匹配（该树没有 dep 文件） |
| 消费链 | `grep -n "compiler_depend" native/build-nmake/CMakeFiles/tf2_demo_native.dir/build.make` | `:53 include CMakeFiles\tf2_demo_native.dir\compiler_depend.make` |
| obj 规则 | `sed -n "64,68p" .../build.make` | `:64-66` 依赖 `flags.make`、`src/main.cpp`、`compiler_depend.ts`；`:68` 才跑 `cmake_cl_compile_depends --dep-file=...main.cpp.obj.d --filter-prefix="注意: 包含文件:  "`（文件里存成 GBK 乱码） |
| 依赖内容 | `grep -c "main\.cpp\.obj:" compiler_depend.make`；`grep -m3 -o "[A-Za-z]:[^ ]*native.include.[A-Za-z_]*\.h"` | 517 条；命中 `D:\TF2_Native_Worktrees\t105-e5\native\include\demo_header.h`、`audio_timeline.h`、`audio_resource.h` 等绝对路径 |
| 生成器 | `grep CMAKE_GENERATOR native/build-nmake/CMakeCache.txt` | `CMAKE_GENERATOR:INTERNAL=NMake Makefiles`；`CMAKE_BUILD_TYPE:STRING=Release`；`CMAKE_CXX_COMPILER:UNINITIALIZED=cl` |
| 汇总 Makefile | `wc -l` / `grep -c "\.obj:"` | 195,896 B / 4557 行 / 266 个 `.obj:` 规则；`obj.d` 出现 0 次 |
| 当前树是否最新 | `stat` + `sha256sum` | `native/src/main.cpp` = 794,774 B @2026-11-14 19:28:35.855；`main.cpp.obj` = 10,158,790 B @19:22:35.03；`tf2_demo_native.exe` = 2,991,616 B @19:22:35.59（sha256 `1c3580e2…0797e`）；源比 exe 新约 6 min，**树当前是 stale 的** |
| 头文件时间戳 | `stat native/include/animation_decoder.h` | 3,577 B @2026-11-10 18:37 |
| 保护戳 | `stat native/build-nmake/.headers-stamp` | 0 B @2026-11-14 19:22:35.94 |

## 2. 三条待测假设与判据

| 假设 | 陈述 | 判别输出 | 若成立的含义 |
| --- | --- | --- | --- |
| H1 | 改头后只有 include 它的 TU 重编，依赖链生效 | 构建日志 `Building CXX object` 行数 k 满足 0 < k < 总 TU 数，且 k 的文件集合 = 依赖该头的 TU 集合 | `compiler_depend.make` 真的被 nmake 消费；`build-target.sh:124-135` 的 touch-all 是纯浪费 |
| H2 | 改头后 0 个 TU 重编，stale obj 被链进 exe | k = 0，exe 的 mtime/sha 不变或只链接不变 | 派工前提在当前树复现；必须保留 touch-all 保护 |
| H3 | 走 `build-target.sh` 时保护把 H1 放大成全量重编 | 先出现 `a header is newer than $STAMP; touching sources (header deps are untracked)`（`build-target.sh:131`），随后 k = 全部 TU | 单头改动固定成本 ≈ 全量构建 |

## 3. 测量方案 A：单头改动 → 重编了几个 TU（直连 cmake，绕过保护）

### 3.1 先把树拉回 baseline（否则 k 被污染）

当前 `native/src/main.cpp` 比 exe 新 6 min（§1 末尾一行），说明有一次未完成的构建。**先做一次无改动构建**，让 exe 与 obj 恢复一致，再开始实验；否则 k 里会混入上一次的残留 TU。

### 3.2 前置记录（不动文件）

```bash
# 本卡建议在 .scratch/n26/ 下存快照，不写 docs
mkdir -p .scratch/n26
stat -c "%Y %s %n" native/build-nmake/tf2_demo_native.exe native/build-nmake/CMakeFiles/tf2_demo_native.dir/src/main.cpp.obj native/include/animation_decoder.h > .scratch/n26/before.stat
sha256sum native/build-nmake/tf2_demo_native.exe > .scratch/n26/before.sha
cp native/include/animation_decoder.h .scratch/n26/animation_decoder.h.bak
grep -l "animation_decoder" native/build-nmake/CMakeFiles/*/*.obj.d > .scratch/n26/dependents.txt
wc -l < .scratch/n26/dependents.txt   # 期望就是这个头的依赖 TU 数 k_ref
```

`k_ref` 必须 ≥1 且远小于 51（51 TU 是历史 clean build 的 TU 数，见 `docs/PROJECT-NEXT-TASKS-2026-11-14.md:326`）；若 `dependents.txt` 为空，说明 `.obj.d` 里没有这个头的依赖，改用 `.obj.d` 中命中率最高的项目头，并在记录里写明换头原因。

### 3.3 两种改法都要测（内容 vs 仅 mtime）

| 改法 | 动作 | 测的是什么 |
| --- | --- | --- |
| A1 只碰 mtime | `touch native/include/animation_decoder.h` | nmake 是否只按时间戳重编依赖该头的 TU |
| A2 改内容 | 在文件末尾追加一行 `// n26 probe`（不改语义），再 `touch` | 内容变化 + 新 mtime 的完整路径 |

A1 与 A2 分开做、分开计数；A1 的 k 与 A2 的 k 不同即说明有一步依赖内容/时间戳区分。

### 3.4 构建（直连，不经过 `build-target.sh`）

```bash
cmake --build native/build-nmake --target tf2_demo_native > .scratch/n26/build-a2.log 2>&1; echo "rc=$?"
grep -c "Building CXX object" .scratch/n26/build-a2.log   # k
grep "Building CXX object" .scratch/n26/build-a2.log | sed "s/.*CMakeFiles\///" > .scratch/n26/actually-built.txt
```

直连 `cmake --build` 的原因：`build-target.sh:124-135` 在头比 stamp 新时会先 `touch` 所有 `native/src/*.cpp`、`native/tools/*.cpp`，那样 k 恒为全量，H1/H2 都测不到。直连构建**不改工作树脚本**，也不跑产品。

### 3.5 判据（三个数字闭合）

| 观测 | H1 | H2 | 其它 |
| --- | --- | --- | --- |
| `k = grep -c "Building CXX object"` | 0 < k < 51 | k = 0 | k = 51 → 依赖全失效但构建仍跑（与 H2 不同，要单独记） |
| `actually-built.txt` 与 `dependents.txt` 对应关系 | 两边 TU 集合相同 | 空 | 只重编了一部分依赖者 → 记录差集 |
| `exe` 的 `sha256` 与 `before.sha` | 改变（至少链接变） | 不变或仅 mtime 变 | — |
| 新 obj 的 mtime | 更新 | 未更新 | — |

### 3.6 恢复（必须做，且要在同一窗口内）

```bash
cp .scratch/n26/animation_decoder.h.bak native/include/animation_decoder.h
touch native/include/animation_decoder.h
cmake --build native/build-nmake --target tf2_demo_native > .scratch/n26/build-restore.log 2>&1
stat -c "%Y %s %n" native/build-nmake/tf2_demo_native.exe native/include/animation_decoder.h >> .scratch/n26/after.stat
```

恢复构建同时是第三次观测：头改回旧内容（mtime 更新）后重编的 TU 数应等于 A2 的 k（同一依赖集合）。用备份 cp 恢复而不是 `git checkout`，避免动工作树的 git 状态。

### 3.7 时长估算

历史 clean build 221 s / 51 TU ≈ 4.3 s/TU（`docs/PROJECT-NEXT-TASKS-2026-11-14.md:326`、`docs/F2-2-PROBE-SWEEP-2026-11-13.md:115`），另有链接时间；单次 A1/A2 若 k≈3–20，预计 15–90 s；三次构建（A2、恢复、验证）合计 2–4 min（估算，未实测）。这比一次全量 221 s 便宜，因此方案可执行。

## 4. 测量方案 B：`build-target.sh` 的 touch-all 保护成本

### 4.1 触发条件（代码原文位置）

- `build-target.sh:124-135`：`STAMP="$BUILD/.headers-stamp"`；stamp 缺失 → `note` + touch `native/src/*.cpp native/tools/*.cpp`；stamp 存在但 `find native/include native/src native/tools native/third_party \( -name "*.h" -o -name "*.hpp" \) -newer "$STAMP"` 有输出 → `note "a header is newer than $STAMP; touching sources (header deps are untracked)"` + touch 全部源文件；然后才 `cmake --build "$BUILD" --target "$@"`（`:136`）。
- 记录依据是 `build-target.sh:34-49` 的注释：`Every .obj.d under the build tree is empty: 144 of 144, measured after a clean build on 2026-10-11`，并写明"a header newer than the stamp forces the requested targets to really rebuild by touching their sources"，stamp 在构建树里、clean 会重置。

### 4.2 动作与观测

```bash
# 前置：stamp 必须比所有头新；若头刚被动过，先跑一次 build-target.sh（会全量重编一次）让 stamp 追上
stat -c "%y %n" native/build-nmake/.headers-stamp
touch native/include/animation_decoder.h
./build-target.sh tf2_demo_native > .scratch/n26/build-b.log 2>&1; echo "rc=$?"
grep -n "header deps are untracked" .scratch/n26/build-b.log
grep -c "Building CXX object" .scratch/n26/build-b.log   # k_B
```

### 4.3 判据

| 观测 | 判据 | 含义 |
| --- | --- | --- |
| 日志里有 `header deps are untracked` 行 | 出现 | touch-all 被触发（H3 成立的必要条件） |
| `k_B` | 等于全量 TU 数（51 或当前实数） | 单头改动被放大成全量，成本 221 s 量级 |
| `k_B` 与方案 A 的 k | 比值 = 放大倍数 | 保护换来的确定性成本 |

## 5. 测量方案 C：no-op 构建（缓存命中的最小判据）

不改任何文件，直连构建一次：`cmake --build native/build-nmake --target tf2_demo_native > .scratch/n26/build-noop.log 2>&1`，记录 `grep -c "Building CXX object"`（期望 0）与墙钟（期望 <10 s，未实测）。
这个数字是 CI 里"缓存命中"的判据：**0 行编译 + 退出码 0**。它只有在方案 A 证明依赖链生效之后才有意义；依赖失效时 no-op 也会是 0 行，两者输出相同、含义相反。

## 6. 判据汇总

| 编号 | 判据 | 期望 | 失败含义 |
| --- | --- | --- | --- |
| C1 | A1/A2 的 k > 0 | 依赖链生效 | H2：改头不重编，stale obj 进 exe |
| C2 | actually-built ⊆ dependents 且相等 | 精确重编 | 多编 → 依赖表超集；少编 → 依赖表缺项 |
| C3 | 恢复后 k = A2 的 k | 可重复 | 不可重复 → 测量本身有噪声（并发/镜像锁） |
| C4 | B 的 k_B = 全量 | 保护生效 | 保护没触发 → header 时间戳没被看见，H3 不成立 |
| C5 | no-op 的 k = 0 | 缓存命中 | 非 0 → 有东西每次都重编（生成文件时间戳） |

## 7. N26 的帧时长 / VRAM 行：当前不可测，需先新增测量能力

| 想测的量 | 现状证据 | 结论 |
| --- | --- | --- |
| 帧时长（per-frame GPU/CPU 时序） | `grep -rn "ID3D11Query\|D3D11_QUERY\|TIMESTAMP" native/` 命中 0 处 | 当前不可测，需先新增测量能力 |
| 显存占用（VIDEO_MEMORY_INFO） | `grep -rn "QueryVideoMemoryInfo\|DXGI_QUERY_VIDEO_MEMORY_INFO" native/` 命中 0 处 | 当前不可测，需先新增测量能力 |

现有 capture 路径（`native/src/main.cpp:12742-12755`）只能在指定 tick 请求一张帧，输出的是像素，不是时序与显存；它不能替代帧时长测量。新增能力的最小形态（D3D11 timestamp query pair 或 `IDXGIAdapter3::QueryVideoMemoryInfo`）不在本卡范围，本卡只落"当前不可测"这一行。

## 8. 与 N22 / N24 的接口

- N22 已给出"仓库没有任何 CI"；N26 的 C5（no-op = 0 行）是未来缓存命中 job 的第一条判据，C1–C4 是它成立的前提。
- N24 把 `main.cpp` 切成多文件后，TU 数会从当前的 51 变化；本文件的 `k_ref` 与 `k` 都必须按当时的 TU 实数复测，不能沿用 51。
- 构建时长基线：221 s（历史实测，`docs/PROJECT-NEXT-TASKS-2026-11-14.md:326`）；本卡不重测 clean，只测增量与 no-op。

## 9. 风险与顺序

1. **当前树 stale**（`main.cpp` 比 exe 新 6 min）：任何实验前必须先完成一次构建，否则 k 被污染（§3.1）。
2. Windows 镜像锁：`build-target.sh:138-144` 对失败构建重试一次；实验期间不得并发第二个构建（同一 `native/build-nmake`）。
3. 直连 `cmake --build` 绕过了 touch-all 保护，但它**不会**改工作树脚本；实验后必须用备份 cp 恢复头文件并再构建一次（§3.6），保证交给下一个人时的 exe 与源一致。
4. 方案 B 会真的触发全量重编（约 221 s 量级），必须在持有构建窗口时做，不能与 `hostlock.sh` 的其它使用者重叠。
5. 不跑产品 exe：本文件全部判据只读构建日志、mtime 与 sha256。

## 10. 证据

| 命令 | 结果 |
| --- | --- |
| `find native/build-nmake -name "*.obj.d" -size 0` | 2 个，均为 `generated/tf2_build_stamp.cpp.obj.d` |
| `head CMakeFiles/animation_viewmodel_probe.dir/tools/animation_viewmodel_probe.cpp.obj.d` | 14,946 B，首行 `native/include/animation_decoder.h`，85 处 `.h` |
| `grep -c include CMakeFiles/tf2_demo_native.dir/compiler_depend.make` | 3,470 |
| `grep -m3 -o "[A-Za-z]:[^ ]*native.include.[A-Za-z_]*\.h" compiler_depend.make` | `D:\TF2_Native_Worktrees\t105-e5\native\include\demo_header.h`、`audio_timeline.h`、`audio_resource.h` |
| `sed -n "53p;64,68p" CMakeFiles/tf2_demo_native.dir/build.make` | `include …compiler_depend.make`；obj 依赖 flags.make/src/main.cpp/compiler_depend.ts；recipe 跑 `cmake_cl_compile_depends --depfile … --filter-prefix="注意: 包含文件:  "` |
| `stat -c "%Y %s %n"` 四个文件 | exe @19:22:35.59 / main.cpp.obj @19:22:35.03 / main.cpp @19:28:35.855 / .headers-stamp @19:22:35.94 |
| `grep -rn "ID3D11Query\|D3D11_QUERY\|TIMESTAMP\|QueryVideoMemoryInfo\|DXGI_QUERY_VIDEO_MEMORY_INFO" native/` | 0 命中 |

## 11. 本文件没有证明什么

- 没有执行任何构建：A/B/C 三个方案一次都没跑，k、k_B、no-op 时长全部是待测量，不是实测值。
- 没有证明依赖链失效或生效：`compiler_depend.make` 有项目头路径只说明文件存在且被 include，nmake 会不会据此重编，只有方案 A 能回答。
- 没有解释派工前提（144/144 空）与本次实测（132/134 非空）为什么不同；`build-target.sh:34-49` 的记录是 2026-10-11 的 clean build，本树此后重建过多次，三种候选解释（CMake 版本、`cl` 输出语言、构建树重建时间）均未验证。
- 没有测帧时长与显存：native/ 下 0 处查询/TIMESTAMP 代码，当前不可测，需先新增测量能力。
- 没有给出 clean build / 单 cpp 改动 / no-op 三种构建的实测时长，只有历史值 221 s 可引用。
- 没有改任何脚本、头文件或构建树；恢复步骤（§3.6）本身也未执行。
