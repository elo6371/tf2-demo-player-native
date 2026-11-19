# viewmodel-contract-check 基线（2026-11-19，N18 门闩修复前）

这份基线回答一个问题：**N18 的两道门闩还没修之前，这条门禁在这棵树上读到什么**。
答案是两条既有失败，数字与 2026-11-18 记录的两次读数完全一致 —— 不是新回归，是这条门禁自己欠的账。

## 跑法

```
# hostlock owner: n18-baseline；日志 /d/_shared/n18-contract-baseline.log
bash viewmodel-contract-check.sh        # 不带 --mutation，不改源码
```

- 树：`work/integrate` @ `e5b33781`（2026-11-19 15:30–15:37，约 6.5 分钟）
- job `bash-410d6ddaf43c4176`；跑完 `LOCK FREE`、`product_processes=0`
- 门禁用 `BUILD_TOKEN` 决定帧是否复用，所以 16 张帧全部由本次构建产生，不是别的构建留下的
- 完整日志随本目录入库（`n18-contract-baseline.log.txt`，290 行）

## 结论

`assertions_ok=152`、**`VIEWMODEL-CONTRACT=FAIL`、rc=1**。只有两条 FAIL，其余全绿。

### 失败 1：门禁拿不到它要的采样点（log:146-148）

```
  ..   capture ticks: 0 0 0 0 0 0 0
  ..   capture sceneTicks: 10320 10320 10320 10320 10320 10320 10320  (measured points: 10252)
  FAIL all 7 compared frames came from a measured capture point = 0, expected 7
```

trace 里 `draw: begin tick=0`、`scene: begin tick=0 sceneTick=10320`（`vm90.trace:519/643`、
`novm.trace:515/636`、`vmident.trace:519/643`），而门禁的 premise 是 `CAPTURE_SCENE_TICKS="10252"`
（`viewmodel-contract-check.sh:714`，注释说 measured on the pinned demo）。观测值 10320 与常数差 68，
7 帧一致 —— 不是抓帧时机抖动，是 premise 与今天的 `sceneTick` 对不上。

### 失败 2：恒等蒙皮的负控差 1 级（log:205-215）

```
  BMP-DIFF differing=1 total=2582352 percent=0.0000 max_delta=1 extent=1264x681x24 status=FAIL reason=whole frame max_delta=1 exceeds 0
  FAIL identity skinning does not deform the bind pose (rc=1)
```

`vmident` 的其它读数都对：`skinning=1`、`boneTableCoverage=45/45`、`animationInput=bones:45`、
`reason=drawn`。差的是 2,582,352 个像素里的 **1 个**：坐标 `(658,416)`，
bind pose `(117,131,154)` vs identity skinning `(117,132,154)` —— G 通道 1 个 LSB，典型浮点舍入边界。
断言是 `bmp_diff ... --max-delta 0`（`viewmodel-contract-check.sh:832`），即要求逐字节相同。

## 与 2026-11-18 读数对照

| 断言 | `979c6904`（N18 之前） | `5bac7eaa`（N18 之后） | `e5b33781`（本次） |
|---|---|---|---|
| all 7 compared frames came from a measured capture point | 0 ≠ 7 | 0 ≠ 7 | 0 ≠ 7（sceneTicks 全 10320） |
| identity skinning does not deform the bind pose | 1 px / max_delta=1 | 1 px / max_delta=1 | 1 px / max_delta=1（(658,416)） |
| assertions_ok | 152 | 152 | 152 |

前两列读数是 `evidence/n18-gate-health/` 入库的摘录，结论与本次逐项一致。
`docs/N17-BRANCH-AUDIT-2026-11-18.md` §8 已经把这两条记为「这条门禁自己欠的账，需要单独一张卡」。

## 本次跑绿的关键读数（供门闩改动做前后对照）

- section 1 `viewmodel_contract_probe`：`contractOk=true`、`meshVertices=23169`、`attachments=5`
  （5/5 valid）、`defaultFov=90`、`legacyFovDefault=80`→`migratedLegacyFov=90`、`alignmentIsIdentity=true`。
- 像素：有/无武器 `differing=26119`（1.0114%）、FOV 40/90 `215713`（8.3534%）、
  FOV 90/120 `32907`（1.2743%）、左右手 `35956`（1.3924%）。
- clamp 到投影：`--viewmodel-fov 10` 与 40、`--viewmodel-fov 200` 与 120 **differing=0**。
- 15 张有读数的帧各恰好 1 条 `boneTableCoverage`（section 7）。

## 关于冻结见证（`evidence/viewmodel/`）

门禁默认 `OUT=evidence/viewmodel`，每次跑都会重写那里的 `readings.txt` 与 8 张 PNG。
仓库里存的那份是 **2026-10-11** 的：`154` 条断言、`vertices=9723`、`viewModelPass=6`、
`meshRadius=15.78`、PNG 23–34 KB；今天这次是 `152` 条、`vertices=23169`、`viewModelPass=8`、
`meshRadius=24.98`、PNG 553–617 KB。

本次跑完把被改写的见证**还原**了（`git checkout -- evidence/viewmodel`），没有提交一份红的
`readings.txt`（`failures: 1`）当「冻结见证」。见证过期和「跑门禁会脏工作区」两件事都属于这条门禁的卡，
不在本基线里顺手改。

## 不宣称

- 单次运行；「1 个像素差 1 级」只说明本次构建上如此，没有额外测渲染确定性的重复次数。
  两个不同树的读数（上表）一致，能说明它不是 N18 引入的，不能说明它在每台机器上都是这 1 个像素。
- 完整 trace 与 BMP 是机器输出（`evidence/fast/viewmodel/`，每个 2.5 MB），没有入库；需要时用同一命令重跑。
- 这是基线，不是修复：两条 FAIL 仍然开着。
