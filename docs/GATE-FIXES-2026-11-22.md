# 门禁修复与第 26 步重判（2026-11-22）

> **19:01 结论**：第 26 步为 **A+B**。同 tick 64161、只换二进制（base 390a7c76 / head 6bf2a4b3），覆盖帧逐字节相同；3156 px 的 PASS（4a8aaff7，11-09）早于静态道具进入 entity pass（8fea2539，11-12），门禁之后没重定基线。
> 门禁修复：`t61-pixels.py --identity`（姿态断言只看带实体 id 的像素；新增"场景静止 ≤1.0%"断言，没有 `--off` 时不算通过；身份掩码为空不再提前返回，保证变异套件的具名原因可达）；`player-animation-pixels.sh:372` 传入 `--identity`；`verify-all.sh` 第 26 步断言数 28→29。
> 下文 §2 里"第 26 步可能是被污染二进制造成的"推测**不成立**，保留作为记录。

配套：`docs/CHAIN-RED-ATTRIBUTION-2026-11-22.md` §4 的六件事，加第二轮全仓同类排查（§5）。只改了根目录门禁脚本与 docs/，**未改 native/ 下任何文件**。
改动全部未提交、未构建、未运行（执行环境只能读写文件，不能跑 bash/构建）。

## 1. 改了什么

| 文件 | 改动 | 对应红步 |
|---|---|---|
| `mutate.sh` | `restore_entry()`：`cp -p` → `cp` + `touch`；内容已一致时不复制不 touch | 8，及其后被污染的 10/11/12/15/19/25/27 |
| `mutate.sh` | 新增 `registered_dirty()` / `restore_catchup()`，在每个 case 开头和循环结束后各调一次 | 同上（新发现，见 §2.1） |
| `mutate.sh` | m10 变异 `false && x` → `!x && x`（避免 C4127 撞 /WX） | 8 的 `broke=1` |
| `weapon-animation-check.sh` | `restore_entry()` 同上修法 | 22 之后的 23–29（新发现，见 §2.2） |
| `weapon-animation-check.sh` | 见证者取「最后一条 any= 片段帧数 > 1 的 animwitness」，纯 bash，无多帧时回退 `tail -1` | 22 |
| `viewmodel-companion-check.sh` | `ROOT` 赋值块移到 `TRACE_PRODUCT_DIR` 推导之前 | 24 |
| `verify-all.sh` | `exe_count` 53 → 55；`errors=0`、`exe_count`、`fixture_failures=0` 的 `$` 锚允许可选 CR（`$'...\r?$'`，不经 tr 垫片） | 1、3 |
| `sultry-translucent-atlas-check.sh` | 新增 `iso_line()`，按 `slot0Binds=` 选 `gpu: world isolation` 行，5 处调用替换 | 29 的 8 行 `got ''` |

完整 diff：会话产物 `gate-fixes-2026-11-22.patch`（与 `git diff` 应一致）。

## 2. 新发现（归因文档里没有）

### 2.1 `mutate.sh` 的 `|| continue` 跳过了还原
循环体里 17 处 `rebuild ... || continue`、以及 `patch_in ... || { rc_all=1; continue; }`，都会跳过循环末尾 `:838` 的 `restore`。
`.scratch/mutation-build.log.failed` 是直接证据：构建 `tf2_demo_native` 时 `demo_header_strtab.cpp(833)` 报 C4127/C2220——
那一行正是 m10 的变异点，说明 m10 构建失败后它的变异**带进了后续 case**，只有 on_exit 才把源码还原（且用 `cp -p`，不触发重编）。
所以污染不只是「`cp -p` 让重建空转」一个原因，而是两个叠加。

### 2.2 第 22 步也在往产品 exe 里留变异
`weapon-animation-check.sh` 的三个变异臂（`--mutation` 改 `animation_binding.cpp` 强制 bind pose；`--mutation-model` /
`--mutation-identity` 改 `main.cpp` 的 `--model` 单位矩阵）同样用 `cp -p` 还原，`:1205` 的还原构建因此空转。
第 22 步在第 25、26 步之前运行，所以：
- 第 25 步 `no sequence with four distinct quarter-cycle poses`，恰好是 bind pose 变异的症状；
- **第 26 步**抓帧（`evidence/verify/26-animation-pixels/pixel-check.txt` mtime 15:14:44）时，exe 链接的是带
  m7（precache 清零）/m9 的 `tf2_core.lib` 加第 22 步残留的变异对象。

**结论：第 26 步的 C 判定目前不成立，至少未被证明。** 归因文档 §2 证明了「像素是 entity pass 画的」，但没有排除「entity pass 画的是被污染的二进制」。
`VISIBLE players=1 props=0`、`DRAWN 2`（提交时 96 个 id、5 个 props）与 m7 让模型路径解析失败的形状一致。
**先在干净构建上复跑第 26 步，再决定要不要去查 instance range。**

## 3. 验证顺序（在本机 bash 里执行，构建按 tools/lockrun.sh 惯例）

1. `git diff --stat`，只应看到 16 个根目录脚本（§1 与 §5）+ 本文件 + `docs/REFERENCE-REPOS-2026-11-21.md`；`git status --porcelain -- native/` 应为空。
2. **一次性清除历史污染**：当前 `native/build-nmake/` 下的 obj 是 15:44–15:46 重编的（链结束于 15:35），来源未知。
   做一次干净构建（删 `native/build-nmake/CMakeFiles/tf2_core.dir` 与 `tf2_demo_native.dir` 下的 obj，或整树重建）。
3. 干净基准自检：`presentation_probe.exe` 应为 `"historyCoverage":true` / `4096`；
   `entity_protocol_probe.exe` 对 bagel 不应出现 `model_precache_entries=0`；`entity_message_fixture_probe.exe` 应为 `fixture_failures=0`。
4. 单跑第 26 步（`player-animation-pixels.sh`）——这是本轮最重要的一个读数。
   - 回到 `MASK pixels≈3156` / `68.00%` 附近 → 第 26 步改判 A，链上无真回归。
   - 仍是约 26 万 px → 才按归因文档 §2 末去查 `native_renderer_frame.cpp:1823/:1949` 的 instance range。
5. 单跑 `viewmodel-companion-check.sh`、`sultry-translucent-atlas-check.sh`、`weapon-animation-check.sh`（含三个变异臂），
   每跑完一个立即 `git status --porcelain -- native/`，并确认没有比源码更旧的源文件 mtime 回退。
6. `bash mutate.sh`：期望 `broke=0`；如出现 `RESTORE-CATCHUP` 行，说明还有 case 构建失败，需要看 `.scratch/mutation-build.log.failed`。
7. 整链 `verify-all.sh`。仍会红的只应剩输入缺失类：5、9、14、28 几何半、29 材料半（POV 录音与 14 份标定 demo）。

## 4. 本轮未做（留后续）

- 进程替换与 m12/m13 的 `false &&`：已在 §5 完成。
- 第 19 步：见 §5.4，**改判为 A，不改 native/**。
- 还有 7 处进程替换（`map-geometry-lighting-check.sh:637,910`、`quality-audit-check.sh:365`、`verify-render.sh:291`、
  `verify-entity.sh:192`、`viewmodel-material-check.sh:1420`、`viewmodel-contract-check.sh:1205`）。除 `map-geometry-lighting-check.sh`
  两处的 diff 结果会进入断言外，其余只用于诊断打印，留到下一轮。
- `.scratch/viewmodel-animation.lock` 陈旧锁识别（本次检查时该目录已不存在）。
- 其它门禁里同样的 `mkdir` 锁、`cp -p` 还原模式：建议抽一个公共 `tools/gate-lib.sh`（锁带 owner/pid、还原必 touch、CR 容忍的 grep、无进程替换的 diff）。

## 5. 第二轮：全仓同类排查

### 5.1 进程替换交给 WinuxCmd 垫片 → 实体文件（6 个脚本）

| 文件 | 原位置 | 改法 |
|---|---|---|
| `class-pixels-check.sh` | :340-341 | 两份 census 写进 `$ISO_DIR/census-identity{,-2}.txt` 再 diff |
| `hud-check.sh` | :1027-1035 | after 清单只生成一次，写进 `$OUT/user-shared-after.sha256.txt`，判定与打印读同一份 |
| `install-lifecycle-check.sh` | :376 | `hashlist` 写 `$OUT/clean/{stage,pkg}.sha256.txt` |
| `install-readiness-check.sh` | :461、:805-806 | 复用上一行已生成的 `package-hashes-after-runs.txt`；M3 两份排序清单落盘后再 `comm` |
| `census-negative-test.sh` | :116 | 抽样表写进 `mktemp` 文件 |
| `check-probe-output-additive.sh` | :173、:216-218（`paste` 三路）、:312、:316 | 全部落到 `$TMP/`；:312 就是第 7 步假红（`0a1,18`）的位置 |

### 5.2 `false &&` 变异撞 /WX（C4127→C2220）→ `!(E) && (E)`

`mutate.sh` m10/m12/m13、`camera-ui-check.sh`（keybinds 两处）、`playback-control-check.sh` m3/m4、`viewmodel-animation-check.sh`（两处）、
`viewmodel-companion-check.sh`（四处）、`vpk-build-check.sh` m2/m5。被变异的翻译单元都属于 `tf2_core`（`native/cmake/tf2-libraries.cmake`）
或 `tf2_demo_native`，二者都在 `TF2_WX_DEFAULT_TARGETS`（`native/CMakeLists.txt`）。`evidence/hud/mut1.build` 有同类 C4127 记录，
`hud-check.sh:1162` 的注释也早就写过这条规则。

### 5.3 `cp -p` 还原 → D-101

`outline-check.sh:168-199` 在 2026-11-11（D-101）已改成 `cp` + `touch` 并写明了原因，但没有推广。本轮推广到
`mutate.sh`、`weapon-animation-check.sh`、`camera-ui-check.sh`、`idle-spin-check.sh`。
`player-*-mutations.sh`、`review-workspace-mutations.sh` 在 `cp -p` 后紧跟 `touch`，本来就正确，未改动。

### 5.4 第 19 步重判：A，不是计数器缺陷

`resolverFallbackScanned` 在 `native/src/entity_model.cpp:763-767` 累加：遍历整张 `statesByIndex`，跳过已有 render request 的实体，其余每个槽 +1。
读数恰好是 2048（整表），说明 `covered` 为空，即这一次**没有任何 request**。这正是 m7 变异的形状（precache 清零 → 没有模型路径 → 没有 request），
与第 12 步的 `model_precache_entries=0` 同源；`demo_header_entity.cpp:337` 的 `resize(2048u)` 只是同值巧合。
另有一个独立的小问题：该计数器把空槽（`classId < 0`）也算进去。干净构建复跑后如果 bagel 仍不是 0，再在 `:767` 前跳过空槽。
这是 native/ 改动，需要单独提交，并重新核对 fixture 的 `fallbackScanned=3`。

## 6. 提交与同步

`D:/TF2_Native_Worktrees/_shared/sync-2026-11-22.sh` 给两个仓各做一个提交：本仓（task/merge-all）与问题库（`_playbook`）。
**默认只提交、不推送**；确认验证通过后加 `--push`。
