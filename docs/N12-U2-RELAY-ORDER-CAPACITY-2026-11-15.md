# N12-U2：relay/目标、serial、容量与包内顺序负控（测量卡）

- 角色：ui-d（实例3「D 交互」，task-3 卡 U2 子片）
- 日期：2026-11-15
- 仓库：D:/TF2_Native_Worktrees/t105-e5，分支 candidate/e5
- 开工 HEAD：153aea15（subject: docs(N17): product-level re-test -- nine-class stance census, the medic crouch freeze, and the air-state boundary）
- 成文时 HEAD：2c166c9b（subject: docs(defects): register D72-D73, the lock pid space and the medic crouch freeze）—— 区间内他人提交，未触及 native/src/demo_header*、native/src/main.cpp 的 follow/relay 代码路径
- 证据根目录：D:/_shared/EXEC-2026-11-15/INST3/

本卡只做**测量与判定**：不动 native/ 任何源码、不改 accept.py 的判据、不重做已落地的 N12 三步（`db0f384`/`a5c70eb`/`68105a8`）。

## 0. 结论表

| 项 | 结论 | 依据 |
|---|---|---|
| 包内顺序（svc_SetView 与 svc_PacketEntities 同包时的先后） | **两个方向都已由合成合法消息序列证出**；真实语料 **0 自然样本** | §4，五份合成 dem + 两份真实 STV dem |
| 包内顺序的产品侧读数 | **不存在**（产品不打印这三个计数器，只有探针打印） | §4.4 |
| follow 队列容量 16 | **边界双向实测**：17 个 flag → overflow=1；16 个 flag → overflow=0 | §5.1 |
| 相机样本 maxSamples=400000 | **声明值，边界未触达**：最大实测 300,642 行 = 容量的 75.2%，淘汰路径 0 样本 | §5.2 |
| serial / 索引复用 | 解码后在 `native/src/demo_header_entity.cpp` 被丢弃（D66），两份真实 dem 的独立探针普查 **serialConflicts=0**（4,265 + 4,777 样本 / 各 18 实体）；复用失败路径**本语料不存在**，需合成夹具才能跑 | §6 |
| B1 换可区分样本 | **由既有的 B2 单元兑现，不需要新采集、不需要放宽容差**：headroom 23.0 u vs TOL 2.0 u（11.5×），residual 0.006 u | §7 |
| relay 跨 exe 一致性 | 三个 exe 三份采集：`1c3580e2` 与 `90f781b5` 的 offsetZ 直方图**逐桶相同**（这是两个不同二进制的独立一致）；`b4a2355e` 早于 relay 尾部，**没有任何 offsetZ 可比较** | §8 |
| N12-C 第一步遗留的「跨 demo 覆盖」缺口 | **已关闭**：pl_upward_f12 STV（85,198 行）与 cp_process_f12 STV（120,917 行）各扫到末尾 | §9 |
| N12-C 第一步遗留的「容量淘汰未实测」缺口 | **未关闭，只收窄**：仍是 0 样本，改为「最大 75.2% 容量」的定量表述 | §9 |

## 1. 身份（产品 / 探针 / 输入）

### 1.1 产品 exe —— 本卡期间**变过一次**，按 exe 分组

| 用途 | exe sha256 | 字节 | mtime | 何时用它 |
|---|---|---|---|---|
| 本卡两次产品实跑 | **e61c7fd9e5dba5167463f493937b23be799c96bcdd35bee7e0970d747a3bb078** | 3,025,408 | 2026-11-15 22:58:31.035043000 +0800 | §5.1 的 17/16 flag 两次运行（23:06:32 / 23:12:49 落盘） |
| U1 卡遗留的 24 次运行 | 7a8da164de67553281065ef76638c135a49baf537cb5027865bca4f39f3992ff | 3,017,216 | 2026-11-15 21:34 | 仅 §5.1 里作为第三方对照引用的 `scout16` |
| 三份 N12 采集 | 1c3580e2… / 90f781b5… / b4a2355e… | 2,991,616 / 3,003,392 / 2,991,104 | 19:22:35 / — / 18:47:41 | §7、§8 的判定（accept.py 与 relay 普查读的是它们的 trace） |

**证据按 exe 分两组，不得合并叙述**：§5.1 与 §6 的读数是 `e61c7fd9`（探针读数除外）；§7、§8 的读数是那三份历史采集的 trace，本卡**没有**用 `e61c7fd9` 重采过 relay 数据。22:58:31 之后我没有重建 exe，也没有再跑过别的产品进程。

### 1.2 探针 exe —— 本卡全部探针读数都出自这一个二进制

`native/build-nmake/demo_open_probe.exe`，663,040 B，mtime 2026-11-14 23:24:22.892009600 +0800，sha256 **168e8d9f5ed7d0bd06ba6a7a7376cb150259bb9b7e19793a66cb75d1320fdc8f**。它**没有**被我或本卡任何人重建，且这个 sha 与 `N12-QA/capture-90f781b5/identity.txt` 里记录的 probe_sha256 逐字符相同 —— 所以本卡的探针读数与 N12 历史采集是同一个探针。

`native/build-nmake/model_pose_probe.exe`，966,144 B，mtime 2026-11-15 21:50，用于 §6 的 serial 普查。

### 1.3 输入 demo

| 标签 | 路径 | 说明 |
|---|---|---|
| pl_upward_f12 STV | E:/demo测试/pl_upward_f12/STV/a9630c65…-pl_upward_f12.dem | 75.7 MB，`recording=SourceTV`，`source_tv=1`，`server_name=serveme.tf #1570108` |
| cp_process_f12 STV | E:/demo测试/cp_process_f12/STV/1338f5de…-cp_process_f12.dem | 91.0 MB，`server_name=MatchaTV` |
| pl_upward_f12 POV | E:/demo测试/pl_upward_f12/POV/autorecord_2026-08-08_21-43-59.dem | 146.9 MB |
| koth_bagel_rc11 POV | E:/demo测试/koth_bagel_rc11/POV/testdemo111.dem | 5.5 MB，§5.1 的两次队列运行读它 |
| 五份合成 dem | INST3/fixtures/order-*.dem | 1,179–1,190 B，见 §4.2 |

E:/demo测试 下共 16 个 .dem：8 张图的 POV 各一份（含无 STV 的 pl_upward）＋ **7 份 STV**（cp_gullywash_f9 / cp_process_f12 / cp_sultry_b8a / cp_sunshine / koth_bagel_rc11 / koth_product_final / pl_upward_f12）＋ 1 份 6.3 MB 的动作素材。所以「换一份 STV 再取顺序样本」不需要任何新采集 —— §4.3 就是这么做的（pl_upward_f12 → cp_process_f12）。

## 2. 判据、口径与「什么不算通过」

1. **包内顺序不是「有没有 SetView」**，而是「同一条 command-2 包里，`svc_SetView` 的报文比特位与 `svc_PacketEntities` 的报文比特位谁在前」。计数器语义：`demo_header_camera.cpp` 的 `cameraPacketsWithSetView` / `…SetViewBeforeEntities` / `…SetViewAfterEntities`。
2. **0 样本不得计为通过**（路线图裁定）。一个 0 只有在「同一谓词在别的输入上能非 0」时才具备判据资格 —— 这正是 §4.2 五份合成 dem 的作用。
3. **容量不是「没溢出过」**。要证明边界，必须两向：刚好到容量时 overflow=0，超一个时 overflow=1。只有单向读数是「计数器没卡住」还是「从未到达边界」，两者不可区分。
4. **区分力规则（沿用 accept.py 既有实现，不改）**：`headroom_abs = |oracle_vo_z − SPECTATOR_CONST(68.0)|`，当 `headroom <= TOL['derived_pos'](2.0)` 时该单元 **COINCIDENT**，不进 PASS 分母（`D:/_shared/EXEC-2026-11-15/N12-QA/accept.py:698-714`）。
5. **tick 空间不可直接比较**：产品 relay 行的 `offsetTick` 是 **server tick**，oracle 的 `prop_tick` 是 **demo tick**，两者差一个每份录像各自的常数（STV 语料 11056，POV 语料 525，均由探针独立打印 `serverMinusPlayback=` 给出）。任何跨空间的比较都必须写明换算。

## 3. 复跑方式

~~~bash
cd /d/TF2_Native_Worktrees/t105-e5
PY=C:/Users/Administrator/.dsh/dsh-runtimes/dsh-primary-runtime/dependencies/python/python.exe
# 1) 重建五份合成 dem（纯 Python，不碰产品也不碰探针）
$PY /d/_shared/EXEC-2026-11-15/INST3/u2_order_fixture.py    # order-neither / -before / -after / -none
$PY /d/_shared/EXEC-2026-11-15/INST3/u2_order_fixture2.py   # order-after-clean
$PY /d/_shared/EXEC-2026-11-15/INST3/u2_fixture_check.py    # 结构自检，须打印 OVERALL OK
$PY /d/_shared/EXEC-2026-11-15/INST3/u2_hdr_check.py        # 头部长度自检，须打印 identical True
# 2) 探针读数（须串行锁）
bash tools/lockrun.sh inst3-u2 -- bash /d/_shared/EXEC-2026-11-15/INST3/u2_order_run.sh
bash tools/lockrun.sh inst3-u2 -- bash /d/_shared/EXEC-2026-11-15/INST3/u2-batch2.sh order
# 3) 产品实跑（须串行锁）
bash tools/lockrun.sh inst3-u2 -- bash /d/_shared/EXEC-2026-11-15/INST3/u2-batch2.sh cap      # 17 flag
bash tools/lockrun.sh inst3-u2 -- bash /d/_shared/EXEC-2026-11-15/INST3/u2-batch2.sh cap16    # 16 flag
# 4) serial 普查（须串行锁）
bash tools/lockrun.sh inst3-u2 -- bash /d/_shared/EXEC-2026-11-15/INST3/u2-census.sh
# 5) relay 普查与 tick 映射（只读盘上 trace）
$PY /d/_shared/EXEC-2026-11-15/INST3/u2_relay_disc2.py
$PY /d/_shared/EXEC-2026-11-15/INST3/u2_relay_tickmap.py
~~~

按 lead 的 D72 要求：每次取锁后立刻写 `D:/_shared/EXEC-2026-11-15/LOCK-NOTES/inst3-u2.txt`（时间 + 要跑什么 + 预计分钟）。

## 4. 包内顺序：合成合法消息序列（正/反/零三向）

### 4.1 计数器代码原文

`native/src/demo_header_camera.cpp:237-254`（`stampCameraTickRow`）：

~~~cpp
void stampCameraTickRow(DemoNetworkSummary& summary, const DemoMessageStreamResult& stream, std::size_t row, bool tickKnown) {
  (void)row;
  if (!summary.cameraTrackLimits.enabled) return;
  // THE ORDERING MEASUREMENT COMES FIRST, because it is a fact about the packet and not about
  // whether a row could be built: …
  if (stream.lastSetViewMessageBit >= 0) {
    ++summary.cameraPacketsWithSetView;
    if (stream.lastPacketEntitiesMessageBit >= 0) {
      if (stream.lastSetViewMessageBit > stream.lastPacketEntitiesMessageBit) { ++summary.cameraPacketsSetViewAfterEntities; }
      else { ++summary.cameraPacketsSetViewBeforeEntities; }
    }
  }
  if (!tickKnown) { ++summary.cameraRowsNoTick; return; }
  …
}
~~~

三个要点：(a) 计数发生在**建行之前**，所以「行建不出来」不影响顺序计数；(b) `lastSetViewMessageBit` 由 `native/src/demo_header.cpp:179` 在 type 18 分支写入，`lastPacketEntitiesMessageBit` 由 `:185` 的 type 26 分支在 **readPacketEntities 之前**写入 —— 即使该 packet-entities 解码失败也参与计数；(c) 计数器由 `stampCameraTickRow` 无条件调用（`demo_header.cpp:346`），但函数体第一句就 `if (!enabled) return;`，所以只有 `--camera-track`（探针 `demo_open_probe.cpp:595`）或产品（`main.cpp:4437` 恒为 true）才有读数。

### 4.2 五份合成 dem：为什么必须是合成

合成 dem 走的是**已发布的解析路径**（`parseDemoHeader` → `indexDemoCommands` → `decodeDemoMessageStream` → `stampCameraTickRow`），不是另写一套判定。格式约束全部取自源码：1072 字节头（`native/src/demo_header_core.cpp:81-90`），demoProtocol 4、networkProtocol 24，command-2 包帧 = `byte cmd | i32 tick | 1B slot | 76B cmdinfo+8B | i32 len | payload`（`demo_header_core.cpp:119-170`），必须以 3 字节 tick 的 DEM_STOP(7) 收尾；消息流是 6 bit 类型 + 私有位布局，`MessageBits` 取 bit i 于字节 `i/8` 的第 `i%8` 位（`demo_header_internal.h:22-67`）。

| 文件 | 消息序列 | 预测 | 实测 |
|---|---|---|---|
| order-neither.dem | NetTick + **SetView**，无 PacketEntities | with_set_view=1, before=0, after=0 | **完全相同** |
| order-before.dem | NetTick + SetView + PacketEntities | with=1, **before=1**, after=0 | **完全相同** |
| order-after.dem | NetTick + PacketEntities(payloadBits=16) + SetView | with=1, before=0, **after=1** | **完全相同** |
| order-none.dem | 只有 NetTick | with=0, before=0, after=0 | **完全相同** |
| order-after-clean.dem | NetTick + PacketEntities(payloadBits=0) + SetView | with=1, **after=1** 且 `entity_failures=0` | **完全相同** |

逐份读数（`--scan --camera-track`）：

~~~
order-neither     camera_packets_with_set_view=1 before=0 after=0 setview_count=1 last_view_entity=7 entity_history_packets=0
order-before      camera_packets_with_set_view=1 before=1 after=0 setview_count=1 last_view_entity=8 entity_failures=0 first_entities_message_bit=87
order-after       camera_packets_with_set_view=1 before=0 after=1 setview_count=1 last_view_entity=9 entity_failures=1 first_entities_payload=16
order-after-clean camera_packets_with_set_view=1 before=0 after=1 setview_count=1 last_view_entity=10 entity_failures=0 first_entities_payload=0
order-none        camera_packets_with_set_view=0 before=0 after=0 setview_count=0 last_view_entity=0
~~~

五份共同的头部/索引读数：`header=1 index=1 scan=1 recording=SourceTV map=cp_synthetic commands=2 packets=1 ticks=1000 frames=10 malformed_packets=0 camera_track=1 camera_rows=0 camera_rows_no_target=1 camera_tick_ordering=1`。

**这组夹具能抓住的两种实现错误**：

- 「计数器只在能建出行时才加」——`order-neither` 没有任何 PacketEntities，`camera_rows=0 / camera_rows_no_target=1`，但 `with_set_view` 仍然是 1。若实现把计数写进建行分支，这一格必然读 0。
- 「比较方向写反」——`order-before` 与 `order-after` 分别是 1/0 与 0/1，方向反了就会互换。
另外 `order-after` 的 `entity_failures=1` 是**良性**的：它声明了 16 bit 尾部载荷，`native/src/demo_header_entity.cpp:402` 的 `bits.skip(bits.remaining() - payloadEnd)` 会 `++packetEntityDecodeFailures`。`order-after-clean` 把尾部载荷改成 0，`entity_failures=0` 而 `after=1` 不变 —— 这一对证明**顺序计数器与那次良性解码失败正交**。

### 4.3 真实语料：两份 STV dem 各 0 自然样本

| 输入 | packets | setview_count | with_set_view | before | after | camera_rows |
|---|---|---|---|---|---|---|
| pl_upward_f12 STV | 85,201 | **1** | **0** | 0 | 0 | 85,198 |
| cp_process_f12 STV | 120,920 | **1** | **0** | 0 | 0 | 120,917 |
| pl_upward_f12 POV | 300,643 | 1 | 0 | 0 | 0 | 300,642 |

两份互不相同的 STV 录像（不同服务器、不同地图、不同 tick 范围）都是「整段录像只有 1 条 svc_SetView，且它不与 svc_PacketEntities 同包」。因此：

> **包内顺序在现有语料上没有自然样本；它是被合成合法消息序列证出来的，不是被真实录像证出来的。**

这句话必须原样保留 —— 把合成结论写成「真实录像已验证」是造假，把 0 样本写成「通过」是被明令禁止的。

### 4.4 产品侧没有这个读数（覆盖声明的另一半）

`native/src/main.cpp:4437` 让产品恒开相机轨道，但产品唯一的相机摘要打印（`native/src/main.cpp:14488-14508`）只输出 `camera_track / camera_rows / camera_bytes / camera_tick_ordering` 加 pov*/relay* 计数器。`grep -nE 'camera_packets_with_set_view|camera_set_view_(before|after)'` 在 `native/src/main.cpp` 与两份产品 trace 里都**没有任何命中**。三个顺序计数器只有 `native/tools/demo_open_probe.cpp:971-973` 打印。

=> 「产品能不能自己回答包内顺序」的答案是**不能**，这也是覆盖缺口本身，不是判据缺陷。任何产品侧的顺序结论都需要一个目前不存在的打印点。

## 5. 容量：两件事，一个测到了边界，一个没有

### 5.1 follow 命令队列（kFollowQueueMax = 16）—— 边界双向实测

代码（`native/src/main.cpp:1209-1250`）：`constexpr std::size_t kFollowQueueMax = 16;`、`followQueuePush` 满时 `{ ++g_followArgumentRejected; ++g_followQueueOverflows; return false; }`、`followQueuePop` 每帧最多一次（`main.cpp:13620-13623` 的 `if (g_followSnapshotReady) { … if (followQueuePop(...)) … }`）。打印点在 `main.cpp:14172`：`follow: queue capacity=%u overflow=%u pending=%u`。

| 运行 | 传入 flag 数 | picks | pending | rejectedFlags | **overflow** | 判定 |
|---|---|---|---|---|---|---|
| u2-cap-overflow（e61c7fd9） | **17** | 6 | 10 | 1 | **1** | 超容量被拒 |
| u2-cap-16（e61c7fd9） | **16** | 16 | 0 | 0 | **0** | 刚好装满 |
| scout16（U1 卡，7a8da164，STV 真实内容） | 16 | 16 | 0 | 0 | **0** | 第二次到容量不溢出 |

原行：

~~~
u2-cap-overflow.trace  follow: summary picks=6 idempotent=5 rejects=0 fallbacks=0 pinnedDeaths=0 nameMissing=0 rejectedFlags=1 state=ok target=1 serial=0
u2-cap-overflow.trace  follow: queue capacity=16 overflow=1 pending=10
u2-cap-16.trace        follow: summary picks=16 idempotent=15 rejects=0 fallbacks=0 pinnedDeaths=0 nameMissing=0 rejectedFlags=0 state=ok target=1 serial=0
u2-cap-16.trace        follow: queue capacity=16 overflow=0 pending=0
~~~

**边界是算术闭合的**：17 次 push 中 16 次成功 = picks(6，已被逐帧弹出处理) + pending(10，仍在队列)；第 17 次被 `followQueuePush` 拒绝，同时把 `g_followArgumentRejected` 与 `g_followQueueOverflows` 各加一。16 次 push 则 `pending=0`、`overflow=0`，因为第 16 次看到的是 `count=15 < 16`。

**不要误读的两点**：

- `picks=6` 不是「17 个里只有 6 个生效」。`followQueuePop` 每帧最多弹一个，且只在快照就绪的帧弹；这次 400 tick 的 bagel 运行只给了这么多次机会，剩下 10 条就留在 `pending` 里。把 6 当成「容量只有 6」是错的，把 10 当成丢失也是错的。
- `rejectedFlags=1` **单独不能**证明队列满：`native/src/main.cpp:1290` 的 `g_followArgumentRejected` 同时被 CLI 范围检查与队列满两处递增。能把两者分开的只有 `follow: queue overflow=` 这一行（本卡把它作为判据，`rejectedFlags` 只作辅证）。这是本卡发现的**报告可读性缺陷**（不是功能 bug），已登记为观察项，未改代码。

对照：`u2_queue_census.py` 普查 U1 的 24 条 trace，21 条带队列行（另 3 条 a1-neg / a1-neg-stv / calib 无 follow 活动故无此行），**全部** 是 `capacity=16 overflow=0 pending=0`。也就是说在 23:06 之前，容量 16 一直是「声明值 + 从无溢出」，从未被顶到边界 —— 现在不是了。

### 5.2 相机样本 maxSamples = 400,000 —— 边界未触达

`native/include/demo_header.h:402/:490` `std::size_t maxSamples = 400000;`，`native/src/demo_header_camera.cpp:344` 满了就 `erase(begin())` 并 `++cameraRowsDropped`（源码注释自己写明这是 O(n) 淘汰、且本语料够不到）。

| 输入 | camera_rows | camera_rows_dropped | 占容量 |
|---|---|---|---|
| pl_upward_f12 POV | **300,642** | 0 | 75.2% |
| cp_process_f12 STV | 120,917 | 0 | 30.2% |
| pl_upward_f12 STV | 85,198 | 0 | 21.3% |

=> 容量 **声明 400,000，最大实测 300,642（75.2%），淘汰路径 0 样本**。要触达必须造 >400k 行的合成输入或下调上限，两者本卡都**没做**，所以这一项写成「未触达」，不是「通过」。

## 6. serial：解码后被丢弃（D66），以及本语料为何证不出复用失败

### 6.1 源码事实

`native/src/demo_header_entity.cpp:338-339` 每次 Enter 都解出 10 bit serial：

~~~cpp
std::uint32_t classId = 0, serial = 0;
if (!bits.read(classBits, classId) || !bits.read(10, serial)) { ++summary.packetEntityDecodeFailures; entityUpdatesComplete = false; break; }
~~~

但 `EntityState` 上没有存它的字段，所以 D66「解出来又丢掉」成立。产品侧 handle 的拆分是**已有**实现：`native/src/entity_model.cpp:270-298` `resolveObserverFocus` 做 `targetIndex = handle & kEntityHandleIndexMask` / `targetSerial = handle >> kEntityHandleIndexBits`；`native/src/main.cpp:1393` 的 `followSlotSerial` 目前 `return 0;`，写点 `:1577`。`SpectateSlot`（`native/include/spectate_target.h:53-64`）没有 serial 字段 —— 要真正消费必须改头文件。

### 6.2 两份真实 dem 的独立普查：serialConflicts = 0

用**既有**探针 `model_pose_probe.exe --player-animation-event-census --demo <p> --follow-window 200 --sample-limit 200000`（`native/tools/model_pose_probe.cpp:784-805` 解析参数，`:1187-1188` 打印结果）：

| 输入 | n | idxInRange | **serialConflicts** | distinctEntities | 用时 |
|---|---|---|---|---|---|
| pl_upward_f12 STV | 4,265 | 4,265 | **0** | 18 | 46,619 ms |
| pl_upward_f12 POV | 4,777 | 4,777 | **0** | 18 | 157,700 ms |

样本行证明 handle 布局与真实数据一致：`player=301073 entity=17 handleIndex=17 serial=147`（与 `native/tools/spectate_probe.cpp:84-85` 的 S1 断言逐值相同）、`player=26635 entity=11 serial=13`、`player=1585156 entity=4 serial=774`、`player=1722384 entity=16 serial=841` 等。

尾行同时给出一条本卡有用的独立常量：STV `serverMinusPlayback=11056`、POV `serverMinusPlayback=525`。

### 6.3 结论的正确措辞

> 两份真实录像里**没有发生**同一索引被复用且 serial 改变的情况（9,042 个样本、18 个实体、0 冲突）。所以「复用检测」的失败路径**在本语料不存在**，而不是「已验证正确」。要跑通失败路径必须造合成夹具，现成写法：`native/tools/entity_message_fixture_probe.cpp:372-383` 的 `writeUpdate(body, 0, 2); body.write(0, 1); body.write(1, 10);  // class id 0 / serial`，再 `writePacketEntities(out, 16, false, 0, 1, body)`。本卡**没有**写这个夹具（它属于「实现/消费 serial」那一层，而 U2 是测量卡），登记为下一步。

同时 `serverMinusPlayback` 在 STV 与 POV 上是**不同的常数**（11056 vs 525），所以 accept.py 里硬编码的 11056 是「STV roster 需要」而非「全局常数」。任何跨 demo 的 tick 换算都必须取各自录像的值。

## 7. B1 换成可区分样本：无需新采集

### 7.1 判据原文（accept.py 已经实现，本卡不改）

`D:/_shared/EXEC-2026-11-15/N12-QA/accept.py:671-714`：

~~~python
kconst = SPECTATOR_CONST                       # 68.0
headroom = abs(vo_z - kconst)
S['discrimination'] = {..., 'cannot_discriminate': headroom <= TOL['derived_pos'],   # 2.0
                        'product_uses_constant_exactly': abs(residual - kconst) <= PRINT_FLOOR}
# 注释原文：a residual of ~68 is what BOTH a correct read of a 68.000065 crouch-height field
# AND the bare constant produce … this cell has ZERO discrimination and may not be scored as a
# PASS (lead ruling). It becomes COINCIDENT: not green, not red, not in the PASS denominator.
~~~

### 7.2 既有判定表里两个单元各自的处境（`N12-QA/accept-1c3580e2.json`）

| 单元 | 帧 | 状态 | oracle vo_z | headroom | residual | 分类 |
|---|---|---|---|---|---|---|
| **B1**（站姿，实体 2） | stv-t20000 | NOT VERIFIED | 68.00006484991536 | **0.0001** | +67.9851 | **COINCIDENT** |
| **B2**（下蹲，实体 2） | stv-t40000 | **PASS** | 45.000042915385166 | **23.0** | +45.006 | **PASS(read)** |

B2 的行级读数：x/y/z 三轴 delta 0.0335 / 0.0385 / **0.0059** u，pitch/yaw PASS，`view_offset_prop_tick=39981`，`n_distinct_ticks=5`，`strict_literal_32tick_rule=TRUE`。

=> **lead 要求的「B1 换一条能与 68.0 常量区分的样本」在既有产物里已经兑现**：分母里算 PASS 的那个单元本来就是 headroom 23.0 u（11.5 倍容差）的下蹲样本，B1 那个 68.000065 已经被判 COINCIDENT 并**排除在分母之外**。本卡的任务不是重新打分，而是把这条机制写清楚，并证明它不是巧合。

### 7.3 该样本在产品侧的对应行（discriminating 证据链）

在 `capture-1c3580e2/stv-t20000` 与 `capture-90f781b5/stv-t20000` 两份采集里，都能找到 offsetZ 明显远离 68 的 relay 行：

~~~
39764.6 spectate: camera relay target=2 eye=-1945.3,-1016.6,243.1 pitch=19.41 yaw=100.65 behind=0 offsetZ=45.0000 offsetSrc=netprop offsetTick=12744 offsetAge=21 life=0 obs=0 reason=applied
68013.9 spectate: camera relay target=3 eye=-119.4,-2344.6,235.4 pitch=-11.65 yaw=10.91 behind=0 offsetZ=63.0320 offsetSrc=netprop offsetTick=27832 offsetAge=0 life=0 obs=0 reason=applied
~~~

偏移值分布（`u2_relay_discrimination.json`）：`1c3580e2/stv-t20000` 24 行 = {45.0000:8, 63.0320:1, 68.0001:15}，可区分 9 行；`stv-t40000` 46 行 = {45.0000:11, 63.0320:1, 68.0001:34}，可区分 12 行。`63.0320` 的 headroom 4.968 u = 2.5 倍容差，是合法但更弱的第二候选。

### 7.4 tick 映射（必须写明，不能装作同 tick）

`u2_relay_tickmap.py` 对 140 条 relay 行做 `demo = offsetTick − 11056` 再就近取 oracle 快照（快照只有 10 个 tick：18899–18903、39984–39988）：

| relay offsetTick | 换算 demo tick | 最近快照 | 间隔 | 该行 offsetZ |
|---|---|---|---|---|
| **51037** | **39981** | **39984** | **+3** | **45.0000** |
| 29614 | 18558 | 18899 | +341 | 68.0001 |
| 其余 138 行 | — | — | 1,394–11,049 | — |

只有一行落到 3 tick 内，且它在**两份采集里都出现**。它与 accept.json 的 B2 单元对得上：B2 的 `view_offset_prop_tick` 正是 39981，产品眼位 (-536.0,-479.8,586.0) 与 oracle derived_eye (-536.03351,-479.83847,585.99406) 相差 0.006 u。

+11056 的**来源**不是拟合：oracle 的 tick 空间从 10989 起，产品 STV spectate tick 从 11057 起（差 68 tick，与常数不同源但同量级），而 `resolve_stv` 先减 `stale` 再减 11056，且 B1 的 stale 是 1100、B2 是 15 —— 一个固定的 server→demo 偏移，不是逐帧修正。探针在 `model_pose_probe` 上独立打印同一个 11056，是第二条证据。

## 8. relay 跨 exe：三份采集，两份可比、一份无尾部

三份采集的 trace 前 16 位 sha256 互不相同（stv-t20000：`df1e3da3f5ffccfa` / `4694708269310232` / `5215e753b40d50eb`），所以是三次不同运行。`capture-90f781b5/identity.txt` 记录的产出 exe 是 `90f781b5…`（3,003,392 B，head `636c45db`），与 `1c3580e2…`（2,991,616 B，head `83657f10`）、`b4a2355e…`（2,991,104 B，head `a901ecec`）三者互不相同。

| 采集 | exe | stv-t20000 行数 / 可区分 / 最大 headroom | stv-t40000 |
|---|---|---|---|
| capture-1c3580e2 | 1c3580e2 | 24 / 9 / 23.0 | 46 / 12 / 23.0 |
| capture-90f781b5 | 90f781b5 | **24 / 9 / 23.0（与上行逐桶相同）** | **46 / 12 / 23.0** |
| capture-b4a2355e | b4a2355e | 24 / **0**（24 行全无尾部） | 46 / 0 |

=> `1c3580e2` 与 `90f781b5` 是**两个不同二进制给出的同一分布**，这是真正的跨 exe 一致；`b4a2355e` 早于 relay 尾部字段，offsetZ 集合为空，**任何**依赖 offsetZ 的比较都必须把它报成「无尾部」，绝不报成 0。

POV 帧在两份/三份采集里 relay 行数**恒为 0** —— relay 只发生在 STV。

## 9. 本卡关闭/未关闭的既有缺口

| 出处 | 原话 | 本卡状态 |
|---|---|---|
| `docs/N12-C-STEP1-IMPL-2026-11-15.md:110` | 「跨 demo 覆盖：只测了 pov_f12 一份。stv_f12 / upward 的 dem 路径未能定位，NOT TESTED」 | **关闭**：pl_upward_f12 STV 85,198 行、cp_process_f12 STV 120,917 行，均扫到末尾 |
| `docs/N12-C-STEP1-IMPL-2026-11-15.md:108` | 「容量淘汰路径 0 次触发 … maxSamples=400000 与 ~300k 行的边界未实测」 | **未关闭，收窄**：最大 300,642 = 75.2%，仍是 0 样本 |
| `docs/N12-ACCEPTANCE-2026-11-15.md:296` | 「offsetZ 取值集合只有 {45.0000, 63.0320, 68.0001}」 | **补正**：集合正确，但文档没指出其中**两个值本身就能区分 68.0 常量**，而 B1 恰好挑中最不可区分的那个 |
| 路线图 U2 行「包内顺序 0 样本不当通过」 | — | **满足**：五份合成 dem 双向证明 + 真实语料 0 样本的独立结论 |

## 10. 三条缺陷类自检（沿用 D53/D56/D59 家族）

1. **awk 数字 vs 字符串（D53 家族）**：`grep -c awk u2-batch2.sh u2-census.sh u2_order_run.sh` 全为 0；`u2_*.py` 无一使用 awk。所有字段提取走 `cut`/`tr`/`grep -E` 或 Python。
2. **引用生产者不打印的字段（D56 家族）**：本卡每个判据都指向**同一行**的同一字段 —— 队列读 `follow: queue capacity=/overflow=/pending=` 同一行；顺序读 `camera_packets_with_set_view=` 等的探针行；relay 读 `spectate: camera relay` 行自身的 offsetZ/offsetTick。没有一处用 A 行的字段去证明 B 行的行为。§4.4 反过来确认了「产品不打印顺序计数器」，因此本卡不给任何产品侧顺序结论。
3. **采样密度改变读数（D59 家族）**：本卡的探针运行参数固定（`--scan --camera-track`，无 `--sample-limit` 变化）；`order-after` 与 `order-after-clean` 只改「尾部载荷位数」这一个自变量，其余逐字节相同，从而把 `entity_failures` 的影响隔离出来。`u2_queue_census.py` 跨 24 条 trace 的普查也用了同一提取式。

另记一条**脚本级教训**（本卡实际踩到）：`u2_order_fixture2.py` 第一版把 13 字节的 `b"SourceTV Demo"` 赋给 9 字节切片 `h[276:276+9]`，**插入**了 4 字节把头部撑到 1076，索引器于是在 1072 处读到 0 并报 `index_error=unknown demo command`。头部必须逐字段按精确长度写入（`h[276:276+13]`）。原始五份夹具因为「字符串缩短 4 字节 + 加长 4 字节」净零而偶然正确，`u2_hdr_check.py` 用 `buggy_len / exact_len / identical` 三项把它钉住。

## 11. 本卡证明什么 / 没有证明什么

**证明（有逐行读数、有反例方向）**：
1. 包内顺序计数器在 `svc_SetView` **前于/后于** `svc_PacketEntities` 两种合法序列上分别给出 before=1 / after=1，方向正确；在只有 SetView 的包上仍计 `with_set_view=1`，说明它不依赖能否建行；在只有 NetTick 的包上给 0，提供真零控。
2. 两份真实 STV 录像各只有 1 条 svc_SetView 且从不与 PacketEntities 同包，故真实语料**没有**自然样本。
3. follow 队列容量 16 的边界：17 → overflow=1（且 `rejectedFlags=1`）、16 → overflow=0（两次独立运行）。
4. 相机样本容量 400,000 未触达：最大实测 300,642 = 75.2%，淘汰 0 次。
5. 两份真实 dem 的 serial 复用冲突为 0（9,042 样本 / 18 实体），且 handle 布局与既有断言逐值一致。
6. B1/B2 的区分力：B2 headroom 23.0 u、B1 headroom 0.0001 u；B1 已被既有判定表排除出 PASS 分母，B2 已在分母内。
7. relay offsetZ 分布跨两个不同 exe 逐桶相同；第三份采集无尾部。
8. 产品**不打印**包内顺序计数器（覆盖缺口，用语料外的打印点搜索确认）。

**没有证明**：
1. 真实录像上的包内顺序 —— 0 样本，只有合成证据。
2. 相机样本容量淘汰路径 —— 0 次触发。
3. serial 复用时的失败行为 —— 本语料不存在该情形，夹具未写。
4. 产品侧能否消费 serial —— `SpectateSlot` 无字段、`followSlotSerial` 恒 0（D66 未修）。
5. relay 读数在 `e61c7fd9` 上的复现 —— §7/§8 全部来自三份历史采集，本卡没有用当前 exe 重采。
6. 任何「真实导播相机」等价性 —— 仍只能 PROVEN-FOLLOW。
7. 包内顺序在**同一个包内多条 SetView / 多条 PacketEntities** 时的语义（计数器只记最后一条的位）；未造该序列。
8. 队列容量在**真实 STV 长跑**里的持续溢出行为（本卡只做了单进程 17 flag 的定点；U1 的 scout16 是 16 flag）。

## 12. 锁与残留进程（按 lead D72 要求逐条记）

- 本卡全部 `lockrun` 调用均已打印 `VERIFY-LOCK=ACQUIRED` / `VERIFY-LOCK=RELEASED`，无一次绕过锁。
- **没有**观察到 `STALE-LOCK=RECLAIMED … reason=owner-dead`：本卡目录内 grep 无该字样，因此没有需要上报的误回收窗口。
- 本卡的两次产品运行发生在 23:06:32 与 23:12:49，均在 exe `e61c7fd9`（mtime 22:58:31）之后，未被其他实例的构建打断。
- 期间多次因 `n05-n06-maps` 持锁而改做只读工作（`VERIFY-LOCK=FAIL (another verify-all run owns …)`），未做重试循环。

## 13. 产物哈希（D:/_shared/EXEC-2026-11-15/INST3/）

~~~
222      240c313abcc2267c  u2-order-identity.txt
3410     a8dd6a8d9c71eca2  u2-order-order-neither.txt
3409     dd59a8a72e93d5e8  u2-order-order-before.txt
3410     6ce3c331b7e0d056  u2-order-order-after.txt
3410     206da0a9b19d73e1  u2-order-order-none.txt
3405     49b1ede2ee80cd53  u2-order-order-after-clean.txt
3696     53f2ec02348b3cc0  u2-order-stv.txt
3719     f30b8b4cb78069d2  u2-order-process-stv.txt
3768     789a6d78ce712f17  u2-order-pov.txt
684144   2b00bb7d8e0a53da  u2-census-stv.txt
753940   c097247daed85016  u2-census-pov.txt
139259   4be312c5890346cb  u2-cap-overflow.trace
214736   4e04c5e2a9864d69  u2-cap-16.trace
3453     dcb5665d7f1929ec  u2_relay_discrimination.json
32419    3c964f3bdfe37e27  u2_relay_tickmap.json

fixtures/  order-neither.dem   1181  000625ba9e5010de   order-none.dem      1179  57bcc43e3ba74b38
           order-before.dem    1188  8ff4e2263f72a60e   order-after.dem     1190  5a55249d5c1261e9
           order-after-clean.dem 1188 5763584ae84d1194

tools:    u2-batch2.sh        3,045  f95f383a022b38f1   u2-census.sh           807  c2c2d226e2dd2c78
          u2_order_fixture.py 3,388  23ed729557f3bdce   u2_order_fixture2.py 2,981  b87645ddd2e27c87
          u2_order_run.sh     1,536  a3bb178516c94762   u2_fixture_check.py  4,440  3fed8d3a501f4a6e
          u2_hdr_check.py     1,253  ecd70b42484e3d95   u2_relay_rows.py     1,558   de4fcdc224384279
          u2_relay_disc2.py   3,906  31475a69cd4d8244   u2_relay_tickmap.py  2,671  fb50cd908b60f85a
          u2_queue_census.py    884  4f2616cf9bcfa2fd
~~~

身份：产品 `e61c7fd9e5dba516…`（3,025,408 B，mtime 2026-11-15 22:58:31）· 探针 `168e8d9f5ed7d0bd…`（663,040 B，mtime 2026-11-14 23:24:22）· 成文 HEAD `2c166c9b`。
