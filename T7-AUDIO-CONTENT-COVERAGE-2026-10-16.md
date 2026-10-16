# T7 交付：音频内容覆盖（2026-10-16）

- 任务：**T7 Audio Content Coverage**，负责人 AI-B
- 分支：`task/audio-content-coverage`
- 工作树：`D:/TF2_Native_Worktrees/audio-content-coverage`
- 基线提交：`4360403aee7e606768cf7cadcdd288d635700cc8`（`p0-entity-protocol`，2026-10-16 19:18:21 +0800）
- 最终提交：本分支 tip，分三个提交，链式记录在
  `evidence/t7-audio-content/COMMITS.txt`：
  | 提交 | sha | 内容 |
  |---|---|---|
  | **A** | `0a01873cbc38a10ea5e407c6f0a075b4b75e38b3` | 产品、探针与门禁（`build-target.sh`、`native/**`、`verify-all.sh`、`verify-audio.sh`） |
  | **B** | 见 `COMMITS.txt` | 证据与本文档（`evidence/t7-audio-content/**`、本文件） |
  | **C** | `git log -1` | 仅 `COMMITS.txt`，把 A 与 B 的 sha 钉死 |

  **A 是合并目标**（`WORKTREE-WORKFLOW.md` 规则 7：整合者只从已提交 commit 合并）。
  为什么是三个：本文档要钉"最终提交"，而文档自身所在的提交无法在文档内部写出自己的 sha
  （自引用）。C 用一个只含提交清单的文件打破这个循环——A 与 B 都被逐字钉住，
  C 的身份由分支 tip 取得。

## 0. 一句话结论

T7 期间在本机**复现并修掉了一个真实缺陷**：MP3 资源在资源层只有"帧签名检查"、没有解码，
因此音频归档里 **1340 个 MP3 事件**（三份 pinned 录制合计）全部落在
`unsupported_format`，**一个都播不出来**——其中包含**不是**过滤类别的
`ambient_mp3/water/water_splash{1,2}.mp3`（world 类，cp_well 会真的去调度并播放它们）。
修复后用 Windows ACM 的 MPEG Layer-3 编解码器做真解码；同一批输入上
`unsupported_format` 归零、`resolved` 相应上升，cp_well 的 `playedEvents` **+2**，
这两个正是那两声水花。

同时把 T3 留下 `NOT VERIFIED` 的 **music** 类别从"三份 pinned 录制里 0 个事件"推进到有真实输入：
在 55 份录制的选择性扫描里量到 **22 个 music 事件、分布在 5 份录制 / 4 张地图**，
并按 T7 要求把每个"没量到"的类别保留成**打印出来的读数**而不是删掉的断言。

`temp_entity` 与 `pcf` 两个类别**至今 NOT VERIFIED**，这是有意的（见 §11）。

## 1. 交付格式对照

| 交付格式条目 | 本文档 |
|---|---|
| 1. 任务 id / 分支 / 工作树 / 基线 / 最终提交 | §1、§2 |
| 2. 改动文件与每项原因 | §4 |
| 3. 真实输入、精确命令、退出码、构建身份 | §5、§6 |
| 4. 正向读数、拒绝读数与哈希 | §7、§9 |
| 5. 变异结果与源码逐字节复原证明 | §8 |
| 6. 证据路径、工作树洁净度、残留进程、局限 | §10、§11、§12、§13、§14 |
| 7. 合并前需声明的跨任务依赖 | §15 |

## 2. 任务与工作区

```bash
cd /d/TF2_Native_Worktrees/audio-content-coverage
git branch --show-current      # task/audio-content-coverage
git rev-parse HEAD             # 4360403aee7e606768cf7cadcdd288d635700cc8（基线）
```

按 `WORKTREE-WORKFLOW.md` 硬隔离规则 1，本任务只读写本工作树、`native/build-nmake`、
`.scratch/t7` 和 `evidence/t7-audio-content`。没有读写任何其它任务的目录、脚本或 exe。
所有路径按本任务根目录计算，未硬编码其它 worktree。

## 3. 对照 T7 的七个步骤

| 步骤 | 做法 | 落点 |
|---|---|---|
| 1. 先读现状再改 | 先读内容报告、分类器、资源解码器、调度器、`verify-audio.sh` | §4 的缺陷就是这一步量出来的 |
| 2. 搜语料、记录哪些输入真有该类事件 | 1687 份 header 全扫 + 55 份选择性扫描 | §7.3，证据 `corpus-scan-55.jsonl` |
| 3. 逐事件追 表/索引 → tick → 资源路径 → 分类 → 解码 → 调度/播放 | 内容映射 tsv（逐事件行）+ 语料扫描逐录制记录 | §7.2、证据 `.tsv` |
| 4. 把缺资源/格式不支持/被过滤/重复/坏载荷/成功播放分开计数 | 六个 `SoundResolveStatus` + `resolvedFiltered`/`unresolvedFiltered` | §7.1、§7.2 |
| 5. 只对**已复现**的缺口加支持，保持 null sink 与显式设备选择 | 只加 MP3 解码；`WAVE_MAPPER` 仍未使用 | §4.1、§7.2 |
| 6. null sink 与 device 检查分开跑，三件套与人耳分开记 | 门禁 6/6 节 + `--device` 节，两个独立 trace | §7.4、§7.5 |
| 7. 改完跑正向与变异门禁 | 两次全量门禁（`--mutation`；`--device --mutation`） | §6 |

## 4. 修改文件与原因

### 4.1 产品代码（复现出来的缺口）

| 文件 | 改动 | 原因 |
|---|---|---|
| `native/src/audio_resource.cpp` | 新增 ACM MPEG Layer-3 解码（`findMp3FrameHeader` / `decodeMp3ToPcm` / `mp3DecoderAvailable`）；修正头文件顺序 | 唯一能真解码 MP3 的本地后端；`WIN32_LEAN_AND_MEAN` 下 `windows.h` 不拉 `mmsystem.h`，`FOURCC` / `MMSYSERR_NOERROR` 缺失，必须让 `mmsystem.h` 先于 `mmreg.h`/`msacm.h` |
| `native/include/audio_resource.h` | 声明解码 API；把 `WavPcmData` 移到 `decodeMp3ToPcm` 之前 | 否则 `error C2061: 语法错误: 标识符"WavPcmData"` |
| `native/src/audio_timeline.cpp` | 把只查签名的 `hasMp3Frame` 换成真解码（`readSoundResource` 与 `classifySoundResourceBytes` 两处）；新增 `soundResolveStatusForMp3Decode` | 这是缺陷本体：`valid=true` 却没 PCM 可交，产物层必然播不出 |
| `native/include/audio_timeline.h` | 声明 `soundResolveStatusForMp3Decode` | 把"解码结局 → 具名状态"的映射收在一处，并让"本机没有解码器"这个**无法在有好解码器的机器上产生**的输入可以被断言 |
| `native/src/main.cpp` | 解析条件从 `codec == SoundCodec::Wav` 改为 `wav.valid && wavPcmIsPlayable(...)` | **接线缺陷**：解码落到资源层了，但解析器仍要求 WAV，于是刚被解码变可播的事件一个都进不了产物路径 |
| `native/src/demo_header.cpp` / `native/include/demo_header.h` | 把 `demoTickRate` 与 `buildSoundTimeline` 提上来共用 | 四个调用方各自一份实现，第五份正要写；`applySoundDelayTicks` 对 tickRate 敏感，两份不一致会把同一事件放到不同 tick |

### 4.2 探针与门禁（判据侧）

| 文件 | 改动 | 原因 |
|---|---|---|
| `native/tools/audio_scheduler_probe.cpp` | 新增 MP3 读数；`indexResolved` 拆成 `indexResolvedWav`/`indexResolvedMp3`；+2 个变异（`mp3_decode`、`mp3_silent_accept`） | 原来 `vpkWav` 取自"已解析资源数"，加了解码后该值变 2，名字与现实不符；两个新变异让"解码"与"解码到静音也算过"这两种缺陷都能被抓 |
| `native/tools/sound_content_map_probe.cpp` | 解析条件与产物改成同一条；新增 `resolvedFiltered`/`unresolvedFiltered` 并按事件分类打印 | 探针若与产物用不同判据，就会回答"产物会怎么调度"却答错 |
| `native/tools/sound_corpus_scan_probe.cpp` | **新增** | T7 步骤 2 需要一个能扫整个语料、并按类别给出**每份录制**记录的仪器 |
| `verify-audio.sh` | 新增 5/6 语料类别切片（pinned manifest + 逐录制 pin + 覆盖累加器 + NOT VERIFIED 打印行）；节号重编为 `/6`；内容节恒等式 #5 改写；新增 MP3 needles；+4 个语料变异；构建目标表加 `sound_corpus_scan_probe` | 把 §7.3 的读数变成判据；恒等式 #5 的改写在 §7.1.1 |
| `verify-all.sh` | `exe_count` pin 32 → 33 | 新增一个目标；该 pin 若不动，总链第 1 步会恒 `BUILD=FAIL`（同一个错误在本仓库的历史上已经犯过一次，注释里记着） |
| `build-target.sh` | 目标在 `Makefile` 里没有规则时先 configure | 新目标没有规则，`nmake <name>` 会在任何再生成之前以 `U1073` 死掉 |

## 5. 真实输入与精确命令

### 5.1 pinned 输入（全部 size + sha256 钉住）

| 代号 | 文件 | size | sha256（前 12） | 出现在 |
|---|---|---|---|---|
| pov | `$TF/demos/autorecord_2026-07-02_13-26-46.dem` | 21856667 | `c8a58501d222` | T3 已用；§6/§7/§9 |
| matcha | `D:/TF2_Demo_Player/testdata/demos/bb841c6d379ff7c40d0c8baf99f59d…cp_snakewater_final1.dem` | 51834617 | `308cc1453672` | T3 已用；§6/§9 |
| well | `$TF/demos/autorecord_2026-07-16_12-41-25.dem` | 5509069 | `5e0b1fe8d972` | T3 已用；§7.1 的 +2 出自这里 |
| pd_circus | `$TF/demos/autorecord_2026-10-04_12-08-10.dem` | 30271844 | `fe0eccffc960` | 5/6 语料切片 |
| cp_manor_event | `$TF/demos/autorecord_2026-10-02_14-26-03.dem` | 18553189 | `758bb294726b` | 5/6 语料切片 |
| cp_wildcat_event | `$TF/demos/autorecord_2026-10-02_13-20-12.dem` | 7728247 | `05249c1e0b00` | 5/6 语料切片 |
| plr_hightower_event | `$TF/demos/autorecord_2026-10-12_16-47-40.dem` | 5156901 | `9b88815c6b00` | 5/6 语料切片 |

`$TF = D:/SteamLibrary/steamapps/common/Team Fortress 2/tf`。
5/6 切片的清单固定在 `evidence/t7-audio-content/corpus-inputs.txt`（判据只读这份清单，
不读活动目录，因此换机器不会让门禁变红）。

### 5.2 命令

```bash
cd /d/TF2_Native_Worktrees/audio-content-coverage

# 1) 正向 + 变异（无设备；null sink）
VERIFY_AUDIO_OUT=evidence/t7-audio-content/gate bash verify-audio.sh --mutation \
  > evidence/t7-audio-content/gate-run.log 2>&1

# 2) 设备验收（可听，device 6；含设备两个变异）
VERIFY_AUDIO_OUT=evidence/t7-audio-content/gate bash verify-audio.sh --device --mutation \
  > evidence/t7-audio-content/gate-run-device.log 2>&1

# 3) 单独复核 MP3 解码（一句话读全部 MP3 读数）
native/build-nmake/audio_scheduler_probe.exe

# 4) 语料类别扫描（55 份，本次选择规则见 corpus-scan-55-selection.txt）
#    实际执行时 stderr 落到 .scratch/t7/candidates-scan.err，运行结束为 0 字节
native/build-nmake/sound_corpus_scan_probe.exe --manifest .scratch/t7/candidates.txt --per-demo \
  > evidence/t7-audio-content/corpus-scan-55.jsonl 2> .scratch/t7/candidates-scan.err
tail -1 evidence/t7-audio-content/corpus-scan-55.jsonl \
  > evidence/t7-audio-content/corpus-scan-55-summary.json

# 5) 单份录制的逐事件内容映射（pov 例）
native/build-nmake/sound_content_map_probe.exe \
  --demo "$TF/demos/autorecord_2026-07-02_13-26-46.dem" --tf-root "$TF" \
  --map-out evidence/t7-audio-content/content-pov.tsv
```

## 6. 构建与门禁退出码

### 6.1 构建身份

| 项 | 值 |
|---|---|
| 生成器 | NMake Makefiles（`CMAKE_BUILD_TYPE=Release`） |
| 编译器 | MSVC 19.44.35227.0，工具集 14.44.35207，Hostx64/x64 |
| CMake | 4.4.3 |
| Windows SDK | 10.0.26100.0 |
| 目标数 | `grep -c '^add_executable(' native/CMakeLists.txt` = **33**，`native/build-nmake/*.exe` = **33** |

### 6.2 退出码

| 命令 | 退出码 | 末行 | 用时 |
|---|---|---|---|
| `verify-audio.sh --mutation`（无设备） | **0** | `VERIFY-AUDIO=PASS (assertions_ok=235)` | 17m02s |
| `verify-audio.sh --device --mutation`（最终树） | **0** | `VERIFY-AUDIO=PASS (assertions_ok=240)` | 19m37s |

240 = 235 + 设备节 5 条，`assertions_bad=0`。两跑的逐节分布（`awk` 按节统计，不是手算）：

| 节 | `--mutation` | `--device --mutation` |
|---|---|---|
| 2/6 探针（effects 12 + scheduler 33 + sink 4 + devices 3） | 52 | 52 |
| 3/6 census | 12 | 12 |
| 4/6 内容映射 | 82 | 82 |
| 5/6 语料类别切片 | 80 | 80 |
| 6/6 播放器 | 9 | 9 |
| `--device` 设备节（单次开合 + null + device 6） | — | **5** |
| **合计** | **235** | **240** |

> 复核方式：上表是 `awk` 按 `=== N/6` 分节统计出来的，不是从末行倒推；
> 每行都另用 `grep -c '^  OK'` 对过。两跑唯一差异就是设备节那 5 条，
> 其余五节逐节相等——这也顺带证明 `--device` 没有改变任何非设备断言。
> 逐条清单见 `evidence/t7-audio-content/gate-run.log` 与 `gate-run-device.log`。

### 6.3 与 `verify-all.sh` 的关系

本任务**不**运行 `verify-all.sh --quick`（它属于 T5 的串行总链，且任务书明确要求
T6–T9 期间不得再起总链）。本轮唯一影响总链的改动是 `exe_count` pin（32→33），
其证明是同一棵树上 `grep -c '^add_executable('` = 33 与构建产物 33 个 exe。
若其它任务也加目标，整合后需按最终树一次性重数——这正是注释里写的约定。

## 7. 具名正向读数与拒绝读数

### 7.1 缺陷与修复的读数对照（同一批 pinned 输入）

| 读数 | 修复前 | 修复后 | 说明 |
|---|---|---|---|
| pov `statusUnsupportedFormat` | 511 | **0** | 511 个 MP3 事件全部是 filtered 的 voice/announcer 片段 |
| pov `statusResolved` | 3564 | 4075 | +511 |
| pov `playedEvents` | 3564 | **3564** | 未变——filtered 的解析出来也不播 |
| matcha `statusUnsupportedFormat` | 637 | **0** | |
| well `statusUnsupportedFormat` | 192 | **0** | |
| well `playedEvents` | 4371 | **4373** | **+2**，就是两声 world 类水花 |
| `audio_scheduler_probe` `indexResolved` | 1（只有 WAV） | 2（WAV 1 + **MP3 1**） | `indexResolvedWav=1 indexResolvedMp3=1` |

三份录制合计 **1340** 个 MP3 事件，其中只有 **2** 个（cp_well 的
`ambient_mp3/water/water_splash{1,2}.mp3`，world 类）是产物路径真会去播的；
另外 1338 个是 filtered 的 voice/announcer，解析出来也不会播。这个区分就是 §7.1.1 恒等式改写的来源。

T7 步骤 3 要求"逐事件追 表/索引 → tick → 资源路径 → 分类 → 解码 → 调度/播放"，
这两条就是修复后按那个顺序读出来的原始行（内容映射 tsv，列序
`tick  index  kind  status  seconds  srctable  path  name  detail`）：

```
2004:4650  6615  world  resolved  1.4  soundprecache  sound/ambient_mp3/water/water_splash1.mp3  ambient_mp3/water/water_splash1.mp3  decoded '...' channels=1 rate=44100 bits=16 bytes=122112
3493:7675  6616  world  resolved  1.6  soundprecache  sound/ambient_mp3/water/water_splash2.mp3  ambient_mp3/water/water_splash2.mp3  decoded '...' channels=1 rate=44100 bits=16 bytes=140544
```

（`soundprecache` 是源表，6615/6616 是它的索引，4650/7675 是事件 tick，`detail` 里的
`decoded` 是解码结局。修复前这两行的 `status` 是 `unsupported_format`、
`detail` 是 `MP3 is not decoded by the native PCM output path`，因此它们不播；
`grep -c ambient_mp3` 在 cp_well 的映射表里恰好是 **2**，与 `playedEvents` 的 +2 同源。）

#### 7.1.1 顺带修掉的一条判据恒等式

修好解码之后，探针立刻报出一个**恒等式不成立**：`statusResolved` 随 MP3 人口上升了
（pov 3564→4075，+511），而 `playedEvents` **没动**。原因不是丢了播放，而是恒等式本身写错了——
它把 filtered 的事件也算进了 `resolved`。

- 修前：`played == resolved`（pov 上曾是 3564 == 3564，看起来成立，纯属巧合：
  当时所有 filtered 事件都因为 MP3 不支持而落在 `unsupported_format` 里，没进 `resolved`）
- 修后：探针新增 `resolvedFiltered` / `unresolvedFiltered`，
  门禁把内容节恒等式 #5 改成
  `played == resolved - resolvedFiltered`、`missing == notResolved - unresolvedFiltered`，
  并加了 `filtered == resolvedFiltered + unresolvedFiltered` 的闭账。

实测：pov 的 `resolvedFiltered=511`、`played` 仍 **3564**（正确）；
well 的 `resolvedFiltered=190`、`played` 4371→**4373**（+2，正是那两声水花）。
两个数一起才说明"多解析出来的 1338 个 MP3 里，只有 2 个真进了播放路径"。
这条恒等式同时被 `content-status` 变异打红（2/10），所以它确实在检查东西。

### 7.2 修复后的最终读数（节选，提交树上）

```
indexMp3Path=sound/ambient_mp3/lair/rolling_thunder1.mp3
indexMp3PcmBytes=1437696 indexMp3Rate=44100 indexMp3Channels=2 indexMp3Seconds=8.15
mp3EventPath=sound/ambient_mp3/water/water_splash1.mp3
mp3EventStatus=resolved mp3EventPcmBytes=122112 mp3EventSeconds=1.38
mp3EventRate=44100 mp3EventChannels=1
mp3DecoderAvailable=1
mp3NoFrameStatus=invalid_wav          # MP3 形状但没有帧 → 数据事实
mp3SilentStatus=invalid_wav           # 解码出 0 个 PCM → 数据事实
mp3Layer2Status=unsupported_format    # Layer II 被按名拒绝
mp3MapNoDecoderStatus=unsupported_format   # 本机没有解码器 → 能力缺口
mp3MapRefusedFormatStatus=unsupported_format
mp3MapDecodedStatus=resolved
device=0 playbackCalls=0 device_opened=0 device_open_attempts=0
mutationsCaught=10 mutationsTotal=10
realEntries=15958 realVoice=12335 realAnnouncer=442 realMusic=59 realTempEntity=0 realPcf=0
```

`realTempEntity=0 realPcf=0` 是在两个音频归档的全部 15958 条预缓存名上量的：**没有任何一个
出货音效名带 `temp_entity` 或 `pcf` token**。探针里 `kindTempEntity=1 kindPcf=1` 是分类器的
**合成**用例（证明分类器能命名它们），与上面这条实测是两件事，不能互相背书。

### 7.3 语料类别切片（5/6 节的覆盖累加器原文）

```
-- corpus coverage over 5 pinned recording(s), 24357 scheduled events:
   music events in 3, temp_entity events in 0, pcf events in 0,
   svc_VoiceData in 2, svc_TempEntities in 5;
   music-kind names in tables: 67 (of which recordings with names but no music event: 2)
```

55 份扫描（`corpus-scan-55-summary.json`）：

| 读数 | 值 |
|---|---|
| 录制数 / 解析失败 | 55 / 0 |
| music **事件** | **22**（分布在 5 份录制、4 张地图） |
| temp_entity / pcf 事件 | **0 / 0** |
| voice / announcer 事件 | 20142 / 66 |
| 调度事件总数 | 202620 |
| 解码事件总数 | 213966 |
| 构建拒绝数 | **0** |
| `svc_VoiceData` 出现的录制数（载荷位数） | 17（3530680 bits） |
| `svc_TempEntities` 出现的录制数（载荷位数） | 55（269126492 bits） |
| 表内 music 名 / 带名但无事件的录制 | 741 / 50 |
| 未知消息包 | 0 |

**为什么是这 55 份**：整个 `tf/demos` 是 1687 份 / 44 163 051 737 字节（41.13 GiB）。
全量一趟按本次实测速率外推约 **13 小时**（55 份 = 1 134 705 010 字节 / 约 20m20s ≈ 0.89 MiB/s），
门禁跑不动；第一趟全量尝试三分钟内没有 flush 出任何一条记录，与该外推一致。
选择规则是"地图名含 `event` / `mvm` / `circus` / `asteroid` / `doomsday`"——
即全部非标准模式人口（万圣节图、MvM、以及带回合结束音乐的两张图）= 22 张地图 / 55 份录制。
规则与外推都写在 `evidence/t7-audio-content/corpus-scan-55-selection.txt`，
其中明确注明**外推只作可行性论证，不作为计数**；所有当作证据的计数都来自真扫过的 55 份。

> 复核方式：上表不是照抄 summary 行。用一份独立的 Python 脚本从 55 条逐录制记录重算
> `kinds` / `table`，与本文件里的 summary 行逐键比对——**事件类别逐键相等**。
> `table` 一处差异是**格式**而非事实：逐录制记录的 `table` 对象**只打印非零类别**，
> 而 summary 行带全部 10 个键（`temp_entity:0`、`pcf:0`）；两者是同一个读数。
> 这也解释了 summary 行里 `"table":{...}` 缺 `unknown` 与之对应的原因。

5 个 music 载体（逐条）：`cp_manor_event` 1、`pd_circus` 3、`koth_slaughter_event` 4、
`koth_slaughter_event` 12、`plr_hightower_event` 2。

实际观察到的 music 名：`music/bump_in_the_night.wav`（11306808 B, 64.1 s）、
`music/carousel_of_curses.wav`（7714532 B, 43.7 s）、`music/taunt_bongos.wav`（2116800 B, 12.0 s）、
`ui/mm_round_end_red_win_music.wav`（2645676 B, 15.0 s）。
最后一条**不是**按 `music/` 前缀分类的，而是命中 `music` 这个路径边界 token——
两者不是一回事，门禁按后者断言。

### 7.4 拒绝读数（负例）

| 输入 | 期望 | 实测 |
|---|---|---|
| MP3 形状但无帧的字节 | `invalid_wav` | `mp3NoFrameStatus=invalid_wav` |
| 解码出 0 个 PCM 的流 | `invalid_wav`（不是 resolved） | `mp3SilentStatus=invalid_wav` |
| MPEG Layer II 流 | 按名拒绝 | `mp3Layer2Status=unsupported_format` |
| 本机无解码器 | 能力缺口，不是数据损坏 | `mp3MapNoDecoderStatus=unsupported_format` |
| 3 个畸形 WAV | 全部拒绝 | `wavMalformed=3` |
| 超过播放上限的时长 | `excessive_duration` | `indexExcessiveDuration=1` |
| 缺资源的事件 | 永不播放 | `missingPlayed=0 missingResource=2` |
| 无标记名字 | 不解析 | `indexMissingPrecache=1 indexMissingWav=1` |

### 7.5 设备三件套（两个运行分别记录）

两个运行只差一个 `--audio-disabled`，是两个独立进程、两份独立 trace，
所以任一侧的读数都不可能从另一侧继承。pinned 设备 id 是 **6**；
门禁在 `device_count < 7` 时直接 FAIL，不允许回落到默认设备。

| 运行 | 命令差异 | `audio_device_opened`（历史调用计数） | `audio_device_open_attempts` | `audio_device_opens_ok` | `events_played` | `rc` |
|---|---|---|---|---|---|---|
| null sink | `--audio-disabled --audio-device 6` | 0 | **0** | 0 | **0** | 0 |
| device 6 | `--audio-device 6` | 742 | **742** | **742** | **742** | 0 |

外加单次受控开合（100 ms 静默缓冲）：
`winmm_sink_probe --device 6` →
`mode=explicit device=6 device_count=13 device_opened=1 playback_written=1 prepared=1
reset_ok=1 unprepare_ok=1 close_ok=1 … status=pass`（rc=0）。

设备断言除了上面三列，还要求 `played >= 1`、`played <= opens_ok`、
且历史调用计数 == attempts——即"健康设备上开失败"是这套验收专门要暴露的读数。

完整原文（含命令、`device=6 name=耳机 (xduoo audio)`、两跑全部读数、
以及"为什么这些计数不构成可听性证据"）：`evidence/t7-audio-content/device-runs.txt`。

### 7.6 人耳确认（与计数分开，且不拿计数当听觉证据）

- 结论：**用户确认"听到了声音，听起来正常"**（2026-10-17 本机时间，
  紧随 `--device --mutation` 门禁之后；device 6「耳机 (xduoo audio)」；
  pov、tick 2000 窗口 ≈30 秒）。
- 用户在播放前被明确询问并选择"能听，跑完就播"；该结论是**人的陈述**，
  与 §7.5 的 `played=742` 是**产品计数**这一事实并存、互不背书。
- 这次听觉检查用的命令与门禁 `--device` 节的 device-6 运行同形
  （同 demo、同 tick 2000、同设备），`exit=0`。
- **没有**被这条陈述证明的东西（也不声称）：听到的内容是否正确（哪几个音、
  混音/曲目是否对）、与已知良好播放器的对照、延迟正确性、device 6 以外的设备、
  以及负载下的稳定性。详见 `evidence/t7-audio-content/listening.txt` 的
  "WHAT IS AND IS NOT ESTABLISHED"。

## 8. 变异结果与源码逐字节复原证明

### 8.1 两层变异

**第一层：探针内部翻转**（`audio_scheduler_probe` 自己跑完 10 个用例并打印）
`mutationsCaught=10 mutationsTotal=10`，用例：
`tick_dedup`、`reverse_reset`、`voice_filter`、`delay_rounding`、`missing_as_played`、
`null_sink_device`、`duration_cap`、`marker_strip`、**`mp3_decode`**、**`mp3_silent_accept`**。
后两个是 T7 新增：`mp3_decode` 一次打红 5 条具名读数
（`indexResolved`、`indexUnsupportedFormat`、`indexMp3PcmBytes`、`mp3EventStatus`、`mp3EventPcmBytes`），
`mp3_silent_accept` 打红 `mp3SilentStatus`。

**第二层：门禁对捕获文件副本的扰动**。`--mutation` 25/25 全部
`MUTATION-CAUGHT=PASS`；`--device --mutation` 再 +2（`device-null-attempts`、`device6-opens`）= **27/27**。

| # | 用例 | 打红断言数 |
|---|---|---|
| 1 | `effects` | 1 / 12 |
| 2–4 | `scheduler`、`scheduler-duration`、`scheduler-marker` | 各 1 / 33 |
| 5 | `sink` | 1 / 4 |
| 6–9 | `census-origin`、`-forward`、`-duplicate`、`-reverse` | 各 1 / 4 |
| 10–16 | `player-played`、`-device`、`-attempts`、`-voice`、`-origin`、`-read`、`-accounting` | 各 1 / 8 |
| 17–21 | `content-status`(2/10)、`-maplines`、`-marker`、`-duplicate`、`-reverse` | 1 / 10 |
| 22–25 | `corpus-accounting`、`corpus-kinds`(2/4)、`corpus-table`、**`corpus-pin`(9/10)** | 1–9 / 4–10 |
| 26–27 | `device-null-attempts`（1/1）、`device6-opens`（1/1） | 1 / 1 |

两跑的实测：`--mutation` = **25/25**，`--device --mutation` = **27/27**，
`MUTATION-CAUGHT=FAIL` 与 `MUTATION-SETUP=FAIL` 各 **0** 条（`grep -c 'FAIL'` = 0）。
`device6-opens` 的期望值不是硬编码的，是运行时从 device-6 的 `mode: summary` 行里读出来的
`audio_device_opens_ok`，所以它钉的是"这次真读到的那个数"。

> `--device` 的两个设备变异只在**本次真的带 `--device` 抓了 trace** 时才跑：
> 条件是 `--device` 标志，不是"trace 文件存在"。捕获文件跨运行存活在 `$OUT` 里，
> 早先按"文件存在"判断时，一次普通 `--mutation` 会去变异**上一轮**留下的 trace
> 并照报捕获——那是本仓库反复点名的"PASS 读自错误字节"。本轮的
> `--mutation` 日志里对应打印的是
> `(device mutations skipped: run with --device to capture them)`，即 25 而不是 27，
> 两跑计数因此自洽。

每个用例都作用在 `$OUT` 里的**副本**上（`cp` + `sed -i`），并用**门禁自己调用的同一个函数**
重跑断言；用例若没改动任何字节会先报 `MUTATION-SETUP=FAIL`，所以"MUTATION-CAUGHT=PASS
却什么都没检查"这条路径是堵住的。

### 8.2 逐字节复原证明

变异全部作用于捕获副本与探针内存态，**不写源码**。这不是声明，是量出来的：

- 在变异段开始前（门禁跑到 5/6 节、`line 1100` 尚未进入）抓取 14 个被改文件的 sha256
  → `evidence/t7-audio-content/gate/SHA256SUMS.src-before.txt`；
- 运行结束后抓同样的 14 个 → `SHA256SUMS.src-after.txt`；
- `diff` 两文件**为空**：14/14 逐字节一致。
- `--device --mutation` 那一跑前后同样抓一对
  （`SHA256SUMS.src-before-device.txt` / `-after-device.txt`），`diff` 同样为空。

**覆盖区间要说准**：这两对清单覆盖的是**各自那一跑的运行区间**
（即"变异段有没有写源码"），取的时候工作副本还是 LF。
那之后做了 §9.4 所述的行尾归一化（只改行尾、不改提交内容），
所以再补一份 `gate/SHA256SUMS.src-at-C.txt` 钉住**被提交的那份状态**，
同时给两列读数：工作副本形态的 sha256，与 index blob 的 sha1。
判"提交 == 被测"用 blob 列（`git rev-parse HEAD:<path>`），
判"工作副本 == 提交内容"用 sha256 列（规范化行尾后）。
两份清单的用途不同，不要互相代替。

## 9. 哈希

### 9.1 输入（size + sha256 全钉，门禁只读这份清单）

见 §5.1；清单原文 `evidence/t7-audio-content/corpus-inputs.txt`（5/6 切片）
与 `evidence/audio/timeline-inputs.txt`（3/6、4/6 节，T3 建、本轮沿用未改）。
门禁对每份输入都打印 `pinned input verified: size <N>, sha256 <前12>`，并**在不符时 FAIL**
（"输入不在"与"输入干净"不允许读成同一个结果）。

### 9.2 证据文件

`evidence/t7-audio-content/SHA256SUMS`（9 条）：

| 文件 | sha256（前 16） |
|---|---|
| `gate-run.log` | `d72530d5fe30d2d8` |
| `gate-run-device.log` | `afde031db4f92e3a` |
| `corpus-scan-55.jsonl` | `7ec64f0f1ddfc340` |
| `corpus-scan-55-summary.json` | `7a8b008071b3f2a1` |
| `corpus-scan-55-selection.txt` | `6d69edeadfe55cfa` |
| `corpus-inputs.txt` | `02868cbc16499f1a` |
| `content-pov.txt` | `11fd163ace4e40ca` |
| `device-runs.txt` | `04b5b60f00442c0c` |
| `listening.txt` | `2c0948301ef47c4d` |

**复核配方**（本文件与 T3 一样是 CRLF；`sha256sum -c` 会把行尾的 CR 当成文件名的一部分，
所以要先 `tr -d '\r'`）：

```bash
cd evidence/t7-audio-content
tr -d '\r' < SHA256SUMS > /tmp/t7.sums
sha256sum -c /tmp/t7.sums            # 实测：9/9 OK
```

不先 `tr` 直接 `sha256sum -c SHA256SUMS` 会报
`'./gate-run.log'$'\r': No such file or directory`——这不是证据坏了，是本机
`core.autocrlf=true` 与 T3 已经记录过的同一个坑。本文件把配方写全，正是为了让下一个人不必再踩一次。

> 行尾归一化的说明（做过的，不是假设）：本轮 `evidence/t7-audio-content/` 下 72 个文本文件
> 统一转成 CRLF（3 个 `.bmp` 按二进制跳过），使工作副本与仓库其余证据目录一致
> （T3 的 `evidence/audio/SHA256SUMS` 就是 `w/crlf`）。
> 转换前 `gate-run*.log` 是**混合行尾**——bash 自己打印的行是 LF、被调用的 exe 走 CRT 文本模式
> 是 CRLF。归一化后两跑的**全部读数逐项复验未变**：
> `--mutation` = 290 行 / 235 OK / 0 FAIL / 25 捕获 / `VERIFY-AUDIO=PASS (assertions_ok=235)`；
> `--device --mutation` = 296 行 / 240 OK / 0 FAIL / 27 捕获 /
> `VERIFY-AUDIO=PASS (assertions_ok=240)`。
> 提交 blob 与行尾无关（`core.autocrlf` 一律规范成 LF），所以 §9.2 的哈希是
> **CRLF 工作副本**的哈希，与 §9.4 里 index blob 的哈希是两种不同的度量，不要混读。

> `evidence/t7-audio-content/gate/` 下的各节原始捕获（探针输出、census、内容映射 tsv、
> 语料逐录制、设备 trace、27 个变异副本）体量大且会被下一次运行覆盖，
> 因此**不**进 `SHA256SUMS`；它们的作用是"断言当时读的就是这些字节"，
> 而 §9.3 的源码哈希对才是复原证明。

### 9.3 源码逐字节复原（§8.2 的五个清单）

| 清单 | 覆盖区间 | `diff` 结果 |
|---|---|---|
| `gate/SHA256SUMS.src-before.txt` | `--mutation` 跑：5/6 节之后、变异段之前 | — |
| `gate/SHA256SUMS.src-after.txt` | `--mutation` 跑：运行结束 | **空** vs before |
| `gate/SHA256SUMS.src-before-device.txt` | `--device --mutation` 跑：运行前 | — |
| `gate/SHA256SUMS.src-after-device.txt` | `--device --mutation` 跑：运行后 | **空** vs before |
| `gate/SHA256SUMS.src-at-C.txt` | **提交 C**：两列（工作副本 sha256 + index blob sha1） | 见 §8.2 的覆盖区间说明 |

覆盖的 14 个文件：`build-target.sh`、`native/CMakeLists.txt`、
`native/include/{audio_resource,audio_timeline,demo_header}.h`、
`native/src/{audio_resource,audio_timeline,demo_header,main}.cpp`、
`native/tools/{audio_scheduler_probe,sound_content_map_probe,sound_corpus_scan_probe}.cpp`、
`verify-all.sh`、`verify-audio.sh`。

### 9.4 提交内容的哈希证明（行尾插曲）

`verify-audio.sh` 在本轮被就地编辑后变成了 **LF-only**（仓库其余 80+ 脚本都是 CRLF）。
处理方式与证明：

- 不是手改字节，而是先用 `git cat-file -p :verify-audio.sh | sha256sum` 确认
  **暂存 blob 是 LF**，即 `core.autocrlf=true`（来自便携 Git 的 system config，
  不是仓库配置）会在提交时把两种行尾都规范成 LF——**提交内容与行尾无关**；
- 该 blob 的 sha256 = `0763f6dd2f58713c…`，**与门禁实际跑过的那份文件逐字节相同**
  （即"门禁验证的字节 = 要提交的字节"）；
- 再把工作副本按 repo 惯例转成 CRLF（`lines=1280 CR=1280`，`git ls-files --eol`
  变为 `i/lf w/crlf`），转后重新 `git add`，**暂存 blob 哈希不变**，
  `bash -n` 通过，`git diff --cached --numstat` 仍是 `393 17`。
- 因此最终提交里的 `verify-audio.sh` 与 §6 门禁所验证的内容一致；
  最终树上的那一跑是 `--device --mutation`（§6.2 第二行）。

同一个插曲也发生在**新增文件** `native/tools/sound_corpus_scan_probe.cpp` 上
（新写出来的文件是 LF，仓库惯例是 CRLF）。处理方式完全相同：
转 CRLF 后重新 `git add`，暂存 blob 仍为 `80303968aad2f5f4…`（不变），
然后**在最终字节上重新构建并复查脚本语法**，以免"已验证的字节"和"编译过的字节"分家：

```
bash build-target.sh audio_effects_probe audio_scheduler_probe winmm_sink_probe \
  sound_timeline_census_probe sound_content_map_probe sound_corpus_scan_probe tf2_demo_native
  → rc=0  build_rc=0   native/build-nmake/*.exe = 33
bash -n verify-audio.sh && bash -n build-target.sh && bash -n verify-all.sh   → 三者均 OK
```

因此 §6 的两跑与最终提交之间的差异，只剩**行尾**这一类不影响语义、也不影响提交内容的改动。

## 10. 证据路径

`evidence/t7-audio-content/`（本次新建，旧证据未改动）：

| 文件 | 内容 |
|---|---|
| `corpus-inputs.txt` | 5/6 切片 pinned 输入（5 份录制的 size+sha256 + 选取理由） |
| `corpus-scan-55.jsonl` | 55 份逐录制记录（55 行 JSON + 1 行 summary） |
| `corpus-scan-55-summary.json` | 上面的 summary 行单独一份 |
| `corpus-scan-55-selection.txt` | 55 份的选取规则、全量外推、以及"外推不作计数"的声明 |
| `gate-run.log` | `--mutation` 运行完整日志（235 条断言 + 25 个变异） |
| `gate-run-device.log` | `--device --mutation` 运行完整日志 |
| `gate/` | 各节原始捕获（探针输出、census、内容映射、语料逐录制、设备 trace、变异副本） |
| `gate/SHA256SUMS.src-*.txt` | §8.2 的源码前后哈希对（两组） |

## 11. 未验证项与外部阻塞

| 项 | 状态 | 依据 |
|---|---|---|
| `temp_entity` 类音频事件 | **NOT VERIFIED** | 55 份扫描 0 个；两个归档 15958 条预缓存名中 0 条带该 token；`realTempEntity=0` |
| `pcf` 类音频事件 | **NOT VERIFIED** | 同上，`realPcf=0` |
| 人耳听觉（音高/内容/延迟是否正确） | 见 §7.6 | 计数不证明人听到；人耳也不证明听到的是对的内容 |
| 全语料 1687 份的穷尽计数 | 未做，且**明确不是**本次证据 | §7.3 的外推只作可行性论证 |
| `verify-all.sh --quick` 总链 | 未跑（任务书禁止 T6–T9 期间起总链） | §6.3 |

## 12. 能力边界（不是 bug 的部分）

- **filtered 类别不被解析是设计**：voice / announcer / music 三类留在时间线上并标 `skipVoice`，
  永远不解析、不播放。§7.1 里 1338 个 MP3 属这一类，修复后它们仍不播——这是对的。
- **`unsupported_format` 有两种含义**：本机没有该编解码器（能力缺口）与字节本身不被接受。
  两者都映射到同一个具名状态是**有意**的（`soundResolveStatusForMp3Decode` 一处定义），
  但探针另外把"本机是否有解码器"单独打印成 `mp3DecoderAvailable`，以免两者混读。
- **`ui/mm_round_end_red_win_music.wav` 不是 `music/` 前缀**：它靠 `music` 路径边界 token 分类。
- `durationOverCapSeconds=4194304 durationUnderCapSeconds=524` 是探针构造的极端值，不是真实资源。

## 13. 干净工作树与可运行状态

- 交付后工作树**干净**：`git status --short` 无输出（`git status --porcelain | wc -l` = 0）。
- **本任务无残留进程**。判定方式不是"看任务管理器里有没有同名进程"，
  而是按命令行匹配本工作树路径：
  `Get-CimInstance Win32_Process | Where-Object { $_.CommandLine -like '*audio-content-coverage*' }`
  → 只匹配到该查询自身的 PowerShell 进程；没有任何 `tf2_demo_native.exe` / `*probe.exe` /
  `nmake.exe` / `cl.exe` 引用本工作树。
- 两次门禁都与外部负载重叠，如实记录：本次运行期间本机有**另一个任务的全量构建**
  （`nmake -f Makefile` / `Makefile2 -LS all` / `cl.exe`，构建目标
  `CMakeFiles\viewmodel_animation_probe.dir\build.make`，即 T6 侧）与一次
  `viewmodel_contract_probe.exe`（PID 12788）。本任务的门禁断言全部是确定性计数与恒等式、
  与帧率/负载无关（T3 那轮曾因负载误红的 `duplicateTicks>=1` 已在 T3 内换成账户恒等式），
  两跑均 `assertions_bad=0`，因此判定为不影响结论；受影响面仅限 §7.6 的人耳观察，已在
  `listening.txt` 里写明。
- 可运行状态：`native/build-nmake/` 下 33 个 exe 齐全，`tf2_demo_native.exe` 与四个音频探针
  在提交树上可直接运行（§5.2 的命令均以 `exit=0` 复现过）。
- 未做：`verify-all.sh --quick`（任务书禁止 T6–T9 期间起总链）。

## 14. 未提交文件与证据来源（`WORKTREE-WORKFLOW.md` 规则 6）

- **未提交文件：无。** 本轮所有源码、门禁改动、证据与本文档都已提交（见 §1 的三个提交）。
- **不在提交内、也不要提交的**：`.scratch/t7/`（脚手架：构建日志、临时 trace、ACME 试验、
  探针 exe 副本、`verify-audio.sh` 的 LF 备份）。`.scratch/` 在 `.gitignore` 第 31 行，
  本来就忽略；提交前已把误落在仓库根的 `.scratch-t7-build.log` 移进 `.scratch/t7/build.log`，
  以免工作树出现未跟踪文件。
- **证据来源**：`evidence/t7-audio-content/` 下全部文件由**本任务本轮**产生
  （§10 列出）。沿用而未改的旧证据只有 `evidence/audio/timeline-inputs.txt`
  （T3 建，3/6、4/6 节的 pinned 输入清单）与 `evidence/audio/SHA256SUMS` 所覆盖的 T3 文件——
  **只读，未修改**，且本轮对它们的引用只限于"输入清单"这一用途。
  没有把任何历史内容映射当作本轮证据：§7.3 的语料数字全部来自本轮
  `corpus-scan-55.jsonl`，§7.1/§7.2 的读数全部来自本轮门禁日志。
- **exe 与构建产物**不入库；`evidence/t7-audio-content/gate/` 下的原始捕获随本轮一起提交
  （它们是"断言当时读的字节"），但**不进** `SHA256SUMS`——理由见 §9.2 的注。

## 15. 跨任务依赖（交付格式第 7 项，合并前声明）

1. **`native/CMakeLists.txt` 与 `verify-all.sh` 的 `exe_count` pin 是共享文件**。
   T6 / T8 / T9 若也新增目标，`exe_count` 必须由整合者在**合并后的最终树**上一次性重数。
   本条与 T3 交付里同样的提示是同一个冲突点，第 N 次出现。
2. **运行期排队**：按任务书，产品构建 / 设备检查 / 变异必须一次一个。
   本次运行期间本机**有另一个任务的进程在跑**（`viewmodel_contract_probe.exe`，PID 12788，见 §13），
   即存在重叠。本次门禁的断言全部是确定性计数与恒等式、与帧率/负载无关
   （T3 那轮曾因负载误红过的 `duplicateTicks>=1` 已在 T3 内换成账户恒等式），
   因此判定为不影响结论；但 `--device` 的人耳观察受负载影响，见 §7.6 的边界声明。
3. **`build-target.sh` 的修复影响所有任务**：它现在会在"目标在 `Makefile` 里没有规则"时先
   configure。任何任务新增目标都必须带上这次修复，否则 `nmake <newtarget>` 会以 `U1073` 死。
   这是本任务对共享工具的最小侵入改动，建议优先合并。
