# N27 — 产品内导出入口设计（方案，未实现）

- 日期：2026-11-16　实例3 / card-char　worktree：`D:/TF2_Native_Worktrees/t105-e5-n27a`（`work/card-char-n27a`，base `997e945e`）
- **状态：方案。没有写任何产品代码，没有重建产品 exe。** 主树产品 exe `native/build-nmake/tf2_demo_native.exe` = `75dae10e60740cd2c3e780ef68c72eb100e6f4221f8c234227a907a99d0607ab`（3,044,864 B，mtime 2026-11-16 14:14:35）本文件落盘前后未动。
- 本文件回答 lead 的问题：导出入口放哪、数据流与状态归属怎么定、失败模式有哪些、在不动主树 exe 前提下怎么验证。**不构成「导出已可用」的声明。**

## 1. 现状（代码事实，全部带位置）

| 事实 | 位置 |
|---|---|
| 产品已有**单帧**按 tick 的确定性渲染入口：`--capture-frame <bmp>` + `--capture-tick N` | 选项 `native/src/main.cpp:2884`、`:2920-2921`；解析 `:4096-4107`；校验 `:4181-4189` |
| 该入口命中后只写**一帧**就结束：`captureArmed` 是一次性闸刀，armed 后立刻清 | `native/src/main.cpp:11241`、`:14173-14176`（`captureArmed = false`）、收尾 `:14272-14317` |
| 帧由主循环按墙钟节流后调用 `renderer.draw()` 画出 | `native/src/main.cpp:11281`（`while (running)`）、`:11285`（PeekMessage）、`:14151`（`elapsed >= kTargetFrameSeconds`）、`:14234`（`renderer.draw(clearR,clearG,clearB)`） |
| 帧落盘是**后台缓冲的 BMP**（24 位，1264×681，2,582,406 B），另有深度 readback；都不是编码 | `native/src/native_renderer_capture.cpp:1`（文件头注释）、`:27`（`requestFrameCapture`）、`:88`（`writeBackBufferToFile`）；声明 `native/include/native_renderer.h:1631-1633`、`:1667` |
| 产品**没有任何 MF/编码封装代码**：`IMF|SinkWriter|MFStartup|MFCreate|mfplat|mfreadwrite` 在 `native/src` 命中 **0** | 本卡 grep（2026-11-16） |
| 整条编码链只在探针里：dump/encode/normalize/decode + verify 脚本 | `native/tools/mf_export_probe.cpp`、`native/tools/mf_export_verify.sh`（task A，commit `ca3fa348`） |
| 音频是 **waveOut 实时路径**，调度器按 tick 推进；离线 PCM 工具已存在（`applyPcmPan`；scheduler 有可注入的 outcome sink 缝） | `native/include/audio_player.h:18-21`、`:185`；产品配置 `native/src/main.cpp:7193`、`:7201`、`:7209`、`:7247`；推进 `:11318-11327`、`:11370` |
| **draw 节奏与 tick 节奏本来就是两套时钟**：draw 目标 ~8.3 ms（墙钟），tick 是 demo 的 tickRate（~66.7 Hz），同一个 `elapsed` 驱动；负载一重就每十 tick 才画一次 | `native/src/main.cpp:11340-11349` 注释与 `:11350-11353` 的 capture-hold |

第 9 条是导出必须先解决的结构问题：**如果导出复用「墙钟决定画哪一帧」，导出的帧集合取决于机器负载**。现有 `--capture-tick` 的 hold 只保证「捕获那一帧的 tick 准确」，不能保证「逐帧连续」。

## 2. 入口选择：CLI 子模式 `--export-video <path>`

选 CLI，不选菜单/HUD：

1. 现有全部证据都是 CLI 驱动出来的（`--capture-frame`/`--capture-tick`/`--start-paused`/`--settings-file`/相机脚本），导出要能被同一套 shell 闸门无头复跑；菜单路径无法回归。
2. 参数解析器已经对未知旗标/缺值做硬拒绝（`:4181-4189` 一带），新入口天然继承这套拒绝语义，不需要 UI 事件循环改造。
3. 出口要在 CI 里断言「同一输入两次导出逐字节相同」，CLI 是最短路径。

草案旗标（实现时以解析器风格为准）：

| 旗标 | 含义 | 默认 |
|---|---|---|
| `--export-video <path>` | 打开导出模式，写 mp4 | 关 |
| `--export-start-tick N` | 首帧 tick（含） | demo 起点 |
| `--export-frame-count N` | 导出帧数，0 = 到 demo 结束 | 0 |
| `--export-fps N` | 时间基与帧率 | 60 |
| `--export-size WxH` | 导出分辨率，**必须偶数**（H.264 约束；现有捕获画布 1264×681 是奇数高，不能直接沿用） | 1280x720 |
| `--export-audio on\|off` | 是否混音轨 | on |
| `--export-dump-inputs <dir>` | 验证用：把喂给 writer 的每帧 RGB24 与 PCM 落盘（`frames.rgb24`/`pcm.raw`，与探针同名同格式） | 关 |

互斥：`--export-video` 与 `--capture-frame`/`--camera-script` 同给 = 参数错误退出（不静默挑一个）。理由：三条都抢 draw 时钟与同一个 capture 槽，静默择一会让证据不可解释。

## 3. 数据流与状态归属

**单线程同步导出**，不新开渲染线程：

```
for i in 0..frameCount-1:
    tick = startTick + ticksForFrame(i)        // 由固定时间基换算，不看墙钟
    playback.evaluate(tick)                     // 复用现有 tick-change 求值（相机/观察目标/粒子/声音调度）
    renderer.draw(clear)                        // 同一个 draw，不另写离屏渲染器
    rgb24 = renderer.readBackToRgb24()          // 复用 capture 的 staging readback
    sink.writeVideo(rgb24, ts = i / fps)
    pcm = audioMixer.pull(samplesForFrame(i))   // 绝对 48 kHz 采样钟，由 tick 推进驱动
    sink.writeAudio(pcm, ts = i / fps)
```

| 状态 | 归属 | 说明 |
|---|---|---|
| tick / 相机 / 观察目标 / 粒子 | 现有 `g_playback` + spectate 块（`main.cpp:11233-11236`） | 导出期间只由导出循环写；不允许用户的键鼠输入改 |
| 设备、交换链、readback 缓冲 | Renderer（`native_renderer_capture.cpp`） | 新增内存出口，与 BMP 出口共用同一 readback，避免两套像素路径 |
| writer、两条 track 配置 | 新 `ExportSink`（新 TU `native/src/demo_export.cpp`） | 同步写；MF 内部自有线程，不再叠一层 |
| WAV 资源与混音 | 现有 `AudioEventScheduler` + `applyPcmPan`（`audio_player.h:18-21`、`:185`） | 导出时**不打开音频设备**：进程计数器 `audioDeviceOpenAttempts()` 必须仍为 0（`audio_player.h:29-30`） |

**为什么复用主循环而不是新写离屏渲染器**：现有像素闸门已经证明 `renderer.draw` 在同一输入上逐字节可复现（D75 的 C90/D40flag/E120flag 三次独立运行同一 sha `442489af…`）。新写一条渲染路径等于新增一条必须重新锚定的像素通路，且会与产品的 HUD/材质/trace 语义漂移。

## 4. 失败模式

| 症状 | 检测点 | 处置 |
|---|---|---|
| demo 读不出 / 无视频轨 | 现有 demo 解析失败路径 | 沿用产品既有退出码；trace 一行 `export: result=fail detail=no-video` |
| 无音频资源 / 全静音 | mixer 产出 0 样本 | 写**视频单轨**并在 trace 明说 `audio=absent`；不伪造静音轨 |
| `tickRate` 或 fps 非法 | 参数校验 | 拒绝（不 clamp 后继续） |
| 帧数/尺寸为奇数 | 参数校验 | 拒绝（H.264 偶数约束） |
| MF 初始化/SinkWriter 创建失败 | `MFCreateSinkWriterFromURL`/`SetInputMediaType` 的 HRESULT | 退出码 + trace 带 HRESULT 十六进制；先删临时文件 |
| 写盘中途失败（盘满/权限） | `WriteSample`/`Finalize` 返回值 | 退出码 + 保留临时文件路径在 trace；**rename 只在成功后做** |
| 用户中断（WM_QUIT / Ctrl+C） | 主循环 `running=false` | 收尾 Finalize，保留已写帧，退出码非 0，trace 写 `frames=` |
| 目标文件已存在 | 打开前检查 | 默认拒绝覆盖（避免证据互相覆盖）；显式 `--export-overwrite` 才覆盖 |

## 5. 不动主树 exe 的验证方案

**构建隔离**：只在 worktree 里 `bash build-target.sh tf2_demo_native`（产品目标在 `native/cmake/tf2-product.cmake:16-17`，链接行 `:157`）。构建与运行前后各记一次主树 exe sha256，必须恒为 `75dae10e…`。

**验证链（全部复用 task A 已有的判据，不新造阈值）**：

1. 导出 60 帧：snakewater `bb841c6d…`，`--export-start-tick 2000 --export-frame-count 60 --export-size 1280x720 --export-audio on`。
2. `--export-dump-inputs` 落 `frames.rgb24`（60 × 2,764,800 B）与 `pcm.raw`，用 `native/tools/mf_export_verify.sh` 同款断言：
   - video dump sha 与 task A 探针 dump 的 `6e592af18c31afb5feb9c5f7986735cabc4dc640e0b6ac218f060d54345719b6` 一致（同 demo/窗口/设置下产品渲染 == 探针输入）；
   - pcm dump sha 与 `c2126a45d484b296ff6e12809b1d4d32f8652b9442402673aa27568599c48663` 一致；
   - 产品 mp4 用探针 `--normalize` 归零后，时间戳归零字节数 = 40（5 box × 2 字段），`--decode` 回 60/60 帧 sha 命中、音频样本数差 ≤ 2,048；
   - 两次独立导出 normalize 后逐字节相同（探针 task A 的负控读数：raw 差 10 B/20 B 是时间戳低字节，normalize 后同 sha `428abe60…`）。
3. 负控（证明校验器会红）：导出后把 dump 的 `frames.rgb24` 删 1 帧 / 把 pcm 截 512 样本，重跑 verify 必须 FAIL；不修改产品代码。

基线：探针 sha `c66b15b1…`、verify 脚本、60 帧读数都来自 task A（`commit ca3fa348`），本轮**不重跑**，只作为验收判据来源。

## 6. 改动面（预计）

| 文件 | 改动 |
|---|---|
| `native/include/native_renderer.h`（`:1667` 区） | 新增 `readBackBufferToRgb24(std::vector<uint8_t>&, int& w, int& h)`，与 `writeBackBufferToFile` 共用 readback |
| `native/src/native_renderer_capture.cpp:88` 附近 | 把 BMP 写盘拆成「readback → 内存」+「内存 → BMP」两段，BMP 出口行为不变 |
| `native/src/main.cpp:2884`、`:4096-4107`、`:4181-4189` | 新旗标声明/解析/校验 |
| `native/src/main.cpp:11241`、`:14151-14176` | 导出模式：每循环恰好一帧、tick 由固定时间基给，绕开 `elapsed` 墙钟节流 |
| 新 `native/src/demo_export.cpp` + `native/include/demo_export.h` | `ExportSink`：SinkWriter 封装、两条 track、临时文件 + rename；编码核心从 `native/tools/mf_export_probe.cpp` 提取共用，避免两套 writer 漂移 |
| `native/cmake/tf2-product.cmake:16-17`、`:157` | 源文件加进 `add_executable`；链接行追加 `mfplat mfreadwrite mfuuid`（`ole32` 已在） |
| 音频侧（`audio_player.h` 或新 TU） | 导出用的离线 pull：复用 `applyPcmPan` 与 scheduler 的 outcome sink 缝（`main.cpp:7201`），**不打开设备** |

**不选**的替代方案：导出 BMP 序列再调外部 ffmpeg 封装。产品目前不依赖 ffmpeg；多进程会让「同一输入两次导出逐字节相同」多一个外部变量，且探针已证明进程内 writer 可行。

## 7. 不声称

- **未实现、未构建、未运行**；本文所有产品行为都是对现有代码的阅读结论。
- 未验证 MF 在**产品进程**内的初始化/清理顺序（探针进程里验证过，不等于产品里已验证）。
- 未测 >2 秒长片、连续多小时导出的内存/句柄曲线；未测 60 fps 下 writer 吞吐是否掉帧；未测多 demo。
- 未测 `--export-size` 改变客户区后，现有 HUD 矩形解析（`main.cpp:14172` `hudApplyRects`）是否仍给出同样的 HUD 像素。

## 8. 参考读数（来自 task A，本卡不重跑）

- 60 帧 × 2,764,800 B（1280×720×RGB24）；`pcm.raw` 384,000 B = 96,000 帧（48 kHz/16-bit/立体声，2 s）。
- encode：framesRead=60、frameShaMatches=60、pcmShaMatch=yes、videoSamplesWritten=60、audioSamplesWritten=94、bytes=207,556。
- normalize：fieldsZeroed=5、bytesZeroed=40、traks=2、verify_nonzero_fields=0。
- decode：视频 60/60 meanAbsDiff 1.958 / max 2.129；音频 96,256 vs 96,000（Δ256 ≤ 2,048）pcmSimilar=yes。
