# N05 R1：逐 pass 深度回读——109 px 边界到底是哪条边界（2026-11-16）

状态：**已构建、已运行、读数完整**。结论一句话：在这帧的 1290 px 分析足迹里，深度写入只发生在
**1181 px**（首次且唯一一次写入发生在 **50-entities** pass），**109 px 从头到尾没有任何一个 pass
写过深度**；换句话说 M2b 的 109 px 边界就是"深度写覆盖率的补集"，是**写掩码边界**，不是深度测试
结果的边界。

## 1. 问题与仪器

M2b 只用帧末一张深度平面，能说"三角形没写深度"，但不能说**是哪一段没有写**。R1 增加
`--capture-pass-depth <prefix>`：在渲染帧的 10 个固定点各回读一次 D24S8 深度模板（mask
`& 0x00FFFFFF`，除以 16777215 归一化），写 `<prefix>-<index>-<name>.pgm`（P5 16-bit、big-endian、
值 = lround(clamp(depth,0,1) * 65535)），并在 stdout 打一行
`pass-depth: index=%d name=%s depthStateLastBind=%s width=%u height=%u bytes=%llu wrote=%d`。
dump 点（函数内名字）：`00-cleared` `10-sky` `20-world-batchNN-alphaM` `30-world-done`
`40-water`（该帧水面未绘制，故缺席）`50-entities` `60-overlay` `70-hud-pre`
`80-post-viewmodel`；同时每个点记录当时最后一次深度状态 bind 的标签，用来区分"没画"与"画了但
深度写关闭"。

代码：`native/include/native_renderer.h`（+27）、`native/src/main.cpp`（+45/−16）、
`native/src/native_renderer_capture.cpp`（+98）、`native/src/native_renderer_frame.cpp`（+35）；
分支 `work/card-maps-r1b`（基于 `0ada1d81`，代码内容与已合并的 `e6cf1f61` 相同）。带 prefix 但
缺 `--capture-frame`、或写了 0 张平面、或回读失败 ⇒ 进程 exit 16，不会静默产出一个"看起来正常"的帧。

## 2. 构建与三组对照

构建：`D:/_shared/EXEC-2026-11-15/INST2/m2c-r1/build-r1b.sh`（`tools/tf2-toolchain.sh` →
CMake NMake → nmake），`TOOLCHAIN=OK CONFIGURE=OK NMAKE=OK`。产物
`D:/TF2_Native_Worktrees/t105-e5-r1b/native/build-nmake/tf2_demo_native.exe`，
sha256 `bf76cdae3e68ff1f7bf29ddee565ad018105fe547c0ec3a74f518db3858dd919`，3,057,152 B。
（`strings -a` 找不到 `--capture-pass-depth` 是正常的：解析用的是宽字符串，要用
`strings -a -e l`。）

运行：`D:/_shared/EXEC-2026-11-15/INST2/m2c-r1/run-r1b.sh`，每 run 独立 75 s 左右，产品启动走
`_shared/hostlock.sh run card-maps`；输出 `…/m2c-r1/runs/`。冻结基线 exe
`…/m2b/exe-baseline/tf2_demo_native-e61c7fd9.exe`（sha
`e61c7fd9e5dba5167463f493937b23be799c96bcdd35bee7e0970d747a3bb078`）。

| 组 | exe | 开关 | rc | BMP sha256 | depth PGM sha256 | pass 平面 |
|---|---|---|---|---|---|---|
| `g1-baseline` | e61c7fd9 | mask + `--capture-depth` | 0 | `757f5aa2…b8b40` | `07f513c4…aa1834` | 0 |
| `g2-control` | bf76cdae | mask + `--capture-depth` | 0 | **`757f5aa2…b8b40`** | `07f513c4…aa1834` | 0 |
| `g3-passdepth` | bf76cdae | mask + depth + `--capture-pass-depth` | 0 | **`757f5aa2…b8b40`** | `07f513c4…aa1834` | **8** |
| `g4-plainframe` | bf76cdae | 常规（无 mask/三角形/no-cull），只 frame | 0 | `b194973c…e707a` | – | 0 |
| `g5-plaindepth` | bf76cdae | 常规 + `--capture-depth` | 0 | `b194973c…e707a` | `2babadb1…7632f` | 0 |
| `g6-plainpass` | bf76cdae | 常规 + depth + pass | 0 | `b194973c…e707a` | `2babadb1…7632f` | 14 |

四个结论直接可读：**新 exe 的仪器改动零渲染影响**（g1=g2=g3 帧、g2=g3 深度平面逐字节相同）；
**`--capture-depth` 本身不改帧**（g4=g5）；**`--capture-pass-depth` 也不改帧**（g4=g5=g6）；
A 帧 `757f5aa2` 在三种配置下都逐字节复现，BMP 2,582,406 B、帧 1264×681。

## 3. 逐 pass 读数（g3；表列 = 与上一 pass 相比的变化像素数）

足迹口径与 M2b 完全一致：从 trace 的 `clip-capture screen vertex` 取 A/B/C，只统计像素中心
`(x+0.5,y+0.5)` 严格在三角形内的 1290 px，再按颜色分 class：
`R1-FOOTPRINT centre_inside=1290 ribbon=109 interior=1181 closure=TRUE`；
A=(950.251,333.076) B=(906.165,433.139) C=(884.183,424.539)。

| pass | depthStateLastBind | 非 far px | d_frame_all | d_ribbon | d_interior |
|---|---|---|---|---|---|
| 0-`00-cleared` | frame-start:write-all | 0 | 0 | – | – |
| 1-`10-sky` | sky:write-off | 0 | 0 | 0 | 0 |
| 2-`20-world-batch00-alpha1` | world-blended:write-off | 0 | 0 | 0 | 0 |
| 3-`30-world-done` | world-blended:write-off | 0 | 0 | 0 | 0 |
| 4-`50-entities` | entity-restore:write-all | **223,766** | **223,766** | **0** | **1181** |
| 5-`60-overlay` | overlay-restore:write-all | 223,766 | 0 | 0 | 0 |
| 6-`70-hud-pre` | overlay-restore:write-all | 223,766 | 0 | 0 | 0 |
| 7-`80-post-viewmodel` | post-viewmodel:write-all | 223,766 | 0 | 0 | 0 |

`R1-FIRST-WRITE ribbon=never interior=4-50-entities(1181 px)`；
`R1-DEPTH-PLANE-EQUAL TRUE`（`70-hud-pre` 平面与 `--capture-depth` 平面逐字节相同 ⇒ 两个回读
路径一致，HUD/viewmodel 不写深度）；帧末非 far 像素 223,766（值 59314..65444）。

隔离帧里世界只画了 1 个 `alphaMode=1` 的 batch（trace：`world isolation requested=19539
stored=19539`、`indexCount=3 batches=8 firstBatchIndices=0 isolated=19539`、`batchDraws=1`），
而 alpha=1 的批次深度写关闭（`world-blended:write-off`），所以 0→3 号平面全清、世界阶段一个深度
都没写。这点与常规渲染路径同构：见 §4。

## 4. 常规渲染路径的 pass 结构（g6，14 张平面）

| pass | depthStateLastBind | 非 far px | changed |
|---|---|---|---|
| 0-`00-cleared` | frame-start:write-all | 0 | – |
| 1-`10-sky` | sky:write-off | 0 | 0 |
| 2-`20-world-batch00-alpha0` | world:write-all | 773,685 | 93,086 |
| 3-`20-world-batch01-alpha0` | world:write-all | 782,109 | 17,061 |
| 4-`20-world-batch02-alpha0` | world:write-all | 784,479 | 2,791 |
| 5..8-`20-world-batch03..06-alpha1` | world-blended:write-off | 784,479 | 0 |
| 9-`30-world-done` | world-blended:write-off | 784,479 | 0 |
| 10-`50-entities` | entity-restore:write-all | 795,937 | 11,458 |
| 11-`60-overlay` | overlay-restore:write-all | 795,937 | 0 |
| 12-`70-hud-pre` | overlay-restore:write-all | 795,937 | 0 |
| 13-`80-post-viewmodel` | post-viewmodel:write-all | 795,937 | 0 |

`--capture-depth` 平面（795,937 px）与 10/11/12/13 号平面**逐字节相同**（`ndiff=0`）⇒ 常规路径
里 50-entities 之后没有任何 pass 再写深度。3 个 alpha=0 批次写深度、4 个 alpha=1 批次不写——
与 §3 的 `world-blended:write-off` 互为印证。

## 5. 1181 px 的写入者长什么样（不依赖 pass 命名）

把 `50-entities` 平面在 interior 上的存值与三角形自身插值深度逐像素比（`compare-entities.py`）：

| 集合 | n | 存值 raw16 | 三角形 raw16 | delta=存值−三角形（raw16） | 相关 |
|---|---|---|---|---|---|
| interior | 1181 | 65044..65128 | 65016..65098 | min +4 / med +21 / max +112，mean 20.8，sd 5.0 | **0.9583** |
| ribbon | 109 | 65535（全 far） | 65019..65102 | +433..+516 | – |

即：写入 1181 px 的那个表面与三角形**几乎平行**（相关 0.958），整体比三角形**远约 21 个 quanta**
（1 quantum = 1.526e-05）。所以它不是"更近的遮挡物"；它更像同一个面/共面面的第二次绘制。具体是
50-entities 里的哪一次 range 绘制，当前仪器还没有逐 range trace（见 §7）。

## 6. 对 M2b 问题的回答

**M2b 的 109 px 边界 = 某个 pass 的深度写掩码边界。** 精确表述：

- 在 1290 px 分析足迹上，深度写入集合**恰好等于 interior 的 1181 px**，且只由 `50-entities`
  一次写入完成（`d_ribbon=0`，`d_interior=1181`）；
- 109 px 在整个帧的 8 个 dump 点上**始终是清除值**，没有任何 pass 写过它们；
- 因此这条边界不是"深度测试谁赢"的边界（没有人在这 109 px 上做过写入尝试），而是**"有没有片元
  覆盖 + 深度写是否打开"的边界**；在 pass 粒度上，它就是 `50-entities` 写掩码在足迹上的补集。

M2b 当时的 `INCONCLUSIVE` 是"帧末一张平面无法归因"的必然结果；R1 把它变成读数：
`R1-FIRST-WRITE ribbon=never interior=4-50-entities`。

## 7. 未证与下一步

1. **50-entities 里具体哪一次绘制写的那 1181 px 未证**（需要逐 range/逐 model 的 trace 或
   draw-call 计数）。但这不影响 §6 的边界结论。
2. **水面 pass 在该帧缺席**（`40-water` 没有平面、也没有 trace 行），所以"9-11 张平面"里的
   上界这一帧用不到；水面帧要另跑（改 `--capture-tick`）。
3. `--capture-depth` 与逐 pass 平面的一致性只在 mask 帧验证过（`R1-DEPTH-PLANE-EQUAL TRUE`）；
   常规帧的对应关系是"10 号之后的平面全等于帧末平面"，已由 g6 的 ndiff=0 覆盖。

## 8. 复现命令

```
# 构建（隔离 worktree，锁内）
cd /d/TF2_Native_Worktrees/t105-e5 && bash tools/lockrun.sh card-maps -- bash /d/_shared/EXEC-2026-11-15/INST2/m2c-r1/build-r1b.sh

# 三组对照（产品启动套 host 锁）
bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run card-maps bash /d/_shared/EXEC-2026-11-15/INST2/m2c-r1/r1b-groups.sh
bash /d/TF2_Native_Worktrees/_shared/hostlock.sh run card-maps bash /d/_shared/EXEC-2026-11-15/INST2/m2c-r1/r1b-plain.sh

# 分析
cd /d/_shared/EXEC-2026-11-15/INST2/m2c-r1 && python analyze-r1.py g3-passdepth
```

产物目录 `D:/_shared/EXEC-2026-11-15/INST2/m2c-r1/`：`runs/<tag>.{bmp,pgm,log,trace,run.txt}`、
`runs/<tag>-pass-*.pgm`、`r1-analysis-g3.txt`、`groups-mask.log`、`groups-plain.log`。
