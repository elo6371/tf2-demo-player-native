# T31 交付：音频内容完整性（2026-10-20）

- 任务：**T31 Audio content completeness**（任务板 task-3，owner=t31-audio）
- 分支：`task/t31-audio-content`
- 工作树：`D:/TF2_Native_Worktrees/t31-audio-content`
- 基线：`36303c2`（`p0-entity-protocol`）
- 本文件与证据、产品、探针、门禁同属本分支的提交序列（见 §1 锚点与 `git log`）

## 0. 一句话结论

本轮把"MP3 链"从**资源层能解码**推进到**真实被调度的事件逐条走到输出路径**：pinned 的 cp_well 录制里有
两条 world 类 MP3 事件（tick 4650/7675），产品现在为每条打印一行 **`audio: mp3`** 逐事件记录
（tick、源表索引、归一化路径、解码后字节/采样率/声道/时长、播放结局），并在 `audio: summary` 上给出
五个具名 MP3 计数；同一条 tick 窗口（0→7700）分别用 null sink 与真实设备 6 跑一次，两条记录的
**tick/index/路径/字节/时长逐字节相同**，只有 outcome 按预期不同（null=skipped，设备=played）；
tick 4650 那条在两次运行里都读 `inaudible`，因为让它是 inaudible 的原因（观察者与源的距离）
两次运行完全相同——这正是"同窗口"该有的样子。
同时发现并**具名了一个原来被折叠掉的结局**：tick 4650 的水花在观察者 2048 单位之外，空间混音静音，
过去与"资源缺失"共用 `eventsMissingResource`；现在它是具名的 `inaudible`（总数不变，原有恒等式不变），
所以"没播"不再只有一种解释。负向 fixture（无帧 MP3 / 解码零 PCM / MPEG Layer II / 本机无解码器）
在**播放边界**上被验证为"计数而非静默丢弃、且永不进 sink"。

## 1. 任务与工作区

```
cd /d/TF2_Native_Worktrees/t31-audio-content
git branch --show-current      # task/t31-audio-content
git log --oneline 36303c2..HEAD
```

| 提交 | 内容 | 说明 |
|---|---|---|
| `d5486b3` | 产品 + 探针 + 门禁（`native/**`、`verify-audio-content.sh`、`verify-audio.sh` 的计数 pin） | **合并目标**（WORKTREE-WORKFLOW 规则 7：整合者只从已提交 commit 合并） |
| 其后的提交 | 证据 `evidence/t31-audio-content/**` 与本文档 | 不触碰任何构建输入；因此按 `d5486b3` 合并等价于按 tip 合并产品代码 |

之所以不把 tip 的 sha 抄进本表：本文件自身在最后那个提交里，写出时无法知道它的 sha（自引用）。
权威清单读 `git log --oneline 36303c2..HEAD`。

按 `WORKTREE-WORKFLOW.md` 的硬隔离规则，本轮只读写本工作树、`native/build-nmake`、
`.scratch/t31` 与 `evidence/t31-audio-content`（外加 `evidence/fast/audio-content`：
被 `.gitignore` 忽略的门禁输出目录）。没有读写 `D:/TF2_Native_Test` 或其它任务的工作树。

## 2. 对照任务书五步

| 步骤 | 本轮做法 | 落点 |
|---|---|---|
| 1. 扫语料并区分类别 | 对 7 份 pinned 录制做 MP3 感知扫描（每份记录 .mp3 事件按 kind 拆分、表内 .mp3 名、无扩展名名）；对出货 VPK 做 index-only 的 MP3 普查（按目录分桶）；复用 T7 已提交的 55 份类别扫描作为类别覆盖基线 | §10、`evidence/t31-audio-content/corpus-scan-7.jsonl`、`mp3-census.txt` |
| 2. 每个选中的 MP3 事件记录 5 项 | 内容映射（tick/index/kind/status/seconds/srctable/path/name/detail 九列）给出**全部 192 条** MP3 行；被调度的 2 条在门禁里逐字段 pin；产品逐事件行给出播放结局 | §6、§7、`content-well.tsv` |
| 3. null sink 与真实设备同窗口 | 同一条 demo、同一个 `--capture-tick 7700`、同一份 timeline；两次独立进程、两份 trace，逐项对照 | §8 |
| 4. 负向 fixture 具名拒绝 | 4 类负向（无帧/零 PCM/Layer II/无解码器）+ 播放边界计数（seen=5、played=2、missing=3、sinkCalls=2）+ 本轮新增的 `inaudible` 命名 | §7 |
| 5. forward 绿后才跑既有门禁 | `verify-audio.sh --mutation` 与 `--device --mutation` 都在新内容门禁转绿之后才运行 | §5.2 |

## 3. 改动文件与理由

### 3.1 产品（可观察行为）

| 文件 | 改动 | 理由 |
|---|---|---|
| `native/include/audio_resource.h` | `WavPcmData` 增加 `SoundCodec codec` | 解码后的 PCM 携带"谁产生的"来源。调度器只看得到 PCM，没有它就无法说出"这条声音来自 MP3 解码"——逐事件记录会变成按名字猜编解码器，而名字层面的猜法正是本任务要避免的 |
| `native/src/audio_resource.cpp` | `parseWav` 置 `codec=Wav`（1 行）、`decodeMp3ToPcm` 成功处置 `codec=Mp3`（1 行） | 上述来源的唯一写入点，两个读者各写各的来源 |
| `native/include/audio_player.h` | `AudioScheduleStats` 增加 5 个 MP3 计数 + `eventsInaudible`；新增 `ContentObserver` 与 `setContentObserver` | 总数看不见 MP3：一条 MP3 事件与一条 WAV 事件都只是 1 个 `eventsSeen`，所以"解码链断了"不会移动任何总数。五个计数与逐事件观察器是唯一能回答"MP3 是否走到输出路径"的读数。`inaudible` 是原先被折叠进 `eventsMissingResource` 的第四种结局（资源正常但观察者听不到） |
| `native/src/audio_player.cpp` | `advance()`：从 PCM 的来源判 MP3、五个分支各自计数、每个结局回调观察器；空间静音分支具名 `inaudible` | 计数与逐事件记录的实现点；所有既有分支的计数语义不变（`inaudible` 仍在 `eventsMissingResource` 之内，因此既有门禁的恒等式原样成立） |
| `native/src/main.cpp` | 安装 `setContentObserver`：只为 MP3 打印 `audio: mp3 tick=… index=… name=… pcmBytes=… rate=… channels=… bits=… seconds=… outcome=…`；`audio: summary` 追加 `soundEventsMp3Seen/Played/Skipped/Failed/Inaudible` 与 `soundEventsInaudible` | 逐事件记录与汇总读数的出口。只打印 MP3：全打印会让一次运行多出数千行，把记录埋掉 |

### 3.2 探针（判据侧）

| 文件 | 改动 | 理由 |
|---|---|---|
| `native/tools/audio_scheduler_probe.cpp` | 新增 2d 节：5 条事件（可播 WAV、真实 MP3、无帧/零 PCM/LayerII 三条拒绝）走真实 `AudioEventScheduler`，读出 seen/played/missing/sinkCalls/refused 与逐事件观察结果；新增 10 个固定读数与第 11 个内部变异 `mp3_playback_drop` | 2c 只说明"字节是什么"，2d 说明"被拒绝的事件在播放边界上会怎样"：必须被计数、必须不进 sink、且每条保留自己的名字。变异用"解析器再次拒绝 MP3"的真实缺陷形态计算 |
| `native/tools/sound_corpus_scan_probe.cpp` | 每份录制新增 `mp3.{events,table,tableNoExtension,kinds}`；汇总新增 `demosWithMp3Events/mp3Events/mp3TableNames/tableNamesNoExtension/mp3EventKinds` | T7 的 55 份扫描按 kind 分类，但**没有编解码器维度**。"哪些录制真的调度了 MP3"需要新读数；无扩展名的名字单独计数，因为它们的编解码器只能由归档探测决定，扫描不猜 |

### 3.3 门禁

| 文件 | 改动 | 理由 |
|---|---|---|
| `verify-audio-content.sh`（新增） | 5 节聚焦门禁：1/5 构建+pin 输入、2/5 内容映射（2 条被调度 MP3 逐字段 pin）、3/5 null sink 播放器同窗口（逐事件 + 计数恒等式 + map join）、4/5 负向 fixture、5/5（`--device`）真实设备同窗口；`--mutation` 对 20 个捕获副本逐字段扰动并要求变红 | 任务书要求的"聚焦 forward gate + 真实 mutation gate"。既有 `verify-audio.sh` 的 MP3 读数来自合成索引表，播放器跑的是 pov（无 world 类 MP3），端到端 MP3 内容一直没有门禁 |
| `verify-audio.sh` | `mutationsCaught=10` → `11` | 探针新增第 11 个内部变异；该 pin 若不动，既有门禁会因"数字变小"而红。这是本轮对既有门禁唯一的改动 |

### 3.4 未改动的部分（有意）

- **MP3 解码路径本身**没有改：仍走 Windows ACM 的 MPEG Layer-3 编解码器（T7 落地），本轮只在它外面加可观察性。
- 时间线构建、声音字符剥离、时长上限、设备打开计数、null sink 契约均未改。
- 没有为"让门禁好写"新增产品命令行开关：设备轮询沿用 `--audio-device 6`、静默沿用 `--audio-disabled`、窗口沿用 `--hidden-window`。

## 4. 真实输入与精确命令

### 4.1 输入（size+sha256 钉住）

`$TF = D:/SteamLibrary/steamapps/common/Team Fortress 2/tf`

| 代号 | 文件 | size | sha256（前 12） | 用途 |
|---|---|---|---|---|
| cp_well / well | `$TF/demos/autorecord_2026-07-16_12-41-25.dem` | 5509069 | `5e0b1fe8d972` | 内容映射 + null/device 播放（唯一调度 world 类 MP3 的 pinned 录制） |

清单：`evidence/t31-audio-content/content-inputs.txt`（门禁只读这份，不 glob 活动目录：
缺文件 / 错大小 / 错哈希在门禁里是三个不同的 FAIL）。
扫描用的 7 份录制清单：`evidence/t31-audio-content/corpus-scan-manifest.txt`（其 size+sha256 在
`evidence/audio/content-inputs.txt` 与 `evidence/t7-audio-content/corpus-inputs.txt` 里）。

### 4.2 命令（全部在 `hostlock.sh run t31` 内执行）

```
# 1) 构建受影响目标（只这些）
bash build-target.sh audio_scheduler_probe sound_content_map_probe \
  sound_corpus_scan_probe vpk_query tf2_demo_native

# 2) 新门禁：forward（null sink）+ 捕获变异
bash verify-audio-content.sh --mutation > .scratch/t31/gate-mutation-1.log 2>&1

# 3) 新门禁：null sink 与真实设备 6 同窗口 + 两侧变异（设备可听）
bash verify-audio-content.sh --device --mutation > .scratch/t31/gate-device-1.log 2>&1

# 4) 源码级变异 → 被 forward 门禁捕获 → 逐字节复原 → 复现参考读数
bash evidence/t31-audio-content/source-mutation.sh

# 5) 既有音频门禁（在两支内容门禁都转绿之后）
bash verify-audio.sh --mutation            > .scratch/t31/verify-audio-mutation.log 2>&1
bash verify-audio.sh --device --mutation   > .scratch/t31/verify-audio-device.log 2>&1

# 6) 语料切片扫描（7 份 pinned 录制）与出货 VPK 的 MP3 普查
native/build-nmake/sound_corpus_scan_probe.exe \
  --manifest evidence/t31-audio-content/corpus-scan-manifest.txt --per-demo \
  > evidence/t31-audio-content/corpus-scan-7.jsonl
native/build-nmake/vpk_query.exe "$TF" sound .mp3 > .scratch/t31/vpk-mp3-all.txt
```

单次持锁记录（`hostlock v2.1` 日志，每段一次 `run`，无并行）：

| 序列 | ACQUIRED | 内容 | 退出 |
|---|---|---|---|
| B | 2026-10-20 12:30:14 | 构建 + forward/mutation 内容门禁 + VPK 普查 + 7 份语料扫描 | rc=0 |
| C | 2026-10-20 12:55:45 | 设备 6 门禁（同窗口）+ cp_wildcat 内容映射 + 源码变异/复原 | rc=0 |
| EF | 2026-10-20 13:17:56 | `verify-audio.sh --mutation` 与 `--device --mutation` | rc=0 |
| SM | 2026-10-20 13:38:20 | 修正后的源码变异复跑（第 7 步） | rc=0 |

四次合计持锁约 43 分钟，单次最长 17 分钟（C）。没有绕过锁跑过任何构建/启动/抓帧/变异/设备测试。

## 5. 构建与门禁退出码

### 5.1 构建身份

| 项 | 值 |
|---|---|
| 生成器 | NMake Makefiles（`CMAKE_BUILD_TYPE=Release`） |
| 编译器 | MSVC（本机 `cl.exe`） |
| 目标 | 只构建受影响目标：`audio_scheduler_probe`、`sound_content_map_probe`、`sound_corpus_scan_probe`、`vpk_query`、`tf2_demo_native` |
| 编译警告 | 构建日志 8 条 warning，**全部来自本轮未改动的行**：`main.cpp` 1103–1107（C4244 ×5）与 6203（C4457）、`native_renderer.cpp` 3642/3643（C4267）与 3950/4120（C4456）。本轮改动的 `audio_resource.cpp`、`audio_player.cpp`、`main.cpp`（3246/7046 两处）、两个探针**各 0 条**。 |

### 5.2 退出码与末行

| 命令 | 退出码 | 末行 |
|---|---|---|
| `verify-audio-content.sh --mutation`（null sink） | **0** | `VERIFY-AUDIO-CONTENT=PASS (assertions_ok=32 mutations=20/20)` |
| `verify-audio-content.sh --device --mutation` | **0** | `VERIFY-AUDIO-CONTENT=PASS (assertions_ok=39 mutations=25/25)` |
| `source-mutation.sh`（跑两次） | **1 / 1** | 两次的第 3/4/5/6 步全部 PASS（变异被捕获、探针 exit 1、复原逐字节相同、复原后门禁 PASS）；退出码 1 都只出在第 7 步的**文件比对**上（第一次抓参考的顺序错了、第二次把 trace 的毫秒前缀也算了进去），两次的实际读数见 §9.2，修正后用同一份 normalise 重算得 **IDENTICAL** |
| `verify-audio.sh --mutation` | **0** | `VERIFY-AUDIO-MUTATION=PASS` → `VERIFY-AUDIO=PASS (assertions_ok=235)`；235 OK / 0 FAIL，**25/25** 捕获 |
| `verify-audio.sh --device --mutation` | **0** | `VERIFY-AUDIO-MUTATION=PASS` → `VERIFY-AUDIO=PASS (assertions_ok=240)`；240 OK / 0 FAIL，**27/27** 捕获（25 + `device-null-attempts`、`device6-opens`） |

## 6. 正向读数：被调度的 MP3 逐事件记录

### 6.1 内容映射（同一份 pinned 录制，`content-well.tsv`）

```
# demo=autorecord_2026-07-16_12-41-25.dem origin=0 originFound=1 tickRate=66.7
# srctable=soundprecache entries=6761 decodeFailures=0 archives=4
4650  6615  world  resolved  1.4  soundprecache  sound/ambient_mp3/water/water_splash1.mp3  ambient_mp3/water/water_splash1.mp3  decoded 'sound/ambient_mp3/water/water_splash1.mp3' channels=1 rate=44100 bits=16 bytes=122112
7675  6616  world  resolved  1.6  soundprecache  sound/ambient_mp3/water/water_splash2.mp3  ambient_mp3/water/water_splash2.mp3  decoded 'sound/ambient_mp3/water/water_splash2.mp3' channels=1 rate=44100 bits=16 bytes=140544
```

同一份映射里 MP3 行共 **192** 条：被调度的 **2** 条（world），被过滤的 **190** 条
（188 voice + 2 announcer，全部 `decoded`，永不播放）。这 190 条正是门禁 2/5 钉住的
`EXPECTED_FILTERED_MP3_ROWS=190`：它证明"MP3 能解码"与"MP3 会被播放"是两件事，
不是一句"MP3 现在支持了"。

### 6.2 产品逐事件行（null sink 与设备两次运行）

见 §8 的两份记录。字段：tick / index（源表 soundprecache 索引）/ name（录制里的名字，保留原拼写）/
pcmBytes / rate / channels / bits / seconds / outcome。归一化路径（`sound/…`）来自内容映射的 path 列，
两次运行通过 tick+index 与映射 join，因此产品播的是映射点名的那两条，而不是"任意两条 MP3"。

### 6.3 汇总行（`audio: summary`）

```
soundEventsMp3Seen=… soundEventsMp3Played=… soundEventsMp3Skipped=… soundEventsMp3Failed=… soundEventsMp3Inaudible=… soundEventsInaudible=…
```

恒等式：`Seen == Played + Skipped + Failed + Inaudible`。两次运行逐项见 §8。

## 7. 负向 fixture：具名拒绝（播放边界）

`audio_scheduler_probe` 的 2d 节把 5 条事件交给真实调度器（可播 WAV、真实 MP3、三条拒绝），读出：

```
mp3PlaybackSeen=5 mp3PlaybackPlayed=2 mp3PlaybackMissing=3 mp3PlaybackSinkCalls=2
mp3PlaybackRefused=3 mp3PlaybackRefusalNames=probe_no_frame.mp3:invalid_wav,probe_sync_only.mp3:invalid_wav,probe_layer2.mp3:unsupported_format
mp3PlaybackMp3Seen=1 mp3PlaybackMp3Played=1 mp3PlaybackMp3Failed=0
mp3PlaybackObserved=probe_play_wav.wav:played,probe_play_mp3.mp3:played
```

| 输入 | 具名结局 | 读数 |
|---|---|---|
| MP3 形状但无帧 | `invalid_wav` | `mp3NoFrameStatus=invalid_wav` |
| 解码出 0 个 PCM 的流 | `invalid_wav` | `mp3SilentStatus=invalid_wav` |
| MPEG Layer II 流 | `unsupported_format` | `mp3Layer2Status=unsupported_format` |
| 本机无解码器（能力缺口） | `unsupported_format` | `mp3MapNoDecoderStatus=unsupported_format` |
| 被拒绝的事件在播放边界 | **计数**（missing +1）且**不进 sink** | `mp3PlaybackMissing=3`、`mp3PlaybackSinkCalls=2` |
| 解码正常但观察者听不到 | **具名 `inaudible`** | `soundEventsMp3Inaudible=1`（见 §8） |

## 8. null sink 与真实设备 6：同一条窗口

两张表都由门禁 3/5 与 5/5 各自读取自己的 trace（两次独立进程，读数不可能互相继承）：

| 读数 | null sink（`--audio-disabled`） | 设备 6 |
|---|---|---|
| `audio: mp3` 逐事件行数 | **2** | **2** |
| tick 4650（splash1，122112 B / 1.384 s）outcome | `inaudible` | `inaudible` |
| tick 7675（splash2，140544 B / 1.593 s）outcome | `skipped` | **`played`** |
| `soundEventsMp3Seen / Played / Skipped / Failed / Inaudible` | `2 / 0 / 1 / 0 / 1` | **`2 / 1 / 0 / 0 / 1`** |
| `audio_device_open_attempts / opens_ok / audio_device_opened` | `0 / 0 / 0` | **`1828 / 1828 / 1828`** |
| `events_seen / played / skipped / missing` | `3501 / 0 / 1955 / 1546` | `3501 / 1828 / 127 / 1546` |
| `soundEventsInaudible`（全事件） | `1267` | `1267` |
| 窗口行 | `window_visible=0 hidden=1` | `window_visible=0 hidden=1` |

null sink 的逐事件记录原文（`mp3-events-null.txt`）：

```
76817.7 audio: mp3 tick=4650 index=6615 name=ambient_mp3/water/water_splash1.mp3 pcmBytes=122112 rate=44100 channels=1 bits=16 seconds=1.384 outcome=inaudible
121304.4 audio: mp3 tick=7675 index=6616 name=ambient_mp3/water/water_splash2.mp3 pcmBytes=140544 rate=44100 channels=1 bits=16 seconds=1.593 outcome=skipped
```

设备 6 的逐事件记录原文（`mp3-events-device.txt`）：

```
62502.5 audio: mp3 tick=4650 index=6615 name=ambient_mp3/water/water_splash1.mp3 pcmBytes=122112 rate=44100 channels=1 bits=16 seconds=1.384 outcome=inaudible
99301.3 audio: mp3 tick=7675 index=6616 name=ambient_mp3/water/water_splash2.mp3 pcmBytes=140544 rate=44100 channels=1 bits=16 seconds=1.593 outcome=played
```

两次运行的对照就是本任务要的"设备与 null sink 计数"：
**同一批事件、同一窗口**，`Seen=2` 两边相同、`Inaudible=1` 两边相同、只有
`Skipped/Played` 互换（null：0/1；设备：1/0），设备侧 `attempts=opens_ok=1828=events_played`
（每次播放都真开了一次设备、每次都成功）。

（行首的 `76817.7` 是 trace 自己的运行毫秒数前缀，不是记录的字段。）

**新计数顺手暴露的一件事**：这次运行里 `events_missing=1546`，其中 **1267** 条是"资源解析成功但观察者
听不到"（`soundEventsInaudible=1267`），真正的资源缺失只有 **279** 条。没有这个具名计数时，
1546 会被读成 1546 个资源问题——把"听不见"混进"缺资源"，正是本任务要求"具名拒绝、不静默丢弃"的
同一类错误。`eventsInaudible` 在 `eventsMissingResource` 之内，所以既有门禁的
`seen == played + skipped + missing` 恒等式原样成立。

## 9. 变异

### 9.1 捕获变异（`--mutation`）

每个用例作用在 `$OUT` 里的**捕获副本**上（`cp` + `sed -i`），并用**门禁自己调用的同一个函数**重跑断言；
用例若没改动任何字节先报 `MUTATION-SETUP=FAIL`，所以"MUTATION-CAUGHT=PASS 却什么都没检查"这条路是堵住的。

null sink 一跑：**20/20 全部捕获**（`--device` 再 +3，见 §5.2 的设备跑）：

| # | 用例 | 打红断言数 | 打红了什么 |
|---|---|---|---|
| 1 | `content-seconds` | 1 / 5 | 把 splash1 的时长 1.4 改成 9.9 |
| 2 | `content-path` | 2 / 5 | 把 splash2 的归一化路径改成错名（行与 detail 两处同时红） |
| 3 | `content-tick` | 1 / 5 | 把 tick 7675 改成 7676 |
| 4 | `content-detail` | 1 / 5 | 把解码字节数 122112 改成 122113 |
| 5 | `content-filtered` | 1 / 5 | 把一条 voice 行的 status 从 resolved 改掉（filtered 计数 190→189） |
| 6 | `null-outcome` | 1 / 7 | 把 skipped 改成 played |
| 7 | `null-inaudible-name` | 1 / 7 | 把 inaudible 折叠成 skipped（本轮产品改动的反面） |
| 8 | `null-skips` | 1 / 7 | MP3 skipped 计数 1→0（恒等式红） |
| 9 | `null-inaudible-count` | 1 / 7 | MP3 inaudible 计数 1→0（恒等式红） |
| 10 | `null-plays` | 1 / 7 | MP3 played 0→1（null sink 不可能有 played） |
| 11 | `null-bytes` | 1 / 7 | 逐事件行 pcmBytes 122112→1 |
| 12 | `null-attempts` | 1 / 7 | `audio_device_open_attempts=0`→1（静默契约红） |
| 13 | `null-hidden` | 1 / 7 | `hidden=1`→0（窗口不可见契约红） |
| 14 | `scheduler-refused` | 1 / 16 | `mp3PlaybackRefused=3`→0 |
| 15 | `scheduler-missing` | 1 / 16 | `mp3PlaybackMissing=3`→2（"被计数"没了） |
| 16 | `scheduler-sink` | 1 / 16 | `mp3PlaybackSinkCalls=2`→3（拒绝的事件进了 sink） |
| 17 | `scheduler-noframe` | 1 / 16 | 无帧 MP3 从 `invalid_wav` 改成 `resolved` |
| 18 | `scheduler-provenance` | 1 / 16 | `mp3PlaybackMp3Seen=1`→0（编解码器归属消失） |
| 19 | `scheduler-refusalnames` | 1 / 16 | 拒绝名里的状态从 invalid_wav 改成 resolved |
| 20 | `scheduler-observer` | 1 / 16 | 观察器结果少掉一条 |

### 9.2 源码变异 + 逐字节复原（交付契约第 5 条）

被变异的源码是 `native/src/audio_resource.cpp` 里唯一标记"这段 PCM 来自 MP3 解码"的一行：

```cpp
out.codec = SoundCodec::Mp3;
```

**为什么是这一行**：删掉它之后解码器仍然工作、MP3 事件仍然解码并仍然播放，**没有任何总数会移动**
（`eventsPlayed`、`eventsSeen`、`soundEventsPlayed` 全部不变）。唯一移动的是本轮新增的读数：
`audio: mp3` 逐事件行消失、五个 MP3 计数归零、探针的 `mp3PlaybackMp3Seen` 从 1 变 0。
也就是说，这个变异检验的正是"本轮新增的观测器是否真的接在产物上"，而不是检验一个总数。

配方式的复现脚本：`evidence/t31-audio-content/source-mutation.sh`（内含每一步的判据）：

| 步 | 动作 | 判据 |
|---|---|---|
| 1 | 记下源码 sha256 并留一份唯一的复原副本 | — |
| 2 | 删除该行 | `cmp` 证明文件确实变了 |
| 3 | 只重建受影响目标，跑 forward 内容门禁 | **必须非 0**，且失败断言必须点名 `per-event MP3 records` |
| 4 | 单独跑 `audio_scheduler_probe` | **必须 exit 1**，且 stderr 点名 codec 归属断言 |
| 5 | 用第 1 步的副本复原 | sha256 **逐字节相同** + `cmp` 为空 |
| 6 | 重建，再跑同一支 forward 门禁 | **必须 exit 0** |
| 7 | 与变异前抓的逐事件记录对比 | `diff` 为空、sha256 相同 |

实测（`source-mutation.log`，首次运行）：

| 读数 | 值 |
|---|---|
| 源码 sha256 变异前 | `c20bddd32f943281407dbeb510958d9a50e0ea8117013935d4b00c4ee7bd9b39` |
| 源码 sha256 变异后 | `fe4cb0f6c2095ab089640ed319bb4cfe837d9cd4292ec790ed2cd7e70e01556d` |
| 源码 sha256 复原后 | `c20bddd32f943281407dbeb510958d9a50e0ea8117013935d4b00c4ee7bd9b39`（**与变异前逐字节相同**，`cmp` 亦为空） |
| 变异树重建 | `build_rc=0` |
| 变异树跑 forward 内容门禁 | **rc=1**：`assertions_ok=11 assertions_bad=21`，脚本判定 `MUTATION-CAUGHT=PASS (the forward gate failed on the per-event MP3 record)` |
| 变异树上单独跑探针 | **rc=1**，stderr 首行 `FAIL the playable MP3 is attributed to the MP3 codec at the playback boundary` |
| 复原树重建 | `build_rc=0` |
| 复原树跑同一支门禁 | **rc=0**，`VERIFY-AUDIO-CONTENT=PASS (assertions_ok=32)` → `REFERENCE-REPRODUCED=PASS` |
| 第 7 步（与变异前的逐事件记录 `diff`） | **IDENTICAL**（见下） |

第 7 步两次都先报了失败，原因不是读数变了，而是**比对的对象不对**，两次都记在这里：

1. **第一次运行**：脚本在"重建并跑门禁之后"才去抓参考文件，那时 `mp3-events-null.txt` 已被变异树
   那次运行覆盖成空文件，于是只能读 `NOT-COMPARED`。已修：参考文件改在变异**之前**抓
   （`source-mutation.sh` 第 1 步的注释写明为什么）。
2. **第二次运行**：第 7 步报 `DIFFERS`，但 4 行 diff **全部是行首的毫秒前缀**
   （`65897.6 audio: mp3 …` vs `58759.3 audio: mp3 …`）——那是 trace 自己的墙钟时间，不是记录字段。
   已修：比对前用 `sed -e 's/^[0-9][0-9.]* audio: mp3/audio: mp3/'` 归一化（门禁的 `mp3_segments`
   本来就用同一条表达式剥这个前缀，所以"门禁读的记录"和"这里比的记录"是同一个东西）。

用修好的 normalise 在**已经捕获的两份文件**上重算（不需要再跑产品，因为比对是纯文本操作）：
`evidence/t31-audio-content/source-mutation-step7.txt`

```
reference sha256 (normalised): 210998646dd7d118bc64093ff8282a5b4c5efce3ace2cc3fb6ea8328bcfa679f
restored  sha256 (normalised): 210998646dd7d118bc64093ff8282a5b4c5efce3ace2cc3fb6ea8328bcfa679f
PER-EVENT-RECORD=IDENTICAL after stripping the elapsed prefix (2 lines)
audio: mp3 tick=4650 index=6615 name=ambient_mp3/water/water_splash1.mp3 pcmBytes=122112 rate=44100 channels=1 bits=16 seconds=1.384 outcome=inaudible
audio: mp3 tick=7675 index=6616 name=ambient_mp3/water/water_splash2.mp3 pcmBytes=140544 rate=44100 channels=1 bits=16 seconds=1.593 outcome=skipped
```

于是契约第 5 条的两半都齐了：**源码逐字节复原**（sha256 前后相同）与**复原后复现参考读数**
（门禁的 pinned 记录全绿 + 逐事件记录归一化后 sha256 相同）。

## 10. 语料扫描与 MP3 普查

### 10.1 7 份 pinned 录制的 MP3 感知扫描（`corpus-scan-7.jsonl`）

```
{"demos":7,"demosFailed":0,"timeline":34575,"mp3Events":2685,
 "demosWithMp3Events":7,"mp3TableNames":35384,"tableNamesNoExtension":0,
 "mp3EventKinds":{"world":5,"voice":2664,"announcer":16,"music":0,...},
 "eventKinds":{"weapon":7755,"footstep":13249,"uber":69,"world":10816,
               "voice":2664,"announcer":16,"music":6,"temp_entity":0,"pcf":0}}
```

逐份（只列 MP3 维度）：

| 录制 | 地图 | timeline | MP3 事件 | 其中 world | 其中 filtered |
|---|---|---|---|---|---|
| autorecord_2026-07-02_13-26-46（pov） | pl_odyssey | 5323 | 511 | **0** | 511（504 voice + 7 announcer） |
| autorecord_2026-07-16_12-41-25（well） | cp_well | 4989 | 192 | **2** | 190（188 voice + 2 announcer） |
| matcha（cp_snakewater） | cp_snakewater_final1 | 5229 | 637 | **0** | 637（633 voice + 4 announcer） |
| autorecord_2026-10-04_12-08-10 | pd_circus | 4930 | 272 | 0 | 272 voice |
| autorecord_2026-10-02_14-26-03 | cp_manor_event | 4306 | 479 | 0 | 479 voice |
| autorecord_2026-10-02_13-20-12 | cp_wildcat_event | 5669 | 234 | **3** | 231（229 voice + 2 announcer） |
| autorecord_2026-10-12_16-47-40 | plr_hightower_event | 4129 | 360 | 0 | 360（359 voice + 1 announcer） |

三个结论，都可以从这张表直接读出来：

1. **"MP3 能解码"几乎全是 voice 的事**：2685 条 .mp3 命名事件里 2664 条是 voice、16 条 announcer，
   这两类都是 filtered，解析出来也永不播放。把它们算作"MP3 播放覆盖"是本仓库反复警告的那种读法。
2. **真正被调度的 world 类 MP3 只有 5 条，分布在 2 份录制**：cp_well 两条（本轮门禁 pin 住的两声水花）
   与 cp_wildcat_event 三条（§10.3 列出）。所以"选 cp_well 作为 MP3 内容门禁的输入"是有语料依据的，
   不是随口挑的。
3. **music 事件一条都不是 .mp3 命名**：`mp3EventKinds.music=0`，而 `eventKinds.music=6`
   （pd_circus 3、cp_manor_event 1、plr_hightower_event 2）。这与 T7 观察到的 4 个 music 名字全是
   `.wav` 一致：music 走 WAV，不构成 MP3 的内容缺口（它照旧是 filtered、永不播放）。
4. `tableNamesNoExtension=0`：7 份录制的 35384 条 .mp3 表名与全部 precache 名都带扩展名，
   所以"按名字判 MP3"在这批输入上不是猜测（这也是本扫描唯一敢按名字判 MP3 的理由；
   扫描不打开 VPK，解码与否由门禁的 2/5 负责）。

### 10.2 出货 VPK 的 MP3 普查（index-only，`mp3-census.txt`）

```
pak01_dir.vpk: OPEN FAILED
tf2_misc_dir.vpk: 104137 entries, 0 match 'sound' ext=.mp3
tf2_textures_dir.vpk: 29639 entries, 0 match 'sound' ext=.mp3
tf2_sound_misc_dir.vpk: 3230 entries, 472 match 'sound' ext=.mp3
tf2_sound_vo_english_dir.vpk: 12728 entries, 12668 match 'sound' ext=.mp3
total sound/**/*.mp3 = 13140
  vo: 12668   ambient_mp3: 335   ui: 55   commentary: 49
  misc: 16    items: 14   mvm: 1   music: 1   replay: 1
```

这张表回答的是"MP3 内容有多少"，与"被调度了多少"是两件事：**335 个 ambient_mp3 文件**在库存里，
7 份录制只调度了其中 **5** 个 world 类事件。`pak01_dir.vpk` 在本机打不开（本机安装的归档名不同），
是如实记录的读数，不是本任务需要修的缺陷。

### 10.3 语料里那 3 条 cp_wildcat_event 的 world MP3 事件

```
2672  6642  world  resolved  2.1  soundprecache  sound/ambient_mp3/water/water_splash3.mp3  ambient_mp3/water/water_splash3.mp3  decoded … channels=1 rate=44100 bits=16 bytes=182016
4965  6640  world  resolved  1.4  soundprecache  sound/ambient_mp3/water/water_splash1.mp3  ambient_mp3/water/water_splash1.mp3  decoded … channels=1 rate=44100 bits=16 bytes=122112
4965  6640  world  resolved  1.4  soundprecache  sound/ambient_mp3/water/water_splash1.mp3  ambient_mp3/water/water_splash1.mp3  decoded … channels=1 rate=44100 bits=16 bytes=122112
```

第三条是同一 tick、同一 soundprecache 索引的**第二条事件**（同一 tick 上两个实体各触发一次水花），
所以"3 条事件、2 个不同索引"是同一件事的两种数法。这三条没有进本轮门禁：门禁只 pin 一份录制
（cp_well）以保证"同窗口"可复现；它们在这里的作用是证明"world 类 MP3 事件不只在 cp_well 出现"，
即 §10.1 的 5 条不是单份录制的偶然。

## 11. 证据清单与哈希

提交目录：`evidence/t31-audio-content/`（每个文件的用途见该目录的 `README.md`）。
`SHA256SUMS` 覆盖 20 个**读数**文件（脚本与 README 不参与，它们由 git 跟踪、不是关于产品的证据）：

| 文件 | sha256（前 16） |
|---|---|
| `content-inputs.txt` | `1674cf42ced63948` |
| `content-well.tsv` | `f4668f66e0905246` |
| `corpus-scan-7.jsonl` | `68790093eb0c0f3d` |
| `corpus-scan-manifest.txt` | `de5ec1cb8b51e5f5` |
| `corpus-world-mp3-rows-wildcat.tsv` | `242c00c87bf73742` |
| `device-enumeration.txt` | `b5f88f4317b3344c` |
| `device-run-readings.txt` | `d200a25c6721aaca` |
| `gate-device.log` | `596d69a6d0915515` |
| `gate-forward-first-run.log` | `eeea28d8b79850b6` |
| `gate-mutation.log` | `64c853bbf5c00e93` |
| `mp3-census.txt` | `17356f93c2d23d12` |
| `mp3-events-device.txt` | `3ce2b766851b026e` |
| `mp3-events-null.txt` | `cdd74d3d1185674e` |
| `null-run-readings.txt` | `6e5ba01fa6c05301` |
| `refusal-probe-readings.txt` | `9daba9d861f99885` |
| `source-mutation-step7.txt` | `e45d55a74c194c9f` |
| `source-mutation.log` | `6159ac3ffbc1bf91` |
| `verify-audio-device.log` | `73019a54bfb265ab` |
| `verify-audio-mutation.log` | `5e970b20d561f802` |
| `window-advance-lines.txt` | `7c44842677f4210b` |

复核：

```
cd evidence/t31-audio-content
tr -d '\r' < SHA256SUMS | sha256sum -c -     # 实测 20/20 OK
```

**行尾说明（做过的，不是假设）**：本仓库 `core.autocrlf=true`，工作副本是 CRLF 而提交 blob 是 LF。
`SHA256SUMS` 记录的是**工作副本字节**（CRLF）的哈希，随后由 `git add` 规范成 LF 入库，所以
校验时必须先用 `tr -d '\r'` 去掉 sums 文件自身的行尾——否则 `sha256sum` 会把文件名读成带 \r 的名字。
这与 T3/T7 记过的同一个坑是同一条约定；`collect.sh` 在 LF 工作副本上重跑会得到一组 LF 哈希，
那是另一种度量，不要与本表混读。

**不进 `SHA256SUMS`、也不提交的**：`evidence/fast/audio-content/` 下的原始 trace（约 2×1.5 MB 的逐帧行）、
两张 2.5 MB BMP、每个变异副本，以及 `.scratch/t31/` 下的构建日志。它们的作用是"断言当时读的就是这些字节"，
每次运行都会被覆盖；提交的是从它们里抽出来的读数。`collect.sh` 就是那个抽取器，可以重跑。

## 12. 未验证项（NOT VERIFIED）

| 项 | 状态 | 依据 / 最便宜的下一步 |
|---|---|---|
| `temp_entity` 类音频事件（真实内容） | **NOT VERIFIED** | 语料里没有输入：T7 在两个出货音效归档的 15958 条预缓存名上量到 `realTempEntity=0 realPcf=0`，55 份录制扫描 0 个事件；本轮 7 份扫描同样只读名字与事件，不制造覆盖。最便宜下一步：等某个录制真的发出带该 token 的名字（探针的 `realTempEntity`/`realPcf` 会先变红），或改用 `audio_effects_probe` 的合成 TempEntity/PCF 路径 |
| `pcf` 类音频事件（真实内容） | **NOT VERIFIED** | 同上；PCF operator 路径已有 `audio_effects_probe` 对真实 PCF 的覆盖，但那是效果层，不是"被调度的声音事件" |
| music 事件的 MP3 维度 | **NOT VERIFIED（本轮窗口内）** | 本轮 pinned 的 cp_well 里 music 事件为 0；7 份录制扫描给出"music 事件里有没有 .mp3 命名"的读数（见 §10）。T7 已证明 music 是 filtered 类、从不播放，其观测到的 4 个 music 名字都是 `.wav`。最便宜下一步：给 5 个 music 载体之一加一个同窗口播放器运行（约 2 分钟） |
| 人耳是否真的听到、听到的内容是否正确 | **NOT VERIFIED** | 本轮只记产品计数；没有请人耳确认，也没有与已知良好播放器对照。设备 6 的读取是"产品自己数到 played"，不是听觉证据。最便宜下一步：由人耳在同一条命令上听 30 秒并另记一份陈述（T3/T7 的做法） |
| 全语料 1687 份的穷尽 MP3 计数 | **NOT VERIFIED** | 41.13 GiB / 约 13 小时（T7 实测 0.89 MiB/s 外推），不是门禁规模。最便宜下一步：用本轮已扩展的 `sound_corpus_scan_probe` 跑 T7 写好的 55 份 manifest（约 20 分钟） |
| 7 份以外录制的 MP3 事件 | **NOT VERIFIED** | 本轮只扫 7 份 pinned 录制；"MP3 事件只出现在 cp_well"这句话只在 7 份里成立 |
| device 6 以外的设备、独占模式、热插拔 | **NOT VERIFIED** | 沿用 T3/T7 的边界；本轮只碰 device 6 |
| MP3 播放的相对音量/延迟/空间化正确性 | **NOT VERIFIED** | 逐事件记录只有 outcome 与 PCM 形状；没有做响度、延迟或与 WAV 同类事件的对照 |

## 13. 干净工作树与残留进程

- **交付后工作树干净**：提交前后各查一次 `git status --short`；本报告最后一个提交之后应为空输出。
- **本任务没有残留进程**。判定方式不是"任务管理器里有没有同名进程"，而是按命令行匹配本工作树路径：
  `Get-CimInstance Win32_Process | Where-Object { $_.CommandLine -like '*t31-audio-content*' }`
  → 只匹配到查询自身的 PowerShell / Harness 进程；没有任何 `tf2_demo_native.exe`、`*probe.exe`、
  `nmake.exe`、`cl.exe` 引用本工作树。
- **主机锁**：本任务四段 `run` 全部打印 `ACQUIRED` 并以 `LOCKED-COMMAND-EXIT rc=0` 结束（§4.2）。
  交付复核时锁不在本任务手上（那时 `owner=t30`），所以 `hostlock.sh status` 里若出现
  `product_processes=1`，那是别的任务的进程；本任务是否干净以上一条的命令行匹配为准。
- **机器产物（不提交）**：`.scratch/t31/` 47 文件 / 1.8 MB；`evidence/fast/audio-content/` 62 文件 / 99.5 MB
  （两张 2.5 MB BMP、两份逐帧 trace、25 个变异副本）；`native/build-nmake/` 551 文件 / 59.9 MB。
- **提交的证据目录**：24 文件 / 1.0 MB（其中 `content-well.tsv` 954 KB 是九列全量内容映射）。
- **可运行性**：`native/build-nmake/` 目标齐全，`tf2_demo_native.exe` 与四个音频探针在提交树上可直接运行
  （§4.2 的命令都以 exit=0 复现过）。

## 14. 未提交文件与证据来源

- `.scratch/t31/`：构建日志、门禁中间日志、变异副本、源码变异工作区。`.gitignore` 第 31 行已忽略。
- `evidence/fast/audio-content/`：门禁输出目录（内容映射 tsv、两侧 trace、BMP 帧、变异副本）。被
  `.gitignore` 的 `evidence/fast/` 覆盖。提交的是 `collect.sh` 复制出来的**冻结读数**。
- `native/build-nmake/`：构建产物，已忽略。
- 证据来源：§6/§7/§8 的读数来自本轮的两次门禁运行与探针输出；§10 的扫描来自本轮
  `send_corpus_scan` 运行；T7 的 55 份扫描只作为**类别覆盖基线**引用（未重跑），其数字在
  `evidence/t7-audio-content/` 中。

## 15. 跨任务依赖

1. `verify-audio.sh` 的 `mutationsCaught` pin 从 10 改为 11：任何在 T31 之后合并、且改动
   `audio_scheduler_probe` 变异数的分支，必须把这个 pin 一起改。合并顺序若把 T31 排后，
   以合并后最终树的 `mutationsCaught/mutationsTotal` 为准重数一次。
2. `native/include/audio_player.h` 的 `AudioScheduleStats` 与 `AudioEventScheduler` 是共享接口：
   新增的 5 个 MP3 计数、`eventsInaudible`、`ContentObserver` 都是**追加**，没有改既有字段语义；
   任何并行任务若也在动这两个类，应保留追加部分。
3. `verify-all.sh --quick` 总链未跑（任务书要求 T28 才跑）；本轮没有新增可执行目标，故
   `exe_count` pin 不变（33）。
