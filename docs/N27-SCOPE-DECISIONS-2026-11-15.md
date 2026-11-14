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

## 七、本文件没有证明什么

- **没有估工期**。§2 的"未估"是因为 §4 三件事一次都没测过，不是保守措辞。
- **没有测过一帧的渲染/读回/写盘耗时**，也没有测过连续多帧是否可行。
- **没有验证过音频离线渲染的时钟一致性**（音频事件现在只存在于播放期调度里）。
- **没有验证 ffmpeg 在这台机器上的可用性**，只从 CMake 配置日志里读到它的搜索路径。
- 不含任何"导出功能已实现/已排除"的结论 —— 这两件事只能由产品负责人裁决后写。
