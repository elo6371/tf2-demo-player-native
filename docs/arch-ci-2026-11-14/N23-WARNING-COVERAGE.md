# N23 — /W4 覆盖与告警实测（arch-ci，2026-11-14）

> 作者：arch-ci（角色 F）。写域：docs 新增。未改任何源码、CMake、脚本，未提交 git。
> 基线：candidate/e5 @ a901ece。工作树 D:/TF2_Native_Worktrees/t105-e5。

## 0. 一句话结论

**真正的发现不是「57 条告警」，而是 55 个目标里只有 7 个在 /W4 下编译（12.7%），其余 48 个落 MSVC 默认 /W1，而且没有任何机制会阻止第 49 个也落进去。** 告警数是症状，默认 /W1 才是病。

## 1. 口径（先读这一节，否则后面的数字会被误读）

- 目标总数 **55** = 54 个 add_executable 目标（去重后）+ tf2_core 静态库。add_executable 名去重 54 与共享、共享。
- (target, TU) 对总数 **134**，来自构建树 native/build-nmake/CMakeFiles/*.dir/ 的 TU 列表（57 个 .dir 目录，55 个有 TU 列表）。134 对里有 1 个 TU 被两个目标复用，故唯一 TU = 133。
- **实测 92 对，未测 42 对。** 未测的是 15 个目标：tf2_demo_native（28 TU）与 viewmodel_animation_probe / viewmodel_contract_probe / viewmodel_fov_probe / viewmodel_material_probe / viewmodel_range_probe / vpk_integrity_probe / vpk_query / vpk_readiness_probe / weapon_animation_probe / weapon_body_probe / winmm_sink_probe / world_feature_probe / world_geometry_probe / world_material_probe（各 1 TU）。
- 已测 92 对里：**91 对编译成功，1 对失败**（见 §5，是我的复现 harness 的假失败，不是产品缺陷）。
- 告警数只统计**编译成功**的那 91 对：**19 个 TU 有告警，共 31 条。**
- 另有 26 条告警出现在 9 个 TU 的**被中断**编译日志里（前端已出诊断、代码生成阶段因 /Fo 路径错误死掉）。这 26 条是真实诊断，但那些编译没跑完，**只能当下界，不能当总数**。
- lead 转述的「28 个 TU / 57 条」= 19 + 9 个 TU、31 + 26 条告警。这两个数混合了「跑完的」和「被打断的」，报告里必须分开写：**31 条来自跑完的编译，26 条来自被打断的编译**。

## 2. /W4 覆盖矩阵（这是本报告的主结论）

| 目标 | mode | /W4 | TU 数 | 实测成功 | 告警(跑完) |
|---|---|---|---|---|---|
| tf2_core | W4 | 是 | 51 | 50（1 假失败） | 5 |
| tf2_demo_native | W4 | 是 | 28 | 0 | — 未测 |
| minimap_probe | W4 | 是 | 2 | 2 | 0 |
| entity_message_fixture_probe | W4 | 是 | 1 | 1 | 0 |
| entity_protocol_probe | W4 | 是 | 1 | 1 | 0 |
| hud_probe | W4 | 是 | 1 | 1 | 0 |
| review_workspace_probe | W4 | 是 | 1 | 1 | 0 |
| **小计 7 个目标** | | | **85** | 56 | 5 |
| 其余 48 个目标（48 个 add_executable，install_cli_probe 有 2 TU） | INJECT | 否 | 49 | 35 | 26 |

覆盖率三种算法，都要一起看：
- 按目标：7/55 = **12.7%**（只看可执行文件是 6/54 = 11.1%，与 docs/N23-W4-COVERAGE-2026-11-14.md 一致）。
- 按 TU：85/134 = **63.4%** —— 这个数看起来高，是因为 tf2_core 和 tf2_demo_native 这两个大目标把 TU 数撑起来了。
- 按源码行：未测量（要统计每个 TU 的行数，本次没做）。

结论一句话：**宽的那个（产品 + 核心库）是覆盖的，长尾 48 个 probe 是不覆盖的**。所以「63% 覆盖」的说法会误导人，真正的问题是长尾零覆盖。

## 3. 48 个目标为什么完全一致（lead 点名要写清的这条）

**不是全局 CMAKE_CXX_FLAGS 的问题，恰恰相反：全树根本没有全局 /W4 —— 这是逐 target 手工白名单，48 个目标一起落进了 MSVC 默认 /W1。**

证据（native/cmake 全目录只有这 7 条 target_compile_options 带 /W4）：

- native/cmake/tf2-libraries.cmake:111 — `target_compile_options(tf2_core PRIVATE /W4 /permissive- /utf-8 /EHsc)`
- native/cmake/tf2-package.cmake:88 — tf2_demo_native（/W4 /permissive- /utf-8）
- native/cmake/tf2-package.cmake:89 — entity_protocol_probe
- native/cmake/tf2-package.cmake:90 — entity_message_fixture_probe
- native/cmake/tf2-package.cmake:93 — hud_probe（:91 注释：T51，probe 是证据，/W3 建出来的证据不是同一个证据）
- native/cmake/tf2-package.cmake:95 — minimap_probe（:94 注释：T69，同 hud_probe 的理由）
- native/cmake/tf2-package.cmake:97 — review_workspace_probe（:96 注释：T71/T72）

而全局只有一条 add_compile_options：native/cmake/tf2-settings.cmake:31 — `add_compile_options(/Brepro)`（可复现字节，T90）。**没有 /W4，也没有 /W3。**

所以机制是「按 feature 步骤逐个 opt-in」，注释里的理由都是「这个 probe 的证据要被当证据读，所以要跟产品同一个告警水位」。没有一条是「新目标默认就该带 /W4」。后果：

1. 新加一个 target 只要不自己写 target_compile_options，就静默落 /W1；
2. 没有任何检查会发现这件事（本报告就是靠解析 flags.make 才发现 48 个目标完全一致的）；
3. 48 个目标的 CXX_FLAGS 实测**完全相同**：`/DWIN32 /D_WINDOWS /EHsc /O2 /Ob2 /DNDEBUG -std:c++17 -MT /Brepro` —— 既无 /W3、无 /permissive-、无 /utf-8，正是 MSVC 默认 /W1 的形状。

另外注意 native/cmake/tf2-probes-protocol.cmake:17 有一条注释说某 probe「对齐产品 flags（/W4 /permissive-）所以头文件改动会…」—— 那只是注释，不是机制；该目标的 flags.make 实测不带 /W4。**注释不能当覆盖证据，这是本报告第 2 个方法学要点。**

## 4. 告警分类（跑完的 91 对，31 条）

代码分布：C4456×15、C4996×7、C4189×3、C4267×2、C4310×1、C4100×1、C4307×1、C4127×1。

### 4.1 真缺陷（3 条）

- **C4189 native/src/install_manifest.cpp:164** `DWORD objectSize = 0, dataSize = 0, produced = 0;` —— `dataSize` 全文件只出现这一次（ops_grep 单点命中），是纯死变量。无害但确定是缺陷。注：PROGRESS.txt:57 记录过该文件 C4189（当时报 :51），说明这处已经红过一次没被清。
- **C4267 native/tools/camera_probe.cpp:202** `readings.set("bookmarksSaved", store.savedCount())` —— camera_bookmarks.h:73 `std::size_t savedCount() const;` 是 size_t，读数字段按 uint32 存，窄化。此例实际值 ≤3，真实风险为零，但类型是对的、水位是错的。
- **C4267 native/tools/terrain_blend_probe.cpp:96** `put32(12, headerSize)` —— headerSize 是 `const std::size_t headerSize = depth > 0 ? 65u : 64u;`（:85）。值有界，同样是有意但类型不对。

### 4.2 刻意为之、不许「修」（2 条）

- **C4127 native/src/spectate_target.cpp:309** `if (kHandoverSeconds <= 0.0) return 1.0;`，常量定义在 native/include/spectate_target.h:80 `static constexpr double kHandoverSeconds = 0.3;`。**这是有记录的取舍**：PROGRESS.txt:56 原话「kHandoverSeconds > 0.0 raises warning C4127 on purpose so the m1 mutant (0.0) cannot divide by zero and its red names『the handovers disappeared』instead of『it crashed』」。把这条消掉就等于废掉 mutation m1 的命名能力。**/WX 上这条之前必须先给这条加 targeted disable，否则会把一个正确的设计判红。**
- **C4307 native/tools/sound_timeline_census_probe.cpp:93** `static_cast<std::int32_t>(100 - intMin)` —— 该 self-test 就是要观察「INT_MIN origin 把正 tick 翻负」，:89-91 注释写明这是「the overflow road to the same collapse」。字面上它确实是 int 溢出 UB，建议改成无符号算术表达，但它的存在本身是对的。

### 4.3 移植性假警报（7 条 C4996）

全部是 MSVC CRT 的 `_CRT_SECURE_NO_WARNINGS` 家族（getenv×5、fopen、sscanf）：animation_viewmodel_probe.cpp:401、audio_effects_probe.cpp:762、audio_scheduler_probe.cpp:297、playback_control_probe.cpp:279、skeleton_skin_probe.cpp:183、sound_content_map_probe.cpp:117、terrain_blend_probe.cpp:133。

**这不是代码缺陷，是平台方言**：建议的 `_dupenv_s`/`fopen_s` 是 Windows 专有 API，照改会把可移植代码绑死在 MSVC 上。正确处理是在探针目标上加 `_CRT_SECURE_NO_WARNINGS` 或按文件 targeted disable，不是在源码里换 API。

### 4.4 卫生问题（16 条，零行为影响）

- C4456 ×15（变量遮蔽：demo_open_probe.cpp:888/892/902/906/939/949 的 i、entity_model_probe.cpp:376/617 的 state、:640 的 transform、:2076/2084 的 entry、material_chain_probe.cpp:296 的 error、model_loader.cpp:536 的 i、camera_bookmarks.cpp:134/191 的 index）。
- C4100 ×1（projectile_track_probe.cpp:945 未引用的形参 tfRoot）。
- C4189 ×2（entity_model_probe.cpp:2043 runsBuilt、model_pose_probe.cpp:1064 hasAttacker）—— 编译器判定为死，我没有逐行读这两处，属「已判定、未人工确认」。

### 4.5 产品侧（tf2_core）只有 5 条

model_loader.cpp:536 C4456、spectate_target.cpp:309 C4127、camera_bookmarks.cpp:134/191 C4456、install_manifest.cpp:164 C4189。**其中 1 条是刻意（C4127）、1 条是真死变量（C4189）、3 条是遮蔽。产品核心库在 /W4 下是干净的** —— 这与「长尾 probe 一堆告警」形成对照：真正该补 /W4 的不是产品，是那 48 个 probe。

## 5. 那 1 个失败：我的 harness 的假失败

`092_tf2_core__install_lifecycle_cpp` rc=2，报：

```
native/src/install_lifecycle.cpp(750): error C2017: 非法的转义序列
native/src/install_lifecycle.cpp(750): error C2001: 常量中有换行符
native/src/install_lifecycle.cpp(751): error C2146: 语法错误: 缺少";"
```

原因：flags.make 的 CXX_DEFINES 是 `-DTF2_PACKAGE_VERSION=\"0.1.0\"`，这份反斜杠转义是给 NMake 响应文件用的；我把它拼进自己的命令行时被二次解释，变成裸双引号，于是 :750 `info.version = TF2_PACKAGE_VERSION;` 展开成未闭合字符串。**真实构建里这个文件编译通过**（构建树 rc=0）。这是「复现 harness 的转义层与构建系统的转义层不是同一层」的教训，不是产品缺陷。

## 6. 方法学：这条路是怎么量出来的（含三个坑）

不改 CMake 目标图，从构建树复现每个 (target,TU) 的编译行：解析 native/build-nmake/CMakeFiles/<target>.dir/build.make 拿 TU 列表，解析同目录 flags.make 拿 defines/includes/flags，然后对**非 /W4 的 48 个目标**追加 /W4 单 TU 编译到 .scratch/w4probe。产物全在 .scratch（.gitignore:35），不碰 evidence/、不碰 native/tests/fixtures。

三个坑（都已修复，重跑前必须知道）：

1. **shell 变量名不能叫 CL** —— cl.exe 会读 %CL% 当附加命令行选项，于是 `/c/Program Files/...` 被当成选项，日志出现 `cl: 命令行 warning D9002 :忽略未知选项"/c/Program"` 与 `c1xx: fatal error C1083: 无法打开源文件: "Files/Microsoft"`。改名 CLPATH 即可。
2. **/Fo 不能用 MSYS 风格路径** —— 会被拼成 `D:/d/TF2_Native_Worktrees/...`（盘符重复），报 `fatal error C1083: 无法打开编译器生成的文件`。必须给 Windows 字面量路径。
3. **长编译必须后台跑** —— 塞在同步调用里会被 120s 超时掐断，而 hostlock 里被掐的子进程仍挂在持锁人名下，会变成 stale lock 挡住别人（本轮真的挡住了 n12-camera）。

三个坑合起来解释了本报告 §1 的分裂口径：前两轮 134/134 全失败的结果必须整体作废，不能用来数告警。

## 7. 建议（按性价比排序，均未实施）

1. **先修机制，再修告警**：在 CMake 加一条「新目标默认警告水位」的规则（例如在 tf2-settings.cmake 里给所有 probe 统一 /W4，产品/库保持现状），否则清完这一批，下一个新 probe 又落回 /W1。**这是唯一能真正把 12.7% 变成 100% 的动作。**
2. **按 cmake 文件成批改，每批只动 target_compile_options，不动源码**（tf2-probes-world.cmake 6 个、tf2-probes-audio.cmake 6 个、tf2-probes-core.cmake 6 个…），每批独立验证。
3. **/WX 先别上**：先把 4.2 的两条刻意告警按文件 disable，再按目标逐个 /WX（tf2_core 最接近可上，只剩 1 条死变量）。
4. **C4996 用宏关掉而不是换 API**（保可移植性）。
5. 补测剩余 42 对（尤其 tf2_demo_native 28 个 TU —— 产品主 TU 的 /W4 读数目前仍是空白）。

## 8. 这份文档不能证明什么

- **不能证明 134 对都测过**：只测了 92 对，42 对没测。未测里包含 tf2_demo_native 的全部 28 个 TU，也就是**产品主目标一条实测告警都没有**——那 26 条来自被打断编译的日志不能替代它。
- 不能证明 48 个目标「抬到 /W4 会报多少告警」：本轮只测了它们**已经**在 /W4 下的表现（而不是它们的默认 /W1 行为），48 个目标仍有 22 个没测到（15 个目标 / 42 对里有 49-35=14 对 INJECT 未跑）。
- 不能证明「31 条」是完整集合：单 TU 编译只出该 TU 的告警；模板在多个 TU 里实例化会重复报，我没有跨 TU 去重，所以真实唯一问题数 ≤31。
- 不能证明告警与**当前**源码行号一致：日志来自本轮实测（.scratch/w4probe/log，t105-e5 现场），但 native/src/main.cpp 在实测期间正被 n12-camera 修改（实测 mtime 2026-11-14 18:39:35，sha256 `045f5e3de79ae964b5eefb04f260513b62d75cdcde416697a958aa10f35ff9c4`，789,666 B），main.cpp 相关行号会漂。tf2_core 与 probe 的行号不受影响。
- **不能拿字节数当身份**：本轮实测里 `native/build-nmake/tf2_demo_native.exe` 两次构建出来**同为 2,991,104 B 而 sha256 不同**（旧 `423ed9a8…` → 现 `b4a2355e…`）。字节数相同不能证明是同一个二进制；凡是靠「大小对上了」核对 exe 的地方都会漏掉这种替换，必须比 sha256。
- 不能证明 CI 上有任何覆盖：本报告完全是本地测量，仓库当前**没有** CI（见 N22）。
- 4.4 里两条 C4189 我没有逐行读源码确认是死代码，只采信编译器判定。

## 9. 证据

| 文件 | 内容 | sha256 |
|---|---|---|
| .scratch/w4-matrix.json | 134 个 (target,TU) 的解析结果（target/mode/src/opts） | 7e0d650e94f27059d620b77579ca5cd1e0e7dddb72d6b3a15504af1aa48c698d |
| .scratch/w4probe/manifest2.tsv | 驱动清单 134 行（name/target/mode/src/opts） | f30c7af880c5f57659a38a3b3f7a417331911ec21cbad3323a1b1a52912174b3 |
| .scratch/w4probe/rc.txt | 逐对退出码 92 行 | 988e574db4190dfd8e550678a4aa05f360b01abb8ef2a0b0702cf5c256837f02 |
| .scratch/w4probe/log/*.log | 逐对编译器日志（134 个） | 见上三件的口径 |
| .scratch/w4probe/obj | 92 个 .obj（成功编译的产物） | — |
| evidence/cmake-build.log | 真实全量构建日志（21 条告警，2026-11-13 09:50Z，比当前源码旧） | f24e99d61e854dbd7e2c7ad8fd63a1add7d923524561055cf95e25610de31f54 |
| native/build-nmake/tf2_demo_native.exe | 当前产品 exe（2,991,104 B, mtime 2026-11-14 18:47:41） | b4a2355ecc0a9fe83978f77347b5b9a619b10a8b3a96746daefcddad8891fbdd |

辅助证据（可交叉验证，但行号已漂）：evidence/cmake-build.log 的 21 条告警覆盖 C4456×8 / C4244×6 / C4100×2 / C4267×2 / C4127×1 / C4189×1 / C4457×1，与本报告 tf2_core 段一致（model_loader.cpp:536、spectate_target.cpp:309、camera_bookmarks.cpp:134/191、install_manifest.cpp 的 C4189 全在其中）。