# demo 异常 tick 检测（task-35，2026-11-21）

* 实现：`native/include/demo_tick_audit.h` + `native/src/demo_tick_audit.cpp`（`tf2_core` 静态库成员），入口是 `tf2::native::auditDemoTicks()` 与 `formatDemoTickAuditLines()`。
* 开关：只在命令行显式给 `--tick-audit` 时抓取。产品（`tf2_demo_native`）把每行写进 trace（前缀 `tick-audit: `），探针（`demo_open_probe --scan --tick-audit`）把同样的行写到 stdout。不给开关时 `DemoNetworkSummary::tickAuditCapture` 保持空指针，抓取器不构造、不记录任何包行和 sim 采样。
* 判词分三档：`None / Info / Warning / Edited`（表头里的 `verdict=` 取最严重的一档）。

## 0. 结论

| 项目 | 结果 |
|---|---|
| 构建 | `bash tools/lockrun.sh lead -- bash build-target.sh tf2_demo_native demo_open_probe` → rc=0（/W4 /WX 下零警告） |
| 干净真实 demo（cp_process_f12 pr600） | `verdict=None issues=0`，包序号/时钟偏移/header 全部一致 |
| 干净真实 demo（koth_bagel_rc11） | `verdict=None issues=0` |
| 负控 1：改一个数据包的 demo tick（+200） | `verdict=Edited`，`tick-backwards` 命中（199）+ `clock-jump` 命中（200） |
| 负控 2：改一个数据包的 in/out sequence（-7） | `verdict=Edited`，`in-sequence-backwards` / `out-sequence-backwards` 各 1 次 + `sequence-gaps` Info |
| 既有默认抓帧 | `md5=2ddade356e85dd77b795a7380c254bcb`（与 task-34 的 `after2` 相同）；加 `--tick-audit` 后同 tick、同字节数、同 md5，rc=0 |
| 玩家级计数 | 两条真实 demo 都跑通（attacks=2204/shots=1035、attacks=1784/shots=1118），干净 demo 上 `players=0 hits=0`，没有误报 |

## 1. 检测什么（两层，单遍）

### 1.1 文件层：demo 流自身的一致性

| 检查 | 判据 | 级别 |
|---|---|---|
| demo tick 单调 | 数据包（command 2）的 tick 比上一包小 | Edited（每次回退一条，最多列 8 条，计数照常累加） |
| 包序号单调 | 数据包的 `inSequence`/`outSequence` 比上一包小 | Edited |
| 包序号连续性 | `inSequence`/`outSequence` 步长 > 1 | Info |
| 服务器时钟 vs demo 时钟 | `offset = svc_NetTick 的 server tick - demo tick`，在最近 2000 tick 的滑动窗口里与中位数的偏差 > 66 tick | Warning；≥ 132 tick 或偏差是 66 的 2 倍 → Edited |
| 暖机 | 首包之后再等 200 tick（`warmupTicks`），窗口里样本 < 32 不判 | — |
| header 记账 | header 帧数 vs 实际数据包数、header tick 跨度 vs 首/末数据包 tick、header 秒数 vs `ticks / tickRate` | 不一致记 issue（当前两条真实 demo 均一致） |

阈值来源：`docs/REF-TF2-DEMO-ANALYSER-2026-11-20.md` §4.2 记录的参考实现（GPL-3.0，**只读**，未复制代码）：`CLOCK_SKIP_TICKS=66`、`CLOCK_MEMORY_TICKS=2000`、`WARMUP_TICKS=200`、三档 Info/Warning/Edited。我们自己的实现按本仓库 demo 语料重新量过（两条 demo 偏移恒定，无跳变），没有直接把它的判词搬过来。

### 1.2 玩家层：tick-shift / doubletap

每个包结束时结算上一包的 `CTEPlayerAnimEvent`：`m_hPlayer` 解析成玩家实体，`m_hActiveWeapon` 解析成武器实体，按武器类名查“两发之间最少多少 tick”的表，再看同一帧内的事件数和相邻两发的间隔。

| 规则 | 判据 | 权重 |
|---|---|---|
| 同帧多发 | 同一玩家同一武器在同一 tick 里 ≥ 2 次开火，且 `gap < (shots-1) * floor * 50%` | 2 |
| 相邻间隔过短 | `gap < floor * 80% - tolerance - slack`，其中 `slack = (gap-1) + (prev.gap-1)`，只有阈值 ≥ 6 tick 才算 | 1 |
| 卡顿豁免 | 最近 30 tick 内该玩家 sim time 的最大步长 ≥ max(floor, 4)，或 ≥ 100 | 不计 |
| 切枪豁免 | 与上次开火间隔 ≤ 3 tick 且武器实体变了 | 不计 |
| 转轮 66 tick 窗口 | 窗口内事件 > 30（默认关，`minigunRateEnabled=false`） | 2 |
| 多玩家熔断 | 2000 tick 内被标记玩家 > 12 → 降级为 Info（服务器侧状况，不是十二个作弊者） | — |

武器 floor（两发最小间隔，tick）：CTFScatterGun 37（Force-A-Nature item 45/1078 记 19）、CTFSodaPopper 18、CTFPEPBrawlerBlaster 33、CTFPistol_ScoutPrimary 14、CTFPistol_Scout/CTFPistol 8（不做间隔检查）、CTFShotgun_Soldier/Pyro/CTFShotgun/_Revenge 36、CTFShotgun_HWG 31、CTFMinigun 只走 66 tick 窗口。没有 floor 的武器（火箭、狙击、火焰、医疗枪、近战等）不判 doubletap，计入 `unresolved` —— 这不是解码失败，是本表不覆盖的武器类型（见 §4）。

## 2. 证据

### 2.1 干净 demo（pr600，SourceTV，`--scan --tick-audit`）

```
tick-audit: file packets=120920 data=120917 signon=3 span=1..120936
tick-audit: seq in-backwards=0 in-gaps=0 out-backwards=0 out-gaps=0 tick-backwards=0 max-tick-backwards=0
tick-audit: clock samples=120917 offset-first=48844 offset-last=48844 offset-min=48844 offset-max=48844 jumps=0 max-jump=0 max-gap=4
tick-audit: header detail=frames=120917 data=120917 span=120936 playback=1814.040 expected=1814.040 mismatches=0
tick-audit: player attacks=2204 shots=1035 unresolved=636 same-frame=0 short-interval=0 lag-excused=0 switch-guarded=0 minigun=0 sim-samples=1950739
tick-audit: players=0 hits=0 fuse=clear
tick-audit: verdict=None issues=0 dropped=0
```

### 2.2 干净 demo（koth_bagel_rc11）

```
tick-audit: file packets=100614 data=100611 signon=3 span=1..100632
tick-audit: seq in-backwards=0 in-gaps=0 out-backwards=0 out-gaps=0 tick-backwards=0 max-tick-backwards=0
tick-audit: clock samples=100611 offset-first=26667 offset-last=26667 offset-min=26667 offset-max=26667 jumps=0 max-jump=0 max-gap=5
tick-audit: header detail=frames=100611 data=100611 span=100632 playback=1509.480 expected=1509.480 mismatches=0
tick-audit: player attacks=1784 shots=1118 unresolved=666 same-frame=0 short-interval=0 lag-excused=0 switch-guarded=0 minigun=0 sim-samples=1170228
tick-audit: players=0 hits=0 fuse=clear
tick-audit: verdict=None issues=0 dropped=0
```

### 2.3 负控 1：把一个数据包的 demo tick +200

做法：`.scratch/n35-tamper/tamper.py`（一次性工具，不提交）按 demo 协议解析命令行（该 demo `demoProtocol=3`，IP 参数只有 command + int32 tick，没有 player slot；76 字节包信息后是 int32 inSequence、int32 outSequence、int32 length），把中间第 40305 个数据包（文件偏移 30078538，tick 40309）的 tick 写成 40509。

```
tick-audit: seq in-backwards=0 in-gaps=0 out-backwards=0 out-gaps=0 tick-backwards=1 max-tick-backwards=199
tick-audit: clock samples=120917 offset-first=48844 offset-last=48844 offset-min=48644 offset-max=48844 jumps=1 max-jump=200 max-gap=4
tick-audit: players=0 hits=0 fuse=clear
tick-audit: verdict=Edited issues=2 dropped=0
tick-audit: issue level=Edited code=tick-backwards tick=40310 server=-1 packet=40309 detail=demo tick went backwards by 199
tick-audit: issue level=Edited code=clock-jump tick=40509 server=89153 packet=40308 detail=server clock offset moved by 200 ticks (offset=48644 median=48844)
```

### 2.4 负控 2：把同一个包的 in/out sequence -7

```
tick-audit: seq in-backwards=1 in-gaps=1 out-backwards=1 out-gaps=1 tick-backwards=0 max-tick-backwards=0
tick-audit: clock samples=120917 offset-first=48844 offset-last=48844 offset-min=48844 offset-max=48844 jumps=0 max-jump=0 max-gap=4
tick-audit: players=0 hits=0 fuse=clear
tick-audit: verdict=Edited issues=3 dropped=0
tick-audit: issue level=Edited code=in-sequence-backwards tick=40309 server=-1 packet=40308 detail=in sequence went backwards 1 times
tick-audit: issue level=Edited code=out-sequence-backwards tick=40309 server=-1 packet=40308 detail=out sequence went backwards 1 times
tick-audit: issue level=Info code=sequence-gaps tick=40310 server=-1 packet=40309 detail=in gaps=1 out gaps=1
```

这里的 `packet=` 是 signon+data 包里的 0 基序号，`tick=` 是该包的 demo tick；时钟跳变行的 `server=` 是该包生效的 `svc_NetTick`。

## 3. 产品默认路径不受影响

同一份 build 上跑 `.scratch/n34-alpha/cap.sh`（cp_process_f12、capture tick 600、默认参数、`--hidden-window`）：

| 调用 | rc | 抓帧 | md5 |
|---|---|---|---|
| 默认（不带新开关） | 0 | 2,582,406 B | `2ddade356e85dd77b795a7380c254bcb` |
| 加 `--tick-audit` | 0 | 2,582,406 B | `2ddade356e85dd77b795a7380c254bcb` |

默认 trace 里 `tick-audit:` 行数 = 0；带开关时 = 7 行。也就是说抓帧路径、退出码、默认 trace 都没有新行为，只有显式打开开关才多出诊断行。抓帧 md5 与 task-34 的 `after2` 基线一致（task-34 把 VMT 的 `$alphatestreference` 接进默认路径，缺省基线因此从旧硬编码 0.5 的 `d9544cd67773552f0b948a98bf47458c` 变成 `2ddade35...`；这里的比较对象是当前的默认基线，不是更早的 0.5 基线）。

## 4. 已知限制

1. 玩家层只覆盖有 floor 的 hitscan 武器（§1.2 表）；其余武器的开火事件计入 `unresolved`，所以这条计数（pr600 636/1035、bagel 666/1118）不能读成“解码失败率”，它是“本表不判的武器占比”。解除限制要先补 floor，而 floor 必须来自可查证的来源，不能凭感觉填。
2. 玩家层的 tick 模型依赖 `m_flSimulationTime` 的 8 位网络值还原（base + raw + ±127 回绕 + 每 100 tick 前进一格的规则）；2000 tick 窗口内足够用，但跨过 100 tick 的 base 翻转时仍然依赖“上一包在 force 里的 net_Tick”，录到“服务器 tick 不变而 tick 值回绕”的包会由 `clock-jump` 兜住，不会静默。
3. 负控是“改文件里的字段”，不是“重新编码一整段 demo”；它能证明检测器对这三类字段的改动敏感，但不能证明对任意剪辑/重排都敏感（那需要生成器，见下）。
4. 没有对 57 份语料做回归（本机只有 4 份 demo）：`in-gaps/out-gaps` 在两条真实源上都为 0，但更多 demo（尤其包含 `dem_synctick` 或丢包的源）可能出现 Info 级 gap；它们不能单独把判词抬到 Edited，这也正是分档的意义。

## 5. 参考实现与许可（只读记录）

* 读过的外部包：`C:\Users\Administrator\Downloads\TF2-Demo-Analyser-build-4\`（本地 build 4 源码 + 冻结 exe）。许可证 GPL-3.0。
* 本地 build 4 **没有**任何 tick/篡改检测（exe 里连 `anomal` 都没有；`on_demo_bytes` 钩子是空实现）。build 7+ 上游才加入 `src/util/integrity.rs`；本项目文档 `docs/REF-TF2-DEMO-ANALYSER-2026-11-20.md` §4.2 已逐条记录它的量测项、阈值（66/2000/200/1.0 s/三档）和 `forgeries` 自测思路。
* 本次实现：数据通路（`DemoIndex` 记 `inSequence/outSequence`、扫描时在每个包前后打点、`m_flSimulationTime` 采样、`CTEPlayerAnimEvent` 采样）和判据代码全部为本仓库新写；没有复制该项目的 Rust 代码，也没有链接它的 exe。玩家层 floor 表来自公开的服务器 tick 行为（`docs/REF-TF2-DEMO-ANALYSER-2026-11-20.md` 记录的双 tap 规格），代码里每个常数都写了出处/理由。
* 复跑命令：
  ```
  bash tools/lockrun.sh lead -- bash build-target.sh tf2_demo_native demo_open_probe
  native/build-nmake/demo_open_probe.exe --scan --tick-audit <demo.dem>
  # 产品路径：TF2_NATIVE_TRACE=<file> native/build-nmake/tf2_demo_native.exe --tf-root <tf> --demo <demo.dem> --tick-audit ...
  ```

## 6. 文件与数据

* 新增：`native/include/demo_tick_audit.h`、`native/src/demo_tick_audit.cpp`。
* 改动：`native/include/demo_header.h`（包序号数组 + summary 上的 capture 指针）、`native/src/demo_header_core.cpp`（两个索引路径都记录 in/out sequence）、`native/src/demo_header.cpp`（每包 begin/end）、`native/src/demo_header_entity.cpp`（sim time 采样）、`native/src/demo_header_events.cpp`（temp 事件采样）、`native/src/main.cpp`（`--tick-audit` 与 trace 输出）、`native/tools/demo_open_probe.cpp`（探针开关）、`native/cmake/tf2-libraries.cmake`（源文件入列）。
* demo：`E:\demo测试\cp_process_f12\STV\1338f5de79ea343682c17dd514c980d9_matcha-20260927-1312-cp_process_f12.dem`（90,995,077 B）；`D:\TF2_Demo_Player\testdata\demos\dfbce608c80a4d72a390c70c597f0377_match-20261101-1959-koth_bagel_rc11.dem`。
* 负控副本：`.scratch/n35-tamper/tick.dem`、`.scratch/n35-tamper/seq.dem`（都在 `.scratch` 下，未提交）；生成脚本同目录 `tamper.py`。
