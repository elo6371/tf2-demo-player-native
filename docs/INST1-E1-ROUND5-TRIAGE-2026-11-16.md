# E1 第 5 轮 triage（2026-11-16，exe 75dae10e）

窗口 15:10 起，card-arch 在共享工作树 `t105-e5` 上跑 viewmodel-material / minimap / map-geometry /
companion 四件事。本文只记录**实测读数与分流**，不改判据、不把红改绿。

## 1 身份

| 项 | 值 |
| --- | --- |
| 产品 exe sha256 | `75dae10e60740cd2c3e780ef68c72eb100e6f4221f8c234227a907a99d0607ab` |
| 大小 / mtime | 3,044,864 B / 2026-11-16 14:14:35（第 4 轮 same-bytes 重建后未再变） |
| HEAD（本轮起） | `997e945e` |
| HEAD（收工） | `f71ff277`（我的 triage 提交 `f8315bc1` 在其上） |
| exe 树绑定（sidecar） | sourceTree `e8cabb154d47`、sourceContent `661a4bc5e988f57bbf2e0b0115a6cfecf0d2a2cb05ed911481cd2d13aa5fbe66`、contentBinding `carried`、buildUtc `2026-11-16T05:44:34Z`、dirtyFiles=9（7 tracked + 2 untracked） |
| 主树构建 | 本轮**不在主树构建**；exe 全程同 sha（收工再取仍同） |
| 锁 | verify-all 用 `tools/lockrun.sh`；产品/捕获用 `/d/TF2_Native_Worktrees/_shared/hostlock.sh` |

窗口中途 HEAD 移动过：`d325ff0f`/`35f8856a` 于 16:11:52、`f71ff277` 于 16:18:49，作者是 card-maps。
`git show --name-only` 核实这五个提交（含 lead 的 `71c983cb`/`997e945e`）只碰 `map-material-matrix.sh`、
`source-tv-coverage-check.sh`、`evidence/source-tv*` 与 `docs/*`，**没有一行 `native/` 或构建输入**。
本轮四门都是乘积行为类，不吃这些提交；本轮也未跑 release-rebuild / userinfo，所以没有任何 identity 红。
下一窗口若见到 identity 不一致或 STALE，先按「窗口中途有脚本+docs 提交」定性，不要改判据、不要动 exe、
不要 revert 别人的提交。

## 2 门表

| 门 | 结果 | 耗时 | 关键读数 |
| --- | --- | --- | --- |
| viewmodel-material | **FAIL**（分段跑） | §1–8 1100 s（§9 前被上限截断）+ §9 单跑 | run A: 202 OK / 7 FAIL；`VIEWMODEL_MATERIAL_SECTIONS=9` 单跑: 9 OK / 3 FAIL |
| minimap-check | **PASS** | 2 s | 27 assertions；ledger cp_snakewater_final1 scale=0.078293204308、pl_badwater=0.130932897329 |
| minimap-hud | **FAIL** | 285 s | 96 assertions / 3 failed（A1、A6、A7） |
| map-geometry cp_snakewater_final1 | **p1 PASS**，p2 WINDOW-DEADLINE | p1 ~11 min，p2 超窗 | p1: 24 OK / 0 FAIL，re-pin 写入 frame-pins-p1 + cull-reference-p1（重写后 git diff 为空） |
| viewmodel-companion | NOT RUN | — | WINDOW-DEADLINE |

未跑的四张图（pl_odyssey / cp_sunshine / koth_bagel_rc12 / koth_bagel_rc11）同样记 WINDOW-DEADLINE。

## 3 红分流

### 3.1 viewmodel-material（产品路径数值，不是门禁算术）

§7 the binding is what changes the picture：

```
FAIL flat control draws the fallback = 3, expected 1
FAIL flat control uploads no texture = 2, expected 0
```

§8 a slot that does not resolve falls back, and says so：

```
FAIL nothing is bound = 2, expected 0
FAIL no texture was uploaded for it = 5, expected 0
FAIL no texture bind happened = 2, expected 0
FAIL the pass still draws the geometry = 3, expected 1
BMP-DIFF differing=7272 total=2582352 percent=0.2816 status=FAIL reason=whole frame percent=0.2816 exceeds 0.0000
FAIL the fallback frame is exactly the flat control frame (rc=1)
```

§9 the alpha path（`VIEWMODEL_MATERIAL_SECTIONS=9` 单跑）：

```
FAIL one range uses the alpha blend state = 3, expected 1
FAIL alpha is the only blend path applied = 3, expected 1
FAIL the bound slot is the glass one = uploadSlot=-2, expected it to contain uploadSlot=2
       path=models/weapons/v_models/v_sapper_spy.mdl slots=4 resolved=4 decoded=4 uploads=4 drawnSlot=0
```

判读：在 75dae10e 上，flat/fallback 路径不再产生 flat control 帧（画 3 个几何、2 次纹理上传），
alpha 路径有 3 个 range 走 alpha blend（钉值 1）。这两组都是产品路径的数值，不是门禁算术；
未改判据，交材料/alpha 语义 owner 重定口径。上一轮 OK 的两条 pin（`:689`/`:691`
`the pass built one range = 2` / `the range has a texture bound = 2`）本轮仍 OK。

### 3.2 minimap-hud（A1 与 A6/A7）

```
FAIL the panel handed over thirteen shapes = 1243, expected 13
     lw=ok floor=21566 faces=3480 kept=18061 fkept=2084 bands=6 runs=2332 rects=1230 drawn=4
FAIL the unflipped position is inside the panel (so this is not vacuous) = 15, expected >= 20
FAIL the mirrored position is inside the panel (so this is not vacuous) = 8, expected >= 20
```

- A1 的 1243 = `13 + 1230`（rects 并入），与 lead 记的「产品 13+1230=1243 / 13+815=828」同一口径；
  重算期望 rects 632/707（0a1aad40 后几何/材质计数变化），差 598/108 未证明是模型差，**不钉**。
- A6/A7 的 vacuity 守卫前提（面板内背景像素 >= 20）被 U3a 基底层填充破坏：实测 15 与 8，
  同组 `nothing is drawn` 仍 = 0。属语义变化，与 A1 一起交 card-ui 重定。
- A12 跳过（`skipped_controls=1`：needs `--mutation`，会构建；按主树不构建约束跳过）。

### 3.3 map-geometry（cp_snakewater_final1）

p1 PASS：24 OK / 0 FAIL。读数：

```
kept face-id pixels = 425899
cull removed = 3467 px from 16 faces (front=710 back=2757); frame cull_only=2510 pvs_only=0 union=2510
dark=64589 (back=3469 front=58679) attributable=7968 countsTotal=64589 frame=860784
product-attributable = 0.9257% of the frame (<= 2%)
```

re-pin（带理由，写进 pin 头）：0a1aad40 修 minimap floor 材质大小写、8eb6c085 修 oracle plane
stride 16→20 B，两者都改变了帧与 cull 参考背后的几何/材质口径；本轮按实测重钉
`evidence/t62-map-matrix/cp_snakewater_final1/frame-pins-p1.txt` 与 `cull-reference-p1.txt`，
不是把红改绿。实测：re-pin 后 `frame-pins-p1.txt` 与 `cull-reference-p1.txt` 被重写，但
`git diff` 为空（与已提交参考逐字节相同），所以本轮没有参考变更要提交。

p2：10/11 帧后到本窗口上限（620 s 被 TERM），无最终裁决；run 改写的 `frame-pins-p2.txt`
（11 条 sha 变化）没有 cull-reference-p2 与裁决背书，按未验证参考还原，记 WINDOW-DEADLINE。

## 4 门禁缺陷（本轮新发现，需单独修）

1. **`--reuse` 会假通过**。走到 reuse 分支后 bash 报
   `map-geometry-lighting-check.sh: command substitution: line 336: unexpected EOF while looking for matching ')'`，
   随后主循环提前结束并打印
   `MAP-GEOMETRY-LIGHTING=PASS maps=0 geometry=0 lighting=0 failed=0`
   —— **零张图却 PASS**。本轮 p1/p2 因此全部走新捕获（不用 `--reuse`）；修好前不要在汇报里引用 `--reuse` 的 PASS。
2. **hostlock 被超时 kill 后会留 stale owner**（owner=t62-mapaxes、product_processes=0、heartbeat 600 s+），
   必须 `hostlock.sh break t62-mapaxes`；死掉的 waiter 票还会卡在队列头（本次删
   `_shared/queue/<ticket>` 才放行）。kill 后先 break、再看 queue。
3. **门禁写 tracked evidence**：`evidence/t62-map-axes` 的 readings 与 `evidence/t62-map-matrix` 的
   status/readings 在运行中变脏（lockrun 报 D40/D49），收工需还原甲供输出、只提交 re-pin 的参考文件。

## 5 窗口与未完成

- 15:10–16:20 一个窗口：material 用满 1100 s 只到 §8/§9 边界（§9 另跑一次补齐），minimap 两门完成，
  map-geometry 只完成 snakewater p1，p2 与其余四图超窗，companion 未开。
- companion 待办不变：从 `E:/demo测试/pl_upward_f12/POV/autorecord_2026-08-08_21-43-59.dem`
  （sha256 `14b4943a8d7e866677091bb5a4843882bfed1105bc5ef27d955bbc4cd3ad00e8`）重导 witness，
  要求 observations >= 1000、witness ticks derived >= 3；上轮 36 FAIL/43 OK 属结构性红，断言未动。
- t2-visual-matrix 三红与 A1 口径维持第 4 轮分流，待语义 owner；本轮未复跑。

## 6 复现

```bash
# material（整门；分段补跑 §9）
bash viewmodel-material-check.sh
VIEWMODEL_MATERIAL_SECTIONS=9 bash viewmodel-material-check.sh

# minimap（SCRATCH/OUT 必须 /d/ 形式，D:/ 会被拼成 /d/.../D:/...）
bash minimap-check.sh
MINIMAP_CHECK_SCRATCH=/d/_shared/EXEC-2026-11-15/INST1/E1-round5/minimap-hud-scratch \
MINIMAP_CHECK_OUT=/d/_shared/EXEC-2026-11-15/INST1/E1-round5/minimap-hud \
MINIMAP_CHECK_DEMO_SUNSHINE="E:/demo测试/cp_sunshine/STV/SUNSHINE.dem" \
  bash minimap-hud-check.sh

# map-geometry（勿用 --reuse，见 §4.1）
bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run t62-mapaxes env HOLD_LOCK=1 MAP_AXIS_OUT=evidence/t62-map-axes \
  bash map-geometry-lighting-check.sh --only=cp_snakewater_final1 --poses=p1 --re-pin="<why>"
```
