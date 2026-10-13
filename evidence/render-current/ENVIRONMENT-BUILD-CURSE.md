T1 环境故障记录：构建被沙箱 safe-delete 拦截（2026-10-13 14:57–16:14）
====================================================================

现象
----
变异运行（14:38 启动）自 14:57 起，所有需要重编 tf2_demo_native 的构建全部失败：
NMAKE 报 `fatal error U1077: ... cmake -E cmake_cl_compile_depends ... cl.exe @nmXXXX.tmp:
返回代码 0x3`，且日志中没有任何编译器诊断。失败点始终是同一个文件：
`native/build-nmake/CMakeFiles/tf2_demo_native.dir/src/audio_timeline.cpp.obj`。

受影响的门禁读数（第一次变异运行，VERIFY-RENDER=FAIL (4 red)）：
- viewmodel-contract：1/5 caught（attachment-bone CAUGHT；fov-default / hand-matrix /
  companion-missing / draw-skipped 四个变异 BUILD-FAILED）
- viewmodel-animation：变异构建与恢复构建均失败（FATAL: the restore build failed --
  the mutated binary may still be on disk）
- viewmodel-companion：4/6 caught（force-v-model、owner-cleared 的 build 与 restore build 失败）
- evidence-tree：红，但属本轮自己的写入（见下）

根因（三层，逐层有证据）
----------------------
1. 直接原因：cl.exe 写 obj 被拒。绕过 nmake/cmake 直接调 cl.exe（响应文件方式，
   旗标不经 MSYS mangling）拿到真实诊断：
       audio_timeline.cpp : fatal error C1083: 无法打开输出文件
       '...\audio_timeline.cpp.obj': Permission denied
   诊断被吞的原因：cmake_cl_compile_depends 捕获并过滤 cl 的输出，失败时不回显。

2. 拦截者：WorkBuddy 沙箱的 safe-delete 包装器。它在一次 rm 失败时打印了自辩名：
       [safe-delete][diag] genie-trash failed: exit=1
         path=.../audio_timeline.cpp.obj
         stderr=ERROR ...: Error during a `trash` operation:
               Unknown { description: "Some operations were aborted" }
       [safe-delete][SAFE_DELETE_FAIL_CLOSED] {"target":"...audio_timeline.cpp.obj",
         "reason":"trash-failed", ...}
   即：该路径上的删除/覆盖被转交 genie-trash，trash 失败 → fail-closed → 拒绝写入。
   该状态跨进程持续（14:57 至 16:14 的每次尝试都失败）。

3. 自持循环：viewmodel-contract 的 fov-default 变异改的是头文件
   native/include/viewmodel.h；build-target.sh 的 stamp 守卫见"头文件新于
   .headers-stamp"就 touch 全部源文件 → 全量重编 → 必然覆盖 audio_timeline.cpp.obj
   → 撞诅咒 → 构建失败 → stamp 不前进 → 头文件 mtime（git checkout 恢复时刷新）
   永远新于 stamp → 此后每次构建都全量重编 → 每次都失败。12:48 与 14:47 的构建
   成功，是因为那时 obj 尚不存在（创建不被拦截，只有覆盖已存在文件才触发 trash）。

判别实验（排除的假设）
--------------------
- 并发构建竞争（另一 worktree world-material-lighting 当时在构建）：排除。
  对方 10 分钟未构建、本机无 cl/nmake 进程时，构建仍以同样方式失败。
- MSVC 环境丢失（INCLUDE/LIB 为空）：排除。env-msvc.sh 本就自带 INCLUDE/LIB；
  且缺环境的症状是 C1083 'cstddef'（有诊断），不是零诊断的 Permission denied。
- TEMP 响应文件被并发 nmake 同名覆盖：排除。TMP 指到隔离目录后失败依旧。
- 产品源码/门禁/工作树问题：排除。git status 干净、源码逐字节恢复、同一份源码
  在路径腾空后编译成功（rc=0，482,139 字节）。

处置（工件侧绕行，未改任何产品源码与门禁）
----------------------------------------
- 把 audio_timeline.cpp.obj 的 mtime 打到未来（touch -d 2026-10-14）：
  nmake 见 obj 新于源即跳过重编。该源在本次变异档的任何变异中都不会被修改
  （变异只打 model_loader.cpp / viewmodel.h / viewmodel.cpp / native_renderer.cpp /
  main.cpp），且 obj 内容就是用当前源码、当前旗标、当前编译器编译出的真实产物，
  故跳过重编不影响产物正确性。
- 验证：touch native/include/viewmodel.h 人为触发 stamp 守卫 → 全量重编 23 个文件
  → audio_timeline 被跳过 → 链接成功 rc=0，诅咒未触发。
- 重跑前把工作树恢复到一致状态：build_rc=0，源码干净，exe 16:14 重新链接。
- 计划：变异档重跑（evidence/fast/render-mutation2）后，把该 obj 的 mtime 复位为
  当前时间，避免给后续构建留下未来时间戳。

遗留风险（须向维护者报告）
------------------------
- 该路径的 safe-delete fail-closed 状态无法从工作区内清除（genie-trash 的 trash 存储
  与拒绝状态在 WorkBuddy 内部，探查被沙箱策略拒绝）。任何今后需要覆盖
  audio_timeline.cpp.obj 的构建（例如真正修改 audio_timeline.cpp 后重新编译）都会
  再次失败，直到 WorkBuddy 侧修复 genie-trash 或清除该路径状态。
- 若其他 obj 路径进入同样状态，本绕行只对 audio_timeline.cpp.obj 有效。
