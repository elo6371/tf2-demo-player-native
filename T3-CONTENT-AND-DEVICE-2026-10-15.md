# T3 交付：音频内容类别与真实设备验收（2026-10-15）

本文件是 T3（`NEXT-TASKS-2026-10-15.md` §6）的交付记录。每个数字都来自一次真实运行，
命令在 §4、证据在 §8，可按图索骥重跑。凡未跑到的写在 §9，能力边界写在 §10。

**一句话结论**：把音频证据从「时间线定位」推进到「内容与真实设备」的过程中，先发现了
一个**真实解析缺陷**——TF2 的 soundprecache 会把 Source「声音字符」（`soundchars.h`：
`*`=STREAM、`#`=DRYMIX、`)`=SPATIALSTEREO、`>`=DOPPLER、`^`=DISTVARIANCE，可叠两个）写在
文件名前面，而名字→资源路径的转换按原文使用，于是**所有带标记的库存声音全部解析失败、永不播放**。
pov 录制 6720 条 precache 名字里 713 条带标记，旧转换解析成功 **0 条**；修复后 696 条解析到
真实文件（其余 17 条是服务器自定义内容，两种转换都缺失）。整条时间线上 pov 的
`statusResolved` 2819→**3564**、`statusMissingWav` 1993→**1248**；真实设备（device 6）同一
tick 窗口的 `events_played` 601→**742**。同轮补齐：时长上限命名拒绝（`excessive_duration`）、
逐事件可追溯内容映射探针、`device_opened/attempts/opens` 三件套读数、null sink 与 device 6
分开验收，以及**用户人耳确认**（本次为「听到了声音」，与自动计数分开记录）。

---

## 1. 任务与工作区

| 项 | 值 |
|---|---|
| 任务 | T3 音频内容与真实设备验收（`NEXT-TASKS-2026-10-15.md` §6） |
| 工作区 | `D:\TF2_Native_Worktrees\audio-content-device` |
| 分支 | `task/audio-content-device` |
| 基线 commit | `917b012`（`docs: record final chain pin status and task plan`，即当前整合 HEAD） |
| 产品/门禁提交 | A：`8e06aba` `fix(audio): strip Source sound characters before resolving a precache name` |
| 附带提交 | `dc52027` `chore: drop a stray merge marker from the changelog`（见 §3.4） |
| 门禁自身修复 | `270d5b4` `exe_count` pin 31→32；`9864a93` 带标记名字三态拆分；`d063747` 播放器判据改为与负载无关；`f745bcd` 设备变异只在 `--device` 下跑（后三条见 §7） |
| 证据/文档提交 | B：本文件所在提交 |
| 是否可运行 | 是。`native/build-nmake/tf2_demo_native.exe`，退出码 0 |

未回写整合主线（`D:\TF2_Native_Test` 只读）；`D:\TF2_Demo_Player` 只读。
未改协议 bit reader、未改渲染器；产品改动全部落在音频资源/时间线层与 `mode: summary` 的音频字段。

## 2. 本轮做了什么（对照 §6 清单）

| §6 要求 | 结果 |
|---|---|
| 1. 选样覆盖 weapon/footstep/Uber/TempEntity/PCF/voice/music/announcer | 扫描 11 份录制；选定 3 份 pin 住（见 §2.1）。weapon/footstep/uber/world/voice/announcer 有真实事件；TempEntity/PCF 零（发放名字不含该 token，探针每轮实测量）；music 事件零，但每份录制的 soundprecache 表里带 13–15 条 music 候选（taunt/MvM/round-end） |
| 2. 可追溯映射（index/表 → 路径、tick、来源表、分类、解码状态） | 新探针 `sound_content_map_probe`：每个入表事件一行 `tick/index/kind/status/seconds/srctable/path/name/detail`；另可 `--table-out` 导出整张 soundprecache。门禁断言「映射行数 == 时间线大小 == 文件数据行数」 |
| 3. 七类计数分开 | `read=5563 / rejected=240(=unresolved 240+unplaceable 0+refused 0) / scheduled=5323 / resolved 3564 / filtered 511 / missing_wav 1248 / duplicate×1 / played 3564`（pov；全部在 JSON 与门禁断言里） |
| 4. null sink 与 device 6 分开运行，记录 device_opened 与 device_open_attempts | `mode: summary` 新增 `audio_device_open_attempts` / `audio_device_opens_ok`。null：`opened=0 attempts=0 opens_ok=0 played=0`；device 6：`opened=742 attempts=742 opens_ok=742 played=742` |
| 5. 负例：missing WAV / malformed WAV / unsupported format / **excessive duration** / duplicate tick / backward playback | 前五个均为具名状态（新增 `excessive_duration`）；后两者在 census、调度器探针、内容探针三处各自断言。构造的 48 天 WAV 被拒；同一字节在 8000 Hz 下（524 s）正常解析——对照组证明拒绝来自时长而非形状 |
| 6. 人耳确认与自动计数分开 | `evidence/audio/listening.txt`：用户确认「听到了声音」（device 6、pov、tick 2000 窗口）；该记录与所有计数器分离存放 |
| 7. 每次产品/门禁改动后重跑正向与变异 | 见 §5：`verify-audio.sh` 129 条断言、`--mutation` 21 个（`--device` 时 23 个）全部捕获 |

### 2.1 选样与扫描（11 份）

扫描口径：`sound_content_map_probe --demo <f>`（只用固定清单里的文件；Steam demos 目录是活的，
不 glob）。类别覆盖（事件数）：

| demo | map | w | f | u | world | temp | pcf | voice | ann | music | 表内 music 候选 | marker 事件 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| autorecord_2026-07-02（pov，已 pin） | pl_odyssey | 888 | 2263 | 14 | 1647 | 0 | 0 | 504 | 7 | 0 | 13 | 745 |
| matcha（已 pin） | cp_snakewater | 2953 | 5 | 14 | 1620 | 0 | 0 | 633 | 4 | 0 | 13 | 1460 |
| autorecord_2026-07-16（well，已 pin） | cp_well | 675 | 3003 | 7 | 1114 | 0 | 0 | 188 | 2 | 0 | 13 | 723 |
| autorecord_2026-08-20 | koth_harvest | 2078 | 1343 | 0 | 1547 | 0 | 0 | 342 | 0 | 0 | 15 | 1417 |
| autorecord_2026-06-24 | pl_citadel | 1696 | 1734 | 0 | 1737 | 0 | 0 | 478 | 2 | 0 | 13 | 806 |
| autorecord_2026-07-19 | pl_embargo | 475 | 1060 | 9 | 728 | 0 | 0 | 205 | 0 | 0 | 13 | 235（228 解出，7 条自定义） |
| autorecord_2026-08-26 | pl_goldrush | 82 | 195 | 0 | 14 | 0 | 0 | 24 | 0 | 0 | 13 | 46 |
| autorecord_2026-08-02 | cp_dustbowl | 17 | 98 | 0 | 171 | 0 | 0 | 43 | 0 | 0 | 13 | 108（108 解出） |
| autorecord_2026-08-31 | pl_barnblitz | 95 | 197 | 0 | 192 | 0 | 0 | 74 | 0 | 0 | 13 | 71（71 解出） |
| autorecord_2026-06-16 | pl_aquarius | 145 | 525 | 0 | 156 | 0 | 0 | 110 | 0 | 0 | 13 | 150（140 解出 + 10 其他具名状态，见 §7.1 第 2 条） |
| autorecord_2026-05-18 | pl_swiftwater | 173 | 100 | 0 | 74 | 0 | 0 | 13 | 0 | 0 | 15 | 112（112 解出） |

（"表内 music 候选"= 该录制 soundprecache 表里 `music/` 类名字的数量，`tableKindMusic` 读数；
"marker 事件"= 名字带声音字符的已入时间线事件数，修复后计数。11 份逐份读数见
`evidence/audio/content-scan.txt`——后四份在这里补上，是为了让"类别覆盖不是 pov/matcha 的特例"
这句话自己有数据支撑，而不是只在前七份成立。）

选定 pin 住的三份：**pov**（原点缺陷的那份 + 类别最全）、**matcha**（防止 pov 数字被读成一般
规律）、**well**（footstep 主导 + uber/announcer 的对照密度）。pin 清单：
`evidence/audio/content-inputs.txt`（size+sha256，缺文件/错大小/错哈希分别报错）。

## 3. 修改文件与原因

### 3.1 产品（提交 `8e06aba`）

| 文件 | 改动与原因 |
|---|---|
| `native/src/audio_resource.cpp` | 新增 `isSoundCharacter`/`leadingSoundCharacterCount`（至多两个，文档口径）；`normalizeSoundPath` 在**唯一一处**「名字→归档路径」转换点去掉声音字符。名字本身保留，映射仍能看到录制里存的是什么 |
| `native/include/audio_resource.h` | 声明上述助手；`normalizeSoundPath` 文档说明为何剥离、为何不剥名字本身 |
| `native/src/audio_timeline.cpp` | 新增 `SoundResolveStatus::ExcessiveDuration` 与 `wavDurationSeconds`/`wavWithinDurationCap`（上限 600 s；速率不可测→拒绝，不做除零）；抽出 `classifySoundResourceBytes`（具名链的字节级一半，索引链与构造字节路径共用一份实现）；`readSoundResource` 同样落上限 |
| `native/include/audio_timeline.h` | 上述声明与文档 |
| `native/src/main.cpp` | `mode: summary` 增加 `audio_device_open_attempts` / `audio_device_opens_ok`（历史字段 `audio_device_opened` 保持原义为调用计数） |
| `native/CMakeLists.txt` | 注册 `sound_content_map_probe` 目标 |

### 3.2 探针与门禁（提交 `8e06aba`）

| 文件 | 改动与原因 |
|---|---|
| `native/tools/sound_content_map_probe.cpp`（新） | 逐事件可追溯映射 + 状态/类别/调度七类计数；`--map-out` 全量映射、`--table-out` 整表导出；永不打开设备 |
| `native/tools/audio_scheduler_probe.cpp` | 负例 `indexExcessiveDuration`（48 天 WAV 被拒 + 同字节 524 s 对照组）；真实 VPK 上的带标记名字正例（`indexMarkedResolved=2`、两条路径读数）；内部变异 6→8（`duration_cap`、`marker_strip`）；固定契约新增 8 个键 |
| `verify-audio.sh` | 4/5 内容映射节（3 份 pinned 输入、每个 demo 9 类断言 + 逐项 pin）；5/5 静默播放器节新增设备三件套断言；`--device` 可选节（null 与 device 6 两次运行、对比断言、含单次受控 open/write/reset/close）；变异 12→20（`--device` 时 22） |
| `evidence/audio/content-inputs.txt`（新） | 内容节的 pinned 输入清单（门禁的输入文件，与 `timeline-inputs.txt` 同规格） |

### 3.3 链条 pin（提交 B 中单列，见 §5.3）

| 文件 | 改动与原因 |
|---|---|
| `verify-all.sh` | `exe_count` 31→32：本轮新增 `sound_content_map_probe` 一个目标。注释按既有约定点名了这次新增；计数由 `grep -c '^add_executable('`=32 与全量构建实打印 `exe_count=32` 证明 |
| `verify-fast.sh` | 注释里的 `exe_count=31` 同步为 32（仅注释） |

### 3.4 附带修复（提交 `dc52027`，可单独回退）

`CHANGELOG.md` 第 23 行自 `95291f3` 起带着一个冲突标记
`>>>>>>> 95291f3 (docs: add T3 manual follow-up report and sync status docs)`——不是内容，
是遗留物。准备本轮 T3 条目时发现，单列一个提交删除该行；除这一行没有其它改动。

## 4. 真实输入与精确命令

输入（全部 size+sha256 pin 住）：

| 文件 | size | sha256（前 12） |
|---|---|---|
| `$TF/demos/autorecord_2026-07-02_13-26-46.dem`（pov） | 21856667 | `c8a58501d222` |
| `D:/TF2_Demo_Player/testdata/demos/bb841c6d…cp_snakewater_final1.dem`（matcha） | 51834617 | `308cc1453672` |
| `$TF/demos/autorecord_2026-07-16_12-41-25.dem`（well） | 5509069 | `5e0b1fe8d972` |

`$TF = D:/SteamLibrary/steamapps/common/Team Fortress 2/tf`（本机 2026-10-15 的安装）。

```bash
cd /d/TF2_Native_Worktrees/audio-content-device

# 1) 正向（含构建；§5 表里的 F）
bash verify-audio.sh

# 2) 变异（默认，20 个）
bash verify-audio.sh --mutation --no-build

# 3) 设备验收（可听；null 与 device 6 分开跑，含其变异）
bash verify-audio.sh --device --mutation --no-build

# 4) 单目标复核（内容映射，pov 例）
native/build-nmake/sound_content_map_probe.exe \
  --demo "$TF/demos/autorecord_2026-07-02_13-26-46.dem" --tf-root "$TF" \
  --map-out evidence/fast/audio/content-sample.tsv

# 5) 人耳确认所用命令（device 6；与门禁 --device 节的 device 运行同一条）
TF2_NATIVE_TRACE=.scratch/t3/listen.trace native/build-nmake/tf2_demo_native.exe \
  --demo "$TF/demos/autorecord_2026-07-02_13-26-46.dem" --audio-device 6 \
  --capture-tick 2000 --capture-frame .scratch/t3/listen.bmp
```

## 5. 构建与门禁退出码（最终提交树上）

### 5.1 音频档（T3 的命令清单）

| 命令 | 退出码 | 末行 |
|---|---|---|
| `bash verify-audio.sh`（提交后 F 运行） | 0 | `VERIFY-AUDIO=PASS (assertions_ok=129)` |
| `bash verify-audio.sh --mutation --no-build`（M） | 0 | `VERIFY-AUDIO-MUTATION=PASS` → `VERIFY-AUDIO=PASS (assertions_ok=129)`；21/21 捕获 |
| `bash verify-audio.sh --device --mutation --no-build`（D） | 0 | 同上 129 条 + 设备节 5 条 + 23/23 捕获 |
| `audio_scheduler_probe --self-test` | 0 | `… mutationsCaught=8 mutationsTotal=8`（见 §6.2） |

（F/M/D 的完整日志见 `evidence/audio/verify-audio-*.txt`。129 的构成：原 48 + scheduler 新键 7 条
+ 内容节 73 条（3 份输入 × [2 条输入校验 + 1 条运行 + 9 条断言] + 逐项 pin 12/12/13）
+ 静默播放器设备三件套 1 条 = 129；逐条清单见 F 日志。）

### 5.2 与 `verify-all.sh` 有关的说明

本任务**不**运行 `verify-all.sh --quick`（它属于 T5 的串行总链，且不得与其它任务的构建/变异并发）。
本轮唯一影响总链的改动是 `exe_count` pin（31→32，见 §3.3）与其证明：全量构建输出
`exe_count=32`（日志见 §8）。T1/T2/T4 若也增加目标，T5 合并后需一次性按最终树重新数一遍——
这正是注释里写的约定。

### 5.3 未改动 `mutate.sh` 的理由

T3 的验收命令只有 `verify-audio.sh`（正向+变异）；其变异全部作用在**捕获文件副本**与探针内部
翻转上，不触碰源码，因此没有"源码复原"环节（见 §7）。源码级变异属于 `mutate.sh`/verify-all
链，由 T5 统一决定，本轮不越界。

## 6. 具名正向读数与负例读数

### 6.1 缺陷与修复的读数对照（同一批 pinned 输入）

| 读数 | 修复前 | 修复后 |
|---|---|---|
| 表可达性（pov，6720 条）：旧转换 vs 新转换 | 旧解析 5631；713 条带标记名字解出 **0** | 新解析 6327；713 条带标记解出 **696**（17 条自定义内容两边都缺） |
| 内容探针 pov `statusResolved` / `statusMissingWav` | 2819 / 1993 | **3564 / 1248** |
| 内容探针 matcha | 2048 / 2543 | **3508 / 1083** |
| 内容探针 well | 3648 / 1149 | **4371 / 426** |
| 播放器 pov 前 750 事件（`--audio-disabled`）`events_missing` | 283 | **185** |
| 设备 6 pov tick 2000 窗口 `events_played` / `events_missing` | 601 / 388（`T3-MANUAL-FOLLOWUP-2026-10-14.md` §7） | **742 / 247** |

### 6.2 修复后的最终读数（提交树上，节选）

scheduler 探针一行（真实 VPK）：

```text
… wavMalformed=3 indexExcessiveDuration=1 durationOverCapSeconds=4194304
durationUnderCapSeconds=524 … indexMarkedResolved=2
indexMarkedPath=sound/weapons/shotgun_shoot.wav readMarkedPath=sound/weapons/shotgun_shoot.wav
… mutationsCaught=8 mutationsTotal=8
```

内容探针（pov，`--map-out` 5323 行）：

```text
read=5563 unresolved=240 unplaceable=0 refused=0 rejected=240 scheduled=5323 mapLines=5323
kinds w888/f2263/u14/o1647/v504/a7/m0（与播放器 audio: timeline kinds= 逐项一致）
status: resolved 3564 / missing_precache 0 / missing_wav 1248 / invalid_wav 0 /
        unsupported_format 511 / excessive_duration 0
markerEvents=745 markerEventsResolved=745 markerEventsMissing=0
seen=5323 = played 3564 + voice 511 + missing 1248；played == statusResolved
replay 窗口 [7364,29456]：seen 1344 == expected 1344；duplicate×1
maxResolvedSeconds=10（上限 600，真实内容不触发）
```

播放器 device 6（人耳确认同一条命令，tick 2000 窗口）：

```text
mode: summary … audio_mode=device audio_device_opened=742 audio_device_open_attempts=742
             audio_device_opens_ok=742 events_seen=1037 events_played=742
             events_skipped=48 events_missing=247 exit=0
```

负例/拒绝读数（全部具名，无 SKIP 冒充）：

| 负例 | 读数 |
|---|---|
| 缺 WAV | `indexMissingWav=1`；真实数据上 `statusMissingWav>0`（自定义内容） |
| 畸形 WAV | `wavMalformed=3`（空/截断/chunk 超界三种，各带具名 error） |
| 不支持格式 | `indexUnsupportedFormat=1`（真实 MP3）；真实数据 511 条 = 全部 voice/announcer（MP3） |
| **超长时长** | `indexExcessiveDuration=1`（4194304 s 被拒；同字节 524 s 正常） |
| 同 tick 重复 | census/内容/调度器三处：`seenAfter == seenBefore`、`duplicateTicks>=1` |
| 回退播放 | `replaySeen == replayExpected`（三处独立窗口）、正向 `reverseRecoveries=0` |
| 带标记名字找不到（旧缺陷形状） | 内部翻转 `marker_strip` 被捕获（3 个键同时变红） |

### 6.3 设备三件套（两个运行分别记录）

| 运行 | 命令差异 | `audio_device_opened`（调用） | `audio_device_open_attempts` | `audio_device_opens_ok` | `events_played` |
|---|---|---|---|---|---|
| null sink | `--audio-disabled --audio-device 6` | 0 | 0 | 0 | 0 |
| device 6 | `--audio-device 6` | 742 | 742 | 742 | 742 |

外加单次受控开合（静默缓冲）：`winmm_sink_probe --device 6` → `device_count=13 device_opened=1
playback_written=1 … status=pass`（rc=0）。

### 6.4 人耳确认（与计数分开）

- 结论：**用户确认听到了声音**（2026-10-15 本机时间约 22:42，device 6「耳机 (xduudio audio)」，
  pov、tick 2000 窗口 ≈30 秒）。
- 该结论是**人的陈述**；上面的 `played=742` 是**产品计数**。两者并存、互不背书：
  - 计数不证明人听到；人耳不证明听到的是正确的内容（曲目/音量/延迟未对照）。
- 完整记录：`evidence/audio/listening.txt`。

## 7. 变异结果与源码复原证明

- `verify-audio.sh --mutation`（M 运行）：**21/21** 全部 `MUTATION-CAUGHT=PASS`，每个都要求一条
  具名断言变红（`scheduler-duration`、`scheduler-marker`、`player-attempts`、`player-accounting`、
  `content-status`、`content-maplines`、`content-marker`、`content-duplicate`、`content-reverse` 等）。
- `--device` 时再 +2：`device-null-attempts`、`device6-opens`，共 **23/23**。
- `audio_scheduler_probe` 内部 8 个翻转（含 `duration_cap`、`marker_strip`）在正跑中强制自检：
  `mutationsCaught=8 mutationsTotal=8`。
- **源码复原**：本轮所有变异都作用于**捕获文件副本**与**探针内存态**，不写源码；树内无
  `patch_in` 型变异，故没有"复原后逐字节一致"环节（也没有需要重建的目标）。

### 7.1 本轮发现并修掉的三个判据缺陷（判据自己的账）

1. **`duplicateTicks>=1` 是随机器负载的判据（同轮修掉，`d063747`）**：播放器的同 tick 守卫
   计数只有在"某渲染帧推进了 0 个 tick"（帧率高于 demo 的 66.7 Hz）时才可能非零；帧全部慢于
   tick 率时合法读 0。2026-10-15 首次提交后重跑时，三个并行任务会话共用本机，该断言在
   健康产品上**假红一次**（128 OK / 1 FAIL，其余全绿）。产品无缺陷，是判据在读机器负载。
   修法：播放器级改断言**账户恒等式** `events_seen == events_played + events_skipped +
   events_missing`（与帧钟无关），`duplicateTicks` 保留为打印读数；守卫"不重放"的行为仍由
   census 与内容两节的确定性断言承担（重放同一 tick、必须 0 新增且守卫计数 ≥1）。新增
   `player-accounting` 变异（值运行时读取）。
2. **带标记名字的三态断言过窄（同轮修掉，`9864a93`）**：内容节原断言
   `markerEventsResolved == markerEvents - markerEventsMissing` 暗含"标记事件只有解出/缺失两种
   结局"。第 11 份扫描（cp_aquarius）给出第三种：150 = 140 解出 + 0 缺失 + **10 个其他具名
   状态**（含 MP3 的 unsupported_format）。探针补 `markerEventsOther`，门禁改断言四项恒等式
   `resolved + missing + other == total` 且 `resolved >= 1`。
3. **设备变异以「捕获文件存在」为条件，于是读到了上一次运行的字节（同轮修掉，`f745bcd`）**：
   捕获留在 `$OUT` 里跨运行存活，普通 `--mutation` 因此找到上一轮 `--device` 留下的
   `device-null-trace.txt` / `device-6-trace.txt`，照样变异并报 23 捕获——而本文件记录的是
   `--mutation` 21 / `--device` 23。判据本身没错，**被验证的字节属于另一次运行**，正是本仓库
   反复点名的"PASS 读自错误字节"。修法：条件换成能保证捕获新鲜的 `--device` 标志，缺该标志时
   打印"跳过两个设备变异"说明原因（M''' 实测 21/21 并打印该行，D''' 实测 23/23）。

## 8. 证据路径与哈希

`evidence/audio/`（提交 B）：

| 文件 | 内容 |
|---|---|
| `content-inputs.txt` | 内容节 pinned 输入（3 份录制的 size+sha256） |
| `content-pov.json` / `content-pov.tsv` | pov 内容映射读数与逐事件映射（tsv 为 5323 数据行 + 4 行 `#` 注释头） |
| `content-matcha.json` / `content-matcha.tsv` | matcha（5229 数据行 + 4 行头） |
| `content-well.json` / `content-well.tsv` | well（4989 数据行 + 4 行头） |
| `content-scan.txt` | 11 份录制扫描表（类别/表内 music 候选/marker 计数） |
| `marker-reach.txt` | 声音字符缺陷的直接测量：旧转换 0/713 解出，新转换 696/713；含 25 条例证与复现说明 |
| `verify-audio-forward.txt` | F 运行完整日志（129 条逐条 `OK`） |
| `verify-audio-mutation.txt` | M 运行完整日志（21/21 捕获） |
| `verify-audio-device.txt` | D 运行完整日志（设备节 5 条 + 23/23 捕获，断言 134） |
| `device-runs.txt` | 两运行的命令与逐项读数、`winmm_sink_probe --device 6` 原样输出；含两条 `mode: summary` 原文 |
| `listening.txt` | 人耳确认记录（结论、时刻、命令、自动读数、边界） |
| `SHA256SUMS` | 上述文件的 sha256（`sha256sum -c` 复核；fresh checkout 下先 `tr -d '\r'`，见 §11 注） |

## 9. 未验证项与外部阻塞

1. **music 事件**：11 份扫描录制中 0 条 music 事件；表内候选（`tableKindMusic` 13–15/份）证明
   候选源存在，但"某份真实录制真的触发 music 事件"没有输入 → **NOT VERIFIED**（扫描范围=11 份，
   非全语料）。
2. **TempEntity / PCF 声音事件**：所有扫描录制为 0；分类器两 token 保留但无发放名字携带
   （`realTempEntity=0 realPcf=0` 每轮实测）→ 该类别只有合成覆盖，真实内容 **NOT VERIFIED**。
3. **MP3 设备播放**：全部 voice/announcer 是 MP3 → `unsupported_format`（原生 PCM 路径不解码
   MP3）。"MP3 在设备上实际出声"仍是能力缺口，未实现 → **UNSUPPORTED**（沿 T3 上一轮）。
4. **听到的内容对不对**：人耳确认只证明"有声音"；曲目正确性、音量、延迟、空间感未对照 →
   **NOT VERIFIED**。
5. **1690+ 份未扫描录制**：扫描 11 份；其余语料未跑（语料是活的，逐份 pin 住才能复现）。
6. **多设备/多驱动**：只测了本机 device 6（xduoo）；其它设备、独占模式冲突、蓝牙/USB 热插拔
   → **NOT VERIFIED**。
7. **`exe_count=32` 的总链证明**：本任务只证明"本树全量构建打印 exe_count=32"；合并 T1/T2/T4
   后的最终计数属于 T5 的串行总链。

## 10. 能力边界（不是 bug 的部分）

- **「请求被构造、事件被定位、tick 被推进」≠「听见了声音」**。device 6 的 `played=742` 是产品
  自己的计数；人耳确认另记（本次为肯定）。
- **两个同 tick 读数要分开看**：播放器级的 `duplicateTicks` 是**帧时序相关**的计数（实测 33–1637
  随窗口变化，且机器负载重时会合法读 0），本轮起**只打印不断言**（§7.1 第 1 条）；census 与内容节
  的 `duplicateTicksAfter` 是进程内"重放同一 tick"的计数，与帧钟无关，门禁断言 `>=1` 且重放 0 事件。
  守卫行为由后者承担，不是由机器负载承担。
- **`unsupported_format` 对 voice/announcer 的 511 条是真实解码事实**，不是解析失败：它们都是
  MP3，且作为 filtered kinds 不会被调度器解析/播放。
- **声音字符剥离只影响「名字→路径」**；名字本身（映射的 `name` 列）保持录制原文，便于追溯。
- **时长上限 600 s** 是防御性设计（单缓冲占满一个 voice 直到排空）；真实内容最大值 10 s，
  门禁断言两者并记录余量。

## 11. 干净工作树与可运行状态

- 提交 B 后 `git status --short` 干净（见提交信息与最终报告末尾复核输出）。
- 可运行性：`native/build-nmake/tf2_demo_native.exe ... --audio-disabled` rc=0（129 条断言里包含
  真实 demo 端到端）。
- SHA256SUMS 的 CR 注意事项沿用 `T3-MANUAL-FOLLOWUP` §11 的既有先例：本仓库 core.autocrlf=true，
  fresh checkout 用 `tr -d '\r' < SHA256SUMS | sha256sum -c -` 复核。

## 12. 未提交文件与证据来源（`WORKTREE-WORKFLOW.md` 规则 6）

**已提交源码/报告/证据之外的产物**（全部被 `.gitignore` 忽略，全部可由已提交脚本重新生成；
任务结束前已确认本任务没有残留的 `tf2_demo_native.exe` 或脚本进程——其它 worktree 的进程不属于
本任务，规则 5/6）：

| 目录 | 体量 | 内容 |
|---|---|---|
| `.scratch/t3/` | 15 MB / 37 项 | 门禁中间日志、变异用的捕获副本、before/after 对照（`before-*`/`after-*`）、`cl.log`、以及 `marker-reach.txt` 原始数据的一次性测量工具输出 |
| `evidence/fast/audio/` | 19 MB / 68 项 | `verify-audio.sh` 的输出目录（`VERIFY_AUDIO_OUT` 默认）：三份内容映射、census/调度器读数、device 两次运行的 trace 与两张 2.5 MB BMP |
| `native/build-nmake/` | 235 MB | 构建产物（exe / obj / lib） |
| `evidence/cmake-build.log` | — | 全量构建日志 |

**证据来源**，按本报告引用顺序：

1. §2/§3 的**修复后**读数、§5 的三条运行日志、§8 的全部文件 → 已提交 `evidence/audio/`，
   `SHA256SUMS` 可复核。
2. §3 的**修复前**读数（`resolved 2819` / `missingWav 1993`、旧转换解出 0/713 等）→ 用提交 A 的
   父提交 `917b012` 建树重跑同一批 pinned 输入得到，原始输出只留在 `.scratch/t3/before-*` 与
   `reach-pov.txt`；复现方式：`git checkout 917b012` → 构建 → 跑
   `sound_content_map_probe --demo <同一份文件>`（输入清单见 `evidence/audio/content-inputs.txt`）。
   数字同时写进了提交 A 的提交信息，不依赖 scratch。
3. device 6 的**修复前** `played=601 / missing=388` → 已提交文档
   `T3-MANUAL-FOLLOWUP-2026-10-14.md`（上一轮同一 demo、同一 tick 窗口的实测），是本仓库自己的
   记录，不是别处抄来的。
4. §2.1 的 11 份扫描 → 已提交 `evidence/audio/content-scan.txt`；三份 pinned 录制的
   size+sha256 在 `evidence/audio/content-inputs.txt`，缺文件/错大小/错哈希门禁分别报错。
5. 未引用其它任务 worktree 的任何输出；未读取别的任务的 `.scratch`（规则 5）。

**人耳确认**（§2 第 6 项、`listening.txt`）是唯一不属于上述任何一类的证据：它是人的陈述，
不是命令输出，因此单独存放、不与任何计数互相印证。
