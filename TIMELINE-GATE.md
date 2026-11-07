# TIMELINE-GATE — 把 task-9 的时间线矩阵变成门禁

- 工作树：`D:/TF2_Native_Worktrees/t93-timeline`，分支 `gate/timeline`，基线 commit `e15577b`
- 门禁：`timeline-check.sh`（280+ 行，4 个 section，全绿时 29 条断言）
- 变异：`timeline-mutations.sh` + `timeline-patchlib.py`（m1 / m2）
- 脚本：`scripts/timeline-speed.script`、`timeline-pause.script`、`timeline-clamp.script`
- 证据：`evidence/t85-timeline/gate/`（每例一个目录：trace.txt / stdout.txt / frame.bmp / metrics.csv / cmd.txt）
- 设置：每次启动的 `--settings-file` / `--ui-settings-file` 指向 `mktemp -d` 的副本，脚本退出时 `rm -rf`（不落进证据目录，更不写 `%APPDATA%`）

## 1. 为什么要有这条门禁

task-9 用手工方式测了 31 次播放时钟：seek 落点、暂停、倍速、越界钳位。结论写进了 `D:/_shared/t85-timeline/TIMELINE-ACCEPTANCE.md`，但**没有任何一条链会因为它回归而变红**——`verify-all.sh` 的 30 步里没有一步读播放时钟（step 16 抓帧只证明"同一场景两次字节一致"，从不问抓的是哪个 tick）。

这条门禁把四个读数变成断言。它的判据是"产品的时钟仪器是否还准"，不是"画面是否正确"。

## 2. 它断言什么（4 个 section / 29 条）

| section | 断言 | 读数来源 | 防的已知缺陷 |
|---|---|---|---|
| 1 seek 精度（T=12000/20001/24001，32x） | 产品 rc=0；`capture: armed` 的 `requested`==T；`frame_tick`==T | trace `capture: armed requested=%d frame_tick=%d`（native/src/main.cpp:11271 处发射）；hold 本身在 native/src/main.cpp:9005-9019 | T26：抓帧落到"首个 ≥N 的帧"，32x 下越界数十 tick；由变异 m1 复现 |
| 2 同 tick 跨倍速（1x/8x/32x，tick 600） | 每速 rc=0、`frame_tick`==600、`playback: state` 的 `speed`==请求值；三对 BMP payload XOR==0 | trace `playback: state ... speed=%.6f`（native/src/main.cpp:1179）；BMP 比较取 header offset 10 起的 payload（timeline-check.sh:167-187） | 画面依赖"怎么走到这一帧"而非 tick；speed 读数防倍速映射被改（m2）。**像素断言抓的是确定性，不是倍速标定**，见 §4 |
| 3 暂停/恢复（1x） | rc=0；`expect what=same-tick` 的 `match`==1 且 `actual`==`marked`；resume 后 `cursor-marked` ≥1 且 ≤2 | trace `camera: mark tick=`（main.cpp:1093）、`camera: expect what=same-tick`（main.cpp:1101）、`playback: state` 的 `cursor`（main.cpp:1179） | 暂停期间时钟仍在走；恢复时把暂停的墙钟时间一次性补跳（"墙钟补跳"上界 2 tick） |
| 4 越界钳位（--start-paused） | rc=0；`seek -1` 的 `appliedTick`==0；`seek 999999` 的 `appliedTick`==`playback: state` 的 `end` | trace `playback: request ... requestedTick=%d appliedTick=%d`（native/src/playback_state.cpp:15-24）与 `playback: state ... end=%d`（main.cpp:1179） | 越界 seek 被静默改写/回绕；期望值不写死，用同一次运行报告的 endTick |

收尾规则：断言数 <4 一律 FAIL（timeline-check.sh:292-295），全绿打印 `TIMELINE-CHECK=PASS assertions=29`，否则 `TIMELINE-CHECK=FAIL assertions=N failures=M`。

**读第 1 行之前必须知道：`--capture-tick N` 不是 seek。** 它不移动播放游标：产品照常从当前位置往前播，游标越过 N 之后把 cursor 按回 N，只为抽出"画在 tick N 的那一帧"（`native/src/main.cpp:9005-9019`，T27 注释原文 "the frame drawn AT tick N, not the first frame at or after N"），trace 的 `frame_tick` 报的就是这一帧的真实 tick（`main.cpp:11271`）。真正的 seek 是另一条路：`JumpTicks` 走 transport 请求（`main.cpp:1156-1165` → `native/src/playback_state.cpp:13-31`）。因此 section 1 的断言是"钉帧准确"，**不能当作"seek 落点被验证"引用**——这是全文件最容易被误读成 seek 的地方（因为它的输入参数就叫 tick）。transport 自己的越界钳位在 section 4 读。该区分单独记为 D-27（见 §10），不只写在脚注里。

## 3. 它不断言什么（写进脚本头注释，也在这里重复一次）

1. **后退 seek 的动画相位不复位**——那是 T61 的有意取舍，不是缺陷：`native/src/main.cpp:9339-9346` 在 `sceneTick < lastObservedTick` 时清空 `animationStats` 与 `playerAnimationRuntimes`，注释写明"一条被倒回的 demo 是另一次运行"。task-9 实测了它的用户可见代价（同 tick、XOR=1,952,690、跨批可复现）。门禁若对此变红，等于要求推翻既有决策 → 债务编号 D-26。
2. **反向播放 / Stop**：产品侧 `PlaybackRequestKind::Stop` 无生产者（playback_control.cpp:172 处理、:574 名字表，探针 :304 是唯一调用者），反向只有键盘 R / UI 按钮生产者（main.cpp:1591-1601、1779-1794）；本门禁不注入输入。
3. **帧内容正确性**（材质、光照、角色姿态）属于其它门禁；本门禁只说"何时抓"和"同 tick 同字节"。

## 4. 变异证明（门禁能变红，且红在具名断言上）

规则照 `spectate-mutations.sh`：变异必须能编译（否则 `MUTATION-INVALID`，不算抓到）；门禁必须 rc≠0；必须有一条**具名** `FAIL` 落在该变异针对的断言上；每例逐字节恢复并重建，最后跑一次 baseline 证明树是干净的。

| 变异 | 改什么 | 预期红 | 备注 |
|---|---|---|---|
| m1 | 删掉 `native/src/main.cpp:9005-9019` 的 T27 hold（抓帧前把 cursor 钉在 N） | 断言 1 的 `seek 24001: captured frame drawn at the requested tick` | 复现历史 T26 语义："首个 ≥N 的帧" |
| m2 | `native/src/main.cpp:2429` 把 `--playback-speed 8` 当 4 播放 | 断言 2 的 `speed 8x: the run really used the requested speed` | 断言 2 的**像素**部分预期仍绿：tick 600 的帧是 tick 的纯函数（task-9 在 1x/8x/32x/64x 实测同 hash），所以像素断言测的是确定性，不是倍速映射。脚本把这一条明确记进 `mutation.log`，不假装像素抓到了映射错误 |

m1 的用例选 T=24001 而非 12000：越界幅度随 tick 增大，24001 在 32x 下必然跨过多个 tick。

## 5. 怎么跑

```bash
# 正式跑（必须走全机串行锁；产品启动已在脚本内固定 --audio-disabled --hidden-window）
bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run t85-timeline bash timeline-check.sh

# 只跑某几个 section（变异用）
TIMELINE_SECTIONS=1 bash timeline-check.sh

# dry run：只打印 8 次启动的计划，不启动产品、不需要 exe
bash timeline-check.sh --dry-run
# -> TIMELINE-CHECK=DRY runs=8
```

dry run 的意义：门禁可以**先接入链**（CI 预检 flags、评审看抓帧计划），等 exe 构建好再真跑；它不打印 PASS，避免"dry 也算过"。

隔离：每次启动都带 `--settings-file` / `--ui-settings-file`，指向脚本自建的 `mktemp -d` 副本（`run_product`，timeline-check.sh:149-168），每例一个子目录；脚本里唯一的 trap（`trap cleanup_tmproot EXIT INT TERM`）在退出时 `rm -rf` 整棵树。产品的退出写设置是**无条件**的（`main.cpp:11613-11629`，ui.ini 见 `native_ui.cpp:137-139`），所以这两个 flag 不是可选项；写进 `%APPDATA%` 会让下一次运行继承上一次的 recent-demo MRU（D-22）。

## 6. 本次实测（2026-11-07，t85-timeline）

（待填：baseline / m1 / m2 的 rc、断言行、耗时、exe 与 demo SHA256、锁与进程收尾）

## 7. 接入建议：进 verify-all（第 31 步），不进 release 判定层

- **建议进 `verify-all.sh`，作为第 31 步追加**（不改现有 1..30 编号）。理由：这条门禁需要 Release exe、真实 demo、GPU 抓帧，与 step 16（frame-capture-check.sh）同类；verify-all 正是"从构建到画面"的重现链，末行 `VERIFY=PASS/FAIL`，每步都钉死断言数（`verify-all.sh:1448-1452` 是范例），新增一步的接入成本最低。
- **不进 `verify-fast.sh`**：它只跑 6 个 probe、43 条断言，不读 demo、不开窗口（`verify-fast.sh:16`、`:72`），放进去会把"快"这条性质毁掉，且它本来就不启动产品。
- **不进 release 判定层**：本树 `e15577b` **没有 `verify-release.sh`**（grep 只找到 `verify-all.sh` / `verify-fast.sh` / `stability-release-check.sh`）。发布判定由 `stability-release-check.sh` 承担，而它明确"不启动任何进程，只判定已有证据"（`stability-release-check.sh:13-20`）。把一条需要 8 次产品启动的门禁放进判定层，等于让判定层自己产证据，与它的设计相反；正确做法是 verify-all 跑完后把 `evidence/t85-timeline/gate/` 一起归档，判定层读摘要。
- 接入细节：门禁全绿时打印 `TIMELINE-CHECK=PASS assertions=29`；verify-all 里按现有风格校验 `rc=0 && grep -q '^TIMELINE-CHECK=PASS' && 断言数==29`，日志落 `evidence/verify/31-timeline.txt`。

## 8. 成本

（待填实测：8 次启动 + 每次约 20-40 s）

## 9. 未验证 / 风险

- 本门禁只在本机验证（GPU 抓帧、SNAKE demo、`python 3.13` 的 `hashlib`）；换机器需要 demo/BSP 与 python 路径。
- `--capture-tick` 的 trace 行是唯一读法；若该 trace 格式变化，断言会以 `<missing>` 变红而不是静默通过（`field()` 的回退值，timeline-check.sh:134-138）。
- 变异 m2 暴露的"像素断言抓不到倍速映射"是门禁设计边界，不是缺陷；若要真正标定倍速，需要另一个读数（例如到 tick N 的墙钟时长），本门禁不接。
- section 1 的三条用例**不是** seek 验证（见 §2 粗体段）。若下一轮有人拿 `TIMELINE-CHECK=PASS` 去证明"跳转到 tick 可用"，那是把 pin 当 transport——transport 的读数是 `playback: request ... requestedTick/appliedTick`，本门禁只在 section 4 用它验越界钳位。

## 10. D 表条目（D-27，需由 D 表所有者落盘）

D 表真身在 `D:/TF2_Native_Worktrees/t86-phase0/docs/T85-DEBT-LIST.md`（不是本树，也不在本人的写域——t86-phase0 属 ledger-recon）。下面这一行按该表 §8 的五列格式写，可直接粘贴到 D-27 位置；**请 lead / ledger-recon 落盘**。落盘前该表末列必须是 `CAN-RED-UNANCHORED`：`timeline-check.sh` 目前只在 `t93-timeline`（分支 `gate/timeline`），不在主线树，`debt-claim-check.sh:132-137` 会以 missing-script 判红；等门禁进主线后改成 `CAN-RED gate:timeline-check.sh#captured frame drawn at the requested tick`（该字面串就在 `timeline-check.sh:262`）。

```markdown
| D-27 | `--capture-tick N` 被误读成"seek 到 N"：它只把游标按回 N 以抽出该帧，不移动 transport | 部分覆盖（pin 已断言且可判红；"误读"本身无断言） | **现象**：门禁与报告里"N 帧抓到了"读起来像"seek 落到 N"，但两者是不同代码路径。**语义**：`--capture-tick` 让产品照常向前播，当 `g_playback.tick > N` 时把 cursor 按回 N 并 `noteExternalCursor()`，只影响被画的那一帧（`native/src/main.cpp:9005-9019`，T27 注释原文 "the frame drawn AT tick N, not the first frame at or after N"），trace `capture: armed requested=%d frame_tick=%d` 报抽帧走到哪（`main.cpp:11271`）；真 seek 走 `JumpTicks` → transport 请求（`main.cpp:1156-1165` → `native/src/playback_state.cpp:13-31`，trace `playback: request ... requestedTick= appliedTick=`）。**为什么危险**：把 `timeline-check.sh` 的 section 1 当成 seek 证据，会掩盖"transport 落点没人验"这一事实；section 4 才读 transport 的越界钳位。**证据**：`t93-timeline/timeline-check.sh:43-57`（头注释专段）、`TIMELINE-GATE.md` §2 与 §9、task-9 的 `TIMELINE-ACCEPTANCE.md` §1（seek 精度另有实测）。**归属**：t93-timeline（gate/timeline）→ 进 verify-all 第 31 步时一并入库。 | CAN-RED-UNANCHORED timeline-check.sh 只在 t93-timeline 分支，未进主线树；落地后改 CAN-RED gate:timeline-check.sh#captured frame drawn at the requested tick |
```

词面依据（粘贴前可复核）：

- `timeline-check.sh:262`：`assert_eq "seek $T: captured frame drawn at the requested tick" "$ft" "$T"` —— 注意断言名里就有 "seek"，这正是误读来源；名字说的是"抓的那一帧画在请求的 tick 上"，不是"seek 请求被接受"。
- `timeline-check.sh:43-57`：头注释 `--capture-tick IS NOT A SEEK` 段。
- `native/src/main.cpp:11271` 与 `9005-9019`：两条路径的代码锚点。
