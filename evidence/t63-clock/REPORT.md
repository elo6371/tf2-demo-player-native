# T63-F1 时钟 / 双 tick 空间修复报告

任务: task-10  |  负责人: Lead（原 t63-sourcetv，子智能体后端不可用后由 Lead 接手）
基线: p0-entity-protocol @ 99eeea2  |  分支: task/t63-clock @ 5ee9642  |  合并: 1b99fa2
工作区: D:/TF2_Native_Worktrees/t63-clock  |  合入树: D:/TF2_Native_Test

## 1. 目标与非目标
目标：把 demo 的两个 tick 空间分开，使场景查询用对的 tick；新增 demo-clock-check.sh 门禁，
      用 A1-A6 钉住两个原点、快照一致性、view 轨道有序性、相机眼点与负控；m1-m4 变异必须各自变红。
非目标：不改相机跟随逻辑（T66）、不改 HUD 读取（T67）、不改事件解码（T68）；不跑全链。

## 2. 根因（实测，不是推断）
产品原来把 `firstPacketTick` 当作“服务器 tick 原点”，取的是第一个 command 1 或 2 的 entry tick。
那是 **signon** 的 entry tick，不是数据包空间，也不是服务器 tick 空间。

独立 python 解析 demo 命令流（不依赖产品）：
- snakewater bb841c6d…：cmd1(signon) tick=10252；第一个数据包 entry tick=1（无 svc_NetTick，只有 type 23）；
  第一个带时钟的数据包 entry tick=2 → svc_NetTick=10321；之后每包 +1。
- bagel 4a9bfb92…：cmd1(signon) tick=1；第一个数据包 entry tick=1 → svc_NetTick=56148；之后每包 +1。

产品旧公式 `sceneTick = firstPacketTick + playback.tick`：
- snakewater playback 0 → 10252，而第 0 行数据包服务器 tick 是 **10320** → 早 **68 tick**
- bagel playback 0 → 1，而第 0 行是 **56148** → 早 **56147 tick**（14.6 分钟）

（提交信息里写的 67 / 56146 是用 net_minus_entry=10319/56147 反推的近似值；以本报告的行 0 对比为准。）

第二个缺陷：同一个 sceneTick 同时喂给两个不同空间——
- `queryEntitySnapshotAtOrBeforeTick`：实体历史按 **server tick**（svc_NetTick）建索引；
- `findObserverViewAtOrBeforeTick`：view samples 按 **entry tick**（1,2,3…）建索引。
并且 viewSamples 同时收 command 1/2 的 cmdinfo 样本，POV demo 会乱序；`std::upper_bound` 对乱序输入
返回空 → 相机读回“该 tick 之前没有视图”。

## 3. 改动（4 个源文件 + 1 个门禁）
- native/include/demo_header.h：新增 firstDataEntryTick / firstDataNetTick / signonViewSamples /
  viewSamplesOutOfOrder / viewSamplesOutOfOrderAfterRepair / viewSamplesDuplicates / packetServerTicks /
  packetServerTickKnown / packetServerTickOwn / dataPacketCount / serverTickKnownCount / serverTickHeldCount /
  fixAngleSamples / viewSamplesOrdered。
- native/src/demo_header.cpp：按持久 svc_NetTick 值建 per-data-packet 服务器 tick 表（丢包不能让表整体平移）；
  view 修复 = 丢 signon 样本 + FixAngle 样本移出 + stable_sort + 同 tick 去重保留**最后一个**。
- native/src/main.cpp：sceneEntryTick（view samples）与 sceneServerTick（实体/弹道）分离；服务器 tick 走
  表查行（按 playback 行号），不再用偏移；未知时具名上报（reason=no-data-packets / serverTickKnown=0），
  不回退到 signon tick。
- native/tools/demo_open_probe.cpp：发布表、修复计数、实体历史 checkpoint/archive 间隔、快照 resolved tick 与 stale。
- demo-clock-check.sh（526 行）：A1-A6 + m1-m4，见 §5。

## 4. 构建
命令（锁内）：bash build-target.sh tf2_demo_native demo_open_probe entity_overlay_probe projectile_track_probe
BUILD_RC=0，error C 计数 0（evidence/t63-clock/build5.log）。
二进制：demo_open_probe.exe sha256 44acf257…，tf2_demo_native.exe sha256 89968670…（binaries4.sha256）。

## 5. 门禁读数（证据/t63-clock/gate-forward6.log）
DEMO-CLOCK-CHECK=PASS，GATE_FORWARD_RC=0，**63 OK / 0 FAIL**。
- A1 snakewater：tick0 entryTick=1 serverTick=10320 known=1；tick1 entryTick=2 serverTick=10321 known=1。
- A2 bagel：tick0 entryTick=1 serverTick=56148 known=1。
- A3 tick20000：entryTick=20001 serverTick=30320（> 线性偏移 30319，证明表不是 offset+playback）；
  snapshot status=1，states=2048，stale=794 与探针同 tick 的 stale 相等，且 794 <= 实体历史 archive 间隔 1035。
- A4 S1 view_ordered=1、after_repair=0、最后视图 tick=64352；S5 最后视图 tick=77466、dropped=0。
- A5 P1（pl_frontier POV）camera_found=1、camera_sample_tick=600、原点 1784.66,-3594.82,239.965，
  与产品眼点一致（SourceTV demo 的 cmdinfo 是 76 个零字节，无法做眼点证明，所以用 POV）。
- A6 负控：bagel server tick 1 = before-history（states=0）；56148 = checkpoint（2048 states）。

## 6. 变异（证据/t63-clock/gate-mutation10.log）
MUTATION-CAUGHT=PASS（4/4 变红并字节还原）：
- m1 signon entry tick 当 playback 原点 → “entry-tick reading moved”，restored main.cpp byte-identical
- m2 实体查询用 entry tick → “snapshot reading moved”，restored main.cpp byte-identical
- m3 去掉同 tick 去重 → P1 after_repair=504（期望 0），restored demo_header.cpp byte-identical
- m4 恢复 65536 样本上限 → S5 view_samples_dropped=7585（期望 0），restored demo_header.cpp byte-identical
还原后基线：m4r S5 view_samples_dropped=0。

注：m3 前两次尝试（把 signon 样本放回 / 去掉 stable_sort）都是**无效变异**——partition + 去重已经让数据
非降序，sort 是防御性步骤；有效变异必须动真正起作用的那一步（去重）。这条已记入 playbook 计划。

## 7. 已知限制 / NOT VERIFIED
- bagel 没有 command==1：first_signon_entry_tick=INT_MIN 是哨兵，门禁**不**断言它。
- SourceTV demo 的 cmdinfo 全零（76 字节），相机眼点只能在 POV demo 上证明。
- POV demo 的 view samples 有 504 个相邻重复 tick（entry 空间），修复保留最后一个；不同 POV 录制的
  重复分布未逐一统计。
- 未验证：其它 1746 个 demo 的时钟偏移分布（只测了 S1/S5/P1 三个）；fixangle 样本在真实 POV 录制里的
  语义只按源码分类处理。
- **受影响回归门禁 outline-check.sh / trajectory-overlay-check.sh 的重跑结果见 §8**（tick 原点变了，
  这两个门禁的像素证据必须重跑；它们的旧验收证据是在错误原点上自洽的，所以像素门禁本身抓不到这个缺陷）。

## 8. 受影响回归（合并树 D:/TF2_Native_Test）

时钟原点一变，凡是把 tick 写进像素证据的门禁都必须重跑；它们**自身的像素门禁抓不到这个缺陷**，
因为旧证据是在错误原点上自洽的。逐项结论如下（2026-10-26 归档时补齐，全部实测）。

| 门禁 | 日志 | 结论 |
|---|---|---|
| demo-clock-check.sh | evidence/release-gates/demo-clock-p0.log | **DEMO-CLOCK-CHECK=PASS**，63 OK / 0 FAIL |
| outline-check.sh | evidence/release-gates/outline-p0-clock.log | **OUTLINE-CHECK=PASS**，88 OK / 0 FAIL |
| trajectory-overlay-check.sh | evidence/release-gates/trajectory-p0-clock2.log | **TRAJECTORY-OVERLAY=PASS**，40 OK / 0 FAIL |
| game-event-check.sh | evidence/release-gates/game-event-p0.log | **GATE=PASS**，112 OK / 0 FAIL |

### 8.1 一次“拒绝读数”和一次真实失败，都保留在证据里

1. **trajectory 的第一份日志（trajectory-p0-clock.log，214 B）不是读数，是拒绝读数**：
   它只有一行 `STALE-BINARY FAIL: native/build-nmake/projectile_track_probe.exe is older than:
   native/src/projectile_track.cpp`。这是门禁的正确行为（陈旧二进制下的读数不算数）。
   时间线：16:55 拒绝 → 17:24 `bash build-target.sh projectile_track_probe` rc=0
   （evidence/release-gates/build-projtrack.log）→ 17:25 才有 clock2 那份 40 OK。
   **旧日志保留不删**，因为“门禁拒绝给自己读数”本身就是证据。
2. **game-event 在合并树上的第一次跑是 FAIL，不是 PASS**：
   evidence/t68-events/p0-forward.log（15:08:37）第 2 行 =
   `FAIL golden same-tick sha256 = 18f506ff…, expected 9d6817c9…`。
   原因不是产品：`core.autocrlf=true` 让 `evidence/t68-events/golden/same-tick-334.tsv` 在 checkout 时
   从 LF 被改写成 CRLF，425 个行尾各多一个字节，哈希自然从 9d6817c9 变成 18f506ff。
   修法是 `.gitattributes` 里的 `evidence/t68-events/** -text`（commit ffbd809，15:12:28，比那次 FAIL 晚 4 分钟）。
   修完后 15:50:01 的重跑是 112 OK / 0 FAIL。**两次日志都留在 evidence 里**：
   失败那份是“金标夹具与自身 checkout 不一致”这一类缺陷的唯一书面证据。

### 8.2 本节的更正记录

归档时发现本节原先写的两个路径**并不存在**：`evidence/outline/p0-clock-forward.log` 与
`evidence/trajectory/p0-clock-forward.log`（实际文件在 `evidence/release-gates/` 下，且 trajectory 有两份、
第一份是 STALE-BINARY 拒绝）。原先那一行还写着“（本报告提交时填写结论）”，即结论从未填写。
本节据此重写为上面的表格与 8.1 的两条实测说明；**没有动任何其它章节**。

## 9. 结束状态
构建/启动/变异全部在 hostlock 内串行；每次结束 release；未 break 任何他人的锁。
工作区 native/ 干净；无产品进程；evidence 见 evidence/t63-clock/。

## 10. 下一步
T66（相机接力）与 T67（HUD 文本）合并后重跑各自门禁；随后 T69/T60；最后 T73 由用户跑一次全链。
