# N25 — 提速安全矩阵：B 列门禁逐门判定（arch-ci, 2026-11-14）

> 作者：执行卡 E5（替换实例）。写盘范围：`docs/arch-ci-2026-11-14/N25-GATE-SPEED-MATRIX.md` 一个文件。
> 不提交：本卡不产生 git commit，改动留在工作树。
> 状态：判定矩阵。本文件不执行任何提速复测；§6 是留给下一个窗口的测量计划。

## 0. 一句话结论

能不能提速不是墙钟问题，而是「这条门禁断言的是不是 capture 帧上的状态」：生产者把 capture 帧的 tick 钳到 `--capture-tick` 请求值（`native/src/main.cpp:10019-10033`），所以同一 tick 的像素与状态字段与 `--playback-speed` 无关；被速度改变的是「到达该 tick 之前经过了多少绘制帧、多少事件」。
按可核实的启动行重建，本表覆盖 43 个门禁：16 个只读状态量、可直接 8x；7 个有一处计数读数待核、核完可并入 8x；11 个含帧计数/边沿/墙钟读数、必须留 1x；9 个的启动点就在 tick 0 或 `--start-paused`、提速收益为 0。16+7+11+9=43。

> 名单口径：派工给的是「B 列 40 个」。逐个核对启动行后得到 43 个文件，多出的三个是 `timeline-check.sh`、`hud-live-check.sh`、`frame-capture-check.sh`——它们缺速发生在不旅行 tick 的启动点上（`--start-paused` 或 `--capture-tick 0`），属「缺速但无害」。9+40+3=52 与 63 之间差的 11 个文件，本卡只确认了「提及 exe 但不启动产品+demo」的存在（`install-lifecycle-check.sh` 复制夹具、`build-identity-check.sh:313` 只取 `--version`、`stability-release-check.sh:327-331` 只 stat/sha、`resource-reachability-check.sh:33` 的 EXE 是 probe），没有逐个定位那 11 个文件。

## 1. 判定框架

### 1.1 capture 帧的 tick 由生产者钳死

| 代号 | 位置 | 事实 |
| --- | --- | --- |
| P1 | `native/src/main.cpp:10015-10017` | 每帧 `const std::int32_t advanced = g_playbackControl.advance(elapsed); if (advanced != 0) { g_playback.tick += advanced; }` |
| P2 | `native/src/main.cpp:10019-10033` | 截图待发且 `g_playback.tick > captureTick` 时把 tick 写回 `captureTick`，并 `noteExternalCursor` + `resetAccumulator`；注释写明「`--capture-tick N` 是『在 tick N 绘制的那一帧』，不是『第一个不早于 N 的帧』」 |
| P3 | `native/src/main.cpp:12716-12719` | 相机脚本每个绘制帧走一步（`elapsed >= kTargetFrameSeconds` 才走） |
| P4 | `native/src/main.cpp:12721` | 每个绘制帧打印 draw begin，带 `tick=` |
| P5 | `native/src/main.cpp:12742-12755` | arm 后第一个 `tick >= captureTick` 的绘制帧请求截图，打印 `capture: armed requested=%d frame_tick=%d frames=%llu`；旁边注释 T26 记录「跳过 tick 的 run 会截到更晚的相机（world-ids 跑出 1999 后 2002，与矩阵差约 15k 像素）」 |
| P6 | `native/src/main.cpp:11202` | animdsum 与状态名（`state=`）在这一行附近产生 |
| P7 | `native/src/main.cpp:11208-11220` | `flinchStarts` 由「上一帧在 flinch、本帧不在」的下降沿累加 |
| P8 | `native/src/main.cpp:5977-5981` | `std::clamp(playbackSpeed, 0.125, 64.0)`，提速上限 64x |
| P9 | `native/src/main.cpp:1959` | 没有 `--playback-speed` 时 `playbackSpeed = 0.0`，跳过 P8 的钳位，速度保持 1.0（`native/include/playback_state.h:25`） |
| P10 | `native/src/main.cpp:9982` | 每帧 `elapsed` 被钳到 0..0.25 s；1x 下每帧最多 16.67 tick（66.6667 tick/s × 0.25） |

由 P1+P2+P5 得矩阵的核心不变量：**同一 `captureTick` 的 capture 帧，其像素与帧上状态与速度无关**。8x 只改变到达该帧之前经过了多少帧（P3、P4 的行数），以及在整段 run 内被累加/取峰/取边沿了多少次（P6、P7）。

### 1.2 三类读数

| 类型 | 定义 | 提速后的变化 | 本表出现的 tag 形状 | 取证 |
| --- | --- | --- | --- | --- |
| 状态量 | capture 帧上的像素或字段 | 不变 | `draw: begin` 的 `tick=`、`gpu: world`、`scene: *`、`capture: armed` 的 `frame_tick=`、`mode: window_created` | P2/P4/P5 |
| 帧计数 | 整段 run 的累计、峰值、次数 | 变小或改变（帧少了） | `drawn=`、`frames=`、`events_seen=`、`calls=`、`updates=`、`picks=`、`dsum`、`captures=` | P1/P3 |
| 边沿 | 需要相邻两帧的状态转移 | 8x 下可能整段漏掉 | `flinchStarts=` | P6/P7 |
| 墙钟 | 依赖 sleep、采样超时、外部进程时序 | 直接改变 | 各门禁的 `TIMEOUT=`、按键脚本的 `--wait` 间隔 | 各门禁自身配置行 |

### 1.3 判据

一条门禁可提速，当且仅当：每条断言只读状态量，或断言的不变量与计数无关（「出现即通过」、`>=1`、`!=0`）。只要有一条断言把帧计数/边沿写成**数值期望**，该门禁留 1x；若它能改成「capture 帧不含/包含某串」，则先改断言再提速。

## 2. 矩阵（43 行）

速度列的口径：`8x` = 只差 `--playback-speed`，读数全部沿量；`8x*` = 沿量但有一处读数未核，核完才可落；`1x` = 含帧计数/边沿/墙钟；`保持缺省` = 启动点不旅行 tick，提速无收益（不是「不安全」）。验收方式一列统一写「双跑全等」，其定义见 §4。

| 门禁（脚本） | 状态量读数（提速安全） | 帧计数/边沿读数（提速改变观察对象） | 建议速度 | 验收方式 |
| --- | --- | --- | --- | --- |
| `camera-ui-check.sh` | `camera: snapshot` :967-969、`camera: expect` :963-996/:1390、`camera: verify` :1006/:1420、`camera: bookmark` :1022、`camera: capture` :1100-1101、`mode: summary` :1112 是对 capture 帧的逐字段期望（P2 保证 tick 精确） | `camera: accept` :951-960 的 `steps=`/`failures=`（P3 每绘制帧走一步，8x 到同 tick 的步数变少 → 相机轨迹与 1x 不同）；`camera: director` :1055-1391 同为逐帧驱动 | 1x | 保持 1x；若要提速，先把 `camera: accept` 的步数期望改成与帧数无关（按 tick 记步） |
| `lighting-semantics-check.sh` | `draw: begin` :142 的 `tick=`、`gpu: world` :158-163/:276；启动行 :132-133 给 `--capture-tick 2000` | 未普查（未见对 `drawn=`/`frames=` 总数的断言） | 8x | 双跑全等：帧 md5 相同 + `gpu: world` 全部字段相同 |
| `lighting-dark-account-check.sh` | `draw: begin` :138 的 `tick=`、`scene: camera` :208 | 未普查 | 8x | 双跑全等 |
| `quality-audit-check.sh` | `draw: begin` :137、`gpu: world` :152-154 | 未普查 | 8x | 双跑全等 |
| `review-workspace-check.sh` | `review-workspace: step/expect/field` :85-175、`summary: no` :175、`summary: add` :188-190（到 tick 时的面板文本） | 未普查（`summary:` 的计数来源未核） | 8x* | 双跑全等：`summary:` 行逐字节相同；若含次数则降为 1x |
| `visual-baseline-check.sh` | `draw: begin` :174、`gpu: world` :273-306 的基线比较 | `gpu: world` 基线里是否含逐帧累计计数未逐字段核；本门禁是 P0 基线 | 1x | 保持 1x；8x 只在双跑全等后才可切换 |
| `world-material-check.sh` | 像素 diff（`--capture-tick 0`，启动行 :380-382）、`gpu: world` :385/:417/:438、`renderer: split` :734 | 无（tick 0 不旅行，P2/P3 不介入） | 保持缺省 | 不需提速；加 `--playback-speed` 也只是 no-op，若加需双跑确认 |
| `world-material-effects-check.sh` | `gpu: world` :234/:408、`gpu: viewmodel` :303-305（启动行 :193-195 / :480，tick 0） | 无（同上） | 保持缺省 | 同上 |
| `world-material-face-check.sh` | `draw: begin` :121、`gpu: world` :189-213、`maptex: fallback` :272 | 未普查 | 8x | 双跑全等 |
| `world-material-semantics-check.sh` | `gpu: world` :198/:336、`draw: begin` :297（`--capture-tick $target`） | 未普查 | 8x | 双跑全等 |
| `world-near-clip-check.sh` | `draw: begin` :161、`gpu: world` :210-242 | 未普查 | 8x | 双跑全等 |
| `world-render-reliability-check.sh` | `gpu: world` :169/:262、`gpu: reload-stage` :230/:408、`gpu: post-reload` :234、`gpu: device-error-policy` :296/:409（tick 0） | 无 | 保持缺省 | 不需提速 |
| `world-black-account-check.sh` | `field` :65-212（tick 0） | 无 | 保持缺省 | 不需提速 |
| `world-geometry-hole-check.sh` | `capture: armed` :301/:511 的 `frame_tick=`、`draw: begin` :304/:1430 的 `tick=`、`scene: camera` :527/:754、`gpu: world` :547/:969 | `capture: armed` 的 `frames=`（整段绘制帧数）；门禁是否对它断言数值未核 | 8x* | 双跑全等；`frames=` 只作诊断，不比数值 |
| `sultry-translucent-atlas-check.sh` | `gpu: world` :147-149（lit 0，启动行 :108-109/:127-128） | 无 | 保持缺省 | 不需提速 |
| `viewmodel-animation-check.sh` | product 段 :682-683 的 `scene: vm` :369-674、`gpu: viewmodel` :670（capture 帧姿态）；probe 段 :514/:640 用 `--player-animation` 给显式 pose tick，与速度无关 | 未普查 | 8x | product 段双跑全等；probe 段不变 |
| `weapon-animation-check.sh` | `scene: anim` :775 的 `state=`/`label=`（capture 帧）、`gpu: viewmodel`；product 启动行 :764-767/:965 | `animwitness` :776 是逐帧见证序列；门禁是否断言次数未核 | 8x* | 双跑全等；`animwitness` 只比「出现」不比次数 |
| `viewmodel-companion-check.sh` | `scene: anim` :460、`gpu: viewmodel` :561、`deny`/`always-deny`/`real-failure` :468-493（退出码=状态） | 未普查 | 8x | 双跑全等 |
| `viewmodel-contract-check.sh` | `draw: begin` :358、`gpu: viewmodel` :389-391 | 未普查 | 8x | 双跑全等 |
| `viewmodel-presentation-check.sh` | `bind`/`apply`/`arms`/`body` :327-353 的 capture 帧字段 | `baseline` :720-796 段的计数未核 | 8x* | 双跑全等；baseline 段单独确认 |
| `viewmodel-range-check.sh` | `gpu: viewmodel` :501-503（启动行 :492-493 带 `--start-paused`） | 未普查 | 8x | 双跑全等 |
| `viewmodel-robustness-check.sh` | `body-off` :614、`contact` :752-754、`m1`-`m4` :868-976 的 capture 帧字段 | 未普查 | 8x | 双跑全等 |
| `viewmodel-apply-check.sh` | `applied: vm` :378-379、`skinning` :380、`bone` :381、`bind: the` :388、`effective` :404、`hand` :443、`attachment` :445（capture 帧的应用结果） | :383-393 的 `the …` 逐帧尝试序列；`bind: no` :387 若断言「整段 trace 不出现」则是整段判定 | 1x | 先把「整段 trace 不含某串」改成「capture 帧不含」，再谈提速 |
| `weapon-body-check.sh` | `gpu: viewmodel` :308/:543、`draw: begin` :350、`scene: weapon` :538（`--capture-tick 2000`，启动行 :300-302） | 未普查 | 8x | 双跑全等 |
| `weapon-material-check.sh` | `draw: begin` :257、`gpu: viewmodel` :285/:512、`scene: weapon` :429（tick 2000/16306，启动行 :277-279） | 未普查 | 8x | 双跑全等 |
| `terrain-blend-check.sh` | `gpu: world` :308-310（启动行 :240-244：`--start-paused --capture-tick $TICK`） | 无（pause + 精确 capture 帧） | 8x（收益小） | 双跑全等 |
| `playback-control-check.sh` | `focus: keyboard` :228、`playback: state` :282 的 cursor 字段 | 脚本命令由墙钟 `--wait` 驱动，8x 改变「第几个绘制帧执行第几条命令」，命令交错序列改变（启动行 :250-254） | 1x | 保持 1x；提速需先把脚本改成按 tick 驱动 |
| `entity-material-check.sh` | python 打印的 `^PLAYBACK=` / `^ENTITYMATERIALS=`（:155-164 Popen，`CAPTURE_TICK`）：capture 时刻的 tick 与 resolved/total | python 块是否打印整段 run 的计数未核 | 8x* | 双跑：`PLAYBACK=` 相同 + `ENTITYMATERIALS=` 相同 |
| `material-coverage-check.sh` | `gpu: world` :83/:570/:574、`capture: armed` :750、`gpu: viewmodel` :752-753（启动行 :558-561） | `capture: armed` 的 `frames=`（同上） | 8x* | 双跑全等；`frames=` 只作诊断 |
| `hud-text-check.sh` | `hudtext:` :144-152（capture 帧的 HUD 文本行，启动行 :132-136） | 未普查 | 8x | 双跑：`hudtext:` 行逐字节相同 |
| `minimap-hud-check.sh` | `minimap: hud` :504、`minimap: cp` :505、`capture: frame` :506（run_capture 只给 `--capture-frame`，取首帧） | 无（无 `--capture-tick`，不旅行） | 保持缺省 | 不需提速 |
| `frame-capture-check.sh` | `capture: frame` 的 tick 0 帧（:302/:336 给 `--capture-tick 0`；:370 是 paused 段） | 无（三次启动都在 tick 0 或 paused） | 保持缺省 | 不需提速（本行即「缺速但无害」三行之一，§3.3） |
| `quiet-capture-check.sh` | `field` :178-184 的 capture 帧字段（:240/:242 是 tick 0；:304/:306/:392 是 `$LIVE_TICK`） | `$LIVE_TICK` 段若断言整段计数则变；未核 | 8x*（只对 `$LIVE_TICK` 段） | 双跑：`field` 行全等 |
| `logstf-check.sh` | `logstf: runtime` :357 的 `kind=` 等字段（启动行 :191-202） | `frames=` 是整段绘制帧计数 | 1x | 保持 1x；或把 `frames=` 的数值断言改掉 |
| `webview2-loader-check.sh` | 未普查（无 trace tag 命中；启动行 :193-195） | 未普查 | 1x（未证） | 先补 tag 取证再谈 |
| `fullscreen-quiet-check.sh` | `mode: window_created` :247/:365、`mode: summary` :254（窗口/音频标志=状态） | `hud: keys` :330/:743 的按键派发次数；门禁还含采样/sleep 的墙钟判定 | 1x | 保持 1x |
| `hud-check.sh` | `hud:` 布局与 `hud: keys` :455-480 之外的 capture 帧字段 | `hud: keys` 的按键派发计数是逐帧事件量 | 1x | 保持 1x |
| `audio-keys-check.sh` | `audio: master applied` :157/:186（出现即通过；启动行 :94-97 给 `--capture-tick $tick`、:107-110 给 1200） | 未普查 | 8x | 双跑：`audio:` 行出现即通过，数值不比 |
| `playback-error-paths-check.sh` | `audio: advance` :90/:196、`audio: seek` :181/:191、`audio: timeline` :225-226、`mode: window_created` :127/:363、`map: lookup` :128/:364、`camera: snapshot` :171、`camera: expect` :174（到 tick 600 的状态） | `camera: summary` :154/:232 的 counters、`mode: summary` :227-229 的 `events_seen=`/`played=`/`skipped=`、`capture: armed` :581/:600；且 D56 已记录本门禁两处断言与生产者不符（:154/:232、:227） | 1x | 先修 D56 的断言口径，再谈提速 |
| `animation-drive-states-check.sh` | LINE A/B/C 的 per-state trace 行（`state=`/`label=`/`needle=`/`reason=`，C1）与 crouch/crouchwalk 的 animdsum 状态值；md5 对比（9 次 run 合计 143,450 tick，启动 :125-150 无 `--playback-speed`） | C2 的 `flinchStarts=` 是下降沿计数（P6/P7）；`peak_of` 是整段峰值；`dsum_lines` 是帧数 | 1x（D59）；复测计划见 §6 | §6 |
| `hud-live-check.sh` | `scene: begin` :1036-1050 的 `tick=`、`pixels: outside` :1085、`hud_off: quads` :1086 | `hud: live` :1040 的逐帧文本更新计数（calls/updates/skips/published） | 保持缺省（本行即三行之一：capture run :366-370 只取首帧，提速无收益） | 不需提速 |
| `timeline-check.sh` | 无（tick 推进由 clamp 提供） | `clamp: product` :367 读整段 run 的 tick/clamp 结果，依赖墙钟推进（:274/:289/:310 已给 32/1，:366 是 `--start-paused` 无旅行） | 保持缺省（本行即三行之一） | 不需提速；`clamp:` 断言改动需单独设计 |
| `idle-spin-check.sh` | `metrics` CSV 的 capture 帧行（:192 `run_sample $EXE --audio-device`、:286 `--demo $SNAKE`） | metrics 是整段 idle 采样的计数/时长序列 | 1x | 保持 1x |

## 3. 统计与清单

### 3.1 分类计数

| 类别 | 行数 | 门禁 |
| --- | --- | --- |
| 纯沿量，可直接 8x | 16 | `lighting-semantics`、`lighting-dark-account`、`quality-audit`、`world-material-face`、`world-material-semantics`、`world-near-clip`、`viewmodel-animation`、`viewmodel-companion`、`viewmodel-contract`、`viewmodel-range`、`viewmodel-robustness`、`weapon-body`、`weapon-material`、`terrain-blend`、`hud-text`、`audio-keys` |
| 待核一处计数读数，核完可并入 8x | 7 | `review-workspace`、`world-geometry-hole`、`weapon-animation`、`viewmodel-presentation`、`entity-material`、`material-coverage`、`quiet-capture` |
| 保持缺省（tick 0 / paused / 首帧，收益 0） | 9 | `world-material`、`world-material-effects`、`world-render-reliability`、`world-black-account`、`sultry-translucent-atlas`、`minimap-hud`、`frame-capture`、`hud-live`、`timeline` |
| 必须 1x | 11 | `camera-ui`、`visual-baseline`、`viewmodel-apply`、`playback-control`、`logstf`、`webview2-loader`、`fullscreen-quiet`、`hud-check`、`playback-error-paths`、`animation-drive-states`、`idle-spin` |

合计 16 + 7 + 9 + 11 = 43，与 §2 行数一致。7 条带 `*` 的门禁对应 §3.2 的五处待核读数，核完必须按 §4 双跑才并入 8x。

### 3.2 待核的五处读数，核法各一句

1. `summary:` 计数（`review-workspace-check.sh:175`、`:188-190`）：核该串是否被次数断言，还是只判文本出现。
2. `capture: armed` 的 `frames=`（`world-geometry-hole-check.sh`、`material-coverage-check.sh`、`quiet-capture-check.sh`）：`grep -n "frames=" 脚本` 看它是否进入断言；只出现在诊断输出里则 8x 可落。
3. `animwitness`（`weapon-animation-check.sh:776`）：同法核该串是否被计数断言。
4. `baseline` 段（`viewmodel-presentation-check.sh:720-796`）：核里面有没有把次数写进比较。
5. python 块（`entity-material-check.sh:155-164` 之后）：核打印的是 capture 时刻的 tick 还是整段计数。

### 3.3 「缺速但无害」的三行

`timeline-check.sh`、`hud-live-check.sh`、`frame-capture-check.sh` 的启动点分别在 `--start-paused`、`--capture-frame`（首帧）、`--capture-tick 0`：不旅行 tick 时 `--playback-speed` 无论给不给，capture 帧都是 tick 0 的那一帧（P2 的钳位不发生）；它们的缺速不改变观察对象，所以归 B 或归 C 取决于列定义，本表保留成行但速度列写「保持缺省」。

### 3.4 与 D57 / D59 的对应

D57 记录「这些门禁从不 pin 速度」：43 行里 40 行的启动行确实没有 `--playback-speed`（§2 每行给出启动行号；`animation-drive-states` 的 `run_capture` 在 :125-150）。
D59 记录「提速不能按墙钟接受，要逐读数裁决」：本表把 D59 落成 43 行的逐读数判定，并给出统一验收配方（§4）。

## 4. 可提速门禁的验收配方

### 4.1 双跑全等

同一脚本、同一 `--capture-tick`、同一夹具，只差 `--playback-speed`：1x 跑一次、8x 跑一次。判据三条，全满足才算沿量：

1. capture 帧 md5 逐字节相同（P2 保证 tick 精确；若不同，问题在生产者或夹具，不在门禁）；
2. 状态行（`gpu:`/`scene:`/`mode:`/`draw: begin`）逐字节相同；
3. 计数行只比「出现」或 `>=1`，不比数值；若某条断言写了数值，这条门禁不进 8x 名单。

### 4.2 8x 名单（16 条纯沿量）

`lighting-semantics`、`lighting-dark-account`、`quality-audit`、`world-material-face`、`world-material-semantics`、`world-near-clip`、`viewmodel-animation`、`viewmodel-companion`、`viewmodel-contract`、`viewmodel-range`、`viewmodel-robustness`、`weapon-body`、`weapon-material`、`terrain-blend`、`hud-text`、`audio-keys`。带 `*` 的 7 条（§3.2）核完再并入；完整逐行判定以 §2 为准。

### 4.3 反向证据（防止「提速没生效」被误判为通过）

8x 跑必须同时满足：输出里仍有 `draw: begin` 行、`capture: armed` 行的 `frame_tick=` 等于请求的 `--capture-tick`、`frames=` 严格小于 1x 的值。三条缺一条即判定「提速未生效」，测量作废（§6.6 同此判据）。

### 4.4 不允许的验收方式

只用墙钟变短判定通过（D59 已否决）。

### 4.5 速度上限与建议值

上限 64x（P8）。建议 8x：1x 下每帧最多 16.67 tick（P10），8x 下每帧最多 133 tick，P2 仍把 capture 帧钳到请求值，但相机脚本的步进粒度（P3）变粗 8 倍——这正是 `camera-ui` 一类逐帧驱动门禁不能提速的原因。

### 4.6 收益的量化口径

收益 = `--capture-tick` 值 ÷（66.6667 × 速度）。已知 capture-tick 门禁时间里约 80% 来自 `animation-drive-states`（143,450 tick，1x 约 35.86 min）；其余门禁的 tick 总和本卡未逐个统计（未普查），因此本卡不给全库节省总量。

## 5. 必须留 1x 的 11 个门禁与改造方向

| 门禁 | 卡住的原因 | 改造方向（超出本卡） |
| --- | --- | --- |
| `camera-ui-check.sh` | `camera: accept` 的步数依赖逐帧步进（P3） | 相机脚本按 tick 记步，不按帧 |
| `visual-baseline-check.sh` | P0 基线，含未核的逐帧计数 | 先做 1x/8x 双跑取证，再决定 |
| `viewmodel-apply-check.sh` | 「整段 trace 不含某串」的判定 | 改成 capture 帧判定 |
| `playback-control-check.sh` | 命令交错由墙钟 `--wait` 驱动 | 脚本改成按 tick 注入 |
| `logstf-check.sh` | `frames=` 是整段帧计数 | 断言改成 `frames>=1` 或改读 `kind=` |
| `webview2-loader-check.sh` | 未普查（无 tag 命中） | 先补取证 |
| `fullscreen-quiet-check.sh` | `hud: keys` 派发次数 + 墙钟采样 | 键盘脚本按 tick 注入 |
| `hud-check.sh` | `hud: keys` 派发计数 | 同左 |
| `playback-error-paths-check.sh` | `camera: summary`/`mode: summary` 计数，且 D56 断言口径未修 | 先修 D56，再逐条改计数断言 |
| `animation-drive-states-check.sh` | `flinchStarts` 下降沿（P6/P7） | §6 复测；若确认变小，生产者改累计计数器（记缺陷） |
| `idle-spin-check.sh` | metrics 是整段采样序列 | 改成 capture 帧读 metrics |

## 6. lead 第 4 点：`animation-drive-states` 的 8x 复测计划（本窗口不执行）

### 6.1 目的

用一次受控复测回答：`animation-drive-states-check.sh` 的 C2 读数里，哪几个在 8x 下会变。这是整库提速的关键样本——它占已知 capture-tick 时间的约 80%。

### 6.2 精确改法（scratch 副本，不改工作树脚本）

1. `mkdir -p .scratch/n25 && cp animation-drive-states-check.sh .scratch/n25/ads-8x.sh`（脚本按相对仓库根取夹具，位置可复制运行；若路径解析依赖脚本目录，则改为在同目录建临时副本，事后删除）。
2. 在 `run_capture`（:125-150）调用产品的 argv 里加 `--playback-speed 8`（与该函数已有的 `--capture-tick`、`--capture-frame`、`--metrics-file` 同级）；9 次调用与 4 个负控一律不改。
3. 1x 对照不新建文件：用 worktree 里的原脚本原样跑一次（工作树当前状态即为 1x 取证）。

### 6.3 要逐值比较的六个数字

`LINE C peak over N animdsum frames: crouch=<a> crouchwalk=<b>(tick=<t1>) flinch=<c> flinchStarts=<d>(tick=<t2>) fallback=<e>` 这一行里的五个数 a..e，加上 N（`dsum_lines`，即 animdsum 帧数）与紧邻的 `LINE C last frame:` 行；另加 LINE A/B 的全部 md5。比较方式：逐 token 取 `值=` 后按数值比较，不做文本 diff。

### 6.4 判据（可判定，不写乐观结论）

| 读数 | 8x 与 1x 的判据 | 依据 |
| --- | --- | --- |
| crouch、crouchwalk | 期望相等；不等即说明 C1 的抽取吃帧数，门禁降级为 1x | C1 从 trace 的 `state=` 字段抽取（P6 状态字段） |
| flinch | 记录实测值；若变小，与 `flinchStarts` 同因 | P6 |
| flinchStarts | 允许 `<`（下降沿在 8x 下漏采样）；若 `<`：本门禁只能 1x，或生产者改成累计计数器（P7 的「上一帧在 flinch、本帧不在」依赖相邻帧）；后者超出本卡 → 记缺陷 | P7 |
| fallback | 期望相等（状态回退，不是边沿） | P6 |
| N（dsum_lines） | 期望严格 `<`；若相等 → 提速未生效，测量作废（§4.3） | P1/P3 |
| md5（LINE A/B 全部） | 必须全部相同；任一不同 → 观察对象被改变，停止提速讨论 | P2 |

### 6.5 墙钟

纯 capture 时间：143,450 tick ÷（66.6667 × 8）= 268.97 s ≈ 4.48 min（8x）；1x 为 143,450 ÷ 66.6667 = 2151.75 s ≈ 35.86 min。加上 9 次进程启动、4 个负控整跑与脚本解析，估算 8x 一次 6–8 min、1x 一次 40–45 min。以上为 tick 算术估算，未实测。

### 6.6 失败时的处置

md5 不同 → 先查夹具与 `--capture-tick` 是否真的传给了产品（这是脚本/生产者问题，不是门禁问题）；N 相等 → 提速参数没生效；`flinchStarts` 变小 → 按 6.4 记缺陷或把该门禁固定在 1x。三种处置都不在本卡执行。

## 7. 与既有裁决的接口

- D57（门禁从不 pin 速度）：本表 §2 每行的启动行号即为证据；提速改造的第一步统一是「把 `--playback-speed` 写成脚本常量并显式传入」，而不是继承缺省 1.0（P9）。
- D59（逐读数裁决）：本表是它的执行形态；§4 的「不接受墙钟通过」直接沿用。
- 与 D58（Win32 控件与 HUD 面板不重叠无人负责）无交集：本表只判读数值不变性，不判画面观感。

## 8. 未决问题

1. 那 11 个「提及 exe 但从不启动」的文件没有逐个定位（只确认了 4 个代表），9+40+3 与 63 的差额因此仍差一行合账。
2. 23 条沿量门禁的 tick 总和未统计，全局节省量给不出。
3. 4 处待核读数（§3.2）未核。
4. A 列 10 个已在用速度的门禁没有逐门判定（`attach-weapon-budget` 32x、`demo-clock`、`map-geometry-lighting`、`spectate` 9 处、`outline-check` 6 处等）；它们已在速度下通过，但「通过时断言读的是状态量」未逐条取证。建议 W3 抽检 3 个（`attach-weapon-budget`、`demo-clock`、`spectate`）。
5. `--playback-speed` 上限 64x（P8）只在生产者侧验证过接受范围，没有门禁在 64x 下跑过（未普查）。

## 9. 证据

| 命令 | 结果 |
| --- | --- |
| `grep -c -- "--playback-speed" *-check.sh` 逐文件 | 14 个文件提到该参数：attach-weapon-budget 2、bone-mismatch-semantics 1、build-identity 5、class-pixels 1、demo-clock 1、fog 1、hud-live 3、map-geometry-lighting 1、outline-check 6、player-body-appearance 1、spectate 9、third-person-weapon 2、timeline 6、world-geometry-budget 2 |
| 对每个门禁 `grep -n "tf2_demo_native\|\$EXE\|\$PRODUCT" <gate>` 并读上下文 | 得到 §2 每行的启动行号；例：`lighting-semantics-check.sh:132-133`、`world-material-check.sh:380-382`、`terrain-blend-check.sh:240-244`、`animation-drive-states-check.sh:125-150` |
| 读 `native/src/main.cpp` 的 10015-10033、12716-12721、12742-12755、11202、11208-11220、5977-5981、1959、9982 区段 | §1.1 的 P1–P10 表按行号逐条落账 |
| 读各门禁的 `TIMEOUT=`/`SPEED=`/`TICK=` 赋值行 | `animation-drive-states-check.sh:100 TIMEOUT=420`；`attach-weapon-budget-check.sh:42 SPEED=32`、`:43 TICK=30001` |

## 10. 本文件没有证明什么

- 没有执行任何 8x 复测：§2 的「8x」是「读数均为状态量」的判定，不是已观测到的通过；§4 的双跑全等一次都没跑。
- 没有证明 §4.2 的 16 条纯沿量门禁在 8x 下真的全等——§2 里多条门禁的计数行写着「未普查」（没有逐条核过），双跑是唯一判据；7 条带 `*` 的门禁另有一处读数未核。
- 没有给出全局节省量：除 `animation-drive-states` 外的 tick 总和未统计。
- 没有核那 11 个「提及 exe 但从不启动」的文件，63 的合账仍缺一行。
- 没有判定 A 列 10 个已在用速度的门禁，也没有任何门禁在 64x 上限下跑过。
- 没有动脚本、没有改断言、没有提交；`animation-drive-states` 的 8x 计划（§6）在本窗口不执行。
