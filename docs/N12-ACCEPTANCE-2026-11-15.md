# N12 验收报告（post-B 终态）

- 角色：n12-qa（接替崩溃实例；本版全部读数由盘上产物加只读探针重建，不依赖任何前一轮的中间记忆）
- 日期：2026-11-15
- 仓库：D:/TF2_Native_Worktrees/t105-e5，分支 candidate/e5
- HEAD（成文时）：6dd853ef24494b6066e4624970ac75620d4863ea，短 sha 6dd853e，commit date 2026-11-14 19:37:47 +0800，subject fix(camera): N12-B 观战眼高改读被跟随目标的 m_vecViewOffset[2]
- 被测 exe：native/build-nmake/tf2_demo_native.exe，2,991,616 B，sha256 1c3580e278a160194783ec2c96d3bd622a5fe10ee2b5b70bafbea3295af0797e（短 1c3580e2），mtime 2026-11-14 19:22:35
- 证据根目录：D:/_shared/EXEC-2026-11-15/N12-QA/
- 本版取代 docs/N12-ACCEPTANCE-2026-11-15.md 旧版（403 行，pre-B 态，exe b4a2355e）。旧版的三列里「oldexe」「preB」两列按其原值保留在本报告 §9。

## 0. 复跑方式

~~~bash
cd /d/_shared/EXEC-2026-11-15/N12-QA
N12_CAP=capture-1c3580e2 python accept.py     # 写 accept.json / accept-table.md
python check-6.py                            # 写 check-6.json
python n12_postb_census.py                   # 写 n12-postB-census.json（本报告 §6 的 census）
python n12_controls_dump.py                  # 打印 accept.json 的 13 条负控
~~~

复跑只读盘上 trace 与 accept.py 内嵌的预言机表，不启动产品 exe、不重新采集。产物哈希见 §15。

## 1. 卡级结论

| 卡 | 判定 | 分母与依据 |
|---|---|---|
| N12-A POV 眼高 = origin + viewOffset[2] | **PASS** | A1 / A2 / A3 三帧全 PASS；EX-3（死亡机位）按判据不进分母。accept-table.md:20 |
| N12-B 观战下蹲眼高 = 目标 origin + viewOffset[2] | **PASS** | 判定样本 B2 PASS；B1 记 NOT VERIFIED / COINCIDENT（零分辨力，不进 PASS 分母）。accept-table.md:21 |
| N12-C 快照对新样本的选择 | **BOUNDED-STALE** | 不计 PASS、不计 FAIL。拆 (a) selection_correct 与 (b) freshness_bound 两列，见 §6。accept-table.md:22 |

三句话把卡级说清楚：

1. **N12-A PASS**：三帧 POV 眼高与预言机 D_derived 值差都在 0.0313 u 以内（判据 2.0 u），x/y 最大差 0.0422 u，角度最大差 0.0036 deg（判据 0.5 deg）。§4。**注意：这些差全部小于打印量子（eye 0.1 u / 角度 0.01 deg），单独不构成证据；A 卡的判别力在 `offsetZ` 四位读数（75.0001 对 75.000072、68.0001 对 68.000065）与修复前后 +75.0 u 的位移，见 §4 末「读数精度声明」。**
2. **N12-B PASS**：下蹲观战帧眼高由修前的 609.0 变为 586.0，与「目标 origin 540.9940 + viewOffset[2] 45.0000」一致；修前多出来的 23.0 u 正是旧实现统一加上的常量 68.0。站立帧 B1 逐位不变（像素级：六帧 bmp 只有 stv-t40000 变了）。§5。
3. **N12-C 是 BOUNDED-STALE，不是 PASS、不是 FAIL、不是 NOT VERIFIED**：阈值（stale ≤ 1 tick）没有放宽，两个注册样本上实测 1100 tick 与 15 tick，都远大于 1。选择规则本身是对的（(a) PASS），达不到 1 tick 的原因是留存密度（(b) 是留存属性）。§6。

## 2. 身份

### 2.1 exe 与仓库

| 项 | 值 | 证据 |
|---|---|---|
| exe sha256 | 1c3580e278a160194783ec2c96d3bd622a5fe10ee2b5b70bafbea3295af0797e | capture-1c3580e2/identity.txt |
| exe 字节数 | 2,991,616 | 同上 |
| exe mtime | 2026-11-14 19:22:35 | 同上 |
| 采集时 HEAD | 83657f100c14ec4b328d050b07acf82cad7545d5 | 同上（采集脚本记录的 git rev-parse） |
| 成文时 HEAD | 6dd853ef24494b6066e4624970ac75620d4863ea | git rev-parse HEAD |
| 83657f10..HEAD 的提交 | 仅 6dd853e 一条 | git log --oneline 83657f10..HEAD |
| 该提交改动的 native/ 文件 | native/src/main.cpp，107 insertions(+)，3 deletions(-) | git diff --stat 83657f10..HEAD -- native/ |
| 当前工作树 native/ 是否干净 | 是（0 行输出） | git status --porcelain -- native/ |

关于时间顺序的事实：exe 的 mtime（19:22:35）早于 6dd853e 的 commit date（19:37:47）。因此采集时 HEAD 标签（83657f10）不含该修复提交，而 exe 是从含未提交修复的工作树构建的；修复在采集之后才提交。**从 6dd853e 干净重建能否得到逐字节相同的 1c3580e2 = NOT VERIFIED（本轮未重建 exe）。**

### 2.2 输入 demo

| demo | 路径 | 字节 | sha256 | 用途 |
|---|---|---|---|---|
| pl_upward_f12 POV | E:/demo测试/pl_upward_f12/POV/autorecord_2026-08-08_21-43-59.dem | 146,853,666 | 14b4943a8d7e866677091bb5a4843882bfed1105bc5ef27d955bbc4cd3ad00e8 | A1、A2；archive gap（pov_f12） |
| pl_upward_f12 STV | E:/demo测试/pl_upward_f12/STV/a9630c6557468b24f0b8c2e7dab38ed8_match-20261030-2145-pl_upward_f12.dem | 75,663,754 | 7e047aff1f7743915e355eac907a95dc3d981aa0fae5591b6b15346fb77dc4a3 | B1、B2；archive gap（stv_f12） |
| pl_upward POV | E:/demo测试/pl_upward/POV/autorecord_2026-11-13_13-59-18.dem | 56,328,194 | 708683e191ac5ae7b4dacbfcbcb4d542c1d5489128d05cb8d9b53681655ddf7d | A3、EX-3；archive gap（upward） |

pl_upward 与 pl_upward_f12 是两张不同地图的录像，不得跨图比较任何数值。

### 2.3 六帧采集产物（capture-1c3580e2）

| frame | 场景 | 采样 demo tick | 用时 s | trace 字节 | trace sha16 | bmp sha16 | bmp 字节 |
|---|---|---|---|---|---|---|---|
| pov-t20000 | A1，pl_upward_f12 POV | 20000 | 168.5 | 15,609,799 | 12ffec095be0c1a8 | 250de7df310a2a05 | 2,582,406 |
| pov-t40000 | A2，pl_upward_f12 POV | 40000（预言机取最近样本 39999） | 195.7 | 41,742,367 | e9dcd030f10ce854 | 8e62c339b7cfa13e | 2,582,406 |
| upward-pov-t20000 | EX-3，pl_upward POV（采样时已死亡） | 20000 | 77.9 | 26,292,516 | 8d6cc0ac369c0268 | 3ff046064f8b86f3 | 2,582,406 |
| upward-pov-t39999 | A3，pl_upward POV | 39999 | 115.6 | 50,990,779 | 0f4b23a598e6d09e | 83b348447cd34ef5 | 2,582,406 |
| stv-t20000 | B1，pl_upward_f12 STV（站立目标） | 20000 | 75.5 | 18,982,184 | df1e3da3f5ffccfa | 1e796ab076838274 | 2,582,406 |
| stv-t40000 | B2，pl_upward_f12 STV（下蹲目标） | 40000 | 113.2 | 36,776,453 | 6631554b213f1fdc | 450806898cfdacac | 2,582,406 |

六帧的 run.txt 都记 rc=0、trace_state=ok（capture-1c3580e2/*/run.txt）。

**像素级对照（与 pre-B 采集 capture-b4a2355e 比）**：六帧 bmp 里五帧 sha16 完全相同（250de7df…、8e62c339…、3ff04606…、83b34844…、1e796ab0…），只有 stv-t40000 由 9168fefdc7c2229f 变为 450806898cfdacac。即 N12-B 的修复在画面上只动了下蹲观战那一帧，其余五帧逐像素不变。

## 3. 判据、分母与取数口径

- 位置判据：D_derived / D_follow 样本 2.0 u；角度判据 0.5 deg。
- D_direct 样本另有 0.5 u / 0.05 deg 的紧判据。
- 打印精度：位置 0.1 u、角度 0.01 deg；|Δ| < 0.05 u 记 0。
- 角度期望取 cmdinfo 的 viewAngles（裁定 R2：m_angEyeAngles 只作 INFO 交叉校验）。
- 判据来源：D:/_shared/EXEC-2026-11-15/N12-COMPARE/PROTOCOL.md §3–§5、修订 R1、裁定 m00121 / m00766 / R2（n12a-angle-source-ruling.md）。
- 分母：A 卡 = {A1, A2, A3}；B 卡 = {B2}。EX-3 是负控（不进 A 分母），B1 因零分辨力记 NOT VERIFIED（不进 B 分母）。分母由 accept.json 的 denominator 字段给出。
- 取数口径：D_derived 的 z 必须按 kind 过滤后再取（accept.py 每个样本有两组同轴 z：D_direct 的 origin 与 D_derived 的 eye；混读会把 A1 读成 662.5）。

## 4. N12-A：POV 眼高

| 样本 | 产品 eye z | 预言机 D_derived z | Δ (u) | 判定 |
|---|---|---|---|---|
| A1 pov-t20000 | 737.5 | 737.5296 | −0.0296 | PASS |
| A2 pov-t40000 | 335.0 | 334.9775 | +0.0225 | PASS（预言机取最近样本 tick 39999） |
| A3 upward-pov-t39999 | 596.0 | 596.0313 | −0.0313 | PASS |
| EX-3 upward-pov-t20000 | 896.0 | 896.0（origin；死亡机位不加眼高） | 0.0000 | PASS（负控，不进分母，见 §8.1） |

三条 eye 行原文（post-B，各帧最后一条，capture-1c3580e2/<frame>/<frame>.trace）：

~~~
254408.7 scene: eye origin=2119.7,-265.6,662.5 offsetZ=75.0001 eye=2119.7,-265.6,737.5 viewEntity=9 originTick=20000 offsetTick=19488 offsetAge=343 life=0 obs=0 reason=applied
303416.0 scene: eye origin=503.8,1437.7,260.0 offsetZ=75.0001 eye=503.8,1437.7,335.0 viewEntity=9 originTick=39999 offsetTick=39169 offsetAge=100 life=0 obs=0 reason=applied
114363.8 scene: eye origin=140.3,86.0,528.0 offsetZ=68.0001 eye=140.3,86.0,596.0 viewEntity=10 originTick=39999 offsetTick=39791 offsetAge=390 life=0 obs=0 reason=applied
~~~

三条 offsetZ 读数与预言机 viewOffset[2] 一致：A1/A2 为 75.0001 对 75.000072，A3 为 68.0001 对 68.000065（check-6.json 的 offsetZ 三条 PASS）。

x/y 与角度：x/y 最大 |Δ| 0.0422 u（A1 的 x），角度最大 |Δ| 0.0036 deg（A3），都在判据内（accept.json samples）。

**读数精度声明（lead 复核追加，2026-11-14 20:30）：上面两行的 Δ 都小于一个打印量子，不能单独当判别证据。**

- 同一条 `scene: eye` 行的精度是混的：origin/eye 三分量是 `%.1f`（量子 **0.1 u**），`offsetZ` 是 `%.4f`（量子 **0.0001 u**），pitch/yaw 是 `%.2f`（量子 **0.01 deg**）。出处 `native/src/main.cpp:10261`（`scene: eye origin=%.1f,%.1f,%.1f offsetZ=%.4f eye=%.1f,%.1f,%.1f`）与 `:12621`（`eye=%.1f,%.1f,%.1f pitch=%.2f yaw=%.2f`）。
- 因此：eye z 的三个 Δ（0.0225 / 0.0296 / 0.0313）、x/y 最大 Δ 0.0422 u、角度最大 Δ 0.0036 deg —— **全部小于各自量子**。只拿这些数字说「一致到 0.03 u」是把打印分辨率当成了测量精度，和 §8.1 里 B1 被判 COINCIDENT 是同一个陷阱（那边是 0.0001 u 对 0.1 u 量子）。
- A 卡真正的判别力来自两处，都是量子级之上的：
  1. **`offsetZ` 四位数**：A1/A2 = 75.0001 对预言机 75.000072（Δ 2.8e-5，量子 1e-4），A3 = 68.0001 对 68.000065（Δ 3.5e-5）。这一列才是「产品读的是 viewOffset[2] 而不是常量」的直接读数。
  2. **修复前后的位移**：同帧 eye 662.5 → 737.5，位移 +75.0 u，是量子的 **750 倍**（§8.1 表）。
- B2 同理：它的 PASS 不靠 Δ 0.0059（eye 量子 0.1），靠的是修复前后移动 −23.0 u（230 个量子，方向与预言机自己的两个 viewOffset[2] 之差一致：68.00006484991536 → 45.000042915385166，差 −23.0000219）。
- 本声明不改变 A1/A2/A3 与 B2 的 PASS 判定，只改变**证据链的落点**：判词应读成「offsetZ 四位读数吻合 + 修复前后位移与预言机预测一致」，**不是**「eye 差 0.03 u」。

A 卡结论：3/3 PASS，且 reason=applied（不是 fallback 常量、不是 none）。

## 5. N12-B：下蹲观战眼高

| 样本 | 产品 eye | 预言机 origin + viewOffset[2] | Δz | Δx / Δy | Δpitch / Δyaw | 判定 |
|---|---|---|---|---|---|---|
| B1 stv-t20000（站立） | 289.9 | 221.91494750976562 + 68.00006484991536 = 289.91501236 | −0.0150 | 0.0009 / 0.0322 | −0.0012 / −0.0028 | COINCIDENT / NOT VERIFIED（零分辨力，见下） |
| B2 stv-t40000（下蹲） | 586.0 | 540.9940185546875 + 45.000042915385166 = 585.99406147 | +0.0059 | 0.0335 / 0.0385 | +0.0012 / −0.0023 | PASS |

两条相机行原文（post-B，各帧最后一条）：

~~~
74062.8 scene: camera mode=first-person target=1 follows=1791 holds=1 freeze=0 eye=-348.9,-1444.0,289.9 pitch=10.94 yaw=334.66 src=follow
111530.3 scene: camera mode=first-person target=1 follows=3307 holds=1 freeze=0 eye=-536.0,-479.8,586.0 pitch=25.06 yaw=103.81 src=follow
~~~

**为什么 B1 记 COINCIDENT 而不是 PASS**：站立目标的真实 m_vecViewOffset[2] = 68.00006484991536，与旧实现写死的常量 kSpectatorEyeHeight = 68.0（native/src/main.cpp:458）只差 0.0001 u，低于打印精度 0.1 u。也就是说，产品即使仍用常量，打印出来也是 289.9。这一格没有分辨力，不能用作「读到了 netprop」的证据，记 COINCIDENT 且不进 PASS 分母。B1 的其余轴（x 0.0009、y 0.0322、pitch 0.0012、yaw 0.0028）都在判据内（accept.json samples.B1）。

**B2 为什么够判别力**：下蹲目标的 viewOffset[2] = 45.0000429，与常量 68.0 差 23.0 u，远高于打印精度。

**修复效应**（accept.json ab_fixed_effect）：

- 修前产品 z = 609.0，隐式偏移 609.0 − 540.9940 = 68.0060；修后产品 z = 586.0，隐式偏移 586.0 − 540.9940 = 45.0060。
- delta-of-delta = −22.9791，要求 −23.0，偏差 0.0209 u（低于位置判据 2.0 u）。
- 修前该帧「产品 −（origin + viewOffset）」= +68.0060，与常量 68.0 的差 0.0060 u 属于打印舍入；修后 = +45.0060，等于读到的是 45.0 而不是 68.0。
- B1 逐位不变（289.9 → 289.9，bmp sha 也不变）。

实现依据：观战眼高路径由 placeSpectatorView（native/src/main.cpp:715-750）计算，eye z = transform.origin[2] + relayEye.offsetZ − forward[2] * kSpectatorBackUnits（:736-740，kSpectatorBackUnits = 0.0f 见 :466）；relayEye 由 resolveRelayEye（:664-696）产出：m_lifeState != 0 时 offsetZ=0、source=none、reason=dead-no-eye-offset（:667-674）；m_iObserverMode != 0 时 reason=observing-no-eye-offset（:679-684）；否则 readEyeOffsetZ 成功则 offsetZ=offset.z、source=netprop、reason=applied（:686-694）。结构体默认值 source=fallback、offsetTick=-1 见 :655-662。

B 卡结论：判定样本 B2 PASS（四个轴都在判据内，且 z 的期望与旧常量差 23 u，判别力充分）；B1 记 COINCIDENT / NOT VERIFIED。卡级 PASS（accept-table.md:21）。

## 6. N12-C：快照选择与新鲜度界

### 6.1 两列读数（(a) 选择正确性 / (b) 新鲜度界）

| 样本 | (a) selection_correct | (a) 依据 | (b) freshness_bound | (b) 依据 |
|---|---|---|---|---|
| B1 stv-t20000 | True | 取到 ≤T 的最新检查点 demo tick 18901，产品 resolved 也是 18901；跟随目标身份未换（entity 2）；xy 与预言机差 0.0009 / 0.0322 | 1100 tick | accept.json stale_c 的键 'B1 STV-f12 t20000 (standing, entity 2)'（server 29957 / demo 18901 / stale 1100） |
| B2 stv-t40000 | True | 取到 ≤T 的最新检查点 39986，产品 resolved 39986；身份未换（entity 2）；xy 差 0.0335 / 0.0385 | 15 tick | accept.json stale_c 的键 'B2 STV-f12 t40000 (ducked, entity 2)'（server 51042 / demo 39986 / stale 15） |

(a) 的机制证据在源码里：档案分支用 std::upper_bound + prev 取「≤ tick 的最新检查点」（native/src/demo_header_entity.cpp:436-438，注释在 :441：A Checkpoint answer is stale by construction）；live 分支对 packet 做 packet.tick > tick 过滤（:460），因此不会读到未来 tick 的包。两条路径都不会返回晚于查询 tick 的状态。

(b) 的两个数是实测的 stale 值，不是估计值：它们来自 scene: snapshot 行的 stale= 字段（打印点 native/src/main.cpp:10094；调用点在 :10082-10084）。

卡级仍记 **BOUNDED-STALE**：阈值 stale ≤ 1 tick 没有达到，也没有被放宽。**BOUNDED-STALE 的语义是「上限存在且被实测记录下来」**，它不是 NOT VERIFIED（我们确实测到了数），也不是放宽后的 PASS。

### 6.2 archive gap 分布（entityHistoryArchive 检查点 tick 列表）

方法：只读探针 native/build-nmake/entity_model_probe.exe --history-stats <demo> 打印每份 demo 的 entityHistoryArchive 检查点 tick 列表并给出 gap 统计；原始输出在 N12-QA/n12c-archive-raw/*.stderr.txt 与 *.stdout.json，探针日志 n12c-archive-probe.log（三份 rc=0：pov_f12 用时 121354 ms、stv_f12 29716 ms、upward 35971 ms）。规范化结果写入 N12-QA/archive-gaps.json（由 tick 列表重算 gap，并与探针自己报的 gap_* 逐项断言相等，self_check.all_ok = true）。

| demo | 检查点数 | tick 跨度 | gap median | gap p95 | gap max | gap mean | 均匀假设下期望 stale |
|---|---|---|---|---|---|---|---|
| pov_f12（pl_upward_f12 POV） | 96 | 2177..306193 | **3152** | **4426** | **4612** | 3200.168 | 1600.08 |
| stv_f12（pl_upward_f12 STV） | 96 | 11057..95845 | **887** | **1221** | **1269** | 892.505 | 446.25 |
| upward（pl_upward POV） | 96 | 1265..125879 | **1288** | **1797** | **1888** | 1311.726 | 655.86 |

口径：gap 数 n = 95/份；百分位用 nearest-rank，index = ceil(q*n) − 1；span 与 first/last gap 见 archive-gaps.json。三份 demo 都只留 96 个检查点，等于 demo_header.h:318-323 的 archiveMax = 96 上限；三份的 gapDropped = 0、flushes = 854 / 279 / 331、liveCheckpoints = 3 / 3 / 7（探针 stderr 原文）。

读数含义：在任何一次扫描里，最新可用检查点平均落后 887 到 3200 tick（取决于哪份 demo、哪一段）；p95 到 4426、max 到 4612（pov_f12）。B1 的 1100 与 B2 的 15 都落在这条分布里（B1 在 stv_f12 分布的中段，B2 是检查点恰好落在查询 tick 附近 15 tick 内的对齐巧合）。

### 6.3 为什么改选择规则到不了 stale ≤ 1 tick

1. **扫描是一次性的**：entityHistoryArchive 在打开 demo 时一次性全量扫描（native/src/demo_header.cpp:239、:289；唯一触发点在 native/src/main.cpp:3489-3492），之后只维护有限留存。
2. **留存有界且会丢弃旧状态**：limits 是 maxEvents 8192 / maxCheckpoints 8 / checkpointStride 128 / archiveMax 96（native/src/demo_header.h:318-323）；flush 在 native/src/demo_header_entity.cpp:183-185 触发，动作在 :193-199 — append 一个检查点后把 events / packets / checkpoints 三个容器 clear()。被 clear 掉的状态在查询时已经不存在了。
3. **live 分支钉在扫描尾部**：liveWindow 只在扫描位置附近有效（native/src/demo_header_entity.cpp:429-430），:452-487 的 live 重放路径要求 packet.tick 落在窗口内且不晚于 tick（:460）。mid-demo 查询不落在窗口里，就走不到 live 重放。
4. **mid-demo 能选到的最新数据就是档案里的稀疏检查点**：档案分支 upper_bound + prev（:436-438）返回的那个 Checkpoint 就是全部可得的最新状态；更晚的状态在扫描时已被第 2 条丢弃。

因此，「改选择规则」能动的只有「在已有候选里挑哪个」；候选本身由留存密度决定。要让 stale ≤ 1 tick，必须提高留存密度或改变留存策略（P1 定点窄留存 / P2 续扫 / P3 提高密度，见 n12c-retention-rootcause.md），而不是改 upper_bound/prev 或加别的过滤条件。档案 gap 的 median/p95/max 就是这条结论的定量形式：pov_f12 上中位数 3152 tick，任何「只挑不存」的改法都不可能把这个数降到 1。

### 6.4 更正记录（旧版措辞「可达但不可选」按机制改写）

旧版把 stale ≤ 1 tick 的状态称为「**可达但不可选**」，这个说法把责任放在选择规则上，与实测不符，予以更正。

- 更正后的措辞：**可达，但只在档案恰好命中时，不是靠重放拿到的。**
- 机制：可达性来自「检查点 tick 恰好等于查询 tick」这一对齐事件（此时 prev 取到的就是那一格，stale = 0），而不是来自 live 重放分支。选择规则本身没有任何一处挡住它 —— (a) selection_correct 为 True 说的就是这件事；更晚的状态在扫描期已被 clear() 丢弃（§6.3 第 2 条），所以「选不到」不是规则问题，是候选集问题。
- 佐证：六帧 20525 条 snapshot 行里 stale == 0 出现 17 次（§6.5），说明「恰好命中」在语料里真实存在；只是不在四个注册样本 tick 上（B1 1100、B2 15）。
- 因此责任归属更正为：**天花板在留存策略（EntityHistoryLimits），不在选择规则**；这同时意味着一件事 —— 如果 stale == 0 在语料里从未出现，那这条界就是语料上限、产品无责；它出现了，只是不在注册样本上，所以这条界应当算在留存设计头上。

### 6.5 快照查询普查（status 分布）

| frame | snapshot 行 | status 计数 | stale 范围 | stale == 0 行 |
|---|---|---|---|---|
| pov-t20000 | 1872 | 1=Checkpoint × 1872 | 0..4058 | 1 |
| pov-t40000 | 5012 | 1=Checkpoint × 5012 | 0..4059 | 2 |
| upward-pov-t20000 | 3026 | 1=Checkpoint × 3026 | 0..1819 | 3 |
| upward-pov-t39999 | 5517 | 1=Checkpoint × 5517 | 0..1884 | 6 |
| stv-t20000 | 1791 | 1=Checkpoint × 1791 | 0..1186 | 2 |
| stv-t40000 | 3307 | 1=Checkpoint × 3307 | 0..1264 | 3 |

合计 20525 行，status=0（live 重放）**0 行**，stale == 0 共 17 行。数字由 n12_postb_census.py 独立重算（n12-postB-census.json），与 accept-table.md §7e（:257-277）一致。

## 7. TICK-ALIGN：产品 offsetTick 与预言机 prop_tick 的口径

产品打印的 offsetTick 是 server 空间，预言机 prop_tick 是 demo 空间；两者直接相减没有意义。下表把两套空间分开：

| 样本 | demo tick | server tick | snapshot stale | offsetTick(server) | offsetAge | 映射后 offsetTick(demo) | 预言机 prop_tick | 产品 resolved demo tick | 预言机写入是否晚于产品快照 | 判定 |
|---|---|---|---|---|---|---|---|---|---|---|
| A1 | 20000 | 22203 | 2372 | 19488 | 343 | 17285 | 19660 | 17628 | 是（19660 > 17628） | PASS-ON-VALUE / OPEN-ON-PROVENANCE |
| A2 | 40000 | 42201 | 2932 | 39169 | 100 | 36968 | 39927 | 37068 | 是（39927 > 37068） | 同上 |
| A3 | 39999 | 41272 | 1091 | 39791 | 390 | 38518 | 39701 | 38908 | 是（39701 > 38908） | 同上 |

会计恒等式在三帧都成立：offsetTick + offsetAge == serverTick − stale（A1：19488 + 343 = 19831 = 22203 − 2372；A2、A3 同式成立，accept.json tick_align.accounting_identity_holds）。

结论：产品读到的不是预言机最新那次写入，而是同一数值更早的一次写入 —— 因为产品快照本身落后（demo 17628 vs 预言机最新写入 19660），最新那次写入在产品视野之外。判 **PASS-ON-VALUE / OPEN-ON-PROVENANCE**：数值对得上，但「产品读到的究竟是哪一次写入」没有被证明，记为开放项。

两个口径陷阱（本报告取数时都踩到过）：

1. raw gap 是混合空间相减，符号会翻：A3 的 raw −90 映射到 demo 空间后是 +1183（accept.json raw_gap_sign_disagrees = true）。
2. age 差是口径差：产品 age = resolvedSceneTick − offsetTick，预言机 age = demo_tick − prop_tick；N12-C 起统一用产品口径。

## 8. 负控（13 条，全部 red = true）

负控来自 accept.py 的自检集，每一条都是「必须红」的注入：如果某条通过了，说明判据本身失效。accept.json control_summary 逐条记 red_as_required = true，13/13。

| 编号 | 注入 | 要求 | 实测 |
|---|---|---|---|
| N1a | 用 T+10 的预言机（20010）比产品 20000 | FAIL | FAIL |
| N1b | 40010 比 40000 | FAIL | FAIL |
| N1c | 40009 比 39999 | FAIL | FAIL |
| N2a | POV 预言机比 STV 产品（样本类别错配） | FAIL | FAIL |
| N2b | STV 预言机比 POV 产品 | FAIL | FAIL |
| N3 | 把期望全换成 (0,0,0)/(0,0) | FAIL | FAIL |
| EX-1 | 用表值 68.0 冒充预言机（无 last-write tick） | INVALID EVIDENCE | gate=INVALID_TABLE |
| EX-2 | 目标缺 viewOffset 时不得回填常量 | INVALID EVIDENCE | gate=INVALID_TABLE |
| EX-3 | 死亡机位不得补眼高 | PASS（不抬高） | 896.0 = origin 896.0，reason=dead-no-eye-offset |
| EX-4 | lastViewEntity 默认值 / signon 冻结实体不得变成相机 | FAIL | 派生意 [-930.244,-2314.286,500.779] 对产品 [-536.0,-479.8,586.0] |
| EX-5 | 存在未来样本时不得选中 | REFUSED | T=19997，最早样本 19998，按 d ≤ 0 拒绝 |
| EX-6 | 目标失踪时记 NOT VERIFIED，不静默换邻居 | NOT VERIFIED | entity 999999 不退回 nearest player |
| EX-7 | 同 tick 两样本必须取 max(sequence_out) | 突变体 FAIL | 正确 seqout 187163，首写者 187162 的突变体红 |

EX-3、EX-5、EX-6、EX-7 是「不得误判」类；N1a–N3 是「判据本身能否被伪造」类；EX-1/EX-2 是证据门（不合格证据直接判 INVALID，不参与打分）。

### 8.1 EX-3 与 B1 的分辨力对照（为什么一个 PASS 一个 COINCIDENT）

- EX-3：死亡机位的候选差是 68 u 到 75 u 量级，实测散布 0.0000，且 offsetTick = −1 是「未读」哨兵，与 life=2 / obs=1 互证 —— 判别力充分，PASS 是有效证据。
- B1：期望值与旧常量只差 0.0001 u（68.000065 对 68.0），低于打印精度 —— 判别力为零，所以记 COINCIDENT 而不是 PASS。这是分辨率问题，不是产品问题。

## 9. 三列对照（oldexe / preB / postB）

| 读数 | oldexe（cfcdbe5d） | preB（b4a2355e） | postB（1c3580e2） |
|---|---|---|---|
| A1 眼高 z | 662.5（Δ −75.0296 FAIL） | 737.5（Δ −0.0296 PASS） | 737.5（Δ −0.0296 PASS） |
| A2 眼高 z | 260.0（Δ −74.9775 FAIL） | 335.0（Δ +0.0225 PASS） | 335.0（Δ +0.0225 PASS） |
| A3 眼高 z | 528.0（Δ −68.0313 FAIL） | 596.0（Δ −0.0313 PASS） | 596.0（Δ −0.0313 PASS） |
| B1 眼高 z | 289.9（COINCIDENT） | 289.9（COINCIDENT） | 289.9（COINCIDENT） |
| B2 眼高 z | 609.0（Δ +23.0059 FAIL） | 609.0（Δ +23.0059 FAIL） | 586.0（Δ +0.0059 PASS） |
| 卡 N12-A | FAIL | PASS | PASS |
| 卡 N12-B | FAIL | FAIL | PASS |
| 卡 N12-C | BOUNDED-STALE | BOUNDED-STALE | BOUNDED-STALE |
| snapshot 行 | 20620 | 14491 | 20525 |
| status=0（live） | 0 | 0 | 0 |
| stale == 0 行 | 19 | 15 | 17 |

两列历史读数按其原始采集集记（oldexe 20620 行、preB 14491 行、postB 20525 行 —— 采集集不同，行数不可直接横比）。oldexe/preB 两列的 z 值与卡级见 accept-oldexe.json / accept-table-oldexe.md、accept-preB.json / accept-table-preB.md。

三条 A 卡 Δ 在 oldexe 列都是 68–75 u 量级，正好等于被漏掉的 m_vecViewOffset[2] 本身的量级；B2 的 +23.0 则等于「常量 68.0 减下蹲真实偏移 45.0」。EX-3（896.0）与 B1（289.9）在三列逐位不变 —— 修复没有副作用。

## 10. 观战为什么不走 N12-A 的 POV 路径（三证）

1. **STV 帧没有 eye 行**：grep -c "scene: eye " 在 stv-t20000 / stv-t40000 上都是 0，而 POV 帧的 eye 行数与 camera 行数、snapshot 行数一致（1872 / 5012 / 3026 / 5517）。eye 行的打印点在 if (haveObserverView) 内部（native/src/main.cpp:10146、:10156）。STV 帧 haveObserverView 为假，因此不走 POV/eye 路径。
2. **观战相机由 placeSpectatorView 单独放置**：native/src/main.cpp:715-750，eye z = origin[2] + relayEye.offsetZ − forward[2] * kSpectatorBackUnits（:736-740），relayEye 来自 resolveRelayEye（:664-696），修复后读被跟随目标的 m_vecViewOffset[2]。卡 B 的判定因此用相机行（scene: camera … eye=…），不用 eye 行。
3. **POV 路径是另一个函数**：POV 眼高由 resolvePovEye（native/src/main.cpp:572）产出，调用点 :10248，打印点 :10261；与观战相机是两条独立的代码路径。

所以 N12-A 与 N12-B 的证据面不同：A 卡看 eye 行（POV），B 卡看 camera 行（STV）。把两者混起来比（例如拿 STV 相机眼去比 POV 预言机）正是负控 N2a/N2b 要抓的错。

### 10.1 站/蹲固定效应（B 卡的唯一主证据）

| 目标 | 真实 viewOffset[2] | 产品 −（origin + viewOffset） | 说明 |
|---|---|---|---|
| B1 站立 | 68.00006484991536 | 289.9 − 289.91501236 = −0.0150（打印舍入） | 与常量不可分辨 |
| B2 下蹲 | 45.000042915385166 | 586.0 − 585.99406147 = +0.0059（打印舍入） | 与常量差 23 u，可分辨 |

站/蹲的差是同一实体、同一路径上的固定效应；跨实体比较不能作为证据（不同玩家的 origin 不同）。

## 11. relay 行与新鲜度字段

- pre-B 的 relay 行不带来源字段（stv-t20000 24 条、stv-t40000 46 条，has_tail = no）；post-B 的 relay 行 24/24 与 46/46 都带尾部：offsetZ、offsetSrc、offsetTick、offsetAge、life、obs、reason。offsetSrc 实测全是 netprop（24 条与 46 条），offsetZ 取值集合只有 {45.0000, 63.0320, 68.0001}。
- 原行示例（stv-t20000 首条与末条）：

~~~
36769.9 spectate: camera relay target=2 eye=308.7,-1983.7,199.7 pitch=10.94 yaw=354.37 behind=0 offsetZ=68.0001 offsetSrc=netprop offsetTick=11057 offsetAge=0 life=0 obs=0 reason=applied
72005.8 spectate: camera relay target=2 eye=-348.9,-1444.0,289.9 pitch=10.94 yaw=334.66 behind=0 offsetZ=68.0001 offsetSrc=netprop offsetTick=29614 offsetAge=343 life=0 obs=0 reason=applied
~~~

- **silent_freshness_claim**：相机行（scene: camera）不带 stale=；新鲜度只在 scene: snapshot 行里（status / stale / states 三段，打印点 native/src/main.cpp:10094）。relay 行的新鲜度是 offsetTick / offsetAge 表达的，不是 stale。所以「相机用了多旧的状态」只有一个来源：同 tick 的 snapshot 行。任何「相机行显示新鲜」的说法都没有证据面。

## 12. 三个脚本级陷阱（都会造成假红/假绿）

1. **D_direct 不能拿相机眼比**：D_direct 的比较对象是产品自己打印的 origin（accept.py 的 product_origin_source），不是相机眼。拿相机眼比会得到 +75.0 的假红。旧 exe 没有 eye 行时才退回相机眼。
2. **CRLF 自测**：arch-ci 的 D53 class-pose-family-check.sh:130 曾把行尾带 CR 的 1 判成非 1，造成 40 条假红。本轮 accept.py 的 CRLF 自测 PASS（accept.json crlf_selftest）。
3. **relay 行尾部新增字段的位置取数风险**：现存消费者都按名字取值（camera-ui-check.sh:133-148 用 num($0,"target")；evidence/t79-camera/run-pix.sh:99 用 sed 按 target= 锚定；run-pix.sh:132-134 的 cut -d, -f1..3 切的是自己拼的 ORIGIN 变量）。将来若有脚本按列号取字段（awk '{print $5}' 之类），会在 relay 行尾部加字段时静默读错。

## 13. 未证与覆盖缺口（NOT VERIFIED）

- **live 重放分支从未被观测**：六帧 20525 条 snapshot 全是 status=1（Checkpoint），status=0 为 0；而 live 重放返回 Available=0 的路径在 native/src/demo_header_entity.cpp:452-487。本轮没有任何一次查询落到该分支。这是覆盖缺口，不是已发现的缺陷。
- **从 6dd853e 重建 exe 能否逐字节一致**：未重建，NOT VERIFIED（§2.1）。
- **offsetTick 的 provenance**（产品读到的是哪一次写入）：OPEN-ON-PROVENANCE（§7）。
- **N12-C 的 1 tick 上限**：没有达到，只在档案恰好命中时出现 stale = 0；界由留存决定（§6.3）。
- **其他地图 / 其他 class / 其他视图（非 POV、非 STV follow）/ 音频与性能**：本轮没有采集，未证。
- **本表不证明观战相机与游戏真实导播相机一致** —— 它只证明两台被采样相机各自的眼高读数与预言机的关系。

## 14. 本文件没有证明什么

- 没有证明 N12-C 的选择规则能在 stale ≤ 1 tick 内工作：实测上限是留存给的，注册样本是 1100 与 15 tick。
- 没有证明 live 重放分支（Available）在任何真实查询下被走到：六帧全是 Checkpoint。
- 没有证明 offsetTick 的 provenance，也就是没有证明产品读到的具体是预言机里的哪一次写入。
- 没有证明 exe 1c3580e2 能由 HEAD 6dd853e 干净重建得到。
- 没有证明 N12-B 的修复对下蹲以外的姿态（趴、蹲+站切换的过渡帧）有效；采样只有站立与下蹲两帧。
- 没有证明其他地图、其他观察者模式、第一人称以外的视口、以及性能与音频表现。
- 没有证明观战相机等价于游戏真实导播相机。

## 15. 交付物、状态

| 文件 | 说明 | sha256（前 8） |
|---|---|---|
| docs/N12-ACCEPTANCE-2026-11-15.md | 本报告 | 见交付行 |
| N12-QA/accept.py | 判定脚本（已改：优先读 archive-gaps.json、freshness_bound 带 corpus 分布、§7h 六列） | c41bfc4a |
| N12-QA/check-6.py | 6 项检查脚本 | fc62a005 |
| N12-QA/accept.json | post-B 判定结果 | 275b3b9c |
| N12-QA/accept-table.md | post-B 判定表（英文，脚本生成） | 2b754e58 |
| N12-QA/archive-gaps.json | 三份 demo 的 archive 检查点 tick 列表与 gap 分布 | 5f7a545c |
| N12-QA/check-6.json | 6 项检查结果 | b0e5e88c |
| N12-QA/capture-1c3580e2/ | 六帧 trace / bmp / run.txt / identity.txt | — |
| N12-QA/capture-b4a2355e/ | pre-B 采集（对照用） | — |
| N12-QA/n12c-archive-raw/ + n12c-archive-probe.log | 探针原始输出与日志 | — |
| N12-QA/n12_postb_census.py + n12-postB-census.json | §6.5 普查的脚本与结果 | — |
| N12-QA/n12_z_rows.py / n12_a1a3_rows.py / n12_b_rows.py / n12_tickalign.py / n12_controls_dump.py | 取数辅助脚本 | — |
| accept-oldexe.json / accept-table-oldexe.md / accept-preB.json / accept-table-preB.md | 两列历史对照 | — |

状态：**未提交**（按分工，n12-qa 不 commit；改动留在工作树，由 lead 统一提交）。本轮不启动产品 exe、不起 hostlock、不碰 native/。

## 16. N12-C 第二步对验收的增量要求（lead，2026-11-14 22:50）

§1–§15 描述的是 exe `1c3580e2…`（2,991,616 B）上的 A/B 终态。第二步落了消费端（`native/src/main.cpp` +480），产品 trace 与验收脚本都要跟着改口径：

- **`accept.py` 的会计恒等式只在快照来源上成立**：`D:/_shared/EXEC-2026-11-15/N12-QA/accept.py:1143-1144` 断言 `resolved_server == offsetTick + offsetAge`（`resolved_server = serv - stale`）。相机路径命中时 `offsetTick = row.tick = scene tick`、`offsetAge = 0`，只有 `stale = 0` 时恒等式才成立 ⇒ **一旦命中就 FAIL**。本语料 0 次命中，所以这问题是潜伏的。验收脚本必须按新增行里的 `src=camera | camera-backfill | snapshot` 分支：camera 行断言 `offsetTick == clock serverTick && offsetAge == 0`，snapshot 行才用 §7 的恒等式。**不改产品去凑门禁**（lead 裁决）。
- **新增两行，既有行不变**：每帧 `scene: eye row src=… reqTick=… rowTick=… backfill=… flags=… rowOrigin=… rowOffsetZ=… srcOrigin=… offsetAge=… miss=…`（紧跟 frozen `scene: eye` 行），退出时一次 `camera: eye summary …`（30 个计数器）。frozen `scene: eye origin=` / `scene: snapshot` / `spectate: camera relay target=` 的格式串与逐 tick 取值都未变（247/247、247/247、105/105 共同 tick 逐字节相同，见 §6 与 EXEC-PLAN 22:50 节）。
- **A/B 结论的 binding exe 要写清**：本轮 A/B 读数绑定 `1c3580e2…`；第二步之后的产品 exe 是 `ba109055…`（2,996,736 → 3,002,880 B）。A/B 没有在这个新 exe 上重跑，**不得把 A/B 结论直接挂到新 exe 上**。
- **`scene: eye` 行的数值仍然是快照推导来的**：本语料每行相机样本 `flags=25`，`kCameraHasViewOffset`/`kCameraHasAngles` 从未置位，`povApplied=0`。也就是说 §4/§5 那套"眼高来自 `m_vecViewOffset[2]`"的读数**没有变成**"眼高来自相机 tick 行"。要等数据面修好（接受扁平分量并置位）才能改这句话。
- **因此本卡的下一步验收判据**：先看新行 `src` 是否为 `camera`/`camera-backfill`、`povApplied > 0`，再看 `eye[2] - rowOrigin[2]` 是否等于 `rowOffsetZ`（print 量子：frozen 行 `%.1f`、`offsetZ` `%.4f`、新行 4 dp/6 dp）。在 `povApplied` 仍为 0 的 exe 上继续用 §4/§5 的判据。
## 17. 第三步之后：相机命中已触发，A1 按新 exe 重基线（lead，2026-11-14 23:40）

第三步（`native/src/demo_header_camera.cpp` +37，`docs/N12-C-STEP3-IMPL-2026-11-15.md`）修好了数据面：Source 把 `m_vecViewOffset` / `m_angEyeAngles` 发成**扁平分量**（`DT_LocalPlayerExclusive.m_vecViewOffset[0..2]`、`DT_TFLocalPlayerExclusive.m_angEyeAngles[0..1]`，由 `--props 9` 实测，676 条属性里没有整向量名），解码器现在按实测名补回 `viewOffsetZ`/`angles`，行 `flags` 由 25 变 31。结果：

| 运行 | exe | `src=camera` 行 | `povApplied` | 越界回退 | exe sha256 |
| --- | --- | --- | --- | --- | --- |
| pov t20000 | `90f781b5…` | **1407** | 1407 | 22 | `90f781b54c96ac88cbef2502afd06a159b9d643d01579bddf85cff94a97551df` |
| pov t8415 | 同上 | **599** | 599 | 15 | 同上 |
| pov t2000 | 同上 | **146** | 146 | 8 | 同上 |
| stv t20000 | 同上 | 0（全部 `identity-mismatch`） | 0 | 999 | 同上 |

### 17.1 A1/A2/A3 的当前行（新 exe，按 playback tick 末条）

```
scene: eye origin=2119.7,-265.6,662.5 offsetZ=75.0001 eye=2119.7,-265.6,737.5 viewEntity=9 originTick=22203 offsetTick=22203 offsetAge=0 life=0 obs=0 reason=applied   # A1 (t=20000)
scene: eye origin=596.0,1512.0,576.0  offsetZ=75.0001 eye=596.0,1512.0,651.0  viewEntity=9 originTick=10617 offsetTick=10617 offsetAge=0 life=0 obs=0 reason=applied   # A2 (t=8415)
scene: eye origin=1797.4,-1398.0,312.9 offsetZ=75.0001 eye=1797.4,-1398.0,387.9 viewEntity=9 originTick=4203 offsetTick=4203 offsetAge=0 life=0 obs=0 reason=applied   # A3 (t=2000)
```

与 §4 的 `1c3580e2` 行相比：**eye 的 z 三个样本都没变**（737.5 / 651.0 / 387.9，因为这三个 tick 的轨道 `viewOffsetZ` 本来就是 75.0001），变的是 tick 三字段（`originTick`/`offsetTick` 现在是轨道行的 server tick，`offsetAge=0`）。**所以 A1/A2/A3 的数值判据继续成立**，只是 provenance 换了来源。

### 17.2 眼高在 30 个 tick 上真的变了（这是修复生效，不是回归）

旧 exe 在这些 tick 上用快照推导的 75.0001；新 exe 用该 tick 轨道行的真实值。例：

```
scene: eye origin=210.0,-2078.2,297.3 offsetZ=45.0000 eye=210.0,-2078.2,342.3 viewEntity=9 originTick=2416 offsetTick=2416 offsetAge=0 life=0 obs=0 reason=applied
scene: eye row  src=camera reqTick=2416 rowTick=2416 backfill=0 flags=31 rowOrigin=210.0067,-2078.1704,297.3045 rowOffsetZ=45.0000 srcOrigin=210.0067,-2078.1704,297.3045 offsetAge=0 miss=
```

`probe.dense.rows.txt` 显示 tick 2425–2432 为 `45.000042`、2433–2440 为 `75.000069`，是阶跃；另有 71.1273 / 74.5191 / 74.9446 / 65.5079 / 68.5201 / 62.2425 / 48.8728 等过渡值分布在别的 tick。**45 的物理含义（蹲/站）标 NOT VERIFIED**，本卡的判据只许写成「eye = 该 tick 轨道行的 `rowOrigin_z + rowOffsetZ`」，不许写成「与零售渲染一致」。

### 17.3 accept.py 必须按 src 分支（硬要求，不改产品）

`accept.py:1143-1144` 的 `resolved_server == offsetTick + offsetAge` 是**快照来源专用**会计。相机行满足 `offsetTick == originTick == row.tick`、`offsetAge == 0`，代入恒等式会得到 `serv == serv` 只在 `stale == 0` 时成立，而 A1 的 `stale=2372` ⇒ 一旦命中必然 FAIL（本语料在第二步是 0 命中所以潜伏）。要求的实现：

- 读同一帧配对的 `scene: eye row … src=…`；`src in {camera, camera-backfill}` → 断言 `originTick == offsetTick == rowTick`、`offsetAge == 0`、`eye_z − rowOrigin_z − rowOffsetZ` 在 **0.1 u** 内（frozen 行 origin/eye 各 1 位小数，量子 0.05+0.05）。
- `src == snapshot` → 用原恒等式（`offsetTick + offsetAge == serverTick − stale`），§7 的 TICK-ALIGN 表不变。
- 断言不得回读 `povApplied` 的**整数行数**做等价比较（每次运行帧数不同）；按 tick 归键比对。

### 17.4 回归护栏（必须在后续 exe 上继续成立）

- `scene: clock tick=` 与 `scene: snapshot` 在 POV 运行里**逐字节不变**（第三步 keyed diff：149/149、75/75、28/28 全同）。
- STV 的 `spectate: camera relay target=` 4 条**逐字节不变**（`offsetSrc=netprop`、`offsetTick=11057`）。
- `demo_open_probe.exe --scan`（3171 B `8007b92b…`）与 `--scan --camera-track`（3763 B `3ef091d5…`）与第一步基线 `cmp` 通过。

### 17.5 §1–§15 的效力边界

§1–§15 的证据绑定 exe `1c3580e2…`（2,991,616 B）。第三步之后：**数值判据（A1/A2/A3 的 eye、offsetZ、x/y、角度）仍然有效**；**provenance 判据（tick 字段、会计恒等式、`povApplied=0` 的描述）失效**，以本节为准。§16 描述的「下一步判据」现已满足（`povApplied > 0`、`src=camera`），故 §16 转为历史记录。
