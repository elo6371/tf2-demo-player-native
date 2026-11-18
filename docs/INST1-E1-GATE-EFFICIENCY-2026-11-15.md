# INST1 E1 门禁效率 — 12 个被算术清扫改过的脚本、帧身份、复用判据、耗时登记

日期 2026-11-15　实例 arch-ci　证据目录 `D:/_shared/EXEC-2026-11-15/INST1/`

## 0. 一句话结论

12 个脚本按"改它要付多少代价"分成四类后，真正的成本主项**不是产品启动，是重建**：`world-material-check.sh` 有 10 处 `build-target.sh`、`world-material-effects-check.sh` 有 5 处，而两者各只有 5 次和 2 次产品启动 —— 每注入一个 mutation 就要重编一次产品。因此复用机制应当以 **exe sha256** 为第一判据（`map-geometry-lighting-check.sh:335` 的 `.build.<name>` 已经是这么做的，是全仓唯一落地的一份），而 13 键身份的哈希成本只有 0.5–1 s，**不是瓶颈**（snakewater demo 51.8 MB → 0.13 s）。

另外两条：① 12 个里有 **9 个**的 `OUT` 缺省直接写进被跟踪的 `evidence/`（`userinfo-check.sh:29` 甚至不可覆盖），只有 3 个落 `.scratch`；② D53 §9.3 的普查口径漏了两个脚本 —— `t2-visual-matrix.sh` 与 `release-rebuild.sh` 不是 `*-check.sh`，从未进入那套 85/54/14 的统计。

## 1. 本轮硬约束：为什么没有产品耗时

本片开工时 `bash /d/TF2_Native_Worktrees/_shared/hostlock.sh status` 返回：

```
FREE
queue=head=
reservation=none
product_processes=2
ANOMALY product_running_without_lock=1 (lock is free but tf2_demo_native.exe is alive:
  a capture is unprotected and a new run would share the host)
```

两个活产品（`wmic` 实测，均为别的实例）：

| PID | 发起方 | 命令要点 | 状态 |
|---|---|---|---|
| 50732 | INST3 | `--capture-tick 20000 --playback-speed 8 --follow-target 9 --follow-action prev` | Not Responding，1,481,744 K |
| 50544 | INST4 | `--capture-tick 14765 --player-animation --animation-drive legacy` | Running，418,896 K |

→ 锁虽是 FREE，但宿主上已有两个渲染器在抢 GPU。此状态下任何"新起一次产品"，量到的都不是地图而是竞争（`world-geometry-hole-capture.sh:395` 的 `wait_for_idle_host()` 就是为这条写的）。**所以本片不登记任何产品秒数**，只登记静态门禁与离线自检的实测耗时；产品侧耗时按"启动次数 × 单次量级"给出估算区间，并明确标注为未测。

## 2. 表 A：12 个脚本四分类

分类定义（本片自用，四类互斥，按"最贵的验证方式"归类）：

- **A 静态可跑**：不起产品、不建、不要锁。
- **B 短正负控**：只跑探针（`probe.exe` 类），或只跑脚本自带的自检；仍需锁的写明。
- **C 可复用捕获**：要起产品，且每次起产品的输入可用哈希钉住 → 帧可缓存。
- **D 需重渲染**：要起产品，且判据是像素/末帧/峰值，缓存一旦张冠李戴就是假绿。

| # | 脚本 | 类 | 产品启动（实测） | `build-target.sh`（实测） | 自带离线自检 | 锁 |
|---|---|---|---|---|---|---|
| 1 | `hud-live-check.sh` | **C**（且有 A 的一角） | 1 个函数 `run_capture()` :352，6 个调用点 :623/:624/:810/:829/:878/:924 | 2（:1172 注入、:1213 还原） | **有**：`--selftest-print-out` :232-288 | 不用（自检）/ 需（正式） |
| 2 | `map-geometry-lighting-check.sh` | **C（参考实现）** | `capture()` :327，`for attempt in 1 2` :348 | 1（:248 提示语） | 无 | **自己持有** hostlock :192/:253-272 |
| 3 | `player-model-check.sh` | **B** | **0**（只跑 probe：:137 `--water-fixture`、:153 `--fixture`、:159 `--bsp`） | 1 | 无 | 无 |
| 4 | `release-rebuild.sh` | **A** | 0（只做重建与 mtime 诊断） | 2 | — | 无 |
| 5 | `t2-visual-matrix.sh` | **D** | `run_capture()` :144，`--capture-tick "$tick"` :150 | 0 | 无 | 无 |
| 6 | `userinfo-check.sh` | **B** | 0（probe + 被跟踪的 `gen_fixtures.py` :149） | 4 | 无 | **要求调用者已持锁** :13-15 |
| 7 | `viewmodel-animation-check.sh` | **D** | `run_product()` :717，**8 个调用** :751/:834/:885/:925/:1006/:1053/:1071/:1104 | 2 | 无（但会跑 probe `--self-test` :478） | 自己建 `.scratch/verify-all.lock` :287 |
| 8 | `viewmodel-companion-check.sh` | **D** | 1 个启动体 :556，`for attempt in 1 2` :380 | 3 | 无（probe `--self-test` :263） | `.scratch/viewmodel-companion.lock` :108 |
| 9 | `viewmodel-material-check.sh` | **D（冻结帧样板）** | `product_launch()` :721 | 3 | 无 | `.scratch/viewmodel-material.lock` :163 |
| 10 | `vmt-nesting-check.sh` | **B** | 0（只跑 probe） | 0 | 无 | **要求调用者已持锁** :36-37/:49/:77 |
| 11 | `world-material-check.sh` | **C（最贵）** | 5 处 `timeout ... $PRODUCT`（首条 :193，另有 :380/:493 等） | **10**（:508/:530/:566/:603/:642 …） | 无（probe `--self-test` :142） | 无 |
| 12 | `world-material-effects-check.sh` | **C** | `run_product()` :183-201，启动 :193 | **5**（:508/:530/:566/:603/:642） | 无（probe `--self-test` :105） | 无 |

分类计数：**A 1 / B 3 / C 4 / D 4**。

### 2.1 这条分类给出的第一个结论

把 `build-target.sh` 的出现次数和产品启动次数并排看：

- `world-material-check.sh`：10 次构建 vs 5 次产品启动 —— **构建次数是启动次数的 2 倍**。
- `world-material-effects-check.sh`：5 次构建 vs 2 次产品启动 —— **2.5 倍**。
- `viewmodel-*` 三个：3/3/2 次构建。
- `t2-visual-matrix.sh`、`vmt-nesting-check.sh`：0 次构建。

全仓产品构建在本机是"只编 `generated/tf2_build_stamp.cpp` 一个 TU + 重链"（R1 实测日志，13 行，`rc=0`），单次量级是**秒级**；但 mutation 型门禁每次注入都要改源码再全量重链，量级上到**十秒–分钟级**。所以：**"减少门禁耗时"的第一出手点是把 mutation 的构建结果按 exe sha 缓存，而不是把产品启动提速。**

## 3. 表 B：五列（gate / 状态量读数 / 沿量读数 / 建议 speed / 验收方式）

先说清口径。D59 的三分法（`docs/D53-CR-ASSUMPTION-SWEEP-2026-11-15.md:430-434`）：状态量=某时刻字段或远长于帧间隔的持续状态；沿量=相邻两个被观测 tick 的差分/边沿计数；帧计数=trace 行数/绘制帧数/首末帧序号。**提速只改变后两类。**

本片对 12 个脚本做了读数模式普查（pk=`sort -n`/`tail -1`/`peak_of`/`max_frames`；cnt=`wc -l`/`grep -c`；edge=`Starts`；pix=`md5_of`/`cmp_frames`/`bmp_diff`/`sha256sum $`）。**其中 5 个脚本落在 D53 §9.3 的"52 个未钉 speed"集合里，有完整画像；其余 7 个要么已经钉了 speed（不在普查集），要么不是 `*-check.sh`（根本没进普查）。未普查的格子我一律写"未普查"，不填猜测值。**

| gate | 状态量读数 | 沿量读数 | 建议 speed | 验收方式 |
|---|---|---|---|---|
| `hud-live` | 帧 `hud: live summary` 计数/更新数/跳过数（每 case 一条汇总行） | 未普查（已自行钉 `ADVANCE_SPEED=8`） | **保持 8**，并把 :143-146 那段"8x 是墙钟手段不是被测属性"的注释推广成模板 | px 比较 15 处 + 69 断言；另有**离线自检 3 断言**可先跑 |
| `map-geometry-lighting` | 未普查（已钉 `--playback-speed "$SPEED"` :343） | 未普查 | 保持现值；**复用是它的重点而不是 speed** | 13 断言；`.build.<name>` = exe sha 才复用（:335） |
| `player-model` | probe 输出的模型/预算行（cnt=1） | 无（edge=0） | 不需要（**不跑产品**） | 25 断言，纯 probe |
| `release-rebuild` | mtime/size 诊断（DIAGNOSTIC ONLY，从不裁决） | 无 | 不需要 | 0 断言，靠人读 |
| `t2-visual-matrix` | 各视角的 `SKY_ON/VIS_ON/WAT_ON` 读数行 | 未普查 | 未普查（**不在 52 集内**：非 `-check.sh`） | 31 断言 + 4 处像素比较；bmp 与 sha 复制进 `$OUT/frames/` :303/:310/:319 |
| `userinfo` | `userinfo_at` 逐 tick 行 | 无（edge 未普查） | 不需要（**不跑产品**） | 50 断言，probe + 被跟踪 fixture 生成器 |
| `viewmodel-animation` | 探针末行（`rfield`/`rline`/`rsum`） | **有**：`max_frames` 采样峰值，判据 `==0`/`==31`/`==61`（D53 §9.4 第 9 行已逐条核过） | **明确拒绝提速**（头注释 :64 `--playback-speed is REFUSED (exit 2)`，理由 :96-97） | 160 断言 + probe 自检 |
| `viewmodel-companion` | pk=11 cnt=6 里的大部分 | edge=1 | 未钉；**先修阻塞再看 speed** | 127 断言；probe `--self-test` |
| `viewmodel-material` | pk=7 pix=11 的读数 | edge=0 | **不需要提速**：全部像素比较用 `--start-paused --capture-tick 0`（:540-546 记录了 D57 的四次实测：200/201/202 三个 tick 抖动） | 137 断言 + 11 处像素比较 |
| `vmt-nesting` | probe 的 json/err/rc 三件套（cnt 未普查） | 无（不跑产品） | 不需要 | 41 断言 |
| `world-material` | pk=1 cnt=1；主体是逐 case 的 probe 报告行 | edge=1 | 未钉 | 129 断言；5 次构建是主要成本 |
| `world-material-effects` | pk=12 pix=8 | edge=1 | 未钉；9 处 `--start-paused` 已经把世界钉住了，**提速对它几乎没有意义** | 90 断言 + 13 处像素比较 |

### 3.1 这五列给出的第二个结论

**对判据是像素的门禁，正确的密度旋钮不是 `--playback-speed`，而是 `--start-paused --capture-tick 0`。** `viewmodel-material-check.sh:540-546` 把这条实测写进了注释：四次 `--capture-tick 200` 分别落在 tick 200/201/202，窗口抖动本身就是读数的一部分；改用 `--start-paused --capture-tick 0` 后同一设置两次启动字节一致，并且注释里另有一句成本对照 —— **"a paused launch takes about 20 s against about 55 s for a 200-tick one"**。

这条比 D53 §9.7 的"给读数打标签"更省事：不必逐条判状态量还是沿量，**只要判据是像素，就先冻结 tick**，speed 归 1 也无所谓。12 个脚本里已经有 4 个在用这条路径（`viewmodel-material` 3 处、`world-material-effects` 9 处、`t2-visual-matrix` 按 `pauseArg` 分支 :151、`hud-live` 5 个 case 中的 4 个）。剩下该补的是 `world-material`（5 次启动全是 `--capture-tick 0` 但**没有 `--start-paused`**，见 :193/:380/:493 的命令行）。

## 4. 帧身份字段与复用判据

**不要发明新字段集。** 全仓唯一落地的一份在 `world-geometry-hole-capture.sh`，直接照抄：

```
# world-geometry-hole-capture.sh:237
KEY_FIELDS="cacheFormat exe shader demo map assets tick window flags captureScript settings captureMode gpu"
```

13 项，逐项来源（同文件）：`exe`=`sha_of "$PRODUCT"` :212；`shader`=`shader_identity()` :163（逐个着色器文件 size+sha 后整体 sha）；`demo`/`map` :213-214；`assets`=`asset_identity()` :175（`*_dir.vpk` + loose `materials/` + loose `models/` 三块分别列出后整体 sha）；`tick`/`window`/`flags` 由调用点给 :247-249；`captureScript`=`sha_of "$0"` :215；`settings`=`sha_of "$(iso_fixture_settings)"` :216；`captureMode`；`gpu`=`gpu_identity()` :194。

配套三条，缺一条复用就会骗人：

1. **provenance 记而不比**：`provenance_line()` :262-267 写 `provenanceHead`/`provenanceTfRoot`/`provenanceProduct`/`provenanceCapturedAt`，文件里明写理由 `a commit is not a pixel`（:287）。
2. **先验产物再信键**：`artifact_line()` :268-280 写 `frameSha256`/`frameBytes`/`traceSha256`/`traceBytes`/`frameWidth`/`frameHeight`/`capturePose`；复用裁决时**先**核这些（:351-363）**再**比键（:364-370）。
3. **字段存在性与取值分开判**：:338-340 的注释说明 `window=` 对 `ref` 那一帧**合法为空**，所以只对 `KEY_FIELDS` 查存在、对 `artifact` 块查非空。

### 4.1 复用判据（可直接照做的五步）

```
1. 产物存在且非空                     -> 否则 miss=absent
2. 产物 tick == 本次 tick            -> 否则 miss=trace-tick=<x>
3. 相机行存在且含 eye= 与 src=       -> 否则 miss=trace-no-camera-pose / -no-eye / -no-src
4. 身份文件存在 + 13 字段齐全 + frameSha/traceSha/分辨率/capturePose 全对
5. 13 个 key 逐字段与本次相等         -> miss 时报出具体字段名
```

命中时若只有 `provenanceHead` 变了，`world-geometry-hole-capture.sh:371-376` 会记一条 detail 但**不判失效** —— 这条要保留，否则每次 commit 都会把缓存清空。

### 4.2 复用成本核算（本轮实测，32 核）

| 输入 | 体积 | `sha256sum` |
|---|---|---|
| snakewater matcha demo | 51,834,617 B | **0.13 s** |
| `pl_upward.bsp` | 25,446,018 B | **0.11 s** |
| pl_upward POV demo | 146,853,666 B | **0.34 s** |
| `tf/*_dir.vpk` ×4 | — | 0.08 s |
| `hl2/*_dir.vpk` ×4 | — | 0.10 s |

本机 `tf/materials` 的 loose 文件只有 **4 个**、`tf/models` loose **0 个**，所以 `asset_identity()` 的 `find` 在这台机器上几乎零成本。**整份 13 键身份 ≈ 0.5–1 s**，相对单次产品启动（秒级）或单次 mutation 构建（十秒–分钟级）可以忽略。**复用的成本不在这里，别拿"哈希太贵"当不做的理由。**

### 4.3 现在的落地情况

- 12 个脚本里**只有 1 个**有可复用捕获：`map-geometry-lighting-check.sh`，且它的判据只是 exe sha256（`--reuse` :214/:229，`.build.$name` 比对 :335，注释 :330 写着「A frame on disk is only this build 的 frame」）。
- 它的缓存目录 `.scratch/t62-map-axes` **当前不存在**（`ls` → No such file or directory，`find *.bmp` → 0）→ **`--reuse` 此刻无物可复用，必须先跑一次完整矩阵才谈得上复用。**
- `world-geometry-hole-capture.sh` 的身份机制成熟，但它是 capture 脚本、不在这 12 个里；`evidence/` 下被跟踪文件 6,172 个（241 个 `.bmp`、227 个 `*identity*`），`evidence/t26-world-holes` 39 个 —— **带身份的历史帧基本只有 t26 一份。**
- `.scratch/` 里堆着大量无身份帧：`map-asset-probe` 18G、`world-material` 122M、`world-material-semantics.*` 120M、`e5-g2-hud` 84M、`t36-account.5487` 78M、`hud-check` 75M、`w4probe` 74M、`logstf` 63M。

## 5. 耗时登记（本片实测，均为静态/离线，不含产品）

| 命令 | 本轮实测 | 上次实测 | 说明 |
|---|---|---|---|
| `bash gate-registry-check.sh` | **55.01 s** | 11.549 s | `GATE-REGISTRY=PASS gates=84 registered=34 excluded=50`；**同一命令在宿主被两个产品占满时慢 4.8×** |
| `bash gate-launch-hygiene-check.sh` | **4.23 s** | 2.842 s | `PASS violations=0 baseline=0 warnings=26 lifecycle=1 arrskipped=524 pslaunch=5 psunflagged=0` |
| `bash tools/check-nested-arith.sh` | **0.75 s** | — | `files=136 arithmetic=907 nested_cmdsub=21 sites=0` / `VERDICT: OK`（算术体 905→907，**期间又有人改过脚本**） |
| `bash hud-live-check.sh --selftest-print-out` | **2.04 s** | — | rc=0，`HUD-LIVE-REDIRECT-SELFTEST=PASS assertions=3`，**不起产品/不建/不要锁** |
| `bash -n` ×12 脚本 | **3.70 s** | — | 全部通过 |

产品侧（**未测，本片禁止**）：按 D53 §8 的量级，一次 1× 播放到 tick N 约 N/66 秒；`--playback-speed 32` 可压到 1/32。`viewmodel-material-check.sh:546` 的自记读数是"paused ≈ 20 s，200-tick ≈ 55 s"。**这两个数字都不是我这一轮测的，引用时须标注出处。**

## 6. 三条结构性发现

### 6.1 成本主项是构建，不是启动
见 §2.1：`world-material-check.sh` 10 次构建 / 5 次启动，`world-material-effects-check.sh` 5/2。**任何"让门禁更快"的方案，先看 mutation 构建能不能复用。**

### 6.2 只有 2 个门禁对提速表态，其余 13 个是"默默选了一个密度"
全仓 15 个传 `--playback-speed` 的门禁里，只有 `viewmodel-animation-check.sh`（:64 `--playback-speed is REFUSED (exit 2)`）与 `animation-drive-states-check.sh` 各写了 1 处拒绝声明，并在注释里给出理由。另外 13 个（`attach-weapon-budget` 2、`bone-mismatch-semantics` 1、`class-pixels` 1、`demo-clock` 1、`fog` 1、`hud-live` 3、`map-geometry-lighting` 1、`outline` 6、`player-body-appearance` 1、`spectate` 9、`third-person-weapon` 2、`timeline` 6、`world-geometry-budget` 2）**选了密度但没留任何"我知道这会改变读数"的痕迹** —— 这正是 D59 说的"没有信号"。

### 6.3 普查口径漏了两个脚本（D53 §9.3 的 85 是个下界）
`gate-registry-check.sh` 本轮报 `gates=84`；D53 §9.3 用的是"仓库根 `*-check.sh` 共 85 个"；我本轮按"命中任一读数模式"数出 81 个（含 `--demo` 的 67 个，其中 15 个钉了 speed、52 个没钉）。**三个数字对不上，原因之一已经定位**：`t2-visual-matrix.sh` 与 `release-rebuild.sh` 根本不叫 `*-check.sh`，所以既不在 85 里也不在 81 里 —— 而它俩都在本次被算术清扫改过的 12 个脚本之中。**任何基于 `*-check.sh` glob 的普查都会漏掉它们。**

## 7. 12 个脚本的阻塞项（与本机现状有关）

**缺 demo（本机不存在 `autorecord_2026-07-02_13-26-46.dem`）→ 门禁在第一个产品启动前就退：**

- `viewmodel-material-check.sh:548` `PRODUCT_DEMO="$TF/demos/autorecord_2026-07-02_13-26-46.dem"`（硬编码，没有 `${VAR:-}` 兜底）
- `world-material-effects-check.sh:58` `VM_DEMO="$TF/demos/autorecord_2026-07-02_13-26-46.dem"`（同上）
- `viewmodel-companion-check.sh:246` 从 `PINS` 清单里按**文件名**认领 `autorecord_2026-07-02_13-26-46.dem`；认领失败时 :250-253 会 `VIEWMODEL-COMPANION=FAIL (the pinned POV recording could not be resolved)` + `exit 1`

对照：已经改成 env 覆盖式的门禁有 5 个（`bone-mismatch-semantics:68`、`class-pixels:102`、`player-animation-pixels:47`、`player-body-appearance:42`、`probe_attach:26`），写法都是 `${VAR:-<默认 demo>}`。**改法照抄它们即可，不要发明新写法。**

**硬编码 TF 根（12 个里 10 处）**：`hud-live:112`、`map-geometry-lighting:191`、`player-model:47`、`t2-visual-matrix:89`、`userinfo:32`（`DEMO_DIR`）、`viewmodel-animation:121`（另有 :437 写死 `E:/demo测试/pl_upward_f12/POV/`）、`viewmodel-companion:85`、`viewmodel-material:126`、`world-material:73`、`world-material-effects:55`。全部是 `D:/SteamLibrary/steamapps/common/Team Fortress 2/tf` 字面量，没有一处 `${TF_ROOT:-}`。

**OUT 缺省落进被跟踪目录（9 个）**：`hud-live`→`evidence/t97-hud`(:117)、`map-geometry-lighting`→`evidence/t62-map-axes`(:194)、`player-model`→`evidence/t57-player-model/runtime`(:50)、`t2-visual-matrix`→`evidence/t2-visual-20261015`(:99)、`viewmodel-animation`→`evidence/fast/viewmodel-animation`(:152)、`viewmodel-companion`→`evidence/fast/viewmodel-companion`(:105)、`viewmodel-material`→`evidence/viewmodel`(:132)、`world-material`→`evidence/world-material`(:77)、`world-material-effects`→`evidence/world-material-effects`(:65)。**唯一不可覆盖**：`userinfo-check.sh:29` `OUT=evidence/t100-userinfo/runs`（连 `printenv` 都没有）。只有 `vmt-nesting`(:45 `.scratch/vmt-nesting`) 与 `hud-live` 的 mutation 分支(:213 `OUT="$SCRATCH/out"`) 落 `.scratch`。

→ **建议（不在本轮实施）**：缺省统一成 `.scratch/<gate>/<RUN_TAG>`，显式 `--publish` 才写 `evidence/`。`hud-live` 的 `resolve_out_placement()` :206-221 + `--selftest-print-out` :232-288 已经把这条规则**做成可离线验证的 3 条断言**，是最好的样板。

## 8. 承接的欠账：14 个算术站点真跑证据

lead 的清扫报告自己写了 NOT VERIFIED：**14 个修复站点从未在真实 check 脚本里跑过**，只经最小复现 + 修复后静态扫描 + `bash -n`。本片能补的只有一格：

- 12 个脚本里**只有 `hud-live-check.sh` 有一条真能离线跑通的路径**（`--selftest-print-out`，:232-288，3 条断言：默认落位 / mutation 重定向到 `.scratch` / 显式 evidence 被尊重并 WARN）。
- 本轮实跑：`rc=0`，`OK S1` / `OK S2` / `OK S3` / `HUD-LIVE-REDIRECT-SELFTEST=PASS assertions=3`，**2.04 s**，全程无产品、无构建、无锁。证据：`D:/_shared/EXEC-2026-11-15/INST1/E1-out/selftest-hud-live-print-out.txt`。
- **但它没有覆盖被改的那个站点。** `hud-live-check.sh` 的算术站点在 :702（`EXPECT_GLYPHS=$(( $(slen "$L_T1") + ... ))`，修复后是 :705-708），位于像素比较段，自检根本不进那段。**所以这条实测只能证明"该脚本能离线自检"，不能证明"守卫会正确触发"。**

其余 11 个脚本没有任何离线出口：`map-geometry-lighting`（要锁 + 产品）、`player-model`/`userinfo`/`vmt-nesting`（要 probe exe）、`release-rebuild`（要重建）、`t2-visual-matrix`/`viewmodel-*`/`world-material*`（要产品）。**14 个站点的真跑证据仍然欠 13 个。**

## 9. 本片未证明项

1. **任何产品耗时都没测**（§1 的 ANOMALY）。表 A 的"产品启动次数"是静态计数，不是墙钟。
2. **7 个脚本的读数类型未普查**（表 B 里写"未普查"的格子）：`hud-live`、`map-geometry-lighting`、`t2-visual-matrix`、`userinfo` 的 edge 维度、`viewmodel-companion` 的分类、`world-material` 的沿量。要普查就得逐行读 trace 消费点，本片没做。
3. **`--reuse` 从未在 map-geometry-lighting 上真正命中过**（`.scratch/t62-map-axes` 不存在）。它的判据是"读代码 + 读注释"得出的，不是我看着一次 miss 变 hit。
4. **`world-geometry-hole-capture.sh` 的复用路径也没跑过**（它要 18 帧产品捕获）。13 键字段集是从代码读出来的，字段语义（尤其 `gpu_identity()` 到底读什么）我没实跑核对。
5. **建议的 `.scratch` 缺省落位没做成本核算**：改了之后每个门禁多一次 mkdir 与一次清理，是否会带来新的 `d/` 式路径污染（`hud-live:120-126` 记的那次事故），未验证。
6. **`t2-visual-matrix.sh` 的 `pauseArg` 分支只读了 151 行**，没核每个 case 到底传 `pause=1` 还是 0；表 B 把它标"未普查"就是这个原因。
7. **D53 §9.3 与 lead 转述的"40 个"差额仍未核对**（arch-ci 原清单不在本工作树可读范围）。本片只提供了第三个数字（81/67/15/52）与一个已定位的口径漏洞（§6.3）。

## 10. 复现命令

```bash
cd /d/TF2_Native_Worktrees/t105-e5
# 门禁数与注册数
bash gate-registry-check.sh
# 启动卫生（静态，4 秒）
bash gate-launch-hygiene-check.sh
# 嵌套算术门禁（0.75 秒）
bash tools/check-nested-arith.sh
# 唯一能离线跑的门禁自检（2 秒，无产品/无构建/无锁）
bash hud-live-check.sh --selftest-print-out
# 12 个脚本的读数模式普查（只读）
for f in hud-live-check.sh map-geometry-lighting-check.sh player-model-check.sh release-rebuild.sh \
         t2-visual-matrix.sh userinfo-check.sh viewmodel-animation-check.sh viewmodel-companion-check.sh \
         viewmodel-material-check.sh vmt-nesting-check.sh world-material-check.sh world-material-effects-check.sh; do
  printf "%-40s pk=%s cnt=%s edge=%s pix=%s\n" "$f" \
    "$(grep -cE "sort -n|tail -1|peak_of|max_frames" "$f")" \
    "$(grep -cE "wc -l|grep -c" "$f")" \
    "$(grep -cE "Starts" "$f")" \
    "$(grep -cE "md5_of|cmp_frames|bmp_diff" "$f")"
done
# 复用缓存现状（当前应为"不存在"）
ls -la .scratch/t62-map-axes; find .scratch/t62-map-axes -name "*.bmp" | wc -l
```

