# N05 R2: 常规渲染路径是否复现 109 px 边界 —— 验证设计（2026-11-16）

状态：设计，未执行。本文只定义可执行验证与判据，不改任何代码、不产生读数。

## 1. 要回答的问题

M2a/M2b 的读数来自 **capture 隔离路径**：`--world-clip-capture --world-triangle 19539
--world-magenta` 只让三角形 19539 按 magenta 隔离色绘制，其余世界内容走哨兵色，且诊断隔离
会跳过实体标记与实体模型实例 pass。M2b 已证明在那一帧里 109 px 的深度是清除值 1.000000
（`raw16=65535`）、其余 1181 px 的可见表面比三角形更远 4..112 quanta，因此遮挡不成立。

R2 的问题不是"深度为什么输"，而是：**三角形在 1290 px 足迹上只覆盖 109 px 这件事，是常规
渲染路径本身就有的，还是隔离/捕获路径的产物？** 若为后者，则 N05 的边界结论只能约束捕获
路径，不能外推到播放器实际画面。

## 2. 输入（与 M2b 完全一致，便于逐字节对照）

- 产品：主树 `native/build-nmake/tf2_demo_native.exe`（本轮 sha256
  `75dae10e60740cd2c3e780ef68c72eb100e6f4221f8c234227a907a99d0607ab`）；若用 R1 构建，
  必须在报告里写明 sha 与该 exe 相对主树的关系。
- 录像：`E:/demo测试/cp_sultry_b8a/STV/f63a7a2040379d7d6a8414f76bbb81dc_matcha-20260703-1442-cp_sultry_b8a.dem`
  （85,506,321 B，sha256 `032bbd5ab719012cc2a7b4858f0a5d4e1ba882b4cdae1d86f2b966a3578c4374`）。
- TF root、`--audio-disabled --hidden-window --start-paused --capture-tick 0 --world-sky`、
  隔离设置（`verify-iso.sh` + `iso_seed_gate_settings`）、相机 `--debug-camera -600 -600 950 20 45`、
  `--world-face-trace --no-world-cull`。
- 输出目录：`D:/_shared/EXEC-2026-11-15/INST2/m2d-r2/runs/`（共享证据区，不写 tracked 目录，D71）。

## 3. 三组运行（每组只差一个开关）

| 组 | 隔离开关 | 捕获 | 作用 |
|---|---|---|---|
| N1 | **无** `--world-clip-capture --world-triangle --world-magenta` | `--capture-frame` + `--capture-depth` | 常规渲染路径的帧与帧末深度（被测量对象） |
| N2 | 有（= M2b 的 mask 配方） | `--capture-frame` + `--capture-depth` | 足迹来源与阳性对照：必须逐字节复现 M2b 的 A 帧 `757f5aa2…`（BMP 2,582,406 B） |
| N3 | **无** | `--capture-frame`（无 depth） | 证明深度目标不改产品像素：N3 与 N1 的 BMP **逐字节相同** |

各组重复运行一次（N1a/N1b），用于证明常规路径帧在相同输入下逐字节稳定；不稳定则不解读，
先查原因。运行仍走 `tools/lockrun.sh card-maps --`（主树锁）与 gate 的 host 锁，避免与他人
GPU 进程互斥。

## 4. 怎么在常规帧里找到三角形的覆盖像素（不需要新代码）

常规帧没有 magenta，但三角形有一个与它一一对应的判据：**它的深度平面**。M2b 已经从
`--world-face-trace` 读到隔离三角形的三个角
`A=(950.251,333.076,z=0.992067) B=(906.165,433.139,z=0.993167) C=(884.183,424.539,z=0.993397)`
（NDC 深度），因此"某像素的存储深度等于三角形平面在该像素的深度"就是"三角形在这一像素赢了
深度测试"的可判定判据（容差 `2q = 3.052e-05`，M2b 已标定：`1q = 1.526e-05`）。

足迹集合固定取自 N2 帧（与 M2b 同一份像素类别，中心采样口径）：ribbon（magenta）109 px、
interior（哨兵色 `(89,158,97)`）1181 px，闭合断言 `109+1181==1290`。

## 5. 读数与判据

对 N1 的 `--capture-depth` 平面，在 N2 的 1290 px 足迹上分别统计：

1. `T_interior` = interior 中"存储深度 ≈ 三角形平面深度（|delta| ≤ 2q）"的像素数；
2. `T_ribbon` = ribbon 中同一判据（≈三角形深度）的像素数；
3. `F_interior` = interior 中"存储深度严格更远（delta < -2q）"的像素数；
4. `C_ribbon` = ribbon 中"存储深度 = 清除值 1.000000"的像素数。

判据（预先写死，运行前不调）：

- **A. 边界在常规路径复现**：`T_interior = 0` 且 `T_ribbon = 109`。含义：常规帧里三角形同样只
  覆盖那 109 px，1181 px 的可见表面依旧比它远，边界不是隔离路径的产物。
- **B. 边界是隔离/捕获路径的产物**：`T_interior ≥ 1150`（≥ 97% 的 interior）。含义：常规帧里三角形
  正常写满 1290 px，隔离帧之所以只有 109 px 是隔离绘制本身造成的。
- **C. 混合/不成立**：其余取值（0 < T_interior < 1150），或 `T_ribbon` 与 `T_interior` 的合计与
  1290 差得离谱。此时如实报"边界在常规路径下不是二值的"，并给出逐像素 delta 分布，不做推断。

辅助读数（不作判据，只入档）：N1 与 N2 的帧末深度在"世界域像素"上的差（实体/overlay 像素先
用 N2 的哨兵色排除，理由见 M2b 的 D74 边界）；N1 的 ribbon 像素若 `C_ribbon = 109`，说明常规帧
里那 109 px 从未被任何 pass 写过深度 —— 与 M2b 的帧末读数一致，可直接对照。

## 6. 需要报的失败模式

- N2 没有逐字节复现 `757f5aa2…` ⇒ 阳性对照失败，整组读数作废，先查 exe/录像/设置身份。
- N3 与 N1 的 BMP 不同 ⇒ 深度目标影响了产品像素，N1 的深度读数不可用于"常规路径"结论。
- 深度文件缺失或 rc=16 ⇒ 记 `WINDOW-DEADLINE`/失败，不用推断替代读数。
- N1a/N1b 不同 ⇒ 常规路径非确定，先证明确定性再谈判据。

## 7. 为什么这个设计能回答问题

判据只依赖两件已经存在的东西：三角形自己的深度（`--world-face-trace` 的三角读数）和常规帧的
深度平面（`--capture-depth` 本来就会写）。它不需要在常规路径里给三角形染色，也不需要改渲染
代码，因此不会污染"常规路径"这个被测对象；而 N2 同时充当足迹来源与阳性对照，把"我们量的到底
是不是同一帧同一相机"钉住。

## 8. 执行结果（2026-11-16 追加，判据 C）

三组运行与 §3 一致，exe 为 R1 构建 `bf76cdae…`（隔离 worktree `t105-e5-r1b`），逐字节复现：

| 组 | 实际标签 | 开关 | BMP sha256 | depth PGM sha256 |
|---|---|---|---|---|
| N2（阳性对照） | `g3-passdepth` | mask + depth + pass | `757f5aa2…b8b40` ✓ 复现 | `07f513c4…aa1834` |
| N1 | `g5-plaindepth` | 常规 + depth | `b194973c…e707a` | `2babadb1…7632f` |
| N3 | `g4-plainframe` | 常规，只 frame | `b194973c…e707a`（= N1）✓ | – |

稳定性：N1 重复三次（g4/g5/g6）帧逐字节相同 ⇒ 常规路径在该帧确定。N2 复现 M2b 的 A 帧 ⇒ 足迹来源
可信。N3 = N1 ⇒ 深度目标不改产品像素。

在 N2 足迹（1290 px = ribbon 109 + interior 1181）上量 N1 的帧末深度，判据按 §5 预先写死：

```
R2-INTERIOR {'nearer': 1, 'farther': 1164, 'equal': 16}
R2-RIBBON   {'farther': 103, 'equal': 6}
R2-CRITERIA T_interior=16 T_ribbon=6 F_interior=1164 C_ribbon=0 (tol 2q=3.052e-05)
R2-VERDICT  C: mixed / not binary on the normal path
```

delta（=N1 存值 − 三角形自身插值深度，raw16，1 quantum = 1.526e-05）：
interior `min -8 / p5 16 / med 21 / p95 23 / max 69`；ribbon 109 px **全部非清除值**，
`0..70，med 34`（隔离帧里这 109 px 是 65535 清除值）。

读数含义（不做超出读数的推断）：

1. **"三角形不写深度"这一点在常规路径同样成立**：1290 px 里只有 22 px（16+6）落在 2q 容差内，
   1267 px 严格更远、1 px 更近、0 px 清除。N1 的 interior 中位数 +21 与隔离帧的 +21 完全一致，
   说明写入这些像素的表面在两条路径里是同一个。
2. **但 109/1181 的二值边界在常规帧里不出现**：常规帧里 ribbon 那 109 px 被背景几何覆盖（+34），
   不是清除值；判据 A（`T_interior=0 且 T_ribbon=109`）与 B（`T_interior≥1150`）都不成立，
   按 §5 落 **C**。
3. 结合 R1（`docs/N05-R1-PASS-DEPTH-RESULT-2026-11-16.md`）的逐 pass 读数：隔离帧里 1181 px 的
   深度由 `50-entities` 一次写入、109 px 从未被写；常规帧里同位置的 +21 场与隔离帧一致，而 109 px
   只是被更远的背景填上。所以 M2b 的边界应读作**写/覆盖掩码的边界**，不是遮挡边界；这一点不依赖
   隔离路径。

### 8.1 最小区分实验（设计，未执行）

要一次性证伪"109 px 是光栅覆盖边界"，只需让隔离三角形的绘制**打开深度写**（或用一个不透明变体
重画同一 index 缓冲），再 dump 同一帧：

- 若写入像素**恰为 1181**（= interior）、109 px 仍为清除值 ⇒ 覆盖边界成立（109 px 根本没有片元，
  即使打开深度写也写不到）；
- 若写入 **1290** ⇒ 覆盖不是原因，边界来自绘制时的深度测试/写状态，需要再查该 draw 的 depth-stencil
  state；
- 若写入 0 ⇒ 该 draw 的深度写被整体禁用，与覆盖无关，边界结论退回 M2b 的 `INCONCLUSIVE`。

配套（不必须先做）：在 entity range 循环里加逐 range 的 `indexCount/depthState` trace，把 R1 §5 里
"写入 1181 px 的那次绘制"从 `50-entities` 收窄到具体 range。

