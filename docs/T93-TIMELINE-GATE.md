# T93-TIMELINE-GATE — 时间线门禁：四段断言、代码锚点、变异怎么打穿

- 工作树 `D:/TF2_Native_Worktrees/t93-timeline`，分支 `gate/timeline`，基线 `e15577b`。
- 门禁：`timeline-check.sh`（307 行，4 段 `S1..S4`，全绿 **29 条**断言，末行 `TIMELINE-CHECK=PASS assertions=29`）。
- 变异：`timeline-mutations.sh`（251 行）+ `timeline-patchlib.py`（146 行），两例 `m1`/`m2`。
- 相机脚本：`scripts/timeline-speed.script`、`timeline-pause.script`、`timeline-clamp.script`。
- 背景证据（手工 31 例）：`D:/_shared/t85-timeline/TIMELINE-ACCEPTANCE.md`、`TIMELINE-MEASUREMENTS.md`、`GAPS.md`。
- 本文件由 owner `t85-timeline` 写，供总报告引用；所有断言都可在脚本里按标签原文 grep 到。

## 1. 它测什么、不测什么

**测**：产品自己的播放时钟仪器是否还准 —— 抓帧落点、同 tick 跨倍速的字节一致性、暂停冻结与恢复不补跳、越界 seek 的钳位。

**不测**：画面内容对不对（材质/光照/姿态属其它门禁）；后退 seek 的动画相位（T61 有意取舍，D-26）；反向播放与 Stop（产品侧无脚本化生产者，且本门禁不注入输入）。

一句话判据：**"时钟还准" 不等于 "画面还对"，也不等于 "seek 已被验证"** —— 见 §2 的 `--capture-tick` 语义。

## 2. 三处读数（门禁只看产品自己打印的 trace）

trace 不是默认写的：必须给 `TF2_NATIVE_TRACE=<file>` 才落盘（`native/src/main_trace.cpp:52-85`）。门禁每例都设到 `<out>/<case>/trace.txt`（`timeline-check.sh:186`）。

| 读数 | 原文格式 | 发射点 | 用来断言 |
|---|---|---|---|
| 抓帧 | `capture: armed requested=%d frame_tick=%d frames=%llu` | `native/src/main.cpp:11271` | `requested`==请求 T、`frame_tick`==实际画出的 tick |
| 播放状态 | `playback: state phase=... cursor=%d end=%d paused=%d reverse=%d speed=%.6f ...` | `native/src/main.cpp:1179-1191` | `cursor`（暂停/恢复）、`speed`（倍速映射）、`end`（钳位上界） |
| 传输请求 | `playback: request id= kind= source= requestedTick=%d appliedTick=%d ...` | `native/src/playback_state.cpp:15-24` | 越界 seek 的 `appliedTick` |

读法纪律：`field()`（`timeline-check.sh:158-163`）找不到字段时返回字面量 `<missing>`，所以 trace 格式一变就是**具名红**，不会静默读到空串而误判绿。

**`--capture-tick N` 不是 seek（最容易被误读的一条）**：它不移动 transport。产品照常向前播，当 `g_playback.tick > N` 时把 cursor 按回 N 并 `noteExternalCursor()`，只为抽出"画在 tick N 的那一帧"（`native/src/main.cpp:9005-9019`，T27 注释原文 *"the frame drawn AT tick N, not the first frame at or after N"*）。真正 seek 走 `JumpTicks` → transport 请求（`main.cpp:1156-1165` → `native/src/playback_state.cpp:13-31`）。因此 **S1 断言的是"钉帧准确"，不能当作"seek 落点被验证"引用**；transport 自己的越界钳位在 S4 读。该区分记为 D-27（§7）。

## 3. S1..S4：每条断言是什么、锚在哪、防什么

全绿 29 条的构成：S1 9 条 + S2 12 条 + S3 5 条 + S4 3 条。

### S1 抓帧落在请求的 tick 上（9 条）

3 次启动，`--playback-speed 32 --capture-tick T`，`T = 12000 / 20001 / 24001`，每次 3 条断言：

| 断言标签原文 | 判断什么 | 锚点 |
|---|---|---|
| `seek $T: product exit code` | 产品在这组参数下正常退出（rc=0） | `timeline-check.sh:257` |
| `seek $T: capture armed for the requested tick` | `capture: armed` 的 `requested` 就是请求的 T（请求没被改写） | `timeline-check.sh:261`，格式 `main.cpp:11271` |
| `seek $T: captured frame drawn at the requested tick` | `frame_tick` == T（**画出来的那一帧**就在 T，不是"≥T 的第一帧"） | `timeline-check.sh:262` |

防的缺陷：T26 历史缺陷 —— 抓帧落到"首个 ≥N 的帧"，32x 下一帧跨数十 tick，抓到的画面比请求晚。选 24001 而不是 12000：越界幅度随 tick 增大，24001 在 32x 下必然跨过多个 tick，能稳定打穿（task-9 实测 32x 下相邻绘制帧差 39 tick，见 `TIMELINE-ACCEPTANCE.md` §1）。

### S2 同一 tick 在不同倍速下是同一张图（12 条）

3 次启动，`--player-animation --capture-tick 600 --playback-speed 1|8|32`，每次 3 条断言 + 3 对像素比较：

| 断言标签原文 | 判断什么 | 锚点 |
|---|---|---|
| `speed ${S}x: product exit code` | rc=0 | `:273` |
| `speed ${S}x: capture lands on tick 600` | `frame_tick`==600（三个速度都钉在同一帧） | `:276` |
| `speed ${S}x: the run really used the requested speed` | `playback: state` 的 `speed` 等于请求值（`%.6f`，1/8/32） | `:279`，格式 `main.cpp:1179` |
| `same tick 600 at ${1}x vs ${2}x: payload bytes identical (0/N)` ×3 对（1-8 / 1-32 / 8-32） | 两张 BMP 的**像素 payload** 逐字节相同 | `:281-285`，比较器 `:196-215` |

像素比较取 BMP 头 offset 10 记录的 payload 起点（`:203-204`），长度不一致报 `length-mismatch`，读不出报 `no comparison was possible`（`:217-228`）—— 三种结果都可判红，不会"没法比就算过"。

**这一段的边界（重要）**：像素断言测的是**确定性**（帧是 tick 的纯函数），**不是倍速标定**。产品若把 8x 当 4x 播，tick 600 的字节仍然一样 —— 所以 S2 还有 `the run really used the requested speed` 这条读数；m2 变异就是专门证明这一点（§5）。task-9 在 1x/8x/32x/64x 实测同 tick 同 hash `7507c6c3…dfa5`、XOR 全 0，是这条设计的事实依据。

### S3 暂停冻结游标；恢复不补跳（5 条）

1 次启动，`--playback-speed 1 --camera-script scripts/timeline-pause.script`。脚本序列：`pause` → `mark-tick` → `frames 3` → `expect-same-tick` → `resume` → `playback-state` → `quit`（`main.cpp:872-882`、`:1091-1104`）。

| 断言标签原文 | 判断什么 | 锚点 |
|---|---|---|
| `pause/resume: product exit code` | rc=0（步骤失败会让 rc=1，见 `main.cpp:1316`） | `:293` |
| `pause freezes the cursor: expect-same-tick match` | 暂停中跑 3 帧，`expect same-tick` 的 `match`==1 | `:298`，格式 `main.cpp:1101` |
| `pause freeze read the marked tick` | `actual`==`marked`（比较的两个数确实是同一个 tick） | `:299` |
| `resume advances the cursor past the pause tick (delta=…)` | resume 后 `cursor - marked` ≥ 1（确实往前走了） | `:303` |
| `resume does not replay paused wall time (delta=…)` | 同一个 delta ≤ 2（**没有把暂停的墙钟时间一次性补回来**） | `:304` |

为什么上界是 2：1x 下 resume 后是"下一个绘制帧"，允许 1~2 tick 的调度余量；若实现把暂停秒数换算成 tick 补跳，delta 会到几十上百，必然红。task-9 实测 E1：pause@0 → 3 帧不动 → resume → 5 帧到 7 → 再 pause/step -3 → 9 → jump-seconds 1 → 76，delta 与"无补跳"一致。

### S4 越界 seek 钳到 demo 自己的两端（3 条）

1 次启动，`--start-paused --camera-script scripts/timeline-clamp.script`。脚本序列：`seek -1` → `expect-tick 0` → `seek 999999` → `playback-state` → `quit`（`main.cpp:1156-1165`，钳位实现 `native/src/playback_control.cpp:233-253`）。

| 断言标签原文 | 判断什么 | 锚点 |
|---|---|---|
| `clamp: product exit code` | rc=0 | `:312` |
| `seek -1 clamps to tick 0` | `requestedTick=-1` 的请求行里 `appliedTick`==0 | `:315` |
| `seek 999999 clamps to the demo's last tick` | `requestedTick=999999` 的 `appliedTick` == 同一次运行 `playback: state` 的 `end` | `:320` |

期望值不写死：上界读同一次运行报的 `end`，换 demo 也不会因为常数过期而误红。task-9 实测 C2：`seek 999999999` → applied 64352（=endTick），`seek -100` → applied 0。

## 4. 一次运行 = 8 次产品启动 + 隔离

| 段 | 启动次数 | 参数要点 |
|---|---|---|
| S1 | 3 | `--playback-speed 32 --capture-tick T`，timeout 180s |
| S2 | 3 | `--player-animation --capture-tick 600 --camera-script scripts/timeline-speed.script`，timeout 180s |
| S3 | 1 | `--playback-speed 1 --camera-script scripts/timeline-pause.script`，timeout 120s |
| S4 | 1 | `--start-paused --camera-script scripts/timeline-clamp.script`，timeout 120s |

每次启动（`run_product`，`timeline-check.sh:173-192`）固定带：

- `--tf-root`（本机 TF2 内容）、`--audio-disabled --hidden-window`（沙箱要求）；
- `--settings-file` / `--ui-settings-file` 指向脚本自建的 `mktemp -d` 副本，每例一个子目录；脚本里唯一的 trap `trap cleanup_tmproot EXIT INT TERM`（`:129-131`）退出时 `rm -rf` 整棵树。
  理由：产品退出**无条件**写设置（`main.cpp:11613-11629`；ui.ini 的 recent-demo MRU 在 `native_ui.cpp:137-139`），不隔离就会改用户真实配置，并让下一次运行继承上一次的 MRU（D-22）。
- 抓帧模式另加 `--capture-frame <out>/<case>/frame.bmp --metrics-file <out>/<case>/metrics.csv`。

`--dry-run`（`:93-123`）只打印这 8 次启动的计划并打 `TIMELINE-CHECK=DRY runs=8`，不启动产品、不需要 exe —— 用途是"先接入链、后真跑"，且 dry 不冒充 PASS。

## 5. 变异：怎么证明这条门禁会红

规则：变异必须能编译（否则 `MUTATION-INVALID`，不算抓到）；门禁必须 rc≠0；**必须有一条具名 FAIL 落在该变异针对的断言上**；每例逐字节恢复并重建；最后再跑一次 baseline 证明树是干净的（`timeline-mutations.sh:32-42`）。

| 变异 | 改哪里 | 预期红（原文） | 为什么这条 |
|---|---|---|---|
| `m1` | 删掉 `native/src/main.cpp:9005-9019` 的 T27 hold，改为 `(void)captureTick;` | `FAIL seek 24001: captured frame drawn at the requested tick` | 复现 T26 语义："首个 ≥N 的帧"。32x 下必然跨 tick，24001 是最稳的用例 |
| `m2` | `native/src/main.cpp:2429` 把 `--playback-speed 8` 当 4 播 | `FAIL speed 8x: the run really used the requested speed` | 像素断言抓不到映射错误（见 S2 边界）；必须有读数断言兜住，否则"8x 当 4x"会全绿 |

补充记录（脚本自动做，不靠人记）：m2 下把三对像素断言的结果也 grep 进 `mutation.log`（`timeline-mutations.sh:191-192`）—— 预期它们**仍然绿**，证明"像素测确定性、不测倍速"。这不是门禁的缺陷，而是设计边界，写在 `TIMELINE-GATE.md` §4 里。

断言命中的机器判据（`timeline-mutations.sh:83-85`）：

```bash
named_red() { grep -a -qF "FAIL $2" "$OUT/gates/$1/gate.log"; }
```

即"具名红"= 门禁日志里出现字面量 `FAIL <断言原文>`；编译失败、超时、别的断言红都不算，分别记 `INVALID` / `RED-UNNAMED` 并 rc=1。

产物：`results.tsv`（每例 tag/rc/summary）、`mutation.log`、`gates/<tag>/gate.log`、`build-<tag>.log`、`status-begin/end.txt`，末行 `TIMELINE-MUTATIONS=PASS|FAIL`。

## 6. 跑之前必须满足的前提（踩过的坑）

1. **工作树必须干净（`git status --porcelain` 空）**。`timeline-mutations.sh:117-122` 在开头自检，脏树直接 `FATAL: the tree is dirty before the run` + rc=1 —— 因为变异要在同一棵树里改 `main.cpp`，只有 git 能证明"恢复无损"。2026-11-07 17:01 第一次拿到锁就死在这里：6 个交付文件还是 untracked。**正确顺序是先 commit 再排票**；门禁产物目录 `evidence/t85-timeline/` 已进 `.gitignore`（照 `evidence/fast/` 先例），否则跑一次就脏一次。
2. **必须整批在一次租约里**：`hostlock.sh run t85-timeline bash timeline-mutations.sh`，`HOSTLOCK_TIMEOUT` 至少 5400s。四次构建（baseline/m1/m2/restore）+ 五次门禁（baseline、m1-S1、m2-S2、final-baseline；S1/S2 各约 3 次启动）—— fresh 树还要先 configure。
3. **票会过期**：15:39 入队、16:35 到 3600s 上限被自动移除（`harness.out:61 TIMEOUT ... ticket_removed=1`）。排队久时把 `HOSTLOCK_TIMEOUT` 放大，别用短超时的前台命令等锁。
4. 单例门禁自身有 timeout（S1/S2 180s、S3/S4 120s），产品卡住会红而不是挂死。

## 7. 与 D 表的关系

- **D-26**（后退 seek 相位复位）：本门禁**不覆盖**，理由写在脚本头注释（`timeline-check.sh:43-49`）—— 那是 T61 的有意取舍（`main.cpp:9339-9346`），对其变红等于要求推翻既有决策。
- **D-27（提案，待 D 表所有者落盘）**：`--capture-tick` 被误读成 seek。整行文本在 `TIMELINE-GATE.md` §10，落盘时末列先写 `CAN-RED-UNANCHORED`（门禁尚未进主线树），进主线后改 `CAN-RED gate:timeline-check.sh#captured frame drawn at the requested tick`。
- 门禁可判红但**未接入**时的口径：本条门禁建议作为 `verify-all.sh` 第 31 步（需要 GPU 抓帧，与 step 16 同类），不进 `verify-fast.sh`（不读 demo、不开窗），也不进"只判定已有证据"的发布判定层 —— 详见 `TIMELINE-GATE.md` §7。

## 8. 本文件的证据状态（不吹）

- 断言语义、代码锚点、变异机制：**已实现**，锚点行号逐条读过源码与脚本。
- 全绿 29 条 / m1 / m2 的**本树实测数字**：**尚未产出**。forward 部分由合并窗口整链第 12 步（`TIMELINE-CHECK=PASS`）代跑；m1/m2 待整链结束后按 lead 派发单独跑。本文件与 `TIMELINE-GATE.md` 里的实测占位（§6/§8）在拿到原文后回填，未回填前不得把"设计如此"写成"已验证"。
- 环境依赖：Windows + D3D11 GPU 抓帧、SNAKE demo（`bb841c6d…cp_snakewater_final1.dem`）、`python 3.13`。换机器要改 `timeline-check.sh:76-84` 的路径。
