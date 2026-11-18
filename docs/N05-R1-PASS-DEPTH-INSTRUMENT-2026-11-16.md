# N05 R1: per-pass 深度回读仪 —— 状态（2026-11-16）

状态：**代码已提交、从未构建成功**。构建、三组对照、逐 pass 读数全部待验；
本窗口按 lead 的 WINDOW-DEADLINE 裁定收工（verify 锁被 card-arch 的 E1 从 15:53 起连续持有，
lockrun 重试 60+10 次全部 `VERIFY-LOCK=FAIL`，没有一次进到编译）。

## 1. 目的

M2b 证明 109 px 上的深度是清除值、其余 1181 px 的可见表面比三角形更远，因此遮挡不成立；
但 M2b 读的是**帧末唯一一张**深度平面，说不清是哪一 pass 写的。R1 在同一帧里按 pass
dump 深度缓冲与深度写状态，把「109 px 边界是否恰好等于某个 pass 的写掩码边界」变成读数。

## 2. 实现（4 文件，+189/−16）

隔离 worktree `D:/TF2_Native_Worktrees/t105-e5-maps-r1`，分支 `work/card-maps-r1`，
基于 `997e945e`；提交 `e6cf1f61`。主树 exe 未动（`75dae10e…`，3,044,864 B）。

- `native/include/native_renderer.h`：`setPassDepthPrefix` / `passDepthWritten` /
  `lastPassDepthSucceeded` / `noteDepthState`；私有 `writeDepthPlanePgm` / `capturePassDepth`。
- `native/src/native_renderer_capture.cpp`：`writeWorldDepthToPgm` 拆成通用
  `writeDepthPlanePgm`（P5 16-bit，输出字节不变）+ 薄包装；新增 `capturePassDepth`，
  读 D24S8 到 staging、`(raw & 0x00FFFFFF)/16777215` 与 `--capture-depth` 同口径，
  写 `<prefix>-<index>-<name>.pgm`，并打印
  `pass-depth: index=… name=… depthStateLastBind=… width=… height=… bytes=… wrote=…`。
- `native/src/native_renderer_frame.cpp`：10 个 dump 点 —— `00-cleared`、`10-sky`、
  `20-world-batchNN-alpha<0|1>`（每个世界批次）、`30-world-done`、`40-water`、
  `50-entities`、`60-overlay`、`70-hud-pre`、`80-post-viewmodel`，并在每个
  `OMSetDepthStencilState` 处记 `frame-start:write-all` / `sky:write-off` /
  `world:write-all` / `world-blended:write-off` / `entity:write-all` /
  `entity-blended:write-off` / `overlay:own` / `hud:own` 等标签。
- `native/src/main.cpp`：`--capture-pass-depth <prefix>`；没有 `--capture-frame` 时按
  既有 rc=13 语义拒绝；没写出任何平面或读回失败并入 rc=16；summary 行加
  `pass_depth_written=%d`。

守卫：`capturePassDepth` 第一行 `if (passDepthPrefix_.empty() || capturePath_.empty()) return;`，
默认路径只付一次字符串判空。

## 3. 判据（下一窗口照此执行）

1. 构建通过，dump 点齐全（≥9 张 PGM），标签自洽；
2. 三组对照：baseline 旧 exe `e61c7fd9…`、新 exe 无新开关、新 exe 带新开关 ——
   **三组 BMP 都必须逐字节复现 M2b 的 A 帧 `757f5aa25a9ffded886e6290ec67deab292bd27532729a1bf453f1ee4b2b8b40`**
   （2,582,406 B），否则读数作废；
3. `70-hud-pre` 平面必须与 `--capture-depth` 平面逐字节相同（证明 dump 点没扰动）；
4. 逐 pass 比较 footprint（中心采样：ribbon 109 / interior 1181，闭合 1290）内首次写入的 pass，
   回答「边界是否等于某 pass 的写掩码边界」；若没有任何 pass 写过 ribbon，就如实写"不是任何
   pass 的写掩码边界"，与 M2b 的否证一致。

脚本已写未跑：`D:/_shared/EXEC-2026-11-15/INST2/m2c-r1/run-r1.sh`、
`D:/_shared/EXEC-2026-11-15/INST2/m2c-r1/analyze-r1.py`、`build-r1.sh`。
输出目录 `D:/_shared/EXEC-2026-11-15/INST2/m2c-r1/runs/`（共享目录，不写 tracked 目录）。

## 4. 未证

本文件不含任何实跑读数：仪器**没有构建过**，也没有跑过一帧带 `--capture-pass-depth` 的
capture。所以「深度仪可用」「109 px 边界等于某 pass 写掩码边界」都还没有证据。
