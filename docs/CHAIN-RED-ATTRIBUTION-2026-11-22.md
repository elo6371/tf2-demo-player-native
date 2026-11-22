# 30 步 verify-all 链的红步归因与真问题清单（2026-11-22）

本文件是 `docs/PROJECT-OVERVIEW-2026-11-22.md` §7「验收现状」的配套记录：把本轮 30 步链上**每一条红**判成
A（机器/环境：输入缺失、宿主工具链、陈旧目标文件）／B（门禁过期或门禁脚本自身有缺陷）／C（真正的分支回归），
并把需要改的东西写成可执行的条目。

- 链：`verify-all.sh`，2026-11-22 14:01:16 起、15:35:44 止（约 94 分钟），log `.scratch/lead-verifyall6/verify-all.out`（731 行）。
- 树：`task/merge-all`，head `cfdd96c8`（其上 `7ec173a0` 是六卡合并提交），base `390a7c76`。
- 结论行：`VERIFY=FAIL (30 steps, 1 skipped: WORLD-GEOMETRY-CHECK)`。
- 判定来源：只读取证代理 `reds`（`.scratch/reds/REPORT.md`，84 行）＋ 主代理独立复核（第 1/3/5/7/9/22/26 步逐条两端比对）。

## 0. 一句话结论

**30 步里只有 1 条是真回归（第 26 步），其余 29 条是这台机器缺输入（15 条 A）、门禁自己过期或有缺陷（5 条 B）、
以及变异套件把半编译的旧目标文件留在库里，导致其后 8 个步骤读到被污染的二进制（A 的主体）。**
链之所以是 `VERIFY=FAIL`，主因不是分支代码坏了，而是门禁与机器状态——但这恰恰是最该修的那一类问题，
因为它让「绿」失去意义。

## 1. 全表（30 行，一行不少）

| 步 | 门禁脚本 | 判定 | 一句话依据 | 需干净重建复跑 | 需真修 |
|---:|---|:--:|---|:--:|:--:|
| 1 | `build-cmake.sh`，pin 在 `verify-all.sh:374-375` | **B** | 构建本身干净（`cmake_build_rc=0 errors=0 warnings=0 exe_count=55`），`add_executable(` 数也是 55，但 pin 仍写 `^exe_count=53$`，落后两个 target | 否 | 是：把 pin 的 53 改成 55 |
| 2 | `check-oracle.sh` | PASS | `CENSUS=PASS (9/9 demos at zero)`、`COVERAGE=PASS (9/9 demos, every observed message type decoded)` | 否 | 否 |
| 3 | `entity_message_fixture_probe.exe`，pin 在 `verify-all.sh:403` | **B** | 探针确实打印了要的值（`fixture_pass_count=58`、`fixture_failures=0`），但 pin 用 `^fixture_failures=0$` 去匹配一个 CRLF 文件（`git ls-files --eol` 报 `i/lf w/crlf`），`$` 永远匹配不上 | 否 | 是：先 `tr -d '\r'` 或用探针退出码判 |
| 4 | `check-oracle.sh` | PASS | `ORACLE-GATE=PASS`、`ORACLE=PASS` | 否 | 否 |
| 5 | `check-oracle.sh --sample 40` | **A** | `FATAL: 14 of 24 pinned demos are missing`（首条 `...tf/demos/73.dem`）：标定语料不在本机 | 否 | 否（是输入：补 demo 或重钉） |
| 6 | `oracle-recording-types.sh` | PASS | 九行 OK，`compared=9/9`，`ORACLE-RECORDING-TYPES=PASS` | 否 | 否 |
| 7 | `check-probe-output-additive.sh:312,316` | **A** | 本机 `diff` 是 WinuxCmd 垫片（`diff (WinuxCmd) 1.2.1`），打不开 bash 进程替换给的 `/dev/fd/63`，冻结侧读成 0 字节，于是打印 `0a1,18`；冻结文件本身正确（18 行、无 CR），提交的转录在 390a7c76 与 7ec173a0 **两端都是 PASS** | 否（单独跑必现，整链里才出现，见 §3.3） | 是：`tr -d '\r' < "$ADDED" > "$TMP/frozen.clean"` 后 diff 两个实体文件 |
| 8 | `mutate.sh` | **A** | `mutation_red=17 hold=2 green=9 broke=1 restored_identical=2`（pin 是 49/6/0/0/6）。变异套件跑在**仍带着上一轮变异的目标文件**上，因为 `cp -p` 还原把源码 mtime 回退到比它的 .obj 更旧，nmake 因此从不重编；`broke=1` 是 m10 的 `false &&` 撞 `/WX` 的 C2220 | 是 | 是：还原后 `touch` 源码或删掉对应 .obj |
| 9 | `census-negative-test.sh`，pin 在 `verify-all.sh:517-521` | **A** | 与第 5 步同源：十四条 `FAIL pinned demo is not in the corpus`，随后 `FAIL baseline not PASS`，十个变异臂全部 `FAIL verdict stayed green`；`CENSUS-NEGATIVE=FAIL (pinned_reports=24)` | 否 | 否（是输入） |
| 10 | `oracle-trajectory-check.py` | **A** | `FATAL: probe reported an empty live window`、`TRAJECTORY-ORACLE=FAIL (rc=1 compared=0 mutation_rc=1)`；探针链接的 `tf2_core.lib` 里还带着 m7/m9 变异 | 是 | 否（代码不用改，要改的是产出它的构建卫生） |
| 11 | `history-coverage-check.sh` | **A** | `FAIL sampled=0 but 512 were asked for`、`FAIL 0 retained slots hold only 0 distinct ticks`；活二进制回答 `"historyCoverage":false,"historyWorstGap":23040`，与 `evidence/mutation/recording.m9.txt` 逐字相同，即 m9 仍被链接进去 | 是 | 否 |
| 12 | `weapon-world-model-check.sh` | **A** | `FAIL bagel pathFromWorld = 0, expected 32`、`bagel known = 22, expected 33`、`bagel worldModelRequests = 0, expected 32`；活探针打印 `model_precache_entries=0 ... asset_model_path_from_precache=0`，与 `evidence/mutation/bagel.restored.txt` 逐字相同，即 m7 仍在 | 是 | 否 |
| 13 | `observer-focus-check.sh` ＋ `--mutation` | **A** | 所有读数都是 `observer-focus at tick=... status=before-window resolved=0`，然后 `FATAL: expected 5 observer rows for entity 18, got 1`；fixture 段 11/11 全绿，是 POV 窗口在这份输入上没有 observer 行，`--mutation` 臂继承同一个空集 | 是 | 否 |
| 14 | `slot-freshness-check.sh` | **A** | `FATAL: missing input: ...tf/demos/autorecord_2026-07-02_13-26-46.dem`，`SLOT-FRESHNESS=FAIL (assertions_ok=0, POV chosenStale=none)`：钉住的 POV 录音本机没有 | 否 | 否（是输入） |
| 15 | `source-tv-coverage-check.sh` | **A** | 九行 `FAIL A1 recording type <demo>: header=POV stream=SourceTV want=SourceTV`，而同一行的 A2 in-stream 信号全绿（`hltv=1 replay=0 flag=1`）、A3/A4/A5 也过；`HEAD:evidence/verify/15-source-tv-coverage.txt` 的九行 `OK A1 ... header+stream=SourceTV` 来自**逐字相同**的门禁脚本，所以动的是陈旧探针（mtime 14:15:45）里的 header 分类器，不是 demo | 是 | 否 |
| 16 | `frame-capture-check.sh` | PASS | `FRAME-CAPTURE=PASS (assertions_ok=18)` ＋ 变异 PASS；BMP 审计 `dataOffset=54`、1264x681 bpp=24、`imageBytes == stride*height` | 否 | 否 |
| 17 | `resource-reachability-check.sh` | PASS | `RESOURCE-REACHABILITY=PASS (assertions_ok=12)` ＋ 变异 PASS | 否 | 否 |
| 18 | `idle-spin-check.sh` | PASS | `IDLE-SPIN=PASS (assertions_ok=11)` ＋ 变异 PASS；1 ms 等待读 1.00，在 4 ms 地板以下 | 否 | 否 |
| 19 | `entity-property-lookup-check.sh` | **A** | `FAIL bagel sweep is cold (every player already has a request) = 2048, expected 0`＋pov 同款。这个数在普通与 `--mutation` 转录里完全相同，而 `resolverTransforms` 却在 10→15、1→10 变动，说明变异改的代码不产生它：2048 是 `native/src/demo_header_entity.cpp:337` 的 `summary.projectileTrackSlotGeneration.resize(2048u)` 容量被当扫描计数报出来，经陈旧探针（14:35:02）读出 | 是 | 部分是：把「容量」当「扫描数」报的计数器要改 |
| 20 | `entity-material-check.sh` | PASS | `ENTITY-MATERIAL=PASS (assertions_ok=24)` ＋ 变异 PASS | 否 | 否 |
| 21 | `animation-availability-check.sh` | PASS | `ANIMATION-AVAILABILITY=PASS (assertions_ok=23)` ＋ 变异 PASS | 否 | 否 |
| 22 | `weapon-animation-check.sh` | **B** | 65 OK 对 67 pin。缺的是第 3 段的 `witness frame advances by the last tick` 与 `frames under a still camera differ with playback (distinct)`：门禁在 `weapon-animation-check.sh:775-776` 用 `tail -1` 取见证者，而这份 demo 最后一条 `scene: animwitness` 是 `models/player/scout.mdl ... seq=212 frame=0/1` 的单帧 clip，`:841` 的 `assert_ge ... 1` 因此不可能满足——同一 trace 里还有 1669 条带真实游标的 animwitness（`frame=41/45`、`frame=12/49`）。demo 字节**不是**差异：snakewater 51834617 B / sha256 `308cc145...607f`，与仓内所有 pin 一致 | 是 | 是：取「最后一条帧数非零的见证者」而不是最后一行 |
| 23 | `viewmodel-animation-check.sh` | **A** | 转录只有一行：`VIEWMODEL-ANIMATION=FAIL (another viewmodel-animation-check run owns .scratch/viewmodel-animation.lock)`。`.scratch/viewmodel-animation.lock` 是前一晚（2026-11-21 22:52）留下的**空目录**（`:156-158` `mkdir` 拿，`:215` trap 删） | 否，删锁重跑即可 | 否，或让锁能识别陈旧 |
| 24 | `viewmodel-companion-check.sh` | **B** | 转录只有一行：`viewmodel-companion-check.sh: line 118: ROOT: unbound variable`。脚本 `:71` 就 `set -uo pipefail`，`:118` 展开 `TRACE_PRODUCT_DIR="$ROOT/$TRACE_DIR"`，而 `ROOT` 到 `:159` 才赋值；链在 `verify-all.sh:1151-1152` 传的是相对路径，于是 `set -u` 在任何断言之前就炸 | 否 | 是：把 `ROOT` 的赋值提到 case 之前 |
| 25 | `player-animation-check.sh` | **A** | `PLAYER-ANIMATION-CHECK=FAIL (rc=1 ok=27 expected 233)`。每个职业都倒在同一个产品拒绝上：`FAIL player-animation-test reason=no-animating-sequence detail=no sequence with four distinct quarter-cycle poses`——模型从 VPK 出（`tf/models/player/scout.mdl` 在磁盘上不存在）；三个负控（错 tf-root、错 model、越界 sequence）全 PASS。门禁与 `native/tools/model_pose_probe.cpp` 与 base 逐字相同 | 是 | 否 |
| 26 | `player-animation-pixels.sh` ＋ `t61-pixels.py` | **C** | 唯一一条不是环境的红。`MASK pixels=262681` 对提交的 3156（83 倍）、`2.24%` 对 `68.00%`；同 tick 的 entity-id 帧证明这些像素确实是 entity pass 画的（见 §2），所以不是哨兵误判，是 entity pass 真的覆盖了 30.5% 画面 | 是，且这是唯一值得按 C 深挖的 | 是：找出 entity pass 在 30.5% 画面上画了什么 |
| 27 | `spectate-check.sh` | **A** | `SPECTATE-CHECK=FAIL (rc=1 ok=68 floor 93)`。20 条红全是同一个量：`deaths = 1, expected 3`、`handovers = 1, expected 3`，同 demo 同窗口同 `frames=1936`，而 `checkpoint_lag_mean` 从 341.3 爆到 2663.3（worst 1031→7157）。门禁与 `tools/spectate_probe.cpp` 与 base 相同，探针链接同一个陈旧 `tf2_core.lib`（14:46:20、43991760 B），lag 数字直接来自 m9 变异后的 checkpoint 归档 | 是 | 否 |
| 28 | `world-geometry-hole-check.sh` 再 `world-sky-gate-check.sh`（一步两门禁） | **A** | 几何半 `SKIP`：`no recording at ...autorecord_2026-07-02_13-26-46.dem`、`SUMMARY checks=0 fail=0 skip=1`、`VERDICT world-geometry-hole: SKIP`。能跑的天空半 `WORLD-SKY-GATE=PASS (17 assertions)`、0 失败。没有回归，这一步按构造就是 0 断言 | 否 | 否（是输入：要那份录音或重钉） |
| 29 | `material-coverage-check.sh` 再 `sultry-translucent-atlas-check.sh` | **B**（含一条 C 味） | 材料半死于同一份缺失 POV 录音（`MATERIAL-COVERAGE-CHECK=FAIL (rc=1 ok=0 expected 100)`）。图集半 `ok=122 expected 139`：八行读 `got ''`，因为 `sultry-translucent-atlas-check.sh:102` 的 `field()` 只 grep **第一条** `gpu: world isolation` 行，而 `native/src/main.cpp` 有三个发射点（`:9389`、`:9557` 不带 key、`:10743` 带 key），key 根本没进比较。另 `pixels changed by the rescue: got '11530' want '84965'` 与五行 `pixel hole ... material`（要玻璃得到砖/金属）是真实像素差异 | 是 | 是：改选行；五行像素孔要单独查 |
| 30 | `review-workspace-check.sh` | PASS | `REVIEW-WORKSPACE-CHECK=PASS (186 assertions)`、`assertions_bad=0`、`RC=0` | 否 | 否 |

统计：**PASS 9 步**（2、4、6、16、17、18、20、21、30；第 28 步几何半 SKIP、天空半 PASS）；
**A 15 步**（5、7、8、9、10、11、12、13、14、15、19、23、25、27、28）；**B 5 步**（1、3、22、24、29）；**C 1 步**（26）。

## 2. 第 26 步：唯一一条真回归，怎么证明的

两端都复核过：`git show HEAD:evidence/verify/26-animation-pixels/pixel-check.txt` 与 390a7c76 的副本逐字相同
（PASS、`MASK pixels=3156`、`68.00%`），所以 pin 是诚实的，`player-animation-pixels.sh` 与 `t61-pixels.py` 也没被分支改过。

对 `evidence/verify/26-animation-pixels/*.bmp` 做颜色普查：

- 今天这帧是 1264x681 = 860784 px，只有三种颜色：`(89,158,97)` 598043、`(255,0,255)` 262681、`(94,94,136)` 60。
  提交时的抓帧是 `(89,158,97)` 668469、`(14,18,22)` 188818、`(255,0,255)` 3497。
- 今天的哨兵是**上半屏连续色带**（第 0-470 行每行约 500-600 px，每行 2-7 段），不是人体轮廓；
  提交时是**人体色块**（320..388 行、x 434..635）。
- **决定性交叉验证是同 tick 的 entity-id 帧**（`entityid.bmp`）：`(89,158,97)` 598043、`(255,255,255)` 257073、
  `(1,0,0)` 5608、`(94,94,136)` 60，且「白 ∪ 红 == coverage 哨兵集合」为真（`白 ∩ 哨兵 = 257073`、`红 ∩ 哨兵 = 5608`）。
  即这 262681 px 在 coverage 臂（`envFeatures.x = 10.0`）画成品红、在 id 臂（`envFeatures.x = 11.0`）画成实体颜色，
  默认实例色就是白——**两个视图里都是 entity pass 画的**。
- 真正带身体 id 的只有 5608 px：`MASK distinct-ids=1 pixels=5608`、`SUBJECT id=1 ent=5 model=models/player/demo.mdl state=moving seq=147 label=run_PRIMARY`、`VISIBLE players=1 props=0`、`DRAWN 2`。
  提交时是 `LEDGER lines=96 distinct-ids=96`、`props=5`、`MASK distinct-ids=6 pixels=2705`。
- 于是两条失败断言是**同一个原因**：掩码溢出 `total // 4 = 215196`，且 on/off 差异被约 257k px 完全静止的天空/世界摊薄，68.00% 才塌成 2.24%。

下一步只需回答一行：在 `--cosmetics` 默认关闭（`native/src/main.cpp:3415-3416`）的前提下，
`context_->DrawInstanced(range.vertexCount, range.instanceCount, 0, range.instanceStart)`
（`native/src/native_renderer_frame.cpp:1823` 与 `:1949`，range 在 `native/src/native_renderer_entity.cpp:803-872` 构造）
里**哪一段 instance range 现在覆盖了 262681 px**。第二候选（约一小时可排）：`native/src/native_renderer_frame.cpp:1732`
的实体常量上传以 `frameConstantsValid && (worldDebugView_ != 0 || frameConstants.lightmapFeatures.x != 1.0f)` 为条件，
每个调试视图都会重传，而世界 pass 可以经 `TF2_NATIVE_WORLD_VIEW`（`:1242-1245`）读到另一个活视图。

## 3. 解释 A 列的三台机器

### 3.1 陈旧目标文件（最大的一台）
`mutate.sh:55-75` 用 `cp -p` 备份、`restore_entry()` 用 `cp -p` 还原（`mutate.sh:73`），把源码 mtime 退回原值——
比它编译出的 .obj 更旧。nmake 于是无事可做，`restore()` 里那次 `rebuild`（`mutate.sh:168-181`）是空转。
本机实测：`cp -p` 精确复制 mtime，普通 `cp` 给 now。

活二进制证明污染还在库里：`presentation_probe.exe` 打印 `"historyCoverage":false,"historyWorstGap":23040`，
那是 `evidence/mutation/recording.m9.txt`，不是干净的 `recording.fixed.txt`（`true`/`4096`）；
`entity_protocol_probe.exe` 打印 `model_precache_entries=0 ... asset_model_path_from_precache=0`，
那是 `evidence/mutation/bagel.restored.txt`，不是 `bagel.fixed.txt`；`entity_message_fixture_probe.exe` 打印 `fixture_failures=1`（干净为 0）。
`native/build-nmake/tf2_core.lib` 时间 14:46:20、43991760 B，里面 `demo_header_strtab.cpp.obj`（14:15:11）与
`demo_header_entity.cpp.obj`（14:15:38）都编译于变异窗口内，而它们的源码被回退到 13:51:18。
`weapon-animation-check.sh:345-377` 用同样的还原方式，所以这是整套门禁的共性。
第 8、10、11、12、15、19、25、27 步读的都是链接了这个库的二进制。

### 3.2 机器输入缺失
POV 录音 `autorecord_2026-07-02_13-26-46.dem` 没了（第 14、28、29 步），标定语料 24 份里 14 份没了（第 5、9 步）。
第 28 步自己的 SKIP 文本写明别的图不能顶替，`docs/T85-SUPPORT-SCOPE.md:35,:50` 也已记录「默认路径随用户 TF 安装一起丢了」。

### 3.3 WinuxCmd 垫片
`diff`、`comm`、`grep`、`awk`、`sort`、`head`、`tail`、`paste`、`wc`、`cut`、`find`、`tr` 全部解析到 `.../winuxcmd/usr/bin/`。
第 7 步就是它咬的地方：垫片打不开 bash 进程替换递过去的 `/dev/fd/63`（本 MSYS 根本没有 `/dev/fd`），
而在**整链环境里**它甚至把参数弄成 `0`（在门禁的逐字节副本里用 `bash -x` 复现：`+ diff -q .../added.new /dev/fd/63`
紧接 `line 1: 0: No such file or directory`）。同一条命令在干净 shell 里 30 次全过，说明失败与门禁运行时的状态有关。

同一族、留给后续修复清单：把文件经进程替换交给垫片的位置还有
`class-pixels-check.sh:340-341`、`hud-check.sh:1033-1034`、`install-lifecycle-check.sh:376`、
`install-readiness-check.sh:461`、`census-negative-test.sh:116`、`check-probe-output-additive.sh:312,316`；
`comm` 也是垫片，两处 `comm` 站点同样在列。

### 3.4 对探针输出做「行尾锚定」的 grep
`verify-all.sh:374`、`:375`、`:403` 是唯三的 `$` 锚定 grep；真正咬人的只有 `:403`，因为 fixture 探针写 CRLF。
工作区里 `evidence/` 下 244 个文件是 `w/crlf` 而索引是 `i/lf`（`git ls-files --eol evidence/ | grep -c 'w/crlf'`），
`core.autocrlf` 未设，所以 CR 来自 Windows 探针的文本模式，不是 checkout 规范。

## 4. 要真修的六件事（按性价比排序）

1. **第 26 步（C）**：找出 entity pass 为什么覆盖 30.5% 画面。这是唯一一条真回归，别被其它 28 条噪声盖住。
2. **`mutate.sh` 的还原卫生**：`restore_entry()` 之后 `touch` 源码或删掉对应 `.obj`。这一条一改，第 8/10/11/12/15/19/25/27 步的「红」才有诊断价值。
3. **`viewmodel-companion-check.sh` 的 `ROOT` 顺序**：把 `:159` 的赋值提到 `:118` 之前。目前这一步**在任何断言之前就炸**，等于常年没跑。
4. **`verify-all.sh:375` 的 `exe_count` pin**：53 → 55（`ls native/build-nmake/*.exe` 与 `add_executable` 都是 55）。
5. **`verify-all.sh:403` 的 CRLF 锚**：先 `tr -d '\r'`，或用探针退出码判。
6. **`weapon-animation-check.sh:775-776` 的见证者选择**：取最后一条帧数非零的 `animwitness`，而不是 `tail -1`。
   连带 `sultry-translucent-atlas-check.sh:102` 的选行（三个 `gpu: world isolation` 发射点，要挑带 key 的那一个）。

外加两件「环境侧」：补回 POV 录音与 14 份标定 demo（或正式重钉），以及把进程替换交给 WinuxCmd 垫片的那 8 处改成实体文件。

## 5. 这份记录不能证明什么

- 它不能证明分支代码是对的：它只证明**这 30 条红里 29 条不该按「代码坏了」去读**。
- 第 26 步的 C 判定基于同 tick 两帧的颜色交叉验证，不是根因；根因要等 §2 末那一行问题被回答。
- 链跑完时工作区是脏的（176 条，全是重新生成的 evidence）。**一次 verify-all 运行永远会让工作树变脏**——
  这是既有设计（`evidence/final` 被第 2 步覆盖等），任何「工作区干净」的说法都必须先还原或刻意提交这些文件。
