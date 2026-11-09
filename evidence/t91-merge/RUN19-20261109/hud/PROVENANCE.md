# T97 HUD 活读数门禁 —— 这份读数是谁、在什么版本上跑出来的

本文只解释 **evidence/t97-hud/readings.txt 这一份读数** 的来历与边界；它不改门禁、不重跑任何东西。
读数是 `hud-live-check.sh` 的正向（forward）运行产物；同一份文件里**不含**变异（--mutation）结果。

---

## 1. 运行身份（谁、在哪棵树、哪个 exe、什么锁）

| 项 | 值 |
|---|---|
| 运行者 | lead 的 RUN19 窗口脚本 `D:/_shared/EXEC-2026-11-09/run19-keypath3.sh`（hud2 阶段），不是我 |
| 锁 | hostlock **owner=t91-keypath**，ACQUIRED **2026-11-09 22:25:39**；hud2 阶段从 scratch 标签 **22:26:50** 跑到日志落盘 **22:32:36**（约 5 分 46 秒） |
| 仓库 | `D:/TF2_Native_Worktrees/t91-integration`，branch `integration/t91` |
| HEAD | **f2c4fbd89193969729a7155aae2c798af37b0902**（"fix(gates): the exit restore restores this window's edits, and hud scratch is absolute"） |
| 工作树 | `dirty=0`（跑之前；run-context.txt 记录），`native_tree=7378ca192be9684e5aa6ee851910f06c7db0e6a2074f5f4545cdf157ecef8192` |
| 构建 | 同一窗口内 `build-target.sh tf2_demo_native brush_entity_probe world_geometry_probe entity_overlay_probe entity_protocol_probe entity_model_probe`，`build_rc=0` |
| 产品 | `native/build-nmake/tf2_demo_native.exe`，**2,838,528 B**，mtime 2026-11-09 22:26:36，sha256 **37388f4ad5bd82c699529c828b09c78b12d628b77ddbbb17d267b24d6d410aff** |
| 门禁 | `hud-live-check.sh`，812 行；盘上文件（CRLF）41,365 B，sha256 **18ea81655832a27f79cbe53fa3d4c3885a4fab2da1749324ef36a0be169b6d34**（= readings 里记的那个）；git blob `HEAD:hud-live-check.sh` 40,553 B，`git hash-object` = **f822a5fcd19356531c5a314c60358fe0d9c51f37**，`git show HEAD:… \| sha256sum` = **7a789d6e0173418417cd9a1cbdba6796acbf8aebda61e156c0ee7d75973485f2**（三值差异见 §3.4，纯行尾） |
| demo | `D:/TF2_Demo_Player/testdata/demos/bb841c6d379ff7c40d0c8baf99f59d8d_matcha-20260927-1347-cp_snakewater_final1.dem`（51,834,617 B，sha256 308cc145…；hud-live-check.sh:102） |
| 启动参数 | 由门禁自己拼：`--tf-root … --audio-device … --audio-disabled --hidden-window --demo … --capture-frame … --metrics-file … [--hud-live]` + 固定相机参数；每个 run 有自己的 settings.ini / ui.ini 副本（见 scratch 目录） |

产物与哈希（都在这台机器上，除 readings.txt 外**不入库**）：

| 文件 | sha256 |
|---|---|
| `/d/_shared/EXEC-2026-11-09/RUN19-KEYPATH/hud2.log`（5,041 B，94 行） | `c004d0921a6b71c381f91115a183c1b6433309b6fa3898b64bbae1aeaebf2fc1` |
| `/d/_shared/EXEC-2026-11-09/RUN19-KEYPATH/run-context.txt` | `c50c0de9530fbeeb0613fe533c67e4b400d98d77687c125c3995e94f239e2d88` |
| `/d/_shared/EXEC-2026-11-09/RUN19-KEYPATH/build.log` | `abadbf4a9ebb329b0681117651a3e6bb74469e9ad5aaf4027198a4ff1f0ec453` |
| `/d/_shared/EXEC-2026-11-09/RUN19-KEYPATH/exe-sha.txt` | `90bace1269197d626764c35d1fc22e44c68ffb199f0a77a33340890dfc1c0b96` |
| `/d/_shared/EXEC-2026-11-09/RUN19-KEYPATH-window.log`（锁窗口 4 行头 + 各阶段 token） | `e5460d475d6750ebdbe8a72f85eca00d7a328d7a186eb42180f578077d2f8be5` |
| `evidence/t97-hud/readings.txt`（本文件解释的对象，765 B） | `aab9bdc829a6d517cd205021a748c804bf53b6ab4c1105d047343135fc946ccf` |
| `evidence/t97-hud/mutation/readings.txt`（**旧**，见 §3.2） | `187c8736e8259b4fce904bf26eb9e65114d6a508c3fd7a49ae202ca8abb6c4b3` |

本次正向运行一共启动产品 **6 次**，全在 `.scratch/t97-hudlive/20261109222650-270199/`：
`live`（22:27:41）、`off`（22:28:30，--hud-off）、`suppress`（22:29:45，evidence 模式无 --hud-live）、`advance`（22:30:50，seek 到 9000 tick）、`resized`（22:31:42，窗口 784x561）、`live2`（22:32:31，第二个进程）。
每个 run 各有一份 `<name>.{trace,log,bmp,csv,settings.ini,ui.ini}`；scratch 目录不入库。

---

## 2. readings.txt 逐字段：它到底断言了什么

文件只有 18 行，但它把 6 次产品运行的结论压在一处。逐行（行号按 readings.txt）：

| readings 行 | 字段 | 来自哪个 run | 断言的是（对应 hud2.log 的 OK 行） |
|---|---|---|---|
| 1-2 | `exe=` / `exe_sha256=` | 构建 | 这份读数对应的产品字节（上面表里的 37388f4a…）；换 exe 后读数不再可比 |
| 3 | `gate_sha256=` | 生成时 | 生成这份读数时**盘上**门禁文件的 sha256（CRLF，见 §3.4） |
| 4 | `demo_sha256=` | 生成时 | 输入 demo 的身份（snakewater，308cc145…）；换 demo 读数不可比 |
| 5 | `tick=0` | live/off/suppress/resized/live2 | 暂停帧抓在 demo 的 **tick 0**（相机冻结，帧才可逐像素比） |
| 5 | `advance_tick=9000` | advance | 刷新实验把 demo 推进到 **tick 9000**，正是"读数会随 demo 前进而变"的那个位置（log:66-80） |
| 6-8 | `reading1/2/3=` | live | 产品**发布**的三行文本原文：`TIME 10:00 PAUSED` / `RED ?  BLU ?` / `NAME ?  HP 1  ?  ?`（log:10；第 8 行刻意是 `NAME ?` 而不是编出来的名字，log:16） |
| 9 | `source=round-timer glyphs=47 region=24,24,288,60` | live | 时钟来自回合计时器；发布文本 47 个字形；面板区域是**像素锚定**的 24,24,288,60（log:21-23） |
| 10 | `publishes=1` | live | 普通运行至少发布 1 次、且 enabled=1（log:11-12） |
| 11 | `advance_text1=TIME 4:00 advance_updates=2 advance_skips=510 advance_source=round-timer` | advance | 进到 tick 9000 后时钟变 `TIME 4:00`（240 < 600，log:69-70）；512 次调用 = 2 次重建 + 510 次跳过（log:72-78），"没变的帧不重复上传" |
| 12 | `name=unavailable reason=no-userinfo` | live | 名字**缺失时如实说缺**，理由是可机械检查的 `no-userinfo`（log:13-14） |
| 13 | `resized_region=24,24,288,60` | resized | 窗口从 1264x681 缩到 784x561，区域一个像素都不动（log:84-87） |
| 14 | `pixels: outside=0 inside=3504 glyphBad=0` | live | 发布的区域**外面**一个像素都不画（outside=0），里面 3504 个亮像素，且逐字形与字体表比对 0 处不符（log:41-45）；同一行的负控还证明这个验证器会拒掉"没画字"和"画错字"的帧（log:46-52） |
| 15 | `hud_off: quads=0 primitives=0 drawn=0 rects=0` | off | `--hud-off` 确实把整块 HUD 拿掉（四边形/图元/绘制数/布局矩形全 0，log:54-59） |
| 16 | `mode=forced` | live | 该 run 声明的是强制打开（log:9） |
| 16 | `suppress_reason=evidence-run` | suppress | evidence 模式**没有** --hud-live 时，产品自己说为什么关（log:62-63） |
| 16 | `suppress_enabled_lines=0` | suppress | 抑制时**一行 enabled=1 都不发布**（log:62） |
| 16 | `suppress_frame=identical` | suppress vs off | 被抑制的帧与 `--hud-off` 帧**逐字节相同**（log:65；盘上 `cmp suppress.bmp off.bmp` rc=0） |

一句话：readings 里的每个数都是"产品自己说 + 帧里量到"两条腿同时站住的，不是从日志文本里抄的期待值。

---

## 2.5 这一窗口里世界几何预算为什么没有读数

这一节只解释"同一窗口里 budget 阶段那条 rc 为什么是 137"，它是缺读数，不是成绩。

- **嵌套等锁死锁**：`RUN19` 外层已经持有 hostlock（owner=t91-keypath），而 `world-geometry-budget-check.sh:129-142` 自己还会通过 `locked()` 走 `bash "$LOCK" run "$LOCK_OWNER" …`（只有 `HOLD_LOCK=1` 才原地执行）；外层没传 `HOLD_LOCK=1`，于是它从 **22:32:42 一直排到 22:45**，13 分钟零读数（budget.log 只有两行 seeded + `== cp_snakewater_final1 ==`）。
- **rc=137 = 被 SIGKILL**：lead 先 kill 掉嵌套 waiter，脚本才往下走到 cp_sunshine 又去等锁；整段 `timeout 2400` 到点被 kill，`budget.rc` 记 **137**，`budget.log` 留 `capture cp_snakewater_final1/forward failed rc=137` / `FAIL cp_snakewater_final1: no capture`。**这一阶段没有任何预算读数**，故本文与 RUN19 都不对它下判断。
- **之后的做法**：budget 裸跑重排（锁外）或在锁内显式传 `HOLD_LOCK=1 LOCK_OWNER=<owner>`。hud / timeline / build-identity 三个门禁只在注释里写了 hostlock 命令、自身不取锁（hud-live-check.sh 无 `hostlock.sh run`；timeline-check.sh:98、build-identity-check.sh:78 仅是用法注释），所以它们能在本窗口内正常跑完——hud2 的 65 条 OK 不受这件事影响。

---

## 3. KNOWN LIMITS（读这份读数时不许越过的地方）

### 3.1 整链里的第 14 步只证明正向
`verify-release.sh:89` 的步表里 `hud-live` 是第 14 步，调用形式是 `bash hud-live-check.sh`，**不带 `--mutation`**（step 只设 `HUD_LIVE_CHECK_OUT`，verify-release.sh:386；没有 per-step 参数机制）。所以：
- 整链读到 `HUD-LIVE-CHECK=PASS` 时，被证明的只有**正向 65 条断言**；
- 三个负控（m1-drop-text / m2-freeze / m3-fake-name）**不在这条链里**，它们是离线跑一次 `--mutation` 的产物；
- 这与 D-55 同型：PASS 说明"当前二进制满足读数"，不说明"坏二进制会被抓住"。链里唯一的负控来自门禁自己在 §3 里做的两个像素负控（log:46-52），那只覆盖像素验证器，不覆盖产品侧改动。

### 3.2 变异证据的时点：旧 gate、旧 exe
`evidence/t97-hud/mutation/readings.txt`（mtime 2026-11-09 21:46，sha 187c8736…）记录的是：
- `gate_sha256=d29bb95c7f63da7e8c8bf29a22123057d3e0c054dba32f5732834d76ddfc569f` = 提交 **8290374** 时的 blob；
- `exe_sha256=65533cb3e66704a560842677e3fabc321554d1b27dadafc00a7a493deadb6a51`（当时构建的二进制）。

而当前树是 **f2c4fbd**、门禁 blob `f822a5f`（内容 sha 7a789d6e…）、exe **37388f4a…**。8290374→f2c4fbd 对 hud-live-check.sh 的改动只有「SCRATCH 绝对化 / 去掉 $PWD 拼接 / EXIT 恢复本窗口的编辑」，不碰断言与变异补丁，但**证据时点已经不同**。

因此在**重跑之前**，任何引用该文件的文字只能写：

> 负控在 8290374 的 gate（d29bb95c）上跑过；当前 gate（7a789d6e…）/ exe（37388f4a…）尚未重跑。

重跑计划（等锁，未执行）：
```
HOSTLOCK_TIMEOUT=3600 bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run <owner> \
  bash /d/TF2_Native_Worktrees/t91-integration/hud-live-check.sh --mutation
```
估计 10–15 分钟（3 个 case × 一次 `build-target.sh tf2_demo_native` ≈ 90–120 s + 每个 case 只跑必要小节 1–4 分钟）。跑完必须自证：gate blob、exe sha、三条变异各自的 `RESTORED-IDENTICAL`、末行 `MUTATION-SUITE=PASS`。

### 3.3 交互窗口渲染 UNVERIFIED
本门禁的所有帧都是 `--hidden-window` + 固定相机 + 暂停/定时 seek 抓的离屏帧。它证明"产品画出的像素等于它发布的文本"，**不证明**：
- 真人窗口里拖动/缩放/切分辨率时的时序与重绘；
- 真实输入（鼠标/键盘）驱动下 HUD 的响应（本仓库没有点击类门禁，见 UI 能力审计）；
- 多显示器 / DPI 缩放 / 不同 GPU 驱动下的字形与布局。

### 3.4 gate_sha256 是 CRLF 盘上哈希，不是 blob 哈希
同一份 `hud-live-check.sh` 有三个"sha256"，互不相等只是因为行尾：
- **18ea8165…** = 盘上文件（CRLF）原始字节的 sha256，**readings 里记的就是这个**（生成那一刻的文件字节）；
- **7a789d6e…** = `git show HEAD:hud-live-check.sh | sha256sum`（LF、40,553 B）；
- **f822a5f…** = `git hash-object hud-live-check.sh`（git blob id，和 `git rev-parse HEAD:hud-live-check.sh` 相同）。
三者都能用，但复核时必须说清用的是哪一种；不要用 `git show | sha256sum` 去对 readings 里的数而判"文件被改过"。

---

## 4. 复核命令（全部只读，不需要锁、不启产品）

```bash
cd /d/TF2_Native_Worktrees/t91-integration
L=/d/_shared/EXEC-2026-11-09/RUN19-KEYPATH
S=.scratch/t97-hudlive/20261109222650-270199

# (1) 65 OK / 0 FAIL / 7 节
grep -c '^  OK '   $L/hud2.log      # 65
grep -c '^  FAIL ' $L/hud2.log      # 0
grep -c '^=== [0-9]' $L/hud2.log    # 7（第 1 行是标题，不算节）

# (2) 三条 suppress 读数（对应 readings.txt 第 16 行）
grep -ac 'hud: live enabled=0 reason=evidence-run drawn=0' $S/suppress.trace   # 1
grep -ac 'hud: live enabled=0 reason=cli-hud-off drawn=0' $S/off.trace         # 1
cmp "$S/suppress.bmp" "$S/off.bmp" && echo "suppress == hud-off (byte identical)"   # rc=0
cmp "$S/live.bmp" "$S/off.bmp" >/dev/null; echo "live vs off rc=$?"            # rc=1（必须不同）

# (3) 帧的身份
sha256sum $S/live.bmp $S/live2.bmp $S/off.bmp $S/suppress.bmp $S/advance.bmp $S/resized.bmp
#   live     = d32bdb1d85d972f23cd4fe4a1db3e26a1dc624c1fe0032e2baa4fb29c7ee98f8
#   live2    = d32bdb1d85d972f23cd4fe4a1db3e26a1dc624c1fe0032e2baa4fb29c7ee98f8  （两进程同帧）
#   off      = afe6296d5c40f6aed968f9c870f239dd1b9fb3860a092905b7881a301787b8e3
#   suppress = afe6296d5c40f6aed968f9c870f239dd1b9fb3860a092905b7881a301787b8e3  （抑制 == hud-off）
#   advance  = 5b4a508d351f0c781c9583312cfa03025f748ee896e83d7f51815917f9b12d0e
#   resized  = 6064d8c3208fcc9822a1b05a6f2f5beed59015d9c980a687dab03d77028fc50f

# (4) 读数的来源行（trace 里的产品自述，不是日志里的期待值）
grep -a 'hud: live summary' $S/live.trace $S/advance.trace
#   live:    calls=2 updates=1 skips=1 published=1 …
#   advance: calls=512 updates=2 skips=510 published=2 … lastText1="TIME 4:00"

# (5) 三份读数/日志的 sha256 对 §1 的表
sha256sum $L/hud2.log evidence/t97-hud/readings.txt evidence/t97-hud/mutation/readings.txt
```

> 现状提醒：`evidence/t97-hud/readings.txt` 目前是工作树里的 **未提交修改**（`M`），与本文件一起由 lead 在窗口结束后提交；
> `evidence/t97-hud/mutation/readings.txt` **本次没有改动**，仍是 §3.2 说的旧时点。
