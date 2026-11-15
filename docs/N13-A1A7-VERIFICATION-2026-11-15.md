# N13 A1–A7 命令语义验收（step-1 实现态）

- 角色：ui-d（实例3「D 交互」，task-3 卡 U1 子片）
- 日期：2026-11-15
- 仓库：D:/TF2_Native_Worktrees/t105-e5，分支 candidate/e5
- 开工 HEAD：cf625b42156aac1714f99c72a0d28ce0d7698014（subject: docs(N05): the stream-output plan, with the control sample that gates it）
- 成文时 HEAD：6f3aa8fc（subject: feat(N07): the four asset controls, the water stage, and a read chain that can say read-failed）—— 区间内他人提交，未触及 native/ 的 follow 代码路径
- 被测 exe：native/build-nmake/tf2_demo_native.exe，3,017,216 B，sha256 7a8da164de67553281065ef76638c135a49baf537cb5027865bca4f39f3992ff（短 7a8da164），mtime 2026-11-15 21:34。整张卡期间该 exe 未变，全部读数归属于它。
- 证据根目录：D:/_shared/EXEC-2026-11-15/INST3/

本报告只验收 step-1 落地的那一层（命令入队 / 状态生效 / 相机决策记录 / trace 发布）。相机优先级本体（POV 眼 > pin）不在本卡范围，见 §14。

## 0. 复跑方式

~~~bash
cd /d/_shared/EXEC-2026-11-15/INST3
PY=C:/Users/Administrator/.dsh/dsh-runtimes/dsh-primary-runtime/dependencies/python/python.exe
$PY n13_analyze.py *.trace > analyze.json     # 逐条 label 解析，写 follow_parse_failures
bash hash-all.sh                             # 重算本报告 §15 的产物哈希
# 单次产品运行（须串行锁）：
cd /d/TF2_Native_Worktrees/t105-e5 && bash tools/lockrun.sh inst3-n13 -- bash /d/_shared/EXEC-2026-11-15/INST3/n13-matrix.sh <mode>
~~~

复跑只读盘上 24 份 trace 与 3 份 camera-script；除显式重跑矩阵外不启动产品 exe、不重新采集。矩阵脚本 n13-matrix.sh 的 mode 为 bagel / bagel2 / bagel3 / bagel4 / stv / stv2 / a7 / a7b。

## 1. 卡级结论

| 断言 | 判定 | 分母与依据 |
|---|---|---|
| A1 select 语义 | **PASS** | a1-pos / a1-neg（bagel）+ a1-neg-stv（STV），正负控各一。§4 |
| A2 幂等选人 | **PASS（写法需限定）** | a2-pos / a2-neg（bagel）+ a2-neg-stv（STV）。限定量见 §5。 |
| A3 next/prev 环绕 | **PASS（STV）／NOT ASSERTABLE（bagel）** | a3-next / a3-prev（STV）；bagel 只有 1 个可选目标，a3-next-bgl / a3-prev-bgl 不能作证。§6 |
| A4 固定目标死亡 | **PASS（状态与计数）／FAIL（reason 词元）** | a2-neg-stv / a3-prev 提供 pinnedDeaths=1 与 `state=dead`；`reason=pinned-dead` 结构性不可达。§7、§11 |
| A5 serial 复用 / 越界 | **UNSUPPORTED（规格形态不可达）** | a5-oor / a5-maxidx / a5-noslot / a5-max2047 / a5-bogus。§8 |
| A6 名字解析 | **PASS（枚举值形态）** | scout12（无 userinfo 侧）+ scout16（有 userinfo 侧）。§9 |
| A7 跨 tick 切换 | **PARTIAL** | a7-dead（STV，seek 后重新发布，可读）／a7-pov（POV，seek 后不重新发布，不可读）；serial 半侧不可断言。§10 |
| A8 历史未干扰 | PASS（继承，不重测） | docs/N13-STEP1-IMPL-2026-11-15.md 的 a8 证据链，本卡复核读法未变。§13 |

三句话把卡级说清楚：

1. **A1 / A2 / A3 / A6 是干净的 PASS**：命令入队、幂等、环绕、名字解析四条在 trace 上都有具名正负控，解析失败计数为 0。
2. **A4 只有一半**：产品确实发现了「被钉住的目标死了」并把 pin 清掉（`pinned=0 valid=1 state=dead`、`pinnedDeaths=1`），但它把 reason 打印成 `ok` —— 因为 `FollowReason::PinnedDead` 在整个 native/src/main.cpp 里**从未被赋值**。这是代码事实，不是偶发运行，因此写 FAIL 而不是「不稳定」。
3. **A5 与 A7 的失败形态不同**：A5 是规格里的三个形态在本 build 里**根本没有输入源**（越界被 CLI 上限吞掉、no-slot 被 isPlayer 门抢先、index-reused 需要 serial 而 serial 恒为 0），记 UNSUPPORTED；A7 是**看得到但只在一种 demo 上看得到**，记 PARTIAL。两者都不许写成 PASS，也不许写成「修一个 bug 就好」。

## 2. 身份

### 2.1 exe 与仓库

| 项 | 值 | 证据 |
|---|---|---|
| exe sha256 | 7a8da164de67553281065ef76638c135a49baf537cb5027865bca4f39f3992ff | identity.txt |
| exe 字节数 | 3,017,216 | identity.txt |
| exe mtime | 2026-11-15 21:34 | ls -l native/build-nmake |
| 开工 HEAD | cf625b42156aac1714f99c72a0d28ce0d7698014 | git rev-parse HEAD（claim 时） |
| 成文时 HEAD | 6f3aa8fc | git rev-parse HEAD |
| 工作树是否被我改动 | 否。git status --porcelain 只有他人文件（.gitattributes、N-STATUS、PROJECT-NEXT-TASKS、PROJECT-ROADMAP-HANDOFF 及若干未跟踪 docs） | git status --porcelain |
| tools/audit-tree.sh | VERDICT: OK（no forbidden writes, line endings intact） | 提交前复跑 |

**exe 在卡期内被他人三次重建**（本卡开工前是 6f3e64cd / 3,016,704 B；doc 记 2,991,104 b4a2355e；N12 采集用 2,988,544 cfcdbe5d）。本报告所有读数都属 7a8da164；与其它哈希的读数不可混用。

### 2.2 输入 demo

| demo | 路径 | 字节 | 用途 |
|---|---|---|---|
| koth_bagel_rc11 POV | E:/demo测试/koth_bagel_rc11/POV/testdemo111.dem | 5.2 MB 级（未在本次单独复量） | A1/A2/A5/A6 负侧；单次运行 ~21–25 s |
| pl_upward_f12 STV | E:/demo测试/pl_upward_f12/STV/a9630c6557468b24f0b8c2e7dab38ed8_match-20261030-2145-pl_upward_f12.dem | 75,663,754 | A3/A4 正侧、A6 正侧、A7；单次运行 90–126 s |

bagel POV 只有一个可用跟随目标（实体 1 = POV 录制者），其余实体全部 `reason=not-observable`。这是 demo 属性，不是产品缺陷。

### 2.3 24 次产品运行

| label | demo | 关键参数 | 用时 | trace 字节 |
|---|---|---|---|---|
| calib | bagel | capture-tick 400 speed 16 | 20,876 ms | 179,367 |
| scout12 | bagel | --follow-target 1..12（单进程） | 22,755 ms | 203,767 |
| scout16 | STV | --follow-target 1..16 | 126,178 ms | 8,176,891 |
| a1-neg / a1-neg-stv | bagel / STV | 无 follow 参数 | — | 200,503 / 9,610,915 |
| a1-pos | bagel | --follow-target 1 | — | 180,407 |
| a2-pos | bagel | --follow-target 1 两次 | — | 214,212 |
| a2-neg | bagel | --follow-target 1 然后 2 | — | 215,342 |
| a2-neg-stv | STV | 同上 | — | 8,964,010 |
| a3-next / a3-prev | STV | 钉 9 后 next / prev | — | 9,127,059 / 9,330,169 |
| a3-next-bgl / a3-prev-bgl | bagel | 钉 1 后 next / prev | 25,386 / 25,378 ms | 139,257 / 139,254 |
| a4-unfol / a4-notfol / a4-pov | bagel | unfollow / 无 pin 时 unfollow / back-to-pov | — | 200,667 / 173,357 / 125,364 |
| a5-oor / a5-maxidx / a5-noslot / a5-max2047 / a5-bogus | bagel | 5000 / 2048 / 2000 / 2047 / --follow-action bogus | — | 200,342 / 139,013 / 234,849 / 139,234 / 166,468 |
| a7-seek | STV | follow 9 + seek 20000 | — | 8,020,864 |
| a7-dead | STV | follow 3 + tick 12000 / seek 18500 / frames 200 | 90,231 ms | 10,763,942 |
| a7-pov | bagel | follow 1 + tick 3000 / seek 12000 / frames 200 | 24,437 ms | 4,992,646 |

全部 24 次 rc=0，全部在 `bash tools/lockrun.sh inst3-n13 -- …` 下串行执行。

## 3. 判据、分母与取数口径

- **归键规则**：所有断言按 tick + 目标实体归键比对，**不得**回读整数行数做等价比较（每次运行帧数不同）。依据 docs/N12-ACCEPTANCE-2026-11-15.md:398。
- **行解析**：n13_analyze.py 逐字段按名取值（14 个字段名硬编码），带引号的 name 字段保留空格；缺字段计 `follow_parse_failures`，不跳过。24 份 trace 的 parse_failures 全为 0。
- **摘要行不与状态行混计**：三行退出摘要是 `follow: summary …` / `follow: rejects …` / `follow: queue …`，其中两行也带 `target=`。早前一版按 `target=` 过滤把摘要行算成状态行，已修正并复验（见 §13）。
- **pinAccepted 与 cameraEffect 不等于「相机跟过去了」**：它们记录的是「pin 与导演是否一致」。本 build 中 POV 眼与 STV 中继的优先级高于 pin，fallbacks 每次 324–853，这是 step-2 的工作。**任何断言都不得声称相机跟随成立。**

## 4. A1 — select 语义

| 样本 | 读数 | 判定 |
|---|---|---|
| a1-neg（无 --follow-target） | 0 条 follow 行，0 条摘要行 | 负控通过 |
| a1-pos（--follow-target 1） | 恰好 1 条状态行；`target=1 serial=0 src=manual pinned=1 valid=1 state=ok reason=ok nameKnown=1 nameSource=userinfo nameReason=ok name="SpawN" tick=2098` | 正控通过 |
| 同帧导演一致 | 同一 tick 的 `spectate: tick=2098 … target=1 src=manual` | director.source() 与 src=manual 一致 |

正控原文：

~~~
follow: target=1 serial=0 src=manual pinned=1 valid=1 state=ok reason=ok nameKnown=1 nameSource=userinfo nameReason=ok name="SpawN" tick=2098 cameraEffect=pov-eye-wins pinAccepted=0
spectate: tick=2098 dead=… corpses=0 target=1 src=manual tr=0 prog=0.000 … director=1 stable=0 …
~~~

**A1 = PASS**。命中断言要求的全部六项：一条 follow 行、target 正确、pinned=1、src=manual、state=ok、导演 targetIndex 一致。

## 5. A2 — 幂等选人

| 样本 | 读数 | 判定 |
|---|---|---|
| a2-pos（同一目标选两次） | **1 条**状态行；摘要 `picks=2 idempotent=1` | PASS，但见下方限定 |
| a2-neg（先后选 1 与 2） | 4 条 follow 行 / 3 条状态行；摘要 `picks=2 rejects=1`，notObservable=1 | 负控通过 |
| a2-neg-stv（STV 上选 2 与 3） | 11 条 follow / 10 条状态；`fallbacks=324 pinnedDeaths=1` | 负控通过 |

**限定量（必须写进任何引用 A2 的地方）**：「选两次只打一行」之所以成立，是因为 followPublishTrace 的打印键 {target, serial, src, pinned, valid, state, nameKnown} 第二次没有变化，**第二次命令本身在 trace 上不可见**，只在退出摘要里以 `idempotent=1` 体现。因此 A2 的断言语义是「一条状态行 + idempotent 加一」，不是「两条命令都留有痕迹」。

a2-neg 的四行里第 2、3 行共享同一时间戳，原因是 followPublishTrace 在**同一次调用**内既打了拒绝行又打了状态行（拒绝待决 + 状态键变化）。这不是 reason 串味，机制见 §7 与 §11。

## 6. A3 — next / prev 环绕

| 样本 | 读数 | 判定 |
|---|---|---|
| a3-next（STV，钉 9 后 next） | `9 ok @11057` → `9 director-refused` → `10 ok` → `10 director-refused`；摘要 `picks=2 target=10` | PASS：升序取下一个可观察存活玩家 |
| a3-prev（STV，钉 9 后 prev） | `9 ok` → `8 ok` → … → tick 25800 `target=8 pinned=0 valid=1 state=dead` → 27832 `state=ok`；`picks=2 fallbacks=625 pinnedDeaths=1` | PASS：降序取上一个 |
| a3-next-bgl / a3-prev-bgl（bagel） | 各 **1 条**状态行，`picks=2 idempotent=1` | NOT ASSERTABLE：只有一个候选，环绕回到自己，被报成 already-following |

候选集定义取自产品自身谓词（`entityIndex>=0 && slotObservable && slotAlive`，升序，从 pin 起步，越过末尾回卷），**不是**外部探针的另一套谓词。这也正是矩阵用 `--follow-target 1..16` 单进程扫掠得到候选表的原因。

## 7. A4 — 被钉目标死亡

| 样本 | 读数 | 判定 |
|---|---|---|
| a2-neg-stv | tick 18295 `target=3 state=dead`；`pinnedDeaths=1` | 状态侧 PASS |
| a3-prev（STV） | tick 25800 `target=8 pinned=0 valid=1 state=dead`；`pinnedDeaths=1` | 状态侧 PASS |
| scout16（STV） | tick 27832 `pinned=0 valid=1 state=dead`；`pinnedDeaths=1` | 状态侧 PASS |
| reason 词元 | 上述三处的 `reason=` 全是 `ok`，从未出现 `pinned-dead` | **FAIL** |

死分支原文（name/tick 略）：`follow: target=3 … pinned=0 valid=1 state=dead reason=ok nameKnown=1 …`

**pinned vs unpinned 的判别（A4 负控）**：

- a1-neg-stv（无 pin）：`tick=11057 target=2 handovers=0` → `tick=19016 target=3 deaths=1 handovers=1`，中继在其自身目标死亡时立刻交接。
- a2-neg-stv（有 pin）：handovers 0@11057 → 1@11190（deaths=0，导演重选）→ 2@19016（deaths=1）→ 3@19908（deaths=2）。**pin 并未阻断中继交接**，这与 step-1 未改相机优先级一致。

结论：A4 的「产品会发现自己被钉的目标死了并解除 pin」**已被三条独立读数证明**；「reason 会说 pinned-dead」**没有**，根因在 §11。

## 8. A5 — serial 复用 / 越界

| 样本 | 命令 | 读数 | 判定 |
|---|---|---|---|
| a5-oor | --follow-target 5000 | **0 条 follow 行**；摘要 `picks=0 rejectedFlags=1` | 规格形态不可达 |
| a5-maxidx | --follow-target 2048 | 同上 | 同上 |
| a5-noslot | --follow-target 2000 | 1 条拒绝行 `state=unchanged reason=not-observable`；`rejects=1 notObservable=1` | no-slot 被 isPlayer 门抢先 |
| a5-max2047 | --follow-target 2047 | 与 a5-noslot 同形 | 同上 |
| a5-bogus | --follow-action bogus | 0 条 follow 行；`rejectedFlags=1` | 参数拒绝不产生 follow 行 |
| index-reused | 需要 serial 变化 | 不可构造（serial 恒 0） | 不可达 |

**A5 = UNSUPPORTED**。三条原因各自独立、都要写清楚：

1. CLI 层先做 `parsed >= 0 && parsed <= 2047` 的范围检查（native/src/main.cpp:1307-1320），越界只加 `rejectedFlags`，**连拒绝行都不打**。因此 `reason=index-out-of-range` 在 >2047 的输入上永远看不到。
2. 槽表是按实体表大小构造的（main.cpp:11676-11715），2000 / 2047 这类下标**存在行**，只是不是玩家，于是 followValidate 的第 1 道门（范围）到不了，第 3 道门（isPlayer）先开火，输出成 `not-observable`。
3. `index-reused` 要求两侧 serial 都已知非零（followReconcile :1601-1652），而 followSlotSerial（:1386-1395）**恒返回 0**，该分支不可达。

## 9. A6 — 名字解析

| 样本 | 读数 | 判定 |
|---|---|---|
| scout16（STV，有 userinfo） | 每个被接受的实体都 `nameKnown=1 nameSource=userinfo nameReason=ok name="<真实选手名>"` | 正控通过 |
| scout12（bagel，无 userinfo） | `nameKnown=0 nameSource=none nameReason=no-userinfo-tick name=""` | 负控通过 |
| a5-noslot | `nameReason=no-userinfo-slot name=""` | 另一条负侧取值 |

**A6 = PASS（枚举值形态）**。断言写法是「nameKnown / nameSource / nameReason 取值落在枚举内，且 name="" 时绝不伪造名字」，**不是**「名字拼对了」。

两处继承缺陷仍在（N13 设计 §1.5 已登记，本卡实测未变）：native/src/main_hud.cpp:730（viewEntity<1）与 :742（无 userinfo 槽）**提前返回时 reason 留空**。产品层兜底成 `no-userinfo-slot`（main_hud.cpp:1101-1113 的 hudFollowResolveName），因此 trace 上看不到空串，但那是兜底而不是根因修复。

## 10. A7 — 跨 tick 切换

| 样本 | 读数 | 判定 |
|---|---|---|
| a7-dead（STV） | seek 后（`appliedTick=18500 status=applied deltaTicks=6461`）在 **tick 29366 重新发布了 `state=ok`**，其后 31085 又 `dead` | 可读：pin 在新 tick 上重新发布 |
| a7-pov（bagel POV） | seek 后 647 条 `scene: eye row`（203 条 reqTick≥12000，rowTick==reqTick，src=camera），但 **follow 行只有 1 条**，停在 tick=2098 | 不可读：follow 行不按 tick 归键 |
| serial 半侧 | `state=ok only if the serial still matches` | 不可断言（serial 恒 0） |

A7 的真实形态：**场景眼行按 tick 归键，follow 行按状态归键**。因此「seek 之后来一条 tick=U 的新行」只在状态于该 tick 发生变化时才发生。设计原文期待的是后者，产品实现的是前者；这是设计-实现的口径差，必须记下来而不是算成一次失败的测量。

顺带查明：`scene: eye` 行整体被 `haveObserverView` 门控（main.cpp:11295），**STV demo 永远不打眼行**，所以早前「a7-seek 有 0 条眼行」的读数属 demo 属性而非 seek 失效。

## 11. 结构性不可达（两条，带代码坐标）

### 11.1 `FollowReason::PinnedDead` 从未被赋值

`FollowReason::PinnedDead` 只出现在 native/include/target_identity.h:97（枚举）与 :115（名字 "pinned-dead"），**在整个 native/src/main.cpp 里没有任何赋值点**。followReconcile 的死分支（:1640-1648）只做 `state = FollowState::Dead` 与 `++g_followPinnedDeaths`，不理 `g_followLastReason`。而 `g_followLastReason` 的全部写点是 :1194 / :1475 / :1501 / :1510 / :1527 / :1569 / :1583，全在命令路径上；于是死分支打印的 reason 只能是上一次命令留下的值（通常是 `ok`）。

### 11.2 `FollowReason::GoneIndexReused` 在 reconcile 路径不可达

followSlotSerial（native/src/main.cpp:1386-1395）**恒返回 0**（SpectateSlot 在 native/include/spectate_target.h:53-64、EntityState 在 native/include/demo_header.h:111 都不带 CBaseHandle）。复用门要求两侧都已知非零，故该分支不可能触发。

## 12. 本卡新增缺陷登记（建议 D63–D67）

| 建议编号 | 一句话 | 位置 | 定级理由 |
|---|---|---|---|
| D63 | `FollowReason::PinnedDead` 有枚举有名有测试期待，但全仓无赋值点，死分支 reason 打印成上游残留值 | native/include/target_identity.h:97/:115；native/src/main.cpp:1640-1648 | 静默错误：数值合理、含义是假的；A4 的唯一 FAIL 项 |
| D64 | 同一次 followPublishTrace 调用内，状态行会带上刚才那次拒绝留下的 reason | native/src/main.cpp:1686-1715 | 外观缺陷：状态行 reason 可能名不副实，但不改变状态机 |
| D65 | `hudLiveResolveName` 两处提前返回把 reason 留空，靠上层兜底成 no-userinfo-slot | native/src/main_hud.cpp:730、:742 | 分层归属错误：缺陷被下层掩盖，换调用方就会暴露 |
| D66 | follow 的 serial 没有数据源，恒 0，导致 index-reused 语义整体不可用 | native/src/main.cpp:1386-1395 | 功能缺口：设计里的三分之一复用安全逻辑无法生效 |
| D67 | `--follow-target` 的 CLI 范围门把越界输入吞成 rejectedFlags，连拒绝行都不打 | native/src/main.cpp:1307-1320 | 可观测性缺口：用户敲错下标在 trace 里没有任何具名痕迹 |

编号说明：仓库现有最高号是 D62（docs/DEFECT-REGISTER-ADDENDUM-D51-D56-2026-11-15.md:203），故本卡从 D63 起。**本报告只建议编号，不改主表**（该表规定只追加、由 lead 分配）。

## 13. 风险与自我检查

本卡在工具与读法上**抓到并修掉**三处 D53/D55/D56 类错误，全部记录在案而不是悄悄改掉：

1. **按 `target=` 过滤 follow 行**把三行退出摘要算成状态行（摘要 2 行也带 target=），会让「恰好一条状态行」的断言读到 2。修法：先分类摘要行，再取状态行。
2. **产出物标签覆盖**：第一轮 bagel2 批写了 a3-next / a3-prev 两个标签，随后的 stv 批用同名标签把它们覆盖了。修法：补跑 bagel4 写出 a3-next-bgl / a3-prev-bgl，并在矩阵脚本注释里点名该故障。
3. **假定 summary 字段类型**：n13_table.py 首版假定 `follow_summary` 恒为 dict，在无摘要行的 label 上它以空串出现，直接 `AttributeError`。修法：先判类型再取值。

另有一条读数纪律要重申：**pinAccepted / cameraEffect 不是「相机跟过去了」**。本 build 中 POV 眼与 STV 中继的优先级高于 pin，fallbacks 每次 324–853，pinAccepted 常为 0。这是 step-2 的预期工作量，不是本卡失败项。

## 14. 本条证明什么 / 没有证明什么

**证明**：命令入队与状态生效这一层（select / 幂等 / next-prev 环绕 / 死亡发现 / 名字解析）在 7a8da164 上按断言原文工作，且有具名正负控与 0 解析失败。

**没有证明**：

- 相机真的跟随了被钉目标（本 build 中 POV 眼与中继优先级更高，pinAccepted 常为 0）。
- serial 复用安全（无数据源）。
- `reason=pinned-dead` 这一词元（无赋值点）。
- 任何 UI 可行性判断（普通用户如何在产品界面里触达这些命令）：**NOT VERIFIED**，需 N13 后半段与 N16 接口落地后再验。
- 跨 exe 的稳定性：本卡只对 7a8da164 负责；exe 在卡期内被他人重建过三次。

## 15. 产物哈希（D:/_shared/EXEC-2026-11-15/INST3/）

~~~
a1-neg        200,503  f55c6c0f5b1a9931    a1-neg-stv   9,610,915  b78523db3a6ffd0f
a1-pos        180,407  760e50abbf984960    a2-neg         215,342  e7d7d95785575b90
a2-neg-stv  8,964,010  665158c0d4b10ef3    a2-pos         214,212  ab8d422a50c73747
a3-next     9,127,059  625d4168e86f629c    a3-next-bgl    139,257  17357f7fb34f1393
a3-prev     9,330,169  3b15a0c55a026962    a3-prev-bgl    139,254  87158b44459fe5d0
a4-notfol     173,357  f1974c1475361ed2    a4-pov         125,364  f02c06e8bbdb3df0
a4-unfol      200,667  33df8569cceb0267    a5-bogus       166,468  c124849c042c2647
a5-max2047    139,234  fc8612f8f8c0ba3f    a5-maxidx      139,013  3a39123b34e42510
a5-noslot     234,849  d04df5144243d8f8    a5-oor         200,342  48977849c23c2c58
a7-seek     8,020,864  57eff1a2e0d038e8    a7-dead     10,763,942  86ff7d617b9fd39f
a7-pov      4,992,646  99451558bd1e2d92    calib          179,367  0b075cce177a88fd
scout12       203,767  bf27138ec53a715c    scout16      8,176,891  d93342ef2bd564f9

tools: n13-a1a7.sh 3,652 76ba64833cfb0081 · n13-matrix.sh 5,395 8f5186fda0aa22d3
       n13_analyze.py 5,844 59d561cc919207a9 · identity.txt 403 081679bf2f00e6c5
       a7-seek.script 953 5a6edb1496ba4fc5 · a7-dead.script 427 d9c24014c83b5886
       a7-pov.script 364 34d1207bf68175ee · n13_table.py 614
~~~

以上 sha256 均取前 16 位；完整值由 hash-all.sh 重算。所有 trace 的 rc=0、跑在 `bash tools/lockrun.sh inst3-n13` 下，锁在每次运行后打印 VERIFY-LOCK=RELEASED。
