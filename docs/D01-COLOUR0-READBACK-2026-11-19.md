# D01 的 849 / 919：COLOR0 回读与当前树的复现结论 — 2026-11-19

这份文件是 L3 的交付（`docs/TASK-ALLOCATION-2026-11-19.md` §2）。它回答两个问题，
并且只回答这两个：

1. D01 说的"顶点色缺失"成立吗？—— **不成立**：两个开口像素的 `colour.rgb` 今天分别读到 70 和 255。
2. 归档里那个 0,0,0 今天还能拍到吗？—— **不能**：同一 recipe 下产品帧在 849 读到 70,70,70。

因此 D01 不能再按"缺 COLOR0"追；要么它在 2026-11-14 之后被顺带修掉，要么当年那张帧
来自另一个播放状态。两者都需要一个**绑定提交的复现器**才能区分，这不在本轮范围内。

## 1. 方法（不改产品代码）

两件工具，都用产品里**已经存在的**诊断视图，不新增开关、不重建：

| 脚本 | 视图 | 读出什么 |
|---|---|---|
| `tools/d01-colour0-readback.sh` | 16 / 17 | view16 = `float4(colour.r, colour.g, colour.a, blendWeight)`；view17 = `float4(albedo.r, sampled.r, sampledTexel.a, colour.a)` |
| `tools/d01-compose-probe.sh` | 0 / 13 / 14 / 15 | view0 = 产品路径（对照）；view13 = lightmap 探针；view14 = `albedo * lightingRgb`（产品将要写的颜色，未掺雾）；view15 = `R=diffuse, G=|Ng|, B=lighting/2` |

视图映射在读代码时核对过：`native/src/native_renderer_frame.cpp:1025-1063`（环境变量
`TF2_NATIVE_WORLD_VIEW` 覆盖 debug view）、`native/src/native_renderer_shaders.cpp:615-625`
（1.62 / 1.52 两个分支的通道语义）、`:191`（`sampledTexel`）、`:234`（`albedo = lerp(colour.rgb, sampled * colour.rgb, colour.a)`）。

recipe 与 `docs/BLACKPOINT-849-919-2026-11-14.md` §4 和 `world-black-account-check.sh` 完全一致，
所以三组帧可比：demo `cp_sultry_b8a` STV `f63a7a20…dem`、`--start-paused --capture-tick 0 --world-sky`、
`--debug-camera -600 -600 950 20 45`、隔离 settings/ui。帧尺寸 `1264×681 24bpp`，与归档帧同尺寸，
所以像素坐标可以直接比。

## 2. 读数一：COLOR0 在不在（`evidence/d01-colour0/readings.txt`）

| 像素 | view16 colour.rgb / colour.a | view17 albedo.r / sampled.r / texel.a | 说明 |
|---|---|---|---|
| 849,396 | 70,70 / 70 | 70 / 70 / 255 | OPEN（归档帧此处为 0,0,0） |
| 919,379 | 255,255 / 255 | 142 / 142 / 255 | OPEN（归档帧此处为 210,208,197） |
| 902,385 | 255,255 / 255 | 148 / 148 / 255 | 同面参照 |
| 873,366 | 57,59 / 57 | 57 / 57 / 255 | alpha=0 的对照 |

**COLOR0 今天不是 0。** 849 的 `colour.rgb` = 70（`colour.a` 也是 70），919 的 `colour.rgb` = 255。
view17 的自洽检查也过：B 通道（`colour.a`）与 view16 的 B 一致。

## 3. 读数二：算到产品输出之前是不是还是 0（`evidence/d01-compose/readings.txt`）

| 像素 | view0（产品） | view13（lightmap） | view14（albedo×lighting） | view15（diffuse / |Ng| / lighting/2） |
|---|---|---|---|---|
| 849,396 | **70,70,70** | 70,70,70 | 70,70,70 | 70,70,70 |
| 919,379 | **39,36,34** | 255,255,255 | 255,255,255 | 209,255,127 |
| 902,385 | 68,60,53 | 255,255,255 | 255,255,255 | 209,255,127 |
| 873,366 | 57,59,57 | 57,59,57 | 57,59,57 | 57,59,57 |

- 849：四个视图全等（70），**产品输出与合成值一致**，没有"先算后清"的零化步骤。
- 919：debug 各视图是 255/209，产品帧是 39,36,34 —— 该像素在最终输出前被**衰减**，不是清零。
- 归档帧对照：`/d/_shared/EXEC-2026-11-13/F1/black/out3/face.bmp`（1264×681 24bpp）
  在 849,396 = **0,0,0**，同目录 `albedo.bmp` 也是 0,0,0，而 `texel.bmp` = 84,83,81。
  也就是说 **2026-11-14 那棵树的 `colour.rgb` 乘子是 0**（`sampled * 0`），与今天读到的 70 不同。

## 4. 结论

1. **"D01 缺 COLOR0 回读"这句话已经过时**（`BLACKPOINT` 首屏的待办就是这么写的）：
   view16/17 今天可以不改产品地读到 COLOR0，849 读到 70、919 读到 255。
2. **"黑来自逐顶点 albedo 调制项"这个机制描述对 2026-11-14 的帧成立，对今天的树不成立**：
   同一 recipe、同一帧尺寸、同一像素，今天产品帧是 70,70,70 而不是 0,0,0。
3. 所以 D01 的**现象层**需要重新确证：要么给出一个今天仍能拍黑的复现器（绑定提交 +
   demo/tick/相机/settings 落盘），要么承认它已随其它改动消失。**在拿到复现器之前，
   继续按"vertex colour = 0"归因是在修一个当前不存在的 bug。**
4. 唯一的残余读数：919,379 在产品帧里比 debug 视图暗（39 vs 255），同面 902 是 68,60,53。
   这是衰减不是清零，与 873 那种 `alpha=0` 的已知解释同类，**列为待查，不是 D01 的证明**。

## 5. 复跑

```bash
cd <repo>
bash ../_shared/hostlock.sh run d01-colour0 bash tools/d01-colour0-readback.sh   # 约 2 分钟
bash ../_shared/hostlock.sh run d01-compose bash -c 'D01C_VIEWS="0 13 14 15" bash tools/d01-compose-probe.sh'
```

证据：`evidence/d01-colour0/readings.txt`、`evidence/d01-compose/readings.txt` 入库；
BMP 与 trace 在 `.scratch/d01-colour0`、`.scratch/d01-compose`（可再生）。
日志：`/d/_shared/d01-colour0.log`、`/d/_shared/d01-compose.log`、`/d/_shared/d01-compose2.log`。

## 6. 本文件不宣称什么

- 不宣称 D01 已修复：只宣称当前 HEAD 用该 recipe 拍不到 0,0,0。
- 不宣称 919 的 39,36,34 是缺陷；没有对照组。
- 不宣称当年的帧"拍错了"：归档 BMP 的像素读数（0,0,0 / albedo 0）已在本轮复核，文件是真的。
