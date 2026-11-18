# INST1 / E3b — 工具链入口落地 + /W4 告警门禁（2026-11-15）

作者：card-arch（task-1 实例1：CMake/构建目录/公共 runner/属性文件）
前置：R0→R1、E1、E2、E3a 已提交（HEAD 3e9ff50a 起）。本文是 E3b 的交付记录，E4 另有读数文档。

## 0. 结论（先看这四行）

1. **入口落地**：`env-msvc.sh` 变成薄壳 → `tools/tf2-toolchain.sh`；`build-cmake.sh:30-39` 的自带常量删除改为 source；`build-target.sh:61` 加“环境失败即停”。四个旧消费者（build-target / logstf-syntax-check / mf_export_probe / t75 verify-split）全部经薄壳受益，无需各自改动。
2. **环境 diff 0（可复现）**：四组断言全绿 —— 导出的环境与 HEAD 版 `env-msvc.sh` 逐行相同（PATH 2,223 B / INCLUDE 350 B / LIB 217 B 逐字节相同，PATH/INCLUDE/LIB 逐条比对），成功时 stdout+stderr 均为 0 B。
3. **坏工具链负对照**：`TF2_MSVC_ROOT=/c/NoSuchVS` 时薄壳 rc=1、环境**未改变**、stderr 给出命名原因；HEAD 版在同一条件 rc=0 且**已经把 PATH 改了一半** —— 这正是本次要消除的 silent half-env。
4. **身份/缓存**：工具链入口不在 D-90 输入集（roots = `native/{CMakeLists.txt,src,include,third_party,cmake}`），新增/提交这些 `.sh` **不改变 sourceContent**（本次读数 `9fd2b1d9…`，提交前后同值），也不触发 D-90 拒绝；代价是内容哈希**不能**证明“这个 exe 是用哪个工具链/入口脚本构建的”，所以复用判据仍是 **exe sha256 优先**，工具链身份必须写进 sidecar/读数。

## 1. 入口清单（改前 → 改后）

| 入口 | 位置 | 改前 | 改后 | 影响面 |
| --- | --- | --- | --- | --- |
| `env-msvc.sh` | 仓库根 | 4 个硬编码常量 + 手写 PATH/INCLUDE/LIB | 10 行薄壳：quiet source `tools/tf2-toolchain.sh`，失败 rc=1、环境不动 | 4 个直接消费者 |
| `build-target.sh` | :61 | `source ./env-msvc.sh`（失败静默继续） | 加 `\|\| { …; exit 1; }`，环境失败**具名即停** | 所有 target 构建 |
| `build-cmake.sh` | :30-39 | 与 env-msvc.sh 重复的一份常量 | `. ./tools/tf2-toolchain.sh \|\| exit 1`（删 10 行常量） | clean 构建 |
| `logstf-syntax-check.sh` | :27 | `source ./env-msvc.sh \|\| exit 90` | 不变（已有守卫，经薄壳受益） | 语法门禁 |
| `native/tools/mf_export_probe.sh` | :19 | `source "$ROOT/env-msvc.sh"` | 不变（无守卫；失败会返回 1 但不停止，见 §5） | probe |
| `evidence/t75-cmake/verify-split.sh` | :23 | `source "$ROOT/env-msvc.sh"` | 不变（同上） | 旧 T75 证据脚本 |
| `resource-discovery-check.sh` | :226-231 | 内联一份 PATH/INCLUDE/LIB | **未改**（不在本卡写域；另有 owner，改动会让不相关门禁在别人手里换行为） | 资源发现门禁 |
| `tools/warn-check.sh`、`E3b-out/compile-one.sh` | — | 已直接 source `tools/tf2-toolchain.sh` | 不变 | 编译门禁 |

`tools/tf2-toolchain.sh` 本身是 E3a 写好的（vswhere→目录扫描→sort -V 取最新；export 前断言 7 个目录 + cl/nmake/rc/mt **全有才导出**，即 all-or-nothing），本次只是让它真正成为入口。

## 2. 可复现证据：环境 diff 0

复现命令（任意时刻、仓库任意干净度均可）：

```
bash D:/_shared/EXEC-2026-11-15/INST1/E3b-entry-out/env-diff.sh .
```

采样方法（避免自证）：
- **now 侧** = 仓库内真实 `./env-msvc.sh`，在仓库根 source，与 `build-target.sh:61` 完全同一条路径解析链；
- **head 侧** = `git show HEAD:env-msvc.sh` 写到临时文件后 source（HEAD 版自包含，不依赖 tools/）；
- 两侧各起一个**独立非登录 bash**，继承同一份调用者环境并剥掉 `TF2_*` override，因此两侧唯一的差别就是被 source 的文件；
- 快照记录：source 的 rc、`env | sort`、PATH/INCLUDE/LIB **逐条拆行**（条目挪位会显示为行移动，而不是一整串 changed）。

2026-11-16 00:44:41 实测输出（日志：`E3b-entry-out/env-diff-run.log`）：

```
  OK   C1 env identical to HEAD:env-msvc.sh (50 lines; HEAD rc=0, shim rc=0)
  OK   C1.PATH byte-identical (2223 B)
  OK   C1.INCLUDE byte-identical (350 B)
  OK   C1.LIB byte-identical (217 B)
  OK   C2 stdout silent on success (0 B)
  OK   C2 stderr silent on success (0 B)
  OK   C3 broken toolchain -> shim rc=1
  OK   C3 environment unchanged after the failed source
  OK   C3 named reason: TF2-TOOLCHAIN: TF2_MSVC_ROOT=/c/NoSuchVS has no VC/Tools/MSVC
  OK   C3 control: HEAD rc=0 and DID change the env under the same broken override (the silent half-env this removes)
  OK   C4 shim env == direct tools/tf2-toolchain.sh source
ENV-DIFF=IDENTICAL claims=4 repo=/d/TF2_Native_Worktrees/t105-e5
```

每条断言都能变红：C1 的 diff 为空才绿；C2 量的是 0 字节；C3 要求 rc=1 且 `env | sort` 前后相同；C3 对照要求 HEAD 在同一 override 下 rc=0 且环境确实被改（改前改后不同 = ENV-UNCHANGED=NO）；C4 要求薄壳导出的环境与直接 source 工具链完全相同（薄壳没有夹带任何东西）。

**残留 shell 状态（不是环境）**：薄壳 source 后调用者 shell 里会多出 `TF2_TOOLCHAIN_RC`、`TF2_TC_MSVC_ROOT/VER`、`TF2_TC_SDK_ROOT/VER` 几个**未导出**变量（工具链诊断记录），子进程环境里看不到；树内 grep 无读者。`TF2_TOOLCHAIN_QUIET` 由薄壳临时置 1 并在返回前恢复原状。

## 3. 身份哈希 / D-90 / 复用判据

**输入集（唯一权威定义）**：`native/CMakeLists.txt`、`native/src`、`native/include`、`native/third_party`、`native/cmake`（`build-identity-content.sh` 的 `ROOTS`，与 `gen-build-stamp.cmake` 的 `TF2_CONTENT_ROOTS` 一致）。当前 186 个 tracked 文件。

**本次读数**：

```
$ bash build-identity-content.sh method    -> git-clean-v1
$ bash build-identity-content.sh hash      -> 9fd2b1d9679d6d8b6c2ca13c6f757c60213c9b5a31f615c401cc305664e04209
$ bash build-identity-content.sh stats     -> method=git-clean-v1 untrackedInputs=0 untrackedOther=0 ignoredInputs=0 ignoredOther=0 generated=2
```

未跟踪清单（全部在 roots 之外）：`docs/PROJECT-EXECUTION-ROADMAP-2026-11-15.md`、`docs/PROJECT-HANDOFF-2026-11-14.md`、`tools/fault-seam-check.sh`、`tools/tf2-toolchain.sh`、`tools/warn-baseline.tsv`、`tools/warn-check.sh`。

**对 D-90 的含义**：D-90 只拒绝 “roots 内出现未跟踪/被忽略但看起来是编译输入的文件”。工具链入口（`env-msvc.sh`、`build-cmake.sh`、`build-target.sh`、`tools/*.sh`）**全部在 roots 之外**，所以：① 它们不进 digest；② 不会被 D-90 拒绝；③ 把它们从 untracked 变成 tracked **不改变 digest**（工作树字节没变，且它们不在枚举范围）→ **不会诱发 rebuild**。这些都已由 `stats` 的 `untrackedInputs=0` 和提交前后同一 hash 直接体现。

**对缓存/复用判据的含义（重要，别读反）**：
- 内容哈希回答的是“编译输入有没有变”，**不回答**“这个 exe 是哪个工具链/哪版入口脚本产出的”。工具链版本、`env-msvc.sh` 的行为、`build-cmake.sh` 的 flags 全在哈希之外。
- 因此复用第一判据仍是 **exe sha256**（E2 已证 `/Brepro` + `/INCREMENTAL:NO` 下跨 build 目录逐字节可复现）；内容哈希是“输入未变”的旁证，不是“产物可信”的充分条件。
- 工具链身份必须随读数落盘：E4 的读数按 `<owner, vsroot/msvc/sdk, 入口脚本 git rev, exe sha256/size/mtime>` 标注；换机器/换工具链时即使 sourceContent 相同，也**不得**直接复用一个旧 sha 的结论，必须重新构建并对比 sha。
- 正面推论：本次环境逐字节未变（§2）⇒ cl 的 flags/宏环境不变 ⇒ 期望重新构建得到同一 exe sha；E4 用 02:00-02:25 的窗口实测这一点。

## 4. 同一批次落地的 N23 门禁

### 4.1 `/W4` 告警门禁 `tools/warn-check.sh`
- 范围：带 `/W4` 的 7 个 target、86 个 TU（tf2_core 52、tf2_demo_native 28、minimap_probe 2、其余四个 probe 各 1）；用 `flags.make` 里的真实 flags 编译，产物落 `.scratch/warn-check/objs/`，**不碰 build tree**。
- 比较方式：`<count> TAB <file>:<code>`，按站点+代码建键（不是行号），所以在一个告警上方插几行不会误报 NEW；`--emit-baseline` 在有 TU 失败时拒绝写基线（BASELINE-REFUSED），防止把编译失败洗成“告警变少”。
- 基线 `tools/warn-baseline.tsv`：10 条、计数合计 15（native_renderer_viewmodel C4267×2 / C4456×2、native_renderer_overlay C4456/C4100、native_renderer C4456、`<external>/xutility` C4244、main.cpp C4456/C4244×5/C4457）。
- 读数：`--selftest` → `WARN-GATE=SELFTEST-PASS planted=1 reported=1`（种一个 C4189，比较器必须只报这一处）；全量 → `WARN-GATE tus=86 new=0 fixed=0 same=9`。
- 五个 TU 的 `/W4` 清理（`camera_bookmarks`、`install_manifest`、`logstf_panel`、`model_loader`、`spectate_target`）已包含在本批提交，基线里没有它们的键 —— 即清理是“真的清掉了”，不是被基线吸收。

### 4.2 失败接缝门禁 `tools/fault-seam-check.sh`
- 事实前提：`audio_effects_probe` 早就实现了整套 waveOut 失败注入（9 次注入 open、reset 失败、unprepare 失败、cleanup 失败），但 2026-11-15 之前**树里没有任何脚本调用** `--player-fault-selftest` —— 断言存在、读数存在，就是不 gate 提交。
- 现在要求：harness 自报成功、四个注入计数 ≥1（防“根本没注入所以全过”）、`failedChecks==0`（失败必须被具名）、`backendRestored==true`、接缝触发后**正常路径仍 rc=0**。
- 读数：`--selftest` 5/5 全按预期（1 绿 4 红）；对真实 probe → `FAULT-SEAM=PASS seams=winmm injected=9 named=0-unnamed backend-restored=yes valid-path=ok`。
- **未覆盖（如实记录）**：N23 的 D3D/`--renderer-fault` + 像素断言半边不在本脚本里；驱动它需要 product exe 和一个窗口，且本批期间 `native/src/main.cpp` 带着别的实例的语法错误。这是缺口，不是通过。

## 5. 没做 / 风险 / 边界

- `resource-discovery-check.sh:226-231` 的内联副本未迁（不在本卡写域）。建议替换为 `source tools/tf2-toolchain.sh` 后删掉那 6 行；风险是若它被别的卡片依赖“无断言也能继续”的行为，替换后会变成硬失败 —— 需其 owner 决定。
- `native/tools/mf_export_probe.sh:19`、`evidence/t75-cmake/verify-split.sh:23` 直接 source 但没有 `\|\|` 守卫：薄壳失败时它们会拿到 rc=1 但继续跑（和旧的静默行为一致）。建议各自 owner 加一行守卫，我这里不动别人的文件。
- `build-target.sh` 语义变化：环境失败时**从“继续跑然后晚点炸”变成 rc=1 立即停**。调用方全部是 `if`/`||` 判断（已 grep `camera-ui-check.sh`、`build-identity-check.sh` 等），没有对 `rc==1` 的精确断言，所以不破坏调用方。
- 只有本机验证：无第二台机器、无远程 CI；本机 discovery 结果 MSVC 14.44.35207 / SDK 10.0.26100.0，别的机器会选别的版本（这正是 §3 要求把工具链身份写进读数的原因）。python 侧 31 处硬编码未动（E3a 已记录）。
- `tools/warn-check.sh` 的门禁范围只覆盖“本来就请求 /W4”的 target；不开 /W4 的 target 不在本期（E3b batch 2）。

## 6. 复现命令

```
bash D:/_shared/EXEC-2026-11-15/INST1/E3b-entry-out/env-diff.sh .   # §2 四条断言
bash build-identity-content.sh stats                                 # §3 D-90 输入普查
bash build-identity-content.sh hash                                  # §3 内容哈希
bash tools/warn-check.sh --selftest                                  # §4.1 门禁能变红（0.7 s）
WARN_CHECK_JOBS=8 bash tools/warn-check.sh                           # §4.1 全量（~2.5 min）
bash tools/fault-seam-check.sh --selftest                            # §4.2 能变红
bash tools/fault-seam-check.sh                                       # §4.2 真实 probe 接缝
```

共享证据目录：`D:/_shared/EXEC-2026-11-15/INST1/E3b-entry-out/`（`env-diff.sh`、`env-diff-run.log`、`env-head.txt`、`env-shim.txt`、`env-head-vs-shim.diff`、`neg-broken-toolchain.txt`、`neg-broken-toolchain.stderr`、`neg-broken-toolchain-head-control.txt`）。
