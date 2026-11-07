# T93 门禁校准：七条「修后仍红」的断言，错在哪、改成什么

这份文件记录 `playback-error-paths-check.sh` 第一次在**修好的二进制**上跑时出现的 7 条红。
结论先说：**7 条全是断言自己写错，不是产品没修好**——每一条都在同一份 trace 里留下了反证读数。
校准后 = 25 pass / 7 red → 全绿；判断依据不是「改到能过」，而是**同一条新断言在修前二进制上仍然红**（见 §3 回放表）。

## 0. 两轮基线（都可复跑）

| 轮次 | 二进制 | 结果 | 用时 |
|---|---|---|---|
| tag=pre | 冻结 t79-integration @ e15577b（修前） | **13 pass / 19 red**，A1/B/C/D/E 全红 | 556 s |
| tag=green | t93-playback @ bbf3559（修后） | **25 pass / 7 red** ← 本文校准对象 | 225 s |

两者的调用完全相同：`bash playback-error-paths-check.sh --tag pre|green`（`TF2_EXE` 指向对应二进制）。

## 1. 七条红一表看完

| # | 断言 id | 第一次绿的原文 | 错在哪（一句话） | 新写法 |
|---|---|---|---|---|
| 1 | `B-exit` | `rc=1 want 0` | 脚本 `camera: accept failures=1`，失败来自我漏了 `mark-tick` | 脚本补 `mark-tick`；rc 断言不变 |
| 2 | `B-paused` | `has "camera: mode applied=free-look tick=400 paused=1"` | tick 写死 400，实际落 403/413 | 拆成两条：`paused=1` + tick 与 snapshot 相等 |
| 3 | `B-script` | `failures=0` | 同 #1（mark-tick 缺失导致 -1 比对必失败） | 同 #1 |
| 4 | `B-clock` | `snapshots at tick=413/413 (want 400/400)` | 同上：把「同一个 tick」错写成「tick=400」 | 断言 `applied == before == after` 且 `expect-same-tick match=1` |
| 5 | `C-burst` | `one audio: advance saw=750 (want <=60)` | 窗口从 trace 头开始，把**装载期首帧的 750 条**算进来 | 只统计 `audio: seek reset` **之后**的 advance |
| 6 | `D-tick0` | `fnum "audio: advance tick=0 " "seen="` → `-` | 断言写死 tick=0；`--start-paused` 首帧直接落在 tick=16 | 改取**第一条** advance 的 `seen>=700` |
| 7 | `D-dup` | 同 #6 的 `dup=` → `-` | 同 #6 | 改取第一条 advance 的 `dup=0` |

## 2. 逐条：原写法 / 错在哪 / 新写法 / 为什么新写法不是「迁就实现」

### 2.1 `B-exit` (`rc=1`) + `B-script` (`failures=1`)

- **原写法**：B 用例脚本里有 `snapshot before` … `expect-same-tick` … `quit`，但**没有 `mark-tick`**。
  脚本 `expect-same-tick` 的实现是 `g_playback.tick == g_scriptTickMark`（main.cpp:1112-1121），而 `g_scriptTickMark` 的初值是 **-1**（main.cpp:765）。
  没有 `mark-tick` 就永远比 -1 ⇒ 必 `fail("tick-moved")` ⇒ `camera: accept failures=1` ⇒ `quit` 把脚本失败换算成进程 rc=1（main.cpp:1308-1319）。
- **错在哪**：这是**我的脚本缺一步**，产品侧行为完全正确（trace: `camera: accept step=12 cmd=expect-same-tick result=fail detail=tick-moved`）。
  它不是「产品没冻结 tick」——同一份 trace 里两张 snapshot 的 tick 都是 413。
- **新写法**：脚本在 `pause`/`frames 4` 之后加 `mark-tick`；`B-script` 仍是 `failures=0`，`B-exit` 仍是 `rc=0`。
- **为什么不是迁就实现**：加了 `mark-tick` 之后，真正要证明的东西才被证明——`mark` 记下的 tick 与 `expect-same-tick` 时刻的 tick 相同（`match=1`）。
  如果产品在暂停中偷偷推进 tick，这条会红。它比原来那条**更严**，不是更松。

### 2.2 `B-paused` + `B-clock`（tick 写死 400）

- **原写法**：`has "camera: mode applied=free-look tick=400 paused=1"`；`snapshots at tick=$TB/$TA (want 400/400)`。
- **错在哪**：脚本的 `tick 400` 语义是**「等到至少 400」**（main.cpp:852-865：未到就回滚 index 继续等），暂停指令在下一帧才生效。
  实测两次落点不同：pre 跑 403，green 跑 413（`evidence/t93-playback/*/green-B-freefly-paused.trace`：`camera: snapshot label=before … tick=413`）。
  把 tick 写死 = 把「暂停是否冻结时钟」偷换成「机器多快」——这是**测试机器速度的门禁**，本来就是错的门禁。
- **新写法**（两条互相独立）：
  1. `PA=$(fnum … "camera: mode applied=free-look" "paused=")` 必须 = 1（**模式是在暂停帧上被应用的**）；
  2. `applied tick == snapshot before tick == snapshot after tick`，且 `expect-same-tick match=1`（**同一个 tick，时钟没动**）。
- **为什么不是迁就实现**：断言的核心不变量从「等于某个常数」变成「三者相等 + 冻结」，产品任何一次「暂停中 tick 前进」或「模式没在暂停帧应用」都会红。
  迁移后仍然**只**接受实测过的形态：pre 二进制上这两条依然红（`camera: mode applied=…` 行根本不存在，`fnum` 返回 `-`）。

### 2.3 `C-burst`（窗口起点错）

- **原写法**：`maxseen $T` —— 全 trace 所有 `audio: advance` 的最大 `seen`，要求 `<=60`。
- **错在哪**：一次普通播放的**第一帧**本来就要补 0..N 的装载期区间，实测 750 条（与 T85 审计 a1 的 `audio: advance tick=0 seen=746` 同一现象）。
  这个 750 与 seek 无关：它在 `audio: seek reset from=34 to=3000` **之前**。把它们混在一起，等于用一个跟缺陷无关的数字判缺陷。
- **新写法**：`maxseen_after $T "audio: seek reset"` —— 只统计 reset 行之后的 advance（本机实测 28）；
  **并且**：如果 trace 里根本没有 `audio: seek reset` 行，就退回全 trace 最大值（否则「不打这行」反而能过）。
- **为什么不是迁就实现**：红/绿分离度实测——同一断言，green 二进制 = 28，pre 二进制 = 1494（回落逻辑生效，因为 pre 不打 reset 行）。
  亦即：**缺陷在时这条必红，修好后这条才绿**，不是靠放宽阈值。

### 2.4 `D-tick0` + `D-dup`（tick 写死 0）

- **原写法**：`fnum $T "audio: advance tick=0 " "seen="` ≥ 700；同法取 `dup=` = 0。
- **错在哪**：`--start-paused` 的 resume 边沿到第一次 `advance` 之间，循环已经跑到 tick=16（`audio: advance tick=16 cursor=16 seen=750 played=0 … dup=0`），
  根本没有 `tick=0` 那一行。断言取不到值 ⇒ `-` ⇒ 红。
- **新写法**：`firstf $T "audio: advance" "seen="` ≥ 700 且 `dup=0`——即「resume 之后第一条 advance 必须把 tick 0 的 746 条带上、且不把重入 tick 记成重复」。
- **为什么不是迁就实现**：pre 二进制首条 advance = `tick=16 seen=4`（tick0 的 746 条全丢），green = `tick=16 seen=750`。
  **同一个 tick、同一个字段，4 vs 750**：这正是缺陷与修复的分界，与新写法完全对齐。

## 3. 校准的验证方式：先在已提交的 trace 上回放（不占锁）

改断言不需要再起产品：把新 awk 表达式套在 `evidence/t93-playback/*.trace` 上就能确认它区分不区分得开。
实测回放（`fnum`/`firstf`/`maxseen_after` 都是门禁里的同一份实现）：

| 断言 | green 二进制 | pre 二进制（冻结 t79） | 判断 |
|---|---|---|---|
| `C` seek 后最大 seen | 28 | 1494（回落全 trace） | ≤60 → 绿 / 红 ✔ |
| `D` 首条 advance `seen` | 750（tick=16） | 4（tick=16） | ≥700 → 绿 / 红 ✔ |
| `D` 首条 advance `dup` | 0 | 0 | 两者都绿，但该字段不是本缺陷的分界，保留为「不许重入重复」的守门 |
| `B` `camera: mode applied` tick | 413（paused=1） | `-`（无此行） | 有三者相等 → 绿 / 红 ✔ |
| `B` snapshot tick before/after | 413 / 413 | 403 / 403 | 相等 → 绿 / 红 ✔（pre 靠上面那行判红） |
| `B` `modeApplies` | 1 | `-` | ≥1 → 绿 / 红 ✔ |
| `A1` rc | 2（0 s） | 124（300 s 超时） | =2 → 绿 / 红 ✔ |

## 4. 校准没有放松的东西

- **不变量没动**：A1 必须 rc=2 且无窗口/无 map lookup；A2 必须照常启动；E 必须 `scheduled=5323` / `events_seen=759` / `audio_device_opened=0` / `modeApplies=0` / 无 seek reset。
- **卫生没动**：每次启动强制 `--hidden-window --audio-disabled`，缺一个 `return 99` 拒跑；`--settings-file`/`--ui-settings-file` 一律指向 `mktemp -d`（跑完 `rm -rf`），不写真实 `%LOCALAPPDATA%/TF2 Demo Player/`（D-22）。
- **没有新增 case、没有新增选项**：改的只是 B/C/D 三组的断言表达式与 B 的脚本一行。
- **仍然不证明的事**（别当结论读）：真实设备出声、像素差「像不像轨道机位」、跨 seek 的 `audio: summary` 总量（reset 会清零，必须逐条累加 `audio: advance`）。