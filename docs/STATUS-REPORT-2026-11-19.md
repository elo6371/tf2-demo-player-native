# 状态报告 — GitHub 同步 / 环境 / 难题 / 架构（2026-11-19）

写给"现在的进度到哪了、还剩什么卡着"。所有数字都是 2026-11-19 在本机（t105-integrate，
HEAD `89733ac4`）实测或从 GitHub API 读到的，不引用旧报告当现状。

## 1. GitHub 与当前进度一致吗

**分支内容一致；CI 已实跑（tier 1 首次全绿）；大部分工作分支按用户裁决不推。**

| 项 | 读数 |
|---|---|
| `origin/work/integrate` | `89733ac4ed8e2ee650fb30d75841b5c592e0a5ab` = 本地 HEAD（2026-11-19 16:05 实测） |
| GitHub 分支 | 5 个：`main` `98ddaeba`、`p0-entity-protocol` `6f475caf`、`work/integrate` `89733ac4`、`candidate/ci-smoke` `4f1a52d7`（临时 CI 验证分支）、`archive/full-history-2026-11-13` `50a355a0` |
| GitHub Actions | 3 次 tier-1 run：`44462555445` 红（LF 守卫）、`44476744537` 红（两条新门禁未登记）、`44479356155` **绿**（`4f1a52d7`，3 m 14 s） |
| `.github/workflows/tier1.yml` | 在 `work/integrate` 与 `candidate/ci-smoke` 上；触发 `main`、`candidate/*`、PR、手动 dispatch，**不含 `work/*`** |
| 本地未推送 | **161 个本地分支**；**234 个提交**不在任何 origin ref 里（`git rev-list --all --not --remotes=origin`）；origin 只有 5 个分支 |
| worktree | 20 个登记，3 个有未提交改动（`D:/TF2_Native_Test` 25、`t105-n17anim` 8、`t105-overview` 4），按用户裁决保留现场 |
| patch 等价 | `task/weapon-animation` 的 7 个提交 `git cherry` 全为 `-`（内容已在远端，只是 SHA 不同） |
| 断链谱系 | `integration/animation-diagnostics`（142 个提交）、`integration/entity-material`（141 个），根 `087926a4` 与干净根 `7ab4e725` 不连通，**不可合并、不可当工作推** |

结论：远端 `work/integrate` 与本地完全一致到 `89733ac4`（含文档、门禁修复、`/analyze` 结论）；
"进度"层面不一致的部分是 —— 161 个本地分支 / 234 个未推提交按用户 2026-11-19 裁决不整体推；CI 只验证过 `candidate/ci-smoke` 上的 tier 1（run #3 绿）。

## 2. 环境：x64 ASan 已安装（本轮）

- 安装：`vs_installer.exe modify --installPath "C:\Program Files\Microsoft Visual Studio\2022\Community"
  --add Microsoft.VisualStudio.Component.VC.ASAN --quiet --norestart` → exit 0，09:12:34 落地：
  `lib/x64/clang_rt.asan_dynamic-x86_64.lib`、`clang_rt.asan_dynamic_runtime_thunk-x86_64.lib`、
  `clang_rt.asan_static_runtime_thunk-x86_64.lib`、`clang_rt.asan_dbg_dynamic-x86_64.lib`、
  `bin/Hostx64/x64/clang_rt.asan_dynamic-x86_64.dll`。
- 自证：`/d/_shared/asan-smoke.cpp`（`a[i+1]`，i=2）用 `cl /EHsc /Zi /MD /fsanitize=address` 编，
  运行报 `AddressSanitizer: stack-buffer-overflow` / `READ of size 4` / `asan-smoke.cpp:5`，exit 1。
  之前同一命令报 `LNK1104: clang_rt.asan_static_runtime_thunk-x86_64.lib`，环境确实换了。
- 项目 ASan 构建：`/d/_shared/n23-asan-build.sh` → `cmake -S native -B /d/_shared/n23-asan-build
  -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_FLAGS="/fsanitize=address /Zi"`；
  产物 `tf2_core.lib`、`install_cli_probe.exe`、`demo_open_probe.exe`、`model_robustness_probe.exe`、
  `material_chain_probe.exe`、`brush_entity_probe.exe`（1.7–3.1 MB）。sidecar 自报
  `sourceTree=2aa812dc4419 dirty=0`，说明这是当前 HEAD 的树。

### 第一组 sanitizer 读数（不是"全树干净"）

| 目标 | 命令 | 读数 |
|---|---|---|
| install CLI | `install_cli_probe.exe --install-safety-selftest` | `install-safety-selftest=PASS cases=4 checks=21 fail=0`，exit 0，**0 条 ASan 报告** |
| demo 解析 | `demo_open_probe.exe --scan testdemo111.dem`（koth_bagel_rc11，POV） | commands=51491 packets=25130 ticks=25514 frames=25126，`malformed_packets=0`，exit 0，**0 条 ASan 报告**（日志 3197 B） |

边界：只覆盖 install 命令行与 POV demo 解码两条路径；运行需把
`.../VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64` 加进 `PATH`（ASan DLL 在那），本次统一
`ASAN_OPTIONS=detect_leaks=0`。本机 TF 安装 `tf/` 下没有 `models` 目录，model/material 系探针没有语料，
资源生命周期路径**没有读数**。ASan 构建不在任何门禁里，也不代表 55 个门禁带 sanitizer 跑过。

## 3. 卡住/未解决的问题

**A. 产品与证据缺口（不是工程问题）**
- N05 849/919 像素缺陷：现象已确证，机制未归因，D01 仍 OPEN。
- N10 3D skybox 渲染 pass 未实现；`sky_camera` 实体**已解析**（`native/src/bsp_map.cpp:628-640` 写 `map.skyCamera`），缺的是缩放第二遍与深度清除。
- N11 七图矩阵 14 格只有 2 格合格；证据跨 4 个 exe，没有同一 exe 覆盖七图。
- N13 没有 broadcaster/follow/选人命令（缺模型层命令，不是缺按钮）。
- N17/N18 viewmodel 动画仍是 bind pose、POV 帧无 viewmodel、LOD1+ 不支持。
- N20 三个指定效果未实现；N21 听感需人在场；N27 导出只有契约、产品未实现；N28–N30 发布未冻结、干净环境走查与 `verify-release.sh` 未跑。

**B. 工程瓶颈（本仓库自身）**
- CI 只跑 tier 1（run #3 `44479356155` 绿，3 m 14 s）；verify-all 与 verify-release 未进 CI，55 个门禁其余没有真实 verdict。
  2026-11-19 追加：animation-drive-states 在 `work/integrate`（HEAD `7542bf5e`）重跑得到 `ANIMATION-DRIVE-STATES=PASS`（9 次运行全 rc=0、3848 s、四条内置负控 4/4 带原因串验证），见 `docs/GATE-VERDICTS-ROUND2-2026-11-15.md` 末尾。
  2026-11-19 14:48 成本已压：同一门禁加 `--jobs 4`（九个捕获分波并行，默认仍是 1）后 3,848 → 1,713 s（捕获 839 s），`READINGS=PASS`、负控 4/4、九帧 md5 与串行逐字节相同，读数 `evidence/t84-animdrive/parallel-jobs4/`。
- `/analyze` 不在门禁；4 条 C6262 里三处（`native_ui.cpp:70/76`、`settings_store.cpp:132`、`main.cpp:2287`）2026-11-19 已搬堆，第 4 条 `wWinMain`（58,208 B）留待 N24 拆分；`/analyze` 三 TU 已在 2026-11-19 复测（`native_ui` warnings=0、`settings_store` warnings=0、`main.cpp` 只剩 `wWinMain` 的 C6262/C28251，全树未重扫）。
- 161 个本地分支 / 234 个未推提交（按用户 2026-11-19 裁决不整体推）；两条断链谱系占 283 个原始提交，属清理前历史。
- `.gitattributes` 行尾规则已定策略，仍等安全窗口落地。

**C. 工具与网络**
- MSYS 只转换命令行参数、不转换环境变量值（门禁侧已用 `cygpath -w` 修）。
- `gh` 未登录；本机 `HTTPS_PROXY=127.0.0.1:7897` 无服务，curl 必须 `env -u HTTPS_PROXY -u HTTP_PROXY` 才通（GitHub API 直连可用）。
- `demo_open_probe` 的 `--snapshot`/options 在 demo 参数前的用法与 usage 行不符（报 `demo file cannot be opened`，demo 在前则正常）——观察，未深究。

## 4. 架构优化现状

- `native/src/main.cpp` 现在 **16,221 行**（N24 phase 1 于 2026-11-19 抽出 renderer self-test，`88ade848`）；
  `int WINAPI wWinMain` 现在 `native/src/main.cpp:2971` 开、`:16221` 闭（N24 phase 1 抽出
  `main_selftest.cpp` 之后起点前移；`:3163` 已在 CLI 声明区内），**13,251 行一个函数**未变
  （N24 计划时 15,324 行文件 / 12,392 行函数，之后长了 1,089/859 行）。
- 已有的拆分只搬周边：`main_hud.cpp` 1,608、`native_ui.cpp` 730、`main_helpers.cpp` 198、
  `camera_script.cpp` 633、`main_ui.cpp` 347、`main_internal.h` 180、`main_camera.cpp` 139、
  `main_lifecycle.cpp` 116、`main_trace.cpp` 104。
- `docs/N24-MAIN-SPLIT-PLAN-2026-11-16.md` §7 **phase 1 已落地**（`88ade848`）：新 TU 213 行、`main.cpp` −192 行、
  `main_internal.h` 声明、`tf2-product.cmake` 条目；`wWinMain` 未动。§4 其余阶段仍是方案。
  phase 1 顺带暴露一个构建陷阱：编辑工具保留 mtime → NMake 既不重编也不重跑 CMake，链接了拆分前 obj，
  身份门禁的 compiled-input 探针以 `LNK2019` 抓到；`touch native/CMakeLists.txt` 重配后才转绿。
- 结构化约束（计划里记录、仍成立）：wWinMain 无 `goto`/清理标签、191 条 `L"--"` 比较（`wcscmp` 全部 212 条、`L"--"` 字面量 200 个）一条长比较链、
  MSVC C1061 嵌套上限已撞过一次、306 处 `traceFmt` 属可观测契约、54 个按引用 lambda。
- `tf2_core` 51 个编译单元、`tf2_demo_native` 30 个（`/analyze` 覆盖口径）；N22 的 `tier1.yml`
  文件已落地但零运行；N26 构建缓存实测未做。
- 判断：架构优化刚起步——phase 1（最低风险、可门禁验证）已落地并有五项读数，`wWinMain` 仍未动。
  下一步是 phase 2 的 seam（renderer/HUD 调用面），而不是继续往 `main.cpp` 里加东西。

## 5. 本轮之后的第一步（建议顺序）

1. 把本报告的 sanitizer 结论写回 `docs/N23-WALL-LEVEL-2026-11-18.md` §8 与状态行（本轮已做）。
2. ~~N24 phase 1（renderer self-test 抽出 `main.cpp`）~~ 已于 2026-11-19 完成（`88ade848`，读数见 §4）；
   下一步 phase 2。
3. N25 剩下的门禁补真实 verdict（身份门禁已 PASS；animation-drive-states 2026-11-19 也 PASS 且负控 4/4；其余仍是"修了没跑"）。
4. 161 个本地分支 / 234 个未推提交按用户 2026-11-19 裁决不整体推，只推验收后的任务分支或修复提交；20 个 worktree 不批量合并/删除/归档，3 个脏现场保留（`D:/TF2_Native_Test` 25、`t105-n17anim` 8、`t105-overview` 4）；断链谱系只留历史。
5. 本批（2026-11-19 下午）：P9 三处栈转堆已落地、外部评审 triage 已写（`docs/GEMINI-REVIEW-TRIAGE-2026-11-19.md`）；
   排期与未解决项见 `docs/TASK-LIST-2026-11-19.md`；身份门禁已在 P9 树复跑 PASS（`head=dbe8abcf`，
   2026-11-19 12:02），下一步是 `/analyze` 全树复扫。
6. 本批追加：N11 七图矩阵已用**同一个 exe** 跑完 14/14 格并 PASS
   （`N11-SEVEN-MAP-MATRIX=PASS rows=14/14 identical_pairs=0`，13:32，约 22 分钟；
   读数 `evidence/n11-matrix/readings.tsv`，记录 `docs/N11-SEVEN-MAP-MATRIX-2026-11-15.md` §9）。
   两条待查读数：cp_sunshine POV 的 eye=0,0,-359 且 PVS 未剔除；全部 14 格 tiles=0。
7. 本批再追加：D01 的 COLOR0 取证完成（`docs/D01-COLOUR0-READBACK-2026-11-19.md`）。
   COLOR0 今天**不缺失**（849 读 70、919 读 255），同一 recipe 的产品帧在 849 读到 70,70,70
   而不是归档帧的 0,0,0；`albedo×lighting` 与 lightmap 探针同值，没有零化步骤。
   结论：D01 的「顶点色缺失」对今天的树不成立，要关它需要一个绑定提交的复现器。
8. 本批收尾：`/analyze` 全树重扫完成（55 target / 136 TU / **19 条警告** / 分析失败 0，约 10 分钟），
   读数 `evidence/analyze/2026-11-19-summary.tsv`、记录 `docs/ANALYZE-FULL-TREE-2026-11-19.md`。
   产品 exe 只剩 5 条，其中 `wWinMain` 的 C6262 58,208 B 要等 N24 phase 2 拆函数才会消失。

9. 本批 G6：文档-源码一致性第二遍已应用并推送（`d68c5286`，3 份台账 16 行 + 分配表 G6 标完成）。
   改掉的旧事实：`WARN-GATE tus=138`→**139**、`main.cpp` 12,883→**16,221**（`wWinMain` 起于 `:2971`）、
   N22「未推远端」→ `.github/workflows/tier1.yml` 已推但零运行（触发不含 `work/*`）；
   N18 的 `viewModelRequested` 引用改到 `main.cpp:8601/8985/10155/11833/14704`，`sky_camera` 指向 `native/src/bsp_map.cpp:629-638`。

10. 本批 L6：CI 首跑两连红已定位修齐 —— run #1 是 LF 守卫把 `evidence/**` 下 44 个未钉脚本当 CRLF（`d3bb5d05` 修）；
    run #2 的 tier 1 红在 3/3 的门禁登记（`GATE-REGISTRY=FAIL gates=88 registered=35 excluded=51`：新门禁
    `argument-refusal-check.sh`（`97ad8917`）与 `world-entity-tint-check.sh`（`adce1061`）既不在链里也不在排除表），
    两条已按 segmented 登记进 `docs/GATES-UNREGISTERED.md`；本地 `verify-fast.sh --mutation` 复跑
    `GATE-REGISTRY=PASS excluded=53`、`VERIFY-FAST=PASS (assertions_ok=43)`；run #3 `44479356155`（`4f1a52d7`）七个 step 全 success（3 m 14 s），
    首次 CI 绿，读数 `evidence/ci-run3-2026-11-19.md`。

11. 本批 N18 基线：`viewmodel-contract-check.sh` 在 `e5b33781` 上跑出 `assertions_ok=152` / `VIEWMODEL-CONTRACT=FAIL`，
    两条红与 2026-11-18 在 `979c6904`（N18 之前）和 `5bac7eaa`（N18 之后）上的读数逐项相同 —— capture point 0/7
    （trace `draw: begin tick=0`、`sceneTick=10320` vs premise `10252`）、identity 1 px LSB（`(658,416)`；
    其余 2,582,351 像素相同）。结论：门禁基线是「既有欠账」，不是 N18 回归；读数
    `evidence/n18-gate-baseline-2026-11-19/`，卡见 `docs/TASK-LIST-2026-11-19.md` §2 第 14 条。

## 6. 证据路径

- 门禁：`/d/_shared/p9-identity.log`（P9 树 `head=dbe8abcf`，`BUILD-IDENTITY=PASS assertions=222`，12:02）；
  早先 `/d/_shared/n23-identity-forward4`（`7542bf5e` 树，同样 222）
- `/analyze`：`/d/_shared/n23-analyze/<target>/`、`/d/_shared/n23-analyze-final.log`
- ASan 构建：`/d/_shared/n23-asan-build/`、脚本 `/d/_shared/n23-asan-build.sh`、日志 `/d/_shared/n23-asan-build.log`
- sanitizer 读数：`/d/_shared/n23-asan-selftest.log`、`/d/_shared/n23-asan-demo-scan.log`
- 环境自证：`/d/_shared/asan-smoke.cpp`、`smoke-run.log`
- T84 门禁重跑：`/d/_shared/n25-drive/`（9 次捕获各 2,582,406 B bmp + ~200 MB trace）、日志 `/d/_shared/n25-drive-rerun.log`
- T84 并行跑法（`--jobs 4`）：`/d/_shared/t84-jobs4/`（9 bmp + trace）、日志 `/d/_shared/t84-jobs4b.log`；入库读数 `evidence/t84-animdrive/parallel-jobs4/`
- N18 门禁基线（`e5b33781`，2026-11-19）：日志 `/d/_shared/n18-contract-baseline.log`；入库读数 `evidence/n18-gate-baseline-2026-11-19/`（README + 290 行日志）
- N11 七图矩阵：`evidence/n11-matrix/readings.tsv`（入库）、`.scratch/n11-matrix/`（BMP 与完整 trace，可再生）、日志 `/d/_shared/n11-matrix.log`
