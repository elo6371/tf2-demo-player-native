# N27 范围未决项：视频裁剪/导出、PIP 四视角、LOD1+

- 日期：2026-11-15
- 依据：`docs/PROJECT-NEXT-TASKS-2026-11-14.md:332-336`（N27 卡面自述）
- 旧编号：D35（范围未决，P3）—— 见 `docs/DEFECT-REGISTER-MASTER-2026-11-14.md:63`；T55 在 `docs/T85-COVERAGE-MANIFEST.md:85` 记为 UNSUPPORTED
- 本文件性质：**给产品负责人的裁决材料**。它给出现状事实与候选方案的工作分解，**不给虚假工期**。本文件不是实现授权，也不是"已完成"或"已排除"的声明。
- HEAD `6dd853e`；本文件不跑产品、不改代码。

## 一、现状事实（能查到出处的）

### 1.1 产品现在能做到什么

| 能力 | 现状 | 出处 |
|---|---|---|
| 单帧 BMP 落盘 | **有**，一次性诊断：`--capture-frame <path>` + `--capture-tick <N>` | 解析 `native/src/main.cpp:3151-3152`、`:3160`；一次性语义 `native/src/main.cpp:9917-9920`；tick 钉住 `native/src/main.cpp:10029-10030`；调用 `native/src/main.cpp:12743` |
| 深度图 | 有（与 capture-frame 绑死，缺 capture-frame 直接 rc 拒绝） | `native/src/main.cpp:3245-3252` |
| 帧序列导出 | **没有** | 全树无 `capture-sequence` / 逐帧循环落盘入口；`requestFrameCapture` 只接一次路径（`native/src/native_renderer_capture.cpp:30`） |
| 视频编码 | **没有** | `native/` 源码内 `MFCreateSinkWriter` / `IMFSinkWriter` / `avcodec` / `libx264` 命中 **0**；`ffmpeg` 的 54 处命中**全部**落在 CMake 配置日志（`native/build/CMakeFiles/CMakeConfigureLog.yaml` 等），是构建期搜索路径不是产品代码 |
| 音频文件输出 | **没有** | `native/` 内 `sf_open` / `sf_writef_short` / `dr_wav` 命中 0；音频只有 WinMM `waveOutOpen` 播放路径（每个事件一次，`native/src/audio_player.cpp:202`） |
| 确定性逐 tick 渲染 | **没有** | `--playback-speed` 只改每帧推进多少 tick（`native/src/main.cpp:5977-5981` 的 `std::clamp(playbackSpeed, 0.125, 64.0)`），**不产生"每 tick 一帧"**；帧数由 swap 节奏决定 |

### 1.2 这台机器上有外部编码器

`native/build/CMakeFiles/CMakeConfigureLog.yaml:70` 记录 CMake 搜索到 `C:\Users\Administrator\AppData\Local\Microsoft\WinGet\Packages\Gyan.FFmpeg.Shared_...` —— **外部 ffmpeg 存在**，可作为不链接进产品的后处理工具。

## 二、T55「视频裁剪/导出」：三个候选，不含工期承诺

卡面要求写清五件事：输入区间、音画同步、格式/编码、输出失败/取消/磁盘不足恢复、依赖与工期。逐项对应如下。

### 方案 A（最小可交付）：帧序列 + 外部 ffmpeg 合成

**做法**：产品新增 `--export-range <startTick>:<endTick>:<dir>`，在区间内**每 N tick 渲染一帧写 BMP**（复用 `requestFrameCapture` 的 BMP 写盘与现成的 tick 钉住逻辑），**不做编码、不做音频**；用户用 ffmpeg 把序列合成 mp4。

- 输入区间：区间用 tick 表示（与 `--capture-tick` 同一坐标系），另给 `--export-fps` 决定每多少 tick 一帧。
- 音画同步：**首版不做**（音频不进导出物）。必须在文档与 UI 里写明"无音轨"，否则用户会以为导出掉了声音。
- 格式/编码：BMP 序列（无损、体积大）+ 用户侧 ffmpeg 命令模板。
- 失败/取消/磁盘不足：写盘失败必须**非零退出并留最后一步的错误串**；不许沿用 `> file` 那种"启动即截断"的形态（D52 已登记）。
- 依赖：无新库。**工期：未估**（估它需要先做 §4 的探针）。

**成本驱动（不是数字，是杠杆）**：BMP 体积 = 宽×高×3。本仓库已有的一帧是 1264×681 = 2,582,406 B（`D:/_shared/EXEC-2026-11-15/n10-capture/sky_on/sky_on.bmp` 实测）。30 秒 60 fps = 1,800 帧 ≈ **4.6 GB**。所以 A 方案的第一版必须带"输出目录容量上限与预检"，否则磁盘不足是必然事件而不是异常。

### 方案 B（A + 音轨）：再把音频时间轴离线渲染成 WAV

**做法**：在 A 的基础上，把已记账的音频事件表（`native/src/main.cpp:5985-6046` 的 `audio: timeline`）按 tick→秒换算离线渲染成单轨 WAV，再让 ffmpeg 混流。

- 音画同步：成立的前提是"音频事件的 tick 与导出的帧 tick 用同一个时钟"。这一点**未验**：音频事件现在由 `native/src/audio_player.cpp:390-479 advance()` 在**播放时**调度，不是离线渲染器。
- 额外风险：音频资源解析有六档 `SoundResolveStatus`（`native/include/audio_timeline.h:42-55`），离线渲染必须把"缺资源"与"离线不支持"分开报，否则导出物缺声音时无法解释（与 N21 缺陷一同源）。
- **工期：未估。**

### 方案 C（全内置编码）：接 Media Foundation / 内置编码器

- 代价：新增库依赖或 MF 会话管理、色彩/时间基转换、编码失败恢复。**与 N22（工具链便携）和 N29（干净环境走查）直接冲突**：多一个必须随包分发的依赖，就多一条安装失败路径。
- 建议：**除非产品负责人明确要求"零外部工具"，否则不选**。理由不是贵，是它会改变发布物的形态。

## 三、建议（供裁决，不是决定）

1. 若"能拿到一段可发给别人看的视频"是首发需求的最低形态 → **选 A**，并**明确写出"无音轨"**。
2. 若"导出必须带声音"是硬需求 → **A + B**，但先做一次**可行性探针**（见 §5），因为离线音频渲染的时钟一致性未验。
3. 若要求"用户不装任何外部工具" → **C**，并且必须同时把 N29 的干净环境项扩成"带编码器依赖的安装/卸载/失败恢复"。

## 四、估工期前必须先测的三件事（否则任何数字都是编的）

1. **一帧的确定性渲染耗时**：现有一帧落盘走的是 `requestFrameCapture` 的同步读回（`native/src/native_renderer_capture.cpp`），**读回开销未测**；且它是否可用于连续多帧（还是只能一次）**未验**。
2. **渲染是否与墙钟耦合**：`--playback-speed` 只改"每帧几个 tick"，若要"每 tick 一帧"必须新增一套步进循环；这套循环会不会打破现有 66.6666667 tick/s 的播放语义（`native/src/playback_state.h:26`）**未验**。
3. **BMP 写盘带宽**：4.6 GB 量级的顺序写在这台机器上要多久，**未测**。

## 五、PIP / 四视角 与 LOD1+ 的里程碑声明（N27 第三条）

- **PIP / 四视角**：**未实现，且不并入 N16 分屏第一片**。N16 的契约（`docs/N16-SPLITVIEW-CONTRACT-2026-11-16.md`）只覆盖"两个等分窗同帧渲染"，四视角是小地图/多路视角的独立能力，需要独立的容量测量（当前产品**没有任何 GPU 计时能力**：`native/` 下 `ID3D11Query` / `TIMESTAMP` / `QueryVideoMemoryInfo` / `DXGI_QUERY_VIDEO_MEMORY_INFO` 命中全 0，这条 lead 已亲自复核）。→ 记**后续里程碑**，不记"本轮排除"。
- **LOD1+**：资产侧只有 LOD0（`docs/N17-N20-CHAR-COVERAGE-2026-11-15.md`：`viewmodel_range_probe --all-lods` 37 个模型全 `resolution=found-vpk`，LOD1+ 无数据）。→ 记**后续里程碑**，理由写"资产只有 LOD0"，不写"不需要"。

## 六、请产品负责人裁决的三项

1. 首发是否包含视频导出？（不包含 = D35 转为"明确排除"，必须在路线图里写；包含 = 在 A / A+B / C 里择一）
2. 若包含，**是否接受"首版无音轨"**？
3. 是否接受"必须自备 ffmpeg"这一前置条件？


## 七、用户裁决（2026-11-14 20:36）与它把方案钉到了哪

用户原话：「首发包含视频导出，不接受没音轨，不允许自备 ffmpeg」。三条同时成立，候选集只剩一个：

| 要求 | 排除掉的 | 保留的 |
| --- | --- | --- |
| 首发必须包含视频导出 | 「先只做帧序列、导出留到后续里程碑」 | 本次必须有导出 |
| 不接受没音轨 | 方案 A（帧序列 + 外部 ffmpeg，首版无音轨） | 必须有音频通路 |
| 不允许自备 ffmpeg | 方案 A 与 A+B 的合成端（本机 `C:\Users\Administrator\AppData\Local\Microsoft\WinGet\Packages\Gyan.FFmpeg.Shared_...` 那套不能作为交付前提） | 编码必须在产品内完成 |

**结论：走方案 C（内置 Media Foundation）+ 方案 B 的音频离线混流。** 即：产品自己用 `IMFSinkWriter` 出 MP4，视频 H.264、音频 AAC，音频由产品自己把事件时间轴离线混成 PCM 再交给 SinkWriter，全程不依赖任何外部可执行文件。

### 8.1 这个决定带来的三个硬约束（不是工期，是正确性）

1. **音频必须能离线合成，不能只靠播放期调度。** 今天音频事件只在 `native/src/audio_player.cpp:390-479 advance()` 的播放循环里被消费，且输出端是 WinMM 实时写（`native/src/audio_player.cpp:155-209 play()` 每个事件一次 `waveOutOpen`）。导出路径要的是「按 tick 算出的采样偏移」，与墙钟无关。**这是新代码，不是复用。**
2. **确定性与实时必须分开。** 导出时不能靠 `elapsed` 走时钟：同一份 demo 两次导出必须逐字节一致（否则 QA 无从判定）。实现上要用固定步长推进（每帧 1/fps，且 tick 由 playback ledger 按同一速率推），并把 `--capture-tick` 那套「钳住 tick」的语义扩展到每一帧。
3. **干净环境不能假设 Codec Pack。** H.264 编码器与 AAC 编码器在 Windows 10/11 上是系统自带 MFT，但「本机有」不等于「干净环境有」。**必须先探测再实现**（见 8.2），探测不到就不能答应这条需求。

### 8.2 第一步不是写编码器，是探测能力（**已做**，2026-11-14 20:48）

探针 `native/tools/mf_export_probe.cpp` + `.sh`（`cl.exe` 直编，未改 `native/CMakeLists.txt`，未启动产品），报告 `D:/_shared/EXEC-2026-11-15/LEAD/mf-export-probe.md`（306 行，逐条 HRESULT/GUID/ffprobe 原文）：

| 问题 | 实测（真机） | 判 |
|---|---|---|
| H.264 编码器存在？ | 19 个视频编码器 MFT 里 **4 条输出 H.264**：Intel QSV `{4BE8D3C0-0515-4A37-AD55-E4BAE19AF471}`（两条）、NVIDIA `{60F44560-5A20-4857-BFEF-D29773CB8040}`、微软软件 `H264 Encoder MFT` `{6CA50344-051A-4DED-9779-A43305165E35}`。输入子类型**没拿到**：硬件 MFT `0xC00D6D77 MF_E_TRANSFORM_ASYNC_LOCKED`，软件 MFT `0xC00D36E6 MF_E_ATTRIBUTENOTFOUND` —— 记为未获得，不是缺陷 | 是 |
| AAC 编码器存在？ | `count=1`，`Microsoft AAC Audio Encoder MFT` `{93AF0C51-2275-45D2-A35B-F2BA21CAED00}`，输入子类型枚举成功（PCM/Float 共 64 条） | 是 |
| MP4 容器 + 双流？ | `MFTranscodeContainerType_MPEG4` 下 `AddStream(H.264)` 与 `AddStream(AAC)` 均 `S_OK`，`Finalize S_OK` | 是 |
| RGB32 被接受？ | RGB32 / NV12 / YUY2 三种全部 `SetInputMediaType S_OK` —— **不必自写 NV12 转换** | 是 |
| 独立回读？ | `MFCreateSourceReaderFromURL` 回读 2 流、首帧解码成功（`bytes=1382400` = 1280×720×1.5 NV12）；**逐流 `MF_PD_DURATION` 返回 0.000 s**，逐流时长只能用第三方 ffprobe 核对（video 2.000 / audio 2.005313）—— ffprobe 是 QA 工具，**不是产品依赖** | 是（一处空缺） |
| 可分离性（组件方案的前提） | 独立进程从 60 张 BMP + 1 个 WAV 出 239,516 B MP4；`dumpbin /dependents` 只有 `MFPlat.DLL` / `MFReadWrite.dll` / `ole32.dll` / `KERNEL32.dll` | 是 |

**四条没有一条为否 → 方案 C 的实现形状不变**：不需要自写 NV12、不需要退回 AVI/MKV、不需要 ffmpeg。成片样片 `sample_720p_2s.mp4` = 207,556 B，h264 Main 1280×720@30（`nb_frames=60`，722,776 bps）+ aac LC 48 kHz 立体声（`nb_frames=94`，97,221 bps）；音频不是静音（`mean_volume: -15.1 dB`、`max_volume: -11.5 dB`，与设计正弦 RMS −15.06 dB 吻合）；抽第 0 帧肉眼核对渐变方向、白条位置（x=0、rows 120–600）全对，无上下颠倒/BGR 互换。吞吐：100 帧 1280×720 = **308 ms（3.08 ms/帧）**，外推 1800 帧（30 s@60 fps）≈ 4.3–5.5 s，**仅编码**，不含每帧渲染与混音。

### 8.3 这次裁决没有改变的事

- PIP / 四视角、LOD1+ 仍在后续里程碑（§5），不因为「首发含导出」而进本批。
- N22 工具链便携、N29 干净环境两条约束仍然有效：方案 C 正是为了不与它们冲突才被选，**如果探测发现系统不带 H.264/AAC，冲突就回来了**，那时要回到用户重新裁决，而不是偷偷退回方案 A。

### 8.4 探测带出来的两条裁决（lead，2026-11-14 20:52）

**一、B 帧 / profile：默认不写 `MF_MT_MPEG2_PROFILE`。** 探针实测两条路 —— 显式写 `Main` 会启用 B 帧，**视频起始晚 1 帧（33.3 ms）**、容器 2.0333 s，而音频仍从 0 开始（音画起点差 33.3 ms）；不写该属性（默认 Constrained Baseline，`has_b_frames=0`）则起始 **0.000000**、容器 2.005313 s，代价是同内容大约 **15% 体积**（239,516 vs 207,556 B）。裁决理由：这是一条「让人看回放」的导出，**音画起点对齐比文件大小重要**，2 秒样片上 15% 不过 32 KB。第三条路（自建 topology 设 `CODECAPI_AVEncMPVDefaultBPictureCount=0`）本探针**没验**；若将来体积成硬指标，先测它再谈改判。**任何 profile 变更必须同时重跑音画起点检查。**

**二、文件 sha256 不能当导出确定性的判据。** 同一秒内两次编码逐字节相同（sha256 `02ef1a59…`）；跨秒比差 **10 个字节**，全在 `moov` 内（205,167–206,722），值 `0x15`→`0x16`，对应 `mvhd/tkhd/mdhd` 的 `creation_time`/`modification_time`（ffprobe 证实 `2026-11-14T12:44:05Z` vs `…:06Z`）。裁决：导出 QA 的确定性断言必须是「**解码后逐帧像素 sha 相同 + 帧数相同 + 音频采样数相同**」；容器时间戳允许不同，但报告里要显式写成「预期不同」，不许写成「不稳定」。若将来要文件级可比，做法是把这 10 字节归零（探针未验）。

## 八、本文件没有证明什么

- **没有估工期**。§2 的"未估"是因为 §4 三件事一次都没测过，不是保守措辞。
- **已测编码吞吐**（3.08 ms/帧，§8.2），但**仍未测**每帧渲染 / 读回 / 写盘耗时，也未测 60 fps 1080p 与长片的编码内存。
- **音频离线渲染的时钟一致性仍未验证**：§8.2 只证明容器能装 AAC 流、能装正弦波，**不证明**产品能按 tick 算出正确的采样偏移（音频事件今天只存在于播放期调度里）。
- **干净环境 / N 版 Windows 未验**：H.264 与 AAC 由系统自带 MFT 提供只在这台机器上验过；sink 实际选了哪个编码器 MFT 也未验（探针没读回选中项的 CLSID）。
- **A/V 漂移未验**：只测了 2 秒样片的起点与容器时长，没有测 30 秒以上音画是否同步。
- **ffmpeg 在这台机器上的可用性仍未验证** —— 方案 C 不需要它，所以也没有用它作交叉核对。
- 不含任何"导出功能已实现/已排除"的结论 —— 这两件事只能由产品负责人裁决后写。
