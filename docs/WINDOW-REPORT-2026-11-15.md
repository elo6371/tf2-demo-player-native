# 本窗口完整任务报告（2026-11-14 21:00 → 2026-11-15 01:15）

仓库 `D:/TF2_Native_Worktrees/t105-e5`（分支 `candidate/e5`）。执行：lead + 派工智能体（n12cimpl2 / n05static / n06group / gateverdict / gateverdict2 / n12accept / n13design / n13impl / n12capture / arithsweep）。证据根 `D:/_shared/EXEC-2026-11-15/`。

## §0 一句话结论

本窗口把 **N12-C 相机轨道消费端**从「代码在树里但 POV 眼位没跟上」推到「POV 眼位真的按精确 tick 取值（三次捕获 1407/599/146 帧 src=camera）」；把 **五门禁**从「零真实 verdict」推到「两道可声称真绿、其余红是环境假红」；把 **N13** 从「没有产品通路」推到「施工规格入库 + 第一步代码写完（未构建）」；修掉 **14 处会让门禁静默算错的嵌套算术**。**没有关闭任何一张产品卡**：N12 的 A2/A3/B2 覆盖率因缺帧仍 NOT VERIFIED，N13 未编译，D39/D01 仍 OPEN。

## §1 提交账（9 个）

| 提交 | 内容 | 规模 |
| --- | --- | --- |
| `636c45d` | N05 919/849 draw 路径清单（只读静态归因） | 223 行 |
| `a5c70eb` | N12-C 第二步：POV/relay 眼位改读精确 tick 相机轨道 | 6 文件 +780/−63 |
| `ed419f6` | N06 七图世界帧异常分组（14 格 + 三族判据 + 三个最小实验） | 290 行 |
| `304813d` | 五门禁第一轮真实 verdict + 教训九/十 | 3 文件 |
| `569c67e` | 两处宿主级可跑性修复（[ -d ]→[ -e ]、嵌套算术两步赋值） | 2 文件 +18/−2 |
| `68105a8` | N12-C 第三步：解码扁平分量，POV 眼位真正吃相机轨道 | 6 文件 +387/−3 |
| `71104ae` | N13 施工规格入库（含裁决与锚点复核） | 348 行新文件 |
| `f74ce5f` | 14 处嵌套算术改两步赋值 + 新自检门禁 | 15 文件 +391/−15 |
| `3581025` | 门禁第二轮 verdict（gate2/gate4 转真绿） | 3 文件 +109 |

每个提交前 `tools/audit-tree.sh` 都是 `VERDICT: OK`；唯一的产品行为变更在 `68105a8`，已裁决并写明范围。

## §2 N12-C：三步收口（含一次行为变更裁决）

### 2.1 三步

1. 第一步（窗口前）：相机 tick 轨道写入 demo 解析层，`demo_open_probe --scan --camera-track` 可导出。
2. 第二步 `a5c70eb`：POV/relay 眼位按精确 tick 查轨道行。**当时 povApplied=0** —— 轨道行只带整向量名（m_vecViewOffset），而 demo 实际发的是**扁平分量**（m_vecViewOffset[0..2]），命中路径死在数据事实而不是代码 bug 上。
3. 第三步 `68105a8`：`demo_header_camera.cpp +37` 新增 `readFloatRanked` —— 整向量优先、不存在时才读同名扁平分量；`[0]/[1]` 故意不合成（无消费者）。`CameraTickSample` 仍 56 B。

### 2.2 读数（我独立复核过）

- exe `90f781b5…` / 3,003,392 B；probe `168e8d9f…` / 663,040 B。
- flags 25 → 31；POV 三次 src=camera = **1407 / 599 / 146** 帧，与 povApplied 一一相等；越界诚实回退 22/15/8（轨道 viewOffsetZ=0 被既有 (0,300] 策略拒）。
- scene: clock / scene: snapshot 逐字节不变；STV 4 条 relay 冻结行逐字节不变；`--scan` 8007b92b… / `--scan --camera-track` 3ef091d5… 与第一步基线 cmp 通过；删 3 个 obj 重建两 exe sha 相同。
- eye_z − origin_z − offsetZ：冻结行只打 1 位小数（上界 0.10），实测 max **0.0999 / 0.0809 / 0.0809**、越界 **0**。

### 2.3 被接受的行为变更（裁决 2026-11-14 23:40）

修好数据面后**冻结的 scene: eye origin= 行变了**：按 tick 键控比对，t20000 **142/149**、t8415 **71/75**、t2000 **27/28** 不同；三类 = 只改 tick 三字段 / 另改 offsetZ+eye（30 个 tick，旧 exe 用陈旧 75.0001）/ t=0 的 origin 打印量子。`probe.dense.rows.txt` 显示 tick 2425–2432 = 45.000042、2433–2440 = 75.000069，**是阶跃不是插值**。

**接受**：① 眼位跟上精确 tick 就是 N12-C 的交付物，不许它改既有行等于要求修复只出现在旁路；② 变更范围被证据钉住（clock/snapshot/relay 不变）；③ 数值判据没变（737.5/651.0/387.9 本来就没变，变的是 provenance）。**条件**：A1 按新 exe 重基线；accept.py 恒等式按 src= 分支（相机行断言 originTick==offsetTick==rowTick + offsetAge==0，快照行才用 resolved_server == offsetTick + offsetAge）；不许改产品打印凑门禁；45.0000 物理含义与像素级一致标 NOT VERIFIED。

### 2.4 验收现状（本窗口最重要缺口）

`n12accept` 用同一脚本 + 新 exe 重跑（accept.py 86,792 B，分支实现 `:1238-1380`）：

| 样本 | 判词 | 说明 |
| --- | --- | --- |
| A1 | **PASS** | camera 分支 6/6：originTick==offsetTick==rowTick=22203、offsetAge=0、\|737.5−(662.5295+75.0001)\|=0.0296 |
| A2 / A3 | NOT VERIFIED | 卡定义在 t=40000/39999，step3 只采了 t8415/t2000 |
| N12-B | NOT VERIFIED | B1 零分辨力（68.000065 vs 常量 68.0 差 0.0001）、B2 未采 |
| N12-C | NOT VERIFIED | 缺 B2 帧 ⇒ all(BOUNDED-STALE) 不成立（实测仍 1100） |

**判据没放水**：同脚本在旧 capture-1c3580e2 上跑 legacy6 仍得 A PASS / B PASS / C BOUNDED-STALE，且 5 个 tick-align 样本全走 unknown → NOT VERIFIED。口径纠正：stv-after3-t20000 实测是 demo 18901、m_vecViewOffset[2]=68.000065（站立）= 旧 **B1**（不是任务书写的 B2）。

**缺口可修**：只差在 exe `90f781b5…` 上补采 `pov-t40000` / `upward-pov-t39999` / `upward-pov-t20000` / `stv-t40000`，再给 accept.py 加 ROSTER['full90'] 重跑。本轮已派 n12capture，但 gate4 占 hostlock 时它排队，收工指令下已中止，**未采到任何新帧**。

## §3 五门禁：两轮真实 verdict

### 3.1 第一轮 `304813d`：只有 class-pose 真绿

关键纠正：此前记的「gate2 rc=0 44 s」是验证者 wrapper 的 bug（exec-gate.sh 末尾裸 echo GATE-RC=$? 把退出码洗成 0），门禁末行是 PLAYER-ANIMATION-CHECK=FAIL。**口径：五道里只有 class-pose 可声称绿，其余四道的红都不能记成 D53/D55/D56/D59 的失败。** 五道门禁全都有官方 OUT 变量（T62_POSEFAMILY_OUT / PLAYER_ANIMATION_OUT / PLAYBACK_OUT / T84_STATES_OUT / VIEWMODEL_ANIMATION_TRACE_DIR）⇒ 不必碰 evidence/。

### 3.2 第二轮 `3581025`：再加两道绿

| 门禁 | 真 rc | 末行 | 关键读数 |
| --- | --- | --- | --- |
| #2 player-animation-check.sh | **0**（67 s，235 OK / 0 FAIL） | PLAYER-ANIMATION-CHECK=PASS | E synthetic movingTicks=31 movingAdvanced=30；E demo movingTicks=39 movingAdvanced=26；D55 lines=8 bad_move=0 bad_idle=0 bad_parse=0 |
| #4 animation-drive-states-check.sh | **0**（4002 s，20 OK / 0 FAIL） | ANIMATION-DRIVE-STATES=PASS | 9 次产品运行每次 rc=0、帧 2,582,406 B；READINGS=PASS；A default==legacy md5 6f6309c0… |
| #3 #5 | 未重跑 | — | 第一轮的红 = 本机缺 autorecord_2026-07-02_13-26-46.dem 的环境假红 |
| #1 class-pose | 沿用第一轮 0 | PASS | 本轮未复跑 |

两处修复都被读数证明：`:329` 的两步赋值让 E 段真的读出 30/26（旧一行式吐字面算术文本），`:136` 的 [ -e ] 让 gate4 从 exit 2 零读数变成真跑 9 次产品。跑完 **55 个二进制逐字节未变**，exe 仍 90f781b5…。

**gate4 自带四条负控记 NOT VERIFIED**：脚本 `:445` 用 `grep -m1E` 拼原因串，本机 grep 解析成 `-m 1E` ⇒ `invalid argument '1E' for '-m'`，每条 "failed as required:" 的原因字段为空；注入后的症状行本身正确（neg-1 no trace line names state=flinch / neg-2 state lines x0, a_flinch frames x0 / neg-3 byte-identical at tick 14765 / neg-4 no animdsum line），但原因串为空 ⇒ 不能证明失败源自注入。**一行修复（grep -m1E → grep -m 1 -E）已记录未应用**。另：controls=skipped 是因为 `:491` 用 ${SKIP_NEG:+skipped} 而 SKIP_NEG 被设成非空的 0，负控其实跑了。

## §4 N13：规格入库 + 第一步代码（未构建）

### 4.1 设计 `71104ae`（docs/N13-COMMAND-STATE-MODEL-2026-11-15.md，348 行）

n13design 只读产出 328 行 + 我的 §0 裁决与 §0.1 锚点复核。核心：

1. **三身份分离**：broadcaster（demo 的观察者）/ POV 主体（lastViewEntity）/ 被跟随者（用户 pin 的目标）——混用这三个是这张卡最容易被做错的地方。
2. **一个真源结构**：TargetIdentity{entityIndex, serial, source, pinned, valid}，正是 N16 的 PaneTarget，N16 必须 `using PaneTarget = tf2::native::TargetIdentity;` 而不是再写一遍。SpectateDirector 保持「相机实现」。
3. **第一步是纯增量**；**第二步（pinned follow 压过 demo eye）需单独裁决**，因此第一步做完，**POV demo 里选人不会让画面真的跟着走**（打印 cameraEffect=pov-eye-wins）。

我核过六个锚点全对：handleCameraAction:199、--spectate-target:3336-3348、spectateSlots:11046、一次性选人 :12978-12981、director.update:12987、退出计数器 :13453。

### 4.2 第一步代码（**工作树未提交、未编译、未运行**）

| 文件 | 变化 |
| --- | --- |
| native/include/target_identity.h | 新增 127 行（FollowTarget/FollowState/FollowReason/FollowAction + 名字表，header-only，不动 CMake） |
| native/src/main.cpp | +615 / −0：状态机、五个命令、CLI（--follow-target / --follow-action）、有界命令队列（16 项）、应用点、follow: 行、三行退出汇总 |
| native/src/main_hud.cpp | +22 / −0：薄访问器 hudFollowResolveName，内部调用既有 hudLiveResolveName（:726） |
| native/src/main_hud.h | +20 / −0：FollowNameInfo + 声明 |

- **四道闸** followValidate `main.cpp:1296-1335`（range / no-slot / isPlayer+team<2+slotObservable / alive），team 与存活直接复用 slotObservable / slotAlive；应用点 `:13523` 在槽位构建（:11573）之后、director.update（:13468）之前。
- **A8 静态自证**：三个 tracked 文件 numstat 全 `+N/−0`（零删除行 ⇒ 没有一行既有 trace 被改写）；不带 follow 参数时 latch=None、followPublishTrace 在 neverFollowed 处返回、汇总受 if (followActivity) 门控 ⇒ 追加 0 行。
- **诚实缺口**：serial=0 恒为未知（SpectateSlot / EntityState 都不带 CBaseHandle），gone-index-reused 已编码但本步不可达；cameraEffect 四取值与队列节流 UNOBSERVED。
- **WIP 备份**（防工作树被清）：`D:/_shared/EXEC-2026-11-15/LEAD/n13-step1-wip/tracked.patch`（37,471 B，sha bd4b3fc3…）+ target_identity.h.untracked（5,040 B，sha e2af0787…）。

## §5 N05 / N06（只读归因，不结卡）

- **N05 `636c45d`**（223 行）：919 的帧像素排除 world/entity ⇒ 指向天空 pass（native_renderer_frame.cpp:293-329）；849 仍在 world 内（native_renderer_shaders.cpp:218-269）。4 条 NOT VERIFIED。
- **N06 `ed419f6`**（290 行）：14 格分类（7 格 STV 有证据、POV 6 格 NO-EVIDENCE、P7=upward POV 帧 250de7df·tick20000）；三族判据 D-W 白噪（hf60%>=1.6，成员 1.791/1.650/8.765 vs 非成员 ≤0.580）、D-B 黑区（近黑块 ≥2500 px + bbox 不贴边 + 5 px 环带中位亮度 ≥40，gullywash 余量 3.2 已标 NOT VERIFIED）、D-P 浅蓝面（y≥400 + 逐列等厚 44 px/斜率 −0.200 + 中位色漂移 ≤3）；三个最小实验各带复现闸 sha 与负控决策树。**零个 on/off 帧执行** ⇒ D39 仍 OPEN。

## §6 嵌套算术清扫 `f74ce5f`

教训九从「知道有坑」变成「有检测器 + 已清扫」：

- 权威读数（tools/check-nested-arith.sh）：仓库根 **136 个 *.sh / 905 处**算术展开 / **31 处**含命令替换 / **14 处**内层含引号 ⇒ 命中 bug（与 LEAD/arith-sites.txt 的 14/17 一致；教训九的「26/22」是行级 grep 粗数，**以 31/14 为准**）。
- 14 处全改两步赋值（12 个脚本），按各文件约定补非整数护栏；全部 bash -n 通过、LF。
- 新门禁 --selftest PASS（正控干净 + 本机真复现 bug）、仓扫 sites=0 VERDICT: OK、回放 12 个文件 HEAD 旧版得 sites=14 VERDICT: FAIL ⇒ **14/14 全抓**。
- **没做**：没跑这 12 道门禁的真实整轮，所以「修完是否仍按原语义判绿」只有语法与自检支撑。

## §7 行尾与 .gitattributes

按负责人裁决（不加全局 LF 规则；只对源码/构建/门禁脚本设 LF；保留证据目录 -text；不归一化 Markdown；不做全树重写）已逐字节还原在工作树：3,147 B / CR=0 / sha256 dfd74b5e… / numstat = 14 0。**仍未提交**（负责人标为「待安全窗口应用」）。本窗口另把 native/src/main_hud.h 的 123 个 CR 归一成 LF（该文件是 i/lf w/crlf 的历史产物），归一后 audit-tree 从 CRLF-DAMAGE 回到 VERDICT: OK，diff 仍是 +20/−0。

## §8 明日第一件事（有序，含依赖）

1. **N12 覆盖率收复（最高优先，纯增量）**：在 exe 90f781b5…（**不重建**）上补采四帧 → accept.py 加 ROSTER['full90'] → 重跑六样本 → 恢复 A2/A3/B2 判词。派工书在 EXEC-PLAN 的派工记录里；hostlock 现在 FREE。
2. **N13 第一步构建 + 实测**：先 build-target.sh（会改 exe sha，**必须排在第 1 步之后**），再按 n13-step1-impl.md 跑 A1/A2/A3/A8：A8 要求既有 scene: eye* / scene: camera mode= / spectate: camera relay target= 行逐字节不变 + demo_open_probe --scan 两个 sha 不变。**A8 不过不许提交**。
3. **gate4 的 grep -m1E 修复**（:445 → grep -m 1 -E）+ 重跑四条自带负控，把 NOT VERIFIED 变成可判。
4. N14/N15（跟随 UI）→ N16（每窗复用 TargetIdentity）→ N27 导出实现 → N17–N20 → N28/N29/N30。
5. 12 道算术清扫过的门禁重跑一遍（至少 world-material-check / viewmodel-*），确认没把语义改坏。

## §9 NOT VERIFIED 总表

| # | 事项 | 为什么没验证 |
| --- | --- | --- |
| 1 | N12 的 A2/A3/B2/EX3 | 缺帧（t40000 / t39999 / 下蹲帧） |
| 2 | N12-C 的 45.0000 物理含义、像素级一致 | 只有数值与结构证据 |
| 3 | N13 第一步的全部运行时行为 | **未编译、未运行** |
| 4 | gate4 自带四条负控 | grep -m1E 引号粘连导致原因串为空 |
| 5 | gate3 / gate5 | 本机缺 autorecord_2026-07-02_13-26-46.dem（环境），未重跑 |
| 6 | 12 道清扫过的门禁的整轮语义 | 只做 bash -n 与自检 |
| 7 | N05 的 4 条、N06 的 gullywash 余量与 POV 6 格 | 静态归因，无 on/off 帧 |
| 8 | 相机优先级（pinned > demo eye） | 明确留给第二步并需单独裁决 |

## §10 教训（已写进 EXEC-PLAN，可复跑）

- **教训九（嵌套算术）**：$(( $(f "x") - 1 )) 在本机 bash 5.3.20(1)-msys 上不展开、静默给字面文本。识别信号：断言消息里出现未展开的 $(( 。现由 tools/check-nested-arith.sh 兜住。
- **教训十（[ -d ] 怪相）**：TF 根上 bash 内建 [ -d ] 为假而 [ -e ]/-r/-w/-x 全真、/usr/bin/test -d 为真、cd 进得去、非 reparse point。**机制未定**，只记事实与规避方式（判根用 -e）。
- **教训十一（负控的同义反复）**：负控 harness 里任何一步自己的报错都会让门禁「失败」，于是 failed as required 变成**假负控**。判据必须是「失败原因文本 = 注入的症状」。gate4 四条负控正栽在这里。

## §11 工作树交接状态（收工时 01:15）

- HEAD `3581025`；本窗口 9 个提交全部 audit-tree VERDICT: OK。
- **未提交（我的）**：N13 第一步四个文件（main.cpp +615 / main_hud.cpp +22 / main_hud.h +20 / target_identity.h 新增）+ 上述 WIP 备份。**未构建**。
- **未提交（负责人的）**：.gitattributes（3,147 B 待安全窗口）、docs/PROJECT-NEXT-TASKS-2026-11-14.md、docs/PROJECT-ROADMAP-HANDOFF-2026-11-12.md、新文件 docs/PROJECT-HANDOFF-2026-11-14.md —— 我全程没碰这四份。
- hostlock：FREE；product_processes=0；无遗留产品进程；gate4 的两条 checker 复跑任务已被执行者杀掉且不作为证据。
- 交付物索引：docs/ 下新增 N12-C-STEP2-IMPL / N12-C-STEP3-IMPL / N05-DRAW-INVENTORY / N06-GROUPING / GATE-VERDICTS / GATE-VERDICTS-ROUND2 / N13-COMMAND-STATE-MODEL / 本报告；证据根 D:/_shared/EXEC-2026-11-15/（N12-QA/ 含 accept.py 与 capture-90f781b5/，LEAD/ 含全部派工报告）。
