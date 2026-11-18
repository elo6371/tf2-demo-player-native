# INST1 / E2 — 构建实测：clean、no-op、单文件与公共头的真实代价

- 日期：2026-11-15（本机时间；构建日志内的 UTC 见 sidecar）
- 卡片：`docs/PROJECT-EXECUTION-ROADMAP-2026-11-15.md:129`（E2/N26）
- 场地：主树 `D:/TF2_Native_Worktrees/t105-e5` + 隔离副本 `.scratch/e2-iso` + 独立构建目录 `.scratch/e2-build`
- 证据：`D:/_shared/EXEC-2026-11-15/INST1/E2-out/`
- 作者：arch-ci（实例1）

## 0. 一句话结论

构建时间的**主项不是「改了哪个文件」，而是 `build-target.sh` 的 header stamp guard**：只要有任何表头比 `.headers-stamp` 新，它就把 `native/src/*.cpp` + `native/tools/*.cpp` 全部 touch 一遍（133 个），于是**任何部分改动都变成全量重编**。实测同一处改动，guard 触发时 78 步 / 206–237 s，guard 绕过时 1 步 / 11.7 s（单文件，17.6×）或 17 步 / 101.5 s（`texture2d.h`，2.34×）。而 CMake 自己的依赖图**已经准确**：`compiler_depend.make` 里登记的依赖集合与「实际被重编的 obj 集合」逐元素相等（46/46、17/17）。因此 E2 的改进方向是**收窄或删除 guard**，而不是加缓存、加依赖或换构建系统。

同时有两条必须一并写下的反直觉事实：

1. **no-op 也不是零成本**：地板 5.1 s（主树）/ 6.6 s（副本），几乎全部来自 `.PHONY` 的 `tf2_build_stamp` 目标——它每次构建都要跑一遍 `git ls-files` 内容哈希。
2. **跨构建目录的 exe 字节完全相同**（sha256 逐位一致），这是「改进前后同环境比较」以及 E1「以 exe sha 作帧身份键」的物理前提。

## 1. 本轮约束与场地

| 项 | 状态 |
|---|---|
| host lock | 本轮开始时 `FREE` + `ANOMALY product_running_without_lock=1`（两个别人的产品进程），中途被 `inst3-n13` 短暂占用（`owner=inst3-n13 age=9s/26s heartbeat_limit=900s`），后清场至 `product_processes=0` |
| 主树构建 | **不做**。主树 `native/build-nmake/.headers-stamp` 为 22:26:51，而 `native/include/native_renderer.h` 与 `native/src/native_renderer_shaders.h` 是 22:46:59（别人正在改 renderer），此刻在主树跑 `build-target.sh` 必然触发 guard 并 touch 133 个 .cpp，与实例2 的未提交改动互相干扰 |
| 变更实验 | 全部在 `.scratch/e2-iso`（8.6 MB / **210 个源文件（135 个 .cpp + 75 个 .h）** 的 `cp -a` 副本，自带 `build-target.sh` 与 `env-msvc.sh`） |
| clean 构建 | 独立空目录 `.scratch/e2-build`（`FRESH_TREE=1`，guard 自我跳过） |
| 改动范围 | 只写 `.scratch/e2-build`、`.scratch/e2-iso`、`D:/_shared/.../E2-out/`。**没有触碰 `native/`、没有改 `build-target.sh`、没有提交** |

## 2. clean 构建（`.scratch/e2-build`，全新空目录）

命令：`bash tools/lockrun.sh arch-ci-e2 -- env TF2_BUILD_DIR=.scratch/e2-build bash build-target.sh tf2_demo_native tf2_core`

| 读数 | 值 |
|---|---|
| 墙钟 | **209,816 ms（209.8 s）** |
| `Building CXX object` 步数 | **80** |
| `Linking` 步数 | **2**（`[62%] Linking CXX static library tf2_core.lib`、`[100%] Linking CXX executable tf2_demo_native.exe`） |
| warning / error | **21 / 0** |
| 产物 obj | 83 个 |
| `tf2_core.lib` | 39,579,366 B |
| 日志 | 16,853 B / 228 行 → `E2-out/clean-build.log.txt` |

### 2.1 21 条 warning 的逐条清单（E3b 的第一批现成工单）

本仓源码共 20 条，另 1 条来自 MSVC SDK 头（`xutility(4813)`，wchar_t→char，非本仓）：

| 文件:行 | 代码 | 含义 |
|---|---|---|
| `native/src/camera_bookmarks.cpp:134` | C4456 | 局部变量 `index` 隐藏上一个本地声明 |
| `native/src/camera_bookmarks.cpp:191` | C4456 | 局部变量 `index` 隐藏上一个本地声明 |
| `native/src/model_loader.cpp:536` | C4456 | 局部变量 `i` 隐藏上一个本地声明 |
| `native/src/spectate_target.cpp:309` | C4127 | 条件表达式是常量 |
| `native/src/install_manifest.cpp:164` | C4189 | 局部变量 `dataSize` 已初始化但未引用 |
| `native/src/main.cpp:3584` | C4244 | double → float 可能丢数据 |
| `native/src/main.cpp:3585` | C4244 | 同上 |
| `native/src/main.cpp:3586` | C4244 | 同上 |
| `native/src/main.cpp:3587` | C4244 | 同上 |
| `native/src/main.cpp:3588` | C4244 | 同上 |
| `native/src/main.cpp:11982` | C4457 | 局部变量 `instance` 隐藏函数参数 |
| `native/src/main.cpp:12290` | C4456 | 局部变量 `elapsed` 隐藏上一个局部声明 |
| `native/src/native_renderer.cpp:808` | C4456 | `samplerDescription` 隐藏 |
| `native/src/native_renderer_overlay.cpp:161` | C4100 | 参数 `tick` 未引用 |
| `native/src/native_renderer_overlay.cpp:452` | C4456 | `world` 隐藏 |
| `native/src/native_renderer_viewmodel.cpp:390` | C4267 | size_t → uint32_t 截断 |
| `native/src/native_renderer_viewmodel.cpp:391` | C4267 | 同上 |
| `native/src/native_renderer_viewmodel.cpp:698` | C4456 | `view` 隐藏 |
| `native/src/native_renderer_viewmodel.cpp:868` | C4456 | `view` 隐藏 |
| `native/src/logstf_panel.cpp:376` | C4100 | 参数 `window` 未引用 |

分类（本仓 20 条）：**C4456/C4457 遮蔽 9 条**（`index`×2、`view`×2、`i`、`elapsed`、`samplerDescription`、`world`、`instance`；低风险，改名即可）；**C4244/C4267 截断 7 条**（`main.cpp:3584-3588` 的 double→float 5 条 + `native_renderer_viewmodel.cpp:390/391` 的 size_t→uint32_t 2 条；需逐个证明范围安全，属 E3b「返回值/生命周期」优先档）；**C4100 未引用参数 2 条**（`tick`、`window`）；**C4127 常量条件 1 条**（`spectate_target.cpp:309`）；**C4189 未引用局部 1 条**（`install_manifest.cpp:164`）。合计 9+7+2+1+1 = 20。

### 2.2 exe 身份：跨构建目录逐位相同

| 项 | `.scratch/e2-build`（本轮 clean） | `native/build-nmake`（别人 21:34 构建） |
|---|---|---|
| 大小 | 3,017,216 B | 3,017,216 B |
| sha256 | `7a8da164de67553281065ef76638c135a49baf537cb5027865bca4f39f3992ff` | **同左，逐位一致** |

机制链：全局 `/Brepro`（`native/cmake/tf2-settings.cmake:31`，实测 57 个目标里 55 个带 `/Brepro`、0 个不带）+ 链接行含 `/INCREMENTAL:NO ... /Brepro` + 由 `sourceDateEpoch` 固定的 stamp。

**这是两件事的物理前提**：① 「改进前后同环境比较」成立（换构建目录不换字节）；② E1 用 exe sha256 当帧缓存键成立。

### 2.3 sidecar（`.scratch/e2-build/tf2_demo_native.identity`）

```
format=tf2-build-identity-sidecar
formatVersion=1
target=tf2_demo_native
exeSha256=7a8da164de67553281065ef76638c135a49baf537cb5027865bca4f39f3992ff
exeBytes=3017216
contentBinding=recompiled
sourceContent=11bf1bb1c46e517f95e54e0e9ac8039d92a536d4476c59ebb29e273d62de8895
contentMethod=git-clean-v1
sourceTree=6f3aa8fc0120
sourceDateEpoch=1794752160
dirty=1
dirtyFiles=8
dirtyTracked=4
dirtyUntracked=4
buildUtc=2026-11-15T14:22:19Z
```

`sourceContent` 与 R1 独立重算的 `11bf1bb1…` **一致**（那次重算早于 HEAD 推进到 `d733831b`，说明内容身份只跟着**被编译的输入**走，不跟 docs 提交走——这本身是内容身份设计的正确表现）。

## 3. no-op 构建：5.1 s 的地板在哪

| 场地 | 墙钟 | 编译步 | 链接步 | exe sha256 |
|---|---|---|---|---|
| `.scratch/e2-build`（主树源码） | **5,098 ms** | 0 | 0 | 未变 |
| `.scratch/e2-iso`（副本，`raw-v1` 内容身份） | **6,615 ms** | 0 | 0 | 未变 |

no-op 的日志只有 6 行，无一行 `Building`：

```
[ 62%] Built target tf2_core
[ 65%] build identity: content hash -> generated/tf2_build_stamp.cpp, provenance -> generated/tf2_build_stamp.identity
-- build identity: ... stamp=unchanged
[ 65%] Built target tf2_build_stamp
[100%] Built target tf2_demo_native
[100%] Built target tf2_core
```

### 3.1 地板成本归因（实测拆分）

| 调用 | 墙钟 |
|---|---|
| `build-target.sh tf2_demo_native` 单独 | **3,711 ms** |
| `build-target.sh tf2_core` 单独 | **1,956 ms** |
| 两者一起 | **5,187 ms** |

`tf2_build_stamp` 是 `.PHONY` 目标（`native/build-nmake/CMakeFiles/tf2_build_stamp.dir/build.make:59/63/67/71` 四处 `.PHONY`），每次构建都真跑一遍 `gen-build-stamp.cmake`：`git ls-files` 扫描 + 内容哈希。**这就是 no-op 的地板。** 副本的 6.6 s 更高，因为副本走 `raw-v1` 回退（对 210 个文件逐个 sha256）。

**改进切口**（属 E2 结论文档，不在本片动手）：「内容身份未变时跳过 stamp 重生成」，而不是去动 CMake 依赖图。

## 4. 变更影响面实验（A–J，全部实测）

方法：在副本里 `touch` 一个文件，然后分别经 `build-target.sh`（guard 生效）与直接 `cmake --build`（guard 绕过）各跑一次；步数从 cmake 原始日志数（**不是**从 `build-target.sh` 的 stdout 数——见 §8.1）。

| # | 改动 | 通道 | 墙钟 | 编译步 | 链接步 |
|---|---|---|---|---|---|
| A | `src/world_material.cpp` | guard 生效 | 206,070 ms | **78** | 2 |
| B | `include/vpk_archive.h` | guard 生效 | 210,504 ms | **78** | 2 |
| C | `include/vpk_archive.h` | **guard 绕过** | 138,649 ms | **46** | 2 |
| D | `src/world_material.cpp` | **guard 绕过** | 8,571 ms | **1** | 2 |
| E | `src/world_material.cpp`（当时有表头比 stamp 新） | guard 生效 | 183,892 ms | 78 | 2 |
| F | 紧接着的 no-op | — | 6,615 ms | 0 | 0 |
| G | `src/world_material.cpp`（stamp 新鲜，`headers newer than stamp, before: 0`） | guard 生效（不触发） | **11,732 ms** | **1** | 2 |
| H | `include/texture2d.h` | guard 生效 | 232,708 ms | **78** | 2 |
| I | `include/texture2d.h` | **guard 绕过** | **101,545 ms** | **17** | 1 |
| J | `include/texture2d.h` | guard 生效 | 237,499 ms | **78** | 2 |

### 4.1 三组对照

**单文件改动**：G = 11.7 s / 1 步（guard 不触发）；D = 8.6 s / 1 步（绕过）；A/E = 206 s / 183.9 s、78 步（触发）→ **guard 触发时贵 17.6×，且 78 步已经接近 clean 的 80 步，等于重编一遍。**

**扇出 17 的表头**：I = 101.5 s / **17 步**；J = 237.5 s / **78 步** → 同一改动贵 **2.34×**，多编 **61 个 TU**。

**扇出 46 的表头**：C = 138.6 s / **46 步**；B = 210.5 s / **78 步** → 贵 1.52×，多编 32 个 TU。

### 4.2 集合等式：CMake 的依赖图与实际重编集合逐元素相等

这不是「步数接近」，而是**集合相等**（用 `comm` 双向差集验证，两侧都为空）：

| 表头 | `compiler_depend.make` 里登记的依赖 obj | 绕过 guard 后实际重编的 obj | 双向差集 |
|---|---|---|---|
| `native/include/vpk_archive.h` | **46** | **46** | 空 / 空 |
| `native/include/texture2d.h` | **17** | **17** | 空 / 空 |

**结论**：依赖信息没有丢，`build-target.sh:34-49` 那段「Every `.obj.d` under the build tree is empty: 144 of 144, measured after a clean build on 2026-10-11」的头注释**已经过时**——本轮同一棵树上：

| 读数 | 值 |
|---|---|
| `*.obj.d` 总数 | **135** |
| 非空 | **133**（仅 2 个空，都对应生成文件 `generated/tf2_build_stamp.cpp.obj.d`） |
| `tf2_demo_native.dir/src/main.cpp.obj.d` | **517 行**，其中 **60 行**是本仓头 |
| `tf2_demo_native.dir/compiler_depend.make` | 1,298,183 B / 9,460 行（`.obj:` 行 8,526） |
| `tf2_core.dir/compiler_depend.make` | 1,194,588 B |
| `depend.make`（另一个文件） | 仍为空（94 B / 101 B 的 `# Empty dependencies file`） |

→ **依赖记录在 `compiler_depend.make`，不在 `depend.make`。** `build.make:51-53` 同时 include 两者，所以依赖实际生效。原注释的观察对象是 `depend.make`（或 2026-10-11 时的另一版 CMake），不是今天的实际行为。

## 5. 表头扇出普查（68 个本仓表头）

方法：解析全部 57 个 `compiler_depend.make`（17,159 行依赖），按「右侧路径 = 本仓 `native/{include,src,tools,third_party}` 下的一级 `.h/.hpp`」统计去重后的 obj 数。**TOP 12**：

| 表头 | 扇出 | 占 68 |
|---|---|---|
| `native/include/vpk_archive.h` | **49** | 0.72 |
| `native/include/audio_resource.h` | 42 | 0.62 |
| `native/include/audio_timeline.h` | 41 | 0.60 |
| `native/include/demo_header.h` | 39 | 0.57 |
| `native/include/vmt_material.h` | 28 | 0.41 |
| `native/include/item_schema.h` | 26 | 0.38 |
| `native/include/asset_root.h` | 26 | 0.38 |
| `native/include/model_loader.h` | 25 | 0.37 |
| `native/include/viewmodel_material.h` | 23 | 0.34 |
| `native/include/animation_decoder.h` | 20 | 0.29 |
| `native/include/bsp_map.h` | 19 | 0.28 |
| `native/include/animation_binding.h` | 19 | 0.28 |

分布：**中位数 5**、**max 49**、**min 1**；扇出 = 1 的 5 个（`include/target_identity.h`、`include/pcf_operators.h`、`include/proto_wire.h`、`include/logstf_webview.h`、`include/material_slot_resolution.h`）；扇出 ≤ 5 的 36 个；扇出 ≥ 20 的 10 个。

**口径修正（我先前两版都错了，这里留痕）**：第一版正则只匹配 `native\include\` 单层（60 表头 / 中位数 2）；第二版只认单反斜杠段且数错了行（85 obj / 68 表头 / 中位数 5）。**第三版（本表）以「按 `.obj: ` 分列、右侧逐项剥离引号与反斜杠」为准**，并已用「扇出 17 ↔ 实际重编 17」「扇出 46 ↔ 实际重编 46」两组集合等式交叉验证。另注：`grep -c "include.texture2d.h"` 会得 18 而非 17，第 18 条是 CMake 生成的头文件 stanza 行（`...include	exture2d.h:`，冒号在路径后、无依赖），不是依赖行。

## 6. `tf2_core` 参与链接与 `/W4` 覆盖（卡片要求「先验证」的两项）

### 6.1 `tf2_core` 是 STATIC 库，include/定义/链接全部 PUBLIC 传播

`native/cmake/tf2-libraries.cmake`：

```cmake
:30   add_library(tf2_core STATIC ...)
:104  target_include_directories(tf2_core PUBLIC include third_party/lzma)
:109  target_compile_definitions(tf2_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN ${TF2_VERSION_DEFINES})
:113  target_compile_options(tf2_core PRIVATE /W4 /permissive- /utf-8 /EHsc)
:117  target_link_libraries(tf2_core PUBLIC winmm msacm32)
```

产品链接行的直接证据（`native/build-nmake/CMakeFiles/tf2_demo_native.dir/build.make`）：`:687` 依赖边 `tf2_demo_native.exe: tf2_core.lib`；`:691` 链接参数 `... /machine:x64 /INCREMENTAL:NO /subsystem:windows /Brepro /DYNAMICBASE /NXCOMPAT tf2_core.lib d3d11.lib dxgi.lib ...`。→ **`tf2_core` 确实参与产品链接，且 `NOMINMAX`/`WIN32_LEAN_AND_MEAN` 通过 PUBLIC 传到产品。**

### 6.2 `/W4` 只覆盖 7 个目标（57 个中）

扫 55 个 `flags.make`（57 个 `.dir` 中 `tf2_build_stamp.dir` 与 `tf2_identity.dir` 没有这个文件）的 `CXX_FLAGS` 行：

| 带 `/W4` | 不带 |
|---|---|
| `tf2_core`、`tf2_demo_native`、`entity_message_fixture_probe`、`entity_protocol_probe`、`hud_probe`、`minimap_probe`、`review_workspace_probe`（**7 个**） | 其余 **48 个** |

`tf2_build_stamp` / `tf2_identity` 无 `flags.make`（生成/自定义目标）。声明点：`tf2-libraries.cmake:113`、`tf2-package.cmake:88/89/90/93/95/97`。产品与 core 的 `CXX_FLAGS` 实测：`/DWIN32 /D_WINDOWS /EHsc /O2 /Ob2 /DNDEBUG -std:c++17 -MT /Brepro /W4 /permissive- /utf-8`。

→ **E3b「按目标逐批治理」的真实范围是 7 个目标**（不是 57）。其余 48 个目标当前不受 `/W4` 约束，若要让它们也进 W4，那是另一张卡（会一次冒出未知数量的新警告）。

## 7. 三条结论与改进论证

### 7.1 guard 把「改一个文件」变成「全量重编」

| 改动 | 无 guard | 有 guard | 倍数 |
|---|---|---|---|
| 1 个 .cpp | 1 步 / 8.6–11.7 s | 78 步 / 183.9–206 s | **17.6×** |
| 1 个表头（扇出 17） | 17 步 / 101.5 s | 78 步 / 237.5 s | **2.34×** |
| 1 个表头（扇出 46） | 46 步 / 138.6 s | 78 步 / 210.5 s | **1.52×** |
| clean 全量 | — | 80 步 / 209.8 s | 参照系 |

**论证**：guard 的代价与「改动大小」无关，只与「是否触发」有关——触发即等于 clean。而它要防范的风险（头文件依赖不被记录）在本轮已被 §4.2 的集合等式否定。

### 7.2 no-op 地板是 `.PHONY` 的 `tf2_build_stamp`

5.1 s / 0 步。切法是「内容身份未变时跳过 stamp 重生成」，需要 `gen-build-stamp.cmake` 先做一次廉价预判（例如 `git status --porcelain` 为空且 HEAD 未变则直接复用上一份 stamp），**而不是动依赖图**。

### 7.3 数据不支持加构建缓存/新依赖

卡片原文要求「数据没证明瓶颈不加缓存/新依赖」。本轮数据：瓶颈 100% 在 guard 触发后的重复编译（78 步 = clean），**没有一个读数是「编译本身慢」**（单 TU 8.6–11.7 s 含链接与 CMake 自身开销；链接 2 步稳定出现）。因此：

- **不要**引入 ccache / sccache / distcc：它们优化的是「同样的 TU 再编一次」，而 guard 的问题恰恰是「制造出 61–77 个本来不需要编的 TU」；删掉 guard 后缓存命中率自然归零，不需要它。
- **不要**加 `-MP`/`-MD` 之类依赖相关开关：依赖已经在 `compiler_depend.make` 里齐全。
- **唯一值得做**：收窄 guard（或整体删除并保留 `FRESH_TREE` 分支），以及 stamp 的预判跳过。

## 8. 两个必须写下的坑（复现时会撞到）

### 8.1 `build-target.sh` 把 cmake 输出重定向进自己的日志，stdout 里没有步数

`build-target.sh:136` 是 `cmake --build "$BUILD" --target "$@" > "$LOG" 2>&1`（`LOG=".scratch/build-target.log"`）。所以对 `bash build-target.sh ... > capture.txt` 的 `capture.txt` grep `Building CXX object` **恒为 0**。本轮第一版实验因此得出「所有实验都是 0 步」的假读数。**正确做法：从 `$ISO/.scratch/build-target.log`（cmake 原始输出）数，或直接跑 `cmake --build` 并捕获。** 另：`grep -c` 在 0 匹配时退出码非 0，配合 `set -e` 会静默中断脚本，应写 `grep -ac ... || true`。

### 8.2 副本的内容身份是 `raw-v1`，主树是 `git-clean-v1`

`native/cmake/gen-build-stamp.cmake:97-103` 跑 `git ls-files -s -- src include third_party cmake CMakeLists.txt`，`WORKING_DIRECTORY` 是 `TF2_REPO_DIR`（副本里 = `native/`）。副本的 `native/` **不是仓库根**（`.scratch/e2-iso` 只是 `cp -a` 的拷贝，虽然 `git rev-parse --show-toplevel` 会向上找到真仓库），于是 `git ls-files` 在该目录下拿不到跟踪清单，落到 `:333-356` 的 `raw-v1` 回退（按文件 sha256 逐行）。

| 项 | 主树 / `.scratch/e2-build` | 副本 `.scratch/e2-iso` |
|---|---|---|
| `contentMethod` | `git-clean-v1` | `raw-v1` |
| `sourceContent` | `11bf1bb1…` | `05b88612…` |
| `exeSha256` | `7a8da164…` | `6cd55501d3c6c883…`（前 16 位） |
| exe 大小 | 3,017,216 B | 3,017,216 B（相同） |

**「字节相同」不等于「内容身份相同」。** 本轮的变更实验全部在副本里做，因此那些实验的 exe 字节可跨场地比较（同一 sha16），但 sidecar 的 `sourceContent` 不可跨场地比较。

## 9. 未证明项

1. **主树的 clean 构建未做**（主树表头被他人改过、产品进程占用 exe）。本报告的 clean 读数来自 `.scratch/e2-build`（同源码、同工具链、exe 字节相同），但「主树自身 clean 也 209.8 s」未直接测。
2. **21 条 warning 只测了一次 clean**，未做「同一 TU 重复编译是否稳定产出同样 21 条」的验证。
3. **`/W4` 覆盖 7/57 是按 `flags.make` 的 `CXX_FLAGS` 文本判定**，未验证「这 7 个目标的 obj 是否真的带着 W4 编出来」（间接证据：clean 构建的 21 条 warning 全部来自这 7 个目标里的文件，符合预期）。
4. **扇出表基于当前构建目录的 `compiler_depend.make`**；若 CMake 重新 configure 或换生成器（Ninja），`compiler_depend.make` 文件形态会变，本法不适用。
5. **guard 收窄后的收益未实测**（本片只做测量，不改 `build-target.sh`，因为它是主线共享脚本，改它需要 lead 裁决）。
6. **副本里 A/E 两轮的「假读数」我已识别但现在无法重放**：E 当时确有表头比 stamp 新（所以它其实是一次合法的 guard 触发，只是我的假设错了），我在报告里把它标为「假设错、读数本身有效」。

## 10. 复现命令

```bash
# clean（独立构建目录；无 guard 干扰）
bash tools/lockrun.sh arch-ci-e2 -- env TF2_BUILD_DIR=.scratch/e2-build bash build-target.sh tf2_demo_native tf2_core

# no-op（同一目录再来一次，5 s 级）
env TF2_BUILD_DIR=.scratch/e2-build bash build-target.sh tf2_demo_native tf2_core

# 变更实验（全部在副本里，脚本已存证）
bash D:/_shared/EXEC-2026-11-15/INST1/E2-out/run-change-experiment.sh

# 扇出表重建
cd native/build-nmake/CMakeFiles && awk -f D:/_shared/EXEC-2026-11-15/INST1/E2-out/fanout.awk */compiler_depend.make | sort -rn

# 集合等式（依赖侧 vs 实际重编侧）
bash D:/_shared/EXEC-2026-11-15/INST1/E2-out/fanout6.sh
bash D:/_shared/EXEC-2026-11-15/INST1/E2-out/fanout7.sh
```

## 11. 证据文件索引

| 文件 | 内容 |
|---|---|
| `E2-out/clean-build.log.txt` | clean 构建全文（16,853 B / 228 行），含测量头注释 |
| `E2-out/change-experiment.txt` | A–J 全部轮次的读数（含前两轮失败现场） |
| `E2-out/run-change-experiment.sh` | 最后一版实验脚本 |
| `E2-out/header-mtimes-before.json` | 68 个表头的 mtime 基线（3,428 B） |
| `E2-out/header-fanout.txt` | 68 行扇出表（`扇出<TAB>绝对路径`） |
| `E2-out/fanout.awk` / `deps-of.awk` / `built.awk` | 扇出与集合等式的三个 awk 程序 |
| `E2-out/fanout{,2,3,4,5,6,7}.sh` | 口径核对脚本（含 17 vs 18 的排查过程） |
| `E2-out/cmake-{a..j}-*.log` | 每轮 cmake 原始输出（步数从这里数） |
| `E2-out/stdout-{a,b,e,f,g,h,j}-*.txt` | `build-target.sh` 的 stdout（证明它不含步数，见 §8.1） |

