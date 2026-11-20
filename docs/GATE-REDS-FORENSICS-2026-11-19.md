# 两道既有红的取证与裁决（第 8 轮 G11 交付，已核对落盘）

* 基线：`work/integrate @ 28aba482`
* 来源：Gemini 第 8 轮交付 `gemini_round8_deliverable_2026_11_19.md` 第三部分（G11）
* 核对结论：两条红的证据链**全部为真**（行号、原文、读数逐条对过）；裁决也是对的；
  **但红 1 的根因解释要换**（§1.2），且交付自报的第 3 个未决项已经可以回答（§2.3）。

## 1. 红 1：Capture Point `0 / 7`

### 1.1 证据（已逐行核对）

* `viewmodel-contract-check.sh:713-714`：
  `# comparison premise and was measured on the pinned demo (firstPacketTick=10252).` / `CAPTURE_SCENE_TICKS="10252"`
* `viewmodel-contract-check.sh:372-375`：`capture_scene_tick` 用 awk 取「`draw: begin` 之前的最后一个 `scene: begin`」。
* `viewmodel-contract-check.sh:727-741`：7 帧循环 + `assert_eq "all 7 compared frames came from a measured capture point" "$COMPARE_POINTS_KNOWN" "7"`。
* `evidence/n18-gate-baseline-2026-11-19/n18-contract-baseline.log.txt:146-148`：
  `capture sceneTicks: 10320 10320 10320 10320 10320 10320 10320  (measured points: 10252)` +
  `FAIL all 7 compared frames came from a measured capture point = 0, expected 7`。

### 1.2 根因：不是「启动抖动 / 首个快照漂移」，是 T63-F1 的两套 tick 空间

* `demo-clock-check.sh:2-14` 就是这条规则的出处（标题即 "one demo carries TWO tick spaces"）：
  * **entry tick** = 命令在 demo 文件里的序号；signon 命令也占号，所以首个 data packet 不是 0
    （snakewater：**signon entry 10252**、首个 data entry 1）。
  * **server tick** 只在 `svc_NetTick` 出现时前进；包会丢，所以两者之差**不是常数**。
  * T63-F1 之前，产品拿 `firstPacketTick`（=signon entry tick）当服务器原点再加 playback tick，
    所有实体/特效查询都差了约 10k tick——那种失败看起来像「没有快照」，不像时钟错误。
* `demo-clock-check.sh:17-21`（A1 断言原文）：
  「snakewater playback 0 (paused, tick 0): entryTick=1, serverTick=10320, serverTickKnown=1.
   The first data packet does not stamp its own `svc_NetTick`, but the persistent networkTick is already valid
   (**the signon block carried 10320**), so the tick is KNOWN by carry-over」。
* `demo-clock-check.sh:404-409` 的 m1 mutation 把 `:408` 的 `'? firstPacketTick + g_playback.tick : -1;'`
  换成 `'? demoNetworkSummary.firstSignonEntryTick + g_playback.tick : -1;'`——那正是 T63-F1 之前的错误口径。

**所以 `+68 = 10320 - 10252` 是「signon entry tick → 同一时刻的 server tick」在 signon 点的偏移**，
是一个可复现的常数（7 次运行全 10320），不是启动竞态，也不是「首个有效快照漂移」。
交付把它写成后者，结论（改成 10320）不受影响，但解释必须换成这个，否则下次换 demo 会有人按「抖动」去调参。

### 1.3 裁决与代价

* **改前提**：`viewmodel-contract-check.sh:714` 改 `CAPTURE_SCENE_TICKS="10320"`；
  `:713` 注释改成「the server tick the signon block carries on the pinned demo (entry tick = 10252)」并点名 `demo-clock-check.sh`。
* **同一处还有一句过时注释**：`viewmodel-contract-check.sh:363-364`「The two ticks differ by 10252 on this demo」——
  现在差的是 10320，这句话本身就是 T63-F1 之前的口径，一起改。
* 代价：1 行常量 + 2 处注释。**判别力不变**：`10320` 是 demo 数据里的服务器 tick，
  只要 `scene: begin` 丢了、或产品又退回 entry-tick 口径，7 帧就不会再稳定等于 10320，断言照样转红。
  反过来说，「改前提会不会让断言永真」这个问题可以明确否定——它钉的是 tick 语义，不是某个启动巧合。

## 2. 红 2：Identity Skinning `1 px / max_delta=1`

### 2.1 证据（已核对）

* `viewmodel-contract-check.sh:828-829`：
  `bmp_diff "identity skinning does not deform the bind pose" "$TRACE_DIR/vm90.bmp" "$TRACE_DIR/vmident.bmp" --max-delta 0`
* `n18-contract-baseline.log.txt:207-215`：vmident `skinning=1 boneTableCoverage=45/45 animationInput=bones:45`，
  而 `BMP-DIFF differing=1 total=2582352 percent=0.0000 max_delta=1 extent=1264x681x24 status=FAIL reason=whole frame max_delta=1 exceeds 0`。

### 2.2 为什么只有这一组红

* `vm90` 走 `skinning=0`（直通顶点），`vmident` 走 `skinning=1`（45 块单位矩阵进 VS 做加权乘加）。
  两条不同的浮点指令流，在 258 万像素里恰好 1 个片元落在 UNORM8 量化边界上，差 1 LSB。
* 另外 4 组 `--max-delta 0` 不红，是因为它们两侧走的是**同一条**指令流：
  `vmfov10`/`vm40` 与 `vmfov200`/`vm120` 在 CPU 端已钳到同一 FOV；`novm`/`vmmissing` 都 `drawn=0`；
  `vm90`/`vmshort` 是拒收后回落 `skinning=0`。**只有 `:828` 把 0 蒙皮与 1 蒙皮硬比在 0 LSB 上。**

### 2.3 裁决与代价（含交付自报未决项的回答）

* 交付问「`bmp_diff` 能不能在 `--max-delta 1` 下同时叠占比上限」——**能**。
  `bmp-pixel-diff.py:19-20` 两个选项并列说明：`--max-delta N`（no channel may differ by more than N）、
  `--max-diff-percent P`（at most P% of bytes may differ）；`:223-231` 两个 bound **各自独立判定**，同时给才同时生效。
* 已有先例：`docs/reports/T34-NEARCLIP-2026-10-20.md:166` 与 `:310`、`docs/reports/T39-WEAPON-ANIMATION-2026-10-21.md:20`
  都在用 `--max-delta 1 --max-diff-percent 0.001`（`0.001% × 2,582,406 ≈ 25 px`）。
* 因此建议改 `viewmodel-contract-check.sh:828-829` 为
  `bmp_diff ... --max-delta 1 --max-diff-percent 0.001`，其余 4 组维持 `--max-delta 0`。
  代价：容忍「1 LSB 量化边界翻动」，但任何真实骨骼变形都会产生成片像素差，远超 25 px 的预算。

## 3. 状态与批准

两条都是**既有红**，修的是门禁自身的前提/阈值，不是产品行为。改门禁需要用户点头；
本条只把取证、根因、精确改法钉死，等批准后一次性改完并重跑 `viewmodel-contract-check.sh`。

* 待改 1：`viewmodel-contract-check.sh:714`（10252→10320）+ `:713` 注释 + `:363-364` 注释。
* 待改 2：`viewmodel-contract-check.sh:828-829`（`--max-delta 1 --max-diff-percent 0.001`）。
* 不要顺手改的：`:752/:754/:772/:851` 的 `--max-delta 0`，它们是「同一指令流」的对照组，放宽等于丢掉判别力。
