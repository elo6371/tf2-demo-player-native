# 门禁前提过期普查（2026-11-20）

状态：核对并改写自 Gemini 第 9 轮 G13 交付（`gemini_round9_deliverable_2026_11_20.md` 第二部分）。
基线 `28aba48274a9f33da2d9c6ee9053dd0ec6e6f02b`；脚本按工作树原样核对（本类文件本轮未改）。
核对方法：把交付里的每条「脚本:行号 + 前提原文」逐条打开原文比对，再打开它引用的台账行与它假设的磁盘文件；
凡交付自己标注「不确定」的，本轮都实读了目标文件。**142 个根目录 `*.sh` 的数字复核实为 142。**

判定记号：**CONFIRMED** = 前提确实过期/硬编码成立；**PARTIAL** = 现象成立但交付给的归因或严重度不对；
**REFUTED** = 交付的结论不成立。

## §1 逐条判定

| # | 交付判定 | 本轮判定 | 证据 |
| :-: | :--- | :-: | :--- |
| 1 | `world-material-semantics-check.sh:136-138` 钉的 07-02 录制已不存在 | **CONFIRMED** | `:138` `DEMO="$TF/demos/autorecord_2026-07-02_13-26-46.dem"`；`docs/DEFECT-REGISTER-MASTER-2026-11-14.md:40`（D12）原文：「它钉的 `tf/demos/autorecord_2026-07-02_13-26-46.dem` 已不在磁盘上，capture A rc=2 重试 12 次全空」。本轮实测 `tf/demos/` 有 2114 个文件，**没有任何 `2026-07-02` 开头的文件**。 |
| 2 | `playback-error-paths-check.sh:54,60-61` 的录制缺失导致假红 | **REFUTED（归因错，硬编码成立）** | 该脚本 `:55-59` 自己写着：「N01 plan A (2026-11-15): the 07-02 Sniper POV this gate pinned is gone from disk (Steam tf/demos, E:/demo测试 and D:/TF2_Demo_Player all searched). The pl_upward_f12 POV the user recorded replaces it ... E's two counts are re-pinned to this recording's readings, not relaxed.」所以「缺失」早已被处理并重钉，`AUDIO_DEMO` 指向的 `E:/demo测试/pl_upward_f12/POV/autorecord_2026-08-08_21-43-59.dem` **实测存在**（306,131 ticks 的 POV）。留下的真问题只有：硬编码 `E:/` 路径，且 `AUDIO_DEMO`/`CAM_DEMO` 未走 `TF2_` 环境变量（`EXE`/`TF` 已走，见 `:51-54`）。 |
| 3 | `viewmodel-animation-check.sh:121-122` 钉死录制缺失致假红 | **PARTIAL** | `:121-122` `TF="D:/SteamLibrary/..."` / `TESTDATA_ROOT="D:/TF2_Demo_Player/testdata/demos"` 确实硬编码、无 env 覆盖——这一点成立。但它钉的 `bb841c6d...snakewater` 与 `dfbce608...bagel_rc11` 本轮实测**都在**，所以「缺失致假红」这个归因本轮不成立（`N-STATUS-2026-11-14.md:78` 记的是当年那轮，已过时）。 |
| 4 | `viewmodel-animation-check.sh:1117` 写死帧数 `61` | **PARTIAL（脆弱非过期）** | 原文 `assert_eq "real motion, applied: frame count" ... "61"` 确认存在。它绑定的是固定 fixture demo 的一个序列帧数，换 demo 即红；属「脆弱断言」，不是「前提已过期」。 |
| 5 | `:1159-1160` 写死 `classesWithSequence 197` | **PARTIAL（脆弱非过期）** | 同上：特定快照类计数，随 TF2 更新失效。 |
| 6 | `:1169-1170` 负控 `assert_ne changes 91` | **PARTIAL（脆弱非过期）** | `:1164-1172` 已注明「基线 91 / mutation 21」，语义是「变异体结果不得等于基线」，本身是正确设计；固化成 91 才是脆弱点。 |
| 7 | `playback-error-paths-check.sh:23-24` 注释钉 `verify-release.sh:399/:412` | **CONFIRMED（低危）** | 打开 `verify-release.sh`：`:399` 仍是 `elif [ "$rc" -eq 0 ] && grep -qE "^[[:space:]]*${verdict}=PASS($| )" "$log"`，`:409` 取 `key=...` 末行、`:412` `rec "step=... key=..."`。注释的「:399 要求 verdict=PASS、:412 取最后一个 TOKEN」与现状一致——**行号暂未漂移，风险是将来重构**。 |
| 8 | `world-black-account-check.sh:36-37` 硬编码 `E:/demo测试/...` | **CONFIRMED（可移植性）** | 原文确认；该 sultry 录制本轮实测存在。 |
| 9 | `player-animation-check.sh:47-49` 硬编码 `D:/TF2_Demo_Player` | **CONFIRMED（可移植性）** | 原文确认；`BAGEL`（rc13）与 `SNAKEWATER` 本轮实测都在。 |
| 10 | `frame-capture-check.sh:67,76-77` 与 `hud-check.sh:63-64` 硬编码盘符 | **CONFIRMED（可移植性）** | 原文确认（`TF=`、`BAGEL=`/`SNAKE=`、`DEMO=`）。 |
| 11 | `class-pose-family-check.sh:5-6` 注释「idle|moving only」已与 `crouch/flinch/airborne` 支持冲突 | **REFUTED** | 产品侧选择器 `native/src/main.cpp:481-505` `choosePlayerAnimationSequence(const AnimationBinding&, bool moving, std::string* label)` 的实现是 `chooseSequenceByNeedles(..., moving ? kMovingNeedles : kIdleNeedles, ...)`——**只有 idle / moving 两组 needle，没有 crouch/flinch 路径**。注释说的是这个选择器的可达集，不是门禁能力，因此与 `animation-drive-states` 驱动别的家族并不冲突。 |

| 12 | （本轮新发现，交付未列）`weapon-body-check.sh` 钉的 07-02 录制同样已不存在 | **CONFIRMED** | 本轮实跑（job bash-26b108cf1a9048e9，rc=1）：`0/6` 段 FAIL「the recording's size = 0, expected 21856667」「the recording's sha256 = <none>, expected c8a58501d222bdd9aec6db92d2782931a29a13d963a87364d603263e6dab091e」；`body-off.trace` 头 `demo: open failed path=D:/SteamLibrary/steamapps/common/Team Fortress 2/tf/demos/autorecord_2026-07-02_13-26-46.dem reason=demo file cannot be opened`；`3/6` 段 body-on/body-off/repeat 全 `rc=2`、读数全 `<none>`。与 #1/D12 同一根因（`CAPTURE_TICK=2000`，证据目录 `evidence/fast/weapon-body` 已被 .gitignore:41 忽略）。 |
| 13 | （本轮新发现）`viewmodel-apply-check.sh` 第 0 段在 N01 重钉后失效 | **CONFIRMED** | `:56` `INPUTS=evidence/viewmodel/animation-inputs.txt`；`:267-283` 的循环只收 `root = steam`（`:270` `[ "$root" = "steam" ] || continue`，拼 `$TF/demos/$name`），而输入文件仅两行数据、root 是 `archive-pov`（`:32` 146853666/`14b4943a…` `autorecord_2026-08-08_21-43-59.dem`）与 `testdata`（`:33`），两行都被 skip → `assert_eq "pinned recordings verified" 0 1` → `VIEWMODEL-APPLY=FAIL (no recording)`，2026-11-20 09:54 实跑约 2 秒退出、未留锁。**后果：m4 修复无法用整门禁验证。** |

| 14 | （本轮新发现，交付未列）`while read … done < 钉住表` 里调 `sha256sum` 会吞掉表的剩余行 | **CONFIRMED** | 本机 `which sha256sum` = `…/winuxcmd/usr/bin/sha256sum.exe`（**WinuxCmd 1.2.1**），它在**给了文件参数时仍然读标准输入**；放在 `while read -r size sha256 root name; do … done < "$INPUTS"` 的循环体里，第一次 `sha256sum` 就把钉住表剩下的行吃光，于是**只有第一行数据被核对**。复现：3 行文件同构造，裸调用只处理 1 行，加 `</dev/null` 处理 3 行。后果：`viewmodel-apply-check.sh` 第 0 段读 `PINNED=1`（期望 2），matcha 行从第二行加入钉住表起**从未被核对过**；`viewmodel-animation-check.sh:444` 同一构造同一症状。同类站点（均为 `while read` 循环体，未修）：`viewmodel-companion-check.sh:294`、`viewmodel-presentation-check.sh:204`、`viewmodel-range-check.sh:306`、`viewmodel-robustness-check.sh:240`、`verify-audio.sh:318/527/779`、`verify-audio-content.sh:162`、`hud-check.sh:971`（`cd "$dir" && sha256sum`）。修法：调用处补 `</dev/null`（一行，语义不变）。已修：`viewmodel-apply-check.sh:295`、`viewmodel-animation-check.sh:444`。 |

| 15 | （本轮新发现）`viewmodel-apply-check.sh` 的 m2 变异打向 `native/src/native_renderer.cpp`，而 14 个 HLSL 程序已在 `3218e9bd`「hold the 14 HLSL programs apart from the state that compiles them」搬到 `native/src/native_renderer_shaders.cpp` | **CONFIRMED** | 2026-11-20 首次跑通前向之后跑 `--mutation`（job `bash-0cd1334dc38a4135`）：m1 三条 OK、m3 三条 OK、m4 OK（`the eye is inside the model: distance=6.5735 against radius=29.8797 (the gate pins 2.2x)`）、还原后 bind 帧逐字节一致 OK，**唯一红是 `FAIL m2 did not apply to native/src/native_renderer.cpp`** —— 那行的语义是「这个缺陷已不在该文件」，不是「修复写错了」。修法：目标改 `native/src/native_renderer_shaders.cpp`；该文件有**三个**同构造 `skinByBone`（两个世界/实体 program 读 `boneColumns`、一个 ViewModel program 读 `viewModelBones`），`patch-source.py` 只替换一处且拒绝歧义 needle，所以 needle 取**整个函数**并以 `viewModelBones` 锚定，避免打到世界 program 上导致被测帧不变、变异被误报为「未检出」。dry-run 实测只改 `:1055-1059` 五行。同批还修了 `MUTATION_SOURCES`（原缺 `native_renderer_shaders.cpp` 与 m4 的目标 `native_renderer_viewmodel.cpp`，中断时孤儿备份扫描不会还原这两个文件）。 |

统计：CONFIRMED 9 条（#1、#7、#8、#9、#10、#12、#13、#14、#15），PARTIAL 4 条（#3、#4、#5、#6），REFUTED 2 条（#2、#11）。

## §2 交付需要改口径的地方

1. **#2 的「假红」结论作废**：那条录制 2026-11-15 已重钉（脚本自带注释），今天跑它不会因缺失而红；
   剩下的是可移植性问题（`E:/` 硬编码），归到和 #8/#9/#10 同一类。
2. **#11 作废**：注释与代码一致（`main.cpp:481-505`）。若将来 `choosePlayerAnimationSequence` 真的加了家族参数，
   那时才需要同步注释。
3. **#3 的严重度降级**：`viewmodel-animation-check.sh` 的 `rc=1` 若今天仍出现，原因不在缺失录制，必须重跑取真实原因；
   「钉死录制缺失」是 2026-11-14 的历史读数。
4. **#4/#5/#6 改叫「脆弱断言」**（D 类，不是 B 类）：它们绑定固定 fixture，换 demo/TF2 版本才红，
   当前不构成过期前提；改法（同 run 基线对比）仍有价值。

5. **#12/#13/#14 已修（2026-11-20，用户批准后）**：`weapon-body-check.sh` 改钉 POV 录制（146,853,666 B / `14b4943a…`）、
   拆分 `CAPTURE_TICK=3000` 与 `PROBE_TICK=5202`、section 5 改「每个 distinct model 各建一次」→ run4 `WEAPON-BODY=PASS`（RC=0）；
   `viewmodel-apply-check.sh` 第 0 段加三根映射 → `pinned recordings verified = 2`，三处红按机制修（`</dev/null`/
   同 run 基线对比/`--dilate 1`）→ `VIEWMODEL-APPLY=PASS (42 assertions, 0 failures)`。
   #14 的修法只覆盖被测两处，其余 8 个站点未动。
   #15 的修法已验：`--mutation` 重跑（`.scratch/apply-mutation2.log`，job `bash-7c4ce22ee6f642f6`）
   `VIEWMODEL-APPLY=PASS (14 assertions, 0 failures)`、RC=0，其中
   `m2 applied against the bind ViewModel mask: 18177 px changed, 7747 outside a 48554 px ViewModel mask`（变异被抓到）、
   `m4 the eye is inside the model: distance=6.5735 against radius=29.8797 (the gate pins 2.2x)`，
   还原后 `the restored source redraws the pre-mutation bind frame = 085cbb1788969505…`（源码确实回到原样）。
   这条读数是 **m4 修复的整门禁验证**：前向 42 条断言 + 四个变异全部按设计动到本门禁读的数。

## §3 本轮自己加的增量（校准样本之外）

- **m4 已修（未提交）**：`viewmodel-apply-check.sh:717/718/724` 的 patch 目标 `native/src/native_renderer.cpp`
  → `native/src/native_renderer_viewmodel.cpp`（mutation 仍 `2.2f` → `0.22f`；目标行 `native_renderer_viewmodel.cpp:543`
  `  const float distance = radius * 2.2f;`，两个空格缩进）。整门禁验证约 40 分钟（6 次捕获 + 4 次 mutation），
  记在 `docs/TASK-LIST-2026-11-19.md` 第 15 条。
- **`viewmodel-contract-check.sh:363-364` 注释已过时**：注释写「The two ticks differ by 10252」，
  现在的口径是 T63-F1 的两套 tick 空间（demo entry tick 从 1 起、signon 也占号；snakewater signon entry=10252、
  signon 块携带的 server tick=10320）。同一条红（capture point `0 != 7`，`:714` premise `CAPTURE_SCENE_TICKS=10252` vs 实测 10320）
  是同一处口径的产物，改门禁需用户批准。

- **#12（本轮实跑）**：`weapon-body-check.sh` 与 D12/#1 同根因，今天整门禁不可运行（第 0 段即红，后续段 rc=2 全是级联）。
  它不是「bug 在门禁里」，而是「门禁的语料没了」——修法与 #1 相同：换现存录制并重抓该门禁的全部基线（6 段 + 4 个 mutation）。
- **#13（本轮实跑）**：`viewmodel-apply-check.sh` 第 0 段在 N01（2026-11-15）把 POV 槽重钉到 pl_upward_f12 录制后失配：
  输入文件 root 用 `archive-pov`/`testdata`，脚本仍只认 `steam`。修法二选一——(a) 脚本按 root 建路径映射
  （`archive-pov` → `E:/demo测试/pl_upward_f12/POV`，`testdata` → `D:/TF2_Demo_Player/testdata/demos`），
  与 `viewmodel-animation-check.sh` 的读法对齐；(b) 输入文件补一行 `steam` root 的现存录制。两者都动门禁语义，需批准；
  在批准前，m4 的整门禁验证处于「无法执行」，只能靠 6 个捕获 + 4 个变异的人工等价检查替代。

## §4 建议修复顺序（按代价/收益）

1. **P0 · 只解真实红**：#1（换现存 `testdata/demos/` 语料并重抓基线，中）。
2. **P1 · 可移植性一批做完**：#2/#8/#9/#10 统一成 `TF2_ROOT` / `TF2_TESTDATA` / `TF2_DEMO_*` 环境变量 + 缺文件时显式命名失败或 skip（低）。
3. **P2 · 脆弱断言**：#4/#5/#6 改成同 run 基线对比（低，但会动断言语义，需要证据说明不是放宽）。
4. **P3 · 注释/行号**：#7 改成符号引用（极低）。

## §5 仍未覆盖 / 下一步

- 交付只实读了 8 个高频脚本；A 类（`patch_source` 指向已拆分文件）最值得一条命令普查：
  `grep -rn "native_renderer\.cpp\|main\.cpp" *.sh | grep patch_source` —— 本轮只修了已知的 m4，
  `native_renderer.cpp` 拆分后（viewmodel/frame/world 三个 TU）同类债务可能还有。
- 全链 `verify-all.sh` 与需要私有 demo/GPU 的脚本不在本轮范围。
