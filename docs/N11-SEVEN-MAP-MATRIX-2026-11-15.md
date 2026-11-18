# N11 七图地图最终矩阵（POV × STV）

- 撰写者：n11matrix（N11 实例，新实例）
- 日期：2026-11-15
- 仓库：D:/TF2_Native_Worktrees/t105-e5，分支 candidate/e5，HEAD 6dd853e
- 本卡**只读**：未改代码、未构建、未运行产品 exe、未起 hostlock、未 commit。本文所有读数来自已落盘产物，加上本轮只读探针（sha256sum / stat / grep / BMP 逐字节解码）。
- 辅助脚本与中间产物：D:/_shared/EXEC-2026-11-15/n11/（d08diff.py、scan-pov.sh、scan-pov.txt）。

---

## 0. 结论：14 格只有 2 格合格

七图 × 两列 = 14 格。按卡面「具名物件 **且** 同机位对照」的双条件统计：

| 列 | 具名物件 + 同机位对照 | 具名物件、无同机位对照 | 连录制帧都没有 |
|---|---|---|---|
| POV（7 格） | **1**（pl_upward_f12） | 0 | **6**（process / gullywash / sultry / sunshine / bagel / product） |
| STV（7 格） | **1**（pl_upward_f12） | **6**（同上六图） | 0 |

- 唯一合格的 2 格都在 pl_upward_f12：POV 有 A1/A2/A3 三帧 + 跨构建同帧（b4a2355e 与 1c3580e2 两次构建，五帧 sha256 前 16 位完全相同）；STV 有 --world-sky/--no-world-sky 同机位开关对（N10 §4.3）+ 跨构建对（N12 §2.3）。
- 六图 STV 有帧、有相机真值、有具名控制点实体、有逐帧 PVS 读数，但**没有第二张任何形态的同机位对照**（G2 trace 里 world face-trace 命中 0，也没有逐物件屏幕矩形）。
- 六图 POV **一帧都没有抓过**：POV demo 在盘上（清单见 §1.2），但没有任何一次 POV 抓帧产物。
- 结论：D01（docs/DEFECT-REGISTER-MASTER-2026-11-14.md 表内 D01 行）**不能关闭**。本文件把「矩阵未做」细化为「每格缺什么、下一步打哪个 tick、期望读到什么数」。

---

## 1. 身份：这 14 格分别来自哪个 exe

### 1.1 六套证据，四个 exe

| 证据集 | exe sha256 | exe 字节 | 记录的 HEAD | demo | tick | 目录 |
|---|---|---|---|---|---|---|
| E1 G2 七图世界帧 | cfcdbe5d48c6e71e… | 2,988,544（此项取自 D08 §表；G2 的 identity.txt 只记 sha 与 head，未记字节，见 §7-③） | 1dfefb14a6403fd0 | 七图**各自 STV** demo（见 1.2） | 30000 | D:/_shared/EXEC-2026-11-14/G2/world-frames/ |
| E2 D08 七图面板对 | cfcdbe5d48c6e71e… | 2,988,544 | a153030（sidecar sourceTree=a15303062d5e dirty=1） | 七图各自 demo | 0 | D:/_shared/EXEC-2026-11-14/G1/minimap-five/ |
| E3 N14 七图面板 | b4a2355ecc0a9fe8… | 2,991,104 | a901ecec921253312e46f24a3cebc9f41e9b5805 | 七图各自 demo | 0 | D:/_shared/EXEC-2026-11-15/n14/captures/ |
| E4 N10 天空开关对 | 1c3580e278a16019… | 2,991,616 | **6dd853e**（n10-capture/identity.txt） | upward STV a9630c65…（75,663,754 B，sha256 7e047aff…） | 40000 | D:/_shared/EXEC-2026-11-15/n10-capture/ |
| E5 N12 六帧（修后） | 1c3580e278a16019… | 2,991,616 | **83657f10**（capture-1c3580e2/identity.txt） | upward STV 同上；upward POV 14b4943a…；pl_upward POV 708683e1… | 20000 / 40000 / 39999 | D:/_shared/EXEC-2026-11-15/N12-QA/capture-1c3580e2/ |
| E6 N12 六帧（修前） | b4a2355ecc0a9fe8… | 2,991,104 | a901ecec… | 同上 | 同上 | D:/_shared/EXEC-2026-11-15/N12-QA/capture-b4a2355e/ |
| E7 919/849 三视图 | 00ec8b99（docs/PHASE-F-REPORT-2026-11-13.md §12） | — | sourceTree f533293 | process demo（PHASE-F §12） | 0（--start-paused） | D:/_shared/EXEC-2026-11-13/F1/black/out3/ |

**这张表本身就是一条结论**：七图证据横跨 4 个 exe。**没有任何一个 exe 同时拥有七图的 POV 帧与 STV 帧**——POV 帧只有 upward（1c3580e2 的 E5）；七图 STV 帧只在 cfcdbe5d（E1）上。这正是 D01 说的「未在同一 exe 上完成」。

### 1.2 七图两列可用的录制文件（本轮 ls 实测，字节数原样）

| 图 | POV demo | 字节 | STV demo | 字节 |
|---|---|---|---|---|
| cp_process_f12 | POV/autorecord_2026-09-03_21-05-39.dem | 100,302,450 | STV/1338f5de…matcha-20260927-1312-cp_process_f12.dem | 90,995,077 |
| cp_gullywash_f9 | POV/autorecord_2026-05-21_21-08-36.dem | 63,296,494 | STV/gully4454.dem | 81,660,813 |
| cp_sultry_b8a | POV/autorecord_2026-06-16_21-42-44.dem | 56,193,150 | STV/f63a7a20…matcha-20260703-1442-cp_sultry_b8a.dem | 85,506,321 |
| cp_sunshine | POV/autorecord_2026-05-17_16-32-36.dem | 75,905,225 | STV/SUNSHINE.dem | 82,759,239 |
| koth_bagel_rc11 | POV/**testdemo111.dem** | **5,497,243** | STV/dfbce608…match-20261101-1959-koth_bagel_rc11.dem | 64,600,031 |
| koth_product_final | POV/autorecord_2026-09-10_21-01-05.dem | 100,388,768 | STV/prodcutscout.dem | 62,389,932 |
| pl_upward_f12 | POV/autorecord_2026-08-08_21-43-59.dem | 146,853,666 | STV/a9630c65…match-20261030-2145-pl_upward_f12.dem | 75,663,754 |
| （备用）pl_upward | POV/autorecord_2026-11-13_13-59-18.dem | 56,328,194 | — | — |

koth_bagel_rc11 的 POV 只有 5.5 MB（其余 POV 56–147 MB，STV 62–91 MB），文件名是 testdemo111.dem。**它是否是一场有效比赛录制 NOT VERIFIED**（见 §8-⑩ 与 §7-⑧）。

---

## 2. 主矩阵

### 2.1 STV 列（七行）

统一说明：E1 的七帧全部来自**各图 STV demo**（逐图 ui.ini 的 recentDemo0 七条全指 STV 目录，本轮实测），tick=30000，exe cfcdbe5d，trace 里最后一条相机行为 src=follow（跟随目标推导），**不是**录制相机真值。下表「PVS」是同一帧的 scene: staticProps 行（instances/visible/pvsRejected/drawn/eyeCluster）。

| 图 | 相机真值（trace 最后一条 scene: camera，原样数字） | 具名物件（entity / model / skin / origin） | 同帧 PVS | 天空上传 | 同机位对照 | 判定 |
|---|---|---|---|---|---|---|
| cp_process_f12 | eye=1234.7,292.4,657.9 pitch=2.47 yaw=243.17 src=follow | 320 models/props_gameplay/cap_point_base.mdl skin=2 -3540,-1280,576；323 skin=2 -1664,-2176,768；328 skin=1 1664,2176,768 | 1353 / 212 / 1141 / 212 / 852 | 6 面 36 顶点 12 三角 side=1024 | **无** | 部分：物件✔ 对照✘ |
| cp_gullywash_f9 | eye=1911.1,-786.2,363.0 pitch=25.06 yaw=232.61 src=follow | 273 models/props_gameplay/cap_point_base skin=1 -3766,758,158.84；276 skin=2 3766,-758,158.25；291 skin=2 1041,-1612,161.25 | 1257 / 139 / 1118 / 139 / 1618 | 6 面 side=512 | **无** | 部分 |
| cp_sultry_b8a | eye=-32.2,-243.5,740.0 pitch=-34.94 yaw=331.85 src=follow | 91 models/props_gameplay/cap_point_base skin=2 -2536,796,704.25；127 **models/props_doomsday/cap_point_small.mdl** skin=1 2655.44,-3919.81,657.22 parent=200832；144 同模型 skin=2 -2719.44,3892.16,657.22 parent=452753 | 1866 / 410 / 1456 / 410 / 3800 | 6 面 side=512 | **无** | 部分 |
| cp_sunshine | eye=-5730.8,6244.7,88.7 pitch=1.06 yaw=292.43 src=follow | 213 models/props_gameplay/cap_point_base skin=0 -5632,5248,64.22；216 skin=2 -7704,6824,128；219 skin=1 -3560,3672,128 | 1617 / 372 / 1245 / 372 / 355 | 6 面 side=512 | **无** | 部分 |
| koth_bagel_rc11 | eye=512.0,-380.4,356.2 pitch=4.59 yaw=197.77 src=follow | 675 models/props_gameplay/cap_point_base skin=0 0,0,455；676 skin=0 0,0,273 parent=811680（parentname=towermove） | 1562 / 602 / 960 / 602 / 836 | 6 面 side=512 | **无** | 部分 |
| koth_product_final | eye=-2627.2,977.0,196.4 pitch=4.59 yaw=26.39 src=follow | 42 models/props_gameplay/cap_point_base skin=0 -1536,0,224（全图唯一控制点） | 942 / 305 / 637 / 305 / 757 | 6 面 side=1024 | **无** | 部分 |
| pl_upward_f12 | eye=-145.7,685.8,478.6 pitch=-39.18 yaw=187.21 src=follow | 262/264/265 models/props_trainyard/track_midcap.mdl（1024,-1536,128 / 1536,1088,256 / -1024,624,510） | 1107 / 218 / 889 / 218 / 444 | 6 面 side=512 | **有**：N10 天空开/关（同 tick 40000）；N12 跨构建（t20000/t40000） | **合格** |

### 2.2 POV 列（七行）

| 图 | 帧 | 相机/眼高（trace 原样） | 具名物件 | 同机位对照 | 判定 |
|---|---|---|---|---|---|
| pl_upward_f12 | A1 t20000（E5，exe 1c3580e2，168.5 s）；A2 t40000（195.7 s）；A3 = pl_upward POV t39999（115.6 s，非 f12）；EX-3 t20000 死亡负控（77.9 s） | A1: eye=2119.7,-265.6,737.5 pitch=14.52 yaw=-108.69 **src=demo**，origin=2119.7,-265.6,662.5 offsetZ=75.0001 viewEntity=9，capture: armed requested=20000 frame_tick=20000 frames=1870 | POV trace 同 run：320 bomb_cart.mdl skin=0 -1653.97,-1536,30.94；262 track_midcap.mdl 1024,-1536,128；318 cappoint_hologram.mdl body=2 1024,-1536,136 | **有**：跨构建五帧 sha256 前 16 位全同；跨帧 t20000↔t40000（同 demo 同相机路径） | **合格** |
| 其余六图 | **零帧** | — | — | — | **缺口** |

**为什么「STV 相机不是导播相机」这句需要有证据而不是断言**：E1 七帧的相机行都写 src=follow（跟随目标），E5 的 POV 帧写 src=demo（录制相机）。产品对 STV demo 走的是「跟随目标推导」这条路，所以 STV 侧任何 eye 都是派生值；这不证明它与游戏真实导播相机一致，也不证明不一致——产品根本没有那台相机的输入。

## 3. 逐图明细

三列写法固定：**有帧**（帧的物理身份）→ **具名物件**（trace 里能指名道姓的东西）→ **同机位对照**（同 tick、同相机、只改一个变量的两张）。资料源标记（报告名与真实路径，全部本轮核对过存在）：E1=G2 世界帧（cfcdbe5d，tick 30000），E5=1c3580e2 构建，E6=b4a2355e 构建。报告：**N07**=N07 只读材质缺口复核（`D:/_shared/EXEC-2026-11-15/N07/N07-REPORT.md` §1 七图读数表 :12-20）；**N08/N09/N10**=`docs/N08-N10-MAPS-STATIC-EVIDENCE.md` 单文件三卡（§2=N08 控制点、§3=N09 推车、§4=N10 天空；本文写「N08 §2.1」「N10 §4.3」「N08 :252-254」都指该文件内的小节/行号）；**N12**=`docs/N12-ACCEPTANCE-2026-11-15.md`（§2.3 六帧表、§4 眼高、§8.1 EX-3）；**N13**=`docs/N13-IDENTITY-HUD-UI-2026-11-15.md`；**N14**=`docs/N14-MINIMAP-BASEMAP-2026-11-15.md`（§4 七图测量表、§5.2 相机行）；**D08**=`docs/D08-MINIMAP-SEVEN-MAPS-2026-11-14.md`。

### 3.1 pl_upward_f12 —— 唯一两列都合格的图

**STV 列**（E1，tick 30000，exe cfcdbe5d 48c6e71e…，demo STV/a9630c65…match-20261030-2145-pl_upward_f12.dem 75,663,754 B）

- 有帧：capture: armed requested=30000 frame_tick=30000 frames=<见 trace>；"map: material path=" 18 条（该帧实际引用 18 个材质路径）；"scene: staticProps … instances=1107 visible=218 pvsRejected=889 drawn=218 eyeCluster=444"；天空 "gpu: world sky-upload state=active vertices=36 triangles=12 faces=6 side=512 resampled=1"。
- 具名物件：262 / 264 / 265 全为 models/props_trainyard/track_midcap.mdl（1024,-1536,128 / 1536,1088,256 / -1024,624,510，parent=2097151=world），skin 见 G2 trace；另 bomb_cart 与 cappoint_hologram 在 POV 帧 trace 里出现（§3.1 POV 段）。
- 同帧 PVS：instances 1107 → visible 218 / pvsRejected 889 → drawn 218（该行同帧计数字段，口径见 §2.1）。
- 同机位对照：**有**。(i) N10 §4.3 --world-sky vs --no-world-sky 同 tick 40000（sky_on=450806898cfdacac…、sky_off=dacf9488d364e514…）；(ii) N12 §2.3 跨构建 t20000 / t40000（stv-t20000 两构建同 1e796ab076838274，stv-t40000 由 9168fefdc7c2229f 变 450806898cfdacac）。两对都是"同 demo 同 tick 只改一个变量"。
- 判定：**合格**。仍缺的只有「相邻 tick 序列」与「面级覆盖率」，见 §8。

**POV 列**（E5，exe 1c3580e2 78a16019…，demo POV/autorecord_2026-08-08_21-43-59.dem 146,853,666 B）

- 有帧：pov-t20000.bmp（250de7df310a2a05）、pov-t40000.bmp（8e62c339b7cfa13e）来自 pl_upward_f12 POV demo；upward-pov-t20000.bmp（3ff046064f8b86f3，**= EX-3 负控**：pl_upward POV demo 采样时已死亡，reason=dead-no-eye-offset，不进分母）与 upward-pov-t39999.bmp（83b348447cd34ef5，A3）来自 pl_upward POV demo，是两个不同 demo。
- 相机真值（trace 原文）：165118.2 "scene: eye origin=2119.7,-265.6,662.5 offsetZ=75.0001 eye=2119.7,-265.6,737.5 viewEntity=9 originTick=20000 offsetTick=19488 offsetAge=343 life=0 obs=0 reason=applied"；165130.7 "scene: camera mode=first-person target=1 follows=1872 holds=0 freeze=0 eye=2119.7,-265.6,737.5 pitch=14.52 yaw=-108.69 **src=demo**"；165131.3 "capture: armed requested=20000 frame_tick=20000 frames=1870"。→ 眼高 75.0001 = 站立视高，相机来自录制（src=demo），不是产品推导。
- 具名物件（同一 POV trace，127401.9）：entity=320 path=models/props_trainyard/bomb_cart.mdl skin=0 body=0 origin=-1653.97,-1536.00,30.94 parent=2097151；entity=262 track_midcap.mdl 1024,-1536,128；entity=318 cappoint_hologram.mdl body=2 1024,-1536,136。这三条与 STV 列的 262/264/265 是同图同名物件，但相机完全不同——**这正是"同图不同机位"的实证**。
- 同机位对照：**有**。跨构建五帧 sha256 前 16 位全同（pov-t20000 250de7df、pov-t40000 8e62c339、upward-pov-t20000 3ff04606、upward-pov-t39999 83b34844 在 b4a2355e 与 1c3580e2 两目录逐字节相同）；跨帧 t20000↔t40000 同一 demo 同一条眼高规则（两次都 offsetZ=75.0001）。
- 判定：**合格**（两列唯一同格合格项）。

### 3.2 cp_process_f12

**STV**：E1 tick 30000。"map: material path=" 18 条；staticProps 1353/212/1141/212/852；sky-upload side=**1024**；title BSP 71,999 tris。具名：320 models/props_gameplay/cap_point_base.mdl skin=2 origin=-3540,-1280,576；323 skin=2 -1664,-2176,768；328 skin=1 1664,2176,768（parent 均 2097151）。N07 静态普查（N07-REPORT.md §1 表 :14）：119 材质 / 75 VMT 已解析 / 2 VMT 缺失 / 42 无基础纹理所（另 0 VTF 缺失）。同机位对照：**无**。缺口+下一步：同 demo 同 tick 30000 抓 --no-world-sky 一张，diff 与 N10 判据一（860,784 像素中 sky_off clear 29,054=3.38%）同法统计；期望 non-sky 区逐字节相同、sky 区差异全在 CLEAR 掩码内。
**POV**：**零帧**。demo 在盘 100,302,450 B，--scan 实测 ticks=276,794（本轮 scan-pov.txt）。下一步：--capture-tick 60000 抓一张（远小于 last），期望 trace 出现 "scene: eye … offsetZ=75.0001"（或 68.0001 下蹲）与 "scene: camera … src=demo"；若显示的是 src=follow 则说明该 demo 无录制相机。
判定：**部分（物件✔ 对照✘ POV 缺）**。

### 3.3 cp_gullywash_f9

**STV**：E1 tick 30000；18 条材质路径；staticProps 1257/139/1118/139/1618；sky-upload side=512；title 107,623 tris。具名：273 skin=1 -3766,758,158.84；276 skin=2 3766,-758,158.25；291 skin=2 1041,-1612,161.25（models/props_gameplay/cap_point_base）。N07：122 材质 / 50 可渲染 / 1 missing VMT / 72 恒不可达。同机位对照：**无**。
**POV**：**零帧**；demo 63,296,494 B，--scan ticks=180,184（本轮）。下一步同 3.2，tick 60000。
判定：**部分**。

### 3.4 cp_sultry_b8a

**STV**：E1 tick 30000；18 条材质路径；staticProps 1866/410/1456/410/3800（七图 instances 最多，eyeCluster 3800）；sky-upload side=512；title 68,966 tris。具名：91 models/props_gameplay/cap_point_base skin=2 -2536,796,704.25；127 **models/props_doomsday/cap_point_small.mdl** skin=1 2655.44,-3919.81,657.22 parent=200832（非 world）；144 同模型 skin=2 -2719.44,3892.16,657.22 parent=452753。→ 本图控制点**跨两组模型**，且两台 parent 指向非 world 实体，是"具名物件"里最能证伪"按 BSP 静态位置贴图"的一格。N07：263 材质 / 120 可渲染 / 10 missing VMT / 134 恒不可达。同机位对照：**无**。
**POV**：**零帧**；demo 56,193,150 B，--scan ticks=185,879（本轮）。下一步 tick 60000。
判定：**部分**。

### 3.5 cp_sunshine

**STV**：E1 tick 30000；18 条材质路径；staticProps 1617/372/1245/372/355；sky-upload side=512；title 82,859 tris。具名：213 skin=0 -5632,5248,64.22；216 skin=2 -7704,6824,128；219 skin=1 -3560,3672,128（models/props_gameplay/cap_point_base）。N07：170 材质 / 108 可渲染 / 2 missing VMT / 61 恒不可达；N09 说 func_breakable 只本图有 2 个（N08 :252）。**注意**：本图 STV 帧的 run.log 背书有瑕疵（G2 run.log 写 "SKIP cp_sunshine missing demo"，而 ui.ini 的 recentDemo0=STV/SUNSHINE.dem 在盘），该帧必须在文件里写明"不由 G2 run.log 背书、由 G2 world-frames 目录与 mtime 单独证明"（§7-①）。同机位对照：**无**。
**POV**：**零帧**；demo 75,905,225 B，--scan ticks=221,570（本轮）。下一步 tick 60000。
判定：**部分**。

### 3.6 koth_bagel_rc11

**STV**：E1 tick 30000；18 条材质路径；staticProps 1562/602/960/602/836；sky-upload side=512；title 97,769 tris。具名：675 models/props_gameplay/cap_point_base skin=0 0,0,455；676 skin=0 0,0,273 parent=811680（parentname=towermove，即控制点在一个可动的逻辑父实体下）。N07：99 材质 / 78 可渲染 / **0 missing VMT** / 22 恒不可达（七图里唯一无缺材质的）。同机位对照：**无**。
**POV**：**零帧**；POV 目录只有 testdemo111.dem **5,497,243 B**，--scan ticks=25,515（本轮）。→ 这是七图 POV 里唯一明显偏短的录制（其余 56–147 MB），「它是不是一场有效比赛」NOT VERIFIED。下一步：tick 20000（上限 25515），期望同上；若 trace 无 eye 行，则该 POV demo 不能用于本格，须记 SUPPORTED-BY-NOTHING。
判定：**部分**（POV 格另加"录制疑似残片"风险）。

### 3.7 koth_product_final

**STV**：E1 tick 30000；18 条材质路径；staticProps 942/305/637/305/757；sky-upload side=**1024**；title 55,878 tris（七图最少）。具名：42 models/props_gameplay/cap_point_base skin=0 -1536,0,224（全图唯一控制点）。N07：67 材质 / 56 可渲染 / 1 missing VMT（DEV/REFLECTIVITY_90，304 tris）/ 10 恒不可达。同机位对照：**无**。
**POV**：**零帧**；demo 100,388,768 B，--scan ticks=227,790（本轮）。**索引旁证**：这份 POV demo 实际位于 …/koth_product_final/POV/，而 N08 §6.3 的 DEMO-INDEX 把它记在 pl_upward_f12 下并标"不存在"——文件在、位置错（§7-⑧）。下一步 tick 60000。
判定：**部分**。

## 4. 两个像素缺陷：现象已确证，机制未定

**身份（三视图同一次抓帧）**：D:/_shared/EXEC-2026-11-13/F1/black/out3/face.bmp、albedo.bmp、texel.bmp，各 2,582,406 B、1264×681、24bpp、行跨距 3792；参数 --debug-camera -600 -600 950 20 45 --capture-tick 0 --start-paused；构建 exe 00ec8b99、sourceTree f533293（docs/PHASE-F-REPORT-2026-11-13.md §12）。trace 文件 D:/_shared/EXEC-2026-11-13/F1/black/out3/face.trace。

**缺陷一：点 (919,379)**（face.trace:47854.9）
- 现象：三视图该点为同一 RGB (210,208,197)（5×5 邻域 15 色，画面亮白），而几何/纹理读数指向暗铁（METAL/METALGRATE013A2）。
- trace：point=919,379 tri=19539 texel=82,81,79 texelA=255 depth=0.9927 frameDepth=0.9931；uv 0.54595,0.53106 → 源 mip0 (139,135) 像素 (0,0,0,0)；全纹理精确匹配 0 次。
- 已否证（卡B，alpha 假设）：该 VTF 全 6 级 mip 的 max RGB ≤ 111（111/107/107/90/107/107），$translucent 1、$alphatest 被注释、无 $basetexturetransform → 该材质**不可能**产出 (210,208,197)。uff08N07 D07 卡B）
- 状态：**现象已确证、机制未定**（是哪个 pass、哪条命令画出这个亮像素，没有证据）。

**缺陷二：点 (849,396)**（face.trace:47850.8）
- trace：point=849,396 tri=60275 material=METAL/METALGRATE013A2 texel=84,83,81 texelA=255 blend=none albedo=84,73,54 expected=84,73,54 reason=tiled depth=0.9937 frameDepth=0.9943。
- 实测三视图：albedo (0,0,0)、face (0,0,0)、texel (84,83,81) → trace 说"写进 albedo 的是 84,73,54"，像素说 albedo 是 0；且 face 也是 0（黑），与 trace 的 texel 不符。
- 已否证（卡C，tiled colour 假设）：shaders.cpp:234 albedo=lerp(colour.rgb, sampled*colour.rgb, colour.a)、:277 分支；colour=(1,1,1,1) ⇒ albedo==sampled；实测 albedo(0,0,0) ≠ texel(84,83,81)；世界 pass 所有上传 colour.rgb 非 0（world.cpp:109-111 / :124-126 / :132-134 / :140-144 / :883-885 / :661-665）；BSP v20 无 vertex colour lump。
- 状态：**现象已确证、机制未定**。

**未定的三件事（不许写"应该"）**
1. 最终画出这两点的是哪个 pass / 哪条三角形：没有逐 pass 覆盖证据。
2. atlas tile 0 装的是哪级 mip / 哪个材质：trace 的 texel 读自 atlas rect=2057,2057（atlas 4112×4112、源 size 256×256），与源 mip0 不符、与 mip1 差 6~8。
3. 849 的 albedo=0 从哪来（清屏残留 vs 其他 pass 覆盖）；以及三视图是否同 tick（alb 与 tex 全等 95.9%、face 与 albedo 全等 18.1%、三视图全等 16.3%——比例本身说明至少一部分像素不同帧/不同 pass，机制未定）。
4. 不能用"旧代码"解释：git diff f533293..HEAD 对 native_renderer_world.cpp / shaders.cpp / frame.cpp 为空（N07 已核）。

**矩阵里的落点**：本文件**不**把这两点算作已解释缺陷；只作为"现象已确证"引用（D01/PHASE-F 的原始记录见 docs/PHASE-F-REPORT-2026-11-13.md §12 与 D:/_shared/EXEC-2026-11-13/F1/black/out3/）。

---

## 5. N10 天空四判词在矩阵里的落点

判据一（遮罩/边界，**可判项 PASS**）：860,784 个天空像素；--no-world-sky 时 sky_off 为 CLEAR 的像素 29,054 = 3.38%；两图差异 29,054 = 3.375%；差异像素里 sky_off 侧是 CLEAR 的占 100.00%；只在 sky_off 有、开图漏掉的像素 0。faces.py（容差 10）三面主色与 trace 一致：up 面 11,694 像素（40.2%，std 2.96）、side 面 13,032（44.9%，std 4.97）、dn 面 210（0.7%）。→ 天空确实只画在遮罩区内、且开图后遮罩区全覆盖。

判据二（六面接缝）：**NOT VERIFIED**。N10 没有在可判帧上量接缝（六面各自重采样到立方体面，接缝处是否有 1px 缝/重复列未测）。

判据三（Fallback）：**NOT VERIFIED**。七图六面 VMT 全可解析（N07 天空材质逐面 18 条 path 全部可读），要测 Fallback 必须人工删掉某一面 VMT 再跑——没有做过。

判据四（对照/归一化，**NOT VERIFIED + 有污染**）：3D skybox detail 非天空面计数 upward 690 / sunshine 497 / gullywash 324 / bagel 72 / sultry 30 / process 22 / product 14，PVS 全部不可见；归一化污染 upward span 12288 vs 7168 = 1.71×、水平缩水 −71.4%，process −97.3% → 用"天空像素占比"横向比较七图会被 3D skybox detail 面污染，**不许**拿它做横向指标。

sky_camera：七图各 1 个 sky_camera（scale 16），但产品**不消费**它——native_renderer_frame.cpp:303-305 把平移清零、world_material.h:572 halfExtent=40。→ **sky_camera UNSUPPORTED**，3D skybox **未实现**（D02）。

**本矩阵用法**：STV 列每图的"天空上传"只给 side（512/1024）与 36 顶点/12 三角的实测行；**不得**把 N10 判词写成"七图天空 PASS"（N10 只测了 pl_upward_f12 的 tick 40000 一帧，且该帧与 N12 修后 stv-t40000 逐字节相同，见 §7-⑥）。

---

## 6. N08 / N09 在矩阵里的落点

- **N08 = 部分**：七图控制点几何/材质/skin 静态成立（本文 §2.1、§3 的 models/props_gameplay/cap_point_base / cap_point_small / track_midcap 都是 N08 §2.1 的被测对象）；但 entity I/O 不可用、trigger_capture_area 永不触发、小地图 CP marker 是编译期常量 → "控制点**逻辑**"未验证，"控制点**外观**"已验证。
- **N09 = 静态成立 + 推车未验证 + 平台/可破坏物不适用**：静态几何/PVS 成立；推车（bomb_cart / 移动 payload）"跟时间移动"未验证（产品无 path_track 插值）；28 格全 0 只指 func_plat / func_breakable_surf / func_water_analog / func_trackautochange 四类，**func_breakable 只 cp_sunshine 有 2 个**（N08–N10 报告 :252-254）→ 若读成"可破坏物全 0"就漏了 sunshine。
- 落到矩阵：upward 的 POV 帧里 entity=320 bomb_cart.mdl origin=-1653.97,-1536,30.94 是**静态+单帧**证据；N09 的"随时间移动"在矩阵里仍是 **NOT VERIFIED**（没有 tick 序列）。

---

## 7. 我复核出的一致性与矛盾（逐条）

| # | 检查项 | 结论 | 依据 |
|---|---|---|---|
| ① | G2 §7 把七图当一轮 | **矛盾**：run.log 只有 6 条 WORLD + "SKIP cp_sunshine missing demo"，而 cp_sunshine.bmp mtime 12:45:05 晚于 run.log 12:42:30；sunshine 的 ui.ini recentDemo0=STV/SUNSHINE.dem 在盘（82,759,239 B） | run.log 11 行（590 B）本轮 cat；mtime ls；ui.ini grep |
| ② | 六图 STV 有无同机位对照 | **无**：G2 trace 里 world face-trace 命中 0，"map: material path=" 只 18 条/帧，无逐物件屏幕矩形 | 本轮 grep G2 七图 trace |
| ③ | G2 identity 完整度 | **不足**：只记 exe sha/head/tick，未记 exe 字节；字节只能从 D08 借 2,988,544（同 sha cfcdbe5d） | identity.txt vs D08 §表 |
| ④ | STV 帧的"相机真值" | **性质受限**：七帧相机行全 src=follow（跟随目标推导），而 N12 POV 帧是 src=demo → STV 侧无录制相机真值（逐帧证据，非断言） | G2 trace vs pov-t20000.trace |
| ⑤ | "N12 §2.3 五帧同、只有 stv-t40000 变" | **逐字成立**，但两目录身份不同（b4a2355e 2,991,104 B / 1c3580e2 2,991,616 B），引用必须带目录名 | 本轮逐个 sha256 |
| ⑥ | N10 sky_on 与 N12 修后 stv-t40000 | **逐字节相同**（450806898cfdacac…）→ N10 的遮罩 PASS 实质是"一张 tick 40000 的 upward STV 帧" | sha256sum 实测 |
| ⑦ | 同一 exe 两个 HEAD | **身份冲突**：1c3580e2 在 n10-capture/identity.txt 记 head=6dd853e，在 capture-1c3580e2/identity.txt 记 head=83657f10 | 两 identity.txt |
| ⑧ | "七图 POV 齐全"（索引口径）vs DEMO-INDEX | **索引缺陷形态是地图目录串位**：product 的 POV demo 在 koth_product_final/POV/（100,388,768 B），N08 §6.3 记它在 pl_upward_f12 下"不存在"；bagel POV 仅 5,497,243 B（testdemo111.dem），是否有效录制 NOT VERIFIED | ls + N08 §6.3 |
| ⑨ | 两份"七图小地图"证据 | **不可合并**：N14 用 --debug-camera 0 -2048 512 0 33 + b4a2355e；D08 用每图自己的 debug 相机 + cfcdbe5d → exe/相机/tick 三项都不同 | N14 §4 / D08 §表 |
| ⑩ | N08 §2.1 的 sultry 控制点模型名 | **精度不足**：N08 写 cap_point_small.mdl，G2 trace 实路径 models/props_doomsday/cap_point_small.mdl | G2 sultry trace |
| ⑪ | "七图 28 格全 0" | **口径易误读**：只指 func_plat / func_breakable_surf / func_water_analog / func_trackautochange；func_breakable 只 sunshine 2 个（N08–N10 报告 :252-254） | N08 :252-254 |

另有一致性正向结论：G2 七图 title 行 BSP 三角数（71,999 / 107,623 / 68,966 / 82,859 / 97,769 / 55,878 / 66,707）与 N07 报告 §1 表（`D:/_shared/EXEC-2026-11-15/N07/N07-REPORT.md`:12-20）的三角数逐图完全一致（七图七个值全对，无一处差）→ 产品运行时的 BSP 三角数被静态普查独立复现。staticProps instances（1,353 / 1,257 / 1,866 / 1,617 / 1,562 / 942 / 1,107）是运行时逐帧计数，N07 表没有同名列，二者**不做数值对比**。

---

## 8. 本文件没有证明什么

1. **没有证明**六图 STV 帧的像素与 BSP 对应：无同机位对照、无 face-trace、无逐物件屏幕矩形（只有单帧 + 相机 + PVS 计数）。
2. **没有证明**任何一格 POV 的像素内容：六图零帧；upward 三帧证明的是相机眼高、跨构建一致性与 demo 相机来源，不是画面正确性。
3. **没有证明** 919,379 / 849,396 的机制：现象已确证、机制未定（alpha 与 tiled colour 两条假设均被实测否证）。
4. **没有证明** N10 的天空接缝 / Fallback / sky_camera / 3D skybox 任何一项可用：接缝与 Fallback NOT VERIFIED，sky_camera UNSUPPORTED，3D skybox 未实现。
5. **没有证明** N08 控制点的运行时行为：entity I/O 不存在、trigger_capture_area 永不触发。
6. **没有证明** N09 推车"随时间移动"：只有单帧静态位置，无 tick 序列。
7. **没有证明** STV 相机等于游戏真实导播相机：产品对 STV demo 走 src=follow 推导，没有那台相机的输入。
8. **没有证明**七图能在"同一个 exe"上同时成立：证据横跨 4 个 exe（cfcdbe5d / b4a2355e / 1c3580e2 / 00ec8b99）。
9. **没有证明**小地图/DPI 侧（N14 / D08 / N15 是不同 exe/相机/tick 的三份证据），本文只引用它们的"面板"结论，不外推到世界。
10. **没有证明** bagel 的 POV 录制有效（5.5 MB testdemo111.dem）。
11. **没有证明**任何"面级覆盖率"（几何像素匹配率）：N13 的 99.7%/98.9% 是"像素非黑比例"，不是与 BSP 几何对位。
12. 本卡**只读**：没有构建、没有运行产品 exe、没有新抓任何一帧，本文全部读数是已有产物的复算与引用。

---

## 附录 A：本文引用的证据 sha 清单

| 证据 | 文件 | sha256 |
|---|---|---|
| N10 天空开 | n10-capture/sky_on.bmp | 450806898cfdacac…（与 N12 修后 stv-t40000 逐字节相同） |
| N10 天空关 | n10-capture/sky_off.bmp | dacf9488d364e514… |
| N12 修后 stv-t40000 | capture-1c3580e2/stv-t40000/stv-t40000.bmp | 450806898cfdacac… |
| N12 修前 stv-t40000 | capture-b4a2355e/stv-t40000/… | 9168fefdc7c2229f… |
| N12 stv-t20000（两构建同） | capture-*/stv-t20000/… | 1e796ab076838274… |
| N12 pov-t20000（两构建同） | capture-*/pov-t20000/… | 250de7df310a2a05… |
| N12 pov-t40000（两构建同） | capture-*/pov-t40000/… | 8e62c339b7cfa13e… |
| N12 upward-pov-t20000（两构建同） | capture-*/upward-pov-t20000/… | 3ff046064f8b86f3… |
| N12 upward-pov-t39999（两构建同） | capture-*/upward-pov-t39999/… | 83b348447cd34ef5… |
| N12 修后 exe | native/build-nmake/tf2_demo_native.exe | 1c3580e2…（2,991,616 B） |
| N12 修前 exe | 同上（另一时间点） | b4a2355e…（2,991,104 B） |
| G2 exe | 同上 | cfcdbe5d48c6e71e…（2,988,544 B，字节据 D08） |
| D08 面板对（七图） | G1/minimap-five/*/{on,off}.bmp | 见 §7-⑨；本轮 BMP 逐字节复算 diff=25,600 px、bbox 全同 |

注：sha 标"…"者为前 16 位十六进制（本文写作时的实测前缀）；完整值见各目录 identity.txt 与 N12 §2.3 表。

---

## 附录 B：逐图可执行命令（下一轮补证用）

统一形状（取自 N12 E5 的实际 argv，脚本骨架仿 n10-sky.sh）：

```bash
WT=/d/TF2_Native_Worktrees/t105-e5
EXE=$WT/native/build-nmake/tf2_demo_native.exe
TFROOT="D:/SteamLibrary/steamapps/common/Team Fortress 2/tf"
DEMO="<该图 demo 绝对路径>"
SET="<该图 settings.ini>"   # 各图 STV 的 settings/ui ini 在 G2/world-frames/<map>/ 里；POV 用 G4/camera-matrix 模板
UI="<该图 ui.ini>"
TICK=<见下表>
OUT=D:/_shared/EXEC-2026-11-15/n11/captures/<map>-<col>-<variant>

"$EXE" --tf-root "$TFROOT" --hidden-window --audio-disabled \
  --settings-file "$SET" --ui-settings-file "$UI" --demo "$DEMO" \
  --capture-tick $TICK --playback-speed 8 --capture-frame "$OUT.bmp" \
  --window-size 1280x720 [--world-sky|--no-world-sky] 2> "$OUT.trace"
```

**STV 补证（同机位对照，每图一对）**：同 demo、同 tick 30000、只差 --world-sky/--no-world-sky。期望读数：两图元数据同（1264×681）；trace 出现 "gpu: world sky-upload state=active vertices=36 triangles=12 faces=6 side=<512|1024> resampled=1"，且 staticProps 行与该图 §3 数字**完全相同**（process 1353/212/1141/212/852；gullywash 1257/139/1118/139/1618；sultry 1866/410/1456/410/3800；sunshine 1617/372/1245/372/355；bagel 1562/602/960/602/836；product 942/305/637/305/757）。判据：非天空区逐字节相同、天空区差异 ⊆ CLEAR 掩码（N10 判据一脚本 D:/_shared/EXEC-2026-11-15/n10-capture/diff.py）。

**POV 补证（每图两帧）**：同 demo、tick 见下表，抓一张当前构建；期望 trace 出现 "scene: eye … offsetZ=75.0001"（下蹲帧为 68.0001）与 "scene: camera … src=demo"；若出现 src=follow 则该 demo 无录制相机，须记 SUPPORTED-BY-NOTHING。第二张用另一构建同 tick 复抓，期望 sha256 相同（upward 已证此性质）。

| 图 | POV demo | --scan 实测 ticks（摘要行 / 全量行） | 建议 POV tick | STV tick |
|---|---|---|---|---|
| cp_process_f12 | POV/autorecord_2026-09-03_21-05-39.dem | 276,794 / 276,793 | 60000 | 30000 |
| cp_gullywash_f9 | POV/autorecord_2026-05-21_21-08-36.dem | 180,184 / 180,395 | 60000 | 30000 |
| cp_sultry_b8a | POV/autorecord_2026-06-16_21-42-44.dem | 185,879 / 185,878 | 60000 | 30000 |
| cp_sunshine | POV/autorecord_2026-05-17_16-32-36.dem | 221,570 / 221,569 | 60000 | 30000 |
| koth_bagel_rc11 | POV/testdemo111.dem | 25,515 / 25,514 | 20000 | 30000 |
| koth_product_final | POV/autorecord_2026-09-10_21-01-05.dem | 227,790 / 228,006 | 60000 | 30000 |
| pl_upward_f12 | POV/autorecord_2026-08-08_21-43-59.dem | 305,919 / 306,131（已抓 20000、40000） | —（已抓） | 30000 |

注：两个 tick 值分别来自同一 demo 的扫描摘要行与全量行（计数口径差 ≤216，均远大于建议 tick，不影响选点）；八份 POV demo 全部 source_tv=0，即它们都不带导播相机流。

**扫描命令**（拿 last tick / 判断 demo 是否有录制相机，零渲染成本）：
```bash
D:/TF2_Native_Worktrees/t105-e5/native/build-nmake/demo_open_probe.exe --scan "<dem>"
```
期望：输出 "map=<图> … ticks=<last> … source_tv=0"（source_tv=1 表示该 demo 是导播源）；POV demo 若 "source_tv=1" 则它本身带导播相机流。

---

*本文写作时的仓库状态：HEAD 6dd853e，工作树无改动（本卡只读）。*

