# N05 第二步：109 px 的深度归因 - 2026-11-16

- 问题：N05 第一步量到隔离三角形 19539 只画出 109 个洋红像素（其余被别的表面盖住）。本轮用既有 `--capture-depth` 回读同一帧的深度平面，问一句：**这 1181 个像素是不是"被更近的表面在深度测试里挡掉"的**。
- 结论：**不是。** 该帧深度缓冲在该三角形 1290 px 投影范围内**没有一处记录到它自己的深度**（109 px 恰好是清除值 1.000000），而 1181 px 上可见的表面在深度上比它**更远** 4..112 个 quanta。所以"更近的遮挡物在深度测试里赢了"这条假设被否证；109 px 的边界是**绘制顺序/深度写状态**的边界，不是遮挡边界。
- 仪器：既有 `--capture-depth`（`native/src/native_renderer_capture.cpp:46-78` 写 P5、16 位大端、行自上而下、1.0=清除远平面），**零代码改动**，不动 `native/src/main.cpp`、不加开关、不改产品路径。

## 1 仪器与不变量

| 项 | 值 |
|---|---|
| exe（冻结副本，实际执行） | `D:/_shared/EXEC-2026-11-15/INST2/m2b/exe-baseline/tf2_demo_native-e61c7fd9.exe` |
| exe sha256 | `e61c7fd9e5dba5167463f493937b23be799c96bcdd35bee7e0970d747a3bb078` |
| exe 字节/mtime | 3,025,408 / 2026-11-15 22:58:31（运行前后一致） |
| demo sha256 | `032bbd5ab719012cc2a7b4858f0a5d4e1ba882b4cdae1d86f2b966a3578c4374`（85,506,321 B） |
| HEAD | `a6e85f98` |
| tick / 相机 | `--capture-tick 0`、`--debug-camera -600 -600 950 20 45`、`--world-sky` |
| 帧几何 | 1264×681（BMP 2,582,406 B；PGM 1,721,568 B 深度样本） |
| 深度的采样点 | 世界/实体 pass 之后、`drawViewModel()` 清深度之前（`native/src/native_renderer_frame.cpp:1257-1264`） |

注意：仓库里 `native/build-nmake/tf2_demo_native.exe` 现在是别的卡重编的 `26bff436…`，本轮**没有**用它；上表是冻结副本，运行前后 sha 都核过。

## 2 A/B 对照（唯一的硬门）

| 轮 | 参数差异 | BMP sha256 | rc | tick |
|---|---|---|---|---|
| A 掩码+深度 | `--world-magenta --no-world-cull ... --capture-frame` + `--capture-depth` | `757f5aa25a9ffded886e6290ec67deab292bd27532729a1bf453f1ee4b2b8b40` | 0 | 0 |
| B 掩码对照 | 与 A 完全相同，**只去掉** `--capture-depth` | `757f5aa2…`（同上） | 0 | 0 |

- `cmp -s A B` → **相同**：打开深度目标没有改动任何一个产品像素。
- `cmp -s A` 对 M2a 参考帧 `.scratch/n05-clip-capture/tri19539-magenta.bmp` → **相同**：同一配方逐字节复现了第一步的掩码帧。
- A 的深度输出门：`capture_depth_written=1 capture_depth_bytes=1721568`（=1264×681×2），PGM sha256 `07f513c4868ad9bffcd1dc10c5d4047170c12b8cc92c35680c53ee8684aa1834`。
- 若 A/B 不同则停止，本轮一切结论都作废（未发生）。

## 3 像素口径：两种都写

采样必须是**像素中心**（`(x+0.5, y+0.5)`）：D3D 光栅器的采样点就是像素中心，三角形"内部"= 三个边函数在采样点上同号，用整数角点采样会漏掉/多算边界像素。

```
centre sampling (本轮与后续一律用这个)
  interior = 1290   magenta(ribbon) = 109   flat green(interior) = 1181
  ASSERT 109 + 1181 == 1290            -> TRUE
  ASSERT 内部没有第三种颜色             -> TRUE (只有 (255,0,255) 与 (89,158,97))
integer-corner sampling (M2a 旧口径)
  interior = 1288   magenta = 85   green = 1152   ASSERT 85 + 1152 == 1288 -> FALSE (缺 51 px)
```

所以第一步文档里的 **85 / 1152** 是"整数角点采样"的等价量，**不是光栅器自己的采样点**；第一步的"1152 是内部纯绿"实际上是 view8 掩码在整数采样下的洋红数。本报告所有判定只用中心采样的 **109 / 1181**。

## 4 深度读数

三角形三个顶点的屏幕坐标与 GPU 深度（来自同一帧 trace 的 `world clip-capture`）：

| 顶点 | x | y | depth |
|---|---|---|---|
| 0 | 950.251 | 333.076 | 0.992067 |
| 1 | 906.165 | 433.139 | 0.993167 |
| 2 | 884.183 | 424.539 | 0.993397 |

每像素的三角形自身深度 `d_tri` 由三顶点在屏幕空间重心插值（NDC 深度在屏幕空间仿射）。判定容差 2 quanta = 2/65535 = **3.052e-05**。

| 类 | 像素 | 缓冲里存的深度 min/median/max (ndc) | raw16 | 该类 `d_tri` min/median/max | delta = d_tri − stored min/median/max | |delta|≤2q | delta>2q | delta<−2q |
|---|---|---|---|---|---|---|---|---|
| RIBBON（洋红） | 109 | 1.000000 / 1.000000 / 1.000000 | 65535（全部=清除值） | 0.992131 / 0.993248 / 0.993393 | −0.007869 / −0.006752 / −0.006607 | **0/109** | 0 | 109 |
| INTERIOR（绿） | 1181 | 0.992508 / 0.993225 / 0.993790 | 65044 / 65091 / 65128 | 0.992084 / 0.992900 / 0.993328 | −0.001705 / −0.000321 / −0.000062 | **0/1181** | 0 | 1181 |

- 整帧：抽 17,738 像素，74.0% 是清除远平面；可见表面最浅 0.934371。
- 两个方向都要读，才能区分"被挡住"和"根本没写"：
  - **RIBBON**：缓冲里是**清除值**，不是近似等于三角形深度（0/109 落在 2q 内），也不是更近的表面。即：隔离三角形的这次绘制在这 109 px 上**一个深度样本都没写**。
  - **INTERIOR**：缓冲里是比三角形**更远**的表面（1181/1181 delta<−2q，距离 4..112 quanta），**没有任何像素更近**。既然深度测试是 `LESS_EQUAL`、写入 `ALL`，一个更远的表面不可能在深度上赢过它——只可能是因为**三角形没写深度**，那个更远的表面是对着清除值通过的。

## 5 判定

先按约定的判据逐条读：

| 判据要求（"被更近的表面挡掉"） | 实测 | 结论 |
|---|---|---|
| ribbon 的 `|delta|` ≤ 2q（缓冲里就是三角形自己的深度，且输给了更近者） | 0/109 | **不成立** |
| interior 的 delta 明显大于 2q（被更近的表面覆盖） | 0/1181，且方向相反（更远） | **不成立** |

- **判据的两条分支都不成立，而且都在同一个方向上失效**：缓冲里没有任何比该三角形更近的表面。所以"被更近的遮挡物在深度测试里挡掉"这条假设**被否证**。
- 判据本身预设了"三角形的片元进了深度缓冲"，而实测说它**没进**（ribbon 精确等于清除值）。这是判据没覆盖的第三种情况，本轮据实记为：**109 px 的边界不是深度遮挡边界**。

## 6 三条分立结论

1. **顶点变换是对的（与 M2a 一致）**：同帧 `control compared=3 matched=3 worstRelative=3.955e-07 threshold=1.0e-05 verdict=agree`——CPU 预测与 GPU 流出的顶点在 1e-5 相对误差内一致。所以 109 px 不是顶点/投影/裁剪算错。
2. **109 px 不是深度测试失败（本轮新增，可复现）**：109 px 上该三角形的这次绘制没有写深度；1181 px 上可见的表面比它更远。产物帧与掩码帧逐字节可比（A/B 相同），因此这不是采集副作用。
3. **覆盖那 1181 px 的表面属于世界域（world），不是玩家**：诊断隔离帧会跳过实体标记（`native/src/native_renderer_frame.cpp:705`）和实体模型实例 pass（`native/src/native_renderer_frame.cpp:737`），而这 1181 px 仍然被画成平坦哨兵色 (89,158,97)，说明是**世界域**的表面。遮挡物类别仍只能记 **{static prop, map model}**（D74），**本轮不点名**到具体模型/道具。

## 7 机制：候选与判别（未证明）

- **边界**：本实验只覆盖 **capture 隔离路径**（`--world-triangle` 掩码帧 + `--capture-depth`）；**常规渲染路径没有测过**，本节机制候选保持「未证明」标签不变。

已确定的约束：隔离范围的这次绘制**以深度写关闭**的状态绘制。证据链——
- 状态对象：`native/src/native_renderer.cpp:708-712` `worldBlendedDepthStencilState_` = DepthEnable TRUE / **WRITE_MASK_ZERO** / LESS_EQUAL；非透明用 `native/src/native_renderer.cpp:692-696`（WRITE_MASK_ALL）。
- 逐批次绑定：`native/src/native_renderer_frame.cpp:822` `OMSetDepthStencilState(range.translucent ? worldBlendedDepthStencilState_ : depthStencilState_, 0)`。
- 本帧台账（trace）：`gpu: world alpha frame blendedTriangles=1376 cutoutTriangles=32 blendedBatches=1 blendState=1 depthWriteOff=1`；隔离绘制 `world isolation indexCount=3 batches=8 firstBatchIndices=0 isolated=19539` / `world draw branch batchesIndexed=1 indexCount=3 batchDrawCalls=1 isolated=19539`。

未定的只有"那 1181 px 是谁画的"。候选（都不点名到具体几何）：
- **A. 世界批次里更晚的非透明批次**：在先画的隔离范围之后绘制，深度写 ALL、对清除值通过，写入 0.9932 并涂哨兵色。
- **B. 水面 pass**：水在世界批次之后绘制（`native/src/native_renderer_frame.cpp:586-699`，ordinal 2），**该段没有任何 `OMSetDepthStencilState`**，继承前面恢复的 `depthStencilState_`（`native/src/native_renderer_frame.cpp:550`）——即写深度。
- 贴边形状（ribbon 是 1..7 px 宽、贴着三角形一条边的细边）更像 A 的"同一笔刷另一面稍微靠后"，而不是一整块水面把三角形切一刀；但本轮没有能分辨二者的数据。

判别实验（属于下一步，不是本轮）：给世界/水面/实体各 pass 之间各回读一次深度（per-pass depth readback），或对水位开关单独出一次掩码帧；同时把隔离绘制的 `OM` 深度状态在 trace 里打出来（而不是只打 `depthWriteOff=1` 这个全局布尔）。这需要 instrument（改代码），按纪律不在本轮的"零代码改动"范围内。

## 8 复现

```
bash tools/lockrun.sh card-maps -- bash /d/_shared/EXEC-2026-11-15/INST2/m2b/m2b-session.sh
python D:/_shared/EXEC-2026-11-15/INST2/m2b/depth-classify.py \
  D:/_shared/EXEC-2026-11-15/INST2/m2b/runs/tri19539-mask-depth.trace \
  D:/_shared/EXEC-2026-11-15/INST2/m2b/runs/tri19539-mask-depth.bmp \
  D:/_shared/EXEC-2026-11-15/INST2/m2b/runs/tri19539-mask-depth.pgm
```

分析器在运行输出里自己打印 `M2B-CLASSES`（=109/1181）、`M2B-CLOSURE-ASSERT`（109+1181==1290 且无第三色）、两向 `M2B-VERDICT` 与唯一一行 `M2B-CRITERION`，并自带两个自检（有遮挡合成平面 → OCCLUDED；无遮挡合成平面 → NOT-DEPTH）。

## 9 证据

| 内容 | 路径 |
|---|---|
| A 帧 / 深度 / trace / 日志 | `D:/_shared/EXEC-2026-11-15/INST2/m2b/runs/tri19539-mask-depth.{bmp,pgm,trace,log}` |
| B 帧 | `D:/_shared/EXEC-2026-11-15/INST2/m2b/runs/tri19539-mask-control.bmp` |
| 每轮证据（sha、rc、tick、深度门） | `runs/tri19539-mask-depth.run.txt`、`runs/tri19539-mask-control.run.txt` |
| 会话门（A/B cmp） | `D:/_shared/EXEC-2026-11-15/INST2/m2b/M2B-SESSION.run.txt` |
| 锁记录 | `D:/_shared/EXEC-2026-11-15/LOCK-NOTES/card-maps.txt`（ACQUIRED/RELEASED，owner=card-maps） |
| 分析器与脚本 | `D:/_shared/EXEC-2026-11-15/INST2/m2b/{depth-classify.py,m2b-depth-control.sh,m2b-session.sh}` |
| M2a 参考帧（逐字节同 A） | `.scratch/n05-clip-capture/tri19539-magenta.bmp` |
