# 状态报告 — GitHub 同步 / 环境 / 难题 / 架构（2026-11-19）

写给"现在的进度到哪了、还剩什么卡着"。所有数字都是 2026-11-19 在本机（t105-integrate，
HEAD `2aa812dc`）实测或从 GitHub API 读到的，不引用旧报告当现状。

## 1. GitHub 与当前进度一致吗

**分支内容一致，CI 零运行，大部分工作分支没推。**

| 项 | 读数 |
|---|---|
| `origin/work/integrate` | `2aa812dc441978d742632f4f9add67a18bb7669c` = 本地 HEAD |
| GitHub 分支 | 仅 4 个：`main` `98ddaeba`、`p0-entity-protocol` `6f475caf`、`work/integrate` `2aa812dc`、`archive/full-history-2026-11-13` `50a355a0` |
| GitHub Actions | `actions/runs` 的 `total_count = 0` —— **一次都没有跑过** |
| `.github/workflows/tier1.yml` | 只在 `work/integrate` 上有；触发条件 `main`、`candidate/*`、PR、手动 dispatch，**不含 `work/*`**；远端没有 `candidate/*` 分支 |
| 本地未推送 | 33 个分支带内容，干净谱系 **82 个唯一提交**未推（`git rev-list --all --not --remotes=origin` 过滤干净根 `7ab4e725`） |
| patch 等价 | `task/weapon-animation` 的 7 个提交 `git cherry` 全为 `-`（内容已在远端，只是 SHA 不同） |
| 断链谱系 | `integration/animation-diagnostics`（142 个提交）、`integration/entity-material`（141 个），根 `087926a4` 与干净根 `7ab4e725` 不连通，**不可合并、不可当工作推** |

结论：远端 `work/integrate` 与本地完全一致（含文档、门禁修复、`/analyze` 结论）；
"进度"层面不一致的部分是 —— 33 个分支的 82 个提交只在本地，CI 从未验证过任何一次提交。

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
- N10 3D skybox 未实现、`sky_camera` 路径 UNSUPPORTED。
- N11 七图矩阵 14 格只有 2 格合格；证据跨 4 个 exe，没有同一 exe 覆盖七图。
- N13 没有 broadcaster/follow/选人命令（缺模型层命令，不是缺按钮）。
- N17/N18 viewmodel 动画仍是 bind pose、POV 帧无 viewmodel、LOD1+ 不支持。
- N20 三个指定效果未实现；N21 听感需人在场；N27 导出只有契约、产品未实现；N28–N30 发布未冻结、干净环境走查与 `verify-release.sh` 未跑。

**B. 工程瓶颈（本仓库自身）**
- CI 零运行（见 §1）；55 个门禁在本轮只跑了身份门禁 + 3 个烟测 + demo-clock，其余没有真实 verdict。
  2026-11-19 追加：animation-drive-states 在 `work/integrate`（HEAD `7542bf5e`）重跑得到 `ANIMATION-DRIVE-STATES=PASS`（9 次运行全 rc=0、3848 s、四条内置负控 4/4 带原因串验证），见 `docs/GATE-VERDICTS-ROUND2-2026-11-15.md` 末尾。
- `/analyze` 不在门禁；4 条 C6262（57–131 KB 栈帧）只记录未取舍。
- 33 分支 82 提交未推；两条断链谱系占 283 个原始提交，属清理前历史。
- `.gitattributes` 行尾规则已定策略，仍等安全窗口落地。

**C. 工具与网络**
- MSYS 只转换命令行参数、不转换环境变量值（门禁侧已用 `cygpath -w` 修）。
- `gh` 未登录；本机 `HTTPS_PROXY=127.0.0.1:7897` 无服务，curl 必须 `env -u HTTPS_PROXY -u HTTP_PROXY` 才通（GitHub API 直连可用）。
- `demo_open_probe` 的 `--snapshot`/options 在 demo 参数前的用法与 usage 行不符（报 `demo file cannot be opened`，demo 在前则正常）——观察，未深究。

## 4. 架构优化现状

- `native/src/main.cpp` 现在 **16,221 行**（N24 phase 1 于 2026-11-19 抽出 renderer self-test，`88ade848`）；
  `int WINAPI wWinMain` 仍在 `native/src/main.cpp:3163` 开、`:16221` 闭，**13,251 行一个函数**未变
  （N24 计划时 15,324 行文件 / 12,392 行函数，之后长了 1,089/859 行）。
- 已有的拆分只搬周边：`main_hud.cpp` 1,608、`native_ui.cpp` 730、`main_helpers.cpp` 198、
  `camera_script.cpp` 633、`main_ui.cpp` 347、`main_internal.h` 180、`main_camera.cpp` 139、
  `main_lifecycle.cpp` 116、`main_trace.cpp` 104。
- `docs/N24-MAIN-SPLIT-PLAN-2026-11-16.md` §7 **phase 1 已落地**（`88ade848`）：新 TU 213 行、`main.cpp` −192 行、
  `main_internal.h` 声明、`tf2-product.cmake` 条目；`wWinMain` 未动。§4 其余阶段仍是方案。
  phase 1 顺带暴露一个构建陷阱：编辑工具保留 mtime → NMake 既不重编也不重跑 CMake，链接了拆分前 obj，
  身份门禁的 compiled-input 探针以 `LNK2019` 抓到；`touch native/CMakeLists.txt` 重配后才转绿。
- 结构化约束（计划里记录、仍成立）：wWinMain 无 `goto`/清理标签、183 个开关一条 1354 行比较链、
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
4. 其余 33 个分支是否推 GitHub 属用户决策；断链谱系只留历史。

## 6. 证据路径

- 门禁：`/d/_shared/n23-identity-forward4`（`BUILD-IDENTITY=PASS assertions=222`）
- `/analyze`：`/d/_shared/n23-analyze/<target>/`、`/d/_shared/n23-analyze-final.log`
- ASan 构建：`/d/_shared/n23-asan-build/`、脚本 `/d/_shared/n23-asan-build.sh`、日志 `/d/_shared/n23-asan-build.log`
- sanitizer 读数：`/d/_shared/n23-asan-selftest.log`、`/d/_shared/n23-asan-demo-scan.log`
- 环境自证：`/d/_shared/asan-smoke.cpp`、`smoke-run.log`
- T84 门禁重跑：`/d/_shared/n25-drive/`（9 次捕获各 2,582,406 B bmp + ~200 MB trace）、日志 `/d/_shared/n25-drive-rerun.log`
