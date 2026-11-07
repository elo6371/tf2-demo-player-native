
# T92 安装 CLI 契约修复说明（D-24 / D-25）

本页把两个安装器命令行的真缺陷合并成一份可复核的记录：现象、根因行号、修法、门禁怎么抓、修前修后对比。
代码在分支 `task/t92-install`（HEAD 3676d0a，基于冻结基线 e15577b），**未 push**；门禁 `install-cli-contract-check.sh` 539 行、sha256 `f175d9cead78787ef6d9039e4898d1aedab42c63f367612a389ae1719107df25`。

## 0 一句话

`--install-dry-run` 单独用会打开真 GUI（D-24）；manifest 的 `installDirectory` 写的是 ANSI/CP936 字节而格式说是 UTF-8（D-25）。
两条都修在源码里，并且各配一条能在合并树上判红的门禁断言（共 5 组断言 + 2 个变异）。

## 1 D-24：单独 `--install-dry-run` 退化成产品启动

### 1.1 现象（冻结二进制实测，2026-11-07）

| 观测 | 证据 |
| --- | --- |
| 单独 `--install-dry-run` 跑满全生命周期：67.6 s（13:44:04.420→13:45:12.024）后以 rc=1 退出，stdout 0 字节，没有任何 `install=DRYRUN` | t85-install 交付物 §6.1，round1 C1 |
| 只观前 8 s：进程存活、`tasklist //v` 抓到真窗口标题 `TF2 Demo Player - TF2 资源目录: D:\Stea…`，随后脚本 kill（rc=143=128+15） | round2 R1，logs/accept-results-r2.txt |
| 目录零变化、无 manifest、无 payload 变动 | 同上 `inventory_changed=0` |

两个观察是同一行为：**flag 单用没有走 dry-run 分支，而是走正常产品启动路径**。rc=1 那次的退出原因无证据可判定（无输出、无窗口日志），不主张崩溃或关窗；8 s 那次只证明起的是 GUI，不主张"永久挂住"。

### 1.2 根因

`native/src/main_lifecycle.cpp`（e15577b）：

- `:27` `else if (wcscmp(argument, L"--install-dry-run") == 0) request->options.dryRun = true;` —— 只置 `options.dryRun`，**从不设置 mode**；
- `:33` `return request->mode != LifecycleMode::None;` —— mode 仍是 None，解析器返回 false；
- `native/src/main.cpp` 于是按"不是生命周期请求"处理，走正常启动：建窗口、读设置、跑 GUI。

同源推论（源码可判、未实测）：修前 `--uninstall --install-dry-run` 的 dryRun 对 Uninstall 模式毫无作用，会**真的执行卸载**——把"我以为在试跑"变成真操作，这比挂住更危险，所以修法里第二半是显式拒绝而不是让显式 mode 静默取胜。

### 1.3 修法（本分支）

| 文件:行 | 内容 |
| --- | --- |
| native/src/main_lifecycle.cpp:42-44 | 整条参数表读完之后再决策：`if (dryRun && mode == None) mode = LifecycleMode::Install;`（位置无关） |
| native/src/main_lifecycle.cpp:48-51 | `if (dryRun && mode != None && mode != Install) { dryRunConflict = true; return true; }` —— 两种问题一起问就拒绝 |
| native/src/main_internal.h:61-64 | 新增 `bool dryRunConflict = false;` |
| native/src/main_lifecycle.cpp:79-85 | 冲突分支输出 `install=FAIL reason=dry-run-conflict mode=<uninstall\|install-status\|version\|unknown> detail=--install-dry-run only modifies --install`，`exitCode = 2` |
| native/src/install_lifecycle.cpp:296-297 | dry-run 自报身份：`install: dryrun mode=install dir=<UTF-8 路径> plan=<N> files`，再接原有的 `install=DRYRUN files=… backups=… manifest=…` |

正确用法两种，等价：`--install-dry-run`（修后等价于 install+dryRun）与 `--install --install-dry-run`。

### 1.4 门禁怎么抓（A1 / A1b / A2 / A2b）

- **A1**：裸 `--install-dry-run` 必须 exit 0、5 s 内返回、输出含 `install=DRYRUN` 与 `files=4`、目录逐文件哈希不变、无 manifest。修前这条直接对着"起了 GUI"判红。
- **A1b**：裸写法与 `--install --install-dry-run` 规划同一个 payload（两行 `files=` 数值相等），且输出含自报身份行 `install: dryrun mode=install dir=` —— 防止"只把 mode 塞进去、位置一变又失效"。
- **A2 / A2b**：`--uninstall --install-dry-run`、`--install-status --install-dry-run` 必须 exit 2、含 `reason=dry-run-conflict` 与 `mode=uninstall`/`mode=install-status`、目录不变、不得出现 `status=VERIFIED`。
- **变异 M1**：删掉 main_lifecycle.cpp:43 那行 `request->mode = LifecycleMode::Install;`（锚点唯一：4 空格缩进；:25 的 else-if 不匹配），重建后 A1 必须红；exe 哈希不变 = `MUTATION-NOT-LINKED`（退出 90）。

## 2 D-25：manifest 的 installDirectory 写成 ANSI 字节

### 2.1 现象（实测字节）

在中文+空格路径 `D:/_shared/t85-install/work/验收 路径 空格/cjk-bytes` 安装后，`.uninstall/install-manifest.txt` 里 `installDirectory=` 行 69 B，值为 52 B：

`443a2f5f7368617265642f7438352d696e7374616c6c2f776f726b2f` + `d1e9cad5`(验收) + `20` + `c2b7beb6`(路径) + `20` + `bfd5b8f1`(空格) + `2f636a6b2d6279746573`(/cjk-bytes)

- `matches_cp936=True`、`matches_utf8=False`；
- 按 UTF-8 严格解码失败：`can't decode byte 0xd1 in position 28: invalid continuation byte`；
- 按 cp936 解码恰好还原原路径。

方向判定：**写入的是 ACP/CP936，被当 UTF-8 读时才乱码**（不是反过来）。产品自身不受影响（全树只有写/序列化/解析三处引用该字段，产品从不回读它），受影响的是任何外部解析器/工具。

### 2.2 根因

`native/src/install_lifecycle.cpp`（e15577b）：`:340` `manifest.installDirectory = installDir.generic_string();` —— MSVC 上 `generic_string()` 给的是 **active code page** 字节（本机 cp936）；
`native/src/install_manifest.cpp`：`:189` 序列化直写、`:255` 解析直接赋值，两边都不校验编码。

### 2.3 修法（本分支）

| 文件:行 | 内容 |
| --- | --- |
| native/src/install_lifecycle.cpp:354 | `manifest.installDirectory = installDir.u8string();`（C++17 下返回 `std::string` 的 UTF-8 字节） |
| native/src/install_manifest.cpp:47-64 | 新增匿名 namespace 的 `bool validUtf8(const std::string&)`（拒绝 >0xf4 的 lead、<0xc2 的 lead、非 0x80 续字节、截断） |
| native/src/install_manifest.cpp:66-80 | `std::string acpToUtf8(const std::string&)`：`MultiByteToWideChar(CP_ACP)` → `WideCharToMultiByte(CP_UTF8)` |
| native/src/install_manifest.cpp:82-90 | `installDirectoryAsUtf8`：合法 UTF-8 原样返回；否则 `std::printf("manifest: legacy-acp installDirectory\n"); fflush(stdout);` 再转 ACP —— 让读侧回退**可见、可测** |
| native/src/install_manifest.cpp:308 | parse 的 `installDirectory` 分支改用 `installDirectoryAsUtf8(value)` |

### 2.4 门禁怎么抓（A3 / A4 / A5）

- **A3**：中文路径安装成功后，manifest 里的值必须 `valid_utf8=yes`、`equals_utf8=yes`（与传入路径逐字符相等，比较前把反斜杠归一化成 `/`）、且**不得** `cp936_reads_as_path=yes`。
- **A4**：控制组 —— UTF-8 manifest 跑 `--install-status` 不得出现 legacy 提示；把同一行重写成 cp936 字节后再跑，必须仍然 `status=VERIFIED` 且输出含 `manifest: legacy-acp installDirectory`（旧版本装的目录还能读）。
- **A5**：中文路径 + legacy manifest 走 `--uninstall`：exit 0、`uninstall=PASS`、`removed>=1`、manifest 消失（证明回退不只是打印，卸载真的能工作）。
- **变异 M2**：把 `installDir.u8string()` 改回 `generic_string()`（锚点唯一，install_lifecycle.cpp:354），重建后 A3 必须在中文路径上红；exe 哈希不变 = 90。

## 3 门禁本体

- 位置：仓库根 `install-cli-contract-check.sh`，用法 `bash install-cli-contract-check.sh --forward|--mutation <out-dir>`；
- 退出码：0=全绿，1=有断言红，2=usage，90=`MUTATION-NOT-LINKED`；
- 末行是可判红字面串：`INSTALL-CLI-CONTRACT=PASS assertions=N` / `INSTALL-CLI-CONTRACT=FAIL assertions=N`；变异轮另有 `INSTALL-CLI-CONTRACT-MUTATION=…`；
- **不自取 hostlock**（整链里由外层持锁），**fresh worktree 无 `native/build-nmake` 时自建**：0/5 步先 `bash build-target.sh tf2_demo_native install_cli_probe native_install_smoke_probe` 并断言 rc=0 与三个产物存在；
- hermetic：10 处产品启动全部带 `--settings-file`/`--ui-settings-file` 指向 mktemp 副本（`trap` 清理，不改真实 `%LOCALAPPDATA%`）；
- 断言分组：A1 裸 dry-run、A1b 两种写法等价、A2/A2b 冲突拒绝、A3 UTF-8 写入、A4 legacy 回退、A5 legacy 后卸载；变异 M1/M2 跑在 forward 之后。
- 环境超时：整链给这一步 1800 s；单跑 forward（含首次全量构建）数分钟量级，mutation 是 4 次增量重链 + 一次 45 s deadline 的 M1 兜底。

## 4 第一次跑红：不是产品缺陷，是门禁自己缺前置

首次在 fresh worktree 跑 forward 得到 `cases=4 checks=31 fail=14`，A1/A3/A4 大片红而 A2/A2b 8 条全绿。根因：

`cmake --install` 在新 prefix 只装了 `tf2_demo_native.exe` 就报
`CMake Error at native/build-nmake/cmake_install.cmake:40 (file): file INSTALL cannot find "D:/TF2_Native_Worktrees/t92-install/native/build-nmake/native_install_smoke_probe.exe": File exists.`
—— `native/cmake/tf2-package.cmake:30` 的 `install(TARGETS tf2_demo_native native_install_smoke_probe …)` 需要一个没构建的目标产物，stage 只剩 1 个 exe、没有 `.uninstall/payload`，于是所有安装类断言都对着 `install=FAIL reason=no_payload` 判红。

`install-lifecycle-check.sh`（1,095 行 / 205 条断言）多年来默认 build tree 里已有该 exe，所以从没暴露这件事。修法只动门禁：0/5 自己构建三个目标并断言、A1/A3/M1/M2 加 stage rc 断言与失败证据（cmake 原始报错 tail + stage 文件清单）。教训：**fresh worktree 里 stage 的前置必须自己建**。

## 5 修前 / 修后对比

| 维度 | 修前（e15577b 二进制 a3c93d57…3724e9） | 修后（本分支构建） |
| --- | --- | --- |
| 裸 `--install-dry-run` | 起真 GUI：67.6 s 后 rc=1（round1），或 8 s 存活后被 kill（round2）；无 `install=DRYRUN` | exit 0、`install=DRYRUN files=4`、`install: dryrun mode=install dir=… plan=4 files`，目录零字节改动（A1，见 §6） |
| `--uninstall --install-dry-run` | dryRun 对非 Install 模式无效果 → 会真的卸载（源码推论，未实测） | exit 2、`reason=dry-run-conflict mode=uninstall`、目录不变（A2 首跑已绿） |
| 中文路径 manifest `installDirectory` | cp936 52 B，`matches_utf8=False`（字节实测） | UTF-8 且等于路径（A3，见 §6） |
| 旧版 manifest 读回 | 直接赋值，外部读者乱码 | 回退 ACP 并打印 `manifest: legacy-acp installDirectory`；status 仍 VERIFIED（A4） |
| 旧版 manifest 卸载 | — | exit 0、`uninstall=PASS`、removed≥1（A5） |

## 6 合并树实测

（本节由合并窗口的 forward/mutation 结果回填；本节出现前，A1/A1b/A3/A4/A5 与 M1/M2 都只能算"有断言、未在本分支跑绿"；A2/A2b 在 2026-11-07 首跑里已实测 8 条全绿。）

## 7 复现命令

```bash
# 正向（fresh worktree 会自动构建三个目标）
bash install-cli-contract-check.sh --forward  /d/_shared/t85-install/t92-gate
# 变异（必须接在 forward 之后、同一 OUT）
bash install-cli-contract-check.sh --mutation /d/_shared/t85-install/t92-gate
```

手工复现 D-24（修前二进制）：
```bash
"D:/_shared/t85-install/work/pkg/tf2_demo_native.exe" --install-dry-run \
  --install-dir "D:/_shared/t85-install/work/manual" \
  --settings-file  "D:/_shared/t85-install/out/manual-settings.ini" \
  --ui-settings-file "D:/_shared/t85-install/out/manual-ui.ini" \
  --audio-disabled --hidden-window
# 修前：起 GUI、无 install=DRYRUN；修后：立即打印 install: dryrun … 并退出 0
```

手工复现 D-25 字节层：安装到中文路径，然后
```bash
grep '^installDirectory=' .uninstall/install-manifest.txt | hexdump -C
# 修前 d1e9cad5 / c2b7beb6 / bfd5b8f1 是 cp936；修后同一路径是 UTF-8 的多字节序列
```
